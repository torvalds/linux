/* 
 * Driver for NeoMagic 256AV and 256ZX chipsets.
 * Copyright (c) 2000 by Takashi Iwai <tiwai@suse.de>
 *
 * Based on nm256_audio.c OSS driver in linux kernel.
 * The original author of OSS nm256 driver wishes to remain anonymous,
 * so I just put my acknoledgment to him/her here.
 * The original author's web page is found at
 *	http://www.uglx.org/sony.html
 *
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
  
#include <asm/io.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/mutex.h>

#include <sound/core.h>
#include <sound/info.h>
#include <sound/control.h>
#include <sound/pcm.h>
#include <sound/ac97_codec.h>
#include <sound/initval.h>

#define CARD_NAME "NeoMagic 256AV/ZX"
#define DRIVER_NAME "NM256"

MODULE_AUTHOR("Takashi Iwai <tiwai@suse.de>");
MODULE_DESCRIPTION("NeoMagic NM256AV/ZX");
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("{{NeoMagic,NM256AV},"
		"{NeoMagic,NM256ZX}}");

/*
 * some compile conditions.
 */

static int index = SNDRV_DEFAULT_IDX1;	/* Index */
static char *id = SNDRV_DEFAULT_STR1;	/* ID for this card */
static int playback_bufsize = 16;
static int capture_bufsize = 16;
static bool force_ac97;			/* disabled as default */
static int buffer_top;			/* not specified */
static bool use_cache;			/* disabled */
static bool vaio_hack;			/* disabled */
static bool reset_workaround;
static bool reset_workaround_2;

module_param(index, int, 0444);
MODULE_PARM_DESC(index, "Index value for " CARD_NAME " soundcard.");
module_param(id, charp, 0444);
MODULE_PARM_DESC(id, "ID string for " CARD_NAME " soundcard.");
module_param(playback_bufsize, int, 0444);
MODULE_PARM_DESC(playback_bufsize, "DAC frame size in kB for " CARD_NAME " soundcard.");
module_param(capture_bufsize, int, 0444);
MODULE_PARM_DESC(capture_bufsize, "ADC frame size in kB for " CARD_NAME " soundcard.");
module_param(force_ac97, bool, 0444);
MODULE_PARM_DESC(force_ac97, "Force to use AC97 codec for " CARD_NAME " soundcard.");
module_param(buffer_top, int, 0444);
MODULE_PARM_DESC(buffer_top, "Set the top address of audio buffer for " CARD_NAME " soundcard.");
module_param(use_cache, bool, 0444);
MODULE_PARM_DESC(use_cache, "Enable the cache for coefficient table access.");
module_param(vaio_hack, bool, 0444);
MODULE_PARM_DESC(vaio_hack, "Enable workaround for Sony VAIO notebooks.");
module_param(reset_workaround, bool, 0444);
MODULE_PARM_DESC(reset_workaround, "Enable AC97 RESET workaround for some laptops.");
module_param(reset_workaround_2, bool, 0444);
MODULE_PARM_DESC(reset_workaround_2, "Enable extended AC97 RESET workaround for some other laptops.");

/* just for backward compatibility */
static bool enable;
module_param(enable, bool, 0444);



/*
 * hw definitions
 */

/* The BIOS signature. */
#define NM_SIGNATURE 0x4e4d0000
/* Signature mask. */
#define NM_SIG_MASK 0xffff0000

/* Size of the second memory area. */
#define NM_PORT2_SIZE 4096

/* The base offset of the mixer in the second memory area. */
#define NM_MIXER_OFFSET 0x600

/* The maximum size of a coefficient entry. */
#define NM_MAX_PLAYBACK_COEF_SIZE	0x5000
#define NM_MAX_RECORD_COEF_SIZE		0x1260

/* The interrupt register. */
#define NM_INT_REG 0xa04
/* And its bits. */
#define NM_PLAYBACK_INT 0x40
#define NM_RECORD_INT 0x100
#define NM_MISC_INT_1 0x4000
#define NM_MISC_INT_2 0x1
#define NM_ACK_INT(chip, X) snd_nm256_writew(chip, NM_INT_REG, (X) << 1)

/* The AV's "mixer ready" status bit and location. */
#define NM_MIXER_STATUS_OFFSET 0xa04
#define NM_MIXER_READY_MASK 0x0800
#define NM_MIXER_PRESENCE 0xa06
#define NM_PRESENCE_MASK 0x0050
#define NM_PRESENCE_VALUE 0x0040

/*
 * For the ZX.  It uses the same interrupt register, but it holds 32
 * bits instead of 16.
 */
#define NM2_PLAYBACK_INT 0x10000
#define NM2_RECORD_INT 0x80000
#define NM2_MISC_INT_1 0x8
#define NM2_MISC_INT_2 0x2
#define NM2_ACK_INT(chip, X) snd_nm256_writel(chip, NM_INT_REG, (X))

/* The ZX's "mixer ready" status bit and location. */
#define NM2_MIXER_STATUS_OFFSET 0xa06
#define NM2_MIXER_READY_MASK 0x0800

/* The playback registers start from here. */
#define NM_PLAYBACK_REG_OFFSET 0x0
/* The record registers start from here. */
#define NM_RECORD_REG_OFFSET 0x200

/* The rate register is located 2 bytes from the start of the register area. */
#define NM_RATE_REG_OFFSET 2

/* Mono/stereo flag, number of bits on playback, and rate mask. */
#define NM_RATE_STEREO 1
#define NM_RATE_BITS_16 2
#define NM_RATE_MASK 0xf0

/* Playback enable register. */
#define NM_PLAYBACK_ENABLE_REG (NM_PLAYBACK_REG_OFFSET + 0x1)
#define NM_PLAYBACK_ENABLE_FLAG 1
#define NM_PLAYBACK_ONESHOT 2
#define NM_PLAYBACK_FREERUN 4

/* Mutes the audio output. */
#define NM_AUDIO_MUTE_REG (NM_PLAYBACK_REG_OFFSET + 0x18)
#define NM_AUDIO_MUTE_LEFT 0x8000
#define NM_AUDIO_MUTE_RIGHT 0x0080

/* Recording enable register. */
#define NM_RECORD_ENABLE_REG (NM_RECORD_REG_OFFSET + 0)
#define NM_RECORD_ENABLE_FLAG 1
#define NM_RECORD_FREERUN 2

/* coefficient buffer pointer */
#define NM_COEFF_START_OFFSET	0x1c
#define NM_COEFF_END_OFFSET	0x20

/* DMA buffer offsets */
#define NM_RBUFFER_START (NM_RECORD_REG_OFFSET + 0x4)
#define NM_RBUFFER_END   (NM_RECORD_REG_OFFSET + 0x10)
#define NM_RBUFFER_WMARK (NM_RECORD_REG_OFFSET + 0xc)
#define NM_RBUFFER_CURRP (NM_RECORD_REG_OFFSET + 0x8)

#define NM_PBUFFER_START (NM_PLAYBACK_REG_OFFSET + 0x4)
#define NM_PBUFFER_END   (NM_PLAYBACK_REG_OFFSET + 0x14)
#define NM_PBUFFER_WMARK (NM_PLAYBACK_REG_OFFSET + 0xc)
#define NM_PBUFFER_CURRP (NM_PLAYBACK_REG_OFFSET + 0x8)

struct nm256_stream {

	struct nm256 *chip;
	struct snd_pcm_substream *substream;
	int running;
	int suspended;
	
