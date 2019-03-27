/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 * (c) UNIX System Laboratories, Inc.
 * All or some portions of this file are derived from material licensed
 * to the University of California by American Telephone and Telegraph
 * Co. or Unix System Laboratories, Inc. and are reproduced herein with
 * the permission of UNIX System Laboratories, Inc.
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
 *	@(#)vfs_subr.c	8.31 (Berkeley) 5/26/95
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_inet.h"
#include "opt_inet6.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/dirent.h>
#include <sys/jail.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/mount.h>
#include <sys/mutex.h>
#include <sys/rmlock.h>
#include <sys/refcount.h>
#include <sys/signalvar.h>
#include <sys/socket.h>
#include <sys/vnode.h>

#include <netinet/in.h>
#include <net/radix.h>

static MALLOC_DEFINE(M_NETADDR, "export_host", "Export host address structure");

#if defined(INET) || defined(INET6)
static struct radix_node_head *vfs_create_addrlist_af(
		    struct radix_node_head **prnh, int off);
#endif
static void	vfs_free_addrlist(struct netexport *nep);
static int	vfs_free_netcred(struct radix_node *rn, void *w);
static void	vfs_free_addrlist_af(struct radix_node_head **prnh);
static int	vfs_hang_addrlist(struct mount *mp, struct netexport *nep,
		    struct export_args *argp);
static struct netcred *vfs_export_lookup(struct mount *, struct sockaddr *);

/*
 * Network address lookup element
 */
struct netcred {
	struct	radix_node netc_rnodes[2];
	int	netc_exflags;
	struct	ucred *netc_anon;
	int	netc_numsecflavors;
	int	netc_secflavors[MAXSECFLAVORS];
};

/*
 * Network export information
 */
struct netexport {
	struct	netcred ne_defexported;		      /* Default export */
	struct 	radix_node_head	*ne4;
	struct 	radix_node_head	*ne6;
};

/*
 * Build hash lists of net addresses and hang them off the mount point.
 * Called by vfs_export() to set up the lists of export addresses.
 */
static int
vfs_hang_addrlist(struct mount *mp, struct netexport *nep,
    struct export_args *argp)
{
	struct netcred *np;
	struct radix_node_head *rnh;
	int i;
	struct radix_node *rn;
	struct sockaddr *saddr, *smask = NULL;
#if defined(INET6) || defined(INET)
	int off;
#endif
	int error;

	/*
	 * XXX: This routine converts from a `struct xucred'
	 * (argp->ex_anon) to a `struct ucred' (np->netc_anon).  This
	 * operation is questionable; for example, what should be done
	 * with fields like cr_uidinfo and cr_prison?  Currently, this
	 * routine does not touch them (leaves them as NULL).
	 */
	if (argp->ex_anon.cr_version != XUCRED_VERSION) {
		vfs_mount_error(mp, "ex_anon.cr_version: %d != %d",
		    argp->ex_anon.cr_version, XUCRED_VERSION);
		return (EINVAL);
	}

	if (argp->ex_addrlen == 0) {
		if (mp->mnt_flag & MNT_DEFEXPORTED) {
			vfs_mount_error(mp,
			    "MNT_DEFEXPORTED already set for mount %p", mp);
			return (EPERM);
		}
		np = &nep->ne_defexported;
		np->netc_exflags = argp->ex_flags;
		np->netc_anon = crget();
		np->netc_anon->cr_uid = argp->ex_anon.cr_uid;
		crsetgroups(np->netc_anon, argp->ex_anon.cr_ngroups,
		    argp->ex_anon.cr_groups);
		np->netc_anon->cr_prison = &prison0;
		prison_hold(np->netc_anon->cr_prison);
		np->netc_numsecflavors = argp->ex_numsecflavors;
		bcopy(argp->ex_secflavors, np->netc_secflavors,
		    sizeof(np->netc_secflavors));
		MNT_ILOCK(mp);
		mp->mnt_flag |= MNT_DEFEXPORTED;
		MNT_IUNLOCK(mp);
		return (0);
	}

#if MSIZE <= 256
	if (argp->ex_addrlen > MLEN) {
		vfs_mount_error(mp, "ex_addrlen %d is greater than %d",
		    argp->ex_addrlen, MLEN);
		return (EINVAL);
	}
#endif

	i = sizeof(struct netcred) + argp->ex_addrlen + argp->ex_masklen;
	np = (struct netcred *) malloc(i, M_NETADDR, M_WAITOK | M_ZERO);
	saddr = (struct sockaddr *) (np + 1);
	if ((error = copyin(argp->ex_addr, saddr, argp->ex_addrlen)))
		goto out;
	if (saddr->sa_family == AF_UNSPEC || saddr->sa_family > AF_MAX) {
		error = EINVAL;
		vfs_mount_error(mp, "Invalid saddr->sa_family: %d");
		goto out;
	}
	if (saddr->sa_len > argp->ex_addrlen)
		saddr->sa_len = argp->ex_addrlen;
	if (argp->ex_masklen) {
		smask = (struct sockaddr *)((caddr_t)saddr + argp->ex_addrlen);
		error = copyin(argp->ex_mask, smask, argp->ex_masklen);
		if (error)
			goto out;
		if (smask->sa_len > argp->ex_masklen)
			smask->sa_len = argp->ex_masklen;
	}
	rnh = NULL;
	switch (saddr->sa_family) {
#ifdef INET
	case AF_INET:
		if ((rnh = nep->ne4) == NULL) {
			off = offsetof(struct sockaddr_in, sin_addr) << 3;
			rnh = vfs_create_addrlist_af(&nep->ne4, off);
		}
		break;
#endif
#ifdef INET6
	case AF_INET6:
		if ((rnh = nep->ne6) == NULL) {
			off = offsetof(struct sockaddr_in6, sin6_addr) << 3;
			rnh = vfs_create_addrlist_af(&nep->ne6, off);
		}
		break;
#endif
	}
	if (rnh == NULL) {
		error = ENOBUFS;
		vfs_mount_error(mp, "%s %s %d",
		    "Unable to initialize radix node head ",
		    "for address family", saddr->sa_family);
		goto out;
	}
	RADIX_NODE_HEAD_LOCK(rnh);
	rn = (*rnh->rnh_addaddr)(saddr, smask, &rnh->rh, np->netc_rnodes);
	RADIX_NODE_HEAD_UNLOCK(rnh);
	if (rn == NULL || np != (struct netcred *)rn) {	/* already exists */
		error = EPERM;
		vfs_mount_error(mp,
		    "netcred already exists for given addr/mask");
		goto out;
	}
	np->netc_exflags = argp->ex_flags;
	np->netc_anon = crget();
	np->netc_anon->cr_uid = argp->ex_anon.cr_uid;
	crsetgroups(np->netc_anon, argp->ex_anon.cr_ngroups,
	    argp->ex_anon.cr_groups);
	np->netc_anon->cr_prison = &prison0;
	prison_hold(np->netc_anon->cr_prison);
	np->netc_numsecflavors = argp->ex_numsecflavors;
	bcopy(argp->ex_secflavors, np->netc_secflavors,
	    sizeof(np->netc_secflavors));
	return (0);
out:
	free(np, M_NETADDR);
	return (error);
}

/* Helper for vfs_free_addrlist. */
/* ARGSUSED */
static int
vfs_free_netcred(struct radix_node *rn, void *w)
{
	struct radix_node_head *rnh = (struct radix_node_head *) w;
	struct ucred *cred;

	(*rnh->rnh_deladdr) (rn->rn_key, rn->rn_mask, &rnh->rh);
	cred = ((struct netcred *)rn)->netc_anon;
	if (cred != NULL)
		crfree(cred);
	free(rn, M_NETADDR);
	return (0);
}

#if defined(INET) || defined(INET6)
static struct radix_node_head *
vfs_create_addrlist_af(struct radix_node_head **prnh, int off)
{

	if (rn_inithead((void **)prnh, off) == 0)
		return (NULL);
	RADIX_NODE_HEAD_LOCK_INIT(*prnh);
	return (*prnh);
}
#endif

static void
vfs_free_addrlist_af(struct radix_node_head **prnh)
{
	struct radix_node_head *rnh;

	rnh = *prnh;
	RADIX_NODE_HEAD_LOCK(rnh);
	(*rnh->rnh_walktree)(&rnh->rh, vfs_free_netcred, rnh);
	RADIX_NODE_HEAD_UNLOCK(rnh);
	RADIX_NODE_HEAD_DESTROY(rnh);
	rn_detachhead((void **)prnh);
	prnh = NULL;
}

/*
 * Free the net address hash lists that are hanging off the mount points.
 */
static void
vfs_free_addrlist(struct netexport *nep)
{
	struct ucred *cred;

	if (nep->ne4 != NULL)
		vfs_free_addrlist_af(&nep->ne4);
	if (nep->ne6 != NULL)
		vfs_free_addrlist_af(&nep->ne6);

	cred = nep->ne_defexported.netc_anon;
	if (cred != NULL)
		crfree(cred);

}

/*
 * High level function to manipulate export options on a mount point
 * and the passed in netexport.
 * Struct export_args *argp is the variable used to twiddle options,
 * the structure is described in sys/mount.h
 */
int
vfs_export(struct mount *mp, struct export_args *argp)
{
	struct netexport *nep;
	int error;

	if (argp->ex_numsecflavors < 0
	    || argp->ex_numsecflavors >= MAXSECFLAVORS)
		return (EINVAL);

	error = 0;
	lockmgr(&mp->mnt_explock, LK_EXCLUSIVE, NULL);
	nep = mp->mnt_export;
	if (argp->ex_flags & MNT_DELEXPORT) {
		if (nep == NULL) {
			error = ENOENT;
			goto out;
		}
		if (mp->mnt_flag & MNT_EXPUBLIC) {
			vfs_setpublicfs(NULL, NULL, NULL);
			MNT_ILOCK(mp);
			mp->mnt_flag &= ~MNT_EXPUBLIC;
			MNT_IUNLOCK(mp);
		}
		vfs_free_addrlist(nep);
		mp->mnt_export = NULL;
		free(nep, M_MOUNT);
		nep = NULL;
		MNT_ILOCK(mp);
		mp->mnt_flag &= ~(MNT_EXPORTED | MNT_DEFEXPORTED);
		MNT_IUNLOCK(mp);
	}
	if (argp->ex_flags & MNT_EXPORTED) {
		if (nep == NULL) {
			nep = malloc(sizeof(struct netexport), M_MOUNT, M_WAITOK | M_ZERO);
			mp->mnt_export = nep;
		}
		if (argp->ex_flags & MNT_EXPUBLIC) {
			if ((error = vfs_setpublicfs(mp, nep, argp)) != 0)
				goto out;
			MNT_ILOCK(mp);
			mp->mnt_flag |= MNT_EXPUBLIC;
			MNT_IUNLOCK(mp);
		}
		if ((error = vfs_hang_addrlist(mp, nep, argp)))
			goto out;
		MNT_ILOCK(mp);
		mp->mnt_flag |= MNT_EXPORTED;
		MNT_IUNLOCK(mp);
	}

out:
	lockmgr(&mp->mnt_explock, LK_RELEASE, NULL);
	/*
	 * Once we have executed the vfs_export() command, we do
	 * not want to keep the "export" option around in the
	 * options list, since that will cause subsequent MNT_UPDATE
	 * calls to fail.  The export information is saved in
	 * mp->mnt_export, so we can safely delete the "export" mount option
	 * here.
	 */
	vfs_deleteopt(mp->mnt_optnew, "export");
	vfs_deleteopt(mp->mnt_opt, "export");
	return (error);
}

/*
 * Set the publicly exported filesystem (WebNFS). Currently, only
 * one public filesystem is possible in the spec (RFC 2054 and 2055)
 */
int
vfs_setpublicfs(struct mount *mp, struct netexport *nep,
    struct export_args *argp)
{
	int error;
	struct vnode *rvp;
	char *cp;

	/*
	 * mp == NULL -> invalidate the current info, the FS is
	 * no longer exported. May be called from either vfs_export
	 * or unmount, so check if it hasn't already been done.
	 */
	if (mp == NULL) {
		if (nfs_pub.np_valid) {
			nfs_pub.np_valid = 0;
			if (nfs_pub.np_index != NULL) {
				free(nfs_pub.np_index, M_TEMP);
				nfs_pub.np_index = NULL;
			}
		}
		return (0);
	}

	/*
	 * Only one allowed at a time.
	 */
	if (nfs_pub.np_valid != 0 && mp != nfs_pub.np_mount)
		return (EBUSY);

	/*
	 * Get real filehandle for root of exported FS.
	 */
	bzero(&nfs_pub.np_handle, sizeof(nfs_pub.np_handle));
	nfs_pub.np_handle.fh_fsid = mp->mnt_stat.f_fsid;

	if ((error = VFS_ROOT(mp, LK_EXCLUSIVE, &rvp)))
		return (error);

	if ((error = VOP_VPTOFH(rvp, &nfs_pub.np_handle.fh_fid)))
		return (error);

	vput(rvp);

	/*
	 * If an indexfile was specified, pull it in.
	 */
	if (argp->ex_indexfile != NULL) {
		if (nfs_pub.np_index == NULL)
			nfs_pub.np_index = malloc(MAXNAMLEN + 1, M_TEMP,
			    M_WAITOK);
		error = copyinstr(argp->ex_indexfile, nfs_pub.np_index,
		    MAXNAMLEN, (size_t *)0);
		if (!error) {
			/*
			 * Check for illegal filenames.
			 */
			for (cp = nfs_pub.np_index; *cp; cp++) {
				if (*cp == '/') {
					error = EINVAL;
					break;
				}
			}
		}
		if (error) {
			free(nfs_pub.np_index, M_TEMP);
			nfs_pub.np_index = NULL;
			return (error);
		}
	}

	nfs_pub.np_mount = mp;
	nfs_pub.np_valid = 1;
	return (0);
}

/*
 * Used by the filesystems to determine if a given network address
 * (passed in 'nam') is present in their exports list, returns a pointer
 * to struct netcred so that the filesystem can examine it for
 * access rights (read/write/etc).
 */
static struct netcred *
vfs_export_lookup(struct mount *mp, struct sockaddr *nam)
{
	RADIX_NODE_HEAD_RLOCK_TRACKER;
	struct netexport *nep;
	struct netcred *np = NULL;
	struct radix_node_head *rnh;
	struct sockaddr *saddr;

	nep = mp->mnt_export;
	if (nep == NULL)
		return (NULL);
	if ((mp->mnt_flag & MNT_EXPORTED) == 0)
		return (NULL);

	/*
	 * Lookup in the export list
	 */
	if (nam != NULL) {
		saddr = nam;
		rnh = NULL;
		switch (saddr->sa_family) {
		case AF_INET:
			rnh = nep->ne4;
			break;
		case AF_INET6:
			rnh = nep->ne6;
			break;
		}
		if (rnh != NULL) {
			RADIX_NODE_HEAD_RLOCK(rnh);
			np = (struct netcred *) (*rnh->rnh_matchaddr)(saddr, &rnh->rh);
			RADIX_NODE_HEAD_RUNLOCK(rnh);
			if (np != NULL && (np->netc_rnodes->rn_flags & RNF_ROOT) != 0)
				return (NULL);
		}
	}

	/*
	 * If no address match, use the default if it exists.
	 */
	if (np == NULL && (mp->mnt_flag & MNT_DEFEXPORTED) != 0)
		return (&nep->ne_defexported);

	return (np);
}

/*
 * XXX: This comment comes from the deprecated ufs_check_export()
 * XXX: and may not entirely apply, but lacking something better:
 * This is the generic part of fhtovp called after the underlying
 * filesystem has validated the file handle.
 *
 * Verify that a host should have access to a filesystem.
 */

int 
vfs_stdcheckexp(struct mount *mp, struct sockaddr *nam, int *extflagsp,
    struct ucred **credanonp, int *numsecflavors, int **secflavors)
{
	struct netcred *np;

	lockmgr(&mp->mnt_explock, LK_SHARED, NULL);
	np = vfs_export_lookup(mp, nam);
	if (np == NULL) {
		lockmgr(&mp->mnt_explock, LK_RELEASE, NULL);
		*credanonp = NULL;
		return (EACCES);
	}
	*extflagsp = np->netc_exflags;
	if ((*credanonp = np->netc_anon) != NULL)
		crhold(*credanonp);
	if (numsecflavors)
		*numsecflavors = np->netc_numsecflavors;
	if (secflavors)
		*secflavors = np->netc_secflavors;
	lockmgr(&mp->mnt_explock, LK_RELEASE, NULL);
	return (0);
}

