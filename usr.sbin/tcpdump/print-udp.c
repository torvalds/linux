/*	$OpenBSD: print-udp.c,v 1.56 2020/08/17 06:29:29 dlg Exp $	*/

/*
 * Copyright (c) 1988, 1989, 1990, 1991, 1992, 1993, 1994, 1995, 1996
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that: (1) source code distributions
 * retain the above copyright notice and this paragraph in its entirety, (2)
 * distributions including binary code include the above copyright notice and
 * this paragraph in its entirety in the documentation or other materials
 * provided with the distribution, and (3) all advertising materials mentioning
 * features or use of this software display the following acknowledgement:
 * ``This product includes software developed by the University of California,
 * Lawrence Berkeley Laboratory and its contributors.'' Neither the name of
 * the University nor the names of its contributors may be used to endorse
 * or promote products derived from this software without specific prior
 * written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#include <sys/time.h>
#include <sys/socket.h>

#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>
#include <netinet/ip_var.h>
#include <netinet/udp.h>
#include <netinet/udp_var.h>

#include <net80211/ieee80211.h>

#ifdef NOERROR
#undef NOERROR					/* Solaris sucks */
#endif
#ifdef T_UNSPEC
#undef T_UNSPEC					/* SINIX does too */
#endif
#include <arpa/nameser.h>
#ifdef SEGSIZE
#undef SEGSIZE
#endif
#include <arpa/tftp.h>

#include <rpc/rpc.h>

#include <stdio.h>
#include <string.h>

#include "interface.h"
#include "addrtoname.h"
#include "extract.h"
#include "appletalk.h"

#include "nfsv2.h"
#include "bootp.h"
#include "iapp.h"

struct rtcphdr {
	u_short rh_flags;	/* T:2 P:1 CNT:5 PT:8 */
	u_short rh_len;		/* length of message (in words) */
	u_int rh_ssrc;		/* synchronization src id */
};

typedef struct {
	u_int upper;		/* more significant 32 bits */
	u_int lower;		/* less significant 32 bits */
} ntp64;

/*
 * Sender report.
 */
struct rtcp_sr {
	ntp64 sr_ntp;		/* 64-bit ntp timestamp */
	u_int sr_ts;		/* reference media timestamp */
	u_int sr_np;		/* no. packets sent */
	u_int sr_nb;		/* no. bytes sent */
};

/*
 * Receiver report.
 * Time stamps are middle 32-bits of ntp timestamp.
 */
struct rtcp_rr {
	u_int rr_srcid;		/* sender being reported */
	u_int rr_nl;		/* no. packets lost */
	u_int rr_ls;		/* extended last seq number received */
	u_int rr_dv;		/* jitter (delay variance) */
	u_int rr_lsr;		/* orig. ts from last rr from this src  */
	u_int rr_dlsr;		/* time from recpt of last rr to xmit time */
};

/*XXX*/
#define RTCP_PT_SR	200
#define RTCP_PT_RR	201
#define RTCP_PT_SDES	202
#define 	RTCP_SDES_CNAME	1
#define 	RTCP_SDES_NAME	2
#define 	RTCP_SDES_EMAIL	3
#define 	RTCP_SDES_PHONE	4
#define 	RTCP_SDES_LOC	5
#define 	RTCP_SDES_TOOL	6
#define 	RTCP_SDES_NOTE	7
#define 	RTCP_SDES_PRIV	8
#define RTCP_PT_BYE	203
#define RTCP_PT_APP	204

static void
vat_print(const void *hdr, u_int len, const struct udphdr *up)
{
	/* vat/vt audio */
	u_int ts = *(u_short *)hdr;
	if ((ts & 0xf060) != 0) {
		/* probably vt */
		printf("udp/vt %u %d / %d",
		    (u_int32_t)(ntohs(up->uh_ulen) - sizeof(*up)),
		    ts & 0x3ff, ts >> 10);
	} else {
		/* probably vat */
		u_int i0 = ntohl(((u_int *)hdr)[0]);
		u_int i1 = ntohl(((u_int *)hdr)[1]);
		printf("udp/vat %u c%d %u%s",
		    (u_int32_t)(ntohs(up->uh_ulen) - sizeof(*up) - 8),
		    i0 & 0xffff,
		    i1, i0 & 0x800000? "*" : "");
		/* audio format */
		if (i0 & 0x1f0000)
			printf(" f%d", (i0 >> 16) & 0x1f);
		if (i0 & 0x3f000000)
			printf(" s%d", (i0 >> 24) & 0x3f);
	}
}

