// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (c) 2004 James Courtier-Dutton <James@superbug.demon.co.uk>
 *  Driver CA0106 chips. e.g. Sound Blaster Audigy LS and Live 24bit
 *  Version: 0.0.18
 *
 *  FEATURES currently supported:
 *    See ca0106_main.c for features.
 * 
 *  Changelog:
 *    Support interrupts per period.
 *    Removed noise from Center/LFE channel when in Analog mode.
 *    Rename and remove mixer controls.
 *  0.0.6
 *    Use separate card based DMA buffer for periods table list.
 *  0.0.7
 *    Change remove and rename ctrls into lists.
 *  0.0.8
 *    Try to fix capture sources.
 *  0.0.9
 *    Fix AC3 output.
 *    Enable S32_LE format support.
 *  0.0.10
 *    Enable playback 48000 and 96000 rates. (Rates other that these do not work, even with "plug:front".)
 *  0.0.11
 *    Add Model name recognition.
 *  0.0.12
 *    Correct interrupt timing. interrupt at end of period, instead of in the middle of a playback period.
 *    Remove redundent "voice" handling.
 *  0.0.13
 *    Single trigger call for multi channels.
 *  0.0.14
 *    Set limits based on what the sound card hardware can do.
 *    playback periods_min=2, periods_max=8
 *    capture hw constraints require period_size = n * 64 bytes.
 *    playback hw constraints require period_size = n * 64 bytes.
 *  0.0.15
 *    Separated ca0106.c into separate functional .c files.
 *  0.0.16
 *    Modified Copyright message.
 *  0.0.17
 *    Implement Mic and Line in Capture.
 *  0.0.18
 *    Add support for mute control on SB Live 24bit (cards w/ SPI DAC)
 *
 *  This code was initially based on code from ALSA's emu10k1x.c which is:
 *  Copyright (c) by Francisco Moraes <fmoraes@nc.rr.com>
 */
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/moduleparam.h>
#include <sound/core.h>
#include <sound/initval.h>
#include <sound/pcm.h>
#include <sound/ac97_codec.h>
#include <sound/info.h>
#include <sound/tlv.h>
#include <linux/io.h>

#include "ca0106.h"

static void ca0106_spdif_enable(struct snd_ca0106 *emu)
{
	unsigned int val;

	if (emu->spdif_enable) {
		/* Digital */
		snd_ca0106_ptr_write(emu, SPDIF_SELECT1, 0, 0xf);
		snd_ca0106_ptr_write(emu, SPDIF_SELECT2, 0, 0x0b000000);
		val = snd_ca0106_ptr_read(emu, CAPTURE_CONTROL, 0) & ~0x1000;
		snd_ca0106_ptr_write(emu, CAPTURE_CONTROL, 0, val);
		val = inl(emu->port + GPIO) & ~0x101;
		outl(val, emu->port + GPIO);

	} else {
		/* Analog */
		snd_ca0106_ptr_write(emu, SPDIF_SELECT1, 0, 0xf);
		snd_ca0106_ptr_write(emu, SPDIF_SELECT2, 0, 0x000f0000);
		val = snd_ca0106_ptr_read(emu, CAPTURE_CONTROL, 0) | 0x1000;
		snd_ca0106_ptr_write(emu, CAPTURE_CONTROL, 0, val);
		val = inl(emu->port + GPIO) | 0x101;
		outl(val, emu->port + GPIO);
	}
}

static void ca0106_set_capture_source(struct snd_ca0106 *emu)
{
	unsigned int val = emu->capture_source;
	unsigned int source, mask;
	source = (val << 28) | (val << 24) | (val << 20) | (val << 16);
	mask = snd_ca0106_ptr_read(emu, CAPTURE_SOURCE, 0) & 0xffff;
	snd_ca0106_ptr_write(emu, CAPTURE_SOURCE, 0, source | mask);
}

