/*-
 * Copyright (c) 2003 Hiroyuki Aizu <aizu@navi.org>
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_acpi.h"
#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/bus.h>

#include <contrib/dev/acpica/include/acpi.h>

#include <dev/acpica/acpivar.h>

#define _COMPONENT	ACPI_OEM
ACPI_MODULE_NAME("Toshiba")

/*
 * Toshiba HCI interface definitions
 *
 * HCI is Toshiba's "Hardware Control Interface" which is supposed to
 * be uniform across all their models.  Ideally we would just call
 * dedicated ACPI methods instead of using this primitive interface.
 * However, the ACPI methods seem to be incomplete in some areas (for
 * example they allow setting, but not reading, the LCD brightness
 * value), so this is still useful.
 */

#define METHOD_HCI		"GHCI"
#define METHOD_HCI_ENABLE	"ENAB"
#define METHOD_VIDEO		"DSSX"

/* Operations */
#define HCI_SET				0xFF00
#define HCI_GET				0xFE00

/* Return codes */
#define HCI_SUCCESS			0x0000
#define HCI_FAILURE			0x1000
#define HCI_NOT_SUPPORTED		0x8000
#define HCI_EMPTY			0x8C00

/* Functions */
#define HCI_REG_LCD_BACKLIGHT		0x0002
#define HCI_REG_FAN			0x0004
#define HCI_REG_SYSTEM_EVENT		0x0016
#define HCI_REG_VIDEO_OUTPUT		0x001C
#define HCI_REG_HOTKEY_EVENT		0x001E
#define HCI_REG_LCD_BRIGHTNESS		0x002A
#define HCI_REG_CPU_SPEED		0x0032

/* Field definitions */
#define HCI_FAN_SHIFT			7
#define HCI_LCD_BRIGHTNESS_BITS		3
#define HCI_LCD_BRIGHTNESS_SHIFT	(16 - HCI_LCD_BRIGHTNESS_BITS)
#define HCI_LCD_BRIGHTNESS_MAX		((1 << HCI_LCD_BRIGHTNESS_BITS) - 1)
#define HCI_VIDEO_OUTPUT_FLAG		0x0100
#define HCI_VIDEO_OUTPUT_LCD		0x1
#define HCI_VIDEO_OUTPUT_CRT		0x2
#define HCI_VIDEO_OUTPUT_TV		0x4
#define HCI_CPU_SPEED_BITS		3
#define HCI_CPU_SPEED_SHIFT		(16 - HCI_CPU_SPEED_BITS)
#define HCI_CPU_SPEED_MAX		((1 << HCI_CPU_SPEED_BITS) - 1)

/* Key press/release events. */
#define FN_F1_PRESS	0x013B
#define FN_F1_RELEASE	0x01BB
#define FN_F2_PRESS	0x013C
#define FN_F2_RELEASE	0x01BC
#define FN_F3_PRESS	0x013D
#define FN_F3_RELEASE	0x01BD
#define FN_F4_PRESS	0x013E
#define FN_F4_RELEASE	0x01BE
#define FN_F5_PRESS	0x013F
#define FN_F5_RELEASE	0x01BF
#define FN_F6_PRESS	0x0140
#define FN_F6_RELEASE	0x01C0
#define FN_F7_PRESS	0x0141
#define FN_F7_RELEASE	0x01C1
#define FN_F8_PRESS	0x0142
#define FN_F8_RELEASE	0x01C2
#define FN_F9_PRESS	0x0143
#define FN_F9_RELEASE	0x01C3
#define FN_BS_PRESS	0x010E
#define FN_BS_RELEASE	0x018E
#define FN_ESC_PRESS	0x0101
#define FN_ESC_RELEASE	0x0181
#define FN_KNJ_PRESS	0x0129
#define FN_KNJ_RELEASE	0x01A9

/* HCI register definitions. */
#define HCI_WORDS	6		/* Number of registers */
#define HCI_REG_AX	0		/* Operation, then return value */
#define HCI_REG_BX	1		/* Function */
#define HCI_REG_CX	2		/* Argument (in or out) */
#define HCI_REG_DX	3		/* Unused? */
#define HCI_REG_SI	4		/* Unused? */
#define HCI_REG_DI	5		/* Unused? */

