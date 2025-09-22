/* $OpenBSD: es8316ac.c,v 1.3 2022/04/06 18:59:28 naddy Exp $ */
/* $NetBSD: es8316ac.c,v 1.2 2020/01/03 01:00:08 jmcneill Exp $ */
/*-
 * Copyright (c) 2020 Jared McNeill <jmcneill@invisible.ca>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/device.h>

#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/i2c/i2cvar.h>
#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_clock.h>
#include <dev/ofw/ofw_misc.h>

#include <sys/audioio.h>
#include <dev/audio_if.h>
#include <dev/midi_if.h>

#define	ESCODEC_RESET_REG		0x00
#define	 RESET_ALL				(0x3f << 0)
#define	 RESET_CSM_ON				(1 << 7)
#define	ESCODEC_CLKMAN1_REG		0x01
#define	 CLKMAN1_MCLK_ON			(1 << 6)
#define	 CLKMAN1_BCLK_ON			(1 << 5)
#define	 CLKMAN1_CLK_CP_ON			(1 << 4)
#define	 CLKMAN1_CLK_DAC_ON			(1 << 2)
#define	 CLKMAN1_ANACLK_DAC_ON			(1 << 0)
#define	ESCODEC_ADC_OSR_REG		0x03
#define	ESCODEC_SD_CLK_REG		0x09
#define	 SD_CLK_MSC				(1 << 7)
#define	 SD_CLK_BCLK_INV			(1 << 5)
#define	ESCODEC_SD_ADC_REG		0x0a
#define	ESCODEC_SD_DAC_REG		0x0b
#define	 SD_FMT_LRP				(1 << 5)
#define	 SD_FMT_WL_MASK				(0x7 << 2)
#define	 SD_FMT_WL_16				(3 << 2)
#define	 SD_FMT_MASK				(0x3 << 0)
#define	 SD_FMT_I2S				(0 << 0)
#define	ESCODEC_VMID_REG		0x0c
#define	ESCODEC_PDN_REG			0x0d
#define	ESCODEC_HPSEL_REG		0x13
#define	ESCODEC_HPMIXRT_REG		0x14
#define	 HPMIXRT_LD2LHPMIX			(1 << 7)
#define	 HPMIXRT_RD2RHPMIX			(1 << 3)
#define	ESCODEC_HPMIX_REG		0x15
#define	 HPMIX_LHPMIX_MUTE			(1 << 5)
#define	 HPMIX_PDN_LHP_MIX			(1 << 4)
#define	 HPMIX_RHPMIX_MUTE			(1 << 1)
#define	 HPMIX_PDN_RHP_MIX			(1 << 0)
#define	ESCODEC_HPMIXVOL_REG		0x16
#define	 HPMIXVOL_LHPMIXVOL_MASK		0xf
#define	 HPMIXVOL_LHPMIXVOL_SHIFT		4
#define	 HPMIXVOL_RHPMIXVOL_MASK		0xf
#define	 HPMIXVOL_RHPMIXVOL_SHIFT		0
#define	ESCODEC_HPOUTEN_REG		0x17
#define	 HPOUTEN_EN_HPL				(1 << 6)
#define	 HPOUTEN_HPL_OUTEN			(1 << 5)
#define	 HPOUTEN_EN_HPR				(1 << 2)
#define	 HPOUTEN_HPR_OUTEN			(1 << 1)
#define	ESCODEC_HPVOL_REG		0x18
#define	 HPVOL_PDN_LICAL			(1 << 7)
#define	 HPVOL_HPLVOL_MASK			0x3
#define	 HPVOL_HPLVOL_SHIFT			4
#define	 HPVOL_PDN_RICAL			(1 << 3)
#define	 HPVOL_HPRVOL_MASK			0x3
#define	 HPVOL_HPRVOL_SHIFT			0
#define	ESCODEC_HPPWR_REG		0x19
#define	 HPPWR_PDN_CPHP				(1 << 2)
#define	ESCODEC_CPPWR_REG		0x1a
#define	 CPPWR_PDN_CP				(1 << 5)
#define	ESCODEC_DACPWR_REG		0x2f
#define	 DACPWR_PDN_DAC_L			(1 << 4)
#define	 DACPWR_PDN_DAC_R			(1 << 0)
#define	ESCODEC_DACCTL1_REG		0x30
#define	 DACCTL1_MUTE				(1 << 5)
#define	ESCODEC_DACVOL_L_REG		0x33
#define	 DACVOL_L_DACVOLUME_MASK		0xff
#define	 DACVOL_L_DACVOLUME_SHIFT		0
#define	ESCODEC_DACVOL_R_REG		0x34
#define	 DACVOL_R_DACVOLUME_MASK		0xff
#define	 DACVOL_R_DACVOLUME_SHIFT		0

struct escodec_softc {
	struct device		 sc_dev;
	i2c_tag_t		 sc_tag;
	i2c_addr_t		 sc_addr;
	int			 sc_node;

	struct dai_device	 sc_dai;
};

int escodec_match(struct device *, void *, void *);
void escodec_attach(struct device *, struct device *, void *);

int escodec_set_format(void *, uint32_t, uint32_t, uint32_t);
int escodec_set_sysclk(void *, uint32_t);

void escodec_init(struct escodec_softc *);
void escodec_lock(struct escodec_softc *);
void escodec_unlock(struct escodec_softc *);
uint8_t escodec_read(struct escodec_softc *, uint8_t);
void escodec_write(struct escodec_softc *, uint8_t, uint8_t);

int escodec_set_port(void *, mixer_ctrl_t *);
int escodec_get_port(void *, mixer_ctrl_t *);
int escodec_query_devinfo(void *, mixer_devinfo_t *);

const struct audio_hw_if escodec_hw_if = {
	.set_port = escodec_set_port,
	.get_port = escodec_get_port,
	.query_devinfo = escodec_query_devinfo,
};

const struct cfattach escodec_ca = {
	sizeof(struct escodec_softc), escodec_match, escodec_attach
};

struct cfdriver escodec_cd = {
	NULL, "escodec", DV_DULL
};

int
escodec_match(struct device *parent, void *match, void *aux)
{
	struct i2c_attach_args *ia = aux;

	if (strcmp(ia->ia_name, "everest,es8316") == 0)
		return 1;

	return 0;
}

void
escodec_attach(struct device *parent, struct device *self, void *aux)
{
	struct escodec_softc *sc = (struct escodec_softc *)self;
	struct i2c_attach_args *ia = aux;

	sc->sc_tag = ia->ia_tag;
	sc->sc_addr = ia->ia_addr;
	sc->sc_node = *(int *)ia->ia_cookie;

	clock_enable(sc->sc_node, "mclk");

	printf("\n");

	escodec_init(sc);

	sc->sc_dai.dd_node = sc->sc_node;
	sc->sc_dai.dd_cookie = sc;
	sc->sc_dai.dd_hw_if = &escodec_hw_if;
	sc->sc_dai.dd_set_format = escodec_set_format;
	sc->sc_dai.dd_set_sysclk = escodec_set_sysclk;
	dai_register(&sc->sc_dai);
}

void
escodec_init(struct escodec_softc *sc)
{
	uint8_t val;

	escodec_lock(sc);

	escodec_write(sc, ESCODEC_RESET_REG, RESET_ALL);
	delay(5000);
	escodec_write(sc, ESCODEC_RESET_REG, RESET_CSM_ON);
	delay(30000);

	escodec_write(sc, ESCODEC_VMID_REG, 0xff);
	escodec_write(sc, ESCODEC_ADC_OSR_REG, 0x32);

	val = escodec_read(sc, ESCODEC_SD_ADC_REG);
	val &= ~SD_FMT_WL_MASK;
	val |= SD_FMT_WL_16;
	escodec_write(sc, ESCODEC_SD_ADC_REG, val);

	val = escodec_read(sc, ESCODEC_SD_DAC_REG);
	val &= ~SD_FMT_WL_MASK;
	val |= SD_FMT_WL_16;
	escodec_write(sc, ESCODEC_SD_DAC_REG, val);

	/* Power up */
	escodec_write(sc, ESCODEC_PDN_REG, 0);

	/* Route DAC signal to HP mixer */
	val = HPMIXRT_LD2LHPMIX | HPMIXRT_RD2RHPMIX;
	escodec_write(sc, ESCODEC_HPMIXRT_REG, val);

	/* Power up DAC */
	escodec_write(sc, ESCODEC_DACPWR_REG, 0);

	/* Power up HP mixer and unmute */
	escodec_write(sc, ESCODEC_HPMIX_REG, 0);

	/* Power up HP output driver */
	val = escodec_read(sc, ESCODEC_HPPWR_REG);
	val &= ~HPPWR_PDN_CPHP;
	escodec_write(sc, ESCODEC_HPPWR_REG, val);

	/* Power up HP charge pump circuits */
	val = escodec_read(sc, ESCODEC_CPPWR_REG);
	val &= ~CPPWR_PDN_CP;
	escodec_write(sc, ESCODEC_CPPWR_REG, val);

	/* Set LIN1/RIN1 as inputs for HP mixer */
	escodec_write(sc, ESCODEC_HPSEL_REG, 0);

	/* Power up HP output driver calibration */
	val = escodec_read(sc, ESCODEC_HPVOL_REG);
	val &= ~HPVOL_PDN_LICAL;
	val &= ~HPVOL_PDN_RICAL;
	escodec_write(sc, ESCODEC_HPVOL_REG, val);

	/* Set headphone mixer to -6dB */
	escodec_write(sc, ESCODEC_HPMIXVOL_REG, 0x44);

	/* Set charge pump headphone to -48dB */
	escodec_write(sc, ESCODEC_HPVOL_REG, 0x33);

	/* Set DAC to 0dB */
	escodec_write(sc, ESCODEC_DACVOL_L_REG, 0);
	escodec_write(sc, ESCODEC_DACVOL_R_REG, 0);

	/* Enable HP output */
	val = HPOUTEN_EN_HPL | HPOUTEN_EN_HPR |
	    HPOUTEN_HPL_OUTEN | HPOUTEN_HPR_OUTEN;
	escodec_write(sc, ESCODEC_HPOUTEN_REG, val);

	escodec_unlock(sc);
}

