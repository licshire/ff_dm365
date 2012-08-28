/*
 * common.h
 *
 *  Created on: 1.8.2012
 *      Author: honza
 */

#ifndef COMMON_H_
#define COMMON_H_

#include "cmem.h"

#define ALIGN(x, y)     (((x + (y-1))/y)*y)

struct buf_info {
    void *user_addr;
    unsigned long phy_addr;
    unsigned long size;
    unsigned long bytes_used;
    unsigned width;
    unsigned height;
    int index;
};

int info(const char *format, ...);
int allocate_user_buffers(int buf_size,
        struct buf_info *buffers, int num_bufs);
int free_user_buffers(struct buf_info *buffers, int num_bufs);
void video_enum_inputs(int fd);
int video_set_input(int fd, int in);
int video_prepare_buffers(int fd, int width, int height,
        struct buf_info *capture_buffers, int buf_count);
int video_clean_buffers(int fd);
int get_pitch(int capt_fd);
int video_start_stream(int fd);
int video_stop_stream(int fd);
int video_fps_avg(int captFrmCnt, struct timeval timestamp);


#endif /* COMMON_H_ */
