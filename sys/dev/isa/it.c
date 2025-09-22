/*	$OpenBSD: it.c,v 1.48 2022/04/16 19:32:22 naddy Exp $	*/

/*
 * Copyright (c) 2007-2008 Oleg Safiullin <form@pdp-11.org.ru>
 * Copyright (c) 2006-2007 Juan Romero Pardines <juan@xtrarom.org>
 * Copyright (c) 2003 Julien Bordet <zejames@greyhats.org>
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
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/sensors.h>

#include <machine/bus.h>

#include <dev/isa/isavar.h>
#include <dev/isa/itvar.h>


#if defined(ITDEBUG)
#define DPRINTF(x)		do { printf x; } while (0)
#else
#define DPRINTF(x)
#endif


int it_match(struct device *, void *, void *);
void it_attach(struct device *, struct device *, void *);
int it_activate(struct device *, int);
u_int8_t it_readreg(bus_space_tag_t, bus_space_handle_t, int);
void it_writereg(bus_space_tag_t, bus_space_handle_t, int, u_int8_t);
void it_enter(bus_space_tag_t, bus_space_handle_t, int);
void it_exit(bus_space_tag_t, bus_space_handle_t);

u_int8_t it_ec_readreg(struct it_softc *, int);
void it_ec_writereg(struct it_softc *, int, u_int8_t);
void it_ec_refresh(void *arg);

int it_wdog_cb(void *, int);

/*
 * IT87-compatible chips can typically measure voltages up to 4.096 V.
 * To measure higher voltages the input is attenuated with (external)
 * resistors.  Negative voltages are measured using a reference
 * voltage.  So we have to convert the sensor values back to real
 * voltages by applying the appropriate resistor factor.
 */
#define RFACT_NONE		10000
#define RFACT(x, y)		(RFACT_NONE * ((x) + (y)) / (y))


const struct {
	enum sensor_type	type;
	const char		*desc;
} it_sensors[IT_EC_NUMSENSORS] = {
#define IT_TEMP_BASE		0
#define IT_TEMP_COUNT		3
	{ SENSOR_TEMP,		NULL		},
	{ SENSOR_TEMP,		NULL		},
	{ SENSOR_TEMP,		NULL		},

#define IT_FAN_BASE		3
#define IT_FAN_COUNT		5
	{ SENSOR_FANRPM,	NULL		},
	{ SENSOR_FANRPM,	NULL		},
	{ SENSOR_FANRPM,	NULL		},
	{ SENSOR_FANRPM,	NULL		},
	{ SENSOR_FANRPM,	NULL		},

#define IT_VOLT_BASE		8
#define IT_VOLT_COUNT		9
	{ SENSOR_VOLTS_DC,	"VCORE_A"	},
	{ SENSOR_VOLTS_DC,	"VCORE_B"	},
	{ SENSOR_VOLTS_DC,	"+3.3V"		},
	{ SENSOR_VOLTS_DC,	"+5V"		},
	{ SENSOR_VOLTS_DC,	"+12V"		},
	{ SENSOR_VOLTS_DC,	"-12V"		},
	{ SENSOR_VOLTS_DC,	"-5V"		},
	{ SENSOR_VOLTS_DC,	"+5VSB"		},
	{ SENSOR_VOLTS_DC,	"VBAT"		}
};

/* rfact values for voltage sensors */
const int it_vrfact[IT_VOLT_COUNT] = {
	RFACT_NONE,		/* VCORE_A	*/
	RFACT_NONE,		/* VCORE_A	*/
	RFACT_NONE,		/* +3.3V	*/
	RFACT(68, 100),		/* +5V		*/
	RFACT(30, 10),		/* +12V		*/
	RFACT(83, 20),		/* -12V		*/
	RFACT(21, 10),		/* -5V		*/
	RFACT(68, 100),		/* +5VSB	*/
	RFACT_NONE		/* VBAT		*/
};

const int it_fan_regs[] = {
	IT_EC_FAN_TAC1, IT_EC_FAN_TAC2, IT_EC_FAN_TAC3,
	IT_EC_FAN_TAC4_LSB, IT_EC_FAN_TAC5_LSB
};

