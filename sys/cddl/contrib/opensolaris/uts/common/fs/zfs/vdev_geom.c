/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright (c) 2006 Pawel Jakub Dawidek <pjd@FreeBSD.org>
 * All rights reserved.
 *
 * Portions Copyright (c) 2012 Martin Matuska <mm@FreeBSD.org>
 */

#include <sys/zfs_context.h>
#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/bio.h>
#include <sys/disk.h>
#include <sys/spa.h>
#include <sys/spa_impl.h>
#include <sys/vdev_impl.h>
#include <sys/fs/zfs.h>
#include <sys/zio.h>
#include <geom/geom.h>
#include <geom/geom_int.h>

/*
 * Virtual device vector for GEOM.
 */

static g_attrchanged_t vdev_geom_attrchanged;
struct g_class zfs_vdev_class = {
	.name = "ZFS::VDEV",
	.version = G_VERSION,
	.attrchanged = vdev_geom_attrchanged,
};

struct consumer_vdev_elem {
	SLIST_ENTRY(consumer_vdev_elem)	elems;
	vdev_t				*vd;
};

SLIST_HEAD(consumer_priv_t, consumer_vdev_elem);
_Static_assert(sizeof(((struct g_consumer*)NULL)->private)
    == sizeof(struct consumer_priv_t*),
    "consumer_priv_t* can't be stored in g_consumer.private");

DECLARE_GEOM_CLASS(zfs_vdev_class, zfs_vdev);

SYSCTL_DECL(_vfs_zfs_vdev);
/* Don't send BIO_FLUSH. */
static int vdev_geom_bio_flush_disable;
SYSCTL_INT(_vfs_zfs_vdev, OID_AUTO, bio_flush_disable, CTLFLAG_RWTUN,
    &vdev_geom_bio_flush_disable, 0, "Disable BIO_FLUSH");
/* Don't send BIO_DELETE. */
static int vdev_geom_bio_delete_disable;
SYSCTL_INT(_vfs_zfs_vdev, OID_AUTO, bio_delete_disable, CTLFLAG_RWTUN,
    &vdev_geom_bio_delete_disable, 0, "Disable BIO_DELETE");

/* Declare local functions */
static void vdev_geom_detach(struct g_consumer *cp, boolean_t open_for_read);

/*
 * Thread local storage used to indicate when a thread is probing geoms
 * for their guids.  If NULL, this thread is not tasting geoms.  If non NULL,
 * it is looking for a replacement for the vdev_t* that is its value.
 */
uint_t zfs_geom_probe_vdev_key;

static void
vdev_geom_set_rotation_rate(vdev_t *vd, struct g_consumer *cp)
{ 
	int error;
	uint16_t rate;

	error = g_getattr("GEOM::rotation_rate", cp, &rate);
	if (error == 0 && rate == 1)
		vd->vdev_nonrot = B_TRUE;
	else
		vd->vdev_nonrot = B_FALSE;
}

static void
vdev_geom_set_physpath(vdev_t *vd, struct g_consumer *cp,
		       boolean_t do_null_update)
{
	boolean_t needs_update = B_FALSE;
	char *physpath;
	int error, physpath_len;

	physpath_len = MAXPATHLEN;
	physpath = g_malloc(physpath_len, M_WAITOK|M_ZERO);
	error = g_io_getattr("GEOM::physpath", cp, &physpath_len, physpath);
	if (error == 0) {
		char *old_physpath;

		/* g_topology lock ensures that vdev has not been closed */
		g_topology_assert();
		old_physpath = vd->vdev_physpath;
		vd->vdev_physpath = spa_strdup(physpath);

		if (old_physpath != NULL) {
			needs_update = (strcmp(old_physpath,
						vd->vdev_physpath) != 0);
			spa_strfree(old_physpath);
		} else
			needs_update = do_null_update;
	}
	g_free(physpath);

	/*
	 * If the physical path changed, update the config.
	 * Only request an update for previously unset physpaths if
	 * requested by the caller.
	 */
	if (needs_update)
		spa_async_request(vd->vdev_spa, SPA_ASYNC_CONFIG_UPDATE);

}

static void
vdev_geom_attrchanged(struct g_consumer *cp, const char *attr)
{
	char *old_physpath;
	struct consumer_priv_t *priv;
	struct consumer_vdev_elem *elem;
	int error;

	priv = (struct consumer_priv_t*)&cp->private;
	if (SLIST_EMPTY(priv))
		return;

	SLIST_FOREACH(elem, priv, elems) {
		vdev_t *vd = elem->vd;
		if (strcmp(attr, "GEOM::rotation_rate") == 0) {
			vdev_geom_set_rotation_rate(vd, cp);
			return;
		}
		if (strcmp(attr, "GEOM::physpath") == 0) {
			vdev_geom_set_physpath(vd, cp, /*null_update*/B_TRUE);
			return;
		}
	}
}

