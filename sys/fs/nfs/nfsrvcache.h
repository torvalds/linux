/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
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
 * $FreeBSD$
 */

#ifndef _NFS_NFSRVCACHE_H_
#define	_NFS_NFSRVCACHE_H_

/*
 * Definitions for the server recent request cache
 */
#define	NFSRVCACHE_MAX_SIZE	2048
#define	NFSRVCACHE_MIN_SIZE	  64

#define	NFSRVCACHE_HASHSIZE	500

/* Cache table entry. */
struct nfsrvcache {
	LIST_ENTRY(nfsrvcache) rc_hash;		/* Hash chain */
	LIST_ENTRY(nfsrvcache) rc_ahash;	/* ACK hash chain */
	TAILQ_ENTRY(nfsrvcache)	rc_lru;		/* UDP lru chain */
	u_int32_t	rc_xid;			/* rpc id number */
	time_t		rc_timestamp;		/* Time done */
	union {
		mbuf_t repmb;			/* Reply mbuf list OR */
		int repstat;			/* Reply status */
	} rc_un;
	union {
		struct {
			union nethostaddr haddr; /* Host address */
		} udp;
		struct {
			u_int64_t	sockref;
			u_int32_t	len;
			u_int32_t	tcpseq;
			int16_t		refcnt;
			u_int16_t	cksum;
			time_t		cachetime;
			int		acked;
		} ot;
	} rc_un2;
	u_int16_t	rc_proc;		/* rpc proc number */
	u_int16_t	rc_flag;		/* Flag bits */
};

#define	rc_reply	rc_un.repmb
#define	rc_status	rc_un.repstat
#define	rc_inet		rc_un2.udp.haddr.had_inet.s_addr
#define	rc_inet6	rc_un2.udp.haddr.had_inet6
#define	rc_haddr	rc_un2.udp.haddr
#define	rc_sockref	rc_un2.ot.sockref
#define	rc_tcpseq	rc_un2.ot.tcpseq
#define	rc_refcnt	rc_un2.ot.refcnt
#define	rc_reqlen	rc_un2.ot.len
#define	rc_cksum	rc_un2.ot.cksum
#define	rc_cachetime	rc_un2.ot.cachetime
#define	rc_acked	rc_un2.ot.acked

/* TCP ACK values */
#define	RC_NO_SEQ		0
#define	RC_NO_ACK		1
#define	RC_ACK			2
#define	RC_NACK			3

/* Return values */
#define	RC_DROPIT		0
#define	RC_REPLY		1
#define	RC_DOIT			2

/* Flag bits */
#define	RC_LOCKED	0x0001
#define	RC_WANTED	0x0002
#define	RC_REPSTATUS	0x0004
#define	RC_REPMBUF	0x0008
#define	RC_UDP		0x0010
#define	RC_INETIPV6	0x0020
#define	RC_INPROG	0x0040
#define	RC_NFSV2	0x0100
#define	RC_NFSV3	0x0200
#define	RC_NFSV4	0x0400
#define	RC_NFSVERS	(RC_NFSV2 | RC_NFSV3 | RC_NFSV4)
#define	RC_REFCNT	0x0800
#define	RC_SAMETCPCONN	0x1000

LIST_HEAD(nfsrvhashhead, nfsrvcache);

/* The fine-grained locked cache hash table for TCP. */
struct nfsrchash_bucket {
	struct mtx		mtx;
	struct nfsrvhashhead	tbl;
};

#endif	/* _NFS_NFSRVCACHE_H_ */
