// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (c) by Jaroslav Kysela <perex@perex.cz>
 *                   Creative Labs, Inc.
 *  Routines for control of EMU10K1 chips / PCM routines
 *  Multichannel PCM support Copyright (c) Lee Revell <rlrevell@joe-job.com>
 *
 *  BUGS:
 *    --
 *
 *  TODO:
 *    --
 */

#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <linux/init.h>
#include <sound/core.h>
#include <sound/emu10k1.h>

static void snd_emu10k1_pcm_interrupt(struct snd_emu10k1 *emu,
				      struct snd_emu10k1_voice *voice)
{
	struct snd_emu10k1_pcm *epcm;

	epcm = voice->epcm;
	if (!epcm)
		return;
	if (epcm->substream == NULL)
		return;
#if 0
	dev_dbg(emu->card->dev,
		"IRQ: position = 0x%x, period = 0x%x, size = 0x%x\n",
			epcm->substream->runtime->hw->pointer(emu, epcm->substream),
			snd_pcm_lib_period_bytes(epcm->substream),
			snd_pcm_lib_buffer_bytes(epcm->substream));
#endif
	snd_pcm_period_elapsed(epcm->substream);
}

static void snd_emu10k1_pcm_ac97adc_interrupt(struct snd_emu10k1 *emu,
					      unsigned int status)
{
#if 0
	if (status & IPR_ADCBUFHALFFULL) {
		if (emu->pcm_capture_substream->runtime->mode == SNDRV_PCM_MODE_FRAME)
			return;
	}
#endif
	snd_pcm_period_elapsed(emu->pcm_capture_substream);
}

static void snd_emu10k1_pcm_ac97mic_interrupt(struct snd_emu10k1 *emu,
					      unsigned int status)
{
#if 0
	if (status & IPR_MICBUFHALFFULL) {
		if (emu->pcm_capture_mic_substream->runtime->mode == SNDRV_PCM_MODE_FRAME)
			return;
	}
#endif
	snd_pcm_period_elapsed(emu->pcm_capture_mic_substream);
}

static void snd_emu10k1_pcm_efx_interrupt(struct snd_emu10k1 *emu,
					  unsigned int status)
{
#if 0
	if (status & IPR_EFXBUFHALFFULL) {
		if (emu->pcm_capture_efx_substream->runtime->mode == SNDRV_PCM_MODE_FRAME)
			return;
	}
#endif
	snd_pcm_period_elapsed(emu->pcm_capture_efx_substream);
}	 

static void snd_emu10k1_pcm_free_voices(struct snd_emu10k1_pcm *epcm)
{
	for (unsigned i = 0; i < ARRAY_SIZE(epcm->voices); i++) {
		if (epcm->voices[i]) {
			snd_emu10k1_voice_free(epcm->emu, epcm->voices[i]);
			epcm->voices[i] = NULL;
		}
	}
}

static int snd_emu10k1_pcm_channel_alloc(struct snd_emu10k1_pcm *epcm,
					 int type, int count, int channels)
{
	int err;

	snd_emu10k1_pcm_free_voices(epcm);

	err = snd_emu10k1_voice_alloc(epcm->emu,
				      type, count, channels,
				      epcm, &epcm->voices[0]);
	if (err < 0)
		return err;

	if (epcm->extra == NULL) {
		// The hardware supports only (half-)loop interrupts, so to support an
		// arbitrary number of periods per buffer, we use an extra voice with a
		// period-sized loop as the interrupt source. Additionally, the interrupt
		// timing of the hardware is "suboptimal" and needs some compensation.
		err = snd_emu10k1_voice_alloc(epcm->emu,
					      type + 1, 1, 1,
					      epcm, &epcm->extra);
		if (err < 0) {
			/*
			dev_dbg(emu->card->dev, "pcm_channel_alloc: "
			       "failed extra: voices=%d, frame=%d\n",
			       voices, frame);
			*/
			snd_emu10k1_pcm_free_voices(epcm);
			return err;
		}
		epcm->extra->interrupt = snd_emu10k1_pcm_interrupt;
	}

	return 0;
}

// Primes 2-7 and 2^n multiples thereof, up to 16.
static const unsigned int efx_capture_channels[] = {
	1, 2, 3, 4, 5, 6, 7, 8, 10, 12, 14, 16
};

static const struct snd_pcm_hw_constraint_list hw_constraints_efx_capture_channels = {
	.count = ARRAY_SIZE(efx_capture_channels),
	.list = efx_capture_channels,
	.mask = 0
};

static const unsigned int capture_buffer_sizes[31] = {
	384,	448,	512,	640,
	384*2,	448*2,	512*2,	640*2,
	384*4,	448*4,	512*4,	640*4,
	384*8,	448*8,	512*8,	640*8,
	384*16,	448*16,	512*16,	640*16,
	384*32,	448*32,	512*32,	640*32,
	384*64,	448*64,	512*64,	640*64,
	384*128,448*128,512*128
};

static const struct snd_pcm_hw_constraint_list hw_constraints_capture_buffer_sizes = {
	.count = 31,
	.list = capture_buffer_sizes,
	.mask = 0
};

static const unsigned int capture_rates[8] = {
	8000, 11025, 16000, 22050, 24000, 32000, 44100, 48000
};

