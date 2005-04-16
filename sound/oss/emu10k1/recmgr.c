/*
 **********************************************************************
 *     recmgr.c -- Recording manager for emu10k1 driver
 *     Copyright 1999, 2000 Creative Labs, Inc.
 *
 **********************************************************************
 *
 *     Date                 Author          Summary of changes
 *     ----                 ------          ------------------
 *     October 20, 1999     Bertrand Lee    base code release
 *
 **********************************************************************
 *
 *     This program is free software; you can redistribute it and/or
 *     modify it under the terms of the GNU General Public License as
 *     published by the Free Software Foundation; either version 2 of
 *     the License, or (at your option) any later version.
 *
 *     This program is distributed in the hope that it will be useful,
 *     but WITHOUT ANY WARRANTY; without even the implied warranty of
 *     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *     GNU General Public License for more details.
 *
 *     You should have received a copy of the GNU General Public
 *     License along with this program; if not, write to the Free
 *     Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139,
 *     USA.
 *
 **********************************************************************
 */

#include <asm/delay.h>
#include "8010.h"
#include "recmgr.h"

void emu10k1_reset_record(struct emu10k1_card *card, struct wavein_buffer *buffer)
{
	DPF(2, "emu10k1_reset_record()\n");

	sblive_writeptr(card, buffer->sizereg, 0, ADCBS_BUFSIZE_NONE);

	sblive_writeptr(card, buffer->sizereg, 0, buffer->sizeregval);	

	while (sblive_readptr(card, buffer->idxreg, 0))
		udelay(5);
}

void emu10k1_start_record(struct emu10k1_card *card, struct wavein_buffer *buffer)
{
	DPF(2, "emu10k1_start_record()\n");

	if (buffer->adcctl)
		sblive_writeptr(card, ADCCR, 0, buffer->adcctl);
}

void emu10k1_stop_record(struct emu10k1_card *card, struct wavein_buffer *buffer)
{
	DPF(2, "emu10k1_stop_record()\n");

	/* Disable record transfer */
	if (buffer->adcctl)
		sblive_writeptr(card, ADCCR, 0, 0);
}

void emu10k1_set_record_src(struct emu10k1_card *card, struct wiinst *wiinst)
{
	struct wavein_buffer *buffer = &wiinst->buffer;

	DPF(2, "emu10k1_set_record_src()\n");

	switch (wiinst->recsrc) {

	case WAVERECORD_AC97:
		DPF(2, "recording source: AC97\n");
		buffer->sizereg = ADCBS;
		buffer->addrreg = ADCBA;
		buffer->idxreg = card->is_audigy ? A_ADCIDX_IDX : ADCIDX_IDX;

		switch (wiinst->format.samplingrate) {
		case 0xBB80:
			buffer->adcctl = ADCCR_SAMPLERATE_48;
			break;
		case 0xAC44:
			buffer->adcctl = ADCCR_SAMPLERATE_44;
			break;
		case 0x7D00:
			buffer->adcctl = ADCCR_SAMPLERATE_32;
			break;
		case 0x5DC0:
			buffer->adcctl = ADCCR_SAMPLERATE_24;
			break;
		case 0x5622:
			buffer->adcctl = ADCCR_SAMPLERATE_22;
			break;
		case 0x3E80:
			buffer->adcctl = ADCCR_SAMPLERATE_16;
			break;
		// FIXME: audigy supports 12kHz recording
		/*
		case ????:
			buffer->adcctl = A_ADCCR_SAMPLERATE_12;
			break;
		*/
		case 0x2B11:
			buffer->adcctl = card->is_audigy ? A_ADCCR_SAMPLERATE_11 : ADCCR_SAMPLERATE_11;
			break;
		case 0x1F40:
			buffer->adcctl = card->is_audigy ? A_ADCCR_SAMPLERATE_8 : ADCCR_SAMPLERATE_8;
			break;
		default:
			BUG();
			break;
		}

		buffer->adcctl |= card->is_audigy ? A_ADCCR_LCHANENABLE : ADCCR_LCHANENABLE;

		if (wiinst->format.channels == 2)
			buffer->adcctl |= card->is_audigy ? A_ADCCR_RCHANENABLE : ADCCR_RCHANENABLE;

		break;

	case WAVERECORD_MIC:
		DPF(2, "recording source: MIC\n");
		buffer->sizereg = MICBS;
		buffer->addrreg = MICBA;
		buffer->idxreg = MICIDX_IDX;
		buffer->adcctl = 0;
		break;

	case WAVERECORD_FX:
		DPF(2, "recording source: FX\n");
		buffer->sizereg = FXBS;
		buffer->addrreg = FXBA;
		buffer->idxreg = FXIDX_IDX;
		buffer->adcctl = 0;

		sblive_writeptr(card, FXWC, 0, wiinst->fxwc);
		break;
	default:
		BUG();
		break;
	}

	DPD(2, "bus addx: %#lx\n", (unsigned long) buffer->dma_handle);

	sblive_writeptr(card, buffer->addrreg, 0, (u32)buffer->dma_handle);
}
