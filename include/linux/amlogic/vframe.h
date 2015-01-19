/*
 * AMLOGIC Audio/Video streaming port driver.
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

#ifndef VFRAME_H
#define VFRAME_H

#include <linux/types.h>
#include <linux/amlogic/tvin/tvin.h>

#define VIDTYPE_PROGRESSIVE             0x0
#define VIDTYPE_INTERLACE_TOP           0x1
#define VIDTYPE_INTERLACE_BOTTOM        0x3
#define VIDTYPE_TYPEMASK                0x7
#define VIDTYPE_INTERLACE               0x1
#define VIDTYPE_INTERLACE_FIRST         0x8
#define VIDTYPE_MVC                     0x10
#define VIDTYPE_NO_VIDEO_ENABLE         0x20
#define VIDTYPE_VIU_422                 0x800
#define VIDTYPE_VIU_FIELD               0x1000
#define VIDTYPE_VIU_SINGLE_PLANE        0x2000
#define VIDTYPE_VIU_444                 0x4000
#define VIDTYPE_VIU_NV21                0x8000
#define VIDTYPE_VSCALE_DISABLE          0x10000
#define VIDTYPE_CANVAS_TOGGLE           0x20000
#define VIDTYPE_PRE_INTERLACE           0x40000

#define DISP_RATIO_FORCECONFIG          0x80000000
#define DISP_RATIO_FORCE_NORMALWIDE     0x40000000
#define DISP_RATIO_FORCE_FULL_STRETCH   0x20000000
#define DISP_RATIO_CTRL_MASK            0x00000003
#define DISP_RATIO_NO_KEEPRATIO         0x00000000
#define DISP_RATIO_KEEPRATIO            0x00000001
#define DISP_RATIO_PORTRAIT_MODE        0x00000004

#define DISP_RATIO_ASPECT_RATIO_MASK    0x0003ff00
#define DISP_RATIO_ASPECT_RATIO_BIT     8
#define DISP_RATIO_ASPECT_RATIO_MAX     0x3ff

typedef enum pixel_aspect_ratio_e {
        PIXEL_ASPECT_RATIO_1_1,
        PIXEL_ASPECT_RATIO_8_9,
        PIXEL_ASPECT_RATIO_16_15,
} pixel_aspect_ratio_t;

/*
 * If pixel_sum[21:0] == 0, then all Histogram information are invalid
 */
typedef struct vframe_hist_s
{
        unsigned int   luma_sum;
        unsigned int   chroma_sum;
        unsigned int   pixel_sum;  // [31:30] POW [21:0] PIXEL_SUM
        unsigned int   height;
        unsigned int   width;
        unsigned char  luma_max;
        unsigned char  luma_min;
        unsigned short gamma[64];
#ifdef AML_LOCAL_DIMMING
        unsigned int   ldim_max[100];
#endif
} vframe_hist_t;


/*
 * If bottom == 0 or right == 0, then all Blackbar information are invalid
 */
typedef struct vframe_bbar_s
{
        unsigned short top;
        unsigned short bottom;
        unsigned short left;
        unsigned short right;
} vfame_bbar_t;


/*
 * If vsin == 0, then all Measurement information are invalid
 */
typedef struct vframe_meas_s
{
        //float          frin;      // Frame Rate of Video Input in the unit of Hz
        unsigned int        vs_span_cnt;
        unsigned long long  vs_cnt;
        unsigned int        hs_cnt0;
        unsigned int        hs_cnt1;
        unsigned int        hs_cnt2;
        unsigned int        hs_cnt3;
        unsigned int vs_cycle;
        unsigned int vs_stamp;
} vframe_meas_t;

typedef struct vframe_view_s {
        int start_x;
        int start_y;
        unsigned int width;
        unsigned int height;
} vframe_view_t;

/* vframe properties */
typedef struct vframe_prop_s
{
        struct vframe_hist_s hist;
        struct vframe_bbar_s bbar;
        struct vframe_meas_s meas;
} vframe_prop_t;

typedef enum vframe_source_type_e {
        VFRAME_SOURCE_TYPE_OTHERS = 0,
        VFRAME_SOURCE_TYPE_TUNER,
        VFRAME_SOURCE_TYPE_CVBS,
        VFRAME_SOURCE_TYPE_COMP,
        VFRAME_SOURCE_TYPE_HDMI,
        VFRAME_SOURCE_TYPE_PPMGR,
        VFRAME_SOURCE_TYPE_OSD,
} vframe_source_type_t;

typedef enum vframe_source_mode_e {
        VFRAME_SOURCE_MODE_OTHERS = 0,
        VFRAME_SOURCE_MODE_PAL,
        VFRAME_SOURCE_MODE_NTSC,
        VFRAME_SOURCE_MODE_SECAM,
} vframe_source_mode_t;
typedef enum vframe_secam_phase_e {
        VFRAME_PHASE_DB = 0,
        VFRAME_PHASE_DR,
} vframe_secam_phase_t;
typedef struct vframe_s {
        u32 index;
        u32 type;
        u32 type_backup;
        u32 blend_mode;
        u32 duration;
        u32 duration_pulldown;
        u32 pts;
        u32 flag;

        u32 canvas0Addr;
        u32 canvas1Addr;

        u32 bufWidth;
        u32 width;
        u32 height;
        u32 ratio_control;

        u32 orientation;
        enum vframe_source_type_e source_type;
        enum vframe_secam_phase_e  phase;
        enum vframe_source_mode_e source_mode;
        tvin_sig_fmt_t sig_fmt;

        enum tvin_trans_fmt  trans_fmt;
        struct vframe_view_s left_eye;
        struct vframe_view_s right_eye;

        u32   mode_3d_enable ;

        /* vframe extension */
        int (*early_process_fun)(void* arg, struct vframe_s* vf);
        int (*process_fun)(void* arg, unsigned zoom_start_x_lines,
                        unsigned zoom_end_x_lines, unsigned zoom_start_y_lines, unsigned zoom_end_y_lines, struct vframe_s* vf);
        void* private_data;
#if 1
        /* vframe properties */
        struct vframe_prop_s prop;
#endif
        struct list_head list;
        struct tvafe_vga_parm_s vga_parm;
        /* pixel aspect ratio */
        enum pixel_aspect_ratio_e pixel_ratio;
        u32  frame_dirty;
} vframe_t;

#if 0
struct vframe_prop_s * vdin_get_vframe_prop(u32 index);
#endif
int get_curren_frame_para(int* top ,int* left , int* bottom, int* right);


#ifdef CONFIG_VSYNC_RDMA
int VSYNC_WR_MPEG_REG(unsigned long adr, unsigned long val);

int VSYNC_WR_MPEG_REG_BITS(unsigned long adr, unsigned long val, unsigned long start, unsigned long len);

unsigned long VSYNC_RD_MPEG_REG(unsigned long adr);
#else
#define VSYNC_WR_MPEG_REG(adr,val) WRITE_MPEG_REG(adr, val)
#define VSYNC_WR_MPEG_REG_BITS(adr, val, start, len)  WRITE_MPEG_REG_BITS(adr, val, start, len)
#define VSYNC_RD_MPEG_REG(adr) READ_MPEG_REG(adr)
#endif

#endif /* VFRAME_H */

