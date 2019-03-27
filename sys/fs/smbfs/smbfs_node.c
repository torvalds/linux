/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2000-2001 Boris Popov
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
 *
 * $FreeBSD$
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/fnv_hash.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mount.h>
#include <sys/proc.h>
#include <sys/queue.h>
#include <sys/stat.h>
#include <sys/sx.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <sys/vnode.h>

#include <netsmb/smb.h>
#include <netsmb/smb_conn.h>
#include <netsmb/smb_subr.h>

#include <vm/vm.h>
#include <vm/vm_extern.h>
/*#include <vm/vm_page.h>
#include <vm/vm_object.h>*/

#include <fs/smbfs/smbfs.h>
#include <fs/smbfs/smbfs_node.h>
#include <fs/smbfs/smbfs_subr.h>

extern struct vop_vector smbfs_vnodeops;	/* XXX -> .h file */

static MALLOC_DEFINE(M_SMBNODE, "smbufs_node", "SMBFS vnode private part");
static MALLOC_DEFINE(M_SMBNODENAME, "smbufs_nname", "SMBFS node name");

u_int32_t __inline
smbfs_hash(const u_char *name, int nmlen)
{
	return (fnv_32_buf(name, nmlen, FNV1_32_INIT)); 
}

static char *
smbfs_name_alloc(const u_char *name, int nmlen)
{
	u_char *cp;

	nmlen++;
	cp = malloc(nmlen, M_SMBNODENAME, M_WAITOK);
	bcopy(name, cp, nmlen - 1);
	cp[nmlen - 1] = 0;
	return cp;
}

static void
smbfs_name_free(u_char *name)
{

	free(name, M_SMBNODENAME);
}

static int __inline
smbfs_vnode_cmp(struct vnode *vp, void *_sc) 
{
	struct smbnode *np;
	struct smbcmp *sc;

	np = (struct smbnode *) vp->v_data;
	sc = (struct smbcmp *) _sc;
	if (np->n_parent != sc->n_parent || np->n_nmlen != sc->n_nmlen ||
	    bcmp(sc->n_name, np->n_name, sc->n_nmlen) != 0)
		return 1;
	return 0;
}

static int
smbfs_node_alloc(struct mount *mp, struct vnode *dvp, const char *dirnm, 
	int dirlen, const char *name, int nmlen, char sep, 
	struct smbfattr *fap, struct vnode **vpp)
{
	struct vattr vattr;
	struct thread *td = curthread;	/* XXX */
	struct smbmount *smp = VFSTOSMBFS(mp);
	struct smbnode *np, *dnp;
	struct vnode *vp, *vp2;
	struct smbcmp sc;
	char *p, *rpath;
	int error, rplen;

	sc.n_parent = dvp;
	sc.n_nmlen = nmlen;
	sc.n_name = name;	
	if (smp->sm_root != NULL && dvp == NULL) {
		SMBERROR("do not allocate root vnode twice!\n");
		return EINVAL;
	}
	if (nmlen == 2 && bcmp(name, "..", 2) == 0) {
		if (dvp == NULL)
			return EINVAL;
		vp = VTOSMB(VTOSMB(dvp)->n_parent)->n_vnode;
		error = vget(vp, LK_EXCLUSIVE, td);
		if (error == 0)
			*vpp = vp;
		return error;
	} else if (nmlen == 1 && name[0] == '.') {
		SMBERROR("do not call me with dot!\n");
		return EINVAL;
	}
	dnp = dvp ? VTOSMB(dvp) : NULL;
	if (dnp == NULL && dvp != NULL) {
		vn_printf(dvp, "smbfs_node_alloc: dead parent vnode ");
		return EINVAL;
	}
	error = vfs_hash_get(mp, smbfs_hash(name, nmlen), LK_EXCLUSIVE, td,
	    vpp, smbfs_vnode_cmp, &sc);
	if (error)
		return (error);
	if (*vpp) {
		np = VTOSMB(*vpp);
		/* Force cached attributes to be refreshed if stale. */
		(void)VOP_GETATTR(*vpp, &vattr, td->td_ucred);
		/*
		 * If the file type on the server is inconsistent with
		 * what it was when we created the vnode, kill the
		 * bogus vnode now and fall through to the code below
		 * to create a new one with the right type.
		 */
		if (((*vpp)->v_type == VDIR && 
		    (np->n_dosattr & SMB_FA_DIR) == 0) ||
	    	    ((*vpp)->v_type == VREG && 
		    (np->n_dosattr & SMB_FA_DIR) != 0)) {
			vgone(*vpp);
			vput(*vpp);
		}
		else {
			SMBVDEBUG("vnode taken from the hashtable\n");
			return (0);
		}
	}
	/*
	 * If we don't have node attributes, then it is an explicit lookup
	 * for an existing vnode.
	 */
	if (fap == NULL)
		return ENOENT;

