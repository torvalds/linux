/*	$OpenBSD: viasio.c,v 1.15 2022/04/06 18:59:29 naddy Exp $	*/
/*
 * Copyright (c) 2005 Alexander Yurchenko <grange@openbsd.org>
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
 * VIA VT1211 LPC Super I/O driver.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/kernel.h>
#include <sys/sensors.h>
#include <sys/timeout.h>

#include <machine/bus.h>

#include <dev/isa/isareg.h>
#include <dev/isa/isavar.h>

#include <dev/isa/viasioreg.h>

#ifdef VIASIO_DEBUG
#define DPRINTF(x) printf x
#else
#define DPRINTF(x)
#endif

/* autoconf flags */
#define VIASIO_CFFLAGS_HM_ENABLE	0x0001	/* enable HM if disabled */
#define VIASIO_CFFLAGS_WDG_ENABLE	0x0002	/* enable WDG if disabled */

struct viasio_softc {
	struct device		sc_dev;

	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;

	/* Hardware monitor */
	bus_space_handle_t	sc_hm_ioh;
	int			sc_hm_clock;
	struct ksensor		sc_hm_sensors[VT1211_HM_NSENSORS];
	struct ksensordev	sc_sensordev;
	struct timeout		sc_hm_timo;

	/* Watchdog timer */
	bus_space_handle_t	sc_wdg_ioh;
};

int	viasio_probe(struct device *, void *, void *);
void	viasio_attach(struct device *, struct device *, void *);
int	viasio_activate(struct device *, int);

void	viasio_hm_init(struct viasio_softc *);
void	viasio_hm_refresh(void *);

void	viasio_wdg_init(struct viasio_softc *);
int	viasio_wdg_cb(void *, int);

const struct cfattach viasio_ca = {
	sizeof(struct viasio_softc),
	viasio_probe,
	viasio_attach,
	NULL,
	viasio_activate
};

struct cfdriver viasio_cd = {
	NULL, "viasio", DV_DULL
};

static __inline void
viasio_conf_enable(bus_space_tag_t iot, bus_space_handle_t ioh)
{
	bus_space_write_1(iot, ioh, VT1211_INDEX, VT1211_CONF_EN_MAGIC);
	bus_space_write_1(iot, ioh, VT1211_INDEX, VT1211_CONF_EN_MAGIC);
}

static __inline void
viasio_conf_disable(bus_space_tag_t iot, bus_space_handle_t ioh)
{
	bus_space_write_1(iot, ioh, VT1211_INDEX, VT1211_CONF_DS_MAGIC);
}

static __inline u_int8_t
viasio_conf_read(bus_space_tag_t iot, bus_space_handle_t ioh, u_int8_t index)
{
	bus_space_write_1(iot, ioh, VT1211_INDEX, index);
	return (bus_space_read_1(iot, ioh, VT1211_DATA));
}

static __inline void
viasio_conf_write(bus_space_tag_t iot, bus_space_handle_t ioh, u_int8_t index,
    u_int8_t data)
{
	bus_space_write_1(iot, ioh, VT1211_INDEX, index);
	bus_space_write_1(iot, ioh, VT1211_DATA, data);
}

static __inline int64_t
viasio_raw2temp(int raw)
{
	int tblsize = sizeof(vt1211_hm_temptbl) / sizeof(vt1211_hm_temptbl[0]);
	int i;
	int raw1, raw2;
	int64_t temp = -1, temp1, temp2;

	if (raw < vt1211_hm_temptbl[0].raw ||
	    raw > vt1211_hm_temptbl[tblsize - 1].raw)
		return (-1);

	for (i = 0; i < tblsize - 1; i++) {
		raw1 = vt1211_hm_temptbl[i].raw;
		temp1 = vt1211_hm_temptbl[i].temp;
		raw2 = vt1211_hm_temptbl[i + 1].raw;
		temp2 = vt1211_hm_temptbl[i + 1].temp;

		if (raw >= raw1 && raw <= raw2) {
			/* linear interpolation */
			temp = temp1 + ((raw - raw1) * (temp2 - temp1)) /
			    (raw2 - raw1);
			break;
		}
	}

	return (temp);
}