struct acpi_toshiba_softc {
	device_t	dev;
	ACPI_HANDLE	handle;
	ACPI_HANDLE	video_handle;
	struct		sysctl_ctx_list sysctl_ctx;
	struct		sysctl_oid *sysctl_tree;
};

/* Prototype for HCI functions for getting/setting a value. */
typedef int	hci_fn_t(ACPI_HANDLE, int, UINT32 *);

static int	acpi_toshiba_probe(device_t dev);
static int	acpi_toshiba_attach(device_t dev);
static int	acpi_toshiba_detach(device_t dev);
static int	acpi_toshiba_sysctl(SYSCTL_HANDLER_ARGS);
static hci_fn_t	hci_force_fan;
static hci_fn_t	hci_video_output;
static hci_fn_t	hci_lcd_brightness;
static hci_fn_t	hci_lcd_backlight;
static hci_fn_t	hci_cpu_speed;
static int	hci_call(ACPI_HANDLE h, int op, int function, UINT32 *arg);
static void	hci_key_action(struct acpi_toshiba_softc *sc, ACPI_HANDLE h,
		    UINT32 key);
static void	acpi_toshiba_notify(ACPI_HANDLE h, UINT32 notify,
		    void *context);
static int	acpi_toshiba_video_probe(device_t dev);
static int	acpi_toshiba_video_attach(device_t dev);

ACPI_SERIAL_DECL(toshiba, "ACPI Toshiba Extras");

/* Table of sysctl names and HCI functions to call. */
static struct {
	char		*name;
	hci_fn_t	*handler;
} sysctl_table[] = {
	/* name,		handler */
	{"force_fan",		hci_force_fan},
	{"video_output",	hci_video_output},
	{"lcd_brightness",	hci_lcd_brightness},
	{"lcd_backlight",	hci_lcd_backlight},
	{"cpu_speed",		hci_cpu_speed},
	{NULL, NULL}
};

static device_method_t acpi_toshiba_methods[] = {
	DEVMETHOD(device_probe,		acpi_toshiba_probe),
	DEVMETHOD(device_attach,	acpi_toshiba_attach),
	DEVMETHOD(device_detach,	acpi_toshiba_detach),

	DEVMETHOD_END
};

static driver_t acpi_toshiba_driver = {
	"acpi_toshiba",
	acpi_toshiba_methods,
	sizeof(struct acpi_toshiba_softc),
};

static devclass_t acpi_toshiba_devclass;
DRIVER_MODULE(acpi_toshiba, acpi, acpi_toshiba_driver, acpi_toshiba_devclass,
    0, 0);
MODULE_DEPEND(acpi_toshiba, acpi, 1, 1, 1);

static device_method_t acpi_toshiba_video_methods[] = {
	DEVMETHOD(device_probe,		acpi_toshiba_video_probe),
	DEVMETHOD(device_attach,	acpi_toshiba_video_attach),

	DEVMETHOD_END
};

static driver_t acpi_toshiba_video_driver = {
	"acpi_toshiba_video",
	acpi_toshiba_video_methods,
	0,
};

static devclass_t acpi_toshiba_video_devclass;
DRIVER_MODULE(acpi_toshiba_video, acpi, acpi_toshiba_video_driver,
    acpi_toshiba_video_devclass, 0, 0);
MODULE_DEPEND(acpi_toshiba_video, acpi, 1, 1, 1);

static int	enable_fn_keys = 1;
TUNABLE_INT("hw.acpi.toshiba.enable_fn_keys", &enable_fn_keys);

/*
 * HID      Model
 * -------------------------------------
 * TOS6200  Libretto L Series
 *          Dynabook Satellite 2455
 *          Dynabook SS 3500
 * TOS6207  Dynabook SS2110 Series
 * TOS6208  SPA40
 */
