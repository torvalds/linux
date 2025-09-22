/*	$OpenBSD: ipsec_input.c,v 1.221 2025/07/08 00:47:41 jsg Exp $	*/
/*
 * The authors of this code are John Ioannidis (ji@tla.org),
 * Angelos D. Keromytis (kermit@csd.uch.gr) and
 * Niels Provos (provos@physnet.uni-hamburg.de).
 *
 * This code was written by John Ioannidis for BSD/OS in Athens, Greece,
 * in November 1995.
 *
 * Ported to OpenBSD and NetBSD, with additional transforms, in December 1996,
 * by Angelos D. Keromytis.
 *
 * Additional transforms and features in 1997 and 1998 by Angelos D. Keromytis
 * and Niels Provos.
 *
 * Additional features in 1999 by Angelos D. Keromytis.
 *
 * Copyright (C) 1995, 1996, 1997, 1998, 1999 by John Ioannidis,
 * Angelos D. Keromytis and Niels Provos.
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
#include "sec.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/protosw.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <sys/timeout.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/bpf.h>

#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_var.h>
#include <netinet/ip_icmp.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>

#if NPF > 0
#include <net/pfvar.h>
#endif

#if NSEC > 0
#include <net/if_sec.h>
#endif

#ifdef INET6
#include <netinet/ip6.h>
#include <netinet6/ip6_var.h>
#endif /* INET6 */

#include <netinet/ip_ipsp.h>
#include <netinet/ip_esp.h>
#include <netinet/ip_ah.h>
#include <netinet/ip_ipcomp.h>

#include <net/if_enc.h>

#include "bpfilter.h"

/*
 * Locks used to protect data:
 *	a	atomic
 */

void ipsec_common_ctlinput(u_int, int, struct sockaddr *, void *, int);

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

/* sysctl variables */
int encdebug = 0;						/* [a] */
int ipsec_keep_invalid = IPSEC_DEFAULT_EMBRYONIC_SA_TIMEOUT;	/* [a] */
int ipsec_require_pfs = IPSEC_DEFAULT_PFS;			/* [a] */
int ipsec_soft_allocations = IPSEC_DEFAULT_SOFT_ALLOCATIONS;	/* [a] */
int ipsec_exp_allocations = IPSEC_DEFAULT_EXP_ALLOCATIONS;	/* [a] */
int ipsec_soft_bytes = IPSEC_DEFAULT_SOFT_BYTES;		/* [a] */
int ipsec_exp_bytes = IPSEC_DEFAULT_EXP_BYTES;			/* [a] */
int ipsec_soft_timeout = IPSEC_DEFAULT_SOFT_TIMEOUT;		/* [a] */
int ipsec_exp_timeout = IPSEC_DEFAULT_EXP_TIMEOUT;		/* [a] */
int ipsec_soft_first_use = IPSEC_DEFAULT_SOFT_FIRST_USE;	/* [a] */
int ipsec_exp_first_use = IPSEC_DEFAULT_EXP_FIRST_USE;		/* [a] */
int ipsec_expire_acquire = IPSEC_DEFAULT_EXPIRE_ACQUIRE;	/* [a] */

int esp_enable = 1;		/* [a] */
int ah_enable = 1;		/* [a] */
int ipcomp_enable = 0;		/* [a] */

const struct sysctl_bounded_args espctl_vars[] = {
	{ESPCTL_ENABLE, &esp_enable, 0, 1},
	{ESPCTL_UDPENCAP_ENABLE, &udpencap_enable, 0, 1},
	{ESPCTL_UDPENCAP_PORT, &udpencap_port, 0, 65535},
};

const struct sysctl_bounded_args ahctl_vars[] = {
	{AHCTL_ENABLE, &ah_enable, 0, 1},
};
const struct sysctl_bounded_args ipcompctl_vars[] = {
	{IPCOMPCTL_ENABLE, &ipcomp_enable, 0, 1},
};

struct cpumem *espcounters;
struct cpumem *ahcounters;
struct cpumem *ipcompcounters;
struct cpumem *ipseccounters;

struct ipsec_sysctl_algorithm {
	const char *name;
	int val;
};

const struct ipsec_sysctl_algorithm ipsec_sysctl_enc_algs[] = {
	{"aes",			IPSEC_ENC_AES},
	{"aesctr",		IPSEC_ENC_AESCTR},
	{"3des",		IPSEC_ENC_3DES},
	{"blowfish",		IPSEC_ENC_BLOWFISH},
	{"cast128",		IPSEC_ENC_CAST128},
	{NULL,			-1},
};

const struct ipsec_sysctl_algorithm ipsec_sysctl_auth_algs[] = {
	{"hmac-sha1",		IPSEC_AUTH_HMAC_SHA1},
	{"hmac-ripemd160",	IPSEC_AUTH_HMAC_RIPEMD160},
	{"hmac-md5",		IPSEC_AUTH_MD5},
	{"hmac-sha2-256",	IPSEC_AUTH_SHA2_256},
	{"hmac-sha2-384",	IPSEC_AUTH_SHA2_384},
	{"hmac-sha2-512",	IPSEC_AUTH_SHA2_512},
	{NULL,			-1},
};

const struct ipsec_sysctl_algorithm ipsec_sysctl_comp_algs[] = {
	{"deflate",		IPSEC_COMP_DEFLATE},
	{NULL,			-1},
};

