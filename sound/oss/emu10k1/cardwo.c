/*
 **********************************************************************
 *     cardwo.c - PCM output HAL for emu10k1 driver
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
#include "8010.h"
#include "voicemgr.h"
#include "cardwo.h"
#include "audio.h"

static u32 samplerate_to_linearpitch(u32 samplingrate)
{
	samplingrate = (samplingrate << 8) / 375;
	return (samplingrate >> 1) + (samplingrate & 1);
}

static void query_format(struct emu10k1_wavedevice *wave_dev, struct wave_format *wave_fmt)
{
	int i, j, do_passthrough = 0, is_ac3 = 0;
	struct emu10k1_card *card = wave_dev->card;
	struct woinst *woinst = wave_dev->woinst;

	if ((wave_fmt->channels > 2) && (wave_fmt->id != AFMT_S16_LE) && (wave_fmt->id != AFMT_U8))
		wave_fmt->channels = 2;

	if ((wave_fmt->channels < 1) || (wave_fmt->channels > WAVEOUT_MAXVOICES))
		wave_fmt->channels = 2;

	if (wave_fmt->channels == 2)
		woinst->num_voices = 1;
	else
		woinst->num_voices = wave_fmt->channels;

	if (wave_fmt->samplingrate >= 0x2ee00)
		wave_fmt->samplingrate = 0x2ee00;

	wave_fmt->passthrough = 0;
	do_passthrough = is_ac3 = 0;

	if (card->pt.selected)
		do_passthrough = 1;

	switch (wave_fmt->id) {
	case AFMT_S16_LE:
		wave_fmt->bitsperchannel = 16;
		break;
	case AFMT_U8:
		wave_fmt->bitsperchannel = 8;
		break;
	case AFMT_AC3:
		do_passthrough = 1;
		is_ac3 = 1;
		break;
	default:
		wave_fmt->id = AFMT_S16_LE;
		wave_fmt->bitsperchannel = 16;
		break;
	}	
	if (do_passthrough) {
		/* currently only one waveout instance may use pass-through */
		if (woinst->state != WAVE_STATE_CLOSED || 
		    card->pt.state != PT_STATE_INACTIVE ||
		    (wave_fmt->samplingrate != 48000 && !is_ac3)) {
			DPF(2, "unable to set pass-through mode\n");
		} else if (USE_PT_METHOD1) {
			i = emu10k1_find_control_gpr(&card->mgr, card->pt.patch_name, card->pt.intr_gpr_name);
			j = emu10k1_find_control_gpr(&card->mgr, card->pt.patch_name, card->pt.enable_gpr_name);
			if (i < 0 || j < 0)
				DPF(2, "unable to set pass-through mode\n");
			else {
				wave_fmt->samplingrate = 48000;
				wave_fmt->channels = 2;
				card->pt.pos_gpr = emu10k1_find_control_gpr(&card->mgr, card->pt.patch_name,
									    card->pt.pos_gpr_name);
				wave_fmt->passthrough = 1;
				card->pt.intr_gpr = i;
				card->pt.enable_gpr = j;
				card->pt.state = PT_STATE_INACTIVE;
			
				DPD(2, "is_ac3 is %d\n", is_ac3);
				card->pt.ac3data = is_ac3;
				wave_fmt->bitsperchannel = 16;
			}
		}else{
			DPF(2, "Using Passthrough Method 2\n");
			card->pt.enable_gpr = emu10k1_find_control_gpr(&card->mgr, card->pt.patch_name,
								       card->pt.enable_gpr_name);
			wave_fmt->passthrough = 2;
			wave_fmt->bitsperchannel = 16;
		}
	}

	wave_fmt->bytesperchannel = wave_fmt->bitsperchannel >> 3;
	wave_fmt->bytespersample = wave_fmt->channels * wave_fmt->bytesperchannel;
	wave_fmt->bytespersec = wave_fmt->bytespersample * wave_fmt->samplingrate;

	if (wave_fmt->channels == 2)
		wave_fmt->bytespervoicesample = wave_fmt->channels * wave_fmt->bytesperchannel;
	else
		wave_fmt->bytespervoicesample = wave_fmt->bytesperchannel;
}

