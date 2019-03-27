/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011 Sandvine Incorporated ULC.
 * Copyright (c) 2012 iXsystems, Inc.
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
/*
 * Support for Winbond watchdog.
 *
 * With minor abstractions it might be possible to add support for other
 * different Winbond Super I/O chips as well.  Winbond seems to have four
 * different types of chips, four different ways to get into extended config
 * mode.
 *
 * Note: there is no serialization between the debugging sysctl handlers and
 * the watchdog functions and possibly others poking the registers at the same
 * time.  For that at least possibly interfering sysctls are hidden by default.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/eventhandler.h>
#include <sys/lock.h>
#include <sys/module.h>
#include <sys/rman.h>
#include <sys/sbuf.h>
#include <sys/sysctl.h>
#include <sys/watchdog.h>

#include <isa/isavar.h>

#include <machine/bus.h>
#include <machine/resource.h>

/*
 * Global registers.
 */
#define	WB_DEVICE_ID_REG	0x20	/* Device ID */
#define	WB_DEVICE_REV_REG	0x21	/* Device revision */
#define	WB_CR26			0x26	/* Bit6: HEFRAS (base port selector) */

/* LDN selection. */
#define	WB_LDN_REG		0x07
#define	WB_LDN_REG_LDN8		0x08	/* GPIO 2, Watchdog */

/*
 * LDN8 (GPIO 2, Watchdog) specific registers and options.
 */
/* CR30: LDN8 activation control. */
#define	WB_LDN8_CR30		0x30
#define	WB_LDN8_CR30_ACTIVE	0x01	/* 1: LD active */

/* CRF5: Watchdog scale, P20. Mapped to reg_1. */
#define	WB_LDN8_CRF5		0xF5
#define	WB_LDN8_CRF5_SCALE	0x08	/* 0: 1s, 1: 60s */
#define	WB_LDN8_CRF5_KEYB_P20	0x04	/* 1: keyb P20 forces timeout */
#define	WB_LDN8_CRF5_KBRST	0x02	/* 1: timeout causes pin60 kbd reset */

/* CRF6: Watchdog Timeout (0 == off). Mapped to reg_timeout. */
#define	WB_LDN8_CRF6		0xF6

/* CRF7: Watchdog mouse, keyb, force, .. Mapped to reg_2. */
#define	WB_LDN8_CRF7		0xF7
#define	WB_LDN8_CRF7_MOUSE	0x80	/* 1: mouse irq resets wd timer */
#define	WB_LDN8_CRF7_KEYB	0x40	/* 1: keyb irq resets wd timer */
#define	WB_LDN8_CRF7_FORCE	0x20	/* 1: force timeout (self-clear) */
#define	WB_LDN8_CRF7_TS		0x10	/* 0: counting, 1: fired */
#define	WB_LDN8_CRF7_IRQS	0x0f	/* irq source for watchdog, 2 == SMI */

enum chips { w83627hf, w83627s, w83697hf, w83697ug, w83637hf, w83627thf,
	     w83687thf, w83627ehf, w83627dhg, w83627uhg, w83667hg,
	     w83627dhg_p, w83667hg_b, nct6775, nct6776, nct6779, nct6791,
	     nct6792, nct6793, nct6795, nct6102 };

struct wb_softc {
	device_t		dev;
	struct resource		*portres;
	bus_space_tag_t		bst;
	bus_space_handle_t	bsh;
	int			rid;
	eventhandler_tag	ev_tag;
	int			(*ext_cfg_enter_f)(struct wb_softc *, u_short);
	void			(*ext_cfg_exit_f)(struct wb_softc *, u_short);
	enum chips		chip;
	uint8_t			ctl_reg;
	uint8_t			time_reg;
	uint8_t			csr_reg;
	int			debug_verbose;

	/*
	 * Special feature to let the watchdog fire at a different
	 * timeout as set by watchdog(4) but still use that API to
	 * re-load it periodically.
	 */
	unsigned int		timeout_override;

