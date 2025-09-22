/*	$OpenBSD: tmpfs_mem.c,v 1.8 2015/12/11 22:34:34 beck Exp $	*/
/*	$NetBSD: tmpfs_mem.c,v 1.4 2011/05/24 01:09:47 rmind Exp $	*/

/*
 * Copyright (c) 2010, 2011 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Mindaugas Rasiukevicius.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * tmpfs memory allocation routines.
 * Implements memory usage accounting and limiting.
 */

#include <sys/param.h>
#include <sys/namei.h>
#include <sys/pool.h>
#include <sys/vnode.h>
#include <sys/malloc.h>

#include <tmpfs/tmpfs.h>

extern struct pool	tmpfs_dirent_pool;
extern struct pool	tmpfs_node_pool;

void
tmpfs_mntmem_init(struct tmpfs_mount *mp, uint64_t memlimit)
{

	rw_init(&mp->tm_acc_lock, "tacclk");
	mp->tm_mem_limit = memlimit;
	mp->tm_bytes_used = 0;
}

void
tmpfs_mntmem_destroy(struct tmpfs_mount *mp)
{

	KASSERT(mp->tm_bytes_used == 0);
	/* mutex_destroy(&mp->tm_acc_lock); */
}

/*
 * tmpfs_mem_info: return the number of available memory pages.
 *
 * => If 'total' is true, then return _total_ amount of pages.
 * => If false, then return the amount of _free_ memory pages.
 *
 * Remember to remove TMPFS_PAGES_RESERVED from the returned value to avoid
 * excessive memory usage.
 */
size_t
tmpfs_mem_info(int total)
{
	int size = 0;

	/* XXX: unlocked */
	size += uvmexp.swpages;
	if (!total) {
		size -= uvmexp.swpgonly;
	}

	size += uvmexp.free;
	/* size += uvmexp.filepages; */
	if (size > uvmexp.wired) {
		size -= uvmexp.wired;
	} else {
		size = 0;
	}

	KASSERT(size >= 0);

	return (size_t)size;
}

uint64_t
tmpfs_bytes_max(struct tmpfs_mount *mp)
{
	size_t freepages = tmpfs_mem_info(0);
	uint64_t avail_mem;

	if (freepages < TMPFS_PAGES_RESERVED) {
		freepages = 0;
	} else {
		freepages -= TMPFS_PAGES_RESERVED;
	}
	avail_mem = round_page(mp->tm_bytes_used) + (freepages << PAGE_SHIFT);
	return MIN(mp->tm_mem_limit, avail_mem);
}

uint64_t
tmpfs_pages_avail(struct tmpfs_mount *mp)
{

	return (tmpfs_bytes_max(mp) - mp->tm_bytes_used) >> PAGE_SHIFT;
}

int
tmpfs_mem_incr(struct tmpfs_mount *mp, size_t sz)
{
	uint64_t lim;

	rw_enter_write(&mp->tm_acc_lock);
	lim = tmpfs_bytes_max(mp);
	if (mp->tm_bytes_used + sz >= lim) {
		rw_exit_write(&mp->tm_acc_lock);
		return 0;
	}
	mp->tm_bytes_used += sz;
	rw_exit_write(&mp->tm_acc_lock);
	return 1;
}

void
tmpfs_mem_decr(struct tmpfs_mount *mp, size_t sz)
{

	rw_enter_write(&mp->tm_acc_lock);
	KASSERT(mp->tm_bytes_used >= sz);
	mp->tm_bytes_used -= sz;
	rw_exit_write(&mp->tm_acc_lock);
}

struct tmpfs_dirent *
tmpfs_dirent_get(struct tmpfs_mount *mp)
{

	if (!tmpfs_mem_incr(mp, sizeof(struct tmpfs_dirent))) {
		return NULL;
	}
	return pool_get(&tmpfs_dirent_pool, PR_ZERO|PR_WAITOK);
}

void
tmpfs_dirent_put(struct tmpfs_mount *mp, struct tmpfs_dirent *de)
{

	tmpfs_mem_decr(mp, sizeof(struct tmpfs_dirent));
	pool_put(&tmpfs_dirent_pool, de);
}

struct tmpfs_node *
tmpfs_node_get(struct tmpfs_mount *mp)
{

	mp->tm_nodes_cnt++;
	if (mp->tm_nodes_cnt > mp->tm_nodes_max) {
		mp->tm_nodes_cnt--;
		return NULL;
	}
	if (!tmpfs_mem_incr(mp, sizeof(struct tmpfs_node))) {
		return NULL;
	}
	return pool_get(&tmpfs_node_pool, PR_WAITOK);
}

void
tmpfs_node_put(struct tmpfs_mount *mp, struct tmpfs_node *tn)
{

	mp->tm_nodes_cnt--;
	tmpfs_mem_decr(mp, sizeof(struct tmpfs_node));
	pool_put(&tmpfs_node_pool, tn);
}

/*
 * Quantum size to round-up the tmpfs names in order to reduce re-allocations.
 */

#define	TMPFS_NAME_QUANTUM	(32)
#define	roundup2(x, y)	(((x)+((y)-1))&(~((y)-1))) /* if y is powers of two */

char *
tmpfs_strname_alloc(struct tmpfs_mount *mp, size_t len)
{
	const size_t sz = roundup2(len, TMPFS_NAME_QUANTUM);

	KASSERT(sz > 0 && sz <= 1024);
	if (!tmpfs_mem_incr(mp, sz)) {
		return NULL;
	}
	return malloc(sz, M_TEMP, M_WAITOK); /* XXX */
}

void
tmpfs_strname_free(struct tmpfs_mount *mp, char *str, size_t len)
{
	const size_t sz = roundup2(len, TMPFS_NAME_QUANTUM);

	KASSERT(sz > 0 && sz <= 1024);
	tmpfs_mem_decr(mp, sz);
	free(str, M_TEMP, sz);
}

int
tmpfs_strname_neqlen(struct componentname *fcnp, struct componentname *tcnp)
{
	const size_t fln = roundup2(fcnp->cn_namelen, TMPFS_NAME_QUANTUM);
	const size_t tln = roundup2(tcnp->cn_namelen, TMPFS_NAME_QUANTUM);

	return (fln != tln) || memcmp(fcnp->cn_nameptr, tcnp->cn_nameptr, fln);
}
