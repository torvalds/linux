/*	$OpenBSD: udl.c,v 1.81 2014/12/09 07:05:06 doug Exp $ */
/*	$FreeBSD$ */

/*-
 * Copyright (c) 2015 Hans Petter Selasky <hselasky@freebsd.org>
 * Copyright (c) 2009 Marcus Glocker <mglocker@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * Driver for the "DisplayLink DL-120 / DL-160" graphic chips based on
 * the reversed engineered specifications of Florian Echtler
 * <floe@butterbrot.org>:
 *
 * 	http://floe.butterbrot.org/displaylink/doku.php
 */

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/callout.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/condvar.h>
#include <sys/sysctl.h>
#include <sys/systm.h>
#include <sys/consio.h>
#include <sys/fbio.h>

#include <dev/fb/fbreg.h>
#include <dev/syscons/syscons.h>

#include <dev/videomode/videomode.h>
#include <dev/videomode/edidvar.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdi_util.h>
#include "usbdevs.h"

#include <dev/usb/video/udl.h>

#include "fb_if.h"

#undef DPRINTF
#undef DPRINTFN
#define	USB_DEBUG_VAR udl_debug
#include <dev/usb/usb_debug.h>

static	SYSCTL_NODE(_hw_usb, OID_AUTO, udl, CTLFLAG_RW, 0, "USB UDL");

#ifdef USB_DEBUG
static int udl_debug = 0;

SYSCTL_INT(_hw_usb_udl, OID_AUTO, debug, CTLFLAG_RWTUN,
    &udl_debug, 0, "Debug level");
#endif

#define	UDL_FPS_MAX	60
#define	UDL_FPS_MIN	1

static int udl_fps = 25;
SYSCTL_INT(_hw_usb_udl, OID_AUTO, fps, CTLFLAG_RWTUN,
    &udl_fps, 0, "Frames Per Second, 1-60");

static struct mtx udl_buffer_mtx;
static struct udl_buffer_head udl_buffer_head;

MALLOC_DEFINE(M_USB_DL, "USB", "USB DisplayLink");

/*
 * Prototypes.
 */
static usb_callback_t udl_bulk_write_callback;

static device_probe_t udl_probe;
static device_attach_t udl_attach;
static device_detach_t udl_detach;
static fb_getinfo_t udl_fb_getinfo;
static fb_setblankmode_t udl_fb_setblankmode;

static void udl_select_chip(struct udl_softc *, struct usb_attach_arg *);
static int udl_init_chip(struct udl_softc *);
static void udl_select_mode(struct udl_softc *);
static int udl_init_resolution(struct udl_softc *);
static void udl_fbmem_alloc(struct udl_softc *);
static int udl_cmd_write_buf_le16(struct udl_softc *, const uint8_t *, uint32_t, uint8_t, int);
static int udl_cmd_buf_copy_le16(struct udl_softc *, uint32_t, uint32_t, uint8_t, int);
static void udl_cmd_insert_int_1(struct udl_cmd_buf *, uint8_t);
static void udl_cmd_insert_int_3(struct udl_cmd_buf *, uint32_t);
static void udl_cmd_insert_buf_le16(struct udl_cmd_buf *, const uint8_t *, uint32_t);
static void udl_cmd_write_reg_1(struct udl_cmd_buf *, uint8_t, uint8_t);
static void udl_cmd_write_reg_3(struct udl_cmd_buf *, uint8_t, uint32_t);
static int udl_power_save(struct udl_softc *, int, int);

static const struct usb_config udl_config[UDL_N_TRANSFER] = {
	[UDL_BULK_WRITE_0] = {
		.type = UE_BULK,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_TX,
		.flags = {.pipe_bof = 1,.force_short_xfer = 1,.ext_buffer = 1,},
		.bufsize = UDL_CMD_MAX_DATA_SIZE * UDL_CMD_MAX_FRAMES,
		.callback = &udl_bulk_write_callback,
		.frames = UDL_CMD_MAX_FRAMES,
		.timeout = 5000,	/* 5 seconds */
	},
	[UDL_BULK_WRITE_1] = {
		.type = UE_BULK,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_TX,
		.flags = {.pipe_bof = 1,.force_short_xfer = 1,.ext_buffer = 1,},
		.bufsize = UDL_CMD_MAX_DATA_SIZE * UDL_CMD_MAX_FRAMES,
		.callback = &udl_bulk_write_callback,
		.frames = UDL_CMD_MAX_FRAMES,
		.timeout = 5000,	/* 5 seconds */
	},
};

/*
 * Driver glue.
 */
static devclass_t udl_devclass;

static device_method_t udl_methods[] = {
	DEVMETHOD(device_probe, udl_probe),
	DEVMETHOD(device_attach, udl_attach),
	DEVMETHOD(device_detach, udl_detach),
	DEVMETHOD(fb_getinfo, udl_fb_getinfo),
	DEVMETHOD_END
};

static driver_t udl_driver = {
	.name = "udl",
	.methods = udl_methods,
	.size = sizeof(struct udl_softc),
};

DRIVER_MODULE(udl, uhub, udl_driver, udl_devclass, NULL, NULL);
MODULE_DEPEND(udl, usb, 1, 1, 1);
MODULE_DEPEND(udl, fbd, 1, 1, 1);
MODULE_DEPEND(udl, videomode, 1, 1, 1);
MODULE_VERSION(udl, 1);

/*
 * Matching devices.
 */