static int get_voice(struct emu10k1_card *card, struct woinst *woinst, unsigned int voicenum)
{
	struct emu_voice *voice = &woinst->voice[voicenum];

	/* Allocate voices here, if no voices available, return error. */

	voice->usage = VOICE_USAGE_PLAYBACK;

	voice->flags = 0;

	if (woinst->format.channels == 2)
		voice->flags |= VOICE_FLAGS_STEREO;

	if (woinst->format.bitsperchannel == 16)
		voice->flags |= VOICE_FLAGS_16BIT;

	if (emu10k1_voice_alloc(card, voice) < 0) {
		voice->usage = VOICE_USAGE_FREE;
		return -1;
	}

	/* Calculate pitch */
	voice->initial_pitch = (u16) (srToPitch(woinst->format.samplingrate) >> 8);
	voice->pitch_target = samplerate_to_linearpitch(woinst->format.samplingrate);

	DPD(2, "Initial pitch --> %#x\n", voice->initial_pitch);

	voice->startloop = (voice->mem.emupageindex << 12) /
	 woinst->format.bytespervoicesample;
	voice->endloop = voice->startloop + woinst->buffer.size / woinst->format.bytespervoicesample;
	voice->start = voice->startloop;

	
	voice->params[0].volume_target = 0xffff;
	voice->params[0].initial_fc = 0xff;
	voice->params[0].initial_attn = 0x00;
	voice->params[0].byampl_env_sustain = 0x7f;
	voice->params[0].byampl_env_decay = 0x7f;

	
	if (voice->flags & VOICE_FLAGS_STEREO) {
		if (woinst->format.passthrough == 2) {
			voice->params[0].send_routing  = voice->params[1].send_routing  = card->waveout.send_routing[ROUTE_PT];
			voice->params[0].send_routing2 = voice->params[1].send_routing2 = card->waveout.send_routing2[ROUTE_PT];
			voice->params[0].send_dcba = 0xff;
			voice->params[1].send_dcba = 0xff00;
			voice->params[0].send_hgfe = voice->params[1].send_hgfe=0;
		} else {
			voice->params[0].send_dcba = card->waveout.send_dcba[SEND_LEFT];
			voice->params[0].send_hgfe = card->waveout.send_hgfe[SEND_LEFT];
			voice->params[1].send_dcba = card->waveout.send_dcba[SEND_RIGHT];
			voice->params[1].send_hgfe = card->waveout.send_hgfe[SEND_RIGHT];

			if (woinst->device) {
				// /dev/dps1
				voice->params[0].send_routing  = voice->params[1].send_routing  = card->waveout.send_routing[ROUTE_PCM1];
				voice->params[0].send_routing2 = voice->params[1].send_routing2 = card->waveout.send_routing2[ROUTE_PCM1];
			} else {
				voice->params[0].send_routing  = voice->params[1].send_routing  = card->waveout.send_routing[ROUTE_PCM];
				voice->params[0].send_routing2 = voice->params[1].send_routing2 = card->waveout.send_routing2[ROUTE_PCM];
			}
		}
		
		voice->params[1].volume_target = 0xffff;
		voice->params[1].initial_fc = 0xff;
		voice->params[1].initial_attn = 0x00;
		voice->params[1].byampl_env_sustain = 0x7f;
		voice->params[1].byampl_env_decay = 0x7f;
	} else {
		if (woinst->num_voices > 1) {
			// Multichannel pcm
			voice->params[0].send_dcba=0xff;
			voice->params[0].send_hgfe=0;
			if (card->is_audigy) {
				voice->params[0].send_routing = 0x3f3f3f00 + card->mchannel_fx + voicenum;
				voice->params[0].send_routing2 = 0x3f3f3f3f;
			} else {
				voice->params[0].send_routing = 0xfff0 + card->mchannel_fx + voicenum;
			}
			
		} else {
			voice->params[0].send_dcba = card->waveout.send_dcba[SEND_MONO];
			voice->params[0].send_hgfe = card->waveout.send_hgfe[SEND_MONO];

			if (woinst->device) {
				voice->params[0].send_routing = card->waveout.send_routing[ROUTE_PCM1];
				voice->params[0].send_routing2 = card->waveout.send_routing2[ROUTE_PCM1];
			} else {
				voice->params[0].send_routing = card->waveout.send_routing[ROUTE_PCM];
				voice->params[0].send_routing2 = card->waveout.send_routing2[ROUTE_PCM];
			}
		}
	}

	DPD(2, "voice: startloop=%#x, endloop=%#x\n", voice->startloop, voice->endloop);

	emu10k1_voice_playback_setup(voice);

	return 0;
}