int
viasio_probe(struct device *parent, void *match, void *aux)
{
	struct isa_attach_args *ia = aux;
	bus_space_tag_t iot;
	bus_space_handle_t ioh;
	u_int8_t reg;

	/* Match by device ID */
	iot = ia->ia_iot;
	if (bus_space_map(iot, ia->ipa_io[0].base, VT1211_IOSIZE, 0, &ioh))
		return (0);
	viasio_conf_enable(iot, ioh);
	reg = viasio_conf_read(iot, ioh, VT1211_ID);
	DPRINTF(("viasio_probe: id 0x%02x\n", reg));
	viasio_conf_disable(iot, ioh);
	bus_space_unmap(iot, ioh, VT1211_IOSIZE);
	if (reg == VT1211_ID_VT1211) {
		ia->ipa_nio = 1;
		ia->ipa_io[0].length = VT1211_IOSIZE;
		ia->ipa_nmem = 0;
		ia->ipa_nirq = 0;
		ia->ipa_ndrq = 0;
		return (1);
	}

	return (0);
}

void
viasio_attach(struct device *parent, struct device *self, void *aux)
{
	struct viasio_softc *sc = (void *)self;
	struct isa_attach_args *ia = aux;
	u_int8_t reg;

	/* Map ISA I/O space */
	sc->sc_iot = ia->ia_iot;
	if (bus_space_map(sc->sc_iot, ia->ipa_io[0].base,
	    VT1211_IOSIZE, 0, &sc->sc_ioh)) {
		printf(": can't map i/o space\n");
		return;
	}

	/* Enter configuration mode */
	viasio_conf_enable(sc->sc_iot, sc->sc_ioh);

	/* Read device revision */
	reg = viasio_conf_read(sc->sc_iot, sc->sc_ioh, VT1211_REV);
	printf(": VT1211 rev 0x%02x", reg);

	/* Initialize logical devices */
	viasio_hm_init(sc);
	viasio_wdg_init(sc);
	printf("\n");

	/* Escape from configuration mode */
	viasio_conf_disable(sc->sc_iot, sc->sc_ioh);
}

int
viasio_activate(struct device *self, int act)
{
	switch (act) {
	case DVACT_POWERDOWN:
		wdog_shutdown(self);
		break;
	}

	return (0);
}

