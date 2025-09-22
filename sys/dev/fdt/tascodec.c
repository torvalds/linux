/*	$OpenBSD: tascodec.c,v 1.8 2023/12/26 09:25:15 kettenis Exp $	*/
/*
 * Copyright (c) 2022 Mark Kettenis <kettenis@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/audioio.h>
#include <sys/device.h>
#include <sys/malloc.h>

#include <machine/bus.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_gpio.h>
#include <dev/ofw/ofw_misc.h>
#include <dev/ofw/ofw_regulator.h>
#include <dev/ofw/fdt.h>

#include <dev/i2c/i2cvar.h>

#include <dev/audio_if.h>

#define PWR_CTL				0x02
#define  PWR_CTL_ISNS_PD		(1 << 3)
#define  PWR_CTL_VSNS_PD		(1 << 2)
#define  PWR_CTL_MODE_MASK		(3 << 0)
#define  PWR_CTL_MODE_ACTIVE		(0 << 0)
#define  PWR_CTL_MODE_MUTE		(1 << 0)
#define  PWR_CTL_MODE_SHUTDOWN		(2 << 0)
#define PB_CFG2				0x05
#define  PB_CFG2_DVC_PCM_MIN		0xc9
#define  PB_CFG2_DVC_PCM_30DB		0x3c
#define TDM_CFG0			0x0a
#define  TDM_CFG0_FRAME_START		(1 << 0)
#define TDM_CFG1			0x0b
#define  TDM_CFG1_RX_JUSTIFY		(1 << 6)
#define  TDM_CFG1_RX_OFFSET_MASK	(0x1f << 1)
#define  TDM_CFG1_RX_OFFSET_SHIFT	1
#define  TDM_CFG1_RX_EDGE		(1 << 0)
#define TDM_CFG2			0x0c
#define  TDM_CFG2_SCFG_MASK		(3 << 4)
#define  TDM_CFG2_SCFG_MONO_LEFT	(1 << 4)
#define  TDM_CFG2_SCFG_MONO_RIGHT	(2 << 4)
#define  TDM_CFG2_SCFG_STEREO_DOWNMIX	(3 << 4)
#define TDM_CFG3			0x0d
#define  TDM_CFG3_RX_SLOT_R_MASK	0xf0
#define  TDM_CFG3_RX_SLOT_R_SHIFT	4
#define  TDM_CFG3_RX_SLOT_L_MASK	0x0f
#define  TDM_CFG3_RX_SLOT_L_SHIFT	0

struct tascodec_softc {
	struct device		sc_dev;
	i2c_tag_t		sc_tag;
	i2c_addr_t		sc_addr;

	struct dai_device	sc_dai;
	uint8_t			sc_dvc;
	uint8_t			sc_mute;
};

int	tascodec_match(struct device *, void *, void *);
void	tascodec_attach(struct device *, struct device *, void *);
int	tascodec_activate(struct device *, int);

const struct cfattach tascodec_ca = {
	sizeof(struct tascodec_softc), tascodec_match, tascodec_attach,
	NULL, tascodec_activate
};

struct cfdriver tascodec_cd = {
	NULL, "tascodec", DV_DULL
};

int	tascodec_set_format(void *, uint32_t, uint32_t, uint32_t);
int	tascodec_set_tdm_slot(void *, int);

int	tascodec_set_port(void *, mixer_ctrl_t *);
int	tascodec_get_port(void *, mixer_ctrl_t *);
int	tascodec_query_devinfo(void *, mixer_devinfo_t *);
int	tascodec_trigger_output(void *, void *, void *, int,
	    void (*)(void *), void *, struct audio_params *);
int	tascodec_halt_output(void *);

const struct audio_hw_if tascodec_hw_if = {
	.set_port = tascodec_set_port,
	.get_port = tascodec_get_port,
	.query_devinfo = tascodec_query_devinfo,
	.trigger_output = tascodec_trigger_output,
	.halt_output = tascodec_halt_output,
};

uint8_t	tascodec_read(struct tascodec_softc *, int);
void	tascodec_write(struct tascodec_softc *, int, uint8_t);

int
tascodec_match(struct device *parent, void *match, void *aux)
{
	struct i2c_attach_args *ia = aux;

	return iic_is_compatible(ia, "ti,tas2770");
}

void
tascodec_attach(struct device *parent, struct device *self, void *aux)
{
	struct tascodec_softc *sc = (struct tascodec_softc *)self;
	struct i2c_attach_args *ia = aux;
	int node = *(int *)ia->ia_cookie;
	uint32_t *sdz_gpio;
	int sdz_gpiolen;
	uint8_t cfg2;

	sc->sc_tag = ia->ia_tag;
	sc->sc_addr = ia->ia_addr;

	printf("\n");

	regulator_enable(OF_getpropint(node, "SDZ-supply", 0));

	sdz_gpiolen = OF_getproplen(node, "shutdown-gpios");
	if (sdz_gpiolen > 0) {
		sdz_gpio = malloc(sdz_gpiolen, M_TEMP, M_WAITOK);
		OF_getpropintarray(node, "shutdown-gpios",
		    sdz_gpio, sdz_gpiolen);
		gpio_controller_config_pin(sdz_gpio, GPIO_CONFIG_OUTPUT);
		gpio_controller_set_pin(sdz_gpio, 1);
		free(sdz_gpio, M_TEMP, sdz_gpiolen);
		delay(1000);
	}

	/* Set volume to a reasonable level. */
	sc->sc_dvc = PB_CFG2_DVC_PCM_30DB;
	sc->sc_mute = PWR_CTL_MODE_ACTIVE;
	tascodec_write(sc, PB_CFG2, sc->sc_dvc);

	/* Default to stereo downmix mode for now. */
	cfg2 = tascodec_read(sc, TDM_CFG2);
	cfg2 &= ~TDM_CFG2_SCFG_MASK;
	cfg2 |= TDM_CFG2_SCFG_STEREO_DOWNMIX;
	tascodec_write(sc, TDM_CFG2, cfg2);

	sc->sc_dai.dd_node = node;
	sc->sc_dai.dd_cookie = sc;
	sc->sc_dai.dd_hw_if = &tascodec_hw_if;
	sc->sc_dai.dd_set_format = tascodec_set_format;
	sc->sc_dai.dd_set_tdm_slot = tascodec_set_tdm_slot;
	dai_register(&sc->sc_dai);
}

