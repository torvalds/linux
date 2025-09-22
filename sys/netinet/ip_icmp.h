/*	$OpenBSD: ip_icmp.h,v 1.33 2025/03/02 21:28:32 bluhm Exp $	*/
/*	$NetBSD: ip_icmp.h,v 1.10 1996/02/13 23:42:28 christos Exp $	*/

/*
 * Copyright (c) 1982, 1986, 1993
 *	The Regents of the University of California.  All rights reserved.
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
 *	@(#)ip_icmp.h	8.1 (Berkeley) 6/10/93
 */

#ifndef _NETINET_IP_ICMP_H_
#define _NETINET_IP_ICMP_H_

/*
 * Interface Control Message Protocol Definitions.
 * Per RFC 792, September 1981.
 * RFC 950, August 1985. (Address Mask Request / Reply)
 * RFC 1256, September 1991. (Router Advertisement and Solicitation)
 * RFC 1108, November 1991. (Param Problem, Missing Req. Option)
 * RFC 1393, January 1993. (Traceroute)
 * RFC 1475, June 1993. (Datagram Conversion Error)
 * RFC 1812, June 1995. (adm prohib, host precedence, precedence cutoff)
 * RFC 2002, October 1996. (Mobility changes to Router Advertisement)
 */

/*
 * ICMP Router Advertisement data
 */
struct icmp_ra_addr {
	u_int32_t ira_addr;
	u_int32_t ira_preference;
};

/*
 * Structure of an icmp header.
 */
struct icmp {
	u_int8_t  icmp_type;		/* type of message, see below */
	u_int8_t  icmp_code;		/* type sub code */
	u_int16_t icmp_cksum;		/* ones complement cksum of struct */
	union {
		u_int8_t  ih_pptr;	/* ICMP_PARAMPROB */
		struct ih_exthdr {	/* RFC4884 extended header */
			u_int8_t  iex_pad;
			u_int8_t  iex_length;
		} ih_exthdr;
		struct in_addr ih_gwaddr;	/* ICMP_REDIRECT */
		struct ih_idseq {
			  u_int16_t icd_id;
			  u_int16_t icd_seq;
		} ih_idseq;
		int32_t   ih_void;

		/* ICMP_UNREACH_NEEDFRAG -- Path MTU Discovery (RFC1191) */
		struct ih_pmtu {
			  u_int16_t ipm_void;
			  u_int16_t ipm_nextmtu;
		} ih_pmtu;

		struct ih_rtradv {
			u_int8_t irt_num_addrs;
			u_int8_t irt_wpa;
			u_int16_t irt_lifetime;
		} ih_rtradv;
	} icmp_hun;
#define	icmp_pptr	  icmp_hun.ih_pptr
#define	icmp_length	  icmp_hun.ih_exthdr.iex_length
#define	icmp_gwaddr	  icmp_hun.ih_gwaddr
#define	icmp_id		  icmp_hun.ih_idseq.icd_id
#define	icmp_seq	  icmp_hun.ih_idseq.icd_seq
#define	icmp_void	  icmp_hun.ih_void
#define	icmp_pmvoid	  icmp_hun.ih_pmtu.ipm_void
#define	icmp_nextmtu	  icmp_hun.ih_pmtu.ipm_nextmtu
#define	icmp_num_addrs	  icmp_hun.ih_rtradv.irt_num_addrs
#define	icmp_wpa	  icmp_hun.ih_rtradv.irt_wpa
#define	icmp_lifetime	  icmp_hun.ih_rtradv.irt_lifetime
	union {
		struct id_ts {
			  u_int32_t its_otime;
			  u_int32_t its_rtime;
			  u_int32_t its_ttime;
		} id_ts;
		struct id_ip  {
			  struct ip idi_ip;
			  /* options and then 64 bits of data */
		} id_ip;
		u_int32_t id_mask;
		int8_t	  id_data[1];
	} icmp_dun;
#define	icmp_otime	  icmp_dun.id_ts.its_otime
#define	icmp_rtime	  icmp_dun.id_ts.its_rtime
#define	icmp_ttime	  icmp_dun.id_ts.its_ttime
#define	icmp_ip		  icmp_dun.id_ip.idi_ip
#define	icmp_mask	  icmp_dun.id_mask
#define	icmp_data	  icmp_dun.id_data
};

