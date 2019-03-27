/*-
 * Copyright (c) 2000 Takanori Watanabe
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
#include <sys/kernel.h>
#include <sys/bus.h>

#include <machine/bus.h>
#include <sys/rman.h>
#include <sys/ioccom.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/conf.h>
#include <sys/power.h>

#include <contrib/dev/acpica/include/acpi.h>

#include <dev/acpica/acpivar.h>
#include <dev/acpica/acpiio.h>
#include <isa/isavar.h>
#include <isa/pnpvar.h>
 
/* Hooks for the ACPI CA debugging infrastructure */
#define _COMPONENT	ACPI_AC_ADAPTER
ACPI_MODULE_NAME("AC_ADAPTER")

/* Number of times to retry initialization before giving up. */
#define ACPI_ACAD_RETRY_MAX		6

#define ACPI_POWERSOURCE_STAT_CHANGE	0x80

struct	acpi_acad_softc {
    int status;
};

static void	acpi_acad_get_status(void *);
static void	acpi_acad_notify_handler(ACPI_HANDLE, UINT32, void *);
static int	acpi_acad_probe(device_t);
static int	acpi_acad_attach(device_t);
static int	acpi_acad_ioctl(u_long, caddr_t, void *);
static int	acpi_acad_sysctl(SYSCTL_HANDLER_ARGS);
static void	acpi_acad_init_acline(void *arg);
static void	acpi_acad_ac_only(void *arg);

static device_method_t acpi_acad_methods[] = {
    /* Device interface */
    DEVMETHOD(device_probe,	acpi_acad_probe),
    DEVMETHOD(device_attach,	acpi_acad_attach),

    DEVMETHOD_END
};

static driver_t acpi_acad_driver = {
    "acpi_acad",
    acpi_acad_methods,
    sizeof(struct acpi_acad_softc),
};

static devclass_t acpi_acad_devclass;
DRIVER_MODULE(acpi_acad, acpi, acpi_acad_driver, acpi_acad_devclass, 0, 0);
MODULE_DEPEND(acpi_acad, acpi, 1, 1, 1);

ACPI_SERIAL_DECL(acad, "ACPI AC adapter");

SYSINIT(acad, SI_SUB_KTHREAD_IDLE, SI_ORDER_FIRST, acpi_acad_ac_only, NULL);

static void
acpi_acad_get_status(void *context)
{
    struct acpi_acad_softc *sc;
    device_t	dev;
    ACPI_HANDLE	h;
    int		newstatus;

    dev = context;
    sc = device_get_softc(dev);
    h = acpi_get_handle(dev);
    newstatus = -1;
    acpi_GetInteger(h, "_PSR", &newstatus);

    /* If status is valid and has changed, notify the system. */
    ACPI_SERIAL_BEGIN(acad);
    if (newstatus != -1 && sc->status != newstatus) {
	sc->status = newstatus;
	ACPI_SERIAL_END(acad);
	power_profile_set_state(newstatus ? POWER_PROFILE_PERFORMANCE :
	    POWER_PROFILE_ECONOMY);
	ACPI_VPRINT(dev, acpi_device_get_parent_softc(dev),
	    "%s Line\n", newstatus ? "On" : "Off");
	acpi_UserNotify("ACAD", h, newstatus);
    } else
	ACPI_SERIAL_END(acad);
}

static void
acpi_acad_notify_handler(ACPI_HANDLE h, UINT32 notify, void *context)
{
    device_t dev;

    dev = (device_t)context;
    switch (notify) {
    case ACPI_NOTIFY_BUS_CHECK:
    case ACPI_NOTIFY_DEVICE_CHECK:
    case ACPI_POWERSOURCE_STAT_CHANGE:
	/* Temporarily.  It is better to notify policy manager */
	AcpiOsExecute(OSL_NOTIFY_HANDLER, acpi_acad_get_status, context);
	break;
    default:
	device_printf(dev, "unknown notify %#x\n", notify);
	break;
    }
}

