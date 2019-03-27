/*-
 * Copyright (c) 2005-2006 Mitsuru IWASAKI <iwasaki@FreeBSD.org>
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

#include "opt_acpi.h"
#include <sys/param.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>

#include <contrib/dev/acpica/include/acpi.h>
#include <contrib/dev/acpica/include/accommon.h>

#include <dev/acpica/acpivar.h>
#include <dev/acpica/acpiio.h>

/* Hooks for the ACPI CA debugging infrastructure */
#define _COMPONENT	ACPI_DOCK
ACPI_MODULE_NAME("DOCK")

/* For Docking status */
#define ACPI_DOCK_STATUS_UNKNOWN	-1
#define ACPI_DOCK_STATUS_UNDOCKED	0
#define ACPI_DOCK_STATUS_DOCKED		1

#define ACPI_DOCK_UNLOCK		0 /* Allow device to be ejected */
#define ACPI_DOCK_LOCK			1 /* Prevent dev from being removed */

#define ACPI_DOCK_ISOLATE		0 /* Isolate from dock connector */
#define ACPI_DOCK_CONNECT		1 /* Connect to dock */

struct acpi_dock_softc {
	int		_sta;
	int		_bdn;
	int		_uid;
	int		status;
	struct sysctl_ctx_list	*sysctl_ctx;
	struct sysctl_oid	*sysctl_tree;
};

ACPI_SERIAL_DECL(dock, "ACPI Docking Station");

/*
 * Utility functions
 */

static void
acpi_dock_get_info(device_t dev)
{
	struct acpi_dock_softc *sc;
	ACPI_HANDLE	h;

	sc = device_get_softc(dev);
	h = acpi_get_handle(dev);

	if (ACPI_FAILURE(acpi_GetInteger(h, "_STA", &sc->_sta)))
		sc->_sta = ACPI_DOCK_STATUS_UNKNOWN;
	if (ACPI_FAILURE(acpi_GetInteger(h, "_BDN", &sc->_bdn)))
		sc->_bdn = ACPI_DOCK_STATUS_UNKNOWN;
	if (ACPI_FAILURE(acpi_GetInteger(h, "_UID", &sc->_uid)))
		sc->_uid = ACPI_DOCK_STATUS_UNKNOWN;
	ACPI_VPRINT(dev, acpi_device_get_parent_softc(dev),
		    "_STA: %04x, _BDN: %04x, _UID: %04x\n", sc->_sta,
		    sc->_bdn, sc->_uid);
}

static int
acpi_dock_execute_dck(device_t dev, int dock)
{
	ACPI_HANDLE	h;
	ACPI_OBJECT	argobj;
	ACPI_OBJECT_LIST args;
	ACPI_BUFFER	buf;
	ACPI_OBJECT	retobj;
	ACPI_STATUS	status;

	h = acpi_get_handle(dev);

	argobj.Type = ACPI_TYPE_INTEGER;
	argobj.Integer.Value = dock;
	args.Count = 1;
	args.Pointer = &argobj;
	buf.Pointer = &retobj;
	buf.Length = sizeof(retobj);
	status = AcpiEvaluateObject(h, "_DCK", &args, &buf);

	/*
	 * When _DCK is called with 0, OSPM will ignore the return value.
	 */
	if (dock == ACPI_DOCK_ISOLATE)
		return (0);

	/* If _DCK returned 1, the request succeeded. */
	if (ACPI_SUCCESS(status) && retobj.Type == ACPI_TYPE_INTEGER &&
	    retobj.Integer.Value == 1)
		return (0);

	return (-1);
}

/* Lock devices while docked to prevent surprise removal. */
static void
acpi_dock_execute_lck(device_t dev, int lock)
{
	ACPI_HANDLE	h;

	h = acpi_get_handle(dev);
	acpi_SetInteger(h, "_LCK", lock);
}

/* Eject a device (i.e., motorized). */
static int
acpi_dock_execute_ejx(device_t dev, int eject, int state)
{
	ACPI_HANDLE	h;
	ACPI_STATUS	status;
	char		ejx[5];

	h = acpi_get_handle(dev);
	snprintf(ejx, sizeof(ejx), "_EJ%d", state);
	status = acpi_SetInteger(h, ejx, eject);
	if (ACPI_SUCCESS(status))
		return (0);

	return (-1);
}

