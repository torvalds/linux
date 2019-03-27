/*-
 * Copyright (c) 2000, 2001 Michael Smith
 * Copyright (c) 2000 BSDi
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

#include "opt_acpi.h"
#include <sys/param.h>
#include <sys/bus.h>
#include <sys/eventhandler.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/sysctl.h>
#include <sys/timetc.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/rman.h>

#include <contrib/dev/acpica/include/acpi.h>
#include <contrib/dev/acpica/include/accommon.h>

#include <dev/acpica/acpivar.h>
#include <dev/pci/pcivar.h>

/*
 * A timecounter based on the free-running ACPI timer.
 *
 * Based on the i386-only mp_clock.c by <phk@FreeBSD.ORG>.
 */

/* Hooks for the ACPI CA debugging infrastructure */
#define _COMPONENT	ACPI_TIMER
ACPI_MODULE_NAME("TIMER")

static device_t			acpi_timer_dev;
static struct resource		*acpi_timer_reg;
static bus_space_handle_t	acpi_timer_bsh;
static bus_space_tag_t		acpi_timer_bst;
static eventhandler_tag		acpi_timer_eh;

static u_int	acpi_timer_frequency = 14318182 / 4;

/* Knob to disable acpi_timer device */
bool acpi_timer_disabled = false;

static void	acpi_timer_identify(driver_t *driver, device_t parent);
static int	acpi_timer_probe(device_t dev);
static int	acpi_timer_attach(device_t dev);
static void	acpi_timer_resume_handler(struct timecounter *);
static void	acpi_timer_suspend_handler(struct timecounter *);
static u_int	acpi_timer_get_timecount(struct timecounter *tc);
static u_int	acpi_timer_get_timecount_safe(struct timecounter *tc);
static int	acpi_timer_sysctl_freq(SYSCTL_HANDLER_ARGS);
static void	acpi_timer_boot_test(void);

static int	acpi_timer_test(void);

static device_method_t acpi_timer_methods[] = {
    DEVMETHOD(device_identify,	acpi_timer_identify),
    DEVMETHOD(device_probe,	acpi_timer_probe),
    DEVMETHOD(device_attach,	acpi_timer_attach),

    DEVMETHOD_END
};

static driver_t acpi_timer_driver = {
    "acpi_timer",
    acpi_timer_methods,
    0,
};

static devclass_t acpi_timer_devclass;
DRIVER_MODULE(acpi_timer, acpi, acpi_timer_driver, acpi_timer_devclass, 0, 0);
MODULE_DEPEND(acpi_timer, acpi, 1, 1, 1);

static struct timecounter acpi_timer_timecounter = {
	acpi_timer_get_timecount_safe,	/* get_timecount function */
	0,				/* no poll_pps */
	0,				/* no default counter_mask */
	0,				/* no default frequency */
	"ACPI",				/* name */
	-1				/* quality (chosen later) */
};

static __inline uint32_t
acpi_timer_read(void)
{

    return (bus_space_read_4(acpi_timer_bst, acpi_timer_bsh, 0));
}

/*
 * Locate the ACPI timer using the FADT, set up and allocate the I/O resources
 * we will be using.
 */
