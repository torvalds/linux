/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2007-2009 Google Inc. and Amit Singh
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * * Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above
 *   copyright notice, this list of conditions and the following disclaimer
 *   in the documentation and/or other materials provided with the
 *   distribution.
 * * Neither the name of Google Inc. nor the names of its
 *   contributors may be used to endorse or promote products derived from
 *   this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Copyright (C) 2005 Csaba Henk.
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
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
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

#include <sys/types.h>
#include <sys/module.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/conf.h>
#include <sys/uio.h>
#include <sys/malloc.h>
#include <sys/queue.h>
#include <sys/lock.h>
#include <sys/rwlock.h>
#include <sys/sx.h>
#include <sys/proc.h>
#include <sys/mount.h>
#include <sys/vnode.h>
#include <sys/namei.h>
#include <sys/extattr.h>
#include <sys/stat.h>
#include <sys/unistd.h>
#include <sys/filedesc.h>
#include <sys/file.h>
#include <sys/fcntl.h>
#include <sys/dirent.h>
#include <sys/bio.h>
#include <sys/buf.h>
#include <sys/sysctl.h>
#include <sys/vmmeter.h>

#include <vm/vm.h>
#include <vm/vm_extern.h>
#include <vm/pmap.h>
#include <vm/vm_map.h>
#include <vm/vm_page.h>
#include <vm/vm_param.h>
#include <vm/vm_object.h>
#include <vm/vm_pager.h>
#include <vm/vnode_pager.h>
#include <vm/vm_object.h>

#include "fuse.h"
#include "fuse_file.h"
#include "fuse_internal.h"
#include "fuse_ipc.h"
#include "fuse_node.h"
#include "fuse_param.h"
#include "fuse_io.h"

#include <sys/priv.h>

#define FUSE_DEBUG_MODULE VNOPS
#include "fuse_debug.h"

/* vnode ops */
static vop_access_t fuse_vnop_access;
static vop_close_t fuse_vnop_close;
static vop_create_t fuse_vnop_create;
static vop_deleteextattr_t fuse_vnop_deleteextattr;
static vop_fsync_t fuse_vnop_fsync;
static vop_getattr_t fuse_vnop_getattr;
static vop_getextattr_t fuse_vnop_getextattr;
static vop_inactive_t fuse_vnop_inactive;
static vop_link_t fuse_vnop_link;
static vop_listextattr_t fuse_vnop_listextattr;
static vop_lookup_t fuse_vnop_lookup;
static vop_mkdir_t fuse_vnop_mkdir;
static vop_mknod_t fuse_vnop_mknod;
static vop_open_t fuse_vnop_open;
static vop_pathconf_t fuse_vnop_pathconf;
static vop_read_t fuse_vnop_read;
static vop_readdir_t fuse_vnop_readdir;
static vop_readlink_t fuse_vnop_readlink;
static vop_reclaim_t fuse_vnop_reclaim;
static vop_remove_t fuse_vnop_remove;
static vop_rename_t fuse_vnop_rename;
static vop_rmdir_t fuse_vnop_rmdir;
static vop_setattr_t fuse_vnop_setattr;
static vop_setextattr_t fuse_vnop_setextattr;
static vop_strategy_t fuse_vnop_strategy;
static vop_symlink_t fuse_vnop_symlink;
static vop_write_t fuse_vnop_write;
static vop_getpages_t fuse_vnop_getpages;
static vop_putpages_t fuse_vnop_putpages;
static vop_print_t fuse_vnop_print;

struct vop_vector fuse_vnops = {
	.vop_default = &default_vnodeops,
	.vop_access = fuse_vnop_access,
	.vop_close = fuse_vnop_close,
	.vop_create = fuse_vnop_create,
	.vop_deleteextattr = fuse_vnop_deleteextattr,
	.vop_fsync = fuse_vnop_fsync,
	.vop_getattr = fuse_vnop_getattr,
	.vop_getextattr = fuse_vnop_getextattr,
	.vop_inactive = fuse_vnop_inactive,
	.vop_link = fuse_vnop_link,
	.vop_listextattr = fuse_vnop_listextattr,
	.vop_lookup = fuse_vnop_lookup,
	.vop_mkdir = fuse_vnop_mkdir,
	.vop_mknod = fuse_vnop_mknod,
	.vop_open = fuse_vnop_open,
	.vop_pathconf = fuse_vnop_pathconf,
	.vop_read = fuse_vnop_read,
	.vop_readdir = fuse_vnop_readdir,
	.vop_readlink = fuse_vnop_readlink,
	.vop_reclaim = fuse_vnop_reclaim,
	.vop_remove = fuse_vnop_remove,
	.vop_rename = fuse_vnop_rename,
	.vop_rmdir = fuse_vnop_rmdir,
	.vop_setattr = fuse_vnop_setattr,
	.vop_setextattr = fuse_vnop_setextattr,
	.vop_strategy = fuse_vnop_strategy,
	.vop_symlink = fuse_vnop_symlink,
	.vop_write = fuse_vnop_write,
	.vop_getpages = fuse_vnop_getpages,
	.vop_putpages = fuse_vnop_putpages,
	.vop_print = fuse_vnop_print,
};

static u_long fuse_lookup_cache_hits = 0;

SYSCTL_ULONG(_vfs_fusefs, OID_AUTO, lookup_cache_hits, CTLFLAG_RD,
    &fuse_lookup_cache_hits, 0, "number of positive cache hits in lookup");

static u_long fuse_lookup_cache_misses = 0;

SYSCTL_ULONG(_vfs_fusefs, OID_AUTO, lookup_cache_misses, CTLFLAG_RD,
    &fuse_lookup_cache_misses, 0, "number of cache misses in lookup");

int	fuse_lookup_cache_enable = 1;

SYSCTL_INT(_vfs_fusefs, OID_AUTO, lookup_cache_enable, CTLFLAG_RW,
    &fuse_lookup_cache_enable, 0, "if non-zero, enable lookup cache");

/*
 * XXX: This feature is highly experimental and can bring to instabilities,
 * needs revisiting before to be enabled by default.
 */
static int fuse_reclaim_revoked = 0;

SYSCTL_INT(_vfs_fusefs, OID_AUTO, reclaim_revoked, CTLFLAG_RW,
    &fuse_reclaim_revoked, 0, "");

uma_zone_t fuse_pbuf_zone;

#define fuse_vm_page_lock(m)		vm_page_lock((m));
#define fuse_vm_page_unlock(m)		vm_page_unlock((m));
#define fuse_vm_page_lock_queues()	((void)0)
#define fuse_vm_page_unlock_queues()	((void)0)

/*
    struct vnop_access_args {
	struct vnode *a_vp;
#if VOP_ACCESS_TAKES_ACCMODE_T
	accmode_t a_accmode;
#else
	int a_mode;
#endif
	struct ucred *a_cred;
	struct thread *a_td;
    };
*/
static int
fuse_vnop_access(struct vop_access_args *ap)
{
	struct vnode *vp = ap->a_vp;
	int accmode = ap->a_accmode;
	struct ucred *cred = ap->a_cred;

	struct fuse_access_param facp;
	struct fuse_data *data = fuse_get_mpdata(vnode_mount(vp));

	int err;

	FS_DEBUG2G("inode=%ju\n", (uintmax_t)VTOI(vp));

	if (fuse_isdeadfs(vp)) {
		if (vnode_isvroot(vp)) {
			return 0;
		}
		return ENXIO;
	}
	if (!(data->dataflags & FSESS_INITED)) {
		if (vnode_isvroot(vp)) {
			if (priv_check_cred(cred, PRIV_VFS_ADMIN) ||
			    (fuse_match_cred(data->daemoncred, cred) == 0)) {
				return 0;
			}
		}
		return EBADF;
	}
	if (vnode_islnk(vp)) {
		return 0;
	}
	bzero(&facp, sizeof(facp));

	err = fuse_internal_access(vp, accmode, &facp, ap->a_td, ap->a_cred);
	FS_DEBUG2G("err=%d accmode=0x%x\n", err, accmode);
	return err;
}

/*
    struct vnop_close_args {
	struct vnode *a_vp;
	int  a_fflag;
	struct ucred *a_cred;
	struct thread *a_td;
    };
*/
static int
fuse_vnop_close(struct vop_close_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct ucred *cred = ap->a_cred;
	int fflag = ap->a_fflag;
	fufh_type_t fufh_type;

	fuse_trace_printf_vnop();

	if (fuse_isdeadfs(vp)) {
		return 0;
	}
	if (vnode_isdir(vp)) {
		if (fuse_filehandle_valid(vp, FUFH_RDONLY)) {
			fuse_filehandle_close(vp, FUFH_RDONLY, NULL, cred);
		}
		return 0;
	}
	if (fflag & IO_NDELAY) {
		return 0;
	}
	fufh_type = fuse_filehandle_xlate_from_fflags(fflag);

	if (!fuse_filehandle_valid(vp, fufh_type)) {
		int i;

		for (i = 0; i < FUFH_MAXTYPE; i++)
			if (fuse_filehandle_valid(vp, i))
				break;
		if (i == FUFH_MAXTYPE)
			panic("FUSE: fufh type %d found to be invalid in close"
			      " (fflag=0x%x)\n",
			      fufh_type, fflag);
	}
	if ((VTOFUD(vp)->flag & FN_SIZECHANGE) != 0) {
		fuse_vnode_savesize(vp, cred);
	}
	return 0;
}

