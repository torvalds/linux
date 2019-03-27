/*-
 * Copyright (c) 2014-2017 Vladimir Kondratyev <wulf@FreeBSD.org>
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
 * MS Windows 7/8/10 compatible USB HID Multi-touch Device driver.
 * https://msdn.microsoft.com/en-us/library/windows/hardware/jj151569(v=vs.85).aspx
 * https://www.kernel.org/doc/Documentation/input/multi-touch-protocol.txt
 */

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/stddef.h>
#include <sys/sysctl.h>
#include <sys/systm.h>

#include "usbdevs.h"
#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdi_util.h>
#include <dev/usb/usbhid.h>

#include <dev/usb/quirk/usb_quirk.h>

#include <dev/evdev/evdev.h>
#include <dev/evdev/input.h>

#define	USB_DEBUG_VAR wmt_debug
#include <dev/usb/usb_debug.h>

#ifdef USB_DEBUG
static int wmt_debug = 0;

static SYSCTL_NODE(_hw_usb, OID_AUTO, wmt, CTLFLAG_RW, 0,
    "USB MSWindows 7/8/10 compatible Multi-touch Device");
SYSCTL_INT(_hw_usb_wmt, OID_AUTO, debug, CTLFLAG_RWTUN,
    &wmt_debug, 1, "Debug level");
#endif

#define	WMT_BSIZE	1024	/* bytes, buffer size */

enum {
	WMT_INTR_DT,
	WMT_N_TRANSFER,
};

enum {
	WMT_TIP_SWITCH,
#define	WMT_SLOT	WMT_TIP_SWITCH
	WMT_WIDTH,
#define	WMT_MAJOR	WMT_WIDTH
	WMT_HEIGHT,
#define WMT_MINOR	WMT_HEIGHT
	WMT_ORIENTATION,
	WMT_X,
	WMT_Y,
	WMT_CONTACTID,
	WMT_PRESSURE,
	WMT_IN_RANGE,
	WMT_CONFIDENCE,
	WMT_TOOL_X,
	WMT_TOOL_Y,
	WMT_N_USAGES,
};

#define	WMT_NO_CODE	(ABS_MAX + 10)
#define	WMT_NO_USAGE	-1

struct wmt_hid_map_item {
	char		name[5];
	int32_t 	usage;		/* HID usage */
	uint32_t	code;		/* Evdev event code */
	bool		required;	/* Required for MT Digitizers */
};

static const struct wmt_hid_map_item wmt_hid_map[WMT_N_USAGES] = {

	[WMT_TIP_SWITCH] = {	/* WMT_SLOT */
		.name = "TIP",
		.usage = HID_USAGE2(HUP_DIGITIZERS, HUD_TIP_SWITCH),
		.code = ABS_MT_SLOT,
		.required = true,
	},
	[WMT_WIDTH] = {		/* WMT_MAJOR */
		.name = "WDTH",
		.usage = HID_USAGE2(HUP_DIGITIZERS, HUD_WIDTH),
		.code = ABS_MT_TOUCH_MAJOR,
		.required = false,
	},
	[WMT_HEIGHT] = {	/* WMT_MINOR */
		.name = "HGHT",
		.usage = HID_USAGE2(HUP_DIGITIZERS, HUD_HEIGHT),
		.code = ABS_MT_TOUCH_MINOR,
		.required = false,
	},
	[WMT_ORIENTATION] = {
		.name = "ORIE",
		.usage = WMT_NO_USAGE,
		.code = ABS_MT_ORIENTATION,
		.required = false,
	},
	[WMT_X] = {
		.name = "X",
		.usage = HID_USAGE2(HUP_GENERIC_DESKTOP, HUG_X),
		.code = ABS_MT_POSITION_X,
		.required = true,
	},
	[WMT_Y] = {
		.name = "Y",
		.usage = HID_USAGE2(HUP_GENERIC_DESKTOP, HUG_Y),
		.code = ABS_MT_POSITION_Y,
		.required = true,
	},
	[WMT_CONTACTID] = {
		.name = "C_ID",
		.usage = HID_USAGE2(HUP_DIGITIZERS, HUD_CONTACTID),
		.code = ABS_MT_TRACKING_ID,
		.required = true,
	},
	[WMT_PRESSURE] = {
		.name = "PRES",
		.usage = HID_USAGE2(HUP_DIGITIZERS, HUD_TIP_PRESSURE),
		.code = ABS_MT_PRESSURE,
		.required = false,
	},
	[WMT_IN_RANGE] = {
		.name = "RANG",
		.usage = HID_USAGE2(HUP_DIGITIZERS, HUD_IN_RANGE),
		.code = ABS_MT_DISTANCE,
		.required = false,
	},
	[WMT_CONFIDENCE] = {
		.name = "CONF",
		.usage = HID_USAGE2(HUP_DIGITIZERS, HUD_CONFIDENCE),
		.code = WMT_NO_CODE,
		.required = false,
	},
	[WMT_TOOL_X] = {	/* Shares HID usage with WMT_X */
		.name = "TL_X",
		.usage = HID_USAGE2(HUP_GENERIC_DESKTOP, HUG_X),
		.code = ABS_MT_TOOL_X,
		.required = false,
	},
	[WMT_TOOL_Y] = {	/* Shares HID usage with WMT_Y */
		.name = "TL_Y",
		.usage = HID_USAGE2(HUP_GENERIC_DESKTOP, HUG_Y),
		.code = ABS_MT_TOOL_Y,
		.required = false,
	},
};