static const struct snd_pcm_hw_constraint_list hw_constraints_capture_rates = {
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

static u16 emu10k1_send_target_from_amount(u8 amount)
{
	static const u8 shifts[8] = { 4, 4, 5, 6, 7, 8, 9, 10 };
	static const u16 offsets[8] = { 0, 0x200, 0x400, 0x800, 0x1000, 0x2000, 0x4000, 0x8000 };
	u8 exp;

	if (amount == 0xff)
		return 0xffff;
	exp = amount >> 5;
	return ((amount & 0x1f) << shifts[exp]) + offsets[exp];
}

static void snd_emu10k1_pcm_init_voice(struct snd_emu10k1 *emu,
				       struct snd_emu10k1_voice *evoice,
				       bool w_16, bool stereo,
				       unsigned int start_addr,
				       unsigned int end_addr,
				       const unsigned char *send_routing,
				       const unsigned char *send_amount)
{
	struct snd_pcm_substream *substream = evoice->epcm->substream;
	struct snd_pcm_runtime *runtime = substream->runtime;
	unsigned int silent_page;
	int voice;
	unsigned int pitch_target;

	voice = evoice->number;

	if (emu->card_capabilities->emu_model)
		pitch_target = PITCH_48000; /* Disable interpolators on emu1010 card */
	else
		pitch_target = emu10k1_calc_pitch_target(runtime->rate);
	silent_page = ((unsigned int)emu->silent_page.addr << emu->address_mode) |
		      (emu->address_mode ? MAP_PTI_MASK1 : MAP_PTI_MASK0);
	snd_emu10k1_ptr_write_multiple(emu, voice,
		// Not really necessary for the slave, but it doesn't hurt
		CPF, stereo ? CPF_STEREO_MASK : 0,
		// Assumption that PT is already 0 so no harm overwriting
		PTRX, (send_amount[0] << 8) | send_amount[1],
		// Stereo slaves don't need to have the addresses set, but it doesn't hurt
		DSL, end_addr | (send_amount[3] << 24),
		PSST, start_addr | (send_amount[2] << 24),
		CCCA, emu10k1_select_interprom(pitch_target) |
		      (w_16 ? 0 : CCCA_8BITSELECT),
		// Clear filter delay memory
		Z1, 0,
		Z2, 0,
		// Invalidate maps
		MAPA, silent_page,
		MAPB, silent_page,
		// Disable filter (in conjunction with CCCA_RESONANCE == 0)
		VTFT, VTFT_FILTERTARGET_MASK,
		CVCF, CVCF_CURRENTFILTER_MASK,
		REGLIST_END);
	// Setup routing
	if (emu->audigy) {
		snd_emu10k1_ptr_write_multiple(emu, voice,
			A_FXRT1, snd_emu10k1_compose_audigy_fxrt1(send_routing),
			A_FXRT2, snd_emu10k1_compose_audigy_fxrt2(send_routing),
			A_SENDAMOUNTS, snd_emu10k1_compose_audigy_sendamounts(send_amount),
			REGLIST_END);
		for (int i = 0; i < 4; i++) {
			u32 aml = emu10k1_send_target_from_amount(send_amount[2 * i]);
			u32 amh = emu10k1_send_target_from_amount(send_amount[2 * i + 1]);
			snd_emu10k1_ptr_write(emu, A_CSBA + i, voice, (amh << 16) | aml);
		}
	} else {
		snd_emu10k1_ptr_write(emu, FXRT, voice,
				      snd_emu10k1_compose_send_routing(send_routing));
	}

	emu->voices[voice].dirty = 1;
}

static void snd_emu10k1_pcm_init_voices(struct snd_emu10k1 *emu,
					struct snd_emu10k1_voice *evoice,
					bool w_16, bool stereo,
					unsigned int start_addr,
					unsigned int end_addr,
					struct snd_emu10k1_pcm_mixer *mix)
{
	unsigned long flags;

	spin_lock_irqsave(&emu->reg_lock, flags);
	snd_emu10k1_pcm_init_voice(emu, evoice, w_16, stereo,
				   start_addr, end_addr,
				   &mix->send_routing[stereo][0],
				   &mix->send_volume[stereo][0]);
	if (stereo)
		snd_emu10k1_pcm_init_voice(emu, evoice + 1, w_16, true,
					   start_addr, end_addr,
					   &mix->send_routing[2][0],
					   &mix->send_volume[2][0]);
	spin_unlock_irqrestore(&emu->reg_lock, flags);
}

static void snd_emu10k1_pcm_init_extra_voice(struct snd_emu10k1 *emu,
					     struct snd_emu10k1_voice *evoice,
					     bool w_16,
					     unsigned int start_addr,
					     unsigned int end_addr)
{
	static const unsigned char send_routing[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };
	static const unsigned char send_amount[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };

	snd_emu10k1_pcm_init_voice(emu, evoice, w_16, false,
				   start_addr, end_addr,
				   send_routing, send_amount);
}

static int snd_emu10k1_playback_hw_params(struct snd_pcm_substream *substream,
					  struct snd_pcm_hw_params *hw_params)
{
	struct snd_emu10k1 *emu = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_emu10k1_pcm *epcm = runtime->private_data;
	size_t alloc_size;
	int type, channels, count;
	int err;

	if (epcm->type == PLAYBACK_EMUVOICE) {
		type = EMU10K1_PCM;
		channels = 1;
		count = params_channels(hw_params);
	} else {
		type = EMU10K1_EFX;
		channels = params_channels(hw_params);
		count = 1;
	}
	err = snd_emu10k1_pcm_channel_alloc(epcm, type, count, channels);
	if (err < 0)
		return err;

	alloc_size = params_buffer_bytes(hw_params);
	if (emu->iommu_workaround)
		alloc_size += EMUPAGESIZE;
	err = snd_pcm_lib_malloc_pages(substream, alloc_size);
	if (err < 0)
		return err;
	if (emu->iommu_workaround && runtime->dma_bytes >= EMUPAGESIZE)
		runtime->dma_bytes -= EMUPAGESIZE;
	if (err > 0) {	/* change */
		int mapped;
		if (epcm->memblk != NULL)
			snd_emu10k1_free_pages(emu, epcm->memblk);
		epcm->memblk = snd_emu10k1_alloc_pages(emu, substream);
		epcm->start_addr = 0;
		if (! epcm->memblk)
			return -ENOMEM;
		mapped = ((struct snd_emu10k1_memblk *)epcm->memblk)->mapped_page;
		if (mapped < 0)
			return -ENOMEM;
		epcm->start_addr = mapped << PAGE_SHIFT;
	}
	return 0;
}

static int snd_emu10k1_playback_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_emu10k1 *emu = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_emu10k1_pcm *epcm;

	if (runtime->private_data == NULL)
		return 0;
	epcm = runtime->private_data;
	if (epcm->extra) {
		snd_emu10k1_voice_free(epcm->emu, epcm->extra);
		epcm->extra = NULL;
	}
	snd_emu10k1_pcm_free_voices(epcm);
	if (epcm->memblk) {
		snd_emu10k1_free_pages(emu, epcm->memblk);
		epcm->memblk = NULL;
		epcm->start_addr = 0;
	}
	snd_pcm_lib_free_pages(substream);
	return 0;
}

static int snd_emu10k1_playback_prepare(struct snd_pcm_substream *substream)
{
	struct snd_emu10k1 *emu = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_emu10k1_pcm *epcm = runtime->private_data;
	bool w_16 = snd_pcm_format_width(runtime->format) == 16;
	bool stereo = runtime->channels == 2;
	unsigned int start_addr, end_addr;

	start_addr = epcm->start_addr >> w_16;
	end_addr = start_addr + runtime->period_size;
	snd_emu10k1_pcm_init_extra_voice(emu, epcm->extra, w_16,
					 start_addr, end_addr);
	start_addr >>= stereo;
	epcm->ccca_start_addr = start_addr;
	end_addr = start_addr + runtime->buffer_size;
	snd_emu10k1_pcm_init_voices(emu, epcm->voices[0], w_16, stereo,
				    start_addr, end_addr,
				    &emu->pcm_mixer[substream->number]);

	return 0;
}

static int snd_emu10k1_efx_playback_prepare(struct snd_pcm_substream *substream)
{
	struct snd_emu10k1 *emu = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_emu10k1_pcm *epcm = runtime->private_data;
	unsigned int start_addr;
	unsigned int extra_size, channel_size;
	unsigned int i;

	start_addr = epcm->start_addr >> 1;  // 16-bit voices

	extra_size = runtime->period_size;
	channel_size = runtime->buffer_size;

	snd_emu10k1_pcm_init_extra_voice(emu, epcm->extra, true,
					 start_addr, start_addr + extra_size);

	epcm->ccca_start_addr = start_addr;
	for (i = 0; i < runtime->channels; i++) {
		snd_emu10k1_pcm_init_voices(emu, epcm->voices[i], true, false,
					    start_addr, start_addr + channel_size,
					    &emu->efx_pcm_mixer[i]);
		start_addr += channel_size;
	}

	return 0;
}

static const struct snd_pcm_hardware snd_emu10k1_efx_playback =
{
	.info =			(SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_NONINTERLEAVED |
				 SNDRV_PCM_INFO_BLOCK_TRANSFER |
				 SNDRV_PCM_INFO_RESUME |
				 SNDRV_PCM_INFO_MMAP_VALID | SNDRV_PCM_INFO_PAUSE),
	.formats =		SNDRV_PCM_FMTBIT_S16_LE,
	.rates =		SNDRV_PCM_RATE_48000,
	.rate_min =		48000,
	.rate_max =		48000,
	.channels_min =		1,
	.channels_max =		NUM_EFX_PLAYBACK,
	.buffer_bytes_max =	(128*1024),
	.period_bytes_max =	(128*1024),
	.periods_min =		2,
	.periods_max =		1024,
	.fifo_size =		0,
};