static void
acpi_timer_identify(driver_t *driver, device_t parent)
{
    device_t dev;
    rman_res_t rlen, rstart;
    int rid, rtype;

    ACPI_FUNCTION_TRACE((char *)(uintptr_t)__func__);

    if (acpi_disabled("timer") || (acpi_quirks & ACPI_Q_TIMER) ||
	acpi_timer_dev || acpi_timer_disabled ||
	AcpiGbl_FADT.PmTimerLength == 0)
	return_VOID;

    if ((dev = BUS_ADD_CHILD(parent, 2, "acpi_timer", 0)) == NULL) {
	device_printf(parent, "could not add acpi_timer0\n");
	return_VOID;
    }
    acpi_timer_dev = dev;

    switch (AcpiGbl_FADT.XPmTimerBlock.SpaceId) {
    case ACPI_ADR_SPACE_SYSTEM_MEMORY:
	rtype = SYS_RES_MEMORY;
	break;
    case ACPI_ADR_SPACE_SYSTEM_IO:
	rtype = SYS_RES_IOPORT;
	break;
    default:
	return_VOID;
    }
    rid = 0;
    rlen = AcpiGbl_FADT.PmTimerLength;
    rstart = AcpiGbl_FADT.XPmTimerBlock.Address;
    if (bus_set_resource(dev, rtype, rid, rstart, rlen))
	device_printf(dev, "couldn't set resource (%s 0x%jx+0x%jx)\n",
	    (rtype == SYS_RES_IOPORT) ? "port" : "mem", rstart, rlen);
    return_VOID;
}

static int
acpi_timer_probe(device_t dev)
{
    char desc[40];
    int i, j, rid, rtype;

    ACPI_FUNCTION_TRACE((char *)(uintptr_t)__func__);

    if (dev != acpi_timer_dev)
	return (ENXIO);

    switch (AcpiGbl_FADT.XPmTimerBlock.SpaceId) {
    case ACPI_ADR_SPACE_SYSTEM_MEMORY:
	rtype = SYS_RES_MEMORY;
	break;
    case ACPI_ADR_SPACE_SYSTEM_IO:
	rtype = SYS_RES_IOPORT;
	break;
    default:
	return (ENXIO);
    }
    rid = 0;
    acpi_timer_reg = bus_alloc_resource_any(dev, rtype, &rid, RF_ACTIVE);
    if (acpi_timer_reg == NULL) {
	device_printf(dev, "couldn't allocate resource (%s 0x%lx)\n",
	    (rtype == SYS_RES_IOPORT) ? "port" : "mem",
	    (u_long)AcpiGbl_FADT.XPmTimerBlock.Address);
	return (ENXIO);
    }
    acpi_timer_bsh = rman_get_bushandle(acpi_timer_reg);
    acpi_timer_bst = rman_get_bustag(acpi_timer_reg);
    if (AcpiGbl_FADT.Flags & ACPI_FADT_32BIT_TIMER)
	acpi_timer_timecounter.tc_counter_mask = 0xffffffff;
    else
	acpi_timer_timecounter.tc_counter_mask = 0x00ffffff;
    acpi_timer_timecounter.tc_frequency = acpi_timer_frequency;
    acpi_timer_timecounter.tc_flags = TC_FLAGS_SUSPEND_SAFE;
    if (testenv("debug.acpi.timer_test"))
	acpi_timer_boot_test();

    /*
     * If all tests of the counter succeed, use the ACPI-fast method.  If
     * at least one failed, default to using the safe routine, which reads
     * the timer multiple times to get a consistent value before returning.
     */
    j = 0;
    if (bootverbose)
	printf("ACPI timer:");
    for (i = 0; i < 10; i++)
	j += acpi_timer_test();
    if (bootverbose)
	printf(" -> %d\n", j);
    if (j == 10) {
	acpi_timer_timecounter.tc_name = "ACPI-fast";
	acpi_timer_timecounter.tc_get_timecount = acpi_timer_get_timecount;
	acpi_timer_timecounter.tc_quality = 900;
    } else {
	acpi_timer_timecounter.tc_name = "ACPI-safe";
	acpi_timer_timecounter.tc_get_timecount = acpi_timer_get_timecount_safe;
	acpi_timer_timecounter.tc_quality = 850;
    }
    tc_init(&acpi_timer_timecounter);

    sprintf(desc, "%d-bit timer at %u.%06uMHz",
	(AcpiGbl_FADT.Flags & ACPI_FADT_32BIT_TIMER) != 0 ? 32 : 24,
	acpi_timer_frequency / 1000000, acpi_timer_frequency % 1000000);
    device_set_desc_copy(dev, desc);

    /* Release the resource, we'll allocate it again during attach. */
    bus_release_resource(dev, rtype, rid, acpi_timer_reg);
    return (0);
}

