// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
/* gw.c - CAN frame Gateway/Router/Bridge with netlink interface
 *
 * Copyright (c) 2019 Volkswagen Group Electronic Research
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
#include <linux/init.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/rcupdate.h>
#include <linux/rculist.h>
#include <linux/net.h>
#include <linux/netdevice.h>
#include <linux/if_arp.h>
#include <linux/skbuff.h>
#include <linux/can.h>
#include <linux/can/core.h>
#include <linux/can/skb.h>
#include <linux/can/gw.h>
#include <net/rtnetlink.h>
#include <net/net_namespace.h>
#include <net/sock.h>

#define CAN_GW_NAME "can-gw"

MODULE_DESCRIPTION("PF_CAN netlink gateway");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Oliver Hartkopp <oliver.hartkopp@volkswagen.de>");
MODULE_ALIAS(CAN_GW_NAME);

#define CGW_MIN_HOPS 1
#define CGW_MAX_HOPS 6
#define CGW_DEFAULT_HOPS 1

static unsigned int max_hops __read_mostly = CGW_DEFAULT_HOPS;
module_param(max_hops, uint, 0444);
MODULE_PARM_DESC(max_hops,
		 "maximum " CAN_GW_NAME " routing hops for CAN frames "
		 "(valid values: " __stringify(CGW_MIN_HOPS) "-"
		 __stringify(CGW_MAX_HOPS) " hops, "
		 "default: " __stringify(CGW_DEFAULT_HOPS) ")");

static struct notifier_block notifier;
static struct kmem_cache *cgw_cache __read_mostly;

/* structure that contains the (on-the-fly) CAN frame modifications */
struct cf_mod {
	struct {
		struct canfd_frame and;
		struct canfd_frame or;
		struct canfd_frame xor;
		struct canfd_frame set;
	} modframe;
	struct {
		u8 and;
		u8 or;
		u8 xor;
		u8 set;
	} modtype;
	void (*modfunc[MAX_MODFUNCTIONS])(struct canfd_frame *cf,
					  struct cf_mod *mod);

	/* CAN frame checksum calculation after CAN frame modifications */
	struct {
		struct cgw_csum_xor xor;
		struct cgw_csum_crc8 crc8;
	} csum;
	struct {
		void (*xor)(struct canfd_frame *cf,
			    struct cgw_csum_xor *xor);
		void (*crc8)(struct canfd_frame *cf,
			     struct cgw_csum_crc8 *crc8);
	} csumfunc;
	u32 uid;
};

/* So far we just support CAN -> CAN routing and frame modifications.
 *
 * The internal can_can_gw structure contains data and attributes for
 * a CAN -> CAN gateway job.
 */
struct can_can_gw {
	struct can_filter filter;
	int src_idx;
	int dst_idx;
};

/* list entry for CAN gateways jobs */
struct cgw_job {
	struct hlist_node list;
	struct rcu_head rcu;
	u32 handled_frames;
	u32 dropped_frames;
	u32 deleted_frames;
	struct cf_mod mod;
	union {
		/* CAN frame data source */
		struct net_device *dev;
	} src;
	union {
		/* CAN frame data destination */
		struct net_device *dev;
	} dst;
	union {
		struct can_can_gw ccgw;
		/* tbc */
	};
	u8 gwtype;
	u8 limit_hops;
	u16 flags;
};

/* modification functions that are invoked in the hot path in can_can_gw_rcv */

#define MODFUNC(func, op) static void func(struct canfd_frame *cf, \
					   struct cf_mod *mod) { op ; }

MODFUNC(mod_and_id, cf->can_id &= mod->modframe.and.can_id)
MODFUNC(mod_and_len, cf->len &= mod->modframe.and.len)
MODFUNC(mod_and_flags, cf->flags &= mod->modframe.and.flags)
MODFUNC(mod_and_data, *(u64 *)cf->data &= *(u64 *)mod->modframe.and.data)
MODFUNC(mod_or_id, cf->can_id |= mod->modframe.or.can_id)
MODFUNC(mod_or_len, cf->len |= mod->modframe.or.len)
MODFUNC(mod_or_flags, cf->flags |= mod->modframe.or.flags)
MODFUNC(mod_or_data, *(u64 *)cf->data |= *(u64 *)mod->modframe.or.data)
MODFUNC(mod_xor_id, cf->can_id ^= mod->modframe.xor.can_id)
MODFUNC(mod_xor_len, cf->len ^= mod->modframe.xor.len)
MODFUNC(mod_xor_flags, cf->flags ^= mod->modframe.xor.flags)
MODFUNC(mod_xor_data, *(u64 *)cf->data ^= *(u64 *)mod->modframe.xor.data)
MODFUNC(mod_set_id, cf->can_id = mod->modframe.set.can_id)
MODFUNC(mod_set_len, cf->len = mod->modframe.set.len)
MODFUNC(mod_set_flags, cf->flags = mod->modframe.set.flags)
MODFUNC(mod_set_data, *(u64 *)cf->data = *(u64 *)mod->modframe.set.data)

static void mod_and_fddata(struct canfd_frame *cf, struct cf_mod *mod)
{
	int i;

	for (i = 0; i < CANFD_MAX_DLEN; i += 8)
		*(u64 *)(cf->data + i) &= *(u64 *)(mod->modframe.and.data + i);
}

static void mod_or_fddata(struct canfd_frame *cf, struct cf_mod *mod)
{
	int i;

	for (i = 0; i < CANFD_MAX_DLEN; i += 8)
		*(u64 *)(cf->data + i) |= *(u64 *)(mod->modframe.or.data + i);
}