static void
vdev_geom_orphan(struct g_consumer *cp)
{
	struct consumer_priv_t *priv;
	struct consumer_vdev_elem *elem;

	g_topology_assert();

	priv = (struct consumer_priv_t*)&cp->private;
	if (SLIST_EMPTY(priv))
		/* Vdev close in progress.  Ignore the event. */
		return;

	/*
	 * Orphan callbacks occur from the GEOM event thread.
	 * Concurrent with this call, new I/O requests may be
	 * working their way through GEOM about to find out
	 * (only once executed by the g_down thread) that we've
	 * been orphaned from our disk provider.  These I/Os
	 * must be retired before we can detach our consumer.
	 * This is most easily achieved by acquiring the
	 * SPA ZIO configuration lock as a writer, but doing
	 * so with the GEOM topology lock held would cause
	 * a lock order reversal.  Instead, rely on the SPA's
	 * async removal support to invoke a close on this
	 * vdev once it is safe to do so.
	 */
	SLIST_FOREACH(elem, priv, elems) {
		vdev_t *vd = elem->vd;

		vd->vdev_remove_wanted = B_TRUE;
		spa_async_request(vd->vdev_spa, SPA_ASYNC_REMOVE);
	}
}

static struct g_consumer *
vdev_geom_attach(struct g_provider *pp, vdev_t *vd, boolean_t sanity)
{
	struct g_geom *gp;
	struct g_consumer *cp;
	int error;

	g_topology_assert();

	ZFS_LOG(1, "Attaching to %s.", pp->name);

	if (sanity) {
		if (pp->sectorsize > VDEV_PAD_SIZE || !ISP2(pp->sectorsize)) {
			ZFS_LOG(1, "Failing attach of %s. "
				   "Incompatible sectorsize %d\n",
			    pp->name, pp->sectorsize);
			return (NULL);
		} else if (pp->mediasize < SPA_MINDEVSIZE) {
			ZFS_LOG(1, "Failing attach of %s. "
				   "Incompatible mediasize %ju\n",
			    pp->name, pp->mediasize);
			return (NULL);
		}
	}

	/* Do we have geom already? No? Create one. */
	LIST_FOREACH(gp, &zfs_vdev_class.geom, geom) {
		if (gp->flags & G_GEOM_WITHER)
			continue;
		if (strcmp(gp->name, "zfs::vdev") != 0)
			continue;
		break;
	}
	if (gp == NULL) {
		gp = g_new_geomf(&zfs_vdev_class, "zfs::vdev");
		gp->orphan = vdev_geom_orphan;
		gp->attrchanged = vdev_geom_attrchanged;
		cp = g_new_consumer(gp);
		error = g_attach(cp, pp);
		if (error != 0) {
			ZFS_LOG(1, "%s(%d): g_attach failed: %d\n", __func__,
			    __LINE__, error);
			vdev_geom_detach(cp, B_FALSE);
			return (NULL);
		}
		error = g_access(cp, 1, 0, 1);
		if (error != 0) {
			ZFS_LOG(1, "%s(%d): g_access failed: %d\n", __func__,
			       __LINE__, error);
			vdev_geom_detach(cp, B_FALSE);
			return (NULL);
		}
		ZFS_LOG(1, "Created geom and consumer for %s.", pp->name);
	} else {
		/* Check if we are already connected to this provider. */
		LIST_FOREACH(cp, &gp->consumer, consumer) {
			if (cp->provider == pp) {
				ZFS_LOG(1, "Found consumer for %s.", pp->name);
				break;
			}
		}
		if (cp == NULL) {
			cp = g_new_consumer(gp);
			error = g_attach(cp, pp);
			if (error != 0) {
				ZFS_LOG(1, "%s(%d): g_attach failed: %d\n",
				    __func__, __LINE__, error);
				vdev_geom_detach(cp, B_FALSE);
				return (NULL);
			}
			error = g_access(cp, 1, 0, 1);
			if (error != 0) {
				ZFS_LOG(1, "%s(%d): g_access failed: %d\n",
				    __func__, __LINE__, error);
				vdev_geom_detach(cp, B_FALSE);
				return (NULL);
			}
			ZFS_LOG(1, "Created consumer for %s.", pp->name);
		} else {
			error = g_access(cp, 1, 0, 1);
			if (error != 0) {
				ZFS_LOG(1, "%s(%d): g_access failed: %d\n",
				    __func__, __LINE__, error);
				return (NULL);
			}
			ZFS_LOG(1, "Used existing consumer for %s.", pp->name);
		}
	}

	if (vd != NULL)
		vd->vdev_tsd = cp;

	cp->flags |= G_CF_DIRECT_SEND | G_CF_DIRECT_RECEIVE;
	return (cp);
}

