/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2006 Michael Lorenz
 * Copyright 2008 by Nathan Whitehorn
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
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/kthread.h>
#include <sys/clock.h>
#include <sys/proc.h>
#include <sys/reboot.h>
#include <sys/sysctl.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/openfirm.h>
#include <dev/led/led.h>

#include <machine/_inttypes.h>
#include <machine/bus.h>
#include <machine/cpu.h>
#include <machine/hid.h>
#include <machine/intr_machdep.h>
#include <machine/md_var.h>
#include <machine/pcb.h>
#include <machine/pio.h>
#include <machine/resource.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#include <sys/rman.h>

#include <dev/adb/adb.h>

#include "clock_if.h"
#include "pmuvar.h"
#include "viareg.h"
#include "uninorthvar.h"	/* For unin_chip_sleep()/unin_chip_wake() */

#define PMU_DEFAULTS	PMU_INT_TICK | PMU_INT_ADB | \
	PMU_INT_PCEJECT | PMU_INT_SNDBRT | \
	PMU_INT_BATTERY | PMU_INT_ENVIRONMENT

/*
 * Bus interface
 */
static int	pmu_probe(device_t);
static int	pmu_attach(device_t);
static int	pmu_detach(device_t);

/*
 * Clock interface
 */
static int	pmu_gettime(device_t dev, struct timespec *ts);
static int	pmu_settime(device_t dev, struct timespec *ts);

/*
 * ADB Interface
 */

static u_int	pmu_adb_send(device_t dev, u_char command_byte, int len, 
		    u_char *data, u_char poll);
static u_int	pmu_adb_autopoll(device_t dev, uint16_t mask);
static u_int	pmu_poll(device_t dev);

/*
 * Power interface
 */

static void	pmu_shutdown(void *xsc, int howto);
static void	pmu_set_sleepled(void *xsc, int onoff);
static int	pmu_server_mode(SYSCTL_HANDLER_ARGS);
static int	pmu_acline_state(SYSCTL_HANDLER_ARGS);
static int	pmu_query_battery(struct pmu_softc *sc, int batt, 
		    struct pmu_battstate *info);
static int	pmu_battquery_sysctl(SYSCTL_HANDLER_ARGS);
static int	pmu_battmon(SYSCTL_HANDLER_ARGS);
static void	pmu_battquery_proc(void);
static void	pmu_battery_notify(struct pmu_battstate *batt,
		    struct pmu_battstate *old);

/*
 * List of battery-related sysctls we might ask for
 */

enum {
	PMU_BATSYSCTL_PRESENT	= 1 << 8,
	PMU_BATSYSCTL_CHARGING	= 2 << 8,
	PMU_BATSYSCTL_CHARGE	= 3 << 8,
	PMU_BATSYSCTL_MAXCHARGE = 4 << 8,
	PMU_BATSYSCTL_CURRENT	= 5 << 8,
	PMU_BATSYSCTL_VOLTAGE	= 6 << 8,
	PMU_BATSYSCTL_TIME	= 7 << 8,
	PMU_BATSYSCTL_LIFE	= 8 << 8
};

static device_method_t  pmu_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		pmu_probe),
	DEVMETHOD(device_attach,	pmu_attach),
        DEVMETHOD(device_detach,        pmu_detach),
        DEVMETHOD(device_shutdown,      bus_generic_shutdown),

	/* ADB bus interface */
	DEVMETHOD(adb_hb_send_raw_packet,   pmu_adb_send),
	DEVMETHOD(adb_hb_controller_poll,   pmu_poll),
	DEVMETHOD(adb_hb_set_autopoll_mask, pmu_adb_autopoll),

	/* Clock interface */
	DEVMETHOD(clock_gettime,	pmu_gettime),
	DEVMETHOD(clock_settime,	pmu_settime),

	DEVMETHOD_END
};

static driver_t pmu_driver = {
	"pmu",
	pmu_methods,
	sizeof(struct pmu_softc),
};

static devclass_t pmu_devclass;

EARLY_DRIVER_MODULE(pmu, macio, pmu_driver, pmu_devclass, 0, 0,
    BUS_PASS_RESOURCE);
DRIVER_MODULE(adb, pmu, adb_driver, adb_devclass, 0, 0);

static int	pmuextint_probe(device_t);
static int	pmuextint_attach(device_t);

static device_method_t  pmuextint_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		pmuextint_probe),
	DEVMETHOD(device_attach,	pmuextint_attach),
	
	{0,0}
};

