/*
 * Copyright (C) 2008 The Android Open Source Project
 * Copyright@ Samsung Electronics Co. LTD
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
 * \file      exynos_gscaler.c
 * \brief     header file for Gscaler HAL
 * \author    ShinWon Lee (shinwon.lee@samsung.com)
 * \date      2012/01/09
 *
 * <b>Revision History: </b>
 * - 2012.01.09 : ShinWon Lee(shinwon.lee@samsung.com) \n
 *   Create
 *
 * - 2012.02.07 : ShinWon Lee(shinwon.lee@samsung.com) \n
 *   Change file name to exynos_gscaler.h
 *
 * - 2012.02.09 : Sangwoo, Parkk(sw5771.park@samsung.com) \n
 *   Use Multiple Gscaler by Multiple Process
 *
 * - 2012.02.20 : Sangwoo, Park(sw5771.park@samsung.com) \n
 *   Add exynos_gsc_set_rotation() API
 *
 * - 2012.02.20 : ShinWon Lee(shinwon.lee@samsung.com) \n
 *   Add size constrain
 *
 */

//#define LOG_NDEBUG 0
#include "exynos_gsc_utils.h"
#include "content_protect.h"

#include <log.h>

static int exynos_gsc_m2m_wait_frame_done(void *handle);
static int exynos_gsc_m2m_stop(void *handle);

static unsigned int m_gsc_get_plane_count(
    int v4l_pixel_format)
{
    int plane_count = 0;

    switch (v4l_pixel_format) {
    case V4L2_PIX_FMT_RGB32:
    case V4L2_PIX_FMT_BGR32:
    case V4L2_PIX_FMT_RGB24:
    case V4L2_PIX_FMT_RGB565:
    case V4L2_PIX_FMT_RGB555X:
    case V4L2_PIX_FMT_RGB444:
    case V4L2_PIX_FMT_YUYV:
    case V4L2_PIX_FMT_UYVY:
    case V4L2_PIX_FMT_NV16:
    case V4L2_PIX_FMT_NV61:
    case V4L2_PIX_FMT_YVU420:
        plane_count = 1;
        break;
    case V4L2_PIX_FMT_NV12M:
    case V4L2_PIX_FMT_NV12MT_16X16:
    case V4L2_PIX_FMT_NV12:
    case V4L2_PIX_FMT_NV21:
    case V4L2_PIX_FMT_NV21M:
        plane_count = 2;
        break;
    case V4L2_PIX_FMT_YVU420M:
    case V4L2_PIX_FMT_YUV422P:
    case V4L2_PIX_FMT_YUV420M:
        plane_count = 3;
        break;
    default:
        ALOGE("%s::unmatched v4l_pixel_format color_space(0x%x)\n",
             __func__, v4l_pixel_format);
        plane_count = -1;
        break;
    }

    return plane_count;
}

static unsigned int m_gsc_get_plane_size(
    unsigned int *plane_size,
    unsigned int  width,
    unsigned int  height,
    int           v4l_pixel_format)
{
    switch (v4l_pixel_format) {
    /* 1 plane */
    case V4L2_PIX_FMT_RGB32:
    case V4L2_PIX_FMT_BGR32:
        plane_size[0] = width * height * 4;
        plane_size[1] = 0;
        plane_size[2] = 0;
        break;
    case V4L2_PIX_FMT_RGB24:
        plane_size[0] = width * height * 3;
        plane_size[1] = 0;
        plane_size[2] = 0;
        break;
    case V4L2_PIX_FMT_RGB565:
    case V4L2_PIX_FMT_RGB555X:
    case V4L2_PIX_FMT_RGB444:
    case V4L2_PIX_FMT_YUYV:
    case V4L2_PIX_FMT_UYVY:
        plane_size[0] = width * height * 2;
        plane_size[1] = 0;
        plane_size[2] = 0;
        break;
    /* 2 planes */
    case V4L2_PIX_FMT_NV12M:
    case V4L2_PIX_FMT_NV12:
    case V4L2_PIX_FMT_NV21:
    case V4L2_PIX_FMT_NV21M:
        plane_size[0] = width * height;
        plane_size[1] = width * (height / 2);
        plane_size[2] = 0;
        break;
    case V4L2_PIX_FMT_NV16:
    case V4L2_PIX_FMT_NV61:
        plane_size[0] = width * height * 2;
        plane_size[1] = 0;
        plane_size[2] = 0;
        break;
    case V4L2_PIX_FMT_NV12MT_16X16:
        plane_size[0] = ALIGN(width, 16) * ALIGN(height, 16);
        plane_size[1] = ALIGN(width, 16) * ALIGN(height / 2, 8);
        plane_size[2] = 0;
        break;
    /* 3 planes */
    case V4L2_PIX_FMT_YUV420M:
    case V4L2_PIX_FMT_YVU420M:
    case V4L2_PIX_FMT_YUV422P:
        plane_size[0] = width * height;
        plane_size[1] = (width / 2) * (height / 2);
        plane_size[2] = (width / 2) * (height / 2);
        break;
    case V4L2_PIX_FMT_YVU420:
        plane_size[0] = ALIGN(width, 16) * height + ALIGN(width / 2, 16) * height;
        plane_size[1] = 0;
        plane_size[2] = 0;
        break;
    default:
        ALOGE("%s::unmatched v4l_pixel_format color_space(0x%x)\n",
             __func__, v4l_pixel_format);
        return -1;
        break;
    }

    return 0;
}

static int m_exynos_gsc_multiple_of_n(
    int number, int N)
{
    int result = number;
    switch (N) {
    case 1:
    case 2:
    case 4:
    case 8:
    case 16:
    case 32:
    case 64:
    case 128:
    case 256:
        result = (number - (number & (N-1)));
        break;
    default:
        result = number - (number % N);
        break;
    }
    return result;
}

static bool m_exynos_gsc_check_src_size(
    unsigned int *w,      unsigned int *h,
    unsigned int *crop_x, unsigned int *crop_y,
    unsigned int *crop_w, unsigned int *crop_h,
    int v4l2_colorformat)
{
    if (*w < GSC_MIN_W_SIZE || *h < GSC_MIN_H_SIZE) {
        ALOGE("%s::too small size (w : %d < %d) (h : %d < %d)",
            __func__, GSC_MIN_W_SIZE, *w, GSC_MIN_H_SIZE, *h);
        return false;
    }

    if (*crop_w < GSC_MIN_W_SIZE || *crop_h < GSC_MIN_H_SIZE) {
        ALOGE("%s::too small size (w : %d < %d) (h : %d < %d)",
            __func__, GSC_MIN_W_SIZE,* crop_w, GSC_MIN_H_SIZE, *crop_h);
        return false;
    }

    switch (v4l2_colorformat) {
    // YUV420
    case V4L2_PIX_FMT_YUV420M:
    case V4L2_PIX_FMT_YVU420M:
    case V4L2_PIX_FMT_YVU420:
    case V4L2_PIX_FMT_NV12M:
    case V4L2_PIX_FMT_NV12MT:
    case V4L2_PIX_FMT_NV21:
    case V4L2_PIX_FMT_NV21M:
        *w = (*w + 15) & ~15;
        *h = (*h + 15) & ~15;
        //*w      = m_exynos_gsc_multiple_of_n(*w, 16);
        //*h      = m_exynos_gsc_multiple_of_n(*h, 16);
        *crop_w = m_exynos_gsc_multiple_of_n(*crop_w, 2);
        *crop_h = m_exynos_gsc_multiple_of_n(*crop_h, 2);
        break;
    // YUV422
    case V4L2_PIX_FMT_YUYV:
    case V4L2_PIX_FMT_YUV422P:
    case V4L2_PIX_FMT_UYVY:
    case V4L2_PIX_FMT_NV16:
    case V4L2_PIX_FMT_YVYU:
    case V4L2_PIX_FMT_VYUY:
        *h = (*h + 7) & ~7;
        //*h      = m_exynos_gsc_multiple_of_n(*h, 8);
        *crop_w = m_exynos_gsc_multiple_of_n(*crop_w, 4);
        *crop_h = m_exynos_gsc_multiple_of_n(*crop_h, 2);
        break;
    // RGB
    case V4L2_PIX_FMT_RGB32:
    case V4L2_PIX_FMT_RGB24:
    case V4L2_PIX_FMT_RGB565:
    case V4L2_PIX_FMT_BGR32:
    case V4L2_PIX_FMT_RGB555X:
    case V4L2_PIX_FMT_RGB444:
    default:
        *h = (*h + 7) & ~7;
        //*h      = m_exynos_gsc_multiple_of_n(*h, 8);
        *crop_w = m_exynos_gsc_multiple_of_n(*crop_w, 2);
        *crop_h = m_exynos_gsc_multiple_of_n(*crop_h, 2);
        break;
    }

    return true;
}

static bool m_exynos_gsc_check_dst_size(
    unsigned int *w,      unsigned int *h,
    unsigned int *crop_x, unsigned int *crop_y,
    unsigned int *crop_w, unsigned int *crop_h,
    int v4l2_colorformat,
    int rotation)
{
    unsigned int *new_w;
    unsigned int *new_h;
    unsigned int *new_crop_w;
    unsigned int *new_crop_h;

        new_w = w;
        new_h = h;
        new_crop_w = crop_w;
        new_crop_h = crop_h;

    if (*w < GSC_MIN_W_SIZE || *h < GSC_MIN_H_SIZE) {
        ALOGE("%s::too small size (w : %d < %d) (h : %d < %d)",
            __func__, GSC_MIN_W_SIZE, *w, GSC_MIN_H_SIZE, *h);
        return false;
    }

    if (*crop_w < GSC_MIN_W_SIZE || *crop_h < GSC_MIN_H_SIZE) {
        ALOGE("%s::too small size (w : %d < %d) (h : %d < %d)",
            __func__, GSC_MIN_W_SIZE,* crop_w, GSC_MIN_H_SIZE, *crop_h);
        return false;
    }

    switch (v4l2_colorformat) {
    // YUV420
    case V4L2_PIX_FMT_NV12M:
    case V4L2_PIX_FMT_NV12MT:
    case V4L2_PIX_FMT_NV21:
    case V4L2_PIX_FMT_NV21M:
    case V4L2_PIX_FMT_YUV420M:
    case V4L2_PIX_FMT_YVU420M:
    case V4L2_PIX_FMT_YVU420:
        *new_w = m_exynos_gsc_multiple_of_n(*new_w, 2);
        *new_h = m_exynos_gsc_multiple_of_n(*new_h, 2);
        break;
    // YUV422
    case V4L2_PIX_FMT_YUYV:
    case V4L2_PIX_FMT_YUV422P:
    case V4L2_PIX_FMT_UYVY:
    case V4L2_PIX_FMT_NV16:
    case V4L2_PIX_FMT_YVYU:
    case V4L2_PIX_FMT_VYUY:
        *new_w = m_exynos_gsc_multiple_of_n(*new_w, 2);
        break;
    // RGB
    case V4L2_PIX_FMT_RGB32:
    case V4L2_PIX_FMT_RGB24:
    case V4L2_PIX_FMT_RGB565:
    case V4L2_PIX_FMT_BGR32:
    case V4L2_PIX_FMT_RGB555X:
    case V4L2_PIX_FMT_RGB444:
    default:
        break;
    }

    return true;
}