static int
acpi_timer_attach(device_t dev)
{
    int rid, rtype;

    ACPI_FUNCTION_TRACE((char *)(uintptr_t)__func__);

    switch (AcpiGbl_FADT.XPmTimerBlock.SpaceId) {
    case ACPI_ADR_SPACE_SYSTEM_MEMORY:
	rtype = SYS_RES_MEMORY;
	break;
    case ACPI_ADR_SPACE_SYSTEM_IO:
	rtype = SYS_RES_IOPORT;
	break;
    default:
	return (ENXIO);
    }
    rid = 0;
    acpi_timer_reg = bus_alloc_resource_any(dev, rtype, &rid, RF_ACTIVE);
    if (acpi_timer_reg == NULL)
	return (ENXIO);
    acpi_timer_bsh = rman_get_bushandle(acpi_timer_reg);
    acpi_timer_bst = rman_get_bustag(acpi_timer_reg);

    /* Register suspend event handler. */
    if (EVENTHANDLER_REGISTER(power_suspend, acpi_timer_suspend_handler,
	&acpi_timer_timecounter, EVENTHANDLER_PRI_LAST) == NULL)
	device_printf(dev, "failed to register suspend event handler\n");

    return (0);
}

static void
acpi_timer_resume_handler(struct timecounter *newtc)
{
	struct timecounter *tc;

	tc = timecounter;
	if (tc != newtc) {
		if (bootverbose)
			device_printf(acpi_timer_dev,
			    "restoring timecounter, %s -> %s\n",
			    tc->tc_name, newtc->tc_name);
		(void)newtc->tc_get_timecount(newtc);
		(void)newtc->tc_get_timecount(newtc);
		timecounter = newtc;
	}
}

static void
acpi_timer_suspend_handler(struct timecounter *newtc)
{
	struct timecounter *tc;

	/* Deregister existing resume event handler. */
	if (acpi_timer_eh != NULL) {
		EVENTHANDLER_DEREGISTER(power_resume, acpi_timer_eh);
		acpi_timer_eh = NULL;
	}

	if ((timecounter->tc_flags & TC_FLAGS_SUSPEND_SAFE) != 0) {
		/*
		 * If we are using a suspend safe timecounter, don't
		 * save/restore it across suspend/resume.
		 */
		return;
	}

	KASSERT(newtc == &acpi_timer_timecounter,
	    ("acpi_timer_suspend_handler: wrong timecounter"));

	tc = timecounter;
	if (tc != newtc) {
		if (bootverbose)
			device_printf(acpi_timer_dev,
			    "switching timecounter, %s -> %s\n",
			    tc->tc_name, newtc->tc_name);
		(void)acpi_timer_read();
		(void)acpi_timer_read();
		timecounter = newtc;
		acpi_timer_eh = EVENTHANDLER_REGISTER(power_resume,
		    acpi_timer_resume_handler, tc, EVENTHANDLER_PRI_LAST);
	}
}

/*
 * Fetch current time value from reliable hardware.
 */
static u_int
acpi_timer_get_timecount(struct timecounter *tc)
{
    return (acpi_timer_read());
}

/*
 * Fetch current time value from hardware that may not correctly
 * latch the counter.  We need to read until we have three monotonic
 * samples and then use the middle one, otherwise we are not protected
 * against the fact that the bits can be wrong in two directions.  If
 * we only cared about monosity, two reads would be enough.
 */
static u_int
acpi_timer_get_timecount_safe(struct timecounter *tc)
{
    u_int u1, u2, u3;

    u2 = acpi_timer_read();
    u3 = acpi_timer_read();
    do {
	u1 = u2;
	u2 = u3;
	u3 = acpi_timer_read();
    } while (u1 > u2 || u2 > u3);

    return (u2);
}