/* Find dependent devices.  When their parent is removed, so are they. */
static int
acpi_dock_is_ejd_device(ACPI_HANDLE dock_handle, ACPI_HANDLE handle)
{
	int		ret;
	ACPI_STATUS	ret_status;
	ACPI_BUFFER	ejd_buffer;
	ACPI_OBJECT	*obj;

	ret = 0;

	ejd_buffer.Pointer = NULL;
	ejd_buffer.Length = ACPI_ALLOCATE_BUFFER;
	ret_status = AcpiEvaluateObject(handle, "_EJD", NULL, &ejd_buffer);
	if (ACPI_FAILURE(ret_status))
		goto out;

	obj = (ACPI_OBJECT *)ejd_buffer.Pointer;
	if (dock_handle == acpi_GetReference(NULL, obj))
		ret = 1;

out:
	if (ejd_buffer.Pointer != NULL)
		AcpiOsFree(ejd_buffer.Pointer);

	return (ret);
}

/*
 * Docking functions
 */

static void
acpi_dock_attach_later(void *context)
{
	device_t	dev;

	dev = (device_t)context;

	if (!device_is_enabled(dev))
		device_enable(dev);

	mtx_lock(&Giant);
	device_probe_and_attach(dev);
	mtx_unlock(&Giant);
}

static ACPI_STATUS
acpi_dock_insert_child(ACPI_HANDLE handle, UINT32 level, void *context,
    void **status)
{
	device_t	dock_dev, dev;
	ACPI_HANDLE	dock_handle;

	dock_dev = (device_t)context;
	dock_handle = acpi_get_handle(dock_dev);

	if (!acpi_dock_is_ejd_device(dock_handle, handle))
		goto out;

	ACPI_VPRINT(dock_dev, acpi_device_get_parent_softc(dock_dev),
		    "inserting device for %s\n", acpi_name(handle));

#if 0
	/*
	 * If the system boot up w/o Docking, the devices under the dock
	 * still un-initialized, also control methods such as _INI, _STA
	 * are not executed.
	 * Normal devices are initialized at booting by calling
	 * AcpiInitializeObjects(), however the devices under the dock
	 * need to be initialized here on the scheme of ACPICA.
	 */
	ACPI_INIT_WALK_INFO	Info;

	AcpiNsWalkNamespace(ACPI_TYPE_ANY, handle,
	    100, TRUE, AcpiNsInitOneDevice, NULL, &Info, NULL);
#endif

	dev = acpi_get_device(handle);
	if (dev == NULL) {
		device_printf(dock_dev, "error: %s has no associated device\n",
		    acpi_name(handle));
		goto out;
	}

	AcpiOsExecute(OSL_NOTIFY_HANDLER, acpi_dock_attach_later, dev);

out:
	return (AE_OK);
}

static void
acpi_dock_insert_children(device_t dev)
{
	ACPI_STATUS	status;
	ACPI_HANDLE	sb_handle;

	status = AcpiGetHandle(ACPI_ROOT_OBJECT, "\\_SB_", &sb_handle);
	if (ACPI_SUCCESS(status)) {
		AcpiWalkNamespace(ACPI_TYPE_DEVICE, sb_handle,
		    100, acpi_dock_insert_child, NULL, dev, NULL);
	}
}

static void
acpi_dock_insert(device_t dev)
{
	struct acpi_dock_softc *sc;

	ACPI_SERIAL_ASSERT(dock);

	sc = device_get_softc(dev);

	if (sc->status == ACPI_DOCK_STATUS_UNDOCKED ||
	    sc->status == ACPI_DOCK_STATUS_UNKNOWN) {
		acpi_dock_execute_lck(dev, ACPI_DOCK_LOCK);
		if (acpi_dock_execute_dck(dev, ACPI_DOCK_CONNECT) != 0) {
			device_printf(dev, "_DCK failed\n");
			return;
		}

		if (!cold)
			acpi_dock_insert_children(dev);
		sc->status = ACPI_DOCK_STATUS_DOCKED;
	}
}

