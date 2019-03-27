/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2014 Gleb Smirnoff <glebius@FreeBSD.org>
 * Copyright (c) 2003-2004 Alan L. Cox <alc@cs.rice.edu>
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

#ifndef _SYS_SF_BUF_H_
#define _SYS_SF_BUF_H_

struct sfstat {				/* sendfile statistics */
	uint64_t	sf_syscalls;	/* times sendfile was called */
	uint64_t	sf_noiocnt;	/* times sendfile didn't require I/O */
	uint64_t	sf_iocnt;	/* times sendfile had to do disk I/O */
	uint64_t	sf_pages_read;	/* pages read as part of a request */
	uint64_t	sf_pages_valid;	/* pages were valid for a request */
	uint64_t	sf_rhpages_requested;	/* readahead pages requested */
	uint64_t	sf_rhpages_read;	/* readahead pages read */
	uint64_t	sf_busy;	/* times aborted on a busy page */
	uint64_t	sf_allocfail;	/* times sfbuf allocation failed */
	uint64_t	sf_allocwait;	/* times sfbuf allocation had to wait */
	uint64_t	sf_pages_bogus;	/* times bogus page was used */
};

#ifdef _KERNEL
#include <sys/types.h>
#include <sys/systm.h>
#include <sys/counter.h>
#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/vm_page.h>

/*
 * Sf_bufs, or sendfile(2) buffers provide a vm_page that is mapped
 * into kernel address space. Note, that they aren't used only
 * by sendfile(2)!
 *
 * Sf_bufs could be implemented as a feature of vm_page_t, but that
 * would require growth of the structure. That's why they are implemented
 * as a separate hash indexed by vm_page address. Implementation lives in
 * kern/subr_sfbuf.c. Meanwhile, most 64-bit machines have a physical map,
 * so they don't require this hash at all, thus ignore subr_sfbuf.c.
 *
 * Different 32-bit architectures demand different requirements on sf_buf
 * hash and functions. They request features in machine/vmparam.h, which
 * enable parts of this file. They can also optionally provide helpers in
 * machine/sf_buf.h
 *
 * Defines are:
 * SFBUF		This machine requires sf_buf hash.
 * 			subr_sfbuf.c should be compiled.
 * SFBUF_CPUSET		This machine can perform SFB_CPUPRIVATE mappings,
 *			that do no invalidate cache on the rest of CPUs.
 * SFBUF_NOMD		This machine doesn't have machine/sf_buf.h
 *
 * SFBUF_MAP		This machine provides its own sf_buf_map() and
 *			sf_buf_unmap().
 * SFBUF_PROCESS_PAGE	This machine provides sf_buf_process_page()
 *			function.
 */

#ifdef SFBUF
#if defined(SMP) && defined(SFBUF_CPUSET)
#include <sys/_cpuset.h>
#endif
#include <sys/queue.h>

struct sf_buf {
	LIST_ENTRY(sf_buf)	list_entry;	/* list of buffers */
	TAILQ_ENTRY(sf_buf)	free_entry;	/* list of buffers */
	vm_page_t		m;		/* currently mapped page */
	vm_offset_t		kva;		/* va of mapping */
	int			ref_count;	/* usage of this mapping */
#if defined(SMP) && defined(SFBUF_CPUSET)
	cpuset_t		cpumask;	/* where mapping is valid */
#endif
};
#else /* ! SFBUF */
struct sf_buf;
#endif /* SFBUF */

#ifndef SFBUF_NOMD
#include <machine/sf_buf.h>
#endif

#ifdef SFBUF
struct sf_buf *sf_buf_alloc(struct vm_page *, int);
void sf_buf_free(struct sf_buf *);
void sf_buf_ref(struct sf_buf *);

static inline vm_offset_t
sf_buf_kva(struct sf_buf *sf)
{
	if (PMAP_HAS_DMAP)
		return (PHYS_TO_DMAP(VM_PAGE_TO_PHYS((vm_page_t)sf)));

        return (sf->kva);
}

static inline vm_page_t
sf_buf_page(struct sf_buf *sf)
{
	if (PMAP_HAS_DMAP)
		return ((vm_page_t)sf);

        return (sf->m);
}

#ifndef SFBUF_MAP
#include <vm/pmap.h>

static inline void
sf_buf_map(struct sf_buf *sf, int flags)
{

	pmap_qenter(sf->kva, &sf->m, 1);
}

static inline int
sf_buf_unmap(struct sf_buf *sf)
{

	return (0);
}
#endif /* SFBUF_MAP */

#if defined(SMP) && defined(SFBUF_CPUSET)
void sf_buf_shootdown(struct sf_buf *, int);
#endif

#ifdef SFBUF_PROCESS_PAGE
boolean_t sf_buf_process_page(vm_page_t, void (*)(struct sf_buf *));
#endif

#else /* ! SFBUF */

static inline struct sf_buf *
sf_buf_alloc(struct vm_page *m, int pri)
{

	return ((struct sf_buf *)m);
}

static inline void
sf_buf_free(struct sf_buf *sf)
{
}

static inline void
sf_buf_ref(struct sf_buf *sf)
{
}
#endif /* SFBUF */

/*
 * Options to sf_buf_alloc() are specified through its flags argument.  This
 * argument's value should be the result of a bitwise or'ing of one or more
 * of the following values.
 */
#define	SFB_CATCH	1		/* Check signals if the allocation
					   sleeps. */
#define	SFB_CPUPRIVATE	2		/* Create a CPU private mapping. */
#define	SFB_DEFAULT	0
#define	SFB_NOWAIT	4		/* Return NULL if all bufs are used. */

extern counter_u64_t sfstat[sizeof(struct sfstat) / sizeof(uint64_t)];
#define	SFSTAT_ADD(name, val)	\
    counter_u64_add(sfstat[offsetof(struct sfstat, name) / sizeof(uint64_t)],\
	(val))
#define	SFSTAT_INC(name)	SFSTAT_ADD(name, 1)
#endif /* _KERNEL */
#endif /* !_SYS_SF_BUF_H_ */
