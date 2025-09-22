/*	$OpenBSD: envy.c,v 1.89 2025/09/20 13:50:33 mpi Exp $	*/
/*
 * Copyright (c) 2007 Alexandre Ratchov <alex@caoua.org>
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

/*
 * TODO:
 *
 * - add nspdin, nspdout, to struct envy_card
 *
 * - use eeprom version rather isht flag
 *
 * - implement HT mixer, midi uart, spdif, init ADC/DACs for >48kHz modes
 *
 */

#include "midi.h"
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/audioio.h>
#include <sys/malloc.h>
#include <dev/audio_if.h>
#include <dev/midi_if.h>
#include <dev/ic/ac97.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>
#include <dev/pci/envyvar.h>
#include <dev/pci/envyreg.h>
#include <machine/bus.h>
#include <uvm/uvm.h>

#ifdef ENVY_DEBUG
#define DPRINTF(...) do { if (envydebug) printf(__VA_ARGS__); } while(0)
#define DPRINTFN(n, ...) do { if (envydebug > (n)) printf(__VA_ARGS__); } while(0)
int envydebug = 1;
#else
#define DPRINTF(...) do {} while(0)
#define DPRINTFN(n, ...) do {} while(0)
#endif
#define DEVNAME(sc) ((sc)->dev.dv_xname)

int  envymatch(struct device *, void *, void *);
void envyattach(struct device *, struct device *, void *);
int  envydetach(struct device *, int);
int  envyactivate(struct device *, int);

int  envy_ccs_read(struct envy_softc *, int);
void envy_ccs_write(struct envy_softc *, int, int);
int  envy_mt_read_1(struct envy_softc *, int);
void envy_mt_write_1(struct envy_softc *, int, int);
int  envy_mt_read_2(struct envy_softc *, int);
void envy_mt_write_2(struct envy_softc *, int, int);
int  envy_mt_read_4(struct envy_softc *, int);
void envy_mt_write_4(struct envy_softc *, int, int);
int  envy_cci_read(struct envy_softc *, int);
void envy_cci_write(struct envy_softc *, int, int);
void envy_i2c_wait(struct envy_softc *);
int  envy_i2c_read(struct envy_softc *, int, int);
void envy_i2c_write(struct envy_softc *, int, int, int);
int  envy_gpio_getstate(struct envy_softc *);
void envy_gpio_setstate(struct envy_softc *, int);
int  envy_gpio_getmask(struct envy_softc *);
void envy_gpio_setmask(struct envy_softc *, int);
int  envy_gpio_getdir(struct envy_softc *);
void envy_gpio_setdir(struct envy_softc *, int);
void envy_gpio_i2c_start_bit(struct envy_softc *, int, int);
void envy_gpio_i2c_stop_bit(struct envy_softc *, int, int);
void envy_gpio_i2c_byte_out(struct envy_softc *, int, int, int);
int  envy_eeprom_gpioxxx(struct envy_softc *, int);
void envy_midi_wait(struct envy_softc *);
void envy_reset(struct envy_softc *);
int  envy_codec_read(struct envy_softc *, int, int);
void envy_codec_write(struct envy_softc *, int, int, int);
void envy_pintr(struct envy_softc *);
int  envy_intr(void *);

int envy_lineout_getsrc(struct envy_softc *, int);
void envy_lineout_setsrc(struct envy_softc *, int, int);
int envy_spdout_getsrc(struct envy_softc *, int);
void envy_spdout_setsrc(struct envy_softc *, int, int);
void envy_mon_getvol(struct envy_softc *, int, int, int *);
void envy_mon_setvol(struct envy_softc *, int, int, int);

int envy_open(void *, int);
void envy_close(void *);
void *envy_allocm(void *, int, size_t, int, int);
void envy_freem(void *, void *, int);
int envy_set_params(void *, int, int, struct audio_params *,
    struct audio_params *);
int envy_round_blocksize(void *, int);
int envy_trigger_output(void *, void *, void *, int,
    void (*)(void *), void *, struct audio_params *);
int envy_trigger_input(void *, void *, void *, int,
    void (*)(void *), void *, struct audio_params *);
int envy_halt_output(void *);
int envy_halt_input(void *);
int envy_query_devinfo(void *, struct mixer_devinfo *);
int envy_get_port(void *, struct mixer_ctrl *);
int envy_set_port(void *, struct mixer_ctrl *);
#if NMIDI > 0
int envy_midi_open(void *, int, void (*)(void *, int),
    void (*)(void *), void *);
void envy_midi_close(void *);
int envy_midi_output(void *, int);
void envy_midi_getinfo(void *, struct midi_info *);
#endif

int  envy_ac97_wait(struct envy_softc *);
int  envy_ac97_attach_codec(void *, struct ac97_codec_if *);
int  envy_ac97_read_codec(void *, u_int8_t, u_int16_t *);
int  envy_ac97_write_codec(void *, u_int8_t, u_int16_t);
void envy_ac97_reset_codec(void *);
enum ac97_host_flags envy_ac97_flags_codec(void *);

void delta_init(struct envy_softc *);
void delta_codec_write(struct envy_softc *, int, int, int);

void ap192k_init(struct envy_softc *);
void ap192k_codec_write(struct envy_softc *, int, int, int);
void ap192k_set_rate(struct envy_softc *, int);

void ewx_codec_write(struct envy_softc *, int, int, int);

void revo51_init(struct envy_softc *);
void revo51_codec_write(struct envy_softc *, int, int, int);

void envy_ac97_init(struct envy_softc *);
void dynex_sc51_init(struct envy_softc *);

void julia_init(struct envy_softc *);
void julia_codec_write(struct envy_softc *, int, int, int);
void julia_set_rate(struct envy_softc *, int);

void unkenvy_init(struct envy_softc *);
void unkenvy_codec_write(struct envy_softc *, int, int, int);
int unkenvy_codec_ndev(struct envy_softc *);

int ak4524_dac_ndev(struct envy_softc *);
void ak4524_dac_devinfo(struct envy_softc *, struct mixer_devinfo *, int);
void ak4524_dac_get(struct envy_softc *, struct mixer_ctrl *, int);
int ak4524_dac_set(struct envy_softc *, struct mixer_ctrl *, int);
int ak4524_adc_ndev(struct envy_softc *);
void ak4524_adc_devinfo(struct envy_softc *, struct mixer_devinfo *, int);
void ak4524_adc_get(struct envy_softc *, struct mixer_ctrl *, int);
int ak4524_adc_set(struct envy_softc *, struct mixer_ctrl *, int);

int ak4358_dac_ndev(struct envy_softc *);
void ak4358_dac_devinfo(struct envy_softc *, struct mixer_devinfo *, int);
void ak4358_dac_get(struct envy_softc *, struct mixer_ctrl *, int);
int ak4358_dac_set(struct envy_softc *, struct mixer_ctrl *, int);
void ak4358_set_rate(struct envy_softc *, int);

int ak5365_adc_ndev(struct envy_softc *);
void ak5365_adc_devinfo(struct envy_softc *, struct mixer_devinfo *, int);
void ak5365_adc_get(struct envy_softc *, struct mixer_ctrl *, int);
int ak5365_adc_set(struct envy_softc *, struct mixer_ctrl *, int);

const struct cfattach envy_ca = {
	sizeof(struct envy_softc), envymatch, envyattach, envydetach,
	envyactivate
};

struct cfdriver envy_cd = {
	NULL, "envy", DV_DULL
};

const struct audio_hw_if envy_hw_if = {
	.open = envy_open,
	.close = envy_close,
	.set_params = envy_set_params,
	.round_blocksize = envy_round_blocksize,
	.halt_output = envy_halt_output,
	.halt_input = envy_halt_input,
	.set_port = envy_set_port,
	.get_port = envy_get_port,
	.query_devinfo = envy_query_devinfo,
	.allocm = envy_allocm,
	.freem = envy_freem,
	.trigger_output = envy_trigger_output,
	.trigger_input = envy_trigger_input,
};

#if NMIDI > 0
const struct midi_hw_if envy_midi_hw_if = {
	envy_midi_open,
	envy_midi_close,
	envy_midi_output,
	NULL,				/* flush */
	envy_midi_getinfo,
	NULL				/* ioctl */
};
#endif

struct pci_matchid envy_matchids[] = {
	{ PCI_VENDOR_ICENSEMBLE, PCI_PRODUCT_ICENSEMBLE_ICE1712 },
	{ PCI_VENDOR_ICENSEMBLE, PCI_PRODUCT_ICENSEMBLE_VT172X }
};

/*
 * correspondence between rates (in frames per second)
 * and values of rate register
 */
struct {
	int rate, reg;
} envy_rates[] = {
	{ 8000, 0x6}, { 9600, 0x3}, {11025, 0xa}, {12000, 2}, {16000, 5},
	{22050, 0x9}, {24000, 0x1}, {32000, 0x4}, {44100, 8}, {48000, 0},
	{64000, 0xf}, {88200, 0xb}, {96000, 0x7},
	{176400, 0xc}, {192000, 0xe},
	{-1, -1}
};

/*
 * ESI Julia cards don't have EEPROM, use this copy
 */
static unsigned char julia_eeprom[ENVY_EEPROM_MAXSZ] = {
	/* gpio mask/dir/state is from linux */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x20, 0x80, 0xf8, 0xc3,
	0x9f, 0xff, 0x7f,
	0x60, 0x00, 0x7f,
	0x0a, 0x00, 0x00
};

struct envy_codec ak4524_dac = {
	"ak4524 dac", ak4524_dac_ndev, ak4524_dac_devinfo, ak4524_dac_get, ak4524_dac_set
}, ak4524_adc = {
	"ak4524 adc", ak4524_adc_ndev, ak4524_adc_devinfo, ak4524_adc_get, ak4524_adc_set
}, ak4358_dac = {
	"ak4358 dac", ak4358_dac_ndev, ak4358_dac_devinfo, ak4358_dac_get, ak4358_dac_set
}, ak5365_adc = {
	"ak5365 adc", ak5365_adc_ndev, ak5365_adc_devinfo, ak5365_adc_get, ak5365_adc_set
}, unkenvy_codec = {
	"unknown codec", unkenvy_codec_ndev, NULL, NULL, NULL
};

/*
 * array with vendor/product sub-IDs to card info
 */
