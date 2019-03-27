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
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2012, 2015 by Delphix. All rights reserved.
 * Copyright 2015, OmniTI Computer Consulting, Inc. All rights reserved.
 */

/*
 * ZFS control directory (a.k.a. ".zfs")
 *
 * This directory provides a common location for all ZFS meta-objects.
 * Currently, this is only the 'snapshot' directory, but this may expand in the
 * future.  The elements are built using the GFS primitives, as the hierarchy
 * does not actually exist on disk.
 *
 * For 'snapshot', we don't want to have all snapshots always mounted, because
 * this would take up a huge amount of space in /etc/mnttab.  We have three
 * types of objects:
 *
 * 	ctldir ------> snapshotdir -------> snapshot
 *                                             |
 *                                             |
 *                                             V
 *                                         mounted fs
 *
 * The 'snapshot' node contains just enough information to lookup '..' and act
 * as a mountpoint for the snapshot.  Whenever we lookup a specific snapshot, we
 * perform an automount of the underlying filesystem and return the
 * corresponding vnode.
 *
 * All mounts are handled automatically by the kernel, but unmounts are
 * (currently) handled from user land.  The main reason is that there is no
 * reliable way to auto-unmount the filesystem when it's "no longer in use".
 * When the user unmounts a filesystem, we call zfsctl_unmount(), which
 * unmounts any snapshots within the snapshot directory.
 *
 * The '.zfs', '.zfs/snapshot', and all directories created under
 * '.zfs/snapshot' (ie: '.zfs/snapshot/<snapname>') are all GFS nodes and
 * share the same vfs_t as the head filesystem (what '.zfs' lives under).
 *
 * File systems mounted ontop of the GFS nodes '.zfs/snapshot/<snapname>'
 * (ie: snapshots) are ZFS nodes and have their own unique vfs_t.
 * However, vnodes within these mounted on file systems have their v_vfsp
 * fields set to the head filesystem to make NFS happy (see
 * zfsctl_snapdir_lookup()). We VFS_HOLD the head filesystem's vfs_t
 * so that it cannot be freed until all snapshots have been unmounted.
 */

#include <sys/zfs_context.h>
#include <sys/zfs_ctldir.h>
#include <sys/zfs_ioctl.h>
#include <sys/zfs_vfsops.h>
#include <sys/namei.h>
#include <sys/stat.h>
#include <sys/dmu.h>
#include <sys/dsl_dataset.h>
#include <sys/dsl_destroy.h>
#include <sys/dsl_deleg.h>
#include <sys/mount.h>
#include <sys/zap.h>

#include "zfs_namecheck.h"

/* Common access mode for all virtual directories under the ctldir */
const u_short zfsctl_ctldir_mode = S_IRUSR | S_IXUSR | S_IRGRP | S_IXGRP |
    S_IROTH | S_IXOTH;

/*
 * "Synthetic" filesystem implementation.
 */

/*
 * Assert that A implies B.
 */
#define KASSERT_IMPLY(A, B, msg)	KASSERT(!(A) || (B), (msg));

static MALLOC_DEFINE(M_SFSNODES, "sfs_nodes", "synthetic-fs nodes");

typedef struct sfs_node {
	char		sn_name[ZFS_MAX_DATASET_NAME_LEN];
	uint64_t	sn_parent_id;
	uint64_t	sn_id;
} sfs_node_t;

/*
 * Check the parent's ID as well as the node's to account for a chance
 * that IDs originating from different domains (snapshot IDs, artifical
 * IDs, znode IDs) may clash.
 */
static int
sfs_compare_ids(struct vnode *vp, void *arg)
{
	sfs_node_t *n1 = vp->v_data;
	sfs_node_t *n2 = arg;
	bool equal;

	equal = n1->sn_id == n2->sn_id &&
	    n1->sn_parent_id == n2->sn_parent_id;

	/* Zero means equality. */
	return (!equal);
}

static int
sfs_vnode_get(const struct mount *mp, int flags, uint64_t parent_id,
   uint64_t id, struct vnode **vpp)
{
	sfs_node_t search;
	int err;

	search.sn_id = id;
	search.sn_parent_id = parent_id;
	err = vfs_hash_get(mp, (u_int)id, flags, curthread, vpp,
	    sfs_compare_ids, &search);
	return (err);
}

static int
sfs_vnode_insert(struct vnode *vp, int flags, uint64_t parent_id,
   uint64_t id, struct vnode **vpp)
{
	int err;

	KASSERT(vp->v_data != NULL, ("sfs_vnode_insert with NULL v_data"));
	err = vfs_hash_insert(vp, (u_int)id, flags, curthread, vpp,
	    sfs_compare_ids, vp->v_data);
	return (err);
}

static void
sfs_vnode_remove(struct vnode *vp)
{
	vfs_hash_remove(vp);
}

typedef void sfs_vnode_setup_fn(vnode_t *vp, void *arg);

static int
sfs_vgetx(struct mount *mp, int flags, uint64_t parent_id, uint64_t id,
    const char *tag, struct vop_vector *vops,
    sfs_vnode_setup_fn setup, void *arg,
    struct vnode **vpp)
{
	struct vnode *vp;
	int error;

	error = sfs_vnode_get(mp, flags, parent_id, id, vpp);
	if (error != 0 || *vpp != NULL) {
		KASSERT_IMPLY(error == 0, (*vpp)->v_data != NULL,
		    "sfs vnode with no data");
		return (error);
	}

	/* Allocate a new vnode/inode. */
	error = getnewvnode(tag, mp, vops, &vp);
	if (error != 0) {
		*vpp = NULL;
		return (error);
	}

