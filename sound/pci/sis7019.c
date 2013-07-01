/*
 *  Driver for SiS7019 Audio Accelerator
 *
 *  Copyright (C) 2004-2007, David Dillow
 *  Written by David Dillow <dave@thedillows.org>
 *  Inspired by the Trident 4D-WaveDX/NX driver.
 *
 *  All rights reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 2.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include <linux/init.h>
#include <linux/pci.h>
#include <linux/time.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <sound/core.h>
#include <sound/ac97_codec.h>
#include <sound/initval.h>
#include "sis7019.h"

MODULE_AUTHOR("David Dillow <dave@thedillows.org>");
MODULE_DESCRIPTION("SiS7019");
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("{{SiS,SiS7019 Audio Accelerator}}");

static int index = SNDRV_DEFAULT_IDX1;	/* Index 0-MAX */
static char *id = SNDRV_DEFAULT_STR1;	/* ID for this card */
static bool enable = 1;
static int codecs = 1;

module_param(index, int, 0444);
MODULE_PARM_DESC(index, "Index value for SiS7019 Audio Accelerator.");
module_param(id, charp, 0444);
MODULE_PARM_DESC(id, "ID string for SiS7019 Audio Accelerator.");
module_param(enable, bool, 0444);
MODULE_PARM_DESC(enable, "Enable SiS7019 Audio Accelerator.");
module_param(codecs, int, 0444);
MODULE_PARM_DESC(codecs, "Set bit to indicate that codec number is expected to be present (default 1)");

static DEFINE_PCI_DEVICE_TABLE(snd_sis7019_ids) = {
	{ PCI_DEVICE(PCI_VENDOR_ID_SI, 0x7019) },
	{ 0, }
};

MODULE_DEVICE_TABLE(pci, snd_sis7019_ids);

/* There are three timing modes for the voices.
 *
 * For both playback and capture, when the buffer is one or two periods long,
 * we use the hardware's built-in Mid-Loop Interrupt and End-Loop Interrupt
 * to let us know when the periods have ended.
 *
 * When performing playback with more than two periods per buffer, we set
 * the "Stop Sample Offset" and tell the hardware to interrupt us when we
 * reach it. We then update the offset and continue on until we are
 * interrupted for the next period.
 *
 * Capture channels do not have a SSO, so we allocate a playback channel to
 * use as a timer for the capture periods. We use the SSO on the playback
 * channel to clock out virtual periods, and adjust the virtual period length
 * to maintain synchronization. This algorithm came from the Trident driver.
 *
 * FIXME: It'd be nice to make use of some of the synth features in the
 * hardware, but a woeful lack of documentation is a significant roadblock.
 */
struct voice {
	u16 flags;
#define 	VOICE_IN_USE		1
#define 	VOICE_CAPTURE		2
#define 	VOICE_SSO_TIMING	4
#define 	VOICE_SYNC_TIMING	8
	u16 sync_cso;
	u16 period_size;
	u16 buffer_size;
	u16 sync_period_size;
	u16 sync_buffer_size;
	u32 sso;
	u32 vperiod;
	struct snd_pcm_substream *substream;
	struct voice *timing;
	void __iomem *ctrl_base;
	void __iomem *wave_base;
	void __iomem *sync_base;
	int num;
};

/* We need four pages to store our wave parameters during a suspend. If
 * we're not doing power management, we still need to allocate a page
 * for the silence buffer.
 */
#ifdef CONFIG_PM_SLEEP
#define SIS_SUSPEND_PAGES	4
#else
#define SIS_SUSPEND_PAGES	1
#endif

struct sis7019 {
	unsigned long ioport;
	void __iomem *ioaddr;
	int irq;
	int codecs_present;

	struct pci_dev *pci;
	struct snd_pcm *pcm;
	struct snd_card *card;
	struct snd_ac97 *ac97[3];

	/* Protect against more than one thread hitting the AC97
	 * registers (in a more polite manner than pounding the hardware
	 * semaphore)
	 */
	struct mutex ac97_mutex;

	/* voice_lock protects allocation/freeing of the voice descriptions
	 */
	spinlock_t voice_lock;

	struct voice voices[64];
	struct voice capture_voice;

	/* Allocate pages to store the internal wave state during
	 * suspends. When we're operating, this can be used as a silence
	 * buffer for a timing channel.
	 */
	void *suspend_state[SIS_SUSPEND_PAGES];

	int silence_users;
	dma_addr_t silence_dma_addr;
};

/* These values are also used by the module param 'codecs' to indicate
 * which codecs should be present.
 */
#define SIS_PRIMARY_CODEC_PRESENT	0x0001
#define SIS_SECONDARY_CODEC_PRESENT	0x0002
#define SIS_TERTIARY_CODEC_PRESENT	0x0004

/* The HW offset parameters (Loop End, Stop Sample, End Sample) have a
 * documented range of 8-0xfff8 samples. Given that they are 0-based,
 * that places our period/buffer range at 9-0xfff9 samples. That makes the
 * max buffer size 0xfff9 samples * 2 channels * 2 bytes per sample, and
 * max samples / min samples gives us the max periods in a buffer.
 *
 * We'll add a constraint upon open that limits the period and buffer sample
 * size to values that are legal for the hardware.
 */
static struct snd_pcm_hardware sis_playback_hw_info = {
	.info = (SNDRV_PCM_INFO_MMAP |
		 SNDRV_PCM_INFO_MMAP_VALID |
		 SNDRV_PCM_INFO_INTERLEAVED |
		 SNDRV_PCM_INFO_BLOCK_TRANSFER |
		 SNDRV_PCM_INFO_SYNC_START |
		 SNDRV_PCM_INFO_RESUME),
	.formats = (SNDRV_PCM_FMTBIT_S8 | SNDRV_PCM_FMTBIT_U8 |
		    SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_U16_LE),
	.rates = SNDRV_PCM_RATE_8000_48000 | SNDRV_PCM_RATE_CONTINUOUS,
	.rate_min = 4000,
	.rate_max = 48000,
	.channels_min = 1,
	.channels_max = 2,
	.buffer_bytes_max = (0xfff9 * 4),
	.period_bytes_min = 9,
	.period_bytes_max = (0xfff9 * 4),
	.periods_min = 1,
	.periods_max = (0xfff9 / 9),
};