int emu10k1_waveout_open(struct emu10k1_wavedevice *wave_dev)
{
	struct emu10k1_card *card = wave_dev->card;
	struct woinst *woinst = wave_dev->woinst;
	struct waveout_buffer *buffer = &woinst->buffer;
	unsigned int voicenum;
	u16 delay;

	DPF(2, "emu10k1_waveout_open()\n");

	for (voicenum = 0; voicenum < woinst->num_voices; voicenum++) {
		if (emu10k1_voice_alloc_buffer(card, &woinst->voice[voicenum].mem, woinst->buffer.pages) < 0) {
			ERROR();
			emu10k1_waveout_close(wave_dev);
			return -1;
		}

		if (get_voice(card, woinst, voicenum) < 0) {
			ERROR();
			emu10k1_waveout_close(wave_dev);
			return -1;
		}
	}

	buffer->fill_silence = 0;
	buffer->silence_bytes = 0;
	buffer->silence_pos = 0;
	buffer->hw_pos = 0;
	buffer->free_bytes = woinst->buffer.size;

	delay = (48000 * woinst->buffer.fragment_size) /
		 (woinst->format.samplingrate * woinst->format.bytespervoicesample);

	emu10k1_timer_install(card, &woinst->timer, delay);

	woinst->state = WAVE_STATE_OPEN;

	return 0;
}

void emu10k1_waveout_close(struct emu10k1_wavedevice *wave_dev)
{
	struct emu10k1_card *card = wave_dev->card;
	struct woinst *woinst = wave_dev->woinst;
	unsigned int voicenum;

	DPF(2, "emu10k1_waveout_close()\n");

	emu10k1_waveout_stop(wave_dev);

	emu10k1_timer_uninstall(card, &woinst->timer);

	for (voicenum = 0; voicenum < woinst->num_voices; voicenum++) {
		emu10k1_voice_free(&woinst->voice[voicenum]);
		emu10k1_voice_free_buffer(card, &woinst->voice[voicenum].mem);
	}

	woinst->state = WAVE_STATE_CLOSED;
}

void emu10k1_waveout_start(struct emu10k1_wavedevice *wave_dev)
{
	struct emu10k1_card *card = wave_dev->card;
	struct woinst *woinst = wave_dev->woinst;
	struct pt_data *pt = &card->pt;

	DPF(2, "emu10k1_waveout_start()\n");

	if (woinst->format.passthrough == 2) {
		emu10k1_pt_setup(wave_dev);
		sblive_writeptr(card, (card->is_audigy ? A_GPR_BASE : GPR_BASE) + pt->enable_gpr, 0, 1);
		pt->state = PT_STATE_PLAYING;
	}

	/* Actual start */
	emu10k1_voices_start(woinst->voice, woinst->num_voices, woinst->total_played);

	emu10k1_timer_enable(card, &woinst->timer);

	woinst->state |= WAVE_STATE_STARTED;
}

int emu10k1_waveout_setformat(struct emu10k1_wavedevice *wave_dev, struct wave_format *format)
{
	struct emu10k1_card *card = wave_dev->card;
	struct woinst *woinst = wave_dev->woinst;
	unsigned int voicenum;
	u16 delay;

	DPF(2, "emu10k1_waveout_setformat()\n");

	if (woinst->state & WAVE_STATE_STARTED)
		return -1;

	query_format(wave_dev, format);

	if (woinst->format.samplingrate != format->samplingrate ||
	    woinst->format.channels != format->channels ||
	    woinst->format.bitsperchannel != format->bitsperchannel) {

		woinst->format = *format;

		if (woinst->state == WAVE_STATE_CLOSED)
			return 0;

		emu10k1_timer_uninstall(card, &woinst->timer);

		for (voicenum = 0; voicenum < woinst->num_voices; voicenum++) {
			emu10k1_voice_free(&woinst->voice[voicenum]);

			if (get_voice(card, woinst, voicenum) < 0) {
				ERROR();
				emu10k1_waveout_close(wave_dev);
				return -1;
			}
		}

		delay = (48000 * woinst->buffer.fragment_size) /
			 (woinst->format.samplingrate * woinst->format.bytespervoicesample);

		emu10k1_timer_install(card, &woinst->timer, delay);
	}

	return 0;
}

void emu10k1_waveout_stop(struct emu10k1_wavedevice *wave_dev)
{
	struct emu10k1_card *card = wave_dev->card;
	struct woinst *woinst = wave_dev->woinst;

	DPF(2, "emu10k1_waveout_stop()\n");

	if (!(woinst->state & WAVE_STATE_STARTED))
		return;

	emu10k1_timer_disable(card, &woinst->timer);

	/* Stop actual voices */
	emu10k1_voices_stop(woinst->voice, woinst->num_voices);

	emu10k1_waveout_update(woinst);

	woinst->state &= ~WAVE_STATE_STARTED;
}