static driver_t pmuextint_driver = {
	"pmuextint",
	pmuextint_methods,
	0
};

static devclass_t pmuextint_devclass;

EARLY_DRIVER_MODULE(pmuextint, macgpio, pmuextint_driver, pmuextint_devclass,
    0, 0, BUS_PASS_RESOURCE);

/* Make sure uhid is loaded, as it turns off some of the ADB emulation */
MODULE_DEPEND(pmu, usb, 1, 1, 1);

static void pmu_intr(void *arg);
static void pmu_in(struct pmu_softc *sc);
static void pmu_out(struct pmu_softc *sc);
static void pmu_ack_on(struct pmu_softc *sc);
static void pmu_ack_off(struct pmu_softc *sc);
static int pmu_send(void *cookie, int cmd, int length, uint8_t *in_msg,
	int rlen, uint8_t *out_msg);
static uint8_t pmu_read_reg(struct pmu_softc *sc, u_int offset);
static void pmu_write_reg(struct pmu_softc *sc, u_int offset, uint8_t value);
static int pmu_intr_state(struct pmu_softc *);

/* these values shows that number of data returned after 'send' cmd is sent */
static signed char pm_send_cmd_type[] = {
	  -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
	  -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
	0x01, 0x01,   -1,   -1,   -1,   -1,   -1,   -1,
	0x00, 0x00,   -1,   -1,   -1,   -1,   -1, 0x00,
	  -1, 0x00, 0x02, 0x01, 0x01,   -1,   -1,   -1,
	0x00,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
	0x04, 0x14,   -1, 0x03,   -1,   -1,   -1,   -1,
	0x00, 0x00, 0x02, 0x02,   -1,   -1,   -1,   -1,
	0x01, 0x01,   -1,   -1,   -1,   -1,   -1,   -1,
	0x00, 0x00,   -1,   -1, 0x01,   -1,   -1,   -1,
	0x01, 0x00, 0x02, 0x02,   -1, 0x01, 0x03, 0x01,
	0x00, 0x01, 0x00, 0x00, 0x00,   -1,   -1,   -1,
	0x02,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   -1,   -1,
	0x01, 0x01, 0x01,   -1,   -1,   -1,   -1,   -1,
	0x00, 0x00,   -1,   -1,   -1, 0x05, 0x04, 0x04,
	0x04,   -1, 0x00,   -1,   -1,   -1,   -1,   -1,
	0x00,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
	0x01, 0x02,   -1,   -1,   -1,   -1,   -1,   -1,
	0x00, 0x00,   -1,   -1,   -1,   -1,   -1,   -1,
	0x02, 0x02, 0x02, 0x04,   -1, 0x00,   -1,   -1,
	0x01, 0x01, 0x03, 0x02,   -1,   -1,   -1,   -1,
	  -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
	  -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
	  -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
	  -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
	0x00,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
	0x01, 0x01,   -1,   -1, 0x00, 0x00,   -1,   -1,
	  -1, 0x04, 0x00,   -1,   -1,   -1,   -1,   -1,
	0x03,   -1, 0x00,   -1, 0x00,   -1,   -1, 0x00,
	  -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
	  -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1
};

/* these values shows that number of data returned after 'receive' cmd is sent */
static signed char pm_receive_cmd_type[] = {
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	  -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x02, 0x02,   -1,   -1,   -1,   -1,   -1, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	  -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x05, 0x15,   -1, 0x02,   -1,   -1,   -1,   -1,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x02, 0x02,   -1,   -1,   -1,   -1,   -1,   -1,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x02, 0x00, 0x03, 0x03,   -1,   -1,   -1,   -1,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x04, 0x04, 0x03, 0x09,   -1,   -1,   -1,   -1,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	  -1,   -1,   -1,   -1,   -1, 0x01, 0x01, 0x01,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x06,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x02, 0x02,   -1,   -1,   -1,   -1,   -1,   -1,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x02, 0x00, 0x00, 0x00,   -1,   -1,   -1,   -1,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	  -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	  -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x02, 0x02,   -1,   -1, 0x02,   -1,   -1,   -1,
	0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
	  -1,   -1, 0x02,   -1,   -1,   -1,   -1, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	  -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
};

static int pmu_battmon_enabled = 1;
static struct proc *pmubattproc;
static struct kproc_desc pmu_batt_kp = {
	"pmu_batt",
	pmu_battquery_proc,
	&pmubattproc
};

/* We only have one of each device, so globals are safe */
static device_t pmu = NULL;
static device_t pmu_extint = NULL;

