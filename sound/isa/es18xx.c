// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Driver for generic ESS AudioDrive ES18xx soundcards
 *  Copyright (c) by Christian Fischbach <fishbach@pool.informatik.rwth-aachen.de>
 *  Copyright (c) by Abramo Bagnara <abramo@alsa-project.org>
 */
/* GENERAL NOTES:
 *
 * BUGS:
 * - There are pops (we can't delay in trigger function, cause midlevel 
 *   often need to trigger down and then up very quickly).
 *   Any ideas?
 * - Support for 16 bit DMA seems to be broken. I've no hardware to tune it.
 */

/*
 * ES1868  NOTES:
 * - The chip has one half duplex pcm (with very limited full duplex support).
 *
 * - Duplex stereophonic sound is impossible.
 * - Record and playback must share the same frequency rate.
 *
 * - The driver use dma2 for playback and dma1 for capture.
 */

/*
 * ES1869 NOTES:
 *
 * - there are a first full duplex pcm and a second playback only pcm
 *   (incompatible with first pcm capture)
 * 
 * - there is support for the capture volume and ESS Spatializer 3D effect.
 *
 * - contrarily to some pages in DS_1869.PDF the rates can be set
 *   independently.
 *
 * - Zoom Video is implemented by sharing the FM DAC, thus the user can
 *   have either FM playback or Video playback but not both simultaneously.
 *   The Video Playback Switch mixer control toggles this choice.
 *
 * BUGS:
 *
 * - There is a major trouble I noted:
 *
 *   using both channel for playback stereo 16 bit samples at 44100 Hz
 *   the second pcm (Audio1) DMA slows down irregularly and sound is garbled.
 *   
 *   The same happens using Audio1 for captureing.
 *
 *   The Windows driver does not suffer of this (although it use Audio1
 *   only for captureing). I'm unable to discover why.
 *
 */

/*
 * ES1879 NOTES:
 * - When Zoom Video is enabled (reg 0x71 bit 6 toggled on) the PCM playback
 *   seems to be effected (speaker_test plays a lower frequency). Can't find
 *   anything in the datasheet to account for this, so a Video Playback Switch
 *   control has been included to allow ZV to be enabled only when necessary.
 *   Then again on at least one test system the 0x71 bit 6 enable bit is not 
 *   needed for ZV, so maybe the datasheet is entirely wrong here.
 */
 
#include <linux/init.h>
#include <linux/err.h>
#include <linux/isa.h>
#include <linux/pnp.h>
#include <linux/isapnp.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/io.h>

#include <asm/dma.h>
#include <sound/core.h>
#include <sound/control.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/mpu401.h>
#include <sound/opl3.h>
#define SNDRV_LEGACY_FIND_FREE_IRQ
#define SNDRV_LEGACY_FIND_FREE_DMA
#include <sound/initval.h>

struct snd_es18xx {
	struct snd_card *card;
	unsigned long port;		/* port of ESS chip */
	unsigned long ctrl_port;	/* Control port of ESS chip */
	int irq;			/* IRQ number of ESS chip */
	int dma1;			/* DMA1 */
	int dma2;			/* DMA2 */
	unsigned short version;		/* version of ESS chip */
	int caps;			/* Chip capabilities */
	unsigned short audio2_vol;	/* volume level of audio2 */

	unsigned short active;		/* active channel mask */
	unsigned int dma1_shift;
	unsigned int dma2_shift;

	struct snd_pcm *pcm;
	struct snd_pcm_substream *playback_a_substream;
	struct snd_pcm_substream *capture_a_substream;
	struct snd_pcm_substream *playback_b_substream;

	struct snd_rawmidi *rmidi;

	struct snd_kcontrol *hw_volume;
	struct snd_kcontrol *hw_switch;
	struct snd_kcontrol *master_volume;
	struct snd_kcontrol *master_switch;

	spinlock_t reg_lock;
	spinlock_t mixer_lock;
#ifdef CONFIG_PM
	unsigned char pm_reg;
#endif
#ifdef CONFIG_PNP
	struct pnp_dev *dev;
	struct pnp_dev *devc;
#endif
};

#define AUDIO1_IRQ	0x01
#define AUDIO2_IRQ	0x02
#define HWV_IRQ		0x04
#define MPU_IRQ		0x08

#define ES18XX_PCM2	0x0001	/* Has two useable PCM */
#define ES18XX_SPATIALIZER 0x0002	/* Has 3D Spatializer */
#define ES18XX_RECMIX	0x0004	/* Has record mixer */
#define ES18XX_DUPLEX_MONO 0x0008	/* Has mono duplex only */
#define ES18XX_DUPLEX_SAME 0x0010	/* Playback and record must share the same rate */
#define ES18XX_NEW_RATE	0x0020	/* More precise rate setting */
#define ES18XX_AUXB	0x0040	/* AuxB mixer control */
#define ES18XX_HWV	0x0080	/* Has separate hardware volume mixer controls*/
#define ES18XX_MONO	0x0100	/* Mono_in mixer control */
#define ES18XX_I2S	0x0200	/* I2S mixer control */
#define ES18XX_MUTEREC	0x0400	/* Record source can be muted */
#define ES18XX_CONTROL	0x0800	/* Has control ports */
#define ES18XX_GPO_2BIT	0x1000	/* GPO0,1 controlled by PM port */

/* Power Management */
#define ES18XX_PM	0x07
#define ES18XX_PM_GPO0	0x01
#define ES18XX_PM_GPO1	0x02
#define ES18XX_PM_PDR	0x04
#define ES18XX_PM_ANA	0x08
#define ES18XX_PM_FM	0x020
#define ES18XX_PM_SUS	0x080

/* Lowlevel */

#define DAC1 0x01
#define ADC1 0x02
#define DAC2 0x04
#define MILLISECOND 10000

static int snd_es18xx_dsp_command(struct snd_es18xx *chip, unsigned char val)
{
        int i;

        for(i = MILLISECOND; i; i--)
                if ((inb(chip->port + 0x0C) & 0x80) == 0) {
                        outb(val, chip->port + 0x0C);
                        return 0;
                }
	dev_err(chip->card->dev, "dsp_command: timeout (0x%x)\n", val);
        return -EINVAL;
}

static int snd_es18xx_dsp_get_byte(struct snd_es18xx *chip)
{
        int i;

        for(i = MILLISECOND/10; i; i--)
                if (inb(chip->port + 0x0C) & 0x40)
                        return inb(chip->port + 0x0A);
	dev_err(chip->card->dev, "dsp_get_byte failed: 0x%lx = 0x%x!!!\n",
		chip->port + 0x0A, inb(chip->port + 0x0A));
        return -ENODEV;
}

#undef REG_DEBUG

static int snd_es18xx_write(struct snd_es18xx *chip,
			    unsigned char reg, unsigned char data)
{
	int ret;
	
	guard(spinlock_irqsave)(&chip->reg_lock);
	ret = snd_es18xx_dsp_command(chip, reg);
	if (ret < 0)
		return ret;
        ret = snd_es18xx_dsp_command(chip, data);
#ifdef REG_DEBUG
	dev_dbg(chip->card->dev, "Reg %02x set to %02x\n", reg, data);
#endif
	return ret;
}

static int snd_es18xx_read(struct snd_es18xx *chip, unsigned char reg)
{
	int ret, data;

	guard(spinlock_irqsave)(&chip->reg_lock);
	ret = snd_es18xx_dsp_command(chip, 0xC0);
	if (ret < 0)
		return ret;
        ret = snd_es18xx_dsp_command(chip, reg);
	if (ret < 0)
		return ret;
	data = snd_es18xx_dsp_get_byte(chip);
	ret = data;
#ifdef REG_DEBUG
	dev_dbg(chip->card->dev, "Reg %02x now is %02x (%d)\n", reg, data, ret);
#endif
	return ret;
}

/* Return old value */
static int snd_es18xx_bits(struct snd_es18xx *chip, unsigned char reg,
			   unsigned char mask, unsigned char val)
{
        int ret;
	unsigned char old, new, oval;

	guard(spinlock_irqsave)(&chip->reg_lock);
        ret = snd_es18xx_dsp_command(chip, 0xC0);
	if (ret < 0)
		return ret;
        ret = snd_es18xx_dsp_command(chip, reg);
	if (ret < 0)
		return ret;
	ret = snd_es18xx_dsp_get_byte(chip);
	if (ret < 0)
		return ret;
	old = ret;
	oval = old & mask;
	if (val != oval) {
		ret = snd_es18xx_dsp_command(chip, reg);
		if (ret < 0)
			return ret;
		new = (old & ~mask) | (val & mask);
		ret = snd_es18xx_dsp_command(chip, new);
		if (ret < 0)
			return ret;
#ifdef REG_DEBUG
		dev_dbg(chip->card->dev, "Reg %02x was %02x, set to %02x (%d)\n",
			reg, old, new, ret);
#endif
	}
	return oval;
}

static inline void snd_es18xx_mixer_write(struct snd_es18xx *chip,
			    unsigned char reg, unsigned char data)
{
	guard(spinlock_irqsave)(&chip->mixer_lock);
        outb(reg, chip->port + 0x04);
        outb(data, chip->port + 0x05);
#ifdef REG_DEBUG
	dev_dbg(chip->card->dev, "Mixer reg %02x set to %02x\n", reg, data);
#endif
}

static inline int snd_es18xx_mixer_read(struct snd_es18xx *chip, unsigned char reg)
{
	int data;

	guard(spinlock_irqsave)(&chip->mixer_lock);
        outb(reg, chip->port + 0x04);
	data = inb(chip->port + 0x05);
#ifdef REG_DEBUG
	dev_dbg(chip->card->dev, "Mixer reg %02x now is %02x\n", reg, data);
#endif
        return data;
}

/* Return old value */
static inline int snd_es18xx_mixer_bits(struct snd_es18xx *chip, unsigned char reg,
					unsigned char mask, unsigned char val)
{
	unsigned char old, new, oval;

	guard(spinlock_irqsave)(&chip->mixer_lock);
        outb(reg, chip->port + 0x04);
	old = inb(chip->port + 0x05);
	oval = old & mask;
	if (val != oval) {
		new = (old & ~mask) | (val & mask);
		outb(new, chip->port + 0x05);
#ifdef REG_DEBUG
		dev_dbg(chip->card->dev, "Mixer reg %02x was %02x, set to %02x\n",
			reg, old, new);
#endif
	}
	return oval;
}

static inline int snd_es18xx_mixer_writable(struct snd_es18xx *chip, unsigned char reg,
					    unsigned char mask)
{
	int old, expected, new;

	guard(spinlock_irqsave)(&chip->mixer_lock);
        outb(reg, chip->port + 0x04);
	old = inb(chip->port + 0x05);
	expected = old ^ mask;
	outb(expected, chip->port + 0x05);
	new = inb(chip->port + 0x05);
#ifdef REG_DEBUG
	dev_dbg(chip->card->dev, "Mixer reg %02x was %02x, set to %02x, now is %02x\n",
		reg, old, expected, new);
#endif
	return expected == new;
}


