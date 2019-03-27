/*-
 * Copyright (c) 2009 Michael Gmelin <freebsd@grem.de>
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
 * Driver for acpi-wmi mapping, provides an interface for vendor specific
 * implementations (e.g. HP and Acer laptops).
 * Inspired by the ACPI-WMI mapping driver (c) 2008-2008 Carlos Corbacho which
 * implements this functionality for Linux.
 *
 * WMI and ACPI: http://www.microsoft.com/whdc/system/pnppwr/wmi/wmi-acpi.mspx
 * acpi-wmi for Linux: http://www.kernel.org
 */

#include "opt_acpi.h"
#include <sys/param.h>
#include <sys/conf.h>
#include <sys/uio.h>
#include <sys/proc.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/sbuf.h>
#include <sys/module.h>
#include <sys/bus.h>

#include <contrib/dev/acpica/include/acpi.h>
#include <contrib/dev/acpica/include/accommon.h>
#include <dev/acpica/acpivar.h>
#include "acpi_wmi_if.h"

static MALLOC_DEFINE(M_ACPIWMI, "acpiwmi", "ACPI-WMI mapping");

#define _COMPONENT	ACPI_OEM
ACPI_MODULE_NAME("ACPI_WMI");

#define ACPI_WMI_REGFLAG_EXPENSIVE	0x1 /* GUID flag: Expensive operation */
#define ACPI_WMI_REGFLAG_METHOD		0x2	/* GUID flag: Method call */
#define ACPI_WMI_REGFLAG_STRING		0x4	/* GUID flag: String */
#define ACPI_WMI_REGFLAG_EVENT		0x8	/* GUID flag: Event */

/*
 * acpi_wmi driver private structure
 */
struct acpi_wmi_softc {
	device_t	wmi_dev;	/* wmi device id */
	ACPI_HANDLE	wmi_handle;	/* handle of the PNP0C14 node */
	device_t	ec_dev;		/* acpi_ec0 */
	struct cdev	*wmistat_dev_t;	/* wmistat device handle */
	struct sbuf	wmistat_sbuf;	/* sbuf for /dev/wmistat output */
	pid_t		wmistat_open_pid; /* pid operating on /dev/wmistat */
	int		wmistat_bufptr;	/* /dev/wmistat ptr to buffer position */
	TAILQ_HEAD(wmi_info_list_head, wmi_info) wmi_info_list;
};

/*
 * Struct that holds information about
 * about a single GUID entry in _WDG
 */
struct guid_info {
	char	guid[16];	/* 16 byte non human readable GUID */
	char	oid[2];		/* object id or event notify id (first byte) */
	UINT8	max_instance;	/* highest instance known for this GUID */
	UINT8	flags;		/* ACPI_WMI_REGFLAG_%s */
};

/* WExx event generation state (on/off) */
enum event_generation_state {
	EVENT_GENERATION_ON = 1,
	EVENT_GENERATION_OFF = 0
};


/*
 * Information about one entry in _WDG.
 * List of those is used to lookup information by GUID.
 */
struct wmi_info {
	TAILQ_ENTRY(wmi_info)	wmi_list;
	struct guid_info	ginfo;		/* information on guid */
	ACPI_NOTIFY_HANDLER	event_handler;/* client provided event handler */
	void			*event_handler_user_data; /* ev handler cookie  */
};


ACPI_SERIAL_DECL(acpi_wmi, "ACPI-WMI Mapping");

/* public interface - declaration */
/* standard device interface*/
static int		acpi_wmi_probe(device_t dev);
static int		acpi_wmi_attach(device_t dev);
static int		acpi_wmi_detach(device_t dev);
/* see acpi_wmi_if.m */
static int		acpi_wmi_provides_guid_string_method(device_t dev,
			    const char *guid_string);
static ACPI_STATUS	acpi_wmi_evaluate_call_method(device_t dev,
			    const char *guid_string, UINT8 instance,
			    UINT32 method_id, const ACPI_BUFFER *in,
			    ACPI_BUFFER *out);
static ACPI_STATUS	acpi_wmi_install_event_handler_method(device_t dev,
			    const char *guid_string, ACPI_NOTIFY_HANDLER handler,
			    void *data);
static ACPI_STATUS	acpi_wmi_remove_event_handler_method(device_t dev,
			    const char *guid_string);
static ACPI_STATUS	acpi_wmi_get_event_data_method(device_t dev,
			    UINT32 event_id, ACPI_BUFFER *out);
static ACPI_STATUS	acpi_wmi_get_block_method(device_t dev,
			    const char *guid_string,
			    UINT8 instance, ACPI_BUFFER *out);
static ACPI_STATUS	acpi_wmi_set_block_method(device_t dev,
			    const char *guid_string,
			    UINT8 instance, const ACPI_BUFFER *in);
/* private interface - declaration */
/* callbacks */
static void		acpi_wmi_notify_handler(ACPI_HANDLE h, UINT32 notify,
			    void *context);
