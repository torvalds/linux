/*	$OpenBSD: ugold.c,v 1.7 2014/12/11 18:39:27 mpi Exp $   */

/*
 * Copyright (c) 2013 Takayoshi SASANO <sasano@openbsd.org>
 * Copyright (c) 2013 Martin Pieuchot <mpi@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/* Driver for Microdia's HID based TEMPer Temperature sensor */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/stdint.h>
#include <sys/stddef.h>
#include <sys/param.h>
#include <sys/queue.h>
#include <sys/types.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/module.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/condvar.h>
#include <sys/sysctl.h>
#include <sys/sx.h>
#include <sys/unistd.h>
#include <sys/callout.h>
#include <sys/malloc.h>
#include <sys/priv.h>
#include <sys/conf.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbhid.h>
#include <dev/usb/usb_process.h>
#include <dev/usb/usbdi_util.h>
#include "usbdevs.h"

#define	USB_DEBUG_VAR usb_debug
#include <dev/usb/usb_debug.h>

#define	UGOLD_INNER		0
#define	UGOLD_OUTER		1
#define	UGOLD_MAX_SENSORS	2

#define	UGOLD_CMD_DATA		0x80
#define	UGOLD_CMD_INIT		0x82

enum {
	UGOLD_INTR_DT,
	UGOLD_N_TRANSFER,
};

/*
 * This driver only uses two of the three known commands for the
 * TEMPerV1.2 device.
 *
 * The first byte of the answer corresponds to the command and the
 * second one seems to be the size (in bytes) of the answer.
 *
 * The device always sends 8 bytes and if the length of the answer
 * is less than that, it just leaves the last bytes untouched.  That
 * is why most of the time the last n bytes of the answers are the
 * same.
 *
 * The third command below seems to generate two answers with a
 * string corresponding to the device, for example:
 *	'TEMPer1F' and '1.1Per1F' (here Per1F is repeated).
 */
static uint8_t cmd_data[8] = {0x01, 0x80, 0x33, 0x01, 0x00, 0x00, 0x00, 0x00};
static uint8_t cmd_init[8] = {0x01, 0x82, 0x77, 0x01, 0x00, 0x00, 0x00, 0x00};

#if 0
static uint8_t cmd_type[8] = {0x01, 0x86, 0xff, 0x01, 0x00, 0x00, 0x00, 0x00};

#endif

struct ugold_softc;
struct ugold_readout_msg {
	struct usb_proc_msg hdr;
	struct ugold_softc *sc;
};

struct ugold_softc {
	struct usb_device *sc_udev;
	struct usb_xfer *sc_xfer[UGOLD_N_TRANSFER];

	struct callout sc_callout;
	struct mtx sc_mtx;
	struct ugold_readout_msg sc_readout_msg[2];

	int	sc_num_sensors;
	int	sc_sensor[UGOLD_MAX_SENSORS];
  	int	sc_calib[UGOLD_MAX_SENSORS];
	int	sc_valid[UGOLD_MAX_SENSORS];
	uint8_t	sc_report_id;
	uint8_t	sc_iface_index[2];
};

/* prototypes */

static device_probe_t ugold_probe;
static device_attach_t ugold_attach;
static device_detach_t ugold_detach;

static usb_proc_callback_t ugold_readout_msg;

static usb_callback_t ugold_intr_callback;

static devclass_t ugold_devclass;

static device_method_t ugold_methods[] = {
	DEVMETHOD(device_probe, ugold_probe),
	DEVMETHOD(device_attach, ugold_attach),
	DEVMETHOD(device_detach, ugold_detach),

	DEVMETHOD_END
};

static driver_t ugold_driver = {
	.name = "ugold",
	.methods = ugold_methods,
	.size = sizeof(struct ugold_softc),
};

static const STRUCT_USB_HOST_ID ugold_devs[] = {
	{USB_VPI(USB_VENDOR_CHICONY2, USB_PRODUCT_CHICONY2_TEMPER, 0)},
};

DRIVER_MODULE(ugold, uhub, ugold_driver, ugold_devclass, NULL, NULL);
MODULE_DEPEND(ugold, usb, 1, 1, 1);
MODULE_VERSION(ugold, 1);
USB_PNP_HOST_INFO(ugold_devs);

static const struct usb_config ugold_config[UGOLD_N_TRANSFER] = {

	[UGOLD_INTR_DT] = {
		.type = UE_INTERRUPT,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_IN,
		.flags = {.pipe_bof = 1,.short_xfer_ok = 1,},
		.bufsize = 0,		/* use wMaxPacketSize */
		.callback = &ugold_intr_callback,
		.if_index = 1,
	},
};

