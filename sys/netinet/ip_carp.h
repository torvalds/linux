/*	$FreeBSD$	*/
/*	$OpenBSD: ip_carp.h,v 1.8 2004/07/29 22:12:15 mcbride Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2002 Michael Shalayeff. All rights reserved.
 * Copyright (c) 2003 Ryan McBride. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR OR HIS RELATIVES BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF MIND, USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _IP_CARP_H
#define	_IP_CARP_H

/*
 * The CARP header layout is as follows:
 *
 *     0                   1                   2                   3
 *     0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    |Version| Type  | VirtualHostID |    AdvSkew    |    Auth Len   |
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    |   Reserved    |     AdvBase   |          Checksum             |
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    |                         Counter (1)                           |
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    |                         Counter (2)                           |
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    |                        SHA-1 HMAC (1)                         |
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    |                        SHA-1 HMAC (2)                         |
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    |                        SHA-1 HMAC (3)                         |
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    |                        SHA-1 HMAC (4)                         |
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    |                        SHA-1 HMAC (5)                         |
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 */

struct carp_header {
#if BYTE_ORDER == LITTLE_ENDIAN
	u_int8_t	carp_type:4,
			carp_version:4;
#endif
#if BYTE_ORDER == BIG_ENDIAN
	u_int8_t	carp_version:4,
			carp_type:4;
#endif
	u_int8_t	carp_vhid;	/* virtual host id */
	u_int8_t	carp_advskew;	/* advertisement skew */
	u_int8_t	carp_authlen;   /* size of counter+md, 32bit chunks */
	u_int8_t	carp_pad1;	/* reserved */
	u_int8_t	carp_advbase;	/* advertisement interval */
	u_int16_t	carp_cksum;
	u_int32_t	carp_counter[2];
	unsigned char	carp_md[20];	/* SHA1 HMAC */
} __packed;

#ifdef CTASSERT
CTASSERT(sizeof(struct carp_header) == 36);
#endif

#define	CARP_DFLTTL		255

/* carp_version */
#define	CARP_VERSION		2

/* carp_type */
#define	CARP_ADVERTISEMENT	0x01

#define	CARP_KEY_LEN		20	/* a sha1 hash of a passphrase */

/* carp_advbase */
#define	CARP_DFLTINTV		1

/*
 * Statistics.
 */
struct carpstats {
	uint64_t	carps_ipackets;		/* total input packets, IPv4 */
	uint64_t	carps_ipackets6;	/* total input packets, IPv6 */
	uint64_t	carps_badif;		/* wrong interface */
	uint64_t	carps_badttl;		/* TTL is not CARP_DFLTTL */
	uint64_t	carps_hdrops;		/* packets shorter than hdr */
	uint64_t	carps_badsum;		/* bad checksum */
	uint64_t	carps_badver;		/* bad (incl unsupp) version */
	uint64_t	carps_badlen;		/* data length does not match */
	uint64_t	carps_badauth;		/* bad authentication */
	uint64_t	carps_badvhid;		/* bad VHID */
	uint64_t	carps_badaddrs;		/* bad address list */

	uint64_t	carps_opackets;		/* total output packets, IPv4 */
	uint64_t	carps_opackets6;	/* total output packets, IPv6 */
	uint64_t	carps_onomem;		/* no memory for an mbuf */
	uint64_t	carps_ostates;		/* total state updates sent */

	uint64_t	carps_preempt;		/* if enabled, preemptions */
};

/*
 * Configuration structure for SIOCSVH SIOCGVH
 */
struct carpreq {
	int		carpr_count;
	int		carpr_vhid;
#define	CARP_MAXVHID	255
	int		carpr_state;
#define	CARP_STATES	"INIT", "BACKUP", "MASTER"
#define	CARP_MAXSTATE	2
	int		carpr_advskew;
#define	CARP_MAXSKEW	240
	int		carpr_advbase;
	unsigned char	carpr_key[CARP_KEY_LEN];
};
#define	SIOCSVH	_IOWR('i', 245, struct ifreq)
#define	SIOCGVH	_IOWR('i', 246, struct ifreq)

#ifdef _KERNEL
int		carp_ioctl(struct ifreq *, u_long, struct thread *);
int		carp_attach(struct ifaddr *, int);
void		carp_detach(struct ifaddr *, bool);
void		carp_carpdev_state(struct ifnet *);
int		carp_input(struct mbuf **, int *, int);
int		carp6_input (struct mbuf **, int *, int);
int		carp_output (struct ifnet *, struct mbuf *,
		    const struct sockaddr *);
int		carp_master(struct ifaddr *);
int		carp_iamatch(struct ifaddr *, uint8_t **);
struct ifaddr	*carp_iamatch6(struct ifnet *, struct in6_addr *);
caddr_t		carp_macmatch6(struct ifnet *, struct mbuf *, const struct in6_addr *);
int		carp_forus(struct ifnet *, u_char *);

/* These are external networking stack hooks for CARP */
/* net/if.c */
extern int (*carp_ioctl_p)(struct ifreq *, u_long, struct thread *);
extern int (*carp_attach_p)(struct ifaddr *, int);
extern void (*carp_detach_p)(struct ifaddr *, bool);
extern void (*carp_linkstate_p)(struct ifnet *);
extern void (*carp_demote_adj_p)(int, char *);
extern int (*carp_master_p)(struct ifaddr *);
/* net/if_bridge.c net/if_ethersubr.c */
extern int (*carp_forus_p)(struct ifnet *, u_char *);
/* net/if_ethersubr.c */
extern int (*carp_output_p)(struct ifnet *, struct mbuf *,
    const struct sockaddr *);
/* net/rtsock.c */
extern int (*carp_get_vhid_p)(struct ifaddr *);
#ifdef INET
/* netinet/if_ether.c */
extern int (*carp_iamatch_p)(struct ifaddr *, uint8_t **);
#endif
#ifdef INET6
/* netinet6/nd6_nbr.c */
extern struct ifaddr *(*carp_iamatch6_p)(struct ifnet *, struct in6_addr *);
extern caddr_t (*carp_macmatch6_p)(struct ifnet *, struct mbuf *,
    const struct in6_addr *);
#endif
#endif
#endif /* _IP_CARP_H */
