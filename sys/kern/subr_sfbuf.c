/*-
 * Copyright (c) 2014 Gleb Smirnoff <glebius@FreeBSD.org>
 * Copyright (c) 2003, 2005 Alan L. Cox <alc@cs.rice.edu>
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/sf_buf.h>
#include <sys/smp.h>
#include <sys/sysctl.h>

#include <vm/vm.h>
#include <vm/vm_extern.h>
#include <vm/vm_page.h>

#ifndef NSFBUFS
#define	NSFBUFS		(512 + maxusers * 16)
#endif

static int nsfbufs;
static int nsfbufspeak;
static int nsfbufsused;

SYSCTL_INT(_kern_ipc, OID_AUTO, nsfbufs, CTLFLAG_RDTUN, &nsfbufs, 0,
    "Maximum number of sendfile(2) sf_bufs available");
SYSCTL_INT(_kern_ipc, OID_AUTO, nsfbufspeak, CTLFLAG_RD, &nsfbufspeak, 0,
    "Number of sendfile(2) sf_bufs at peak usage");
SYSCTL_INT(_kern_ipc, OID_AUTO, nsfbufsused, CTLFLAG_RD, &nsfbufsused, 0,
    "Number of sendfile(2) sf_bufs in use");

static void	sf_buf_init(void *arg);
SYSINIT(sock_sf, SI_SUB_MBUF, SI_ORDER_ANY, sf_buf_init, NULL);

LIST_HEAD(sf_head, sf_buf);

/*
 * A hash table of active sendfile(2) buffers
 */
static struct sf_head *sf_buf_active;
static u_long sf_buf_hashmask;

#define	SF_BUF_HASH(m)	(((m) - vm_page_array) & sf_buf_hashmask)

static TAILQ_HEAD(, sf_buf) sf_buf_freelist;
static u_int	sf_buf_alloc_want;

/*
 * A lock used to synchronize access to the hash table and free list
 */
static struct mtx sf_buf_lock;

/*
 * Allocate a pool of sf_bufs (sendfile(2) or "super-fast" if you prefer. :-))
 */
static void
sf_buf_init(void *arg)
{
	struct sf_buf *sf_bufs;
	vm_offset_t sf_base;
	int i;

	if (PMAP_HAS_DMAP)
		return;

	nsfbufs = NSFBUFS;
	TUNABLE_INT_FETCH("kern.ipc.nsfbufs", &nsfbufs);

	sf_buf_active = hashinit(nsfbufs, M_TEMP, &sf_buf_hashmask);
	TAILQ_INIT(&sf_buf_freelist);
	sf_base = kva_alloc(nsfbufs * PAGE_SIZE);
	sf_bufs = malloc(nsfbufs * sizeof(struct sf_buf), M_TEMP,
	    M_WAITOK | M_ZERO);
	for (i = 0; i < nsfbufs; i++) {
		sf_bufs[i].kva = sf_base + i * PAGE_SIZE;
		TAILQ_INSERT_TAIL(&sf_buf_freelist, &sf_bufs[i], free_entry);
	}
	sf_buf_alloc_want = 0;
	mtx_init(&sf_buf_lock, "sf_buf", NULL, MTX_DEF);
}

/*
 * Get an sf_buf from the freelist.  May block if none are available.
 */
struct sf_buf *
sf_buf_alloc(struct vm_page *m, int flags)
{
	struct sf_head *hash_list;
	struct sf_buf *sf;
	int error;

	if (PMAP_HAS_DMAP)
		return ((struct sf_buf *)m);

	KASSERT(curthread->td_pinned > 0 || (flags & SFB_CPUPRIVATE) == 0,
	    ("sf_buf_alloc(SFB_CPUPRIVATE): curthread not pinned"));
	hash_list = &sf_buf_active[SF_BUF_HASH(m)];
	mtx_lock(&sf_buf_lock);
	LIST_FOREACH(sf, hash_list, list_entry) {
		if (sf->m == m) {
			sf->ref_count++;
			if (sf->ref_count == 1) {
				TAILQ_REMOVE(&sf_buf_freelist, sf, free_entry);
				nsfbufsused++;
				nsfbufspeak = imax(nsfbufspeak, nsfbufsused);
			}
#if defined(SMP) && defined(SFBUF_CPUSET)
			sf_buf_shootdown(sf, flags);
#endif
			goto done;
		}
	}
	while ((sf = TAILQ_FIRST(&sf_buf_freelist)) == NULL) {
		if (flags & SFB_NOWAIT)
			goto done;
		sf_buf_alloc_want++;
		SFSTAT_INC(sf_allocwait);
		error = msleep(&sf_buf_freelist, &sf_buf_lock,
		    (flags & SFB_CATCH) ? PCATCH | PVM : PVM, "sfbufa", 0);
		sf_buf_alloc_want--;

		/*
		 * If we got a signal, don't risk going back to sleep. 
		 */
		if (error)
			goto done;
	}
	TAILQ_REMOVE(&sf_buf_freelist, sf, free_entry);
	if (sf->m != NULL)
		LIST_REMOVE(sf, list_entry);
	LIST_INSERT_HEAD(hash_list, sf, list_entry);
	sf->ref_count = 1;
	sf->m = m;
	nsfbufsused++;
	nsfbufspeak = imax(nsfbufspeak, nsfbufsused);
	sf_buf_map(sf, flags);
done:
	mtx_unlock(&sf_buf_lock);
	return (sf);
}

/*
 * Remove a reference from the given sf_buf, adding it to the free
 * list when its reference count reaches zero.  A freed sf_buf still,
 * however, retains its virtual-to-physical mapping until it is
 * recycled or reactivated by sf_buf_alloc(9).
 */
void
sf_buf_free(struct sf_buf *sf)
{

	if (PMAP_HAS_DMAP)
		return;

	mtx_lock(&sf_buf_lock);
	sf->ref_count--;
	if (sf->ref_count == 0) {
		TAILQ_INSERT_TAIL(&sf_buf_freelist, sf, free_entry);
		nsfbufsused--;
		if (sf_buf_unmap(sf)) {
			sf->m = NULL;
			LIST_REMOVE(sf, list_entry);
		}
		if (sf_buf_alloc_want > 0)
			wakeup(&sf_buf_freelist);
	}
	mtx_unlock(&sf_buf_lock);
}

void
sf_buf_ref(struct sf_buf *sf)
{

	if (PMAP_HAS_DMAP)
		return;

	mtx_lock(&sf_buf_lock);
	KASSERT(sf->ref_count > 0, ("%s: sf %p not allocated", __func__, sf));
	sf->ref_count++;
	mtx_unlock(&sf_buf_lock);
}

#ifdef SFBUF_PROCESS_PAGE
/*
 * Run callback function on sf_buf that holds a certain page.
 */
boolean_t
sf_buf_process_page(vm_page_t m, void (*cb)(struct sf_buf *))
{
	struct sf_head *hash_list;
	struct sf_buf *sf;

	hash_list = &sf_buf_active[SF_BUF_HASH(m)];
	mtx_lock(&sf_buf_lock);
	LIST_FOREACH(sf, hash_list, list_entry) {
		if (sf->m == m) {
			cb(sf);
			mtx_unlock(&sf_buf_lock);
			return (TRUE);
		}
	}
	mtx_unlock(&sf_buf_lock);
	return (FALSE);
}
#endif	/* SFBUF_PROCESS_PAGE */
