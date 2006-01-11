/*
 * net/tipc/eth_media.c: Ethernet bearer support for TIPC
 * 
 * Copyright (c) 2001-2006, Ericsson AB
 * Copyright (c) 2005, Wind River Systems
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

#include <net/tipc/tipc.h>
#include <net/tipc/tipc_bearer.h>
#include <net/tipc/tipc_msg.h>
#include <linux/netdevice.h>
#include <linux/version.h>

#define MAX_ETH_BEARERS		2
#define TIPC_PROTOCOL		0x88ca
#define ETH_LINK_PRIORITY	10
#define ETH_LINK_TOLERANCE	TIPC_DEF_LINK_TOL


/**
 * struct eth_bearer - Ethernet bearer data structure
 * @bearer: ptr to associated "generic" bearer structure
 * @dev: ptr to associated Ethernet network device
 * @tipc_packet_type: used in binding TIPC to Ethernet driver
 */
 
struct eth_bearer {
	struct tipc_bearer *bearer;
	struct net_device *dev;
	struct packet_type tipc_packet_type;
};

static struct eth_bearer eth_bearers[MAX_ETH_BEARERS];
static int eth_started = 0;
static struct notifier_block notifier;

/**
 * send_msg - send a TIPC message out over an Ethernet interface 
 */

static int send_msg(struct sk_buff *buf, struct tipc_bearer *tb_ptr, 
		    struct tipc_media_addr *dest)
{
	struct sk_buff *clone;
	struct net_device *dev;

	clone = skb_clone(buf, GFP_ATOMIC);
	if (clone) {
		clone->nh.raw = clone->data;
		dev = ((struct eth_bearer *)(tb_ptr->usr_handle))->dev;
		clone->dev = dev;
		dev->hard_header(clone, dev, TIPC_PROTOCOL, 
				 &dest->dev_addr.eth_addr,
				 dev->dev_addr, clone->len);
		dev_queue_xmit(clone);
	}
	return TIPC_OK;
}

/**
 * recv_msg - handle incoming TIPC message from an Ethernet interface
 * 
 * Routine truncates any Ethernet padding/CRC appended to the message,
 * and ensures message size matches actual length
 */

static int recv_msg(struct sk_buff *buf, struct net_device *dev, 
		    struct packet_type *pt, struct net_device *orig_dev)
{
	struct eth_bearer *eb_ptr = (struct eth_bearer *)pt->af_packet_priv;
	u32 size;

	if (likely(eb_ptr->bearer)) {
		size = msg_size((struct tipc_msg *)buf->data);
		skb_trim(buf, size);
		if (likely(buf->len == size)) {
			buf->next = NULL;
			tipc_recv_msg(buf, eb_ptr->bearer);
		} else {
			kfree_skb(buf);
		}
	} else {
		kfree_skb(buf);
	}
	return TIPC_OK;
}

/**
 * enable_bearer - attach TIPC bearer to an Ethernet interface 
 */

static int enable_bearer(struct tipc_bearer *tb_ptr)
{
	struct net_device *dev = dev_base;
	struct eth_bearer *eb_ptr = &eth_bearers[0];
	struct eth_bearer *stop = &eth_bearers[MAX_ETH_BEARERS];
	char *driver_name = strchr((const char *)tb_ptr->name, ':') + 1;

	/* Find device with specified name */

	while (dev && dev->name &&
	       (memcmp(dev->name, driver_name, strlen(dev->name)))) {
		dev = dev->next;
	}
	if (!dev)
		return -ENODEV;

	/* Find Ethernet bearer for device (or create one) */

	for (;(eb_ptr != stop) && eb_ptr->dev && (eb_ptr->dev != dev); eb_ptr++);
	if (eb_ptr == stop)
		return -EDQUOT;
	if (!eb_ptr->dev) {
		eb_ptr->dev = dev;
		eb_ptr->tipc_packet_type.type = __constant_htons(TIPC_PROTOCOL);
		eb_ptr->tipc_packet_type.dev = dev;
		eb_ptr->tipc_packet_type.func = recv_msg;
		eb_ptr->tipc_packet_type.af_packet_priv = eb_ptr;
		INIT_LIST_HEAD(&(eb_ptr->tipc_packet_type.list));
		dev_hold(dev);
		dev_add_pack(&eb_ptr->tipc_packet_type);
	}

	/* Associate TIPC bearer with Ethernet bearer */

	eb_ptr->bearer = tb_ptr;
	tb_ptr->usr_handle = (void *)eb_ptr;
	tb_ptr->mtu = dev->mtu;
	tb_ptr->blocked = 0; 
	tb_ptr->addr.type = htonl(TIPC_MEDIA_TYPE_ETH);
	memcpy(&tb_ptr->addr.dev_addr, &dev->dev_addr, ETH_ALEN);
	return 0;
}

