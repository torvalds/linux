/*	$OpenBSD: ip_ipsp.c,v 1.281 2025/07/08 00:47:41 jsg Exp $	*/
/*
 * The authors of this code are John Ioannidis (ji@tla.org),
 * Angelos D. Keromytis (kermit@csd.uch.gr),
 * Niels Provos (provos@physnet.uni-hamburg.de) and
 * Niklas Hallqvist (niklas@appli.se).
 *
 * The original version of this code was written by John Ioannidis
 * for BSD/OS in Athens, Greece, in November 1995.
 *
 * Ported to OpenBSD and NetBSD, with additional transforms, in December 1996,
 * by Angelos D. Keromytis.
 *
 * Additional transforms and features in 1997 and 1998 by Angelos D. Keromytis
 * and Niels Provos.
 *
 * Additional features in 1999 by Angelos D. Keromytis and Niklas Hallqvist.
 *
 * Copyright (c) 1995, 1996, 1997, 1998, 1999 by John Ioannidis,
 * Angelos D. Keromytis and Niels Provos.
 * Copyright (c) 1999 Niklas Hallqvist.
 * Copyright (c) 2001, Angelos D. Keromytis.
 *
 * Permission to use, copy, and modify this software with or without fee
 * is hereby granted, provided that this entire notice is included in
 * all copies of any software which is or includes a copy or
 * modification of this software.
 * You may use this code under the GNU public license if you so wish. Please
 * contribute changes back to the authors under this freer than GPL license
 * so that we may further the use of strong encryption without limitations to
 * all.
 *
 * THIS SOFTWARE IS BEING PROVIDED "AS IS", WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTY. IN PARTICULAR, NONE OF THE AUTHORS MAKES ANY
 * REPRESENTATION OR WARRANTY OF ANY KIND CONCERNING THE
 * MERCHANTABILITY OF THIS SOFTWARE OR ITS FITNESS FOR ANY PARTICULAR
 * PURPOSE.
 */

#include "pf.h"
#include "pfsync.h"
#include "sec.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/timeout.h>
#include <sys/pool.h>
#include <sys/atomic.h>
#include <sys/mutex.h>

#include <crypto/siphash.h>

#include <net/if.h>

#include <netinet/in.h>
#include <netinet/ip_ipsp.h>

#if NPF > 0
#include <net/pfvar.h>
#endif

#if NPFSYNC > 0
#include <net/if_pfsync.h>
#endif

#if NSEC > 0
#include <net/if_sec.h>
#endif

#include <net/pfkeyv2.h>

#ifdef DDB
#include <ddb/db_output.h>
void tdb_hashstats(void);
#endif

