/*
 *  Copyright (c) by James Courtier-Dutton <James@superbug.demon.co.uk>
 *  Driver p16v chips
 *  Version: 0.25
 *
 *  FEATURES currently supported:
 *    Output fixed at S32_LE, 2 channel to hw:0,0
 *    Rates: 44.1, 48, 96, 192.
 *
 *  Changelog:
 *  0.8
 *    Use separate card based buffer for periods table.
 *  0.9
 *    Use 2 channel output streams instead of 8 channel.
 *       (8 channel output streams might be good for ASIO type output)
 *    Corrected speaker output, so Front -> Front etc.
 *  0.10
 *    Fixed missed interrupts.
 *  0.11
 *    Add Sound card model number and names.
 *    Add Analog volume controls.
 *  0.12
 *    Corrected playback interrupts. Now interrupt per period, instead of half period.
 *  0.13
 *    Use single trigger for multichannel.
 *  0.14
 *    Mic capture now works at fixed: S32_LE, 96000Hz, Stereo.
 *  0.15
 *    Force buffer_size / period_size == INTEGER.
 *  0.16
 *    Update p16v.c to work with changed alsa api.
 *  0.17
 *    Update p16v.c to work with changed alsa api. Removed boot_devs.
 *  0.18
 *    Merging with snd-emu10k1 driver.
 *  0.19
 *    One stereo channel at 24bit now works.
 *  0.20
 *    Added better register defines.
 *  0.21
 *    Integrated with snd-emu10k1 driver.
 *  0.22
 *    Removed #if 0 ... #endif
 *  0.23
 *    Implement different capture rates.
 *  0.24
 *    Implement different capture source channels.
 *    e.g. When HD Capture source is set to SPDIF,
 *    setting HD Capture channel to 0 captures from CDROM digital input.
 *    setting HD Capture channel to 1 captures from SPDIF in.
 *  0.25
 *    Include capture buffer sizes.
 *
 *  BUGS:
 *    Some stability problems when unloading the snd-p16v kernel module.
 *    --
 *
 *  TODO:
 *    SPDIF out.
 *    Find out how to change capture sample rates. E.g. To record SPDIF at 48000Hz.
 *    Currently capture fixed at 48000Hz.
 *
 *    --
 *  GENERAL INFO:
 *    Model: SB0240
 *    P16V Chip: CA0151-DBS
 *    Audigy 2 Chip: CA0102-IAT
 *    AC97 Codec: STAC 9721
 *    ADC: Philips 1361T (Stereo 24bit)
 *    DAC: CS4382-K (8-channel, 24bit, 192Khz)
 *
 *  This code was initally based on code from ALSA's emu10k1x.c which is:
 *  Copyright (c) by Francisco Moraes <fmoraes@nc.rr.com>
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
 *
 */
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/moduleparam.h>
#include <sound/core.h>
#include <sound/initval.h>
#include <sound/pcm.h>
#include <sound/ac97_codec.h>
#include <sound/info.h>
#include <sound/tlv.h>
#include <sound/emu10k1.h>
#include "p16v.h"

#define SET_CHANNEL 0  /* Testing channel outputs 0=Front, 1=Center/LFE, 2=Unknown, 3=Rear */
#define PCM_FRONT_CHANNEL 0
#define PCM_REAR_CHANNEL 1
#define PCM_CENTER_LFE_CHANNEL 2
#define PCM_SIDE_CHANNEL 3
#define CONTROL_FRONT_CHANNEL 0
#define CONTROL_REAR_CHANNEL 3
#define CONTROL_CENTER_LFE_CHANNEL 1
#define CONTROL_SIDE_CHANNEL 2

/* Card IDs:
 * Class 0401: 1102:0004 (rev 04) Subsystem: 1102:2002 -> Audigy2 ZS 7.1 Model:SB0350
 * Class 0401: 1102:0004 (rev 04) Subsystem: 1102:1007 -> Audigy2 6.1    Model:SB0240
 * Class 0401: 1102:0004 (rev 04) Subsystem: 1102:1002 -> Audigy2 Platinum  Model:SB msb0240230009266
 * Class 0401: 1102:0004 (rev 04) Subsystem: 1102:2007 -> Audigy4 Pro Model:SB0380 M1SB0380472001901E
 *
 */

 /* hardware definition */
