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
 * \file      exynos_subdev.c
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
#include <stdarg.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/stat.h>

#include "exynos_v4l2.h"

//#define LOG_NDEBUG 0
#define LOG_TAG "libexynosv4l2-subdev"
#if defined(ANDROID)
#include <utils/Log.h>
#else
#include <log.h>
#endif

#define SUBDEV_MINOR_MAX 191

static int __subdev_open(const char *filename, int oflag, va_list ap)
{
    mode_t mode = 0;
    int fd;

    if (oflag & O_CREAT)
        mode = va_arg(ap, int);

    fd = open(filename, oflag, mode);

    return fd;
}

int exynos_subdev_open(const char *filename, int oflag, ...)
{
    va_list ap;
    int fd;

    va_start(ap, oflag);
    fd = __subdev_open(filename, oflag, ap);
    va_end(ap);

    return fd;
}

int exynos_subdev_get_node_num(const char *devname, int oflag, ...)
{
    bool found = false;
    int ret = -1;
    struct stat s;
    va_list ap;
    FILE *stream_fd;
    char filename[64], name[64];
    int minor, size, i = 0;

    do {
        if (i > (SUBDEV_MINOR_MAX - 128))
            break;

        /* video device node */
        snprintf(filename, sizeof(filename), "/dev/v4l-subdev%d", i++);

        /* if the node is video device */
        if ((lstat(filename, &s) == 0) && S_ISCHR(s.st_mode) &&
                ((int)((unsigned short)(s.st_rdev) >> 8) == 81)) {
            minor = (int)((unsigned short)(s.st_rdev & 0x3f));
            ALOGD("try node: %s, minor: %d", filename, minor);
            /* open sysfs entry */
            snprintf(filename, sizeof(filename), "/sys/class/video4linux/v4l-subdev%d/name", minor);
            if (S_ISLNK(s.st_mode)) {
                ALOGE("symbolic link detected");
                return -1;
            }
            stream_fd = fopen(filename, "r");
            if (stream_fd == NULL) {
                ALOGE("failed to open sysfs entry for subdev");
                continue;   /* try next */
            }

            /* read sysfs entry for device name */
            size = (int)fgets(name, sizeof(name), stream_fd);
            fclose(stream_fd);

            /* check read size */
            if (size == 0) {
                ALOGE("failed to read sysfs entry for subdev");
            } else {
                /* matched */
                if (strncmp(name, devname, strlen(devname)) == 0) {
                    ALOGI("node found for device %s: /dev/v4l-subdev%d", devname, minor);
                    found = true;
                }
            }
        }
    } while (found == false);

    if (found)
        ret = minor;
    else
        ALOGE("no subdev device found");

    return ret;
}

int exynos_subdev_open_devname(const char *devname, int oflag, ...)
{
    bool found = false;
    int fd = -1;
    struct stat s;
    va_list ap;
    FILE *stream_fd;
    char filename[64], name[64];
    int minor, size, i = 0;

    do {
        if (i > (SUBDEV_MINOR_MAX - 128))
            break;

        /* video device node */
        snprintf(filename, sizeof(filename), "/dev/v4l-subdev%d", i++);

        /* if the node is video device */
        if ((lstat(filename, &s) == 0) && S_ISCHR(s.st_mode) &&
                ((int)((unsigned short)(s.st_rdev) >> 8) == 81)) {
            minor = (int)((unsigned short)(s.st_rdev & 0x3f));
            ALOGD("try node: %s, minor: %d", filename, minor);
            /* open sysfs entry */
            snprintf(filename, sizeof(filename), "/sys/class/video4linux/v4l-subdev%d/name", minor);
            if (S_ISLNK(s.st_mode)) {
                ALOGE("symbolic link detected");
                return -1;
            }
            stream_fd = fopen(filename, "r");
            if (stream_fd == NULL) {
                ALOGE("failed to open sysfs entry for subdev");
                continue;   /* try next */
            }

            /* read sysfs entry for device name */
            size = (int)fgets(name, sizeof(name), stream_fd);
            fclose(stream_fd);

            /* check read size */
            if (size == 0) {
                ALOGE("failed to read sysfs entry for subdev");
            } else {
                /* matched */
                if (strncmp(name, devname, strlen(devname)) == 0) {
                    ALOGI("node found for device %s: /dev/v4l-subdev%d", devname, minor);
                    found = true;
                }
            }
        }
    } while (found == false);

    if (found) {
        snprintf(filename, sizeof(filename), "/dev/v4l-subdev%d", minor);
        va_start(ap, oflag);
        fd = __subdev_open(filename, oflag, ap);
        va_end(ap);

        if (fd > 0)
            ALOGI("open subdev device %s", filename);
        else
            ALOGE("failed to open subdev device %s", filename);
    } else {
        ALOGE("no subdev device found");
    }

    return fd;
}

