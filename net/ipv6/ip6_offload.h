/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *	IPV6 GSO/GRO offload support
 *	Linux INET6 implementation
 */

#ifndef __ip6_offload_h
#define __ip6_offload_h

int ipv6_exthdrs_offload_init(void);
int udpv6_offload_init(void);
int udpv6_offload_exit(void);
int tcpv6_offload_init(void);

#endif
