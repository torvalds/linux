/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008 Andrew Thompson <thompsa@FreeBSD.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
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

#include <sys/ctype.h>
#include <sys/param.h>
#include <sys/bio.h>
#include <sys/kernel.h>
#include <sys/limits.h>
#include <sys/malloc.h>
#include <sys/queue.h>
#include <sys/sysctl.h>
#include <sys/systm.h>

#include <geom/geom.h>
#include <sys/endian.h>

#include <geom/linux_lvm/g_linux_lvm.h>

FEATURE(geom_linux_lvm, "GEOM Linux LVM partitioning support");

/* Declare malloc(9) label */
static MALLOC_DEFINE(M_GLLVM, "gllvm", "GEOM_LINUX_LVM Data");

/* GEOM class methods */
static g_access_t g_llvm_access;
static g_init_t g_llvm_init;
static g_orphan_t g_llvm_orphan;
static g_orphan_t g_llvm_taste_orphan;
static g_start_t g_llvm_start;
static g_taste_t g_llvm_taste;
static g_ctl_destroy_geom_t g_llvm_destroy_geom;

static void	g_llvm_done(struct bio *);
static void	g_llvm_remove_disk(struct g_llvm_vg *, struct g_consumer *);
static int	g_llvm_activate_lv(struct g_llvm_vg *, struct g_llvm_lv *);
static int	g_llvm_add_disk(struct g_llvm_vg *, struct g_provider *, char *);
static void	g_llvm_free_vg(struct g_llvm_vg *);
static int	g_llvm_destroy(struct g_llvm_vg *, int);
static int	g_llvm_read_label(struct g_consumer *, struct g_llvm_label *);
static int	g_llvm_read_md(struct g_consumer *, struct g_llvm_metadata *,
		    struct g_llvm_label *);

static int	llvm_label_decode(const u_char *, struct g_llvm_label *, int);
static int	llvm_md_decode(const u_char *, struct g_llvm_metadata *,
		    struct g_llvm_label *);
static int	llvm_textconf_decode(u_char *, int,
		    struct g_llvm_metadata *);
static int	llvm_textconf_decode_pv(char **, char *, struct g_llvm_vg *);
static int	llvm_textconf_decode_lv(char **, char *, struct g_llvm_vg *);
static int	llvm_textconf_decode_sg(char **, char *, struct g_llvm_lv *);

SYSCTL_DECL(_kern_geom);
SYSCTL_NODE(_kern_geom, OID_AUTO, linux_lvm, CTLFLAG_RW, 0,
    "GEOM_LINUX_LVM stuff");
static u_int g_llvm_debug = 0;
SYSCTL_UINT(_kern_geom_linux_lvm, OID_AUTO, debug, CTLFLAG_RWTUN, &g_llvm_debug, 0,
    "Debug level");

LIST_HEAD(, g_llvm_vg) vg_list;

/*
 * Called to notify geom when it's been opened, and for what intent
 */
static int
g_llvm_access(struct g_provider *pp, int dr, int dw, int de)
{
	struct g_consumer *c;
	struct g_llvm_vg *vg;
	struct g_geom *gp;
	int error;

	KASSERT(pp != NULL, ("%s: NULL provider", __func__));
	gp = pp->geom;
	KASSERT(gp != NULL, ("%s: NULL geom", __func__));
	vg = gp->softc;

	if (vg == NULL) {
		/* It seems that .access can be called with negative dr,dw,dx
		 * in this case but I want to check for myself */
		G_LLVM_DEBUG(0, "access(%d, %d, %d) for %s",
		    dr, dw, de, pp->name);

		/* This should only happen when geom is withered so
		 * allow only negative requests */
		KASSERT(dr <= 0 && dw <= 0 && de <= 0,
		    ("%s: Positive access for %s", __func__, pp->name));
		if (pp->acr + dr == 0 && pp->acw + dw == 0 && pp->ace + de == 0)
			G_LLVM_DEBUG(0,
			    "Device %s definitely destroyed", pp->name);
		return (0);
	}

	/* Grab an exclusive bit to propagate on our consumers on first open */
	if (pp->acr == 0 && pp->acw == 0 && pp->ace == 0)
		de++;
	/* ... drop it on close */
	if (pp->acr + dr == 0 && pp->acw + dw == 0 && pp->ace + de == 0)
		de--;

	error = ENXIO;
	LIST_FOREACH(c, &gp->consumer, consumer) {
		KASSERT(c != NULL, ("%s: consumer is NULL", __func__));
		error = g_access(c, dr, dw, de);
		if (error != 0) {
			struct g_consumer *c2;

			/* Backout earlier changes */
			LIST_FOREACH(c2, &gp->consumer, consumer) {
				if (c2 == c) /* all eariler components fixed */
					return (error);
				g_access(c2, -dr, -dw, -de);
			}
		}
	}

	return (error);
}

