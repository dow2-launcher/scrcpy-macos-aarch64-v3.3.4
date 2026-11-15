#ifndef SC_QUEST_H
#define SC_QUEST_H

#include "common.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
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
//     computed by dynamic programming (hard cut, no feathering). By default,
//     the seam is kept as long as possible, because the picture "jumps"
//     whenever it moves (the two eyes do not see close objects at the same
//     position). Alternatively, a single eye may be displayed, without merge;
//  4. crop to a view preset (A/B/C/D).
//
// Everything is done directly on the YUV420P planes (bilinear sampling with
// precomputed fixed-point maps), so no colorspace conversion is necessary:
// the result is a YUV420P frame which can be uploaded to the texture like any
// decoded frame.

#define SC_QUEST_MAX_WORKERS 7

// Delay between the last quality key press and the restart
#define SC_QUEST_RESTART_DELAY_MS 1200

// Number of bytes of video packets received (to report the actual bit rate)
extern atomic_uint_least64_t sc_quest_video_bytes;

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
    char *config_path; // may be NULL
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
    int *seam_candidate; // candidate seam at low resolution
    bool seam_valid; // the current seam matches the current geometry
    bool seam_relock; // recompute the seam on the next frame
    unsigned seam_counter; // frames since the last seam evaluation
    unsigned seam_bad; // consecutive evaluations where the seam was bad
    sc_tick seam_date; // date of the last seam change

    AVFrame *out;

    struct sc_quest_pool pool;

    // Video quality to apply by restarting (if restart_pending)
    bool restart_pending;
    uint32_t restart_bit_rate;
    enum sc_codec restart_codec;
    sc_tick restart_date; // date of the last quality key press
    bool restarting;

    bool first_frame_done;

    // actual video bit rate
    uint64_t rate_bytes;
    sc_tick rate_date;
    float rate_mbps;

    // stats
    float last_ms; // duration of the processing of the last frame
    float avg_ms;
    sc_tick overlay_date;
};

// The parameters not set explicitly (cli_set is a OR of SC_QUEST_SET_*) are
// loaded from the config file (if use_config is set)
bool
sc_quest_init(struct sc_quest *q, const struct sc_quest_params *params,
              uint8_t cli_set, bool use_config);

// In Meta Quest viewing mode, set the video bit rate and codec from the config
// file or from the Quest defaults if they are not set on the command line
void
sc_quest_resolve_options(struct scrcpy_options *opts);

static inline bool
sc_quest_is_restart_pending(const struct sc_quest *q) {
    return q->restart_pending;
}

// To be called SC_QUEST_RESTART_DELAY_MS after a quality key press. If a
// restart is necessary to apply a new video quality, store the command line
// arguments to pass to the new process (including the current window
// geometry) and return true: scrcpy must exit and call
// sc_quest_restart_exec().
bool
sc_quest_request_restart(struct sc_quest *q, bool fullscreen, int x, int y,
                         int w, int h);

// Replace the current process by a new scrcpy instance (if a restart has been
// requested). Return only if no restart is requested or on error.
void
sc_quest_restart_exec(int argc, char *argv[]);

// Save the current parameters to the config file (if enabled)
void
sc_quest_save_config(struct sc_quest *q);

// Read the parameters present in the file (the others are left unchanged)
bool
sc_quest_config_load(const char *path, struct sc_quest_params *params);

bool
sc_quest_config_save(const char *path, const struct sc_quest_params *params);

// Write the command line equivalent to the parameters
void
sc_quest_format_cli(const struct sc_quest_params *params, char *buf,
                    size_t size);

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
