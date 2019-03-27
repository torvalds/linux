/*	$FreeBSD$	*/
/*	$OpenBSD: ip_esp.h,v 1.37 2002/06/09 16:26:10 itojun Exp $	*/
/*-
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

#ifndef _NETIPSEC_ESP_VAR_H_
#define _NETIPSEC_ESP_VAR_H_

/*
 * These define the algorithm indices into the histogram.  They're
 * presently based on the PF_KEY v2 protocol values which is bogus;
 * they should be decoupled from the protocol at which time we can
 * pack them and reduce the size of the array to a reasonable value.
 */
#define	ESP_ALG_MAX	256		/* NB: could be < but skipjack is 249 */

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
	uint64_t	esps_badauth;	/* Only valid for transforms with auth */
	uint64_t	esps_replay;	/* Possible packet replay detected */
	uint64_t	esps_input;	/* Input ESP packets */
	uint64_t	esps_output;	/* Output ESP packets */
	uint64_t	esps_invalid;	/* Trying to use an invalid TDB */
	uint64_t	esps_ibytes;	/* Input bytes */
	uint64_t	esps_obytes;	/* Output bytes */
	uint64_t	esps_toobig;	/* Packet got larger than IP_MAXPACKET */
	uint64_t	esps_pdrops;	/* Packet blocked due to policy */
	uint64_t	esps_crypto;	/* Crypto processing failure */
	uint64_t	esps_tunnel;	/* Tunnel sanity check failure */
	uint64_t	esps_hist[ESP_ALG_MAX];	/* Per-algorithm op count */
};

#ifdef _KERNEL
#include <sys/counter.h>

VNET_DECLARE(int, esp_enable);
VNET_PCPUSTAT_DECLARE(struct espstat, espstat);

#define	ESPSTAT_ADD(name, val)	\
    VNET_PCPUSTAT_ADD(struct espstat, espstat, name, (val))
#define	ESPSTAT_INC(name)	ESPSTAT_ADD(name, 1)
#define	V_esp_enable	VNET(esp_enable)
#endif /* _KERNEL */
#endif /*_NETIPSEC_ESP_VAR_H_*/
