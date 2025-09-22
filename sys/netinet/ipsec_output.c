/*	$OpenBSD: ipsec_output.c,v 1.105 2025/07/08 00:47:41 jsg Exp $ */
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

#include "pf.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/timeout.h>

#include <net/if.h>
#include <net/route.h>

#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>
#include <netinet/ip_ipsp.h>
#include <netinet/ip_var.h>
#include <netinet6/ip6_var.h>

#if NPF > 0
#include <net/pfvar.h>
#endif

#include <netinet/udp.h>
#include <netinet/ip_ipip.h>
#include <netinet/ip_ah.h>
#include <netinet/ip_esp.h>
#include <netinet/ip_ipcomp.h>

#include <crypto/xform.h>

/*
 * Locks used to protect data:
 *	a	atomic
 */

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

int	udpencap_enable = 1;	/* [a] enabled by default */
int	udpencap_port = 4500;	/* [a] triggers decapsulation */

/*
 * Loop over a tdb chain, taking into consideration protocol tunneling. The
 * fourth argument is set if the first encapsulation header is already in
 * place.
 */
int
ipsp_process_packet(struct mbuf *m, struct tdb *tdb, int af, int tunalready,
    int setdf)
{
	int hlen, off, error;
#ifdef INET6
	struct ip6_ext ip6e;
	int nxt;
	int dstopt = 0;
#endif

	struct ip *ip;
#ifdef INET6
	struct ip6_hdr *ip6;
#endif /* INET6 */

#ifdef ENCDEBUG
	char buf[INET6_ADDRSTRLEN];
#endif

	/* Check that the transform is allowed by the administrator. */
	if ((tdb->tdb_sproto == IPPROTO_ESP && !atomic_load_int(&esp_enable)) ||
	    (tdb->tdb_sproto == IPPROTO_AH && !atomic_load_int(&ah_enable)) ||
	    (tdb->tdb_sproto == IPPROTO_IPCOMP &&
	    !atomic_load_int(&ipcomp_enable))) {
		DPRINTF("IPsec outbound packet dropped due to policy "
		    "(check your sysctls)");
		error = EHOSTUNREACH;
		goto drop;
	}

	/* Sanity check. */
	if (!tdb->tdb_xform) {
		DPRINTF("uninitialized TDB");
		error = EHOSTUNREACH;
		goto drop;
	}

	/* Check if the SPI is invalid. */
	if (tdb->tdb_flags & TDBF_INVALID) {
		DPRINTF("attempt to use invalid SA %s/%08x/%u",
		    ipsp_address(&tdb->tdb_dst, buf, sizeof(buf)),
		    ntohl(tdb->tdb_spi), tdb->tdb_sproto);
		error = ENXIO;
		goto drop;
	}

	/* Check that the network protocol is supported */
	switch (tdb->tdb_dst.sa.sa_family) {
	case AF_INET:
		break;

#ifdef INET6
	case AF_INET6:
		break;
#endif /* INET6 */

	default:
		DPRINTF("attempt to use SA %s/%08x/%u for protocol family %d",
		    ipsp_address(&tdb->tdb_dst, buf, sizeof(buf)),
		    ntohl(tdb->tdb_spi), tdb->tdb_sproto,
		    tdb->tdb_dst.sa.sa_family);
		error = EPFNOSUPPORT;
		goto drop;
	}

	/*
	 * Register first use if applicable, setup relevant expiration timer.
	 */
	if (tdb->tdb_first_use == 0) {
		tdb->tdb_first_use = gettime();
		if (tdb->tdb_flags & TDBF_FIRSTUSE) {
			if (timeout_add_sec(&tdb->tdb_first_tmo,
			    tdb->tdb_exp_first_use))
				tdb_ref(tdb);
		}
		if (tdb->tdb_flags & TDBF_SOFT_FIRSTUSE) {
			if (timeout_add_sec(&tdb->tdb_sfirst_tmo,
			    tdb->tdb_soft_first_use))
				tdb_ref(tdb);
		}
	}

	/*
	 * Check for tunneling if we don't have the first header in place.
	 * When doing Ethernet-over-IP, we are handed an already-encapsulated
	 * frame, so we don't need to re-encapsulate.
	 */
	if (tunalready == 0) {
		/*
		 * If the target protocol family is different, we know we'll be
		 * doing tunneling.
		 */
		if (af == tdb->tdb_dst.sa.sa_family) {
			switch (af) {
			case AF_INET:
				hlen = sizeof(struct ip);
				break;
#ifdef INET6
			case AF_INET6:
				hlen = sizeof(struct ip6_hdr);
				break;
#endif /* INET6 */
			}

			/* Bring the network header in the first mbuf. */
			if (m->m_len < hlen) {
				if ((m = m_pullup(m, hlen)) == NULL) {
					error = ENOBUFS;
					goto drop;
				}
			}

			if (af == AF_INET) {
				ip = mtod(m, struct ip *);

				/*
				 * This is not a bridge packet, remember if we
				 * had IP_DF.
				 */
				if (setdf == IPSP_DF_INHERIT) {
					setdf =
					    ISSET(ip->ip_off, htons(IP_DF)) ?
					     IPSP_DF_ON : IPSP_DF_OFF;
				}
			}

#ifdef INET6
			if (af == AF_INET6)
				ip6 = mtod(m, struct ip6_hdr *);
#endif /* INET6 */
		}

		/* Do the appropriate encapsulation, if necessary. */
		if ((tdb->tdb_dst.sa.sa_family != af) || /* PF mismatch */
		    (tdb->tdb_flags & TDBF_TUNNELING) || /* Tunneling needed */
		    (tdb->tdb_xform->xf_type == XF_IP4) || /* ditto */
		    ((tdb->tdb_dst.sa.sa_family == AF_INET) &&
		     (tdb->tdb_dst.sin.sin_addr.s_addr != INADDR_ANY) &&
		     (tdb->tdb_dst.sin.sin_addr.s_addr != ip->ip_dst.s_addr)) ||
#ifdef INET6
		    ((tdb->tdb_dst.sa.sa_family == AF_INET6) &&
		     (!IN6_IS_ADDR_UNSPECIFIED(&tdb->tdb_dst.sin6.sin6_addr)) &&
		     (!IN6_ARE_ADDR_EQUAL(&tdb->tdb_dst.sin6.sin6_addr,
		      &ip6->ip6_dst))) ||
#endif /* INET6 */
		    0) {
			/* Fix IPv4 header checksum and length. */
			if (af == AF_INET) {
				if (m->m_len < sizeof(struct ip))
					if ((m = m_pullup(m,
					    sizeof(struct ip))) == NULL) {
						error = ENOBUFS;
						goto drop;
					}

				ip = mtod(m, struct ip *);
				ip->ip_len = htons(m->m_pkthdr.len);
				ip->ip_sum = 0;
				ip->ip_sum = in_cksum(m, ip->ip_hl << 2);
			}

#ifdef INET6
			/* Fix IPv6 header payload length. */
			if (af == AF_INET6) {
				if (m->m_len < sizeof(struct ip6_hdr))
					if ((m = m_pullup(m,
					    sizeof(struct ip6_hdr))) == NULL) {
						error = ENOBUFS;
						goto drop;
					}

				if (m->m_pkthdr.len - sizeof(*ip6) >
				    IPV6_MAXPACKET) {
					/* No jumbogram support. */
					error = ENXIO;	/*?*/
					goto drop;
				}
				ip6 = mtod(m, struct ip6_hdr *);
				ip6->ip6_plen = htons(m->m_pkthdr.len
				    - sizeof(*ip6));
			}
#endif /* INET6 */

			/* Encapsulate -- m may be changed or set to NULL. */
			error = ipip_output(&m, tdb);
			if ((m == NULL) && (!error))
				error = EFAULT;
			if (error)
				goto drop;

			if (tdb->tdb_dst.sa.sa_family == AF_INET &&
			    setdf == IPSP_DF_ON) {
				if (m->m_len < sizeof(struct ip))
					if ((m = m_pullup(m,
					    sizeof(struct ip))) == NULL) {
						error = ENOBUFS;
						goto drop;
					}

				ip = mtod(m, struct ip *);
				ip->ip_off |= htons(IP_DF);
			}

			/* Remember that we appended a tunnel header. */
			mtx_enter(&tdb->tdb_mtx);
			tdb->tdb_flags |= TDBF_USEDTUNNEL;
			mtx_leave(&tdb->tdb_mtx);
		}
	}

	/*
	 * If this is just an IP-IP TDB and we're told there's already an
	 * encapsulation header or ipip_output() has encapsulated it, move on.
	 */
	if (tdb->tdb_xform->xf_type == XF_IP4)
		return ipsp_process_done(m, tdb);

	/* Extract some information off the headers. */
	switch (tdb->tdb_dst.sa.sa_family) {
	case AF_INET:
		ip = mtod(m, struct ip *);
		hlen = ip->ip_hl << 2;
		off = offsetof(struct ip, ip_p);
		break;

#ifdef INET6
	case AF_INET6:
		ip6 = mtod(m, struct ip6_hdr *);
		hlen = sizeof(struct ip6_hdr);
		off = offsetof(struct ip6_hdr, ip6_nxt);
		nxt = ip6->ip6_nxt;
		/*
		 * chase mbuf chain to find the appropriate place to
		 * put AH/ESP/IPcomp header.
		 *	IPv6 hbh dest1 rthdr ah* [esp* dest2 payload]
		 */
		do {
			switch (nxt) {
			case IPPROTO_AH:
			case IPPROTO_ESP:
			case IPPROTO_IPCOMP:
				/*
				 * we should not skip security header added
				 * beforehand.
				 */
				goto exitip6loop;

			case IPPROTO_HOPOPTS:
			case IPPROTO_DSTOPTS:
			case IPPROTO_ROUTING:
				/*
				 * if we see 2nd destination option header,
				 * we should stop there.
				 */
				if (nxt == IPPROTO_DSTOPTS && dstopt)
					goto exitip6loop;

				if (nxt == IPPROTO_DSTOPTS) {
					/*
					 * seen 1st or 2nd destination option.
					 * next time we see one, it must be 2nd.
					 */
					dstopt = 1;
				} else if (nxt == IPPROTO_ROUTING) {
					/*
					 * if we see destination option next
					 * time, it must be dest2.
					 */
					dstopt = 2;
				}
				if (m->m_pkthdr.len < hlen + sizeof(ip6e)) {
					error = EINVAL;
					goto drop;
				}
				/* skip this header */
				m_copydata(m, hlen, sizeof(ip6e),
				    (caddr_t)&ip6e);
				nxt = ip6e.ip6e_nxt;
				off = hlen + offsetof(struct ip6_ext, ip6e_nxt);
				/*
				 * we will never see nxt == IPPROTO_AH
				 * so it is safe to omit AH case.
				 */
				hlen += (ip6e.ip6e_len + 1) << 3;
				break;
			default:
				goto exitip6loop;
			}
		} while (hlen < m->m_pkthdr.len);
	exitip6loop:
		break;
#endif /* INET6 */
	default:
		error = EPFNOSUPPORT;
		goto drop;
	}

	if (m->m_pkthdr.len < hlen) {
		error = EINVAL;
		goto drop;
	}

	ipsecstat_add(ipsec_ouncompbytes, m->m_pkthdr.len);
	tdbstat_add(tdb, tdb_ouncompbytes, m->m_pkthdr.len);

	/* Non expansion policy for IPCOMP */
	if (tdb->tdb_sproto == IPPROTO_IPCOMP) {
		if ((m->m_pkthdr.len - hlen) < tdb->tdb_compalgxform->minlen) {
			/* No need to compress, leave the packet untouched */
			ipcompstat_inc(ipcomps_minlen);
			return ipsp_process_done(m, tdb);
		}
	}

	/* Invoke the IPsec transform. */
	return (*(tdb->tdb_xform->xf_output))(m, tdb, hlen, off);

 drop:
	m_freem(m);
	return error;
}

