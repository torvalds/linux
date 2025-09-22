/* $OpenBSD: hidmt.c,v 1.16 2025/07/21 21:46:40 bru Exp $ */
/*
 * HID multitouch driver for devices conforming to Windows Precision Touchpad
 * standard
 *
 * https://msdn.microsoft.com/en-us/library/windows/hardware/dn467314%28v=vs.85%29.aspx
 * https://docs.microsoft.com/en-us/windows-hardware/design/component-guidelines/touchscreen-packet-reporting-modes
 *
 * Copyright (c) 2016 joshua stein <jcs@openbsd.org>
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/device.h>
#include <sys/ioctl.h>
#include <sys/malloc.h>

#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wsmousevar.h>

#include <dev/hid/hid.h>
#include <dev/hid/hidmtvar.h>

/* #define HIDMT_DEBUG */

#ifdef HIDMT_DEBUG
#define DPRINTF(x) printf x
#else
#define DPRINTF(x)
#endif

#define IS_REPORT_LEVEL_USAGE(u) ((((u) >> 16) & 0xffff) == HUP_BUTTON || \
    (u) == HID_USAGE2(HUP_DIGITIZERS, HUD_CONTACTCOUNT))

#define HID_UNIT_CM	0x11
#define HID_UNIT_INCH	0x13

/*
 * Calculate the horizontal or vertical resolution, in device units per
 * millimeter.
 *
 * With the length unit specified by the descriptor (centimeter or inch),
 * the result is:
 *     (logical_maximum - logical_minimum) / ((physical_maximum -
 *         physical_minimum) * 10^unit_exponent)
 *
 * The descriptors should encode the unit exponent as a signed half-byte.
 * However, this function accepts the values from -8 to -1 in both the
 * 4-bit format and the usual encoding.  Other values beyond the 4-bit
 * range are treated as undefined.  Possibly a misinterpretation of
 * section 6.2.2.7 of the HID specification (v1.11) has been turned into
 * a standard here, see (from www.usb.org)
 *     HUTRR39: "HID Sensor Usage Tables", sect. 3.9, 3.10, 4.2.1
 * for an official exegesis and
 *     https://patchwork.kernel.org/patch/3033191
 * for details and a different view.
 */
int
hidmt_get_resolution(struct hid_item *h)
{
	int log_extent, phy_extent, exponent;

	if (h->unit != HID_UNIT_CM && h->unit != HID_UNIT_INCH)
		return (0);

	log_extent = h->logical_maximum - h->logical_minimum;
	phy_extent = h->physical_maximum - h->physical_minimum;
	if (log_extent <= 0 || phy_extent <= 0)
		return (0);

	exponent = h->unit_exponent;
	if (exponent < -8 || exponent > 15)		/* See above. */
		return (0);
	if (exponent > 7)
		exponent -= 16;

	for (; exponent < 0 && log_extent <= INT_MAX / 10; exponent++)
		log_extent *= 10;
	for (; exponent > 0 && phy_extent <= INT_MAX / 10; exponent--)
		phy_extent *= 10;
	if (exponent != 0)
		return (0);

	if (h->unit == HID_UNIT_INCH) {			/* Map inches to mm. */
		if ((phy_extent > INT_MAX / 127)
		    || (log_extent > INT_MAX / 5))
			return (0);
		log_extent *= 5;
		phy_extent *= 127;
	} else {					/* Map cm to mm. */
		if (phy_extent > INT_MAX / 10)
			return (0);
		phy_extent *= 10;
	}

	return (log_extent / phy_extent);
}

