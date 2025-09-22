/* $OpenBSD: ip_spd.c,v 1.122 2025/07/08 00:47:41 jsg Exp $ */
/*
 * The author of this code is Angelos D. Keromytis (angelos@cis.upenn.edu)
 *
 * Copyright (c) 2000-2001 Angelos D. Keromytis.
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/pool.h>
#include <sys/timeout.h>

#include <net/rtable.h>

#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>
#include <netinet/ip_ipsp.h>
#include <net/pfkeyv2.h>

int	ipsp_spd_inp(struct mbuf *, const struct ipsec_level *,
	    struct ipsec_policy *, struct tdb **);
int	ipsp_acquire_sa(struct ipsec_policy *, union sockaddr_union *,
	    union sockaddr_union *, struct sockaddr_encap *, struct mbuf *);
int	ipsp_pending_acquire(struct ipsec_policy *, union sockaddr_union *);
void	ipsp_delete_acquire_timer(void *);
void	ipsp_delete_acquire_locked(struct ipsec_acquire *);
void	ipsp_delete_acquire(struct ipsec_acquire *);
void	ipsp_unref_acquire_locked(struct ipsec_acquire *);

struct pool ipsec_policy_pool;
struct pool ipsec_acquire_pool;

/*
 * For tdb_walk() calling tdb_delete_locked() we need lock order
 * tdb_sadb_mtx before ipo_tdb_mtx.
 */
struct mutex ipo_tdb_mtx = MUTEX_INITIALIZER(IPL_SOFTNET);

/* Protected by the NET_LOCK(). */
struct radix_node_head **spd_tables;
unsigned int spd_table_max;

struct mutex ipsec_acquire_mtx = MUTEX_INITIALIZER(IPL_SOFTNET);
struct ipsec_acquire_head ipsec_acquire_head =
    TAILQ_HEAD_INITIALIZER(ipsec_acquire_head);

struct radix_node_head *
spd_table_get(unsigned int rtableid)
{
	unsigned int rdomain;

	NET_ASSERT_LOCKED();

	if (spd_tables == NULL)
		return (NULL);

	rdomain = rtable_l2(rtableid);
	if (rdomain > spd_table_max)
		return (NULL);

	return (spd_tables[rdomain]);
}

struct radix_node_head *
spd_table_add(unsigned int rtableid)
{
	struct radix_node_head *rnh = NULL;
	unsigned int rdomain;
	void *p;

	NET_ASSERT_LOCKED_EXCLUSIVE();

	rdomain = rtable_l2(rtableid);
	if (spd_tables == NULL || rdomain > spd_table_max) {
		if ((p = mallocarray(rdomain + 1, sizeof(*rnh),
		    M_RTABLE, M_NOWAIT|M_ZERO)) == NULL)
			return (NULL);

		if (spd_tables != NULL) {
			memcpy(p, spd_tables, sizeof(*rnh) * (spd_table_max+1));
			free(spd_tables, M_RTABLE,
			    sizeof(*rnh) * (spd_table_max+1));
		}
		spd_tables = p;
		spd_table_max = rdomain;
	}

	if (spd_tables[rdomain] == NULL) {
		if (rn_inithead((void **)&rnh,
		    offsetof(struct sockaddr_encap, sen_type)) == 0)
			rnh = NULL;
		spd_tables[rdomain] = rnh;
	}

	return (spd_tables[rdomain]);
}

int
spd_table_walk(unsigned int rtableid,
    int (*func)(struct ipsec_policy *, void *, unsigned int), void *arg)
{
	struct radix_node_head *rnh;
	int (*walker)(struct radix_node *, void *, u_int) = (void *)func;
	int error;

	rnh = spd_table_get(rtableid);
	if (rnh == NULL)
		return (0);

	/* EGAIN means the tree changed. */
	while ((error = rn_walktree(rnh, walker, arg)) == EAGAIN)
		continue;

	return (error);
}

/*
 * Lookup at the SPD based on the headers contained on the mbuf. The second
 * argument indicates what protocol family the header at the beginning of
 * the mbuf is. hlen is the offset of the transport protocol header
 * in the mbuf.
 *
 * Return combinations (of return value and *tdbout):
 * - -EINVAL -> silently drop the packet
 * - errno   -> drop packet and return error
 * - 0/NULL  -> no IPsec required on packet
 * - 0/TDB   -> do IPsec
 *
 * In the case of incoming flows, only the first three combinations are
 * returned.
 */
