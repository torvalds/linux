/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed
 * to Berkeley by John Heidemann of the UCLA Ficus project.
 *
 * Source: * @(#)i405_init.c 2.10 92/04/27 UCLA Ficus project
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)vfs_init.c	8.3 (Berkeley) 1/4/94
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/fnv_hash.h>
#include <sys/jail.h>
#include <sys/kernel.h>
#include <sys/linker.h>
#include <sys/mount.h>
#include <sys/proc.h>
#include <sys/sx.h>
#include <sys/syscallsubr.h>
#include <sys/sysctl.h>
#include <sys/vnode.h>
#include <sys/malloc.h>

static int	vfs_register(struct vfsconf *);
static int	vfs_unregister(struct vfsconf *);

MALLOC_DEFINE(M_VNODE, "vnodes", "Dynamically allocated vnodes");

/*
 * The highest defined VFS number.
 */
int maxvfsconf = VFS_GENERIC + 1;

/*
 * Single-linked list of configured VFSes.
 * New entries are added/deleted by vfs_register()/vfs_unregister()
 */
struct vfsconfhead vfsconf = TAILQ_HEAD_INITIALIZER(vfsconf);
struct sx vfsconf_sx;
SX_SYSINIT(vfsconf, &vfsconf_sx, "vfsconf");

/*
 * Loader.conf variable vfs.typenumhash enables setting vfc_typenum using a hash
 * calculation on vfc_name, so that it doesn't change when file systems are
 * loaded in a different order. This will avoid the NFS server file handles from
 * changing for file systems that use vfc_typenum in their fsid.
 */
static int	vfs_typenumhash = 1;
SYSCTL_INT(_vfs, OID_AUTO, typenumhash, CTLFLAG_RDTUN, &vfs_typenumhash, 0,
    "Set vfc_typenum using a hash calculation on vfc_name, so that it does not"
    "change when file systems are loaded in a different order.");

/*
 * A Zen vnode attribute structure.
 *
 * Initialized when the first filesystem registers by vfs_register().
 */
struct vattr va_null;

/*
 * vfs_init.c
 *
 * Allocate and fill in operations vectors.
 *
 * An undocumented feature of this approach to defining operations is that
 * there can be multiple entries in vfs_opv_descs for the same operations
 * vector. This allows third parties to extend the set of operations
 * supported by another layer in a binary compatibile way. For example,
 * assume that NFS needed to be modified to support Ficus. NFS has an entry
 * (probably nfs_vnopdeop_decls) declaring all the operations NFS supports by
 * default. Ficus could add another entry (ficus_nfs_vnodeop_decl_entensions)
 * listing those new operations Ficus adds to NFS, all without modifying the
 * NFS code. (Of couse, the OTW NFS protocol still needs to be munged, but
 * that is a(whole)nother story.) This is a feature.
 */

/*
 * Routines having to do with the management of the vnode table.
 */

static struct vfsconf *
vfs_byname_locked(const char *name)
{
	struct vfsconf *vfsp;

	sx_assert(&vfsconf_sx, SA_LOCKED);
	if (!strcmp(name, "ffs"))
		name = "ufs";
	TAILQ_FOREACH(vfsp, &vfsconf, vfc_list) {
		if (!strcmp(name, vfsp->vfc_name))
			return (vfsp);
	}
	return (NULL);
}

struct vfsconf *
vfs_byname(const char *name)
{
	struct vfsconf *vfsp;

	vfsconf_slock();
	vfsp = vfs_byname_locked(name);
	vfsconf_sunlock();
	return (vfsp);
}

struct vfsconf *
vfs_byname_kld(const char *fstype, struct thread *td, int *error)
{
	struct vfsconf *vfsp;
	int fileid, loaded;

	vfsp = vfs_byname(fstype);
	if (vfsp != NULL)
		return (vfsp);

	/* Try to load the respective module. */
	*error = kern_kldload(td, fstype, &fileid);
	loaded = (*error == 0);
	if (*error == EEXIST)
		*error = 0;
	if (*error)
		return (NULL);

	/* Look up again to see if the VFS was loaded. */
	vfsp = vfs_byname(fstype);
	if (vfsp == NULL) {
		if (loaded)
			(void)kern_kldunload(td, fileid, LINKER_UNLOAD_FORCE);
		*error = ENODEV;
		return (NULL);
	}
	return (vfsp);
}