	u32 buf;	/* offset from chip->buffer */
	int bufsize;	/* buffer size in bytes */
	void __iomem *bufptr;		/* mapped pointer */
	unsigned long bufptr_addr;	/* physical address of the mapped pointer */

	int dma_size;		/* buffer size of the substream in bytes */
	int period_size;	/* period size in bytes */
	int periods;		/* # of periods */
	int shift;		/* bit shifts */
	int cur_period;		/* current period # */

};

struct nm256 {
	
	struct snd_card *card;

	void __iomem *cport;		/* control port */
	struct resource *res_cport;	/* its resource */
	unsigned long cport_addr;	/* physical address */

	void __iomem *buffer;		/* buffer */
	struct resource *res_buffer;	/* its resource */
	unsigned long buffer_addr;	/* buffer phyiscal address */

	u32 buffer_start;		/* start offset from pci resource 0 */
	u32 buffer_end;			/* end offset */
	u32 buffer_size;		/* total buffer size */

	u32 all_coeff_buf;		/* coefficient buffer */
	u32 coeff_buf[2];		/* coefficient buffer for each stream */

	unsigned int coeffs_current: 1;	/* coeff. table is loaded? */
	unsigned int use_cache: 1;	/* use one big coef. table */
	unsigned int reset_workaround: 1; /* Workaround for some laptops to avoid freeze */
	unsigned int reset_workaround_2: 1; /* Extended workaround for some other laptops to avoid freeze */
	unsigned int in_resume: 1;

	int mixer_base;			/* register offset of ac97 mixer */
	int mixer_status_offset;	/* offset of mixer status reg. */
	int mixer_status_mask;		/* bit mask to test the mixer status */

	int irq;
	int irq_acks;
	irq_handler_t interrupt;
	int badintrcount;		/* counter to check bogus interrupts */
	struct mutex irq_mutex;

	struct nm256_stream streams[2];

	struct snd_ac97 *ac97;
	unsigned short *ac97_regs; /* register caches, only for valid regs */

	struct snd_pcm *pcm;

	struct pci_dev *pci;

	spinlock_t reg_lock;

};


/*
 * include coefficient table
 */
#include "nm256_coef.c"


/*
 * PCI ids
 */
static DEFINE_PCI_DEVICE_TABLE(snd_nm256_ids) = {
	{PCI_VDEVICE(NEOMAGIC, PCI_DEVICE_ID_NEOMAGIC_NM256AV_AUDIO), 0},
	{PCI_VDEVICE(NEOMAGIC, PCI_DEVICE_ID_NEOMAGIC_NM256ZX_AUDIO), 0},
	{PCI_VDEVICE(NEOMAGIC, PCI_DEVICE_ID_NEOMAGIC_NM256XL_PLUS_AUDIO), 0},
	{0,},
};

MODULE_DEVICE_TABLE(pci, snd_nm256_ids);


/*
 * lowlvel stuffs
 */

static inline u8
snd_nm256_readb(struct nm256 *chip, int offset)
{
	return readb(chip->cport + offset);
}

static inline u16
snd_nm256_readw(struct nm256 *chip, int offset)
{
	return readw(chip->cport + offset);
}

static inline u32
snd_nm256_readl(struct nm256 *chip, int offset)
{
	return readl(chip->cport + offset);
}

static inline void
snd_nm256_writeb(struct nm256 *chip, int offset, u8 val)
{
	writeb(val, chip->cport + offset);
}

static inline void
snd_nm256_writew(struct nm256 *chip, int offset, u16 val)
{
	writew(val, chip->cport + offset);
}

static inline void
snd_nm256_writel(struct nm256 *chip, int offset, u32 val)
{
	writel(val, chip->cport + offset);
}

static inline void
snd_nm256_write_buffer(struct nm256 *chip, void *src, int offset, int size)
{
	offset -= chip->buffer_start;
#ifdef CONFIG_SND_DEBUG
	if (offset < 0 || offset >= chip->buffer_size) {
		snd_printk(KERN_ERR "write_buffer invalid offset = %d size = %d\n",
			   offset, size);
		return;
	}
#endif
	memcpy_toio(chip->buffer + offset, src, size);
}

/*
 * coefficient handlers -- what a magic!
 */

static u16
snd_nm256_get_start_offset(int which)
{
	u16 offset = 0;
	while (which-- > 0)
		offset += coefficient_sizes[which];
	return offset;
}

static void
snd_nm256_load_one_coefficient(struct nm256 *chip, int stream, u32 port, int which)
{
	u32 coeff_buf = chip->coeff_buf[stream];
	u16 offset = snd_nm256_get_start_offset(which);
	u16 size = coefficient_sizes[which];

	snd_nm256_write_buffer(chip, coefficients + offset, coeff_buf, size);
	snd_nm256_writel(chip, port, coeff_buf);
	/* ???  Record seems to behave differently than playback.  */
	if (stream == SNDRV_PCM_STREAM_PLAYBACK)
		size--;
	snd_nm256_writel(chip, port + 4, coeff_buf + size);
}

static void
snd_nm256_load_coefficient(struct nm256 *chip, int stream, int number)
{
	/* The enable register for the specified engine.  */
	u32 poffset = (stream == SNDRV_PCM_STREAM_CAPTURE ?
		       NM_RECORD_ENABLE_REG : NM_PLAYBACK_ENABLE_REG);
	u32 addr = NM_COEFF_START_OFFSET;

	addr += (stream == SNDRV_PCM_STREAM_CAPTURE ?
		 NM_RECORD_REG_OFFSET : NM_PLAYBACK_REG_OFFSET);

	if (snd_nm256_readb(chip, poffset) & 1) {
		snd_printd("NM256: Engine was enabled while loading coefficients!\n");
		return;
	}

	/* The recording engine uses coefficient values 8-15.  */
	number &= 7;
	if (stream == SNDRV_PCM_STREAM_CAPTURE)
		number += 8;

	if (! chip->use_cache) {
		snd_nm256_load_one_coefficient(chip, stream, addr, number);
		return;
	}
	if (! chip->coeffs_current) {
		snd_nm256_write_buffer(chip, coefficients, chip->all_coeff_buf,
				       NM_TOTAL_COEFF_COUNT * 4);
		chip->coeffs_current = 1;
	} else {
		u32 base = chip->all_coeff_buf;
		u32 offset = snd_nm256_get_start_offset(number);
		u32 end_offset = offset + coefficient_sizes[number];
		snd_nm256_writel(chip, addr, base + offset);
		if (stream == SNDRV_PCM_STREAM_PLAYBACK)
			end_offset--;
		snd_nm256_writel(chip, addr + 4, base + end_offset);
	}
}


/* The actual rates supported by the card. */
static unsigned int samplerates[8] = {
	8000, 11025, 16000, 22050, 24000, 32000, 44100, 48000,
};
static struct snd_pcm_hw_constraint_list constraints_rates = {
	.count = ARRAY_SIZE(samplerates), 
	.list = samplerates,
	.mask = 0,
};

/*
 * return the index of the target rate
 */
static int
snd_nm256_fixed_rate(unsigned int rate)
{
	unsigned int i;
	for (i = 0; i < ARRAY_SIZE(samplerates); i++) {
		if (rate == samplerates[i])
			return i;
	}
	snd_BUG();
	return 0;
}

/*
 * set sample rate and format
 */
