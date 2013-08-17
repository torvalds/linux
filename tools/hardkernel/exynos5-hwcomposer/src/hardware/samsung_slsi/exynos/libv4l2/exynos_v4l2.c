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
 * \file      exynos_v4l2.c
 * \brief     source file for libv4l2
 * \author    Jinsung Yang (jsgood.yang@samsung.com)
 * \author    Sangwoo Park (sw5771.park@samsung.com)
 * \date      2012/01/17
 *
 * <b>Revision History: </b>
 * - 2012/01/17: Jinsung Yang (jsgood.yang@samsung.com) \n
 *   Initial version
 *
 */

#include <stdio.h>
#include <errno.h>
#include <stdarg.h>
#include <fcntl.h>
#include <string.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/stat.h>

#include "exynos_v4l2.h"

//#define LOG_NDEBUG 0
#define LOG_TAG "libexynosv4l2"
#if defined(ANDROID)
#include <utils/Log.h>
#include "Exynos_log.h"
#else
#include <log.h>
#endif

#define VIDEODEV_MINOR_MAX 63

//#define EXYNOS_V4L2_TRACE 0
#ifdef EXYNOS_V4L2_TRACE
#define Exynos_v4l2_In() Exynos_Log(EXYNOS_DEV_LOG_DEBUG, LOG_TAG, "%s In , Line: %d", __FUNCTION__, __LINE__)
#define Exynos_v4l2_Out() Exynos_Log(EXYNOS_DEV_LOG_DEBUG, LOG_TAG, "%s Out , Line: %d", __FUNCTION__, __LINE__)
#else
#define Exynos_v4l2_In() ((void *)0)
#define Exynos_v4l2_Out() ((void *)0)
#endif

static bool __v4l2_check_buf_type(enum v4l2_buf_type type)
{
    bool supported;

    switch (type) {
    case V4L2_BUF_TYPE_VIDEO_CAPTURE:
    case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
    case V4L2_BUF_TYPE_VIDEO_OUTPUT:
    case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
    case V4L2_BUF_TYPE_VIDEO_OVERLAY:
        supported = true;
        break;

    default:
        supported = (type >= V4L2_BUF_TYPE_PRIVATE) ? true : false;
        break;
    }

    return supported;
}

static int __v4l2_open(const char *filename, int oflag, va_list ap)
{
    mode_t mode = 0;
    int fd;

    if (oflag & O_CREAT)
        mode = va_arg(ap, int);

    fd = open(filename, oflag, mode);

    return fd;
}

int exynos_v4l2_open(const char *filename, int oflag, ...)
{
    va_list ap;
    int fd;

    Exynos_v4l2_In();

    va_start(ap, oflag);
    fd = __v4l2_open(filename, oflag, ap);
    va_end(ap);

    Exynos_v4l2_Out();

    return fd;
}