/*
 * Undock
 */

static ACPI_STATUS
acpi_dock_eject_child(ACPI_HANDLE handle, UINT32 level, void *context,
    void **status)
{
	device_t	dock_dev, dev;
	ACPI_HANDLE	dock_handle;

	dock_dev = *(device_t *)context;
	dock_handle = acpi_get_handle(dock_dev);

	if (!acpi_dock_is_ejd_device(dock_handle, handle))
		goto out;

	ACPI_VPRINT(dock_dev, acpi_device_get_parent_softc(dock_dev),
	    "ejecting device for %s\n", acpi_name(handle));

	dev = acpi_get_device(handle);
	if (dev != NULL && device_is_attached(dev)) {
		mtx_lock(&Giant);
		device_detach(dev);
		mtx_unlock(&Giant);
	}

	acpi_SetInteger(handle, "_EJ0", 0);
out:
	return (AE_OK);
}

static void
acpi_dock_eject_children(device_t dev)
{
	ACPI_HANDLE	sb_handle;
	ACPI_STATUS	status;

	status = AcpiGetHandle(ACPI_ROOT_OBJECT, "\\_SB_", &sb_handle);
	if (ACPI_SUCCESS(status)) {
		AcpiWalkNamespace(ACPI_TYPE_DEVICE, sb_handle,
		    100, acpi_dock_eject_child, NULL, &dev, NULL);
	}
}

static void
acpi_dock_removal(device_t dev)
{
	struct acpi_dock_softc *sc;

	ACPI_SERIAL_ASSERT(dock);

	sc = device_get_softc(dev);
	if (sc->status == ACPI_DOCK_STATUS_DOCKED ||
	    sc->status == ACPI_DOCK_STATUS_UNKNOWN) {
		acpi_dock_eject_children(dev);
		if (acpi_dock_execute_dck(dev, ACPI_DOCK_ISOLATE) != 0)
			return;

		acpi_dock_execute_lck(dev, ACPI_DOCK_UNLOCK);

		if (acpi_dock_execute_ejx(dev, 1, 0) != 0) {
			device_printf(dev, "_EJ0 failed\n");
			return;
		}

		sc->status = ACPI_DOCK_STATUS_UNDOCKED;
	}

	acpi_dock_get_info(dev);
	if (sc->_sta != 0)
		device_printf(dev, "mechanical failure (%#x).\n", sc->_sta);
}

/*
 * Device/Bus check
 */

static void
acpi_dock_device_check(device_t dev)
{
	struct acpi_dock_softc *sc;

	ACPI_SERIAL_ASSERT(dock);

	sc = device_get_softc(dev);
	acpi_dock_get_info(dev);

	/*
	 * If the _STA method indicates 'present' and 'functioning', the
	 * system is docked.  If _STA does not exist for this device, it
	 * is always present.
	 */
	if (sc->_sta == ACPI_DOCK_STATUS_UNKNOWN ||
	    ACPI_DEVICE_PRESENT(sc->_sta))
		acpi_dock_insert(dev);
	else if (sc->_sta == 0)
		acpi_dock_removal(dev);
}

/*
 * Notify Handler
 */

static void
acpi_dock_notify_handler(ACPI_HANDLE h, UINT32 notify, void *context)
{
	device_t	dev;

	dev = (device_t) context;
	ACPI_VPRINT(dev, acpi_device_get_parent_softc(dev),
		    "got notification %#x\n", notify);

	ACPI_SERIAL_BEGIN(dock);
	switch (notify) {
	case ACPI_NOTIFY_BUS_CHECK:
	case ACPI_NOTIFY_DEVICE_CHECK:
		acpi_dock_device_check(dev);
		break;
	case ACPI_NOTIFY_EJECT_REQUEST:
		acpi_dock_removal(dev);
		break;
	default:
		device_printf(dev, "unknown notify %#x\n", notify);
		break;
	}
	ACPI_SERIAL_END(dock);
}