static void
ugold_timeout(void *arg)
{
	struct ugold_softc *sc = arg;

	usb_proc_explore_lock(sc->sc_udev);
	(void)usb_proc_explore_msignal(sc->sc_udev,
	    &sc->sc_readout_msg[0], &sc->sc_readout_msg[1]);
	usb_proc_explore_unlock(sc->sc_udev);

	callout_reset(&sc->sc_callout, 6 * hz, &ugold_timeout, sc);
}

static int
ugold_probe(device_t dev)
{
	struct usb_attach_arg *uaa;

	uaa = device_get_ivars(dev);
	if (uaa->usb_mode != USB_MODE_HOST)
		return (ENXIO);
	if (uaa->info.bInterfaceClass != UICLASS_HID)
		return (ENXIO);
	if (uaa->info.bIfaceIndex != 0)
		return (ENXIO);

	return (usbd_lookup_id_by_uaa(ugold_devs, sizeof(ugold_devs), uaa));
}

static int
ugold_attach(device_t dev)
{
	struct ugold_softc *sc = device_get_softc(dev);
	struct usb_attach_arg *uaa = device_get_ivars(dev);
	struct sysctl_oid *sensor_tree;
	uint16_t d_len;
	void *d_ptr;
	int error;
	int i;

	sc->sc_udev = uaa->device;
	sc->sc_readout_msg[0].hdr.pm_callback = &ugold_readout_msg;
	sc->sc_readout_msg[0].sc = sc;
	sc->sc_readout_msg[1].hdr.pm_callback = &ugold_readout_msg;
	sc->sc_readout_msg[1].sc = sc;
	sc->sc_iface_index[0] = uaa->info.bIfaceIndex;
	sc->sc_iface_index[1] = uaa->info.bIfaceIndex + 1;

	device_set_usb_desc(dev);
	mtx_init(&sc->sc_mtx, "ugold lock", NULL, MTX_DEF | MTX_RECURSE);
	callout_init_mtx(&sc->sc_callout, &sc->sc_mtx, 0);

	/* grab all interfaces from other drivers */
	for (i = 0;; i++) {
		if (i == uaa->info.bIfaceIndex)
			continue;
		if (usbd_get_iface(uaa->device, i) == NULL)
			break;

		usbd_set_parent_iface(uaa->device, i, uaa->info.bIfaceIndex);
	}

	/* figure out report ID */
	error = usbd_req_get_hid_desc(uaa->device, NULL,
	    &d_ptr, &d_len, M_TEMP, uaa->info.bIfaceIndex);

	if (error)
		goto detach;

	(void)hid_report_size(d_ptr, d_len, hid_input, &sc->sc_report_id);

	free(d_ptr, M_TEMP);

	error = usbd_transfer_setup(uaa->device,
	    sc->sc_iface_index, sc->sc_xfer, ugold_config,
	    UGOLD_N_TRANSFER, sc, &sc->sc_mtx);
	if (error)
		goto detach;

	sensor_tree = SYSCTL_ADD_NODE(device_get_sysctl_ctx(dev),
	    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)), OID_AUTO, "sensors",
	    CTLFLAG_RD, NULL, "");

	if (sensor_tree == NULL) {
		error = ENOMEM;
		goto detach;
	}
	SYSCTL_ADD_INT(device_get_sysctl_ctx(dev),
	    SYSCTL_CHILDREN(sensor_tree),
	    OID_AUTO, "inner", CTLFLAG_RD, &sc->sc_sensor[UGOLD_INNER], 0,
	    "Inner temperature in microCelcius");

	SYSCTL_ADD_INT(device_get_sysctl_ctx(dev),
	    SYSCTL_CHILDREN(sensor_tree),
	    OID_AUTO, "inner_valid", CTLFLAG_RD, &sc->sc_valid[UGOLD_INNER], 0,
	    "Inner temperature is valid");

	SYSCTL_ADD_INT(device_get_sysctl_ctx(dev),
	    SYSCTL_CHILDREN(sensor_tree),
	    OID_AUTO, "inner_calib", CTLFLAG_RWTUN, &sc->sc_calib[UGOLD_INNER], 0,
	    "Inner calibration temperature in microCelcius");
	
	SYSCTL_ADD_INT(device_get_sysctl_ctx(dev),
	    SYSCTL_CHILDREN(sensor_tree),
	    OID_AUTO, "outer", CTLFLAG_RD, &sc->sc_sensor[UGOLD_OUTER], 0,
	    "Outer temperature in microCelcius");

	SYSCTL_ADD_INT(device_get_sysctl_ctx(dev),
	    SYSCTL_CHILDREN(sensor_tree),
	    OID_AUTO, "outer_calib", CTLFLAG_RWTUN, &sc->sc_calib[UGOLD_OUTER], 0,
	    "Outer calibration temperature in microCelcius");

	SYSCTL_ADD_INT(device_get_sysctl_ctx(dev),
	    SYSCTL_CHILDREN(sensor_tree),
	    OID_AUTO, "outer_valid", CTLFLAG_RD, &sc->sc_valid[UGOLD_OUTER], 0,
	    "Outer temperature is valid");

	mtx_lock(&sc->sc_mtx);
	usbd_transfer_start(sc->sc_xfer[UGOLD_INTR_DT]);
	ugold_timeout(sc);
	mtx_unlock(&sc->sc_mtx);

	return (0);