const int it_fan_ext_regs[] = {
	IT_EC_FAN_EXT_TAC1, IT_EC_FAN_EXT_TAC2, IT_EC_FAN_EXT_TAC3,
	IT_EC_FAN_TAC4_MSB, IT_EC_FAN_TAC5_MSB
};

LIST_HEAD(, it_softc) it_softc_list = LIST_HEAD_INITIALIZER(it_softc_list);


int
it_match(struct device *parent, void *match, void *aux)
{
	struct isa_attach_args *ia = aux;
	struct it_softc *sc;
	bus_space_handle_t ioh;
	int ec_iobase, found = 0;
	u_int16_t cr;

	if (ia->ipa_io[0].base != IO_IT1 && ia->ipa_io[0].base != IO_IT2)
		return (0);

	/* map i/o space */
	if (bus_space_map(ia->ia_iot, ia->ipa_io[0].base, 2, 0, &ioh) != 0) {
		DPRINTF(("it_match: can't map i/o space"));
		return (0);
	}

	/* enter MB PnP mode */
	it_enter(ia->ia_iot, ioh, ia->ipa_io[0].base);

	/*
	 * SMSC or similar SuperIO chips use 0x55 magic to enter PnP mode
	 * and 0xaa to exit. These chips also enter PnP mode via ITE
	 * `enter MB PnP mode' sequence, so force chip to exit PnP mode
	 * if this is the case.
	 */
	bus_space_write_1(ia->ia_iot, ioh, IT_IO_ADDR, 0xaa);

	/* get chip id */
	cr = it_readreg(ia->ia_iot, ioh, IT_CHIPID1) << 8;
	cr |= it_readreg(ia->ia_iot, ioh, IT_CHIPID2);

	switch (cr) {
	case IT_ID_8705:
	case IT_ID_8712:
	case IT_ID_8716:
	case IT_ID_8718:
	case IT_ID_8720:
	case IT_ID_8721:
	case IT_ID_8726:
	case IT_ID_8728:
	case IT_ID_8772:
		/* get environment controller base address */
		it_writereg(ia->ia_iot, ioh, IT_LDN, IT_EC_LDN);
		ec_iobase = it_readreg(ia->ia_iot, ioh, IT_EC_MSB) << 8;
		ec_iobase |= it_readreg(ia->ia_iot, ioh, IT_EC_LSB);

		/* check if device already attached */
		LIST_FOREACH(sc, &it_softc_list, sc_list)
			if (sc->sc_ec_iobase == ec_iobase)
				break;

		if (sc == NULL) {
			ia->ipa_nio = 1;
			ia->ipa_io[0].length = 2;
			ia->ipa_nmem = ia->ipa_nirq = ia->ipa_ndrq = 0;
			found++;
		}

		break;
	}

	/* exit MB PnP mode */
	it_exit(ia->ia_iot, ioh);

	/* unmap i/o space */
	bus_space_unmap(ia->ia_iot, ioh, 2);

	return (found);
}

