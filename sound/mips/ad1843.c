// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *   AD1843 low level driver
 *
 *   Copyright 2003 Vivien Chappelier <vivien.chappelier@linux-mips.org>
 *   Copyright 2008 Thomas Bogendoerfer <tsbogend@alpha.franken.de>
 *
 *   inspired from vwsnd.c (SGI VW audio driver)
 *     Copyright 1999 Silicon Graphics, Inc.  All rights reserved.
 */

#include <linux/init.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/ad1843.h>

/*
 * AD1843 bitfield definitions.  All are named as in the AD1843 data
 * sheet, with ad1843_ prepended and individual bit numbers removed.
 *
 * E.g., bits LSS0 through LSS2 become ad1843_LSS.
 *
 * Only the bitfields we need are defined.
 */

struct ad1843_bitfield {
	char reg;
	char lo_bit;
	char nbits;
};

static const struct ad1843_bitfield
	ad1843_PDNO   = {  0, 14,  1 },	/* Converter Power-Down Flag */
	ad1843_INIT   = {  0, 15,  1 },	/* Clock Initialization Flag */
	ad1843_RIG    = {  2,  0,  4 },	/* Right ADC Input Gain */
	ad1843_RMGE   = {  2,  4,  1 },	/* Right ADC Mic Gain Enable */
	ad1843_RSS    = {  2,  5,  3 },	/* Right ADC Source Select */
	ad1843_LIG    = {  2,  8,  4 },	/* Left ADC Input Gain */
	ad1843_LMGE   = {  2, 12,  1 },	/* Left ADC Mic Gain Enable */
	ad1843_LSS    = {  2, 13,  3 },	/* Left ADC Source Select */
	ad1843_RD2M   = {  3,  0,  5 },	/* Right DAC 2 Mix Gain/Atten */
	ad1843_RD2MM  = {  3,  7,  1 },	/* Right DAC 2 Mix Mute */
	ad1843_LD2M   = {  3,  8,  5 },	/* Left DAC 2 Mix Gain/Atten */
	ad1843_LD2MM  = {  3, 15,  1 },	/* Left DAC 2 Mix Mute */
	ad1843_RX1M   = {  4,  0,  5 },	/* Right Aux 1 Mix Gain/Atten */
	ad1843_RX1MM  = {  4,  7,  1 },	/* Right Aux 1 Mix Mute */
	ad1843_LX1M   = {  4,  8,  5 },	/* Left Aux 1 Mix Gain/Atten */
	ad1843_LX1MM  = {  4, 15,  1 },	/* Left Aux 1 Mix Mute */
	ad1843_RX2M   = {  5,  0,  5 },	/* Right Aux 2 Mix Gain/Atten */
	ad1843_RX2MM  = {  5,  7,  1 },	/* Right Aux 2 Mix Mute */
	ad1843_LX2M   = {  5,  8,  5 },	/* Left Aux 2 Mix Gain/Atten */
	ad1843_LX2MM  = {  5, 15,  1 },	/* Left Aux 2 Mix Mute */
	ad1843_RMCM   = {  7,  0,  5 },	/* Right Mic Mix Gain/Atten */
	ad1843_RMCMM  = {  7,  7,  1 },	/* Right Mic Mix Mute */
	ad1843_LMCM   = {  7,  8,  5 },	/* Left Mic Mix Gain/Atten */
	ad1843_LMCMM  = {  7, 15,  1 },	/* Left Mic Mix Mute */
	ad1843_HPOS   = {  8,  4,  1 },	/* Headphone Output Voltage Swing */
	ad1843_HPOM   = {  8,  5,  1 },	/* Headphone Output Mute */
	ad1843_MPOM   = {  8,  6,  1 },	/* Mono Output Mute */
	ad1843_RDA1G  = {  9,  0,  6 },	/* Right DAC1 Analog/Digital Gain */
	ad1843_RDA1GM = {  9,  7,  1 },	/* Right DAC1 Analog Mute */
	ad1843_LDA1G  = {  9,  8,  6 },	/* Left DAC1 Analog/Digital Gain */
	ad1843_LDA1GM = {  9, 15,  1 },	/* Left DAC1 Analog Mute */
	ad1843_RDA2G  = { 10,  0,  6 },	/* Right DAC2 Analog/Digital Gain */
	ad1843_RDA2GM = { 10,  7,  1 },	/* Right DAC2 Analog Mute */
	ad1843_LDA2G  = { 10,  8,  6 },	/* Left DAC2 Analog/Digital Gain */
	ad1843_LDA2GM = { 10, 15,  1 },	/* Left DAC2 Analog Mute */
	ad1843_RDA1AM = { 11,  7,  1 },	/* Right DAC1 Digital Mute */
	ad1843_LDA1AM = { 11, 15,  1 },	/* Left DAC1 Digital Mute */
	ad1843_RDA2AM = { 12,  7,  1 },	/* Right DAC2 Digital Mute */
	ad1843_LDA2AM = { 12, 15,  1 },	/* Left DAC2 Digital Mute */
	ad1843_ADLC   = { 15,  0,  2 },	/* ADC Left Sample Rate Source */
	ad1843_ADRC   = { 15,  2,  2 },	/* ADC Right Sample Rate Source */
	ad1843_DA1C   = { 15,  8,  2 },	/* DAC1 Sample Rate Source */
	ad1843_DA2C   = { 15, 10,  2 },	/* DAC2 Sample Rate Source */
	ad1843_C1C    = { 17,  0, 16 },	/* Clock 1 Sample Rate Select */
	ad1843_C2C    = { 20,  0, 16 },	/* Clock 2 Sample Rate Select */
	ad1843_C3C    = { 23,  0, 16 },	/* Clock 3 Sample Rate Select */
	ad1843_DAADL  = { 25,  4,  2 },	/* Digital ADC Left Source Select */
	ad1843_DAADR  = { 25,  6,  2 },	/* Digital ADC Right Source Select */
	ad1843_DAMIX  = { 25, 14,  1 },	/* DAC Digital Mix Enable */
	ad1843_DRSFLT = { 25, 15,  1 },	/* Digital Reampler Filter Mode */
	ad1843_ADLF   = { 26,  0,  2 }, /* ADC Left Channel Data Format */
	ad1843_ADRF   = { 26,  2,  2 }, /* ADC Right Channel Data Format */
	ad1843_ADTLK  = { 26,  4,  1 },	/* ADC Transmit Lock Mode Select */
	ad1843_SCF    = { 26,  7,  1 },	/* SCLK Frequency Select */
	ad1843_DA1F   = { 26,  8,  2 },	/* DAC1 Data Format Select */
	ad1843_DA2F   = { 26, 10,  2 },	/* DAC2 Data Format Select */
	ad1843_DA1SM  = { 26, 14,  1 },	/* DAC1 Stereo/Mono Mode Select */
	ad1843_DA2SM  = { 26, 15,  1 },	/* DAC2 Stereo/Mono Mode Select */
	ad1843_ADLEN  = { 27,  0,  1 },	/* ADC Left Channel Enable */
	ad1843_ADREN  = { 27,  1,  1 },	/* ADC Right Channel Enable */
	ad1843_AAMEN  = { 27,  4,  1 },	/* Analog to Analog Mix Enable */
	ad1843_ANAEN  = { 27,  7,  1 },	/* Analog Channel Enable */
	ad1843_DA1EN  = { 27,  8,  1 },	/* DAC1 Enable */
	ad1843_DA2EN  = { 27,  9,  1 },	/* DAC2 Enable */
	ad1843_DDMEN  = { 27, 12,  1 },	/* DAC2 to DAC1 Mix  Enable */
	ad1843_C1EN   = { 28, 11,  1 },	/* Clock Generator 1 Enable */
	ad1843_C2EN   = { 28, 12,  1 },	/* Clock Generator 2 Enable */
	ad1843_C3EN   = { 28, 13,  1 },	/* Clock Generator 3 Enable */
	ad1843_PDNI   = { 28, 15,  1 };	/* Converter Power Down */