static void
snd_nm256_set_format(struct nm256 *chip, struct nm256_stream *s,
		     struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	int rate_index = snd_nm256_fixed_rate(runtime->rate);
	unsigned char ratebits = (rate_index << 4) & NM_RATE_MASK;

	s->shift = 0;
	if (snd_pcm_format_width(runtime->format) == 16) {
		ratebits |= NM_RATE_BITS_16;
		s->shift++;
	}
	if (runtime->channels > 1) {
		ratebits |= NM_RATE_STEREO;
		s->shift++;
	}

	runtime->rate = samplerates[rate_index];

	switch (substream->stream) {
	case SNDRV_PCM_STREAM_PLAYBACK:
		snd_nm256_load_coefficient(chip, 0, rate_index); /* 0 = playback */
		snd_nm256_writeb(chip,
				 NM_PLAYBACK_REG_OFFSET + NM_RATE_REG_OFFSET,
				 ratebits);
		break;
	case SNDRV_PCM_STREAM_CAPTURE:
		snd_nm256_load_coefficient(chip, 1, rate_index); /* 1 = record */
		snd_nm256_writeb(chip,
				 NM_RECORD_REG_OFFSET + NM_RATE_REG_OFFSET,
				 ratebits);
		break;
	}
}

/* acquire interrupt */
static int snd_nm256_acquire_irq(struct nm256 *chip)
{
	mutex_lock(&chip->irq_mutex);
	if (chip->irq < 0) {
		if (request_irq(chip->pci->irq, chip->interrupt, IRQF_SHARED,
				KBUILD_MODNAME, chip)) {
			snd_printk(KERN_ERR "unable to grab IRQ %d\n", chip->pci->irq);
			mutex_unlock(&chip->irq_mutex);
			return -EBUSY;
		}
		chip->irq = chip->pci->irq;
	}
	chip->irq_acks++;
	mutex_unlock(&chip->irq_mutex);
	return 0;
}

/* release interrupt */
static void snd_nm256_release_irq(struct nm256 *chip)
{
	mutex_lock(&chip->irq_mutex);
	if (chip->irq_acks > 0)
		chip->irq_acks--;
	if (chip->irq_acks == 0 && chip->irq >= 0) {
		free_irq(chip->irq, chip);
		chip->irq = -1;
	}
	mutex_unlock(&chip->irq_mutex);
}

/*
 * start / stop
 */

/* update the watermark (current period) */
static void snd_nm256_pcm_mark(struct nm256 *chip, struct nm256_stream *s, int reg)
{
	s->cur_period++;
	s->cur_period %= s->periods;
	snd_nm256_writel(chip, reg, s->buf + s->cur_period * s->period_size);
}

#define snd_nm256_playback_mark(chip, s) snd_nm256_pcm_mark(chip, s, NM_PBUFFER_WMARK)
#define snd_nm256_capture_mark(chip, s)  snd_nm256_pcm_mark(chip, s, NM_RBUFFER_WMARK)

static void
snd_nm256_playback_start(struct nm256 *chip, struct nm256_stream *s,
			 struct snd_pcm_substream *substream)
{
	/* program buffer pointers */
	snd_nm256_writel(chip, NM_PBUFFER_START, s->buf);
	snd_nm256_writel(chip, NM_PBUFFER_END, s->buf + s->dma_size - (1 << s->shift));
	snd_nm256_writel(chip, NM_PBUFFER_CURRP, s->buf);
	snd_nm256_playback_mark(chip, s);

	/* Enable playback engine and interrupts. */
	snd_nm256_writeb(chip, NM_PLAYBACK_ENABLE_REG,
			 NM_PLAYBACK_ENABLE_FLAG | NM_PLAYBACK_FREERUN);
	/* Enable both channels. */
	snd_nm256_writew(chip, NM_AUDIO_MUTE_REG, 0x0);
}

static void
snd_nm256_capture_start(struct nm256 *chip, struct nm256_stream *s,
			struct snd_pcm_substream *substream)
{
	/* program buffer pointers */
	snd_nm256_writel(chip, NM_RBUFFER_START, s->buf);
	snd_nm256_writel(chip, NM_RBUFFER_END, s->buf + s->dma_size);
	snd_nm256_writel(chip, NM_RBUFFER_CURRP, s->buf);
	snd_nm256_capture_mark(chip, s);

	/* Enable playback engine and interrupts. */
	snd_nm256_writeb(chip, NM_RECORD_ENABLE_REG,
			 NM_RECORD_ENABLE_FLAG | NM_RECORD_FREERUN);
}

/* Stop the play engine. */
static void
snd_nm256_playback_stop(struct nm256 *chip)
{
	/* Shut off sound from both channels. */
	snd_nm256_writew(chip, NM_AUDIO_MUTE_REG,
			 NM_AUDIO_MUTE_LEFT | NM_AUDIO_MUTE_RIGHT);
	/* Disable play engine. */
	snd_nm256_writeb(chip, NM_PLAYBACK_ENABLE_REG, 0);
}

static void
snd_nm256_capture_stop(struct nm256 *chip)
{
	/* Disable recording engine. */
	snd_nm256_writeb(chip, NM_RECORD_ENABLE_REG, 0);
}

static int
snd_nm256_playback_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct nm256 *chip = snd_pcm_substream_chip(substream);
	struct nm256_stream *s = substream->runtime->private_data;
	int err = 0;

	if (snd_BUG_ON(!s))
		return -ENXIO;

	spin_lock(&chip->reg_lock);
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_RESUME:
		s->suspended = 0;
		/* fallthru */
	case SNDRV_PCM_TRIGGER_START:
		if (! s->running) {
			snd_nm256_playback_start(chip, s, substream);
			s->running = 1;
		}
		break;
	case SNDRV_PCM_TRIGGER_SUSPEND:
		s->suspended = 1;
		/* fallthru */
	case SNDRV_PCM_TRIGGER_STOP:
		if (s->running) {
			snd_nm256_playback_stop(chip);
			s->running = 0;
		}
		break;
	default:
		err = -EINVAL;
		break;
	}
	spin_unlock(&chip->reg_lock);
	return err;
}

static int
snd_nm256_capture_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct nm256 *chip = snd_pcm_substream_chip(substream);
	struct nm256_stream *s = substream->runtime->private_data;
	int err = 0;

	if (snd_BUG_ON(!s))
		return -ENXIO;

	spin_lock(&chip->reg_lock);
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
		if (! s->running) {
			snd_nm256_capture_start(chip, s, substream);
			s->running = 1;
		}
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		if (s->running) {
			snd_nm256_capture_stop(chip);
			s->running = 0;
		}
		break;
	default:
		err = -EINVAL;
		break;
	}
	spin_unlock(&chip->reg_lock);
	return err;
}


/*
 * prepare playback/capture channel
 */
static int snd_nm256_pcm_prepare(struct snd_pcm_substream *substream)
{
	struct nm256 *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct nm256_stream *s = runtime->private_data;

	if (snd_BUG_ON(!s))
		return -ENXIO;
	s->dma_size = frames_to_bytes(runtime, substream->runtime->buffer_size);
	s->period_size = frames_to_bytes(runtime, substream->runtime->period_size);
	s->periods = substream->runtime->periods;
	s->cur_period = 0;

	spin_lock_irq(&chip->reg_lock);
	s->running = 0;
	snd_nm256_set_format(chip, s, substream);
	spin_unlock_irq(&chip->reg_lock);

	return 0;
}


/*
 * get the current pointer
 */