static int
pmuextint_probe(device_t dev)
{
	const char *type = ofw_bus_get_type(dev);

	if (strcmp(type, "extint-gpio1") != 0)
                return (ENXIO);

	device_set_desc(dev, "Apple PMU99 External Interrupt");
	return (0);
}

static int
pmu_probe(device_t dev)
{
	const char *type = ofw_bus_get_type(dev);

	if (strcmp(type, "via-pmu") != 0)
                return (ENXIO);

	device_set_desc(dev, "Apple PMU99 Controller");
	return (0);
}


static int
setup_pmu_intr(device_t dev, device_t extint)
{
	struct pmu_softc *sc;
	sc = device_get_softc(dev);

	sc->sc_irqrid = 0;
	sc->sc_irq = bus_alloc_resource_any(extint, SYS_RES_IRQ, &sc->sc_irqrid,
           	RF_ACTIVE);
        if (sc->sc_irq == NULL) {
                device_printf(dev, "could not allocate interrupt\n");
                return (ENXIO);
        }

	if (bus_setup_intr(dev, sc->sc_irq, INTR_TYPE_MISC | INTR_MPSAFE 
	    | INTR_ENTROPY, NULL, pmu_intr, dev, &sc->sc_ih) != 0) {
                device_printf(dev, "could not setup interrupt\n");
                bus_release_resource(dev, SYS_RES_IRQ, sc->sc_irqrid,
                    sc->sc_irq);
                return (ENXIO);
        }

	return (0);
}

static int
pmuextint_attach(device_t dev)
{
	pmu_extint = dev;
	if (pmu)
		return (setup_pmu_intr(pmu,dev));

	return (0);
}