int
escodec_set_format(void *cookie, uint32_t fmt, uint32_t pol,
    uint32_t clk)
{
	struct escodec_softc *sc = cookie;
	uint8_t sd_clk, sd_fmt, val;

	if (fmt != DAI_FORMAT_I2S)
		return EINVAL;

	if (clk != (DAI_CLOCK_CBS|DAI_CLOCK_CFS))
		return EINVAL;

	switch (pol) {
	case DAI_POLARITY_NB|DAI_POLARITY_NF:
		sd_clk = 0;
		sd_fmt = 0;
		break;
	case DAI_POLARITY_NB|DAI_POLARITY_IF:
		sd_clk = 0;
		sd_fmt = SD_FMT_LRP;
		break;
	case DAI_POLARITY_IB|DAI_POLARITY_NF:
		sd_clk = SD_CLK_BCLK_INV;
		sd_fmt = 0;
		break;
	case DAI_POLARITY_IB|DAI_POLARITY_IF:
		sd_clk = SD_CLK_BCLK_INV;
		sd_fmt = SD_FMT_LRP;
		break;
	}

	escodec_lock(sc);

	val = escodec_read(sc, ESCODEC_SD_CLK_REG);
	val &= ~(SD_CLK_MSC|SD_CLK_BCLK_INV);
	val |= sd_clk;
	escodec_write(sc, ESCODEC_SD_CLK_REG, val);

	val = escodec_read(sc, ESCODEC_SD_ADC_REG);
	val &= ~SD_FMT_MASK;
	val |= SD_FMT_I2S;
	val &= ~SD_FMT_LRP;
	val |= sd_fmt;
	escodec_write(sc, ESCODEC_SD_ADC_REG, val);

	val = escodec_read(sc, ESCODEC_SD_DAC_REG);
	val &= ~SD_FMT_MASK;
	val |= SD_FMT_I2S;
	val &= ~SD_FMT_LRP;
	val |= sd_fmt;
	escodec_write(sc, ESCODEC_SD_DAC_REG, val);

	val = escodec_read(sc, ESCODEC_CLKMAN1_REG);
	val |= CLKMAN1_MCLK_ON;
	val |= CLKMAN1_BCLK_ON;
	val |= CLKMAN1_CLK_CP_ON;
	val |= CLKMAN1_CLK_DAC_ON;
	val |= CLKMAN1_ANACLK_DAC_ON;
	escodec_write(sc, ESCODEC_CLKMAN1_REG, val);

	escodec_unlock(sc);

	return 0;
}