int
hidmt_setup(struct device *self, struct hidmt *mt, void *desc, int dlen)
{
	struct hid_location cap;
	int32_t d;
	uint8_t *rep;
	int capsize;

	struct hid_data *hd;
	struct hid_item h;

	mt->sc_device = self;
	mt->sc_rep_input_size = hid_report_size(desc, dlen, hid_input,
	    mt->sc_rep_input);

	mt->sc_minx = mt->sc_miny = mt->sc_maxx = mt->sc_maxy = 0;

	capsize = hid_report_size(desc, dlen, hid_feature, mt->sc_rep_cap);
	rep = malloc(capsize, M_DEVBUF, M_NOWAIT | M_ZERO);
	if (rep == NULL) {
		printf("\n%s: could not allocate capability report buffer\n",
		    self->dv_xname);
		return 1;
	}

	if (mt->hidev_report_type_conv == NULL)
		panic("no report type conversion function");

	if (mt->hidev_get_report(mt->sc_device,
	    mt->hidev_report_type_conv(hid_feature), mt->sc_rep_cap,
	    rep, capsize)) {
		printf("\n%s: failed getting capability report\n",
		    self->dv_xname);
		free(rep, M_DEVBUF, capsize);
		return 1;
	}

	/* find maximum number of contacts being reported per input report */
	mt->sc_num_contacts = HIDMT_MAX_CONTACTS;
	if (hid_locate(desc, dlen, HID_USAGE2(HUP_DIGITIZERS, HUD_CONTACT_MAX),
	    mt->sc_rep_cap, hid_feature, &cap, NULL)) {
		d = hid_get_udata(rep, capsize, &cap);
		if (d > HIDMT_MAX_CONTACTS)
			printf("\n%s: contacts %d > max %d\n", self->dv_xname,
			    d, HIDMT_MAX_CONTACTS);
		else
			mt->sc_num_contacts = d;
	}

	/* find whether this is a clickpad or not */
	if (hid_locate(desc, dlen, HID_USAGE2(HUP_DIGITIZERS, HUD_BUTTON_TYPE),
	    mt->sc_rep_cap, hid_feature, &cap, NULL)) {
		d = hid_get_udata(rep, capsize, &cap);
		mt->sc_clickpad = (d == 0);
	} else if (hid_locate(desc, dlen, HID_USAGE2(HUP_BUTTON, 1),
	    mt->sc_rep_input, hid_input, &cap, NULL) ||
	    !hid_locate(desc, dlen, HID_USAGE2(HUP_BUTTON, 2),
	    mt->sc_rep_input, hid_input, &cap, NULL) ||
	    !hid_locate(desc, dlen, HID_USAGE2(HUP_BUTTON, 3),
	    mt->sc_rep_input, hid_input, &cap, NULL)) {
		mt->sc_clickpad = 1;
	}
	/*
	 * Walk HID descriptor and store usages we care about to know what to
	 * pluck out of input reports.
	 */

	SIMPLEQ_INIT(&mt->sc_inputs);

	hd = hid_start_parse(desc, dlen, hid_input);
	while (hid_get_item(hd, &h)) {
		struct hidmt_data *input;

		if (h.report_ID != mt->sc_rep_input)
			continue;
		if (h.kind != hid_input)
			continue;

		switch (h.usage) {
		/* contact level usages */
		case HID_USAGE2(HUP_GENERIC_DESKTOP, HUG_X):
			if (h.logical_maximum - h.logical_minimum) {
				mt->sc_minx = h.logical_minimum;
				mt->sc_maxx = h.logical_maximum;
				mt->sc_resx = hidmt_get_resolution(&h);
			}
			break;
		case HID_USAGE2(HUP_GENERIC_DESKTOP, HUG_Y):
			if (h.logical_maximum - h.logical_minimum) {
				mt->sc_miny = h.logical_minimum;
				mt->sc_maxy = h.logical_maximum;
				mt->sc_resy = hidmt_get_resolution(&h);
			}
			break;
		case HID_USAGE2(HUP_DIGITIZERS, HUD_TIP_SWITCH):
		case HID_USAGE2(HUP_DIGITIZERS, HUD_CONFIDENCE):
		case HID_USAGE2(HUP_DIGITIZERS, HUD_WIDTH):
		case HID_USAGE2(HUP_DIGITIZERS, HUD_HEIGHT):
		case HID_USAGE2(HUP_DIGITIZERS, HUD_CONTACTID):

		/* report level usages */
		case HID_USAGE2(HUP_DIGITIZERS, HUD_CONTACTCOUNT):
		case HID_USAGE2(HUP_BUTTON, 0x01):
		case HID_USAGE2(HUP_BUTTON, 0x02):
		case HID_USAGE2(HUP_BUTTON, 0x03):
			break;
		default:
			continue;
		}

		input = malloc(sizeof(*input), M_DEVBUF, M_NOWAIT | M_ZERO);
		if (input == NULL) {
			printf("\n%s: could not allocate input report buffer\n",
			    self->dv_xname);
			hid_end_parse(hd);
			goto fail;
		}
		memcpy(&input->loc, &h.loc, sizeof(struct hid_location));
		input->usage = h.usage;

		SIMPLEQ_INSERT_TAIL(&mt->sc_inputs, input, entry);
	}
	hid_end_parse(hd);

	if (mt->sc_maxx <= 0 || mt->sc_maxy <= 0) {
		printf("\n%s: invalid max X/Y %d/%d\n", self->dv_xname,
		    mt->sc_maxx, mt->sc_maxy);
		goto fail;
	}

	if (hidmt_set_input_mode(mt, HIDMT_INPUT_MODE_MT_TOUCHPAD)) {
		printf("\n%s: switch to multitouch mode failed\n",
		    self->dv_xname);
		goto fail;
	}

	free(rep, M_DEVBUF, capsize);
	return 0;

fail:
	while (!SIMPLEQ_EMPTY(&mt->sc_inputs)) {
		struct hidmt_data *input = SIMPLEQ_FIRST(&mt->sc_inputs);
		SIMPLEQ_REMOVE_HEAD(&mt->sc_inputs, entry);
		free(input, M_DEVBUF, sizeof(*input));
	}

	free(rep, M_DEVBUF, capsize);
	return 1;
}