static int
pmu_attach(device_t dev)
{
	struct pmu_softc *sc;

	int i;
	uint8_t reg;
	uint8_t cmd[2] = {2, 0};
	uint8_t resp[16];
	phandle_t node,child;
	struct sysctl_ctx_list *ctx;
	struct sysctl_oid *tree;
	
	sc = device_get_softc(dev);
	sc->sc_dev = dev;
	
	sc->sc_memrid = 0;
	sc->sc_memr = bus_alloc_resource_any(dev, SYS_RES_MEMORY, 
		          &sc->sc_memrid, RF_ACTIVE);

	mtx_init(&sc->sc_mutex,"pmu",NULL,MTX_DEF | MTX_RECURSE);

	if (sc->sc_memr == NULL) {
		device_printf(dev, "Could not alloc mem resource!\n");
		return (ENXIO);
	}

	/*
	 * Our interrupt is attached to a GPIO pin. Depending on probe order,
	 * we may not have found it yet. If we haven't, it will find us, and
	 * attach our interrupt then.
	 */
	pmu = dev;
	if (pmu_extint != NULL) {
		if (setup_pmu_intr(dev,pmu_extint) != 0)
			return (ENXIO);
	}

	sc->sc_autopoll = 0;
	sc->sc_batteries = 0;
	sc->adb_bus = NULL;
	sc->sc_leddev = NULL;

	/* Init PMU */

	pmu_write_reg(sc, vBufB, pmu_read_reg(sc, vBufB) | vPB4);
	pmu_write_reg(sc, vDirB, (pmu_read_reg(sc, vDirB) | vPB4) & ~vPB3);

	reg = PMU_DEFAULTS;
	pmu_send(sc, PMU_SET_IMASK, 1, &reg, 16, resp);

	pmu_write_reg(sc, vIER, 0x94); /* make sure VIA interrupts are on */

	pmu_send(sc, PMU_SYSTEM_READY, 1, cmd, 16, resp);
	pmu_send(sc, PMU_GET_VERSION, 0, cmd, 16, resp);

	/* Initialize child buses (ADB) */
	node = ofw_bus_get_node(dev);

	for (child = OF_child(node); child != 0; child = OF_peer(child)) {
		char name[32];

		memset(name, 0, sizeof(name));
		OF_getprop(child, "name", name, sizeof(name));

		if (bootverbose)
			device_printf(dev, "PMU child <%s>\n",name);

		if (strncmp(name, "adb", 4) == 0) {
			sc->adb_bus = device_add_child(dev,"adb",-1);
		}

		if (strncmp(name, "power-mgt", 9) == 0) {
			uint32_t prim_info[9];

			if (OF_getprop(child, "prim-info", prim_info, 
			    sizeof(prim_info)) >= 7) 
				sc->sc_batteries = (prim_info[6] >> 16) & 0xff;

			if (bootverbose && sc->sc_batteries > 0)
				device_printf(dev, "%d batteries detected\n",
				    sc->sc_batteries);
		}
	}

	/*
	 * Set up sysctls
	 */

	ctx = device_get_sysctl_ctx(dev);
	tree = device_get_sysctl_tree(dev);

	SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
	    "server_mode", CTLTYPE_INT | CTLFLAG_RW, sc, 0,
	    pmu_server_mode, "I", "Enable reboot after power failure");

	if (sc->sc_batteries > 0) {
		struct sysctl_oid *oid, *battroot;
		char battnum[2];

		/* Only start the battery monitor if we have a battery. */
		kproc_start(&pmu_batt_kp);
		SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
		    "monitor_batteries", CTLTYPE_INT | CTLFLAG_RW, sc, 0,
		    pmu_battmon, "I", "Post battery events to devd");


		SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
		    "acline", CTLTYPE_INT | CTLFLAG_RD, sc, 0,
		    pmu_acline_state, "I", "AC Line Status");

		battroot = SYSCTL_ADD_NODE(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
		    "batteries", CTLFLAG_RD, 0, "Battery Information");

		for (i = 0; i < sc->sc_batteries; i++) {
			battnum[0] = i + '0';
			battnum[1] = '\0';

			oid = SYSCTL_ADD_NODE(ctx, SYSCTL_CHILDREN(battroot),
			    OID_AUTO, battnum, CTLFLAG_RD, 0, 
			    "Battery Information");
		
			SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(oid), OID_AUTO,
			    "present", CTLTYPE_INT | CTLFLAG_RD, sc, 
			    PMU_BATSYSCTL_PRESENT | i, pmu_battquery_sysctl, 
			    "I", "Battery present");
			SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(oid), OID_AUTO,
			    "charging", CTLTYPE_INT | CTLFLAG_RD, sc,
			    PMU_BATSYSCTL_CHARGING | i, pmu_battquery_sysctl, 
			    "I", "Battery charging");
			SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(oid), OID_AUTO,
			    "charge", CTLTYPE_INT | CTLFLAG_RD, sc,
			    PMU_BATSYSCTL_CHARGE | i, pmu_battquery_sysctl, 
			    "I", "Battery charge (mAh)");
			SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(oid), OID_AUTO,
			    "maxcharge", CTLTYPE_INT | CTLFLAG_RD, sc,
			    PMU_BATSYSCTL_MAXCHARGE | i, pmu_battquery_sysctl, 
			    "I", "Maximum battery capacity (mAh)");
			SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(oid), OID_AUTO,
			    "rate", CTLTYPE_INT | CTLFLAG_RD, sc,
			    PMU_BATSYSCTL_CURRENT | i, pmu_battquery_sysctl, 
			    "I", "Battery discharge rate (mA)");
			SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(oid), OID_AUTO,
			    "voltage", CTLTYPE_INT | CTLFLAG_RD, sc,
			    PMU_BATSYSCTL_VOLTAGE | i, pmu_battquery_sysctl, 
			    "I", "Battery voltage (mV)");

			/* Knobs for mental compatibility with ACPI */

			SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(oid), OID_AUTO,
			    "time", CTLTYPE_INT | CTLFLAG_RD, sc,
			    PMU_BATSYSCTL_TIME | i, pmu_battquery_sysctl, 
			    "I", "Time Remaining (minutes)");
			SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(oid), OID_AUTO,
			    "life", CTLTYPE_INT | CTLFLAG_RD, sc,
			    PMU_BATSYSCTL_LIFE | i, pmu_battquery_sysctl, 
			    "I", "Capacity remaining (percent)");
		}
	}

	/*
	 * Set up LED interface
	 */

	sc->sc_leddev = led_create(pmu_set_sleepled, sc, "sleepled");

	/*
	 * Register RTC
	 */

	clock_register(dev, 1000);

	/*
	 * Register power control handler
	 */
	EVENTHANDLER_REGISTER(shutdown_final, pmu_shutdown, sc,
	    SHUTDOWN_PRI_LAST);

	return (bus_generic_attach(dev));
}

static int 
pmu_detach(device_t dev) 
{
	struct pmu_softc *sc;

	sc = device_get_softc(dev);

	if (sc->sc_leddev != NULL)
		led_destroy(sc->sc_leddev);

	bus_teardown_intr(dev, sc->sc_irq, sc->sc_ih);
	bus_release_resource(dev, SYS_RES_IRQ, sc->sc_irqrid, sc->sc_irq);
	bus_release_resource(dev, SYS_RES_MEMORY, sc->sc_memrid, sc->sc_memr);
	mtx_destroy(&sc->sc_mutex);

	return (bus_generic_detach(dev));
}

