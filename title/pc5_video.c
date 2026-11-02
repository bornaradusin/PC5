/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include "pc5_video.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#ifdef PC5_VIDEO_FFMPEG
#include <errno.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/error.h>
#include <libswscale/swscale.h>
#ifndef PC5_FFMPEG_EAGAIN
#define PC5_FFMPEG_EAGAIN EAGAIN
#endif

#ifdef __PROSPERO__
#include <stdlib.h>
/* A title has no terminal. FFmpeg probes this only to choose its log style;
 * the SDK's system import is not available in the application process. */
int isatty(int descriptor)
{
    (void)descriptor;
    errno = ENOTTY;
    return 0;
}
/* FFmpeg's SDK-built archives reference __assert, which title libc omits.
 * An assembler alias avoids assuming the SDK's diagnostic argument layout:
 * arguments are intentionally unused, but a failed assertion remains fatal. */
void pc5_video_assert_failed(void) __asm__("__assert") __attribute__((noreturn));
void pc5_video_assert_failed(void)
{
    fputs("PC5 FFmpeg: assertion failed\n", stderr);
    abort();
}
#endif

enum { VW = 1920, VH = 1080, VIDEO_LIMIT = 1024 * 1024 * 1024 };
typedef struct VideoDecoder {
    FILE *file;
    AVFormatContext *format;
    AVIOContext *io;
    AVCodecContext *codec;
    AVFrame *frame;
    AVPacket *packet;
    struct SwsContext *scale;
    atomic_int *stop;
    int stream, draining;
    double fps;
    char error[PC5_ERROR];
} VideoDecoder;

static int video_interrupt(void *context) { return atomic_load((atomic_int *)context); }
static int video_read(void *context, uint8_t *out, int count)
{
    FILE *file = context;
    size_t n = fread(out, 1, (size_t)count, file);
    if (n) return (int)n;
    return ferror(file) ? AVERROR(EIO) : AVERROR_EOF;
}
static int64_t video_seek(void *context, int64_t offset, int whence)
{
    FILE *file = context;
    if (whence == AVSEEK_SIZE) {
        struct stat st;
        return fstat(fileno(file), &st) ? AVERROR(EIO) : st.st_size;
    }
    whence &= ~AVSEEK_FORCE;
    if ((whence != SEEK_SET && whence != SEEK_CUR && whence != SEEK_END) ||
        offset > VIDEO_LIMIT || offset < -VIDEO_LIMIT || fseek(file, (long)offset, whence)) return AVERROR(EIO);
    long at = ftell(file);
    return at < 0 ? AVERROR(EIO) : at;
}

static void decoder_close(VideoDecoder *d)
{
    sws_freeContext(d->scale);
    av_packet_free(&d->packet);
    av_frame_free(&d->frame);
    avcodec_free_context(&d->codec);
    if (d->format) avformat_close_input(&d->format);
    if (d->io) {
        av_freep(&d->io->buffer);
        avio_context_free(&d->io);
    }
    if (d->file) fclose(d->file);
    pc5_free(d);
}

