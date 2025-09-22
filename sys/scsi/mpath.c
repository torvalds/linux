/*	$OpenBSD: mpath.c,v 1.59 2025/09/16 12:18:10 hshoexer Exp $ */

/*
 * Copyright (c) 2009 David Gwynne <dlg@openbsd.org>
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
#include <sys/ioctl.h>

#include <scsi/scsi_all.h>
#include <scsi/scsiconf.h>
#include <scsi/mpathvar.h>

#define MPATH_BUSWIDTH 256

int		mpath_match(struct device *, void *, void *);
void		mpath_attach(struct device *, struct device *, void *);

TAILQ_HEAD(mpath_paths, mpath_path);

struct mpath_group {
	TAILQ_ENTRY(mpath_group) g_entry;
	struct mpath_paths	 g_paths;
	struct mpath_dev	*g_dev;
	u_int			 g_id;
};
TAILQ_HEAD(mpath_groups, mpath_group);

struct mpath_dev {
	struct mutex		 d_mtx;

	struct scsi_xfer_list	 d_xfers;
	struct mpath_path	*d_next_path;

	struct mpath_groups	 d_groups;

	struct mpath_group	*d_failover_iter;
	struct timeout		 d_failover_tmo;
	u_int			 d_failover;

	const struct mpath_ops	*d_ops;
	struct devid		*d_id;
};

struct mpath_softc {
	struct device		sc_dev;
	struct scsibus_softc	*sc_scsibus;
	struct mpath_dev	*sc_devs[MPATH_BUSWIDTH];
};
#define DEVNAME(_s) ((_s)->sc_dev.dv_xname)

struct mpath_softc	*mpath;

const struct cfattach mpath_ca = {
	sizeof(struct mpath_softc),
	mpath_match,
	mpath_attach
};

struct cfdriver mpath_cd = {
	NULL,
	"mpath",
	DV_DULL,
	CD_COCOVM
};

void		mpath_cmd(struct scsi_xfer *);
void		mpath_minphys(struct buf *, struct scsi_link *);
int		mpath_probe(struct scsi_link *);

struct mpath_path *mpath_next_path(struct mpath_dev *);
void		mpath_done(struct scsi_xfer *);

void		mpath_failover(struct mpath_dev *);
void		mpath_failover_start(void *);
void		mpath_failover_check(struct mpath_dev *);

const struct scsi_adapter mpath_switch = {
	mpath_cmd, NULL, mpath_probe, NULL, NULL
};

void		mpath_xs_stuffup(struct scsi_xfer *);

int
mpath_match(struct device *parent, void *match, void *aux)
{
	return (1);
}

void
mpath_attach(struct device *parent, struct device *self, void *aux)
{
	struct mpath_softc		*sc = (struct mpath_softc *)self;
	struct scsibus_attach_args	saa;

	mpath = sc;

	printf("\n");

	saa.saa_adapter = &mpath_switch;
	saa.saa_adapter_softc = sc;
	saa.saa_adapter_target = SDEV_NO_ADAPTER_TARGET;
	saa.saa_adapter_buswidth = MPATH_BUSWIDTH;
	saa.saa_luns = 1;
	saa.saa_openings = 1024; /* XXX magical */
	saa.saa_pool = NULL;
	saa.saa_quirks = saa.saa_flags = 0;
	saa.saa_wwpn = saa.saa_wwnn = 0;

	sc->sc_scsibus = (struct scsibus_softc *)config_found(&sc->sc_dev,
	    &saa, scsiprint);
}

void
mpath_xs_stuffup(struct scsi_xfer *xs)
{
	xs->error = XS_DRIVER_STUFFUP;
	scsi_done(xs);
}

int
mpath_probe(struct scsi_link *link)
{
	struct mpath_softc *sc = link->bus->sb_adapter_softc;
	struct mpath_dev *d = sc->sc_devs[link->target];

	if (link->lun != 0 || d == NULL)
		return (ENXIO);

	link->id = devid_copy(d->d_id);

	return (0);
}

struct mpath_path *
mpath_next_path(struct mpath_dev *d)
{
	struct mpath_group *g;
	struct mpath_path *p;

#ifdef DIAGNOSTIC
	if (d == NULL)
		panic("%s: d is NULL", __func__);
#endif /* DIAGNOSTIC */

	p = d->d_next_path;
	if (p != NULL) {
		d->d_next_path = TAILQ_NEXT(p, p_entry);
		if (d->d_next_path == NULL &&
		    (g = TAILQ_FIRST(&d->d_groups)) != NULL)
			d->d_next_path = TAILQ_FIRST(&g->g_paths);
	}

	return (p);
}