static const STRUCT_USB_HOST_ID udl_devs[] = {
	{USB_VPI(USB_VENDOR_DISPLAYLINK, USB_PRODUCT_DISPLAYLINK_LCD4300U, DL120)},
	{USB_VPI(USB_VENDOR_DISPLAYLINK, USB_PRODUCT_DISPLAYLINK_LCD8000U, DL120)},
	{USB_VPI(USB_VENDOR_DISPLAYLINK, USB_PRODUCT_DISPLAYLINK_GUC2020, DL160)},
	{USB_VPI(USB_VENDOR_DISPLAYLINK, USB_PRODUCT_DISPLAYLINK_LD220, DL165)},
	{USB_VPI(USB_VENDOR_DISPLAYLINK, USB_PRODUCT_DISPLAYLINK_VCUD60, DL160)},
	{USB_VPI(USB_VENDOR_DISPLAYLINK, USB_PRODUCT_DISPLAYLINK_DLDVI, DL160)},
	{USB_VPI(USB_VENDOR_DISPLAYLINK, USB_PRODUCT_DISPLAYLINK_VGA10, DL120)},
	{USB_VPI(USB_VENDOR_DISPLAYLINK, USB_PRODUCT_DISPLAYLINK_WSDVI, DLUNK)},
	{USB_VPI(USB_VENDOR_DISPLAYLINK, USB_PRODUCT_DISPLAYLINK_EC008, DL160)},
	{USB_VPI(USB_VENDOR_DISPLAYLINK, USB_PRODUCT_DISPLAYLINK_HPDOCK, DL160)},
	{USB_VPI(USB_VENDOR_DISPLAYLINK, USB_PRODUCT_DISPLAYLINK_NL571, DL160)},
	{USB_VPI(USB_VENDOR_DISPLAYLINK, USB_PRODUCT_DISPLAYLINK_M01061, DL195)},
	{USB_VPI(USB_VENDOR_DISPLAYLINK, USB_PRODUCT_DISPLAYLINK_NBDOCK, DL165)},
	{USB_VPI(USB_VENDOR_DISPLAYLINK, USB_PRODUCT_DISPLAYLINK_SWDVI, DLUNK)},
	{USB_VPI(USB_VENDOR_DISPLAYLINK, USB_PRODUCT_DISPLAYLINK_UM7X0, DL120)},
	{USB_VPI(USB_VENDOR_DISPLAYLINK, USB_PRODUCT_DISPLAYLINK_CONV, DL160)},
	{USB_VPI(USB_VENDOR_DISPLAYLINK, USB_PRODUCT_DISPLAYLINK_PLUGABLE, DL160)},
	{USB_VPI(USB_VENDOR_DISPLAYLINK, USB_PRODUCT_DISPLAYLINK_LUM70, DL125)},
	{USB_VPI(USB_VENDOR_DISPLAYLINK, USB_PRODUCT_DISPLAYLINK_POLARIS2, DLUNK)},
	{USB_VPI(USB_VENDOR_DISPLAYLINK, USB_PRODUCT_DISPLAYLINK_LT1421, DLUNK)},
	{USB_VPI(USB_VENDOR_DISPLAYLINK, USB_PRODUCT_DISPLAYLINK_ITEC, DL165)},
};

static void
udl_buffer_init(void *arg)
{
	mtx_init(&udl_buffer_mtx, "USB", "UDL", MTX_DEF);
	TAILQ_INIT(&udl_buffer_head);
}
SYSINIT(udl_buffer_init, SI_SUB_LOCK, SI_ORDER_FIRST, udl_buffer_init, NULL);

CTASSERT(sizeof(struct udl_buffer) < PAGE_SIZE);

static void *
udl_buffer_alloc(uint32_t size)
{
	struct udl_buffer *buf;
	mtx_lock(&udl_buffer_mtx);
	TAILQ_FOREACH(buf, &udl_buffer_head, entry) {
		if (buf->size == size) {
			TAILQ_REMOVE(&udl_buffer_head, buf, entry);
			break;
		}
	}
	mtx_unlock(&udl_buffer_mtx);
	if (buf != NULL) {
		uint8_t *ptr = ((uint8_t *)buf) - size;
		/* wipe and recycle buffer */
		memset(ptr, 0, size);
		/* return buffer pointer */
		return (ptr);
	}
	/* allocate new buffer */
	return (malloc(size + sizeof(*buf), M_USB_DL, M_WAITOK | M_ZERO));
}

static void
udl_buffer_free(void *_buf, uint32_t size)
{
	struct udl_buffer *buf;

	/* check for NULL pointer */
	if (_buf == NULL)
		return;
	/* compute pointer to recycle list */
	buf = (struct udl_buffer *)(((uint8_t *)_buf) + size);

	/*
	 * Memory mapped buffers should never be freed.
	 * Put display buffer into a recycle list.
	 */
	mtx_lock(&udl_buffer_mtx);
	buf->size = size;
	TAILQ_INSERT_TAIL(&udl_buffer_head, buf, entry);
	mtx_unlock(&udl_buffer_mtx);
}

static uint32_t
udl_get_fb_size(struct udl_softc *sc)
{
	unsigned i = sc->sc_cur_mode;

	return ((uint32_t)udl_modes[i].hdisplay *
	    (uint32_t)udl_modes[i].vdisplay * 2);
}

static uint32_t
udl_get_fb_width(struct udl_softc *sc)
{
	unsigned i = sc->sc_cur_mode;

	return (udl_modes[i].hdisplay);
}