	/*
	 * Space to save current state temporary and for sysctls.
	 * We want to know the timeout value and usually need two
	 * additional registers for options. Do not name them by
	 * register as these might be different by chip.
	 */
	uint8_t			reg_timeout;
	uint8_t			reg_1;
	uint8_t			reg_2;
};

static int	ext_cfg_enter_0x87_0x87(struct wb_softc *, u_short);
static void	ext_cfg_exit_0xaa(struct wb_softc *, u_short);

struct winbond_superio_cfg {
	uint8_t			efer;	/* and efir */
	int			(*ext_cfg_enter_f)(struct wb_softc *, u_short);
	void			(*ext_cfg_exit_f)(struct wb_softc *, u_short);
} probe_addrs[] = {
	{
		.efer			= 0x2e,
		.ext_cfg_enter_f	= ext_cfg_enter_0x87_0x87,
		.ext_cfg_exit_f		= ext_cfg_exit_0xaa,
	},
	{
		.efer			= 0x4e,
		.ext_cfg_enter_f	= ext_cfg_enter_0x87_0x87,
		.ext_cfg_exit_f		= ext_cfg_exit_0xaa,
	},
};

struct winbond_vendor_device_id {
	uint8_t			device_id;
	enum chips		chip;
	const char *		descr;
} wb_devs[] = {
	{
		.device_id	= 0x52,
		.chip		= w83627hf,
		.descr		= "Winbond 83627HF/F/HG/G",
	},
	{
		.device_id	= 0x59,
		.chip		= w83627s,
		.descr		= "Winbond 83627S",
	},
	{
		.device_id	= 0x60,
		.chip		= w83697hf,
		.descr		= "Winbond 83697HF",
	},
	{
		.device_id	= 0x68,
		.chip		= w83697ug,
		.descr		= "Winbond 83697UG",
	},
	{
		.device_id	= 0x70,
		.chip		= w83637hf,
		.descr		= "Winbond 83637HF",
	},
	{
		.device_id	= 0x82,
		.chip		= w83627thf,
		.descr		= "Winbond 83627THF",
	},
	{
		.device_id	= 0x85,
		.chip		= w83687thf,
		.descr		= "Winbond 83687THF",
	},
	{
		.device_id	= 0x88,
		.chip		= w83627ehf,
		.descr		= "Winbond 83627EHF",
	},
	{
		.device_id	= 0xa0,
		.chip		= w83627dhg,
		.descr		= "Winbond 83627DHG",
	},
	{
		.device_id	= 0xa2,
		.chip		= w83627uhg,
		.descr		= "Winbond 83627UHG",
	},
	{
		.device_id	= 0xa5,
		.chip		= w83667hg,
		.descr		= "Winbond 83667HG",
	},
	{
		.device_id	= 0xb0,
		.chip		= w83627dhg_p,
		.descr		= "Winbond 83627DHG-P",
	},
	{
		.device_id	= 0xb3,
		.chip		= w83667hg_b,
		.descr		= "Winbond 83667HG-B",
	},
	{
		.device_id	= 0xb4,
		.chip		= nct6775,
		.descr		= "Nuvoton NCT6775",
	},
	{
		.device_id	= 0xc3,
		.chip		= nct6776,
		.descr		= "Nuvoton NCT6776",
	},
	{
		.device_id	= 0xc4,
		.chip		= nct6102,
		.descr		= "Nuvoton NCT6102",
	},
	{
		.device_id	= 0xc5,
		.chip		= nct6779,
		.descr		= "Nuvoton NCT6779",
	},
	{
		.device_id	= 0xc8,
		.chip		= nct6791,
		.descr		= "Nuvoton NCT6791",
	},
	{
		.device_id	= 0xc9,
		.chip		= nct6792,
		.descr		= "Nuvoton NCT6792",
	},
	{
		.device_id	= 0xd1,
		.chip		= nct6793,
		.descr		= "Nuvoton NCT6793",
	},
	{
		.device_id	= 0xd3,
		.chip		= nct6795,
		.descr		= "Nuvoton NCT6795",
	},
};