	/*
	 * Exclusively lock the vnode vnode while it's being constructed.
	 */
	lockmgr(vp->v_vnlock, LK_EXCLUSIVE, NULL);
	error = insmntque(vp, mp);
	if (error != 0) {
		*vpp = NULL;
		return (error);
	}

	setup(vp, arg);

	error = sfs_vnode_insert(vp, flags, parent_id, id, vpp);
	if (error != 0 || *vpp != NULL) {
		KASSERT_IMPLY(error == 0, (*vpp)->v_data != NULL,
		    "sfs vnode with no data");
		return (error);
	}

	*vpp = vp;
	return (0);
}

static void
sfs_print_node(sfs_node_t *node)
{
	printf("\tname = %s\n", node->sn_name);
	printf("\tparent_id = %ju\n", (uintmax_t)node->sn_parent_id);
	printf("\tid = %ju\n", (uintmax_t)node->sn_id);
}

static sfs_node_t *
sfs_alloc_node(size_t size, const char *name, uint64_t parent_id, uint64_t id)
{
	struct sfs_node *node;

	KASSERT(strlen(name) < sizeof(node->sn_name),
	    ("sfs node name is too long"));
	KASSERT(size >= sizeof(*node), ("sfs node size is too small"));
	node = malloc(size, M_SFSNODES, M_WAITOK | M_ZERO);
	strlcpy(node->sn_name, name, sizeof(node->sn_name));
	node->sn_parent_id = parent_id;
	node->sn_id = id;

	return (node);
}

static void
sfs_destroy_node(sfs_node_t *node)
{
	free(node, M_SFSNODES);
}

static void *
sfs_reclaim_vnode(vnode_t *vp)
{
	sfs_node_t *node;
	void *data;

	sfs_vnode_remove(vp);
	data = vp->v_data;
	vp->v_data = NULL;
	return (data);
}

static int
sfs_readdir_common(uint64_t parent_id, uint64_t id, struct vop_readdir_args *ap,
    uio_t *uio, off_t *offp)
{
	struct dirent entry;
	int error;

	/* Reset ncookies for subsequent use of vfs_read_dirent. */
	if (ap->a_ncookies != NULL)
		*ap->a_ncookies = 0;

	if (uio->uio_resid < sizeof(entry))
		return (SET_ERROR(EINVAL));

	if (uio->uio_offset < 0)
		return (SET_ERROR(EINVAL));
	if (uio->uio_offset == 0) {
		entry.d_fileno = id;
		entry.d_type = DT_DIR;
		entry.d_name[0] = '.';
		entry.d_namlen = 1;
		entry.d_reclen = sizeof(entry);
		dirent_terminate(&entry);
		error = vfs_read_dirent(ap, &entry, uio->uio_offset);
		if (error != 0)
			return (SET_ERROR(error));
	}

	if (uio->uio_offset < sizeof(entry))
		return (SET_ERROR(EINVAL));
	if (uio->uio_offset == sizeof(entry)) {
		entry.d_fileno = parent_id;
		entry.d_type = DT_DIR;
		entry.d_name[0] = '.';
		entry.d_name[1] = '.';
		entry.d_namlen = 2;
		entry.d_reclen = sizeof(entry);
		dirent_terminate(&entry);
		error = vfs_read_dirent(ap, &entry, uio->uio_offset);
		if (error != 0)
			return (SET_ERROR(error));
	}

	if (offp != NULL)
		*offp = 2 * sizeof(entry);
	return (0);
}


/*
 * .zfs inode namespace
 *
 * We need to generate unique inode numbers for all files and directories
 * within the .zfs pseudo-filesystem.  We use the following scheme:
 *
 * 	ENTRY			ZFSCTL_INODE
 * 	.zfs			1
 * 	.zfs/snapshot		2
 * 	.zfs/snapshot/<snap>	objectid(snap)
 */
#define	ZFSCTL_INO_SNAP(id)	(id)

static struct vop_vector zfsctl_ops_root;
static struct vop_vector zfsctl_ops_snapdir;
static struct vop_vector zfsctl_ops_snapshot;
static struct vop_vector zfsctl_ops_shares_dir;

void
zfsctl_init(void)
{
}

void
zfsctl_fini(void)
{
}

boolean_t
zfsctl_is_node(vnode_t *vp)
{
	return (vn_matchops(vp, zfsctl_ops_root) ||
	    vn_matchops(vp, zfsctl_ops_snapdir) ||
	    vn_matchops(vp, zfsctl_ops_snapshot) ||
	    vn_matchops(vp, zfsctl_ops_shares_dir));

}

typedef struct zfsctl_root {
	sfs_node_t	node;
	sfs_node_t	*snapdir;
	timestruc_t	cmtime;
} zfsctl_root_t;


/*
 * Create the '.zfs' directory.
 */
void
zfsctl_create(zfsvfs_t *zfsvfs)
{
	zfsctl_root_t *dot_zfs;
	sfs_node_t *snapdir;
	vnode_t *rvp;
	uint64_t crtime[2];

	ASSERT(zfsvfs->z_ctldir == NULL);

	snapdir = sfs_alloc_node(sizeof(*snapdir), "snapshot", ZFSCTL_INO_ROOT,
	    ZFSCTL_INO_SNAPDIR);
	dot_zfs = (zfsctl_root_t *)sfs_alloc_node(sizeof(*dot_zfs), ".zfs", 0,
	    ZFSCTL_INO_ROOT);
	dot_zfs->snapdir = snapdir;

	VERIFY(VFS_ROOT(zfsvfs->z_vfs, LK_EXCLUSIVE, &rvp) == 0);
	VERIFY(0 == sa_lookup(VTOZ(rvp)->z_sa_hdl, SA_ZPL_CRTIME(zfsvfs),
	    &crtime, sizeof(crtime)));
	ZFS_TIME_DECODE(&dot_zfs->cmtime, crtime);
	vput(rvp);

	zfsvfs->z_ctldir = dot_zfs;
}

