/*-
 * Copyright (c) 2012 Alexander Motin <mav@FreeBSD.org>
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
#include <sys/conf.h>
#include <sys/uio.h>
#include <sys/proc.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/sbuf.h>
#include <sys/module.h>
#include <sys/sysctl.h>

#include <contrib/dev/acpica/include/acpi.h>
#include <contrib/dev/acpica/include/accommon.h>
#include <dev/acpica/acpivar.h>
#include "acpi_wmi_if.h"

#define _COMPONENT	ACPI_OEM
ACPI_MODULE_NAME("ASUS-WMI")

#define ACPI_ASUS_WMI_MGMT_GUID 	"97845ED0-4E6D-11DE-8A39-0800200C9A66"
#define ACPI_ASUS_WMI_EVENT_GUID	"0B3CBB35-E3C2-45ED-91C2-4C5A6D195D1C"
#define ACPI_EEEPC_WMI_EVENT_GUID	"ABBC0F72-8EA1-11D1-00A0-C90629100000"

/* WMI Methods */
#define ASUS_WMI_METHODID_SPEC          0x43455053
#define ASUS_WMI_METHODID_SFUN          0x4E554653
#define ASUS_WMI_METHODID_DSTS          0x53544344
#define ASUS_WMI_METHODID_DSTS2         0x53545344
#define ASUS_WMI_METHODID_DEVS          0x53564544
#define ASUS_WMI_METHODID_INIT          0x54494E49
#define ASUS_WMI_METHODID_HKEY          0x59454B48

#define ASUS_WMI_UNSUPPORTED_METHOD     0xFFFFFFFE

/* Wireless */
#define ASUS_WMI_DEVID_HW_SWITCH        0x00010001
#define ASUS_WMI_DEVID_WIRELESS_LED     0x00010002
#define ASUS_WMI_DEVID_CWAP             0x00010003
#define ASUS_WMI_DEVID_WLAN             0x00010011
#define ASUS_WMI_DEVID_BLUETOOTH        0x00010013
#define ASUS_WMI_DEVID_GPS              0x00010015
#define ASUS_WMI_DEVID_WIMAX            0x00010017
#define ASUS_WMI_DEVID_WWAN3G           0x00010019
#define ASUS_WMI_DEVID_UWB              0x00010021

/* LEDs */
#define ASUS_WMI_DEVID_LED1             0x00020011
#define ASUS_WMI_DEVID_LED2             0x00020012
#define ASUS_WMI_DEVID_LED3             0x00020013
#define ASUS_WMI_DEVID_LED4             0x00020014
#define ASUS_WMI_DEVID_LED5             0x00020015
#define ASUS_WMI_DEVID_LED6             0x00020016

/* Backlight and Brightness */
#define ASUS_WMI_DEVID_BACKLIGHT        0x00050011
#define ASUS_WMI_DEVID_BRIGHTNESS       0x00050012
#define ASUS_WMI_DEVID_KBD_BACKLIGHT    0x00050021
#define ASUS_WMI_DEVID_LIGHT_SENSOR     0x00050022

/* Misc */
#define ASUS_WMI_DEVID_CAMERA           0x00060013
#define ASUS_WMI_DEVID_CARDREADER       0x00080013
#define ASUS_WMI_DEVID_TOUCHPAD         0x00100011
#define ASUS_WMI_DEVID_TOUCHPAD_LED     0x00100012
#define ASUS_WMI_DEVID_THERMAL_CTRL     0x00110011
#define ASUS_WMI_DEVID_FAN_CTRL         0x00110012
#define ASUS_WMI_DEVID_PROCESSOR_STATE  0x00120012

/* DSTS masks */
#define ASUS_WMI_DSTS_STATUS_BIT        0x00000001
#define ASUS_WMI_DSTS_UNKNOWN_BIT       0x00000002
#define ASUS_WMI_DSTS_PRESENCE_BIT      0x00010000
#define ASUS_WMI_DSTS_USER_BIT          0x00020000
#define ASUS_WMI_DSTS_BIOS_BIT          0x00040000
#define ASUS_WMI_DSTS_BRIGHTNESS_MASK   0x000000FF
#define ASUS_WMI_DSTS_MAX_BRIGTH_MASK   0x0000FF00