static int snd_es18xx_reset(struct snd_es18xx *chip)
{
	int i;
        outb(0x03, chip->port + 0x06);
        inb(chip->port + 0x06);
        outb(0x00, chip->port + 0x06);
        for(i = 0; i < MILLISECOND && !(inb(chip->port + 0x0E) & 0x80); i++);
        if (inb(chip->port + 0x0A) != 0xAA)
                return -1;
	return 0;
}

static int snd_es18xx_reset_fifo(struct snd_es18xx *chip)
{
        outb(0x02, chip->port + 0x06);
        inb(chip->port + 0x06);
        outb(0x00, chip->port + 0x06);
	return 0;
}

static const struct snd_ratnum new_clocks[2] = {
	{
		.num = 793800,
		.den_min = 1,
		.den_max = 128,
		.den_step = 1,
	},
	{
		.num = 768000,
		.den_min = 1,
		.den_max = 128,
		.den_step = 1,
	}
};

static const struct snd_pcm_hw_constraint_ratnums new_hw_constraints_clocks = {
	.nrats = 2,
	.rats = new_clocks,
};

static const struct snd_ratnum old_clocks[2] = {
	{
		.num = 795444,
		.den_min = 1,
		.den_max = 128,
		.den_step = 1,
	},
	{
		.num = 397722,
		.den_min = 1,
		.den_max = 128,
		.den_step = 1,
	}
};

static const struct snd_pcm_hw_constraint_ratnums old_hw_constraints_clocks  = {
	.nrats = 2,
	.rats = old_clocks,
};


static void snd_es18xx_rate_set(struct snd_es18xx *chip, 
				struct snd_pcm_substream *substream,
				int mode)
{
	unsigned int bits, div0;
	struct snd_pcm_runtime *runtime = substream->runtime;
	if (chip->caps & ES18XX_NEW_RATE) {
		if (runtime->rate_num == new_clocks[0].num)
			bits = 128 - runtime->rate_den;
		else
			bits = 256 - runtime->rate_den;
	} else {
		if (runtime->rate_num == old_clocks[0].num)
			bits = 256 - runtime->rate_den;
		else
			bits = 128 - runtime->rate_den;
	}

	/* set filter register */
	div0 = 256 - 7160000*20/(8*82*runtime->rate);
		
	if ((chip->caps & ES18XX_PCM2) && mode == DAC2) {
		snd_es18xx_mixer_write(chip, 0x70, bits);
		/*
		 * Comment from kernel oss driver:
		 * FKS: fascinating: 0x72 doesn't seem to work.
		 */
		snd_es18xx_write(chip, 0xA2, div0);
		snd_es18xx_mixer_write(chip, 0x72, div0);
	} else {
		snd_es18xx_write(chip, 0xA1, bits);
		snd_es18xx_write(chip, 0xA2, div0);
	}
}

static int snd_es18xx_playback_hw_params(struct snd_pcm_substream *substream,
					 struct snd_pcm_hw_params *hw_params)
{
	struct snd_es18xx *chip = snd_pcm_substream_chip(substream);
	int shift;

	shift = 0;
	if (params_channels(hw_params) == 2)
		shift++;
	if (snd_pcm_format_width(params_format(hw_params)) == 16)
		shift++;

	if (substream->number == 0 && (chip->caps & ES18XX_PCM2)) {
		if ((chip->caps & ES18XX_DUPLEX_MONO) &&
		    (chip->capture_a_substream) &&
		    params_channels(hw_params) != 1) {
			_snd_pcm_hw_param_setempty(hw_params, SNDRV_PCM_HW_PARAM_CHANNELS);
			return -EBUSY;
		}
		chip->dma2_shift = shift;
	} else {
		chip->dma1_shift = shift;
	}
	return 0;
}

static int snd_es18xx_playback1_prepare(struct snd_es18xx *chip,
					struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	unsigned int size = snd_pcm_lib_buffer_bytes(substream);
	unsigned int count = snd_pcm_lib_period_bytes(substream);

        snd_es18xx_rate_set(chip, substream, DAC2);

        /* Transfer Count Reload */
        count = 0x10000 - count;
        snd_es18xx_mixer_write(chip, 0x74, count & 0xff);
        snd_es18xx_mixer_write(chip, 0x76, count >> 8);

	/* Set format */
        snd_es18xx_mixer_bits(chip, 0x7A, 0x07,
			      ((runtime->channels == 1) ? 0x00 : 0x02) |
			      (snd_pcm_format_width(runtime->format) == 16 ? 0x01 : 0x00) |
			      (snd_pcm_format_unsigned(runtime->format) ? 0x00 : 0x04));

        /* Set DMA controller */
        snd_dma_program(chip->dma2, runtime->dma_addr, size, DMA_MODE_WRITE | DMA_AUTOINIT);

	return 0;
}

