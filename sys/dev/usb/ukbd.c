/*	$OpenBSD: ukbd.c,v 1.91 2025/08/14 14:39:44 deraadt Exp $	*/
/*      $NetBSD: ukbd.c,v 1.85 2003/03/11 16:44:00 augustss Exp $        */

/*
 * Copyright (c) 2010 Miodrag Vallat.
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
 * Copyright (c) 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Lennart Augustsson (lennart@augustsson.net) at
 * Carlstedt Research & Technology.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * HID spec: https://www.usb.org/sites/default/files/hid1_11.pdf
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/timeout.h>
#include <sys/device.h>

#include <machine/bus.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbhid.h>

#include <dev/usb/usbdi.h>
#include <dev/usb/usbdivar.h> /* needs_reattach() */
#include <dev/usb/usbdi_util.h>
#include <dev/usb/usbdevs.h>
#include <dev/usb/usb_quirks.h>
#include <dev/usb/uhidev.h>
#include <dev/usb/ukbdvar.h>

#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wskbdvar.h>
#include <dev/wscons/wsksymdef.h>
#include <dev/wscons/wsksymvar.h>

#include <dev/hid/hidkbdsc.h>

#ifdef UKBD_DEBUG
#define DPRINTF(x)	do { if (ukbddebug) printf x; } while (0)
#define DPRINTFN(n,x)	do { if (ukbddebug>(n)) printf x; } while (0)
int	ukbddebug = 0;
#else
#define DPRINTF(x)
#define DPRINTFN(n,x)
#endif

const kbd_t ukbd_countrylayout[1 + HCC_MAX] = {
	(kbd_t)-1,
	(kbd_t)-1,	/* arabic */
	KB_BE,		/* belgian */
	(kbd_t)-1,	/* canadian bilingual */
	KB_CF,		/* canadian french */
	(kbd_t)-1,	/* czech */
	KB_DK,		/* danish */
	(kbd_t)-1,	/* finnish */
	KB_FR,		/* french */
	KB_DE,		/* german */
	(kbd_t)-1,	/* greek */
	(kbd_t)-1,	/* hebrew */
	KB_HU,		/* hungary */
	(kbd_t)-1,	/* international (iso) */
	KB_IT,		/* italian */
	KB_JP,		/* japanese (katakana) */
	(kbd_t)-1,	/* korean */
	KB_LA,		/* latin american */
	(kbd_t)-1,	/* netherlands/dutch */
	KB_NO,		/* norwegian */
	(kbd_t)-1,	/* persian (farsi) */
	KB_PL,		/* polish */
	KB_PT,		/* portuguese */
	KB_RU,		/* russian */
	(kbd_t)-1,	/* slovakia */
	KB_ES,		/* spanish */
	KB_SV,		/* swedish */
	KB_SF,		/* swiss french */
	KB_SG,		/* swiss german */
	(kbd_t)-1,	/* switzerland */
	(kbd_t)-1,	/* taiwan */
	KB_TR,		/* turkish Q */
	KB_UK,		/* uk */
	KB_US,		/* us */
	(kbd_t)-1,	/* yugoslavia */
	(kbd_t)-1	/* turkish F */
};

struct ukbd_softc {
	struct uhidev		sc_hdev;
#define sc_ledsize		sc_hdev.sc_osize

	struct hidkbd		sc_kbd;
	int			sc_spl;

#ifdef DDB
	struct timeout		sc_ddb;	/* for entering DDB */
#endif
};

void	ukbd_cngetc(void *, u_int *, int *);
void	ukbd_cnpollc(void *, int);
void	ukbd_cnbell(void *, u_int, u_int, u_int);
void	ukbd_debugger(void *);

const struct wskbd_consops ukbd_consops = {
	ukbd_cngetc,
	ukbd_cnpollc,
	ukbd_cnbell,
#ifdef DDB
	ukbd_debugger,
#endif
};

void	ukbd_intr(struct uhidev *addr, void *ibuf, u_int len);