int
escodec_set_sysclk(void *cookie, uint32_t rate)
{
	struct escodec_softc *sc = cookie;
	int error;

	error = clock_set_frequency(sc->sc_node, "mclk", rate);
	if (error != 0) {
		printf("%s: can't set sysclk to %u Hz\n",
		    sc->sc_dev.dv_xname, rate);
		return error;
	}

	return 0;
}

void
escodec_lock(struct escodec_softc *sc)
{
	iic_acquire_bus(sc->sc_tag, 0);
}

void
escodec_unlock(struct escodec_softc *sc)
{
	iic_release_bus(sc->sc_tag, 0);
}

uint8_t
escodec_read(struct escodec_softc *sc, uint8_t reg)
{
	uint8_t val;

	if (iic_smbus_read_byte(sc->sc_tag, sc->sc_addr, reg, &val, 0) != 0)
		val = 0xff;

	return val;
}

void
escodec_write(struct escodec_softc *sc, uint8_t reg, uint8_t val)
{
	(void)iic_smbus_write_byte(sc->sc_tag, sc->sc_addr, reg, val, 0);
}

enum escodec_mixer_ctrl {
	ESCODEC_OUTPUT_CLASS,
	ESCODEC_INPUT_CLASS,
	ESCODEC_INPUT_DAC,
	ESCODEC_INPUT_DAC_MUTE,
	ESCODEC_INPUT_HEADPHONE,
	ESCODEC_INPUT_MIXEROUT,
	ESCODEC_INPUT_MIXEROUT_MUTE,