static int
acpi_acad_probe(device_t dev)
{
    static char *acad_ids[] = { "ACPI0003", NULL };
    int rv;

    if (acpi_disabled("acad"))
	return (ENXIO);
    rv = ACPI_ID_PROBE(device_get_parent(dev), dev, acad_ids, NULL);
    if (rv <= 0)
	device_set_desc(dev, "AC Adapter");
    return (rv);
}

static int
acpi_acad_attach(device_t dev)
{
    struct acpi_acad_softc *sc;
    struct acpi_softc	   *acpi_sc;
    ACPI_HANDLE	handle;
    int		error;

    sc = device_get_softc(dev);
    handle = acpi_get_handle(dev);

    error = acpi_register_ioctl(ACPIIO_ACAD_GET_STATUS, acpi_acad_ioctl, dev);
    if (error != 0)
	return (error);

    if (device_get_unit(dev) == 0) {
	acpi_sc = acpi_device_get_parent_softc(dev);
	SYSCTL_ADD_PROC(&acpi_sc->acpi_sysctl_ctx,
			SYSCTL_CHILDREN(acpi_sc->acpi_sysctl_tree),
			OID_AUTO, "acline", CTLTYPE_INT | CTLFLAG_RD,
			&sc->status, 0, acpi_acad_sysctl, "I", "");
    }

    /* Get initial status after whole system is up. */
    sc->status = -1;

    /*
     * Install both system and device notify handlers since the Casio
     * FIVA needs them.
     */
    AcpiInstallNotifyHandler(handle, ACPI_ALL_NOTIFY,
			     acpi_acad_notify_handler, dev);
    AcpiOsExecute(OSL_NOTIFY_HANDLER, acpi_acad_init_acline, dev);

    return (0);
}

static int
acpi_acad_ioctl(u_long cmd, caddr_t addr, void *arg)
{
    struct acpi_acad_softc *sc;
    device_t dev;

    dev = (device_t)arg;
    sc = device_get_softc(dev);

    /*
     * No security check required: information retrieval only.  If
     * new functions are added here, a check might be required.
     */
    switch (cmd) {
    case ACPIIO_ACAD_GET_STATUS:
	acpi_acad_get_status(dev);
	*(int *)addr = sc->status;
	break;
    default:
	break;
    }

    return (0);
}

static int
acpi_acad_sysctl(SYSCTL_HANDLER_ARGS)
{
    int val, error;

    if (acpi_acad_get_acline(&val) != 0)
	return (ENXIO);

    val = *(u_int *)oidp->oid_arg1;
    error = sysctl_handle_int(oidp, &val, 0, req);
    return (error);
}

static void
acpi_acad_init_acline(void *arg)
{
    struct acpi_acad_softc *sc;
    device_t	dev;
    int		retry;

    dev = (device_t)arg;
    sc = device_get_softc(dev);
    ACPI_VPRINT(dev, acpi_device_get_parent_softc(dev),
		"acline initialization start\n");

    for (retry = 0; retry < ACPI_ACAD_RETRY_MAX; retry++) {
	acpi_acad_get_status(dev);
	if (sc->status != -1)
	    break;
	AcpiOsSleep(10000);
    }

    ACPI_VPRINT(dev, acpi_device_get_parent_softc(dev),
		"acline initialization done, tried %d times\n", retry + 1);
}

/*
 * If no AC line devices detected after boot, create an "online" event
 * so that userland code can adjust power settings accordingly.  The default
 * power profile is "performance" so we don't need to repeat that here.
 */
static void
acpi_acad_ac_only(void __unused *arg)
{

    if (devclass_get_count(acpi_acad_devclass) == 0)
	acpi_UserNotify("ACAD", ACPI_ROOT_OBJECT, 1);
}

/*
 * Public interfaces.
 */
int
acpi_acad_get_acline(int *status)
{
    struct acpi_acad_softc *sc;
    device_t dev;

    dev = devclass_get_device(acpi_acad_devclass, 0);
    if (dev == NULL)
	return (ENXIO);
    sc = device_get_softc(dev);

    acpi_acad_get_status(dev);
    *status = sc->status;

    return (0);
}