static ACPI_STATUS	acpi_wmi_ec_handler(UINT32 function,
			    ACPI_PHYSICAL_ADDRESS address, UINT32 width,
			    UINT64 *value, void *context,
			    void *region_context);
/* helpers */
static ACPI_STATUS	acpi_wmi_read_wdg_blocks(struct acpi_wmi_softc *sc, ACPI_HANDLE h);
static ACPI_STATUS	acpi_wmi_toggle_we_event_generation(device_t dev,
			    struct wmi_info *winfo,
			    enum event_generation_state state);
static int		acpi_wmi_guid_string_to_guid(const UINT8 *guid_string,
			    UINT8 *guid);
static struct wmi_info* acpi_wmi_lookup_wmi_info_by_guid_string(struct acpi_wmi_softc *sc,
			    const char *guid_string);

static d_open_t acpi_wmi_wmistat_open;
static d_close_t acpi_wmi_wmistat_close;
static d_read_t acpi_wmi_wmistat_read;

/* handler /dev/wmistat device */
static struct cdevsw wmistat_cdevsw = {
	.d_version = D_VERSION,
	.d_open = acpi_wmi_wmistat_open,
	.d_close = acpi_wmi_wmistat_close,
	.d_read = acpi_wmi_wmistat_read,
	.d_name = "wmistat",
};


static device_method_t acpi_wmi_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,	acpi_wmi_probe),
	DEVMETHOD(device_attach, acpi_wmi_attach),
	DEVMETHOD(device_detach, acpi_wmi_detach),

	/* bus interface */
	DEVMETHOD(bus_add_child,	bus_generic_add_child),

	/* acpi_wmi interface */
	DEVMETHOD(acpi_wmi_provides_guid_string,
		    acpi_wmi_provides_guid_string_method),
	DEVMETHOD(acpi_wmi_evaluate_call, acpi_wmi_evaluate_call_method),
	DEVMETHOD(acpi_wmi_install_event_handler,
		    acpi_wmi_install_event_handler_method),
	DEVMETHOD(acpi_wmi_remove_event_handler,
		    acpi_wmi_remove_event_handler_method),
	DEVMETHOD(acpi_wmi_get_event_data, acpi_wmi_get_event_data_method),
	DEVMETHOD(acpi_wmi_get_block, acpi_wmi_get_block_method),
	DEVMETHOD(acpi_wmi_set_block, acpi_wmi_set_block_method),

	DEVMETHOD_END
};

static driver_t acpi_wmi_driver = {
	"acpi_wmi",
	acpi_wmi_methods,
	sizeof(struct acpi_wmi_softc),
};

static devclass_t acpi_wmi_devclass;
DRIVER_MODULE(acpi_wmi, acpi, acpi_wmi_driver, acpi_wmi_devclass, 0, 0);
MODULE_VERSION(acpi_wmi, 1);
MODULE_DEPEND(acpi_wmi, acpi, 1, 1, 1);
static char *wmi_ids[] = {"PNP0C14", NULL};

/*
 * Probe for the PNP0C14 ACPI node
 */
static int
acpi_wmi_probe(device_t dev)
{
	int rv; 

	if (acpi_disabled("wmi"))
		return (ENXIO);
	rv = ACPI_ID_PROBE(device_get_parent(dev), dev, wmi_ids, NULL);
	if (rv <= 0)
		device_set_desc(dev, "ACPI-WMI mapping");

	return (rv);
}

/*
 * Attach the device by:
 * - Looking for the first ACPI EC device
 * - Install the notify handler
 * - Install the EC address space handler
 * - Look for the _WDG node and read GUID information blocks
 */
