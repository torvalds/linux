/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2014 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Edward Tomasz Napierala under sponsorship
 * from the FreeBSD Foundation.
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
 *
 */

#include <sys/cdefs.h>
 __FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/condvar.h>
#include <sys/ioccom.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/sx.h>
#include <sys/taskqueue.h>
#include <sys/tree.h>
#include <sys/vnode.h>

#include <fs/autofs/autofs.h>

static const char *autofs_opts[] = {
	"from", "master_options", "master_prefix", NULL
};

extern struct autofs_softc	*autofs_softc;

static int
autofs_mount(struct mount *mp)
{
	struct autofs_mount *amp;
	char *from, *fspath, *options, *prefix;
	int error;

	if (vfs_filteropt(mp->mnt_optnew, autofs_opts))
		return (EINVAL);

	if (mp->mnt_flag & MNT_UPDATE) {
		autofs_flush(VFSTOAUTOFS(mp));
		return (0);
	}

	if (vfs_getopt(mp->mnt_optnew, "from", (void **)&from, NULL))
		return (EINVAL);

	if (vfs_getopt(mp->mnt_optnew, "fspath", (void **)&fspath, NULL))
		return (EINVAL);

	if (vfs_getopt(mp->mnt_optnew, "master_options", (void **)&options, NULL))
		return (EINVAL);

	if (vfs_getopt(mp->mnt_optnew, "master_prefix", (void **)&prefix, NULL))
		return (EINVAL);

	amp = malloc(sizeof(*amp), M_AUTOFS, M_WAITOK | M_ZERO);
	mp->mnt_data = amp;
	amp->am_mp = mp;
	strlcpy(amp->am_from, from, sizeof(amp->am_from));
	strlcpy(amp->am_mountpoint, fspath, sizeof(amp->am_mountpoint));
	strlcpy(amp->am_options, options, sizeof(amp->am_options));
	strlcpy(amp->am_prefix, prefix, sizeof(amp->am_prefix));
	sx_init(&amp->am_lock, "autofslk");
	amp->am_last_fileno = 1;

	vfs_getnewfsid(mp);

	MNT_ILOCK(mp);
	mp->mnt_kern_flag |= MNTK_LOOKUP_SHARED;
	MNT_IUNLOCK(mp);

	AUTOFS_XLOCK(amp);
	error = autofs_node_new(NULL, amp, ".", -1, &amp->am_root);
	if (error != 0) {
		AUTOFS_XUNLOCK(amp);
		free(amp, M_AUTOFS);
		return (error);
	}
	AUTOFS_XUNLOCK(amp);

	vfs_mountedfrom(mp, from);

	return (0);
}

static int
autofs_unmount(struct mount *mp, int mntflags)
{
	struct autofs_mount *amp;
	struct autofs_node *anp;
	struct autofs_request *ar;
	int error, flags;
	bool found;

	amp = VFSTOAUTOFS(mp);

	flags = 0;
	if (mntflags & MNT_FORCE)
		flags |= FORCECLOSE;
	error = vflush(mp, 0, flags, curthread);
	if (error != 0) {
		AUTOFS_WARN("vflush failed with error %d", error);
		return (error);
	}

	/*
	 * All vnodes are gone, and new one will not appear - so,
	 * no new triggerings.  We can iterate over outstanding
	 * autofs_requests and terminate them.
	 */
	for (;;) {
		found = false;
		sx_xlock(&autofs_softc->sc_lock);
		TAILQ_FOREACH(ar, &autofs_softc->sc_requests, ar_next) {
			if (ar->ar_mount != amp)
				continue;
			ar->ar_error = ENXIO;
			ar->ar_done = true;
			ar->ar_in_progress = false;
			found = true;
		}
		sx_xunlock(&autofs_softc->sc_lock);
		if (found == false)
			break;

		cv_broadcast(&autofs_softc->sc_cv);
		pause("autofs_umount", 1);
	}

	AUTOFS_XLOCK(amp);

	/*
	 * Not terribly efficient, but at least not recursive.
	 */
	while (!RB_EMPTY(&amp->am_root->an_children)) {
		anp = RB_MIN(autofs_node_tree, &amp->am_root->an_children);
		while (!RB_EMPTY(&anp->an_children))
			anp = RB_MIN(autofs_node_tree, &anp->an_children);
		autofs_node_delete(anp);
	}
	autofs_node_delete(amp->am_root);

	mp->mnt_data = NULL;
	AUTOFS_XUNLOCK(amp);

	sx_destroy(&amp->am_lock);

	free(amp, M_AUTOFS);

	return (0);
}

static int
autofs_root(struct mount *mp, int flags, struct vnode **vpp)
{
	struct autofs_mount *amp;
	int error;

	amp = VFSTOAUTOFS(mp);

	error = autofs_node_vn(amp->am_root, mp, flags, vpp);

	return (error);
}

static int
autofs_statfs(struct mount *mp, struct statfs *sbp)
{

	sbp->f_bsize = S_BLKSIZE;
	sbp->f_iosize = 0;
	sbp->f_blocks = 0;
	sbp->f_bfree = 0;
	sbp->f_bavail = 0;
	sbp->f_files = 0;
	sbp->f_ffree = 0;

	return (0);
}

static struct vfsops autofs_vfsops = {
	.vfs_fhtovp =		NULL, /* XXX */
	.vfs_mount =		autofs_mount,
	.vfs_unmount =		autofs_unmount,
	.vfs_root =		autofs_root,
	.vfs_statfs =		autofs_statfs,
	.vfs_init =		autofs_init,
	.vfs_uninit =		autofs_uninit,
};

VFS_SET(autofs_vfsops, autofs, VFCF_SYNTHETIC | VFCF_NETWORK);
MODULE_VERSION(autofs, 1);
