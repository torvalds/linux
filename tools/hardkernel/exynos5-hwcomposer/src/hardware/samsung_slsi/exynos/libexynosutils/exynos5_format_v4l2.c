/*
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

/*
 * file exynos5_format_v4l2.c
 * author Sangwoo, Park(sw5771.park@samsung.com)
 * date 2011/06/02
 *
 * Revision History:
 * - 2010/06/03 : Sangwoo, Park(sw5771.park@samsung.com)
 * Initial version
 *
 * - 2011/12/07 : Hyeonmyeong Choi( hyeon.choi@samsung.com)
 * Add V4L2_PIX_FMT_YVU420M
 *
 * - 2012/03/06 : shinwon lee(shinwon.lee@samsung.com)
 * Merge to libexynosutils
 *
 */

#ifndef __EXYNOS5_FORMAT_V4L2_H__
#define __EXYNOS5_FORMAT_V4L2_H__

//---------------------------------------------------------//
// Include
//---------------------------------------------------------//
#include <hardware/hardware.h>
#if !defined(ANDROID)
#include <log.h>
#include <stddef.h>
#endif
#include "exynos_format.h"
#include "s5p_fimc_v4l2.h"
#if defined(ANDROID)
#include <utils/Log.h>
#endif
#include "videodev2.h"
#include "videodev2_exynos_media.h"

int HAL_PIXEL_FORMAT_2_V4L2_PIX(
    int hal_pixel_format)
{
    int v4l2_pixel_format = -1;

    switch (hal_pixel_format) {
    case HAL_PIXEL_FORMAT_RGBA_8888:
    case HAL_PIXEL_FORMAT_RGBX_8888:
        v4l2_pixel_format = V4L2_PIX_FMT_RGB32;
        break;

    case HAL_PIXEL_FORMAT_RGB_888:
        v4l2_pixel_format = V4L2_PIX_FMT_RGB24;
        break;

    case HAL_PIXEL_FORMAT_RGB_565:
        v4l2_pixel_format = V4L2_PIX_FMT_RGB565;
        break;

    case HAL_PIXEL_FORMAT_BGRA_8888:
        v4l2_pixel_format = V4L2_PIX_FMT_BGR32;
        break;

    case HAL_PIXEL_FORMAT_RGBA_5551:
        v4l2_pixel_format = V4L2_PIX_FMT_RGB555X;
        break;

    case HAL_PIXEL_FORMAT_RGBA_4444:
        v4l2_pixel_format = V4L2_PIX_FMT_RGB444;
        break;

    case HAL_PIXEL_FORMAT_EXYNOS_YV12:
        v4l2_pixel_format = V4L2_PIX_FMT_YVU420M;
        break;

    case HAL_PIXEL_FORMAT_YV12:
        v4l2_pixel_format = V4L2_PIX_FMT_YVU420;
        break;

    case HAL_PIXEL_FORMAT_YCbCr_420_P:
        v4l2_pixel_format = V4L2_PIX_FMT_YUV420M;
        break;

    case HAL_PIXEL_FORMAT_YCbCr_422_SP:
    case HAL_PIXEL_FORMAT_CUSTOM_YCbCr_422_SP:
        v4l2_pixel_format = V4L2_PIX_FMT_NV16;
        break;

    case HAL_PIXEL_FORMAT_YCbCr_420_SP:
    case HAL_PIXEL_FORMAT_CUSTOM_YCbCr_420_SP:
        v4l2_pixel_format = V4L2_PIX_FMT_NV12M;
        break;

    case HAL_PIXEL_FORMAT_YCbCr_422_I:
    case HAL_PIXEL_FORMAT_CUSTOM_YCbCr_422_I:
        v4l2_pixel_format = V4L2_PIX_FMT_YUYV;
        break;

    case HAL_PIXEL_FORMAT_YCbCr_422_P:
        v4l2_pixel_format = V4L2_PIX_FMT_YUV422P;
        break;

    case HAL_PIXEL_FORMAT_CbYCrY_422_I:
    case HAL_PIXEL_FORMAT_CUSTOM_CbYCrY_422_I:
        v4l2_pixel_format = V4L2_PIX_FMT_UYVY;
        break;

    case HAL_PIXEL_FORMAT_YCrCb_422_SP:
    case HAL_PIXEL_FORMAT_CUSTOM_YCrCb_422_SP:
        v4l2_pixel_format = V4L2_PIX_FMT_NV61;
        break;

    case HAL_PIXEL_FORMAT_EXYNOS_YCrCb_420_SP:
    case HAL_PIXEL_FORMAT_CUSTOM_YCrCb_420_SP:
    case HAL_PIXEL_FORMAT_YCrCb_420_SP:
        v4l2_pixel_format = V4L2_PIX_FMT_NV21M;
        break;

   case HAL_PIXEL_FORMAT_YCbCr_420_SP_TILED:
        v4l2_pixel_format = V4L2_PIX_FMT_NV12MT_16X16;
        break;

    case HAL_PIXEL_FORMAT_CUSTOM_YCbCr_420_SP_TILED:
        v4l2_pixel_format = V4L2_PIX_FMT_NV12MT_16X16;
        break;

   case HAL_PIXEL_FORMAT_CUSTOM_YCrCb_422_I:
        v4l2_pixel_format = V4L2_PIX_FMT_YVYU;
        break;

   case HAL_PIXEL_FORMAT_CUSTOM_CrYCbY_422_I:
        v4l2_pixel_format = V4L2_PIX_FMT_VYUY;
        break;

    default:
        ALOGE("%s::unmatched HAL_PIXEL_FORMAT color_space(0x%x)\n",
                __func__, hal_pixel_format);
        break;
    }

    return v4l2_pixel_format;
}