static void ca0106_set_i2c_capture_source(struct snd_ca0106 *emu,
					  unsigned int val, int force)
{
	unsigned int ngain, ogain;
	u32 source;

	snd_ca0106_i2c_write(emu, ADC_MUX, 0); /* Mute input */
	ngain = emu->i2c_capture_volume[val][0]; /* Left */
	ogain = emu->i2c_capture_volume[emu->i2c_capture_source][0]; /* Left */
	if (force || ngain != ogain)
		snd_ca0106_i2c_write(emu, ADC_ATTEN_ADCL, ngain & 0xff);
	ngain = emu->i2c_capture_volume[val][1]; /* Right */
	ogain = emu->i2c_capture_volume[emu->i2c_capture_source][1]; /* Right */
	if (force || ngain != ogain)
		snd_ca0106_i2c_write(emu, ADC_ATTEN_ADCR, ngain & 0xff);
	source = 1 << val;
	snd_ca0106_i2c_write(emu, ADC_MUX, source); /* Set source */
	emu->i2c_capture_source = val;
}

static void ca0106_set_capture_mic_line_in(struct snd_ca0106 *emu)
{
	u32 tmp;

	if (emu->capture_mic_line_in) {
		/* snd_ca0106_i2c_write(emu, ADC_MUX, 0); */ /* Mute input */
		tmp = inl(emu->port+GPIO) & ~0x400;
		tmp = tmp | 0x400;
		outl(tmp, emu->port+GPIO);
		/* snd_ca0106_i2c_write(emu, ADC_MUX, ADC_MUX_MIC); */
	} else {
		/* snd_ca0106_i2c_write(emu, ADC_MUX, 0); */ /* Mute input */
		tmp = inl(emu->port+GPIO) & ~0x400;
		outl(tmp, emu->port+GPIO);
		/* snd_ca0106_i2c_write(emu, ADC_MUX, ADC_MUX_LINEIN); */
	}
}

static void ca0106_set_spdif_bits(struct snd_ca0106 *emu, int idx)
{
	snd_ca0106_ptr_write(emu, SPCS0 + idx, 0, emu->spdif_str_bits[idx]);
}

/*
 */
static const DECLARE_TLV_DB_SCALE(snd_ca0106_db_scale1, -5175, 25, 1);
static const DECLARE_TLV_DB_SCALE(snd_ca0106_db_scale2, -10350, 50, 1);

#define snd_ca0106_shared_spdif_info	snd_ctl_boolean_mono_info

static int snd_ca0106_shared_spdif_get(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ca0106 *emu = snd_kcontrol_chip(kcontrol);

	ucontrol->value.integer.value[0] = emu->spdif_enable;
	return 0;
}

static int snd_ca0106_shared_spdif_put(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ca0106 *emu = snd_kcontrol_chip(kcontrol);
	unsigned int val;
	int change = 0;

	val = !!ucontrol->value.integer.value[0];
	change = (emu->spdif_enable != val);
	if (change) {
		emu->spdif_enable = val;
		ca0106_spdif_enable(emu);
	}
        return change;
}

static int snd_ca0106_capture_source_info(struct snd_kcontrol *kcontrol,
					  struct snd_ctl_elem_info *uinfo)
{
	static const char * const texts[6] = {
		"IEC958 out", "i2s mixer out", "IEC958 in", "i2s in", "AC97 in", "SRC out"
	};

	return snd_ctl_enum_info(uinfo, 1, 6, texts);
}

static int snd_ca0106_capture_source_get(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ca0106 *emu = snd_kcontrol_chip(kcontrol);

	ucontrol->value.enumerated.item[0] = emu->capture_source;
	return 0;
}

static int snd_ca0106_capture_source_put(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ca0106 *emu = snd_kcontrol_chip(kcontrol);
	unsigned int val;
	int change = 0;

	val = ucontrol->value.enumerated.item[0] ;
	if (val >= 6)
		return -EINVAL;
	change = (emu->capture_source != val);
	if (change) {
		emu->capture_source = val;
		ca0106_set_capture_source(emu);
	}
        return change;
}

static int snd_ca0106_i2c_capture_source_info(struct snd_kcontrol *kcontrol,
					  struct snd_ctl_elem_info *uinfo)
{
	static const char * const texts[4] = {
		"Phone", "Mic", "Line in", "Aux"
	};

	return snd_ctl_enum_info(uinfo, 1, 4, texts);
}

static int snd_ca0106_i2c_capture_source_get(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ca0106 *emu = snd_kcontrol_chip(kcontrol);

	ucontrol->value.enumerated.item[0] = emu->i2c_capture_source;
	return 0;
}

