/*
 * gw.c - CAN frame Gateway/Router/Bridge with netlink interface
 *
 * Copyright (c) 2011 Volkswagen Group Electronic Research
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
#include <linux/can/gw.h>
#include <net/rtnetlink.h>
#include <net/net_namespace.h>
#include <net/sock.h>

#define CAN_GW_VERSION "20101209"
static __initdata const char banner[] =
	KERN_INFO "can: netlink gateway (rev " CAN_GW_VERSION ")\n";

MODULE_DESCRIPTION("PF_CAN netlink gateway");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Oliver Hartkopp <oliver.hartkopp@volkswagen.de>");
MODULE_ALIAS("can-gw");

static HLIST_HEAD(cgw_list);
static struct notifier_block notifier;

static struct kmem_cache *cgw_cache __read_mostly;

/* structure that contains the (on-the-fly) CAN frame modifications */
struct cf_mod {
	struct {
		struct can_frame and;
		struct can_frame or;
		struct can_frame xor;
		struct can_frame set;
	} modframe;
	struct {
		u8 and;
		u8 or;
		u8 xor;
		u8 set;
	} modtype;
	void (*modfunc[MAX_MODFUNCTIONS])(struct can_frame *cf,
					  struct cf_mod *mod);

	/* CAN frame checksum calculation after CAN frame modifications */
	struct {
		struct cgw_csum_xor xor;
		struct cgw_csum_crc8 crc8;
	} csum;
	struct {
		void (*xor)(struct can_frame *cf, struct cgw_csum_xor *xor);
		void (*crc8)(struct can_frame *cf, struct cgw_csum_crc8 *crc8);
	} csumfunc;
};


/*
 * So far we just support CAN -> CAN routing and frame modifications.
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
	u16 flags;
};

/* modification functions that are invoked in the hot path in can_can_gw_rcv */

#define MODFUNC(func, op) static void func(struct can_frame *cf, \
					   struct cf_mod *mod) { op ; }

MODFUNC(mod_and_id, cf->can_id &= mod->modframe.and.can_id)
MODFUNC(mod_and_dlc, cf->can_dlc &= mod->modframe.and.can_dlc)
MODFUNC(mod_and_data, *(u64 *)cf->data &= *(u64 *)mod->modframe.and.data)
MODFUNC(mod_or_id, cf->can_id |= mod->modframe.or.can_id)
MODFUNC(mod_or_dlc, cf->can_dlc |= mod->modframe.or.can_dlc)
MODFUNC(mod_or_data, *(u64 *)cf->data |= *(u64 *)mod->modframe.or.data)
MODFUNC(mod_xor_id, cf->can_id ^= mod->modframe.xor.can_id)
MODFUNC(mod_xor_dlc, cf->can_dlc ^= mod->modframe.xor.can_dlc)
MODFUNC(mod_xor_data, *(u64 *)cf->data ^= *(u64 *)mod->modframe.xor.data)
MODFUNC(mod_set_id, cf->can_id = mod->modframe.set.can_id)
MODFUNC(mod_set_dlc, cf->can_dlc = mod->modframe.set.can_dlc)
MODFUNC(mod_set_data, *(u64 *)cf->data = *(u64 *)mod->modframe.set.data)

static inline void canframecpy(struct can_frame *dst, struct can_frame *src)
{
	/*
	 * Copy the struct members separately to ensure that no uninitialized
	 * data are copied in the 3 bytes hole of the struct. This is needed
	 * to make easy compares of the data in the struct cf_mod.
	 */

	dst->can_id = src->can_id;
	dst->can_dlc = src->can_dlc;
	*(u64 *)dst->data = *(u64 *)src->data;
}

static int cgw_chk_csum_parms(s8 fr, s8 to, s8 re)
{
	/*
	 * absolute dlc values 0 .. 7 => 0 .. 7, e.g. data [0]
	 * relative to received dlc -1 .. -8 :
	 * e.g. for received dlc = 8
	 * -1 => index = 7 (data[7])
	 * -3 => index = 5 (data[5])
	 * -8 => index = 0 (data[0])
	 */

	if (fr > -9 && fr < 8 &&
	    to > -9 && to < 8 &&
	    re > -9 && re < 8)
		return 0;
	else
		return -EINVAL;
}