static void mod_xor_fddata(struct canfd_frame *cf, struct cf_mod *mod)
{
	int i;

	for (i = 0; i < CANFD_MAX_DLEN; i += 8)
		*(u64 *)(cf->data + i) ^= *(u64 *)(mod->modframe.xor.data + i);
}

static void mod_set_fddata(struct canfd_frame *cf, struct cf_mod *mod)
{
	memcpy(cf->data, mod->modframe.set.data, CANFD_MAX_DLEN);
}

static void canframecpy(struct canfd_frame *dst, struct can_frame *src)
{
	/* Copy the struct members separately to ensure that no uninitialized
	 * data are copied in the 3 bytes hole of the struct. This is needed
	 * to make easy compares of the data in the struct cf_mod.
	 */

	dst->can_id = src->can_id;
	dst->len = src->can_dlc;
	*(u64 *)dst->data = *(u64 *)src->data;
}

static void canfdframecpy(struct canfd_frame *dst, struct canfd_frame *src)
{
	/* Copy the struct members separately to ensure that no uninitialized
	 * data are copied in the 2 bytes hole of the struct. This is needed
	 * to make easy compares of the data in the struct cf_mod.
	 */

	dst->can_id = src->can_id;
	dst->flags = src->flags;
	dst->len = src->len;
	memcpy(dst->data, src->data, CANFD_MAX_DLEN);
}

static int cgw_chk_csum_parms(s8 fr, s8 to, s8 re, struct rtcanmsg *r)
{
	s8 dlen = CAN_MAX_DLEN;

	if (r->flags & CGW_FLAGS_CAN_FD)
		dlen = CANFD_MAX_DLEN;

	/* absolute dlc values 0 .. 7 => 0 .. 7, e.g. data [0]
	 * relative to received dlc -1 .. -8 :
	 * e.g. for received dlc = 8
	 * -1 => index = 7 (data[7])
	 * -3 => index = 5 (data[5])
	 * -8 => index = 0 (data[0])
	 */

	if (fr >= -dlen && fr < dlen &&
	    to >= -dlen && to < dlen &&
	    re >= -dlen && re < dlen)
		return 0;
	else
		return -EINVAL;
}

static inline int calc_idx(int idx, int rx_len)
{
	if (idx < 0)
		return rx_len + idx;
	else
		return idx;
}

static void cgw_csum_xor_rel(struct canfd_frame *cf, struct cgw_csum_xor *xor)
{
	int from = calc_idx(xor->from_idx, cf->len);
	int to = calc_idx(xor->to_idx, cf->len);
	int res = calc_idx(xor->result_idx, cf->len);
	u8 val = xor->init_xor_val;
	int i;

	if (from < 0 || to < 0 || res < 0)
		return;

	if (from <= to) {
		for (i = from; i <= to; i++)
			val ^= cf->data[i];
	} else {
		for (i = from; i >= to; i--)
			val ^= cf->data[i];
	}

	cf->data[res] = val;
}

static void cgw_csum_xor_pos(struct canfd_frame *cf, struct cgw_csum_xor *xor)
{
	u8 val = xor->init_xor_val;
	int i;

	for (i = xor->from_idx; i <= xor->to_idx; i++)
		val ^= cf->data[i];

	cf->data[xor->result_idx] = val;
}

static void cgw_csum_xor_neg(struct canfd_frame *cf, struct cgw_csum_xor *xor)
{
	u8 val = xor->init_xor_val;
	int i;

	for (i = xor->from_idx; i >= xor->to_idx; i--)
		val ^= cf->data[i];

	cf->data[xor->result_idx] = val;
}

static void cgw_csum_crc8_rel(struct canfd_frame *cf,
			      struct cgw_csum_crc8 *crc8)
{
	int from = calc_idx(crc8->from_idx, cf->len);
	int to = calc_idx(crc8->to_idx, cf->len);
	int res = calc_idx(crc8->result_idx, cf->len);
	u8 crc = crc8->init_crc_val;
	int i;

	if (from < 0 || to < 0 || res < 0)
		return;

	if (from <= to) {
		for (i = crc8->from_idx; i <= crc8->to_idx; i++)
			crc = crc8->crctab[crc ^ cf->data[i]];
	} else {
		for (i = crc8->from_idx; i >= crc8->to_idx; i--)
			crc = crc8->crctab[crc ^ cf->data[i]];
	}

	switch (crc8->profile) {
	case CGW_CRC8PRF_1U8:
		crc = crc8->crctab[crc ^ crc8->profile_data[0]];
		break;

	case  CGW_CRC8PRF_16U8:
		crc = crc8->crctab[crc ^ crc8->profile_data[cf->data[1] & 0xF]];
		break;

	case CGW_CRC8PRF_SFFID_XOR:
		crc = crc8->crctab[crc ^ (cf->can_id & 0xFF) ^
				   (cf->can_id >> 8 & 0xFF)];
		break;
	}

	cf->data[crc8->result_idx] = crc ^ crc8->final_xor_val;
}

static void cgw_csum_crc8_pos(struct canfd_frame *cf,
			      struct cgw_csum_crc8 *crc8)
{
	u8 crc = crc8->init_crc_val;
	int i;

	for (i = crc8->from_idx; i <= crc8->to_idx; i++)
		crc = crc8->crctab[crc ^ cf->data[i]];