static int
vfs_mount_sigdefer(struct mount *mp)
{
	int prev_stops, rc;

	TSRAW(curthread, TS_ENTER, "VFS_MOUNT", mp->mnt_vfc->vfc_name);
	prev_stops = sigdeferstop(SIGDEFERSTOP_SILENT);
	rc = (*mp->mnt_vfc->vfc_vfsops_sd->vfs_mount)(mp);
	sigallowstop(prev_stops);
	TSRAW(curthread, TS_EXIT, "VFS_MOUNT", mp->mnt_vfc->vfc_name);
	return (rc);
}

static int
vfs_unmount_sigdefer(struct mount *mp, int mntflags)
{
	int prev_stops, rc;

	prev_stops = sigdeferstop(SIGDEFERSTOP_SILENT);
	rc = (*mp->mnt_vfc->vfc_vfsops_sd->vfs_unmount)(mp, mntflags);
	sigallowstop(prev_stops);
	return (rc);
}

static int
vfs_root_sigdefer(struct mount *mp, int flags, struct vnode **vpp)
{
	int prev_stops, rc;

	prev_stops = sigdeferstop(SIGDEFERSTOP_SILENT);
	rc = (*mp->mnt_vfc->vfc_vfsops_sd->vfs_root)(mp, flags, vpp);
	sigallowstop(prev_stops);
	return (rc);
}

static int
vfs_quotactl_sigdefer(struct mount *mp, int cmd, uid_t uid, void *arg)
{
	int prev_stops, rc;

	prev_stops = sigdeferstop(SIGDEFERSTOP_SILENT);
	rc = (*mp->mnt_vfc->vfc_vfsops_sd->vfs_quotactl)(mp, cmd, uid, arg);
	sigallowstop(prev_stops);
	return (rc);
}

static int
vfs_statfs_sigdefer(struct mount *mp, struct statfs *sbp)
{
	int prev_stops, rc;

	prev_stops = sigdeferstop(SIGDEFERSTOP_SILENT);
	rc = (*mp->mnt_vfc->vfc_vfsops_sd->vfs_statfs)(mp, sbp);
	sigallowstop(prev_stops);
	return (rc);
}

static int
vfs_sync_sigdefer(struct mount *mp, int waitfor)
{
	int prev_stops, rc;

	prev_stops = sigdeferstop(SIGDEFERSTOP_SILENT);
	rc = (*mp->mnt_vfc->vfc_vfsops_sd->vfs_sync)(mp, waitfor);
	sigallowstop(prev_stops);
	return (rc);
}

static int
vfs_vget_sigdefer(struct mount *mp, ino_t ino, int flags, struct vnode **vpp)
{
	int prev_stops, rc;

	prev_stops = sigdeferstop(SIGDEFERSTOP_SILENT);
	rc = (*mp->mnt_vfc->vfc_vfsops_sd->vfs_vget)(mp, ino, flags, vpp);
	sigallowstop(prev_stops);
	return (rc);
}

static int
vfs_fhtovp_sigdefer(struct mount *mp, struct fid *fidp, int flags,
    struct vnode **vpp)
{
	int prev_stops, rc;

	prev_stops = sigdeferstop(SIGDEFERSTOP_SILENT);
	rc = (*mp->mnt_vfc->vfc_vfsops_sd->vfs_fhtovp)(mp, fidp, flags, vpp);
	sigallowstop(prev_stops);
	return (rc);
}

static int
vfs_checkexp_sigdefer(struct mount *mp, struct sockaddr *nam, int *exflg,
    struct ucred **credp, int *numsecflavors, int **secflavors)
{
	int prev_stops, rc;