static int snd_emu10k1_capture_prepare(struct snd_pcm_substream *substream)
{
	struct snd_emu10k1 *emu = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_emu10k1_pcm *epcm = runtime->private_data;
	int idx;

	/* zeroing the buffer size will stop capture */
	snd_emu10k1_ptr_write(emu, epcm->capture_bs_reg, 0, 0);
	switch (epcm->type) {
	case CAPTURE_AC97ADC:
		snd_emu10k1_ptr_write(emu, ADCCR, 0, 0);
		break;
	case CAPTURE_EFX:
		if (emu->card_capabilities->emu_model) {
			// The upper 32 16-bit capture voices, two for each of the 16 32-bit channels.
			// The lower voices are occupied by A_EXTOUT_*_CAP*.
			epcm->capture_cr_val = 0;
			epcm->capture_cr_val2 = 0xffffffff >> (32 - runtime->channels * 2);
		}
		if (emu->audigy) {
			snd_emu10k1_ptr_write_multiple(emu, 0,
				A_FXWC1, 0,
				A_FXWC2, 0,
				REGLIST_END);
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
		if (capture_buffer_sizes[idx] == epcm->capture_bufsize) {
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

static void snd_emu10k1_playback_fill_cache(struct snd_emu10k1 *emu,
					    unsigned voice,
					    u32 sample, bool stereo)
{
	u32 ccr;

	// We assume that the cache is resting at this point (i.e.,
	// CCR_CACHEINVALIDSIZE is very small).

	// Clear leading frames. For simplicitly, this does too much,
	// except for 16-bit stereo. And the interpolator will actually
	// access them at all only when we're pitch-shifting.
	for (int i = 0; i < 3; i++)
		snd_emu10k1_ptr_write(emu, CD0 + i, voice, sample);

	// Fill cache
	ccr = (64 - 3) << REG_SHIFT(CCR_CACHEINVALIDSIZE);
	if (stereo) {
		// The engine goes haywire if CCR_READADDRESS is out of sync
		snd_emu10k1_ptr_write(emu, CCR, voice + 1, ccr);
	}
	snd_emu10k1_ptr_write(emu, CCR, voice, ccr);
}

static void snd_emu10k1_playback_prepare_voices(struct snd_emu10k1 *emu,
						struct snd_emu10k1_pcm *epcm,
						bool w_16, bool stereo,
						int channels)
{
	struct snd_pcm_substream *substream = epcm->substream;
	struct snd_pcm_runtime *runtime = substream->runtime;
	unsigned eloop_start = epcm->start_addr >> w_16;
	unsigned loop_start = eloop_start >> stereo;
	unsigned eloop_size = runtime->period_size;
	unsigned loop_size = runtime->buffer_size;
	u32 sample = w_16 ? 0 : 0x80808080;

	// To make the playback actually start at the 1st frame,
	// we need to compensate for two circumstances:
	// - The actual position is delayed by the cache size (64 frames)
	// - The interpolator is centered around the 4th frame
	loop_start += (epcm->resume_pos + 64 - 3) % loop_size;
	for (int i = 0; i < channels; i++) {
		unsigned voice = epcm->voices[i]->number;
		snd_emu10k1_ptr_write(emu, CCCA_CURRADDR, voice, loop_start);
		loop_start += loop_size;
		snd_emu10k1_playback_fill_cache(emu, voice, sample, stereo);
	}

	// The interrupt is triggered when CCCA_CURRADDR (CA) wraps around,
	// which is ahead of the actual playback position, so the interrupt
	// source needs to be delayed.
	//
	// In principle, this wouldn't need to be the cache's entire size - in
	// practice, CCR_CACHEINVALIDSIZE (CIS) > `fetch threshold` has never
	// been observed, and assuming 40 _bytes_ should be safe.
	//
	// The cache fills are somewhat random, which makes it impossible to
	// align them with the interrupts. This makes a non-delayed interrupt
	// source not practical, as the interrupt handler would have to wait
	// for (CA - CIS) >= period_boundary for every channel in the stream.
	//
	// This is why all other (open) drivers for these chips use timer-based
	// interrupts.
	//
	eloop_start += (epcm->resume_pos + eloop_size - 3) % eloop_size;
	snd_emu10k1_ptr_write(emu, CCCA_CURRADDR, epcm->extra->number, eloop_start);

	// It takes a moment until the cache fills complete,
	// but the unmuting takes long enough for that.
}

static void snd_emu10k1_playback_commit_volume(struct snd_emu10k1 *emu,
					       struct snd_emu10k1_voice *evoice,
					       unsigned int vattn)
{
	snd_emu10k1_ptr_write_multiple(emu, evoice->number,
		VTFT, vattn | VTFT_FILTERTARGET_MASK,
		CVCF, vattn | CVCF_CURRENTFILTER_MASK,
		REGLIST_END);
}

static void snd_emu10k1_playback_unmute_voice(struct snd_emu10k1 *emu,
					      struct snd_emu10k1_voice *evoice,
					      bool stereo, bool master,
					      struct snd_emu10k1_pcm_mixer *mix)
{
	unsigned int vattn;
	unsigned int tmp;

	tmp = stereo ? (master ? 1 : 2) : 0;
	vattn = mix->attn[tmp] << 16;
	snd_emu10k1_playback_commit_volume(emu, evoice, vattn);
}	

static void snd_emu10k1_playback_unmute_voices(struct snd_emu10k1 *emu,
					       struct snd_emu10k1_voice *evoice,
					       bool stereo,
					       struct snd_emu10k1_pcm_mixer *mix)
{
	snd_emu10k1_playback_unmute_voice(emu, evoice, stereo, true, mix);
	if (stereo)
		snd_emu10k1_playback_unmute_voice(emu, evoice + 1, true, false, mix);
}

static void snd_emu10k1_playback_mute_voice(struct snd_emu10k1 *emu,
					    struct snd_emu10k1_voice *evoice)
{
	snd_emu10k1_playback_commit_volume(emu, evoice, 0);
}

static void snd_emu10k1_playback_mute_voices(struct snd_emu10k1 *emu,
					     struct snd_emu10k1_voice *evoice,
					     bool stereo)
{
	snd_emu10k1_playback_mute_voice(emu, evoice);
	if (stereo)
		snd_emu10k1_playback_mute_voice(emu, evoice + 1);
}

static void snd_emu10k1_playback_commit_pitch(struct snd_emu10k1 *emu,
					      u32 voice, u32 pitch_target)
{
	u32 ptrx = snd_emu10k1_ptr_read(emu, PTRX, voice);
	u32 cpf = snd_emu10k1_ptr_read(emu, CPF, voice);
	snd_emu10k1_ptr_write_multiple(emu, voice,
		PTRX, (ptrx & ~PTRX_PITCHTARGET_MASK) | pitch_target,
		CPF, (cpf & ~(CPF_CURRENTPITCH_MASK | CPF_FRACADDRESS_MASK)) | pitch_target,
		REGLIST_END);
}

static void snd_emu10k1_playback_trigger_voice(struct snd_emu10k1 *emu,
					       struct snd_emu10k1_voice *evoice)
{
	struct snd_pcm_substream *substream;
	struct snd_pcm_runtime *runtime;
	unsigned int voice, pitch_target;

	substream = evoice->epcm->substream;
	runtime = substream->runtime;
	voice = evoice->number;

	if (emu->card_capabilities->emu_model)
		pitch_target = PITCH_48000; /* Disable interpolators on emu1010 card */
	else 
		pitch_target = emu10k1_calc_pitch_target(runtime->rate);
	snd_emu10k1_playback_commit_pitch(emu, voice, pitch_target << 16);
}

static void snd_emu10k1_playback_stop_voice(struct snd_emu10k1 *emu,
					    struct snd_emu10k1_voice *evoice)
{
	unsigned int voice;

	voice = evoice->number;
	snd_emu10k1_playback_commit_pitch(emu, voice, 0);
}

static void snd_emu10k1_playback_set_running(struct snd_emu10k1 *emu,
					     struct snd_emu10k1_pcm *epcm)
{
	epcm->running = 1;
	snd_emu10k1_voice_intr_enable(emu, epcm->extra->number);
}

static void snd_emu10k1_playback_set_stopped(struct snd_emu10k1 *emu,
					      struct snd_emu10k1_pcm *epcm)
{
	snd_emu10k1_voice_intr_disable(emu, epcm->extra->number);
	epcm->running = 0;
}

static int snd_emu10k1_playback_trigger(struct snd_pcm_substream *substream,
				        int cmd)
{
	struct snd_emu10k1 *emu = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_emu10k1_pcm *epcm = runtime->private_data;
	struct snd_emu10k1_pcm_mixer *mix;
	bool w_16 = snd_pcm_format_width(runtime->format) == 16;
	bool stereo = runtime->channels == 2;
	int result = 0;

	/*
	dev_dbg(emu->card->dev,
		"trigger - emu10k1 = 0x%x, cmd = %i, pointer = %i\n",
	       (int)emu, cmd, substream->ops->pointer(substream))
	*/
	spin_lock(&emu->reg_lock);
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		snd_emu10k1_playback_prepare_voices(emu, epcm, w_16, stereo, 1);
		fallthrough;
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
	case SNDRV_PCM_TRIGGER_RESUME:
		mix = &emu->pcm_mixer[substream->number];
		snd_emu10k1_playback_unmute_voices(emu, epcm->voices[0], stereo, mix);
		snd_emu10k1_playback_set_running(emu, epcm);
		snd_emu10k1_playback_trigger_voice(emu, epcm->voices[0]);
		snd_emu10k1_playback_trigger_voice(emu, epcm->extra);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		snd_emu10k1_playback_stop_voice(emu, epcm->voices[0]);
		snd_emu10k1_playback_stop_voice(emu, epcm->extra);
		snd_emu10k1_playback_set_stopped(emu, epcm);
		snd_emu10k1_playback_mute_voices(emu, epcm->voices[0], stereo);
		break;
	default:
		result = -EINVAL;
		break;
	}
	spin_unlock(&emu->reg_lock);
	return result;
}

static int snd_emu10k1_capture_trigger(struct snd_pcm_substream *substream,
				       int cmd)
{
	struct snd_emu10k1 *emu = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_emu10k1_pcm *epcm = runtime->private_data;
	int result = 0;

	spin_lock(&emu->reg_lock);
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
		/* hmm this should cause full and half full interrupt to be raised? */
		outl(epcm->capture_ipr, emu->port + IPR);
		snd_emu10k1_intr_enable(emu, epcm->capture_inte);
		/*
		dev_dbg(emu->card->dev, "adccr = 0x%x, adcbs = 0x%x\n",
		       epcm->adccr, epcm->adcbs);
		*/
		switch (epcm->type) {
		case CAPTURE_AC97ADC:
			snd_emu10k1_ptr_write(emu, ADCCR, 0, epcm->capture_cr_val);
			break;
		case CAPTURE_EFX:
			if (emu->audigy) {
				snd_emu10k1_ptr_write_multiple(emu, 0,
					A_FXWC1, epcm->capture_cr_val,
					A_FXWC2, epcm->capture_cr_val2,
					REGLIST_END);
				dev_dbg(emu->card->dev,
					"cr_val=0x%x, cr_val2=0x%x\n",
					epcm->capture_cr_val,
					epcm->capture_cr_val2);
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
	case SNDRV_PCM_TRIGGER_SUSPEND:
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
				snd_emu10k1_ptr_write_multiple(emu, 0,
					A_FXWC1, 0,
					A_FXWC2, 0,
					REGLIST_END);
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

static snd_pcm_uframes_t snd_emu10k1_playback_pointer(struct snd_pcm_substream *substream)
{
	struct snd_emu10k1 *emu = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_emu10k1_pcm *epcm = runtime->private_data;
	int ptr;

	if (!epcm->running)
		return 0;

	ptr = snd_emu10k1_ptr_read(emu, CCCA, epcm->voices[0]->number) & 0x00ffffff;
	ptr -= epcm->ccca_start_addr;

	// This is the size of the whole cache minus the interpolator read-ahead,
	// which leads us to the actual playback position.
	//
	// The cache is constantly kept mostly filled, so in principle we could
	// return a more advanced position representing how far the hardware has
	// already read the buffer, and set runtime->delay accordingly. However,
	// this would be slightly different for every channel (and remarkably slow
	// to obtain), so only a fixed worst-case value would be practical.
	//
	ptr -= 64 - 3;
	if (ptr < 0)
		ptr += runtime->buffer_size;

	/*
	dev_dbg(emu->card->dev,
	       "ptr = 0x%lx, buffer_size = 0x%lx, period_size = 0x%lx\n",
	       (long)ptr, (long)runtime->buffer_size,
	       (long)runtime->period_size);
	*/
	return ptr;
}

static u64 snd_emu10k1_efx_playback_voice_mask(struct snd_emu10k1_pcm *epcm,
					       int channels)
{
	u64 mask = 0;

	for (int i = 0; i < channels; i++) {
		int voice = epcm->voices[i]->number;
		mask |= 1ULL << voice;
	}
	return mask;
}

static void snd_emu10k1_efx_playback_freeze_voices(struct snd_emu10k1 *emu,
						   struct snd_emu10k1_pcm *epcm,
						   int channels)
{
	for (int i = 0; i < channels; i++) {
		int voice = epcm->voices[i]->number;
		snd_emu10k1_ptr_write(emu, CPF_STOP, voice, 1);
		snd_emu10k1_playback_commit_pitch(emu, voice, PITCH_48000 << 16);
	}
}

static void snd_emu10k1_efx_playback_unmute_voices(struct snd_emu10k1 *emu,
						   struct snd_emu10k1_pcm *epcm,
						   int channels)
{
	for (int i = 0; i < channels; i++)
		snd_emu10k1_playback_unmute_voice(emu, epcm->voices[i], false, true,
						  &emu->efx_pcm_mixer[i]);
}

static void snd_emu10k1_efx_playback_stop_voices(struct snd_emu10k1 *emu,
						 struct snd_emu10k1_pcm *epcm,
						 int channels)
{
	for (int i = 0; i < channels; i++)
		snd_emu10k1_playback_stop_voice(emu, epcm->voices[i]);
	snd_emu10k1_playback_set_stopped(emu, epcm);

	for (int i = 0; i < channels; i++)
		snd_emu10k1_playback_mute_voice(emu, epcm->voices[i]);
}

static int snd_emu10k1_efx_playback_trigger(struct snd_pcm_substream *substream,
				        int cmd)
{
	struct snd_emu10k1 *emu = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_emu10k1_pcm *epcm = runtime->private_data;
	u64 mask;
	int result = 0;

	spin_lock(&emu->reg_lock);
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
	case SNDRV_PCM_TRIGGER_RESUME:
		mask = snd_emu10k1_efx_playback_voice_mask(
				epcm, runtime->channels);
		for (int i = 0; i < 10; i++) {
			// Note that the freeze is not interruptible, so we make no
			// effort to reset the bits outside the error handling here.
			snd_emu10k1_voice_set_loop_stop_multiple(emu, mask);
			snd_emu10k1_efx_playback_freeze_voices(
					emu, epcm, runtime->channels);
			snd_emu10k1_playback_prepare_voices(
					emu, epcm, true, false, runtime->channels);

			// It might seem to make more sense to unmute the voices only after
			// they have been started, to potentially avoid torturing the speakers
			// if something goes wrong. However, we cannot unmute atomically,
			// which means that we'd get some mild artifacts in the regular case.
			snd_emu10k1_efx_playback_unmute_voices(emu, epcm, runtime->channels);

			snd_emu10k1_playback_set_running(emu, epcm);
			result = snd_emu10k1_voice_clear_loop_stop_multiple_atomic(emu, mask);
			if (result == 0) {
				// The extra voice is allowed to lag a bit
				snd_emu10k1_playback_trigger_voice(emu, epcm->extra);
				goto leave;
			}

			snd_emu10k1_efx_playback_stop_voices(
					emu, epcm, runtime->channels);

			if (result != -EAGAIN)
				break;
			// The sync start can legitimately fail due to NMIs, etc.
		}
		snd_emu10k1_voice_clear_loop_stop_multiple(emu, mask);
		break;
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		snd_emu10k1_playback_stop_voice(emu, epcm->extra);
		snd_emu10k1_efx_playback_stop_voices(
				emu, epcm, runtime->channels);

		epcm->resume_pos = snd_emu10k1_playback_pointer(substream);
		break;
	default:
		result = -EINVAL;
		break;
	}
leave:
	spin_unlock(&emu->reg_lock);
	return result;
}


static snd_pcm_uframes_t snd_emu10k1_capture_pointer(struct snd_pcm_substream *substream)
{
	struct snd_emu10k1 *emu = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_emu10k1_pcm *epcm = runtime->private_data;
	unsigned int ptr;

	if (!epcm->running)
		return 0;
	if (epcm->first_ptr) {
		udelay(50);	/* hack, it takes awhile until capture is started */
		epcm->first_ptr = 0;
	}
	ptr = snd_emu10k1_ptr_read(emu, epcm->capture_idx_reg, 0) & 0x0000ffff;
	return bytes_to_frames(runtime, ptr);
}

/*
 *  Playback support device description
 */

static const struct snd_pcm_hardware snd_emu10k1_playback =
{
	.info =			(SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_BLOCK_TRANSFER |
				 SNDRV_PCM_INFO_RESUME |
				 SNDRV_PCM_INFO_MMAP_VALID | SNDRV_PCM_INFO_PAUSE),
	.formats =		SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S16_LE,
	.rates =		SNDRV_PCM_RATE_CONTINUOUS | SNDRV_PCM_RATE_8000_96000,
	.rate_min =		4000,
	.rate_max =		96000,
	.channels_min =		1,
	.channels_max =		2,
	.buffer_bytes_max =	(128*1024),
	.period_bytes_max =	(128*1024),
	.periods_min =		2,
	.periods_max =		1024,
	.fifo_size =		0,
};

/*
 *  Capture support device description
 */

static const struct snd_pcm_hardware snd_emu10k1_capture =
{
	.info =			(SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_BLOCK_TRANSFER |
				 SNDRV_PCM_INFO_RESUME |
				 SNDRV_PCM_INFO_MMAP_VALID),
	.formats =		SNDRV_PCM_FMTBIT_S16_LE,
	.rates =		SNDRV_PCM_RATE_8000_48000 | SNDRV_PCM_RATE_KNOT,
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

static const struct snd_pcm_hardware snd_emu10k1_capture_efx =
{
	.info =			(SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_BLOCK_TRANSFER |
				 SNDRV_PCM_INFO_RESUME |
				 SNDRV_PCM_INFO_MMAP_VALID),
	.formats =		SNDRV_PCM_FMTBIT_S16_LE,
	.rates =		SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000 | 
				 SNDRV_PCM_RATE_88200 | SNDRV_PCM_RATE_96000 | 
				 SNDRV_PCM_RATE_176400 | SNDRV_PCM_RATE_192000,
	.rate_min =		44100,
	.rate_max =		192000,
	.channels_min =		1,
	.channels_max =		16,
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

static void snd_emu10k1_pcm_mixer_notify1(struct snd_emu10k1 *emu, struct snd_kcontrol *kctl, int idx, int activate)
{
	struct snd_ctl_elem_id id;

	if (! kctl)
		return;
	if (activate)
		kctl->vd[idx].access &= ~SNDRV_CTL_ELEM_ACCESS_INACTIVE;
	else
		kctl->vd[idx].access |= SNDRV_CTL_ELEM_ACCESS_INACTIVE;
	snd_ctl_notify(emu->card, SNDRV_CTL_EVENT_MASK_VALUE |
		       SNDRV_CTL_EVENT_MASK_INFO,
		       snd_ctl_build_ioff(&id, kctl, idx));
}

static void snd_emu10k1_pcm_mixer_notify(struct snd_emu10k1 *emu, int idx, int activate)
{
	snd_emu10k1_pcm_mixer_notify1(emu, emu->ctl_send_routing, idx, activate);
	snd_emu10k1_pcm_mixer_notify1(emu, emu->ctl_send_volume, idx, activate);
	snd_emu10k1_pcm_mixer_notify1(emu, emu->ctl_attn, idx, activate);
}

static void snd_emu10k1_pcm_efx_mixer_notify(struct snd_emu10k1 *emu, int idx, int activate)
{
	snd_emu10k1_pcm_mixer_notify1(emu, emu->ctl_efx_send_routing, idx, activate);
	snd_emu10k1_pcm_mixer_notify1(emu, emu->ctl_efx_send_volume, idx, activate);
	snd_emu10k1_pcm_mixer_notify1(emu, emu->ctl_efx_attn, idx, activate);
}

static void snd_emu10k1_pcm_free_substream(struct snd_pcm_runtime *runtime)
{
	kfree(runtime->private_data);
}

static int snd_emu10k1_efx_playback_close(struct snd_pcm_substream *substream)
{
	struct snd_emu10k1 *emu = snd_pcm_substream_chip(substream);
	struct snd_emu10k1_pcm_mixer *mix;
	int i;

	for (i = 0; i < NUM_EFX_PLAYBACK; i++) {
		mix = &emu->efx_pcm_mixer[i];
		mix->epcm = NULL;
		snd_emu10k1_pcm_efx_mixer_notify(emu, i, 0);
	}
	return 0;
}

static int snd_emu10k1_playback_set_constraints(struct snd_pcm_runtime *runtime)
{
	int err;

	// The buffer size must be a multiple of the period size, to avoid a
	// mismatch between the extra voice and the regular voices.
	err = snd_pcm_hw_constraint_integer(runtime, SNDRV_PCM_HW_PARAM_PERIODS);
	if (err < 0)
		return err;
	// The hardware is typically the cache's size of 64 frames ahead.
	// Leave enough time for actually filling up the buffer.
	err = snd_pcm_hw_constraint_minmax(
			runtime, SNDRV_PCM_HW_PARAM_PERIOD_SIZE, 128, UINT_MAX);
	return err;
}

static int snd_emu10k1_efx_playback_open(struct snd_pcm_substream *substream)
{
	struct snd_emu10k1 *emu = snd_pcm_substream_chip(substream);
	struct snd_emu10k1_pcm *epcm;
	struct snd_emu10k1_pcm_mixer *mix;
	struct snd_pcm_runtime *runtime = substream->runtime;
	int i, j, err;

	epcm = kzalloc(sizeof(*epcm), GFP_KERNEL);
	if (epcm == NULL)
		return -ENOMEM;
	epcm->emu = emu;
	epcm->type = PLAYBACK_EFX;
	epcm->substream = substream;
	
	runtime->private_data = epcm;
	runtime->private_free = snd_emu10k1_pcm_free_substream;
	runtime->hw = snd_emu10k1_efx_playback;
	err = snd_emu10k1_playback_set_constraints(runtime);
	if (err < 0) {
		kfree(epcm);
		return err;
	}

	for (i = 0; i < NUM_EFX_PLAYBACK; i++) {
		mix = &emu->efx_pcm_mixer[i];
		for (j = 0; j < 8; j++)
			mix->send_routing[0][j] = i + j;
		memset(&mix->send_volume, 0, sizeof(mix->send_volume));
		mix->send_volume[0][0] = 255;
		mix->attn[0] = 0x8000;
		mix->epcm = epcm;
		snd_emu10k1_pcm_efx_mixer_notify(emu, i, 1);
	}
	return 0;
}

static int snd_emu10k1_playback_open(struct snd_pcm_substream *substream)
{
	struct snd_emu10k1 *emu = snd_pcm_substream_chip(substream);
	struct snd_emu10k1_pcm *epcm;
	struct snd_emu10k1_pcm_mixer *mix;
	struct snd_pcm_runtime *runtime = substream->runtime;
	int i, err, sample_rate;

	epcm = kzalloc(sizeof(*epcm), GFP_KERNEL);
	if (epcm == NULL)
		return -ENOMEM;
	epcm->emu = emu;
	epcm->type = PLAYBACK_EMUVOICE;
	epcm->substream = substream;
	runtime->private_data = epcm;
	runtime->private_free = snd_emu10k1_pcm_free_substream;
	runtime->hw = snd_emu10k1_playback;
	err = snd_emu10k1_playback_set_constraints(runtime);
	if (err < 0) {
		kfree(epcm);
		return err;
	}
	if (emu->card_capabilities->emu_model && emu->emu1010.internal_clock == 0)
		sample_rate = 44100;
	else
		sample_rate = 48000;
	err = snd_pcm_hw_rule_noresample(runtime, sample_rate);
	if (err < 0) {
		kfree(epcm);
		return err;
	}
	mix = &emu->pcm_mixer[substream->number];
	for (i = 0; i < 8; i++)
		mix->send_routing[0][i] = mix->send_routing[1][i] = mix->send_routing[2][i] = i;
	memset(&mix->send_volume, 0, sizeof(mix->send_volume));
	mix->send_volume[0][0] = mix->send_volume[0][1] =
	mix->send_volume[1][0] = mix->send_volume[2][1] = 255;
	mix->attn[0] = mix->attn[1] = mix->attn[2] = 0x8000;
	mix->epcm = epcm;
	snd_emu10k1_pcm_mixer_notify(emu, substream->number, 1);
	return 0;
}

static int snd_emu10k1_playback_close(struct snd_pcm_substream *substream)
{
	struct snd_emu10k1 *emu = snd_pcm_substream_chip(substream);
	struct snd_emu10k1_pcm_mixer *mix = &emu->pcm_mixer[substream->number];

	mix->epcm = NULL;
	snd_emu10k1_pcm_mixer_notify(emu, substream->number, 0);
	return 0;
}

static int snd_emu10k1_capture_open(struct snd_pcm_substream *substream)
{
	struct snd_emu10k1 *emu = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_emu10k1_pcm *epcm;

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
	snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_BUFFER_BYTES,
				   &hw_constraints_capture_buffer_sizes);
	emu->capture_interrupt = snd_emu10k1_pcm_ac97adc_interrupt;
	emu->pcm_capture_substream = substream;
	snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_RATE, &hw_constraints_capture_rates);
	return 0;
}

static int snd_emu10k1_capture_close(struct snd_pcm_substream *substream)
{
	struct snd_emu10k1 *emu = snd_pcm_substream_chip(substream);

	emu->capture_interrupt = NULL;
	emu->pcm_capture_substream = NULL;
	return 0;
}

static int snd_emu10k1_capture_mic_open(struct snd_pcm_substream *substream)
{
	struct snd_emu10k1 *emu = snd_pcm_substream_chip(substream);
	struct snd_emu10k1_pcm *epcm;
	struct snd_pcm_runtime *runtime = substream->runtime;

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
	snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_BUFFER_BYTES,
				   &hw_constraints_capture_buffer_sizes);
	emu->capture_mic_interrupt = snd_emu10k1_pcm_ac97mic_interrupt;
	emu->pcm_capture_mic_substream = substream;
	return 0;
}

static int snd_emu10k1_capture_mic_close(struct snd_pcm_substream *substream)
{
	struct snd_emu10k1 *emu = snd_pcm_substream_chip(substream);

	emu->capture_mic_interrupt = NULL;
	emu->pcm_capture_mic_substream = NULL;
	return 0;
}

static int snd_emu10k1_capture_efx_open(struct snd_pcm_substream *substream)
{
	struct snd_emu10k1 *emu = snd_pcm_substream_chip(substream);
	struct snd_emu10k1_pcm *epcm;
	struct snd_pcm_runtime *runtime = substream->runtime;
	int nefx = emu->audigy ? 64 : 32;
	int idx, err;

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
	runtime->hw = snd_emu10k1_capture_efx;
	runtime->hw.rates = SNDRV_PCM_RATE_48000;
	runtime->hw.rate_min = runtime->hw.rate_max = 48000;
	if (emu->card_capabilities->emu_model) {
		/* TODO
		 * SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000 |
		 * SNDRV_PCM_RATE_88200 | SNDRV_PCM_RATE_96000 |
		 * SNDRV_PCM_RATE_176400 | SNDRV_PCM_RATE_192000
		 * rate_min = 44100,
		 * rate_max = 192000,
		 * Need to add mixer control to fix sample rate
		 *                 
		 * There are 32 mono channels of 16bits each.
		 * 24bit Audio uses 2x channels over 16bit,
		 * 96kHz uses 2x channels over 48kHz,
		 * 192kHz uses 4x channels over 48kHz.
		 * So, for 48kHz 24bit, one has 16 channels,
		 * for 96kHz 24bit, one has 8 channels,
		 * for 192kHz 24bit, one has 4 channels.
		 * 1010rev2 and 1616(m) cards have double that,
		 * but we don't exceed 16 channels anyway.
		 */
#if 1
		switch (emu->emu1010.internal_clock) {
		case 0:
			/* For 44.1kHz */
			runtime->hw.rates = SNDRV_PCM_RATE_44100;
			runtime->hw.rate_min = runtime->hw.rate_max = 44100;
			break;
		case 1:
			/* For 48kHz */
			runtime->hw.rates = SNDRV_PCM_RATE_48000;
			runtime->hw.rate_min = runtime->hw.rate_max = 48000;
			break;
		}
#endif
#if 0
		/* For 96kHz */
		runtime->hw.rates = SNDRV_PCM_RATE_96000;
		runtime->hw.rate_min = runtime->hw.rate_max = 96000;
		runtime->hw.channels_min = runtime->hw.channels_max = 4;
#endif
#if 0
		/* For 192kHz */
		runtime->hw.rates = SNDRV_PCM_RATE_192000;
		runtime->hw.rate_min = runtime->hw.rate_max = 192000;
		runtime->hw.channels_min = runtime->hw.channels_max = 2;
#endif
		runtime->hw.formats = SNDRV_PCM_FMTBIT_S32_LE;
	} else {
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
	}
	err = snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_CHANNELS,
					 &hw_constraints_efx_capture_channels);
	if (err < 0) {
		kfree(epcm);
		return err;
	}
	snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_BUFFER_BYTES,
				   &hw_constraints_capture_buffer_sizes);
	emu->capture_efx_interrupt = snd_emu10k1_pcm_efx_interrupt;
	emu->pcm_capture_efx_substream = substream;
	return 0;
}

static int snd_emu10k1_capture_efx_close(struct snd_pcm_substream *substream)
{
	struct snd_emu10k1 *emu = snd_pcm_substream_chip(substream);

	emu->capture_efx_interrupt = NULL;
	emu->pcm_capture_efx_substream = NULL;
	return 0;
}

static const struct snd_pcm_ops snd_emu10k1_playback_ops = {
	.open =			snd_emu10k1_playback_open,
	.close =		snd_emu10k1_playback_close,
	.hw_params =		snd_emu10k1_playback_hw_params,
	.hw_free =		snd_emu10k1_playback_hw_free,
	.prepare =		snd_emu10k1_playback_prepare,
	.trigger =		snd_emu10k1_playback_trigger,
	.pointer =		snd_emu10k1_playback_pointer,
};

static const struct snd_pcm_ops snd_emu10k1_capture_ops = {
	.open =			snd_emu10k1_capture_open,
	.close =		snd_emu10k1_capture_close,
	.prepare =		snd_emu10k1_capture_prepare,
	.trigger =		snd_emu10k1_capture_trigger,
	.pointer =		snd_emu10k1_capture_pointer,
};

/* EFX playback */
static const struct snd_pcm_ops snd_emu10k1_efx_playback_ops = {
	.open =			snd_emu10k1_efx_playback_open,
	.close =		snd_emu10k1_efx_playback_close,
	.hw_params =		snd_emu10k1_playback_hw_params,
	.hw_free =		snd_emu10k1_playback_hw_free,
	.prepare =		snd_emu10k1_efx_playback_prepare,
	.trigger =		snd_emu10k1_efx_playback_trigger,
	.pointer =		snd_emu10k1_playback_pointer,
};

int snd_emu10k1_pcm(struct snd_emu10k1 *emu, int device)
{
	struct snd_pcm *pcm;
	struct snd_pcm_substream *substream;
	int err;

	err = snd_pcm_new(emu->card, "emu10k1", device, 32, 1, &pcm);
	if (err < 0)
		return err;

	pcm->private_data = emu;

	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_emu10k1_playback_ops);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_emu10k1_capture_ops);

	pcm->info_flags = 0;
	pcm->dev_subclass = SNDRV_PCM_SUBCLASS_GENERIC_MIX;
	strcpy(pcm->name, "ADC Capture/Standard PCM Playback");
	emu->pcm = pcm;

	/* playback substream can't use managed buffers due to alignment */
	for (substream = pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream; substream; substream = substream->next)
		snd_pcm_lib_preallocate_pages(substream, SNDRV_DMA_TYPE_DEV_SG,
					      &emu->pci->dev,
					      64*1024, 64*1024);

	for (substream = pcm->streams[SNDRV_PCM_STREAM_CAPTURE].substream; substream; substream = substream->next)
		snd_pcm_set_managed_buffer(substream, SNDRV_DMA_TYPE_DEV,
					   &emu->pci->dev, 64*1024, 64*1024);

	return 0;
}