int
ipsp_spd_lookup(struct mbuf *m, int af, int hlen, int direction,
    struct tdb *tdbin, const struct ipsec_level *seclevel, struct tdb **tdbout,
    struct ipsec_ids *ipsecflowinfo_ids)
{
	struct radix_node_head *rnh;
	struct radix_node *rn;
	union sockaddr_union sdst, ssrc;
	struct sockaddr_encap *ddst, dst;
	struct ipsec_policy *ipo;
	struct ipsec_ids *ids = NULL;
	int error, signore = 0, dignore = 0;
	u_int rdomain;

	NET_ASSERT_LOCKED();

	/*
	 * If there are no flows in place, there's no point
	 * continuing with the SPD lookup.
	 */
	if (!ipsec_in_use)
		return ipsp_spd_inp(m, seclevel, NULL, tdbout);

	/*
	 * If an input packet is destined to a BYPASS socket, just accept it.
	 */
	if ((seclevel != NULL) && (direction == IPSP_DIRECTION_IN) &&
	    (seclevel->sl_esp_trans == IPSEC_LEVEL_BYPASS) &&
	    (seclevel->sl_esp_network == IPSEC_LEVEL_BYPASS) &&
	    (seclevel->sl_auth == IPSEC_LEVEL_BYPASS)) {
		if (tdbout != NULL)
			*tdbout = NULL;
		return 0;
	}

	memset(&dst, 0, sizeof(dst));
	memset(&sdst, 0, sizeof(union sockaddr_union));
	memset(&ssrc, 0, sizeof(union sockaddr_union));
	ddst = (struct sockaddr_encap *)&dst;
	ddst->sen_family = PF_KEY;
	ddst->sen_len = SENT_LEN;

	switch (af) {
	case AF_INET:
		if (hlen < sizeof (struct ip) || m->m_pkthdr.len < hlen)
			return EINVAL;

		ddst->sen_direction = direction;
		ddst->sen_type = SENT_IP4;

		m_copydata(m, offsetof(struct ip, ip_src),
		    sizeof(struct in_addr), (caddr_t) &(ddst->sen_ip_src));
		m_copydata(m, offsetof(struct ip, ip_dst),
		    sizeof(struct in_addr), (caddr_t) &(ddst->sen_ip_dst));
		m_copydata(m, offsetof(struct ip, ip_p), sizeof(u_int8_t),
		    (caddr_t) &(ddst->sen_proto));

		sdst.sin.sin_family = ssrc.sin.sin_family = AF_INET;
		sdst.sin.sin_len = ssrc.sin.sin_len =
		    sizeof(struct sockaddr_in);
		ssrc.sin.sin_addr = ddst->sen_ip_src;
		sdst.sin.sin_addr = ddst->sen_ip_dst;

		/*
		 * If TCP/UDP, extract the port numbers to use in the lookup.
		 */
		switch (ddst->sen_proto) {
		case IPPROTO_UDP:
		case IPPROTO_TCP:
			/* Make sure there's enough data in the packet. */
			if (m->m_pkthdr.len < hlen + 2 * sizeof(u_int16_t))
				return EINVAL;

			/*
			 * Luckily, the offset of the src/dst ports in
			 * both the UDP and TCP headers is the same (first
			 * two 16-bit values in the respective headers),
			 * so we can just copy them.
			 */
			m_copydata(m, hlen, sizeof(u_int16_t),
			    (caddr_t) &(ddst->sen_sport));
			m_copydata(m, hlen + sizeof(u_int16_t),
			    sizeof(u_int16_t),
			    (caddr_t) &(ddst->sen_dport));
			break;

		default:
			ddst->sen_sport = 0;
			ddst->sen_dport = 0;
		}

		break;

#ifdef INET6
	case AF_INET6:
		if (hlen < sizeof (struct ip6_hdr) || m->m_pkthdr.len < hlen)
			return EINVAL;

		ddst->sen_type = SENT_IP6;
		ddst->sen_ip6_direction = direction;

		m_copydata(m, offsetof(struct ip6_hdr, ip6_src),
		    sizeof(struct in6_addr),
		    (caddr_t) &(ddst->sen_ip6_src));
		m_copydata(m, offsetof(struct ip6_hdr, ip6_dst),
		    sizeof(struct in6_addr),
		    (caddr_t) &(ddst->sen_ip6_dst));
		m_copydata(m, offsetof(struct ip6_hdr, ip6_nxt),
		    sizeof(u_int8_t),
		    (caddr_t) &(ddst->sen_ip6_proto));

		sdst.sin6.sin6_family = ssrc.sin6.sin6_family = AF_INET6;
		sdst.sin6.sin6_len = ssrc.sin6.sin6_len =
		    sizeof(struct sockaddr_in6);
		in6_recoverscope(&ssrc.sin6, &ddst->sen_ip6_src);
		in6_recoverscope(&sdst.sin6, &ddst->sen_ip6_dst);

		/*
		 * If TCP/UDP, extract the port numbers to use in the lookup.
		 */
		switch (ddst->sen_ip6_proto) {
		case IPPROTO_UDP:
		case IPPROTO_TCP:
			/* Make sure there's enough data in the packet. */
			if (m->m_pkthdr.len < hlen + 2 * sizeof(u_int16_t))
				return EINVAL;

			/*
			 * Luckily, the offset of the src/dst ports in
			 * both the UDP and TCP headers is the same
			 * (first two 16-bit values in the respective
			 * headers), so we can just copy them.
			 */
			m_copydata(m, hlen, sizeof(u_int16_t),
			    (caddr_t) &(ddst->sen_ip6_sport));
			m_copydata(m, hlen + sizeof(u_int16_t),
			    sizeof(u_int16_t),
			    (caddr_t) &(ddst->sen_ip6_dport));
			break;

		default:
			ddst->sen_ip6_sport = 0;
			ddst->sen_ip6_dport = 0;
		}

		break;
#endif /* INET6 */

	default:
		return EAFNOSUPPORT;
	}