static snd_pcm_uframes_t
snd_nm256_playback_pointer(struct snd_pcm_substream *substream)
{
	struct nm256 *chip = snd_pcm_substream_chip(substream);
	struct nm256_stream *s = substream->runtime->private_data;
	unsigned long curp;

	if (snd_BUG_ON(!s))
		return 0;
	curp = snd_nm256_readl(chip, NM_PBUFFER_CURRP) - (unsigned long)s->buf;
	curp %= s->dma_size;
	return bytes_to_frames(substream->runtime, curp);
}

static snd_pcm_uframes_t
snd_nm256_capture_pointer(struct snd_pcm_substream *substream)
{
	struct nm256 *chip = snd_pcm_substream_chip(substream);
	struct nm256_stream *s = substream->runtime->private_data;
	unsigned long curp;

	if (snd_BUG_ON(!s))
		return 0;
	curp = snd_nm256_readl(chip, NM_RBUFFER_CURRP) - (unsigned long)s->buf;
	curp %= s->dma_size;	
	return bytes_to_frames(substream->runtime, curp);
}

/* Remapped I/O space can be accessible as pointer on i386 */
/* This might be changed in the future */
#ifndef __i386__
/*
 * silence / copy for playback
 */
static int
snd_nm256_playback_silence(struct snd_pcm_substream *substream,
			   int channel, /* not used (interleaved data) */
			   snd_pcm_uframes_t pos,
			   snd_pcm_uframes_t count)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct nm256_stream *s = runtime->private_data;
	count = frames_to_bytes(runtime, count);
	pos = frames_to_bytes(runtime, pos);
	memset_io(s->bufptr + pos, 0, count);
	return 0;
}

static int
snd_nm256_playback_copy(struct snd_pcm_substream *substream,
			int channel, /* not used (interleaved data) */
			snd_pcm_uframes_t pos,
			void __user *src,
			snd_pcm_uframes_t count)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct nm256_stream *s = runtime->private_data;
	count = frames_to_bytes(runtime, count);
	pos = frames_to_bytes(runtime, pos);
	if (copy_from_user_toio(s->bufptr + pos, src, count))
		return -EFAULT;
	return 0;
}

/*
 * copy to user
 */
static int
snd_nm256_capture_copy(struct snd_pcm_substream *substream,
		       int channel, /* not used (interleaved data) */
		       snd_pcm_uframes_t pos,
		       void __user *dst,
		       snd_pcm_uframes_t count)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct nm256_stream *s = runtime->private_data;
	count = frames_to_bytes(runtime, count);
	pos = frames_to_bytes(runtime, pos);
	if (copy_to_user_fromio(dst, s->bufptr + pos, count))
		return -EFAULT;
	return 0;
}

#endif /* !__i386__ */


/*
 * update playback/capture watermarks
 */

/* spinlock held! */
static void
snd_nm256_playback_update(struct nm256 *chip)
{
	struct nm256_stream *s;

	s = &chip->streams[SNDRV_PCM_STREAM_PLAYBACK];
	if (s->running && s->substream) {
		spin_unlock(&chip->reg_lock);
		snd_pcm_period_elapsed(s->substream);
		spin_lock(&chip->reg_lock);
		snd_nm256_playback_mark(chip, s);
	}
}

/* spinlock held! */
static void
snd_nm256_capture_update(struct nm256 *chip)
{
	struct nm256_stream *s;

	s = &chip->streams[SNDRV_PCM_STREAM_CAPTURE];
	if (s->running && s->substream) {
		spin_unlock(&chip->reg_lock);
		snd_pcm_period_elapsed(s->substream);
		spin_lock(&chip->reg_lock);
		snd_nm256_capture_mark(chip, s);
	}
}

/*
 * hardware info
 */
static struct snd_pcm_hardware snd_nm256_playback =
{
	.info =			SNDRV_PCM_INFO_MMAP_IOMEM |SNDRV_PCM_INFO_MMAP_VALID |
				SNDRV_PCM_INFO_INTERLEAVED |
				/*SNDRV_PCM_INFO_PAUSE |*/
				SNDRV_PCM_INFO_RESUME,
	.formats =		SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S16_LE,
	.rates =		SNDRV_PCM_RATE_KNOT/*24k*/ | SNDRV_PCM_RATE_8000_48000,
	.rate_min =		8000,
	.rate_max =		48000,
	.channels_min =		1,
	.channels_max =		2,
	.periods_min =		2,
	.periods_max =		1024,
	.buffer_bytes_max =	128 * 1024,
	.period_bytes_min =	256,
	.period_bytes_max =	128 * 1024,
};

static struct snd_pcm_hardware snd_nm256_capture =
{
	.info =			SNDRV_PCM_INFO_MMAP_IOMEM | SNDRV_PCM_INFO_MMAP_VALID |
				SNDRV_PCM_INFO_INTERLEAVED |
				/*SNDRV_PCM_INFO_PAUSE |*/
				SNDRV_PCM_INFO_RESUME,
	.formats =		SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S16_LE,
	.rates =		SNDRV_PCM_RATE_KNOT/*24k*/ | SNDRV_PCM_RATE_8000_48000,
	.rate_min =		8000,
	.rate_max =		48000,
	.channels_min =		1,
	.channels_max =		2,
	.periods_min =		2,
	.periods_max =		1024,
	.buffer_bytes_max =	128 * 1024,
	.period_bytes_min =	256,
	.period_bytes_max =	128 * 1024,
};


/* set dma transfer size */
static int snd_nm256_pcm_hw_params(struct snd_pcm_substream *substream,
				   struct snd_pcm_hw_params *hw_params)
{
	/* area and addr are already set and unchanged */
	substream->runtime->dma_bytes = params_buffer_bytes(hw_params);
	return 0;
}

/*
 * open
 */
static void snd_nm256_setup_stream(struct nm256 *chip, struct nm256_stream *s,
				   struct snd_pcm_substream *substream,
				   struct snd_pcm_hardware *hw_ptr)
{
	struct snd_pcm_runtime *runtime = substream->runtime;

	s->running = 0;
	runtime->hw = *hw_ptr;
	runtime->hw.buffer_bytes_max = s->bufsize;
	runtime->hw.period_bytes_max = s->bufsize / 2;
	runtime->dma_area = (void __force *) s->bufptr;
	runtime->dma_addr = s->bufptr_addr;
	runtime->dma_bytes = s->bufsize;
	runtime->private_data = s;
	s->substream = substream;

	snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_RATE,
				   &constraints_rates);
}

static int
snd_nm256_playback_open(struct snd_pcm_substream *substream)
{
	struct nm256 *chip = snd_pcm_substream_chip(substream);

	if (snd_nm256_acquire_irq(chip) < 0)
		return -EBUSY;
	snd_nm256_setup_stream(chip, &chip->streams[SNDRV_PCM_STREAM_PLAYBACK],
			       substream, &snd_nm256_playback);
	return 0;
}

static int
snd_nm256_capture_open(struct snd_pcm_substream *substream)
{
	struct nm256 *chip = snd_pcm_substream_chip(substream);

	if (snd_nm256_acquire_irq(chip) < 0)
		return -EBUSY;
	snd_nm256_setup_stream(chip, &chip->streams[SNDRV_PCM_STREAM_CAPTURE],
			       substream, &snd_nm256_capture);
	return 0;
}

/*
 * close - we don't have to do special..
 */
static int
snd_nm256_playback_close(struct snd_pcm_substream *substream)
{
	struct nm256 *chip = snd_pcm_substream_chip(substream);

	snd_nm256_release_irq(chip);
	return 0;
}