static struct snd_pcm_hardware snd_p16v_playback_hw = {
	.info =			SNDRV_PCM_INFO_MMAP | 
				SNDRV_PCM_INFO_INTERLEAVED |
				SNDRV_PCM_INFO_BLOCK_TRANSFER |
				SNDRV_PCM_INFO_RESUME |
				SNDRV_PCM_INFO_MMAP_VALID |
				SNDRV_PCM_INFO_SYNC_START,
	.formats =		SNDRV_PCM_FMTBIT_S32_LE, /* Only supports 24-bit samples padded to 32 bits. */
	.rates =		SNDRV_PCM_RATE_192000 | SNDRV_PCM_RATE_96000 | SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_44100, 
	.rate_min =		44100,
	.rate_max =		192000,
	.channels_min =		8, 
	.channels_max =		8,
	.buffer_bytes_max =	((65536 - 64) * 8),
	.period_bytes_min =	64,
	.period_bytes_max =	(65536 - 64),
	.periods_min =		2,
	.periods_max =		8,
	.fifo_size =		0,
};

static struct snd_pcm_hardware snd_p16v_capture_hw = {
	.info =			(SNDRV_PCM_INFO_MMAP |
				 SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_BLOCK_TRANSFER |
				 SNDRV_PCM_INFO_RESUME |
				 SNDRV_PCM_INFO_MMAP_VALID),
	.formats =		SNDRV_PCM_FMTBIT_S32_LE,
	.rates =		SNDRV_PCM_RATE_192000 | SNDRV_PCM_RATE_96000 | SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_44100, 
	.rate_min =		44100,
	.rate_max =		192000,
	.channels_min =		2,
	.channels_max =		2,
	.buffer_bytes_max =	(65536 - 64),
	.period_bytes_min =	64,
	.period_bytes_max =	(65536 - 128) >> 1,  /* size has to be N*64 bytes */
	.periods_min =		2,
	.periods_max =		2,
	.fifo_size =		0,
};

static void snd_p16v_pcm_free_substream(struct snd_pcm_runtime *runtime)
{
	struct snd_emu10k1_pcm *epcm = runtime->private_data;
  
	if (epcm) {
        	/* snd_printk(KERN_DEBUG "epcm free: %p\n", epcm); */
		kfree(epcm);
	}
}

/* open_playback callback */
static int snd_p16v_pcm_open_playback_channel(struct snd_pcm_substream *substream, int channel_id)
{
	struct snd_emu10k1 *emu = snd_pcm_substream_chip(substream);
        struct snd_emu10k1_voice *channel = &(emu->p16v_voices[channel_id]);
	struct snd_emu10k1_pcm *epcm;
	struct snd_pcm_runtime *runtime = substream->runtime;
	int err;

	epcm = kzalloc(sizeof(*epcm), GFP_KERNEL);
        /* snd_printk(KERN_DEBUG "epcm kcalloc: %p\n", epcm); */

	if (epcm == NULL)
		return -ENOMEM;
	epcm->emu = emu;
	epcm->substream = substream;
	/*
	snd_printk(KERN_DEBUG "epcm device=%d, channel_id=%d\n",
		   substream->pcm->device, channel_id);
	*/
	runtime->private_data = epcm;
	runtime->private_free = snd_p16v_pcm_free_substream;
  
	runtime->hw = snd_p16v_playback_hw;

        channel->emu = emu;
        channel->number = channel_id;

        channel->use=1;
#if 0 /* debug */
	snd_printk(KERN_DEBUG
		   "p16v: open channel_id=%d, channel=%p, use=0x%x\n",
		   channel_id, channel, channel->use);
	printk(KERN_DEBUG "open:channel_id=%d, chip=%p, channel=%p\n",
	       channel_id, chip, channel);
#endif /* debug */
	/* channel->interrupt = snd_p16v_pcm_channel_interrupt; */
	channel->epcm = epcm;
	if ((err = snd_pcm_hw_constraint_integer(runtime, SNDRV_PCM_HW_PARAM_PERIODS)) < 0)
                return err;

	runtime->sync.id32[0] = substream->pcm->card->number;
	runtime->sync.id32[1] = 'P';
	runtime->sync.id32[2] = 16;
	runtime->sync.id32[3] = 'V';

	return 0;
}
/* open_capture callback */
static int snd_p16v_pcm_open_capture_channel(struct snd_pcm_substream *substream, int channel_id)
{
	struct snd_emu10k1 *emu = snd_pcm_substream_chip(substream);
	struct snd_emu10k1_voice *channel = &(emu->p16v_capture_voice);
	struct snd_emu10k1_pcm *epcm;
	struct snd_pcm_runtime *runtime = substream->runtime;
	int err;

	epcm = kzalloc(sizeof(*epcm), GFP_KERNEL);
	/* snd_printk(KERN_DEBUG "epcm kcalloc: %p\n", epcm); */

	if (epcm == NULL)
		return -ENOMEM;
	epcm->emu = emu;
	epcm->substream = substream;
	/*
	snd_printk(KERN_DEBUG "epcm device=%d, channel_id=%d\n",
		   substream->pcm->device, channel_id);
	*/
	runtime->private_data = epcm;
	runtime->private_free = snd_p16v_pcm_free_substream;
  
	runtime->hw = snd_p16v_capture_hw;

	channel->emu = emu;
	channel->number = channel_id;

	channel->use=1;
#if 0 /* debug */
	snd_printk(KERN_DEBUG
		   "p16v: open channel_id=%d, channel=%p, use=0x%x\n",
		   channel_id, channel, channel->use);
	printk(KERN_DEBUG "open:channel_id=%d, chip=%p, channel=%p\n",
	       channel_id, chip, channel);
#endif /* debug */
	/* channel->interrupt = snd_p16v_pcm_channel_interrupt; */
	channel->epcm = epcm;
	if ((err = snd_pcm_hw_constraint_integer(runtime, SNDRV_PCM_HW_PARAM_PERIODS)) < 0)
		return err;

	return 0;
}