void	ukbd_db_enter(void *);
int	ukbd_enable(void *, int);
void	ukbd_set_leds(void *, int);
int	ukbd_ioctl(void *, u_long, caddr_t, int, struct proc *);

const struct wskbd_accessops ukbd_accessops = {
	ukbd_enable,
	ukbd_set_leds,
	ukbd_ioctl,
};

int	ukbd_match(struct device *, void *, void *);
void	ukbd_attach(struct device *, struct device *, void *);
int	ukbd_detach(struct device *, int);

struct cfdriver ukbd_cd = {
	NULL, "ukbd", DV_DULL
};

const struct cfattach ukbd_ca = {
	sizeof(struct ukbd_softc), ukbd_match, ukbd_attach, ukbd_detach
};

#ifdef __loongson__
void	ukbd_gdium_munge(void *, uint8_t *, u_int);
#endif

const struct usb_devno ukbd_never_console[] = {
	/* Apple HID-proxy is always detected before any real USB keyboard */
	{ USB_VENDOR_APPLE, USB_PRODUCT_APPLE_BLUETOOTH_HCI },
	/* ugold(4) devices, which also present themselves as ukbd */
	{ USB_VENDOR_MICRODIA, USB_PRODUCT_MICRODIA_TEMPER },
	{ USB_VENDOR_MICRODIA, USB_PRODUCT_MICRODIA_TEMPERHUM },
	{ USB_VENDOR_PCSENSORS, USB_PRODUCT_PCSENSORS_TEMPER },
	{ USB_VENDOR_RDING, USB_PRODUCT_RDING_TEMPER },
	{ USB_VENDOR_WCH2, USB_PRODUCT_WCH2_TEMPER },
};

int
ukbd_match(struct device *parent, void *match, void *aux)
{
	struct uhidev_attach_arg *uha = (struct uhidev_attach_arg *)aux;
	int size;
	void *desc;

	/*
	 * Most Yubikey have OTP enabled by default, and the feature
	 * is difficult to disable.  Policy decision: Don't attach
	 * as a keyboard.
	 */
	if (uha->uaa->vendor == USB_VENDOR_YUBICO)
		return (UMATCH_NONE);

	if (UHIDEV_CLAIM_MULTIPLE_REPORTID(uha))
		return (UMATCH_NONE);

	uhidev_get_report_desc(uha->parent, &desc, &size);
	if (!hid_is_collection(desc, size, uha->reportid,
	    HID_USAGE2(HUP_GENERIC_DESKTOP, HUG_KEYBOARD)))
		return (UMATCH_NONE);

	return (UMATCH_IFACECLASS);
}

