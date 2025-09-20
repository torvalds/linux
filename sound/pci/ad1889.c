// SPDX-License-Identifier: GPL-2.0-only
/* Analog Devices 1889 audio driver
 *
 * This is a driver for the AD1889 PCI audio chipset found
 * on the HP PA-RISC [BCJ]-xxx0 workstations.
 *
 * Copyright (C) 2004-2005, Kyle McMartin <kyle@parisc-linux.org>
 * Copyright (C) 2005, Thibaut Varene <varenet@parisc-linux.org>
 *   Based on the OSS AD1889 driver by Randolph Chung <tausq@debian.org>
 *
 * TODO:
 *	Do we need to take care of CCS register?
 *	Maybe we could use finer grained locking (separate locks for pb/cap)?
 * Wishlist:
 *	Control Interface (mixer) support
 *	Better AC97 support (VSR...)?
 *	PM support
 *	MIDI support
 *	Game Port support
 *	SG DMA support (this will need *a lot* of work)
 */

#include <linux/init.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/compiler.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/io.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/initval.h>
#include <sound/ac97_codec.h>

#include "ad1889.h"
#include "ac97/ac97_id.h"

#define	AD1889_DRVVER	"Version: 1.7"

MODULE_AUTHOR("Kyle McMartin <kyle@parisc-linux.org>, Thibaut Varene <t-bone@parisc-linux.org>");
MODULE_DESCRIPTION("Analog Devices AD1889 ALSA sound driver");
MODULE_LICENSE("GPL");

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;
module_param_array(index, int, NULL, 0444);
MODULE_PARM_DESC(index, "Index value for the AD1889 soundcard.");

static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;
module_param_array(id, charp, NULL, 0444);
MODULE_PARM_DESC(id, "ID string for the AD1889 soundcard.");

static bool enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE_PNP;
module_param_array(enable, bool, NULL, 0444);
MODULE_PARM_DESC(enable, "Enable AD1889 soundcard.");

static char *ac97_quirk[SNDRV_CARDS];
module_param_array(ac97_quirk, charp, NULL, 0444);
MODULE_PARM_DESC(ac97_quirk, "AC'97 workaround for strange hardware.");

#define DEVNAME "ad1889"
#define PFX	DEVNAME ": "

/* keep track of some hw registers */
struct ad1889_register_state {
	u16 reg;	/* reg setup */
	u32 addr;	/* dma base address */
	unsigned long size;	/* DMA buffer size */
};

struct snd_ad1889 {
	struct snd_card *card;
	struct pci_dev *pci;

	int irq;
	unsigned long bar;
	void __iomem *iobase;

	struct snd_ac97 *ac97;
	struct snd_ac97_bus *ac97_bus;
	struct snd_pcm *pcm;
	struct snd_info_entry *proc;

	struct snd_pcm_substream *psubs;
	struct snd_pcm_substream *csubs;

	/* playback register state */
	struct ad1889_register_state wave;
	struct ad1889_register_state ramc;

	spinlock_t lock;
};

static inline u16
ad1889_readw(struct snd_ad1889 *chip, unsigned reg)
{
	return readw(chip->iobase + reg);
}

static inline void
ad1889_writew(struct snd_ad1889 *chip, unsigned reg, u16 val)
{
	writew(val, chip->iobase + reg);
}

static inline u32
ad1889_readl(struct snd_ad1889 *chip, unsigned reg)
{
	return readl(chip->iobase + reg);
}

static inline void
ad1889_writel(struct snd_ad1889 *chip, unsigned reg, u32 val)
{
	writel(val, chip->iobase + reg);
}

static inline void
ad1889_unmute(struct snd_ad1889 *chip)
{
	u16 st;
	st = ad1889_readw(chip, AD_DS_WADA) & 
		~(AD_DS_WADA_RWAM | AD_DS_WADA_LWAM);
	ad1889_writew(chip, AD_DS_WADA, st);
	ad1889_readw(chip, AD_DS_WADA);
}

static inline void
ad1889_mute(struct snd_ad1889 *chip)
{
	u16 st;
	st = ad1889_readw(chip, AD_DS_WADA) | AD_DS_WADA_RWAM | AD_DS_WADA_LWAM;
	ad1889_writew(chip, AD_DS_WADA, st);
	ad1889_readw(chip, AD_DS_WADA);
}