static inline int calc_idx(int idx, int rx_dlc)
{
	if (idx < 0)
		return rx_dlc + idx;
	else
		return idx;
}

static void cgw_csum_xor_rel(struct can_frame *cf, struct cgw_csum_xor *xor)
{
	int from = calc_idx(xor->from_idx, cf->can_dlc);
	int to = calc_idx(xor->to_idx, cf->can_dlc);
	int res = calc_idx(xor->result_idx, cf->can_dlc);
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

static void cgw_csum_xor_pos(struct can_frame *cf, struct cgw_csum_xor *xor)
{
	u8 val = xor->init_xor_val;
	int i;

	for (i = xor->from_idx; i <= xor->to_idx; i++)
		val ^= cf->data[i];

	cf->data[xor->result_idx] = val;
}

static void cgw_csum_xor_neg(struct can_frame *cf, struct cgw_csum_xor *xor)
{
	u8 val = xor->init_xor_val;
	int i;

	for (i = xor->from_idx; i >= xor->to_idx; i--)
		val ^= cf->data[i];

	cf->data[xor->result_idx] = val;
}

static void cgw_csum_crc8_rel(struct can_frame *cf, struct cgw_csum_crc8 *crc8)
{
	int from = calc_idx(crc8->from_idx, cf->can_dlc);
	int to = calc_idx(crc8->to_idx, cf->can_dlc);
	int res = calc_idx(crc8->result_idx, cf->can_dlc);
	u8 crc = crc8->init_crc_val;
	int i;

	if (from < 0 || to < 0 || res < 0)
		return;

	if (from <= to) {
		for (i = crc8->from_idx; i <= crc8->to_idx; i++)
			crc = crc8->crctab[crc^cf->data[i]];
	} else {
		for (i = crc8->from_idx; i >= crc8->to_idx; i--)
			crc = crc8->crctab[crc^cf->data[i]];
	}

	switch (crc8->profile) {

	case CGW_CRC8PRF_1U8:
		crc = crc8->crctab[crc^crc8->profile_data[0]];
		break;

	case  CGW_CRC8PRF_16U8:
		crc = crc8->crctab[crc^crc8->profile_data[cf->data[1] & 0xF]];
		break;

	case CGW_CRC8PRF_SFFID_XOR:
		crc = crc8->crctab[crc^(cf->can_id & 0xFF)^
				   (cf->can_id >> 8 & 0xFF)];
		break;

	}

	cf->data[crc8->result_idx] = crc^crc8->final_xor_val;
}

static void cgw_csum_crc8_pos(struct can_frame *cf, struct cgw_csum_crc8 *crc8)
{
	u8 crc = crc8->init_crc_val;
	int i;

	for (i = crc8->from_idx; i <= crc8->to_idx; i++)
		crc = crc8->crctab[crc^cf->data[i]];

	switch (crc8->profile) {

	case CGW_CRC8PRF_1U8:
		crc = crc8->crctab[crc^crc8->profile_data[0]];
		break;

	case  CGW_CRC8PRF_16U8:
		crc = crc8->crctab[crc^crc8->profile_data[cf->data[1] & 0xF]];
		break;

	case CGW_CRC8PRF_SFFID_XOR:
		crc = crc8->crctab[crc^(cf->can_id & 0xFF)^
				   (cf->can_id >> 8 & 0xFF)];
		break;
	}

	cf->data[crc8->result_idx] = crc^crc8->final_xor_val;
}

static void cgw_csum_crc8_neg(struct can_frame *cf, struct cgw_csum_crc8 *crc8)
{
	u8 crc = crc8->init_crc_val;
	int i;

	for (i = crc8->from_idx; i >= crc8->to_idx; i--)
		crc = crc8->crctab[crc^cf->data[i]];

	switch (crc8->profile) {

	case CGW_CRC8PRF_1U8:
		crc = crc8->crctab[crc^crc8->profile_data[0]];
		break;

	case  CGW_CRC8PRF_16U8:
		crc = crc8->crctab[crc^crc8->profile_data[cf->data[1] & 0xF]];
		break;

	case CGW_CRC8PRF_SFFID_XOR:
		crc = crc8->crctab[crc^(cf->can_id & 0xFF)^
				   (cf->can_id >> 8 & 0xFF)];
		break;
	}

	cf->data[crc8->result_idx] = crc^crc8->final_xor_val;
}

/* the receive & process & send function */
static void can_can_gw_rcv(struct sk_buff *skb, void *data)
{
	struct cgw_job *gwj = (struct cgw_job *)data;
	struct can_frame *cf;
	struct sk_buff *nskb;
	int modidx = 0;

	/* do not handle already routed frames - see comment below */
	if (skb_mac_header_was_set(skb))
		return;

	if (!(gwj->dst.dev->flags & IFF_UP)) {
		gwj->dropped_frames++;
		return;
	}

	/*
	 * clone the given skb, which has not been done in can_rcv()
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

	/*
	 * Mark routed frames by setting some mac header length which is
	 * not relevant for the CAN frames located in the skb->data section.
	 *
	 * As dev->header_ops is not set in CAN netdevices no one is ever
	 * accessing the various header offsets in the CAN skbuffs anyway.
	 * E.g. using the packet socket to read CAN frames is still working.
	 */
	skb_set_mac_header(nskb, 8);
	nskb->dev = gwj->dst.dev;

	/* pointer to modifiable CAN frame */
	cf = (struct can_frame *)nskb->data;

	/* perform preprocessed modification functions if there are any */
	while (modidx < MAX_MODFUNCTIONS && gwj->mod.modfunc[modidx])
		(*gwj->mod.modfunc[modidx++])(cf, &gwj->mod);

	/* check for checksum updates when the CAN frame has been modified */
	if (modidx) {
		if (gwj->mod.csumfunc.crc8)
			(*gwj->mod.csumfunc.crc8)(cf, &gwj->mod.csum.crc8);

		if (gwj->mod.csumfunc.xor)
			(*gwj->mod.csumfunc.xor)(cf, &gwj->mod.csum.xor);
	}

	/* clear the skb timestamp if not configured the other way */
	if (!(gwj->flags & CGW_FLAGS_CAN_SRC_TSTAMP))
		nskb->tstamp.tv64 = 0;

	/* send to netdevice */
	if (can_send(nskb, gwj->flags & CGW_FLAGS_CAN_ECHO))
		gwj->dropped_frames++;
	else
		gwj->handled_frames++;
}

static inline int cgw_register_filter(struct cgw_job *gwj)
{
	return can_rx_register(gwj->src.dev, gwj->ccgw.filter.can_id,
			       gwj->ccgw.filter.can_mask, can_can_gw_rcv,
			       gwj, "gw");
}

static inline void cgw_unregister_filter(struct cgw_job *gwj)
{
	can_rx_unregister(gwj->src.dev, gwj->ccgw.filter.can_id,
			  gwj->ccgw.filter.can_mask, can_can_gw_rcv, gwj);
}

static int cgw_notifier(struct notifier_block *nb,
			unsigned long msg, void *data)
{
	struct net_device *dev = (struct net_device *)data;

	if (!net_eq(dev_net(dev), &init_net))
		return NOTIFY_DONE;
	if (dev->type != ARPHRD_CAN)
		return NOTIFY_DONE;

	if (msg == NETDEV_UNREGISTER) {

		struct cgw_job *gwj = NULL;
		struct hlist_node *n, *nx;

		ASSERT_RTNL();

		hlist_for_each_entry_safe(gwj, n, nx, &cgw_list, list) {

			if (gwj->src.dev == dev || gwj->dst.dev == dev) {
				hlist_del(&gwj->list);
				cgw_unregister_filter(gwj);
				kfree(gwj);
			}
		}
	}

	return NOTIFY_DONE;
}

static int cgw_put_job(struct sk_buff *skb, struct cgw_job *gwj, int type,
		       u32 pid, u32 seq, int flags)
{
	struct cgw_frame_mod mb;
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

	/* check non default settings of attributes */

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

	return nlmsg_end(skb, nlh);

cancel:
	nlmsg_cancel(skb, nlh);
	return -EMSGSIZE;
}

/* Dump information about all CAN gateway jobs, in response to RTM_GETROUTE */
static int cgw_dump_jobs(struct sk_buff *skb, struct netlink_callback *cb)
{
	struct cgw_job *gwj = NULL;
	struct hlist_node *n;
	int idx = 0;
	int s_idx = cb->args[0];

	rcu_read_lock();
	hlist_for_each_entry_rcu(gwj, n, &cgw_list, list) {
		if (idx < s_idx)
			goto cont;

		if (cgw_put_job(skb, gwj, RTM_NEWROUTE, NETLINK_CB(cb->skb).portid,
		    cb->nlh->nlmsg_seq, NLM_F_MULTI) < 0)
			break;
cont:
		idx++;
	}
	rcu_read_unlock();

	cb->args[0] = idx;

	return skb->len;
}

static const struct nla_policy cgw_policy[CGW_MAX+1] = {
	[CGW_MOD_AND]	= { .len = sizeof(struct cgw_frame_mod) },
	[CGW_MOD_OR]	= { .len = sizeof(struct cgw_frame_mod) },
	[CGW_MOD_XOR]	= { .len = sizeof(struct cgw_frame_mod) },
	[CGW_MOD_SET]	= { .len = sizeof(struct cgw_frame_mod) },
	[CGW_CS_XOR]	= { .len = sizeof(struct cgw_csum_xor) },
	[CGW_CS_CRC8]	= { .len = sizeof(struct cgw_csum_crc8) },
	[CGW_SRC_IF]	= { .type = NLA_U32 },
	[CGW_DST_IF]	= { .type = NLA_U32 },
	[CGW_FILTER]	= { .len = sizeof(struct can_filter) },
};

/* check for common and gwtype specific attributes */
static int cgw_parse_attr(struct nlmsghdr *nlh, struct cf_mod *mod,
			  u8 gwtype, void *gwtypeattr)
{
	struct nlattr *tb[CGW_MAX+1];
	struct cgw_frame_mod mb;
	int modidx = 0;
	int err = 0;

	/* initialize modification & checksum data space */
	memset(mod, 0, sizeof(*mod));

	err = nlmsg_parse(nlh, sizeof(struct rtcanmsg), tb, CGW_MAX,
			  cgw_policy);
	if (err < 0)
		return err;

	/* check for AND/OR/XOR/SET modifications */

	if (tb[CGW_MOD_AND]) {
		nla_memcpy(&mb, tb[CGW_MOD_AND], CGW_MODATTR_LEN);

		canframecpy(&mod->modframe.and, &mb.cf);
		mod->modtype.and = mb.modtype;

		if (mb.modtype & CGW_MOD_ID)
			mod->modfunc[modidx++] = mod_and_id;

		if (mb.modtype & CGW_MOD_DLC)
			mod->modfunc[modidx++] = mod_and_dlc;

		if (mb.modtype & CGW_MOD_DATA)
			mod->modfunc[modidx++] = mod_and_data;
	}

	if (tb[CGW_MOD_OR]) {
		nla_memcpy(&mb, tb[CGW_MOD_OR], CGW_MODATTR_LEN);

		canframecpy(&mod->modframe.or, &mb.cf);
		mod->modtype.or = mb.modtype;

		if (mb.modtype & CGW_MOD_ID)
			mod->modfunc[modidx++] = mod_or_id;

		if (mb.modtype & CGW_MOD_DLC)
			mod->modfunc[modidx++] = mod_or_dlc;

		if (mb.modtype & CGW_MOD_DATA)
			mod->modfunc[modidx++] = mod_or_data;
	}

	if (tb[CGW_MOD_XOR]) {
		nla_memcpy(&mb, tb[CGW_MOD_XOR], CGW_MODATTR_LEN);

		canframecpy(&mod->modframe.xor, &mb.cf);
		mod->modtype.xor = mb.modtype;

		if (mb.modtype & CGW_MOD_ID)
			mod->modfunc[modidx++] = mod_xor_id;

		if (mb.modtype & CGW_MOD_DLC)
			mod->modfunc[modidx++] = mod_xor_dlc;

		if (mb.modtype & CGW_MOD_DATA)
			mod->modfunc[modidx++] = mod_xor_data;
	}

	if (tb[CGW_MOD_SET]) {
		nla_memcpy(&mb, tb[CGW_MOD_SET], CGW_MODATTR_LEN);

		canframecpy(&mod->modframe.set, &mb.cf);
		mod->modtype.set = mb.modtype;

		if (mb.modtype & CGW_MOD_ID)
			mod->modfunc[modidx++] = mod_set_id;

		if (mb.modtype & CGW_MOD_DLC)
			mod->modfunc[modidx++] = mod_set_dlc;

		if (mb.modtype & CGW_MOD_DATA)
			mod->modfunc[modidx++] = mod_set_data;
	}

	/* check for checksum operations after CAN frame modifications */
	if (modidx) {

		if (tb[CGW_CS_CRC8]) {
			struct cgw_csum_crc8 *c = nla_data(tb[CGW_CS_CRC8]);

			err = cgw_chk_csum_parms(c->from_idx, c->to_idx,
						 c->result_idx);
			if (err)
				return err;

			nla_memcpy(&mod->csum.crc8, tb[CGW_CS_CRC8],
				   CGW_CS_CRC8_LEN);

			/*
			 * select dedicated processing function to reduce
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
						 c->result_idx);
			if (err)
				return err;

			nla_memcpy(&mod->csum.xor, tb[CGW_CS_XOR],
				   CGW_CS_XOR_LEN);

			/*
			 * select dedicated processing function to reduce
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
			  void *arg)
{
	struct rtcanmsg *r;
	struct cgw_job *gwj;
	int err = 0;

	if (nlmsg_len(nlh) < sizeof(*r))
		return -EINVAL;

	r = nlmsg_data(nlh);
	if (r->can_family != AF_CAN)
		return -EPFNOSUPPORT;

	/* so far we only support CAN -> CAN routings */
	if (r->gwtype != CGW_TYPE_CAN_CAN)
		return -EINVAL;

	gwj = kmem_cache_alloc(cgw_cache, GFP_KERNEL);
	if (!gwj)
		return -ENOMEM;

	gwj->handled_frames = 0;
	gwj->dropped_frames = 0;
	gwj->flags = r->flags;
	gwj->gwtype = r->gwtype;

	err = cgw_parse_attr(nlh, &gwj->mod, CGW_TYPE_CAN_CAN, &gwj->ccgw);
	if (err < 0)
		goto out;

	err = -ENODEV;

	/* ifindex == 0 is not allowed for job creation */
	if (!gwj->ccgw.src_idx || !gwj->ccgw.dst_idx)
		goto out;

	gwj->src.dev = dev_get_by_index(&init_net, gwj->ccgw.src_idx);

	if (!gwj->src.dev)
		goto out;

	/* check for CAN netdev not using header_ops - see gw_rcv() */
	if (gwj->src.dev->type != ARPHRD_CAN || gwj->src.dev->header_ops)
		goto put_src_out;

	gwj->dst.dev = dev_get_by_index(&init_net, gwj->ccgw.dst_idx);

	if (!gwj->dst.dev)
		goto put_src_out;

	/* check for CAN netdev not using header_ops - see gw_rcv() */
	if (gwj->dst.dev->type != ARPHRD_CAN || gwj->dst.dev->header_ops)
		goto put_src_dst_out;

	ASSERT_RTNL();

	err = cgw_register_filter(gwj);
	if (!err)
		hlist_add_head_rcu(&gwj->list, &cgw_list);

put_src_dst_out:
	dev_put(gwj->dst.dev);
put_src_out:
	dev_put(gwj->src.dev);
out:
	if (err)
		kmem_cache_free(cgw_cache, gwj);

	return err;
}

static void cgw_remove_all_jobs(void)
{
	struct cgw_job *gwj = NULL;
	struct hlist_node *n, *nx;

	ASSERT_RTNL();

	hlist_for_each_entry_safe(gwj, n, nx, &cgw_list, list) {
		hlist_del(&gwj->list);
		cgw_unregister_filter(gwj);
		kfree(gwj);
	}
}

static int cgw_remove_job(struct sk_buff *skb,  struct nlmsghdr *nlh, void *arg)
{
	struct cgw_job *gwj = NULL;
	struct hlist_node *n, *nx;
	struct rtcanmsg *r;
	struct cf_mod mod;
	struct can_can_gw ccgw;
	int err = 0;

	if (nlmsg_len(nlh) < sizeof(*r))
		return -EINVAL;

	r = nlmsg_data(nlh);
	if (r->can_family != AF_CAN)
		return -EPFNOSUPPORT;

	/* so far we only support CAN -> CAN routings */
	if (r->gwtype != CGW_TYPE_CAN_CAN)
		return -EINVAL;

	err = cgw_parse_attr(nlh, &mod, CGW_TYPE_CAN_CAN, &ccgw);
	if (err < 0)
		return err;

	/* two interface indices both set to 0 => remove all entries */
	if (!ccgw.src_idx && !ccgw.dst_idx) {
		cgw_remove_all_jobs();
		return 0;
	}

	err = -EINVAL;

	ASSERT_RTNL();

	/* remove only the first matching entry */
	hlist_for_each_entry_safe(gwj, n, nx, &cgw_list, list) {

		if (gwj->flags != r->flags)
			continue;

		if (memcmp(&gwj->mod, &mod, sizeof(mod)))
			continue;

		/* if (r->gwtype == CGW_TYPE_CAN_CAN) - is made sure here */
		if (memcmp(&gwj->ccgw, &ccgw, sizeof(ccgw)))
			continue;

		hlist_del(&gwj->list);
		cgw_unregister_filter(gwj);
		kfree(gwj);
		err = 0;
		break;
	}

	return err;
}

static __init int cgw_module_init(void)
{
	printk(banner);

	cgw_cache = kmem_cache_create("can_gw", sizeof(struct cgw_job),
				      0, 0, NULL);

	if (!cgw_cache)
		return -ENOMEM;

	/* set notifier */
	notifier.notifier_call = cgw_notifier;
	register_netdevice_notifier(&notifier);

	if (__rtnl_register(PF_CAN, RTM_GETROUTE, NULL, cgw_dump_jobs, NULL)) {
		unregister_netdevice_notifier(&notifier);
		kmem_cache_destroy(cgw_cache);
		return -ENOBUFS;
	}

	/* Only the first call to __rtnl_register can fail */
	__rtnl_register(PF_CAN, RTM_NEWROUTE, cgw_create_job, NULL, NULL);
	__rtnl_register(PF_CAN, RTM_DELROUTE, cgw_remove_job, NULL, NULL);

	return 0;
}

static __exit void cgw_module_exit(void)
{
	rtnl_unregister_all(PF_CAN);

	unregister_netdevice_notifier(&notifier);

	rtnl_lock();
	cgw_remove_all_jobs();
	rtnl_unlock();

	rcu_barrier(); /* Wait for completion of call_rcu()'s */

	kmem_cache_destroy(cgw_cache);
}

module_init(cgw_module_init);
module_exit(cgw_module_exit);
