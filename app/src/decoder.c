#include "decoder.h"

#include <errno.h>
#include <libavcodec/packet.h>
#include <libavutil/avutil.h>
#include <libavutil/pixfmt.h>

#include "quest.h"
#include "util/log.h"
#include "util/tick.h"

/** Downcast packet_sink to decoder */
#define DOWNCAST(SINK) container_of(SINK, struct sc_decoder, packet_sink)

static bool
sc_decoder_open(struct sc_decoder *decoder, AVCodecContext *ctx) {
    decoder->frame = av_frame_alloc();
    if (!decoder->frame) {
        LOG_OOM();
        return false;
    }

    decoder->sw_frame = NULL;
    decoder->hw_tmp_frame = NULL;

    if (!sc_frame_source_sinks_open(&decoder->frame_source, ctx)) {
        av_frame_free(&decoder->frame);
        return false;
    }

    decoder->ctx = ctx;

    return true;
}

static void
sc_decoder_close(struct sc_decoder *decoder) {
    sc_frame_source_sinks_close(&decoder->frame_source);
    av_frame_free(&decoder->frame);
    av_frame_free(&decoder->sw_frame);
    av_frame_free(&decoder->hw_tmp_frame);
}

static bool
sc_decoder_push(struct sc_decoder *decoder, const AVPacket *packet) {
    bool is_config = packet->pts == AV_NOPTS_VALUE;
    if (is_config) {
        // nothing to do
        return true;
    }

    bool is_video = decoder->ctx->codec_type == AVMEDIA_TYPE_VIDEO;
    sc_tick start = is_video ? sc_tick_now() : 0;

    int ret = avcodec_send_packet(decoder->ctx, packet);
    if (ret < 0 && ret != AVERROR(EAGAIN)) {
        LOGE("Decoder '%s': could not send video packet: %d",
             decoder->name, ret);
        return false;
    }

    for (;;) {
        ret = avcodec_receive_frame(decoder->ctx, decoder->frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
        }

        if (ret) {
            LOGE("Decoder '%s', could not receive video frame: %d",
                 decoder->name, ret);
            return false;
        }

        // a frame was received
        AVFrame *frame = decoder->frame;
        if (frame->format == AV_PIX_FMT_VIDEOTOOLBOX) {
            // Hardware frame (Meta Quest viewing mode with hardware decoding)
            if (!decoder->sw_frame) {
                decoder->sw_frame = av_frame_alloc();
                decoder->hw_tmp_frame = av_frame_alloc();
                if (!decoder->sw_frame || !decoder->hw_tmp_frame) {
                    LOG_OOM();
                    return false;
                }
            }
            if (!sc_quest_download_frame(frame, decoder->hw_tmp_frame,
                                         decoder->sw_frame)) {
                LOGE("Decoder '%s': could not download the hardware frame",
                     decoder->name);
                return false;
            }
            frame = decoder->sw_frame;
        }

        if (is_video) {
            // For the performance line of the Meta Quest viewing mode
            sc_tick now = sc_tick_now();
            sc_quest_stats_on_frame(frame->pts, now - start, now);
            start = now;
        }

        bool ok = sc_frame_source_sinks_push(&decoder->frame_source, frame);
        if (frame != decoder->frame) {
            av_frame_unref(frame);
        }
        av_frame_unref(decoder->frame);
        if (!ok) {
            // Error already logged
            return false;
        }
    }

    return true;
}

static bool
sc_decoder_packet_sink_open(struct sc_packet_sink *sink, AVCodecContext *ctx) {
    struct sc_decoder *decoder = DOWNCAST(sink);
    return sc_decoder_open(decoder, ctx);
}

static void
sc_decoder_packet_sink_close(struct sc_packet_sink *sink) {
    struct sc_decoder *decoder = DOWNCAST(sink);
    sc_decoder_close(decoder);
}

static bool
sc_decoder_packet_sink_push(struct sc_packet_sink *sink,
                            const AVPacket *packet) {
    struct sc_decoder *decoder = DOWNCAST(sink);
    return sc_decoder_push(decoder, packet);
}

void
sc_decoder_init(struct sc_decoder *decoder, const char *name) {
    decoder->name = name; // statically allocated
    sc_frame_source_init(&decoder->frame_source);

    static const struct sc_packet_sink_ops ops = {
        .open = sc_decoder_packet_sink_open,
        .close = sc_decoder_packet_sink_close,
        .push = sc_decoder_packet_sink_push,
    };

    decoder->packet_sink.ops = &ops;
}
