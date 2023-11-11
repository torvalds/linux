// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
/* af_can.c - Protocol family CAN core module
 *            (used by different CAN protocol modules)
 *
 * Copyright (c) 2002-2017 Volkswagen Group Electronic Research
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
 * 3. Neither the name of Volkswagen nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * Alternatively, provided that this notice is retained in full, this
 * software may be distributed under the terms of the GNU General
 * Public License ("GPL") version 2, in which case the provisions of the
 * GPL apply INSTEAD OF those given above.
 *
 * The provided data structures and external interfaces from this code
 * are not restricted to be used by modules with a GPL compatible license.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 *
 */

#include <linux/module.h>
#include <linux/stddef.h>
#include <linux/init.h>
#include <linux/kmod.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/rcupdate.h>
#include <linux/uaccess.h>
#include <linux/net.h>
#include <linux/netdevice.h>
#include <linux/socket.h>
#include <linux/if_ether.h>
#include <linux/if_arp.h>
#include <linux/skbuff.h>
#include <linux/can.h>
#include <linux/can/core.h>
#include <linux/can/skb.h>
#include <linux/can/can-ml.h>
#include <linux/ratelimit.h>
#include <net/net_namespace.h>
#include <net/sock.h>

#include "af_can.h"

MODULE_DESCRIPTION("Controller Area Network PF_CAN core");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Urs Thuermann <urs.thuermann@volkswagen.de>, "
	      "Oliver Hartkopp <oliver.hartkopp@volkswagen.de>");

MODULE_ALIAS_NETPROTO(PF_CAN);

static int stats_timer __read_mostly = 1;
module_param(stats_timer, int, 0444);
MODULE_PARM_DESC(stats_timer, "enable timer for statistics (default:on)");

static struct kmem_cache *rcv_cache __read_mostly;

/* table of registered CAN protocols */
static const struct can_proto __rcu *proto_tab[CAN_NPROTO] __read_mostly;
static DEFINE_MUTEX(proto_tab_lock);

static atomic_t skbcounter = ATOMIC_INIT(0);

/* af_can socket functions */

void can_sock_destruct(struct sock *sk)
{
	skb_queue_purge(&sk->sk_receive_queue);
	skb_queue_purge(&sk->sk_error_queue);
}
EXPORT_SYMBOL(can_sock_destruct);

static const struct can_proto *can_get_proto(int protocol)
{
	const struct can_proto *cp;

	rcu_read_lock();
	cp = rcu_dereference(proto_tab[protocol]);
	if (cp && !try_module_get(cp->prot->owner))
		cp = NULL;
	rcu_read_unlock();

	return cp;
}

static inline void can_put_proto(const struct can_proto *cp)
{
	module_put(cp->prot->owner);
}

static int can_create(struct net *net, struct socket *sock, int protocol,
		      int kern)
{
	struct sock *sk;
	const struct can_proto *cp;
	int err = 0;

	sock->state = SS_UNCONNECTED;

	if (protocol < 0 || protocol >= CAN_NPROTO)
		return -EINVAL;

	cp = can_get_proto(protocol);

#ifdef CONFIG_MODULES
	if (!cp) {
		/* try to load protocol module if kernel is modular */

		err = request_module("can-proto-%d", protocol);

		/* In case of error we only print a message but don't
		 * return the error code immediately.  Below we will
		 * return -EPROTONOSUPPORT
		 */
		if (err)
			pr_err_ratelimited("can: request_module (can-proto-%d) failed.\n",
					   protocol);

		cp = can_get_proto(protocol);
	}
#endif

	/* check for available protocol and correct usage */

	if (!cp)
		return -EPROTONOSUPPORT;

	if (cp->type != sock->type) {
		err = -EPROTOTYPE;
		goto errout;
	}

	sock->ops = cp->ops;

	sk = sk_alloc(net, PF_CAN, GFP_KERNEL, cp->prot, kern);
	if (!sk) {
		err = -ENOMEM;
		goto errout;
	}

	sock_init_data(sock, sk);
	sk->sk_destruct = can_sock_destruct;

	if (sk->sk_prot->init)
		err = sk->sk_prot->init(sk);

	if (err) {
		/* release sk on errors */
		sock_orphan(sk);
		sock_put(sk);
	}

 errout:
	can_put_proto(cp);
	return err;
}

