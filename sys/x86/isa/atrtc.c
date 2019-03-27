/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008 Poul-Henning Kamp
 * Copyright (c) 2010 Alexander Motin <mav@FreeBSD.org>
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
 *
 * $FreeBSD$
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_acpi.h"
#include "opt_isa.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/clock.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/kdb.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/proc.h>
#include <sys/rman.h>
#include <sys/timeet.h>

#include <isa/rtc.h>
#ifdef DEV_ISA
#include <isa/isareg.h>
#include <isa/isavar.h>
#endif
#include <machine/intr_machdep.h>
#include "clock_if.h"
#ifdef DEV_ACPI
#include <contrib/dev/acpica/include/acpi.h>
#include <contrib/dev/acpica/include/accommon.h>
#include <dev/acpica/acpivar.h>
#include <machine/md_var.h>
#endif

/*
 * atrtc_lock protects low-level access to individual hardware registers.
 * atrtc_time_lock protects the entire sequence of accessing multiple registers
 * to read or write the date and time.
 */
static struct mtx atrtc_lock;
MTX_SYSINIT(atrtc_lock_init, &atrtc_lock, "atrtc", MTX_SPIN);

/* Force RTC enabled/disabled. */
static int atrtc_enabled = -1;
TUNABLE_INT("hw.atrtc.enabled", &atrtc_enabled);

struct mtx atrtc_time_lock;
MTX_SYSINIT(atrtc_time_lock_init, &atrtc_time_lock, "atrtc_time", MTX_DEF);

int	atrtcclock_disable = 0;

static	int	rtc_reg = -1;
static	u_char	rtc_statusa = RTCSA_DIVIDER | RTCSA_NOPROF;
static	u_char	rtc_statusb = RTCSB_24HR;

#ifdef DEV_ACPI
#define	_COMPONENT	ACPI_TIMER
ACPI_MODULE_NAME("ATRTC")
#endif

/*
 * RTC support routines
 */

static inline u_char
rtcin_locked(int reg)
{

	if (rtc_reg != reg) {
		inb(0x84);
		outb(IO_RTC, reg);
		rtc_reg = reg;
		inb(0x84);
	}
	return (inb(IO_RTC + 1));
}

static inline void
rtcout_locked(int reg, u_char val)
{

	if (rtc_reg != reg) {
		inb(0x84);
		outb(IO_RTC, reg);
		rtc_reg = reg;
		inb(0x84);
	}
	outb(IO_RTC + 1, val);
	inb(0x84);
}

int
rtcin(int reg)
{
	u_char val;

	mtx_lock_spin(&atrtc_lock);
	val = rtcin_locked(reg);
	mtx_unlock_spin(&atrtc_lock);
	return (val);
}

void
writertc(int reg, u_char val)
{

	mtx_lock_spin(&atrtc_lock);
	rtcout_locked(reg, val);
	mtx_unlock_spin(&atrtc_lock);
}

static void
atrtc_start(void)
{

	mtx_lock_spin(&atrtc_lock);
	rtcout_locked(RTC_STATUSA, rtc_statusa);
	rtcout_locked(RTC_STATUSB, RTCSB_24HR);
	mtx_unlock_spin(&atrtc_lock);
}

static void
atrtc_rate(unsigned rate)
{

	rtc_statusa = RTCSA_DIVIDER | rate;
	writertc(RTC_STATUSA, rtc_statusa);
}

static void
atrtc_enable_intr(void)
{

	rtc_statusb |= RTCSB_PINTR;
	mtx_lock_spin(&atrtc_lock);
	rtcout_locked(RTC_STATUSB, rtc_statusb);
	rtcin_locked(RTC_INTR);
	mtx_unlock_spin(&atrtc_lock);
}

static void
atrtc_disable_intr(void)
{

	rtc_statusb &= ~RTCSB_PINTR;
	mtx_lock_spin(&atrtc_lock);
	rtcout_locked(RTC_STATUSB, rtc_statusb);
	rtcin_locked(RTC_INTR);
	mtx_unlock_spin(&atrtc_lock);
}