static int
acpi_toshiba_probe(device_t dev)
{
	static char *tosh_ids[] = { "TOS6200", "TOS6207", "TOS6208", NULL };
	int rv;

	if (acpi_disabled("toshiba") ||
	    device_get_unit(dev) != 0)
		return (ENXIO);
	rv = ACPI_ID_PROBE(device_get_parent(dev), dev, tosh_ids, NULL);
	if (rv <= 0)
		device_set_desc(dev, "Toshiba HCI Extras");
	return (rv);
}

static int
acpi_toshiba_attach(device_t dev)
{
	struct		acpi_toshiba_softc *sc;
	struct		acpi_softc *acpi_sc;
	ACPI_STATUS	status;
	int		i;

	sc = device_get_softc(dev);
	sc->dev = dev;
	sc->handle = acpi_get_handle(dev);

	acpi_sc = acpi_device_get_parent_softc(dev);
	sysctl_ctx_init(&sc->sysctl_ctx);
	sc->sysctl_tree = SYSCTL_ADD_NODE(&sc->sysctl_ctx,
	    SYSCTL_CHILDREN(acpi_sc->acpi_sysctl_tree), OID_AUTO,
	    "toshiba", CTLFLAG_RD, 0, "");

	for (i = 0; sysctl_table[i].name != NULL; i++) {
		SYSCTL_ADD_PROC(&sc->sysctl_ctx,
		    SYSCTL_CHILDREN(sc->sysctl_tree), OID_AUTO,
		    sysctl_table[i].name,
		    CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_ANYBODY,
		    sc, i, acpi_toshiba_sysctl, "I", "");
	}

	if (enable_fn_keys != 0) {
		status = AcpiEvaluateObject(sc->handle, METHOD_HCI_ENABLE,
					    NULL, NULL);
		if (ACPI_FAILURE(status)) {
			device_printf(dev, "enable FN keys failed\n");
			sysctl_ctx_free(&sc->sysctl_ctx);
			return (ENXIO);
		}
		AcpiInstallNotifyHandler(sc->handle, ACPI_DEVICE_NOTIFY,
					 acpi_toshiba_notify, sc);
	}

	return (0);
}

static int
acpi_toshiba_detach(device_t dev)
{
	struct		acpi_toshiba_softc *sc;

	sc = device_get_softc(dev);
	if (enable_fn_keys != 0) {
		AcpiRemoveNotifyHandler(sc->handle, ACPI_DEVICE_NOTIFY,
					acpi_toshiba_notify);
	}
	sysctl_ctx_free(&sc->sysctl_ctx);

	return (0);
}

static int
acpi_toshiba_sysctl(SYSCTL_HANDLER_ARGS)
{
	struct		acpi_toshiba_softc *sc;
	UINT32		arg;
	int		function, error = 0;
	hci_fn_t	*handler;

	sc = (struct acpi_toshiba_softc *)oidp->oid_arg1;
	function = oidp->oid_arg2;
	handler = sysctl_table[function].handler;

	/* Get the current value from the appropriate function. */
	ACPI_SERIAL_BEGIN(toshiba);
	error = handler(sc->handle, HCI_GET, &arg);
	if (error != 0)
		goto out;

	/* Send the current value to the user and return if no new value. */
	error = sysctl_handle_int(oidp, &arg, 0, req);
	if (error != 0 || req->newptr == NULL)
		goto out;

	/* Set the new value via the appropriate function. */
	error = handler(sc->handle, HCI_SET, &arg);

out:
	ACPI_SERIAL_END(toshiba);
	return (error);
}

static int
hci_force_fan(ACPI_HANDLE h, int op, UINT32 *state)
{
	int		ret;

	ACPI_SERIAL_ASSERT(toshiba);
	if (op == HCI_SET) {
		if (*state > 1)
			return (EINVAL);
		*state <<= HCI_FAN_SHIFT;
	}
	ret = hci_call(h, op, HCI_REG_FAN, state);
	if (ret == 0 && op == HCI_GET)
		*state >>= HCI_FAN_SHIFT;
	return (ret);
}