static void
write_efir_1(struct wb_softc *sc, u_short baseport, uint8_t value)
{

	MPASS(sc != NULL || baseport != 0);
	if (sc != NULL)
		bus_space_write_1((sc)->bst, (sc)->bsh, 0, (value));
	else
		outb(baseport, value);
}

static uint8_t __unused
read_efir_1(struct wb_softc *sc, u_short baseport)
{

	MPASS(sc != NULL || baseport != 0);
	if (sc != NULL)
		return (bus_space_read_1((sc)->bst, (sc)->bsh, 0));
	else
		return (inb(baseport));
}

static void
write_efdr_1(struct wb_softc *sc, u_short baseport, uint8_t value)
{

	MPASS(sc != NULL || baseport != 0);
	if (sc != NULL)
		bus_space_write_1((sc)->bst, (sc)->bsh, 1, (value));
	else
		outb(baseport + 1, value);
}

static uint8_t
read_efdr_1(struct wb_softc *sc, u_short baseport)
{

	MPASS(sc != NULL || baseport != 0);
	if (sc != NULL)
		return (bus_space_read_1((sc)->bst, (sc)->bsh, 1));
	else
		return (inb(baseport + 1));
}

static void
write_reg(struct wb_softc *sc, uint8_t reg, uint8_t value)
{

	write_efir_1(sc, 0, reg);
	write_efdr_1(sc, 0, value);
}

static uint8_t
read_reg(struct wb_softc *sc, uint8_t reg)
{

	write_efir_1(sc, 0, reg);
	return (read_efdr_1(sc, 0));
}

/*
 * Return the watchdog related registers as we last read them.  This will
 * usually not give the current timeout or state on whether the watchdog
 * fired.
 */
static int
sysctl_wb_debug(SYSCTL_HANDLER_ARGS)
{
	struct wb_softc *sc;
	struct sbuf sb;
	int error;

	sc = arg1;

	sbuf_new_for_sysctl(&sb, NULL, 64, req);

	sbuf_printf(&sb, "LDN8 (GPIO2, Watchdog): ");
	sbuf_printf(&sb, "CR%02X 0x%02x ", sc->ctl_reg, sc->reg_1);
	sbuf_printf(&sb, "CR%02X 0x%02x ", sc->time_reg, sc->reg_timeout);
	sbuf_printf(&sb, "CR%02X 0x%02x", sc->csr_reg, sc->reg_2);

	error = sbuf_finish(&sb);
	sbuf_delete(&sb);
	return (error);
}

/*
 * Read the current values before returning them.  Given this might poke
 * the registers the same time as the watchdog, this sysctl handler should
 * be marked CTLFLAG_SKIP to not show up by default.
 */
static int
sysctl_wb_debug_current(SYSCTL_HANDLER_ARGS)
{
	struct wb_softc *sc;

	sc = arg1;

	if ((*sc->ext_cfg_enter_f)(sc, 0) != 0)
		return (ENXIO);

	/* Watchdog is configured as part of LDN 8 (GPIO Port2, Watchdog). */
	write_reg(sc, WB_LDN_REG, WB_LDN_REG_LDN8);

	sc->reg_1 = read_reg(sc, sc->ctl_reg);
	sc->reg_timeout = read_reg(sc, sc->time_reg);
	sc->reg_2 = read_reg(sc, sc->csr_reg);

	(*sc->ext_cfg_exit_f)(sc, 0);

	return (sysctl_wb_debug(oidp, arg1, arg2, req));
}

/*
 * Sysctl handlers to force a watchdog timeout or to test the NMI functionality
 * works as expetced.
 * For testing we could set a test_nmi flag in the softc that, in case of NMI, a
 * callback function from trap.c could check whether we fired and not report the
 * timeout but clear the flag for the sysctl again.  This is interesting given a
 * lot of boards have jumpers to change the action on watchdog timeout or
 * disable the watchdog completely.
 * XXX-BZ notyet: currently no general infrastructure exists to do this.
 */