static int
acpi_wmi_attach(device_t dev)
{
	struct acpi_wmi_softc *sc;
	int ret;
	ACPI_STATUS status;

	ACPI_FUNCTION_TRACE((char *)(uintptr_t)__func__);
	sc = device_get_softc(dev);
	ret = ENXIO;

	ACPI_SERIAL_BEGIN(acpi_wmi);
	sc->wmi_dev = dev;
	sc->wmi_handle = acpi_get_handle(dev);
	TAILQ_INIT(&sc->wmi_info_list);
	/* XXX Only works with one EC, but nearly all systems only have one. */
	if ((sc->ec_dev = devclass_get_device(devclass_find("acpi_ec"), 0))
	    == NULL)
		device_printf(dev, "cannot find EC device\n");
	else if (ACPI_FAILURE((status = AcpiInstallNotifyHandler(sc->wmi_handle,
		    ACPI_DEVICE_NOTIFY, acpi_wmi_notify_handler, sc))))
		device_printf(sc->wmi_dev, "couldn't install notify handler - %s\n",
		    AcpiFormatException(status));
	else if (ACPI_FAILURE((status = AcpiInstallAddressSpaceHandler(
		    sc->wmi_handle, ACPI_ADR_SPACE_EC, acpi_wmi_ec_handler,
		    NULL, sc)))) {
		device_printf(sc->wmi_dev, "couldn't install EC handler - %s\n",
		    AcpiFormatException(status));
		AcpiRemoveNotifyHandler(sc->wmi_handle, ACPI_DEVICE_NOTIFY,
		    acpi_wmi_notify_handler);
	} else if (ACPI_FAILURE((status = acpi_wmi_read_wdg_blocks(sc,
		    sc->wmi_handle)))) {
		device_printf(sc->wmi_dev, "couldn't parse _WDG - %s\n",
		    AcpiFormatException(status));
		AcpiRemoveNotifyHandler(sc->wmi_handle, ACPI_DEVICE_NOTIFY,
		    acpi_wmi_notify_handler);
		AcpiRemoveAddressSpaceHandler(sc->wmi_handle, ACPI_ADR_SPACE_EC,
		    acpi_wmi_ec_handler);
	} else {
		sc->wmistat_dev_t = make_dev(&wmistat_cdevsw, 0, UID_ROOT,
		    GID_WHEEL, 0644, "wmistat%d", device_get_unit(dev));
		sc->wmistat_dev_t->si_drv1 = sc;
		sc->wmistat_open_pid = 0;
		sc->wmistat_bufptr = -1;
		ret = 0;
	}
	ACPI_SERIAL_END(acpi_wmi);

	if (ret == 0) {
		bus_generic_probe(dev);
		ret = bus_generic_attach(dev);
	}

	return (ret);
}

/*
 * Detach the driver by:
 * - Removing notification handler
 * - Removing address space handler
 * - Turning off event generation for all WExx event activated by
 *   child drivers
 */
static int
acpi_wmi_detach(device_t dev)
{
	struct wmi_info *winfo, *tmp;
	struct acpi_wmi_softc *sc;
	int ret;

	ACPI_FUNCTION_TRACE((char *)(uintptr_t)__func__);
	sc = device_get_softc(dev);
	ACPI_SERIAL_BEGIN(acpi_wmi);

	if (sc->wmistat_open_pid != 0) {
		ret = EBUSY;
	} else {
		AcpiRemoveNotifyHandler(sc->wmi_handle, ACPI_DEVICE_NOTIFY,
		    acpi_wmi_notify_handler);
		AcpiRemoveAddressSpaceHandler(sc->wmi_handle,
		    ACPI_ADR_SPACE_EC, acpi_wmi_ec_handler);
		TAILQ_FOREACH_SAFE(winfo, &sc->wmi_info_list, wmi_list, tmp) {
			if (winfo->event_handler)
				acpi_wmi_toggle_we_event_generation(dev,
				    winfo, EVENT_GENERATION_OFF);
			TAILQ_REMOVE(&sc->wmi_info_list, winfo, wmi_list);
			free(winfo, M_ACPIWMI);
		}
		if (sc->wmistat_bufptr != -1) {
			sbuf_delete(&sc->wmistat_sbuf);
			sc->wmistat_bufptr = -1;
		}
		sc->wmistat_open_pid = 0;
		destroy_dev(sc->wmistat_dev_t);
		ret = 0;
	}
	ACPI_SERIAL_END(acpi_wmi);

	return (ret);
}


/*
 * Check if the given GUID string (human readable format
 * AABBCCDD-EEFF-GGHH-IIJJ-KKLLMMNNOOPP)
 * exists within _WDG
 */
static int
acpi_wmi_provides_guid_string_method(device_t dev, const char *guid_string)
{
	struct acpi_wmi_softc *sc;
	struct wmi_info *winfo;
	int ret;

	sc = device_get_softc(dev);
	ACPI_FUNCTION_TRACE((char *)(uintptr_t)__func__);
	ACPI_SERIAL_BEGIN(acpi_wmi);
	winfo = acpi_wmi_lookup_wmi_info_by_guid_string(sc, guid_string);
	ret = (winfo == NULL)?0:winfo->ginfo.max_instance+1;
	ACPI_SERIAL_END(acpi_wmi);

	return (ret);
}

/*
 * Call a method "method_id" on the given GUID block
 * write result into user provided output buffer
 */