	switch (crc8->profile) {
	case CGW_CRC8PRF_1U8:
		crc = crc8->crctab[crc ^ crc8->profile_data[0]];
		break;

	case  CGW_CRC8PRF_16U8:
		crc = crc8->crctab[crc ^ crc8->profile_data[cf->data[1] & 0xF]];
		break;

	case CGW_CRC8PRF_SFFID_XOR:
		crc = crc8->crctab[crc ^ (cf->can_id & 0xFF) ^
				   (cf->can_id >> 8 & 0xFF)];
		break;
	}

	cf->data[crc8->result_idx] = crc ^ crc8->final_xor_val;
}

static void cgw_csum_crc8_neg(struct canfd_frame *cf,
			      struct cgw_csum_crc8 *crc8)
{
	u8 crc = crc8->init_crc_val;
	int i;

	for (i = crc8->from_idx; i >= crc8->to_idx; i--)
		crc = crc8->crctab[crc ^ cf->data[i]];

	switch (crc8->profile) {
	case CGW_CRC8PRF_1U8:
		crc = crc8->crctab[crc ^ crc8->profile_data[0]];
		break;

	case  CGW_CRC8PRF_16U8:
		crc = crc8->crctab[crc ^ crc8->profile_data[cf->data[1] & 0xF]];
		break;

	case CGW_CRC8PRF_SFFID_XOR:
		crc = crc8->crctab[crc ^ (cf->can_id & 0xFF) ^
				   (cf->can_id >> 8 & 0xFF)];
		break;
	}

	cf->data[crc8->result_idx] = crc ^ crc8->final_xor_val;
}

/* the receive & process & send function */
static void can_can_gw_rcv(struct sk_buff *skb, void *data)
{
	struct cgw_job *gwj = (struct cgw_job *)data;
	struct canfd_frame *cf;
	struct sk_buff *nskb;
	int modidx = 0;

	/* process strictly Classic CAN or CAN FD frames */
	if (gwj->flags & CGW_FLAGS_CAN_FD) {
		if (skb->len != CANFD_MTU)
			return;
	} else {
		if (skb->len != CAN_MTU)
			return;
	}

	/* Do not handle CAN frames routed more than 'max_hops' times.
	 * In general we should never catch this delimiter which is intended
	 * to cover a misconfiguration protection (e.g. circular CAN routes).
	 *
	 * The Controller Area Network controllers only accept CAN frames with
	 * correct CRCs - which are not visible in the controller registers.
	 * According to skbuff.h documentation the csum_start element for IP
	 * checksums is undefined/unused when ip_summed == CHECKSUM_UNNECESSARY.
	 * Only CAN skbs can be processed here which already have this property.
	 */

#define cgw_hops(skb) ((skb)->csum_start)

	BUG_ON(skb->ip_summed != CHECKSUM_UNNECESSARY);

	if (cgw_hops(skb) >= max_hops) {
		/* indicate deleted frames due to misconfiguration */
		gwj->deleted_frames++;
		return;
	}

	if (!(gwj->dst.dev->flags & IFF_UP)) {
		gwj->dropped_frames++;
		return;
	}

	/* is sending the skb back to the incoming interface not allowed? */
	if (!(gwj->flags & CGW_FLAGS_CAN_IIF_TX_OK) &&
	    can_skb_prv(skb)->ifindex == gwj->dst.dev->ifindex)
		return;

	/* clone the given skb, which has not been done in can_rcv()
	 *
	 * When there is at least one modification function activated,
	 * we need to copy the skb as we want to modify skb->data.
	 */
	if (gwj->mod.modfunc[0])
		nskb = skb_copy(skb, GFP_ATOMIC);
	else
		nskb = skb_clone(skb, GFP_ATOMIC);

	if (!nskb) {
		gwj->dropped_frames++;
		return;
	}

	/* put the incremented hop counter in the cloned skb */
	cgw_hops(nskb) = cgw_hops(skb) + 1;

	/* first processing of this CAN frame -> adjust to private hop limit */
	if (gwj->limit_hops && cgw_hops(nskb) == 1)
		cgw_hops(nskb) = max_hops - gwj->limit_hops + 1;

	nskb->dev = gwj->dst.dev;

	/* pointer to modifiable CAN frame */
	cf = (struct canfd_frame *)nskb->data;

	/* perform preprocessed modification functions if there are any */
	while (modidx < MAX_MODFUNCTIONS && gwj->mod.modfunc[modidx])
		(*gwj->mod.modfunc[modidx++])(cf, &gwj->mod);

	/* Has the CAN frame been modified? */
	if (modidx) {
		/* get available space for the processed CAN frame type */
		int max_len = nskb->len - offsetof(struct canfd_frame, data);

		/* dlc may have changed, make sure it fits to the CAN frame */
		if (cf->len > max_len) {
			/* delete frame due to misconfiguration */
			gwj->deleted_frames++;
			kfree_skb(nskb);
			return;
		}

		/* check for checksum updates */
		if (gwj->mod.csumfunc.crc8)
			(*gwj->mod.csumfunc.crc8)(cf, &gwj->mod.csum.crc8);

		if (gwj->mod.csumfunc.xor)
			(*gwj->mod.csumfunc.xor)(cf, &gwj->mod.csum.xor);
	}

	/* clear the skb timestamp if not configured the other way */
	if (!(gwj->flags & CGW_FLAGS_CAN_SRC_TSTAMP))
		nskb->tstamp = 0;

	/* send to netdevice */
	if (can_send(nskb, gwj->flags & CGW_FLAGS_CAN_ECHO))
		gwj->dropped_frames++;
	else
		gwj->handled_frames++;
}