static int m_exynos_gsc_output_create(
    struct GSC_HANDLE *gsc_handle,
    int dev_num,
    int out_mode)
{
    struct media_device *media0;
    struct media_entity *gsc_sd_entity;
    struct media_entity *gsc_vd_entity;
    struct media_entity *sink_sd_entity;
    struct media_link *links;
    char node[32];
    char devname[32];
    unsigned int cap;
    int         i;
    int         fd = 0;

    Exynos_gsc_In();

    if ((out_mode != GSC_OUT_FIMD) &&
        (out_mode != GSC_OUT_TV))
        return -1;

    gsc_handle->out_mode = out_mode;
    /* GSCX => FIMD_WINX : arbitrary linking is not allowed */
    if ((out_mode == GSC_OUT_FIMD) &&
        (dev_num > 2))
        return -1;

    /* media0 */
    snprintf(node, sizeof(node), "%s%d", PFX_NODE_MEDIADEV, 0);
    media0 = exynos_media_open(node);
    if (media0 == NULL) {
        ALOGE("%s::exynos_media_open failed (node=%s)", __func__, node);
        return false;
    }

    /* Get the sink subdev entity by name and make the node of sink subdev*/
    if (out_mode == GSC_OUT_FIMD)
        snprintf(devname, sizeof(devname), PFX_FIMD_ENTITY, dev_num);
    else
        snprintf(devname, sizeof(devname), PFX_MXR_ENTITY, 0);

    sink_sd_entity = exynos_media_get_entity_by_name(media0, devname, strlen(devname));
    sink_sd_entity->fd = exynos_subdev_open_devname(devname, O_RDWR);
    if ( sink_sd_entity->fd < 0) {
        ALOGE("%s:: failed to open sink subdev node", __func__);
        goto gsc_output_err;
    }

    /* get GSC video dev & sub dev entity by name*/
    snprintf(devname, sizeof(devname), PFX_GSC_VIDEODEV_ENTITY, dev_num);
    gsc_vd_entity= exynos_media_get_entity_by_name(media0, devname, strlen(devname));

    snprintf(devname, sizeof(devname), PFX_GSC_SUBDEV_ENTITY, dev_num);
    gsc_sd_entity= exynos_media_get_entity_by_name(media0, devname, strlen(devname));

    /* gsc sub-dev open */
    snprintf(devname, sizeof(devname), PFX_GSC_SUBDEV_ENTITY, dev_num);
    gsc_sd_entity->fd = exynos_subdev_open_devname(devname, O_RDWR);

    /* setup link : GSC : video device --> sub device */
    for (i = 0; i < (int) gsc_vd_entity->num_links; i++) {
        links = &gsc_vd_entity->links[i];

        if (links == NULL ||
            links->source->entity != gsc_vd_entity ||
            links->sink->entity   != gsc_sd_entity) {
            continue;
        } else if (exynos_media_setup_link(media0,  links->source,  links->sink, MEDIA_LNK_FL_ENABLED) < 0) {
            ALOGE("%s::exynos_media_setup_link [src.entity=%d->sink.entity=%d] failed",
                  __func__, links->source->entity->info.id, links->sink->entity->info.id);
            return -1;
        }
    }

    /* setup link : GSC: sub device --> sink device */
    for (i = 0; i < (int) gsc_sd_entity->num_links; i++) {
        links = &gsc_sd_entity->links[i];

        if (links == NULL || links->source->entity != gsc_sd_entity ||
                             links->sink->entity   != sink_sd_entity) {
            continue;
        } else if (exynos_media_setup_link(media0,  links->source,  links->sink, MEDIA_LNK_FL_ENABLED) < 0) {
            ALOGE("%s::exynos_media_setup_link [src.entity=%d->sink.entity=%d] failed",
                  __func__, links->source->entity->info.id, links->sink->entity->info.id);
            return -1;
        }
    }

    /* gsc video-dev open */
    snprintf(devname, sizeof(devname), PFX_GSC_VIDEODEV_ENTITY, dev_num);
    gsc_vd_entity->fd = exynos_v4l2_open_devname(devname, O_RDWR);
    cap = V4L2_CAP_STREAMING |
          V4L2_CAP_VIDEO_OUTPUT_MPLANE;

    if (exynos_v4l2_querycap(gsc_vd_entity->fd, cap) == false) {
        ALOGE("%s::exynos_v4l2_querycap() fail", __func__);
        goto gsc_output_err;
    }
    gsc_handle->gsc_sd_entity = gsc_sd_entity;
    gsc_handle->gsc_vd_entity = gsc_vd_entity;
    gsc_handle->sink_sd_entity = sink_sd_entity;
    gsc_handle->media0 = media0;

    Exynos_gsc_Out();

    return 0;

gsc_output_err:
    /* to do */
    return -1;

}

static int m_exynos_gsc_m2m_create(
    int dev)
{
    int          fd = 0;
    int          video_node_num;
    unsigned int cap;
    char         node[32];

    Exynos_gsc_In();

    switch(dev) {
    case 0:
        video_node_num = NODE_NUM_GSC_0;
        break;
    case 1:
        video_node_num = NODE_NUM_GSC_1;
        break;
    case 2:
        video_node_num = NODE_NUM_GSC_2;
        break;
    case 3:
        video_node_num = NODE_NUM_GSC_3;
        break;
    default:
        ALOGE("%s::unexpected dev(%d) fail", __func__, dev);
        return -1;
        break;
    }

    snprintf(node, sizeof(node), "%s%d", PFX_NODE_GSC, video_node_num);
    fd = exynos_v4l2_open(node, O_RDWR);
    if (fd < 0) {
        ALOGE("%s::exynos_v4l2_open(%s) fail", __func__, node);
        return -1;
    }

    cap = V4L2_CAP_STREAMING |
          V4L2_CAP_VIDEO_OUTPUT_MPLANE |
          V4L2_CAP_VIDEO_CAPTURE_MPLANE;

    if (exynos_v4l2_querycap(fd, cap) == false) {
        ALOGE("%s::exynos_v4l2_querycap() fail", __func__);
        if (0 < fd)
            close(fd);
        fd = 0;
        return -1;
    }

    Exynos_gsc_Out();

    return fd;
}


static bool m_exynos_gsc_out_destroy(struct GSC_HANDLE *gsc_handle)
{
    struct media_link *links;
    int i;

    Exynos_gsc_In();

    if (gsc_handle == NULL) {
        ALOGE("%s::gsc_handle is NULL", __func__);
        return false;
    }

    if (gsc_handle->src.stream_on == true) {
        if (exynos_gsc_out_stop((void *)gsc_handle) < 0)
            ALOGE("%s::exynos_gsc_out_stop() fail", __func__);

            gsc_handle->src.stream_on = false;
    }

    Exynos_gsc_Out();

    return true;

}

int exynos_gsc_free_and_close(void *handle)
{
    struct GSC_HANDLE *gsc_handle;
    struct v4l2_requestbuffers reqbuf;
    struct v4l2_buffer buf;
    struct v4l2_plane  planes[NUM_OF_GSC_PLANES];
    int i;

    Exynos_gsc_In();

    gsc_handle = (struct GSC_HANDLE *)handle;
    if (handle == NULL) {
        ALOGE("%s::handle == NULL() fail", __func__);
        return -1;
    }

    memset(&reqbuf, 0, sizeof(struct v4l2_requestbuffers));
    reqbuf.type   = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    reqbuf.memory = V4L2_MEMORY_DMABUF;
    reqbuf.count  = 0;

    if (exynos_v4l2_reqbufs(gsc_handle->gsc_vd_entity->fd, &reqbuf) < 0) {
        ALOGE("%s::request buffers failed", __func__);
        return -1;
    }

    close(gsc_handle->gsc_sd_entity->fd);
    close(gsc_handle->gsc_vd_entity->fd);
    gsc_handle->gsc_sd_entity->fd = -1;
    gsc_handle->gsc_vd_entity->fd = -1;

    exynos_gsc_destroy(gsc_handle);
    Exynos_gsc_Out();

    return 0;
}

static bool m_exynos_gsc_destroy(
    struct GSC_HANDLE *gsc_handle)
{
    Exynos_gsc_In();

    /* just in case, we call stop here because we cannot afford to leave
     * secure side protection on if things failed.
     */
    exynos_gsc_m2m_stop(gsc_handle);

    if (0 < gsc_handle->gsc_fd)
        close(gsc_handle->gsc_fd);
    gsc_handle->gsc_fd = 0;

    Exynos_gsc_Out();

    return true;
}

bool m_exynos_gsc_find_and_trylock_and_create(
    struct GSC_HANDLE *gsc_handle)
{
    int          i                 = 0;
    bool         flag_find_new_gsc = false;
    unsigned int total_sleep_time  = 0;

    Exynos_gsc_In();

    do {
        for (i = 0; i < NUM_OF_GSC_HW; i++) {
            // HACK : HWComposer, HDMI uses gscaler with their own code.
            //        So, This obj_mutex cannot defense their open()
            if (i == 0 || i == 3)
                continue;

            if (exynos_mutex_trylock(gsc_handle->obj_mutex[i]) == true) {

                // destroy old one.
                m_exynos_gsc_destroy(gsc_handle);

                // create new one.
                gsc_handle->gsc_id = i;
                gsc_handle->gsc_fd = m_exynos_gsc_m2m_create(i);
                if (gsc_handle->gsc_fd < 0) {
                    gsc_handle->gsc_fd = 0;
                    exynos_mutex_unlock(gsc_handle->obj_mutex[i]);
                    continue;
                }

                if (gsc_handle->cur_obj_mutex)
                    exynos_mutex_unlock(gsc_handle->cur_obj_mutex);

                gsc_handle->cur_obj_mutex = gsc_handle->obj_mutex[i];

                flag_find_new_gsc = true;
                break;
            }
        }

        // waiting for another process doesn't use gscaler.
        // we need to make decision how to do.
        if (flag_find_new_gsc == false) {
            usleep(GSC_WAITING_TIME_FOR_TRYLOCK);
            total_sleep_time += GSC_WAITING_TIME_FOR_TRYLOCK;
            ALOGV("%s::waiting for anthere process doens't use gscaler", __func__);
        }

    } while(   flag_find_new_gsc == false
            && total_sleep_time < MAX_GSC_WAITING_TIME_FOR_TRYLOCK);