static int
snd_nm256_capture_close(struct snd_pcm_substream *substream)
{
	struct nm256 *chip = snd_pcm_substream_chip(substream);

	snd_nm256_release_irq(chip);
	return 0;
}

/*
 * create a pcm instance
 */
static struct snd_pcm_ops snd_nm256_playback_ops = {
	.open =		snd_nm256_playback_open,
	.close =	snd_nm256_playback_close,
	.ioctl =	snd_pcm_lib_ioctl,
	.hw_params =	snd_nm256_pcm_hw_params,
	.prepare =	snd_nm256_pcm_prepare,
	.trigger =	snd_nm256_playback_trigger,
	.pointer =	snd_nm256_playback_pointer,
#ifndef __i386__
	.copy =		snd_nm256_playback_copy,
	.silence =	snd_nm256_playback_silence,
#endif
	.mmap =		snd_pcm_lib_mmap_iomem,
};

static struct snd_pcm_ops snd_nm256_capture_ops = {
	.open =		snd_nm256_capture_open,
	.close =	snd_nm256_capture_close,
	.ioctl =	snd_pcm_lib_ioctl,
	.hw_params =	snd_nm256_pcm_hw_params,
	.prepare =	snd_nm256_pcm_prepare,
	.trigger =	snd_nm256_capture_trigger,
	.pointer =	snd_nm256_capture_pointer,
#ifndef __i386__
	.copy =		snd_nm256_capture_copy,
#endif
	.mmap =		snd_pcm_lib_mmap_iomem,
};

static int
snd_nm256_pcm(struct nm256 *chip, int device)
{
	struct snd_pcm *pcm;
	int i, err;

	for (i = 0; i < 2; i++) {
		struct nm256_stream *s = &chip->streams[i];
		s->bufptr = chip->buffer + (s->buf - chip->buffer_start);
		s->bufptr_addr = chip->buffer_addr + (s->buf - chip->buffer_start);
	}

	err = snd_pcm_new(chip->card, chip->card->driver, device,
			  1, 1, &pcm);
	if (err < 0)
		return err;

	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_nm256_playback_ops);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_nm256_capture_ops);

	pcm->private_data = chip;
	pcm->info_flags = 0;
	chip->pcm = pcm;

	return 0;
}


/* 
 * Initialize the hardware. 
 */
static void
snd_nm256_init_chip(struct nm256 *chip)
{
	/* Reset everything. */
	snd_nm256_writeb(chip, 0x0, 0x11);
	snd_nm256_writew(chip, 0x214, 0);
	/* stop sounds.. */
	//snd_nm256_playback_stop(chip);
	//snd_nm256_capture_stop(chip);
}


static irqreturn_t
snd_nm256_intr_check(struct nm256 *chip)
{
	if (chip->badintrcount++ > 1000) {
		/*
		 * I'm not sure if the best thing is to stop the card from
		 * playing or just release the interrupt (after all, we're in
		 * a bad situation, so doing fancy stuff may not be such a good
		 * idea).
		 *
		 * I worry about the card engine continuing to play noise
		 * over and over, however--that could become a very
		 * obnoxious problem.  And we know that when this usually
		 * happens things are fairly safe, it just means the user's
		 * inserted a PCMCIA card and someone's spamming us with IRQ 9s.
		 */
		if (chip->streams[SNDRV_PCM_STREAM_PLAYBACK].running)
			snd_nm256_playback_stop(chip);
		if (chip->streams[SNDRV_PCM_STREAM_CAPTURE].running)
			snd_nm256_capture_stop(chip);
		chip->badintrcount = 0;
		return IRQ_HANDLED;
	}
	return IRQ_NONE;
}

/* 
 * Handle a potential interrupt for the device referred to by DEV_ID. 
 *
 * I don't like the cut-n-paste job here either between the two routines,
 * but there are sufficient differences between the two interrupt handlers
 * that parameterizing it isn't all that great either.  (Could use a macro,
 * I suppose...yucky bleah.)
 */

static irqreturn_t
snd_nm256_interrupt(int irq, void *dev_id)
{
	struct nm256 *chip = dev_id;
	u16 status;
	u8 cbyte;

	status = snd_nm256_readw(chip, NM_INT_REG);

	/* Not ours. */
	if (status == 0)
		return snd_nm256_intr_check(chip);

	chip->badintrcount = 0;

	/* Rather boring; check for individual interrupts and process them. */

	spin_lock(&chip->reg_lock);
	if (status & NM_PLAYBACK_INT) {
		status &= ~NM_PLAYBACK_INT;
		NM_ACK_INT(chip, NM_PLAYBACK_INT);
		snd_nm256_playback_update(chip);
	}

	if (status & NM_RECORD_INT) {
		status &= ~NM_RECORD_INT;
		NM_ACK_INT(chip, NM_RECORD_INT);
		snd_nm256_capture_update(chip);
	}

	if (status & NM_MISC_INT_1) {
		status &= ~NM_MISC_INT_1;
		NM_ACK_INT(chip, NM_MISC_INT_1);
		snd_printd("NM256: Got misc interrupt #1\n");
		snd_nm256_writew(chip, NM_INT_REG, 0x8000);
		cbyte = snd_nm256_readb(chip, 0x400);
		snd_nm256_writeb(chip, 0x400, cbyte | 2);
	}

	if (status & NM_MISC_INT_2) {
		status &= ~NM_MISC_INT_2;
		NM_ACK_INT(chip, NM_MISC_INT_2);
		snd_printd("NM256: Got misc interrupt #2\n");
		cbyte = snd_nm256_readb(chip, 0x400);
		snd_nm256_writeb(chip, 0x400, cbyte & ~2);
	}

	/* Unknown interrupt. */
	if (status) {
		snd_printd("NM256: Fire in the hole! Unknown status 0x%x\n",
			   status);
		/* Pray. */
		NM_ACK_INT(chip, status);
	}

	spin_unlock(&chip->reg_lock);
	return IRQ_HANDLED;
}

/*
 * Handle a potential interrupt for the device referred to by DEV_ID.
 * This handler is for the 256ZX, and is very similar to the non-ZX
 * routine.
 */

static irqreturn_t
snd_nm256_interrupt_zx(int irq, void *dev_id)
{
	struct nm256 *chip = dev_id;
	u32 status;
	u8 cbyte;

	status = snd_nm256_readl(chip, NM_INT_REG);

	/* Not ours. */
	if (status == 0)
		return snd_nm256_intr_check(chip);

	chip->badintrcount = 0;

	/* Rather boring; check for individual interrupts and process them. */

	spin_lock(&chip->reg_lock);
	if (status & NM2_PLAYBACK_INT) {
		status &= ~NM2_PLAYBACK_INT;
		NM2_ACK_INT(chip, NM2_PLAYBACK_INT);
		snd_nm256_playback_update(chip);
	}

	if (status & NM2_RECORD_INT) {
		status &= ~NM2_RECORD_INT;
		NM2_ACK_INT(chip, NM2_RECORD_INT);
		snd_nm256_capture_update(chip);
	}

	if (status & NM2_MISC_INT_1) {
		status &= ~NM2_MISC_INT_1;
		NM2_ACK_INT(chip, NM2_MISC_INT_1);
		snd_printd("NM256: Got misc interrupt #1\n");
		cbyte = snd_nm256_readb(chip, 0x400);
		snd_nm256_writeb(chip, 0x400, cbyte | 2);
	}

	if (status & NM2_MISC_INT_2) {
		status &= ~NM2_MISC_INT_2;
		NM2_ACK_INT(chip, NM2_MISC_INT_2);
		snd_printd("NM256: Got misc interrupt #2\n");
		cbyte = snd_nm256_readb(chip, 0x400);
		snd_nm256_writeb(chip, 0x400, cbyte & ~2);
	}

	/* Unknown interrupt. */
	if (status) {
		snd_printd("NM256: Fire in the hole! Unknown status 0x%x\n",
			   status);
		/* Pray. */
		NM2_ACK_INT(chip, status);
	}

	spin_unlock(&chip->reg_lock);
	return IRQ_HANDLED;
}