/* af_can tx path */

/**
 * can_send - transmit a CAN frame (optional with local loopback)
 * @skb: pointer to socket buffer with CAN frame in data section
 * @loop: loopback for listeners on local CAN sockets (recommended default!)
 *
 * Due to the loopback this routine must not be called from hardirq context.
 *
 * Return:
 *  0 on success
 *  -ENETDOWN when the selected interface is down
 *  -ENOBUFS on full driver queue (see net_xmit_errno())
 *  -ENOMEM when local loopback failed at calling skb_clone()
 *  -EPERM when trying to send on a non-CAN interface
 *  -EMSGSIZE CAN frame size is bigger than CAN interface MTU
 *  -EINVAL when the skb->data does not contain a valid CAN frame
 */
int can_send(struct sk_buff *skb, int loop)
{
	struct sk_buff *newskb = NULL;
	struct can_pkg_stats *pkg_stats = dev_net(skb->dev)->can.pkg_stats;
	int err = -EINVAL;

	if (can_is_canxl_skb(skb)) {
		skb->protocol = htons(ETH_P_CANXL);
	} else if (can_is_can_skb(skb)) {
		skb->protocol = htons(ETH_P_CAN);
	} else if (can_is_canfd_skb(skb)) {
		struct canfd_frame *cfd = (struct canfd_frame *)skb->data;

		skb->protocol = htons(ETH_P_CANFD);

		/* set CAN FD flag for CAN FD frames by default */
		cfd->flags |= CANFD_FDF;
	} else {
		goto inval_skb;
	}

	/* Make sure the CAN frame can pass the selected CAN netdevice. */
	if (unlikely(skb->len > skb->dev->mtu)) {
		err = -EMSGSIZE;
		goto inval_skb;
	}

	if (unlikely(skb->dev->type != ARPHRD_CAN)) {
		err = -EPERM;
		goto inval_skb;
	}

	if (unlikely(!(skb->dev->flags & IFF_UP))) {
		err = -ENETDOWN;
		goto inval_skb;
	}

	skb->ip_summed = CHECKSUM_UNNECESSARY;

	skb_reset_mac_header(skb);
	skb_reset_network_header(skb);
	skb_reset_transport_header(skb);

	if (loop) {
		/* local loopback of sent CAN frames */

		/* indication for the CAN driver: do loopback */
		skb->pkt_type = PACKET_LOOPBACK;

		/* The reference to the originating sock may be required
		 * by the receiving socket to check whether the frame is
		 * its own. Example: can_raw sockopt CAN_RAW_RECV_OWN_MSGS
		 * Therefore we have to ensure that skb->sk remains the
		 * reference to the originating sock by restoring skb->sk
		 * after each skb_clone() or skb_orphan() usage.
		 */

		if (!(skb->dev->flags & IFF_ECHO)) {
			/* If the interface is not capable to do loopback
			 * itself, we do it here.
			 */
			newskb = skb_clone(skb, GFP_ATOMIC);
			if (!newskb) {
				kfree_skb(skb);
				return -ENOMEM;
			}

			can_skb_set_owner(newskb, skb->sk);
			newskb->ip_summed = CHECKSUM_UNNECESSARY;
			newskb->pkt_type = PACKET_BROADCAST;
		}
	} else {
		/* indication for the CAN driver: no loopback required */
		skb->pkt_type = PACKET_HOST;
	}

	/* send to netdevice */
	err = dev_queue_xmit(skb);
	if (err > 0)
		err = net_xmit_errno(err);

	if (err) {
		kfree_skb(newskb);
		return err;
	}

	if (newskb)
		netif_rx(newskb);

	/* update statistics */
	pkg_stats->tx_frames++;
	pkg_stats->tx_frames_delta++;

	return 0;

inval_skb:
	kfree_skb(skb);
	return err;
}
EXPORT_SYMBOL(can_send);

/* af_can rx path */

static struct can_dev_rcv_lists *can_dev_rcv_lists_find(struct net *net,
							struct net_device *dev)
{
	if (dev) {
		struct can_ml_priv *can_ml = can_get_ml_priv(dev);
		return &can_ml->dev_rcv_lists;
	} else {
		return net->can.rx_alldev_list;
	}
}

