/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012 Damjan Marion <dmarion@Freebsd.org>
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");
/*
* TI TPS65217 PMIC companion chip for AM335x SoC sitting on I2C bus
*/
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/eventhandler.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/clock.h>
#include <sys/time.h>
#include <sys/bus.h>
#include <sys/proc.h>
#include <sys/reboot.h>
#include <sys/resource.h>
#include <sys/rman.h>

#include <dev/iicbus/iicbus.h>
#include <dev/iicbus/iiconf.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <arm/ti/am335x/am335x_rtcvar.h>
#include <arm/ti/am335x/tps65217x.h>

#include "iicbus_if.h"

#define MAX_IIC_DATA_SIZE	2


struct am335x_pmic_softc {
	device_t		sc_dev;
	uint32_t		sc_addr;
	struct intr_config_hook enum_hook;
	struct resource		*sc_irq_res;
	void			*sc_intrhand;
};

static const char *tps65217_voreg_c[4] = {"4.10V", "4.15V", "4.20V", "4.25V"};

static int am335x_pmic_bootverbose = 0;
TUNABLE_INT("hw.am335x_pmic.bootverbose", &am335x_pmic_bootverbose);
static char am335x_pmic_vo[6];
TUNABLE_STR("hw.am335x_pmic.vo", am335x_pmic_vo, sizeof(am335x_pmic_vo));

static void am335x_pmic_shutdown(void *, int);

static int
am335x_pmic_read(device_t dev, uint8_t addr, uint8_t *data, uint8_t size)
{
	struct am335x_pmic_softc *sc = device_get_softc(dev);
	struct iic_msg msg[] = {
		{ sc->sc_addr, IIC_M_WR, 1, &addr },
		{ sc->sc_addr, IIC_M_RD, size, data },
	};
	return (iicbus_transfer(dev, msg, 2));
}

static int
am335x_pmic_write(device_t dev, uint8_t address, uint8_t *data, uint8_t size)
{
	uint8_t buffer[MAX_IIC_DATA_SIZE + 1];
	struct am335x_pmic_softc *sc = device_get_softc(dev);
	struct iic_msg msg[] = {
		{ sc->sc_addr, IIC_M_WR, size + 1, buffer },
	};

	if (size > MAX_IIC_DATA_SIZE)
		return (ENOMEM);

	buffer[0] = address;
	memcpy(buffer + 1, data, size);

	return (iicbus_transfer(dev, msg, 1));
}

static void
am335x_pmic_intr(void *arg)
{
	struct am335x_pmic_softc *sc = (struct am335x_pmic_softc *)arg;
	struct tps65217_status_reg status_reg;
	struct tps65217_int_reg int_reg;
	int rv;
	char notify_buf[16];

	THREAD_SLEEPING_OK();
	rv = am335x_pmic_read(sc->sc_dev, TPS65217_INT_REG, (uint8_t *)&int_reg, 1);
	if (rv != 0) {
		device_printf(sc->sc_dev, "Cannot read interrupt register\n");
		THREAD_NO_SLEEPING();
		return;
	}
	rv = am335x_pmic_read(sc->sc_dev, TPS65217_STATUS_REG, (uint8_t *)&status_reg, 1);
	if (rv != 0) {
		device_printf(sc->sc_dev, "Cannot read status register\n");
		THREAD_NO_SLEEPING();
		return;
	}
	THREAD_NO_SLEEPING();

	if (int_reg.pbi && status_reg.pb)
		shutdown_nice(RB_POWEROFF);
	if (int_reg.aci) {
		snprintf(notify_buf, sizeof(notify_buf), "notify=0x%02x",
		    status_reg.acpwr);
		devctl_notify_f("ACPI", "ACAD", "power", notify_buf, M_NOWAIT);
	}
}

static int
am335x_pmic_probe(device_t dev)
{
	struct am335x_pmic_softc *sc;

	if (!ofw_bus_is_compatible(dev, "ti,tps65217"))
		return (ENXIO);

	sc = device_get_softc(dev);
	sc->sc_dev = dev;
	/* Convert to 8-bit addressing */
	sc->sc_addr = iicbus_get_addr(dev);

	device_set_desc(dev, "TI TPS65217 Power Management IC");

	return (0);
}