/*
 * Destroy the '.zfs' directory.  Only called when the filesystem is unmounted.
 * The nodes must not have any associated vnodes by now as they should be
 * vflush-ed.
 */
void
zfsctl_destroy(zfsvfs_t *zfsvfs)
{
	sfs_destroy_node(zfsvfs->z_ctldir->snapdir);
	sfs_destroy_node((sfs_node_t *)zfsvfs->z_ctldir);
	zfsvfs->z_ctldir = NULL;
}

static int
zfsctl_fs_root_vnode(struct mount *mp, void *arg __unused, int flags,
    struct vnode **vpp)
{
	return (VFS_ROOT(mp, flags, vpp));
}

static void
zfsctl_common_vnode_setup(vnode_t *vp, void *arg)
{
	ASSERT_VOP_ELOCKED(vp, __func__);

	/* We support shared locking. */
	VN_LOCK_ASHARE(vp);
	vp->v_type = VDIR;
	vp->v_data = arg;
}

static int
zfsctl_root_vnode(struct mount *mp, void *arg __unused, int flags,
    struct vnode **vpp)
{
	void *node;
	int err;

	node = ((zfsvfs_t*)mp->mnt_data)->z_ctldir;
	err = sfs_vgetx(mp, flags, 0, ZFSCTL_INO_ROOT, "zfs", &zfsctl_ops_root,
	    zfsctl_common_vnode_setup, node, vpp);
	return (err);
}

static int
zfsctl_snapdir_vnode(struct mount *mp, void *arg __unused, int flags,
    struct vnode **vpp)
{
	void *node;
	int err;

	node = ((zfsvfs_t*)mp->mnt_data)->z_ctldir->snapdir;
	err = sfs_vgetx(mp, flags, ZFSCTL_INO_ROOT, ZFSCTL_INO_SNAPDIR, "zfs",
	   &zfsctl_ops_snapdir, zfsctl_common_vnode_setup, node, vpp);
	return (err);
}

/*
 * Given a root znode, retrieve the associated .zfs directory.
 * Add a hold to the vnode and return it.
 */
int
zfsctl_root(zfsvfs_t *zfsvfs, int flags, vnode_t **vpp)
{
	vnode_t *vp;
	int error;

	error = zfsctl_root_vnode(zfsvfs->z_vfs, NULL, flags, vpp);
	return (error);
}

/*
 * Common open routine.  Disallow any write access.
 */
static int
zfsctl_common_open(struct vop_open_args *ap)
{
	int flags = ap->a_mode;

	if (flags & FWRITE)
		return (SET_ERROR(EACCES));

	return (0);
}

/*
 * Common close routine.  Nothing to do here.
 */
/* ARGSUSED */
static int
zfsctl_common_close(struct vop_close_args *ap)
{
	return (0);
}

/*
 * Common access routine.  Disallow writes.
 */
static int
zfsctl_common_access(ap)
	struct vop_access_args /* {
		struct vnode *a_vp;
		accmode_t a_accmode;
		struct ucred *a_cred;
		struct thread *a_td;
	} */ *ap;
{
	accmode_t accmode = ap->a_accmode;

	if (accmode & VWRITE)
		return (SET_ERROR(EACCES));
	return (0);
}

/*
 * Common getattr function.  Fill in basic information.
 */
static void
zfsctl_common_getattr(vnode_t *vp, vattr_t *vap)
{
	timestruc_t	now;
	sfs_node_t *node;

	node = vp->v_data;

	vap->va_uid = 0;
	vap->va_gid = 0;
	vap->va_rdev = 0;
	/*
	 * We are a purely virtual object, so we have no
	 * blocksize or allocated blocks.
	 */
	vap->va_blksize = 0;
	vap->va_nblocks = 0;
	vap->va_seq = 0;
	vn_fsid(vp, vap);
	vap->va_mode = zfsctl_ctldir_mode;
	vap->va_type = VDIR;
	/*
	 * We live in the now (for atime).
	 */
	gethrestime(&now);
	vap->va_atime = now;
	/* FreeBSD: Reset chflags(2) flags. */
	vap->va_flags = 0;

	vap->va_nodeid = node->sn_id;

	/* At least '.' and '..'. */
	vap->va_nlink = 2;
}

static int
zfsctl_common_fid(ap)
	struct vop_fid_args /* {
		struct vnode *a_vp;
		struct fid *a_fid;
	} */ *ap;
{
	vnode_t		*vp = ap->a_vp;
	fid_t		*fidp = (void *)ap->a_fid;
	sfs_node_t	*node = vp->v_data;
	uint64_t	object = node->sn_id;
	zfid_short_t	*zfid;
	int		i;

	zfid = (zfid_short_t *)fidp;
	zfid->zf_len = SHORT_FID_LEN;

	for (i = 0; i < sizeof(zfid->zf_object); i++)
		zfid->zf_object[i] = (uint8_t)(object >> (8 * i));

	/* .zfs nodes always have a generation number of 0 */
	for (i = 0; i < sizeof(zfid->zf_gen); i++)
		zfid->zf_gen[i] = 0;

	return (0);
}

static int
zfsctl_common_reclaim(ap)
	struct vop_reclaim_args /* {
		struct vnode *a_vp;
		struct thread *a_td;
	} */ *ap;
{
	vnode_t *vp = ap->a_vp;

	(void) sfs_reclaim_vnode(vp);
	return (0);
}