static inline int cgw_register_filter(struct net *net, struct cgw_job *gwj)
{
	return can_rx_register(net, gwj->src.dev, gwj->ccgw.filter.can_id,
			       gwj->ccgw.filter.can_mask, can_can_gw_rcv,
			       gwj, "gw", NULL);
}

static inline void cgw_unregister_filter(struct net *net, struct cgw_job *gwj)
{
	can_rx_unregister(net, gwj->src.dev, gwj->ccgw.filter.can_id,
			  gwj->ccgw.filter.can_mask, can_can_gw_rcv, gwj);
}

static int cgw_notifier(struct notifier_block *nb,
			unsigned long msg, void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);
	struct net *net = dev_net(dev);

	if (dev->type != ARPHRD_CAN)
		return NOTIFY_DONE;

	if (msg == NETDEV_UNREGISTER) {
		struct cgw_job *gwj = NULL;
		struct hlist_node *nx;

		ASSERT_RTNL();

		hlist_for_each_entry_safe(gwj, nx, &net->can.cgw_list, list) {
			if (gwj->src.dev == dev || gwj->dst.dev == dev) {
				hlist_del(&gwj->list);
				cgw_unregister_filter(net, gwj);
				synchronize_rcu();
				kmem_cache_free(cgw_cache, gwj);
			}
		}
	}

	return NOTIFY_DONE;
}

static int cgw_put_job(struct sk_buff *skb, struct cgw_job *gwj, int type,
		       u32 pid, u32 seq, int flags)
{
	struct rtcanmsg *rtcan;
	struct nlmsghdr *nlh;

	nlh = nlmsg_put(skb, pid, seq, type, sizeof(*rtcan), flags);
	if (!nlh)
		return -EMSGSIZE;

	rtcan = nlmsg_data(nlh);
	rtcan->can_family = AF_CAN;
	rtcan->gwtype = gwj->gwtype;
	rtcan->flags = gwj->flags;

	/* add statistics if available */

	if (gwj->handled_frames) {
		if (nla_put_u32(skb, CGW_HANDLED, gwj->handled_frames) < 0)
			goto cancel;
	}

	if (gwj->dropped_frames) {
		if (nla_put_u32(skb, CGW_DROPPED, gwj->dropped_frames) < 0)
			goto cancel;
	}

	if (gwj->deleted_frames) {
		if (nla_put_u32(skb, CGW_DELETED, gwj->deleted_frames) < 0)
			goto cancel;
	}

	/* check non default settings of attributes */

	if (gwj->limit_hops) {
		if (nla_put_u8(skb, CGW_LIM_HOPS, gwj->limit_hops) < 0)
			goto cancel;
	}

	if (gwj->flags & CGW_FLAGS_CAN_FD) {
		struct cgw_fdframe_mod mb;

		if (gwj->mod.modtype.and) {
			memcpy(&mb.cf, &gwj->mod.modframe.and, sizeof(mb.cf));
			mb.modtype = gwj->mod.modtype.and;
			if (nla_put(skb, CGW_FDMOD_AND, sizeof(mb), &mb) < 0)
				goto cancel;
		}

		if (gwj->mod.modtype.or) {
			memcpy(&mb.cf, &gwj->mod.modframe.or, sizeof(mb.cf));
			mb.modtype = gwj->mod.modtype.or;
			if (nla_put(skb, CGW_FDMOD_OR, sizeof(mb), &mb) < 0)
				goto cancel;
		}

		if (gwj->mod.modtype.xor) {
			memcpy(&mb.cf, &gwj->mod.modframe.xor, sizeof(mb.cf));
			mb.modtype = gwj->mod.modtype.xor;
			if (nla_put(skb, CGW_FDMOD_XOR, sizeof(mb), &mb) < 0)
				goto cancel;
		}

		if (gwj->mod.modtype.set) {
			memcpy(&mb.cf, &gwj->mod.modframe.set, sizeof(mb.cf));
			mb.modtype = gwj->mod.modtype.set;
			if (nla_put(skb, CGW_FDMOD_SET, sizeof(mb), &mb) < 0)
				goto cancel;
		}
	} else {
		struct cgw_frame_mod mb;

		if (gwj->mod.modtype.and) {
			memcpy(&mb.cf, &gwj->mod.modframe.and, sizeof(mb.cf));
			mb.modtype = gwj->mod.modtype.and;
			if (nla_put(skb, CGW_MOD_AND, sizeof(mb), &mb) < 0)
				goto cancel;
		}

		if (gwj->mod.modtype.or) {
			memcpy(&mb.cf, &gwj->mod.modframe.or, sizeof(mb.cf));
			mb.modtype = gwj->mod.modtype.or;
			if (nla_put(skb, CGW_MOD_OR, sizeof(mb), &mb) < 0)
				goto cancel;
		}

		if (gwj->mod.modtype.xor) {
			memcpy(&mb.cf, &gwj->mod.modframe.xor, sizeof(mb.cf));
			mb.modtype = gwj->mod.modtype.xor;
			if (nla_put(skb, CGW_MOD_XOR, sizeof(mb), &mb) < 0)
				goto cancel;
		}

		if (gwj->mod.modtype.set) {
			memcpy(&mb.cf, &gwj->mod.modframe.set, sizeof(mb.cf));
			mb.modtype = gwj->mod.modtype.set;
			if (nla_put(skb, CGW_MOD_SET, sizeof(mb), &mb) < 0)
				goto cancel;
		}
	}

	if (gwj->mod.uid) {
		if (nla_put_u32(skb, CGW_MOD_UID, gwj->mod.uid) < 0)
			goto cancel;
	}

	if (gwj->mod.csumfunc.crc8) {
		if (nla_put(skb, CGW_CS_CRC8, CGW_CS_CRC8_LEN,
			    &gwj->mod.csum.crc8) < 0)
			goto cancel;
	}

	if (gwj->mod.csumfunc.xor) {
		if (nla_put(skb, CGW_CS_XOR, CGW_CS_XOR_LEN,
			    &gwj->mod.csum.xor) < 0)
			goto cancel;
	}

	if (gwj->gwtype == CGW_TYPE_CAN_CAN) {
		if (gwj->ccgw.filter.can_id || gwj->ccgw.filter.can_mask) {
			if (nla_put(skb, CGW_FILTER, sizeof(struct can_filter),
				    &gwj->ccgw.filter) < 0)
				goto cancel;
		}

		if (nla_put_u32(skb, CGW_SRC_IF, gwj->ccgw.src_idx) < 0)
			goto cancel;

		if (nla_put_u32(skb, CGW_DST_IF, gwj->ccgw.dst_idx) < 0)
			goto cancel;
	}

	nlmsg_end(skb, nlh);
	return 0;

cancel:
	nlmsg_cancel(skb, nlh);
	return -EMSGSIZE;
}

