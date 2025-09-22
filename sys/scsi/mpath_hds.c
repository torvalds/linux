/*	$OpenBSD: mpath_hds.c,v 1.26 2022/07/02 08:50:42 visa Exp $ */

/*
 * Copyright (c) 2011 David Gwynne <dlg@openbsd.org>
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

/* Hitachi Modular Storage support for mpath(4) */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/device.h>
#include <sys/conf.h>
#include <sys/queue.h>
#include <sys/rwlock.h>
#include <sys/pool.h>
#include <sys/ioctl.h>

#include <scsi/scsi_all.h>
#include <scsi/scsiconf.h>
#include <scsi/mpathvar.h>

#define HDS_INQ_LDEV_OFFSET	44
#define HDS_INQ_LDEV_LEN	4
#define HDS_INQ_CTRL_OFFSET	49
#define HDS_INQ_PORT_OFFSET	50
#define HDS_INQ_TYPE_OFFSET	128
#define HDS_INQ_TYPE		0x44463030 /* "DF00" */

#define HDS_VPD			0xe0

struct hds_vpd {
	struct scsi_vpd_hdr	hdr; /* HDS_VPD */
	u_int8_t		state;
#define HDS_VPD_VALID			0x80
#define HDS_VPD_PREFERRED		0x40

	/* followed by lots of unknown stuff */
};

#define HDS_SYMMETRIC		0
#define HDS_ASYMMETRIC		1

struct hds_softc {
	struct device		sc_dev;
	struct mpath_path	sc_path;
	struct scsi_xshandler	sc_xsh;
	struct hds_vpd		*sc_vpd;
	int			sc_mode;
	int			sc_ctrl;
};
#define DEVNAME(_s) ((_s)->sc_dev.dv_xname)

int		hds_match(struct device *, void *, void *);
void		hds_attach(struct device *, struct device *, void *);
int		hds_detach(struct device *, int);
int		hds_activate(struct device *, int);

const struct cfattach hds_ca = {
	sizeof(struct hds_softc),
	hds_match,
	hds_attach,
	hds_detach,
	hds_activate
};

struct cfdriver hds_cd = {
	NULL,
	"hds",
	DV_DULL
};

void		hds_mpath_start(struct scsi_xfer *);
int		hds_mpath_checksense(struct scsi_xfer *);
void		hds_mpath_status(struct scsi_link *);

const struct mpath_ops hds_mpath_ops = {
	"hds",
	hds_mpath_checksense,
	hds_mpath_status
};

struct hds_device {
	char *vendor;
	char *product;
};

int		hds_inquiry(struct scsi_link *, int *);
int		hds_info(struct hds_softc *);

void		hds_status(struct scsi_xfer *);
void		hds_status_done(struct scsi_xfer *);

struct hds_device hds_devices[] = {
/*	  " vendor "  "     device     " */
/*	  "01234567"  "0123456789012345" */
	{ "HITACHI ", "DF600F          " },
	{ "HITACHI ", "DF600F-CM       " }
};

int
hds_match(struct device *parent, void *match, void *aux)
{
	struct scsi_attach_args *sa = aux;
	struct scsi_link *link = sa->sa_sc_link;
	struct scsi_inquiry_data *inq = &link->inqdata;
	struct hds_device *s;
	int i, mode;

	if (mpath_path_probe(sa->sa_sc_link) != 0)
		return (0);

	for (i = 0; i < nitems(hds_devices); i++) {
		s = &hds_devices[i];

		if (bcmp(s->vendor, inq->vendor, strlen(s->vendor)) == 0 &&
		    bcmp(s->product, inq->product, strlen(s->product)) == 0 &&
		    hds_inquiry(link, &mode) == 0)
			return (8);
	}

	return (0);
}

void
hds_attach(struct device *parent, struct device *self, void *aux)
{
	struct hds_softc *sc = (struct hds_softc *)self;
	struct scsi_attach_args *sa = aux;
	struct scsi_link *link = sa->sa_sc_link;

	printf("\n");

	/* init link */
	link->device_softc = sc;

	/* init path */
	scsi_xsh_set(&sc->sc_path.p_xsh, link, hds_mpath_start);
	sc->sc_path.p_link = link;

	/* init status handler */
	scsi_xsh_set(&sc->sc_xsh, link, hds_status);
	sc->sc_vpd = dma_alloc(sizeof(*sc->sc_vpd), PR_WAITOK);

	if (hds_inquiry(link, &sc->sc_mode) != 0) {
		printf("%s: unable to query controller mode\n", DEVNAME(sc));
		return;
	}

	if (hds_info(sc) != 0) {
		printf("%s: unable to query path info\n", DEVNAME(sc));
		return;
	}

	if (mpath_path_attach(&sc->sc_path,
	    (sc->sc_mode == HDS_SYMMETRIC) ? 0 : sc->sc_ctrl,
	    &hds_mpath_ops) != 0)
		printf("%s: unable to attach path\n", DEVNAME(sc));
}