/*
    struct vnop_create_args {
	struct vnode *a_dvp;
	struct vnode **a_vpp;
	struct componentname *a_cnp;
	struct vattr *a_vap;
    };
*/
static int
fuse_vnop_create(struct vop_create_args *ap)
{
	struct vnode *dvp = ap->a_dvp;
	struct vnode **vpp = ap->a_vpp;
	struct componentname *cnp = ap->a_cnp;
	struct vattr *vap = ap->a_vap;
	struct thread *td = cnp->cn_thread;
	struct ucred *cred = cnp->cn_cred;

	struct fuse_open_in *foi;
	struct fuse_entry_out *feo;
	struct fuse_dispatcher fdi;
	struct fuse_dispatcher *fdip = &fdi;

	int err;

	struct mount *mp = vnode_mount(dvp);
	uint64_t parentnid = VTOFUD(dvp)->nid;
	mode_t mode = MAKEIMODE(vap->va_type, vap->va_mode);
	uint64_t x_fh_id;
	uint32_t x_open_flags;

	fuse_trace_printf_vnop();

	if (fuse_isdeadfs(dvp)) {
		return ENXIO;
	}
	bzero(&fdi, sizeof(fdi));

	/* XXX:	Will we ever want devices ? */
	if ((vap->va_type != VREG)) {
		printf("fuse_vnop_create: unsupported va_type %d\n",
		    vap->va_type);
		return (EINVAL);
	}
	debug_printf("parent nid = %ju, mode = %x\n", (uintmax_t)parentnid,
	    mode);

	fdisp_init(fdip, sizeof(*foi) + cnp->cn_namelen + 1);
	if (!fsess_isimpl(mp, FUSE_CREATE)) {
		debug_printf("eh, daemon doesn't implement create?\n");
		return (EINVAL);
	}
	fdisp_make(fdip, FUSE_CREATE, vnode_mount(dvp), parentnid, td, cred);

	foi = fdip->indata;
	foi->mode = mode;
	foi->flags = O_CREAT | O_RDWR;

	memcpy((char *)fdip->indata + sizeof(*foi), cnp->cn_nameptr,
	    cnp->cn_namelen);
	((char *)fdip->indata)[sizeof(*foi) + cnp->cn_namelen] = '\0';

	err = fdisp_wait_answ(fdip);

	if (err) {
		if (err == ENOSYS)
			fsess_set_notimpl(mp, FUSE_CREATE);
		debug_printf("create: got err=%d from daemon\n", err);
		goto out;
	}

	feo = fdip->answ;

	if ((err = fuse_internal_checkentry(feo, VREG))) {
		goto out;
	}
	err = fuse_vnode_get(mp, feo, feo->nodeid, dvp, vpp, cnp, VREG);
	if (err) {
		struct fuse_release_in *fri;
		uint64_t nodeid = feo->nodeid;
		uint64_t fh_id = ((struct fuse_open_out *)(feo + 1))->fh;

		fdisp_init(fdip, sizeof(*fri));
		fdisp_make(fdip, FUSE_RELEASE, mp, nodeid, td, cred);
		fri = fdip->indata;
		fri->fh = fh_id;
		fri->flags = OFLAGS(mode);
		fuse_insert_callback(fdip->tick, fuse_internal_forget_callback);
		fuse_insert_message(fdip->tick);
		return err;
	}
	ASSERT_VOP_ELOCKED(*vpp, "fuse_vnop_create");

	fdip->answ = feo + 1;

	x_fh_id = ((struct fuse_open_out *)(feo + 1))->fh;
	x_open_flags = ((struct fuse_open_out *)(feo + 1))->open_flags;
	fuse_filehandle_init(*vpp, FUFH_RDWR, NULL, x_fh_id);
	fuse_vnode_open(*vpp, x_open_flags, td);
	cache_purge_negative(dvp);

out:
	fdisp_destroy(fdip);
	return err;
}

/*
 * Our vnop_fsync roughly corresponds to the FUSE_FSYNC method. The Linux
 * version of FUSE also has a FUSE_FLUSH method.
 *
 * On Linux, fsync() synchronizes a file's complete in-core state with that
 * on disk. The call is not supposed to return until the system has completed
 * that action or until an error is detected.
 *
 * Linux also has an fdatasync() call that is similar to fsync() but is not
 * required to update the metadata such as access time and modification time.
 */

/*
    struct vnop_fsync_args {
	struct vnodeop_desc *a_desc;
	struct vnode * a_vp;
	struct ucred * a_cred;
	int  a_waitfor;
	struct thread * a_td;
    };
*/
static int
fuse_vnop_fsync(struct vop_fsync_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct thread *td = ap->a_td;

	struct fuse_filehandle *fufh;
	struct fuse_vnode_data *fvdat = VTOFUD(vp);

	int type, err = 0;

	fuse_trace_printf_vnop();

	if (fuse_isdeadfs(vp)) {
		return 0;
	}
	if ((err = vop_stdfsync(ap)))
		return err;

	if (!fsess_isimpl(vnode_mount(vp),
	    (vnode_vtype(vp) == VDIR ? FUSE_FSYNCDIR : FUSE_FSYNC))) {
		goto out;
	}
	for (type = 0; type < FUFH_MAXTYPE; type++) {
		fufh = &(fvdat->fufh[type]);
		if (FUFH_IS_VALID(fufh)) {
			fuse_internal_fsync(vp, td, NULL, fufh);
		}
	}

out:
	return 0;
}

/*
    struct vnop_getattr_args {
	struct vnode *a_vp;
	struct vattr *a_vap;
	struct ucred *a_cred;
	struct thread *a_td;
    };
*/
static int
fuse_vnop_getattr(struct vop_getattr_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct vattr *vap = ap->a_vap;
	struct ucred *cred = ap->a_cred;
	struct thread *td = curthread;
	struct fuse_vnode_data *fvdat = VTOFUD(vp);

	int err = 0;
	int dataflags;
	struct fuse_dispatcher fdi;

	FS_DEBUG2G("inode=%ju\n", (uintmax_t)VTOI(vp));

	dataflags = fuse_get_mpdata(vnode_mount(vp))->dataflags;

	/* Note that we are not bailing out on a dead file system just yet. */

	if (!(dataflags & FSESS_INITED)) {
		if (!vnode_isvroot(vp)) {
			fdata_set_dead(fuse_get_mpdata(vnode_mount(vp)));
			err = ENOTCONN;
			debug_printf("fuse_getattr b: returning ENOTCONN\n");
			return err;
		} else {
			goto fake;
		}
	}
	fdisp_init(&fdi, 0);
	if ((err = fdisp_simple_putget_vp(&fdi, FUSE_GETATTR, vp, td, cred))) {
		if ((err == ENOTCONN) && vnode_isvroot(vp)) {
			/* see comment at similar place in fuse_statfs() */
			fdisp_destroy(&fdi);
			goto fake;
		}
		if (err == ENOENT) {
			fuse_internal_vnode_disappear(vp);
		}
		goto out;
	}

	cache_attrs(vp, (struct fuse_attr_out *)fdi.answ, vap);
	if (vap->va_type != vnode_vtype(vp)) {
		fuse_internal_vnode_disappear(vp);
		err = ENOENT;
		goto out;
	}
	if ((fvdat->flag & FN_SIZECHANGE) != 0)
		vap->va_size = fvdat->filesize;

	if (vnode_isreg(vp) && (fvdat->flag & FN_SIZECHANGE) == 0) {
		/*
	         * This is for those cases when the file size changed without us
	         * knowing, and we want to catch up.
	         */
		off_t new_filesize = ((struct fuse_attr_out *)
				      fdi.answ)->attr.size;

		if (fvdat->filesize != new_filesize) {
			fuse_vnode_setsize(vp, cred, new_filesize);
			fvdat->flag &= ~FN_SIZECHANGE;
		}
	}
	debug_printf("fuse_getattr e: returning 0\n");

out:
	fdisp_destroy(&fdi);
	return err;

fake:
	bzero(vap, sizeof(*vap));
	vap->va_type = vnode_vtype(vp);

	return 0;
}

/*
    struct vnop_inactive_args {
	struct vnode *a_vp;
	struct thread *a_td;
    };
*/
static int
fuse_vnop_inactive(struct vop_inactive_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct thread *td = ap->a_td;

	struct fuse_vnode_data *fvdat = VTOFUD(vp);
	struct fuse_filehandle *fufh = NULL;

	int type, need_flush = 1;

	FS_DEBUG("inode=%ju\n", (uintmax_t)VTOI(vp));

	for (type = 0; type < FUFH_MAXTYPE; type++) {
		fufh = &(fvdat->fufh[type]);
		if (FUFH_IS_VALID(fufh)) {
			if (need_flush && vp->v_type == VREG) {
				if ((VTOFUD(vp)->flag & FN_SIZECHANGE) != 0) {
					fuse_vnode_savesize(vp, NULL);
				}
				if (fuse_data_cache_invalidate ||
				    (fvdat->flag & FN_REVOKED) != 0)
					fuse_io_invalbuf(vp, td);
				else
					fuse_io_flushbuf(vp, MNT_WAIT, td);
				need_flush = 0;
			}
			fuse_filehandle_close(vp, type, td, NULL);
		}
	}

	if ((fvdat->flag & FN_REVOKED) != 0 && fuse_reclaim_revoked) {
		vrecycle(vp);
	}
	return 0;
}