static inline void
ad1889_load_adc_buffer_address(struct snd_ad1889 *chip, u32 address)
{
	ad1889_writel(chip, AD_DMA_ADCBA, address);
	ad1889_writel(chip, AD_DMA_ADCCA, address);
}

static inline void
ad1889_load_adc_buffer_count(struct snd_ad1889 *chip, u32 count)
{
	ad1889_writel(chip, AD_DMA_ADCBC, count);
	ad1889_writel(chip, AD_DMA_ADCCC, count);
}

static inline void
ad1889_load_adc_interrupt_count(struct snd_ad1889 *chip, u32 count)
{
	ad1889_writel(chip, AD_DMA_ADCIB, count);
	ad1889_writel(chip, AD_DMA_ADCIC, count);
}

static inline void
ad1889_load_wave_buffer_address(struct snd_ad1889 *chip, u32 address)
{
	ad1889_writel(chip, AD_DMA_WAVBA, address);
	ad1889_writel(chip, AD_DMA_WAVCA, address);
}

static inline void
ad1889_load_wave_buffer_count(struct snd_ad1889 *chip, u32 count)
{
	ad1889_writel(chip, AD_DMA_WAVBC, count);
	ad1889_writel(chip, AD_DMA_WAVCC, count);
}

static inline void
ad1889_load_wave_interrupt_count(struct snd_ad1889 *chip, u32 count)
{
	ad1889_writel(chip, AD_DMA_WAVIB, count);
	ad1889_writel(chip, AD_DMA_WAVIC, count);
}

static void
ad1889_channel_reset(struct snd_ad1889 *chip, unsigned int channel)
{
	u16 reg;
	
	if (channel & AD_CHAN_WAV) {
		/* Disable wave channel */
		reg = ad1889_readw(chip, AD_DS_WSMC) & ~AD_DS_WSMC_WAEN;
		ad1889_writew(chip, AD_DS_WSMC, reg);
		chip->wave.reg = reg;
		
		/* disable IRQs */
		reg = ad1889_readw(chip, AD_DMA_WAV);
		reg &= AD_DMA_IM_DIS;
		reg &= ~AD_DMA_LOOP;
		ad1889_writew(chip, AD_DMA_WAV, reg);

		/* clear IRQ and address counters and pointers */
		ad1889_load_wave_buffer_address(chip, 0x0);
		ad1889_load_wave_buffer_count(chip, 0x0);
		ad1889_load_wave_interrupt_count(chip, 0x0);

		/* flush */
		ad1889_readw(chip, AD_DMA_WAV);
	}
	
	if (channel & AD_CHAN_ADC) {
		/* Disable ADC channel */
		reg = ad1889_readw(chip, AD_DS_RAMC) & ~AD_DS_RAMC_ADEN;
		ad1889_writew(chip, AD_DS_RAMC, reg);
		chip->ramc.reg = reg;

		reg = ad1889_readw(chip, AD_DMA_ADC);
		reg &= AD_DMA_IM_DIS;
		reg &= ~AD_DMA_LOOP;
		ad1889_writew(chip, AD_DMA_ADC, reg);
	
		ad1889_load_adc_buffer_address(chip, 0x0);
		ad1889_load_adc_buffer_count(chip, 0x0);
		ad1889_load_adc_interrupt_count(chip, 0x0);

		/* flush */
		ad1889_readw(chip, AD_DMA_ADC);
	}
}

static u16
snd_ad1889_ac97_read(struct snd_ac97 *ac97, unsigned short reg)
{
	struct snd_ad1889 *chip = ac97->private_data;
	return ad1889_readw(chip, AD_AC97_BASE + reg);
}

static void
snd_ad1889_ac97_write(struct snd_ac97 *ac97, unsigned short reg, unsigned short val)
{
	struct snd_ad1889 *chip = ac97->private_data;
	ad1889_writew(chip, AD_AC97_BASE + reg, val);
}