/*
 * The various registers of the AD1843 use three different formats for
 * specifying gain.  The ad1843_gain structure parameterizes the
 * formats.
 */

struct ad1843_gain {
	int	negative;		/* nonzero if gain is negative. */
	const struct ad1843_bitfield *lfield;
	const struct ad1843_bitfield *rfield;
	const struct ad1843_bitfield *lmute;
	const struct ad1843_bitfield *rmute;
};

static const struct ad1843_gain ad1843_gain_RECLEV = {
	.negative = 0,
	.lfield   = &ad1843_LIG,
	.rfield   = &ad1843_RIG
};
static const struct ad1843_gain ad1843_gain_LINE = {
	.negative = 1,
	.lfield   = &ad1843_LX1M,
	.rfield   = &ad1843_RX1M,
	.lmute    = &ad1843_LX1MM,
	.rmute    = &ad1843_RX1MM
};
static const struct ad1843_gain ad1843_gain_LINE_2 = {
	.negative = 1,
	.lfield   = &ad1843_LDA2G,
	.rfield   = &ad1843_RDA2G,
	.lmute    = &ad1843_LDA2GM,
	.rmute    = &ad1843_RDA2GM
};
static const struct ad1843_gain ad1843_gain_MIC = {
	.negative = 1,
	.lfield   = &ad1843_LMCM,
	.rfield   = &ad1843_RMCM,
	.lmute    = &ad1843_LMCMM,
	.rmute    = &ad1843_RMCMM
};
static const struct ad1843_gain ad1843_gain_PCM_0 = {
	.negative = 1,
	.lfield   = &ad1843_LDA1G,
	.rfield   = &ad1843_RDA1G,
	.lmute    = &ad1843_LDA1GM,
	.rmute    = &ad1843_RDA1GM
};
static const struct ad1843_gain ad1843_gain_PCM_1 = {
	.negative = 1,
	.lfield   = &ad1843_LD2M,
	.rfield   = &ad1843_RD2M,
	.lmute    = &ad1843_LD2MM,
	.rmute    = &ad1843_RD2MM
};

