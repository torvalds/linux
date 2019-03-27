/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2002, 2003 Gordon Tetlow
 * Copyright (c) 2006 Pawel Jakub Dawidek <pjd@FreeBSD.org>
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/vnode.h>

#include <ufs/ufs/dinode.h>
#include <ufs/ffs/fs.h>
#include <ufs/ufs/quota.h>
#include <ufs/ufs/extattr.h>
#include <ufs/ffs/ffs_extern.h>

#include <geom/geom.h>
#include <geom/label/g_label.h>

#define G_LABEL_UFS_VOLUME_DIR	"ufs"
#define G_LABEL_UFS_ID_DIR	"ufsid"

#define	G_LABEL_UFS_VOLUME	0
#define	G_LABEL_UFS_ID		1

/*
 * G_LABEL_UFS_CMP returns true if difference between provider mediasize
 * and filesystem size is less than G_LABEL_UFS_MAXDIFF sectors
 */
#define	G_LABEL_UFS_CMP(prov, fsys, size) 				   \
	( abs( ((fsys)->size) - ( (prov)->mediasize / (fsys)->fs_fsize ))  \
				< G_LABEL_UFS_MAXDIFF )
#define	G_LABEL_UFS_MAXDIFF	0x100

/*
 * Try to find a superblock on the provider. If successful, then
 * check that the size in the superblock corresponds to the size
 * of the underlying provider. Finally, look for a volume label
 * and create an appropriate provider based on that.
 */
static void
g_label_ufs_taste_common(struct g_consumer *cp, char *label, size_t size, int what)
{
	struct g_provider *pp;
	struct fs *fs;

	g_topology_assert_not();
	pp = cp->provider;
	label[0] = '\0';

	fs = NULL;
	if (SBLOCKSIZE % pp->sectorsize != 0 ||
	    ffs_sbget(cp, &fs, STDSB, M_GEOM, g_use_g_read_data) != 0) {
		KASSERT(fs == NULL,
		    ("g_label_ufs_taste_common: non-NULL fs %p\n", fs));
		return;
	}

	/*
	 * Check for magic. We also need to check if file system size
	 * is almost equal to providers size, because sysinstall(8)
	 * used to bogusly put first partition at offset 0
	 * instead of 16, and glabel/ufs would find file system on slice
	 * instead of partition.
	 *
	 * In addition, media size can be a bit bigger than file system
	 * size. For instance, mkuzip can append bytes to align data
	 * to large sector size (it improves compression rates).
	 */
	if (fs->fs_magic == FS_UFS1_MAGIC && fs->fs_fsize > 0 &&
	    ( G_LABEL_UFS_CMP(pp, fs, fs_old_size)
		|| G_LABEL_UFS_CMP(pp, fs, fs_providersize))) {
		/* Valid UFS1. */
	} else if (fs->fs_magic == FS_UFS2_MAGIC && fs->fs_fsize > 0 &&
	    ( G_LABEL_UFS_CMP(pp, fs, fs_size)
		|| G_LABEL_UFS_CMP(pp, fs, fs_providersize))) {
		/* Valid UFS2. */
	} else {
		goto out;
	}
	G_LABEL_DEBUG(1, "%s file system detected on %s.",
	    fs->fs_magic == FS_UFS1_MAGIC ? "UFS1" : "UFS2", pp->name);
	switch (what) {
	case G_LABEL_UFS_VOLUME:
		/* Check for volume label */
		if (fs->fs_volname[0] != '\0')
			strlcpy(label, fs->fs_volname, size);
		break;
	case G_LABEL_UFS_ID:
		if (fs->fs_id[0] != 0 || fs->fs_id[1] != 0)
			snprintf(label, size, "%08x%08x", fs->fs_id[0],
			    fs->fs_id[1]);
		break;
	}
out:
	g_free(fs->fs_csp);
	g_free(fs);
}

static void
g_label_ufs_volume_taste(struct g_consumer *cp, char *label, size_t size)
{

	g_label_ufs_taste_common(cp, label, size, G_LABEL_UFS_VOLUME);
}

static void
g_label_ufs_id_taste(struct g_consumer *cp, char *label, size_t size)
{

	g_label_ufs_taste_common(cp, label, size, G_LABEL_UFS_ID);
}

struct g_label_desc g_label_ufs_volume = {
	.ld_taste = g_label_ufs_volume_taste,
	.ld_dir = G_LABEL_UFS_VOLUME_DIR,
	.ld_enabled = 1
};

struct g_label_desc g_label_ufs_id = {
	.ld_taste = g_label_ufs_id_taste,
	.ld_dir = G_LABEL_UFS_ID_DIR,
	.ld_enabled = 1
};

G_LABEL_INIT(ufsid, g_label_ufs_id, "Create device nodes for UFS file system IDs");
G_LABEL_INIT(ufs, g_label_ufs_volume, "Create device nodes for UFS volume names");

MODULE_DEPEND(g_label, ufs, 1, 1, 1);