struct wmt_absinfo {
	int32_t			min;
	int32_t			max;
	int32_t			res;
};

struct wmt_softc
{
	device_t		dev;
	struct mtx		mtx;
	struct wmt_absinfo	ai[WMT_N_USAGES];
	struct hid_location	locs[MAX_MT_SLOTS][WMT_N_USAGES];
	struct hid_location	nconts_loc;

	struct usb_xfer		*xfer[WMT_N_TRANSFER];
	struct evdev_dev	*evdev;

	uint32_t		slot_data[WMT_N_USAGES];
	uint32_t		caps;
	uint32_t		isize;
	uint32_t		nconts_max;
	uint8_t			report_id;

	struct hid_location	cont_max_loc;
	uint32_t		cont_max_rlen;
	uint8_t			cont_max_rid;
	uint32_t		thqa_cert_rlen;
	uint8_t			thqa_cert_rid;

	uint8_t			buf[WMT_BSIZE] __aligned(4);
};

#define	USAGE_SUPPORTED(caps, usage)	((caps) & (1 << (usage)))
#define	WMT_FOREACH_USAGE(caps, usage)			\
	for ((usage) = 0; (usage) < WMT_N_USAGES; ++(usage))	\
		if (USAGE_SUPPORTED((caps), (usage)))

static bool wmt_hid_parse(struct wmt_softc *, const void *, uint16_t);
static void wmt_cont_max_parse(struct wmt_softc *, const void *, uint16_t);

static usb_callback_t	wmt_intr_callback;

static device_probe_t	wmt_probe;
static device_attach_t	wmt_attach;
static device_detach_t	wmt_detach;

#if __FreeBSD_version >= 1200077
static evdev_open_t	wmt_ev_open;
static evdev_close_t	wmt_ev_close;
#else
static evdev_open_t	wmt_ev_open_11;
static evdev_close_t	wmt_ev_close_11;
#endif

static const struct evdev_methods wmt_evdev_methods = {
#if __FreeBSD_version >= 1200077
	.ev_open = &wmt_ev_open,
	.ev_close = &wmt_ev_close,
#else
	.ev_open = &wmt_ev_open_11,
	.ev_close = &wmt_ev_close_11,
#endif
};

static const struct usb_config wmt_config[WMT_N_TRANSFER] = {

	[WMT_INTR_DT] = {
		.type = UE_INTERRUPT,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_IN,
		.flags = { .pipe_bof = 1, .short_xfer_ok = 1 },
		.bufsize = WMT_BSIZE,
		.callback = &wmt_intr_callback,
	},
};

static int
wmt_probe(device_t dev)
{
	struct usb_attach_arg *uaa = device_get_ivars(dev);
	void *d_ptr;
	uint16_t d_len;
	int err;

	if (uaa->usb_mode != USB_MODE_HOST)
		return (ENXIO);

	if (uaa->info.bInterfaceClass != UICLASS_HID)
		return (ENXIO);

	if (usb_test_quirk(uaa, UQ_WMT_IGNORE))
		return (ENXIO);

	err = usbd_req_get_hid_desc(uaa->device, NULL,
	    &d_ptr, &d_len, M_TEMP, uaa->info.bIfaceIndex);
	if (err)
		return (ENXIO);

	if (wmt_hid_parse(NULL, d_ptr, d_len))
		err = BUS_PROBE_DEFAULT;
	else
		err = ENXIO;

	free(d_ptr, M_TEMP);
	return (err);
}

