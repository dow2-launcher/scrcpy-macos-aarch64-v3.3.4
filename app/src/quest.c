#include "quest.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libavutil/pixfmt.h>
#include <SDL2/SDL_cpuinfo.h>

#include "util/log.h"

#ifndef M_PI
# define M_PI 3.14159265358979323846
#endif

// The seam is computed on a picture downscaled by this factor
#define SC_QUEST_SEAM_SCALE 4

// A small cost is added to seam positions far from the seam of the previous
// frame, to avoid flickering when several paths are almost equivalent
#define SC_QUEST_SEAM_STICKINESS_MAX 8

#define SC_QUEST_MAP_INVALID 0xFFFF
// Coordinates are stored in 1/16 pixel units in 16 bits
#define SC_QUEST_MAX_EYE_SIZE 4094

#define SC_QUEST_ANGLE_MAX 45.f
#define SC_QUEST_K_MAX 1.f
#define SC_QUEST_OVFRAC_MIN 0.5f
#define SC_QUEST_OVFRAC_MAX 0.98f

// View presets: crop of the merged picture, as fractions of its size
struct sc_quest_crop {
    float x0;
    float x1;
    float y0;
    float y1;
};

static const struct sc_quest_crop presets[] = {
    {0.131f, 0.869f, 0.097f, 0.512f}, // A: wide (~1.69:1)
    {0.151f, 0.849f, 0.045f, 0.631f}, // B: (~1.13:1)
    {0.194f, 0.811f, 0.026f, 0.740f}, // C: tall (~0.82:1)
    {0.f, 1.f, 0.f, 1.f},             // D: full picture
};

/* Worker threads */

static int
run_worker(void *data) {
    struct sc_quest_pool *pool = data;

    sc_mutex_lock(&pool->mutex);
    // The worker index is assigned on start
    unsigned index = pool->pending++;
    sc_cond_signal(&pool->cond_done);
    unsigned generation = 0;

    for (;;) {
        while (!pool->stopped && pool->generation[index] == generation) {
            sc_cond_wait(&pool->cond_work, &pool->mutex);
        }
        if (pool->stopped) {
            break;
        }
        generation = pool->generation[index];
        void (*fn)(void *, unsigned, unsigned) = pool->fn;
        void *arg = pool->arg;
        unsigned nbands = pool->count + 1;
        sc_mutex_unlock(&pool->mutex);

        // band 0 is executed by the calling thread
        fn(arg, index + 1, nbands);

        sc_mutex_lock(&pool->mutex);
        assert(pool->pending);
        if (!--pool->pending) {
            sc_cond_signal(&pool->cond_done);
        }
    }
    sc_mutex_unlock(&pool->mutex);

    return 0;
}

static bool
sc_quest_pool_init(struct sc_quest_pool *pool) {
    if (!sc_mutex_init(&pool->mutex)) {
        return false;
    }
    if (!sc_cond_init(&pool->cond_work)) {
        sc_mutex_destroy(&pool->mutex);
        return false;
    }
    if (!sc_cond_init(&pool->cond_done)) {
        sc_cond_destroy(&pool->cond_work);
        sc_mutex_destroy(&pool->mutex);
        return false;
    }

    pool->stopped = false;
    pool->pending = 0;
    pool->count = 0;
    pool->fn = NULL;
    pool->arg = NULL;

    int cpus = SDL_GetCPUCount();
    // Keep some cores for the decoder and the rest of the system
    int workers = cpus / 2 - 1;
    if (workers > SC_QUEST_MAX_WORKERS) {
        workers = SC_QUEST_MAX_WORKERS;
    }

    for (int i = 0; i < workers; ++i) {
        pool->generation[i] = 0;
        bool ok = sc_thread_create(&pool->threads[pool->count], run_worker,
                                   "scrcpy-quest", pool);
        if (!ok) {
            // Not fatal, just use less threads
            break;
        }
        ++pool->count;
    }

    // Wait for all the workers to get their index
    sc_mutex_lock(&pool->mutex);
    while (pool->pending < pool->count) {
        sc_cond_wait(&pool->cond_done, &pool->mutex);
    }
    pool->pending = 0;
    sc_mutex_unlock(&pool->mutex);

    return true;
}

static void
sc_quest_pool_destroy(struct sc_quest_pool *pool) {
    sc_mutex_lock(&pool->mutex);
    pool->stopped = true;
    sc_cond_broadcast(&pool->cond_work);
    sc_mutex_unlock(&pool->mutex);

    for (unsigned i = 0; i < pool->count; ++i) {
        sc_thread_join(&pool->threads[i], NULL);
    }

    sc_cond_destroy(&pool->cond_done);
    sc_cond_destroy(&pool->cond_work);
    sc_mutex_destroy(&pool->mutex);
}