/* close callback */
static int snd_p16v_pcm_close_playback(struct snd_pcm_substream *substream)
{
	struct snd_emu10k1 *emu = snd_pcm_substream_chip(substream);
	//struct snd_pcm_runtime *runtime = substream->runtime;
	//struct snd_emu10k1_pcm *epcm = runtime->private_data;
	emu->p16v_voices[substream->pcm->device - emu->p16v_device_offset].use = 0;
	/* FIXME: maybe zero others */
	return 0;
}

/* close callback */
static int snd_p16v_pcm_close_capture(struct snd_pcm_substream *substream)
{
	struct snd_emu10k1 *emu = snd_pcm_substream_chip(substream);
	//struct snd_pcm_runtime *runtime = substream->runtime;
	//struct snd_emu10k1_pcm *epcm = runtime->private_data;
	emu->p16v_capture_voice.use = 0;
	/* FIXME: maybe zero others */
	return 0;
}

static int snd_p16v_pcm_open_playback_front(struct snd_pcm_substream *substream)
{
	return snd_p16v_pcm_open_playback_channel(substream, PCM_FRONT_CHANNEL);
}

static int snd_p16v_pcm_open_capture(struct snd_pcm_substream *substream)
{
	// Only using channel 0 for now, but the card has 2 channels.
	return snd_p16v_pcm_open_capture_channel(substream, 0);
}

/* hw_params callback */
static int snd_p16v_pcm_hw_params_playback(struct snd_pcm_substream *substream,
				      struct snd_pcm_hw_params *hw_params)
{
	int result;
	result = snd_pcm_lib_malloc_pages(substream,
					params_buffer_bytes(hw_params));
	return result;
}

/* hw_params callback */
static int snd_p16v_pcm_hw_params_capture(struct snd_pcm_substream *substream,
				      struct snd_pcm_hw_params *hw_params)
{
	int result;
	result = snd_pcm_lib_malloc_pages(substream,
					params_buffer_bytes(hw_params));
	return result;
}


/* hw_free callback */
static int snd_p16v_pcm_hw_free_playback(struct snd_pcm_substream *substream)
{
	int result;
	result = snd_pcm_lib_free_pages(substream);
	return result;
}

/* hw_free callback */
static int snd_p16v_pcm_hw_free_capture(struct snd_pcm_substream *substream)
{
	int result;
	result = snd_pcm_lib_free_pages(substream);
	return result;
}


