/*	$OpenBSD: mount_xdr.c,v 1.5 2014/10/20 02:33:42 guenther Exp $	*/

/*
 * Copyright (c) 1989 Jan-Simon Pendry
 * Copyright (c) 1989 Imperial College of Science, Technology & Medicine
 * Copyright (c) 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Jan-Simon Pendry at Imperial College, London.
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
 *	from: @(#)mount_xdr.c	8.1 (Berkeley) 6/6/93
 */

#include "am.h"
#include "mount.h"


#include <nfs/rpcv2.h>

int
xdr_fhstatus(XDR *xdrsp, fhstatus *objp)
{
	int i;
	long auth, authcnt, authfnd = 0;


	if (!xdr_u_long(xdrsp, &objp->fhs_stat))
		return (0);
	if (objp->fhs_stat)
		return (1);
	switch (objp->fhs_vers) {
	case 1:
		objp->fhs_size = NFSX_V2FH;
		return (xdr_opaque(xdrsp, (caddr_t)objp->fhs_fhandle, NFSX_V2FH));
	case 3:
		if (!xdr_long(xdrsp, &objp->fhs_size))
			return (0);
		if (objp->fhs_size <= 0 || objp->fhs_size > NFSX_V3FHMAX)
			return (0);
		if (!xdr_opaque(xdrsp, (caddr_t)objp->fhs_fhandle, objp->fhs_size))
			return (0);
		if (!xdr_long(xdrsp, &authcnt))
			return (0);
		for (i = 0; i < authcnt; i++) {
			if (!xdr_long(xdrsp, &auth))
				return (0);
			if (auth == objp->fhs_auth)
				authfnd++;
		}
		/*
		 * Some servers, such as DEC's OSF/1 return a nil authenticator
		 * list to indicate RPCAUTH_UNIX.
		 */
		if (!authfnd && (authcnt > 0 || objp->fhs_auth != RPCAUTH_UNIX))
			objp->fhs_stat = EAUTH;
		return (1);
	default:
		return (0);
	};
}

bool_t
xdr_dirpath(XDR *xdrs, dirpath *objp)
{
	if (!xdr_string(xdrs, objp, MNTPATHLEN)) {
		return (FALSE);
	}
	return (TRUE);
}

bool_t
xdr_name(XDR *xdrs, name *objp)
{
	if (!xdr_string(xdrs, objp, MNTNAMLEN)) {
		return (FALSE);
	}
	return (TRUE);
}

bool_t
xdr_mountlist(XDR *xdrs, mountlist *objp)
{
	if (!xdr_pointer(xdrs, (char **)objp, sizeof(struct mountbody), xdr_mountbody)) {
		return (FALSE);
	}
	return (TRUE);
}

bool_t
xdr_mountbody(XDR *xdrs, mountbody *objp)
{
	if (!xdr_name(xdrs, &objp->ml_hostname)) {
		return (FALSE);
	}
	if (!xdr_dirpath(xdrs, &objp->ml_directory)) {
		return (FALSE);
	}
	if (!xdr_mountlist(xdrs, &objp->ml_next)) {
		return (FALSE);
	}
	return (TRUE);
}

bool_t
xdr_groups(XDR *xdrs, groups *objp)
{
	if (!xdr_pointer(xdrs, (char **)objp, sizeof(struct groupnode), xdr_groupnode)) {
		return (FALSE);
	}
	return (TRUE);
}

bool_t
xdr_groupnode(XDR *xdrs, groupnode *objp)
{
	if (!xdr_name(xdrs, &objp->gr_name)) {
		return (FALSE);
	}
	if (!xdr_groups(xdrs, &objp->gr_next)) {
		return (FALSE);
	}
	return (TRUE);
}

bool_t
xdr_exports(XDR *xdrs, exports *objp)
{
	if (!xdr_pointer(xdrs, (char **)objp, sizeof(struct exportnode), xdr_exportnode)) {
		return (FALSE);
	}
	return (TRUE);
}

bool_t
xdr_exportnode(XDR *xdrs, exportnode *objp)
{
	if (!xdr_dirpath(xdrs, &objp->ex_dir)) {
		return (FALSE);
	}
	if (!xdr_groups(xdrs, &objp->ex_groups)) {
		return (FALSE);
	}
	if (!xdr_exports(xdrs, &objp->ex_next)) {
		return (FALSE);
	}
	return (TRUE);
}
