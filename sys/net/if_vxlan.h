/*-
 * Copyright (c) 2014, Bryan Venteicher <bryanv@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _NET_IF_VXLAN_H_
#define _NET_IF_VXLAN_H_

#include <sys/types.h>
#include <sys/socket.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <netinet/in.h>

struct vxlan_header {
	uint32_t	vxlh_flags;
	uint32_t	vxlh_vni;
};

#define VXLAN_HDR_FLAGS_VALID_VNI	0x08000000
#define VXLAN_HDR_VNI_SHIFT		8

#define VXLAN_VNI_MAX	(1 << 24)
#define VXLAN_VNI_MASK	(VXLAN_VNI_MAX - 1)

/*
 * The port assigned by IANA is 4789, but some early implementations
 * (like Linux) use 8472 instead. If not specified, we default to
 * the IANA port.
 */
#define VXLAN_PORT		4789
#define VXLAN_LEGACY_PORT	8472

union vxlan_sockaddr {
	struct sockaddr		sa;
	struct sockaddr_in	in4;
	struct sockaddr_in6	in6;
};

struct ifvxlanparam {
	uint64_t		vxlp_with;

#define VXLAN_PARAM_WITH_VNI		0x0001
#define VXLAN_PARAM_WITH_LOCAL_ADDR4	0x0002
#define VXLAN_PARAM_WITH_LOCAL_ADDR6	0x0004
#define VXLAN_PARAM_WITH_REMOTE_ADDR4	0x0008
#define VXLAN_PARAM_WITH_REMOTE_ADDR6	0x0010
#define VXLAN_PARAM_WITH_LOCAL_PORT	0x0020
#define VXLAN_PARAM_WITH_REMOTE_PORT	0x0040
#define VXLAN_PARAM_WITH_PORT_RANGE	0x0080
#define VXLAN_PARAM_WITH_FTABLE_TIMEOUT	0x0100
#define VXLAN_PARAM_WITH_FTABLE_MAX	0x0200
#define VXLAN_PARAM_WITH_MULTICAST_IF	0x0400
#define VXLAN_PARAM_WITH_TTL		0x0800
#define VXLAN_PARAM_WITH_LEARN		0x1000

	uint32_t		vxlp_vni;
	union vxlan_sockaddr 	vxlp_local_sa;
	union vxlan_sockaddr 	vxlp_remote_sa;
	uint16_t		vxlp_local_port;
	uint16_t		vxlp_remote_port;
	uint16_t		vxlp_min_port;
	uint16_t		vxlp_max_port;
	char			vxlp_mc_ifname[IFNAMSIZ];
	uint32_t		vxlp_ftable_timeout;
	uint32_t		vxlp_ftable_max;
	uint8_t			vxlp_ttl;
	uint8_t			vxlp_learn;
};

#define VXLAN_SOCKADDR_IS_IPV4(_vxsin)	((_vxsin)->sa.sa_family == AF_INET)
#define VXLAN_SOCKADDR_IS_IPV6(_vxsin)	((_vxsin)->sa.sa_family == AF_INET6)
#define VXLAN_SOCKADDR_IS_IPV46(_vxsin) \
    (VXLAN_SOCKADDR_IS_IPV4(_vxsin) || VXLAN_SOCKADDR_IS_IPV6(_vxsin))

#define VXLAN_CMD_GET_CONFIG		0
#define VXLAN_CMD_SET_VNI		1
#define VXLAN_CMD_SET_LOCAL_ADDR	2
#define VXLAN_CMD_SET_REMOTE_ADDR	4
#define VXLAN_CMD_SET_LOCAL_PORT	5
#define VXLAN_CMD_SET_REMOTE_PORT	6
#define VXLAN_CMD_SET_PORT_RANGE	7
#define VXLAN_CMD_SET_FTABLE_TIMEOUT	8
#define VXLAN_CMD_SET_FTABLE_MAX	9
#define VXLAN_CMD_SET_MULTICAST_IF	10
#define VXLAN_CMD_SET_TTL		11
#define VXLAN_CMD_SET_LEARN		12
#define VXLAN_CMD_FTABLE_ENTRY_ADD	13
#define VXLAN_CMD_FTABLE_ENTRY_REM	14
#define VXLAN_CMD_FLUSH			15

struct ifvxlancfg {
	uint32_t		vxlc_vni;
	union vxlan_sockaddr	vxlc_local_sa;
	union vxlan_sockaddr	vxlc_remote_sa;
	uint32_t		vxlc_mc_ifindex;
	uint32_t		vxlc_ftable_cnt;
	uint32_t		vxlc_ftable_max;
	uint32_t		vxlc_ftable_timeout;
	uint16_t		vxlc_port_min;
	uint16_t		vxlc_port_max;
	uint8_t			vxlc_learn;
	uint8_t			vxlc_ttl;
};

struct ifvxlancmd {
	uint32_t		vxlcmd_flags;
#define VXLAN_CMD_FLAG_FLUSH_ALL	0x0001
#define VXLAN_CMD_FLAG_LEARN		0x0002

	uint32_t		vxlcmd_vni;
	uint32_t		vxlcmd_ftable_timeout;
	uint32_t		vxlcmd_ftable_max;
	uint16_t		vxlcmd_port;
	uint16_t		vxlcmd_port_min;
	uint16_t		vxlcmd_port_max;
	uint8_t			vxlcmd_mac[ETHER_ADDR_LEN];
	uint8_t			vxlcmd_ttl;
	union vxlan_sockaddr	vxlcmd_sa;
	char			vxlcmd_ifname[IFNAMSIZ];
};

#endif /* _NET_IF_VXLAN_H_ */