    if (flag_find_new_gsc == false)
        ALOGE("%s::we don't have no available gsc.. fail", __func__);

    Exynos_gsc_Out();

    return flag_find_new_gsc;
}

static bool m_exynos_gsc_set_format(
    int              fd,
    struct gsc_info *info)
{
    Exynos_gsc_In();

    struct v4l2_requestbuffers req_buf;
    int                        plane_count;

    plane_count = m_gsc_get_plane_count(info->v4l2_colorformat);
    if (plane_count < 0) {
        ALOGE("%s::not supported v4l2_colorformat", __func__);
        return false;
    }

    if (exynos_v4l2_s_ctrl(fd, V4L2_CID_ROTATE, info->rotation) < 0) {
        ALOGE("%s::exynos_v4l2_s_ctrl(V4L2_CID_ROTATE) fail", __func__);
        return false;
    }

    if (exynos_v4l2_s_ctrl(fd, V4L2_CID_VFLIP, info->flip_horizontal) < 0) {
        ALOGE("%s::exynos_v4l2_s_ctrl(V4L2_CID_VFLIP) fail", __func__);
        return false;
    }

    if (exynos_v4l2_s_ctrl(fd, V4L2_CID_HFLIP, info->flip_vertical) < 0) {
        ALOGE("%s::exynos_v4l2_s_ctrl(V4L2_CID_HFLIP) fail", __func__);
        return false;
    }

    if (exynos_v4l2_s_ctrl(fd, V4L2_CID_CSC_RANGE, info->csc_range) < 0) {
        ALOGE("%s::exynos_v4l2_s_ctrl(V4L2_CID_CSC_RANGE) fail", __func__);
        return false;
    }
    info->format.type = info->buf_type;
    info->format.fmt.pix_mp.width       = info->width;
    info->format.fmt.pix_mp.height      = info->height;
    info->format.fmt.pix_mp.pixelformat = info->v4l2_colorformat;
    info->format.fmt.pix_mp.field       = V4L2_FIELD_ANY;
    info->format.fmt.pix_mp.num_planes  = plane_count;

    if (exynos_v4l2_s_fmt(fd, &info->format) < 0) {
        ALOGE("%s::exynos_v4l2_s_fmt() fail", __func__);
        return false;
    }

    info->crop.type     = info->buf_type;
    info->crop.c.left   = info->crop_left;
    info->crop.c.top    = info->crop_top;
    info->crop.c.width  = info->crop_width;
    info->crop.c.height = info->crop_height;

    if (exynos_v4l2_s_crop(fd, &info->crop) < 0) {
        ALOGE("%s::exynos_v4l2_s_crop() fail", __func__);
        return false;
    }

    if (exynos_v4l2_s_ctrl(fd, V4L2_CID_CACHEABLE, info->cacheable) < 0) {
        ALOGE("%s::exynos_v4l2_s_ctrl() fail", __func__);
        return false;
    }

    req_buf.count  = 1;
    req_buf.type   = info->buf_type;
    req_buf.memory = info->mem_type;
    if (exynos_v4l2_reqbufs(fd, &req_buf) < 0) {
        ALOGE("%s::exynos_v4l2_reqbufs() fail", __func__);
        return false;
    }

    Exynos_gsc_Out();

    return true;
}

static bool m_exynos_gsc_set_addr(
    int              fd,
    struct gsc_info *info)
{
    unsigned int i;
    unsigned int plane_size[NUM_OF_GSC_PLANES];

    m_gsc_get_plane_size(plane_size,
                         info->width,
                         info->height,
                         info->v4l2_colorformat);

    info->buffer.index    = 0;
    info->buffer.flags    = V4L2_BUF_FLAG_USE_SYNC;
    info->buffer.type     = info->buf_type;
    info->buffer.memory   = info->mem_type;
    info->buffer.m.planes = info->planes;
    info->buffer.length   = info->format.fmt.pix_mp.num_planes;
    info->buffer.reserved = info->acquireFenceFd;

    for (i = 0; i < info->format.fmt.pix_mp.num_planes; i++) {
        if (info->buffer.memory == GSC_MEM_DMABUF)
            info->buffer.m.planes[i].m.fd = (int)info->addr[i];
        else
            info->buffer.m.planes[i].m.userptr = (unsigned long)info->addr[i];
        info->buffer.m.planes[i].length    = plane_size[i];
        info->buffer.m.planes[i].bytesused = 0;
    }

    if (exynos_v4l2_qbuf(fd, &info->buffer) < 0) {
        ALOGE("%s::exynos_v4l2_qbuf() fail", __func__);
        return false;
    }
    info->buffer_queued = true;

    info->releaseFenceFd = info->buffer.reserved;

    return true;
}

void *exynos_gsc_create(
    void)
{
    int i     = 0;
    int op_id = 0;
    char mutex_name[32];

    Exynos_gsc_In();

    struct GSC_HANDLE *gsc_handle = (struct GSC_HANDLE *)malloc(sizeof(struct GSC_HANDLE));
    if (gsc_handle == NULL) {
        ALOGE("%s::malloc(struct GSC_HANDLE) fail", __func__);
        goto err;
    }

    gsc_handle->gsc_fd = 0;
    memset(&gsc_handle->src, 0, sizeof(struct gsc_info));
    memset(&gsc_handle->dst, 0, sizeof(struct gsc_info));

    gsc_handle->src.buf_type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    gsc_handle->dst.buf_type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

    gsc_handle->op_mutex = NULL;
    for (i = 0; i < NUM_OF_GSC_HW; i++)
        gsc_handle->obj_mutex[i] = NULL;

    gsc_handle->cur_obj_mutex = NULL;
    gsc_handle->flag_local_path = false;
    gsc_handle->flag_exclusive_open = false;

    srand(time(NULL));
    op_id = rand() % 1000000; // just make random id
    snprintf(mutex_name, sizeof(mutex_name), "%sOp%d", LOG_TAG, op_id);
    gsc_handle->op_mutex = exynos_mutex_create(EXYNOS_MUTEX_TYPE_PRIVATE, mutex_name);
    if (gsc_handle->op_mutex == NULL) {
        ALOGE("%s::exynos_mutex_create(%s) fail", __func__, mutex_name);
        goto err;
    }

    exynos_mutex_lock(gsc_handle->op_mutex);

    // check if it is available
    for (i = 0; i < NUM_OF_GSC_HW; i++) {
        snprintf(mutex_name, sizeof(mutex_name), "%sObject%d", LOG_TAG, i);

        gsc_handle->obj_mutex[i] = exynos_mutex_create(EXYNOS_MUTEX_TYPE_SHARED, mutex_name);
        if (gsc_handle->obj_mutex[i] == NULL) {
            ALOGE("%s::exynos_mutex_create(%s) fail", __func__, mutex_name);
            goto err;
        }
    }

    if (m_exynos_gsc_find_and_trylock_and_create(gsc_handle) == false) {
        ALOGE("%s::m_exynos_gsc_find_and_trylock_and_create() fail", __func__);
        goto err;
    }

    exynos_mutex_unlock(gsc_handle->cur_obj_mutex);
    exynos_mutex_unlock(gsc_handle->op_mutex);

    return (void *)gsc_handle;

err:
    if (gsc_handle) {
        m_exynos_gsc_destroy(gsc_handle);

        if (gsc_handle->cur_obj_mutex)
            exynos_mutex_unlock(gsc_handle->cur_obj_mutex);

        for (i = 0; i < NUM_OF_GSC_HW; i++) {
            if ((gsc_handle->obj_mutex[i] != NULL) &&
                (exynos_mutex_get_created_status(gsc_handle->obj_mutex[i]) == true)) {
                if (exynos_mutex_destroy(gsc_handle->obj_mutex[i]) == false)
                    ALOGE("%s::exynos_mutex_destroy() fail", __func__);
            }
        }

        if (gsc_handle->op_mutex)
            exynos_mutex_unlock(gsc_handle->op_mutex);

        if (exynos_mutex_destroy(gsc_handle->op_mutex) == false)
            ALOGE("%s::exynos_mutex_destroy(op_mutex) fail", __func__);

        free(gsc_handle);
    }

    Exynos_gsc_Out();

    return NULL;
}

void *exynos_gsc_reserve(int dev_num)
{
    char mutex_name[32];
    unsigned int total_sleep_time  = 0;
    bool    gsc_flag = false;

    if ((dev_num < 0) || (dev_num >= NUM_OF_GSC_HW)) {
        ALOGE("%s::fail:: dev_num is not valid(%d) ", __func__, dev_num);
        return NULL;
    }

    struct GSC_HANDLE *gsc_handle = (struct GSC_HANDLE *)malloc(sizeof(struct GSC_HANDLE));
    if (gsc_handle == NULL) {
        ALOGE("%s::malloc(struct GSC_HANDLE) fail", __func__);
        goto err;
    }

    gsc_handle->gsc_fd = -1;
    gsc_handle->op_mutex = NULL;
    gsc_handle->cur_obj_mutex = NULL;

    snprintf(mutex_name, sizeof(mutex_name), "%sObject%d", LOG_TAG, dev_num);
    gsc_handle->cur_obj_mutex = exynos_mutex_create(EXYNOS_MUTEX_TYPE_SHARED, mutex_name);
    if (gsc_handle->cur_obj_mutex == NULL) {
        ALOGE("%s::exynos_mutex_create(%s) fail", __func__, mutex_name);
        goto err;
    }

    do {
        if (exynos_mutex_trylock(gsc_handle->cur_obj_mutex) == true) {
            gsc_flag = true;
            break;
        }
        usleep(GSC_WAITING_TIME_FOR_TRYLOCK);
        total_sleep_time += GSC_WAITING_TIME_FOR_TRYLOCK;
        ALOGV("%s::waiting for another process to release the requested gscaler", __func__);
    } while(total_sleep_time < MAX_GSC_WAITING_TIME_FOR_TRYLOCK);

    if (gsc_flag == true)
         return (void *)gsc_handle;

err:
    if (gsc_handle) {
        free(gsc_handle);
    }

    return NULL;
}

void exynos_gsc_release(void *handle)
{
    struct GSC_HANDLE *gsc_handle = (struct GSC_HANDLE *)handle;

    if (handle == NULL) {
        ALOGE("%s::handle == NULL() fail", __func__);
        return;
    }

    exynos_mutex_unlock(gsc_handle->cur_obj_mutex);
    exynos_mutex_destroy(gsc_handle->cur_obj_mutex);
    free(gsc_handle);
    return;
}