// Execute fn(arg, band, nbands) for each band in [0, nbands), in parallel
static void
sc_quest_pool_run(struct sc_quest_pool *pool,
                  void (*fn)(void *arg, unsigned band, unsigned nbands),
                  void *arg) {
    unsigned nbands = pool->count + 1;

    if (pool->count) {
        sc_mutex_lock(&pool->mutex);
        pool->fn = fn;
        pool->arg = arg;
        pool->pending = pool->count;
        for (unsigned i = 0; i < pool->count; ++i) {
            ++pool->generation[i];
        }
        sc_cond_broadcast(&pool->cond_work);
        sc_mutex_unlock(&pool->mutex);
    }

    fn(arg, 0, nbands);

    if (pool->count) {
        sc_mutex_lock(&pool->mutex);
        while (pool->pending) {
            sc_cond_wait(&pool->cond_done, &pool->mutex);
        }
        sc_mutex_unlock(&pool->mutex);
    }
}

/* Parameters */

static float
clampf(float value, float min, float max) {
    return value < min ? min : value > max ? max : value;
}

static void
sc_quest_sanitize_params(struct sc_quest_params *params) {
    params->angle = clampf(params->angle, -SC_QUEST_ANGLE_MAX,
                           SC_QUEST_ANGLE_MAX);
    params->k1 = clampf(params->k1, -SC_QUEST_K_MAX, SC_QUEST_K_MAX);
    params->k2 = clampf(params->k2, -SC_QUEST_K_MAX, SC_QUEST_K_MAX);
    params->ovfrac = clampf(params->ovfrac, SC_QUEST_OVFRAC_MIN,
                            SC_QUEST_OVFRAC_MAX);
    if (params->preset < SC_QUEST_PRESET_A
            || params->preset > SC_QUEST_PRESET_D) {
        params->preset = SC_QUEST_PRESET_B;
    }
}

bool
sc_quest_init(struct sc_quest *q, const struct sc_quest_params *params) {
    memset(q, 0, sizeof(*q));

    q->params = *params;
    sc_quest_sanitize_params(&q->params);
    q->initial_params = q->params;
    q->processing = true;
    q->overlay = true;
    q->overlay_dirty = true;

    q->out = av_frame_alloc();
    if (!q->out) {
        LOG_OOM();
        return false;
    }

    if (!sc_quest_pool_init(&q->pool)) {
        av_frame_free(&q->out);
        return false;
    }

    LOGI("Quest mode enabled (angle=%.1f k1=%.3f k2=%.3f ovfrac=%.3f "
         "preset=%c, %u threads)", q->params.angle, q->params.k1, q->params.k2,
         q->params.ovfrac, 'A' + q->params.preset, q->pool.count + 1);

    return true;
}

void
sc_quest_destroy(struct sc_quest *q) {
    sc_quest_pool_destroy(&q->pool);
    av_frame_free(&q->out);
    free(q->map[0]);
    free(q->map[1]);
    free(q->seam);
    free(q->seam_low);
    free(q->dp_prev);
    free(q->dp_cur);
    free(q->dp_back);
    free(q->seam_diff);
}

/* Geometry */

struct sc_quest_geometry {
    int ew;
    int eh;
    int ov;
    int off;
    int out_w;
    int crop_x;
    int crop_y;
    int crop_w;
    int crop_h;
};

static bool
sc_quest_compute_geometry(const struct sc_quest_params *params, int src_w,
                          int src_h, struct sc_quest_geometry *g) {
    int ew = src_w / 2;
    int eh = src_h;
    if (ew < 32 || eh < 32 || ew > SC_QUEST_MAX_EYE_SIZE
            || eh > SC_QUEST_MAX_EYE_SIZE) {
        return false;
    }

    g->ew = ew;
    g->eh = eh;
    g->ov = (int) (ew * params->ovfrac);
    if (g->ov < SC_QUEST_SEAM_SCALE * 2) {
        g->ov = SC_QUEST_SEAM_SCALE * 2;
    }
    if (g->ov > ew) {
        g->ov = ew;
    }
    g->off = ew - g->ov;
    g->out_w = ew + g->off;

    const struct sc_quest_crop *preset = &presets[params->preset];
    // The crop rectangle must have even position and dimensions (YUV 4:2:0)
    int x0 = (int) lroundf(preset->x0 * g->out_w) & ~1;
    int y0 = (int) lroundf(preset->y0 * eh) & ~1;
    int w = (int) lroundf((preset->x1 - preset->x0) * g->out_w) & ~1;
    int h = (int) lroundf((preset->y1 - preset->y0) * eh) & ~1;
    if (x0 + w > g->out_w) {
        w = (g->out_w - x0) & ~1;
    }
    if (y0 + h > eh) {
        h = (eh - y0) & ~1;
    }
    if (w < 2 || h < 2) {
        return false;
    }

    g->crop_x = x0;
    g->crop_y = y0;
    g->crop_w = w;
    g->crop_h = h;
    return true;
}

struct sc_size
sc_quest_get_output_size(struct sc_quest *q, struct sc_size src_size) {
    struct sc_quest_geometry g;
    if (!q->processing
            || !sc_quest_compute_geometry(&q->params, src_size.width,
                                          src_size.height, &g)) {
        return src_size;
    }

    struct sc_size size = {
        .width = g.crop_w,
        .height = g.crop_h,
    };
    return size;
}

/* Remap tables */

struct sc_quest_map_job {
    struct sc_quest *q;
};