struct acpi_asus_wmi_softc {
	device_t	dev;
	device_t	wmi_dev;
	const char	*notify_guid;
	struct sysctl_ctx_list	*sysctl_ctx;
	struct sysctl_oid	*sysctl_tree;
	int		dsts_id;
	int		handle_keys;
};

static struct {
	char	*name;
	int	dev_id;
	char	*description;
	int	flag_rdonly;
} acpi_asus_wmi_sysctls[] = {
	{
		.name		= "hw_switch",
		.dev_id		= ASUS_WMI_DEVID_HW_SWITCH,
		.description	= "hw_switch",
	},
	{
		.name		= "wireless_led",
		.dev_id		= ASUS_WMI_DEVID_WIRELESS_LED,
		.description	= "Wireless LED control",
	},
	{
		.name		= "cwap",
		.dev_id		= ASUS_WMI_DEVID_CWAP,
		.description	= "Alt+F2 function",
	},
	{
		.name		= "wlan",
		.dev_id		= ASUS_WMI_DEVID_WLAN,
		.description	= "WLAN power control",
	},
	{
		.name		= "bluetooth",
		.dev_id		= ASUS_WMI_DEVID_BLUETOOTH,
		.description	= "Bluetooth power control",
	},
	{
		.name		= "gps",
		.dev_id		= ASUS_WMI_DEVID_GPS,
		.description	= "GPS power control",
	},
	{
		.name		= "wimax",
		.dev_id		= ASUS_WMI_DEVID_WIMAX,
		.description	= "WiMAX power control",
	},
	{
		.name		= "wwan3g",
		.dev_id		= ASUS_WMI_DEVID_WWAN3G,
		.description	= "WWAN-3G power control",
	},
	{
		.name		= "uwb",
		.dev_id		= ASUS_WMI_DEVID_UWB,
		.description	= "UWB power control",
	},
	{
		.name		= "led1",
		.dev_id		= ASUS_WMI_DEVID_LED1,
		.description	= "LED1 control",
	},
	{
		.name		= "led2",
		.dev_id		= ASUS_WMI_DEVID_LED2,
		.description	= "LED2 control",
	},
	{
		.name		= "led3",
		.dev_id		= ASUS_WMI_DEVID_LED3,
		.description	= "LED3 control",
	},
	{
		.name		= "led4",
		.dev_id		= ASUS_WMI_DEVID_LED4,
		.description	= "LED4 control",
	},
	{
		.name		= "led5",
		.dev_id		= ASUS_WMI_DEVID_LED5,
		.description	= "LED5 control",
	},
	{
		.name		= "led6",
		.dev_id		= ASUS_WMI_DEVID_LED6,
		.description	= "LED6 control",
	},
	{
		.name		= "backlight",
		.dev_id		= ASUS_WMI_DEVID_BACKLIGHT,
		.description	= "LCD backlight on/off control",
	},
	{
		.name		= "brightness",
		.dev_id		= ASUS_WMI_DEVID_BRIGHTNESS,
		.description	= "LCD backlight brightness control",
	},
	{
		.name		= "kbd_backlight",
		.dev_id		= ASUS_WMI_DEVID_KBD_BACKLIGHT,
		.description	= "Keyboard backlight brightness control",
	},
	{
		.name		= "light_sensor",
		.dev_id		= ASUS_WMI_DEVID_LIGHT_SENSOR,
		.description	= "Ambient light sensor",
	},
	{
		.name		= "camera",
		.dev_id		= ASUS_WMI_DEVID_CAMERA,
		.description	= "Camera power control",
	},
	{
		.name		= "cardreader",
		.dev_id		= ASUS_WMI_DEVID_CARDREADER,
		.description	= "Cardreader power control",
	},
	{
		.name		= "touchpad",
		.dev_id		= ASUS_WMI_DEVID_TOUCHPAD,
		.description	= "Touchpad control",
	},
	{
		.name		= "touchpad_led",
		.dev_id		= ASUS_WMI_DEVID_TOUCHPAD_LED,
		.description	= "Touchpad LED control",
	},
	{
		.name		= "themperature",
		.dev_id		= ASUS_WMI_DEVID_THERMAL_CTRL,
		.description	= "Temperature (C)",
		.flag_rdonly	= 1
	},
	{
		.name		= "fan_speed",
		.dev_id		= ASUS_WMI_DEVID_FAN_CTRL,
		.description	= "Fan speed (0-3)",
		.flag_rdonly	= 1
	},
	{
		.name		= "processor_state",
		.dev_id		= ASUS_WMI_DEVID_PROCESSOR_STATE,
		.flag_rdonly	= 1
	},
	{ NULL, 0, NULL, 0 }
};

