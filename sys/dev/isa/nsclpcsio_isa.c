/* $OpenBSD: nsclpcsio_isa.c,v 1.15 2022/04/06 18:59:28 naddy Exp $ */
/* $NetBSD: nsclpcsio_isa.c,v 1.5 2002/10/22 16:18:26 drochner Exp $ */

/*
 * Copyright (c) 2002 Matthias Drochner.  All rights reserved.
 * Copyright (c) 2004 Markus Friedl.  All rights reserved.
 * Copyright (c) 2004 Alexander Yurchenko.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer.
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

/*
 * National Semiconductor PC87366 LPC Super I/O.
 * Supported logical devices: GPIO, TMS, VLM.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/gpio.h>
#include <sys/kernel.h>
#include <sys/sensors.h>
#include <sys/timeout.h>

#include <machine/bus.h>

#include <dev/isa/isareg.h>
#include <dev/isa/isavar.h>

#include <dev/gpio/gpiovar.h>

#if defined(NSC_LPC_SIO_DEBUG)
#define DPRINTF(x)              do { printf x; } while (0)
#else
#define DPRINTF(x)
#endif

#define SIO_REG_SID	0x20	/* Super I/O ID */
#define SIO_SID_PC87366	0xE9 	/* PC87366 is identified by 0xE9.*/

#define SIO_REG_SRID	0x27	/* Super I/O Revision */

#define SIO_REG_LDN	0x07	/* Logical Device Number */
#define SIO_LDN_FDC	0x00	/* Floppy Disk Controller (FDC) */
#define SIO_LDN_PP	0x01	/* Parallel Port (PP) */
#define SIO_LDN_SP2	0x02	/* Serial Port 2 with IR (SP2) */
#define SIO_LDN_SP1	0x03	/* Serial Port 1 (SP1) */
#define SIO_LDN_SWC	0x04	/* System Wake-Up Control (SWC) */
#define SIO_LDN_KBCM	0x05	/* Mouse Controller (KBC) */
#define SIO_LDN_KBCK	0x06	/* Keyboard Controller (KBC) */
#define SIO_LDN_GPIO	0x07	/* General-Purpose I/O (GPIO) Ports */
#define SIO_LDN_ACB	0x08	/* ACCESS.bus Interface (ACB) */
#define SIO_LDN_FSCM	0x09	/* Fan Speed Control and Monitor (FSCM) */
#define SIO_LDN_WDT	0x0A	/* WATCHDOG Timer (WDT) */
#define SIO_LDN_GMP	0x0B	/* Game Port (GMP) */
#define SIO_LDN_MIDI	0x0C	/* Musical Instrument Digital Interface */
#define SIO_LDN_VLM	0x0D	/* Voltage Level Monitor (VLM) */
#define SIO_LDN_TMS	0x0E	/* Temperature Sensor (TMS) */

#define SIO_REG_ACTIVE	0x30	/* Logical Device Activate Register */
#define SIO_ACTIVE_EN		0x01	/* enabled */

#define SIO_REG_IO_MSB	0x60	/* I/O Port Base, bits 15-8 */
#define SIO_REG_IO_LSB	0x61	/* I/O Port Base, bits 7-0 */

#define SIO_LDNUM	15	/* total number of logical devices */

/* Supported logical devices description */
static const struct {
	const char *ld_name;
	int ld_num;
	int ld_iosize;
} sio_ld[] = {
	{ "GPIO", SIO_LDN_GPIO, 16 },
	{ "VLM", SIO_LDN_VLM, 16 },
	{ "TMS", SIO_LDN_TMS, 16 },
};

/* GPIO */
#define SIO_GPIO_PINSEL	0xf0
#define SIO_GPIO_PINCFG	0xf1
#define SIO_GPIO_PINEV	0xf2

#define SIO_GPIO_CONF_OUTPUTEN	(1 << 0)
#define SIO_GPIO_CONF_PUSHPULL	(1 << 1)
#define SIO_GPIO_CONF_PULLUP	(1 << 2)