static int snd_es18xx_playback1_trigger(struct snd_es18xx *chip,
					struct snd_pcm_substream *substream,
					int cmd)
{
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
		if (chip->active & DAC2)
			return 0;
		chip->active |= DAC2;
                /* Start DMA */
		if (chip->dma2 >= 4)
			snd_es18xx_mixer_write(chip, 0x78, 0xb3);
		else
			snd_es18xx_mixer_write(chip, 0x78, 0x93);
#ifdef AVOID_POPS
		/* Avoid pops */
		mdelay(100);
		if (chip->caps & ES18XX_PCM2)
			/* Restore Audio 2 volume */
			snd_es18xx_mixer_write(chip, 0x7C, chip->audio2_vol);
		else
			/* Enable PCM output */
			snd_es18xx_dsp_command(chip, 0xD1);
#endif
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		if (!(chip->active & DAC2))
			return 0;
		chip->active &= ~DAC2;
                /* Stop DMA */
                snd_es18xx_mixer_write(chip, 0x78, 0x00);
#ifdef AVOID_POPS
		mdelay(25);
		if (chip->caps & ES18XX_PCM2)
			/* Set Audio 2 volume to 0 */
			snd_es18xx_mixer_write(chip, 0x7C, 0);
		else
			/* Disable PCM output */
			snd_es18xx_dsp_command(chip, 0xD3);
#endif
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int snd_es18xx_capture_hw_params(struct snd_pcm_substream *substream,
					struct snd_pcm_hw_params *hw_params)
{
	struct snd_es18xx *chip = snd_pcm_substream_chip(substream);
	int shift;

	shift = 0;
	if ((chip->caps & ES18XX_DUPLEX_MONO) &&
	    chip->playback_a_substream &&
	    params_channels(hw_params) != 1) {
		_snd_pcm_hw_param_setempty(hw_params, SNDRV_PCM_HW_PARAM_CHANNELS);
		return -EBUSY;
	}
	if (params_channels(hw_params) == 2)
		shift++;
	if (snd_pcm_format_width(params_format(hw_params)) == 16)
		shift++;
	chip->dma1_shift = shift;
	return 0;
}

static int snd_es18xx_capture_prepare(struct snd_pcm_substream *substream)
{
        struct snd_es18xx *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	unsigned int size = snd_pcm_lib_buffer_bytes(substream);
	unsigned int count = snd_pcm_lib_period_bytes(substream);

	snd_es18xx_reset_fifo(chip);

        /* Set stereo/mono */
        snd_es18xx_bits(chip, 0xA8, 0x03, runtime->channels == 1 ? 0x02 : 0x01);

        snd_es18xx_rate_set(chip, substream, ADC1);

        /* Transfer Count Reload */
	count = 0x10000 - count;
	snd_es18xx_write(chip, 0xA4, count & 0xff);
	snd_es18xx_write(chip, 0xA5, count >> 8);

#ifdef AVOID_POPS
	mdelay(100);
#endif

        /* Set format */
        snd_es18xx_write(chip, 0xB7, 
                         snd_pcm_format_unsigned(runtime->format) ? 0x51 : 0x71);
        snd_es18xx_write(chip, 0xB7, 0x90 |
                         ((runtime->channels == 1) ? 0x40 : 0x08) |
                         (snd_pcm_format_width(runtime->format) == 16 ? 0x04 : 0x00) |
                         (snd_pcm_format_unsigned(runtime->format) ? 0x00 : 0x20));

        /* Set DMA controller */
        snd_dma_program(chip->dma1, runtime->dma_addr, size, DMA_MODE_READ | DMA_AUTOINIT);

	return 0;
}

static int snd_es18xx_capture_trigger(struct snd_pcm_substream *substream,
				      int cmd)
{
        struct snd_es18xx *chip = snd_pcm_substream_chip(substream);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
		if (chip->active & ADC1)
			return 0;
		chip->active |= ADC1;
                /* Start DMA */
                snd_es18xx_write(chip, 0xB8, 0x0f);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		if (!(chip->active & ADC1))
			return 0;
		chip->active &= ~ADC1;
                /* Stop DMA */
                snd_es18xx_write(chip, 0xB8, 0x00);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int snd_es18xx_playback2_prepare(struct snd_es18xx *chip,
					struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	unsigned int size = snd_pcm_lib_buffer_bytes(substream);
	unsigned int count = snd_pcm_lib_period_bytes(substream);

	snd_es18xx_reset_fifo(chip);

        /* Set stereo/mono */
        snd_es18xx_bits(chip, 0xA8, 0x03, runtime->channels == 1 ? 0x02 : 0x01);

        snd_es18xx_rate_set(chip, substream, DAC1);

        /* Transfer Count Reload */
	count = 0x10000 - count;
	snd_es18xx_write(chip, 0xA4, count & 0xff);
	snd_es18xx_write(chip, 0xA5, count >> 8);

        /* Set format */
        snd_es18xx_write(chip, 0xB6,
                         snd_pcm_format_unsigned(runtime->format) ? 0x80 : 0x00);
        snd_es18xx_write(chip, 0xB7, 
                         snd_pcm_format_unsigned(runtime->format) ? 0x51 : 0x71);
        snd_es18xx_write(chip, 0xB7, 0x90 |
                         (runtime->channels == 1 ? 0x40 : 0x08) |
                         (snd_pcm_format_width(runtime->format) == 16 ? 0x04 : 0x00) |
                         (snd_pcm_format_unsigned(runtime->format) ? 0x00 : 0x20));

        /* Set DMA controller */
        snd_dma_program(chip->dma1, runtime->dma_addr, size, DMA_MODE_WRITE | DMA_AUTOINIT);

	return 0;
}

static int snd_es18xx_playback2_trigger(struct snd_es18xx *chip,
					struct snd_pcm_substream *substream,
					int cmd)
{
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
		if (chip->active & DAC1)
			return 0;
		chip->active |= DAC1;
                /* Start DMA */
                snd_es18xx_write(chip, 0xB8, 0x05);
#ifdef AVOID_POPS
		/* Avoid pops */
		mdelay(100);
                /* Enable Audio 1 */
                snd_es18xx_dsp_command(chip, 0xD1);
#endif
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		if (!(chip->active & DAC1))
			return 0;
		chip->active &= ~DAC1;
                /* Stop DMA */
                snd_es18xx_write(chip, 0xB8, 0x00);
#ifdef AVOID_POPS
		/* Avoid pops */
		mdelay(25);
                /* Disable Audio 1 */
                snd_es18xx_dsp_command(chip, 0xD3);
#endif
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int snd_es18xx_playback_prepare(struct snd_pcm_substream *substream)
{
        struct snd_es18xx *chip = snd_pcm_substream_chip(substream);
	if (substream->number == 0 && (chip->caps & ES18XX_PCM2))
		return snd_es18xx_playback1_prepare(chip, substream);
	else
		return snd_es18xx_playback2_prepare(chip, substream);
}

static int snd_es18xx_playback_trigger(struct snd_pcm_substream *substream,
				       int cmd)
{
        struct snd_es18xx *chip = snd_pcm_substream_chip(substream);
	if (substream->number == 0 && (chip->caps & ES18XX_PCM2))
		return snd_es18xx_playback1_trigger(chip, substream, cmd);
	else
		return snd_es18xx_playback2_trigger(chip, substream, cmd);
}

static irqreturn_t snd_es18xx_interrupt(int irq, void *dev_id)
{
	struct snd_card *card = dev_id;
	struct snd_es18xx *chip = card->private_data;
	unsigned char status;

	if (chip->caps & ES18XX_CONTROL) {
		/* Read Interrupt status */
		status = inb(chip->ctrl_port + 6);
	} else {
		/* Read Interrupt status */
		status = snd_es18xx_mixer_read(chip, 0x7f) >> 4;
	}
#if 0
	else {
		status = 0;
		if (inb(chip->port + 0x0C) & 0x01)
			status |= AUDIO1_IRQ;
		if (snd_es18xx_mixer_read(chip, 0x7A) & 0x80)
			status |= AUDIO2_IRQ;
		if ((chip->caps & ES18XX_HWV) &&
		    snd_es18xx_mixer_read(chip, 0x64) & 0x10)
			status |= HWV_IRQ;
	}
#endif

	/* Audio 1 & Audio 2 */
        if (status & AUDIO2_IRQ) {
                if (chip->active & DAC2)
                	snd_pcm_period_elapsed(chip->playback_a_substream);
		/* ack interrupt */
                snd_es18xx_mixer_bits(chip, 0x7A, 0x80, 0x00);
        }
        if (status & AUDIO1_IRQ) {
                /* ok.. capture is active */
                if (chip->active & ADC1)
                	snd_pcm_period_elapsed(chip->capture_a_substream);
                /* ok.. playback2 is active */
                else if (chip->active & DAC1)
                	snd_pcm_period_elapsed(chip->playback_b_substream);
		/* ack interrupt */
		inb(chip->port + 0x0E);
        }

	/* MPU */
	if ((status & MPU_IRQ) && chip->rmidi)
		snd_mpu401_uart_interrupt(irq, chip->rmidi->private_data);

	/* Hardware volume */
	if (status & HWV_IRQ) {
		int split = 0;
		if (chip->caps & ES18XX_HWV) {
			split = snd_es18xx_mixer_read(chip, 0x64) & 0x80;
			snd_ctl_notify(card, SNDRV_CTL_EVENT_MASK_VALUE,
					&chip->hw_switch->id);
			snd_ctl_notify(card, SNDRV_CTL_EVENT_MASK_VALUE,
					&chip->hw_volume->id);
		}
		if (!split) {
			snd_ctl_notify(card, SNDRV_CTL_EVENT_MASK_VALUE,
					&chip->master_switch->id);
			snd_ctl_notify(card, SNDRV_CTL_EVENT_MASK_VALUE,
					&chip->master_volume->id);
		}
		/* ack interrupt */
		snd_es18xx_mixer_write(chip, 0x66, 0x00);
	}
	return IRQ_HANDLED;
}

static snd_pcm_uframes_t snd_es18xx_playback_pointer(struct snd_pcm_substream *substream)
{
        struct snd_es18xx *chip = snd_pcm_substream_chip(substream);
	unsigned int size = snd_pcm_lib_buffer_bytes(substream);
	int pos;

	if (substream->number == 0 && (chip->caps & ES18XX_PCM2)) {
		if (!(chip->active & DAC2))
			return 0;
		pos = snd_dma_pointer(chip->dma2, size);
		return pos >> chip->dma2_shift;
	} else {
		if (!(chip->active & DAC1))
			return 0;
		pos = snd_dma_pointer(chip->dma1, size);
		return pos >> chip->dma1_shift;
	}
}

static snd_pcm_uframes_t snd_es18xx_capture_pointer(struct snd_pcm_substream *substream)
{
        struct snd_es18xx *chip = snd_pcm_substream_chip(substream);
	unsigned int size = snd_pcm_lib_buffer_bytes(substream);
	int pos;

        if (!(chip->active & ADC1))
                return 0;
	pos = snd_dma_pointer(chip->dma1, size);
	return pos >> chip->dma1_shift;
}

static const struct snd_pcm_hardware snd_es18xx_playback =
{
	.info =			(SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_RESUME |
				 SNDRV_PCM_INFO_MMAP_VALID),
	.formats =		(SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S8 | 
				 SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_U16_LE),
	.rates =		SNDRV_PCM_RATE_CONTINUOUS | SNDRV_PCM_RATE_8000_48000,
	.rate_min =		4000,
	.rate_max =		48000,
	.channels_min =		1,
	.channels_max =		2,
	.buffer_bytes_max =	65536,
	.period_bytes_min =	64,
	.period_bytes_max =	65536,
	.periods_min =		1,
	.periods_max =		1024,
	.fifo_size =		0,
};

static const struct snd_pcm_hardware snd_es18xx_capture =
{
	.info =			(SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_RESUME |
				 SNDRV_PCM_INFO_MMAP_VALID),
	.formats =		(SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S8 | 
				 SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_U16_LE),
	.rates =		SNDRV_PCM_RATE_CONTINUOUS | SNDRV_PCM_RATE_8000_48000,
	.rate_min =		4000,
	.rate_max =		48000,
	.channels_min =		1,
	.channels_max =		2,
	.buffer_bytes_max =	65536,
	.period_bytes_min =	64,
	.period_bytes_max =	65536,
	.periods_min =		1,
	.periods_max =		1024,
	.fifo_size =		0,
};

static int snd_es18xx_playback_open(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
        struct snd_es18xx *chip = snd_pcm_substream_chip(substream);

	if (substream->number == 0 && (chip->caps & ES18XX_PCM2)) {
		if ((chip->caps & ES18XX_DUPLEX_MONO) &&
		    chip->capture_a_substream && 
		    chip->capture_a_substream->runtime->channels != 1)
			return -EAGAIN;
		chip->playback_a_substream = substream;
	} else if (substream->number <= 1) {
		if (chip->capture_a_substream)
			return -EAGAIN;
		chip->playback_b_substream = substream;
	} else {
		snd_BUG();
		return -EINVAL;
	}
	substream->runtime->hw = snd_es18xx_playback;
	snd_pcm_hw_constraint_ratnums(runtime, 0, SNDRV_PCM_HW_PARAM_RATE,
				      (chip->caps & ES18XX_NEW_RATE) ? &new_hw_constraints_clocks : &old_hw_constraints_clocks);
        return 0;
}

static int snd_es18xx_capture_open(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
        struct snd_es18xx *chip = snd_pcm_substream_chip(substream);

        if (chip->playback_b_substream)
                return -EAGAIN;
	if ((chip->caps & ES18XX_DUPLEX_MONO) &&
	    chip->playback_a_substream &&
	    chip->playback_a_substream->runtime->channels != 1)
		return -EAGAIN;
        chip->capture_a_substream = substream;
	substream->runtime->hw = snd_es18xx_capture;
	snd_pcm_hw_constraint_ratnums(runtime, 0, SNDRV_PCM_HW_PARAM_RATE,
				      (chip->caps & ES18XX_NEW_RATE) ? &new_hw_constraints_clocks : &old_hw_constraints_clocks);
        return 0;
}

static int snd_es18xx_playback_close(struct snd_pcm_substream *substream)
{
        struct snd_es18xx *chip = snd_pcm_substream_chip(substream);

	if (substream->number == 0 && (chip->caps & ES18XX_PCM2))
		chip->playback_a_substream = NULL;
	else
		chip->playback_b_substream = NULL;
	
	return 0;
}

static int snd_es18xx_capture_close(struct snd_pcm_substream *substream)
{
        struct snd_es18xx *chip = snd_pcm_substream_chip(substream);

        chip->capture_a_substream = NULL;
        return 0;
}

/*
 *  MIXER part
 */

/* Record source mux routines:
 * Depending on the chipset this mux switches between 4, 5, or 8 possible inputs.
 * bit table for the 4/5 source mux:
 * reg 1C:
 *  b2 b1 b0   muxSource
 *   x  0  x   microphone
 *   0  1  x   CD
 *   1  1  0   line
 *   1  1  1   mixer
 * if it's "mixer" and it's a 5 source mux chipset then reg 7A bit 3 determines
 * either the play mixer or the capture mixer.
 *
 * "map4Source" translates from source number to reg bit pattern
 * "invMap4Source" translates from reg bit pattern to source number
 */

static int snd_es18xx_info_mux(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	static const char * const texts5Source[5] = {
		"Mic", "CD", "Line", "Master", "Mix"
	};
	static const char * const texts8Source[8] = {
		"Mic", "Mic Master", "CD", "AOUT",
		"Mic1", "Mix", "Line", "Master"
	};
	struct snd_es18xx *chip = snd_kcontrol_chip(kcontrol);

	switch (chip->version) {
	case 0x1868:
	case 0x1878:
		return snd_ctl_enum_info(uinfo, 1, 4, texts5Source);
	case 0x1887:
	case 0x1888:
		return snd_ctl_enum_info(uinfo, 1, 5, texts5Source);
	case 0x1869: /* DS somewhat contradictory for 1869: could be 5 or 8 */
	case 0x1879:
		return snd_ctl_enum_info(uinfo, 1, 8, texts8Source);
	default:
		return -EINVAL;
	}
}

static int snd_es18xx_get_mux(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	static const unsigned char invMap4Source[8] = {0, 0, 1, 1, 0, 0, 2, 3};
	struct snd_es18xx *chip = snd_kcontrol_chip(kcontrol);
	int muxSource = snd_es18xx_mixer_read(chip, 0x1c) & 0x07;
	if (!(chip->version == 0x1869 || chip->version == 0x1879)) {
		muxSource = invMap4Source[muxSource];
		if (muxSource==3 && 
		    (chip->version == 0x1887 || chip->version == 0x1888) &&
		    (snd_es18xx_mixer_read(chip, 0x7a) & 0x08)
		) 
			muxSource = 4;
	}
	ucontrol->value.enumerated.item[0] = muxSource;
	return 0;
}

static int snd_es18xx_put_mux(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	static const unsigned char map4Source[4] = {0, 2, 6, 7};
	struct snd_es18xx *chip = snd_kcontrol_chip(kcontrol);
	unsigned char val = ucontrol->value.enumerated.item[0];
	unsigned char retVal = 0;

	switch (chip->version) {
 /* 5 source chips */
	case 0x1887:
	case 0x1888:
		if (val > 4)
			return -EINVAL;
		if (val == 4) {
			retVal = snd_es18xx_mixer_bits(chip, 0x7a, 0x08, 0x08) != 0x08;
			val = 3;
		} else
			retVal = snd_es18xx_mixer_bits(chip, 0x7a, 0x08, 0x00) != 0x00;
		fallthrough;
 /* 4 source chips */
	case 0x1868:
	case 0x1878:
		if (val > 3)
			return -EINVAL;
		val = map4Source[val];
		break;
 /* 8 source chips */
	case 0x1869:
	case 0x1879:
		if (val > 7)
			return -EINVAL;
		break;
	default:
		return -EINVAL;
	}
	return (snd_es18xx_mixer_bits(chip, 0x1c, 0x07, val) != val) || retVal;
}

#define snd_es18xx_info_spatializer_enable	snd_ctl_boolean_mono_info

static int snd_es18xx_get_spatializer_enable(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_es18xx *chip = snd_kcontrol_chip(kcontrol);
	unsigned char val = snd_es18xx_mixer_read(chip, 0x50);
	ucontrol->value.integer.value[0] = !!(val & 8);
	return 0;
}

static int snd_es18xx_put_spatializer_enable(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_es18xx *chip = snd_kcontrol_chip(kcontrol);
	unsigned char oval, nval;
	int change;
	nval = ucontrol->value.integer.value[0] ? 0x0c : 0x04;
	oval = snd_es18xx_mixer_read(chip, 0x50) & 0x0c;
	change = nval != oval;
	if (change) {
		snd_es18xx_mixer_write(chip, 0x50, nval & ~0x04);
		snd_es18xx_mixer_write(chip, 0x50, nval);
	}
	return change;
}

static int snd_es18xx_info_hw_volume(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 63;
	return 0;
}

static int snd_es18xx_get_hw_volume(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_es18xx *chip = snd_kcontrol_chip(kcontrol);
	ucontrol->value.integer.value[0] = snd_es18xx_mixer_read(chip, 0x61) & 0x3f;
	ucontrol->value.integer.value[1] = snd_es18xx_mixer_read(chip, 0x63) & 0x3f;
	return 0;
}

#define snd_es18xx_info_hw_switch	snd_ctl_boolean_stereo_info

static int snd_es18xx_get_hw_switch(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_es18xx *chip = snd_kcontrol_chip(kcontrol);
	ucontrol->value.integer.value[0] = !(snd_es18xx_mixer_read(chip, 0x61) & 0x40);
	ucontrol->value.integer.value[1] = !(snd_es18xx_mixer_read(chip, 0x63) & 0x40);
	return 0;
}

static void snd_es18xx_hwv_free(struct snd_kcontrol *kcontrol)
{
	struct snd_es18xx *chip = snd_kcontrol_chip(kcontrol);
	chip->master_volume = NULL;
	chip->master_switch = NULL;
	chip->hw_volume = NULL;
	chip->hw_switch = NULL;
}

static int snd_es18xx_reg_bits(struct snd_es18xx *chip, unsigned char reg,
			       unsigned char mask, unsigned char val)
{
	if (reg < 0xa0)
		return snd_es18xx_mixer_bits(chip, reg, mask, val);
	else
		return snd_es18xx_bits(chip, reg, mask, val);
}

static int snd_es18xx_reg_read(struct snd_es18xx *chip, unsigned char reg)
{
	if (reg < 0xa0)
		return snd_es18xx_mixer_read(chip, reg);
	else
		return snd_es18xx_read(chip, reg);
}

#define ES18XX_SINGLE(xname, xindex, reg, shift, mask, flags) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, .index = xindex, \
  .info = snd_es18xx_info_single, \
  .get = snd_es18xx_get_single, .put = snd_es18xx_put_single, \
  .private_value = reg | (shift << 8) | (mask << 16) | (flags << 24) }

#define ES18XX_FL_INVERT	(1 << 0)
#define ES18XX_FL_PMPORT	(1 << 1)

static int snd_es18xx_info_single(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	int mask = (kcontrol->private_value >> 16) & 0xff;

	uinfo->type = mask == 1 ? SNDRV_CTL_ELEM_TYPE_BOOLEAN : SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = mask;
	return 0;
}

static int snd_es18xx_get_single(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_es18xx *chip = snd_kcontrol_chip(kcontrol);
	int reg = kcontrol->private_value & 0xff;
	int shift = (kcontrol->private_value >> 8) & 0xff;
	int mask = (kcontrol->private_value >> 16) & 0xff;
	int invert = (kcontrol->private_value >> 24) & ES18XX_FL_INVERT;
	int pm_port = (kcontrol->private_value >> 24) & ES18XX_FL_PMPORT;
	int val;

	if (pm_port)
		val = inb(chip->port + ES18XX_PM);
	else
		val = snd_es18xx_reg_read(chip, reg);
	ucontrol->value.integer.value[0] = (val >> shift) & mask;
	if (invert)
		ucontrol->value.integer.value[0] = mask - ucontrol->value.integer.value[0];
	return 0;
}

static int snd_es18xx_put_single(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_es18xx *chip = snd_kcontrol_chip(kcontrol);
	int reg = kcontrol->private_value & 0xff;
	int shift = (kcontrol->private_value >> 8) & 0xff;
	int mask = (kcontrol->private_value >> 16) & 0xff;
	int invert = (kcontrol->private_value >> 24) & ES18XX_FL_INVERT;
	int pm_port = (kcontrol->private_value >> 24) & ES18XX_FL_PMPORT;
	unsigned char val;
	
	val = (ucontrol->value.integer.value[0] & mask);
	if (invert)
		val = mask - val;
	mask <<= shift;
	val <<= shift;
	if (pm_port) {
		unsigned char cur = inb(chip->port + ES18XX_PM);

		if ((cur & mask) == val)
			return 0;
		outb((cur & ~mask) | val, chip->port + ES18XX_PM);
		return 1;
	}

	return snd_es18xx_reg_bits(chip, reg, mask, val) != val;
}

#define ES18XX_DOUBLE(xname, xindex, left_reg, right_reg, shift_left, shift_right, mask, invert) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, .index = xindex, \
  .info = snd_es18xx_info_double, \
  .get = snd_es18xx_get_double, .put = snd_es18xx_put_double, \
  .private_value = left_reg | (right_reg << 8) | (shift_left << 16) | (shift_right << 19) | (mask << 24) | (invert << 22) }

static int snd_es18xx_info_double(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	int mask = (kcontrol->private_value >> 24) & 0xff;

	uinfo->type = mask == 1 ? SNDRV_CTL_ELEM_TYPE_BOOLEAN : SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = mask;
	return 0;
}

static int snd_es18xx_get_double(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_es18xx *chip = snd_kcontrol_chip(kcontrol);
	int left_reg = kcontrol->private_value & 0xff;
	int right_reg = (kcontrol->private_value >> 8) & 0xff;
	int shift_left = (kcontrol->private_value >> 16) & 0x07;
	int shift_right = (kcontrol->private_value >> 19) & 0x07;
	int mask = (kcontrol->private_value >> 24) & 0xff;
	int invert = (kcontrol->private_value >> 22) & 1;
	unsigned char left, right;
	
	left = snd_es18xx_reg_read(chip, left_reg);
	if (left_reg != right_reg)
		right = snd_es18xx_reg_read(chip, right_reg);
	else
		right = left;
	ucontrol->value.integer.value[0] = (left >> shift_left) & mask;
	ucontrol->value.integer.value[1] = (right >> shift_right) & mask;
	if (invert) {
		ucontrol->value.integer.value[0] = mask - ucontrol->value.integer.value[0];
		ucontrol->value.integer.value[1] = mask - ucontrol->value.integer.value[1];
	}
	return 0;
}

static int snd_es18xx_put_double(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_es18xx *chip = snd_kcontrol_chip(kcontrol);
	int left_reg = kcontrol->private_value & 0xff;
	int right_reg = (kcontrol->private_value >> 8) & 0xff;
	int shift_left = (kcontrol->private_value >> 16) & 0x07;
	int shift_right = (kcontrol->private_value >> 19) & 0x07;
	int mask = (kcontrol->private_value >> 24) & 0xff;
	int invert = (kcontrol->private_value >> 22) & 1;
	int change;
	unsigned char val1, val2, mask1, mask2;
	
	val1 = ucontrol->value.integer.value[0] & mask;
	val2 = ucontrol->value.integer.value[1] & mask;
	if (invert) {
		val1 = mask - val1;
		val2 = mask - val2;
	}
	val1 <<= shift_left;
	val2 <<= shift_right;
	mask1 = mask << shift_left;
	mask2 = mask << shift_right;
	if (left_reg != right_reg) {
		change = 0;
		if (snd_es18xx_reg_bits(chip, left_reg, mask1, val1) != val1)
			change = 1;
		if (snd_es18xx_reg_bits(chip, right_reg, mask2, val2) != val2)
			change = 1;
	} else {
		change = (snd_es18xx_reg_bits(chip, left_reg, mask1 | mask2, 
					      val1 | val2) != (val1 | val2));
	}
	return change;
}

/* Mixer controls
 * These arrays contain setup data for mixer controls.
 * 
 * The controls that are universal to all chipsets are fully initialized
 * here.
 */
static const struct snd_kcontrol_new snd_es18xx_base_controls[] = {
ES18XX_DOUBLE("Master Playback Volume", 0, 0x60, 0x62, 0, 0, 63, 0),
ES18XX_DOUBLE("Master Playback Switch", 0, 0x60, 0x62, 6, 6, 1, 1),
ES18XX_DOUBLE("Line Playback Volume", 0, 0x3e, 0x3e, 4, 0, 15, 0),
ES18XX_DOUBLE("CD Playback Volume", 0, 0x38, 0x38, 4, 0, 15, 0),
ES18XX_DOUBLE("FM Playback Volume", 0, 0x36, 0x36, 4, 0, 15, 0),
ES18XX_DOUBLE("Mic Playback Volume", 0, 0x1a, 0x1a, 4, 0, 15, 0),
ES18XX_DOUBLE("Aux Playback Volume", 0, 0x3a, 0x3a, 4, 0, 15, 0),
ES18XX_SINGLE("Record Monitor", 0, 0xa8, 3, 1, 0),
ES18XX_DOUBLE("Capture Volume", 0, 0xb4, 0xb4, 4, 0, 15, 0),
{
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Capture Source",
	.info = snd_es18xx_info_mux,
	.get = snd_es18xx_get_mux,
	.put = snd_es18xx_put_mux,
}
};

static const struct snd_kcontrol_new snd_es18xx_recmix_controls[] = {
ES18XX_DOUBLE("PCM Capture Volume", 0, 0x69, 0x69, 4, 0, 15, 0),
ES18XX_DOUBLE("Mic Capture Volume", 0, 0x68, 0x68, 4, 0, 15, 0),
ES18XX_DOUBLE("Line Capture Volume", 0, 0x6e, 0x6e, 4, 0, 15, 0),
ES18XX_DOUBLE("FM Capture Volume", 0, 0x6b, 0x6b, 4, 0, 15, 0),
ES18XX_DOUBLE("CD Capture Volume", 0, 0x6a, 0x6a, 4, 0, 15, 0),
ES18XX_DOUBLE("Aux Capture Volume", 0, 0x6c, 0x6c, 4, 0, 15, 0)
};

/*
 * The chipset specific mixer controls
 */
static const struct snd_kcontrol_new snd_es18xx_opt_speaker =
	ES18XX_SINGLE("Beep Playback Volume", 0, 0x3c, 0, 7, 0);

static const struct snd_kcontrol_new snd_es18xx_opt_1869[] = {
ES18XX_SINGLE("Capture Switch", 0, 0x1c, 4, 1, ES18XX_FL_INVERT),
ES18XX_SINGLE("Video Playback Switch", 0, 0x7f, 0, 1, 0),
ES18XX_DOUBLE("Mono Playback Volume", 0, 0x6d, 0x6d, 4, 0, 15, 0),
ES18XX_DOUBLE("Mono Capture Volume", 0, 0x6f, 0x6f, 4, 0, 15, 0)
};

static const struct snd_kcontrol_new snd_es18xx_opt_1878 =
	ES18XX_DOUBLE("Video Playback Volume", 0, 0x68, 0x68, 4, 0, 15, 0);

static const struct snd_kcontrol_new snd_es18xx_opt_1879[] = {
ES18XX_SINGLE("Video Playback Switch", 0, 0x71, 6, 1, 0),
ES18XX_DOUBLE("Video Playback Volume", 0, 0x6d, 0x6d, 4, 0, 15, 0),
ES18XX_DOUBLE("Video Capture Volume", 0, 0x6f, 0x6f, 4, 0, 15, 0)
};

static const struct snd_kcontrol_new snd_es18xx_pcm1_controls[] = {
ES18XX_DOUBLE("PCM Playback Volume", 0, 0x14, 0x14, 4, 0, 15, 0),
};

static const struct snd_kcontrol_new snd_es18xx_pcm2_controls[] = {
ES18XX_DOUBLE("PCM Playback Volume", 0, 0x7c, 0x7c, 4, 0, 15, 0),
ES18XX_DOUBLE("PCM Playback Volume", 1, 0x14, 0x14, 4, 0, 15, 0)
};

static const struct snd_kcontrol_new snd_es18xx_spatializer_controls[] = {
ES18XX_SINGLE("3D Control - Level", 0, 0x52, 0, 63, 0),
{
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "3D Control - Switch",
	.info = snd_es18xx_info_spatializer_enable,
	.get = snd_es18xx_get_spatializer_enable,
	.put = snd_es18xx_put_spatializer_enable,
}
};

static const struct snd_kcontrol_new snd_es18xx_micpre1_control =
ES18XX_SINGLE("Mic Boost (+26dB)", 0, 0xa9, 2, 1, 0);

static const struct snd_kcontrol_new snd_es18xx_micpre2_control =
ES18XX_SINGLE("Mic Boost (+26dB)", 0, 0x7d, 3, 1, 0);

static const struct snd_kcontrol_new snd_es18xx_hw_volume_controls[] = {
{
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Hardware Master Playback Volume",
	.access = SNDRV_CTL_ELEM_ACCESS_READ,
	.info = snd_es18xx_info_hw_volume,
	.get = snd_es18xx_get_hw_volume,
},
{
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Hardware Master Playback Switch",
	.access = SNDRV_CTL_ELEM_ACCESS_READ,
	.info = snd_es18xx_info_hw_switch,
	.get = snd_es18xx_get_hw_switch,
},
ES18XX_SINGLE("Hardware Master Volume Split", 0, 0x64, 7, 1, 0),
};

static const struct snd_kcontrol_new snd_es18xx_opt_gpo_2bit[] = {
ES18XX_SINGLE("GPO0 Switch", 0, ES18XX_PM, 0, 1, ES18XX_FL_PMPORT),
ES18XX_SINGLE("GPO1 Switch", 0, ES18XX_PM, 1, 1, ES18XX_FL_PMPORT),
};

static int snd_es18xx_config_read(struct snd_es18xx *chip, unsigned char reg)
{
	outb(reg, chip->ctrl_port);
	return inb(chip->ctrl_port + 1);
}

static void snd_es18xx_config_write(struct snd_es18xx *chip,
				    unsigned char reg, unsigned char data)
{
	/* No need for spinlocks, this function is used only in
	   otherwise protected init code */
	outb(reg, chip->ctrl_port);
	outb(data, chip->ctrl_port + 1);
#ifdef REG_DEBUG
	dev_dbg(chip->card->dev, "Config reg %02x set to %02x\n", reg, data);
#endif
}

static int snd_es18xx_initialize(struct snd_es18xx *chip,
				 unsigned long mpu_port,
				 unsigned long fm_port)
{
	int mask = 0;

        /* enable extended mode */
        snd_es18xx_dsp_command(chip, 0xC6);
	/* Reset mixer registers */
	snd_es18xx_mixer_write(chip, 0x00, 0x00);

        /* Audio 1 DMA demand mode (4 bytes/request) */
        snd_es18xx_write(chip, 0xB9, 2);
	if (chip->caps & ES18XX_CONTROL) {
		/* Hardware volume IRQ */
		snd_es18xx_config_write(chip, 0x27, chip->irq);
		if (fm_port > 0 && fm_port != SNDRV_AUTO_PORT) {
			/* FM I/O */
			snd_es18xx_config_write(chip, 0x62, fm_port >> 8);
			snd_es18xx_config_write(chip, 0x63, fm_port & 0xff);
		}
		if (mpu_port > 0 && mpu_port != SNDRV_AUTO_PORT) {
			/* MPU-401 I/O */
			snd_es18xx_config_write(chip, 0x64, mpu_port >> 8);
			snd_es18xx_config_write(chip, 0x65, mpu_port & 0xff);
			/* MPU-401 IRQ */
			snd_es18xx_config_write(chip, 0x28, chip->irq);
		}
		/* Audio1 IRQ */
		snd_es18xx_config_write(chip, 0x70, chip->irq);
		/* Audio2 IRQ */
		snd_es18xx_config_write(chip, 0x72, chip->irq);
		/* Audio1 DMA */
		snd_es18xx_config_write(chip, 0x74, chip->dma1);
		/* Audio2 DMA */
		snd_es18xx_config_write(chip, 0x75, chip->dma2);

		/* Enable Audio 1 IRQ */
		snd_es18xx_write(chip, 0xB1, 0x50);
		/* Enable Audio 2 IRQ */
		snd_es18xx_mixer_write(chip, 0x7A, 0x40);
		/* Enable Audio 1 DMA */
		snd_es18xx_write(chip, 0xB2, 0x50);
		/* Enable MPU and hardware volume interrupt */
		snd_es18xx_mixer_write(chip, 0x64, 0x42);
		/* Enable ESS wavetable input */
		snd_es18xx_mixer_bits(chip, 0x48, 0x10, 0x10);
	}
	else {
		int irqmask, dma1mask, dma2mask;
		switch (chip->irq) {
		case 2:
		case 9:
			irqmask = 0;
			break;
		case 5:
			irqmask = 1;
			break;
		case 7:
			irqmask = 2;
			break;
		case 10:
			irqmask = 3;
			break;
		default:
			dev_err(chip->card->dev, "invalid irq %d\n", chip->irq);
			return -ENODEV;
		}
		switch (chip->dma1) {
		case 0:
			dma1mask = 1;
			break;
		case 1:
			dma1mask = 2;
			break;
		case 3:
			dma1mask = 3;
			break;
		default:
			dev_err(chip->card->dev, "invalid dma1 %d\n", chip->dma1);
			return -ENODEV;
		}
		switch (chip->dma2) {
		case 0:
			dma2mask = 0;
			break;
		case 1:
			dma2mask = 1;
			break;
		case 3:
			dma2mask = 2;
			break;
		case 5:
			dma2mask = 3;
			break;
		default:
			dev_err(chip->card->dev, "invalid dma2 %d\n", chip->dma2);
			return -ENODEV;
		}

		/* Enable and set Audio 1 IRQ */
		snd_es18xx_write(chip, 0xB1, 0x50 | (irqmask << 2));
		/* Enable and set Audio 1 DMA */
		snd_es18xx_write(chip, 0xB2, 0x50 | (dma1mask << 2));
		/* Set Audio 2 DMA */
		snd_es18xx_mixer_bits(chip, 0x7d, 0x07, 0x04 | dma2mask);
		/* Enable Audio 2 IRQ and DMA
		   Set capture mixer input */
		snd_es18xx_mixer_write(chip, 0x7A, 0x68);
		/* Enable and set hardware volume interrupt */
		snd_es18xx_mixer_write(chip, 0x64, 0x06);
		if (mpu_port > 0 && mpu_port != SNDRV_AUTO_PORT) {
			/* MPU401 share irq with audio
			   Joystick enabled
			   FM enabled */
			snd_es18xx_mixer_write(chip, 0x40,
					       0x43 | (mpu_port & 0xf0) >> 1);
		}
		snd_es18xx_mixer_write(chip, 0x7f, ((irqmask + 1) << 1) | 0x01);
	}
	if (chip->caps & ES18XX_NEW_RATE) {
		/* Change behaviour of register A1
		   4x oversampling
		   2nd channel DAC asynchronous */
		snd_es18xx_mixer_write(chip, 0x71, 0x32);
	}
	if (!(chip->caps & ES18XX_PCM2)) {
		/* Enable DMA FIFO */
		snd_es18xx_write(chip, 0xB7, 0x80);
	}
	if (chip->caps & ES18XX_SPATIALIZER) {
		/* Set spatializer parameters to recommended values */
		snd_es18xx_mixer_write(chip, 0x54, 0x8f);
		snd_es18xx_mixer_write(chip, 0x56, 0x95);
		snd_es18xx_mixer_write(chip, 0x58, 0x94);
		snd_es18xx_mixer_write(chip, 0x5a, 0x80);
	}
	/* Flip the "enable I2S" bits for those chipsets that need it */
	switch (chip->version) {
	case 0x1879:
		//Leaving I2S enabled on the 1879 screws up the PCM playback (rate effected somehow)
		//so a Switch control has been added to toggle this 0x71 bit on/off:
		//snd_es18xx_mixer_bits(chip, 0x71, 0x40, 0x40);
		/* Note: we fall through on purpose here. */
	case 0x1878:
		snd_es18xx_config_write(chip, 0x29, snd_es18xx_config_read(chip, 0x29) | 0x40);
		break;
	}
	/* Mute input source */
	if (chip->caps & ES18XX_MUTEREC)
		mask = 0x10;
	if (chip->caps & ES18XX_RECMIX)
		snd_es18xx_mixer_write(chip, 0x1c, 0x05 | mask);
	else {
		snd_es18xx_mixer_write(chip, 0x1c, 0x00 | mask);
		snd_es18xx_write(chip, 0xb4, 0x00);
	}
#ifndef AVOID_POPS
	/* Enable PCM output */
	snd_es18xx_dsp_command(chip, 0xD1);
#endif

        return 0;
}

static int snd_es18xx_identify(struct snd_card *card, struct snd_es18xx *chip)
{
	int hi,lo;

	/* reset */
	if (snd_es18xx_reset(chip) < 0) {
		dev_err(card->dev, "reset at 0x%lx failed!!!\n", chip->port);
		return -ENODEV;
	}

	snd_es18xx_dsp_command(chip, 0xe7);
	hi = snd_es18xx_dsp_get_byte(chip);
	if (hi < 0) {
		return hi;
	}
	lo = snd_es18xx_dsp_get_byte(chip);
	if ((lo & 0xf0) != 0x80) {
		return -ENODEV;
	}
	if (hi == 0x48) {
		chip->version = 0x488;
		return 0;
	}
	if (hi != 0x68) {
		return -ENODEV;
	}
	if ((lo & 0x0f) < 8) {
		chip->version = 0x688;
		return 0;
	}
			
        outb(0x40, chip->port + 0x04);
	udelay(10);
	hi = inb(chip->port + 0x05);
	udelay(10);
	lo = inb(chip->port + 0x05);
	if (hi != lo) {
		chip->version = hi << 8 | lo;
		chip->ctrl_port = inb(chip->port + 0x05) << 8;
		udelay(10);
		chip->ctrl_port += inb(chip->port + 0x05);

		if (!devm_request_region(card->dev, chip->ctrl_port, 8,
					 "ES18xx - CTRL")) {
			dev_err(card->dev, "unable go grab port 0x%lx\n", chip->ctrl_port);
			return -EBUSY;
		}

		return 0;
	}

	/* If has Hardware volume */
	if (snd_es18xx_mixer_writable(chip, 0x64, 0x04)) {
		/* If has Audio2 */
		if (snd_es18xx_mixer_writable(chip, 0x70, 0x7f)) {
			/* If has volume count */
			if (snd_es18xx_mixer_writable(chip, 0x64, 0x20)) {
				chip->version = 0x1887;
			} else {
				chip->version = 0x1888;
			}
		} else {
			chip->version = 0x1788;
		}
	}
	else
		chip->version = 0x1688;
	return 0;
}

static int snd_es18xx_probe(struct snd_card *card,
			    struct snd_es18xx *chip,
			    unsigned long mpu_port,
			    unsigned long fm_port)
{
	if (snd_es18xx_identify(card, chip) < 0) {
		dev_err(card->dev, "[0x%lx] ESS chip not found\n", chip->port);
                return -ENODEV;
	}

	switch (chip->version) {
	case 0x1868:
		chip->caps = ES18XX_DUPLEX_MONO | ES18XX_DUPLEX_SAME | ES18XX_CONTROL | ES18XX_GPO_2BIT;
		break;
	case 0x1869:
		chip->caps = ES18XX_PCM2 | ES18XX_SPATIALIZER | ES18XX_RECMIX | ES18XX_NEW_RATE | ES18XX_AUXB | ES18XX_MONO | ES18XX_MUTEREC | ES18XX_CONTROL | ES18XX_HWV | ES18XX_GPO_2BIT;
		break;
	case 0x1878:
		chip->caps = ES18XX_DUPLEX_MONO | ES18XX_DUPLEX_SAME | ES18XX_I2S | ES18XX_CONTROL;
		break;
	case 0x1879:
		chip->caps = ES18XX_PCM2 | ES18XX_SPATIALIZER | ES18XX_RECMIX | ES18XX_NEW_RATE | ES18XX_AUXB | ES18XX_I2S | ES18XX_CONTROL | ES18XX_HWV;
		break;
	case 0x1887:
	case 0x1888:
		chip->caps = ES18XX_PCM2 | ES18XX_RECMIX | ES18XX_AUXB | ES18XX_DUPLEX_SAME | ES18XX_GPO_2BIT;
		break;
	default:
		dev_err(card->dev, "[0x%lx] unsupported chip ES%x\n",
			chip->port, chip->version);
                return -ENODEV;
        }

	dev_dbg(card->dev, "[0x%lx] ESS%x chip found\n", chip->port, chip->version);

	if (chip->dma1 == chip->dma2)
		chip->caps &= ~(ES18XX_PCM2 | ES18XX_DUPLEX_SAME);

	return snd_es18xx_initialize(chip, mpu_port, fm_port);
}

static const struct snd_pcm_ops snd_es18xx_playback_ops = {
	.open =		snd_es18xx_playback_open,
	.close =	snd_es18xx_playback_close,
	.hw_params =	snd_es18xx_playback_hw_params,
	.prepare =	snd_es18xx_playback_prepare,
	.trigger =	snd_es18xx_playback_trigger,
	.pointer =	snd_es18xx_playback_pointer,
};

static const struct snd_pcm_ops snd_es18xx_capture_ops = {
	.open =		snd_es18xx_capture_open,
	.close =	snd_es18xx_capture_close,
	.hw_params =	snd_es18xx_capture_hw_params,
	.prepare =	snd_es18xx_capture_prepare,
	.trigger =	snd_es18xx_capture_trigger,
	.pointer =	snd_es18xx_capture_pointer,
};

static int snd_es18xx_pcm(struct snd_card *card, int device)
{
	struct snd_es18xx *chip = card->private_data;
        struct snd_pcm *pcm;
	char str[16];
	int err;

	sprintf(str, "ES%x", chip->version);
	if (chip->caps & ES18XX_PCM2)
		err = snd_pcm_new(card, str, device, 2, 1, &pcm);
	else
		err = snd_pcm_new(card, str, device, 1, 1, &pcm);
        if (err < 0)
                return err;

	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_es18xx_playback_ops);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_es18xx_capture_ops);

	/* global setup */
        pcm->private_data = chip;
        pcm->info_flags = 0;
	if (chip->caps & ES18XX_DUPLEX_SAME)
		pcm->info_flags |= SNDRV_PCM_INFO_JOINT_DUPLEX;
	if (! (chip->caps & ES18XX_PCM2))
		pcm->info_flags |= SNDRV_PCM_INFO_HALF_DUPLEX;
	sprintf(pcm->name, "ESS AudioDrive ES%x", chip->version);
        chip->pcm = pcm;

	snd_pcm_set_managed_buffer_all(pcm, SNDRV_DMA_TYPE_DEV, card->dev,
				       64*1024,
				       chip->dma1 > 3 || chip->dma2 > 3 ? 128*1024 : 64*1024);
	return 0;
}