int ipsec_def_enc = IPSEC_ENC_AES;		/* [a] */
int ipsec_def_auth = IPSEC_AUTH_HMAC_SHA1;	/* [a] */
int ipsec_def_comp = IPSEC_COMP_DEFLATE;	/* [a] */

const struct sysctl_bounded_args ipsecctl_vars[] = {
	{ IPSEC_ENCDEBUG, &encdebug, 0, 1 },
	{ IPSEC_EXPIRE_ACQUIRE, &ipsec_expire_acquire, 0, INT_MAX },
	{ IPSEC_EMBRYONIC_SA_TIMEOUT, &ipsec_keep_invalid, 0, INT_MAX },
	{ IPSEC_REQUIRE_PFS, &ipsec_require_pfs, 0, 1 },
	{ IPSEC_SOFT_ALLOCATIONS, &ipsec_soft_allocations, 0, INT_MAX },
	{ IPSEC_ALLOCATIONS, &ipsec_exp_allocations, 0, INT_MAX },
	{ IPSEC_SOFT_BYTES, &ipsec_soft_bytes, 0, INT_MAX },
	{ IPSEC_BYTES, &ipsec_exp_bytes, 0, INT_MAX },
	{ IPSEC_TIMEOUT, &ipsec_exp_timeout, 0, INT_MAX },
	{ IPSEC_SOFT_TIMEOUT, &ipsec_soft_timeout, 0, INT_MAX },
	{ IPSEC_SOFT_FIRSTUSE, &ipsec_soft_first_use, 0, INT_MAX },
	{ IPSEC_FIRSTUSE, &ipsec_exp_first_use, 0, INT_MAX },
};

int ipsec_sysctl_algorithm(int, void *, size_t *, void *, size_t);
int esp_sysctl_espstat(void *, size_t *, void *);
int ah_sysctl_ahstat(void *, size_t *, void *);
int ipcomp_sysctl_ipcompstat(void *, size_t *, void *);
int ipsec_sysctl_ipsecstat(void *, size_t *, void *);

void
ipsec_init(void)
{
	espcounters = counters_alloc(esps_ncounters);
	ahcounters = counters_alloc(ahs_ncounters);
	ipcompcounters = counters_alloc(ipcomps_ncounters);
	ipseccounters = counters_alloc(ipsec_ncounters);

	ipsp_init();
}

/*
 * ipsec_common_input() gets called when we receive an IPsec-protected packet
 * in IPv4 or IPv6. All it does is find the right TDB and call the appropriate
 * transform. The callback takes care of further processing (like ingress
 * filtering).
 */
int
ipsec_common_input(struct mbuf **mp, int skip, int protoff, int af, int sproto,
    int udpencap, struct netstack *ns)
{
#define IPSEC_ISTAT(x,y,z) do {			\
	if (sproto == IPPROTO_ESP)		\
		espstat_inc(x);			\
	else if (sproto == IPPROTO_AH)		\
		ahstat_inc(y);			\
	else					\
		ipcompstat_inc(z);		\
} while (0)

	struct mbuf *m = *mp;
	union sockaddr_union dst_address;
	struct tdb *tdbp = NULL;
	u_int32_t spi;
	u_int16_t cpi;
	int prot;
#ifdef ENCDEBUG
	char buf[INET6_ADDRSTRLEN];