	/* Actual SPD lookup. */
	rdomain = rtable_l2(m->m_pkthdr.ph_rtableid);
	if ((rnh = spd_table_get(rdomain)) == NULL ||
	    (rn = rn_match((caddr_t)&dst, rnh)) == NULL) {
		/*
		 * Return whatever the socket requirements are, there are no
		 * system-wide policies.
		 */
		return ipsp_spd_inp(m, seclevel, NULL, tdbout);
	}
	ipo = (struct ipsec_policy *)rn;

	switch (ipo->ipo_type) {
	case IPSP_PERMIT:
		return ipsp_spd_inp(m, seclevel, ipo, tdbout);

	case IPSP_DENY:
		return EHOSTUNREACH;

	case IPSP_IPSEC_USE:
	case IPSP_IPSEC_ACQUIRE:
	case IPSP_IPSEC_REQUIRE:
	case IPSP_IPSEC_DONTACQ:
		/* Nothing more needed here. */
		break;

	default:
		return EINVAL;
	}

	/* Check for non-specific destination in the policy. */
	switch (ipo->ipo_dst.sa.sa_family) {
	case AF_INET:
		if ((ipo->ipo_dst.sin.sin_addr.s_addr == INADDR_ANY) ||
		    (ipo->ipo_dst.sin.sin_addr.s_addr == INADDR_BROADCAST))
			dignore = 1;
		break;

#ifdef INET6
	case AF_INET6:
		if ((IN6_IS_ADDR_UNSPECIFIED(&ipo->ipo_dst.sin6.sin6_addr)) ||
		    (memcmp(&ipo->ipo_dst.sin6.sin6_addr, &in6mask128,
		    sizeof(in6mask128)) == 0))
			dignore = 1;
		break;
#endif /* INET6 */
	}

	/* Likewise for source. */
	switch (ipo->ipo_src.sa.sa_family) {
	case AF_INET:
		if (ipo->ipo_src.sin.sin_addr.s_addr == INADDR_ANY)
			signore = 1;
		break;

#ifdef INET6
	case AF_INET6:
		if (IN6_IS_ADDR_UNSPECIFIED(&ipo->ipo_src.sin6.sin6_addr))
			signore = 1;
		break;
#endif /* INET6 */
	}

	/* Do we have a cached entry ? If so, check if it's still valid. */
	mtx_enter(&ipo_tdb_mtx);
	if (ipo->ipo_tdb != NULL &&
	    (ipo->ipo_tdb->tdb_flags & TDBF_INVALID)) {
		TAILQ_REMOVE(&ipo->ipo_tdb->tdb_policy_head, ipo,
		    ipo_tdb_next);
		tdb_unref(ipo->ipo_tdb);
		ipo->ipo_tdb = NULL;
	}
	mtx_leave(&ipo_tdb_mtx);