#define SIO_GPDO0	0x00
#define SIO_GPDI0	0x01
#define SIO_GPEVEN0	0x02
#define SIO_GPEVST0	0x03
#define SIO_GPDO1	0x04
#define SIO_GPDI1	0x05
#define SIO_GPEVEN1	0x06
#define SIO_GPEVST1	0x07
#define SIO_GPDO2	0x08
#define SIO_GPDI2	0x09
#define SIO_GPDO3	0x0a
#define SIO_GPDI3	0x0b

#define SIO_GPIO_NPINS	29

/* TMS */
#define SIO_TEVSTS	0x00	/* Temperature Event Status */
#define SIO_TEVSMI	0x02	/* Temperature Event to SMI */
#define SIO_TEVIRQ	0x04	/* Temperature Event to IRQ */
#define SIO_TMSCFG	0x08	/* TMS Configuration */
#define SIO_TMSBS	0x09	/* TMS Bank Select */
#define SIO_TCHCFST	0x0A	/* Temperature Channel Config and Status */
#define SIO_RDCHT	0x0B	/* Read Channel Temperature */
#define SIO_CHTH	0x0C	/* Channel Temperature High Limit */
#define SIO_CHTL	0x0D	/* Channel Temperature Low Limit */
#define SIO_CHOTL	0x0E	/* Channel Overtemperature Limit */

/* VLM */
#define SIO_VEVSTS0	0x00	/* Voltage Event Status 0 */
#define SIO_VEVSTS1	0x01	/* Voltage Event Status 1 */
#define SIO_VEVSMI0	0x02	/* Voltage Event to SMI 0 */
#define SIO_VEVSMI1	0x03	/* Voltage Event to SMI 1 */
#define SIO_VEVIRQ0	0x04	/* Voltage Event to IRQ 0 */
#define SIO_VEVIRQ1	0x05	/* Voltage Event to IRQ 1 */
#define SIO_VID		0x06	/* Voltage ID */
#define SIO_VCNVR	0x07	/* Voltage Conversion Rate */
#define SIO_VLMCFG	0x08	/* VLM Configuration */
#define SIO_VLMBS	0x09	/* VLM Bank Select */
#define SIO_VCHCFST	0x0A	/* Voltage Channel Config and Status */
#define SIO_RDCHV	0x0B	/* Read Channel Voltage */
#define SIO_CHVH	0x0C	/* Channel Voltage High Limit */
#define SIO_CHVL	0x0D	/* Channel Voltage Low Limit */
#define SIO_OTSL	0x0E	/* Overtemperature Shutdown Limit */

#define SIO_REG_SIOCF1	0x21
#define SIO_REG_SIOCF2	0x22
#define SIO_REG_SIOCF3	0x23
#define SIO_REG_SIOCF4	0x24
#define SIO_REG_SIOCF5	0x25
#define SIO_REG_SIOCF8	0x28
#define SIO_REG_SIOCFA	0x2A
#define SIO_REG_SIOCFB	0x2B
#define SIO_REG_SIOCFC	0x2C
#define SIO_REG_SIOCFD	0x2D

#define	SIO_NUM_SENSORS	(3+14)
#define SIO_VLM_OFF	3
#define SIO_VREF	1235	/* 1000.0 * VREF */

struct nsclpcsio_softc {
	struct device sc_dev;
	bus_space_tag_t sc_iot;
	bus_space_handle_t sc_ioh;

	bus_space_handle_t sc_ld_ioh[SIO_LDNUM];
	int sc_ld_en[SIO_LDNUM];

	/* GPIO */
	struct gpio_chipset_tag sc_gpio_gc;
	struct gpio_pin sc_gpio_pins[SIO_GPIO_NPINS];

	/* TMS and VLM */
	struct ksensor sensors[SIO_NUM_SENSORS];
	struct ksensordev sensordev;
};

#define GPIO_READ(sc, reg) \
	bus_space_read_1((sc)->sc_iot,				\
	    (sc)->sc_ld_ioh[SIO_LDN_GPIO], (reg))