void
ukbd_attach(struct device *parent, struct device *self, void *aux)
{
	struct ukbd_softc *sc = (struct ukbd_softc *)self;
	struct hidkbd *kbd = &sc->sc_kbd;
	struct uhidev_attach_arg *uha = (struct uhidev_attach_arg *)aux;
	struct usb_hid_descriptor *hid;
	u_int32_t quirks, qflags = 0;
	int dlen, repid;
	int console = 1;
	void *desc;
	kbd_t layout = (kbd_t)-1;

	sc->sc_hdev.sc_intr = ukbd_intr;
	sc->sc_hdev.sc_parent = uha->parent;
	sc->sc_hdev.sc_udev = uha->uaa->device;
	sc->sc_hdev.sc_report_id = uha->reportid;

	usbd_set_idle(uha->parent->sc_udev, uha->parent->sc_ifaceno, 0, 0);

	uhidev_get_report_desc(uha->parent, &desc, &dlen);
	repid = uha->reportid;
	sc->sc_hdev.sc_isize = hid_report_size(desc, dlen, hid_input, repid);
	sc->sc_hdev.sc_osize = hid_report_size(desc, dlen, hid_output, repid);
	sc->sc_hdev.sc_fsize = hid_report_size(desc, dlen, hid_feature, repid);

	 /*
	  * Do not allow unwanted devices to claim the console.
	  */
	if (usb_lookup(ukbd_never_console, uha->uaa->vendor, uha->uaa->product))
		console = 0;

	quirks = usbd_get_quirks(sc->sc_hdev.sc_udev)->uq_flags;
	if (quirks & UQ_SPUR_BUT_UP)
		qflags |= HIDKBD_SPUR_BUT_UP;

	if (hidkbd_attach(self, kbd, console, qflags, repid, desc, dlen) != 0)
		return;

	if (uha->uaa->vendor == USB_VENDOR_APPLE) {
		if (hid_locate(desc, dlen, HID_USAGE2(HUP_APPLE, HUG_FN_KEY),
		    uha->reportid, hid_input, &kbd->sc_fn, &qflags)) {
			if (qflags & HIO_VARIABLE) {
				switch (uha->uaa->product) {
				case USB_PRODUCT_APPLE_FOUNTAIN_ISO:
				case USB_PRODUCT_APPLE_GEYSER_ISO:
				case USB_PRODUCT_APPLE_GEYSER3_ISO:
				case USB_PRODUCT_APPLE_WELLSPRING6_ISO:
				case USB_PRODUCT_APPLE_WELLSPRING8_ISO:
					kbd->sc_munge = hidkbd_apple_iso_munge;
					break;
				case USB_PRODUCT_APPLE_WELLSPRING_ISO:
				case USB_PRODUCT_APPLE_WELLSPRING4_ISO:
				case USB_PRODUCT_APPLE_WELLSPRING4A_ISO:
					kbd->sc_munge = hidkbd_apple_iso_mba_munge;
					break;
				case USB_PRODUCT_APPLE_WELLSPRING_ANSI:
				case USB_PRODUCT_APPLE_WELLSPRING_JIS:
				case USB_PRODUCT_APPLE_WELLSPRING4_ANSI:
				case USB_PRODUCT_APPLE_WELLSPRING4_JIS:
				case USB_PRODUCT_APPLE_WELLSPRING4A_ANSI:
				case USB_PRODUCT_APPLE_WELLSPRING4A_JIS:
					kbd->sc_munge = hidkbd_apple_mba_munge;
					break;
				default:
					kbd->sc_munge = hidkbd_apple_munge;
					break;
				}
			}
		}
	}

	if (uha->uaa->vendor == USB_VENDOR_TOPRE &&
	    uha->uaa->product == USB_PRODUCT_TOPRE_HHKB) {
		/* ignore country code on purpose */
	} else {
		usb_interface_descriptor_t *id;

		id = usbd_get_interface_descriptor(uha->uaa->iface);
		hid = usbd_get_hid_descriptor(uha->uaa->device, id);

		if (hid->bCountryCode <= HCC_MAX)
			layout = ukbd_countrylayout[hid->bCountryCode];
#ifdef DIAGNOSTIC
		if (hid->bCountryCode != 0)
			printf(", country code %d", hid->bCountryCode);
#endif
	}
	if (layout == (kbd_t)-1) {
#ifdef UKBD_LAYOUT
		layout = UKBD_LAYOUT;
#else
		layout = KB_US | KB_DEFAULT;
#endif
	}

	printf("\n");

#ifdef __loongson__
	if (uha->uaa->vendor == USB_VENDOR_CYPRESS &&
	    uha->uaa->product == USB_PRODUCT_CYPRESS_LPRDK)
		kbd->sc_munge = ukbd_gdium_munge;
#endif

	if (kbd->sc_console_keyboard) {
		extern struct wskbd_mapdata ukbd_keymapdata;

		DPRINTF(("ukbd_attach: console keyboard sc=%p\n", sc));
		ukbd_keymapdata.layout = layout;
		wskbd_cnattach(&ukbd_consops, sc, &ukbd_keymapdata);
		ukbd_enable(sc, 1);
	}

	/* Flash the leds; no real purpose, just shows we're alive. */
	ukbd_set_leds(sc, WSKBD_LED_SCROLL | WSKBD_LED_NUM |
		          WSKBD_LED_CAPS | WSKBD_LED_COMPOSE);
	usbd_delay_ms(sc->sc_hdev.sc_udev, 400);
	ukbd_set_leds(sc, 0);

	hidkbd_attach_wskbd(kbd, layout, &ukbd_accessops);

#ifdef DDB
	timeout_set(&sc->sc_ddb, ukbd_db_enter, sc);
#endif
}