static int
sysctl_wb_force_test_nmi(SYSCTL_HANDLER_ARGS)
{
	struct wb_softc *sc;
	int error, test, val;

	sc = arg1;
	test = arg2;

#ifdef notyet
	val = sc->test_nmi;
#else
	val = 0;
#endif
	error = sysctl_handle_int(oidp, &val, 0, req);
        if (error || !req->newptr)
                return (error);

#ifdef notyet
	/* Manually clear the test for a value of 0 and do nothing else. */
	if (test && val == 0) {
		sc->test_nmi = 0;
		return (0);
	}
#endif

	if ((*sc->ext_cfg_enter_f)(sc, 0) != 0)
		return (ENXIO);

#ifdef notyet
	/*
	 * If we are testing the NMI functionality, set the flag before
	 * forcing the timeout.
	 */
	if (test)
		sc->test_nmi = 1;
#endif

	/* Watchdog is configured as part of LDN 8 (GPIO Port2, Watchdog). */
	write_reg(sc, WB_LDN_REG, WB_LDN_REG_LDN8);

	/* Force watchdog to fire. */
	sc->reg_2 = read_reg(sc, sc->csr_reg);
	sc->reg_2 |= WB_LDN8_CRF7_FORCE;
	write_reg(sc, sc->csr_reg, sc->reg_2);

	(*sc->ext_cfg_exit_f)(sc, 0);

	return (0);
}

/*
 * Print current watchdog state.
 *
 * Note: it is the responsibility of the caller to update the registers
 * upfront.
 */
static void
wb_print_state(struct wb_softc *sc, const char *msg)
{

	device_printf(sc->dev, "%s%sWatchdog %sabled. %s"
	    "Scaling by %ds, timer at %d (%s=%ds%s). "
	    "CRF5 0x%02x CRF7 0x%02x\n",
	    (msg != NULL) ? msg : "", (msg != NULL) ? ": " : "",
	    (sc->reg_timeout > 0x00) ? "en" : "dis",
	    (sc->reg_2 & WB_LDN8_CRF7_TS) ? "Watchdog fired. " : "",
	    (sc->reg_1 & WB_LDN8_CRF5_SCALE) ? 60 : 1,
	    sc->reg_timeout,
	    (sc->reg_timeout > 0x00) ? "<" : "",
	    sc->reg_timeout * ((sc->reg_1 & WB_LDN8_CRF5_SCALE) ? 60 : 1),
	    (sc->reg_timeout > 0x00) ? " left" : "",
	    sc->reg_1, sc->reg_2);
}

/*
 * Functions to enter and exit extended function mode.  Possibly shared
 * between different chips.
 */
static int
ext_cfg_enter_0x87_0x87(struct wb_softc *sc, u_short baseport)
{

	/*
	 * Enable extended function mode.
	 * Winbond does not allow us to validate so always return success.
	 */
	write_efir_1(sc, baseport, 0x87);
	write_efir_1(sc, baseport, 0x87);

	return (0);
}

static void
ext_cfg_exit_0xaa(struct wb_softc *sc, u_short baseport)
{

	write_efir_1(sc, baseport, 0xaa);
}

/*
 * (Re)load the watchdog counter depending on timeout.  A timeout of 0 will
 * disable the watchdog.
 */
