/*-
 * Copyright (c) 2016 Ruslan Bukin <br@bsdpad.com>
 * All rights reserved.
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory under DARPA/AFRL contract FA8750-10-C-0237
 * ("CTSRD"), as part of the DARPA CRASH research programme.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/* Ingenic JZ4780 CODEC. */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/resource.h>
#include <sys/rman.h>
#include <sys/gpio.h>

#include <machine/bus.h>

#include <dev/fdt/fdt_common.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <dev/gpio/gpiobusvar.h>

#include <dev/extres/clk/clk.h>

#include <mips/ingenic/jz4780_common.h>
#include <mips/ingenic/jz4780_codec.h>

#define	CI20_HP_PIN	13
#define	CI20_HP_PORT	3

struct codec_softc {
	device_t		dev;
	struct resource		*res[1];
	bus_space_tag_t		bst;
	bus_space_handle_t	bsh;
	clk_t			clk;
};

static struct resource_spec codec_spec[] = {
	{ SYS_RES_MEMORY,	0,	RF_ACTIVE },
	{ -1, 0 }
};

static int codec_probe(device_t dev);
static int codec_attach(device_t dev);
static int codec_detach(device_t dev);
void codec_print_registers(struct codec_softc *sc);

static int
codec_write(struct codec_softc *sc, uint32_t reg, uint32_t val)
{
	uint32_t tmp;

	clk_enable(sc->clk);

	tmp = (reg << RGADW_RGADDR_S);
	tmp |= (val << RGADW_RGDIN_S);
	tmp |= RGADW_RGWR;

	WRITE4(sc, CODEC_RGADW, tmp);

	while(READ4(sc, CODEC_RGADW) & RGADW_RGWR)
		;

	clk_disable(sc->clk);

	return (0);
}

static int
codec_read(struct codec_softc *sc, uint32_t reg)
{
	uint32_t tmp;

	clk_enable(sc->clk);

	tmp = (reg << RGADW_RGADDR_S);
	WRITE4(sc, CODEC_RGADW, tmp);

	tmp = READ4(sc, CODEC_RGDATA);

	clk_disable(sc->clk);

	return (tmp);
}

void
codec_print_registers(struct codec_softc *sc)
{

	printf("codec SR %x\n", codec_read(sc, SR));
	printf("codec SR2 %x\n", codec_read(sc, SR2));
	printf("codec MR %x\n", codec_read(sc, MR));
	printf("codec AICR_DAC %x\n", codec_read(sc, AICR_DAC));
	printf("codec AICR_ADC %x\n", codec_read(sc, AICR_ADC));
	printf("codec CR_LO %x\n", codec_read(sc, CR_LO));
	printf("codec CR_HP %x\n", codec_read(sc, CR_HP));
	printf("codec CR_DMIC %x\n", codec_read(sc, CR_DMIC));
	printf("codec CR_MIC1 %x\n", codec_read(sc, CR_MIC1));
	printf("codec CR_MIC2 %x\n", codec_read(sc, CR_MIC2));
	printf("codec CR_LI1 %x\n", codec_read(sc, CR_LI1));
	printf("codec CR_LI2 %x\n", codec_read(sc, CR_LI2));
	printf("codec CR_DAC %x\n", codec_read(sc, CR_DAC));
	printf("codec CR_ADC %x\n", codec_read(sc, CR_ADC));
	printf("codec CR_MIX %x\n", codec_read(sc, CR_MIX));
	printf("codec DR_MIX %x\n", codec_read(sc, DR_MIX));
	printf("codec CR_VIC %x\n", codec_read(sc, CR_VIC));
	printf("codec CR_CK %x\n", codec_read(sc, CR_CK));
	printf("codec FCR_DAC %x\n", codec_read(sc, FCR_DAC));
	printf("codec FCR_ADC %x\n", codec_read(sc, FCR_ADC));
	printf("codec CR_TIMER_MSB %x\n", codec_read(sc, CR_TIMER_MSB));
	printf("codec CR_TIMER_LSB %x\n", codec_read(sc, CR_TIMER_LSB));
	printf("codec ICR %x\n", codec_read(sc, ICR));
	printf("codec IMR %x\n", codec_read(sc, IMR));
	printf("codec IFR %x\n", codec_read(sc, IFR));
	printf("codec IMR2 %x\n", codec_read(sc, IMR2));
	printf("codec IFR2 %x\n", codec_read(sc, IFR2));
	printf("codec GCR_HPL %x\n", codec_read(sc, GCR_HPL));
	printf("codec GCR_HPR %x\n", codec_read(sc, GCR_HPR));
	printf("codec GCR_LIBYL %x\n", codec_read(sc, GCR_LIBYL));
	printf("codec GCR_LIBYR %x\n", codec_read(sc, GCR_LIBYR));
	printf("codec GCR_DACL %x\n", codec_read(sc, GCR_DACL));
	printf("codec GCR_DACR %x\n", codec_read(sc, GCR_DACR));
	printf("codec GCR_MIC1 %x\n", codec_read(sc, GCR_MIC1));
	printf("codec GCR_MIC2 %x\n", codec_read(sc, GCR_MIC2));
	printf("codec GCR_ADCL %x\n", codec_read(sc, GCR_ADCL));
	printf("codec GCR_ADCR %x\n", codec_read(sc, GCR_ADCR));
	printf("codec GCR_MIXDACL %x\n", codec_read(sc, GCR_MIXDACL));
	printf("codec GCR_MIXDACR %x\n", codec_read(sc, GCR_MIXDACR));
	printf("codec GCR_MIXADCL %x\n", codec_read(sc, GCR_MIXADCL));
	printf("codec GCR_MIXADCR %x\n", codec_read(sc, GCR_MIXADCR));
	printf("codec CR_ADC_AGC %x\n", codec_read(sc, CR_ADC_AGC));
	printf("codec DR_ADC_AGC %x\n", codec_read(sc, DR_ADC_AGC));
}