	error = getnewvnode("smbfs", mp, &smbfs_vnodeops, vpp);
	if (error)
		return (error);
	vp = *vpp;
	np = malloc(sizeof *np, M_SMBNODE, M_WAITOK | M_ZERO);
	rplen = dirlen;
	if (sep != '\0')
		rplen++;
	rplen += nmlen;
	rpath = malloc(rplen + 1, M_SMBNODENAME, M_WAITOK);
	p = rpath;
	bcopy(dirnm, p, dirlen);
	p += dirlen;
	if (sep != '\0')
		*p++ = sep;
	if (name != NULL) {
		bcopy(name, p, nmlen);
		p += nmlen;
	}
	*p = '\0';
	MPASS(p == rpath + rplen);
	lockmgr(vp->v_vnlock, LK_EXCLUSIVE, NULL);
	/* Vnode initialization */
	vp->v_type = fap->fa_attr & SMB_FA_DIR ? VDIR : VREG;
	vp->v_data = np;
	np->n_vnode = vp;
	np->n_mount = VFSTOSMBFS(mp);
	np->n_rpath = rpath;
	np->n_rplen = rplen;
	np->n_nmlen = nmlen;
	np->n_name = smbfs_name_alloc(name, nmlen);
	np->n_ino = fap->fa_ino;
	if (dvp) {
		ASSERT_VOP_LOCKED(dvp, "smbfs_node_alloc");
		np->n_parent = dvp;
		np->n_parentino = VTOSMB(dvp)->n_ino;
		if (/*vp->v_type == VDIR &&*/ (dvp->v_vflag & VV_ROOT) == 0) {
			vref(dvp);
			np->n_flag |= NREFPARENT;
		}
	} else if (vp->v_type == VREG)
		SMBERROR("new vnode '%s' born without parent ?\n", np->n_name);
	error = insmntque(vp, mp);
	if (error) {
		free(np, M_SMBNODE);
		return (error);
	}
	error = vfs_hash_insert(vp, smbfs_hash(name, nmlen), LK_EXCLUSIVE,
	    td, &vp2, smbfs_vnode_cmp, &sc);
	if (error) 
		return (error);
	if (vp2 != NULL)
		*vpp = vp2;
	return (0);
}

int
smbfs_nget(struct mount *mp, struct vnode *dvp, const char *name, int nmlen,
	struct smbfattr *fap, struct vnode **vpp)
{
	struct smbnode *dnp, *np;
	struct vnode *vp;
	int error, sep;

	dnp = (dvp) ? VTOSMB(dvp) : NULL;
	sep = 0;
	if (dnp != NULL) {
		sep = SMBFS_DNP_SEP(dnp); 
		error = smbfs_node_alloc(mp, dvp, dnp->n_rpath, dnp->n_rplen, 
		    name, nmlen, sep, fap, &vp); 
	} else
		error = smbfs_node_alloc(mp, NULL, "\\", 1, name, nmlen, 
		    sep, fap, &vp); 
	if (error)
		return error;
	MPASS(vp != NULL);
	np = VTOSMB(vp);
	if (fap)
		smbfs_attr_cacheenter(vp, fap);
	*vpp = vp;
	return 0;
}

/*
 * Free smbnode, and give vnode back to system
 */
int
smbfs_reclaim(ap)                     
        struct vop_reclaim_args /* {
		struct vnode *a_vp;
		struct thread *a_p;
        } */ *ap;
{
	struct vnode *vp = ap->a_vp;
	struct vnode *dvp;
	struct smbnode *np = VTOSMB(vp);
	struct smbmount *smp = VTOSMBFS(vp);
	
	SMBVDEBUG("%s,%d\n", np->n_name, vrefcnt(vp));

	KASSERT((np->n_flag & NOPEN) == 0, ("file not closed before reclaim"));

	/*
	 * Destroy the vm object and flush associated pages.
	 */
	vnode_destroy_vobject(vp);
	dvp = (np->n_parent && (np->n_flag & NREFPARENT)) ?
	    np->n_parent : NULL;
	
	/*
	 * Remove the vnode from its hash chain.
	 */
	vfs_hash_remove(vp);
	if (np->n_name)
		smbfs_name_free(np->n_name);
	if (np->n_rpath)
		free(np->n_rpath, M_SMBNODENAME);
	free(np, M_SMBNODE);
	vp->v_data = NULL;
	if (dvp != NULL) {
		vrele(dvp);
		/*
		 * Indicate that we released something; see comment
		 * in smbfs_unmount().
		 */
		smp->sm_didrele = 1;
	}
	return 0;
}