struct icmp_ext_hdr {
	u_int8_t  ieh_version;		/* only high nibble used */
	u_int8_t  ieh_res;		/* reserved, must be zero */
	u_int16_t ieh_cksum;		/* ones complement cksum of ext hdr */
};

#define ICMP_EXT_HDR_VERSION	0x20
#define ICMP_EXT_HDR_VMASK	0xf0
#define ICMP_EXT_OFFSET		128

struct icmp_ext_obj_hdr {
	u_int16_t ieo_length;		/* length of obj incl this header */
	u_int8_t  ieo_cnum;		/* class number */
	u_int8_t  ieo_ctype;		/* sub class type */
};

#define ICMP_EXT_MPLS		1
#define ICMP_EXT_IFINFO		2

/*
 * For IPv6 transition related ICMP errors.
 */
#define	ICMP_V6ADVLENMIN	(8 + sizeof(struct ip) + 40)
#define	ICMP_V6ADVLEN(p)	(8 + ((p)->icmp_ip.ip_hl << 2) + 40)

/*
 * Lower bounds on packet lengths for various types.
 * For the error advice packets must first insure that the
 * packet is large enough to contain the returned ip header.
 * Only then can we do the check to see if 64 bits of packet
 * data have been returned, since we need to check the returned
 * ip header length.
 */
#define	ICMP_MINLEN	8				/* abs minimum */
#define	ICMP_TSLEN	(8 + 3 * sizeof (u_int32_t))	/* timestamp */
#define	ICMP_MASKLEN	12				/* address mask */
#define	ICMP_ADVLENMIN	(8 + sizeof (struct ip) + 8)	/* min */
#define	ICMP_ADVLENMAX	(8 + 60 + 40)			/* maximum */
#define	ICMP_ADVLEN(p)	(8 + ((p)->icmp_ip.ip_hl << 2) + 8)
	/* N.B.: must separately check that ip_hl >= 5 */

/*
 * Definition of type and code field values.
 *	https://www.iana.org/assignments/icmp-parameters
 */