/**
 * effhash - hash function for 29 bit CAN identifier reduction
 * @can_id: 29 bit CAN identifier
 *
 * Description:
 *  To reduce the linear traversal in one linked list of _single_ EFF CAN
 *  frame subscriptions the 29 bit identifier is mapped to 10 bits.
 *  (see CAN_EFF_RCV_HASH_BITS definition)
 *
 * Return:
 *  Hash value from 0x000 - 0x3FF ( enforced by CAN_EFF_RCV_HASH_BITS mask )
 */
static unsigned int effhash(canid_t can_id)
{
	unsigned int hash;

	hash = can_id;
	hash ^= can_id >> CAN_EFF_RCV_HASH_BITS;
	hash ^= can_id >> (2 * CAN_EFF_RCV_HASH_BITS);

	return hash & ((1 << CAN_EFF_RCV_HASH_BITS) - 1);
}

/**
 * can_rcv_list_find - determine optimal filterlist inside device filter struct
 * @can_id: pointer to CAN identifier of a given can_filter
 * @mask: pointer to CAN mask of a given can_filter
 * @dev_rcv_lists: pointer to the device filter struct
 *
 * Description:
 *  Returns the optimal filterlist to reduce the filter handling in the
 *  receive path. This function is called by service functions that need
 *  to register or unregister a can_filter in the filter lists.
 *
 *  A filter matches in general, when
 *
 *          <received_can_id> & mask == can_id & mask
 *
 *  so every bit set in the mask (even CAN_EFF_FLAG, CAN_RTR_FLAG) describe
 *  relevant bits for the filter.
 *
 *  The filter can be inverted (CAN_INV_FILTER bit set in can_id) or it can
 *  filter for error messages (CAN_ERR_FLAG bit set in mask). For error msg
 *  frames there is a special filterlist and a special rx path filter handling.
 *
 * Return:
 *  Pointer to optimal filterlist for the given can_id/mask pair.
 *  Consistency checked mask.
 *  Reduced can_id to have a preprocessed filter compare value.
 */
static struct hlist_head *can_rcv_list_find(canid_t *can_id, canid_t *mask,
					    struct can_dev_rcv_lists *dev_rcv_lists)
{
	canid_t inv = *can_id & CAN_INV_FILTER; /* save flag before masking */

	/* filter for error message frames in extra filterlist */
	if (*mask & CAN_ERR_FLAG) {
		/* clear CAN_ERR_FLAG in filter entry */
		*mask &= CAN_ERR_MASK;
		return &dev_rcv_lists->rx[RX_ERR];
	}

	/* with cleared CAN_ERR_FLAG we have a simple mask/value filterpair */

#define CAN_EFF_RTR_FLAGS (CAN_EFF_FLAG | CAN_RTR_FLAG)

	/* ensure valid values in can_mask for 'SFF only' frame filtering */
	if ((*mask & CAN_EFF_FLAG) && !(*can_id & CAN_EFF_FLAG))
		*mask &= (CAN_SFF_MASK | CAN_EFF_RTR_FLAGS);

	/* reduce condition testing at receive time */
	*can_id &= *mask;

	/* inverse can_id/can_mask filter */
	if (inv)
		return &dev_rcv_lists->rx[RX_INV];

	/* mask == 0 => no condition testing at receive time */
	if (!(*mask))
		return &dev_rcv_lists->rx[RX_ALL];

	/* extra filterlists for the subscription of a single non-RTR can_id */
	if (((*mask & CAN_EFF_RTR_FLAGS) == CAN_EFF_RTR_FLAGS) &&
	    !(*can_id & CAN_RTR_FLAG)) {
		if (*can_id & CAN_EFF_FLAG) {
			if (*mask == (CAN_EFF_MASK | CAN_EFF_RTR_FLAGS))
				return &dev_rcv_lists->rx_eff[effhash(*can_id)];
		} else {
			if (*mask == (CAN_SFF_MASK | CAN_EFF_RTR_FLAGS))
				return &dev_rcv_lists->rx_sff[*can_id];
		}
	}

	/* default: filter via can_id/can_mask */
	return &dev_rcv_lists->rx[RX_FIL];
}