void *exynos_gsc_create_exclusive(
    int dev_num,
    int mode,
    int out_mode,
    int allow_drm)
{
    int i     = 0;
    int op_id = 0;
    char mutex_name[32];
    unsigned int total_sleep_time  = 0;
    bool    gsc_flag = false;
    int ret = 0;

    Exynos_gsc_In();

    if ((dev_num < 0) || (dev_num >= NUM_OF_GSC_HW)) {
        ALOGE("%s::fail:: dev_num is not valid(%d) ", __func__, dev_num);
        return NULL;
    }

    if ((mode < 0) || (mode >= NUM_OF_GSC_HW)) {
        ALOGE("%s::fail:: mode is not valid(%d) ", __func__, mode);
        return NULL;
    }

    /* currently only gscalers 0 and 3 are DRM capable */
    if (allow_drm && (dev_num != 0 && dev_num != 3)) {
        ALOGE("%s::fail:: gscaler %d does not support drm\n", __func__,
              dev_num);
        return NULL;
    }

    struct GSC_HANDLE *gsc_handle = (struct GSC_HANDLE *)malloc(sizeof(struct GSC_HANDLE));
    if (gsc_handle == NULL) {
        ALOGE("%s::malloc(struct GSC_HANDLE) fail", __func__);
        goto err;
    }
    memset(gsc_handle, 0, sizeof(struct GSC_HANDLE));
    gsc_handle->gsc_fd = -1;
    gsc_handle->gsc_mode = mode;
    gsc_handle->gsc_id = dev_num;
    gsc_handle->allow_drm = allow_drm;

    gsc_handle->src.buf_type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    gsc_handle->dst.buf_type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

    gsc_handle->op_mutex = NULL;
    for (i = 0; i < NUM_OF_GSC_HW; i++)
        gsc_handle->obj_mutex[i] = NULL;

    gsc_handle->cur_obj_mutex = NULL;
    gsc_handle->flag_local_path = false;
    gsc_handle->flag_exclusive_open = true;

    srand(time(NULL));
    op_id = rand() % 1000000; // just make random id
    snprintf(mutex_name, sizeof(mutex_name), "%sOp%d", LOG_TAG, op_id);
    gsc_handle->op_mutex = exynos_mutex_create(EXYNOS_MUTEX_TYPE_PRIVATE, mutex_name);
    if (gsc_handle->op_mutex == NULL) {
        ALOGE("%s::exynos_mutex_create(%s) fail", __func__, mutex_name);
        goto err;
    }

    exynos_mutex_lock(gsc_handle->op_mutex);

    snprintf(mutex_name, sizeof(mutex_name), "%sObject%d", LOG_TAG, dev_num);
    gsc_handle->cur_obj_mutex = exynos_mutex_create(EXYNOS_MUTEX_TYPE_SHARED, mutex_name);
    if (gsc_handle->cur_obj_mutex == NULL) {
        ALOGE("%s::exynos_mutex_create(%s) fail", __func__, mutex_name);
        goto err;
    }

    do {
        if (exynos_mutex_trylock(gsc_handle->cur_obj_mutex) == true) {
            if (mode == GSC_M2M_MODE) {
                gsc_handle->gsc_fd = m_exynos_gsc_m2m_create(dev_num);
                if (gsc_handle->gsc_fd < 0) {
                    ALOGE("%s::m_exynos_gsc_m2m_create(%i) fail", __func__, dev_num);
                    goto err;
                }
            } else if (mode == GSC_OUTPUT_MODE) {
                ret = m_exynos_gsc_output_create(gsc_handle, dev_num, out_mode);
                if (ret < 0) {
                    ALOGE("%s::m_exynos_gsc_output_create(%i) fail", __func__, dev_num);
                    goto err;
                }
            }
            /*else
                gsc_handle->gsc_fd = m_exynos_gsc_capture_create(dev_num);*/

            gsc_flag = true;
            break;
        }
        usleep(GSC_WAITING_TIME_FOR_TRYLOCK);
        total_sleep_time += GSC_WAITING_TIME_FOR_TRYLOCK;
        ALOGV("%s::waiting for anthere process doens't use gscaler", __func__);
    } while(total_sleep_time < MAX_GSC_WAITING_TIME_FOR_TRYLOCK);

    exynos_mutex_unlock(gsc_handle->op_mutex);
    if (gsc_flag == true) {
        Exynos_gsc_Out();
        return (void *)gsc_handle;
        }

err:
    if (gsc_handle) {
        m_exynos_gsc_destroy(gsc_handle);

        if (gsc_handle->cur_obj_mutex)
            exynos_mutex_unlock(gsc_handle->cur_obj_mutex);

        for (i = 0; i < NUM_OF_GSC_HW; i++) {
            if ((gsc_handle->obj_mutex[i] != NULL) &&
                (exynos_mutex_get_created_status(gsc_handle->obj_mutex[i]) == true)) {
                if (exynos_mutex_destroy(gsc_handle->obj_mutex[i]) == false)
                    ALOGE("%s::exynos_mutex_destroy() fail", __func__);
            }
        }

        if (gsc_handle->op_mutex)
            exynos_mutex_unlock(gsc_handle->op_mutex);

        if (exynos_mutex_destroy(gsc_handle->op_mutex) == false)
            ALOGE("%s::exynos_mutex_destroy(op_mutex) fail", __func__);

        free(gsc_handle);
    }

    Exynos_gsc_Out();

    return NULL;
}

void exynos_gsc_destroy(
    void *handle)
{
    int i = 0;
    struct GSC_HANDLE *gsc_handle = (struct GSC_HANDLE *)handle;

    Exynos_gsc_In();

    if (handle == NULL) {
        ALOGE("%s::handle == NULL() fail", __func__);
        return;
    }

    exynos_mutex_lock(gsc_handle->op_mutex);

    if (gsc_handle->flag_exclusive_open == false)
        exynos_mutex_lock(gsc_handle->cur_obj_mutex);

    if (gsc_handle->gsc_mode == GSC_OUTPUT_MODE)
        m_exynos_gsc_out_destroy(gsc_handle);
    else
        m_exynos_gsc_destroy(gsc_handle);

    exynos_mutex_unlock(gsc_handle->cur_obj_mutex);

    for (i = 0; i < NUM_OF_GSC_HW; i++) {
        if ((gsc_handle->obj_mutex[i] != NULL) &&
            (exynos_mutex_get_created_status(gsc_handle->obj_mutex[i]) == true)) {
            if (exynos_mutex_destroy(gsc_handle->obj_mutex[i]) == false)
                ALOGE("%s::exynos_mutex_destroy(obj_mutex) fail", __func__);
        }
    }

    exynos_mutex_unlock(gsc_handle->op_mutex);

    if (exynos_mutex_destroy(gsc_handle->op_mutex) == false)
        ALOGE("%s::exynos_mutex_destroy(op_mutex) fail", __func__);

    if (gsc_handle)
        free(gsc_handle);

    Exynos_gsc_Out();

}

int exynos_gsc_set_src_format(
    void        *handle,
    unsigned int width,
    unsigned int height,
    unsigned int crop_left,
    unsigned int crop_top,
    unsigned int crop_width,
    unsigned int crop_height,
    unsigned int v4l2_colorformat,
    unsigned int cacheable,
    unsigned int mode_drm)
{
    Exynos_gsc_In();

    struct GSC_HANDLE *gsc_handle;
    gsc_handle = (struct GSC_HANDLE *)handle;

    if (handle == NULL) {
        ALOGE("%s::handle == NULL() fail", __func__);
        return -1;
    }

    exynos_mutex_lock(gsc_handle->op_mutex);

    gsc_handle->src.width            = width;
    gsc_handle->src.height           = height;
    gsc_handle->src.crop_left        = crop_left;
    gsc_handle->src.crop_top         = crop_top;
    gsc_handle->src.crop_width       = crop_width;
    gsc_handle->src.crop_height      = crop_height;
    gsc_handle->src.v4l2_colorformat = v4l2_colorformat;
    gsc_handle->src.cacheable        = cacheable;
    gsc_handle->src.mode_drm         = mode_drm;
    gsc_handle->src.dirty            = true;


    exynos_mutex_unlock(gsc_handle->op_mutex);

    Exynos_gsc_Out();

    return 0;
}

int exynos_gsc_set_dst_format(
    void        *handle,
    unsigned int width,
    unsigned int height,
    unsigned int crop_left,
    unsigned int crop_top,
    unsigned int crop_width,
    unsigned int crop_height,
    unsigned int v4l2_colorformat,
    unsigned int cacheable,
    unsigned int mode_drm,
    unsigned int narrowRgb)
{
    Exynos_gsc_In();

    struct GSC_HANDLE *gsc_handle;
    gsc_handle = (struct GSC_HANDLE *)handle;

    if (handle == NULL) {
        ALOGE("%s::handle == NULL() fail", __func__);
        return -1;
    }

    exynos_mutex_lock(gsc_handle->op_mutex);

    gsc_handle->dst.width            = width;
    gsc_handle->dst.height           = height;
    gsc_handle->dst.crop_left        = crop_left;
    gsc_handle->dst.crop_top         = crop_top;
    gsc_handle->dst.crop_width       = crop_width;
    gsc_handle->dst.crop_height      = crop_height;
    gsc_handle->dst.v4l2_colorformat = v4l2_colorformat;
    gsc_handle->dst.cacheable        = cacheable;
    gsc_handle->dst.mode_drm         = mode_drm;
    gsc_handle->dst.dirty            = true;
    gsc_handle->dst.csc_range        = !narrowRgb;

    exynos_mutex_unlock(gsc_handle->op_mutex);

    Exynos_gsc_Out();
    return 0;
}

int exynos_gsc_set_rotation(
    void *handle,
    int   rotation,
    int   flip_horizontal,
    int   flip_vertical)
{
    int ret = -1;
    struct GSC_HANDLE *gsc_handle;
    gsc_handle = (struct GSC_HANDLE *)handle;

    if (handle == NULL) {
        ALOGE("%s::handle == NULL() fail", __func__);
        return ret;
    }

    exynos_mutex_lock(gsc_handle->op_mutex);

    int new_rotation = rotation % 360;

    if (new_rotation % 90 != 0) {
        ALOGE("%s::rotation(%d) cannot be acceptable fail", __func__, rotation);
        goto done;
    }

    if(new_rotation < 0)
        new_rotation = -new_rotation;

    gsc_handle->dst.rotation        = new_rotation;
    gsc_handle->dst.flip_horizontal = flip_horizontal;
    gsc_handle->dst.flip_vertical   = flip_vertical;

    ret = 0;
done:
    exynos_mutex_unlock(gsc_handle->op_mutex);

    return ret;
}

int exynos_gsc_set_src_addr(
    void *handle,
    void *addr[3],
    int mem_type,
    int acquireFenceFd)
{
    struct GSC_HANDLE *gsc_handle;
    gsc_handle = (struct GSC_HANDLE *)handle;

    Exynos_gsc_In();

    if (handle == NULL) {
        ALOGE("%s::handle == NULL() fail", __func__);
        return -1;
    }

    exynos_mutex_lock(gsc_handle->op_mutex);

    gsc_handle->src.addr[0] = addr[0];
    gsc_handle->src.addr[1] = addr[1];
    gsc_handle->src.addr[2] = addr[2];
    gsc_handle->src.acquireFenceFd = acquireFenceFd;
    gsc_handle->src.mem_type = mem_type;

    exynos_mutex_unlock(gsc_handle->op_mutex);

    Exynos_gsc_Out();

    return 0;
}