void
mpath_cmd(struct scsi_xfer *xs)
{
	struct scsi_link *link = xs->sc_link;
	struct mpath_softc *sc = link->bus->sb_adapter_softc;
	struct mpath_dev *d = sc->sc_devs[link->target];
	struct mpath_path *p;
	struct scsi_xfer *mxs;

#ifdef DIAGNOSTIC
	if (d == NULL)
		panic("mpath_cmd issued against nonexistent device");
#endif /* DIAGNOSTIC */

	if (ISSET(xs->flags, SCSI_POLL)) {
		mtx_enter(&d->d_mtx);
		p = mpath_next_path(d);
		mtx_leave(&d->d_mtx);
		if (p == NULL) {
			mpath_xs_stuffup(xs);
			return;
		}

		mxs = scsi_xs_get(p->p_link, xs->flags);
		if (mxs == NULL) {
			mpath_xs_stuffup(xs);
			return;
		}

		memcpy(&mxs->cmd, &xs->cmd, xs->cmdlen);
		mxs->cmdlen = xs->cmdlen;
		mxs->data = xs->data;
		mxs->datalen = xs->datalen;
		mxs->retries = xs->retries;
		mxs->timeout = xs->timeout;
		mxs->bp = xs->bp;

		scsi_xs_sync(mxs);

		xs->error = mxs->error;
		xs->status = mxs->status;
		xs->resid = mxs->resid;

		memcpy(&xs->sense, &mxs->sense, sizeof(xs->sense));

		scsi_xs_put(mxs);
		scsi_done(xs);
		return;
	}

	mtx_enter(&d->d_mtx);
	SIMPLEQ_INSERT_TAIL(&d->d_xfers, xs, xfer_list);
	p = mpath_next_path(d);
	mtx_leave(&d->d_mtx);

	if (p != NULL)
		scsi_xsh_add(&p->p_xsh);
}

void
mpath_start(struct mpath_path *p, struct scsi_xfer *mxs)
{
	struct mpath_dev *d = p->p_group->g_dev;
	struct scsi_xfer *xs;
	int addxsh = 0;

	if (ISSET(p->p_link->state, SDEV_S_DYING) || d == NULL)
		goto fail;

	mtx_enter(&d->d_mtx);
	xs = SIMPLEQ_FIRST(&d->d_xfers);
	if (xs != NULL) {
		SIMPLEQ_REMOVE_HEAD(&d->d_xfers, xfer_list);
		if (!SIMPLEQ_EMPTY(&d->d_xfers))
			addxsh = 1;
	}
	mtx_leave(&d->d_mtx);

	if (xs == NULL)
		goto fail;

	memcpy(&mxs->cmd, &xs->cmd, xs->cmdlen);
	mxs->cmdlen = xs->cmdlen;
	mxs->data = xs->data;
	mxs->datalen = xs->datalen;
	mxs->retries = xs->retries;
	mxs->timeout = xs->timeout;
	mxs->bp = xs->bp;
	mxs->flags = xs->flags;

	mxs->cookie = xs;
	mxs->done = mpath_done;

	scsi_xs_exec(mxs);

	if (addxsh)
		scsi_xsh_add(&p->p_xsh);

	return;
fail:
	scsi_xs_put(mxs);
}

void
mpath_done(struct scsi_xfer *mxs)
{
	struct scsi_xfer *xs = mxs->cookie;
	struct scsi_link *link = xs->sc_link;
	struct mpath_softc *sc = link->bus->sb_adapter_softc;
	struct mpath_dev *d = sc->sc_devs[link->target];
	struct mpath_path *p;

	switch (mxs->error) {
	case XS_SELTIMEOUT: /* physical path is gone, try the next */
	case XS_RESET:
		mtx_enter(&d->d_mtx);
		SIMPLEQ_INSERT_HEAD(&d->d_xfers, xs, xfer_list);
		p = mpath_next_path(d);
		mtx_leave(&d->d_mtx);

		scsi_xs_put(mxs);

		if (p != NULL)
			scsi_xsh_add(&p->p_xsh);
		return;
	case XS_SENSE:
		switch (d->d_ops->op_checksense(mxs)) {
		case MPATH_SENSE_FAILOVER:
			mtx_enter(&d->d_mtx);
			SIMPLEQ_INSERT_HEAD(&d->d_xfers, xs, xfer_list);
			p = mpath_next_path(d);
			mtx_leave(&d->d_mtx);

			scsi_xs_put(mxs);

			mpath_failover(d);
			return;
		case MPATH_SENSE_DECLINED:
			break;
#ifdef DIAGNOSTIC
		default:
			panic("unexpected return from checksense");
#endif /* DIAGNOSTIC */
		}
		break;
	}

	xs->error = mxs->error;
	xs->status = mxs->status;
	xs->resid = mxs->resid;

	memcpy(&xs->sense, &mxs->sense, sizeof(xs->sense));

	scsi_xs_put(mxs);

	scsi_done(xs);
}

