/*
 * Driver for Sound Core PDAudioCF soundcard
 *
 * Copyright (c) 2003 by Jaroslav Kysela <perex@perex.cz>
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

#include <sound/core.h>
#include "pdaudiocf.h"
#include <sound/initval.h>
#include <asm/irq_regs.h>

/*
 *
 */
irqreturn_t pdacf_interrupt(int irq, void *dev)
{
	struct snd_pdacf *chip = dev;
	unsigned short stat;

	if ((chip->chip_status & (PDAUDIOCF_STAT_IS_STALE|
				  PDAUDIOCF_STAT_IS_CONFIGURED|
				  PDAUDIOCF_STAT_IS_SUSPENDED)) != PDAUDIOCF_STAT_IS_CONFIGURED)
		return IRQ_HANDLED;	/* IRQ_NONE here? */

	stat = inw(chip->port + PDAUDIOCF_REG_ISR);
	if (stat & (PDAUDIOCF_IRQLVL|PDAUDIOCF_IRQOVR)) {
		if (stat & PDAUDIOCF_IRQOVR)	/* should never happen */
			snd_printk(KERN_ERR "PDAUDIOCF SRAM buffer overrun detected!\n");
		if (chip->pcm_substream)
			tasklet_schedule(&chip->tq);
		if (!(stat & PDAUDIOCF_IRQAKM))
			stat |= PDAUDIOCF_IRQAKM;	/* check rate */
	}
	if (get_irq_regs() != NULL)
		snd_ak4117_check_rate_and_errors(chip->ak4117, 0);
	return IRQ_HANDLED;
}

static inline void pdacf_transfer_mono16(u16 *dst, u16 xor, unsigned int size, unsigned long rdp_port)
{
	while (size-- > 0) {
		*dst++ = inw(rdp_port) ^ xor;
		inw(rdp_port);
	}
}

static inline void pdacf_transfer_mono32(u32 *dst, u32 xor, unsigned int size, unsigned long rdp_port)
{
	register u16 val1, val2;

	while (size-- > 0) {
		val1 = inw(rdp_port);
		val2 = inw(rdp_port);
		inw(rdp_port);
		*dst++ = ((((u32)val2 & 0xff) << 24) | ((u32)val1 << 8)) ^ xor;
	}
}

static inline void pdacf_transfer_stereo16(u16 *dst, u16 xor, unsigned int size, unsigned long rdp_port)
{
	while (size-- > 0) {
		*dst++ = inw(rdp_port) ^ xor;
		*dst++ = inw(rdp_port) ^ xor;
	}
}

static inline void pdacf_transfer_stereo32(u32 *dst, u32 xor, unsigned int size, unsigned long rdp_port)
{
	register u16 val1, val2, val3;

	while (size-- > 0) {
		val1 = inw(rdp_port);
		val2 = inw(rdp_port);
		val3 = inw(rdp_port);
		*dst++ = ((((u32)val2 & 0xff) << 24) | ((u32)val1 << 8)) ^ xor;
		*dst++ = (((u32)val3 << 16) | (val2 & 0xff00)) ^ xor;
	}
}

static inline void pdacf_transfer_mono16sw(u16 *dst, u16 xor, unsigned int size, unsigned long rdp_port)
{
	while (size-- > 0) {
		*dst++ = swab16(inw(rdp_port) ^ xor);
		inw(rdp_port);
	}
}

static inline void pdacf_transfer_mono32sw(u32 *dst, u32 xor, unsigned int size, unsigned long rdp_port)
{
	register u16 val1, val2;

	while (size-- > 0) {
		val1 = inw(rdp_port);
		val2 = inw(rdp_port);
		inw(rdp_port);
		*dst++ = swab32((((val2 & 0xff) << 24) | ((u32)val1 << 8)) ^ xor);
	}
}

static inline void pdacf_transfer_stereo16sw(u16 *dst, u16 xor, unsigned int size, unsigned long rdp_port)
{
	while (size-- > 0) {
		*dst++ = swab16(inw(rdp_port) ^ xor);
		*dst++ = swab16(inw(rdp_port) ^ xor);
	}
}