ACPI_SERIAL_DECL(asus_wmi, "ASUS WMI device");

static void	acpi_asus_wmi_identify(driver_t *driver, device_t parent);
static int	acpi_asus_wmi_probe(device_t dev);
static int	acpi_asus_wmi_attach(device_t dev);
static int	acpi_asus_wmi_detach(device_t dev);

static int	acpi_asus_wmi_sysctl(SYSCTL_HANDLER_ARGS);
static int	acpi_asus_wmi_sysctl_set(struct acpi_asus_wmi_softc *sc, int dev_id,
		    int arg, int oldarg);
static int	acpi_asus_wmi_sysctl_get(struct acpi_asus_wmi_softc *sc, int dev_id);
static int	acpi_asus_wmi_evaluate_method(device_t wmi_dev, int method,
		    UINT32 arg0, UINT32 arg1, UINT32 *retval);
static int	acpi_wpi_asus_get_devstate(struct acpi_asus_wmi_softc *sc,
		    UINT32 dev_id, UINT32 *retval);
static int	acpi_wpi_asus_set_devstate(struct acpi_asus_wmi_softc *sc,
		    UINT32 dev_id, UINT32 ctrl_param, UINT32 *retval);
static void	acpi_asus_wmi_notify(ACPI_HANDLE h, UINT32 notify, void *context);

static device_method_t acpi_asus_wmi_methods[] = {
	DEVMETHOD(device_identify, acpi_asus_wmi_identify),
	DEVMETHOD(device_probe, acpi_asus_wmi_probe),
	DEVMETHOD(device_attach, acpi_asus_wmi_attach),
	DEVMETHOD(device_detach, acpi_asus_wmi_detach),

	DEVMETHOD_END
};

static driver_t	acpi_asus_wmi_driver = {
	"acpi_asus_wmi",
	acpi_asus_wmi_methods,
	sizeof(struct acpi_asus_wmi_softc),
};

static devclass_t acpi_asus_wmi_devclass;

DRIVER_MODULE(acpi_asus_wmi, acpi_wmi, acpi_asus_wmi_driver,
    acpi_asus_wmi_devclass, 0, 0);
MODULE_DEPEND(acpi_asus_wmi, acpi_wmi, 1, 1, 1);
MODULE_DEPEND(acpi_asus_wmi, acpi, 1, 1, 1);

static void
acpi_asus_wmi_identify(driver_t *driver, device_t parent)
{

	/* Don't do anything if driver is disabled. */
	if (acpi_disabled("asus_wmi"))
		return;

	/* Add only a single device instance. */
	if (device_find_child(parent, "acpi_asus_wmi", -1) != NULL)
		return;

	/* Check management GUID to see whether system is compatible. */
	if (!ACPI_WMI_PROVIDES_GUID_STRING(parent,
	    ACPI_ASUS_WMI_MGMT_GUID))
		return;

	if (BUS_ADD_CHILD(parent, 0, "acpi_asus_wmi", -1) == NULL)
		device_printf(parent, "add acpi_asus_wmi child failed\n");
}

static int
acpi_asus_wmi_probe(device_t dev)
{

	if (!ACPI_WMI_PROVIDES_GUID_STRING(device_get_parent(dev),
	    ACPI_ASUS_WMI_MGMT_GUID))
		return (EINVAL);
	device_set_desc(dev, "ASUS WMI device");
	return (0);
}