static int
wmt_attach(device_t dev)
{
	struct usb_attach_arg *uaa = device_get_ivars(dev);
	struct wmt_softc *sc = device_get_softc(dev);
	void *d_ptr;
	uint16_t d_len;
	size_t i;
	int err;
	bool hid_ok;

	device_set_usb_desc(dev);
	sc->dev = dev;

	/* Get HID descriptor */
	err = usbd_req_get_hid_desc(uaa->device, NULL,
	    &d_ptr, &d_len, M_TEMP, uaa->info.bIfaceIndex);
	if (err) {
		DPRINTF("usbd_req_get_hid_desc error=%s\n", usbd_errstr(err));
		return (ENXIO);
	}

	hid_ok = wmt_hid_parse(sc, d_ptr, d_len);
	free(d_ptr, M_TEMP);
	if (!hid_ok) {
		DPRINTF("multi-touch HID descriptor not found\n");
		return (ENXIO);
	}

	/* Check HID report length */
	if (sc->isize <= 0 || sc->isize > WMT_BSIZE) {
		DPRINTF("Input size invalid or too large: %d\n", sc->isize);
		return (ENXIO);
	}

	/* Fetch and parse "Contact count maximum" feature report */
	if (sc->cont_max_rlen > 0 && sc->cont_max_rlen <= WMT_BSIZE) {
		err = usbd_req_get_report(uaa->device, NULL, sc->buf,
		    sc->cont_max_rlen, uaa->info.bIfaceIndex,
		    UHID_FEATURE_REPORT, sc->cont_max_rid);
		if (err == USB_ERR_NORMAL_COMPLETION)
			wmt_cont_max_parse(sc, sc->buf, sc->cont_max_rlen);
		else
			DPRINTF("usbd_req_get_report error=(%s)\n",
			    usbd_errstr(err));
	} else
		DPRINTF("Feature report %hhu size invalid or too large: %u\n",
		    sc->cont_max_rid, sc->cont_max_rlen);

	/* Fetch THQA certificate to enable some devices like WaveShare */
	if (sc->thqa_cert_rlen > 0 && sc->thqa_cert_rlen <= WMT_BSIZE &&
	    sc->thqa_cert_rid != sc->cont_max_rid)
		(void)usbd_req_get_report(uaa->device, NULL, sc->buf,
		    sc->thqa_cert_rlen, uaa->info.bIfaceIndex,
		    UHID_FEATURE_REPORT, sc->thqa_cert_rid);

	mtx_init(&sc->mtx, "wmt lock", NULL, MTX_DEF);

	err = usbd_transfer_setup(uaa->device, &uaa->info.bIfaceIndex,
	    sc->xfer, wmt_config, WMT_N_TRANSFER, sc, &sc->mtx);
	if (err != USB_ERR_NORMAL_COMPLETION) {
		DPRINTF("usbd_transfer_setup error=%s\n", usbd_errstr(err));
		goto detach;
	}

	sc->evdev = evdev_alloc();
	evdev_set_name(sc->evdev, device_get_desc(dev));
	evdev_set_phys(sc->evdev, device_get_nameunit(dev));
	evdev_set_id(sc->evdev, BUS_USB, uaa->info.idVendor,
	    uaa->info.idProduct, 0);
	evdev_set_serial(sc->evdev, usb_get_serial(uaa->device));
	evdev_set_methods(sc->evdev, sc, &wmt_evdev_methods);
	evdev_set_flag(sc->evdev, EVDEV_FLAG_MT_STCOMPAT);
	evdev_support_prop(sc->evdev, INPUT_PROP_DIRECT);
	evdev_support_event(sc->evdev, EV_SYN);
	evdev_support_event(sc->evdev, EV_ABS);
	WMT_FOREACH_USAGE(sc->caps, i) {
		if (wmt_hid_map[i].code != WMT_NO_CODE)
			evdev_support_abs(sc->evdev, wmt_hid_map[i].code, 0,
			    sc->ai[i].min, sc->ai[i].max, 0, 0, sc->ai[i].res);
	}

	err = evdev_register_mtx(sc->evdev, &sc->mtx);
	if (err)
		goto detach;

	return (0);

detach:
	wmt_detach(dev);
	return (ENXIO);
}

