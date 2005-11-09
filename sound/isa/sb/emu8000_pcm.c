/*
 * pcm emulation on emu8000 wavetable
 *
 *  Copyright (C) 2002 Takashi Iwai <tiwai@suse.de>
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

#include "emu8000_local.h"
#include <linux/init.h>
#include <sound/initval.h>
#include <sound/pcm.h>

/*
 * define the following if you want to use this pcm with non-interleaved mode
 */
/* #define USE_NONINTERLEAVE */

/* NOTE: for using the non-interleaved mode with alsa-lib, you have to set
 * mmap_emulation flag to 1 in your .asoundrc, such like
 *
 *	pcm.emu8k {
 *		type plug
 *		slave.pcm {
 *			type hw
 *			card 0
 *			device 1
 *			mmap_emulation 1
 *		}
 *	}
 *
 * besides, for the time being, the non-interleaved mode doesn't work well on
 * alsa-lib...
 */


typedef struct snd_emu8k_pcm emu8k_pcm_t;

struct snd_emu8k_pcm {
	emu8000_t *emu;
	snd_pcm_substream_t *substream;

	unsigned int allocated_bytes;
	snd_util_memblk_t *block;
	unsigned int offset;
	unsigned int buf_size;
	unsigned int period_size;
	unsigned int loop_start[2];
	unsigned int pitch;
	int panning[2];
	int last_ptr;
	int period_pos;
	int voices;
	unsigned int dram_opened: 1;
	unsigned int running: 1;
	unsigned int timer_running: 1;
	struct timer_list timer;
	spinlock_t timer_lock;
};

#define LOOP_BLANK_SIZE		8


/*
 * open up channels for the simultaneous data transfer and playback
 */
static int
emu8k_open_dram_for_pcm(emu8000_t *emu, int channels)
{
	int i;

	/* reserve up to 2 voices for playback */
	snd_emux_lock_voice(emu->emu, 0);
	if (channels > 1)
		snd_emux_lock_voice(emu->emu, 1);

	/* reserve 28 voices for loading */
	for (i = channels + 1; i < EMU8000_DRAM_VOICES; i++) {
		unsigned int mode = EMU8000_RAM_WRITE;
		snd_emux_lock_voice(emu->emu, i);
#ifndef USE_NONINTERLEAVE
		if (channels > 1 && (i & 1) != 0)
			mode |= EMU8000_RAM_RIGHT;
#endif
		snd_emu8000_dma_chan(emu, i, mode);
	}

	/* assign voice 31 and 32 to ROM */
	EMU8000_VTFT_WRITE(emu, 30, 0);
	EMU8000_PSST_WRITE(emu, 30, 0x1d8);
	EMU8000_CSL_WRITE(emu, 30, 0x1e0);
	EMU8000_CCCA_WRITE(emu, 30, 0x1d8);
	EMU8000_VTFT_WRITE(emu, 31, 0);
	EMU8000_PSST_WRITE(emu, 31, 0x1d8);
	EMU8000_CSL_WRITE(emu, 31, 0x1e0);
	EMU8000_CCCA_WRITE(emu, 31, 0x1d8);

	return 0;
}

/*
 */
static void
snd_emu8000_write_wait(emu8000_t *emu, int can_schedule)
{
	while ((EMU8000_SMALW_READ(emu) & 0x80000000) != 0) {
		if (can_schedule) {
			schedule_timeout_interruptible(1);
			if (signal_pending(current))
				break;
		}
	}
}

/*
 * close all channels
 */
static void
emu8k_close_dram(emu8000_t *emu)
{
	int i;

	for (i = 0; i < 2; i++)
		snd_emux_unlock_voice(emu->emu, i);
	for (; i < EMU8000_DRAM_VOICES; i++) {
		snd_emu8000_dma_chan(emu, i, EMU8000_RAM_CLOSE);
		snd_emux_unlock_voice(emu->emu, i);
	}
}