/*
 * AC97 interface
 */

/*
 * Waits for the mixer to become ready to be written; returns a zero value
 * if it timed out.
 */
static int
snd_nm256_ac97_ready(struct nm256 *chip)
{
	int timeout = 10;
	u32 testaddr;
	u16 testb;

	testaddr = chip->mixer_status_offset;
	testb = chip->mixer_status_mask;

	/* 
	 * Loop around waiting for the mixer to become ready. 
	 */
	while (timeout-- > 0) {
		if ((snd_nm256_readw(chip, testaddr) & testb) == 0)
			return 1;
		udelay(100);
	}
	return 0;
}

/* 
 * Initial register values to be written to the AC97 mixer.
 * While most of these are identical to the reset values, we do this
 * so that we have most of the register contents cached--this avoids
 * reading from the mixer directly (which seems to be problematic,
 * probably due to ignorance).
 */

struct initialValues {
	unsigned short reg;
	unsigned short value;
};

static struct initialValues nm256_ac97_init_val[] =
{
	{ AC97_MASTER, 		0x8000 },
	{ AC97_HEADPHONE,	0x8000 },
	{ AC97_MASTER_MONO,	0x8000 },
	{ AC97_PC_BEEP,		0x8000 },
	{ AC97_PHONE,		0x8008 },
	{ AC97_MIC,		0x8000 },
	{ AC97_LINE,		0x8808 },
	{ AC97_CD,		0x8808 },
	{ AC97_VIDEO,		0x8808 },
	{ AC97_AUX,		0x8808 },
	{ AC97_PCM,		0x8808 },
	{ AC97_REC_SEL,		0x0000 },
	{ AC97_REC_GAIN,	0x0B0B },
	{ AC97_GENERAL_PURPOSE,	0x0000 },
	{ AC97_3D_CONTROL,	0x8000 }, 
	{ AC97_VENDOR_ID1, 	0x8384 },
	{ AC97_VENDOR_ID2,	0x7609 },
};

static int nm256_ac97_idx(unsigned short reg)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(nm256_ac97_init_val); i++)
		if (nm256_ac97_init_val[i].reg == reg)
			return i;
	return -1;
}

/*
 * some nm256 easily crash when reading from mixer registers
 * thus we're treating it as a write-only mixer and cache the
 * written values
 */
static unsigned short
snd_nm256_ac97_read(struct snd_ac97 *ac97, unsigned short reg)
{
	struct nm256 *chip = ac97->private_data;
	int idx = nm256_ac97_idx(reg);

	if (idx < 0)
		return 0;
	return chip->ac97_regs[idx];
}

/* 
 */
static void
snd_nm256_ac97_write(struct snd_ac97 *ac97,
		     unsigned short reg, unsigned short val)
{
	struct nm256 *chip = ac97->private_data;
	int tries = 2;
	int idx = nm256_ac97_idx(reg);
	u32 base;

	if (idx < 0)
		return;

	base = chip->mixer_base;

	snd_nm256_ac97_ready(chip);

	/* Wait for the write to take, too. */
	while (tries-- > 0) {
		snd_nm256_writew(chip, base + reg, val);
		msleep(1);  /* a little delay here seems better.. */
		if (snd_nm256_ac97_ready(chip)) {
			/* successful write: set cache */
			chip->ac97_regs[idx] = val;
			return;
		}
	}
	snd_printd("nm256: ac97 codec not ready..\n");
}

/* static resolution table */
static struct snd_ac97_res_table nm256_res_table[] = {
	{ AC97_MASTER, 0x1f1f },
	{ AC97_HEADPHONE, 0x1f1f },
	{ AC97_MASTER_MONO, 0x001f },
	{ AC97_PC_BEEP, 0x001f },
	{ AC97_PHONE, 0x001f },
	{ AC97_MIC, 0x001f },
	{ AC97_LINE, 0x1f1f },
	{ AC97_CD, 0x1f1f },
	{ AC97_VIDEO, 0x1f1f },
	{ AC97_AUX, 0x1f1f },
	{ AC97_PCM, 0x1f1f },
	{ AC97_REC_GAIN, 0x0f0f },
	{ } /* terminator */
};

/* initialize the ac97 into a known state */
static void
snd_nm256_ac97_reset(struct snd_ac97 *ac97)
{
	struct nm256 *chip = ac97->private_data;

	/* Reset the mixer.  'Tis magic!  */
	snd_nm256_writeb(chip, 0x6c0, 1);
	if (! chip->reset_workaround) {
		/* Dell latitude LS will lock up by this */
		snd_nm256_writeb(chip, 0x6cc, 0x87);
	}
	if (! chip->reset_workaround_2) {
		/* Dell latitude CSx will lock up by this */
		snd_nm256_writeb(chip, 0x6cc, 0x80);
		snd_nm256_writeb(chip, 0x6cc, 0x0);
	}
	if (! chip->in_resume) {
		int i;
		for (i = 0; i < ARRAY_SIZE(nm256_ac97_init_val); i++) {
			/* preload the cache, so as to avoid even a single
			 * read of the mixer regs
			 */
			snd_nm256_ac97_write(ac97, nm256_ac97_init_val[i].reg,
					     nm256_ac97_init_val[i].value);
		}
	}
}

/* create an ac97 mixer interface */
static int
snd_nm256_mixer(struct nm256 *chip)
{
	struct snd_ac97_bus *pbus;
	struct snd_ac97_template ac97;
	int err;
	static struct snd_ac97_bus_ops ops = {
		.reset = snd_nm256_ac97_reset,
		.write = snd_nm256_ac97_write,
		.read = snd_nm256_ac97_read,
	};

	chip->ac97_regs = kcalloc(ARRAY_SIZE(nm256_ac97_init_val),
				  sizeof(short), GFP_KERNEL);
	if (! chip->ac97_regs)
		return -ENOMEM;

	if ((err = snd_ac97_bus(chip->card, 0, &ops, NULL, &pbus)) < 0)
		return err;

	memset(&ac97, 0, sizeof(ac97));
	ac97.scaps = AC97_SCAP_AUDIO; /* we support audio! */
	ac97.private_data = chip;
	ac97.res_table = nm256_res_table;
	pbus->no_vra = 1;
	err = snd_ac97_mixer(pbus, &ac97, &chip->ac97);
	if (err < 0)
		return err;
	if (! (chip->ac97->id & (0xf0000000))) {
		/* looks like an invalid id */
		sprintf(chip->card->mixername, "%s AC97", chip->card->driver);
	}
	return 0;
}

/* 
 * See if the signature left by the NM256 BIOS is intact; if so, we use
 * the associated address as the end of our audio buffer in the video
 * RAM.
 */