static struct snd_pcm_hardware sis_capture_hw_info = {
	.info = (SNDRV_PCM_INFO_MMAP |
		 SNDRV_PCM_INFO_MMAP_VALID |
		 SNDRV_PCM_INFO_INTERLEAVED |
		 SNDRV_PCM_INFO_BLOCK_TRANSFER |
		 SNDRV_PCM_INFO_SYNC_START |
		 SNDRV_PCM_INFO_RESUME),
	.formats = (SNDRV_PCM_FMTBIT_S8 | SNDRV_PCM_FMTBIT_U8 |
		    SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_U16_LE),
	.rates = SNDRV_PCM_RATE_48000,
	.rate_min = 4000,
	.rate_max = 48000,
	.channels_min = 1,
	.channels_max = 2,
	.buffer_bytes_max = (0xfff9 * 4),
	.period_bytes_min = 9,
	.period_bytes_max = (0xfff9 * 4),
	.periods_min = 1,
	.periods_max = (0xfff9 / 9),
};

static void sis_update_sso(struct voice *voice, u16 period)
{
	void __iomem *base = voice->ctrl_base;

	voice->sso += period;
	if (voice->sso >= voice->buffer_size)
		voice->sso -= voice->buffer_size;

	/* Enforce the documented hardware minimum offset */
	if (voice->sso < 8)
		voice->sso = 8;

	/* The SSO is in the upper 16 bits of the register. */
	writew(voice->sso & 0xffff, base + SIS_PLAY_DMA_SSO_ESO + 2);
}

static void sis_update_voice(struct voice *voice)
{
	if (voice->flags & VOICE_SSO_TIMING) {
		sis_update_sso(voice, voice->period_size);
	} else if (voice->flags & VOICE_SYNC_TIMING) {
		int sync;

		/* If we've not hit the end of the virtual period, update
		 * our records and keep going.
		 */
		if (voice->vperiod > voice->period_size) {
			voice->vperiod -= voice->period_size;
			if (voice->vperiod < voice->period_size)
				sis_update_sso(voice, voice->vperiod);
			else
				sis_update_sso(voice, voice->period_size);
			return;
		}

		/* Calculate our relative offset between the target and
		 * the actual CSO value. Since we're operating in a loop,
		 * if the value is more than half way around, we can
		 * consider ourselves wrapped.
		 */
		sync = voice->sync_cso;
		sync -= readw(voice->sync_base + SIS_CAPTURE_DMA_FORMAT_CSO);
		if (sync > (voice->sync_buffer_size / 2))
			sync -= voice->sync_buffer_size;

		/* If sync is positive, then we interrupted too early, and
		 * we'll need to come back in a few samples and try again.
		 * There's a minimum wait, as it takes some time for the DMA
		 * engine to startup, etc...
		 */
		if (sync > 0) {
			if (sync < 16)
				sync = 16;
			sis_update_sso(voice, sync);
			return;
		}

		/* Ok, we interrupted right on time, or (hopefully) just
		 * a bit late. We'll adjst our next waiting period based
		 * on how close we got.
		 *
		 * We need to stay just behind the actual channel to ensure
		 * it really is past a period when we get our interrupt --
		 * otherwise we'll fall into the early code above and have
		 * a minimum wait time, which makes us quite late here,
		 * eating into the user's time to refresh the buffer, esp.
		 * if using small periods.
		 *
		 * If we're less than 9 samples behind, we're on target.
		 * Otherwise, shorten the next vperiod by the amount we've
		 * been delayed.
		 */
		if (sync > -9)
			voice->vperiod = voice->sync_period_size + 1;
		else
			voice->vperiod = voice->sync_period_size + sync + 10;

		if (voice->vperiod < voice->buffer_size) {
			sis_update_sso(voice, voice->vperiod);
			voice->vperiod = 0;
		} else
			sis_update_sso(voice, voice->period_size);

		sync = voice->sync_cso + voice->sync_period_size;
		if (sync >= voice->sync_buffer_size)
			sync -= voice->sync_buffer_size;
		voice->sync_cso = sync;
	}

	snd_pcm_period_elapsed(voice->substream);
}

static void sis_voice_irq(u32 status, struct voice *voice)
{
	int bit;

	while (status) {
		bit = __ffs(status);
		status >>= bit + 1;
		voice += bit;
		sis_update_voice(voice);
		voice++;
	}
}

static irqreturn_t sis_interrupt(int irq, void *dev)
{
	struct sis7019 *sis = dev;
	unsigned long io = sis->ioport;
	struct voice *voice;
	u32 intr, status;

	/* We only use the DMA interrupts, and we don't enable any other
	 * source of interrupts. But, it is possible to see an interrupt
	 * status that didn't actually interrupt us, so eliminate anything
	 * we're not expecting to avoid falsely claiming an IRQ, and an
	 * ensuing endless loop.
	 */
	intr = inl(io + SIS_GISR);
	intr &= SIS_GISR_AUDIO_PLAY_DMA_IRQ_STATUS |
		SIS_GISR_AUDIO_RECORD_DMA_IRQ_STATUS;
	if (!intr)
		return IRQ_NONE;

	do {
		status = inl(io + SIS_PISR_A);
		if (status) {
			sis_voice_irq(status, sis->voices);
			outl(status, io + SIS_PISR_A);
		}

		status = inl(io + SIS_PISR_B);
		if (status) {
			sis_voice_irq(status, &sis->voices[32]);
			outl(status, io + SIS_PISR_B);
		}

		status = inl(io + SIS_RISR);
		if (status) {
			voice = &sis->capture_voice;
			if (!voice->timing)
				snd_pcm_period_elapsed(voice->substream);

			outl(status, io + SIS_RISR);
		}

		outl(intr, io + SIS_GISR);
		intr = inl(io + SIS_GISR);
		intr &= SIS_GISR_AUDIO_PLAY_DMA_IRQ_STATUS |
			SIS_GISR_AUDIO_RECORD_DMA_IRQ_STATUS;
	} while (intr);

	return IRQ_HANDLED;
}

static u32 sis_rate_to_delta(unsigned int rate)
{
	u32 delta;

	/* This was copied from the trident driver, but it seems its gotten
	 * around a bit... nevertheless, it works well.
	 *
	 * We special case 44100 and 8000 since rounding with the equation
	 * does not give us an accurate enough value. For 11025 and 22050
	 * the equation gives us the best answer. All other frequencies will
	 * also use the equation. JDW
	 */
	if (rate == 44100)
		delta = 0xeb3;
	else if (rate == 8000)
		delta = 0x2ab;
	else if (rate == 48000)
		delta = 0x1000;
	else
		delta = (((rate << 12) + 24000) / 48000) & 0x0000ffff;
	return delta;
}