/* prepare playback callback */
static int snd_p16v_pcm_prepare_playback(struct snd_pcm_substream *substream)
{
	struct snd_emu10k1 *emu = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	int channel = substream->pcm->device - emu->p16v_device_offset;
	u32 *table_base = (u32 *)(emu->p16v_buffer.area+(8*16*channel));
	u32 period_size_bytes = frames_to_bytes(runtime, runtime->period_size);
	int i;
	u32 tmp;
	
#if 0 /* debug */
	snd_printk(KERN_DEBUG "prepare:channel_number=%d, rate=%d, "
		   "format=0x%x, channels=%d, buffer_size=%ld, "
		   "period_size=%ld, periods=%u, frames_to_bytes=%d\n",
		   channel, runtime->rate, runtime->format, runtime->channels,
		   runtime->buffer_size, runtime->period_size,
		   runtime->periods, frames_to_bytes(runtime, 1));
	snd_printk(KERN_DEBUG "dma_addr=%x, dma_area=%p, table_base=%p\n",
		   runtime->dma_addr, runtime->dma_area, table_base);
	snd_printk(KERN_DEBUG "dma_addr=%x, dma_area=%p, dma_bytes(size)=%x\n",
		   emu->p16v_buffer.addr, emu->p16v_buffer.area,
		   emu->p16v_buffer.bytes);
#endif /* debug */
	tmp = snd_emu10k1_ptr_read(emu, A_SPDIF_SAMPLERATE, channel);
        switch (runtime->rate) {
	case 44100:
	  snd_emu10k1_ptr_write(emu, A_SPDIF_SAMPLERATE, channel, (tmp & ~0xe0e0) | 0x8080);
	  break;
	case 96000:
	  snd_emu10k1_ptr_write(emu, A_SPDIF_SAMPLERATE, channel, (tmp & ~0xe0e0) | 0x4040);
	  break;
	case 192000:
	  snd_emu10k1_ptr_write(emu, A_SPDIF_SAMPLERATE, channel, (tmp & ~0xe0e0) | 0x2020);
	  break;
	case 48000:
	default:
	  snd_emu10k1_ptr_write(emu, A_SPDIF_SAMPLERATE, channel, (tmp & ~0xe0e0) | 0x0000);
	  break;
	}
	/* FIXME: Check emu->buffer.size before actually writing to it. */
	for(i = 0; i < runtime->periods; i++) {
		table_base[i*2]=runtime->dma_addr+(i*period_size_bytes);
		table_base[(i*2)+1]=period_size_bytes<<16;
	}
 
	snd_emu10k1_ptr20_write(emu, PLAYBACK_LIST_ADDR, channel, emu->p16v_buffer.addr+(8*16*channel));
	snd_emu10k1_ptr20_write(emu, PLAYBACK_LIST_SIZE, channel, (runtime->periods - 1) << 19);
	snd_emu10k1_ptr20_write(emu, PLAYBACK_LIST_PTR, channel, 0);
	snd_emu10k1_ptr20_write(emu, PLAYBACK_DMA_ADDR, channel, runtime->dma_addr);
	//snd_emu10k1_ptr20_write(emu, PLAYBACK_PERIOD_SIZE, channel, frames_to_bytes(runtime, runtime->period_size)<<16); // buffer size in bytes
	snd_emu10k1_ptr20_write(emu, PLAYBACK_PERIOD_SIZE, channel, 0); // buffer size in bytes
	snd_emu10k1_ptr20_write(emu, PLAYBACK_POINTER, channel, 0);
	snd_emu10k1_ptr20_write(emu, 0x07, channel, 0x0);
	snd_emu10k1_ptr20_write(emu, 0x08, channel, 0);

	return 0;
}

/* prepare capture callback */
static int snd_p16v_pcm_prepare_capture(struct snd_pcm_substream *substream)
{
	struct snd_emu10k1 *emu = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	int channel = substream->pcm->device - emu->p16v_device_offset;
	u32 tmp;

	/*
	printk(KERN_DEBUG "prepare capture:channel_number=%d, rate=%d, "
	       "format=0x%x, channels=%d, buffer_size=%ld, period_size=%ld, "
	       "frames_to_bytes=%d\n",
	       channel, runtime->rate, runtime->format, runtime->channels,
	       runtime->buffer_size, runtime->period_size,
	       frames_to_bytes(runtime, 1));
	*/
	tmp = snd_emu10k1_ptr_read(emu, A_SPDIF_SAMPLERATE, channel);
        switch (runtime->rate) {
	case 44100:
	  snd_emu10k1_ptr_write(emu, A_SPDIF_SAMPLERATE, channel, (tmp & ~0x0e00) | 0x0800);
	  break;
	case 96000:
	  snd_emu10k1_ptr_write(emu, A_SPDIF_SAMPLERATE, channel, (tmp & ~0x0e00) | 0x0400);
	  break;
	case 192000:
	  snd_emu10k1_ptr_write(emu, A_SPDIF_SAMPLERATE, channel, (tmp & ~0x0e00) | 0x0200);
	  break;
	case 48000:
	default:
	  snd_emu10k1_ptr_write(emu, A_SPDIF_SAMPLERATE, channel, (tmp & ~0x0e00) | 0x0000);
	  break;
	}
	/* FIXME: Check emu->buffer.size before actually writing to it. */
	snd_emu10k1_ptr20_write(emu, 0x13, channel, 0);
	snd_emu10k1_ptr20_write(emu, CAPTURE_DMA_ADDR, channel, runtime->dma_addr);
	snd_emu10k1_ptr20_write(emu, CAPTURE_BUFFER_SIZE, channel, frames_to_bytes(runtime, runtime->buffer_size) << 16); // buffer size in bytes
	snd_emu10k1_ptr20_write(emu, CAPTURE_POINTER, channel, 0);
	//snd_emu10k1_ptr20_write(emu, CAPTURE_SOURCE, 0x0, 0x333300e4); /* Select MIC or Line in */
	//snd_emu10k1_ptr20_write(emu, EXTENDED_INT_MASK, 0, snd_emu10k1_ptr20_read(emu, EXTENDED_INT_MASK, 0) | (0x110000<<channel));

	return 0;
}