static int
wb_set_watchdog(struct wb_softc *sc, unsigned int timeout)
{

	if (timeout != 0) {
		/*
		 * In case an override is set, let it override.  It may lead
		 * to strange results as we do not check the input of the sysctl.
		 */
		if (sc->timeout_override > 0)
			timeout = sc->timeout_override;

		/* Make sure we support the requested timeout. */
		if (timeout > 255 * 60)
			return (EINVAL);
	}

	if (sc->debug_verbose)
		wb_print_state(sc, "Before watchdog counter (re)load");

	if ((*sc->ext_cfg_enter_f)(sc, 0) != 0)
		return (ENXIO);

	/* Watchdog is configured as part of LDN 8 (GPIO Port2, Watchdog) */
	write_reg(sc, WB_LDN_REG, WB_LDN_REG_LDN8);

	if (timeout == 0) {
		/* Disable watchdog. */
		sc->reg_timeout = 0;
		write_reg(sc, sc->time_reg, sc->reg_timeout);

	} else {
		/* Read current scaling factor. */
		sc->reg_1 = read_reg(sc, sc->ctl_reg);

		if (timeout > 255) {
			/* Set scaling factor to 60s. */
			sc->reg_1 |= WB_LDN8_CRF5_SCALE;
			sc->reg_timeout = (timeout / 60);
			if (timeout % 60)
				sc->reg_timeout++;
		} else {
			/* Set scaling factor to 1s. */
			sc->reg_1 &= ~WB_LDN8_CRF5_SCALE;
			sc->reg_timeout = timeout;
		}

		/* In case we fired before we need to clear to fire again. */
		sc->reg_2 = read_reg(sc, sc->csr_reg);
		if (sc->reg_2 & WB_LDN8_CRF7_TS) {
			sc->reg_2 &= ~WB_LDN8_CRF7_TS;
			write_reg(sc, sc->csr_reg, sc->reg_2);
		}

		/* Write back scaling factor. */
		write_reg(sc, sc->ctl_reg, sc->reg_1);

		/* Set timer and arm/reset the watchdog. */
		write_reg(sc, sc->time_reg, sc->reg_timeout);
	}

	(*sc->ext_cfg_exit_f)(sc, 0);

	if (sc->debug_verbose)
		wb_print_state(sc, "After watchdog counter (re)load");
	return (0);
}

/*
 * watchdog(9) EVENTHANDLER function implementation to (re)load the counter
 * with the given timeout or disable the watchdog.
 */
static void
wb_watchdog_fn(void *private, u_int cmd, int *error)
{
	struct wb_softc *sc;
	unsigned int timeout;
	int e;

	sc = private;
	KASSERT(sc != NULL, ("%s: watchdog handler function called without "
	    "softc.", __func__));

	cmd &= WD_INTERVAL;
	if (cmd > 0 && cmd <= 63) {
		/* Reset (and arm) watchdog. */
		timeout = ((uint64_t)1 << cmd) / 1000000000;
		if (timeout == 0)
			timeout = 1;
		e = wb_set_watchdog(sc, timeout);
		if (e == 0) {
			if (error != NULL)
				*error = 0;
		} else {
			/* On error, try to make sure the WD is disabled. */
			wb_set_watchdog(sc, 0);
		}

	} else {
		/* Disable watchdog. */
		e = wb_set_watchdog(sc, 0);
		if (e != 0 && cmd == 0 && error != NULL) {
			/* Failed to disable watchdog. */
			*error = EOPNOTSUPP;
		}
	}
}

/*
 * Probe/attach the Winbond Super I/O chip.
 *
 * Initial abstraction to possibly support more chips:
 * - Iterate over the well known base ports, try to enable extended function
 *   mode and read and match the device ID and device revision.  Unfortunately
 *   the Vendor ID is in the hardware monitoring section accessible by different
 *   base ports only.
 * - Also HEFRAS, which would tell use the base port, is only accessible after
 *   entering extended function mode, for which the base port is needed.
 *   At least check HEFRAS to match the current base port we are probing.
 * - On match set the description, remember functions to enter/exit extended
 *   function mode as well as the base port.
 */
