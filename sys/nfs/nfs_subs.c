/*	$OpenBSD: nfs_subs.c,v 1.151 2024/09/09 03:50:14 jsg Exp $	*/
/*	$NetBSD: nfs_subs.c,v 1.27.4.3 1996/07/08 20:34:24 jtc Exp $	*/

/*
 * Copyright (c) 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Rick Macklem at The University of Guelph.
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
 *	@(#)nfs_subs.c	8.8 (Berkeley) 5/22/95
 */


/*
 * These functions support the nfsm_subs.h inline functions and help fiddle
 * mbuf chains for the nfs op functions. They do things such as creating the
 * rpc header and copying data between mbuf chains and uio lists.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/mount.h>
#include <sys/vnode.h>
#include <sys/namei.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/stat.h>
#include <sys/pool.h>
#include <sys/time.h>

#include <nfs/rpcv2.h>
#include <nfs/nfsproto.h>
#include <nfs/nfsnode.h>
#include <nfs/nfs.h>
#include <nfs/xdr_subs.h>
#include <nfs/nfsmount.h>
#include <nfs/nfs_var.h>
#include <nfs/nfsm_subs.h>

#include <netinet/in.h>

#include <crypto/idgen.h>

int	nfs_attrtimeo(struct nfsnode *np);
u_int32_t nfs_get_xid(void);

/*
 * Data items converted to xdr at startup, since they are constant
 * This is kinda hokey, but may save a little time doing byte swaps
 */
u_int32_t nfs_xdrneg1;
u_int32_t rpc_call, rpc_vers, rpc_reply, rpc_msgdenied, rpc_autherr,
	rpc_mismatch, rpc_auth_unix, rpc_msgaccepted;
u_int32_t nfs_prog, nfs_true, nfs_false;

/* And other global data */
const nfstype nfsv2_type[9] =
    { NFNON, NFREG, NFDIR, NFBLK, NFCHR, NFLNK, NFNON, NFCHR, NFNON };
const nfstype nfsv3_type[9] =
    { NFNON, NFREG, NFDIR, NFBLK, NFCHR, NFLNK, NFSOCK, NFFIFO, NFNON };
const enum vtype nv2tov_type[8] =
    { VNON, VREG, VDIR, VBLK, VCHR, VLNK, VNON, VNON };
const enum vtype nv3tov_type[8]=
    { VNON, VREG, VDIR, VBLK, VCHR, VLNK, VSOCK, VFIFO };
int nfs_ticks;
struct nfsstats nfsstats;

/*
 * Mapping of old NFS Version 2 RPC numbers to generic numbers.
 */
const int nfsv3_procid[NFS_NPROCS] = {
	NFSPROC_NULL,
	NFSPROC_GETATTR,
	NFSPROC_SETATTR,
	NFSPROC_NOOP,
	NFSPROC_LOOKUP,
	NFSPROC_READLINK,
	NFSPROC_READ,
	NFSPROC_NOOP,
	NFSPROC_WRITE,
	NFSPROC_CREATE,
	NFSPROC_REMOVE,
	NFSPROC_RENAME,
	NFSPROC_LINK,
	NFSPROC_SYMLINK,
	NFSPROC_MKDIR,
	NFSPROC_RMDIR,
	NFSPROC_READDIR,
	NFSPROC_FSSTAT,
	NFSPROC_NOOP,
	NFSPROC_NOOP,
	NFSPROC_NOOP,
	NFSPROC_NOOP,
	NFSPROC_NOOP
};

/*
 * and the reverse mapping from generic to Version 2 procedure numbers
 */
const int nfsv2_procid[NFS_NPROCS] = {
	NFSV2PROC_NULL,
	NFSV2PROC_GETATTR,
	NFSV2PROC_SETATTR,
	NFSV2PROC_LOOKUP,
	NFSV2PROC_NOOP,
	NFSV2PROC_READLINK,
	NFSV2PROC_READ,
	NFSV2PROC_WRITE,
	NFSV2PROC_CREATE,
	NFSV2PROC_MKDIR,
	NFSV2PROC_SYMLINK,
	NFSV2PROC_CREATE,
	NFSV2PROC_REMOVE,
	NFSV2PROC_RMDIR,
	NFSV2PROC_RENAME,
	NFSV2PROC_LINK,
	NFSV2PROC_READDIR,
	NFSV2PROC_NOOP,
	NFSV2PROC_STATFS,
	NFSV2PROC_NOOP,
	NFSV2PROC_NOOP,
	NFSV2PROC_NOOP,
	NFSV2PROC_NOOP
};

/*
 * Maps errno values to nfs error numbers.
 * Use NFSERR_IO as the catch all for ones not specifically defined in
 * RFC 1094.
 */
static const u_char nfsrv_v2errmap[] = {
  NFSERR_PERM,	NFSERR_NOENT,	NFSERR_IO,	NFSERR_IO,	NFSERR_IO,
  NFSERR_NXIO,	NFSERR_IO,	NFSERR_IO,	NFSERR_IO,	NFSERR_IO,
  NFSERR_IO,	NFSERR_IO,	NFSERR_ACCES,	NFSERR_IO,	NFSERR_IO,
  NFSERR_IO,	NFSERR_EXIST,	NFSERR_IO,	NFSERR_NODEV,	NFSERR_NOTDIR,
  NFSERR_ISDIR,	NFSERR_IO,	NFSERR_IO,	NFSERR_IO,	NFSERR_IO,
  NFSERR_IO,	NFSERR_FBIG,	NFSERR_NOSPC,	NFSERR_IO,	NFSERR_ROFS,
  NFSERR_IO,	NFSERR_IO,	NFSERR_IO,	NFSERR_IO,	NFSERR_IO,
  NFSERR_IO,	NFSERR_IO,	NFSERR_IO,	NFSERR_IO,	NFSERR_IO,
  NFSERR_IO,	NFSERR_IO,	NFSERR_IO,	NFSERR_IO,	NFSERR_IO,
  NFSERR_IO,	NFSERR_IO,	NFSERR_IO,	NFSERR_IO,	NFSERR_IO,
  NFSERR_IO,	NFSERR_IO,	NFSERR_IO,	NFSERR_IO,	NFSERR_IO,
  NFSERR_IO,	NFSERR_IO,	NFSERR_IO,	NFSERR_IO,	NFSERR_IO,
  NFSERR_IO,	NFSERR_IO,	NFSERR_NAMETOL,	NFSERR_IO,	NFSERR_IO,
  NFSERR_NOTEMPTY, NFSERR_IO,	NFSERR_IO,	NFSERR_DQUOT,	NFSERR_STALE
  /* Everything after this maps to NFSERR_IO, so far */
};