static void snd_p16v_intr_enable(struct snd_emu10k1 *emu, unsigned int intrenb)
{
	unsigned long flags;
	unsigned int enable;

	spin_lock_irqsave(&emu->emu_lock, flags);
	enable = inl(emu->port + INTE2) | intrenb;
	outl(enable, emu->port + INTE2);
	spin_unlock_irqrestore(&emu->emu_lock, flags);
}

static void snd_p16v_intr_disable(struct snd_emu10k1 *emu, unsigned int intrenb)
{
	unsigned long flags;
	unsigned int disable;

	spin_lock_irqsave(&emu->emu_lock, flags);
	disable = inl(emu->port + INTE2) & (~intrenb);
	outl(disable, emu->port + INTE2);
	spin_unlock_irqrestore(&emu->emu_lock, flags);
}

/* trigger_playback callback */
static int snd_p16v_pcm_trigger_playback(struct snd_pcm_substream *substream,
				    int cmd)
{
	struct snd_emu10k1 *emu = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime;
	struct snd_emu10k1_pcm *epcm;
	int channel;
	int result = 0;
        struct snd_pcm_substream *s;
	u32 basic = 0;
	u32 inte = 0;
	int running = 0;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		running=1;
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	default:
		running = 0;
		break;
	}
        snd_pcm_group_for_each_entry(s, substream) {
		if (snd_pcm_substream_chip(s) != emu ||
		    s->stream != SNDRV_PCM_STREAM_PLAYBACK)
			continue;
		runtime = s->runtime;
		epcm = runtime->private_data;
		channel = substream->pcm->device-emu->p16v_device_offset;
		/* snd_printk(KERN_DEBUG "p16v channel=%d\n", channel); */
		epcm->running = running;
		basic |= (0x1<<channel);
		inte |= (INTE2_PLAYBACK_CH_0_LOOP<<channel);
                snd_pcm_trigger_done(s, substream);
        }
	/* snd_printk(KERN_DEBUG "basic=0x%x, inte=0x%x\n", basic, inte); */

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		snd_p16v_intr_enable(emu, inte);
		snd_emu10k1_ptr20_write(emu, BASIC_INTERRUPT, 0, snd_emu10k1_ptr20_read(emu, BASIC_INTERRUPT, 0)| (basic));
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		snd_emu10k1_ptr20_write(emu, BASIC_INTERRUPT, 0, snd_emu10k1_ptr20_read(emu, BASIC_INTERRUPT, 0) & ~(basic));
		snd_p16v_intr_disable(emu, inte);
		break;
	default:
		result = -EINVAL;
		break;
	}
	return result;
}

/* trigger_capture callback */
static int snd_p16v_pcm_trigger_capture(struct snd_pcm_substream *substream,
                                   int cmd)
{
	struct snd_emu10k1 *emu = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_emu10k1_pcm *epcm = runtime->private_data;
	int channel = 0;
	int result = 0;
	u32 inte = INTE2_CAPTURE_CH_0_LOOP | INTE2_CAPTURE_CH_0_HALF_LOOP;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		snd_p16v_intr_enable(emu, inte);
		snd_emu10k1_ptr20_write(emu, BASIC_INTERRUPT, 0, snd_emu10k1_ptr20_read(emu, BASIC_INTERRUPT, 0)|(0x100<<channel));
		epcm->running = 1;
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		snd_emu10k1_ptr20_write(emu, BASIC_INTERRUPT, 0, snd_emu10k1_ptr20_read(emu, BASIC_INTERRUPT, 0) & ~(0x100<<channel));
		snd_p16v_intr_disable(emu, inte);
		//snd_emu10k1_ptr20_write(emu, EXTENDED_INT_MASK, 0, snd_emu10k1_ptr20_read(emu, EXTENDED_INT_MASK, 0) & ~(0x110000<<channel));
		epcm->running = 0;
		break;
	default:
		result = -EINVAL;
		break;
	}
	return result;
}