static int snd_ca0106_i2c_capture_source_put(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ca0106 *emu = snd_kcontrol_chip(kcontrol);
	unsigned int source_id;
	int change = 0;
	/* If the capture source has changed,
	 * update the capture volume from the cached value
	 * for the particular source.
	 */
	source_id = ucontrol->value.enumerated.item[0] ;
	if (source_id >= 4)
		return -EINVAL;
	change = (emu->i2c_capture_source != source_id);
	if (change) {
		ca0106_set_i2c_capture_source(emu, source_id, 0);
	}
        return change;
}

static int snd_ca0106_capture_line_in_side_out_info(struct snd_kcontrol *kcontrol,
					       struct snd_ctl_elem_info *uinfo)
{
	static const char * const texts[2] = { "Side out", "Line in" };

	return snd_ctl_enum_info(uinfo, 1, 2, texts);
}

static int snd_ca0106_capture_mic_line_in_info(struct snd_kcontrol *kcontrol,
					       struct snd_ctl_elem_info *uinfo)
{
	static const char * const texts[2] = { "Line in", "Mic in" };

	return snd_ctl_enum_info(uinfo, 1, 2, texts);
}

static int snd_ca0106_capture_mic_line_in_get(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ca0106 *emu = snd_kcontrol_chip(kcontrol);

	ucontrol->value.enumerated.item[0] = emu->capture_mic_line_in;
	return 0;
}

static int snd_ca0106_capture_mic_line_in_put(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ca0106 *emu = snd_kcontrol_chip(kcontrol);
	unsigned int val;
	int change = 0;

	val = ucontrol->value.enumerated.item[0] ;
	if (val > 1)
		return -EINVAL;
	change = (emu->capture_mic_line_in != val);
	if (change) {
		emu->capture_mic_line_in = val;
		ca0106_set_capture_mic_line_in(emu);
	}
        return change;
}

static const struct snd_kcontrol_new snd_ca0106_capture_mic_line_in =
{
	.iface =	SNDRV_CTL_ELEM_IFACE_MIXER,
	.name =		"Shared Mic/Line in Capture Switch",
	.info =		snd_ca0106_capture_mic_line_in_info,
	.get =		snd_ca0106_capture_mic_line_in_get,
	.put =		snd_ca0106_capture_mic_line_in_put
};

static const struct snd_kcontrol_new snd_ca0106_capture_line_in_side_out =
{
	.iface =	SNDRV_CTL_ELEM_IFACE_MIXER,
	.name =		"Shared Line in/Side out Capture Switch",
	.info =		snd_ca0106_capture_line_in_side_out_info,
	.get =		snd_ca0106_capture_mic_line_in_get,
	.put =		snd_ca0106_capture_mic_line_in_put
};


static int snd_ca0106_spdif_info(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_IEC958;
	uinfo->count = 1;
	return 0;
}

static void decode_spdif_bits(unsigned char *status, unsigned int bits)
{
	status[0] = (bits >> 0) & 0xff;
	status[1] = (bits >> 8) & 0xff;
	status[2] = (bits >> 16) & 0xff;
	status[3] = (bits >> 24) & 0xff;
}

static int snd_ca0106_spdif_get_default(struct snd_kcontrol *kcontrol,
                                 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ca0106 *emu = snd_kcontrol_chip(kcontrol);
	unsigned int idx = snd_ctl_get_ioffidx(kcontrol, &ucontrol->id);

	decode_spdif_bits(ucontrol->value.iec958.status,
			  emu->spdif_bits[idx]);
        return 0;
}

static int snd_ca0106_spdif_get_stream(struct snd_kcontrol *kcontrol,
                                 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ca0106 *emu = snd_kcontrol_chip(kcontrol);
	unsigned int idx = snd_ctl_get_ioffidx(kcontrol, &ucontrol->id);

	decode_spdif_bits(ucontrol->value.iec958.status,
			  emu->spdif_str_bits[idx]);
        return 0;
}

static int snd_ca0106_spdif_get_mask(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.iec958.status[0] = 0xff;
	ucontrol->value.iec958.status[1] = 0xff;
	ucontrol->value.iec958.status[2] = 0xff;
	ucontrol->value.iec958.status[3] = 0xff;
        return 0;
}