int exynos_subdev_close(int fd)
{
    int ret = -1;

    if (fd < 0)
        ALOGE("%s: invalid fd: %d", __func__, fd);
    else
        ret = close(fd);

    return ret;
}

/**
 * @brief enum frame size on a pad.
 * @return 0 on success, or a negative error code on failure.
 */
int exynos_subdev_enum_frame_size(int fd, struct v4l2_subdev_frame_size_enum *frame_size_enum)
{
    int ret = -1;

    if (fd < 0) {
        ALOGE("%s: invalid fd: %d", __func__, fd);
        return ret;
    }

    if (!frame_size_enum) {
        ALOGE("%s: frame_size_enum is NULL", __func__);
        return ret;
    }

    ret = ioctl(fd, VIDIOC_SUBDEV_ENUM_FRAME_SIZE, frame_size_enum);
    if (ret) {
        ALOGE("failed to ioctl: VIDIOC_SUBDEV_ENUM_FRAME_SIZE");
        return ret;
    }

    return ret;
}

/**
 * @brief Retrieve the format on a pad.
 * @return 0 on success, or a negative error code on failure.
 */
int exynos_subdev_g_fmt(int fd, struct v4l2_subdev_format *fmt)
{
    int ret = -1;

    if (fd < 0) {
        ALOGE("%s: invalid fd: %d", __func__, fd);
        return ret;
    }

    if (!fmt) {
        ALOGE("%s: fmt is NULL", __func__);
        return ret;
    }

    ret = ioctl(fd, VIDIOC_SUBDEV_G_FMT, fmt);
    if (ret) {
        ALOGE("failed to ioctl: VIDIOC_SUBDEV_G_FMT");
        return ret;
    }

    return ret;
}

/**
 * @brief Set the format on a pad.
 * @return 0 on success, or a negative error code on failure.
 */
int exynos_subdev_s_fmt(int fd, struct v4l2_subdev_format *fmt)
{
    int ret = -1;

    if (fd < 0) {
        ALOGE("%s: invalid fd: %d", __func__, fd);
        return ret;
    }

    if (!fmt) {
        ALOGE("%s: fmt is NULL", __func__);
        return ret;
    }

    ret = ioctl(fd, VIDIOC_SUBDEV_S_FMT, fmt);
    if (ret) {
        ALOGE("failed to ioctl: VIDIOC_SUBDEV_S_FMT");
        return ret;
    }

    return ret;
}

/**
 * @brief Retrieve the crop rectangle on a pad.
 * @return 0 on success, or a negative error code on failure.
 */
int exynos_subdev_g_crop(int fd, struct v4l2_subdev_crop *crop)
{
    int ret = -1;

    if (fd < 0) {
        ALOGE("%s: invalid fd: %d", __func__, fd);
        return ret;
    }

    if (!crop) {
        ALOGE("%s: crop is NULL", __func__);
        return ret;
    }

    ret = ioctl(fd, VIDIOC_SUBDEV_G_CROP, crop);
    if (ret) {
        ALOGE("failed to ioctl: VIDIOC_SUBDEV_G_CROP");
        return ret;
    }

    return ret;
}