static inline void pdacf_transfer_stereo32sw(u32 *dst, u32 xor, unsigned int size, unsigned long rdp_port)
{
	register u16 val1, val2, val3;

	while (size-- > 0) {
		val1 = inw(rdp_port);
		val2 = inw(rdp_port);
		val3 = inw(rdp_port);
		*dst++ = swab32((((val2 & 0xff) << 24) | ((u32)val1 << 8)) ^ xor);
		*dst++ = swab32((((u32)val3 << 16) | (val2 & 0xff00)) ^ xor);
	}
}

static inline void pdacf_transfer_mono24le(u8 *dst, u16 xor, unsigned int size, unsigned long rdp_port)
{
	register u16 val1, val2;
	register u32 xval1;

	while (size-- > 0) {
		val1 = inw(rdp_port);
		val2 = inw(rdp_port);
		inw(rdp_port);
		xval1 = (((val2 & 0xff) << 8) | (val1 << 16)) ^ xor;
		*dst++ = (u8)(xval1 >> 8);
		*dst++ = (u8)(xval1 >> 16);
		*dst++ = (u8)(xval1 >> 24);
	}
}

static inline void pdacf_transfer_mono24be(u8 *dst, u16 xor, unsigned int size, unsigned long rdp_port)
{
	register u16 val1, val2;
	register u32 xval1;

	while (size-- > 0) {
		val1 = inw(rdp_port);
		val2 = inw(rdp_port);
		inw(rdp_port);
		xval1 = (((val2 & 0xff) << 8) | (val1 << 16)) ^ xor;
		*dst++ = (u8)(xval1 >> 24);
		*dst++ = (u8)(xval1 >> 16);
		*dst++ = (u8)(xval1 >> 8);
	}
}

static inline void pdacf_transfer_stereo24le(u8 *dst, u32 xor, unsigned int size, unsigned long rdp_port)
{
	register u16 val1, val2, val3;
	register u32 xval1, xval2;

	while (size-- > 0) {
		val1 = inw(rdp_port);
		val2 = inw(rdp_port);
		val3 = inw(rdp_port);
		xval1 = ((((u32)val2 & 0xff) << 24) | ((u32)val1 << 8)) ^ xor;
		xval2 = (((u32)val3 << 16) | (val2 & 0xff00)) ^ xor;
		*dst++ = (u8)(xval1 >> 8);
		*dst++ = (u8)(xval1 >> 16);
		*dst++ = (u8)(xval1 >> 24);
		*dst++ = (u8)(xval2 >> 8);
		*dst++ = (u8)(xval2 >> 16);
		*dst++ = (u8)(xval2 >> 24);
	}
}

static inline void pdacf_transfer_stereo24be(u8 *dst, u32 xor, unsigned int size, unsigned long rdp_port)
{
	register u16 val1, val2, val3;
	register u32 xval1, xval2;

	while (size-- > 0) {
		val1 = inw(rdp_port);
		val2 = inw(rdp_port);
		val3 = inw(rdp_port);
		xval1 = ((((u32)val2 & 0xff) << 24) | ((u32)val1 << 8)) ^ xor;
		xval2 = (((u32)val3 << 16) | (val2 & 0xff00)) ^ xor;
		*dst++ = (u8)(xval1 >> 24);
		*dst++ = (u8)(xval1 >> 16);
		*dst++ = (u8)(xval1 >> 8);
		*dst++ = (u8)(xval2 >> 24);
		*dst++ = (u8)(xval2 >> 16);
		*dst++ = (u8)(xval2 >> 8);
	}
}