int snd_emu10k1_pcm_multi(struct snd_emu10k1 *emu, int device)
{
	struct snd_pcm *pcm;
	struct snd_pcm_substream *substream;
	int err;

	err = snd_pcm_new(emu->card, "emu10k1", device, 1, 0, &pcm);
	if (err < 0)
		return err;

	pcm->private_data = emu;

	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_emu10k1_efx_playback_ops);

	pcm->info_flags = 0;
	pcm->dev_subclass = SNDRV_PCM_SUBCLASS_GENERIC_MIX;
	strcpy(pcm->name, "Multichannel Playback");
	emu->pcm_multi = pcm;

	for (substream = pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream; substream; substream = substream->next)
		snd_pcm_lib_preallocate_pages(substream, SNDRV_DMA_TYPE_DEV_SG,
					      &emu->pci->dev,
					      64*1024, 64*1024);

	return 0;
}


static const struct snd_pcm_ops snd_emu10k1_capture_mic_ops = {
	.open =			snd_emu10k1_capture_mic_open,
	.close =		snd_emu10k1_capture_mic_close,
	.prepare =		snd_emu10k1_capture_prepare,
	.trigger =		snd_emu10k1_capture_trigger,
	.pointer =		snd_emu10k1_capture_pointer,
};

int snd_emu10k1_pcm_mic(struct snd_emu10k1 *emu, int device)
{
	struct snd_pcm *pcm;
	int err;

	err = snd_pcm_new(emu->card, "emu10k1 mic", device, 0, 1, &pcm);
	if (err < 0)
		return err;

	pcm->private_data = emu;

	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_emu10k1_capture_mic_ops);

	pcm->info_flags = 0;
	strcpy(pcm->name, "Mic Capture");
	emu->pcm_mic = pcm;

	snd_pcm_set_managed_buffer_all(pcm, SNDRV_DMA_TYPE_DEV, &emu->pci->dev,
				       64*1024, 64*1024);

	return 0;
}

