/*
 **********************************************************************
 *     cardwi.c - PCM input HAL for emu10k1 driver
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

#include <linux/poll.h>
#include "hwaccess.h"
#include "timer.h"
#include "recmgr.h"
#include "audio.h"
#include "cardwi.h"

/**
 * query_format - returns a valid sound format
 *
 * This function will return a valid sound format as close
 * to the requested one as possible. 
 */
static void query_format(int recsrc, struct wave_format *wave_fmt)
{

	switch (recsrc) {
	case WAVERECORD_AC97:

		if ((wave_fmt->channels != 1) && (wave_fmt->channels != 2))
			wave_fmt->channels = 2;

		if (wave_fmt->samplingrate >= (0xBB80 + 0xAC44) / 2)
			wave_fmt->samplingrate = 0xBB80;
		else if (wave_fmt->samplingrate >= (0xAC44 + 0x7D00) / 2)
			wave_fmt->samplingrate = 0xAC44;
		else if (wave_fmt->samplingrate >= (0x7D00 + 0x5DC0) / 2)
			wave_fmt->samplingrate = 0x7D00;
		else if (wave_fmt->samplingrate >= (0x5DC0 + 0x5622) / 2)
			wave_fmt->samplingrate = 0x5DC0;
		else if (wave_fmt->samplingrate >= (0x5622 + 0x3E80) / 2)
			wave_fmt->samplingrate = 0x5622;
		else if (wave_fmt->samplingrate >= (0x3E80 + 0x2B11) / 2)
			wave_fmt->samplingrate = 0x3E80;
		else if (wave_fmt->samplingrate >= (0x2B11 + 0x1F40) / 2)
			wave_fmt->samplingrate = 0x2B11;
		else
			wave_fmt->samplingrate = 0x1F40;

		switch (wave_fmt->id) {
		case AFMT_S16_LE:
			wave_fmt->bitsperchannel = 16;
			break;
		case AFMT_U8:
			wave_fmt->bitsperchannel = 8;
			break;
		default:
			wave_fmt->id = AFMT_S16_LE;
			wave_fmt->bitsperchannel = 16;
			break;
		}

		break;

	/* these can't be changed from the original values */
	case WAVERECORD_MIC:
	case WAVERECORD_FX:
		break;

	default:
		BUG();
		break;
	}

	wave_fmt->bytesperchannel = wave_fmt->bitsperchannel >> 3;
	wave_fmt->bytespersample = wave_fmt->channels * wave_fmt->bytesperchannel;
	wave_fmt->bytespersec = wave_fmt->bytespersample * wave_fmt->samplingrate;
	wave_fmt->bytespervoicesample = wave_fmt->bytespersample;
}

static int alloc_buffer(struct emu10k1_card *card, struct wavein_buffer *buffer)
{
	buffer->addr = pci_alloc_consistent(card->pci_dev, buffer->size * buffer->cov,
					    &buffer->dma_handle);
	if (buffer->addr == NULL)
		return -1;

	return 0;
}

static void free_buffer(struct emu10k1_card *card, struct wavein_buffer *buffer)
{
	if (buffer->addr != NULL)
		pci_free_consistent(card->pci_dev, buffer->size * buffer->cov,
				    buffer->addr, buffer->dma_handle);
}

int emu10k1_wavein_open(struct emu10k1_wavedevice *wave_dev)
{
	struct emu10k1_card *card = wave_dev->card;
	struct wiinst *wiinst = wave_dev->wiinst;
	struct wiinst **wiinst_tmp = NULL;
	u16 delay;
	unsigned long flags;

	DPF(2, "emu10k1_wavein_open()\n");

	switch (wiinst->recsrc) {
	case WAVERECORD_AC97:
		wiinst_tmp = &card->wavein.ac97;
		break;
	case WAVERECORD_MIC:
		wiinst_tmp = &card->wavein.mic;
		break;
	case WAVERECORD_FX:
		wiinst_tmp = &card->wavein.fx;
		break;
	default:
		BUG();
		break;
	}

	spin_lock_irqsave(&card->lock, flags);
	if (*wiinst_tmp != NULL) {
		spin_unlock_irqrestore(&card->lock, flags);
		return -1;
	}

	*wiinst_tmp = wiinst;
	spin_unlock_irqrestore(&card->lock, flags);

	/* handle 8 bit recording */
	if (wiinst->format.bytesperchannel == 1) {
		if (wiinst->buffer.size > 0x8000) {
			wiinst->buffer.size = 0x8000;
			wiinst->buffer.sizeregval = 0x1f;
		} else
			wiinst->buffer.sizeregval += 4;

		wiinst->buffer.cov = 2;
	} else
		wiinst->buffer.cov = 1;

	if (alloc_buffer(card, &wiinst->buffer) < 0) {
		ERROR();
		return -1;
	}

	emu10k1_set_record_src(card, wiinst);

	emu10k1_reset_record(card, &wiinst->buffer);

	wiinst->buffer.hw_pos = 0;
	wiinst->buffer.pos = 0;
	wiinst->buffer.bytestocopy = 0;

	delay = (48000 * wiinst->buffer.fragment_size) / wiinst->format.bytespersec;

	emu10k1_timer_install(card, &wiinst->timer, delay / 2);

	wiinst->state = WAVE_STATE_OPEN;

	return 0;
}