int exynos_v4l2_open_devname(const char *devname, int oflag, ...)
{
    bool found = false;
    int fd = -1;
    struct stat s;
    va_list ap;
    FILE *stream_fd;
    char filename[64], name[64];
    int minor, size, i = 0;

    Exynos_v4l2_In();

    do {
        if (i > VIDEODEV_MINOR_MAX)
            break;

        /* video device node */
        snprintf(filename, sizeof(filename), "/dev/video%d", i++);

        /* if the node is video device */
        if ((lstat(filename, &s) == 0) && S_ISCHR(s.st_mode) &&
                ((int)((unsigned short)(s.st_rdev) >> 8) == 81)) {
            minor = (int)((unsigned short)(s.st_rdev & 0x3f));
            ALOGD("try node: %s, minor: %d", filename, minor);
            /* open sysfs entry */
            snprintf(filename, sizeof(filename), "/sys/class/video4linux/video%d/name", minor);
            if (S_ISLNK(s.st_mode)) {
                ALOGE("symbolic link detected");
                return -1;
            }
            stream_fd = fopen(filename, "r");
            if (stream_fd == NULL) {
                ALOGE("failed to open sysfs entry for videodev");
                continue;   /* try next */
            }

            /* read sysfs entry for device name */
            size = (int)fgets(name, sizeof(name), stream_fd);
            fclose(stream_fd);

            /* check read size */
            if (size == 0) {
                ALOGE("failed to read sysfs entry for videodev");
            } else {
                /* matched */
                if (strncmp(name, devname, strlen(devname)) == 0) {
                    ALOGI("node found for device %s: /dev/video%d", devname, minor);
                    found = true;
                }
            }
        }
    } while (found == false);

    if (found) {
        snprintf(filename, sizeof(filename), "/dev/video%d", minor);
        va_start(ap, oflag);
        fd = __v4l2_open(filename, oflag, ap);
        va_end(ap);

        if (fd > 0)
            ALOGI("open video device %s", filename);
        else
            ALOGE("failed to open video device %s", filename);
    } else {
        ALOGE("no video device found");
    }

    Exynos_v4l2_Out();

    return fd;
}

int exynos_v4l2_close(int fd)
{
    int ret = -1;

    Exynos_v4l2_In();

    if (fd < 0)
        ALOGE("%s: invalid fd: %d", __func__, fd);
    else
        ret = close(fd);

    Exynos_v4l2_Out();

    return ret;
}

bool exynos_v4l2_enuminput(int fd, int index, char *input_name_buf)
{
    int ret = -1;
    struct v4l2_input input;

    Exynos_v4l2_In();

    if (fd < 0) {
        ALOGE("%s: invalid fd: %d", __func__, fd);
        return NULL;
    }

    input.index = index;
    ret = ioctl(fd, VIDIOC_ENUMINPUT, &input, 32);
    if (ret) {
        ALOGE("%s: no matching index founds", __func__);
        return false;
    }

    ALOGI("Name of input channel[%d] is %s", input.index, input.name);

    strncpy(input_name_buf, (const char *)input.name, 32);

    Exynos_v4l2_Out();

    return true;
}

int exynos_v4l2_s_input(int fd, int index)
{
    int ret = -1;
    struct v4l2_input input;

    Exynos_v4l2_In();

    if (fd < 0) {
        ALOGE("%s: invalid fd: %d", __func__, fd);
        return ret;
    }

    input.index = index;

    ret = ioctl(fd, VIDIOC_S_INPUT, &input);
    if (ret){
        ALOGE("failed to ioctl: VIDIOC_S_INPUT (%d - %s)", errno, strerror(errno));
        return ret;
    }

    Exynos_v4l2_Out();

    return ret;
}

bool exynos_v4l2_querycap(int fd, unsigned int need_caps)
{
    struct v4l2_capability cap;
    int ret;

    Exynos_v4l2_In();

    if (fd < 0) {
        ALOGE("%s: invalid fd: %d", __func__, fd);
        return false;
    }

    if (!(need_caps & V4L2_CAP_VIDEO_CAPTURE) &&
            !(need_caps & V4L2_CAP_VIDEO_CAPTURE_MPLANE) &&
            !(need_caps & V4L2_CAP_VIDEO_OUTPUT) &&
            !(need_caps & V4L2_CAP_VIDEO_OUTPUT_MPLANE) &&
            !(need_caps & V4L2_CAP_VIDEO_OVERLAY)) {
        ALOGE("%s: unsupported capabilities", __func__);
        return false;
    }

    memset(&cap, 0, sizeof(cap));

    ret = ioctl(fd, VIDIOC_QUERYCAP, &cap);
    if (ret) {
        ALOGE("failed to ioctl: VIDIOC_QUERYCAP (%d - %s)", errno, strerror(errno));
        return false;
    }

    if ((need_caps & cap.capabilities) != need_caps) {
        ALOGE("%s: unsupported capabilities", __func__);
        return false;
    }

    Exynos_v4l2_Out();

    return true;
}