static int snd_emu10k1_pcm_efx_voices_mask_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	struct snd_emu10k1 *emu = snd_kcontrol_chip(kcontrol);
	int nefx = emu->audigy ? 64 : 32;
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = nefx;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;
	return 0;
}

static int snd_emu10k1_pcm_efx_voices_mask_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_emu10k1 *emu = snd_kcontrol_chip(kcontrol);
	int nefx = emu->audigy ? 64 : 32;
	int idx;
	
	for (idx = 0; idx < nefx; idx++)
		ucontrol->value.integer.value[idx] = (emu->efx_voices_mask[idx / 32] & (1 << (idx % 32))) ? 1 : 0;
	return 0;
}

static int snd_emu10k1_pcm_efx_voices_mask_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_emu10k1 *emu = snd_kcontrol_chip(kcontrol);
	unsigned int nval[2], bits;
	int nefx = emu->audigy ? 64 : 32;
	int change, idx;
	
	nval[0] = nval[1] = 0;
	for (idx = 0, bits = 0; idx < nefx; idx++)
		if (ucontrol->value.integer.value[idx]) {
			nval[idx / 32] |= 1 << (idx % 32);
			bits++;
		}

	if (bits == 9 || bits == 11 || bits == 13 || bits == 15 || bits > 16)
		return -EINVAL;

	spin_lock_irq(&emu->reg_lock);
	change = (nval[0] != emu->efx_voices_mask[0]) ||
		(nval[1] != emu->efx_voices_mask[1]);
	emu->efx_voices_mask[0] = nval[0];
	emu->efx_voices_mask[1] = nval[1];
	spin_unlock_irq(&emu->reg_lock);
	return change;
}

