/*	$OpenBSD: ip_ah.h,v 1.37 2020/09/01 01:53:34 gnezdo Exp $	*/
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
 * Copyright (C) 1995, 1996, 1997, 1998, 1999 John Ioannidis,
 * Angelos D. Keromytis and Niels Provos.
 * Copyright (c) 2001 Angelos D. Keromytis.
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

#ifndef _NETINET_IP_AH_H_
#define _NETINET_IP_AH_H_

struct ahstat {
	uint64_t	ahs_hdrops;	/* Packet shorter than header shows */
	uint64_t	ahs_nopf;	/* Protocol family not supported */
	uint64_t	ahs_notdb;
	uint64_t	ahs_badkcr;
	uint64_t	ahs_badauth;
	uint64_t	ahs_noxform;
	uint64_t	ahs_qfull;
	uint64_t	ahs_wrap;
	uint64_t	ahs_replay;
	uint64_t	ahs_badauthl;	/* Bad authenticator length */
	uint64_t	ahs_input;	/* Input AH packets */
	uint64_t	ahs_output;	/* Output AH packets */
	uint64_t	ahs_invalid;	/* Trying to use an invalid TDB */
	uint64_t	ahs_ibytes;	/* Input bytes */
	uint64_t	ahs_obytes;	/* Output bytes */
	uint64_t	ahs_toobig;	/* Packet got larger than
					 * IP_MAXPACKET */
	uint64_t	ahs_pdrops;	/* Packet blocked due to policy */
	uint64_t	ahs_crypto;	/* Crypto processing failure */
	uint64_t	ahs_outfail;	/* Packet output failure */
};

struct ah {
    u_int8_t   ah_nh;
    u_int8_t   ah_hl;
    u_int16_t  ah_rv;
    u_int32_t  ah_spi;
    u_int32_t  ah_rpl;  /* We may not use this, if we're using old xforms */
};

/* Length of base AH header */
#define AH_FLENGTH		8

/*
 * Names for AH sysctl objects
 */
#define	AHCTL_ENABLE	1		/* Enable AH processing */
#define	AHCTL_STATS	2		/* AH stats */
#define	AHCTL_MAXID	3

#define AHCTL_NAMES { \
	{ 0, 0 }, \
	{ "enable", CTLTYPE_INT }, \
	{ "stats", CTLTYPE_STRUCT } \
}

#ifdef _KERNEL

#include <sys/percpu.h>

enum ahstat_counters {
	ahs_hdrops,			/* Packet shorter than header shows */
	ahs_nopf,			/* Protocol family not supported */
	ahs_notdb,
	ahs_badkcr,
	ahs_badauth,
	ahs_noxform,
	ahs_qfull,
	ahs_wrap,
	ahs_replay,
	ahs_badauthl,			/* Bad authenticator length */
	ahs_input,			/* Input AH packets */
	ahs_output,			/* Output AH packets */
	ahs_invalid,			/* Trying to use an invalid TDB */
	ahs_ibytes,			/* Input bytes */
	ahs_obytes,			/* Output bytes */
	ahs_toobig,			/* Packet got larger than
					 * IP_MAXPACKET */
	ahs_pdrops,			/* Packet blocked due to policy */
	ahs_crypto,			/* Crypto processing failure */
	ahs_outfail,			/* Packet output failure */

	ahs_ncounters
};

extern struct cpumem *ahcounters;

static inline void
ahstat_inc(enum ahstat_counters c)
{
	counters_inc(ahcounters, c);
}

static inline void
ahstat_add(enum ahstat_counters c, uint64_t v)
{
	counters_add(ahcounters, c, v);
}

extern int ah_enable;

#endif /* _KERNEL */
#endif /* _NETINET_IP_AH_H_ */