	/* Outgoing packet policy check. */
	if (direction == IPSP_DIRECTION_OUT) {
		/*
		 * If the packet is destined for the policy-specified
		 * gateway/endhost, and the socket has the BYPASS
		 * option set, skip IPsec processing.
		 */
		if ((seclevel != NULL) &&
		    (seclevel->sl_esp_trans == IPSEC_LEVEL_BYPASS) &&
		    (seclevel->sl_esp_network == IPSEC_LEVEL_BYPASS) &&
		    (seclevel->sl_auth == IPSEC_LEVEL_BYPASS)) {
			/* Direct match. */
			if (dignore ||
			    !memcmp(&sdst, &ipo->ipo_dst, sdst.sa.sa_len)) {
				if (tdbout != NULL)
					*tdbout = NULL;
				return 0;
			}
		}

		/* Check that the cached TDB (if present), is appropriate. */
		mtx_enter(&ipo_tdb_mtx);
		if (ipo->ipo_tdb != NULL) {
			if ((ipo->ipo_last_searched <= ipsec_last_added) ||
			    (ipo->ipo_sproto != ipo->ipo_tdb->tdb_sproto) ||
			    memcmp(dignore ? &sdst : &ipo->ipo_dst,
			    &ipo->ipo_tdb->tdb_dst,
			    ipo->ipo_tdb->tdb_dst.sa.sa_len))
				goto nomatchout;

			if (!ipsp_aux_match(ipo->ipo_tdb,
			    ipsecflowinfo_ids? ipsecflowinfo_ids: ipo->ipo_ids,
			    &ipo->ipo_addr, &ipo->ipo_mask))
				goto nomatchout;

			/* Cached entry is good. */
			error = ipsp_spd_inp(m, seclevel, ipo, tdbout);
			mtx_leave(&ipo_tdb_mtx);
			return error;

  nomatchout:
			/* Cached TDB was not good. */
			TAILQ_REMOVE(&ipo->ipo_tdb->tdb_policy_head, ipo,
			    ipo_tdb_next);
			tdb_unref(ipo->ipo_tdb);
			ipo->ipo_tdb = NULL;
			ipo->ipo_last_searched = 0;
		}

		/*
		 * If no SA has been added since the last time we did a
		 * lookup, there's no point searching for one. However, if the
		 * destination gateway is left unspecified (or is all-1's),
		 * always lookup since this is a generic-match rule
		 * (otherwise, we can have situations where SAs to some
		 * destinations exist but are not used, possibly leading to an
		 * explosion in the number of acquired SAs).
		 */
		if (ipo->ipo_last_searched <= ipsec_last_added)	{
			struct tdb *tdbp_new;

			/* "Touch" the entry. */
			if (dignore == 0)
				ipo->ipo_last_searched = getuptime();

			/* gettdb() takes tdb_sadb_mtx, preserve lock order */
			mtx_leave(&ipo_tdb_mtx);
			/* Find an appropriate SA from the existing ones. */
			tdbp_new = gettdbbydst(rdomain,
			    dignore ? &sdst : &ipo->ipo_dst,
			    ipo->ipo_sproto,
			    ipsecflowinfo_ids? ipsecflowinfo_ids: ipo->ipo_ids,
			    &ipo->ipo_addr, &ipo->ipo_mask);
			ids = NULL;
			mtx_enter(&ipo_tdb_mtx);
			if ((tdbp_new != NULL) &&
			    (tdbp_new->tdb_flags & TDBF_DELETED)) {
				/*
				 * After tdb_delete() has released ipo_tdb_mtx
				 * in tdb_unlink(), never add a new one.
				 * tdb_cleanspd() has to catch all of them.
				 */
				tdb_unref(tdbp_new);
				tdbp_new = NULL;
			}
			if (ipo->ipo_tdb != NULL) {
				/* Remove cached TDB from parallel thread. */
				TAILQ_REMOVE(&ipo->ipo_tdb->tdb_policy_head,
				    ipo, ipo_tdb_next);
				tdb_unref(ipo->ipo_tdb);
			}
			ipo->ipo_tdb = tdbp_new;
			if (ipo->ipo_tdb != NULL) {
				/* gettdbbydst() has already refcounted tdb */
				TAILQ_INSERT_TAIL(
				    &ipo->ipo_tdb->tdb_policy_head,
				    ipo, ipo_tdb_next);
				error = ipsp_spd_inp(m, seclevel, ipo, tdbout);
				mtx_leave(&ipo_tdb_mtx);
				return error;
			}
		}
		mtx_leave(&ipo_tdb_mtx);

		/* So, we don't have an SA -- just a policy. */
		switch (ipo->ipo_type) {
		case IPSP_IPSEC_REQUIRE:
			/* Acquire SA through key management. */
			if (ipsp_acquire_sa(ipo,
			    dignore ? &sdst : &ipo->ipo_dst,
			    signore ? NULL : &ipo->ipo_src, ddst, m) != 0) {
				return EACCES;
			}

			/* FALLTHROUGH */
		case IPSP_IPSEC_DONTACQ:
			return -EINVAL;  /* Silently drop packet. */

		case IPSP_IPSEC_ACQUIRE:
			/* Acquire SA through key management. */
			ipsp_acquire_sa(ipo, dignore ? &sdst : &ipo->ipo_dst,
			    signore ? NULL : &ipo->ipo_src, ddst, NULL);

			/* FALLTHROUGH */
		case IPSP_IPSEC_USE:
			return ipsp_spd_inp(m, seclevel, ipo, tdbout);
		}
	} else { /* IPSP_DIRECTION_IN */
		if (tdbin != NULL) {
			/*
			 * Special case for bundled IPcomp/ESP SAs:
			 * 1) only IPcomp flows are loaded into kernel
			 * 2) input processing processes ESP SA first
			 * 3) then optional IPcomp processing happens
			 * 4) we only update m_tag for ESP
			 * => 'tdbin' is always set to ESP SA
			 * => flow has ipo_proto for IPcomp
			 * So if 'tdbin' points to an ESP SA and this 'tdbin' is
			 * bundled with an IPcomp SA, then we replace 'tdbin'
			 * with the IPcomp SA at tdbin->tdb_inext.
			 */
			if (ipo->ipo_sproto == IPPROTO_IPCOMP &&
			    tdbin->tdb_sproto == IPPROTO_ESP &&
			    tdbin->tdb_inext != NULL &&
			    tdbin->tdb_inext->tdb_sproto == IPPROTO_IPCOMP)
				tdbin = tdbin->tdb_inext;

			/* Direct match in the cache. */
			mtx_enter(&ipo_tdb_mtx);
			if (ipo->ipo_tdb == tdbin) {
				error = ipsp_spd_inp(m, seclevel, ipo, tdbout);
				mtx_leave(&ipo_tdb_mtx);
				return error;
			}
			mtx_leave(&ipo_tdb_mtx);

			if (memcmp(dignore ? &ssrc : &ipo->ipo_dst,
			    &tdbin->tdb_src, tdbin->tdb_src.sa.sa_len) ||
			    (ipo->ipo_sproto != tdbin->tdb_sproto))
				goto nomatchin;

			/* Match source/dest IDs. */
			if (ipo->ipo_ids)
				if (tdbin->tdb_ids == NULL ||
				    !ipsp_ids_match(ipo->ipo_ids,
				    tdbin->tdb_ids))
					goto nomatchin;

			/* Add it to the cache. */
			mtx_enter(&ipo_tdb_mtx);
			if (ipo->ipo_tdb != NULL) {
				TAILQ_REMOVE(&ipo->ipo_tdb->tdb_policy_head,
				    ipo, ipo_tdb_next);
				tdb_unref(ipo->ipo_tdb);
			}
			ipo->ipo_tdb = tdb_ref(tdbin);
			TAILQ_INSERT_TAIL(&tdbin->tdb_policy_head, ipo,
			    ipo_tdb_next);
			error = ipsp_spd_inp(m, seclevel, ipo, tdbout);
			mtx_leave(&ipo_tdb_mtx);
			return error;

  nomatchin: /* Nothing needed here, falling through */
	;
		}

		/* Check whether cached entry applies. */
		mtx_enter(&ipo_tdb_mtx);
		if (ipo->ipo_tdb != NULL) {
			/*
			 * We only need to check that the correct
			 * security protocol and security gateway are
			 * set; IDs will be the same since the cached
			 * entry is linked on this policy.
			 */
			if (ipo->ipo_sproto == ipo->ipo_tdb->tdb_sproto &&
			    !memcmp(&ipo->ipo_tdb->tdb_src,
			    dignore ? &ssrc : &ipo->ipo_dst,
			    ipo->ipo_tdb->tdb_src.sa.sa_len))
				goto skipinputsearch;

			/* Not applicable, unlink. */
			TAILQ_REMOVE(&ipo->ipo_tdb->tdb_policy_head, ipo,
			    ipo_tdb_next);
			tdb_unref(ipo->ipo_tdb);
			ipo->ipo_tdb = NULL;
			ipo->ipo_last_searched = 0;
		}

		/* Find whether there exists an appropriate SA. */
		if (ipo->ipo_last_searched <= ipsec_last_added)	{
			struct tdb *tdbp_new;

			if (dignore == 0)
				ipo->ipo_last_searched = getuptime();

			/* gettdb() takes tdb_sadb_mtx, preserve lock order */
			mtx_leave(&ipo_tdb_mtx);
			tdbp_new = gettdbbysrc(rdomain,
			    dignore ? &ssrc : &ipo->ipo_dst,
			    ipo->ipo_sproto, ipo->ipo_ids,
			    &ipo->ipo_addr, &ipo->ipo_mask);
			mtx_enter(&ipo_tdb_mtx);
			if ((tdbp_new != NULL) &&
			    (tdbp_new->tdb_flags & TDBF_DELETED)) {
				/*
				 * After tdb_delete() has released ipo_tdb_mtx
				 * in tdb_unlink(), never add a new one.
				 * tdb_cleanspd() has to catch all of them.
				 */
				tdb_unref(tdbp_new);
				tdbp_new = NULL;
			}
			if (ipo->ipo_tdb != NULL) {
				/* Remove cached TDB from parallel thread. */
				TAILQ_REMOVE(&ipo->ipo_tdb->tdb_policy_head,
				    ipo, ipo_tdb_next);
				tdb_unref(ipo->ipo_tdb);
			}
			ipo->ipo_tdb = tdbp_new;
			if (ipo->ipo_tdb != NULL) {
				/* gettdbbysrc() has already refcounted tdb */
				TAILQ_INSERT_TAIL(
				    &ipo->ipo_tdb->tdb_policy_head,
				    ipo, ipo_tdb_next);
			}
		}
  skipinputsearch:
		mtx_leave(&ipo_tdb_mtx);

		switch (ipo->ipo_type) {
		case IPSP_IPSEC_REQUIRE:
			/* If appropriate SA exists, don't acquire another. */
			if (ipo->ipo_tdb != NULL)
				return -EINVAL;  /* Silently drop packet. */

			/* Acquire SA through key management. */
			if ((error = ipsp_acquire_sa(ipo,
			    dignore ? &ssrc : &ipo->ipo_dst,
			    signore ? NULL : &ipo->ipo_src, ddst, m)) != 0)
				return error;

			/* FALLTHROUGH */
		case IPSP_IPSEC_DONTACQ:
			return -EINVAL;  /* Silently drop packet. */

		case IPSP_IPSEC_ACQUIRE:
			/* If appropriate SA exists, don't acquire another. */
			if (ipo->ipo_tdb != NULL)
				return ipsp_spd_inp(m, seclevel, ipo, tdbout);

			/* Acquire SA through key management. */
			ipsp_acquire_sa(ipo, dignore ? &ssrc : &ipo->ipo_dst,
			    signore ? NULL : &ipo->ipo_src, ddst, NULL);

			/* FALLTHROUGH */
		case IPSP_IPSEC_USE:
			return ipsp_spd_inp(m, seclevel, ipo, tdbout);
		}
	}