static void
am335x_pmic_dump_chgconfig(device_t dev)
{
	struct tps65217_chgconfig0_reg reg0;
	struct tps65217_chgconfig1_reg reg1;
	struct tps65217_chgconfig2_reg reg2;
	struct tps65217_chgconfig3_reg reg3;
	const char *e_d[] = {"enabled", "disabled"};
	const char *d_e[] = {"disabled", "enabled"};
	const char *i_a[] = {"inactive", "active"};
	const char *f_t[] = {"false", "true"};
	const char *timer_c[] = {"4h", "5h", "6h", "8h"};
	const char *ntc_type_c[] = {"100k", "10k"};
	const char *vprechg_c[] = {"2.9V", "2.5V"};
	const char *trange_c[] = {"0-45 C", "0-60 C"};
	const char *termif_c[] = {"2.5%", "7.5%", "15%", "18%"};
	const char *pchrgt_c[] = {"30 min", "60 min"};
	const char *dppmth_c[] = {"3.50V", "3.75V", "4.00V", "4.25V"};
	const char *ichrg_c[] = {"300mA", "400mA", "500mA", "700mA"};

	am335x_pmic_read(dev, TPS65217_CHGCONFIG0_REG, (uint8_t *)&reg0, 1);
	device_printf(dev, " BAT TEMP/NTC ERROR: %s\n", f_t[reg0.battemp]);
	device_printf(dev, " Pre-charge timer time-out: %s\n", f_t[reg0.pchgtout]);
	device_printf(dev, " Charge timer time-out: %s\n", f_t[reg0.chgtout]);
	device_printf(dev, " Charger active: %s\n", f_t[reg0.active]);
	device_printf(dev, " Termination current detected: %s\n", f_t[reg0.termi]);
	device_printf(dev, " Thermal suspend: %s\n", f_t[reg0.tsusp]);
	device_printf(dev, " DPPM active: %s\n", f_t[reg0.dppm]);
	device_printf(dev, " Thermal regulation: %s\n", i_a[reg0.treg]);

	am335x_pmic_read(dev, TPS65217_CHGCONFIG1_REG, (uint8_t *)&reg1, 1);
	device_printf(dev, " Charger: %s\n", d_e[reg1.chg_en]);
	device_printf(dev, " Suspend charge: %s\n", i_a[reg1.susp]);
	device_printf(dev, " Charge termination: %s\n", e_d[reg1.term]);
	device_printf(dev, " Charger reset: %s\n", i_a[reg1.reset]);
	device_printf(dev, " NTC TYPE: %s\n", ntc_type_c[reg1.ntc_type]);
	device_printf(dev, " Safety timer: %s\n", d_e[reg1.tmr_en]);
	device_printf(dev, " Charge safety timer: %s\n", timer_c[reg1.timer]);

	am335x_pmic_read(dev, TPS65217_CHGCONFIG2_REG, (uint8_t *)&reg2, 1);
	device_printf(dev, " Charge voltage: %s\n", tps65217_voreg_c[reg2.voreg]);
	device_printf(dev, " Pre-charge to fast charge transition voltage: %s\n",
	    vprechg_c[reg2.vprechg]);
	device_printf(dev, " Dynamic timer function: %s\n", d_e[reg2.dyntmr]);

	am335x_pmic_read(dev, TPS65217_CHGCONFIG3_REG, (uint8_t *)&reg3, 1);
	device_printf(dev, " Temperature range for charging: %s\n", trange_c[reg3.trange]);
	device_printf(dev, " Termination current factor: %s\n", termif_c[reg3.termif]);
	device_printf(dev, " Pre-charge time: %s\n", pchrgt_c[reg3.pchrgt]);
	device_printf(dev, " Power path DPPM threshold: %s\n", dppmth_c[reg3.dppmth]);
	device_printf(dev, " Charge current: %s\n", ichrg_c[reg3.ichrg]);
}

static void
am335x_pmic_setvo(device_t dev, uint8_t vo)
{
	struct tps65217_chgconfig2_reg reg2;

	am335x_pmic_read(dev, TPS65217_CHGCONFIG2_REG, (uint8_t *)&reg2, 1);
	reg2.voreg = vo;
	am335x_pmic_write(dev, TPS65217_CHGCONFIG2_REG, (uint8_t *)&reg2, 1);
}