void
viasio_hm_init(struct viasio_softc *sc)
{
	u_int8_t reg0, reg1;
	u_int16_t iobase;
	int i;

	printf(", HM");

	/* Select HM logical device */
	viasio_conf_write(sc->sc_iot, sc->sc_ioh, VT1211_LDN, VT1211_LDN_HM);

	/*
	 * Check if logical device is activated by firmware.  If not
	 * try to activate it only if requested.
	 */
	reg0 = viasio_conf_read(sc->sc_iot, sc->sc_ioh, VT1211_HM_ACT);
	DPRINTF((": ACT 0x%02x", reg0));
	if ((reg0 & VT1211_HM_ACT_EN) == 0) {
		if ((sc->sc_dev.dv_cfdata->cf_flags &
		    VIASIO_CFFLAGS_HM_ENABLE) != 0) {
			reg0 |= VT1211_HM_ACT_EN;
			viasio_conf_write(sc->sc_iot, sc->sc_ioh,
			    VT1211_HM_ACT, reg0);
			reg0 = viasio_conf_read(sc->sc_iot, sc->sc_ioh,
			    VT1211_HM_ACT);
			DPRINTF((", new ACT 0x%02x", reg0));
			if ((reg0 & VT1211_HM_ACT_EN) == 0) {
				printf(" failed to activate");
				return;
			}
		} else {
			printf(" not activated");
			return;
		}
	}

	/* Read HM I/O space address */
	reg0 = viasio_conf_read(sc->sc_iot, sc->sc_ioh, VT1211_HM_ADDR_LSB);
	reg1 = viasio_conf_read(sc->sc_iot, sc->sc_ioh, VT1211_HM_ADDR_MSB);
	iobase = (reg1 << 8) | reg0;
	DPRINTF((", addr 0x%04x", iobase));

	/* Map HM I/O space */
	if (bus_space_map(sc->sc_iot, iobase, VT1211_HM_IOSIZE, 0,
	    &sc->sc_hm_ioh)) {
		printf(" can't map i/o space");
		return;
	}

	/*
	 * Check if hardware monitoring is enabled by firmware.  If not
	 * try to enable it only if requested.
	 */
	reg0 = bus_space_read_1(sc->sc_iot, sc->sc_hm_ioh, VT1211_HM_CONF);
	DPRINTF((", CONF 0x%02x", reg0));
	if ((reg0 & VT1211_HM_CONF_START) == 0) {
		if ((sc->sc_dev.dv_cfdata->cf_flags &
		    VIASIO_CFFLAGS_HM_ENABLE) != 0) {
			reg0 |= VT1211_HM_CONF_START;
			bus_space_write_1(sc->sc_iot, sc->sc_hm_ioh,
			    VT1211_HM_CONF, reg0);
			reg0 = bus_space_read_1(sc->sc_iot, sc->sc_hm_ioh,
			    VT1211_HM_CONF);
			DPRINTF((", new CONF 0x%02x", reg0));
			if ((reg0 & VT1211_HM_CONF_START) == 0) {
				printf(" failed to enable monitoring");
				return;
			}
		} else {
			printf(" monitoring not enabled");
			return;
		}
	}

	/* Read PWM clock frequency */
	reg0 = bus_space_read_1(sc->sc_iot, sc->sc_hm_ioh, VT1211_HM_PWMCS);
	sc->sc_hm_clock = vt1211_hm_clock[reg0 & 0x07];
	DPRINTF((", PWMCS 0x%02x, %dHz", reg0, sc->sc_hm_clock));

	/* Temperature reading 1 */
	sc->sc_hm_sensors[VT1211_HMS_TEMP1].type = SENSOR_TEMP;

	/* Universal channels (UCH) 1-5 */
	reg0 = bus_space_read_1(sc->sc_iot, sc->sc_hm_ioh, VT1211_HM_UCHCONF);
	DPRINTF((", UCHCONF 0x%02x", reg0));
	for (i = 1; i <= 5; i++) {
		/* UCH can be configured either as thermal or voltage input */
		if (VT1211_HM_UCHCONF_ISTEMP(reg0, i)) {
			sc->sc_hm_sensors[VT1211_HMS_UCH1 + i - 1].type =
			    SENSOR_TEMP;
		} else {
			sc->sc_hm_sensors[VT1211_HMS_UCH1 + i - 1].type =
			    SENSOR_VOLTS_DC;
		}
		snprintf(sc->sc_hm_sensors[VT1211_HMS_UCH1 + i - 1].desc,
		    sizeof(sc->sc_hm_sensors[VT1211_HMS_UCH1 + i - 1].desc),
		    "UCH%d", i);
	}

	/* Internal +3.3V */
	sc->sc_hm_sensors[VT1211_HMS_33V].type = SENSOR_VOLTS_DC;
	strlcpy(sc->sc_hm_sensors[VT1211_HMS_33V].desc, "+3.3V",
	    sizeof(sc->sc_hm_sensors[VT1211_HMS_33V].desc));

	/* FAN reading 1, 2 */
	sc->sc_hm_sensors[VT1211_HMS_FAN1].type = SENSOR_FANRPM;
	sc->sc_hm_sensors[VT1211_HMS_FAN2].type = SENSOR_FANRPM;

	/* Start sensors */
	strlcpy(sc->sc_sensordev.xname, sc->sc_dev.dv_xname,
	    sizeof(sc->sc_sensordev.xname));
	for (i = 0; i < VT1211_HM_NSENSORS; i++)
		sensor_attach(&sc->sc_sensordev, &sc->sc_hm_sensors[i]);
	sensordev_install(&sc->sc_sensordev);
	timeout_set(&sc->sc_hm_timo, viasio_hm_refresh, sc);
	timeout_add_sec(&sc->sc_hm_timo, 1);
}