void
hidmt_configure(struct hidmt *mt)
{
	struct wsmousehw *hw;

	if (mt->sc_wsmousedev == NULL)
		return;

	hw = wsmouse_get_hw(mt->sc_wsmousedev);
	hw->type = WSMOUSE_TYPE_TOUCHPAD;
	hw->hw_type = (mt->sc_clickpad
	    ? WSMOUSEHW_CLICKPAD : WSMOUSEHW_TOUCHPAD);
	hw->x_min = mt->sc_minx;
	hw->x_max = mt->sc_maxx;
	hw->y_min = mt->sc_miny;
	hw->y_max = mt->sc_maxy;
	hw->h_res = mt->sc_resx;
	hw->v_res = mt->sc_resy;
	hw->mt_slots = HIDMT_MAX_CONTACTS;

	wsmouse_configure(mt->sc_wsmousedev, NULL, 0);
}

void
hidmt_attach(struct hidmt *mt, const struct wsmouse_accessops *ops)
{
	struct wsmousedev_attach_args a;

	printf(": %spad, %d contact%s\n",
	    (mt->sc_clickpad ? "click" : "touch"), mt->sc_num_contacts,
	    (mt->sc_num_contacts == 1 ? "" : "s"));

	a.accessops = ops;
	a.accesscookie = mt->sc_device;
	mt->sc_wsmousedev = config_found(mt->sc_device, &a, wsmousedevprint);
	hidmt_configure(mt);
}

int
hidmt_detach(struct hidmt *mt, int flags)
{
	int rv = 0;

	if (mt->sc_wsmousedev != NULL)
		rv = config_detach(mt->sc_wsmousedev, flags);

	return (rv);
}

int
hidmt_set_input_mode(struct hidmt *mt, uint16_t mode)
{
	if (mt->hidev_report_type_conv == NULL)
		panic("no report type conversion function");

	return mt->hidev_set_report(mt->sc_device,
	    mt->hidev_report_type_conv(hid_feature),
	    mt->sc_rep_config, &mode, sizeof(mode));
}

