#include "common.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>

#include "quest.h"
#include "util/tick.h"

#define SRC_W 4128
#define SRC_H 2208

static const struct sc_quest_params default_params = {
    .angle = 20.9f,
    .k1 = -0.26f,
    .k2 = -0.05f,
    .ovfrac = 0.844f,
    .preset = SC_QUEST_PRESET_B,
};

static AVFrame *
alloc_frame(int w, int h) {
    AVFrame *frame = av_frame_alloc();
    assert(frame);
    frame->format = AV_PIX_FMT_YUV420P;
    frame->width = w;
    frame->height = h;
    int r = av_frame_get_buffer(frame, 0);
    assert(!r);
    (void) r;
    return frame;
}

// Luma and chroma of a virtual panorama
static inline uint8_t
pano_y(int x, int y) {
    return (uint8_t) (x * 7 + y * 13 + ((x / 64 + y / 64) & 1) * 90);
}

static inline uint8_t
pano_u(int cx, int cy) {
    return (uint8_t) (cx * 3 + cy * 5);
}

static inline uint8_t
pano_v(int cx, int cy) {
    return (uint8_t) (cx * 11 - cy * 3);
}

// Create a frame where both eyes see the same panorama, the right eye being
// shifted by "off" pixels
static AVFrame *
create_pano_frame(int off) {
    assert(!(off & 1));
    AVFrame *frame = alloc_frame(SRC_W, SRC_H);
    int ew = SRC_W / 2;
    for (int y = 0; y < SRC_H; ++y) {
        uint8_t *row = frame->data[0] + (size_t) y * frame->linesize[0];
        for (int x = 0; x < ew; ++x) {
            row[x] = pano_y(x, y);
            row[ew + x] = pano_y(x + off, y);
        }
    }
    for (int cy = 0; cy < SRC_H / 2; ++cy) {
        uint8_t *row_u = frame->data[1] + (size_t) cy * frame->linesize[1];
        uint8_t *row_v = frame->data[2] + (size_t) cy * frame->linesize[2];
        for (int cx = 0; cx < ew / 2; ++cx) {
            row_u[cx] = pano_u(cx, cy);
            row_u[ew / 2 + cx] = pano_u(cx + off / 2, cy);
            row_v[cx] = pano_v(cx, cy);
            row_v[ew / 2 + cx] = pano_v(cx + off / 2, cy);
        }
    }
    return frame;
}

// Without lens correction and rotation, merging the two eyes must
// reconstruct the panorama exactly, wherever the seam is
static void test_identity_merge(void) {
    struct sc_quest_params params = {
        .angle = 0,
        .k1 = 0,
        .k2 = 0,
        .ovfrac = 0.844f,
        .preset = SC_QUEST_PRESET_D,
    };

    struct sc_quest q;
    bool ok = sc_quest_init(&q, &params);
    assert(ok);
    (void) ok;

    int ew = SRC_W / 2;
    int ov = (int) (ew * params.ovfrac);
    int off = ew - ov;
    int out_w = ew + off;

    struct sc_size src_size = {SRC_W, SRC_H};
    struct sc_size out_size = sc_quest_get_output_size(&q, src_size);
    assert(out_size.width == out_w);
    assert(out_size.height == SRC_H);

    AVFrame *in = create_pano_frame(off);

    const AVFrame *out = sc_quest_process(&q, in);
    assert(out && out != in);
    assert(out->width == out_w);
    assert(out->height == SRC_H);
    assert(out->format == AV_PIX_FMT_YUV420P);

    // The last row and column are interpolated with a clamped position
    for (int y = 0; y < SRC_H - 1; ++y) {
        const uint8_t *row = out->data[0] + (size_t) y * out->linesize[0];
        for (int x = 0; x < out_w - 1; ++x) {
            assert(row[x] == pano_y(x, y));
        }
    }
    for (int cy = 0; cy < SRC_H / 2 - 1; ++cy) {
        const uint8_t *row_u = out->data[1] + (size_t) cy * out->linesize[1];
        const uint8_t *row_v = out->data[2] + (size_t) cy * out->linesize[2];
        for (int cx = 0; cx < out_w / 2 - 1; ++cx) {
            assert(row_u[cx] == pano_u(cx, cy));
            assert(row_v[cx] == pano_v(cx, cy));
        }
    }

    // Disable the processing
    bool changed = sc_quest_handle_key(&q, SDL_SCANCODE_BACKSLASH, false);
    assert(changed);
    (void) changed;
    assert(sc_quest_process(&q, in) == in);
    out_size = sc_quest_get_output_size(&q, src_size);
    assert(out_size.width == SRC_W);
    assert(out_size.height == SRC_H);

    av_frame_free(&in);
    sc_quest_destroy(&q);
}

