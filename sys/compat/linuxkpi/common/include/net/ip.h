/*-
 * Copyright (c) 2010 Isilon Systems, Inc.
 * Copyright (c) 2010 iX Systems, Inc.
 * Copyright (c) 2010 Panasas, Inc.
 * Copyright (c) 2013, 2014 Mellanox Technologies, Ltd.
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
#ifndef _LINUX_NET_IP_H_
#define	_LINUX_NET_IP_H_

#include "opt_inet.h"

#include <sys/types.h>
#include <sys/socket.h>

#include <net/if_types.h>
#include <net/if.h>
#include <net/if_var.h>

#include <netinet/in.h>
#include <netinet/in_pcb.h>

static inline void
inet_get_local_port_range(struct vnet *vnet, int *low, int *high)
{
#ifdef INET
	CURVNET_SET_QUIET(vnet);
	*low = V_ipport_firstauto;
	*high = V_ipport_lastauto;
	CURVNET_RESTORE();
#else
	*low = IPPORT_EPHEMERALFIRST;     /* 10000 */
	*high = IPPORT_EPHEMERALLAST;     /* 65535 */
#endif
}

static inline void
ip_eth_mc_map(uint32_t addr, char *buf)
{

	addr = ntohl(addr);

	buf[0] = 0x01;
	buf[1] = 0x00;
	buf[2] = 0x5e;
	buf[3] = (addr >> 16) & 0x7f;
	buf[4] = (addr >> 8) & 0xff;
	buf[5] = (addr & 0xff);
}

static inline void
ip_ib_mc_map(uint32_t addr, const unsigned char *bcast, char *buf)
{
	unsigned char scope;

	addr = ntohl(addr);
	scope = bcast[5] & 0xF;
	buf[0] = 0;
	buf[1] = 0xff;
	buf[2] = 0xff;
	buf[3] = 0xff;
	buf[4] = 0xff;
	buf[5] = 0x10 | scope;
	buf[6] = 0x40;
	buf[7] = 0x1b;
	buf[8] = bcast[8];
	buf[9] = bcast[9];
	buf[10] = 0;
	buf[11] = 0;
	buf[12] = 0;
	buf[13] = 0;
	buf[14] = 0;
	buf[15] = 0;
	buf[16] = (addr >> 24) & 0xff;
	buf[17] = (addr >> 16) & 0xff;
	buf[18] = (addr >> 8) & 0xff;
	buf[19] = addr & 0xff;
}

#endif	/* _LINUX_NET_IP_H_ */