int
smbfs_inactive(ap)
	struct vop_inactive_args /* {
		struct vnode *a_vp;
		struct thread *a_td;
	} */ *ap;
{
	struct thread *td = ap->a_td;
	struct ucred *cred = td->td_ucred;
	struct vnode *vp = ap->a_vp;
	struct smbnode *np = VTOSMB(vp);
	struct smb_cred *scred;
	struct vattr va;

	SMBVDEBUG("%s: %d\n", VTOSMB(vp)->n_name, vrefcnt(vp));
	if ((np->n_flag & NOPEN) != 0) {
		scred = smbfs_malloc_scred();
		smb_makescred(scred, td, cred);
		smbfs_vinvalbuf(vp, td);
		if (vp->v_type == VREG) {
			VOP_GETATTR(vp, &va, cred);
			smbfs_smb_close(np->n_mount->sm_share, np->n_fid,
			    &np->n_mtime, scred);
		} else if (vp->v_type == VDIR) {
			if (np->n_dirseq != NULL) {
				smbfs_findclose(np->n_dirseq, scred);
				np->n_dirseq = NULL;
			}
		}
		np->n_flag &= ~NOPEN;
		smbfs_attr_cacheremove(vp);
		smbfs_free_scred(scred);
	}
	if (np->n_flag & NGONE)
		vrecycle(vp);
	return (0);
}
/*
 * routines to maintain vnode attributes cache
 * smbfs_attr_cacheenter: unpack np.i to vattr structure
 */
void
smbfs_attr_cacheenter(struct vnode *vp, struct smbfattr *fap)
{
	struct smbnode *np = VTOSMB(vp);

	if (vp->v_type == VREG) {
		if (np->n_size != fap->fa_size) {
			np->n_size = fap->fa_size;
			vnode_pager_setsize(vp, np->n_size);
		}
	} else if (vp->v_type == VDIR) {
		np->n_size = 16384; 		/* should be a better way ... */
	} else
		return;
	np->n_mtime = fap->fa_mtime;
	np->n_dosattr = fap->fa_attr;
	np->n_attrage = time_second;
	return;
}

int
smbfs_attr_cachelookup(struct vnode *vp, struct vattr *va)
{
	struct smbnode *np = VTOSMB(vp);
	struct smbmount *smp = VTOSMBFS(vp);
	int diff;

	diff = time_second - np->n_attrage;
	if (diff > 2)	/* XXX should be configurable */
		return ENOENT;
	va->va_type = vp->v_type;		/* vnode type (for create) */
	va->va_flags = 0;			/* flags defined for file */
	if (vp->v_type == VREG) {
		va->va_mode = smp->sm_file_mode; /* files access mode and type */
		if (np->n_dosattr & SMB_FA_RDONLY) {
			va->va_mode &= ~(S_IWUSR|S_IWGRP|S_IWOTH);
			va->va_flags |= UF_READONLY;
		}
	} else if (vp->v_type == VDIR) {
		va->va_mode = smp->sm_dir_mode;	/* files access mode and type */
	} else
		return EINVAL;
	va->va_size = np->n_size;
	va->va_nlink = 1;		/* number of references to file */
	va->va_uid = smp->sm_uid;	/* owner user id */
	va->va_gid = smp->sm_gid;	/* owner group id */
	va->va_fsid = vp->v_mount->mnt_stat.f_fsid.val[0];
	va->va_fileid = np->n_ino;	/* file id */
	if (va->va_fileid == 0)
		va->va_fileid = 2;
	va->va_blocksize = SSTOVC(smp->sm_share)->vc_txmax;
	va->va_mtime = np->n_mtime;
	va->va_atime = va->va_ctime = va->va_mtime;	/* time file changed */
	va->va_gen = VNOVAL;		/* generation number of file */
	if (np->n_dosattr & SMB_FA_HIDDEN)
		va->va_flags |= UF_HIDDEN;
	if (np->n_dosattr & SMB_FA_SYSTEM)
		va->va_flags |= UF_SYSTEM;
	/*
	 * We don't set the archive bit for directories.
	 */
	if ((vp->v_type != VDIR) && (np->n_dosattr & SMB_FA_ARCHIVE))
		va->va_flags |= UF_ARCHIVE;
	va->va_rdev = NODEV;		/* device the special file represents */
	va->va_bytes = va->va_size;	/* bytes of disk space held by file */
	va->va_filerev = 0;		/* file modification number */
	va->va_vaflags = 0;		/* operations flags */
	return 0;
}