/**
 * can_rx_register - subscribe CAN frames from a specific interface
 * @net: the applicable net namespace
 * @dev: pointer to netdevice (NULL => subscribe from 'all' CAN devices list)
 * @can_id: CAN identifier (see description)
 * @mask: CAN mask (see description)
 * @func: callback function on filter match
 * @data: returned parameter for callback function
 * @ident: string for calling module identification
 * @sk: socket pointer (might be NULL)
 *
 * Description:
 *  Invokes the callback function with the received sk_buff and the given
 *  parameter 'data' on a matching receive filter. A filter matches, when
 *
 *          <received_can_id> & mask == can_id & mask
 *
 *  The filter can be inverted (CAN_INV_FILTER bit set in can_id) or it can
 *  filter for error message frames (CAN_ERR_FLAG bit set in mask).
 *
 *  The provided pointer to the sk_buff is guaranteed to be valid as long as
 *  the callback function is running. The callback function must *not* free
 *  the given sk_buff while processing it's task. When the given sk_buff is
 *  needed after the end of the callback function it must be cloned inside
 *  the callback function with skb_clone().
 *
 * Return:
 *  0 on success
 *  -ENOMEM on missing cache mem to create subscription entry
 *  -ENODEV unknown device
 */
int can_rx_register(struct net *net, struct net_device *dev, canid_t can_id,
		    canid_t mask, void (*func)(struct sk_buff *, void *),
		    void *data, char *ident, struct sock *sk)
{
	struct receiver *rcv;
	struct hlist_head *rcv_list;
	struct can_dev_rcv_lists *dev_rcv_lists;
	struct can_rcv_lists_stats *rcv_lists_stats = net->can.rcv_lists_stats;
	int err = 0;

	/* insert new receiver  (dev,canid,mask) -> (func,data) */

	if (dev && (dev->type != ARPHRD_CAN || !can_get_ml_priv(dev)))
		return -ENODEV;

	if (dev && !net_eq(net, dev_net(dev)))
		return -ENODEV;

	rcv = kmem_cache_alloc(rcv_cache, GFP_KERNEL);
	if (!rcv)
		return -ENOMEM;

	spin_lock_bh(&net->can.rcvlists_lock);

	dev_rcv_lists = can_dev_rcv_lists_find(net, dev);
	rcv_list = can_rcv_list_find(&can_id, &mask, dev_rcv_lists);

	rcv->can_id = can_id;
	rcv->mask = mask;
	rcv->matches = 0;
	rcv->func = func;
	rcv->data = data;
	rcv->ident = ident;
	rcv->sk = sk;

	hlist_add_head_rcu(&rcv->list, rcv_list);
	dev_rcv_lists->entries++;

	rcv_lists_stats->rcv_entries++;
	rcv_lists_stats->rcv_entries_max = max(rcv_lists_stats->rcv_entries_max,
					       rcv_lists_stats->rcv_entries);
	spin_unlock_bh(&net->can.rcvlists_lock);

	return err;
}
EXPORT_SYMBOL(can_rx_register);

/* can_rx_delete_receiver - rcu callback for single receiver entry removal */
static void can_rx_delete_receiver(struct rcu_head *rp)
{
	struct receiver *rcv = container_of(rp, struct receiver, rcu);
	struct sock *sk = rcv->sk;

	kmem_cache_free(rcv_cache, rcv);
	if (sk)
		sock_put(sk);
}

/**
 * can_rx_unregister - unsubscribe CAN frames from a specific interface
 * @net: the applicable net namespace
 * @dev: pointer to netdevice (NULL => unsubscribe from 'all' CAN devices list)
 * @can_id: CAN identifier
 * @mask: CAN mask
 * @func: callback function on filter match
 * @data: returned parameter for callback function
 *
 * Description:
 *  Removes subscription entry depending on given (subscription) values.
 */