static uint32_t
udl_get_fb_height(struct udl_softc *sc)
{
	unsigned i = sc->sc_cur_mode;

	return (udl_modes[i].vdisplay);
}

static uint32_t
udl_get_fb_hz(struct udl_softc *sc)
{
	unsigned i = sc->sc_cur_mode;

	return (udl_modes[i].hz);
}

static void
udl_callout(void *arg)
{
	struct udl_softc *sc = arg;
	const uint32_t max = udl_get_fb_size(sc);
	int fps;

	if (sc->sc_power_save == 0) {
		fps = udl_fps;

	  	/* figure out number of frames per second */
		if (fps < UDL_FPS_MIN)
			fps = UDL_FPS_MIN;
		else if (fps > UDL_FPS_MAX)
			fps = UDL_FPS_MAX;

		if (sc->sc_sync_off >= max)
			sc->sc_sync_off = 0;
		usbd_transfer_start(sc->sc_xfer[UDL_BULK_WRITE_0]);
		usbd_transfer_start(sc->sc_xfer[UDL_BULK_WRITE_1]);
	} else {
		fps = 1;
	}
	callout_reset(&sc->sc_callout, hz / fps, &udl_callout, sc);
}

static int
udl_probe(device_t dev)
{
	struct usb_attach_arg *uaa = device_get_ivars(dev);

	if (uaa->usb_mode != USB_MODE_HOST)
		return (ENXIO);
	if (uaa->info.bConfigIndex != 0)
		return (ENXIO);
	if (uaa->info.bIfaceIndex != 0)
		return (ENXIO);

	return (usbd_lookup_id_by_uaa(udl_devs, sizeof(udl_devs), uaa));
}

static int
udl_attach(device_t dev)
{
	struct sysctl_ctx_list *ctx = device_get_sysctl_ctx(dev);
	struct sysctl_oid *tree = device_get_sysctl_tree(dev);
	struct udl_softc *sc = device_get_softc(dev);
	struct usb_attach_arg *uaa = device_get_ivars(dev);
	int error;
	int i;

	device_set_usb_desc(dev);

	mtx_init(&sc->sc_mtx, "UDL lock", NULL, MTX_DEF);
	cv_init(&sc->sc_cv, "UDLCV");
	callout_init_mtx(&sc->sc_callout, &sc->sc_mtx, 0);
	sc->sc_udev = uaa->device;

	error = usbd_transfer_setup(uaa->device, &uaa->info.bIfaceIndex,
	    sc->sc_xfer, udl_config, UDL_N_TRANSFER, sc, &sc->sc_mtx);

	if (error) {
		DPRINTF("usbd_transfer_setup error=%s\n", usbd_errstr(error));
		goto detach;
	}
	usbd_xfer_set_priv(sc->sc_xfer[UDL_BULK_WRITE_0], &sc->sc_xfer_head[0]);
	usbd_xfer_set_priv(sc->sc_xfer[UDL_BULK_WRITE_1], &sc->sc_xfer_head[1]);

	TAILQ_INIT(&sc->sc_xfer_head[0]);
	TAILQ_INIT(&sc->sc_xfer_head[1]);
	TAILQ_INIT(&sc->sc_cmd_buf_free);
	TAILQ_INIT(&sc->sc_cmd_buf_pending);

	sc->sc_def_chip = -1;
	sc->sc_chip = USB_GET_DRIVER_INFO(uaa);
	sc->sc_def_mode = -1;
	sc->sc_cur_mode = UDL_MAX_MODES;

	/* Allow chip ID to be overwritten */
	SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(tree), OID_AUTO, "chipid_force",
	    CTLFLAG_RWTUN, &sc->sc_def_chip, 0, "chip ID");

	/* Export current chip ID */
	SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(tree), OID_AUTO, "chipid",
	    CTLFLAG_RD, &sc->sc_chip, 0, "chip ID");

	if (sc->sc_def_chip > -1 && sc->sc_def_chip <= DLMAX) {
		device_printf(dev, "Forcing chip ID to 0x%04x\n", sc->sc_def_chip);
		sc->sc_chip = sc->sc_def_chip;
	}
	/*
	 * The product might have more than one chip
	 */
	if (sc->sc_chip == DLUNK)
		udl_select_chip(sc, uaa);

	for (i = 0; i != UDL_CMD_MAX_BUFFERS; i++) {
		struct udl_cmd_buf *cb = &sc->sc_cmd_buf_temp[i];

		TAILQ_INSERT_TAIL(&sc->sc_cmd_buf_free, cb, entry);
	}

	/*
	 * Initialize chip.
	 */
	error = udl_init_chip(sc);
	if (error != USB_ERR_NORMAL_COMPLETION)
		goto detach;

	/*
	 * Select edid mode.
	 */
	udl_select_mode(sc);

	/* Allow default mode to be overwritten */
	SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(tree), OID_AUTO, "mode_force",
	    CTLFLAG_RWTUN, &sc->sc_def_mode, 0, "mode");

	/* Export current mode */
	SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(tree), OID_AUTO, "mode",
	    CTLFLAG_RD, &sc->sc_cur_mode, 0, "mode");

	i = sc->sc_def_mode;
	if (i > -1 && i < UDL_MAX_MODES) {
		if (udl_modes[i].chip <= sc->sc_chip) {
			device_printf(dev, "Forcing mode to %d\n", i);
			sc->sc_cur_mode = i;
		}
	}
	/* Printout current mode */
	device_printf(dev, "Mode selected %dx%d @ %dHz\n",
	    (int)udl_get_fb_width(sc),
	    (int)udl_get_fb_height(sc),
	    (int)udl_get_fb_hz(sc));

	udl_init_resolution(sc);

	/* Allocate frame buffer */
	udl_fbmem_alloc(sc);

	UDL_LOCK(sc);
	udl_callout(sc);
	UDL_UNLOCK(sc);

	sc->sc_fb_info.fb_name = device_get_nameunit(dev);
	sc->sc_fb_info.fb_size = sc->sc_fb_size;
	sc->sc_fb_info.fb_bpp = 16;
	sc->sc_fb_info.fb_depth = 16;
	sc->sc_fb_info.fb_width = udl_get_fb_width(sc);
	sc->sc_fb_info.fb_height = udl_get_fb_height(sc);
	sc->sc_fb_info.fb_stride = sc->sc_fb_info.fb_width * 2;
	sc->sc_fb_info.fb_pbase = 0;
	sc->sc_fb_info.fb_vbase = (uintptr_t)sc->sc_fb_addr;
	sc->sc_fb_info.fb_priv = sc;
	sc->sc_fb_info.setblankmode = &udl_fb_setblankmode;

	sc->sc_fbdev = device_add_child(dev, "fbd", -1);
	if (sc->sc_fbdev == NULL)
		goto detach;
	if (device_probe_and_attach(sc->sc_fbdev) != 0)
		goto detach;

	return (0);