	ESCODEC_MIXER_CTRL_LAST
};

enum escodec_mixer_type {
	ESCODEC_MIXER_CLASS,
	ESCODEC_MIXER_AMPLIFIER,
	ESCODEC_MIXER_ATTENUATOR,
	ESCODEC_MIXER_MUTE,
};

struct escodec_mixer {
	const char *			name;
	int				mixer_class;
	int				prev, next;
	enum escodec_mixer_ctrl		ctrl;
	enum escodec_mixer_type		type;
	u_int				reg[2];
	uint8_t				mask[2];
	uint8_t				shift[2];
	uint8_t				maxval;
} escodec_mixers[ESCODEC_MIXER_CTRL_LAST] = {
	/*
	 * Mixer classes
	 */
	[ESCODEC_OUTPUT_CLASS] = {
		.name = AudioCoutputs,
		.type = ESCODEC_MIXER_CLASS,
	},
	[ESCODEC_INPUT_CLASS] = {
		.name = AudioCinputs,
		.type = ESCODEC_MIXER_CLASS,
	},

	/*
	 * Stereo DAC
	 */
	[ESCODEC_INPUT_DAC] = {
		.name = AudioNdac,
		.mixer_class = ESCODEC_INPUT_CLASS,
		.prev = AUDIO_MIXER_LAST,
		.next = ESCODEC_INPUT_DAC_MUTE,
		.type = ESCODEC_MIXER_ATTENUATOR,
		.reg = {
			[AUDIO_MIXER_LEVEL_LEFT] = ESCODEC_DACVOL_L_REG,
			[AUDIO_MIXER_LEVEL_RIGHT] = ESCODEC_DACVOL_R_REG,
		},
		.mask = {
			[AUDIO_MIXER_LEVEL_LEFT] = DACVOL_L_DACVOLUME_MASK,
			[AUDIO_MIXER_LEVEL_RIGHT] = DACVOL_R_DACVOLUME_MASK,
		},
		.shift = {
			[AUDIO_MIXER_LEVEL_LEFT] = DACVOL_L_DACVOLUME_SHIFT,
			[AUDIO_MIXER_LEVEL_RIGHT] = DACVOL_R_DACVOLUME_SHIFT,
		},
		.maxval = 0xc0,
	},
	[ESCODEC_INPUT_DAC_MUTE] = {
		.name = AudioNmute,
		.mixer_class = ESCODEC_INPUT_CLASS,
		.prev = ESCODEC_INPUT_DAC,
		.next = AUDIO_MIXER_LAST,
		.type = ESCODEC_MIXER_MUTE,
		.reg = {
			[AUDIO_MIXER_LEVEL_MONO] = ESCODEC_DACCTL1_REG,
		},
		.mask = {
			[AUDIO_MIXER_LEVEL_MONO] = DACCTL1_MUTE,
		}
	},

	/*
	 * Charge Pump Headphones
	 */
	[ESCODEC_INPUT_HEADPHONE] = {
		.name = AudioNheadphone,
		.mixer_class = ESCODEC_INPUT_CLASS,
		.prev = AUDIO_MIXER_LAST,
		.next = AUDIO_MIXER_LAST,
		.type = ESCODEC_MIXER_ATTENUATOR,
		.reg = {
			[AUDIO_MIXER_LEVEL_LEFT] = ESCODEC_HPVOL_REG,
			[AUDIO_MIXER_LEVEL_RIGHT] = ESCODEC_HPVOL_REG,
		},
		.mask = {
			[AUDIO_MIXER_LEVEL_LEFT] = HPVOL_HPLVOL_MASK,
			[AUDIO_MIXER_LEVEL_RIGHT] = HPVOL_HPRVOL_MASK,
		},
		.shift = {
			[AUDIO_MIXER_LEVEL_LEFT] = HPVOL_HPLVOL_SHIFT,
			[AUDIO_MIXER_LEVEL_RIGHT] = HPVOL_HPRVOL_SHIFT,
		}
	},

	/*
	 * Headphone mixer
	 */
	[ESCODEC_INPUT_MIXEROUT] = {
		.name = AudioNmixerout,
		.mixer_class = ESCODEC_INPUT_CLASS,
		.prev = AUDIO_MIXER_LAST,
		.next = ESCODEC_INPUT_MIXEROUT_MUTE,
		.type = ESCODEC_MIXER_AMPLIFIER,
		.reg = {
			[AUDIO_MIXER_LEVEL_LEFT] = ESCODEC_HPMIXVOL_REG,
			[AUDIO_MIXER_LEVEL_RIGHT] = ESCODEC_HPMIXVOL_REG,
		},
		.mask = {
			[AUDIO_MIXER_LEVEL_LEFT] = HPMIXVOL_LHPMIXVOL_MASK,
			[AUDIO_MIXER_LEVEL_RIGHT] = HPMIXVOL_RHPMIXVOL_MASK
		},
		.shift = {
			[AUDIO_MIXER_LEVEL_LEFT] = HPMIXVOL_LHPMIXVOL_SHIFT,
			[AUDIO_MIXER_LEVEL_RIGHT] = HPMIXVOL_RHPMIXVOL_SHIFT
		},
		/*
		 * Datasheet says this field goes up to 0xb, but values
		 * above 0x4 result in noisy output in practice.
		 */
		.maxval = 0x4,
	},
	[ESCODEC_INPUT_MIXEROUT_MUTE] = {
		.name = AudioNmute,
		.mixer_class = ESCODEC_INPUT_CLASS,
		.prev = ESCODEC_INPUT_MIXEROUT,
		.next = AUDIO_MIXER_LAST,
		.type = ESCODEC_MIXER_MUTE,
		.reg = {
			[AUDIO_MIXER_LEVEL_MONO] = ESCODEC_HPMIX_REG,
		},
		.mask = {
			[AUDIO_MIXER_LEVEL_MONO] = HPMIX_LHPMIX_MUTE | HPMIX_RHPMIX_MUTE,
		}
	},
};