#define GPIO_WRITE(sc, reg, val) \
	bus_space_write_1((sc)->sc_iot,				\
	    (sc)->sc_ld_ioh[SIO_LDN_GPIO], (reg), (val))
#define TMS_WRITE(sc, reg, val) \
	bus_space_write_1((sc)->sc_iot,				\
	    (sc)->sc_ld_ioh[SIO_LDN_TMS], (reg), (val))
#define TMS_READ(sc, reg) \
	bus_space_read_1((sc)->sc_iot,				\
	    (sc)->sc_ld_ioh[SIO_LDN_TMS], (reg))
#define VLM_WRITE(sc, reg, val) \
	bus_space_write_1((sc)->sc_iot,				\
	    (sc)->sc_ld_ioh[SIO_LDN_VLM], (reg), (val))
#define VLM_READ(sc, reg) \
	bus_space_read_1((sc)->sc_iot,				\
	    (sc)->sc_ld_ioh[SIO_LDN_VLM], (reg))

int	 nsclpcsio_isa_match(struct device *, void *, void *);
void	 nsclpcsio_isa_attach(struct device *, struct device *, void *);

const struct cfattach nsclpcsio_isa_ca = {
	sizeof(struct nsclpcsio_softc),
	nsclpcsio_isa_match,
	nsclpcsio_isa_attach
};

struct cfdriver nsclpcsio_cd = {
	NULL, "nsclpcsio", DV_DULL
};

struct timeout  nsclpcsio_timeout;

static u_int8_t	nsread(bus_space_tag_t, bus_space_handle_t, int);
static void	nswrite(bus_space_tag_t, bus_space_handle_t, int, u_int8_t);

void	nsclpcsio_gpio_init(struct nsclpcsio_softc *);
int	nsclpcsio_gpio_pin_read(void *, int);
void	nsclpcsio_gpio_pin_write(void *, int, int);
void	nsclpcsio_gpio_pin_ctl(void *, int, int);

void	nsclpcsio_tms_init(struct nsclpcsio_softc *);
void	nsclpcsio_vlm_init(struct nsclpcsio_softc *);
void	nsclpcsio_tms_update(struct nsclpcsio_softc *);
void	nsclpcsio_vlm_update(struct nsclpcsio_softc *);
void	nsclpcsio_refresh(void *);

static u_int8_t
nsread(bus_space_tag_t iot, bus_space_handle_t ioh, int idx)
{
	bus_space_write_1(iot, ioh, 0, idx);
	return (bus_space_read_1(iot, ioh, 1));
}

static void
nswrite(bus_space_tag_t iot, bus_space_handle_t ioh, int idx, u_int8_t data)
{
	bus_space_write_1(iot, ioh, 0, idx);
	bus_space_write_1(iot, ioh, 1, data);
}

int
nsclpcsio_isa_match(struct device *parent, void *match, void *aux)
{
	struct isa_attach_args *ia = aux;
	bus_space_tag_t iot;
	bus_space_handle_t ioh;
	int iobase;
	int rv = 0;

	iot = ia->ia_iot;
	iobase = ia->ipa_io[0].base;
	if (bus_space_map(iot, iobase, 2, 0, &ioh))
		return (0);

	if (nsread(iot, ioh, SIO_REG_SID) == SIO_SID_PC87366)
		rv = 1;

	bus_space_unmap(iot, ioh, 2);

	if (rv) {
		ia->ipa_nio = 1;
		ia->ipa_io[0].length = 2;

		ia->ipa_nmem = 0;
		ia->ipa_nirq = 0;
		ia->ipa_ndrq = 0;
	}

	return (rv);
}