void can_rx_unregister(struct net *net, struct net_device *dev, canid_t can_id,
		       canid_t mask, void (*func)(struct sk_buff *, void *),
		       void *data)
{
	struct receiver *rcv = NULL;
	struct hlist_head *rcv_list;
	struct can_rcv_lists_stats *rcv_lists_stats = net->can.rcv_lists_stats;
	struct can_dev_rcv_lists *dev_rcv_lists;

	if (dev && dev->type != ARPHRD_CAN)
		return;

	if (dev && !net_eq(net, dev_net(dev)))
		return;

	spin_lock_bh(&net->can.rcvlists_lock);

	dev_rcv_lists = can_dev_rcv_lists_find(net, dev);
	rcv_list = can_rcv_list_find(&can_id, &mask, dev_rcv_lists);

	/* Search the receiver list for the item to delete.  This should
	 * exist, since no receiver may be unregistered that hasn't
	 * been registered before.
	 */
	hlist_for_each_entry_rcu(rcv, rcv_list, list) {
		if (rcv->can_id == can_id && rcv->mask == mask &&
		    rcv->func == func && rcv->data == data)
			break;
	}

	/* Check for bugs in CAN protocol implementations using af_can.c:
	 * 'rcv' will be NULL if no matching list item was found for removal.
	 * As this case may potentially happen when closing a socket while
	 * the notifier for removing the CAN netdev is running we just print
	 * a warning here.
	 */
	if (!rcv) {
		pr_warn("can: receive list entry not found for dev %s, id %03X, mask %03X\n",
			DNAME(dev), can_id, mask);
		goto out;
	}

	hlist_del_rcu(&rcv->list);
	dev_rcv_lists->entries--;

	if (rcv_lists_stats->rcv_entries > 0)
		rcv_lists_stats->rcv_entries--;

 out:
	spin_unlock_bh(&net->can.rcvlists_lock);

	/* schedule the receiver item for deletion */
	if (rcv) {
		if (rcv->sk)
			sock_hold(rcv->sk);
		call_rcu(&rcv->rcu, can_rx_delete_receiver);
	}
}
EXPORT_SYMBOL(can_rx_unregister);

static inline void deliver(struct sk_buff *skb, struct receiver *rcv)
{
	rcv->func(skb, rcv->data);
	rcv->matches++;
}

static int can_rcv_filter(struct can_dev_rcv_lists *dev_rcv_lists, struct sk_buff *skb)
{
	struct receiver *rcv;
	int matches = 0;
	struct can_frame *cf = (struct can_frame *)skb->data;
	canid_t can_id = cf->can_id;

	if (dev_rcv_lists->entries == 0)
		return 0;

	if (can_id & CAN_ERR_FLAG) {
		/* check for error message frame entries only */
		hlist_for_each_entry_rcu(rcv, &dev_rcv_lists->rx[RX_ERR], list) {
			if (can_id & rcv->mask) {
				deliver(skb, rcv);
				matches++;
			}
		}
		return matches;
	}

	/* check for unfiltered entries */
	hlist_for_each_entry_rcu(rcv, &dev_rcv_lists->rx[RX_ALL], list) {
		deliver(skb, rcv);
		matches++;
	}

	/* check for can_id/mask entries */
	hlist_for_each_entry_rcu(rcv, &dev_rcv_lists->rx[RX_FIL], list) {
		if ((can_id & rcv->mask) == rcv->can_id) {
			deliver(skb, rcv);
			matches++;
		}
	}

	/* check for inverted can_id/mask entries */
	hlist_for_each_entry_rcu(rcv, &dev_rcv_lists->rx[RX_INV], list) {
		if ((can_id & rcv->mask) != rcv->can_id) {
			deliver(skb, rcv);
			matches++;
		}
	}

	/* check filterlists for single non-RTR can_ids */
	if (can_id & CAN_RTR_FLAG)
		return matches;

	if (can_id & CAN_EFF_FLAG) {
		hlist_for_each_entry_rcu(rcv, &dev_rcv_lists->rx_eff[effhash(can_id)], list) {
			if (rcv->can_id == can_id) {
				deliver(skb, rcv);
				matches++;
			}
		}
	} else {
		can_id &= CAN_SFF_MASK;
		hlist_for_each_entry_rcu(rcv, &dev_rcv_lists->rx_sff[can_id], list) {
			deliver(skb, rcv);
			matches++;
		}
	}

	return matches;
}