int V4L2_PIX_2_HAL_PIXEL_FORMAT(
    int v4l2_pixel_format)
{
    int hal_pixel_format = -1;

    switch (v4l2_pixel_format) {
    case V4L2_PIX_FMT_RGB32:
        hal_pixel_format = HAL_PIXEL_FORMAT_RGBA_8888;
        break;

    case V4L2_PIX_FMT_RGB24:
        hal_pixel_format = HAL_PIXEL_FORMAT_RGB_888;
        break;

    case V4L2_PIX_FMT_RGB565:
        hal_pixel_format = HAL_PIXEL_FORMAT_RGB_565;
        break;

    case V4L2_PIX_FMT_BGR32:
        hal_pixel_format = HAL_PIXEL_FORMAT_BGRA_8888;
        break;

    case V4L2_PIX_FMT_RGB555X:
        hal_pixel_format = HAL_PIXEL_FORMAT_RGBA_5551;
        break;

    case V4L2_PIX_FMT_RGB444:
        hal_pixel_format = HAL_PIXEL_FORMAT_RGBA_4444;
        break;

    case V4L2_PIX_FMT_YUV420:
    case V4L2_PIX_FMT_YUV420M:
        hal_pixel_format = HAL_PIXEL_FORMAT_YCbCr_420_P;
        break;

    case V4L2_PIX_FMT_YVU420:
        hal_pixel_format = HAL_PIXEL_FORMAT_YV12;
        break;

    case V4L2_PIX_FMT_YVU420M:
         hal_pixel_format = HAL_PIXEL_FORMAT_EXYNOS_YV12;
         break;

    case V4L2_PIX_FMT_NV16:
        hal_pixel_format = HAL_PIXEL_FORMAT_CUSTOM_YCbCr_422_SP;
        break;

    case V4L2_PIX_FMT_NV12:
        hal_pixel_format = HAL_PIXEL_FORMAT_YCbCr_420_SP;
        break;

    case V4L2_PIX_FMT_NV12M:
        hal_pixel_format = HAL_PIXEL_FORMAT_CUSTOM_YCbCr_420_SP;
        break;

    case V4L2_PIX_FMT_NV21M:
        hal_pixel_format = HAL_PIXEL_FORMAT_EXYNOS_YCrCb_420_SP;
        break;

    case V4L2_PIX_FMT_YUYV:
        hal_pixel_format = HAL_PIXEL_FORMAT_CUSTOM_YCbCr_422_I;
        break;

    case V4L2_PIX_FMT_YUV422P:
        hal_pixel_format = HAL_PIXEL_FORMAT_YCbCr_422_P;
        break;

    case V4L2_PIX_FMT_UYVY:
        hal_pixel_format = HAL_PIXEL_FORMAT_CUSTOM_CbYCrY_422_I;
        break;

    case V4L2_PIX_FMT_NV21:
        hal_pixel_format = HAL_PIXEL_FORMAT_CUSTOM_YCrCb_420_SP;
        break;
    case V4L2_PIX_FMT_NV12MT_16X16:

        hal_pixel_format = HAL_PIXEL_FORMAT_YCbCr_420_SP_TILED;
        break;

    case V4L2_PIX_FMT_NV61:
        hal_pixel_format = HAL_PIXEL_FORMAT_CUSTOM_YCrCb_422_SP;
        break;

    case V4L2_PIX_FMT_YVYU:
        hal_pixel_format = HAL_PIXEL_FORMAT_CUSTOM_YCrCb_422_I;
        break;

    case V4L2_PIX_FMT_VYUY:
        hal_pixel_format = HAL_PIXEL_FORMAT_CUSTOM_CrYCbY_422_I;
        break;

    default:
        ALOGE("%s::unmatched V4L2_PIX color_space(%d)\n",
                __func__, v4l2_pixel_format);
        break;
    }

    return hal_pixel_format;
}