static const struct ad1843_gain *ad1843_gain[AD1843_GAIN_SIZE] =
{
	&ad1843_gain_RECLEV,
	&ad1843_gain_LINE,
	&ad1843_gain_LINE_2,
	&ad1843_gain_MIC,
	&ad1843_gain_PCM_0,
	&ad1843_gain_PCM_1,
};

/* read the current value of an AD1843 bitfield. */

static int ad1843_read_bits(struct snd_ad1843 *ad1843,
			    const struct ad1843_bitfield *field)
{
	int w;

	w = ad1843->read(ad1843->chip, field->reg);
	return w >> field->lo_bit & ((1 << field->nbits) - 1);
}

/*
 * write a new value to an AD1843 bitfield and return the old value.
 */

static int ad1843_write_bits(struct snd_ad1843 *ad1843,
			     const struct ad1843_bitfield *field,
			     int newval)
{
	int w, mask, oldval, newbits;

	w = ad1843->read(ad1843->chip, field->reg);
	mask = ((1 << field->nbits) - 1) << field->lo_bit;
	oldval = (w & mask) >> field->lo_bit;
	newbits = (newval << field->lo_bit) & mask;
	w = (w & ~mask) | newbits;
	ad1843->write(ad1843->chip, field->reg, w);

	return oldval;
}