struct escodec_mixer *
escodec_get_mixer(u_int index)
{
	if (index >= ESCODEC_MIXER_CTRL_LAST)
		return NULL;

	return &escodec_mixers[index];
}

int
escodec_set_port(void *priv, mixer_ctrl_t *mc)
{
	struct escodec_softc *sc = priv;
	const struct escodec_mixer *mix;
	int nvol, shift, ch;
	uint8_t val;

	if ((mix = escodec_get_mixer(mc->dev)) == NULL)
		return ENXIO;

	switch (mix->type) {
	case ESCODEC_MIXER_AMPLIFIER:
	case ESCODEC_MIXER_ATTENUATOR:
		escodec_lock(sc);
		for (ch = 0; ch < 2; ch++) {
			val = escodec_read(sc, mix->reg[ch]);
			shift = 8 - fls(mix->mask[ch]);
			nvol = mc->un.value.level[ch] >> shift;
			if (mix->type == ESCODEC_MIXER_ATTENUATOR)
				nvol = mix->mask[ch] - nvol;
			if (mix->maxval != 0 && nvol > mix->maxval)
				nvol = mix->maxval;

			val &= ~(mix->mask[ch] << mix->shift[ch]);
			val |= (nvol & mix->mask[ch]) << mix->shift[ch];
			escodec_write(sc, mix->reg[ch], val);
		}
		escodec_unlock(sc);
		return 0;

	case ESCODEC_MIXER_MUTE:
		if (mc->un.ord < 0 || mc->un.ord > 1)
			return EINVAL;
		escodec_lock(sc);
		val = escodec_read(sc, mix->reg[0]);
		if (mc->un.ord)
			val |= mix->mask[0];
		else
			val &= ~mix->mask[0];
		escodec_write(sc, mix->reg[0], val);
		escodec_unlock(sc);
		return 0;

	default:
		return ENXIO;
	}
}