/*
 * Dismantle bio_queue and destroy its components
 */
static void
bioq_dismantle(struct bio_queue_head *bq)
{
	struct bio *b;

	for (b = bioq_first(bq); b != NULL; b = bioq_first(bq)) {
		bioq_remove(bq, b);
		g_destroy_bio(b);
	}
}

/*
 * GEOM .done handler
 * Can't use standard handler because one requested IO may
 * fork into additional data IOs
 */
static void
g_llvm_done(struct bio *b)
{
	struct bio *parent_b;

	parent_b = b->bio_parent;

	if (b->bio_error != 0) {
		G_LLVM_DEBUG(0, "Error %d for offset=%ju, length=%ju on %s",
		    b->bio_error, b->bio_offset, b->bio_length,
		    b->bio_to->name);
		if (parent_b->bio_error == 0)
			parent_b->bio_error = b->bio_error;
	}

	parent_b->bio_inbed++;
	parent_b->bio_completed += b->bio_completed;

	if (parent_b->bio_children == parent_b->bio_inbed) {
		parent_b->bio_completed = parent_b->bio_length;
		g_io_deliver(parent_b, parent_b->bio_error);
	}
	g_destroy_bio(b);
}

static void
g_llvm_start(struct bio *bp)
{
	struct g_provider *pp;
	struct g_llvm_vg *vg;
	struct g_llvm_pv *pv;
	struct g_llvm_lv *lv;
	struct g_llvm_segment *sg;
	struct bio *cb;
	struct bio_queue_head bq;
	size_t chunk_size;
	off_t offset, length;
	char *addr;
	u_int count;

	pp = bp->bio_to;
	lv = pp->private;
	vg = pp->geom->softc;

	switch (bp->bio_cmd) {
	case BIO_READ:
	case BIO_WRITE:
	case BIO_DELETE:
	/* XXX BIO_GETATTR allowed? */
		break;
	default:
		g_io_deliver(bp, EOPNOTSUPP);
		return;
	}

	bioq_init(&bq);

	chunk_size = vg->vg_extentsize;
	addr = bp->bio_data;
	offset = bp->bio_offset;	/* virtual offset and length */
	length = bp->bio_length;

	while (length > 0) {
		size_t chunk_index, in_chunk_offset, in_chunk_length;

		pv = NULL;
		cb = g_clone_bio(bp);
		if (cb == NULL) {
			bioq_dismantle(&bq);
			if (bp->bio_error == 0)
				bp->bio_error = ENOMEM;
			g_io_deliver(bp, bp->bio_error);
			return;
		}

		/* get the segment and the pv */
		if (lv->lv_sgcount == 1) {
			/* skip much of the calculations for a single sg */
			chunk_index = 0;
			in_chunk_offset = 0;
			in_chunk_length = length;
			sg = lv->lv_firstsg;
			pv = sg->sg_pv;
			cb->bio_offset = offset + sg->sg_pvoffset;
		} else {
			chunk_index = offset / chunk_size; /* round downwards */
			in_chunk_offset = offset % chunk_size;
			in_chunk_length =
			    min(length, chunk_size - in_chunk_offset);

			/* XXX could be faster */
			LIST_FOREACH(sg, &lv->lv_segs, sg_next) {
				if (chunk_index >= sg->sg_start &&
				    chunk_index <= sg->sg_end) {
					/* adjust chunk index for sg start */
					chunk_index -= sg->sg_start;
					pv = sg->sg_pv;
					break;
				}
			}
			cb->bio_offset =
			    (off_t)chunk_index * (off_t)chunk_size
			    + in_chunk_offset + sg->sg_pvoffset;
		}

		KASSERT(pv != NULL, ("Can't find PV for chunk %zu",
		    chunk_index));

		cb->bio_to = pv->pv_gprov;
		cb->bio_done = g_llvm_done;
		cb->bio_length = in_chunk_length;
		cb->bio_data = addr;
		cb->bio_caller1 = pv;
		bioq_disksort(&bq, cb);

		G_LLVM_DEBUG(5,
		    "Mapped %s(%ju, %ju) on %s to %zu(%zu,%zu) @ %s:%ju",
		    bp->bio_cmd == BIO_READ ? "R" : "W",
		    offset, length, lv->lv_name,
		    chunk_index, in_chunk_offset, in_chunk_length,
		    pv->pv_name, cb->bio_offset);

		addr += in_chunk_length;
		length -= in_chunk_length;
		offset += in_chunk_length;
	}

	/* Fire off bio's here */
	count = 0;
	for (cb = bioq_first(&bq); cb != NULL; cb = bioq_first(&bq)) {
		bioq_remove(&bq, cb);
		pv = cb->bio_caller1;
		cb->bio_caller1 = NULL;
		G_LLVM_DEBUG(6, "firing bio to %s, offset=%ju, length=%ju",
		    cb->bio_to->name, cb->bio_offset, cb->bio_length);
		g_io_request(cb, pv->pv_gcons);
		count++;
	}
	if (count == 0) { /* We handled everything locally */
		bp->bio_completed = bp->bio_length;
		g_io_deliver(bp, 0);
	}
}