static unsigned int encode_spdif_bits(unsigned char *status)
{
	return ((unsigned int)status[0] << 0) |
		((unsigned int)status[1] << 8) |
		((unsigned int)status[2] << 16) |
		((unsigned int)status[3] << 24);
}

static int snd_ca0106_spdif_put_default(struct snd_kcontrol *kcontrol,
                                 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ca0106 *emu = snd_kcontrol_chip(kcontrol);
	unsigned int idx = snd_ctl_get_ioffidx(kcontrol, &ucontrol->id);
	unsigned int val;

	val = encode_spdif_bits(ucontrol->value.iec958.status);
	if (val != emu->spdif_bits[idx]) {
		emu->spdif_bits[idx] = val;
		/* FIXME: this isn't safe, but needed to keep the compatibility
		 * with older alsa-lib config
		 */
		emu->spdif_str_bits[idx] = val;
		ca0106_set_spdif_bits(emu, idx);
		return 1;
	}
	return 0;
}

static int snd_ca0106_spdif_put_stream(struct snd_kcontrol *kcontrol,
                                 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ca0106 *emu = snd_kcontrol_chip(kcontrol);
	unsigned int idx = snd_ctl_get_ioffidx(kcontrol, &ucontrol->id);
	unsigned int val;

	val = encode_spdif_bits(ucontrol->value.iec958.status);
	if (val != emu->spdif_str_bits[idx]) {
		emu->spdif_str_bits[idx] = val;
		ca0106_set_spdif_bits(emu, idx);
		return 1;
	}
        return 0;
}

static int snd_ca0106_volume_info(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_info *uinfo)
{
        uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
        uinfo->count = 2;
        uinfo->value.integer.min = 0;
        uinfo->value.integer.max = 255;
        return 0;
}

static int snd_ca0106_volume_get(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
        struct snd_ca0106 *emu = snd_kcontrol_chip(kcontrol);
        unsigned int value;
	int channel_id, reg;

	channel_id = (kcontrol->private_value >> 8) & 0xff;
	reg = kcontrol->private_value & 0xff;

        value = snd_ca0106_ptr_read(emu, reg, channel_id);
        ucontrol->value.integer.value[0] = 0xff - ((value >> 24) & 0xff); /* Left */
        ucontrol->value.integer.value[1] = 0xff - ((value >> 16) & 0xff); /* Right */
        return 0;
}

static int snd_ca0106_volume_put(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
        struct snd_ca0106 *emu = snd_kcontrol_chip(kcontrol);
        unsigned int oval, nval;
	int channel_id, reg;

	channel_id = (kcontrol->private_value >> 8) & 0xff;
	reg = kcontrol->private_value & 0xff;

	oval = snd_ca0106_ptr_read(emu, reg, channel_id);
	nval = ((0xff - ucontrol->value.integer.value[0]) << 24) |
		((0xff - ucontrol->value.integer.value[1]) << 16);
        nval |= ((0xff - ucontrol->value.integer.value[0]) << 8) |
		((0xff - ucontrol->value.integer.value[1]) );
	if (oval == nval)
		return 0;
	snd_ca0106_ptr_write(emu, reg, channel_id, nval);
	return 1;
}

static int snd_ca0106_i2c_volume_info(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_info *uinfo)
{
        uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
        uinfo->count = 2;
        uinfo->value.integer.min = 0;
        uinfo->value.integer.max = 255;
        return 0;
}

static int snd_ca0106_i2c_volume_get(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
        struct snd_ca0106 *emu = snd_kcontrol_chip(kcontrol);
	int source_id;

	source_id = kcontrol->private_value;

        ucontrol->value.integer.value[0] = emu->i2c_capture_volume[source_id][0];
        ucontrol->value.integer.value[1] = emu->i2c_capture_volume[source_id][1];
        return 0;
}