static void
build_map_band(void *arg, unsigned band, unsigned nbands) {
    struct sc_quest_map_job *job = arg;
    struct sc_quest *q = job->q;

    int ew = q->ew;
    int eh = q->eh;
    int y_start = (int) ((int64_t) eh * band / nbands);
    int y_end = (int) ((int64_t) eh * (band + 1) / nbands);

    float cx = ew / 2.f;
    float cy = eh / 2.f;
    float inv_nd = 1.f / (cx * cx + cy * cy);
    float k1 = q->params.k1;
    float k2 = q->params.k2;

    // The last valid fixed-point coordinates (the bilinear interpolation
    // reads the next pixel and the next row)
    int max_fx = (ew - 2) * 16 + 15;
    int max_fy = (eh - 2) * 16 + 15;

    for (int eye = 0; eye < 2; ++eye) {
        // Left eye: +angle, right eye: -angle (mirrored).
        // The result is rotate(lenscorrected, +angle), so for each output
        // pixel: un-rotate by -angle, then apply the lens correction.
        double a = (eye ? -1 : 1) * q->params.angle * M_PI / 180.0;
        float ca = (float) cos(-a);
        float sa = (float) sin(-a);

        for (int oy = y_start; oy < y_end; ++oy) {
            uint16_t *m = q->map[eye] + (size_t) oy * ew * 2;
            float dy = oy - cy;
            for (int ox = 0; ox < ew; ++ox) {
                float dx = ox - cx;
                float ux = dx * ca - dy * sa;
                float uy = dx * sa + dy * ca;
                float r2 = (ux * ux + uy * uy) * inv_nd;
                float k = 1.f + k1 * r2 + k2 * r2 * r2;
                float sx = cx + ux * k;
                float sy = cy + uy * k;
                // The rotation is applied to the lens-corrected eye picture:
                // what falls outside this (un-rotated) picture is black, like
                // with ffmpeg lenscorrection+rotate filters (so the merged
                // picture is an octagon)
                bool inside = ux >= -cx && ux <= ew - 1 - cx
                           && uy >= -cy && uy <= eh - 1 - cy;
                if (inside && sx >= 0 && sy >= 0 && sx <= ew - 1
                        && sy <= eh - 1) {
                    int fx = (int) (sx * 16.f + .5f);
                    int fy = (int) (sy * 16.f + .5f);
                    if (fx > max_fx) {
                        fx = max_fx;
                    }
                    if (fy > max_fy) {
                        fy = max_fy;
                    }
                    m[0] = fx;
                    m[1] = fy;
                } else {
                    m[0] = SC_QUEST_MAP_INVALID;
                    m[1] = SC_QUEST_MAP_INVALID;
                }
                m += 2;
            }
        }
    }
}

// Update the geometry, the remap tables and the buffers for the current
// parameters and source size
static bool
sc_quest_configure(struct sc_quest *q, int src_w, int src_h) {
    struct sc_quest_geometry g;
    if (!sc_quest_compute_geometry(&q->params, src_w, src_h, &g)) {
        return false;
    }

    q->src_w = src_w;
    q->src_h = src_h;
    q->ew = g.ew;
    q->eh = g.eh;
    q->ov = g.ov;
    q->off = g.off;
    q->out_w = g.out_w;
    q->crop.x = g.crop_x;
    q->crop.y = g.crop_y;
    q->crop.w = g.crop_w;
    q->crop.h = g.crop_h;

    bool size_changed = q->map_ew != g.ew || q->map_eh != g.eh;
    if (size_changed) {
        size_t map_size = sizeof(uint16_t) * 2 * g.ew * g.eh;
        uint16_t *map0 = realloc(q->map[0], map_size);
        if (map0) {
            q->map[0] = map0;
        }
        uint16_t *map1 = realloc(q->map[1], map_size);
        if (map1) {
            q->map[1] = map1;
        }
        int *seam = realloc(q->seam, sizeof(int) * g.eh);
        if (seam) {
            q->seam = seam;
        }
        q->map_ew = 0;
        q->map_eh = 0;
        if (!map0 || !map1 || !seam) {
            LOG_OOM();
            return false;
        }
    }

    if (size_changed || q->map_angle != q->params.angle
            || q->map_k1 != q->params.k1 || q->map_k2 != q->params.k2) {
        struct sc_quest_map_job job = {
            .q = q,
        };
        sc_quest_pool_run(&q->pool, build_map_band, &job);
        q->map_ew = g.ew;
        q->map_eh = g.eh;
        q->map_angle = q->params.angle;
        q->map_k1 = q->params.k1;
        q->map_k2 = q->params.k2;
        q->seam_low_valid = false;
    }

    int low_w = g.ov / SC_QUEST_SEAM_SCALE;
    int low_h = (g.eh + SC_QUEST_SEAM_SCALE - 1) / SC_QUEST_SEAM_SCALE;
    if (low_w != q->low_w || low_h != q->low_h) {
        q->low_w = 0;
        q->low_h = 0;
        q->seam_low_valid = false;

        int *seam_low = realloc(q->seam_low, sizeof(int) * low_h);
        if (seam_low) {
            q->seam_low = seam_low;
        }
        int32_t *dp_prev = realloc(q->dp_prev, sizeof(int32_t) * low_w);
        if (dp_prev) {
            q->dp_prev = dp_prev;
        }
        int32_t *dp_cur = realloc(q->dp_cur, sizeof(int32_t) * low_w);
        if (dp_cur) {
            q->dp_cur = dp_cur;
        }
        int8_t *dp_back = realloc(q->dp_back, (size_t) low_w * low_h);
        if (dp_back) {
            q->dp_back = dp_back;
        }
        uint8_t *seam_diff = realloc(q->seam_diff, (size_t) low_w * low_h);
        if (seam_diff) {
            q->seam_diff = seam_diff;
        }
        if (!seam_low || !dp_prev || !dp_cur || !dp_back || !seam_diff) {
            LOG_OOM();
            return false;
        }
        q->low_w = low_w;
        q->low_h = low_h;
    }

    AVFrame *out = q->out;
    if (out->width != g.crop_w || out->height != g.crop_h || !out->data[0]) {
        av_frame_unref(out);
        out->format = AV_PIX_FMT_YUV420P;
        out->width = g.crop_w;
        out->height = g.crop_h;
        int r = av_frame_get_buffer(out, 0);
        if (r) {
            LOG_OOM();
            out->width = 0;
            out->height = 0;
            return false;
        }
        LOGI("Quest output: %dx%d (merged %dx%d, eye %dx%d, overlap %d)",
             g.crop_w, g.crop_h, g.out_w, g.eh, g.ew, g.eh, g.ov);
    }

    return true;
}