int
ukbd_detach(struct device *self, int flags)
{
	struct ukbd_softc *sc = (struct ukbd_softc *)self;
	struct hidkbd *kbd = &sc->sc_kbd;
	int rv;

	rv = hidkbd_detach(kbd, flags);

	/* The console keyboard does not get a disable call, so check pipe. */
	if (sc->sc_hdev.sc_state & UHIDEV_OPEN)
		uhidev_close(&sc->sc_hdev);

	return (rv);
}

void
ukbd_intr(struct uhidev *addr, void *ibuf, u_int len)
{
	struct ukbd_softc *sc = (struct ukbd_softc *)addr;
	struct hidkbd *kbd = &sc->sc_kbd;

	if (kbd->sc_enabled != 0)
		hidkbd_input(kbd, (uint8_t *)ibuf, len);
}

int
ukbd_enable(void *v, int on)
{
	struct ukbd_softc *sc = v;
	struct hidkbd *kbd = &sc->sc_kbd;
	int rv;

	if (on && usbd_is_dying(sc->sc_hdev.sc_udev))
		return EIO;

	if ((rv = hidkbd_enable(kbd, on)) != 0)
		return rv;

	if (on) {
		return uhidev_open(&sc->sc_hdev);
	} else {
		uhidev_close(&sc->sc_hdev);
		return 0;
	}
}

void
ukbd_set_leds(void *v, int leds)
{
	struct ukbd_softc *sc = v;
	struct hidkbd *kbd = &sc->sc_kbd;
	u_int8_t res;

	if (usbd_is_dying(sc->sc_hdev.sc_udev))
		return;

	if (sc->sc_ledsize && hidkbd_set_leds(kbd, leds, &res) != 0)
		uhidev_set_report_async(sc->sc_hdev.sc_parent,
		    UHID_OUTPUT_REPORT, sc->sc_hdev.sc_report_id, &res, 1);
}

int
ukbd_ioctl(void *v, u_long cmd, caddr_t data, int flag, struct proc *p)
{
	struct ukbd_softc *sc = v;
	struct hidkbd *kbd = &sc->sc_kbd;
	int rc;

	switch (cmd) {
	case WSKBDIO_GTYPE:
		*(int *)data = WSKBD_TYPE_USB;
		return (0);
	case WSKBDIO_SETLEDS:
		ukbd_set_leds(v, *(int *)data);
		return (0);
	default:
		rc = uhidev_ioctl(&sc->sc_hdev, cmd, data, flag, p);
		if (rc != -1)
			return rc;
		else
			return hidkbd_ioctl(kbd, cmd, data, flag, p);
	}
}

/* Console interface. */
void
ukbd_cngetc(void *v, u_int *type, int *data)
{
	struct ukbd_softc *sc = v;
	struct hidkbd *kbd = &sc->sc_kbd;

	DPRINTFN(0,("ukbd_cngetc: enter\n"));
	kbd->sc_polling = 1;
	while (kbd->sc_npollchar <= 0)
		usbd_dopoll(sc->sc_hdev.sc_udev);
	kbd->sc_polling = 0;
	hidkbd_cngetc(kbd, type, data);
	DPRINTFN(0,("ukbd_cngetc: return 0x%02x\n", *data));
}