static int
snd_nm256_peek_for_sig(struct nm256 *chip)
{
	/* The signature is located 1K below the end of video RAM.  */
	void __iomem *temp;
	/* Default buffer end is 5120 bytes below the top of RAM.  */
	unsigned long pointer_found = chip->buffer_end - 0x1400;
	u32 sig;

	temp = ioremap_nocache(chip->buffer_addr + chip->buffer_end - 0x400, 16);
	if (temp == NULL) {
		snd_printk(KERN_ERR "Unable to scan for card signature in video RAM\n");
		return -EBUSY;
	}

	sig = readl(temp);
	if ((sig & NM_SIG_MASK) == NM_SIGNATURE) {
		u32 pointer = readl(temp + 4);

		/*
		 * If it's obviously invalid, don't use it
		 */
		if (pointer == 0xffffffff ||
		    pointer < chip->buffer_size ||
		    pointer > chip->buffer_end) {
			snd_printk(KERN_ERR "invalid signature found: 0x%x\n", pointer);
			iounmap(temp);
			return -ENODEV;
		} else {
			pointer_found = pointer;
			printk(KERN_INFO "nm256: found card signature in video RAM: 0x%x\n",
			       pointer);
		}
	}

	iounmap(temp);
	chip->buffer_end = pointer_found;

	return 0;
}

#ifdef CONFIG_PM_SLEEP
/*
 * APM event handler, so the card is properly reinitialized after a power
 * event.
 */
static int nm256_suspend(struct device *dev)
{
	struct pci_dev *pci = to_pci_dev(dev);
	struct snd_card *card = dev_get_drvdata(dev);
	struct nm256 *chip = card->private_data;

	snd_power_change_state(card, SNDRV_CTL_POWER_D3hot);
	snd_pcm_suspend_all(chip->pcm);
	snd_ac97_suspend(chip->ac97);
	chip->coeffs_current = 0;
	pci_disable_device(pci);
	pci_save_state(pci);
	pci_set_power_state(pci, PCI_D3hot);
	return 0;
}

static int nm256_resume(struct device *dev)
{
	struct pci_dev *pci = to_pci_dev(dev);
	struct snd_card *card = dev_get_drvdata(dev);
	struct nm256 *chip = card->private_data;
	int i;

	/* Perform a full reset on the hardware */
	chip->in_resume = 1;

	pci_set_power_state(pci, PCI_D0);
	pci_restore_state(pci);
	if (pci_enable_device(pci) < 0) {
		printk(KERN_ERR "nm256: pci_enable_device failed, "
		       "disabling device\n");
		snd_card_disconnect(card);
		return -EIO;
	}
	pci_set_master(pci);

	snd_nm256_init_chip(chip);

	/* restore ac97 */
	snd_ac97_resume(chip->ac97);

	for (i = 0; i < 2; i++) {
		struct nm256_stream *s = &chip->streams[i];
		if (s->substream && s->suspended) {
			spin_lock_irq(&chip->reg_lock);
			snd_nm256_set_format(chip, s, s->substream);
			spin_unlock_irq(&chip->reg_lock);
		}
	}

	snd_power_change_state(card, SNDRV_CTL_POWER_D0);
	chip->in_resume = 0;
	return 0;
}

static SIMPLE_DEV_PM_OPS(nm256_pm, nm256_suspend, nm256_resume);
#define NM256_PM_OPS	&nm256_pm
#else
#define NM256_PM_OPS	NULL
#endif /* CONFIG_PM_SLEEP */

static int snd_nm256_free(struct nm256 *chip)
{
	if (chip->streams[SNDRV_PCM_STREAM_PLAYBACK].running)
		snd_nm256_playback_stop(chip);
	if (chip->streams[SNDRV_PCM_STREAM_CAPTURE].running)
		snd_nm256_capture_stop(chip);

	if (chip->irq >= 0)
		free_irq(chip->irq, chip);

	if (chip->cport)
		iounmap(chip->cport);
	if (chip->buffer)
		iounmap(chip->buffer);
	release_and_free_resource(chip->res_cport);
	release_and_free_resource(chip->res_buffer);

	pci_disable_device(chip->pci);
	kfree(chip->ac97_regs);
	kfree(chip);
	return 0;
}

static int snd_nm256_dev_free(struct snd_device *device)
{
	struct nm256 *chip = device->device_data;
	return snd_nm256_free(chip);
}

static int
snd_nm256_create(struct snd_card *card, struct pci_dev *pci,
		 struct nm256 **chip_ret)
{
	struct nm256 *chip;
	int err, pval;
	static struct snd_device_ops ops = {
		.dev_free =	snd_nm256_dev_free,
	};
	u32 addr;

	*chip_ret = NULL;

	if ((err = pci_enable_device(pci)) < 0)
		return err;

	chip = kzalloc(sizeof(*chip), GFP_KERNEL);
	if (chip == NULL) {
		pci_disable_device(pci);
		return -ENOMEM;
	}

	chip->card = card;
	chip->pci = pci;
	chip->use_cache = use_cache;
	spin_lock_init(&chip->reg_lock);
	chip->irq = -1;
	mutex_init(&chip->irq_mutex);

	/* store buffer sizes in bytes */
	chip->streams[SNDRV_PCM_STREAM_PLAYBACK].bufsize = playback_bufsize * 1024;
	chip->streams[SNDRV_PCM_STREAM_CAPTURE].bufsize = capture_bufsize * 1024;

	/* 
	 * The NM256 has two memory ports.  The first port is nothing
	 * more than a chunk of video RAM, which is used as the I/O ring
	 * buffer.  The second port has the actual juicy stuff (like the
	 * mixer and the playback engine control registers).
	 */

	chip->buffer_addr = pci_resource_start(pci, 0);
	chip->cport_addr = pci_resource_start(pci, 1);

	/* Init the memory port info.  */
	/* remap control port (#2) */
	chip->res_cport = request_mem_region(chip->cport_addr, NM_PORT2_SIZE,
					     card->driver);
	if (chip->res_cport == NULL) {
		snd_printk(KERN_ERR "memory region 0x%lx (size 0x%x) busy\n",
			   chip->cport_addr, NM_PORT2_SIZE);
		err = -EBUSY;
		goto __error;
	}
	chip->cport = ioremap_nocache(chip->cport_addr, NM_PORT2_SIZE);
	if (chip->cport == NULL) {
		snd_printk(KERN_ERR "unable to map control port %lx\n", chip->cport_addr);
		err = -ENOMEM;
		goto __error;
	}

	if (!strcmp(card->driver, "NM256AV")) {
		/* Ok, try to see if this is a non-AC97 version of the hardware. */
		pval = snd_nm256_readw(chip, NM_MIXER_PRESENCE);
		if ((pval & NM_PRESENCE_MASK) != NM_PRESENCE_VALUE) {
			if (! force_ac97) {
				printk(KERN_ERR "nm256: no ac97 is found!\n");
				printk(KERN_ERR "  force the driver to load by "
				       "passing in the module parameter\n");
				printk(KERN_ERR "    force_ac97=1\n");
				printk(KERN_ERR "  or try sb16, opl3sa2, or "
				       "cs423x drivers instead.\n");
				err = -ENXIO;
				goto __error;
			}
		}
		chip->buffer_end = 2560 * 1024;
		chip->interrupt = snd_nm256_interrupt;
		chip->mixer_status_offset = NM_MIXER_STATUS_OFFSET;
		chip->mixer_status_mask = NM_MIXER_READY_MASK;
	} else {
		/* Not sure if there is any relevant detect for the ZX or not.  */
		if (snd_nm256_readb(chip, 0xa0b) != 0)
			chip->buffer_end = 6144 * 1024;
		else
			chip->buffer_end = 4096 * 1024;

		chip->interrupt = snd_nm256_interrupt_zx;
		chip->mixer_status_offset = NM2_MIXER_STATUS_OFFSET;
		chip->mixer_status_mask = NM2_MIXER_READY_MASK;
	}
	