/* Seam */

static inline int
sample_nearest(const uint16_t *m, const uint8_t *plane, int linesize) {
    if (m[0] == SC_QUEST_MAP_INVALID) {
        // like a black pixel
        return 0;
    }
    return plane[(size_t) (m[1] >> 4) * linesize + (m[0] >> 4)];
}

struct sc_quest_seam_job {
    struct sc_quest *q;
    const AVFrame *in;
};

// Compute the absolute differences between the left and right eyes in the
// overlap area, at low resolution on the luma plane
static void
seam_diff_band(void *arg, unsigned band, unsigned nbands) {
    struct sc_quest_seam_job *job = arg;
    struct sc_quest *q = job->q;
    const AVFrame *frame = job->in;

    const int scale = SC_QUEST_SEAM_SCALE;
    int ew = q->ew;
    int eh = q->eh;
    int off = q->off;
    int low_w = q->low_w;
    int low_h = q->low_h;

    const uint8_t *plane_l = frame->data[0];
    const uint8_t *plane_r = frame->data[0] + ew;
    int linesize = frame->linesize[0];

    int ly_start = (int) ((int64_t) low_h * band / nbands);
    int ly_end = (int) ((int64_t) low_h * (band + 1) / nbands);

    for (int ly = ly_start; ly < ly_end; ++ly) {
        int y = ly * scale + scale / 2;
        if (y >= eh) {
            y = eh - 1;
        }
        const uint16_t *ml = q->map[0] + ((size_t) y * ew + off) * 2;
        const uint16_t *mr = q->map[1] + (size_t) y * ew * 2;
        uint8_t *diff = q->seam_diff + (size_t) ly * low_w;

        for (int lx = 0; lx < low_w; ++lx) {
            int x = lx * scale;
            int l = sample_nearest(ml + x * 2, plane_l, linesize);
            int r = sample_nearest(mr + x * 2, plane_r, linesize);
            diff[lx] = abs(l - r);
        }
    }
}