/*
 * convert Hz to AWE32 rate offset (see emux/soundfont.c)
 */

#define OFFSET_SAMPLERATE	1011119		/* base = 44100 */
#define SAMPLERATE_RATIO	4096

static int calc_rate_offset(int hz)
{
	return snd_sf_linear_to_log(hz, OFFSET_SAMPLERATE, SAMPLERATE_RATIO);
}


/*
 */

static snd_pcm_hardware_t emu8k_pcm_hw = {
#ifdef USE_NONINTERLEAVE
	.info =			SNDRV_PCM_INFO_NONINTERLEAVED,
#else
	.info =			SNDRV_PCM_INFO_INTERLEAVED,
#endif
	.formats =		SNDRV_PCM_FMTBIT_S16_LE,
	.rates =		SNDRV_PCM_RATE_CONTINUOUS | SNDRV_PCM_RATE_8000_48000,
	.rate_min =		4000,
	.rate_max =		48000,
	.channels_min =		1,
	.channels_max =		2,
	.buffer_bytes_max =	(128*1024),
	.period_bytes_min =	1024,
	.period_bytes_max =	(128*1024),
	.periods_min =		2,
	.periods_max =		1024,
	.fifo_size =		0,

};

/*
 * get the current position at the given channel from CCCA register
 */
static inline int emu8k_get_curpos(emu8k_pcm_t *rec, int ch)
{
	int val = EMU8000_CCCA_READ(rec->emu, ch) & 0xfffffff;
	val -= rec->loop_start[ch] - 1;
	return val;
}


/*
 * timer interrupt handler
 * check the current position and update the period if necessary.
 */
static void emu8k_pcm_timer_func(unsigned long data)
{
	emu8k_pcm_t *rec = (emu8k_pcm_t *)data;
	int ptr, delta;

	spin_lock(&rec->timer_lock);
	/* update the current pointer */
	ptr = emu8k_get_curpos(rec, 0);
	if (ptr < rec->last_ptr)
		delta = ptr + rec->buf_size - rec->last_ptr;
	else
		delta = ptr - rec->last_ptr;
	rec->period_pos += delta;
	rec->last_ptr = ptr;

	/* reprogram timer */
	rec->timer.expires = jiffies + 1;
	add_timer(&rec->timer);

	/* update period */
	if (rec->period_pos >= (int)rec->period_size) {
		rec->period_pos %= rec->period_size;
		spin_unlock(&rec->timer_lock);
		snd_pcm_period_elapsed(rec->substream);
		return;
	}
	spin_unlock(&rec->timer_lock);
}


/*
 * open pcm
 * creating an instance here
 */
static int emu8k_pcm_open(snd_pcm_substream_t *subs)
{
	emu8000_t *emu = snd_pcm_substream_chip(subs);
	emu8k_pcm_t *rec;
	snd_pcm_runtime_t *runtime = subs->runtime;

	rec = kzalloc(sizeof(*rec), GFP_KERNEL);
	if (! rec)
		return -ENOMEM;

	rec->emu = emu;
	rec->substream = subs;
	runtime->private_data = rec;

	spin_lock_init(&rec->timer_lock);
	init_timer(&rec->timer);
	rec->timer.function = emu8k_pcm_timer_func;
	rec->timer.data = (unsigned long)rec;

	runtime->hw = emu8k_pcm_hw;
	runtime->hw.buffer_bytes_max = emu->mem_size - LOOP_BLANK_SIZE * 3;
	runtime->hw.period_bytes_max = runtime->hw.buffer_bytes_max / 2;

	/* use timer to update periods.. (specified in msec) */
	snd_pcm_hw_constraint_minmax(runtime, SNDRV_PCM_HW_PARAM_PERIOD_TIME,
				     (1000000 + HZ - 1) / HZ, UINT_MAX);

	return 0;
}