static int
wb_probe_enable(device_t dev, int probe)
{
	struct wb_softc *sc;
	int error, found, i, j;
	uint8_t dev_id, dev_rev, cr26;
	char buf[128];

	if (dev == NULL)
		sc = NULL;
	else {
		sc = device_get_softc(dev);
		bzero(sc, sizeof(*sc));
		sc->dev = dev;
	}

	error = ENXIO;
	found = 0;
	for (i = 0; i < nitems(probe_addrs); i++) {

		if (sc != NULL) {
			/* Allocate bus resources for IO index/data register access. */
			sc->portres = bus_alloc_resource(dev, SYS_RES_IOPORT, &sc->rid,
			    probe_addrs[i].efer, probe_addrs[i].efer + 1, 2, RF_ACTIVE);
			if (sc->portres == NULL)
				continue;
			sc->bst = rman_get_bustag(sc->portres);
			sc->bsh = rman_get_bushandle(sc->portres);
		}

		error = (*probe_addrs[i].ext_cfg_enter_f)(sc, probe_addrs[i].efer);
		if (error != 0)
			goto cleanup;

		/* Identify the SuperIO chip. */
		write_efir_1(sc, probe_addrs[i].efer, WB_DEVICE_ID_REG);
		dev_id = read_efdr_1(sc, probe_addrs[i].efer);
		write_efir_1(sc, probe_addrs[i].efer, WB_DEVICE_REV_REG);
		dev_rev = read_efdr_1(sc, probe_addrs[i].efer);
		write_efir_1(sc, probe_addrs[i].efer, WB_CR26);
		cr26 = read_efdr_1(sc, probe_addrs[i].efer);

		if (dev_id == 0xff && dev_rev == 0xff)
			goto cleanup;

		/* HEFRAS of 0 means EFER at 0x2e, 1 means EFER at 0x4e. */
		if (((cr26 & 0x40) == 0x00 && probe_addrs[i].efer != 0x2e) ||
		    ((cr26 & 0x40) == 0x40 && probe_addrs[i].efer != 0x4e)) {
			if (dev != NULL)
				device_printf(dev, "HEFRAS and EFER do not "
				    "align: EFER 0x%02x DevID 0x%02x DevRev "
				    "0x%02x CR26 0x%02x\n",
				    probe_addrs[i].efer, dev_id, dev_rev, cr26);
			goto cleanup;
		}

		for (j = 0; j < nitems(wb_devs); j++) {
			if (wb_devs[j].device_id == dev_id) {
				found = 1;
				break;
			}
		}

		if (probe && dev != NULL) {
			snprintf(buf, sizeof(buf),
			    "%s (0x%02x/0x%02x) Watchdog Timer",
			    found ? wb_devs[j].descr :
			     "Unknown Winbond/Nuvoton", dev_id, dev_rev);
			device_set_desc_copy(dev, buf);
		}

		/* If this is hinted attach, try to guess the model. */
		if (dev != NULL && !found) {
			found = 1;
			j = 0;
		}

cleanup:
		if (probe || !found) {
			(*probe_addrs[i].ext_cfg_exit_f)(sc, probe_addrs[i].efer);
			if (sc != NULL)
				(void) bus_release_resource(dev, SYS_RES_IOPORT,
				    sc->rid, sc->portres);
		}

		/*
		 * Stop probing if have successfully identified the SuperIO.
		 * Remember the extended function mode enter/exit functions
		 * for operations.
		 */
		if (found) {
			if (sc != NULL) {
				sc->ext_cfg_enter_f = probe_addrs[i].ext_cfg_enter_f;
				sc->ext_cfg_exit_f = probe_addrs[i].ext_cfg_exit_f;
				sc->chip = wb_devs[j].chip;
				sc->ctl_reg = 0xf5;
				sc->time_reg = 0xf6;
				sc->csr_reg = 0xf7;
				if (sc->chip == w83697hf ||
				    sc->chip == w83697ug) {
					sc->ctl_reg = 0xf3;
					sc->time_reg = 0xf4;
				} else if (sc->chip == nct6102) {
					sc->ctl_reg = 0xf0;
					sc->time_reg = 0xf1;
					sc->csr_reg = 0xf2;
				}
			}
			return (BUS_PROBE_SPECIFIC);
		} else
			error = ENXIO;
	}

	return (error);
}

static void
wb_identify(driver_t *driver, device_t parent)
{

	if (device_find_child(parent, driver->name, 0) == NULL) {
		if (wb_probe_enable(NULL, 1) <= 0)
			BUS_ADD_CHILD(parent, 0, driver->name, 0);
	}
}