// Find the optimal seam in the overlap area by dynamic programming: the path
// from top to bottom (moving by at most 1 horizontally per row) minimizing the
// sum of absolute differences between the left and right eyes.
//
// For performance, it is computed at low resolution on the luma plane, then
// interpolated.
static void
sc_quest_compute_seam(struct sc_quest *q, const AVFrame *frame) {
    const int scale = SC_QUEST_SEAM_SCALE;
    int eh = q->eh;
    int low_w = q->low_w;
    int low_h = q->low_h;

    struct sc_quest_seam_job job = {
        .q = q,
        .in = frame,
    };
    sc_quest_pool_run(&q->pool, seam_diff_band, &job);

    int32_t *prev = q->dp_prev;
    int32_t *cur = q->dp_cur;
    bool sticky = q->seam_low_valid;

    for (int ly = 0; ly < low_h; ++ly) {
        const uint8_t *diff = q->seam_diff + (size_t) ly * low_w;
        int8_t *back = q->dp_back + (size_t) ly * low_w;
        int previous_seam = sticky ? q->seam_low[ly] : 0;

        for (int lx = 0; lx < low_w; ++lx) {
            int32_t d = diff[lx];
            if (sticky) {
                int dist = abs(lx - previous_seam);
                d += dist < SC_QUEST_SEAM_STICKINESS_MAX
                   ? dist : SC_QUEST_SEAM_STICKINESS_MAX;
            }

            if (ly == 0) {
                cur[lx] = d;
                back[lx] = 0;
            } else {
                int32_t best = prev[lx];
                int8_t b = 0;
                if (lx > 0 && prev[lx - 1] < best) {
                    best = prev[lx - 1];
                    b = -1;
                }
                if (lx < low_w - 1 && prev[lx + 1] < best) {
                    best = prev[lx + 1];
                    b = 1;
                }
                cur[lx] = d + best;
                back[lx] = b;
            }
        }

        int32_t *tmp = prev;
        prev = cur;
        cur = tmp;
    }

    // prev contains the last row
    int sx = 0;
    for (int lx = 1; lx < low_w; ++lx) {
        if (prev[lx] < prev[sx]) {
            sx = lx;
        }
    }

    int *seam_low = q->seam_low;
    seam_low[low_h - 1] = sx;
    for (int ly = low_h - 2; ly >= 0; --ly) {
        sx += q->dp_back[(size_t) (ly + 1) * low_w + sx];
        if (sx < 0) {
            sx = 0;
        } else if (sx >= low_w) {
            sx = low_w - 1;
        }
        seam_low[ly] = sx;
    }
    q->seam_low_valid = true;

    // Interpolate to full resolution
    int *seam = q->seam;
    int max = q->ov - 1;
    for (int y = 0; y < eh; ++y) {
        // low-res row ly is centered on y = ly * scale + scale / 2
        int t = y - scale / 2;
        if (t < 0) {
            t = 0;
        }
        int ly0 = t / scale;
        int frac = t % scale;
        int ly1 = ly0 + 1;
        if (ly0 >= low_h - 1) {
            ly0 = low_h - 1;
            ly1 = ly0;
        }
        int s = (seam_low[ly0] * (scale - frac) + seam_low[ly1] * frac)
              + scale / 2;
        seam[y] = s > max ? max : s;
    }
}

/* Remap */

static inline void
remap_luma(uint8_t *dst, int n, const uint16_t *m, const uint8_t *src,
           int linesize, uint8_t black) {
    for (int i = 0; i < n; ++i, m += 2) {
        unsigned mx = m[0];
        if (mx == SC_QUEST_MAP_INVALID) {
            dst[i] = black;
            continue;
        }
        unsigned my = m[1];
        const uint8_t *p = src + (size_t) (my >> 4) * linesize + (mx >> 4);
        unsigned fx = mx & 15;
        unsigned fy = my & 15;
        unsigned top = p[0] * (16 - fx) + p[1] * fx;
        unsigned bottom = p[linesize] * (16 - fx) + p[linesize + 1] * fx;
        dst[i] = (top * (16 - fy) + bottom * fy + 128) >> 8;
    }
}

// The map contains luma coordinates: the chroma coordinates are halved
static inline void
remap_chroma(uint8_t *dst_u, uint8_t *dst_v, int n, const uint16_t *m,
             const uint8_t *src_u, const uint8_t *src_v, int linesize_u,
             int linesize_v, unsigned max_cx, unsigned max_cy) {
    for (int i = 0; i < n; ++i, m += 4) {
        unsigned mx = m[0];
        if (mx == SC_QUEST_MAP_INVALID) {
            dst_u[i] = 128;
            dst_v[i] = 128;
            continue;
        }
        mx >>= 1;
        unsigned my = m[1] >> 1;
        if (mx > max_cx) {
            mx = max_cx;
        }
        if (my > max_cy) {
            my = max_cy;
        }
        unsigned fx = mx & 15;
        unsigned fy = my & 15;
        unsigned ix = mx >> 4;
        unsigned iy = my >> 4;

        const uint8_t *p = src_u + (size_t) iy * linesize_u + ix;
        unsigned top = p[0] * (16 - fx) + p[1] * fx;
        unsigned bottom = p[linesize_u] * (16 - fx) + p[linesize_u + 1] * fx;
        dst_u[i] = (top * (16 - fy) + bottom * fy + 128) >> 8;

        p = src_v + (size_t) iy * linesize_v + ix;
        top = p[0] * (16 - fx) + p[1] * fx;
        bottom = p[linesize_v] * (16 - fx) + p[linesize_v + 1] * fx;
        dst_v[i] = (top * (16 - fy) + bottom * fy + 128) >> 8;
    }
}

struct sc_quest_remap_job {
    struct sc_quest *q;
    const AVFrame *in;
};