/*
    struct vnop_link_args {
	struct vnode *a_tdvp;
	struct vnode *a_vp;
	struct componentname *a_cnp;
    };
*/
static int
fuse_vnop_link(struct vop_link_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct vnode *tdvp = ap->a_tdvp;
	struct componentname *cnp = ap->a_cnp;

	struct vattr *vap = VTOVA(vp);

	struct fuse_dispatcher fdi;
	struct fuse_entry_out *feo;
	struct fuse_link_in fli;

	int err;

	fuse_trace_printf_vnop();

	if (fuse_isdeadfs(vp)) {
		return ENXIO;
	}
	if (vnode_mount(tdvp) != vnode_mount(vp)) {
		return EXDEV;
	}

	/*
	 * This is a seatbelt check to protect naive userspace filesystems from
	 * themselves and the limitations of the FUSE IPC protocol.  If a
	 * filesystem does not allow attribute caching, assume it is capable of
	 * validating that nlink does not overflow.
	 */
	if (vap != NULL && vap->va_nlink >= FUSE_LINK_MAX)
		return EMLINK;
	fli.oldnodeid = VTOI(vp);

	fdisp_init(&fdi, 0);
	fuse_internal_newentry_makerequest(vnode_mount(tdvp), VTOI(tdvp), cnp,
	    FUSE_LINK, &fli, sizeof(fli), &fdi);
	if ((err = fdisp_wait_answ(&fdi))) {
		goto out;
	}
	feo = fdi.answ;

	err = fuse_internal_checkentry(feo, vnode_vtype(vp));
out:
	fdisp_destroy(&fdi);
	return err;
}

/*
    struct vnop_lookup_args {
	struct vnodeop_desc *a_desc;
	struct vnode *a_dvp;
	struct vnode **a_vpp;
	struct componentname *a_cnp;
    };
*/
int
fuse_vnop_lookup(struct vop_lookup_args *ap)
{
	struct vnode *dvp = ap->a_dvp;
	struct vnode **vpp = ap->a_vpp;
	struct componentname *cnp = ap->a_cnp;
	struct thread *td = cnp->cn_thread;
	struct ucred *cred = cnp->cn_cred;

	int nameiop = cnp->cn_nameiop;
	int flags = cnp->cn_flags;
	int wantparent = flags & (LOCKPARENT | WANTPARENT);
	int islastcn = flags & ISLASTCN;
	struct mount *mp = vnode_mount(dvp);

	int err = 0;
	int lookup_err = 0;
	struct vnode *vp = NULL;

	struct fuse_dispatcher fdi;
	enum fuse_opcode op;

	uint64_t nid;
	struct fuse_access_param facp;

	FS_DEBUG2G("parent_inode=%ju - %*s\n",
	    (uintmax_t)VTOI(dvp), (int)cnp->cn_namelen, cnp->cn_nameptr);

	if (fuse_isdeadfs(dvp)) {
		*vpp = NULL;
		return ENXIO;
	}
	if (!vnode_isdir(dvp)) {
		return ENOTDIR;
	}
	if (islastcn && vfs_isrdonly(mp) && (nameiop != LOOKUP)) {
		return EROFS;
	}
	/*
	 * We do access check prior to doing anything else only in the case
	 * when we are at fs root (we'd like to say, "we are at the first
	 * component", but that's not exactly the same... nevermind).
	 * See further comments at further access checks.
	 */

	bzero(&facp, sizeof(facp));
	if (vnode_isvroot(dvp)) {	/* early permission check hack */
		if ((err = fuse_internal_access(dvp, VEXEC, &facp, td, cred))) {
			return err;
		}
	}
	if (flags & ISDOTDOT) {
		nid = VTOFUD(dvp)->parent_nid;
		if (nid == 0) {
			return ENOENT;
		}
		fdisp_init(&fdi, 0);
		op = FUSE_GETATTR;
		goto calldaemon;
	} else if (cnp->cn_namelen == 1 && *(cnp->cn_nameptr) == '.') {
		nid = VTOI(dvp);
		fdisp_init(&fdi, 0);
		op = FUSE_GETATTR;
		goto calldaemon;
	} else if (fuse_lookup_cache_enable) {
		err = cache_lookup(dvp, vpp, cnp, NULL, NULL);
		switch (err) {

		case -1:		/* positive match */
			atomic_add_acq_long(&fuse_lookup_cache_hits, 1);
			return 0;

		case 0:		/* no match in cache */
			atomic_add_acq_long(&fuse_lookup_cache_misses, 1);
			break;

		case ENOENT:		/* negative match */
			/* fall through */
		default:
			return err;
		}
	}
	nid = VTOI(dvp);
	fdisp_init(&fdi, cnp->cn_namelen + 1);
	op = FUSE_LOOKUP;

calldaemon:
	fdisp_make(&fdi, op, mp, nid, td, cred);

	if (op == FUSE_LOOKUP) {
		memcpy(fdi.indata, cnp->cn_nameptr, cnp->cn_namelen);
		((char *)fdi.indata)[cnp->cn_namelen] = '\0';
	}
	lookup_err = fdisp_wait_answ(&fdi);

	if ((op == FUSE_LOOKUP) && !lookup_err) {	/* lookup call succeeded */
		nid = ((struct fuse_entry_out *)fdi.answ)->nodeid;
		if (!nid) {
			/*
	                 * zero nodeid is the same as "not found",
	                 * but it's also cacheable (which we keep
	                 * keep on doing not as of writing this)
	                 */
			lookup_err = ENOENT;
		} else if (nid == FUSE_ROOT_ID) {
			lookup_err = EINVAL;
		}
	}
	if (lookup_err &&
	    (!fdi.answ_stat || lookup_err != ENOENT || op != FUSE_LOOKUP)) {
		fdisp_destroy(&fdi);
		return lookup_err;
	}
	/* lookup_err, if non-zero, must be ENOENT at this point */

	if (lookup_err) {

		if ((nameiop == CREATE || nameiop == RENAME) && islastcn
		     /* && directory dvp has not been removed */ ) {

			if (vfs_isrdonly(mp)) {
				err = EROFS;
				goto out;
			}
#if 0 /* THINK_ABOUT_THIS */
			if ((err = fuse_internal_access(dvp, VWRITE, cred, td, &facp))) {
				goto out;
			}
#endif

			/*
	                 * Possibly record the position of a slot in the
	                 * directory large enough for the new component name.
	                 * This can be recorded in the vnode private data for
	                 * dvp. Set the SAVENAME flag to hold onto the
	                 * pathname for use later in VOP_CREATE or VOP_RENAME.
	                 */
			cnp->cn_flags |= SAVENAME;

			err = EJUSTRETURN;
			goto out;
		}
		/* Consider inserting name into cache. */

		/*
	         * No we can't use negative caching, as the fs
	         * changes are out of our control.
	         * False positives' falseness turns out just as things
	         * go by, but false negatives' falseness doesn't.
	         * (and aiding the caching mechanism with extra control
	         * mechanisms comes quite close to beating the whole purpose
	         * caching...)
	         */
#if 0
		if ((cnp->cn_flags & MAKEENTRY) != 0) {
			FS_DEBUG("inserting NULL into cache\n");
			cache_enter(dvp, NULL, cnp);
		}
#endif
		err = ENOENT;
		goto out;

	} else {

		/* !lookup_err */

		struct fuse_entry_out *feo = NULL;
		struct fuse_attr *fattr = NULL;

		if (op == FUSE_GETATTR) {
			fattr = &((struct fuse_attr_out *)fdi.answ)->attr;
		} else {
			feo = (struct fuse_entry_out *)fdi.answ;
			fattr = &(feo->attr);
		}

		/*
	         * If deleting, and at end of pathname, return parameters
	         * which can be used to remove file.  If the wantparent flag
	         * isn't set, we return only the directory, otherwise we go on
	         * and lock the inode, being careful with ".".
	         */
		if (nameiop == DELETE && islastcn) {
			/*
	                 * Check for write access on directory.
	                 */
			facp.xuid = fattr->uid;
			facp.facc_flags |= FACCESS_STICKY;
			err = fuse_internal_access(dvp, VWRITE, &facp, td, cred);
			facp.facc_flags &= ~FACCESS_XQUERIES;

			if (err) {
				goto out;
			}
			if (nid == VTOI(dvp)) {
				vref(dvp);
				*vpp = dvp;
			} else {
				err = fuse_vnode_get(dvp->v_mount, feo, nid,
				    dvp, &vp, cnp, IFTOVT(fattr->mode));
				if (err)
					goto out;
				*vpp = vp;
			}

			/*
			 * Save the name for use in VOP_RMDIR and VOP_REMOVE
			 * later.
			 */
			cnp->cn_flags |= SAVENAME;
			goto out;

		}
		/*
	         * If rewriting (RENAME), return the inode and the
	         * information required to rewrite the present directory
	         * Must get inode of directory entry to verify it's a
	         * regular file, or empty directory.
	         */
		if (nameiop == RENAME && wantparent && islastcn) {

#if 0 /* THINK_ABOUT_THIS */
			if ((err = fuse_internal_access(dvp, VWRITE, cred, td, &facp))) {
				goto out;
			}
#endif

			/*
	                 * Check for "."
	                 */
			if (nid == VTOI(dvp)) {
				err = EISDIR;
				goto out;
			}
			err = fuse_vnode_get(vnode_mount(dvp), feo, nid, dvp,
			    &vp, cnp, IFTOVT(fattr->mode));
			if (err) {
				goto out;
			}
			*vpp = vp;
			/*
	                 * Save the name for use in VOP_RENAME later.
	                 */
			cnp->cn_flags |= SAVENAME;

			goto out;
		}
		if (flags & ISDOTDOT) {
			struct mount *mp;
			int ltype;

			/*
			 * Expanded copy of vn_vget_ino() so that
			 * fuse_vnode_get() can be used.
			 */
			mp = dvp->v_mount;
			ltype = VOP_ISLOCKED(dvp);
			err = vfs_busy(mp, MBF_NOWAIT);
			if (err != 0) {
				vfs_ref(mp);
				VOP_UNLOCK(dvp, 0);
				err = vfs_busy(mp, 0);
				vn_lock(dvp, ltype | LK_RETRY);
				vfs_rel(mp);
				if (err)
					goto out;
				if ((dvp->v_iflag & VI_DOOMED) != 0) {
					err = ENOENT;
					vfs_unbusy(mp);
					goto out;
				}
			}
			VOP_UNLOCK(dvp, 0);
			err = fuse_vnode_get(vnode_mount(dvp), feo, nid, NULL,
			    &vp, cnp, IFTOVT(fattr->mode));
			vfs_unbusy(mp);
			vn_lock(dvp, ltype | LK_RETRY);
			if ((dvp->v_iflag & VI_DOOMED) != 0) {
				if (err == 0)
					vput(vp);
				err = ENOENT;
			}
			if (err)
				goto out;
			*vpp = vp;
		} else if (nid == VTOI(dvp)) {
			vref(dvp);
			*vpp = dvp;
		} else {
			struct fuse_vnode_data *fvdat;

			err = fuse_vnode_get(vnode_mount(dvp), feo, nid, dvp,
			    &vp, cnp, IFTOVT(fattr->mode));
			if (err) {
				goto out;
			}
			fuse_vnode_setparent(vp, dvp);

			/*
			 * In the case where we are looking up a FUSE node
			 * represented by an existing cached vnode, and the
			 * true size reported by FUSE_LOOKUP doesn't match
			 * the vnode's cached size, fix the vnode cache to
			 * match the real object size.
			 *
			 * This can occur via FUSE distributed filesystems,
			 * irregular files, etc.
			 */
			fvdat = VTOFUD(vp);
			if (vnode_isreg(vp) &&
			    fattr->size != fvdat->filesize) {
				/*
				 * The FN_SIZECHANGE flag reflects a dirty
				 * append.  If userspace lets us know our cache
				 * is invalid, that write was lost.  (Dirty
				 * writes that do not cause append are also
				 * lost, but we don't detect them here.)
				 *
				 * XXX: Maybe disable WB caching on this mount.
				 */
				if (fvdat->flag & FN_SIZECHANGE)
					printf("%s: WB cache incoherent on "
					    "%s!\n", __func__,
					    vnode_mount(vp)->mnt_stat.f_mntonname);

				(void)fuse_vnode_setsize(vp, cred, fattr->size);
				fvdat->flag &= ~FN_SIZECHANGE;
			}
			*vpp = vp;
		}

		if (op == FUSE_GETATTR) {
			cache_attrs(*vpp, (struct fuse_attr_out *)fdi.answ,
			    NULL);
		} else {
			cache_attrs(*vpp, (struct fuse_entry_out *)fdi.answ,
			    NULL);
		}

		/* Insert name into cache if appropriate. */

		/*
	         * Nooo, caching is evil. With caching, we can't avoid stale
	         * information taking over the playground (cached info is not
	         * just positive/negative, it does have qualitative aspects,
	         * too). And a (VOP/FUSE)_GETATTR is always thrown anyway, when
	         * walking down along cached path components, and that's not
	         * any cheaper than FUSE_LOOKUP. This might change with
	         * implementing kernel side attr caching, but... In Linux,
	         * lookup results are not cached, and the daemon is bombarded
	         * with FUSE_LOOKUPS on and on. This shows that by design, the
	         * daemon is expected to handle frequent lookup queries
	         * efficiently, do its caching in userspace, and so on.
	         *
	         * So just leave the name cache alone.
	         */

		/*
	         * Well, now I know, Linux caches lookups, but with a
	         * timeout... So it's the same thing as attribute caching:
	         * we can deal with it when implement timeouts.
	         */
#if 0
		if (cnp->cn_flags & MAKEENTRY) {
			cache_enter(dvp, *vpp, cnp);
		}
#endif
	}
out:
	if (!lookup_err) {

		/* No lookup error; need to clean up. */

		if (err) {		/* Found inode; exit with no vnode. */
			if (op == FUSE_LOOKUP) {
				fuse_internal_forget_send(vnode_mount(dvp), td, cred,
				    nid, 1);
			}
			fdisp_destroy(&fdi);
			return err;
		} else {
#ifndef NO_EARLY_PERM_CHECK_HACK
			if (!islastcn) {
				/*
				 * We have the attributes of the next item
				 * *now*, and it's a fact, and we do not
				 * have to do extra work for it (ie, beg the
				 * daemon), and it neither depends on such
				 * accidental things like attr caching. So
				 * the big idea: check credentials *now*,
				 * not at the beginning of the next call to
				 * lookup.
				 * 
				 * The first item of the lookup chain (fs root)
				 * won't be checked then here, of course, as
				 * its never "the next". But go and see that
				 * the root is taken care about at the very
				 * beginning of this function.
				 * 
				 * Now, given we want to do the access check
				 * this way, one might ask: so then why not
				 * do the access check just after fetching
				 * the inode and its attributes from the
				 * daemon? Why bother with producing the
				 * corresponding vnode at all if something
				 * is not OK? We know what's the deal as
				 * soon as we get those attrs... There is
				 * one bit of info though not given us by
				 * the daemon: whether his response is
				 * authoritative or not... His response should
				 * be ignored if something is mounted over
				 * the dir in question. But that can be
				 * known only by having the vnode...
				 */
				int tmpvtype = vnode_vtype(*vpp);

				bzero(&facp, sizeof(facp));
				/*the early perm check hack */
				    facp.facc_flags |= FACCESS_VA_VALID;

				if ((tmpvtype != VDIR) && (tmpvtype != VLNK)) {
					err = ENOTDIR;
				}
				if (!err && !vnode_mountedhere(*vpp)) {
					err = fuse_internal_access(*vpp, VEXEC, &facp, td, cred);
				}
				if (err) {
					if (tmpvtype == VLNK)
						FS_DEBUG("weird, permission error with a symlink?\n");
					vput(*vpp);
					*vpp = NULL;
				}
			}
#endif
		}
	}
	fdisp_destroy(&fdi);

	return err;
}

