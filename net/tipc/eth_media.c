/*
 * net/tipc/eth_media.c: Ethernet bearer support for TIPC
 *
 * Copyright (c) 2001-2007, 2013-2014, Ericsson AB
 * Copyright (c) 2005-2008, 2011-2013, Wind River Systems
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the names of the copyright holders nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "core.h"
#include "bearer.h"

/* Convert Ethernet address (media address format) to string */
static int tipc_eth_addr2str(struct tipc_media_addr *addr,
			     char *strbuf, int bufsz)
{
	if (bufsz < 18)	/* 18 = strlen("aa:bb:cc:dd:ee:ff\0") */
		return 1;

	sprintf(strbuf, "%pM", addr->value);
	return 0;
}

/* Convert from media address format to discovery message addr format */
static int tipc_eth_addr2msg(char *msg, struct tipc_media_addr *addr)
{
	memset(msg, 0, TIPC_MEDIA_INFO_SIZE);
	msg[TIPC_MEDIA_TYPE_OFFSET] = TIPC_MEDIA_TYPE_ETH;
	memcpy(msg + TIPC_MEDIA_ADDR_OFFSET, addr->value, ETH_ALEN);
	return 0;
}

/* Convert raw mac address format to media addr format */
static int tipc_eth_raw2addr(struct tipc_bearer *b,
			     struct tipc_media_addr *addr,
			     char *msg)
{
	char bcast_mac[ETH_ALEN] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

	memset(addr, 0, sizeof(*addr));
	ether_addr_copy(addr->value, msg);
	addr->media_id = TIPC_MEDIA_TYPE_ETH;
	addr->broadcast = !memcmp(addr->value, bcast_mac, ETH_ALEN);
	return 0;
}

/* Convert discovery msg addr format to Ethernet media addr format */
static int tipc_eth_msg2addr(struct tipc_bearer *b,
			     struct tipc_media_addr *addr,
			     char *msg)
{
	/* Skip past preamble: */
	msg += TIPC_MEDIA_ADDR_OFFSET;
	return tipc_eth_raw2addr(b, addr, msg);
}

/* Ethernet media registration info */
struct tipc_media eth_media_info = {
	.send_msg	= tipc_l2_send_msg,
	.enable_media	= tipc_enable_l2_media,
	.disable_media	= tipc_disable_l2_media,
	.addr2str	= tipc_eth_addr2str,
	.addr2msg	= tipc_eth_addr2msg,
	.msg2addr	= tipc_eth_msg2addr,
	.raw2addr	= tipc_eth_raw2addr,
	.priority	= TIPC_DEF_LINK_PRI,
	.tolerance	= TIPC_DEF_LINK_TOL,
	.min_win	= TIPC_DEF_LINK_WIN,
	.max_win	= TIPC_MAX_LINK_WIN,
	.type_id	= TIPC_MEDIA_TYPE_ETH,
	.hwaddr_len	= ETH_ALEN,
	.name		= "eth"
};