/*
 * Maps errno values to nfs error numbers.
 * Although it is not obvious whether or not NFS clients really care if
 * a returned error value is in the specified list for the procedure, the
 * safest thing to do is filter them appropriately. For Version 2, the
 * X/Open XNFS document is the only specification that defines error values
 * for each RPC (The RFC simply lists all possible error values for all RPCs),
 * so I have decided to not do this for Version 2.
 * The first entry is the default error return and the rest are the valid
 * errors for that RPC in increasing numeric order.
 */
static const short nfsv3err_null[] = {
	0,
	0,
};

static const short nfsv3err_getattr[] = {
	NFSERR_IO,
	NFSERR_IO,
	NFSERR_STALE,
	NFSERR_BADHANDLE,
	NFSERR_SERVERFAULT,
	0,
};

static const short nfsv3err_setattr[] = {
	NFSERR_IO,
	NFSERR_PERM,
	NFSERR_IO,
	NFSERR_ACCES,
	NFSERR_INVAL,
	NFSERR_NOSPC,
	NFSERR_ROFS,
	NFSERR_DQUOT,
	NFSERR_STALE,
	NFSERR_BADHANDLE,
	NFSERR_NOT_SYNC,
	NFSERR_SERVERFAULT,
	0,
};

static const short nfsv3err_lookup[] = {
	NFSERR_IO,
	NFSERR_NOENT,
	NFSERR_IO,
	NFSERR_ACCES,
	NFSERR_NOTDIR,
	NFSERR_NAMETOL,
	NFSERR_STALE,
	NFSERR_BADHANDLE,
	NFSERR_SERVERFAULT,
	0,
};

static const short nfsv3err_access[] = {
	NFSERR_IO,
	NFSERR_IO,
	NFSERR_STALE,
	NFSERR_BADHANDLE,
	NFSERR_SERVERFAULT,
	0,
};

static const short nfsv3err_readlink[] = {
	NFSERR_IO,
	NFSERR_IO,
	NFSERR_ACCES,
	NFSERR_INVAL,
	NFSERR_STALE,
	NFSERR_BADHANDLE,
	NFSERR_NOTSUPP,
	NFSERR_SERVERFAULT,
	0,
};

static const short nfsv3err_read[] = {
	NFSERR_IO,
	NFSERR_IO,
	NFSERR_NXIO,
	NFSERR_ACCES,
	NFSERR_INVAL,
	NFSERR_STALE,
	NFSERR_BADHANDLE,
	NFSERR_SERVERFAULT,
	0,
};

static const short nfsv3err_write[] = {
	NFSERR_IO,
	NFSERR_IO,
	NFSERR_ACCES,
	NFSERR_INVAL,
	NFSERR_FBIG,
	NFSERR_NOSPC,
	NFSERR_ROFS,
	NFSERR_DQUOT,
	NFSERR_STALE,
	NFSERR_BADHANDLE,
	NFSERR_SERVERFAULT,
	0,
};

static const short nfsv3err_create[] = {
	NFSERR_IO,
	NFSERR_IO,
	NFSERR_ACCES,
	NFSERR_EXIST,
	NFSERR_NOTDIR,
	NFSERR_NOSPC,
	NFSERR_ROFS,
	NFSERR_NAMETOL,
	NFSERR_DQUOT,
	NFSERR_STALE,
	NFSERR_BADHANDLE,
	NFSERR_NOTSUPP,
	NFSERR_SERVERFAULT,
	0,
};

static const short nfsv3err_mkdir[] = {
	NFSERR_IO,
	NFSERR_IO,
	NFSERR_ACCES,
	NFSERR_EXIST,
	NFSERR_NOTDIR,
	NFSERR_NOSPC,
	NFSERR_ROFS,
	NFSERR_NAMETOL,
	NFSERR_DQUOT,
	NFSERR_STALE,
	NFSERR_BADHANDLE,
	NFSERR_NOTSUPP,
	NFSERR_SERVERFAULT,
	0,
};

static const short nfsv3err_symlink[] = {
	NFSERR_IO,
	NFSERR_IO,
	NFSERR_ACCES,
	NFSERR_EXIST,
	NFSERR_NOTDIR,
	NFSERR_NOSPC,
	NFSERR_ROFS,
	NFSERR_NAMETOL,
	NFSERR_DQUOT,
	NFSERR_STALE,
	NFSERR_BADHANDLE,
	NFSERR_NOTSUPP,
	NFSERR_SERVERFAULT,
	0,
};

static const short nfsv3err_mknod[] = {
	NFSERR_IO,
	NFSERR_IO,
	NFSERR_ACCES,
	NFSERR_EXIST,
	NFSERR_NOTDIR,
	NFSERR_NOSPC,
	NFSERR_ROFS,
	NFSERR_NAMETOL,
	NFSERR_DQUOT,
	NFSERR_STALE,
	NFSERR_BADHANDLE,
	NFSERR_NOTSUPP,
	NFSERR_SERVERFAULT,
	NFSERR_BADTYPE,
	0,
};

static const short nfsv3err_remove[] = {
	NFSERR_IO,
	NFSERR_NOENT,
	NFSERR_IO,
	NFSERR_ACCES,
	NFSERR_NOTDIR,
	NFSERR_ROFS,
	NFSERR_NAMETOL,
	NFSERR_STALE,
	NFSERR_BADHANDLE,
	NFSERR_SERVERFAULT,
	0,
};

static const short nfsv3err_rmdir[] = {
	NFSERR_IO,
	NFSERR_NOENT,
	NFSERR_IO,
	NFSERR_ACCES,
	NFSERR_EXIST,
	NFSERR_NOTDIR,
	NFSERR_INVAL,
	NFSERR_ROFS,
	NFSERR_NAMETOL,
	NFSERR_NOTEMPTY,
	NFSERR_STALE,
	NFSERR_BADHANDLE,
	NFSERR_NOTSUPP,
	NFSERR_SERVERFAULT,
	0,
};