int
tascodec_activate(struct device *self, int act)
{
	struct tascodec_softc *sc = (struct tascodec_softc *)self;

	switch (act) {
	case DVACT_POWERDOWN:
		tascodec_write(sc, PWR_CTL,
		    PWR_CTL_ISNS_PD | PWR_CTL_VSNS_PD | PWR_CTL_MODE_SHUTDOWN);
		break;
	}

	return 0;
}

int
tascodec_set_format(void *cookie, uint32_t fmt, uint32_t pol,
    uint32_t clk)
{
	struct tascodec_softc *sc = cookie;
	uint8_t cfg0, cfg1;

	cfg0 = tascodec_read(sc, TDM_CFG0);
	cfg1 = tascodec_read(sc, TDM_CFG1);
	cfg1 &= ~TDM_CFG1_RX_OFFSET_MASK;

	switch (fmt) {
	case DAI_FORMAT_I2S:
		cfg0 |= TDM_CFG0_FRAME_START;
		cfg1 &= ~TDM_CFG1_RX_JUSTIFY;
		cfg1 |= (1 << TDM_CFG1_RX_OFFSET_SHIFT);
		cfg1 &= ~TDM_CFG1_RX_EDGE;
		break;
	case DAI_FORMAT_RJ:
		cfg0 &= ~TDM_CFG0_FRAME_START;
		cfg1 |= TDM_CFG1_RX_JUSTIFY;
		cfg1 &= ~TDM_CFG1_RX_EDGE;
		break;
	case DAI_FORMAT_LJ:
		cfg0 &= ~TDM_CFG0_FRAME_START;
		cfg1 &= ~TDM_CFG1_RX_JUSTIFY;
		cfg1 &= ~TDM_CFG1_RX_EDGE;
		break;
	default:
		return EINVAL;
	}

	if (pol & DAI_POLARITY_IB)
		cfg1 ^= TDM_CFG1_RX_EDGE;
	if (pol & DAI_POLARITY_IF)
		cfg0 ^= TDM_CFG0_FRAME_START;

	if (!(clk & DAI_CLOCK_CBM) || !(clk & DAI_CLOCK_CFM))
		return EINVAL;

	tascodec_write(sc, TDM_CFG0, cfg0);
	tascodec_write(sc, TDM_CFG1, cfg1);

	return 0;
}