static int
zfsctl_common_print(ap)
	struct vop_print_args /* {
		struct vnode *a_vp;
	} */ *ap;
{
	sfs_print_node(ap->a_vp->v_data);
	return (0);
}

/*
 * Get root directory attributes.
 */
static int
zfsctl_root_getattr(ap)
	struct vop_getattr_args /* {
		struct vnode *a_vp;
		struct vattr *a_vap;
		struct ucred *a_cred;
	} */ *ap;
{
	struct vnode *vp = ap->a_vp;
	struct vattr *vap = ap->a_vap;
	zfsctl_root_t *node = vp->v_data;

	zfsctl_common_getattr(vp, vap);
	vap->va_ctime = node->cmtime;
	vap->va_mtime = vap->va_ctime;
	vap->va_birthtime = vap->va_ctime;
	vap->va_nlink += 1; /* snapdir */
	vap->va_size = vap->va_nlink;
	return (0);
}

/*
 * When we lookup "." we still can be asked to lock it
 * differently, can't we?
 */
int
zfsctl_relock_dot(vnode_t *dvp, int ltype)
{
	vref(dvp);
	if (ltype != VOP_ISLOCKED(dvp)) {
		if (ltype == LK_EXCLUSIVE)
			vn_lock(dvp, LK_UPGRADE | LK_RETRY);
		else /* if (ltype == LK_SHARED) */
			vn_lock(dvp, LK_DOWNGRADE | LK_RETRY);

		/* Relock for the "." case may left us with reclaimed vnode. */
		if ((dvp->v_iflag & VI_DOOMED) != 0) {
			vrele(dvp);
			return (SET_ERROR(ENOENT));
		}
	}
	return (0);
}

/*
 * Special case the handling of "..".
 */
int
zfsctl_root_lookup(ap)
	struct vop_lookup_args /* {
		struct vnode *a_dvp;
		struct vnode **a_vpp;
		struct componentname *a_cnp;
	} */ *ap;
{
	struct componentname *cnp = ap->a_cnp;
	vnode_t *dvp = ap->a_dvp;
	vnode_t **vpp = ap->a_vpp;
	cred_t *cr = ap->a_cnp->cn_cred;
	int flags = ap->a_cnp->cn_flags;
	int lkflags = ap->a_cnp->cn_lkflags;
	int nameiop = ap->a_cnp->cn_nameiop;
	int err;
	int ltype;

	ASSERT(dvp->v_type == VDIR);

	if ((flags & ISLASTCN) != 0 && nameiop != LOOKUP)
		return (SET_ERROR(ENOTSUP));

	if (cnp->cn_namelen == 1 && *cnp->cn_nameptr == '.') {
		err = zfsctl_relock_dot(dvp, lkflags & LK_TYPE_MASK);
		if (err == 0)
			*vpp = dvp;
	} else if ((flags & ISDOTDOT) != 0) {
		err = vn_vget_ino_gen(dvp, zfsctl_fs_root_vnode, NULL,
		    lkflags, vpp);
	} else if (strncmp(cnp->cn_nameptr, "snapshot", cnp->cn_namelen) == 0) {
		err = zfsctl_snapdir_vnode(dvp->v_mount, NULL, lkflags, vpp);
	} else {
		err = SET_ERROR(ENOENT);
	}
	if (err != 0)
		*vpp = NULL;
	return (err);
}

static int
zfsctl_root_readdir(ap)
	struct vop_readdir_args /* {
		struct vnode *a_vp;
		struct uio *a_uio;
		struct ucred *a_cred;
		int *a_eofflag;
		int *ncookies;
		u_long **a_cookies;
	} */ *ap;
{
	struct dirent entry;
	vnode_t *vp = ap->a_vp;
	zfsvfs_t *zfsvfs = vp->v_vfsp->vfs_data;
	zfsctl_root_t *node = vp->v_data;
	uio_t *uio = ap->a_uio;
	int *eofp = ap->a_eofflag;
	off_t dots_offset;
	int error;

	ASSERT(vp->v_type == VDIR);

	error = sfs_readdir_common(zfsvfs->z_root, ZFSCTL_INO_ROOT, ap, uio,
	    &dots_offset);
	if (error != 0) {
		if (error == ENAMETOOLONG) /* ran out of destination space */
			error = 0;
		return (error);
	}
	if (uio->uio_offset != dots_offset)
		return (SET_ERROR(EINVAL));

	CTASSERT(sizeof(node->snapdir->sn_name) <= sizeof(entry.d_name));
	entry.d_fileno = node->snapdir->sn_id;
	entry.d_type = DT_DIR;
	strcpy(entry.d_name, node->snapdir->sn_name);
	entry.d_namlen = strlen(entry.d_name);
	entry.d_reclen = sizeof(entry);
	dirent_terminate(&entry);
	error = vfs_read_dirent(ap, &entry, uio->uio_offset);
	if (error != 0) {
		if (error == ENAMETOOLONG)
			error = 0;
		return (SET_ERROR(error));
	}
	if (eofp != NULL)
		*eofp = 1;
	return (0);
}

static int
zfsctl_root_vptocnp(struct vop_vptocnp_args *ap)
{
	static const char dotzfs_name[4] = ".zfs";
	vnode_t *dvp;
	int error;

	if (*ap->a_buflen < sizeof (dotzfs_name))
		return (SET_ERROR(ENOMEM));

	error = vn_vget_ino_gen(ap->a_vp, zfsctl_fs_root_vnode, NULL,
	    LK_SHARED, &dvp);
	if (error != 0)
		return (SET_ERROR(error));

	VOP_UNLOCK(dvp, 0);
	*ap->a_vpp = dvp;
	*ap->a_buflen -= sizeof (dotzfs_name);
	bcopy(dotzfs_name, ap->a_buf + *ap->a_buflen, sizeof (dotzfs_name));
	return (0);
}

