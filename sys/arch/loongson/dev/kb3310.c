/*	$OpenBSD: kb3310.c,v 1.24 2024/01/21 07:17:06 miod Exp $	*/
/*
 * Copyright (c) 2010 Otto Moerbeek <otto@drijf.net>
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
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/sensors.h>
#include <sys/timeout.h>

#include <machine/apmvar.h>
#include <machine/autoconf.h>
#include <machine/bus.h>
#include <dev/isa/isavar.h>

#include <dev/pci/glxreg.h>

#include <loongson/dev/bonitoreg.h>
#include <loongson/dev/kb3310var.h>

#include "apm.h"
#include "pckbd.h"
#include "hidkbd.h"

#if NPCKBD > 0 || NHIDKBD > 0
#include <dev/ic/pckbcvar.h>
#include <dev/pckbc/pckbdvar.h>
#include <dev/hid/hidkbdvar.h>
#endif

struct cfdriver ykbec_cd = {
	NULL, "ykbec", DV_DULL,
};

#ifdef KB3310_DEBUG
#define DPRINTF(x)	printf x
#else
#define DPRINTF(x)
#endif

#define IO_YKBEC		0x381
#define IO_YKBECSIZE		0x3

static const struct {
	const char *desc;
	int type;
} ykbec_table[] = {
#define YKBEC_FAN	0
	{ NULL,				SENSOR_FANRPM },
#define YKBEC_ITEMP	1
	{ "Internal temperature",	SENSOR_TEMP },
#define YKBEC_FCAP	2
	{ "Battery full charge capacity", SENSOR_AMPHOUR },
#define YKBEC_BCURRENT	3
	{ "Battery current",		SENSOR_AMPS },
#define YKBEC_BVOLT	4
	{ "Battery voltage",		SENSOR_VOLTS_DC },
#define YKBEC_BTEMP	5
	{ "Battery temperature",	SENSOR_TEMP },
#define YKBEC_CAP	6
	{ "Battery capacity",		SENSOR_PERCENT },
#define YKBEC_CHARGING	7
	{ "Battery charging",		SENSOR_INDICATOR },
#define YKBEC_AC	8
	{ "AC-Power",			SENSOR_INDICATOR },
#define YKBEC_LID	9
	{ "Lid open",			SENSOR_INDICATOR }
#define YKBEC_NSENSORS	10
};

struct ykbec_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;
	struct ksensor		sc_sensor[YKBEC_NSENSORS];
	struct ksensordev	sc_sensordev;
#if NPCKBD > 0 || NHIDKBD > 0
	struct timeout		sc_bell_tmo;
#endif
};

static struct ykbec_softc *ykbec_sc;
static int ykbec_chip_config;

extern void loongson_set_isa_imr(uint);

int	ykbec_match(struct device *, void *, void *);
void	ykbec_attach(struct device *, struct device *, void *);

const struct cfattach ykbec_ca = {
	sizeof(struct ykbec_softc), ykbec_match, ykbec_attach
};

int	ykbec_apminfo(struct apm_power_info *);
void	ykbec_bell(void *, u_int, u_int, u_int, int);
void	ykbec_bell_stop(void *);
void	ykbec_print_bat_info(struct ykbec_softc *);
u_int	ykbec_read(struct ykbec_softc *, u_int);
u_int	ykbec_read16(struct ykbec_softc *, u_int);
void	ykbec_refresh(void *arg);
void	ykbec_write(struct ykbec_softc *, u_int, u_int);

#if NAPM > 0
struct apm_power_info ykbec_apmdata;
const char *ykbec_batstate[] = {
	"high",
	"low",
	"critical",
	"charging",
	"unknown"
};
#define BATTERY_STRING(x) ((x) < nitems(ykbec_batstate) ? \
	ykbec_batstate[x] : ykbec_batstate[4])
#endif

int
ykbec_match(struct device *parent, void *match, void *aux)
{
	struct isa_attach_args *ia = aux;
	bus_space_handle_t ioh;

	/* XXX maybe allow LOONGSON_EBT700 ??? */
	if (sys_platform->system_type != LOONGSON_YEELOONG)
		return (0);

	if ((ia->ia_iobase != IOBASEUNK && ia->ia_iobase != IO_YKBEC) ||
	    /* (ia->ia_iosize != 0 && ia->ia_iosize != IO_YKBECSIZE) || XXX isa.c */
	    ia->ia_maddr != MADDRUNK || ia->ia_msize != 0 ||
	    ia->ia_irq != IRQUNK || ia->ia_drq != DRQUNK)
		return (0);

	if (bus_space_map(ia->ia_iot, IO_YKBEC, IO_YKBECSIZE, 0, &ioh))
		return (0);

	bus_space_unmap(ia->ia_iot, ioh, IO_YKBECSIZE);

	ia->ia_iobase = IO_YKBEC;
	ia->ia_iosize = IO_YKBECSIZE;

	return (1);
}