static void __sis_map_silence(struct sis7019 *sis)
{
	/* Helper function: must hold sis->voice_lock on entry */
	if (!sis->silence_users)
		sis->silence_dma_addr = pci_map_single(sis->pci,
						sis->suspend_state[0],
						4096, PCI_DMA_TODEVICE);
	sis->silence_users++;
}

static void __sis_unmap_silence(struct sis7019 *sis)
{
	/* Helper function: must hold sis->voice_lock on entry */
	sis->silence_users--;
	if (!sis->silence_users)
		pci_unmap_single(sis->pci, sis->silence_dma_addr, 4096,
					PCI_DMA_TODEVICE);
}

static void sis_free_voice(struct sis7019 *sis, struct voice *voice)
{
	unsigned long flags;

	spin_lock_irqsave(&sis->voice_lock, flags);
	if (voice->timing) {
		__sis_unmap_silence(sis);
		voice->timing->flags &= ~(VOICE_IN_USE | VOICE_SSO_TIMING |
						VOICE_SYNC_TIMING);
		voice->timing = NULL;
	}
	voice->flags &= ~(VOICE_IN_USE | VOICE_SSO_TIMING | VOICE_SYNC_TIMING);
	spin_unlock_irqrestore(&sis->voice_lock, flags);
}

static struct voice *__sis_alloc_playback_voice(struct sis7019 *sis)
{
	/* Must hold the voice_lock on entry */
	struct voice *voice;
	int i;

	for (i = 0; i < 64; i++) {
		voice = &sis->voices[i];
		if (voice->flags & VOICE_IN_USE)
			continue;
		voice->flags |= VOICE_IN_USE;
		goto found_one;
	}
	voice = NULL;

found_one:
	return voice;
}

static struct voice *sis_alloc_playback_voice(struct sis7019 *sis)
{
	struct voice *voice;
	unsigned long flags;

	spin_lock_irqsave(&sis->voice_lock, flags);
	voice = __sis_alloc_playback_voice(sis);
	spin_unlock_irqrestore(&sis->voice_lock, flags);

	return voice;
}

static int sis_alloc_timing_voice(struct snd_pcm_substream *substream,
					struct snd_pcm_hw_params *hw_params)
{
	struct sis7019 *sis = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct voice *voice = runtime->private_data;
	unsigned int period_size, buffer_size;
	unsigned long flags;
	int needed;

	/* If there are one or two periods per buffer, we don't need a
	 * timing voice, as we can use the capture channel's interrupts
	 * to clock out the periods.
	 */
	period_size = params_period_size(hw_params);
	buffer_size = params_buffer_size(hw_params);
	needed = (period_size != buffer_size &&
			period_size != (buffer_size / 2));

	if (needed && !voice->timing) {
		spin_lock_irqsave(&sis->voice_lock, flags);
		voice->timing = __sis_alloc_playback_voice(sis);
		if (voice->timing)
			__sis_map_silence(sis);
		spin_unlock_irqrestore(&sis->voice_lock, flags);
		if (!voice->timing)
			return -ENOMEM;
		voice->timing->substream = substream;
	} else if (!needed && voice->timing) {
		sis_free_voice(sis, voice);
		voice->timing = NULL;
	}

	return 0;
}

static int sis_playback_open(struct snd_pcm_substream *substream)
{
	struct sis7019 *sis = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct voice *voice;

	voice = sis_alloc_playback_voice(sis);
	if (!voice)
		return -EAGAIN;

	voice->substream = substream;
	runtime->private_data = voice;
	runtime->hw = sis_playback_hw_info;
	snd_pcm_hw_constraint_minmax(runtime, SNDRV_PCM_HW_PARAM_PERIOD_SIZE,
						9, 0xfff9);
	snd_pcm_hw_constraint_minmax(runtime, SNDRV_PCM_HW_PARAM_BUFFER_SIZE,
						9, 0xfff9);
	snd_pcm_set_sync(substream);
	return 0;
}

static int sis_substream_close(struct snd_pcm_substream *substream)
{
	struct sis7019 *sis = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct voice *voice = runtime->private_data;

	sis_free_voice(sis, voice);
	return 0;
}

static int sis_playback_hw_params(struct snd_pcm_substream *substream,
					struct snd_pcm_hw_params *hw_params)
{
	return snd_pcm_lib_malloc_pages(substream,
					params_buffer_bytes(hw_params));
}

static int sis_hw_free(struct snd_pcm_substream *substream)
{
	return snd_pcm_lib_free_pages(substream);
}

static int sis_pcm_playback_prepare(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct voice *voice = runtime->private_data;
	void __iomem *ctrl_base = voice->ctrl_base;
	void __iomem *wave_base = voice->wave_base;
	u32 format, dma_addr, control, sso_eso, delta, reg;
	u16 leo;

	/* We rely on the PCM core to ensure that the parameters for this
	 * substream do not change on us while we're programming the HW.
	 */
	format = 0;
	if (snd_pcm_format_width(runtime->format) == 8)
		format |= SIS_PLAY_DMA_FORMAT_8BIT;
	if (!snd_pcm_format_signed(runtime->format))
		format |= SIS_PLAY_DMA_FORMAT_UNSIGNED;
	if (runtime->channels == 1)
		format |= SIS_PLAY_DMA_FORMAT_MONO;

	/* The baseline setup is for a single period per buffer, and
	 * we add bells and whistles as needed from there.
	 */
	dma_addr = runtime->dma_addr;
	leo = runtime->buffer_size - 1;
	control = leo | SIS_PLAY_DMA_LOOP | SIS_PLAY_DMA_INTR_AT_LEO;
	sso_eso = leo;

	if (runtime->period_size == (runtime->buffer_size / 2)) {
		control |= SIS_PLAY_DMA_INTR_AT_MLP;
	} else if (runtime->period_size != runtime->buffer_size) {
		voice->flags |= VOICE_SSO_TIMING;
		voice->sso = runtime->period_size - 1;
		voice->period_size = runtime->period_size;
		voice->buffer_size = runtime->buffer_size;

		control &= ~SIS_PLAY_DMA_INTR_AT_LEO;
		control |= SIS_PLAY_DMA_INTR_AT_SSO;
		sso_eso |= (runtime->period_size - 1) << 16;
	}

	delta = sis_rate_to_delta(runtime->rate);

	/* Ok, we're ready to go, set up the channel.
	 */
	writel(format, ctrl_base + SIS_PLAY_DMA_FORMAT_CSO);
	writel(dma_addr, ctrl_base + SIS_PLAY_DMA_BASE);
	writel(control, ctrl_base + SIS_PLAY_DMA_CONTROL);
	writel(sso_eso, ctrl_base + SIS_PLAY_DMA_SSO_ESO);

	for (reg = 0; reg < SIS_WAVE_SIZE; reg += 4)
		writel(0, wave_base + reg);

	writel(SIS_WAVE_GENERAL_WAVE_VOLUME, wave_base + SIS_WAVE_GENERAL);
	writel(delta << 16, wave_base + SIS_WAVE_GENERAL_ARTICULATION);
	writel(SIS_WAVE_CHANNEL_CONTROL_FIRST_SAMPLE |
			SIS_WAVE_CHANNEL_CONTROL_AMP_ENABLE |
			SIS_WAVE_CHANNEL_CONTROL_INTERPOLATE_ENABLE,
			wave_base + SIS_WAVE_CHANNEL_CONTROL);

	/* Force PCI writes to post. */
	readl(ctrl_base);

	return 0;
}