	/* Shouldn't ever get this far. */
	return EINVAL;
}

/*
 * Delete a policy from the SPD.
 */
int
ipsec_delete_policy(struct ipsec_policy *ipo)
{
	struct ipsec_acquire *ipa;
	struct radix_node_head *rnh;
	struct radix_node *rn = (struct radix_node *)ipo;

	NET_ASSERT_LOCKED_EXCLUSIVE();

	if (refcnt_rele(&ipo->ipo_refcnt) == 0)
		return 0;

	/* Delete from SPD. */
	if ((rnh = spd_table_get(ipo->ipo_rdomain)) == NULL ||
	    rn_delete(&ipo->ipo_addr, &ipo->ipo_mask, rnh, rn) == NULL)
		return (ESRCH);

	mtx_enter(&ipo_tdb_mtx);
	if (ipo->ipo_tdb != NULL) {
		TAILQ_REMOVE(&ipo->ipo_tdb->tdb_policy_head, ipo,
		    ipo_tdb_next);
		tdb_unref(ipo->ipo_tdb);
		ipo->ipo_tdb = NULL;
	}
	mtx_leave(&ipo_tdb_mtx);

	mtx_enter(&ipsec_acquire_mtx);
	while ((ipa = TAILQ_FIRST(&ipo->ipo_acquires)) != NULL)
		ipsp_delete_acquire_locked(ipa);
	mtx_leave(&ipsec_acquire_mtx);

	TAILQ_REMOVE(&ipsec_policy_head, ipo, ipo_list);

	if (ipo->ipo_ids)
		ipsp_ids_free(ipo->ipo_ids);

	ipsec_in_use--;

	pool_put(&ipsec_policy_pool, ipo);

	return 0;
}