static void
vdev_geom_detach(struct g_consumer *cp, boolean_t open_for_read)
{
	struct g_geom *gp;

	g_topology_assert();

	ZFS_LOG(1, "Detaching from %s.",
	    cp->provider && cp->provider->name ? cp->provider->name : "NULL");

	gp = cp->geom;
	if (open_for_read)
		g_access(cp, -1, 0, -1);
	/* Destroy consumer on last close. */
	if (cp->acr == 0 && cp->ace == 0) {
		if (cp->acw > 0)
			g_access(cp, 0, -cp->acw, 0);
		if (cp->provider != NULL) {
			ZFS_LOG(1, "Destroying consumer for %s.",
			    cp->provider->name ? cp->provider->name : "NULL");
			g_detach(cp);
		}
		g_destroy_consumer(cp);
	}
	/* Destroy geom if there are no consumers left. */
	if (LIST_EMPTY(&gp->consumer)) {
		ZFS_LOG(1, "Destroyed geom %s.", gp->name);
		g_wither_geom(gp, ENXIO);
	}
}

static void
vdev_geom_close_locked(vdev_t *vd)
{
	struct g_consumer *cp;
	struct consumer_priv_t *priv;
	struct consumer_vdev_elem *elem, *elem_temp;

	g_topology_assert();

	cp = vd->vdev_tsd;
	vd->vdev_delayed_close = B_FALSE;
	if (cp == NULL)
		return;

	ZFS_LOG(1, "Closing access to %s.", cp->provider->name);
	KASSERT(cp->private != NULL, ("%s: cp->private is NULL", __func__));
	priv = (struct consumer_priv_t*)&cp->private;
	vd->vdev_tsd = NULL;
	SLIST_FOREACH_SAFE(elem, priv, elems, elem_temp) {
		if (elem->vd == vd) {
			SLIST_REMOVE(priv, elem, consumer_vdev_elem, elems);
			g_free(elem);
		}
	}

	vdev_geom_detach(cp, B_TRUE);
}

/*
 * Issue one or more bios to the vdev in parallel
 * cmds, datas, offsets, errors, and sizes are arrays of length ncmds.  Each IO
 * operation is described by parallel entries from each array.  There may be
 * more bios actually issued than entries in the array
 */
static void
vdev_geom_io(struct g_consumer *cp, int *cmds, void **datas, off_t *offsets,
    off_t *sizes, int *errors, int ncmds)
{
	struct bio **bios;
	u_char *p;
	off_t off, maxio, s, end;
	int i, n_bios, j;
	size_t bios_size;

	maxio = MAXPHYS - (MAXPHYS % cp->provider->sectorsize);
	n_bios = 0;

	/* How many bios are required for all commands ? */
	for (i = 0; i < ncmds; i++)
		n_bios += (sizes[i] + maxio - 1) / maxio;

	/* Allocate memory for the bios */
	bios_size = n_bios * sizeof(struct bio*);
	bios = kmem_zalloc(bios_size, KM_SLEEP);

	/* Prepare and issue all of the bios */
	for (i = j = 0; i < ncmds; i++) {
		off = offsets[i];
		p = datas[i];
		s = sizes[i];
		end = off + s;
		ASSERT((off % cp->provider->sectorsize) == 0);
		ASSERT((s % cp->provider->sectorsize) == 0);

		for (; off < end; off += maxio, p += maxio, s -= maxio, j++) {
			bios[j] = g_alloc_bio();
			bios[j]->bio_cmd = cmds[i];
			bios[j]->bio_done = NULL;
			bios[j]->bio_offset = off;
			bios[j]->bio_length = MIN(s, maxio);
			bios[j]->bio_data = p;
			g_io_request(bios[j], cp);
		}
	}
	ASSERT(j == n_bios);

	/* Wait for all of the bios to complete, and clean them up */
	for (i = j = 0; i < ncmds; i++) {
		off = offsets[i];
		s = sizes[i];
		end = off + s;

		for (; off < end; off += maxio, s -= maxio, j++) {
			errors[i] = biowait(bios[j], "vdev_geom_io") || errors[i];
			g_destroy_bio(bios[j]);
		}
	}
	kmem_free(bios, bios_size);
}

/* 
 * Read the vdev config from a device.  Return the number of valid labels that
 * were found.  The vdev config will be returned in config if and only if at
 * least one valid label was found.
 */