static int emu8k_pcm_close(snd_pcm_substream_t *subs)
{
	emu8k_pcm_t *rec = subs->runtime->private_data;
	kfree(rec);
	subs->runtime->private_data = NULL;
	return 0;
}

/*
 * calculate pitch target
 */
static int calc_pitch_target(int pitch)
{
	int ptarget = 1 << (pitch >> 12);
	if (pitch & 0x800) ptarget += (ptarget * 0x102e) / 0x2710;
	if (pitch & 0x400) ptarget += (ptarget * 0x764) / 0x2710;
	if (pitch & 0x200) ptarget += (ptarget * 0x389) / 0x2710;
	ptarget += (ptarget >> 1);
	if (ptarget > 0xffff) ptarget = 0xffff;
	return ptarget;
}

/*
 * set up the voice
 */
static void setup_voice(emu8k_pcm_t *rec, int ch)
{
	emu8000_t *hw = rec->emu;
	unsigned int temp;

	/* channel to be silent and idle */
	EMU8000_DCYSUSV_WRITE(hw, ch, 0x0080);
	EMU8000_VTFT_WRITE(hw, ch, 0x0000FFFF);
	EMU8000_CVCF_WRITE(hw, ch, 0x0000FFFF);
	EMU8000_PTRX_WRITE(hw, ch, 0);
	EMU8000_CPF_WRITE(hw, ch, 0);

	/* pitch offset */
	EMU8000_IP_WRITE(hw, ch, rec->pitch);
	/* set envelope parameters */
	EMU8000_ENVVAL_WRITE(hw, ch, 0x8000);
	EMU8000_ATKHLD_WRITE(hw, ch, 0x7f7f);
	EMU8000_DCYSUS_WRITE(hw, ch, 0x7f7f);
	EMU8000_ENVVOL_WRITE(hw, ch, 0x8000);
	EMU8000_ATKHLDV_WRITE(hw, ch, 0x7f7f);
	/* decay/sustain parameter for volume envelope is used
	   for triggerg the voice */
	/* modulation envelope heights */
	EMU8000_PEFE_WRITE(hw, ch, 0x0);
	/* lfo1/2 delay */
	EMU8000_LFO1VAL_WRITE(hw, ch, 0x8000);
	EMU8000_LFO2VAL_WRITE(hw, ch, 0x8000);
	/* lfo1 pitch & cutoff shift */
	EMU8000_FMMOD_WRITE(hw, ch, 0);
	/* lfo1 volume & freq */
	EMU8000_TREMFRQ_WRITE(hw, ch, 0);
	/* lfo2 pitch & freq */
	EMU8000_FM2FRQ2_WRITE(hw, ch, 0);
	/* pan & loop start */
	temp = rec->panning[ch];
	temp = (temp <<24) | ((unsigned int)rec->loop_start[ch] - 1);
	EMU8000_PSST_WRITE(hw, ch, temp);
	/* chorus & loop end (chorus 8bit, MSB) */
	temp = 0; // chorus
	temp = (temp << 24) | ((unsigned int)rec->loop_start[ch] + rec->buf_size - 1);
	EMU8000_CSL_WRITE(hw, ch, temp);
	/* Q & current address (Q 4bit value, MSB) */
	temp = 0; // filterQ
	temp = (temp << 28) | ((unsigned int)rec->loop_start[ch] - 1);
	EMU8000_CCCA_WRITE(hw, ch, temp);
	/* clear unknown registers */
	EMU8000_00A0_WRITE(hw, ch, 0);
	EMU8000_0080_WRITE(hw, ch, 0);
}

/*
 * trigger the voice
 */