void
ipsp_delete_acquire_timer(void *v)
{
	struct ipsec_acquire *ipa = v;

	mtx_enter(&ipsec_acquire_mtx);
	refcnt_rele(&ipa->ipa_refcnt);
	ipsp_delete_acquire_locked(ipa);
	mtx_leave(&ipsec_acquire_mtx);
}

/*
 * Delete a pending IPsec acquire record.
 */
void
ipsp_delete_acquire(struct ipsec_acquire *ipa)
{
	mtx_enter(&ipsec_acquire_mtx);
	ipsp_delete_acquire_locked(ipa);
	mtx_leave(&ipsec_acquire_mtx);
}

void
ipsp_delete_acquire_locked(struct ipsec_acquire *ipa)
{
	if (timeout_del(&ipa->ipa_timeout) == 1)
		refcnt_rele(&ipa->ipa_refcnt);
	ipsp_unref_acquire_locked(ipa);
}

void
ipsec_unref_acquire(struct ipsec_acquire *ipa)
{
	mtx_enter(&ipsec_acquire_mtx);
	ipsp_unref_acquire_locked(ipa);
	mtx_leave(&ipsec_acquire_mtx);
}

void
ipsp_unref_acquire_locked(struct ipsec_acquire *ipa)
{
	MUTEX_ASSERT_LOCKED(&ipsec_acquire_mtx);

	if (refcnt_rele(&ipa->ipa_refcnt) == 0)
		return;
	TAILQ_REMOVE(&ipsec_acquire_head, ipa, ipa_next);
	TAILQ_REMOVE(&ipa->ipa_policy->ipo_acquires, ipa, ipa_ipo_next);
	ipa->ipa_policy = NULL;

	pool_put(&ipsec_acquire_pool, ipa);
}

