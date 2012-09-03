/*
 * Libavformat API example: Output a media file in any supported
 * libavformat format. Modified for libdm365 testing
 *
 * Copyright (c) 2011 Jan Pohanka
 * Copyright (c) 2003 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdint.h>

#include <libavformat/avformat.h>
#include <libswscale/swscale.h>

#include "cmem.h"

#undef exit

/* 5 seconds stream duration */
#define STREAM_DURATION   2.0
#define STREAM_FRAME_RATE 5
#define STREAM_NB_FRAMES  ((int)(STREAM_DURATION * STREAM_FRAME_RATE))


/**************************************************************/
/* video output */
static AVFrame *picture, *tmp_picture;
static uint8_t *video_outbuf;
static int frame_count, video_outbuf_size;
static CMEM_AllocParams alloc_params = {
        .type = CMEM_HEAP,
        .alignment = 32,
        .flags = CMEM_NONCACHED,
};

/* add a video output stream */
static AVStream *add_video_stream(AVFormatContext *oc, enum CodecID codec_id)
{
    AVCodecContext *c;
    AVStream *st;

    st = av_new_stream(oc, 0);
    if (!st) {
        fprintf(stderr, "Could not alloc stream\n");
        exit(1);
    }

    c = st->codec;
    c->codec_id = codec_id;
    c->codec_type = AVMEDIA_TYPE_VIDEO;

    /* put sample parameters */
    c->bit_rate = 100000;
    /* resolution must be a multiple of two */
    c->width = 640;
    c->height = 480;
    /* time base: this is the fundamental unit of time (in seconds) in terms
       of which frame timestamps are represented. for fixed-fps content,
       timebase should be 1/framerate and timestamp increments should be
       identically 1. */
    c->time_base.den = 5000;
    c->time_base.num = 1000;
    c->gop_size = 12; /* emit one intra frame every twelve frames at most */
    c->pix_fmt = PIX_FMT_NV12;

    // some formats want stream headers to be separate
    if(oc->oformat->flags & AVFMT_GLOBALHEADER)
        c->flags |= CODEC_FLAG_GLOBAL_HEADER;

    return st;
}

static AVFrame *alloc_picture(enum PixelFormat pix_fmt, int width, int height)
{
    AVFrame *picture;
    uint8_t *picture_buf;
    int size;

    picture = avcodec_alloc_frame();
    if (!picture)
        return NULL;
    size = avpicture_get_size(pix_fmt, width, height);
    picture_buf = CMEM_alloc(size, &alloc_params);
    if (!picture_buf) {
        av_free(picture);
        return NULL;
    }
    avpicture_fill((AVPicture *)picture, picture_buf,
                   pix_fmt, width, height);
    return picture;
}

static void open_video(AVFormatContext *oc, AVStream *st)
{
    AVCodec *codec;
    AVCodecContext *c;

    c = st->codec;

    /* find the video encoder */
    codec = avcodec_find_encoder(c->codec_id);
    if (!codec) {
        fprintf(stderr, "codec not found\n");
        exit(1);
    }

    /* open the codec */
    if (avcodec_open(c, codec) < 0) {
        fprintf(stderr, "could not open codec\n");
        exit(1);
    }

    if (codec->pix_fmts && codec->pix_fmts[0] != -1) {
        c->pix_fmt = codec->pix_fmts[0];
    }

    video_outbuf = NULL;
    if (!(oc->oformat->flags & AVFMT_RAWPICTURE)) {

        /* allocate output buffer */
        /* buffers passed into lav* can be allocated any way you prefer,
           as long as they're aligned enough for the architecture, and
           they're freed appropriately (such as using av_free for buffers
           allocated with av_malloc) */
        video_outbuf_size = 3*1024*1024;
        video_outbuf = CMEM_alloc(video_outbuf_size, &alloc_params);
    }

    /* allocate the encoded raw picture */
    picture = alloc_picture(c->pix_fmt, c->width, c->height);
    if (!picture) {
        fprintf(stderr, "Could not allocate picture\n");
        exit(1);
    }

    /* if the output format is not YUV420P, then a temporary YUV420P
       picture is needed too. It is then converted to the required
       output format */
    tmp_picture = NULL;
    if (c->pix_fmt != PIX_FMT_YUV420P) {
        tmp_picture = alloc_picture(PIX_FMT_YUV420P, c->width, c->height);
        if (!tmp_picture) {
            fprintf(stderr, "Could not allocate temporary picture\n");
            exit(1);
        }
    }
}