static const short nfsv3err_rename[] = {
	NFSERR_IO,
	NFSERR_NOENT,
	NFSERR_IO,
	NFSERR_ACCES,
	NFSERR_EXIST,
	NFSERR_XDEV,
	NFSERR_NOTDIR,
	NFSERR_ISDIR,
	NFSERR_INVAL,
	NFSERR_NOSPC,
	NFSERR_ROFS,
	NFSERR_MLINK,
	NFSERR_NAMETOL,
	NFSERR_NOTEMPTY,
	NFSERR_DQUOT,
	NFSERR_STALE,
	NFSERR_BADHANDLE,
	NFSERR_NOTSUPP,
	NFSERR_SERVERFAULT,
	0,
};

static const short nfsv3err_link[] = {
	NFSERR_IO,
	NFSERR_IO,
	NFSERR_ACCES,
	NFSERR_EXIST,
	NFSERR_XDEV,
	NFSERR_NOTDIR,
	NFSERR_INVAL,
	NFSERR_NOSPC,
	NFSERR_ROFS,
	NFSERR_MLINK,
	NFSERR_NAMETOL,
	NFSERR_DQUOT,
	NFSERR_STALE,
	NFSERR_BADHANDLE,
	NFSERR_NOTSUPP,
	NFSERR_SERVERFAULT,
	0,
};

static const short nfsv3err_readdir[] = {
	NFSERR_IO,
	NFSERR_IO,
	NFSERR_ACCES,
	NFSERR_NOTDIR,
	NFSERR_STALE,
	NFSERR_BADHANDLE,
	NFSERR_BAD_COOKIE,
	NFSERR_TOOSMALL,
	NFSERR_SERVERFAULT,
	0,
};

static const short nfsv3err_readdirplus[] = {
	NFSERR_IO,
	NFSERR_IO,
	NFSERR_ACCES,
	NFSERR_NOTDIR,
	NFSERR_STALE,
	NFSERR_BADHANDLE,
	NFSERR_BAD_COOKIE,
	NFSERR_NOTSUPP,
	NFSERR_TOOSMALL,
	NFSERR_SERVERFAULT,
	0,
};

static const short nfsv3err_fsstat[] = {
	NFSERR_IO,
	NFSERR_IO,
	NFSERR_STALE,
	NFSERR_BADHANDLE,
	NFSERR_SERVERFAULT,
	0,
};

static const short nfsv3err_fsinfo[] = {
	NFSERR_STALE,
	NFSERR_STALE,
	NFSERR_BADHANDLE,
	NFSERR_SERVERFAULT,
	0,
};

static const short nfsv3err_pathconf[] = {
	NFSERR_STALE,
	NFSERR_STALE,
	NFSERR_BADHANDLE,
	NFSERR_SERVERFAULT,
	0,
};

static const short nfsv3err_commit[] = {
	NFSERR_IO,
	NFSERR_IO,
	NFSERR_STALE,
	NFSERR_BADHANDLE,
	NFSERR_SERVERFAULT,
	0,
};

static const short *nfsrv_v3errmap[] = {
	nfsv3err_null,
	nfsv3err_getattr,
	nfsv3err_setattr,
	nfsv3err_lookup,
	nfsv3err_access,
	nfsv3err_readlink,
	nfsv3err_read,
	nfsv3err_write,
	nfsv3err_create,
	nfsv3err_mkdir,
	nfsv3err_symlink,
	nfsv3err_mknod,
	nfsv3err_remove,
	nfsv3err_rmdir,
	nfsv3err_rename,
	nfsv3err_link,
	nfsv3err_readdir,
	nfsv3err_readdirplus,
	nfsv3err_fsstat,
	nfsv3err_fsinfo,
	nfsv3err_pathconf,
	nfsv3err_commit,
};

struct pool nfsreqpl;

/*
 * Create the header for an rpc request packet
 * The hsiz is the size of the rest of the nfs request header.
 * (just used to decide if a cluster is a good idea)
 */
struct mbuf *
nfsm_reqhead(int hsiz)
{
	struct mbuf *mb;

	MGET(mb, M_WAIT, MT_DATA);
	if (hsiz > MLEN)
		MCLGET(mb, M_WAIT);
	mb->m_len = 0;
	
	/* Finally, return values */
	return (mb);
}

/*
 * Return an unpredictable XID in XDR form.
 */
u_int32_t
nfs_get_xid(void)
{
	static struct idgen32_ctx nfs_xid_ctx;
	static int called = 0;

	if (!called) {
		called = 1;
		idgen32_init(&nfs_xid_ctx);
	}
	return (txdr_unsigned(idgen32(&nfs_xid_ctx)));
}

/*
 * Build the RPC header and fill in the authorization info.
 * Right now we are pretty centric around RPCAUTH_UNIX, in the
 * future, this function will need some love to be able to handle
 * other authorization methods, such as Kerberos.
 */