static int
hci_video_output(ACPI_HANDLE h, int op, UINT32 *video_output)
{
	int		ret;
	ACPI_STATUS	status;

	ACPI_SERIAL_ASSERT(toshiba);
	if (op == HCI_SET) {
		if (*video_output < 1 || *video_output > 7)
			return (EINVAL);
		if (h == NULL)
			return (ENXIO);
		*video_output |= HCI_VIDEO_OUTPUT_FLAG;
		status = acpi_SetInteger(h, METHOD_VIDEO, *video_output);
		if (ACPI_SUCCESS(status))
			ret = 0;
		else
			ret = ENXIO;
	} else {
		ret = hci_call(h, op, HCI_REG_VIDEO_OUTPUT, video_output);
		if (ret == 0)
			*video_output &= 0xff;
	}

	return (ret);
}

static int
hci_lcd_brightness(ACPI_HANDLE h, int op, UINT32 *brightness)
{
	int		ret;

	ACPI_SERIAL_ASSERT(toshiba);
	if (op == HCI_SET) {
		if (*brightness > HCI_LCD_BRIGHTNESS_MAX)
			return (EINVAL);
		*brightness <<= HCI_LCD_BRIGHTNESS_SHIFT;
	}
	ret = hci_call(h, op, HCI_REG_LCD_BRIGHTNESS, brightness);
	if (ret == 0 && op == HCI_GET)
		*brightness >>= HCI_LCD_BRIGHTNESS_SHIFT;
	return (ret);
}

static int
hci_lcd_backlight(ACPI_HANDLE h, int op, UINT32 *backlight)
{

	ACPI_SERIAL_ASSERT(toshiba);
	if (op == HCI_SET) {
		if (*backlight > 1)
			return (EINVAL);
	}
	return (hci_call(h, op, HCI_REG_LCD_BACKLIGHT, backlight));
}

static int
hci_cpu_speed(ACPI_HANDLE h, int op, UINT32 *speed)
{
	int		ret;

	ACPI_SERIAL_ASSERT(toshiba);
	if (op == HCI_SET) {
		if (*speed > HCI_CPU_SPEED_MAX)
			return (EINVAL);
		*speed <<= HCI_CPU_SPEED_SHIFT;
	}
	ret = hci_call(h, op, HCI_REG_CPU_SPEED, speed);
	if (ret == 0 && op == HCI_GET)
		*speed >>= HCI_CPU_SPEED_SHIFT;
	return (ret);
}

static int
hci_call(ACPI_HANDLE h, int op, int function, UINT32 *arg)
{
	ACPI_OBJECT_LIST args;
	ACPI_BUFFER	results;
	ACPI_OBJECT	obj[HCI_WORDS];
	ACPI_OBJECT	*res;
	int		status, i, ret;

	ACPI_SERIAL_ASSERT(toshiba);
	status = ENXIO;

	for (i = 0; i < HCI_WORDS; i++) {
		obj[i].Type = ACPI_TYPE_INTEGER;
		obj[i].Integer.Value = 0;
	}
	obj[HCI_REG_AX].Integer.Value = op;
	obj[HCI_REG_BX].Integer.Value = function;
	if (op == HCI_SET)
		obj[HCI_REG_CX].Integer.Value = *arg;

	args.Count = HCI_WORDS;
	args.Pointer = obj;
	results.Pointer = NULL;
	results.Length = ACPI_ALLOCATE_BUFFER;
	if (ACPI_FAILURE(AcpiEvaluateObject(h, METHOD_HCI, &args, &results)))
		goto end;
	res = (ACPI_OBJECT *)results.Pointer;
	if (!ACPI_PKG_VALID(res, HCI_WORDS)) {
		printf("toshiba: invalid package!\n");
		return (ENXIO);
	}

	acpi_PkgInt32(res, HCI_REG_AX, &ret);
	if (ret == HCI_SUCCESS) {
		if (op == HCI_GET)
			acpi_PkgInt32(res, HCI_REG_CX, arg);
		status = 0;
	} else if (function == HCI_REG_SYSTEM_EVENT && op == HCI_GET &&
	    ret == HCI_NOT_SUPPORTED) {
		/*
		 * Sometimes system events are disabled without us requesting
		 * it.  This workaround attempts to re-enable them.
		 *
		 * XXX This call probably shouldn't be recursive.  Queueing
		 * a task via AcpiOsQueueForExecution() might be better.
		 */
		 i = 1;
		 hci_call(h, HCI_SET, HCI_REG_SYSTEM_EVENT, &i);
	}

end:
	if (results.Pointer != NULL)
		AcpiOsFree(results.Pointer);

	return (status);
}

