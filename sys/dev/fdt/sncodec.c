/*	$OpenBSD: sncodec.c,v 1.4 2023/12/26 09:25:15 kettenis Exp $	*/
/*
 * Copyright (c) 2023 Mark Kettenis <kettenis@openbsd.org>
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

#define MODE_CTRL			0x02
#define  MODE_CTRL_BOP_SRC		(1 << 7)
#define  MODE_CTRL_ISNS_PD		(1 << 4)
#define  MODE_CTRL_VSNS_PD		(1 << 3)
#define  MODE_CTRL_MODE_MASK		(7 << 0)
#define  MODE_CTRL_MODE_ACTIVE		(0 << 0)
#define  MODE_CTRL_MODE_MUTE		(1 << 0)
#define  MODE_CTRL_MODE_SHUTDOWN	(2 << 0)
#define TDM_CFG0			0x08
#define  TDM_CFG0_FRAME_START		(1 << 0)
#define TDM_CFG1			0x09
#define  TDM_CFG1_RX_JUSTIFY		(1 << 6)
#define  TDM_CFG1_RX_OFFSET_MASK	(0x1f << 1)
#define  TDM_CFG1_RX_OFFSET_SHIFT	1
#define  TDM_CFG1_RX_EDGE		(1 << 0)
#define TDM_CFG2			0x0a
#define  TDM_CFG2_SCFG_MASK		(3 << 4)
#define  TDM_CFG2_SCFG_MONO_LEFT	(1 << 4)
#define  TDM_CFG2_SCFG_MONO_RIGHT	(2 << 4)
#define  TDM_CFG2_SCFG_STEREO_DOWNMIX	(3 << 4)
#define TDM_CFG3			0x0c
#define  TDM_CFG3_RX_SLOT_R_MASK	0xf0
#define  TDM_CFG3_RX_SLOT_R_SHIFT	4
#define  TDM_CFG3_RX_SLOT_L_MASK	0x0f
#define  TDM_CFG3_RX_SLOT_L_SHIFT	0
#define DVC				0x1a
#define  DVC_LVL_MIN			0xc9
#define  DVC_LVL_30DB			0x3c
#define BOP_CFG0			0x1d

uint8_t sncodec_bop_cfg[] = {
	0x01, 0x32, 0x02, 0x22, 0x83, 0x2d, 0x80, 0x02, 0x06,
	0x32, 0x40, 0x30, 0x02, 0x06, 0x38, 0x40, 0x30, 0x02,
	0x06, 0x3e, 0x37, 0x30, 0xff, 0xe6
};

struct sncodec_softc {
	struct device		sc_dev;
	i2c_tag_t		sc_tag;
	i2c_addr_t		sc_addr;

	struct dai_device	sc_dai;
	uint8_t			sc_dvc;
	uint8_t			sc_mute;
};

int	sncodec_match(struct device *, void *, void *);
void	sncodec_attach(struct device *, struct device *, void *);
int	sncodec_activate(struct device *, int);

const struct cfattach sncodec_ca = {
	sizeof(struct sncodec_softc), sncodec_match, sncodec_attach,
	NULL, sncodec_activate
};

struct cfdriver sncodec_cd = {
	NULL, "sncodec", DV_DULL
};

int	sncodec_set_format(void *, uint32_t, uint32_t, uint32_t);
int	sncodec_set_tdm_slot(void *, int);

int	sncodec_set_port(void *, mixer_ctrl_t *);
int	sncodec_get_port(void *, mixer_ctrl_t *);
int	sncodec_query_devinfo(void *, mixer_devinfo_t *);
int	sncodec_trigger_output(void *, void *, void *, int,
	    void (*)(void *), void *, struct audio_params *);
int	sncodec_halt_output(void *);

const struct audio_hw_if sncodec_hw_if = {
	.set_port = sncodec_set_port,
	.get_port = sncodec_get_port,
	.query_devinfo = sncodec_query_devinfo,
	.trigger_output = sncodec_trigger_output,
	.halt_output = sncodec_halt_output,
};

uint8_t	sncodec_read(struct sncodec_softc *, int);
void	sncodec_write(struct sncodec_softc *, int, uint8_t);

int
sncodec_match(struct device *parent, void *match, void *aux)
{
	struct i2c_attach_args *ia = aux;

	return iic_is_compatible(ia, "ti,tas2764");
}

void
sncodec_attach(struct device *parent, struct device *self, void *aux)
{
	struct sncodec_softc *sc = (struct sncodec_softc *)self;
	struct i2c_attach_args *ia = aux;
	int node = *(int *)ia->ia_cookie;
	uint32_t *sdz_gpio;
	int sdz_gpiolen;
	uint8_t cfg2;
	int i;

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
	sc->sc_dvc = DVC_LVL_30DB;
	sc->sc_mute = MODE_CTRL_MODE_ACTIVE;
	sncodec_write(sc, DVC, sc->sc_dvc);

	/* Default to stereo downmix mode for now. */
	cfg2 = sncodec_read(sc, TDM_CFG2);
	cfg2 &= ~TDM_CFG2_SCFG_MASK;
	cfg2 |= TDM_CFG2_SCFG_STEREO_DOWNMIX;
	sncodec_write(sc, TDM_CFG2, cfg2);

	/* Configure brownout prevention. */
	for (i = 0; i < nitems(sncodec_bop_cfg); i++)
		sncodec_write(sc, BOP_CFG0 + i, sncodec_bop_cfg[i]);

	sc->sc_dai.dd_node = node;
	sc->sc_dai.dd_cookie = sc;
	sc->sc_dai.dd_hw_if = &sncodec_hw_if;
	sc->sc_dai.dd_set_format = sncodec_set_format;
	sc->sc_dai.dd_set_tdm_slot = sncodec_set_tdm_slot;
	dai_register(&sc->sc_dai);
}

