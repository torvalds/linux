/*	$OpenBSD: ip_ether.h,v 1.30 2019/10/04 05:00:49 dlg Exp $ */
/*
 * The author of this code is Angelos D. Keromytis (angelos@adk.gr)
 *
 * This code was written by Angelos D. Keromytis in October 1999.
 *
 * Copyright (C) 1999-2001 Angelos D. Keromytis.
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

#ifndef _NETINET_IP_ETHER_H_
#define _NETINET_IP_ETHER_H_

/*
 * Ethernet-inside-IP processing.
 */

struct etheripstat {
	u_int64_t	etherips_hdrops;	/* packet shorter than header shows */
	u_int64_t	etherips_qfull;		/* bridge queue full, packet dropped */
	u_int64_t	etherips_noifdrops;	/* no interface/bridge information */
	u_int64_t	etherips_pdrops;	/* packet dropped due to policy */
	u_int64_t	etherips_adrops;	/* all other drops */
	u_int64_t	etherips_ipackets;	/* total input packets */
	u_int64_t	etherips_opackets;	/* total output packets */
	u_int64_t	etherips_ibytes;	/* input bytes */
	u_int64_t	etherips_obytes;	/* output bytes */
};

struct etherip_header {
#if BYTE_ORDER == LITTLE_ENDIAN
	u_int		eip_res:4;	/* reserved */
	u_int		eip_ver:4;	/* version */
#endif
#if BYTE_ORDER == BIG_ENDIAN
	u_int		eip_ver:4;	/* version */
	u_int		eip_res:4;	/* reserved */
#endif
	u_int8_t	eip_pad;	/* required padding byte */
} __packed;

#define ETHERIP_VERSION		0x03

/*
 * Names for Ether-IP sysctl objects
 */
#define	ETHERIPCTL_ALLOW	1	/* accept incoming EtherIP packets */
#define	ETHERIPCTL_STATS	2	/* etherip stats */
#define	ETHERIPCTL_MAXID	3

#define ETHERIPCTL_NAMES { \
	{ 0, 0 }, \
	{ "allow", CTLTYPE_INT }, \
	{ "stats", CTLTYPE_STRUCT }, \
}

#ifdef _KERNEL

#include <sys/percpu.h>

enum etheripstat_counters {
	etherips_hdrops,		/* packet shorter than header shows */
	etherips_qfull,			/* bridge queue full, packet dropped */
	etherips_noifdrops,		/* no interface/bridge information */
	etherips_pdrops,		/* packet dropped due to policy */
	etherips_adrops,		/* all other drops */
	etherips_ipackets,		/* total input packets */
	etherips_opackets,		/* total output packets */
	etherips_ibytes,		/* input bytes */
	etherips_obytes,		/* output bytes */

	etherips_ncounters
};

extern struct cpumem *etheripcounters;

static inline void
etheripstat_inc(enum etheripstat_counters c)
{
	counters_inc(etheripcounters, c);
}

static inline void
etheripstat_add(enum etheripstat_counters c, uint64_t v)
{
	counters_add(etheripcounters, c, v);
}

static inline void
etheripstat_pkt(enum etheripstat_counters pcounter,
    enum etheripstat_counters bcounter, uint64_t v)
{
	counters_pkt(etheripcounters, pcounter, bcounter, v);
}

#endif /* _KERNEL */
#endif /* _NETINET_IP_ETHER_H_ */