static void
rtp_print(const void *hdr, u_int len, const struct udphdr *up)
{
	/* rtp v1 or v2 */
	u_int *ip = (u_int *)hdr;
	u_int hasopt, hasext, contype, hasmarker;
	u_int i0 = ntohl(((u_int *)hdr)[0]);
	u_int i1 = ntohl(((u_int *)hdr)[1]);
	u_int dlen = ntohs(up->uh_ulen) - sizeof(*up) - 8;
	const char * ptype;

	ip += 2;
	len >>= 2;
	len -= 2;
	hasopt = 0;
	hasext = 0;
	if ((i0 >> 30) == 1) {
		/* rtp v1 */
		hasopt = i0 & 0x800000;
		contype = (i0 >> 16) & 0x3f;
		hasmarker = i0 & 0x400000;
		ptype = "rtpv1";
	} else {
		/* rtp v2 */
		hasext = i0 & 0x10000000;
		contype = (i0 >> 16) & 0x7f;
		hasmarker = i0 & 0x800000;
		dlen -= 4;
		ptype = "rtp";
		ip += 1;
		len -= 1;
	}
	printf(" udp/%s %d c%d %s%s %d %u",
	    ptype, dlen, contype, (hasopt || hasext)? "+" : "",
	    hasmarker? "*" : "", i0 & 0xffff, i1);
	if (vflag) {
		printf(" %u", i1);
		if (hasopt) {
			u_int i2, optlen;
			do {
				i2 = ip[0];
				optlen = (i2 >> 16) & 0xff;
				if (optlen == 0 || optlen > len) {
					printf(" !opt");
					return;
				}
				ip += optlen;
				len -= optlen;
			} while ((int)i2 >= 0);
		}
		if (hasext) {
			u_int i2, extlen;
			i2 = ip[0];
			extlen = (i2 & 0xffff) + 1;
			if (extlen > len) {
				printf(" !ext");
				return;
			}
			ip += extlen;
		}
		if (contype == 0x1f) /*XXX H.261 */
			printf(" 0x%04x", ip[0] >> 16);
	}
}