struct envy_card envy_cards[] = {
	{
		PCI_ID_CODE(0x1412, 0xd630),
		"M-Audio Delta 1010",
		8, &ak4524_adc, 8, &ak4524_dac, 1,
		delta_init,
		delta_codec_write,
	}, {
		PCI_ID_CODE(0x1412, 0xd632),
		"M-Audio Delta 66",
		4, &ak4524_adc, 4, &ak4524_dac, 0,
		delta_init,
		delta_codec_write,
	}, {
#define ENVY_SUBID_DELTA44	(PCI_ID_CODE(0x1412, 0xd633))
		PCI_ID_CODE(0x1412, 0xd633),
		"M-Audio Delta 44",
		4, &ak4524_adc, 4, &ak4524_dac, 0,
		delta_init,
		delta_codec_write,
	}, {
		PCI_ID_CODE(0x1412, 0xd63b),
		"M-Audio Delta 1010LT",
		8, &ak4524_adc, 8, &ak4524_dac, 1,
		delta_init,
		delta_codec_write,
	}, {
		PCI_ID_CODE(0x1412, 0xd634),
		"M-Audio Audiophile 2496",
		2, &ak4524_adc, 2, &ak4524_dac, 1,
		delta_init,
		delta_codec_write,
	}, {
		PCI_ID_CODE(0x153b, 0x1130),
		"Terratec EWX 24/96",
		2, &ak4524_adc, 2, &ak4524_dac, 1,
		delta_init,
		ewx_codec_write,
	}, {
		0,
		"unknown 1712-based card",
		8, &unkenvy_codec, 8, &unkenvy_codec, 1,
		unkenvy_init,
		unkenvy_codec_write
	}
}, envy_cards_ht[] = {
	{
		PCI_ID_CODE(0x3031, 0x4553),
		"ESI Julia",
		2, &unkenvy_codec, 2, &ak4358_dac, 1,
		julia_init,
		julia_codec_write,
		julia_set_rate,
		julia_eeprom
	}, {
		PCI_ID_CODE(0x1412, 0x3632),
		"M-Audio Audiophile 192k",
		2, &unkenvy_codec, 2, &ak4358_dac, 1,
		ap192k_init,
		ap192k_codec_write,
		ap192k_set_rate
	}, {
		PCI_ID_CODE(0x1412, 0x3631),
		"M-Audio Revolution 5.1",
		2, &ak5365_adc, 6, &ak4358_dac, 1,
		revo51_init,
		revo51_codec_write
	}, {
		PCI_ID_CODE(0x1412, 0x2403),
		"VIA Tremor 5.1",
		2, &unkenvy_codec, 6, &unkenvy_codec, 1,
		envy_ac97_init,
		unkenvy_codec_write
	}, {
		PCI_ID_CODE(0x14c3, 0x1705),
		"Dynex DX-SC51",
		2, &unkenvy_codec, 6, &unkenvy_codec, 0,
		dynex_sc51_init,
		unkenvy_codec_write
	}, {
		0,
		"unknown 1724-based card",
		2, &unkenvy_codec, 8, &unkenvy_codec, 1,
		unkenvy_init,
		unkenvy_codec_write
	}
};


/*
 * M-Audio Delta specific code
 */

void
delta_init(struct envy_softc *sc)
{
	int dev;

	for (dev = 0; dev < sc->card->noch / 2; dev++) {
		envy_codec_write(sc, dev, AK4524_RST, 0x0);
		delay(300);
		envy_codec_write(sc, dev, AK4524_RST,
		    AK4524_RST_AD | AK4524_RST_DA);
		envy_codec_write(sc, dev, AK4524_FMT,
		    AK4524_FMT_IIS24);
		sc->shadow[dev][AK4524_DEEMVOL] = AK4524_DEEM_OFF;
		sc->shadow[dev][AK4524_ADC_GAIN0] = 0x7f;
		sc->shadow[dev][AK4524_ADC_GAIN1] = 0x7f;
		sc->shadow[dev][AK4524_DAC_GAIN0] = 0x7f;
		sc->shadow[dev][AK4524_DAC_GAIN1] = 0x7f;
	}
}

void
delta_codec_write(struct envy_softc *sc, int dev, int addr, int data)
{
	int bits, i, reg;
	int clk, dout, csmask, cs;

	/*
	 * GPIO pin numbers
	 */
	if (sc->card->subid == ENVY_SUBID_DELTA44) {
		clk = 0x20;
		dout = 0x10;
		csmask = 0xc0;
		cs = dev ? 0x40 : 0x80;
	} else {
		clk = 0x2;
		dout = 0x8;
		csmask = 0x70;
		cs = dev << 4;
	}

	reg = envy_gpio_getstate(sc);
	reg &= ~csmask;
	reg |= cs;
	envy_gpio_setstate(sc, reg);
	delay(1);

	bits  = 0xa000 | (addr << 8) | data;
	for (i = 0; i < 16; i++) {
		reg &= ~(clk | dout);
		reg |= (bits & 0x8000) ? dout : 0;
		envy_gpio_setstate(sc, reg);
		delay(1);

		reg |= clk;
		envy_gpio_setstate(sc, reg);
		delay(1);
		bits <<= 1;
	}

	reg |= csmask;
	envy_gpio_setstate(sc, reg);
	delay(1);
}

/*
 * M-Audio Audiophile 192 specific code
 */

/*
 * GPIO pin numbers
 */
#define AP192K_GPIO_CLK		0x2
#define AP192K_GPIO_DOUT	0x8
#define AP192K_GPIO_CSMASK	0x30
#define AP192K_GPIO_CS(dev)	((dev) << 4)

#define AP192K_AK5385_CKS0	(1 << 8)
#define AP192K_AK5385_DFS0	(1 << 9)
#define AP192K_AK5385_DFS1	(1 << 10)
#define AP192K_AK5385_PWR	(1 << 11)
#define AP192K_AK5385_SPD_MASK	0x700

void
ap192k_init(struct envy_softc *sc)
{
	int i, reg;

	/* AK4358 */
	envy_codec_write(sc, 0, 0, 0);	/* reset */
	delay(300);
	envy_codec_write(sc, 0, 0, 0x87);	/* i2s mode */
	delay(1);
	for (i = 0; i < sc->card->noch; i++) {
		sc->shadow[0][AK4358_ATT(i)] = 0xff;
	}

	/* AK5385 */
	delay(1);
	reg = envy_gpio_getstate(sc);
	reg &= ~(AP192K_AK5385_PWR | AP192K_AK5385_SPD_MASK);
	envy_gpio_setstate(sc, reg);
	reg |= AP192K_AK5385_PWR;
	envy_gpio_setstate(sc, reg);
}

void
ap192k_codec_write(struct envy_softc *sc, int dev, int addr, int data)
{
	int bits, i, reg;

	reg = envy_gpio_getstate(sc);
	reg &= ~AP192K_GPIO_CSMASK;
	reg |=  AP192K_GPIO_CS(dev);
	envy_gpio_setstate(sc, reg);
	delay(1);

	bits  = 0xa000 | (addr << 8) | data;
	for (i = 0; i < 16; i++) {
		reg &= ~(AP192K_GPIO_CLK | AP192K_GPIO_DOUT);
		reg |= (bits & 0x8000) ? AP192K_GPIO_DOUT : 0;
		envy_gpio_setstate(sc, reg);
		delay(1);

		reg |= AP192K_GPIO_CLK;
		envy_gpio_setstate(sc, reg);
		delay(1);
		bits <<= 1;
	}

	reg |= AP192K_GPIO_CSMASK;
	envy_gpio_setstate(sc, reg);
	delay(1);
}

void
ap192k_set_rate(struct envy_softc *sc, int rate)
{
	int reg;

	/* set AK5385 clock params */
	reg = envy_gpio_getstate(sc) & ~(AP192K_AK5385_SPD_MASK);
	if (rate > 96000)
		reg |= AP192K_AK5385_CKS0 | AP192K_AK5385_DFS1;
	else if (rate > 48000)
		reg |= AP192K_AK5385_DFS0;
	envy_gpio_setstate(sc, reg);

	ak4358_set_rate(sc, rate);
}

/*
 * Terratec EWX specific code
 */

/*
 * GPIO pin numbers
 */
#define EWX_GPIO_CSMASK		0x01
#define EWX_GPIO_DOUT		0x10
#define EWX_GPIO_CLK		0x20

void
ewx_codec_write(struct envy_softc *sc, int dev, int addr, int data)
{
	int bits, i, reg;

	reg = envy_gpio_getstate(sc);
	reg |= (EWX_GPIO_CSMASK | EWX_GPIO_CLK);
	envy_gpio_setstate(sc, reg);
	delay(1);

	bits = 0xa000 | (addr << 8) | data;
	for (i = 0; i < 16; i++) {
		reg &= ~(EWX_GPIO_CLK | EWX_GPIO_DOUT);
		reg |= (bits & 0x8000) ? EWX_GPIO_DOUT : 0;
		envy_gpio_setstate(sc, reg);
		delay(1);

		reg |= EWX_GPIO_CLK;
		envy_gpio_setstate(sc, reg);
		delay(1);
		bits <<= 1;
	}

	reg &= ~EWX_GPIO_CSMASK;
	envy_gpio_setstate(sc, reg);
	delay(1);

	reg |= EWX_GPIO_CSMASK;
	envy_gpio_setstate(sc, reg);
	delay(1);
}


/*
 * M-Audio Revolution 5.1 specific code
 */

#define REVO51_GPIO_CLK		0x2
#define REVO51_GPIO_DOUT	0x8
#define REVO51_GPIO_CSMASK	0x30
#define REVO51_GPIO_CS(dev)	((dev) ? 0x10 : 0x20)
#define REVO51_MUTE		0x400000
#define REVO51_PT2258S_SDA	0x40
#define REVO51_PT2258S_SCL	0x80
#define REVO51_PT2258S_ADDR	0x80
#define REVO51_PT2258S_MUTE	6

void
revo51_init(struct envy_softc *sc)
{
	int i, reg;

	/* AK4358 */
	envy_codec_write(sc, 0, 0, 0);	/* reset */
	delay(300);
	envy_codec_write(sc, 0, 0, 0x87);	/* i2s mode */
	for (i = 0; i < sc->card->noch; i++) {
		sc->shadow[0][AK4358_ATT(i)] = 0xff;
	}

	/* AK5365 */
	envy_codec_write(sc, 1, AK5365_RST, 0);	/* reset */
	delay(300);
	envy_codec_write(sc, 1, AK5365_CTRL, AK5365_CTRL_I2S);	/* i2s mode */
	envy_codec_write(sc, 1, AK5365_RST , AK5365_RST_NORM);
	sc->shadow[1][AK5365_ATT(0)] = 0x7f;
	sc->shadow[1][AK5365_ATT(1)] = 0x7f;

	/* PT2258S */
	envy_codec_write(sc, 2, REVO51_PT2258S_MUTE, 0xc0);	/* reset */
	envy_codec_write(sc, 2, REVO51_PT2258S_MUTE, 0xf9);	/* mute */

	reg = envy_gpio_getstate(sc);
	reg |= REVO51_MUTE;
	envy_gpio_setstate(sc, reg);
}

void
revo51_codec_write(struct envy_softc *sc, int dev, int addr, int data)
{
	int attn, bits, mask, reg;
	int xlat[6] = {0x90, 0x50, 0x10, 0x30, 0x70, 0xb0};

	/* AK4358 & AK5365 */
	if (dev < 2) {
		reg = envy_gpio_getstate(sc);
		reg &= ~REVO51_GPIO_CSMASK;
		reg |=  REVO51_GPIO_CS(dev);
		envy_gpio_setstate(sc, reg);
		delay(1);

		bits  = 0xa000 | (addr << 8) | data;
		for (mask = 0x8000; mask != 0; mask >>= 1) {
			reg &= ~(REVO51_GPIO_CLK | REVO51_GPIO_DOUT);
			reg |= (bits & mask) ? REVO51_GPIO_DOUT : 0;
			envy_gpio_setstate(sc, reg);
			delay(1);

			reg |= REVO51_GPIO_CLK;
			envy_gpio_setstate(sc, reg);
			delay(1);
		}

		reg |= REVO51_GPIO_CSMASK;
		envy_gpio_setstate(sc, reg);
		delay(1);
		return;
	}

	/* PT2258S */
	envy_gpio_i2c_start_bit(sc, REVO51_PT2258S_SDA, REVO51_PT2258S_SCL);
	envy_gpio_i2c_byte_out(sc, REVO51_PT2258S_SDA, REVO51_PT2258S_SCL,
	    REVO51_PT2258S_ADDR);

	if (addr == REVO51_PT2258S_MUTE) {
		envy_gpio_i2c_byte_out(sc, REVO51_PT2258S_SDA,
		    REVO51_PT2258S_SCL, data);
	} else {
		/* 1's digit */
		attn = data % 10;
		attn += xlat[addr];
		envy_gpio_i2c_byte_out(sc, REVO51_PT2258S_SDA,
		    REVO51_PT2258S_SCL, attn);

		/* 10's digit */
		attn = data / 10;
		attn += xlat[addr] - 0x10;
		envy_gpio_i2c_byte_out(sc, REVO51_PT2258S_SDA,
		    REVO51_PT2258S_SCL, attn);
	}

	envy_gpio_i2c_stop_bit(sc, REVO51_PT2258S_SDA, REVO51_PT2258S_SCL);
}