void
mpath_failover(struct mpath_dev *d)
{
	if (!scsi_pending_start(&d->d_mtx, &d->d_failover))
		return;

	mpath_failover_start(d);
}

void
mpath_failover_start(void *xd)
{
	struct mpath_dev *d = xd;

	mtx_enter(&d->d_mtx);
	d->d_failover_iter = TAILQ_FIRST(&d->d_groups);
	mtx_leave(&d->d_mtx);

	mpath_failover_check(d);
}

void
mpath_failover_check(struct mpath_dev *d)
{
	struct mpath_group *g = d->d_failover_iter;
	struct mpath_path *p;

	if (g == NULL)
		timeout_add_sec(&d->d_failover_tmo, 1);
	else {
		p = TAILQ_FIRST(&g->g_paths);
		d->d_ops->op_status(p->p_link);
	}
}

void
mpath_path_status(struct mpath_path *p, int status)
{
	struct mpath_group *g = p->p_group;
	struct mpath_dev *d = g->g_dev;

	mtx_enter(&d->d_mtx);
	if (status == MPATH_S_ACTIVE) {
		TAILQ_REMOVE(&d->d_groups, g, g_entry);
		TAILQ_INSERT_HEAD(&d->d_groups, g, g_entry);
		d->d_next_path = p;
	} else
		d->d_failover_iter = TAILQ_NEXT(d->d_failover_iter, g_entry);
	mtx_leave(&d->d_mtx);

	if (status == MPATH_S_ACTIVE) {
		scsi_xsh_add(&p->p_xsh);
		if (!scsi_pending_finish(&d->d_mtx, &d->d_failover))
			mpath_failover_start(d);
	} else
		mpath_failover_check(d);
}

void
mpath_minphys(struct buf *bp, struct scsi_link *link)
{
	struct mpath_softc *sc = link->bus->sb_adapter_softc;
	struct mpath_dev *d = sc->sc_devs[link->target];
	struct mpath_group *g;
	struct mpath_path *p;

#ifdef DIAGNOSTIC
	if (d == NULL)
		panic("mpath_minphys against nonexistent device");
#endif /* DIAGNOSTIC */

	mtx_enter(&d->d_mtx);
	TAILQ_FOREACH(g, &d->d_groups, g_entry) {
		TAILQ_FOREACH(p, &g->g_paths, p_entry) {
			/* XXX crossing layers with mutex held */
			if (p->p_link->bus->sb_adapter->dev_minphys != NULL)
				p->p_link->bus->sb_adapter->dev_minphys(bp,
				    p->p_link);
		}
	}
	mtx_leave(&d->d_mtx);
}

int
mpath_path_probe(struct scsi_link *link)
{
	if (mpath == NULL)
		return (ENXIO);

	if (link->id == NULL)
		return (EINVAL);

	if (ISSET(link->flags, SDEV_UMASS))
		return (EINVAL);

	if (mpath == link->bus->sb_adapter_softc)
		return (ENXIO);

	return (0);
}