static void start_voice(emu8k_pcm_t *rec, int ch)
{
	unsigned long flags;
	emu8000_t *hw = rec->emu;
	unsigned int temp, aux;
	int pt = calc_pitch_target(rec->pitch);

	/* cutoff and volume */
	EMU8000_IFATN_WRITE(hw, ch, 0xff00);
	EMU8000_VTFT_WRITE(hw, ch, 0xffff);
	EMU8000_CVCF_WRITE(hw, ch, 0xffff);
	/* trigger envelope */
	EMU8000_DCYSUSV_WRITE(hw, ch, 0x7f7f);
	/* set reverb and pitch target */
	temp = 0; // reverb
	if (rec->panning[ch] == 0)
		aux = 0xff;
	else
		aux = (-rec->panning[ch]) & 0xff;
	temp = (temp << 8) | (pt << 16) | aux;
	EMU8000_PTRX_WRITE(hw, ch, temp);
	EMU8000_CPF_WRITE(hw, ch, pt << 16);

	/* start timer */
	spin_lock_irqsave(&rec->timer_lock, flags);
	if (! rec->timer_running) {
		rec->timer.expires = jiffies + 1;
		add_timer(&rec->timer);
		rec->timer_running = 1;
	}
	spin_unlock_irqrestore(&rec->timer_lock, flags);
}

/*
 * stop the voice immediately
 */
static void stop_voice(emu8k_pcm_t *rec, int ch)
{
	unsigned long flags;
	emu8000_t *hw = rec->emu;

	EMU8000_DCYSUSV_WRITE(hw, ch, 0x807F);

	/* stop timer */
	spin_lock_irqsave(&rec->timer_lock, flags);
	if (rec->timer_running) {
		del_timer(&rec->timer);
		rec->timer_running = 0;
	}
	spin_unlock_irqrestore(&rec->timer_lock, flags);
}

static int emu8k_pcm_trigger(snd_pcm_substream_t *subs, int cmd)
{
	emu8k_pcm_t *rec = subs->runtime->private_data;
	int ch;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		for (ch = 0; ch < rec->voices; ch++)
			start_voice(rec, ch);
		rec->running = 1;
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		rec->running = 0;
		for (ch = 0; ch < rec->voices; ch++)
			stop_voice(rec, ch);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}


/*
 * copy / silence ops
 */

/*
 * this macro should be inserted in the copy/silence loops
 * to reduce the latency.  without this, the system will hang up
 * during the whole loop.
 */
#define CHECK_SCHEDULER() \
do { \
	cond_resched();\
	if (signal_pending(current))\
		return -EAGAIN;\
} while (0)


#ifdef USE_NONINTERLEAVE
/* copy one channel block */
static int emu8k_transfer_block(emu8000_t *emu, int offset, unsigned short *buf, int count)
{
	EMU8000_SMALW_WRITE(emu, offset);
	while (count > 0) {
		unsigned short sval;
		CHECK_SCHEDULER();
		get_user(sval, buf);
		EMU8000_SMLD_WRITE(emu, sval);
		buf++;
		count--;
	}
	return 0;
}

static int emu8k_pcm_copy(snd_pcm_substream_t *subs,
			  int voice,
			  snd_pcm_uframes_t pos,
			  void *src,
			  snd_pcm_uframes_t count)
{
	emu8k_pcm_t *rec = subs->runtime->private_data;
	emu8000_t *emu = rec->emu;

	snd_emu8000_write_wait(emu, 1);
	if (voice == -1) {
		unsigned short *buf = src;
		int i, err;
		count /= rec->voices;
		for (i = 0; i < rec->voices; i++) {
			err = emu8k_transfer_block(emu, pos + rec->loop_start[i], buf, count);
			if (err < 0)
				return err;
			buf += count;
		}
		return 0;
	} else {
		return emu8k_transfer_block(emu, pos + rec->loop_start[voice], src, count);
	}
}

/* make a channel block silence */
static int emu8k_silence_block(emu8000_t *emu, int offset, int count)
{
	EMU8000_SMALW_WRITE(emu, offset);
	while (count > 0) {
		CHECK_SCHEDULER();
		EMU8000_SMLD_WRITE(emu, 0);
		count--;
	}
	return 0;
}