static void
g_llvm_remove_disk(struct g_llvm_vg *vg, struct g_consumer *cp)
{
	struct g_llvm_pv *pv;
	struct g_llvm_lv *lv;
	struct g_llvm_segment *sg;
	int found;

	KASSERT(cp != NULL, ("Non-valid disk in %s.", __func__));
	pv = (struct g_llvm_pv *)cp->private;

	G_LLVM_DEBUG(0, "Disk %s removed from %s.", cp->provider->name,
	    pv->pv_name);

	LIST_FOREACH(lv, &vg->vg_lvs, lv_next) {
		/* Find segments that map to this disk */
		found = 0;
		LIST_FOREACH(sg, &lv->lv_segs, sg_next) {
			if (sg->sg_pv == pv) {
				sg->sg_pv = NULL;
				lv->lv_sgactive--;
				found = 1;
				break;
			}
		}
		if (found) {
			G_LLVM_DEBUG(0, "Device %s removed.",
			    lv->lv_gprov->name);
			g_wither_provider(lv->lv_gprov, ENXIO);
			lv->lv_gprov = NULL;
		}
	}

	if (cp->acr > 0 || cp->acw > 0 || cp->ace > 0)
		g_access(cp, -cp->acr, -cp->acw, -cp->ace);
	g_detach(cp);
	g_destroy_consumer(cp);
}

static void
g_llvm_orphan(struct g_consumer *cp)
{
	struct g_llvm_vg *vg;
	struct g_geom *gp;

	g_topology_assert();
	gp = cp->geom;
	vg = gp->softc;
	if (vg == NULL)
		return;

	g_llvm_remove_disk(vg, cp);
	g_llvm_destroy(vg, 1);
}

static int
g_llvm_activate_lv(struct g_llvm_vg *vg, struct g_llvm_lv *lv)
{
	struct g_geom *gp;
	struct g_provider *pp;

	g_topology_assert();

	KASSERT(lv->lv_sgactive == lv->lv_sgcount, ("segment missing"));

	gp = vg->vg_geom;
	pp = g_new_providerf(gp, "linux_lvm/%s-%s", vg->vg_name, lv->lv_name);
	pp->mediasize = vg->vg_extentsize * (off_t)lv->lv_extentcount;
	pp->sectorsize = vg->vg_sectorsize;
	g_error_provider(pp, 0);
	lv->lv_gprov = pp;
	pp->private = lv;

	G_LLVM_DEBUG(1, "Created %s, %juM", pp->name,
	    pp->mediasize / (1024*1024));

	return (0);
}

static int
g_llvm_add_disk(struct g_llvm_vg *vg, struct g_provider *pp, char *uuid)
{
	struct g_geom *gp;
	struct g_consumer *cp, *fcp;
	struct g_llvm_pv *pv;
	struct g_llvm_lv *lv;
	struct g_llvm_segment *sg;
	int error;

	g_topology_assert();

	LIST_FOREACH(pv, &vg->vg_pvs, pv_next) {
		if (strcmp(pv->pv_uuid, uuid) == 0)
			break;	/* found it */
	}
	if (pv == NULL) {
		G_LLVM_DEBUG(3, "uuid %s not found in pv list", uuid);
		return (ENOENT);
	}
	if (pv->pv_gprov != NULL) {
		G_LLVM_DEBUG(0, "disk %s already initialised in %s",
		    pv->pv_name, vg->vg_name);
		return (EEXIST);
	}

	pv->pv_start *= vg->vg_sectorsize;
	gp = vg->vg_geom;
	fcp = LIST_FIRST(&gp->consumer);

	cp = g_new_consumer(gp);
	error = g_attach(cp, pp);
	G_LLVM_DEBUG(1, "Attached %s to %s at offset %ju",
	    pp->name, pv->pv_name, pv->pv_start);

	if (error != 0) {
		G_LLVM_DEBUG(0, "cannot attach %s to %s",
		    pp->name, vg->vg_name);
		g_destroy_consumer(cp);
		return (error);
	}

	if (fcp != NULL) {
		if (fcp->provider->sectorsize != pp->sectorsize) {
			G_LLVM_DEBUG(0, "Provider %s of %s has invalid "
			    "sector size (%d)", pp->name, vg->vg_name,
			    pp->sectorsize);
			return (EINVAL);
		}
		if (fcp->acr > 0 || fcp->acw || fcp->ace > 0) {
			/* Replicate access permissions from first "live"
			 * consumer to the new one */
			error = g_access(cp, fcp->acr, fcp->acw, fcp->ace);
			if (error != 0) {
				g_detach(cp);
				g_destroy_consumer(cp);
				return (error);
			}
		}
	}

	cp->private = pv;
	pv->pv_gcons = cp;
	pv->pv_gprov = pp;

	LIST_FOREACH(lv, &vg->vg_lvs, lv_next) {
		/* Find segments that map to this disk */
		LIST_FOREACH(sg, &lv->lv_segs, sg_next) {
			if (strcmp(sg->sg_pvname, pv->pv_name) == 0) {
				/* avtivate the segment */
				KASSERT(sg->sg_pv == NULL,
				    ("segment already mapped"));
				sg->sg_pvoffset =
				    (off_t)sg->sg_pvstart * vg->vg_extentsize
				    + pv->pv_start;
				sg->sg_pv = pv;
				lv->lv_sgactive++;

				G_LLVM_DEBUG(2, "%s: %d to %d @ %s:%d"
				    " offset %ju sector %ju",
				    lv->lv_name, sg->sg_start, sg->sg_end,
				    sg->sg_pvname, sg->sg_pvstart,
				    sg->sg_pvoffset,
				    sg->sg_pvoffset / vg->vg_sectorsize);
			}
		}
		/* Activate any lvs waiting on this disk */
		if (lv->lv_gprov == NULL && lv->lv_sgactive == lv->lv_sgcount) {
			error = g_llvm_activate_lv(vg, lv);
			if (error)
				break;
		}
	}
	return (error);
}

