
#ifndef __DRV_HDMI_H__
#define __DRV_HDMI_H__

typedef struct
{
	__u8    hw_intf;        /* 0:iis  1:spdif 2:pcm */
	__u16	fs_between;     /* fs */
	__u32   sample_rate;    /*sample rate*/  
	__u8    clk_edge;       /* 0:*/
	__u8    ch0_en;         /* 1 */
	__u8    ch1_en;         /* 0 */
	__u8 	ch2_en;         /* 0 */
	__u8 	ch3_en;         /* 0 */
	__u8	word_length;    /* 32 */
	__u8    shift_ctl;      /* 0 */
	__u8    dir_ctl;        /* 0 */
	__u8    ws_pol;
	__u8    just_pol;
	__u8    channel_num;
}hdmi_audio_t;

typedef struct
{
    __s32 (*hdmi_audio_enable)(__u8 mode, __u8 channel);
    __s32 (*hdmi_set_audio_para)(hdmi_audio_t * audio_para);
}__audio_hdmi_func;

#endif
