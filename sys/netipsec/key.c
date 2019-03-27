/*	$FreeBSD$	*/
/*	$KAME: key.c,v 1.191 2001/06/27 10:46:49 sakane Exp $	*/

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

/*
 * This code is referd to RFC 2367
 */

#include "opt_inet.h"
#include "opt_inet6.h"
#include "opt_ipsec.h"

#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/fnv_hash.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/mbuf.h>
#include <sys/domain.h>
#include <sys/protosw.h>
#include <sys/malloc.h>
#include <sys/rmlock.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/sysctl.h>
#include <sys/errno.h>
#include <sys/proc.h>
#include <sys/queue.h>
#include <sys/refcount.h>
#include <sys/syslog.h>

#include <vm/uma.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/vnet.h>
#include <net/raw_cb.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/in_var.h>
#include <netinet/udp.h>

#ifdef INET6
#include <netinet/ip6.h>
#include <netinet6/in6_var.h>
#include <netinet6/ip6_var.h>
#endif /* INET6 */

#include <net/pfkeyv2.h>
#include <netipsec/keydb.h>
#include <netipsec/key.h>
#include <netipsec/keysock.h>
#include <netipsec/key_debug.h>

#include <netipsec/ipsec.h>
#ifdef INET6
#include <netipsec/ipsec6.h>
#endif

#include <netipsec/xform.h>
#include <machine/in_cksum.h>
#include <machine/stdarg.h>

/* randomness */
#include <sys/random.h>

#define FULLMASK	0xff
#define	_BITS(bytes)	((bytes) << 3)

/*
 * Note on SA reference counting:
 * - SAs that are not in DEAD state will have (total external reference + 1)
 *   following value in reference count field.  they cannot be freed and are
 *   referenced from SA header.
 * - SAs that are in DEAD state will have (total external reference)
 *   in reference count field.  they are ready to be freed.  reference from
 *   SA header will be removed in key_delsav(), when the reference count
 *   field hits 0 (= no external reference other than from SA header.
 */

VNET_DEFINE(u_int32_t, key_debug_level) = 0;
VNET_DEFINE_STATIC(u_int, key_spi_trycnt) = 1000;
VNET_DEFINE_STATIC(u_int32_t, key_spi_minval) = 0x100;
VNET_DEFINE_STATIC(u_int32_t, key_spi_maxval) = 0x0fffffff;	/* XXX */
VNET_DEFINE_STATIC(u_int32_t, policy_id) = 0;
/*interval to initialize randseed,1(m)*/
VNET_DEFINE_STATIC(u_int, key_int_random) = 60;
/* interval to expire acquiring, 30(s)*/
VNET_DEFINE_STATIC(u_int, key_larval_lifetime) = 30;
/* counter for blocking SADB_ACQUIRE.*/
VNET_DEFINE_STATIC(int, key_blockacq_count) = 10;
/* lifetime for blocking SADB_ACQUIRE.*/
VNET_DEFINE_STATIC(int, key_blockacq_lifetime) = 20;
/* preferred old sa rather than new sa.*/
VNET_DEFINE_STATIC(int, key_preferred_oldsa) = 1;
#define	V_key_spi_trycnt	VNET(key_spi_trycnt)
#define	V_key_spi_minval	VNET(key_spi_minval)
#define	V_key_spi_maxval	VNET(key_spi_maxval)
#define	V_policy_id		VNET(policy_id)
#define	V_key_int_random	VNET(key_int_random)
#define	V_key_larval_lifetime	VNET(key_larval_lifetime)
#define	V_key_blockacq_count	VNET(key_blockacq_count)
#define	V_key_blockacq_lifetime	VNET(key_blockacq_lifetime)
#define	V_key_preferred_oldsa	VNET(key_preferred_oldsa)

VNET_DEFINE_STATIC(u_int32_t, acq_seq) = 0;
#define	V_acq_seq		VNET(acq_seq)

VNET_DEFINE_STATIC(uint32_t, sp_genid) = 0;
#define	V_sp_genid		VNET(sp_genid)

/* SPD */
TAILQ_HEAD(secpolicy_queue, secpolicy);
LIST_HEAD(secpolicy_list, secpolicy);
VNET_DEFINE_STATIC(struct secpolicy_queue, sptree[IPSEC_DIR_MAX]);
VNET_DEFINE_STATIC(struct secpolicy_queue, sptree_ifnet[IPSEC_DIR_MAX]);
static struct rmlock sptree_lock;
#define	V_sptree		VNET(sptree)
#define	V_sptree_ifnet		VNET(sptree_ifnet)
#define	SPTREE_LOCK_INIT()      rm_init(&sptree_lock, "sptree")
#define	SPTREE_LOCK_DESTROY()   rm_destroy(&sptree_lock)
#define	SPTREE_RLOCK_TRACKER    struct rm_priotracker sptree_tracker
#define	SPTREE_RLOCK()          rm_rlock(&sptree_lock, &sptree_tracker)
#define	SPTREE_RUNLOCK()        rm_runlock(&sptree_lock, &sptree_tracker)
#define	SPTREE_RLOCK_ASSERT()   rm_assert(&sptree_lock, RA_RLOCKED)
#define	SPTREE_WLOCK()          rm_wlock(&sptree_lock)
#define	SPTREE_WUNLOCK()        rm_wunlock(&sptree_lock)
#define	SPTREE_WLOCK_ASSERT()   rm_assert(&sptree_lock, RA_WLOCKED)
#define	SPTREE_UNLOCK_ASSERT()  rm_assert(&sptree_lock, RA_UNLOCKED)

/* Hash table for lookup SP using unique id */
VNET_DEFINE_STATIC(struct secpolicy_list *, sphashtbl);
VNET_DEFINE_STATIC(u_long, sphash_mask);
#define	V_sphashtbl		VNET(sphashtbl)
#define	V_sphash_mask		VNET(sphash_mask)

#define	SPHASH_NHASH_LOG2	7
#define	SPHASH_NHASH		(1 << SPHASH_NHASH_LOG2)
#define	SPHASH_HASHVAL(id)	(key_u32hash(id) & V_sphash_mask)
#define	SPHASH_HASH(id)		&V_sphashtbl[SPHASH_HASHVAL(id)]

/* SPD cache */
struct spdcache_entry {
   struct secpolicyindex spidx;	/* secpolicyindex */
   struct secpolicy *sp;	/* cached policy to be used */

   LIST_ENTRY(spdcache_entry) chain;
};
LIST_HEAD(spdcache_entry_list, spdcache_entry);

#define	SPDCACHE_MAX_ENTRIES_PER_HASH	8

VNET_DEFINE_STATIC(u_int, key_spdcache_maxentries) = 0;
#define	V_key_spdcache_maxentries	VNET(key_spdcache_maxentries)
VNET_DEFINE_STATIC(u_int, key_spdcache_threshold) = 32;
#define	V_key_spdcache_threshold	VNET(key_spdcache_threshold)
VNET_DEFINE_STATIC(unsigned long, spd_size) = 0;
#define	V_spd_size		VNET(spd_size)

#define SPDCACHE_ENABLED()	(V_key_spdcache_maxentries != 0)
#define SPDCACHE_ACTIVE() \
	(SPDCACHE_ENABLED() && V_spd_size >= V_key_spdcache_threshold)

VNET_DEFINE_STATIC(struct spdcache_entry_list *, spdcachehashtbl);
VNET_DEFINE_STATIC(u_long, spdcachehash_mask);
#define	V_spdcachehashtbl	VNET(spdcachehashtbl)
#define	V_spdcachehash_mask	VNET(spdcachehash_mask)

#define	SPDCACHE_HASHVAL(idx) \
	(key_addrprotohash(&(idx)->src, &(idx)->dst, &(idx)->ul_proto) &  \
	    V_spdcachehash_mask)

/* Each cache line is protected by a mutex */
VNET_DEFINE_STATIC(struct mtx *, spdcache_lock);
#define	V_spdcache_lock		VNET(spdcache_lock)

#define	SPDCACHE_LOCK_INIT(a) \
	mtx_init(&V_spdcache_lock[a], "spdcache", \
	    "fast ipsec SPD cache", MTX_DEF|MTX_DUPOK)
#define	SPDCACHE_LOCK_DESTROY(a)	mtx_destroy(&V_spdcache_lock[a])
#define	SPDCACHE_LOCK(a)		mtx_lock(&V_spdcache_lock[a]);
#define	SPDCACHE_UNLOCK(a)		mtx_unlock(&V_spdcache_lock[a]);

/* SAD */
TAILQ_HEAD(secashead_queue, secashead);
LIST_HEAD(secashead_list, secashead);
VNET_DEFINE_STATIC(struct secashead_queue, sahtree);
static struct rmlock sahtree_lock;
#define	V_sahtree		VNET(sahtree)
#define	SAHTREE_LOCK_INIT()	rm_init(&sahtree_lock, "sahtree")
#define	SAHTREE_LOCK_DESTROY()	rm_destroy(&sahtree_lock)
#define	SAHTREE_RLOCK_TRACKER	struct rm_priotracker sahtree_tracker
#define	SAHTREE_RLOCK()		rm_rlock(&sahtree_lock, &sahtree_tracker)
#define	SAHTREE_RUNLOCK()	rm_runlock(&sahtree_lock, &sahtree_tracker)
#define	SAHTREE_RLOCK_ASSERT()	rm_assert(&sahtree_lock, RA_RLOCKED)
#define	SAHTREE_WLOCK()		rm_wlock(&sahtree_lock)
#define	SAHTREE_WUNLOCK()	rm_wunlock(&sahtree_lock)
#define	SAHTREE_WLOCK_ASSERT()	rm_assert(&sahtree_lock, RA_WLOCKED)
#define	SAHTREE_UNLOCK_ASSERT()	rm_assert(&sahtree_lock, RA_UNLOCKED)

/* Hash table for lookup in SAD using SA addresses */
VNET_DEFINE_STATIC(struct secashead_list *, sahaddrhashtbl);
VNET_DEFINE_STATIC(u_long, sahaddrhash_mask);
#define	V_sahaddrhashtbl	VNET(sahaddrhashtbl)
#define	V_sahaddrhash_mask	VNET(sahaddrhash_mask)

#define	SAHHASH_NHASH_LOG2	7
#define	SAHHASH_NHASH		(1 << SAHHASH_NHASH_LOG2)
#define	SAHADDRHASH_HASHVAL(idx)	\
	(key_addrprotohash(&(idx)->src, &(idx)->dst, &(idx)->proto) & \
	    V_sahaddrhash_mask)
#define	SAHADDRHASH_HASH(saidx)		\
    &V_sahaddrhashtbl[SAHADDRHASH_HASHVAL(saidx)]

/* Hash table for lookup in SAD using SPI */
LIST_HEAD(secasvar_list, secasvar);
VNET_DEFINE_STATIC(struct secasvar_list *, savhashtbl);
VNET_DEFINE_STATIC(u_long, savhash_mask);
#define	V_savhashtbl		VNET(savhashtbl)
#define	V_savhash_mask		VNET(savhash_mask)
#define	SAVHASH_NHASH_LOG2	7
#define	SAVHASH_NHASH		(1 << SAVHASH_NHASH_LOG2)
#define	SAVHASH_HASHVAL(spi)	(key_u32hash(spi) & V_savhash_mask)
#define	SAVHASH_HASH(spi)	&V_savhashtbl[SAVHASH_HASHVAL(spi)]

static uint32_t
key_addrprotohash(const union sockaddr_union *src,
    const union sockaddr_union *dst, const uint8_t *proto)
{
	uint32_t hval;

	hval = fnv_32_buf(proto, sizeof(*proto),
	    FNV1_32_INIT);
	switch (dst->sa.sa_family) {
#ifdef INET
	case AF_INET:
		hval = fnv_32_buf(&src->sin.sin_addr,
		    sizeof(in_addr_t), hval);
		hval = fnv_32_buf(&dst->sin.sin_addr,
		    sizeof(in_addr_t), hval);
		break;
#endif
#ifdef INET6
	case AF_INET6:
		hval = fnv_32_buf(&src->sin6.sin6_addr,
		    sizeof(struct in6_addr), hval);
		hval = fnv_32_buf(&dst->sin6.sin6_addr,
		    sizeof(struct in6_addr), hval);
		break;
#endif
	default:
		hval = 0;
		ipseclog((LOG_DEBUG, "%s: unknown address family %d",
		    __func__, dst->sa.sa_family));
	}
	return (hval);
}

static uint32_t
key_u32hash(uint32_t val)
{

	return (fnv_32_buf(&val, sizeof(val), FNV1_32_INIT));
}

							/* registed list */
VNET_DEFINE_STATIC(LIST_HEAD(_regtree, secreg), regtree[SADB_SATYPE_MAX + 1]);
#define	V_regtree		VNET(regtree)
static struct mtx regtree_lock;
#define	REGTREE_LOCK_INIT() \
	mtx_init(&regtree_lock, "regtree", "fast ipsec regtree", MTX_DEF)
#define	REGTREE_LOCK_DESTROY()	mtx_destroy(&regtree_lock)
#define	REGTREE_LOCK()		mtx_lock(&regtree_lock)
#define	REGTREE_UNLOCK()	mtx_unlock(&regtree_lock)
#define	REGTREE_LOCK_ASSERT()	mtx_assert(&regtree_lock, MA_OWNED)

/* Acquiring list */
LIST_HEAD(secacq_list, secacq);
VNET_DEFINE_STATIC(struct secacq_list, acqtree);
#define	V_acqtree		VNET(acqtree)
static struct mtx acq_lock;
#define	ACQ_LOCK_INIT() \
    mtx_init(&acq_lock, "acqtree", "ipsec SA acquiring list", MTX_DEF)
#define	ACQ_LOCK_DESTROY()	mtx_destroy(&acq_lock)
#define	ACQ_LOCK()		mtx_lock(&acq_lock)
#define	ACQ_UNLOCK()		mtx_unlock(&acq_lock)
#define	ACQ_LOCK_ASSERT()	mtx_assert(&acq_lock, MA_OWNED)

/* Hash table for lookup in ACQ list using SA addresses */
VNET_DEFINE_STATIC(struct secacq_list *, acqaddrhashtbl);
VNET_DEFINE_STATIC(u_long, acqaddrhash_mask);
#define	V_acqaddrhashtbl	VNET(acqaddrhashtbl)
#define	V_acqaddrhash_mask	VNET(acqaddrhash_mask)

/* Hash table for lookup in ACQ list using SEQ number */
VNET_DEFINE_STATIC(struct secacq_list *, acqseqhashtbl);
VNET_DEFINE_STATIC(u_long, acqseqhash_mask);
#define	V_acqseqhashtbl		VNET(acqseqhashtbl)
#define	V_acqseqhash_mask	VNET(acqseqhash_mask)

#define	ACQHASH_NHASH_LOG2	7
#define	ACQHASH_NHASH		(1 << ACQHASH_NHASH_LOG2)
#define	ACQADDRHASH_HASHVAL(idx)	\
	(key_addrprotohash(&(idx)->src, &(idx)->dst, &(idx)->proto) & \
	    V_acqaddrhash_mask)
#define	ACQSEQHASH_HASHVAL(seq)		\
    (key_u32hash(seq) & V_acqseqhash_mask)
#define	ACQADDRHASH_HASH(saidx)	\
    &V_acqaddrhashtbl[ACQADDRHASH_HASHVAL(saidx)]
#define	ACQSEQHASH_HASH(seq)	\
    &V_acqseqhashtbl[ACQSEQHASH_HASHVAL(seq)]
							/* SP acquiring list */
VNET_DEFINE_STATIC(LIST_HEAD(_spacqtree, secspacq), spacqtree);
#define	V_spacqtree		VNET(spacqtree)
static struct mtx spacq_lock;
#define	SPACQ_LOCK_INIT() \
	mtx_init(&spacq_lock, "spacqtree", \
		"fast ipsec security policy acquire list", MTX_DEF)
#define	SPACQ_LOCK_DESTROY()	mtx_destroy(&spacq_lock)
#define	SPACQ_LOCK()		mtx_lock(&spacq_lock)
#define	SPACQ_UNLOCK()		mtx_unlock(&spacq_lock)
#define	SPACQ_LOCK_ASSERT()	mtx_assert(&spacq_lock, MA_OWNED)

static const int minsize[] = {
	sizeof(struct sadb_msg),	/* SADB_EXT_RESERVED */
	sizeof(struct sadb_sa),		/* SADB_EXT_SA */
	sizeof(struct sadb_lifetime),	/* SADB_EXT_LIFETIME_CURRENT */
	sizeof(struct sadb_lifetime),	/* SADB_EXT_LIFETIME_HARD */
	sizeof(struct sadb_lifetime),	/* SADB_EXT_LIFETIME_SOFT */
	sizeof(struct sadb_address),	/* SADB_EXT_ADDRESS_SRC */
	sizeof(struct sadb_address),	/* SADB_EXT_ADDRESS_DST */
	sizeof(struct sadb_address),	/* SADB_EXT_ADDRESS_PROXY */
	sizeof(struct sadb_key),	/* SADB_EXT_KEY_AUTH */
	sizeof(struct sadb_key),	/* SADB_EXT_KEY_ENCRYPT */
	sizeof(struct sadb_ident),	/* SADB_EXT_IDENTITY_SRC */
	sizeof(struct sadb_ident),	/* SADB_EXT_IDENTITY_DST */
	sizeof(struct sadb_sens),	/* SADB_EXT_SENSITIVITY */
	sizeof(struct sadb_prop),	/* SADB_EXT_PROPOSAL */
	sizeof(struct sadb_supported),	/* SADB_EXT_SUPPORTED_AUTH */
	sizeof(struct sadb_supported),	/* SADB_EXT_SUPPORTED_ENCRYPT */
	sizeof(struct sadb_spirange),	/* SADB_EXT_SPIRANGE */
	0,				/* SADB_X_EXT_KMPRIVATE */
	sizeof(struct sadb_x_policy),	/* SADB_X_EXT_POLICY */
	sizeof(struct sadb_x_sa2),	/* SADB_X_SA2 */
	sizeof(struct sadb_x_nat_t_type),/* SADB_X_EXT_NAT_T_TYPE */
	sizeof(struct sadb_x_nat_t_port),/* SADB_X_EXT_NAT_T_SPORT */
	sizeof(struct sadb_x_nat_t_port),/* SADB_X_EXT_NAT_T_DPORT */
	sizeof(struct sadb_address),	/* SADB_X_EXT_NAT_T_OAI */
	sizeof(struct sadb_address),	/* SADB_X_EXT_NAT_T_OAR */
	sizeof(struct sadb_x_nat_t_frag),/* SADB_X_EXT_NAT_T_FRAG */
	sizeof(struct sadb_x_sa_replay), /* SADB_X_EXT_SA_REPLAY */
	sizeof(struct sadb_address),	/* SADB_X_EXT_NEW_ADDRESS_SRC */
	sizeof(struct sadb_address),	/* SADB_X_EXT_NEW_ADDRESS_DST */
};
_Static_assert(sizeof(minsize)/sizeof(int) == SADB_EXT_MAX + 1, "minsize size mismatch");

static const int maxsize[] = {
	sizeof(struct sadb_msg),	/* SADB_EXT_RESERVED */
	sizeof(struct sadb_sa),		/* SADB_EXT_SA */
	sizeof(struct sadb_lifetime),	/* SADB_EXT_LIFETIME_CURRENT */
	sizeof(struct sadb_lifetime),	/* SADB_EXT_LIFETIME_HARD */
	sizeof(struct sadb_lifetime),	/* SADB_EXT_LIFETIME_SOFT */
	0,				/* SADB_EXT_ADDRESS_SRC */
	0,				/* SADB_EXT_ADDRESS_DST */
	0,				/* SADB_EXT_ADDRESS_PROXY */
	0,				/* SADB_EXT_KEY_AUTH */
	0,				/* SADB_EXT_KEY_ENCRYPT */
	0,				/* SADB_EXT_IDENTITY_SRC */
	0,				/* SADB_EXT_IDENTITY_DST */
	0,				/* SADB_EXT_SENSITIVITY */
	0,				/* SADB_EXT_PROPOSAL */
	0,				/* SADB_EXT_SUPPORTED_AUTH */
	0,				/* SADB_EXT_SUPPORTED_ENCRYPT */
	sizeof(struct sadb_spirange),	/* SADB_EXT_SPIRANGE */
	0,				/* SADB_X_EXT_KMPRIVATE */
	0,				/* SADB_X_EXT_POLICY */
	sizeof(struct sadb_x_sa2),	/* SADB_X_SA2 */
	sizeof(struct sadb_x_nat_t_type),/* SADB_X_EXT_NAT_T_TYPE */
	sizeof(struct sadb_x_nat_t_port),/* SADB_X_EXT_NAT_T_SPORT */
	sizeof(struct sadb_x_nat_t_port),/* SADB_X_EXT_NAT_T_DPORT */
	0,				/* SADB_X_EXT_NAT_T_OAI */
	0,				/* SADB_X_EXT_NAT_T_OAR */
	sizeof(struct sadb_x_nat_t_frag),/* SADB_X_EXT_NAT_T_FRAG */
	sizeof(struct sadb_x_sa_replay), /* SADB_X_EXT_SA_REPLAY */
	0,				/* SADB_X_EXT_NEW_ADDRESS_SRC */
	0,				/* SADB_X_EXT_NEW_ADDRESS_DST */
};
_Static_assert(sizeof(maxsize)/sizeof(int) == SADB_EXT_MAX + 1, "minsize size mismatch");

/*
 * Internal values for SA flags:
 * SADB_X_EXT_F_CLONED means that SA was cloned by key_updateaddresses,
 *	thus we will not free the most of SA content in key_delsav().
 */
#define	SADB_X_EXT_F_CLONED	0x80000000

#define	SADB_CHECKLEN(_mhp, _ext)			\
    ((_mhp)->extlen[(_ext)] < minsize[(_ext)] || (maxsize[(_ext)] != 0 && \
	((_mhp)->extlen[(_ext)] > maxsize[(_ext)])))
#define	SADB_CHECKHDR(_mhp, _ext)	((_mhp)->ext[(_ext)] == NULL)

VNET_DEFINE_STATIC(int, ipsec_esp_keymin) = 256;
VNET_DEFINE_STATIC(int, ipsec_esp_auth) = 0;
VNET_DEFINE_STATIC(int, ipsec_ah_keymin) = 128;

#define	V_ipsec_esp_keymin	VNET(ipsec_esp_keymin)
#define	V_ipsec_esp_auth	VNET(ipsec_esp_auth)
#define	V_ipsec_ah_keymin	VNET(ipsec_ah_keymin)

#ifdef IPSEC_DEBUG
VNET_DEFINE(int, ipsec_debug) = 1;
#else
VNET_DEFINE(int, ipsec_debug) = 0;
#endif

#ifdef INET
SYSCTL_DECL(_net_inet_ipsec);
SYSCTL_INT(_net_inet_ipsec, IPSECCTL_DEBUG, debug,
    CTLFLAG_VNET | CTLFLAG_RW, &VNET_NAME(ipsec_debug), 0,
    "Enable IPsec debugging output when set.");
#endif
#ifdef INET6
SYSCTL_DECL(_net_inet6_ipsec6);
SYSCTL_INT(_net_inet6_ipsec6, IPSECCTL_DEBUG, debug,
    CTLFLAG_VNET | CTLFLAG_RW, &VNET_NAME(ipsec_debug), 0,
    "Enable IPsec debugging output when set.");
#endif

SYSCTL_DECL(_net_key);
SYSCTL_INT(_net_key, KEYCTL_DEBUG_LEVEL,	debug,
	CTLFLAG_VNET | CTLFLAG_RW, &VNET_NAME(key_debug_level), 0, "");

/* max count of trial for the decision of spi value */
SYSCTL_INT(_net_key, KEYCTL_SPI_TRY, spi_trycnt,
	CTLFLAG_VNET | CTLFLAG_RW, &VNET_NAME(key_spi_trycnt), 0, "");

/* minimum spi value to allocate automatically. */
SYSCTL_INT(_net_key, KEYCTL_SPI_MIN_VALUE, spi_minval,
	CTLFLAG_VNET | CTLFLAG_RW, &VNET_NAME(key_spi_minval), 0, "");

/* maximun spi value to allocate automatically. */
SYSCTL_INT(_net_key, KEYCTL_SPI_MAX_VALUE, spi_maxval,
	CTLFLAG_VNET | CTLFLAG_RW, &VNET_NAME(key_spi_maxval), 0, "");

/* interval to initialize randseed */
SYSCTL_INT(_net_key, KEYCTL_RANDOM_INT, int_random,
	CTLFLAG_VNET | CTLFLAG_RW, &VNET_NAME(key_int_random), 0, "");

/* lifetime for larval SA */
SYSCTL_INT(_net_key, KEYCTL_LARVAL_LIFETIME, larval_lifetime,
	CTLFLAG_VNET | CTLFLAG_RW, &VNET_NAME(key_larval_lifetime), 0, "");

/* counter for blocking to send SADB_ACQUIRE to IKEd */
SYSCTL_INT(_net_key, KEYCTL_BLOCKACQ_COUNT, blockacq_count,
	CTLFLAG_VNET | CTLFLAG_RW, &VNET_NAME(key_blockacq_count), 0, "");

/* lifetime for blocking to send SADB_ACQUIRE to IKEd */
SYSCTL_INT(_net_key, KEYCTL_BLOCKACQ_LIFETIME, blockacq_lifetime,
	CTLFLAG_VNET | CTLFLAG_RW, &VNET_NAME(key_blockacq_lifetime), 0, "");

/* ESP auth */
SYSCTL_INT(_net_key, KEYCTL_ESP_AUTH, esp_auth,
	CTLFLAG_VNET | CTLFLAG_RW, &VNET_NAME(ipsec_esp_auth), 0, "");

/* minimum ESP key length */
SYSCTL_INT(_net_key, KEYCTL_ESP_KEYMIN, esp_keymin,
	CTLFLAG_VNET | CTLFLAG_RW, &VNET_NAME(ipsec_esp_keymin), 0, "");

/* minimum AH key length */
SYSCTL_INT(_net_key, KEYCTL_AH_KEYMIN, ah_keymin,
	CTLFLAG_VNET | CTLFLAG_RW, &VNET_NAME(ipsec_ah_keymin), 0, "");

/* perfered old SA rather than new SA */
SYSCTL_INT(_net_key, KEYCTL_PREFERED_OLDSA, preferred_oldsa,
	CTLFLAG_VNET | CTLFLAG_RW, &VNET_NAME(key_preferred_oldsa), 0, "");

static SYSCTL_NODE(_net_key, OID_AUTO, spdcache, CTLFLAG_RW, 0, "SPD cache");

SYSCTL_UINT(_net_key_spdcache, OID_AUTO, maxentries,
	CTLFLAG_VNET | CTLFLAG_RDTUN, &VNET_NAME(key_spdcache_maxentries), 0,
	"Maximum number of entries in the SPD cache"
	" (power of 2, 0 to disable)");

SYSCTL_UINT(_net_key_spdcache, OID_AUTO, threshold,
	CTLFLAG_VNET | CTLFLAG_RDTUN, &VNET_NAME(key_spdcache_threshold), 0,
	"Number of SPs that make the SPD cache active");

#define __LIST_CHAINED(elm) \
	(!((elm)->chain.le_next == NULL && (elm)->chain.le_prev == NULL))

MALLOC_DEFINE(M_IPSEC_SA, "secasvar", "ipsec security association");
MALLOC_DEFINE(M_IPSEC_SAH, "sahead", "ipsec sa head");
MALLOC_DEFINE(M_IPSEC_SP, "ipsecpolicy", "ipsec security policy");
MALLOC_DEFINE(M_IPSEC_SR, "ipsecrequest", "ipsec security request");
MALLOC_DEFINE(M_IPSEC_MISC, "ipsec-misc", "ipsec miscellaneous");
MALLOC_DEFINE(M_IPSEC_SAQ, "ipsec-saq", "ipsec sa acquire");
MALLOC_DEFINE(M_IPSEC_SAR, "ipsec-reg", "ipsec sa acquire");
MALLOC_DEFINE(M_IPSEC_SPDCACHE, "ipsec-spdcache", "ipsec SPD cache");

VNET_DEFINE_STATIC(uma_zone_t, key_lft_zone);
#define	V_key_lft_zone		VNET(key_lft_zone)

/*
 * set parameters into secpolicyindex buffer.
 * Must allocate secpolicyindex buffer passed to this function.
 */
#define KEY_SETSECSPIDX(_dir, s, d, ps, pd, ulp, idx) \
do { \
	bzero((idx), sizeof(struct secpolicyindex));                         \
	(idx)->dir = (_dir);                                                 \
	(idx)->prefs = (ps);                                                 \
	(idx)->prefd = (pd);                                                 \
	(idx)->ul_proto = (ulp);                                             \
	bcopy((s), &(idx)->src, ((const struct sockaddr *)(s))->sa_len);     \
	bcopy((d), &(idx)->dst, ((const struct sockaddr *)(d))->sa_len);     \
} while (0)

/*
 * set parameters into secasindex buffer.
 * Must allocate secasindex buffer before calling this function.
 */
#define KEY_SETSECASIDX(p, m, r, s, d, idx) \
do { \
	bzero((idx), sizeof(struct secasindex));                             \
	(idx)->proto = (p);                                                  \
	(idx)->mode = (m);                                                   \
	(idx)->reqid = (r);                                                  \
	bcopy((s), &(idx)->src, ((const struct sockaddr *)(s))->sa_len);     \
	bcopy((d), &(idx)->dst, ((const struct sockaddr *)(d))->sa_len);     \
	key_porttosaddr(&(idx)->src.sa, 0);				     \
	key_porttosaddr(&(idx)->dst.sa, 0);				     \
} while (0)

/* key statistics */
struct _keystat {
	u_long getspi_count; /* the avarage of count to try to get new SPI */
} keystat;

struct sadb_msghdr {
	struct sadb_msg *msg;
	struct sadb_ext *ext[SADB_EXT_MAX + 1];
	int extoff[SADB_EXT_MAX + 1];
	int extlen[SADB_EXT_MAX + 1];
};

static struct supported_ealgs {
	int sadb_alg;
	const struct enc_xform *xform;
} supported_ealgs[] = {
	{ SADB_EALG_DESCBC,		&enc_xform_des },
	{ SADB_EALG_3DESCBC,		&enc_xform_3des },
	{ SADB_X_EALG_AES,		&enc_xform_rijndael128 },
	{ SADB_X_EALG_BLOWFISHCBC,	&enc_xform_blf },
	{ SADB_X_EALG_CAST128CBC,	&enc_xform_cast5 },
	{ SADB_EALG_NULL,		&enc_xform_null },
	{ SADB_X_EALG_CAMELLIACBC,	&enc_xform_camellia },
	{ SADB_X_EALG_AESCTR,		&enc_xform_aes_icm },
	{ SADB_X_EALG_AESGCM16,		&enc_xform_aes_nist_gcm },
	{ SADB_X_EALG_AESGMAC,		&enc_xform_aes_nist_gmac },
};

static struct supported_aalgs {
	int sadb_alg;
	const struct auth_hash *xform;
} supported_aalgs[] = {
	{ SADB_X_AALG_NULL,		&auth_hash_null },
	{ SADB_AALG_MD5HMAC,		&auth_hash_hmac_md5 },
	{ SADB_AALG_SHA1HMAC,		&auth_hash_hmac_sha1 },
	{ SADB_X_AALG_RIPEMD160HMAC,	&auth_hash_hmac_ripemd_160 },
	{ SADB_X_AALG_MD5,		&auth_hash_key_md5 },
	{ SADB_X_AALG_SHA,		&auth_hash_key_sha1 },
	{ SADB_X_AALG_SHA2_256,		&auth_hash_hmac_sha2_256 },
	{ SADB_X_AALG_SHA2_384,		&auth_hash_hmac_sha2_384 },
	{ SADB_X_AALG_SHA2_512,		&auth_hash_hmac_sha2_512 },
	{ SADB_X_AALG_AES128GMAC,	&auth_hash_nist_gmac_aes_128 },
	{ SADB_X_AALG_AES192GMAC,	&auth_hash_nist_gmac_aes_192 },
	{ SADB_X_AALG_AES256GMAC,	&auth_hash_nist_gmac_aes_256 },
};

static struct supported_calgs {
	int sadb_alg;
	const struct comp_algo *xform;
} supported_calgs[] = {
	{ SADB_X_CALG_DEFLATE,		&comp_algo_deflate },
};

#ifndef IPSEC_DEBUG2
static struct callout key_timer;
#endif

static void key_unlink(struct secpolicy *);
static struct secpolicy *key_do_allocsp(struct secpolicyindex *spidx, u_int dir);
static struct secpolicy *key_getsp(struct secpolicyindex *);
static struct secpolicy *key_getspbyid(u_int32_t);
static struct mbuf *key_gather_mbuf(struct mbuf *,
	const struct sadb_msghdr *, int, int, ...);
static int key_spdadd(struct socket *, struct mbuf *,
	const struct sadb_msghdr *);
static uint32_t key_getnewspid(void);
static int key_spddelete(struct socket *, struct mbuf *,
	const struct sadb_msghdr *);
static int key_spddelete2(struct socket *, struct mbuf *,
	const struct sadb_msghdr *);
static int key_spdget(struct socket *, struct mbuf *,
	const struct sadb_msghdr *);
static int key_spdflush(struct socket *, struct mbuf *,
	const struct sadb_msghdr *);
static int key_spddump(struct socket *, struct mbuf *,
	const struct sadb_msghdr *);
static struct mbuf *key_setdumpsp(struct secpolicy *,
	u_int8_t, u_int32_t, u_int32_t);
static struct mbuf *key_sp2mbuf(struct secpolicy *);
static size_t key_getspreqmsglen(struct secpolicy *);
static int key_spdexpire(struct secpolicy *);
static struct secashead *key_newsah(struct secasindex *);
static void key_freesah(struct secashead **);
static void key_delsah(struct secashead *);
static struct secasvar *key_newsav(const struct sadb_msghdr *,
    struct secasindex *, uint32_t, int *);
static void key_delsav(struct secasvar *);
static void key_unlinksav(struct secasvar *);
static struct secashead *key_getsah(struct secasindex *);
static int key_checkspidup(uint32_t);
static struct secasvar *key_getsavbyspi(uint32_t);
static int key_setnatt(struct secasvar *, const struct sadb_msghdr *);
static int key_setsaval(struct secasvar *, const struct sadb_msghdr *);
static int key_updatelifetimes(struct secasvar *, const struct sadb_msghdr *);
static int key_updateaddresses(struct socket *, struct mbuf *,
    const struct sadb_msghdr *, struct secasvar *, struct secasindex *);

static struct mbuf *key_setdumpsa(struct secasvar *, u_int8_t,
	u_int8_t, u_int32_t, u_int32_t);
static struct mbuf *key_setsadbmsg(u_int8_t, u_int16_t, u_int8_t,
	u_int32_t, pid_t, u_int16_t);
static struct mbuf *key_setsadbsa(struct secasvar *);
static struct mbuf *key_setsadbaddr(u_int16_t,
	const struct sockaddr *, u_int8_t, u_int16_t);
static struct mbuf *key_setsadbxport(u_int16_t, u_int16_t);
static struct mbuf *key_setsadbxtype(u_int16_t);
static struct mbuf *key_setsadbxsa2(u_int8_t, u_int32_t, u_int32_t);
static struct mbuf *key_setsadbxsareplay(u_int32_t);
static struct mbuf *key_setsadbxpolicy(u_int16_t, u_int8_t,
	u_int32_t, u_int32_t);
static struct seckey *key_dup_keymsg(const struct sadb_key *, size_t,
    struct malloc_type *);
static struct seclifetime *key_dup_lifemsg(const struct sadb_lifetime *src,
    struct malloc_type *);

/* flags for key_cmpsaidx() */
#define CMP_HEAD	1	/* protocol, addresses. */
#define CMP_MODE_REQID	2	/* additionally HEAD, reqid, mode. */
#define CMP_REQID	3	/* additionally HEAD, reaid. */
#define CMP_EXACTLY	4	/* all elements. */
static int key_cmpsaidx(const struct secasindex *,
    const struct secasindex *, int);
static int key_cmpspidx_exactly(struct secpolicyindex *,
    struct secpolicyindex *);
static int key_cmpspidx_withmask(struct secpolicyindex *,
    struct secpolicyindex *);
static int key_bbcmp(const void *, const void *, u_int);
static uint8_t key_satype2proto(uint8_t);
static uint8_t key_proto2satype(uint8_t);

static int key_getspi(struct socket *, struct mbuf *,
	const struct sadb_msghdr *);
static uint32_t key_do_getnewspi(struct sadb_spirange *, struct secasindex *);
static int key_update(struct socket *, struct mbuf *,
	const struct sadb_msghdr *);
static int key_add(struct socket *, struct mbuf *,
	const struct sadb_msghdr *);
static int key_setident(struct secashead *, const struct sadb_msghdr *);
static struct mbuf *key_getmsgbuf_x1(struct mbuf *,
	const struct sadb_msghdr *);
static int key_delete(struct socket *, struct mbuf *,
	const struct sadb_msghdr *);
static int key_delete_all(struct socket *, struct mbuf *,
	const struct sadb_msghdr *, struct secasindex *);
static int key_get(struct socket *, struct mbuf *,
	const struct sadb_msghdr *);

static void key_getcomb_setlifetime(struct sadb_comb *);
static struct mbuf *key_getcomb_ealg(void);
static struct mbuf *key_getcomb_ah(void);
static struct mbuf *key_getcomb_ipcomp(void);
static struct mbuf *key_getprop(const struct secasindex *);

static int key_acquire(const struct secasindex *, struct secpolicy *);
static uint32_t key_newacq(const struct secasindex *, int *);
static uint32_t key_getacq(const struct secasindex *, int *);
static int key_acqdone(const struct secasindex *, uint32_t);
static int key_acqreset(uint32_t);
static struct secspacq *key_newspacq(struct secpolicyindex *);
static struct secspacq *key_getspacq(struct secpolicyindex *);
static int key_acquire2(struct socket *, struct mbuf *,
	const struct sadb_msghdr *);
static int key_register(struct socket *, struct mbuf *,
	const struct sadb_msghdr *);
static int key_expire(struct secasvar *, int);
static int key_flush(struct socket *, struct mbuf *,
	const struct sadb_msghdr *);
static int key_dump(struct socket *, struct mbuf *,
	const struct sadb_msghdr *);
static int key_promisc(struct socket *, struct mbuf *,
	const struct sadb_msghdr *);
static int key_senderror(struct socket *, struct mbuf *, int);
static int key_validate_ext(const struct sadb_ext *, int);
static int key_align(struct mbuf *, struct sadb_msghdr *);
static struct mbuf *key_setlifetime(struct seclifetime *, uint16_t);
static struct mbuf *key_setkey(struct seckey *, uint16_t);

static void spdcache_init(void);
static void spdcache_clear(void);
static struct spdcache_entry *spdcache_entry_alloc(
	const struct secpolicyindex *spidx,
	struct secpolicy *policy);
static void spdcache_entry_free(struct spdcache_entry *entry);
#ifdef VIMAGE
static void spdcache_destroy(void);
#endif

#define	DBG_IPSEC_INITREF(t, p)	do {				\
	refcount_init(&(p)->refcnt, 1);				\
	KEYDBG(KEY_STAMP,					\
	    printf("%s: Initialize refcnt %s(%p) = %u\n",	\
	    __func__, #t, (p), (p)->refcnt));			\
} while (0)
#define	DBG_IPSEC_ADDREF(t, p)	do {				\
	refcount_acquire(&(p)->refcnt);				\
	KEYDBG(KEY_STAMP,					\
	    printf("%s: Acquire refcnt %s(%p) -> %u\n",		\
	    __func__, #t, (p), (p)->refcnt));			\
} while (0)
#define	DBG_IPSEC_DELREF(t, p)	do {				\
	KEYDBG(KEY_STAMP,					\
	    printf("%s: Release refcnt %s(%p) -> %u\n",		\
	    __func__, #t, (p), (p)->refcnt - 1));		\
	refcount_release(&(p)->refcnt);				\
} while (0)

#define	IPSEC_INITREF(t, p)	refcount_init(&(p)->refcnt, 1)
#define	IPSEC_ADDREF(t, p)	refcount_acquire(&(p)->refcnt)
#define	IPSEC_DELREF(t, p)	refcount_release(&(p)->refcnt)

#define	SP_INITREF(p)	IPSEC_INITREF(SP, p)
#define	SP_ADDREF(p)	IPSEC_ADDREF(SP, p)
#define	SP_DELREF(p)	IPSEC_DELREF(SP, p)

#define	SAH_INITREF(p)	IPSEC_INITREF(SAH, p)
#define	SAH_ADDREF(p)	IPSEC_ADDREF(SAH, p)
#define	SAH_DELREF(p)	IPSEC_DELREF(SAH, p)

#define	SAV_INITREF(p)	IPSEC_INITREF(SAV, p)
#define	SAV_ADDREF(p)	IPSEC_ADDREF(SAV, p)
#define	SAV_DELREF(p)	IPSEC_DELREF(SAV, p)

/*
 * Update the refcnt while holding the SPTREE lock.
 */
void
key_addref(struct secpolicy *sp)
{

	SP_ADDREF(sp);
}

/*
 * Return 0 when there are known to be no SP's for the specified
 * direction.  Otherwise return 1.  This is used by IPsec code
 * to optimize performance.
 */
int
key_havesp(u_int dir)
{

	return (dir == IPSEC_DIR_INBOUND || dir == IPSEC_DIR_OUTBOUND ?
		TAILQ_FIRST(&V_sptree[dir]) != NULL : 1);
}

/* %%% IPsec policy management */
/*
 * Return current SPDB generation.
 */
uint32_t
key_getspgen(void)
{

	return (V_sp_genid);
}

void
key_bumpspgen(void)
{

	V_sp_genid++;
}

static int
key_checksockaddrs(struct sockaddr *src, struct sockaddr *dst)
{

	/* family match */
	if (src->sa_family != dst->sa_family)
		return (EINVAL);
	/* sa_len match */
	if (src->sa_len != dst->sa_len)
		return (EINVAL);
	switch (src->sa_family) {
#ifdef INET
	case AF_INET:
		if (src->sa_len != sizeof(struct sockaddr_in))
			return (EINVAL);
		break;
#endif
#ifdef INET6
	case AF_INET6:
		if (src->sa_len != sizeof(struct sockaddr_in6))
			return (EINVAL);
		break;
#endif
	default:
		return (EAFNOSUPPORT);
	}
	return (0);
}

struct secpolicy *
key_do_allocsp(struct secpolicyindex *spidx, u_int dir)
{
	SPTREE_RLOCK_TRACKER;
	struct secpolicy *sp;

	IPSEC_ASSERT(spidx != NULL, ("null spidx"));
	IPSEC_ASSERT(dir == IPSEC_DIR_INBOUND || dir == IPSEC_DIR_OUTBOUND,
		("invalid direction %u", dir));

	SPTREE_RLOCK();
	TAILQ_FOREACH(sp, &V_sptree[dir], chain) {
		if (key_cmpspidx_withmask(&sp->spidx, spidx)) {
			SP_ADDREF(sp);
			break;
		}
	}
	SPTREE_RUNLOCK();
	return (sp);
}


/*
 * allocating a SP for OUTBOUND or INBOUND packet.
 * Must call key_freesp() later.
 * OUT:	NULL:	not found
 *	others:	found and return the pointer.
 */
struct secpolicy *
key_allocsp(struct secpolicyindex *spidx, u_int dir)
{
	struct spdcache_entry *entry, *lastentry, *tmpentry;
	struct secpolicy *sp;
	uint32_t hashv;
	int nb_entries;

	if (!SPDCACHE_ACTIVE()) {
		sp = key_do_allocsp(spidx, dir);
		goto out;
	}

	hashv = SPDCACHE_HASHVAL(spidx);
	SPDCACHE_LOCK(hashv);
	nb_entries = 0;
	LIST_FOREACH_SAFE(entry, &V_spdcachehashtbl[hashv], chain, tmpentry) {
		/* Removed outdated entries */
		if (entry->sp != NULL &&
		    entry->sp->state == IPSEC_SPSTATE_DEAD) {
			LIST_REMOVE(entry, chain);
			spdcache_entry_free(entry);
			continue;
		}

		nb_entries++;
		if (!key_cmpspidx_exactly(&entry->spidx, spidx)) {
			lastentry = entry;
			continue;
		}

		sp = entry->sp;
		if (entry->sp != NULL)
			SP_ADDREF(sp);

		/* IPSECSTAT_INC(ips_spdcache_hits); */

		SPDCACHE_UNLOCK(hashv);
		goto out;
	}

	/* IPSECSTAT_INC(ips_spdcache_misses); */

	sp = key_do_allocsp(spidx, dir);
	entry = spdcache_entry_alloc(spidx, sp);
	if (entry != NULL) {
		if (nb_entries >= SPDCACHE_MAX_ENTRIES_PER_HASH) {
			LIST_REMOVE(lastentry, chain);
			spdcache_entry_free(lastentry);
		}

		LIST_INSERT_HEAD(&V_spdcachehashtbl[hashv], entry, chain);
	}

	SPDCACHE_UNLOCK(hashv);

out:
	if (sp != NULL) {	/* found a SPD entry */
		sp->lastused = time_second;
		KEYDBG(IPSEC_STAMP,
		    printf("%s: return SP(%p)\n", __func__, sp));
		KEYDBG(IPSEC_DATA, kdebug_secpolicy(sp));
	} else {
		KEYDBG(IPSEC_DATA,
		    printf("%s: lookup failed for ", __func__);
		    kdebug_secpolicyindex(spidx, NULL));
	}
	return (sp);
}

/*
 * Allocating an SA entry for an *INBOUND* or *OUTBOUND* TCP packet, signed
 * or should be signed by MD5 signature.
 * We don't use key_allocsa() for such lookups, because we don't know SPI.
 * Unlike ESP and AH protocols, SPI isn't transmitted in the TCP header with
 * signed packet. We use SADB only as storage for password.
 * OUT:	positive:	corresponding SA for given saidx found.
 *	NULL:		SA not found
 */
struct secasvar *
key_allocsa_tcpmd5(struct secasindex *saidx)
{
	SAHTREE_RLOCK_TRACKER;
	struct secashead *sah;
	struct secasvar *sav;

	IPSEC_ASSERT(saidx->proto == IPPROTO_TCP,
	    ("unexpected security protocol %u", saidx->proto));
	IPSEC_ASSERT(saidx->mode == IPSEC_MODE_TCPMD5,
	    ("unexpected mode %u", saidx->mode));

	SAHTREE_RLOCK();
	LIST_FOREACH(sah, SAHADDRHASH_HASH(saidx), addrhash) {
		KEYDBG(IPSEC_DUMP,
		    printf("%s: checking SAH\n", __func__);
		    kdebug_secash(sah, "  "));
		if (sah->saidx.proto != IPPROTO_TCP)
			continue;
		if (!key_sockaddrcmp(&saidx->dst.sa, &sah->saidx.dst.sa, 0) &&
		    !key_sockaddrcmp(&saidx->src.sa, &sah->saidx.src.sa, 0))
			break;
	}
	if (sah != NULL) {
		if (V_key_preferred_oldsa)
			sav = TAILQ_LAST(&sah->savtree_alive, secasvar_queue);
		else
			sav = TAILQ_FIRST(&sah->savtree_alive);
		if (sav != NULL)
			SAV_ADDREF(sav);
	} else
		sav = NULL;
	SAHTREE_RUNLOCK();

	if (sav != NULL) {
		KEYDBG(IPSEC_STAMP,
		    printf("%s: return SA(%p)\n", __func__, sav));
		KEYDBG(IPSEC_DATA, kdebug_secasv(sav));
	} else {
		KEYDBG(IPSEC_STAMP,
		    printf("%s: SA not found\n", __func__));
		KEYDBG(IPSEC_DATA, kdebug_secasindex(saidx, NULL));
	}
	return (sav);
}

/*
 * Allocating an SA entry for an *OUTBOUND* packet.
 * OUT:	positive:	corresponding SA for given saidx found.
 *	NULL:		SA not found, but will be acquired, check *error
 *			for acquiring status.
 */
struct secasvar *
key_allocsa_policy(struct secpolicy *sp, const struct secasindex *saidx,
    int *error)
{
	SAHTREE_RLOCK_TRACKER;
	struct secashead *sah;
	struct secasvar *sav;

	IPSEC_ASSERT(saidx != NULL, ("null saidx"));
	IPSEC_ASSERT(saidx->mode == IPSEC_MODE_TRANSPORT ||
		saidx->mode == IPSEC_MODE_TUNNEL,
		("unexpected policy %u", saidx->mode));

	/*
	 * We check new SA in the IPsec request because a different
	 * SA may be involved each time this request is checked, either
	 * because new SAs are being configured, or this request is
	 * associated with an unconnected datagram socket, or this request
	 * is associated with a system default policy.
	 */
	SAHTREE_RLOCK();
	LIST_FOREACH(sah, SAHADDRHASH_HASH(saidx), addrhash) {
		KEYDBG(IPSEC_DUMP,
		    printf("%s: checking SAH\n", __func__);
		    kdebug_secash(sah, "  "));
		if (key_cmpsaidx(&sah->saidx, saidx, CMP_MODE_REQID))
			break;

	}
	if (sah != NULL) {
		/*
		 * Allocate the oldest SA available according to
		 * draft-jenkins-ipsec-rekeying-03.
		 */
		if (V_key_preferred_oldsa)
			sav = TAILQ_LAST(&sah->savtree_alive, secasvar_queue);
		else
			sav = TAILQ_FIRST(&sah->savtree_alive);
		if (sav != NULL)
			SAV_ADDREF(sav);
	} else
		sav = NULL;
	SAHTREE_RUNLOCK();

	if (sav != NULL) {
		*error = 0;
		KEYDBG(IPSEC_STAMP,
		    printf("%s: chosen SA(%p) for SP(%p)\n", __func__,
			sav, sp));
		KEYDBG(IPSEC_DATA, kdebug_secasv(sav));
		return (sav); /* return referenced SA */
	}

	/* there is no SA */
	*error = key_acquire(saidx, sp);
	if ((*error) != 0)
		ipseclog((LOG_DEBUG,
		    "%s: error %d returned from key_acquire()\n",
			__func__, *error));
	KEYDBG(IPSEC_STAMP,
	    printf("%s: acquire SA for SP(%p), error %d\n",
		__func__, sp, *error));
	KEYDBG(IPSEC_DATA, kdebug_secasindex(saidx, NULL));
	return (NULL);
}

/*
 * allocating a usable SA entry for a *INBOUND* packet.
 * Must call key_freesav() later.
 * OUT: positive:	pointer to a usable sav (i.e. MATURE or DYING state).
 *	NULL:		not found, or error occurred.
 *
 * According to RFC 2401 SA is uniquely identified by a triple SPI,
 * destination address, and security protocol. But according to RFC 4301,
 * SPI by itself suffices to specify an SA.
 *
 * Note that, however, we do need to keep source address in IPsec SA.
 * IKE specification and PF_KEY specification do assume that we
 * keep source address in IPsec SA.  We see a tricky situation here.
 */
struct secasvar *
key_allocsa(union sockaddr_union *dst, uint8_t proto, uint32_t spi)
{
	SAHTREE_RLOCK_TRACKER;
	struct secasvar *sav;

	IPSEC_ASSERT(proto == IPPROTO_ESP || proto == IPPROTO_AH ||
	    proto == IPPROTO_IPCOMP, ("unexpected security protocol %u",
	    proto));

	SAHTREE_RLOCK();
	LIST_FOREACH(sav, SAVHASH_HASH(spi), spihash) {
		if (sav->spi == spi)
			break;
	}
	/*
	 * We use single SPI namespace for all protocols, so it is
	 * impossible to have SPI duplicates in the SAVHASH.
	 */
	if (sav != NULL) {
		if (sav->state != SADB_SASTATE_LARVAL &&
		    sav->sah->saidx.proto == proto &&
		    key_sockaddrcmp(&dst->sa,
			&sav->sah->saidx.dst.sa, 0) == 0)
			SAV_ADDREF(sav);
		else
			sav = NULL;
	}
	SAHTREE_RUNLOCK();

	if (sav == NULL) {
		KEYDBG(IPSEC_STAMP,
		    char buf[IPSEC_ADDRSTRLEN];
		    printf("%s: SA not found for spi %u proto %u dst %s\n",
			__func__, ntohl(spi), proto, ipsec_address(dst, buf,
			sizeof(buf))));
	} else {
		KEYDBG(IPSEC_STAMP,
		    printf("%s: return SA(%p)\n", __func__, sav));
		KEYDBG(IPSEC_DATA, kdebug_secasv(sav));
	}
	return (sav);
}

struct secasvar *
key_allocsa_tunnel(union sockaddr_union *src, union sockaddr_union *dst,
    uint8_t proto)
{
	SAHTREE_RLOCK_TRACKER;
	struct secasindex saidx;
	struct secashead *sah;
	struct secasvar *sav;

	IPSEC_ASSERT(src != NULL, ("null src address"));
	IPSEC_ASSERT(dst != NULL, ("null dst address"));

	KEY_SETSECASIDX(proto, IPSEC_MODE_TUNNEL, 0, &src->sa,
	    &dst->sa, &saidx);

	sav = NULL;
	SAHTREE_RLOCK();
	LIST_FOREACH(sah, SAHADDRHASH_HASH(&saidx), addrhash) {
		if (IPSEC_MODE_TUNNEL != sah->saidx.mode)
			continue;
		if (proto != sah->saidx.proto)
			continue;
		if (key_sockaddrcmp(&src->sa, &sah->saidx.src.sa, 0) != 0)
			continue;
		if (key_sockaddrcmp(&dst->sa, &sah->saidx.dst.sa, 0) != 0)
			continue;
		/* XXXAE: is key_preferred_oldsa reasonably?*/
		if (V_key_preferred_oldsa)
			sav = TAILQ_LAST(&sah->savtree_alive, secasvar_queue);
		else
			sav = TAILQ_FIRST(&sah->savtree_alive);
		if (sav != NULL) {
			SAV_ADDREF(sav);
			break;
		}
	}
	SAHTREE_RUNLOCK();
	KEYDBG(IPSEC_STAMP,
	    printf("%s: return SA(%p)\n", __func__, sav));
	if (sav != NULL)
		KEYDBG(IPSEC_DATA, kdebug_secasv(sav));
	return (sav);
}

/*
 * Must be called after calling key_allocsp().
 */
void
key_freesp(struct secpolicy **spp)
{
	struct secpolicy *sp = *spp;

	IPSEC_ASSERT(sp != NULL, ("null sp"));
	if (SP_DELREF(sp) == 0)
		return;

	KEYDBG(IPSEC_STAMP,
	    printf("%s: last reference to SP(%p)\n", __func__, sp));
	KEYDBG(IPSEC_DATA, kdebug_secpolicy(sp));

	*spp = NULL;
	while (sp->tcount > 0)
		ipsec_delisr(sp->req[--sp->tcount]);
	free(sp, M_IPSEC_SP);
}

static void
key_unlink(struct secpolicy *sp)
{

	IPSEC_ASSERT(sp->spidx.dir == IPSEC_DIR_INBOUND ||
	    sp->spidx.dir == IPSEC_DIR_OUTBOUND,
	    ("invalid direction %u", sp->spidx.dir));
	SPTREE_UNLOCK_ASSERT();

	KEYDBG(KEY_STAMP,
	    printf("%s: SP(%p)\n", __func__, sp));
	SPTREE_WLOCK();
	if (sp->state != IPSEC_SPSTATE_ALIVE) {
		/* SP is already unlinked */
		SPTREE_WUNLOCK();
		return;
	}
	sp->state = IPSEC_SPSTATE_DEAD;
	TAILQ_REMOVE(&V_sptree[sp->spidx.dir], sp, chain);
	V_spd_size--;
	LIST_REMOVE(sp, idhash);
	V_sp_genid++;
	SPTREE_WUNLOCK();
	if (SPDCACHE_ENABLED())
		spdcache_clear();
	key_freesp(&sp);
}

/*
 * insert a secpolicy into the SP database. Lower priorities first
 */
static void
key_insertsp(struct secpolicy *newsp)
{
	struct secpolicy *sp;

	SPTREE_WLOCK_ASSERT();
	TAILQ_FOREACH(sp, &V_sptree[newsp->spidx.dir], chain) {
		if (newsp->priority < sp->priority) {
			TAILQ_INSERT_BEFORE(sp, newsp, chain);
			goto done;
		}
	}
	TAILQ_INSERT_TAIL(&V_sptree[newsp->spidx.dir], newsp, chain);
done:
	LIST_INSERT_HEAD(SPHASH_HASH(newsp->id), newsp, idhash);
	newsp->state = IPSEC_SPSTATE_ALIVE;
	V_spd_size++;
	V_sp_genid++;
}

/*
 * Insert a bunch of VTI secpolicies into the SPDB.
 * We keep VTI policies in the separate list due to following reasons:
 * 1) they should be immutable to user's or some deamon's attempts to
 *    delete. The only way delete such policies - destroy or unconfigure
 *    corresponding virtual inteface.
 * 2) such policies have traffic selector that matches all traffic per
 *    address family.
 * Since all VTI policies have the same priority, we don't care about
 * policies order.
 */
int
key_register_ifnet(struct secpolicy **spp, u_int count)
{
	struct mbuf *m;
	u_int i;

	SPTREE_WLOCK();
	/*
	 * First of try to acquire id for each SP.
	 */
	for (i = 0; i < count; i++) {
		IPSEC_ASSERT(spp[i]->spidx.dir == IPSEC_DIR_INBOUND ||
		    spp[i]->spidx.dir == IPSEC_DIR_OUTBOUND,
		    ("invalid direction %u", spp[i]->spidx.dir));

		if ((spp[i]->id = key_getnewspid()) == 0) {
			SPTREE_WUNLOCK();
			return (EAGAIN);
		}
	}
	for (i = 0; i < count; i++) {
		TAILQ_INSERT_TAIL(&V_sptree_ifnet[spp[i]->spidx.dir],
		    spp[i], chain);
		/*
		 * NOTE: despite the fact that we keep VTI SP in the
		 * separate list, SPHASH contains policies from both
		 * sources. Thus SADB_X_SPDGET will correctly return
		 * SP by id, because it uses SPHASH for lookups.
		 */
		LIST_INSERT_HEAD(SPHASH_HASH(spp[i]->id), spp[i], idhash);
		spp[i]->state = IPSEC_SPSTATE_IFNET;
	}
	SPTREE_WUNLOCK();
	/*
	 * Notify user processes about new SP.
	 */
	for (i = 0; i < count; i++) {
		m = key_setdumpsp(spp[i], SADB_X_SPDADD, 0, 0);
		if (m != NULL)
			key_sendup_mbuf(NULL, m, KEY_SENDUP_ALL);
	}
	return (0);
}

void
key_unregister_ifnet(struct secpolicy **spp, u_int count)
{
	struct mbuf *m;
	u_int i;

	SPTREE_WLOCK();
	for (i = 0; i < count; i++) {
		IPSEC_ASSERT(spp[i]->spidx.dir == IPSEC_DIR_INBOUND ||
		    spp[i]->spidx.dir == IPSEC_DIR_OUTBOUND,
		    ("invalid direction %u", spp[i]->spidx.dir));

		if (spp[i]->state != IPSEC_SPSTATE_IFNET)
			continue;
		spp[i]->state = IPSEC_SPSTATE_DEAD;
		TAILQ_REMOVE(&V_sptree_ifnet[spp[i]->spidx.dir],
		    spp[i], chain);
		V_spd_size--;
		LIST_REMOVE(spp[i], idhash);
	}
	SPTREE_WUNLOCK();
	if (SPDCACHE_ENABLED())
		spdcache_clear();

	for (i = 0; i < count; i++) {
		m = key_setdumpsp(spp[i], SADB_X_SPDDELETE, 0, 0);
		if (m != NULL)
			key_sendup_mbuf(NULL, m, KEY_SENDUP_ALL);
	}
}

/*
 * Must be called after calling key_allocsa().
 * This function is called by key_freesp() to free some SA allocated
 * for a policy.
 */
void
key_freesav(struct secasvar **psav)
{
	struct secasvar *sav = *psav;

	IPSEC_ASSERT(sav != NULL, ("null sav"));
	if (SAV_DELREF(sav) == 0)
		return;

	KEYDBG(IPSEC_STAMP,
	    printf("%s: last reference to SA(%p)\n", __func__, sav));

	*psav = NULL;
	key_delsav(sav);
}

/*
 * Unlink SA from SAH and SPI hash under SAHTREE_WLOCK.
 * Expect that SA has extra reference due to lookup.
 * Release this references, also release SAH reference after unlink.
 */
static void
key_unlinksav(struct secasvar *sav)
{
	struct secashead *sah;

	KEYDBG(KEY_STAMP,
	    printf("%s: SA(%p)\n", __func__, sav));

	SAHTREE_UNLOCK_ASSERT();
	SAHTREE_WLOCK();
	if (sav->state == SADB_SASTATE_DEAD) {
		/* SA is already unlinked */
		SAHTREE_WUNLOCK();
		return;
	}
	/* Unlink from SAH */
	if (sav->state == SADB_SASTATE_LARVAL)
		TAILQ_REMOVE(&sav->sah->savtree_larval, sav, chain);
	else
		TAILQ_REMOVE(&sav->sah->savtree_alive, sav, chain);
	/* Unlink from SPI hash */
	LIST_REMOVE(sav, spihash);
	sav->state = SADB_SASTATE_DEAD;
	sah = sav->sah;
	SAHTREE_WUNLOCK();
	key_freesav(&sav);
	/* Since we are unlinked, release reference to SAH */
	key_freesah(&sah);
}

/* %%% SPD management */
/*
 * search SPD
 * OUT:	NULL	: not found
 *	others	: found, pointer to a SP.
 */
static struct secpolicy *
key_getsp(struct secpolicyindex *spidx)
{
	SPTREE_RLOCK_TRACKER;
	struct secpolicy *sp;

	IPSEC_ASSERT(spidx != NULL, ("null spidx"));

	SPTREE_RLOCK();
	TAILQ_FOREACH(sp, &V_sptree[spidx->dir], chain) {
		if (key_cmpspidx_exactly(spidx, &sp->spidx)) {
			SP_ADDREF(sp);
			break;
		}
	}
	SPTREE_RUNLOCK();

	return sp;
}

/*
 * get SP by index.
 * OUT:	NULL	: not found
 *	others	: found, pointer to referenced SP.
 */
static struct secpolicy *
key_getspbyid(uint32_t id)
{
	SPTREE_RLOCK_TRACKER;
	struct secpolicy *sp;

	SPTREE_RLOCK();
	LIST_FOREACH(sp, SPHASH_HASH(id), idhash) {
		if (sp->id == id) {
			SP_ADDREF(sp);
			break;
		}
	}
	SPTREE_RUNLOCK();
	return (sp);
}

struct secpolicy *
key_newsp(void)
{
	struct secpolicy *sp;

	sp = malloc(sizeof(*sp), M_IPSEC_SP, M_NOWAIT | M_ZERO);
	if (sp != NULL)
		SP_INITREF(sp);
	return (sp);
}

struct ipsecrequest *
ipsec_newisr(void)
{

	return (malloc(sizeof(struct ipsecrequest), M_IPSEC_SR,
	    M_NOWAIT | M_ZERO));
}

void
ipsec_delisr(struct ipsecrequest *p)
{

	free(p, M_IPSEC_SR);
}

/*
 * create secpolicy structure from sadb_x_policy structure.
 * NOTE: `state', `secpolicyindex' and 'id' in secpolicy structure
 * are not set, so must be set properly later.
 */
struct secpolicy *
key_msg2sp(struct sadb_x_policy *xpl0, size_t len, int *error)
{
	struct secpolicy *newsp;

	IPSEC_ASSERT(xpl0 != NULL, ("null xpl0"));
	IPSEC_ASSERT(len >= sizeof(*xpl0), ("policy too short: %zu", len));

	if (len != PFKEY_EXTLEN(xpl0)) {
		ipseclog((LOG_DEBUG, "%s: Invalid msg length.\n", __func__));
		*error = EINVAL;
		return NULL;
	}

	if ((newsp = key_newsp()) == NULL) {
		*error = ENOBUFS;
		return NULL;
	}

	newsp->spidx.dir = xpl0->sadb_x_policy_dir;
	newsp->policy = xpl0->sadb_x_policy_type;
	newsp->priority = xpl0->sadb_x_policy_priority;
	newsp->tcount = 0;

	/* check policy */
	switch (xpl0->sadb_x_policy_type) {
	case IPSEC_POLICY_DISCARD:
	case IPSEC_POLICY_NONE:
	case IPSEC_POLICY_ENTRUST:
	case IPSEC_POLICY_BYPASS:
		break;

	case IPSEC_POLICY_IPSEC:
	    {
		struct sadb_x_ipsecrequest *xisr;
		struct ipsecrequest *isr;
		int tlen;

		/* validity check */
		if (PFKEY_EXTLEN(xpl0) < sizeof(*xpl0)) {
			ipseclog((LOG_DEBUG, "%s: Invalid msg length.\n",
				__func__));
			key_freesp(&newsp);
			*error = EINVAL;
			return NULL;
		}

		tlen = PFKEY_EXTLEN(xpl0) - sizeof(*xpl0);
		xisr = (struct sadb_x_ipsecrequest *)(xpl0 + 1);

		while (tlen > 0) {
			/* length check */
			if (xisr->sadb_x_ipsecrequest_len < sizeof(*xisr) ||
			    xisr->sadb_x_ipsecrequest_len > tlen) {
				ipseclog((LOG_DEBUG, "%s: invalid ipsecrequest "
					"length.\n", __func__));
				key_freesp(&newsp);
				*error = EINVAL;
				return NULL;
			}

			if (newsp->tcount >= IPSEC_MAXREQ) {
				ipseclog((LOG_DEBUG,
				    "%s: too many ipsecrequests.\n",
				    __func__));
				key_freesp(&newsp);
				*error = EINVAL;
				return (NULL);
			}

			/* allocate request buffer */
			/* NB: data structure is zero'd */
			isr = ipsec_newisr();
			if (isr == NULL) {
				ipseclog((LOG_DEBUG,
				    "%s: No more memory.\n", __func__));
				key_freesp(&newsp);
				*error = ENOBUFS;
				return NULL;
			}

			newsp->req[newsp->tcount++] = isr;

			/* set values */
			switch (xisr->sadb_x_ipsecrequest_proto) {
			case IPPROTO_ESP:
			case IPPROTO_AH:
			case IPPROTO_IPCOMP:
				break;
			default:
				ipseclog((LOG_DEBUG,
				    "%s: invalid proto type=%u\n", __func__,
				    xisr->sadb_x_ipsecrequest_proto));
				key_freesp(&newsp);
				*error = EPROTONOSUPPORT;
				return NULL;
			}
			isr->saidx.proto =
			    (uint8_t)xisr->sadb_x_ipsecrequest_proto;

			switch (xisr->sadb_x_ipsecrequest_mode) {
			case IPSEC_MODE_TRANSPORT:
			case IPSEC_MODE_TUNNEL:
				break;
			case IPSEC_MODE_ANY:
			default:
				ipseclog((LOG_DEBUG,
				    "%s: invalid mode=%u\n", __func__,
				    xisr->sadb_x_ipsecrequest_mode));
				key_freesp(&newsp);
				*error = EINVAL;
				return NULL;
			}
			isr->saidx.mode = xisr->sadb_x_ipsecrequest_mode;

			switch (xisr->sadb_x_ipsecrequest_level) {
			case IPSEC_LEVEL_DEFAULT:
			case IPSEC_LEVEL_USE:
			case IPSEC_LEVEL_REQUIRE:
				break;
			case IPSEC_LEVEL_UNIQUE:
				/* validity check */
				/*
				 * If range violation of reqid, kernel will
				 * update it, don't refuse it.
				 */
				if (xisr->sadb_x_ipsecrequest_reqid
						> IPSEC_MANUAL_REQID_MAX) {
					ipseclog((LOG_DEBUG,
					    "%s: reqid=%d range "
					    "violation, updated by kernel.\n",
					    __func__,
					    xisr->sadb_x_ipsecrequest_reqid));
					xisr->sadb_x_ipsecrequest_reqid = 0;
				}

				/* allocate new reqid id if reqid is zero. */
				if (xisr->sadb_x_ipsecrequest_reqid == 0) {
					u_int32_t reqid;
					if ((reqid = key_newreqid()) == 0) {
						key_freesp(&newsp);
						*error = ENOBUFS;
						return NULL;
					}
					isr->saidx.reqid = reqid;
					xisr->sadb_x_ipsecrequest_reqid = reqid;
				} else {
				/* set it for manual keying. */
					isr->saidx.reqid =
					    xisr->sadb_x_ipsecrequest_reqid;
				}
				break;

			default:
				ipseclog((LOG_DEBUG, "%s: invalid level=%u\n",
					__func__,
					xisr->sadb_x_ipsecrequest_level));
				key_freesp(&newsp);
				*error = EINVAL;
				return NULL;
			}
			isr->level = xisr->sadb_x_ipsecrequest_level;

			/* set IP addresses if there */
			if (xisr->sadb_x_ipsecrequest_len > sizeof(*xisr)) {
				struct sockaddr *paddr;

				len = tlen - sizeof(*xisr);
				paddr = (struct sockaddr *)(xisr + 1);
				/* validity check */
				if (len < sizeof(struct sockaddr) ||
				    len < 2 * paddr->sa_len ||
				    paddr->sa_len > sizeof(isr->saidx.src)) {
					ipseclog((LOG_DEBUG, "%s: invalid "
						"request address length.\n",
						__func__));
					key_freesp(&newsp);
					*error = EINVAL;
					return NULL;
				}
				/*
				 * Request length should be enough to keep
				 * source and destination addresses.
				 */
				if (xisr->sadb_x_ipsecrequest_len <
				    sizeof(*xisr) + 2 * paddr->sa_len) {
					ipseclog((LOG_DEBUG, "%s: invalid "
					    "ipsecrequest length.\n",
					    __func__));
					key_freesp(&newsp);
					*error = EINVAL;
					return (NULL);
				}
				bcopy(paddr, &isr->saidx.src, paddr->sa_len);
				paddr = (struct sockaddr *)((caddr_t)paddr +
				    paddr->sa_len);

				/* validity check */
				if (paddr->sa_len !=
				    isr->saidx.src.sa.sa_len) {
					ipseclog((LOG_DEBUG, "%s: invalid "
						"request address length.\n",
						__func__));
					key_freesp(&newsp);
					*error = EINVAL;
					return NULL;
				}
				/* AF family should match */
				if (paddr->sa_family !=
				    isr->saidx.src.sa.sa_family) {
					ipseclog((LOG_DEBUG, "%s: address "
					    "family doesn't match.\n",
						__func__));
					key_freesp(&newsp);
					*error = EINVAL;
					return (NULL);
				}
				bcopy(paddr, &isr->saidx.dst, paddr->sa_len);
			} else {
				/*
				 * Addresses for TUNNEL mode requests are
				 * mandatory.
				 */
				if (isr->saidx.mode == IPSEC_MODE_TUNNEL) {
					ipseclog((LOG_DEBUG, "%s: missing "
					    "request addresses.\n", __func__));
					key_freesp(&newsp);
					*error = EINVAL;
					return (NULL);
				}
			}
			tlen -= xisr->sadb_x_ipsecrequest_len;

			/* validity check */
			if (tlen < 0) {
				ipseclog((LOG_DEBUG, "%s: becoming tlen < 0.\n",
					__func__));
				key_freesp(&newsp);
				*error = EINVAL;
				return NULL;
			}

			xisr = (struct sadb_x_ipsecrequest *)((caddr_t)xisr
			                 + xisr->sadb_x_ipsecrequest_len);
		}
		/* XXXAE: LARVAL SP */
		if (newsp->tcount < 1) {
			ipseclog((LOG_DEBUG, "%s: valid IPSEC transforms "
			    "not found.\n", __func__));
			key_freesp(&newsp);
			*error = EINVAL;
			return (NULL);
		}
	    }
		break;
	default:
		ipseclog((LOG_DEBUG, "%s: invalid policy type.\n", __func__));
		key_freesp(&newsp);
		*error = EINVAL;
		return NULL;
	}

	*error = 0;
	return (newsp);
}

uint32_t
key_newreqid(void)
{
	static uint32_t auto_reqid = IPSEC_MANUAL_REQID_MAX + 1;

	if (auto_reqid == ~0)
		auto_reqid = IPSEC_MANUAL_REQID_MAX + 1;
	else
		auto_reqid++;

	/* XXX should be unique check */
	return (auto_reqid);
}

/*
 * copy secpolicy struct to sadb_x_policy structure indicated.
 */
static struct mbuf *
key_sp2mbuf(struct secpolicy *sp)
{
	struct mbuf *m;
	size_t tlen;

	tlen = key_getspreqmsglen(sp);
	m = m_get2(tlen, M_NOWAIT, MT_DATA, 0);
	if (m == NULL)
		return (NULL);
	m_align(m, tlen);
	m->m_len = tlen;
	if (key_sp2msg(sp, m->m_data, &tlen) != 0) {
		m_freem(m);
		return (NULL);
	}
	return (m);
}

int
key_sp2msg(struct secpolicy *sp, void *request, size_t *len)
{
	struct sadb_x_ipsecrequest *xisr;
	struct sadb_x_policy *xpl;
	struct ipsecrequest *isr;
	size_t xlen, ilen;
	caddr_t p;
	int error, i;

	IPSEC_ASSERT(sp != NULL, ("null policy"));

	xlen = sizeof(*xpl);
	if (*len < xlen)
		return (EINVAL);

	error = 0;
	bzero(request, *len);
	xpl = (struct sadb_x_policy *)request;
	xpl->sadb_x_policy_exttype = SADB_X_EXT_POLICY;
	xpl->sadb_x_policy_type = sp->policy;
	xpl->sadb_x_policy_dir = sp->spidx.dir;
	xpl->sadb_x_policy_id = sp->id;
	xpl->sadb_x_policy_priority = sp->priority;
	switch (sp->state) {
	case IPSEC_SPSTATE_IFNET:
		xpl->sadb_x_policy_scope = IPSEC_POLICYSCOPE_IFNET;
		break;
	case IPSEC_SPSTATE_PCB:
		xpl->sadb_x_policy_scope = IPSEC_POLICYSCOPE_PCB;
		break;
	default:
		xpl->sadb_x_policy_scope = IPSEC_POLICYSCOPE_GLOBAL;
	}

	/* if is the policy for ipsec ? */
	if (sp->policy == IPSEC_POLICY_IPSEC) {
		p = (caddr_t)xpl + sizeof(*xpl);
		for (i = 0; i < sp->tcount; i++) {
			isr = sp->req[i];
			ilen = PFKEY_ALIGN8(sizeof(*xisr) +
			    isr->saidx.src.sa.sa_len +
			    isr->saidx.dst.sa.sa_len);
			xlen += ilen;
			if (xlen > *len) {
				error = ENOBUFS;
				/* Calculate needed size */
				continue;
			}
			xisr = (struct sadb_x_ipsecrequest *)p;
			xisr->sadb_x_ipsecrequest_len = ilen;
			xisr->sadb_x_ipsecrequest_proto = isr->saidx.proto;
			xisr->sadb_x_ipsecrequest_mode = isr->saidx.mode;
			xisr->sadb_x_ipsecrequest_level = isr->level;
			xisr->sadb_x_ipsecrequest_reqid = isr->saidx.reqid;

			p += sizeof(*xisr);
			bcopy(&isr->saidx.src, p, isr->saidx.src.sa.sa_len);
			p += isr->saidx.src.sa.sa_len;
			bcopy(&isr->saidx.dst, p, isr->saidx.dst.sa.sa_len);
			p += isr->saidx.dst.sa.sa_len;
		}
	}
	xpl->sadb_x_policy_len = PFKEY_UNIT64(xlen);
	if (error == 0)
		*len = xlen;
	else
		*len = sizeof(*xpl);
	return (error);
}

/* m will not be freed nor modified */
static struct mbuf *
key_gather_mbuf(struct mbuf *m, const struct sadb_msghdr *mhp,
    int ndeep, int nitem, ...)
{
	va_list ap;
	int idx;
	int i;
	struct mbuf *result = NULL, *n;
	int len;

	IPSEC_ASSERT(m != NULL, ("null mbuf"));
	IPSEC_ASSERT(mhp != NULL, ("null msghdr"));

	va_start(ap, nitem);
	for (i = 0; i < nitem; i++) {
		idx = va_arg(ap, int);
		if (idx < 0 || idx > SADB_EXT_MAX)
			goto fail;
		/* don't attempt to pull empty extension */
		if (idx == SADB_EXT_RESERVED && mhp->msg == NULL)
			continue;
		if (idx != SADB_EXT_RESERVED  &&
		    (mhp->ext[idx] == NULL || mhp->extlen[idx] == 0))
			continue;

		if (idx == SADB_EXT_RESERVED) {
			len = PFKEY_ALIGN8(sizeof(struct sadb_msg));

			IPSEC_ASSERT(len <= MHLEN, ("header too big %u", len));

			MGETHDR(n, M_NOWAIT, MT_DATA);
			if (!n)
				goto fail;
			n->m_len = len;
			n->m_next = NULL;
			m_copydata(m, 0, sizeof(struct sadb_msg),
			    mtod(n, caddr_t));
		} else if (i < ndeep) {
			len = mhp->extlen[idx];
			n = m_get2(len, M_NOWAIT, MT_DATA, 0);
			if (n == NULL)
				goto fail;
			m_align(n, len);
			n->m_len = len;
			m_copydata(m, mhp->extoff[idx], mhp->extlen[idx],
			    mtod(n, caddr_t));
		} else {
			n = m_copym(m, mhp->extoff[idx], mhp->extlen[idx],
			    M_NOWAIT);
		}
		if (n == NULL)
			goto fail;

		if (result)
			m_cat(result, n);
		else
			result = n;
	}
	va_end(ap);

	if ((result->m_flags & M_PKTHDR) != 0) {
		result->m_pkthdr.len = 0;
		for (n = result; n; n = n->m_next)
			result->m_pkthdr.len += n->m_len;
	}

	return result;

fail:
	m_freem(result);
	va_end(ap);
	return NULL;
}

/*
 * SADB_X_SPDADD, SADB_X_SPDSETIDX or SADB_X_SPDUPDATE processing
 * add an entry to SP database, when received
 *   <base, address(SD), (lifetime(H),) policy>
 * from the user(?).
 * Adding to SP database,
 * and send
 *   <base, address(SD), (lifetime(H),) policy>
 * to the socket which was send.
 *
 * SPDADD set a unique policy entry.
 * SPDSETIDX like SPDADD without a part of policy requests.
 * SPDUPDATE replace a unique policy entry.
 *
 * XXXAE: serialize this in PF_KEY to avoid races.
 * m will always be freed.
 */
static int
key_spdadd(struct socket *so, struct mbuf *m, const struct sadb_msghdr *mhp)
{
	struct secpolicyindex spidx;
	struct sadb_address *src0, *dst0;
	struct sadb_x_policy *xpl0, *xpl;
	struct sadb_lifetime *lft = NULL;
	struct secpolicy *newsp;
	int error;

	IPSEC_ASSERT(so != NULL, ("null socket"));
	IPSEC_ASSERT(m != NULL, ("null mbuf"));
	IPSEC_ASSERT(mhp != NULL, ("null msghdr"));
	IPSEC_ASSERT(mhp->msg != NULL, ("null msg"));

	if (SADB_CHECKHDR(mhp, SADB_EXT_ADDRESS_SRC) ||
	    SADB_CHECKHDR(mhp, SADB_EXT_ADDRESS_DST) ||
	    SADB_CHECKHDR(mhp, SADB_X_EXT_POLICY)) {
		ipseclog((LOG_DEBUG,
		    "%s: invalid message: missing required header.\n",
		    __func__));
		return key_senderror(so, m, EINVAL);
	}
	if (SADB_CHECKLEN(mhp, SADB_EXT_ADDRESS_SRC) ||
	    SADB_CHECKLEN(mhp, SADB_EXT_ADDRESS_DST) ||
	    SADB_CHECKLEN(mhp, SADB_X_EXT_POLICY)) {
		ipseclog((LOG_DEBUG,
		    "%s: invalid message: wrong header size.\n", __func__));
		return key_senderror(so, m, EINVAL);
	}
	if (!SADB_CHECKHDR(mhp, SADB_EXT_LIFETIME_HARD)) {
		if (SADB_CHECKLEN(mhp, SADB_EXT_LIFETIME_HARD)) {
			ipseclog((LOG_DEBUG,
			    "%s: invalid message: wrong header size.\n",
			    __func__));
			return key_senderror(so, m, EINVAL);
		}
		lft = (struct sadb_lifetime *)mhp->ext[SADB_EXT_LIFETIME_HARD];
	}

	src0 = (struct sadb_address *)mhp->ext[SADB_EXT_ADDRESS_SRC];
	dst0 = (struct sadb_address *)mhp->ext[SADB_EXT_ADDRESS_DST];
	xpl0 = (struct sadb_x_policy *)mhp->ext[SADB_X_EXT_POLICY];

	/* check the direciton */
	switch (xpl0->sadb_x_policy_dir) {
	case IPSEC_DIR_INBOUND:
	case IPSEC_DIR_OUTBOUND:
		break;
	default:
		ipseclog((LOG_DEBUG, "%s: invalid SP direction.\n", __func__));
		return key_senderror(so, m, EINVAL);
	}
	/* key_spdadd() accepts DISCARD, NONE and IPSEC. */
	if (xpl0->sadb_x_policy_type != IPSEC_POLICY_DISCARD &&
	    xpl0->sadb_x_policy_type != IPSEC_POLICY_NONE &&
	    xpl0->sadb_x_policy_type != IPSEC_POLICY_IPSEC) {
		ipseclog((LOG_DEBUG, "%s: invalid policy type.\n", __func__));
		return key_senderror(so, m, EINVAL);
	}

	/* policy requests are mandatory when action is ipsec. */
	if (xpl0->sadb_x_policy_type == IPSEC_POLICY_IPSEC &&
	    mhp->extlen[SADB_X_EXT_POLICY] <= sizeof(*xpl0)) {
		ipseclog((LOG_DEBUG,
		    "%s: policy requests required.\n", __func__));
		return key_senderror(so, m, EINVAL);
	}

	error = key_checksockaddrs((struct sockaddr *)(src0 + 1),
	    (struct sockaddr *)(dst0 + 1));
	if (error != 0 ||
	    src0->sadb_address_proto != dst0->sadb_address_proto) {
		ipseclog((LOG_DEBUG, "%s: invalid sockaddr.\n", __func__));
		return key_senderror(so, m, error);
	}
	/* make secindex */
	KEY_SETSECSPIDX(xpl0->sadb_x_policy_dir,
	                src0 + 1,
	                dst0 + 1,
	                src0->sadb_address_prefixlen,
	                dst0->sadb_address_prefixlen,
	                src0->sadb_address_proto,
	                &spidx);
	/* Checking there is SP already or not. */
	newsp = key_getsp(&spidx);
	if (newsp != NULL) {
		if (mhp->msg->sadb_msg_type == SADB_X_SPDUPDATE) {
			KEYDBG(KEY_STAMP,
			    printf("%s: unlink SP(%p) for SPDUPDATE\n",
				__func__, newsp));
			KEYDBG(KEY_DATA, kdebug_secpolicy(newsp));
			key_unlink(newsp);
			key_freesp(&newsp);
		} else {
			key_freesp(&newsp);
			ipseclog((LOG_DEBUG, "%s: a SP entry exists already.",
			    __func__));
			return (key_senderror(so, m, EEXIST));
		}
	}

	/* allocate new SP entry */
	if ((newsp = key_msg2sp(xpl0, PFKEY_EXTLEN(xpl0), &error)) == NULL) {
		return key_senderror(so, m, error);
	}

	newsp->lastused = newsp->created = time_second;
	newsp->lifetime = lft ? lft->sadb_lifetime_addtime : 0;
	newsp->validtime = lft ? lft->sadb_lifetime_usetime : 0;
	bcopy(&spidx, &newsp->spidx, sizeof(spidx));

	/* XXXAE: there is race between key_getsp() and key_insertsp() */
	SPTREE_WLOCK();
	if ((newsp->id = key_getnewspid()) == 0) {
		SPTREE_WUNLOCK();
		key_freesp(&newsp);
		return key_senderror(so, m, ENOBUFS);
	}
	key_insertsp(newsp);
	SPTREE_WUNLOCK();
	if (SPDCACHE_ENABLED())
		spdcache_clear();

	KEYDBG(KEY_STAMP,
	    printf("%s: SP(%p)\n", __func__, newsp));
	KEYDBG(KEY_DATA, kdebug_secpolicy(newsp));

    {
	struct mbuf *n, *mpolicy;
	struct sadb_msg *newmsg;
	int off;

	/* create new sadb_msg to reply. */
	if (lft) {
		n = key_gather_mbuf(m, mhp, 2, 5, SADB_EXT_RESERVED,
		    SADB_X_EXT_POLICY, SADB_EXT_LIFETIME_HARD,
		    SADB_EXT_ADDRESS_SRC, SADB_EXT_ADDRESS_DST);
	} else {
		n = key_gather_mbuf(m, mhp, 2, 4, SADB_EXT_RESERVED,
		    SADB_X_EXT_POLICY,
		    SADB_EXT_ADDRESS_SRC, SADB_EXT_ADDRESS_DST);
	}
	if (!n)
		return key_senderror(so, m, ENOBUFS);

	if (n->m_len < sizeof(*newmsg)) {
		n = m_pullup(n, sizeof(*newmsg));
		if (!n)
			return key_senderror(so, m, ENOBUFS);
	}
	newmsg = mtod(n, struct sadb_msg *);
	newmsg->sadb_msg_errno = 0;
	newmsg->sadb_msg_len = PFKEY_UNIT64(n->m_pkthdr.len);

	off = 0;
	mpolicy = m_pulldown(n, PFKEY_ALIGN8(sizeof(struct sadb_msg)),
	    sizeof(*xpl), &off);
	if (mpolicy == NULL) {
		/* n is already freed */
		return key_senderror(so, m, ENOBUFS);
	}
	xpl = (struct sadb_x_policy *)(mtod(mpolicy, caddr_t) + off);
	if (xpl->sadb_x_policy_exttype != SADB_X_EXT_POLICY) {
		m_freem(n);
		return key_senderror(so, m, EINVAL);
	}
	xpl->sadb_x_policy_id = newsp->id;

	m_freem(m);
	return key_sendup_mbuf(so, n, KEY_SENDUP_ALL);
    }
}

/*
 * get new policy id.
 * OUT:
 *	0:	failure.
 *	others: success.
 */
static uint32_t
key_getnewspid(void)
{
	struct secpolicy *sp;
	uint32_t newid = 0;
	int count = V_key_spi_trycnt;	/* XXX */

	SPTREE_WLOCK_ASSERT();
	while (count--) {
		if (V_policy_id == ~0) /* overflowed */
			newid = V_policy_id = 1;
		else
			newid = ++V_policy_id;
		LIST_FOREACH(sp, SPHASH_HASH(newid), idhash) {
			if (sp->id == newid)
				break;
		}
		if (sp == NULL)
			break;
	}
	if (count == 0 || newid == 0) {
		ipseclog((LOG_DEBUG, "%s: failed to allocate policy id.\n",
		    __func__));
		return (0);
	}
	return (newid);
}

/*
 * SADB_SPDDELETE processing
 * receive
 *   <base, address(SD), policy(*)>
 * from the user(?), and set SADB_SASTATE_DEAD,
 * and send,
 *   <base, address(SD), policy(*)>
 * to the ikmpd.
 * policy(*) including direction of policy.
 *
 * m will always be freed.
 */
static int
key_spddelete(struct socket *so, struct mbuf *m,
    const struct sadb_msghdr *mhp)
{
	struct secpolicyindex spidx;
	struct sadb_address *src0, *dst0;
	struct sadb_x_policy *xpl0;
	struct secpolicy *sp;

	IPSEC_ASSERT(so != NULL, ("null so"));
	IPSEC_ASSERT(m != NULL, ("null mbuf"));
	IPSEC_ASSERT(mhp != NULL, ("null msghdr"));
	IPSEC_ASSERT(mhp->msg != NULL, ("null msg"));

	if (SADB_CHECKHDR(mhp, SADB_EXT_ADDRESS_SRC) ||
	    SADB_CHECKHDR(mhp, SADB_EXT_ADDRESS_DST) ||
	    SADB_CHECKHDR(mhp, SADB_X_EXT_POLICY)) {
		ipseclog((LOG_DEBUG,
		    "%s: invalid message: missing required header.\n",
		    __func__));
		return key_senderror(so, m, EINVAL);
	}
	if (SADB_CHECKLEN(mhp, SADB_EXT_ADDRESS_SRC) ||
	    SADB_CHECKLEN(mhp, SADB_EXT_ADDRESS_DST) ||
	    SADB_CHECKLEN(mhp, SADB_X_EXT_POLICY)) {
		ipseclog((LOG_DEBUG,
		    "%s: invalid message: wrong header size.\n", __func__));
		return key_senderror(so, m, EINVAL);
	}

	src0 = (struct sadb_address *)mhp->ext[SADB_EXT_ADDRESS_SRC];
	dst0 = (struct sadb_address *)mhp->ext[SADB_EXT_ADDRESS_DST];
	xpl0 = (struct sadb_x_policy *)mhp->ext[SADB_X_EXT_POLICY];

	/* check the direciton */
	switch (xpl0->sadb_x_policy_dir) {
	case IPSEC_DIR_INBOUND:
	case IPSEC_DIR_OUTBOUND:
		break;
	default:
		ipseclog((LOG_DEBUG, "%s: invalid SP direction.\n", __func__));
		return key_senderror(so, m, EINVAL);
	}
	/* Only DISCARD, NONE and IPSEC are allowed */
	if (xpl0->sadb_x_policy_type != IPSEC_POLICY_DISCARD &&
	    xpl0->sadb_x_policy_type != IPSEC_POLICY_NONE &&
	    xpl0->sadb_x_policy_type != IPSEC_POLICY_IPSEC) {
		ipseclog((LOG_DEBUG, "%s: invalid policy type.\n", __func__));
		return key_senderror(so, m, EINVAL);
	}
	if (key_checksockaddrs((struct sockaddr *)(src0 + 1),
	    (struct sockaddr *)(dst0 + 1)) != 0 ||
	    src0->sadb_address_proto != dst0->sadb_address_proto) {
		ipseclog((LOG_DEBUG, "%s: invalid sockaddr.\n", __func__));
		return key_senderror(so, m, EINVAL);
	}
	/* make secindex */
	KEY_SETSECSPIDX(xpl0->sadb_x_policy_dir,
	                src0 + 1,
	                dst0 + 1,
	                src0->sadb_address_prefixlen,
	                dst0->sadb_address_prefixlen,
	                src0->sadb_address_proto,
	                &spidx);

	/* Is there SP in SPD ? */
	if ((sp = key_getsp(&spidx)) == NULL) {
		ipseclog((LOG_DEBUG, "%s: no SP found.\n", __func__));
		return key_senderror(so, m, EINVAL);
	}

	/* save policy id to buffer to be returned. */
	xpl0->sadb_x_policy_id = sp->id;

	KEYDBG(KEY_STAMP,
	    printf("%s: SP(%p)\n", __func__, sp));
	KEYDBG(KEY_DATA, kdebug_secpolicy(sp));
	key_unlink(sp);
	key_freesp(&sp);

    {
	struct mbuf *n;
	struct sadb_msg *newmsg;

	/* create new sadb_msg to reply. */
	n = key_gather_mbuf(m, mhp, 1, 4, SADB_EXT_RESERVED,
	    SADB_X_EXT_POLICY, SADB_EXT_ADDRESS_SRC, SADB_EXT_ADDRESS_DST);
	if (!n)
		return key_senderror(so, m, ENOBUFS);

	newmsg = mtod(n, struct sadb_msg *);
	newmsg->sadb_msg_errno = 0;
	newmsg->sadb_msg_len = PFKEY_UNIT64(n->m_pkthdr.len);

	m_freem(m);
	return key_sendup_mbuf(so, n, KEY_SENDUP_ALL);
    }
}

/*
 * SADB_SPDDELETE2 processing
 * receive
 *   <base, policy(*)>
 * from the user(?), and set SADB_SASTATE_DEAD,
 * and send,
 *   <base, policy(*)>
 * to the ikmpd.
 * policy(*) including direction of policy.
 *
 * m will always be freed.
 */
static int
key_spddelete2(struct socket *so, struct mbuf *m,
    const struct sadb_msghdr *mhp)
{
	struct secpolicy *sp;
	uint32_t id;

	IPSEC_ASSERT(so != NULL, ("null socket"));
	IPSEC_ASSERT(m != NULL, ("null mbuf"));
	IPSEC_ASSERT(mhp != NULL, ("null msghdr"));
	IPSEC_ASSERT(mhp->msg != NULL, ("null msg"));

	if (SADB_CHECKHDR(mhp, SADB_X_EXT_POLICY) ||
	    SADB_CHECKLEN(mhp, SADB_X_EXT_POLICY)) {
		ipseclog((LOG_DEBUG, "%s: invalid message is passed.\n",
		    __func__));
		return key_senderror(so, m, EINVAL);
	}

	id = ((struct sadb_x_policy *)
	    mhp->ext[SADB_X_EXT_POLICY])->sadb_x_policy_id;

	/* Is there SP in SPD ? */
	if ((sp = key_getspbyid(id)) == NULL) {
		ipseclog((LOG_DEBUG, "%s: no SP found for id %u.\n",
		    __func__, id));
		return key_senderror(so, m, EINVAL);
	}

	KEYDBG(KEY_STAMP,
	    printf("%s: SP(%p)\n", __func__, sp));
	KEYDBG(KEY_DATA, kdebug_secpolicy(sp));
	key_unlink(sp);
	if (sp->state != IPSEC_SPSTATE_DEAD) {
		ipseclog((LOG_DEBUG, "%s: failed to delete SP with id %u.\n",
		    __func__, id));
		key_freesp(&sp);
		return (key_senderror(so, m, EACCES));
	}
	key_freesp(&sp);

    {
	struct mbuf *n, *nn;
	struct sadb_msg *newmsg;
	int off, len;

	/* create new sadb_msg to reply. */
	len = PFKEY_ALIGN8(sizeof(struct sadb_msg));

	MGETHDR(n, M_NOWAIT, MT_DATA);
	if (n && len > MHLEN) {
		if (!(MCLGET(n, M_NOWAIT))) {
			m_freem(n);
			n = NULL;
		}
	}
	if (!n)
		return key_senderror(so, m, ENOBUFS);

	n->m_len = len;
	n->m_next = NULL;
	off = 0;

	m_copydata(m, 0, sizeof(struct sadb_msg), mtod(n, caddr_t) + off);
	off += PFKEY_ALIGN8(sizeof(struct sadb_msg));

	IPSEC_ASSERT(off == len, ("length inconsistency (off %u len %u)",
		off, len));

	n->m_next = m_copym(m, mhp->extoff[SADB_X_EXT_POLICY],
	    mhp->extlen[SADB_X_EXT_POLICY], M_NOWAIT);
	if (!n->m_next) {
		m_freem(n);
		return key_senderror(so, m, ENOBUFS);
	}

	n->m_pkthdr.len = 0;
	for (nn = n; nn; nn = nn->m_next)
		n->m_pkthdr.len += nn->m_len;

	newmsg = mtod(n, struct sadb_msg *);
	newmsg->sadb_msg_errno = 0;
	newmsg->sadb_msg_len = PFKEY_UNIT64(n->m_pkthdr.len);

	m_freem(m);
	return key_sendup_mbuf(so, n, KEY_SENDUP_ALL);
    }
}

/*
 * SADB_X_SPDGET processing
 * receive
 *   <base, policy(*)>
 * from the user(?),
 * and send,
 *   <base, address(SD), policy>
 * to the ikmpd.
 * policy(*) including direction of policy.
 *
 * m will always be freed.
 */
static int
key_spdget(struct socket *so, struct mbuf *m, const struct sadb_msghdr *mhp)
{
	struct secpolicy *sp;
	struct mbuf *n;
	uint32_t id;

	IPSEC_ASSERT(so != NULL, ("null socket"));
	IPSEC_ASSERT(m != NULL, ("null mbuf"));
	IPSEC_ASSERT(mhp != NULL, ("null msghdr"));
	IPSEC_ASSERT(mhp->msg != NULL, ("null msg"));

	if (SADB_CHECKHDR(mhp, SADB_X_EXT_POLICY) ||
	    SADB_CHECKLEN(mhp, SADB_X_EXT_POLICY)) {
		ipseclog((LOG_DEBUG, "%s: invalid message is passed.\n",
		    __func__));
		return key_senderror(so, m, EINVAL);
	}

	id = ((struct sadb_x_policy *)
	    mhp->ext[SADB_X_EXT_POLICY])->sadb_x_policy_id;

	/* Is there SP in SPD ? */
	if ((sp = key_getspbyid(id)) == NULL) {
		ipseclog((LOG_DEBUG, "%s: no SP found for id %u.\n",
		    __func__, id));
		return key_senderror(so, m, ENOENT);
	}

	n = key_setdumpsp(sp, SADB_X_SPDGET, mhp->msg->sadb_msg_seq,
	    mhp->msg->sadb_msg_pid);
	key_freesp(&sp);
	if (n != NULL) {
		m_freem(m);
		return key_sendup_mbuf(so, n, KEY_SENDUP_ONE);
	} else
		return key_senderror(so, m, ENOBUFS);
}

/*
 * SADB_X_SPDACQUIRE processing.
 * Acquire policy and SA(s) for a *OUTBOUND* packet.
 * send
 *   <base, policy(*)>
 * to KMD, and expect to receive
 *   <base> with SADB_X_SPDACQUIRE if error occurred,
 * or
 *   <base, policy>
 * with SADB_X_SPDUPDATE from KMD by PF_KEY.
 * policy(*) is without policy requests.
 *
 *    0     : succeed
 *    others: error number
 */
int
key_spdacquire(struct secpolicy *sp)
{
	struct mbuf *result = NULL, *m;
	struct secspacq *newspacq;

	IPSEC_ASSERT(sp != NULL, ("null secpolicy"));
	IPSEC_ASSERT(sp->req == NULL, ("policy exists"));
	IPSEC_ASSERT(sp->policy == IPSEC_POLICY_IPSEC,
		("policy not IPSEC %u", sp->policy));

	/* Get an entry to check whether sent message or not. */
	newspacq = key_getspacq(&sp->spidx);
	if (newspacq != NULL) {
		if (V_key_blockacq_count < newspacq->count) {
			/* reset counter and do send message. */
			newspacq->count = 0;
		} else {
			/* increment counter and do nothing. */
			newspacq->count++;
			SPACQ_UNLOCK();
			return (0);
		}
		SPACQ_UNLOCK();
	} else {
		/* make new entry for blocking to send SADB_ACQUIRE. */
		newspacq = key_newspacq(&sp->spidx);
		if (newspacq == NULL)
			return ENOBUFS;
	}

	/* create new sadb_msg to reply. */
	m = key_setsadbmsg(SADB_X_SPDACQUIRE, 0, 0, 0, 0, 0);
	if (!m)
		return ENOBUFS;

	result = m;

	result->m_pkthdr.len = 0;
	for (m = result; m; m = m->m_next)
		result->m_pkthdr.len += m->m_len;

	mtod(result, struct sadb_msg *)->sadb_msg_len =
	    PFKEY_UNIT64(result->m_pkthdr.len);

	return key_sendup_mbuf(NULL, m, KEY_SENDUP_REGISTERED);
}

/*
 * SADB_SPDFLUSH processing
 * receive
 *   <base>
 * from the user, and free all entries in secpctree.
 * and send,
 *   <base>
 * to the user.
 * NOTE: what to do is only marking SADB_SASTATE_DEAD.
 *
 * m will always be freed.
 */
static int
key_spdflush(struct socket *so, struct mbuf *m, const struct sadb_msghdr *mhp)
{
	struct secpolicy_queue drainq;
	struct sadb_msg *newmsg;
	struct secpolicy *sp, *nextsp;
	u_int dir;

	IPSEC_ASSERT(so != NULL, ("null socket"));
	IPSEC_ASSERT(m != NULL, ("null mbuf"));
	IPSEC_ASSERT(mhp != NULL, ("null msghdr"));
	IPSEC_ASSERT(mhp->msg != NULL, ("null msg"));

	if (m->m_len != PFKEY_ALIGN8(sizeof(struct sadb_msg)))
		return key_senderror(so, m, EINVAL);

	TAILQ_INIT(&drainq);
	SPTREE_WLOCK();
	for (dir = 0; dir < IPSEC_DIR_MAX; dir++) {
		TAILQ_CONCAT(&drainq, &V_sptree[dir], chain);
	}
	/*
	 * We need to set state to DEAD for each policy to be sure,
	 * that another thread won't try to unlink it.
	 * Also remove SP from sphash.
	 */
	TAILQ_FOREACH(sp, &drainq, chain) {
		sp->state = IPSEC_SPSTATE_DEAD;
		LIST_REMOVE(sp, idhash);
	}
	V_sp_genid++;
	V_spd_size = 0;
	SPTREE_WUNLOCK();
	if (SPDCACHE_ENABLED())
		spdcache_clear();
	sp = TAILQ_FIRST(&drainq);
	while (sp != NULL) {
		nextsp = TAILQ_NEXT(sp, chain);
		key_freesp(&sp);
		sp = nextsp;
	}

	if (sizeof(struct sadb_msg) > m->m_len + M_TRAILINGSPACE(m)) {
		ipseclog((LOG_DEBUG, "%s: No more memory.\n", __func__));
		return key_senderror(so, m, ENOBUFS);
	}

	if (m->m_next)
		m_freem(m->m_next);
	m->m_next = NULL;
	m->m_pkthdr.len = m->m_len = PFKEY_ALIGN8(sizeof(struct sadb_msg));
	newmsg = mtod(m, struct sadb_msg *);
	newmsg->sadb_msg_errno = 0;
	newmsg->sadb_msg_len = PFKEY_UNIT64(m->m_pkthdr.len);

	return key_sendup_mbuf(so, m, KEY_SENDUP_ALL);
}

static uint8_t
key_satype2scopemask(uint8_t satype)
{

	if (satype == IPSEC_POLICYSCOPE_ANY)
		return (0xff);
	return (satype);
}
/*
 * SADB_SPDDUMP processing
 * receive
 *   <base>
 * from the user, and dump all SP leaves and send,
 *   <base> .....
 * to the ikmpd.
 *
 * NOTE:
 *   sadb_msg_satype is considered as mask of policy scopes.
 *   m will always be freed.
 */
static int
key_spddump(struct socket *so, struct mbuf *m, const struct sadb_msghdr *mhp)
{
	SPTREE_RLOCK_TRACKER;
	struct secpolicy *sp;
	struct mbuf *n;
	int cnt;
	u_int dir, scope;

	IPSEC_ASSERT(so != NULL, ("null socket"));
	IPSEC_ASSERT(m != NULL, ("null mbuf"));
	IPSEC_ASSERT(mhp != NULL, ("null msghdr"));
	IPSEC_ASSERT(mhp->msg != NULL, ("null msg"));

	/* search SPD entry and get buffer size. */
	cnt = 0;
	scope = key_satype2scopemask(mhp->msg->sadb_msg_satype);
	SPTREE_RLOCK();
	for (dir = 0; dir < IPSEC_DIR_MAX; dir++) {
		if (scope & IPSEC_POLICYSCOPE_GLOBAL) {
			TAILQ_FOREACH(sp, &V_sptree[dir], chain)
				cnt++;
		}
		if (scope & IPSEC_POLICYSCOPE_IFNET) {
			TAILQ_FOREACH(sp, &V_sptree_ifnet[dir], chain)
				cnt++;
		}
	}

	if (cnt == 0) {
		SPTREE_RUNLOCK();
		return key_senderror(so, m, ENOENT);
	}

	for (dir = 0; dir < IPSEC_DIR_MAX; dir++) {
		if (scope & IPSEC_POLICYSCOPE_GLOBAL) {
			TAILQ_FOREACH(sp, &V_sptree[dir], chain) {
				--cnt;
				n = key_setdumpsp(sp, SADB_X_SPDDUMP, cnt,
				    mhp->msg->sadb_msg_pid);

				if (n != NULL)
					key_sendup_mbuf(so, n, KEY_SENDUP_ONE);
			}
		}
		if (scope & IPSEC_POLICYSCOPE_IFNET) {
			TAILQ_FOREACH(sp, &V_sptree_ifnet[dir], chain) {
				--cnt;
				n = key_setdumpsp(sp, SADB_X_SPDDUMP, cnt,
				    mhp->msg->sadb_msg_pid);

				if (n != NULL)
					key_sendup_mbuf(so, n, KEY_SENDUP_ONE);
			}
		}
	}

	SPTREE_RUNLOCK();
	m_freem(m);
	return (0);
}

static struct mbuf *
key_setdumpsp(struct secpolicy *sp, u_int8_t type, u_int32_t seq,
    u_int32_t pid)
{
	struct mbuf *result = NULL, *m;
	struct seclifetime lt;

	m = key_setsadbmsg(type, 0, SADB_SATYPE_UNSPEC, seq, pid, sp->refcnt);
	if (!m)
		goto fail;
	result = m;

	m = key_setsadbaddr(SADB_EXT_ADDRESS_SRC,
	    &sp->spidx.src.sa, sp->spidx.prefs,
	    sp->spidx.ul_proto);
	if (!m)
		goto fail;
	m_cat(result, m);

	m = key_setsadbaddr(SADB_EXT_ADDRESS_DST,
	    &sp->spidx.dst.sa, sp->spidx.prefd,
	    sp->spidx.ul_proto);
	if (!m)
		goto fail;
	m_cat(result, m);

	m = key_sp2mbuf(sp);
	if (!m)
		goto fail;
	m_cat(result, m);

	if(sp->lifetime){
		lt.addtime=sp->created;
		lt.usetime= sp->lastused;
		m = key_setlifetime(&lt, SADB_EXT_LIFETIME_CURRENT);
		if (!m)
			goto fail;
		m_cat(result, m);
		
		lt.addtime=sp->lifetime;
		lt.usetime= sp->validtime;
		m = key_setlifetime(&lt, SADB_EXT_LIFETIME_HARD);
		if (!m)
			goto fail;
		m_cat(result, m);
	}

	if ((result->m_flags & M_PKTHDR) == 0)
		goto fail;

	if (result->m_len < sizeof(struct sadb_msg)) {
		result = m_pullup(result, sizeof(struct sadb_msg));
		if (result == NULL)
			goto fail;
	}

	result->m_pkthdr.len = 0;
	for (m = result; m; m = m->m_next)
		result->m_pkthdr.len += m->m_len;

	mtod(result, struct sadb_msg *)->sadb_msg_len =
	    PFKEY_UNIT64(result->m_pkthdr.len);

	return result;

fail:
	m_freem(result);
	return NULL;
}
/*
 * get PFKEY message length for security policy and request.
 */
static size_t
key_getspreqmsglen(struct secpolicy *sp)
{
	size_t tlen, len;
	int i;

	tlen = sizeof(struct sadb_x_policy);
	/* if is the policy for ipsec ? */
	if (sp->policy != IPSEC_POLICY_IPSEC)
		return (tlen);

	/* get length of ipsec requests */
	for (i = 0; i < sp->tcount; i++) {
		len = sizeof(struct sadb_x_ipsecrequest)
			+ sp->req[i]->saidx.src.sa.sa_len
			+ sp->req[i]->saidx.dst.sa.sa_len;

		tlen += PFKEY_ALIGN8(len);
	}
	return (tlen);
}

/*
 * SADB_SPDEXPIRE processing
 * send
 *   <base, address(SD), lifetime(CH), policy>
 * to KMD by PF_KEY.
 *
 * OUT:	0	: succeed
 *	others	: error number
 */
static int
key_spdexpire(struct secpolicy *sp)
{
	struct sadb_lifetime *lt;
	struct mbuf *result = NULL, *m;
	int len, error = -1;

	IPSEC_ASSERT(sp != NULL, ("null secpolicy"));

	KEYDBG(KEY_STAMP,
	    printf("%s: SP(%p)\n", __func__, sp));
	KEYDBG(KEY_DATA, kdebug_secpolicy(sp));

	/* set msg header */
	m = key_setsadbmsg(SADB_X_SPDEXPIRE, 0, 0, 0, 0, 0);
	if (!m) {
		error = ENOBUFS;
		goto fail;
	}
	result = m;

	/* create lifetime extension (current and hard) */
	len = PFKEY_ALIGN8(sizeof(*lt)) * 2;
	m = m_get2(len, M_NOWAIT, MT_DATA, 0);
	if (m == NULL) {
		error = ENOBUFS;
		goto fail;
	}
	m_align(m, len);
	m->m_len = len;
	bzero(mtod(m, caddr_t), len);
	lt = mtod(m, struct sadb_lifetime *);
	lt->sadb_lifetime_len = PFKEY_UNIT64(sizeof(struct sadb_lifetime));
	lt->sadb_lifetime_exttype = SADB_EXT_LIFETIME_CURRENT;
	lt->sadb_lifetime_allocations = 0;
	lt->sadb_lifetime_bytes = 0;
	lt->sadb_lifetime_addtime = sp->created;
	lt->sadb_lifetime_usetime = sp->lastused;
	lt = (struct sadb_lifetime *)(mtod(m, caddr_t) + len / 2);
	lt->sadb_lifetime_len = PFKEY_UNIT64(sizeof(struct sadb_lifetime));
	lt->sadb_lifetime_exttype = SADB_EXT_LIFETIME_HARD;
	lt->sadb_lifetime_allocations = 0;
	lt->sadb_lifetime_bytes = 0;
	lt->sadb_lifetime_addtime = sp->lifetime;
	lt->sadb_lifetime_usetime = sp->validtime;
	m_cat(result, m);

	/* set sadb_address for source */
	m = key_setsadbaddr(SADB_EXT_ADDRESS_SRC,
	    &sp->spidx.src.sa,
	    sp->spidx.prefs, sp->spidx.ul_proto);
	if (!m) {
		error = ENOBUFS;
		goto fail;
	}
	m_cat(result, m);

	/* set sadb_address for destination */
	m = key_setsadbaddr(SADB_EXT_ADDRESS_DST,
	    &sp->spidx.dst.sa,
	    sp->spidx.prefd, sp->spidx.ul_proto);
	if (!m) {
		error = ENOBUFS;
		goto fail;
	}
	m_cat(result, m);

	/* set secpolicy */
	m = key_sp2mbuf(sp);
	if (!m) {
		error = ENOBUFS;
		goto fail;
	}
	m_cat(result, m);

	if ((result->m_flags & M_PKTHDR) == 0) {
		error = EINVAL;
		goto fail;
	}

	if (result->m_len < sizeof(struct sadb_msg)) {
		result = m_pullup(result, sizeof(struct sadb_msg));
		if (result == NULL) {
			error = ENOBUFS;
			goto fail;
		}
	}

	result->m_pkthdr.len = 0;
	for (m = result; m; m = m->m_next)
		result->m_pkthdr.len += m->m_len;

	mtod(result, struct sadb_msg *)->sadb_msg_len =
	    PFKEY_UNIT64(result->m_pkthdr.len);

	return key_sendup_mbuf(NULL, result, KEY_SENDUP_REGISTERED);

 fail:
	if (result)
		m_freem(result);
	return error;
}

/* %%% SAD management */
/*
 * allocating and initialize new SA head.
 * OUT:	NULL	: failure due to the lack of memory.
 *	others	: pointer to new SA head.
 */
static struct secashead *
key_newsah(struct secasindex *saidx)
{
	struct secashead *sah;

	sah = malloc(sizeof(struct secashead), M_IPSEC_SAH,
	    M_NOWAIT | M_ZERO);
	if (sah == NULL) {
		PFKEYSTAT_INC(in_nomem);
		return (NULL);
	}
	TAILQ_INIT(&sah->savtree_larval);
	TAILQ_INIT(&sah->savtree_alive);
	sah->saidx = *saidx;
	sah->state = SADB_SASTATE_DEAD;
	SAH_INITREF(sah);

	KEYDBG(KEY_STAMP,
	    printf("%s: SAH(%p)\n", __func__, sah));
	KEYDBG(KEY_DATA, kdebug_secash(sah, NULL));
	return (sah);
}

static void
key_freesah(struct secashead **psah)
{
	struct secashead *sah = *psah;

	if (SAH_DELREF(sah) == 0)
		return;

	KEYDBG(KEY_STAMP,
	    printf("%s: last reference to SAH(%p)\n", __func__, sah));
	KEYDBG(KEY_DATA, kdebug_secash(sah, NULL));

	*psah = NULL;
	key_delsah(sah);
}

static void
key_delsah(struct secashead *sah)
{
	IPSEC_ASSERT(sah != NULL, ("NULL sah"));
	IPSEC_ASSERT(sah->state == SADB_SASTATE_DEAD,
	    ("Attempt to free non DEAD SAH %p", sah));
	IPSEC_ASSERT(TAILQ_EMPTY(&sah->savtree_larval),
	    ("Attempt to free SAH %p with LARVAL SA", sah));
	IPSEC_ASSERT(TAILQ_EMPTY(&sah->savtree_alive),
	    ("Attempt to free SAH %p with ALIVE SA", sah));

	free(sah, M_IPSEC_SAH);
}

/*
 * allocating a new SA for key_add() and key_getspi() call,
 * and copy the values of mhp into new buffer.
 * When SAD message type is SADB_GETSPI set SA state to LARVAL.
 * For SADB_ADD create and initialize SA with MATURE state.
 * OUT:	NULL	: fail
 *	others	: pointer to new secasvar.
 */
static struct secasvar *
key_newsav(const struct sadb_msghdr *mhp, struct secasindex *saidx,
    uint32_t spi, int *errp)
{
	struct secashead *sah;
	struct secasvar *sav;
	int isnew;

	IPSEC_ASSERT(mhp != NULL, ("null msghdr"));
	IPSEC_ASSERT(mhp->msg != NULL, ("null msg"));
	IPSEC_ASSERT(mhp->msg->sadb_msg_type == SADB_GETSPI ||
	    mhp->msg->sadb_msg_type == SADB_ADD, ("wrong message type"));

	sav = NULL;
	sah = NULL;
	/* check SPI value */
	switch (saidx->proto) {
	case IPPROTO_ESP:
	case IPPROTO_AH:
		/*
		 * RFC 4302, 2.4. Security Parameters Index (SPI), SPI values
		 * 1-255 reserved by IANA for future use,
		 * 0 for implementation specific, local use.
		 */
		if (ntohl(spi) <= 255) {
			ipseclog((LOG_DEBUG, "%s: illegal range of SPI %u.\n",
			    __func__, ntohl(spi)));
			*errp = EINVAL;
			goto done;
		}
		break;
	}

	sav = malloc(sizeof(struct secasvar), M_IPSEC_SA, M_NOWAIT | M_ZERO);
	if (sav == NULL) {
		*errp = ENOBUFS;
		goto done;
	}
	sav->lock = malloc(sizeof(struct mtx), M_IPSEC_MISC,
	    M_NOWAIT | M_ZERO);
	if (sav->lock == NULL) {
		*errp = ENOBUFS;
		goto done;
	}
	mtx_init(sav->lock, "ipsec association", NULL, MTX_DEF);
	sav->lft_c = uma_zalloc_pcpu(V_key_lft_zone, M_NOWAIT);
	if (sav->lft_c == NULL) {
		*errp = ENOBUFS;
		goto done;
	}
	counter_u64_zero(sav->lft_c_allocations);
	counter_u64_zero(sav->lft_c_bytes);

	sav->spi = spi;
	sav->seq = mhp->msg->sadb_msg_seq;
	sav->state = SADB_SASTATE_LARVAL;
	sav->pid = (pid_t)mhp->msg->sadb_msg_pid;
	SAV_INITREF(sav);
again:
	sah = key_getsah(saidx);
	if (sah == NULL) {
		/* create a new SA index */
		sah = key_newsah(saidx);
		if (sah == NULL) {
			ipseclog((LOG_DEBUG,
			    "%s: No more memory.\n", __func__));
			*errp = ENOBUFS;
			goto done;
		}
		isnew = 1;
	} else
		isnew = 0;

	sav->sah = sah;
	if (mhp->msg->sadb_msg_type == SADB_GETSPI) {
		sav->created = time_second;
	} else if (sav->state == SADB_SASTATE_LARVAL) {
		/*
		 * Do not call key_setsaval() second time in case
		 * of `goto again`. We will have MATURE state.
		 */
		*errp = key_setsaval(sav, mhp);
		if (*errp != 0)
			goto done;
		sav->state = SADB_SASTATE_MATURE;
	}

	SAHTREE_WLOCK();
	/*
	 * Check that existing SAH wasn't unlinked.
	 * Since we didn't hold the SAHTREE lock, it is possible,
	 * that callout handler or key_flush() or key_delete() could
	 * unlink this SAH.
	 */
	if (isnew == 0 && sah->state == SADB_SASTATE_DEAD) {
		SAHTREE_WUNLOCK();
		key_freesah(&sah);	/* reference from key_getsah() */
		goto again;
	}
	if (isnew != 0) {
		/*
		 * Add new SAH into SADB.
		 *
		 * XXXAE: we can serialize key_add and key_getspi calls, so
		 * several threads will not fight in the race.
		 * Otherwise we should check under SAHTREE lock, that this
		 * SAH would not added twice.
		 */
		TAILQ_INSERT_HEAD(&V_sahtree, sah, chain);
		/* Add new SAH into hash by addresses */
		LIST_INSERT_HEAD(SAHADDRHASH_HASH(saidx), sah, addrhash);
		/* Now we are linked in the chain */
		sah->state = SADB_SASTATE_MATURE;
		/*
		 * SAV references this new SAH.
		 * In case of existing SAH we reuse reference
		 * from key_getsah().
		 */
		SAH_ADDREF(sah);
	}
	/* Link SAV with SAH */
	if (sav->state == SADB_SASTATE_MATURE)
		TAILQ_INSERT_HEAD(&sah->savtree_alive, sav, chain);
	else
		TAILQ_INSERT_HEAD(&sah->savtree_larval, sav, chain);
	/* Add SAV into SPI hash */
	LIST_INSERT_HEAD(SAVHASH_HASH(sav->spi), sav, spihash);
	SAHTREE_WUNLOCK();
	*errp = 0;	/* success */
done:
	if (*errp != 0) {
		if (sav != NULL) {
			if (sav->lock != NULL) {
				mtx_destroy(sav->lock);
				free(sav->lock, M_IPSEC_MISC);
			}
			if (sav->lft_c != NULL)
				uma_zfree_pcpu(V_key_lft_zone, sav->lft_c);
			free(sav, M_IPSEC_SA), sav = NULL;
		}
		if (sah != NULL)
			key_freesah(&sah);
		if (*errp == ENOBUFS) {
			ipseclog((LOG_DEBUG, "%s: No more memory.\n",
			    __func__));
			PFKEYSTAT_INC(in_nomem);
		}
	}
	return (sav);
}

/*
 * free() SA variable entry.
 */
static void
key_cleansav(struct secasvar *sav)
{

	if (sav->natt != NULL) {
		free(sav->natt, M_IPSEC_MISC);
		sav->natt = NULL;
	}
	if (sav->flags & SADB_X_EXT_F_CLONED)
		return;
	/*
	 * Cleanup xform state.  Note that zeroize'ing causes the
	 * keys to be cleared; otherwise we must do it ourself.
	 */
	if (sav->tdb_xform != NULL) {
		sav->tdb_xform->xf_zeroize(sav);
		sav->tdb_xform = NULL;
	} else {
		if (sav->key_auth != NULL)
			bzero(sav->key_auth->key_data, _KEYLEN(sav->key_auth));
		if (sav->key_enc != NULL)
			bzero(sav->key_enc->key_data, _KEYLEN(sav->key_enc));
	}
	if (sav->key_auth != NULL) {
		if (sav->key_auth->key_data != NULL)
			free(sav->key_auth->key_data, M_IPSEC_MISC);
		free(sav->key_auth, M_IPSEC_MISC);
		sav->key_auth = NULL;
	}
	if (sav->key_enc != NULL) {
		if (sav->key_enc->key_data != NULL)
			free(sav->key_enc->key_data, M_IPSEC_MISC);
		free(sav->key_enc, M_IPSEC_MISC);
		sav->key_enc = NULL;
	}
	if (sav->replay != NULL) {
		if (sav->replay->bitmap != NULL)
			free(sav->replay->bitmap, M_IPSEC_MISC);
		free(sav->replay, M_IPSEC_MISC);
		sav->replay = NULL;
	}
	if (sav->lft_h != NULL) {
		free(sav->lft_h, M_IPSEC_MISC);
		sav->lft_h = NULL;
	}
	if (sav->lft_s != NULL) {
		free(sav->lft_s, M_IPSEC_MISC);
		sav->lft_s = NULL;
	}
}

/*
 * free() SA variable entry.
 */
static void
key_delsav(struct secasvar *sav)
{
	IPSEC_ASSERT(sav != NULL, ("null sav"));
	IPSEC_ASSERT(sav->state == SADB_SASTATE_DEAD,
	    ("attempt to free non DEAD SA %p", sav));
	IPSEC_ASSERT(sav->refcnt == 0, ("reference count %u > 0",
	    sav->refcnt));

	/*
	 * SA must be unlinked from the chain and hashtbl.
	 * If SA was cloned, we leave all fields untouched,
	 * except NAT-T config.
	 */
	key_cleansav(sav);
	if ((sav->flags & SADB_X_EXT_F_CLONED) == 0) {
		mtx_destroy(sav->lock);
		free(sav->lock, M_IPSEC_MISC);
		uma_zfree(V_key_lft_zone, sav->lft_c);
	}
	free(sav, M_IPSEC_SA);
}

/*
 * search SAH.
 * OUT:
 *	NULL	: not found
 *	others	: found, referenced pointer to a SAH.
 */
static struct secashead *
key_getsah(struct secasindex *saidx)
{
	SAHTREE_RLOCK_TRACKER;
	struct secashead *sah;

	SAHTREE_RLOCK();
	LIST_FOREACH(sah, SAHADDRHASH_HASH(saidx), addrhash) {
	    if (key_cmpsaidx(&sah->saidx, saidx, CMP_MODE_REQID) != 0) {
		    SAH_ADDREF(sah);
		    break;
	    }
	}
	SAHTREE_RUNLOCK();
	return (sah);
}

/*
 * Check not to be duplicated SPI.
 * OUT:
 *	0	: not found
 *	1	: found SA with given SPI.
 */
static int
key_checkspidup(uint32_t spi)
{
	SAHTREE_RLOCK_TRACKER;
	struct secasvar *sav;

	/* Assume SPI is in network byte order */
	SAHTREE_RLOCK();
	LIST_FOREACH(sav, SAVHASH_HASH(spi), spihash) {
		if (sav->spi == spi)
			break;
	}
	SAHTREE_RUNLOCK();
	return (sav != NULL);
}

/*
 * Search SA by SPI.
 * OUT:
 *	NULL	: not found
 *	others	: found, referenced pointer to a SA.
 */
static struct secasvar *
key_getsavbyspi(uint32_t spi)
{
	SAHTREE_RLOCK_TRACKER;
	struct secasvar *sav;

	/* Assume SPI is in network byte order */
	SAHTREE_RLOCK();
	LIST_FOREACH(sav, SAVHASH_HASH(spi), spihash) {
		if (sav->spi != spi)
			continue;
		SAV_ADDREF(sav);
		break;
	}
	SAHTREE_RUNLOCK();
	return (sav);
}

static int
key_updatelifetimes(struct secasvar *sav, const struct sadb_msghdr *mhp)
{
	struct seclifetime *lft_h, *lft_s, *tmp;

	/* Lifetime extension is optional, check that it is present. */
	if (SADB_CHECKHDR(mhp, SADB_EXT_LIFETIME_HARD) &&
	    SADB_CHECKHDR(mhp, SADB_EXT_LIFETIME_SOFT)) {
		/*
		 * In case of SADB_UPDATE we may need to change
		 * existing lifetimes.
		 */
		if (sav->state == SADB_SASTATE_MATURE) {
			lft_h = lft_s = NULL;
			goto reset;
		}
		return (0);
	}
	/* Both HARD and SOFT extensions must present */
	if ((SADB_CHECKHDR(mhp, SADB_EXT_LIFETIME_HARD) &&
	    !SADB_CHECKHDR(mhp, SADB_EXT_LIFETIME_SOFT)) ||
	    (SADB_CHECKHDR(mhp, SADB_EXT_LIFETIME_SOFT) &&
	    !SADB_CHECKHDR(mhp, SADB_EXT_LIFETIME_HARD))) {
		ipseclog((LOG_DEBUG,
		    "%s: invalid message: missing required header.\n",
		    __func__));
		return (EINVAL);
	}
	if (SADB_CHECKLEN(mhp, SADB_EXT_LIFETIME_HARD) ||
	    SADB_CHECKLEN(mhp, SADB_EXT_LIFETIME_SOFT)) {
		ipseclog((LOG_DEBUG,
		    "%s: invalid message: wrong header size.\n", __func__));
		return (EINVAL);
	}
	lft_h = key_dup_lifemsg((const struct sadb_lifetime *)
	    mhp->ext[SADB_EXT_LIFETIME_HARD], M_IPSEC_MISC);
	if (lft_h == NULL) {
		PFKEYSTAT_INC(in_nomem);
		ipseclog((LOG_DEBUG, "%s: No more memory.\n", __func__));
		return (ENOBUFS);
	}
	lft_s = key_dup_lifemsg((const struct sadb_lifetime *)
	    mhp->ext[SADB_EXT_LIFETIME_SOFT], M_IPSEC_MISC);
	if (lft_s == NULL) {
		PFKEYSTAT_INC(in_nomem);
		free(lft_h, M_IPSEC_MISC);
		ipseclog((LOG_DEBUG, "%s: No more memory.\n", __func__));
		return (ENOBUFS);
	}
reset:
	if (sav->state != SADB_SASTATE_LARVAL) {
		/*
		 * key_update() holds reference to this SA,
		 * so it won't be deleted in meanwhile.
		 */
		SECASVAR_LOCK(sav);
		tmp = sav->lft_h;
		sav->lft_h = lft_h;
		lft_h = tmp;

		tmp = sav->lft_s;
		sav->lft_s = lft_s;
		lft_s = tmp;
		SECASVAR_UNLOCK(sav);
		if (lft_h != NULL)
			free(lft_h, M_IPSEC_MISC);
		if (lft_s != NULL)
			free(lft_s, M_IPSEC_MISC);
		return (0);
	}
	/* We can update lifetime without holding a lock */
	IPSEC_ASSERT(sav->lft_h == NULL, ("lft_h is already initialized\n"));
	IPSEC_ASSERT(sav->lft_s == NULL, ("lft_s is already initialized\n"));
	sav->lft_h = lft_h;
	sav->lft_s = lft_s;
	return (0);
}

/*
 * copy SA values from PF_KEY message except *SPI, SEQ, PID and TYPE*.
 * You must update these if need. Expects only LARVAL SAs.
 * OUT:	0:	success.
 *	!0:	failure.
 */
static int
key_setsaval(struct secasvar *sav, const struct sadb_msghdr *mhp)
{
	const struct sadb_sa *sa0;
	const struct sadb_key *key0;
	uint32_t replay;
	size_t len;
	int error;

	IPSEC_ASSERT(mhp != NULL, ("null msghdr"));
	IPSEC_ASSERT(mhp->msg != NULL, ("null msg"));
	IPSEC_ASSERT(sav->state == SADB_SASTATE_LARVAL,
	    ("Attempt to update non LARVAL SA"));

	/* XXX rewrite */
	error = key_setident(sav->sah, mhp);
	if (error != 0)
		goto fail;

	/* SA */
	if (!SADB_CHECKHDR(mhp, SADB_EXT_SA)) {
		if (SADB_CHECKLEN(mhp, SADB_EXT_SA)) {
			error = EINVAL;
			goto fail;
		}
		sa0 = (const struct sadb_sa *)mhp->ext[SADB_EXT_SA];
		sav->alg_auth = sa0->sadb_sa_auth;
		sav->alg_enc = sa0->sadb_sa_encrypt;
		sav->flags = sa0->sadb_sa_flags;
		if ((sav->flags & SADB_KEY_FLAGS_MAX) != sav->flags) {
			ipseclog((LOG_DEBUG,
			    "%s: invalid sa_flags 0x%08x.\n", __func__,
			    sav->flags));
			error = EINVAL;
			goto fail;
		}

		/* Optional replay window */
		replay = 0;
		if ((sa0->sadb_sa_flags & SADB_X_EXT_OLD) == 0)
			replay = sa0->sadb_sa_replay;
		if (!SADB_CHECKHDR(mhp, SADB_X_EXT_SA_REPLAY)) {
			if (SADB_CHECKLEN(mhp, SADB_X_EXT_SA_REPLAY)) {
				error = EINVAL;
				goto fail;
			}
			replay = ((const struct sadb_x_sa_replay *)
			    mhp->ext[SADB_X_EXT_SA_REPLAY])->sadb_x_sa_replay_replay;

			if (replay > UINT32_MAX - 32) {
				ipseclog((LOG_DEBUG,
				    "%s: replay window too big.\n", __func__));
				error = EINVAL;
				goto fail;
			}

			replay = (replay + 7) >> 3;
		}

		sav->replay = malloc(sizeof(struct secreplay), M_IPSEC_MISC,
		    M_NOWAIT | M_ZERO);
		if (sav->replay == NULL) {
			PFKEYSTAT_INC(in_nomem);
			ipseclog((LOG_DEBUG, "%s: No more memory.\n",
			    __func__));
			error = ENOBUFS;
			goto fail;
		}

		if (replay != 0) {
			/* number of 32b blocks to be allocated */
			uint32_t bitmap_size;

			/* RFC 6479:
			 * - the allocated replay window size must be
			 *   a power of two.
			 * - use an extra 32b block as a redundant window.
			 */
			bitmap_size = 1;
			while (replay + 4 > bitmap_size)
				bitmap_size <<= 1;
			bitmap_size = bitmap_size / 4;

			sav->replay->bitmap = malloc(
			    bitmap_size * sizeof(uint32_t), M_IPSEC_MISC,
			    M_NOWAIT | M_ZERO);
			if (sav->replay->bitmap == NULL) {
				PFKEYSTAT_INC(in_nomem);
				ipseclog((LOG_DEBUG, "%s: No more memory.\n",
					__func__));
				error = ENOBUFS;
				goto fail;
			}
			sav->replay->bitmap_size = bitmap_size;
			sav->replay->wsize = replay;
		}
	}

	/* Authentication keys */
	if (!SADB_CHECKHDR(mhp, SADB_EXT_KEY_AUTH)) {
		if (SADB_CHECKLEN(mhp, SADB_EXT_KEY_AUTH)) {
			error = EINVAL;
			goto fail;
		}
		error = 0;
		key0 = (const struct sadb_key *)mhp->ext[SADB_EXT_KEY_AUTH];
		len = mhp->extlen[SADB_EXT_KEY_AUTH];
		switch (mhp->msg->sadb_msg_satype) {
		case SADB_SATYPE_AH:
		case SADB_SATYPE_ESP:
		case SADB_X_SATYPE_TCPSIGNATURE:
			if (len == PFKEY_ALIGN8(sizeof(struct sadb_key)) &&
			    sav->alg_auth != SADB_X_AALG_NULL)
				error = EINVAL;
			break;
		case SADB_X_SATYPE_IPCOMP:
		default:
			error = EINVAL;
			break;
		}
		if (error) {
			ipseclog((LOG_DEBUG, "%s: invalid key_auth values.\n",
				__func__));
			goto fail;
		}

		sav->key_auth = key_dup_keymsg(key0, len, M_IPSEC_MISC);
		if (sav->key_auth == NULL ) {
			ipseclog((LOG_DEBUG, "%s: No more memory.\n",
				  __func__));
			PFKEYSTAT_INC(in_nomem);
			error = ENOBUFS;
			goto fail;
		}
	}

	/* Encryption key */
	if (!SADB_CHECKHDR(mhp, SADB_EXT_KEY_ENCRYPT)) {
		if (SADB_CHECKLEN(mhp, SADB_EXT_KEY_ENCRYPT)) {
			error = EINVAL;
			goto fail;
		}
		error = 0;
		key0 = (const struct sadb_key *)mhp->ext[SADB_EXT_KEY_ENCRYPT];
		len = mhp->extlen[SADB_EXT_KEY_ENCRYPT];
		switch (mhp->msg->sadb_msg_satype) {
		case SADB_SATYPE_ESP:
			if (len == PFKEY_ALIGN8(sizeof(struct sadb_key)) &&
			    sav->alg_enc != SADB_EALG_NULL) {
				error = EINVAL;
				break;
			}
			sav->key_enc = key_dup_keymsg(key0, len, M_IPSEC_MISC);
			if (sav->key_enc == NULL) {
				ipseclog((LOG_DEBUG, "%s: No more memory.\n",
					__func__));
				PFKEYSTAT_INC(in_nomem);
				error = ENOBUFS;
				goto fail;
			}
			break;
		case SADB_X_SATYPE_IPCOMP:
			if (len != PFKEY_ALIGN8(sizeof(struct sadb_key)))
				error = EINVAL;
			sav->key_enc = NULL;	/*just in case*/
			break;
		case SADB_SATYPE_AH:
		case SADB_X_SATYPE_TCPSIGNATURE:
		default:
			error = EINVAL;
			break;
		}
		if (error) {
			ipseclog((LOG_DEBUG, "%s: invalid key_enc value.\n",
				__func__));
			goto fail;
		}
	}

	/* set iv */
	sav->ivlen = 0;
	switch (mhp->msg->sadb_msg_satype) {
	case SADB_SATYPE_AH:
		if (sav->flags & SADB_X_EXT_DERIV) {
			ipseclog((LOG_DEBUG, "%s: invalid flag (derived) "
			    "given to AH SA.\n", __func__));
			error = EINVAL;
			goto fail;
		}
		if (sav->alg_enc != SADB_EALG_NONE) {
			ipseclog((LOG_DEBUG, "%s: protocol and algorithm "
			    "mismated.\n", __func__));
			error = EINVAL;
			goto fail;
		}
		error = xform_init(sav, XF_AH);
		break;
	case SADB_SATYPE_ESP:
		if ((sav->flags & (SADB_X_EXT_OLD | SADB_X_EXT_DERIV)) ==
		    (SADB_X_EXT_OLD | SADB_X_EXT_DERIV)) {
			ipseclog((LOG_DEBUG, "%s: invalid flag (derived) "
			    "given to old-esp.\n", __func__));
			error = EINVAL;
			goto fail;
		}
		error = xform_init(sav, XF_ESP);
		break;
	case SADB_X_SATYPE_IPCOMP:
		if (sav->alg_auth != SADB_AALG_NONE) {
			ipseclog((LOG_DEBUG, "%s: protocol and algorithm "
			    "mismated.\n", __func__));
			error = EINVAL;
			goto fail;
		}
		if ((sav->flags & SADB_X_EXT_RAWCPI) == 0 &&
		    ntohl(sav->spi) >= 0x10000) {
			ipseclog((LOG_DEBUG, "%s: invalid cpi for IPComp.\n",
			    __func__));
			error = EINVAL;
			goto fail;
		}
		error = xform_init(sav, XF_IPCOMP);
		break;
	case SADB_X_SATYPE_TCPSIGNATURE:
		if (sav->alg_enc != SADB_EALG_NONE) {
			ipseclog((LOG_DEBUG, "%s: protocol and algorithm "
			    "mismated.\n", __func__));
			error = EINVAL;
			goto fail;
		}
		error = xform_init(sav, XF_TCPSIGNATURE);
		break;
	default:
		ipseclog((LOG_DEBUG, "%s: Invalid satype.\n", __func__));
		error = EPROTONOSUPPORT;
		goto fail;
	}
	if (error) {
		ipseclog((LOG_DEBUG, "%s: unable to initialize SA type %u.\n",
		    __func__, mhp->msg->sadb_msg_satype));
		goto fail;
	}

	/* Handle NAT-T headers */
	error = key_setnatt(sav, mhp);
	if (error != 0)
		goto fail;

	/* Initialize lifetime for CURRENT */
	sav->firstused = 0;
	sav->created = time_second;

	/* lifetimes for HARD and SOFT */
	error = key_updatelifetimes(sav, mhp);
	if (error == 0)
		return (0);
fail:
	key_cleansav(sav);
	return (error);
}

/*
 * subroutine for SADB_GET and SADB_DUMP.
 */
static struct mbuf *
key_setdumpsa(struct secasvar *sav, uint8_t type, uint8_t satype,
    uint32_t seq, uint32_t pid)
{
	struct seclifetime lft_c;
	struct mbuf *result = NULL, *tres = NULL, *m;
	int i, dumporder[] = {
		SADB_EXT_SA, SADB_X_EXT_SA2, SADB_X_EXT_SA_REPLAY,
		SADB_EXT_LIFETIME_HARD, SADB_EXT_LIFETIME_SOFT,
		SADB_EXT_LIFETIME_CURRENT, SADB_EXT_ADDRESS_SRC,
		SADB_EXT_ADDRESS_DST, SADB_EXT_ADDRESS_PROXY,
		SADB_EXT_KEY_AUTH, SADB_EXT_KEY_ENCRYPT,
		SADB_EXT_IDENTITY_SRC, SADB_EXT_IDENTITY_DST,
		SADB_EXT_SENSITIVITY,
		SADB_X_EXT_NAT_T_TYPE,
		SADB_X_EXT_NAT_T_SPORT, SADB_X_EXT_NAT_T_DPORT,
		SADB_X_EXT_NAT_T_OAI, SADB_X_EXT_NAT_T_OAR,
		SADB_X_EXT_NAT_T_FRAG,
	};
	uint32_t replay_count;

	m = key_setsadbmsg(type, 0, satype, seq, pid, sav->refcnt);
	if (m == NULL)
		goto fail;
	result = m;

	for (i = nitems(dumporder) - 1; i >= 0; i--) {
		m = NULL;
		switch (dumporder[i]) {
		case SADB_EXT_SA:
			m = key_setsadbsa(sav);
			if (!m)
				goto fail;
			break;

		case SADB_X_EXT_SA2:
			SECASVAR_LOCK(sav);
			replay_count = sav->replay ? sav->replay->count : 0;
			SECASVAR_UNLOCK(sav);
			m = key_setsadbxsa2(sav->sah->saidx.mode, replay_count,
					sav->sah->saidx.reqid);
			if (!m)
				goto fail;
			break;

		case SADB_X_EXT_SA_REPLAY:
			if (sav->replay == NULL ||
			    sav->replay->wsize <= UINT8_MAX)
				continue;

			m = key_setsadbxsareplay(sav->replay->wsize);
			if (!m)
				goto fail;
			break;

		case SADB_EXT_ADDRESS_SRC:
			m = key_setsadbaddr(SADB_EXT_ADDRESS_SRC,
			    &sav->sah->saidx.src.sa,
			    FULLMASK, IPSEC_ULPROTO_ANY);
			if (!m)
				goto fail;
			break;

		case SADB_EXT_ADDRESS_DST:
			m = key_setsadbaddr(SADB_EXT_ADDRESS_DST,
			    &sav->sah->saidx.dst.sa,
			    FULLMASK, IPSEC_ULPROTO_ANY);
			if (!m)
				goto fail;
			break;

		case SADB_EXT_KEY_AUTH:
			if (!sav->key_auth)
				continue;
			m = key_setkey(sav->key_auth, SADB_EXT_KEY_AUTH);
			if (!m)
				goto fail;
			break;

		case SADB_EXT_KEY_ENCRYPT:
			if (!sav->key_enc)
				continue;
			m = key_setkey(sav->key_enc, SADB_EXT_KEY_ENCRYPT);
			if (!m)
				goto fail;
			break;

		case SADB_EXT_LIFETIME_CURRENT:
			lft_c.addtime = sav->created;
			lft_c.allocations = (uint32_t)counter_u64_fetch(
			    sav->lft_c_allocations);
			lft_c.bytes = counter_u64_fetch(sav->lft_c_bytes);
			lft_c.usetime = sav->firstused;
			m = key_setlifetime(&lft_c, SADB_EXT_LIFETIME_CURRENT);
			if (!m)
				goto fail;
			break;

		case SADB_EXT_LIFETIME_HARD:
			if (!sav->lft_h)
				continue;
			m = key_setlifetime(sav->lft_h, 
					    SADB_EXT_LIFETIME_HARD);
			if (!m)
				goto fail;
			break;

		case SADB_EXT_LIFETIME_SOFT:
			if (!sav->lft_s)
				continue;
			m = key_setlifetime(sav->lft_s, 
					    SADB_EXT_LIFETIME_SOFT);

			if (!m)
				goto fail;
			break;

		case SADB_X_EXT_NAT_T_TYPE:
			if (sav->natt == NULL)
				continue;
			m = key_setsadbxtype(UDP_ENCAP_ESPINUDP);
			if (!m)
				goto fail;
			break;

		case SADB_X_EXT_NAT_T_DPORT:
			if (sav->natt == NULL)
				continue;
			m = key_setsadbxport(sav->natt->dport,
			    SADB_X_EXT_NAT_T_DPORT);
			if (!m)
				goto fail;
			break;

		case SADB_X_EXT_NAT_T_SPORT:
			if (sav->natt == NULL)
				continue;
			m = key_setsadbxport(sav->natt->sport,
			    SADB_X_EXT_NAT_T_SPORT);
			if (!m)
				goto fail;
			break;

		case SADB_X_EXT_NAT_T_OAI:
			if (sav->natt == NULL ||
			    (sav->natt->flags & IPSEC_NATT_F_OAI) == 0)
				continue;
			m = key_setsadbaddr(SADB_X_EXT_NAT_T_OAI,
			    &sav->natt->oai.sa, FULLMASK, IPSEC_ULPROTO_ANY);
			if (!m)
				goto fail;
			break;
		case SADB_X_EXT_NAT_T_OAR:
			if (sav->natt == NULL ||
			    (sav->natt->flags & IPSEC_NATT_F_OAR) == 0)
				continue;
			m = key_setsadbaddr(SADB_X_EXT_NAT_T_OAR,
			    &sav->natt->oar.sa, FULLMASK, IPSEC_ULPROTO_ANY);
			if (!m)
				goto fail;
			break;
		case SADB_X_EXT_NAT_T_FRAG:
			/* We do not (yet) support those. */
			continue;

		case SADB_EXT_ADDRESS_PROXY:
		case SADB_EXT_IDENTITY_SRC:
		case SADB_EXT_IDENTITY_DST:
			/* XXX: should we brought from SPD ? */
		case SADB_EXT_SENSITIVITY:
		default:
			continue;
		}

		if (!m)
			goto fail;
		if (tres)
			m_cat(m, tres);
		tres = m;
	}

	m_cat(result, tres);
	tres = NULL;
	if (result->m_len < sizeof(struct sadb_msg)) {
		result = m_pullup(result, sizeof(struct sadb_msg));
		if (result == NULL)
			goto fail;
	}

	result->m_pkthdr.len = 0;
	for (m = result; m; m = m->m_next)
		result->m_pkthdr.len += m->m_len;

	mtod(result, struct sadb_msg *)->sadb_msg_len =
	    PFKEY_UNIT64(result->m_pkthdr.len);

	return result;

fail:
	m_freem(result);
	m_freem(tres);
	return NULL;
}

/*
 * set data into sadb_msg.
 */
static struct mbuf *
key_setsadbmsg(u_int8_t type, u_int16_t tlen, u_int8_t satype, u_int32_t seq,
    pid_t pid, u_int16_t reserved)
{
	struct mbuf *m;
	struct sadb_msg *p;
	int len;

	len = PFKEY_ALIGN8(sizeof(struct sadb_msg));
	if (len > MCLBYTES)
		return NULL;
	MGETHDR(m, M_NOWAIT, MT_DATA);
	if (m && len > MHLEN) {
		if (!(MCLGET(m, M_NOWAIT))) {
			m_freem(m);
			m = NULL;
		}
	}
	if (!m)
		return NULL;
	m->m_pkthdr.len = m->m_len = len;
	m->m_next = NULL;

	p = mtod(m, struct sadb_msg *);

	bzero(p, len);
	p->sadb_msg_version = PF_KEY_V2;
	p->sadb_msg_type = type;
	p->sadb_msg_errno = 0;
	p->sadb_msg_satype = satype;
	p->sadb_msg_len = PFKEY_UNIT64(tlen);
	p->sadb_msg_reserved = reserved;
	p->sadb_msg_seq = seq;
	p->sadb_msg_pid = (u_int32_t)pid;

	return m;
}

/*
 * copy secasvar data into sadb_address.
 */
static struct mbuf *
key_setsadbsa(struct secasvar *sav)
{
	struct mbuf *m;
	struct sadb_sa *p;
	int len;

	len = PFKEY_ALIGN8(sizeof(struct sadb_sa));
	m = m_get2(len, M_NOWAIT, MT_DATA, 0);
	if (m == NULL)
		return (NULL);
	m_align(m, len);
	m->m_len = len;
	p = mtod(m, struct sadb_sa *);
	bzero(p, len);
	p->sadb_sa_len = PFKEY_UNIT64(len);
	p->sadb_sa_exttype = SADB_EXT_SA;
	p->sadb_sa_spi = sav->spi;
	p->sadb_sa_replay = sav->replay ?
	    (sav->replay->wsize > UINT8_MAX ? UINT8_MAX :
		sav->replay->wsize): 0;
	p->sadb_sa_state = sav->state;
	p->sadb_sa_auth = sav->alg_auth;
	p->sadb_sa_encrypt = sav->alg_enc;
	p->sadb_sa_flags = sav->flags & SADB_KEY_FLAGS_MAX;
	return (m);
}

/*
 * set data into sadb_address.
 */
static struct mbuf *
key_setsadbaddr(u_int16_t exttype, const struct sockaddr *saddr,
    u_int8_t prefixlen, u_int16_t ul_proto)
{
	struct mbuf *m;
	struct sadb_address *p;
	size_t len;

	len = PFKEY_ALIGN8(sizeof(struct sadb_address)) +
	    PFKEY_ALIGN8(saddr->sa_len);
	m = m_get2(len, M_NOWAIT, MT_DATA, 0);
	if (m == NULL)
		return (NULL);
	m_align(m, len);
	m->m_len = len;
	p = mtod(m, struct sadb_address *);

	bzero(p, len);
	p->sadb_address_len = PFKEY_UNIT64(len);
	p->sadb_address_exttype = exttype;
	p->sadb_address_proto = ul_proto;
	if (prefixlen == FULLMASK) {
		switch (saddr->sa_family) {
		case AF_INET:
			prefixlen = sizeof(struct in_addr) << 3;
			break;
		case AF_INET6:
			prefixlen = sizeof(struct in6_addr) << 3;
			break;
		default:
			; /*XXX*/
		}
	}
	p->sadb_address_prefixlen = prefixlen;
	p->sadb_address_reserved = 0;

	bcopy(saddr,
	    mtod(m, caddr_t) + PFKEY_ALIGN8(sizeof(struct sadb_address)),
	    saddr->sa_len);

	return m;
}

/*
 * set data into sadb_x_sa2.
 */
static struct mbuf *
key_setsadbxsa2(u_int8_t mode, u_int32_t seq, u_int32_t reqid)
{
	struct mbuf *m;
	struct sadb_x_sa2 *p;
	size_t len;

	len = PFKEY_ALIGN8(sizeof(struct sadb_x_sa2));
	m = m_get2(len, M_NOWAIT, MT_DATA, 0);
	if (m == NULL)
		return (NULL);
	m_align(m, len);
	m->m_len = len;
	p = mtod(m, struct sadb_x_sa2 *);

	bzero(p, len);
	p->sadb_x_sa2_len = PFKEY_UNIT64(len);
	p->sadb_x_sa2_exttype = SADB_X_EXT_SA2;
	p->sadb_x_sa2_mode = mode;
	p->sadb_x_sa2_reserved1 = 0;
	p->sadb_x_sa2_reserved2 = 0;
	p->sadb_x_sa2_sequence = seq;
	p->sadb_x_sa2_reqid = reqid;

	return m;
}

/*
 * Set data into sadb_x_sa_replay.
 */
static struct mbuf *
key_setsadbxsareplay(u_int32_t replay)
{
	struct mbuf *m;
	struct sadb_x_sa_replay *p;
	size_t len;

	len = PFKEY_ALIGN8(sizeof(struct sadb_x_sa_replay));
	m = m_get2(len, M_NOWAIT, MT_DATA, 0);
	if (m == NULL)
		return (NULL);
	m_align(m, len);
	m->m_len = len;
	p = mtod(m, struct sadb_x_sa_replay *);

	bzero(p, len);
	p->sadb_x_sa_replay_len = PFKEY_UNIT64(len);
	p->sadb_x_sa_replay_exttype = SADB_X_EXT_SA_REPLAY;
	p->sadb_x_sa_replay_replay = (replay << 3);

	return m;
}

/*
 * Set a type in sadb_x_nat_t_type.
 */
static struct mbuf *
key_setsadbxtype(u_int16_t type)
{
	struct mbuf *m;
	size_t len;
	struct sadb_x_nat_t_type *p;

	len = PFKEY_ALIGN8(sizeof(struct sadb_x_nat_t_type));

	m = m_get2(len, M_NOWAIT, MT_DATA, 0);
	if (m == NULL)
		return (NULL);
	m_align(m, len);
	m->m_len = len;
	p = mtod(m, struct sadb_x_nat_t_type *);

	bzero(p, len);
	p->sadb_x_nat_t_type_len = PFKEY_UNIT64(len);
	p->sadb_x_nat_t_type_exttype = SADB_X_EXT_NAT_T_TYPE;
	p->sadb_x_nat_t_type_type = type;

	return (m);
}
/*
 * Set a port in sadb_x_nat_t_port.
 * In contrast to default RFC 2367 behaviour, port is in network byte order.
 */
static struct mbuf *
key_setsadbxport(u_int16_t port, u_int16_t type)
{
	struct mbuf *m;
	size_t len;
	struct sadb_x_nat_t_port *p;

	len = PFKEY_ALIGN8(sizeof(struct sadb_x_nat_t_port));

	m = m_get2(len, M_NOWAIT, MT_DATA, 0);
	if (m == NULL)
		return (NULL);
	m_align(m, len);
	m->m_len = len;
	p = mtod(m, struct sadb_x_nat_t_port *);

	bzero(p, len);
	p->sadb_x_nat_t_port_len = PFKEY_UNIT64(len);
	p->sadb_x_nat_t_port_exttype = type;
	p->sadb_x_nat_t_port_port = port;

	return (m);
}

/*
 * Get port from sockaddr. Port is in network byte order.
 */
uint16_t
key_portfromsaddr(struct sockaddr *sa)
{

	switch (sa->sa_family) {
#ifdef INET
	case AF_INET:
		return ((struct sockaddr_in *)sa)->sin_port;
#endif
#ifdef INET6
	case AF_INET6:
		return ((struct sockaddr_in6 *)sa)->sin6_port;
#endif
	}
	return (0);
}

/*
 * Set port in struct sockaddr. Port is in network byte order.
 */
void
key_porttosaddr(struct sockaddr *sa, uint16_t port)
{

	switch (sa->sa_family) {
#ifdef INET
	case AF_INET:
		((struct sockaddr_in *)sa)->sin_port = port;
		break;
#endif
#ifdef INET6
	case AF_INET6:
		((struct sockaddr_in6 *)sa)->sin6_port = port;
		break;
#endif
	default:
		ipseclog((LOG_DEBUG, "%s: unexpected address family %d.\n",
			__func__, sa->sa_family));
		break;
	}
}

/*
 * set data into sadb_x_policy
 */
static struct mbuf *
key_setsadbxpolicy(u_int16_t type, u_int8_t dir, u_int32_t id, u_int32_t priority)
{
	struct mbuf *m;
	struct sadb_x_policy *p;
	size_t len;

	len = PFKEY_ALIGN8(sizeof(struct sadb_x_policy));
	m = m_get2(len, M_NOWAIT, MT_DATA, 0);
	if (m == NULL)
		return (NULL);
	m_align(m, len);
	m->m_len = len;
	p = mtod(m, struct sadb_x_policy *);

	bzero(p, len);
	p->sadb_x_policy_len = PFKEY_UNIT64(len);
	p->sadb_x_policy_exttype = SADB_X_EXT_POLICY;
	p->sadb_x_policy_type = type;
	p->sadb_x_policy_dir = dir;
	p->sadb_x_policy_id = id;
	p->sadb_x_policy_priority = priority;

	return m;
}

/* %%% utilities */
/* Take a key message (sadb_key) from the socket and turn it into one
 * of the kernel's key structures (seckey).
 *
 * IN: pointer to the src
 * OUT: NULL no more memory
 */
struct seckey *
key_dup_keymsg(const struct sadb_key *src, size_t len,
    struct malloc_type *type)
{
	struct seckey *dst;

	dst = malloc(sizeof(*dst), type, M_NOWAIT);
	if (dst != NULL) {
		dst->bits = src->sadb_key_bits;
		dst->key_data = malloc(len, type, M_NOWAIT);
		if (dst->key_data != NULL) {
			bcopy((const char *)(src + 1), dst->key_data, len);
		} else {
			ipseclog((LOG_DEBUG, "%s: No more memory.\n",
			    __func__));
			free(dst, type);
			dst = NULL;
		}
	} else {
		ipseclog((LOG_DEBUG, "%s: No more memory.\n",
		    __func__));

	}
	return (dst);
}

/* Take a lifetime message (sadb_lifetime) passed in on a socket and
 * turn it into one of the kernel's lifetime structures (seclifetime).
 *
 * IN: pointer to the destination, source and malloc type
 * OUT: NULL, no more memory
 */

static struct seclifetime *
key_dup_lifemsg(const struct sadb_lifetime *src, struct malloc_type *type)
{
	struct seclifetime *dst;

	dst = malloc(sizeof(*dst), type, M_NOWAIT);
	if (dst == NULL) {
		ipseclog((LOG_DEBUG, "%s: No more memory.\n", __func__));
		return (NULL);
	}
	dst->allocations = src->sadb_lifetime_allocations;
	dst->bytes = src->sadb_lifetime_bytes;
	dst->addtime = src->sadb_lifetime_addtime;
	dst->usetime = src->sadb_lifetime_usetime;
	return (dst);
}

/*
 * compare two secasindex structure.
 * flag can specify to compare 2 saidxes.
 * compare two secasindex structure without both mode and reqid.
 * don't compare port.
 * IN:  
 *      saidx0: source, it can be in SAD.
 *      saidx1: object.
 * OUT: 
 *      1 : equal
 *      0 : not equal
 */
static int
key_cmpsaidx(const struct secasindex *saidx0, const struct secasindex *saidx1,
    int flag)
{

	/* sanity */
	if (saidx0 == NULL && saidx1 == NULL)
		return 1;

	if (saidx0 == NULL || saidx1 == NULL)
		return 0;

	if (saidx0->proto != saidx1->proto)
		return 0;

	if (flag == CMP_EXACTLY) {
		if (saidx0->mode != saidx1->mode)
			return 0;
		if (saidx0->reqid != saidx1->reqid)
			return 0;
		if (bcmp(&saidx0->src, &saidx1->src,
		    saidx0->src.sa.sa_len) != 0 ||
		    bcmp(&saidx0->dst, &saidx1->dst,
		    saidx0->dst.sa.sa_len) != 0)
			return 0;
	} else {

		/* CMP_MODE_REQID, CMP_REQID, CMP_HEAD */
		if (flag == CMP_MODE_REQID || flag == CMP_REQID) {
			/*
			 * If reqid of SPD is non-zero, unique SA is required.
			 * The result must be of same reqid in this case.
			 */
			if (saidx1->reqid != 0 &&
			    saidx0->reqid != saidx1->reqid)
				return 0;
		}

		if (flag == CMP_MODE_REQID) {
			if (saidx0->mode != IPSEC_MODE_ANY
			 && saidx0->mode != saidx1->mode)
				return 0;
		}

		if (key_sockaddrcmp(&saidx0->src.sa, &saidx1->src.sa, 0) != 0)
			return 0;
		if (key_sockaddrcmp(&saidx0->dst.sa, &saidx1->dst.sa, 0) != 0)
			return 0;
	}

	return 1;
}

/*
 * compare two secindex structure exactly.
 * IN:
 *	spidx0: source, it is often in SPD.
 *	spidx1: object, it is often from PFKEY message.
 * OUT:
 *	1 : equal
 *	0 : not equal
 */
static int
key_cmpspidx_exactly(struct secpolicyindex *spidx0,
    struct secpolicyindex *spidx1)
{
	/* sanity */
	if (spidx0 == NULL && spidx1 == NULL)
		return 1;

	if (spidx0 == NULL || spidx1 == NULL)
		return 0;

	if (spidx0->prefs != spidx1->prefs
	 || spidx0->prefd != spidx1->prefd
	 || spidx0->ul_proto != spidx1->ul_proto
	 || spidx0->dir != spidx1->dir)
		return 0;

	return key_sockaddrcmp(&spidx0->src.sa, &spidx1->src.sa, 1) == 0 &&
	       key_sockaddrcmp(&spidx0->dst.sa, &spidx1->dst.sa, 1) == 0;
}

/*
 * compare two secindex structure with mask.
 * IN:
 *	spidx0: source, it is often in SPD.
 *	spidx1: object, it is often from IP header.
 * OUT:
 *	1 : equal
 *	0 : not equal
 */
static int
key_cmpspidx_withmask(struct secpolicyindex *spidx0,
    struct secpolicyindex *spidx1)
{
	/* sanity */
	if (spidx0 == NULL && spidx1 == NULL)
		return 1;

	if (spidx0 == NULL || spidx1 == NULL)
		return 0;

	if (spidx0->src.sa.sa_family != spidx1->src.sa.sa_family ||
	    spidx0->dst.sa.sa_family != spidx1->dst.sa.sa_family ||
	    spidx0->src.sa.sa_len != spidx1->src.sa.sa_len ||
	    spidx0->dst.sa.sa_len != spidx1->dst.sa.sa_len)
		return 0;

	/* if spidx.ul_proto == IPSEC_ULPROTO_ANY, ignore. */
	if (spidx0->ul_proto != (u_int16_t)IPSEC_ULPROTO_ANY
	 && spidx0->ul_proto != spidx1->ul_proto)
		return 0;

	switch (spidx0->src.sa.sa_family) {
	case AF_INET:
		if (spidx0->src.sin.sin_port != IPSEC_PORT_ANY
		 && spidx0->src.sin.sin_port != spidx1->src.sin.sin_port)
			return 0;
		if (!key_bbcmp(&spidx0->src.sin.sin_addr,
		    &spidx1->src.sin.sin_addr, spidx0->prefs))
			return 0;
		break;
	case AF_INET6:
		if (spidx0->src.sin6.sin6_port != IPSEC_PORT_ANY
		 && spidx0->src.sin6.sin6_port != spidx1->src.sin6.sin6_port)
			return 0;
		/*
		 * scope_id check. if sin6_scope_id is 0, we regard it
		 * as a wildcard scope, which matches any scope zone ID. 
		 */
		if (spidx0->src.sin6.sin6_scope_id &&
		    spidx1->src.sin6.sin6_scope_id &&
		    spidx0->src.sin6.sin6_scope_id != spidx1->src.sin6.sin6_scope_id)
			return 0;
		if (!key_bbcmp(&spidx0->src.sin6.sin6_addr,
		    &spidx1->src.sin6.sin6_addr, spidx0->prefs))
			return 0;
		break;
	default:
		/* XXX */
		if (bcmp(&spidx0->src, &spidx1->src, spidx0->src.sa.sa_len) != 0)
			return 0;
		break;
	}

	switch (spidx0->dst.sa.sa_family) {
	case AF_INET:
		if (spidx0->dst.sin.sin_port != IPSEC_PORT_ANY
		 && spidx0->dst.sin.sin_port != spidx1->dst.sin.sin_port)
			return 0;
		if (!key_bbcmp(&spidx0->dst.sin.sin_addr,
		    &spidx1->dst.sin.sin_addr, spidx0->prefd))
			return 0;
		break;
	case AF_INET6:
		if (spidx0->dst.sin6.sin6_port != IPSEC_PORT_ANY
		 && spidx0->dst.sin6.sin6_port != spidx1->dst.sin6.sin6_port)
			return 0;
		/*
		 * scope_id check. if sin6_scope_id is 0, we regard it
		 * as a wildcard scope, which matches any scope zone ID. 
		 */
		if (spidx0->dst.sin6.sin6_scope_id &&
		    spidx1->dst.sin6.sin6_scope_id &&
		    spidx0->dst.sin6.sin6_scope_id != spidx1->dst.sin6.sin6_scope_id)
			return 0;
		if (!key_bbcmp(&spidx0->dst.sin6.sin6_addr,
		    &spidx1->dst.sin6.sin6_addr, spidx0->prefd))
			return 0;
		break;
	default:
		/* XXX */
		if (bcmp(&spidx0->dst, &spidx1->dst, spidx0->dst.sa.sa_len) != 0)
			return 0;
		break;
	}

	/* XXX Do we check other field ?  e.g. flowinfo */

	return 1;
}

#ifdef satosin
#undef satosin
#endif
#define satosin(s) ((const struct sockaddr_in *)s)
#ifdef satosin6
#undef satosin6
#endif
#define satosin6(s) ((const struct sockaddr_in6 *)s)
/* returns 0 on match */
int
key_sockaddrcmp(const struct sockaddr *sa1, const struct sockaddr *sa2,
    int port)
{
	if (sa1->sa_family != sa2->sa_family || sa1->sa_len != sa2->sa_len)
		return 1;

	switch (sa1->sa_family) {
#ifdef INET
	case AF_INET:
		if (sa1->sa_len != sizeof(struct sockaddr_in))
			return 1;
		if (satosin(sa1)->sin_addr.s_addr !=
		    satosin(sa2)->sin_addr.s_addr) {
			return 1;
		}
		if (port && satosin(sa1)->sin_port != satosin(sa2)->sin_port)
			return 1;
		break;
#endif
#ifdef INET6
	case AF_INET6:
		if (sa1->sa_len != sizeof(struct sockaddr_in6))
			return 1;	/*EINVAL*/
		if (satosin6(sa1)->sin6_scope_id !=
		    satosin6(sa2)->sin6_scope_id) {
			return 1;
		}
		if (!IN6_ARE_ADDR_EQUAL(&satosin6(sa1)->sin6_addr,
		    &satosin6(sa2)->sin6_addr)) {
			return 1;
		}
		if (port &&
		    satosin6(sa1)->sin6_port != satosin6(sa2)->sin6_port) {
			return 1;
		}
		break;
#endif
	default:
		if (bcmp(sa1, sa2, sa1->sa_len) != 0)
			return 1;
		break;
	}

	return 0;
}

/* returns 0 on match */
int
key_sockaddrcmp_withmask(const struct sockaddr *sa1,
    const struct sockaddr *sa2, size_t mask)
{
	if (sa1->sa_family != sa2->sa_family || sa1->sa_len != sa2->sa_len)
		return (1);

	switch (sa1->sa_family) {
#ifdef INET
	case AF_INET:
		return (!key_bbcmp(&satosin(sa1)->sin_addr,
		    &satosin(sa2)->sin_addr, mask));
#endif
#ifdef INET6
	case AF_INET6:
		if (satosin6(sa1)->sin6_scope_id !=
		    satosin6(sa2)->sin6_scope_id)
			return (1);
		return (!key_bbcmp(&satosin6(sa1)->sin6_addr,
		    &satosin6(sa2)->sin6_addr, mask));
#endif
	}
	return (1);
}
#undef satosin
#undef satosin6

/*
 * compare two buffers with mask.
 * IN:
 *	addr1: source
 *	addr2: object
 *	bits:  Number of bits to compare
 * OUT:
 *	1 : equal
 *	0 : not equal
 */
static int
key_bbcmp(const void *a1, const void *a2, u_int bits)
{
	const unsigned char *p1 = a1;
	const unsigned char *p2 = a2;

	/* XXX: This could be considerably faster if we compare a word
	 * at a time, but it is complicated on LSB Endian machines */

	/* Handle null pointers */
	if (p1 == NULL || p2 == NULL)
		return (p1 == p2);

	while (bits >= 8) {
		if (*p1++ != *p2++)
			return 0;
		bits -= 8;
	}

	if (bits > 0) {
		u_int8_t mask = ~((1<<(8-bits))-1);
		if ((*p1 & mask) != (*p2 & mask))
			return 0;
	}
	return 1;	/* Match! */
}

static void
key_flush_spd(time_t now)
{
	SPTREE_RLOCK_TRACKER;
	struct secpolicy_list drainq;
	struct secpolicy *sp, *nextsp;
	u_int dir;

	LIST_INIT(&drainq);
	SPTREE_RLOCK();
	for (dir = 0; dir < IPSEC_DIR_MAX; dir++) {
		TAILQ_FOREACH(sp, &V_sptree[dir], chain) {
			if (sp->lifetime == 0 && sp->validtime == 0)
				continue;
			if ((sp->lifetime &&
			    now - sp->created > sp->lifetime) ||
			    (sp->validtime &&
			    now - sp->lastused > sp->validtime)) {
				/* Hold extra reference to send SPDEXPIRE */
				SP_ADDREF(sp);
				LIST_INSERT_HEAD(&drainq, sp, drainq);
			}
		}
	}
	SPTREE_RUNLOCK();
	if (LIST_EMPTY(&drainq))
		return;

	SPTREE_WLOCK();
	sp = LIST_FIRST(&drainq);
	while (sp != NULL) {
		nextsp = LIST_NEXT(sp, drainq);
		/* Check that SP is still linked */
		if (sp->state != IPSEC_SPSTATE_ALIVE) {
			LIST_REMOVE(sp, drainq);
			key_freesp(&sp); /* release extra reference */
			sp = nextsp;
			continue;
		}
		TAILQ_REMOVE(&V_sptree[sp->spidx.dir], sp, chain);
		V_spd_size--;
		LIST_REMOVE(sp, idhash);
		sp->state = IPSEC_SPSTATE_DEAD;
		sp = nextsp;
	}
	V_sp_genid++;
	SPTREE_WUNLOCK();
	if (SPDCACHE_ENABLED())
		spdcache_clear();

	sp = LIST_FIRST(&drainq);
	while (sp != NULL) {
		nextsp = LIST_NEXT(sp, drainq);
		key_spdexpire(sp);
		key_freesp(&sp); /* release extra reference */
		key_freesp(&sp); /* release last reference */
		sp = nextsp;
	}
}

static void
key_flush_sad(time_t now)
{
	SAHTREE_RLOCK_TRACKER;
	struct secashead_list emptyq;
	struct secasvar_list drainq, hexpireq, sexpireq, freeq;
	struct secashead *sah, *nextsah;
	struct secasvar *sav, *nextsav;

	LIST_INIT(&drainq);
	LIST_INIT(&hexpireq);
	LIST_INIT(&sexpireq);
	LIST_INIT(&emptyq);

	SAHTREE_RLOCK();
	TAILQ_FOREACH(sah, &V_sahtree, chain) {
		/* Check for empty SAH */
		if (TAILQ_EMPTY(&sah->savtree_larval) &&
		    TAILQ_EMPTY(&sah->savtree_alive)) {
			SAH_ADDREF(sah);
			LIST_INSERT_HEAD(&emptyq, sah, drainq);
			continue;
		}
		/* Add all stale LARVAL SAs into drainq */
		TAILQ_FOREACH(sav, &sah->savtree_larval, chain) {
			if (now - sav->created < V_key_larval_lifetime)
				continue;
			SAV_ADDREF(sav);
			LIST_INSERT_HEAD(&drainq, sav, drainq);
		}
		TAILQ_FOREACH(sav, &sah->savtree_alive, chain) {
			/* lifetimes aren't specified */
			if (sav->lft_h == NULL)
				continue;
			SECASVAR_LOCK(sav);
			/*
			 * Check again with lock held, because it may
			 * be updated by SADB_UPDATE.
			 */
			if (sav->lft_h == NULL) {
				SECASVAR_UNLOCK(sav);
				continue;
			}
			/*
			 * RFC 2367:
			 * HARD lifetimes MUST take precedence over SOFT
			 * lifetimes, meaning if the HARD and SOFT lifetimes
			 * are the same, the HARD lifetime will appear on the
			 * EXPIRE message.
			 */
			/* check HARD lifetime */
			if ((sav->lft_h->addtime != 0 &&
			    now - sav->created > sav->lft_h->addtime) ||
			    (sav->lft_h->usetime != 0 && sav->firstused &&
			    now - sav->firstused > sav->lft_h->usetime) ||
			    (sav->lft_h->bytes != 0 && counter_u64_fetch(
			        sav->lft_c_bytes) > sav->lft_h->bytes)) {
				SECASVAR_UNLOCK(sav);
				SAV_ADDREF(sav);
				LIST_INSERT_HEAD(&hexpireq, sav, drainq);
				continue;
			}
			/* check SOFT lifetime (only for MATURE SAs) */
			if (sav->state == SADB_SASTATE_MATURE && (
			    (sav->lft_s->addtime != 0 &&
			    now - sav->created > sav->lft_s->addtime) ||
			    (sav->lft_s->usetime != 0 && sav->firstused &&
			    now - sav->firstused > sav->lft_s->usetime) ||
			    (sav->lft_s->bytes != 0 && counter_u64_fetch(
				sav->lft_c_bytes) > sav->lft_s->bytes))) {
				SECASVAR_UNLOCK(sav);
				SAV_ADDREF(sav);
				LIST_INSERT_HEAD(&sexpireq, sav, drainq);
				continue;
			}
			SECASVAR_UNLOCK(sav);
		}
	}
	SAHTREE_RUNLOCK();

	if (LIST_EMPTY(&emptyq) && LIST_EMPTY(&drainq) &&
	    LIST_EMPTY(&hexpireq) && LIST_EMPTY(&sexpireq))
		return;

	LIST_INIT(&freeq);
	SAHTREE_WLOCK();
	/* Unlink stale LARVAL SAs */
	sav = LIST_FIRST(&drainq);
	while (sav != NULL) {
		nextsav = LIST_NEXT(sav, drainq);
		/* Check that SA is still LARVAL */
		if (sav->state != SADB_SASTATE_LARVAL) {
			LIST_REMOVE(sav, drainq);
			LIST_INSERT_HEAD(&freeq, sav, drainq);
			sav = nextsav;
			continue;
		}
		TAILQ_REMOVE(&sav->sah->savtree_larval, sav, chain);
		LIST_REMOVE(sav, spihash);
		sav->state = SADB_SASTATE_DEAD;
		sav = nextsav;
	}
	/* Unlink all SAs with expired HARD lifetime */
	sav = LIST_FIRST(&hexpireq);
	while (sav != NULL) {
		nextsav = LIST_NEXT(sav, drainq);
		/* Check that SA is not unlinked */
		if (sav->state == SADB_SASTATE_DEAD) {
			LIST_REMOVE(sav, drainq);
			LIST_INSERT_HEAD(&freeq, sav, drainq);
			sav = nextsav;
			continue;
		}
		TAILQ_REMOVE(&sav->sah->savtree_alive, sav, chain);
		LIST_REMOVE(sav, spihash);
		sav->state = SADB_SASTATE_DEAD;
		sav = nextsav;
	}
	/* Mark all SAs with expired SOFT lifetime as DYING */
	sav = LIST_FIRST(&sexpireq);
	while (sav != NULL) {
		nextsav = LIST_NEXT(sav, drainq);
		/* Check that SA is not unlinked */
		if (sav->state == SADB_SASTATE_DEAD) {
			LIST_REMOVE(sav, drainq);
			LIST_INSERT_HEAD(&freeq, sav, drainq);
			sav = nextsav;
			continue;
		}
		/*
		 * NOTE: this doesn't change SA order in the chain.
		 */
		sav->state = SADB_SASTATE_DYING;
		sav = nextsav;
	}
	/* Unlink empty SAHs */
	sah = LIST_FIRST(&emptyq);
	while (sah != NULL) {
		nextsah = LIST_NEXT(sah, drainq);
		/* Check that SAH is still empty and not unlinked */
		if (sah->state == SADB_SASTATE_DEAD ||
		    !TAILQ_EMPTY(&sah->savtree_larval) ||
		    !TAILQ_EMPTY(&sah->savtree_alive)) {
			LIST_REMOVE(sah, drainq);
			key_freesah(&sah); /* release extra reference */
			sah = nextsah;
			continue;
		}
		TAILQ_REMOVE(&V_sahtree, sah, chain);
		LIST_REMOVE(sah, addrhash);
		sah->state = SADB_SASTATE_DEAD;
		sah = nextsah;
	}
	SAHTREE_WUNLOCK();

	/* Send SPDEXPIRE messages */
	sav = LIST_FIRST(&hexpireq);
	while (sav != NULL) {
		nextsav = LIST_NEXT(sav, drainq);
		key_expire(sav, 1);
		key_freesah(&sav->sah); /* release reference from SAV */
		key_freesav(&sav); /* release extra reference */
		key_freesav(&sav); /* release last reference */
		sav = nextsav;
	}
	sav = LIST_FIRST(&sexpireq);
	while (sav != NULL) {
		nextsav = LIST_NEXT(sav, drainq);
		key_expire(sav, 0);
		key_freesav(&sav); /* release extra reference */
		sav = nextsav;
	}
	/* Free stale LARVAL SAs */
	sav = LIST_FIRST(&drainq);
	while (sav != NULL) {
		nextsav = LIST_NEXT(sav, drainq);
		key_freesah(&sav->sah); /* release reference from SAV */
		key_freesav(&sav); /* release extra reference */
		key_freesav(&sav); /* release last reference */
		sav = nextsav;
	}
	/* Free SAs that were unlinked/changed by someone else */
	sav = LIST_FIRST(&freeq);
	while (sav != NULL) {
		nextsav = LIST_NEXT(sav, drainq);
		key_freesav(&sav); /* release extra reference */
		sav = nextsav;
	}
	/* Free empty SAH */
	sah = LIST_FIRST(&emptyq);
	while (sah != NULL) {
		nextsah = LIST_NEXT(sah, drainq);
		key_freesah(&sah); /* release extra reference */
		key_freesah(&sah); /* release last reference */
		sah = nextsah;
	}
}

static void
key_flush_acq(time_t now)
{
	struct secacq *acq, *nextacq;

	/* ACQ tree */
	ACQ_LOCK();
	acq = LIST_FIRST(&V_acqtree);
	while (acq != NULL) {
		nextacq = LIST_NEXT(acq, chain);
		if (now - acq->created > V_key_blockacq_lifetime) {
			LIST_REMOVE(acq, chain);
			LIST_REMOVE(acq, addrhash);
			LIST_REMOVE(acq, seqhash);
			free(acq, M_IPSEC_SAQ);
		}
		acq = nextacq;
	}
	ACQ_UNLOCK();
}

static void
key_flush_spacq(time_t now)
{
	struct secspacq *acq, *nextacq;

	/* SP ACQ tree */
	SPACQ_LOCK();
	for (acq = LIST_FIRST(&V_spacqtree); acq != NULL; acq = nextacq) {
		nextacq = LIST_NEXT(acq, chain);
		if (now - acq->created > V_key_blockacq_lifetime
		 && __LIST_CHAINED(acq)) {
			LIST_REMOVE(acq, chain);
			free(acq, M_IPSEC_SAQ);
		}
	}
	SPACQ_UNLOCK();
}

/*
 * time handler.
 * scanning SPD and SAD to check status for each entries,
 * and do to remove or to expire.
 * XXX: year 2038 problem may remain.
 */
static void
key_timehandler(void *arg)
{
	VNET_ITERATOR_DECL(vnet_iter);
	time_t now = time_second;

	VNET_LIST_RLOCK_NOSLEEP();
	VNET_FOREACH(vnet_iter) {
		CURVNET_SET(vnet_iter);
		key_flush_spd(now);
		key_flush_sad(now);
		key_flush_acq(now);
		key_flush_spacq(now);
		CURVNET_RESTORE();
	}
	VNET_LIST_RUNLOCK_NOSLEEP();

#ifndef IPSEC_DEBUG2
	/* do exchange to tick time !! */
	callout_schedule(&key_timer, hz);
#endif /* IPSEC_DEBUG2 */
}

u_long
key_random()
{
	u_long value;

	key_randomfill(&value, sizeof(value));
	return value;
}

void
key_randomfill(void *p, size_t l)
{
	size_t n;
	u_long v;
	static int warn = 1;

	n = 0;
	n = (size_t)read_random(p, (u_int)l);
	/* last resort */
	while (n < l) {
		v = random();
		bcopy(&v, (u_int8_t *)p + n,
		    l - n < sizeof(v) ? l - n : sizeof(v));
		n += sizeof(v);

		if (warn) {
			printf("WARNING: pseudo-random number generator "
			    "used for IPsec processing\n");
			warn = 0;
		}
	}
}

/*
 * map SADB_SATYPE_* to IPPROTO_*.
 * if satype == SADB_SATYPE then satype is mapped to ~0.
 * OUT:
 *	0: invalid satype.
 */
static uint8_t
key_satype2proto(uint8_t satype)
{
	switch (satype) {
	case SADB_SATYPE_UNSPEC:
		return IPSEC_PROTO_ANY;
	case SADB_SATYPE_AH:
		return IPPROTO_AH;
	case SADB_SATYPE_ESP:
		return IPPROTO_ESP;
	case SADB_X_SATYPE_IPCOMP:
		return IPPROTO_IPCOMP;
	case SADB_X_SATYPE_TCPSIGNATURE:
		return IPPROTO_TCP;
	default:
		return 0;
	}
	/* NOTREACHED */
}

/*
 * map IPPROTO_* to SADB_SATYPE_*
 * OUT:
 *	0: invalid protocol type.
 */
static uint8_t
key_proto2satype(uint8_t proto)
{
	switch (proto) {
	case IPPROTO_AH:
		return SADB_SATYPE_AH;
	case IPPROTO_ESP:
		return SADB_SATYPE_ESP;
	case IPPROTO_IPCOMP:
		return SADB_X_SATYPE_IPCOMP;
	case IPPROTO_TCP:
		return SADB_X_SATYPE_TCPSIGNATURE;
	default:
		return 0;
	}
	/* NOTREACHED */
}

/* %%% PF_KEY */
/*
 * SADB_GETSPI processing is to receive
 *	<base, (SA2), src address, dst address, (SPI range)>
 * from the IKMPd, to assign a unique spi value, to hang on the INBOUND
 * tree with the status of LARVAL, and send
 *	<base, SA(*), address(SD)>
 * to the IKMPd.
 *
 * IN:	mhp: pointer to the pointer to each header.
 * OUT:	NULL if fail.
 *	other if success, return pointer to the message to send.
 */
static int
key_getspi(struct socket *so, struct mbuf *m, const struct sadb_msghdr *mhp)
{
	struct secasindex saidx;
	struct sadb_address *src0, *dst0;
	struct secasvar *sav;
	uint32_t reqid, spi;
	int error;
	uint8_t mode, proto;

	IPSEC_ASSERT(so != NULL, ("null socket"));
	IPSEC_ASSERT(m != NULL, ("null mbuf"));
	IPSEC_ASSERT(mhp != NULL, ("null msghdr"));
	IPSEC_ASSERT(mhp->msg != NULL, ("null msg"));

	if (SADB_CHECKHDR(mhp, SADB_EXT_ADDRESS_SRC) ||
	    SADB_CHECKHDR(mhp, SADB_EXT_ADDRESS_DST)
#ifdef PFKEY_STRICT_CHECKS
	    || SADB_CHECKHDR(mhp, SADB_EXT_SPIRANGE)
#endif
	    ) {
		ipseclog((LOG_DEBUG,
		    "%s: invalid message: missing required header.\n",
		    __func__));
		error = EINVAL;
		goto fail;
	}
	if (SADB_CHECKLEN(mhp, SADB_EXT_ADDRESS_SRC) ||
	    SADB_CHECKLEN(mhp, SADB_EXT_ADDRESS_DST)
#ifdef PFKEY_STRICT_CHECKS
	    || SADB_CHECKLEN(mhp, SADB_EXT_SPIRANGE)
#endif
	    ) {
		ipseclog((LOG_DEBUG,
		    "%s: invalid message: wrong header size.\n", __func__));
		error = EINVAL;
		goto fail;
	}
	if (SADB_CHECKHDR(mhp, SADB_X_EXT_SA2)) {
		mode = IPSEC_MODE_ANY;
		reqid = 0;
	} else {
		if (SADB_CHECKLEN(mhp, SADB_X_EXT_SA2)) {
			ipseclog((LOG_DEBUG,
			    "%s: invalid message: wrong header size.\n",
			    __func__));
			error = EINVAL;
			goto fail;
		}
		mode = ((struct sadb_x_sa2 *)
		    mhp->ext[SADB_X_EXT_SA2])->sadb_x_sa2_mode;
		reqid = ((struct sadb_x_sa2 *)
		    mhp->ext[SADB_X_EXT_SA2])->sadb_x_sa2_reqid;
	}

	src0 = (struct sadb_address *)(mhp->ext[SADB_EXT_ADDRESS_SRC]);
	dst0 = (struct sadb_address *)(mhp->ext[SADB_EXT_ADDRESS_DST]);

	/* map satype to proto */
	if ((proto = key_satype2proto(mhp->msg->sadb_msg_satype)) == 0) {
		ipseclog((LOG_DEBUG, "%s: invalid satype is passed.\n",
			__func__));
		error = EINVAL;
		goto fail;
	}
	error = key_checksockaddrs((struct sockaddr *)(src0 + 1),
	    (struct sockaddr *)(dst0 + 1));
	if (error != 0) {
		ipseclog((LOG_DEBUG, "%s: invalid sockaddr.\n", __func__));
		error = EINVAL;
		goto fail;
	}
	KEY_SETSECASIDX(proto, mode, reqid, src0 + 1, dst0 + 1, &saidx);

	/* SPI allocation */
	spi = key_do_getnewspi(
	    (struct sadb_spirange *)mhp->ext[SADB_EXT_SPIRANGE], &saidx);
	if (spi == 0) {
		/*
		 * Requested SPI or SPI range is not available or
		 * already used.
		 */
		error = EEXIST;
		goto fail;
	}
	sav = key_newsav(mhp, &saidx, spi, &error);
	if (sav == NULL)
		goto fail;

	if (sav->seq != 0) {
		/*
		 * RFC2367:
		 * If the SADB_GETSPI message is in response to a
		 * kernel-generated SADB_ACQUIRE, the sadb_msg_seq
		 * MUST be the same as the SADB_ACQUIRE message.
		 *
		 * XXXAE: However it doesn't definethe behaviour how to
		 * check this and what to do if it doesn't match.
		 * Also what we should do if it matches?
		 *
		 * We can compare saidx used in SADB_ACQUIRE with saidx
		 * used in SADB_GETSPI, but this probably can break
		 * existing software. For now just warn if it doesn't match.
		 *
		 * XXXAE: anyway it looks useless.
		 */
		key_acqdone(&saidx, sav->seq);
	}
	KEYDBG(KEY_STAMP,
	    printf("%s: SA(%p)\n", __func__, sav));
	KEYDBG(KEY_DATA, kdebug_secasv(sav));

    {
	struct mbuf *n, *nn;
	struct sadb_sa *m_sa;
	struct sadb_msg *newmsg;
	int off, len;

	/* create new sadb_msg to reply. */
	len = PFKEY_ALIGN8(sizeof(struct sadb_msg)) +
	    PFKEY_ALIGN8(sizeof(struct sadb_sa));

	MGETHDR(n, M_NOWAIT, MT_DATA);
	if (len > MHLEN) {
		if (!(MCLGET(n, M_NOWAIT))) {
			m_freem(n);
			n = NULL;
		}
	}
	if (!n) {
		error = ENOBUFS;
		goto fail;
	}

	n->m_len = len;
	n->m_next = NULL;
	off = 0;

	m_copydata(m, 0, sizeof(struct sadb_msg), mtod(n, caddr_t) + off);
	off += PFKEY_ALIGN8(sizeof(struct sadb_msg));

	m_sa = (struct sadb_sa *)(mtod(n, caddr_t) + off);
	m_sa->sadb_sa_len = PFKEY_UNIT64(sizeof(struct sadb_sa));
	m_sa->sadb_sa_exttype = SADB_EXT_SA;
	m_sa->sadb_sa_spi = spi; /* SPI is already in network byte order */
	off += PFKEY_ALIGN8(sizeof(struct sadb_sa));

	IPSEC_ASSERT(off == len,
		("length inconsistency (off %u len %u)", off, len));

	n->m_next = key_gather_mbuf(m, mhp, 0, 2, SADB_EXT_ADDRESS_SRC,
	    SADB_EXT_ADDRESS_DST);
	if (!n->m_next) {
		m_freem(n);
		error = ENOBUFS;
		goto fail;
	}

	if (n->m_len < sizeof(struct sadb_msg)) {
		n = m_pullup(n, sizeof(struct sadb_msg));
		if (n == NULL)
			return key_sendup_mbuf(so, m, KEY_SENDUP_ONE);
	}

	n->m_pkthdr.len = 0;
	for (nn = n; nn; nn = nn->m_next)
		n->m_pkthdr.len += nn->m_len;

	newmsg = mtod(n, struct sadb_msg *);
	newmsg->sadb_msg_seq = sav->seq;
	newmsg->sadb_msg_errno = 0;
	newmsg->sadb_msg_len = PFKEY_UNIT64(n->m_pkthdr.len);

	m_freem(m);
	return key_sendup_mbuf(so, n, KEY_SENDUP_ONE);
    }

fail:
	return (key_senderror(so, m, error));
}

/*
 * allocating new SPI
 * called by key_getspi().
 * OUT:
 *	0:	failure.
 *	others: success, SPI in network byte order.
 */
static uint32_t
key_do_getnewspi(struct sadb_spirange *spirange, struct secasindex *saidx)
{
	uint32_t min, max, newspi, t;
	int count = V_key_spi_trycnt;

	/* set spi range to allocate */
	if (spirange != NULL) {
		min = spirange->sadb_spirange_min;
		max = spirange->sadb_spirange_max;
	} else {
		min = V_key_spi_minval;
		max = V_key_spi_maxval;
	}
	/* IPCOMP needs 2-byte SPI */
	if (saidx->proto == IPPROTO_IPCOMP) {
		if (min >= 0x10000)
			min = 0xffff;
		if (max >= 0x10000)
			max = 0xffff;
		if (min > max) {
			t = min; min = max; max = t;
		}
	}

	if (min == max) {
		if (!key_checkspidup(htonl(min))) {
			ipseclog((LOG_DEBUG, "%s: SPI %u exists already.\n",
			    __func__, min));
			return 0;
		}

		count--; /* taking one cost. */
		newspi = min;
	} else {

		/* init SPI */
		newspi = 0;

		/* when requesting to allocate spi ranged */
		while (count--) {
			/* generate pseudo-random SPI value ranged. */
			newspi = min + (key_random() % (max - min + 1));
			if (!key_checkspidup(htonl(newspi)))
				break;
		}

		if (count == 0 || newspi == 0) {
			ipseclog((LOG_DEBUG,
			    "%s: failed to allocate SPI.\n", __func__));
			return 0;
		}
	}

	/* statistics */
	keystat.getspi_count =
	    (keystat.getspi_count + V_key_spi_trycnt - count) / 2;

	return (htonl(newspi));
}

/*
 * Find TCP-MD5 SA with corresponding secasindex.
 * If not found, return NULL and fill SPI with usable value if needed.
 */
static struct secasvar *
key_getsav_tcpmd5(struct secasindex *saidx, uint32_t *spi)
{
	SAHTREE_RLOCK_TRACKER;
	struct secashead *sah;
	struct secasvar *sav;

	IPSEC_ASSERT(saidx->proto == IPPROTO_TCP, ("wrong proto"));
	SAHTREE_RLOCK();
	LIST_FOREACH(sah, SAHADDRHASH_HASH(saidx), addrhash) {
		if (sah->saidx.proto != IPPROTO_TCP)
			continue;
		if (!key_sockaddrcmp(&saidx->dst.sa, &sah->saidx.dst.sa, 0) &&
		    !key_sockaddrcmp(&saidx->src.sa, &sah->saidx.src.sa, 0))
			break;
	}
	if (sah != NULL) {
		if (V_key_preferred_oldsa)
			sav = TAILQ_LAST(&sah->savtree_alive, secasvar_queue);
		else
			sav = TAILQ_FIRST(&sah->savtree_alive);
		if (sav != NULL) {
			SAV_ADDREF(sav);
			SAHTREE_RUNLOCK();
			return (sav);
		}
	}
	if (spi == NULL) {
		/* No SPI required */
		SAHTREE_RUNLOCK();
		return (NULL);
	}
	/* Check that SPI is unique */
	LIST_FOREACH(sav, SAVHASH_HASH(*spi), spihash) {
		if (sav->spi == *spi)
			break;
	}
	if (sav == NULL) {
		SAHTREE_RUNLOCK();
		/* SPI is already unique */
		return (NULL);
	}
	SAHTREE_RUNLOCK();
	/* XXX: not optimal */
	*spi = key_do_getnewspi(NULL, saidx);
	return (NULL);
}

static int
key_updateaddresses(struct socket *so, struct mbuf *m,
    const struct sadb_msghdr *mhp, struct secasvar *sav,
    struct secasindex *saidx)
{
	struct sockaddr *newaddr;
	struct secashead *sah;
	struct secasvar *newsav, *tmp;
	struct mbuf *n;
	int error, isnew;

	/* Check that we need to change SAH */
	if (!SADB_CHECKHDR(mhp, SADB_X_EXT_NEW_ADDRESS_SRC)) {
		newaddr = (struct sockaddr *)(
		    ((struct sadb_address *)
		    mhp->ext[SADB_X_EXT_NEW_ADDRESS_SRC]) + 1);
		bcopy(newaddr, &saidx->src, newaddr->sa_len);
		key_porttosaddr(&saidx->src.sa, 0);
	}
	if (!SADB_CHECKHDR(mhp, SADB_X_EXT_NEW_ADDRESS_DST)) {
		newaddr = (struct sockaddr *)(
		    ((struct sadb_address *)
		    mhp->ext[SADB_X_EXT_NEW_ADDRESS_DST]) + 1);
		bcopy(newaddr, &saidx->dst, newaddr->sa_len);
		key_porttosaddr(&saidx->dst.sa, 0);
	}
	if (!SADB_CHECKHDR(mhp, SADB_X_EXT_NEW_ADDRESS_SRC) ||
	    !SADB_CHECKHDR(mhp, SADB_X_EXT_NEW_ADDRESS_DST)) {
		error = key_checksockaddrs(&saidx->src.sa, &saidx->dst.sa);
		if (error != 0) {
			ipseclog((LOG_DEBUG, "%s: invalid new sockaddr.\n",
			    __func__));
			return (error);
		}

		sah = key_getsah(saidx);
		if (sah == NULL) {
			/* create a new SA index */
			sah = key_newsah(saidx);
			if (sah == NULL) {
				ipseclog((LOG_DEBUG,
				    "%s: No more memory.\n", __func__));
				return (ENOBUFS);
			}
			isnew = 2; /* SAH is new */
		} else
			isnew = 1; /* existing SAH is referenced */
	} else {
		/*
		 * src and dst addresses are still the same.
		 * Do we want to change NAT-T config?
		 */
		if (sav->sah->saidx.proto != IPPROTO_ESP ||
		    SADB_CHECKHDR(mhp, SADB_X_EXT_NAT_T_TYPE) ||
		    SADB_CHECKHDR(mhp, SADB_X_EXT_NAT_T_SPORT) ||
		    SADB_CHECKHDR(mhp, SADB_X_EXT_NAT_T_DPORT)) {
			ipseclog((LOG_DEBUG,
			    "%s: invalid message: missing required header.\n",
			    __func__));
			return (EINVAL);
		}
		/* We hold reference to SA, thus SAH will be referenced too. */
		sah = sav->sah;
		isnew = 0;
	}

	newsav = malloc(sizeof(struct secasvar), M_IPSEC_SA,
	    M_NOWAIT | M_ZERO);
	if (newsav == NULL) {
		ipseclog((LOG_DEBUG, "%s: No more memory.\n", __func__));
		error = ENOBUFS;
		goto fail;
	}

	/* Clone SA's content into newsav */
	SAV_INITREF(newsav);
	bcopy(sav, newsav, offsetof(struct secasvar, chain));
	/*
	 * We create new NAT-T config if it is needed.
	 * Old NAT-T config will be freed by key_cleansav() when
	 * last reference to SA will be released.
	 */
	newsav->natt = NULL;
	newsav->sah = sah;
	newsav->state = SADB_SASTATE_MATURE;
	error = key_setnatt(newsav, mhp);
	if (error != 0)
		goto fail;

	SAHTREE_WLOCK();
	/* Check that SA is still alive */
	if (sav->state == SADB_SASTATE_DEAD) {
		/* SA was unlinked */
		SAHTREE_WUNLOCK();
		error = ESRCH;
		goto fail;
	}

	/* Unlink SA from SAH and SPI hash */
	IPSEC_ASSERT((sav->flags & SADB_X_EXT_F_CLONED) == 0,
	    ("SA is already cloned"));
	IPSEC_ASSERT(sav->state == SADB_SASTATE_MATURE ||
	    sav->state == SADB_SASTATE_DYING,
	    ("Wrong SA state %u\n", sav->state));
	TAILQ_REMOVE(&sav->sah->savtree_alive, sav, chain);
	LIST_REMOVE(sav, spihash);
	sav->state = SADB_SASTATE_DEAD;

	/*
	 * Link new SA with SAH. Keep SAs ordered by
	 * create time (newer are first).
	 */
	TAILQ_FOREACH(tmp, &sah->savtree_alive, chain) {
		if (newsav->created > tmp->created) {
			TAILQ_INSERT_BEFORE(tmp, newsav, chain);
			break;
		}
	}
	if (tmp == NULL)
		TAILQ_INSERT_TAIL(&sah->savtree_alive, newsav, chain);

	/* Add new SA into SPI hash. */
	LIST_INSERT_HEAD(SAVHASH_HASH(newsav->spi), newsav, spihash);

	/* Add new SAH into SADB. */
	if (isnew == 2) {
		TAILQ_INSERT_HEAD(&V_sahtree, sah, chain);
		LIST_INSERT_HEAD(SAHADDRHASH_HASH(saidx), sah, addrhash);
		sah->state = SADB_SASTATE_MATURE;
		SAH_ADDREF(sah); /* newsav references new SAH */
	}
	/*
	 * isnew == 1 -> @sah was referenced by key_getsah().
	 * isnew == 0 -> we use the same @sah, that was used by @sav,
	 *	and we use its reference for @newsav.
	 */
	SECASVAR_LOCK(sav);
	/* XXX: replace cntr with pointer? */
	newsav->cntr = sav->cntr;
	sav->flags |= SADB_X_EXT_F_CLONED;
	SECASVAR_UNLOCK(sav);

	SAHTREE_WUNLOCK();

	KEYDBG(KEY_STAMP,
	    printf("%s: SA(%p) cloned into SA(%p)\n",
	    __func__, sav, newsav));
	KEYDBG(KEY_DATA, kdebug_secasv(newsav));

	key_freesav(&sav); /* release last reference */

	/* set msg buf from mhp */
	n = key_getmsgbuf_x1(m, mhp);
	if (n == NULL) {
		ipseclog((LOG_DEBUG, "%s: No more memory.\n", __func__));
		return (ENOBUFS);
	}
	m_freem(m);
	key_sendup_mbuf(so, n, KEY_SENDUP_ALL);
	return (0);
fail:
	if (isnew != 0)
		key_freesah(&sah);
	if (newsav != NULL) {
		if (newsav->natt != NULL)
			free(newsav->natt, M_IPSEC_MISC);
		free(newsav, M_IPSEC_SA);
	}
	return (error);
}

/*
 * SADB_UPDATE processing
 * receive
 *   <base, SA, (SA2), (lifetime(HSC),) address(SD), (address(P),)
 *       key(AE), (identity(SD),) (sensitivity)>
 * from the ikmpd, and update a secasvar entry whose status is SADB_SASTATE_LARVAL.
 * and send
 *   <base, SA, (SA2), (lifetime(HSC),) address(SD), (address(P),)
 *       (identity(SD),) (sensitivity)>
 * to the ikmpd.
 *
 * m will always be freed.
 */
static int
key_update(struct socket *so, struct mbuf *m, const struct sadb_msghdr *mhp)
{
	struct secasindex saidx;
	struct sadb_address *src0, *dst0;
	struct sadb_sa *sa0;
	struct secasvar *sav;
	uint32_t reqid;
	int error;
	uint8_t mode, proto;

	IPSEC_ASSERT(so != NULL, ("null socket"));
	IPSEC_ASSERT(m != NULL, ("null mbuf"));
	IPSEC_ASSERT(mhp != NULL, ("null msghdr"));
	IPSEC_ASSERT(mhp->msg != NULL, ("null msg"));

	/* map satype to proto */
	if ((proto = key_satype2proto(mhp->msg->sadb_msg_satype)) == 0) {
		ipseclog((LOG_DEBUG, "%s: invalid satype is passed.\n",
		    __func__));
		return key_senderror(so, m, EINVAL);
	}

	if (SADB_CHECKHDR(mhp, SADB_EXT_SA) ||
	    SADB_CHECKHDR(mhp, SADB_EXT_ADDRESS_SRC) ||
	    SADB_CHECKHDR(mhp, SADB_EXT_ADDRESS_DST) ||
	    (SADB_CHECKHDR(mhp, SADB_EXT_LIFETIME_HARD) &&
		!SADB_CHECKHDR(mhp, SADB_EXT_LIFETIME_SOFT)) ||
	    (SADB_CHECKHDR(mhp, SADB_EXT_LIFETIME_SOFT) &&
		!SADB_CHECKHDR(mhp, SADB_EXT_LIFETIME_HARD))) {
		ipseclog((LOG_DEBUG,
		    "%s: invalid message: missing required header.\n",
		    __func__));
		return key_senderror(so, m, EINVAL);
	}
	if (SADB_CHECKLEN(mhp, SADB_EXT_SA) ||
	    SADB_CHECKLEN(mhp, SADB_EXT_ADDRESS_SRC) ||
	    SADB_CHECKLEN(mhp, SADB_EXT_ADDRESS_DST)) {
		ipseclog((LOG_DEBUG,
		    "%s: invalid message: wrong header size.\n", __func__));
		return key_senderror(so, m, EINVAL);
	}
	if (SADB_CHECKHDR(mhp, SADB_X_EXT_SA2)) {
		mode = IPSEC_MODE_ANY;
		reqid = 0;
	} else {
		if (SADB_CHECKLEN(mhp, SADB_X_EXT_SA2)) {
			ipseclog((LOG_DEBUG,
			    "%s: invalid message: wrong header size.\n",
			    __func__));
			return key_senderror(so, m, EINVAL);
		}
		mode = ((struct sadb_x_sa2 *)
		    mhp->ext[SADB_X_EXT_SA2])->sadb_x_sa2_mode;
		reqid = ((struct sadb_x_sa2 *)
		    mhp->ext[SADB_X_EXT_SA2])->sadb_x_sa2_reqid;
	}

	sa0 = (struct sadb_sa *)mhp->ext[SADB_EXT_SA];
	src0 = (struct sadb_address *)(mhp->ext[SADB_EXT_ADDRESS_SRC]);
	dst0 = (struct sadb_address *)(mhp->ext[SADB_EXT_ADDRESS_DST]);

	/*
	 * Only SADB_SASTATE_MATURE SAs may be submitted in an
	 * SADB_UPDATE message.
	 */
	if (sa0->sadb_sa_state != SADB_SASTATE_MATURE) {
		ipseclog((LOG_DEBUG, "%s: invalid state.\n", __func__));
#ifdef PFKEY_STRICT_CHECKS
		return key_senderror(so, m, EINVAL);
#endif
	}
	error = key_checksockaddrs((struct sockaddr *)(src0 + 1),
	    (struct sockaddr *)(dst0 + 1));
	if (error != 0) {
		ipseclog((LOG_DEBUG, "%s: invalid sockaddr.\n", __func__));
		return key_senderror(so, m, error);
	}
	KEY_SETSECASIDX(proto, mode, reqid, src0 + 1, dst0 + 1, &saidx);
	sav = key_getsavbyspi(sa0->sadb_sa_spi);
	if (sav == NULL) {
		ipseclog((LOG_DEBUG, "%s: no SA found for SPI %u\n",
		    __func__, ntohl(sa0->sadb_sa_spi)));
		return key_senderror(so, m, EINVAL);
	}
	/*
	 * Check that SADB_UPDATE issued by the same process that did
	 * SADB_GETSPI or SADB_ADD.
	 */
	if (sav->pid != mhp->msg->sadb_msg_pid) {
		ipseclog((LOG_DEBUG,
		    "%s: pid mismatched (SPI %u, pid %u vs. %u)\n", __func__,
		    ntohl(sav->spi), sav->pid, mhp->msg->sadb_msg_pid));
		key_freesav(&sav);
		return key_senderror(so, m, EINVAL);
	}
	/* saidx should match with SA. */
	if (key_cmpsaidx(&sav->sah->saidx, &saidx, CMP_MODE_REQID) == 0) {
		ipseclog((LOG_DEBUG, "%s: saidx mismatched for SPI %u",
		    __func__, ntohl(sav->spi)));
		key_freesav(&sav);
		return key_senderror(so, m, ESRCH);
	}

	if (sav->state == SADB_SASTATE_LARVAL) {
		if ((mhp->msg->sadb_msg_satype == SADB_SATYPE_ESP &&
		    SADB_CHECKHDR(mhp, SADB_EXT_KEY_ENCRYPT)) ||
		    (mhp->msg->sadb_msg_satype == SADB_SATYPE_AH &&
		    SADB_CHECKHDR(mhp, SADB_EXT_KEY_AUTH))) {
			ipseclog((LOG_DEBUG,
			    "%s: invalid message: missing required header.\n",
			    __func__));
			key_freesav(&sav);
			return key_senderror(so, m, EINVAL);
		}
		/*
		 * We can set any values except src, dst and SPI.
		 */
		error = key_setsaval(sav, mhp);
		if (error != 0) {
			key_freesav(&sav);
			return (key_senderror(so, m, error));
		}
		/* Change SA state to MATURE */
		SAHTREE_WLOCK();
		if (sav->state != SADB_SASTATE_LARVAL) {
			/* SA was deleted or another thread made it MATURE. */
			SAHTREE_WUNLOCK();
			key_freesav(&sav);
			return (key_senderror(so, m, ESRCH));
		}
		/*
		 * NOTE: we keep SAs in savtree_alive ordered by created
		 * time. When SA's state changed from LARVAL to MATURE,
		 * we update its created time in key_setsaval() and move
		 * it into head of savtree_alive.
		 */
		TAILQ_REMOVE(&sav->sah->savtree_larval, sav, chain);
		TAILQ_INSERT_HEAD(&sav->sah->savtree_alive, sav, chain);
		sav->state = SADB_SASTATE_MATURE;
		SAHTREE_WUNLOCK();
	} else {
		/*
		 * For DYING and MATURE SA we can change only state
		 * and lifetimes. Report EINVAL if something else attempted
		 * to change.
		 */
		if (!SADB_CHECKHDR(mhp, SADB_EXT_KEY_ENCRYPT) ||
		    !SADB_CHECKHDR(mhp, SADB_EXT_KEY_AUTH)) {
			key_freesav(&sav);
			return (key_senderror(so, m, EINVAL));
		}
		error = key_updatelifetimes(sav, mhp);
		if (error != 0) {
			key_freesav(&sav);
			return (key_senderror(so, m, error));
		}
		/*
		 * This is FreeBSD extension to RFC2367.
		 * IKEd can specify SADB_X_EXT_NEW_ADDRESS_SRC and/or
		 * SADB_X_EXT_NEW_ADDRESS_DST when it wants to change
		 * SA addresses (for example to implement MOBIKE protocol
		 * as described in RFC4555). Also we allow to change
		 * NAT-T config.
		 */
		if (!SADB_CHECKHDR(mhp, SADB_X_EXT_NEW_ADDRESS_SRC) ||
		    !SADB_CHECKHDR(mhp, SADB_X_EXT_NEW_ADDRESS_DST) ||
		    !SADB_CHECKHDR(mhp, SADB_X_EXT_NAT_T_TYPE) ||
		    sav->natt != NULL) {
			error = key_updateaddresses(so, m, mhp, sav, &saidx);
			key_freesav(&sav);
			if (error != 0)
				return (key_senderror(so, m, error));
			return (0);
		}
		/* Check that SA is still alive */
		SAHTREE_WLOCK();
		if (sav->state == SADB_SASTATE_DEAD) {
			/* SA was unlinked */
			SAHTREE_WUNLOCK();
			key_freesav(&sav);
			return (key_senderror(so, m, ESRCH));
		}
		/*
		 * NOTE: there is possible state moving from DYING to MATURE,
		 * but this doesn't change created time, so we won't reorder
		 * this SA.
		 */
		sav->state = SADB_SASTATE_MATURE;
		SAHTREE_WUNLOCK();
	}
	KEYDBG(KEY_STAMP,
	    printf("%s: SA(%p)\n", __func__, sav));
	KEYDBG(KEY_DATA, kdebug_secasv(sav));
	key_freesav(&sav);

    {
	struct mbuf *n;

	/* set msg buf from mhp */
	n = key_getmsgbuf_x1(m, mhp);
	if (n == NULL) {
		ipseclog((LOG_DEBUG, "%s: No more memory.\n", __func__));
		return key_senderror(so, m, ENOBUFS);
	}

	m_freem(m);
	return key_sendup_mbuf(so, n, KEY_SENDUP_ALL);
    }
}

/*
 * SADB_ADD processing
 * add an entry to SA database, when received
 *   <base, SA, (SA2), (lifetime(HSC),) address(SD), (address(P),)
 *       key(AE), (identity(SD),) (sensitivity)>
 * from the ikmpd,
 * and send
 *   <base, SA, (SA2), (lifetime(HSC),) address(SD), (address(P),)
 *       (identity(SD),) (sensitivity)>
 * to the ikmpd.
 *
 * IGNORE identity and sensitivity messages.
 *
 * m will always be freed.
 */
static int
key_add(struct socket *so, struct mbuf *m, const struct sadb_msghdr *mhp)
{
	struct secasindex saidx;
	struct sadb_address *src0, *dst0;
	struct sadb_sa *sa0;
	struct secasvar *sav;
	uint32_t reqid, spi;
	uint8_t mode, proto;
	int error;

	IPSEC_ASSERT(so != NULL, ("null socket"));
	IPSEC_ASSERT(m != NULL, ("null mbuf"));
	IPSEC_ASSERT(mhp != NULL, ("null msghdr"));
	IPSEC_ASSERT(mhp->msg != NULL, ("null msg"));

	/* map satype to proto */
	if ((proto = key_satype2proto(mhp->msg->sadb_msg_satype)) == 0) {
		ipseclog((LOG_DEBUG, "%s: invalid satype is passed.\n",
		    __func__));
		return key_senderror(so, m, EINVAL);
	}

	if (SADB_CHECKHDR(mhp, SADB_EXT_SA) ||
	    SADB_CHECKHDR(mhp, SADB_EXT_ADDRESS_SRC) ||
	    SADB_CHECKHDR(mhp, SADB_EXT_ADDRESS_DST) ||
	    (mhp->msg->sadb_msg_satype == SADB_SATYPE_ESP && (
		SADB_CHECKHDR(mhp, SADB_EXT_KEY_ENCRYPT) ||
		SADB_CHECKLEN(mhp, SADB_EXT_KEY_ENCRYPT))) ||
	    (mhp->msg->sadb_msg_satype == SADB_SATYPE_AH && (
		SADB_CHECKHDR(mhp, SADB_EXT_KEY_AUTH) ||
		SADB_CHECKLEN(mhp, SADB_EXT_KEY_AUTH))) ||
	    (SADB_CHECKHDR(mhp, SADB_EXT_LIFETIME_HARD) &&
		!SADB_CHECKHDR(mhp, SADB_EXT_LIFETIME_SOFT)) ||
	    (SADB_CHECKHDR(mhp, SADB_EXT_LIFETIME_SOFT) &&
		!SADB_CHECKHDR(mhp, SADB_EXT_LIFETIME_HARD))) {
		ipseclog((LOG_DEBUG,
		    "%s: invalid message: missing required header.\n",
		    __func__));
		return key_senderror(so, m, EINVAL);
	}
	if (SADB_CHECKLEN(mhp, SADB_EXT_SA) ||
	    SADB_CHECKLEN(mhp, SADB_EXT_ADDRESS_SRC) ||
	    SADB_CHECKLEN(mhp, SADB_EXT_ADDRESS_DST)) {
		ipseclog((LOG_DEBUG,
		    "%s: invalid message: wrong header size.\n", __func__));
		return key_senderror(so, m, EINVAL);
	}
	if (SADB_CHECKHDR(mhp, SADB_X_EXT_SA2)) {
		mode = IPSEC_MODE_ANY;
		reqid = 0;
	} else {
		if (SADB_CHECKLEN(mhp, SADB_X_EXT_SA2)) {
			ipseclog((LOG_DEBUG,
			    "%s: invalid message: wrong header size.\n",
			    __func__));
			return key_senderror(so, m, EINVAL);
		}
		mode = ((struct sadb_x_sa2 *)
		    mhp->ext[SADB_X_EXT_SA2])->sadb_x_sa2_mode;
		reqid = ((struct sadb_x_sa2 *)
		    mhp->ext[SADB_X_EXT_SA2])->sadb_x_sa2_reqid;
	}

	sa0 = (struct sadb_sa *)mhp->ext[SADB_EXT_SA];
	src0 = (struct sadb_address *)mhp->ext[SADB_EXT_ADDRESS_SRC];
	dst0 = (struct sadb_address *)mhp->ext[SADB_EXT_ADDRESS_DST];

	/*
	 * Only SADB_SASTATE_MATURE SAs may be submitted in an
	 * SADB_ADD message.
	 */
	if (sa0->sadb_sa_state != SADB_SASTATE_MATURE) {
		ipseclog((LOG_DEBUG, "%s: invalid state.\n", __func__));
#ifdef PFKEY_STRICT_CHECKS
		return key_senderror(so, m, EINVAL);
#endif
	}
	error = key_checksockaddrs((struct sockaddr *)(src0 + 1),
	    (struct sockaddr *)(dst0 + 1));
	if (error != 0) {
		ipseclog((LOG_DEBUG, "%s: invalid sockaddr.\n", __func__));
		return key_senderror(so, m, error);
	}
	KEY_SETSECASIDX(proto, mode, reqid, src0 + 1, dst0 + 1, &saidx);
	spi = sa0->sadb_sa_spi;
	/*
	 * For TCP-MD5 SAs we don't use SPI. Check the uniqueness using
	 * secasindex.
	 * XXXAE: IPComp seems also doesn't use SPI.
	 */
	if (proto == IPPROTO_TCP) {
		sav = key_getsav_tcpmd5(&saidx, &spi);
		if (sav == NULL && spi == 0) {
			/* Failed to allocate SPI */
			ipseclog((LOG_DEBUG, "%s: SA already exists.\n",
			    __func__));
			return key_senderror(so, m, EEXIST);
		}
		/* XXX: SPI that we report back can have another value */
	} else {
		/* We can create new SA only if SPI is different. */
		sav = key_getsavbyspi(spi);
	}
	if (sav != NULL) {
		key_freesav(&sav);
		ipseclog((LOG_DEBUG, "%s: SA already exists.\n", __func__));
		return key_senderror(so, m, EEXIST);
	}

	sav = key_newsav(mhp, &saidx, spi, &error);
	if (sav == NULL)
		return key_senderror(so, m, error);
	KEYDBG(KEY_STAMP,
	    printf("%s: return SA(%p)\n", __func__, sav));
	KEYDBG(KEY_DATA, kdebug_secasv(sav));
	/*
	 * If SADB_ADD was in response to SADB_ACQUIRE, we need to schedule
	 * ACQ for deletion.
	 */
	if (sav->seq != 0)
		key_acqdone(&saidx, sav->seq);

    {
	/*
	 * Don't call key_freesav() on error here, as we would like to
	 * keep the SA in the database.
	 */
	struct mbuf *n;

	/* set msg buf from mhp */
	n = key_getmsgbuf_x1(m, mhp);
	if (n == NULL) {
		ipseclog((LOG_DEBUG, "%s: No more memory.\n", __func__));
		return key_senderror(so, m, ENOBUFS);
	}

	m_freem(m);
	return key_sendup_mbuf(so, n, KEY_SENDUP_ALL);
    }
}

/*
 * NAT-T support.
 * IKEd may request the use ESP in UDP encapsulation when it detects the
 * presence of NAT. It uses NAT-T extension headers for such SAs to specify
 * parameters needed for encapsulation and decapsulation. These PF_KEY
 * extension headers are not standardized, so this comment addresses our
 * implementation.
 * SADB_X_EXT_NAT_T_TYPE specifies type of encapsulation, we support only
 * UDP_ENCAP_ESPINUDP as described in RFC3948.
 * SADB_X_EXT_NAT_T_SPORT/DPORT specifies source and destination ports for
 * UDP header. We use these ports in UDP encapsulation procedure, also we
 * can check them in UDP decapsulation procedure.
 * SADB_X_EXT_NAT_T_OA[IR] specifies original address of initiator or
 * responder. These addresses can be used for transport mode to adjust
 * checksum after decapsulation and decryption. Since original IP addresses
 * used by peer usually different (we detected presence of NAT), TCP/UDP
 * pseudo header checksum and IP header checksum was calculated using original
 * addresses. After decapsulation and decryption we need to adjust checksum
 * to have correct datagram.
 *
 * We expect presence of NAT-T extension headers only in SADB_ADD and
 * SADB_UPDATE messages. We report NAT-T extension headers in replies
 * to SADB_ADD, SADB_UPDATE, SADB_GET, and SADB_DUMP messages.
 */
static int
key_setnatt(struct secasvar *sav, const struct sadb_msghdr *mhp)
{
	struct sadb_x_nat_t_port *port;
	struct sadb_x_nat_t_type *type;
	struct sadb_address *oai, *oar;
	struct sockaddr *sa;
	uint32_t addr;
	uint16_t cksum;

	IPSEC_ASSERT(sav->natt == NULL, ("natt is already initialized"));
	/*
	 * Ignore NAT-T headers if sproto isn't ESP.
	 */
	if (sav->sah->saidx.proto != IPPROTO_ESP)
		return (0);

	if (!SADB_CHECKHDR(mhp, SADB_X_EXT_NAT_T_TYPE) &&
	    !SADB_CHECKHDR(mhp, SADB_X_EXT_NAT_T_SPORT) &&
	    !SADB_CHECKHDR(mhp, SADB_X_EXT_NAT_T_DPORT)) {
		if (SADB_CHECKLEN(mhp, SADB_X_EXT_NAT_T_TYPE) ||
		    SADB_CHECKLEN(mhp, SADB_X_EXT_NAT_T_SPORT) ||
		    SADB_CHECKLEN(mhp, SADB_X_EXT_NAT_T_DPORT)) {
			ipseclog((LOG_DEBUG,
			    "%s: invalid message: wrong header size.\n",
			    __func__));
			return (EINVAL);
		}
	} else
		return (0);

	type = (struct sadb_x_nat_t_type *)mhp->ext[SADB_X_EXT_NAT_T_TYPE];
	if (type->sadb_x_nat_t_type_type != UDP_ENCAP_ESPINUDP) {
		ipseclog((LOG_DEBUG, "%s: unsupported NAT-T type %u.\n",
		    __func__, type->sadb_x_nat_t_type_type));
		return (EINVAL);
	}
	/*
	 * Allocate storage for NAT-T config.
	 * On error it will be released by key_cleansav().
	 */
	sav->natt = malloc(sizeof(struct secnatt), M_IPSEC_MISC,
	    M_NOWAIT | M_ZERO);
	if (sav->natt == NULL) {
		PFKEYSTAT_INC(in_nomem);
		ipseclog((LOG_DEBUG, "%s: No more memory.\n", __func__));
		return (ENOBUFS);
	}
	port = (struct sadb_x_nat_t_port *)mhp->ext[SADB_X_EXT_NAT_T_SPORT];
	if (port->sadb_x_nat_t_port_port == 0) {
		ipseclog((LOG_DEBUG, "%s: invalid NAT-T sport specified.\n",
		    __func__));
		return (EINVAL);
	}
	sav->natt->sport = port->sadb_x_nat_t_port_port;
	port = (struct sadb_x_nat_t_port *)mhp->ext[SADB_X_EXT_NAT_T_DPORT];
	if (port->sadb_x_nat_t_port_port == 0) {
		ipseclog((LOG_DEBUG, "%s: invalid NAT-T dport specified.\n",
		    __func__));
		return (EINVAL);
	}
	sav->natt->dport = port->sadb_x_nat_t_port_port;

	/*
	 * SADB_X_EXT_NAT_T_OAI and SADB_X_EXT_NAT_T_OAR are optional
	 * and needed only for transport mode IPsec.
	 * Usually NAT translates only one address, but it is possible,
	 * that both addresses could be translated.
	 * NOTE: Value of SADB_X_EXT_NAT_T_OAI is equal to SADB_X_EXT_NAT_T_OA.
	 */
	if (!SADB_CHECKHDR(mhp, SADB_X_EXT_NAT_T_OAI)) {
		if (SADB_CHECKLEN(mhp, SADB_X_EXT_NAT_T_OAI)) {
			ipseclog((LOG_DEBUG,
			    "%s: invalid message: wrong header size.\n",
			    __func__));
			return (EINVAL);
		}
		oai = (struct sadb_address *)mhp->ext[SADB_X_EXT_NAT_T_OAI];
	} else
		oai = NULL;
	if (!SADB_CHECKHDR(mhp, SADB_X_EXT_NAT_T_OAR)) {
		if (SADB_CHECKLEN(mhp, SADB_X_EXT_NAT_T_OAR)) {
			ipseclog((LOG_DEBUG,
			    "%s: invalid message: wrong header size.\n",
			    __func__));
			return (EINVAL);
		}
		oar = (struct sadb_address *)mhp->ext[SADB_X_EXT_NAT_T_OAR];
	} else
		oar = NULL;

	/* Initialize addresses only for transport mode */
	if (sav->sah->saidx.mode != IPSEC_MODE_TUNNEL) {
		cksum = 0;
		if (oai != NULL) {
			/* Currently we support only AF_INET */
			sa = (struct sockaddr *)(oai + 1);
			if (sa->sa_family != AF_INET ||
			    sa->sa_len != sizeof(struct sockaddr_in)) {
				ipseclog((LOG_DEBUG,
				    "%s: wrong NAT-OAi header.\n",
				    __func__));
				return (EINVAL);
			}
			/* Ignore address if it the same */
			if (((struct sockaddr_in *)sa)->sin_addr.s_addr !=
			    sav->sah->saidx.src.sin.sin_addr.s_addr) {
				bcopy(sa, &sav->natt->oai.sa, sa->sa_len);
				sav->natt->flags |= IPSEC_NATT_F_OAI;
				/* Calculate checksum delta */
				addr = sav->sah->saidx.src.sin.sin_addr.s_addr;
				cksum = in_addword(cksum, ~addr >> 16);
				cksum = in_addword(cksum, ~addr & 0xffff);
				addr = sav->natt->oai.sin.sin_addr.s_addr;
				cksum = in_addword(cksum, addr >> 16);
				cksum = in_addword(cksum, addr & 0xffff);
			}
		}
		if (oar != NULL) {
			/* Currently we support only AF_INET */
			sa = (struct sockaddr *)(oar + 1);
			if (sa->sa_family != AF_INET ||
			    sa->sa_len != sizeof(struct sockaddr_in)) {
				ipseclog((LOG_DEBUG,
				    "%s: wrong NAT-OAr header.\n",
				    __func__));
				return (EINVAL);
			}
			/* Ignore address if it the same */
			if (((struct sockaddr_in *)sa)->sin_addr.s_addr !=
			    sav->sah->saidx.dst.sin.sin_addr.s_addr) {
				bcopy(sa, &sav->natt->oar.sa, sa->sa_len);
				sav->natt->flags |= IPSEC_NATT_F_OAR;
				/* Calculate checksum delta */
				addr = sav->sah->saidx.dst.sin.sin_addr.s_addr;
				cksum = in_addword(cksum, ~addr >> 16);
				cksum = in_addword(cksum, ~addr & 0xffff);
				addr = sav->natt->oar.sin.sin_addr.s_addr;
				cksum = in_addword(cksum, addr >> 16);
				cksum = in_addword(cksum, addr & 0xffff);
			}
		}
		sav->natt->cksum = cksum;
	}
	return (0);
}

static int
key_setident(struct secashead *sah, const struct sadb_msghdr *mhp)
{
	const struct sadb_ident *idsrc, *iddst;

	IPSEC_ASSERT(sah != NULL, ("null secashead"));
	IPSEC_ASSERT(mhp != NULL, ("null msghdr"));
	IPSEC_ASSERT(mhp->msg != NULL, ("null msg"));

	/* don't make buffer if not there */
	if (SADB_CHECKHDR(mhp, SADB_EXT_IDENTITY_SRC) &&
	    SADB_CHECKHDR(mhp, SADB_EXT_IDENTITY_DST)) {
		sah->idents = NULL;
		sah->identd = NULL;
		return (0);
	}

	if (SADB_CHECKHDR(mhp, SADB_EXT_IDENTITY_SRC) ||
	    SADB_CHECKHDR(mhp, SADB_EXT_IDENTITY_DST)) {
		ipseclog((LOG_DEBUG, "%s: invalid identity.\n", __func__));
		return (EINVAL);
	}

	idsrc = (const struct sadb_ident *)mhp->ext[SADB_EXT_IDENTITY_SRC];
	iddst = (const struct sadb_ident *)mhp->ext[SADB_EXT_IDENTITY_DST];

	/* validity check */
	if (idsrc->sadb_ident_type != iddst->sadb_ident_type) {
		ipseclog((LOG_DEBUG, "%s: ident type mismatch.\n", __func__));
		return EINVAL;
	}

	switch (idsrc->sadb_ident_type) {
	case SADB_IDENTTYPE_PREFIX:
	case SADB_IDENTTYPE_FQDN:
	case SADB_IDENTTYPE_USERFQDN:
	default:
		/* XXX do nothing */
		sah->idents = NULL;
		sah->identd = NULL;
	 	return 0;
	}

	/* make structure */
	sah->idents = malloc(sizeof(struct secident), M_IPSEC_MISC, M_NOWAIT);
	if (sah->idents == NULL) {
		ipseclog((LOG_DEBUG, "%s: No more memory.\n", __func__));
		return ENOBUFS;
	}
	sah->identd = malloc(sizeof(struct secident), M_IPSEC_MISC, M_NOWAIT);
	if (sah->identd == NULL) {
		free(sah->idents, M_IPSEC_MISC);
		sah->idents = NULL;
		ipseclog((LOG_DEBUG, "%s: No more memory.\n", __func__));
		return ENOBUFS;
	}
	sah->idents->type = idsrc->sadb_ident_type;
	sah->idents->id = idsrc->sadb_ident_id;

	sah->identd->type = iddst->sadb_ident_type;
	sah->identd->id = iddst->sadb_ident_id;

	return 0;
}

/*
 * m will not be freed on return.
 * it is caller's responsibility to free the result.
 *
 * Called from SADB_ADD and SADB_UPDATE. Reply will contain headers
 * from the request in defined order.
 */
static struct mbuf *
key_getmsgbuf_x1(struct mbuf *m, const struct sadb_msghdr *mhp)
{
	struct mbuf *n;

	IPSEC_ASSERT(m != NULL, ("null mbuf"));
	IPSEC_ASSERT(mhp != NULL, ("null msghdr"));
	IPSEC_ASSERT(mhp->msg != NULL, ("null msg"));

	/* create new sadb_msg to reply. */
	n = key_gather_mbuf(m, mhp, 1, 16, SADB_EXT_RESERVED,
	    SADB_EXT_SA, SADB_X_EXT_SA2,
	    SADB_EXT_ADDRESS_SRC, SADB_EXT_ADDRESS_DST,
	    SADB_EXT_LIFETIME_HARD, SADB_EXT_LIFETIME_SOFT,
	    SADB_EXT_IDENTITY_SRC, SADB_EXT_IDENTITY_DST,
	    SADB_X_EXT_NAT_T_TYPE, SADB_X_EXT_NAT_T_SPORT,
	    SADB_X_EXT_NAT_T_DPORT, SADB_X_EXT_NAT_T_OAI,
	    SADB_X_EXT_NAT_T_OAR, SADB_X_EXT_NEW_ADDRESS_SRC,
	    SADB_X_EXT_NEW_ADDRESS_DST);
	if (!n)
		return NULL;

	if (n->m_len < sizeof(struct sadb_msg)) {
		n = m_pullup(n, sizeof(struct sadb_msg));
		if (n == NULL)
			return NULL;
	}
	mtod(n, struct sadb_msg *)->sadb_msg_errno = 0;
	mtod(n, struct sadb_msg *)->sadb_msg_len =
	    PFKEY_UNIT64(n->m_pkthdr.len);

	return n;
}

/*
 * SADB_DELETE processing
 * receive
 *   <base, SA(*), address(SD)>
 * from the ikmpd, and set SADB_SASTATE_DEAD,
 * and send,
 *   <base, SA(*), address(SD)>
 * to the ikmpd.
 *
 * m will always be freed.
 */
static int
key_delete(struct socket *so, struct mbuf *m, const struct sadb_msghdr *mhp)
{
	struct secasindex saidx;
	struct sadb_address *src0, *dst0;
	struct secasvar *sav;
	struct sadb_sa *sa0;
	uint8_t proto;

	IPSEC_ASSERT(so != NULL, ("null socket"));
	IPSEC_ASSERT(m != NULL, ("null mbuf"));
	IPSEC_ASSERT(mhp != NULL, ("null msghdr"));
	IPSEC_ASSERT(mhp->msg != NULL, ("null msg"));

	/* map satype to proto */
	if ((proto = key_satype2proto(mhp->msg->sadb_msg_satype)) == 0) {
		ipseclog((LOG_DEBUG, "%s: invalid satype is passed.\n",
		    __func__));
		return key_senderror(so, m, EINVAL);
	}

	if (SADB_CHECKHDR(mhp, SADB_EXT_ADDRESS_SRC) ||
	    SADB_CHECKHDR(mhp, SADB_EXT_ADDRESS_DST) ||
	    SADB_CHECKLEN(mhp, SADB_EXT_ADDRESS_SRC) ||
	    SADB_CHECKLEN(mhp, SADB_EXT_ADDRESS_DST)) {
		ipseclog((LOG_DEBUG, "%s: invalid message is passed.\n",
		    __func__));
		return key_senderror(so, m, EINVAL);
	}

	src0 = (struct sadb_address *)(mhp->ext[SADB_EXT_ADDRESS_SRC]);
	dst0 = (struct sadb_address *)(mhp->ext[SADB_EXT_ADDRESS_DST]);

	if (key_checksockaddrs((struct sockaddr *)(src0 + 1),
	    (struct sockaddr *)(dst0 + 1)) != 0) {
		ipseclog((LOG_DEBUG, "%s: invalid sockaddr.\n", __func__));
		return (key_senderror(so, m, EINVAL));
	}
	KEY_SETSECASIDX(proto, IPSEC_MODE_ANY, 0, src0 + 1, dst0 + 1, &saidx);
	if (SADB_CHECKHDR(mhp, SADB_EXT_SA)) {
		/*
		 * Caller wants us to delete all non-LARVAL SAs
		 * that match the src/dst.  This is used during
		 * IKE INITIAL-CONTACT.
		 * XXXAE: this looks like some extension to RFC2367.
		 */
		ipseclog((LOG_DEBUG, "%s: doing delete all.\n", __func__));
		return (key_delete_all(so, m, mhp, &saidx));
	}
	if (SADB_CHECKLEN(mhp, SADB_EXT_SA)) {
		ipseclog((LOG_DEBUG,
		    "%s: invalid message: wrong header size.\n", __func__));
		return (key_senderror(so, m, EINVAL));
	}
	sa0 = (struct sadb_sa *)mhp->ext[SADB_EXT_SA];
	if (proto == IPPROTO_TCP)
		sav = key_getsav_tcpmd5(&saidx, NULL);
	else
		sav = key_getsavbyspi(sa0->sadb_sa_spi);
	if (sav == NULL) {
		ipseclog((LOG_DEBUG, "%s: no SA found for SPI %u.\n",
		    __func__, ntohl(sa0->sadb_sa_spi)));
		return (key_senderror(so, m, ESRCH));
	}
	if (key_cmpsaidx(&sav->sah->saidx, &saidx, CMP_HEAD) == 0) {
		ipseclog((LOG_DEBUG, "%s: saidx mismatched for SPI %u.\n",
		    __func__, ntohl(sav->spi)));
		key_freesav(&sav);
		return (key_senderror(so, m, ESRCH));
	}
	KEYDBG(KEY_STAMP,
	    printf("%s: SA(%p)\n", __func__, sav));
	KEYDBG(KEY_DATA, kdebug_secasv(sav));
	key_unlinksav(sav);
	key_freesav(&sav);

    {
	struct mbuf *n;
	struct sadb_msg *newmsg;

	/* create new sadb_msg to reply. */
	n = key_gather_mbuf(m, mhp, 1, 4, SADB_EXT_RESERVED,
	    SADB_EXT_SA, SADB_EXT_ADDRESS_SRC, SADB_EXT_ADDRESS_DST);
	if (!n)
		return key_senderror(so, m, ENOBUFS);

	if (n->m_len < sizeof(struct sadb_msg)) {
		n = m_pullup(n, sizeof(struct sadb_msg));
		if (n == NULL)
			return key_senderror(so, m, ENOBUFS);
	}
	newmsg = mtod(n, struct sadb_msg *);
	newmsg->sadb_msg_errno = 0;
	newmsg->sadb_msg_len = PFKEY_UNIT64(n->m_pkthdr.len);

	m_freem(m);
	return key_sendup_mbuf(so, n, KEY_SENDUP_ALL);
    }
}

/*
 * delete all SAs for src/dst.  Called from key_delete().
 */
static int
key_delete_all(struct socket *so, struct mbuf *m,
    const struct sadb_msghdr *mhp, struct secasindex *saidx)
{
	struct secasvar_queue drainq;
	struct secashead *sah;
	struct secasvar *sav, *nextsav;

	TAILQ_INIT(&drainq);
	SAHTREE_WLOCK();
	LIST_FOREACH(sah, SAHADDRHASH_HASH(saidx), addrhash) {
		if (key_cmpsaidx(&sah->saidx, saidx, CMP_HEAD) == 0)
			continue;
		/* Move all ALIVE SAs into drainq */
		TAILQ_CONCAT(&drainq, &sah->savtree_alive, chain);
	}
	/* Unlink all queued SAs from SPI hash */
	TAILQ_FOREACH(sav, &drainq, chain) {
		sav->state = SADB_SASTATE_DEAD;
		LIST_REMOVE(sav, spihash);
	}
	SAHTREE_WUNLOCK();
	/* Now we can release reference for all SAs in drainq */
	sav = TAILQ_FIRST(&drainq);
	while (sav != NULL) {
		KEYDBG(KEY_STAMP,
		    printf("%s: SA(%p)\n", __func__, sav));
		KEYDBG(KEY_DATA, kdebug_secasv(sav));
		nextsav = TAILQ_NEXT(sav, chain);
		key_freesah(&sav->sah); /* release reference from SAV */
		key_freesav(&sav); /* release last reference */
		sav = nextsav;
	}

    {
	struct mbuf *n;
	struct sadb_msg *newmsg;

	/* create new sadb_msg to reply. */
	n = key_gather_mbuf(m, mhp, 1, 3, SADB_EXT_RESERVED,
	    SADB_EXT_ADDRESS_SRC, SADB_EXT_ADDRESS_DST);
	if (!n)
		return key_senderror(so, m, ENOBUFS);

	if (n->m_len < sizeof(struct sadb_msg)) {
		n = m_pullup(n, sizeof(struct sadb_msg));
		if (n == NULL)
			return key_senderror(so, m, ENOBUFS);
	}
	newmsg = mtod(n, struct sadb_msg *);
	newmsg->sadb_msg_errno = 0;
	newmsg->sadb_msg_len = PFKEY_UNIT64(n->m_pkthdr.len);

	m_freem(m);
	return key_sendup_mbuf(so, n, KEY_SENDUP_ALL);
    }
}

/*
 * Delete all alive SAs for corresponding xform.
 * Larval SAs have not initialized tdb_xform, so it is safe to leave them
 * here when xform disappears.
 */
void
key_delete_xform(const struct xformsw *xsp)
{
	struct secasvar_queue drainq;
	struct secashead *sah;
	struct secasvar *sav, *nextsav;

	TAILQ_INIT(&drainq);
	SAHTREE_WLOCK();
	TAILQ_FOREACH(sah, &V_sahtree, chain) {
		sav = TAILQ_FIRST(&sah->savtree_alive);
		if (sav == NULL)
			continue;
		if (sav->tdb_xform != xsp)
			continue;
		/*
		 * It is supposed that all SAs in the chain are related to
		 * one xform.
		 */
		TAILQ_CONCAT(&drainq, &sah->savtree_alive, chain);
	}
	/* Unlink all queued SAs from SPI hash */
	TAILQ_FOREACH(sav, &drainq, chain) {
		sav->state = SADB_SASTATE_DEAD;
		LIST_REMOVE(sav, spihash);
	}
	SAHTREE_WUNLOCK();

	/* Now we can release reference for all SAs in drainq */
	sav = TAILQ_FIRST(&drainq);
	while (sav != NULL) {
		KEYDBG(KEY_STAMP,
		    printf("%s: SA(%p)\n", __func__, sav));
		KEYDBG(KEY_DATA, kdebug_secasv(sav));
		nextsav = TAILQ_NEXT(sav, chain);
		key_freesah(&sav->sah); /* release reference from SAV */
		key_freesav(&sav); /* release last reference */
		sav = nextsav;
	}
}

/*
 * SADB_GET processing
 * receive
 *   <base, SA(*), address(SD)>
 * from the ikmpd, and get a SP and a SA to respond,
 * and send,
 *   <base, SA, (lifetime(HSC),) address(SD), (address(P),) key(AE),
 *       (identity(SD),) (sensitivity)>
 * to the ikmpd.
 *
 * m will always be freed.
 */
static int
key_get(struct socket *so, struct mbuf *m, const struct sadb_msghdr *mhp)
{
	struct secasindex saidx;
	struct sadb_address *src0, *dst0;
	struct sadb_sa *sa0;
	struct secasvar *sav;
	uint8_t proto;

	IPSEC_ASSERT(so != NULL, ("null socket"));
	IPSEC_ASSERT(m != NULL, ("null mbuf"));
	IPSEC_ASSERT(mhp != NULL, ("null msghdr"));
	IPSEC_ASSERT(mhp->msg != NULL, ("null msg"));

	/* map satype to proto */
	if ((proto = key_satype2proto(mhp->msg->sadb_msg_satype)) == 0) {
		ipseclog((LOG_DEBUG, "%s: invalid satype is passed.\n",
			__func__));
		return key_senderror(so, m, EINVAL);
	}

	if (SADB_CHECKHDR(mhp, SADB_EXT_SA) ||
	    SADB_CHECKHDR(mhp, SADB_EXT_ADDRESS_SRC) ||
	    SADB_CHECKHDR(mhp, SADB_EXT_ADDRESS_DST)) {
		ipseclog((LOG_DEBUG,
		    "%s: invalid message: missing required header.\n",
		    __func__));
		return key_senderror(so, m, EINVAL);
	}
	if (SADB_CHECKLEN(mhp, SADB_EXT_SA) ||
	    SADB_CHECKLEN(mhp, SADB_EXT_ADDRESS_SRC) ||
	    SADB_CHECKLEN(mhp, SADB_EXT_ADDRESS_DST)) {
		ipseclog((LOG_DEBUG,
		    "%s: invalid message: wrong header size.\n", __func__));
		return key_senderror(so, m, EINVAL);
	}

	sa0 = (struct sadb_sa *)mhp->ext[SADB_EXT_SA];
	src0 = (struct sadb_address *)mhp->ext[SADB_EXT_ADDRESS_SRC];
	dst0 = (struct sadb_address *)mhp->ext[SADB_EXT_ADDRESS_DST];

	if (key_checksockaddrs((struct sockaddr *)(src0 + 1),
	    (struct sockaddr *)(dst0 + 1)) != 0) {
		ipseclog((LOG_DEBUG, "%s: invalid sockaddr.\n", __func__));
		return key_senderror(so, m, EINVAL);
	}
	KEY_SETSECASIDX(proto, IPSEC_MODE_ANY, 0, src0 + 1, dst0 + 1, &saidx);

	if (proto == IPPROTO_TCP)
		sav = key_getsav_tcpmd5(&saidx, NULL);
	else
		sav = key_getsavbyspi(sa0->sadb_sa_spi);
	if (sav == NULL) {
		ipseclog((LOG_DEBUG, "%s: no SA found.\n", __func__));
		return key_senderror(so, m, ESRCH);
	}
	if (key_cmpsaidx(&sav->sah->saidx, &saidx, CMP_HEAD) == 0) {
		ipseclog((LOG_DEBUG, "%s: saidx mismatched for SPI %u.\n",
		    __func__, ntohl(sa0->sadb_sa_spi)));
		key_freesav(&sav);
		return (key_senderror(so, m, ESRCH));
	}

    {
	struct mbuf *n;
	uint8_t satype;

	/* map proto to satype */
	if ((satype = key_proto2satype(sav->sah->saidx.proto)) == 0) {
		ipseclog((LOG_DEBUG, "%s: there was invalid proto in SAD.\n",
		    __func__));
		key_freesav(&sav);
		return key_senderror(so, m, EINVAL);
	}

	/* create new sadb_msg to reply. */
	n = key_setdumpsa(sav, SADB_GET, satype, mhp->msg->sadb_msg_seq,
	    mhp->msg->sadb_msg_pid);

	key_freesav(&sav);
	if (!n)
		return key_senderror(so, m, ENOBUFS);

	m_freem(m);
	return key_sendup_mbuf(so, n, KEY_SENDUP_ONE);
    }
}

/* XXX make it sysctl-configurable? */
static void
key_getcomb_setlifetime(struct sadb_comb *comb)
{

	comb->sadb_comb_soft_allocations = 1;
	comb->sadb_comb_hard_allocations = 1;
	comb->sadb_comb_soft_bytes = 0;
	comb->sadb_comb_hard_bytes = 0;
	comb->sadb_comb_hard_addtime = 86400;	/* 1 day */
	comb->sadb_comb_soft_addtime = comb->sadb_comb_soft_addtime * 80 / 100;
	comb->sadb_comb_soft_usetime = 28800;	/* 8 hours */
	comb->sadb_comb_hard_usetime = comb->sadb_comb_hard_usetime * 80 / 100;
}

/*
 * XXX reorder combinations by preference
 * XXX no idea if the user wants ESP authentication or not
 */
static struct mbuf *
key_getcomb_ealg(void)
{
	struct sadb_comb *comb;
	const struct enc_xform *algo;
	struct mbuf *result = NULL, *m, *n;
	int encmin;
	int i, off, o;
	int totlen;
	const int l = PFKEY_ALIGN8(sizeof(struct sadb_comb));

	m = NULL;
	for (i = 1; i <= SADB_EALG_MAX; i++) {
		algo = enc_algorithm_lookup(i);
		if (algo == NULL)
			continue;

		/* discard algorithms with key size smaller than system min */
		if (_BITS(algo->maxkey) < V_ipsec_esp_keymin)
			continue;
		if (_BITS(algo->minkey) < V_ipsec_esp_keymin)
			encmin = V_ipsec_esp_keymin;
		else
			encmin = _BITS(algo->minkey);

		if (V_ipsec_esp_auth)
			m = key_getcomb_ah();
		else {
			IPSEC_ASSERT(l <= MLEN,
				("l=%u > MLEN=%lu", l, (u_long) MLEN));
			MGET(m, M_NOWAIT, MT_DATA);
			if (m) {
				M_ALIGN(m, l);
				m->m_len = l;
				m->m_next = NULL;
				bzero(mtod(m, caddr_t), m->m_len);
			}
		}
		if (!m)
			goto fail;

		totlen = 0;
		for (n = m; n; n = n->m_next)
			totlen += n->m_len;
		IPSEC_ASSERT((totlen % l) == 0, ("totlen=%u, l=%u", totlen, l));

		for (off = 0; off < totlen; off += l) {
			n = m_pulldown(m, off, l, &o);
			if (!n) {
				/* m is already freed */
				goto fail;
			}
			comb = (struct sadb_comb *)(mtod(n, caddr_t) + o);
			bzero(comb, sizeof(*comb));
			key_getcomb_setlifetime(comb);
			comb->sadb_comb_encrypt = i;
			comb->sadb_comb_encrypt_minbits = encmin;
			comb->sadb_comb_encrypt_maxbits = _BITS(algo->maxkey);
		}

		if (!result)
			result = m;
		else
			m_cat(result, m);
	}

	return result;

 fail:
	if (result)
		m_freem(result);
	return NULL;
}

static void
key_getsizes_ah(const struct auth_hash *ah, int alg, u_int16_t* min,
    u_int16_t* max)
{

	*min = *max = ah->hashsize;
	if (ah->keysize == 0) {
		/*
		 * Transform takes arbitrary key size but algorithm
		 * key size is restricted.  Enforce this here.
		 */
		switch (alg) {
		case SADB_X_AALG_MD5:	*min = *max = 16; break;
		case SADB_X_AALG_SHA:	*min = *max = 20; break;
		case SADB_X_AALG_NULL:	*min = 1; *max = 256; break;
		case SADB_X_AALG_SHA2_256: *min = *max = 32; break;
		case SADB_X_AALG_SHA2_384: *min = *max = 48; break;
		case SADB_X_AALG_SHA2_512: *min = *max = 64; break;
		default:
			DPRINTF(("%s: unknown AH algorithm %u\n",
				__func__, alg));
			break;
		}
	}
}

/*
 * XXX reorder combinations by preference
 */
static struct mbuf *
key_getcomb_ah()
{
	const struct auth_hash *algo;
	struct sadb_comb *comb;
	struct mbuf *m;
	u_int16_t minkeysize, maxkeysize;
	int i;
	const int l = PFKEY_ALIGN8(sizeof(struct sadb_comb));

	m = NULL;
	for (i = 1; i <= SADB_AALG_MAX; i++) {
#if 1
		/* we prefer HMAC algorithms, not old algorithms */
		if (i != SADB_AALG_SHA1HMAC &&
		    i != SADB_AALG_MD5HMAC  &&
		    i != SADB_X_AALG_SHA2_256 &&
		    i != SADB_X_AALG_SHA2_384 &&
		    i != SADB_X_AALG_SHA2_512)
			continue;
#endif
		algo = auth_algorithm_lookup(i);
		if (!algo)
			continue;
		key_getsizes_ah(algo, i, &minkeysize, &maxkeysize);
		/* discard algorithms with key size smaller than system min */
		if (_BITS(minkeysize) < V_ipsec_ah_keymin)
			continue;

		if (!m) {
			IPSEC_ASSERT(l <= MLEN,
				("l=%u > MLEN=%lu", l, (u_long) MLEN));
			MGET(m, M_NOWAIT, MT_DATA);
			if (m) {
				M_ALIGN(m, l);
				m->m_len = l;
				m->m_next = NULL;
			}
		} else
			M_PREPEND(m, l, M_NOWAIT);
		if (!m)
			return NULL;

		comb = mtod(m, struct sadb_comb *);
		bzero(comb, sizeof(*comb));
		key_getcomb_setlifetime(comb);
		comb->sadb_comb_auth = i;
		comb->sadb_comb_auth_minbits = _BITS(minkeysize);
		comb->sadb_comb_auth_maxbits = _BITS(maxkeysize);
	}

	return m;
}

/*
 * not really an official behavior.  discussed in pf_key@inner.net in Sep2000.
 * XXX reorder combinations by preference
 */
static struct mbuf *
key_getcomb_ipcomp()
{
	const struct comp_algo *algo;
	struct sadb_comb *comb;
	struct mbuf *m;
	int i;
	const int l = PFKEY_ALIGN8(sizeof(struct sadb_comb));

	m = NULL;
	for (i = 1; i <= SADB_X_CALG_MAX; i++) {
		algo = comp_algorithm_lookup(i);
		if (!algo)
			continue;

		if (!m) {
			IPSEC_ASSERT(l <= MLEN,
				("l=%u > MLEN=%lu", l, (u_long) MLEN));
			MGET(m, M_NOWAIT, MT_DATA);
			if (m) {
				M_ALIGN(m, l);
				m->m_len = l;
				m->m_next = NULL;
			}
		} else
			M_PREPEND(m, l, M_NOWAIT);
		if (!m)
			return NULL;

		comb = mtod(m, struct sadb_comb *);
		bzero(comb, sizeof(*comb));
		key_getcomb_setlifetime(comb);
		comb->sadb_comb_encrypt = i;
		/* what should we set into sadb_comb_*_{min,max}bits? */
	}

	return m;
}

/*
 * XXX no way to pass mode (transport/tunnel) to userland
 * XXX replay checking?
 * XXX sysctl interface to ipsec_{ah,esp}_keymin
 */
static struct mbuf *
key_getprop(const struct secasindex *saidx)
{
	struct sadb_prop *prop;
	struct mbuf *m, *n;
	const int l = PFKEY_ALIGN8(sizeof(struct sadb_prop));
	int totlen;

	switch (saidx->proto)  {
	case IPPROTO_ESP:
		m = key_getcomb_ealg();
		break;
	case IPPROTO_AH:
		m = key_getcomb_ah();
		break;
	case IPPROTO_IPCOMP:
		m = key_getcomb_ipcomp();
		break;
	default:
		return NULL;
	}

	if (!m)
		return NULL;
	M_PREPEND(m, l, M_NOWAIT);
	if (!m)
		return NULL;

	totlen = 0;
	for (n = m; n; n = n->m_next)
		totlen += n->m_len;

	prop = mtod(m, struct sadb_prop *);
	bzero(prop, sizeof(*prop));
	prop->sadb_prop_len = PFKEY_UNIT64(totlen);
	prop->sadb_prop_exttype = SADB_EXT_PROPOSAL;
	prop->sadb_prop_replay = 32;	/* XXX */

	return m;
}

/*
 * SADB_ACQUIRE processing called by key_checkrequest() and key_acquire2().
 * send
 *   <base, SA, address(SD), (address(P)), x_policy,
 *       (identity(SD),) (sensitivity,) proposal>
 * to KMD, and expect to receive
 *   <base> with SADB_ACQUIRE if error occurred,
 * or
 *   <base, src address, dst address, (SPI range)> with SADB_GETSPI
 * from KMD by PF_KEY.
 *
 * XXX x_policy is outside of RFC2367 (KAME extension).
 * XXX sensitivity is not supported.
 * XXX for ipcomp, RFC2367 does not define how to fill in proposal.
 * see comment for key_getcomb_ipcomp().
 *
 * OUT:
 *    0     : succeed
 *    others: error number
 */
static int
key_acquire(const struct secasindex *saidx, struct secpolicy *sp)
{
	union sockaddr_union addr;
	struct mbuf *result, *m;
	uint32_t seq;
	int error;
	uint16_t ul_proto;
	uint8_t mask, satype;

	IPSEC_ASSERT(saidx != NULL, ("null saidx"));
	satype = key_proto2satype(saidx->proto);
	IPSEC_ASSERT(satype != 0, ("null satype, protocol %u", saidx->proto));

	error = -1;
	result = NULL;
	ul_proto = IPSEC_ULPROTO_ANY;

	/* Get seq number to check whether sending message or not. */
	seq = key_getacq(saidx, &error);
	if (seq == 0)
		return (error);

	m = key_setsadbmsg(SADB_ACQUIRE, 0, satype, seq, 0, 0);
	if (!m) {
		error = ENOBUFS;
		goto fail;
	}
	result = m;

	/*
	 * set sadb_address for saidx's.
	 *
	 * Note that if sp is supplied, then we're being called from
	 * key_allocsa_policy() and should supply port and protocol
	 * information.
	 * XXXAE: why only TCP and UDP? ICMP and SCTP looks applicable too.
	 * XXXAE: probably we can handle this in the ipsec[46]_allocsa().
	 * XXXAE: it looks like we should save this info in the ACQ entry.
	 */
	if (sp != NULL && (sp->spidx.ul_proto == IPPROTO_TCP ||
	    sp->spidx.ul_proto == IPPROTO_UDP))
		ul_proto = sp->spidx.ul_proto;

	addr = saidx->src;
	mask = FULLMASK;
	if (ul_proto != IPSEC_ULPROTO_ANY) {
		switch (sp->spidx.src.sa.sa_family) {
		case AF_INET:
			if (sp->spidx.src.sin.sin_port != IPSEC_PORT_ANY) {
				addr.sin.sin_port = sp->spidx.src.sin.sin_port;
				mask = sp->spidx.prefs;
			}
			break;
		case AF_INET6:
			if (sp->spidx.src.sin6.sin6_port != IPSEC_PORT_ANY) {
				addr.sin6.sin6_port =
				    sp->spidx.src.sin6.sin6_port;
				mask = sp->spidx.prefs;
			}
			break;
		default:
			break;
		}
	}
	m = key_setsadbaddr(SADB_EXT_ADDRESS_SRC, &addr.sa, mask, ul_proto);
	if (!m) {
		error = ENOBUFS;
		goto fail;
	}
	m_cat(result, m);

	addr = saidx->dst;
	mask = FULLMASK;
	if (ul_proto != IPSEC_ULPROTO_ANY) {
		switch (sp->spidx.dst.sa.sa_family) {
		case AF_INET:
			if (sp->spidx.dst.sin.sin_port != IPSEC_PORT_ANY) {
				addr.sin.sin_port = sp->spidx.dst.sin.sin_port;
				mask = sp->spidx.prefd;
			}
			break;
		case AF_INET6:
			if (sp->spidx.dst.sin6.sin6_port != IPSEC_PORT_ANY) {
				addr.sin6.sin6_port =
				    sp->spidx.dst.sin6.sin6_port;
				mask = sp->spidx.prefd;
			}
			break;
		default:
			break;
		}
	}
	m = key_setsadbaddr(SADB_EXT_ADDRESS_DST, &addr.sa, mask, ul_proto);
	if (!m) {
		error = ENOBUFS;
		goto fail;
	}
	m_cat(result, m);

	/* XXX proxy address (optional) */

	/*
	 * Set sadb_x_policy. This is KAME extension to RFC2367.
	 */
	if (sp != NULL) {
		m = key_setsadbxpolicy(sp->policy, sp->spidx.dir, sp->id,
		    sp->priority);
		if (!m) {
			error = ENOBUFS;
			goto fail;
		}
		m_cat(result, m);
	}

	/*
	 * Set sadb_x_sa2 extension if saidx->reqid is not zero.
	 * This is FreeBSD extension to RFC2367.
	 */
	if (saidx->reqid != 0) {
		m = key_setsadbxsa2(saidx->mode, 0, saidx->reqid);
		if (m == NULL) {
			error = ENOBUFS;
			goto fail;
		}
		m_cat(result, m);
	}
	/* XXX identity (optional) */
#if 0
	if (idexttype && fqdn) {
		/* create identity extension (FQDN) */
		struct sadb_ident *id;
		int fqdnlen;

		fqdnlen = strlen(fqdn) + 1;	/* +1 for terminating-NUL */
		id = (struct sadb_ident *)p;
		bzero(id, sizeof(*id) + PFKEY_ALIGN8(fqdnlen));
		id->sadb_ident_len = PFKEY_UNIT64(sizeof(*id) + PFKEY_ALIGN8(fqdnlen));
		id->sadb_ident_exttype = idexttype;
		id->sadb_ident_type = SADB_IDENTTYPE_FQDN;
		bcopy(fqdn, id + 1, fqdnlen);
		p += sizeof(struct sadb_ident) + PFKEY_ALIGN8(fqdnlen);
	}

	if (idexttype) {
		/* create identity extension (USERFQDN) */
		struct sadb_ident *id;
		int userfqdnlen;

		if (userfqdn) {
			/* +1 for terminating-NUL */
			userfqdnlen = strlen(userfqdn) + 1;
		} else
			userfqdnlen = 0;
		id = (struct sadb_ident *)p;
		bzero(id, sizeof(*id) + PFKEY_ALIGN8(userfqdnlen));
		id->sadb_ident_len = PFKEY_UNIT64(sizeof(*id) + PFKEY_ALIGN8(userfqdnlen));
		id->sadb_ident_exttype = idexttype;
		id->sadb_ident_type = SADB_IDENTTYPE_USERFQDN;
		/* XXX is it correct? */
		if (curproc && curproc->p_cred)
			id->sadb_ident_id = curproc->p_cred->p_ruid;
		if (userfqdn && userfqdnlen)
			bcopy(userfqdn, id + 1, userfqdnlen);
		p += sizeof(struct sadb_ident) + PFKEY_ALIGN8(userfqdnlen);
	}
#endif

	/* XXX sensitivity (optional) */

	/* create proposal/combination extension */
	m = key_getprop(saidx);
#if 0
	/*
	 * spec conformant: always attach proposal/combination extension,
	 * the problem is that we have no way to attach it for ipcomp,
	 * due to the way sadb_comb is declared in RFC2367.
	 */
	if (!m) {
		error = ENOBUFS;
		goto fail;
	}
	m_cat(result, m);
#else
	/*
	 * outside of spec; make proposal/combination extension optional.
	 */
	if (m)
		m_cat(result, m);
#endif

	if ((result->m_flags & M_PKTHDR) == 0) {
		error = EINVAL;
		goto fail;
	}

	if (result->m_len < sizeof(struct sadb_msg)) {
		result = m_pullup(result, sizeof(struct sadb_msg));
		if (result == NULL) {
			error = ENOBUFS;
			goto fail;
		}
	}

	result->m_pkthdr.len = 0;
	for (m = result; m; m = m->m_next)
		result->m_pkthdr.len += m->m_len;

	mtod(result, struct sadb_msg *)->sadb_msg_len =
	    PFKEY_UNIT64(result->m_pkthdr.len);

	KEYDBG(KEY_STAMP,
	    printf("%s: SP(%p)\n", __func__, sp));
	KEYDBG(KEY_DATA, kdebug_secasindex(saidx, NULL));

	return key_sendup_mbuf(NULL, result, KEY_SENDUP_REGISTERED);

 fail:
	if (result)
		m_freem(result);
	return error;
}

static uint32_t
key_newacq(const struct secasindex *saidx, int *perror)
{
	struct secacq *acq;
	uint32_t seq;

	acq = malloc(sizeof(*acq), M_IPSEC_SAQ, M_NOWAIT | M_ZERO);
	if (acq == NULL) {
		ipseclog((LOG_DEBUG, "%s: No more memory.\n", __func__));
		*perror = ENOBUFS;
		return (0);
	}

	/* copy secindex */
	bcopy(saidx, &acq->saidx, sizeof(acq->saidx));
	acq->created = time_second;
	acq->count = 0;

	/* add to acqtree */
	ACQ_LOCK();
	seq = acq->seq = (V_acq_seq == ~0 ? 1 : ++V_acq_seq);
	LIST_INSERT_HEAD(&V_acqtree, acq, chain);
	LIST_INSERT_HEAD(ACQADDRHASH_HASH(saidx), acq, addrhash);
	LIST_INSERT_HEAD(ACQSEQHASH_HASH(seq), acq, seqhash);
	ACQ_UNLOCK();
	*perror = 0;
	return (seq);
}

static uint32_t
key_getacq(const struct secasindex *saidx, int *perror)
{
	struct secacq *acq;
	uint32_t seq;

	ACQ_LOCK();
	LIST_FOREACH(acq, ACQADDRHASH_HASH(saidx), addrhash) {
		if (key_cmpsaidx(&acq->saidx, saidx, CMP_EXACTLY)) {
			if (acq->count > V_key_blockacq_count) {
				/*
				 * Reset counter and send message.
				 * Also reset created time to keep ACQ for
				 * this saidx.
				 */
				acq->created = time_second;
				acq->count = 0;
				seq = acq->seq;
			} else {
				/*
				 * Increment counter and do nothing.
				 * We send SADB_ACQUIRE message only
				 * for each V_key_blockacq_count packet.
				 */
				acq->count++;
				seq = 0;
			}
			break;
		}
	}
	ACQ_UNLOCK();
	if (acq != NULL) {
		*perror = 0;
		return (seq);
	}
	/* allocate new  entry */
	return (key_newacq(saidx, perror));
}

static int
key_acqreset(uint32_t seq)
{
	struct secacq *acq;

	ACQ_LOCK();
	LIST_FOREACH(acq, ACQSEQHASH_HASH(seq), seqhash) {
		if (acq->seq == seq) {
			acq->count = 0;
			acq->created = time_second;
			break;
		}
	}
	ACQ_UNLOCK();
	if (acq == NULL)
		return (ESRCH);
	return (0);
}
/*
 * Mark ACQ entry as stale to remove it in key_flush_acq().
 * Called after successful SADB_GETSPI message.
 */
static int
key_acqdone(const struct secasindex *saidx, uint32_t seq)
{
	struct secacq *acq;

	ACQ_LOCK();
	LIST_FOREACH(acq, ACQSEQHASH_HASH(seq), seqhash) {
		if (acq->seq == seq)
			break;
	}
	if (acq != NULL) {
		if (key_cmpsaidx(&acq->saidx, saidx, CMP_EXACTLY) == 0) {
			ipseclog((LOG_DEBUG,
			    "%s: Mismatched saidx for ACQ %u", __func__, seq));
			acq = NULL;
		} else {
			acq->created = 0;
		}
	} else {
		ipseclog((LOG_DEBUG,
		    "%s: ACQ %u is not found.", __func__, seq));
	}
	ACQ_UNLOCK();
	if (acq == NULL)
		return (ESRCH);
	return (0);
}

static struct secspacq *
key_newspacq(struct secpolicyindex *spidx)
{
	struct secspacq *acq;

	/* get new entry */
	acq = malloc(sizeof(struct secspacq), M_IPSEC_SAQ, M_NOWAIT|M_ZERO);
	if (acq == NULL) {
		ipseclog((LOG_DEBUG, "%s: No more memory.\n", __func__));
		return NULL;
	}

	/* copy secindex */
	bcopy(spidx, &acq->spidx, sizeof(acq->spidx));
	acq->created = time_second;
	acq->count = 0;

	/* add to spacqtree */
	SPACQ_LOCK();
	LIST_INSERT_HEAD(&V_spacqtree, acq, chain);
	SPACQ_UNLOCK();

	return acq;
}

static struct secspacq *
key_getspacq(struct secpolicyindex *spidx)
{
	struct secspacq *acq;

	SPACQ_LOCK();
	LIST_FOREACH(acq, &V_spacqtree, chain) {
		if (key_cmpspidx_exactly(spidx, &acq->spidx)) {
			/* NB: return holding spacq_lock */
			return acq;
		}
	}
	SPACQ_UNLOCK();

	return NULL;
}

/*
 * SADB_ACQUIRE processing,
 * in first situation, is receiving
 *   <base>
 * from the ikmpd, and clear sequence of its secasvar entry.
 *
 * In second situation, is receiving
 *   <base, address(SD), (address(P),) (identity(SD),) (sensitivity,) proposal>
 * from a user land process, and return
 *   <base, address(SD), (address(P),) (identity(SD),) (sensitivity,) proposal>
 * to the socket.
 *
 * m will always be freed.
 */
static int
key_acquire2(struct socket *so, struct mbuf *m, const struct sadb_msghdr *mhp)
{
	SAHTREE_RLOCK_TRACKER;
	struct sadb_address *src0, *dst0;
	struct secasindex saidx;
	struct secashead *sah;
	uint32_t reqid;
	int error;
	uint8_t mode, proto;

	IPSEC_ASSERT(so != NULL, ("null socket"));
	IPSEC_ASSERT(m != NULL, ("null mbuf"));
	IPSEC_ASSERT(mhp != NULL, ("null msghdr"));
	IPSEC_ASSERT(mhp->msg != NULL, ("null msg"));

	/*
	 * Error message from KMd.
	 * We assume that if error was occurred in IKEd, the length of PFKEY
	 * message is equal to the size of sadb_msg structure.
	 * We do not raise error even if error occurred in this function.
	 */
	if (mhp->msg->sadb_msg_len == PFKEY_UNIT64(sizeof(struct sadb_msg))) {
		/* check sequence number */
		if (mhp->msg->sadb_msg_seq == 0 ||
		    mhp->msg->sadb_msg_errno == 0) {
			ipseclog((LOG_DEBUG, "%s: must specify sequence "
				"number and errno.\n", __func__));
		} else {
			/*
			 * IKEd reported that error occurred.
			 * XXXAE: what it expects from the kernel?
			 * Probably we should send SADB_ACQUIRE again?
			 * If so, reset ACQ's state.
			 * XXXAE: it looks useless.
			 */
			key_acqreset(mhp->msg->sadb_msg_seq);
		}
		m_freem(m);
		return (0);
	}

	/*
	 * This message is from user land.
	 */

	/* map satype to proto */
	if ((proto = key_satype2proto(mhp->msg->sadb_msg_satype)) == 0) {
		ipseclog((LOG_DEBUG, "%s: invalid satype is passed.\n",
		    __func__));
		return key_senderror(so, m, EINVAL);
	}

	if (SADB_CHECKHDR(mhp, SADB_EXT_ADDRESS_SRC) ||
	    SADB_CHECKHDR(mhp, SADB_EXT_ADDRESS_DST) ||
	    SADB_CHECKHDR(mhp, SADB_EXT_PROPOSAL)) {
		ipseclog((LOG_DEBUG,
		    "%s: invalid message: missing required header.\n",
		    __func__));
		return key_senderror(so, m, EINVAL);
	}
	if (SADB_CHECKLEN(mhp, SADB_EXT_ADDRESS_SRC) ||
	    SADB_CHECKLEN(mhp, SADB_EXT_ADDRESS_DST) ||
	    SADB_CHECKLEN(mhp, SADB_EXT_PROPOSAL)) {
		ipseclog((LOG_DEBUG,
		    "%s: invalid message: wrong header size.\n", __func__));
		return key_senderror(so, m, EINVAL);
	}

	if (SADB_CHECKHDR(mhp, SADB_X_EXT_SA2)) {
		mode = IPSEC_MODE_ANY;
		reqid = 0;
	} else {
		if (SADB_CHECKLEN(mhp, SADB_X_EXT_SA2)) {
			ipseclog((LOG_DEBUG,
			    "%s: invalid message: wrong header size.\n",
			    __func__));
			return key_senderror(so, m, EINVAL);
		}
		mode = ((struct sadb_x_sa2 *)
		    mhp->ext[SADB_X_EXT_SA2])->sadb_x_sa2_mode;
		reqid = ((struct sadb_x_sa2 *)
		    mhp->ext[SADB_X_EXT_SA2])->sadb_x_sa2_reqid;
	}

	src0 = (struct sadb_address *)mhp->ext[SADB_EXT_ADDRESS_SRC];
	dst0 = (struct sadb_address *)mhp->ext[SADB_EXT_ADDRESS_DST];

	error = key_checksockaddrs((struct sockaddr *)(src0 + 1),
	    (struct sockaddr *)(dst0 + 1));
	if (error != 0) {
		ipseclog((LOG_DEBUG, "%s: invalid sockaddr.\n", __func__));
		return key_senderror(so, m, EINVAL);
	}
	KEY_SETSECASIDX(proto, mode, reqid, src0 + 1, dst0 + 1, &saidx);

	/* get a SA index */
	SAHTREE_RLOCK();
	LIST_FOREACH(sah, SAHADDRHASH_HASH(&saidx), addrhash) {
		if (key_cmpsaidx(&sah->saidx, &saidx, CMP_MODE_REQID))
			break;
	}
	SAHTREE_RUNLOCK();
	if (sah != NULL) {
		ipseclog((LOG_DEBUG, "%s: a SA exists already.\n", __func__));
		return key_senderror(so, m, EEXIST);
	}

	error = key_acquire(&saidx, NULL);
	if (error != 0) {
		ipseclog((LOG_DEBUG,
		    "%s: error %d returned from key_acquire()\n",
			__func__, error));
		return key_senderror(so, m, error);
	}
	m_freem(m);
	return (0);
}

/*
 * SADB_REGISTER processing.
 * If SATYPE_UNSPEC has been passed as satype, only return sabd_supported.
 * receive
 *   <base>
 * from the ikmpd, and register a socket to send PF_KEY messages,
 * and send
 *   <base, supported>
 * to KMD by PF_KEY.
 * If socket is detached, must free from regnode.
 *
 * m will always be freed.
 */
static int
key_register(struct socket *so, struct mbuf *m, const struct sadb_msghdr *mhp)
{
	struct secreg *reg, *newreg = NULL;

	IPSEC_ASSERT(so != NULL, ("null socket"));
	IPSEC_ASSERT(m != NULL, ("null mbuf"));
	IPSEC_ASSERT(mhp != NULL, ("null msghdr"));
	IPSEC_ASSERT(mhp->msg != NULL, ("null msg"));

	/* check for invalid register message */
	if (mhp->msg->sadb_msg_satype >= sizeof(V_regtree)/sizeof(V_regtree[0]))
		return key_senderror(so, m, EINVAL);

	/* When SATYPE_UNSPEC is specified, only return sabd_supported. */
	if (mhp->msg->sadb_msg_satype == SADB_SATYPE_UNSPEC)
		goto setmsg;

	/* check whether existing or not */
	REGTREE_LOCK();
	LIST_FOREACH(reg, &V_regtree[mhp->msg->sadb_msg_satype], chain) {
		if (reg->so == so) {
			REGTREE_UNLOCK();
			ipseclog((LOG_DEBUG, "%s: socket exists already.\n",
				__func__));
			return key_senderror(so, m, EEXIST);
		}
	}

	/* create regnode */
	newreg =  malloc(sizeof(struct secreg), M_IPSEC_SAR, M_NOWAIT|M_ZERO);
	if (newreg == NULL) {
		REGTREE_UNLOCK();
		ipseclog((LOG_DEBUG, "%s: No more memory.\n", __func__));
		return key_senderror(so, m, ENOBUFS);
	}

	newreg->so = so;
	((struct keycb *)sotorawcb(so))->kp_registered++;

	/* add regnode to regtree. */
	LIST_INSERT_HEAD(&V_regtree[mhp->msg->sadb_msg_satype], newreg, chain);
	REGTREE_UNLOCK();

  setmsg:
    {
	struct mbuf *n;
	struct sadb_msg *newmsg;
	struct sadb_supported *sup;
	u_int len, alen, elen;
	int off;
	int i;
	struct sadb_alg *alg;

	/* create new sadb_msg to reply. */
	alen = 0;
	for (i = 1; i <= SADB_AALG_MAX; i++) {
		if (auth_algorithm_lookup(i))
			alen += sizeof(struct sadb_alg);
	}
	if (alen)
		alen += sizeof(struct sadb_supported);
	elen = 0;
	for (i = 1; i <= SADB_EALG_MAX; i++) {
		if (enc_algorithm_lookup(i))
			elen += sizeof(struct sadb_alg);
	}
	if (elen)
		elen += sizeof(struct sadb_supported);

	len = sizeof(struct sadb_msg) + alen + elen;

	if (len > MCLBYTES)
		return key_senderror(so, m, ENOBUFS);

	MGETHDR(n, M_NOWAIT, MT_DATA);
	if (len > MHLEN) {
		if (!(MCLGET(n, M_NOWAIT))) {
			m_freem(n);
			n = NULL;
		}
	}
	if (!n)
		return key_senderror(so, m, ENOBUFS);

	n->m_pkthdr.len = n->m_len = len;
	n->m_next = NULL;
	off = 0;

	m_copydata(m, 0, sizeof(struct sadb_msg), mtod(n, caddr_t) + off);
	newmsg = mtod(n, struct sadb_msg *);
	newmsg->sadb_msg_errno = 0;
	newmsg->sadb_msg_len = PFKEY_UNIT64(len);
	off += PFKEY_ALIGN8(sizeof(struct sadb_msg));

	/* for authentication algorithm */
	if (alen) {
		sup = (struct sadb_supported *)(mtod(n, caddr_t) + off);
		sup->sadb_supported_len = PFKEY_UNIT64(alen);
		sup->sadb_supported_exttype = SADB_EXT_SUPPORTED_AUTH;
		off += PFKEY_ALIGN8(sizeof(*sup));

		for (i = 1; i <= SADB_AALG_MAX; i++) {
			const struct auth_hash *aalgo;
			u_int16_t minkeysize, maxkeysize;

			aalgo = auth_algorithm_lookup(i);
			if (!aalgo)
				continue;
			alg = (struct sadb_alg *)(mtod(n, caddr_t) + off);
			alg->sadb_alg_id = i;
			alg->sadb_alg_ivlen = 0;
			key_getsizes_ah(aalgo, i, &minkeysize, &maxkeysize);
			alg->sadb_alg_minbits = _BITS(minkeysize);
			alg->sadb_alg_maxbits = _BITS(maxkeysize);
			off += PFKEY_ALIGN8(sizeof(*alg));
		}
	}

	/* for encryption algorithm */
	if (elen) {
		sup = (struct sadb_supported *)(mtod(n, caddr_t) + off);
		sup->sadb_supported_len = PFKEY_UNIT64(elen);
		sup->sadb_supported_exttype = SADB_EXT_SUPPORTED_ENCRYPT;
		off += PFKEY_ALIGN8(sizeof(*sup));

		for (i = 1; i <= SADB_EALG_MAX; i++) {
			const struct enc_xform *ealgo;

			ealgo = enc_algorithm_lookup(i);
			if (!ealgo)
				continue;
			alg = (struct sadb_alg *)(mtod(n, caddr_t) + off);
			alg->sadb_alg_id = i;
			alg->sadb_alg_ivlen = ealgo->ivsize;
			alg->sadb_alg_minbits = _BITS(ealgo->minkey);
			alg->sadb_alg_maxbits = _BITS(ealgo->maxkey);
			off += PFKEY_ALIGN8(sizeof(struct sadb_alg));
		}
	}

	IPSEC_ASSERT(off == len,
		("length assumption failed (off %u len %u)", off, len));

	m_freem(m);
	return key_sendup_mbuf(so, n, KEY_SENDUP_REGISTERED);
    }
}

/*
 * free secreg entry registered.
 * XXX: I want to do free a socket marked done SADB_RESIGER to socket.
 */
void
key_freereg(struct socket *so)
{
	struct secreg *reg;
	int i;

	IPSEC_ASSERT(so != NULL, ("NULL so"));

	/*
	 * check whether existing or not.
	 * check all type of SA, because there is a potential that
	 * one socket is registered to multiple type of SA.
	 */
	REGTREE_LOCK();
	for (i = 0; i <= SADB_SATYPE_MAX; i++) {
		LIST_FOREACH(reg, &V_regtree[i], chain) {
			if (reg->so == so && __LIST_CHAINED(reg)) {
				LIST_REMOVE(reg, chain);
				free(reg, M_IPSEC_SAR);
				break;
			}
		}
	}
	REGTREE_UNLOCK();
}

/*
 * SADB_EXPIRE processing
 * send
 *   <base, SA, SA2, lifetime(C and one of HS), address(SD)>
 * to KMD by PF_KEY.
 * NOTE: We send only soft lifetime extension.
 *
 * OUT:	0	: succeed
 *	others	: error number
 */
static int
key_expire(struct secasvar *sav, int hard)
{
	struct mbuf *result = NULL, *m;
	struct sadb_lifetime *lt;
	uint32_t replay_count;
	int error, len;
	uint8_t satype;

	IPSEC_ASSERT (sav != NULL, ("null sav"));
	IPSEC_ASSERT (sav->sah != NULL, ("null sa header"));

	KEYDBG(KEY_STAMP,
	    printf("%s: SA(%p) expired %s lifetime\n", __func__,
		sav, hard ? "hard": "soft"));
	KEYDBG(KEY_DATA, kdebug_secasv(sav));
	/* set msg header */
	satype = key_proto2satype(sav->sah->saidx.proto);
	IPSEC_ASSERT(satype != 0, ("invalid proto, satype %u", satype));
	m = key_setsadbmsg(SADB_EXPIRE, 0, satype, sav->seq, 0, sav->refcnt);
	if (!m) {
		error = ENOBUFS;
		goto fail;
	}
	result = m;

	/* create SA extension */
	m = key_setsadbsa(sav);
	if (!m) {
		error = ENOBUFS;
		goto fail;
	}
	m_cat(result, m);

	/* create SA extension */
	SECASVAR_LOCK(sav);
	replay_count = sav->replay ? sav->replay->count : 0;
	SECASVAR_UNLOCK(sav);

	m = key_setsadbxsa2(sav->sah->saidx.mode, replay_count,
			sav->sah->saidx.reqid);
	if (!m) {
		error = ENOBUFS;
		goto fail;
	}
	m_cat(result, m);

	if (sav->replay && sav->replay->wsize > UINT8_MAX) {
		m = key_setsadbxsareplay(sav->replay->wsize);
		if (!m) {
			error = ENOBUFS;
			goto fail;
		}
		m_cat(result, m);
	}

	/* create lifetime extension (current and soft) */
	len = PFKEY_ALIGN8(sizeof(*lt)) * 2;
	m = m_get2(len, M_NOWAIT, MT_DATA, 0);
	if (m == NULL) {
		error = ENOBUFS;
		goto fail;
	}
	m_align(m, len);
	m->m_len = len;
	bzero(mtod(m, caddr_t), len);
	lt = mtod(m, struct sadb_lifetime *);
	lt->sadb_lifetime_len = PFKEY_UNIT64(sizeof(struct sadb_lifetime));
	lt->sadb_lifetime_exttype = SADB_EXT_LIFETIME_CURRENT;
	lt->sadb_lifetime_allocations =
	    (uint32_t)counter_u64_fetch(sav->lft_c_allocations);
	lt->sadb_lifetime_bytes =
	    counter_u64_fetch(sav->lft_c_bytes);
	lt->sadb_lifetime_addtime = sav->created;
	lt->sadb_lifetime_usetime = sav->firstused;
	lt = (struct sadb_lifetime *)(mtod(m, caddr_t) + len / 2);
	lt->sadb_lifetime_len = PFKEY_UNIT64(sizeof(struct sadb_lifetime));
	if (hard) {
		lt->sadb_lifetime_exttype = SADB_EXT_LIFETIME_HARD;
		lt->sadb_lifetime_allocations = sav->lft_h->allocations;
		lt->sadb_lifetime_bytes = sav->lft_h->bytes;
		lt->sadb_lifetime_addtime = sav->lft_h->addtime;
		lt->sadb_lifetime_usetime = sav->lft_h->usetime;
	} else {
		lt->sadb_lifetime_exttype = SADB_EXT_LIFETIME_SOFT;
		lt->sadb_lifetime_allocations = sav->lft_s->allocations;
		lt->sadb_lifetime_bytes = sav->lft_s->bytes;
		lt->sadb_lifetime_addtime = sav->lft_s->addtime;
		lt->sadb_lifetime_usetime = sav->lft_s->usetime;
	}
	m_cat(result, m);

	/* set sadb_address for source */
	m = key_setsadbaddr(SADB_EXT_ADDRESS_SRC,
	    &sav->sah->saidx.src.sa,
	    FULLMASK, IPSEC_ULPROTO_ANY);
	if (!m) {
		error = ENOBUFS;
		goto fail;
	}
	m_cat(result, m);

	/* set sadb_address for destination */
	m = key_setsadbaddr(SADB_EXT_ADDRESS_DST,
	    &sav->sah->saidx.dst.sa,
	    FULLMASK, IPSEC_ULPROTO_ANY);
	if (!m) {
		error = ENOBUFS;
		goto fail;
	}
	m_cat(result, m);

	/*
	 * XXX-BZ Handle NAT-T extensions here.
	 * XXXAE: it doesn't seem quite useful. IKEs should not depend on
	 * this information, we report only significant SA fields.
	 */

	if ((result->m_flags & M_PKTHDR) == 0) {
		error = EINVAL;
		goto fail;
	}

	if (result->m_len < sizeof(struct sadb_msg)) {
		result = m_pullup(result, sizeof(struct sadb_msg));
		if (result == NULL) {
			error = ENOBUFS;
			goto fail;
		}
	}

	result->m_pkthdr.len = 0;
	for (m = result; m; m = m->m_next)
		result->m_pkthdr.len += m->m_len;

	mtod(result, struct sadb_msg *)->sadb_msg_len =
	    PFKEY_UNIT64(result->m_pkthdr.len);

	return key_sendup_mbuf(NULL, result, KEY_SENDUP_REGISTERED);

 fail:
	if (result)
		m_freem(result);
	return error;
}

static void
key_freesah_flushed(struct secashead_queue *flushq)
{
	struct secashead *sah, *nextsah;
	struct secasvar *sav, *nextsav;

	sah = TAILQ_FIRST(flushq);
	while (sah != NULL) {
		sav = TAILQ_FIRST(&sah->savtree_larval);
		while (sav != NULL) {
			nextsav = TAILQ_NEXT(sav, chain);
			TAILQ_REMOVE(&sah->savtree_larval, sav, chain);
			key_freesav(&sav); /* release last reference */
			key_freesah(&sah); /* release reference from SAV */
			sav = nextsav;
		}
		sav = TAILQ_FIRST(&sah->savtree_alive);
		while (sav != NULL) {
			nextsav = TAILQ_NEXT(sav, chain);
			TAILQ_REMOVE(&sah->savtree_alive, sav, chain);
			key_freesav(&sav); /* release last reference */
			key_freesah(&sah); /* release reference from SAV */
			sav = nextsav;
		}
		nextsah = TAILQ_NEXT(sah, chain);
		key_freesah(&sah);	/* release last reference */
		sah = nextsah;
	}
}

/*
 * SADB_FLUSH processing
 * receive
 *   <base>
 * from the ikmpd, and free all entries in secastree.
 * and send,
 *   <base>
 * to the ikmpd.
 * NOTE: to do is only marking SADB_SASTATE_DEAD.
 *
 * m will always be freed.
 */
static int
key_flush(struct socket *so, struct mbuf *m, const struct sadb_msghdr *mhp)
{
	struct secashead_queue flushq;
	struct sadb_msg *newmsg;
	struct secashead *sah, *nextsah;
	struct secasvar *sav;
	uint8_t proto;
	int i;

	IPSEC_ASSERT(so != NULL, ("null socket"));
	IPSEC_ASSERT(mhp != NULL, ("null msghdr"));
	IPSEC_ASSERT(mhp->msg != NULL, ("null msg"));

	/* map satype to proto */
	if ((proto = key_satype2proto(mhp->msg->sadb_msg_satype)) == 0) {
		ipseclog((LOG_DEBUG, "%s: invalid satype is passed.\n",
			__func__));
		return key_senderror(so, m, EINVAL);
	}
	KEYDBG(KEY_STAMP,
	    printf("%s: proto %u\n", __func__, proto));

	TAILQ_INIT(&flushq);
	if (proto == IPSEC_PROTO_ANY) {
		/* no SATYPE specified, i.e. flushing all SA. */
		SAHTREE_WLOCK();
		/* Move all SAHs into flushq */
		TAILQ_CONCAT(&flushq, &V_sahtree, chain);
		/* Flush all buckets in SPI hash */
		for (i = 0; i < V_savhash_mask + 1; i++)
			LIST_INIT(&V_savhashtbl[i]);
		/* Flush all buckets in SAHADDRHASH */
		for (i = 0; i < V_sahaddrhash_mask + 1; i++)
			LIST_INIT(&V_sahaddrhashtbl[i]);
		/* Mark all SAHs as unlinked */
		TAILQ_FOREACH(sah, &flushq, chain) {
			sah->state = SADB_SASTATE_DEAD;
			/*
			 * Callout handler makes its job using
			 * RLOCK and drain queues. In case, when this
			 * function will be called just before it
			 * acquires WLOCK, we need to mark SAs as
			 * unlinked to prevent second unlink.
			 */
			TAILQ_FOREACH(sav, &sah->savtree_larval, chain) {
				sav->state = SADB_SASTATE_DEAD;
			}
			TAILQ_FOREACH(sav, &sah->savtree_alive, chain) {
				sav->state = SADB_SASTATE_DEAD;
			}
		}
		SAHTREE_WUNLOCK();
	} else {
		SAHTREE_WLOCK();
		sah = TAILQ_FIRST(&V_sahtree);
		while (sah != NULL) {
			IPSEC_ASSERT(sah->state != SADB_SASTATE_DEAD,
			    ("DEAD SAH %p in SADB_FLUSH", sah));
			nextsah = TAILQ_NEXT(sah, chain);
			if (sah->saidx.proto != proto) {
				sah = nextsah;
				continue;
			}
			sah->state = SADB_SASTATE_DEAD;
			TAILQ_REMOVE(&V_sahtree, sah, chain);
			LIST_REMOVE(sah, addrhash);
			/* Unlink all SAs from SPI hash */
			TAILQ_FOREACH(sav, &sah->savtree_larval, chain) {
				LIST_REMOVE(sav, spihash);
				sav->state = SADB_SASTATE_DEAD;
			}
			TAILQ_FOREACH(sav, &sah->savtree_alive, chain) {
				LIST_REMOVE(sav, spihash);
				sav->state = SADB_SASTATE_DEAD;
			}
			/* Add SAH into flushq */
			TAILQ_INSERT_HEAD(&flushq, sah, chain);
			sah = nextsah;
		}
		SAHTREE_WUNLOCK();
	}

	key_freesah_flushed(&flushq);
	/* Free all queued SAs and SAHs */
	if (m->m_len < sizeof(struct sadb_msg) ||
	    sizeof(struct sadb_msg) > m->m_len + M_TRAILINGSPACE(m)) {
		ipseclog((LOG_DEBUG, "%s: No more memory.\n", __func__));
		return key_senderror(so, m, ENOBUFS);
	}

	if (m->m_next)
		m_freem(m->m_next);
	m->m_next = NULL;
	m->m_pkthdr.len = m->m_len = sizeof(struct sadb_msg);
	newmsg = mtod(m, struct sadb_msg *);
	newmsg->sadb_msg_errno = 0;
	newmsg->sadb_msg_len = PFKEY_UNIT64(m->m_pkthdr.len);

	return key_sendup_mbuf(so, m, KEY_SENDUP_ALL);
}

/*
 * SADB_DUMP processing
 * dump all entries including status of DEAD in SAD.
 * receive
 *   <base>
 * from the ikmpd, and dump all secasvar leaves
 * and send,
 *   <base> .....
 * to the ikmpd.
 *
 * m will always be freed.
 */
static int
key_dump(struct socket *so, struct mbuf *m, const struct sadb_msghdr *mhp)
{
	SAHTREE_RLOCK_TRACKER;
	struct secashead *sah;
	struct secasvar *sav;
	struct mbuf *n;
	uint32_t cnt;
	uint8_t proto, satype;

	IPSEC_ASSERT(so != NULL, ("null socket"));
	IPSEC_ASSERT(m != NULL, ("null mbuf"));
	IPSEC_ASSERT(mhp != NULL, ("null msghdr"));
	IPSEC_ASSERT(mhp->msg != NULL, ("null msg"));

	/* map satype to proto */
	if ((proto = key_satype2proto(mhp->msg->sadb_msg_satype)) == 0) {
		ipseclog((LOG_DEBUG, "%s: invalid satype is passed.\n",
		    __func__));
		return key_senderror(so, m, EINVAL);
	}

	/* count sav entries to be sent to the userland. */
	cnt = 0;
	SAHTREE_RLOCK();
	TAILQ_FOREACH(sah, &V_sahtree, chain) {
		if (mhp->msg->sadb_msg_satype != SADB_SATYPE_UNSPEC &&
		    proto != sah->saidx.proto)
			continue;

		TAILQ_FOREACH(sav, &sah->savtree_larval, chain)
			cnt++;
		TAILQ_FOREACH(sav, &sah->savtree_alive, chain)
			cnt++;
	}

	if (cnt == 0) {
		SAHTREE_RUNLOCK();
		return key_senderror(so, m, ENOENT);
	}

	/* send this to the userland, one at a time. */
	TAILQ_FOREACH(sah, &V_sahtree, chain) {
		if (mhp->msg->sadb_msg_satype != SADB_SATYPE_UNSPEC &&
		    proto != sah->saidx.proto)
			continue;

		/* map proto to satype */
		if ((satype = key_proto2satype(sah->saidx.proto)) == 0) {
			SAHTREE_RUNLOCK();
			ipseclog((LOG_DEBUG, "%s: there was invalid proto in "
			    "SAD.\n", __func__));
			return key_senderror(so, m, EINVAL);
		}
		TAILQ_FOREACH(sav, &sah->savtree_larval, chain) {
			n = key_setdumpsa(sav, SADB_DUMP, satype,
			    --cnt, mhp->msg->sadb_msg_pid);
			if (n == NULL) {
				SAHTREE_RUNLOCK();
				return key_senderror(so, m, ENOBUFS);
			}
			key_sendup_mbuf(so, n, KEY_SENDUP_ONE);
		}
		TAILQ_FOREACH(sav, &sah->savtree_alive, chain) {
			n = key_setdumpsa(sav, SADB_DUMP, satype,
			    --cnt, mhp->msg->sadb_msg_pid);
			if (n == NULL) {
				SAHTREE_RUNLOCK();
				return key_senderror(so, m, ENOBUFS);
			}
			key_sendup_mbuf(so, n, KEY_SENDUP_ONE);
		}
	}
	SAHTREE_RUNLOCK();
	m_freem(m);
	return (0);
}
/*
 * SADB_X_PROMISC processing
 *
 * m will always be freed.
 */
static int
key_promisc(struct socket *so, struct mbuf *m, const struct sadb_msghdr *mhp)
{
	int olen;

	IPSEC_ASSERT(so != NULL, ("null socket"));
	IPSEC_ASSERT(m != NULL, ("null mbuf"));
	IPSEC_ASSERT(mhp != NULL, ("null msghdr"));
	IPSEC_ASSERT(mhp->msg != NULL, ("null msg"));

	olen = PFKEY_UNUNIT64(mhp->msg->sadb_msg_len);

	if (olen < sizeof(struct sadb_msg)) {
#if 1
		return key_senderror(so, m, EINVAL);
#else
		m_freem(m);
		return 0;
#endif
	} else if (olen == sizeof(struct sadb_msg)) {
		/* enable/disable promisc mode */
		struct keycb *kp;

		if ((kp = (struct keycb *)sotorawcb(so)) == NULL)
			return key_senderror(so, m, EINVAL);
		mhp->msg->sadb_msg_errno = 0;
		switch (mhp->msg->sadb_msg_satype) {
		case 0:
		case 1:
			kp->kp_promisc = mhp->msg->sadb_msg_satype;
			break;
		default:
			return key_senderror(so, m, EINVAL);
		}

		/* send the original message back to everyone */
		mhp->msg->sadb_msg_errno = 0;
		return key_sendup_mbuf(so, m, KEY_SENDUP_ALL);
	} else {
		/* send packet as is */

		m_adj(m, PFKEY_ALIGN8(sizeof(struct sadb_msg)));

		/* TODO: if sadb_msg_seq is specified, send to specific pid */
		return key_sendup_mbuf(so, m, KEY_SENDUP_ALL);
	}
}

static int (*key_typesw[])(struct socket *, struct mbuf *,
		const struct sadb_msghdr *) = {
	NULL,		/* SADB_RESERVED */
	key_getspi,	/* SADB_GETSPI */
	key_update,	/* SADB_UPDATE */
	key_add,	/* SADB_ADD */
	key_delete,	/* SADB_DELETE */
	key_get,	/* SADB_GET */
	key_acquire2,	/* SADB_ACQUIRE */
	key_register,	/* SADB_REGISTER */
	NULL,		/* SADB_EXPIRE */
	key_flush,	/* SADB_FLUSH */
	key_dump,	/* SADB_DUMP */
	key_promisc,	/* SADB_X_PROMISC */
	NULL,		/* SADB_X_PCHANGE */
	key_spdadd,	/* SADB_X_SPDUPDATE */
	key_spdadd,	/* SADB_X_SPDADD */
	key_spddelete,	/* SADB_X_SPDDELETE */
	key_spdget,	/* SADB_X_SPDGET */
	NULL,		/* SADB_X_SPDACQUIRE */
	key_spddump,	/* SADB_X_SPDDUMP */
	key_spdflush,	/* SADB_X_SPDFLUSH */
	key_spdadd,	/* SADB_X_SPDSETIDX */
	NULL,		/* SADB_X_SPDEXPIRE */
	key_spddelete2,	/* SADB_X_SPDDELETE2 */
};

/*
 * parse sadb_msg buffer to process PFKEYv2,
 * and create a data to response if needed.
 * I think to be dealed with mbuf directly.
 * IN:
 *     msgp  : pointer to pointer to a received buffer pulluped.
 *             This is rewrited to response.
 *     so    : pointer to socket.
 * OUT:
 *    length for buffer to send to user process.
 */
int
key_parse(struct mbuf *m, struct socket *so)
{
	struct sadb_msg *msg;
	struct sadb_msghdr mh;
	u_int orglen;
	int error;
	int target;

	IPSEC_ASSERT(so != NULL, ("null socket"));
	IPSEC_ASSERT(m != NULL, ("null mbuf"));

	if (m->m_len < sizeof(struct sadb_msg)) {
		m = m_pullup(m, sizeof(struct sadb_msg));
		if (!m)
			return ENOBUFS;
	}
	msg = mtod(m, struct sadb_msg *);
	orglen = PFKEY_UNUNIT64(msg->sadb_msg_len);
	target = KEY_SENDUP_ONE;

	if ((m->m_flags & M_PKTHDR) == 0 || m->m_pkthdr.len != orglen) {
		ipseclog((LOG_DEBUG, "%s: invalid message length.\n",__func__));
		PFKEYSTAT_INC(out_invlen);
		error = EINVAL;
		goto senderror;
	}

	if (msg->sadb_msg_version != PF_KEY_V2) {
		ipseclog((LOG_DEBUG, "%s: PF_KEY version %u is mismatched.\n",
		    __func__, msg->sadb_msg_version));
		PFKEYSTAT_INC(out_invver);
		error = EINVAL;
		goto senderror;
	}

	if (msg->sadb_msg_type > SADB_MAX) {
		ipseclog((LOG_DEBUG, "%s: invalid type %u is passed.\n",
		    __func__, msg->sadb_msg_type));
		PFKEYSTAT_INC(out_invmsgtype);
		error = EINVAL;
		goto senderror;
	}

	/* for old-fashioned code - should be nuked */
	if (m->m_pkthdr.len > MCLBYTES) {
		m_freem(m);
		return ENOBUFS;
	}
	if (m->m_next) {
		struct mbuf *n;

		MGETHDR(n, M_NOWAIT, MT_DATA);
		if (n && m->m_pkthdr.len > MHLEN) {
			if (!(MCLGET(n, M_NOWAIT))) {
				m_free(n);
				n = NULL;
			}
		}
		if (!n) {
			m_freem(m);
			return ENOBUFS;
		}
		m_copydata(m, 0, m->m_pkthdr.len, mtod(n, caddr_t));
		n->m_pkthdr.len = n->m_len = m->m_pkthdr.len;
		n->m_next = NULL;
		m_freem(m);
		m = n;
	}

	/* align the mbuf chain so that extensions are in contiguous region. */
	error = key_align(m, &mh);
	if (error)
		return error;

	msg = mh.msg;

	/* We use satype as scope mask for spddump */
	if (msg->sadb_msg_type == SADB_X_SPDDUMP) {
		switch (msg->sadb_msg_satype) {
		case IPSEC_POLICYSCOPE_ANY:
		case IPSEC_POLICYSCOPE_GLOBAL:
		case IPSEC_POLICYSCOPE_IFNET:
		case IPSEC_POLICYSCOPE_PCB:
			break;
		default:
			ipseclog((LOG_DEBUG, "%s: illegal satype=%u\n",
			    __func__, msg->sadb_msg_type));
			PFKEYSTAT_INC(out_invsatype);
			error = EINVAL;
			goto senderror;
		}
	} else {
		switch (msg->sadb_msg_satype) { /* check SA type */
		case SADB_SATYPE_UNSPEC:
			switch (msg->sadb_msg_type) {
			case SADB_GETSPI:
			case SADB_UPDATE:
			case SADB_ADD:
			case SADB_DELETE:
			case SADB_GET:
			case SADB_ACQUIRE:
			case SADB_EXPIRE:
				ipseclog((LOG_DEBUG, "%s: must specify satype "
				    "when msg type=%u.\n", __func__,
				    msg->sadb_msg_type));
				PFKEYSTAT_INC(out_invsatype);
				error = EINVAL;
				goto senderror;
			}
			break;
		case SADB_SATYPE_AH:
		case SADB_SATYPE_ESP:
		case SADB_X_SATYPE_IPCOMP:
		case SADB_X_SATYPE_TCPSIGNATURE:
			switch (msg->sadb_msg_type) {
			case SADB_X_SPDADD:
			case SADB_X_SPDDELETE:
			case SADB_X_SPDGET:
			case SADB_X_SPDFLUSH:
			case SADB_X_SPDSETIDX:
			case SADB_X_SPDUPDATE:
			case SADB_X_SPDDELETE2:
				ipseclog((LOG_DEBUG, "%s: illegal satype=%u\n",
				    __func__, msg->sadb_msg_type));
				PFKEYSTAT_INC(out_invsatype);
				error = EINVAL;
				goto senderror;
			}
			break;
		case SADB_SATYPE_RSVP:
		case SADB_SATYPE_OSPFV2:
		case SADB_SATYPE_RIPV2:
		case SADB_SATYPE_MIP:
			ipseclog((LOG_DEBUG, "%s: type %u isn't supported.\n",
			    __func__, msg->sadb_msg_satype));
			PFKEYSTAT_INC(out_invsatype);
			error = EOPNOTSUPP;
			goto senderror;
		case 1:	/* XXX: What does it do? */
			if (msg->sadb_msg_type == SADB_X_PROMISC)
				break;
			/*FALLTHROUGH*/
		default:
			ipseclog((LOG_DEBUG, "%s: invalid type %u is passed.\n",
			    __func__, msg->sadb_msg_satype));
			PFKEYSTAT_INC(out_invsatype);
			error = EINVAL;
			goto senderror;
		}
	}

	/* check field of upper layer protocol and address family */
	if (mh.ext[SADB_EXT_ADDRESS_SRC] != NULL
	 && mh.ext[SADB_EXT_ADDRESS_DST] != NULL) {
		struct sadb_address *src0, *dst0;
		u_int plen;

		src0 = (struct sadb_address *)(mh.ext[SADB_EXT_ADDRESS_SRC]);
		dst0 = (struct sadb_address *)(mh.ext[SADB_EXT_ADDRESS_DST]);

		/* check upper layer protocol */
		if (src0->sadb_address_proto != dst0->sadb_address_proto) {
			ipseclog((LOG_DEBUG, "%s: upper layer protocol "
				"mismatched.\n", __func__));
			PFKEYSTAT_INC(out_invaddr);
			error = EINVAL;
			goto senderror;
		}

		/* check family */
		if (PFKEY_ADDR_SADDR(src0)->sa_family !=
		    PFKEY_ADDR_SADDR(dst0)->sa_family) {
			ipseclog((LOG_DEBUG, "%s: address family mismatched.\n",
				__func__));
			PFKEYSTAT_INC(out_invaddr);
			error = EINVAL;
			goto senderror;
		}
		if (PFKEY_ADDR_SADDR(src0)->sa_len !=
		    PFKEY_ADDR_SADDR(dst0)->sa_len) {
			ipseclog((LOG_DEBUG, "%s: address struct size "
				"mismatched.\n", __func__));
			PFKEYSTAT_INC(out_invaddr);
			error = EINVAL;
			goto senderror;
		}

		switch (PFKEY_ADDR_SADDR(src0)->sa_family) {
		case AF_INET:
			if (PFKEY_ADDR_SADDR(src0)->sa_len !=
			    sizeof(struct sockaddr_in)) {
				PFKEYSTAT_INC(out_invaddr);
				error = EINVAL;
				goto senderror;
			}
			break;
		case AF_INET6:
			if (PFKEY_ADDR_SADDR(src0)->sa_len !=
			    sizeof(struct sockaddr_in6)) {
				PFKEYSTAT_INC(out_invaddr);
				error = EINVAL;
				goto senderror;
			}
			break;
		default:
			ipseclog((LOG_DEBUG, "%s: unsupported address family\n",
				__func__));
			PFKEYSTAT_INC(out_invaddr);
			error = EAFNOSUPPORT;
			goto senderror;
		}

		switch (PFKEY_ADDR_SADDR(src0)->sa_family) {
		case AF_INET:
			plen = sizeof(struct in_addr) << 3;
			break;
		case AF_INET6:
			plen = sizeof(struct in6_addr) << 3;
			break;
		default:
			plen = 0;	/*fool gcc*/
			break;
		}

		/* check max prefix length */
		if (src0->sadb_address_prefixlen > plen ||
		    dst0->sadb_address_prefixlen > plen) {
			ipseclog((LOG_DEBUG, "%s: illegal prefixlen.\n",
				__func__));
			PFKEYSTAT_INC(out_invaddr);
			error = EINVAL;
			goto senderror;
		}

		/*
		 * prefixlen == 0 is valid because there can be a case when
		 * all addresses are matched.
		 */
	}

	if (msg->sadb_msg_type >= nitems(key_typesw) ||
	    key_typesw[msg->sadb_msg_type] == NULL) {
		PFKEYSTAT_INC(out_invmsgtype);
		error = EINVAL;
		goto senderror;
	}

	return (*key_typesw[msg->sadb_msg_type])(so, m, &mh);

senderror:
	msg->sadb_msg_errno = error;
	return key_sendup_mbuf(so, m, target);
}

static int
key_senderror(struct socket *so, struct mbuf *m, int code)
{
	struct sadb_msg *msg;

	IPSEC_ASSERT(m->m_len >= sizeof(struct sadb_msg),
		("mbuf too small, len %u", m->m_len));

	msg = mtod(m, struct sadb_msg *);
	msg->sadb_msg_errno = code;
	return key_sendup_mbuf(so, m, KEY_SENDUP_ONE);
}

/*
 * set the pointer to each header into message buffer.
 * m will be freed on error.
 * XXX larger-than-MCLBYTES extension?
 */
static int
key_align(struct mbuf *m, struct sadb_msghdr *mhp)
{
	struct mbuf *n;
	struct sadb_ext *ext;
	size_t off, end;
	int extlen;
	int toff;

	IPSEC_ASSERT(m != NULL, ("null mbuf"));
	IPSEC_ASSERT(mhp != NULL, ("null msghdr"));
	IPSEC_ASSERT(m->m_len >= sizeof(struct sadb_msg),
		("mbuf too small, len %u", m->m_len));

	/* initialize */
	bzero(mhp, sizeof(*mhp));

	mhp->msg = mtod(m, struct sadb_msg *);
	mhp->ext[0] = (struct sadb_ext *)mhp->msg;	/*XXX backward compat */

	end = PFKEY_UNUNIT64(mhp->msg->sadb_msg_len);
	extlen = end;	/*just in case extlen is not updated*/
	for (off = sizeof(struct sadb_msg); off < end; off += extlen) {
		n = m_pulldown(m, off, sizeof(struct sadb_ext), &toff);
		if (!n) {
			/* m is already freed */
			return ENOBUFS;
		}
		ext = (struct sadb_ext *)(mtod(n, caddr_t) + toff);

		/* set pointer */
		switch (ext->sadb_ext_type) {
		case SADB_EXT_SA:
		case SADB_EXT_ADDRESS_SRC:
		case SADB_EXT_ADDRESS_DST:
		case SADB_EXT_ADDRESS_PROXY:
		case SADB_EXT_LIFETIME_CURRENT:
		case SADB_EXT_LIFETIME_HARD:
		case SADB_EXT_LIFETIME_SOFT:
		case SADB_EXT_KEY_AUTH:
		case SADB_EXT_KEY_ENCRYPT:
		case SADB_EXT_IDENTITY_SRC:
		case SADB_EXT_IDENTITY_DST:
		case SADB_EXT_SENSITIVITY:
		case SADB_EXT_PROPOSAL:
		case SADB_EXT_SUPPORTED_AUTH:
		case SADB_EXT_SUPPORTED_ENCRYPT:
		case SADB_EXT_SPIRANGE:
		case SADB_X_EXT_POLICY:
		case SADB_X_EXT_SA2:
		case SADB_X_EXT_NAT_T_TYPE:
		case SADB_X_EXT_NAT_T_SPORT:
		case SADB_X_EXT_NAT_T_DPORT:
		case SADB_X_EXT_NAT_T_OAI:
		case SADB_X_EXT_NAT_T_OAR:
		case SADB_X_EXT_NAT_T_FRAG:
		case SADB_X_EXT_SA_REPLAY:
		case SADB_X_EXT_NEW_ADDRESS_SRC:
		case SADB_X_EXT_NEW_ADDRESS_DST:
			/* duplicate check */
			/*
			 * XXX Are there duplication payloads of either
			 * KEY_AUTH or KEY_ENCRYPT ?
			 */
			if (mhp->ext[ext->sadb_ext_type] != NULL) {
				ipseclog((LOG_DEBUG, "%s: duplicate ext_type "
					"%u\n", __func__, ext->sadb_ext_type));
				m_freem(m);
				PFKEYSTAT_INC(out_dupext);
				return EINVAL;
			}
			break;
		default:
			ipseclog((LOG_DEBUG, "%s: invalid ext_type %u\n",
				__func__, ext->sadb_ext_type));
			m_freem(m);
			PFKEYSTAT_INC(out_invexttype);
			return EINVAL;
		}

		extlen = PFKEY_UNUNIT64(ext->sadb_ext_len);

		if (key_validate_ext(ext, extlen)) {
			m_freem(m);
			PFKEYSTAT_INC(out_invlen);
			return EINVAL;
		}

		n = m_pulldown(m, off, extlen, &toff);
		if (!n) {
			/* m is already freed */
			return ENOBUFS;
		}
		ext = (struct sadb_ext *)(mtod(n, caddr_t) + toff);

		mhp->ext[ext->sadb_ext_type] = ext;
		mhp->extoff[ext->sadb_ext_type] = off;
		mhp->extlen[ext->sadb_ext_type] = extlen;
	}

	if (off != end) {
		m_freem(m);
		PFKEYSTAT_INC(out_invlen);
		return EINVAL;
	}

	return 0;
}

static int
key_validate_ext(const struct sadb_ext *ext, int len)
{
	const struct sockaddr *sa;
	enum { NONE, ADDR } checktype = NONE;
	int baselen = 0;
	const int sal = offsetof(struct sockaddr, sa_len) + sizeof(sa->sa_len);

	if (len != PFKEY_UNUNIT64(ext->sadb_ext_len))
		return EINVAL;

	/* if it does not match minimum/maximum length, bail */
	if (ext->sadb_ext_type >= nitems(minsize) ||
	    ext->sadb_ext_type >= nitems(maxsize))
		return EINVAL;
	if (!minsize[ext->sadb_ext_type] || len < minsize[ext->sadb_ext_type])
		return EINVAL;
	if (maxsize[ext->sadb_ext_type] && len > maxsize[ext->sadb_ext_type])
		return EINVAL;

	/* more checks based on sadb_ext_type XXX need more */
	switch (ext->sadb_ext_type) {
	case SADB_EXT_ADDRESS_SRC:
	case SADB_EXT_ADDRESS_DST:
	case SADB_EXT_ADDRESS_PROXY:
	case SADB_X_EXT_NAT_T_OAI:
	case SADB_X_EXT_NAT_T_OAR:
	case SADB_X_EXT_NEW_ADDRESS_SRC:
	case SADB_X_EXT_NEW_ADDRESS_DST:
		baselen = PFKEY_ALIGN8(sizeof(struct sadb_address));
		checktype = ADDR;
		break;
	case SADB_EXT_IDENTITY_SRC:
	case SADB_EXT_IDENTITY_DST:
		if (((const struct sadb_ident *)ext)->sadb_ident_type ==
		    SADB_X_IDENTTYPE_ADDR) {
			baselen = PFKEY_ALIGN8(sizeof(struct sadb_ident));
			checktype = ADDR;
		} else
			checktype = NONE;
		break;
	default:
		checktype = NONE;
		break;
	}

	switch (checktype) {
	case NONE:
		break;
	case ADDR:
		sa = (const struct sockaddr *)(((const u_int8_t*)ext)+baselen);
		if (len < baselen + sal)
			return EINVAL;
		if (baselen + PFKEY_ALIGN8(sa->sa_len) != len)
			return EINVAL;
		break;
	}

	return 0;
}

void
spdcache_init(void)
{
	int i;

	TUNABLE_INT_FETCH("net.key.spdcache.maxentries",
	    &V_key_spdcache_maxentries);
	TUNABLE_INT_FETCH("net.key.spdcache.threshold",
	    &V_key_spdcache_threshold);

	if (V_key_spdcache_maxentries) {
		V_key_spdcache_maxentries = MAX(V_key_spdcache_maxentries,
		    SPDCACHE_MAX_ENTRIES_PER_HASH);
		V_spdcachehashtbl = hashinit(V_key_spdcache_maxentries /
		    SPDCACHE_MAX_ENTRIES_PER_HASH,
		    M_IPSEC_SPDCACHE, &V_spdcachehash_mask);
		V_key_spdcache_maxentries = (V_spdcachehash_mask + 1)
		    * SPDCACHE_MAX_ENTRIES_PER_HASH;

		V_spdcache_lock = malloc(sizeof(struct mtx) *
		    (V_spdcachehash_mask + 1),
		    M_IPSEC_SPDCACHE, M_WAITOK|M_ZERO);

		for (i = 0; i < V_spdcachehash_mask + 1; ++i)
			SPDCACHE_LOCK_INIT(i);
	}
}

struct spdcache_entry *
spdcache_entry_alloc(const struct secpolicyindex *spidx, struct secpolicy *sp)
{
	struct spdcache_entry *entry;

	entry = malloc(sizeof(struct spdcache_entry),
		    M_IPSEC_SPDCACHE, M_NOWAIT|M_ZERO);
	if (entry == NULL)
		return NULL;

	if (sp != NULL)
		SP_ADDREF(sp);

	entry->spidx = *spidx;
	entry->sp = sp;

	return (entry);
}

void
spdcache_entry_free(struct spdcache_entry *entry)
{

	if (entry->sp != NULL)
		key_freesp(&entry->sp);
	free(entry, M_IPSEC_SPDCACHE);
}

void
spdcache_clear(void)
{
	struct spdcache_entry *entry;
	int i;

	for (i = 0; i < V_spdcachehash_mask + 1; ++i) {
		SPDCACHE_LOCK(i);
		while (!LIST_EMPTY(&V_spdcachehashtbl[i])) {
			entry = LIST_FIRST(&V_spdcachehashtbl[i]);
			LIST_REMOVE(entry, chain);
			spdcache_entry_free(entry);
		}
		SPDCACHE_UNLOCK(i);
	}
}

#ifdef VIMAGE
void
spdcache_destroy(void)
{
	int i;

	if (SPDCACHE_ENABLED()) {
		spdcache_clear();
		hashdestroy(V_spdcachehashtbl, M_IPSEC_SPDCACHE, V_spdcachehash_mask);

		for (i = 0; i < V_spdcachehash_mask + 1; ++i)
			SPDCACHE_LOCK_DESTROY(i);

		free(V_spdcache_lock, M_IPSEC_SPDCACHE);
	}
}
#endif
void
key_init(void)
{
	int i;

	for (i = 0; i < IPSEC_DIR_MAX; i++) {
		TAILQ_INIT(&V_sptree[i]);
		TAILQ_INIT(&V_sptree_ifnet[i]);
	}

	V_key_lft_zone = uma_zcreate("IPsec SA lft_c",
	    sizeof(uint64_t) * 2, NULL, NULL, NULL, NULL,
	    UMA_ALIGN_PTR, UMA_ZONE_PCPU);

	TAILQ_INIT(&V_sahtree);
	V_sphashtbl = hashinit(SPHASH_NHASH, M_IPSEC_SP, &V_sphash_mask);
	V_savhashtbl = hashinit(SAVHASH_NHASH, M_IPSEC_SA, &V_savhash_mask);
	V_sahaddrhashtbl = hashinit(SAHHASH_NHASH, M_IPSEC_SAH,
	    &V_sahaddrhash_mask);
	V_acqaddrhashtbl = hashinit(ACQHASH_NHASH, M_IPSEC_SAQ,
	    &V_acqaddrhash_mask);
	V_acqseqhashtbl = hashinit(ACQHASH_NHASH, M_IPSEC_SAQ,
	    &V_acqseqhash_mask);

	spdcache_init();

	for (i = 0; i <= SADB_SATYPE_MAX; i++)
		LIST_INIT(&V_regtree[i]);

	LIST_INIT(&V_acqtree);
	LIST_INIT(&V_spacqtree);

	if (!IS_DEFAULT_VNET(curvnet))
		return;

	SPTREE_LOCK_INIT();
	REGTREE_LOCK_INIT();
	SAHTREE_LOCK_INIT();
	ACQ_LOCK_INIT();
	SPACQ_LOCK_INIT();

#ifndef IPSEC_DEBUG2
	callout_init(&key_timer, 1);
	callout_reset(&key_timer, hz, key_timehandler, NULL);
#endif /*IPSEC_DEBUG2*/

	/* initialize key statistics */
	keystat.getspi_count = 1;

	if (bootverbose)
		printf("IPsec: Initialized Security Association Processing.\n");
}

#ifdef VIMAGE
void
key_destroy(void)
{
	struct secashead_queue sahdrainq;
	struct secpolicy_queue drainq;
	struct secpolicy *sp, *nextsp;
	struct secacq *acq, *nextacq;
	struct secspacq *spacq, *nextspacq;
	struct secashead *sah;
	struct secasvar *sav;
	struct secreg *reg;
	int i;

	/*
	 * XXX: can we just call free() for each object without
	 * walking through safe way with releasing references?
	 */
	TAILQ_INIT(&drainq);
	SPTREE_WLOCK();
	for (i = 0; i < IPSEC_DIR_MAX; i++) {
		TAILQ_CONCAT(&drainq, &V_sptree[i], chain);
		TAILQ_CONCAT(&drainq, &V_sptree_ifnet[i], chain);
	}
	for (i = 0; i < V_sphash_mask + 1; i++)
		LIST_INIT(&V_sphashtbl[i]);
	SPTREE_WUNLOCK();
	spdcache_destroy();

	sp = TAILQ_FIRST(&drainq);
	while (sp != NULL) {
		nextsp = TAILQ_NEXT(sp, chain);
		key_freesp(&sp);
		sp = nextsp;
	}

	TAILQ_INIT(&sahdrainq);
	SAHTREE_WLOCK();
	TAILQ_CONCAT(&sahdrainq, &V_sahtree, chain);
	for (i = 0; i < V_savhash_mask + 1; i++)
		LIST_INIT(&V_savhashtbl[i]);
	for (i = 0; i < V_sahaddrhash_mask + 1; i++)
		LIST_INIT(&V_sahaddrhashtbl[i]);
	TAILQ_FOREACH(sah, &sahdrainq, chain) {
		sah->state = SADB_SASTATE_DEAD;
		TAILQ_FOREACH(sav, &sah->savtree_larval, chain) {
			sav->state = SADB_SASTATE_DEAD;
		}
		TAILQ_FOREACH(sav, &sah->savtree_alive, chain) {
			sav->state = SADB_SASTATE_DEAD;
		}
	}
	SAHTREE_WUNLOCK();

	key_freesah_flushed(&sahdrainq);
	hashdestroy(V_sphashtbl, M_IPSEC_SP, V_sphash_mask);
	hashdestroy(V_savhashtbl, M_IPSEC_SA, V_savhash_mask);
	hashdestroy(V_sahaddrhashtbl, M_IPSEC_SAH, V_sahaddrhash_mask);

	REGTREE_LOCK();
	for (i = 0; i <= SADB_SATYPE_MAX; i++) {
		LIST_FOREACH(reg, &V_regtree[i], chain) {
			if (__LIST_CHAINED(reg)) {
				LIST_REMOVE(reg, chain);
				free(reg, M_IPSEC_SAR);
				break;
			}
		}
	}
	REGTREE_UNLOCK();

	ACQ_LOCK();
	acq = LIST_FIRST(&V_acqtree);
	while (acq != NULL) {
		nextacq = LIST_NEXT(acq, chain);
		LIST_REMOVE(acq, chain);
		free(acq, M_IPSEC_SAQ);
		acq = nextacq;
	}
	for (i = 0; i < V_acqaddrhash_mask + 1; i++)
		LIST_INIT(&V_acqaddrhashtbl[i]);
	for (i = 0; i < V_acqseqhash_mask + 1; i++)
		LIST_INIT(&V_acqseqhashtbl[i]);
	ACQ_UNLOCK();

	SPACQ_LOCK();
	for (spacq = LIST_FIRST(&V_spacqtree); spacq != NULL;
	    spacq = nextspacq) {
		nextspacq = LIST_NEXT(spacq, chain);
		if (__LIST_CHAINED(spacq)) {
			LIST_REMOVE(spacq, chain);
			free(spacq, M_IPSEC_SAQ);
		}
	}
	SPACQ_UNLOCK();
	hashdestroy(V_acqaddrhashtbl, M_IPSEC_SAQ, V_acqaddrhash_mask);
	hashdestroy(V_acqseqhashtbl, M_IPSEC_SAQ, V_acqseqhash_mask);
	uma_zdestroy(V_key_lft_zone);

	if (!IS_DEFAULT_VNET(curvnet))
		return;
#ifndef IPSEC_DEBUG2
	callout_drain(&key_timer);
#endif
	SPTREE_LOCK_DESTROY();
	REGTREE_LOCK_DESTROY();
	SAHTREE_LOCK_DESTROY();
	ACQ_LOCK_DESTROY();
	SPACQ_LOCK_DESTROY();
}
#endif

/* record data transfer on SA, and update timestamps */
void
key_sa_recordxfer(struct secasvar *sav, struct mbuf *m)
{
	IPSEC_ASSERT(sav != NULL, ("Null secasvar"));
	IPSEC_ASSERT(m != NULL, ("Null mbuf"));

	/*
	 * XXX Currently, there is a difference of bytes size
	 * between inbound and outbound processing.
	 */
	counter_u64_add(sav->lft_c_bytes, m->m_pkthdr.len);

	/*
	 * We use the number of packets as the unit of
	 * allocations.  We increment the variable
	 * whenever {esp,ah}_{in,out}put is called.
	 */
	counter_u64_add(sav->lft_c_allocations, 1);

	/*
	 * NOTE: We record CURRENT usetime by using wall clock,
	 * in seconds.  HARD and SOFT lifetime are measured by the time
	 * difference (again in seconds) from usetime.
	 *
	 *	usetime
	 *	v     expire   expire
	 * -----+-----+--------+---> t
	 *	<--------------> HARD
	 *	<-----> SOFT
	 */
	if (sav->firstused == 0)
		sav->firstused = time_second;
}

/*
 * Take one of the kernel's security keys and convert it into a PF_KEY
 * structure within an mbuf, suitable for sending up to a waiting
 * application in user land.
 * 
 * IN: 
 *    src: A pointer to a kernel security key.
 *    exttype: Which type of key this is. Refer to the PF_KEY data structures.
 * OUT:
 *    a valid mbuf or NULL indicating an error
 *
 */

static struct mbuf *
key_setkey(struct seckey *src, uint16_t exttype) 
{
	struct mbuf *m;
	struct sadb_key *p;
	int len;

	if (src == NULL)
		return NULL;

	len = PFKEY_ALIGN8(sizeof(struct sadb_key) + _KEYLEN(src));
	m = m_get2(len, M_NOWAIT, MT_DATA, 0);
	if (m == NULL)
		return NULL;
	m_align(m, len);
	m->m_len = len;
	p = mtod(m, struct sadb_key *);
	bzero(p, len);
	p->sadb_key_len = PFKEY_UNIT64(len);
	p->sadb_key_exttype = exttype;
	p->sadb_key_bits = src->bits;
	bcopy(src->key_data, _KEYBUF(p), _KEYLEN(src));

	return m;
}

/*
 * Take one of the kernel's lifetime data structures and convert it
 * into a PF_KEY structure within an mbuf, suitable for sending up to
 * a waiting application in user land.
 * 
 * IN: 
 *    src: A pointer to a kernel lifetime structure.
 *    exttype: Which type of lifetime this is. Refer to the PF_KEY 
 *             data structures for more information.
 * OUT:
 *    a valid mbuf or NULL indicating an error
 *
 */

static struct mbuf *
key_setlifetime(struct seclifetime *src, uint16_t exttype)
{
	struct mbuf *m = NULL;
	struct sadb_lifetime *p;
	int len = PFKEY_ALIGN8(sizeof(struct sadb_lifetime));

	if (src == NULL)
		return NULL;

	m = m_get2(len, M_NOWAIT, MT_DATA, 0);
	if (m == NULL)
		return m;
	m_align(m, len);
	m->m_len = len;
	p = mtod(m, struct sadb_lifetime *);

	bzero(p, len);
	p->sadb_lifetime_len = PFKEY_UNIT64(len);
	p->sadb_lifetime_exttype = exttype;
	p->sadb_lifetime_allocations = src->allocations;
	p->sadb_lifetime_bytes = src->bytes;
	p->sadb_lifetime_addtime = src->addtime;
	p->sadb_lifetime_usetime = src->usetime;
	
	return m;

}

const struct enc_xform *
enc_algorithm_lookup(int alg)
{
	int i;

	for (i = 0; i < nitems(supported_ealgs); i++)
		if (alg == supported_ealgs[i].sadb_alg)
			return (supported_ealgs[i].xform);
	return (NULL);
}

const struct auth_hash *
auth_algorithm_lookup(int alg)
{
	int i;

	for (i = 0; i < nitems(supported_aalgs); i++)
		if (alg == supported_aalgs[i].sadb_alg)
			return (supported_aalgs[i].xform);
	return (NULL);
}

const struct comp_algo *
comp_algorithm_lookup(int alg)
{
	int i;

	for (i = 0; i < nitems(supported_calgs); i++)
		if (alg == supported_calgs[i].sadb_alg)
			return (supported_calgs[i].xform);
	return (NULL);
}