static int emu8k_pcm_silence(snd_pcm_substream_t *subs,
			     int voice,
			     snd_pcm_uframes_t pos,
			     snd_pcm_uframes_t count)
{
	emu8k_pcm_t *rec = subs->runtime->private_data;
	emu8000_t *emu = rec->emu;

	snd_emu8000_write_wait(emu, 1);
	if (voice == -1 && rec->voices == 1)
		voice = 0;
	if (voice == -1) {
		int err;
		err = emu8k_silence_block(emu, pos + rec->loop_start[0], count / 2);
		if (err < 0)
			return err;
		return emu8k_silence_block(emu, pos + rec->loop_start[1], count / 2);
	} else {
		return emu8k_silence_block(emu, pos + rec->loop_start[voice], count);
	}
}

#else /* interleave */

/*
 * copy the interleaved data can be done easily by using
 * DMA "left" and "right" channels on emu8k engine.
 */
static int emu8k_pcm_copy(snd_pcm_substream_t *subs,
			  int voice,
			  snd_pcm_uframes_t pos,
			  void __user *src,
			  snd_pcm_uframes_t count)
{
	emu8k_pcm_t *rec = subs->runtime->private_data;
	emu8000_t *emu = rec->emu;
	unsigned short __user *buf = src;

	snd_emu8000_write_wait(emu, 1);
	EMU8000_SMALW_WRITE(emu, pos + rec->loop_start[0]);
	if (rec->voices > 1)
		EMU8000_SMARW_WRITE(emu, pos + rec->loop_start[1]);

	while (count-- > 0) {
		unsigned short sval;
		CHECK_SCHEDULER();
		get_user(sval, buf);
		EMU8000_SMLD_WRITE(emu, sval);
		buf++;
		if (rec->voices > 1) {
			CHECK_SCHEDULER();
			get_user(sval, buf);
			EMU8000_SMRD_WRITE(emu, sval);
			buf++;
		}
	}
	return 0;
}

static int emu8k_pcm_silence(snd_pcm_substream_t *subs,
			     int voice,
			     snd_pcm_uframes_t pos,
			     snd_pcm_uframes_t count)
{
	emu8k_pcm_t *rec = subs->runtime->private_data;
	emu8000_t *emu = rec->emu;

	snd_emu8000_write_wait(emu, 1);
	EMU8000_SMALW_WRITE(emu, rec->loop_start[0] + pos);
	if (rec->voices > 1)
		EMU8000_SMARW_WRITE(emu, rec->loop_start[1] + pos);
	while (count-- > 0) {
		CHECK_SCHEDULER();
		EMU8000_SMLD_WRITE(emu, 0);
		if (rec->voices > 1) {
			CHECK_SCHEDULER();
			EMU8000_SMRD_WRITE(emu, 0);
		}
	}
	return 0;
}
#endif


/*
 * allocate a memory block
 */
static int emu8k_pcm_hw_params(snd_pcm_substream_t *subs,
			       snd_pcm_hw_params_t *hw_params)
{
	emu8k_pcm_t *rec = subs->runtime->private_data;

	if (rec->block) {
		/* reallocation - release the old block */
		snd_util_mem_free(rec->emu->memhdr, rec->block);
		rec->block = NULL;
	}

	rec->allocated_bytes = params_buffer_bytes(hw_params) + LOOP_BLANK_SIZE * 4;
	rec->block = snd_util_mem_alloc(rec->emu->memhdr, rec->allocated_bytes);
	if (! rec->block)
		return -ENOMEM;
	rec->offset = EMU8000_DRAM_OFFSET + (rec->block->offset >> 1); /* in word */
	/* at least dma_bytes must be set for non-interleaved mode */
	subs->dma_buffer.bytes = params_buffer_bytes(hw_params);

	return 0;
}

/*
 * free the memory block
 */