void
nfsm_rpchead(struct nfsreq *req, struct ucred *cr, int auth_type)
{
	struct mbuf	*mb;
	u_int32_t	*tl;
	int		i, authsiz, auth_len, ngroups;

	KASSERT(auth_type == RPCAUTH_UNIX);

	/*
	 * RPCAUTH_UNIX fits in an hdr mbuf, in the future other
	 * authorization methods need to figure out their own sizes
	 * and allocate and chain mbufs accordingly.
	 */
	mb = req->r_mreq;

	/*
	 * We need to start out by finding how big the authorization cred
	 * and verifier are for the auth_type, to be able to correctly
	 * align the mbuf header/chain.
	 */
	switch (auth_type) {
	case RPCAUTH_UNIX:
		/*
		 * In the RPCAUTH_UNIX case, the size is the static
		 * part as shown in RFC1831 + the number of groups,
		 * RPCAUTH_UNIX has a zero verifier.
		 */
		if (cr->cr_ngroups > req->r_nmp->nm_numgrps)
			ngroups = req->r_nmp->nm_numgrps;
		else
			ngroups = cr->cr_ngroups;

		auth_len = (ngroups << 2) + 5 * NFSX_UNSIGNED;
		authsiz = nfsm_rndup(auth_len);
		/* The authorization size + the size of the static part */
		m_align(mb, authsiz + 10 * NFSX_UNSIGNED);
		break;
	}

	mb->m_len = 0;

	/* First the RPC header. */
	tl = nfsm_build(&mb, 6 * NFSX_UNSIGNED);

	/* Get a new (non-zero) xid */
	*tl++ = req->r_xid = nfs_get_xid();
	*tl++ = rpc_call;
	*tl++ = rpc_vers;
	*tl++ = nfs_prog;
	if (ISSET(req->r_nmp->nm_flag, NFSMNT_NFSV3)) {
		*tl++ = txdr_unsigned(NFS_VER3);
		*tl = txdr_unsigned(req->r_procnum);
	} else {
		*tl++ = txdr_unsigned(NFS_VER2);
		*tl = txdr_unsigned(nfsv2_procid[req->r_procnum]);
	}

	/* The Authorization cred and its verifier */
	switch (auth_type) {
	case RPCAUTH_UNIX:
		tl = nfsm_build(&mb, auth_len + 4 * NFSX_UNSIGNED);
		*tl++ = txdr_unsigned(RPCAUTH_UNIX);
		*tl++ = txdr_unsigned(authsiz);

		/* The authorization cred */
		*tl++ = 0;		/* stamp */
		*tl++ = 0;		/* NULL hostname */
		*tl++ = txdr_unsigned(cr->cr_uid);
		*tl++ = txdr_unsigned(cr->cr_gid);
		*tl++ = txdr_unsigned(ngroups);
		for (i = 0; i < ngroups; i++)
			*tl++ = txdr_unsigned(cr->cr_groups[i]);
		/* The authorization verifier */
		*tl++ = txdr_unsigned(RPCAUTH_NULL);
		*tl = 0;
		break;
	}

	mb->m_pkthdr.len += authsiz + 10 * NFSX_UNSIGNED;
	mb->m_pkthdr.ph_ifidx = 0;
}

/*
 * copies mbuf chain to the uio scatter/gather list
 */
int
nfsm_mbuftouio(struct mbuf **mrep, struct uio *uiop, int siz, caddr_t *dpos)
{
	char *mbufcp, *uiocp;
	int xfer, left, len;
	struct mbuf *mp;
	long uiosiz, rem;
	int error = 0;

	mp = *mrep;
	mbufcp = *dpos;
	len = mtod(mp, caddr_t)+mp->m_len-mbufcp;
	rem = nfsm_padlen(siz);
	while (siz > 0) {
		if (uiop->uio_iovcnt <= 0 || uiop->uio_iov == NULL)
			return (EFBIG);
		left = uiop->uio_iov->iov_len;
		uiocp = uiop->uio_iov->iov_base;
		if (left > siz)
			left = siz;
		uiosiz = left;
		while (left > 0) {
			while (len == 0) {
				mp = mp->m_next;
				if (mp == NULL)
					return (EBADRPC);
				mbufcp = mtod(mp, caddr_t);
				len = mp->m_len;
			}
			xfer = (left > len) ? len : left;
			if (uiop->uio_segflg == UIO_SYSSPACE)
				memcpy(uiocp, mbufcp, xfer);
			else
				copyout(mbufcp, uiocp, xfer);
			left -= xfer;
			len -= xfer;
			mbufcp += xfer;
			uiocp += xfer;
			uiop->uio_offset += xfer;
			uiop->uio_resid -= xfer;
		}
		if (uiop->uio_iov->iov_len <= siz) {
			uiop->uio_iovcnt--;
			uiop->uio_iov++;
		} else {
			uiop->uio_iov->iov_base =
			    (char *)uiop->uio_iov->iov_base + uiosiz;
			uiop->uio_iov->iov_len -= uiosiz;
		}
		siz -= uiosiz;
	}
	*dpos = mbufcp;
	*mrep = mp;
	if (rem > 0) {
		if (len < rem)
			error = nfs_adv(mrep, dpos, rem, len);
		else
			*dpos += rem;
	}
	return (error);
}

/*
 * Copy a uio scatter/gather list to an mbuf chain.
 */
void
nfsm_uiotombuf(struct mbuf **mp, struct uio *uiop, size_t len)
{
	struct mbuf *mb, *mb2;
	size_t xfer, pad;

	mb = *mp;

	pad = nfsm_padlen(len);

	/* XXX -- the following should be done by the caller */
	uiop->uio_resid = len;
	uiop->uio_rw = UIO_WRITE;

	while (len) {
		xfer = ulmin(len, m_trailingspace(mb));
		uiomove(mb_offset(mb), xfer, uiop);
		mb->m_len += xfer;
		len -= xfer;
		if (len > 0) {
			MGET(mb2, M_WAIT, MT_DATA);
			if (len > MLEN)
				MCLGET(mb2, M_WAIT);
			mb2->m_len = 0;
			mb->m_next = mb2;
			mb = mb2;
		}
	}

	if (pad > 0) {
		if (pad > m_trailingspace(mb)) {
			MGET(mb2, M_WAIT, MT_DATA);
			mb2->m_len = 0;
			mb->m_next = mb2;
			mb = mb2;
		}
		memset(mb_offset(mb), 0, pad);
		mb->m_len += pad;
	}

	*mp = mb;
}

/*
 * Copy a buffer to an mbuf chain
 */
void
nfsm_buftombuf(struct mbuf **mp, void *buf, size_t len)
{
	struct iovec iov;
	struct uio io;

	iov.iov_base = buf;
	iov.iov_len = len;

	io.uio_iov = &iov;
	io.uio_iovcnt = 1;
	io.uio_resid = len;
	io.uio_segflg = UIO_SYSSPACE;
	io.uio_rw = UIO_WRITE;

	nfsm_uiotombuf(mp, &io, len);
}

/*
 * Copy a string to an mbuf chain
 */
void
nfsm_strtombuf(struct mbuf **mp, void *str, size_t len)
{
	struct iovec iov[2];
	struct uio io;
	uint32_t strlen;

	strlen = txdr_unsigned(len);

	iov[0].iov_base = &strlen;
	iov[0].iov_len = sizeof(uint32_t);
	iov[1].iov_base = str;
	iov[1].iov_len = len;

	io.uio_iov = iov;
	io.uio_iovcnt = 2;
	io.uio_resid = sizeof(uint32_t) + len;
	io.uio_segflg = UIO_SYSSPACE;
	io.uio_rw = UIO_WRITE;

	nfsm_uiotombuf(mp, &io, io.uio_resid);
}