static void
g_llvm_init(struct g_class *mp)
{
	LIST_INIT(&vg_list);
}

static void
g_llvm_free_vg(struct g_llvm_vg *vg)
{
	struct g_llvm_pv *pv;
	struct g_llvm_lv *lv;
	struct g_llvm_segment *sg;

	/* Free all the structures */
	while ((pv = LIST_FIRST(&vg->vg_pvs)) != NULL) {
		LIST_REMOVE(pv, pv_next);
		free(pv, M_GLLVM);
	}
	while ((lv = LIST_FIRST(&vg->vg_lvs)) != NULL) {
		while ((sg = LIST_FIRST(&lv->lv_segs)) != NULL) {
			LIST_REMOVE(sg, sg_next);
			free(sg, M_GLLVM);
		}
		LIST_REMOVE(lv, lv_next);
		free(lv, M_GLLVM);
	}
	LIST_REMOVE(vg, vg_next);
	free(vg, M_GLLVM);
}

static void
g_llvm_taste_orphan(struct g_consumer *cp)
{

	KASSERT(1 == 0, ("%s called while tasting %s.", __func__,
	    cp->provider->name));
}

static struct g_geom *
g_llvm_taste(struct g_class *mp, struct g_provider *pp, int flags __unused)
{
	struct g_consumer *cp;
	struct g_geom *gp;
	struct g_llvm_label ll;
	struct g_llvm_metadata md;
	struct g_llvm_vg *vg;
	int error;

	bzero(&md, sizeof(md));

	g_topology_assert();
	g_trace(G_T_TOPOLOGY, "%s(%s, %s)", __func__, mp->name, pp->name);
	gp = g_new_geomf(mp, "linux_lvm:taste");
	/* This orphan function should be never called. */
	gp->orphan = g_llvm_taste_orphan;
	cp = g_new_consumer(gp);
	g_attach(cp, pp);
	error = g_llvm_read_label(cp, &ll);
	if (!error)
		error = g_llvm_read_md(cp, &md, &ll);
	g_detach(cp);
	g_destroy_consumer(cp);
	g_destroy_geom(gp);
	if (error != 0)
		return (NULL);

	vg = md.md_vg;
	if (vg->vg_geom == NULL) {
		/* new volume group */
		gp = g_new_geomf(mp, "%s", vg->vg_name);
		gp->start = g_llvm_start;
		gp->spoiled = g_llvm_orphan;
		gp->orphan = g_llvm_orphan;
		gp->access = g_llvm_access;
		vg->vg_sectorsize = pp->sectorsize;
		vg->vg_extentsize *= vg->vg_sectorsize;
		vg->vg_geom = gp;
		gp->softc = vg;
		G_LLVM_DEBUG(1, "Created volume %s, extent size %zuK",
		    vg->vg_name, vg->vg_extentsize / 1024);
	}

	/* initialise this disk in the volume group */
	g_llvm_add_disk(vg, pp, ll.ll_uuid);
	return (vg->vg_geom);
}

static int
g_llvm_destroy(struct g_llvm_vg *vg, int force)
{
	struct g_provider *pp;
	struct g_geom *gp;

	g_topology_assert();
	if (vg == NULL)
		return (ENXIO);
	gp = vg->vg_geom;

	LIST_FOREACH(pp, &gp->provider, provider) {
		if (pp->acr != 0 || pp->acw != 0 || pp->ace != 0) {
			G_LLVM_DEBUG(1, "Device %s is still open (r%dw%de%d)",
			    pp->name, pp->acr, pp->acw, pp->ace);
			if (!force)
				return (EBUSY);
		}
	}

	g_llvm_free_vg(gp->softc);
	gp->softc = NULL;
	g_wither_geom(gp, ENXIO);
	return (0);
}