#endif

	NET_ASSERT_LOCKED();

	ipsecstat_pkt(ipsec_ipackets, ipsec_ibytes, m->m_pkthdr.len);
	IPSEC_ISTAT(esps_input, ahs_input, ipcomps_input);

	if ((sproto == IPPROTO_IPCOMP) && (m->m_flags & M_COMP)) {
		DPRINTF("repeated decompression");
		ipcompstat_inc(ipcomps_pdrops);
		goto drop;
	}

	if (m->m_pkthdr.len - skip < 2 * sizeof(u_int32_t)) {
		DPRINTF("packet too small");
		IPSEC_ISTAT(esps_hdrops, ahs_hdrops, ipcomps_hdrops);
		goto drop;
	}

	/* Retrieve the SPI from the relevant IPsec header */
	switch (sproto) {
	case IPPROTO_ESP:
		m_copydata(m, skip, sizeof(u_int32_t), (caddr_t) &spi);
		break;
	case IPPROTO_AH:
		m_copydata(m, skip + sizeof(u_int32_t), sizeof(u_int32_t),
		    (caddr_t) &spi);
		break;
	case IPPROTO_IPCOMP:
		m_copydata(m, skip + sizeof(u_int16_t), sizeof(u_int16_t),
		    (caddr_t) &cpi);
		spi = ntohl(htons(cpi));
		break;
	default:
		panic("%s: unknown/unsupported security protocol %d",
		    __func__, sproto);
	}

	/*
	 * Find tunnel control block and (indirectly) call the appropriate
	 * kernel crypto routine. The resulting mbuf chain is a valid
	 * IP packet ready to go through input processing.
	 */

	memset(&dst_address, 0, sizeof(dst_address));
	dst_address.sa.sa_family = af;

	switch (af) {
	case AF_INET:
		dst_address.sin.sin_len = sizeof(struct sockaddr_in);
		m_copydata(m, offsetof(struct ip, ip_dst),
		    sizeof(struct in_addr),
		    (caddr_t) &(dst_address.sin.sin_addr));
		break;

#ifdef INET6
	case AF_INET6:
		dst_address.sin6.sin6_len = sizeof(struct sockaddr_in6);
		m_copydata(m, offsetof(struct ip6_hdr, ip6_dst),
		    sizeof(struct in6_addr),
		    (caddr_t) &(dst_address.sin6.sin6_addr));
		in6_recoverscope(&dst_address.sin6,
		    &dst_address.sin6.sin6_addr);
		break;
#endif /* INET6 */

	default:
		DPRINTF("unsupported protocol family %d", af);
		IPSEC_ISTAT(esps_nopf, ahs_nopf, ipcomps_nopf);
		goto drop;
	}

	tdbp = gettdb(rtable_l2(m->m_pkthdr.ph_rtableid),
	    spi, &dst_address, sproto);
	if (tdbp == NULL) {
		DPRINTF("could not find SA for packet to %s, spi %08x",
		    ipsp_address(&dst_address, buf, sizeof(buf)), ntohl(spi));
		IPSEC_ISTAT(esps_notdb, ahs_notdb, ipcomps_notdb);
		goto drop;
	}

	if (tdbp->tdb_flags & TDBF_INVALID) {
		DPRINTF("attempted to use invalid SA %s/%08x/%u",
		    ipsp_address(&dst_address, buf, sizeof(buf)),
		    ntohl(spi), tdbp->tdb_sproto);
		IPSEC_ISTAT(esps_invalid, ahs_invalid, ipcomps_invalid);
		goto drop;
	}

	if (udpencap && !(tdbp->tdb_flags & TDBF_UDPENCAP)) {
		DPRINTF("attempted to use non-udpencap SA %s/%08x/%u",
		    ipsp_address(&dst_address, buf, sizeof(buf)),
		    ntohl(spi), tdbp->tdb_sproto);
		espstat_inc(esps_udpinval);
		goto drop;
	}

	if (!udpencap && (tdbp->tdb_flags & TDBF_UDPENCAP)) {
		DPRINTF("attempted to use udpencap SA %s/%08x/%u",
		    ipsp_address(&dst_address, buf, sizeof(buf)),
		    ntohl(spi), tdbp->tdb_sproto);
		espstat_inc(esps_udpneeded);
		goto drop;
	}

	if (tdbp->tdb_xform == NULL) {
		DPRINTF("attempted to use uninitialized SA %s/%08x/%u",
		    ipsp_address(&dst_address, buf, sizeof(buf)),
		    ntohl(spi), tdbp->tdb_sproto);
		IPSEC_ISTAT(esps_noxform, ahs_noxform, ipcomps_noxform);
		goto drop;
	}

	KERNEL_LOCK();
	/* Register first use, setup expiration timer. */
	if (tdbp->tdb_first_use == 0) {
		tdbp->tdb_first_use = gettime();
		if (tdbp->tdb_flags & TDBF_FIRSTUSE) {
			if (timeout_add_sec(&tdbp->tdb_first_tmo,
			    tdbp->tdb_exp_first_use))
				tdb_ref(tdbp);
		}
		if (tdbp->tdb_flags & TDBF_SOFT_FIRSTUSE) {
			if (timeout_add_sec(&tdbp->tdb_sfirst_tmo,
			    tdbp->tdb_soft_first_use))
				tdb_ref(tdbp);
		}
	}

	tdbstat_pkt(tdbp, tdb_ipackets, tdb_ibytes, m->m_pkthdr.len);

	/*
	 * Call appropriate transform and return -- callback takes care of
	 * everything else.
	 */
	prot = (*(tdbp->tdb_xform->xf_input))(mp, tdbp, skip, protoff, ns);
	if (prot == IPPROTO_DONE) {
		ipsecstat_inc(ipsec_idrops);
		tdbstat_inc(tdbp, tdb_idrops);
	}
	tdb_unref(tdbp);
	KERNEL_UNLOCK();
	return prot;

 drop:
	m_freemp(mp);
	ipsecstat_inc(ipsec_idrops);
	if (tdbp != NULL)
		tdbstat_inc(tdbp, tdb_idrops);
	tdb_unref(tdbp);
	return IPPROTO_DONE;
}

/*
 * IPsec input callback, called by the transform callback. Takes care of
 * filtering and other sanity checks on the processed packet.
 */