/*
 * ad1843_read_multi reads multiple bitfields from the same AD1843
 * register.  It uses a single read cycle to do it.  (Reading the
 * ad1843 requires 256 bit times at 12.288 MHz, or nearly 20
 * microseconds.)
 *
 * Called like this.
 *
 *  ad1843_read_multi(ad1843, nfields,
 *		      &ad1843_FIELD1, &val1,
 *		      &ad1843_FIELD2, &val2, ...);
 */

static void ad1843_read_multi(struct snd_ad1843 *ad1843, int argcount, ...)
{
	va_list ap;
	const struct ad1843_bitfield *fp;
	int w = 0, mask, *value, reg = -1;

	va_start(ap, argcount);
	while (--argcount >= 0) {
		fp = va_arg(ap, const struct ad1843_bitfield *);
		value = va_arg(ap, int *);
		if (reg == -1) {
			reg = fp->reg;
			w = ad1843->read(ad1843->chip, reg);
		}

		mask = (1 << fp->nbits) - 1;
		*value = w >> fp->lo_bit & mask;
	}
	va_end(ap);
}

/*
 * ad1843_write_multi stores multiple bitfields into the same AD1843
 * register.  It uses one read and one write cycle to do it.
 *
 * Called like this.
 *
 *  ad1843_write_multi(ad1843, nfields,
 *		       &ad1843_FIELD1, val1,
 *		       &ad1843_FIELF2, val2, ...);
 */

static void ad1843_write_multi(struct snd_ad1843 *ad1843, int argcount, ...)
{
	va_list ap;
	int reg;
	const struct ad1843_bitfield *fp;
	int value;
	int w, m, mask, bits;

	mask = 0;
	bits = 0;
	reg = -1;

	va_start(ap, argcount);
	while (--argcount >= 0) {
		fp = va_arg(ap, const struct ad1843_bitfield *);
		value = va_arg(ap, int);
		if (reg == -1)
			reg = fp->reg;
		else
			WARN_ON(reg != fp->reg);
		m = ((1 << fp->nbits) - 1) << fp->lo_bit;
		mask |= m;
		bits |= (value << fp->lo_bit) & m;
	}
	va_end(ap);

	if (~mask & 0xFFFF)
		w = ad1843->read(ad1843->chip, reg);
	else
		w = 0;
	w = (w & ~mask) | bits;
	ad1843->write(ad1843->chip, reg, w);
}

int ad1843_get_gain_max(struct snd_ad1843 *ad1843, int id)
{
	const struct ad1843_gain *gp = ad1843_gain[id];
	int ret;

	ret = (1 << gp->lfield->nbits);
	if (!gp->lmute)
		ret -= 1;
	return ret;
}

/*
 * ad1843_get_gain reads the specified register and extracts the gain value
 * using the supplied gain type.
 */

int ad1843_get_gain(struct snd_ad1843 *ad1843, int id)
{
	int lg, rg, lm, rm;
	const struct ad1843_gain *gp = ad1843_gain[id];
	unsigned short mask = (1 << gp->lfield->nbits) - 1;

	ad1843_read_multi(ad1843, 2, gp->lfield, &lg, gp->rfield, &rg);
	if (gp->negative) {
		lg = mask - lg;
		rg = mask - rg;
	}
	if (gp->lmute) {
		ad1843_read_multi(ad1843, 2, gp->lmute, &lm, gp->rmute, &rm);
		if (lm)
			lg = 0;
		if (rm)
			rg = 0;
	}
	return lg << 0 | rg << 8;
}

/*
 * Set an audio channel's gain.
 *
 * Returns the new gain, which may be lower than the old gain.
 */

int ad1843_set_gain(struct snd_ad1843 *ad1843, int id, int newval)
{
	const struct ad1843_gain *gp = ad1843_gain[id];
	unsigned short mask = (1 << gp->lfield->nbits) - 1;

	int lg = (newval >> 0) & mask;
	int rg = (newval >> 8) & mask;
	int lm = (lg == 0) ? 1 : 0;
	int rm = (rg == 0) ? 1 : 0;

	if (gp->negative) {
		lg = mask - lg;
		rg = mask - rg;
	}
	if (gp->lmute)
		ad1843_write_multi(ad1843, 2, gp->lmute, lm, gp->rmute, rm);
	ad1843_write_multi(ad1843, 2, gp->lfield, lg, gp->rfield, rg);
	return ad1843_get_gain(ad1843, id);
}