/*
    struct vnop_mkdir_args {
	struct vnode *a_dvp;
	struct vnode **a_vpp;
	struct componentname *a_cnp;
	struct vattr *a_vap;
    };
*/
static int
fuse_vnop_mkdir(struct vop_mkdir_args *ap)
{
	struct vnode *dvp = ap->a_dvp;
	struct vnode **vpp = ap->a_vpp;
	struct componentname *cnp = ap->a_cnp;
	struct vattr *vap = ap->a_vap;

	struct fuse_mkdir_in fmdi;

	fuse_trace_printf_vnop();

	if (fuse_isdeadfs(dvp)) {
		return ENXIO;
	}
	fmdi.mode = MAKEIMODE(vap->va_type, vap->va_mode);

	return (fuse_internal_newentry(dvp, vpp, cnp, FUSE_MKDIR, &fmdi,
	    sizeof(fmdi), VDIR));
}

/*
    struct vnop_mknod_args {
	struct vnode *a_dvp;
	struct vnode **a_vpp;
	struct componentname *a_cnp;
	struct vattr *a_vap;
    };
*/
static int
fuse_vnop_mknod(struct vop_mknod_args *ap)
{

	return (EINVAL);
}


/*
    struct vnop_open_args {
	struct vnode *a_vp;
	int  a_mode;
	struct ucred *a_cred;
	struct thread *a_td;
	int a_fdidx; / struct file *a_fp;
    };
*/
static int
fuse_vnop_open(struct vop_open_args *ap)
{
	struct vnode *vp = ap->a_vp;
	int mode = ap->a_mode;
	struct thread *td = ap->a_td;
	struct ucred *cred = ap->a_cred;

	fufh_type_t fufh_type;
	struct fuse_vnode_data *fvdat;

	int error, isdir = 0;
	int32_t fuse_open_flags;

	FS_DEBUG2G("inode=%ju mode=0x%x\n", (uintmax_t)VTOI(vp), mode);

	if (fuse_isdeadfs(vp)) {
		return ENXIO;
	}
	if ((mode & (FREAD | FWRITE)) == 0)
		return EINVAL;

	fvdat = VTOFUD(vp);

	if (vnode_isdir(vp)) {
		isdir = 1;
	}
	fuse_open_flags = 0;
	if (isdir) {
		fufh_type = FUFH_RDONLY;
	} else {
		fufh_type = fuse_filehandle_xlate_from_fflags(mode);
		/*
		 * For WRONLY opens, force DIRECT_IO.  This is necessary
		 * since writing a partial block through the buffer cache
		 * will result in a read of the block and that read won't
		 * be allowed by the WRONLY open.
		 */
		if (fufh_type == FUFH_WRONLY ||
		    (fvdat->flag & FN_DIRECTIO) != 0)
			fuse_open_flags = FOPEN_DIRECT_IO;
	}

	if (fuse_filehandle_validrw(vp, fufh_type) != FUFH_INVALID) {
		fuse_vnode_open(vp, fuse_open_flags, td);
		return 0;
	}
	error = fuse_filehandle_open(vp, fufh_type, NULL, td, cred);

	return error;
}