/* pointer_playback callback */
static snd_pcm_uframes_t
snd_p16v_pcm_pointer_playback(struct snd_pcm_substream *substream)
{
	struct snd_emu10k1 *emu = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_emu10k1_pcm *epcm = runtime->private_data;
	snd_pcm_uframes_t ptr, ptr1, ptr2,ptr3,ptr4 = 0;
	int channel = substream->pcm->device - emu->p16v_device_offset;
	if (!epcm->running)
		return 0;

	ptr3 = snd_emu10k1_ptr20_read(emu, PLAYBACK_LIST_PTR, channel);
	ptr1 = snd_emu10k1_ptr20_read(emu, PLAYBACK_POINTER, channel);
	ptr4 = snd_emu10k1_ptr20_read(emu, PLAYBACK_LIST_PTR, channel);
	if (ptr3 != ptr4) ptr1 = snd_emu10k1_ptr20_read(emu, PLAYBACK_POINTER, channel);
	ptr2 = bytes_to_frames(runtime, ptr1);
	ptr2+= (ptr4 >> 3) * runtime->period_size;
	ptr=ptr2;
        if (ptr >= runtime->buffer_size)
		ptr -= runtime->buffer_size;

	return ptr;
}

/* pointer_capture callback */
static snd_pcm_uframes_t
snd_p16v_pcm_pointer_capture(struct snd_pcm_substream *substream)
{
	struct snd_emu10k1 *emu = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_emu10k1_pcm *epcm = runtime->private_data;
	snd_pcm_uframes_t ptr, ptr1, ptr2 = 0;
	int channel = 0;

	if (!epcm->running)
		return 0;

	ptr1 = snd_emu10k1_ptr20_read(emu, CAPTURE_POINTER, channel);
	ptr2 = bytes_to_frames(runtime, ptr1);
	ptr=ptr2;
	if (ptr >= runtime->buffer_size) {
		ptr -= runtime->buffer_size;
		printk(KERN_WARNING "buffer capture limited!\n");
	}
	/*
	printk(KERN_DEBUG "ptr1 = 0x%lx, ptr2=0x%lx, ptr=0x%lx, "
	       "buffer_size = 0x%x, period_size = 0x%x, bits=%d, rate=%d\n",
	       ptr1, ptr2, ptr, (int)runtime->buffer_size,
	       (int)runtime->period_size, (int)runtime->frame_bits,
	       (int)runtime->rate);
	*/
	return ptr;
}

/* operators */
static struct snd_pcm_ops snd_p16v_playback_front_ops = {
	.open =        snd_p16v_pcm_open_playback_front,
	.close =       snd_p16v_pcm_close_playback,
	.ioctl =       snd_pcm_lib_ioctl,
	.hw_params =   snd_p16v_pcm_hw_params_playback,
	.hw_free =     snd_p16v_pcm_hw_free_playback,
	.prepare =     snd_p16v_pcm_prepare_playback,
	.trigger =     snd_p16v_pcm_trigger_playback,
	.pointer =     snd_p16v_pcm_pointer_playback,
};

static struct snd_pcm_ops snd_p16v_capture_ops = {
	.open =        snd_p16v_pcm_open_capture,
	.close =       snd_p16v_pcm_close_capture,
	.ioctl =       snd_pcm_lib_ioctl,
	.hw_params =   snd_p16v_pcm_hw_params_capture,
	.hw_free =     snd_p16v_pcm_hw_free_capture,
	.prepare =     snd_p16v_pcm_prepare_capture,
	.trigger =     snd_p16v_pcm_trigger_capture,
	.pointer =     snd_p16v_pcm_pointer_capture,
};


int snd_p16v_free(struct snd_emu10k1 *chip)
{
	// release the data
	if (chip->p16v_buffer.area) {
		snd_dma_free_pages(&chip->p16v_buffer);
		/*
		snd_printk(KERN_DEBUG "period lables free: %p\n",
			   &chip->p16v_buffer);
		*/
	}
	return 0;
}

int __devinit snd_p16v_pcm(struct snd_emu10k1 *emu, int device, struct snd_pcm **rpcm)
{
	struct snd_pcm *pcm;
	struct snd_pcm_substream *substream;
	int err;
        int capture=1;
  
	/* snd_printk(KERN_DEBUG "snd_p16v_pcm called. device=%d\n", device); */
	emu->p16v_device_offset = device;
	if (rpcm)
		*rpcm = NULL;

	if ((err = snd_pcm_new(emu->card, "p16v", device, 1, capture, &pcm)) < 0)
		return err;
  
	pcm->private_data = emu;
	// Single playback 8 channel device.
	// Single capture 2 channel device.
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_p16v_playback_front_ops);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_p16v_capture_ops);

	pcm->info_flags = 0;
	pcm->dev_subclass = SNDRV_PCM_SUBCLASS_GENERIC_MIX;
	strcpy(pcm->name, "p16v");
	emu->pcm_p16v = pcm;

	for(substream = pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream; 
	    substream; 
	    substream = substream->next) {
		if ((err = snd_pcm_lib_preallocate_pages(substream, 
							 SNDRV_DMA_TYPE_DEV, 
							 snd_dma_pci_data(emu->pci), 
							 ((65536 - 64) * 8), ((65536 - 64) * 8))) < 0) 
			return err;
		/*
		snd_printk(KERN_DEBUG
			   "preallocate playback substream: err=%d\n", err);
		*/
	}

	for (substream = pcm->streams[SNDRV_PCM_STREAM_CAPTURE].substream; 
	      substream; 
	      substream = substream->next) {
 		if ((err = snd_pcm_lib_preallocate_pages(substream, 
	                                           SNDRV_DMA_TYPE_DEV, 
	                                           snd_dma_pci_data(emu->pci), 
	                                           65536 - 64, 65536 - 64)) < 0)
			return err;
		/*
		snd_printk(KERN_DEBUG
			   "preallocate capture substream: err=%d\n", err);
		*/
	}
  
	if (rpcm)
		*rpcm = pcm;
  
	return 0;
}