static void
remap_band(void *arg, unsigned band, unsigned nbands) {
    struct sc_quest_remap_job *job = arg;
    struct sc_quest *q = job->q;
    const AVFrame *in = job->in;
    AVFrame *out = q->out;

    int ew = q->ew;
    int off = q->off;
    int crop_x = q->crop.x;
    int crop_y = q->crop.y;
    int w = q->crop.w;
    int ch = q->crop.h / 2;
    int cw = w / 2;

    // Split on chroma rows (one chroma row = two luma rows)
    int cy_start = (int) ((int64_t) ch * band / nbands);
    int cy_end = (int) ((int64_t) ch * (band + 1) / nbands);

    uint8_t black = in->color_range == AVCOL_RANGE_JPEG ? 0 : 16;

    const uint8_t *src_y[2] = {in->data[0], in->data[0] + ew};
    int linesize_y = in->linesize[0];

    for (int oy = cy_start * 2; oy < cy_end * 2; ++oy) {
        int y = crop_y + oy;
        // First column (in the merged picture) taken from the right eye
        int cut = off + q->seam[y];
        int nl = cut - crop_x;
        if (nl < 0) {
            nl = 0;
        } else if (nl > w) {
            nl = w;
        }

        uint8_t *dst = out->data[0] + (size_t) oy * out->linesize[0];
        const uint16_t *row_l = q->map[0] + (size_t) y * ew * 2;
        const uint16_t *row_r = q->map[1] + (size_t) y * ew * 2;
        remap_luma(dst, nl, row_l + crop_x * 2, src_y[0], linesize_y, black);
        remap_luma(dst + nl, w - nl, row_r + (crop_x + nl - off) * 2, src_y[1],
                   linesize_y, black);
    }

    int cew = ew / 2;
    int ceh = q->eh / 2;
    unsigned max_cx = (cew - 2) * 16 + 15;
    unsigned max_cy = (ceh - 2) * 16 + 15;
    const uint8_t *src_u[2] = {in->data[1], in->data[1] + cew};
    const uint8_t *src_v[2] = {in->data[2], in->data[2] + cew};
    int linesize_u = in->linesize[1];
    int linesize_v = in->linesize[2];

    for (int oc = cy_start; oc < cy_end; ++oc) {
        int y = crop_y + oc * 2;
        int cut = off + q->seam[y];
        // number of chroma pixels taken from the left eye
        int nl = (cut - crop_x + 1) / 2;
        if (nl < 0) {
            nl = 0;
        } else if (nl > cw) {
            nl = cw;
        }

        uint8_t *dst_u = out->data[1] + (size_t) oc * out->linesize[1];
        uint8_t *dst_v = out->data[2] + (size_t) oc * out->linesize[2];
        const uint16_t *row_l = q->map[0] + (size_t) y * ew * 2;
        const uint16_t *row_r = q->map[1] + (size_t) y * ew * 2;
        remap_chroma(dst_u, dst_v, nl, row_l + crop_x * 2, src_u[0], src_v[0],
                     linesize_u, linesize_v, max_cx, max_cy);
        remap_chroma(dst_u + nl, dst_v + nl, cw - nl,
                     row_r + (crop_x + nl * 2 - off) * 2, src_u[1], src_v[1],
                     linesize_u, linesize_v, max_cx, max_cy);
    }
}

const AVFrame *
sc_quest_process(struct sc_quest *q, const AVFrame *frame) {
    if (!q->processing) {
        return frame;
    }

    if (frame->format != AV_PIX_FMT_YUV420P) {
        return frame;
    }

    sc_tick start = sc_tick_now();

    if (!sc_quest_configure(q, frame->width, frame->height)) {
        // Not applicable to this frame size (or out of memory)
        return frame;
    }

    sc_quest_compute_seam(q, frame);

    struct sc_quest_remap_job job = {
        .q = q,
        .in = frame,
    };
    sc_quest_pool_run(&q->pool, remap_band, &job);

    AVFrame *out = q->out;
    out->color_range = frame->color_range;
    out->colorspace = frame->colorspace;
    out->pts = frame->pts;

    sc_tick now = sc_tick_now();
    q->last_ms = (now - start) / 1000.f;
    q->avg_ms = q->avg_ms ? q->avg_ms * .95f + q->last_ms * .05f : q->last_ms;

    if (q->overlay && now - q->overlay_date >= SC_TICK_FROM_SEC(1)) {
        // Refresh the duration displayed in the overlay
        q->overlay_dirty = true;
    }

    return out;
}

/* Keys */

bool
sc_quest_is_key(SDL_Scancode scancode) {
    switch (scancode) {
        case SDL_SCANCODE_1:
        case SDL_SCANCODE_2:
        case SDL_SCANCODE_3:
        case SDL_SCANCODE_4:
        case SDL_SCANCODE_0:
        case SDL_SCANCODE_LEFTBRACKET:
        case SDL_SCANCODE_RIGHTBRACKET:
        case SDL_SCANCODE_MINUS:
        case SDL_SCANCODE_EQUALS:
        case SDL_SCANCODE_SEMICOLON:
        case SDL_SCANCODE_APOSTROPHE:
        case SDL_SCANCODE_COMMA:
        case SDL_SCANCODE_PERIOD:
        case SDL_SCANCODE_BACKSLASH:
        case SDL_SCANCODE_SLASH:
            return true;
        default:
            return false;
    }
}

static float
step(float value, float delta, float unit) {
    // Avoid accumulating floating-point errors
    return roundf((value + delta) / unit) * unit;
}