static const u_char *
rtcp_print(const u_char *hdr, const u_char *ep)
{
	/* rtp v2 control (rtcp) */
	struct rtcp_rr *rr = NULL;
	struct rtcp_sr *sr;
	struct rtcphdr *rh = (struct rtcphdr *)hdr;
	u_int len;
	u_short flags;
	int cnt;
	double ts, dts;
	if ((u_char *)(rh + 1) > ep) {
		printf(" [|rtcp]");
		return (ep);
	}
	len = (ntohs(rh->rh_len) + 1) * 4;
	flags = ntohs(rh->rh_flags);
	cnt = (flags >> 8) & 0x1f;
	switch (flags & 0xff) {
	case RTCP_PT_SR:
		sr = (struct rtcp_sr *)(rh + 1);
		printf(" sr");
		if (len != cnt * sizeof(*rr) + sizeof(*sr) + sizeof(*rh))
			printf(" [%d]", len);
		if (vflag)
			printf(" %u", (u_int32_t)ntohl(rh->rh_ssrc));
		if ((u_char *)(sr + 1) > ep) {
			printf(" [|rtcp]");
			return (ep);
		}
		ts = (double)((u_int32_t)ntohl(sr->sr_ntp.upper)) +
		    ((double)((u_int32_t)ntohl(sr->sr_ntp.lower)) /
		    4294967296.0);
		printf(" @%.2f %u %up %ub", ts, (u_int32_t)ntohl(sr->sr_ts),
		    (u_int32_t)ntohl(sr->sr_np), (u_int32_t)ntohl(sr->sr_nb));
		rr = (struct rtcp_rr *)(sr + 1);
		break;
	case RTCP_PT_RR:
		printf(" rr");
		if (len != cnt * sizeof(*rr) + sizeof(*rh))
			printf(" [%d]", len);
		rr = (struct rtcp_rr *)(rh + 1);
		if (vflag)
			printf(" %u", (u_int32_t)ntohl(rh->rh_ssrc));
		break;
	case RTCP_PT_SDES:
		printf(" sdes %d", len);
		if (vflag)
			printf(" %u", (u_int32_t)ntohl(rh->rh_ssrc));
		cnt = 0;
		break;
	case RTCP_PT_BYE:
		printf(" bye %d", len);
		if (vflag)
			printf(" %u", (u_int32_t)ntohl(rh->rh_ssrc));
		cnt = 0;
		break;
	default:
		printf(" type-0x%x %d", flags & 0xff, len);
		cnt = 0;
		break;
	}
	if (cnt > 1)
		printf(" c%d", cnt);
	while (--cnt >= 0) {
		if ((u_char *)(rr + 1) > ep) {
			printf(" [|rtcp]");
			return (ep);
		}
		if (vflag)
			printf(" %u", (u_int32_t)ntohl(rr->rr_srcid));
		ts = (double)((u_int32_t)ntohl(rr->rr_lsr)) / 65536.;
		dts = (double)((u_int32_t)ntohl(rr->rr_dlsr)) / 65536.;
		printf(" %ul %us %uj @%.2f+%.2f",
		    (u_int32_t)ntohl(rr->rr_nl) & 0x00ffffff,
		    (u_int32_t)ntohl(rr->rr_ls),
		    (u_int32_t)ntohl(rr->rr_dv), ts, dts);
	}
	return (hdr + len);
}

/* XXX probably should use getservbyname() and cache answers */
#define TFTP_PORT		69		/*XXX*/
#define KERBEROS_PORT		88		/*XXX*/
#define SUNRPC_PORT		111		/*XXX*/
#define NTP_PORT		123		/*XXX*/
#define NETBIOS_NS_PORT		137		/*XXX*/
#define NETBIOS_DGRAM_PORT	138		/*XXX*/
#define SNMP_PORT		161		/*XXX*/
#define SNMPTRAP_PORT		162		/*XXX*/
#define ISAKMP_PORT		500		/*XXX*/
#define RIP_PORT		520		/*XXX*/
#define TIMED_PORT		525		/*XXX*/
#define KERBEROS_SEC_PORT	750		/*XXX*/
#define LWRES_PORT		921
#define VQP_PORT		1589
#define OLD_RADIUS_AUTH_PORT	1645
#define OLD_RADIUS_ACCT_PORT	1646
#define L2TP_PORT		1701		/*XXX*/
#define RADIUS_AUTH_PORT	1812
#define RADIUS_ACCT_PORT	1813
#define HSRP_PORT		1985		/*XXX*/
#define GTP_C_PORT		2123
#define GTP_U_PORT		2152
#define GTP_PRIME_PORT		3386
#define UDPENCAP_PORT		4500		/*XXX*/
#define GRE_PORT		4754
#define VXLAN_PORT		4789
#define VXLAN_GPE_PORT		4790
#define GENEVE_PORT		6081
#define MULTICASTDNS_PORT	5353
#define MPLS_PORT		6635

#define RIPNG_PORT		521		/*XXX*/
#define DHCP6_PORT1		546		/*XXX*/
#define DHCP6_PORT2		547		/*XXX*/