int NUM_PLANES(int hal_pixel_format)
{
    switch(hal_pixel_format) {
    case HAL_PIXEL_FORMAT_EXYNOS_YV12:
    case HAL_PIXEL_FORMAT_YCbCr_420_P:
	return 3;
    case HAL_PIXEL_FORMAT_EXYNOS_YCrCb_420_SP:
    case HAL_PIXEL_FORMAT_YCrCb_420_SP:
    case HAL_PIXEL_FORMAT_YCbCr_420_SP:
    case HAL_PIXEL_FORMAT_CUSTOM_YCbCr_420_SP:
    case HAL_PIXEL_FORMAT_CUSTOM_YCrCb_420_SP:
    case HAL_PIXEL_FORMAT_CUSTOM_YCbCr_420_SP_TILED:
	return 2;
	break;
    case HAL_PIXEL_FORMAT_YCbCr_420_I:
    case HAL_PIXEL_FORMAT_CbYCrY_420_I:
    case HAL_PIXEL_FORMAT_YV12:
        return 1;
    default:
	return 1;
    }
}

unsigned int FRAME_SIZE(
    int hal_pixel_format,
    int width,
    int height)
{
    unsigned int frame_size = 0;
    unsigned int size       = 0;

    switch (hal_pixel_format) {
    // 16bpp
    case HAL_PIXEL_FORMAT_RGB_565:
    case HAL_PIXEL_FORMAT_RGBA_5551:
    case HAL_PIXEL_FORMAT_RGBA_4444:
        frame_size = GET_16BPP_FRAME_SIZE(width, height);
        break;

    // 24bpp
    case HAL_PIXEL_FORMAT_RGB_888:
        frame_size = GET_24BPP_FRAME_SIZE(width, height);
        break;

    // 32bpp
    case HAL_PIXEL_FORMAT_RGBA_8888:
    case HAL_PIXEL_FORMAT_BGRA_8888:
    case HAL_PIXEL_FORMAT_RGBX_8888:
        frame_size = GET_32BPP_FRAME_SIZE(width, height);
        break;

    // 12bpp
    case HAL_PIXEL_FORMAT_EXYNOS_YV12:
    case HAL_PIXEL_FORMAT_YCbCr_420_P:
    case HAL_PIXEL_FORMAT_YCbCr_420_I:
    case HAL_PIXEL_FORMAT_CbYCrY_420_I:
        size = ALIGN(width, 16) * ALIGN(height, 16);
        frame_size = size + 2 * ALIGN(width >> 1, 8) * ALIGN(height >> 1, 8);
        break;

    case HAL_PIXEL_FORMAT_YV12:
        size = ALIGN(width, 16) * height;
        frame_size = size + ALIGN(width / 2, 16) * height;
        break;

    case HAL_PIXEL_FORMAT_EXYNOS_YCrCb_420_SP:
    case HAL_PIXEL_FORMAT_YCrCb_420_SP:
    case HAL_PIXEL_FORMAT_YCbCr_420_SP:
    case HAL_PIXEL_FORMAT_CUSTOM_YCbCr_420_SP:
    case HAL_PIXEL_FORMAT_CUSTOM_YCrCb_420_SP:
        size = ALIGN(ALIGN(width, 16) * ALIGN(height, 16), 2048);
        frame_size = size + ALIGN(ALIGN(width, 16) * ALIGN(height >> 1, 8), 2048);
        break;

    case HAL_PIXEL_FORMAT_CUSTOM_YCbCr_420_SP_TILED:
        frame_size =   ALIGN_TO_8KB(ALIGN_TO_128B(width) * ALIGN_TO_32B(height))
                     + ALIGN_TO_8KB(ALIGN_TO_128B(width) * ALIGN_TO_32B(height >> 1));
        break;

    // 16bpp
    case HAL_PIXEL_FORMAT_YCbCr_422_SP:
    case HAL_PIXEL_FORMAT_YCbCr_422_I:
    case HAL_PIXEL_FORMAT_YCbCr_422_P:
    case HAL_PIXEL_FORMAT_CbYCrY_422_I:
    case HAL_PIXEL_FORMAT_YCrCb_422_SP:
    case HAL_PIXEL_FORMAT_CUSTOM_YCbCr_422_SP:
    case HAL_PIXEL_FORMAT_CUSTOM_YCrCb_422_SP:
    case HAL_PIXEL_FORMAT_CUSTOM_YCbCr_422_I:
    case HAL_PIXEL_FORMAT_CUSTOM_YCrCb_422_I:
    case HAL_PIXEL_FORMAT_CUSTOM_CbYCrY_422_I:
    case HAL_PIXEL_FORMAT_CUSTOM_CrYCbY_422_I:
        frame_size = GET_16BPP_FRAME_SIZE(width, height);
        break;

    default:
        ALOGD("%s::no matching source colorformat(0x%x), width(%d), h(%d) fail\n",
                __func__, hal_pixel_format, width, height);
        break;
    }

    return frame_size;
}