static void
am335x_pmic_start(void *xdev)
{
	struct am335x_pmic_softc *sc;
	device_t dev = (device_t)xdev;
	struct tps65217_status_reg status_reg;
	struct tps65217_chipid_reg chipid_reg;
	uint8_t reg, vo;
	char name[20];
	char pwr[4][11] = {"Battery", "USB", "AC", "USB and AC"};
	int rv;

	sc = device_get_softc(dev);

	am335x_pmic_read(dev, TPS65217_CHIPID_REG, (uint8_t *)&chipid_reg, 1);
	switch (chipid_reg.chip) {
		case TPS65217A:
			sprintf(name, "TPS65217A ver 1.%u", chipid_reg.rev);
			break;
		case TPS65217B:
			sprintf(name, "TPS65217B ver 1.%u", chipid_reg.rev);
			break;
		case TPS65217C:
			sprintf(name, "TPS65217C ver 1.%u", chipid_reg.rev);
			break;
		case TPS65217D:
			sprintf(name, "TPS65217D ver 1.%u", chipid_reg.rev);
			break;
		default:
			sprintf(name, "Unknown PMIC");
	}

	am335x_pmic_read(dev, TPS65217_STATUS_REG, (uint8_t *)&status_reg, 1);
	device_printf(dev, "%s powered by %s\n", name,
	    pwr[status_reg.usbpwr | (status_reg.acpwr << 1)]);

	if (am335x_pmic_vo[0] != '\0') {
		for (vo = 0; vo < 4; vo++) {
			if (strcmp(tps65217_voreg_c[vo], am335x_pmic_vo) == 0)
				break;
		}
		if (vo == 4) {
			device_printf(dev, "WARNING: hw.am335x_pmic.vo=\"%s\""
			    ": unsupported value\n", am335x_pmic_vo);
		} else {
			am335x_pmic_setvo(dev, vo);
		}
	}

	if (bootverbose || am335x_pmic_bootverbose) {
		am335x_pmic_dump_chgconfig(dev);
	}

	EVENTHANDLER_REGISTER(shutdown_final, am335x_pmic_shutdown, dev,
	    SHUTDOWN_PRI_LAST);

	config_intrhook_disestablish(&sc->enum_hook);

	/* Unmask all interrupts and clear pending status */
	reg = 0;
	am335x_pmic_write(dev, TPS65217_INT_REG, &reg, 1);
	am335x_pmic_read(dev, TPS65217_INT_REG, &reg, 1);

	if (sc->sc_irq_res != NULL) {
		rv = bus_setup_intr(dev, sc->sc_irq_res,
		    INTR_TYPE_MISC | INTR_MPSAFE, NULL, am335x_pmic_intr,
		    sc, &sc->sc_intrhand);
		if (rv != 0)
			device_printf(dev,
			    "Unable to setup the irq handler.\n");
	}
}

static int
am335x_pmic_attach(device_t dev)
{
	struct am335x_pmic_softc *sc;
	int rid;

	sc = device_get_softc(dev);

	rid = 0;
	sc->sc_irq_res = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid,
	    RF_ACTIVE);
	if (!sc->sc_irq_res) {
		device_printf(dev, "cannot allocate interrupt\n");
		/* return (ENXIO); */
	}

	sc->enum_hook.ich_func = am335x_pmic_start;
	sc->enum_hook.ich_arg = dev;

	if (config_intrhook_establish(&sc->enum_hook) != 0)
		return (ENOMEM);

	return (0);
}

static void
am335x_pmic_shutdown(void *xdev, int howto)
{
	device_t dev;
	struct tps65217_status_reg reg;

	if (!(howto & RB_POWEROFF))
		return;
	dev = (device_t)xdev;
	am335x_pmic_read(dev, TPS65217_STATUS_REG, (uint8_t *)&reg, 1);
	/* Set the OFF bit on status register to start the shutdown sequence. */
	reg.off = 1;
	am335x_pmic_write(dev, TPS65217_STATUS_REG, (uint8_t *)&reg, 1);
	/* Toggle pmic_pwr_enable to shutdown the PMIC. */
	am335x_rtc_pmic_pwr_toggle();
}

static device_method_t am335x_pmic_methods[] = {
	DEVMETHOD(device_probe,		am335x_pmic_probe),
	DEVMETHOD(device_attach,	am335x_pmic_attach),
	{0, 0},
};

static driver_t am335x_pmic_driver = {
	"am335x_pmic",
	am335x_pmic_methods,
	sizeof(struct am335x_pmic_softc),
};

static devclass_t am335x_pmic_devclass;

DRIVER_MODULE(am335x_pmic, iicbus, am335x_pmic_driver, am335x_pmic_devclass, 0, 0);
MODULE_VERSION(am335x_pmic, 1);
MODULE_DEPEND(am335x_pmic, iicbus, 1, 1, 1);