static int
snd_ad1889_ac97_ready(struct snd_ad1889 *chip)
{
	int retry = 400; /* average needs 352 msec */
	
	while (!(ad1889_readw(chip, AD_AC97_ACIC) & AD_AC97_ACIC_ACRDY) 
			&& --retry)
		usleep_range(1000, 2000);
	if (!retry) {
		dev_err(chip->card->dev, "[%s] Link is not ready.\n",
			__func__);
		return -EIO;
	}
	dev_dbg(chip->card->dev, "[%s] ready after %d ms\n", __func__, 400 - retry);

	return 0;
}

static const struct snd_pcm_hardware snd_ad1889_playback_hw = {
	.info = SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
		SNDRV_PCM_INFO_MMAP_VALID | SNDRV_PCM_INFO_BLOCK_TRANSFER,
	.formats = SNDRV_PCM_FMTBIT_S16_LE,
	.rates = SNDRV_PCM_RATE_CONTINUOUS | SNDRV_PCM_RATE_8000_48000,
	.rate_min = 8000,	/* docs say 7000, but we're lazy */
	.rate_max = 48000,
	.channels_min = 1,
	.channels_max = 2,
	.buffer_bytes_max = BUFFER_BYTES_MAX,
	.period_bytes_min = PERIOD_BYTES_MIN,
	.period_bytes_max = PERIOD_BYTES_MAX,
	.periods_min = PERIODS_MIN,
	.periods_max = PERIODS_MAX,
	/*.fifo_size = 0,*/
};

static const struct snd_pcm_hardware snd_ad1889_capture_hw = {
	.info = SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
		SNDRV_PCM_INFO_MMAP_VALID | SNDRV_PCM_INFO_BLOCK_TRANSFER,
	.formats = SNDRV_PCM_FMTBIT_S16_LE,
	.rates = SNDRV_PCM_RATE_48000,
	.rate_min = 48000,	/* docs say we could to VSR, but we're lazy */
	.rate_max = 48000,
	.channels_min = 1,
	.channels_max = 2,
	.buffer_bytes_max = BUFFER_BYTES_MAX,
	.period_bytes_min = PERIOD_BYTES_MIN,
	.period_bytes_max = PERIOD_BYTES_MAX,
	.periods_min = PERIODS_MIN,
	.periods_max = PERIODS_MAX,
	/*.fifo_size = 0,*/
};

static int
snd_ad1889_playback_open(struct snd_pcm_substream *ss)
{
	struct snd_ad1889 *chip = snd_pcm_substream_chip(ss);
	struct snd_pcm_runtime *rt = ss->runtime;

	chip->psubs = ss;
	rt->hw = snd_ad1889_playback_hw;

	return 0;
}

static int
snd_ad1889_capture_open(struct snd_pcm_substream *ss)
{
	struct snd_ad1889 *chip = snd_pcm_substream_chip(ss);
	struct snd_pcm_runtime *rt = ss->runtime;

	chip->csubs = ss;
	rt->hw = snd_ad1889_capture_hw;

	return 0;
}

static int
snd_ad1889_playback_close(struct snd_pcm_substream *ss)
{
	struct snd_ad1889 *chip = snd_pcm_substream_chip(ss);
	chip->psubs = NULL;
	return 0;
}

static int
snd_ad1889_capture_close(struct snd_pcm_substream *ss)
{
	struct snd_ad1889 *chip = snd_pcm_substream_chip(ss);
	chip->csubs = NULL;
	return 0;
}

static int
snd_ad1889_playback_prepare(struct snd_pcm_substream *ss)
{
	struct snd_ad1889 *chip = snd_pcm_substream_chip(ss);
	struct snd_pcm_runtime *rt = ss->runtime;
	unsigned int size = snd_pcm_lib_buffer_bytes(ss);
	unsigned int count = snd_pcm_lib_period_bytes(ss);
	u16 reg;

	ad1889_channel_reset(chip, AD_CHAN_WAV);

	reg = ad1889_readw(chip, AD_DS_WSMC);
	
	/* Mask out 16-bit / Stereo */
	reg &= ~(AD_DS_WSMC_WA16 | AD_DS_WSMC_WAST);

	if (snd_pcm_format_width(rt->format) == 16)
		reg |= AD_DS_WSMC_WA16;

	if (rt->channels > 1)
		reg |= AD_DS_WSMC_WAST;

	/* let's make sure we don't clobber ourselves */
	guard(spinlock_irq)(&chip->lock);
	
	chip->wave.size = size;
	chip->wave.reg = reg;
	chip->wave.addr = rt->dma_addr;

	ad1889_writew(chip, AD_DS_WSMC, chip->wave.reg);
	
	/* Set sample rates on the codec */
	ad1889_writew(chip, AD_DS_WAS, rt->rate);

	/* Set up DMA */
	ad1889_load_wave_buffer_address(chip, chip->wave.addr);
	ad1889_load_wave_buffer_count(chip, size);
	ad1889_load_wave_interrupt_count(chip, count);

	/* writes flush */
	ad1889_readw(chip, AD_DS_WSMC);
	
	dev_dbg(chip->card->dev,
		"prepare playback: addr = 0x%x, count = %u, size = %u, reg = 0x%x, rate = %u\n",
		chip->wave.addr, count, size, reg, rt->rate);
	return 0;
}