static void can_receive(struct sk_buff *skb, struct net_device *dev)
{
	struct can_dev_rcv_lists *dev_rcv_lists;
	struct net *net = dev_net(dev);
	struct can_pkg_stats *pkg_stats = net->can.pkg_stats;
	int matches;

	/* update statistics */
	pkg_stats->rx_frames++;
	pkg_stats->rx_frames_delta++;

	/* create non-zero unique skb identifier together with *skb */
	while (!(can_skb_prv(skb)->skbcnt))
		can_skb_prv(skb)->skbcnt = atomic_inc_return(&skbcounter);

	rcu_read_lock();

	/* deliver the packet to sockets listening on all devices */
	matches = can_rcv_filter(net->can.rx_alldev_list, skb);

	/* find receive list for this device */
	dev_rcv_lists = can_dev_rcv_lists_find(net, dev);
	matches += can_rcv_filter(dev_rcv_lists, skb);

	rcu_read_unlock();

	/* consume the skbuff allocated by the netdevice driver */
	consume_skb(skb);

	if (matches > 0) {
		pkg_stats->matches++;
		pkg_stats->matches_delta++;
	}
}

static int can_rcv(struct sk_buff *skb, struct net_device *dev,
		   struct packet_type *pt, struct net_device *orig_dev)
{
	if (unlikely(dev->type != ARPHRD_CAN || !can_get_ml_priv(dev) || !can_is_can_skb(skb))) {
		pr_warn_once("PF_CAN: dropped non conform CAN skbuff: dev type %d, len %d\n",
			     dev->type, skb->len);

		kfree_skb(skb);
		return NET_RX_DROP;
	}

	can_receive(skb, dev);
	return NET_RX_SUCCESS;
}

static int canfd_rcv(struct sk_buff *skb, struct net_device *dev,
		     struct packet_type *pt, struct net_device *orig_dev)
{
	if (unlikely(dev->type != ARPHRD_CAN || !can_get_ml_priv(dev) || !can_is_canfd_skb(skb))) {
		pr_warn_once("PF_CAN: dropped non conform CAN FD skbuff: dev type %d, len %d\n",
			     dev->type, skb->len);

		kfree_skb(skb);
		return NET_RX_DROP;
	}

	can_receive(skb, dev);
	return NET_RX_SUCCESS;
}

static int canxl_rcv(struct sk_buff *skb, struct net_device *dev,
		     struct packet_type *pt, struct net_device *orig_dev)
{
	if (unlikely(dev->type != ARPHRD_CAN || !can_get_ml_priv(dev) || !can_is_canxl_skb(skb))) {
		pr_warn_once("PF_CAN: dropped non conform CAN XL skbuff: dev type %d, len %d\n",
			     dev->type, skb->len);

		kfree_skb(skb);
		return NET_RX_DROP;
	}

	can_receive(skb, dev);
	return NET_RX_SUCCESS;
}

/* af_can protocol functions */

/**
 * can_proto_register - register CAN transport protocol
 * @cp: pointer to CAN protocol structure
 *
 * Return:
 *  0 on success
 *  -EINVAL invalid (out of range) protocol number
 *  -EBUSY  protocol already in use
 *  -ENOBUF if proto_register() fails
 */
int can_proto_register(const struct can_proto *cp)
{
	int proto = cp->protocol;
	int err = 0;

	if (proto < 0 || proto >= CAN_NPROTO) {
		pr_err("can: protocol number %d out of range\n", proto);
		return -EINVAL;
	}

	err = proto_register(cp->prot, 0);
	if (err < 0)
		return err;

	mutex_lock(&proto_tab_lock);

	if (rcu_access_pointer(proto_tab[proto])) {
		pr_err("can: protocol %d already registered\n", proto);
		err = -EBUSY;
	} else {
		RCU_INIT_POINTER(proto_tab[proto], cp);
	}

	mutex_unlock(&proto_tab_lock);

	if (err < 0)
		proto_unregister(cp->prot);

	return err;
}
EXPORT_SYMBOL(can_proto_register);

/**
 * can_proto_unregister - unregister CAN transport protocol
 * @cp: pointer to CAN protocol structure
 */
void can_proto_unregister(const struct can_proto *cp)
{
	int proto = cp->protocol;

	mutex_lock(&proto_tab_lock);
	BUG_ON(rcu_access_pointer(proto_tab[proto]) != cp);
	RCU_INIT_POINTER(proto_tab[proto], NULL);
	mutex_unlock(&proto_tab_lock);

	synchronize_rcu();

	proto_unregister(cp->prot);
}
EXPORT_SYMBOL(can_proto_unregister);

