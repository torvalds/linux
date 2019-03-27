/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005-2006 Pawel Jakub Dawidek <pjd@FreeBSD.org>
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
#include <sys/vnode.h>
#include <sys/mount.h>

#include <ufs/ufs/extattr.h>
#include <ufs/ufs/quota.h>
#include <ufs/ufs/inode.h>
#include <ufs/ufs/ufs_extern.h>
#include <ufs/ufs/ufsmount.h>

#include <ufs/ffs/fs.h>
#include <ufs/ffs/ffs_extern.h>

#include <geom/geom.h>
#include <geom/journal/g_journal.h>

static int
g_journal_ufs_clean(struct mount *mp)
{
	struct ufsmount *ump;
	struct fs *fs;
	int flags;

	ump = VFSTOUFS(mp);
	fs = ump->um_fs;

	flags = fs->fs_flags;
	fs->fs_flags &= ~(FS_UNCLEAN | FS_NEEDSFSCK);
	ffs_sbupdate(ump, MNT_WAIT, 1);
	fs->fs_flags = flags;

	return (0);
}

static void
g_journal_ufs_dirty(struct g_consumer *cp)
{
	struct fs *fs;
	int error;

	fs = NULL;
	if (SBLOCKSIZE % cp->provider->sectorsize != 0 ||
	    ffs_sbget(cp, &fs, STDSB, M_GEOM, g_use_g_read_data) != 0) {
		GJ_DEBUG(0, "Cannot find superblock to mark file system %s "
		    "as dirty.", cp->provider->name);
		KASSERT(fs == NULL,
		    ("g_journal_ufs_dirty: non-NULL fs %p\n", fs));
		return;
	}
	GJ_DEBUG(0, "clean=%d flags=0x%x", fs->fs_clean, fs->fs_flags);
	fs->fs_clean = 0;
	fs->fs_flags |= FS_NEEDSFSCK | FS_UNCLEAN;
	error = ffs_sbput(cp, fs, fs->fs_sblockloc, g_use_g_write_data);
	g_free(fs->fs_csp);
	g_free(fs);
	if (error != 0) {
		GJ_DEBUG(0, "Cannot mark file system %s as dirty "
		    "(error=%d).", cp->provider->name, error);
	} else {
		GJ_DEBUG(0, "File system %s marked as dirty.",
		    cp->provider->name);
	}
}

const struct g_journal_desc g_journal_ufs = {
	.jd_fstype = "ufs",
	.jd_clean = g_journal_ufs_clean,
	.jd_dirty = g_journal_ufs_dirty
};

MODULE_DEPEND(g_journal, ufs, 1, 1, 1);
MODULE_VERSION(geom_journal, 0);