bool exynos_v4l2_enum_fmt(int fd, enum v4l2_buf_type type, unsigned int fmt)
{
    struct v4l2_fmtdesc fmtdesc;
    int found = 0;

    Exynos_v4l2_In();

    fmtdesc.type = type;
    fmtdesc.index = 0;

    while (ioctl(fd, VIDIOC_ENUM_FMT, &fmtdesc) == 0) {
        if (fmtdesc.pixelformat == fmt) {
            ALOGE("Passed fmt = %#x found pixel format[%d]: %s", fmt, fmtdesc.index, fmtdesc.description);
            found = 1;
            break;
        }

        fmtdesc.index++;
    }

    if (!found) {
        ALOGE("%s: unsupported pixel format", __func__);
        return false;
    }

    Exynos_v4l2_Out();

    return true;
}

int exynos_v4l2_g_fmt(int fd, struct v4l2_format *fmt)
{
    int ret = -1;

    Exynos_v4l2_In();

    if (fd < 0) {
        ALOGE("%s: invalid fd: %d", __func__, fd);
        return ret;
    }

    if (!fmt) {
        ALOGE("%s: fmt is NULL", __func__);
        return ret;
    }

    if (__v4l2_check_buf_type(fmt->type) == false) {
        ALOGE("%s: unsupported buffer type", __func__);
        return ret;
    }

    ret = ioctl(fd, VIDIOC_G_FMT, fmt);
    if (ret) {
        ALOGE("failed to ioctl: VIDIOC_G_FMT (%d - %s)", errno, strerror(errno));
        return ret;
    }

    Exynos_v4l2_Out();

    return ret;
}

static int __v4l2_s_fmt(int fd, unsigned int request, struct v4l2_format *fmt)
{
    int ret = -1;

    Exynos_v4l2_In();

    if (fd < 0) {
        ALOGE("%s: invalid fd: %d", __func__, fd);
        return ret;
    }

    if (!fmt) {
        ALOGE("%s: fmt is NULL", __func__);
        return ret;
    }

    if (__v4l2_check_buf_type(fmt->type) == false) {
        ALOGE("%s: unsupported buffer type", __func__);
        return ret;
    } else {
        ret = ioctl(fd, request, fmt);
        if (ret) {
            if (request == VIDIOC_TRY_FMT)
                ALOGE("failed to ioctl: VIDIOC_TRY_FMT (%d - %s)", errno, strerror(errno));
            else
                ALOGE("failed to ioctl: VIDIOC_S_FMT (%d - %s)", errno, strerror(errno));

            return ret;
        }
    }

    Exynos_v4l2_Out();

    return ret;
}

int exynos_v4l2_try_fmt(int fd, struct v4l2_format *fmt)
{
    return __v4l2_s_fmt(fd, VIDIOC_TRY_FMT, fmt);
}

int exynos_v4l2_s_fmt(int fd, struct v4l2_format *fmt)
{
    return __v4l2_s_fmt(fd, VIDIOC_S_FMT, fmt);
}

int exynos_v4l2_reqbufs(int fd, struct v4l2_requestbuffers *req)
{
    int ret = -1;
    unsigned int count;

    Exynos_v4l2_In();

    if (fd < 0) {
        ALOGE("%s: invalid fd: %d", __func__, fd);
        return ret;
    }

    if (!req) {
        ALOGE("%s: req is NULL", __func__);
        return ret;
    }

    if ((req->memory != V4L2_MEMORY_MMAP) &&
	(req->memory != V4L2_MEMORY_USERPTR) &&
	(req->memory != V4L2_MEMORY_DMABUF)) {
        ALOGE("%s: unsupported memory type", __func__);
        return ret;
    }

    if (__v4l2_check_buf_type(req->type) == false) {
        ALOGE("%s: unsupported buffer type", __func__);
        return ret;
    }

    count = req->count;

    ret = ioctl(fd, VIDIOC_REQBUFS, req);
    if (ret) {
        ALOGE("failed to ioctl: VIDIOC_REQBUFS (%d - %s)", ret, strerror(errno));
        return ret;
    }

    if (count != req->count) {
        ALOGW("number of buffers had been changed: %d => %d", count, req->count);
    }

    Exynos_v4l2_Out();

    return ret;
}