void
viasio_hm_refresh(void *arg)
{
	struct viasio_softc *sc = arg;
	u_int8_t reg0, reg1;
	int64_t val, rfact;
	int i;

	/* TEMP1 is a 10-bit thermal input */
	reg0 = bus_space_read_1(sc->sc_iot, sc->sc_hm_ioh, VT1211_HM_TEMP1);
	reg1 = bus_space_read_1(sc->sc_iot, sc->sc_hm_ioh, VT1211_HM_TCONF1);
	reg1 = VT1211_HM_TCONF1_TEMP1(reg1);
	val = (reg0 << 2) | reg1;

	/* Convert to uK */
	/* XXX: conversion function is guessed */
	val = viasio_raw2temp(val);
	if (val == -1) {
		sc->sc_hm_sensors[VT1211_HMS_TEMP1].flags |= SENSOR_FINVALID;
	} else {
		sc->sc_hm_sensors[VT1211_HMS_TEMP1].flags &= ~SENSOR_FINVALID;
		sc->sc_hm_sensors[VT1211_HMS_TEMP1].value = val;
	}

	/* Universal channels 1-5 */
	for (i = 1; i <= 5; i++) {
		if (sc->sc_hm_sensors[VT1211_HMS_UCH1 + i - 1].type ==
		    SENSOR_TEMP) {
			/* UCH is a 10-bit thermal input */
			reg0 = bus_space_read_1(sc->sc_iot, sc->sc_hm_ioh,
			    VT1211_HM_UCH1 + i - 1);
			if (i == 1) {
				reg1 = bus_space_read_1(sc->sc_iot,
				    sc->sc_hm_ioh, VT1211_HM_VID4);
				reg1 = VT1211_HM_VID4_UCH1(reg1);
			} else {
				reg1 = bus_space_read_1(sc->sc_iot,
				    sc->sc_hm_ioh, VT1211_HM_ETR);
				reg1 = VT1211_HM_ETR_UCH(reg1, i);
			}
			val = (reg0 << 2) | reg1;

			/* Convert to uK */
			/* XXX: conversion function is guessed */
			val = viasio_raw2temp(val);
			if (val == -1) {
				sc->sc_hm_sensors[VT1211_HMS_UCH1 +
				    i - 1].flags |= SENSOR_FINVALID;
			} else {
				sc->sc_hm_sensors[VT1211_HMS_UCH1 +
				    i - 1].flags &= ~SENSOR_FINVALID;
				sc->sc_hm_sensors[VT1211_HMS_UCH1 +
				    i - 1].value = val;
			}
		} else {
			/* UCH is a voltage input */
			reg0 = bus_space_read_1(sc->sc_iot, sc->sc_hm_ioh,
			    VT1211_HM_UCH1 + i - 1);
			val = reg0;

			/* Convert to uV */
			/* XXX: conversion function is guessed */
			rfact = vt1211_hm_vrfact[i - 1];
			sc->sc_hm_sensors[VT1211_HMS_UCH1 + i - 1].value =
			    ((val * 100000000000ULL) / (rfact * 958));
		}
	}

	/* Read internal +3.3V */
	reg0 = bus_space_read_1(sc->sc_iot, sc->sc_hm_ioh, VT1211_HM_33V);
	val = reg0;

	/* Convert to uV */
	/* XXX: conversion function is guessed */
	rfact = vt1211_hm_vrfact[5];
	sc->sc_hm_sensors[VT1211_HMS_33V].value = ((val * 100000000000ULL) /
	    (rfact * 958));

	/* Read FAN1 clock counter and divisor */
	reg0 = bus_space_read_1(sc->sc_iot, sc->sc_hm_ioh, VT1211_HM_FAN1);
	reg1 = bus_space_read_1(sc->sc_iot, sc->sc_hm_ioh, VT1211_HM_FSCTL);
	reg1 = VT1211_HM_FSCTL_DIV1(reg1);
	val = reg0 << reg1;

	/* Convert to RPM */
	/* XXX: conversion function is guessed */
	if (val != 0) {		
		sc->sc_hm_sensors[VT1211_HMS_FAN1].value =
		    (sc->sc_hm_clock * 60 / 2) / val;
		sc->sc_hm_sensors[VT1211_HMS_FAN1].flags &= ~SENSOR_FINVALID;
	} else {
		sc->sc_hm_sensors[VT1211_HMS_FAN1].flags |= SENSOR_FINVALID;
	}

	/* Read FAN2 clock counter and divisor */
	reg0 = bus_space_read_1(sc->sc_iot, sc->sc_hm_ioh, VT1211_HM_FAN2);
	reg1 = bus_space_read_1(sc->sc_iot, sc->sc_hm_ioh, VT1211_HM_FSCTL);
	reg1 = VT1211_HM_FSCTL_DIV2(reg1);
	val = reg0 << reg1;

	/* Convert to RPM */
	/* XXX: conversion function is guessed */
	if (val != 0) {		
		sc->sc_hm_sensors[VT1211_HMS_FAN2].value =
		    (sc->sc_hm_clock * 60 / 2) / val;
		sc->sc_hm_sensors[VT1211_HMS_FAN2].flags &= ~SENSOR_FINVALID;
	} else {
		sc->sc_hm_sensors[VT1211_HMS_FAN2].flags |= SENSOR_FINVALID;
	}

	timeout_add_sec(&sc->sc_hm_timo, 1);
}