static int
acpi_asus_wmi_attach(device_t dev)
{
	struct acpi_asus_wmi_softc	*sc;
	UINT32			val;
	int			dev_id, i;

	ACPI_FUNCTION_TRACE((char *)(uintptr_t) __func__);

	sc = device_get_softc(dev);
	sc->dev = dev;
	sc->wmi_dev = device_get_parent(dev);
	sc->handle_keys = 1;

	/* Check management GUID. */
	if (!ACPI_WMI_PROVIDES_GUID_STRING(sc->wmi_dev,
	    ACPI_ASUS_WMI_MGMT_GUID)) {
		device_printf(dev,
		    "WMI device does not provide the ASUS management GUID\n");
		return (EINVAL);
	}

	/* Find proper DSTS method. */
	sc->dsts_id = ASUS_WMI_METHODID_DSTS;
next:
	for (i = 0; acpi_asus_wmi_sysctls[i].name != NULL; ++i) {
		dev_id = acpi_asus_wmi_sysctls[i].dev_id;
		if (acpi_wpi_asus_get_devstate(sc, dev_id, &val))
			continue;
		break;
	}
	if (acpi_asus_wmi_sysctls[i].name == NULL) {
		if (sc->dsts_id == ASUS_WMI_METHODID_DSTS) {
			sc->dsts_id = ASUS_WMI_METHODID_DSTS2;
			goto next;
		} else {
			device_printf(dev, "Can not detect DSTS method ID\n");
			return (EINVAL);
		}
	}

	/* Find proper and attach to notufy GUID. */
	if (ACPI_WMI_PROVIDES_GUID_STRING(sc->wmi_dev,
	    ACPI_ASUS_WMI_EVENT_GUID))
		sc->notify_guid = ACPI_ASUS_WMI_EVENT_GUID;
	else if (ACPI_WMI_PROVIDES_GUID_STRING(sc->wmi_dev,
	    ACPI_EEEPC_WMI_EVENT_GUID))
		sc->notify_guid = ACPI_EEEPC_WMI_EVENT_GUID;
	else
		sc->notify_guid = NULL;
	if (sc->notify_guid != NULL) {
		if (ACPI_WMI_INSTALL_EVENT_HANDLER(sc->wmi_dev,
		    sc->notify_guid, acpi_asus_wmi_notify, dev))
			sc->notify_guid = NULL;
	}
	if (sc->notify_guid == NULL)
		device_printf(dev, "Could not install event handler!\n");

	/* Initialize. */
	if (!acpi_asus_wmi_evaluate_method(sc->wmi_dev,
	    ASUS_WMI_METHODID_INIT, 0, 0, &val) && bootverbose)
		device_printf(dev, "Initialization: %#x\n", val);
	if (!acpi_asus_wmi_evaluate_method(sc->wmi_dev,
	    ASUS_WMI_METHODID_SPEC, 0, 0x9, &val) && bootverbose)
		device_printf(dev, "WMI BIOS version: %d.%d\n",
		    val >> 16, val & 0xFF);
	if (!acpi_asus_wmi_evaluate_method(sc->wmi_dev,
	    ASUS_WMI_METHODID_SFUN, 0, 0, &val) && bootverbose)
		device_printf(dev, "SFUN value: %#x\n", val);

	ACPI_SERIAL_BEGIN(asus_wmi);

	sc->sysctl_ctx = device_get_sysctl_ctx(dev);
	sc->sysctl_tree = device_get_sysctl_tree(dev);
	SYSCTL_ADD_INT(sc->sysctl_ctx,
	    SYSCTL_CHILDREN(sc->sysctl_tree), OID_AUTO,
	    "handle_keys", CTLFLAG_RW, &sc->handle_keys,
	    0, "Handle some hardware keys inside the driver");
	for (i = 0; acpi_asus_wmi_sysctls[i].name != NULL; ++i) {
		dev_id = acpi_asus_wmi_sysctls[i].dev_id;
		if (acpi_wpi_asus_get_devstate(sc, dev_id, &val))
			continue;
		switch (dev_id) {
		case ASUS_WMI_DEVID_THERMAL_CTRL:
		case ASUS_WMI_DEVID_PROCESSOR_STATE:
		case ASUS_WMI_DEVID_FAN_CTRL:
		case ASUS_WMI_DEVID_BRIGHTNESS:
			if (val == 0)
				continue;
			break;
		default:
			if ((val & ASUS_WMI_DSTS_PRESENCE_BIT) == 0)
				continue;
			break;
		}

		if (acpi_asus_wmi_sysctls[i].flag_rdonly != 0) {
			SYSCTL_ADD_PROC(sc->sysctl_ctx,
			    SYSCTL_CHILDREN(sc->sysctl_tree), OID_AUTO,
			    acpi_asus_wmi_sysctls[i].name,
			    CTLTYPE_INT | CTLFLAG_RD,
			    sc, i, acpi_asus_wmi_sysctl, "I",
			    acpi_asus_wmi_sysctls[i].description);
		} else {
			SYSCTL_ADD_PROC(sc->sysctl_ctx,
			    SYSCTL_CHILDREN(sc->sysctl_tree), OID_AUTO,
			    acpi_asus_wmi_sysctls[i].name,
			    CTLTYPE_INT | CTLFLAG_RW,
			    sc, i, acpi_asus_wmi_sysctl, "I",
			    acpi_asus_wmi_sysctls[i].description);
		}
	}
	ACPI_SERIAL_END(asus_wmi);

	return (0);
}