int
tascodec_set_tdm_slot(void *cookie, int slot)
{
	struct tascodec_softc *sc = cookie;
	uint8_t cfg2, cfg3;

	if (slot < 0 || slot >= 16)
		return EINVAL;

	cfg2 = tascodec_read(sc, TDM_CFG2);
	cfg3 = tascodec_read(sc, TDM_CFG3);
	cfg2 &= ~TDM_CFG2_SCFG_MASK;
	cfg2 |= TDM_CFG2_SCFG_MONO_LEFT;
	cfg3 &= ~TDM_CFG3_RX_SLOT_L_MASK;
	cfg3 |= slot << TDM_CFG3_RX_SLOT_L_SHIFT;
	tascodec_write(sc, TDM_CFG2, cfg2);
	tascodec_write(sc, TDM_CFG3, cfg3);

	return 0;
}

/*
 * Mixer controls; the gain of the TAS2770 is determined by the
 * amplifier gain and digital volume control setting, but we only
 * expose the digital volume control setting through the mixer
 * interface.
 */
enum {
	TASCODEC_MASTER_VOL,
	TASCODEC_MASTER_MUTE,
	TASCODEC_OUTPUT_CLASS
};

int
tascodec_set_port(void *priv, mixer_ctrl_t *mc)
{
	struct tascodec_softc *sc = priv;
	u_char level;
	uint8_t mode;

	switch (mc->dev) {
	case TASCODEC_MASTER_VOL:
		level = mc->un.value.level[AUDIO_MIXER_LEVEL_MONO];
		sc->sc_dvc = (PB_CFG2_DVC_PCM_MIN * (255 - level)) / 255;
		tascodec_write(sc, PB_CFG2, sc->sc_dvc);
		return 0;

	case TASCODEC_MASTER_MUTE:
		sc->sc_mute = mc->un.ord ?
		    PWR_CTL_MODE_MUTE : PWR_CTL_MODE_ACTIVE;
		mode = tascodec_read(sc, PWR_CTL);
		if ((mode & PWR_CTL_MODE_MASK) == PWR_CTL_MODE_ACTIVE ||
		    (mode & PWR_CTL_MODE_MASK) == PWR_CTL_MODE_MUTE) {
			mode &= ~PWR_CTL_MODE_MASK;
			mode |= sc->sc_mute;
			tascodec_write(sc, PWR_CTL, mode);
		}
		return 0;
		
	}

	return EINVAL;
}

int
tascodec_get_port(void *priv, mixer_ctrl_t *mc)
{
	struct tascodec_softc *sc = priv;
	u_char level;

	switch (mc->dev) {
	case TASCODEC_MASTER_VOL:
		mc->un.value.num_channels = 1;
		level = 255 - ((255 * sc->sc_dvc) / PB_CFG2_DVC_PCM_MIN);
		mc->un.value.level[AUDIO_MIXER_LEVEL_MONO] = level;
		return 0;

	case TASCODEC_MASTER_MUTE:
		mc->un.ord = (sc->sc_mute == PWR_CTL_MODE_MUTE);
		return 0;
	}

	return EINVAL;
}