static ACPI_STATUS
acpi_wmi_evaluate_call_method(device_t dev, const char *guid_string,
    UINT8 instance, UINT32 method_id, const ACPI_BUFFER *in, ACPI_BUFFER *out)
{
	ACPI_OBJECT params[3];
	ACPI_OBJECT_LIST input;
	char method[5] = "WMxx";
	struct wmi_info *winfo;
	struct acpi_wmi_softc *sc;
	ACPI_STATUS status;

	ACPI_FUNCTION_TRACE((char *)(uintptr_t)__func__);

	sc = device_get_softc(dev);
	ACPI_SERIAL_BEGIN(acpi_wmi);
	if ((winfo = acpi_wmi_lookup_wmi_info_by_guid_string(sc, guid_string))
		    == NULL)
		status = AE_NOT_FOUND;
	else if (!(winfo->ginfo.flags & ACPI_WMI_REGFLAG_METHOD))
		status = AE_BAD_DATA;
	else if (instance > winfo->ginfo.max_instance)
		status = AE_BAD_PARAMETER;
	else {
		params[0].Type = ACPI_TYPE_INTEGER;
		params[0].Integer.Value = instance;
		params[1].Type = ACPI_TYPE_INTEGER;
		params[1].Integer.Value = method_id;
		input.Pointer = params;
		input.Count = 2;
		if (in) {
			params[2].Type =
			    (winfo->ginfo.flags & ACPI_WMI_REGFLAG_STRING)
			    ?ACPI_TYPE_STRING:ACPI_TYPE_BUFFER;
			params[2].Buffer.Length = in->Length;
			params[2].Buffer.Pointer = in->Pointer;
			input.Count = 3;
		}
		method[2] = winfo->ginfo.oid[0];
		method[3] = winfo->ginfo.oid[1];
		status = AcpiEvaluateObject(sc->wmi_handle, method,
			    &input, out);
	}
	ACPI_SERIAL_END(acpi_wmi);

	return (status);
}

/*
 * Install a user provided event_handler on the given GUID
 * provided *data will be passed on callback
 * If there is already an existing event handler registered it will be silently
 * discarded
 */
static ACPI_STATUS
acpi_wmi_install_event_handler_method(device_t dev, const char *guid_string,
    ACPI_NOTIFY_HANDLER event_handler, void *data)
{
	struct acpi_wmi_softc *sc = device_get_softc(dev);
	struct wmi_info *winfo;
	ACPI_STATUS status;

	ACPI_FUNCTION_TRACE((char *)(uintptr_t)__func__);

	status = AE_OK;
	ACPI_SERIAL_BEGIN(acpi_wmi);
	if (guid_string == NULL || event_handler == NULL)
		status = AE_BAD_PARAMETER;
	else if ((winfo = acpi_wmi_lookup_wmi_info_by_guid_string(sc, guid_string))
		    == NULL)
		status = AE_NOT_EXIST;
	else if (winfo->event_handler != NULL ||
		(status = acpi_wmi_toggle_we_event_generation(dev, winfo,
		    EVENT_GENERATION_ON)) == AE_OK) {
		winfo->event_handler = event_handler;
		winfo->event_handler_user_data = data;
	}
	ACPI_SERIAL_END(acpi_wmi);

	return (status);
}

/*
 * Remove a previously installed event handler from the given GUID
 * If there was none installed, this call is silently discarded and
 * reported as AE_OK
 */
static ACPI_STATUS
acpi_wmi_remove_event_handler_method(device_t dev, const char *guid_string)
{
	struct acpi_wmi_softc *sc = device_get_softc(dev);
	struct wmi_info *winfo;
	ACPI_STATUS status;

	ACPI_FUNCTION_TRACE((char *)(uintptr_t)__func__);

	status = AE_OK;
	ACPI_SERIAL_BEGIN(acpi_wmi);
	if (guid_string &&
	    (winfo = acpi_wmi_lookup_wmi_info_by_guid_string(sc, guid_string))
	    != NULL && winfo->event_handler) {
		status = acpi_wmi_toggle_we_event_generation(dev, winfo,
			    EVENT_GENERATION_OFF);
		winfo->event_handler = NULL;
		winfo->event_handler_user_data = NULL;
	}
	ACPI_SERIAL_END(acpi_wmi);

	return (status);
}

/*
 * Get details on an event received through a callback registered
 * through ACPI_WMI_REMOVE_EVENT_HANDLER into a user provided output buffer.
 * (event_id equals "notify" passed in the callback)
 */
static ACPI_STATUS
acpi_wmi_get_event_data_method(device_t dev, UINT32 event_id, ACPI_BUFFER *out)
{
	ACPI_OBJECT_LIST input;
	ACPI_OBJECT params[1];
	struct acpi_wmi_softc *sc;
	struct wmi_info *winfo;
	ACPI_STATUS status;

	ACPI_FUNCTION_TRACE((char *)(uintptr_t)__func__);

	sc = device_get_softc(dev);
	status = AE_NOT_FOUND;	
	ACPI_SERIAL_BEGIN(acpi_wmi);
	params[0].Type = ACPI_TYPE_INTEGER;
	params[0].Integer.Value = event_id;
	input.Pointer = params;
	input.Count = 1;
	TAILQ_FOREACH(winfo, &sc->wmi_info_list, wmi_list) {
		if ((winfo->ginfo.flags & ACPI_WMI_REGFLAG_EVENT) &&
		    ((UINT8) winfo->ginfo.oid[0] == event_id)) {
			status = AcpiEvaluateObject(sc->wmi_handle, "_WED",
				    &input, out);
			break;
		}
	}
	ACPI_SERIAL_END(acpi_wmi);

	return (status);
}