	prev_stops = sigdeferstop(SIGDEFERSTOP_SILENT);
	rc = (*mp->mnt_vfc->vfc_vfsops_sd->vfs_checkexp)(mp, nam, exflg, credp,
	    numsecflavors, secflavors);
	sigallowstop(prev_stops);
	return (rc);
}

static int
vfs_extattrctl_sigdefer(struct mount *mp, int cmd, struct vnode *filename_vp,
    int attrnamespace, const char *attrname)
{
	int prev_stops, rc;

	prev_stops = sigdeferstop(SIGDEFERSTOP_SILENT);
	rc = (*mp->mnt_vfc->vfc_vfsops_sd->vfs_extattrctl)(mp, cmd,
	    filename_vp, attrnamespace, attrname);
	sigallowstop(prev_stops);
	return (rc);
}

static int
vfs_sysctl_sigdefer(struct mount *mp, fsctlop_t op, struct sysctl_req *req)
{
	int prev_stops, rc;

	prev_stops = sigdeferstop(SIGDEFERSTOP_SILENT);
	rc = (*mp->mnt_vfc->vfc_vfsops_sd->vfs_sysctl)(mp, op, req);
	sigallowstop(prev_stops);
	return (rc);
}

static void
vfs_susp_clean_sigdefer(struct mount *mp)
{
	int prev_stops;

	if (*mp->mnt_vfc->vfc_vfsops_sd->vfs_susp_clean == NULL)
		return;
	prev_stops = sigdeferstop(SIGDEFERSTOP_SILENT);
	(*mp->mnt_vfc->vfc_vfsops_sd->vfs_susp_clean)(mp);
	sigallowstop(prev_stops);
}

static void
vfs_reclaim_lowervp_sigdefer(struct mount *mp, struct vnode *vp)
{
	int prev_stops;

	if (*mp->mnt_vfc->vfc_vfsops_sd->vfs_reclaim_lowervp == NULL)
		return;
	prev_stops = sigdeferstop(SIGDEFERSTOP_SILENT);
	(*mp->mnt_vfc->vfc_vfsops_sd->vfs_reclaim_lowervp)(mp, vp);
	sigallowstop(prev_stops);
}

static void
vfs_unlink_lowervp_sigdefer(struct mount *mp, struct vnode *vp)
{
	int prev_stops;

	if (*mp->mnt_vfc->vfc_vfsops_sd->vfs_unlink_lowervp == NULL)
		return;
	prev_stops = sigdeferstop(SIGDEFERSTOP_SILENT);
	(*(mp)->mnt_vfc->vfc_vfsops_sd->vfs_unlink_lowervp)(mp, vp);
	sigallowstop(prev_stops);
}

static void
vfs_purge_sigdefer(struct mount *mp)
{
	int prev_stops;

	prev_stops = sigdeferstop(SIGDEFERSTOP_SILENT);
	(*mp->mnt_vfc->vfc_vfsops_sd->vfs_purge)(mp);
	sigallowstop(prev_stops);
}

static struct vfsops vfsops_sigdefer = {
	.vfs_mount =		vfs_mount_sigdefer,
	.vfs_unmount =		vfs_unmount_sigdefer,
	.vfs_root =		vfs_root_sigdefer,
	.vfs_quotactl =		vfs_quotactl_sigdefer,
	.vfs_statfs =		vfs_statfs_sigdefer,
	.vfs_sync =		vfs_sync_sigdefer,
	.vfs_vget =		vfs_vget_sigdefer,
	.vfs_fhtovp =		vfs_fhtovp_sigdefer,
	.vfs_checkexp =		vfs_checkexp_sigdefer,
	.vfs_extattrctl =	vfs_extattrctl_sigdefer,
	.vfs_sysctl =		vfs_sysctl_sigdefer,
	.vfs_susp_clean =	vfs_susp_clean_sigdefer,
	.vfs_reclaim_lowervp =	vfs_reclaim_lowervp_sigdefer,
	.vfs_unlink_lowervp =	vfs_unlink_lowervp_sigdefer,
	.vfs_purge =		vfs_purge_sigdefer,

};