static int snd_ca0106_i2c_volume_put(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
        struct snd_ca0106 *emu = snd_kcontrol_chip(kcontrol);
        unsigned int ogain;
        unsigned int ngain;
	int source_id;
	int change = 0;

	source_id = kcontrol->private_value;
	ogain = emu->i2c_capture_volume[source_id][0]; /* Left */
	ngain = ucontrol->value.integer.value[0];
	if (ngain > 0xff)
		return -EINVAL;
	if (ogain != ngain) {
		if (emu->i2c_capture_source == source_id)
			snd_ca0106_i2c_write(emu, ADC_ATTEN_ADCL, ((ngain) & 0xff) );
		emu->i2c_capture_volume[source_id][0] = ucontrol->value.integer.value[0];
		change = 1;
	}
	ogain = emu->i2c_capture_volume[source_id][1]; /* Right */
	ngain = ucontrol->value.integer.value[1];
	if (ngain > 0xff)
		return -EINVAL;
	if (ogain != ngain) {
		if (emu->i2c_capture_source == source_id)
			snd_ca0106_i2c_write(emu, ADC_ATTEN_ADCR, ((ngain) & 0xff));
		emu->i2c_capture_volume[source_id][1] = ucontrol->value.integer.value[1];
		change = 1;
	}

	return change;
}

#define spi_mute_info	snd_ctl_boolean_mono_info

static int spi_mute_get(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ca0106 *emu = snd_kcontrol_chip(kcontrol);
	unsigned int reg = kcontrol->private_value >> SPI_REG_SHIFT;
	unsigned int bit = kcontrol->private_value & SPI_REG_MASK;

	ucontrol->value.integer.value[0] = !(emu->spi_dac_reg[reg] & bit);
	return 0;
}

static int spi_mute_put(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ca0106 *emu = snd_kcontrol_chip(kcontrol);
	unsigned int reg = kcontrol->private_value >> SPI_REG_SHIFT;
	unsigned int bit = kcontrol->private_value & SPI_REG_MASK;
	int ret;

	ret = emu->spi_dac_reg[reg] & bit;
	if (ucontrol->value.integer.value[0]) {
		if (!ret)	/* bit already cleared, do nothing */
			return 0;
		emu->spi_dac_reg[reg] &= ~bit;
	} else {
		if (ret)	/* bit already set, do nothing */
			return 0;
		emu->spi_dac_reg[reg] |= bit;
	}

	ret = snd_ca0106_spi_write(emu, emu->spi_dac_reg[reg]);
	return ret ? -EINVAL : 1;
}

#define CA_VOLUME(xname,chid,reg) \
{								\
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname,	\
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE |		\
	          SNDRV_CTL_ELEM_ACCESS_TLV_READ,		\
	.info =	 snd_ca0106_volume_info,			\
	.get =   snd_ca0106_volume_get,				\
	.put =   snd_ca0106_volume_put,				\
	.tlv = { .p = snd_ca0106_db_scale1 },			\
	.private_value = ((chid) << 8) | (reg)			\
}

static const struct snd_kcontrol_new snd_ca0106_volume_ctls[] = {
	CA_VOLUME("Analog Front Playback Volume",
		  CONTROL_FRONT_CHANNEL, PLAYBACK_VOLUME2),
        CA_VOLUME("Analog Rear Playback Volume",
		  CONTROL_REAR_CHANNEL, PLAYBACK_VOLUME2),
	CA_VOLUME("Analog Center/LFE Playback Volume",
		  CONTROL_CENTER_LFE_CHANNEL, PLAYBACK_VOLUME2),
        CA_VOLUME("Analog Side Playback Volume",
		  CONTROL_UNKNOWN_CHANNEL, PLAYBACK_VOLUME2),

        CA_VOLUME("IEC958 Front Playback Volume",
		  CONTROL_FRONT_CHANNEL, PLAYBACK_VOLUME1),
	CA_VOLUME("IEC958 Rear Playback Volume",
		  CONTROL_REAR_CHANNEL, PLAYBACK_VOLUME1),
	CA_VOLUME("IEC958 Center/LFE Playback Volume",
		  CONTROL_CENTER_LFE_CHANNEL, PLAYBACK_VOLUME1),
	CA_VOLUME("IEC958 Unknown Playback Volume",
		  CONTROL_UNKNOWN_CHANNEL, PLAYBACK_VOLUME1),

        CA_VOLUME("CAPTURE feedback Playback Volume",
		  1, CAPTURE_CONTROL),

	{
		.access =	SNDRV_CTL_ELEM_ACCESS_READ,
		.iface =        SNDRV_CTL_ELEM_IFACE_PCM,
		.name =         SNDRV_CTL_NAME_IEC958("",PLAYBACK,MASK),
		.count =	4,
		.info =         snd_ca0106_spdif_info,
		.get =          snd_ca0106_spdif_get_mask
	},
	{
		.iface =	SNDRV_CTL_ELEM_IFACE_MIXER,
		.name =		"IEC958 Playback Switch",
		.info =		snd_ca0106_shared_spdif_info,
		.get =		snd_ca0106_shared_spdif_get,
		.put =		snd_ca0106_shared_spdif_put
	},
	{
		.iface =	SNDRV_CTL_ELEM_IFACE_MIXER,
		.name =		"Digital Source Capture Enum",
		.info =		snd_ca0106_capture_source_info,
		.get =		snd_ca0106_capture_source_get,
		.put =		snd_ca0106_capture_source_put
	},
	{
		.iface =	SNDRV_CTL_ELEM_IFACE_MIXER,
		.name =		"Analog Source Capture Enum",
		.info =		snd_ca0106_i2c_capture_source_info,
		.get =		snd_ca0106_i2c_capture_source_get,
		.put =		snd_ca0106_i2c_capture_source_put
	},
	{
		.iface =	SNDRV_CTL_ELEM_IFACE_PCM,
		.name =         SNDRV_CTL_NAME_IEC958("",PLAYBACK,DEFAULT),
		.count =	4,
		.info =         snd_ca0106_spdif_info,
		.get =          snd_ca0106_spdif_get_default,
		.put =          snd_ca0106_spdif_put_default
	},
	{
		.iface =	SNDRV_CTL_ELEM_IFACE_PCM,
		.name =         SNDRV_CTL_NAME_IEC958("",PLAYBACK,PCM_STREAM),
		.count =	4,
		.info =         snd_ca0106_spdif_info,
		.get =          snd_ca0106_spdif_get_stream,
		.put =          snd_ca0106_spdif_put_stream
	},
};