int
hds_detach(struct device *self, int flags)
{
	struct hds_softc *sc = (struct hds_softc *)self;

	dma_free(sc->sc_vpd, sizeof(*sc->sc_vpd));

	return (0);
}

int
hds_activate(struct device *self, int act)
{
	struct hds_softc *sc = (struct hds_softc *)self;

	switch (act) {
	case DVACT_DEACTIVATE:
		if (sc->sc_path.p_group != NULL)
			mpath_path_detach(&sc->sc_path);
		break;
	}
	return (0);
}

void
hds_mpath_start(struct scsi_xfer *xs)
{
	struct hds_softc *sc = xs->sc_link->device_softc;

	mpath_start(&sc->sc_path, xs);
}

int
hds_mpath_checksense(struct scsi_xfer *xs)
{
	return (MPATH_SENSE_DECLINED);
}

void
hds_mpath_status(struct scsi_link *link)
{
	struct hds_softc *sc = link->device_softc;

	if (sc->sc_mode == HDS_SYMMETRIC)
		mpath_path_status(&sc->sc_path, MPATH_S_ACTIVE);
	else
		scsi_xsh_add(&sc->sc_xsh);
}

void
hds_status(struct scsi_xfer *xs)
{
	struct scsi_link *link = xs->sc_link;
	struct hds_softc *sc = link->device_softc;

	scsi_init_inquiry(xs, SI_EVPD, HDS_VPD,
	    sc->sc_vpd, sizeof(*sc->sc_vpd));

	xs->done = hds_status_done;

	scsi_xs_exec(xs);
}

void
hds_status_done(struct scsi_xfer *xs)
{
	struct scsi_link *link = xs->sc_link;
	struct hds_softc *sc = link->device_softc;
	struct hds_vpd *vpd = sc->sc_vpd;
	int status = MPATH_S_UNKNOWN;

	if (xs->error == XS_NOERROR &&
	    _2btol(vpd->hdr.page_length) >= sizeof(vpd->state) &&
	    ISSET(vpd->state, HDS_VPD_VALID)) {
		status = ISSET(vpd->state, HDS_VPD_PREFERRED) ?
		    MPATH_S_ACTIVE : MPATH_S_PASSIVE;
	}

	scsi_xs_put(xs);

	mpath_path_status(&sc->sc_path, status);
}

int
hds_inquiry(struct scsi_link *link, int *mode)
{
	struct scsi_xfer *xs;
	u_int8_t *buf;
	size_t len = SID_SCSI2_HDRLEN + link->inqdata.additional_length;
	int error;

	if (len < HDS_INQ_TYPE_OFFSET + sizeof(int))
		return (ENXIO);

	xs = scsi_xs_get(link, scsi_autoconf);
	if (xs == NULL)
		return (ENOMEM);

	buf = dma_alloc(len, PR_WAITOK);
	scsi_init_inquiry(xs, 0, 0, buf, len);
	error = scsi_xs_sync(xs);
	scsi_xs_put(xs);
	if (error)
		goto done;

	if (buf[128] == '\0')
		*mode = HDS_ASYMMETRIC;
	else if (_4btol(&buf[HDS_INQ_TYPE_OFFSET]) == HDS_INQ_TYPE)
		*mode = HDS_SYMMETRIC;
	else
		error = ENXIO;

done:
	dma_free(buf, len);
	return (error);
}

int
hds_info(struct hds_softc *sc)
{
	struct scsi_link *link = sc->sc_path.p_link;
	struct scsi_xfer *xs;
	u_int8_t *buf;
	size_t len = SID_SCSI2_HDRLEN + link->inqdata.additional_length;
	char ldev[9], ctrl, port;
	int error;

	xs = scsi_xs_get(link, scsi_autoconf);
	if (xs == NULL)
		return (ENOMEM);

	buf = dma_alloc(len, PR_WAITOK);
	scsi_init_inquiry(xs, 0, 0, buf, len);
	error = scsi_xs_sync(xs);
	scsi_xs_put(xs);
	if (error)
		goto done;

	bzero(ldev, sizeof(ldev));
	scsi_strvis(ldev, &buf[HDS_INQ_LDEV_OFFSET], HDS_INQ_LDEV_LEN);
	ctrl = buf[HDS_INQ_CTRL_OFFSET];
	port = buf[HDS_INQ_PORT_OFFSET];

	if (ctrl >= '0' && ctrl <= '9') {
		printf("%s: ldev %s, controller %c, port %c, %s\n",
		    DEVNAME(sc), ldev, ctrl, port,
		    sc->sc_mode == HDS_SYMMETRIC ? "symmetric" : "asymmetric");

		sc->sc_ctrl = ctrl;
	} else
		error = ENXIO;

done:
	dma_free(buf, len);
	return (error);
}