static uint8_t
pmu_read_reg(struct pmu_softc *sc, u_int offset) 
{
	return (bus_read_1(sc->sc_memr, offset));
}

static void
pmu_write_reg(struct pmu_softc *sc, u_int offset, uint8_t value) 
{
	bus_write_1(sc->sc_memr, offset, value);
}

static int
pmu_send_byte(struct pmu_softc *sc, uint8_t data)
{

	pmu_out(sc);
	pmu_write_reg(sc, vSR, data);
	pmu_ack_off(sc);
	/* wait for intr to come up */
	/* XXX should add a timeout and bail if it expires */
	do {} while (pmu_intr_state(sc) == 0);
	pmu_ack_on(sc);
	do {} while (pmu_intr_state(sc));
	pmu_ack_on(sc);
	return 0;
}

static inline int
pmu_read_byte(struct pmu_softc *sc, uint8_t *data)
{
	volatile uint8_t scratch;
	pmu_in(sc);
	scratch = pmu_read_reg(sc, vSR);
	pmu_ack_off(sc);
	/* wait for intr to come up */
	do {} while (pmu_intr_state(sc) == 0);
	pmu_ack_on(sc);
	do {} while (pmu_intr_state(sc));
	*data = pmu_read_reg(sc, vSR);
	return 0;
}

static int
pmu_intr_state(struct pmu_softc *sc)
{
	return ((pmu_read_reg(sc, vBufB) & vPB3) == 0);
}

static int
pmu_send(void *cookie, int cmd, int length, uint8_t *in_msg, int rlen,
    uint8_t *out_msg)
{
	struct pmu_softc *sc = cookie;
	int i, rcv_len = -1;
	uint8_t out_len, intreg;

	intreg = pmu_read_reg(sc, vIER);
	intreg &= 0x10;
	pmu_write_reg(sc, vIER, intreg);

	/* wait idle */
	do {} while (pmu_intr_state(sc));

	/* send command */
	pmu_send_byte(sc, cmd);

	/* send length if necessary */
	if (pm_send_cmd_type[cmd] < 0) {
		pmu_send_byte(sc, length);
	}

	for (i = 0; i < length; i++) {
		pmu_send_byte(sc, in_msg[i]);
	}

	/* see if there's data to read */
	rcv_len = pm_receive_cmd_type[cmd];
	if (rcv_len == 0) 
		goto done;

	/* read command */
	if (rcv_len == 1) {
		pmu_read_byte(sc, out_msg);
		goto done;
	} else
		out_msg[0] = cmd;
	if (rcv_len < 0) {
		pmu_read_byte(sc, &out_len);
		rcv_len = out_len + 1;
	}
	for (i = 1; i < min(rcv_len, rlen); i++)
		pmu_read_byte(sc, &out_msg[i]);

done:
	pmu_write_reg(sc, vIER, (intreg == 0) ? 0 : 0x90);

	return rcv_len;
}


static u_int
pmu_poll(device_t dev)
{
	pmu_intr(dev);
	return (0);
}

static void
pmu_in(struct pmu_softc *sc)
{
	uint8_t reg;

	reg = pmu_read_reg(sc, vACR);
	reg &= ~vSR_OUT;
	reg |= 0x0c;
	pmu_write_reg(sc, vACR, reg);
}

static void
pmu_out(struct pmu_softc *sc)
{
	uint8_t reg;

	reg = pmu_read_reg(sc, vACR);
	reg |= vSR_OUT;
	reg |= 0x0c;
	pmu_write_reg(sc, vACR, reg);
}

static void
pmu_ack_off(struct pmu_softc *sc)
{
	uint8_t reg;

	reg = pmu_read_reg(sc, vBufB);
	reg &= ~vPB4;
	pmu_write_reg(sc, vBufB, reg);
}

static void
pmu_ack_on(struct pmu_softc *sc)
{
	uint8_t reg;

	reg = pmu_read_reg(sc, vBufB);
	reg |= vPB4;
	pmu_write_reg(sc, vBufB, reg);
}