void
viasio_wdg_init(struct viasio_softc *sc)
{
	u_int8_t reg0, reg1;
	u_int16_t iobase;

	printf(", WDG");

	/* Select WDG logical device */
	viasio_conf_write(sc->sc_iot, sc->sc_ioh, VT1211_LDN, VT1211_LDN_WDG);

	/*
	 * Check if logical device is activated by firmware.  If not
	 * try to activate it only if requested.
	 */
	reg0 = viasio_conf_read(sc->sc_iot, sc->sc_ioh, VT1211_WDG_ACT);
	DPRINTF((": ACT 0x%02x", reg0));
	if ((reg0 & VT1211_WDG_ACT_EN) == 0) {
		if ((sc->sc_dev.dv_cfdata->cf_flags &
		    VIASIO_CFFLAGS_WDG_ENABLE) != 0) {
			reg0 |= VT1211_WDG_ACT_EN;
			viasio_conf_write(sc->sc_iot, sc->sc_ioh,
			    VT1211_WDG_ACT, reg0);
			reg0 = viasio_conf_read(sc->sc_iot, sc->sc_ioh,
			    VT1211_WDG_ACT);
			DPRINTF((", new ACT 0x%02x", reg0));
			if ((reg0 & VT1211_WDG_ACT_EN) == 0) {
				printf(" failed to activate");
				return;
			}
		} else {
			printf(" not activated");
			return;
		}
	}

	/* Read WDG I/O space address */
	reg0 = viasio_conf_read(sc->sc_iot, sc->sc_ioh, VT1211_WDG_ADDR_LSB);
	reg1 = viasio_conf_read(sc->sc_iot, sc->sc_ioh, VT1211_WDG_ADDR_MSB);
	iobase = (reg1 << 8) | reg0;
	DPRINTF((", addr 0x%04x", iobase));

	/* Map WDG I/O space */
	if (bus_space_map(sc->sc_iot, iobase, VT1211_WDG_IOSIZE, 0,
	    &sc->sc_wdg_ioh)) {
		printf(" can't map i/o space");
		return;
	}

	/* Register new watchdog */
	wdog_register(viasio_wdg_cb, sc);
}

int
viasio_wdg_cb(void *arg, int period)
{
	struct viasio_softc *sc = arg;
	int mins;

	mins = (period + 59) / 60;
	if (mins > 255)
		mins = 255;

	bus_space_write_1(sc->sc_iot, sc->sc_wdg_ioh, VT1211_WDG_TIMEOUT, mins);
	DPRINTF(("viasio_wdg_cb: %d mins\n", mins));

	return (mins * 60);
}