static VideoDecoder *decoder_open(const char *path, atomic_int *stop, char *error)
{
    struct stat st;
    const char *stage = "Video file access";
    int rc = 0;
    VideoDecoder *d = pc5_alloc(sizeof(*d));
    if (!d) { snprintf(error, PC5_ERROR, "Not enough memory for video reader"); return NULL; }
    d->stop = stop;
    if (stat(path, &st) || !S_ISREG(st.st_mode) || st.st_size <= 0 || st.st_size > VIDEO_LIMIT) goto bad;
    d->file = fopen(path, "rb");
    stage = "Video reader allocation";
    d->format = avformat_alloc_context();
    if (!d->file || !d->format) goto bad;
    uint8_t *buffer = av_malloc(32768);
    if (!buffer) goto bad;
    d->io = avio_alloc_context(buffer, 32768, 0, d->file, video_read, NULL, video_seek);
    if (!d->io) { av_free(buffer); goto bad; }
    d->format->pb = d->io;
    d->format->flags |= AVFMT_FLAG_CUSTOM_IO;
    d->format->interrupt_callback = (AVIOInterruptCB){ video_interrupt, stop };
    d->format->probesize = 2 << 20;
    d->format->max_analyze_duration = 2 * AV_TIME_BASE;
    stage = "Opening video container";
    rc = avformat_open_input(&d->format, NULL, NULL, NULL);
    if (rc < 0) goto bad;
    stage = "Reading video stream information";
    rc = avformat_find_stream_info(d->format, NULL);
    if (rc < 0) goto bad;
    stage = "Finding video stream";
    d->stream = av_find_best_stream(d->format, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (d->stream < 0) goto bad;
    AVStream *stream = d->format->streams[d->stream];
    AVCodecParameters *p = stream->codecpar;
    stage = "Video must be H.264 and at most 1920x1080";
    if (p->codec_id != AV_CODEC_ID_H264 || p->width <= 0 || p->height <= 0 || p->width > VW || p->height > VH) goto bad;
    AVRational rate = av_guess_frame_rate(d->format, stream, NULL);
    d->fps = rate.num > 0 && rate.den > 0 ? av_q2d(rate) : 30;
    stage = "Video must be at most 60 fps";
    if (d->fps <= 0 || d->fps > 60.01) goto bad;
    const AVCodec *codec = avcodec_find_decoder(p->codec_id);
    stage = "Allocating H.264 decoder";
    if (!codec || !(d->codec = avcodec_alloc_context3(codec)) || avcodec_parameters_to_context(d->codec, p) < 0) goto bad;
    d->codec->thread_count = 4;
    d->codec->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
    /* H.264 may code 1080 visible rows in 1088 padded rows. Visible output
     * dimensions are still checked separately before a frame is accepted. */
    d->codec->max_pixels = (int64_t)((VW + 15) & ~15) * ((VH + 15) & ~15);
    stage = "Opening H.264 decoder";
    rc = avcodec_open2(d->codec, codec, NULL);
    if (rc < 0) goto bad;
    stage = "Allocating video frames";
    d->frame = av_frame_alloc();
    d->packet = av_packet_alloc();
    if (!d->frame || !d->packet) goto bad;
    return d;
bad:
    if (rc < 0) {
        char why[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(rc, why, sizeof(why));
        snprintf(error, PC5_ERROR, "%s: %s (%d)", stage, why, rc);
    } else snprintf(error, PC5_ERROR, "%s (system error %d)", stage, errno);
    decoder_close(d);
    return NULL;
}

static int decoder_error(VideoDecoder *d, const char *stage, int code)
{
    char why[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(code, why, sizeof(why));
    snprintf(d->error, sizeof(d->error), "%s: %s (%d)", stage, why, code);
    return -1;
}

static int decoder_next(VideoDecoder *d)
{
    while (!atomic_load(d->stop)) {
        int rc = avcodec_receive_frame(d->codec, d->frame);
        if (rc == 0) {
            if (d->frame->width <= 0 || d->frame->width > VW || d->frame->height <= 0 || d->frame->height > VH)
                return decoder_error(d, "Decoded dimensions exceed 1080p", AVERROR_INVALIDDATA);
            return 1;
        }
        if (rc == AVERROR_EOF) return 0;
        /* The SDK-built FFmpeg and the title runtime can have different errno
         * numbers. Use the decoder's EAGAIN ABI, determined by the build. */
        if (rc != AVERROR(PC5_FFMPEG_EAGAIN) || d->draining) return decoder_error(d, "Receiving H.264 frame", rc);
        for (;;) {
            rc = av_read_frame(d->format, d->packet);
            if (rc < 0) {
                if (rc != AVERROR_EOF) return decoder_error(d, "Reading video packet", rc);
                d->draining = 1;
                rc = avcodec_send_packet(d->codec, NULL);
                if (rc < 0) return decoder_error(d, "Finishing H.264 stream", rc);
                break;
            }
            if (d->packet->size > (64 << 20)) { av_packet_unref(d->packet); return decoder_error(d, "Video packet too large", AVERROR_INVALIDDATA); }
            if (d->packet->stream_index == d->stream) {
                rc = avcodec_send_packet(d->codec, d->packet);
                av_packet_unref(d->packet);
                if (rc < 0) return decoder_error(d, "Sending H.264 packet", rc);
                break;
            }
            av_packet_unref(d->packet);  /* Audio is deliberately ignored. */
            if (atomic_load(d->stop)) return -1;
        }
    }
    return -1;
}

static uint64_t video_clock(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000 + (uint64_t)now.tv_nsec / 1000000;
}

static void video_shade(uint32_t *pixels, int darkness)
{
    int horizontal[VW];
    for (int x = 0; x < VW; x++) {
        float t = x < 1250 ? (float)x / 1250 : 1;
        horizontal[x] = (int)((0.22f + 0.78f * t * t * (3 - 2 * t)) * 256);
    }
    for (int y = 0; y < VH; y++) {
        float v = y < 170 ? 0.62f + 0.38f * (float)y / 170 : 1;
        if (y > 470) { float t = (float)(y - 470) / (VH - 470); v *= 1 - 0.86f * t * (2 - t); }
        int vertical = (int)(v * 256) * (100 - darkness) / 100;
        for (int x = 0; x < VW; x++) {
            uint32_t p = pixels[(size_t)y * VW + x], m = (uint32_t)(vertical * horizontal[x] >> 8);
            pixels[(size_t)y * VW + x] = 0xff000000u | (((p & 0xff00ffu) * m >> 8) & 0xff00ffu) |
                                        (((p & 0x00ff00u) * m >> 8) & 0x00ff00u);
        }
    }
}

static void *video_worker(void *context)
{
    Pc5Video *video = context;
    VideoDecoder *d = decoder_open(video->path, &video->stop, video->error);
    video->front = pc5_alloc((size_t)VW * VH * 4);
    video->back = pc5_alloc((size_t)VW * VH * 4);
    uint64_t began = video_clock(), number = 0;
    int64_t first = AV_NOPTS_VALUE;
    if (!d || !video->front || !video->back) goto done;
    AVStream *stream = d->format->streams[d->stream];
    int64_t origin = stream->start_time == AV_NOPTS_VALUE ? 0 : stream->start_time;
    double timebase = av_q2d(stream->time_base);
    if (stream->time_base.num <= 0 || stream->time_base.den <= 0 || !(timebase > 0)) {
        pc5_copy(video->error, sizeof(video->error), "Video has an invalid time base"); goto done;
    }
    int start_seconds = video->options.video_start;
    double duration = stream->duration != AV_NOPTS_VALUE ? stream->duration * av_q2d(stream->time_base) :
        d->format->duration != AV_NOPTS_VALUE ? (double)d->format->duration / AV_TIME_BASE : 0;
    if (duration > 0 && start_seconds >= duration) start_seconds = 0;
    int64_t offset = (int64_t)(start_seconds / timebase);
    if (offset > 0 && origin > INT64_MAX - offset) {
        pc5_copy(video->error, sizeof(video->error), "Video seek timestamp is out of range"); goto done;
    }
    int64_t start = origin + offset;
    if (start_seconds && av_seek_frame(d->format, d->stream, start, AVSEEK_FLAG_BACKWARD) >= 0)
        avcodec_flush_buffers(d->codec);
    else if (start_seconds) { start_seconds = 0; start = origin; }
    int accepted = 0, reset = 0;
    while (!atomic_load(&video->stop)) {
        int rc = reset ? 0 : decoder_next(d);
        if (rc == 0) {
            if (!accepted || av_seek_frame(d->format, d->stream, start, AVSEEK_FLAG_BACKWARD) < 0) goto done;
            avcodec_flush_buffers(d->codec);
            d->draining = 0;
            first = AV_NOPTS_VALUE; number = 0; began = video_clock();
            accepted = reset = 0;
            continue;
        }
        if (rc < 0) { pc5_copy(video->error, sizeof(video->error), d->error); goto done; }
        atomic_fetch_add(&video->decoded, 1);
        double elapsed = (double)number++ * 1000 / d->fps;
        int64_t pts = d->frame->best_effort_timestamp;
        if (pts != AV_NOPTS_VALUE) {
            double position = ((double)pts - (double)origin) * timebase;
            if (position + 0.0001 < start_seconds) continue;
            if (video->options.video_end && position >= video->options.video_end) {
                reset = 1; continue;
            }
            if (first == AV_NOPTS_VALUE) first = pts;
            elapsed = ((double)pts - (double)first) * av_q2d(d->format->streams[d->stream]->time_base) * 1000;
        }
        if (!accepted) began = video_clock();
        accepted = 1;
        if (elapsed < 0 || elapsed > 24.0 * 3600000) goto done;
        uint64_t due = began + (uint64_t)elapsed;
        while (!atomic_load(&video->stop) && video_clock() < due) usleep(2000);
        if (atomic_load(&video->stop)) break;
        if (video_clock() > due + 100 && number > 1) {
            atomic_fetch_add(&video->dropped, 1);
            continue;  /* Drop late frames, keep UI responsive. */
        }
        while (atomic_load(&video->pending) && !atomic_load(&video->stop)) usleep(2000);
        if (atomic_load(&video->stop)) break;
        int out_w = VW, out_h = VH;
        double ratio = (double)d->frame->width / d->frame->height;
        if (video->options.fit == 1) {
            if (ratio > (double)VW / VH) out_h = (int)(VW / ratio);
            else out_w = (int)(VH * ratio);
        }
        /* Fill uses a centered source crop, so no oversized intermediate buffer. */
        AVFrame *frame = d->frame;
        if (video->options.fit == 2) {
            if (ratio > (double)VW / VH) {
                int target = (int)((double)frame->height * VW / VH) & ~1;
                frame->crop_left = ((frame->width - target) / 2) & ~1;
                frame->crop_right = frame->width - target - frame->crop_left;
            } else {
                int target = (int)((double)frame->width * VH / VW) & ~1;
                frame->crop_top = ((frame->height - target) / 2) & ~1;
                frame->crop_bottom = frame->height - target - frame->crop_top;
            }
            if (av_frame_apply_cropping(frame, 0) < 0) goto done;
        }
        if (out_w < 1 || out_h < 1) goto done;
        memset(video->back, 0, (size_t)VW * VH * 4);
        d->scale = sws_getCachedContext(d->scale, frame->width, frame->height, (enum AVPixelFormat)frame->format,
                                        out_w, out_h, AV_PIX_FMT_BGRA, SWS_BILINEAR, NULL, NULL, NULL);
        uint8_t *planes[4] = { (uint8_t *)(video->back + (VH - out_h) / 2 * VW + (VW - out_w) / 2), NULL, NULL, NULL };
        int lines[4] = { VW * 4, 0, 0, 0 };
        if (!d->scale || sws_scale(d->scale, (const uint8_t *const *)d->frame->data, d->frame->linesize,
                                   0, d->frame->height, planes, lines) != out_h) goto done;
        video_shade(video->back, video->options.darkness);
        atomic_store(&video->pending, 1);
    }
done:
    if (d) decoder_close(d);
    video->failed = !atomic_load(&video->stop);
    atomic_store(&video->done, 1);
    return NULL;
}

static void *video_import_worker(void *context)
{
    Pc5VideoImport *job = context;
    char part[PC5_PATH];
    struct stat before, after;
    FILE *input = NULL, *output = NULL;
    uint8_t *buffer = NULL;
    VideoDecoder *d = NULL;
    part[0] = 0;
    job->result = PC5_VIDEO_FORMAT;
    if (atomic_load(&job->stop)) goto done;
    if (stat(job->source, &before)) { snprintf(job->error, sizeof(job->error), "Source file access failed (%d)", errno); goto done; }
    d = decoder_open(job->source, &job->stop, job->error);
    if (!d) goto done;
    int first = decoder_next(d);
    if (first != 1) {
        pc5_copy(job->error, sizeof(job->error), d->error[0] ? d->error : "Video opened, but no frame could be decoded");
        goto done;
    }
    decoder_close(d); d = NULL;
    job->result = PC5_VIDEO_IO;
    if (pc5_format(part, sizeof(part), "%s.part", job->destination)) goto done;
    input = fopen(job->source, "rb"); output = fopen(part, "wb"); buffer = pc5_alloc(65536);
    if (!input || !output || !buffer) goto done;
    uint64_t copied = 0;
    while (!atomic_load(&job->stop)) {
        size_t n = fread(buffer, 1, 65536, input);
        if (n && fwrite(buffer, 1, n, output) != n) goto done;
        copied += n;
        if (copied > VIDEO_LIMIT) goto done;
        if (!n) { if (ferror(input)) goto done; break; }
    }
    if (atomic_load(&job->stop)) goto done;
    if (fclose(output)) { output = NULL; goto done; } output = NULL;
    if (stat(job->source, &after) || copied != (uint64_t)before.st_size || before.st_size != after.st_size ||
        before.st_mtime != after.st_mtime || rename(part, job->destination)) goto done;
    job->result = PC5_VIDEO_OK;
done:
    if (d) decoder_close(d);
    if (input) fclose(input);
    if (output) fclose(output);
    pc5_free(buffer);
    if (part[0]) unlink(part);
    if (atomic_load(&job->stop)) job->result = PC5_VIDEO_CANCELLED;
    atomic_store(&job->done, 1);
    return NULL;
}
#endif

int pc5_video_start(Pc5Video *video, const char *path)
{
    const Pc5Appearance options = { .volume = 30 };
    return pc5_video_start_options(video, path, &options);
}
int pc5_video_start_options(Pc5Video *video, const char *path, const Pc5Appearance *options)
{
    memset(video, 0, sizeof(*video));
    atomic_init(&video->stop, 0); atomic_init(&video->pending, 0); atomic_init(&video->done, 0);
    atomic_init(&video->decoded, 0); atomic_init(&video->dropped, 0); atomic_init(&video->shown, 0);
    if (strlen(path) >= sizeof(video->path)) return -1;
    pc5_copy(video->path, sizeof(video->path), path);
    if (!pc5_appearance_valid(options)) return -1;
    video->options = *options;
#ifdef PC5_VIDEO_FFMPEG
    if (pc5_thread_start(&video->thread, video_worker, video, 0)) return -1;
    video->running = 1;
    return 0;
#else
    return -1;
#endif
}
int pc5_video_poll(Pc5Video *video)
{
    if (!video->running || !atomic_load(&video->pending)) return 0;
    uint32_t *old = video->front;
    video->front = video->back; video->back = old;
    video->have = 1;
    atomic_fetch_add(&video->shown, 1);
    atomic_store(&video->pending, 0);
    return 1;
}
void pc5_video_stop(Pc5Video *video)
{
    if (video->running) {
        atomic_store(&video->stop, 1);
        pthread_join(video->thread, NULL);
    }
    pc5_free(video->front); pc5_free(video->back);
    memset(video, 0, sizeof(*video));
}
int pc5_video_check(const char *path, atomic_int *stop)
{
#ifndef PC5_VIDEO_FFMPEG
    (void)path; (void)stop;
    return -1;   /* no decoder in this build */
#else
    char error[PC5_ERROR] = "";
    struct stat st;
    if (stat(path, &st) || !S_ISREG(st.st_mode) || (uint64_t)st.st_size > VIDEO_LIMIT) return -1;
    VideoDecoder *d = decoder_open(path, stop, error);
    if (!d) return -1;
    int first = decoder_next(d);
    decoder_close(d);
    return first == 1 && !atomic_load(stop) ? 0 : -1;
#endif
}

int pc5_video_import_start(Pc5VideoImport *job, const char *source, const char *destination)
{
    memset(job, 0, sizeof(*job));
    atomic_init(&job->stop, 0); atomic_init(&job->done, 0);
    job->result = PC5_VIDEO_IO;
    if (strlen(source) >= sizeof(job->source) || strlen(destination) + 5 >= sizeof(job->destination)) return -1;
    pc5_copy(job->source, sizeof(job->source), source);
    pc5_copy(job->destination, sizeof(job->destination), destination);
#ifdef PC5_VIDEO_FFMPEG
    if (pc5_thread_start(&job->thread, video_import_worker, job, 0)) return -1;
    job->running = 1;
    return 0;
#else
    return -1;
#endif
}
void pc5_video_import_end(Pc5VideoImport *job)
{
    if (!job->running) return;
    if (!atomic_load(&job->done)) atomic_store(&job->stop, 1);
    pthread_join(job->thread, NULL); job->running = 0;
}