static int
vdev_geom_read_config(struct g_consumer *cp, nvlist_t **configp)
{
	struct g_provider *pp;
	nvlist_t *config;
	vdev_phys_t *vdev_lists[VDEV_LABELS];
	char *buf;
	size_t buflen;
	uint64_t psize, state, txg;
	off_t offsets[VDEV_LABELS];
	off_t size;
	off_t sizes[VDEV_LABELS];
	int cmds[VDEV_LABELS];
	int errors[VDEV_LABELS];
	int l, nlabels;

	g_topology_assert_not();

	pp = cp->provider;
	ZFS_LOG(1, "Reading config from %s...", pp->name);

	psize = pp->mediasize;
	psize = P2ALIGN(psize, (uint64_t)sizeof(vdev_label_t));

	size = sizeof(*vdev_lists[0]) + pp->sectorsize -
	    ((sizeof(*vdev_lists[0]) - 1) % pp->sectorsize) - 1;

	buflen = sizeof(vdev_lists[0]->vp_nvlist);

	/* Create all of the IO requests */
	for (l = 0; l < VDEV_LABELS; l++) {
		cmds[l] = BIO_READ;
		vdev_lists[l] = kmem_alloc(size, KM_SLEEP);
		offsets[l] = vdev_label_offset(psize, l, 0) + VDEV_SKIP_SIZE;
		sizes[l] = size;
		errors[l] = 0;
		ASSERT(offsets[l] % pp->sectorsize == 0);
	}

	/* Issue the IO requests */
	vdev_geom_io(cp, cmds, (void**)vdev_lists, offsets, sizes, errors,
	    VDEV_LABELS);

	/* Parse the labels */
	config = *configp = NULL;
	nlabels = 0;
	for (l = 0; l < VDEV_LABELS; l++) {
		if (errors[l] != 0)
			continue;

		buf = vdev_lists[l]->vp_nvlist;

		if (nvlist_unpack(buf, buflen, &config, 0) != 0)
			continue;

		if (nvlist_lookup_uint64(config, ZPOOL_CONFIG_POOL_STATE,
		    &state) != 0 || state > POOL_STATE_L2CACHE) {
			nvlist_free(config);
			continue;
		}

		if (state != POOL_STATE_SPARE &&
		    state != POOL_STATE_L2CACHE &&
		    (nvlist_lookup_uint64(config, ZPOOL_CONFIG_POOL_TXG,
		    &txg) != 0 || txg == 0)) {
			nvlist_free(config);
			continue;
		}

		if (*configp != NULL)
			nvlist_free(*configp);
		*configp = config;

		nlabels++;
	}

	/* Free the label storage */
	for (l = 0; l < VDEV_LABELS; l++)
		kmem_free(vdev_lists[l], size);

	return (nlabels);
}

static void
resize_configs(nvlist_t ***configs, uint64_t *count, uint64_t id)
{
	nvlist_t **new_configs;
	uint64_t i;

	if (id < *count)
		return;
	new_configs = kmem_zalloc((id + 1) * sizeof(nvlist_t *),
	    KM_SLEEP);
	for (i = 0; i < *count; i++)
		new_configs[i] = (*configs)[i];
	if (*configs != NULL)
		kmem_free(*configs, *count * sizeof(void *));
	*configs = new_configs;
	*count = id + 1;
}

static void
process_vdev_config(nvlist_t ***configs, uint64_t *count, nvlist_t *cfg,
    const char *name, uint64_t* known_pool_guid)
{
	nvlist_t *vdev_tree;
	uint64_t pool_guid;
	uint64_t vdev_guid, known_guid;
	uint64_t id, txg, known_txg;
	char *pname;
	int i;

	if (nvlist_lookup_string(cfg, ZPOOL_CONFIG_POOL_NAME, &pname) != 0 ||
	    strcmp(pname, name) != 0)
		goto ignore;

	if (nvlist_lookup_uint64(cfg, ZPOOL_CONFIG_POOL_GUID, &pool_guid) != 0)
		goto ignore;

	if (nvlist_lookup_uint64(cfg, ZPOOL_CONFIG_TOP_GUID, &vdev_guid) != 0)
		goto ignore;

	if (nvlist_lookup_nvlist(cfg, ZPOOL_CONFIG_VDEV_TREE, &vdev_tree) != 0)
		goto ignore;

	if (nvlist_lookup_uint64(vdev_tree, ZPOOL_CONFIG_ID, &id) != 0)
		goto ignore;

	VERIFY(nvlist_lookup_uint64(cfg, ZPOOL_CONFIG_POOL_TXG, &txg) == 0);

	if (*known_pool_guid != 0) {
		if (pool_guid != *known_pool_guid)
			goto ignore;
	} else
		*known_pool_guid = pool_guid;

	resize_configs(configs, count, id);

	if ((*configs)[id] != NULL) {
		VERIFY(nvlist_lookup_uint64((*configs)[id],
		    ZPOOL_CONFIG_POOL_TXG, &known_txg) == 0);
		if (txg <= known_txg)
			goto ignore;
		nvlist_free((*configs)[id]);
	}

	(*configs)[id] = cfg;
	return;

ignore:
	nvlist_free(cfg);
}