static void test_presets_and_keys(void) {
    struct sc_quest q;
    bool ok = sc_quest_init(&q, &default_params);
    assert(ok);
    (void) ok;

    AVFrame *in = create_pano_frame(322);

    // merged picture: 2386x2208
    static const struct {
        SDL_Scancode key;
        int w;
        int h;
    } expected[] = {
        {SDL_SCANCODE_1, 1760, 916},  // A: (0.869-0.131)*2386, (0.512-0.097)*2208
        {SDL_SCANCODE_2, 1664, 1294}, // B
        {SDL_SCANCODE_3, 1472, 1576}, // C
        {SDL_SCANCODE_4, 2386, 2208}, // D
    };

    for (size_t i = 0; i < 4; ++i) {
        assert(sc_quest_is_key(expected[i].key));
        sc_quest_handle_key(&q, expected[i].key, false);
        const AVFrame *out = sc_quest_process(&q, in);
        assert(out && out != in);
        printf("preset %c: %dx%d\n", (char) ('A' + i), out->width,
               out->height);
        assert(out->width == expected[i].w);
        assert(out->height == expected[i].h);
    }

    // Live adjustments
    sc_quest_handle_key(&q, SDL_SCANCODE_RIGHTBRACKET, false);
    assert(q.params.angle > 20.95f && q.params.angle < 21.05f);
    sc_quest_handle_key(&q, SDL_SCANCODE_LEFTBRACKET, true);
    assert(q.params.angle > 19.95f && q.params.angle < 20.05f);
    sc_quest_handle_key(&q, SDL_SCANCODE_MINUS, false);
    assert(q.params.k1 < -0.265f && q.params.k1 > -0.275f);
    sc_quest_handle_key(&q, SDL_SCANCODE_APOSTROPHE, false);
    assert(q.params.k2 < -0.035f && q.params.k2 > -0.045f);
    sc_quest_handle_key(&q, SDL_SCANCODE_COMMA, false);
    assert(q.params.ovfrac > 0.841f && q.params.ovfrac < 0.843f);
    const AVFrame *out = sc_quest_process(&q, in);
    assert(out && out != in);
    // the merged picture is wider if the overlap is smaller
    assert(out->width > 2386);

    // Reset
    sc_quest_handle_key(&q, SDL_SCANCODE_0, false);
    assert(q.params.angle == default_params.angle);
    assert(q.params.preset == SC_QUEST_PRESET_B);

    assert(!sc_quest_is_key(SDL_SCANCODE_A));
    assert(!sc_quest_handle_key(&q, SDL_SCANCODE_A, false));

    int w;
    int h;
    assert(sc_quest_consume_overlay_dirty(&q));
    assert(!sc_quest_consume_overlay_dirty(&q));
    uint32_t *pixels = sc_quest_render_overlay(&q, &w, &h);
    assert(pixels);
    assert(w > 0 && h > 0);
    free(pixels);

    av_frame_free(&in);
    sc_quest_destroy(&q);
}