detach:
	udl_detach(dev);

	return (ENXIO);
}

static int
udl_detach(device_t dev)
{
	struct udl_softc *sc = device_get_softc(dev);

	/* delete all child devices */
	device_delete_children(dev);

	UDL_LOCK(sc);
	sc->sc_gone = 1;
	callout_stop(&sc->sc_callout);
	UDL_UNLOCK(sc);

	usbd_transfer_unsetup(sc->sc_xfer, UDL_N_TRANSFER);

	callout_drain(&sc->sc_callout);

	mtx_destroy(&sc->sc_mtx);
	cv_destroy(&sc->sc_cv);

	/* put main framebuffer into a recycle list, if any */
	udl_buffer_free(sc->sc_fb_addr, sc->sc_fb_size);

	/* free shadow framebuffer memory, if any */
	free(sc->sc_fb_copy, M_USB_DL);

	return (0);
}

static struct fb_info *
udl_fb_getinfo(device_t dev)
{
	struct udl_softc *sc = device_get_softc(dev);

	return (&sc->sc_fb_info);
}

static int
udl_fb_setblankmode(void *arg, int mode)
{
	struct udl_softc *sc = arg;

	switch (mode) {
	case V_DISPLAY_ON:
		udl_power_save(sc, 1, M_WAITOK);
		break;
	case V_DISPLAY_BLANK:
		udl_power_save(sc, 1, M_WAITOK);
		if (sc->sc_fb_addr != 0) {
			const uint32_t max = udl_get_fb_size(sc);

			memset((void *)sc->sc_fb_addr, 0, max);
		}
		break;
	case V_DISPLAY_STAND_BY:
	case V_DISPLAY_SUSPEND:
		udl_power_save(sc, 0, M_WAITOK);
		break;
	}
	return (0);
}

static struct udl_cmd_buf *
udl_cmd_buf_alloc_locked(struct udl_softc *sc, int flags)
{
	struct udl_cmd_buf *cb;

	while ((cb = TAILQ_FIRST(&sc->sc_cmd_buf_free)) == NULL) {
		if (flags != M_WAITOK)
			break;
		cv_wait(&sc->sc_cv, &sc->sc_mtx);
	}
	if (cb != NULL) {
		TAILQ_REMOVE(&sc->sc_cmd_buf_free, cb, entry);
		cb->off = 0;
	}
	return (cb);
}

static struct udl_cmd_buf *
udl_cmd_buf_alloc(struct udl_softc *sc, int flags)
{
	struct udl_cmd_buf *cb;

	UDL_LOCK(sc);
	cb = udl_cmd_buf_alloc_locked(sc, flags);
	UDL_UNLOCK(sc);
	return (cb);
}

static void
udl_cmd_buf_send(struct udl_softc *sc, struct udl_cmd_buf *cb)
{
	UDL_LOCK(sc);
	if (sc->sc_gone) {
		TAILQ_INSERT_TAIL(&sc->sc_cmd_buf_free, cb, entry);
	} else {
		/* mark end of command stack */
		udl_cmd_insert_int_1(cb, UDL_BULK_SOC);
		udl_cmd_insert_int_1(cb, UDL_BULK_CMD_EOC);

		TAILQ_INSERT_TAIL(&sc->sc_cmd_buf_pending, cb, entry);
		usbd_transfer_start(sc->sc_xfer[UDL_BULK_WRITE_0]);
		usbd_transfer_start(sc->sc_xfer[UDL_BULK_WRITE_1]);
	}
	UDL_UNLOCK(sc);
}

