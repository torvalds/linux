/*	$OpenBSD: fuse_ihash.c,v 1.1 2024/10/31 13:55:21 claudio Exp $	*/
/*	$NetBSD: ufs_ihash.c,v 1.3 1996/02/09 22:36:04 christos Exp $	*/

/*
 * Copyright (c) 1982, 1986, 1989, 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
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
 *	@(#)ufs_ihash.c	8.4 (Berkeley) 12/30/93
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/vnode.h>
#include <sys/malloc.h>
#include <sys/mount.h>
#include <sys/lock.h>

#include "fusefs_node.h"

#include <crypto/siphash.h>

/*
 * Structures associated with inode caching.
 */
LIST_HEAD(fuse_ihashhead, fusefs_node) *fuse_ihashtbl;
u_long	fuse_ihashsz;		/* size of hash table - 1 */
SIPHASH_KEY fuse_ihashkey;

struct fuse_ihashhead *fuse_ihash(dev_t, ino_t);

struct fuse_ihashhead *
fuse_ihash(dev_t dev, ino_t inum)
{
	SIPHASH_CTX ctx;

	SipHash24_Init(&ctx, &fuse_ihashkey);
	SipHash24_Update(&ctx, &dev, sizeof(dev));
	SipHash24_Update(&ctx, &inum, sizeof(inum));

	return (&fuse_ihashtbl[SipHash24_End(&ctx) & fuse_ihashsz]);
}

/*
 * Initialize inode hash table.
 */
void
fuse_ihashinit(void)
{
	fuse_ihashtbl = hashinit(initialvnodes, M_FUSEFS, M_WAITOK,
	    &fuse_ihashsz);
	arc4random_buf(&fuse_ihashkey, sizeof(fuse_ihashkey));
}

/*
 * Use the device/inum pair to find the incore inode, and return a pointer
 * to it. If it is in core, but locked, wait for it.
 */
struct vnode *
fuse_ihashget(dev_t dev, ino_t inum)
{
	struct fuse_ihashhead *ipp;
	struct fusefs_node *ip;
	struct vnode *vp;
loop:
	/* XXXLOCKING lock hash list */
	ipp = fuse_ihash(dev, inum);
	LIST_FOREACH(ip, ipp, i_hash) {
		if (inum == ip->i_number && dev == ip->i_dev) {
			vp = ITOV(ip);
			/* XXXLOCKING unlock hash list? */
			if (vget(vp, LK_EXCLUSIVE))
				goto loop;

			return (vp);
		}
	}
	/* XXXLOCKING unlock hash list? */
	return (NULL);
}

/*
 * Insert the inode into the hash table, and return it locked.
 */
int
fuse_ihashins(struct fusefs_node *ip)
{
	struct   fusefs_node *curip;
	struct   fuse_ihashhead *ipp;
	dev_t    dev = ip->i_dev;
	ino_t	 inum = ip->i_number;

	/* lock the inode, then put it on the appropriate hash list */
	VOP_LOCK(ITOV(ip), LK_EXCLUSIVE);

	/* XXXLOCKING lock hash list */

	ipp = fuse_ihash(dev, inum);
	LIST_FOREACH(curip, ipp, i_hash) {
		if (inum == curip->i_number && dev == curip->i_dev) {
			/* XXXLOCKING unlock hash list? */
			VOP_UNLOCK(ITOV(ip));
			return (EEXIST);
		}
	}

	LIST_INSERT_HEAD(ipp, ip, i_hash);
	/* XXXLOCKING unlock hash list? */

	return (0);
}

/*
 * Remove the inode from the hash table.
 */
void
fuse_ihashrem(struct fusefs_node *ip)
{
	/* XXXLOCKING lock hash list */

	if (ip->i_hash.le_prev == NULL)
		return;
	LIST_REMOVE(ip, i_hash);
#ifdef DIAGNOSTIC
	ip->i_hash.le_next = NULL;
	ip->i_hash.le_prev = NULL;
#endif
	/* XXXLOCKING unlock hash list? */
}