int exynos_v4l2_querybuf(int fd, struct v4l2_buffer *buf)
{
    int ret = -1;

    Exynos_v4l2_In();

    if (fd < 0) {
        ALOGE("%s: invalid fd: %d", __func__, fd);
        return ret;
    }

    if (!buf) {
        ALOGE("%s: buf is NULL", __func__);
        return ret;
    }

    if ((buf->memory != V4L2_MEMORY_MMAP) &&
	(buf->memory != V4L2_MEMORY_DMABUF)) {
        ALOGE("%s: unsupported memory type", __func__);
        return ret;
    }

    if (__v4l2_check_buf_type(buf->type) == false) {
        ALOGE("%s: unsupported buffer type", __func__);
        return ret;
    }

    ret = ioctl(fd, VIDIOC_QUERYBUF, buf);
    if (ret) {
        ALOGE("failed to ioctl: VIDIOC_QUERYBUF (%d - %s)", errno, strerror(errno));
        return ret;
    }

    Exynos_v4l2_Out();

    return ret;
}

int exynos_v4l2_qbuf(int fd, struct v4l2_buffer *buf)
{
    int ret = -1;

    Exynos_v4l2_In();

    if (fd < 0) {
        ALOGE("%s: invalid fd: %d", __func__, fd);
        return ret;
    }

    if (!buf) {
        ALOGE("%s: buf is NULL", __func__);
        return ret;
    }

    if ((buf->memory != V4L2_MEMORY_MMAP) &&
	(buf->memory != V4L2_MEMORY_USERPTR) &&
	(buf->memory != V4L2_MEMORY_DMABUF)) {
        ALOGE("%s: unsupported memory type", __func__);
        return ret;
    }

    if (__v4l2_check_buf_type(buf->type) == false) {
        ALOGE("%s: unsupported buffer type", __func__);
        return ret;
    }

    ret = ioctl(fd, VIDIOC_QBUF, buf);
    if (ret) {
        ALOGE("failed to ioctl: VIDIOC_QBUF (%d - %s)", errno, strerror(errno));
        return ret;
    }

    Exynos_v4l2_Out();

    return ret;
}

int exynos_v4l2_dqbuf(int fd, struct v4l2_buffer *buf)
{
    int ret = -1;

    Exynos_v4l2_In();

    if (fd < 0) {
        ALOGE("%s: invalid fd: %d", __func__, fd);
        return ret;
    }

    if (!buf) {
        ALOGE("%s: buf is NULL", __func__);
        return ret;
    }

    if ((buf->memory != V4L2_MEMORY_MMAP) &&
	(buf->memory != V4L2_MEMORY_USERPTR) &&
	(buf->memory != V4L2_MEMORY_DMABUF)) {
        ALOGE("%s: unsupported memory type", __func__);
        return ret;
    }

    if (__v4l2_check_buf_type(buf->type) == false) {
        ALOGE("%s: unsupported buffer type", __func__);
        return ret;
    }

    ret = ioctl(fd, VIDIOC_DQBUF, buf);
    if (ret) {
        if (errno == EAGAIN)
            return -errno;

        ALOGW("failed to ioctl: VIDIOC_DQBUF (%d - %s)", errno, strerror(errno));
        return ret;
    }

    Exynos_v4l2_Out();

    return ret;
}