static struct udl_cmd_buf *
udl_fb_synchronize_locked(struct udl_softc *sc)
{
	const uint32_t max = udl_get_fb_size(sc);

	/* check if framebuffer is not ready */
	if (sc->sc_fb_addr == NULL ||
	    sc->sc_fb_copy == NULL)
		return (NULL);

	while (sc->sc_sync_off < max) {
		uint32_t delta = max - sc->sc_sync_off;

		if (delta > UDL_CMD_MAX_PIXEL_COUNT * 2)
			delta = UDL_CMD_MAX_PIXEL_COUNT * 2;
		if (bcmp(sc->sc_fb_addr + sc->sc_sync_off, sc->sc_fb_copy + sc->sc_sync_off, delta) != 0) {
			struct udl_cmd_buf *cb;

			cb = udl_cmd_buf_alloc_locked(sc, M_NOWAIT);
			if (cb == NULL)
				goto done;
			memcpy(sc->sc_fb_copy + sc->sc_sync_off,
			    sc->sc_fb_addr + sc->sc_sync_off, delta);
			udl_cmd_insert_int_1(cb, UDL_BULK_SOC);
			udl_cmd_insert_int_1(cb, UDL_BULK_CMD_FB_WRITE | UDL_BULK_CMD_FB_WORD);
			udl_cmd_insert_int_3(cb, sc->sc_sync_off);
			udl_cmd_insert_int_1(cb, delta / 2);
			udl_cmd_insert_buf_le16(cb, sc->sc_fb_copy + sc->sc_sync_off, delta);
			sc->sc_sync_off += delta;
			return (cb);
		} else {
			sc->sc_sync_off += delta;
		}
	}
done:
	return (NULL);
}

static void
udl_bulk_write_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct udl_softc *sc = usbd_xfer_softc(xfer);
	struct udl_cmd_head *phead = usbd_xfer_get_priv(xfer);
	struct udl_cmd_buf *cb;
	unsigned i;

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:
		TAILQ_CONCAT(&sc->sc_cmd_buf_free, phead, entry);
	case USB_ST_SETUP:
tr_setup:
		for (i = 0; i != UDL_CMD_MAX_FRAMES; i++) {
			cb = TAILQ_FIRST(&sc->sc_cmd_buf_pending);
			if (cb == NULL) {
				cb = udl_fb_synchronize_locked(sc);
				if (cb == NULL)
					break;
			} else {
				TAILQ_REMOVE(&sc->sc_cmd_buf_pending, cb, entry);
			}
			TAILQ_INSERT_TAIL(phead, cb, entry);
			usbd_xfer_set_frame_data(xfer, i, cb->buf, cb->off);
		}
		if (i != 0) {
			usbd_xfer_set_frames(xfer, i);
			usbd_transfer_submit(xfer);
		}
		break;
	default:
		TAILQ_CONCAT(&sc->sc_cmd_buf_free, phead, entry);
		if (error != USB_ERR_CANCELLED) {
			/* try clear stall first */
			usbd_xfer_set_stall(xfer);
			goto tr_setup;
		}
		break;
	}
	/* wakeup any waiters */
	cv_signal(&sc->sc_cv);
}

static int
udl_power_save(struct udl_softc *sc, int on, int flags)
{
	struct udl_cmd_buf *cb;

	/* get new buffer */
	cb = udl_cmd_buf_alloc(sc, flags);
	if (cb == NULL)
		return (EAGAIN);

	DPRINTF("screen %s\n", on ? "ON" : "OFF");

	sc->sc_power_save = on ? 0 : 1;

	if (on)
		udl_cmd_write_reg_1(cb, UDL_REG_SCREEN, UDL_REG_SCREEN_ON);
	else
		udl_cmd_write_reg_1(cb, UDL_REG_SCREEN, UDL_REG_SCREEN_OFF);

	udl_cmd_write_reg_1(cb, UDL_REG_SYNC, 0xff);
	udl_cmd_buf_send(sc, cb);
	return (0);
}

static int
udl_ctrl_msg(struct udl_softc *sc, uint8_t rt, uint8_t r,
    uint16_t index, uint16_t value, uint8_t *buf, size_t len)
{
	usb_device_request_t req;
	int error;

	req.bmRequestType = rt;
	req.bRequest = r;
	USETW(req.wIndex, index);
	USETW(req.wValue, value);
	USETW(req.wLength, len);

	error = usbd_do_request_flags(sc->sc_udev, NULL,
	    &req, buf, 0, NULL, USB_DEFAULT_TIMEOUT);

	DPRINTF("%s\n", usbd_errstr(error));

	return (error);
}

static int
udl_poll(struct udl_softc *sc, uint32_t *buf)
{
	uint32_t lbuf;
	int error;

	error = udl_ctrl_msg(sc, UT_READ_VENDOR_DEVICE,
	    UDL_CTRL_CMD_POLL, 0x0000, 0x0000, (uint8_t *)&lbuf, sizeof(lbuf));
	if (error == USB_ERR_NORMAL_COMPLETION)
		*buf = le32toh(lbuf);
	return (error);
}

static int
udl_read_1(struct udl_softc *sc, uint16_t addr, uint8_t *buf)
{
	uint8_t lbuf[1];
	int error;

	error = udl_ctrl_msg(sc, UT_READ_VENDOR_DEVICE,
	    UDL_CTRL_CMD_READ_1, addr, 0x0000, lbuf, 1);
	if (error == USB_ERR_NORMAL_COMPLETION)
		*buf = *(uint8_t *)lbuf;
	return (error);
}

static int
udl_write_1(struct udl_softc *sc, uint16_t addr, uint8_t buf)
{
	int error;

	error = udl_ctrl_msg(sc, UT_WRITE_VENDOR_DEVICE,
	    UDL_CTRL_CMD_WRITE_1, addr, 0x0000, &buf, 1);
	return (error);
}