/*
 * Generic AC'97 initialization
 */

void
envy_ac97_init(struct envy_softc *sc)
{
	sc->isac97 = 1;
	sc->host_if.arg = sc;
	sc->host_if.attach = envy_ac97_attach_codec;
	sc->host_if.read = envy_ac97_read_codec;
	sc->host_if.write = envy_ac97_write_codec;
	sc->host_if.reset = envy_ac97_reset_codec;
	sc->host_if.flags = envy_ac97_flags_codec;

	if (ac97_attach(&sc->host_if) != 0)
		printf("%s: can't attach ac97\n", DEVNAME(sc));
}

/*
 * Dynex
 */

void
dynex_sc51_init(struct envy_softc *sc)
{
	sc->codec_flags |= AC97_HOST_VT1616_DYNEX;
	envy_ac97_init(sc);
}

/*
 * ESI Julia specific code
 */

#define JULIA_AK5385_CKS0	(1 << 8)
#define JULIA_AK5385_DFS1	(1 << 9)
#define JULIA_AK5385_DFS0	(1 << 10)
#define JULIA_AK5385_CKS1	(1 << 14)
#define JULIA_AK5385_MASK	0x4700

void
julia_init(struct envy_softc *sc)
{
	int i;

	envy_codec_write(sc, 0, 0, 0);	/* reset */
	delay(300);
	envy_codec_write(sc, 0, 0, 0x87);	/* i2s mode */
	for (i = 0; i < sc->card->noch; i++) {
		sc->shadow[0][AK4358_ATT(i)] = 0xff;
	}
}

void
julia_codec_write(struct envy_softc *sc, int dev, int addr, int data)
{
#define JULIA_AK4358_ADDR	0x11
	envy_i2c_write(sc, JULIA_AK4358_ADDR, addr, data);
}

void
julia_set_rate(struct envy_softc *sc, int rate)
{
	int reg;

	/* set AK5385 clock params */
	reg = envy_gpio_getstate(sc) & ~(JULIA_AK5385_MASK);
	if (rate > 96000)
		reg |= JULIA_AK5385_CKS0 | JULIA_AK5385_DFS1;
	else if (rate > 48000)
		reg |= JULIA_AK5385_DFS0;
	envy_gpio_setstate(sc, reg);

	ak4358_set_rate(sc, rate);
}

/*
 * unknown card, ignore codecs setup and hope it works with the power on
 * settings
 */

void
unkenvy_init(struct envy_softc *sc)
{
}

void
unkenvy_codec_write(struct envy_softc *sc, int dev, int addr, int data)
{
}

int
unkenvy_codec_ndev(struct envy_softc *sc)
{
	return 0;
}

/*
 * AK 4358 DAC specific code
 */
int
ak4358_dac_ndev(struct envy_softc *sc)
{
	/* 1 volume knob per channel */
	return sc->card->noch;
}

void
ak4358_dac_devinfo(struct envy_softc *sc, struct mixer_devinfo *dev, int idx)
{
	dev->type = AUDIO_MIXER_VALUE;
	dev->mixer_class = ENVY_MIX_CLASSOUT;
	dev->un.v.delta = 2;
	dev->un.v.num_channels = 1;
	snprintf(dev->label.name, MAX_AUDIO_DEV_LEN,
	    AudioNline "-%d", idx);
	strlcpy(dev->un.v.units.name, AudioNvolume,
	    MAX_AUDIO_DEV_LEN);
}

void
ak4358_dac_get(struct envy_softc *sc, struct mixer_ctrl *ctl, int idx)
{
	int val;

	val = envy_codec_read(sc, 0, AK4358_ATT(idx)) & ~AK4358_ATT_EN;
	ctl->un.value.num_channels = 1;
	ctl->un.value.level[0] = 2 * val;
}

int
ak4358_dac_set(struct envy_softc *sc, struct mixer_ctrl *ctl, int idx)
{
	int val;

	if (ctl->un.value.num_channels != 1)
		return EINVAL;
	val = ctl->un.value.level[0] / 2;
	envy_codec_write(sc, 0, AK4358_ATT(idx), val | AK4358_ATT_EN);
	return 0;
}

void
ak4358_set_rate(struct envy_softc *sc, int rate)
{
	int reg;

	reg = AK4358_SPEED_DEFAULT & ~(AK4358_SPEED_DFS0 | AK4358_SPEED_DFS1);
	if (rate > 96000)
		reg |= AK4358_SPEED_DFS1;
	else if (rate > 48000)
		reg |= AK4358_SPEED_DFS0;

	/* put in reset state */
	reg &= ~AK4358_SPEED_RSTN;
	envy_codec_write(sc, 0, AK4358_SPEED, reg);

	/* back in normal state */
	reg |= AK4358_SPEED_RSTN;
	envy_codec_write(sc, 0, AK4358_SPEED, reg);
}

/*
 * AK 4524 DAC specific code
 */
int
ak4524_dac_ndev(struct envy_softc *sc)
{
	/* 1 mute + 2 volume knobs per channel pair */
	return 3 * (sc->card->noch / 2);
}

void
ak4524_dac_devinfo(struct envy_softc *sc, struct mixer_devinfo *dev, int idx)
{
	int ndev;

	ndev = sc->card->noch;
	if (idx < ndev) {
		dev->type = AUDIO_MIXER_VALUE;
		dev->mixer_class = ENVY_MIX_CLASSOUT;
		dev->un.v.delta = 2;
		dev->un.v.num_channels = 1;
		snprintf(dev->label.name, MAX_AUDIO_DEV_LEN,
		    AudioNline "-%d", idx);
		strlcpy(dev->un.v.units.name, AudioNvolume,
		    MAX_AUDIO_DEV_LEN);
	} else {
		idx -= ndev;
		dev->type = AUDIO_MIXER_ENUM;
		dev->mixer_class = ENVY_MIX_CLASSOUT;
		dev->un.e.member[0].ord = 0;
		strlcpy(dev->un.e.member[0].label.name, AudioNoff,
		    MAX_AUDIO_DEV_LEN);
		dev->un.e.member[1].ord = 1;
		strlcpy(dev->un.e.member[1].label.name, AudioNon,
		   MAX_AUDIO_DEV_LEN);
		dev->un.e.num_mem = 2;
		snprintf(dev->label.name, MAX_AUDIO_DEV_LEN,
		    AudioNline "-%d:%d_" AudioNmute, 2 * idx, 2 * idx + 1);
	}
}

void
ak4524_dac_get(struct envy_softc *sc, struct mixer_ctrl *ctl, int idx)
{
	int val, ndev;

	ndev = sc->card->noch;
	if (idx < ndev) {
		val = envy_codec_read(sc, idx / 2,
		    (idx % 2) + AK4524_DAC_GAIN0);
		ctl->un.value.num_channels = 1;
		ctl->un.value.level[0] = 2 * val;
	} else {
		idx -= ndev;
		val = envy_codec_read(sc, idx, AK4524_DEEMVOL);
		ctl->un.ord = (val & AK4524_MUTE) ? 1 : 0;
	}
}

int
ak4524_dac_set(struct envy_softc *sc, struct mixer_ctrl *ctl, int idx)
{
	int val, ndev;

	ndev = sc->card->noch;
	if (idx < ndev) {
		if (ctl->un.value.num_channels != 1)
			return EINVAL;
		val = ctl->un.value.level[0] / 2;
		envy_codec_write(sc, idx / 2,
		    (idx % 2) + AK4524_DAC_GAIN0, val);
	} else {
		idx -= ndev;
		if (ctl->un.ord >= 2)
			return EINVAL;
		val = AK4524_DEEM_OFF | (ctl->un.ord ? AK4524_MUTE : 0);
		envy_codec_write(sc, idx, AK4524_DEEMVOL, val);
	}
	return 0;
}

/*
 * AK 4524 ADC specific code
 */
int
ak4524_adc_ndev(struct envy_softc *sc)
{
	/* one volume per channel */
	return sc->card->nich;
}

void
ak4524_adc_devinfo(struct envy_softc *sc, struct mixer_devinfo *dev, int idx)
{
	dev->type = AUDIO_MIXER_VALUE;
	dev->mixer_class = ENVY_MIX_CLASSIN;
	dev->un.v.delta = 2;
	dev->un.v.num_channels = 1;
	snprintf(dev->label.name, MAX_AUDIO_DEV_LEN, AudioNline "-%d", idx);
	strlcpy(dev->un.v.units.name, AudioNvolume, MAX_AUDIO_DEV_LEN);
}

void
ak4524_adc_get(struct envy_softc *sc, struct mixer_ctrl *ctl, int idx)
{
	int val;

	val = envy_codec_read(sc, idx / 2, (idx % 2) + AK4524_ADC_GAIN0);
	ctl->un.value.num_channels = 1;
	ctl->un.value.level[0] = 2 * val;
}

int
ak4524_adc_set(struct envy_softc *sc, struct mixer_ctrl *ctl, int idx)
{
	int val;

	if (ctl->un.value.num_channels != 1)
		return EINVAL;
	val = ctl->un.value.level[0] / 2;
	envy_codec_write(sc, idx / 2, (idx % 2) + AK4524_ADC_GAIN0, val);
	return 0;
}

/*
 * AK 5365 ADC specific code
 */
int
ak5365_adc_ndev(struct envy_softc *sc)
{
	/* 1 source + 2 volume knobs per channel pair */
	return (sc->card->nich + 1);
}

void
ak5365_adc_devinfo(struct envy_softc *sc, struct mixer_devinfo *dev, int idx)
{
	int ndev, i;

	ndev = sc->card->nich;
	if (idx < ndev) {
		dev->type = AUDIO_MIXER_VALUE;
		dev->mixer_class = ENVY_MIX_CLASSIN;
		dev->un.v.delta = 2;
		dev->un.v.num_channels = 1;
		snprintf(dev->label.name, MAX_AUDIO_DEV_LEN,
		    AudioNline "-%d", idx);
		strlcpy(dev->un.v.units.name, AudioNvolume,
		    MAX_AUDIO_DEV_LEN);
	} else {
		dev->type = AUDIO_MIXER_ENUM;
		dev->mixer_class = ENVY_MIX_CLASSIN;
		for (i = 0; i < 5; i++) {
			dev->un.e.member[i].ord = i;
			snprintf(dev->un.e.member[i].label.name,
			    MAX_AUDIO_DEV_LEN, AudioNline "-%d", i);
		}
		dev->un.e.num_mem = 5;
		strlcpy(dev->label.name, AudioNsource,
		    MAX_AUDIO_DEV_LEN);
	}
}