/*
 * Called by the IPsec output transform callbacks, to transmit the packet
 * or do further processing, as necessary.
 */
int
ipsp_process_done(struct mbuf *m, struct tdb *tdb)
{
	struct ip *ip;
#ifdef INET6
	struct ip6_hdr *ip6;
#endif /* INET6 */
	struct tdb *tdbo;
	struct tdb_ident *tdbi;
	struct m_tag *mtag;
	int roff, error;

	NET_ASSERT_LOCKED();

	tdb->tdb_last_used = gettime();

	if ((tdb->tdb_flags & TDBF_UDPENCAP) != 0) {
		struct mbuf *mi;
		struct udphdr *uh;
		int iphlen;
		int udpencap_port_local = atomic_load_int(&udpencap_port);

		if (!atomic_load_int(&udpencap_enable) ||
		    !udpencap_port_local) {
			error = ENXIO;
			goto drop;
		}

		switch (tdb->tdb_dst.sa.sa_family) {
		case AF_INET:
			iphlen = sizeof(struct ip);
			break;
#ifdef INET6
		case AF_INET6:
			iphlen = sizeof(struct ip6_hdr);
			break;
#endif /* INET6 */
		default:
			DPRINTF("unknown protocol family (%d)",
			    tdb->tdb_dst.sa.sa_family);
			error = EPFNOSUPPORT;
			goto drop;
		}

		mi = m_makespace(m, iphlen, sizeof(struct udphdr), &roff);
		if (mi == NULL) {
			error = ENOMEM;
			goto drop;
		}
		uh = (struct udphdr *)(mtod(mi, caddr_t) + roff);
		uh->uh_sport = uh->uh_dport = htons(udpencap_port_local);
		if (tdb->tdb_udpencap_port)
			uh->uh_dport = tdb->tdb_udpencap_port;

		uh->uh_ulen = htons(m->m_pkthdr.len - iphlen);
		uh->uh_sum = 0;
#ifdef INET6
		if (tdb->tdb_dst.sa.sa_family == AF_INET6)
			m->m_pkthdr.csum_flags |= M_UDP_CSUM_OUT;
#endif /* INET6 */
		espstat_inc(esps_udpencout);
	}

	switch (tdb->tdb_dst.sa.sa_family) {
	case AF_INET:
		/* Fix the header length, for AH processing. */
		ip = mtod(m, struct ip *);
		ip->ip_len = htons(m->m_pkthdr.len);
		if ((tdb->tdb_flags & TDBF_UDPENCAP) != 0)
			ip->ip_p = IPPROTO_UDP;
		break;

#ifdef INET6
	case AF_INET6:
		/* Fix the header length, for AH processing. */
		if (m->m_pkthdr.len < sizeof(*ip6)) {
			error = ENXIO;
			goto drop;
		}
		if (m->m_pkthdr.len - sizeof(*ip6) > IPV6_MAXPACKET) {
			/* No jumbogram support. */
			error = ENXIO;
			goto drop;
		}
		ip6 = mtod(m, struct ip6_hdr *);
		ip6->ip6_plen = htons(m->m_pkthdr.len - sizeof(*ip6));
		if ((tdb->tdb_flags & TDBF_UDPENCAP) != 0)
			ip6->ip6_nxt = IPPROTO_UDP;
		break;
#endif /* INET6 */

	default:
		DPRINTF("unknown protocol family (%d)",
		    tdb->tdb_dst.sa.sa_family);
		error = EPFNOSUPPORT;
		goto drop;
	}

	/*
	 * Add a record of what we've done or what needs to be done to the
	 * packet.
	 */
	mtag = m_tag_get(PACKET_TAG_IPSEC_OUT_DONE, sizeof(struct tdb_ident),
	    M_NOWAIT);
	if (mtag == NULL) {
		DPRINTF("could not allocate packet tag");
		error = ENOMEM;
		goto drop;
	}

	tdbi = (struct tdb_ident *)(mtag + 1);
	tdbi->dst = tdb->tdb_dst;
	tdbi->proto = tdb->tdb_sproto;
	tdbi->spi = tdb->tdb_spi;
	tdbi->rdomain = tdb->tdb_rdomain;

	m_tag_prepend(m, mtag);

	ipsecstat_pkt(ipsec_opackets, ipsec_obytes, m->m_pkthdr.len);
	tdbstat_pkt(tdb, tdb_opackets, tdb_obytes, m->m_pkthdr.len);

	/* If there's another (bundled) TDB to apply, do so. */
	tdbo = tdb_ref(tdb->tdb_onext);
	if (tdbo != NULL) {
		KERNEL_ASSERT_LOCKED();
		error = ipsp_process_packet(m, tdbo,
		    tdb->tdb_dst.sa.sa_family, 0, IPSP_DF_INHERIT);
		tdb_unref(tdbo);
		return error;
	}

#if NPF > 0
	/* Add pf tag if requested. */
	pf_tag_packet(m, tdb->tdb_tag, -1);
	pf_pkt_addr_changed(m);
#endif
	if (tdb->tdb_rdomain != tdb->tdb_rdomain_post)
		m->m_pkthdr.ph_rtableid = tdb->tdb_rdomain_post;

	/*
	 * We're done with IPsec processing, transmit the packet using the
	 * appropriate network protocol (IP or IPv6). SPD lookup will be
	 * performed again there.
	 */
	switch (tdb->tdb_dst.sa.sa_family) {
	case AF_INET:
		error = ip_output(m, NULL, NULL, IP_RAWOUTPUT, NULL, NULL, 0);
		break;
#ifdef INET6
	case AF_INET6:
		/*
		 * We don't need massage, IPv6 header fields are always in
		 * net endian.
		 */
		error = ip6_output(m, NULL, NULL, 0, NULL, NULL);
		break;
#endif /* INET6 */
	default:
		error = EPFNOSUPPORT;
		break;
	}
	return error;

 drop:
	m_freem(m);
	return error;
}