/* Power Management support functions */
#ifdef CONFIG_PM
static int snd_es18xx_suspend(struct snd_card *card, pm_message_t state)
{
	struct snd_es18xx *chip = card->private_data;

	snd_power_change_state(card, SNDRV_CTL_POWER_D3hot);

	/* power down */
	chip->pm_reg = (unsigned char)snd_es18xx_read(chip, ES18XX_PM);
	chip->pm_reg |= (ES18XX_PM_FM | ES18XX_PM_SUS);
	snd_es18xx_write(chip, ES18XX_PM, chip->pm_reg);
	snd_es18xx_write(chip, ES18XX_PM, chip->pm_reg ^= ES18XX_PM_SUS);

	return 0;
}

static int snd_es18xx_resume(struct snd_card *card)
{
	struct snd_es18xx *chip = card->private_data;

	/* restore PM register, we won't wake till (not 0x07) i/o activity though */
	snd_es18xx_write(chip, ES18XX_PM, chip->pm_reg ^= ES18XX_PM_FM);

	snd_power_change_state(card, SNDRV_CTL_POWER_D0);
	return 0;
}
#endif /* CONFIG_PM */

static int snd_es18xx_new_device(struct snd_card *card,
				 unsigned long port,
				 unsigned long mpu_port,
				 unsigned long fm_port,
				 int irq, int dma1, int dma2)
{
	struct snd_es18xx *chip = card->private_data;

	chip->card = card;
	spin_lock_init(&chip->reg_lock);
 	spin_lock_init(&chip->mixer_lock);
        chip->port = port;
        chip->irq = -1;
        chip->dma1 = -1;
        chip->dma2 = -1;
        chip->audio2_vol = 0x00;
	chip->active = 0;

	if (!devm_request_region(card->dev, port, 16, "ES18xx")) {
		dev_err(card->dev, "unable to grab ports 0x%lx-0x%lx\n", port, port + 16 - 1);
		return -EBUSY;
	}

	if (devm_request_irq(card->dev, irq, snd_es18xx_interrupt, 0, "ES18xx",
			     (void *) card)) {
		dev_err(card->dev, "unable to grab IRQ %d\n", irq);
		return -EBUSY;
	}
	chip->irq = irq;
	card->sync_irq = chip->irq;

	if (snd_devm_request_dma(card->dev, dma1, "ES18xx DMA 1")) {
		dev_err(card->dev, "unable to grab DMA1 %d\n", dma1);
		return -EBUSY;
	}
	chip->dma1 = dma1;

	if (dma2 != dma1 &&
	    snd_devm_request_dma(card->dev, dma2, "ES18xx DMA 2")) {
		dev_err(card->dev, "unable to grab DMA2 %d\n", dma2);
		return -EBUSY;
	}
	chip->dma2 = dma2;

	if (snd_es18xx_probe(card, chip, mpu_port, fm_port) < 0)
		return -ENODEV;
        return 0;
}