static int snd_p16v_volume_info(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_info *uinfo)
{
        uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
        uinfo->count = 2;
        uinfo->value.integer.min = 0;
        uinfo->value.integer.max = 255;
        return 0;
}

static int snd_p16v_volume_get(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
        struct snd_emu10k1 *emu = snd_kcontrol_chip(kcontrol);
	int high_low = (kcontrol->private_value >> 8) & 0xff;
	int reg = kcontrol->private_value & 0xff;
	u32 value;

	value = snd_emu10k1_ptr20_read(emu, reg, high_low);
	if (high_low) {
		ucontrol->value.integer.value[0] = 0xff - ((value >> 24) & 0xff); /* Left */
		ucontrol->value.integer.value[1] = 0xff - ((value >> 16) & 0xff); /* Right */
	} else {
		ucontrol->value.integer.value[0] = 0xff - ((value >> 8) & 0xff); /* Left */
		ucontrol->value.integer.value[1] = 0xff - ((value >> 0) & 0xff); /* Right */
	}
	return 0;
}

static int snd_p16v_volume_put(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
        struct snd_emu10k1 *emu = snd_kcontrol_chip(kcontrol);
	int high_low = (kcontrol->private_value >> 8) & 0xff;
	int reg = kcontrol->private_value & 0xff;
        u32 value, oval;

	oval = value = snd_emu10k1_ptr20_read(emu, reg, 0);
	if (high_low == 1) {
		value &= 0xffff;
		value |= ((0xff - ucontrol->value.integer.value[0]) << 24) |
			((0xff - ucontrol->value.integer.value[1]) << 16);
	} else {
		value &= 0xffff0000;
		value |= ((0xff - ucontrol->value.integer.value[0]) << 8) |
			((0xff - ucontrol->value.integer.value[1]) );
	}
	if (value != oval) {
		snd_emu10k1_ptr20_write(emu, reg, 0, value);
		return 1;
	}
	return 0;
}

static int snd_p16v_capture_source_info(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_info *uinfo)
{
	static char *texts[8] = {
		"SPDIF", "I2S", "SRC48", "SRCMulti_SPDIF", "SRCMulti_I2S",
		"CDIF", "FX", "AC97"
	};

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 8;
	if (uinfo->value.enumerated.item > 7)
                uinfo->value.enumerated.item = 7;
	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}

static int snd_p16v_capture_source_get(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct snd_emu10k1 *emu = snd_kcontrol_chip(kcontrol);

	ucontrol->value.enumerated.item[0] = emu->p16v_capture_source;
	return 0;
}

static int snd_p16v_capture_source_put(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct snd_emu10k1 *emu = snd_kcontrol_chip(kcontrol);
	unsigned int val;
	int change = 0;
	u32 mask;
	u32 source;

	val = ucontrol->value.enumerated.item[0] ;
	if (val > 7)
		return -EINVAL;
	change = (emu->p16v_capture_source != val);
	if (change) {
		emu->p16v_capture_source = val;
		source = (val << 28) | (val << 24) | (val << 20) | (val << 16);
		mask = snd_emu10k1_ptr20_read(emu, BASIC_INTERRUPT, 0) & 0xffff;
		snd_emu10k1_ptr20_write(emu, BASIC_INTERRUPT, 0, source | mask);
	}
        return change;
}

static int snd_p16v_capture_channel_info(struct snd_kcontrol *kcontrol,
					 struct snd_ctl_elem_info *uinfo)
{
	static char *texts[4] = { "0", "1", "2", "3",  };

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 4;
	if (uinfo->value.enumerated.item > 3)
                uinfo->value.enumerated.item = 3;
	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}

static int snd_p16v_capture_channel_get(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct snd_emu10k1 *emu = snd_kcontrol_chip(kcontrol);

	ucontrol->value.enumerated.item[0] = emu->p16v_capture_channel;
	return 0;
}

