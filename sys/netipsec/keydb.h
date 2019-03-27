/*	$FreeBSD$	*/
/*	$KAME: keydb.h,v 1.14 2000/08/02 17:58:26 sakane Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (C) 1995, 1996, 1997, and 1998 WIDE Project.
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
 * 3. Neither the name of the project nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _NETIPSEC_KEYDB_H_
#define _NETIPSEC_KEYDB_H_

#ifdef _KERNEL
#include <sys/counter.h>
#include <sys/lock.h>
#include <sys/mutex.h>

#include <netipsec/key_var.h>
#include <opencrypto/_cryptodev.h>

#ifndef _SOCKADDR_UNION_DEFINED
#define	_SOCKADDR_UNION_DEFINED
/*
 * The union of all possible address formats we handle.
 */
union sockaddr_union {
	struct sockaddr		sa;
	struct sockaddr_in	sin;
	struct sockaddr_in6	sin6;
};
#endif /* _SOCKADDR_UNION_DEFINED */

/* Security Assocciation Index */
/* NOTE: Ensure to be same address family */
struct secasindex {
	union sockaddr_union src;	/* source address for SA */
	union sockaddr_union dst;	/* destination address for SA */
	uint8_t proto;			/* IPPROTO_ESP or IPPROTO_AH */
	uint8_t mode;			/* mode of protocol, see ipsec.h */
	uint32_t reqid;			/* reqid id who owned this SA */
					/* see IPSEC_MANUAL_REQID_MAX. */
};

/* 
 * In order to split out the keydb implementation from that of the
 * PF_KEY sockets we need to define a few structures that while they
 * may seem common are likely to diverge over time. 
 */

/* sadb_identity */
struct secident {
	u_int16_t type;
	u_int64_t id;
};

/* sadb_key */
struct seckey {
	u_int16_t bits;
	char *key_data;
};

struct seclifetime {
	u_int32_t allocations;
	u_int64_t bytes;
	u_int64_t addtime;
	u_int64_t usetime;
};

struct secnatt {
	union sockaddr_union oai;	/* original addresses of initiator */
	union sockaddr_union oar;	/* original address of responder */
	uint16_t sport;			/* source port */
	uint16_t dport;			/* destination port */
	uint16_t cksum;			/* checksum delta */
	uint16_t flags;
#define	IPSEC_NATT_F_OAI	0x0001
#define	IPSEC_NATT_F_OAR	0x0002
};

/* Security Association Data Base */
TAILQ_HEAD(secasvar_queue, secasvar);
struct secashead {
	TAILQ_ENTRY(secashead) chain;
	LIST_ENTRY(secashead) addrhash;	/* hash by sproto+src+dst addresses */
	LIST_ENTRY(secashead) drainq;	/* used ONLY by flush callout */

	struct secasindex saidx;

	struct secident *idents;	/* source identity */
	struct secident *identd;	/* destination identity */
					/* XXX I don't know how to use them. */

	volatile u_int refcnt;		/* reference count */
	uint8_t state;			/* MATURE or DEAD. */
	struct secasvar_queue savtree_alive;	/* MATURE and DYING SA */
	struct secasvar_queue savtree_larval;	/* LARVAL SA */
};

struct xformsw;
struct enc_xform;
struct auth_hash;
struct comp_algo;

/*
 * Security Association
 *
 * For INBOUND packets we do SA lookup using SPI, thus only SPIHASH is used.
 * For OUTBOUND packets there may be several SA suitable for packet.
 * We use key_preferred_oldsa variable to choose better SA. First of we do
 * lookup for suitable SAH using packet's saidx. Then we use SAH's savtree
 * to search better candidate. The newer SA (by created time) are placed
 * in the beginning of the savtree list. There is no preference between
 * DYING and MATURE.
 *
 * NB: Fields with a tdb_ prefix are part of the "glue" used
 *     to interface to the OpenBSD crypto support.  This was done
 *     to distinguish this code from the mainline KAME code.
 * NB: Fields are sorted on the basis of the frequency of changes, i.e.
 *     constants and unchangeable fields are going first.
 * NB: if you want to change this structure, check that this will not break
 *     key_updateaddresses().
 */