void
ykbec_attach(struct device *parent, struct device *self, void *aux)
{
	struct isa_attach_args *ia = aux;
	struct ykbec_softc *sc = (struct ykbec_softc *)self;
	int i;

	sc->sc_iot = ia->ia_iot;
	if (bus_space_map(sc->sc_iot, ia->ia_iobase, ia->ia_iosize, 0,
	    &sc->sc_ioh)) {
		printf(": couldn't map I/O space");
		return;
	}

	/* Initialize sensor data. */
	strlcpy(sc->sc_sensordev.xname, sc->sc_dev.dv_xname,
	    sizeof(sc->sc_sensordev.xname));
	if (sensor_task_register(sc, ykbec_refresh, 5) == NULL) {
		printf(", unable to register update task\n");
		return;
	}

#ifdef KB3310_DEBUG
	ykbec_print_bat_info(sc);
#endif
	printf("\n");

	for (i = 0; i < YKBEC_NSENSORS; i++) {
		sc->sc_sensor[i].type = ykbec_table[i].type;
		if (ykbec_table[i].desc)
			strlcpy(sc->sc_sensor[i].desc, ykbec_table[i].desc,
			    sizeof(sc->sc_sensor[i].desc));
		sensor_attach(&sc->sc_sensordev, &sc->sc_sensor[i]);
	}

	sensordev_install(&sc->sc_sensordev);

#if NAPM > 0
	/* make sure we have the apm state initialized before apm attaches */
	ykbec_refresh(sc);
	apm_setinfohook(ykbec_apminfo);
#endif
#if NPCKBD > 0 || NHIDKBD > 0
	timeout_set(&sc->sc_bell_tmo, ykbec_bell_stop, sc);
#if NPCKBD > 0
	pckbd_hookup_bell(ykbec_bell, sc);
#endif
#if NHIDKBD > 0
	hidkbd_hookup_bell(ykbec_bell, sc);
#endif
#endif
	ykbec_sc = sc;
}

void
ykbec_write(struct ykbec_softc *mcsc, u_int reg, u_int datum)
{
	struct ykbec_softc *sc = (struct ykbec_softc *)mcsc;
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;

	bus_space_write_1(iot, ioh, 0, (reg >> 8) & 0xff);
	bus_space_write_1(iot, ioh, 1, (reg >> 0) & 0xff);
	bus_space_write_1(iot, ioh, 2, datum);
}

u_int
ykbec_read(struct ykbec_softc *mcsc, u_int reg)
{
	struct ykbec_softc *sc = (struct ykbec_softc *)mcsc;
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;

	bus_space_write_1(iot, ioh, 0, (reg >> 8) & 0xff);
	bus_space_write_1(iot, ioh, 1, (reg >> 0) & 0xff);
	return bus_space_read_1(iot, ioh, 2);
}

u_int
ykbec_read16(struct ykbec_softc *mcsc, u_int reg)
{
	u_int val;

	val = ykbec_read(mcsc, reg);
	return (val << 8) | ykbec_read(mcsc, reg + 1);
}

#define KB3310_FAN_SPEED_DIVIDER	480000

#define ECTEMP_CURRENT_REG		0xf458
#define REG_FAN_SPEED_HIGH		0xfe22
#define REG_FAN_SPEED_LOW		0xfe23

#define REG_DESIGN_CAP_HIGH		0xf77d
#define REG_DESIGN_CAP_LOW		0xf77e
#define REG_FULLCHG_CAP_HIGH		0xf780
#define REG_FULLCHG_CAP_LOW		0xf781