static int
acpi_dock_status_sysctl(SYSCTL_HANDLER_ARGS)
{
	struct acpi_dock_softc *sc;
	device_t	dev;
	int		status, err;

	dev = (device_t)arg1;

	sc = device_get_softc(dev);
	status = sc->status;

	ACPI_SERIAL_BEGIN(dock);
	err = sysctl_handle_int(oidp, &status, 0, req);
	if (err != 0 || req->newptr == NULL)
		goto out;

	if (status != ACPI_DOCK_STATUS_UNDOCKED &&
	    status != ACPI_DOCK_STATUS_DOCKED) {
		err = EINVAL;
		goto out;
	}

	if (status == sc->status)
		goto out;

	switch (status) {
	case ACPI_DOCK_STATUS_UNDOCKED:
		acpi_dock_removal(dev);
		break;
	case ACPI_DOCK_STATUS_DOCKED:
		acpi_dock_device_check(dev);
		break;
	default:
		err = EINVAL;
		break;
	}
out:
	ACPI_SERIAL_END(dock);
	return (err);
}

static int
acpi_dock_probe(device_t dev)
{
	ACPI_HANDLE	h, tmp;

	h = acpi_get_handle(dev);
	if (acpi_disabled("dock") ||
	    ACPI_FAILURE(AcpiGetHandle(h, "_DCK", &tmp)))
		return (ENXIO);

	device_set_desc(dev, "ACPI Docking Station");

	/*
	 * XXX Somewhere else in the kernel panics on "sysctl kern" if we
	 * return a negative value here (reprobe ok).
	 */
	return (0);
}

static int
acpi_dock_attach(device_t dev)
{
	struct acpi_dock_softc *sc;
	ACPI_HANDLE	h;

	sc = device_get_softc(dev);
	h = acpi_get_handle(dev);
	if (sc == NULL || h == NULL)
		return (ENXIO);

	sc->status = ACPI_DOCK_STATUS_UNKNOWN;

	AcpiEvaluateObject(h, "_INI", NULL, NULL);

	ACPI_SERIAL_BEGIN(dock);

	acpi_dock_device_check(dev);

	/* Get the sysctl tree */
	sc->sysctl_ctx = device_get_sysctl_ctx(dev);
	sc->sysctl_tree = device_get_sysctl_tree(dev);

	SYSCTL_ADD_INT(sc->sysctl_ctx,
		SYSCTL_CHILDREN(sc->sysctl_tree),
		OID_AUTO, "_sta", CTLFLAG_RD,
		&sc->_sta, 0, "Dock _STA");
	SYSCTL_ADD_INT(sc->sysctl_ctx,
		SYSCTL_CHILDREN(sc->sysctl_tree),
		OID_AUTO, "_bdn", CTLFLAG_RD,
		&sc->_bdn, 0, "Dock _BDN");
	SYSCTL_ADD_INT(sc->sysctl_ctx,
		SYSCTL_CHILDREN(sc->sysctl_tree),
		OID_AUTO, "_uid", CTLFLAG_RD,
		&sc->_uid, 0, "Dock _UID");
	SYSCTL_ADD_PROC(sc->sysctl_ctx,
		SYSCTL_CHILDREN(sc->sysctl_tree),
		OID_AUTO, "status",
		CTLTYPE_INT|CTLFLAG_RW, dev, 0,
		acpi_dock_status_sysctl, "I",
		"Dock/Undock operation");

	ACPI_SERIAL_END(dock);

	AcpiInstallNotifyHandler(h, ACPI_ALL_NOTIFY,
				 acpi_dock_notify_handler, dev);

	return (0);
}

static device_method_t acpi_dock_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe, acpi_dock_probe),
	DEVMETHOD(device_attach, acpi_dock_attach),

	DEVMETHOD_END
};

static driver_t	acpi_dock_driver = {
	"acpi_dock",
	acpi_dock_methods,
	sizeof(struct acpi_dock_softc),
};

static devclass_t acpi_dock_devclass;

DRIVER_MODULE(acpi_dock, acpi, acpi_dock_driver, acpi_dock_devclass, 0, 0);
MODULE_DEPEND(acpi_dock, acpi, 1, 1, 1);