void
hidmt_input(struct hidmt *mt, uint8_t *data, u_int len)
{
	struct hidmt_data *hi;
	struct hidmt_contact hc;
	int32_t d, firstu = 0;
	int contactcount = 0, seencontacts = 0, tips = 0, buttons = 0, i, s, z;

	if (len != mt->sc_rep_input_size) {
		DPRINTF(("%s: %s: length %d not %d, ignoring\n",
		    mt->sc_device->dv_xname, __func__, len,
		    mt->sc_rep_input_size));
		return;
	}

	/*
	 * "In Parallel mode, devices report all contact information in a
	 * single packet. Each physical contact is represented by a logical
	 * collection that is embedded in the top-level collection."
	 *
	 * Since additional contacts that were not present will still be in the
	 * report with contactid=0 but contactids are zero-based, find
	 * contactcount first.
	 */
	SIMPLEQ_FOREACH(hi, &mt->sc_inputs, entry) {
		if (hi->usage == HID_USAGE2(HUP_DIGITIZERS, HUD_CONTACTCOUNT))
			contactcount = hid_get_udata(data, len, &hi->loc);
	}

	if (contactcount)
		mt->sc_cur_contactcount = contactcount;
	else {
		/*
		* "In Hybrid mode, the number of contacts that can be reported
		* in one report is less than the maximum number of contacts
		* that the device supports. For example, a device that supports
		* a maximum of 4 concurrent physical contacts, can set up its
		* top-level collection to deliver a maximum of two contacts in
		* one report. If four contact points are present, the device
		* can break these up into two serial reports that deliver two
		* contacts each.
		*
		* "When a device delivers data in this manner, the Contact
		* Count usage value in the first report should reflect the
		* total number of contacts that are being delivered in the
		* hybrid reports. The other serial reports should have a
		* contact count of zero (0)."
		*/
		contactcount = mt->sc_cur_contactcount;
	}

	if (!contactcount) {
		DPRINTF(("%s: %s: no contactcount in report\n",
		    mt->sc_device->dv_xname, __func__));
		return;
	}

	/*
	 * Walk through each input we know about and fetch its data from the
	 * report, storing it in a temporary contact.  Once we see our first
	 * usage again, we'll know we saw all usages being presented for that
	 * contact.
	 */
	bzero(&hc, sizeof(struct hidmt_contact));
	SIMPLEQ_FOREACH(hi, &mt->sc_inputs, entry) {
		d = hid_get_udata(data, len, &hi->loc);

		if (firstu && hi->usage == firstu) {
			if (seencontacts < contactcount) {
				hc.seen = 1;
				i = wsmouse_id_to_slot(
				    mt->sc_wsmousedev, hc.contactid);
				if (i >= 0)
					memcpy(&mt->sc_contacts[i], &hc,
					    sizeof(struct hidmt_contact));
				seencontacts++;
			}

			bzero(&hc, sizeof(struct hidmt_contact));
		}
		else if (!firstu && !IS_REPORT_LEVEL_USAGE(hi->usage))
			firstu = hi->usage;

		switch (hi->usage) {
		case HID_USAGE2(HUP_GENERIC_DESKTOP, HUG_X):
			hc.x = d;
			break;
		case HID_USAGE2(HUP_GENERIC_DESKTOP, HUG_Y):
			if (mt->sc_flags & HIDMT_REVY)
				hc.y = mt->sc_maxy - d;
			else
				hc.y = d;
			break;
		case HID_USAGE2(HUP_DIGITIZERS, HUD_TIP_SWITCH):
			hc.tip = d;
			if (d)
				tips++;
			break;
		case HID_USAGE2(HUP_DIGITIZERS, HUD_CONFIDENCE):
			hc.confidence = d;
			break;
		case HID_USAGE2(HUP_DIGITIZERS, HUD_WIDTH):
			hc.width = d;
			break;
		case HID_USAGE2(HUP_DIGITIZERS, HUD_HEIGHT):
			hc.height = d;
			break;
		case HID_USAGE2(HUP_DIGITIZERS, HUD_CONTACTID):
			hc.contactid = d;
			break;

		/* these will only appear once per report */
		case HID_USAGE2(HUP_DIGITIZERS, HUD_CONTACTCOUNT):
			if (d)
				contactcount = d;
			break;
		case HID_USAGE2(HUP_BUTTON, 0x01):
		case HID_USAGE2(HUP_BUTTON, 0x02):
			if (d != 0)
				buttons |= 1;
			break;
		case HID_USAGE2(HUP_BUTTON, 0x03):
			if (d != 0)
				buttons |= 1 << 2;
			break;
		}
	}
	if (seencontacts < contactcount) {
		hc.seen = 1;
		i = wsmouse_id_to_slot(mt->sc_wsmousedev, hc.contactid);
		if (i >= 0)
			memcpy(&mt->sc_contacts[i], &hc,
			    sizeof(struct hidmt_contact));
		seencontacts++;
	}

	s = spltty();
	if (mt->sc_buttons != buttons) {
		wsmouse_buttons(mt->sc_wsmousedev, buttons);
		mt->sc_buttons = buttons;
	}
	for (i = 0; i < HIDMT_MAX_CONTACTS; i++) {
		if (!mt->sc_contacts[i].seen)
			continue;

		mt->sc_contacts[i].seen = 0;

		DPRINTF(("%s: %s: contact %d of %d: id %d, x %d, y %d, "
		    "touch %d, confidence %d, width %d, height %d "
		    "(button 0x%x)\n",
		    mt->sc_device->dv_xname, __func__,
		    i + 1, contactcount,
		    mt->sc_contacts[i].contactid,
		    mt->sc_contacts[i].x,
		    mt->sc_contacts[i].y,
		    mt->sc_contacts[i].tip,
		    mt->sc_contacts[i].confidence,
		    mt->sc_contacts[i].width,
		    mt->sc_contacts[i].height,
		    mt->sc_buttons));

		if (mt->sc_contacts[i].tip && !mt->sc_contacts[i].confidence)
			continue;

		/* Report width as pressure. */
		z = (mt->sc_contacts[i].tip
		    ? imax(mt->sc_contacts[i].width, 50) : 0);

		wsmouse_mtstate(mt->sc_wsmousedev,
		    i, mt->sc_contacts[i].x, mt->sc_contacts[i].y, z);
	}
	wsmouse_input_sync(mt->sc_wsmousedev);

	splx(s);
}