/* Dump information about all CAN gateway jobs, in response to RTM_GETROUTE */
static int cgw_dump_jobs(struct sk_buff *skb, struct netlink_callback *cb)
{
	struct net *net = sock_net(skb->sk);
	struct cgw_job *gwj = NULL;
	int idx = 0;
	int s_idx = cb->args[0];

	rcu_read_lock();
	hlist_for_each_entry_rcu(gwj, &net->can.cgw_list, list) {
		if (idx < s_idx)
			goto cont;

		if (cgw_put_job(skb, gwj, RTM_NEWROUTE,
				NETLINK_CB(cb->skb).portid,
				cb->nlh->nlmsg_seq, NLM_F_MULTI) < 0)
			break;
cont:
		idx++;
	}
	rcu_read_unlock();

	cb->args[0] = idx;

	return skb->len;
}

static const struct nla_policy cgw_policy[CGW_MAX + 1] = {
	[CGW_MOD_AND]	= { .len = sizeof(struct cgw_frame_mod) },
	[CGW_MOD_OR]	= { .len = sizeof(struct cgw_frame_mod) },
	[CGW_MOD_XOR]	= { .len = sizeof(struct cgw_frame_mod) },
	[CGW_MOD_SET]	= { .len = sizeof(struct cgw_frame_mod) },
	[CGW_CS_XOR]	= { .len = sizeof(struct cgw_csum_xor) },
	[CGW_CS_CRC8]	= { .len = sizeof(struct cgw_csum_crc8) },
	[CGW_SRC_IF]	= { .type = NLA_U32 },
	[CGW_DST_IF]	= { .type = NLA_U32 },
	[CGW_FILTER]	= { .len = sizeof(struct can_filter) },
	[CGW_LIM_HOPS]	= { .type = NLA_U8 },
	[CGW_MOD_UID]	= { .type = NLA_U32 },
	[CGW_FDMOD_AND]	= { .len = sizeof(struct cgw_fdframe_mod) },
	[CGW_FDMOD_OR]	= { .len = sizeof(struct cgw_fdframe_mod) },
	[CGW_FDMOD_XOR]	= { .len = sizeof(struct cgw_fdframe_mod) },
	[CGW_FDMOD_SET]	= { .len = sizeof(struct cgw_fdframe_mod) },
};