static int
udl_read_edid(struct udl_softc *sc, uint8_t *buf)
{
	uint8_t lbuf[64];
	uint16_t offset;
	int error;

	offset = 0;

	error = udl_ctrl_msg(sc, UT_READ_VENDOR_DEVICE,
	    UDL_CTRL_CMD_READ_EDID, 0x00a1, (offset << 8), lbuf, 64);
	if (error != USB_ERR_NORMAL_COMPLETION)
		goto fail;
	bcopy(lbuf + 1, buf + offset, 63);
	offset += 63;

	error = udl_ctrl_msg(sc, UT_READ_VENDOR_DEVICE,
	    UDL_CTRL_CMD_READ_EDID, 0x00a1, (offset << 8), lbuf, 64);
	if (error != USB_ERR_NORMAL_COMPLETION)
		goto fail;
	bcopy(lbuf + 1, buf + offset, 63);
	offset += 63;

	error = udl_ctrl_msg(sc, UT_READ_VENDOR_DEVICE,
	    UDL_CTRL_CMD_READ_EDID, 0x00a1, (offset << 8), lbuf, 3);
	if (error != USB_ERR_NORMAL_COMPLETION)
		goto fail;
	bcopy(lbuf + 1, buf + offset, 2);
fail:
	return (error);
}

static uint8_t
udl_lookup_mode(uint16_t hdisplay, uint16_t vdisplay, uint8_t hz,
    uint16_t chip, uint32_t clock)
{
	uint8_t idx;

	/*
	 * Check first if we have a matching mode with pixelclock
	 */
	for (idx = 0; idx != UDL_MAX_MODES; idx++) {
		if ((udl_modes[idx].hdisplay == hdisplay) &&
		    (udl_modes[idx].vdisplay == vdisplay) &&
		    (udl_modes[idx].clock == clock) &&
		    (udl_modes[idx].chip <= chip)) {
			return (idx);
		}
	}

	/*
	 * If not, check for matching mode with update frequency
	 */
	for (idx = 0; idx != UDL_MAX_MODES; idx++) {
		if ((udl_modes[idx].hdisplay == hdisplay) &&
		    (udl_modes[idx].vdisplay == vdisplay) &&
		    (udl_modes[idx].hz == hz) &&
		    (udl_modes[idx].chip <= chip)) {
			return (idx);
		}
	}
	return (idx);
}

static void
udl_select_chip(struct udl_softc *sc, struct usb_attach_arg *uaa)
{
	const char *pserial;

	pserial = usb_get_serial(uaa->device);

	sc->sc_chip = DL120;

	if ((uaa->info.idVendor == USB_VENDOR_DISPLAYLINK) &&
	    (uaa->info.idProduct == USB_PRODUCT_DISPLAYLINK_WSDVI)) {

		/*
		 * WS Tech DVI is DL120 or DL160. All deviced uses the
		 * same revision (0.04) so iSerialNumber must be used
		 * to determin which chip it is.
		 */

		if (strlen(pserial) > 7) {
			if (strncmp(pserial, "0198-13", 7) == 0)
				sc->sc_chip = DL160;
		}
		DPRINTF("iSerialNumber (%s) used to select chip (%d)\n",
		    pserial, sc->sc_chip);
	}
	if ((uaa->info.idVendor == USB_VENDOR_DISPLAYLINK) &&
	    (uaa->info.idProduct == USB_PRODUCT_DISPLAYLINK_SWDVI)) {

		/*
		 * SUNWEIT DVI is DL160, DL125, DL165 or DL195. Major revision
		 * can be used to differ between DL1x0 and DL1x5. Minor to
		 * differ between DL1x5. iSerialNumber seems not to be uniqe.
		 */

		sc->sc_chip = DL160;

		if (uaa->info.bcdDevice >= 0x100) {
			sc->sc_chip = DL165;
			if (uaa->info.bcdDevice == 0x104)
				sc->sc_chip = DL195;
			if (uaa->info.bcdDevice == 0x108)
				sc->sc_chip = DL125;
		}
		DPRINTF("bcdDevice (%02x) used to select chip (%d)\n",
		    uaa->info.bcdDevice, sc->sc_chip);
	}
}

static int
udl_set_enc_key(struct udl_softc *sc, uint8_t *buf, uint8_t len)
{
	int error;

	error = udl_ctrl_msg(sc, UT_WRITE_VENDOR_DEVICE,
	    UDL_CTRL_CMD_SET_KEY, 0x0000, 0x0000, buf, len);
	return (error);
}

static void
udl_fbmem_alloc(struct udl_softc *sc)
{
	uint32_t size;

	size = udl_get_fb_size(sc);
	size = round_page(size);
	/* check for zero size */
	if (size == 0)
		size = PAGE_SIZE;
	/*
	 * It is assumed that allocations above PAGE_SIZE bytes will
	 * be PAGE_SIZE aligned for use with mmap()
	 */
	sc->sc_fb_addr = udl_buffer_alloc(size);
	sc->sc_fb_copy = malloc(size, M_USB_DL, M_WAITOK | M_ZERO);
	sc->sc_fb_size = size;
}

static void
udl_cmd_insert_int_1(struct udl_cmd_buf *cb, uint8_t value)
{

	cb->buf[cb->off] = value;
	cb->off += 1;
}

#if 0
static void
udl_cmd_insert_int_2(struct udl_cmd_buf *cb, uint16_t value)
{
	uint16_t lvalue;

	lvalue = htobe16(value);
	bcopy(&lvalue, cb->buf + cb->off, 2);

	cb->off += 2;
}