static int
fuse_vnop_pathconf(struct vop_pathconf_args *ap)
{

	switch (ap->a_name) {
	case _PC_FILESIZEBITS:
		*ap->a_retval = 64;
		return (0);
	case _PC_NAME_MAX:
		*ap->a_retval = NAME_MAX;
		return (0);
	case _PC_LINK_MAX:
		*ap->a_retval = MIN(LONG_MAX, FUSE_LINK_MAX);
		return (0);
	case _PC_SYMLINK_MAX:
		*ap->a_retval = MAXPATHLEN;
		return (0);
	case _PC_NO_TRUNC:
		*ap->a_retval = 1;
		return (0);
	default:
		return (vop_stdpathconf(ap));
	}
}

/*
    struct vnop_read_args {
	struct vnode *a_vp;
	struct uio *a_uio;
	int  a_ioflag;
	struct ucred *a_cred;
    };
*/
static int
fuse_vnop_read(struct vop_read_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct uio *uio = ap->a_uio;
	int ioflag = ap->a_ioflag;
	struct ucred *cred = ap->a_cred;

	FS_DEBUG2G("inode=%ju offset=%jd resid=%zd\n",
	    (uintmax_t)VTOI(vp), uio->uio_offset, uio->uio_resid);

	if (fuse_isdeadfs(vp)) {
		return ENXIO;
	}

	if (VTOFUD(vp)->flag & FN_DIRECTIO) {
		ioflag |= IO_DIRECT;
	}

	return fuse_io_dispatch(vp, uio, ioflag, cred);
}

/*
    struct vnop_readdir_args {
	struct vnode *a_vp;
	struct uio *a_uio;
	struct ucred *a_cred;
	int *a_eofflag;
	int *ncookies;
	u_long **a_cookies;
    };
*/
static int
fuse_vnop_readdir(struct vop_readdir_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct uio *uio = ap->a_uio;
	struct ucred *cred = ap->a_cred;

	struct fuse_filehandle *fufh = NULL;
	struct fuse_iov cookediov;

	int err = 0;
	int freefufh = 0;

	FS_DEBUG2G("inode=%ju\n", (uintmax_t)VTOI(vp));

	if (fuse_isdeadfs(vp)) {
		return ENXIO;
	}
	if (				/* XXXIP ((uio_iovcnt(uio) > 1)) || */
	    (uio_resid(uio) < sizeof(struct dirent))) {
		return EINVAL;
	}

	if (!fuse_filehandle_valid(vp, FUFH_RDONLY)) {
		FS_DEBUG("calling readdir() before open()");
		err = fuse_filehandle_open(vp, FUFH_RDONLY, &fufh, NULL, cred);
		freefufh = 1;
	} else {
		err = fuse_filehandle_get(vp, FUFH_RDONLY, &fufh);
	}
	if (err) {
		return (err);
	}
#define DIRCOOKEDSIZE FUSE_DIRENT_ALIGN(FUSE_NAME_OFFSET + MAXNAMLEN + 1)
	fiov_init(&cookediov, DIRCOOKEDSIZE);

	err = fuse_internal_readdir(vp, uio, fufh, &cookediov);

	fiov_teardown(&cookediov);
	if (freefufh) {
		fuse_filehandle_close(vp, FUFH_RDONLY, NULL, cred);
	}
	return err;
}

/*
    struct vnop_readlink_args {
	struct vnode *a_vp;
	struct uio *a_uio;
	struct ucred *a_cred;
    };
*/
static int
fuse_vnop_readlink(struct vop_readlink_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct uio *uio = ap->a_uio;
	struct ucred *cred = ap->a_cred;

	struct fuse_dispatcher fdi;
	int err;

	FS_DEBUG2G("inode=%ju\n", (uintmax_t)VTOI(vp));

	if (fuse_isdeadfs(vp)) {
		return ENXIO;
	}
	if (!vnode_islnk(vp)) {
		return EINVAL;
	}
	fdisp_init(&fdi, 0);
	err = fdisp_simple_putget_vp(&fdi, FUSE_READLINK, vp, curthread, cred);
	if (err) {
		goto out;
	}
	if (((char *)fdi.answ)[0] == '/' &&
	    fuse_get_mpdata(vnode_mount(vp))->dataflags & FSESS_PUSH_SYMLINKS_IN) {
		char *mpth = vnode_mount(vp)->mnt_stat.f_mntonname;

		err = uiomove(mpth, strlen(mpth), uio);
	}
	if (!err) {
		err = uiomove(fdi.answ, fdi.iosize, uio);
	}
out:
	fdisp_destroy(&fdi);
	return err;
}

/*
    struct vnop_reclaim_args {
	struct vnode *a_vp;
	struct thread *a_td;
    };
*/
static int
fuse_vnop_reclaim(struct vop_reclaim_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct thread *td = ap->a_td;

	struct fuse_vnode_data *fvdat = VTOFUD(vp);
	struct fuse_filehandle *fufh = NULL;

	int type;

	if (!fvdat) {
		panic("FUSE: no vnode data during recycling");
	}
	FS_DEBUG("inode=%ju\n", (uintmax_t)VTOI(vp));

	for (type = 0; type < FUFH_MAXTYPE; type++) {
		fufh = &(fvdat->fufh[type]);
		if (FUFH_IS_VALID(fufh)) {
			printf("FUSE: vnode being reclaimed but fufh (type=%d) is valid",
			    type);
			fuse_filehandle_close(vp, type, td, NULL);
		}
	}

	if ((!fuse_isdeadfs(vp)) && (fvdat->nlookup)) {
		fuse_internal_forget_send(vnode_mount(vp), td, NULL, VTOI(vp),
		    fvdat->nlookup);
	}
	fuse_vnode_setparent(vp, NULL);
	cache_purge(vp);
	vfs_hash_remove(vp);
	vnode_destroy_vobject(vp);
	fuse_vnode_destroy(vp);

	return 0;
}

/*
    struct vnop_remove_args {
	struct vnode *a_dvp;
	struct vnode *a_vp;
	struct componentname *a_cnp;
    };
*/
static int
fuse_vnop_remove(struct vop_remove_args *ap)
{
	struct vnode *dvp = ap->a_dvp;
	struct vnode *vp = ap->a_vp;
	struct componentname *cnp = ap->a_cnp;

	int err;

	FS_DEBUG2G("inode=%ju name=%*s\n",
	    (uintmax_t)VTOI(vp), (int)cnp->cn_namelen, cnp->cn_nameptr);

	if (fuse_isdeadfs(vp)) {
		return ENXIO;
	}
	if (vnode_isdir(vp)) {
		return EPERM;
	}
	cache_purge(vp);

	err = fuse_internal_remove(dvp, vp, cnp, FUSE_UNLINK);

	if (err == 0)
		fuse_internal_vnode_disappear(vp);
	return err;
}

/*
    struct vnop_rename_args {
	struct vnode *a_fdvp;
	struct vnode *a_fvp;
	struct componentname *a_fcnp;
	struct vnode *a_tdvp;
	struct vnode *a_tvp;
	struct componentname *a_tcnp;
    };
*/
static int
fuse_vnop_rename(struct vop_rename_args *ap)
{
	struct vnode *fdvp = ap->a_fdvp;
	struct vnode *fvp = ap->a_fvp;
	struct componentname *fcnp = ap->a_fcnp;
	struct vnode *tdvp = ap->a_tdvp;
	struct vnode *tvp = ap->a_tvp;
	struct componentname *tcnp = ap->a_tcnp;
	struct fuse_data *data;

	int err = 0;

	FS_DEBUG2G("from: inode=%ju name=%*s -> to: inode=%ju name=%*s\n",
	    (uintmax_t)VTOI(fvp), (int)fcnp->cn_namelen, fcnp->cn_nameptr,
	    (uintmax_t)(tvp == NULL ? -1 : VTOI(tvp)),
	    (int)tcnp->cn_namelen, tcnp->cn_nameptr);

	if (fuse_isdeadfs(fdvp)) {
		return ENXIO;
	}
	if (fvp->v_mount != tdvp->v_mount ||
	    (tvp && fvp->v_mount != tvp->v_mount)) {
		FS_DEBUG("cross-device rename: %s -> %s\n",
		    fcnp->cn_nameptr, (tcnp != NULL ? tcnp->cn_nameptr : "(NULL)"));
		err = EXDEV;
		goto out;
	}
	cache_purge(fvp);

	/*
	 * FUSE library is expected to check if target directory is not
	 * under the source directory in the file system tree.
	 * Linux performs this check at VFS level.
	 */
	data = fuse_get_mpdata(vnode_mount(tdvp));
	sx_xlock(&data->rename_lock);
	err = fuse_internal_rename(fdvp, fcnp, tdvp, tcnp);
	if (err == 0) {
		if (tdvp != fdvp)
			fuse_vnode_setparent(fvp, tdvp);
		if (tvp != NULL)
			fuse_vnode_setparent(tvp, NULL);
	}
	sx_unlock(&data->rename_lock);

	if (tvp != NULL && tvp != fvp) {
		cache_purge(tvp);
	}
	if (vnode_isdir(fvp)) {
		if ((tvp != NULL) && vnode_isdir(tvp)) {
			cache_purge(tdvp);
		}
		cache_purge(fdvp);
	}
out:
	if (tdvp == tvp) {
		vrele(tdvp);
	} else {
		vput(tdvp);
	}
	if (tvp != NULL) {
		vput(tvp);
	}
	vrele(fdvp);
	vrele(fvp);

	return err;
}