int
ipsec_common_input_cb(struct mbuf **mp, struct tdb *tdbp, int skip,
    int protoff, struct netstack *ns)
{
	struct mbuf *m = *mp;
	int af, sproto;
	u_int8_t prot;
#if NBPFILTER > 0
	struct ifnet *encif;
#endif
	struct ip *ip;
#ifdef INET6
	struct ip6_hdr *ip6;
#endif /* INET6 */
	struct m_tag *mtag;
	struct tdb_ident *tdbi;
#ifdef ENCDEBUG
	char buf[INET6_ADDRSTRLEN];
#endif

	af = tdbp->tdb_dst.sa.sa_family;
	sproto = tdbp->tdb_sproto;

	tdbp->tdb_last_used = gettime();

	/* Fix IPv4 header */
	if (af == AF_INET) {
		if (m->m_len < skip &&
		    (m = *mp = m_pullup(m, skip)) == NULL) {
			DPRINTF("processing failed for SA %s/%08x",
			    ipsp_address(&tdbp->tdb_dst, buf, sizeof(buf)),
			    ntohl(tdbp->tdb_spi));
			IPSEC_ISTAT(esps_hdrops, ahs_hdrops, ipcomps_hdrops);
			goto baddone;
		}

		ip = mtod(m, struct ip *);
		ip->ip_len = htons(m->m_pkthdr.len);
		in_hdr_cksum_out(m, NULL);
		prot = ip->ip_p;
	}

#ifdef INET6
	/* Fix IPv6 header */
	if (af == AF_INET6) {
		if (m->m_len < sizeof(struct ip6_hdr) &&
		    (m = *mp = m_pullup(m, sizeof(struct ip6_hdr))) == NULL) {

			DPRINTF("processing failed for SA %s/%08x",
			    ipsp_address(&tdbp->tdb_dst, buf, sizeof(buf)),
			    ntohl(tdbp->tdb_spi));
			IPSEC_ISTAT(esps_hdrops, ahs_hdrops, ipcomps_hdrops);
			goto baddone;
		}

		ip6 = mtod(m, struct ip6_hdr *);
		ip6->ip6_plen = htons(m->m_pkthdr.len - skip);

		/* Save protocol */
		m_copydata(m, protoff, 1, (caddr_t) &prot);
	}
#endif /* INET6 */

	/*
	 * Fix TCP/UDP checksum of UDP encapsulated transport mode ESP packet.
	 * (RFC3948 3.1.2)
	 */
	if ((af == AF_INET || af == AF_INET6) &&
	    (tdbp->tdb_flags & TDBF_UDPENCAP) &&
	    (tdbp->tdb_flags & TDBF_TUNNELING) == 0) {
		u_int16_t cksum;

		switch (prot) {
		case IPPROTO_UDP:
			if (m->m_pkthdr.len < skip + sizeof(struct udphdr)) {
				IPSEC_ISTAT(esps_hdrops, ahs_hdrops,
				    ipcomps_hdrops);
				goto baddone;
			}
			cksum = 0;
			m_copyback(m, skip + offsetof(struct udphdr, uh_sum),
			    sizeof(cksum), &cksum, M_NOWAIT);
#ifdef INET6
			if (af == AF_INET6) {
				cksum = in6_cksum(m, IPPROTO_UDP, skip,
				    m->m_pkthdr.len - skip);
				m_copyback(m, skip + offsetof(struct udphdr,
				    uh_sum), sizeof(cksum), &cksum, M_NOWAIT);
			}
#endif
			break;
		case IPPROTO_TCP:
			if (m->m_pkthdr.len < skip + sizeof(struct tcphdr)) {
				IPSEC_ISTAT(esps_hdrops, ahs_hdrops,
				    ipcomps_hdrops);
				goto baddone;
			}
			cksum = 0;
			m_copyback(m, skip + offsetof(struct tcphdr, th_sum),
			    sizeof(cksum), &cksum, M_NOWAIT);
			if (af == AF_INET)
				cksum = in4_cksum(m, IPPROTO_TCP, skip,
				    m->m_pkthdr.len - skip);
#ifdef INET6
			else if (af == AF_INET6)
				cksum = in6_cksum(m, IPPROTO_TCP, skip,
				    m->m_pkthdr.len - skip);
#endif
			m_copyback(m, skip + offsetof(struct tcphdr, th_sum),
			    sizeof(cksum), &cksum, M_NOWAIT);
			break;
		}
	}

	/*
	 * Record what we've done to the packet (under what SA it was
	 * processed).
	 */
	if (tdbp->tdb_sproto != IPPROTO_IPCOMP) {
		mtag = m_tag_get(PACKET_TAG_IPSEC_IN_DONE,
		    sizeof(struct tdb_ident), M_NOWAIT);
		if (mtag == NULL) {
			DPRINTF("failed to get tag");
			IPSEC_ISTAT(esps_hdrops, ahs_hdrops, ipcomps_hdrops);
			goto baddone;
		}

		tdbi = (struct tdb_ident *)(mtag + 1);
		tdbi->dst = tdbp->tdb_dst;
		tdbi->proto = tdbp->tdb_sproto;
		tdbi->spi = tdbp->tdb_spi;
		tdbi->rdomain = tdbp->tdb_rdomain;

		m_tag_prepend(m, mtag);
	}

	switch (sproto) {
	case IPPROTO_ESP:
		/* Packet is confidential ? */
		if (tdbp->tdb_encalgxform)
			m->m_flags |= M_CONF;

		/* Check if we had authenticated ESP. */
		if (tdbp->tdb_authalgxform)
			m->m_flags |= M_AUTH;
		break;
	case IPPROTO_AH:
		m->m_flags |= M_AUTH;
		break;
	case IPPROTO_IPCOMP:
		m->m_flags |= M_COMP;
		break;
	default:
		panic("%s: unknown/unsupported security protocol %d",
		    __func__, sproto);
	}

#if NPF > 0
	/* Add pf tag if requested. */
	pf_tag_packet(m, tdbp->tdb_tag, -1);
	pf_pkt_addr_changed(m);
#endif
	if (tdbp->tdb_rdomain != tdbp->tdb_rdomain_post)
		m->m_pkthdr.ph_rtableid = tdbp->tdb_rdomain_post;

	if (tdbp->tdb_flags & TDBF_TUNNELING)
		m->m_flags |= M_TUNNEL;

	ipsecstat_add(ipsec_idecompbytes, m->m_pkthdr.len);
	tdbstat_add(tdbp, tdb_idecompbytes, m->m_pkthdr.len);

#if NBPFILTER > 0
	encif = enc_getif(tdbp->tdb_rdomain_post, tdbp->tdb_tap);
	if (encif != NULL) {
		encif->if_ipackets++;
		encif->if_ibytes += m->m_pkthdr.len;

		if (sproto != IPPROTO_IPCOMP) {
			/* XXX This conflicts with the scoped nature of IPv6 */
			m->m_pkthdr.ph_ifidx = encif->if_index;
		}
		if (encif->if_bpf) {
			struct enchdr hdr;

			hdr.af = af;
			hdr.spi = tdbp->tdb_spi;
			hdr.flags = m->m_flags & (M_AUTH|M_CONF);

			bpf_mtap_hdr(encif->if_bpf, (char *)&hdr,
			    ENC_HDRLEN, m, BPF_DIRECTION_IN);
		}
	}
#endif

	if (ISSET(tdbp->tdb_flags, TDBF_IFACE)) {
#if NSEC > 0
		if (ISSET(tdbp->tdb_flags, TDBF_TUNNELING) &&
		    tdbp->tdb_iface_dir == IPSP_DIRECTION_IN) {
			struct sec_softc *sc = sec_get(tdbp->tdb_iface);
			if (sc == NULL)
				goto baddone;

			sec_input(sc, af, prot, m, ns);
			sec_put(sc);
			return IPPROTO_DONE;
		}
#endif /* NSEC > 0 */
		goto baddone;
	}

#if NPF > 0
	/*
	 * The ip_deliver() shortcut avoids running through ip_input() with the
	 * same IP header twice.  Packets in transport mode have to be be
	 * passed to pf explicitly.  In tunnel mode the inner IP header will
	 * run through ip_input() and pf anyway.
	 */
	if ((tdbp->tdb_flags & TDBF_TUNNELING) == 0) {
		struct ifnet *ifp;

		/* This is the enc0 interface unless for ipcomp. */
		if ((ifp = if_get(m->m_pkthdr.ph_ifidx)) == NULL) {
			goto baddone;
		}
		if (pf_test(af, PF_IN, ifp, mp) != PF_PASS) {
			if_put(ifp);
			goto baddone;
		}
		m = *mp;
		if_put(ifp);
		if (m == NULL)
			return IPPROTO_DONE;
	}
#endif
	/* Return to the appropriate protocol handler in deliver loop. */
	return prot;

 baddone:
	m_freemp(mp);
	return IPPROTO_DONE;
#undef IPSEC_ISTAT
}

