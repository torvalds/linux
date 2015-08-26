/*
 * Copyright (C) 2011-2015 Freescale Semiconductor, Inc. All Rights Reserved.
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */
#ifndef __LINUX_MXC_HDMI_CORE_H_
#define __LINUX_MXC_HDMI_CORE_H_

#include <video/mxc_edid.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

#define IRQ_DISABLE_SUCCEED	0
#define IRQ_DISABLE_FAIL	1

bool hdmi_check_overflow(void);

u8 hdmi_readb(unsigned int reg);
void hdmi_writeb(u8 value, unsigned int reg);
void hdmi_mask_writeb(u8 data, unsigned int addr, u8 shift, u8 mask);
unsigned int hdmi_read4(unsigned int reg);
void hdmi_write4(unsigned int value, unsigned int reg);

void hdmi_irq_init(void);
void hdmi_irq_enable(int irq);
unsigned int hdmi_irq_disable(int irq);

void hdmi_set_sample_rate(unsigned int rate);
void hdmi_set_dma_mode(unsigned int dma_running);
void hdmi_init_clk_regenerator(void);
void hdmi_clk_regenerator_update_pixel_clock(u32 pixclock);

void hdmi_set_edid_cfg(struct mxc_edid_cfg *cfg);
void hdmi_get_edid_cfg(struct mxc_edid_cfg *cfg);

extern int mxc_hdmi_ipu_id;
extern int mxc_hdmi_disp_id;

void hdmi_set_registered(int registered);
int hdmi_get_registered(void);
int mxc_hdmi_abort_stream(void);
int mxc_hdmi_register_audio(struct snd_pcm_substream *substream);
void mxc_hdmi_unregister_audio(struct snd_pcm_substream *substream);
unsigned int hdmi_set_cable_state(unsigned int state);
unsigned int hdmi_set_blank_state(unsigned int state);
int check_hdmi_state(void);

#endif