static int snd_es18xx_mixer(struct snd_card *card)
{
	struct snd_es18xx *chip = card->private_data;
	int err;
	unsigned int idx;

	strscpy(card->mixername, chip->pcm->name);

	for (idx = 0; idx < ARRAY_SIZE(snd_es18xx_base_controls); idx++) {
		struct snd_kcontrol *kctl;
		kctl = snd_ctl_new1(&snd_es18xx_base_controls[idx], chip);
		if (chip->caps & ES18XX_HWV) {
			switch (idx) {
			case 0:
				chip->master_volume = kctl;
				kctl->private_free = snd_es18xx_hwv_free;
				break;
			case 1:
				chip->master_switch = kctl;
				kctl->private_free = snd_es18xx_hwv_free;
				break;
			}
		}
		err = snd_ctl_add(card, kctl);
		if (err < 0)
			return err;
	}
	if (chip->caps & ES18XX_PCM2) {
		for (idx = 0; idx < ARRAY_SIZE(snd_es18xx_pcm2_controls); idx++) {
			err = snd_ctl_add(card, snd_ctl_new1(&snd_es18xx_pcm2_controls[idx], chip));
			if (err < 0)
				return err;
		} 
	} else {
		for (idx = 0; idx < ARRAY_SIZE(snd_es18xx_pcm1_controls); idx++) {
			err = snd_ctl_add(card, snd_ctl_new1(&snd_es18xx_pcm1_controls[idx], chip));
			if (err < 0)
				return err;
		}
	}

	if (chip->caps & ES18XX_RECMIX) {
		for (idx = 0; idx < ARRAY_SIZE(snd_es18xx_recmix_controls); idx++) {
			err = snd_ctl_add(card, snd_ctl_new1(&snd_es18xx_recmix_controls[idx], chip));
			if (err < 0)
				return err;
		}
	}
	switch (chip->version) {
	default:
		err = snd_ctl_add(card, snd_ctl_new1(&snd_es18xx_micpre1_control, chip));
		if (err < 0)
			return err;
		break;
	case 0x1869:
	case 0x1879:
		err = snd_ctl_add(card, snd_ctl_new1(&snd_es18xx_micpre2_control, chip));
		if (err < 0)
			return err;
		break;
	}
	if (chip->caps & ES18XX_SPATIALIZER) {
		for (idx = 0; idx < ARRAY_SIZE(snd_es18xx_spatializer_controls); idx++) {
			err = snd_ctl_add(card, snd_ctl_new1(&snd_es18xx_spatializer_controls[idx], chip));
			if (err < 0)
				return err;
		}
	}
	if (chip->caps & ES18XX_HWV) {
		for (idx = 0; idx < ARRAY_SIZE(snd_es18xx_hw_volume_controls); idx++) {
			struct snd_kcontrol *kctl;
			kctl = snd_ctl_new1(&snd_es18xx_hw_volume_controls[idx], chip);
			if (idx == 0)
				chip->hw_volume = kctl;
			else
				chip->hw_switch = kctl;
			kctl->private_free = snd_es18xx_hwv_free;
			err = snd_ctl_add(card, kctl);
			if (err < 0)
				return err;
			
		}
	}
	/* finish initializing other chipset specific controls
	 */
	if (chip->version != 0x1868) {
		err = snd_ctl_add(card, snd_ctl_new1(&snd_es18xx_opt_speaker,
						     chip));
		if (err < 0)
			return err;
	}
	if (chip->version == 0x1869) {
		for (idx = 0; idx < ARRAY_SIZE(snd_es18xx_opt_1869); idx++) {
			err = snd_ctl_add(card,
					  snd_ctl_new1(&snd_es18xx_opt_1869[idx],
						       chip));
			if (err < 0)
				return err;
		}
	} else if (chip->version == 0x1878) {
		err = snd_ctl_add(card, snd_ctl_new1(&snd_es18xx_opt_1878,
						     chip));
		if (err < 0)
			return err;
	} else if (chip->version == 0x1879) {
		for (idx = 0; idx < ARRAY_SIZE(snd_es18xx_opt_1879); idx++) {
			err = snd_ctl_add(card,
					  snd_ctl_new1(&snd_es18xx_opt_1879[idx],
						       chip));
			if (err < 0)
				return err;
		}
	}
	if (chip->caps & ES18XX_GPO_2BIT) {
		for (idx = 0; idx < ARRAY_SIZE(snd_es18xx_opt_gpo_2bit); idx++) {
			err = snd_ctl_add(card,
					  snd_ctl_new1(&snd_es18xx_opt_gpo_2bit[idx],
						       chip));
			if (err < 0)
				return err;
		}
	}
	return 0;
}
       