int
sncodec_activate(struct device *self, int act)
{
	struct sncodec_softc *sc = (struct sncodec_softc *)self;

	switch (act) {
	case DVACT_POWERDOWN:
		sncodec_write(sc, MODE_CTRL, MODE_CTRL_ISNS_PD |
		    MODE_CTRL_VSNS_PD | MODE_CTRL_MODE_SHUTDOWN);
		break;
	}

	return 0;
}

int
sncodec_set_format(void *cookie, uint32_t fmt, uint32_t pol,
    uint32_t clk)
{
	struct sncodec_softc *sc = cookie;
	uint8_t cfg0, cfg1;

	cfg0 = sncodec_read(sc, TDM_CFG0);
	cfg1 = sncodec_read(sc, TDM_CFG1);
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

	sncodec_write(sc, TDM_CFG0, cfg0);
	sncodec_write(sc, TDM_CFG1, cfg1);

	return 0;
}

int
sncodec_set_tdm_slot(void *cookie, int slot)
{
	struct sncodec_softc *sc = cookie;
	uint8_t cfg2, cfg3;

	if (slot < 0 || slot >= 16)
		return EINVAL;

	cfg2 = sncodec_read(sc, TDM_CFG2);
	cfg3 = sncodec_read(sc, TDM_CFG3);
	cfg2 &= ~TDM_CFG2_SCFG_MASK;
	cfg2 |= TDM_CFG2_SCFG_MONO_LEFT;
	cfg3 &= ~TDM_CFG3_RX_SLOT_L_MASK;
	cfg3 |= slot << TDM_CFG3_RX_SLOT_L_SHIFT;
	sncodec_write(sc, TDM_CFG2, cfg2);
	sncodec_write(sc, TDM_CFG3, cfg3);

	return 0;
}

/*
 * Mixer controls; the gain of the TAS2764 is determined by the
 * amplifier gain and digital volume control setting, but we only
 * expose the digital volume control setting through the mixer
 * interface.
 */
enum {
	SNCODEC_MASTER_VOL,
	SNCODEC_MASTER_MUTE,
	SNCODEC_OUTPUT_CLASS
};

