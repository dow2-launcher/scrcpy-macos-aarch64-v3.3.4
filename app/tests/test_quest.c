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
    .bit_rate = 50000000,
    .codec = SC_CODEC_H264,
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
    bool ok = sc_quest_init(&q, &params, 0, false);
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

    // A single eye, without merge
    sc_quest_handle_key(&q, SDL_SCANCODE_5, false);
    assert(q.params.eye == SC_QUEST_EYE_LEFT);
    out = sc_quest_process(&q, in);
    assert(out && out->width == ew && out->height == SRC_H);
    for (int y = 0; y < SRC_H - 1; ++y) {
        const uint8_t *row = out->data[0] + (size_t) y * out->linesize[0];
        for (int x = 0; x < ew - 1; ++x) {
            assert(row[x] == pano_y(x, y));
        }
    }
    sc_quest_handle_key(&q, SDL_SCANCODE_5, false);
    assert(q.params.eye == SC_QUEST_EYE_RIGHT);
    out = sc_quest_process(&q, in);
    assert(out && out->width == ew && out->height == SRC_H);
    for (int y = 0; y < SRC_H - 1; ++y) {
        const uint8_t *row = out->data[0] + (size_t) y * out->linesize[0];
        for (int x = 0; x < ew - 1; ++x) {
            assert(row[x] == pano_y(x + off, y));
        }
    }
    for (int cy = 0; cy < SRC_H / 2 - 1; ++cy) {
        const uint8_t *row_u = out->data[1] + (size_t) cy * out->linesize[1];
        for (int cx = 0; cx < ew / 2 - 1; ++cx) {
            assert(row_u[cx] == pano_u(cx + off / 2, cy));
        }
    }
    sc_quest_handle_key(&q, SDL_SCANCODE_5, false);
    assert(q.params.eye == SC_QUEST_EYE_BOTH);

    // Fixed vertical seam, moved to the left then far to the right
    sc_quest_handle_key(&q, SDL_SCANCODE_8, false);
    assert(q.params.seam == SC_QUEST_SEAM_FIXED);
    out = sc_quest_process(&q, in);
    assert(out && out->width == out_w);
    int seam_x = q.seam[0];
    assert(seam_x == ov / 2 - 2); // -0.001 * 2064
    for (int y = 0; y < SRC_H; ++y) {
        assert(q.seam[y] == seam_x);
    }
    for (int i = 0; i < 60; ++i) {
        sc_quest_handle_key(&q, SDL_SCANCODE_9, true);
    }
    out = sc_quest_process(&q, in);
    assert(q.seam[SRC_H / 2] == ov - 1); // clamped to the overlap
    for (int y = 0; y < SRC_H - 1; y += 7) {
        const uint8_t *row = out->data[0] + (size_t) y * out->linesize[0];
        for (int x = 0; x < out_w - 1; ++x) {
            assert(row[x] == pano_y(x, y));
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
    bool ok = sc_quest_init(&q, &default_params, 0, false);
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

    // Single eye presets (eye: 2064x2208)
    static const struct {
        SDL_Scancode key;
        int w;
        int h;
    } eye_expected[] = {
        {SDL_SCANCODE_1, 1568, 882},
        {SDL_SCANCODE_2, 1568, 1412},
        {SDL_SCANCODE_3, 1238, 1854},
        {SDL_SCANCODE_4, 2064, 2208},
    };
    sc_quest_handle_key(&q, SDL_SCANCODE_0, false);
    sc_quest_handle_key(&q, SDL_SCANCODE_5, false);
    for (size_t i = 0; i < 4; ++i) {
        sc_quest_handle_key(&q, eye_expected[i].key, false);
        out = sc_quest_process(&q, in);
        assert(out && out != in);
        printf("left eye preset %c: %dx%d\n", (char) ('A' + i), out->width,
               out->height);
        assert(out->width == eye_expected[i].w);
        assert(out->height == eye_expected[i].h);
    }

    // Reset (the default is the left eye only since 15.11)
    sc_quest_handle_key(&q, SDL_SCANCODE_0, false);
    assert(q.params.eye == SC_QUEST_EYE_LEFT);
    assert(q.params.seam == SC_QUEST_SEAM_AUTO);
    assert(q.params.angle == default_params.angle);
    assert(q.params.preset == SC_QUEST_PRESET_B);

    // Quality keys request a restart, they do not change the actual values
    assert(q.params.bit_rate == 50000000);
    assert(sc_quest_handle_key(&q, SDL_SCANCODE_Q, false));
    assert(sc_quest_is_restart_pending(&q));
    assert(q.restart.bit_rate == 80000000);
    assert(sc_quest_handle_key(&q, SDL_SCANCODE_Q, true));
    assert(sc_quest_handle_key(&q, SDL_SCANCODE_Q, true));
    assert(q.restart.bit_rate == 30000000);
    assert(sc_quest_handle_key(&q, SDL_SCANCODE_T, false));
    assert(q.restart.decode_threads == 1);
    // Automatic: all the CPU cores (1 to 16), whatever the bit rate
    assert(sc_quest_get_auto_decode_threads(30000000) >= 1);
    assert(sc_quest_get_auto_decode_threads(30000000) <= 16);
    assert(sc_quest_get_auto_decode_threads(30000000)
            == sc_quest_get_auto_decode_threads(80000000));
    assert(sc_quest_handle_key(&q, SDL_SCANCODE_H, false));
    assert(q.restart.codec == SC_CODEC_H265);
    assert(q.params.bit_rate == 50000000);
    assert(q.params.codec == SC_CODEC_H264);
    // too early (the user may still be pressing keys)
    assert(!sc_quest_request_restart(&q, false, 0, 0, 100, 100));
    assert(sc_quest_is_restart_pending(&q));

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

static uint32_t
next_random(uint32_t *state) {
    *state = *state * 1664525 + 1013904223;
    return *state >> 16;
}

// In auto and lock modes, the seam must not move from frame to frame (the
// picture "jumps" when it moves), even if the pictures are noisy
static void test_seam_stability(void) {
    struct sc_quest q;
    bool ok = sc_quest_init(&q, &default_params, 0, false);
    assert(ok);
    (void) ok;
    assert(q.params.seam == SC_QUEST_SEAM_AUTO);

    AVFrame *in = create_pano_frame(322);
    const AVFrame *out = sc_quest_process(&q, in);
    assert(out && out != in);

    int *reference = malloc(sizeof(int) * SRC_H);
    assert(reference);
    memcpy(reference, q.seam, sizeof(int) * SRC_H);

    uint32_t state = 1;
    for (int frame = 0; frame < 40; ++frame) {
        // add some noise everywhere
        for (int i = 0; i < 200000; ++i) {
            int x = next_random(&state) % SRC_W;
            int y = next_random(&state) % SRC_H;
            in->data[0][(size_t) y * in->linesize[0] + x] ^=
                next_random(&state) & 0x1f;
        }
        sc_quest_process(&q, in);
        assert(!memcmp(reference, q.seam, sizeof(int) * SRC_H));
    }

    sc_quest_handle_key(&q, SDL_SCANCODE_6, false);
    assert(q.params.seam == SC_QUEST_SEAM_LOCK);
    sc_quest_process(&q, in);
    // auto -> lock freezes the current seam
    assert(!memcmp(reference, q.seam, sizeof(int) * SRC_H));
    // recompute explicitly
    sc_quest_handle_key(&q, SDL_SCANCODE_7, false);
    sc_quest_process(&q, in);
    assert(q.seam_valid && !q.seam_relock);

    free(reference);
    av_frame_free(&in);
    sc_quest_destroy(&q);
}

static void test_config(void) {
    const char *path = "test_quest.conf";
    struct sc_quest_params params = {
        .angle = 21.3f,
        .k1 = -0.31f,
        .k2 = 0.02f,
        .ovfrac = 0.852f,
        .preset = SC_QUEST_PRESET_C,
        .eye = SC_QUEST_EYE_RIGHT,
        .seam = SC_QUEST_SEAM_FIXED,
        .seam_x = -0.042f,
        .bit_rate = 120000000,
        .codec = SC_CODEC_H265,
        .decode_threads = 4,
        .hwdec = true,
    };
    bool ok = sc_quest_config_save(path, &params);
    assert(ok);

    struct sc_quest_params loaded = default_params;
    ok = sc_quest_config_load(path, &loaded);
    assert(ok);
    (void) ok;
    assert(loaded.codec == SC_CODEC_H264);
    loaded.codec = SC_CODEC_H265;
    assert(loaded.angle > 21.29f && loaded.angle < 21.31f);
    assert(loaded.k1 > -0.311f && loaded.k1 < -0.309f);
    assert(loaded.k2 > 0.019f && loaded.k2 < 0.021f);
    assert(loaded.ovfrac > 0.8519f && loaded.ovfrac < 0.8521f);
    assert(loaded.preset == SC_QUEST_PRESET_C);
    assert(loaded.eye == SC_QUEST_EYE_RIGHT);
    assert(loaded.seam == SC_QUEST_SEAM_FIXED);
    assert(loaded.seam_x > -0.0421f && loaded.seam_x < -0.0419f);

    char cli[256];
    sc_quest_format_cli(&loaded, cli, sizeof(cli));
    printf("%s\n", cli);
    assert(!strcmp(cli, "--quest --quest-angle=21.3 --quest-k1=-0.31 "
                        "--quest-k2=0.02 --quest-ovfrac=0.852 "
                        "--quest-preset=C --quest-eye=right "
                        "--quest-seam=fixed --quest-seam-x=-0.042 "
                        "--video-bit-rate=120000000 --video-codec=h265 "
                        "--quest-decode-threads=4 --quest-hwdec=on"));
    assert(loaded.decode_threads == 4);
    assert(loaded.hwdec);
    assert(loaded.bit_rate == 120000000);
    // the codec is not remembered

    remove(path);
    assert(!sc_quest_config_load(path, &loaded));
}

// Not a test: measure the processing duration (QUEST_BENCH=1), and optionally
// dump the result of each preset as PPM (QUEST_DUMP=<prefix>)
static void bench(void) {
    const char *dump = getenv("QUEST_DUMP");

    struct sc_quest q;
    bool ok = sc_quest_init(&q, &default_params, 0, false);
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
    test_seam_stability();
    test_config();

    if (getenv("QUEST_BENCH")) {
        bench();
    }

    return 0;
}