int
ipsec_sysctl(int *name, u_int namelen, void *oldp, size_t *oldlenp, void *newp,
    size_t newlen)
{
	switch (name[0]) {
	case IPCTL_IPSEC_ENC_ALGORITHM:
	case IPCTL_IPSEC_AUTH_ALGORITHM:
	case IPCTL_IPSEC_IPCOMP_ALGORITHM:
		return (ipsec_sysctl_algorithm(name[0], oldp, oldlenp,
		    newp, newlen));
	case IPCTL_IPSEC_STATS:
		return (ipsec_sysctl_ipsecstat(oldp, oldlenp, newp));
	default:
		return (sysctl_bounded_arr(ipsecctl_vars, nitems(ipsecctl_vars),
		    name, namelen, oldp, oldlenp, newp, newlen));
	}
}

int
ipsec_sysctl_algorithm(int name, void *oldp, size_t *oldlenp,
    void *newp, size_t newlen)
{
	const struct ipsec_sysctl_algorithm *algs, *p;
	int *var, oldval, error;
	char buf[20];

	switch (name) {
	case IPCTL_IPSEC_ENC_ALGORITHM:
		algs = ipsec_sysctl_enc_algs;
		var = &ipsec_def_enc;
		break;
	case IPCTL_IPSEC_AUTH_ALGORITHM:
		algs = ipsec_sysctl_auth_algs;
		var = &ipsec_def_auth;
		break;
	case IPCTL_IPSEC_IPCOMP_ALGORITHM:
		algs = ipsec_sysctl_comp_algs;
		var = &ipsec_def_comp;
		break;
	default:
		return (EOPNOTSUPP);
	}

	oldval = atomic_load_int(var);

	for (p = algs; p->name != NULL; p++) {
		if (p->val == oldval) {
			strlcpy(buf, p->name, sizeof(buf));
			break;
		}
	}

	KASSERT(p->name != NULL);

	error = sysctl_tstring(oldp, oldlenp, newp, newlen,
	    buf, sizeof(buf));
	if (error)
		return (error);

	if (newp) {
		size_t buflen;

		if ((buflen = strlen(buf)) == 0)
			return (EINVAL);

		for (p = algs; p->name != NULL; p++) {
			if (strncasecmp(buf, p->name, buflen) == 0)
				break;
		}

		if (p->name == NULL)
			return (EINVAL);

		if (p->val != oldval)
			atomic_store_int(var, p->val);
	}

	return (0);
}

int
esp_sysctl(int *name, u_int namelen, void *oldp, size_t *oldlenp, void *newp,
    size_t newlen)
{
	/* All sysctl names at this level are terminal. */
	if (namelen != 1)
		return (ENOTDIR);

	switch (name[0]) {
	case ESPCTL_STATS:
		return (esp_sysctl_espstat(oldp, oldlenp, newp));
	default:
		return (sysctl_bounded_arr(espctl_vars, nitems(espctl_vars),
		    name, namelen, oldp, oldlenp, newp, newlen));
	}
}

