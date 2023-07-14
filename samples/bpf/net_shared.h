// SPDX-License-Identifier: GPL-2.0
#ifndef _NET_SHARED_H
#define _NET_SHARED_H

#define AF_INET		2
#define AF_INET6	10

#define ETH_ALEN 6
#define ETH_P_802_3_MIN 0x0600
#define ETH_P_8021Q 0x8100
#define ETH_P_8021AD 0x88A8
#define ETH_P_IP 0x0800
#define ETH_P_IPV6 0x86DD
#define ETH_P_ARP 0x0806
#define IPPROTO_ICMPV6 58

#define TC_ACT_OK		0
#define TC_ACT_SHOT		2

#if defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__) && \
	__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define bpf_ntohs(x)		__builtin_bswap16(x)
#define bpf_htons(x)		__builtin_bswap16(x)
#elif defined(__BYTE_ORDER__) && defined(__ORDER_BIG_ENDIAN__) && \
	__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define bpf_ntohs(x)		(x)
#define bpf_htons(x)		(x)
#else
# error "Endianness detection needs to be set up for your compiler?!"
#endif

#endif