/*
    struct vnop_rmdir_args {
	    struct vnode *a_dvp;
	    struct vnode *a_vp;
	    struct componentname *a_cnp;
    } *ap;
*/
static int
fuse_vnop_rmdir(struct vop_rmdir_args *ap)
{
	struct vnode *dvp = ap->a_dvp;
	struct vnode *vp = ap->a_vp;

	int err;

	FS_DEBUG2G("inode=%ju\n", (uintmax_t)VTOI(vp));

	if (fuse_isdeadfs(vp)) {
		return ENXIO;
	}
	if (VTOFUD(vp) == VTOFUD(dvp)) {
		return EINVAL;
	}
	err = fuse_internal_remove(dvp, vp, ap->a_cnp, FUSE_RMDIR);

	if (err == 0)
		fuse_internal_vnode_disappear(vp);
	return err;
}

/*
    struct vnop_setattr_args {
	struct vnode *a_vp;
	struct vattr *a_vap;
	struct ucred *a_cred;
	struct thread *a_td;
    };
*/
static int
fuse_vnop_setattr(struct vop_setattr_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct vattr *vap = ap->a_vap;
	struct ucred *cred = ap->a_cred;
	struct thread *td = curthread;

	struct fuse_dispatcher fdi;
	struct fuse_setattr_in *fsai;
	struct fuse_access_param facp;

	int err = 0;
	enum vtype vtyp;
	int sizechanged = 0;
	uint64_t newsize = 0;

	FS_DEBUG2G("inode=%ju\n", (uintmax_t)VTOI(vp));

	if (fuse_isdeadfs(vp)) {
		return ENXIO;
	}
	fdisp_init(&fdi, sizeof(*fsai));
	fdisp_make_vp(&fdi, FUSE_SETATTR, vp, td, cred);
	fsai = fdi.indata;
	fsai->valid = 0;

	bzero(&facp, sizeof(facp));

	facp.xuid = vap->va_uid;
	facp.xgid = vap->va_gid;

	if (vap->va_uid != (uid_t)VNOVAL) {
		facp.facc_flags |= FACCESS_CHOWN;
		fsai->uid = vap->va_uid;
		fsai->valid |= FATTR_UID;
	}
	if (vap->va_gid != (gid_t)VNOVAL) {
		facp.facc_flags |= FACCESS_CHOWN;
		fsai->gid = vap->va_gid;
		fsai->valid |= FATTR_GID;
	}
	if (vap->va_size != VNOVAL) {

		struct fuse_filehandle *fufh = NULL;

		/*Truncate to a new value. */
		    fsai->size = vap->va_size;
		sizechanged = 1;
		newsize = vap->va_size;
		fsai->valid |= FATTR_SIZE;

		fuse_filehandle_getrw(vp, FUFH_WRONLY, &fufh);
		if (fufh) {
			fsai->fh = fufh->fh_id;
			fsai->valid |= FATTR_FH;
		}
	}
	if (vap->va_atime.tv_sec != VNOVAL) {
		fsai->atime = vap->va_atime.tv_sec;
		fsai->atimensec = vap->va_atime.tv_nsec;
		fsai->valid |= FATTR_ATIME;
	}
	if (vap->va_mtime.tv_sec != VNOVAL) {
		fsai->mtime = vap->va_mtime.tv_sec;
		fsai->mtimensec = vap->va_mtime.tv_nsec;
		fsai->valid |= FATTR_MTIME;
	}
	if (vap->va_mode != (mode_t)VNOVAL) {
		fsai->mode = vap->va_mode & ALLPERMS;
		fsai->valid |= FATTR_MODE;
	}
	if (!fsai->valid) {
		goto out;
	}
	vtyp = vnode_vtype(vp);

	if (fsai->valid & FATTR_SIZE && vtyp == VDIR) {
		err = EISDIR;
		goto out;
	}
	if (vfs_isrdonly(vnode_mount(vp)) && (fsai->valid & ~FATTR_SIZE || vtyp == VREG)) {
		err = EROFS;
		goto out;
	}
	if (fsai->valid & ~FATTR_SIZE) {
	  /*err = fuse_internal_access(vp, VADMIN, context, &facp); */
	  /*XXX */
		    err = 0;
	}
	facp.facc_flags &= ~FACCESS_XQUERIES;

	if (err && !(fsai->valid & ~(FATTR_ATIME | FATTR_MTIME)) &&
	    vap->va_vaflags & VA_UTIMES_NULL) {
		err = fuse_internal_access(vp, VWRITE, &facp, td, cred);
	}
	if (err)
		goto out;
	if ((err = fdisp_wait_answ(&fdi)))
		goto out;
	vtyp = IFTOVT(((struct fuse_attr_out *)fdi.answ)->attr.mode);

	if (vnode_vtype(vp) != vtyp) {
		if (vnode_vtype(vp) == VNON && vtyp != VNON) {
			debug_printf("FUSE: Dang! vnode_vtype is VNON and vtype isn't.\n");
		} else {
			/*
	                 * STALE vnode, ditch
	                 *
	                 * The vnode has changed its type "behind our back". There's
	                 * nothing really we can do, so let us just force an internal
	                 * revocation and tell the caller to try again, if interested.
	                 */
			fuse_internal_vnode_disappear(vp);
			err = EAGAIN;
		}
	}
	if (err == 0)
		cache_attrs(vp, (struct fuse_attr_out *)fdi.answ, NULL);

out:
	fdisp_destroy(&fdi);
	if (!err && sizechanged) {
		fuse_vnode_setsize(vp, cred, newsize);
		VTOFUD(vp)->flag &= ~FN_SIZECHANGE;
	}
	return err;
}

/*
    struct vnop_strategy_args {
	struct vnode *a_vp;
	struct buf *a_bp;
    };
*/
static int
fuse_vnop_strategy(struct vop_strategy_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct buf *bp = ap->a_bp;

	fuse_trace_printf_vnop();

	if (!vp || fuse_isdeadfs(vp)) {
		bp->b_ioflags |= BIO_ERROR;
		bp->b_error = ENXIO;
		bufdone(bp);
		return ENXIO;
	}
	if (bp->b_iocmd == BIO_WRITE)
		fuse_vnode_refreshsize(vp, NOCRED);

	(void)fuse_io_strategy(vp, bp);

	/*
	 * This is a dangerous function. If returns error, that might mean a
	 * panic. We prefer pretty much anything over being forced to panic
	 * by a malicious daemon (a demon?). So we just return 0 anyway. You
	 * should never mind this: this function has its own error
	 * propagation mechanism via the argument buffer, so
	 * not-that-melodramatic residents of the call chain still will be
	 * able to know what to do.
	 */
	return 0;
}


/*
    struct vnop_symlink_args {
	struct vnode *a_dvp;
	struct vnode **a_vpp;
	struct componentname *a_cnp;
	struct vattr *a_vap;
	char *a_target;
    };
*/
static int
fuse_vnop_symlink(struct vop_symlink_args *ap)
{
	struct vnode *dvp = ap->a_dvp;
	struct vnode **vpp = ap->a_vpp;
	struct componentname *cnp = ap->a_cnp;
	const char *target = ap->a_target;

	struct fuse_dispatcher fdi;

	int err;
	size_t len;

	FS_DEBUG2G("inode=%ju name=%*s\n",
	    (uintmax_t)VTOI(dvp), (int)cnp->cn_namelen, cnp->cn_nameptr);

	if (fuse_isdeadfs(dvp)) {
		return ENXIO;
	}
	/*
	 * Unlike the other creator type calls, here we have to create a message
	 * where the name of the new entry comes first, and the data describing
	 * the entry comes second.
	 * Hence we can't rely on our handy fuse_internal_newentry() routine,
	 * but put together the message manually and just call the core part.
	 */

	len = strlen(target) + 1;
	fdisp_init(&fdi, len + cnp->cn_namelen + 1);
	fdisp_make_vp(&fdi, FUSE_SYMLINK, dvp, curthread, NULL);

	memcpy(fdi.indata, cnp->cn_nameptr, cnp->cn_namelen);
	((char *)fdi.indata)[cnp->cn_namelen] = '\0';
	memcpy((char *)fdi.indata + cnp->cn_namelen + 1, target, len);

	err = fuse_internal_newentry_core(dvp, vpp, cnp, VLNK, &fdi);
	fdisp_destroy(&fdi);
	return err;
}

/*
    struct vnop_write_args {
	struct vnode *a_vp;
	struct uio *a_uio;
	int  a_ioflag;
	struct ucred *a_cred;
    };
*/
static int
fuse_vnop_write(struct vop_write_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct uio *uio = ap->a_uio;
	int ioflag = ap->a_ioflag;
	struct ucred *cred = ap->a_cred;

	fuse_trace_printf_vnop();

	if (fuse_isdeadfs(vp)) {
		return ENXIO;
	}
	fuse_vnode_refreshsize(vp, cred);

	if (VTOFUD(vp)->flag & FN_DIRECTIO) {
		ioflag |= IO_DIRECT;
	}

	return fuse_io_dispatch(vp, uio, ioflag, cred);
}