static int
wmt_detach(device_t dev)
{
	struct wmt_softc *sc = device_get_softc(dev);

	evdev_free(sc->evdev);
	usbd_transfer_unsetup(sc->xfer, WMT_N_TRANSFER);
	mtx_destroy(&sc->mtx);
	return (0);
}

static void
wmt_process_report(struct wmt_softc *sc, uint8_t *buf, int len)
{
	size_t usage;
	uint32_t *slot_data = sc->slot_data;
	uint32_t cont;
	uint32_t nconts;
	uint32_t width;
	uint32_t height;
	int32_t slot;

	nconts = hid_get_data_unsigned(buf, len, &sc->nconts_loc);

#ifdef USB_DEBUG
	DPRINTFN(6, "nconts = %u   ", (unsigned)nconts);
	if (wmt_debug >= 6) {
		WMT_FOREACH_USAGE(sc->caps, usage) {
			if (wmt_hid_map[usage].usage != WMT_NO_USAGE)
				printf(" %-4s", wmt_hid_map[usage].name);
		}
		printf("\n");
	}
#endif

	if (nconts > sc->nconts_max) {
		DPRINTF("Contact count overflow %u\n", (unsigned)nconts);
		nconts = sc->nconts_max;
	}

	/* Use protocol Type B for reporting events */
	for (cont = 0; cont < nconts; cont++) {

		bzero(slot_data, sizeof(sc->slot_data));
		WMT_FOREACH_USAGE(sc->caps, usage) {
			if (sc->locs[cont][usage].size > 0)
				slot_data[usage] = hid_get_data_unsigned(
				    buf, len, &sc->locs[cont][usage]);
		}

		slot = evdev_get_mt_slot_by_tracking_id(sc->evdev,
		    slot_data[WMT_CONTACTID]);

#ifdef USB_DEBUG
		DPRINTFN(6, "cont%01x: data = ", cont);
		if (wmt_debug >= 6) {
			WMT_FOREACH_USAGE(sc->caps, usage) {
				if (wmt_hid_map[usage].usage != WMT_NO_USAGE)
					printf("%04x ", slot_data[usage]);
			}
			printf("slot = %d\n", (int)slot);
		}
#endif

		if (slot == -1) {
			DPRINTF("Slot overflow for contact_id %u\n",
			    (unsigned)slot_data[WMT_CONTACTID]);
			continue;
		}

		if (slot_data[WMT_TIP_SWITCH] != 0 &&
		    !(USAGE_SUPPORTED(sc->caps, WMT_CONFIDENCE) &&
		      slot_data[WMT_CONFIDENCE] == 0)) {
			/* This finger is in proximity of the sensor */
			slot_data[WMT_SLOT] = slot;
			slot_data[WMT_IN_RANGE] = !slot_data[WMT_IN_RANGE];
			/* Divided by two to match visual scale of touch */
			width = slot_data[WMT_WIDTH] >> 1;
			height = slot_data[WMT_HEIGHT] >> 1;
			slot_data[WMT_ORIENTATION] = width > height;
			slot_data[WMT_MAJOR] = MAX(width, height);
			slot_data[WMT_MINOR] = MIN(width, height);

			WMT_FOREACH_USAGE(sc->caps, usage)
				if (wmt_hid_map[usage].code != WMT_NO_CODE)
					evdev_push_abs(sc->evdev,
					    wmt_hid_map[usage].code,
					    slot_data[usage]);
		} else {
			evdev_push_abs(sc->evdev, ABS_MT_SLOT, slot);
			evdev_push_abs(sc->evdev, ABS_MT_TRACKING_ID, -1);
		}
	}
	evdev_sync(sc->evdev);
}

static void
wmt_intr_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct wmt_softc *sc = usbd_xfer_softc(xfer);
	struct usb_page_cache *pc;
	uint8_t *buf = sc->buf;
	int len;

	usbd_xfer_status(xfer, &len, NULL, NULL, NULL);

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:
		pc = usbd_xfer_get_frame(xfer, 0);

		DPRINTFN(6, "sc=%p actlen=%d\n", sc, len);

		if (len >= (int)sc->isize || (len > 0 && sc->report_id != 0)) {
			/* Limit report length to the maximum */
			if (len > (int)sc->isize)
				len = sc->isize;

			usbd_copy_out(pc, 0, buf, len);

			/* Ignore irrelevant reports */
			if (sc->report_id && *buf != sc->report_id)
				goto tr_ignore;

			/* Make sure we don't process old data */
			if (len < sc->isize)
				bzero(buf + len, sc->isize - len);

			/* Strip leading "report ID" byte */
			if (sc->report_id) {
				len--;
				buf++;
			}

			wmt_process_report(sc, buf, len);
		} else {
tr_ignore:
			DPRINTF("Ignored transfer, %d bytes\n", len);
		}

	case USB_ST_SETUP:
