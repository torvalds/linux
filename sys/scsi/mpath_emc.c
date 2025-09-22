/*	$OpenBSD: mpath_emc.c,v 1.25 2022/07/02 08:50:42 visa Exp $ */

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

/* EMC CLARiiON AX/CX support for mpath(4) */

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

#define EMC_VPD_SP_INFO			0xc0

struct emc_vpd_sp_info {
	struct scsi_vpd_hdr	hdr; /* EMC_VPD_SP_INFO */

	u_int8_t		lun_state;
#define EMC_SP_INFO_LUN_STATE_UNBOUND	0x00
#define EMC_SP_INFO_LUN_STATE_BOUND	0x01
#define EMC_SP_INFO_LUN_STATE_OWNED	0x02
	u_int8_t		default_sp;
	u_int8_t		_reserved1[1];
	u_int8_t		port;
	u_int8_t		current_sp;
	u_int8_t		_reserved2[1];
	u_int8_t		unique_id[16];
	u_int8_t		_reserved3[1];
	u_int8_t		type;
	u_int8_t		failover_mode;
	u_int8_t		_reserved4[21];
	u_int8_t		serial[16];
} __packed;

struct emc_softc {
	struct device		sc_dev;
	struct mpath_path	sc_path;
	struct scsi_xshandler   sc_xsh;
	struct emc_vpd_sp_info	*sc_pg;
};
#define DEVNAME(_s) ((_s)->sc_dev.dv_xname)

int		emc_match(struct device *, void *, void *);
void		emc_attach(struct device *, struct device *, void *);
int		emc_detach(struct device *, int);
int		emc_activate(struct device *, int);

const struct cfattach emc_ca = {
	sizeof(struct emc_softc),
	emc_match,
	emc_attach,
	emc_detach,
	emc_activate
};

struct cfdriver emc_cd = {
	NULL,
	"emc",
	DV_DULL
};

void		emc_mpath_start(struct scsi_xfer *);
int		emc_mpath_checksense(struct scsi_xfer *);
void		emc_mpath_status(struct scsi_link *);

const struct mpath_ops emc_mpath_ops = {
	"emc",
	emc_mpath_checksense,
	emc_mpath_status
};

struct emc_device {
	char *vendor;
	char *product;
};

void		emc_status(struct scsi_xfer *);
void		emc_status_done(struct scsi_xfer *);

int		emc_sp_info(struct emc_softc *, int *);

struct emc_device emc_devices[] = {
/*	  " vendor "  "     device     " */
/*	  "01234567"  "0123456789012345" */
	{ "DGC     ", "RAID" },
	{ "DGC     ", "DISK" },
	{ "DGC     ", "VRAID" }
};

int
emc_match(struct device *parent, void *match, void *aux)
{
	struct scsi_attach_args *sa = aux;
	struct scsi_inquiry_data *inq = &sa->sa_sc_link->inqdata;
	struct emc_device *s;
	int i;

	if (mpath_path_probe(sa->sa_sc_link) != 0)
		return (0);

	for (i = 0; i < nitems(emc_devices); i++) {
		s = &emc_devices[i];

		if (bcmp(s->vendor, inq->vendor, strlen(s->vendor)) == 0 &&
		    bcmp(s->product, inq->product, strlen(s->product)) == 0)
			return (8);
	}

	return (0);
}

void
emc_attach(struct device *parent, struct device *self, void *aux)
{
	struct emc_softc *sc = (struct emc_softc *)self;
	struct scsi_attach_args *sa = aux;
	struct scsi_link *link = sa->sa_sc_link;
	int sp;

	printf("\n");

	/* init link */
	link->device_softc = sc;

	/* init path */
	scsi_xsh_set(&sc->sc_path.p_xsh, link, emc_mpath_start);
	sc->sc_path.p_link = link;

	/* init status handler */
	scsi_xsh_set(&sc->sc_xsh, link, emc_status);
	sc->sc_pg = dma_alloc(sizeof(*sc->sc_pg), PR_WAITOK);

	/* let's go */

	if (emc_sp_info(sc, &sp)) {
		printf("%s: unable to get sp info\n", DEVNAME(sc));
		return;
	}

	if (mpath_path_attach(&sc->sc_path, sp, &emc_mpath_ops) != 0)
		printf("%s: unable to attach path\n", DEVNAME(sc));
}

int
emc_detach(struct device *self, int flags)
{
	struct emc_softc *sc = (struct emc_softc *)self;

	dma_free(sc->sc_pg, sizeof(*sc->sc_pg));

	return (0);
}

int
emc_activate(struct device *self, int act)
{
	struct emc_softc *sc = (struct emc_softc *)self;

	switch (act) {
	case DVACT_DEACTIVATE:
		if (sc->sc_path.p_group != NULL)
			mpath_path_detach(&sc->sc_path);
		break;
	}
	return (0);
}

void
emc_mpath_start(struct scsi_xfer *xs)
{
	struct emc_softc *sc = xs->sc_link->device_softc;

	mpath_start(&sc->sc_path, xs);
}

int
emc_mpath_checksense(struct scsi_xfer *xs)
{
	struct scsi_sense_data *sense = &xs->sense;

	if ((sense->error_code & SSD_ERRCODE) == SSD_ERRCODE_CURRENT &&
	    (sense->flags & SSD_KEY) == SKEY_NOT_READY &&
	    ASC_ASCQ(sense) == 0x0403) {
		/* Logical Unit Not Ready, Manual Intervention Required */
		return (MPATH_SENSE_FAILOVER);
	}

	return (MPATH_SENSE_DECLINED);
}

void
emc_mpath_status(struct scsi_link *link)
{
	struct emc_softc *sc = link->device_softc;

	scsi_xsh_add(&sc->sc_xsh);
}

void
emc_status(struct scsi_xfer *xs)
{
	struct scsi_link *link = xs->sc_link;
	struct emc_softc *sc = link->device_softc;

	scsi_init_inquiry(xs, SI_EVPD, EMC_VPD_SP_INFO,
	    sc->sc_pg, sizeof(*sc->sc_pg));

	xs->done = emc_status_done;

	scsi_xs_exec(xs);
}

void
emc_status_done(struct scsi_xfer *xs)
{
	struct scsi_link *link = xs->sc_link;
	struct emc_softc *sc = link->device_softc;
	struct emc_vpd_sp_info *pg = sc->sc_pg;
	int status = MPATH_S_UNKNOWN;

	if (xs->error == XS_NOERROR) {
		status = (pg->lun_state == EMC_SP_INFO_LUN_STATE_OWNED) ?
		    MPATH_S_ACTIVE : MPATH_S_PASSIVE;
	}

	scsi_xs_put(xs);

	mpath_path_status(&sc->sc_path, status);
}

int
emc_sp_info(struct emc_softc *sc, int *sp)
{
	struct emc_vpd_sp_info *pg = sc->sc_pg;
	int error;

	error = scsi_inquire_vpd(sc->sc_path.p_link, pg, sizeof(*pg),
	    EMC_VPD_SP_INFO, scsi_autoconf);
	if (error != 0)
		return (error);

	*sp = pg->current_sp;

	printf("%s: SP-%c port %d\n", DEVNAME(sc), pg->current_sp + 'A',
	    pg->port);

	return (0);
}