static int
snd_ad1889_capture_prepare(struct snd_pcm_substream *ss)
{
	struct snd_ad1889 *chip = snd_pcm_substream_chip(ss);
	struct snd_pcm_runtime *rt = ss->runtime;
	unsigned int size = snd_pcm_lib_buffer_bytes(ss);
	unsigned int count = snd_pcm_lib_period_bytes(ss);
	u16 reg;

	ad1889_channel_reset(chip, AD_CHAN_ADC);
	
	reg = ad1889_readw(chip, AD_DS_RAMC);

	/* Mask out 16-bit / Stereo */
	reg &= ~(AD_DS_RAMC_AD16 | AD_DS_RAMC_ADST);

	if (snd_pcm_format_width(rt->format) == 16)
		reg |= AD_DS_RAMC_AD16;

	if (rt->channels > 1)
		reg |= AD_DS_RAMC_ADST;

	/* let's make sure we don't clobber ourselves */
	guard(spinlock_irq)(&chip->lock);
	
	chip->ramc.size = size;
	chip->ramc.reg = reg;
	chip->ramc.addr = rt->dma_addr;

	ad1889_writew(chip, AD_DS_RAMC, chip->ramc.reg);

	/* Set up DMA */
	ad1889_load_adc_buffer_address(chip, chip->ramc.addr);
	ad1889_load_adc_buffer_count(chip, size);
	ad1889_load_adc_interrupt_count(chip, count);

	/* writes flush */
	ad1889_readw(chip, AD_DS_RAMC);
	
	dev_dbg(chip->card->dev,
		"prepare capture: addr = 0x%x, count = %u, size = %u, reg = 0x%x, rate = %u\n",
		chip->ramc.addr, count, size, reg, rt->rate);
	return 0;
}

/* this is called in atomic context with IRQ disabled.
   Must be as fast as possible and not sleep.
   DMA should be *triggered* by this call.
   The WSMC "WAEN" bit triggers DMA Wave On/Off */
static int
snd_ad1889_playback_trigger(struct snd_pcm_substream *ss, int cmd)
{
	u16 wsmc;
	struct snd_ad1889 *chip = snd_pcm_substream_chip(ss);
	
	wsmc = ad1889_readw(chip, AD_DS_WSMC);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		/* enable DMA loop & interrupts */
		ad1889_writew(chip, AD_DMA_WAV, AD_DMA_LOOP | AD_DMA_IM_CNT);
		wsmc |= AD_DS_WSMC_WAEN;
		/* 1 to clear CHSS bit */
		ad1889_writel(chip, AD_DMA_CHSS, AD_DMA_CHSS_WAVS);
		ad1889_unmute(chip);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		ad1889_mute(chip);
		wsmc &= ~AD_DS_WSMC_WAEN;
		break;
	default:
		snd_BUG();
		return -EINVAL;
	}
	
	chip->wave.reg = wsmc;
	ad1889_writew(chip, AD_DS_WSMC, wsmc);	
	ad1889_readw(chip, AD_DS_WSMC);	/* flush */

	/* reset the chip when STOP - will disable IRQs */
	if (cmd == SNDRV_PCM_TRIGGER_STOP)
		ad1889_channel_reset(chip, AD_CHAN_WAV);

	return 0;
}

/* this is called in atomic context with IRQ disabled.
   Must be as fast as possible and not sleep.
   DMA should be *triggered* by this call.
   The RAMC "ADEN" bit triggers DMA ADC On/Off */