static void
pmu_intr(void *arg)
{
	device_t        dev;
	struct pmu_softc *sc;

	unsigned int len;
	uint8_t resp[16];
	uint8_t junk[16];

        dev = (device_t)arg;
	sc = device_get_softc(dev);

	mtx_lock(&sc->sc_mutex);

	pmu_write_reg(sc, vIFR, 0x90);	/* Clear 'em */
	len = pmu_send(sc, PMU_INT_ACK, 0, NULL, 16, resp);

	mtx_unlock(&sc->sc_mutex);

	if ((len < 1) || (resp[1] == 0)) {
		return;
	}

	if (resp[1] & PMU_INT_ADB) {
		/*
		 * the PMU will turn off autopolling after each command that
		 * it did not issue, so we assume any but TALK R0 is ours and
		 * re-enable autopoll here whenever we receive an ACK for a
		 * non TR0 command.
		 */
		mtx_lock(&sc->sc_mutex);

		if ((resp[2] & 0x0f) != (ADB_COMMAND_TALK << 2)) {
			if (sc->sc_autopoll) {
				uint8_t cmd[] = {0, PMU_SET_POLL_MASK, 
				    (sc->sc_autopoll >> 8) & 0xff, 
				    sc->sc_autopoll & 0xff};

				pmu_send(sc, PMU_ADB_CMD, 4, cmd, 16, junk);
			}
		}	

		mtx_unlock(&sc->sc_mutex);

		adb_receive_raw_packet(sc->adb_bus,resp[1],resp[2],
			len - 3,&resp[3]);
	}
	if (resp[1] & PMU_INT_ENVIRONMENT) {
		/* if the lid was just closed, notify devd. */
		if ((resp[2] & PMU_ENV_LID_CLOSED) && (!sc->lid_closed)) {
			sc->lid_closed = 1;
			devctl_notify("PMU", "lid", "close", NULL);
		}
		else if (!(resp[2] & PMU_ENV_LID_CLOSED) && (sc->lid_closed)) {
			/* if the lid was just opened, notify devd. */
			sc->lid_closed = 0;
			devctl_notify("PMU", "lid", "open", NULL);
		}
		if (resp[2] & PMU_ENV_POWER)
			devctl_notify("PMU", "Button", "pressed", NULL);
	}
}

static u_int
pmu_adb_send(device_t dev, u_char command_byte, int len, u_char *data, 
    u_char poll)
{
	struct pmu_softc *sc = device_get_softc(dev);
	int i,replen;
	uint8_t packet[16], resp[16];

	/* construct an ADB command packet and send it */

	packet[0] = command_byte;

	packet[1] = 0;
	packet[2] = len;
	for (i = 0; i < len; i++)
		packet[i + 3] = data[i];

	mtx_lock(&sc->sc_mutex);
	replen = pmu_send(sc, PMU_ADB_CMD, len + 3, packet, 16, resp);
	mtx_unlock(&sc->sc_mutex);

	if (poll)
		pmu_poll(dev);

	return 0;
}

static u_int 
pmu_adb_autopoll(device_t dev, uint16_t mask) 
{
	struct pmu_softc *sc = device_get_softc(dev);

	/* magical incantation to re-enable autopolling */
	uint8_t cmd[] = {0, PMU_SET_POLL_MASK, (mask >> 8) & 0xff, mask & 0xff};
	uint8_t resp[16];

	mtx_lock(&sc->sc_mutex);

	if (sc->sc_autopoll == mask) {
		mtx_unlock(&sc->sc_mutex);
		return 0;
	}

	sc->sc_autopoll = mask & 0xffff;

	if (mask)
		pmu_send(sc, PMU_ADB_CMD, 4, cmd, 16, resp);
	else
		pmu_send(sc, PMU_ADB_POLL_OFF, 0, NULL, 16, resp);

	mtx_unlock(&sc->sc_mutex);
	
	return 0;
}

static void
pmu_shutdown(void *xsc, int howto)
{
	struct pmu_softc *sc = xsc;
	uint8_t cmd[] = {'M', 'A', 'T', 'T'};
	
	if (howto & RB_HALT)
		pmu_send(sc, PMU_POWER_OFF, 4, cmd, 0, NULL);
	else
		pmu_send(sc, PMU_RESET_CPU, 0, NULL, 0, NULL);

	for (;;);
}

static void
pmu_set_sleepled(void *xsc, int onoff)
{
	struct pmu_softc *sc = xsc;
	uint8_t cmd[] = {4, 0, 0};

	cmd[2] = onoff;
	
	mtx_lock(&sc->sc_mutex);
	pmu_send(sc, PMU_SET_SLEEPLED, 3, cmd, 0, NULL);
	mtx_unlock(&sc->sc_mutex);
}