int
vdev_geom_read_pool_label(const char *name,
    nvlist_t ***configs, uint64_t *count)
{
	struct g_class *mp;
	struct g_geom *gp;
	struct g_provider *pp;
	struct g_consumer *zcp;
	nvlist_t *vdev_cfg;
	uint64_t pool_guid;
	int error, nlabels;

	DROP_GIANT();
	g_topology_lock();

	*configs = NULL;
	*count = 0;
	pool_guid = 0;
	LIST_FOREACH(mp, &g_classes, class) {
		if (mp == &zfs_vdev_class)
			continue;
		LIST_FOREACH(gp, &mp->geom, geom) {
			if (gp->flags & G_GEOM_WITHER)
				continue;
			LIST_FOREACH(pp, &gp->provider, provider) {
				if (pp->flags & G_PF_WITHER)
					continue;
				zcp = vdev_geom_attach(pp, NULL, B_TRUE);
				if (zcp == NULL)
					continue;
				g_topology_unlock();
				nlabels = vdev_geom_read_config(zcp, &vdev_cfg);
				g_topology_lock();
				vdev_geom_detach(zcp, B_TRUE);
				if (nlabels == 0)
					continue;
				ZFS_LOG(1, "successfully read vdev config");

				process_vdev_config(configs, count,
				    vdev_cfg, name, &pool_guid);
			}
		}
	}
	g_topology_unlock();
	PICKUP_GIANT();

	return (*count > 0 ? 0 : ENOENT);
}

enum match {
	NO_MATCH = 0,		/* No matching labels found */
	TOPGUID_MATCH = 1,	/* Labels match top guid, not vdev guid*/
	ZERO_MATCH = 1,		/* Should never be returned */
	ONE_MATCH = 2,		/* 1 label matching the vdev_guid */
	TWO_MATCH = 3,		/* 2 label matching the vdev_guid */
	THREE_MATCH = 4,	/* 3 label matching the vdev_guid */
	FULL_MATCH = 5		/* all labels match the vdev_guid */
};

static enum match
vdev_attach_ok(vdev_t *vd, struct g_provider *pp)
{
	nvlist_t *config;
	uint64_t pool_guid, top_guid, vdev_guid;
	struct g_consumer *cp;
	int nlabels;

	cp = vdev_geom_attach(pp, NULL, B_TRUE);
	if (cp == NULL) {
		ZFS_LOG(1, "Unable to attach tasting instance to %s.",
		    pp->name);
		return (NO_MATCH);
	}
	g_topology_unlock();
	nlabels = vdev_geom_read_config(cp, &config);
	g_topology_lock();
	vdev_geom_detach(cp, B_TRUE);
	if (nlabels == 0) {
		ZFS_LOG(1, "Unable to read config from %s.", pp->name);
		return (NO_MATCH);
	}

	pool_guid = 0;
	(void) nvlist_lookup_uint64(config, ZPOOL_CONFIG_POOL_GUID, &pool_guid);
	top_guid = 0;
	(void) nvlist_lookup_uint64(config, ZPOOL_CONFIG_TOP_GUID, &top_guid);
	vdev_guid = 0;
	(void) nvlist_lookup_uint64(config, ZPOOL_CONFIG_GUID, &vdev_guid);
	nvlist_free(config);

	/*
	 * Check that the label's pool guid matches the desired guid.
	 * Inactive spares and L2ARCs do not have any pool guid in the label.
	 */
	if (pool_guid != 0 && pool_guid != spa_guid(vd->vdev_spa)) {
		ZFS_LOG(1, "pool guid mismatch for provider %s: %ju != %ju.",
		    pp->name,
		    (uintmax_t)spa_guid(vd->vdev_spa), (uintmax_t)pool_guid);
		return (NO_MATCH);
	}

	/*
	 * Check that the label's vdev guid matches the desired guid.
	 * The second condition handles possible race on vdev detach, when
	 * remaining vdev receives GUID of destroyed top level mirror vdev.
	 */
	if (vdev_guid == vd->vdev_guid) {
		ZFS_LOG(1, "guids match for provider %s.", pp->name);
		return (ZERO_MATCH + nlabels);
	} else if (top_guid == vd->vdev_guid && vd == vd->vdev_top) {
		ZFS_LOG(1, "top vdev guid match for provider %s.", pp->name);
		return (TOPGUID_MATCH);
	}
	ZFS_LOG(1, "vdev guid mismatch for provider %s: %ju != %ju.",
	    pp->name, (uintmax_t)vd->vdev_guid, (uintmax_t)vdev_guid);
	return (NO_MATCH);
}

static struct g_consumer *
vdev_geom_attach_by_guids(vdev_t *vd)
{
	struct g_class *mp;
	struct g_geom *gp;
	struct g_provider *pp, *best_pp;
	struct g_consumer *cp;
	const char *vdpath;
	enum match match, best_match;

	g_topology_assert();

	vdpath = vd->vdev_path + sizeof("/dev/") - 1;
	cp = NULL;
	best_pp = NULL;
	best_match = NO_MATCH;
	LIST_FOREACH(mp, &g_classes, class) {
		if (mp == &zfs_vdev_class)
			continue;
		LIST_FOREACH(gp, &mp->geom, geom) {
			if (gp->flags & G_GEOM_WITHER)
				continue;
			LIST_FOREACH(pp, &gp->provider, provider) {
				match = vdev_attach_ok(vd, pp);
				if (match > best_match) {
					best_match = match;
					best_pp = pp;
				} else if (match == best_match) {
					if (strcmp(pp->name, vdpath) == 0) {
						best_pp = pp;
					}
				}
				if (match == FULL_MATCH)
					goto out;
			}
		}
	}

out:
	if (best_pp) {
		cp = vdev_geom_attach(best_pp, vd, B_TRUE);
		if (cp == NULL) {
			printf("ZFS WARNING: Unable to attach to %s.\n",
			    best_pp->name);
		}
	}
	return (cp);
}

