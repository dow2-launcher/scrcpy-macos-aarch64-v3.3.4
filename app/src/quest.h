#ifndef SC_QUEST_H
#define SC_QUEST_H

#include "common.h"

#include <stdbool.h>
#include <stdint.h>
#include <libavutil/frame.h>
#include <SDL2/SDL_scancode.h>

#include "coords.h"
#include "options.h"
#include "util/thread.h"
#include "util/tick.h"

// Meta Quest 3 viewing mode (--quest)
//
// The Quest display buffer contains both eyes side by side (left eye in the
// left half, right eye in the right half). Each eye is pre-distorted for the
// lens (fisheye) and rotated (canted panels, mirrored angles).
//
// This component turns a decoded frame into a single flat picture:
//  1. per-eye lens correction (same model as the ffmpeg lenscorrection
//     filter, parameters k1 and k2);
//  2. mirrored rotation (+angle for the left eye, -angle for the right eye);
//  3. merge of the two eyes with an overlap (ovfrac), along an optimal seam
//     computed by dynamic programming (hard cut, no feathering);
//  4. crop to a view preset (A/B/C/D).
//
// Everything is done directly on the YUV420P planes (bilinear sampling with
// precomputed fixed-point maps), so no colorspace conversion is necessary:
// the result is a YUV420P frame which can be uploaded to the texture like any
// decoded frame.

#define SC_QUEST_MAX_WORKERS 7

struct sc_quest_pool {
    sc_mutex mutex;
    sc_cond cond_work;
    sc_cond cond_done;
    sc_thread threads[SC_QUEST_MAX_WORKERS];
    unsigned count; // number of worker threads
    unsigned generation[SC_QUEST_MAX_WORKERS];
    unsigned pending;
    bool stopped;
    void (*fn)(void *arg, unsigned band, unsigned nbands);
    void *arg;
};

struct sc_quest {
    struct sc_quest_params params;
    struct sc_quest_params initial_params;
    bool processing; // if false, frames are returned unchanged (raw buffer)
    bool overlay; // show the text overlay
    bool overlay_dirty;

    // source geometry
    int src_w;
    int src_h;
    int ew; // eye width
    int eh; // eye height
    int ov; // overlap width
    int off; // ew - ov
    int out_w; // merged width (ew + off)

    // crop rectangle in the merged picture (the output frame)
    struct {
        int x;
        int y;
        int w;
        int h;
    } crop;

    // For each pixel of a corrected eye, the source position in the raw eye,
    // as two uint16_t (x, y) in 1/16 pixel units
    uint16_t *map[2];
    int map_ew;
    int map_eh;
    float map_angle;
    float map_k1;
    float map_k2;

    // seam
    int *seam; // seam x position (in the overlap) for each row
    int *seam_low; // seam at low resolution
    int32_t *dp_prev;
    int32_t *dp_cur;
    int8_t *dp_back;
    uint8_t *seam_diff; // low resolution |left - right|
    int low_w;
    int low_h;
    bool seam_low_valid;

    AVFrame *out;

    struct sc_quest_pool pool;

    // stats
    float last_ms; // duration of the processing of the last frame
    float avg_ms;
    sc_tick overlay_date;
};

bool
sc_quest_init(struct sc_quest *q, const struct sc_quest_params *params);

void
sc_quest_destroy(struct sc_quest *q);

// Compute the size of the processed frame for a given source frame size
struct sc_size
sc_quest_get_output_size(struct sc_quest *q, struct sc_size src_size);

// Process a decoded frame.
//
// The returned frame is owned by sc_quest and is valid until the next call. If
// the processing is disabled (or not applicable to this frame), the input
// frame is returned. Return NULL on error.
const AVFrame *
sc_quest_process(struct sc_quest *q, const AVFrame *frame);

// Return true if the key is used by the quest mode (the event must not be
// forwarded to the device)
bool
sc_quest_is_key(SDL_Scancode scancode);

// Handle a key press. Return true if a parameter changed (a refresh of the
// current frame is necessary).
bool
sc_quest_handle_key(struct sc_quest *q, SDL_Scancode scancode, bool shift);

// Return true (once) if the overlay content must be updated
bool
sc_quest_consume_overlay_dirty(struct sc_quest *q);

// Render the text overlay to a new ARGB8888 buffer (to be freed by the
// caller). Return NULL if the overlay is disabled or on error.
uint32_t *
sc_quest_render_overlay(struct sc_quest *q, int *w, int *h);

#endif