detach:
	DPRINTF("error=%s\n", usbd_errstr(error));
	ugold_detach(dev);
	return (error);
}

static int
ugold_detach(device_t dev)
{
	struct ugold_softc *sc = device_get_softc(dev);

	callout_drain(&sc->sc_callout);

	usb_proc_explore_lock(sc->sc_udev);
	usb_proc_explore_mwait(sc->sc_udev,
	    &sc->sc_readout_msg[0], &sc->sc_readout_msg[1]);
	usb_proc_explore_unlock(sc->sc_udev);

	usbd_transfer_unsetup(sc->sc_xfer, UGOLD_N_TRANSFER);

	mtx_destroy(&sc->sc_mtx);

	return (0);
}

static int
ugold_ds75_temp(uint8_t msb, uint8_t lsb)
{
	/* DS75: 12bit precision mode : 0.0625 degrees Celsius ticks */
	/* NOTE: MSB has a sign bit for negative temperatures */
	int32_t temp = (msb << 24) | ((lsb & 0xF0) << 16);
	return (((int64_t)temp * (int64_t)1000000LL) >> 24);
}


static void
ugold_intr_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct ugold_softc *sc = usbd_xfer_softc(xfer);
	struct usb_page_cache *pc;
	uint8_t buf[8];
	int temp;
	int len;

	usbd_xfer_status(xfer, &len, NULL, NULL, NULL);

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:
		memset(buf, 0, sizeof(buf));

		pc = usbd_xfer_get_frame(xfer, 0);
		usbd_copy_out(pc, 0, buf, MIN(len, sizeof(buf)));

		switch (buf[0]) {
		case UGOLD_CMD_INIT:
			if (sc->sc_num_sensors)
				break;

			sc->sc_num_sensors = MIN(buf[1], UGOLD_MAX_SENSORS) /* XXX */ ;

			DPRINTF("%d sensor%s type ds75/12bit (temperature)\n",
			    sc->sc_num_sensors, (sc->sc_num_sensors == 1) ? "" : "s");
			break;
		case UGOLD_CMD_DATA:
			switch (buf[1]) {
			case 4:
				temp = ugold_ds75_temp(buf[4], buf[5]);
				sc->sc_sensor[UGOLD_OUTER] = temp + sc->sc_calib[UGOLD_OUTER];
				sc->sc_valid[UGOLD_OUTER] = 1;
				/* FALLTHROUGH */
			case 2:
				temp = ugold_ds75_temp(buf[2], buf[3]);
				sc->sc_sensor[UGOLD_INNER] = temp + sc->sc_calib[UGOLD_INNER];
				sc->sc_valid[UGOLD_INNER] = 1;
				break;
			default:
				DPRINTF("invalid data length (%d bytes)\n", buf[1]);
			}
			break;
		default:
			DPRINTF("unknown command 0x%02x\n", buf[0]);
			break;
		}
		/* FALLTHROUGH */
	case USB_ST_SETUP:
tr_setup:
		usbd_xfer_set_frame_len(xfer, 0, usbd_xfer_max_len(xfer));
		usbd_transfer_submit(xfer);
		break;
	default:			/* Error */
		if (error != USB_ERR_CANCELLED) {
			/* try clear stall first */
			usbd_xfer_set_stall(xfer);
			goto tr_setup;
		}
		break;
	}
}

static int
ugold_issue_cmd(struct ugold_softc *sc, uint8_t *cmd, int len)
{
	return (usbd_req_set_report(sc->sc_udev, &sc->sc_mtx, cmd, len,
	    sc->sc_iface_index[1], UHID_OUTPUT_REPORT, sc->sc_report_id));
}

static void
ugold_readout_msg(struct usb_proc_msg *pm)
{
	struct ugold_softc *sc = ((struct ugold_readout_msg *)pm)->sc;

	usb_proc_explore_unlock(sc->sc_udev);

	mtx_lock(&sc->sc_mtx);
	if (sc->sc_num_sensors == 0)
		ugold_issue_cmd(sc, cmd_init, sizeof(cmd_init));

	ugold_issue_cmd(sc, cmd_data, sizeof(cmd_data));
	mtx_unlock(&sc->sc_mtx);

	usb_proc_explore_lock(sc->sc_udev);
}