static struct g_consumer *
vdev_geom_open_by_guids(vdev_t *vd)
{
	struct g_consumer *cp;
	char *buf;
	size_t len;

	g_topology_assert();

	ZFS_LOG(1, "Searching by guids [%ju:%ju].",
		(uintmax_t)spa_guid(vd->vdev_spa), (uintmax_t)vd->vdev_guid);
	cp = vdev_geom_attach_by_guids(vd);
	if (cp != NULL) {
		len = strlen(cp->provider->name) + strlen("/dev/") + 1;
		buf = kmem_alloc(len, KM_SLEEP);

		snprintf(buf, len, "/dev/%s", cp->provider->name);
		spa_strfree(vd->vdev_path);
		vd->vdev_path = buf;

		ZFS_LOG(1, "Attach by guid [%ju:%ju] succeeded, provider %s.",
		    (uintmax_t)spa_guid(vd->vdev_spa),
		    (uintmax_t)vd->vdev_guid, cp->provider->name);
	} else {
		ZFS_LOG(1, "Search by guid [%ju:%ju] failed.",
		    (uintmax_t)spa_guid(vd->vdev_spa),
		    (uintmax_t)vd->vdev_guid);
	}

	return (cp);
}

static struct g_consumer *
vdev_geom_open_by_path(vdev_t *vd, int check_guid)
{
	struct g_provider *pp;
	struct g_consumer *cp;

	g_topology_assert();

	cp = NULL;
	pp = g_provider_by_name(vd->vdev_path + sizeof("/dev/") - 1);
	if (pp != NULL) {
		ZFS_LOG(1, "Found provider by name %s.", vd->vdev_path);
		if (!check_guid || vdev_attach_ok(vd, pp) == FULL_MATCH)
			cp = vdev_geom_attach(pp, vd, B_FALSE);
	}

	return (cp);
}