void
nsclpcsio_isa_attach(struct device *parent, struct device *self, void *aux)
{
	struct nsclpcsio_softc *sc = (void *)self;
	struct isa_attach_args *ia = aux;
	struct gpiobus_attach_args gba;
	int iobase;
	int i;

	iobase = ia->ipa_io[0].base;
	sc->sc_iot = ia->ia_iot;
	if (bus_space_map(ia->ia_iot, iobase, 2, 0, &sc->sc_ioh)) {
		printf(": can't map i/o space\n");
		return;
	}
	printf(": NSC PC87366 rev %d:",
	    nsread(sc->sc_iot, sc->sc_ioh, SIO_REG_SRID));

	/* Configure all supported logical devices */
	for (i = 0; i < sizeof(sio_ld) / sizeof(sio_ld[0]); i++) {
		sc->sc_ld_en[sio_ld[i].ld_num] = 0;

		/* Select the device and check if it's activated */
		nswrite(sc->sc_iot, sc->sc_ioh, SIO_REG_LDN, sio_ld[i].ld_num);
		if ((nsread(sc->sc_iot, sc->sc_ioh,
		    SIO_REG_ACTIVE) & SIO_ACTIVE_EN) == 0)
			continue;

		/* Map I/O space if necessary */
		if (sio_ld[i].ld_iosize != 0) {
			iobase = (nsread(sc->sc_iot, sc->sc_ioh,
			    SIO_REG_IO_MSB) << 8);
			iobase |= nsread(sc->sc_iot, sc->sc_ioh,
			    SIO_REG_IO_LSB);
			if (bus_space_map(sc->sc_iot, iobase,
			    sio_ld[i].ld_iosize, 0,
			    &sc->sc_ld_ioh[sio_ld[i].ld_num]))
				continue;
		}

		sc->sc_ld_en[sio_ld[i].ld_num] = 1;
		printf(" %s", sio_ld[i].ld_name);
	}

	printf("\n");

	nsclpcsio_gpio_init(sc);
	nsclpcsio_tms_init(sc);
	nsclpcsio_vlm_init(sc);

	/* Hook into hw.sensors sysctl */
	strlcpy(sc->sensordev.xname, sc->sc_dev.dv_xname,
	    sizeof(sc->sensordev.xname));
	for (i = 0; i < SIO_NUM_SENSORS; i++) {
		if (i < SIO_VLM_OFF && !sc->sc_ld_en[SIO_LDN_TMS])
			continue;
		if (i >= SIO_VLM_OFF && !sc->sc_ld_en[SIO_LDN_VLM])
			continue;
		sensor_attach(&sc->sensordev, &sc->sensors[i]);
	}
	sensordev_install(&sc->sensordev);
	if (sc->sc_ld_en[SIO_LDN_TMS] || sc->sc_ld_en[SIO_LDN_VLM]) {
		timeout_set(&nsclpcsio_timeout, nsclpcsio_refresh, sc);
		timeout_add_sec(&nsclpcsio_timeout, 2);
	}

	/* Attach GPIO framework */
	if (sc->sc_ld_en[SIO_LDN_GPIO]) {
		gba.gba_name = "gpio";
		gba.gba_gc = &sc->sc_gpio_gc;
		gba.gba_pins = sc->sc_gpio_pins;
		gba.gba_npins = SIO_GPIO_NPINS;
		config_found(&sc->sc_dev, &gba, NULL);
	}
}

void
nsclpcsio_refresh(void *arg)
{
	struct nsclpcsio_softc *sc = (struct nsclpcsio_softc *)arg;

	if (sc->sc_ld_en[SIO_LDN_TMS])
		nsclpcsio_tms_update(sc);
	if (sc->sc_ld_en[SIO_LDN_VLM])
		nsclpcsio_vlm_update(sc);
	timeout_add_sec(&nsclpcsio_timeout, 2);
}

