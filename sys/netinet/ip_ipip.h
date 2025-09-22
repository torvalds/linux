/*	$OpenBSD: ip_ipip.h,v 1.15 2025/03/02 21:28:32 bluhm Exp $ */
/*
 * The authors of this code are John Ioannidis (ji@tla.org),
 * Angelos D. Keromytis (kermit@csd.uch.gr) and
 * Niels Provos (provos@physnet.uni-hamburg.de).
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

#ifndef _NETINET_IPIP_H_
#define _NETINET_IPIP_H_

/*
 * IP-inside-IP processing.
 * Not quite all the functionality of RFC-1853, but the main idea is there.
 */

struct ipipstat {
    u_int64_t	ipips_ipackets;		/* total input packets */
    u_int64_t	ipips_opackets;		/* total output packets */
    u_int64_t	ipips_hdrops;		/* packet shorter than header shows */
    u_int64_t	ipips_qfull;
    u_int64_t	ipips_ibytes;
    u_int64_t	ipips_obytes;
    u_int64_t	ipips_pdrops;		/* packet dropped due to policy */
    u_int64_t	ipips_spoof;		/* IP spoofing attempts */
    u_int64_t	ipips_family;		/* Protocol family mismatch */
    u_int64_t	ipips_unspec;            /* Missing tunnel endpoint address */
};

#define IP4_DEFAULT_TTL    0
#define IP4_SAME_TTL	  -1

/*
 * Names for IPIP sysctl objects
 */
#define	IPIPCTL_ALLOW	1		/* accept incoming IP4 packets */
#define	IPIPCTL_STATS	2		/* IPIP stats */
#define	IPIPCTL_MAXID	3

#define IPIPCTL_NAMES { \
	{ 0, 0 }, \
	{ "allow", CTLTYPE_INT }, \
	{ "stats", CTLTYPE_STRUCT }, \
}

#ifdef _KERNEL

#include <sys/percpu.h>

enum ipipstat_counters {
	ipips_ipackets,
	ipips_opackets,
	ipips_hdrops,
	ipips_qfull,
	ipips_ibytes,
	ipips_obytes,
	ipips_pdrops,
	ipips_spoof,
	ipips_family,
	ipips_unspec,
	ipips_ncounters
};

extern struct cpumem *ipipcounters;

static inline void
ipipstat_inc(enum ipipstat_counters c)
{
	counters_inc(ipipcounters, c);
}

static inline void
ipipstat_add(enum ipipstat_counters c, uint64_t v)
{
	counters_add(ipipcounters, c, v);
}

static inline void
ipipstat_pkt(enum ipipstat_counters p, enum ipipstat_counters b, uint64_t v)
{
	counters_pkt(ipipcounters, p, b, v);
}

struct tdb;

void	ipip_init(void);
int	ipip_input(struct mbuf **, int *, int, int, struct netstack *);
int	ipip_input_if(struct mbuf **, int *, int, int, int, struct ifnet *,
	    struct netstack *);
int	ipip_output(struct mbuf **, struct tdb *);
int	ipip_sysctl(int *, u_int, void *, size_t *, void *, size_t);

extern int ipip_allow;
#endif /* _KERNEL */
#endif /* _NETINET_IPIP_H_ */