#define REG_DESIGN_VOL_HIGH		0xf782
#define REG_DESIGN_VOL_LOW		0xf783
#define REG_CURRENT_HIGH		0xf784
#define REG_CURRENT_LOW			0xf785
#define REG_VOLTAGE_HIGH		0xf786
#define REG_VOLTAGE_LOW			0xf787
#define REG_TEMPERATURE_HIGH		0xf788
#define REG_TEMPERATURE_LOW		0xf789
#define REG_RELATIVE_CAT_HIGH		0xf492
#define REG_RELATIVE_CAT_LOW		0xf493
#define REG_BAT_VENDOR			0xf4c4
#define REG_BAT_CELL_COUNT		0xf4c6

#define REG_BAT_CHARGE			0xf4a2
#define BAT_CHARGE_AC			0x00
#define BAT_CHARGE_DISCHARGE		0x01
#define BAT_CHARGE_CHARGE		0x02

#define REG_POWER_FLAG			0xf440
#define POWER_FLAG_ADAPTER_IN		(1<<0)
#define POWER_FLAG_POWER_ON		(1<<1)
#define POWER_FLAG_ENTER_SUS		(1<<2)

#define REG_BAT_STATUS			0xf4b0
#define BAT_STATUS_BAT_EXISTS		(1<<0)
#define BAT_STATUS_BAT_FULL		(1<<1)
#define BAT_STATUS_BAT_DESTROY		(1<<2)
#define BAT_STATUS_BAT_LOW		(1<<5)

#define REG_CHARGE_STATUS		0xf4b1
#define CHARGE_STATUS_PRECHARGE		(1<<1)
#define CHARGE_STATUS_OVERHEAT		(1<<2)

#define REG_BAT_STATE			0xf482
#define BAT_STATE_DISCHARGING		(1<<0)
#define BAT_STATE_CHARGING		(1<<1)

#define	REG_BEEP_CONTROL		0xf4d0
#define	BEEP_ENABLE			(1<<0)

#define REG_PMUCFG			0xff0c
#define PMUCFG_STOP_MODE		(1<<7)
#define PMUCFG_IDLE_MODE		(1<<6)
#define PMUCFG_LPC_WAKEUP		(1<<5)
#define PMUCFG_RESET_8051		(1<<4)
#define PMUCFG_SCI_WAKEUP		(1<<3)
#define PMUCFG_WDT_WAKEUP		(1<<2)
#define PMUCFG_GPWU_WAKEUP		(1<<1)
#define PMUCFG_IRQ_IDLE			(1<<0)

#define REG_USB0			0xf461
#define REG_USB1			0xf462
#define REG_USB2			0xf463
#define USB_FLAG_ON			1
#define USB_FLAG_OFF			0

#define REG_FAN_CONTROL			0xf4d2
#define	REG_FAN_ON			1
#define REG_FAN_OFF			0

#define REG_LID_STATE			0xf4bd
#define LID_OPEN			1
#define LID_CLOSED			0

#define YKBEC_SCI_IRQ			0xa

#ifdef KB3310_DEBUG
void
ykbec_print_bat_info(struct ykbec_softc *sc)
{
	uint bat_status, count, dvolt, dcap;

	printf(": battery ");
	bat_status = ykbec_read(sc, REG_BAT_STATUS);
	if (!ISSET(bat_status, BAT_STATUS_BAT_EXISTS)) {
		printf("absent");
		return;
	}

	count = ykbec_read(sc, REG_BAT_CELL_COUNT);
	dvolt = ykbec_read16(sc, REG_DESIGN_VOL_HIGH);
	dcap = ykbec_read16(sc, REG_DESIGN_CAP_HIGH);
	printf("%d cells, design capacity %dmV %dmAh", count, dvolt, dcap);
}
#endif