void
atrtc_restore(void)
{

	/* Restore all of the RTC's "status" (actually, control) registers. */
	mtx_lock_spin(&atrtc_lock);
	rtcin_locked(RTC_STATUSA);	/* dummy to get rtc_reg set */
	rtcout_locked(RTC_STATUSB, RTCSB_24HR);
	rtcout_locked(RTC_STATUSA, rtc_statusa);
	rtcout_locked(RTC_STATUSB, rtc_statusb);
	rtcin_locked(RTC_INTR);
	mtx_unlock_spin(&atrtc_lock);
}

/**********************************************************************
 * RTC driver for subr_rtc
 */

struct atrtc_softc {
	int port_rid, intr_rid;
	struct resource *port_res;
	struct resource *intr_res;
	void *intr_handler;
	struct eventtimer et;
#ifdef DEV_ACPI
	ACPI_HANDLE acpi_handle;
#endif
};

static int
rtc_start(struct eventtimer *et, sbintime_t first, sbintime_t period)
{

	atrtc_rate(max(fls(period + (period >> 1)) - 17, 1));
	atrtc_enable_intr();
	return (0);
}

static int
rtc_stop(struct eventtimer *et)
{

	atrtc_disable_intr();
	return (0);
}

/*
 * This routine receives statistical clock interrupts from the RTC.
 * As explained above, these occur at 128 interrupts per second.
 * When profiling, we receive interrupts at a rate of 1024 Hz.
 *
 * This does not actually add as much overhead as it sounds, because
 * when the statistical clock is active, the hardclock driver no longer
 * needs to keep (inaccurate) statistics on its own.  This decouples
 * statistics gathering from scheduling interrupts.
 *
 * The RTC chip requires that we read status register C (RTC_INTR)
 * to acknowledge an interrupt, before it will generate the next one.
 * Under high interrupt load, rtcintr() can be indefinitely delayed and
 * the clock can tick immediately after the read from RTC_INTR.  In this
 * case, the mc146818A interrupt signal will not drop for long enough
 * to register with the 8259 PIC.  If an interrupt is missed, the stat
 * clock will halt, considerably degrading system performance.  This is
 * why we use 'while' rather than a more straightforward 'if' below.
 * Stat clock ticks can still be lost, causing minor loss of accuracy
 * in the statistics, but the stat clock will no longer stop.
 */
static int
rtc_intr(void *arg)
{
	struct atrtc_softc *sc = (struct atrtc_softc *)arg;
	int flag = 0;

	while (rtcin(RTC_INTR) & RTCIR_PERIOD) {
		flag = 1;
		if (sc->et.et_active)
			sc->et.et_event_cb(&sc->et, sc->et.et_arg);
	}
	return(flag ? FILTER_HANDLED : FILTER_STRAY);
}

#ifdef DEV_ACPI
/*
 *  ACPI RTC CMOS address space handler
 */
#define	ATRTC_LAST_REG	0x40

static void
rtcin_region(int reg, void *buf, int len)
{
	u_char *ptr = buf;

	/* Drop lock after each IO as intr and settime have greater priority */
	while (len-- > 0)
		*ptr++ = rtcin(reg++) & 0xff;
}

static void
rtcout_region(int reg, const void *buf, int len)
{
	const u_char *ptr = buf;

	while (len-- > 0)
		writertc(reg++, *ptr++);
}

static bool
atrtc_check_cmos_access(bool is_read, ACPI_PHYSICAL_ADDRESS addr, UINT32 len)
{

	/* Block address space wrapping on out-of-bound access */
	if (addr >= ATRTC_LAST_REG || addr + len > ATRTC_LAST_REG)
		return (false);

	if (is_read) {
		/* Reading 0x0C will muck with interrupts */
		if (addr <= RTC_INTR && addr + len > RTC_INTR)
			return (false);
	} else {
		/*
		 * Allow single-byte writes to alarm registers and
		 * multi-byte writes to addr >= 0x30, else deny.
		 */
		if (!((len == 1 && (addr == RTC_SECALRM ||
				    addr == RTC_MINALRM ||
				    addr == RTC_HRSALRM)) ||
		      addr >= 0x30))
			return (false);
	}
	return (true);
}