bool
sc_quest_handle_key(struct sc_quest *q, SDL_Scancode scancode, bool shift) {
    struct sc_quest_params *p = &q->params;
    float angle_step = shift ? 1.f : .1f;
    float k_step = shift ? .05f : .01f;
    float ov_step = shift ? .01f : .002f;

    switch (scancode) {
        case SDL_SCANCODE_1:
            p->preset = SC_QUEST_PRESET_A;
            break;
        case SDL_SCANCODE_2:
            p->preset = SC_QUEST_PRESET_B;
            break;
        case SDL_SCANCODE_3:
            p->preset = SC_QUEST_PRESET_C;
            break;
        case SDL_SCANCODE_4:
            p->preset = SC_QUEST_PRESET_D;
            break;
        case SDL_SCANCODE_0:
            *p = q->initial_params;
            break;
        case SDL_SCANCODE_LEFTBRACKET:
            p->angle = step(p->angle, -angle_step, .1f);
            break;
        case SDL_SCANCODE_RIGHTBRACKET:
            p->angle = step(p->angle, angle_step, .1f);
            break;
        case SDL_SCANCODE_MINUS:
            p->k1 = step(p->k1, -k_step, .01f);
            break;
        case SDL_SCANCODE_EQUALS:
            p->k1 = step(p->k1, k_step, .01f);
            break;
        case SDL_SCANCODE_SEMICOLON:
            p->k2 = step(p->k2, -k_step, .01f);
            break;
        case SDL_SCANCODE_APOSTROPHE:
            p->k2 = step(p->k2, k_step, .01f);
            break;
        case SDL_SCANCODE_COMMA:
            p->ovfrac = step(p->ovfrac, -ov_step, .002f);
            break;
        case SDL_SCANCODE_PERIOD:
            p->ovfrac = step(p->ovfrac, ov_step, .002f);
            break;
        case SDL_SCANCODE_BACKSLASH:
            q->processing = !q->processing;
            break;
        case SDL_SCANCODE_SLASH:
            q->overlay = !q->overlay;
            break;
        default:
            return false;
    }

    sc_quest_sanitize_params(p);
    q->overlay_dirty = true;

    if (q->processing) {
        LOGI("Quest: preset=%c angle=%.1f k1=%.3f k2=%.3f ovfrac=%.3f",
             'A' + p->preset, p->angle, p->k1, p->k2, p->ovfrac);
    } else {
        LOGI("Quest: processing disabled (raw buffer)");
    }

    return true;
}

/* Overlay */

bool
sc_quest_consume_overlay_dirty(struct sc_quest *q) {
    bool dirty = q->overlay_dirty;
    q->overlay_dirty = false;
    return dirty;
}

// Classic 5x7 font, ASCII 0x20..0x5F, 5 columns per glyph (LSB = top row)
static const uint8_t font5x7[64][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00}, // space
    {0x00, 0x00, 0x5F, 0x00, 0x00}, // !
    {0x00, 0x07, 0x00, 0x07, 0x00}, // "
    {0x14, 0x7F, 0x14, 0x7F, 0x14}, // #
    {0x24, 0x2A, 0x7F, 0x2A, 0x12}, // $
    {0x23, 0x13, 0x08, 0x64, 0x62}, // %
    {0x36, 0x49, 0x55, 0x22, 0x50}, // &
    {0x00, 0x05, 0x03, 0x00, 0x00}, // '
    {0x00, 0x1C, 0x22, 0x41, 0x00}, // (
    {0x00, 0x41, 0x22, 0x1C, 0x00}, // )
    {0x14, 0x08, 0x3E, 0x08, 0x14}, // *
    {0x08, 0x08, 0x3E, 0x08, 0x08}, // +
    {0x00, 0x50, 0x30, 0x00, 0x00}, // ,
    {0x08, 0x08, 0x08, 0x08, 0x08}, // -
    {0x00, 0x60, 0x60, 0x00, 0x00}, // .
    {0x20, 0x10, 0x08, 0x04, 0x02}, // /
    {0x3E, 0x51, 0x49, 0x45, 0x3E}, // 0
    {0x00, 0x42, 0x7F, 0x40, 0x00}, // 1
    {0x42, 0x61, 0x51, 0x49, 0x46}, // 2
    {0x21, 0x41, 0x45, 0x4B, 0x31}, // 3
    {0x18, 0x14, 0x12, 0x7F, 0x10}, // 4
    {0x27, 0x45, 0x45, 0x45, 0x39}, // 5
    {0x3C, 0x4A, 0x49, 0x49, 0x30}, // 6
    {0x01, 0x71, 0x09, 0x05, 0x03}, // 7
    {0x36, 0x49, 0x49, 0x49, 0x36}, // 8
    {0x06, 0x49, 0x49, 0x29, 0x1E}, // 9
    {0x00, 0x36, 0x36, 0x00, 0x00}, // :
    {0x00, 0x56, 0x36, 0x00, 0x00}, // ;
    {0x08, 0x14, 0x22, 0x41, 0x00}, // <
    {0x14, 0x14, 0x14, 0x14, 0x14}, // =
    {0x00, 0x41, 0x22, 0x14, 0x08}, // >
    {0x02, 0x01, 0x51, 0x09, 0x06}, // ?
    {0x32, 0x49, 0x79, 0x41, 0x3E}, // @
    {0x7E, 0x11, 0x11, 0x11, 0x7E}, // A
    {0x7F, 0x49, 0x49, 0x49, 0x36}, // B
    {0x3E, 0x41, 0x41, 0x41, 0x22}, // C
    {0x7F, 0x41, 0x41, 0x22, 0x1C}, // D
    {0x7F, 0x49, 0x49, 0x49, 0x41}, // E
    {0x7F, 0x09, 0x09, 0x09, 0x01}, // F
    {0x3E, 0x41, 0x49, 0x49, 0x7A}, // G
    {0x7F, 0x08, 0x08, 0x08, 0x7F}, // H
    {0x00, 0x41, 0x7F, 0x41, 0x00}, // I
    {0x20, 0x40, 0x41, 0x3F, 0x01}, // J
    {0x7F, 0x08, 0x14, 0x22, 0x41}, // K
    {0x7F, 0x40, 0x40, 0x40, 0x40}, // L
    {0x7F, 0x02, 0x0C, 0x02, 0x7F}, // M
    {0x7F, 0x04, 0x08, 0x10, 0x7F}, // N
    {0x3E, 0x41, 0x41, 0x41, 0x3E}, // O
    {0x7F, 0x09, 0x09, 0x09, 0x06}, // P
    {0x3E, 0x41, 0x51, 0x21, 0x5E}, // Q
    {0x7F, 0x09, 0x19, 0x29, 0x46}, // R
    {0x46, 0x49, 0x49, 0x49, 0x31}, // S
    {0x01, 0x01, 0x7F, 0x01, 0x01}, // T
    {0x3F, 0x40, 0x40, 0x40, 0x3F}, // U
    {0x1F, 0x20, 0x40, 0x20, 0x1F}, // V
    {0x3F, 0x40, 0x38, 0x40, 0x3F}, // W
    {0x63, 0x14, 0x08, 0x14, 0x63}, // X
    {0x07, 0x08, 0x70, 0x08, 0x07}, // Y
    {0x61, 0x51, 0x49, 0x45, 0x43}, // Z
    {0x00, 0x7F, 0x41, 0x41, 0x00}, // [
    {0x02, 0x04, 0x08, 0x10, 0x20}, // backslash
    {0x00, 0x41, 0x41, 0x7F, 0x00}, // ]
    {0x04, 0x02, 0x01, 0x02, 0x04}, // ^
    {0x40, 0x40, 0x40, 0x40, 0x40}, // _
};