static int emu8k_pcm_hw_free(snd_pcm_substream_t *subs)
{
	emu8k_pcm_t *rec = subs->runtime->private_data;

	if (rec->block) {
		int ch;
		for (ch = 0; ch < rec->voices; ch++)
			stop_voice(rec, ch); // to be sure
		if (rec->dram_opened)
			emu8k_close_dram(rec->emu);
		snd_util_mem_free(rec->emu->memhdr, rec->block);
		rec->block = NULL;
	}
	return 0;
}

/*
 */
static int emu8k_pcm_prepare(snd_pcm_substream_t *subs)
{
	emu8k_pcm_t *rec = subs->runtime->private_data;

	rec->pitch = 0xe000 + calc_rate_offset(subs->runtime->rate);
	rec->last_ptr = 0;
	rec->period_pos = 0;

	rec->buf_size = subs->runtime->buffer_size;
	rec->period_size = subs->runtime->period_size;
	rec->voices = subs->runtime->channels;
	rec->loop_start[0] = rec->offset + LOOP_BLANK_SIZE;
	if (rec->voices > 1)
		rec->loop_start[1] = rec->loop_start[0] + rec->buf_size + LOOP_BLANK_SIZE;
	if (rec->voices > 1) {
		rec->panning[0] = 0xff;
		rec->panning[1] = 0x00;
	} else
		rec->panning[0] = 0x80;

	if (! rec->dram_opened) {
		int err, i, ch;

		snd_emux_terminate_all(rec->emu->emu);
		if ((err = emu8k_open_dram_for_pcm(rec->emu, rec->voices)) != 0)
			return err;
		rec->dram_opened = 1;

		/* clear loop blanks */
		snd_emu8000_write_wait(rec->emu, 0);
		EMU8000_SMALW_WRITE(rec->emu, rec->offset);
		for (i = 0; i < LOOP_BLANK_SIZE; i++)
			EMU8000_SMLD_WRITE(rec->emu, 0);
		for (ch = 0; ch < rec->voices; ch++) {
			EMU8000_SMALW_WRITE(rec->emu, rec->loop_start[ch] + rec->buf_size);
			for (i = 0; i < LOOP_BLANK_SIZE; i++)
				EMU8000_SMLD_WRITE(rec->emu, 0);
		}
	}

	setup_voice(rec, 0);
	if (rec->voices > 1)
		setup_voice(rec, 1);
	return 0;
}

static snd_pcm_uframes_t emu8k_pcm_pointer(snd_pcm_substream_t *subs)
{
	emu8k_pcm_t *rec = subs->runtime->private_data;
	if (rec->running)
		return emu8k_get_curpos(rec, 0);
	return 0;
}


static snd_pcm_ops_t emu8k_pcm_ops = {
	.open =		emu8k_pcm_open,
	.close =	emu8k_pcm_close,
	.ioctl =	snd_pcm_lib_ioctl,
	.hw_params =	emu8k_pcm_hw_params,
	.hw_free =	emu8k_pcm_hw_free,
	.prepare =	emu8k_pcm_prepare,
	.trigger =	emu8k_pcm_trigger,
	.pointer =	emu8k_pcm_pointer,
	.copy =		emu8k_pcm_copy,
	.silence =	emu8k_pcm_silence,
};


static void snd_emu8000_pcm_free(snd_pcm_t *pcm)
{
	emu8000_t *emu = pcm->private_data;
	emu->pcm = NULL;
}

int snd_emu8000_pcm_new(snd_card_t *card, emu8000_t *emu, int index)
{
	snd_pcm_t *pcm;
	int err;

	if ((err = snd_pcm_new(card, "Emu8000 PCM", index, 1, 0, &pcm)) < 0)
		return err;
	pcm->private_data = emu;
	pcm->private_free = snd_emu8000_pcm_free;
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &emu8k_pcm_ops);
	emu->pcm = pcm;

	snd_device_register(card, pcm);

	return 0;
}
