/*
 * Copyright (C) 2011 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*!
 * \file      exynos_v4l2.h
 * \brief     header file for libv4l2
 * \author    Jinsung Yang (jsgood.yang@samsung.com)
 * \date      2011/12/15
 *
 * <b>Revision History: </b>
 * - 2011/12/15 : Jinsung Yang (jsgood.yang@samsung.com) \n
 *   Initial version
 *
 */

/*!
 * \defgroup exynos_v4l2
 * \brief API for v4l2
 * \addtogroup Exynos
 */

#ifndef __EXYNOS_LIB_V4L2_H__
#define __EXYNOS_LIB_V4L2_H__

#ifdef __cplusplus
extern "C" {
#endif

/* V4L2 */
#include <stdbool.h>
#include "videodev2.h" /* vendor specific videodev2.h */
#include "videodev2_exynos_media.h"

/*! \ingroup exynos_v4l2 */
int exynos_v4l2_open(const char *filename, int oflag, ...);
/*! \ingroup exynos_v4l2 */
int exynos_v4l2_open_devname(const char *devname, int oflag, ...);
/*! \ingroup exynos_v4l2 */
int exynos_v4l2_close(int fd);
/*! \ingroup exynos_v4l2 */
bool exynos_v4l2_enuminput(int fd, int index, char *input_name_buf);
/*! \ingroup exynos_v4l2 */
int exynos_v4l2_s_input(int fd, int index);
/*! \ingroup exynos_v4l2 */
bool exynos_v4l2_querycap(int fd, unsigned int need_caps);
/*! \ingroup exynos_v4l2 */
bool exynos_v4l2_enum_fmt(int fd, enum v4l2_buf_type type, unsigned int fmt);
/*! \ingroup exynos_v4l2 */
int exynos_v4l2_g_fmt(int fd, struct v4l2_format *fmt);
/*! \ingroup exynos_v4l2 */
int exynos_v4l2_s_fmt(int fd, struct v4l2_format *fmt);
/*! \ingroup exynos_v4l2 */
int exynos_v4l2_try_fmt(int fd, struct v4l2_format *fmt);
/*! \ingroup exynos_v4l2 */
int exynos_v4l2_reqbufs(int fd, struct v4l2_requestbuffers *req);
/*! \ingroup exynos_v4l2 */
int exynos_v4l2_querybuf(int fd, struct v4l2_buffer *buf);
/*! \ingroup exynos_v4l2 */
int exynos_v4l2_qbuf(int fd, struct v4l2_buffer *buf);
/*! \ingroup exynos_v4l2 */
int exynos_v4l2_dqbuf(int fd, struct v4l2_buffer *buf);
/*! \ingroup exynos_v4l2 */
int exynos_v4l2_streamon(int fd, enum v4l2_buf_type type);
/*! \ingroup exynos_v4l2 */
int exynos_v4l2_streamoff(int fd, enum v4l2_buf_type type);
/*! \ingroup exynos_v4l2 */
int exynos_v4l2_cropcap(int fd, struct v4l2_cropcap *crop);
/*! \ingroup exynos_v4l2 */
int exynos_v4l2_g_crop(int fd, struct v4l2_crop *crop);
/*! \ingroup exynos_v4l2 */
int exynos_v4l2_s_crop(int fd, struct v4l2_crop *crop);
/*! \ingroup exynos_v4l2 */
int exynos_v4l2_g_ctrl(int fd, unsigned int id, int *value);
/*! \ingroup exynos_v4l2 */
int exynos_v4l2_s_ctrl(int fd, unsigned int id, int value);
/*! \ingroup exynos_v4l2 */
int exynos_v4l2_g_parm(int fd, struct v4l2_streamparm *streamparm);
/*! \ingroup exynos_v4l2 */
int exynos_v4l2_s_parm(int fd, struct v4l2_streamparm *streamparm);
/*! \ingroup exynos_v4l2 */
int exynos_v4l2_g_ext_ctrl(int fd, struct v4l2_ext_controls *ctrl);
/*! \ingroup exynos_v4l2 */
int exynos_v4l2_s_ext_ctrl(int fd, struct v4l2_ext_controls *ctrl);

/* V4L2_SUBDEV */
#include <v4l2-subdev.h>

/*! \ingroup exynos_v4l2 */
int exynos_subdev_open(const char *filename, int oflag, ...);
/*! \ingroup exynos_v4l2 */
int exynos_subdev_get_node_num(const char *devname, int oflag, ...);
/*! \ingroup exynos_v4l2 */
int exynos_subdev_open_devname(const char *devname, int oflag, ...);
/*! \ingroup exynos_v4l2 */
int exynos_subdev_close(int fd);
/*! \ingroup exynos_v4l2 */
int exynos_subdev_enum_frame_size(int fd, struct v4l2_subdev_frame_size_enum *frame_size_enum);
/*! \ingroup exynos_v4l2 */
int exynos_subdev_g_fmt(int fd, struct v4l2_subdev_format *fmt);
/*! \ingroup exynos_v4l2 */
int exynos_subdev_s_fmt(int fd, struct v4l2_subdev_format *fmt);
/*! \ingroup exynos_v4l2 */
int exynos_subdev_g_crop(int fd, struct v4l2_subdev_crop *crop);
/*! \ingroup exynos_v4l2 */
int exynos_subdev_s_crop(int fd, struct v4l2_subdev_crop *crop);
/*! \ingroup exynos_v4l2 */
int exynos_subdev_enum_frame_interval(int fd, struct v4l2_subdev_frame_interval_enum *frame_internval_enum);
/*! \ingroup exynos_v4l2 */
int exynos_subdev_g_frame_interval(int fd, struct v4l2_subdev_frame_interval *frame_internval_enum);
/*! \ingroup exynos_v4l2 */
int exynos_subdev_s_frame_interval(int fd, struct v4l2_subdev_frame_interval *frame_internval_enum);
/*! \ingroup exynos_v4l2 */
int exynos_subdev_enum_mbus_code(int fd, struct v4l2_subdev_mbus_code_enum *mbus_code_enum);

/* MEDIA CONTORLLER */
#include <media.h>

/*! media_link
 * \ingroup exynos_v4l2
 */
struct media_link {
    struct media_pad *source;
    struct media_pad *sink;
    struct media_link *twin;
    __u32 flags;
    __u32 padding[3];
};

/*! media_link
 * \ingroup exynos_v4l2
 */
struct media_pad {
    struct media_entity *entity;
    __u32 index;
    __u32 flags;
    __u32 padding[3];
};

/*! media_link
 * \ingroup exynos_v4l2
 */
struct media_entity {
    struct media_device *media;
    struct media_entity_desc info;
    struct media_pad *pads;
    struct media_link *links;
    unsigned int max_links;
    unsigned int num_links;

    char devname[32];
    int fd;
    __u32 padding[6];
};

/*! media_link
 * \ingroup exynos_v4l2
 */
struct media_device {
    int fd;
    struct media_entity *entities;
    unsigned int entities_count;
    void (*debug_handler)(void *, ...);
    void *debug_priv;
    __u32 padding[6];
};

/*! \ingroup exynos_v4l2 */
struct media_device *exynos_media_open(const char *filename);
/*! \ingroup exynos_v4l2 */
void exynos_media_close(struct media_device *media);
/*! \ingroup exynos_v4l2 */
struct media_pad *exynos_media_entity_remote_source(struct media_pad *pad);
/*! \ingroup exynos_v4l2 */
struct media_entity *exynos_media_get_entity_by_name(struct media_device *media, const char *name, size_t length);
/*! \ingroup exynos_v4l2 */
struct media_entity *exynos_media_get_entity_by_id(struct media_device *media, __u32 id);
/*! \ingroup exynos_v4l2 */
int exynos_media_setup_link(struct media_device *media, struct media_pad *source, struct media_pad *sink, __u32 flags);
/*! \ingroup exynos_v4l2 */
int exynos_media_reset_links(struct media_device *media);
/*! \ingroup exynos_v4l2 */
struct media_pad *exynos_media_parse_pad(struct media_device *media, const char *p, char **endp);
/*! \ingroup exynos_v4l2 */
struct media_link *exynos_media_parse_link(struct media_device *media, const char *p, char **endp);
/*! \ingroup exynos_v4l2 */
int exynos_media_parse_setup_link(struct media_device *media, const char *p, char **endp);
/*! \ingroup exynos_v4l2 */
int exynos_media_parse_setup_links(struct media_device *media, const char *p);

#ifdef __cplusplus
}
#endif

#endif /* __EXYNOS_LIB_V4L2_H__ */