ssize_t
ipsec_hdrsz(struct tdb *tdbp)
{
	ssize_t adjust;

	switch (tdbp->tdb_sproto) {
	case IPPROTO_IPIP:
		adjust = 0;
		break;

	case IPPROTO_ESP:
		if (tdbp->tdb_encalgxform == NULL)
			return (-1);

		/* Header length */
		adjust = 2 * sizeof(u_int32_t) + tdbp->tdb_ivlen;
		if (tdbp->tdb_flags & TDBF_UDPENCAP)
			adjust += sizeof(struct udphdr);
		/* Authenticator */
		if (tdbp->tdb_authalgxform != NULL)
			adjust += tdbp->tdb_authalgxform->authsize;
		/* Padding */
		adjust += MAX(4, tdbp->tdb_encalgxform->blocksize);
		break;

	case IPPROTO_AH:
		if (tdbp->tdb_authalgxform == NULL)
			return (-1);

		adjust = AH_FLENGTH + sizeof(u_int32_t);
		adjust += tdbp->tdb_authalgxform->authsize;
		break;

	default:
		return (-1);
	}

	if (!(tdbp->tdb_flags & TDBF_TUNNELING) &&
	    !(tdbp->tdb_flags & TDBF_USEDTUNNEL))
		return (adjust);

	switch (tdbp->tdb_dst.sa.sa_family) {
	case AF_INET:
		adjust += sizeof(struct ip);
		break;
#ifdef INET6
	case AF_INET6:
		adjust += sizeof(struct ip6_hdr);
		break;
#endif /* INET6 */
	}

	return (adjust);
}