static int
wb_probe(device_t dev)
{

	/* Make sure we do not claim some ISA PNP device. */
	if (isa_get_logicalid(dev) != 0)
		return (ENXIO);

	return (wb_probe_enable(dev, 1));
}

static int
wb_attach(device_t dev)
{
	struct wb_softc *sc;
	struct sysctl_ctx_list *sctx;
	struct sysctl_oid *soid;
	unsigned long timeout;
	int error;
	uint8_t t;

	error = wb_probe_enable(dev, 0);
	if (error > 0)
		return (ENXIO);

	sc = device_get_softc(dev);
	KASSERT(sc->ext_cfg_enter_f != NULL && sc->ext_cfg_exit_f != NULL,
	    ("%s: successful probe result but not setup correctly", __func__));

	/* Watchdog is configured as part of LDN 8 (GPIO Port2, Watchdog). */
	write_reg(sc, WB_LDN_REG, WB_LDN_REG_LDN8);

	/* Make sure WDT is enabled. */
	write_reg(sc, WB_LDN8_CR30,
	    read_reg(sc, WB_LDN8_CR30) | WB_LDN8_CR30_ACTIVE);

	switch (sc->chip) {
	case w83627hf:
	case w83627s:
		t = read_reg(sc, 0x2B) & ~0x10;
		write_reg(sc, 0x2B, t); /* set GPIO24 to WDT0 */
		break;
	case w83697hf:
		/* Set pin 119 to WDTO# mode (= CR29, WDT0) */
		t = read_reg(sc, 0x29) & ~0x60;
		t |= 0x20;
		write_reg(sc, 0x29, t);
		break;
	case w83697ug:
		/* Set pin 118 to WDTO# mode */
		t = read_reg(sc, 0x2b) & ~0x04;
		write_reg(sc, 0x2b, t);
		break;
	case w83627thf:
		t = (read_reg(sc, 0x2B) & ~0x08) | 0x04;
		write_reg(sc, 0x2B, t); /* set GPIO3 to WDT0 */
		break;
	case w83627dhg:
	case w83627dhg_p:
		t = read_reg(sc, 0x2D) & ~0x01; /* PIN77 -> WDT0# */
		write_reg(sc, 0x2D, t); /* set GPIO5 to WDT0 */
		t = read_reg(sc, sc->ctl_reg);
		t |= 0x02;	/* enable the WDTO# output low pulse
				 * to the KBRST# pin */
		write_reg(sc, sc->ctl_reg, t);
		break;
	case w83637hf:
		break;
	case w83687thf:
		t = read_reg(sc, 0x2C) & ~0x80; /* PIN47 -> WDT0# */
		write_reg(sc, 0x2C, t);
		break;
	case w83627ehf:
	case w83627uhg:
	case w83667hg:
	case w83667hg_b:
	case nct6775:
	case nct6776:
	case nct6779:
	case nct6791:
	case nct6792:
	case nct6793:
	case nct6795:
	case nct6102:
		/*
		 * These chips have a fixed WDTO# output pin (W83627UHG),
		 * or support more than one WDTO# output pin.
		 * Don't touch its configuration, and hope the BIOS
		 * does the right thing.
		 */
		t = read_reg(sc, sc->ctl_reg);
		t |= 0x02;	/* enable the WDTO# output low pulse
				 * to the KBRST# pin */
		write_reg(sc, sc->ctl_reg, t);
		break;
	default:
		break;
	}

	/* Read the current watchdog configuration. */
	sc->reg_1 = read_reg(sc, sc->ctl_reg);
	sc->reg_timeout = read_reg(sc, sc->time_reg);
	sc->reg_2 = read_reg(sc, sc->csr_reg);

	/* Print current state if bootverbose or watchdog already enabled. */
	if (bootverbose || (sc->reg_timeout > 0x00))
		wb_print_state(sc, "Before watchdog attach");

	sc->reg_1 &= ~WB_LDN8_CRF5_KEYB_P20;
	sc->reg_1 |= WB_LDN8_CRF5_KBRST;
	write_reg(sc, sc->ctl_reg, sc->reg_1);

	/*
	 * Clear a previous watchdog timeout event (if still set).
	 * Disable timer reset on mouse interrupts.  Leave reset on keyboard,
	 * since one of my boards is getting stuck in reboot without it.
	 */
	sc->reg_2 &= ~(WB_LDN8_CRF7_MOUSE|WB_LDN8_CRF7_TS);
	write_reg(sc, sc->csr_reg, sc->reg_2);

	(*sc->ext_cfg_exit_f)(sc, 0);

	/* Read global timeout override tunable, Add per device sysctls. */
	if (TUNABLE_ULONG_FETCH("hw.wbwd.timeout_override", &timeout)) {
		if (timeout > 0)
			sc->timeout_override = timeout;
	}
	sctx = device_get_sysctl_ctx(dev);
	soid = device_get_sysctl_tree(dev);
        SYSCTL_ADD_UINT(sctx, SYSCTL_CHILDREN(soid), OID_AUTO,
	    "timeout_override", CTLFLAG_RW, &sc->timeout_override, 0,
            "Timeout in seconds overriding default watchdog timeout");
        SYSCTL_ADD_INT(sctx, SYSCTL_CHILDREN(soid), OID_AUTO,
	    "debug_verbose", CTLFLAG_RW, &sc->debug_verbose, 0,
            "Enables extra debugging information");
        SYSCTL_ADD_PROC(sctx, SYSCTL_CHILDREN(soid), OID_AUTO, "debug",
	    CTLTYPE_STRING|CTLFLAG_RD, sc, 0, sysctl_wb_debug, "A",
            "Selected register information from last change by driver");
        SYSCTL_ADD_PROC(sctx, SYSCTL_CHILDREN(soid), OID_AUTO, "debug_current",
	    CTLTYPE_STRING|CTLFLAG_RD|CTLFLAG_SKIP, sc, 0,
	     sysctl_wb_debug_current, "A",
	     "Selected register information (may interfere)");
	SYSCTL_ADD_PROC(sctx, SYSCTL_CHILDREN(soid), OID_AUTO, "force_timeout",
	    CTLTYPE_INT|CTLFLAG_RW|CTLFLAG_SKIP, sc, 0,
	    sysctl_wb_force_test_nmi, "I", "Enable to force watchdog to fire.");

	/* Register watchdog. */
	sc->ev_tag = EVENTHANDLER_REGISTER(watchdog_list, wb_watchdog_fn, sc,
	    0);

	if (bootverbose)
		wb_print_state(sc, "After watchdog attach");

	return (0);
}

static int
wb_detach(device_t dev)
{
	struct wb_softc *sc;

	sc = device_get_softc(dev);

	/* Unregister and stop the watchdog if running. */
	if (sc->ev_tag)
		EVENTHANDLER_DEREGISTER(watchdog_list, sc->ev_tag);
	wb_set_watchdog(sc, 0);

	/* Disable extended function mode. */
	(*sc->ext_cfg_exit_f)(sc, 0);

	/* Cleanup resources. */
	(void) bus_release_resource(dev, SYS_RES_IOPORT, sc->rid, sc->portres);

	/* Bus subroutines take care of sysctls already. */

	return (0);
}

static device_method_t wb_methods[] = {
	/* Device interface */
	DEVMETHOD(device_identify,	wb_identify),
	DEVMETHOD(device_probe,		wb_probe),
	DEVMETHOD(device_attach,	wb_attach),
	DEVMETHOD(device_detach,	wb_detach),

	DEVMETHOD_END
};

static driver_t wb_isa_driver = {
	"wbwd",
	wb_methods,
	sizeof(struct wb_softc)
};

static devclass_t wb_devclass;

DRIVER_MODULE(wb, isa, wb_isa_driver, wb_devclass, NULL, NULL);
