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

#ifndef _EXYNOS_FORMAT_H_
#define _EXYNOS_FORMAT_H_

#ifdef __cplusplus
extern "C" {
#endif

enum {
    HAL_PIXEL_FORMAT_YCbCr_422_P                = 0x100,
    HAL_PIXEL_FORMAT_YCbCr_420_P                = 0x101,
    HAL_PIXEL_FORMAT_YCbCr_420_I                = 0x102,
    HAL_PIXEL_FORMAT_CbYCrY_422_I               = 0x103,
    HAL_PIXEL_FORMAT_CbYCrY_420_I               = 0x104,
    HAL_PIXEL_FORMAT_YCbCr_420_SP               = 0x105,
    HAL_PIXEL_FORMAT_YCrCb_422_SP               = 0x106,
    HAL_PIXEL_FORMAT_YCbCr_420_SP_TILED         = 0x107,
    HAL_PIXEL_FORMAT_ARGB888                    = 0x108,
    // support custom format for zero copy
    HAL_PIXEL_FORMAT_CUSTOM_YCbCr_420_SP        = 0x110,
    HAL_PIXEL_FORMAT_CUSTOM_YCrCb_420_SP        = 0x111,
    HAL_PIXEL_FORMAT_CUSTOM_YCbCr_420_SP_TILED  = 0x112,
    HAL_PIXEL_FORMAT_CUSTOM_YCbCr_422_SP        = 0x113,
    HAL_PIXEL_FORMAT_CUSTOM_YCrCb_422_SP        = 0x114,
    HAL_PIXEL_FORMAT_CUSTOM_YCbCr_422_I         = 0x115,
    HAL_PIXEL_FORMAT_CUSTOM_YCrCb_422_I         = 0x116,
    HAL_PIXEL_FORMAT_CUSTOM_CbYCrY_422_I        = 0x117,
    HAL_PIXEL_FORMAT_CUSTOM_CrYCbY_422_I        = 0x118,
    HAL_PIXEL_FORMAT_CUSTOM_CbYCr_422_I         = 0x11B,

    HAL_PIXEL_FORMAT_EXYNOS_YV12                = 0x11C,
    HAL_PIXEL_FORMAT_EXYNOS_YCrCb_420_SP        = 0x11D,
    HAL_PIXEL_FORMAT_CUSTOM_MAX
};

struct ADDRS {
    unsigned int addr_y;
    unsigned int addr_cbcr;
    unsigned int buf_idx;
    unsigned int reserved;
};

/* 12  Y/CbCr 4:2:0 64x32 macroblocks */
#define V4L2_PIX_FMT_NV12T    v4l2_fourcc('T', 'V', '1', '2')

#define ALIGN_UP(x, a)    (((x) + (a) - 1) & ~((a) - 1))
#define ALIGN_DOWN(x, a)  ((x) - (x % a))
#define ALIGN(x, a)       ALIGN_UP(x, a)
#define ALIGN_TO_32B(x)   ((((x) + (1 <<  5) - 1) >>  5) <<  5)
#define ALIGN_TO_128B(x)  ((((x) + (1 <<  7) - 1) >>  7) <<  7)
#define ALIGN_TO_8KB(x)   ((((x) + (1 << 13) - 1) >> 13) << 13)

#define GET_32BPP_FRAME_SIZE(w, h)  (((w) * (h)) << 2)
#define GET_24BPP_FRAME_SIZE(w, h)  (((w) * (h)) * 3)
#define GET_16BPP_FRAME_SIZE(w, h)  (((w) * (h)) << 1)

/*
 * Convert hal_pixel_format to v4l2_pixel_format.
 *
 * @param hal_pixel_format
 *   hal_pixel_format[in]
 *
 * @return
 *   v4l2_pixel_format
 */
int HAL_PIXEL_FORMAT_2_V4L2_PIX(
    int hal_pixel_format);

/*
 * Convert v4l2_pixel_format to hal_pixel_format.
 *
 * @param v4l2_pixel_format
 *   v4l2_pixel_format[in]
 *
 * @return
 *   hal_pixel_format
 */
int V4L2_PIX_2_HAL_PIXEL_FORMAT(
    int v4l2_pixel_format);

/*
 * Get frame_size of hal_pixel_format.
 *
 * @param hal_pixel_format
 *   hal_pixel_format[in]
 *
 * @param width
 *   width[in]
 *
 * @param height
 *   height[in]
 *
 * @return
 *   frame_size
 */
unsigned int FRAME_SIZE(
    int hal_pixel_format,
    int width,
    int height);

int PLANAR_FRAME_SIZE(
    int hal_pixel_format,
    int width,
    int height, 
    unsigned int *luma_size,
    unsigned int *chroma_size);

int NUM_PLANES(int hal_pixel_format);


/*
 * Get bpp and plane of v4l2_pixel_format.
 *
 * @param v4l2_pixel_format
 *   v4l2_pixel_format[in]
 *
 * @param bpp
 *   address of bpp[out]
 *
 * @param planes
 *   address of planes[out]
 *
 * @return
 *   error code
 */
int V4L2_PIX_2_YUV_INFO(
    unsigned int  v4l2_pixel_format,
    unsigned int *bpp,
    unsigned int *planes);

/*
 * Get bpp of v4l2_pixel_format.
 *
 * @param v4l2_pixel_format
 *   v4l2_pixel_format[in]
 *
 * @return
 *   bpp
 */
unsigned int get_yuv_bpp(
    unsigned int v4l2_pixel_format);

/*
 * Get plane of v4l2_pixel_format.
 *
 * @param v4l2_pixel_format
 *   v4l2_pixel_format[in]
 *
 * @return
 *   num of plane
 */
unsigned int get_yuv_planes(
    unsigned int v4l2_pixel_format);

#ifdef __cplusplus
}
#endif

#endif
