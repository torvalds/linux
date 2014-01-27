/*
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */


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
	__u8    sample_bit;
}hdmi_audio_t;

typedef struct
{
    __s32 (*hdmi_audio_enable)(__u8 mode, __u8 channel);
    __s32 (*hdmi_set_audio_para)(hdmi_audio_t * audio_para);
}__audio_hdmi_func;

#endif
