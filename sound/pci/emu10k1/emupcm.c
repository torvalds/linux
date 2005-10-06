/*
 *  Copyright (c) by Jaroslav Kysela <perex@suse.cz>
 *                   Creative Labs, Inc.
 *  Routines for control of EMU10K1 chips / PCM routines
 *  Multichannel PCM support Copyright (c) Lee Revell <rlrevell@joe-job.com>
 *
 *  BUGS:
 *    --
 *
 *  TODO:
 *    --
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

#include <sound/driver.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <linux/init.h>
#include <sound/core.h>
#include <sound/emu10k1.h>

static void snd_emu10k1_pcm_interrupt(emu10k1_t *emu, emu10k1_voice_t *voice)
{
	emu10k1_pcm_t *epcm;

	if ((epcm = voice->epcm) == NULL)
		return;
	if (epcm->substream == NULL)
		return;
#if 0
	printk("IRQ: position = 0x%x, period = 0x%x, size = 0x%x\n",
			epcm->substream->runtime->hw->pointer(emu, epcm->substream),
			snd_pcm_lib_period_bytes(epcm->substream),
			snd_pcm_lib_buffer_bytes(epcm->substream));
#endif
	snd_pcm_period_elapsed(epcm->substream);
}

static void snd_emu10k1_pcm_ac97adc_interrupt(emu10k1_t *emu, unsigned int status)
{
#if 0
	if (status & IPR_ADCBUFHALFFULL) {
		if (emu->pcm_capture_substream->runtime->mode == SNDRV_PCM_MODE_FRAME)
			return;
	}
#endif
	snd_pcm_period_elapsed(emu->pcm_capture_substream);
}

static void snd_emu10k1_pcm_ac97mic_interrupt(emu10k1_t *emu, unsigned int status)
{
#if 0
	if (status & IPR_MICBUFHALFFULL) {
		if (emu->pcm_capture_mic_substream->runtime->mode == SNDRV_PCM_MODE_FRAME)
			return;
	}
#endif
	snd_pcm_period_elapsed(emu->pcm_capture_mic_substream);
}

static void snd_emu10k1_pcm_efx_interrupt(emu10k1_t *emu, unsigned int status)
{
#if 0
	if (status & IPR_EFXBUFHALFFULL) {
		if (emu->pcm_capture_efx_substream->runtime->mode == SNDRV_PCM_MODE_FRAME)
			return;
	}
#endif
	snd_pcm_period_elapsed(emu->pcm_capture_efx_substream);
}	 

static snd_pcm_uframes_t snd_emu10k1_efx_playback_pointer(snd_pcm_substream_t * substream)
{
	emu10k1_t *emu = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	emu10k1_pcm_t *epcm = runtime->private_data;
	unsigned int ptr;

	if (!epcm->running)
		return 0;
	ptr = snd_emu10k1_ptr_read(emu, CCCA, epcm->voices[0]->number) & 0x00ffffff;
	ptr += runtime->buffer_size;
	ptr -= epcm->ccca_start_addr;
	ptr %= runtime->buffer_size;

	return ptr;
}

static int snd_emu10k1_pcm_channel_alloc(emu10k1_pcm_t * epcm, int voices)
{
	int err, i;

	if (epcm->voices[1] != NULL && voices < 2) {
		snd_emu10k1_voice_free(epcm->emu, epcm->voices[1]);
		epcm->voices[1] = NULL;
	}
	for (i = 0; i < voices; i++) {
		if (epcm->voices[i] == NULL)
			break;
	}
	if (i == voices)
		return 0; /* already allocated */

	for (i = 0; i < ARRAY_SIZE(epcm->voices); i++) {
		if (epcm->voices[i]) {
			snd_emu10k1_voice_free(epcm->emu, epcm->voices[i]);
			epcm->voices[i] = NULL;
		}
	}
	err = snd_emu10k1_voice_alloc(epcm->emu,
				      epcm->type == PLAYBACK_EMUVOICE ? EMU10K1_PCM : EMU10K1_EFX,
				      voices,
				      &epcm->voices[0]);
	
	if (err < 0)
		return err;
	epcm->voices[0]->epcm = epcm;
	if (voices > 1) {
		for (i = 1; i < voices; i++) {
			epcm->voices[i] = &epcm->emu->voices[epcm->voices[0]->number + i];
			epcm->voices[i]->epcm = epcm;
		}
	}
	if (epcm->extra == NULL) {
		err = snd_emu10k1_voice_alloc(epcm->emu,
					      epcm->type == PLAYBACK_EMUVOICE ? EMU10K1_PCM : EMU10K1_EFX,
					      1,
					      &epcm->extra);
		if (err < 0) {
			// printk("pcm_channel_alloc: failed extra: voices=%d, frame=%d\n", voices, frame);
			for (i = 0; i < voices; i++) {
				snd_emu10k1_voice_free(epcm->emu, epcm->voices[i]);
				epcm->voices[i] = NULL;
			}
			return err;
		}
		epcm->extra->epcm = epcm;
		epcm->extra->interrupt = snd_emu10k1_pcm_interrupt;
	}
	return 0;
}

static unsigned int capture_period_sizes[31] = {
	384,	448,	512,	640,
	384*2,	448*2,	512*2,	640*2,
	384*4,	448*4,	512*4,	640*4,
	384*8,	448*8,	512*8,	640*8,
	384*16,	448*16,	512*16,	640*16,
	384*32,	448*32,	512*32,	640*32,
	384*64,	448*64,	512*64,	640*64,
	384*128,448*128,512*128
};

static snd_pcm_hw_constraint_list_t hw_constraints_capture_period_sizes = {
	.count = 31,
	.list = capture_period_sizes,
	.mask = 0
};

static unsigned int capture_rates[8] = {
	8000, 11025, 16000, 22050, 24000, 32000, 44100, 48000
};

static snd_pcm_hw_constraint_list_t hw_constraints_capture_rates = {
	.count = 8,
	.list = capture_rates,
	.mask = 0
};

static unsigned int snd_emu10k1_capture_rate_reg(unsigned int rate)
{
	switch (rate) {
	case 8000:	return ADCCR_SAMPLERATE_8;
	case 11025:	return ADCCR_SAMPLERATE_11;
	case 16000:	return ADCCR_SAMPLERATE_16;
	case 22050:	return ADCCR_SAMPLERATE_22;
	case 24000:	return ADCCR_SAMPLERATE_24;
	case 32000:	return ADCCR_SAMPLERATE_32;
	case 44100:	return ADCCR_SAMPLERATE_44;
	case 48000:	return ADCCR_SAMPLERATE_48;
	default:
			snd_BUG();
			return ADCCR_SAMPLERATE_8;
	}
}

static unsigned int snd_emu10k1_audigy_capture_rate_reg(unsigned int rate)
{
	switch (rate) {
	case 8000:	return A_ADCCR_SAMPLERATE_8;
	case 11025:	return A_ADCCR_SAMPLERATE_11;
	case 12000:	return A_ADCCR_SAMPLERATE_12; /* really supported? */
	case 16000:	return ADCCR_SAMPLERATE_16;
	case 22050:	return ADCCR_SAMPLERATE_22;
	case 24000:	return ADCCR_SAMPLERATE_24;
	case 32000:	return ADCCR_SAMPLERATE_32;
	case 44100:	return ADCCR_SAMPLERATE_44;
	case 48000:	return ADCCR_SAMPLERATE_48;
	default:
			snd_BUG();
			return A_ADCCR_SAMPLERATE_8;
	}
}

static unsigned int emu10k1_calc_pitch_target(unsigned int rate)
{
	unsigned int pitch_target;

	pitch_target = (rate << 8) / 375;
	pitch_target = (pitch_target >> 1) + (pitch_target & 1);
	return pitch_target;
}

#define PITCH_48000 0x00004000
#define PITCH_96000 0x00008000
#define PITCH_85000 0x00007155
#define PITCH_80726 0x00006ba2
#define PITCH_67882 0x00005a82
#define PITCH_57081 0x00004c1c

static unsigned int emu10k1_select_interprom(unsigned int pitch_target)
{
	if (pitch_target == PITCH_48000)
		return CCCA_INTERPROM_0;
	else if (pitch_target < PITCH_48000)
		return CCCA_INTERPROM_1;
	else if (pitch_target >= PITCH_96000)
		return CCCA_INTERPROM_0;
	else if (pitch_target >= PITCH_85000)
		return CCCA_INTERPROM_6;
	else if (pitch_target >= PITCH_80726)
		return CCCA_INTERPROM_5;
	else if (pitch_target >= PITCH_67882)
		return CCCA_INTERPROM_4;
	else if (pitch_target >= PITCH_57081)
		return CCCA_INTERPROM_3;
	else  
		return CCCA_INTERPROM_2;
}

/*
 * calculate cache invalidate size 
 *
 * stereo: channel is stereo
 * w_16: using 16bit samples
 *
 * returns: cache invalidate size in samples
 */