/*
    struct vnop_getpages_args {
	struct vnode *a_vp;
	vm_page_t *a_m;
	int a_count;
	int a_reqpage;
    };
*/
static int
fuse_vnop_getpages(struct vop_getpages_args *ap)
{
	int i, error, nextoff, size, toff, count, npages;
	struct uio uio;
	struct iovec iov;
	vm_offset_t kva;
	struct buf *bp;
	struct vnode *vp;
	struct thread *td;
	struct ucred *cred;
	vm_page_t *pages;

	FS_DEBUG2G("heh\n");

	vp = ap->a_vp;
	KASSERT(vp->v_object, ("objectless vp passed to getpages"));
	td = curthread;			/* XXX */
	cred = curthread->td_ucred;	/* XXX */
	pages = ap->a_m;
	npages = ap->a_count;

	if (!fsess_opt_mmap(vnode_mount(vp))) {
		FS_DEBUG("called on non-cacheable vnode??\n");
		return (VM_PAGER_ERROR);
	}

	/*
	 * If the last page is partially valid, just return it and allow
	 * the pager to zero-out the blanks.  Partially valid pages can
	 * only occur at the file EOF.
	 *
	 * XXXGL: is that true for FUSE, which is a local filesystem,
	 * but still somewhat disconnected from the kernel?
	 */
	VM_OBJECT_WLOCK(vp->v_object);
	if (pages[npages - 1]->valid != 0 && --npages == 0)
		goto out;
	VM_OBJECT_WUNLOCK(vp->v_object);

	/*
	 * We use only the kva address for the buffer, but this is extremely
	 * convenient and fast.
	 */
	bp = uma_zalloc(fuse_pbuf_zone, M_WAITOK);

	kva = (vm_offset_t)bp->b_data;
	pmap_qenter(kva, pages, npages);
	VM_CNT_INC(v_vnodein);
	VM_CNT_ADD(v_vnodepgsin, npages);

	count = npages << PAGE_SHIFT;
	iov.iov_base = (caddr_t)kva;
	iov.iov_len = count;
	uio.uio_iov = &iov;
	uio.uio_iovcnt = 1;
	uio.uio_offset = IDX_TO_OFF(pages[0]->pindex);
	uio.uio_resid = count;
	uio.uio_segflg = UIO_SYSSPACE;
	uio.uio_rw = UIO_READ;
	uio.uio_td = td;

	error = fuse_io_dispatch(vp, &uio, IO_DIRECT, cred);
	pmap_qremove(kva, npages);

	uma_zfree(fuse_pbuf_zone, bp);

	if (error && (uio.uio_resid == count)) {
		FS_DEBUG("error %d\n", error);
		return VM_PAGER_ERROR;
	}
	/*
	 * Calculate the number of bytes read and validate only that number
	 * of bytes.  Note that due to pending writes, size may be 0.  This
	 * does not mean that the remaining data is invalid!
	 */

	size = count - uio.uio_resid;
	VM_OBJECT_WLOCK(vp->v_object);
	fuse_vm_page_lock_queues();
	for (i = 0, toff = 0; i < npages; i++, toff = nextoff) {
		vm_page_t m;

		nextoff = toff + PAGE_SIZE;
		m = pages[i];

		if (nextoff <= size) {
			/*
			 * Read operation filled an entire page
			 */
			m->valid = VM_PAGE_BITS_ALL;
			KASSERT(m->dirty == 0,
			    ("fuse_getpages: page %p is dirty", m));
		} else if (size > toff) {
			/*
			 * Read operation filled a partial page.
			 */
			m->valid = 0;
			vm_page_set_valid_range(m, 0, size - toff);
			KASSERT(m->dirty == 0,
			    ("fuse_getpages: page %p is dirty", m));
		} else {
			/*
			 * Read operation was short.  If no error occurred
			 * we may have hit a zero-fill section.   We simply
			 * leave valid set to 0.
			 */
			;
		}
	}
	fuse_vm_page_unlock_queues();
out:
	VM_OBJECT_WUNLOCK(vp->v_object);
	if (ap->a_rbehind)
		*ap->a_rbehind = 0;
	if (ap->a_rahead)
		*ap->a_rahead = 0;
	return (VM_PAGER_OK);
}

/*
    struct vnop_putpages_args {
	struct vnode *a_vp;
	vm_page_t *a_m;
	int a_count;
	int a_sync;
	int *a_rtvals;
	vm_ooffset_t a_offset;
    };
*/
static int
fuse_vnop_putpages(struct vop_putpages_args *ap)
{
	struct uio uio;
	struct iovec iov;
	vm_offset_t kva;
	struct buf *bp;
	int i, error, npages, count;
	off_t offset;
	int *rtvals;
	struct vnode *vp;
	struct thread *td;
	struct ucred *cred;
	vm_page_t *pages;
	vm_ooffset_t fsize;

	FS_DEBUG2G("heh\n");

	vp = ap->a_vp;
	KASSERT(vp->v_object, ("objectless vp passed to putpages"));
	fsize = vp->v_object->un_pager.vnp.vnp_size;
	td = curthread;			/* XXX */
	cred = curthread->td_ucred;	/* XXX */
	pages = ap->a_m;
	count = ap->a_count;
	rtvals = ap->a_rtvals;
	npages = btoc(count);
	offset = IDX_TO_OFF(pages[0]->pindex);

	if (!fsess_opt_mmap(vnode_mount(vp))) {
		FS_DEBUG("called on non-cacheable vnode??\n");
	}
	for (i = 0; i < npages; i++)
		rtvals[i] = VM_PAGER_AGAIN;

	/*
	 * When putting pages, do not extend file past EOF.
	 */

	if (offset + count > fsize) {
		count = fsize - offset;
		if (count < 0)
			count = 0;
	}
	/*
	 * We use only the kva address for the buffer, but this is extremely
	 * convenient and fast.
	 */
	bp = uma_zalloc(fuse_pbuf_zone, M_WAITOK);

	kva = (vm_offset_t)bp->b_data;
	pmap_qenter(kva, pages, npages);
	VM_CNT_INC(v_vnodeout);
	VM_CNT_ADD(v_vnodepgsout, count);

	iov.iov_base = (caddr_t)kva;
	iov.iov_len = count;
	uio.uio_iov = &iov;
	uio.uio_iovcnt = 1;
	uio.uio_offset = offset;
	uio.uio_resid = count;
	uio.uio_segflg = UIO_SYSSPACE;
	uio.uio_rw = UIO_WRITE;
	uio.uio_td = td;

	error = fuse_io_dispatch(vp, &uio, IO_DIRECT, cred);

	pmap_qremove(kva, npages);
	uma_zfree(fuse_pbuf_zone, bp);

	if (!error) {
		int nwritten = round_page(count - uio.uio_resid) / PAGE_SIZE;

		for (i = 0; i < nwritten; i++) {
			rtvals[i] = VM_PAGER_OK;
			VM_OBJECT_WLOCK(pages[i]->object);
			vm_page_undirty(pages[i]);
			VM_OBJECT_WUNLOCK(pages[i]->object);
		}
	}
	return rtvals[0];
}

static const char extattr_namespace_separator = '.';

/*
    struct vop_getextattr_args {
	struct vop_generic_args a_gen;
	struct vnode *a_vp;
	int a_attrnamespace;
	const char *a_name;
	struct uio *a_uio;
	size_t *a_size;
	struct ucred *a_cred;
	struct thread *a_td;
    };
*/
static int
fuse_vnop_getextattr(struct vop_getextattr_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct uio *uio = ap->a_uio;
	struct fuse_dispatcher fdi;
	struct fuse_getxattr_in *get_xattr_in;
	struct fuse_getxattr_out *get_xattr_out;
	struct mount *mp = vnode_mount(vp);
	struct thread *td = ap->a_td;
	struct ucred *cred = ap->a_cred;
	char *prefix;
	char *attr_str;
	size_t len;
	int err;

	fuse_trace_printf_vnop();

	if (fuse_isdeadfs(vp))
		return (ENXIO);

	/* Default to looking for user attributes. */
	if (ap->a_attrnamespace == EXTATTR_NAMESPACE_SYSTEM)
		prefix = EXTATTR_NAMESPACE_SYSTEM_STRING;
	else
		prefix = EXTATTR_NAMESPACE_USER_STRING;

	len = strlen(prefix) + sizeof(extattr_namespace_separator) +
	    strlen(ap->a_name) + 1;

	fdisp_init(&fdi, len + sizeof(*get_xattr_in));
	fdisp_make_vp(&fdi, FUSE_GETXATTR, vp, td, cred);

	get_xattr_in = fdi.indata;
	/*
	 * Check to see whether we're querying the available size or
	 * issuing the actual request.  If we pass in 0, we get back struct
	 * fuse_getxattr_out.  If we pass in a non-zero size, we get back
	 * that much data, without the struct fuse_getxattr_out header.
	 */
	if (uio == NULL)
		get_xattr_in->size = 0;
	else
		get_xattr_in->size = uio->uio_resid;

	attr_str = (char *)fdi.indata + sizeof(*get_xattr_in);
	snprintf(attr_str, len, "%s%c%s", prefix, extattr_namespace_separator,
	    ap->a_name);

	err = fdisp_wait_answ(&fdi);
	if (err != 0) {
		if (err == ENOSYS)
			fsess_set_notimpl(mp, FUSE_GETXATTR);
		debug_printf("getxattr: got err=%d from daemon\n", err);
		goto out;
	}

	get_xattr_out = fdi.answ;

	if (ap->a_size != NULL)
		*ap->a_size = get_xattr_out->size;

	if (uio != NULL)
		err = uiomove(fdi.answ, fdi.iosize, uio);

out:
	fdisp_destroy(&fdi);
	return (err);
}