int
sncodec_set_port(void *priv, mixer_ctrl_t *mc)
{
	struct sncodec_softc *sc = priv;
	u_char level;
	uint8_t mode;

	switch (mc->dev) {
	case SNCODEC_MASTER_VOL:
		level = mc->un.value.level[AUDIO_MIXER_LEVEL_MONO];
		sc->sc_dvc = (DVC_LVL_MIN * (255 - level)) / 255;
		sncodec_write(sc, DVC, sc->sc_dvc);
		return 0;

	case SNCODEC_MASTER_MUTE:
		sc->sc_mute = mc->un.ord ?
		    MODE_CTRL_MODE_MUTE : MODE_CTRL_MODE_ACTIVE;
		mode = sncodec_read(sc, MODE_CTRL);
		if ((mode & MODE_CTRL_MODE_MASK) == MODE_CTRL_MODE_ACTIVE ||
		    (mode & MODE_CTRL_MODE_MASK) == MODE_CTRL_MODE_MUTE) {
			mode &= ~MODE_CTRL_MODE_MASK;
			mode |= sc->sc_mute;
			sncodec_write(sc, MODE_CTRL, mode);
		}
		return 0;
	}

	return EINVAL;
}

int
sncodec_get_port(void *priv, mixer_ctrl_t *mc)
{
	struct sncodec_softc *sc = priv;
	u_char level;

	switch (mc->dev) {
	case SNCODEC_MASTER_VOL:
		mc->un.value.num_channels = 1;
		level = 255 - ((255 * sc->sc_dvc) / DVC_LVL_MIN);
		mc->un.value.level[AUDIO_MIXER_LEVEL_MONO] = level;
		return 0;

	case SNCODEC_MASTER_MUTE:
		mc->un.ord = (sc->sc_mute == MODE_CTRL_MODE_MUTE);
		return 0;
	}

	return EINVAL;
}

int
sncodec_query_devinfo(void *priv, mixer_devinfo_t *di)
{
	switch (di->index) {
	case SNCODEC_MASTER_VOL:
		di->mixer_class = SNCODEC_OUTPUT_CLASS;
		di->prev = AUDIO_MIXER_LAST;
		di->next = SNCODEC_MASTER_MUTE;
		strlcpy(di->label.name, AudioNmaster, sizeof(di->label.name));
		di->type = AUDIO_MIXER_VALUE;
		di->un.v.num_channels = 1;
		strlcpy(di->un.v.units.name, AudioNvolume,
		    sizeof(di->un.v.units.name));
		return 0;

	case SNCODEC_MASTER_MUTE:
		di->mixer_class = SNCODEC_OUTPUT_CLASS;
		di->prev = SNCODEC_MASTER_VOL;
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

	case SNCODEC_OUTPUT_CLASS:
		di->mixer_class = SNCODEC_OUTPUT_CLASS;
		di->next = di->prev = AUDIO_MIXER_LAST;
		strlcpy(di->label.name, AudioCoutputs, sizeof(di->label.name));
		di->type = AUDIO_MIXER_CLASS;
		return 0;
	}

	return ENXIO;
}

int
sncodec_trigger_output(void *cookie, void *start, void *end, int blksize,
    void (*intr)(void *), void *intrarg, struct audio_params *params)
{
	struct sncodec_softc *sc = cookie;

	sncodec_write(sc, MODE_CTRL, MODE_CTRL_BOP_SRC |
	    MODE_CTRL_ISNS_PD | MODE_CTRL_VSNS_PD | sc->sc_mute);
	return 0;
}

int
sncodec_halt_output(void *cookie)
{
	struct sncodec_softc *sc = cookie;

	sncodec_write(sc, MODE_CTRL, MODE_CTRL_BOP_SRC |
	    MODE_CTRL_ISNS_PD | MODE_CTRL_VSNS_PD | MODE_CTRL_MODE_SHUTDOWN);
	return 0;
}

uint8_t
sncodec_read(struct sncodec_softc *sc, int reg)
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
sncodec_write(struct sncodec_softc *sc, int reg, uint8_t val)
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