static int
acpi_asus_wmi_detach(device_t dev)
{
	struct acpi_asus_wmi_softc *sc = device_get_softc(dev);
	
	ACPI_FUNCTION_TRACE((char *)(uintptr_t) __func__);

	if (sc->notify_guid)
		ACPI_WMI_REMOVE_EVENT_HANDLER(dev, sc->notify_guid);

	return (0);
}

static int
acpi_asus_wmi_sysctl(SYSCTL_HANDLER_ARGS)
{
	struct acpi_asus_wmi_softc	*sc;
	int			arg;
	int			oldarg;
	int			error = 0;
	int			function;
	int			dev_id;
	
	ACPI_FUNCTION_TRACE((char *)(uintptr_t)__func__);

	sc = (struct acpi_asus_wmi_softc *)oidp->oid_arg1;
	function = oidp->oid_arg2;
	dev_id = acpi_asus_wmi_sysctls[function].dev_id;

	ACPI_SERIAL_BEGIN(asus_wmi);
	arg = acpi_asus_wmi_sysctl_get(sc, dev_id);
	oldarg = arg;
	error = sysctl_handle_int(oidp, &arg, 0, req);
	if (!error && req->newptr != NULL)
		error = acpi_asus_wmi_sysctl_set(sc, dev_id, arg, oldarg);
	ACPI_SERIAL_END(asus_wmi);

	return (error);
}

static int
acpi_asus_wmi_sysctl_get(struct acpi_asus_wmi_softc *sc, int dev_id)
{
	UINT32	val = 0;

	ACPI_FUNCTION_TRACE((char *)(uintptr_t)__func__);
	ACPI_SERIAL_ASSERT(asus_wmi);

	acpi_wpi_asus_get_devstate(sc, dev_id, &val);

	switch(dev_id) {
	case ASUS_WMI_DEVID_THERMAL_CTRL:
		val = (val - 2731 + 5) / 10;
		break;
	case ASUS_WMI_DEVID_PROCESSOR_STATE:
	case ASUS_WMI_DEVID_FAN_CTRL:
		break;
	case ASUS_WMI_DEVID_BRIGHTNESS:
		val &= ASUS_WMI_DSTS_BRIGHTNESS_MASK;
		break;
	case ASUS_WMI_DEVID_KBD_BACKLIGHT:
		val &= 0x7;
		break;
	default:
		if (val & ASUS_WMI_DSTS_UNKNOWN_BIT)
			val = -1;
		else
			val = !!(val & ASUS_WMI_DSTS_STATUS_BIT);
		break;
	}

	return (val);
}

static int
acpi_asus_wmi_sysctl_set(struct acpi_asus_wmi_softc *sc, int dev_id, int arg, int oldarg)
{
	ACPI_FUNCTION_TRACE((char *)(uintptr_t)__func__);
	ACPI_SERIAL_ASSERT(asus_wmi);

	switch(dev_id) {
	case ASUS_WMI_DEVID_KBD_BACKLIGHT:
		arg = min(0x7, arg);
		if (arg != 0)
			arg |= 0x80;
		break;
	}

	acpi_wpi_asus_set_devstate(sc, dev_id, arg, NULL);

	return (0);
}