/* Returns the current recording source */

int ad1843_get_recsrc(struct snd_ad1843 *ad1843)
{
	int val = ad1843_read_bits(ad1843, &ad1843_LSS);

	if (val < 0 || val > 2) {
		val = 2;
		ad1843_write_multi(ad1843, 2,
				   &ad1843_LSS, val, &ad1843_RSS, val);
	}
	return val;
}

/*
 * Set recording source.
 *
 * Returns newsrc on success, -errno on failure.
 */

int ad1843_set_recsrc(struct snd_ad1843 *ad1843, int newsrc)
{
	if (newsrc < 0 || newsrc > 2)
		return -EINVAL;

	ad1843_write_multi(ad1843, 2, &ad1843_LSS, newsrc, &ad1843_RSS, newsrc);
	return newsrc;
}

/* Setup ad1843 for D/A conversion. */

void ad1843_setup_dac(struct snd_ad1843 *ad1843,
		      unsigned int id,
		      unsigned int framerate,
		      snd_pcm_format_t fmt,
		      unsigned int channels)
{
	int ad_fmt = 0, ad_mode = 0;

	switch (fmt) {
	case SNDRV_PCM_FORMAT_S8:
		ad_fmt = 0;
		break;
	case SNDRV_PCM_FORMAT_U8:
		ad_fmt = 0;
		break;
	case SNDRV_PCM_FORMAT_S16_LE:
		ad_fmt = 1;
		break;
	case SNDRV_PCM_FORMAT_MU_LAW:
		ad_fmt = 2;
		break;
	case SNDRV_PCM_FORMAT_A_LAW:
		ad_fmt = 3;
		break;
	default:
		break;
	}

	switch (channels) {
	case 2:
		ad_mode = 0;
		break;
	case 1:
		ad_mode = 1;
		break;
	default:
		break;
	}

	if (id) {
		ad1843_write_bits(ad1843, &ad1843_C2C, framerate);
		ad1843_write_multi(ad1843, 2,
				   &ad1843_DA2SM, ad_mode,
				   &ad1843_DA2F, ad_fmt);
	} else {
		ad1843_write_bits(ad1843, &ad1843_C1C, framerate);
		ad1843_write_multi(ad1843, 2,
				   &ad1843_DA1SM, ad_mode,
				   &ad1843_DA1F, ad_fmt);
	}
}

void ad1843_shutdown_dac(struct snd_ad1843 *ad1843, unsigned int id)
{
	if (id)
		ad1843_write_bits(ad1843, &ad1843_DA2F, 1);
	else
		ad1843_write_bits(ad1843, &ad1843_DA1F, 1);
}

void ad1843_setup_adc(struct snd_ad1843 *ad1843,
		      unsigned int framerate,
		      snd_pcm_format_t fmt,
		      unsigned int channels)
{
	int da_fmt = 0;

	switch (fmt) {
	case SNDRV_PCM_FORMAT_S8:	da_fmt = 0; break;
	case SNDRV_PCM_FORMAT_U8:	da_fmt = 0; break;
	case SNDRV_PCM_FORMAT_S16_LE:	da_fmt = 1; break;
	case SNDRV_PCM_FORMAT_MU_LAW:	da_fmt = 2; break;
	case SNDRV_PCM_FORMAT_A_LAW:	da_fmt = 3; break;
	default:		break;
	}

	ad1843_write_bits(ad1843, &ad1843_C3C, framerate);
	ad1843_write_multi(ad1843, 2,
			   &ad1843_ADLF, da_fmt, &ad1843_ADRF, da_fmt);
}