static inline int emu10k1_ccis(int stereo, int w_16)
{
	if (w_16) {
		return stereo ? 24 : 26;
	} else {
		return stereo ? 24*2 : 26*2;
	}
}

static void snd_emu10k1_pcm_init_voice(emu10k1_t *emu,
				       int master, int extra,
				       emu10k1_voice_t *evoice,
				       unsigned int start_addr,
				       unsigned int end_addr,
				       emu10k1_pcm_mixer_t *mix)
{
	snd_pcm_substream_t *substream = evoice->epcm->substream;
	snd_pcm_runtime_t *runtime = substream->runtime;
	unsigned int silent_page, tmp;
	int voice, stereo, w_16;
	unsigned char attn, send_amount[8];
	unsigned char send_routing[8];
	unsigned long flags;
	unsigned int pitch_target;
	unsigned int ccis;

	voice = evoice->number;
	stereo = runtime->channels == 2;
	w_16 = snd_pcm_format_width(runtime->format) == 16;

	if (!extra && stereo) {
		start_addr >>= 1;
		end_addr >>= 1;
	}
	if (w_16) {
		start_addr >>= 1;
		end_addr >>= 1;
	}

	spin_lock_irqsave(&emu->reg_lock, flags);

	/* volume parameters */
	if (extra) {
		attn = 0;
		memset(send_routing, 0, sizeof(send_routing));
		send_routing[0] = 0;
		send_routing[1] = 1;
		send_routing[2] = 2;
		send_routing[3] = 3;
		memset(send_amount, 0, sizeof(send_amount));
	} else {
		/* mono, left, right (master voice = left) */
		tmp = stereo ? (master ? 1 : 2) : 0;
		memcpy(send_routing, &mix->send_routing[tmp][0], 8);
		memcpy(send_amount, &mix->send_volume[tmp][0], 8);
	}

	ccis = emu10k1_ccis(stereo, w_16);
	
	if (master) {
		evoice->epcm->ccca_start_addr = start_addr + ccis;
		if (extra) {
			start_addr += ccis;
			end_addr += ccis;
		}
		if (stereo && !extra) {
			snd_emu10k1_ptr_write(emu, CPF, voice, CPF_STEREO_MASK);
			snd_emu10k1_ptr_write(emu, CPF, (voice + 1), CPF_STEREO_MASK);
		} else {
			snd_emu10k1_ptr_write(emu, CPF, voice, 0);
		}
	}

	// setup routing
	if (emu->audigy) {
		snd_emu10k1_ptr_write(emu, A_FXRT1, voice,
				      snd_emu10k1_compose_audigy_fxrt1(send_routing));
		snd_emu10k1_ptr_write(emu, A_FXRT2, voice,
				      snd_emu10k1_compose_audigy_fxrt2(send_routing));
		snd_emu10k1_ptr_write(emu, A_SENDAMOUNTS, voice,
				      ((unsigned int)send_amount[4] << 24) |
				      ((unsigned int)send_amount[5] << 16) |
				      ((unsigned int)send_amount[6] << 8) |
				      (unsigned int)send_amount[7]);
	} else
		snd_emu10k1_ptr_write(emu, FXRT, voice,
				      snd_emu10k1_compose_send_routing(send_routing));
	// Stop CA
	// Assumption that PT is already 0 so no harm overwriting
	snd_emu10k1_ptr_write(emu, PTRX, voice, (send_amount[0] << 8) | send_amount[1]);
	snd_emu10k1_ptr_write(emu, DSL, voice, end_addr | (send_amount[3] << 24));
	snd_emu10k1_ptr_write(emu, PSST, voice, start_addr | (send_amount[2] << 24));
	pitch_target = emu10k1_calc_pitch_target(runtime->rate);
	if (extra)
		snd_emu10k1_ptr_write(emu, CCCA, voice, start_addr |
			      emu10k1_select_interprom(pitch_target) |
			      (w_16 ? 0 : CCCA_8BITSELECT));
	else
		snd_emu10k1_ptr_write(emu, CCCA, voice, (start_addr + ccis) |
			      emu10k1_select_interprom(pitch_target) |
			      (w_16 ? 0 : CCCA_8BITSELECT));
	// Clear filter delay memory
	snd_emu10k1_ptr_write(emu, Z1, voice, 0);
	snd_emu10k1_ptr_write(emu, Z2, voice, 0);
	// invalidate maps
	silent_page = ((unsigned int)emu->silent_page.addr << 1) | MAP_PTI_MASK;
	snd_emu10k1_ptr_write(emu, MAPA, voice, silent_page);
	snd_emu10k1_ptr_write(emu, MAPB, voice, silent_page);
	// modulation envelope
	snd_emu10k1_ptr_write(emu, CVCF, voice, 0xffff);
	snd_emu10k1_ptr_write(emu, VTFT, voice, 0xffff);
	snd_emu10k1_ptr_write(emu, ATKHLDM, voice, 0);
	snd_emu10k1_ptr_write(emu, DCYSUSM, voice, 0x007f);
	snd_emu10k1_ptr_write(emu, LFOVAL1, voice, 0x8000);
	snd_emu10k1_ptr_write(emu, LFOVAL2, voice, 0x8000);
	snd_emu10k1_ptr_write(emu, FMMOD, voice, 0);
	snd_emu10k1_ptr_write(emu, TREMFRQ, voice, 0);
	snd_emu10k1_ptr_write(emu, FM2FRQ2, voice, 0);
	snd_emu10k1_ptr_write(emu, ENVVAL, voice, 0x8000);
	// volume envelope
	snd_emu10k1_ptr_write(emu, ATKHLDV, voice, 0x7f7f);
	snd_emu10k1_ptr_write(emu, ENVVOL, voice, 0x0000);
	// filter envelope
	snd_emu10k1_ptr_write(emu, PEFE_FILTERAMOUNT, voice, 0x7f);
	// pitch envelope
	snd_emu10k1_ptr_write(emu, PEFE_PITCHAMOUNT, voice, 0);

	spin_unlock_irqrestore(&emu->reg_lock, flags);
}

static int snd_emu10k1_playback_hw_params(snd_pcm_substream_t * substream,
					  snd_pcm_hw_params_t * hw_params)
{
	emu10k1_t *emu = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	emu10k1_pcm_t *epcm = runtime->private_data;
	int err;

	if ((err = snd_emu10k1_pcm_channel_alloc(epcm, params_channels(hw_params))) < 0)
		return err;
	if ((err = snd_pcm_lib_malloc_pages(substream, params_buffer_bytes(hw_params))) < 0)
		return err;
	if (err > 0) {	/* change */
		snd_util_memblk_t *memblk;
		if (epcm->memblk != NULL)
			snd_emu10k1_free_pages(emu, epcm->memblk);
		memblk = snd_emu10k1_alloc_pages(emu, substream);
		if ((epcm->memblk = memblk) == NULL || ((emu10k1_memblk_t *)memblk)->mapped_page < 0) {
			epcm->start_addr = 0;
			return -ENOMEM;
		}
		epcm->start_addr = ((emu10k1_memblk_t *)memblk)->mapped_page << PAGE_SHIFT;
	}
	return 0;
}

static int snd_emu10k1_playback_hw_free(snd_pcm_substream_t * substream)
{
	emu10k1_t *emu = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	emu10k1_pcm_t *epcm;

	if (runtime->private_data == NULL)
		return 0;
	epcm = runtime->private_data;
	if (epcm->extra) {
		snd_emu10k1_voice_free(epcm->emu, epcm->extra);
		epcm->extra = NULL;
	}
	if (epcm->voices[1]) {
		snd_emu10k1_voice_free(epcm->emu, epcm->voices[1]);
		epcm->voices[1] = NULL;
	}
	if (epcm->voices[0]) {
		snd_emu10k1_voice_free(epcm->emu, epcm->voices[0]);
		epcm->voices[0] = NULL;
	}
	if (epcm->memblk) {
		snd_emu10k1_free_pages(emu, epcm->memblk);
		epcm->memblk = NULL;
		epcm->start_addr = 0;
	}
	snd_pcm_lib_free_pages(substream);
	return 0;
}

static int snd_emu10k1_efx_playback_hw_free(snd_pcm_substream_t * substream)
{
	emu10k1_t *emu = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	emu10k1_pcm_t *epcm;
	int i;

	if (runtime->private_data == NULL)
		return 0;
	epcm = runtime->private_data;
	if (epcm->extra) {
		snd_emu10k1_voice_free(epcm->emu, epcm->extra);
		epcm->extra = NULL;
	}
	for (i=0; i < NUM_EFX_PLAYBACK; i++) {
		if (epcm->voices[i]) {
			snd_emu10k1_voice_free(epcm->emu, epcm->voices[i]);
			epcm->voices[i] = NULL;
		}
	}
	if (epcm->memblk) {
		snd_emu10k1_free_pages(emu, epcm->memblk);
		epcm->memblk = NULL;
		epcm->start_addr = 0;
	}
	snd_pcm_lib_free_pages(substream);
	return 0;
}

