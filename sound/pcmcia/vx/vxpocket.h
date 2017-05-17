/*
 * Driver for Digigram VXpocket soundcards
 *
 * Copyright (c) 2002 by Takashi Iwai <tiwai@suse.de>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#ifndef __VXPOCKET_H
#define __VXPOCKET_H

#include <sound/vx_core.h>

#include <pcmcia/cistpl.h>
#include <pcmcia/ds.h>

struct snd_vxpocket {

	struct vx_core core;

	unsigned long port;

	int mic_level;	/* analog mic level (or boost) */

	unsigned int regCDSP;	/* current CDSP register */
	unsigned int regDIALOG;	/* current DIALOG register */

	int index;	/* card index */

	/* pcmcia stuff */
	struct pcmcia_device	*p_dev;
};

#define to_vxpocket(x)	container_of(x, struct snd_vxpocket, core)

extern struct snd_vx_ops snd_vxpocket_ops;

void vx_set_mic_boost(struct vx_core *chip, int boost);
void vx_set_mic_level(struct vx_core *chip, int level);

int vxp_add_mic_controls(struct vx_core *chip);

/* Constants used to access the CDSP register (0x08). */
#define CDSP_MAGIC	0xA7	/* magic value (for read) */
/* for write */
#define VXP_CDSP_CLOCKIN_SEL_MASK	0x80	/* 0 (internal), 1 (AES/EBU) */
#define VXP_CDSP_DATAIN_SEL_MASK	0x40	/* 0 (analog), 1 (UER) */
#define VXP_CDSP_SMPTE_SEL_MASK		0x20
#define VXP_CDSP_RESERVED_MASK		0x10
#define VXP_CDSP_MIC_SEL_MASK		0x08
#define VXP_CDSP_VALID_IRQ_MASK		0x04
#define VXP_CDSP_CODEC_RESET_MASK	0x02
#define VXP_CDSP_DSP_RESET_MASK		0x01
/* VXPOCKET 240/440 */
#define P24_CDSP_MICS_SEL_MASK		0x18
#define P24_CDSP_MIC20_SEL_MASK		0x10
#define P24_CDSP_MIC38_SEL_MASK		0x08

/* Constants used to access the MEMIRQ register (0x0C). */
#define P44_MEMIRQ_MASTER_SLAVE_SEL_MASK 0x08
#define P44_MEMIRQ_SYNCED_ALONE_SEL_MASK 0x04
#define P44_MEMIRQ_WCLK_OUT_IN_SEL_MASK  0x02 /* Not used */
#define P44_MEMIRQ_WCLK_UER_SEL_MASK     0x01 /* Not used */

/* Micro levels (0x0C) */

/* Constants used to access the DIALOG register (0x0D). */
#define VXP_DLG_XILINX_REPROG_MASK	0x80	/* W */
#define VXP_DLG_DATA_XICOR_MASK		0x80	/* R */
#define VXP_DLG_RESERVED4_0_MASK	0x40
#define VXP_DLG_RESERVED2_0_MASK	0x20
#define VXP_DLG_RESERVED1_0_MASK	0x10
#define VXP_DLG_DMAWRITE_SEL_MASK	0x08	/* W */
#define VXP_DLG_DMAREAD_SEL_MASK	0x04	/* W */
#define VXP_DLG_MEMIRQ_MASK		0x02	/* R */
#define VXP_DLG_DMA16_SEL_MASK		0x02	/* W */
#define VXP_DLG_ACK_MEMIRQ_MASK		0x01	/* R/W */


#endif /* __VXPOCKET_H */