int exynos_gsc_set_dst_addr(
    void *handle,
    void *addr[3],
    int mem_type,
    int acquireFenceFd)
{
    struct GSC_HANDLE *gsc_handle;
    gsc_handle = (struct GSC_HANDLE *)handle;
    int ret = 0;

    Exynos_gsc_In();

    if (handle == NULL) {
        ALOGE("%s::handle == NULL() fail", __func__);
        return -1;
    }

    exynos_mutex_lock(gsc_handle->op_mutex);

    gsc_handle->dst.addr[0] = addr[0];
    gsc_handle->dst.addr[1] = addr[1];
    gsc_handle->dst.addr[2] = addr[2];
    gsc_handle->dst.acquireFenceFd = acquireFenceFd;
    gsc_handle->dst.mem_type = mem_type;

    exynos_mutex_unlock(gsc_handle->op_mutex);

    Exynos_gsc_Out();

    return ret;
}

static void rotateValueHAL2GSC(unsigned int transform,
    unsigned int *rotate,
    unsigned int *hflip,
    unsigned int *vflip)
{
    int rotate_flag = transform & 0x7;
    *rotate = 0;
    *hflip = 0;
    *vflip = 0;

    switch (rotate_flag) {
    case HAL_TRANSFORM_ROT_90:
        *rotate = 90;
        break;
    case HAL_TRANSFORM_ROT_180:
        *rotate = 180;
        break;
    case HAL_TRANSFORM_ROT_270:
        *rotate = 270;
        break;
    case HAL_TRANSFORM_FLIP_H | HAL_TRANSFORM_ROT_90:
        *rotate = 90;
        *vflip = 1; /* set vflip to compensate the rot & flip order. */
        break;
    case HAL_TRANSFORM_FLIP_V | HAL_TRANSFORM_ROT_90:
        *rotate = 90;
        *hflip = 1; /* set hflip to compensate the rot & flip order. */
        break;
    case HAL_TRANSFORM_FLIP_H:
        *hflip = 1;
         break;
    case HAL_TRANSFORM_FLIP_V:
        *vflip = 1;
         break;
    default:
        break;
    }
}

static bool get_plane_size(int V4L2_PIX,
    unsigned int * size,
    unsigned int frame_size,
    int src_planes)
{
    unsigned int frame_ratio = 1;
    int src_bpp    = get_yuv_bpp(V4L2_PIX);

    src_planes = (src_planes == -1) ? 1 : src_planes;
    frame_ratio = 8 * (src_planes -1) / (src_bpp - 8);

    switch (src_planes) {
    case 1:
        switch (V4L2_PIX) {
        case V4L2_PIX_FMT_BGR32:
        case V4L2_PIX_FMT_RGB32:
            size[0] = frame_size << 2;
            break;
        case V4L2_PIX_FMT_RGB565X:
        case V4L2_PIX_FMT_NV16:
        case V4L2_PIX_FMT_NV61:
        case V4L2_PIX_FMT_YUYV:
        case V4L2_PIX_FMT_UYVY:
        case V4L2_PIX_FMT_VYUY:
        case V4L2_PIX_FMT_YVYU:
            size[0] = frame_size << 1;
            break;
        case V4L2_PIX_FMT_YUV420:
        case V4L2_PIX_FMT_NV12:
        case V4L2_PIX_FMT_NV21:
        case V4L2_PIX_FMT_NV21M:
            size[0] = (frame_size * 3) >> 1;
            break;
        default:
            ALOGE("%s::invalid color type", __func__);
            return false;
            break;
        }
        size[1] = 0;
        size[2] = 0;
        break;
    case 2:
        size[0] = frame_size;
        size[1] = frame_size / frame_ratio;
        size[2] = 0;
        break;
    case 3:
        size[0] = frame_size;
        size[1] = frame_size / frame_ratio;
        size[2] = frame_size / frame_ratio;
        break;
    default:
        ALOGE("%s::invalid color foarmt", __func__);
        return false;
        break;
    }

    return true;
}

int exynos_gsc_m2m_config(void *handle,
    exynos_gsc_img *src_img,
    exynos_gsc_img *dst_img)
{
    struct GSC_HANDLE *gsc_handle;
    int32_t      src_color_space;
    int32_t      dst_color_space;
    int ret;
    unsigned int rotate;
    unsigned int hflip;
    unsigned int vflip;

    Exynos_gsc_In();

    gsc_handle = (struct GSC_HANDLE *)handle;
    if (gsc_handle == NULL) {
        ALOGE("%s::gsc_handle == NULL() fail", __func__);
        return -1;
    }

    if ((src_img->drmMode && !gsc_handle->allow_drm) ||
        (src_img->drmMode != dst_img->drmMode)) {
        ALOGE("%s::invalid drm state request for gsc%d (s=%d d=%d)",
              __func__, gsc_handle->gsc_id,
              src_img->drmMode, dst_img->drmMode);
        return -1;
    }

    src_color_space = HAL_PIXEL_FORMAT_2_V4L2_PIX(src_img->format);
    dst_color_space = HAL_PIXEL_FORMAT_2_V4L2_PIX(dst_img->format);
    rotateValueHAL2GSC(dst_img->rot, &rotate, &hflip, &vflip);
    exynos_gsc_set_rotation(gsc_handle, rotate, hflip, vflip);

    ret = exynos_gsc_set_src_format(gsc_handle,  src_img->fw, src_img->fh,
                                  src_img->x, src_img->y, src_img->w, src_img->h,
                                  src_color_space, src_img->cacheable, src_img->drmMode);
    if (ret < 0) {
        ALOGE("%s: fail: exynos_gsc_set_src_format [fw %d fh %d x %d y %d w %d h %d f %x rot %d]",
            __func__, src_img->fw, src_img->fh, src_img->x, src_img->y, src_img->w, src_img->h,
            src_color_space, src_img->rot);
        return -1;
    }

    ret = exynos_gsc_set_dst_format(gsc_handle, dst_img->fw, dst_img->fh,
                                  dst_img->x, dst_img->y, dst_img->w, dst_img->h,
                                  dst_color_space, dst_img->cacheable, dst_img->drmMode,
                                  dst_img->narrowRgb);
    if (ret < 0) {
        ALOGE("%s: fail: exynos_gsc_set_dst_format [fw %d fh %d x %d y %d w %d h %d f %x rot %d]",
            __func__, dst_img->fw, dst_img->fh, dst_img->x, dst_img->y, dst_img->w, dst_img->h,
            src_color_space, dst_img->rot);
        return -1;
    }

    Exynos_gsc_Out();

    return 0;
}