void
it_attach(struct device *parent, struct device *self, void *aux)
{
	struct it_softc *sc = (void *)self;
	struct isa_attach_args *ia = aux;
	int i;

	sc->sc_iot = ia->ia_iot;
	sc->sc_iobase = ia->ipa_io[0].base;
	if (bus_space_map(sc->sc_iot, sc->sc_iobase, 2, 0, &sc->sc_ioh) != 0) {
		printf(": can't map i/o space\n");
		return;
	}

	/* enter MB PnP mode */
	it_enter(sc->sc_iot, sc->sc_ioh, sc->sc_iobase);

	/* get chip id and rev */
	sc->sc_chipid = it_readreg(sc->sc_iot, sc->sc_ioh, IT_CHIPID1) << 8;
	sc->sc_chipid |= it_readreg(sc->sc_iot, sc->sc_ioh, IT_CHIPID2);
	sc->sc_chiprev = it_readreg(sc->sc_iot, sc->sc_ioh, IT_CHIPREV) & 0x0f;

	/* get environment controller base address */
	it_writereg(sc->sc_iot, sc->sc_ioh, IT_LDN, IT_EC_LDN);
	sc->sc_ec_iobase = it_readreg(sc->sc_iot, sc->sc_ioh, IT_EC_MSB) << 8;
	sc->sc_ec_iobase |= it_readreg(sc->sc_iot, sc->sc_ioh, IT_EC_LSB);

	/* initialize watchdog timer */
	if (sc->sc_chipid != IT_ID_8705) {
		it_writereg(sc->sc_iot, sc->sc_ioh, IT_LDN, IT_WDT_LDN);
		it_writereg(sc->sc_iot, sc->sc_ioh, IT_WDT_CSR, 0x00);
		it_writereg(sc->sc_iot, sc->sc_ioh, IT_WDT_TCR, 0x00);
		wdog_register(it_wdog_cb, sc);
	}

	/* exit MB PnP mode and unmap */
	it_exit(sc->sc_iot, sc->sc_ioh);

	LIST_INSERT_HEAD(&it_softc_list, sc, sc_list);
	printf(": IT%xF rev %X", sc->sc_chipid, sc->sc_chiprev);

	if (sc->sc_ec_iobase == 0) {
		printf(", EC disabled\n");
		return;
	}

	printf(", EC port 0x%x\n", sc->sc_ec_iobase);

	/* map environment controller i/o space */
	sc->sc_ec_iot = ia->ia_iot;
	if (bus_space_map(sc->sc_ec_iot, sc->sc_ec_iobase, 8, 0,
	    &sc->sc_ec_ioh) != 0) {
		printf("%s: can't map EC i/o space\n", sc->sc_dev.dv_xname);
		return;
	}

	/* initialize sensor structures */
	for (i = 0; i < IT_EC_NUMSENSORS; i++) {
		sc->sc_sensors[i].type = it_sensors[i].type;

		if (it_sensors[i].desc != NULL)
			strlcpy(sc->sc_sensors[i].desc, it_sensors[i].desc,
			    sizeof(sc->sc_sensors[i].desc));
	}

	/* register sensor update task */
	if (sensor_task_register(sc, it_ec_refresh, IT_EC_INTERVAL) == NULL) {
		printf("%s: unable to register update task\n",
		    sc->sc_dev.dv_xname);
		bus_space_unmap(sc->sc_ec_iot, sc->sc_ec_ioh, 8);
		return;
	}

	/* use 16-bit FAN tachometer registers for newer chips */
	if (sc->sc_chipid != IT_ID_8705 && sc->sc_chipid != IT_ID_8712)
		it_ec_writereg(sc, IT_EC_FAN_ECER,
		    it_ec_readreg(sc, IT_EC_FAN_ECER) | 0x07);

	/* activate monitoring */
	it_ec_writereg(sc, IT_EC_CFG,
	    it_ec_readreg(sc, IT_EC_CFG) | IT_EC_CFG_START | IT_EC_CFG_INTCLR);

	/* initialize sensors */
	strlcpy(sc->sc_sensordev.xname, sc->sc_dev.dv_xname,
	    sizeof(sc->sc_sensordev.xname));
	for (i = 0; i < IT_EC_NUMSENSORS; i++)
		sensor_attach(&sc->sc_sensordev, &sc->sc_sensors[i]);
	sensordev_install(&sc->sc_sensordev);
}

int
it_activate(struct device *self, int act)
{
	switch (act) {
	case DVACT_POWERDOWN:
		wdog_shutdown(self);
		break;
	}

	return (0);
}

u_int8_t
it_readreg(bus_space_tag_t iot, bus_space_handle_t ioh, int r)
{
	bus_space_write_1(iot, ioh, IT_IO_ADDR, r);
	return (bus_space_read_1(iot, ioh, IT_IO_DATA));
}

void
it_writereg(bus_space_tag_t iot, bus_space_handle_t ioh, int r, u_int8_t v)
{
	bus_space_write_1(iot, ioh, IT_IO_ADDR, r);
	bus_space_write_1(iot, ioh, IT_IO_DATA, v);
}

void
it_enter(bus_space_tag_t iot, bus_space_handle_t ioh, int iobase)
{
	bus_space_write_1(iot, ioh, IT_IO_ADDR, 0x87);
	bus_space_write_1(iot, ioh, IT_IO_ADDR, 0x01);
	bus_space_write_1(iot, ioh, IT_IO_ADDR, 0x55);
	if (iobase == IO_IT1)
		bus_space_write_1(iot, ioh, IT_IO_ADDR, 0x55);
	else
		bus_space_write_1(iot, ioh, IT_IO_ADDR, 0xaa);
}