void
nsclpcsio_tms_init(struct nsclpcsio_softc *sc)
{
	int i;

	/* Initialisation, PC87366.pdf, page 208 */
	TMS_WRITE(sc, 0x08, 0x00);
	TMS_WRITE(sc, 0x09, 0x0f);
	TMS_WRITE(sc, 0x0a, 0x08);
	TMS_WRITE(sc, 0x0b, 0x04);
	TMS_WRITE(sc, 0x0c, 0x35);
	TMS_WRITE(sc, 0x0d, 0x05);
	TMS_WRITE(sc, 0x0e, 0x05);

	TMS_WRITE(sc, SIO_TMSCFG, 0x00);

	/* Enable the sensors */
	for (i = 0; i < 3; i++) {
		TMS_WRITE(sc, SIO_TMSBS, i);
		TMS_WRITE(sc, SIO_TCHCFST, 0x01);

		sc->sensors[i].type = SENSOR_TEMP;
	}

	strlcpy(sc->sensors[0].desc, "Remote", sizeof(sc->sensors[0].desc));
	strlcpy(sc->sensors[1].desc, "Remote", sizeof(sc->sensors[1].desc));
	strlcpy(sc->sensors[2].desc, "Local", sizeof(sc->sensors[2].desc));

	nsclpcsio_tms_update(sc);
}

void
nsclpcsio_tms_update(struct nsclpcsio_softc *sc)
{
	u_int8_t status;
	int8_t sdata;
	int i;

	for (i = 0; i < 3; i++) {
		TMS_WRITE(sc, SIO_TMSBS, i);
		status = TMS_READ(sc, SIO_TCHCFST);
		if (!(status & 0x01)) {
			DPRINTF(("%s: status %d: disabled\n",
			    sc->sensors[i].desc, status));
			sc->sensors[i].value = 0;
			continue;
		}
		sdata = TMS_READ(sc, SIO_RDCHT);
		DPRINTF(("%s: status %d C %d\n", sc->sensors[i].desc,
		    status, sdata));
		sc->sensors[i].value = sdata * 1000000 + 273150000;
	}
}

void
nsclpcsio_vlm_init(struct nsclpcsio_softc *sc)
{
	int i;
	char *desc = NULL;

	VLM_WRITE(sc, SIO_VLMCFG, 0x00);

	/* Enable the sensors */
	for (i = 0; i < 14; i++) {
		VLM_WRITE(sc, SIO_VLMBS, i);
		VLM_WRITE(sc, SIO_VCHCFST, 0x01);

		desc = NULL;
		switch (i) {
		case 7:
			desc = "VSB";
			break;
		case 8:
			desc = "VDD";
			break;
		case 9:
			desc = "VBAT";
			break;
		case 10:
			desc = "AVDD";
			break;
		case 11:
			desc = "TS1";
			break;
		case 12:
			desc = "TS2";
			break;
		case 13:
			desc = "TS3";
			break;
		}
		/* only init .desc if we have something meaningful to say */
		if (desc != NULL)
			strlcpy(sc->sensors[SIO_VLM_OFF + i].desc, desc,
			    sizeof(sc->sensors[SIO_VLM_OFF + i].desc));
		sc->sensors[SIO_VLM_OFF + i].type = SENSOR_VOLTS_DC;

	}
	nsclpcsio_vlm_update(sc);
}

void
nsclpcsio_vlm_update(struct nsclpcsio_softc *sc)
{
	u_int8_t status;
	u_int8_t data;
	int scale, rfact, i;

	for (i = 0; i < 14; i++) {
		VLM_WRITE(sc, SIO_VLMBS, i);
		status = VLM_READ(sc, SIO_VCHCFST);
		if (!(status & 0x01)) {
			DPRINTF(("%s: status %d: disabled\n",
			    sc->sensors[SIO_VLM_OFF + i].desc, status));
			sc->sensors[SIO_VLM_OFF + i].value = 0;
			continue;
		}
		data = VLM_READ(sc, SIO_RDCHV);
		DPRINTF(("%s: status %d V %d\n",
		    sc->sensors[SIO_VLM_OFF + i].desc, status, data));

		scale = 1;
		switch (i) {
		case 7:
		case 8:
		case 10:
			scale = 2;
		}

		/* Vi = (2.45±0.05)*VREF *RDCHVi / 256 */
		rfact = 10 * scale * ((245 * SIO_VREF) >> 8);
		sc->sensors[SIO_VLM_OFF + i].value = data * rfact;
	}
}