static void pdacf_transfer(struct snd_pdacf *chip, unsigned int size, unsigned int off)
{
	unsigned long rdp_port = chip->port + PDAUDIOCF_REG_MD;
	unsigned int xor = chip->pcm_xor;

	if (chip->pcm_sample == 3) {
		if (chip->pcm_little) {
			if (chip->pcm_channels == 1) {
				pdacf_transfer_mono24le((char *)chip->pcm_area + (off * 3), xor, size, rdp_port);
			} else {
				pdacf_transfer_stereo24le((char *)chip->pcm_area + (off * 6), xor, size, rdp_port);
			}
		} else {
			if (chip->pcm_channels == 1) {
				pdacf_transfer_mono24be((char *)chip->pcm_area + (off * 3), xor, size, rdp_port);
			} else {
				pdacf_transfer_stereo24be((char *)chip->pcm_area + (off * 6), xor, size, rdp_port);
			}			
		}
		return;
	}
	if (chip->pcm_swab == 0) {
		if (chip->pcm_channels == 1) {
			if (chip->pcm_frame == 2) {
				pdacf_transfer_mono16((u16 *)chip->pcm_area + off, xor, size, rdp_port);
			} else {
				pdacf_transfer_mono32((u32 *)chip->pcm_area + off, xor, size, rdp_port);
			}
		} else {
			if (chip->pcm_frame == 2) {
				pdacf_transfer_stereo16((u16 *)chip->pcm_area + (off * 2), xor, size, rdp_port);
			} else {
				pdacf_transfer_stereo32((u32 *)chip->pcm_area + (off * 2), xor, size, rdp_port);
			}
		}
	} else {
		if (chip->pcm_channels == 1) {
			if (chip->pcm_frame == 2) {
				pdacf_transfer_mono16sw((u16 *)chip->pcm_area + off, xor, size, rdp_port);
			} else {
				pdacf_transfer_mono32sw((u32 *)chip->pcm_area + off, xor, size, rdp_port);
			}
		} else {
			if (chip->pcm_frame == 2) {
				pdacf_transfer_stereo16sw((u16 *)chip->pcm_area + (off * 2), xor, size, rdp_port);
			} else {
				pdacf_transfer_stereo32sw((u32 *)chip->pcm_area + (off * 2), xor, size, rdp_port);
			}
		}
	}
}

void pdacf_tasklet(unsigned long private_data)
{
	struct snd_pdacf *chip = (struct snd_pdacf *) private_data;
	int size, off, cont, rdp, wdp;

	if ((chip->chip_status & (PDAUDIOCF_STAT_IS_STALE|PDAUDIOCF_STAT_IS_CONFIGURED)) != PDAUDIOCF_STAT_IS_CONFIGURED)
		return;
	
	if (chip->pcm_substream == NULL || chip->pcm_substream->runtime == NULL || !snd_pcm_running(chip->pcm_substream))
		return;

	rdp = inw(chip->port + PDAUDIOCF_REG_RDP);
	wdp = inw(chip->port + PDAUDIOCF_REG_WDP);
	/* printk(KERN_DEBUG "TASKLET: rdp = %x, wdp = %x\n", rdp, wdp); */
	size = wdp - rdp;
	if (size < 0)
		size += 0x10000;
	if (size == 0)
		size = 0x10000;
	size /= chip->pcm_frame;
	if (size > 64)
		size -= 32;

#if 0
	chip->pcm_hwptr += size;
	chip->pcm_hwptr %= chip->pcm_size;
	chip->pcm_tdone += size;
	if (chip->pcm_frame == 2) {
		unsigned long rdp_port = chip->port + PDAUDIOCF_REG_MD;
		while (size-- > 0) {
			inw(rdp_port);
			inw(rdp_port);
		}
	} else {
		unsigned long rdp_port = chip->port + PDAUDIOCF_REG_MD;
		while (size-- > 0) {
			inw(rdp_port);
			inw(rdp_port);
			inw(rdp_port);
		}
	}
#else
	off = chip->pcm_hwptr + chip->pcm_tdone;
	off %= chip->pcm_size;
	chip->pcm_tdone += size;
	while (size > 0) {
		cont = chip->pcm_size - off;
		if (cont > size)
			cont = size;
		pdacf_transfer(chip, cont, off);
		off += cont;
		off %= chip->pcm_size;
		size -= cont;
	}
#endif
	spin_lock(&chip->reg_lock);
	while (chip->pcm_tdone >= chip->pcm_period) {
		chip->pcm_hwptr += chip->pcm_period;
		chip->pcm_hwptr %= chip->pcm_size;
		chip->pcm_tdone -= chip->pcm_period;
		spin_unlock(&chip->reg_lock);
		snd_pcm_period_elapsed(chip->pcm_substream);
		spin_lock(&chip->reg_lock);
	}
	spin_unlock(&chip->reg_lock);
	/* printk(KERN_DEBUG "TASKLET: end\n"); */
}