int exynos_gsc_out_config(void *handle,
    exynos_gsc_img *src_img,
    exynos_gsc_img *dst_img)
{
    struct GSC_HANDLE *gsc_handle;
    struct v4l2_format  fmt;
    struct v4l2_crop    crop;
    struct v4l2_requestbuffers reqbuf;
    struct v4l2_subdev_format sd_fmt;
    struct v4l2_subdev_crop   sd_crop;
    int i;
    unsigned int rotate;
    unsigned int hflip;
    unsigned int vflip;
    unsigned int plane_size[NUM_OF_GSC_PLANES];
    bool rgb;
    int csc_range = !dst_img->narrowRgb;

    struct v4l2_rect dst_rect;
    int32_t      src_color_space;
    int32_t      dst_color_space;
    int32_t      src_planes;

    gsc_handle = (struct GSC_HANDLE *)handle;
     if (gsc_handle == NULL) {
        ALOGE("%s::gsc_handle == NULL() fail", __func__);
        return -1;
    }

    Exynos_gsc_In();

     if (gsc_handle->src.stream_on != false) {
        ALOGE("Error: Src is already streamed on !!!!");
        return -1;
     }

    memcpy(&gsc_handle->src_img, src_img, sizeof(exynos_gsc_img));
    memcpy(&gsc_handle->dst_img, dst_img, sizeof(exynos_gsc_img));
    src_color_space = HAL_PIXEL_FORMAT_2_V4L2_PIX(src_img->format);
    dst_color_space = HAL_PIXEL_FORMAT_2_V4L2_PIX(dst_img->format);
    src_planes = get_yuv_planes(src_color_space);
    src_planes = (src_planes == -1) ? 1 : src_planes;
    rgb = get_yuv_planes(dst_color_space) == -1;
    rotateValueHAL2GSC(dst_img->rot, &rotate, &hflip, &vflip);

    if (m_exynos_gsc_check_src_size(&gsc_handle->src_img.fw, &gsc_handle->src_img.fh,
                                        &gsc_handle->src_img.x, &gsc_handle->src_img.y,
                                        &gsc_handle->src_img.w, &gsc_handle->src_img.h,
                                        src_color_space) == false) {
            ALOGE("%s::m_exynos_gsc_check_src_size() fail", __func__);
            return -1;
    }

    /*set: src v4l2_buffer*/
    gsc_handle->src.src_buf_idx = 0;
    gsc_handle->src.qbuf_cnt = 0;
    /* set format: src pad of GSC sub-dev*/
    sd_fmt.pad   = GSCALER_SUBDEV_PAD_SOURCE;
    sd_fmt.which = V4L2_SUBDEV_FORMAT_ACTIVE;
    if (gsc_handle->out_mode == GSC_OUT_FIMD) {
        sd_fmt.format.width  = gsc_handle->dst_img.fw;
        sd_fmt.format.height = gsc_handle->dst_img.fh;
    } else {
        sd_fmt.format.width  = gsc_handle->dst_img.w;
        sd_fmt.format.height = gsc_handle->dst_img.h;
    }
    sd_fmt.format.code   = rgb ? V4L2_MBUS_FMT_XRGB8888_4X8_LE :
                                    V4L2_MBUS_FMT_YUV8_1X24;
    if (exynos_subdev_s_fmt(gsc_handle->gsc_sd_entity->fd, &sd_fmt) < 0) {
            ALOGE("%s::GSC subdev set format failed", __func__);
            return -1;
    }

    /* set crop: src crop of GSC sub-dev*/
    sd_crop.pad   = GSCALER_SUBDEV_PAD_SOURCE;
    sd_crop.which = V4L2_SUBDEV_FORMAT_ACTIVE;
    if (gsc_handle->out_mode == GSC_OUT_FIMD) {
        sd_crop.rect.left   = gsc_handle->dst_img.x;
        sd_crop.rect.top    = gsc_handle->dst_img.y;
        sd_crop.rect.width  = gsc_handle->dst_img.w;
        sd_crop.rect.height = gsc_handle->dst_img.h;
    } else {
        sd_crop.rect.left   = 0;
        sd_crop.rect.top    = 0;
        sd_crop.rect.width  = gsc_handle->dst_img.w;
        sd_crop.rect.height = gsc_handle->dst_img.h;
    }
    if (exynos_subdev_s_crop(gsc_handle->gsc_sd_entity->fd, &sd_crop) < 0) {
            ALOGE("%s::GSC subdev set crop failed", __func__);
            return -1;
    }

    /* sink pad is connected to GSC out */
    /*  set format: sink sub-dev */
    if (gsc_handle->out_mode == GSC_OUT_FIMD) {
        sd_fmt.pad   = FIMD_SUBDEV_PAD_SINK;
        sd_fmt.format.width  = gsc_handle->dst_img.w;
        sd_fmt.format.height = gsc_handle->dst_img.h;
    } else {
        sd_fmt.pad   = MIXER_V_SUBDEV_PAD_SINK;
        sd_fmt.format.width  = gsc_handle->dst_img.w + gsc_handle->dst_img.x*2;
        sd_fmt.format.height = gsc_handle->dst_img.h + gsc_handle->dst_img.y*2;
    }

    sd_fmt.which = V4L2_SUBDEV_FORMAT_ACTIVE;
    sd_fmt.format.code   = rgb ? V4L2_MBUS_FMT_XRGB8888_4X8_LE :
                                    V4L2_MBUS_FMT_YUV8_1X24;
    if (exynos_subdev_s_fmt(gsc_handle->sink_sd_entity->fd, &sd_fmt) < 0) {
        ALOGE("%s::sink:set format failed (PAD=%d)", __func__, sd_fmt.pad);
        return -1;
    }

    /*  set crop: sink sub-dev */
    if (gsc_handle->out_mode == GSC_OUT_FIMD)
        sd_crop.pad   = FIMD_SUBDEV_PAD_SINK;
    else
        sd_crop.pad   = MIXER_V_SUBDEV_PAD_SINK;

    sd_crop.which = V4L2_SUBDEV_FORMAT_ACTIVE;
    if (gsc_handle->out_mode == GSC_OUT_FIMD) {
        sd_crop.rect.left   = gsc_handle->dst_img.x;
        sd_crop.rect.top    = gsc_handle->dst_img.y;
        sd_crop.rect.width  = gsc_handle->dst_img.w;
        sd_crop.rect.height = gsc_handle->dst_img.h;
    } else {
        sd_crop.rect.left   = 0;
        sd_crop.rect.top    = 0;
        sd_crop.rect.width  = gsc_handle->dst_img.w;
        sd_crop.rect.height = gsc_handle->dst_img.h;
    }
    if (exynos_subdev_s_crop(gsc_handle->sink_sd_entity->fd, &sd_crop) < 0) {
            ALOGE("%s::sink: subdev set crop failed(PAD=%d)", __func__, sd_crop.pad);
            return -1;
    }

    if (gsc_handle->out_mode != GSC_OUT_FIMD) {
        sd_fmt.pad   = MIXER_V_SUBDEV_PAD_SOURCE;
        sd_fmt.which = V4L2_SUBDEV_FORMAT_ACTIVE;
        sd_fmt.format.width  = gsc_handle->dst_img.w + gsc_handle->dst_img.x*2;
        sd_fmt.format.height = gsc_handle->dst_img.h + gsc_handle->dst_img.y*2;
        sd_fmt.format.code   = V4L2_MBUS_FMT_XRGB8888_4X8_LE;
        if (exynos_subdev_s_fmt(gsc_handle->sink_sd_entity->fd, &sd_fmt) < 0) {
            ALOGE("%s::sink:set format failed (PAD=%d)", __func__, sd_fmt.pad);
            return -1;
        }

        sd_fmt.pad   = MIXER_V_SUBDEV_PAD_SOURCE;
        sd_crop.which = V4L2_SUBDEV_FORMAT_ACTIVE;
        sd_crop.rect.left   = gsc_handle->dst_img.x;
        sd_crop.rect.top    = gsc_handle->dst_img.y;
        sd_crop.rect.width  = gsc_handle->dst_img.w;
        sd_crop.rect.height = gsc_handle->dst_img.h;
        if (exynos_subdev_s_crop(gsc_handle->sink_sd_entity->fd, &sd_crop) < 0) {
            ALOGE("%s::sink: subdev set crop failed(PAD=%d)", __func__, sd_crop.pad);
            return -1;
        }
    }

    /*set GSC ctrls */
    if (exynos_v4l2_s_ctrl(gsc_handle->gsc_vd_entity->fd, V4L2_CID_ROTATE, rotate) < 0) {
        ALOGE("%s:: exynos_v4l2_s_ctrl (V4L2_CID_ROTATE: %d) failed", __func__,  rotate);
        return -1;
    }

    if (exynos_v4l2_s_ctrl(gsc_handle->gsc_vd_entity->fd, V4L2_CID_HFLIP, vflip) < 0) {
        ALOGE("%s:: exynos_v4l2_s_ctrl (V4L2_CID_HFLIP: %d) failed", __func__,  vflip);
        return -1;
    }

    if (exynos_v4l2_s_ctrl(gsc_handle->gsc_vd_entity->fd, V4L2_CID_VFLIP, hflip) < 0) {
        ALOGE("%s:: exynos_v4l2_s_ctrl (V4L2_CID_VFLIP: %d) failed", __func__,  hflip);
        return -1;
    }

     if (exynos_v4l2_s_ctrl(gsc_handle->gsc_vd_entity->fd, V4L2_CID_CACHEABLE, 1) < 0) {
        ALOGE("%s:: exynos_v4l2_s_ctrl (V4L2_CID_CACHEABLE: 1) failed", __func__);
        return -1;
    }

    if (exynos_v4l2_s_ctrl(gsc_handle->gsc_vd_entity->fd,
        V4L2_CID_CONTENT_PROTECTION, gsc_handle->src_img.drmMode) < 0) {
        ALOGE("%s::exynos_v4l2_s_ctrl(V4L2_CID_CONTENT_PROTECTION) fail", __func__);
        return -1;
    }

    if (exynos_v4l2_s_ctrl(gsc_handle->gsc_vd_entity->fd, V4L2_CID_CSC_RANGE,
            csc_range)) {
        ALOGE("%s::exynos_v4l2_s_ctrl(V4L2_CID_CSC_RANGE: %d) fail", __func__,
                csc_range);
        return -1;
    }

      /* set src format  :GSC video dev*/
    fmt.type  = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    fmt.fmt.pix_mp.width            = gsc_handle->src_img.fw;
    fmt.fmt.pix_mp.height           = gsc_handle->src_img.fh;
    fmt.fmt.pix_mp.pixelformat    = src_color_space;
    fmt.fmt.pix_mp.field              = V4L2_FIELD_NONE;
    fmt.fmt.pix_mp.num_planes   = src_planes;

    if (exynos_v4l2_s_fmt(gsc_handle->gsc_vd_entity->fd, &fmt) < 0) {
            ALOGE("%s::videodev set format failed", __func__);
            return -1;
    }

    /* set src crop info :GSC video dev*/
    crop.type     = fmt.type;
    crop.c.left    = gsc_handle->src_img.x;
    crop.c.top     = gsc_handle->src_img.y;
    crop.c.width  = gsc_handle->src_img.w;
    crop.c.height = gsc_handle->src_img.h;

    if (exynos_v4l2_s_crop(gsc_handle->gsc_vd_entity->fd, &crop) < 0) {
        ALOGE("%s::videodev set crop failed", __func__);
        return -1;
    }

    reqbuf.type   = fmt.type;
    reqbuf.memory = V4L2_MEMORY_DMABUF;
    reqbuf.count  = MAX_BUFFERS_GSCALER_OUT;

    if (exynos_v4l2_reqbufs(gsc_handle->gsc_vd_entity->fd, &reqbuf) < 0) {
        ALOGE("%s::request buffers failed", __func__);
        return -1;
    }

    Exynos_gsc_Out();

    return 0;
}

static int exynos_gsc_out_run(void *handle,
    exynos_gsc_img *src_img)
{
    struct GSC_HANDLE *gsc_handle;
    struct v4l2_plane  planes[NUM_OF_GSC_PLANES];
    struct v4l2_buffer buf;
    int32_t      src_color_space;
    int32_t      src_planes;
    int             i;
    unsigned int plane_size[NUM_OF_GSC_PLANES];

    gsc_handle = (struct GSC_HANDLE *)handle;
    if (handle == NULL) {
        ALOGE("%s::handle == NULL() fail", __func__);
        return -1;
    }

    /* All buffers have been queued, dequeue one */
    if (gsc_handle->src.qbuf_cnt == MAX_BUFFERS_GSCALER_OUT) {
        memset(&buf, 0, sizeof(struct v4l2_buffer));
        for (i = 0; i < MAX_BUFFERS_GSCALER_OUT; i++)
            memset(&planes[i], 0, sizeof(struct v4l2_plane));

        buf.type     = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
        buf.memory   = V4L2_MEMORY_DMABUF;
        buf.m.planes = planes;

        if (exynos_v4l2_dqbuf(gsc_handle->gsc_vd_entity->fd, &buf) < 0) {
            ALOGE("%s::dequeue buffer failed (index=%d)(mSrcBufNum=%d)", __func__,
                gsc_handle->src.src_buf_idx, MAX_BUFFERS_GSCALER_OUT);
            return -1;
        }
        gsc_handle->src.qbuf_cnt--;
    }

    memset(&buf, 0, sizeof(struct v4l2_buffer));
    for (i = 0; i < NUM_OF_GSC_PLANES; i++)
        memset(&planes[i], 0, sizeof(struct v4l2_plane));

    src_color_space = HAL_PIXEL_FORMAT_2_V4L2_PIX(gsc_handle->src_img.format);
    src_planes = get_yuv_planes(src_color_space);
    src_planes = (src_planes == -1) ? 1 : src_planes;

    buf.type     = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    buf.memory   = V4L2_MEMORY_DMABUF;
    buf.flags    = V4L2_BUF_FLAG_USE_SYNC;
    buf.length   = src_planes;
    buf.index    = gsc_handle->src.src_buf_idx;
    buf.m.planes = planes;
    buf.reserved = src_img->acquireFenceFd;

    gsc_handle->src.addr[0] = src_img->yaddr;
    gsc_handle->src.addr[1] = src_img->uaddr;
    gsc_handle->src.addr[2] = src_img->vaddr;

    if (get_plane_size(src_color_space, plane_size,
        gsc_handle->src_img.fw * gsc_handle->src_img.fh, src_planes) != true) {
        ALOGE("%s:get_plane_size:fail", __func__);
        return -1;
    }

    for (i = 0; i < buf.length; i++) {
        buf.m.planes[i].m.fd = (int)gsc_handle->src.addr[i];
        buf.m.planes[i].length    = plane_size[i];
        buf.m.planes[i].bytesused = plane_size[i];
    }

    /* Queue the buf */
    if (exynos_v4l2_qbuf(gsc_handle->gsc_vd_entity->fd, &buf) < 0) {
        ALOGE("%s::queue buffer failed (index=%d)(mSrcBufNum=%d)", __func__,
            gsc_handle->src.src_buf_idx, MAX_BUFFERS_GSCALER_OUT);
        return -1;
    }
    gsc_handle->src.src_buf_idx++;
    gsc_handle->src.src_buf_idx = gsc_handle->src.src_buf_idx % MAX_BUFFERS_GSCALER_OUT;
    gsc_handle->src.qbuf_cnt++;

    if (gsc_handle->src.stream_on == false) {
        if (exynos_v4l2_streamon(gsc_handle->gsc_vd_entity->fd, buf.type) < 0) {
            ALOGE("%s::stream on failed", __func__);
            return -1;
        }
        gsc_handle->src.stream_on = true;
    }

    src_img->releaseFenceFd = buf.reserved;
    return 0;
}