static int
snd_ad1889_capture_trigger(struct snd_pcm_substream *ss, int cmd)
{
	u16 ramc;
	struct snd_ad1889 *chip = snd_pcm_substream_chip(ss);

	ramc = ad1889_readw(chip, AD_DS_RAMC);
	
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		/* enable DMA loop & interrupts */
		ad1889_writew(chip, AD_DMA_ADC, AD_DMA_LOOP | AD_DMA_IM_CNT);
		ramc |= AD_DS_RAMC_ADEN;
		/* 1 to clear CHSS bit */
		ad1889_writel(chip, AD_DMA_CHSS, AD_DMA_CHSS_ADCS);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		ramc &= ~AD_DS_RAMC_ADEN;
		break;
	default:
		return -EINVAL;
	}
	
	chip->ramc.reg = ramc;
	ad1889_writew(chip, AD_DS_RAMC, ramc);	
	ad1889_readw(chip, AD_DS_RAMC);	/* flush */
	
	/* reset the chip when STOP - will disable IRQs */
	if (cmd == SNDRV_PCM_TRIGGER_STOP)
		ad1889_channel_reset(chip, AD_CHAN_ADC);
		
	return 0;
}

/* Called in atomic context with IRQ disabled */
static snd_pcm_uframes_t
snd_ad1889_playback_pointer(struct snd_pcm_substream *ss)
{
	size_t ptr = 0;
	struct snd_ad1889 *chip = snd_pcm_substream_chip(ss);

	if (unlikely(!(chip->wave.reg & AD_DS_WSMC_WAEN)))
		return 0;

	ptr = ad1889_readl(chip, AD_DMA_WAVCA);
	ptr -= chip->wave.addr;
	
	if (snd_BUG_ON(ptr >= chip->wave.size))
		return 0;
	
	return bytes_to_frames(ss->runtime, ptr);
}

/* Called in atomic context with IRQ disabled */
static snd_pcm_uframes_t
snd_ad1889_capture_pointer(struct snd_pcm_substream *ss)
{
	size_t ptr = 0;
	struct snd_ad1889 *chip = snd_pcm_substream_chip(ss);

	if (unlikely(!(chip->ramc.reg & AD_DS_RAMC_ADEN)))
		return 0;

	ptr = ad1889_readl(chip, AD_DMA_ADCCA);
	ptr -= chip->ramc.addr;

	if (snd_BUG_ON(ptr >= chip->ramc.size))
		return 0;
	
	return bytes_to_frames(ss->runtime, ptr);
}

static const struct snd_pcm_ops snd_ad1889_playback_ops = {
	.open = snd_ad1889_playback_open,
	.close = snd_ad1889_playback_close,
	.prepare = snd_ad1889_playback_prepare,
	.trigger = snd_ad1889_playback_trigger,
	.pointer = snd_ad1889_playback_pointer, 
};

static const struct snd_pcm_ops snd_ad1889_capture_ops = {
	.open = snd_ad1889_capture_open,
	.close = snd_ad1889_capture_close,
	.prepare = snd_ad1889_capture_prepare,
	.trigger = snd_ad1889_capture_trigger,
	.pointer = snd_ad1889_capture_pointer, 
};

static irqreturn_t
snd_ad1889_interrupt(int irq, void *dev_id)
{
	unsigned long st;
	struct snd_ad1889 *chip = dev_id;

	st = ad1889_readl(chip, AD_DMA_DISR);

	/* clear ISR */
	ad1889_writel(chip, AD_DMA_DISR, st);

	st &= AD_INTR_MASK;

	if (unlikely(!st))
		return IRQ_NONE;

	if (st & (AD_DMA_DISR_PMAI|AD_DMA_DISR_PTAI))
		dev_dbg(chip->card->dev,
			"Unexpected master or target abort interrupt!\n");

	if ((st & AD_DMA_DISR_WAVI) && chip->psubs)
		snd_pcm_period_elapsed(chip->psubs);
	if ((st & AD_DMA_DISR_ADCI) && chip->csubs)
		snd_pcm_period_elapsed(chip->csubs);

	return IRQ_HANDLED;
}