int
tascodec_query_devinfo(void *priv, mixer_devinfo_t *di)
{
	switch (di->index) {
	case TASCODEC_MASTER_VOL:
		di->mixer_class = TASCODEC_OUTPUT_CLASS;
		di->prev = AUDIO_MIXER_LAST;
		di->next = TASCODEC_MASTER_MUTE;
		strlcpy(di->label.name, AudioNmaster, sizeof(di->label.name));
		di->type = AUDIO_MIXER_VALUE;
		di->un.v.num_channels = 1;
		strlcpy(di->un.v.units.name, AudioNvolume,
		    sizeof(di->un.v.units.name));
		return 0;

	case TASCODEC_MASTER_MUTE:
		di->mixer_class = TASCODEC_OUTPUT_CLASS;
		di->prev = TASCODEC_MASTER_VOL;
		di->next = AUDIO_MIXER_LAST;
		strlcpy(di->label.name, AudioNmute, sizeof(di->label.name));
		di->type = AUDIO_MIXER_ENUM;
		di->un.e.num_mem = 2;
		di->un.e.member[0].ord = 0;
		strlcpy(di->un.e.member[0].label.name, AudioNoff,
		    MAX_AUDIO_DEV_LEN);
		di->un.e.member[1].ord = 1;
		strlcpy(di->un.e.member[1].label.name, AudioNon,
		    MAX_AUDIO_DEV_LEN);
		return 0;

	case TASCODEC_OUTPUT_CLASS:
		di->mixer_class = TASCODEC_OUTPUT_CLASS;
		di->next = di->prev = AUDIO_MIXER_LAST;
		strlcpy(di->label.name, AudioCoutputs, sizeof(di->label.name));
		di->type = AUDIO_MIXER_CLASS;
		return 0;
	}

	return ENXIO;
}

int
tascodec_trigger_output(void *cookie, void *start, void *end, int blksize,
    void (*intr)(void *), void *intrarg, struct audio_params *params)
{
	struct tascodec_softc *sc = cookie;

	tascodec_write(sc, PWR_CTL,
	    PWR_CTL_ISNS_PD | PWR_CTL_VSNS_PD | sc->sc_mute);
	return 0;
}

int
tascodec_halt_output(void *cookie)
{
	struct tascodec_softc *sc = cookie;

	tascodec_write(sc, PWR_CTL,
	    PWR_CTL_ISNS_PD | PWR_CTL_VSNS_PD | PWR_CTL_MODE_SHUTDOWN);
	return 0;
}

uint8_t
tascodec_read(struct tascodec_softc *sc, int reg)
{
	uint8_t cmd = reg;
	uint8_t val;
	int error;

	iic_acquire_bus(sc->sc_tag, I2C_F_POLL);
	error = iic_exec(sc->sc_tag, I2C_OP_READ_WITH_STOP, sc->sc_addr,
	    &cmd, sizeof cmd, &val, sizeof val, I2C_F_POLL);
	iic_release_bus(sc->sc_tag, I2C_F_POLL);

	if (error) {
		printf("%s: can't read register 0x%02x\n",
		    sc->sc_dev.dv_xname, reg);
		val = 0xff;
	}

	return val;
}

void
tascodec_write(struct tascodec_softc *sc, int reg, uint8_t val)
{
	uint8_t cmd = reg;
	int error;

	iic_acquire_bus(sc->sc_tag, I2C_F_POLL);
	error = iic_exec(sc->sc_tag, I2C_OP_WRITE_WITH_STOP, sc->sc_addr,
	    &cmd, sizeof cmd, &val, sizeof val, I2C_F_POLL);
	iic_release_bus(sc->sc_tag, I2C_F_POLL);

	if (error) {
		printf("%s: can't write register 0x%02x\n",
		    sc->sc_dev.dv_xname, reg);
	}
}