void
ykbec_refresh(void *arg)
{
	struct ykbec_softc *sc = (struct ykbec_softc *)arg;
	u_int val, bat_charge, bat_status, charge_status, bat_state, power_flag;
	u_int lid_state, cap_pct, fullcap;
	int current;
#if NAPM > 0
	struct apm_power_info old;
#endif

	val = ykbec_read16(sc, REG_FAN_SPEED_HIGH) & 0xfffff;
	if (val != 0) {
		val = KB3310_FAN_SPEED_DIVIDER / val;
		sc->sc_sensor[YKBEC_FAN].value = val;
		CLR(sc->sc_sensor[YKBEC_FAN].flags, SENSOR_FINVALID);
	} else
		SET(sc->sc_sensor[YKBEC_FAN].flags, SENSOR_FINVALID);

	val = ykbec_read(sc, ECTEMP_CURRENT_REG);
	sc->sc_sensor[YKBEC_ITEMP].value = val * 1000000 + 273150000;

	fullcap = ykbec_read16(sc, REG_FULLCHG_CAP_HIGH);
	sc->sc_sensor[YKBEC_FCAP].value = fullcap * 1000;

	current = ykbec_read16(sc, REG_CURRENT_HIGH);
	/* sign extend short -> int, int -> int64 will be done next statement */
	current |= -(current & 0x8000);
	sc->sc_sensor[YKBEC_BCURRENT].value = -1000 * current;

	sc->sc_sensor[YKBEC_BVOLT].value = ykbec_read16(sc, REG_VOLTAGE_HIGH) *
	    1000;

	val = ykbec_read16(sc, REG_TEMPERATURE_HIGH);
	sc->sc_sensor[YKBEC_BTEMP].value = val * 1000000 + 273150000;

	cap_pct = ykbec_read16(sc, REG_RELATIVE_CAT_HIGH);
	sc->sc_sensor[YKBEC_CAP].value = cap_pct * 1000;

	bat_charge = ykbec_read(sc, REG_BAT_CHARGE);
	bat_status = ykbec_read(sc, REG_BAT_STATUS);
	charge_status = ykbec_read(sc, REG_CHARGE_STATUS);
	bat_state = ykbec_read(sc, REG_BAT_STATE);
	power_flag = ykbec_read(sc, REG_POWER_FLAG);
	lid_state = ykbec_read(sc, REG_LID_STATE);

	sc->sc_sensor[YKBEC_CHARGING].value = !!ISSET(bat_state,
	    BAT_STATE_CHARGING);
	sc->sc_sensor[YKBEC_AC].value = !!ISSET(power_flag,
	    POWER_FLAG_ADAPTER_IN);

	sc->sc_sensor[YKBEC_LID].value = !!ISSET(lid_state, LID_OPEN);

	sc->sc_sensor[YKBEC_CAP].status = ISSET(bat_status, BAT_STATUS_BAT_LOW) ?
		SENSOR_S_CRIT : SENSOR_S_OK;

#if NAPM > 0
	bcopy(&ykbec_apmdata, &old, sizeof(old));
	ykbec_apmdata.battery_life = cap_pct;
	ykbec_apmdata.ac_state = ISSET(power_flag, POWER_FLAG_ADAPTER_IN) ?
	    APM_AC_ON : APM_AC_OFF;
	if (!ISSET(bat_status, BAT_STATUS_BAT_EXISTS)) {
		ykbec_apmdata.battery_state = APM_BATTERY_ABSENT;
		ykbec_apmdata.minutes_left = 0;
		ykbec_apmdata.battery_life = 0;
	} else {
		if (ISSET(bat_state, BAT_STATE_CHARGING))
			ykbec_apmdata.battery_state = APM_BATT_CHARGING;
		else if (ISSET(bat_status, BAT_STATUS_BAT_LOW))
			ykbec_apmdata.battery_state = APM_BATT_CRITICAL;
		else if (cap_pct > 50)
			ykbec_apmdata.battery_state = APM_BATT_HIGH;
		else
			ykbec_apmdata.battery_state = APM_BATT_LOW;

		/* if charging, current is positive */
		if (ISSET(bat_state, BAT_STATE_CHARGING))
			current = 0;
		else
			current = -current;
		/* XXX Yeeloong draw is about 1A */
		if (current <= 0)
			current = 1000;
		/* XXX at 5?%, the Yeeloong shuts down */
		if (cap_pct <= 5)
			cap_pct = 0;
		else
			cap_pct -= 5;
		fullcap = cap_pct * 60 * fullcap / 100;
		ykbec_apmdata.minutes_left = fullcap / current;

	}
	if (old.ac_state != ykbec_apmdata.ac_state)
		apm_record_event(APM_POWER_CHANGE, "AC power",
			ykbec_apmdata.ac_state ? "restored" : "lost");
	if (old.battery_state != ykbec_apmdata.battery_state)
		apm_record_event(APM_POWER_CHANGE, "battery",
		    BATTERY_STRING(ykbec_apmdata.battery_state));
#endif
}