int
esp_sysctl_espstat(void *oldp, size_t *oldlenp, void *newp)
{
	struct espstat espstat;

	CTASSERT(sizeof(espstat) == (esps_ncounters * sizeof(uint64_t)));
	memset(&espstat, 0, sizeof espstat);
	counters_read(espcounters, (uint64_t *)&espstat, esps_ncounters, NULL);
	return (sysctl_rdstruct(oldp, oldlenp, newp, &espstat,
	    sizeof(espstat)));
}

int
ah_sysctl(int *name, u_int namelen, void *oldp, size_t *oldlenp, void *newp,
    size_t newlen)
{
	/* All sysctl names at this level are terminal. */
	if (namelen != 1)
		return (ENOTDIR);

	switch (name[0]) {
	case AHCTL_STATS:
		return ah_sysctl_ahstat(oldp, oldlenp, newp);
	default:
		return sysctl_bounded_arr(ahctl_vars, nitems(ahctl_vars), name,
		    namelen, oldp, oldlenp, newp, newlen);
	}
}

int
ah_sysctl_ahstat(void *oldp, size_t *oldlenp, void *newp)
{
	struct ahstat ahstat;

	CTASSERT(sizeof(ahstat) == (ahs_ncounters * sizeof(uint64_t)));
	memset(&ahstat, 0, sizeof ahstat);
	counters_read(ahcounters, (uint64_t *)&ahstat, ahs_ncounters, NULL);
	return (sysctl_rdstruct(oldp, oldlenp, newp, &ahstat, sizeof(ahstat)));
}

int
ipcomp_sysctl(int *name, u_int namelen, void *oldp, size_t *oldlenp, void *newp,
    size_t newlen)
{
	/* All sysctl names at this level are terminal. */
	if (namelen != 1)
		return (ENOTDIR);

	switch (name[0]) {
	case IPCOMPCTL_STATS:
		return ipcomp_sysctl_ipcompstat(oldp, oldlenp, newp);
	default:
		return sysctl_bounded_arr(ipcompctl_vars,
		    nitems(ipcompctl_vars), name, namelen, oldp, oldlenp,
		    newp, newlen);
	}
}

int
ipcomp_sysctl_ipcompstat(void *oldp, size_t *oldlenp, void *newp)
{
	struct ipcompstat ipcompstat;

	CTASSERT(sizeof(ipcompstat) == (ipcomps_ncounters * sizeof(uint64_t)));
	memset(&ipcompstat, 0, sizeof ipcompstat);
	counters_read(ipcompcounters, (uint64_t *)&ipcompstat,
	    ipcomps_ncounters, NULL);
	return (sysctl_rdstruct(oldp, oldlenp, newp, &ipcompstat,
	    sizeof(ipcompstat)));
}

int
ipsec_sysctl_ipsecstat(void *oldp, size_t *oldlenp, void *newp)
{
	struct ipsecstat ipsecstat;

	CTASSERT(sizeof(ipsecstat) == (ipsec_ncounters * sizeof(uint64_t)));
	memset(&ipsecstat, 0, sizeof ipsecstat);
	counters_read(ipseccounters, (uint64_t *)&ipsecstat, ipsec_ncounters,
	    NULL);
	return (sysctl_rdstruct(oldp, oldlenp, newp, &ipsecstat,
	    sizeof(ipsecstat)));
}

int
ipsec_input_disabled(struct mbuf **mp, int *offp, int proto, int af,
    struct netstack *ns)
{
	switch (af) {
	case AF_INET:
		return rip_input(mp, offp, proto, af, ns);
#ifdef INET6
	case AF_INET6:
		return rip6_input(mp, offp, proto, af, ns);
#endif
	default:
		unhandled_af(af);
	}
}

int
ah46_input(struct mbuf **mp, int *offp, int proto, int af, struct netstack *ns)
{
	int protoff;

	if (
#if NPF > 0
	    ((*mp)->m_pkthdr.pf.flags & PF_TAG_DIVERTED) ||
#endif
	    !atomic_load_int(&ah_enable))
		return ipsec_input_disabled(mp, offp, proto, af, ns);

	protoff = ipsec_protoff(*mp, *offp, af);
	if (protoff < 0) {
		DPRINTF("bad packet header chain");
		ahstat_inc(ahs_hdrops);
		m_freemp(mp);
		return IPPROTO_DONE;
	}

	return ipsec_common_input(mp, *offp, protoff, af, proto, 0, ns);
}

void
ah4_ctlinput(int cmd, struct sockaddr *sa, u_int rdomain, void *v)
{
	if (sa->sa_family != AF_INET ||
	    sa->sa_len != sizeof(struct sockaddr_in))
		return;

	ipsec_common_ctlinput(rdomain, cmd, sa, v, IPPROTO_AH);
}

int
esp46_input(struct mbuf **mp, int *offp, int proto, int af,
    struct netstack *ns)
{
	int protoff;

	if (
#if NPF > 0
	    ((*mp)->m_pkthdr.pf.flags & PF_TAG_DIVERTED) ||
#endif
	    !atomic_load_int(&esp_enable))
		return ipsec_input_disabled(mp, offp, proto, af, ns);

	protoff = ipsec_protoff(*mp, *offp, af);
	if (protoff < 0) {
		DPRINTF("bad packet header chain");
		espstat_inc(esps_hdrops);
		m_freemp(mp);
		return IPPROTO_DONE;
	}