int exynos_gsc_out_stop(void *handle)
{
    struct GSC_HANDLE *gsc_handle;
    struct v4l2_buffer buf;
    struct v4l2_plane  planes[NUM_OF_GSC_PLANES];
    int i;
    struct media_link *links;

    Exynos_gsc_In();

    gsc_handle = (struct GSC_HANDLE *)handle;
    if (handle == NULL) {
        ALOGE("%s::handle == NULL() fail", __func__);
        return -1;
    }

    if (gsc_handle->src.stream_on == true) {
        if (exynos_v4l2_streamoff(gsc_handle->gsc_vd_entity->fd,
                                V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) < 0) {
            ALOGE("%s::stream off failed", __func__);
            return -1;
        }
        gsc_handle->src.stream_on = false;
    }

    gsc_handle->src.src_buf_idx = 0;
    gsc_handle->src.qbuf_cnt = 0;

    /* unlink : gscaler-out --> fimd */
    for (i = 0; i < (int) gsc_handle->gsc_sd_entity->num_links; i++) {
        links = &gsc_handle->gsc_sd_entity->links[i];

        if (links == NULL || links->source->entity != gsc_handle->gsc_sd_entity ||
                             links->sink->entity   != gsc_handle->sink_sd_entity)
            continue;
        else if (exynos_media_setup_link(gsc_handle->media0,  links->source,
                                                                    links->sink, 0) < 0)
            ALOGE("%s::exynos_media_setup_unlink [src.entity=%d->sink.entity=%d] failed",
                  __func__, links->source->entity->info.id, links->sink->entity->info.id);
    }

    Exynos_gsc_Out();

    return 0;
}

static int exynos_gsc_m2m_run_core(void *handle)
{
    struct GSC_HANDLE *gsc_handle;
    bool is_dirty;
    bool is_drm;

    gsc_handle = (struct GSC_HANDLE *)handle;

    Exynos_gsc_In();

    if (handle == NULL) {
        ALOGE("%s::handle == NULL() fail", __func__);
        return -1;
    }

    is_dirty = gsc_handle->src.dirty || gsc_handle->dst.dirty;
    is_drm = gsc_handle->src.mode_drm;

    if (is_dirty && (gsc_handle->src.mode_drm != gsc_handle->dst.mode_drm)) {
        ALOGE("%s: drm mode mismatch between src and dst, gsc%d (s=%d d=%d)",
              __func__, gsc_handle->gsc_id, gsc_handle->src.mode_drm,
              gsc_handle->dst.mode_drm);
        return -1;
    } else if (is_drm && !gsc_handle->allow_drm) {
        ALOGE("%s: drm mode is not supported on gsc%d", __func__,
              gsc_handle->gsc_id);
        return -1;
    }

    if (m_exynos_gsc_check_src_size(&gsc_handle->src.width, &gsc_handle->src.height,
                                    &gsc_handle->src.crop_left, &gsc_handle->src.crop_top,
                                    &gsc_handle->src.crop_width, &gsc_handle->src.crop_height,
                                    gsc_handle->src.v4l2_colorformat) == false) {
        ALOGE("%s::m_exynos_gsc_check_src_size() fail", __func__);
        return -1;
    }

    if (m_exynos_gsc_check_dst_size(&gsc_handle->dst.width, &gsc_handle->dst.height,
                                    &gsc_handle->dst.crop_left, &gsc_handle->dst.crop_top,
                                    &gsc_handle->dst.crop_width, &gsc_handle->dst.crop_height,
                                    gsc_handle->dst.v4l2_colorformat,
                                    gsc_handle->dst.rotation) == false) {
        ALOGE("%s::m_exynos_gsc_check_dst_size() fail", __func__);
        return -1;
    }

    /* dequeue buffers from previous work if necessary */
    if (gsc_handle->src.stream_on == true) {
        if (exynos_gsc_m2m_wait_frame_done(handle) < 0) {
            ALOGE("%s::exynos_gsc_m2m_wait_frame_done fail", __func__);
            return -1;
        }
    }

    /*
     * need to set the content protection flag before doing reqbufs
     * in set_format
     */
    if (is_dirty && gsc_handle->allow_drm && is_drm) {
        if (exynos_v4l2_s_ctrl(gsc_handle->gsc_fd,
                               V4L2_CID_CONTENT_PROTECTION, is_drm) < 0) {
            ALOGE("%s::exynos_v4l2_s_ctrl() fail", __func__);
            return -1;
        }
    }

    /*
     * from this point on, we have to ensure to call stop to clean up whatever
     * state we have set.
     */

    if (gsc_handle->src.dirty) {
        if (m_exynos_gsc_set_format(gsc_handle->gsc_fd, &gsc_handle->src) == false) {
            ALOGE("%s::m_exynos_gsc_set_format(src) fail", __func__);
            goto done;
        }
        gsc_handle->src.dirty = false;
    }

    if (gsc_handle->dst.dirty) {
        if (m_exynos_gsc_set_format(gsc_handle->gsc_fd, &gsc_handle->dst) == false) {
            ALOGE("%s::m_exynos_gsc_set_format(dst) fail", __func__);
            goto done;
        }
        gsc_handle->dst.dirty = false;
    }

    /* if we are enabling drm, make sure to enable hw protection.
     * Need to do this before queuing buffers so that the mmu is reserved
     * and power domain is kept on.
     */
    if (is_dirty && gsc_handle->allow_drm && is_drm) {
        unsigned int protect_id = 0;

        if (gsc_handle->gsc_id == 0) {
            protect_id = CP_PROTECT_GSC0;
        } else if (gsc_handle->gsc_id == 3) {
            protect_id = CP_PROTECT_GSC3;
        } else {
            ALOGE("%s::invalid gscaler id %d for content protection", __func__,
                  gsc_handle->gsc_id);
            goto done;
        }

#if 0	// FIXME: Not necessary?
        if (CP_Enable_Path_Protection(protect_id) != 0) {
            ALOGE("%s::CP_Enable_Path_Protection failed", __func__);
            goto done;
        }
#endif
        gsc_handle->protection_enabled = true;
    }

    if (m_exynos_gsc_set_addr(gsc_handle->gsc_fd, &gsc_handle->src) == false) {
        ALOGE("%s::m_exynos_gsc_set_addr(src) fail", __func__);
        goto done;
    }

    if (m_exynos_gsc_set_addr(gsc_handle->gsc_fd, &gsc_handle->dst) == false) {
        ALOGE("%s::m_exynos_gsc_set_addr(dst) fail", __func__);
        goto done;
    }

    if (gsc_handle->src.stream_on == false) {
        if (exynos_v4l2_streamon(gsc_handle->gsc_fd, gsc_handle->src.buf_type) < 0) {
            ALOGE("%s::exynos_v4l2_streamon(src) fail", __func__);
            goto done;
        }
        gsc_handle->src.stream_on = true;
    }

    if (gsc_handle->dst.stream_on == false) {
        if (exynos_v4l2_streamon(gsc_handle->gsc_fd, gsc_handle->dst.buf_type) < 0) {
            ALOGE("%s::exynos_v4l2_streamon(dst) fail", __func__);
            goto done;
        }
        gsc_handle->dst.stream_on = true;
    }

    Exynos_gsc_Out();

    return 0;

done:
    exynos_gsc_m2m_stop(handle);
    return -1;
}

static int exynos_gsc_m2m_wait_frame_done(void *handle)
{
    struct GSC_HANDLE *gsc_handle;

    gsc_handle = (struct GSC_HANDLE *)handle;

    Exynos_gsc_In();

    if (handle == NULL) {
        ALOGE("%s::handle == NULL() fail", __func__);
        return -1;
    }

    if ((gsc_handle->src.stream_on == false) || (gsc_handle->dst.stream_on == false)) {
        ALOGE("%s:: src_strean_on or dst_stream_on are false", __func__);
        return -1;
    }

    if (gsc_handle->src.buffer_queued) {
        if (exynos_v4l2_dqbuf(gsc_handle->gsc_fd, &gsc_handle->src.buffer) < 0) {
            ALOGE("%s::exynos_v4l2_dqbuf(src) fail", __func__);
            return -1;
        }
        gsc_handle->src.buffer_queued = false;
    }

    if (gsc_handle->dst.buffer_queued) {
        if (exynos_v4l2_dqbuf(gsc_handle->gsc_fd, &gsc_handle->dst.buffer) < 0) {
            ALOGE("%s::exynos_v4l2_dqbuf(dst) fail", __func__);
            return -1;
        }
        gsc_handle->dst.buffer_queued = false;
    }

    Exynos_gsc_Out();

    return 0;
}