/*
 * Read a block of data from the given GUID (using WQxx (query))
 * Will be returned in a user provided buffer (out).
 * If the method is marked as expensive (ACPI_WMI_REGFLAG_EXPENSIVE)
 * we will first call the WCxx control method to lock the node to
 * lock the node for data collection and release it afterwards.
 * (Failed WCxx calls are ignored to "support" broken implementations)
 */
static ACPI_STATUS
acpi_wmi_get_block_method(device_t dev, const char *guid_string, UINT8 instance,
	ACPI_BUFFER *out)
{
	char wc_method[5] = "WCxx";
	char wq_method[5] = "WQxx";
	ACPI_OBJECT_LIST wc_input;
	ACPI_OBJECT_LIST wq_input;
	ACPI_OBJECT wc_params[1];
	ACPI_OBJECT wq_params[1];
	ACPI_HANDLE wc_handle;
	struct acpi_wmi_softc *sc;
	struct wmi_info *winfo;
	ACPI_STATUS status;
	ACPI_STATUS wc_status;

	ACPI_FUNCTION_TRACE((char *)(uintptr_t)__func__);

	sc = device_get_softc(dev);
	wc_status = AE_ERROR;
	ACPI_SERIAL_BEGIN(acpi_wmi);
	if (guid_string == NULL || out == NULL)
		status = AE_BAD_PARAMETER;
	else if ((winfo = acpi_wmi_lookup_wmi_info_by_guid_string(sc, guid_string))
		    == NULL)
		status = AE_ERROR;
	else if (instance > winfo->ginfo.max_instance)
		status = AE_BAD_PARAMETER;
	else if ((winfo->ginfo.flags & ACPI_WMI_REGFLAG_EVENT) ||
	    (winfo->ginfo.flags & ACPI_WMI_REGFLAG_METHOD))
		status = AE_ERROR;
	else {
		wq_params[0].Type = ACPI_TYPE_INTEGER;
		wq_params[0].Integer.Value = instance;
		wq_input.Pointer = wq_params;
		wq_input.Count = 1;
		if (winfo->ginfo.flags & ACPI_WMI_REGFLAG_EXPENSIVE) {
			wc_params[0].Type = ACPI_TYPE_INTEGER;
			wc_params[0].Integer.Value = 1;
			wc_input.Pointer = wc_params;
			wc_input.Count = 1;
			wc_method[2] = winfo->ginfo.oid[0];
			wc_method[3] = winfo->ginfo.oid[1];
			wc_status = AcpiGetHandle(sc->wmi_handle, wc_method,
				    &wc_handle);
			if (ACPI_SUCCESS(wc_status))
				wc_status = AcpiEvaluateObject(wc_handle,
						wc_method, &wc_input, NULL);
		}
		wq_method[2] = winfo->ginfo.oid[0];
		wq_method[3] = winfo->ginfo.oid[1];
		status = AcpiEvaluateObject(sc->wmi_handle, wq_method,
			    &wq_input, out);
		if ((winfo->ginfo.flags & ACPI_WMI_REGFLAG_EXPENSIVE)
		    && ACPI_SUCCESS(wc_status)) {
			wc_params[0].Integer.Value = 0;
			status = AcpiEvaluateObject(wc_handle, wc_method,
				    &wc_input, NULL);  /* XXX this might be
				    			 the wrong status to
				    			 return? */
		}
	}
	ACPI_SERIAL_END(acpi_wmi);

	return (status);
}

/*
 * Write a block of data to the given GUID (using WSxx)
 */
static ACPI_STATUS
acpi_wmi_set_block_method(device_t dev, const char *guid_string, UINT8 instance,
	const ACPI_BUFFER *in)
{
	char method[5] = "WSxx";
	ACPI_OBJECT_LIST input;
	ACPI_OBJECT params[2];
	struct wmi_info *winfo;
	struct acpi_wmi_softc *sc;
	ACPI_STATUS status;

	ACPI_FUNCTION_TRACE((char *)(uintptr_t)__func__);

	sc = device_get_softc(dev);
	ACPI_SERIAL_BEGIN(acpi_wmi);
	if (guid_string == NULL || in == NULL)
		status = AE_BAD_DATA;
	else if ((winfo = acpi_wmi_lookup_wmi_info_by_guid_string(sc, guid_string))
		    == NULL)
		status = AE_ERROR;
	else if (instance > winfo->ginfo.max_instance)
		status = AE_BAD_PARAMETER;
	else if ((winfo->ginfo.flags & ACPI_WMI_REGFLAG_EVENT) ||
		    (winfo->ginfo.flags & ACPI_WMI_REGFLAG_METHOD))
		status = AE_ERROR;
	else {
		params[0].Type = ACPI_TYPE_INTEGER;
		params[0].Integer.Value = instance;
		input.Pointer = params;
		input.Count = 2;
		params[1].Type = (winfo->ginfo.flags & ACPI_WMI_REGFLAG_STRING)
		    ?ACPI_TYPE_STRING:ACPI_TYPE_BUFFER;
		params[1].Buffer.Length = in->Length;
		params[1].Buffer.Pointer = in->Pointer;
		method[2] = winfo->ginfo.oid[0];
		method[3] = winfo->ginfo.oid[1];
		status = AcpiEvaluateObject(sc->wmi_handle, method,
			    &input, NULL);
	}
	ACPI_SERIAL_END(acpi_wmi);

	return (status);
}