/* Register a new filesystem type in the global table */
static int
vfs_register(struct vfsconf *vfc)
{
	struct sysctl_oid *oidp;
	struct vfsops *vfsops;
	static int once;
	struct vfsconf *tvfc;
	uint32_t hashval;
	int secondpass;

	if (!once) {
		vattr_null(&va_null);
		once = 1;
	}
	
	if (vfc->vfc_version != VFS_VERSION) {
		printf("ERROR: filesystem %s, unsupported ABI version %x\n",
		    vfc->vfc_name, vfc->vfc_version);
		return (EINVAL);
	}
	vfsconf_lock();
	if (vfs_byname_locked(vfc->vfc_name) != NULL) {
		vfsconf_unlock();
		return (EEXIST);
	}

	if (vfs_typenumhash != 0) {
		/*
		 * Calculate a hash on vfc_name to use for vfc_typenum. Unless
		 * all of 1<->255 are assigned, it is limited to 8bits since
		 * that is what ZFS uses from vfc_typenum and is also the
		 * preferred range for vfs_getnewfsid().
		 */
		hashval = fnv_32_str(vfc->vfc_name, FNV1_32_INIT);
		hashval &= 0xff;
		secondpass = 0;
		do {
			/* Look for and fix any collision. */
			TAILQ_FOREACH(tvfc, &vfsconf, vfc_list) {
				if (hashval == tvfc->vfc_typenum) {
					if (hashval == 255 && secondpass == 0) {
						hashval = 1;
						secondpass = 1;
					} else
						hashval++;
					break;
				}
			}
		} while (tvfc != NULL);
		vfc->vfc_typenum = hashval;
		if (vfc->vfc_typenum >= maxvfsconf)
			maxvfsconf = vfc->vfc_typenum + 1;
	} else
		vfc->vfc_typenum = maxvfsconf++;
	TAILQ_INSERT_TAIL(&vfsconf, vfc, vfc_list);

	/*
	 * Initialise unused ``struct vfsops'' fields, to use
	 * the vfs_std*() functions.  Note, we need the mount
	 * and unmount operations, at the least.  The check
	 * for vfsops available is just a debugging aid.
	 */
	KASSERT(vfc->vfc_vfsops != NULL,
	    ("Filesystem %s has no vfsops", vfc->vfc_name));
	/*
	 * Check the mount and unmount operations.
	 */
	vfsops = vfc->vfc_vfsops;
	KASSERT(vfsops->vfs_mount != NULL,
	    ("Filesystem %s has no mount op", vfc->vfc_name));
	KASSERT(vfsops->vfs_unmount != NULL,
	    ("Filesystem %s has no unmount op", vfc->vfc_name));

	if (vfsops->vfs_root == NULL)
		/* return file system's root vnode */
		vfsops->vfs_root =	vfs_stdroot;
	if (vfsops->vfs_quotactl == NULL)
		/* quota control */
		vfsops->vfs_quotactl =	vfs_stdquotactl;
	if (vfsops->vfs_statfs == NULL)
		/* return file system's status */
		vfsops->vfs_statfs =	vfs_stdstatfs;
	if (vfsops->vfs_sync == NULL)
		/*
		 * flush unwritten data (nosync)
		 * file systems can use vfs_stdsync
		 * explicitly by setting it in the
		 * vfsop vector.
		 */
		vfsops->vfs_sync =	vfs_stdnosync;
	if (vfsops->vfs_vget == NULL)
		/* convert an inode number to a vnode */
		vfsops->vfs_vget =	vfs_stdvget;
	if (vfsops->vfs_fhtovp == NULL)
		/* turn an NFS file handle into a vnode */
		vfsops->vfs_fhtovp =	vfs_stdfhtovp;
	if (vfsops->vfs_checkexp == NULL)
		/* check if file system is exported */
		vfsops->vfs_checkexp =	vfs_stdcheckexp;
	if (vfsops->vfs_init == NULL)
		/* file system specific initialisation */
		vfsops->vfs_init =	vfs_stdinit;
	if (vfsops->vfs_uninit == NULL)
		/* file system specific uninitialisation */
		vfsops->vfs_uninit =	vfs_stduninit;
	if (vfsops->vfs_extattrctl == NULL)
		/* extended attribute control */
		vfsops->vfs_extattrctl = vfs_stdextattrctl;
	if (vfsops->vfs_sysctl == NULL)
		vfsops->vfs_sysctl = vfs_stdsysctl;

	if ((vfc->vfc_flags & VFCF_SBDRY) != 0) {
		vfc->vfc_vfsops_sd = vfc->vfc_vfsops;
		vfc->vfc_vfsops = &vfsops_sigdefer;
	}

	if (vfc->vfc_flags & VFCF_JAIL)
		prison_add_vfs(vfc);

	/*
	 * Call init function for this VFS...
	 */
	if ((vfc->vfc_flags & VFCF_SBDRY) != 0)
		vfc->vfc_vfsops_sd->vfs_init(vfc);
	else
		vfc->vfc_vfsops->vfs_init(vfc);
	vfsconf_unlock();

	/*
	 * If this filesystem has a sysctl node under vfs
	 * (i.e. vfs.xxfs), then change the oid number of that node to
	 * match the filesystem's type number.  This allows user code
	 * which uses the type number to read sysctl variables defined
	 * by the filesystem to continue working. Since the oids are
	 * in a sorted list, we need to make sure the order is
	 * preserved by re-registering the oid after modifying its
	 * number.
	 */
	sysctl_wlock();
	SLIST_FOREACH(oidp, SYSCTL_CHILDREN(&sysctl___vfs), oid_link) {
		if (strcmp(oidp->oid_name, vfc->vfc_name) == 0) {
			sysctl_unregister_oid(oidp);
			oidp->oid_number = vfc->vfc_typenum;
			sysctl_register_oid(oidp);
			break;
		}
	}
	sysctl_wunlock();

	return (0);
}