static int
snd_ad1889_pcm_init(struct snd_ad1889 *chip, int device)
{
	int err;
	struct snd_pcm *pcm;

	err = snd_pcm_new(chip->card, chip->card->driver, device, 1, 1, &pcm);
	if (err < 0)
		return err;

	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, 
			&snd_ad1889_playback_ops);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE,
			&snd_ad1889_capture_ops);

	pcm->private_data = chip;
	pcm->info_flags = 0;
	strscpy(pcm->name, chip->card->shortname);
	
	chip->pcm = pcm;
	chip->psubs = NULL;
	chip->csubs = NULL;

	snd_pcm_set_managed_buffer_all(pcm, SNDRV_DMA_TYPE_DEV, &chip->pci->dev,
				       BUFFER_BYTES_MAX / 2, BUFFER_BYTES_MAX);

	return 0;
}

static void
snd_ad1889_proc_read(struct snd_info_entry *entry, struct snd_info_buffer *buffer)
{
	struct snd_ad1889 *chip = entry->private_data;
	u16 reg;
	int tmp;

	reg = ad1889_readw(chip, AD_DS_WSMC);
	snd_iprintf(buffer, "Wave output: %s\n",
			str_enabled_disabled(reg & AD_DS_WSMC_WAEN));
	snd_iprintf(buffer, "Wave Channels: %s\n",
			(reg & AD_DS_WSMC_WAST) ? "stereo" : "mono");
	snd_iprintf(buffer, "Wave Quality: %d-bit linear\n",
			(reg & AD_DS_WSMC_WA16) ? 16 : 8);
	
	/* WARQ is at offset 12 */
	tmp = (reg & AD_DS_WSMC_WARQ) ?
		((((reg & AD_DS_WSMC_WARQ) >> 12) & 0x01) ? 12 : 18) : 4;
	tmp /= (reg & AD_DS_WSMC_WAST) ? 2 : 1;
	
	snd_iprintf(buffer, "Wave FIFO: %d %s words\n\n", tmp,
			(reg & AD_DS_WSMC_WAST) ? "stereo" : "mono");
				
	
	snd_iprintf(buffer, "Synthesis output: %s\n",
			str_enabled_disabled(reg & AD_DS_WSMC_SYEN));
	
	/* SYRQ is at offset 4 */
	tmp = (reg & AD_DS_WSMC_SYRQ) ?
		((((reg & AD_DS_WSMC_SYRQ) >> 4) & 0x01) ? 12 : 18) : 4;
	tmp /= (reg & AD_DS_WSMC_WAST) ? 2 : 1;
	
	snd_iprintf(buffer, "Synthesis FIFO: %d %s words\n\n", tmp,
			(reg & AD_DS_WSMC_WAST) ? "stereo" : "mono");

	reg = ad1889_readw(chip, AD_DS_RAMC);
	snd_iprintf(buffer, "ADC input: %s\n",
			str_enabled_disabled(reg & AD_DS_RAMC_ADEN));
	snd_iprintf(buffer, "ADC Channels: %s\n",
			(reg & AD_DS_RAMC_ADST) ? "stereo" : "mono");
	snd_iprintf(buffer, "ADC Quality: %d-bit linear\n",
			(reg & AD_DS_RAMC_AD16) ? 16 : 8);
	
	/* ACRQ is at offset 4 */
	tmp = (reg & AD_DS_RAMC_ACRQ) ?
		((((reg & AD_DS_RAMC_ACRQ) >> 4) & 0x01) ? 12 : 18) : 4;
	tmp /= (reg & AD_DS_RAMC_ADST) ? 2 : 1;
	
	snd_iprintf(buffer, "ADC FIFO: %d %s words\n\n", tmp,
			(reg & AD_DS_RAMC_ADST) ? "stereo" : "mono");
	
	snd_iprintf(buffer, "Resampler input: %s\n",
			str_enabled_disabled(reg & AD_DS_RAMC_REEN));
			
	/* RERQ is at offset 12 */
	tmp = (reg & AD_DS_RAMC_RERQ) ?
		((((reg & AD_DS_RAMC_RERQ) >> 12) & 0x01) ? 12 : 18) : 4;
	tmp /= (reg & AD_DS_RAMC_ADST) ? 2 : 1;
	
	snd_iprintf(buffer, "Resampler FIFO: %d %s words\n\n", tmp,
			(reg & AD_DS_WSMC_WAST) ? "stereo" : "mono");
				
	
	/* doc says LSB represents -1.5dB, but the max value (-94.5dB)
	suggests that LSB is -3dB, which is more coherent with the logarithmic
	nature of the dB scale */
	reg = ad1889_readw(chip, AD_DS_WADA);
	snd_iprintf(buffer, "Left: %s, -%d dB\n",
			(reg & AD_DS_WADA_LWAM) ? "mute" : "unmute",
			((reg & AD_DS_WADA_LWAA) >> 8) * 3);
	reg = ad1889_readw(chip, AD_DS_WADA);
	snd_iprintf(buffer, "Right: %s, -%d dB\n",
			(reg & AD_DS_WADA_RWAM) ? "mute" : "unmute",
			(reg & AD_DS_WADA_RWAA) * 3);
	
	reg = ad1889_readw(chip, AD_DS_WAS);
	snd_iprintf(buffer, "Wave samplerate: %u Hz\n", reg);
	reg = ad1889_readw(chip, AD_DS_RES);
	snd_iprintf(buffer, "Resampler samplerate: %u Hz\n", reg);
}