static int
zfsctl_common_pathconf(ap)
	struct vop_pathconf_args /* {
		struct vnode *a_vp;
		int a_name;
		int *a_retval;
	} */ *ap;
{
	/*
	 * We care about ACL variables so that user land utilities like ls
	 * can display them correctly.  Since the ctldir's st_dev is set to be
	 * the same as the parent dataset, we must support all variables that
	 * it supports.
	 */
	switch (ap->a_name) {
	case _PC_LINK_MAX:
		*ap->a_retval = MIN(LONG_MAX, ZFS_LINK_MAX);
		return (0);

	case _PC_FILESIZEBITS:
		*ap->a_retval = 64;
		return (0);

	case _PC_MIN_HOLE_SIZE:
		*ap->a_retval = (int)SPA_MINBLOCKSIZE;
		return (0);

	case _PC_ACL_NFS4:
		*ap->a_retval = 1;
		return (0);

	case _PC_ACL_PATH_MAX:
		*ap->a_retval = ACL_MAX_ENTRIES;
		return (0);

	case _PC_NAME_MAX:
		*ap->a_retval = NAME_MAX;
		return (0);

	default:
		return (vop_stdpathconf(ap));
	}
}

/**
 * Returns a trivial ACL
 */
int
zfsctl_common_getacl(ap)
	struct vop_getacl_args /* {
		struct vnode *vp;
		acl_type_t a_type;
		struct acl *a_aclp;
		struct ucred *cred;
		struct thread *td;
	} */ *ap;
{
	int i;

	if (ap->a_type != ACL_TYPE_NFS4)
		return (EINVAL);

	acl_nfs4_sync_acl_from_mode(ap->a_aclp, zfsctl_ctldir_mode, 0);
	/*
	 * acl_nfs4_sync_acl_from_mode assumes that the owner can always modify
	 * attributes.  That is not the case for the ctldir, so we must clear
	 * those bits.  We also must clear ACL_READ_NAMED_ATTRS, because xattrs
	 * aren't supported by the ctldir.
	 */
	for (i = 0; i < ap->a_aclp->acl_cnt; i++) {
		struct acl_entry *entry;
		entry = &(ap->a_aclp->acl_entry[i]);
		uint32_t old_perm = entry->ae_perm;
		entry->ae_perm &= ~(ACL_WRITE_ACL | ACL_WRITE_OWNER |
		    ACL_WRITE_ATTRIBUTES | ACL_WRITE_NAMED_ATTRS |
		    ACL_READ_NAMED_ATTRS );
	}

	return (0);
}

static struct vop_vector zfsctl_ops_root = {
	.vop_default =	&default_vnodeops,
	.vop_open =	zfsctl_common_open,
	.vop_close =	zfsctl_common_close,
	.vop_ioctl =	VOP_EINVAL,
	.vop_getattr =	zfsctl_root_getattr,
	.vop_access =	zfsctl_common_access,
	.vop_readdir =	zfsctl_root_readdir,
	.vop_lookup =	zfsctl_root_lookup,
	.vop_inactive =	VOP_NULL,
	.vop_reclaim =	zfsctl_common_reclaim,
	.vop_fid =	zfsctl_common_fid,
	.vop_print =	zfsctl_common_print,
	.vop_vptocnp =	zfsctl_root_vptocnp,
	.vop_pathconf =	zfsctl_common_pathconf,
	.vop_getacl =	zfsctl_common_getacl,
};

static int
zfsctl_snapshot_zname(vnode_t *vp, const char *name, int len, char *zname)
{
	objset_t *os = ((zfsvfs_t *)((vp)->v_vfsp->vfs_data))->z_os;

	dmu_objset_name(os, zname);
	if (strlen(zname) + 1 + strlen(name) >= len)
		return (SET_ERROR(ENAMETOOLONG));
	(void) strcat(zname, "@");
	(void) strcat(zname, name);
	return (0);
}

static int
zfsctl_snapshot_lookup(vnode_t *vp, const char *name, uint64_t *id)
{
	objset_t *os = ((zfsvfs_t *)((vp)->v_vfsp->vfs_data))->z_os;
	int err;

	err = dsl_dataset_snap_lookup(dmu_objset_ds(os), name, id);
	return (err);
}

/*
 * Given a vnode get a root vnode of a filesystem mounted on top of
 * the vnode, if any.  The root vnode is referenced and locked.
 * If no filesystem is mounted then the orinal vnode remains referenced
 * and locked.  If any error happens the orinal vnode is unlocked and
 * released.
 */
static int
zfsctl_mounted_here(vnode_t **vpp, int flags)
{
	struct mount *mp;
	int err;

	ASSERT_VOP_LOCKED(*vpp, __func__);
	ASSERT3S((*vpp)->v_type, ==, VDIR);

	if ((mp = (*vpp)->v_mountedhere) != NULL) {
		err = vfs_busy(mp, 0);
		KASSERT(err == 0, ("vfs_busy(mp, 0) failed with %d", err));
		KASSERT(vrefcnt(*vpp) > 1, ("unreferenced mountpoint"));
		vput(*vpp);
		err = VFS_ROOT(mp, flags, vpp);
		vfs_unbusy(mp);
		return (err);
	}
	return (EJUSTRETURN);
}

typedef struct {
	const char *snap_name;
	uint64_t    snap_id;
} snapshot_setup_arg_t;

