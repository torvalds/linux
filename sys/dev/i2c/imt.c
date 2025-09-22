/* $OpenBSD: imt.c,v 1.7 2025/07/21 21:46:40 bru Exp $ */
/*
 * HID-over-i2c multitouch trackpad driver for devices conforming to
 * Windows Precision Touchpad standard
 *
 * https://docs.microsoft.com/en-us/windows-hardware/design/component-guidelines/windows-precision-touchpad-required-hid-top-level-collections
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

#include <dev/i2c/i2cvar.h>
#include <dev/i2c/ihidev.h>

#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wsmousevar.h>

#include <dev/hid/hid.h>
#include <dev/hid/hidmtvar.h>

struct imt_softc {
	struct ihidev	sc_hdev;
	struct hidmt	sc_mt;

	int		sc_rep_input;
	int		sc_rep_config;
	int		sc_rep_cap;
};

int	imt_enable(void *);
void	imt_intr(struct ihidev *, void *, u_int);
void	imt_disable(void *);
int	imt_ioctl(void *, u_long, caddr_t, int, struct proc *);

const struct wsmouse_accessops imt_accessops = {
	imt_enable,
	imt_ioctl,
	imt_disable,
};

int	imt_match(struct device *, void *, void *);
void	imt_attach(struct device *, struct device *, void *);
int	imt_hidev_get_report(struct device *, int, int, void *, int);
int	imt_hidev_set_report(struct device *, int, int, void *, int);
int	imt_detach(struct device *, int);

struct cfdriver imt_cd = {
	NULL, "imt", DV_DULL
};

const struct cfattach imt_ca = {
	sizeof(struct imt_softc),
	imt_match,
	imt_attach,
	imt_detach
};

int
imt_match(struct device *parent, void *match, void *aux)
{
	struct ihidev_attach_arg *iha = (struct ihidev_attach_arg *)aux;
	int input_rid, conf_rid, cap_rid;
	int size;
	void *desc;

	if (iha->reportid == IHIDEV_CLAIM_MULTIPLEID) {
		ihidev_get_report_desc(iha->parent, &desc, &size);
		if (hidmt_find_winptp_reports(desc, size,
		    &input_rid, &conf_rid, &cap_rid)) {
			iha->claims[0] = input_rid;
			iha->claims[1] = conf_rid;
			iha->claims[2] = cap_rid;
			iha->nclaims = 3;
			return (IMATCH_DEVCLASS_DEVSUBCLASS);
		}
	}

	return (IMATCH_NONE);
}

void
imt_attach(struct device *parent, struct device *self, void *aux)
{
	struct imt_softc *sc = (struct imt_softc *)self;
	struct hidmt *mt = &sc->sc_mt;
	struct ihidev_attach_arg *iha = (struct ihidev_attach_arg *)aux;
	int size;
	void *desc;

	sc->sc_hdev.sc_intr = imt_intr;
	sc->sc_hdev.sc_parent = iha->parent;

	ihidev_get_report_desc(iha->parent, &desc, &size);
	hidmt_find_winptp_reports(desc, size, &sc->sc_rep_input,
	    &sc->sc_rep_config, &sc->sc_rep_cap);

	memset(mt, 0, sizeof(sc->sc_mt));

	/* assume everything has "natural scrolling" where Y axis is reversed */
	mt->sc_flags = HIDMT_REVY;

	mt->hidev_report_type_conv = ihidev_report_type_conv;
	mt->hidev_get_report = imt_hidev_get_report;
	mt->hidev_set_report = imt_hidev_set_report;
	mt->sc_rep_input = sc->sc_rep_input;
	mt->sc_rep_config = sc->sc_rep_config;
	mt->sc_rep_cap = sc->sc_rep_cap;

	if (hidmt_setup(self, mt, desc, size) != 0)
		return;

	hidmt_attach(mt, &imt_accessops);
}

int
imt_hidev_get_report(struct device *self, int type, int id, void *data, int len)
{
	struct imt_softc *sc = (struct imt_softc *)self;

	return ihidev_get_report((struct device *)sc->sc_hdev.sc_parent, type,
	    id, data, len);
}

int
imt_hidev_set_report(struct device *self, int type, int id, void *data, int len)
{
	struct imt_softc *sc = (struct imt_softc *)self;

	return ihidev_set_report((struct device *)sc->sc_hdev.sc_parent, type,
	    id, data, len);
}

int
imt_detach(struct device *self, int flags)
{
	struct imt_softc *sc = (struct imt_softc *)self;
	struct hidmt *mt = &sc->sc_mt;

	return hidmt_detach(mt, flags);
}

void
imt_intr(struct ihidev *dev, void *buf, u_int len)
{
	struct imt_softc *sc = (struct imt_softc *)dev;
	struct hidmt *mt = &sc->sc_mt;

	if (!mt->sc_enabled)
		return;

	hidmt_input(mt, (uint8_t *)buf, len);
}

int
imt_enable(void *v)
{
	struct imt_softc *sc = v;
	struct hidmt *mt = &sc->sc_mt;
	int rv;

	if ((rv = hidmt_enable(mt)) != 0)
		return rv;

	rv = ihidev_open(&sc->sc_hdev);

	hidmt_set_input_mode(mt, HIDMT_INPUT_MODE_MT_TOUCHPAD);

	return rv;
}

void
imt_disable(void *v)
{
	struct imt_softc *sc = v;
	struct hidmt *mt = &sc->sc_mt;

	hidmt_disable(mt);
	ihidev_close(&sc->sc_hdev);
}

int
imt_ioctl(void *v, u_long cmd, caddr_t data, int flag, struct proc *p)
{
	struct imt_softc *sc = v;
	struct hidmt *mt = &sc->sc_mt;
	int rc;

	rc = ihidev_ioctl(&sc->sc_hdev, cmd, data, flag, p);
	if (rc != -1)
		return rc;

	return hidmt_ioctl(mt, cmd, data, flag, p);
}
