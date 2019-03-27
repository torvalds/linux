/*-
 * Copyright (c) 2007 Pawel Jakub Dawidek <pjd@FreeBSD.org>
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
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/pathname.h>
#include <sys/vfs.h>
#include <sys/vnode.h>

int
lookupname(char *dirname, enum uio_seg seg, enum symfollow follow,
    vnode_t **dirvpp, vnode_t **compvpp)
{

	return (lookupnameat(dirname, seg, follow, dirvpp, compvpp, NULL));
}

int
lookupnameat(char *dirname, enum uio_seg seg, enum symfollow follow,
    vnode_t **dirvpp, vnode_t **compvpp, vnode_t *startvp)
{
	struct nameidata nd;
	int error, ltype;

	ASSERT(dirvpp == NULL);

	vref(startvp);
	ltype = VOP_ISLOCKED(startvp);
	VOP_UNLOCK(startvp, 0);
	NDINIT_ATVP(&nd, LOOKUP, LOCKLEAF | follow, seg, dirname,
	    startvp, curthread);
	error = namei(&nd);
	*compvpp = nd.ni_vp;
	NDFREE(&nd, NDF_ONLY_PNBUF);
	vn_lock(startvp, ltype | LK_RETRY);
	return (error);
}