static void
zfsctl_snapshot_vnode_setup(vnode_t *vp, void *arg)
{
	snapshot_setup_arg_t *ssa = arg;
	sfs_node_t *node;

	ASSERT_VOP_ELOCKED(vp, __func__);

	node = sfs_alloc_node(sizeof(sfs_node_t),
	    ssa->snap_name, ZFSCTL_INO_SNAPDIR, ssa->snap_id);
	zfsctl_common_vnode_setup(vp, node);

	/* We have to support recursive locking. */
	VN_LOCK_AREC(vp);
}

/*
 * Lookup entry point for the 'snapshot' directory.  Try to open the
 * snapshot if it exist, creating the pseudo filesystem vnode as necessary.
 * Perform a mount of the associated dataset on top of the vnode.
 * There are four possibilities:
 * - the snapshot node and vnode do not exist
 * - the snapshot vnode is covered by the mounted snapshot
 * - the snapshot vnode is not covered yet, the mount operation is in progress
 * - the snapshot vnode is not covered, because the snapshot has been unmounted
 * The last two states are transient and should be relatively short-lived.
 */
int
zfsctl_snapdir_lookup(ap)
	struct vop_lookup_args /* {
		struct vnode *a_dvp;
		struct vnode **a_vpp;
		struct componentname *a_cnp;
	} */ *ap;
{
	vnode_t *dvp = ap->a_dvp;
	vnode_t **vpp = ap->a_vpp;
	struct componentname *cnp = ap->a_cnp;
	char name[NAME_MAX + 1];
	char fullname[ZFS_MAX_DATASET_NAME_LEN];
	char *mountpoint;
	size_t mountpoint_len;
	zfsvfs_t *zfsvfs = dvp->v_vfsp->vfs_data;
	uint64_t snap_id;
	int nameiop = cnp->cn_nameiop;
	int lkflags = cnp->cn_lkflags;
	int flags = cnp->cn_flags;
	int err;

	ASSERT(dvp->v_type == VDIR);

	if ((flags & ISLASTCN) != 0 && nameiop != LOOKUP)
		return (SET_ERROR(ENOTSUP));

	if (cnp->cn_namelen == 1 && *cnp->cn_nameptr == '.') {
		err = zfsctl_relock_dot(dvp, lkflags & LK_TYPE_MASK);
		if (err == 0)
			*vpp = dvp;
		return (err);
	}
	if (flags & ISDOTDOT) {
		err = vn_vget_ino_gen(dvp, zfsctl_root_vnode, NULL, lkflags,
		    vpp);
		return (err);
	}

	if (cnp->cn_namelen >= sizeof(name))
		return (SET_ERROR(ENAMETOOLONG));

	strlcpy(name, ap->a_cnp->cn_nameptr, ap->a_cnp->cn_namelen + 1);
	err = zfsctl_snapshot_lookup(dvp, name, &snap_id);
	if (err != 0)
		return (SET_ERROR(ENOENT));

	for (;;) {
		snapshot_setup_arg_t ssa;

		ssa.snap_name = name;
		ssa.snap_id = snap_id;
		err = sfs_vgetx(dvp->v_mount, LK_SHARED, ZFSCTL_INO_SNAPDIR,
		   snap_id, "zfs", &zfsctl_ops_snapshot,
		   zfsctl_snapshot_vnode_setup, &ssa, vpp);
		if (err != 0)
			return (err);

		/* Check if a new vnode has just been created. */
		if (VOP_ISLOCKED(*vpp) == LK_EXCLUSIVE)
			break;

		/*
		 * Check if a snapshot is already mounted on top of the vnode.
		 */
		err = zfsctl_mounted_here(vpp, lkflags);
		if (err != EJUSTRETURN)
			return (err);

		/*
		 * If the vnode is not covered, then either the mount operation
		 * is in progress or the snapshot has already been unmounted
		 * but the vnode hasn't been inactivated and reclaimed yet.
		 * We can try to re-use the vnode in the latter case.
		 */
		VI_LOCK(*vpp);
		if (((*vpp)->v_iflag & VI_MOUNT) == 0) {
			/* Upgrade to exclusive lock in order to:
			 * - avoid race conditions
			 * - satisfy the contract of mount_snapshot()
			 */
			err = VOP_LOCK(*vpp, LK_TRYUPGRADE | LK_INTERLOCK);
			if (err == 0)
				break;
		} else {
			VI_UNLOCK(*vpp);
		}

		/*
		 * In this state we can loop on uncontested locks and starve
		 * the thread doing the lengthy, non-trivial mount operation.
		 * So, yield to prevent that from happening.
		 */
		vput(*vpp);
		kern_yield(PRI_USER);
	}

	VERIFY0(zfsctl_snapshot_zname(dvp, name, sizeof(fullname), fullname));

	mountpoint_len = strlen(dvp->v_vfsp->mnt_stat.f_mntonname) +
	    strlen("/" ZFS_CTLDIR_NAME "/snapshot/") + strlen(name) + 1;
	mountpoint = kmem_alloc(mountpoint_len, KM_SLEEP);
	(void) snprintf(mountpoint, mountpoint_len,
	    "%s/" ZFS_CTLDIR_NAME "/snapshot/%s",
	    dvp->v_vfsp->mnt_stat.f_mntonname, name);

	err = mount_snapshot(curthread, vpp, "zfs", mountpoint, fullname, 0);
	kmem_free(mountpoint, mountpoint_len);
	if (err == 0) {
		/*
		 * Fix up the root vnode mounted on .zfs/snapshot/<snapname>.
		 *
		 * This is where we lie about our v_vfsp in order to
		 * make .zfs/snapshot/<snapname> accessible over NFS
		 * without requiring manual mounts of <snapname>.
		 */
		ASSERT(VTOZ(*vpp)->z_zfsvfs != zfsvfs);
		VTOZ(*vpp)->z_zfsvfs->z_parent = zfsvfs;

		/* Clear the root flag (set via VFS_ROOT) as well. */
		(*vpp)->v_vflag &= ~VV_ROOT;
	}

	if (err != 0)
		*vpp = NULL;
	return (err);
}

