#ifndef  _VIDEO_PROT_H
#define _VIDEO_PROT_H

#include <linux/kernel.h>
#include <plat/regops.h>
#include <mach/am_regs.h>
#include <linux/amlogic/amlog.h>
#include <linux/amlogic/amports/vframe.h>
#include <linux/amlogic/amports/canvas.h>

#define  CUGT           0
#define  CID_VALUE      161
#define	 CID_MODE       6
#define	 REQ_ONOFF_EN   0
#define  REQ_ON_MAX     0
#define	 REQ_OFF_MIN    0
#define  PAT_VAL        0x00000000
#define  PAT_START_PTR  1
#define  PAT_END_PTR    1
#define  HOLD_LINES     4
#define  LITTLE_ENDIAN  0

typedef struct {
    u32 status;
    u32 video_started;
    u32 viu_type;
    u32 power_down;
    u32 power_on;
    u32 use_prot;
    u32 disable_prot;
    u32 x_start;
    u32 y_start;
    u32 x_end;
    u32 y_end;
    u32 y_step;
    u32 pat_val;
    u32 prot2_canvas;
    u32 prot3_canvas;
    u32 is_4k2k;
    u32 angle;
    u32 angle_changed;
    u32 size_changed;
    u32 enable_layer;
    u32 src_vframe_width;
    u32 src_vframe_height;
    u32 src_vframe_ratio;
    u32 src_vframe_orientation;
} video_prot_t;

//extern void early_init_prot();
void video_prot_init(video_prot_t* video_prot, vframe_t *vf);
void video_prot_clear(video_prot_t* video_prot);
void video_prot_set_angle(video_prot_t* video_prot, u32 angle_orientation);
void video_prot_revert_vframe(video_prot_t* video_prot, vframe_t *vf);
void video_prot_set_canvas(vframe_t *vf);
void video_prot_gate_on(void);
void video_prot_gate_off(void);
u32 get_video_angle(void);
u32 get_prot_status(void);
void set_video_angle(u32 s_value);
void video_prot_axis (video_prot_t* video_prot, u32 hd_start, u32 hd_end, u32 vd_start, u32 vd_end);
#endif