int
hidmt_enable(struct hidmt *mt)
{
	if (mt->sc_enabled)
		return EBUSY;

	mt->sc_enabled = 1;

	return 0;
}

int
hidmt_ioctl(struct hidmt *mt, u_long cmd, caddr_t data, int flag,
    struct proc *p)
{
	struct wsmouse_calibcoords *wsmc = (struct wsmouse_calibcoords *)data;
	int wsmode;

	switch (cmd) {
	case WSMOUSEIO_GTYPE: {
		struct wsmousehw *hw = wsmouse_get_hw(mt->sc_wsmousedev);
		*(u_int *)data = hw->type;
		break;
	}

	case WSMOUSEIO_GCALIBCOORDS:
		wsmc->minx = mt->sc_minx;
		wsmc->maxx = mt->sc_maxx;
		wsmc->miny = mt->sc_miny;
		wsmc->maxy = mt->sc_maxy;
		wsmc->swapxy = 0;
		wsmc->resx = mt->sc_resx;
		wsmc->resy = mt->sc_resy;
		break;

	case WSMOUSEIO_SETMODE:
		wsmode = *(u_int *)data;
		if (wsmode != WSMOUSE_COMPAT && wsmode != WSMOUSE_NATIVE) {
			printf("%s: invalid mode %d\n",
			    mt->sc_device->dv_xname, wsmode);
			return (EINVAL);
		}

		DPRINTF(("%s: changing mode to %s\n", mt->sc_device->dv_xname,
		    (wsmode == WSMOUSE_COMPAT ? "compat" : "native")));

		wsmouse_set_mode(mt->sc_wsmousedev, wsmode);

		break;

	default:
		return -1;
	}

	return 0;
}

void
hidmt_disable(struct hidmt *mt)
{
	mt->sc_enabled = 0;
}

int
hidmt_find_winptp_reports(const void *desc, int len, int *input_rid,
    int *config_rid, int *cap_rid)
{
	static int32_t ptp_collections[] = {
		HID_USAGE2(HUP_DIGITIZERS, HUD_FINGER), 0
	};
	static int32_t input_usages[] = {
		/* report-level */
		HID_USAGE2(HUP_DIGITIZERS, HUD_CONTACTCOUNT),
		/* contact-level */
		HID_USAGE2(HUP_GENERIC_DESKTOP, HUG_X),
		HID_USAGE2(HUP_GENERIC_DESKTOP, HUG_Y),
		HID_USAGE2(HUP_DIGITIZERS, HUD_CONFIDENCE),
		HID_USAGE2(HUP_DIGITIZERS, HUD_TIP_SWITCH),
		HID_USAGE2(HUP_DIGITIZERS, HUD_CONTACTID),
	};
	static int32_t cfg_usages[] = {
		HID_USAGE2(HUP_DIGITIZERS, HUD_INPUT_MODE),
	};
	static int32_t cap_usages[] = {
		HID_USAGE2(HUP_DIGITIZERS, HUD_CONTACT_MAX),
	};

	*input_rid = hid_find_report(desc, len, hid_input,
	    HID_USAGE2(HUP_DIGITIZERS, HUD_TOUCHPAD),
	    nitems(input_usages), input_usages, ptp_collections);
	*config_rid = hid_find_report(desc, len, hid_feature,
	    HID_USAGE2(HUP_DIGITIZERS, HUD_CONFIG),
	    nitems(cfg_usages), cfg_usages, ptp_collections);
	*cap_rid = hid_find_report(desc, len, hid_feature,
	    HID_USAGE2(HUP_DIGITIZERS, HUD_TOUCHPAD),
	    nitems(cap_usages), cap_usages, ptp_collections);

	return (*input_rid > 0 && *config_rid > 0 && *cap_rid > 0);
}