static void
snd_ad1889_proc_init(struct snd_ad1889 *chip)
{
	snd_card_ro_proc_new(chip->card, chip->card->driver,
			     chip, snd_ad1889_proc_read);
}

static const struct ac97_quirk ac97_quirks[] = {
	{
		.subvendor = 0x11d4,	/* AD */
		.subdevice = 0x1889,	/* AD1889 */
		.codec_id = AC97_ID_AD1819,
		.name = "AD1889",
		.type = AC97_TUNE_HP_ONLY
	},
	{ } /* terminator */
};

static void
snd_ad1889_ac97_xinit(struct snd_ad1889 *chip)
{
	u16 reg;

	reg = ad1889_readw(chip, AD_AC97_ACIC);
	reg |= AD_AC97_ACIC_ACRD;		/* Reset Disable */
	ad1889_writew(chip, AD_AC97_ACIC, reg);
	ad1889_readw(chip, AD_AC97_ACIC);	/* flush posted write */
	udelay(10);
	/* Interface Enable */
	reg |= AD_AC97_ACIC_ACIE;
	ad1889_writew(chip, AD_AC97_ACIC, reg);
	
	snd_ad1889_ac97_ready(chip);

	/* Audio Stream Output | Variable Sample Rate Mode */
	reg = ad1889_readw(chip, AD_AC97_ACIC);
	reg |= AD_AC97_ACIC_ASOE | AD_AC97_ACIC_VSRM;
	ad1889_writew(chip, AD_AC97_ACIC, reg);
	ad1889_readw(chip, AD_AC97_ACIC); /* flush posted write */

}

static int
snd_ad1889_ac97_init(struct snd_ad1889 *chip, const char *quirk_override)
{
	int err;
	struct snd_ac97_template ac97;
	static const struct snd_ac97_bus_ops ops = {
		.write = snd_ad1889_ac97_write,
		.read = snd_ad1889_ac97_read,
	};

	/* doing that here, it works. */
	snd_ad1889_ac97_xinit(chip);

	err = snd_ac97_bus(chip->card, 0, &ops, chip, &chip->ac97_bus);
	if (err < 0)
		return err;
	
	memset(&ac97, 0, sizeof(ac97));
	ac97.private_data = chip;
	ac97.pci = chip->pci;

	err = snd_ac97_mixer(chip->ac97_bus, &ac97, &chip->ac97);
	if (err < 0)
		return err;
		
	snd_ac97_tune_hardware(chip->ac97, ac97_quirks, quirk_override);
	
	return 0;
}

static void
snd_ad1889_free(struct snd_card *card)
{
	struct snd_ad1889 *chip = card->private_data;

	guard(spinlock_irq)(&chip->lock);

	ad1889_mute(chip);

	/* Turn off interrupt on count and zero DMA registers */
	ad1889_channel_reset(chip, AD_CHAN_WAV | AD_CHAN_ADC);

	/* clear DISR. If we don't, we'd better jump off the Eiffel Tower */
	ad1889_writel(chip, AD_DMA_DISR, AD_DMA_DISR_PTAI | AD_DMA_DISR_PMAI);
	ad1889_readl(chip, AD_DMA_DISR);	/* flush, dammit! */
}