#if NAPM > 0
int
ykbec_apminfo(struct apm_power_info *info)
{
	bcopy(&ykbec_apmdata, info, sizeof(struct apm_power_info));
	return 0;
}

int
ykbec_suspend()
{
	struct ykbec_softc *sc = ykbec_sc;
	int ctrl;

	/*
	 * Set up wakeup sources: currently only the internal keyboard.
	 */
	loongson_set_isa_imr(1 << 1);

	/* USB */
	DPRINTF(("USB\n"));
	ykbec_write(sc, REG_USB0, USB_FLAG_OFF);
	ykbec_write(sc, REG_USB1, USB_FLAG_OFF);
	ykbec_write(sc, REG_USB2, USB_FLAG_OFF);

	/* EC */
	DPRINTF(("REG_PMUCFG\n"));
	ctrl = PMUCFG_SCI_WAKEUP | PMUCFG_WDT_WAKEUP | PMUCFG_GPWU_WAKEUP |
	    PMUCFG_LPC_WAKEUP | PMUCFG_STOP_MODE | PMUCFG_RESET_8051;
	ykbec_write(sc, REG_PMUCFG, ctrl);

	/* FAN */
	DPRINTF(("FAN\n"));
	ykbec_write(sc, REG_FAN_CONTROL, REG_FAN_OFF);

	/* CPU */
	DPRINTF(("CPU\n"));
	ykbec_chip_config = REGVAL(LOONGSON_CHIP_CONFIG0);
	enableintr();
	REGVAL(LOONGSON_CHIP_CONFIG0) = ykbec_chip_config & ~0x7;
	(void)REGVAL(LOONGSON_CHIP_CONFIG0);

	/*
	 * When a resume interrupt fires, we will enter the interrupt
	 * dispatcher, which will do nothing because we are at splhigh,
	 * and execution flow will return here and continue.
	 */
	(void)disableintr();

	return 0;
}

int
ykbec_resume()
{
	struct ykbec_softc *sc = ykbec_sc;

	/* CPU */
	DPRINTF(("CPU\n"));
	REGVAL(LOONGSON_CHIP_CONFIG0) = ykbec_chip_config;
	(void)REGVAL(LOONGSON_CHIP_CONFIG0);

	/* FAN */
	DPRINTF(("FAN\n"));
	ykbec_write(sc, REG_FAN_CONTROL, REG_FAN_ON);

	/* USB */
	DPRINTF(("USB\n"));
	ykbec_write(sc, REG_USB0, USB_FLAG_ON);
	ykbec_write(sc, REG_USB1, USB_FLAG_ON);
	ykbec_write(sc, REG_USB2, USB_FLAG_ON);

	ykbec_refresh(sc);

	return 0;
}
#endif

#if NPCKBD > 0 || NHIDKBD > 0
void
ykbec_bell(void *arg, u_int pitch, u_int period, u_int volume, int poll)
{
	struct ykbec_softc *sc = (struct ykbec_softc *)arg;
	int bctrl;
	int s;

	s = spltty();
	bctrl = ykbec_read(sc, REG_BEEP_CONTROL);
	if (timeout_del(&sc->sc_bell_tmo) || volume == 0) {
		/* inline ykbec_bell_stop(arg); */
		ykbec_write(sc, REG_BEEP_CONTROL, bctrl & ~BEEP_ENABLE);
	}

	if (volume != 0) {
		ykbec_write(sc, REG_BEEP_CONTROL, bctrl | BEEP_ENABLE);
		if (poll) {
			delay(period * 1000);
			ykbec_write(sc, REG_BEEP_CONTROL, bctrl & ~BEEP_ENABLE);
		} else {
			timeout_add_msec(&sc->sc_bell_tmo, period);
		}
	}
	splx(s);
}

void
ykbec_bell_stop(void *arg)
{
	struct ykbec_softc *sc = (struct ykbec_softc *)arg;
	int s;

	s = spltty();
	ykbec_write(sc, REG_BEEP_CONTROL,
	    ykbec_read(sc, REG_BEEP_CONTROL) & ~BEEP_ENABLE);
	splx(s);
}
#endif