void
ak5365_adc_get(struct envy_softc *sc, struct mixer_ctrl *ctl, int idx)
{
	int val, ndev;

	ndev = sc->card->nich;
	if (idx < ndev) {
		val = envy_codec_read(sc, 1, AK5365_ATT(idx));
		ctl->un.value.num_channels = 1;
		ctl->un.value.level[0] = 2 * val;
	} else {
		ctl->un.ord = envy_codec_read(sc, 1, AK5365_SRC);
	}
}

int
ak5365_adc_set(struct envy_softc *sc, struct mixer_ctrl *ctl, int idx)
{
	int val, ndev;

	ndev = sc->card->nich;
	if (idx < ndev) {
		if (ctl->un.value.num_channels != 1)
			return EINVAL;
		val = ctl->un.value.level[0] / 2;
		envy_codec_write(sc, 1, AK5365_ATT(idx), val);
	} else {
		if (ctl->un.ord >= 5)
			return EINVAL;
		val = ctl->un.ord & AK5365_SRC_MASK;
		envy_codec_write(sc, 1, AK5365_SRC, val);
	}
	return 0;
}

/*
 * generic Envy24 and Envy24HT code, common to all cards
 */

int
envy_ccs_read(struct envy_softc *sc, int reg)
{
	int val;

	val = bus_space_read_1(sc->ccs_iot, sc->ccs_ioh, reg);
	bus_space_barrier(sc->ccs_iot, sc->ccs_ioh, 0, sc->ccs_iosz,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	return val;
}

void
envy_ccs_write(struct envy_softc *sc, int reg, int val)
{
	bus_space_write_1(sc->ccs_iot, sc->ccs_ioh, reg, val);
	bus_space_barrier(sc->ccs_iot, sc->ccs_ioh, 0, sc->ccs_iosz,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
}

int
envy_mt_read_1(struct envy_softc *sc, int reg)
{
	int val;

	val = bus_space_read_1(sc->mt_iot, sc->mt_ioh, reg);
	bus_space_barrier(sc->mt_iot, sc->mt_ioh, 0, sc->mt_iosz,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	return val;
}

void
envy_mt_write_1(struct envy_softc *sc, int reg, int val)
{
	bus_space_write_1(sc->mt_iot, sc->mt_ioh, reg, val);
	bus_space_barrier(sc->mt_iot, sc->mt_ioh, 0, sc->mt_iosz,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
}

int
envy_mt_read_2(struct envy_softc *sc, int reg)
{
	int val;

	val = bus_space_read_2(sc->mt_iot, sc->mt_ioh, reg);
	bus_space_barrier(sc->mt_iot, sc->mt_ioh, 0, sc->mt_iosz,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	return val;
}

void
envy_mt_write_2(struct envy_softc *sc, int reg, int val)
{
	bus_space_write_2(sc->mt_iot, sc->mt_ioh, reg, val);
	bus_space_barrier(sc->mt_iot, sc->mt_ioh, 0, sc->mt_iosz,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
}

int
envy_mt_read_4(struct envy_softc *sc, int reg)
{
	int val;

	val = bus_space_read_4(sc->mt_iot, sc->mt_ioh, reg);
	bus_space_barrier(sc->mt_iot, sc->mt_ioh, 0, sc->mt_iosz,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	return val;
}

void
envy_mt_write_4(struct envy_softc *sc, int reg, int val)
{
	bus_space_write_4(sc->mt_iot, sc->mt_ioh, reg, val);
	bus_space_barrier(sc->mt_iot, sc->mt_ioh, 0, sc->mt_iosz,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
}

int
envy_cci_read(struct envy_softc *sc, int index)
{
	envy_ccs_write(sc, ENVY_CCI_INDEX, index);
	return (envy_ccs_read(sc, ENVY_CCI_DATA));
}

void
envy_cci_write(struct envy_softc *sc, int index, int data)
{
	envy_ccs_write(sc, ENVY_CCI_INDEX, index);
	envy_ccs_write(sc, ENVY_CCI_DATA, data);
}

int
envy_gpio_getstate(struct envy_softc *sc)
{
	if (sc->isht) {
		return envy_ccs_read(sc, ENVY_CCS_GPIODATA0) |
		    (envy_ccs_read(sc, ENVY_CCS_GPIODATA1) << 8) |
		    (envy_ccs_read(sc, ENVY_CCS_GPIODATA2) << 16);
	} else
		return envy_cci_read(sc, ENVY_CCI_GPIODATA);
}

void
envy_gpio_setstate(struct envy_softc *sc, int reg)
{
	if (sc->isht) {
		envy_ccs_write(sc, ENVY_CCS_GPIODATA0, reg & 0xff);
		envy_ccs_write(sc, ENVY_CCS_GPIODATA1, (reg >> 8) & 0xff);
		envy_ccs_write(sc, ENVY_CCS_GPIODATA2, (reg >> 16) & 0xff);
	} else
		envy_cci_write(sc, ENVY_CCI_GPIODATA, reg);
}

int
envy_gpio_getmask(struct envy_softc *sc)
{
	if (sc->isht) {
		return envy_ccs_read(sc, ENVY_CCS_GPIOMASK0) |
		    (envy_ccs_read(sc, ENVY_CCS_GPIOMASK1) << 8) |
		    (envy_ccs_read(sc, ENVY_CCS_GPIOMASK2) << 16);
	} else
		return envy_cci_read(sc, ENVY_CCI_GPIOMASK);
}

void
envy_gpio_setmask(struct envy_softc *sc, int mask)
{
	if (sc->isht) {
		envy_ccs_write(sc, ENVY_CCS_GPIOMASK0, mask & 0xff);
		envy_ccs_write(sc, ENVY_CCS_GPIOMASK1, (mask >> 8) & 0xff);
		envy_ccs_write(sc, ENVY_CCS_GPIOMASK2, (mask >> 16) & 0xff);
	} else
		envy_cci_write(sc, ENVY_CCI_GPIOMASK, mask);
}

int
envy_gpio_getdir(struct envy_softc *sc)
{
	if (sc->isht) {
		return envy_ccs_read(sc, ENVY_CCS_GPIODIR0) |
		    (envy_ccs_read(sc, ENVY_CCS_GPIODIR1) << 8) |
		    (envy_ccs_read(sc, ENVY_CCS_GPIODIR2) << 16);
	} else
		return envy_cci_read(sc, ENVY_CCI_GPIODIR);
}

void
envy_gpio_setdir(struct envy_softc *sc, int dir)
{
	if (sc->isht) {
		envy_ccs_write(sc, ENVY_CCS_GPIODIR0, dir & 0xff);
		envy_ccs_write(sc, ENVY_CCS_GPIODIR1, (dir >> 8) & 0xff);
		envy_ccs_write(sc, ENVY_CCS_GPIODIR2, (dir >> 16) & 0xff);
	} else
		envy_cci_write(sc, ENVY_CCI_GPIODIR, dir);
}

void
envy_gpio_i2c_start_bit(struct envy_softc *sc, int sda, int scl)
{
	int reg;

	reg = envy_gpio_getstate(sc);
	reg |= (sda | scl);
	envy_gpio_setstate(sc, reg);
	delay(5);
	reg &= ~sda;
	envy_gpio_setstate(sc, reg);
	delay(4);
	reg &= ~scl;
	envy_gpio_setstate(sc, reg);
	delay(5);
}

void
envy_gpio_i2c_stop_bit(struct envy_softc *sc, int sda, int scl)
{
	int reg;

	reg = envy_gpio_getstate(sc);
	reg &= ~sda;
	reg |= scl;
	envy_gpio_setstate(sc, reg);
	delay(4);
	reg |= sda;
	envy_gpio_setstate(sc, reg);
}

void
envy_gpio_i2c_byte_out(struct envy_softc *sc, int sda, int scl, int val)
{
	int mask, reg;

	reg = envy_gpio_getstate(sc);

	for (mask = 0x80; mask != 0; mask >>= 1) {
		reg &= ~sda;
		reg |= (val & mask) ? sda : 0;
		envy_gpio_setstate(sc, reg);
		delay(1);
		reg |= scl;
		envy_gpio_setstate(sc, reg);
		delay(4);
		reg &= ~scl;
		envy_gpio_setstate(sc, reg);
		delay(5);
	}

	reg |= scl;
	envy_gpio_setstate(sc, reg);
	delay(4);
	reg &= ~scl;
	envy_gpio_setstate(sc, reg);
	delay(5);
}

void
envy_i2c_wait(struct envy_softc *sc)
{
	int timeout = 50, st;

	for (;;) {
		st = envy_ccs_read(sc, ENVY_I2C_CTL);
		if (!(st & ENVY_I2C_CTL_BUSY))
			break;
		if (timeout == 0) {
			printf("%s: i2c busy timeout\n", DEVNAME(sc));
			break;
		}
		delay(50);
		timeout--;
	}
}

int
envy_i2c_read(struct envy_softc *sc, int dev, int addr)
{
	envy_i2c_wait(sc);
	envy_ccs_write(sc, ENVY_I2C_ADDR, addr);
	envy_i2c_wait(sc);
	envy_ccs_write(sc, ENVY_I2C_DEV, dev << 1);
	envy_i2c_wait(sc);
	return envy_ccs_read(sc, ENVY_I2C_DATA);
}

void
envy_i2c_write(struct envy_softc *sc, int dev, int addr, int data)
{
	if (dev == 0x50) {
		printf("%s: writing on eeprom is evil...\n", DEVNAME(sc));
		return;
	}
	envy_i2c_wait(sc);
	envy_ccs_write(sc, ENVY_I2C_ADDR, addr);
	envy_i2c_wait(sc);
	envy_ccs_write(sc, ENVY_I2C_DATA, data);
	envy_i2c_wait(sc);
	envy_ccs_write(sc, ENVY_I2C_DEV, (dev << 1) | 1);
}

int
envy_codec_read(struct envy_softc *sc, int dev, int addr) {
	return sc->shadow[dev][addr];
}

void
envy_codec_write(struct envy_softc *sc, int dev, int addr, int data)
{
	DPRINTFN(2, "envy_codec_write: %d, %d, 0x%x\n", dev, addr, data);
	sc->shadow[dev][addr] = data;
	sc->card->codec_write(sc, dev, addr, data);
}

int
envy_eeprom_gpioxxx(struct envy_softc *sc, int addr)
{
	int val;

	val = sc->eeprom[addr];
	if (sc->isht) {
		val |= sc->eeprom[++addr] << 8;
		val |= sc->eeprom[++addr] << 16;
	}
	return val;
}

int
envy_ac97_wait(struct envy_softc *sc)
{
	int timeout = 50, st;

	for (;;) {
		st = envy_mt_read_1(sc, ENVY_MT_AC97_CMD);
		if ((st & ENVY_MT_AC97_READY) && !(st & ENVY_MT_AC97_CMD_MASK)) {
			st = 0;
			break;
		}
		if (timeout == 0) {
			st = -1;
			break;
		}
		delay(50);
		timeout--;
	}

	return (st);
}

int
envy_ac97_attach_codec(void *hdl, struct ac97_codec_if *codec_if)
{
	struct envy_softc *sc = hdl;

	sc->codec_if = codec_if;

	return (0);
}

int
envy_ac97_read_codec(void *hdl, u_int8_t reg, u_int16_t *result)
{
	struct envy_softc *sc = hdl;

	if (envy_ac97_wait(sc)) {
		printf("%s: envy_ac97_read_codec: timed out\n", DEVNAME(sc));
		return (-1);
	}

	envy_mt_write_1(sc, ENVY_MT_AC97_IDX, reg & 0x7f);
	envy_mt_write_1(sc, ENVY_MT_AC97_CMD,
	    ENVY_MT_AC97_CMD_RD);
	delay(50);

	if (envy_ac97_wait(sc)) {
		printf("%s: envy_ac97_read_codec: timed out\n", DEVNAME(sc));
		return (-1);
	}

	*result = envy_mt_read_2(sc, ENVY_MT_AC97_DATA);

	return (0);
}

int
envy_ac97_write_codec(void *hdl, u_int8_t reg, u_int16_t data)
{
	struct envy_softc *sc = hdl;

	if (envy_ac97_wait(sc)) {
		printf("%s: envy_ac97_write_codec: timed out\n", DEVNAME(sc));
		return (-1);
	}

	envy_mt_write_1(sc, ENVY_MT_AC97_IDX, reg & 0x7f);
	envy_mt_write_2(sc, ENVY_MT_AC97_DATA, data);
	envy_mt_write_1(sc, ENVY_MT_AC97_CMD,
	    ENVY_MT_AC97_CMD_WR);
	delay(50);

	return (0);
}

void
envy_ac97_reset_codec(void *hdl)
{
	struct envy_softc *sc = hdl;

	envy_mt_write_1(sc, ENVY_MT_AC97_CMD, ENVY_MT_AC97_CMD_RST);
	delay(50);
	envy_mt_write_1(sc, ENVY_MT_AC97_CMD, 0);
	delay(50);

	if (envy_ac97_wait(sc)) {
		printf("%s: envy_ac97_reset_codec: timed out\n", DEVNAME(sc));
	}

	return;
}

enum ac97_host_flags
envy_ac97_flags_codec(void *hdl)
{
	struct envy_softc *sc = hdl;

	return (sc->codec_flags);
}

void
envy_midi_wait(struct envy_softc *sc)
{
	int i, st;
	
	for (i = 100;; i--) {
		st = envy_ccs_read(sc, ENVY_CCS_MIDISTAT0);
		if (!(st & ENVY_MIDISTAT_OBUSY(sc)))
			break;
		if (i == 0) {
			printf("%s: midi wait timeout\n", DEVNAME(sc));
			break;
		}
		delay(10);
	}
}

void
envy_reset(struct envy_softc *sc)
{
	int i, reg;

	/*
	 * full reset
	 */
	envy_ccs_write(sc, ENVY_CTL, ENVY_CTL_RESET | ENVY_CTL_NATIVE);
	delay(200);
	envy_ccs_write(sc, ENVY_CTL, ENVY_CTL_NATIVE);
	delay(200);

	/*
	 * read EEPROM using i2c device or from a static array
	 */
	if (sc->card->eeprom == NULL) {
		for (i = 0; i < ENVY_EEPROM_MAXSZ; i++) {
			sc->eeprom[i] = envy_i2c_read(sc, ENVY_I2C_DEV_EEPROM, i);
		}
#ifdef ENVY_DEBUG
		printf("%s: eeprom: ", DEVNAME(sc));
		for (i = 0; i < ENVY_EEPROM_MAXSZ; i++) {
			printf(" %02x", (unsigned)sc->eeprom[i]);
		}
		printf("\n");
#endif
	} else
		memcpy(sc->eeprom, sc->card->eeprom, ENVY_EEPROM_MAXSZ);

	/*
	 * write EEPROM values to corresponding registers
	 */
	if (sc->isht) {
		envy_ccs_write(sc, ENVY_CCS_CONF,
		    sc->eeprom[ENVY_EEPROM_CONF]);
		envy_ccs_write(sc, ENVY_CCS_ACLINK,
		    sc->eeprom[ENVY_EEPROM_ACLINK]);
		envy_ccs_write(sc, ENVY_CCS_I2S,
		    sc->eeprom[ENVY_EEPROM_I2S]);
		envy_ccs_write(sc, ENVY_CCS_SPDIF,
		    sc->eeprom[ENVY_EEPROM_SPDIF]);
	} else {
		pci_conf_write(sc->pci_pc, sc->pci_tag, ENVY_CONF,
		    sc->eeprom[ENVY_EEPROM_CONF] |
		    (sc->eeprom[ENVY_EEPROM_ACLINK] << 8) |
		    (sc->eeprom[ENVY_EEPROM_I2S] << 16) |
		    (sc->eeprom[ENVY_EEPROM_SPDIF] << 24));
	}

	envy_gpio_setmask(sc, envy_eeprom_gpioxxx(sc, ENVY_EEPROM_GPIOMASK(sc)));
	envy_gpio_setdir(sc, envy_eeprom_gpioxxx(sc, ENVY_EEPROM_GPIODIR(sc)));
	envy_gpio_setstate(sc, envy_eeprom_gpioxxx(sc, ENVY_EEPROM_GPIOST(sc)));

	DPRINTF("%s: gpio_mask = %02x\n", DEVNAME(sc),
		envy_gpio_getmask(sc));
	DPRINTF("%s: gpio_dir = %02x\n", DEVNAME(sc),
		envy_gpio_getdir(sc));
	DPRINTF("%s: gpio_state = %02x\n", DEVNAME(sc),
		envy_gpio_getstate(sc));

	if (sc->isht) {
		/*
		 * set water marks so we get an interrupt for each byte
		 */
		envy_ccs_write(sc, ENVY_CCS_MIDIWAT, 1);
		envy_ccs_write(sc, ENVY_CCS_MIDIWAT, 1 | ENVY_CCS_MIDIWAT_RX);
	}

	/*
	 * switch to UART mode
	 */
	envy_ccs_write(sc, ENVY_CCS_MIDISTAT0, 0xff);
	envy_midi_wait(sc);
	envy_ccs_write(sc, ENVY_CCS_MIDISTAT0, ENVY_MIDISTAT_UART);
	envy_midi_wait(sc);
	if (!sc->isht)
		(void)envy_ccs_read(sc, ENVY_CCS_MIDIDATA0);

	/*
	 * clear all interrupts and unmask used ones
	 */
	envy_ccs_write(sc, ENVY_CCS_INTSTAT, 0xff);
	reg = ~ENVY_CCS_INT_MT;
	if (sc->midi_isopen)
		reg &= ~ENVY_CCS_INT_MIDI0;
	envy_ccs_write(sc, ENVY_CCS_INTMASK, ~ENVY_CCS_INT_MT);
	if (sc->isht) {
		envy_mt_write_1(sc, ENVY_MT_NSTREAM, 4 - sc->card->noch / 2);
		envy_mt_write_1(sc, ENVY_MT_IMASK, ~(ENVY_MT_IMASK_PDMA0 |
		    ENVY_MT_IMASK_RDMA0 | ENVY_MT_IMASK_ERR));
	}
	sc->iactive = 0;
	sc->oactive = 0;
	sc->card->init(sc);
}

int
envy_lineout_getsrc(struct envy_softc *sc, int out)
{
	int reg, shift, src;

	if (sc->isht) {
		reg = envy_mt_read_4(sc, ENVY_MT_HTSRC);
		DPRINTF("%s: outsrc=%x\n", DEVNAME(sc), reg);
		shift = 3 * (out / 2) + ((out & 1) ? 20 : 8);
		src = (reg >> shift) & ENVY_MT_HTSRC_MASK;
		if (src == ENVY_MT_HTSRC_DMA) {
			return ENVY_MIX_OUTSRC_DMA;
		} else {
			src -= ENVY_MT_HTSRC_LINE;
			return ENVY_MIX_OUTSRC_LINEIN + src;
		}
	}

	reg = envy_mt_read_2(sc, ENVY_MT_OUTSRC);
	DPRINTF("%s: outsrc=%x\n", DEVNAME(sc), reg);
	shift = (out  & 1) ? (out & ~1) + 8 : out;
	src = (reg >> shift) & 3;
	if (src == ENVY_MT_OUTSRC_DMA) {
		return ENVY_MIX_OUTSRC_DMA;
	} else if (src == ENVY_MT_OUTSRC_MON) {
		return ENVY_MIX_OUTSRC_MON;
	}
	reg = envy_mt_read_4(sc, ENVY_MT_INSEL);
	DPRINTF("%s: insel=%x\n", DEVNAME(sc), reg);
	reg = (reg >> (out * 4)) & 0xf;
	if (src == ENVY_MT_OUTSRC_LINE)
		return ENVY_MIX_OUTSRC_LINEIN + (reg & 7);
	else
		return ENVY_MIX_OUTSRC_SPDIN + (reg >> 3);
}

void
envy_lineout_setsrc(struct envy_softc *sc, int out, int src)
{
	int reg, shift, mask, sel;

	if (sc->isht) {
		if (src < ENVY_MIX_OUTSRC_SPDIN) {
			sel = ENVY_MT_HTSRC_LINE;
			sel += src;
		} else if (src < ENVY_MIX_OUTSRC_DMA) {
			sel = ENVY_MT_HTSRC_SPD;
			sel += src - ENVY_MIX_OUTSRC_SPDIN;
		} else {
			sel = ENVY_MT_HTSRC_DMA;
		}
		shift = 3 * (out / 2) + ((out & 1) ? 20 : 8);
		mask = ENVY_MT_HTSRC_MASK << shift;
		reg = envy_mt_read_4(sc, ENVY_MT_HTSRC);
		reg = (reg & ~mask) | (sel << shift);
		envy_mt_write_4(sc, ENVY_MT_HTSRC, reg);
		DPRINTF("%s: outsrc <- %x\n", DEVNAME(sc), reg);
		return;
	}

	if (src < ENVY_MIX_OUTSRC_DMA) {
		/*
		 * linein and spdin are used as output source so we
		 * must select the input source channel number
		 */
		if (src < ENVY_MIX_OUTSRC_SPDIN)
			sel = src - ENVY_MIX_OUTSRC_LINEIN;
		else
			sel = (src - ENVY_MIX_OUTSRC_SPDIN) << 3;

		shift = out * ENVY_MT_INSEL_BITS;
		mask = ENVY_MT_INSEL_MASK << shift;
		reg = envy_mt_read_4(sc, ENVY_MT_INSEL);
		reg = (reg & ~mask) | (sel << shift);
		envy_mt_write_4(sc, ENVY_MT_INSEL, reg);
		DPRINTF("%s: insel <- %x\n", DEVNAME(sc), reg);
	}

	/*
	 * set the lineout route register
	 */
	if (src < ENVY_MIX_OUTSRC_SPDIN) {
		sel = ENVY_MT_OUTSRC_LINE;
	} else if (src < ENVY_MIX_OUTSRC_DMA) {
		sel = ENVY_MT_OUTSRC_SPD;
	} else if (src == ENVY_MIX_OUTSRC_DMA) {
		sel = ENVY_MT_OUTSRC_DMA;
	} else {
		sel = ENVY_MT_OUTSRC_MON;
	}
	shift = (out  & 1) ? (out & ~1) + 8 : out;
	mask = ENVY_MT_OUTSRC_MASK << shift;
	reg = envy_mt_read_2(sc, ENVY_MT_OUTSRC);
	reg = (reg & ~mask) | (sel << shift);
	envy_mt_write_2(sc, ENVY_MT_OUTSRC, reg);
	DPRINTF("%s: outsrc <- %x\n", DEVNAME(sc), reg);
}


int
envy_spdout_getsrc(struct envy_softc *sc, int out)
{
	int reg, src, sel;

	reg = envy_mt_read_2(sc, ENVY_MT_SPDROUTE);
	DPRINTF("%s: spdroute=%x\n", DEVNAME(sc), reg);
	src = (out == 0) ? reg : reg >> 2;
	src &= ENVY_MT_SPDSRC_MASK;
	if (src == ENVY_MT_SPDSRC_DMA) {
		return ENVY_MIX_OUTSRC_DMA;
	} else if (src == ENVY_MT_SPDSRC_MON) {
		return ENVY_MIX_OUTSRC_MON;
	}

	sel = (out == 0) ? reg >> 8 : reg >> 12;
	sel &= ENVY_MT_SPDSEL_MASK;
	if (src == ENVY_MT_SPDSRC_LINE)
		return ENVY_MIX_OUTSRC_LINEIN + (sel & 7);
	else
		return ENVY_MIX_OUTSRC_SPDIN + (sel >> 3);
}

void
envy_spdout_setsrc(struct envy_softc *sc, int out, int src)
{
	int reg, shift, mask, sel;

	reg = envy_mt_read_2(sc, ENVY_MT_SPDROUTE);
	if (src < ENVY_MIX_OUTSRC_DMA) {
		/*
		 * linein and spdin are used as output source so we
		 * must select the input source channel number
		 */
		if (src < ENVY_MIX_OUTSRC_SPDIN)
			sel = src - ENVY_MIX_OUTSRC_LINEIN;
		else
			sel = (src - ENVY_MIX_OUTSRC_SPDIN) << 3;

		shift = 8 + out * ENVY_MT_SPDSEL_BITS;
		mask = ENVY_MT_SPDSEL_MASK << shift;
		reg = (reg & ~mask) | (sel << shift);
	}

	/*
	 * set the lineout route register
	 */
	if (src < ENVY_MIX_OUTSRC_SPDIN) {
		sel = ENVY_MT_OUTSRC_LINE;
	} else if (src < ENVY_MIX_OUTSRC_DMA) {
		sel = ENVY_MT_OUTSRC_SPD;
	} else if (src == ENVY_MIX_OUTSRC_DMA) {
		sel = ENVY_MT_OUTSRC_DMA;
	} else {
		sel = ENVY_MT_OUTSRC_MON;
	}
	shift = out * 2;
	mask = ENVY_MT_SPDSRC_MASK << shift;
	reg = (reg & ~mask) | (sel << shift);
	envy_mt_write_2(sc, ENVY_MT_SPDROUTE, reg);
	DPRINTF("%s: spdroute <- %x\n", DEVNAME(sc), reg);
}

void
envy_mon_getvol(struct envy_softc *sc, int idx, int ch, int *val)
{
	int reg;

	envy_mt_write_2(sc, ENVY_MT_MONIDX, idx);
	reg = envy_mt_read_1(sc, ENVY_MT_MONDATA + ch);
	*val = 0x7f - (reg & 0x7f);
}

void
envy_mon_setvol(struct envy_softc *sc, int idx, int ch, int val)
{
	int reg;

	envy_mt_write_2(sc, ENVY_MT_MONIDX, idx);
	reg = 0x7f - val;
	DPRINTF("%s: mon=%d/%d <- %d\n", DEVNAME(sc), reg, ch, val);
	envy_mt_write_1(sc, ENVY_MT_MONDATA + ch, reg);
}

int
envymatch(struct device *parent, void *match, void *aux)
{
	return pci_matchbyid((struct pci_attach_args *)aux, envy_matchids,
	    sizeof(envy_matchids) / sizeof(envy_matchids[0]));
}

void
envyattach(struct device *parent, struct device *self, void *aux)
{
	struct envy_softc *sc = (struct envy_softc *)self;
	struct pci_attach_args *pa = (struct pci_attach_args *)aux;
	pci_intr_handle_t ih;
	const char *intrstr;
	int subid;

#if NMIDI > 0
	sc->midi_isopen = 0;
#endif
	sc->pci_tag = pa->pa_tag;
	sc->pci_pc = pa->pa_pc;
	sc->pci_dmat = pa->pa_dmat;
	sc->pci_ih = NULL;
	sc->ibuf.addr = sc->obuf.addr = NULL;
	sc->ccs_iosz = 0;
	sc->mt_iosz = 0;
	sc->isht = (PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_ICENSEMBLE_VT172X);

	if (pci_mapreg_map(pa, ENVY_CTL_BAR, PCI_MAPREG_TYPE_IO, 0,
		&sc->ccs_iot, &sc->ccs_ioh, NULL, &sc->ccs_iosz, 0)) {
		printf(": can't map ctl i/o space\n");
		sc->ccs_iosz = 0;
		return;
	}
	if (pci_mapreg_map(pa, ENVY_MT_BAR(sc->isht), PCI_MAPREG_TYPE_IO, 0,
		&sc->mt_iot, &sc->mt_ioh, NULL, &sc->mt_iosz, 0)) {
		printf(": can't map mt i/o space\n");
		sc->mt_iosz = 0;
		return;
	}
	if (pci_intr_map(pa, &ih)) {
		printf(": can't map interrupt\n");
	}
	intrstr = pci_intr_string(sc->pci_pc, ih);
	sc->pci_ih = pci_intr_establish(sc->pci_pc, ih, IPL_AUDIO | IPL_MPSAFE,
	    envy_intr, sc, sc->dev.dv_xname);
	if (sc->pci_ih == NULL) {
		printf(": can't establish interrupt");
		if (intrstr)
			printf(" at %s", intrstr);
		printf("\n");
		return;
	}
	printf(": %s\n", intrstr);
	subid = pci_conf_read(sc->pci_pc, sc->pci_tag, PCI_SUBVEND_0);
	sc->card = sc->isht ? envy_cards_ht : envy_cards;
	while (sc->card->subid != subid) {
		if (sc->card->subid == 0)
			break;
		sc->card++;
	}
	printf("%s: %s, %u inputs, %u outputs\n", DEVNAME(sc),
	    sc->card->name, sc->card->nich, sc->card->noch);
	envy_reset(sc);
	sc->audio = audio_attach_mi(&envy_hw_if, sc, NULL, &sc->dev);
#if NMIDI > 0
	if (sc->card->nmidi > 0 && (!sc->isht ||
		sc->eeprom[ENVY_EEPROM_CONF] & ENVY_CONF_MIDI)) {
		sc->midi = midi_attach_mi(&envy_midi_hw_if, sc, &sc->dev);
	}
#endif
}

int
envydetach(struct device *self, int flags)
{
	struct envy_softc *sc = (struct envy_softc *)self;

	if (sc->pci_ih != NULL) {
		pci_intr_disestablish(sc->pci_pc, sc->pci_ih);
		sc->pci_ih = NULL;
	}
	if (sc->ccs_iosz) {
		bus_space_unmap(sc->ccs_iot, sc->ccs_ioh, sc->ccs_iosz);
	}
	if (sc->mt_iosz) {
		bus_space_unmap(sc->mt_iot, sc->mt_ioh, sc->mt_iosz);
	}
	return 0;
}

int
envyactivate(struct device *self, int act)
{
	struct envy_softc *sc = (struct envy_softc *)self;

 	if (act == DVACT_RESUME) {
		/*
		 * The audio(4) layer will restore parameters and, if
		 * needed, start DMA. So we only need to reach the
		 * same device state as after the audio_attach() call.
		 */
		envy_reset(sc);
	}
	return config_activate_children(self, act);
}

int
envy_open(void *self, int flags)
{
	return 0;
}

void
envy_close(void *self)
{
}

void *
envy_allocm(void *self, int dir, size_t size, int type, int flags)
{
	struct envy_softc *sc = (struct envy_softc *)self;
	int err, wait;
	struct envy_buf *buf;
	bus_addr_t dma_addr;
	bus_dma_segment_t seg;
	int rseg;

	buf = (dir == AUMODE_RECORD) ? &sc->ibuf : &sc->obuf;
	if (buf->addr != NULL) {
		DPRINTF("%s: multiple alloc, dir = %d\n", DEVNAME(sc), dir);
		return NULL;
	}
	buf->size = size;
	wait = (flags & M_NOWAIT) ? BUS_DMA_NOWAIT : BUS_DMA_WAITOK;

#define ENVY_ALIGN	4
#define ENVY_MAXADDR	((1 << 28) - 1)

	err = bus_dmamem_alloc_range(sc->pci_dmat, buf->size, 0, 0, &seg, 1,
	    &rseg, BUS_DMA_NOWAIT, (bus_addr_t)0, (bus_addr_t)ENVY_MAXADDR);
	if (err) {
		DPRINTF("%s: dmamem_alloc_range failed %d", DEVNAME(sc), err);
		goto err_ret;
	}
	err = bus_dmamem_map(sc->pci_dmat, &seg, rseg, buf->size, &buf->addr,
	    BUS_DMA_NOWAIT);
	if (err) {
		DPRINTF("%s: dmamem_map failed %d\n", DEVNAME(sc), err);
		goto err_unmap;
	}
	err = bus_dmamap_create(sc->pci_dmat, buf->size, 1, buf->size, 0,
	    wait, &buf->map);
	if (err) {
		DPRINTF("%s: dmamap_create: failed %d\n", DEVNAME(sc), err);
		goto err_unmap;
	}
	err = bus_dmamap_load(sc->pci_dmat, buf->map, buf->addr,
	    buf->size, NULL, wait);
	if (err) {
		DPRINTF("%s: dmamap_load: failed %d\n", DEVNAME(sc), err);
		goto err_destroy;
	}
	dma_addr = buf->map->dm_segs[0].ds_addr;
	DPRINTF("%s: allocated %zd bytes dir=%d, ka=%p, da=%lx\n", DEVNAME(sc),
	    buf->size, dir, buf->addr, dma_addr);
	if (!sc->isht && (dma_addr & ~ENVY_MAXADDR)) {
		printf("%s: DMA address beyond 0x10000000\n", DEVNAME(sc));
		goto err_unload;
	}
	return buf->addr;
 err_unload:
	bus_dmamap_unload(sc->pci_dmat, buf->map);
 err_destroy:
	bus_dmamap_destroy(sc->pci_dmat, buf->map);
 err_unmap:
	bus_dmamem_free(sc->pci_dmat, &seg, rseg);
 err_ret:
	return NULL;
}

void
envy_freem(void *self, void *addr, int type)
{
	struct envy_buf *buf;
	struct envy_softc *sc = (struct envy_softc *)self;
	int dir;

	if (sc->ibuf.addr == addr) {
		buf = &sc->ibuf;
		dir = AUMODE_RECORD;
	} else if (sc->obuf.addr == addr) {
		buf = &sc->obuf;
		dir = AUMODE_PLAY;
	} else {
		DPRINTF("%s: no buf to free\n", DEVNAME(sc));
		return;
	}
	bus_dmamap_unload(sc->pci_dmat, buf->map);
	bus_dmamap_destroy(sc->pci_dmat, buf->map);
	bus_dmamem_free(sc->pci_dmat, buf->map->dm_segs, 1);
	buf->addr = NULL;
	DPRINTF("%s: freed buffer (mode=%d)\n", DEVNAME(sc), dir);
}

int
envy_set_params(void *self, int setmode, int usemode,
    struct audio_params *p, struct audio_params *r)
{
	struct envy_softc *sc = (struct envy_softc *)self;
	int i, rate, reg;

	if (setmode == 0)
		return 0;
	if (setmode == (AUMODE_PLAY | AUMODE_RECORD) &&
	    p->sample_rate != r->sample_rate) {
		DPRINTF("%s: play/rec rates mismatch\n", DEVNAME(sc));
		r->sample_rate = p->sample_rate;
	}

	rate = (setmode & AUMODE_PLAY) ? p->sample_rate : r->sample_rate;

	/* only HT model supports rates above 96kHz */
	if (!sc->isht && rate > 96000)
		rate = 96000;	

	for (i = 0; envy_rates[i].rate < rate; i++) {
		if (envy_rates[i].rate == -1) {
			i--;
			DPRINTF("%s: rate: %d -> %d\n", DEVNAME(sc), rate, i);
			break;
		}
	}

	if (sc->isht) {
		reg = envy_mt_read_1(sc, ENVY_MT_FMT);
		if (rate > 96000)
			reg |= ENVY_MT_FMT_128X;
		else
			reg &= ~ENVY_MT_FMT_128X;
		envy_mt_write_1(sc, ENVY_MT_FMT, reg);
	}

	if (sc->card->set_rate)
		sc->card->set_rate(sc, rate);

	reg = envy_mt_read_1(sc, ENVY_MT_RATE);
	reg &= ~ENVY_MT_RATEMASK;
	reg |= envy_rates[i].reg;
	envy_mt_write_1(sc, ENVY_MT_RATE, reg);

	if (setmode & AUMODE_PLAY) {
		p->sample_rate = envy_rates[i].rate;
		p->encoding = AUDIO_ENCODING_SLINEAR_LE;
		p->precision = 24;
		p->bps = 4;
		p->msb = 1;
		p->channels = sc->isht ? sc->card->noch : ENVY_PCHANS;
	}
	if (setmode & AUMODE_RECORD) {
		r->sample_rate = envy_rates[i].rate;
		r->encoding = AUDIO_ENCODING_SLINEAR_LE;
		r->precision = 24;
		r->bps = 4;
		r->msb = 1;
		r->channels = sc->isht ? sc->card->nich : ENVY_RCHANS;
	}
	return 0;
}

int
envy_round_blocksize(void *self, int blksz)
{
	return (blksz + 0x1f) & ~0x1f;
}

#ifdef ENVY_DEBUG
void
envy_pintr(struct envy_softc *sc)
{
	int i;

	if (sc->spurious > 0 || envydebug >= 2) {
		printf("%s: spurious = %u, start = %lld.%ld\n", 
			DEVNAME(sc), sc->spurious,
			(long long)sc->start_ts.tv_sec, sc->start_ts.tv_nsec);
		for (i = 0; i < sc->nintr; i++) {
			printf("%lld.%09ld: "
			    "active=%d/%d pos=%d/%d st=%x/%x, ctl=%x\n",
			    (long long)sc->intrs[i].ts.tv_sec,
			    sc->intrs[i].ts.tv_nsec,
			    sc->intrs[i].iactive,
			    sc->intrs[i].oactive,
			    sc->intrs[i].ipos,
			    sc->intrs[i].opos,
			    sc->intrs[i].st,
			    sc->intrs[i].mask,
			    sc->intrs[i].ctl);
		}
	}
}
#endif

int
envy_intr(void *self)
{
	struct envy_softc *sc = (struct envy_softc *)self;
	unsigned int reg, hwpos, cnt;
	int mintr, mstat, mdata;
	int st, err, ctl;
	int max;

	mtx_enter(&audio_lock);
	st = envy_mt_read_1(sc, ENVY_MT_INTR);
	mintr = envy_ccs_read(sc, ENVY_CCS_INTSTAT);
	if (!(st & ENVY_MT_INTR_ALL) && !(mintr & ENVY_CCS_INT_MIDI0)) {
		mtx_leave(&audio_lock);
		return 0;
	}
	if (st & ENVY_MT_INTR_ERR) {
		err = envy_mt_read_1(sc, ENVY_MT_ERR);
		envy_mt_write_1(sc, ENVY_MT_ERR, err);
	}
	envy_mt_write_1(sc, ENVY_MT_INTR, st);
	envy_ccs_write(sc, ENVY_CCS_INTSTAT, mintr);

#ifdef ENVY_DEBUG
	if (sc->nintr < ENVY_NINTR) {
		sc->intrs[sc->nintr].iactive = sc->iactive;
		sc->intrs[sc->nintr].oactive = sc->oactive;
		sc->intrs[sc->nintr].st = st;
		sc->intrs[sc->nintr].ipos = envy_mt_read_2(sc, ENVY_MT_RBUFSZ);
		sc->intrs[sc->nintr].opos = envy_mt_read_2(sc, ENVY_MT_PBUFSZ);
		sc->intrs[sc->nintr].ctl = envy_mt_read_1(sc, ENVY_MT_CTL);
		sc->intrs[sc->nintr].mask = envy_mt_read_1(sc, ENVY_MT_IMASK);
		nanouptime(&sc->intrs[sc->nintr].ts);
		sc->nintr++;
	}
#endif
	if (mintr & ENVY_CCS_INT_MIDI0) {
		for (max = 128; max > 0; max--) {
			mstat = envy_ccs_read(sc, ENVY_CCS_MIDISTAT0);
			if (mstat & ENVY_MIDISTAT_IEMPTY(sc))
				break;
			mdata = envy_ccs_read(sc, ENVY_CCS_MIDIDATA0);
#if NMIDI > 0
			if (sc->midi_in)
				sc->midi_in(sc->midi_arg, mdata);
#endif
		}
	}
	if (st & ENVY_MT_INTR_PACK) {
		if (sc->oactive) {
			reg = envy_mt_read_2(sc, ENVY_MT_PBUFSZ);
			hwpos = sc->obuf.bufsz - 4 * (reg + 1);
			if (hwpos >= sc->obuf.bufsz)
				hwpos -= sc->obuf.bufsz;
			DPRINTFN(2, "%s: play: reg = %u, pos: %u -> %u\n",
			    DEVNAME(sc), reg, sc->obuf.swpos, hwpos);
			cnt = 0;
			while (hwpos - sc->obuf.swpos >= sc->obuf.blksz) {
				sc->ointr(sc->oarg);
				sc->obuf.swpos += sc->obuf.blksz;
				if (sc->obuf.swpos == sc->obuf.bufsz)
					sc->obuf.swpos = 0;
				cnt++;
			}
			if (cnt != 1) {
				DPRINTFN(2, "%s: play: %u intrs\n",
				    DEVNAME(sc), cnt);
			}
		} else {
			ctl = envy_mt_read_1(sc, ENVY_MT_CTL);
			if (ctl & ENVY_MT_CTL_PSTART) {
				envy_mt_write_1(sc,
				    ENVY_MT_CTL, ctl & ~ENVY_MT_CTL_PSTART);
				st &= ~ENVY_MT_INTR_PACK;
				sc->obusy = 0;
				wakeup(&sc->obusy);
			}
#ifdef ENVY_DEBUG
			else
				sc->spurious++;
#endif
		}
	}
	if (st & ENVY_MT_INTR_RACK) {
		if (sc->iactive) {
			reg = envy_mt_read_2(sc, ENVY_MT_RBUFSZ);
			hwpos = sc->ibuf.bufsz - 4 * (reg + 1);
			if (hwpos >= sc->ibuf.bufsz)
				hwpos -= sc->ibuf.bufsz;
			DPRINTFN(2, "%s: rec: reg = %u, pos: %u -> %u\n",
			    DEVNAME(sc), reg, sc->ibuf.swpos, hwpos);
			cnt = 0;
			while (hwpos - sc->ibuf.swpos >= sc->ibuf.blksz) {
				sc->iintr(sc->iarg);
				sc->ibuf.swpos += sc->ibuf.blksz;
				if (sc->ibuf.swpos == sc->ibuf.bufsz)
					sc->ibuf.swpos = 0;
				cnt++;
			}
			if (cnt != 1) {
				DPRINTFN(2, "%s: rec: %u intrs\n",
				    DEVNAME(sc), cnt);
			}
		} else {
			ctl = envy_mt_read_1(sc, ENVY_MT_CTL);
			if (ctl & ENVY_MT_CTL_RSTART(sc)) {
				envy_mt_write_1(sc,
				    ENVY_MT_CTL, ctl & ~ENVY_MT_CTL_RSTART(sc));
				st &= ~ENVY_MT_INTR_RACK;
				sc->ibusy = 0;
				wakeup(&sc->ibusy);
			}
#ifdef ENVY_DEBUG
			else
				sc->spurious++;
#endif
		}
	}
	mtx_leave(&audio_lock);
	return 1;
}

int
envy_trigger_output(void *self, void *start, void *end, int blksz,
    void (*intr)(void *), void *arg, struct audio_params *param)
{
	struct envy_softc *sc = (struct envy_softc *)self;
	size_t bufsz;
	int st;

	bufsz = (char *)end - (char *)start;
#ifdef ENVY_DEBUG
	if (blksz % (sc->isht ? sc->card->noch * 4 : ENVY_PFRAME_SIZE) != 0) {
		printf("%s: %d: bad output blksz\n", DEVNAME(sc), blksz);
		return EINVAL;
	}
	if (bufsz % blksz) {
		printf("%s: %ld: bad output bufsz\n", DEVNAME(sc), bufsz);
		return EINVAL;
	}
#endif
	mtx_enter(&audio_lock);
	envy_mt_write_4(sc, ENVY_MT_PADDR, sc->obuf.map->dm_segs[0].ds_addr);
	envy_mt_write_2(sc, ENVY_MT_PBUFSZ, bufsz / 4 - 1);
	envy_mt_write_2(sc, ENVY_MT_PBLKSZ(sc), blksz / 4 - 1);

#ifdef ENVY_DEBUG
	if (!sc->iactive) {
		sc->nintr = 0;
		sc->spurious = 0;
		nanouptime(&sc->start_ts);
	}
#endif
	sc->obuf.bufsz = bufsz;
	sc->obuf.blksz = blksz;
	sc->obuf.swpos = 0;
	sc->ointr = intr;
	sc->oarg = arg;
	sc->oactive = 1;
	sc->obusy = 1;
	st = ENVY_MT_INTR_PACK;
	envy_mt_write_1(sc, ENVY_MT_INTR, st);
	st = envy_mt_read_1(sc, ENVY_MT_CTL);
	st |= ENVY_MT_CTL_PSTART;
	envy_mt_write_1(sc, ENVY_MT_CTL, st);
	mtx_leave(&audio_lock);
	return 0;
}

int
envy_trigger_input(void *self, void *start, void *end, int blksz,
    void (*intr)(void *), void *arg, struct audio_params *param)
{
	struct envy_softc *sc = (struct envy_softc *)self;
	size_t bufsz;
	int st;

	bufsz = (char *)end - (char *)start;
#ifdef ENVY_DEBUG
	if (blksz % (sc->isht ? sc->card->nich * 4 : ENVY_RFRAME_SIZE) != 0) {
		printf("%s: %d: bad input blksz\n", DEVNAME(sc), blksz);
		return EINVAL;
	}
	if (bufsz % blksz != 0) {
		printf("%s: %ld: bad input bufsz\n", DEVNAME(sc), bufsz);
		return EINVAL;
	}
#endif
	mtx_enter(&audio_lock);
	envy_mt_write_4(sc, ENVY_MT_RADDR, sc->ibuf.map->dm_segs[0].ds_addr);
	envy_mt_write_2(sc, ENVY_MT_RBUFSZ, bufsz / 4 - 1);
	envy_mt_write_2(sc, ENVY_MT_RBLKSZ, blksz / 4 - 1);

#ifdef ENVY_DEBUG
	if (!sc->oactive) {
		sc->nintr = 0;
		sc->spurious = 0;
		nanouptime(&sc->start_ts);
	}
#endif
	sc->ibuf.bufsz = bufsz;
	sc->ibuf.blksz = blksz;
	sc->ibuf.swpos = 0;
	sc->iintr = intr;
	sc->iarg = arg;
	sc->iactive = 1;
	sc->ibusy = 1;
	st = ENVY_MT_INTR_RACK;
	envy_mt_write_1(sc, ENVY_MT_INTR, st);
	st = envy_mt_read_1(sc, ENVY_MT_CTL);
	st |= ENVY_MT_CTL_RSTART(sc);
	envy_mt_write_1(sc, ENVY_MT_CTL, st);
	mtx_leave(&audio_lock);
	return 0;
}

int
envy_halt_output(void *self)
{
	struct envy_softc *sc = (struct envy_softc *)self;
	int err;

	mtx_enter(&audio_lock);
	sc->oactive = 0;
	if (sc->obusy) {
		err = msleep_nsec(&sc->obusy, &audio_lock, PWAIT, "envyobus",
		    SEC_TO_NSEC(4));
		if (err)
			printf("%s: output DMA halt timeout\n", DEVNAME(sc));
	}
#ifdef ENVY_DEBUG
	if (!sc->iactive)
		envy_pintr(sc);
#endif
	mtx_leave(&audio_lock);
	return 0;
}

int
envy_halt_input(void *self)
{
	struct envy_softc *sc = (struct envy_softc *)self;
	int err;

	mtx_enter(&audio_lock);
	sc->iactive = 0;
	if (sc->ibusy) {
		err = msleep_nsec(&sc->ibusy, &audio_lock, PWAIT, "envyibus",
		    SEC_TO_NSEC(4));
		if (err)
			printf("%s: input DMA halt timeout\n", DEVNAME(sc));
	}
#ifdef ENVY_DEBUG
	if (!sc->oactive)
		envy_pintr(sc);
#endif
	mtx_leave(&audio_lock);
	return 0;
}

int
envy_query_devinfo(void *self, struct mixer_devinfo *dev)
{
	struct envy_softc *sc = (struct envy_softc *)self;
	int i, n, idx, ndev;
	char *classes[] = {
		AudioCinputs, AudioCoutputs, AudioCmonitor
	};

	if (sc->isac97)
		return (sc->codec_if->vtbl->query_devinfo(sc->codec_if, dev));

	if (dev->index < 0)
		return ENXIO;

	idx = dev->index;
	ndev = ENVY_MIX_NCLASS;
	dev->prev = dev->next = AUDIO_MIXER_LAST;

	/*
	 * classes
	 */
	if (idx < ndev) {
		dev->type = AUDIO_MIXER_CLASS;
		dev->mixer_class = idx;
		strlcpy(dev->label.name, classes[idx], MAX_AUDIO_DEV_LEN);
		return 0;
	}
	idx -= ndev;

	/*
	 * output.lineX_source
	 */
	ndev = sc->card->noch;
	if (idx < ndev) {
		n = 0;
		dev->type = AUDIO_MIXER_ENUM;
		dev->mixer_class = ENVY_MIX_CLASSOUT;
		for (i = 0; i < sc->card->nich; i++) {
			dev->un.e.member[n].ord = n;
			snprintf(dev->un.e.member[n++].label.name,
			    MAX_AUDIO_DEV_LEN, AudioNline "-%d", i);
		}
		dev->un.e.member[n].ord = n;
		snprintf(dev->un.e.member[n++].label.name,
			 MAX_AUDIO_DEV_LEN, "play-%d", idx);
		if (!sc->isht && idx < 2) {
			dev->un.e.member[n].ord = n;
			snprintf(dev->un.e.member[n++].label.name,
			    MAX_AUDIO_DEV_LEN, "mon-%d", idx);
		}
		snprintf(dev->label.name, MAX_AUDIO_DEV_LEN,
		    AudioNline "-%d_" AudioNsource, idx);
		dev->un.s.num_mem = n;
		return 0;
	}
	idx -= ndev;

	/*
	 * envy monitor level
	 */
	ndev = sc->isht ? 0 : ENVY_MIX_NMONITOR;
	if (idx < ndev) {
		dev->type = AUDIO_MIXER_VALUE;
		dev->mixer_class = ENVY_MIX_CLASSMON;
		dev->un.v.delta = 2;
		dev->un.v.num_channels = 1;
		snprintf(dev->label.name, MAX_AUDIO_DEV_LEN,
			 "%s-%d", idx < 10 ? "play" : "rec", idx % 10);
		strlcpy(dev->un.v.units.name, AudioNvolume, MAX_AUDIO_DEV_LEN);
		return 0;
	}
	idx -= ndev;

	/*
	 * inputs.xxx
	 */
	ndev = sc->card->adc->ndev(sc);
	if (idx < ndev) {
		sc->card->adc->devinfo(sc, dev, idx);
		return 0;
	}
	idx -= ndev;

	/*
	 * outputs.xxx
	 */
	ndev = sc->card->dac->ndev(sc);
	if (idx < ndev) {
		sc->card->dac->devinfo(sc, dev, idx);
		return 0;
	}
	return ENXIO;
}

int
envy_get_port(void *self, struct mixer_ctrl *ctl)
{
	struct envy_softc *sc = (struct envy_softc *)self;
	int val, idx, ndev;

	if (sc->isac97)
		return (sc->codec_if->vtbl->mixer_get_port(sc->codec_if, ctl));

	if (ctl->dev < ENVY_MIX_NCLASS) {
		return EINVAL;
	}

	idx = ctl->dev - ENVY_MIX_NCLASS;
	ndev = sc->card->noch;
	if (idx < ndev) {
		ctl->un.ord = envy_lineout_getsrc(sc, idx);
		if (ctl->un.ord >= ENVY_MIX_NOUTSRC)
			ctl->un.ord -= ENVY_MIX_NOUTSRC - sc->card->nich;
		return 0;
	}
	idx -= ndev;
	ndev = sc->isht ? 0 : ENVY_MIX_NMONITOR;
	if (idx < ndev) {
		envy_mon_getvol(sc, idx / 2, idx % 2, &val);
		ctl->un.value.num_channels = 1;
		ctl->un.value.level[0] = 2 * val;
		return 0;
	}
	idx -= ndev;
	ndev = sc->card->adc->ndev(sc);
	if (idx < ndev) {
		sc->card->adc->get(sc, ctl, idx);
		return 0;
	}
	idx -= ndev;
	ndev = sc->card->dac->ndev(sc);
	if (idx < ndev) {
		sc->card->dac->get(sc, ctl, idx);
		return 0;
	}
	return ENXIO;
}

int
envy_set_port(void *self, struct mixer_ctrl *ctl)
{
	struct envy_softc *sc = (struct envy_softc *)self;
	int maxsrc, val, idx, ndev;

	if (sc->isac97)
		return (sc->codec_if->vtbl->mixer_set_port(sc->codec_if, ctl));

	if (ctl->dev < ENVY_MIX_NCLASS) {
		return EINVAL;
	}

	idx = ctl->dev - ENVY_MIX_NCLASS;
	ndev = sc->card->noch;
	if (idx < ndev) {
		maxsrc = sc->card->nich + 1;
		if (idx < 2)
			maxsrc++;
		if (ctl->un.ord < 0 || ctl->un.ord >= maxsrc)
			return EINVAL;
		if (ctl->un.ord >= sc->card->nich)
			ctl->un.ord += ENVY_MIX_NOUTSRC - sc->card->nich;
		envy_lineout_setsrc(sc, idx, ctl->un.ord);
		return 0;
	}
	idx -= ndev;
	ndev = sc->isht ? 0 : ENVY_MIX_NMONITOR;
	if (idx < ndev) {
		if (ctl->un.value.num_channels != 1) {
			return EINVAL;
		}
		val = ctl->un.value.level[0] / 2;
		envy_mon_setvol(sc, idx / 2, idx % 2, val);
		return 0;
	}
	idx -= ndev;
	ndev = sc->card->adc->ndev(sc);
	if (idx < ndev)
		return sc->card->adc->set(sc, ctl, idx);
	idx -= ndev;
	ndev = sc->card->dac->ndev(sc);
	if (idx < ndev)
		return sc->card->dac->set(sc, ctl, idx);
	return ENXIO;
}

#if NMIDI > 0
int
envy_midi_open(void *self, int flags,
    void (*in)(void *, int),
    void (*out)(void *),
    void *arg)
{
	struct envy_softc *sc = (struct envy_softc *)self;
	unsigned int i, reg;

	/* discard pending data */
	for (i = 0; i < 128; i++) {
		reg = envy_ccs_read(sc, ENVY_CCS_MIDISTAT0);
		if (reg & ENVY_MIDISTAT_IEMPTY(sc))
			break;
		(void)envy_ccs_read(sc, ENVY_CCS_MIDIDATA0);
	}
#ifdef ENVY_DEBUG
	if (i > 0)
		DPRINTF("%s: midi: discarded %u bytes\n", DEVNAME(sc), i);
#endif

	/* clear pending midi interrupt */
	envy_ccs_write(sc, ENVY_CCS_INTSTAT, ENVY_CCS_INT_MIDI0);

	/* interrupts are disabled, it safe to manipulate these */
	sc->midi_in = in;
	sc->midi_out = out;
	sc->midi_arg = arg;
	sc->midi_isopen = 1;

	/* enable interrupts */
	reg = envy_ccs_read(sc, ENVY_CCS_INTMASK);
	reg &= ~ENVY_CCS_INT_MIDI0;
	envy_ccs_write(sc, ENVY_CCS_INTMASK, reg);
	return 0;
}

void
envy_midi_close(void *self)
{
	struct envy_softc *sc = (struct envy_softc *)self;
	unsigned int reg;

	/* wait for output fifo to drain */
	tsleep_nsec(sc, PWAIT, "envymid", MSEC_TO_NSEC(100));

	/* disable interrupts */
	reg = envy_ccs_read(sc, ENVY_CCS_INTMASK);
	reg |= ENVY_CCS_INT_MIDI0;
	envy_ccs_write(sc, ENVY_CCS_INTMASK, reg);

	/* interrupts are disabled, it safe to manipulate these */
	sc->midi_in = NULL;
	sc->midi_out = NULL;
	sc->midi_isopen = 0;
}

int
envy_midi_output(void *self, int data)
{
	struct envy_softc *sc = (struct envy_softc *)self;
	int st;
	
	st = envy_ccs_read(sc, ENVY_CCS_MIDISTAT0);
	if (st & ENVY_MIDISTAT_OBUSY(sc))
		return 0;
	envy_ccs_write(sc, ENVY_CCS_MIDIDATA0, data);
	return 1;
}

void
envy_midi_getinfo(void *self, struct midi_info *mi)
{
	mi->props = MIDI_PROP_CAN_INPUT;
	mi->name = "Envy24 MIDI UART";
}
#endif
