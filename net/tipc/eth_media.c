/*
 * net/tipc/eth_media.c: Ethernet bearer support for TIPC
 *
 * Copyright (c) 2001-2007, 2013, Ericsson AB
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

#define ETH_ADDR_OFFSET	4	/* message header offset of MAC address */

/**
 * eth_media_addr_set - initialize Ethernet media address structure
 *
 * Media-dependent "value" field stores MAC address in first 6 bytes
 * and zeroes out the remaining bytes.
 */
static void eth_media_addr_set(const struct tipc_bearer *tb_ptr,
			       struct tipc_media_addr *a, char *mac)
{
	memcpy(a->value, mac, ETH_ALEN);
	memset(a->value + ETH_ALEN, 0, sizeof(a->value) - ETH_ALEN);
	a->media_id = TIPC_MEDIA_TYPE_ETH;
	a->broadcast = !memcmp(mac, tb_ptr->bcast_addr.value, ETH_ALEN);
}

/**
 * send_msg - send a TIPC message out over an Ethernet interface
 */
static int send_msg(struct sk_buff *buf, struct tipc_bearer *tb_ptr,
		    struct tipc_media_addr *dest)
{
	struct sk_buff *clone;
	int delta;
	struct net_device *dev = tb_ptr->dev;

	clone = skb_clone(buf, GFP_ATOMIC);
	if (!clone)
		return 0;

	delta = dev->hard_header_len - skb_headroom(buf);
	if ((delta > 0) &&
	    pskb_expand_head(clone, SKB_DATA_ALIGN(delta), 0, GFP_ATOMIC)) {
		kfree_skb(clone);
		return 0;
	}

	skb_reset_network_header(clone);
	clone->dev = dev;
	clone->protocol = htons(ETH_P_TIPC);
	dev_hard_header(clone, dev, ETH_P_TIPC, dest->value,
			dev->dev_addr, clone->len);
	dev_queue_xmit(clone);
	return 0;
}

/**
 * enable_media - attach TIPC bearer to an Ethernet interface
 */
static int enable_media(struct tipc_bearer *tb_ptr)
{
	struct net_device *dev;
	char *driver_name = strchr((const char *)tb_ptr->name, ':') + 1;

	/* Find device with specified name */
	dev = dev_get_by_name(&init_net, driver_name);
	if (!dev)
		return -ENODEV;

	/* Associate TIPC bearer with Ethernet bearer */
	tb_ptr->dev = dev;
	tb_ptr->usr_handle = NULL;
	memset(tb_ptr->bcast_addr.value, 0, sizeof(tb_ptr->bcast_addr.value));
	memcpy(tb_ptr->bcast_addr.value, dev->broadcast, ETH_ALEN);
	tb_ptr->bcast_addr.media_id = TIPC_MEDIA_TYPE_ETH;
	tb_ptr->bcast_addr.broadcast = 1;
	tb_ptr->mtu = dev->mtu;
	eth_media_addr_set(tb_ptr, &tb_ptr->addr, (char *)dev->dev_addr);
	rcu_assign_pointer(dev->tipc_ptr, tb_ptr);
	return 0;
}

/**
 * disable_media - detach TIPC bearer from an Ethernet interface
 *
 * Mark Ethernet bearer as inactive so that incoming buffers are thrown away,
 * then get worker thread to complete bearer cleanup.  (Can't do cleanup
 * here because cleanup code needs to sleep and caller holds spinlocks.)
 */
static void disable_media(struct tipc_bearer *tb_ptr)
{
	RCU_INIT_POINTER(tb_ptr->dev->tipc_ptr, NULL);
	dev_put(tb_ptr->dev);
}

/**
 * eth_addr2str - convert Ethernet address to string
 */
static int eth_addr2str(struct tipc_media_addr *a, char *str_buf, int str_size)
{
	if (str_size < 18)	/* 18 = strlen("aa:bb:cc:dd:ee:ff\0") */
		return 1;

	sprintf(str_buf, "%pM", a->value);
	return 0;
}

/**
 * eth_str2addr - convert Ethernet address format to message header format
 */
static int eth_addr2msg(struct tipc_media_addr *a, char *msg_area)
{
	memset(msg_area, 0, TIPC_MEDIA_ADDR_SIZE);
	msg_area[TIPC_MEDIA_TYPE_OFFSET] = TIPC_MEDIA_TYPE_ETH;
	memcpy(msg_area + ETH_ADDR_OFFSET, a->value, ETH_ALEN);
	return 0;
}

/**
 * eth_str2addr - convert message header address format to Ethernet format
 */
static int eth_msg2addr(const struct tipc_bearer *tb_ptr,
			struct tipc_media_addr *a, char *msg_area)
{
	if (msg_area[TIPC_MEDIA_TYPE_OFFSET] != TIPC_MEDIA_TYPE_ETH)
		return 1;

	eth_media_addr_set(tb_ptr, a, msg_area + ETH_ADDR_OFFSET);
	return 0;
}

/*
 * Ethernet media registration info
 */
struct tipc_media eth_media_info = {
	.send_msg	= send_msg,
	.enable_media	= enable_media,
	.disable_media	= disable_media,
	.addr2str	= eth_addr2str,
	.addr2msg	= eth_addr2msg,
	.msg2addr	= eth_msg2addr,
	.priority	= TIPC_DEF_LINK_PRI,
	.tolerance	= TIPC_DEF_LINK_TOL,
	.window		= TIPC_DEF_LINK_WIN,
	.type_id	= TIPC_MEDIA_TYPE_ETH,
	.name		= "eth"
};

