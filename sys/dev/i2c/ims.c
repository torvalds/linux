/* $OpenBSD: ims.c,v 1.3 2021/01/22 17:35:00 jcs Exp $ */
/*
 * HID-over-i2c mouse/trackpad driver
 *
 * Copyright (c) 2015, 2016 joshua stein <jcs@openbsd.org>
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

#include <dev/i2c/i2cvar.h>
#include <dev/i2c/ihidev.h>

#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wsmousevar.h>

#include <dev/hid/hid.h>
#include <dev/hid/hidmsvar.h>

struct ims_softc {
	struct ihidev	sc_hdev;
	struct hidms	sc_ms;
};

void	ims_intr(struct ihidev *addr, void *ibuf, u_int len);

int	ims_enable(void *);
void	ims_disable(void *);
int	ims_ioctl(void *, u_long, caddr_t, int, struct proc *);

const struct wsmouse_accessops ims_accessops = {
	ims_enable,
	ims_ioctl,
	ims_disable,
};

int	ims_match(struct device *, void *, void *);
void	ims_attach(struct device *, struct device *, void *);
int	ims_detach(struct device *, int);

struct cfdriver ims_cd = {
	NULL, "ims", DV_DULL
};

const struct cfattach ims_ca = {
	sizeof(struct ims_softc),
	ims_match,
	ims_attach,
	ims_detach
};

int
ims_match(struct device *parent, void *match, void *aux)
{
	struct ihidev_attach_arg *iha = (struct ihidev_attach_arg *)aux;
	int size;
	void *desc;

	ihidev_get_report_desc(iha->parent, &desc, &size);

	if (hid_is_collection(desc, size, iha->reportid,
	    HID_USAGE2(HUP_GENERIC_DESKTOP, HUG_POINTER)))
		return (IMATCH_IFACECLASS);

	if (hid_is_collection(desc, size, iha->reportid,
	    HID_USAGE2(HUP_GENERIC_DESKTOP, HUG_MOUSE)))
		return (IMATCH_IFACECLASS);

	if (hid_is_collection(desc, size, iha->reportid,
	    HID_USAGE2(HUP_DIGITIZERS, HUD_PEN)))
		return (IMATCH_IFACECLASS);

	if (hid_is_collection(desc, size, iha->reportid,
	    HID_USAGE2(HUP_DIGITIZERS, HUD_TOUCHSCREEN)) &&
	    hid_locate(desc, size, HID_USAGE2(HUP_GENERIC_DESKTOP, HUG_X),
	    iha->reportid, hid_input, NULL, NULL))
		return (IMATCH_IFACECLASS);

	return (IMATCH_NONE);
}

void
ims_attach(struct device *parent, struct device *self, void *aux)
{
	struct ims_softc *sc = (struct ims_softc *)self;
	struct hidms *ms = &sc->sc_ms;
	struct ihidev_attach_arg *iha = (struct ihidev_attach_arg *)aux;
	int size, repid;
	void *desc;

	sc->sc_hdev.sc_intr = ims_intr;
	sc->sc_hdev.sc_parent = iha->parent;
	sc->sc_hdev.sc_report_id = iha->reportid;

	ihidev_get_report_desc(iha->parent, &desc, &size);
	repid = iha->reportid;
	sc->sc_hdev.sc_isize = hid_report_size(desc, size, hid_input, repid);
	sc->sc_hdev.sc_osize = hid_report_size(desc, size, hid_output, repid);
	sc->sc_hdev.sc_fsize = hid_report_size(desc, size, hid_feature, repid);

	if (hidms_setup(self, ms, 0, iha->reportid, desc, size) != 0)
		return;

	hidms_attach(ms, &ims_accessops);
}

int
ims_detach(struct device *self, int flags)
{
	struct ims_softc *sc = (struct ims_softc *)self;
	struct hidms *ms = &sc->sc_ms;

	return hidms_detach(ms, flags);
}

void
ims_intr(struct ihidev *addr, void *buf, u_int len)
{
	struct ims_softc *sc = (struct ims_softc *)addr;
	struct hidms *ms = &sc->sc_ms;

	if (ms->sc_enabled != 0)
		hidms_input(ms, (uint8_t *)buf, len);
}

int
ims_enable(void *v)
{
	struct ims_softc *sc = v;
	struct hidms *ms = &sc->sc_ms;
	int rv;

	if ((rv = hidms_enable(ms)) != 0)
		return rv;

	return ihidev_open(&sc->sc_hdev);
}

void
ims_disable(void *v)
{
	struct ims_softc *sc = v;
	struct hidms *ms = &sc->sc_ms;

	hidms_disable(ms);
	ihidev_close(&sc->sc_hdev);
}

int
ims_ioctl(void *v, u_long cmd, caddr_t data, int flag, struct proc *p)
{
	struct ims_softc *sc = v;
	struct hidms *ms = &sc->sc_ms;
	int rc;

#if 0
	rc = ihidev_ioctl(&sc->sc_hdev, cmd, data, flag, p);
	if (rc != -1)
		return rc;
#endif

	rc = hidms_ioctl(ms, cmd, data, flag, p);
	if (rc != -1)
		return rc;

	switch (cmd) {
	case WSMOUSEIO_GTYPE:
		*(u_int *)data = WSMOUSE_TYPE_TOUCHPAD;
		return 0;
	default:
		return -1;
	}
}