/*
 * Help break down an mbuf chain by setting the first siz bytes contiguous
 * pointed to by returned val.
 * This is used by nfsm_dissect for tough cases.
 */
int
nfsm_disct(struct mbuf **mdp, caddr_t *dposp, int siz, int left, caddr_t *cp2)
{
	struct mbuf *mp, *mp2;
	int siz2, xfer;
	caddr_t p;

	mp = *mdp;
	while (left == 0) {
		*mdp = mp = mp->m_next;
		if (mp == NULL)
			return (EBADRPC);
		left = mp->m_len;
		*dposp = mtod(mp, caddr_t);
	}
	if (left >= siz) {
		*cp2 = *dposp;
		*dposp += siz;
	} else if (mp->m_next == NULL) {
		return (EBADRPC);
	} else if (siz > MHLEN) {
		panic("nfs S too big");
	} else {
		MGET(mp2, M_WAIT, MT_DATA);
		mp2->m_next = mp->m_next;
		mp->m_next = mp2;
		mp->m_len -= left;
		mp = mp2;
		*cp2 = p = mtod(mp, caddr_t);
		bcopy(*dposp, p, left);		/* Copy what was left */
		siz2 = siz - left;
		p += left;
		mp2 = mp->m_next;
		/* Loop around copying up the siz2 bytes */
		while (siz2 > 0) {
			if (mp2 == NULL)
				return (EBADRPC);
			xfer = (siz2 > mp2->m_len) ? mp2->m_len : siz2;
			if (xfer > 0) {
				bcopy(mtod(mp2, caddr_t), p, xfer);
				mp2->m_data += xfer;
				mp2->m_len -= xfer;
				p += xfer;
				siz2 -= xfer;
			}
			if (siz2 > 0)
				mp2 = mp2->m_next;
		}
		mp->m_len = siz;
		*mdp = mp2;
		*dposp = mtod(mp2, caddr_t);
	}
	return (0);
}

/*
 * Advance the position in the mbuf chain.
 */
int
nfs_adv(struct mbuf **mdp, caddr_t *dposp, int offs, int left)
{
	struct mbuf *m;
	int s;

	m = *mdp;
	s = left;
	while (s < offs) {
		offs -= s;
		m = m->m_next;
		if (m == NULL)
			return (EBADRPC);
		s = m->m_len;
	}
	*mdp = m;
	*dposp = mtod(m, caddr_t)+offs;
	return (0);
}

/*
 * Called once to initialize data structures...
 */
void
nfs_init(void)
{
	rpc_vers = txdr_unsigned(RPC_VER2);
	rpc_call = txdr_unsigned(RPC_CALL);
	rpc_reply = txdr_unsigned(RPC_REPLY);
	rpc_msgdenied = txdr_unsigned(RPC_MSGDENIED);
	rpc_msgaccepted = txdr_unsigned(RPC_MSGACCEPTED);
	rpc_mismatch = txdr_unsigned(RPC_MISMATCH);
	rpc_autherr = txdr_unsigned(RPC_AUTHERR);
	rpc_auth_unix = txdr_unsigned(RPCAUTH_UNIX);
	nfs_prog = txdr_unsigned(NFS_PROG);
	nfs_true = txdr_unsigned(1);
	nfs_false = txdr_unsigned(0);
	nfs_xdrneg1 = txdr_unsigned(-1);
	nfs_ticks = (hz * NFS_TICKINTVL + 500) / 1000;
	if (nfs_ticks < 1)
		nfs_ticks = 1;
#ifdef NFSSERVER
	nfsrv_init(0);			/* Init server data structures */
	nfsrv_initcache();		/* Init the server request cache */
#endif /* NFSSERVER */

	pool_init(&nfsreqpl, sizeof(struct nfsreq), 0, IPL_NONE, PR_WAITOK,
	    "nfsreqpl", NULL);
}

#ifdef NFSCLIENT
int
nfs_vfs_init(struct vfsconf *vfsp)
{
	extern struct pool nfs_node_pool;

	TAILQ_INIT(&nfs_bufq);

	pool_init(&nfs_node_pool, sizeof(struct nfsnode), 0, IPL_NONE,
		  PR_WAITOK, "nfsnodepl", NULL);

	return (0);
}

/*
 * Attribute cache routines.
 * nfs_loadattrcache() - loads or updates the cache contents from attributes
 *	that are on the mbuf list
 * nfs_getattrcache() - returns valid attributes if found in cache, returns
 *	error otherwise
 */

/*
 * Load the attribute cache (that lives in the nfsnode entry) with
 * the values on the mbuf list and
 * Iff vap not NULL
 *    copy the attributes to *vaper
 */
int
nfs_loadattrcache(struct vnode **vpp, struct mbuf **mdp, caddr_t *dposp,
    struct vattr *vaper)
{
	struct vnode *vp = *vpp;
	struct vattr *vap;
	struct nfs_fattr *fp;
	extern const struct vops nfs_specvops;
	struct nfsnode *np;
	int32_t avail;
	int error = 0;
	int32_t rdev;
	struct mbuf *md;
	enum vtype vtyp;
	mode_t vmode;
	struct timespec mtime;
	struct vnode *nvp;
	int v3 = NFS_ISV3(vp);
	uid_t uid;
	gid_t gid;

	md = *mdp;
	avail = (mtod(md, caddr_t) + md->m_len) - *dposp;
	error = nfsm_disct(mdp, dposp, NFSX_FATTR(v3), avail, (caddr_t *)&fp);
	if (error)
		return (error);
	if (v3) {
		vtyp = nfsv3tov_type(fp->fa_type);
		vmode = fxdr_unsigned(mode_t, fp->fa_mode);
		rdev = makedev(fxdr_unsigned(u_int32_t, fp->fa3_rdev.specdata1),
			fxdr_unsigned(u_int32_t, fp->fa3_rdev.specdata2));
		fxdr_nfsv3time(&fp->fa3_mtime, &mtime);
	} else {
		vtyp = nfsv2tov_type(fp->fa_type);
		vmode = fxdr_unsigned(mode_t, fp->fa_mode);
		if (vtyp == VNON || vtyp == VREG)
			vtyp = IFTOVT(vmode);
		rdev = fxdr_unsigned(int32_t, fp->fa2_rdev);
		fxdr_nfsv2time(&fp->fa2_mtime, &mtime);

		/*
		 * Really ugly NFSv2 kludge.
		 */
		if (vtyp == VCHR && rdev == 0xffffffff)
			vtyp = VFIFO;
	}