static int sis_pcm_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct sis7019 *sis = snd_pcm_substream_chip(substream);
	unsigned long io = sis->ioport;
	struct snd_pcm_substream *s;
	struct voice *voice;
	void *chip;
	int starting;
	u32 record = 0;
	u32 play[2] = { 0, 0 };

	/* No locks needed, as the PCM core will hold the locks on the
	 * substreams, and the HW will only start/stop the indicated voices
	 * without changing the state of the others.
	 */
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
	case SNDRV_PCM_TRIGGER_RESUME:
		starting = 1;
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		starting = 0;
		break;
	default:
		return -EINVAL;
	}

	snd_pcm_group_for_each_entry(s, substream) {
		/* Make sure it is for us... */
		chip = snd_pcm_substream_chip(s);
		if (chip != sis)
			continue;

		voice = s->runtime->private_data;
		if (voice->flags & VOICE_CAPTURE) {
			record |= 1 << voice->num;
			voice = voice->timing;
		}

		/* voice could be NULL if this a recording stream, and it
		 * doesn't have an external timing channel.
		 */
		if (voice)
			play[voice->num / 32] |= 1 << (voice->num & 0x1f);

		snd_pcm_trigger_done(s, substream);
	}

	if (starting) {
		if (record)
			outl(record, io + SIS_RECORD_START_REG);
		if (play[0])
			outl(play[0], io + SIS_PLAY_START_A_REG);
		if (play[1])
			outl(play[1], io + SIS_PLAY_START_B_REG);
	} else {
		if (record)
			outl(record, io + SIS_RECORD_STOP_REG);
		if (play[0])
			outl(play[0], io + SIS_PLAY_STOP_A_REG);
		if (play[1])
			outl(play[1], io + SIS_PLAY_STOP_B_REG);
	}
	return 0;
}

static snd_pcm_uframes_t sis_pcm_pointer(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct voice *voice = runtime->private_data;
	u32 cso;

	cso = readl(voice->ctrl_base + SIS_PLAY_DMA_FORMAT_CSO);
	cso &= 0xffff;
	return cso;
}

static int sis_capture_open(struct snd_pcm_substream *substream)
{
	struct sis7019 *sis = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct voice *voice = &sis->capture_voice;
	unsigned long flags;

	/* FIXME: The driver only supports recording from one channel
	 * at the moment, but it could support more.
	 */
	spin_lock_irqsave(&sis->voice_lock, flags);
	if (voice->flags & VOICE_IN_USE)
		voice = NULL;
	else
		voice->flags |= VOICE_IN_USE;
	spin_unlock_irqrestore(&sis->voice_lock, flags);

	if (!voice)
		return -EAGAIN;

	voice->substream = substream;
	runtime->private_data = voice;
	runtime->hw = sis_capture_hw_info;
	runtime->hw.rates = sis->ac97[0]->rates[AC97_RATES_ADC];
	snd_pcm_limit_hw_rates(runtime);
	snd_pcm_hw_constraint_minmax(runtime, SNDRV_PCM_HW_PARAM_PERIOD_SIZE,
						9, 0xfff9);
	snd_pcm_hw_constraint_minmax(runtime, SNDRV_PCM_HW_PARAM_BUFFER_SIZE,
						9, 0xfff9);
	snd_pcm_set_sync(substream);
	return 0;
}

static int sis_capture_hw_params(struct snd_pcm_substream *substream,
					struct snd_pcm_hw_params *hw_params)
{
	struct sis7019 *sis = snd_pcm_substream_chip(substream);
	int rc;

	rc = snd_ac97_set_rate(sis->ac97[0], AC97_PCM_LR_ADC_RATE,
						params_rate(hw_params));
	if (rc)
		goto out;

	rc = snd_pcm_lib_malloc_pages(substream,
					params_buffer_bytes(hw_params));
	if (rc < 0)
		goto out;

	rc = sis_alloc_timing_voice(substream, hw_params);

out:
	return rc;
}

static void sis_prepare_timing_voice(struct voice *voice,
					struct snd_pcm_substream *substream)
{
	struct sis7019 *sis = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct voice *timing = voice->timing;
	void __iomem *play_base = timing->ctrl_base;
	void __iomem *wave_base = timing->wave_base;
	u16 buffer_size, period_size;
	u32 format, control, sso_eso, delta;
	u32 vperiod, sso, reg;

	/* Set our initial buffer and period as large as we can given a
	 * single page of silence.
	 */
	buffer_size = 4096 / runtime->channels;
	buffer_size /= snd_pcm_format_size(runtime->format, 1);
	period_size = buffer_size;

	/* Initially, we want to interrupt just a bit behind the end of
	 * the period we're clocking out. 12 samples seems to give a good
	 * delay.
	 *
	 * We want to spread our interrupts throughout the virtual period,
	 * so that we don't end up with two interrupts back to back at the
	 * end -- this helps minimize the effects of any jitter. Adjust our
	 * clocking period size so that the last period is at least a fourth
	 * of a full period.
	 *
	 * This is all moot if we don't need to use virtual periods.
	 */
	vperiod = runtime->period_size + 12;
	if (vperiod > period_size) {
		u16 tail = vperiod % period_size;
		u16 quarter_period = period_size / 4;

		if (tail && tail < quarter_period) {
			u16 loops = vperiod / period_size;

			tail = quarter_period - tail;
			tail += loops - 1;
			tail /= loops;
			period_size -= tail;
		}

		sso = period_size - 1;
	} else {
		/* The initial period will fit inside the buffer, so we
		 * don't need to use virtual periods -- disable them.
		 */
		period_size = runtime->period_size;
		sso = vperiod - 1;
		vperiod = 0;
	}

