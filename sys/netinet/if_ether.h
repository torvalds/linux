/*	$OpenBSD: if_ether.h,v 1.96 2025/07/07 00:55:15 jsg Exp $	*/
/*	$NetBSD: if_ether.h,v 1.22 1996/05/11 13:00:00 mycroft Exp $	*/

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
 *	@(#)if_ether.h	8.1 (Berkeley) 6/10/93
 */

#ifndef _NETINET_IF_ETHER_H_
#define _NETINET_IF_ETHER_H_

/*
 * Some basic Ethernet constants.
 */
#define	ETHER_ADDR_LEN	6	/* Ethernet address length		*/
#define ETHER_TYPE_LEN	2	/* Ethernet type field length		*/
#define ETHER_CRC_LEN	4	/* Ethernet CRC length			*/
#define ETHER_HDR_LEN	((ETHER_ADDR_LEN * 2) + ETHER_TYPE_LEN)
#define ETHER_MIN_LEN	64	/* Minimum frame length, CRC included	*/
#define ETHER_MAX_LEN	1518	/* Maximum frame length, CRC included	*/
#define ETHER_MAX_DIX_LEN	1536	/* Maximum DIX frame length	*/

/*
 * Some Ethernet extensions.
 */
#define ETHER_VLAN_ENCAP_LEN	4	/* len of 802.1Q VLAN encapsulation */

/*
 * Mbuf adjust factor to force 32-bit alignment of IP header.
 * Drivers should do m_adj(m, ETHER_ALIGN) when setting up a
 * receive so the upper layers get the IP header properly aligned
 * past the 14-byte Ethernet header.
 */
#define ETHER_ALIGN	2	/* driver adjust for IP hdr alignment */

/*
 * The maximum supported Ethernet length and some space for encapsulation.
 */
#define ETHER_MAX_HARDMTU_LEN	65435

/*
 * Ethernet address - 6 octets
 */
struct ether_addr {
	u_int8_t ether_addr_octet[ETHER_ADDR_LEN];
};

/*
 * The length of the combined header.
 */
struct	ether_header {
	u_int8_t  ether_dhost[ETHER_ADDR_LEN];
	u_int8_t  ether_shost[ETHER_ADDR_LEN];
	u_int16_t ether_type;
};

/*
 * VLAN headers.
 */

struct  ether_vlan_header {
	u_char  evl_dhost[ETHER_ADDR_LEN];
	u_char  evl_shost[ETHER_ADDR_LEN];
	u_int16_t evl_encap_proto;
	u_int16_t evl_tag;
	u_int16_t evl_proto;
};

#define EVL_VLID_MASK	0xFFF
#define EVL_VLID_NULL	0x000
/* 0x000 and 0xfff are reserved */
#define EVL_VLID_MIN	0x001
#define EVL_VLID_MAX	0xFFE
#define EVL_VLANOFTAG(tag) ((tag) & EVL_VLID_MASK)

#define EVL_PRIO_MAX    7
#define EVL_PRIO_BITS   13
#define EVL_PRIOFTAG(tag) (((tag) >> EVL_PRIO_BITS) & 7)

#define EVL_ENCAPLEN    4       /* length in octets of encapsulation */

#include <net/ethertypes.h>

#define	ETHER_IS_MULTICAST(addr) (*(addr) & 0x01) /* is address mcast/bcast? */
#define	ETHER_IS_BROADCAST(addr) \
	(((addr)[0] & (addr)[1] & (addr)[2] & \
	  (addr)[3] & (addr)[4] & (addr)[5]) == 0xff)
#define	ETHER_IS_ANYADDR(addr)		\
	(((addr)[0] | (addr)[1] | (addr)[2] | \
	  (addr)[3] | (addr)[4] | (addr)[5]) == 0x00)
#define	ETHER_IS_EQ(a1, a2)	(memcmp((a1), (a2), ETHER_ADDR_LEN) == 0)

/*
 * It can be faster to work with ethernet addresses as a uint64_t.
 * Provide some constants and functionality centrally to better
 * support this.
 */

#define ETH64_IS_MULTICAST(_e64)	((_e64) & 0x010000000000ULL)
#define ETH64_IS_BROADCAST(_e64)	((_e64) == 0xffffffffffffULL)
#define ETH64_IS_ANYADDR(_e64)		((_e64) == 0x000000000000ULL)

#define ETH64_8021_RSVD_PREFIX		0x0180c2000000ULL
#define ETH64_8021_RSVD_MASK		0xfffffffffff0ULL
#define ETH64_IS_8021_RSVD(_e64)	\
    (((_e64) & ETH64_8021_RSVD_MASK) == ETH64_8021_RSVD_PREFIX)

/*
 * Ethernet MTU constants.
 */
#define	ETHERMTU	(ETHER_MAX_LEN - ETHER_HDR_LEN - ETHER_CRC_LEN)
#define	ETHERMIN	(ETHER_MIN_LEN - ETHER_HDR_LEN - ETHER_CRC_LEN)

/*
 * Ethernet CRC32 polynomials (big- and little-endian versions).
 */
#define	ETHER_CRC_POLY_LE	0xedb88320
#define	ETHER_CRC_POLY_BE	0x04c11db6

/*
 * Ethernet Address Resolution Protocol.
 *
 * See RFC 826 for protocol description.  Structure below is adapted
 * to resolving internet addresses.  Field names used correspond to
 * RFC 826.
 */
struct	ether_arp {
	struct	 arphdr ea_hdr;			/* fixed-size header */
	u_int8_t arp_sha[ETHER_ADDR_LEN];	/* sender hardware address */
	u_int8_t arp_spa[4];			/* sender protocol address */
	u_int8_t arp_tha[ETHER_ADDR_LEN];	/* target hardware address */
	u_int8_t arp_tpa[4];			/* target protocol address */
};
#define	arp_hrd	ea_hdr.ar_hrd
#define	arp_pro	ea_hdr.ar_pro
#define	arp_hln	ea_hdr.ar_hln
#define	arp_pln	ea_hdr.ar_pln
#define	arp_op	ea_hdr.ar_op

struct sockaddr_inarp {
	u_int8_t  sin_len;
	u_int8_t  sin_family;
	u_int16_t sin_port;
	struct	  in_addr sin_addr;
	struct	  in_addr sin_srcaddr;
	u_int16_t sin_tos;
	u_int16_t sin_other;
#define SIN_PROXY 1
};

/*
 * IP and ethernet specific routing flags
 */
#define	RTF_USETRAILERS	  RTF_PROTO1	/* use trailers */
#define	RTF_PERMANENT_ARP RTF_PROTO3    /* only manual overwrite of entry */

#ifdef _KERNEL

#include <sys/refcnt.h>

/*
 * Macro to map an IP multicast address to an Ethernet multicast address.
 * The high-order 25 bits of the Ethernet address are statically assigned,
 * and the low-order 23 bits are taken from the low end of the IP address.
 */
#define ETHER_MAP_IP_MULTICAST(ipaddr, enaddr)				\
	/* struct in_addr *ipaddr; */					\
	/* u_int8_t enaddr[ETHER_ADDR_LEN]; */				\
do {									\
	(enaddr)[0] = 0x01;						\
	(enaddr)[1] = 0x00;						\
	(enaddr)[2] = 0x5e;						\
	(enaddr)[3] = ((u_int8_t *)ipaddr)[1] & 0x7f;			\
	(enaddr)[4] = ((u_int8_t *)ipaddr)[2];				\
	(enaddr)[5] = ((u_int8_t *)ipaddr)[3];				\
} while (/* CONSTCOND */ 0)

/*
 * Macro to map an IPv6 multicast address to an Ethernet multicast address.
 * The high-order 16 bits of the Ethernet address are statically assigned,
 * and the low-order 32 bits are taken from the low end of the IPv6 address.
 */
#define ETHER_MAP_IPV6_MULTICAST(ip6addr, enaddr)			\
	/* struct in6_addr *ip6addr; */					\
	/* u_int8_t enaddr[ETHER_ADDR_LEN]; */				\
do {									\
	(enaddr)[0] = 0x33;						\
	(enaddr)[1] = 0x33;						\
	(enaddr)[2] = ((u_int8_t *)ip6addr)[12];			\
	(enaddr)[3] = ((u_int8_t *)ip6addr)[13];			\
	(enaddr)[4] = ((u_int8_t *)ip6addr)[14];			\
	(enaddr)[5] = ((u_int8_t *)ip6addr)[15];			\
} while (/* CONSTCOND */ 0)

#include <net/if_var.h>	/* for "struct ifnet" */

struct ether_brport {
	struct mbuf	*(*eb_input)(struct ifnet *, struct mbuf *,
			   uint64_t, void *, struct netstack *);
	void		(*eb_port_take)(void *);
	void		(*eb_port_rele)(void *);
	void		  *eb_port;
};

/*
 * Structure shared between the ethernet driver modules and
 * the address resolution code.  For example, each ec_softc or il_softc
 * begins with this structure.
 */
struct	arpcom {
	struct	 ifnet ac_if;			/* network-visible interface */
	u_int8_t ac_enaddr[ETHER_ADDR_LEN];	/* ethernet hardware address */
	char	 ac__pad[2];			/* pad for some machines */
	LIST_HEAD(, ether_multi) ac_multiaddrs;	/* list of multicast addrs */
	int	 ac_multicnt;			/* length of ac_multiaddrs */
	int	 ac_multirangecnt;		/* number of mcast ranges */

	void	*ac_trunkport;
	const struct ether_brport *ac_brport;
};

extern int arpt_keep;				/* arp resolved cache expire */
extern int arpt_down;				/* arp down cache expire */

extern u_int8_t etherbroadcastaddr[ETHER_ADDR_LEN];
extern u_int8_t etheranyaddr[ETHER_ADDR_LEN];
extern u_int8_t ether_ipmulticast_min[ETHER_ADDR_LEN];
extern u_int8_t ether_ipmulticast_max[ETHER_ADDR_LEN];

#ifdef NFSCLIENT
extern unsigned int revarp_ifidx;
#endif /* NFSCLIENT */

void	revarpinput(struct ifnet *, struct mbuf *, struct netstack *);
void	revarprequest(struct ifnet *);
int	revarpwhoarewe(struct ifnet *, struct in_addr *, struct in_addr *);
int	revarpwhoami(struct in_addr *, struct ifnet *);

void	arpinit(void);
void	arpinput(struct ifnet *, struct mbuf *, struct netstack *);
void	arprequest(struct ifnet *, u_int32_t *, u_int32_t *, u_int8_t *);
int	arpproxy(struct in_addr, unsigned int);
int	arpresolve(struct ifnet *, struct rtentry *, struct mbuf *,
	    struct sockaddr *, u_char *);
void	arp_rtrequest(struct ifnet *, int, struct rtentry *);

void	ether_fakeaddr(struct ifnet *);
int	ether_addmulti(struct ifreq *, struct arpcom *);
int	ether_delmulti(struct ifreq *, struct arpcom *);
int	ether_multiaddr(struct sockaddr *, u_int8_t *, u_int8_t *);
void	ether_ifattach(struct ifnet *);
void	ether_ifdetach(struct ifnet *);
int	ether_ioctl(struct ifnet *, struct arpcom *, u_long, caddr_t);
void	ether_input(struct ifnet *, struct mbuf *, struct netstack *);
int	ether_resolve(struct ifnet *, struct mbuf *, struct sockaddr *,
	    struct rtentry *, struct ether_header *);
struct mbuf *
	ether_encap(struct ifnet *, struct mbuf *, struct sockaddr *,
	    struct rtentry *, int *);
int	ether_output(struct ifnet *, struct mbuf *, struct sockaddr *,
	    struct rtentry *);
void	ether_rtrequest(struct ifnet *, int, struct rtentry *);
char	*ether_sprintf(u_char *);

int	ether_brport_isset(struct ifnet *);
void	ether_brport_set(struct ifnet *, const struct ether_brport *);
void	ether_brport_clr(struct ifnet *);
const struct ether_brport *
	ether_brport_get(struct ifnet *);
const struct ether_brport *
	ether_brport_get_locked(struct ifnet *);

uint64_t	ether_addr_to_e64(const struct ether_addr *);
void		ether_e64_to_addr(struct ether_addr *, uint64_t);

struct ether_extracted {
	struct ether_header		*eh;
	struct ether_vlan_header	*evh;
	struct ip			*ip4;
	struct ip6_hdr			*ip6;
	struct tcphdr			*tcp;
	struct udphdr			*udp;
	u_int				 iplen;
	u_int				 iphlen;
	u_int				 tcphlen;
	u_int				 paylen;
};

void ether_extract_headers(struct mbuf *, struct ether_extracted *);

/*
 * Ethernet multicast address structure.  There is one of these for each
 * multicast address or range of multicast addresses that we are supposed
 * to listen to on a particular interface.  They are kept in a linked list,
 * rooted in the interface's arpcom structure.  (This really has nothing to
 * do with ARP, or with the Internet address family, but this appears to be
 * the minimally-disrupting place to put it.)
 */
struct ether_multi {
	u_int8_t enm_addrlo[ETHER_ADDR_LEN]; /* low  or only address of range */
	u_int8_t enm_addrhi[ETHER_ADDR_LEN]; /* high or only address of range */
	struct refcnt enm_refcnt;		/* no. claims to this addr/range */
	LIST_ENTRY(ether_multi) enm_list;
};

/*
 * Structure used by macros below to remember position when stepping through
 * all of the ether_multi records.
 */
struct ether_multistep {
	struct ether_multi  *e_enm;
};

/*
 * Macro for looking up the ether_multi record for a given range of Ethernet
 * multicast addresses connected to a given arpcom structure.  If no matching
 * record is found, "enm" returns NULL.
 */
#define ETHER_LOOKUP_MULTI(addrlo, addrhi, ac, enm)			\
	/* u_int8_t addrlo[ETHER_ADDR_LEN]; */				\
	/* u_int8_t addrhi[ETHER_ADDR_LEN]; */				\
	/* struct arpcom *ac; */					\
	/* struct ether_multi *enm; */					\
do {									\
	for ((enm) = LIST_FIRST(&(ac)->ac_multiaddrs);			\
	    (enm) != NULL &&						\
	    (memcmp((enm)->enm_addrlo, (addrlo), ETHER_ADDR_LEN) != 0 ||\
	     memcmp((enm)->enm_addrhi, (addrhi), ETHER_ADDR_LEN) != 0);	\
		(enm) = LIST_NEXT((enm), enm_list));			\
} while (/* CONSTCOND */ 0)

/*
 * Macro to step through all of the ether_multi records, one at a time.
 * The current position is remembered in "step", which the caller must
 * provide.  ETHER_FIRST_MULTI(), below, must be called to initialize "step"
 * and get the first record.  Both macros return a NULL "enm" when there
 * are no remaining records.
 */
#define ETHER_NEXT_MULTI(step, enm)					\
	/* struct ether_multistep step; */				\
	/* struct ether_multi *enm; */					\
do {									\
	if (((enm) = (step).e_enm) != NULL)				\
		(step).e_enm = LIST_NEXT((enm), enm_list);		\
} while (/* CONSTCOND */ 0)

#define ETHER_FIRST_MULTI(step, ac, enm)				\
	/* struct ether_multistep step; */				\
	/* struct arpcom *ac; */					\
	/* struct ether_multi *enm; */					\
do {									\
	(step).e_enm = LIST_FIRST(&(ac)->ac_multiaddrs);		\
	ETHER_NEXT_MULTI((step), (enm));				\
} while (/* CONSTCOND */ 0)

u_int32_t ether_crc32_le_update(u_int32_t crc, const u_int8_t *, size_t);
u_int32_t ether_crc32_be_update(u_int32_t crc, const u_int8_t *, size_t);
u_int32_t ether_crc32_le(const u_int8_t *, size_t);
u_int32_t ether_crc32_be(const u_int8_t *, size_t);

#else /* _KERNEL */

__BEGIN_DECLS
char *ether_ntoa(const struct ether_addr *);
struct ether_addr *ether_aton(const char *);
int ether_ntohost(char *, struct ether_addr *);
int ether_hostton(const char *, struct ether_addr *);
int ether_line(const char *, struct ether_addr *, char *);
__END_DECLS

#endif /* _KERNEL */
#endif /* _NETINET_IF_ETHER_H_ */