/* Remove registration of a filesystem type */
static int
vfs_unregister(struct vfsconf *vfc)
{
	struct vfsconf *vfsp;
	int error, maxtypenum;

	vfsconf_lock();
	vfsp = vfs_byname_locked(vfc->vfc_name);
	if (vfsp == NULL) {
		vfsconf_unlock();
		return (EINVAL);
	}
	if (vfsp->vfc_refcount != 0) {
		vfsconf_unlock();
		return (EBUSY);
	}
	error = 0;
	if ((vfc->vfc_flags & VFCF_SBDRY) != 0) {
		if (vfc->vfc_vfsops_sd->vfs_uninit != NULL)
			error = vfc->vfc_vfsops_sd->vfs_uninit(vfsp);
	} else {
		if (vfc->vfc_vfsops->vfs_uninit != NULL) {
			error = vfc->vfc_vfsops->vfs_uninit(vfsp);
	}
	if (error != 0) {
		vfsconf_unlock();
		return (error);
	}
	}
	TAILQ_REMOVE(&vfsconf, vfsp, vfc_list);
	maxtypenum = VFS_GENERIC;
	TAILQ_FOREACH(vfsp, &vfsconf, vfc_list)
		if (maxtypenum < vfsp->vfc_typenum)
			maxtypenum = vfsp->vfc_typenum;
	maxvfsconf = maxtypenum + 1;
	vfsconf_unlock();
	return (0);
}

/*
 * Standard kernel module handling code for filesystem modules.
 * Referenced from VFS_SET().
 */
int
vfs_modevent(module_t mod, int type, void *data)
{
	struct vfsconf *vfc;
	int error = 0;

	vfc = (struct vfsconf *)data;

	switch (type) {
	case MOD_LOAD:
		if (vfc)
			error = vfs_register(vfc);
		break;

	case MOD_UNLOAD:
		if (vfc)
			error = vfs_unregister(vfc);
		break;
	default:
		error = EOPNOTSUPP;
		break;
	}
	return (error);
}