/* Card level */

MODULE_AUTHOR("Christian Fischbach <fishbach@pool.informatik.rwth-aachen.de>, Abramo Bagnara <abramo@alsa-project.org>");
MODULE_DESCRIPTION("ESS ES18xx AudioDrive");
MODULE_LICENSE("GPL");

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-MAX */
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
static bool enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE_ISAPNP; /* Enable this card */
#ifdef CONFIG_PNP
static bool isapnp[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE_ISAPNP;
#endif
static long port[SNDRV_CARDS] = SNDRV_DEFAULT_PORT;	/* 0x220,0x240,0x260,0x280 */
#ifndef CONFIG_PNP
static long mpu_port[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS - 1)] = -1};
#else
static long mpu_port[SNDRV_CARDS] = SNDRV_DEFAULT_PORT;
#endif
static long fm_port[SNDRV_CARDS] = SNDRV_DEFAULT_PORT;
static int irq[SNDRV_CARDS] = SNDRV_DEFAULT_IRQ;	/* 5,7,9,10 */
static int dma1[SNDRV_CARDS] = SNDRV_DEFAULT_DMA;	/* 0,1,3 */
static int dma2[SNDRV_CARDS] = SNDRV_DEFAULT_DMA;	/* 0,1,3 */

module_param_array(index, int, NULL, 0444);
MODULE_PARM_DESC(index, "Index value for ES18xx soundcard.");
module_param_array(id, charp, NULL, 0444);
MODULE_PARM_DESC(id, "ID string for ES18xx soundcard.");
module_param_array(enable, bool, NULL, 0444);
MODULE_PARM_DESC(enable, "Enable ES18xx soundcard.");
#ifdef CONFIG_PNP
module_param_array(isapnp, bool, NULL, 0444);
MODULE_PARM_DESC(isapnp, "PnP detection for specified soundcard.");
#endif
module_param_hw_array(port, long, ioport, NULL, 0444);
MODULE_PARM_DESC(port, "Port # for ES18xx driver.");
module_param_hw_array(mpu_port, long, ioport, NULL, 0444);
MODULE_PARM_DESC(mpu_port, "MPU-401 port # for ES18xx driver.");
module_param_hw_array(fm_port, long, ioport, NULL, 0444);
MODULE_PARM_DESC(fm_port, "FM port # for ES18xx driver.");
module_param_hw_array(irq, int, irq, NULL, 0444);
MODULE_PARM_DESC(irq, "IRQ # for ES18xx driver.");
module_param_hw_array(dma1, int, dma, NULL, 0444);
MODULE_PARM_DESC(dma1, "DMA 1 # for ES18xx driver.");
module_param_hw_array(dma2, int, dma, NULL, 0444);
MODULE_PARM_DESC(dma2, "DMA 2 # for ES18xx driver.");

