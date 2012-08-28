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

#include "common.h"

#undef exit

/* 5 seconds stream duration */
#define STREAM_DURATION   3.0
#define STREAM_FRAME_RATE 8 /* 25 images/s */
#define STREAM_NB_FRAMES  ((int)(STREAM_DURATION * STREAM_FRAME_RATE))
#define STREAM_PIX_FMT PIX_FMT_YUV420P /* default pix_fmt */



/**************************************************************/
/* video output */
AVFrame *picture, *tmp_picture;
uint8_t *video_outbuf;
int frame_count, video_outbuf_size;
struct buf_info ebuf;
struct buf_info pbuf;

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
    c->pix_fmt = STREAM_PIX_FMT;
    if (c->codec_id == CODEC_ID_MPEG2VIDEO) {
        /* just for testing, we also add B frames */
        c->max_b_frames = 2;
    }
    if (c->codec_id == CODEC_ID_MPEG1VIDEO){
        /* Needed to avoid using macroblocks in which some coeffs overflow.
           This does not happen with normal video, it just happens here as
           the motion of the chroma plane does not match the luma plane. */
        c->mb_decision=2;
    }
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
    allocate_user_buffers(size, &pbuf, 1);
    picture_buf = pbuf.user_addr;
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

    video_outbuf = NULL;
    if (!(oc->oformat->flags & AVFMT_RAWPICTURE)) {
        /* allocate output buffer */
        /* XXX: API change will be done */
        /* buffers passed into lav* can be allocated any way you prefer,
           as long as they're aligned enough for the architecture, and
           they're freed appropriately (such as using av_free for buffers
           allocated with av_malloc) */
        video_outbuf_size = 200000;
        allocate_user_buffers(video_outbuf_size, &ebuf, 1);
        video_outbuf = ebuf.user_addr;
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

    c = st->codec;

    if (frame_count >= STREAM_NB_FRAMES) {
        /* no more frame to compress. The codec has a latency of a few
           frames if using B frames, so we get the last frames by
           passing the same picture again */
    } else {
        fill_yuv_image(picture, frame_count, c->width, c->height);
    }


    if (oc->oformat->flags & AVFMT_RAWPICTURE) {
        /* raw video case. The API will change slightly in the near
           futur for that */
        AVPacket pkt;
        av_init_packet(&pkt);

        pkt.flags |= AV_PKT_FLAG_KEY;
        pkt.stream_index= st->index;
        pkt.data= (uint8_t *)picture;
        pkt.size= sizeof(AVPicture);

        ret = av_interleaved_write_frame(oc, &pkt);
    } else {
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
    }
    if (ret != 0) {
        fprintf(stderr, "Error while writing video frame\n");
        return -1;
    }
    frame_count++;

    return 0;
}

static void close_video(AVFormatContext *oc, AVStream *st)
{
    avcodec_close(st->codec);
    free_user_buffers(&pbuf, 1);
    if (tmp_picture) {
        av_free(tmp_picture->data[0]);
        av_free(tmp_picture);
    }
    free_user_buffers(&ebuf, 1);
}

/**************************************************************/
/* media file output */

int ff_example(const char *filename, const char *format)
{
    AVOutputFormat *fmt;
    AVFormatContext *oc;
    AVStream *video_st;
    double audio_pts, video_pts;
    int i;

    /* auto detect the output format from the name. default is
       mpeg. */
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

    /* add the audio and video streams using the default format codecs
       and initialize the codecs */
    video_st = NULL;
    if (fmt->video_codec != CODEC_ID_NONE) {
        video_st = add_video_stream(oc, fmt->video_codec);
    }

    /* set the output parameters (must be done even if no
       parameters). */
    if (av_set_parameters(oc, NULL) < 0) {
        fprintf(stderr, "Invalid output format parameters\n");
        exit(1);
    }

    av_dump_format(oc, 0, filename, 1);

    /* now that all the parameters are set, we can open the audio and
       video codecs and allocate the necessary encode buffers */
    if (video_st)
        open_video(oc, video_st);

    /* open the output file, if needed */
    if (!(fmt->flags & AVFMT_NOFILE)) {
        if (avio_open(&oc->pb, filename, URL_WRONLY) < 0) {
            fprintf(stderr, "Could not open '%s'\n", filename);
            exit(1);
        }
    }

    /* write the stream header, if any */
    av_write_header(oc);

    for(;;) {
        /* compute current audio and video time */
        audio_pts = 0.0;

        if (video_st)
            video_pts = (double)video_st->pts.val * video_st->time_base.num / video_st->time_base.den;
        else
            video_pts = 0.0;

        if ((!video_st || video_pts >= STREAM_DURATION))
            break;

        /* write interleaved audio and video frames */
        if (write_video_frame(oc, video_st) < 0)
            break;
    }

    /* write the trailer, if any.  the trailer must be written
     * before you close the CodecContexts open when you wrote the
     * header; otherwise write_trailer may try to use memory that
     * was freed on av_codec_close() */
    av_write_trailer(oc);

    /* close each codec */
    if (video_st)
        close_video(oc, video_st);

    /* free the streams */
    for(i = 0; i < oc->nb_streams; i++) {
        av_freep(&oc->streams[i]->codec);
        av_freep(&oc->streams[i]);
    }

    if (!(fmt->flags & AVFMT_NOFILE)) {
        /* close the output file */
        avio_close(oc->pb);
    }

    /* free the stream */
    av_free(oc);

    return 0;
}

static int decode_example(const char *filename)
{
    AVFormatContext *fctx = NULL;
    AVCodec *codec;
    AVCodecContext *avctx;
    int video_st = -1;
    int i, got_pic;
    AVPacket pkt;
    AVFrame *picture;

    av_open_input_file(&fctx, filename, NULL, 0, NULL);
    if (fctx == NULL)
        return AVERROR(1);

    av_find_stream_info(fctx);

    dump_format(fctx, 0, filename, 0);

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

    while (av_read_frame(fctx, &pkt) >= 0) {
        int nb;
        nb = avcodec_decode_video2(avctx, picture, &got_pic, &pkt);
        if (nb < 0) {
            av_log(avctx, AV_LOG_ERROR, "error in decoding\n");
            goto decode_cleanup;
        }
    }

decode_cleanup:
    avcodec_close(avctx);
    av_free(picture);
    av_close_input_file(fctx);
    return 0;
}

int main()
{
    /* initialize libavcodec, and register all codecs and formats */
    av_register_all();

    ff_example("test.mkv", "matroska");
    decode_example("test.mkv");

    return 0;
}