void
ipsec_adjust_mtu(struct mbuf *m, u_int32_t mtu)
{
	struct tdb_ident *tdbi;
	struct tdb *tdbp;
	struct m_tag *mtag;
	ssize_t adjust;

	NET_ASSERT_LOCKED();

	for (mtag = m_tag_find(m, PACKET_TAG_IPSEC_OUT_DONE, NULL); mtag;
	     mtag = m_tag_find(m, PACKET_TAG_IPSEC_OUT_DONE, mtag)) {
		tdbi = (struct tdb_ident *)(mtag + 1);
		tdbp = gettdb(tdbi->rdomain, tdbi->spi, &tdbi->dst,
		    tdbi->proto);
		if (tdbp == NULL)
			break;

		if ((adjust = ipsec_hdrsz(tdbp)) == -1) {
			tdb_unref(tdbp);
			break;
		}

		mtu -= adjust;
		tdbp->tdb_mtu = mtu;
		tdbp->tdb_mtutimeout = gettime() +
		    atomic_load_int(&ip_mtudisc_timeout);
		DPRINTF("spi %08x mtu %d adjust %ld mbuf %p",
		    ntohl(tdbp->tdb_spi), tdbp->tdb_mtu, adjust, m);
		tdb_unref(tdbp);
	}
}