static int
zfsctl_snapdir_readdir(ap)
	struct vop_readdir_args /* {
		struct vnode *a_vp;
		struct uio *a_uio;
		struct ucred *a_cred;
		int *a_eofflag;
		int *ncookies;
		u_long **a_cookies;
	} */ *ap;
{
	char snapname[ZFS_MAX_DATASET_NAME_LEN];
	struct dirent entry;
	vnode_t *vp = ap->a_vp;
	zfsvfs_t *zfsvfs = vp->v_vfsp->vfs_data;
	uio_t *uio = ap->a_uio;
	int *eofp = ap->a_eofflag;
	off_t dots_offset;
	int error;

	ASSERT(vp->v_type == VDIR);

	error = sfs_readdir_common(ZFSCTL_INO_ROOT, ZFSCTL_INO_SNAPDIR, ap, uio,
	    &dots_offset);
	if (error != 0) {
		if (error == ENAMETOOLONG) /* ran out of destination space */
			error = 0;
		return (error);
	}

	ZFS_ENTER(zfsvfs);
	for (;;) {
		uint64_t cookie;
		uint64_t id;

		cookie = uio->uio_offset - dots_offset;

		dsl_pool_config_enter(dmu_objset_pool(zfsvfs->z_os), FTAG);
		error = dmu_snapshot_list_next(zfsvfs->z_os, sizeof(snapname),
		    snapname, &id, &cookie, NULL);
		dsl_pool_config_exit(dmu_objset_pool(zfsvfs->z_os), FTAG);
		if (error != 0) {
			if (error == ENOENT) {
				if (eofp != NULL)
					*eofp = 1;
				error = 0;
			}
			ZFS_EXIT(zfsvfs);
			return (error);
		}

		entry.d_fileno = id;
		entry.d_type = DT_DIR;
		strcpy(entry.d_name, snapname);
		entry.d_namlen = strlen(entry.d_name);
		entry.d_reclen = sizeof(entry);
		/* NOTE: d_off is the offset for the *next* entry. */
		entry.d_off = cookie + dots_offset;
		dirent_terminate(&entry);
		error = vfs_read_dirent(ap, &entry, uio->uio_offset);
		if (error != 0) {
			if (error == ENAMETOOLONG)
				error = 0;
			ZFS_EXIT(zfsvfs);
			return (SET_ERROR(error));
		}
		uio->uio_offset = cookie + dots_offset;
	}
	/* NOTREACHED */
}

static int
zfsctl_snapdir_getattr(ap)
	struct vop_getattr_args /* {
		struct vnode *a_vp;
		struct vattr *a_vap;
		struct ucred *a_cred;
	} */ *ap;
{
	vnode_t *vp = ap->a_vp;
	vattr_t *vap = ap->a_vap;
	zfsvfs_t *zfsvfs = vp->v_vfsp->vfs_data;
	dsl_dataset_t *ds = dmu_objset_ds(zfsvfs->z_os);
	sfs_node_t *node = vp->v_data;
	uint64_t snap_count;
	int err;

	ZFS_ENTER(zfsvfs);
	zfsctl_common_getattr(vp, vap);
	vap->va_ctime = dmu_objset_snap_cmtime(zfsvfs->z_os);
	vap->va_mtime = vap->va_ctime;
	vap->va_birthtime = vap->va_ctime;
	if (dsl_dataset_phys(ds)->ds_snapnames_zapobj != 0) {
		err = zap_count(dmu_objset_pool(ds->ds_objset)->dp_meta_objset,
		    dsl_dataset_phys(ds)->ds_snapnames_zapobj, &snap_count);
		if (err != 0) {
			ZFS_EXIT(zfsvfs);
			return (err);
		}
		vap->va_nlink += snap_count;
	}
	vap->va_size = vap->va_nlink;

	ZFS_EXIT(zfsvfs);
	return (0);
}

static struct vop_vector zfsctl_ops_snapdir = {
	.vop_default =	&default_vnodeops,
	.vop_open =	zfsctl_common_open,
	.vop_close =	zfsctl_common_close,
	.vop_getattr =	zfsctl_snapdir_getattr,
	.vop_access =	zfsctl_common_access,
	.vop_readdir =	zfsctl_snapdir_readdir,
	.vop_lookup =	zfsctl_snapdir_lookup,
	.vop_reclaim =	zfsctl_common_reclaim,
	.vop_fid =	zfsctl_common_fid,
	.vop_print =	zfsctl_common_print,
	.vop_pathconf =	zfsctl_common_pathconf,
	.vop_getacl =	zfsctl_common_getacl,
};

static int
zfsctl_snapshot_inactive(ap)
	struct vop_inactive_args /* {
		struct vnode *a_vp;
		struct thread *a_td;
	} */ *ap;
{
	vnode_t *vp = ap->a_vp;

	VERIFY(vrecycle(vp) == 1);
	return (0);
}

static int
zfsctl_snapshot_reclaim(ap)
	struct vop_reclaim_args /* {
		struct vnode *a_vp;
		struct thread *a_td;
	} */ *ap;
{
	vnode_t *vp = ap->a_vp;
	void *data = vp->v_data;

	sfs_reclaim_vnode(vp);
	sfs_destroy_node(data);
	return (0);
}