static int snd_emu10k1_playback_prepare(snd_pcm_substream_t * substream)
{
	emu10k1_t *emu = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	emu10k1_pcm_t *epcm = runtime->private_data;
	unsigned int start_addr, end_addr;

	start_addr = epcm->start_addr;
	end_addr = snd_pcm_lib_period_bytes(substream);
	if (runtime->channels == 2) {
		start_addr >>= 1;
		end_addr >>= 1;
	}
	end_addr += start_addr;
	snd_emu10k1_pcm_init_voice(emu, 1, 1, epcm->extra,
				   start_addr, end_addr, NULL);
	start_addr = epcm->start_addr;
	end_addr = epcm->start_addr + snd_pcm_lib_buffer_bytes(substream);
	snd_emu10k1_pcm_init_voice(emu, 1, 0, epcm->voices[0],
				   start_addr, end_addr,
				   &emu->pcm_mixer[substream->number]);
	if (epcm->voices[1])
		snd_emu10k1_pcm_init_voice(emu, 0, 0, epcm->voices[1],
					   start_addr, end_addr,
					   &emu->pcm_mixer[substream->number]);
	return 0;
}

static int snd_emu10k1_efx_playback_prepare(snd_pcm_substream_t * substream)
{
	emu10k1_t *emu = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	emu10k1_pcm_t *epcm = runtime->private_data;
	unsigned int start_addr, end_addr;
	unsigned int channel_size;
	int i;

	start_addr = epcm->start_addr;
	end_addr = epcm->start_addr + snd_pcm_lib_buffer_bytes(substream);

	/*
	 * the kX driver leaves some space between voices
	 */
	channel_size = ( end_addr - start_addr ) / NUM_EFX_PLAYBACK;

	snd_emu10k1_pcm_init_voice(emu, 1, 1, epcm->extra,
				   start_addr, start_addr + (channel_size / 2), NULL);

	/* only difference with the master voice is we use it for the pointer */
	snd_emu10k1_pcm_init_voice(emu, 1, 0, epcm->voices[0],
				   start_addr, start_addr + channel_size,
				   &emu->efx_pcm_mixer[0]);

	start_addr += channel_size;
	for (i = 1; i < NUM_EFX_PLAYBACK; i++) {
		snd_emu10k1_pcm_init_voice(emu, 0, 0, epcm->voices[i],
					   start_addr, start_addr + channel_size,
					   &emu->efx_pcm_mixer[i]);
		start_addr += channel_size;
	}

	return 0;
}

static snd_pcm_hardware_t snd_emu10k1_efx_playback =
{
	.info =			(SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_NONINTERLEAVED |
				 SNDRV_PCM_INFO_BLOCK_TRANSFER |
				 SNDRV_PCM_INFO_MMAP_VALID | SNDRV_PCM_INFO_PAUSE),
	.formats =		SNDRV_PCM_FMTBIT_S16_LE,
	.rates =		SNDRV_PCM_RATE_48000,
	.rate_min =		48000,
	.rate_max =		48000,
	.channels_min =		NUM_EFX_PLAYBACK,
	.channels_max =		NUM_EFX_PLAYBACK,
	.buffer_bytes_max =	(64*1024),
	.period_bytes_min =	64,
	.period_bytes_max =	(64*1024),
	.periods_min =		2,
	.periods_max =		2,
	.fifo_size =		0,
};

static int snd_emu10k1_capture_hw_params(snd_pcm_substream_t * substream,
					 snd_pcm_hw_params_t * hw_params)
{
	return snd_pcm_lib_malloc_pages(substream, params_buffer_bytes(hw_params));
}

static int snd_emu10k1_capture_hw_free(snd_pcm_substream_t * substream)
{
	return snd_pcm_lib_free_pages(substream);
}

static int snd_emu10k1_capture_prepare(snd_pcm_substream_t * substream)
{
	emu10k1_t *emu = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	emu10k1_pcm_t *epcm = runtime->private_data;
	int idx;

	/* zeroing the buffer size will stop capture */
	snd_emu10k1_ptr_write(emu, epcm->capture_bs_reg, 0, 0);
	switch (epcm->type) {
	case CAPTURE_AC97ADC:
		snd_emu10k1_ptr_write(emu, ADCCR, 0, 0);
		break;
	case CAPTURE_EFX:
		if (emu->audigy) {
			snd_emu10k1_ptr_write(emu, A_FXWC1, 0, 0);
			snd_emu10k1_ptr_write(emu, A_FXWC2, 0, 0);
		} else
			snd_emu10k1_ptr_write(emu, FXWC, 0, 0);
		break;
	default:
		break;
	}	
	snd_emu10k1_ptr_write(emu, epcm->capture_ba_reg, 0, runtime->dma_addr);
	epcm->capture_bufsize = snd_pcm_lib_buffer_bytes(substream);
	epcm->capture_bs_val = 0;
	for (idx = 0; idx < 31; idx++) {
		if (capture_period_sizes[idx] == epcm->capture_bufsize) {
			epcm->capture_bs_val = idx + 1;
			break;
		}
	}
	if (epcm->capture_bs_val == 0) {
		snd_BUG();
		epcm->capture_bs_val++;
	}
	if (epcm->type == CAPTURE_AC97ADC) {
		epcm->capture_cr_val = emu->audigy ? A_ADCCR_LCHANENABLE : ADCCR_LCHANENABLE;
		if (runtime->channels > 1)
			epcm->capture_cr_val |= emu->audigy ? A_ADCCR_RCHANENABLE : ADCCR_RCHANENABLE;
		epcm->capture_cr_val |= emu->audigy ?
			snd_emu10k1_audigy_capture_rate_reg(runtime->rate) :
			snd_emu10k1_capture_rate_reg(runtime->rate);
	}
	return 0;
}

static void snd_emu10k1_playback_invalidate_cache(emu10k1_t *emu, int extra, emu10k1_voice_t *evoice)
{
	snd_pcm_runtime_t *runtime;
	unsigned int voice, stereo, i, ccis, cra = 64, cs, sample;

	if (evoice == NULL)
		return;
	runtime = evoice->epcm->substream->runtime;
	voice = evoice->number;
	stereo = (!extra && runtime->channels == 2);
	sample = snd_pcm_format_width(runtime->format) == 16 ? 0 : 0x80808080;
	ccis = emu10k1_ccis(stereo, sample == 0);
	// set cs to 2 * number of cache registers beside the invalidated
	cs = (sample == 0) ? (32-ccis) : (64-ccis+1) >> 1;
	if (cs > 16) cs = 16;
	for (i = 0; i < cs; i++) {
		snd_emu10k1_ptr_write(emu, CD0 + i, voice, sample);
		if (stereo) {
			snd_emu10k1_ptr_write(emu, CD0 + i, voice + 1, sample);
		}
	}
	// reset cache
	snd_emu10k1_ptr_write(emu, CCR_CACHEINVALIDSIZE, voice, 0);
	snd_emu10k1_ptr_write(emu, CCR_READADDRESS, voice, cra);
	if (stereo) {
		snd_emu10k1_ptr_write(emu, CCR_CACHEINVALIDSIZE, voice + 1, 0);
		snd_emu10k1_ptr_write(emu, CCR_READADDRESS, voice + 1, cra);
	}
	// fill cache
	snd_emu10k1_ptr_write(emu, CCR_CACHEINVALIDSIZE, voice, ccis);
	if (stereo) {
		snd_emu10k1_ptr_write(emu, CCR_CACHEINVALIDSIZE, voice+1, ccis);
	}
}

static void snd_emu10k1_playback_prepare_voice(emu10k1_t *emu, emu10k1_voice_t *evoice,
					       int master, int extra,
					       emu10k1_pcm_mixer_t *mix)
{
	snd_pcm_substream_t *substream;
	snd_pcm_runtime_t *runtime;
	unsigned int attn, vattn;
	unsigned int voice, tmp;

	if (evoice == NULL)	/* skip second voice for mono */
		return;
	substream = evoice->epcm->substream;
	runtime = substream->runtime;
	voice = evoice->number;

	attn = extra ? 0 : 0x00ff;
	tmp = runtime->channels == 2 ? (master ? 1 : 2) : 0;
	vattn = mix != NULL ? (mix->attn[tmp] << 16) : 0;
	snd_emu10k1_ptr_write(emu, IFATN, voice, attn);
	snd_emu10k1_ptr_write(emu, VTFT, voice, vattn | 0xffff);
	snd_emu10k1_ptr_write(emu, CVCF, voice, vattn | 0xffff);
	snd_emu10k1_ptr_write(emu, DCYSUSV, voice, 0x7f7f);
	snd_emu10k1_voice_clear_loop_stop(emu, voice);
}	