/*
 * Handle events received and dispatch them to
 * stakeholders that registered through ACPI_WMI_INSTALL_EVENT_HANDLER
 */
static void
acpi_wmi_notify_handler(ACPI_HANDLE h, UINT32 notify, void *context)
{
	struct acpi_wmi_softc *sc = context;
	ACPI_NOTIFY_HANDLER handler;
	void *handler_data;
	struct wmi_info *winfo;

	ACPI_FUNCTION_TRACE_U32((char *)(uintptr_t)__func__, notify);

	handler = NULL;
	handler_data = NULL;
	ACPI_SERIAL_BEGIN(acpi_wmi);
	TAILQ_FOREACH(winfo, &sc->wmi_info_list, wmi_list) {
		if ((winfo->ginfo.flags & ACPI_WMI_REGFLAG_EVENT) &&
				((UINT8) winfo->ginfo.oid[0] == notify)) {
			if (winfo->event_handler) {
				handler = winfo->event_handler;
				handler_data = winfo->event_handler_user_data;
				break;
			}
		}
	}
	ACPI_SERIAL_END(acpi_wmi);
	if (handler) {
		handler(h, notify, handler_data);
	}
}

/*
 * Handle EC address space notifications reveived on the WDG node
 * (this mimics EcAddressSpaceHandler in acpi_ec.c)
 */
static ACPI_STATUS
acpi_wmi_ec_handler(UINT32 function, ACPI_PHYSICAL_ADDRESS address,
    UINT32 width, UINT64 *value, void *context,
    void *region_context)
{
	struct acpi_wmi_softc *sc;
	int i;
	UINT64 ec_data;
	UINT8 ec_addr;
	ACPI_STATUS status;

	ACPI_FUNCTION_TRACE_U32((char *)(uintptr_t)__func__, (UINT32)address);

	sc = (struct acpi_wmi_softc *)context;
	if (width % 8 != 0 || value == NULL || context == NULL)
		return (AE_BAD_PARAMETER);
	if (address + (width / 8) - 1 > 0xFF)
		return (AE_BAD_ADDRESS);
	if (function == ACPI_READ)
		*value = 0;
	ec_addr = address;
	status = AE_ERROR;

	for (i = 0; i < width; i += 8, ++ec_addr) {
		switch (function) {
		case ACPI_READ:
			status = ACPI_EC_READ(sc->ec_dev, ec_addr, &ec_data, 1);
			if (ACPI_SUCCESS(status))
				*value |= ((UINT64)ec_data) << i;
		break;
		case ACPI_WRITE:
			ec_data = (UINT8)((*value) >> i);
			status = ACPI_EC_WRITE(sc->ec_dev, ec_addr, ec_data, 1);
			break;
		default:
			device_printf(sc->wmi_dev,
			    "invalid acpi_wmi_ec_handler function %d\n",
			    function);
			status = AE_BAD_PARAMETER;
			break;
		}
		if (ACPI_FAILURE(status))
			break;
	}

	return (status);
}

/*
 * Read GUID blocks from the _WDG node
 * into wmi_info_list.
 */
static ACPI_STATUS
acpi_wmi_read_wdg_blocks(struct acpi_wmi_softc *sc, ACPI_HANDLE h)
{
	ACPI_BUFFER out = {ACPI_ALLOCATE_BUFFER, NULL};
	struct guid_info *ginfo;
	ACPI_OBJECT *obj;
	struct wmi_info *winfo;
	UINT32 i;
	UINT32 wdg_block_count;
	ACPI_STATUS status;

	ACPI_FUNCTION_TRACE((char *)(uintptr_t)__func__);

	ACPI_SERIAL_ASSERT(acpi_wmi);
	if (ACPI_FAILURE(status = AcpiEvaluateObject(h, "_WDG", NULL, &out)))
		return (status);
	obj = (ACPI_OBJECT*) out.Pointer;
	wdg_block_count = obj->Buffer.Length / sizeof(struct guid_info);
	if ((ginfo = malloc(obj->Buffer.Length, M_ACPIWMI, M_NOWAIT))
		    == NULL) {
		AcpiOsFree(out.Pointer);
		return (AE_NO_MEMORY);
	}
	memcpy(ginfo, obj->Buffer.Pointer, obj->Buffer.Length);
	for (i = 0; i < wdg_block_count; ++i) {
		if ((winfo = malloc(sizeof(struct wmi_info), M_ACPIWMI,
			    M_NOWAIT | M_ZERO)) == NULL) {
			AcpiOsFree(out.Pointer);
			free(ginfo, M_ACPIWMI);
			return (AE_NO_MEMORY);
		}
		winfo->ginfo = ginfo[i];
		TAILQ_INSERT_TAIL(&sc->wmi_info_list, winfo, wmi_list);
	}
	AcpiOsFree(out.Pointer);
	free(ginfo, M_ACPIWMI);

	return (status);
}