int
mpath_path_attach(struct mpath_path *p, u_int g_id, const struct mpath_ops *ops)
{
	struct mpath_softc *sc = mpath;
	struct scsi_link *link = p->p_link;
	struct mpath_dev *d = NULL;
	struct mpath_group *g;
	int newdev = 0, addxsh = 0;
	int target;

#ifdef DIAGNOSTIC
	if (p->p_link == NULL)
		panic("mpath_path_attach: NULL link");
	if (p->p_group != NULL)
		panic("mpath_path_attach: group is not NULL");
#endif /* DIAGNOSTIC */

	for (target = 0; target < MPATH_BUSWIDTH; target++) {
		if ((d = sc->sc_devs[target]) == NULL)
			continue;

		if (DEVID_CMP(d->d_id, link->id) && d->d_ops == ops)
			break;

		d = NULL;
	}

	if (d == NULL) {
		for (target = 0; target < MPATH_BUSWIDTH; target++) {
			if (sc->sc_devs[target] == NULL)
				break;
		}
		if (target >= MPATH_BUSWIDTH)
			return (ENXIO);

		d = malloc(sizeof(*d), M_DEVBUF, M_WAITOK | M_CANFAIL | M_ZERO);
		if (d == NULL)
			return (ENOMEM);

		mtx_init(&d->d_mtx, IPL_BIO);
		TAILQ_INIT(&d->d_groups);
		SIMPLEQ_INIT(&d->d_xfers);
		d->d_id = devid_copy(link->id);
		d->d_ops = ops;

		timeout_set(&d->d_failover_tmo, mpath_failover_start, d);

		sc->sc_devs[target] = d;
		newdev = 1;
	} else {
		/*
		 * instead of carrying identical values in different devid
		 * instances, delete the new one and reference the old one in
		 * the new scsi_link.
		 */
		devid_free(link->id);
		link->id = devid_copy(d->d_id);
	}

	TAILQ_FOREACH(g, &d->d_groups, g_entry) {
		if (g->g_id == g_id)
			break;
	}

	if (g == NULL) {
		g = malloc(sizeof(*g),  M_DEVBUF,
		    M_WAITOK | M_CANFAIL | M_ZERO);
		if (g == NULL) {
			if (newdev) {
				free(d, M_DEVBUF, sizeof(*d));
				sc->sc_devs[target] = NULL;
			}

			return (ENOMEM);
		}

		TAILQ_INIT(&g->g_paths);
		g->g_dev = d;
		g->g_id = g_id;

		mtx_enter(&d->d_mtx);
		TAILQ_INSERT_TAIL(&d->d_groups, g, g_entry);
		mtx_leave(&d->d_mtx);
	}

	p->p_group = g;

	mtx_enter(&d->d_mtx);
	TAILQ_INSERT_TAIL(&g->g_paths, p, p_entry);
	if (!SIMPLEQ_EMPTY(&d->d_xfers))
		addxsh = 1;

	if (d->d_next_path == NULL)
		d->d_next_path = p;
	mtx_leave(&d->d_mtx);

	if (newdev)
		scsi_probe_target(mpath->sc_scsibus, target);
	else if (addxsh)
		scsi_xsh_add(&p->p_xsh);

	return (0);
}

int
mpath_path_detach(struct mpath_path *p)
{
	struct mpath_group *g = p->p_group;
	struct mpath_dev *d;
	struct mpath_path *np = NULL;

#ifdef DIAGNOSTIC
	if (g == NULL)
		panic("mpath: detaching a path from a nonexistent bus");
#endif /* DIAGNOSTIC */
	d = g->g_dev;
	p->p_group = NULL;

	mtx_enter(&d->d_mtx);
	TAILQ_REMOVE(&g->g_paths, p, p_entry);
	if (d->d_next_path == p)
		d->d_next_path = TAILQ_FIRST(&g->g_paths);

	if (TAILQ_EMPTY(&g->g_paths))
		TAILQ_REMOVE(&d->d_groups, g, g_entry);
	else
		g = NULL;

	if (!SIMPLEQ_EMPTY(&d->d_xfers))
		np = d->d_next_path;
	mtx_leave(&d->d_mtx);

	if (g != NULL)
		free(g, M_DEVBUF, sizeof(*g));

	scsi_xsh_del(&p->p_xsh);

	if (np == NULL)
		mpath_failover(d);
	else
		scsi_xsh_add(&np->p_xsh);

	return (0);
}

struct device *
mpath_bootdv(struct device *dev)
{
	struct mpath_softc *sc = mpath;
	struct mpath_dev *d;
	struct mpath_group *g;
	struct mpath_path *p;
	int target;

	if (sc == NULL)
		return (dev);

	for (target = 0; target < MPATH_BUSWIDTH; target++) {
		if ((d = sc->sc_devs[target]) == NULL)
			continue;

		TAILQ_FOREACH(g, &d->d_groups, g_entry) {
			TAILQ_FOREACH(p, &g->g_paths, p_entry) {
				if (p->p_link->device_softc == dev) {
					return (scsi_get_link(mpath->sc_scsibus,
					    target, 0)->device_softc);
				}
			}
		}
	}

	return (dev);
}