void
it_exit(bus_space_tag_t iot, bus_space_handle_t ioh)
{
	bus_space_write_1(iot, ioh, IT_IO_ADDR, IT_CCR);
	bus_space_write_1(iot, ioh, IT_IO_DATA, 0x02);
}

u_int8_t
it_ec_readreg(struct it_softc *sc, int r)
{
	bus_space_write_1(sc->sc_ec_iot, sc->sc_ec_ioh, IT_EC_ADDR, r);
	return (bus_space_read_1(sc->sc_ec_iot, sc->sc_ec_ioh, IT_EC_DATA));
}

void
it_ec_writereg(struct it_softc *sc, int r, u_int8_t v)
{
	bus_space_write_1(sc->sc_ec_iot, sc->sc_ec_ioh, IT_EC_ADDR, r);
	bus_space_write_1(sc->sc_ec_iot, sc->sc_ec_ioh, IT_EC_DATA, v);
}

void
it_ec_refresh(void *arg)
{
	struct it_softc *sc = arg;
	int i, sdata, divisor, odivisor, ndivisor;
	u_int8_t cr, ecr;

	/* refresh temp sensors */
	cr = it_ec_readreg(sc, IT_EC_ADC_TEMPER);

	for (i = 0; i < IT_TEMP_COUNT; i++) {
		sc->sc_sensors[IT_TEMP_BASE + i].flags &=
		    SENSOR_FINVALID;

		if (!(cr & (1 << i)) && !(cr & (1 << (i + 3)))) {
			sc->sc_sensors[IT_TEMP_BASE + i].flags |=
			    SENSOR_FINVALID;
			continue;
		}

		sdata = it_ec_readreg(sc, IT_EC_TEMPBASE + i);
		/* convert to degF */
		sc->sc_sensors[IT_TEMP_BASE + i].value =
		    sdata * 1000000 + 273150000;
	}

	/* refresh volt sensors */
	cr = it_ec_readreg(sc, IT_EC_ADC_VINER);

	for (i = 0; i < IT_VOLT_COUNT; i++) {
		sc->sc_sensors[IT_VOLT_BASE + i].flags &=
		    SENSOR_FINVALID;

		if ((i < 8) && !(cr & (1 << i))) {
			sc->sc_sensors[IT_VOLT_BASE + i].flags |=
			    SENSOR_FINVALID;
			continue;
		}

		sdata = it_ec_readreg(sc, IT_EC_VOLTBASE + i);
		/* voltage returned as (mV >> 4) */
		sc->sc_sensors[IT_VOLT_BASE + i].value = sdata << 4;
		/* these two values are negative and formula is different */
		if (i == 5 || i == 6)
			sc->sc_sensors[IT_VOLT_BASE + i].value -= IT_EC_VREF;
		/* rfact is (factor * 10^4) */
		sc->sc_sensors[IT_VOLT_BASE + i].value *= it_vrfact[i];
		/* division by 10 gets us back to uVDC */
		sc->sc_sensors[IT_VOLT_BASE + i].value /= 10;
		if (i == 5 || i == 6)
			sc->sc_sensors[IT_VOLT_BASE + i].value +=
			    IT_EC_VREF * 1000;
	}

	/* refresh fan sensors */
	cr = it_ec_readreg(sc, IT_EC_FAN_MCR);

	if (sc->sc_chipid != IT_ID_8705 && sc->sc_chipid != IT_ID_8712) {
		/* use 16-bit FAN tachometer registers */
		ecr = it_ec_readreg(sc, IT_EC_FAN_ECER);

		for (i = 0; i < IT_FAN_COUNT; i++) {
			sc->sc_sensors[IT_FAN_BASE + i].flags &=
			    ~SENSOR_FINVALID;

			if (i < 3 && !(cr & (1 << (i + 4)))) {
				sc->sc_sensors[IT_FAN_BASE + i].flags |=
				    SENSOR_FINVALID;
				continue;
			} else if (i > 2 && !(ecr & (1 << (i + 1)))) {
				sc->sc_sensors[IT_FAN_BASE + i].flags |=
				    SENSOR_FINVALID;
				continue;
			}

			sdata = it_ec_readreg(sc, it_fan_regs[i]);
			sdata |= it_ec_readreg(sc, it_fan_ext_regs[i]) << 8;

			if (sdata == 0 || sdata == 0xffff)
				sc->sc_sensors[IT_FAN_BASE + i].value = 0;
			else
				sc->sc_sensors[IT_FAN_BASE + i].value =
				    675000 / sdata;
		}
	} else {
		/* use 8-bit FAN tachometer & FAN divisor registers */
		odivisor = ndivisor = divisor =
		    it_ec_readreg(sc, IT_EC_FAN_DIV);

		for (i = 0; i < IT_FAN_COUNT; i++) {
			if (i > 2 || !(cr & (1 << (i + 4)))) {
				sc->sc_sensors[IT_FAN_BASE + i].flags |=
				    SENSOR_FINVALID;
				continue;
			}

			sc->sc_sensors[IT_FAN_BASE + i].flags &=
			    ~SENSOR_FINVALID;

			sdata = it_ec_readreg(sc, it_fan_regs[i]);

			if (sdata == 0xff) {
				sc->sc_sensors[IT_FAN_BASE + i].value = 0;

				if (i == 2)
					ndivisor ^= 0x40;
				else {
					ndivisor &= ~(7 << (i * 3));
					ndivisor |= ((divisor + 1) & 7) <<
					    (i * 3);
				}
			} else if (sdata != 0) {
				if (i == 2)
					divisor = divisor & 1 ? 3 : 1;
				sc->sc_sensors[IT_FAN_BASE + i].value =
				    1350000 / (sdata << (divisor & 7));
			} else
				sc->sc_sensors[IT_FAN_BASE + i].value = 0;
		}

		if (ndivisor != odivisor)
			it_ec_writereg(sc, IT_EC_FAN_DIV, ndivisor);
	}
}

