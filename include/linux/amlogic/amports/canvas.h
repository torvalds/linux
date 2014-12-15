/*
 * AMLOGIC Canvas management driver.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the named License,
 * or any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA
 *
 * Author:  Tim Yao <timyao@amlogic.com>
 *
 */

#ifndef CANVAS_H
#define CANVAS_H

#include <linux/types.h>
#include <linux/kobject.h>

#include <mach/cpu.h>

typedef struct {
    struct kobject kobj;
    ulong addr;
    u32 width;
    u32 height;
    u32 wrap;
    u32 blkmode;
} canvas_t;

#define AMVDEC_ALL_CANVAS_INDEX 0x0
#define AMVDEC_ALL_CANVAS_RANGE_0 0xb  // vc1/real/mpeg12/mpeg4/ jpeg logo / h264
#define AMVDEC_ALL_CANVAS_RANGE_1 0x25 //mjpeg

#define AMVDEC_H264MVC_CANVAS_INDEX 0x78
#define AMVDEC_H264MVC_CANVAS_MAX 0xbf

#define AMVDEC_H264_4K2K_CANVAS_INDEX 0x78
#define AMVDEC_H264_4K2K_CANVAS_MAX 0xbf

#define AMVDEC_H264_CANVAS_INDEX 0x80
#define AMVDEC_H264_CANVAS_MAX 0xbf

//jpeg dec
#define JPEGDEC_CANVAS_INDEX   0//0x18//0x3a
#define JPEGDEC_CANVAS_MAX_INDEX 5//0x1d//0x3f

#define OSD1_CANVAS_INDEX 0x40
#define OSD2_CANVAS_INDEX 0x43
#define OSD3_CANVAS_INDEX 0x41
#define OSD4_CANVAS_INDEX 0x42
#define ALLOC_CANVAS_INDEX  0x44

#define FREESCALE_CANVAS_INDEX 0x50   //for osd&video scale use
#define MAX_FREESCALE_CANVAS_INDEX 0x55

#define VM_CANVAS_INDEX 0x50   //vm
#define VM_CANVAS_MAX_INDEX 0x5f

#define DISPLAY_CANVAS_BASE_INDEX   0x60
#define DISPLAY_CANVAS_MAX_INDEX    0x65

/*do not define both CONFIG_VSYNC_RDMA and CONFIG_AM_VIDEO2 */
#ifdef CONFIG_VSYNC_RDMA
#define DISPLAY_CANVAS_BASE_INDEX2   0x10
#define DISPLAY_CANVAS_MAX_INDEX2    0x15
#endif

#ifdef CONFIG_AM_VIDEO2
#define DISPLAY2_CANVAS_BASE_INDEX   0x1a
#define DISPLAY2_CANVAS_MAX_INDEX    0x1f
#endif

/*here ppmgr share the same canvas with deinterlace and mipi driver for m6*/
#define PPMGR_CANVAS_INDEX 0x70
#define PPMGR_DOUBLE_CANVAS_INDEX 0x74  //for double canvas use
#define PPMGR_DEINTERLACE_BUF_CANVAS 0x77   /*for progressive mjpeg use*/

#define PPMGR_DEINTERLACE_BUF_NV21_CANVAS 0x7a   /*for progressive mjpeg (nv21 output)use*/

#define DI_USE_FIXED_CANVAS_IDX
#ifdef DI_USE_FIXED_CANVAS_IDX
#define DI_PRE_MEM_NR_CANVAS_IDX        0x70
#define DI_PRE_CHAN2_NR_CANVAS_IDX      0x71
#define DI_PRE_WR_NR_CANVAS_IDX         0x72
#define DI_PRE_WR_MTN_CANVAS_IDX        0x73
//NEW DI
#define DI_CONTPRD_CANVAS_IDX           0x74
#define DI_CONTP2RD_CANVAS_IDX           0x75
#define DI_CONTWR_CANVAS_IDX            0x76
//DI POST, share with DISPLAY
#define DI_POST_BUF0_CANVAS_IDX         0x66
#define DI_POST_BUF1_CANVAS_IDX         0x67
#define DI_POST_MTNCRD_CANVAS_IDX       0x68
#define DI_POST_MTNPRD_CANVAS_IDX       0x69

#ifdef CONFIG_VSYNC_RDMA
#define DI_POST_BUF0_CANVAS_IDX2         0x6a
#define DI_POST_BUF1_CANVAS_IDX2         0x6b
#define DI_POST_MTNCRD_CANVAS_IDX2       0x6c
#define DI_POST_MTNPRD_CANVAS_IDX2       0x6d
#endif

#else
#define DEINTERLACE_CANVAS_BASE_INDEX	0x70
#define DEINTERLACE_CANVAS_MAX_INDEX	0x7f
#endif

#ifdef CONFIG_GE2D_KEEP_FRAME
#define DISPLAY_CANVAS_YDUP_INDEX 0x6e
#define DISPLAY_CANVAS_UDUP_INDEX 0x6f
#endif

#define AMVIDEOCAP_CANVAS_INDEX 0x6e

#define MIPI_CANVAS_INDEX 0x70
#define MIPI_CANVAS_MAX_INDEX 0x7f

//tvin vdin: 0x18-0x3B
#define VDIN_CANVAS_INDEX              0x26
#define VDIN_CANVAS_MAX_INDEX          0x3B

#define CAMERA_USER_CANVAS_INDEX             0x4e
#define CAMERA_USER_CANVAS_MAX_INDEX     0x5f

#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
#define AMLVIDEO2_RES_CANVAS 0xD8
#define AMLVIDEO2_MAX_RES_CANVAS 0xE3
#elif MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6TV
#define AMLVIDEO2_RES_CANVAS 0xD8
#define AMLVIDEO2_MAX_RES_CANVAS 0xDA
#else
#define AMLVIDEO2_RES_CANVAS 0x3c
#define AMLVIDEO2_MAX_RES_CANVAS 0x3E
#endif

#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
#define AMVENC_CANVAS_INDEX 0xE4
#define AMVENC_CANVAS_MAX_INDEX 0xEC

#define D2D3_CANVAS_DPG_INDEX      0xED
#define D2D3_CANVAS_DBR_INDEX      0xEE
#elif MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6TV
#define AMVENC_CANVAS_INDEX 0xDB
#define AMVENC_CANVAS_MAX_INDEX 0xE3

#define D2D3_CANVAS_DPG_INDEX      0xE4
#define D2D3_CANVAS_DBR_INDEX      0xE5
#endif

extern void canvas_config(u32 index, ulong addr, u32 width,
                          u32 height, u32 wrap, u32 blkmode);

extern void canvas_read(u32 index, canvas_t *p);

extern void canvas_copy(unsigned src, unsigned dst);

extern void canvas_update_addr(u32 index, u32 addr);

extern unsigned int canvas_get_addr(u32 index);

#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
// TODO: move to register headers
#define CANVAS_ADDR_NOWRAP      0x00
#define CANVAS_ADDR_WRAPX       0x01
#define CANVAS_ADDR_WRAPY       0x02
#define CANVAS_BLKMODE_MASK     3
#define CANVAS_BLKMODE_BIT      24
#define CANVAS_BLKMODE_LINEAR   0x00
#define CANVAS_BLKMODE_32X32    0x01
#define CANVAS_BLKMODE_64X32    0x02

#endif


#endif /* CANVAS_H */
