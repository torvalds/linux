/* Copyright(c) 2011 Samsung Electronics Co, Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Alternatively, Licensed under the Apache License, Version 2.0 (the "License");
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

#ifndef _S5P_FIMC_H_
#define _S5P_FIMC_H_

#include "videodev2.h"
#include "videodev2_exynos_media.h"

/*
 * G E N E R A L S
 *
*/

/*
 * P I X E L   F O R M A T   G U I D E
 *
 * The 'x' means 'DO NOT CARE'
 * The '*' means 'FIMC SPECIFIC'
 * For some fimc formats, we couldn't find equivalent format in the V4L2 FOURCC.
 *
 * FIMC TYPE    PLANES  ORDER       V4L2_PIX_FMT
 * ---------------------------------------------------------
 * RGB565   x   x       V4L2_PIX_FMT_RGB565
 * RGB888   x   x       V4L2_PIX_FMT_RGB24
 * YUV420   2   LSB_CBCR    V4L2_PIX_FMT_NV12
 * YUV420   2   LSB_CRCB    V4L2_PIX_FMT_NV21
 * YUV420   2   MSB_CBCR    V4L2_PIX_FMT_NV21X*
 * YUV420   2   MSB_CRCB    V4L2_PIX_FMT_NV12X*
 * YUV420   3   x       V4L2_PIX_FMT_YUV420
 * YUV422   1   YCBYCR      V4L2_PIX_FMT_YUYV
 * YUV422   1   YCRYCB      V4L2_PIX_FMT_YVYU
 * YUV422   1   CBYCRY      V4L2_PIX_FMT_UYVY
 * YUV422   1   CRYCBY      V4L2_PIX_FMT_VYUY*
 * YUV422   2   LSB_CBCR    V4L2_PIX_FMT_NV16*
 * YUV422   2   LSB_CRCB    V4L2_PIX_FMT_NV61*
 * YUV422   2   MSB_CBCR    V4L2_PIX_FMT_NV16X*
 * YUV422   2   MSB_CRCB    V4L2_PIX_FMT_NV61X*
 * YUV422   3   x       V4L2_PIX_FMT_YUV422P
 *
*/

/*
 * V 4 L 2   F I M C   E X T E N S I O N S
 *
*/
#define V4L2_PIX_FMT_YVYU       v4l2_fourcc('Y', 'V', 'Y', 'U')

/* FOURCC for FIMC specific */
#define V4L2_PIX_FMT_NV12X      v4l2_fourcc('N', '1', '2', 'X')
#define V4L2_PIX_FMT_NV21X      v4l2_fourcc('N', '2', '1', 'X')
#define V4L2_PIX_FMT_VYUY       v4l2_fourcc('V', 'Y', 'U', 'Y')
#define V4L2_PIX_FMT_NV16       v4l2_fourcc('N', 'V', '1', '6')
#define V4L2_PIX_FMT_NV61       v4l2_fourcc('N', 'V', '6', '1')
#define V4L2_PIX_FMT_NV16X      v4l2_fourcc('N', '1', '6', 'X')
#define V4L2_PIX_FMT_NV61X      v4l2_fourcc('N', '6', '1', 'X')

/* CID extensions */
#define V4L2_CID_ROTATION       (V4L2_CID_PRIVATE_BASE + 0)
#define V4L2_CID_OVLY_MODE              (V4L2_CID_PRIVATE_BASE + 9)
#define V4L2_CID_GET_PHY_SRC_YADDR  (V4L2_CID_PRIVATE_BASE + 12)
#define V4L2_CID_GET_PHY_SRC_CADDR  (V4L2_CID_PRIVATE_BASE + 13)
#define V4L2_CID_RESERVED_MEM_BASE_ADDR (V4L2_CID_PRIVATE_BASE + 20)
#define V4L2_CID_FIMC_VERSION       (V4L2_CID_PRIVATE_BASE + 21)

/*
 * U S E R   D E F I N E D   T Y P E S
 *
*/
#define FIMC1_RESERVED_SIZE 32768

enum fimc_overlay_mode {
    FIMC_OVLY_NOT_FIXED       = 0x0,    /* Overlay mode isn't fixed. */
    FIMC_OVLY_FIFO            = 0x1,    /* Non-destructive Overlay with FIFO */
    FIMC_OVLY_DMA_AUTO        = 0x2,    /* Non-destructive Overlay with DMA */
    FIMC_OVLY_DMA_MANUAL      = 0x3,    /* Non-destructive Overlay with DMA */
    FIMC_OVLY_NONE_SINGLE_BUF = 0x4,    /* Destructive Overlay with DMA single destination buffer */
    FIMC_OVLY_NONE_MULTI_BUF  = 0x5,    /* Destructive Overlay with DMA multiple dstination buffer */
};

typedef unsigned int dma_addr_t;

struct fimc_buf {
    dma_addr_t  base[3];
    size_t      size[3];
    int         planes;
};

struct fimc_buffer {
    void    *virt_addr;
    void    *phys_addr;
    size_t  length;
};

struct yuv_fmt_list {
    const char      *name;
    const char      *desc;
    unsigned int    fmt;
    int             bpp;
    int             planes;
};

struct img_offset {
    int y_h;
    int y_v;
    int cb_h;
    int cb_v;
    int cr_h;
    int cr_v;
};

//------------ STRUCT ---------------------------------------------------------//

typedef struct
{
    unsigned int full_width;            // Source Image Full Width (Virtual screen size)
    unsigned int full_height;           // Source Image Full Height (Virtual screen size)
    unsigned int start_x;               // Source Image Start width offset
    unsigned int start_y;               // Source Image Start height offset
    unsigned int width;                 // Source Image Width
    unsigned int height;                // Source Image Height
    unsigned int buf_addr_phy_rgb_y;    // Base Address of the Source Image (RGB or Y): Physical Address
    unsigned int buf_addr_phy_cb;       // Base Address of the Source Image (CB Component) : Physical Address
    unsigned int buf_addr_phy_cr;       // Base Address of the Source Image (CR Component) : Physical Address
    unsigned int color_space;           // Color Space of the Source Image
    unsigned int planes;                // number of planes for the Image
} s5p_fimc_img_info;

typedef struct
{
    s5p_fimc_img_info   src;
    s5p_fimc_img_info   dst;
} s5p_fimc_params_t;

typedef struct _s5p_fimc_t {
    int                 dev_fd;
    struct fimc_buffer  out_buf;

    s5p_fimc_params_t   params;

    int                 use_ext_out_mem;
} s5p_fimc_t;

#endif