#define I2C_VOLUME(xname,chid) \
{								\
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname,	\
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE |		\
	          SNDRV_CTL_ELEM_ACCESS_TLV_READ,		\
	.info =  snd_ca0106_i2c_volume_info,			\
	.get =   snd_ca0106_i2c_volume_get,			\
	.put =   snd_ca0106_i2c_volume_put,			\
	.tlv = { .p = snd_ca0106_db_scale2 },			\
	.private_value = chid					\
}

static const struct snd_kcontrol_new snd_ca0106_volume_i2c_adc_ctls[] = {
        I2C_VOLUME("Phone Capture Volume", 0),
        I2C_VOLUME("Mic Capture Volume", 1),
        I2C_VOLUME("Line in Capture Volume", 2),
        I2C_VOLUME("Aux Capture Volume", 3),
};

static const int spi_dmute_reg[] = {
	SPI_DMUTE0_REG,
	SPI_DMUTE1_REG,
	SPI_DMUTE2_REG,
	0,
	SPI_DMUTE4_REG,
};
static const int spi_dmute_bit[] = {
	SPI_DMUTE0_BIT,
	SPI_DMUTE1_BIT,
	SPI_DMUTE2_BIT,
	0,
	SPI_DMUTE4_BIT,
};

static struct snd_kcontrol_new
snd_ca0106_volume_spi_dac_ctl(const struct snd_ca0106_details *details,
			      int channel_id)
{
	struct snd_kcontrol_new spi_switch = {0};
	int reg, bit;
	int dac_id;

	spi_switch.iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	spi_switch.access = SNDRV_CTL_ELEM_ACCESS_READWRITE;
	spi_switch.info = spi_mute_info;
	spi_switch.get = spi_mute_get;
	spi_switch.put = spi_mute_put;

	switch (channel_id) {
	case PCM_FRONT_CHANNEL:
		spi_switch.name = "Analog Front Playback Switch";
		dac_id = (details->spi_dac & 0xf000) >> (4 * 3);
		break;
	case PCM_REAR_CHANNEL:
		spi_switch.name = "Analog Rear Playback Switch";
		dac_id = (details->spi_dac & 0x0f00) >> (4 * 2);
		break;
	case PCM_CENTER_LFE_CHANNEL:
		spi_switch.name = "Analog Center/LFE Playback Switch";
		dac_id = (details->spi_dac & 0x00f0) >> (4 * 1);
		break;
	case PCM_UNKNOWN_CHANNEL:
		spi_switch.name = "Analog Side Playback Switch";
		dac_id = (details->spi_dac & 0x000f) >> (4 * 0);
		break;
	default:
		/* Unused channel */
		spi_switch.name = NULL;
		dac_id = 0;
	}
	reg = spi_dmute_reg[dac_id];
	bit = spi_dmute_bit[dac_id];

	spi_switch.private_value = (reg << SPI_REG_SHIFT) | bit;

	return spi_switch;
}