int exynos_v4l2_streamon(int fd, enum v4l2_buf_type type)
{
    int ret = -1;

    Exynos_v4l2_In();

    if (fd < 0) {
        ALOGE("%s: invalid fd: %d", __func__, fd);
        return ret;
    }

    if (__v4l2_check_buf_type(type) == false) {
        ALOGE("%s: unsupported buffer type", __func__);
        return ret;
    }

    ret = ioctl(fd, VIDIOC_STREAMON, &type);
    if (ret) {
        ALOGE("failed to ioctl: VIDIOC_STREAMON (%d - %s)", errno, strerror(errno));
        return ret;
    }

    Exynos_v4l2_Out();

    return ret;
}

int exynos_v4l2_streamoff(int fd, enum v4l2_buf_type type)
{
    int ret = -1;

    Exynos_v4l2_In();

    if (fd < 0) {
        ALOGE("%s: invalid fd: %d", __func__, fd);
        return ret;
    }

    if (__v4l2_check_buf_type(type) == false) {
        ALOGE("%s: unsupported buffer type", __func__);
        return ret;
    }

    ret = ioctl(fd, VIDIOC_STREAMOFF, &type);
    if (ret) {
        ALOGE("failed to ioctl: VIDIOC_STREAMOFF (%d - %s)", errno, strerror(errno));
        return ret;
    }

    Exynos_v4l2_Out();

    return ret;
}

int exynos_v4l2_cropcap(int fd, struct v4l2_cropcap *crop)
{
    int ret = -1;

    Exynos_v4l2_In();

    if (fd < 0) {
        ALOGE("%s: invalid fd: %d", __func__, fd);
        return ret;
    }

    if (!crop) {
        ALOGE("%s: crop is NULL", __func__);
        return ret;
    }

    if (__v4l2_check_buf_type(crop->type) == false) {
        ALOGE("%s: unsupported buffer type", __func__);
        return ret;
    }

    ret = ioctl(fd, VIDIOC_CROPCAP, crop);
    if (ret) {
        ALOGE("failed to ioctl: VIDIOC_CROPCAP (%d - %s)", errno, strerror(errno));
        return ret;
    }

    Exynos_v4l2_Out();

    return ret;
}

int exynos_v4l2_g_crop(int fd, struct v4l2_crop *crop)
{
    int ret = -1;

    Exynos_v4l2_In();

    if (fd < 0) {
        ALOGE("%s: invalid fd: %d", __func__, fd);
        return ret;
    }

    if (!crop) {
        ALOGE("%s: crop is NULL", __func__);
        return ret;
    }

    if (__v4l2_check_buf_type(crop->type) == false) {
        ALOGE("%s: unsupported buffer type", __func__);
        return ret;
    }

    ret = ioctl(fd, VIDIOC_G_CROP, crop);
    if (ret) {
        ALOGE("failed to ioctl: VIDIOC_G_CROP (%d - %s)", errno, strerror(errno));
        return ret;
    }

    Exynos_v4l2_Out();

    return ret;
}

int exynos_v4l2_s_crop(int fd, struct v4l2_crop *crop)
{
    int ret = -1;

    Exynos_v4l2_In();

    if (fd < 0) {
        ALOGE("%s: invalid fd: %d", __func__, fd);
        return ret;
    }

    if (!crop) {
        ALOGE("%s: crop is NULL", __func__);
        return ret;
    }

    if (__v4l2_check_buf_type(crop->type) == false) {
        ALOGE("%s: unsupported buffer type", __func__);
        return ret;
    }

    ret = ioctl(fd, VIDIOC_S_CROP, crop);
    if (ret) {
        ALOGE("failed to ioctl: VIDIOC_S_CROP (%d - %s)", errno, strerror(errno));
        return ret;
    }

    Exynos_v4l2_Out();

    return ret;
}