static int
g_llvm_destroy_geom(struct gctl_req *req __unused, struct g_class *mp __unused,
    struct g_geom *gp)
{
	struct g_llvm_vg *vg;

	vg = gp->softc;
	return (g_llvm_destroy(vg, 0));
}

int
g_llvm_read_label(struct g_consumer *cp, struct g_llvm_label *ll)
{
	struct g_provider *pp;
	u_char *buf;
	int i, error = 0;

	g_topology_assert();

	/* The LVM label is stored on the first four sectors */
	error = g_access(cp, 1, 0, 0);
	if (error != 0)
		return (error);
	pp = cp->provider;
	g_topology_unlock();
	buf = g_read_data(cp, 0, pp->sectorsize * 4, &error);
	g_topology_lock();
	g_access(cp, -1, 0, 0);
	if (buf == NULL) {
		G_LLVM_DEBUG(1, "Cannot read metadata from %s (error=%d)",
		    pp->name, error);
		return (error);
	}

	/* Search the four sectors for the LVM label. */
	for (i = 0; i < 4; i++) {
		error = llvm_label_decode(&buf[i * pp->sectorsize], ll, i);
		if (error == 0)
			break;	/* found it */
	}
	g_free(buf);
	return (error);
}

int
g_llvm_read_md(struct g_consumer *cp, struct g_llvm_metadata *md,
    struct g_llvm_label *ll)
{
	struct g_provider *pp;
	u_char *buf;
	int error;
	int size;

	g_topology_assert();

	error = g_access(cp, 1, 0, 0);
	if (error != 0)
		return (error);
	pp = cp->provider;
	g_topology_unlock();
	buf = g_read_data(cp, ll->ll_md_offset, pp->sectorsize, &error);
	g_topology_lock();
	g_access(cp, -1, 0, 0);
	if (buf == NULL) {
		G_LLVM_DEBUG(0, "Cannot read metadata from %s (error=%d)",
		    cp->provider->name, error);
		return (error);
	}

	error = llvm_md_decode(buf, md, ll);
	g_free(buf);
	if (error != 0) {
		return (error);
	}

	G_LLVM_DEBUG(1, "reading LVM2 config @ %s:%ju", pp->name,
		    ll->ll_md_offset + md->md_reloffset);
	error = g_access(cp, 1, 0, 0);
	if (error != 0)
		return (error);
	pp = cp->provider;
	g_topology_unlock();
	/* round up to the nearest sector */
	size = md->md_relsize +
	    (pp->sectorsize - md->md_relsize % pp->sectorsize);
	buf = g_read_data(cp, ll->ll_md_offset + md->md_reloffset, size, &error);
	g_topology_lock();
	g_access(cp, -1, 0, 0);
	if (buf == NULL) {
		G_LLVM_DEBUG(0, "Cannot read LVM2 config from %s (error=%d)",
		    pp->name, error);
		return (error);
	}
	buf[md->md_relsize] = '\0';
	G_LLVM_DEBUG(10, "LVM config:\n%s\n", buf);
	error = llvm_textconf_decode(buf, md->md_relsize, md);
	g_free(buf);

	return (error);
}

static int
llvm_label_decode(const u_char *data, struct g_llvm_label *ll, int sector)
{
	uint64_t off;
	char *uuid;

	/* Magic string */
	if (bcmp("LABELONE", data , 8) != 0)
		return (EINVAL);

	/* We only support LVM2 text format */
	if (bcmp("LVM2 001", data + 24, 8) != 0) {
		G_LLVM_DEBUG(0, "Unsupported LVM format");
		return (EINVAL);
	}

	ll->ll_sector = le64dec(data + 8);
	ll->ll_crc = le32dec(data + 16);
	ll->ll_offset = le32dec(data + 20);

	if (ll->ll_sector != sector) {
		G_LLVM_DEBUG(0, "Expected sector %ju, found at %d",
		    ll->ll_sector, sector);
		return (EINVAL);
	}

	off = ll->ll_offset;
	/*
	 * convert the binary uuid to string format, the format is
	 * xxxxxx-xxxx-xxxx-xxxx-xxxx-xxxx-xxxxxx (6-4-4-4-4-4-6)
	 */
	uuid = ll->ll_uuid;
	bcopy(data + off, uuid, 6);
	off += 6;
	uuid += 6;
	*uuid++ = '-';
	for (int i = 0; i < 5; i++) {
		bcopy(data + off, uuid, 4);
		off += 4;
		uuid += 4;
		*uuid++ = '-';
	}
	bcopy(data + off, uuid, 6);
	off += 6;
	uuid += 6;
	*uuid++ = '\0';

	ll->ll_size = le64dec(data + off);
	off += 8;
	ll->ll_pestart = le64dec(data + off);
	off += 16;

	/* Only one data section is supported */
	if (le64dec(data + off) != 0) {
		G_LLVM_DEBUG(0, "Only one data section supported");
		return (EINVAL);
	}

	off += 16;
	ll->ll_md_offset = le64dec(data + off);
	off += 8;
	ll->ll_md_size = le64dec(data + off);
	off += 8;

	G_LLVM_DEBUG(1, "LVM metadata: offset=%ju, size=%ju", ll->ll_md_offset,
	    ll->ll_md_size);

	/* Only one data section is supported */
	if (le64dec(data + off) != 0) {
		G_LLVM_DEBUG(0, "Only one metadata section supported");
		return (EINVAL);
	}

	G_LLVM_DEBUG(2, "label uuid=%s", ll->ll_uuid);
	G_LLVM_DEBUG(2, "sector=%ju, crc=%u, offset=%u, size=%ju, pestart=%ju",
	    ll->ll_sector, ll->ll_crc, ll->ll_offset, ll->ll_size,
	    ll->ll_pestart);

	return (0);
}