static int
pmu_server_mode(SYSCTL_HANDLER_ARGS)
{
	struct pmu_softc *sc = arg1;
	
	u_int server_mode = 0;
	uint8_t getcmd[] = {PMU_PWR_GET_POWERUP_EVENTS};
	uint8_t setcmd[] = {0, 0, PMU_PWR_WAKEUP_AC_INSERT};
	uint8_t resp[3];
	int error, len;

	mtx_lock(&sc->sc_mutex);
	len = pmu_send(sc, PMU_POWER_EVENTS, 1, getcmd, 3, resp);
	mtx_unlock(&sc->sc_mutex);

	if (len == 3)
		server_mode = (resp[2] & PMU_PWR_WAKEUP_AC_INSERT) ? 1 : 0;

	error = sysctl_handle_int(oidp, &server_mode, 0, req);

	if (len != 3)
		return (EINVAL);

	if (error || !req->newptr)
		return (error);

	if (server_mode == 1)
		setcmd[0] = PMU_PWR_SET_POWERUP_EVENTS;
	else if (server_mode == 0)
		setcmd[0] = PMU_PWR_CLR_POWERUP_EVENTS;
	else
		return (EINVAL);

	setcmd[1] = resp[1];

	mtx_lock(&sc->sc_mutex);
	pmu_send(sc, PMU_POWER_EVENTS, 3, setcmd, 2, resp);
	mtx_unlock(&sc->sc_mutex);

	return (0);
}

static int
pmu_query_battery(struct pmu_softc *sc, int batt, struct pmu_battstate *info)
{
	uint8_t reg;
	uint8_t resp[16];
	int len;

	reg = batt + 1;

	mtx_lock(&sc->sc_mutex);
	len = pmu_send(sc, PMU_SMART_BATTERY_STATE, 1, &reg, 16, resp);
	mtx_unlock(&sc->sc_mutex);

	if (len < 3)
		return (-1);

	/* All PMU battery info replies share a common header:
	 * Byte 1	Payload Format
	 * Byte 2	Battery Flags
	 */

	info->state = resp[2];

	switch (resp[1]) {
	case 3:
	case 4:	
		/*
		 * Formats 3 and 4 appear to be the same:
		 * Byte 3	Charge
		 * Byte 4	Max Charge
		 * Byte 5	Current
		 * Byte 6	Voltage
		 */

		info->charge = resp[3];
		info->maxcharge = resp[4];
		/* Current can be positive or negative */
		info->current = (int8_t)resp[5];
		info->voltage = resp[6];
		break;
	case 5:
		/*
		 * Formats 5 is a wider version of formats 3 and 4
		 * Byte 3-4	Charge
		 * Byte 5-6	Max Charge
		 * Byte 7-8	Current
		 * Byte 9-10	Voltage
		 */

		info->charge = (resp[3] << 8) | resp[4];
		info->maxcharge = (resp[5] << 8) | resp[6];
		/* Current can be positive or negative */
		info->current = (int16_t)((resp[7] << 8) | resp[8]);
		info->voltage = (resp[9] << 8) | resp[10];
		break;
	default:
		device_printf(sc->sc_dev, "Unknown battery info format (%d)!\n",
		    resp[1]);
		return (-1);
	}

	return (0);
}

static void
pmu_battery_notify(struct pmu_battstate *batt, struct pmu_battstate *old)
{
	char notify_buf[16];
	int new_acline, old_acline;

	new_acline = (batt->state & PMU_PWR_AC_PRESENT) ? 1 : 0;
	old_acline = (old->state & PMU_PWR_AC_PRESENT) ? 1 : 0;

	if (new_acline != old_acline) {
		snprintf(notify_buf, sizeof(notify_buf),
		    "notify=0x%02x", new_acline);
		devctl_notify("PMU", "POWER", "ACLINE", notify_buf);
	}
}

static void
pmu_battquery_proc()
{
	struct pmu_softc *sc;
	struct pmu_battstate batt;
	struct pmu_battstate cur_batt;
	int error;

	sc = device_get_softc(pmu);

	bzero(&cur_batt, sizeof(cur_batt));
	while (1) {
		kproc_suspend_check(curproc);
		error = pmu_query_battery(sc, 0, &batt);
		pmu_battery_notify(&batt, &cur_batt);
		cur_batt = batt;
		pause("pmu_batt", hz);
	}
}