int
escodec_get_port(void *priv, mixer_ctrl_t *mc)
{
	struct escodec_softc *sc = priv;
	const struct escodec_mixer *mix;
	int nvol, shift, ch;
	uint8_t val;

	if ((mix = escodec_get_mixer(mc->dev)) == NULL)
		return ENXIO;

	switch (mix->type) {
	case ESCODEC_MIXER_AMPLIFIER:
	case ESCODEC_MIXER_ATTENUATOR:
		escodec_lock(sc);
		for (ch = 0; ch < 2; ch++) {
			val = escodec_read(sc, mix->reg[ch]);
			shift = 8 - fls(mix->mask[ch]);
			nvol = (val >> mix->shift[ch]) & mix->mask[ch];
			if (mix->type == ESCODEC_MIXER_ATTENUATOR)
				nvol = mix->mask[ch] - nvol;
			nvol <<= shift;
			mc->un.value.level[ch] = nvol;
		}
		escodec_unlock(sc);
		return 0;

	case ESCODEC_MIXER_MUTE:
		escodec_lock(sc);
		val = escodec_read(sc, mix->reg[0]);
		mc->un.ord = (val & mix->mask[0]) != 0;
		escodec_unlock(sc);
		return 0;

	default:
		return ENXIO;
	}
}

int
escodec_query_devinfo(void *priv, mixer_devinfo_t *di)
{
	const struct escodec_mixer *mix;

	if ((mix = escodec_get_mixer(di->index)) == NULL)
		return ENXIO;

	strlcpy(di->label.name, mix->name, sizeof(di->label.name));
	di->mixer_class = mix->mixer_class;
	di->next = mix->next;
	di->prev = mix->prev;

	switch (mix->type) {
	case ESCODEC_MIXER_CLASS:
		di->type = AUDIO_MIXER_CLASS;
		return 0;

	case ESCODEC_MIXER_AMPLIFIER:
	case ESCODEC_MIXER_ATTENUATOR:
		di->type = AUDIO_MIXER_VALUE;
		di->un.v.delta =
		    256 / (mix->mask[0] + 1);
		di->un.v.num_channels = 2;
		strlcpy(di->un.v.units.name, AudioNvolume,
		    sizeof(di->un.v.units.name));
		return 0;

	case ESCODEC_MIXER_MUTE:
		di->type = AUDIO_MIXER_ENUM;
		di->un.e.num_mem = 2;
		strlcpy(di->un.e.member[0].label.name, AudioNoff,
		    sizeof(di->un.e.member[0].label.name));
		di->un.e.member[0].ord = 0;
		strlcpy(di->un.e.member[1].label.name, AudioNon,
		    sizeof(di->un.e.member[1].label.name));
		di->un.e.member[1].ord = 1;
		return 0;

	default:
		return ENXIO;
	}
}