	/*
	 * If v_type == VNON it is a new node, so fill in the v_type,
	 * n_mtime fields. Check to see if it represents a special 
	 * device, and if so, check for a possible alias. Once the
	 * correct vnode has been obtained, fill in the rest of the
	 * information.
	 */
	np = VTONFS(vp);
	if (vp->v_type != vtyp) {
		cache_purge(vp);
		vp->v_type = vtyp;
		if (vp->v_type == VFIFO) {
#ifndef FIFO
			return (EOPNOTSUPP);
#else
                        extern const struct vops nfs_fifovops;
			vp->v_op = &nfs_fifovops;
#endif /* FIFO */
		}
		if (vp->v_type == VCHR || vp->v_type == VBLK) {
			vp->v_op = &nfs_specvops;
			nvp = checkalias(vp, (dev_t)rdev, vp->v_mount);
			if (nvp) {
				/*
				 * Discard unneeded vnode, but save its nfsnode.
				 * Since the nfsnode does not have a lock, its
				 * vnode lock has to be carried over.
				 */

				nvp->v_data = vp->v_data;
				vp->v_data = NULL;
				vp->v_op = &spec_vops;
				vrele(vp);
				vgone(vp);
				/*
				 * Reinitialize aliased node.
				 */
				np->n_vnode = nvp;
				*vpp = vp = nvp;
			}
		}
		np->n_mtime = mtime;
	}
	vap = &np->n_vattr;
	vap->va_type = vtyp;
	vap->va_rdev = (dev_t)rdev;
	vap->va_mtime = mtime;
	vap->va_fsid = vp->v_mount->mnt_stat.f_fsid.val[0];

	uid = fxdr_unsigned(uid_t, fp->fa_uid);
	gid = fxdr_unsigned(gid_t, fp->fa_gid);
	/* Invalidate access cache if uid, gid or mode changed. */
	if (np->n_accstamp != -1 &&
	    (gid != vap->va_gid || uid != vap->va_uid ||
	    (vmode & 07777) != vap->va_mode))
		np->n_accstamp = -1;

	vap->va_mode = (vmode & 07777);

	switch (vtyp) {
	case VBLK:
		vap->va_blocksize = BLKDEV_IOSIZE;
		break;
	case VCHR:
		vap->va_blocksize = MAXBSIZE;
		break;
	default:
		vap->va_blocksize = v3 ? vp->v_mount->mnt_stat.f_iosize :
		     fxdr_unsigned(int32_t, fp->fa2_blocksize);
		break;
	}
	vap->va_nlink = fxdr_unsigned(nlink_t, fp->fa_nlink);
	vap->va_uid = fxdr_unsigned(uid_t, fp->fa_uid);
	vap->va_gid = fxdr_unsigned(gid_t, fp->fa_gid);
	if (v3) {
		vap->va_size = fxdr_hyper(&fp->fa3_size);
		vap->va_bytes = fxdr_hyper(&fp->fa3_used);
		vap->va_fileid = fxdr_hyper(&fp->fa3_fileid);
		fxdr_nfsv3time(&fp->fa3_atime, &vap->va_atime);
		fxdr_nfsv3time(&fp->fa3_ctime, &vap->va_ctime);
	} else {
		vap->va_size = fxdr_unsigned(u_int32_t, fp->fa2_size);
		vap->va_bytes =
		    (u_quad_t)fxdr_unsigned(int32_t, fp->fa2_blocks) *
		    NFS_FABLKSIZE;
		vap->va_fileid = fxdr_unsigned(int32_t, fp->fa2_fileid);
		fxdr_nfsv2time(&fp->fa2_atime, &vap->va_atime);
		vap->va_ctime.tv_sec = fxdr_unsigned(u_int32_t,
		    fp->fa2_ctime.nfsv2_sec);
		vap->va_ctime.tv_nsec = 0;
		vap->va_gen = fxdr_unsigned(u_int32_t,fp->fa2_ctime.nfsv2_usec);
	}
	vap->va_flags = 0;
	vap->va_filerev = 0;

	if (vap->va_size != np->n_size) {
		if (vap->va_type == VREG) {
			if (np->n_flag & NMODIFIED) {
				if (vap->va_size < np->n_size)
					vap->va_size = np->n_size;
				else
					np->n_size = vap->va_size;
			} else
				np->n_size = vap->va_size;
			uvm_vnp_setsize(vp, np->n_size);
		} else
			np->n_size = vap->va_size;
	}
	np->n_attrstamp = gettime();
	if (vaper != NULL) {
		bcopy(vap, vaper, sizeof(*vap));
		if (np->n_flag & NCHG) {
			if (np->n_flag & NACC)
				vaper->va_atime = np->n_atim;
			if (np->n_flag & NUPD)
				vaper->va_mtime = np->n_mtim;
		}
	}
	return (0);
}

int
nfs_attrtimeo(struct nfsnode *np)
{
	struct vnode *vp = np->n_vnode;
	struct nfsmount *nmp = VFSTONFS(vp->v_mount);
	int tenthage = (gettime() - np->n_mtime.tv_sec) / 10;
	int minto, maxto;

	if (vp->v_type == VDIR) {
		maxto = nmp->nm_acdirmax;
		minto = nmp->nm_acdirmin;
	} else {
		maxto = nmp->nm_acregmax;
		minto = nmp->nm_acregmin;
	}

	if (np->n_flag & NMODIFIED || tenthage < minto)
		return minto;
	else if (tenthage < maxto)
		return tenthage;
	else
		return maxto;
}

/*
 * Check the time stamp
 * If the cache is valid, copy contents to *vap and return 0
 * otherwise return an error
 */
int
nfs_getattrcache(struct vnode *vp, struct vattr *vaper)
{
	struct nfsnode *np = VTONFS(vp);
	struct vattr *vap;

	if (np->n_attrstamp == 0 ||
	    (gettime() - np->n_attrstamp) >= nfs_attrtimeo(np)) {
		nfsstats.attrcache_misses++;
		return (ENOENT);
	}
	nfsstats.attrcache_hits++;
	vap = &np->n_vattr;
	if (vap->va_size != np->n_size) {
		if (vap->va_type == VREG) {
			if (np->n_flag & NMODIFIED) {
				if (vap->va_size < np->n_size)
					vap->va_size = np->n_size;
				else
					np->n_size = vap->va_size;
			} else
				np->n_size = vap->va_size;
			uvm_vnp_setsize(vp, np->n_size);
		} else
			np->n_size = vap->va_size;
	}
	bcopy(vap, vaper, sizeof(struct vattr));
	if (np->n_flag & NCHG) {
		if (np->n_flag & NACC)
			vaper->va_atime = np->n_atim;
		if (np->n_flag & NUPD)
			vaper->va_mtime = np->n_mtim;
	}
	return (0);
}
#endif /* NFSCLIENT */