/* check for common and gwtype specific attributes */
static int cgw_parse_attr(struct nlmsghdr *nlh, struct cf_mod *mod,
			  u8 gwtype, void *gwtypeattr, u8 *limhops)
{
	struct nlattr *tb[CGW_MAX + 1];
	struct rtcanmsg *r = nlmsg_data(nlh);
	int modidx = 0;
	int err = 0;

	/* initialize modification & checksum data space */
	memset(mod, 0, sizeof(*mod));

	err = nlmsg_parse_deprecated(nlh, sizeof(struct rtcanmsg), tb,
				     CGW_MAX, cgw_policy, NULL);
	if (err < 0)
		return err;

	if (tb[CGW_LIM_HOPS]) {
		*limhops = nla_get_u8(tb[CGW_LIM_HOPS]);

		if (*limhops < 1 || *limhops > max_hops)
			return -EINVAL;
	}

	/* check for AND/OR/XOR/SET modifications */
	if (r->flags & CGW_FLAGS_CAN_FD) {
		struct cgw_fdframe_mod mb;

		if (tb[CGW_FDMOD_AND]) {
			nla_memcpy(&mb, tb[CGW_FDMOD_AND], CGW_FDMODATTR_LEN);

			canfdframecpy(&mod->modframe.and, &mb.cf);
			mod->modtype.and = mb.modtype;

			if (mb.modtype & CGW_MOD_ID)
				mod->modfunc[modidx++] = mod_and_id;

			if (mb.modtype & CGW_MOD_LEN)
				mod->modfunc[modidx++] = mod_and_len;

			if (mb.modtype & CGW_MOD_FLAGS)
				mod->modfunc[modidx++] = mod_and_flags;

			if (mb.modtype & CGW_MOD_DATA)
				mod->modfunc[modidx++] = mod_and_fddata;
		}

		if (tb[CGW_FDMOD_OR]) {
			nla_memcpy(&mb, tb[CGW_FDMOD_OR], CGW_FDMODATTR_LEN);

			canfdframecpy(&mod->modframe.or, &mb.cf);
			mod->modtype.or = mb.modtype;

			if (mb.modtype & CGW_MOD_ID)
				mod->modfunc[modidx++] = mod_or_id;

			if (mb.modtype & CGW_MOD_LEN)
				mod->modfunc[modidx++] = mod_or_len;

			if (mb.modtype & CGW_MOD_FLAGS)
				mod->modfunc[modidx++] = mod_or_flags;

			if (mb.modtype & CGW_MOD_DATA)
				mod->modfunc[modidx++] = mod_or_fddata;
		}

		if (tb[CGW_FDMOD_XOR]) {
			nla_memcpy(&mb, tb[CGW_FDMOD_XOR], CGW_FDMODATTR_LEN);

			canfdframecpy(&mod->modframe.xor, &mb.cf);
			mod->modtype.xor = mb.modtype;

			if (mb.modtype & CGW_MOD_ID)
				mod->modfunc[modidx++] = mod_xor_id;

			if (mb.modtype & CGW_MOD_LEN)
				mod->modfunc[modidx++] = mod_xor_len;

			if (mb.modtype & CGW_MOD_FLAGS)
				mod->modfunc[modidx++] = mod_xor_flags;

			if (mb.modtype & CGW_MOD_DATA)
				mod->modfunc[modidx++] = mod_xor_fddata;
		}

		if (tb[CGW_FDMOD_SET]) {
			nla_memcpy(&mb, tb[CGW_FDMOD_SET], CGW_FDMODATTR_LEN);

			canfdframecpy(&mod->modframe.set, &mb.cf);
			mod->modtype.set = mb.modtype;

			if (mb.modtype & CGW_MOD_ID)
				mod->modfunc[modidx++] = mod_set_id;

			if (mb.modtype & CGW_MOD_LEN)
				mod->modfunc[modidx++] = mod_set_len;

			if (mb.modtype & CGW_MOD_FLAGS)
				mod->modfunc[modidx++] = mod_set_flags;

			if (mb.modtype & CGW_MOD_DATA)
				mod->modfunc[modidx++] = mod_set_fddata;
		}
	} else {
		struct cgw_frame_mod mb;

		if (tb[CGW_MOD_AND]) {
			nla_memcpy(&mb, tb[CGW_MOD_AND], CGW_MODATTR_LEN);

			canframecpy(&mod->modframe.and, &mb.cf);
			mod->modtype.and = mb.modtype;

			if (mb.modtype & CGW_MOD_ID)
				mod->modfunc[modidx++] = mod_and_id;

			if (mb.modtype & CGW_MOD_LEN)
				mod->modfunc[modidx++] = mod_and_len;

			if (mb.modtype & CGW_MOD_DATA)
				mod->modfunc[modidx++] = mod_and_data;
		}

		if (tb[CGW_MOD_OR]) {
			nla_memcpy(&mb, tb[CGW_MOD_OR], CGW_MODATTR_LEN);

			canframecpy(&mod->modframe.or, &mb.cf);
			mod->modtype.or = mb.modtype;

			if (mb.modtype & CGW_MOD_ID)
				mod->modfunc[modidx++] = mod_or_id;

			if (mb.modtype & CGW_MOD_LEN)
				mod->modfunc[modidx++] = mod_or_len;

			if (mb.modtype & CGW_MOD_DATA)
				mod->modfunc[modidx++] = mod_or_data;
		}

		if (tb[CGW_MOD_XOR]) {
			nla_memcpy(&mb, tb[CGW_MOD_XOR], CGW_MODATTR_LEN);

			canframecpy(&mod->modframe.xor, &mb.cf);
			mod->modtype.xor = mb.modtype;

			if (mb.modtype & CGW_MOD_ID)
				mod->modfunc[modidx++] = mod_xor_id;

			if (mb.modtype & CGW_MOD_LEN)
				mod->modfunc[modidx++] = mod_xor_len;

			if (mb.modtype & CGW_MOD_DATA)
				mod->modfunc[modidx++] = mod_xor_data;
		}

		if (tb[CGW_MOD_SET]) {
			nla_memcpy(&mb, tb[CGW_MOD_SET], CGW_MODATTR_LEN);

			canframecpy(&mod->modframe.set, &mb.cf);
			mod->modtype.set = mb.modtype;

			if (mb.modtype & CGW_MOD_ID)
				mod->modfunc[modidx++] = mod_set_id;

			if (mb.modtype & CGW_MOD_LEN)
				mod->modfunc[modidx++] = mod_set_len;

			if (mb.modtype & CGW_MOD_DATA)
				mod->modfunc[modidx++] = mod_set_data;
		}
	}

	/* check for checksum operations after CAN frame modifications */
	if (modidx) {
		if (tb[CGW_CS_CRC8]) {
			struct cgw_csum_crc8 *c = nla_data(tb[CGW_CS_CRC8]);

			err = cgw_chk_csum_parms(c->from_idx, c->to_idx,
						 c->result_idx, r);
			if (err)
				return err;

			nla_memcpy(&mod->csum.crc8, tb[CGW_CS_CRC8],
				   CGW_CS_CRC8_LEN);

			/* select dedicated processing function to reduce
			 * runtime operations in receive hot path.
			 */
			if (c->from_idx < 0 || c->to_idx < 0 ||
			    c->result_idx < 0)
				mod->csumfunc.crc8 = cgw_csum_crc8_rel;
			else if (c->from_idx <= c->to_idx)
				mod->csumfunc.crc8 = cgw_csum_crc8_pos;
			else
				mod->csumfunc.crc8 = cgw_csum_crc8_neg;
		}

		if (tb[CGW_CS_XOR]) {
			struct cgw_csum_xor *c = nla_data(tb[CGW_CS_XOR]);

			err = cgw_chk_csum_parms(c->from_idx, c->to_idx,
						 c->result_idx, r);
			if (err)
				return err;

			nla_memcpy(&mod->csum.xor, tb[CGW_CS_XOR],
				   CGW_CS_XOR_LEN);

			/* select dedicated processing function to reduce
			 * runtime operations in receive hot path.
			 */
			if (c->from_idx < 0 || c->to_idx < 0 ||
			    c->result_idx < 0)
				mod->csumfunc.xor = cgw_csum_xor_rel;
			else if (c->from_idx <= c->to_idx)
				mod->csumfunc.xor = cgw_csum_xor_pos;
			else
				mod->csumfunc.xor = cgw_csum_xor_neg;
		}

		if (tb[CGW_MOD_UID])
			nla_memcpy(&mod->uid, tb[CGW_MOD_UID], sizeof(u32));
	}

	if (gwtype == CGW_TYPE_CAN_CAN) {
		/* check CGW_TYPE_CAN_CAN specific attributes */
		struct can_can_gw *ccgw = (struct can_can_gw *)gwtypeattr;

		memset(ccgw, 0, sizeof(*ccgw));

		/* check for can_filter in attributes */
		if (tb[CGW_FILTER])
			nla_memcpy(&ccgw->filter, tb[CGW_FILTER],
				   sizeof(struct can_filter));

		err = -ENODEV;

		/* specifying two interfaces is mandatory */
		if (!tb[CGW_SRC_IF] || !tb[CGW_DST_IF])
			return err;

		ccgw->src_idx = nla_get_u32(tb[CGW_SRC_IF]);
		ccgw->dst_idx = nla_get_u32(tb[CGW_DST_IF]);

		/* both indices set to 0 for flushing all routing entries */
		if (!ccgw->src_idx && !ccgw->dst_idx)
			return 0;

		/* only one index set to 0 is an error */
		if (!ccgw->src_idx || !ccgw->dst_idx)
			return err;
	}

	/* add the checks for other gwtypes here */

	return 0;
}