/*
 * Toggle event generation in for the given GUID (passed by winfo)
 * Turn on to get notified (through acpi_wmi_notify_handler) if events happen
 * on the given GUID.
 */
static ACPI_STATUS
acpi_wmi_toggle_we_event_generation(device_t dev, struct wmi_info *winfo,
    enum event_generation_state state)
{
	char method[5] = "WExx";
	ACPI_OBJECT_LIST input;
	ACPI_OBJECT params[1];
	struct acpi_wmi_softc *sc;
	ACPI_STATUS status;

	ACPI_FUNCTION_TRACE((char *)(uintptr_t)__func__);

	sc = device_get_softc(dev);
	ACPI_SERIAL_ASSERT(acpi_wmi);
	params[0].Type = ACPI_TYPE_INTEGER;
	params[0].Integer.Value = state==EVENT_GENERATION_ON?1:0;
	input.Pointer = params;
	input.Count = 1;
	
	UINT8 hi = ((UINT8) winfo->ginfo.oid[0]) >> 4;
	UINT8 lo = ((UINT8) winfo->ginfo.oid[0]) & 0xf;
	method[2] = (hi > 9 ? hi + 55: hi + 48);
	method[3] = (lo > 9 ? lo + 55: lo + 48);
	status = AcpiEvaluateObject(sc->wmi_handle, method, &input, NULL);
	if (status == AE_NOT_FOUND) status = AE_OK;

	return (status);
}

/*
 * Convert given two digit hex string (hexin) to an UINT8 referenced
 * by byteout.
 * Return != 0 if the was a problem (invalid input)
 */
static __inline int acpi_wmi_hex_to_int(const UINT8 *hexin, UINT8 *byteout)
{
	unsigned int hi;
	unsigned int lo;

	hi = hexin[0];
	lo = hexin[1];
	if ('0' <= hi && hi <= '9')
		hi -= '0';
	else if ('A' <= hi && hi <= 'F')
		hi -= ('A' - 10);
	else if ('a' <= hi && hi <= 'f')
		hi -= ('a' - 10);
	else
		return (1);
	if ('0' <= lo && lo <= '9')
		lo -= '0';
	else if ('A' <= lo && lo <= 'F')
		lo -= ('A' - 10);
	else if ('a' <= lo && lo <= 'f')
		lo -= ('a' - 10);
	else
		return (1);
	*byteout = (hi << 4) + lo;

	return (0);
}

/*
 * Convert a human readable 36 character GUID into a 16byte
 * machine readable one.
 * The basic algorithm looks as follows:
 * Input:  AABBCCDD-EEFF-GGHH-IIJJ-KKLLMMNNOOPP
 * Output: DCBAFEHGIJKLMNOP
 * (AA BB CC etc. represent two digit hex numbers == bytes)
 * Return != 0 if passed guid string is invalid
 */
static int
acpi_wmi_guid_string_to_guid(const UINT8 *guid_string, UINT8 *guid)
{
	static const int mapping[20] = {3, 2, 1, 0, -1, 5, 4, -1, 7, 6, -1,
	    8, 9, -1, 10, 11, 12, 13, 14, 15};
	int i;

	for (i = 0; i < 20; ++i, ++guid_string) {
		if (mapping[i] >= 0) {
			if (acpi_wmi_hex_to_int(guid_string,
			    &guid[mapping[i]]))
				return (-1);
			++guid_string;
		} else if (*guid_string != '-')
			return (-1);
	}

	return (0);
}

/*
 * Lookup a wmi_info structure in wmi_list based on a
 * human readable GUID
 * Return NULL if the GUID is unknown in the _WDG
 */
static struct wmi_info*
acpi_wmi_lookup_wmi_info_by_guid_string(struct acpi_wmi_softc *sc, const char *guid_string)
{
	char guid[16];
	struct wmi_info *winfo;

	ACPI_FUNCTION_TRACE((char *)(uintptr_t)__func__);

	ACPI_SERIAL_ASSERT(acpi_wmi);

	if (!acpi_wmi_guid_string_to_guid(guid_string, guid)) {
		TAILQ_FOREACH(winfo, &sc->wmi_info_list, wmi_list) {
			if (!memcmp(winfo->ginfo.guid, guid, 16)) {
				return (winfo);
			}
		}
	}

	return (NULL);
}

/*
 * open wmistat device
 */
