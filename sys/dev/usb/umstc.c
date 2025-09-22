/*	$OpenBSD: umstc.c,v 1.8 2024/05/23 03:21:09 jsg Exp $ */

/*
 * Copyright (c) 2020 joshua stein <jcs@jcs.org>
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
 * Microsoft Surface Type Cover driver to respond to F1-F7 keys, but also to
 * keep the USB HID pipes open or else the Type Cover will detach and reattach
 * each time one of these buttons is pressed.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/atomic.h>
#include <sys/task.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbhid.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdi_util.h>
#include <dev/usb/usbdevs.h>
#include <dev/usb/uhidev.h>

#include <dev/wscons/wsdisplayvar.h>

#include "audio.h"
#include "wsdisplay.h"

struct umstc_softc {
	struct uhidev	sc_hdev;
	struct task	sc_brightness_task;
	int		sc_brightness_steps;
};

void	umstc_intr(struct uhidev *addr, void *ibuf, u_int len);
int	umstc_match(struct device *, void *, void *);
void	umstc_attach(struct device *, struct device *, void *);
int	umstc_detach(struct device *, int flags);
void	umstc_brightness_task(void *);

extern int wskbd_set_mixervolume(long, long);

struct cfdriver umstc_cd = {
	NULL, "umstc", DV_DULL
};

const struct cfattach umstc_ca = {
	sizeof(struct umstc_softc),
	umstc_match,
	umstc_attach,
	umstc_detach,
};

static const struct usb_devno umstc_devs[] = {
	{ USB_VENDOR_MICROSOFT, USB_PRODUCT_MICROSOFT_TYPECOVER },
	{ USB_VENDOR_MICROSOFT, USB_PRODUCT_MICROSOFT_TYPECOVER2 },
	{ USB_VENDOR_MICROSOFT, USB_PRODUCT_MICROSOFT_TYPECOVER3 },
};

int
umstc_match(struct device *parent, void *match, void *aux)
{
	struct uhidev_attach_arg *uha = (struct uhidev_attach_arg *)aux;
	int size;
	void *desc;

	if (UHIDEV_CLAIM_MULTIPLE_REPORTID(uha))
		return (UMATCH_NONE);

	if (!usb_lookup(umstc_devs, uha->uaa->vendor, uha->uaa->product))
		return UMATCH_NONE;

	uhidev_get_report_desc(uha->parent, &desc, &size);

	if (hid_is_collection(desc, size, uha->reportid,
	    HID_USAGE2(HUP_CONSUMER, HUC_CONTROL)))
		return UMATCH_IFACECLASS;

	return UMATCH_NONE;
}

void
umstc_attach(struct device *parent, struct device *self, void *aux)
{
	struct umstc_softc *sc = (struct umstc_softc *)self;
	struct uhidev_attach_arg *uha = (struct uhidev_attach_arg *)aux;
	struct usb_attach_arg *uaa = uha->uaa;
	int size, repid;
	void *desc;

	sc->sc_hdev.sc_intr = umstc_intr;
	sc->sc_hdev.sc_parent = uha->parent;
	sc->sc_hdev.sc_udev = uaa->device;
	sc->sc_hdev.sc_report_id = uha->reportid;

	usbd_set_idle(uha->parent->sc_udev, uha->parent->sc_ifaceno, 0, 0);

	uhidev_get_report_desc(uha->parent, &desc, &size);
	repid = uha->reportid;
	sc->sc_hdev.sc_isize = hid_report_size(desc, size, hid_input, repid);
	sc->sc_hdev.sc_osize = hid_report_size(desc, size, hid_output, repid);
	sc->sc_hdev.sc_fsize = hid_report_size(desc, size, hid_feature, repid);

	uhidev_open(&sc->sc_hdev);

	task_set(&sc->sc_brightness_task, umstc_brightness_task, sc);

	printf("\n");
}

int
umstc_detach(struct device *self, int flags)
{
	struct umstc_softc *sc = (struct umstc_softc *)self;

	task_del(systq, &sc->sc_brightness_task);

	uhidev_close(&sc->sc_hdev);

	return 0;
}

void
umstc_intr(struct uhidev *addr, void *buf, u_int len)
{
	struct umstc_softc *sc = (struct umstc_softc *)addr;
	int i;

	if (!len)
		return;

	switch (((unsigned char *)buf)[0]) {
	case HUC_PLAY_PAUSE:
		/*
		 * It would be nice to pass this through to userland but we'd
		 * need to attach a wskbd
		 */
		break;
	case HUC_MUTE:
#if NAUDIO > 0
		wskbd_set_mixervolume(0, 1);
#endif
		break;
	case HUC_VOL_INC:
#if NAUDIO > 0
		wskbd_set_mixervolume(1, 1);
#endif
		break;
	case HUC_VOL_DEC:
#if NAUDIO > 0
		wskbd_set_mixervolume(-1, 1);
#endif
		break;
	case 0x70: /* brightness down */
#if NWSDISPLAY > 0
		atomic_sub_int(&sc->sc_brightness_steps, 1);
		task_add(systq, &sc->sc_brightness_task);
#endif
		break;
	case 0x6f: /* brightness up */
#if NWSDISPLAY > 0
		atomic_add_int(&sc->sc_brightness_steps, 1);
		task_add(systq, &sc->sc_brightness_task);
#endif
		break;
	case 0:
		break;
	default:
		printf("%s: unhandled key ", sc->sc_hdev.sc_dev.dv_xname);
		for (i = 0; i < len; i++)
			printf(" 0x%02x", ((unsigned char *)buf)[i]);
		printf("\n");
	}
}

void
umstc_brightness_task(void *arg)
{
	struct umstc_softc *sc = arg;
	int steps = atomic_swap_uint(&sc->sc_brightness_steps, 0);
	int dir = 1;

	if (steps < 0) {
		steps = -steps;
		dir = -1;
	}
	
	while (steps--)
		wsdisplay_brightness_step(NULL, dir);
}