static int
vdev_geom_open(vdev_t *vd, uint64_t *psize, uint64_t *max_psize,
    uint64_t *logical_ashift, uint64_t *physical_ashift)
{
	struct g_provider *pp;
	struct g_consumer *cp;
	size_t bufsize;
	int error;

	/* Set the TLS to indicate downstack that we should not access zvols*/
	VERIFY(tsd_set(zfs_geom_probe_vdev_key, vd) == 0);

	/*
	 * We must have a pathname, and it must be absolute.
	 */
	if (vd->vdev_path == NULL || strncmp(vd->vdev_path, "/dev/", 5) != 0) {
		vd->vdev_stat.vs_aux = VDEV_AUX_BAD_LABEL;
		return (EINVAL);
	}

	/*
	 * Reopen the device if it's not currently open. Otherwise,
	 * just update the physical size of the device.
	 */
	if ((cp = vd->vdev_tsd) != NULL) {
		ASSERT(vd->vdev_reopening);
		goto skip_open;
	}

	DROP_GIANT();
	g_topology_lock();
	error = 0;

	if (vd->vdev_spa->spa_splitting_newspa ||
	    (vd->vdev_prevstate == VDEV_STATE_UNKNOWN &&
	     vd->vdev_spa->spa_load_state == SPA_LOAD_NONE ||
	     vd->vdev_spa->spa_load_state == SPA_LOAD_CREATE)) {
		/*
		 * We are dealing with a vdev that hasn't been previously
		 * opened (since boot), and we are not loading an
		 * existing pool configuration.  This looks like a
		 * vdev add operation to a new or existing pool.
		 * Assume the user knows what he/she is doing and find
		 * GEOM provider by its name, ignoring GUID mismatches.
		 *
		 * XXPOLICY: It would be safer to only allow a device
		 *           that is unlabeled or labeled but missing
		 *           GUID information to be opened in this fashion,
		 *           unless we are doing a split, in which case we
		 *           should allow any guid.
		 */
		cp = vdev_geom_open_by_path(vd, 0);
	} else {
		/*
		 * Try using the recorded path for this device, but only
		 * accept it if its label data contains the expected GUIDs.
		 */
		cp = vdev_geom_open_by_path(vd, 1);
		if (cp == NULL) {
			/*
			 * The device at vd->vdev_path doesn't have the
			 * expected GUIDs. The disks might have merely
			 * moved around so try all other GEOM providers
			 * to find one with the right GUIDs.
			 */
			cp = vdev_geom_open_by_guids(vd);
		}
	}

	/* Clear the TLS now that tasting is done */
	VERIFY(tsd_set(zfs_geom_probe_vdev_key, NULL) == 0);

	if (cp == NULL) {
		ZFS_LOG(1, "Vdev %s not found.", vd->vdev_path);
		error = ENOENT;
	} else {
		struct consumer_priv_t *priv;
		struct consumer_vdev_elem *elem;
		int spamode;

		priv = (struct consumer_priv_t*)&cp->private;
		if (cp->private == NULL)
			SLIST_INIT(priv);
		elem = g_malloc(sizeof(*elem), M_WAITOK|M_ZERO);
		elem->vd = vd;
		SLIST_INSERT_HEAD(priv, elem, elems);

		spamode = spa_mode(vd->vdev_spa);
		if (cp->provider->sectorsize > VDEV_PAD_SIZE ||
		    !ISP2(cp->provider->sectorsize)) {
			ZFS_LOG(1, "Provider %s has unsupported sectorsize.",
			    cp->provider->name);

			vdev_geom_close_locked(vd);
			error = EINVAL;
			cp = NULL;
		} else if (cp->acw == 0 && (spamode & FWRITE) != 0) {
			int i;

			for (i = 0; i < 5; i++) {
				error = g_access(cp, 0, 1, 0);
				if (error == 0)
					break;
				g_topology_unlock();
				tsleep(vd, 0, "vdev", hz / 2);
				g_topology_lock();
			}
			if (error != 0) {
				printf("ZFS WARNING: Unable to open %s for writing (error=%d).\n",
				    cp->provider->name, error);
				vdev_geom_close_locked(vd);
				cp = NULL;
			}
		}
	}

	/* Fetch initial physical path information for this device. */
	if (cp != NULL) {
		vdev_geom_attrchanged(cp, "GEOM::physpath");
	
		/* Set other GEOM characteristics */
		vdev_geom_set_physpath(vd, cp, /*do_null_update*/B_FALSE);
		vdev_geom_set_rotation_rate(vd, cp);
	}

	g_topology_unlock();
	PICKUP_GIANT();
	if (cp == NULL) {
		vd->vdev_stat.vs_aux = VDEV_AUX_OPEN_FAILED;
		vdev_dbgmsg(vd, "vdev_geom_open: failed to open [error=%d]",
		    error);
		return (error);
	}
skip_open:
	pp = cp->provider;

	/*
	 * Determine the actual size of the device.
	 */
	*max_psize = *psize = pp->mediasize;

	/*
	 * Determine the device's minimum transfer size and preferred
	 * transfer size.
	 */
	*logical_ashift = highbit(MAX(pp->sectorsize, SPA_MINBLOCKSIZE)) - 1;
	*physical_ashift = 0;
	if (pp->stripesize > (1 << *logical_ashift) && ISP2(pp->stripesize) &&
	    pp->stripesize <= (1 << SPA_MAXASHIFT) && pp->stripeoffset == 0)
		*physical_ashift = highbit(pp->stripesize) - 1;

	/*
	 * Clear the nowritecache settings, so that on a vdev_reopen()
	 * we will try again.
	 */
	vd->vdev_nowritecache = B_FALSE;

	return (0);
}

static void
vdev_geom_close(vdev_t *vd)
{
	struct g_consumer *cp;

	cp = vd->vdev_tsd;

	DROP_GIANT();
	g_topology_lock();

	if (!vd->vdev_reopening ||
	    (cp != NULL && ((cp->flags & G_CF_ORPHAN) != 0 ||
	    (cp->provider != NULL && cp->provider->error != 0))))
		vdev_geom_close_locked(vd);

	g_topology_unlock();
	PICKUP_GIANT();
}

static void
vdev_geom_io_intr(struct bio *bp)
{
	vdev_t *vd;
	zio_t *zio;

	zio = bp->bio_caller1;
	vd = zio->io_vd;
	zio->io_error = bp->bio_error;
	if (zio->io_error == 0 && bp->bio_resid != 0)
		zio->io_error = SET_ERROR(EIO);

	switch(zio->io_error) {
	case ENOTSUP:
		/*
		 * If we get ENOTSUP for BIO_FLUSH or BIO_DELETE we know
		 * that future attempts will never succeed. In this case
		 * we set a persistent flag so that we don't bother with
		 * requests in the future.
		 */
		switch(bp->bio_cmd) {
		case BIO_FLUSH:
			vd->vdev_nowritecache = B_TRUE;
			break;
		case BIO_DELETE:
			vd->vdev_notrim = B_TRUE;
			break;
		}
		break;
	case ENXIO:
		if (!vd->vdev_remove_wanted) {
			/*
			 * If provider's error is set we assume it is being
			 * removed.
			 */
			if (bp->bio_to->error != 0) {
				vd->vdev_remove_wanted = B_TRUE;
				spa_async_request(zio->io_spa,
				    SPA_ASYNC_REMOVE);
			} else if (!vd->vdev_delayed_close) {
				vd->vdev_delayed_close = B_TRUE;
			}
		}
		break;
	}

	/*
	 * We have to split bio freeing into two parts, because the ABD code
	 * cannot be called in this context and vdev_op_io_done is not called
	 * for ZIO_TYPE_IOCTL zio-s.
	 */
	if (zio->io_type != ZIO_TYPE_READ && zio->io_type != ZIO_TYPE_WRITE) {
		g_destroy_bio(bp);
		zio->io_bio = NULL;
	}
	zio_delay_interrupt(zio);
}