static void
write_ppm(const char *path, const AVFrame *f) {
    FILE *file = fopen(path, "wb");
    if (!file) {
        return;
    }
    fprintf(file, "P6\n%d %d\n255\n", f->width, f->height);
    for (int y = 0; y < f->height; ++y) {
        for (int x = 0; x < f->width; ++x) {
            int Y = f->data[0][(size_t) y * f->linesize[0] + x];
            int U = f->data[1][(size_t) (y / 2) * f->linesize[1] + x / 2] - 128;
            int V = f->data[2][(size_t) (y / 2) * f->linesize[2] + x / 2] - 128;
            int c = (Y - 16) * 298;
            int r = (c + 409 * V + 128) >> 8;
            int g = (c - 100 * U - 208 * V + 128) >> 8;
            int b = (c + 516 * U + 128) >> 8;
            uint8_t rgb[3] = {
                r < 0 ? 0 : r > 255 ? 255 : r,
                g < 0 ? 0 : g > 255 ? 255 : g,
                b < 0 ? 0 : b > 255 ? 255 : b,
            };
            fwrite(rgb, 1, 3, file);
        }
    }
    fclose(file);
}

// Not a test: measure the processing duration (QUEST_BENCH=1), and optionally
// dump the result of each preset as PPM (QUEST_DUMP=<prefix>)
static void bench(void) {
    const char *dump = getenv("QUEST_DUMP");

    struct sc_quest q;
    bool ok = sc_quest_init(&q, &default_params);
    assert(ok);
    (void) ok;

    AVFrame *in = create_pano_frame(322);

    static const SDL_Scancode keys[] = {
        SDL_SCANCODE_1, SDL_SCANCODE_2, SDL_SCANCODE_3, SDL_SCANCODE_4,
    };
    for (int i = 0; i < 4; ++i) {
        sc_quest_handle_key(&q, keys[i], false);

        sc_tick t0 = sc_tick_now();
        const AVFrame *out = sc_quest_process(&q, in);
        sc_tick t1 = sc_tick_now();

        const int count = 100;
        for (int j = 0; j < count; ++j) {
            // make the seam move
            in->data[0][(size_t) (j * 20) * in->linesize[0] + 1500 + j] ^= 0xff;
            out = sc_quest_process(&q, in);
        }
        sc_tick t2 = sc_tick_now();

        printf("bench %dx%d -> preset %c %dx%d: first frame (with maps) "
               "%.1f ms, then %.2f ms/frame (%u threads)\n",
               SRC_W, SRC_H, 'A' + i, out->width, out->height,
               (t1 - t0) / 1000.f, (t2 - t1) / 1000.f / count,
               q.pool.count + 1);

        if (dump) {
            char path[512];
            snprintf(path, sizeof(path), "%s_%c.ppm", dump, 'A' + i);
            write_ppm(path, out);
        }
    }

    // A parameter change rebuilds the maps
    sc_quest_handle_key(&q, SDL_SCANCODE_RIGHTBRACKET, false);
    sc_tick t0 = sc_tick_now();
    sc_quest_process(&q, in);
    sc_tick t1 = sc_tick_now();
    printf("bench: frame after angle change (maps rebuilt): %.1f ms\n",
           (t1 - t0) / 1000.f);

    if (dump) {
        int w;
        int h;
        uint32_t *pixels = sc_quest_render_overlay(&q, &w, &h);
        assert(pixels);
        char path[512];
        snprintf(path, sizeof(path), "%s_overlay.ppm", dump);
        FILE *file = fopen(path, "wb");
        if (file) {
            fprintf(file, "P6\n%d %d\n255\n", w, h);
            for (int i = 0; i < w * h; ++i) {
                uint8_t v = pixels[i] & 0xff;
                uint8_t rgb[3] = {v, v, v};
                fwrite(rgb, 1, 3, file);
            }
            fclose(file);
        }
        free(pixels);
    }

    av_frame_free(&in);
    sc_quest_destroy(&q);
}

int main(int argc, char *argv[]) {
    (void) argc;
    (void) argv;

    test_identity_merge();
    test_presets_and_keys();

    if (getenv("QUEST_BENCH")) {
        bench();
    }

    return 0;
}