static const struct snd_kcontrol_new snd_emu10k1_pcm_efx_voices_mask = {
	.iface = SNDRV_CTL_ELEM_IFACE_PCM,
	.name = "Captured FX8010 Outputs",
	.info = snd_emu10k1_pcm_efx_voices_mask_info,
	.get = snd_emu10k1_pcm_efx_voices_mask_get,
	.put = snd_emu10k1_pcm_efx_voices_mask_put
};

static const struct snd_pcm_ops snd_emu10k1_capture_efx_ops = {
	.open =			snd_emu10k1_capture_efx_open,
	.close =		snd_emu10k1_capture_efx_close,
	.prepare =		snd_emu10k1_capture_prepare,
	.trigger =		snd_emu10k1_capture_trigger,
	.pointer =		snd_emu10k1_capture_pointer,
};


/* EFX playback */

#define INITIAL_TRAM_SHIFT     14
#define INITIAL_TRAM_POS(size) ((((size) / 2) - INITIAL_TRAM_SHIFT) - 1)

static void snd_emu10k1_fx8010_playback_irq(struct snd_emu10k1 *emu, void *private_data)
{
	struct snd_pcm_substream *substream = private_data;
	snd_pcm_period_elapsed(substream);
}

static void snd_emu10k1_fx8010_playback_tram_poke1(unsigned short *dst_left,
						   unsigned short *dst_right,
						   unsigned short *src,
						   unsigned int count,
						   unsigned int tram_shift)
{
	/*
	dev_dbg(emu->card->dev,
		"tram_poke1: dst_left = 0x%p, dst_right = 0x%p, "
	       "src = 0x%p, count = 0x%x\n",
	       dst_left, dst_right, src, count);
	*/
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

static void fx8010_pb_trans_copy(struct snd_pcm_substream *substream,
				 struct snd_pcm_indirect *rec, size_t bytes)
{
	struct snd_emu10k1 *emu = snd_pcm_substream_chip(substream);
	struct snd_emu10k1_fx8010_pcm *pcm = &emu->fx8010.pcm[substream->number];
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

static int snd_emu10k1_fx8010_playback_transfer(struct snd_pcm_substream *substream)
{
	struct snd_emu10k1 *emu = snd_pcm_substream_chip(substream);
	struct snd_emu10k1_fx8010_pcm *pcm = &emu->fx8010.pcm[substream->number];

	return snd_pcm_indirect_playback_transfer(substream, &pcm->pcm_rec,
						  fx8010_pb_trans_copy);
}

static int snd_emu10k1_fx8010_playback_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_emu10k1 *emu = snd_pcm_substream_chip(substream);
	struct snd_emu10k1_fx8010_pcm *pcm = &emu->fx8010.pcm[substream->number];
	unsigned int i;

	for (i = 0; i < pcm->channels; i++)
		snd_emu10k1_ptr_write(emu, TANKMEMADDRREGBASE + 0x80 + pcm->etram[i], 0, 0);
	return 0;
}

static int snd_emu10k1_fx8010_playback_prepare(struct snd_pcm_substream *substream)
{
	struct snd_emu10k1 *emu = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_emu10k1_fx8010_pcm *pcm = &emu->fx8010.pcm[substream->number];
	unsigned int i;
	
	/*
	dev_dbg(emu->card->dev, "prepare: etram_pages = 0x%p, dma_area = 0x%x, "
	       "buffer_size = 0x%x (0x%x)\n",
	       emu->fx8010.etram_pages, runtime->dma_area,
	       runtime->buffer_size, runtime->buffer_size << 2);
	*/
	memset(&pcm->pcm_rec, 0, sizeof(pcm->pcm_rec));
	pcm->pcm_rec.hw_buffer_size = pcm->buffer_size * 2; /* byte size */
	pcm->pcm_rec.sw_buffer_size = snd_pcm_lib_buffer_bytes(substream);
	pcm->tram_pos = INITIAL_TRAM_POS(pcm->buffer_size);
	pcm->tram_shift = 0;
	snd_emu10k1_ptr_write_multiple(emu, 0,
		emu->gpr_base + pcm->gpr_running, 0,	/* reset */
		emu->gpr_base + pcm->gpr_trigger, 0,	/* reset */
		emu->gpr_base + pcm->gpr_size, runtime->buffer_size,
		emu->gpr_base + pcm->gpr_ptr, 0,	/* reset ptr number */
		emu->gpr_base + pcm->gpr_count, runtime->period_size,
		emu->gpr_base + pcm->gpr_tmpcount, runtime->period_size,
		REGLIST_END);
	for (i = 0; i < pcm->channels; i++)
		snd_emu10k1_ptr_write(emu, TANKMEMADDRREGBASE + 0x80 + pcm->etram[i], 0, (TANKMEMADDRREG_READ|TANKMEMADDRREG_ALIGN) + i * (runtime->buffer_size / pcm->channels));
	return 0;
}

static int snd_emu10k1_fx8010_playback_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_emu10k1 *emu = snd_pcm_substream_chip(substream);
	struct snd_emu10k1_fx8010_pcm *pcm = &emu->fx8010.pcm[substream->number];
	int result = 0;

	spin_lock(&emu->reg_lock);
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		/* follow thru */
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
	case SNDRV_PCM_TRIGGER_RESUME:
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
	case SNDRV_PCM_TRIGGER_SUSPEND:
		snd_emu10k1_fx8010_unregister_irq_handler(emu, &pcm->irq);
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

static snd_pcm_uframes_t snd_emu10k1_fx8010_playback_pointer(struct snd_pcm_substream *substream)
{
	struct snd_emu10k1 *emu = snd_pcm_substream_chip(substream);
	struct snd_emu10k1_fx8010_pcm *pcm = &emu->fx8010.pcm[substream->number];
	size_t ptr; /* byte pointer */

	if (!snd_emu10k1_ptr_read(emu, emu->gpr_base + pcm->gpr_trigger, 0))
		return 0;
	ptr = snd_emu10k1_ptr_read(emu, emu->gpr_base + pcm->gpr_ptr, 0) << 2;
	return snd_pcm_indirect_playback_pointer(substream, &pcm->pcm_rec, ptr);
}

static const struct snd_pcm_hardware snd_emu10k1_fx8010_playback =
{
	.info =			(SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_RESUME |
				 /* SNDRV_PCM_INFO_MMAP_VALID | */ SNDRV_PCM_INFO_PAUSE |
				 SNDRV_PCM_INFO_SYNC_APPLPTR),
	.formats =		SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S16_LE,
	.rates =		SNDRV_PCM_RATE_48000,
	.rate_min =		48000,
	.rate_max =		48000,
	.channels_min =		1,
	.channels_max =		1,
	.buffer_bytes_max =	(128*1024),
	.period_bytes_min =	1024,
	.period_bytes_max =	(128*1024),
	.periods_min =		2,
	.periods_max =		1024,
	.fifo_size =		0,
};

static int snd_emu10k1_fx8010_playback_open(struct snd_pcm_substream *substream)
{
	struct snd_emu10k1 *emu = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_emu10k1_fx8010_pcm *pcm = &emu->fx8010.pcm[substream->number];

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

static int snd_emu10k1_fx8010_playback_close(struct snd_pcm_substream *substream)
{
	struct snd_emu10k1 *emu = snd_pcm_substream_chip(substream);
	struct snd_emu10k1_fx8010_pcm *pcm = &emu->fx8010.pcm[substream->number];

	spin_lock_irq(&emu->reg_lock);
	pcm->opened = 0;
	spin_unlock_irq(&emu->reg_lock);
	return 0;
}

static const struct snd_pcm_ops snd_emu10k1_fx8010_playback_ops = {
	.open =			snd_emu10k1_fx8010_playback_open,
	.close =		snd_emu10k1_fx8010_playback_close,
	.hw_free =		snd_emu10k1_fx8010_playback_hw_free,
	.prepare =		snd_emu10k1_fx8010_playback_prepare,
	.trigger =		snd_emu10k1_fx8010_playback_trigger,
	.pointer =		snd_emu10k1_fx8010_playback_pointer,
	.ack =			snd_emu10k1_fx8010_playback_transfer,
};

int snd_emu10k1_pcm_efx(struct snd_emu10k1 *emu, int device)
{
	struct snd_pcm *pcm;
	struct snd_kcontrol *kctl;
	int err;

	err = snd_pcm_new(emu->card, "emu10k1 efx", device, emu->audigy ? 0 : 8, 1, &pcm);
	if (err < 0)
		return err;

	pcm->private_data = emu;

	if (!emu->audigy)
		snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_emu10k1_fx8010_playback_ops);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_emu10k1_capture_efx_ops);

	pcm->info_flags = 0;
	if (emu->audigy)
		strcpy(pcm->name, "Multichannel Capture");
	else
		strcpy(pcm->name, "Multichannel Capture/PT Playback");
	emu->pcm_efx = pcm;

	if (!emu->card_capabilities->emu_model) {
		// On Sound Blasters, the DSP code copies the EXTINs to FXBUS2.
		// The mask determines which of these and the EXTOUTs the multi-
		// channel capture actually records (the channel order is fixed).
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
		err = snd_ctl_add(emu->card, kctl);
		if (err < 0)
			return err;
	} else {
		// On E-MU cards, the DSP code copies the P16VINs/EMU32INs to
		// FXBUS2. These are already selected & routed by the FPGA,
		// so there is no need to apply additional masking.
	}

	snd_pcm_set_managed_buffer_all(pcm, SNDRV_DMA_TYPE_DEV, &emu->pci->dev,
				       64*1024, 64*1024);

	return 0;
}
