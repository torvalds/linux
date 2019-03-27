/*	$FreeBSD$	*/
/*	$KAME: ipcomp.h,v 1.8 2000/09/26 07:55:14 itojun Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (C) 1999 WIDE Project.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the project nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _NETIPSEC_IPCOMP_VAR_H_
#define _NETIPSEC_IPCOMP_VAR_H_

/*
 * These define the algorithm indices into the histogram.  They're
 * presently based on the PF_KEY v2 protocol values which is bogus;
 * they should be decoupled from the protocol at which time we can
 * pack them and reduce the size of the array to a minimum.
 */
#define	IPCOMP_ALG_MAX	8

#define	IPCOMPSTAT_VERSION	2
struct ipcompstat {
	uint64_t	ipcomps_hdrops;	/* Packet shorter than header shows */
	uint64_t	ipcomps_nopf;	/* Protocol family not supported */
	uint64_t	ipcomps_notdb;
	uint64_t	ipcomps_badkcr;
	uint64_t	ipcomps_qfull;
	uint64_t	ipcomps_noxform;
	uint64_t	ipcomps_wrap;
	uint64_t	ipcomps_input;	/* Input IPcomp packets */
	uint64_t	ipcomps_output;	/* Output IPcomp packets */
	uint64_t	ipcomps_invalid;/* Trying to use an invalid TDB */
	uint64_t	ipcomps_ibytes;	/* Input bytes */
	uint64_t	ipcomps_obytes;	/* Output bytes */
	uint64_t	ipcomps_toobig;	/* Packet got > IP_MAXPACKET */
	uint64_t	ipcomps_pdrops;	/* Packet blocked due to policy */
	uint64_t	ipcomps_crypto;	/* "Crypto" processing failure */
	uint64_t	ipcomps_hist[IPCOMP_ALG_MAX];/* Per-algorithm op count */
	uint64_t	ipcomps_threshold; /* Packet < comp. algo. threshold. */
	uint64_t	ipcomps_uncompr; /* Compression was useles. */
};

#ifdef _KERNEL
#include <sys/counter.h>

VNET_DECLARE(int, ipcomp_enable);
VNET_PCPUSTAT_DECLARE(struct ipcompstat, ipcompstat);

#define	IPCOMPSTAT_ADD(name, val)	\
    VNET_PCPUSTAT_ADD(struct ipcompstat, ipcompstat, name, (val))
#define	IPCOMPSTAT_INC(name)		IPCOMPSTAT_ADD(name, 1)
#define	V_ipcomp_enable		VNET(ipcomp_enable)
#endif /* _KERNEL */
#endif /*_NETIPSEC_IPCOMP_VAR_H_*/