/**
 * emu10k1_waveout_getxfersize -
 *
 * gives the total free bytes on the voice buffer, including silence bytes
 * (basically: total_free_bytes = free_bytes + silence_bytes).
 *
 */
void emu10k1_waveout_getxfersize(struct woinst *woinst, u32 *total_free_bytes)
{
	struct waveout_buffer *buffer = &woinst->buffer;
	int pending_bytes;

	if (woinst->mmapped) {
		*total_free_bytes = buffer->free_bytes;
		return;
	}

	pending_bytes = buffer->size - buffer->free_bytes;

	buffer->fill_silence = (pending_bytes < (signed) buffer->fragment_size * 2) ? 1 : 0;

	if (pending_bytes > (signed) buffer->silence_bytes) {
		*total_free_bytes = (buffer->free_bytes + buffer->silence_bytes);
	} else {
		*total_free_bytes = buffer->size;
		buffer->silence_bytes = pending_bytes;
		if (pending_bytes < 0) {
			buffer->silence_pos = buffer->hw_pos;
			buffer->silence_bytes = 0;
			buffer->free_bytes = buffer->size;
			DPF(1, "buffer underrun\n");
		}
	}
}

/**
 * copy_block -
 *
 * copies a block of pcm data to a voice buffer.
 * Notice that the voice buffer is actually a set of disjointed memory pages.
 *
 */
static void copy_block(void **dst, u32 str, u8 __user *src, u32 len)
{
	unsigned int pg;
	unsigned int pgoff;
	unsigned int k;

	pg = str / PAGE_SIZE;
	pgoff = str % PAGE_SIZE;

	if (len > PAGE_SIZE - pgoff) {
		k = PAGE_SIZE - pgoff;
		if (__copy_from_user((u8 *)dst[pg] + pgoff, src, k))
			return;
		len -= k;
		while (len > PAGE_SIZE) {
			if (__copy_from_user(dst[++pg], src + k, PAGE_SIZE))
				return;
			k += PAGE_SIZE;
			len -= PAGE_SIZE;
		}
		if (__copy_from_user(dst[++pg], src + k, len))
			return;

	} else
		__copy_from_user((u8 *)dst[pg] + pgoff, src, len);
}

/**
 * copy_ilv_block -
 *
 * copies a block of pcm data containing n interleaved channels to n mono voice buffers.
 * Notice that the voice buffer is actually a set of disjointed memory pages.
 *
 */
static void copy_ilv_block(struct woinst *woinst, u32 str, u8 __user *src, u32 len) 
{
        unsigned int pg;
	unsigned int pgoff;
	unsigned int voice_num;
	struct emu_voice *voice = woinst->voice;

	pg = str / PAGE_SIZE;
	pgoff = str % PAGE_SIZE;

	while (len) { 
		for (voice_num = 0; voice_num < woinst->num_voices; voice_num++) {
			if (__copy_from_user((u8 *)(voice[voice_num].mem.addr[pg]) + pgoff, src, woinst->format.bytespervoicesample))
				return;
			src += woinst->format.bytespervoicesample;
		}

		len -= woinst->format.bytespervoicesample;

		pgoff += woinst->format.bytespervoicesample;
		if (pgoff >= PAGE_SIZE) {
			pgoff = 0;
			pg++;
		}
	}
}

/**
 * fill_block -
 *
 * fills a set voice buffers with a block of a given sample.
 *
 */
static void fill_block(struct woinst *woinst, u32 str, u8 data, u32 len)
{
	unsigned int pg;
	unsigned int pgoff;
	unsigned int voice_num;
        struct emu_voice *voice = woinst->voice;
	unsigned int  k;

	pg = str / PAGE_SIZE;
	pgoff = str % PAGE_SIZE;

	if (len > PAGE_SIZE - pgoff) {
		k = PAGE_SIZE - pgoff;
		for (voice_num = 0; voice_num < woinst->num_voices; voice_num++)
			memset((u8 *)voice[voice_num].mem.addr[pg] + pgoff, data, k);
		len -= k;
		while (len > PAGE_SIZE) {
			pg++;
			for (voice_num = 0; voice_num < woinst->num_voices; voice_num++)
				memset(voice[voice_num].mem.addr[pg], data, PAGE_SIZE);

			len -= PAGE_SIZE;
		}
		pg++;
		for (voice_num = 0; voice_num < woinst->num_voices; voice_num++)
			memset(voice[voice_num].mem.addr[pg], data, len);

	} else {
		for (voice_num = 0; voice_num < woinst->num_voices; voice_num++)
			memset((u8 *)voice[voice_num].mem.addr[pg] + pgoff, data, len);
	}
}