static int
llvm_md_decode(const u_char *data, struct g_llvm_metadata *md,
    struct g_llvm_label *ll)
{
	uint64_t off;
	char magic[16];

	off = 0;
	md->md_csum = le32dec(data + off);
	off += 4;
	bcopy(data + off, magic, 16);
	off += 16;
	md->md_version = le32dec(data + off);
	off += 4;
	md->md_start = le64dec(data + off);
	off += 8;
	md->md_size = le64dec(data + off);
	off += 8;

	if (bcmp(G_LLVM_MAGIC, magic, 16) != 0) {
		G_LLVM_DEBUG(0, "Incorrect md magic number");
		return (EINVAL);
	}
	if (md->md_version != 1) {
		G_LLVM_DEBUG(0, "Incorrect md version number (%u)",
		    md->md_version);
		return (EINVAL);
	}
	if (md->md_start != ll->ll_md_offset) {
		G_LLVM_DEBUG(0, "Incorrect md offset (%ju)", md->md_start);
		return (EINVAL);
	}

	/* Aparently only one is ever returned */
	md->md_reloffset = le64dec(data + off);
	off += 8;
	md->md_relsize = le64dec(data + off);
	off += 16;	/* XXX skipped checksum */

	if (le64dec(data + off) != 0) {
		G_LLVM_DEBUG(0, "Only one reloc supported");
		return (EINVAL);
	}

	G_LLVM_DEBUG(3, "reloc: offset=%ju, size=%ju",
	    md->md_reloffset, md->md_relsize);
	G_LLVM_DEBUG(3, "md: version=%u, start=%ju, size=%ju",
	    md->md_version, md->md_start, md->md_size);

	return (0);
}

#define	GRAB_INT(key, tok1, tok2, v)					\
	if (tok1 && tok2 && strncmp(tok1, key, sizeof(key)) == 0) {	\
		v = strtol(tok2, &tok1, 10);				\
		if (tok1 == tok2)					\
			/* strtol did not eat any of the buffer */	\
			goto bad;					\
		continue;						\
	}

#define	GRAB_STR(key, tok1, tok2, v, len)				\
	if (tok1 && tok2 && strncmp(tok1, key, sizeof(key)) == 0) {	\
		strsep(&tok2, "\"");					\
		if (tok2 == NULL)					\
			continue;					\
		tok1 = strsep(&tok2, "\"");				\
		if (tok2 == NULL)					\
			continue;					\
		strncpy(v, tok1, len);					\
		continue;						\
	}

#define	SPLIT(key, value, str)						\
	key = strsep(&value, str);					\
	/* strip trailing whitespace on the key */			\
	for (char *t = key; *t != '\0'; t++)				\
		if (isspace(*t)) {					\
			*t = '\0';					\
			break;						\
		}

static size_t 
llvm_grab_name(char *name, const char *tok)
{
	size_t len;

	len = 0;
	if (tok == NULL)
		return (0);
	if (tok[0] == '-')
		return (0);
	if (strcmp(tok, ".") == 0 || strcmp(tok, "..") == 0)
		return (0);
	while (tok[len] && (isalpha(tok[len]) || isdigit(tok[len]) ||
	    tok[len] == '.' || tok[len] == '_' || tok[len] == '-' ||
	    tok[len] == '+') && len < G_LLVM_NAMELEN - 1)
		len++;
	bcopy(tok, name, len);
	name[len] = '\0';
	return (len);
}