/* prepare a dummy image */
static void fill_yuv_image(AVFrame *pict, int frame_index, int width, int height)
{
    int x, y, i;

    i = frame_index;

    /* Y */
    for(y=0;y<height;y++) {
        for(x=0;x<width;x++) {
            pict->data[0][y * pict->linesize[0] + x] = x + y + i * 3;
        }
    }

    /* Cb and Cr */
    for(y=0;y<height/2;y++) {
        for(x=0;x<width/2;x++) {
            pict->data[1][y * pict->linesize[1] + x] = 128 + y + i * 2;
            pict->data[2][y * pict->linesize[2] + x] = 64 + x + i * 5;
        }
    }
}

static int write_video_frame(AVFormatContext *oc, AVStream *st)
{
    int out_size, ret;
    AVCodecContext *c;
    static struct SwsContext *sctx = NULL;

    c = st->codec;

    if (c->pix_fmt != PIX_FMT_YUV420P) {
        if (sctx == NULL) {
            sctx = sws_getContext(c->width, c->height, PIX_FMT_YUV420P,
                    c->width, c->height, c->pix_fmt,
                    SWS_BICUBIC, NULL, NULL, NULL);
            if (sctx == NULL)
                return -1;
        }

        fill_yuv_image(tmp_picture, frame_count, c->width, c->height);
        sws_scale(sctx, (const uint8_t * const *) tmp_picture->data, tmp_picture->linesize,
                0, c->height, picture->data, picture->linesize);
    } else {
        fill_yuv_image(picture, frame_count, c->width, c->height);
    }


    /* encode the image */
    out_size = avcodec_encode_video(c, video_outbuf, video_outbuf_size, picture);
    /* if zero size, it means the image was buffered */
    if (out_size > 0) {
        AVPacket pkt;
        av_init_packet(&pkt);

        if (c->coded_frame->pts != AV_NOPTS_VALUE)
            pkt.pts= av_rescale_q(c->coded_frame->pts, c->time_base, st->time_base);
        if(c->coded_frame->key_frame)
            pkt.flags |= AV_PKT_FLAG_KEY;
        pkt.stream_index= st->index;
        pkt.data= video_outbuf;
        pkt.size= out_size;

        /* write the compressed frame in the media file */
        ret = av_interleaved_write_frame(oc, &pkt);
    } else {
        ret = 0;
    }

    if (ret != 0) {
        fprintf(stderr, "Error while writing video frame\n");
        return -1;
    }

    printf("Frame written: %d\n", frame_count);
    frame_count++;

    return 0;
}

static void close_video(AVFormatContext *oc, AVStream *st)
{
    avcodec_close(st->codec);
    CMEM_free(picture->data[0], &alloc_params);
    av_free(picture);
    if (tmp_picture) {
        CMEM_free(tmp_picture->data[0], &alloc_params);
        av_free(tmp_picture);
    }
    CMEM_free(video_outbuf, &alloc_params);
}

/**************************************************************/
/* media file output */