	return ipsec_common_input(mp, *offp, protoff, af, proto, 0, ns);
}

/* IPv4 IPCOMP wrapper */
int
ipcomp46_input(struct mbuf **mp, int *offp, int proto, int af,
    struct netstack *ns)
{
	int protoff;

	if (
#if NPF > 0
	    ((*mp)->m_pkthdr.pf.flags & PF_TAG_DIVERTED) ||
#endif
	    !atomic_load_int(&ipcomp_enable))
		return ipsec_input_disabled(mp, offp, proto, af, ns);

	protoff = ipsec_protoff(*mp, *offp, af);
	if (protoff < 0) {
		DPRINTF("bad packet header chain");
		ipcompstat_inc(ipcomps_hdrops);
		m_freemp(mp);
		return IPPROTO_DONE;
	}

	return ipsec_common_input(mp, *offp, protoff, af, proto, 0, ns);
}

void
ipsec_set_mtu(struct tdb *tdbp, u_int32_t mtu)
{
	ssize_t adjust;

	NET_ASSERT_LOCKED();

	/* Walk the chain backwards to the first tdb */
	for (; tdbp != NULL; tdbp = tdbp->tdb_inext) {
		if (tdbp->tdb_flags & TDBF_INVALID ||
		    (adjust = ipsec_hdrsz(tdbp)) == -1)
			return;

		mtu -= adjust;

		/* Store adjusted MTU in tdb */
		tdbp->tdb_mtu = mtu;
		tdbp->tdb_mtutimeout = gettime() +
		    atomic_load_int(&ip_mtudisc_timeout);
		DPRINTF("spi %08x mtu %d adjust %ld",
		    ntohl(tdbp->tdb_spi), tdbp->tdb_mtu, adjust);
	}
}

void
ipsec_common_ctlinput(u_int rdomain, int cmd, struct sockaddr *sa,
    void *v, int proto)
{
	struct ip *ip = v;

	if (cmd == PRC_MSGSIZE && ip && atomic_load_int(&ip_mtudisc) &&
	    ip->ip_v == 4) {
		struct tdb *tdbp;
		struct sockaddr_in dst;
		struct icmp *icp;
		int hlen = ip->ip_hl << 2;
		u_int32_t spi, mtu;

		/* Find the right MTU. */
		icp = (struct icmp *)((caddr_t) ip -
		    offsetof(struct icmp, icmp_ip));
		mtu = ntohs(icp->icmp_nextmtu);

		/*
		 * Ignore the packet, if we do not receive a MTU
		 * or the MTU is too small to be acceptable.
		 */
		if (mtu < 296)
			return;

		memset(&dst, 0, sizeof(struct sockaddr_in));
		dst.sin_family = AF_INET;
		dst.sin_len = sizeof(struct sockaddr_in);
		dst.sin_addr.s_addr = ip->ip_dst.s_addr;

		memcpy(&spi, (caddr_t)ip + hlen, sizeof(u_int32_t));

		tdbp = gettdb_rev(rdomain, spi, (union sockaddr_union *)&dst,
		    proto);
		ipsec_set_mtu(tdbp, mtu);
		tdb_unref(tdbp);
	}
}

void
udpencap_ctlinput(int cmd, struct sockaddr *sa, u_int rdomain, void *v)
{
	struct ip *ip = v;
	struct tdb *tdbp, *first;
	struct icmp *icp;
	u_int32_t mtu;
	struct sockaddr_in dst, src;
	union sockaddr_union *su_dst, *su_src;

	NET_ASSERT_LOCKED();

	icp = (struct icmp *)((caddr_t) ip - offsetof(struct icmp, icmp_ip));
	mtu = ntohs(icp->icmp_nextmtu);

	/*
	 * Ignore the packet, if we do not receive a MTU
	 * or the MTU is too small to be acceptable.
	 */
	if (mtu < 296)
		return;

	memset(&dst, 0, sizeof(dst));
	dst.sin_family = AF_INET;
	dst.sin_len = sizeof(struct sockaddr_in);
	dst.sin_addr.s_addr = ip->ip_dst.s_addr;
	su_dst = (union sockaddr_union *)&dst;
	memset(&src, 0, sizeof(src));
	src.sin_family = AF_INET;
	src.sin_len = sizeof(struct sockaddr_in);
	src.sin_addr.s_addr = ip->ip_src.s_addr;
	su_src = (union sockaddr_union *)&src;

	first = gettdbbysrcdst_rev(rdomain, 0, su_src, su_dst, IPPROTO_ESP);

	mtx_enter(&tdb_sadb_mtx);
	for (tdbp = first; tdbp != NULL; tdbp = tdbp->tdb_snext) {
		if (tdbp->tdb_sproto == IPPROTO_ESP &&
		    ((tdbp->tdb_flags & (TDBF_INVALID|TDBF_UDPENCAP)) ==
		    TDBF_UDPENCAP) &&
		    !memcmp(&tdbp->tdb_dst, &dst, su_dst->sa.sa_len) &&
		    !memcmp(&tdbp->tdb_src, &src, su_src->sa.sa_len))
			ipsec_set_mtu(tdbp, mtu);
	}
	mtx_leave(&tdb_sadb_mtx);
	tdb_unref(first);
}