/*
 * The write verifier has changed (probably due to a server reboot), so all
 * B_NEEDCOMMIT blocks will have to be written again. Since they are on the
 * dirty block list as B_DELWRI, all this takes is clearing the B_NEEDCOMMIT
 * flag. Once done the new write verifier can be set for the mount point.
 */
void
nfs_clearcommit(struct mount *mp)
{
	struct vnode *vp;
	struct buf *bp;
	int s;

	s = splbio();
loop:
	TAILQ_FOREACH(vp, &mp->mnt_vnodelist, v_mntvnodes) {
		if (vp->v_mount != mp)	/* Paranoia */
			goto loop;
		LIST_FOREACH(bp, &vp->v_dirtyblkhd, b_vnbufs) {
			if ((bp->b_flags & (B_BUSY | B_DELWRI | B_NEEDCOMMIT))
			    == (B_DELWRI | B_NEEDCOMMIT))
				bp->b_flags &= ~B_NEEDCOMMIT;
		}
	}
	splx(s);
}

void
nfs_merge_commit_ranges(struct vnode *vp)
{
	struct nfsnode *np = VTONFS(vp);

	if (!(np->n_commitflags & NFS_COMMIT_PUSHED_VALID)) {
		np->n_pushedlo = np->n_pushlo;
		np->n_pushedhi = np->n_pushhi;
		np->n_commitflags |= NFS_COMMIT_PUSHED_VALID;
	} else {
		if (np->n_pushlo < np->n_pushedlo)
			np->n_pushedlo = np->n_pushlo;
		if (np->n_pushhi > np->n_pushedhi)
			np->n_pushedhi = np->n_pushhi;
	}

	np->n_pushlo = np->n_pushhi = 0;
	np->n_commitflags &= ~NFS_COMMIT_PUSH_VALID;
}

int
nfs_in_committed_range(struct vnode *vp, struct buf *bp)
{
	struct nfsnode *np = VTONFS(vp);
	off_t lo, hi;

	if (!(np->n_commitflags & NFS_COMMIT_PUSHED_VALID))
		return 0;
	lo = (off_t)bp->b_blkno * DEV_BSIZE;
	hi = lo + bp->b_dirtyend;

	return (lo >= np->n_pushedlo && hi <= np->n_pushedhi);
}

int
nfs_in_tobecommitted_range(struct vnode *vp, struct buf *bp)
{
	struct nfsnode *np = VTONFS(vp);
	off_t lo, hi;

	if (!(np->n_commitflags & NFS_COMMIT_PUSH_VALID))
		return 0;
	lo = (off_t)bp->b_blkno * DEV_BSIZE;
	hi = lo + bp->b_dirtyend;

	return (lo >= np->n_pushlo && hi <= np->n_pushhi);
}

void
nfs_add_committed_range(struct vnode *vp, struct buf *bp)
{
	struct nfsnode *np = VTONFS(vp);
	off_t lo, hi;

	lo = (off_t)bp->b_blkno * DEV_BSIZE;
	hi = lo + bp->b_dirtyend;

	if (!(np->n_commitflags & NFS_COMMIT_PUSHED_VALID)) {
		np->n_pushedlo = lo;
		np->n_pushedhi = hi;
		np->n_commitflags |= NFS_COMMIT_PUSHED_VALID;
	} else {
		if (hi > np->n_pushedhi)
			np->n_pushedhi = hi;
		if (lo < np->n_pushedlo)
			np->n_pushedlo = lo;
	}
}

void
nfs_del_committed_range(struct vnode *vp, struct buf *bp)
{
	struct nfsnode *np = VTONFS(vp);
	off_t lo, hi;

	if (!(np->n_commitflags & NFS_COMMIT_PUSHED_VALID))
		return;

	lo = (off_t)bp->b_blkno * DEV_BSIZE;
	hi = lo + bp->b_dirtyend;

	if (lo > np->n_pushedhi || hi < np->n_pushedlo)
		return;
	if (lo <= np->n_pushedlo)
		np->n_pushedlo = hi;
	else if (hi >= np->n_pushedhi)
		np->n_pushedhi = lo;
	else {
		/*
		 * XXX There's only one range. If the deleted range
		 * is in the middle, pick the largest of the
		 * contiguous ranges that it leaves.
		 */
		if ((np->n_pushedlo - lo) > (hi - np->n_pushedhi))
			np->n_pushedhi = lo;
		else
			np->n_pushedlo = hi;
	}
}

void
nfs_add_tobecommitted_range(struct vnode *vp, struct buf *bp)
{
	struct nfsnode *np = VTONFS(vp);
	off_t lo, hi;

	lo = (off_t)bp->b_blkno * DEV_BSIZE;
	hi = lo + bp->b_dirtyend;

	if (!(np->n_commitflags & NFS_COMMIT_PUSH_VALID)) {
		np->n_pushlo = lo;
		np->n_pushhi = hi;
		np->n_commitflags |= NFS_COMMIT_PUSH_VALID;
	} else {
		if (lo < np->n_pushlo)
			np->n_pushlo = lo;
		if (hi > np->n_pushhi)
			np->n_pushhi = hi;
	}
}

void
nfs_del_tobecommitted_range(struct vnode *vp, struct buf *bp)
{
	struct nfsnode *np = VTONFS(vp);
	off_t lo, hi;

	if (!(np->n_commitflags & NFS_COMMIT_PUSH_VALID))
		return;

	lo = (off_t)bp->b_blkno * DEV_BSIZE;
	hi = lo + bp->b_dirtyend;

	if (lo > np->n_pushhi || hi < np->n_pushlo)
		return;

	if (lo <= np->n_pushlo)
		np->n_pushlo = hi;
	else if (hi >= np->n_pushhi)
		np->n_pushhi = lo;
	else {
		/*
		 * XXX There's only one range. If the deleted range
		 * is in the middle, pick the largest of the
		 * contiguous ranges that it leaves.
		 */
		if ((np->n_pushlo - lo) > (hi - np->n_pushhi))
			np->n_pushhi = lo;
		else
			np->n_pushlo = hi;
	}
}