#define FONT_CELL_W 6
#define FONT_CELL_H 10
#define OVERLAY_PADDING 4
#define OVERLAY_BG 0xB0000000
#define OVERLAY_FG 0xFFFFFFFF

static uint32_t *
render_text(const char *text, int *out_w, int *out_h) {
    int cols = 0;
    int rows = 1;
    int col = 0;
    for (const char *c = text; *c; ++c) {
        if (*c == '\n') {
            ++rows;
            col = 0;
        } else if (++col > cols) {
            cols = col;
        }
    }

    int w = cols * FONT_CELL_W + 2 * OVERLAY_PADDING;
    int h = rows * FONT_CELL_H + 2 * OVERLAY_PADDING - (FONT_CELL_H - 7);
    uint32_t *pixels = malloc(sizeof(uint32_t) * w * h);
    if (!pixels) {
        LOG_OOM();
        return NULL;
    }

    for (int i = 0; i < w * h; ++i) {
        pixels[i] = OVERLAY_BG;
    }

    int x = OVERLAY_PADDING;
    int y = OVERLAY_PADDING;
    for (const char *c = text; *c; ++c) {
        if (*c == '\n') {
            x = OVERLAY_PADDING;
            y += FONT_CELL_H;
            continue;
        }
        int ch = (unsigned char) *c;
        if (ch >= 'a' && ch <= 'z') {
            ch -= 'a' - 'A';
        }
        if (ch < 0x20 || ch > 0x5F) {
            ch = '?';
        }
        const uint8_t *glyph = font5x7[ch - 0x20];
        for (int gx = 0; gx < 5; ++gx) {
            for (int gy = 0; gy < 7; ++gy) {
                if (glyph[gx] & (1 << gy)) {
                    pixels[(y + gy) * w + x + gx] = OVERLAY_FG;
                }
            }
        }
        x += FONT_CELL_W;
    }

    *out_w = w;
    *out_h = h;
    return pixels;
}

uint32_t *
sc_quest_render_overlay(struct sc_quest *q, int *w, int *h) {
    if (!q->overlay) {
        return NULL;
    }

    q->overlay_date = sc_tick_now();

    const struct sc_quest_params *p = &q->params;
    char text[512];
    if (q->processing) {
        snprintf(text, sizeof(text),
                 "QUEST VIEW %c  ANGLE %.1f  K1 %.2f  K2 %.2f  OVERLAP %.3f"
                 "  %dX%d  %.1f MS\n"
                 "1-4 VIEW  [ ] ANGLE  - = K1  ; ' K2  , . OVERLAP"
                 "  \\ RAW  0 RESET  / HIDE",
                 'A' + p->preset, p->angle, p->k1, p->k2, p->ovfrac,
                 q->crop.w, q->crop.h, q->avg_ms);
    } else {
        snprintf(text, sizeof(text),
                 "QUEST RAW BUFFER (PROCESSING OFF)\n"
                 "\\ PROCESS  / HIDE");
    }

    return render_text(text, w, h);
}