struct secasvar {
	uint32_t spi;			/* SPI Value, network byte order */
	uint32_t flags;			/* holder for SADB_KEY_FLAGS */
	uint32_t seq;			/* sequence number */
	pid_t pid;			/* message's pid */
	u_int ivlen;			/* length of IV */

	struct secashead *sah;		/* back pointer to the secashead */
	struct seckey *key_auth;	/* Key for Authentication */
	struct seckey *key_enc;	        /* Key for Encryption */
	struct secreplay *replay;	/* replay prevention */
	struct secnatt *natt;		/* NAT-T config */
	struct mtx *lock;		/* update/access lock */

	const struct xformsw *tdb_xform;	/* transform */
	const struct enc_xform *tdb_encalgxform;/* encoding algorithm */
	const struct auth_hash *tdb_authalgxform;/* authentication algorithm */
	const struct comp_algo *tdb_compalgxform;/* compression algorithm */
	crypto_session_t tdb_cryptoid;		/* crypto session */

	uint8_t alg_auth;		/* Authentication Algorithm Identifier*/
	uint8_t alg_enc;		/* Cipher Algorithm Identifier */
	uint8_t alg_comp;		/* Compression Algorithm Identifier */
	uint8_t state;			/* Status of this SA (pfkeyv2.h) */

	counter_u64_t lft_c;		/* CURRENT lifetime */
#define	lft_c_allocations	lft_c
#define	lft_c_bytes		lft_c + 1
	struct seclifetime *lft_h;	/* HARD lifetime */
	struct seclifetime *lft_s;	/* SOFT lifetime */

	uint64_t created;		/* time when SA was created */
	uint64_t firstused;		/* time when SA was first used */

	TAILQ_ENTRY(secasvar) chain;
	LIST_ENTRY(secasvar) spihash;
	LIST_ENTRY(secasvar) drainq;	/* used ONLY by flush callout */

	uint64_t cntr;			/* counter for GCM and CTR */
	volatile u_int refcnt;		/* reference count */
};

#define	SECASVAR_LOCK(_sav)		mtx_lock((_sav)->lock)
#define	SECASVAR_UNLOCK(_sav)		mtx_unlock((_sav)->lock)
#define	SECASVAR_LOCK_ASSERT(_sav)	mtx_assert((_sav)->lock, MA_OWNED)
#define	SAV_ISGCM(_sav)							\
			((_sav)->alg_enc == SADB_X_EALG_AESGCM8 ||	\
			(_sav)->alg_enc == SADB_X_EALG_AESGCM12 ||	\
			(_sav)->alg_enc == SADB_X_EALG_AESGCM16)
#define	SAV_ISCTR(_sav) ((_sav)->alg_enc == SADB_X_EALG_AESCTR)
#define SAV_ISCTRORGCM(_sav)	(SAV_ISCTR((_sav)) || SAV_ISGCM((_sav)))

/* Replay prevention, protected by SECASVAR_LOCK:
 *  (m) locked by mtx
 *  (c) read only except during creation / free
 */
struct secreplay {
	u_int32_t count;	/* (m) */
	u_int wsize;		/* (c) window size, i.g. 4 bytes */
	u_int32_t seq;		/* (m) used by sender */
	u_int32_t lastseq;	/* (m) used by receiver */
	u_int32_t *bitmap;	/* (m) used by receiver */
	u_int bitmap_size;	/* (c) size of the bitmap array */
	int overflow;		/* (m) overflow flag */
};

/* socket table due to send PF_KEY messages. */
struct secreg {
	LIST_ENTRY(secreg) chain;

	struct socket *so;
};

/* acquiring list table. */
struct secacq {
	LIST_ENTRY(secacq) chain;
	LIST_ENTRY(secacq) addrhash;
	LIST_ENTRY(secacq) seqhash;

	struct secasindex saidx;
	uint32_t seq;		/* sequence number */
	time_t created;		/* for lifetime */
	int count;		/* for lifetime */
};

#endif /* _KERNEL */

#endif /* _NETIPSEC_KEYDB_H_ */