static int
llvm_textconf_decode(u_char *data, int buflen, struct g_llvm_metadata *md)
{
	struct g_llvm_vg	*vg;
	char *buf = data;
	char *tok, *v;
	char name[G_LLVM_NAMELEN];
	char uuid[G_LLVM_UUIDLEN];
	size_t len;

	if (buf == NULL || *buf == '\0')
		return (EINVAL);

	tok = strsep(&buf, "\n");
	if (tok == NULL)
		return (EINVAL);
	len = llvm_grab_name(name, tok);
	if (len == 0)
		return (EINVAL);

	/* check too see if the vg has already been loaded off another disk */
	LIST_FOREACH(vg, &vg_list, vg_next) {
		if (strcmp(vg->vg_name, name) == 0) {
			uuid[0] = '\0';
			/* grab the volume group uuid */
			while ((tok = strsep(&buf, "\n")) != NULL) {
				if (strstr(tok, "{"))
					break;
				if (strstr(tok, "=")) {
					SPLIT(v, tok, "=");
					GRAB_STR("id", v, tok, uuid,
					    sizeof(uuid));
				}
			}
			if (strcmp(vg->vg_uuid, uuid) == 0) {
				/* existing vg */
				md->md_vg = vg;
				return (0);
			}
			/* XXX different volume group with name clash! */
			G_LLVM_DEBUG(0,
			    "%s already exists, volume group not loaded", name);
			return (EINVAL);
		}
	}

	vg = malloc(sizeof(*vg), M_GLLVM, M_NOWAIT|M_ZERO);
	if (vg == NULL)
		return (ENOMEM);

	strncpy(vg->vg_name, name, sizeof(vg->vg_name));
	LIST_INIT(&vg->vg_pvs);
	LIST_INIT(&vg->vg_lvs);

#define	VOL_FOREACH(func, tok, buf, p)					\
	while ((tok = strsep(buf, "\n")) != NULL) {			\
		if (strstr(tok, "{")) {					\
			func(buf, tok, p);				\
			continue;					\
		}							\
		if (strstr(tok, "}"))					\
			break;						\
	}

	while ((tok = strsep(&buf, "\n")) != NULL) {
		if (strcmp(tok, "physical_volumes {") == 0) {
			VOL_FOREACH(llvm_textconf_decode_pv, tok, &buf, vg);
			continue;
		}
		if (strcmp(tok, "logical_volumes {") == 0) {
			VOL_FOREACH(llvm_textconf_decode_lv, tok, &buf, vg);
			continue;
		}
		if (strstr(tok, "{")) {
			G_LLVM_DEBUG(2, "unknown section %s", tok);
			continue;
		}

		/* parse 'key = value' lines */
		if (strstr(tok, "=")) {
			SPLIT(v, tok, "=");
			GRAB_STR("id", v, tok, vg->vg_uuid, sizeof(vg->vg_uuid));
			GRAB_INT("extent_size", v, tok, vg->vg_extentsize);
			continue;
		}
	}
	/* basic checking */
	if (vg->vg_extentsize == 0)
		goto bad;

	md->md_vg = vg;
	LIST_INSERT_HEAD(&vg_list, vg, vg_next);
	G_LLVM_DEBUG(3, "vg: name=%s uuid=%s", vg->vg_name, vg->vg_uuid);
	return(0);

bad:
	g_llvm_free_vg(vg);
	return (-1);
}
#undef	VOL_FOREACH

static int
llvm_textconf_decode_pv(char **buf, char *tok, struct g_llvm_vg *vg)
{
	struct g_llvm_pv	*pv;
	char *v;
	size_t len;

	if (*buf == NULL || **buf == '\0')
		return (EINVAL);

	pv = malloc(sizeof(*pv), M_GLLVM, M_NOWAIT|M_ZERO);
	if (pv == NULL)
		return (ENOMEM);

	pv->pv_vg = vg;
	len = 0;
	if (tok == NULL)
		goto bad;
	len = llvm_grab_name(pv->pv_name, tok);
	if (len == 0)
		goto bad;

	while ((tok = strsep(buf, "\n")) != NULL) {
		if (strstr(tok, "{"))
			goto bad;

		if (strstr(tok, "}"))
			break;

		/* parse 'key = value' lines */
		if (strstr(tok, "=")) {
			SPLIT(v, tok, "=");
			GRAB_STR("id", v, tok, pv->pv_uuid, sizeof(pv->pv_uuid));
			GRAB_INT("pe_start", v, tok, pv->pv_start);
			GRAB_INT("pe_count", v, tok, pv->pv_count);
			continue;
		}
	}
	if (tok == NULL)
		goto bad;
	/* basic checking */
	if (pv->pv_count == 0)
		goto bad;

	LIST_INSERT_HEAD(&vg->vg_pvs, pv, pv_next);
	G_LLVM_DEBUG(3, "pv: name=%s uuid=%s", pv->pv_name, pv->pv_uuid);

	return (0);
bad:
	free(pv, M_GLLVM);
	return (-1);
}

