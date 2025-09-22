/*	$OpenBSD: mpath_sym.c,v 1.28 2022/07/02 08:50:42 visa Exp $ */

/*
 * Copyright (c) 2010 David Gwynne <dlg@openbsd.org>
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

struct sym_softc {
	struct device		sc_dev;
	struct mpath_path	sc_path;
};
#define DEVNAME(_s) ((_s)->sc_dev.dv_xname)

int		sym_match(struct device *, void *, void *);
void		sym_attach(struct device *, struct device *, void *);
int		sym_detach(struct device *, int);
int		sym_activate(struct device *, int);

const struct cfattach sym_ca = {
	sizeof(struct sym_softc),
	sym_match,
	sym_attach,
	sym_detach,
	sym_activate
};

struct cfdriver sym_cd = {
	NULL,
	"sym",
	DV_DULL
};

void		sym_mpath_start(struct scsi_xfer *);
int		sym_mpath_checksense(struct scsi_xfer *);
void		sym_mpath_status(struct scsi_link *);

const struct mpath_ops sym_mpath_sym_ops = {
	"sym",
	sym_mpath_checksense,
	sym_mpath_status
};

const struct mpath_ops sym_mpath_asym_ops = {
	"sym",
	sym_mpath_checksense,
	sym_mpath_status
};

struct sym_device {
	char *vendor;
	char *product;
};

struct sym_device sym_devices[] = {
/*	  " vendor "  "     device     " */
/*	  "01234567"  "0123456789012345" */
	{ "TOSHIBA ", "MBF" },
	{ "SEAGATE ", "ST" },
	{ "SGI     ", "ST" },
	{ "FUJITSU ", "MBD" },
	{ "FUJITSU ", "MA" }
};

struct sym_device asym_devices[] = {
/*	  " vendor "  "     device     " */
/*	  "01234567"  "0123456789012345" */
	{ "DELL    ", "MD1220          " },
	{ "DELL    ", "MD3060e         " },
	{ "SUN     ", "StorEdge 3510F D" },
	{ "SUNW    ", "SUNWGS INT FCBPL" },
	{ "Transtec", "PROVIGO1100" },
	{ "NetBSD", "NetBSD iSCSI" }
};

int
sym_match(struct device *parent, void *match, void *aux)
{
	struct scsi_attach_args *sa = aux;
	struct scsi_inquiry_data *inq = &sa->sa_sc_link->inqdata;
	struct sym_device *s;
	int i;

	if (mpath_path_probe(sa->sa_sc_link) != 0)
		return (0);

	for (i = 0; i < nitems(sym_devices); i++) {
		s = &sym_devices[i];

		if (bcmp(s->vendor, inq->vendor, strlen(s->vendor)) == 0 &&
		    bcmp(s->product, inq->product, strlen(s->product)) == 0)
			return (8);
	}
	for (i = 0; i < nitems(asym_devices); i++) {
		s = &asym_devices[i];

		if (bcmp(s->vendor, inq->vendor, strlen(s->vendor)) == 0 &&
		    bcmp(s->product, inq->product, strlen(s->product)) == 0)
			return (8);
	}

	return (0);
}

void
sym_attach(struct device *parent, struct device *self, void *aux)
{
	struct sym_softc *sc = (struct sym_softc *)self;
	struct scsi_attach_args *sa = aux;
	struct scsi_link *link = sa->sa_sc_link;
	struct scsi_inquiry_data *inq = &link->inqdata;
	const struct mpath_ops *ops = &sym_mpath_sym_ops;
	struct sym_device *s;
	u_int id = 0;
	int i;

	printf("\n");

	/* check if we're an asymmetric access device */
	for (i = 0; i < nitems(asym_devices); i++) {
		s = &asym_devices[i];

		if (bcmp(s->vendor, inq->vendor, strlen(s->vendor)) == 0 &&
		    bcmp(s->product, inq->product, strlen(s->product)) == 0) {
			ops = &sym_mpath_asym_ops;
			id = sc->sc_dev.dv_unit;
			break;
		}
	}

	/* init link */
	link->device_softc = sc;

	/* init path */
	scsi_xsh_set(&sc->sc_path.p_xsh, link, sym_mpath_start);
	sc->sc_path.p_link = link;

	if (mpath_path_attach(&sc->sc_path, id, ops) != 0)
		printf("%s: unable to attach path\n", DEVNAME(sc));
}

int
sym_detach(struct device *self, int flags)
{
	return (0);
}

int
sym_activate(struct device *self, int act)
{
	struct sym_softc *sc = (struct sym_softc *)self;

	switch (act) {
	case DVACT_DEACTIVATE:
		if (sc->sc_path.p_group != NULL)
			mpath_path_detach(&sc->sc_path);
		break;
	}
	return (0);
}

void
sym_mpath_start(struct scsi_xfer *xs)
{
	struct sym_softc *sc = xs->sc_link->device_softc;

	mpath_start(&sc->sc_path, xs);
}

int
sym_mpath_checksense(struct scsi_xfer *xs)
{
	return (MPATH_SENSE_DECLINED);
}

void
sym_mpath_status(struct scsi_link *link)
{
	struct sym_softc *sc = link->device_softc;

	mpath_path_status(&sc->sc_path, MPATH_S_ACTIVE);
}