static int can_pernet_init(struct net *net)
{
	spin_lock_init(&net->can.rcvlists_lock);
	net->can.rx_alldev_list =
		kzalloc(sizeof(*net->can.rx_alldev_list), GFP_KERNEL);
	if (!net->can.rx_alldev_list)
		goto out;
	net->can.pkg_stats = kzalloc(sizeof(*net->can.pkg_stats), GFP_KERNEL);
	if (!net->can.pkg_stats)
		goto out_free_rx_alldev_list;
	net->can.rcv_lists_stats = kzalloc(sizeof(*net->can.rcv_lists_stats), GFP_KERNEL);
	if (!net->can.rcv_lists_stats)
		goto out_free_pkg_stats;

	if (IS_ENABLED(CONFIG_PROC_FS)) {
		/* the statistics are updated every second (timer triggered) */
		if (stats_timer) {
			timer_setup(&net->can.stattimer, can_stat_update,
				    0);
			mod_timer(&net->can.stattimer,
				  round_jiffies(jiffies + HZ));
		}
		net->can.pkg_stats->jiffies_init = jiffies;
		can_init_proc(net);
	}

	return 0;

 out_free_pkg_stats:
	kfree(net->can.pkg_stats);
 out_free_rx_alldev_list:
	kfree(net->can.rx_alldev_list);
 out:
	return -ENOMEM;
}

static void can_pernet_exit(struct net *net)
{
	if (IS_ENABLED(CONFIG_PROC_FS)) {
		can_remove_proc(net);
		if (stats_timer)
			del_timer_sync(&net->can.stattimer);
	}

	kfree(net->can.rx_alldev_list);
	kfree(net->can.pkg_stats);
	kfree(net->can.rcv_lists_stats);
}

/* af_can module init/exit functions */

static struct packet_type can_packet __read_mostly = {
	.type = cpu_to_be16(ETH_P_CAN),
	.func = can_rcv,
};

static struct packet_type canfd_packet __read_mostly = {
	.type = cpu_to_be16(ETH_P_CANFD),
	.func = canfd_rcv,
};

static struct packet_type canxl_packet __read_mostly = {
	.type = cpu_to_be16(ETH_P_CANXL),
	.func = canxl_rcv,
};

static const struct net_proto_family can_family_ops = {
	.family = PF_CAN,
	.create = can_create,
	.owner  = THIS_MODULE,
};

static struct pernet_operations can_pernet_ops __read_mostly = {
	.init = can_pernet_init,
	.exit = can_pernet_exit,
};

static __init int can_init(void)
{
	int err;

	/* check for correct padding to be able to use the structs similarly */
	BUILD_BUG_ON(offsetof(struct can_frame, len) !=
		     offsetof(struct canfd_frame, len) ||
		     offsetof(struct can_frame, data) !=
		     offsetof(struct canfd_frame, data));

	pr_info("can: controller area network core\n");

	rcv_cache = kmem_cache_create("can_receiver", sizeof(struct receiver),
				      0, 0, NULL);
	if (!rcv_cache)
		return -ENOMEM;

	err = register_pernet_subsys(&can_pernet_ops);
	if (err)
		goto out_pernet;

	/* protocol register */
	err = sock_register(&can_family_ops);
	if (err)
		goto out_sock;

	dev_add_pack(&can_packet);
	dev_add_pack(&canfd_packet);
	dev_add_pack(&canxl_packet);

	return 0;

out_sock:
	unregister_pernet_subsys(&can_pernet_ops);
out_pernet:
	kmem_cache_destroy(rcv_cache);

	return err;
}

static __exit void can_exit(void)
{
	/* protocol unregister */
	dev_remove_pack(&canxl_packet);
	dev_remove_pack(&canfd_packet);
	dev_remove_pack(&can_packet);
	sock_unregister(PF_CAN);

	unregister_pernet_subsys(&can_pernet_ops);

	rcu_barrier(); /* Wait for completion of call_rcu()'s */

	kmem_cache_destroy(rcv_cache);
}

module_init(can_init);
module_exit(can_exit);