	/* The interrupt handler implements the timing synchronization, so
	 * setup its state.
	 */
	timing->flags |= VOICE_SYNC_TIMING;
	timing->sync_base = voice->ctrl_base;
	timing->sync_cso = runtime->period_size;
	timing->sync_period_size = runtime->period_size;
	timing->sync_buffer_size = runtime->buffer_size;
	timing->period_size = period_size;
	timing->buffer_size = buffer_size;
	timing->sso = sso;
	timing->vperiod = vperiod;

	/* Using unsigned samples with the all-zero silence buffer
	 * forces the output to the lower rail, killing playback.
	 * So ignore unsigned vs signed -- it doesn't change the timing.
	 */
	format = 0;
	if (snd_pcm_format_width(runtime->format) == 8)
		format = SIS_CAPTURE_DMA_FORMAT_8BIT;
	if (runtime->channels == 1)
		format |= SIS_CAPTURE_DMA_FORMAT_MONO;

	control = timing->buffer_size - 1;
	control |= SIS_PLAY_DMA_LOOP | SIS_PLAY_DMA_INTR_AT_SSO;
	sso_eso = timing->buffer_size - 1;
	sso_eso |= timing->sso << 16;

	delta = sis_rate_to_delta(runtime->rate);

	/* We've done the math, now configure the channel.
	 */
	writel(format, play_base + SIS_PLAY_DMA_FORMAT_CSO);
	writel(sis->silence_dma_addr, play_base + SIS_PLAY_DMA_BASE);
	writel(control, play_base + SIS_PLAY_DMA_CONTROL);
	writel(sso_eso, play_base + SIS_PLAY_DMA_SSO_ESO);

	for (reg = 0; reg < SIS_WAVE_SIZE; reg += 4)
		writel(0, wave_base + reg);

	writel(SIS_WAVE_GENERAL_WAVE_VOLUME, wave_base + SIS_WAVE_GENERAL);
	writel(delta << 16, wave_base + SIS_WAVE_GENERAL_ARTICULATION);
	writel(SIS_WAVE_CHANNEL_CONTROL_FIRST_SAMPLE |
			SIS_WAVE_CHANNEL_CONTROL_AMP_ENABLE |
			SIS_WAVE_CHANNEL_CONTROL_INTERPOLATE_ENABLE,
			wave_base + SIS_WAVE_CHANNEL_CONTROL);
}

static int sis_pcm_capture_prepare(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct voice *voice = runtime->private_data;
	void __iomem *rec_base = voice->ctrl_base;
	u32 format, dma_addr, control;
	u16 leo;

	/* We rely on the PCM core to ensure that the parameters for this
	 * substream do not change on us while we're programming the HW.
	 */
	format = 0;
	if (snd_pcm_format_width(runtime->format) == 8)
		format = SIS_CAPTURE_DMA_FORMAT_8BIT;
	if (!snd_pcm_format_signed(runtime->format))
		format |= SIS_CAPTURE_DMA_FORMAT_UNSIGNED;
	if (runtime->channels == 1)
		format |= SIS_CAPTURE_DMA_FORMAT_MONO;

	dma_addr = runtime->dma_addr;
	leo = runtime->buffer_size - 1;
	control = leo | SIS_CAPTURE_DMA_LOOP;

	/* If we've got more than two periods per buffer, then we have
	 * use a timing voice to clock out the periods. Otherwise, we can
	 * use the capture channel's interrupts.
	 */
	if (voice->timing) {
		sis_prepare_timing_voice(voice, substream);
	} else {
		control |= SIS_CAPTURE_DMA_INTR_AT_LEO;
		if (runtime->period_size != runtime->buffer_size)
			control |= SIS_CAPTURE_DMA_INTR_AT_MLP;
	}

	writel(format, rec_base + SIS_CAPTURE_DMA_FORMAT_CSO);
	writel(dma_addr, rec_base + SIS_CAPTURE_DMA_BASE);
	writel(control, rec_base + SIS_CAPTURE_DMA_CONTROL);

	/* Force the writes to post. */
	readl(rec_base);

	return 0;
}

static struct snd_pcm_ops sis_playback_ops = {
	.open = sis_playback_open,
	.close = sis_substream_close,
	.ioctl = snd_pcm_lib_ioctl,
	.hw_params = sis_playback_hw_params,
	.hw_free = sis_hw_free,
	.prepare = sis_pcm_playback_prepare,
	.trigger = sis_pcm_trigger,
	.pointer = sis_pcm_pointer,
};

static struct snd_pcm_ops sis_capture_ops = {
	.open = sis_capture_open,
	.close = sis_substream_close,
	.ioctl = snd_pcm_lib_ioctl,
	.hw_params = sis_capture_hw_params,
	.hw_free = sis_hw_free,
	.prepare = sis_pcm_capture_prepare,
	.trigger = sis_pcm_trigger,
	.pointer = sis_pcm_pointer,
};

static int sis_pcm_create(struct sis7019 *sis)
{
	struct snd_pcm *pcm;
	int rc;

	/* We have 64 voices, and the driver currently records from
	 * only one channel, though that could change in the future.
	 */
	rc = snd_pcm_new(sis->card, "SiS7019", 0, 64, 1, &pcm);
	if (rc)
		return rc;

	pcm->private_data = sis;
	strcpy(pcm->name, "SiS7019");
	sis->pcm = pcm;

	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &sis_playback_ops);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &sis_capture_ops);

	/* Try to preallocate some memory, but it's not the end of the
	 * world if this fails.
	 */
	snd_pcm_lib_preallocate_pages_for_all(pcm, SNDRV_DMA_TYPE_DEV,
				snd_dma_pci_data(sis->pci), 64*1024, 128*1024);

	return 0;
}