/**
 * @brief Set the crop rectangle on a pad.
 * @return 0 on success, or a negative error code on failure.
 */
int exynos_subdev_s_crop(int fd, struct v4l2_subdev_crop *crop)
{
    int ret = -1;

    if (fd < 0) {
        ALOGE("%s: invalid fd: %d", __func__, fd);
        return ret;
    }

    if (!crop) {
        ALOGE("%s: crop is NULL", __func__);
        return ret;
    }

    ret = ioctl(fd, VIDIOC_SUBDEV_S_CROP, crop);
    if (ret) {
        ALOGE("failed to ioctl: VIDIOC_SUBDEV_S_CROP");
        return ret;
    }

    return ret;
}

/**
 * @brief Retrieve the frame interval on a sub-device.
 * @return 0 on success, or a negative error code on failure.
 */
int exynos_subdev_enum_frame_interval(int fd, struct v4l2_subdev_frame_interval_enum *frame_internval_enum)
{
    int ret = -1;

    if (fd < 0) {
        ALOGE("%s: invalid fd: %d", __func__, fd);
        return ret;
    }

    if (!frame_internval_enum) {
        ALOGE("%s: frame_internval_enum is NULL", __func__);
        return ret;
    }

    ret = ioctl(fd, VIDIOC_SUBDEV_ENUM_FRAME_INTERVAL, frame_internval_enum);
    if (ret) {
        ALOGE("failed to ioctl: VIDIOC_SUBDEV_ENUM_FRAME_INTERVAL");
        return ret;
    }

    return ret;
}

/**
 * @brief Retrieve the frame interval on a sub-device.
 * @return 0 on success, or a negative error code on failure.
 */
int exynos_subdev_g_frame_interval(int fd, struct v4l2_subdev_frame_interval *frame_internval)
{
    int ret = -1;

    if (fd < 0) {
        ALOGE("%s: invalid fd: %d", __func__, fd);
        return ret;
    }

    if (!frame_internval) {
        ALOGE("%s: frame_internval is NULL", __func__);
        return ret;
    }

    ret = ioctl(fd, VIDIOC_SUBDEV_G_FRAME_INTERVAL, frame_internval);
    if (ret) {
        ALOGE("failed to ioctl: VIDIOC_SUBDEV_G_FRAME_INTERVAL");
        return ret;
    }

    return ret;
}

/**
 * @brief Set the frame interval on a sub-device.
 * @return 0 on success, or a negative error code on failure.
 */
int exynos_subdev_s_frame_interval(int fd, struct v4l2_subdev_frame_interval *frame_internval)
{
    int ret = -1;

    if (fd < 0) {
        ALOGE("%s: invalid fd: %d", __func__, fd);
        return ret;
    }

    if (!frame_internval) {
        ALOGE("%s: frame_internval is NULL", __func__);
        return ret;
    }

    ret = ioctl(fd, VIDIOC_SUBDEV_S_FRAME_INTERVAL, frame_internval);
    if (ret) {
        ALOGE("failed to ioctl: VIDIOC_SUBDEV_S_FRAME_INTERVAL");
        return ret;
    }

    return ret;
}

/**
 * @brief enum mbus code
 * @return 0 on success, or a negative error code on failure.
 */
int exynos_subdev_enum_mbus_code(int fd, struct v4l2_subdev_mbus_code_enum *mbus_code_enum)
{
    int ret = -1;

    if (fd < 0) {
        ALOGE("%s: invalid fd: %d", __func__, fd);
        return ret;
    }

    if (!mbus_code_enum) {
        ALOGE("%s: mbus_code_enum is NULL", __func__);
        return ret;
    }

    ret = ioctl(fd, VIDIOC_SUBDEV_ENUM_MBUS_CODE, mbus_code_enum);
    if (ret) {
        ALOGE("failed to ioctl: VIDIOC_SUBDEV_ENUM_MBUS_CODE");
        return ret;
    }

    return ret;
}