/*
 * Find out if there's an ACQUIRE pending.
 * XXX Need a better structure.
 */
int
ipsp_pending_acquire(struct ipsec_policy *ipo, union sockaddr_union *gw)
{
	struct ipsec_acquire *ipa;

	NET_ASSERT_LOCKED();

	mtx_enter(&ipsec_acquire_mtx);
	TAILQ_FOREACH(ipa, &ipo->ipo_acquires, ipa_ipo_next) {
		if (!memcmp(gw, &ipa->ipa_addr, gw->sa.sa_len))
			break;
	}
	mtx_leave(&ipsec_acquire_mtx);

	return (ipa != NULL);
}

/*
 * Signal key management that we need an SA.
 * XXX For outgoing policies, we could try to hold on to the mbuf.
 */
int
ipsp_acquire_sa(struct ipsec_policy *ipo, union sockaddr_union *gw,
    union sockaddr_union *laddr, struct sockaddr_encap *ddst, struct mbuf *m)
{
	struct ipsec_acquire *ipa;

	NET_ASSERT_LOCKED();

	/* Check whether request has been made already. */
	if (ipsp_pending_acquire(ipo, gw))
		return 0;

	/* Add request in cache and proceed. */
	ipa = pool_get(&ipsec_acquire_pool, PR_NOWAIT|PR_ZERO);
	if (ipa == NULL)
		return ENOMEM;

	ipa->ipa_addr = *gw;

	refcnt_init(&ipa->ipa_refcnt);
	timeout_set(&ipa->ipa_timeout, ipsp_delete_acquire_timer, ipa);

	ipa->ipa_info.sen_len = ipa->ipa_mask.sen_len = SENT_LEN;
	ipa->ipa_info.sen_family = ipa->ipa_mask.sen_family = PF_KEY;