/*
 * Perform a few actions based on the keypress.  Users can extend this
 * functionality by reading the keystrokes we send to devd(8).
 */
static void
hci_key_action(struct acpi_toshiba_softc *sc, ACPI_HANDLE h, UINT32 key)
{
	UINT32		arg;

	ACPI_SERIAL_ASSERT(toshiba);
	switch (key) {
	case FN_F6_RELEASE:
		/* Decrease LCD brightness. */
		hci_lcd_brightness(h, HCI_GET, &arg);
		if (arg-- == 0)
			arg = 0;
		else
			hci_lcd_brightness(h, HCI_SET, &arg);
		break;
	case FN_F7_RELEASE:
		/* Increase LCD brightness. */
		hci_lcd_brightness(h, HCI_GET, &arg);
		if (arg++ == 7)
			arg = 7;
		else
			hci_lcd_brightness(h, HCI_SET, &arg);
		break;
	case FN_F5_RELEASE:
		/* Cycle through video outputs. */
		hci_video_output(h, HCI_GET, &arg);
		arg = (arg + 1) % 7;
		hci_video_output(sc->video_handle, HCI_SET, &arg);
		break;
	case FN_F8_RELEASE:
		/* Toggle LCD backlight. */
		hci_lcd_backlight(h, HCI_GET, &arg);
		arg = (arg != 0) ? 0 : 1;
		hci_lcd_backlight(h, HCI_SET, &arg);
		break;
	case FN_ESC_RELEASE:
		/* Toggle forcing fan on. */
		hci_force_fan(h, HCI_GET, &arg);
		arg = (arg != 0) ? 0 : 1;
		hci_force_fan(h, HCI_SET, &arg);
		break;
	}
}

static void
acpi_toshiba_notify(ACPI_HANDLE h, UINT32 notify, void *context)
{
	struct		acpi_toshiba_softc *sc;
	UINT32		key;

	sc = (struct acpi_toshiba_softc *)context;

	if (notify == 0x80) {
		ACPI_SERIAL_BEGIN(toshiba);
		while (hci_call(h, HCI_GET, HCI_REG_SYSTEM_EVENT, &key) == 0) {
			hci_key_action(sc, h, key);
			acpi_UserNotify("TOSHIBA", h, (uint8_t)key);
		}
		ACPI_SERIAL_END(toshiba);
	} else
		device_printf(sc->dev, "unknown notify: 0x%x\n", notify);
}

/*
 * Toshiba video pseudo-device to provide the DSSX method.
 *
 * HID      Model
 * -------------------------------------
 * TOS6201  Libretto L Series
 */
static int
acpi_toshiba_video_probe(device_t dev)
{
	static char *vid_ids[] = { "TOS6201", NULL };
	int rv;

	if (acpi_disabled("toshiba") ||
	    device_get_unit(dev) != 0)
		return (ENXIO);

	device_quiet(dev);
	rv = ACPI_ID_PROBE(device_get_parent(dev), dev, vid_ids, NULL);
	if (rv <= 0)
		device_set_desc(dev, "Toshiba Video");
	return (rv);
}

static int
acpi_toshiba_video_attach(device_t dev)
{
	struct		acpi_toshiba_softc *sc;

	sc = devclass_get_softc(acpi_toshiba_devclass, 0);
	if (sc == NULL)
		return (ENXIO);
	sc->video_handle = acpi_get_handle(dev);
	return (0);
}