void emu10k1_wavein_close(struct emu10k1_wavedevice *wave_dev)
{
	struct emu10k1_card *card = wave_dev->card;
	struct wiinst *wiinst = wave_dev->wiinst;
	unsigned long flags;

	DPF(2, "emu10k1_wavein_close()\n");

	emu10k1_wavein_stop(wave_dev);

	emu10k1_timer_uninstall(card, &wiinst->timer);

	free_buffer(card, &wiinst->buffer);

	spin_lock_irqsave(&card->lock, flags);
	switch (wave_dev->wiinst->recsrc) {
	case WAVERECORD_AC97:
		card->wavein.ac97 = NULL;
		break;
	case WAVERECORD_MIC:
		card->wavein.mic = NULL;
		break;
	case WAVERECORD_FX:
		card->wavein.fx = NULL;
		break;
	default:
		BUG();
		break;
	}
	spin_unlock_irqrestore(&card->lock, flags);

	wiinst->state = WAVE_STATE_CLOSED;
}

void emu10k1_wavein_start(struct emu10k1_wavedevice *wave_dev)
{
	struct emu10k1_card *card = wave_dev->card;
	struct wiinst *wiinst = wave_dev->wiinst;

	DPF(2, "emu10k1_wavein_start()\n");

	emu10k1_start_record(card, &wiinst->buffer);
	emu10k1_timer_enable(wave_dev->card, &wiinst->timer);

	wiinst->state |= WAVE_STATE_STARTED;
}

void emu10k1_wavein_stop(struct emu10k1_wavedevice *wave_dev)
{
	struct emu10k1_card *card = wave_dev->card;
	struct wiinst *wiinst = wave_dev->wiinst;

	DPF(2, "emu10k1_wavein_stop()\n");

	if (!(wiinst->state & WAVE_STATE_STARTED))
		return;

	emu10k1_timer_disable(card, &wiinst->timer);
	emu10k1_stop_record(card, &wiinst->buffer);

	wiinst->state &= ~WAVE_STATE_STARTED;
}

int emu10k1_wavein_setformat(struct emu10k1_wavedevice *wave_dev, struct wave_format *format)
{
	struct emu10k1_card *card = wave_dev->card;
	struct wiinst *wiinst = wave_dev->wiinst;
	u16 delay;

	DPF(2, "emu10k1_wavein_setformat()\n");

	if (wiinst->state & WAVE_STATE_STARTED)
		return -1;

	query_format(wiinst->recsrc, format);

	if ((wiinst->format.samplingrate != format->samplingrate)
	    || (wiinst->format.bitsperchannel != format->bitsperchannel)
	    || (wiinst->format.channels != format->channels)) {

		wiinst->format = *format;

		if (wiinst->state == WAVE_STATE_CLOSED)
			return 0;

		wiinst->buffer.size *= wiinst->buffer.cov;

		if (wiinst->format.bytesperchannel == 1) {
			wiinst->buffer.cov = 2;
			wiinst->buffer.size /= wiinst->buffer.cov;
		} else
			wiinst->buffer.cov = 1;

		emu10k1_timer_uninstall(card, &wiinst->timer);

		delay = (48000 * wiinst->buffer.fragment_size) / wiinst->format.bytespersec;

		emu10k1_timer_install(card, &wiinst->timer, delay / 2);
	}

	return 0;
}

void emu10k1_wavein_getxfersize(struct wiinst *wiinst, u32 * size)
{
	struct wavein_buffer *buffer = &wiinst->buffer;

	*size = buffer->bytestocopy;

	if (wiinst->mmapped)
		return;

	if (*size > buffer->size) {
		*size = buffer->size;
		buffer->pos = buffer->hw_pos;
		buffer->bytestocopy = buffer->size;
		DPF(1, "buffer overrun\n");
	}
}

static void copy_block(u8 __user *dst, u8 * src, u32 str, u32 len, u8 cov)
{
	if (cov == 1)
		__copy_to_user(dst, src + str, len);
	else {
		u8 byte;
		u32 i;

		src += 1 + 2 * str;

		for (i = 0; i < len; i++) {
			byte = src[2 * i] ^ 0x80;
			__copy_to_user(dst + i, &byte, 1);
		}
	}
}

void emu10k1_wavein_xferdata(struct wiinst *wiinst, u8 __user *data, u32 * size)
{
	struct wavein_buffer *buffer = &wiinst->buffer;
	u32 sizetocopy, sizetocopy_now, start;
	unsigned long flags;

	sizetocopy = min_t(u32, buffer->size, *size);
	*size = sizetocopy;

	if (!sizetocopy)
		return;

	spin_lock_irqsave(&wiinst->lock, flags);
	start = buffer->pos;
	buffer->pos += sizetocopy;
	buffer->pos %= buffer->size;
	buffer->bytestocopy -= sizetocopy;
	sizetocopy_now = buffer->size - start;

	spin_unlock_irqrestore(&wiinst->lock, flags);

	if (sizetocopy > sizetocopy_now) {
		sizetocopy -= sizetocopy_now;

		copy_block(data, buffer->addr, start, sizetocopy_now, buffer->cov);
		copy_block(data + sizetocopy_now, buffer->addr, 0, sizetocopy, buffer->cov);
	} else {
		copy_block(data, buffer->addr, start, sizetocopy, buffer->cov);
	}
}

void emu10k1_wavein_update(struct emu10k1_card *card, struct wiinst *wiinst)
{
	u32 hw_pos;
	u32 diff;

	/* There is no actual start yet */
	if (!(wiinst->state & WAVE_STATE_STARTED)) {
		hw_pos = wiinst->buffer.hw_pos;
	} else {
		/* hw_pos in byte units */
		hw_pos = sblive_readptr(card, wiinst->buffer.idxreg, 0) / wiinst->buffer.cov;
	}

	diff = (wiinst->buffer.size + hw_pos - wiinst->buffer.hw_pos) % wiinst->buffer.size;
	wiinst->total_recorded += diff;
	wiinst->buffer.bytestocopy += diff;

	wiinst->buffer.hw_pos = hw_pos;
}