	/* Just copy the right information. */
	switch (ipo->ipo_addr.sen_type) {
	case SENT_IP4:
		ipa->ipa_info.sen_type = ipa->ipa_mask.sen_type = SENT_IP4;
		ipa->ipa_info.sen_direction = ipo->ipo_addr.sen_direction;
		ipa->ipa_mask.sen_direction = ipo->ipo_mask.sen_direction;

		if (ipsp_is_unspecified(ipo->ipo_dst)) {
			ipa->ipa_info.sen_ip_src = ddst->sen_ip_src;
			ipa->ipa_mask.sen_ip_src.s_addr = INADDR_BROADCAST;

			ipa->ipa_info.sen_ip_dst = ddst->sen_ip_dst;
			ipa->ipa_mask.sen_ip_dst.s_addr = INADDR_BROADCAST;
		} else {
			ipa->ipa_info.sen_ip_src = ipo->ipo_addr.sen_ip_src;
			ipa->ipa_mask.sen_ip_src = ipo->ipo_mask.sen_ip_src;

			ipa->ipa_info.sen_ip_dst = ipo->ipo_addr.sen_ip_dst;
			ipa->ipa_mask.sen_ip_dst = ipo->ipo_mask.sen_ip_dst;
		}

		ipa->ipa_info.sen_proto = ipo->ipo_addr.sen_proto;
		ipa->ipa_mask.sen_proto = ipo->ipo_mask.sen_proto;

		if (ipo->ipo_addr.sen_proto) {
			ipa->ipa_info.sen_sport = ipo->ipo_addr.sen_sport;
			ipa->ipa_mask.sen_sport = ipo->ipo_mask.sen_sport;

			ipa->ipa_info.sen_dport = ipo->ipo_addr.sen_dport;
			ipa->ipa_mask.sen_dport = ipo->ipo_mask.sen_dport;
		}
		break;

#ifdef INET6
	case SENT_IP6:
		ipa->ipa_info.sen_type = ipa->ipa_mask.sen_type = SENT_IP6;
		ipa->ipa_info.sen_ip6_direction =
		    ipo->ipo_addr.sen_ip6_direction;
		ipa->ipa_mask.sen_ip6_direction =
		    ipo->ipo_mask.sen_ip6_direction;

		if (ipsp_is_unspecified(ipo->ipo_dst)) {
			ipa->ipa_info.sen_ip6_src = ddst->sen_ip6_src;
			ipa->ipa_mask.sen_ip6_src = in6mask128;

			ipa->ipa_info.sen_ip6_dst = ddst->sen_ip6_dst;
			ipa->ipa_mask.sen_ip6_dst = in6mask128;
		} else {
			ipa->ipa_info.sen_ip6_src = ipo->ipo_addr.sen_ip6_src;
			ipa->ipa_mask.sen_ip6_src = ipo->ipo_mask.sen_ip6_src;

			ipa->ipa_info.sen_ip6_dst = ipo->ipo_addr.sen_ip6_dst;
			ipa->ipa_mask.sen_ip6_dst = ipo->ipo_mask.sen_ip6_dst;
		}

		ipa->ipa_info.sen_ip6_proto = ipo->ipo_addr.sen_ip6_proto;
		ipa->ipa_mask.sen_ip6_proto = ipo->ipo_mask.sen_ip6_proto;

		if (ipo->ipo_mask.sen_ip6_proto) {
			ipa->ipa_info.sen_ip6_sport =
			    ipo->ipo_addr.sen_ip6_sport;
			ipa->ipa_mask.sen_ip6_sport =
			    ipo->ipo_mask.sen_ip6_sport;
			ipa->ipa_info.sen_ip6_dport =
			    ipo->ipo_addr.sen_ip6_dport;
			ipa->ipa_mask.sen_ip6_dport =
			    ipo->ipo_mask.sen_ip6_dport;
		}
		break;
#endif /* INET6 */

	default:
		pool_put(&ipsec_acquire_pool, ipa);
		return 0;
	}

	mtx_enter(&ipsec_acquire_mtx);
#ifdef IPSEC
	if (timeout_add_sec(&ipa->ipa_timeout,
	    atomic_load_int(&ipsec_expire_acquire)) == 1)
		refcnt_take(&ipa->ipa_refcnt);
#endif
	TAILQ_INSERT_TAIL(&ipsec_acquire_head, ipa, ipa_next);
	TAILQ_INSERT_TAIL(&ipo->ipo_acquires, ipa, ipa_ipo_next);
	ipa->ipa_policy = ipo;
	mtx_leave(&ipsec_acquire_mtx);

	/* PF_KEYv2 notification message. */
	return pfkeyv2_acquire(ipo, gw, laddr, &ipa->ipa_seq, ddst);
}

/*
 * Deal with PCB security requirements.
 */
int
ipsp_spd_inp(struct mbuf *m, const struct ipsec_level *seclevel,
    struct ipsec_policy *ipo, struct tdb **tdbout)
{
	/* Sanity check. */
	if (seclevel == NULL)
		goto justreturn;

	/* We only support IPSEC_LEVEL_BYPASS or IPSEC_LEVEL_AVAIL */

	if (seclevel->sl_esp_trans == IPSEC_LEVEL_BYPASS &&
	    seclevel->sl_esp_network == IPSEC_LEVEL_BYPASS &&
	    seclevel->sl_auth == IPSEC_LEVEL_BYPASS)
		goto justreturn;

	if (seclevel->sl_esp_trans == IPSEC_LEVEL_AVAIL &&
	    seclevel->sl_esp_network == IPSEC_LEVEL_AVAIL &&
	    seclevel->sl_auth == IPSEC_LEVEL_AVAIL)
		goto justreturn;

	return -EINVAL;  /* Silently drop packet. */

 justreturn:
	if (tdbout != NULL) {
		if (ipo != NULL)
			*tdbout = tdb_ref(ipo->ipo_tdb);
		else
			*tdbout = NULL;
	}
	return 0;
}

/*
 * Find a pending ACQUIRE record based on its sequence number.
 * XXX Need to use a better data structure.
 */
struct ipsec_acquire *
ipsec_get_acquire(u_int32_t seq)
{
	struct ipsec_acquire *ipa;

	NET_ASSERT_LOCKED();

	mtx_enter(&ipsec_acquire_mtx);
	TAILQ_FOREACH(ipa, &ipsec_acquire_head, ipa_next) {
		if (ipa->ipa_seq == seq) {
			refcnt_take(&ipa->ipa_refcnt);
			break;
		}
	}
	mtx_leave(&ipsec_acquire_mtx);

	return ipa;
}