void
esp4_ctlinput(int cmd, struct sockaddr *sa, u_int rdomain, void *v)
{
	if (sa->sa_family != AF_INET ||
	    sa->sa_len != sizeof(struct sockaddr_in))
		return;

	ipsec_common_ctlinput(rdomain, cmd, sa, v, IPPROTO_ESP);
}

/* Find the offset of the next protocol field in the previous header. */
int
ipsec_protoff(struct mbuf *m, int off, int af)
{
#ifdef INET6
	struct ip6_ext ip6e;
	int protoff, nxt, l;
#endif /* INET6 */

	switch (af) {
	case AF_INET:
		return offsetof(struct ip, ip_p);
#ifdef INET6
	case AF_INET6:
		break;
#endif /* INET6 */
	default:
		unhandled_af(af);
	}

#ifdef INET6
	if (off < sizeof(struct ip6_hdr))
		return -1;

	if (off == sizeof(struct ip6_hdr))
		return offsetof(struct ip6_hdr, ip6_nxt);

	/* Chase down the header chain... */
	protoff = sizeof(struct ip6_hdr);
	nxt = (mtod(m, struct ip6_hdr *))->ip6_nxt;
	l = 0;

	do {
		protoff += l;
		m_copydata(m, protoff, sizeof(ip6e),
		    (caddr_t) &ip6e);

		if (nxt == IPPROTO_AH)
			l = (ip6e.ip6e_len + 2) << 2;
		else
			l = (ip6e.ip6e_len + 1) << 3;
#ifdef DIAGNOSTIC
		if (l <= 0)
			panic("%s: l went zero or negative", __func__);
#endif

		nxt = ip6e.ip6e_nxt;
	} while (protoff + l < off);

	/* Malformed packet check */
	if (protoff + l != off)
		return -1;

	protoff += offsetof(struct ip6_ext, ip6e_nxt);
	return protoff;
#endif /* INET6 */
}

int
ipsec_forward_check(struct mbuf *m, int hlen, int af)
{
	struct tdb *tdb;
	struct tdb_ident *tdbi;
	struct m_tag *mtag;
	int error = 0;

	/*
	 * IPsec policy check for forwarded packets. Look at
	 * inner-most IPsec SA used.
	 */
	mtag = m_tag_find(m, PACKET_TAG_IPSEC_IN_DONE, NULL);
	if (mtag != NULL) {
		tdbi = (struct tdb_ident *)(mtag + 1);
		tdb = gettdb(tdbi->rdomain, tdbi->spi, &tdbi->dst, tdbi->proto);
	} else
		tdb = NULL;
	error = ipsp_spd_lookup(m, af, hlen, IPSP_DIRECTION_IN,
	    tdb, NULL, NULL, NULL);
	tdb_unref(tdb);

	return error;
}

int
ipsec_local_check(struct mbuf *m, int hlen, int proto, int af)
{
	struct tdb *tdb;
	struct tdb_ident *tdbi;
	struct m_tag *mtag;
	int error = 0;

	/*
	 * If it's a protected packet for us, skip the policy check.
	 * That's because we really only care about the properties of
	 * the protected packet, and not the intermediate versions.
	 * While this is not the most paranoid setting, it allows
	 * some flexibility in handling nested tunnels (in setting up
	 * the policies).
	 */
	if ((proto == IPPROTO_ESP) || (proto == IPPROTO_AH) ||
	    (proto == IPPROTO_IPCOMP))
		return 0;

	/*
	 * If the protected packet was tunneled, then we need to
	 * verify the protected packet's information, not the
	 * external headers. Thus, skip the policy lookup for the
	 * external packet, and keep the IPsec information linked on
	 * the packet header (the encapsulation routines know how
	 * to deal with that).
	 */
	if ((proto == IPPROTO_IPV4) || (proto == IPPROTO_IPV6))
		return 0;

	/*
	 * When processing IPv6 header chains, do not look at the
	 * outer header.  The inner protocol is relevant and will
	 * be checked by the local delivery loop later.
	 */
	if ((af == AF_INET6) && ((proto == IPPROTO_DSTOPTS) ||
	    (proto == IPPROTO_ROUTING) || (proto == IPPROTO_FRAGMENT)))
		return 0;

	/*
	 * If the protected packet is TCP or UDP, we'll do the
	 * policy check in the respective input routine, so we can
	 * check for bypass sockets.
	 */
	if ((proto == IPPROTO_TCP) || (proto == IPPROTO_UDP))
		return 0;

	/*
	 * IPsec policy check for local-delivery packets. Look at the
	 * inner-most SA that protected the packet. This is in fact
	 * a bit too restrictive (it could end up causing packets to
	 * be dropped that semantically follow the policy, e.g., in
	 * certain SA-bundle configurations); but the alternative is
	 * very complicated (and requires keeping track of what
	 * kinds of tunneling headers have been seen in-between the
	 * IPsec headers), and I don't think we lose much functionality
	 * that's needed in the real world (who uses bundles anyway ?).
	 */
	mtag = m_tag_find(m, PACKET_TAG_IPSEC_IN_DONE, NULL);
	if (mtag) {
		tdbi = (struct tdb_ident *)(mtag + 1);
		tdb = gettdb(tdbi->rdomain, tdbi->spi, &tdbi->dst,
		    tdbi->proto);
	} else
		tdb = NULL;
	error = ipsp_spd_lookup(m, af, hlen, IPSP_DIRECTION_IN,
	    tdb, NULL, NULL, NULL);
	tdb_unref(tdb);

	return error;
}