tr_setup:
		usbd_xfer_set_frame_len(xfer, 0, usbd_xfer_max_len(xfer));
		usbd_transfer_submit(xfer);
		break;
	default:
		if (error != USB_ERR_CANCELLED) {
			/* Try clear stall first */
			usbd_xfer_set_stall(xfer);
			goto tr_setup;
		}
		break;
	}
}

static void
wmt_ev_close_11(struct evdev_dev *evdev, void *ev_softc)
{
	struct wmt_softc *sc = ev_softc;

	mtx_assert(&sc->mtx, MA_OWNED);
	usbd_transfer_stop(sc->xfer[WMT_INTR_DT]);
}

static int
wmt_ev_open_11(struct evdev_dev *evdev, void *ev_softc)
{
	struct wmt_softc *sc = ev_softc;

	mtx_assert(&sc->mtx, MA_OWNED);
	usbd_transfer_start(sc->xfer[WMT_INTR_DT]);

	return (0);
}

#if __FreeBSD_version >= 1200077
static int
wmt_ev_close(struct evdev_dev *evdev)
{
	struct wmt_softc *sc = evdev_get_softc(evdev);

	wmt_ev_close_11(evdev, sc);

	return (0);
}

static int
wmt_ev_open(struct evdev_dev *evdev)
{
	struct wmt_softc *sc = evdev_get_softc(evdev);

	return (wmt_ev_open_11(evdev, sc));

}
#endif

/* port of userland hid_report_size() from usbhid(3) to kernel */
static int
wmt_hid_report_size(const void *buf, uint16_t len, enum hid_kind k, uint8_t id)
{
	struct hid_data *d;
	struct hid_item h;
	uint32_t temp;
	uint32_t hpos;
	uint32_t lpos;
	int report_id = 0;

	hpos = 0;
	lpos = 0xFFFFFFFF;

	for (d = hid_start_parse(buf, len, 1 << k); hid_get_item(d, &h);) {
		if (h.kind == k && h.report_ID == id) {
			/* compute minimum */
			if (lpos > h.loc.pos)
				lpos = h.loc.pos;
			/* compute end position */
			temp = h.loc.pos + (h.loc.size * h.loc.count);
			/* compute maximum */
			if (hpos < temp)
				hpos = temp;
			if (h.report_ID != 0)
				report_id = 1;
		}
	}
	hid_end_parse(d);

	/* safety check - can happen in case of currupt descriptors */
	if (lpos > hpos)
		temp = 0;
	else
		temp = hpos - lpos;

	/* return length in bytes rounded up */
	return ((temp + 7) / 8 + report_id);
}