static int cgw_create_job(struct sk_buff *skb,  struct nlmsghdr *nlh,
			  struct netlink_ext_ack *extack)
{
	struct net *net = sock_net(skb->sk);
	struct rtcanmsg *r;
	struct cgw_job *gwj;
	struct cf_mod mod;
	struct can_can_gw ccgw;
	u8 limhops = 0;
	int err = 0;

	if (!netlink_capable(skb, CAP_NET_ADMIN))
		return -EPERM;

	if (nlmsg_len(nlh) < sizeof(*r))
		return -EINVAL;

	r = nlmsg_data(nlh);
	if (r->can_family != AF_CAN)
		return -EPFNOSUPPORT;

	/* so far we only support CAN -> CAN routings */
	if (r->gwtype != CGW_TYPE_CAN_CAN)
		return -EINVAL;

	err = cgw_parse_attr(nlh, &mod, CGW_TYPE_CAN_CAN, &ccgw, &limhops);
	if (err < 0)
		return err;

	if (mod.uid) {
		ASSERT_RTNL();

		/* check for updating an existing job with identical uid */
		hlist_for_each_entry(gwj, &net->can.cgw_list, list) {
			if (gwj->mod.uid != mod.uid)
				continue;

			/* interfaces & filters must be identical */
			if (memcmp(&gwj->ccgw, &ccgw, sizeof(ccgw)))
				return -EINVAL;

			/* update modifications with disabled softirq & quit */
			local_bh_disable();
			memcpy(&gwj->mod, &mod, sizeof(mod));
			local_bh_enable();
			return 0;
		}
	}

	/* ifindex == 0 is not allowed for job creation */
	if (!ccgw.src_idx || !ccgw.dst_idx)
		return -ENODEV;

	gwj = kmem_cache_alloc(cgw_cache, GFP_KERNEL);
	if (!gwj)
		return -ENOMEM;

	gwj->handled_frames = 0;
	gwj->dropped_frames = 0;
	gwj->deleted_frames = 0;
	gwj->flags = r->flags;
	gwj->gwtype = r->gwtype;
	gwj->limit_hops = limhops;

	/* insert already parsed information */
	memcpy(&gwj->mod, &mod, sizeof(mod));
	memcpy(&gwj->ccgw, &ccgw, sizeof(ccgw));

	err = -ENODEV;

	gwj->src.dev = __dev_get_by_index(net, gwj->ccgw.src_idx);

	if (!gwj->src.dev)
		goto out;

	if (gwj->src.dev->type != ARPHRD_CAN)
		goto out;

	gwj->dst.dev = __dev_get_by_index(net, gwj->ccgw.dst_idx);

	if (!gwj->dst.dev)
		goto out;

	if (gwj->dst.dev->type != ARPHRD_CAN)
		goto out;

	ASSERT_RTNL();

	err = cgw_register_filter(net, gwj);
	if (!err)
		hlist_add_head_rcu(&gwj->list, &net->can.cgw_list);
out:
	if (err)
		kmem_cache_free(cgw_cache, gwj);