#ifdef CONFIG_PNP
static int isa_registered;
static int pnp_registered;
static int pnpc_registered;

static const struct pnp_device_id snd_audiodrive_pnpbiosids[] = {
	{ .id = "ESS1869" },
	{ .id = "ESS1879" },
	{ .id = "" }		/* end */
};

MODULE_DEVICE_TABLE(pnp, snd_audiodrive_pnpbiosids);

/* PnP main device initialization */
static int snd_audiodrive_pnp_init_main(int dev, struct pnp_dev *pdev)
{
	if (pnp_activate_dev(pdev) < 0) {
		dev_err(&pdev->dev, "PnP configure failure (out of resources?)\n");
		return -EBUSY;
	}
	/* ok. hack using Vendor-Defined Card-Level registers */
	/* skip csn and logdev initialization - already done in isapnp_configure */
	if (pnp_device_is_isapnp(pdev)) {
		isapnp_cfg_begin(isapnp_card_number(pdev), isapnp_csn_number(pdev));
		isapnp_write_byte(0x27, pnp_irq(pdev, 0));	/* Hardware Volume IRQ Number */
		if (mpu_port[dev] != SNDRV_AUTO_PORT)
			isapnp_write_byte(0x28, pnp_irq(pdev, 0)); /* MPU-401 IRQ Number */
		isapnp_write_byte(0x72, pnp_irq(pdev, 0));	/* second IRQ */
		isapnp_cfg_end();
	}
	port[dev] = pnp_port_start(pdev, 0);
	fm_port[dev] = pnp_port_start(pdev, 1);
	mpu_port[dev] = pnp_port_start(pdev, 2);
	dma1[dev] = pnp_dma(pdev, 0);
	dma2[dev] = pnp_dma(pdev, 1);
	irq[dev] = pnp_irq(pdev, 0);
	dev_dbg(&pdev->dev,
		"PnP ES18xx: port=0x%lx, fm port=0x%lx, mpu port=0x%lx\n",
		port[dev], fm_port[dev], mpu_port[dev]);
	dev_dbg(&pdev->dev,
		"PnP ES18xx: dma1=%i, dma2=%i, irq=%i\n",
		dma1[dev], dma2[dev], irq[dev]);
	return 0;
}

static int snd_audiodrive_pnp(int dev, struct snd_es18xx *chip,
			      struct pnp_dev *pdev)
{
	chip->dev = pdev;
	if (snd_audiodrive_pnp_init_main(dev, chip->dev) < 0)
		return -EBUSY;
	return 0;
}

static const struct pnp_card_device_id snd_audiodrive_pnpids[] = {
	/* ESS 1868 (integrated on Compaq dual P-Pro motherboard and Genius 18PnP 3D) */
	{ .id = "ESS1868", .devs = { { "ESS1868" }, { "ESS0000" } } },
	/* ESS 1868 (integrated on Maxisound Cards) */
	{ .id = "ESS1868", .devs = { { "ESS8601" }, { "ESS8600" } } },
	/* ESS 1868 (integrated on Maxisound Cards) */
	{ .id = "ESS1868", .devs = { { "ESS8611" }, { "ESS8610" } } },
	/* ESS ES1869 Plug and Play AudioDrive */
	{ .id = "ESS0003", .devs = { { "ESS1869" }, { "ESS0006" } } },
	/* ESS 1869 */
	{ .id = "ESS1869", .devs = { { "ESS1869" }, { "ESS0006" } } },
	/* ESS 1878 */
	{ .id = "ESS1878", .devs = { { "ESS1878" }, { "ESS0004" } } },
	/* ESS 1879 */
	{ .id = "ESS1879", .devs = { { "ESS1879" }, { "ESS0009" } } },
	/* --- */
	{ .id = "" } /* end */
};