int ff_example(const char *filename, const char *format)
{
    AVOutputFormat *fmt;
    AVFormatContext *oc;
    AVStream *video_st;
    double video_pts;
    int i;

    fmt = av_guess_format(format, NULL, NULL);
    if (!fmt) {
        fprintf(stderr, "Could not find suitable output format\n");
        exit(1);
    }

    fmt->video_codec = CODEC_ID_H264;

    /* allocate the output media context */
    oc = avformat_alloc_context();
    if (!oc) {
        fprintf(stderr, "Memory error\n");
        exit(1);
    }
    oc->oformat = fmt;
    snprintf(oc->filename, sizeof(oc->filename), "%s", filename);

    video_st = NULL;
    if (fmt->video_codec != CODEC_ID_NONE)
        video_st = add_video_stream(oc, fmt->video_codec);

    av_dump_format(oc, 0, filename, 1);

    /* now that all the parameters are set, we can open the audio and
       video codecs and allocate the necessary encode buffers */
    if (video_st)
        open_video(oc, video_st);

    if (avio_open(&oc->pb, filename, URL_WRONLY) < 0) {
        fprintf(stderr, "Could not open '%s'\n", filename);
        exit(1);
    }


    /* write the stream header, if any */
    avformat_write_header(oc, NULL);

    for(;;) {

        video_pts = (double)video_st->pts.val * video_st->time_base.num / video_st->time_base.den;
        printf("pts: %f\n", video_pts);

        if (frame_count > STREAM_NB_FRAMES)
            break;

        /* write interleaved audio and video frames */
        if (write_video_frame(oc, video_st) < 0)
            break;
    }

    printf("%d frames written\n", frame_count);

    av_write_trailer(oc);

    /* close each codec */
    if (video_st)
        close_video(oc, video_st);

    /* free the streams */
    for(i = 0; i < oc->nb_streams; i++) {
        av_freep(&oc->streams[i]->codec);
        av_freep(&oc->streams[i]);
    }

    avio_close(oc->pb);

    /* free the stream */
    av_free(oc);

    return 0;
}

static void pgm_save(unsigned char *buf, int wrap, int xsize, int ysize, char *filename)
{
    FILE *f;
    int i;

    f = fopen(filename, "w");
    fprintf(f, "P5\n%d %d\n%d\n", xsize, ysize, 255);
    for (i = 0; i < ysize; i++)
        fwrite(buf + i * wrap, 1, xsize, f);
    fclose(f);
}

static int my_scale(const AVPicture *picture, int width, int height,
        AVPicture *dst_picture, int factor)
{
    int i, j;

    for (j = 0; j < height/factor; j++) {
        uint8_t *src_lineY = picture->data[0] + j*factor * picture->linesize[0];
        uint8_t *dst_lineY = dst_picture->data[0] + j * dst_picture->linesize[0];

        for (i = 0; i < width/factor; i++) {
            *(dst_lineY + i) = *(src_lineY + i*factor);
        }
    }
    for (j = 0; j < height/2/factor; j++) {
        uint8_t *src_lineUV = picture->data[1] + j*factor * picture->linesize[1];
        uint8_t *dst_lineUV = dst_picture->data[1] + j * dst_picture->linesize[1];

        for (i = 0; i < width/factor; i+=2) {
            *(dst_lineUV + i) = *(src_lineUV + i*factor);
            *(dst_lineUV + i+1) = *(src_lineUV + i*factor + 1);
        }
    }

    return 0;
}

static int save_image(const AVPicture *picture, enum PixelFormat pix_fmt,
        int width, int height, const char *filename)
{
    AVCodec *codec;
    AVCodecContext *avctx;
    AVFrame *tmp_picture;
    uint8_t *outbuf;
    int outbuf_size;
    int size;
    FILE *f;

    avctx = avcodec_alloc_context();

    avctx->codec_id = CODEC_ID_BMP;
    avctx->codec_type = AVMEDIA_TYPE_VIDEO;
    avctx->width = width;
    avctx->height = height;
    avctx->pix_fmt = PIX_FMT_BGR24;
    avctx->time_base = (AVRational) {1, 1};

    codec = avcodec_find_encoder(avctx->codec_id);
    if (!codec) {
        fprintf(stderr, "codec not found\n");
        return -1;
    }

    /* open the codec */
    if (avcodec_open(avctx, codec) < 0) {
        fprintf(stderr, "could not open codec\n");
        return -1;
    }

    if (pix_fmt != PIX_FMT_BGR24) {
        struct SwsContext *sctx;

        tmp_picture = alloc_picture(PIX_FMT_BGR24, avctx->width, avctx->height);
        if (!tmp_picture)
            return -1;

        sctx = sws_getContext(avctx->width, avctx->height, pix_fmt,
                avctx->width, avctx->height, PIX_FMT_BGR24,
                SWS_POINT, NULL, NULL, NULL);

        sws_scale(sctx, (const uint8_t * const *) picture->data, picture->linesize,
                0, avctx->height, tmp_picture->data, tmp_picture->linesize);
    } else {
        tmp_picture = (AVFrame *)picture;
    }

    outbuf_size = 6*1024*1024;
    outbuf = av_malloc(outbuf_size);
    if (!outbuf)
        return AVERROR(ENOMEM);

    f = fopen(filename, "wb");
    size = avcodec_encode_video(avctx, outbuf, outbuf_size, tmp_picture);
    fwrite(outbuf, sizeof(uint8_t), size, f);
    fclose(f);

    if (pix_fmt != PIX_FMT_BGR24) {
        CMEM_free(tmp_picture->data[0], &alloc_params);
    }
    av_free(tmp_picture);
    av_free(outbuf);
    av_free(avctx);

    return 0;
}

