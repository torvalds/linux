/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2017,	Jeffrey Roberson <jeff@freebsd.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _SYS_DOMAINSET_H_
#define	_SYS_DOMAINSET_H_

#include <sys/_domainset.h>
#include <sys/bitset.h>
#include <sys/queue.h>

#define	_NDOMAINSETBITS			_BITSET_BITS
#define	_NDOMAINSETWORDS		__bitset_words(DOMAINSET_SETSIZE)

#define	DOMAINSETBUFSIZ							\
	    (((2 + sizeof(long) * 2) * _NDOMAINSETWORDS) +		\
	    sizeof("::") + sizeof(__XSTRING(DOMAINSET_POLICY_MAX)) +	\
	    sizeof(__XSTRING(MAXMEMDOM)))


#define	DOMAINSET_CLR(n, p)		BIT_CLR(DOMAINSET_SETSIZE, n, p)
#define	DOMAINSET_COPY(f, t)		BIT_COPY(DOMAINSET_SETSIZE, f, t)
#define	DOMAINSET_ISSET(n, p)		BIT_ISSET(DOMAINSET_SETSIZE, n, p)
#define	DOMAINSET_SET(n, p)		BIT_SET(DOMAINSET_SETSIZE, n, p)
#define	DOMAINSET_ZERO(p) 		BIT_ZERO(DOMAINSET_SETSIZE, p)
#define	DOMAINSET_FILL(p) 		BIT_FILL(DOMAINSET_SETSIZE, p)
#define	DOMAINSET_SETOF(n, p)		BIT_SETOF(DOMAINSET_SETSIZE, n, p)
#define	DOMAINSET_EMPTY(p)		BIT_EMPTY(DOMAINSET_SETSIZE, p)
#define	DOMAINSET_ISFULLSET(p)		BIT_ISFULLSET(DOMAINSET_SETSIZE, p)
#define	DOMAINSET_SUBSET(p, c)		BIT_SUBSET(DOMAINSET_SETSIZE, p, c)
#define	DOMAINSET_OVERLAP(p, c)		BIT_OVERLAP(DOMAINSET_SETSIZE, p, c)
#define	DOMAINSET_CMP(p, c)		BIT_CMP(DOMAINSET_SETSIZE, p, c)
#define	DOMAINSET_OR(d, s)		BIT_OR(DOMAINSET_SETSIZE, d, s)
#define	DOMAINSET_AND(d, s)		BIT_AND(DOMAINSET_SETSIZE, d, s)
#define	DOMAINSET_NAND(d, s)		BIT_NAND(DOMAINSET_SETSIZE, d, s)
#define	DOMAINSET_CLR_ATOMIC(n, p)	BIT_CLR_ATOMIC(DOMAINSET_SETSIZE, n, p)
#define	DOMAINSET_SET_ATOMIC(n, p)	BIT_SET_ATOMIC(DOMAINSET_SETSIZE, n, p)
#define	DOMAINSET_SET_ATOMIC_ACQ(n, p)					\
	    BIT_SET_ATOMIC_ACQ(DOMAINSET_SETSIZE, n, p)
#define	DOMAINSET_AND_ATOMIC(n, p)	BIT_AND_ATOMIC(DOMAINSET_SETSIZE, n, p)
#define	DOMAINSET_OR_ATOMIC(d, s)	BIT_OR_ATOMIC(DOMAINSET_SETSIZE, d, s)
#define	DOMAINSET_COPY_STORE_REL(f, t)					\
	    BIT_COPY_STORE_REL(DOMAINSET_SETSIZE, f, t)
#define	DOMAINSET_FFS(p)		BIT_FFS(DOMAINSET_SETSIZE, p)
#define	DOMAINSET_FLS(p)		BIT_FLS(DOMAINSET_SETSIZE, p)
#define	DOMAINSET_COUNT(p)		BIT_COUNT(DOMAINSET_SETSIZE, p)
#define	DOMAINSET_FSET			BITSET_FSET(_NDOMAINSETWORDS)
#define	DOMAINSET_T_INITIALIZER		BITSET_T_INITIALIZER

#define	DOMAINSET_POLICY_INVALID	0
#define	DOMAINSET_POLICY_ROUNDROBIN	1
#define	DOMAINSET_POLICY_FIRSTTOUCH	2
#define	DOMAINSET_POLICY_PREFER		3
#define	DOMAINSET_POLICY_INTERLEAVE	4
#define	DOMAINSET_POLICY_MAX		DOMAINSET_POLICY_INTERLEAVE

#ifdef _KERNEL
#if MAXMEMDOM < 256
typedef	uint8_t		domainid_t;
#else
typedef uint16_t	domainid_t;
#endif

struct domainset {
	LIST_ENTRY(domainset)	ds_link;
	domainset_t	ds_mask;	/* allowed domains. */
	uint16_t	ds_policy;	/* Policy type. */
	domainid_t	ds_prefer;	/* Preferred domain or -1. */
	domainid_t	ds_cnt;		/* popcnt from above. */
	domainid_t	ds_order[MAXMEMDOM];  /* nth domain table. */
};

extern struct domainset domainset_fixed[MAXMEMDOM], domainset_prefer[MAXMEMDOM];
#define	DOMAINSET_FIXED(domain)	(&domainset_fixed[(domain)])
#define	DOMAINSET_PREF(domain)	(&domainset_prefer[(domain)])
extern struct domainset domainset_roundrobin;
#define	DOMAINSET_RR()		(&domainset_roundrobin)

void domainset_init(void);
void domainset_zero(void);

/*
 * Add a domainset to the system based on a key initializing policy, prefer,
 * and mask.  Do not create and directly use domainset structures.  The
 * returned value will not match the key pointer.
 */
struct domainset *domainset_create(const struct domainset *);
#ifdef _SYS_SYSCTL_H_
int sysctl_handle_domainset(SYSCTL_HANDLER_ARGS);
#endif

#else
__BEGIN_DECLS
int	cpuset_getdomain(cpulevel_t, cpuwhich_t, id_t, size_t, domainset_t *,
	    int *);
int	cpuset_setdomain(cpulevel_t, cpuwhich_t, id_t, size_t,
	    const domainset_t *, int);

__END_DECLS
#endif
#endif /* !_SYS_DOMAINSET_H_ */