MODULE_DEVICE_TABLE(pnp_card, snd_audiodrive_pnpids);

static int snd_audiodrive_pnpc(int dev, struct snd_es18xx *chip,
			       struct pnp_card_link *card,
			       const struct pnp_card_device_id *id)
{
	chip->dev = pnp_request_card_device(card, id->devs[0].id, NULL);
	if (chip->dev == NULL)
		return -EBUSY;

	chip->devc = pnp_request_card_device(card, id->devs[1].id, NULL);
	if (chip->devc == NULL)
		return -EBUSY;

	/* Control port initialization */
	if (pnp_activate_dev(chip->devc) < 0) {
		dev_err(chip->card->dev,
			"PnP control configure failure (out of resources?)\n");
		return -EAGAIN;
	}
	dev_dbg(chip->card->dev, "pnp: port=0x%llx\n",
		(unsigned long long)pnp_port_start(chip->devc, 0));
	if (snd_audiodrive_pnp_init_main(dev, chip->dev) < 0)
		return -EBUSY;

	return 0;
}
#endif /* CONFIG_PNP */

#ifdef CONFIG_PNP
#define is_isapnp_selected(dev)		isapnp[dev]
#else
#define is_isapnp_selected(dev)		0
#endif

static int snd_es18xx_card_new(struct device *pdev, int dev,
			       struct snd_card **cardp)
{
	return snd_devm_card_new(pdev, index[dev], id[dev], THIS_MODULE,
				 sizeof(struct snd_es18xx), cardp);
}

static int snd_audiodrive_probe(struct snd_card *card, int dev)
{
	struct snd_es18xx *chip = card->private_data;
	struct snd_opl3 *opl3;
	int err;

	err = snd_es18xx_new_device(card,
				    port[dev], mpu_port[dev], fm_port[dev],
				    irq[dev], dma1[dev], dma2[dev]);
	if (err < 0)
		return err;

	sprintf(card->driver, "ES%x", chip->version);
	
	sprintf(card->shortname, "ESS AudioDrive ES%x", chip->version);
	if (dma1[dev] != dma2[dev])
		sprintf(card->longname, "%s at 0x%lx, irq %d, dma1 %d, dma2 %d",
			card->shortname,
			chip->port,
			irq[dev], dma1[dev], dma2[dev]);
	else
		sprintf(card->longname, "%s at 0x%lx, irq %d, dma %d",
			card->shortname,
			chip->port,
			irq[dev], dma1[dev]);

	err = snd_es18xx_pcm(card, 0);
	if (err < 0)
		return err;

	err = snd_es18xx_mixer(card);
	if (err < 0)
		return err;

	if (fm_port[dev] > 0 && fm_port[dev] != SNDRV_AUTO_PORT) {
		if (snd_opl3_create(card, fm_port[dev], fm_port[dev] + 2,
				    OPL3_HW_OPL3, 0, &opl3) < 0) {
			dev_warn(card->dev,
				 "opl3 not detected at 0x%lx\n",
				 fm_port[dev]);
		} else {
			err = snd_opl3_hwdep_new(opl3, 0, 1, NULL);
			if (err < 0)
				return err;
		}
	}

	if (mpu_port[dev] > 0 && mpu_port[dev] != SNDRV_AUTO_PORT) {
		err = snd_mpu401_uart_new(card, 0, MPU401_HW_ES18XX,
					  mpu_port[dev], MPU401_INFO_IRQ_HOOK,
					  -1, &chip->rmidi);
		if (err < 0)
			return err;
	}

	return snd_card_register(card);
}

static int snd_es18xx_isa_match(struct device *pdev, unsigned int dev)
{
	return enable[dev] && !is_isapnp_selected(dev);
}

static int snd_es18xx_isa_probe1(int dev, struct device *devptr)
{
	struct snd_card *card;
	int err;

	err = snd_es18xx_card_new(devptr, dev, &card);
	if (err < 0)
		return err;
	err = snd_audiodrive_probe(card, dev);
	if (err < 0)
		return err;
	dev_set_drvdata(devptr, card);
	return 0;
}

static int snd_es18xx_isa_probe(struct device *pdev, unsigned int dev)
{
	int err;
	static const int possible_irqs[] = {5, 9, 10, 7, 11, 12, -1};
	static const int possible_dmas[] = {1, 0, 3, 5, -1};

	if (irq[dev] == SNDRV_AUTO_IRQ) {
		irq[dev] = snd_legacy_find_free_irq(possible_irqs);
		if (irq[dev] < 0) {
			dev_err(pdev, "unable to find a free IRQ\n");
			return -EBUSY;
		}
	}
	if (dma1[dev] == SNDRV_AUTO_DMA) {
		dma1[dev] = snd_legacy_find_free_dma(possible_dmas);
		if (dma1[dev] < 0) {
			dev_err(pdev, "unable to find a free DMA1\n");
			return -EBUSY;
		}
	}
	if (dma2[dev] == SNDRV_AUTO_DMA) {
		dma2[dev] = snd_legacy_find_free_dma(possible_dmas);
		if (dma2[dev] < 0) {
			dev_err(pdev, "unable to find a free DMA2\n");
			return -EBUSY;
		}
	}

	if (port[dev] != SNDRV_AUTO_PORT) {
		return snd_es18xx_isa_probe1(dev, pdev);
	} else {
		static const unsigned long possible_ports[] = {0x220, 0x240, 0x260, 0x280};
		int i;
		for (i = 0; i < ARRAY_SIZE(possible_ports); i++) {
			port[dev] = possible_ports[i];
			err = snd_es18xx_isa_probe1(dev, pdev);
			if (! err)
				return 0;
		}
		return err;
	}
}

#ifdef CONFIG_PM
static int snd_es18xx_isa_suspend(struct device *dev, unsigned int n,
				  pm_message_t state)
{
	return snd_es18xx_suspend(dev_get_drvdata(dev), state);
}

static int snd_es18xx_isa_resume(struct device *dev, unsigned int n)
{
	return snd_es18xx_resume(dev_get_drvdata(dev));
}
#endif

#define DEV_NAME "es18xx"

static struct isa_driver snd_es18xx_isa_driver = {
	.match		= snd_es18xx_isa_match,
	.probe		= snd_es18xx_isa_probe,
#ifdef CONFIG_PM
	.suspend	= snd_es18xx_isa_suspend,
	.resume		= snd_es18xx_isa_resume,
#endif
	.driver		= {
		.name	= DEV_NAME
	},
};


#ifdef CONFIG_PNP
static int snd_audiodrive_pnp_detect(struct pnp_dev *pdev,
				     const struct pnp_device_id *id)
{
	static int dev;
	int err;
	struct snd_card *card;

	if (pnp_device_is_isapnp(pdev))
		return -ENOENT;	/* we have another procedure - card */
	for (; dev < SNDRV_CARDS; dev++) {
		if (enable[dev] && isapnp[dev])
			break;
	}
	if (dev >= SNDRV_CARDS)
		return -ENODEV;

	err = snd_es18xx_card_new(&pdev->dev, dev, &card);
	if (err < 0)
		return err;
	err = snd_audiodrive_pnp(dev, card->private_data, pdev);
	if (err < 0)
		return err;
	err = snd_audiodrive_probe(card, dev);
	if (err < 0)
		return err;
	pnp_set_drvdata(pdev, card);
	dev++;
	return 0;
}

#ifdef CONFIG_PM
static int snd_audiodrive_pnp_suspend(struct pnp_dev *pdev, pm_message_t state)
{
	return snd_es18xx_suspend(pnp_get_drvdata(pdev), state);
}
static int snd_audiodrive_pnp_resume(struct pnp_dev *pdev)
{
	return snd_es18xx_resume(pnp_get_drvdata(pdev));
}
#endif

static struct pnp_driver es18xx_pnp_driver = {
	.name = "es18xx-pnpbios",
	.id_table = snd_audiodrive_pnpbiosids,
	.probe = snd_audiodrive_pnp_detect,
#ifdef CONFIG_PM
	.suspend = snd_audiodrive_pnp_suspend,
	.resume = snd_audiodrive_pnp_resume,
#endif
};

static int snd_audiodrive_pnpc_detect(struct pnp_card_link *pcard,
				      const struct pnp_card_device_id *pid)
{
	static int dev;
	struct snd_card *card;
	int res;

	for ( ; dev < SNDRV_CARDS; dev++) {
		if (enable[dev] && isapnp[dev])
			break;
	}
	if (dev >= SNDRV_CARDS)
		return -ENODEV;

	res = snd_es18xx_card_new(&pcard->card->dev, dev, &card);
	if (res < 0)
		return res;

	res = snd_audiodrive_pnpc(dev, card->private_data, pcard, pid);
	if (res < 0)
		return res;
	res = snd_audiodrive_probe(card, dev);
	if (res < 0)
		return res;

	pnp_set_card_drvdata(pcard, card);
	dev++;
	return 0;
}

#ifdef CONFIG_PM
static int snd_audiodrive_pnpc_suspend(struct pnp_card_link *pcard, pm_message_t state)
{
	return snd_es18xx_suspend(pnp_get_card_drvdata(pcard), state);
}

static int snd_audiodrive_pnpc_resume(struct pnp_card_link *pcard)
{
	return snd_es18xx_resume(pnp_get_card_drvdata(pcard));
}

#endif

static struct pnp_card_driver es18xx_pnpc_driver = {
	.flags = PNP_DRIVER_RES_DISABLE,
	.name = "es18xx",
	.id_table = snd_audiodrive_pnpids,
	.probe = snd_audiodrive_pnpc_detect,
#ifdef CONFIG_PM
	.suspend	= snd_audiodrive_pnpc_suspend,
	.resume		= snd_audiodrive_pnpc_resume,
#endif
};
#endif /* CONFIG_PNP */

static int __init alsa_card_es18xx_init(void)
{
	int err;

	err = isa_register_driver(&snd_es18xx_isa_driver, SNDRV_CARDS);
#ifdef CONFIG_PNP
	if (!err)
		isa_registered = 1;

	err = pnp_register_driver(&es18xx_pnp_driver);
	if (!err)
		pnp_registered = 1;

	err = pnp_register_card_driver(&es18xx_pnpc_driver);
	if (!err)
		pnpc_registered = 1;

	if (isa_registered || pnp_registered)
		err = 0;
#endif
	return err;
}

static void __exit alsa_card_es18xx_exit(void)
{
#ifdef CONFIG_PNP
	if (pnpc_registered)
		pnp_unregister_card_driver(&es18xx_pnpc_driver);
	if (pnp_registered)
		pnp_unregister_driver(&es18xx_pnp_driver);
	if (isa_registered)
#endif
		isa_unregister_driver(&snd_es18xx_isa_driver);
}

module_init(alsa_card_es18xx_init)
module_exit(alsa_card_es18xx_exit)