void ad1843_shutdown_adc(struct snd_ad1843 *ad1843)
{
	/* nothing to do */
}

/*
 * Fully initialize the ad1843.  As described in the AD1843 data
 * sheet, section "START-UP SEQUENCE".  The numbered comments are
 * subsection headings from the data sheet.  See the data sheet, pages
 * 52-54, for more info.
 *
 * return 0 on success, -errno on failure.  */

int ad1843_init(struct snd_ad1843 *ad1843)
{
	unsigned long later;

	if (ad1843_read_bits(ad1843, &ad1843_INIT) != 0) {
		printk(KERN_ERR "ad1843: AD1843 won't initialize\n");
		return -EIO;
	}

	ad1843_write_bits(ad1843, &ad1843_SCF, 1);

	/* 4. Put the conversion resources into standby. */
	ad1843_write_bits(ad1843, &ad1843_PDNI, 0);
	later = jiffies + msecs_to_jiffies(500);

	while (ad1843_read_bits(ad1843, &ad1843_PDNO)) {
		if (time_after(jiffies, later)) {
			printk(KERN_ERR
			       "ad1843: AD1843 won't power up\n");
			return -EIO;
		}
		schedule_timeout_interruptible(5);
	}

	/* 5. Power up the clock generators and enable clock output pins. */
	ad1843_write_multi(ad1843, 3,
			   &ad1843_C1EN, 1,
			   &ad1843_C2EN, 1,
			   &ad1843_C3EN, 1);

	/* 6. Configure conversion resources while they are in standby. */

	/* DAC1/2 use clock 1/2 as source, ADC uses clock 3.  Always. */
	ad1843_write_multi(ad1843, 4,
			   &ad1843_DA1C, 1,
			   &ad1843_DA2C, 2,
			   &ad1843_ADLC, 3,
			   &ad1843_ADRC, 3);

	/* 7. Enable conversion resources. */
	ad1843_write_bits(ad1843, &ad1843_ADTLK, 1);
	ad1843_write_multi(ad1843, 7,
			   &ad1843_ANAEN, 1,
			   &ad1843_AAMEN, 1,
			   &ad1843_DA1EN, 1,
			   &ad1843_DA2EN, 1,
			   &ad1843_DDMEN, 1,
			   &ad1843_ADLEN, 1,
			   &ad1843_ADREN, 1);

	/* 8. Configure conversion resources while they are enabled. */

	/* set gain to 0 for all channels */
	ad1843_set_gain(ad1843, AD1843_GAIN_RECLEV, 0);
	ad1843_set_gain(ad1843, AD1843_GAIN_LINE, 0);
	ad1843_set_gain(ad1843, AD1843_GAIN_LINE_2, 0);
	ad1843_set_gain(ad1843, AD1843_GAIN_MIC, 0);
	ad1843_set_gain(ad1843, AD1843_GAIN_PCM_0, 0);
	ad1843_set_gain(ad1843, AD1843_GAIN_PCM_1, 0);

	/* Unmute all channels. */
	/* DAC1 */
	ad1843_write_multi(ad1843, 2, &ad1843_LDA1GM, 0, &ad1843_RDA1GM, 0);
	/* DAC2 */
	ad1843_write_multi(ad1843, 2, &ad1843_LDA2GM, 0, &ad1843_RDA2GM, 0);

	/* Set default recording source to Line In and set
	 * mic gain to +20 dB.
	 */
	ad1843_set_recsrc(ad1843, 2);
	ad1843_write_multi(ad1843, 2, &ad1843_LMGE, 1, &ad1843_RMGE, 1);

	/* Set Speaker Out level to +/- 4V and unmute it. */
	ad1843_write_multi(ad1843, 3,
			   &ad1843_HPOS, 1,
			   &ad1843_HPOM, 0,
			   &ad1843_MPOM, 0);

	return 0;
}