int exynos_v4l2_g_ctrl(int fd, unsigned int id, int *value)
{
    int ret = -1;
    struct v4l2_control ctrl;

    Exynos_v4l2_In();

    ctrl.id = id;

    if (fd < 0) {
        ALOGE("%s: invalid fd: %d", __func__, fd);
        return ret;
    }

    ret = ioctl(fd, VIDIOC_G_CTRL, &ctrl);
    if (ret) {
        ALOGE("failed to ioctl: VIDIOC_G_CTRL (%d - %s)", errno, strerror(errno));
        return ret;
    }

    *value = ctrl.value;

    Exynos_v4l2_Out();

    return ret;
}

int exynos_v4l2_s_ctrl(int fd, unsigned int id, int value)
{
    int ret = -1;
    struct v4l2_control ctrl;

    Exynos_v4l2_In();

    ctrl.id = id;
    ctrl.value = value;

    if (fd < 0) {
        ALOGE("%s: invalid fd: %d", __func__, fd);
        return ret;
    }

    ret = ioctl(fd, VIDIOC_S_CTRL, &ctrl);
    if (ret) {
        ALOGE("failed to ioctl: VIDIOC_S_CTRL (%d)", errno);
        return ret;
    }

    Exynos_v4l2_Out();

    return ret;
}

int exynos_v4l2_g_parm(int fd, struct v4l2_streamparm *streamparm)
{
    int ret = -1;

    Exynos_v4l2_In();

    if (fd < 0) {
        ALOGE("%s: invalid fd: %d", __func__, fd);
        return ret;
    }

    if (__v4l2_check_buf_type(streamparm->type) == false) {
        ALOGE("%s: unsupported buffer type", __func__);
        return ret;
    }

    ret = ioctl(fd, VIDIOC_G_PARM, streamparm);
    if (ret) {
        ALOGE("failed to ioctl: VIDIOC_G_PARM (%d - %s)", errno, strerror(errno));
        return ret;
    }

    Exynos_v4l2_Out();

    return ret;
}

int exynos_v4l2_s_parm(int fd, struct v4l2_streamparm *streamparm)
{
    int ret = -1;

    Exynos_v4l2_In();

    if (fd < 0) {
        ALOGE("%s: invalid fd: %d", __func__, fd);
        return ret;
    }

    if (__v4l2_check_buf_type(streamparm->type) == false) {
        ALOGE("%s: unsupported buffer type", __func__);
        return ret;
    }

    ret = ioctl(fd, VIDIOC_S_PARM, streamparm);
    if (ret) {
        ALOGE("failed to ioctl: VIDIOC_S_PARM (%d - %s)", errno, strerror(errno));
        return ret;
    }

    Exynos_v4l2_Out();

    return ret;
}

int exynos_v4l2_g_ext_ctrl(int fd, struct v4l2_ext_controls *ctrl)
{
    int ret = -1;

    Exynos_v4l2_In();

    if (fd < 0) {
        ALOGE("%s: invalid fd: %d", __func__, fd);
        return ret;
    }

    if (ctrl == NULL) {
        ALOGE("%s: ctrl is NULL", __func__);
        return ret;
    }

    ret = ioctl(fd, VIDIOC_G_EXT_CTRLS, ctrl);
    if (ret)
        ALOGE("failed to ioctl: VIDIOC_G_EXT_CTRLS (%d - %s)", errno, strerror(errno));

    Exynos_v4l2_Out();

    return ret;
}

int exynos_v4l2_s_ext_ctrl(int fd, struct v4l2_ext_controls *ctrl)
{
    int ret = -1;

    Exynos_v4l2_In();

    if (fd < 0) {
        ALOGE("%s: invalid fd: %d", __func__, fd);
        return ret;
    }

    if (ctrl == NULL) {
        ALOGE("%s: ctrl is NULL", __func__);
        return ret;
    }

    ret = ioctl(fd, VIDIOC_S_EXT_CTRLS, ctrl);
    if (ret)
        ALOGE("failed to ioctl: VIDIOC_S_EXT_CTRLS (%d - %s)", errno, strerror(errno));

    Exynos_v4l2_Out();

    return ret;
}
