/*
 * common.c
 *
 *  Created on: 1.8.2012
 *      Author: honza
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#include <linux/videodev2.h>
#include <linux/media.h>
#include <linux/v4l2-subdev.h>

#include "cmem.h"
#include "common.h"

int info(const char *format, ...)
{
    va_list fmtargs;

    if (!isatty(fileno(stdout)))
        return 0;

    va_start(fmtargs, format);
    vfprintf(stdout, format, fmtargs);
    va_end(fmtargs);

    return 0;
}

int allocate_user_buffers(int buf_size,
        struct buf_info *buffers, int num_bufs)
{
    int i;

    CMEM_AllocParams alloc_params;
    info("calling cmem utilities for allocating frame buffers\n");
    CMEM_init();

    alloc_params.type = CMEM_HEAP;
    alloc_params.flags = CMEM_NONCACHED;
    alloc_params.alignment = 32;

    info("Allocating capture buffers :buf size = %d \n", buf_size);

    for (i = 0; i < num_bufs; i++) {
        memset(&buffers[i], 0, sizeof(struct buf_info));
        buffers[i].index = i;
        buffers[i].user_addr = CMEM_alloc(buf_size, &alloc_params);
        if (buffers[i].user_addr) {
            buffers[i].phy_addr = CMEM_getPhys(buffers[i].user_addr);
            if (0 == buffers[i].phy_addr) {
                perror("Failed to get phy cmem buffer address\n");
                return -1;
            }

            buffers[i].size = buf_size;

        } else {
            perror("Failed to allocate cmem buffer\n");
            return -1;
        }
    }

    return 0;
}

int free_user_buffers(struct buf_info *buffers, int num_bufs)
{
    int i, ret;
    CMEM_AllocParams alloc_params;

    alloc_params.type = CMEM_HEAP;
    alloc_params.flags = CMEM_NONCACHED;
    alloc_params.alignment = 32;

    ret = 0;
    for (i = 0; i < num_bufs; i++) {
        ret |= CMEM_free(buffers[i].user_addr, &alloc_params);
    }

    return ret;
}

void video_enum_inputs(int fd)
{
    struct v4l2_input input;
    int ret;
    int i;

    info("V4L2 inputs: \n");

    input.type = V4L2_INPUT_TYPE_CAMERA;
    i = 0;
    while (1) {
        memset(&input, 0, sizeof(struct v4l2_input));

        input.index = i;

        ret = ioctl(fd, VIDIOC_ENUMINPUT, &input);
        if (ret < 0)
            break;

        info("[%x].%s\n", i, input.name);
        i++;
    }
}

int video_set_input(int fd, int in)
{
    struct v4l2_input input;

    memset(&input, 0, sizeof(struct v4l2_input));
    input.type = V4L2_INPUT_TYPE_CAMERA;
    input.index = in;

    if (ioctl(fd, VIDIOC_S_INPUT, &input.index) < 0) {
        info("failed to set CAMERA with capture device\n");
        return -1;
    }

    return 0;
}

int video_clean_buffers(int fd)
{
    struct v4l2_requestbuffers req;

    memset(&req, 0, sizeof(struct v4l2_requestbuffers));
    req.count = 0;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_USERPTR;

    if (ioctl(fd, VIDIOC_REQBUFS, &req) < 0) {
        info("call to VIDIOC_REQBUFS failed\n");
        return -1;
    }

    return 0;
}

int video_prepare_buffers(int fd, int width, int height,
        struct buf_info *capture_buffers, int buf_count)
{
    int i;
    struct v4l2_requestbuffers req;

    memset(&req, 0, sizeof(struct v4l2_requestbuffers));
    req.count = buf_count;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_USERPTR;

    if (ioctl(fd, VIDIOC_REQBUFS, &req) < 0) {
        info("call to VIDIOC_REQBUFS failed\n");
        return -1;
    }

    if (req.count != buf_count) {
        info("%d buffers not supported by capture device", buf_count);
        return -1;
    }

    /* queue the buffers */
    for (i = 0; i < buf_count; i++) {
        struct v4l2_buffer buf;

        memset(&buf, 0, sizeof(struct v4l2_buffer));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_USERPTR;
        buf.index = i;
        buf.length = capture_buffers[i].size;
        buf.m.userptr = (unsigned long) capture_buffers[i].user_addr;
        capture_buffers[i].width = width;
        capture_buffers[i].height = height;

        if (ioctl(fd, VIDIOC_QBUF, &buf) < 0) {
            info("call to VIDIOC_QBUF failed\n");
            return -1;
        }
    }

    return 0;
}

int get_pitch(int capt_fd)
{
    struct v4l2_format v4l2_fmt;

    memset(&v4l2_fmt, 0, sizeof(v4l2_fmt));
    v4l2_fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (ioctl(capt_fd, VIDIOC_G_FMT, &v4l2_fmt) == -1) {
        info("failed to get format from capture device \n");
        return -1;
    }

    return v4l2_fmt.fmt.pix.bytesperline;
}

int video_start_stream(int fd)
{
    enum v4l2_buf_type type;

    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(fd, VIDIOC_STREAMON, &type) == -1) {
        perror("StartStreaming:ioctl:VIDIOC_STREAMON:\n");
        return -1;
    }

    return 0;
}

int video_stop_stream(int fd)
{
    enum v4l2_buf_type type;
    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (ioctl(fd, VIDIOC_STREAMOFF, &type) == -1) {
        perror("cleanup_capture :ioctl:VIDIOC_STREAMOFF");
        return -1;
    }

    return 0;
}

int video_fps_avg(int captFrmCnt, struct timeval timestamp)
{
    static unsigned long long prev_ts;
    unsigned long long curr_ts;
    static unsigned long fp_period_average = 0;
    static unsigned long fp_period_max = 0;
    static unsigned long fp_period_min = 0;
    unsigned long fp_period;

    if (captFrmCnt == 0)
        prev_ts = (timestamp.tv_sec * 1000000) + timestamp.tv_usec;
    else {
        curr_ts = (timestamp.tv_sec * 1000000) + timestamp.tv_usec;
        fp_period = curr_ts - prev_ts;
        if (captFrmCnt == 1) {
            fp_period_max = fp_period_min = fp_period_average = fp_period;
        } else {
            /* calculate jitters and average */
            if (fp_period > fp_period_max)
                fp_period_max = fp_period;
            if (fp_period < fp_period_min)
                fp_period_min = fp_period;

            fp_period_average = ((fp_period_average * captFrmCnt) + fp_period)
                                    / (captFrmCnt + 1);
        }
        prev_ts = curr_ts;
    }

    return fp_period_average;
}