static int
zfsctl_snapshot_vptocnp(struct vop_vptocnp_args *ap)
{
	struct mount *mp;
	vnode_t *dvp;
	vnode_t *vp;
	sfs_node_t *node;
	size_t len;
	int locked;
	int error;

	vp = ap->a_vp;
	node = vp->v_data;
	len = strlen(node->sn_name);
	if (*ap->a_buflen < len)
		return (SET_ERROR(ENOMEM));

	/*
	 * Prevent unmounting of the snapshot while the vnode lock
	 * is not held.  That is not strictly required, but allows
	 * us to assert that an uncovered snapshot vnode is never
	 * "leaked".
	 */
	mp = vp->v_mountedhere;
	if (mp == NULL)
		return (SET_ERROR(ENOENT));
	error = vfs_busy(mp, 0);
	KASSERT(error == 0, ("vfs_busy(mp, 0) failed with %d", error));

	/*
	 * We can vput the vnode as we can now depend on the reference owned
	 * by the busied mp.  But we also need to hold the vnode, because
	 * the reference may go after vfs_unbusy() which has to be called
	 * before we can lock the vnode again.
	 */
	locked = VOP_ISLOCKED(vp);
	vhold(vp);
	vput(vp);

	/* Look up .zfs/snapshot, our parent. */
	error = zfsctl_snapdir_vnode(vp->v_mount, NULL, LK_SHARED, &dvp);
	if (error == 0) {
		VOP_UNLOCK(dvp, 0);
		*ap->a_vpp = dvp;
		*ap->a_buflen -= len;
		bcopy(node->sn_name, ap->a_buf + *ap->a_buflen, len);
	}
	vfs_unbusy(mp);
	vget(vp, locked | LK_VNHELD | LK_RETRY, curthread);
	return (error);
}

/*
 * These VP's should never see the light of day.  They should always
 * be covered.
 */
static struct vop_vector zfsctl_ops_snapshot = {
	.vop_default =		NULL, /* ensure very restricted access */
	.vop_inactive =		zfsctl_snapshot_inactive,
	.vop_reclaim =		zfsctl_snapshot_reclaim,
	.vop_vptocnp =		zfsctl_snapshot_vptocnp,
	.vop_lock1 =		vop_stdlock,
	.vop_unlock =		vop_stdunlock,
	.vop_islocked =		vop_stdislocked,
	.vop_advlockpurge =	vop_stdadvlockpurge, /* called by vgone */
	.vop_print =		zfsctl_common_print,
};

int
zfsctl_lookup_objset(vfs_t *vfsp, uint64_t objsetid, zfsvfs_t **zfsvfsp)
{
	struct mount *mp;
	zfsvfs_t *zfsvfs = vfsp->vfs_data;
	vnode_t *vp;
	int error;

	ASSERT(zfsvfs->z_ctldir != NULL);
	*zfsvfsp = NULL;
	error = sfs_vnode_get(vfsp, LK_EXCLUSIVE,
	    ZFSCTL_INO_SNAPDIR, objsetid, &vp);
	if (error == 0 && vp != NULL) {
		/*
		 * XXX Probably need to at least reference, if not busy, the mp.
		 */
		if (vp->v_mountedhere != NULL)
			*zfsvfsp = vp->v_mountedhere->mnt_data;
		vput(vp);
	}
	if (*zfsvfsp == NULL)
		return (SET_ERROR(EINVAL));
	return (0);
}

/*
 * Unmount any snapshots for the given filesystem.  This is called from
 * zfs_umount() - if we have a ctldir, then go through and unmount all the
 * snapshots.
 */
int
zfsctl_umount_snapshots(vfs_t *vfsp, int fflags, cred_t *cr)
{
	char snapname[ZFS_MAX_DATASET_NAME_LEN];
	zfsvfs_t *zfsvfs = vfsp->vfs_data;
	struct mount *mp;
	vnode_t *dvp;
	vnode_t *vp;
	sfs_node_t *node;
	sfs_node_t *snap;
	uint64_t cookie;
	int error;

	ASSERT(zfsvfs->z_ctldir != NULL);

	cookie = 0;
	for (;;) {
		uint64_t id;

		dsl_pool_config_enter(dmu_objset_pool(zfsvfs->z_os), FTAG);
		error = dmu_snapshot_list_next(zfsvfs->z_os, sizeof(snapname),
		    snapname, &id, &cookie, NULL);
		dsl_pool_config_exit(dmu_objset_pool(zfsvfs->z_os), FTAG);
		if (error != 0) {
			if (error == ENOENT)
				error = 0;
			break;
		}

		for (;;) {
			error = sfs_vnode_get(vfsp, LK_EXCLUSIVE,
			    ZFSCTL_INO_SNAPDIR, id, &vp);
			if (error != 0 || vp == NULL)
				break;

			mp = vp->v_mountedhere;

			/*
			 * v_mountedhere being NULL means that the
			 * (uncovered) vnode is in a transient state
			 * (mounting or unmounting), so loop until it
			 * settles down.
			 */
			if (mp != NULL)
				break;
			vput(vp);
		}
		if (error != 0)
			break;
		if (vp == NULL)
			continue;	/* no mountpoint, nothing to do */

		/*
		 * The mount-point vnode is kept locked to avoid spurious EBUSY
		 * from a concurrent umount.
		 * The vnode lock must have recursive locking enabled.
		 */
		vfs_ref(mp);
		error = dounmount(mp, fflags, curthread);
		KASSERT_IMPLY(error == 0, vrefcnt(vp) == 1,
		    ("extra references after unmount"));
		vput(vp);
		if (error != 0)
			break;
	}
	KASSERT_IMPLY((fflags & MS_FORCE) != 0, error == 0,
	    ("force unmounting failed"));
	return (error);
}