static void snd_emu10k1_playback_trigger_voice(emu10k1_t *emu, emu10k1_voice_t *evoice, int master, int extra)
{
	snd_pcm_substream_t *substream;
	snd_pcm_runtime_t *runtime;
	unsigned int voice, pitch, pitch_target;

	if (evoice == NULL)	/* skip second voice for mono */
		return;
	substream = evoice->epcm->substream;
	runtime = substream->runtime;
	voice = evoice->number;

	pitch = snd_emu10k1_rate_to_pitch(runtime->rate) >> 8;
	pitch_target = emu10k1_calc_pitch_target(runtime->rate);
	snd_emu10k1_ptr_write(emu, PTRX_PITCHTARGET, voice, pitch_target);
	if (master || evoice->epcm->type == PLAYBACK_EFX)
		snd_emu10k1_ptr_write(emu, CPF_CURRENTPITCH, voice, pitch_target);
	snd_emu10k1_ptr_write(emu, IP, voice, pitch);
	if (extra)
		snd_emu10k1_voice_intr_enable(emu, voice);
}

static void snd_emu10k1_playback_stop_voice(emu10k1_t *emu, emu10k1_voice_t *evoice)
{
	unsigned int voice;

	if (evoice == NULL)
		return;
	voice = evoice->number;
	snd_emu10k1_voice_intr_disable(emu, voice);
	snd_emu10k1_ptr_write(emu, PTRX_PITCHTARGET, voice, 0);
	snd_emu10k1_ptr_write(emu, CPF_CURRENTPITCH, voice, 0);
	snd_emu10k1_ptr_write(emu, IFATN, voice, 0xffff);
	snd_emu10k1_ptr_write(emu, VTFT, voice, 0xffff);
	snd_emu10k1_ptr_write(emu, CVCF, voice, 0xffff);
	snd_emu10k1_ptr_write(emu, IP, voice, 0);
}

static int snd_emu10k1_playback_trigger(snd_pcm_substream_t * substream,
				        int cmd)
{
	emu10k1_t *emu = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	emu10k1_pcm_t *epcm = runtime->private_data;
	emu10k1_pcm_mixer_t *mix;
	int result = 0;

	// printk("trigger - emu10k1 = 0x%x, cmd = %i, pointer = %i\n", (int)emu, cmd, substream->ops->pointer(substream));
	spin_lock(&emu->reg_lock);
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		snd_emu10k1_playback_invalidate_cache(emu, 1, epcm->extra);	/* do we need this? */
		snd_emu10k1_playback_invalidate_cache(emu, 0, epcm->voices[0]);
		/* follow thru */
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		mix = &emu->pcm_mixer[substream->number];
		snd_emu10k1_playback_prepare_voice(emu, epcm->voices[0], 1, 0, mix);
		snd_emu10k1_playback_prepare_voice(emu, epcm->voices[1], 0, 0, mix);
		snd_emu10k1_playback_prepare_voice(emu, epcm->extra, 1, 1, NULL);
		snd_emu10k1_playback_trigger_voice(emu, epcm->voices[0], 1, 0);
		snd_emu10k1_playback_trigger_voice(emu, epcm->voices[1], 0, 0);
		snd_emu10k1_playback_trigger_voice(emu, epcm->extra, 1, 1);
		epcm->running = 1;
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		epcm->running = 0;
		snd_emu10k1_playback_stop_voice(emu, epcm->voices[0]);
		snd_emu10k1_playback_stop_voice(emu, epcm->voices[1]);
		snd_emu10k1_playback_stop_voice(emu, epcm->extra);
		break;
	default:
		result = -EINVAL;
		break;
	}
	spin_unlock(&emu->reg_lock);
	return result;
}

static int snd_emu10k1_capture_trigger(snd_pcm_substream_t * substream,
				       int cmd)
{
	emu10k1_t *emu = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	emu10k1_pcm_t *epcm = runtime->private_data;
	int result = 0;

	spin_lock(&emu->reg_lock);
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		// hmm this should cause full and half full interrupt to be raised?  
		outl(epcm->capture_ipr, emu->port + IPR);
		snd_emu10k1_intr_enable(emu, epcm->capture_inte);
		// printk("adccr = 0x%x, adcbs = 0x%x\n", epcm->adccr, epcm->adcbs);
		switch (epcm->type) {
		case CAPTURE_AC97ADC:
			snd_emu10k1_ptr_write(emu, ADCCR, 0, epcm->capture_cr_val);
			break;
		case CAPTURE_EFX:
			if (emu->audigy) {
				snd_emu10k1_ptr_write(emu, A_FXWC1, 0, epcm->capture_cr_val);
				snd_emu10k1_ptr_write(emu, A_FXWC2, 0, epcm->capture_cr_val2);
			} else
				snd_emu10k1_ptr_write(emu, FXWC, 0, epcm->capture_cr_val);
			break;
		default:	
			break;
		}
		snd_emu10k1_ptr_write(emu, epcm->capture_bs_reg, 0, epcm->capture_bs_val);
		epcm->running = 1;
		epcm->first_ptr = 1;
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		epcm->running = 0;
		snd_emu10k1_intr_disable(emu, epcm->capture_inte);
		outl(epcm->capture_ipr, emu->port + IPR);
		snd_emu10k1_ptr_write(emu, epcm->capture_bs_reg, 0, 0);
		switch (epcm->type) {
		case CAPTURE_AC97ADC:
			snd_emu10k1_ptr_write(emu, ADCCR, 0, 0);
			break;
		case CAPTURE_EFX:
			if (emu->audigy) {
				snd_emu10k1_ptr_write(emu, A_FXWC1, 0, 0);
				snd_emu10k1_ptr_write(emu, A_FXWC2, 0, 0);
			} else
				snd_emu10k1_ptr_write(emu, FXWC, 0, 0);
			break;
		default:
			break;
		}
		break;
	default:
		result = -EINVAL;
	}
	spin_unlock(&emu->reg_lock);
	return result;
}

static snd_pcm_uframes_t snd_emu10k1_playback_pointer(snd_pcm_substream_t * substream)
{
	emu10k1_t *emu = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	emu10k1_pcm_t *epcm = runtime->private_data;
	unsigned int ptr;

	if (!epcm->running)
		return 0;
	ptr = snd_emu10k1_ptr_read(emu, CCCA, epcm->voices[0]->number) & 0x00ffffff;
#if 0	/* Perex's code */
	ptr += runtime->buffer_size;
	ptr -= epcm->ccca_start_addr;
	ptr %= runtime->buffer_size;
#else	/* EMU10K1 Open Source code from Creative */
	if (ptr < epcm->ccca_start_addr)
		ptr += runtime->buffer_size - epcm->ccca_start_addr;
	else {
		ptr -= epcm->ccca_start_addr;
		if (ptr >= runtime->buffer_size)
			ptr -= runtime->buffer_size;
	}
#endif
	// printk("ptr = 0x%x, buffer_size = 0x%x, period_size = 0x%x\n", ptr, runtime->buffer_size, runtime->period_size);
	return ptr;
}


static int snd_emu10k1_efx_playback_trigger(snd_pcm_substream_t * substream,
				        int cmd)
{
	emu10k1_t *emu = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	emu10k1_pcm_t *epcm = runtime->private_data;
	int i;
	int result = 0;

	spin_lock(&emu->reg_lock);
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		// prepare voices
		for (i = 0; i < NUM_EFX_PLAYBACK; i++) {	
			snd_emu10k1_playback_invalidate_cache(emu, 0, epcm->voices[i]);
		}
		snd_emu10k1_playback_invalidate_cache(emu, 1, epcm->extra);

		/* follow thru */
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		snd_emu10k1_playback_prepare_voice(emu, epcm->extra, 1, 1, NULL);
		snd_emu10k1_playback_prepare_voice(emu, epcm->voices[0], 0, 0,
						   &emu->efx_pcm_mixer[0]);
		for (i = 1; i < NUM_EFX_PLAYBACK; i++)
			snd_emu10k1_playback_prepare_voice(emu, epcm->voices[i], 0, 0,
							   &emu->efx_pcm_mixer[i]);
		snd_emu10k1_playback_trigger_voice(emu, epcm->voices[0], 0, 0);
		snd_emu10k1_playback_trigger_voice(emu, epcm->extra, 1, 1);
		for (i = 1; i < NUM_EFX_PLAYBACK; i++)
			snd_emu10k1_playback_trigger_voice(emu, epcm->voices[i], 0, 0);
		epcm->running = 1;
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		epcm->running = 0;
		for (i = 0; i < NUM_EFX_PLAYBACK; i++) {	
			snd_emu10k1_playback_stop_voice(emu, epcm->voices[i]);
		}
		snd_emu10k1_playback_stop_voice(emu, epcm->extra);
		break;
	default:
		result = -EINVAL;
		break;
	}
	spin_unlock(&emu->reg_lock);
	return result;
}


