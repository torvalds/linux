/*	$OpenBSD: ip_esp.h,v 1.48 2025/01/01 13:44:22 bluhm Exp $	*/
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

#ifndef _NETINET_IP_ESP_H_
#define _NETINET_IP_ESP_H_

struct espstat {
	uint64_t	esps_hdrops;	/* Packet shorter than header shows */
	uint64_t	esps_nopf;	/* Protocol family not supported */
	uint64_t	esps_notdb;
	uint64_t	esps_badkcr;
	uint64_t	esps_qfull;
	uint64_t	esps_noxform;
	uint64_t	esps_badilen;
	uint64_t	esps_wrap;	/* Replay counter wrapped around */
	uint64_t	esps_badenc;	/* Bad encryption detected */
	uint64_t	esps_badauth;	/* Only valid for transforms
					 * with auth */
	uint64_t	esps_replay;	/* Possible packet replay detected */
	uint64_t	esps_input;	/* Input ESP packets */
	uint64_t	esps_output;	/* Output ESP packets */
	uint64_t	esps_invalid;	/* Trying to use an invalid TDB */
	uint64_t	esps_ibytes;	/* Input bytes */
	uint64_t	esps_obytes;	/* Output bytes */
	uint64_t	esps_toobig;	/* Packet got larger than
					 * IP_MAXPACKET */
	uint64_t	esps_pdrops;	/* Packet blocked due to policy */
	uint64_t	esps_crypto;	/* Crypto processing failure */
	uint64_t	esps_udpencin;  /* Input ESP-in-UDP packets */
	uint64_t	esps_udpencout; /* Output ESP-in-UDP packets */
	uint64_t	esps_udpinval;  /* Invalid input ESP-in-UDP packets */
	uint64_t	esps_udpneeded; /* Trying to use a ESP-in-UDP TDB */
	uint64_t	esps_outfail;	/* Packet output failure */
};

/*
 * Names for ESP sysctl objects
 */
#define	ESPCTL_ENABLE		1	/* Enable ESP processing */
#define	ESPCTL_UDPENCAP_ENABLE	2	/* Enable ESP over UDP */
#define	ESPCTL_UDPENCAP_PORT	3	/* UDP port for encapsulation */
#define	ESPCTL_STATS		4	/* ESP Stats */
#define ESPCTL_MAXID		5

#define ESPCTL_NAMES { \
	{ 0, 0 }, \
	{ "enable", CTLTYPE_INT }, \
	{ "udpencap", CTLTYPE_INT }, \
	{ "udpencap_port", CTLTYPE_INT }, \
	{ "stats", CTLTYPE_STRUCT }, \
}

#ifdef _KERNEL

#include <sys/percpu.h>

enum espstat_counters {
	esps_hdrops,			/* Packet shorter than header shows */
	esps_nopf,			/* Protocol family not supported */
	esps_notdb,
	esps_badkcr,
	esps_qfull,
	esps_noxform,
	esps_badilen,
	esps_wrap,			/* Replay counter wrapped around */
	esps_badenc,			/* Bad encryption detected */
	esps_badauth,			/* Only valid for transformsx
					 * with auth */
	esps_replay,			/* Possible packet replay detected */
	esps_input,			/* Input ESP packets */
	esps_output,			/* Output ESP packets */
	esps_invalid,			/* Trying to use an invalid TDB */
	esps_ibytes,			/* Input bytes */
	esps_obytes,			/* Output bytes */
	esps_toobig,			/* Packet got larger than
					 * IP_MAXPACKET */
	esps_pdrops,			/* Packet blocked due to policy */
	esps_crypto,			/* Crypto processing failure */
	esps_udpencin,			/* Input ESP-in-UDP packets */
	esps_udpencout,			/* Output ESP-in-UDP packets */
	esps_udpinval,			/* Invalid input ESP-in-UDP packets */
	esps_udpneeded,			/* Trying to use a ESP-in-UDP TDB */
	esps_outfail,			/* Packet output failure */

	esps_ncounters
};

extern struct cpumem *espcounters;

static inline void
espstat_inc(enum espstat_counters c)
{
	counters_inc(espcounters, c);
}

static inline void
espstat_add(enum espstat_counters c, uint64_t v)
{
	counters_add(espcounters, c, v);
}

extern int esp_enable;
extern int udpencap_enable;
extern int udpencap_port;

#endif /* _KERNEL */
#endif /* _NETINET_IP_ESP_H_ */