static int snd_p16v_capture_channel_put(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct snd_emu10k1 *emu = snd_kcontrol_chip(kcontrol);
	unsigned int val;
	int change = 0;
	u32 tmp;

	val = ucontrol->value.enumerated.item[0] ;
	if (val > 3)
		return -EINVAL;
	change = (emu->p16v_capture_channel != val);
	if (change) {
		emu->p16v_capture_channel = val;
		tmp = snd_emu10k1_ptr20_read(emu, CAPTURE_P16V_SOURCE, 0) & 0xfffc;
		snd_emu10k1_ptr20_write(emu, CAPTURE_P16V_SOURCE, 0, tmp | val);
	}
        return change;
}
static const DECLARE_TLV_DB_SCALE(snd_p16v_db_scale1, -5175, 25, 1);

#define P16V_VOL(xname,xreg,xhl) { \
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, \
        .access = SNDRV_CTL_ELEM_ACCESS_READWRITE |             \
                  SNDRV_CTL_ELEM_ACCESS_TLV_READ,               \
	.info = snd_p16v_volume_info, \
	.get = snd_p16v_volume_get, \
	.put = snd_p16v_volume_put, \
	.tlv = { .p = snd_p16v_db_scale1 },	\
	.private_value = ((xreg) | ((xhl) << 8)) \
}

static struct snd_kcontrol_new p16v_mixer_controls[] __devinitdata = {
	P16V_VOL("HD Analog Front Playback Volume", PLAYBACK_VOLUME_MIXER9, 0),
	P16V_VOL("HD Analog Rear Playback Volume", PLAYBACK_VOLUME_MIXER10, 1),
	P16V_VOL("HD Analog Center/LFE Playback Volume", PLAYBACK_VOLUME_MIXER9, 1),
	P16V_VOL("HD Analog Side Playback Volume", PLAYBACK_VOLUME_MIXER10, 0),
	P16V_VOL("HD SPDIF Front Playback Volume", PLAYBACK_VOLUME_MIXER7, 0),
	P16V_VOL("HD SPDIF Rear Playback Volume", PLAYBACK_VOLUME_MIXER8, 1),
	P16V_VOL("HD SPDIF Center/LFE Playback Volume", PLAYBACK_VOLUME_MIXER7, 1),
	P16V_VOL("HD SPDIF Side Playback Volume", PLAYBACK_VOLUME_MIXER8, 0),
	{
		.iface =	SNDRV_CTL_ELEM_IFACE_MIXER,
		.name =		"HD source Capture",
		.info =		snd_p16v_capture_source_info,
		.get =		snd_p16v_capture_source_get,
		.put =		snd_p16v_capture_source_put
	},
	{
		.iface =	SNDRV_CTL_ELEM_IFACE_MIXER,
		.name =		"HD channel Capture",
		.info =		snd_p16v_capture_channel_info,
		.get =		snd_p16v_capture_channel_get,
		.put =		snd_p16v_capture_channel_put
	},
};


int __devinit snd_p16v_mixer(struct snd_emu10k1 *emu)
{
	int i, err;
        struct snd_card *card = emu->card;

	for (i = 0; i < ARRAY_SIZE(p16v_mixer_controls); i++) {
		if ((err = snd_ctl_add(card, snd_ctl_new1(&p16v_mixer_controls[i],
							  emu))) < 0)
			return err;
	}
        return 0;
}

#ifdef CONFIG_PM

#define NUM_CHS	1	/* up to 4, but only first channel is used */

int __devinit snd_p16v_alloc_pm_buffer(struct snd_emu10k1 *emu)
{
	emu->p16v_saved = vmalloc(NUM_CHS * 4 * 0x80);
	if (! emu->p16v_saved)
		return -ENOMEM;
	return 0;
}

void snd_p16v_free_pm_buffer(struct snd_emu10k1 *emu)
{
	vfree(emu->p16v_saved);
}

void snd_p16v_suspend(struct snd_emu10k1 *emu)
{
	int i, ch;
	unsigned int *val;

	val = emu->p16v_saved;
	for (ch = 0; ch < NUM_CHS; ch++)
		for (i = 0; i < 0x80; i++, val++)
			*val = snd_emu10k1_ptr20_read(emu, i, ch);
}

void snd_p16v_resume(struct snd_emu10k1 *emu)
{
	int i, ch;
	unsigned int *val;

	val = emu->p16v_saved;
	for (ch = 0; ch < NUM_CHS; ch++)
		for (i = 0; i < 0x80; i++, val++)
			snd_emu10k1_ptr20_write(emu, i, ch, *val);
}
#endif