int PLANAR_FRAME_SIZE(int hal_pixel_format, int width, int height,
                        unsigned int *luma_size, unsigned int *chroma_size)
{
    unsigned int size = 0;

    switch(hal_pixel_format) {
    case HAL_PIXEL_FORMAT_EXYNOS_YV12:
    case HAL_PIXEL_FORMAT_YCbCr_420_P:
        size = width * height;
        *luma_size = size;
        *chroma_size = size >> 2;
        break;
    case HAL_PIXEL_FORMAT_EXYNOS_YCrCb_420_SP:
    case HAL_PIXEL_FORMAT_YCrCb_420_SP:
    case HAL_PIXEL_FORMAT_YCbCr_420_I:
    case HAL_PIXEL_FORMAT_CbYCrY_420_I:
    case HAL_PIXEL_FORMAT_YCbCr_420_SP:
    case HAL_PIXEL_FORMAT_CUSTOM_YCbCr_420_SP:
    case HAL_PIXEL_FORMAT_CUSTOM_YCrCb_420_SP:
        size = width * height;
        *luma_size = size;
        *chroma_size = (size >> 2) << 1;
        break;
    case HAL_PIXEL_FORMAT_CUSTOM_YCbCr_420_SP_TILED:
        *luma_size = ALIGN_TO_8KB(ALIGN_TO_128B(width) * ALIGN_TO_32B(height));
        *chroma_size = ALIGN_TO_8KB(ALIGN_TO_128B(width) *
                        ALIGN_TO_32B(height >> 1));
        break;
    case HAL_PIXEL_FORMAT_YV12:
        size = ALIGN(width, 16) * height;
        *luma_size = size + ALIGN(width / 2, 16) * height;
        break;
    default:
        *luma_size = FRAME_SIZE(hal_pixel_format, width, height);
        break;
    }

    return 0;
}


int V4L2_PIX_2_YUV_INFO(unsigned int v4l2_pixel_format, unsigned int * bpp, unsigned int * planes)
{
    switch (v4l2_pixel_format) {
    case V4L2_PIX_FMT_NV12:
        *bpp    = 12;
        *planes = 1;
        break;
    case V4L2_PIX_FMT_NV21:
    case V4L2_PIX_FMT_NV21X:
    case V4L2_PIX_FMT_NV12M:
    case V4L2_PIX_FMT_NV21M:
    case V4L2_PIX_FMT_NV12MT_16X16:
        *bpp    = 12;
        *planes = 2;
        break;
    case V4L2_PIX_FMT_YUV420:
    case V4L2_PIX_FMT_YUV420M:
    case V4L2_PIX_FMT_YVU420M:
    case V4L2_PIX_FMT_YVU420:
        *bpp    = 12;
        *planes = 3;
        break;
    case V4L2_PIX_FMT_YUYV:
    case V4L2_PIX_FMT_YVYU:
    case V4L2_PIX_FMT_UYVY:
    case V4L2_PIX_FMT_VYUY:
        *bpp    = 16;
        *planes = 1;
        break;
    case V4L2_PIX_FMT_NV16:
    case V4L2_PIX_FMT_NV61:
    case V4L2_PIX_FMT_NV16X:
    case V4L2_PIX_FMT_NV61X:
        *bpp    = 16;
        *planes = 2;
        break;
    case V4L2_PIX_FMT_YUV422P:
        *bpp    = 16;
        *planes = 3;
        break;
    default:
        return -1;
        break;
    }

    return 0;
}

unsigned int get_yuv_bpp(unsigned int v4l2_pixel_format)
{
    unsigned int bpp, planes;

    if (V4L2_PIX_2_YUV_INFO(v4l2_pixel_format, &bpp, &planes) < 0)
        bpp = -1;

    return bpp;
}

unsigned int get_yuv_planes(unsigned int v4l2_pixel_format)
{
    unsigned int bpp, planes;

    if (V4L2_PIX_2_YUV_INFO(v4l2_pixel_format, &bpp, &planes) < 0)
        planes = -1;

    return planes;
}
#endif