/*
 * Map errnos to NFS error numbers. For Version 3 also filter out error
 * numbers not specified for the associated procedure.
 */
int
nfsrv_errmap(struct nfsrv_descript *nd, int err)
{
	const short *defaulterrp, *errp;

	if (nd->nd_flag & ND_NFSV3) {
	    if (nd->nd_procnum <= NFSPROC_COMMIT) {
		errp = defaulterrp = nfsrv_v3errmap[nd->nd_procnum];
		while (*++errp) {
			if (*errp == err)
				return (err);
			else if (*errp > err)
				break;
		}
		return ((int)*defaulterrp);
	    } else
		return (err & 0xffff);
	}
	if (err <= nitems(nfsrv_v2errmap))
		return ((int)nfsrv_v2errmap[err - 1]);
	return (NFSERR_IO);
}

/*
 * If full is non zero, set all fields, otherwise just set mode and time fields
 */
void
nfsm_v3attrbuild(struct mbuf **mp, struct vattr *a, int full)
{
	struct mbuf *mb;
	u_int32_t *tl;

	mb = *mp;

	if (a->va_mode != (mode_t)VNOVAL) {
		tl = nfsm_build(&mb, 2 * NFSX_UNSIGNED);
		*tl++ = nfs_true;
		*tl = txdr_unsigned(a->va_mode);
	} else {
		tl = nfsm_build(&mb, NFSX_UNSIGNED);
		*tl = nfs_false;
	}
	if (full && a->va_uid != (uid_t)VNOVAL) {
		tl = nfsm_build(&mb, 2 * NFSX_UNSIGNED);
		*tl++ = nfs_true;
		*tl = txdr_unsigned(a->va_uid);
	} else {
		tl = nfsm_build(&mb, NFSX_UNSIGNED);
		*tl = nfs_false;
	}
	if (full && a->va_gid != (gid_t)VNOVAL) {
		tl = nfsm_build(&mb, 2 * NFSX_UNSIGNED);
		*tl++ = nfs_true;
		*tl = txdr_unsigned((a)->va_gid);
	} else {
		tl = nfsm_build(&mb, NFSX_UNSIGNED);
		*tl = nfs_false;
	}
	if (full && a->va_size != VNOVAL) {
		tl = nfsm_build(&mb, 3 * NFSX_UNSIGNED);
		*tl++ = nfs_true;
		txdr_hyper(a->va_size, tl);
	} else {
		tl = nfsm_build(&mb, NFSX_UNSIGNED);
		*tl = nfs_false;
	}
	if (a->va_atime.tv_nsec != VNOVAL) {
		if (a->va_atime.tv_sec != gettime()) {
			tl = nfsm_build(&mb, 3 * NFSX_UNSIGNED);
			*tl++ = txdr_unsigned(NFSV3SATTRTIME_TOCLIENT);
			txdr_nfsv3time(&a->va_atime, tl);
		} else {
			tl = nfsm_build(&mb, NFSX_UNSIGNED);
			*tl = txdr_unsigned(NFSV3SATTRTIME_TOSERVER);
		}
	} else {
		tl = nfsm_build(&mb, NFSX_UNSIGNED);
		*tl = txdr_unsigned(NFSV3SATTRTIME_DONTCHANGE);
	}
	if (a->va_mtime.tv_nsec != VNOVAL) {
		if (a->va_mtime.tv_sec != gettime()) {
			tl = nfsm_build(&mb, 3 * NFSX_UNSIGNED);
			*tl++ = txdr_unsigned(NFSV3SATTRTIME_TOCLIENT);
			txdr_nfsv3time(&a->va_mtime, tl);
		} else {
			tl = nfsm_build(&mb, NFSX_UNSIGNED);
			*tl = txdr_unsigned(NFSV3SATTRTIME_TOSERVER);
		}
	} else {
		tl = nfsm_build(&mb, NFSX_UNSIGNED);
		*tl = txdr_unsigned(NFSV3SATTRTIME_DONTCHANGE);
	}

	*mp = mb;
}

/*
 * Ensure a contiguous buffer len bytes long
 */
void *
nfsm_build(struct mbuf **mp, u_int len)
{
	struct mbuf *mb, *mb2;
	caddr_t bpos;

	mb = *mp;
	bpos = mb_offset(mb);

	if (len > m_trailingspace(mb)) {
		MGET(mb2, M_WAIT, MT_DATA);
		if (len > MLEN)
			panic("build > MLEN");
		mb->m_next = mb2;
		mb = mb2;
		mb->m_len = 0;
		bpos = mtod(mb, caddr_t);
	}
	mb->m_len += len;

	*mp = mb;

	return (bpos);
}

void
nfsm_fhtom(struct nfsm_info *info, struct vnode *v, int v3)
{
	struct nfsnode *n = VTONFS(v);

	if (v3) {
		nfsm_strtombuf(&info->nmi_mb, n->n_fhp, n->n_fhsize);
	} else {
		nfsm_buftombuf(&info->nmi_mb, n->n_fhp, NFSX_V2FH);
	}
}

void
nfsm_srvfhtom(struct mbuf **mp, fhandle_t *f, int v3)
{
	if (v3) {
		nfsm_strtombuf(mp, f, NFSX_V3FH);
	} else {
		nfsm_buftombuf(mp, f, NFSX_V2FH);
	}
}

void
txdr_nfsv2time(const struct timespec *from, struct nfsv2_time *to)
{
	if (from->tv_nsec == VNOVAL) {
		to->nfsv2_sec = nfs_xdrneg1;
		to->nfsv2_usec = nfs_xdrneg1;
	} else if (from->tv_sec == -1) {
		/*
		 * can't request a time of -1; send
		 * -1.000001 == {-2,999999} instead
		 */
		to->nfsv2_sec = htonl(-2);
		to->nfsv2_usec = htonl(999999);
	} else {
		to->nfsv2_sec = htonl(from->tv_sec);
		to->nfsv2_usec = htonl(from->tv_nsec / 1000);
	}
}
