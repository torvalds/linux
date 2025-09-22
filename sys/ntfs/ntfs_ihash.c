/*	$OpenBSD: ntfs_ihash.c,v 1.22 2025/01/13 13:58:41 claudio Exp $	*/
/*	$NetBSD: ntfs_ihash.c,v 1.1 2002/12/23 17:38:32 jdolecek Exp $	*/

/*
 * Copyright (c) 1982, 1986, 1989, 1991, 1993, 1995
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
 *	@(#)ufs_ihash.c	8.7 (Berkeley) 5/17/95
 * Id: ntfs_ihash.c,v 1.5 1999/05/12 09:42:58 semenu Exp
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/rwlock.h>
#include <sys/vnode.h>
#include <sys/malloc.h>
#include <sys/mount.h>

#include <crypto/siphash.h>

#include <ntfs/ntfs.h>
#include <ntfs/ntfs_inode.h>
#include <ntfs/ntfs_ihash.h>

/*
 * Structures associated with inode caching.
 */
u_int ntfs_hash(dev_t, ntfsino_t);
static LIST_HEAD(nthashhead, ntnode) *ntfs_nthashtbl;
static SIPHASH_KEY ntfs_nthashkey;
static u_long	ntfs_nthash;		/* size of hash table - 1 */
#define	NTNOHASH(device, inum) ntfs_hash((device), (inum))

/*
 * Initialize inode hash table.
 */
void
ntfs_nthashinit(void)
{
	u_long nthash;
	void *nthashtbl;

	if (ntfs_nthashtbl)
		return;

	nthashtbl = hashinit(initialvnodes, M_NTFSNTHASH, M_WAITOK, &nthash);
	if (ntfs_nthashtbl) {
		hashfree(nthashtbl, initialvnodes, M_NTFSNTHASH);
		return;
	}
	ntfs_nthashtbl = nthashtbl;
	ntfs_nthash = nthash;

	arc4random_buf(&ntfs_nthashkey, sizeof(ntfs_nthashkey));
}

u_int
ntfs_hash(dev_t dev, ntfsino_t inum)
{
	SIPHASH_CTX ctx;

	SipHash24_Init(&ctx, &ntfs_nthashkey);
	SipHash24_Update(&ctx, &dev, sizeof(dev));
	SipHash24_Update(&ctx, &inum, sizeof(inum));

	return (SipHash24_End(&ctx) & ntfs_nthash);
}

/*
 * Use the device/inum pair to find the incore inode, and return a pointer
 * to it. If it is in core, return it, even if it is locked.
 */
struct ntnode *
ntfs_nthashlookup(dev_t dev, ntfsino_t inum)
{
	struct ntnode *ip;
	struct nthashhead *ipp;

	/* XXXLOCKING lock hash list? */
	ipp = &ntfs_nthashtbl[NTNOHASH(dev, inum)];
	LIST_FOREACH(ip, ipp, i_hash) {
		if (inum == ip->i_number && dev == ip->i_dev)
			break;
	}
	/* XXXLOCKING unlock hash list? */

	return (ip);
}

/*
 * Insert the ntnode into the hash table.
 */
int
ntfs_nthashins(struct ntnode *ip)
{
	struct nthashhead *ipp;
	struct ntnode *curip;

	/* XXXLOCKING lock hash list? */
	ipp = &ntfs_nthashtbl[NTNOHASH(ip->i_dev, ip->i_number)];
	LIST_FOREACH(curip, ipp, i_hash) {
		if (ip->i_number == curip->i_number &&
		    ip->i_dev == curip->i_dev)
			return (EEXIST);
	}

	ip->i_flag |= IN_HASHED;
	LIST_INSERT_HEAD(ipp, ip, i_hash);
	/* XXXLOCKING unlock hash list? */

	return (0);
}

/*
 * Remove the inode from the hash table.
 */
void
ntfs_nthashrem(struct ntnode *ip)
{
	/* XXXLOCKING lock hash list? */
	if (ip->i_flag & IN_HASHED) {
		ip->i_flag &= ~IN_HASHED;
		LIST_REMOVE(ip, i_hash);
	}
	/* XXXLOCKING unlock hash list? */
}