static int remove_ctl(struct snd_card *card, const char *name)
{
	struct snd_ctl_elem_id id;
	memset(&id, 0, sizeof(id));
	strcpy(id.name, name);
	id.iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	return snd_ctl_remove_id(card, &id);
}

static struct snd_kcontrol *ctl_find(struct snd_card *card, const char *name)
{
	struct snd_ctl_elem_id sid;
	memset(&sid, 0, sizeof(sid));
	/* FIXME: strcpy is bad. */
	strcpy(sid.name, name);
	sid.iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	return snd_ctl_find_id(card, &sid);
}

static int rename_ctl(struct snd_card *card, const char *src, const char *dst)
{
	struct snd_kcontrol *kctl = ctl_find(card, src);
	if (kctl) {
		strcpy(kctl->id.name, dst);
		return 0;
	}
	return -ENOENT;
}

#define ADD_CTLS(emu, ctls)						\
	do {								\
		int i, _err;						\
		for (i = 0; i < ARRAY_SIZE(ctls); i++) {		\
			_err = snd_ctl_add(card, snd_ctl_new1(&ctls[i], emu)); \
			if (_err < 0)					\
				return _err;				\
		}							\
	} while (0)

static
DECLARE_TLV_DB_SCALE(snd_ca0106_master_db_scale, -6375, 25, 1);

static const char * const follower_vols[] = {
	"Analog Front Playback Volume",
        "Analog Rear Playback Volume",
	"Analog Center/LFE Playback Volume",
        "Analog Side Playback Volume",
        "IEC958 Front Playback Volume",
	"IEC958 Rear Playback Volume",
	"IEC958 Center/LFE Playback Volume",
	"IEC958 Unknown Playback Volume",
        "CAPTURE feedback Playback Volume",
	NULL
};

static const char * const follower_sws[] = {
	"Analog Front Playback Switch",
	"Analog Rear Playback Switch",
	"Analog Center/LFE Playback Switch",
	"Analog Side Playback Switch",
	"IEC958 Playback Switch",
	NULL
};

static void add_followers(struct snd_card *card,
			  struct snd_kcontrol *master, const char * const *list)
{
	for (; *list; list++) {
		struct snd_kcontrol *follower = ctl_find(card, *list);
		if (follower)
			snd_ctl_add_follower(master, follower);
	}
}