	return err;
}

static void cgw_remove_all_jobs(struct net *net)
{
	struct cgw_job *gwj = NULL;
	struct hlist_node *nx;

	ASSERT_RTNL();

	hlist_for_each_entry_safe(gwj, nx, &net->can.cgw_list, list) {
		hlist_del(&gwj->list);
		cgw_unregister_filter(net, gwj);
		synchronize_rcu();
		kmem_cache_free(cgw_cache, gwj);
	}
}

static int cgw_remove_job(struct sk_buff *skb, struct nlmsghdr *nlh,
			  struct netlink_ext_ack *extack)
{
	struct net *net = sock_net(skb->sk);
	struct cgw_job *gwj = NULL;
	struct hlist_node *nx;
	struct rtcanmsg *r;
	struct cf_mod mod;
	struct can_can_gw ccgw;
	u8 limhops = 0;
	int err = 0;

	if (!netlink_capable(skb, CAP_NET_ADMIN))
		return -EPERM;

	if (nlmsg_len(nlh) < sizeof(*r))
		return -EINVAL;

	r = nlmsg_data(nlh);
	if (r->can_family != AF_CAN)
		return -EPFNOSUPPORT;

	/* so far we only support CAN -> CAN routings */
	if (r->gwtype != CGW_TYPE_CAN_CAN)
		return -EINVAL;

	err = cgw_parse_attr(nlh, &mod, CGW_TYPE_CAN_CAN, &ccgw, &limhops);
	if (err < 0)
		return err;

	/* two interface indices both set to 0 => remove all entries */
	if (!ccgw.src_idx && !ccgw.dst_idx) {
		cgw_remove_all_jobs(net);
		return 0;
	}

	err = -EINVAL;

	ASSERT_RTNL();

	/* remove only the first matching entry */
	hlist_for_each_entry_safe(gwj, nx, &net->can.cgw_list, list) {
		if (gwj->flags != r->flags)
			continue;

		if (gwj->limit_hops != limhops)
			continue;

		/* we have a match when uid is enabled and identical */
		if (gwj->mod.uid || mod.uid) {
			if (gwj->mod.uid != mod.uid)
				continue;
		} else {
			/* no uid => check for identical modifications */
			if (memcmp(&gwj->mod, &mod, sizeof(mod)))
				continue;
		}

		/* if (r->gwtype == CGW_TYPE_CAN_CAN) - is made sure here */
		if (memcmp(&gwj->ccgw, &ccgw, sizeof(ccgw)))
			continue;

		hlist_del(&gwj->list);
		cgw_unregister_filter(net, gwj);
		synchronize_rcu();
		kmem_cache_free(cgw_cache, gwj);
		err = 0;
		break;
	}

	return err;
}

static int __net_init cangw_pernet_init(struct net *net)
{
	INIT_HLIST_HEAD(&net->can.cgw_list);
	return 0;
}

static void __net_exit cangw_pernet_exit(struct net *net)
{
	rtnl_lock();
	cgw_remove_all_jobs(net);
	rtnl_unlock();
}

static struct pernet_operations cangw_pernet_ops = {
	.init = cangw_pernet_init,
	.exit = cangw_pernet_exit,
};

static __init int cgw_module_init(void)
{
	int ret;

	/* sanitize given module parameter */
	max_hops = clamp_t(unsigned int, max_hops, CGW_MIN_HOPS, CGW_MAX_HOPS);

	pr_info("can: netlink gateway - max_hops=%d\n",	max_hops);

	ret = register_pernet_subsys(&cangw_pernet_ops);
	if (ret)
		return ret;

	ret = -ENOMEM;
	cgw_cache = kmem_cache_create("can_gw", sizeof(struct cgw_job),
				      0, 0, NULL);
	if (!cgw_cache)
		goto out_cache_create;

	/* set notifier */
	notifier.notifier_call = cgw_notifier;
	ret = register_netdevice_notifier(&notifier);
	if (ret)
		goto out_register_notifier;

	ret = rtnl_register_module(THIS_MODULE, PF_CAN, RTM_GETROUTE,
				   NULL, cgw_dump_jobs, 0);
	if (ret)
		goto out_rtnl_register1;

	ret = rtnl_register_module(THIS_MODULE, PF_CAN, RTM_NEWROUTE,
				   cgw_create_job, NULL, 0);
	if (ret)
		goto out_rtnl_register2;
	ret = rtnl_register_module(THIS_MODULE, PF_CAN, RTM_DELROUTE,
				   cgw_remove_job, NULL, 0);
	if (ret)
		goto out_rtnl_register3;

	return 0;

out_rtnl_register3:
	rtnl_unregister(PF_CAN, RTM_NEWROUTE);
out_rtnl_register2:
	rtnl_unregister(PF_CAN, RTM_GETROUTE);
out_rtnl_register1:
	unregister_netdevice_notifier(&notifier);
out_register_notifier:
	kmem_cache_destroy(cgw_cache);
out_cache_create:
	unregister_pernet_subsys(&cangw_pernet_ops);

	return ret;
}

static __exit void cgw_module_exit(void)
{
	rtnl_unregister_all(PF_CAN);

	unregister_netdevice_notifier(&notifier);

	unregister_pernet_subsys(&cangw_pernet_ops);
	rcu_barrier(); /* Wait for completion of call_rcu()'s */

	kmem_cache_destroy(cgw_cache);
}

module_init(cgw_module_init);
module_exit(cgw_module_exit);