#endif

static void
udl_cmd_insert_int_3(struct udl_cmd_buf *cb, uint32_t value)
{
	uint32_t lvalue;

#if BYTE_ORDER == BIG_ENDIAN
	lvalue = htobe32(value) << 8;
#else
	lvalue = htobe32(value) >> 8;
#endif
	bcopy(&lvalue, cb->buf + cb->off, 3);

	cb->off += 3;
}

#if 0
static void
udl_cmd_insert_int_4(struct udl_cmd_buf *cb, uint32_t value)
{
	uint32_t lvalue;

	lvalue = htobe32(value);
	bcopy(&lvalue, cb->buf + cb->off, 4);

	cb->off += 4;
}

#endif

static void
udl_cmd_insert_buf_le16(struct udl_cmd_buf *cb, const uint8_t *buf, uint32_t len)
{
	uint32_t x;

	for (x = 0; x != len; x += 2) {
		/* byte swap from little endian to big endian */
		cb->buf[cb->off + x + 0] = buf[x + 1];
		cb->buf[cb->off + x + 1] = buf[x + 0];
	}
	cb->off += len;
}

static void
udl_cmd_write_reg_1(struct udl_cmd_buf *cb, uint8_t reg, uint8_t val)
{

	udl_cmd_insert_int_1(cb, UDL_BULK_SOC);
	udl_cmd_insert_int_1(cb, UDL_BULK_CMD_REG_WRITE_1);
	udl_cmd_insert_int_1(cb, reg);
	udl_cmd_insert_int_1(cb, val);
}

static void
udl_cmd_write_reg_3(struct udl_cmd_buf *cb, uint8_t reg, uint32_t val)
{

	udl_cmd_write_reg_1(cb, reg + 0, (val >> 16) & 0xff);
	udl_cmd_write_reg_1(cb, reg + 1, (val >> 8) & 0xff);
	udl_cmd_write_reg_1(cb, reg + 2, (val >> 0) & 0xff);
}

static int
udl_init_chip(struct udl_softc *sc)
{
	uint32_t ui32;
	uint8_t ui8;
	int error;

	error = udl_poll(sc, &ui32);
	if (error != USB_ERR_NORMAL_COMPLETION)
		return (error);
	DPRINTF("poll=0x%08x\n", ui32);

	/* Some products may use later chip too */
	switch (ui32 & 0xff) {
	case 0xf1:			/* DL1x5 */
		switch (sc->sc_chip) {
		case DL120:
			sc->sc_chip = DL125;
			break;
		case DL160:
			sc->sc_chip = DL165;
			break;
		}
		break;
	}
	DPRINTF("chip 0x%04x\n", sc->sc_chip);

	error = udl_read_1(sc, 0xc484, &ui8);
	if (error != USB_ERR_NORMAL_COMPLETION)
		return (error);
	DPRINTF("read 0x%02x from 0xc484\n", ui8);

	error = udl_write_1(sc, 0xc41f, 0x01);
	if (error != USB_ERR_NORMAL_COMPLETION)
		return (error);
	DPRINTF("write 0x01 to 0xc41f\n");

	error = udl_read_edid(sc, sc->sc_edid);
	if (error != USB_ERR_NORMAL_COMPLETION)
		return (error);
	DPRINTF("read EDID\n");

	error = udl_set_enc_key(sc, __DECONST(void *, udl_null_key_1),
	    sizeof(udl_null_key_1));
	if (error != USB_ERR_NORMAL_COMPLETION)
		return (error);
	DPRINTF("set encryption key\n");

	error = udl_write_1(sc, 0xc40b, 0x00);
	if (error != USB_ERR_NORMAL_COMPLETION)
		return (error);
	DPRINTF("write 0x00 to 0xc40b\n");

	return (USB_ERR_NORMAL_COMPLETION);
}

static void
udl_init_fb_offsets(struct udl_cmd_buf *cb, uint32_t start16, uint32_t stride16,
    uint32_t start8, uint32_t stride8)
{
	udl_cmd_write_reg_1(cb, UDL_REG_SYNC, 0x00);
	udl_cmd_write_reg_3(cb, UDL_REG_ADDR_START16, start16);
	udl_cmd_write_reg_3(cb, UDL_REG_ADDR_STRIDE16, stride16);
	udl_cmd_write_reg_3(cb, UDL_REG_ADDR_START8, start8);
	udl_cmd_write_reg_3(cb, UDL_REG_ADDR_STRIDE8, stride8);
	udl_cmd_write_reg_1(cb, UDL_REG_SYNC, 0xff);
}