static int decode_example(const char *filename)
{
    AVFormatContext *fctx = NULL;
    AVCodec *codec;
    AVCodecContext *avctx;
    int video_st = -1;
    int i, got_pic;
    AVFrame *picture, *tmp_picture;
    int size;
    uint8_t *tmp_buf;
    int ret = 0;

    avformat_open_input(&fctx, filename, NULL, NULL);
    if (fctx == NULL)
        return AVERROR(1);

    av_find_stream_info(fctx);

    av_dump_format(fctx, 0, filename, 0);

    for (i = 0; i < fctx->nb_streams; i++) {
        if (fctx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_st = i;
            break;
        }
    }

    avctx = fctx->streams[video_st]->codec;

    codec = avcodec_find_decoder_by_name("libdm365_h264");
    if (codec == NULL) {
        av_log(avctx, AV_LOG_ERROR, "unsupported codec\n");
        return AVERROR(1);
    }

    if (avcodec_open(avctx, codec) < 0) {
        av_log(avctx, AV_LOG_ERROR, "cannot open codec\n");
        return AVERROR(1);
    }

    picture = avcodec_alloc_frame();
    tmp_picture = avcodec_alloc_frame();

    size = avpicture_get_size(PIX_FMT_YUV420P, avctx->width, avctx->height);
    tmp_buf = av_malloc(size);
    if (tmp_buf == NULL) {
        ret = AVERROR(ENOMEM);
        goto decode_cleanup;
    }
    avpicture_fill((AVPicture *)tmp_picture, tmp_buf,
            PIX_FMT_NV12, avctx->width, avctx->height);

    for (i = 0; i < 10; i++) {
        AVPacket pkt;
        int nb;
        char fname[32];
        int factor = 2;

        if (av_read_frame(fctx, &pkt) < 0)
            break;

        nb = avcodec_decode_video2(avctx, picture, &got_pic, &pkt);
        if (nb < 0) {
            av_log(avctx, AV_LOG_ERROR, "error in decoding\n");
            goto decode_cleanup;
        }
        printf("Decoded frame: %d\n", i);

        my_scale((AVPicture *) picture, avctx->width, avctx->height,
                (AVPicture *) tmp_picture, factor);

        sprintf(fname, "frame%02d.pgm", i+1);
        pgm_save(picture->data[0], picture->linesize[0],
                avctx->width, avctx->height, fname);

        sprintf(fname, "frame%02d.bmp", i+1);
        save_image((AVPicture *)tmp_picture, avctx->pix_fmt,
                avctx->width/factor, avctx->height/factor, fname);
    }

decode_cleanup:
    av_free(picture);
    av_free(tmp_picture->data[0]);
    av_free(tmp_picture);
    av_close_input_file(fctx);
    avcodec_close(avctx);
    return ret;
}

#ifdef CE_TEST
#include <xdc/std.h>
#include <ti/sdo/ce/CERuntime.h>
#endif

int main()
{
    CMEM_init();

#ifdef CE_TEST
    CERuntime_init();
    CERuntime_exit();
    CERuntime_init();
    CERuntime_exit();
#endif

    /* initialize libavcodec, and register all codecs and formats */
    av_register_all();

    /* TODO: can't run both yet, some problem with CE init and exit */
//    ff_example("test.mkv", "matroska");
    decode_example("test.mkv");

    CMEM_exit();
    return 0;
}