static int
pmu_battmon(SYSCTL_HANDLER_ARGS)
{
	struct pmu_softc *sc;
	int error, result;

	sc = arg1;
	result = pmu_battmon_enabled;

	error = sysctl_handle_int(oidp, &result, 0, req);

	if (error || !req->newptr)
		return (error);
	
	if (!result && pmu_battmon_enabled)
		error = kproc_suspend(pmubattproc, hz);
	else if (result && pmu_battmon_enabled == 0)
		error = kproc_resume(pmubattproc);
	pmu_battmon_enabled = (result != 0);

	return (error);
}

static int
pmu_acline_state(SYSCTL_HANDLER_ARGS)
{
	struct pmu_softc *sc;
	struct pmu_battstate batt;
	int error, result;

	sc = arg1;

	/* The PMU treats the AC line status as a property of the battery */
	error = pmu_query_battery(sc, 0, &batt);

	if (error != 0)
		return (error);
	
	result = (batt.state & PMU_PWR_AC_PRESENT) ? 1 : 0;
	error = sysctl_handle_int(oidp, &result, 0, req);

	return (error);
}

static int
pmu_battquery_sysctl(SYSCTL_HANDLER_ARGS)
{
	struct pmu_softc *sc;
	struct pmu_battstate batt;
	int error, result;

	sc = arg1;

	error = pmu_query_battery(sc, arg2 & 0x00ff, &batt);

	if (error != 0)
		return (error);

	switch (arg2 & 0xff00) {
	case PMU_BATSYSCTL_PRESENT:
		result = (batt.state & PMU_PWR_BATT_PRESENT) ? 1 : 0;
		break;
	case PMU_BATSYSCTL_CHARGING:
		result = (batt.state & PMU_PWR_BATT_CHARGING) ? 1 : 0;
		break;
	case PMU_BATSYSCTL_CHARGE:
		result = batt.charge;
		break;
	case PMU_BATSYSCTL_MAXCHARGE:
		result = batt.maxcharge;
		break;
	case PMU_BATSYSCTL_CURRENT:
		result = batt.current;
		break;
	case PMU_BATSYSCTL_VOLTAGE:
		result = batt.voltage;
		break;
	case PMU_BATSYSCTL_TIME:
		/* Time remaining until full charge/discharge, in minutes */

		if (batt.current >= 0)
			result = (batt.maxcharge - batt.charge) /* mAh */ * 60 
			    / batt.current /* mA */;
		else
			result = (batt.charge /* mAh */ * 60) 
			    / (-batt.current /* mA */);
		break;
	case PMU_BATSYSCTL_LIFE:
		/* Battery charge fraction, in percent */
		result = (batt.charge * 100) / batt.maxcharge;
		break;
	default:
		/* This should never happen */
		result = -1;
	}

	error = sysctl_handle_int(oidp, &result, 0, req);

	return (error);
}

#define DIFF19041970	2082844800

static int
pmu_gettime(device_t dev, struct timespec *ts)
{
	struct pmu_softc *sc = device_get_softc(dev);
	uint8_t resp[16];
	uint32_t sec;

	mtx_lock(&sc->sc_mutex);
	pmu_send(sc, PMU_READ_RTC, 0, NULL, 16, resp);
	mtx_unlock(&sc->sc_mutex);

	memcpy(&sec, &resp[1], 4);
	ts->tv_sec = sec - DIFF19041970;
	ts->tv_nsec = 0;

	return (0);
}

static int
pmu_settime(device_t dev, struct timespec *ts)
{
	struct pmu_softc *sc = device_get_softc(dev);
	uint32_t sec;

	sec = ts->tv_sec + DIFF19041970;

	mtx_lock(&sc->sc_mutex);
	pmu_send(sc, PMU_SET_RTC, sizeof(sec), (uint8_t *)&sec, 0, NULL);
	mtx_unlock(&sc->sc_mutex);

	return (0);
}

int
pmu_set_speed(int low_speed)
{
	struct pmu_softc *sc;
	uint8_t sleepcmd[] = {'W', 'O', 'O', 'F', 0};
	uint8_t resp[16];

	sc = device_get_softc(pmu);
	pmu_write_reg(sc, vIER, 0x10);
	spinlock_enter();
	mtdec(0x7fffffff);
	mb();
	mtdec(0x7fffffff);

	sleepcmd[4] = low_speed;
	pmu_send(sc, PMU_CPU_SPEED, 5, sleepcmd, 16, resp);
	unin_chip_sleep(NULL, 1);
	platform_sleep();
	unin_chip_wake(NULL);

	mtdec(1);	/* Force a decrementer exception */
	spinlock_exit();
	pmu_write_reg(sc, vIER, 0x90);

	return (0);
}