static snd_pcm_uframes_t snd_emu10k1_capture_pointer(snd_pcm_substream_t * substream)
{
	emu10k1_t *emu = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	emu10k1_pcm_t *epcm = runtime->private_data;
	unsigned int ptr;

	if (!epcm->running)
		return 0;
	if (epcm->first_ptr) {
		udelay(50);	// hack, it takes awhile until capture is started
		epcm->first_ptr = 0;
	}
	ptr = snd_emu10k1_ptr_read(emu, epcm->capture_idx_reg, 0) & 0x0000ffff;
	return bytes_to_frames(runtime, ptr);
}

/*
 *  Playback support device description
 */

static snd_pcm_hardware_t snd_emu10k1_playback =
{
	.info =			(SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_BLOCK_TRANSFER |
				 SNDRV_PCM_INFO_MMAP_VALID | SNDRV_PCM_INFO_PAUSE),
	.formats =		SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S16_LE,
	.rates =		SNDRV_PCM_RATE_CONTINUOUS | SNDRV_PCM_RATE_8000_96000,
	.rate_min =		4000,
	.rate_max =		96000,
	.channels_min =		1,
	.channels_max =		2,
	.buffer_bytes_max =	(128*1024),
	.period_bytes_min =	64,
	.period_bytes_max =	(128*1024),
	.periods_min =		1,
	.periods_max =		1024,
	.fifo_size =		0,
};

/*
 *  Capture support device description
 */

static snd_pcm_hardware_t snd_emu10k1_capture =
{
	.info =			(SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_BLOCK_TRANSFER |
				 SNDRV_PCM_INFO_MMAP_VALID),
	.formats =		SNDRV_PCM_FMTBIT_S16_LE,
	.rates =		SNDRV_PCM_RATE_8000_48000,
	.rate_min =		8000,
	.rate_max =		48000,
	.channels_min =		1,
	.channels_max =		2,
	.buffer_bytes_max =	(64*1024),
	.period_bytes_min =	384,
	.period_bytes_max =	(64*1024),
	.periods_min =		2,
	.periods_max =		2,
	.fifo_size =		0,
};

/*
 *
 */

static void snd_emu10k1_pcm_mixer_notify1(emu10k1_t *emu, snd_kcontrol_t *kctl, int idx, int activate)
{
	snd_ctl_elem_id_t id;

	snd_runtime_check(kctl != NULL, return);
	if (activate)
		kctl->vd[idx].access &= ~SNDRV_CTL_ELEM_ACCESS_INACTIVE;
	else
		kctl->vd[idx].access |= SNDRV_CTL_ELEM_ACCESS_INACTIVE;
	snd_ctl_notify(emu->card, SNDRV_CTL_EVENT_MASK_VALUE |
		       SNDRV_CTL_EVENT_MASK_INFO,
		       snd_ctl_build_ioff(&id, kctl, idx));
}

static void snd_emu10k1_pcm_mixer_notify(emu10k1_t *emu, int idx, int activate)
{
	snd_emu10k1_pcm_mixer_notify1(emu, emu->ctl_send_routing, idx, activate);
	snd_emu10k1_pcm_mixer_notify1(emu, emu->ctl_send_volume, idx, activate);
	snd_emu10k1_pcm_mixer_notify1(emu, emu->ctl_attn, idx, activate);
}

static void snd_emu10k1_pcm_efx_mixer_notify(emu10k1_t *emu, int idx, int activate)
{
	snd_emu10k1_pcm_mixer_notify1(emu, emu->ctl_efx_send_routing, idx, activate);
	snd_emu10k1_pcm_mixer_notify1(emu, emu->ctl_efx_send_volume, idx, activate);
	snd_emu10k1_pcm_mixer_notify1(emu, emu->ctl_efx_attn, idx, activate);
}

static void snd_emu10k1_pcm_free_substream(snd_pcm_runtime_t *runtime)
{
	kfree(runtime->private_data);
}

static int snd_emu10k1_efx_playback_close(snd_pcm_substream_t * substream)
{
	emu10k1_t *emu = snd_pcm_substream_chip(substream);
	emu10k1_pcm_mixer_t *mix;
	int i;

	for (i=0; i < NUM_EFX_PLAYBACK; i++) {
		mix = &emu->efx_pcm_mixer[i];
		mix->epcm = NULL;
		snd_emu10k1_pcm_efx_mixer_notify(emu, i, 0);
	}
	return 0;
}

static int snd_emu10k1_efx_playback_open(snd_pcm_substream_t * substream)
{
	emu10k1_t *emu = snd_pcm_substream_chip(substream);
	emu10k1_pcm_t *epcm;
	emu10k1_pcm_mixer_t *mix;
	snd_pcm_runtime_t *runtime = substream->runtime;
	int i;

	epcm = kzalloc(sizeof(*epcm), GFP_KERNEL);
	if (epcm == NULL)
		return -ENOMEM;
	epcm->emu = emu;
	epcm->type = PLAYBACK_EFX;
	epcm->substream = substream;
	
	emu->pcm_playback_efx_substream = substream;

	runtime->private_data = epcm;
	runtime->private_free = snd_emu10k1_pcm_free_substream;
	runtime->hw = snd_emu10k1_efx_playback;
	
	for (i=0; i < NUM_EFX_PLAYBACK; i++) {
		mix = &emu->efx_pcm_mixer[i];
		mix->send_routing[0][0] = i;
		memset(&mix->send_volume, 0, sizeof(mix->send_volume));
		mix->send_volume[0][0] = 255;
		mix->attn[0] = 0xffff;
		mix->epcm = epcm;
		snd_emu10k1_pcm_efx_mixer_notify(emu, i, 1);
	}
	return 0;
}

static int snd_emu10k1_playback_open(snd_pcm_substream_t * substream)
{
	emu10k1_t *emu = snd_pcm_substream_chip(substream);
	emu10k1_pcm_t *epcm;
	emu10k1_pcm_mixer_t *mix;
	snd_pcm_runtime_t *runtime = substream->runtime;
	int i, err;

	epcm = kzalloc(sizeof(*epcm), GFP_KERNEL);
	if (epcm == NULL)
		return -ENOMEM;
	epcm->emu = emu;
	epcm->type = PLAYBACK_EMUVOICE;
	epcm->substream = substream;
	runtime->private_data = epcm;
	runtime->private_free = snd_emu10k1_pcm_free_substream;
	runtime->hw = snd_emu10k1_playback;
	if ((err = snd_pcm_hw_constraint_integer(runtime, SNDRV_PCM_HW_PARAM_PERIODS)) < 0) {
		kfree(epcm);
		return err;
	}
	if ((err = snd_pcm_hw_constraint_minmax(runtime, SNDRV_PCM_HW_PARAM_BUFFER_BYTES, 256, UINT_MAX)) < 0) {
		kfree(epcm);
		return err;
	}
	mix = &emu->pcm_mixer[substream->number];
	for (i = 0; i < 4; i++)
		mix->send_routing[0][i] = mix->send_routing[1][i] = mix->send_routing[2][i] = i;
	memset(&mix->send_volume, 0, sizeof(mix->send_volume));
	mix->send_volume[0][0] = mix->send_volume[0][1] =
	mix->send_volume[1][0] = mix->send_volume[2][1] = 255;
	mix->attn[0] = mix->attn[1] = mix->attn[2] = 0xffff;
	mix->epcm = epcm;
	snd_emu10k1_pcm_mixer_notify(emu, substream->number, 1);
	return 0;
}

static int snd_emu10k1_playback_close(snd_pcm_substream_t * substream)
{
	emu10k1_t *emu = snd_pcm_substream_chip(substream);
	emu10k1_pcm_mixer_t *mix = &emu->pcm_mixer[substream->number];

	mix->epcm = NULL;
	snd_emu10k1_pcm_mixer_notify(emu, substream->number, 0);
	return 0;
}

static int snd_emu10k1_capture_open(snd_pcm_substream_t * substream)
{
	emu10k1_t *emu = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	emu10k1_pcm_t *epcm;

	epcm = kzalloc(sizeof(*epcm), GFP_KERNEL);
	if (epcm == NULL)
		return -ENOMEM;
	epcm->emu = emu;
	epcm->type = CAPTURE_AC97ADC;
	epcm->substream = substream;
	epcm->capture_ipr = IPR_ADCBUFFULL|IPR_ADCBUFHALFFULL;
	epcm->capture_inte = INTE_ADCBUFENABLE;
	epcm->capture_ba_reg = ADCBA;
	epcm->capture_bs_reg = ADCBS;
	epcm->capture_idx_reg = emu->audigy ? A_ADCIDX : ADCIDX;
	runtime->private_data = epcm;
	runtime->private_free = snd_emu10k1_pcm_free_substream;
	runtime->hw = snd_emu10k1_capture;
	emu->capture_interrupt = snd_emu10k1_pcm_ac97adc_interrupt;
	emu->pcm_capture_substream = substream;
	snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_PERIOD_BYTES, &hw_constraints_capture_period_sizes);
	snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_RATE, &hw_constraints_capture_rates);
	return 0;
}