void
ukbd_cnpollc(void *v, int on)
{
	struct ukbd_softc *sc = v;

	DPRINTFN(2,("ukbd_cnpollc: sc=%p on=%d\n", v, on));

	if (on)
		sc->sc_spl = splusb();
	else
		splx(sc->sc_spl);
	usbd_set_polling(sc->sc_hdev.sc_udev, on);
}

void
ukbd_cnbell(void *v, u_int pitch, u_int period, u_int volume)
{
	hidkbd_bell(pitch, period, volume, 1);
}

#ifdef DDB
void
ukbd_debugger(void *v)
{
	struct ukbd_softc *sc = v;

	/*
	 * For the console keyboard we can't deliver CTL-ALT-ESC
	 * from the interrupt routine.  Doing so would start
	 * polling from inside the interrupt routine and that
	 * loses bigtime.
	 */
	timeout_add(&sc->sc_ddb, 1);
}

void
ukbd_db_enter(void *xsc)
{
	db_enter();
}
#endif

int
ukbd_cnattach(void)
{
	struct ukbd_softc *sc;
	int i;

	/*
	 * XXX USB requires too many parts of the kernel to be running
	 * XXX in order to work, so we can't do much for the console
	 * XXX keyboard until autoconfiguration has run its course.
	 */
	hidkbd_is_console = 1;

	if (!cold) {
		/*
		 * When switching console dynamically force all USB keyboards
		 * to re-attach and possibly became the 'console' keyboard.
		 */
		for (i = 0; i < ukbd_cd.cd_ndevs; i++) {
			if ((sc = ukbd_cd.cd_devs[i]) != NULL) {
				usb_needs_reattach(sc->sc_hdev.sc_udev);
				break;
			}
		}
	}

	return (0);
}

#ifdef __loongson__
/*
 * Software Fn- translation for Gdium Liberty keyboard.
 */
#define	GDIUM_FN_CODE	0x82
void
ukbd_gdium_munge(void *vsc, uint8_t *ibuf, u_int ilen)
{
	struct ukbd_softc *sc = vsc;
	struct hidkbd *kbd = &sc->sc_kbd;
	uint8_t *pos, *spos, *epos, xlat;
	int fn;

	static const struct hidkbd_translation gdium_fn_trans[] = {
#ifdef notyet
		{ 58, 0 },	/* F1 -> toggle camera */
		{ 59, 0 },	/* F2 -> toggle wireless */
#endif
		{ 60, 127 },	/* F3 -> audio mute */
		{ 61, 128 },	/* F4 -> audio raise */
		{ 62, 129 },	/* F5 -> audio lower */
#ifdef notyet
		{ 63, 0 },	/* F6 -> toggle ext. video */
		{ 64, 0 },	/* F7 -> toggle mouse */
		{ 65, 0 },	/* F8 -> brightness up */
		{ 66, 0 },	/* F9 -> brightness down */
		{ 67, 0 },	/* F10 -> suspend */
		{ 68, 0 },	/* F11 -> user1 */
		{ 69, 0 },	/* F12 -> user2 */
		{ 70, 0 },	/* print screen -> sysrq */
#endif
		{ 76, 71 },	/* delete -> scroll lock */
		{ 81, 78 },	/* down -> page down */
		{ 82, 75 }	/* up -> page up */
	};

	spos = ibuf + kbd->sc_keycodeloc.pos / 8;
	epos = spos + kbd->sc_nkeycode;

	/*
	 * Check for Fn key being down and remove it from the report.
	 */

	fn = 0;
	for (pos = spos; pos != epos; pos++)
		if (*pos == GDIUM_FN_CODE) {
			fn = 1;
			*pos = 0;
			break;
		}

	/*
	 * Rewrite keycodes on the fly to perform Fn-key translation.
	 * Keycodes without a translation are passed unaffected.
	 */

	if (fn != 0)
		for (pos = spos; pos != epos; pos++) {
			xlat = hidkbd_translate(gdium_fn_trans,
			    nitems(gdium_fn_trans), *pos);
			if (xlat != 0)
				*pos = xlat;
		}

}
#endif