/*
    struct vop_setextattr_args {
	struct vop_generic_args a_gen;
	struct vnode *a_vp;
	int a_attrnamespace;
	const char *a_name;
	struct uio *a_uio;
	struct ucred *a_cred;
	struct thread *a_td;
    };
*/
static int
fuse_vnop_setextattr(struct vop_setextattr_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct uio *uio = ap->a_uio;
	struct fuse_dispatcher fdi;
	struct fuse_setxattr_in *set_xattr_in;
	struct mount *mp = vnode_mount(vp);
	struct thread *td = ap->a_td;
	struct ucred *cred = ap->a_cred;
	char *prefix;
	size_t len;
	char *attr_str;
	int err;
	
	fuse_trace_printf_vnop();

	if (fuse_isdeadfs(vp))
		return (ENXIO);

	/* Default to looking for user attributes. */
	if (ap->a_attrnamespace == EXTATTR_NAMESPACE_SYSTEM)
		prefix = EXTATTR_NAMESPACE_SYSTEM_STRING;
	else
		prefix = EXTATTR_NAMESPACE_USER_STRING;

	len = strlen(prefix) + sizeof(extattr_namespace_separator) +
	    strlen(ap->a_name) + 1;

	fdisp_init(&fdi, len + sizeof(*set_xattr_in) + uio->uio_resid);
	fdisp_make_vp(&fdi, FUSE_SETXATTR, vp, td, cred);

	set_xattr_in = fdi.indata;
	set_xattr_in->size = uio->uio_resid;

	attr_str = (char *)fdi.indata + sizeof(*set_xattr_in);
	snprintf(attr_str, len, "%s%c%s", prefix, extattr_namespace_separator,
	    ap->a_name);

	err = uiomove((char *)fdi.indata + sizeof(*set_xattr_in) + len,
	    uio->uio_resid, uio);
	if (err != 0) {
		debug_printf("setxattr: got error %d from uiomove\n", err);
		goto out;
	}

	err = fdisp_wait_answ(&fdi);

	if (err != 0) {
		if (err == ENOSYS)
			fsess_set_notimpl(mp, FUSE_SETXATTR);
		debug_printf("setxattr: got err=%d from daemon\n", err);
		goto out;
	}

out:
	fdisp_destroy(&fdi);
	return (err);
}

/*
 * The Linux / FUSE extended attribute list is simply a collection of
 * NUL-terminated strings.  The FreeBSD extended attribute list is a single
 * byte length followed by a non-NUL terminated string.  So, this allows
 * conversion of the Linux / FUSE format to the FreeBSD format in place.
 * Linux attribute names are reported with the namespace as a prefix (e.g.
 * "user.attribute_name"), but in FreeBSD they are reported without the
 * namespace prefix (e.g. "attribute_name").  So, we're going from:
 *
 * user.attr_name1\0user.attr_name2\0
 *
 * to:
 *
 * <num>attr_name1<num>attr_name2
 *
 * Where "<num>" is a single byte number of characters in the attribute name.
 * 
 * Args:
 * prefix - exattr namespace prefix string
 * list, list_len - input list with namespace prefixes
 * bsd_list, bsd_list_len - output list compatible with bsd vfs
 */
static int
fuse_xattrlist_convert(char *prefix, const char *list, int list_len,
    char *bsd_list, int *bsd_list_len)
{
	int len, pos, dist_to_next, prefix_len;

	pos = 0;
	*bsd_list_len = 0;
	prefix_len = strlen(prefix);

	while (pos < list_len && list[pos] != '\0') {
		dist_to_next = strlen(&list[pos]) + 1;
		if (bcmp(&list[pos], prefix, prefix_len) == 0 &&
		    list[pos + prefix_len] == extattr_namespace_separator) {
			len = dist_to_next -
			    (prefix_len + sizeof(extattr_namespace_separator)) - 1;
			if (len >= EXTATTR_MAXNAMELEN)
				return (ENAMETOOLONG);

			bsd_list[*bsd_list_len] = len;
			memcpy(&bsd_list[*bsd_list_len + 1],
			    &list[pos + prefix_len +
			    sizeof(extattr_namespace_separator)], len);

			*bsd_list_len += len + 1;
		}

		pos += dist_to_next;
	}

	return (0);
}

/*
    struct vop_listextattr_args {
	struct vop_generic_args a_gen;
	struct vnode *a_vp;
	int a_attrnamespace;
	struct uio *a_uio;
	size_t *a_size;
	struct ucred *a_cred;
	struct thread *a_td;
    };
*/
static int
fuse_vnop_listextattr(struct vop_listextattr_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct uio *uio = ap->a_uio;
	struct fuse_dispatcher fdi;
	struct fuse_listxattr_in *list_xattr_in;
	struct fuse_listxattr_out *list_xattr_out;
	struct mount *mp = vnode_mount(vp);
	struct thread *td = ap->a_td;
	struct ucred *cred = ap->a_cred;
	size_t len;
	char *prefix;
	char *attr_str;
	char *bsd_list = NULL;
	char *linux_list;
	int bsd_list_len;
	int linux_list_len;
	int err;

	fuse_trace_printf_vnop();

	if (fuse_isdeadfs(vp))
		return (ENXIO);

	/*
	 * Add space for a NUL and the period separator if enabled.
	 * Default to looking for user attributes.
	 */
	if (ap->a_attrnamespace == EXTATTR_NAMESPACE_SYSTEM)
		prefix = EXTATTR_NAMESPACE_SYSTEM_STRING;
	else
		prefix = EXTATTR_NAMESPACE_USER_STRING;

	len = strlen(prefix) + sizeof(extattr_namespace_separator) + 1;

	fdisp_init(&fdi, sizeof(*list_xattr_in) + len);
	fdisp_make_vp(&fdi, FUSE_LISTXATTR, vp, td, cred);

	/*
	 * Retrieve Linux / FUSE compatible list size.
	 */
	list_xattr_in = fdi.indata;
	list_xattr_in->size = 0;
	attr_str = (char *)fdi.indata + sizeof(*list_xattr_in);
	snprintf(attr_str, len, "%s%c", prefix, extattr_namespace_separator);

	err = fdisp_wait_answ(&fdi);
	if (err != 0) {
		if (err == ENOSYS)
			fsess_set_notimpl(mp, FUSE_LISTXATTR);
		debug_printf("listextattr: got err=%d from daemon\n", err);
		goto out;
	}

	list_xattr_out = fdi.answ;
	linux_list_len = list_xattr_out->size;
	if (linux_list_len == 0) {
		if (ap->a_size != NULL)
			*ap->a_size = linux_list_len;
		goto out;
	}

	/*
	 * Retrieve Linux / FUSE compatible list values.
	 */
	fdisp_make_vp(&fdi, FUSE_LISTXATTR, vp, td, cred);
	list_xattr_in = fdi.indata;
	list_xattr_in->size = linux_list_len + sizeof(*list_xattr_out);
	attr_str = (char *)fdi.indata + sizeof(*list_xattr_in);
	snprintf(attr_str, len, "%s%c", prefix, extattr_namespace_separator);

	err = fdisp_wait_answ(&fdi);
	if (err != 0)
		goto out;

	linux_list = fdi.answ;
	linux_list_len = fdi.iosize;

	/*
	 * Retrieve the BSD compatible list values.
	 * The Linux / FUSE attribute list format isn't the same
	 * as FreeBSD's format. So we need to transform it into
	 * FreeBSD's format before giving it to the user.
	 */
	bsd_list = malloc(linux_list_len, M_TEMP, M_WAITOK);
	err = fuse_xattrlist_convert(prefix, linux_list, linux_list_len,
	    bsd_list, &bsd_list_len);
	if (err != 0)
		goto out;

	if (ap->a_size != NULL)
		*ap->a_size = bsd_list_len;

	if (uio != NULL)
		err = uiomove(bsd_list, bsd_list_len, uio);

out:
	free(bsd_list, M_TEMP);
	fdisp_destroy(&fdi);
	return (err);
}

/*
    struct vop_deleteextattr_args {
	struct vop_generic_args a_gen;
	struct vnode *a_vp;
	int a_attrnamespace;
	const char *a_name;
	struct ucred *a_cred;
	struct thread *a_td;
    };
*/
static int
fuse_vnop_deleteextattr(struct vop_deleteextattr_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct fuse_dispatcher fdi;
	struct mount *mp = vnode_mount(vp);
	struct thread *td = ap->a_td;
	struct ucred *cred = ap->a_cred;
	char *prefix;
	size_t len;
	char *attr_str;
	int err;

	fuse_trace_printf_vnop();

	if (fuse_isdeadfs(vp))
		return (ENXIO);

	/* Default to looking for user attributes. */
	if (ap->a_attrnamespace == EXTATTR_NAMESPACE_SYSTEM)
		prefix = EXTATTR_NAMESPACE_SYSTEM_STRING;
	else
		prefix = EXTATTR_NAMESPACE_USER_STRING;

	len = strlen(prefix) + sizeof(extattr_namespace_separator) +
	    strlen(ap->a_name) + 1;

	fdisp_init(&fdi, len);
	fdisp_make_vp(&fdi, FUSE_REMOVEXATTR, vp, td, cred);

	attr_str = fdi.indata;
	snprintf(attr_str, len, "%s%c%s", prefix, extattr_namespace_separator,
	    ap->a_name);

	err = fdisp_wait_answ(&fdi);
	if (err != 0) {
		if (err == ENOSYS)
			fsess_set_notimpl(mp, FUSE_REMOVEXATTR);
		debug_printf("removexattr: got err=%d from daemon\n", err);
	}

	fdisp_destroy(&fdi);
	return (err);
}

/*
    struct vnop_print_args {
	struct vnode *a_vp;
    };
*/
static int
fuse_vnop_print(struct vop_print_args *ap)
{
	struct fuse_vnode_data *fvdat = VTOFUD(ap->a_vp);

	printf("nodeid: %ju, parent nodeid: %ju, nlookup: %ju, flag: %#x\n",
	    (uintmax_t)VTOILLU(ap->a_vp), (uintmax_t)fvdat->parent_nid,
	    (uintmax_t)fvdat->nlookup,
	    fvdat->flag);

	return 0;
}