static int snd_emu10k1_capture_close(snd_pcm_substream_t * substream)
{
	emu10k1_t *emu = snd_pcm_substream_chip(substream);

	emu->capture_interrupt = NULL;
	emu->pcm_capture_substream = NULL;
	return 0;
}

static int snd_emu10k1_capture_mic_open(snd_pcm_substream_t * substream)
{
	emu10k1_t *emu = snd_pcm_substream_chip(substream);
	emu10k1_pcm_t *epcm;
	snd_pcm_runtime_t *runtime = substream->runtime;

	epcm = kzalloc(sizeof(*epcm), GFP_KERNEL);
	if (epcm == NULL)
		return -ENOMEM;
	epcm->emu = emu;
	epcm->type = CAPTURE_AC97MIC;
	epcm->substream = substream;
	epcm->capture_ipr = IPR_MICBUFFULL|IPR_MICBUFHALFFULL;
	epcm->capture_inte = INTE_MICBUFENABLE;
	epcm->capture_ba_reg = MICBA;
	epcm->capture_bs_reg = MICBS;
	epcm->capture_idx_reg = emu->audigy ? A_MICIDX : MICIDX;
	substream->runtime->private_data = epcm;
	substream->runtime->private_free = snd_emu10k1_pcm_free_substream;
	runtime->hw = snd_emu10k1_capture;
	runtime->hw.rates = SNDRV_PCM_RATE_8000;
	runtime->hw.rate_min = runtime->hw.rate_max = 8000;
	runtime->hw.channels_min = 1;
	emu->capture_mic_interrupt = snd_emu10k1_pcm_ac97mic_interrupt;
	emu->pcm_capture_mic_substream = substream;
	snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_PERIOD_BYTES, &hw_constraints_capture_period_sizes);
	return 0;
}

static int snd_emu10k1_capture_mic_close(snd_pcm_substream_t * substream)
{
	emu10k1_t *emu = snd_pcm_substream_chip(substream);

	emu->capture_interrupt = NULL;
	emu->pcm_capture_mic_substream = NULL;
	return 0;
}

static int snd_emu10k1_capture_efx_open(snd_pcm_substream_t * substream)
{
	emu10k1_t *emu = snd_pcm_substream_chip(substream);
	emu10k1_pcm_t *epcm;
	snd_pcm_runtime_t *runtime = substream->runtime;
	int nefx = emu->audigy ? 64 : 32;
	int idx;

	epcm = kzalloc(sizeof(*epcm), GFP_KERNEL);
	if (epcm == NULL)
		return -ENOMEM;
	epcm->emu = emu;
	epcm->type = CAPTURE_EFX;
	epcm->substream = substream;
	epcm->capture_ipr = IPR_EFXBUFFULL|IPR_EFXBUFHALFFULL;
	epcm->capture_inte = INTE_EFXBUFENABLE;
	epcm->capture_ba_reg = FXBA;
	epcm->capture_bs_reg = FXBS;
	epcm->capture_idx_reg = FXIDX;
	substream->runtime->private_data = epcm;
	substream->runtime->private_free = snd_emu10k1_pcm_free_substream;
	runtime->hw = snd_emu10k1_capture;
	runtime->hw.rates = SNDRV_PCM_RATE_48000;
	runtime->hw.rate_min = runtime->hw.rate_max = 48000;
	spin_lock_irq(&emu->reg_lock);
	runtime->hw.channels_min = runtime->hw.channels_max = 0;
	for (idx = 0; idx < nefx; idx++) {
		if (emu->efx_voices_mask[idx/32] & (1 << (idx%32))) {
			runtime->hw.channels_min++;
			runtime->hw.channels_max++;
		}
	}
	epcm->capture_cr_val = emu->efx_voices_mask[0];
	epcm->capture_cr_val2 = emu->efx_voices_mask[1];
	spin_unlock_irq(&emu->reg_lock);
	emu->capture_efx_interrupt = snd_emu10k1_pcm_efx_interrupt;
	emu->pcm_capture_efx_substream = substream;
	snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_PERIOD_BYTES, &hw_constraints_capture_period_sizes);
	return 0;
}

static int snd_emu10k1_capture_efx_close(snd_pcm_substream_t * substream)
{
	emu10k1_t *emu = snd_pcm_substream_chip(substream);

	emu->capture_interrupt = NULL;
	emu->pcm_capture_efx_substream = NULL;
	return 0;
}

static snd_pcm_ops_t snd_emu10k1_playback_ops = {
	.open =			snd_emu10k1_playback_open,
	.close =		snd_emu10k1_playback_close,
	.ioctl =		snd_pcm_lib_ioctl,
	.hw_params =		snd_emu10k1_playback_hw_params,
	.hw_free =		snd_emu10k1_playback_hw_free,
	.prepare =		snd_emu10k1_playback_prepare,
	.trigger =		snd_emu10k1_playback_trigger,
	.pointer =		snd_emu10k1_playback_pointer,
	.page =			snd_pcm_sgbuf_ops_page,
};

static snd_pcm_ops_t snd_emu10k1_capture_ops = {
	.open =			snd_emu10k1_capture_open,
	.close =		snd_emu10k1_capture_close,
	.ioctl =		snd_pcm_lib_ioctl,
	.hw_params =		snd_emu10k1_capture_hw_params,
	.hw_free =		snd_emu10k1_capture_hw_free,
	.prepare =		snd_emu10k1_capture_prepare,
	.trigger =		snd_emu10k1_capture_trigger,
	.pointer =		snd_emu10k1_capture_pointer,
};

/* EFX playback */
static snd_pcm_ops_t snd_emu10k1_efx_playback_ops = {
	.open =			snd_emu10k1_efx_playback_open,
	.close =		snd_emu10k1_efx_playback_close,
	.ioctl =		snd_pcm_lib_ioctl,
	.hw_params =		snd_emu10k1_playback_hw_params,
	.hw_free =		snd_emu10k1_efx_playback_hw_free,
	.prepare =		snd_emu10k1_efx_playback_prepare,
	.trigger =		snd_emu10k1_efx_playback_trigger,
	.pointer =		snd_emu10k1_efx_playback_pointer,
	.page =			snd_pcm_sgbuf_ops_page,
};

static void snd_emu10k1_pcm_free(snd_pcm_t *pcm)
{
	emu10k1_t *emu = pcm->private_data;
	emu->pcm = NULL;
	snd_pcm_lib_preallocate_free_for_all(pcm);
}

int __devinit snd_emu10k1_pcm(emu10k1_t * emu, int device, snd_pcm_t ** rpcm)
{
	snd_pcm_t *pcm;
	snd_pcm_substream_t *substream;
	int err;

	if (rpcm)
		*rpcm = NULL;

	if ((err = snd_pcm_new(emu->card, "emu10k1", device, 32, 1, &pcm)) < 0)
		return err;

	pcm->private_data = emu;
	pcm->private_free = snd_emu10k1_pcm_free;

	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_emu10k1_playback_ops);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_emu10k1_capture_ops);

	pcm->info_flags = 0;
	pcm->dev_subclass = SNDRV_PCM_SUBCLASS_GENERIC_MIX;
	strcpy(pcm->name, "ADC Capture/Standard PCM Playback");
	emu->pcm = pcm;

	for (substream = pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream; substream; substream = substream->next)
		if ((err = snd_pcm_lib_preallocate_pages(substream, SNDRV_DMA_TYPE_DEV_SG, snd_dma_pci_data(emu->pci), 64*1024, 64*1024)) < 0)
			return err;

	for (substream = pcm->streams[SNDRV_PCM_STREAM_CAPTURE].substream; substream; substream = substream->next)
		snd_pcm_lib_preallocate_pages(substream, SNDRV_DMA_TYPE_DEV, snd_dma_pci_data(emu->pci), 64*1024, 64*1024);

	if (rpcm)
		*rpcm = pcm;

	return 0;
}

int __devinit snd_emu10k1_pcm_multi(emu10k1_t * emu, int device, snd_pcm_t ** rpcm)
{
	snd_pcm_t *pcm;
	snd_pcm_substream_t *substream;
	int err;

	if (rpcm)
		*rpcm = NULL;

	if ((err = snd_pcm_new(emu->card, "emu10k1", device, 1, 0, &pcm)) < 0)
		return err;

	pcm->private_data = emu;
	pcm->private_free = snd_emu10k1_pcm_free;

	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_emu10k1_efx_playback_ops);

	pcm->info_flags = 0;
	pcm->dev_subclass = SNDRV_PCM_SUBCLASS_GENERIC_MIX;
	strcpy(pcm->name, "Multichannel Playback");
	emu->pcm = pcm;

	for (substream = pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream; substream; substream = substream->next)
		if ((err = snd_pcm_lib_preallocate_pages(substream, SNDRV_DMA_TYPE_DEV_SG, snd_dma_pci_data(emu->pci), 64*1024, 64*1024)) < 0)
			return err;

	if (rpcm)
		*rpcm = pcm;

	return 0;
}