static bool
wmt_hid_parse(struct wmt_softc *sc, const void *d_ptr, uint16_t d_len)
{
	struct hid_item hi;
	struct hid_data *hd;
	size_t i;
	size_t cont = 0;
	uint32_t caps = 0;
	int32_t cont_count_max = 0;
	uint8_t report_id = 0;
	uint8_t cont_max_rid = 0;
	uint8_t thqa_cert_rid = 0;
	bool touch_coll = false;
	bool finger_coll = false;
	bool cont_count_found = false;
	bool scan_time_found = false;

#define WMT_HI_ABSOLUTE(hi)	\
	(((hi).flags & (HIO_CONST|HIO_VARIABLE|HIO_RELATIVE)) == HIO_VARIABLE)
#define	HUMS_THQA_CERT	0xC5

	/* Parse features for maximum contact count */
	hd = hid_start_parse(d_ptr, d_len, 1 << hid_feature);
	while (hid_get_item(hd, &hi)) {
		switch (hi.kind) {
		case hid_collection:
			if (hi.collevel == 1 && hi.usage ==
			    HID_USAGE2(HUP_DIGITIZERS, HUD_TOUCHSCREEN))
				touch_coll = true;
			break;
		case hid_endcollection:
			if (hi.collevel == 0 && touch_coll)
				touch_coll = false;
			break;
		case hid_feature:
			if (hi.collevel == 1 && touch_coll && hi.usage ==
			      HID_USAGE2(HUP_MICROSOFT, HUMS_THQA_CERT)) {
				thqa_cert_rid = hi.report_ID;
				break;
			}
			if (hi.collevel == 1 && touch_coll &&
			    WMT_HI_ABSOLUTE(hi) && hi.usage ==
			      HID_USAGE2(HUP_DIGITIZERS, HUD_CONTACT_MAX)) {
				cont_count_max = hi.logical_maximum;
				cont_max_rid = hi.report_ID;
				if (sc != NULL)
					sc->cont_max_loc = hi.loc;
			}
			break;
		default:
			break;
		}
	}
	hid_end_parse(hd);

	/* Maximum contact count is required usage */
	if (cont_max_rid == 0)
		return (false);

	touch_coll = false;

	/* Parse input for other parameters */
	hd = hid_start_parse(d_ptr, d_len, 1 << hid_input);
	while (hid_get_item(hd, &hi)) {
		switch (hi.kind) {
		case hid_collection:
			if (hi.collevel == 1 && hi.usage ==
			    HID_USAGE2(HUP_DIGITIZERS, HUD_TOUCHSCREEN))
				touch_coll = true;
			else if (touch_coll && hi.collevel == 2 &&
			    (report_id == 0 || report_id == hi.report_ID) &&
			    hi.usage == HID_USAGE2(HUP_DIGITIZERS, HUD_FINGER))
				finger_coll = true;
			break;
		case hid_endcollection:
			if (hi.collevel == 1 && finger_coll) {
				finger_coll = false;
				cont++;
			} else if (hi.collevel == 0 && touch_coll)
				touch_coll = false;
			break;
		case hid_input:
			/*
			 * Ensure that all usages are located within the same
			 * report and proper collection.
			 */
			if (WMT_HI_ABSOLUTE(hi) && touch_coll &&
			    (report_id == 0 || report_id == hi.report_ID))
				report_id = hi.report_ID;
			else
				break;

			if (hi.collevel == 1 && hi.usage ==
			    HID_USAGE2(HUP_DIGITIZERS, HUD_CONTACTCOUNT)) {
				cont_count_found = true;
				if (sc != NULL)
					sc->nconts_loc = hi.loc;
				break;
			}
			/* Scan time is required but clobbered by evdev */
			if (hi.collevel == 1 && hi.usage ==
			    HID_USAGE2(HUP_DIGITIZERS, HUD_SCAN_TIME)) {
				scan_time_found = true;
				break;
			}

			if (!finger_coll || hi.collevel != 2)
				break;
			if (sc == NULL && cont > 0)
				break;
			if (cont >= MAX_MT_SLOTS) {
				DPRINTF("Finger %zu ignored\n", cont);
				break;
			}

			for (i = 0; i < WMT_N_USAGES; i++) {
				if (hi.usage == wmt_hid_map[i].usage) {
					if (sc == NULL) {
						if (USAGE_SUPPORTED(caps, i))
							continue;
						caps |= 1 << i;
						break;
					}
					/*
					 * HUG_X usage is an array mapped to
					 * both ABS_MT_POSITION and ABS_MT_TOOL
					 * events. So don`t stop search if we
					 * already have HUG_X mapping done.
					 */
					if (sc->locs[cont][i].size)
						continue;
					sc->locs[cont][i] = hi.loc;
					/*
					 * Hid parser returns valid logical and
					 * physical sizes for first finger only
					 * at least on ElanTS 0x04f3:0x0012.
					 */
					if (cont > 0)
						break;
					caps |= 1 << i;
					sc->ai[i] = (struct wmt_absinfo) {
					    .max = hi.logical_maximum,
					    .min = hi.logical_minimum,
					    .res = hid_item_resolution(&hi),
					};
					break;
				}
			}
			break;
		default:
			break;
		}
	}
	hid_end_parse(hd);

	/* Check for required HID Usages */
	if (!cont_count_found || !scan_time_found || cont == 0)
		return (false);
	for (i = 0; i < WMT_N_USAGES; i++) {
		if (wmt_hid_map[i].required && !USAGE_SUPPORTED(caps, i))
			return (false);
	}

	/* Stop probing here */
	if (sc == NULL)
		return (true);

	/*
	 * According to specifications 'Contact Count Maximum' should be read
	 * from Feature Report rather than from HID descriptor. Set sane
	 * default value now to handle the case of 'Get Report' request failure
	 */
	if (cont_count_max < 1)
		cont_count_max = cont;

	/* Cap contact count maximum to MAX_MT_SLOTS */
	if (cont_count_max > MAX_MT_SLOTS)
		cont_count_max = MAX_MT_SLOTS;

	/* Set number of MT protocol type B slots */
	sc->ai[WMT_SLOT] = (struct wmt_absinfo) {
		.min = 0,
		.max = cont_count_max - 1,
		.res = 0,
	};

	/* Report touch orientation if both width and height are supported */
	if (USAGE_SUPPORTED(caps, WMT_WIDTH) &&
	    USAGE_SUPPORTED(caps, WMT_HEIGHT)) {
		caps |= (1 << WMT_ORIENTATION);
		sc->ai[WMT_ORIENTATION].max = 1;
	}

	sc->isize = wmt_hid_report_size(d_ptr, d_len, hid_input, report_id);
	sc->cont_max_rlen = wmt_hid_report_size(d_ptr, d_len, hid_feature,
	    cont_max_rid);
	if (thqa_cert_rid > 0)
		sc->thqa_cert_rlen = wmt_hid_report_size(d_ptr, d_len,
		    hid_feature, thqa_cert_rid);

	sc->report_id = report_id;
	sc->caps = caps;
	sc->nconts_max = cont;
	sc->cont_max_rid = cont_max_rid;
	sc->thqa_cert_rid = thqa_cert_rid;

	/* Announce information about the touch device */
	device_printf(sc->dev,
	    "%d contacts and [%s%s%s%s%s]. Report range [%d:%d] - [%d:%d]\n",
	    (int)cont_count_max,
	    USAGE_SUPPORTED(sc->caps, WMT_IN_RANGE) ? "R" : "",
	    USAGE_SUPPORTED(sc->caps, WMT_CONFIDENCE) ? "C" : "",
	    USAGE_SUPPORTED(sc->caps, WMT_WIDTH) ? "W" : "",
	    USAGE_SUPPORTED(sc->caps, WMT_HEIGHT) ? "H" : "",
	    USAGE_SUPPORTED(sc->caps, WMT_PRESSURE) ? "P" : "",
	    (int)sc->ai[WMT_X].min, (int)sc->ai[WMT_Y].min,
	    (int)sc->ai[WMT_X].max, (int)sc->ai[WMT_Y].max);
	return (true);
}