static __inline void
nsclpcsio_gpio_pin_select(struct nsclpcsio_softc *sc, int pin)
{
	int port, shift;
	u_int8_t data;

	port = pin / 8;
	shift = pin % 8;
	data = (port << 4) | shift;

	nswrite(sc->sc_iot, sc->sc_ioh, SIO_REG_LDN, SIO_LDN_GPIO);
	nswrite(sc->sc_iot, sc->sc_ioh, SIO_GPIO_PINSEL, data);
}

void
nsclpcsio_gpio_init(struct nsclpcsio_softc *sc)
{
	int i;

	for (i = 0; i < SIO_GPIO_NPINS; i++) {
		sc->sc_gpio_pins[i].pin_num = i;
		sc->sc_gpio_pins[i].pin_caps = GPIO_PIN_INPUT |
		    GPIO_PIN_OUTPUT | GPIO_PIN_OPENDRAIN |
		    GPIO_PIN_PUSHPULL | GPIO_PIN_TRISTATE |
		    GPIO_PIN_PULLUP;

		/* Read initial state */
		sc->sc_gpio_pins[i].pin_state = nsclpcsio_gpio_pin_read(sc,
		    i) ? GPIO_PIN_HIGH : GPIO_PIN_LOW;
	}

	/* Create controller tag */
	sc->sc_gpio_gc.gp_cookie = sc;
	sc->sc_gpio_gc.gp_pin_read = nsclpcsio_gpio_pin_read;
	sc->sc_gpio_gc.gp_pin_write = nsclpcsio_gpio_pin_write;
	sc->sc_gpio_gc.gp_pin_ctl = nsclpcsio_gpio_pin_ctl;
}

int
nsclpcsio_gpio_pin_read(void *arg, int pin)
{
	struct nsclpcsio_softc *sc = arg;
	int port, shift, reg;
	u_int8_t data;

	port = pin / 8;
	shift = pin % 8;

	switch (port) {
	case 0:
		reg = SIO_GPDI0;
		break;
	case 1:
		reg = SIO_GPDI1;
		break;
	case 2:
		reg = SIO_GPDI2;
		break;
	case 3:
		reg = SIO_GPDI3;
		break;
	}

	data = GPIO_READ(sc, reg);

	return ((data >> shift) & 0x1);
}

void
nsclpcsio_gpio_pin_write(void *arg, int pin, int value)
{
	struct nsclpcsio_softc *sc = arg;
	int port, shift, reg;
	u_int8_t data;

	port = pin / 8;
	shift = pin % 8;

	switch (port) {
	case 0:
		reg = SIO_GPDO0;
		break;
	case 1:
		reg = SIO_GPDO1;
		break;
	case 2:
		reg = SIO_GPDO2;
		break;
	case 3:
		reg = SIO_GPDO3;
		break;
	}

	data = GPIO_READ(sc, reg);
	if (value == 0)
		data &= ~(1 << shift);
	else if (value == 1)
		data |= (1 << shift);

	GPIO_WRITE(sc, reg, data);
}

void
nsclpcsio_gpio_pin_ctl(void *arg, int pin, int flags)
{
	struct nsclpcsio_softc *sc = arg;
	u_int8_t conf = 1;

	nswrite(sc->sc_iot, sc->sc_ioh, SIO_REG_LDN, SIO_LDN_GPIO);
	nsclpcsio_gpio_pin_select(sc, pin);
	conf = nsread(sc->sc_iot, sc->sc_ioh, SIO_GPIO_PINCFG);

	conf &= ~(SIO_GPIO_CONF_OUTPUTEN | SIO_GPIO_CONF_PUSHPULL |
	    SIO_GPIO_CONF_PULLUP);
	if ((flags & GPIO_PIN_TRISTATE) == 0)
		conf |= SIO_GPIO_CONF_OUTPUTEN;
	if (flags & GPIO_PIN_PUSHPULL)
		conf |= SIO_GPIO_CONF_PUSHPULL;
	if (flags & GPIO_PIN_PULLUP)
		conf |= SIO_GPIO_CONF_PULLUP;

	nswrite(sc->sc_iot, sc->sc_ioh, SIO_GPIO_PINCFG, conf);
}
