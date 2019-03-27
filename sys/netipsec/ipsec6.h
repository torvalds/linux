/*	$FreeBSD$	*/
/*	$KAME: ipsec.h,v 1.44 2001/03/23 08:08:47 itojun Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (C) 1995, 1996, 1997, and 1998 WIDE Project.
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

/*
 * IPsec controller part.
 */

#ifndef _NETIPSEC_IPSEC6_H_
#define _NETIPSEC_IPSEC6_H_

#include <net/pfkeyv2.h>
#include <netipsec/keydb.h>

#ifdef _KERNEL
#include <sys/counter.h>

VNET_PCPUSTAT_DECLARE(struct ipsecstat, ipsec6stat);
VNET_DECLARE(int, ip6_esp_trans_deflev);
VNET_DECLARE(int, ip6_esp_net_deflev);
VNET_DECLARE(int, ip6_ah_trans_deflev);
VNET_DECLARE(int, ip6_ah_net_deflev);
VNET_DECLARE(int, ip6_ipsec_ecn);

#define	IPSEC6STAT_INC(name)	\
    VNET_PCPUSTAT_ADD(struct ipsecstat, ipsec6stat, name, 1)
#define	V_ip6_esp_trans_deflev	VNET(ip6_esp_trans_deflev)
#define	V_ip6_esp_net_deflev	VNET(ip6_esp_net_deflev)
#define	V_ip6_ah_trans_deflev	VNET(ip6_ah_trans_deflev)
#define	V_ip6_ah_net_deflev	VNET(ip6_ah_net_deflev)
#define	V_ip6_ipsec_ecn		VNET(ip6_ipsec_ecn)

struct inpcb;
struct secpolicy *ipsec6_checkpolicy(const struct mbuf *,
    struct inpcb *, int *, int);

void ipsec6_setsockaddrs(const struct mbuf *, union sockaddr_union *,
    union sockaddr_union *);
int ipsec6_input(struct mbuf *, int, int);
int ipsec6_in_reject(const struct mbuf *, struct inpcb *);
int ipsec6_forward(struct mbuf *);
int ipsec6_pcbctl(struct inpcb *, struct sockopt *);
int ipsec6_output(struct mbuf *, struct inpcb *);
int ipsec6_capability(struct mbuf *, u_int);
int ipsec6_common_input_cb(struct mbuf *, struct secasvar *, int, int);
int ipsec6_process_packet(struct mbuf *, struct secpolicy *, struct inpcb *);

int ip6_ipsec_filtertunnel(struct mbuf *);
int ip6_ipsec_pcbctl(struct inpcb *, struct sockopt *);
#endif /*_KERNEL*/

#endif /*_NETIPSEC_IPSEC6_H_*/