static int
llvm_textconf_decode_lv(char **buf, char *tok, struct g_llvm_vg *vg)
{
	struct g_llvm_lv	*lv;
	struct g_llvm_segment *sg;
	char *v;
	size_t len;

	if (*buf == NULL || **buf == '\0')
		return (EINVAL);

	lv = malloc(sizeof(*lv), M_GLLVM, M_NOWAIT|M_ZERO);
	if (lv == NULL)
		return (ENOMEM);

	lv->lv_vg = vg;
	LIST_INIT(&lv->lv_segs);

	if (tok == NULL)
		goto bad;
	len = llvm_grab_name(lv->lv_name, tok);
	if (len == 0)
		goto bad;

	while ((tok = strsep(buf, "\n")) != NULL) {
		if (strstr(tok, "{")) {
			if (strstr(tok, "segment")) {
				llvm_textconf_decode_sg(buf, tok, lv);
				continue;
			} else
				/* unexpected section */
				goto bad;
		}

		if (strstr(tok, "}"))
			break;

		/* parse 'key = value' lines */
		if (strstr(tok, "=")) {
			SPLIT(v, tok, "=");
			GRAB_STR("id", v, tok, lv->lv_uuid, sizeof(lv->lv_uuid));
			GRAB_INT("segment_count", v, tok, lv->lv_sgcount);
			continue;
		}
	}
	if (tok == NULL)
		goto bad;
	if (lv->lv_sgcount == 0 || lv->lv_sgcount != lv->lv_numsegs)
		/* zero or incomplete segment list */
		goto bad;

	/* Optimize for only one segment on the pv */
	lv->lv_firstsg = LIST_FIRST(&lv->lv_segs);
	LIST_INSERT_HEAD(&vg->vg_lvs, lv, lv_next);
	G_LLVM_DEBUG(3, "lv: name=%s uuid=%s", lv->lv_name, lv->lv_uuid);

	return (0);
bad:
	while ((sg = LIST_FIRST(&lv->lv_segs)) != NULL) {
		LIST_REMOVE(sg, sg_next);
		free(sg, M_GLLVM);
	}
	free(lv, M_GLLVM);
	return (-1);
}

static int
llvm_textconf_decode_sg(char **buf, char *tok, struct g_llvm_lv *lv)
{
	struct g_llvm_segment *sg;
	char *v;
	int count = 0;

	if (*buf == NULL || **buf == '\0')
		return (EINVAL);

	sg = malloc(sizeof(*sg), M_GLLVM, M_NOWAIT|M_ZERO);
	if (sg == NULL)
		return (ENOMEM);

	while ((tok = strsep(buf, "\n")) != NULL) {
		/* only a single linear stripe is supported */
		if (strstr(tok, "stripe_count")) {
			SPLIT(v, tok, "=");
			GRAB_INT("stripe_count", v, tok, count);
			if (count != 1)
				goto bad;
		}

		if (strstr(tok, "{"))
			goto bad;

		if (strstr(tok, "}"))
			break;

		if (strcmp(tok, "stripes = [") == 0) {
			tok = strsep(buf, "\n");
			if (tok == NULL)
				goto bad;

			strsep(&tok, "\"");
			if (tok == NULL)
				goto bad;	/* missing open quotes */
			v = strsep(&tok, "\"");
			if (tok == NULL)
				goto bad;	/* missing close quotes */
			strncpy(sg->sg_pvname, v, sizeof(sg->sg_pvname));
			if (*tok != ',')
				goto bad;	/* missing comma for stripe */
			tok++;

			sg->sg_pvstart = strtol(tok, &v, 10);
			if (v == tok)
				/* strtol did not eat any of the buffer */
				goto bad;

			continue;
		}

		/* parse 'key = value' lines */
		if (strstr(tok, "=")) {
			SPLIT(v, tok, "=");
			GRAB_INT("start_extent", v, tok, sg->sg_start);
			GRAB_INT("extent_count", v, tok, sg->sg_count);
			continue;
		}
	}
	if (tok == NULL)
		goto bad;
	/* basic checking */
	if (count != 1 || sg->sg_count == 0)
		goto bad;

	sg->sg_end = sg->sg_start + sg->sg_count - 1;
	lv->lv_numsegs++;
	lv->lv_extentcount += sg->sg_count;
	LIST_INSERT_HEAD(&lv->lv_segs, sg, sg_next);

	return (0);
bad:
	free(sg, M_GLLVM);
	return (-1);
}
#undef	GRAB_INT
#undef	GRAB_STR
#undef	SPLIT

static struct g_class g_llvm_class = {
	.name = G_LLVM_CLASS_NAME,
	.version = G_VERSION,
	.init = g_llvm_init,
	.taste = g_llvm_taste,
	.destroy_geom = g_llvm_destroy_geom
};

DECLARE_GEOM_CLASS(g_llvm_class, g_linux_lvm);
MODULE_VERSION(geom_linux_lvm, 0);