	chip->buffer_size = chip->streams[SNDRV_PCM_STREAM_PLAYBACK].bufsize +
		chip->streams[SNDRV_PCM_STREAM_CAPTURE].bufsize;
	if (chip->use_cache)
		chip->buffer_size += NM_TOTAL_COEFF_COUNT * 4;
	else
		chip->buffer_size += NM_MAX_PLAYBACK_COEF_SIZE + NM_MAX_RECORD_COEF_SIZE;

	if (buffer_top >= chip->buffer_size && buffer_top < chip->buffer_end)
		chip->buffer_end = buffer_top;
	else {
		/* get buffer end pointer from signature */
		if ((err = snd_nm256_peek_for_sig(chip)) < 0)
			goto __error;
	}

	chip->buffer_start = chip->buffer_end - chip->buffer_size;
	chip->buffer_addr += chip->buffer_start;

	printk(KERN_INFO "nm256: Mapping port 1 from 0x%x - 0x%x\n",
	       chip->buffer_start, chip->buffer_end);

	chip->res_buffer = request_mem_region(chip->buffer_addr,
					      chip->buffer_size,
					      card->driver);
	if (chip->res_buffer == NULL) {
		snd_printk(KERN_ERR "nm256: buffer 0x%lx (size 0x%x) busy\n",
			   chip->buffer_addr, chip->buffer_size);
		err = -EBUSY;
		goto __error;
	}
	chip->buffer = ioremap_nocache(chip->buffer_addr, chip->buffer_size);
	if (chip->buffer == NULL) {
		err = -ENOMEM;
		snd_printk(KERN_ERR "unable to map ring buffer at %lx\n", chip->buffer_addr);
		goto __error;
	}

	/* set offsets */
	addr = chip->buffer_start;
	chip->streams[SNDRV_PCM_STREAM_PLAYBACK].buf = addr;
	addr += chip->streams[SNDRV_PCM_STREAM_PLAYBACK].bufsize;
	chip->streams[SNDRV_PCM_STREAM_CAPTURE].buf = addr;
	addr += chip->streams[SNDRV_PCM_STREAM_CAPTURE].bufsize;
	if (chip->use_cache) {
		chip->all_coeff_buf = addr;
	} else {
		chip->coeff_buf[SNDRV_PCM_STREAM_PLAYBACK] = addr;
		addr += NM_MAX_PLAYBACK_COEF_SIZE;
		chip->coeff_buf[SNDRV_PCM_STREAM_CAPTURE] = addr;
	}

	/* Fixed setting. */
	chip->mixer_base = NM_MIXER_OFFSET;

	chip->coeffs_current = 0;

	snd_nm256_init_chip(chip);

	// pci_set_master(pci); /* needed? */
	
	if ((err = snd_device_new(card, SNDRV_DEV_LOWLEVEL, chip, &ops)) < 0)
		goto __error;

	snd_card_set_dev(card, &pci->dev);

	*chip_ret = chip;
	return 0;

__error:
	snd_nm256_free(chip);
	return err;
}


enum { NM_BLACKLISTED, NM_RESET_WORKAROUND, NM_RESET_WORKAROUND_2 };

static struct snd_pci_quirk nm256_quirks[] = {
	/* HP omnibook 4150 has cs4232 codec internally */
	SND_PCI_QUIRK(0x103c, 0x0007, "HP omnibook 4150", NM_BLACKLISTED),
	/* Reset workarounds to avoid lock-ups */
	SND_PCI_QUIRK(0x104d, 0x8041, "Sony PCG-F305", NM_RESET_WORKAROUND),
	SND_PCI_QUIRK(0x1028, 0x0080, "Dell Latitude LS", NM_RESET_WORKAROUND),
	SND_PCI_QUIRK(0x1028, 0x0091, "Dell Latitude CSx", NM_RESET_WORKAROUND_2),
	{ } /* terminator */
};


static int snd_nm256_probe(struct pci_dev *pci,
			   const struct pci_device_id *pci_id)
{
	struct snd_card *card;
	struct nm256 *chip;
	int err;
	const struct snd_pci_quirk *q;

	q = snd_pci_quirk_lookup(pci, nm256_quirks);
	if (q) {
		snd_printdd(KERN_INFO "nm256: Enabled quirk for %s.\n", q->name);
		switch (q->value) {
		case NM_BLACKLISTED:
			printk(KERN_INFO "nm256: The device is blacklisted. "
			       "Loading stopped\n");
			return -ENODEV;
		case NM_RESET_WORKAROUND_2:
			reset_workaround_2 = 1;
			/* Fall-through */
		case NM_RESET_WORKAROUND:
			reset_workaround = 1;
			break;
		}
	}

	err = snd_card_create(index, id, THIS_MODULE, 0, &card);
	if (err < 0)
		return err;

	switch (pci->device) {
	case PCI_DEVICE_ID_NEOMAGIC_NM256AV_AUDIO:
		strcpy(card->driver, "NM256AV");
		break;
	case PCI_DEVICE_ID_NEOMAGIC_NM256ZX_AUDIO:
		strcpy(card->driver, "NM256ZX");
		break;
	case PCI_DEVICE_ID_NEOMAGIC_NM256XL_PLUS_AUDIO:
		strcpy(card->driver, "NM256XL+");
		break;
	default:
		snd_printk(KERN_ERR "invalid device id 0x%x\n", pci->device);
		snd_card_free(card);
		return -EINVAL;
	}

	if (vaio_hack)
		buffer_top = 0x25a800;	/* this avoids conflicts with XFree86 server */

	if (playback_bufsize < 4)
		playback_bufsize = 4;
	if (playback_bufsize > 128)
		playback_bufsize = 128;
	if (capture_bufsize < 4)
		capture_bufsize = 4;
	if (capture_bufsize > 128)
		capture_bufsize = 128;
	if ((err = snd_nm256_create(card, pci, &chip)) < 0) {
		snd_card_free(card);
		return err;
	}
	card->private_data = chip;

	if (reset_workaround) {
		snd_printdd(KERN_INFO "nm256: reset_workaround activated\n");
		chip->reset_workaround = 1;
	}

	if (reset_workaround_2) {
		snd_printdd(KERN_INFO "nm256: reset_workaround_2 activated\n");
		chip->reset_workaround_2 = 1;
	}

	if ((err = snd_nm256_pcm(chip, 0)) < 0 ||
	    (err = snd_nm256_mixer(chip)) < 0) {
		snd_card_free(card);
		return err;
	}

	sprintf(card->shortname, "NeoMagic %s", card->driver);
	sprintf(card->longname, "%s at 0x%lx & 0x%lx, irq %d",
		card->shortname,
		chip->buffer_addr, chip->cport_addr, chip->irq);

	if ((err = snd_card_register(card)) < 0) {
		snd_card_free(card);
		return err;
	}

	pci_set_drvdata(pci, card);
	return 0;
}

static void snd_nm256_remove(struct pci_dev *pci)
{
	snd_card_free(pci_get_drvdata(pci));
	pci_set_drvdata(pci, NULL);
}


static struct pci_driver nm256_driver = {
	.name = KBUILD_MODNAME,
	.id_table = snd_nm256_ids,
	.probe = snd_nm256_probe,
	.remove = snd_nm256_remove,
	.driver = {
		.pm = NM256_PM_OPS,
	},
};

module_pci_driver(nm256_driver);