/**
 * disable_bearer - detach TIPC bearer from an Ethernet interface 
 *
 * We really should do dev_remove_pack() here, but this function can not be
 * called at tasklet level. => Use eth_bearer->bearer as a flag to throw away
 * incoming buffers, & postpone dev_remove_pack() to eth_media_stop() on exit.
 */

static void disable_bearer(struct tipc_bearer *tb_ptr)
{
	((struct eth_bearer *)tb_ptr->usr_handle)->bearer = 0;
}

/**
 * recv_notification - handle device updates from OS
 *
 * Change the state of the Ethernet bearer (if any) associated with the 
 * specified device.
 */

static int recv_notification(struct notifier_block *nb, unsigned long evt, 
			     void *dv)
{
	struct net_device *dev = (struct net_device *)dv;
	struct eth_bearer *eb_ptr = &eth_bearers[0];
	struct eth_bearer *stop = &eth_bearers[MAX_ETH_BEARERS];

	while ((eb_ptr->dev != dev)) {
		if (++eb_ptr == stop)
			return NOTIFY_DONE;	/* couldn't find device */
	}
	if (!eb_ptr->bearer)
		return NOTIFY_DONE;		/* bearer had been disabled */

        eb_ptr->bearer->mtu = dev->mtu;

	switch (evt) {
	case NETDEV_CHANGE:
		if (netif_carrier_ok(dev))
			tipc_continue(eb_ptr->bearer);
		else
			tipc_block_bearer(eb_ptr->bearer->name);
		break;
	case NETDEV_UP:
		tipc_continue(eb_ptr->bearer);
		break;
	case NETDEV_DOWN:
		tipc_block_bearer(eb_ptr->bearer->name);
		break;
	case NETDEV_CHANGEMTU:
        case NETDEV_CHANGEADDR:
		tipc_block_bearer(eb_ptr->bearer->name);
                tipc_continue(eb_ptr->bearer);
		break;
	case NETDEV_UNREGISTER:
        case NETDEV_CHANGENAME:
		tipc_disable_bearer(eb_ptr->bearer->name);
		break;
	}
	return NOTIFY_OK;
}

/**
 * eth_addr2str - convert Ethernet address to string
 */

static char *eth_addr2str(struct tipc_media_addr *a, char *str_buf, int str_size)
{                       
	unchar *addr = (unchar *)&a->dev_addr;

	if (str_size < 18)
		*str_buf = '\0';
	else
		sprintf(str_buf, "%02x:%02x:%02x:%02x:%02x:%02x",
			addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);
	return str_buf;
}

/**
 * eth_media_start - activate Ethernet bearer support
 *
 * Register Ethernet media type with TIPC bearer code.  Also register
 * with OS for notifications about device state changes.
 */

int eth_media_start(void)
{                       
	struct tipc_media_addr bcast_addr;
	int res;

	if (eth_started)
		return -EINVAL;

	memset(&bcast_addr, 0xff, sizeof(bcast_addr));
	memset(eth_bearers, 0, sizeof(eth_bearers));

	res = tipc_register_media(TIPC_MEDIA_TYPE_ETH, "eth",
				  enable_bearer, disable_bearer, send_msg, 
				  eth_addr2str, &bcast_addr, ETH_LINK_PRIORITY, 
				  ETH_LINK_TOLERANCE, TIPC_DEF_LINK_WIN);
	if (res)
		return res;

	notifier.notifier_call = &recv_notification;
	notifier.priority = 0;
	res = register_netdevice_notifier(&notifier);
	if (!res)
		eth_started = 1;
	return res;
}

/**
 * eth_media_stop - deactivate Ethernet bearer support
 */

void eth_media_stop(void)
{
	int i;

	if (!eth_started)
		return;

	unregister_netdevice_notifier(&notifier);
	for (i = 0; i < MAX_ETH_BEARERS ; i++) {
		if (eth_bearers[i].bearer) {
			eth_bearers[i].bearer->blocked = 1;
			eth_bearers[i].bearer = 0;
		}
		if (eth_bearers[i].dev) {
			dev_remove_pack(&eth_bearers[i].tipc_packet_type);
			dev_put(eth_bearers[i].dev);
		}
	}
	memset(&eth_bearers, 0, sizeof(eth_bearers));
	eth_started = 0;
}