/**
 * emu10k1_waveout_xferdata -
 *
 * copies pcm data to the voice buffer. Silence samples
 * previously added to the buffer are overwritten.
 *
 */
void emu10k1_waveout_xferdata(struct woinst *woinst, u8 __user *data, u32 *size)
{
	struct waveout_buffer *buffer = &woinst->buffer;
	struct voice_mem *mem = &woinst->voice[0].mem;
	u32 sizetocopy, sizetocopy_now, start;
	unsigned long flags;

	sizetocopy = min_t(u32, buffer->size, *size);
	*size = sizetocopy;

	if (!sizetocopy)
		return;
	
	spin_lock_irqsave(&woinst->lock, flags);
	start = (buffer->size + buffer->silence_pos - buffer->silence_bytes) % buffer->size;

	if (sizetocopy > buffer->silence_bytes) {
		buffer->silence_pos += sizetocopy - buffer->silence_bytes;
		buffer->free_bytes -= sizetocopy - buffer->silence_bytes;
		buffer->silence_bytes = 0;
	} else
		buffer->silence_bytes -= sizetocopy;

	spin_unlock_irqrestore(&woinst->lock, flags);

	sizetocopy_now = buffer->size - start;
	if (sizetocopy > sizetocopy_now) {
		sizetocopy -= sizetocopy_now;
		if (woinst->num_voices > 1) {
			copy_ilv_block(woinst, start, data, sizetocopy_now);
			copy_ilv_block(woinst, 0, data + sizetocopy_now * woinst->num_voices, sizetocopy);
		} else {
			copy_block(mem->addr, start, data, sizetocopy_now);
			copy_block(mem->addr, 0, data + sizetocopy_now, sizetocopy);
		}
	} else {
		if (woinst->num_voices > 1)
			copy_ilv_block(woinst, start, data, sizetocopy);
		else
			copy_block(mem->addr, start, data, sizetocopy);
	}
}

/**
 * emu10k1_waveout_fillsilence -
 *
 * adds samples of silence to the voice buffer so that we
 * don't loop over stale pcm data.
 *
 */
void emu10k1_waveout_fillsilence(struct woinst *woinst)
{
	struct waveout_buffer *buffer = &woinst->buffer;
	u32 sizetocopy, sizetocopy_now, start;
	u8 filldata;
	unsigned long flags;

	sizetocopy = buffer->fragment_size;

	if (woinst->format.bitsperchannel == 16)
		filldata = 0x00;
	else
		filldata = 0x80;

	spin_lock_irqsave(&woinst->lock, flags);
	buffer->silence_bytes += sizetocopy;
	buffer->free_bytes -= sizetocopy;
	buffer->silence_pos %= buffer->size;
	start = buffer->silence_pos;
	buffer->silence_pos += sizetocopy;
	spin_unlock_irqrestore(&woinst->lock, flags);

	sizetocopy_now = buffer->size - start;

	if (sizetocopy > sizetocopy_now) {
		sizetocopy -= sizetocopy_now;
		fill_block(woinst, start, filldata, sizetocopy_now);
		fill_block(woinst, 0, filldata, sizetocopy);
	} else {
		fill_block(woinst, start, filldata, sizetocopy);
	}
}

/**
 * emu10k1_waveout_update -
 *
 * updates the position of the voice buffer hardware pointer (hw_pos)
 * and the number of free bytes on the buffer (free_bytes).
 * The free bytes _don't_ include silence bytes that may have been
 * added to the buffer.
 *
 */
void emu10k1_waveout_update(struct woinst *woinst)
{
	u32 hw_pos;
	u32 diff;

	/* There is no actual start yet */
	if (!(woinst->state & WAVE_STATE_STARTED)) {
		hw_pos = woinst->buffer.hw_pos;
	} else {
		/* hw_pos in sample units */
		hw_pos = sblive_readptr(woinst->voice[0].card, CCCA_CURRADDR, woinst->voice[0].num);

		if(hw_pos < woinst->voice[0].start)
			hw_pos += woinst->buffer.size / woinst->format.bytespervoicesample - woinst->voice[0].start;
		else
			hw_pos -= woinst->voice[0].start;

		hw_pos *= woinst->format.bytespervoicesample;
	}

	diff = (woinst->buffer.size + hw_pos - woinst->buffer.hw_pos) % woinst->buffer.size;
	woinst->total_played += diff;
	woinst->buffer.free_bytes += diff;
	woinst->buffer.hw_pos = hw_pos;
}
