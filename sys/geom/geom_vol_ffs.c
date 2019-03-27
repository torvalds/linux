/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2002, 2003 Gordon Tetlow
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
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

#include <sys/param.h>
#include <sys/errno.h>
#include <sys/systm.h>
#include <sys/sysctl.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/bio.h>
#include <sys/lock.h>
#include <sys/mutex.h>

#include <ufs/ufs/dinode.h>
#include <ufs/ffs/fs.h>

#include <geom/geom.h>
#include <geom/geom_slice.h>

FEATURE(geom_vol, "GEOM support for volume names from UFS superblock");

#define VOL_FFS_CLASS_NAME "VOL_FFS"

static int superblocks[] = SBLOCKSEARCH;
static int g_vol_ffs_once;

struct g_vol_ffs_softc {
	char *	vol;
};

static int
g_vol_ffs_start(struct bio *bp __unused)
{
	return(0);
}

static struct g_geom *
g_vol_ffs_taste(struct g_class *mp, struct g_provider *pp, int flags)
{
	struct g_geom *gp;
	struct g_consumer *cp;
	struct g_vol_ffs_softc *ms;
	int sb, superblock;
	struct fs *fs;

	g_trace(G_T_TOPOLOGY, "vol_taste(%s,%s)", mp->name, pp->name);
	g_topology_assert();

	/* 
	 * XXX This is a really weak way to make sure we don't recurse.
	 * Probably ought to use BIO_GETATTR to check for this.
	 */
	if (flags == G_TF_NORMAL &&
	    !strcmp(pp->geom->class->name, VOL_FFS_CLASS_NAME))
		return (NULL);

	gp = g_slice_new(mp, 1, pp, &cp, &ms, sizeof(*ms), g_vol_ffs_start);
	if (gp == NULL)
		return (NULL);
	g_topology_unlock();
	/*
	 * Walk through the standard places that superblocks hide and look
	 * for UFS magic. If we find magic, then check that the size in the
	 * superblock corresponds to the size of the underlying provider.
	 * Finally, look for a volume label and create an appropriate 
	 * provider based on that.
	 */
	for (sb=0; (superblock = superblocks[sb]) != -1; sb++) {
		/*
		 * Take care not to issue an invalid I/O request.  The
		 * offset and size of the superblock candidate must be
		 * multiples of the provider's sector size, otherwise an
		 * FFS can't exist on the provider anyway.
		 */
		if (superblock % cp->provider->sectorsize != 0 ||
		    SBLOCKSIZE % cp->provider->sectorsize != 0)
			continue;

		fs = (struct fs *) g_read_data(cp, superblock,
			SBLOCKSIZE, NULL);
		if (fs == NULL)
			continue;
		/* Check for magic and make sure things are the right size */
		if (fs->fs_magic == FS_UFS1_MAGIC) {
			if (fs->fs_old_size * fs->fs_fsize !=
			    (int32_t) pp->mediasize) {
				g_free(fs);
				continue;
			}
		} else if (fs->fs_magic == FS_UFS2_MAGIC) {
			if (fs->fs_size * fs->fs_fsize !=
			    (int64_t) pp->mediasize) {
				g_free(fs);
				continue;
			}
		} else {
			g_free(fs);
			continue;
		}
		/* Check for volume label */
		if (fs->fs_volname[0] == '\0') {
			g_free(fs);
			continue;
		}
		/* XXX We need to check for namespace conflicts. */
		/* XXX How do you handle a mirror set? */
		/* XXX We don't validate the volume name. */
		g_topology_lock();
		/* Alright, we have a label and a volume name, reconfig. */
		g_slice_config(gp, 0, G_SLICE_CONFIG_SET, (off_t) 0,
		    pp->mediasize, pp->sectorsize, "vol/%s",
		    fs->fs_volname);
		g_free(fs);
		g_topology_unlock();
		break;
	}
	g_topology_lock();
	g_access(cp, -1, 0, 0);
	if (LIST_EMPTY(&gp->provider)) {
		g_slice_spoiled(cp);
		return (NULL);
	}
	if (!g_vol_ffs_once) {
		g_vol_ffs_once = 1;
		printf(
		    "WARNING: geom_vol_Ffs (geom %s) is deprecated, "
		    "use glabel instead.\n", gp->name);
	}
	return (gp);
}

static struct g_class g_vol_ffs_class	= {
	.name = VOL_FFS_CLASS_NAME,
	.version = G_VERSION,
	.taste = g_vol_ffs_taste,
};

DECLARE_GEOM_CLASS(g_vol_ffs_class, g_vol_ffs);
MODULE_VERSION(geom_vol_ffs, 0);