int snd_ca0106_mixer(struct snd_ca0106 *emu)
{
	int err;
        struct snd_card *card = emu->card;
	const char * const *c;
	struct snd_kcontrol *vmaster;
	static const char * const ca0106_remove_ctls[] = {
		"Master Mono Playback Switch",
		"Master Mono Playback Volume",
		"3D Control - Switch",
		"3D Control Sigmatel - Depth",
		"PCM Playback Switch",
		"PCM Playback Volume",
		"CD Playback Switch",
		"CD Playback Volume",
		"Phone Playback Switch",
		"Phone Playback Volume",
		"Video Playback Switch",
		"Video Playback Volume",
		"Beep Playback Switch",
		"Beep Playback Volume",
		"Mono Output Select",
		"Capture Source",
		"Capture Switch",
		"Capture Volume",
		"External Amplifier",
		"Sigmatel 4-Speaker Stereo Playback Switch",
		"Surround Phase Inversion Playback Switch",
		NULL
	};
	static const char * const ca0106_rename_ctls[] = {
		"Master Playback Switch", "Capture Switch",
		"Master Playback Volume", "Capture Volume",
		"Line Playback Switch", "AC97 Line Capture Switch",
		"Line Playback Volume", "AC97 Line Capture Volume",
		"Aux Playback Switch", "AC97 Aux Capture Switch",
		"Aux Playback Volume", "AC97 Aux Capture Volume",
		"Mic Playback Switch", "AC97 Mic Capture Switch",
		"Mic Playback Volume", "AC97 Mic Capture Volume",
		"Mic Select", "AC97 Mic Select",
		"Mic Boost (+20dB)", "AC97 Mic Boost (+20dB)",
		NULL
	};
#if 1
	for (c = ca0106_remove_ctls; *c; c++)
		remove_ctl(card, *c);
	for (c = ca0106_rename_ctls; *c; c += 2)
		rename_ctl(card, c[0], c[1]);
#endif

	ADD_CTLS(emu, snd_ca0106_volume_ctls);
	if (emu->details->i2c_adc == 1) {
		ADD_CTLS(emu, snd_ca0106_volume_i2c_adc_ctls);
		if (emu->details->gpio_type == 1)
			err = snd_ctl_add(card, snd_ctl_new1(&snd_ca0106_capture_mic_line_in, emu));
		else  /* gpio_type == 2 */
			err = snd_ctl_add(card, snd_ctl_new1(&snd_ca0106_capture_line_in_side_out, emu));
		if (err < 0)
			return err;
	}
	if (emu->details->spi_dac) {
		int i;
		for (i = 0;; i++) {
			struct snd_kcontrol_new ctl;
			ctl = snd_ca0106_volume_spi_dac_ctl(emu->details, i);
			if (!ctl.name)
				break;
			err = snd_ctl_add(card, snd_ctl_new1(&ctl, emu));
			if (err < 0)
				return err;
		}
	}

	/* Create virtual master controls */
	vmaster = snd_ctl_make_virtual_master("Master Playback Volume",
					      snd_ca0106_master_db_scale);
	if (!vmaster)
		return -ENOMEM;
	err = snd_ctl_add(card, vmaster);
	if (err < 0)
		return err;
	add_followers(card, vmaster, follower_vols);

	if (emu->details->spi_dac) {
		vmaster = snd_ctl_make_virtual_master("Master Playback Switch",
						      NULL);
		if (!vmaster)
			return -ENOMEM;
		err = snd_ctl_add(card, vmaster);
		if (err < 0)
			return err;
		add_followers(card, vmaster, follower_sws);
	}

	strcpy(card->mixername, "CA0106");
        return 0;
}

#ifdef CONFIG_PM_SLEEP
struct ca0106_vol_tbl {
	unsigned int channel_id;
	unsigned int reg;
};

static const struct ca0106_vol_tbl saved_volumes[NUM_SAVED_VOLUMES] = {
	{ CONTROL_FRONT_CHANNEL, PLAYBACK_VOLUME2 },
	{ CONTROL_REAR_CHANNEL, PLAYBACK_VOLUME2 },
	{ CONTROL_CENTER_LFE_CHANNEL, PLAYBACK_VOLUME2 },
	{ CONTROL_UNKNOWN_CHANNEL, PLAYBACK_VOLUME2 },
	{ CONTROL_FRONT_CHANNEL, PLAYBACK_VOLUME1 },
	{ CONTROL_REAR_CHANNEL, PLAYBACK_VOLUME1 },
	{ CONTROL_CENTER_LFE_CHANNEL, PLAYBACK_VOLUME1 },
	{ CONTROL_UNKNOWN_CHANNEL, PLAYBACK_VOLUME1 },
	{ 1, CAPTURE_CONTROL },
};

void snd_ca0106_mixer_suspend(struct snd_ca0106 *chip)
{
	int i;

	/* save volumes */
	for (i = 0; i < NUM_SAVED_VOLUMES; i++)
		chip->saved_vol[i] =
			snd_ca0106_ptr_read(chip, saved_volumes[i].reg,
					    saved_volumes[i].channel_id);
}

void snd_ca0106_mixer_resume(struct snd_ca0106  *chip)
{
	int i;

	for (i = 0; i < NUM_SAVED_VOLUMES; i++)
		snd_ca0106_ptr_write(chip, saved_volumes[i].reg,
				     saved_volumes[i].channel_id,
				     chip->saved_vol[i]);

	ca0106_spdif_enable(chip);
	ca0106_set_capture_source(chip);
	ca0106_set_i2c_capture_source(chip, chip->i2c_capture_source, 1);
	for (i = 0; i < 4; i++)
		ca0106_set_spdif_bits(chip, i);
	if (chip->details->i2c_adc)
		ca0106_set_capture_mic_line_in(chip);
}
#endif /* CONFIG_PM_SLEEP */