/*
 * Timecounter freqency adjustment interface.
 */ 
static int
acpi_timer_sysctl_freq(SYSCTL_HANDLER_ARGS)
{
    int error;
    u_int freq;
 
    if (acpi_timer_timecounter.tc_frequency == 0)
	return (EOPNOTSUPP);
    freq = acpi_timer_frequency;
    error = sysctl_handle_int(oidp, &freq, 0, req);
    if (error == 0 && req->newptr != NULL) {
	acpi_timer_frequency = freq;
	acpi_timer_timecounter.tc_frequency = acpi_timer_frequency;
    }

    return (error);
}
 
SYSCTL_PROC(_machdep, OID_AUTO, acpi_timer_freq, CTLTYPE_INT | CTLFLAG_RW,
    0, sizeof(u_int), acpi_timer_sysctl_freq, "I", "ACPI timer frequency");

/*
 * Some ACPI timers are known or believed to suffer from implementation
 * problems which can lead to erroneous values being read.  This function
 * tests for consistent results from the timer and returns 1 if it believes
 * the timer is consistent, otherwise it returns 0.
 *
 * It appears the cause is that the counter is not latched to the PCI bus
 * clock when read:
 *
 * ] 20. ACPI Timer Errata
 * ]
 * ]   Problem: The power management timer may return improper result when
 * ]   read. Although the timer value settles properly after incrementing,
 * ]   while incrementing there is a 3nS window every 69.8nS where the
 * ]   timer value is indeterminate (a 4.2% chance that the data will be
 * ]   incorrect when read). As a result, the ACPI free running count up
 * ]   timer specification is violated due to erroneous reads.  Implication:
 * ]   System hangs due to the "inaccuracy" of the timer when used by
 * ]   software for time critical events and delays.
 * ]
 * ] Workaround: Read the register twice and compare.
 * ] Status: This will not be fixed in the PIIX4 or PIIX4E, it is fixed
 * ] in the PIIX4M.
 */
#define N 2000
static int
acpi_timer_test()
{
    uint32_t last, this;
    int delta, max, max2, min, n;
    register_t s;

    min = INT32_MAX;
    max = max2 = 0;

    /* Test the timer with interrupts disabled to get accurate results. */
    s = intr_disable();
    last = acpi_timer_read();
    for (n = 0; n < N; n++) {
	this = acpi_timer_read();
	delta = acpi_TimerDelta(this, last);
	if (delta > max) {
	    max2 = max;
	    max = delta;
	} else if (delta > max2)
	    max2 = delta;
	if (delta < min)
	    min = delta;
	last = this;
    }
    intr_restore(s);

    delta = max2 - min;
    if ((max - min > 8 || delta > 3) && vm_guest == VM_GUEST_NO)
	n = 0;
    else if (min < 0 || max == 0 || max2 == 0)
	n = 0;
    else
	n = 1;
    if (bootverbose)
	printf(" %d/%d", n, delta);

    return (n);
}
#undef N

/*
 * Test harness for verifying ACPI timer behaviour.
 * Boot with debug.acpi.timer_test set to invoke this.
 */
static void
acpi_timer_boot_test(void)
{
    uint32_t u1, u2, u3;

    u1 = acpi_timer_read();
    u2 = acpi_timer_read();
    u3 = acpi_timer_read();

    device_printf(acpi_timer_dev, "timer test in progress, reboot to quit.\n");
    for (;;) {
	/*
	 * The failure case is where u3 > u1, but u2 does not fall between
	 * the two, ie. it contains garbage.
	 */
	if (u3 > u1) {
	    if (u2 < u1 || u2 > u3)
		device_printf(acpi_timer_dev,
			      "timer is not monotonic: 0x%08x,0x%08x,0x%08x\n",
			      u1, u2, u3);
	}
	u1 = u2;
	u2 = u3;
	u3 = acpi_timer_read();
    }
}