static ACPI_STATUS
atrtc_acpi_cmos_handler(UINT32 func, ACPI_PHYSICAL_ADDRESS addr,
    UINT32 bitwidth, UINT64 *value, void *context, void *region_context)
{
	device_t dev = context;
	UINT32 bytewidth = howmany(bitwidth, 8);
	bool is_read = func == ACPI_READ;

	/* ACPICA is very verbose on CMOS handler failures, so we, too */
#define	CMOS_HANDLER_ERR(fmt, ...) \
	device_printf(dev, "ACPI [SystemCMOS] handler: " fmt, ##__VA_ARGS__)

	ACPI_FUNCTION_TRACE((char *)(uintptr_t)__func__);

	if (value == NULL) {
		CMOS_HANDLER_ERR("NULL parameter\n");
		return (AE_BAD_PARAMETER);
	}
	if (bitwidth == 0 || (bitwidth & 0x07) != 0) {
		CMOS_HANDLER_ERR("Invalid bitwidth: %u\n", bitwidth);
		return (AE_BAD_PARAMETER);
	}
	if (!atrtc_check_cmos_access(is_read, addr, bytewidth)) {
		CMOS_HANDLER_ERR("%s access rejected: addr=%#04jx, len=%u\n",
		    is_read ? "Read" : "Write", (uintmax_t)addr, bytewidth);
		return (AE_BAD_PARAMETER);
	}

	switch (func) {
	case ACPI_READ:
		rtcin_region(addr, value, bytewidth);
		break;
	case ACPI_WRITE:
		rtcout_region(addr, value, bytewidth);
		break;
	default:
		CMOS_HANDLER_ERR("Invalid function: %u\n", func);
		return (AE_BAD_PARAMETER);
	}

	ACPI_VPRINT(dev, acpi_device_get_parent_softc(dev),
	    "ACPI RTC CMOS %s access: addr=%#04x, len=%u, val=%*D\n",
	    is_read ? "read" : "write", (unsigned)addr, bytewidth,
	    bytewidth, value, " ");

	return (AE_OK);
}

static int
atrtc_reg_acpi_cmos_handler(device_t dev)
{
	struct atrtc_softc *sc = device_get_softc(dev);

	ACPI_FUNCTION_TRACE((char *)(uintptr_t) __func__);

	/* Don't handle address space events if driver is disabled. */
	if (acpi_disabled("atrtc"))
		return (ENXIO);

	sc->acpi_handle = acpi_get_handle(dev);
	if (sc->acpi_handle == NULL ||
	    ACPI_FAILURE(AcpiInstallAddressSpaceHandler(sc->acpi_handle,
	      ACPI_ADR_SPACE_CMOS, atrtc_acpi_cmos_handler, NULL, dev))) {
		sc->acpi_handle = NULL;
		device_printf(dev,
		    "Can't register ACPI CMOS address space handler\n");
		return (ENXIO);
        }

        return (0);
}

static int
atrtc_unreg_acpi_cmos_handler(device_t dev)
{
	struct atrtc_softc *sc = device_get_softc(dev);

	ACPI_FUNCTION_TRACE((char *)(uintptr_t) __func__);

	if (sc->acpi_handle != NULL)
		AcpiRemoveAddressSpaceHandler(sc->acpi_handle,
		    ACPI_ADR_SPACE_CMOS, atrtc_acpi_cmos_handler);

	return (0);
}
#endif	/* DEV_ACPI */

/*
 * Attach to the ISA PnP descriptors for the timer and realtime clock.
 */
static struct isa_pnp_id atrtc_ids[] = {
	{ 0x000bd041 /* PNP0B00 */, "AT realtime clock" },
	{ 0 }
};

static bool
atrtc_acpi_disabled(void)
{
#ifdef DEV_ACPI
	uint16_t flags;

	if (!acpi_get_fadt_bootflags(&flags))
		return (false);
	return ((flags & ACPI_FADT_NO_CMOS_RTC) != 0);
#else
	return (false);
#endif
}

static int
atrtc_probe(device_t dev)
{
	int result;

	if ((atrtc_enabled == -1 && atrtc_acpi_disabled()) ||
	    (atrtc_enabled == 0))
		return (ENXIO);

	result = ISA_PNP_PROBE(device_get_parent(dev), dev, atrtc_ids);
	/* ENOENT means no PnP-ID, device is hinted. */
	if (result == ENOENT) {
		device_set_desc(dev, "AT realtime clock");
		return (BUS_PROBE_LOW_PRIORITY);
	}
	return (result);
}

static int
atrtc_attach(device_t dev)
{
	struct atrtc_softc *sc;
	rman_res_t s;
	int i;

	sc = device_get_softc(dev);
	sc->port_res = bus_alloc_resource(dev, SYS_RES_IOPORT, &sc->port_rid,
	    IO_RTC, IO_RTC + 1, 2, RF_ACTIVE);
	if (sc->port_res == NULL)
		device_printf(dev, "Warning: Couldn't map I/O.\n");
	atrtc_start();
	clock_register(dev, 1000000);
	bzero(&sc->et, sizeof(struct eventtimer));
	if (!atrtcclock_disable &&
	    (resource_int_value(device_get_name(dev), device_get_unit(dev),
	     "clock", &i) != 0 || i != 0)) {
		sc->intr_rid = 0;
		while (bus_get_resource(dev, SYS_RES_IRQ, sc->intr_rid,
		    &s, NULL) == 0 && s != 8)
			sc->intr_rid++;
		sc->intr_res = bus_alloc_resource(dev, SYS_RES_IRQ,
		    &sc->intr_rid, 8, 8, 1, RF_ACTIVE);
		if (sc->intr_res == NULL) {
			device_printf(dev, "Can't map interrupt.\n");
			return (0);
		} else if ((bus_setup_intr(dev, sc->intr_res, INTR_TYPE_CLK,
		    rtc_intr, NULL, sc, &sc->intr_handler))) {
			device_printf(dev, "Can't setup interrupt.\n");
			return (0);
		} else { 
			/* Bind IRQ to BSP to avoid live migration. */
			bus_bind_intr(dev, sc->intr_res, 0);
		}
		sc->et.et_name = "RTC";
		sc->et.et_flags = ET_FLAGS_PERIODIC | ET_FLAGS_POW2DIV;
		sc->et.et_quality = 0;
		sc->et.et_frequency = 32768;
		sc->et.et_min_period = 0x00080000;
		sc->et.et_max_period = 0x80000000;
		sc->et.et_start = rtc_start;
		sc->et.et_stop = rtc_stop;
		sc->et.et_priv = dev;
		et_register(&sc->et);
	}
	return(0);
}

static int
atrtc_isa_attach(device_t dev)
{

	return (atrtc_attach(dev));
}

#ifdef DEV_ACPI
static int
atrtc_acpi_attach(device_t dev)
{
	int ret;

	ret = atrtc_attach(dev);
	if (ret)
		return (ret);

	(void)atrtc_reg_acpi_cmos_handler(dev);

	return (0);
}

static int
atrtc_acpi_detach(device_t dev)
{

	(void)atrtc_unreg_acpi_cmos_handler(dev);
	return (0);
}
#endif	/* DEV_ACPI */

static int
atrtc_resume(device_t dev)
{

	atrtc_restore();
	return(0);
}

static int
atrtc_settime(device_t dev __unused, struct timespec *ts)
{
	struct bcd_clocktime bct;

	clock_ts_to_bcd(ts, &bct, false);
	clock_dbgprint_bcd(dev, CLOCK_DBG_WRITE, &bct);

	mtx_lock(&atrtc_time_lock);
	mtx_lock_spin(&atrtc_lock);

	/* Disable RTC updates and interrupts.  */
	rtcout_locked(RTC_STATUSB, RTCSB_HALT | RTCSB_24HR);

	/* Write all the time registers. */
	rtcout_locked(RTC_SEC,   bct.sec);
	rtcout_locked(RTC_MIN,   bct.min);
	rtcout_locked(RTC_HRS,   bct.hour);
	rtcout_locked(RTC_WDAY,  bct.dow + 1);
	rtcout_locked(RTC_DAY,   bct.day);
	rtcout_locked(RTC_MONTH, bct.mon);
	rtcout_locked(RTC_YEAR,  bct.year & 0xff);
#ifdef USE_RTC_CENTURY
	rtcout_locked(RTC_CENTURY, bct.year >> 8);
#endif

	/*
	 * Re-enable RTC updates and interrupts.
	 */
	rtcout_locked(RTC_STATUSB, rtc_statusb);
	rtcin_locked(RTC_INTR);

	mtx_unlock_spin(&atrtc_lock);
	mtx_unlock(&atrtc_time_lock);

	return (0);
}

static int
atrtc_gettime(device_t dev, struct timespec *ts)
{
	struct bcd_clocktime bct;

	/* Look if we have a RTC present and the time is valid */
	if (!(rtcin(RTC_STATUSD) & RTCSD_PWR)) {
		device_printf(dev, "WARNING: Battery failure indication\n");
		return (EINVAL);
	}

	/*
	 * wait for time update to complete
	 * If RTCSA_TUP is zero, we have at least 244us before next update.
	 * This is fast enough on most hardware, but a refinement would be
	 * to make sure that no more than 240us pass after we start reading,
	 * and try again if so.
	 */
	mtx_lock(&atrtc_time_lock);
	while (rtcin(RTC_STATUSA) & RTCSA_TUP)
		continue;
	mtx_lock_spin(&atrtc_lock);
	bct.sec  = rtcin_locked(RTC_SEC);
	bct.min  = rtcin_locked(RTC_MIN);
	bct.hour = rtcin_locked(RTC_HRS);
	bct.day  = rtcin_locked(RTC_DAY);
	bct.mon  = rtcin_locked(RTC_MONTH);
	bct.year = rtcin_locked(RTC_YEAR);
#ifdef USE_RTC_CENTURY
	bct.year |= rtcin_locked(RTC_CENTURY) << 8;
#endif
	mtx_unlock_spin(&atrtc_lock);
	mtx_unlock(&atrtc_time_lock);
	/* dow is unused in timespec conversion and we have no nsec info. */
	bct.dow  = 0;
	bct.nsec = 0;
	clock_dbgprint_bcd(dev, CLOCK_DBG_READ, &bct);
	return (clock_bcd_to_ts(&bct, ts, false));
}

static device_method_t atrtc_isa_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		atrtc_probe),
	DEVMETHOD(device_attach,	atrtc_isa_attach),
	DEVMETHOD(device_detach,	bus_generic_detach),
	DEVMETHOD(device_shutdown,	bus_generic_shutdown),
	DEVMETHOD(device_suspend,	bus_generic_suspend),
		/* XXX stop statclock? */
	DEVMETHOD(device_resume,	atrtc_resume),

	/* clock interface */
	DEVMETHOD(clock_gettime,	atrtc_gettime),
	DEVMETHOD(clock_settime,	atrtc_settime),

	{ 0, 0 }
};

static driver_t atrtc_isa_driver = {
	"atrtc",
	atrtc_isa_methods,
	sizeof(struct atrtc_softc),
};

#ifdef DEV_ACPI
static device_method_t atrtc_acpi_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		atrtc_probe),
	DEVMETHOD(device_attach,	atrtc_acpi_attach),
	DEVMETHOD(device_detach,	atrtc_acpi_detach),
		/* XXX stop statclock? */
	DEVMETHOD(device_resume,	atrtc_resume),

	/* clock interface */
	DEVMETHOD(clock_gettime,	atrtc_gettime),
	DEVMETHOD(clock_settime,	atrtc_settime),

	{ 0, 0 }
};

static driver_t atrtc_acpi_driver = {
	"atrtc",
	atrtc_acpi_methods,
	sizeof(struct atrtc_softc),
};
#endif	/* DEV_ACPI */

static devclass_t atrtc_devclass;

DRIVER_MODULE(atrtc, isa, atrtc_isa_driver, atrtc_devclass, 0, 0);
#ifdef DEV_ACPI
DRIVER_MODULE(atrtc, acpi, atrtc_acpi_driver, atrtc_devclass, 0, 0);
#endif
ISA_PNP_INFO(atrtc_ids);