/*
 * CI20 board-specific
 */
static int
ci20_hp_unmute(struct codec_softc *sc)
{
	device_t dev;
	int port;
	int err;
	int pin;

	pin = CI20_HP_PIN;
	port = CI20_HP_PORT;

	dev = devclass_get_device(devclass_find("gpio"), port);
	if (dev == NULL)
		return (0);

	err = GPIO_PIN_SETFLAGS(dev, pin, GPIO_PIN_OUTPUT);
	if (err != 0) {
		device_printf(dev, "Cannot configure GPIO pin %d on %s\n",
		    pin, device_get_nameunit(dev));
		return (err);
	}

	err = GPIO_PIN_SET(dev, pin, 0);
	if (err != 0) {
		device_printf(dev, "Cannot configure GPIO pin %d on %s\n",
		    pin, device_get_nameunit(dev));
		return (err);
	}

	return (0);
}

static int
codec_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "ingenic,jz4780-codec"))
		return (ENXIO);

	device_set_desc(dev, "Ingenic JZ4780 CODEC");

	return (BUS_PROBE_DEFAULT);
}

static int
codec_attach(device_t dev)
{
	struct codec_softc *sc;
	uint8_t reg;

	sc = device_get_softc(dev);
	sc->dev = dev;

	if (bus_alloc_resources(dev, codec_spec, sc->res)) {
		device_printf(dev, "could not allocate resources for device\n");
		return (ENXIO);
	}

	/* Memory interface */
	sc->bst = rman_get_bustag(sc->res[0]);
	sc->bsh = rman_get_bushandle(sc->res[0]);

	if (clk_get_by_ofw_name(dev, 0, "i2s", &sc->clk) != 0) {
		device_printf(dev, "could not get i2s clock\n");
		bus_release_resources(dev, codec_spec, sc->res);
		return (ENXIO);
	}

	/* Initialize codec. */
	reg = codec_read(sc, CR_VIC);
	reg &= ~(VIC_SB_SLEEP | VIC_SB);
	codec_write(sc, CR_VIC, reg);

	DELAY(20000);

	reg = codec_read(sc, CR_DAC);
	reg &= ~(DAC_SB | DAC_MUTE);
	codec_write(sc, CR_DAC, reg);

	DELAY(10000);

	/* I2S, 16-bit, 48 kHz. */
	reg = codec_read(sc, AICR_DAC);
	reg &= ~(AICR_DAC_SB | DAC_ADWL_M);
	reg |= DAC_ADWL_16;
	reg &= ~(AUDIOIF_M);
	reg |= AUDIOIF_I2S;
	codec_write(sc, AICR_DAC, reg);

	DELAY(10000);

	reg = FCR_DAC_48;
	codec_write(sc, FCR_DAC, reg);

	DELAY(10000);

	/* Unmute headphones. */
	reg = codec_read(sc, CR_HP);
	reg &= ~(HP_SB | HP_MUTE);
	codec_write(sc, CR_HP, reg);

	ci20_hp_unmute(sc);

	return (0);
}

static int
codec_detach(device_t dev)
{
	struct codec_softc *sc;

	sc = device_get_softc(dev);

	bus_release_resources(dev, codec_spec, sc->res);

	return (0);
}

static device_method_t codec_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,			codec_probe),
	DEVMETHOD(device_attach,		codec_attach),
	DEVMETHOD(device_detach,		codec_detach),

	DEVMETHOD_END
};

static driver_t codec_driver = {
	"codec",
	codec_methods,
	sizeof(struct codec_softc),
};

static devclass_t codec_devclass;

DRIVER_MODULE(codec, simplebus, codec_driver, codec_devclass, 0, 0);