void
udp_print(const u_char *bp, u_int length, const void *iph)
{
	const struct udphdr *up;
	const u_char *cp;
	const u_char *ep = bp + length;
	u_int16_t sport, dport, ulen;
	const char *ipsrc = NULL, *ipdst = NULL;
	unsigned int ipv = 0;
	uint32_t cksum = 0;

	if (ep > snapend)
		ep = snapend;

	if (iph != NULL) {
		const struct ip *ip = iph;
		ipv = ip->ip_v;

		switch (ipv) {
		case 6: {
			const struct ip6_hdr *ip6 = iph;

			ipsrc = ip6addr_string(&ip6->ip6_src);
			ipdst = ip6addr_string(&ip6->ip6_dst);

			cksum = in_cksum_add(&ip6->ip6_src,
			    sizeof(ip6->ip6_src), cksum);
			cksum = in_cksum_add(&ip6->ip6_dst,
			    sizeof(ip6->ip6_dst), cksum);
			break;
		}
		case 4:
			ipsrc = ipaddr_string(&ip->ip_src);
			ipdst = ipaddr_string(&ip->ip_dst);

			cksum = in_cksum_add(&ip->ip_src,
			    sizeof(ip->ip_src), cksum);
			cksum = in_cksum_add(&ip->ip_dst,
			    sizeof(ip->ip_dst), cksum);
			break;
		}
	}

	up = (const struct udphdr *)bp;
	cp = (const u_char *)(up + 1);

	/* check if the udp header was captured */
	if (cp > snapend) {
		if (ipv)
			printf("%s > %s: ", ipsrc, ipdst);

		printf("[|udp]");
		return;
	}

	/* check if the packet payload is long enough */
	if (length < sizeof(*up)) {
		if (ipv)
			printf("%s > %s: ", ipsrc, ipdst);

		printf("truncated-udp %u", length);
		return;
	}

	sport = ntohs(up->uh_sport);
	dport = ntohs(up->uh_dport);

	if (ipv) {
		printf("%s.%s > %s.%s",
		    ipsrc, udpport_string(sport),
		    ipdst, udpport_string(dport));
	} else {
		printf("udp %s > %s",
		    udpport_string(sport),
		    udpport_string(dport));
	}

	printf(": ");

	cksum += htons(length);

	ulen = ntohs(up->uh_ulen);
	if (length < ulen)
		printf(" truncated-udp - %u bytes missing!", ulen - length);

	length -= sizeof(*up);

	if (vflag && ipv && TTEST2(cp[0], length)) {
		uint16_t sum, usum = up->uh_sum;

		if (usum == 0) {
			if (ipv == 4)
				printf("[no udp cksum] ");
			else		
				printf("[invalid udp cksum 0] ");
		} else {
			cksum += htons(IPPROTO_UDP);
			cksum += up->uh_sport;
			cksum += up->uh_dport;
			cksum += up->uh_ulen;

			sum = in_cksum(cp, length, cksum);

			if (sum == usum)
				printf("[udp sum ok] ");
			else {
				printf("[bad udp cksum %04x! -> %04x] ",
				    usum, sum);
			}
		}
	}

	if (packettype) {
		struct rpc_msg *rp;
		enum msg_type direction;

		switch (packettype) {
		case PT_VAT:
			vat_print(cp, length, up);
			break;

		case PT_WB:
			wb_print(cp, length);
			break;

		case PT_RPC:
			rp = (struct rpc_msg *)cp;
			direction = (enum msg_type)ntohl(rp->rm_direction);
			if (direction == CALL)
				sunrpcrequest_print(cp, length, iph);
			else
				nfsreply_print(cp, length, iph);
			break;

		case PT_RTP:
			rtp_print(cp, length, up);
			break;

		case PT_RTCP:
			while (cp < ep)
				cp = rtcp_print(cp, ep);
			break;
		case PT_CNFP:
			cnfp_print(cp, length);
			break;
		case PT_GRE:
			gre_print(cp, length);
			break;
		case PT_VXLAN:
			vxlan_print(cp, length);
			break;
		case PT_GENEVE:
			geneve_print(cp, length);
			break;
		case PT_MPLS:
			mpls_print(cp, length);
			break;
		case PT_TFTP:
			tftp_print(cp, length);
			break;
		case PT_WIREGUARD:
			wg_print(cp, length);
			break;
		}
		return;
	}

	if (!qflag) {
		struct rpc_msg *rp;
		enum msg_type direction;

		rp = (struct rpc_msg *)cp;
		if (TTEST(rp->rm_direction)) {
			direction = (enum msg_type)ntohl(rp->rm_direction);
			if (dport == NFS_PORT && direction == CALL) {
				nfsreq_print(cp, length, iph);
				return;
			}
			if (sport == NFS_PORT && direction == REPLY) {
				nfsreply_print(cp, length, iph);
				return;
			}
#ifdef notdef
			if (dport == SUNRPC_PORT && direction == CALL) {
				sunrpcrequest_print(cp, length, iph);
				return;
			}
#endif
		}
		if (TTEST(((struct LAP *)cp)->type) &&
		    ((struct LAP *)cp)->type == lapDDP &&
		    (atalk_port(sport) || atalk_port(dport))) {
			if (vflag)
				printf("kip ");
			atalk_print_llap(cp, length);
			return;
		}
	}

	if (!qflag) {
#define ISPORT(p) (dport == (p) || sport == (p))
		if (ISPORT(NAMESERVER_PORT))
			ns_print(cp, length, 0);
		else if (ISPORT(MULTICASTDNS_PORT))
			ns_print(cp, length, 1);
		else if (ISPORT(LWRES_PORT))
			lwres_print(cp, length);
		else if (ISPORT(TIMED_PORT))
			timed_print(cp, length);
		else if (ISPORT(TFTP_PORT))
			tftp_print(cp, length);
		else if (ISPORT(IPPORT_BOOTPC) || ISPORT(IPPORT_BOOTPS))
			bootp_print(cp, length, sport, dport);
		else if (ISPORT(RIP_PORT))
			rip_print(cp, length);
		else if (ISPORT(SNMP_PORT) || ISPORT(SNMPTRAP_PORT))
			snmp_print(cp, length);
		else if (ISPORT(NTP_PORT))
			ntp_print(cp, length);
		else if (ISPORT(KERBEROS_PORT) || ISPORT(KERBEROS_SEC_PORT))
			krb_print(cp, length);
		else if (ISPORT(L2TP_PORT))
			l2tp_print(cp, length);
		else if (ISPORT(UDPENCAP_PORT))
			udpencap_print(cp, length, iph);
		else if (ISPORT(ISAKMP_PORT))
			ike_print(cp, length);
#if 0
		else if (ISPORT(NETBIOS_NS_PORT))
			nbt_udp137_print(cp, length);
		else if (ISPORT(NETBIOS_DGRAM_PORT))
			nbt_udp138_print(cp, length);
#endif
                else if (ISPORT(OLD_RADIUS_AUTH_PORT) ||
                         ISPORT(OLD_RADIUS_ACCT_PORT) ||
                         ISPORT(RADIUS_AUTH_PORT)     ||
                         ISPORT(RADIUS_ACCT_PORT))
                        radius_print(cp, length);
		else if (dport == 3456)
			vat_print(cp, length, up);
		else if (ISPORT(IAPP_PORT) || ISPORT(IAPP_OLD_PORT))
			iapp_print(cp, length);
		else if (ISPORT(VQP_PORT))
			vqp_print(cp, length);
		else if (ISPORT(GRE_PORT))
			gre_print(cp, length);
		else if (ISPORT(VXLAN_PORT) || ISPORT(VXLAN_GPE_PORT))
			vxlan_print(cp, length);
		else if (ISPORT(GENEVE_PORT))
			geneve_print(cp, length);
		else if (ISPORT(MPLS_PORT))
			mpls_print(cp, length);
		else if (ISPORT(RIPNG_PORT))
			ripng_print(cp, length);
		else if (ISPORT(DHCP6_PORT1) || ISPORT(DHCP6_PORT2))
			dhcp6_print(cp, length);
		else if (ISPORT(GTP_C_PORT) || ISPORT(GTP_U_PORT) ||
		    ISPORT(GTP_PRIME_PORT))
			gtp_print(cp, length, sport, dport);
		/*
		 * Kludge in test for whiteboard packets.
		 */
		else if (dport == 4567)
			wb_print(cp, length);
		else if (dport == HSRP_PORT)
			hsrp_print(cp, length);
		else if (wg_match(cp, length))
			wg_print(cp, length);
		else
			printf("udp %u", length);
#undef ISPORT
	} else
		printf("udp %u", length);
}