#ifdef ENCDEBUG
#define DPRINTF(fmt, args...)						\
	do {								\
		if (atomic_load_int(&encdebug))				\
			printf("%s: " fmt "\n", __func__, ## args);	\
	} while (0)
#else
#define DPRINTF(fmt, args...)						\
	do { } while (0)
#endif

/*
 * Locks used to protect global data and struct members:
 *	D	tdb_sadb_mtx
 *	F	ipsec_flows_mtx             SA database global mutex
 */

struct mutex ipsec_flows_mtx = MUTEX_INITIALIZER(IPL_SOFTNET);

int		tdb_rehash(void);
void		tdb_timeout(void *);
void		tdb_firstuse(void *);
void		tdb_soft_timeout(void *);
void		tdb_soft_firstuse(void *);
int		tdb_hash(u_int32_t, union sockaddr_union *, u_int8_t);

int ipsec_in_use = 0;
u_int64_t ipsec_last_added = 0;
int ipsec_ids_idle = 100;		/* keep free ids for 100s */

struct pool tdb_pool;

/* Protected by the NET_LOCK(). */
u_int32_t ipsec_ids_next_flow = 1;		/* [F] may not be zero */
struct ipsec_ids_tree ipsec_ids_tree;		/* [F] */
struct ipsec_ids_flows ipsec_ids_flows;		/* [F] */
struct ipsec_policy_head ipsec_policy_head =
    TAILQ_HEAD_INITIALIZER(ipsec_policy_head);

void ipsp_ids_gc(void *);

LIST_HEAD(, ipsec_ids) ipsp_ids_gc_list =
    LIST_HEAD_INITIALIZER(ipsp_ids_gc_list);	/* [F] */
struct timeout ipsp_ids_gc_timeout =
    TIMEOUT_INITIALIZER_FLAGS(ipsp_ids_gc, NULL, KCLOCK_NONE,
    TIMEOUT_PROC | TIMEOUT_MPSAFE);

static inline int ipsp_ids_cmp(const struct ipsec_ids *,
    const struct ipsec_ids *);
static inline int ipsp_ids_flow_cmp(const struct ipsec_ids *,
    const struct ipsec_ids *);
RBT_PROTOTYPE(ipsec_ids_tree, ipsec_ids, id_node_flow, ipsp_ids_cmp);
RBT_PROTOTYPE(ipsec_ids_flows, ipsec_ids, id_node_id, ipsp_ids_flow_cmp);
RBT_GENERATE(ipsec_ids_tree, ipsec_ids, id_node_flow, ipsp_ids_cmp);
RBT_GENERATE(ipsec_ids_flows, ipsec_ids, id_node_id, ipsp_ids_flow_cmp);

/*
 * This is the proper place to define the various encapsulation transforms.
 */

const struct xformsw xformsw[] = {
#ifdef IPSEC
{
  .xf_type	= XF_IP4,
  .xf_flags	= 0,
  .xf_name	= "IPv4 Simple Encapsulation",
  .xf_attach	= ipe4_attach,
  .xf_init	= ipe4_init,
  .xf_zeroize	= ipe4_zeroize,
  .xf_input	= ipe4_input,
  .xf_output	= NULL,
},
{
  .xf_type	= XF_AH,
  .xf_flags	= XFT_AUTH,
  .xf_name	= "IPsec AH",
  .xf_attach	= ah_attach,
  .xf_init	= ah_init,
  .xf_zeroize	= ah_zeroize,
  .xf_input	= ah_input,
  .xf_output	= ah_output,
},
{
  .xf_type	= XF_ESP,
  .xf_flags	= XFT_CONF|XFT_AUTH,
  .xf_name	= "IPsec ESP",
  .xf_attach	= esp_attach,
  .xf_init	= esp_init,
  .xf_zeroize	= esp_zeroize,
  .xf_input	= esp_input,
  .xf_output	= esp_output,
},
{
  .xf_type	= XF_IPCOMP,
  .xf_flags	= XFT_COMP,
  .xf_name	= "IPcomp",
  .xf_attach	= ipcomp_attach,
  .xf_init	= ipcomp_init,
  .xf_zeroize	= ipcomp_zeroize,
  .xf_input	= ipcomp_input,
  .xf_output	= ipcomp_output,
},
#endif /* IPSEC */
#ifdef TCP_SIGNATURE
{
  .xf_type	= XF_TCPSIGNATURE,
  .xf_flags	= XFT_AUTH,
  .xf_name	= "TCP MD5 Signature Option, RFC 2385",
  .xf_attach	= tcp_signature_tdb_attach,
  .xf_init	= tcp_signature_tdb_init,
  .xf_zeroize	= tcp_signature_tdb_zeroize,
  .xf_input	= tcp_signature_tdb_input,
  .xf_output	= tcp_signature_tdb_output,
}
#endif /* TCP_SIGNATURE */
};

const struct xformsw *const xformswNXFORMSW = &xformsw[nitems(xformsw)];

#define	TDB_HASHSIZE_INIT	32

struct mutex tdb_sadb_mtx = MUTEX_INITIALIZER(IPL_SOFTNET);
static SIPHASH_KEY tdbkey;				/* [D] */
static struct tdb **tdbh;				/* [D] */
static struct tdb **tdbdst;				/* [D] */
static struct tdb **tdbsrc;				/* [D] */
static u_int tdb_hashmask = TDB_HASHSIZE_INIT - 1;	/* [D] */
static int tdb_count;					/* [D] */

void
ipsp_init(void)
{
	pool_init(&tdb_pool, sizeof(struct tdb), 0, IPL_SOFTNET, 0,
	    "tdb", NULL);

	arc4random_buf(&tdbkey, sizeof(tdbkey));
	tdbh = mallocarray(tdb_hashmask + 1, sizeof(struct tdb *), M_TDB,
	    M_WAITOK | M_ZERO);
	tdbdst = mallocarray(tdb_hashmask + 1, sizeof(struct tdb *), M_TDB,
	    M_WAITOK | M_ZERO);
	tdbsrc = mallocarray(tdb_hashmask + 1, sizeof(struct tdb *), M_TDB,
	    M_WAITOK | M_ZERO);
}

/*
 * Our hashing function needs to stir things with a non-zero random multiplier
 * so we cannot be DoS-attacked via choosing of the data to hash.
 */
int
tdb_hash(u_int32_t spi, union sockaddr_union *dst,
    u_int8_t proto)
{
	SIPHASH_CTX ctx;

	MUTEX_ASSERT_LOCKED(&tdb_sadb_mtx);

	SipHash24_Init(&ctx, &tdbkey);
	SipHash24_Update(&ctx, &spi, sizeof(spi));
	SipHash24_Update(&ctx, &proto, sizeof(proto));
	SipHash24_Update(&ctx, dst, dst->sa.sa_len);

	return (SipHash24_End(&ctx) & tdb_hashmask);
}

/*
 * Reserve an SPI; the SA is not valid yet though.  We use 0 as
 * an error return value.
 */
u_int32_t
reserve_spi(u_int rdomain, u_int32_t sspi, u_int32_t tspi,
    union sockaddr_union *src, union sockaddr_union *dst,
    u_int8_t sproto, int *errval)
{
	struct tdb *tdbp, *exists;
	u_int32_t spi;
	int nums;
#ifdef IPSEC
	int keep_invalid_local = atomic_load_int(&ipsec_keep_invalid);
#endif

	/* Don't accept ranges only encompassing reserved SPIs. */
	if (sproto != IPPROTO_IPCOMP &&
	    (tspi < sspi || tspi <= SPI_RESERVED_MAX)) {
		(*errval) = EINVAL;
		return 0;
	}
	if (sproto == IPPROTO_IPCOMP && (tspi < sspi ||
	    tspi <= CPI_RESERVED_MAX ||
	    tspi >= CPI_PRIVATE_MIN)) {
		(*errval) = EINVAL;
		return 0;
	}

	/* Limit the range to not include reserved areas. */
	if (sspi <= SPI_RESERVED_MAX)
		sspi = SPI_RESERVED_MAX + 1;

	/* For IPCOMP the CPI is only 16 bits long, what a good idea.... */

	if (sproto == IPPROTO_IPCOMP) {
		u_int32_t t;
		if (sspi >= 0x10000)
			sspi = 0xffff;
		if (tspi >= 0x10000)
			tspi = 0xffff;
		if (sspi > tspi) {
			t = sspi; sspi = tspi; tspi = t;
		}
	}

	if (sspi == tspi)   /* Asking for a specific SPI. */
		nums = 1;
	else
		nums = 100;  /* Arbitrarily chosen */

	/* allocate ahead of time to avoid potential sleeping race in loop */
	tdbp = tdb_alloc(rdomain);

	while (nums--) {
		if (sspi == tspi)  /* Specific SPI asked. */
			spi = tspi;
		else    /* Range specified */
			spi = sspi + arc4random_uniform(tspi - sspi);

		/* Don't allocate reserved SPIs.  */
		if (spi >= SPI_RESERVED_MIN && spi <= SPI_RESERVED_MAX)
			continue;
		else
			spi = htonl(spi);

		/* Check whether we're using this SPI already. */
		exists = gettdb(rdomain, spi, dst, sproto);
		if (exists != NULL) {
			tdb_unref(exists);
			continue;
		}

		tdbp->tdb_spi = spi;
		memcpy(&tdbp->tdb_dst.sa, &dst->sa, dst->sa.sa_len);
		memcpy(&tdbp->tdb_src.sa, &src->sa, src->sa.sa_len);
		tdbp->tdb_sproto = sproto;
		tdbp->tdb_flags |= TDBF_INVALID; /* Mark SA invalid for now. */
		tdbp->tdb_satype = SADB_SATYPE_UNSPEC;
		puttdb(tdbp);

#ifdef IPSEC
		/* Setup a "silent" expiration (since TDBF_INVALID's set). */
		if (keep_invalid_local > 0) {
			mtx_enter(&tdbp->tdb_mtx);
			tdbp->tdb_flags |= TDBF_TIMER;
			tdbp->tdb_exp_timeout = keep_invalid_local;
			if (timeout_add_sec(&tdbp->tdb_timer_tmo,
			    keep_invalid_local))
				tdb_ref(tdbp);
			mtx_leave(&tdbp->tdb_mtx);
		}
#endif

		return spi;
	}

	(*errval) = EEXIST;
	tdb_unref(tdbp);
	return 0;
}

/*
 * An IPSP SAID is really the concatenation of the SPI found in the
 * packet, the destination address of the packet and the IPsec protocol.
 * When we receive an IPSP packet, we need to look up its tunnel descriptor
 * block, based on the SPI in the packet and the destination address (which
 * is really one of our addresses if we received the packet!
 */
struct tdb *
gettdb_dir(u_int rdomain, u_int32_t spi, union sockaddr_union *dst,
    u_int8_t proto, int reverse)
{
	u_int32_t hashval;
	struct tdb *tdbp;

	NET_ASSERT_LOCKED();

	mtx_enter(&tdb_sadb_mtx);
	hashval = tdb_hash(spi, dst, proto);

	for (tdbp = tdbh[hashval]; tdbp != NULL; tdbp = tdbp->tdb_hnext)
		if ((tdbp->tdb_spi == spi) && (tdbp->tdb_sproto == proto) &&
		    ((!reverse && tdbp->tdb_rdomain == rdomain) ||
		    (reverse && tdbp->tdb_rdomain_post == rdomain)) &&
		    !memcmp(&tdbp->tdb_dst, dst, dst->sa.sa_len))
			break;

	tdb_ref(tdbp);
	mtx_leave(&tdb_sadb_mtx);
	return tdbp;
}

/*
 * Same as gettdb() but compare SRC as well, so we
 * use the tdbsrc[] hash table.  Setting spi to 0
 * matches all SPIs.
 */
struct tdb *
gettdbbysrcdst_dir(u_int rdomain, u_int32_t spi, union sockaddr_union *src,
    union sockaddr_union *dst, u_int8_t proto, int reverse)
{
	u_int32_t hashval;
	struct tdb *tdbp;
	union sockaddr_union su_null;

	mtx_enter(&tdb_sadb_mtx);
	hashval = tdb_hash(0, src, proto);

	for (tdbp = tdbsrc[hashval]; tdbp != NULL; tdbp = tdbp->tdb_snext) {
		if (tdbp->tdb_sproto == proto &&
		    (spi == 0 || tdbp->tdb_spi == spi) &&
		    ((!reverse && tdbp->tdb_rdomain == rdomain) ||
		    (reverse && tdbp->tdb_rdomain_post == rdomain)) &&
		    ((tdbp->tdb_flags & TDBF_INVALID) == 0) &&
		    (tdbp->tdb_dst.sa.sa_family == AF_UNSPEC ||
		    !memcmp(&tdbp->tdb_dst, dst, dst->sa.sa_len)) &&
		    !memcmp(&tdbp->tdb_src, src, src->sa.sa_len))
			break;
	}
	if (tdbp != NULL) {
		tdb_ref(tdbp);
		mtx_leave(&tdb_sadb_mtx);
		return tdbp;
	}

	memset(&su_null, 0, sizeof(su_null));
	su_null.sa.sa_len = sizeof(struct sockaddr);
	hashval = tdb_hash(0, &su_null, proto);

	for (tdbp = tdbsrc[hashval]; tdbp != NULL; tdbp = tdbp->tdb_snext) {
		if (tdbp->tdb_sproto == proto &&
		    (spi == 0 || tdbp->tdb_spi == spi) &&
		    ((!reverse && tdbp->tdb_rdomain == rdomain) ||
		    (reverse && tdbp->tdb_rdomain_post == rdomain)) &&
		    ((tdbp->tdb_flags & TDBF_INVALID) == 0) &&
		    (tdbp->tdb_dst.sa.sa_family == AF_UNSPEC ||
		    !memcmp(&tdbp->tdb_dst, dst, dst->sa.sa_len)) &&
		    tdbp->tdb_src.sa.sa_family == AF_UNSPEC)
			break;
	}
	tdb_ref(tdbp);
	mtx_leave(&tdb_sadb_mtx);
	return tdbp;
}

/*
 * Check that IDs match. Return true if so. The t* range of
 * arguments contains information from TDBs; the p* range of
 * arguments contains information from policies or already
 * established TDBs.
 */
int
ipsp_aux_match(struct tdb *tdb,
    struct ipsec_ids *ids,
    struct sockaddr_encap *pfilter,
    struct sockaddr_encap *pfiltermask)
{
	if (ids != NULL)
		if (tdb->tdb_ids == NULL ||
		    !ipsp_ids_match(tdb->tdb_ids, ids))
			return 0;

	/* Check for filter matches. */
	if (pfilter != NULL && pfiltermask != NULL &&
	    tdb->tdb_filter.sen_type) {
		/*
		 * XXX We should really be doing a subnet-check (see
		 * whether the TDB-associated filter is a subset
		 * of the policy's. For now, an exact match will solve
		 * most problems (all this will do is make every
		 * policy get its own SAs).
		 */
		if (memcmp(&tdb->tdb_filter, pfilter,
		    sizeof(struct sockaddr_encap)) ||
		    memcmp(&tdb->tdb_filtermask, pfiltermask,
		    sizeof(struct sockaddr_encap)))
			return 0;
	}

	return 1;
}

/*
 * Get an SA given the remote address, the security protocol type, and
 * the desired IDs.
 */
struct tdb *
gettdbbydst(u_int rdomain, union sockaddr_union *dst, u_int8_t sproto,
    struct ipsec_ids *ids,
    struct sockaddr_encap *filter, struct sockaddr_encap *filtermask)
{
	u_int32_t hashval;
	struct tdb *tdbp;

	mtx_enter(&tdb_sadb_mtx);
	hashval = tdb_hash(0, dst, sproto);

	for (tdbp = tdbdst[hashval]; tdbp != NULL; tdbp = tdbp->tdb_dnext)
		if ((tdbp->tdb_sproto == sproto) &&
		    (tdbp->tdb_rdomain == rdomain) &&
		    ((tdbp->tdb_flags & TDBF_INVALID) == 0) &&
		    (!memcmp(&tdbp->tdb_dst, dst, dst->sa.sa_len))) {
			/* Check whether IDs match */
			if (!ipsp_aux_match(tdbp, ids, filter, filtermask))
				continue;
			break;
		}

	tdb_ref(tdbp);
	mtx_leave(&tdb_sadb_mtx);
	return tdbp;
}

/*
 * Get an SA given the source address, the security protocol type, and
 * the desired IDs.
 */
struct tdb *
gettdbbysrc(u_int rdomain, union sockaddr_union *src, u_int8_t sproto,
    struct ipsec_ids *ids,
    struct sockaddr_encap *filter, struct sockaddr_encap *filtermask)
{
	u_int32_t hashval;
	struct tdb *tdbp;

	mtx_enter(&tdb_sadb_mtx);
	hashval = tdb_hash(0, src, sproto);

	for (tdbp = tdbsrc[hashval]; tdbp != NULL; tdbp = tdbp->tdb_snext) {
		if ((tdbp->tdb_sproto == sproto) &&
		    (tdbp->tdb_rdomain == rdomain) &&
		    ((tdbp->tdb_flags & TDBF_INVALID) == 0) &&
		    (!memcmp(&tdbp->tdb_src, src, src->sa.sa_len))) {
			/* Check whether IDs match */
			if (!ipsp_aux_match(tdbp, ids, filter, filtermask))
				continue;
			break;
		}
	}
	tdb_ref(tdbp);
	mtx_leave(&tdb_sadb_mtx);
	return tdbp;
}

#ifdef DDB

#define NBUCKETS 16
void
tdb_hashstats(void)
{
	int i, cnt, buckets[NBUCKETS];
	struct tdb *tdbp;

	if (tdbh == NULL) {
		db_printf("no tdb hash table\n");
		return;
	}

	memset(buckets, 0, sizeof(buckets));
	for (i = 0; i <= tdb_hashmask; i++) {
		cnt = 0;
		for (tdbp = tdbh[i]; cnt < NBUCKETS - 1 && tdbp != NULL;
		    tdbp = tdbp->tdb_hnext)
			cnt++;
		buckets[cnt]++;
	}

	db_printf("tdb cnt\t\tbucket cnt\n");
	for (i = 0; i < NBUCKETS; i++)
		if (buckets[i] > 0)
			db_printf("%d%s\t\t%d\n", i, i == NBUCKETS - 1 ?
			    "+" : "", buckets[i]);
}

#define DUMP(m, f) pr("%18s: " f "\n", #m, tdb->tdb_##m)
void
tdb_printit(void *addr, int full, int (*pr)(const char *, ...))
{
	struct tdb *tdb = addr;
	char buf[INET6_ADDRSTRLEN];

	if (full) {
		pr("tdb at %p\n", tdb);
		DUMP(hnext, "%p");
		DUMP(dnext, "%p");
		DUMP(snext, "%p");
		DUMP(inext, "%p");
		DUMP(onext, "%p");
		DUMP(xform, "%p");
		pr("%18s: %d\n", "refcnt", tdb->tdb_refcnt.r_refs);
		DUMP(encalgxform, "%p");
		DUMP(authalgxform, "%p");
		DUMP(compalgxform, "%p");
		pr("%18s: %b\n", "flags", tdb->tdb_flags, TDBF_BITS);
		/* tdb_XXX_tmo */
		DUMP(seq, "%d");
		DUMP(exp_allocations, "%d");
		DUMP(soft_allocations, "%d");
		DUMP(cur_allocations, "%d");
		DUMP(exp_bytes, "%lld");
		DUMP(soft_bytes, "%lld");
		DUMP(cur_bytes, "%lld");
		DUMP(exp_timeout, "%lld");
		DUMP(soft_timeout, "%lld");
		DUMP(established, "%lld");
		DUMP(first_use, "%lld");
		DUMP(soft_first_use, "%lld");
		DUMP(exp_first_use, "%lld");
		DUMP(last_used, "%lld");
		DUMP(last_marked, "%lld");
		/* tdb_data */
		DUMP(cryptoid, "%lld");
		pr("%18s: %08x\n", "tdb_spi", ntohl(tdb->tdb_spi));
		DUMP(amxkeylen, "%d");
		DUMP(emxkeylen, "%d");
		DUMP(ivlen, "%d");
		DUMP(sproto, "%d");
		DUMP(wnd, "%d");
		DUMP(satype, "%d");
		DUMP(updates, "%d");
		pr("%18s: %s\n", "dst",
		    ipsp_address(&tdb->tdb_dst, buf, sizeof(buf)));
		pr("%18s: %s\n", "src",
		    ipsp_address(&tdb->tdb_src, buf, sizeof(buf)));
		DUMP(amxkey, "%p");
		DUMP(emxkey, "%p");
		DUMP(rpl, "%lld");
		/* tdb_seen */
		/* tdb_iv */
		DUMP(ids, "%p");
		DUMP(ids_swapped, "%d");
		DUMP(mtu, "%d");
		DUMP(mtutimeout, "%lld");
		pr("%18s: %d\n", "udpencap_port",
		    ntohs(tdb->tdb_udpencap_port));
		DUMP(tag, "%d");
		DUMP(tap, "%d");
		DUMP(rdomain, "%d");
		DUMP(rdomain_post, "%d");
		/* tdb_filter */
		/* tdb_filtermask */
		/* tdb_policy_head */
		/* tdb_sync_entry */
	} else {
		pr("%p:", tdb);
		pr(" %08x", ntohl(tdb->tdb_spi));
		pr(" %s", ipsp_address(&tdb->tdb_src, buf, sizeof(buf)));
		pr("->%s", ipsp_address(&tdb->tdb_dst, buf, sizeof(buf)));
		pr(":%d", tdb->tdb_sproto);
		pr(" #%d", tdb->tdb_refcnt.r_refs);
		pr(" %08x\n", tdb->tdb_flags);
	}
}
#undef DUMP
#endif	/* DDB */

int
tdb_walk(u_int rdomain, int (*walker)(struct tdb *, void *, int), void *arg)
{
	SIMPLEQ_HEAD(, tdb) tdblist;
	struct tdb *tdbp;
	int i, rval;

	/*
	 * The walker may sleep.  So we cannot hold the tdb_sadb_mtx while
	 * traversing the tdb_hnext list.  Create a new tdb_walk list with
	 * exclusive netlock protection.
	 */
	NET_ASSERT_LOCKED_EXCLUSIVE();
	SIMPLEQ_INIT(&tdblist);

	mtx_enter(&tdb_sadb_mtx);
	for (i = 0; i <= tdb_hashmask; i++) {
		for (tdbp = tdbh[i]; tdbp != NULL; tdbp = tdbp->tdb_hnext) {
			if (rdomain != tdbp->tdb_rdomain)
				continue;
			tdb_ref(tdbp);
			SIMPLEQ_INSERT_TAIL(&tdblist, tdbp, tdb_walk);
		}
	}
	mtx_leave(&tdb_sadb_mtx);

	rval = 0;
	while ((tdbp = SIMPLEQ_FIRST(&tdblist)) != NULL) {
		SIMPLEQ_REMOVE_HEAD(&tdblist, tdb_walk);
		if (rval == 0)
			rval = walker(tdbp, arg, SIMPLEQ_EMPTY(&tdblist));
		tdb_unref(tdbp);
	}

	return rval;
}

void
tdb_timeout(void *v)
{
	struct tdb *tdb = v;

	NET_LOCK();
	if (tdb->tdb_flags & TDBF_TIMER) {
		/* If it's an "invalid" TDB do a silent expiration. */
		if (!(tdb->tdb_flags & TDBF_INVALID)) {
#ifdef IPSEC
			ipsecstat_inc(ipsec_exctdb);
#endif /* IPSEC */
			pfkeyv2_expire(tdb, SADB_EXT_LIFETIME_HARD);
		}
		tdb_delete(tdb);
	}
	/* decrement refcount of the timeout argument */
	tdb_unref(tdb);
	NET_UNLOCK();
}

void
tdb_firstuse(void *v)
{
	struct tdb *tdb = v;

	NET_LOCK();
	if (tdb->tdb_flags & TDBF_SOFT_FIRSTUSE) {
		/* If the TDB hasn't been used, don't renew it. */
		if (tdb->tdb_first_use != 0) {
#ifdef IPSEC
			ipsecstat_inc(ipsec_exctdb);
#endif /* IPSEC */
			pfkeyv2_expire(tdb, SADB_EXT_LIFETIME_HARD);
		}
		tdb_delete(tdb);
	}
	/* decrement refcount of the timeout argument */
	tdb_unref(tdb);
	NET_UNLOCK();
}

void
tdb_addtimeouts(struct tdb *tdbp)
{
	mtx_enter(&tdbp->tdb_mtx);
	if (tdbp->tdb_flags & TDBF_TIMER) {
		if (timeout_add_sec(&tdbp->tdb_timer_tmo,
		    tdbp->tdb_exp_timeout))
			tdb_ref(tdbp);
	}
	if (tdbp->tdb_flags & TDBF_SOFT_TIMER) {
		if (timeout_add_sec(&tdbp->tdb_stimer_tmo,
		    tdbp->tdb_soft_timeout))
			tdb_ref(tdbp);
	}
	mtx_leave(&tdbp->tdb_mtx);
}

void
tdb_soft_timeout(void *v)
{
	struct tdb *tdb = v;

	NET_LOCK();
	mtx_enter(&tdb->tdb_mtx);
	if (tdb->tdb_flags & TDBF_SOFT_TIMER) {
		tdb->tdb_flags &= ~TDBF_SOFT_TIMER;
		mtx_leave(&tdb->tdb_mtx);
		/* Soft expirations. */
		pfkeyv2_expire(tdb, SADB_EXT_LIFETIME_SOFT);
	} else
		mtx_leave(&tdb->tdb_mtx);
	/* decrement refcount of the timeout argument */
	tdb_unref(tdb);
	NET_UNLOCK();
}

void
tdb_soft_firstuse(void *v)
{
	struct tdb *tdb = v;

	NET_LOCK();
	mtx_enter(&tdb->tdb_mtx);
	if (tdb->tdb_flags & TDBF_SOFT_FIRSTUSE) {
		tdb->tdb_flags &= ~TDBF_SOFT_FIRSTUSE;
		mtx_leave(&tdb->tdb_mtx);
		/* If the TDB hasn't been used, don't renew it. */
		if (tdb->tdb_first_use != 0)
			pfkeyv2_expire(tdb, SADB_EXT_LIFETIME_SOFT);
	} else
		mtx_leave(&tdb->tdb_mtx);
	/* decrement refcount of the timeout argument */
	tdb_unref(tdb);
	NET_UNLOCK();
}

int
tdb_rehash(void)
{
	struct tdb **new_tdbh, **new_tdbdst, **new_srcaddr, *tdbp, *tdbnp;
	u_int i, old_hashmask;
	u_int32_t hashval;

	MUTEX_ASSERT_LOCKED(&tdb_sadb_mtx);

	old_hashmask = tdb_hashmask;
	tdb_hashmask = (tdb_hashmask << 1) | 1;

	arc4random_buf(&tdbkey, sizeof(tdbkey));
	new_tdbh = mallocarray(tdb_hashmask + 1, sizeof(struct tdb *), M_TDB,
	    M_NOWAIT | M_ZERO);
	new_tdbdst = mallocarray(tdb_hashmask + 1, sizeof(struct tdb *), M_TDB,
	    M_NOWAIT | M_ZERO);
	new_srcaddr = mallocarray(tdb_hashmask + 1, sizeof(struct tdb *), M_TDB,
	    M_NOWAIT | M_ZERO);
	if (new_tdbh == NULL ||
	    new_tdbdst == NULL ||
	    new_srcaddr == NULL) {
		free(new_tdbh, M_TDB, 0);
		free(new_tdbdst, M_TDB, 0);
		free(new_srcaddr, M_TDB, 0);
		return (ENOMEM);
	}

	for (i = 0; i <= old_hashmask; i++) {
		for (tdbp = tdbh[i]; tdbp != NULL; tdbp = tdbnp) {
			tdbnp = tdbp->tdb_hnext;
			hashval = tdb_hash(tdbp->tdb_spi, &tdbp->tdb_dst,
			    tdbp->tdb_sproto);
			tdbp->tdb_hnext = new_tdbh[hashval];
			new_tdbh[hashval] = tdbp;
		}

		for (tdbp = tdbdst[i]; tdbp != NULL; tdbp = tdbnp) {
			tdbnp = tdbp->tdb_dnext;
			hashval = tdb_hash(0, &tdbp->tdb_dst, tdbp->tdb_sproto);
			tdbp->tdb_dnext = new_tdbdst[hashval];
			new_tdbdst[hashval] = tdbp;
		}

		for (tdbp = tdbsrc[i]; tdbp != NULL; tdbp = tdbnp) {
			tdbnp = tdbp->tdb_snext;
			hashval = tdb_hash(0, &tdbp->tdb_src, tdbp->tdb_sproto);
			tdbp->tdb_snext = new_srcaddr[hashval];
			new_srcaddr[hashval] = tdbp;
		}
	}

	free(tdbh, M_TDB, 0);
	tdbh = new_tdbh;

	free(tdbdst, M_TDB, 0);
	tdbdst = new_tdbdst;

	free(tdbsrc, M_TDB, 0);
	tdbsrc = new_srcaddr;

	return 0;
}

/*
 * Add TDB in the hash table.
 */
void
puttdb(struct tdb *tdbp)
{
	mtx_enter(&tdb_sadb_mtx);
	puttdb_locked(tdbp);
	mtx_leave(&tdb_sadb_mtx);
}

void
puttdb_locked(struct tdb *tdbp)
{
	u_int32_t hashval;

	MUTEX_ASSERT_LOCKED(&tdb_sadb_mtx);

	hashval = tdb_hash(tdbp->tdb_spi, &tdbp->tdb_dst, tdbp->tdb_sproto);

	/*
	 * Rehash if this tdb would cause a bucket to have more than
	 * two items and if the number of tdbs exceed 10% of the
	 * bucket count.  This number is arbitrarily chosen and is
	 * just a measure to not keep rehashing when adding and
	 * removing tdbs which happens to always end up in the same
	 * bucket, which is not uncommon when doing manual keying.
	 */
	if (tdbh[hashval] != NULL && tdbh[hashval]->tdb_hnext != NULL &&
	    tdb_count * 10 > tdb_hashmask + 1) {
		if (tdb_rehash() == 0)
			hashval = tdb_hash(tdbp->tdb_spi, &tdbp->tdb_dst,
			    tdbp->tdb_sproto);
	}

	tdbp->tdb_hnext = tdbh[hashval];
	tdbh[hashval] = tdbp;

	tdb_count++;
#ifdef IPSEC
	if ((tdbp->tdb_flags & (TDBF_INVALID|TDBF_TUNNELING)) == TDBF_TUNNELING)
		ipsecstat_inc(ipsec_tunnels);
#endif /* IPSEC */

	ipsec_last_added = getuptime();

	if (ISSET(tdbp->tdb_flags, TDBF_IFACE)) {
#if NSEC > 0
		sec_tdb_insert(tdbp);
#endif
		return;
	}

	hashval = tdb_hash(0, &tdbp->tdb_dst, tdbp->tdb_sproto);
	tdbp->tdb_dnext = tdbdst[hashval];
	tdbdst[hashval] = tdbp;

	hashval = tdb_hash(0, &tdbp->tdb_src, tdbp->tdb_sproto);
	tdbp->tdb_snext = tdbsrc[hashval];
	tdbsrc[hashval] = tdbp;
}

void
tdb_unlink(struct tdb *tdbp)
{
	mtx_enter(&tdb_sadb_mtx);
	tdb_unlink_locked(tdbp);
	mtx_leave(&tdb_sadb_mtx);
}

void
tdb_unlink_locked(struct tdb *tdbp)
{
	struct tdb *tdbpp;
	u_int32_t hashval;

	MUTEX_ASSERT_LOCKED(&tdb_sadb_mtx);

	hashval = tdb_hash(tdbp->tdb_spi, &tdbp->tdb_dst, tdbp->tdb_sproto);

	if (tdbh[hashval] == tdbp) {
		tdbh[hashval] = tdbp->tdb_hnext;
	} else {
		for (tdbpp = tdbh[hashval]; tdbpp != NULL;
		    tdbpp = tdbpp->tdb_hnext) {
			if (tdbpp->tdb_hnext == tdbp) {
				tdbpp->tdb_hnext = tdbp->tdb_hnext;
				break;
			}
		}
	}

	tdbp->tdb_hnext = NULL;

	tdb_count--;
#ifdef IPSEC
	if ((tdbp->tdb_flags & (TDBF_INVALID|TDBF_TUNNELING)) ==
	    TDBF_TUNNELING) {
		ipsecstat_dec(ipsec_tunnels);
		ipsecstat_inc(ipsec_prevtunnels);
	}
#endif /* IPSEC */

	if (ISSET(tdbp->tdb_flags, TDBF_IFACE)) {
#if NSEC > 0
		sec_tdb_remove(tdbp);
#endif
		return;
	}

	hashval = tdb_hash(0, &tdbp->tdb_dst, tdbp->tdb_sproto);

	if (tdbdst[hashval] == tdbp) {
		tdbdst[hashval] = tdbp->tdb_dnext;
	} else {
		for (tdbpp = tdbdst[hashval]; tdbpp != NULL;
		    tdbpp = tdbpp->tdb_dnext) {
			if (tdbpp->tdb_dnext == tdbp) {
				tdbpp->tdb_dnext = tdbp->tdb_dnext;
				break;
			}
		}
	}

	tdbp->tdb_dnext = NULL;

	hashval = tdb_hash(0, &tdbp->tdb_src, tdbp->tdb_sproto);

	if (tdbsrc[hashval] == tdbp) {
		tdbsrc[hashval] = tdbp->tdb_snext;
	} else {
		for (tdbpp = tdbsrc[hashval]; tdbpp != NULL;
		    tdbpp = tdbpp->tdb_snext) {
			if (tdbpp->tdb_snext == tdbp) {
				tdbpp->tdb_snext = tdbp->tdb_snext;
				break;
			}
		}
	}

	tdbp->tdb_snext = NULL;
}

void
tdb_cleanspd(struct tdb *tdbp)
{
	struct ipsec_policy *ipo;

	mtx_enter(&ipo_tdb_mtx);
	while ((ipo = TAILQ_FIRST(&tdbp->tdb_policy_head)) != NULL) {
		TAILQ_REMOVE(&tdbp->tdb_policy_head, ipo, ipo_tdb_next);
		tdb_unref(ipo->ipo_tdb);
		ipo->ipo_tdb = NULL;
		ipo->ipo_last_searched = 0; /* Force a re-search. */
	}
	mtx_leave(&ipo_tdb_mtx);
}

void
tdb_unbundle(struct tdb *tdbp)
{
	if (tdbp->tdb_onext != NULL) {
		if (tdbp->tdb_onext->tdb_inext == tdbp) {
			tdb_unref(tdbp);	/* to us */
			tdbp->tdb_onext->tdb_inext = NULL;
		}
		tdb_unref(tdbp->tdb_onext);	/* to other */
		tdbp->tdb_onext = NULL;
	}
	if (tdbp->tdb_inext != NULL) {
		if (tdbp->tdb_inext->tdb_onext == tdbp) {
			tdb_unref(tdbp);	/* to us */
			tdbp->tdb_inext->tdb_onext = NULL;
		}
		tdb_unref(tdbp->tdb_inext);	/* to other */
		tdbp->tdb_inext = NULL;
	}
}

void
tdb_deltimeouts(struct tdb *tdbp)
{
	mtx_enter(&tdbp->tdb_mtx);
	tdbp->tdb_flags &= ~(TDBF_FIRSTUSE | TDBF_SOFT_FIRSTUSE | TDBF_TIMER |
	    TDBF_SOFT_TIMER);
	if (timeout_del(&tdbp->tdb_timer_tmo))
		tdb_unref(tdbp);
	if (timeout_del(&tdbp->tdb_first_tmo))
		tdb_unref(tdbp);
	if (timeout_del(&tdbp->tdb_stimer_tmo))
		tdb_unref(tdbp);
	if (timeout_del(&tdbp->tdb_sfirst_tmo))
		tdb_unref(tdbp);
	mtx_leave(&tdbp->tdb_mtx);
}

struct tdb *
tdb_ref(struct tdb *tdb)
{
	if (tdb == NULL)
		return NULL;
	refcnt_take(&tdb->tdb_refcnt);
	return tdb;
}

void
tdb_unref(struct tdb *tdb)
{
	if (tdb == NULL)
		return;
	if (refcnt_rele(&tdb->tdb_refcnt) == 0)
		return;
	tdb_free(tdb);
}

void
tdb_delete(struct tdb *tdbp)
{
	NET_ASSERT_LOCKED();

	mtx_enter(&tdbp->tdb_mtx);
	if (tdbp->tdb_flags & TDBF_DELETED) {
		mtx_leave(&tdbp->tdb_mtx);
		return;
	}
	tdbp->tdb_flags |= TDBF_DELETED;
	mtx_leave(&tdbp->tdb_mtx);
	tdb_unlink(tdbp);

	/* cleanup SPD references */
	tdb_cleanspd(tdbp);
	/* release tdb_onext/tdb_inext references */
	tdb_unbundle(tdbp);
	/* delete timeouts and release references */
	tdb_deltimeouts(tdbp);
	/* release the reference for tdb_unlink() */
	tdb_unref(tdbp);
}

/*
 * Allocate a TDB and initialize a few basic fields.
 */
struct tdb *
tdb_alloc(u_int rdomain)
{
	struct tdb *tdbp;

	tdbp = pool_get(&tdb_pool, PR_WAITOK | PR_ZERO);

	refcnt_init_trace(&tdbp->tdb_refcnt, DT_REFCNT_IDX_TDB);
	mtx_init(&tdbp->tdb_mtx, IPL_SOFTNET);
	TAILQ_INIT(&tdbp->tdb_policy_head);

	/* Record establishment time. */
	tdbp->tdb_established = gettime();

	/* Save routing domain */
	tdbp->tdb_rdomain = rdomain;
	tdbp->tdb_rdomain_post = rdomain;

	/* Initialize counters. */
	tdbp->tdb_counters = counters_alloc(tdb_ncounters);

	/* Initialize timeouts. */
	timeout_set_proc(&tdbp->tdb_timer_tmo, tdb_timeout, tdbp);
	timeout_set_proc(&tdbp->tdb_first_tmo, tdb_firstuse, tdbp);
	timeout_set_proc(&tdbp->tdb_stimer_tmo, tdb_soft_timeout, tdbp);
	timeout_set_proc(&tdbp->tdb_sfirst_tmo, tdb_soft_firstuse, tdbp);

	return tdbp;
}

void
tdb_free(struct tdb *tdbp)
{
	NET_ASSERT_LOCKED();

	if (tdbp->tdb_xform) {
		(*(tdbp->tdb_xform->xf_zeroize))(tdbp);
		tdbp->tdb_xform = NULL;
	}

#if NPFSYNC > 0 && defined(IPSEC)
	/* Cleanup pfsync references */
	pfsync_delete_tdb(tdbp);
#endif

	KASSERT(TAILQ_EMPTY(&tdbp->tdb_policy_head));

	if (tdbp->tdb_ids) {
		ipsp_ids_free(tdbp->tdb_ids);
		tdbp->tdb_ids = NULL;
	}

#if NPF > 0
	if (tdbp->tdb_tag) {
		pf_tag_unref(tdbp->tdb_tag);
		tdbp->tdb_tag = 0;
	}
#endif

	counters_free(tdbp->tdb_counters, tdb_ncounters);

	KASSERT(tdbp->tdb_onext == NULL);
	KASSERT(tdbp->tdb_inext == NULL);

	/* Remove expiration timeouts. */
	KASSERT(timeout_pending(&tdbp->tdb_timer_tmo) == 0);
	KASSERT(timeout_pending(&tdbp->tdb_first_tmo) == 0);
	KASSERT(timeout_pending(&tdbp->tdb_stimer_tmo) == 0);
	KASSERT(timeout_pending(&tdbp->tdb_sfirst_tmo) == 0);

	pool_put(&tdb_pool, tdbp);
}

/*
 * Do further initializations of a TDB.
 */
int
tdb_init(struct tdb *tdbp, u_int16_t alg, struct ipsecinit *ii)
{
	const struct xformsw *xsp;
	int err;
#ifdef ENCDEBUG
	char buf[INET6_ADDRSTRLEN];
#endif

	for (xsp = xformsw; xsp < xformswNXFORMSW; xsp++) {
		if (xsp->xf_type == alg) {
			err = (*(xsp->xf_init))(tdbp, xsp, ii);
			return err;
		}
	}

	DPRINTF("no alg %d for spi %08x, addr %s, proto %d",
	    alg, ntohl(tdbp->tdb_spi),
	    ipsp_address(&tdbp->tdb_dst, buf, sizeof(buf)),
	    tdbp->tdb_sproto);

	return EINVAL;
}

#if defined(DDB) || defined(ENCDEBUG)
/* Return a printable string for the address. */
const char *
ipsp_address(union sockaddr_union *sa, char *buf, socklen_t size)
{
	switch (sa->sa.sa_family) {
	case AF_INET:
		return inet_ntop(AF_INET, &sa->sin.sin_addr,
		    buf, (size_t)size);

#ifdef INET6
	case AF_INET6:
		return inet_ntop(AF_INET6, &sa->sin6.sin6_addr,
		    buf, (size_t)size);
#endif /* INET6 */

	default:
		return "(unknown address family)";
	}
}
#endif /* DDB || ENCDEBUG */

/* Check whether an IP{4,6} address is unspecified. */
int
ipsp_is_unspecified(union sockaddr_union addr)
{
	switch (addr.sa.sa_family) {
	case AF_INET:
		if (addr.sin.sin_addr.s_addr == INADDR_ANY)
			return 1;
		else
			return 0;

#ifdef INET6
	case AF_INET6:
		if (IN6_IS_ADDR_UNSPECIFIED(&addr.sin6.sin6_addr))
			return 1;
		else
			return 0;
#endif /* INET6 */

	case 0: /* No family set. */
	default:
		return 1;
	}
}

int
ipsp_ids_match(struct ipsec_ids *a, struct ipsec_ids *b)
{
	return a == b;
}

struct ipsec_ids *
ipsp_ids_insert(struct ipsec_ids *ids)
{
	struct ipsec_ids *found;
	u_int32_t start_flow;

	mtx_enter(&ipsec_flows_mtx);

	found = RBT_INSERT(ipsec_ids_tree, &ipsec_ids_tree, ids);
	if (found) {
		/* if refcount was zero, then timeout is running */
		if ((++found->id_refcount) == 1) {
			LIST_REMOVE(found, id_gc_list);

			if (LIST_EMPTY(&ipsp_ids_gc_list))
				timeout_del(&ipsp_ids_gc_timeout);
		}
		mtx_leave (&ipsec_flows_mtx);
		DPRINTF("ids %p count %d", found, found->id_refcount);
		return found;
	}

	ids->id_refcount = 1;
	ids->id_flow = start_flow = ipsec_ids_next_flow;

	if (++ipsec_ids_next_flow == 0)
		ipsec_ids_next_flow = 1;
	while (RBT_INSERT(ipsec_ids_flows, &ipsec_ids_flows, ids) != NULL) {
		ids->id_flow = ipsec_ids_next_flow;
		if (++ipsec_ids_next_flow == 0)
			ipsec_ids_next_flow = 1;
		if (ipsec_ids_next_flow == start_flow) {
			RBT_REMOVE(ipsec_ids_tree, &ipsec_ids_tree, ids);
			mtx_leave(&ipsec_flows_mtx);
			DPRINTF("ipsec_ids_next_flow exhausted %u",
			    start_flow);
			return NULL;
		}
	}
	mtx_leave(&ipsec_flows_mtx);
	DPRINTF("new ids %p flow %u", ids, ids->id_flow);
	return ids;
}

struct ipsec_ids *
ipsp_ids_lookup(u_int32_t ipsecflowinfo)
{
	struct ipsec_ids	key;
	struct ipsec_ids	*ids;

	key.id_flow = ipsecflowinfo;

	mtx_enter(&ipsec_flows_mtx);
	ids = RBT_FIND(ipsec_ids_flows, &ipsec_ids_flows, &key);
	if (ids != NULL) {
		if (ids->id_refcount != 0)
			ids->id_refcount++;
		else
			ids = NULL;
	}
	mtx_leave(&ipsec_flows_mtx);

	return ids;
}

/* free ids only from delayed timeout */
void
ipsp_ids_gc(void *arg)
{
	struct ipsec_ids *ids, *tids;

	mtx_enter(&ipsec_flows_mtx);

	LIST_FOREACH_SAFE(ids, &ipsp_ids_gc_list, id_gc_list, tids) {
		KASSERT(ids->id_refcount == 0);
		DPRINTF("ids %p count %d", ids, ids->id_refcount);

		if ((--ids->id_gc_ttl) > 0)
			continue;

		LIST_REMOVE(ids, id_gc_list);
		RBT_REMOVE(ipsec_ids_tree, &ipsec_ids_tree, ids);
		RBT_REMOVE(ipsec_ids_flows, &ipsec_ids_flows, ids);
		free(ids->id_local, M_CREDENTIALS, 0);
		free(ids->id_remote, M_CREDENTIALS, 0);
		free(ids, M_CREDENTIALS, 0);
	}

	if (!LIST_EMPTY(&ipsp_ids_gc_list))
		timeout_add_sec(&ipsp_ids_gc_timeout, 1);

	mtx_leave(&ipsec_flows_mtx);
}

/* decrements refcount, actual free happens in gc */
void
ipsp_ids_free(struct ipsec_ids *ids)
{
	if (ids == NULL)
		return;

	mtx_enter(&ipsec_flows_mtx);

	/*
	 * If the refcount becomes zero, then a timeout is started. This
	 * timeout must be cancelled if refcount is increased from zero.
	 */
	DPRINTF("ids %p count %d", ids, ids->id_refcount);
	KASSERT(ids->id_refcount > 0);

	if ((--ids->id_refcount) > 0) {
		mtx_leave(&ipsec_flows_mtx);
		return;
	}

	/*
	 * Add second for the case ipsp_ids_gc() is already running and
	 * awaits netlock to be released.
	 */
	ids->id_gc_ttl = ipsec_ids_idle + 1;

	if (LIST_EMPTY(&ipsp_ids_gc_list))
		timeout_add_sec(&ipsp_ids_gc_timeout, 1);
	LIST_INSERT_HEAD(&ipsp_ids_gc_list, ids, id_gc_list);

	mtx_leave(&ipsec_flows_mtx);
}

static int
ipsp_id_cmp(struct ipsec_id *a, struct ipsec_id *b)
{
	if (a->type > b->type)
		return 1;
	if (a->type < b->type)
		return -1;
	if (a->len > b->len)
		return 1;
	if (a->len < b->len)
		return -1;
	return memcmp(a + 1, b + 1, a->len);
}

static inline int
ipsp_ids_cmp(const struct ipsec_ids *a, const struct ipsec_ids *b)
{
	int ret;

	ret = ipsp_id_cmp(a->id_remote, b->id_remote);
	if (ret != 0)
		return ret;
	return ipsp_id_cmp(a->id_local, b->id_local);
}

static inline int
ipsp_ids_flow_cmp(const struct ipsec_ids *a, const struct ipsec_ids *b)
{
	if (a->id_flow > b->id_flow)
		return 1;
	if (a->id_flow < b->id_flow)
		return -1;
	return 0;
}