static unsigned short sis_ac97_rw(struct sis7019 *sis, int codec, u32 cmd)
{
	unsigned long io = sis->ioport;
	unsigned short val = 0xffff;
	u16 status;
	u16 rdy;
	int count;
	static const u16 codec_ready[3] = {
		SIS_AC97_STATUS_CODEC_READY,
		SIS_AC97_STATUS_CODEC2_READY,
		SIS_AC97_STATUS_CODEC3_READY,
	};

	rdy = codec_ready[codec];


	/* Get the AC97 semaphore -- software first, so we don't spin
	 * pounding out IO reads on the hardware semaphore...
	 */
	mutex_lock(&sis->ac97_mutex);

	count = 0xffff;
	while ((inw(io + SIS_AC97_SEMA) & SIS_AC97_SEMA_BUSY) && --count)
		udelay(1);

	if (!count)
		goto timeout;

	/* ... and wait for any outstanding commands to complete ...
	 */
	count = 0xffff;
	do {
		status = inw(io + SIS_AC97_STATUS);
		if ((status & rdy) && !(status & SIS_AC97_STATUS_BUSY))
			break;

		udelay(1);
	} while (--count);

	if (!count)
		goto timeout_sema;

	/* ... before sending our command and waiting for it to finish ...
	 */
	outl(cmd, io + SIS_AC97_CMD);
	udelay(10);

	count = 0xffff;
	while ((inw(io + SIS_AC97_STATUS) & SIS_AC97_STATUS_BUSY) && --count)
		udelay(1);

	/* ... and reading the results (if any).
	 */
	val = inl(io + SIS_AC97_CMD) >> 16;

timeout_sema:
	outl(SIS_AC97_SEMA_RELEASE, io + SIS_AC97_SEMA);
timeout:
	mutex_unlock(&sis->ac97_mutex);

	if (!count) {
		dev_err(&sis->pci->dev, "ac97 codec %d timeout cmd 0x%08x\n",
					codec, cmd);
	}

	return val;
}

static void sis_ac97_write(struct snd_ac97 *ac97, unsigned short reg,
				unsigned short val)
{
	static const u32 cmd[3] = {
		SIS_AC97_CMD_CODEC_WRITE,
		SIS_AC97_CMD_CODEC2_WRITE,
		SIS_AC97_CMD_CODEC3_WRITE,
	};
	sis_ac97_rw(ac97->private_data, ac97->num,
			(val << 16) | (reg << 8) | cmd[ac97->num]);
}

static unsigned short sis_ac97_read(struct snd_ac97 *ac97, unsigned short reg)
{
	static const u32 cmd[3] = {
		SIS_AC97_CMD_CODEC_READ,
		SIS_AC97_CMD_CODEC2_READ,
		SIS_AC97_CMD_CODEC3_READ,
	};
	return sis_ac97_rw(ac97->private_data, ac97->num,
					(reg << 8) | cmd[ac97->num]);
}

static int sis_mixer_create(struct sis7019 *sis)
{
	struct snd_ac97_bus *bus;
	struct snd_ac97_template ac97;
	static struct snd_ac97_bus_ops ops = {
		.write = sis_ac97_write,
		.read = sis_ac97_read,
	};
	int rc;

	memset(&ac97, 0, sizeof(ac97));
	ac97.private_data = sis;

	rc = snd_ac97_bus(sis->card, 0, &ops, NULL, &bus);
	if (!rc && sis->codecs_present & SIS_PRIMARY_CODEC_PRESENT)
		rc = snd_ac97_mixer(bus, &ac97, &sis->ac97[0]);
	ac97.num = 1;
	if (!rc && (sis->codecs_present & SIS_SECONDARY_CODEC_PRESENT))
		rc = snd_ac97_mixer(bus, &ac97, &sis->ac97[1]);
	ac97.num = 2;
	if (!rc && (sis->codecs_present & SIS_TERTIARY_CODEC_PRESENT))
		rc = snd_ac97_mixer(bus, &ac97, &sis->ac97[2]);

	/* If we return an error here, then snd_card_free() should
	 * free up any ac97 codecs that got created, as well as the bus.
	 */
	return rc;
}

static void sis_free_suspend(struct sis7019 *sis)
{
	int i;

	for (i = 0; i < SIS_SUSPEND_PAGES; i++)
		kfree(sis->suspend_state[i]);
}

static int sis_chip_free(struct sis7019 *sis)
{
	/* Reset the chip, and disable all interrputs.
	 */
	outl(SIS_GCR_SOFTWARE_RESET, sis->ioport + SIS_GCR);
	udelay(25);
	outl(0, sis->ioport + SIS_GCR);
	outl(0, sis->ioport + SIS_GIER);

	/* Now, free everything we allocated.
	 */
	if (sis->irq >= 0)
		free_irq(sis->irq, sis);

	if (sis->ioaddr)
		iounmap(sis->ioaddr);

	pci_release_regions(sis->pci);
	pci_disable_device(sis->pci);

	sis_free_suspend(sis);
	return 0;
}

static int sis_dev_free(struct snd_device *dev)
{
	struct sis7019 *sis = dev->device_data;
	return sis_chip_free(sis);
}