static snd_pcm_ops_t snd_emu10k1_capture_mic_ops = {
	.open =			snd_emu10k1_capture_mic_open,
	.close =		snd_emu10k1_capture_mic_close,
	.ioctl =		snd_pcm_lib_ioctl,
	.hw_params =		snd_emu10k1_capture_hw_params,
	.hw_free =		snd_emu10k1_capture_hw_free,
	.prepare =		snd_emu10k1_capture_prepare,
	.trigger =		snd_emu10k1_capture_trigger,
	.pointer =		snd_emu10k1_capture_pointer,
};

static void snd_emu10k1_pcm_mic_free(snd_pcm_t *pcm)
{
	emu10k1_t *emu = pcm->private_data;
	emu->pcm_mic = NULL;
	snd_pcm_lib_preallocate_free_for_all(pcm);
}

int __devinit snd_emu10k1_pcm_mic(emu10k1_t * emu, int device, snd_pcm_t ** rpcm)
{
	snd_pcm_t *pcm;
	int err;

	if (rpcm)
		*rpcm = NULL;

	if ((err = snd_pcm_new(emu->card, "emu10k1 mic", device, 0, 1, &pcm)) < 0)
		return err;

	pcm->private_data = emu;
	pcm->private_free = snd_emu10k1_pcm_mic_free;

	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_emu10k1_capture_mic_ops);

	pcm->info_flags = 0;
	strcpy(pcm->name, "Mic Capture");
	emu->pcm_mic = pcm;

	snd_pcm_lib_preallocate_pages_for_all(pcm, SNDRV_DMA_TYPE_DEV, snd_dma_pci_data(emu->pci), 64*1024, 64*1024);

	if (rpcm)
		*rpcm = pcm;
	return 0;
}

static int snd_emu10k1_pcm_efx_voices_mask_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	emu10k1_t *emu = snd_kcontrol_chip(kcontrol);
	int nefx = emu->audigy ? 64 : 32;
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = nefx;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;
	return 0;
}

static int snd_emu10k1_pcm_efx_voices_mask_get(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	emu10k1_t *emu = snd_kcontrol_chip(kcontrol);
	int nefx = emu->audigy ? 64 : 32;
	int idx;
	
	spin_lock_irq(&emu->reg_lock);
	for (idx = 0; idx < nefx; idx++)
		ucontrol->value.integer.value[idx] = (emu->efx_voices_mask[idx / 32] & (1 << (idx % 32))) ? 1 : 0;
	spin_unlock_irq(&emu->reg_lock);
	return 0;
}

static int snd_emu10k1_pcm_efx_voices_mask_put(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	emu10k1_t *emu = snd_kcontrol_chip(kcontrol);
	unsigned int nval[2], bits;
	int nefx = emu->audigy ? 64 : 32;
	int nefxb = emu->audigy ? 7 : 6;
	int change, idx;
	
	nval[0] = nval[1] = 0;
	for (idx = 0, bits = 0; idx < nefx; idx++)
		if (ucontrol->value.integer.value[idx]) {
			nval[idx / 32] |= 1 << (idx % 32);
			bits++;
		}
		
	for (idx = 0; idx < nefxb; idx++)
		if (1 << idx == bits)
			break;
	
	if (idx >= nefxb)
		return -EINVAL;

	spin_lock_irq(&emu->reg_lock);
	change = (nval[0] != emu->efx_voices_mask[0]) ||
		(nval[1] != emu->efx_voices_mask[1]);
	emu->efx_voices_mask[0] = nval[0];
	emu->efx_voices_mask[1] = nval[1];
	spin_unlock_irq(&emu->reg_lock);
	return change;
}

static snd_kcontrol_new_t snd_emu10k1_pcm_efx_voices_mask = {
	.iface = SNDRV_CTL_ELEM_IFACE_PCM,
	.name = "Captured FX8010 Outputs",
	.info = snd_emu10k1_pcm_efx_voices_mask_info,
	.get = snd_emu10k1_pcm_efx_voices_mask_get,
	.put = snd_emu10k1_pcm_efx_voices_mask_put
};

static snd_pcm_ops_t snd_emu10k1_capture_efx_ops = {
	.open =			snd_emu10k1_capture_efx_open,
	.close =		snd_emu10k1_capture_efx_close,
	.ioctl =		snd_pcm_lib_ioctl,
	.hw_params =		snd_emu10k1_capture_hw_params,
	.hw_free =		snd_emu10k1_capture_hw_free,
	.prepare =		snd_emu10k1_capture_prepare,
	.trigger =		snd_emu10k1_capture_trigger,
	.pointer =		snd_emu10k1_capture_pointer,
};


/* EFX playback */

#define INITIAL_TRAM_SHIFT     14
#define INITIAL_TRAM_POS(size) ((((size) / 2) - INITIAL_TRAM_SHIFT) - 1)

static void snd_emu10k1_fx8010_playback_irq(emu10k1_t *emu, void *private_data)
{
	snd_pcm_substream_t *substream = private_data;
	snd_pcm_period_elapsed(substream);
}

static void snd_emu10k1_fx8010_playback_tram_poke1(unsigned short *dst_left,
						   unsigned short *dst_right,
						   unsigned short *src,
						   unsigned int count,
						   unsigned int tram_shift)
{
	// printk("tram_poke1: dst_left = 0x%p, dst_right = 0x%p, src = 0x%p, count = 0x%x\n", dst_left, dst_right, src, count);
	if ((tram_shift & 1) == 0) {
		while (count--) {
			*dst_left-- = *src++;
			*dst_right-- = *src++;
		}
	} else {
		while (count--) {
			*dst_right-- = *src++;
			*dst_left-- = *src++;
		}
	}
}

static void fx8010_pb_trans_copy(snd_pcm_substream_t *substream,
				 snd_pcm_indirect_t *rec, size_t bytes)
{
	emu10k1_t *emu = snd_pcm_substream_chip(substream);
	snd_emu10k1_fx8010_pcm_t *pcm = &emu->fx8010.pcm[substream->number];
	unsigned int tram_size = pcm->buffer_size;
	unsigned short *src = (unsigned short *)(substream->runtime->dma_area + rec->sw_data);
	unsigned int frames = bytes >> 2, count;
	unsigned int tram_pos = pcm->tram_pos;
	unsigned int tram_shift = pcm->tram_shift;

	while (frames > tram_pos) {
		count = tram_pos + 1;
		snd_emu10k1_fx8010_playback_tram_poke1((unsigned short *)emu->fx8010.etram_pages.area + tram_pos,
						       (unsigned short *)emu->fx8010.etram_pages.area + tram_pos + tram_size / 2,
						       src, count, tram_shift);
		src += count * 2;
		frames -= count;
		tram_pos = (tram_size / 2) - 1;
		tram_shift++;
	}
	snd_emu10k1_fx8010_playback_tram_poke1((unsigned short *)emu->fx8010.etram_pages.area + tram_pos,
					       (unsigned short *)emu->fx8010.etram_pages.area + tram_pos + tram_size / 2,
					       src, frames, tram_shift);
	tram_pos -= frames;
	pcm->tram_pos = tram_pos;
	pcm->tram_shift = tram_shift;
}

static int snd_emu10k1_fx8010_playback_transfer(snd_pcm_substream_t *substream)
{
	emu10k1_t *emu = snd_pcm_substream_chip(substream);
	snd_emu10k1_fx8010_pcm_t *pcm = &emu->fx8010.pcm[substream->number];

	snd_pcm_indirect_playback_transfer(substream, &pcm->pcm_rec, fx8010_pb_trans_copy);
	return 0;
}

static int snd_emu10k1_fx8010_playback_hw_params(snd_pcm_substream_t * substream,
						 snd_pcm_hw_params_t * hw_params)
{
	return snd_pcm_lib_malloc_pages(substream, params_buffer_bytes(hw_params));
}

static int snd_emu10k1_fx8010_playback_hw_free(snd_pcm_substream_t * substream)
{
	emu10k1_t *emu = snd_pcm_substream_chip(substream);
	snd_emu10k1_fx8010_pcm_t *pcm = &emu->fx8010.pcm[substream->number];
	unsigned int i;

	for (i = 0; i < pcm->channels; i++)
		snd_emu10k1_ptr_write(emu, TANKMEMADDRREGBASE + 0x80 + pcm->etram[i], 0, 0);
	snd_pcm_lib_free_pages(substream);
	return 0;
}