#define	ICMP_ECHOREPLY		0		/* echo reply */
#define	ICMP_UNREACH		3		/* dest unreachable, codes: */
#define		ICMP_UNREACH_NET		0	/* bad net */
#define		ICMP_UNREACH_HOST		1	/* bad host */
#define		ICMP_UNREACH_PROTOCOL		2	/* bad protocol */
#define		ICMP_UNREACH_PORT		3	/* bad port */
#define		ICMP_UNREACH_NEEDFRAG		4	/* IP_DF caused drop */
#define		ICMP_UNREACH_SRCFAIL		5	/* src route failed */
#define		ICMP_UNREACH_NET_UNKNOWN	6	/* unknown net */
#define		ICMP_UNREACH_HOST_UNKNOWN	7	/* unknown host */
#define		ICMP_UNREACH_ISOLATED		8	/* src host isolated */
#define		ICMP_UNREACH_NET_PROHIB		9	/* for crypto devs */
#define		ICMP_UNREACH_HOST_PROHIB	10	/* ditto */
#define		ICMP_UNREACH_TOSNET		11	/* bad tos for net */
#define		ICMP_UNREACH_TOSHOST		12	/* bad tos for host */
#define		ICMP_UNREACH_FILTER_PROHIB	13	/* prohibited access */
#define		ICMP_UNREACH_HOST_PRECEDENCE	14	/* precedence violat'n*/
#define		ICMP_UNREACH_PRECEDENCE_CUTOFF	15	/* precedence cutoff */
#define	ICMP_SOURCEQUENCH	4		/* packet lost, slow down */
#define	ICMP_REDIRECT		5		/* shorter route, codes: */
#define		ICMP_REDIRECT_NET	0		/* for network */
#define		ICMP_REDIRECT_HOST	1		/* for host */
#define		ICMP_REDIRECT_TOSNET	2		/* for tos and net */
#define		ICMP_REDIRECT_TOSHOST	3		/* for tos and host */
#define	ICMP_ALTHOSTADDR	6		/* alternate host address */
#define	ICMP_ECHO		8		/* echo service */
#define	ICMP_ROUTERADVERT	9		/* router advertisement */
#define		ICMP_ROUTERADVERT_NORMAL		0	/* normal advertisement */
#define		ICMP_ROUTERADVERT_NOROUTE_COMMON	16	/* selective routing */
#define	ICMP_ROUTERSOLICIT	10		/* router solicitation */
#define	ICMP_TIMXCEED		11		/* time exceeded, code: */
#define		ICMP_TIMXCEED_INTRANS	0		/* ttl==0 in transit */
#define		ICMP_TIMXCEED_REASS	1		/* ttl==0 in reass */
#define	ICMP_PARAMPROB		12		/* ip header bad */
#define		ICMP_PARAMPROB_ERRATPTR 0		/* req. opt. absent */
#define		ICMP_PARAMPROB_OPTABSENT 1		/* req. opt. absent */
#define		ICMP_PARAMPROB_LENGTH	2		/* bad length */
#define	ICMP_TSTAMP		13		/* timestamp request */
#define	ICMP_TSTAMPREPLY	14		/* timestamp reply */
#define	ICMP_IREQ		15		/* information request */
#define	ICMP_IREQREPLY		16		/* information reply */
#define	ICMP_MASKREQ		17		/* address mask request */
#define	ICMP_MASKREPLY		18		/* address mask reply */
#define	ICMP_TRACEROUTE		30		/* traceroute */
#define	ICMP_DATACONVERR	31		/* data conversion error */
#define	ICMP_MOBILE_REDIRECT	32		/* mobile host redirect */
#define	ICMP_IPV6_WHEREAREYOU	33		/* IPv6 where-are-you */
#define	ICMP_IPV6_IAMHERE	34		/* IPv6 i-am-here */
#define	ICMP_MOBILE_REGREQUEST	35		/* mobile registration req */
#define	ICMP_MOBILE_REGREPLY	36		/* mobile registration reply */
#define	ICMP_SKIP		39		/* SKIP */
#define	ICMP_PHOTURIS		40		/* Photuris */
#define		ICMP_PHOTURIS_UNKNOWN_INDEX	1	/* unknown sec index */
#define		ICMP_PHOTURIS_AUTH_FAILED	2	/* auth failed */
#define		ICMP_PHOTURIS_DECRYPT_FAILED	3	/* decrypt failed */

#define	ICMP_MAXTYPE		40

#define	ICMP_INFOTYPE(type) \
	((type) == ICMP_ECHOREPLY || (type) == ICMP_ECHO || \
	(type) == ICMP_ROUTERADVERT || (type) == ICMP_ROUTERSOLICIT || \
	(type) == ICMP_TSTAMP || (type) == ICMP_TSTAMPREPLY || \
	(type) == ICMP_IREQ || (type) == ICMP_IREQREPLY || \
	(type) == ICMP_MASKREQ || (type) == ICMP_MASKREPLY)

#ifdef _KERNEL
struct mbuf *
	icmp_do_error(struct mbuf *, int, int, u_int32_t, int);
void	icmp_error(struct mbuf *, int, int, u_int32_t, int);
int	icmp_input(struct mbuf **, int *, int, int, struct netstack *);
void	icmp_init(void);
int	icmp_reflect(struct mbuf *, struct mbuf **, struct in_ifaddr *);
void	icmp_send(struct mbuf *, struct mbuf *);
int	icmp_sysctl(int *, u_int, void *, size_t *, void *, size_t);
struct rtentry *
	icmp_mtudisc_clone(struct in_addr, u_int, int);
void	icmp_mtudisc(struct icmp *, u_int);
int	icmp_do_exthdr(struct mbuf *, u_int16_t, u_int8_t, void *, size_t);
#endif /* _KERNEL */
#endif /* _NETINET_IP_ICMP_H_ */