static void
vdev_geom_io_start(zio_t *zio)
{
	vdev_t *vd;
	struct g_consumer *cp;
	struct bio *bp;
	int error;

	vd = zio->io_vd;

	switch (zio->io_type) {
	case ZIO_TYPE_IOCTL:
		/* XXPOLICY */
		if (!vdev_readable(vd)) {
			zio->io_error = SET_ERROR(ENXIO);
			zio_interrupt(zio);
			return;
		} else {
			switch (zio->io_cmd) {
			case DKIOCFLUSHWRITECACHE:
				if (zfs_nocacheflush || vdev_geom_bio_flush_disable)
					break;
				if (vd->vdev_nowritecache) {
					zio->io_error = SET_ERROR(ENOTSUP);
					break;
				}
				goto sendreq;
			default:
				zio->io_error = SET_ERROR(ENOTSUP);
			}
		}

		zio_execute(zio);
		return;
	case ZIO_TYPE_FREE:
		if (vd->vdev_notrim) {
			zio->io_error = SET_ERROR(ENOTSUP);
		} else if (!vdev_geom_bio_delete_disable) {
			goto sendreq;
		}
		zio_execute(zio);
		return;
	}
sendreq:
	ASSERT(zio->io_type == ZIO_TYPE_READ ||
	    zio->io_type == ZIO_TYPE_WRITE ||
	    zio->io_type == ZIO_TYPE_FREE ||
	    zio->io_type == ZIO_TYPE_IOCTL);

	cp = vd->vdev_tsd;
	if (cp == NULL) {
		zio->io_error = SET_ERROR(ENXIO);
		zio_interrupt(zio);
		return;
	}
	bp = g_alloc_bio();
	bp->bio_caller1 = zio;
	switch (zio->io_type) {
	case ZIO_TYPE_READ:
	case ZIO_TYPE_WRITE:
		zio->io_target_timestamp = zio_handle_io_delay(zio);
		bp->bio_offset = zio->io_offset;
		bp->bio_length = zio->io_size;
		if (zio->io_type == ZIO_TYPE_READ) {
			bp->bio_cmd = BIO_READ;
			bp->bio_data =
			    abd_borrow_buf(zio->io_abd, zio->io_size);
		} else {
			bp->bio_cmd = BIO_WRITE;
			bp->bio_data =
			    abd_borrow_buf_copy(zio->io_abd, zio->io_size);
		}
		break;
	case ZIO_TYPE_FREE:
		bp->bio_cmd = BIO_DELETE;
		bp->bio_data = NULL;
		bp->bio_offset = zio->io_offset;
		bp->bio_length = zio->io_size;
		break;
	case ZIO_TYPE_IOCTL:
		bp->bio_cmd = BIO_FLUSH;
		bp->bio_data = NULL;
		bp->bio_offset = cp->provider->mediasize;
		bp->bio_length = 0;
		break;
	}
	bp->bio_done = vdev_geom_io_intr;
	zio->io_bio = bp;

	g_io_request(bp, cp);
}

static void
vdev_geom_io_done(zio_t *zio)
{
	struct bio *bp = zio->io_bio;

	if (zio->io_type != ZIO_TYPE_READ && zio->io_type != ZIO_TYPE_WRITE) {
		ASSERT(bp == NULL);
		return;
	}

	if (bp == NULL) {
		ASSERT3S(zio->io_error, ==, ENXIO);
		return;
	}

	if (zio->io_type == ZIO_TYPE_READ)
		abd_return_buf_copy(zio->io_abd, bp->bio_data, zio->io_size);
	else
		abd_return_buf(zio->io_abd, bp->bio_data, zio->io_size);

	g_destroy_bio(bp);
	zio->io_bio = NULL;
}

static void
vdev_geom_hold(vdev_t *vd)
{
}

static void
vdev_geom_rele(vdev_t *vd)
{
}

vdev_ops_t vdev_geom_ops = {
	vdev_geom_open,
	vdev_geom_close,
	vdev_default_asize,
	vdev_geom_io_start,
	vdev_geom_io_done,
	NULL,
	NULL,
	vdev_geom_hold,
	vdev_geom_rele,
	NULL,
	vdev_default_xlate,
	VDEV_TYPE_DISK,		/* name of this vdev type */
	B_TRUE			/* leaf vdev */
};