static int snd_emu10k1_fx8010_playback_prepare(snd_pcm_substream_t * substream)
{
	emu10k1_t *emu = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	snd_emu10k1_fx8010_pcm_t *pcm = &emu->fx8010.pcm[substream->number];
	unsigned int i;
	
	// printk("prepare: etram_pages = 0x%p, dma_area = 0x%x, buffer_size = 0x%x (0x%x)\n", emu->fx8010.etram_pages, runtime->dma_area, runtime->buffer_size, runtime->buffer_size << 2);
	memset(&pcm->pcm_rec, 0, sizeof(pcm->pcm_rec));
	pcm->pcm_rec.hw_buffer_size = pcm->buffer_size * 2; /* byte size */
	pcm->pcm_rec.sw_buffer_size = snd_pcm_lib_buffer_bytes(substream);
	pcm->tram_pos = INITIAL_TRAM_POS(pcm->buffer_size);
	pcm->tram_shift = 0;
	snd_emu10k1_ptr_write(emu, emu->gpr_base + pcm->gpr_running, 0, 0);	/* reset */
	snd_emu10k1_ptr_write(emu, emu->gpr_base + pcm->gpr_trigger, 0, 0);	/* reset */
	snd_emu10k1_ptr_write(emu, emu->gpr_base + pcm->gpr_size, 0, runtime->buffer_size);
	snd_emu10k1_ptr_write(emu, emu->gpr_base + pcm->gpr_ptr, 0, 0);		/* reset ptr number */
	snd_emu10k1_ptr_write(emu, emu->gpr_base + pcm->gpr_count, 0, runtime->period_size);
	snd_emu10k1_ptr_write(emu, emu->gpr_base + pcm->gpr_tmpcount, 0, runtime->period_size);
	for (i = 0; i < pcm->channels; i++)
		snd_emu10k1_ptr_write(emu, TANKMEMADDRREGBASE + 0x80 + pcm->etram[i], 0, (TANKMEMADDRREG_READ|TANKMEMADDRREG_ALIGN) + i * (runtime->buffer_size / pcm->channels));
	return 0;
}

static int snd_emu10k1_fx8010_playback_trigger(snd_pcm_substream_t * substream, int cmd)
{
	emu10k1_t *emu = snd_pcm_substream_chip(substream);
	snd_emu10k1_fx8010_pcm_t *pcm = &emu->fx8010.pcm[substream->number];
	int result = 0;

	spin_lock(&emu->reg_lock);
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		/* follow thru */
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
#ifdef EMU10K1_SET_AC3_IEC958
	{
		int i;
		for (i = 0; i < 3; i++) {
			unsigned int bits;
			bits = SPCS_CLKACCY_1000PPM | SPCS_SAMPLERATE_48 |
			       SPCS_CHANNELNUM_LEFT | SPCS_SOURCENUM_UNSPEC | SPCS_GENERATIONSTATUS |
			       0x00001200 | SPCS_EMPHASIS_NONE | SPCS_COPYRIGHT | SPCS_NOTAUDIODATA;
			snd_emu10k1_ptr_write(emu, SPCS0 + i, 0, bits);
		}
	}
#endif
		result = snd_emu10k1_fx8010_register_irq_handler(emu, snd_emu10k1_fx8010_playback_irq, pcm->gpr_running, substream, &pcm->irq);
		if (result < 0)
			goto __err;
		snd_emu10k1_fx8010_playback_transfer(substream);	/* roll the ball */
		snd_emu10k1_ptr_write(emu, emu->gpr_base + pcm->gpr_trigger, 0, 1);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		snd_emu10k1_fx8010_unregister_irq_handler(emu, pcm->irq); pcm->irq = NULL;
		snd_emu10k1_ptr_write(emu, emu->gpr_base + pcm->gpr_trigger, 0, 0);
		pcm->tram_pos = INITIAL_TRAM_POS(pcm->buffer_size);
		pcm->tram_shift = 0;
		break;
	default:
		result = -EINVAL;
		break;
	}
      __err:
	spin_unlock(&emu->reg_lock);
	return result;
}

static snd_pcm_uframes_t snd_emu10k1_fx8010_playback_pointer(snd_pcm_substream_t * substream)
{
	emu10k1_t *emu = snd_pcm_substream_chip(substream);
	snd_emu10k1_fx8010_pcm_t *pcm = &emu->fx8010.pcm[substream->number];
	size_t ptr; /* byte pointer */

	if (!snd_emu10k1_ptr_read(emu, emu->gpr_base + pcm->gpr_trigger, 0))
		return 0;
	ptr = snd_emu10k1_ptr_read(emu, emu->gpr_base + pcm->gpr_ptr, 0) << 2;
	return snd_pcm_indirect_playback_pointer(substream, &pcm->pcm_rec, ptr);
}

static snd_pcm_hardware_t snd_emu10k1_fx8010_playback =
{
	.info =			(SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
				 /* SNDRV_PCM_INFO_MMAP_VALID | */ SNDRV_PCM_INFO_PAUSE),
	.formats =		SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S16_LE,
	.rates =		SNDRV_PCM_RATE_48000,
	.rate_min =		48000,
	.rate_max =		48000,
	.channels_min =		1,
	.channels_max =		1,
	.buffer_bytes_max =	(128*1024),
	.period_bytes_min =	1024,
	.period_bytes_max =	(128*1024),
	.periods_min =		1,
	.periods_max =		1024,
	.fifo_size =		0,
};

static int snd_emu10k1_fx8010_playback_open(snd_pcm_substream_t * substream)
{
	emu10k1_t *emu = snd_pcm_substream_chip(substream);
	snd_pcm_runtime_t *runtime = substream->runtime;
	snd_emu10k1_fx8010_pcm_t *pcm = &emu->fx8010.pcm[substream->number];

	runtime->hw = snd_emu10k1_fx8010_playback;
	runtime->hw.channels_min = runtime->hw.channels_max = pcm->channels;
	runtime->hw.period_bytes_max = (pcm->buffer_size * 2) / 2;
	spin_lock_irq(&emu->reg_lock);
	if (pcm->valid == 0) {
		spin_unlock_irq(&emu->reg_lock);
		return -ENODEV;
	}
	pcm->opened = 1;
	spin_unlock_irq(&emu->reg_lock);
	return 0;
}

static int snd_emu10k1_fx8010_playback_close(snd_pcm_substream_t * substream)
{
	emu10k1_t *emu = snd_pcm_substream_chip(substream);
	snd_emu10k1_fx8010_pcm_t *pcm = &emu->fx8010.pcm[substream->number];

	spin_lock_irq(&emu->reg_lock);
	pcm->opened = 0;
	spin_unlock_irq(&emu->reg_lock);
	return 0;
}

static snd_pcm_ops_t snd_emu10k1_fx8010_playback_ops = {
	.open =			snd_emu10k1_fx8010_playback_open,
	.close =		snd_emu10k1_fx8010_playback_close,
	.ioctl =		snd_pcm_lib_ioctl,
	.hw_params =		snd_emu10k1_fx8010_playback_hw_params,
	.hw_free =		snd_emu10k1_fx8010_playback_hw_free,
	.prepare =		snd_emu10k1_fx8010_playback_prepare,
	.trigger =		snd_emu10k1_fx8010_playback_trigger,
	.pointer =		snd_emu10k1_fx8010_playback_pointer,
	.ack =			snd_emu10k1_fx8010_playback_transfer,
};

static void snd_emu10k1_pcm_efx_free(snd_pcm_t *pcm)
{
	emu10k1_t *emu = pcm->private_data;
	emu->pcm_efx = NULL;
	snd_pcm_lib_preallocate_free_for_all(pcm);
}

int __devinit snd_emu10k1_pcm_efx(emu10k1_t * emu, int device, snd_pcm_t ** rpcm)
{
	snd_pcm_t *pcm;
	snd_kcontrol_t *kctl;
	int err;

	if (rpcm)
		*rpcm = NULL;

	if ((err = snd_pcm_new(emu->card, "emu10k1 efx", device, 8, 1, &pcm)) < 0)
		return err;

	pcm->private_data = emu;
	pcm->private_free = snd_emu10k1_pcm_efx_free;

	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_emu10k1_fx8010_playback_ops);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_emu10k1_capture_efx_ops);

	pcm->info_flags = 0;
	strcpy(pcm->name, "Multichannel Capture/PT Playback");
	emu->pcm_efx = pcm;
	if (rpcm)
		*rpcm = pcm;

	/* EFX capture - record the "FXBUS2" channels, by default we connect the EXTINs 
	 * to these
	 */	
	
	/* emu->efx_voices_mask[0] = FXWC_DEFAULTROUTE_C | FXWC_DEFAULTROUTE_A; */
	if (emu->audigy) {
		emu->efx_voices_mask[0] = 0;
		emu->efx_voices_mask[1] = 0xffff;
	} else {
		emu->efx_voices_mask[0] = 0xffff0000;
		emu->efx_voices_mask[1] = 0;
	}
	kctl = snd_ctl_new1(&snd_emu10k1_pcm_efx_voices_mask, emu);
	if (!kctl)
		return -ENOMEM;
	kctl->id.device = device;
	snd_ctl_add(emu->card, kctl);

	snd_pcm_lib_preallocate_pages_for_all(pcm, SNDRV_DMA_TYPE_DEV, snd_dma_pci_data(emu->pci), 64*1024, 64*1024);

	return 0;
}