static void
wmt_cont_max_parse(struct wmt_softc *sc, const void *r_ptr, uint16_t r_len)
{
	uint32_t cont_count_max;

	cont_count_max = hid_get_data_unsigned((const uint8_t *)r_ptr + 1,
	    r_len - 1, &sc->cont_max_loc);
	if (cont_count_max > MAX_MT_SLOTS) {
		DPRINTF("Hardware reported %d contacts while only %d is "
		    "supported\n", (int)cont_count_max, MAX_MT_SLOTS);
		cont_count_max = MAX_MT_SLOTS;
	}
	/* Feature report is a primary source of 'Contact Count Maximum' */
	if (cont_count_max > 0 &&
	    cont_count_max != sc->ai[WMT_SLOT].max + 1) {
		sc->ai[WMT_SLOT].max = cont_count_max - 1;
		device_printf(sc->dev, "%d feature report contacts",
		    cont_count_max);
	}
}

static const STRUCT_USB_HOST_ID wmt_devs[] = {
	/* generic HID class w/o boot interface */
	{USB_IFACE_CLASS(UICLASS_HID),
	 USB_IFACE_SUBCLASS(0),},
};

static devclass_t wmt_devclass;

static device_method_t wmt_methods[] = {
	DEVMETHOD(device_probe, wmt_probe),
	DEVMETHOD(device_attach, wmt_attach),
	DEVMETHOD(device_detach, wmt_detach),

	DEVMETHOD_END
};

static driver_t wmt_driver = {
	.name = "wmt",
	.methods = wmt_methods,
	.size = sizeof(struct wmt_softc),
};

DRIVER_MODULE(wmt, uhub, wmt_driver, wmt_devclass, NULL, 0);
MODULE_DEPEND(wmt, usb, 1, 1, 1);
MODULE_DEPEND(wmt, evdev, 1, 1, 1);
MODULE_VERSION(wmt, 1);
USB_PNP_HOST_INFO(wmt_devs);