static int
snd_ad1889_create(struct snd_card *card, struct pci_dev *pci)
{
	struct snd_ad1889 *chip = card->private_data;
	int err;

	err = pcim_enable_device(pci);
	if (err < 0)
		return err;

	/* check PCI availability (32bit DMA) */
	if (dma_set_mask_and_coherent(&pci->dev, DMA_BIT_MASK(32))) {
		dev_err(card->dev, "error setting 32-bit DMA mask.\n");
		return -ENXIO;
	}

	chip->card = card;
	chip->pci = pci;
	chip->irq = -1;

	/* (1) PCI resource allocation */
	chip->iobase = pcim_iomap_region(pci, 0, card->driver);
	if (IS_ERR(chip->iobase))
		return PTR_ERR(chip->iobase);

	chip->bar = pci_resource_start(pci, 0);
	
	pci_set_master(pci);

	spin_lock_init(&chip->lock);	/* only now can we call ad1889_free */

	if (devm_request_irq(&pci->dev, pci->irq, snd_ad1889_interrupt,
			     IRQF_SHARED, KBUILD_MODNAME, chip)) {
		dev_err(card->dev, "cannot obtain IRQ %d\n", pci->irq);
		return -EBUSY;
	}

	chip->irq = pci->irq;
	card->sync_irq = chip->irq;
	card->private_free = snd_ad1889_free;

	/* (2) initialization of the chip hardware */
	ad1889_writew(chip, AD_DS_CCS, AD_DS_CCS_CLKEN); /* turn on clock */
	ad1889_readw(chip, AD_DS_CCS);	/* flush posted write */

	usleep_range(10000, 11000);

	/* enable Master and Target abort interrupts */
	ad1889_writel(chip, AD_DMA_DISR, AD_DMA_DISR_PMAE | AD_DMA_DISR_PTAE);

	return 0;
}

static int
__snd_ad1889_probe(struct pci_dev *pci,
		   const struct pci_device_id *pci_id)
{
	int err;
	static int devno;
	struct snd_card *card;
	struct snd_ad1889 *chip;

	/* (1) */
	if (devno >= SNDRV_CARDS)
		return -ENODEV;
	if (!enable[devno]) {
		devno++;
		return -ENOENT;
	}

	/* (2) */
	err = snd_devm_card_new(&pci->dev, index[devno], id[devno], THIS_MODULE,
				sizeof(*chip), &card);
	if (err < 0)
		return err;
	chip = card->private_data;

	strscpy(card->driver, "AD1889");
	strscpy(card->shortname, "Analog Devices AD1889");

	/* (3) */
	err = snd_ad1889_create(card, pci);
	if (err < 0)
		return err;

	/* (4) */
	sprintf(card->longname, "%s at 0x%lx irq %i",
		card->shortname, chip->bar, chip->irq);

	/* (5) */
	/* register AC97 mixer */
	err = snd_ad1889_ac97_init(chip, ac97_quirk[devno]);
	if (err < 0)
		return err;
	
	err = snd_ad1889_pcm_init(chip, 0);
	if (err < 0)
		return err;

	/* register proc interface */
	snd_ad1889_proc_init(chip);

	/* (6) */
	err = snd_card_register(card);
	if (err < 0)
		return err;

	/* (7) */
	pci_set_drvdata(pci, card);

	devno++;
	return 0;
}

static int snd_ad1889_probe(struct pci_dev *pci,
			    const struct pci_device_id *pci_id)
{
	return snd_card_free_on_error(&pci->dev, __snd_ad1889_probe(pci, pci_id));
}

static const struct pci_device_id snd_ad1889_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_ANALOG_DEVICES, PCI_DEVICE_ID_AD1889JS) },
	{ 0, },
};
MODULE_DEVICE_TABLE(pci, snd_ad1889_ids);

static struct pci_driver ad1889_pci_driver = {
	.name = KBUILD_MODNAME,
	.id_table = snd_ad1889_ids,
	.probe = snd_ad1889_probe,
};

module_pci_driver(ad1889_pci_driver);