static int
udl_init_resolution(struct udl_softc *sc)
{
	const uint32_t max = udl_get_fb_size(sc);
	const uint8_t *buf = udl_modes[sc->sc_cur_mode].mode;
	struct udl_cmd_buf *cb;
	uint32_t delta;
	uint32_t i;
	int error;

	/* get new buffer */
	cb = udl_cmd_buf_alloc(sc, M_WAITOK);
	if (cb == NULL)
		return (EAGAIN);

	/* write resolution values and set video memory offsets */
	udl_cmd_write_reg_1(cb, UDL_REG_SYNC, 0x00);
	for (i = 0; i < UDL_MODE_SIZE; i++)
		udl_cmd_write_reg_1(cb, i, buf[i]);
	udl_cmd_write_reg_1(cb, UDL_REG_SYNC, 0xff);

	udl_init_fb_offsets(cb, 0x000000, 0x000a00, 0x555555, 0x000500);
	udl_cmd_buf_send(sc, cb);

	/* fill screen with black color */
	for (i = 0; i < max; i += delta) {
		static const uint8_t udl_black[UDL_CMD_MAX_PIXEL_COUNT * 2] __aligned(4);

		delta = max - i;
		if (delta > UDL_CMD_MAX_PIXEL_COUNT * 2)
			delta = UDL_CMD_MAX_PIXEL_COUNT * 2;
		if (i == 0)
			error = udl_cmd_write_buf_le16(sc, udl_black, i, delta / 2, M_WAITOK);
		else
			error = udl_cmd_buf_copy_le16(sc, 0, i, delta / 2, M_WAITOK);
		if (error)
			return (error);
	}

	/* get new buffer */
	cb = udl_cmd_buf_alloc(sc, M_WAITOK);
	if (cb == NULL)
		return (EAGAIN);

	/* show framebuffer content */
	udl_cmd_write_reg_1(cb, UDL_REG_SCREEN, UDL_REG_SCREEN_ON);
	udl_cmd_write_reg_1(cb, UDL_REG_SYNC, 0xff);
	udl_cmd_buf_send(sc, cb);
	return (0);
}

static void
udl_select_mode(struct udl_softc *sc)
{
	struct udl_mode mode;
	int index = UDL_MAX_MODES;
	int i;

	/* try to get the preferred mode from EDID */
	edid_parse(sc->sc_edid, &sc->sc_edid_info);
#ifdef USB_DEBUG
	edid_print(&sc->sc_edid_info);
#endif
	if (sc->sc_edid_info.edid_preferred_mode != NULL) {
		mode.hz =
		    (sc->sc_edid_info.edid_preferred_mode->dot_clock * 1000) /
		    (sc->sc_edid_info.edid_preferred_mode->htotal *
		    sc->sc_edid_info.edid_preferred_mode->vtotal);
		mode.clock =
		    sc->sc_edid_info.edid_preferred_mode->dot_clock / 10;
		mode.hdisplay =
		    sc->sc_edid_info.edid_preferred_mode->hdisplay;
		mode.vdisplay =
		    sc->sc_edid_info.edid_preferred_mode->vdisplay;
		index = udl_lookup_mode(mode.hdisplay, mode.vdisplay, mode.hz,
		    sc->sc_chip, mode.clock);
		sc->sc_cur_mode = index;
	} else {
		DPRINTF("no preferred mode found!\n");
	}

	if (index == UDL_MAX_MODES) {
		DPRINTF("no mode line found\n");

		i = 0;
		while (i < sc->sc_edid_info.edid_nmodes) {
			mode.hz =
			    (sc->sc_edid_info.edid_modes[i].dot_clock * 1000) /
			    (sc->sc_edid_info.edid_modes[i].htotal *
			    sc->sc_edid_info.edid_modes[i].vtotal);
			mode.clock =
			    sc->sc_edid_info.edid_modes[i].dot_clock / 10;
			mode.hdisplay =
			    sc->sc_edid_info.edid_modes[i].hdisplay;
			mode.vdisplay =
			    sc->sc_edid_info.edid_modes[i].vdisplay;
			index = udl_lookup_mode(mode.hdisplay, mode.vdisplay,
			    mode.hz, sc->sc_chip, mode.clock);
			if (index < UDL_MAX_MODES)
				if ((sc->sc_cur_mode == UDL_MAX_MODES) ||
				    (index > sc->sc_cur_mode))
					sc->sc_cur_mode = index;
			i++;
		}
	}
	/*
	 * If no mode found use default.
	 */
	if (sc->sc_cur_mode == UDL_MAX_MODES)
		sc->sc_cur_mode = udl_lookup_mode(800, 600, 60, sc->sc_chip, 0);
}

static int
udl_cmd_write_buf_le16(struct udl_softc *sc, const uint8_t *buf, uint32_t off,
    uint8_t pixels, int flags)
{
	struct udl_cmd_buf *cb;

	cb = udl_cmd_buf_alloc(sc, flags);
	if (cb == NULL)
		return (EAGAIN);

	udl_cmd_insert_int_1(cb, UDL_BULK_SOC);
	udl_cmd_insert_int_1(cb, UDL_BULK_CMD_FB_WRITE | UDL_BULK_CMD_FB_WORD);
	udl_cmd_insert_int_3(cb, off);
	udl_cmd_insert_int_1(cb, pixels);
	udl_cmd_insert_buf_le16(cb, buf, 2 * pixels);
	udl_cmd_buf_send(sc, cb);

	return (0);
}

static int
udl_cmd_buf_copy_le16(struct udl_softc *sc, uint32_t src, uint32_t dst,
    uint8_t pixels, int flags)
{
	struct udl_cmd_buf *cb;

	cb = udl_cmd_buf_alloc(sc, flags);
	if (cb == NULL)
		return (EAGAIN);

	udl_cmd_insert_int_1(cb, UDL_BULK_SOC);
	udl_cmd_insert_int_1(cb, UDL_BULK_CMD_FB_COPY | UDL_BULK_CMD_FB_WORD);
	udl_cmd_insert_int_3(cb, dst);
	udl_cmd_insert_int_1(cb, pixels);
	udl_cmd_insert_int_3(cb, src);
	udl_cmd_buf_send(sc, cb);

	return (0);
}