static __inline void
acpi_asus_wmi_free_buffer(ACPI_BUFFER* buf) {
	if (buf && buf->Pointer) {
		AcpiOsFree(buf->Pointer);
	}
}

static void
acpi_asus_wmi_notify(ACPI_HANDLE h, UINT32 notify, void *context)
{
	device_t dev = context;
	ACPI_FUNCTION_TRACE_U32((char *)(uintptr_t)__func__, notify);
	UINT32 val;
	int code = 0;

	struct acpi_asus_wmi_softc *sc = device_get_softc(dev);
	ACPI_BUFFER response = { ACPI_ALLOCATE_BUFFER, NULL };
	ACPI_OBJECT *obj;
	ACPI_WMI_GET_EVENT_DATA(sc->wmi_dev, notify, &response);
	obj = (ACPI_OBJECT*) response.Pointer;
	if (obj && obj->Type == ACPI_TYPE_INTEGER) {
		code = obj->Integer.Value;
		acpi_UserNotify("ASUS", ACPI_ROOT_OBJECT,
		    code);
	}
	if (code && sc->handle_keys) {
		/* Keyboard backlight control. */
		if (code == 0xc4 || code == 0xc5) {
			acpi_wpi_asus_get_devstate(sc,
			    ASUS_WMI_DEVID_KBD_BACKLIGHT, &val);
			val &= 0x7;
			if (code == 0xc4) {
				if (val < 0x7)
					val++;
			} else if (val > 0)
				val--;
			if (val != 0)
				val |= 0x80;
			acpi_wpi_asus_set_devstate(sc,
			    ASUS_WMI_DEVID_KBD_BACKLIGHT, val, NULL);
		}
		/* Touchpad control. */
		if (code == 0x6b) {
			acpi_wpi_asus_get_devstate(sc,
			    ASUS_WMI_DEVID_TOUCHPAD, &val);
			val = !(val & 1);
			acpi_wpi_asus_set_devstate(sc,
			    ASUS_WMI_DEVID_TOUCHPAD, val, NULL);
		}
	}
	acpi_asus_wmi_free_buffer(&response);
}

static int
acpi_asus_wmi_evaluate_method(device_t wmi_dev, int method,
    UINT32 arg0, UINT32 arg1, UINT32 *retval)
{
	UINT32		params[2] = { arg0, arg1 };
	UINT32		result;
	ACPI_OBJECT	*obj;
	ACPI_BUFFER	in = { sizeof(params), &params };
	ACPI_BUFFER	out = { ACPI_ALLOCATE_BUFFER, NULL };
	
	if (ACPI_FAILURE(ACPI_WMI_EVALUATE_CALL(wmi_dev,
	    ACPI_ASUS_WMI_MGMT_GUID, 1, method, &in, &out))) {
		acpi_asus_wmi_free_buffer(&out);
		return (-EINVAL);
	}
	obj = out.Pointer;
	if (obj && obj->Type == ACPI_TYPE_INTEGER)
		result = (UINT32) obj->Integer.Value;
	else
		result = 0;
	acpi_asus_wmi_free_buffer(&out);
	if (retval)
		*retval = result;
	return (result == ASUS_WMI_UNSUPPORTED_METHOD ? -ENODEV : 0);
}

static int
acpi_wpi_asus_get_devstate(struct acpi_asus_wmi_softc *sc,
    UINT32 dev_id, UINT32 *retval)
{

	return (acpi_asus_wmi_evaluate_method(sc->wmi_dev,
	    sc->dsts_id, dev_id, 0, retval));
}

static int
acpi_wpi_asus_set_devstate(struct acpi_asus_wmi_softc *sc,
    UINT32 dev_id, UINT32 ctrl_param, UINT32 *retval)
{

	return (acpi_asus_wmi_evaluate_method(sc->wmi_dev,
	    ASUS_WMI_METHODID_DEVS, dev_id, ctrl_param, retval));
}