static int
acpi_wmi_wmistat_open(struct cdev* dev, int flags, int mode, struct thread *td)
{
	struct acpi_wmi_softc *sc;
	int ret;

	if (dev == NULL || dev->si_drv1 == NULL)
		return (EBADF);
	sc = dev->si_drv1;

	ACPI_SERIAL_BEGIN(acpi_wmi);
	if (sc->wmistat_open_pid != 0) {
		ret = EBUSY;
	}
	else {
		if (sbuf_new(&sc->wmistat_sbuf, NULL, 4096, SBUF_AUTOEXTEND)
			    == NULL) {
			ret = ENXIO;
		} else {
			sc->wmistat_open_pid = td->td_proc->p_pid;
			sc->wmistat_bufptr = 0;
			ret = 0;
		}
	}
	ACPI_SERIAL_END(acpi_wmi);

	return (ret);
}

/*
 * close wmistat device
 */
static int
acpi_wmi_wmistat_close(struct cdev* dev, int flags, int mode,
    struct thread *td)
{
	struct acpi_wmi_softc *sc;
	int ret;

	if (dev == NULL || dev->si_drv1 == NULL)
		return (EBADF);
	sc = dev->si_drv1;

	ACPI_SERIAL_BEGIN(acpi_wmi);
	if (sc->wmistat_open_pid == 0) {
		ret = EBADF;
	}
	else {
		if (sc->wmistat_bufptr != -1) {
			sbuf_delete(&sc->wmistat_sbuf);
			sc->wmistat_bufptr = -1;
		}
		sc->wmistat_open_pid = 0;
		ret = 0;
	}
	ACPI_SERIAL_END(acpi_wmi);

	return (ret);
}

/*
 * Read from wmistat guid information
 */
static int
acpi_wmi_wmistat_read(struct cdev *dev, struct uio *buf, int flag)
{
	struct acpi_wmi_softc *sc;
	struct wmi_info *winfo;
	int l;
	int ret;
	UINT8* guid;

	if (dev == NULL || dev->si_drv1 == NULL)
		return (EBADF);
	sc = dev->si_drv1;
	
	ACPI_SERIAL_BEGIN(acpi_wmi);
	if (sc->wmistat_open_pid != buf->uio_td->td_proc->p_pid ||
			sc->wmistat_bufptr == -1) {
		ret = EBADF;
	}
	else {
		if (!sbuf_done(&sc->wmistat_sbuf)) {
			sbuf_printf(&sc->wmistat_sbuf, "GUID                 "
				    "                 INST EXPE METH STR "
				    "EVENT OID\n");
			TAILQ_FOREACH(winfo, &sc->wmi_info_list, wmi_list) {
				guid = (UINT8*)winfo->ginfo.guid;
				sbuf_printf(&sc->wmistat_sbuf,
					    "{%02X%02X%02X%02X-%02X%02X-"
					    "%02X%02X-%02X%02X-%02X%02X"
					    "%02X%02X%02X%02X} %3d %-5s",
					guid[3], guid[2], guid[1], guid[0],
					guid[5], guid[4],
					guid[7], guid[6],
					guid[8], guid[9],
					guid[10], guid[11], guid[12],
					guid[13], guid[14], guid[15],
					winfo->ginfo.max_instance,
					(winfo->ginfo.flags&
						ACPI_WMI_REGFLAG_EXPENSIVE)?
						"YES":"NO"
					);
				if (winfo->ginfo.flags&ACPI_WMI_REGFLAG_METHOD)
					sbuf_printf(&sc->wmistat_sbuf,
						    "WM%c%c ",
						    winfo->ginfo.oid[0],
						    winfo->ginfo.oid[1]);
				else
					sbuf_printf(&sc->wmistat_sbuf, "NO   ");
				sbuf_printf(&sc->wmistat_sbuf, "%-4s",
					    (winfo->ginfo.flags&
					    ACPI_WMI_REGFLAG_STRING)?"YES":"NO"
					);
				if (winfo->ginfo.flags&ACPI_WMI_REGFLAG_EVENT)
					sbuf_printf(&sc->wmistat_sbuf,
						    "0x%02X%s -\n",
						    (UINT8)winfo->ginfo.oid[0],
						    winfo->event_handler==NULL?
						    " ":"+");
				else
					sbuf_printf(&sc->wmistat_sbuf,
						    "NO    %c%c\n",
						    winfo->ginfo.oid[0],
						    winfo->ginfo.oid[1]);
			}
			sbuf_finish(&sc->wmistat_sbuf);
		}
		if (sbuf_len(&sc->wmistat_sbuf) <= 0) {
			sbuf_delete(&sc->wmistat_sbuf);
			sc->wmistat_bufptr = -1;
			sc->wmistat_open_pid = 0;
			ret = ENOMEM;
		} else {
			l = min(buf->uio_resid, sbuf_len(&sc->wmistat_sbuf) -
				    sc->wmistat_bufptr);
			ret = (l > 0)?uiomove(sbuf_data(&sc->wmistat_sbuf) +
				    sc->wmistat_bufptr, l, buf) : 0;
			sc->wmistat_bufptr += l;
		}
	}
	ACPI_SERIAL_END(acpi_wmi);

	return (ret);
}