static int sis_chip_init(struct sis7019 *sis)
{
	unsigned long io = sis->ioport;
	void __iomem *ioaddr = sis->ioaddr;
	unsigned long timeout;
	u16 status;
	int count;
	int i;

	/* Reset the audio controller
	 */
	outl(SIS_GCR_SOFTWARE_RESET, io + SIS_GCR);
	udelay(25);
	outl(0, io + SIS_GCR);

	/* Get the AC-link semaphore, and reset the codecs
	 */
	count = 0xffff;
	while ((inw(io + SIS_AC97_SEMA) & SIS_AC97_SEMA_BUSY) && --count)
		udelay(1);

	if (!count)
		return -EIO;

	outl(SIS_AC97_CMD_CODEC_COLD_RESET, io + SIS_AC97_CMD);
	udelay(250);

	count = 0xffff;
	while ((inw(io + SIS_AC97_STATUS) & SIS_AC97_STATUS_BUSY) && --count)
		udelay(1);

	/* Command complete, we can let go of the semaphore now.
	 */
	outl(SIS_AC97_SEMA_RELEASE, io + SIS_AC97_SEMA);
	if (!count)
		return -EIO;

	/* Now that we've finished the reset, find out what's attached.
	 * There are some codec/board combinations that take an extremely
	 * long time to come up. 350+ ms has been observed in the field,
	 * so we'll give them up to 500ms.
	 */
	sis->codecs_present = 0;
	timeout = msecs_to_jiffies(500) + jiffies;
	while (time_before_eq(jiffies, timeout)) {
		status = inl(io + SIS_AC97_STATUS);
		if (status & SIS_AC97_STATUS_CODEC_READY)
			sis->codecs_present |= SIS_PRIMARY_CODEC_PRESENT;
		if (status & SIS_AC97_STATUS_CODEC2_READY)
			sis->codecs_present |= SIS_SECONDARY_CODEC_PRESENT;
		if (status & SIS_AC97_STATUS_CODEC3_READY)
			sis->codecs_present |= SIS_TERTIARY_CODEC_PRESENT;

		if (sis->codecs_present == codecs)
			break;

		msleep(1);
	}

	/* All done, check for errors.
	 */
	if (!sis->codecs_present) {
		dev_err(&sis->pci->dev, "could not find any codecs\n");
		return -EIO;
	}

	if (sis->codecs_present != codecs) {
		dev_warn(&sis->pci->dev, "missing codecs, found %0x, expected %0x\n",
					 sis->codecs_present, codecs);
	}

	/* Let the hardware know that the audio driver is alive,
	 * and enable PCM slots on the AC-link for L/R playback (3 & 4) and
	 * record channels. We're going to want to use Variable Rate Audio
	 * for recording, to avoid needlessly resampling from 48kHZ.
	 */
	outl(SIS_AC97_CONF_AUDIO_ALIVE, io + SIS_AC97_CONF);
	outl(SIS_AC97_CONF_AUDIO_ALIVE | SIS_AC97_CONF_PCM_LR_ENABLE |
		SIS_AC97_CONF_PCM_CAP_MIC_ENABLE |
		SIS_AC97_CONF_PCM_CAP_LR_ENABLE |
		SIS_AC97_CONF_CODEC_VRA_ENABLE, io + SIS_AC97_CONF);

	/* All AC97 PCM slots should be sourced from sub-mixer 0.
	 */
	outl(0, io + SIS_AC97_PSR);

	/* There is only one valid DMA setup for a PCI environment.
	 */
	outl(SIS_DMA_CSR_PCI_SETTINGS, io + SIS_DMA_CSR);

	/* Reset the synchronization groups for all of the channels
	 * to be asynchronous. If we start doing SPDIF or 5.1 sound, etc.
	 * we'll need to change how we handle these. Until then, we just
	 * assign sub-mixer 0 to all playback channels, and avoid any
	 * attenuation on the audio.
	 */
	outl(0, io + SIS_PLAY_SYNC_GROUP_A);
	outl(0, io + SIS_PLAY_SYNC_GROUP_B);
	outl(0, io + SIS_PLAY_SYNC_GROUP_C);
	outl(0, io + SIS_PLAY_SYNC_GROUP_D);
	outl(0, io + SIS_MIXER_SYNC_GROUP);

	for (i = 0; i < 64; i++) {
		writel(i, SIS_MIXER_START_ADDR(ioaddr, i));
		writel(SIS_MIXER_RIGHT_NO_ATTEN | SIS_MIXER_LEFT_NO_ATTEN |
				SIS_MIXER_DEST_0, SIS_MIXER_ADDR(ioaddr, i));
	}

	/* Don't attenuate any audio set for the wave amplifier.
	 *
	 * FIXME: Maximum attenuation is set for the music amp, which will
	 * need to change if we start using the synth engine.
	 */
	outl(0xffff0000, io + SIS_WEVCR);

	/* Ensure that the wave engine is in normal operating mode.
	 */
	outl(0, io + SIS_WECCR);

	/* Go ahead and enable the DMA interrupts. They won't go live
	 * until we start a channel.
	 */
	outl(SIS_GIER_AUDIO_PLAY_DMA_IRQ_ENABLE |
		SIS_GIER_AUDIO_RECORD_DMA_IRQ_ENABLE, io + SIS_GIER);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int sis_suspend(struct device *dev)
{
	struct pci_dev *pci = to_pci_dev(dev);
	struct snd_card *card = dev_get_drvdata(dev);
	struct sis7019 *sis = card->private_data;
	void __iomem *ioaddr = sis->ioaddr;
	int i;

	snd_power_change_state(card, SNDRV_CTL_POWER_D3hot);
	snd_pcm_suspend_all(sis->pcm);
	if (sis->codecs_present & SIS_PRIMARY_CODEC_PRESENT)
		snd_ac97_suspend(sis->ac97[0]);
	if (sis->codecs_present & SIS_SECONDARY_CODEC_PRESENT)
		snd_ac97_suspend(sis->ac97[1]);
	if (sis->codecs_present & SIS_TERTIARY_CODEC_PRESENT)
		snd_ac97_suspend(sis->ac97[2]);

	/* snd_pcm_suspend_all() stopped all channels, so we're quiescent.
	 */
	if (sis->irq >= 0) {
		free_irq(sis->irq, sis);
		sis->irq = -1;
	}

	/* Save the internal state away
	 */
	for (i = 0; i < 4; i++) {
		memcpy_fromio(sis->suspend_state[i], ioaddr, 4096);
		ioaddr += 4096;
	}

	pci_disable_device(pci);
	pci_save_state(pci);
	pci_set_power_state(pci, PCI_D3hot);
	return 0;
}

static int sis_resume(struct device *dev)
{
	struct pci_dev *pci = to_pci_dev(dev);
	struct snd_card *card = dev_get_drvdata(dev);
	struct sis7019 *sis = card->private_data;
	void __iomem *ioaddr = sis->ioaddr;
	int i;

	pci_set_power_state(pci, PCI_D0);
	pci_restore_state(pci);

	if (pci_enable_device(pci) < 0) {
		dev_err(&pci->dev, "unable to re-enable device\n");
		goto error;
	}

	if (sis_chip_init(sis)) {
		dev_err(&pci->dev, "unable to re-init controller\n");
		goto error;
	}

	if (request_irq(pci->irq, sis_interrupt, IRQF_SHARED,
			KBUILD_MODNAME, sis)) {
		dev_err(&pci->dev, "unable to regain IRQ %d\n", pci->irq);
		goto error;
	}

	/* Restore saved state, then clear out the page we use for the
	 * silence buffer.
	 */
	for (i = 0; i < 4; i++) {
		memcpy_toio(ioaddr, sis->suspend_state[i], 4096);
		ioaddr += 4096;
	}

	memset(sis->suspend_state[0], 0, 4096);

	sis->irq = pci->irq;
	pci_set_master(pci);

	if (sis->codecs_present & SIS_PRIMARY_CODEC_PRESENT)
		snd_ac97_resume(sis->ac97[0]);
	if (sis->codecs_present & SIS_SECONDARY_CODEC_PRESENT)
		snd_ac97_resume(sis->ac97[1]);
	if (sis->codecs_present & SIS_TERTIARY_CODEC_PRESENT)
		snd_ac97_resume(sis->ac97[2]);

	snd_power_change_state(card, SNDRV_CTL_POWER_D0);
	return 0;

error:
	snd_card_disconnect(card);
	return -EIO;
}

static SIMPLE_DEV_PM_OPS(sis_pm, sis_suspend, sis_resume);
#define SIS_PM_OPS	&sis_pm
#else
#define SIS_PM_OPS	NULL
#endif /* CONFIG_PM_SLEEP */

static int sis_alloc_suspend(struct sis7019 *sis)
{
	int i;

	/* We need 16K to store the internal wave engine state during a
	 * suspend, but we don't need it to be contiguous, so play nice
	 * with the memory system. We'll also use this area for a silence
	 * buffer.
	 */
	for (i = 0; i < SIS_SUSPEND_PAGES; i++) {
		sis->suspend_state[i] = kmalloc(4096, GFP_KERNEL);
		if (!sis->suspend_state[i])
			return -ENOMEM;
	}
	memset(sis->suspend_state[0], 0, 4096);

	return 0;
}

static int sis_chip_create(struct snd_card *card,
			   struct pci_dev *pci)
{
	struct sis7019 *sis = card->private_data;
	struct voice *voice;
	static struct snd_device_ops ops = {
		.dev_free = sis_dev_free,
	};
	int rc;
	int i;

	rc = pci_enable_device(pci);
	if (rc)
		goto error_out;

	rc = pci_set_dma_mask(pci, DMA_BIT_MASK(30));
	if (rc < 0) {
		dev_err(&pci->dev, "architecture does not support 30-bit PCI busmaster DMA");
		goto error_out_enabled;
	}

	memset(sis, 0, sizeof(*sis));
	mutex_init(&sis->ac97_mutex);
	spin_lock_init(&sis->voice_lock);
	sis->card = card;
	sis->pci = pci;
	sis->irq = -1;
	sis->ioport = pci_resource_start(pci, 0);

	rc = pci_request_regions(pci, "SiS7019");
	if (rc) {
		dev_err(&pci->dev, "unable request regions\n");
		goto error_out_enabled;
	}

	rc = -EIO;
	sis->ioaddr = ioremap_nocache(pci_resource_start(pci, 1), 0x4000);
	if (!sis->ioaddr) {
		dev_err(&pci->dev, "unable to remap MMIO, aborting\n");
		goto error_out_cleanup;
	}

	rc = sis_alloc_suspend(sis);
	if (rc < 0) {
		dev_err(&pci->dev, "unable to allocate state storage\n");
		goto error_out_cleanup;
	}

	rc = sis_chip_init(sis);
	if (rc)
		goto error_out_cleanup;

	rc = request_irq(pci->irq, sis_interrupt, IRQF_SHARED, KBUILD_MODNAME,
			 sis);
	if (rc) {
		dev_err(&pci->dev, "unable to allocate irq %d\n", sis->irq);
		goto error_out_cleanup;
	}

	sis->irq = pci->irq;
	pci_set_master(pci);

	for (i = 0; i < 64; i++) {
		voice = &sis->voices[i];
		voice->num = i;
		voice->ctrl_base = SIS_PLAY_DMA_ADDR(sis->ioaddr, i);
		voice->wave_base = SIS_WAVE_ADDR(sis->ioaddr, i);
	}

	voice = &sis->capture_voice;
	voice->flags = VOICE_CAPTURE;
	voice->num = SIS_CAPTURE_CHAN_AC97_PCM_IN;
	voice->ctrl_base = SIS_CAPTURE_DMA_ADDR(sis->ioaddr, voice->num);

	rc = snd_device_new(card, SNDRV_DEV_LOWLEVEL, sis, &ops);
	if (rc)
		goto error_out_cleanup;

	snd_card_set_dev(card, &pci->dev);

	return 0;

error_out_cleanup:
	sis_chip_free(sis);

error_out_enabled:
	pci_disable_device(pci);

error_out:
	return rc;
}

static int snd_sis7019_probe(struct pci_dev *pci,
			     const struct pci_device_id *pci_id)
{
	struct snd_card *card;
	struct sis7019 *sis;
	int rc;

	rc = -ENOENT;
	if (!enable)
		goto error_out;

	/* The user can specify which codecs should be present so that we
	 * can wait for them to show up if they are slow to recover from
	 * the AC97 cold reset. We default to a single codec, the primary.
	 *
	 * We assume that SIS_PRIMARY_*_PRESENT matches bits 0-2.
	 */
	codecs &= SIS_PRIMARY_CODEC_PRESENT | SIS_SECONDARY_CODEC_PRESENT |
		  SIS_TERTIARY_CODEC_PRESENT;
	if (!codecs)
		codecs = SIS_PRIMARY_CODEC_PRESENT;

	rc = snd_card_create(index, id, THIS_MODULE, sizeof(*sis), &card);
	if (rc < 0)
		goto error_out;

	strcpy(card->driver, "SiS7019");
	strcpy(card->shortname, "SiS7019");
	rc = sis_chip_create(card, pci);
	if (rc)
		goto card_error_out;

	sis = card->private_data;

	rc = sis_mixer_create(sis);
	if (rc)
		goto card_error_out;

	rc = sis_pcm_create(sis);
	if (rc)
		goto card_error_out;

	snprintf(card->longname, sizeof(card->longname),
			"%s Audio Accelerator with %s at 0x%lx, irq %d",
			card->shortname, snd_ac97_get_short_name(sis->ac97[0]),
			sis->ioport, sis->irq);

	rc = snd_card_register(card);
	if (rc)
		goto card_error_out;

	pci_set_drvdata(pci, card);
	return 0;

card_error_out:
	snd_card_free(card);

error_out:
	return rc;
}

static void snd_sis7019_remove(struct pci_dev *pci)
{
	snd_card_free(pci_get_drvdata(pci));
}

static struct pci_driver sis7019_driver = {
	.name = KBUILD_MODNAME,
	.id_table = snd_sis7019_ids,
	.probe = snd_sis7019_probe,
	.remove = snd_sis7019_remove,
	.driver = {
		.pm = SIS_PM_OPS,
	},
};

module_pci_driver(sis7019_driver);