int
it_wdog_cb(void *arg, int period)
{
	struct it_softc *sc = arg;
	int minutes = 0;

	/* enter MB PnP mode and select WDT device */
	it_enter(sc->sc_iot, sc->sc_ioh, sc->sc_iobase);
	it_writereg(sc->sc_iot, sc->sc_ioh, IT_LDN, IT_WDT_LDN);

	/* disable watchdog timer */
	it_writereg(sc->sc_iot, sc->sc_ioh, IT_WDT_TCR, 0x00);

	/* 1000s should be enough for everyone */
	if (period > 1000)
		period = 1000;
	else if (period < 0)
		period = 0;

	if (period > 0) {
		/*
		 * Older IT8712F chips have 8-bit timeout counter.
		 * Use minutes for 16-bit values for these chips.
		 */
		if (sc->sc_chipid == IT_ID_8712 && sc->sc_chiprev < 0x8 &&
		    period > 0xff) {
			if (period % 60 >= 30)
				period += 60;
			period /= 60;
			minutes++;
		}

		/* set watchdog timeout (low byte) */
		it_writereg(sc->sc_iot, sc->sc_ioh, IT_WDT_TMO_LSB,
		    period & 0xff);

		if (minutes) {
			/* enable watchdog timer */
			it_writereg(sc->sc_iot, sc->sc_ioh, IT_WDT_TCR,
			    IT_WDT_TCR_KRST | IT_WDT_TCR_PWROK);

			period *= 60;
		} else {
			/* set watchdog timeout (high byte) */
			it_writereg(sc->sc_iot, sc->sc_ioh, IT_WDT_TMO_MSB,
			    period >> 8);

			/* enable watchdog timer */
			it_writereg(sc->sc_iot, sc->sc_ioh, IT_WDT_TCR,
			    IT_WDT_TCR_SECS | IT_WDT_TCR_KRST |
			    IT_WDT_TCR_PWROK);
		}
	}

	/* exit MB PnP mode */
	it_exit(sc->sc_iot, sc->sc_ioh);

	return (period);
}


const struct cfattach it_ca = {
	sizeof(struct it_softc),
	it_match,
	it_attach,
	NULL,
	it_activate
};

struct cfdriver it_cd = {
	NULL, "it", DV_DULL
};