static int exynos_gsc_m2m_stop(void *handle)
{
    struct GSC_HANDLE *gsc_handle;
    struct v4l2_requestbuffers req_buf;
    int ret = 0;

    gsc_handle = (struct GSC_HANDLE *)handle;

    Exynos_gsc_In();

    if (!gsc_handle->src.stream_on && !gsc_handle->dst.stream_on) {
        /* wasn't streaming, return success */
        return 0;
    } else if (gsc_handle->src.stream_on != gsc_handle->dst.stream_on) {
        ALOGE("%s: invalid state, queue stream state doesn't match (%d != %d)",
              __func__, gsc_handle->src.stream_on, gsc_handle->dst.stream_on);
        ret = -1;
    }

    /*
     * we need to plow forward on errors below to make sure that if we had
     * turned on content protection on secure side, we turn it off.
     *
     * also, if we only failed to turn on one of the streams, we'll turn
     * the other one off correctly.
     */
    if (gsc_handle->src.stream_on == true) {
        if (exynos_v4l2_streamoff(gsc_handle->gsc_fd, gsc_handle->src.buf_type) < 0) {
            ALOGE("%s::exynos_v4l2_streamoff(src) fail", __func__);
            ret = -1;
        }
        gsc_handle->src.stream_on = false;
    }


    if (gsc_handle->dst.stream_on == true) {
        if (exynos_v4l2_streamoff(gsc_handle->gsc_fd, gsc_handle->dst.buf_type) < 0) {
            ALOGE("%s::exynos_v4l2_streamoff(dst) fail", __func__);
            ret = -1;
        }
        gsc_handle->dst.stream_on = false;
    }

    /* if drm is enabled */
    if (gsc_handle->allow_drm && gsc_handle->protection_enabled) {
        unsigned int protect_id = 0;

        if (gsc_handle->gsc_id == 0)
            protect_id = CP_PROTECT_GSC0;
        else if (gsc_handle->gsc_id == 3)
            protect_id = CP_PROTECT_GSC3;
#if 0	// FIXME: Not necessary?
        CP_Disable_Path_Protection(protect_id);
#endif
        gsc_handle->protection_enabled = false;
    }

    if (exynos_v4l2_s_ctrl(gsc_handle->gsc_fd,
                           V4L2_CID_CONTENT_PROTECTION, 0) < 0) {
        ALOGE("%s::exynos_v4l2_s_ctrl(V4L2_CID_CONTENT_PROTECTION) fail",
              __func__);
        ret = -1;
    }

    /* src: clear_buf */
    req_buf.count  = 0;
    req_buf.type   = gsc_handle->src.buf_type;
    req_buf.memory = gsc_handle->src.mem_type;
    if (exynos_v4l2_reqbufs(gsc_handle->gsc_fd, &req_buf) < 0) {
        ALOGE("%s::exynos_v4l2_reqbufs():src: fail", __func__);
        ret = -1;
    }

    /* dst: clear_buf */
    req_buf.count  = 0;
    req_buf.type   = gsc_handle->dst.buf_type;
    req_buf.memory = gsc_handle->dst.mem_type;;
    if (exynos_v4l2_reqbufs(gsc_handle->gsc_fd, &req_buf) < 0) {
        ALOGE("%s::exynos_v4l2_reqbufs():dst: fail", __func__);
        ret = -1;
    }

    Exynos_gsc_Out();

    return ret;
}

int exynos_gsc_convert(
    void *handle)
{
    struct GSC_HANDLE *gsc_handle;
    int ret    = -1;
    gsc_handle = (struct GSC_HANDLE *)handle;

    Exynos_gsc_In();

    if (handle == NULL) {
        ALOGE("%s::handle == NULL() fail", __func__);
        return -1;
    }

    exynos_mutex_lock(gsc_handle->op_mutex);

    if (gsc_handle->flag_local_path == true) {
        ALOGE("%s::this exynos_gsc is connected by another hw internaly. So, don't call exynos_gsc_convert()", __func__);
            goto done;
        }

    if (exynos_gsc_m2m_run_core(handle) < 0) {
        ALOGE("%s::exynos_gsc_run_core fail", __func__);
            goto done;
        }

    if (exynos_gsc_m2m_wait_frame_done(handle) < 0) {
        ALOGE("%s::exynos_gsc_m2m_wait_frame_done", __func__);
        goto done;
    }

    if (gsc_handle->src.releaseFenceFd >= 0) {
        close(gsc_handle->src.releaseFenceFd);
        gsc_handle->src.releaseFenceFd = -1;
    }

    if (gsc_handle->dst.releaseFenceFd >= 0) {
        close(gsc_handle->dst.releaseFenceFd);
        gsc_handle->dst.releaseFenceFd = -1;
    }

    if (exynos_gsc_m2m_stop(handle) < 0) {
        ALOGE("%s::exynos_gsc_m2m_stop", __func__);
        goto done;
    }

    ret = 0;

done:
    if (gsc_handle->flag_exclusive_open == false) {
        if (gsc_handle->flag_local_path == false)
            exynos_mutex_unlock(gsc_handle->cur_obj_mutex);
    }

    exynos_mutex_unlock(gsc_handle->op_mutex);

    Exynos_gsc_Out();

    return ret;
}

int exynos_gsc_m2m_run(void *handle,
    exynos_gsc_img *src_img,
    exynos_gsc_img *dst_img)
{
    struct GSC_HANDLE *gsc_handle = handle;
    void *addr[3] = {NULL, NULL, NULL};
    int ret = 0;

    Exynos_gsc_In();

    addr[0] = (void *)src_img->yaddr;
    addr[1] = (void *)src_img->uaddr;
    addr[2] = (void *)src_img->vaddr;
    ret = exynos_gsc_set_src_addr(handle, addr, src_img->mem_type, src_img->acquireFenceFd);
    if (ret < 0) {
        ALOGE("%s::fail: exynos_gsc_set_src_addr[%x %x %x]", __func__,
            (unsigned int)addr[0], (unsigned int)addr[1], (unsigned int)addr[2]);
        return -1;
    }

    addr[0] = (void *)dst_img->yaddr;
    addr[1] = (void *)dst_img->uaddr;
    addr[2] = (void *)dst_img->vaddr;
    ret = exynos_gsc_set_dst_addr(handle, addr, dst_img->mem_type, dst_img->acquireFenceFd);
    if (ret < 0) {
        ALOGE("%s::fail: exynos_gsc_set_dst_addr[%x %x %x]", __func__,
            (unsigned int)addr[0], (unsigned int)addr[1], (unsigned int)addr[2]);
        return -1;
    }

    ret = exynos_gsc_m2m_run_core(handle);
     if (ret < 0) {
        ALOGE("%s::fail: exynos_gsc_m2m_run_core", __func__);
        return -1;
    }

    if (src_img->acquireFenceFd >= 0) {
        close(src_img->acquireFenceFd);
        src_img->acquireFenceFd = -1;
    }

    if (dst_img->acquireFenceFd >= 0) {
        close(dst_img->acquireFenceFd);
        dst_img->acquireFenceFd = -1;
    }

    src_img->releaseFenceFd = gsc_handle->src.releaseFenceFd;
    dst_img->releaseFenceFd = gsc_handle->dst.releaseFenceFd;

    Exynos_gsc_Out();

    return 0;
}

int exynos_gsc_config_exclusive(void *handle,
    exynos_gsc_img *src_img,
    exynos_gsc_img *dst_img)
{

    Exynos_gsc_In();

     struct GSC_HANDLE *gsc_handle;
    int ret = 0;
    gsc_handle = (struct GSC_HANDLE *)handle;
    if (handle == NULL) {
        ALOGE("%s::handle == NULL() fail", __func__);
        return -1;
    }

    switch (gsc_handle->gsc_mode) {
    case GSC_M2M_MODE:
        ret = exynos_gsc_m2m_config(handle, src_img, dst_img);
        break;
    case GSC_OUTPUT_MODE:
        ret = exynos_gsc_out_config(handle, src_img, dst_img);
        break;
    case  GSC_CAPTURE_MODE:
        //to do
        break;
    default:
        break;
    }

    Exynos_gsc_Out();

    return ret;

}

int exynos_gsc_run_exclusive(void *handle,
    exynos_gsc_img *src_img,
    exynos_gsc_img *dst_img)
{
    struct GSC_HANDLE *gsc_handle;
    int ret = 0;

    Exynos_gsc_In();

    gsc_handle = (struct GSC_HANDLE *)handle;
    if (handle == NULL) {
        ALOGE("%s::handle == NULL() fail", __func__);
        return -1;
    }

    switch (gsc_handle->gsc_mode) {
    case GSC_M2M_MODE:
        ret = exynos_gsc_m2m_run(handle, src_img, dst_img);
        break;
    case GSC_OUTPUT_MODE:
        ret = exynos_gsc_out_run(handle, src_img);
        break;
    case  GSC_CAPTURE_MODE:
        //to do
        break;
    default:
        break;
    }

    Exynos_gsc_Out();

    return ret;
}

int exynos_gsc_wait_frame_done_exclusive(void *handle)
{
    struct GSC_HANDLE *gsc_handle;
    int ret = 0;
    gsc_handle = (struct GSC_HANDLE *)handle;

    Exynos_gsc_In();

    if (handle == NULL) {
        ALOGE("%s::handle == NULL() fail", __func__);
        return -1;
    }

    if (gsc_handle->gsc_mode == GSC_M2M_MODE)
        ret = exynos_gsc_m2m_wait_frame_done(handle);

    Exynos_gsc_Out();

    return ret;
}

int exynos_gsc_stop_exclusive(void *handle)
{
    struct GSC_HANDLE *gsc_handle;
    int ret = 0;
    gsc_handle = (struct GSC_HANDLE *)handle;

    Exynos_gsc_In();

    if (handle == NULL) {
        ALOGE("%s::handle == NULL() fail", __func__);
        return -1;
    }

    switch (gsc_handle->gsc_mode) {
    case GSC_M2M_MODE:
        ret = exynos_gsc_m2m_stop(handle);
        break;
    case GSC_OUTPUT_MODE:
        ret = exynos_gsc_out_stop(handle);
        break;
    case  GSC_CAPTURE_MODE:
        //to do
        break;
    default:
        break;
    }

    Exynos_gsc_Out();

    return ret;
}

int exynos_gsc_connect(
    void *handle,
    void *hw)
{
    struct GSC_HANDLE *gsc_handle;
    int ret    = -1;
    gsc_handle = (struct GSC_HANDLE *)handle;

    Exynos_gsc_In();

    if (handle == NULL) {
        ALOGE("%s::handle == NULL() fail", __func__);
        return -1;
    }

    exynos_mutex_lock(gsc_handle->op_mutex);

    gsc_handle->flag_local_path = true;

    if (exynos_mutex_trylock(gsc_handle->cur_obj_mutex) == false) {
        if (m_exynos_gsc_find_and_trylock_and_create(gsc_handle) == false) {
            ALOGE("%s::m_exynos_gsc_find_and_trylock_and_create() fail", __func__);
            goto done;
        }
    }

    ret = 0;

    Exynos_gsc_Out();

done:
    exynos_mutex_unlock(gsc_handle->op_mutex);

    return ret;
}

int exynos_gsc_disconnect(
    void *handle,
    void *hw)
{
    struct GSC_HANDLE *gsc_handle;
    gsc_handle = (struct GSC_HANDLE *)handle;

    Exynos_gsc_In();

    if (handle == NULL) {
        ALOGE("%s::handle == NULL() fail", __func__);
        return -1;
    }

    exynos_mutex_lock(gsc_handle->op_mutex);

    gsc_handle->flag_local_path = false;

    exynos_mutex_unlock(gsc_handle->cur_obj_mutex);

    exynos_mutex_unlock(gsc_handle->op_mutex);

    Exynos_gsc_Out();

    return 0;
}
