/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM openvswitch

#if !defined(_TRACE_OPENVSWITCH_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_OPENVSWITCH_H

#include <linux/tracepoint.h>

#include "datapath.h"

TRACE_EVENT(ovs_do_execute_action,

	TP_PROTO(struct datapath *dp, struct sk_buff *skb,
		 struct sw_flow_key *key, const struct nlattr *a, int rem),

	TP_ARGS(dp, skb, key, a, rem),

	TP_STRUCT__entry(
		__field(	void *,		dpaddr			)
		__string(	dp_name,	ovs_dp_name(dp)		)
		__string(	dev_name,	skb->dev->name		)
		__field(	void *,		skbaddr			)
		__field(	unsigned int,	len			)
		__field(	unsigned int,	data_len		)
		__field(	unsigned int,	truesize		)
		__field(	u8,		nr_frags		)
		__field(	u16,		gso_size		)
		__field(	u16,		gso_type		)
		__field(	u32,		ovs_flow_hash		)
		__field(	u32,		recirc_id		)
		__field(	void *,		keyaddr			)
		__field(	u16,		key_eth_type		)
		__field(	u8,		key_ct_state		)
		__field(	u8,		key_ct_orig_proto	)
		__field(	u16,		key_ct_zone		)
		__field(	unsigned int,	flow_key_valid		)
		__field(	u8,		action_type		)
		__field(	unsigned int,	action_len		)
		__field(	void *,		action_data		)
		__field(	u8,		is_last			)
	),

	TP_fast_assign(
		__entry->dpaddr = dp;
		__assign_str(dp_name);
		__assign_str(dev_name);
		__entry->skbaddr = skb;
		__entry->len = skb->len;
		__entry->data_len = skb->data_len;
		__entry->truesize = skb->truesize;
		__entry->nr_frags = skb_shinfo(skb)->nr_frags;
		__entry->gso_size = skb_shinfo(skb)->gso_size;
		__entry->gso_type = skb_shinfo(skb)->gso_type;
		__entry->ovs_flow_hash = key->ovs_flow_hash;
		__entry->recirc_id = key->recirc_id;
		__entry->keyaddr = key;
		__entry->key_eth_type = key->eth.type;
		__entry->key_ct_state = key->ct_state;
		__entry->key_ct_orig_proto = key->ct_orig_proto;
		__entry->key_ct_zone = key->ct_zone;
		__entry->flow_key_valid = !(key->mac_proto & SW_FLOW_KEY_INVALID);
		__entry->action_type = nla_type(a);
		__entry->action_len = nla_len(a);
		__entry->action_data = nla_data(a);
		__entry->is_last = nla_is_last(a, rem);
	),

	TP_printk("dpaddr=%p dp_name=%s dev=%s skbaddr=%p len=%u data_len=%u truesize=%u nr_frags=%d gso_size=%d gso_type=%#x ovs_flow_hash=0x%08x recirc_id=0x%08x keyaddr=%p eth_type=0x%04x ct_state=%02x ct_orig_proto=%02x ct_Zone=%04x flow_key_valid=%d action_type=%u action_len=%u action_data=%p is_last=%d",
		  __entry->dpaddr, __get_str(dp_name), __get_str(dev_name),
		  __entry->skbaddr, __entry->len, __entry->data_len,
		  __entry->truesize, __entry->nr_frags, __entry->gso_size,
		  __entry->gso_type, __entry->ovs_flow_hash,
		  __entry->recirc_id, __entry->keyaddr, __entry->key_eth_type,
		  __entry->key_ct_state, __entry->key_ct_orig_proto,
		  __entry->key_ct_zone,
		  __entry->flow_key_valid,
		  __entry->action_type, __entry->action_len,
		  __entry->action_data, __entry->is_last)
);

TRACE_EVENT(ovs_dp_upcall,

	TP_PROTO(struct datapath *dp, struct sk_buff *skb,
		 const struct sw_flow_key *key,
		 const struct dp_upcall_info *upcall_info),

	TP_ARGS(dp, skb, key, upcall_info),

	TP_STRUCT__entry(
		__field(	void *,		dpaddr			)
		__string(	dp_name,	ovs_dp_name(dp)		)
		__string(	dev_name,	skb->dev->name		)
		__field(	void *,		skbaddr			)
		__field(	unsigned int,	len			)
		__field(	unsigned int,	data_len		)
		__field(	unsigned int,	truesize		)
		__field(	u8,		nr_frags		)
		__field(	u16,		gso_size		)
		__field(	u16,		gso_type		)
		__field(	u32,		ovs_flow_hash		)
		__field(	u32,		recirc_id		)
		__field(	const void *,	keyaddr			)
		__field(	u16,		key_eth_type		)
		__field(	u8,		key_ct_state		)
		__field(	u8,		key_ct_orig_proto	)
		__field(	u16,		key_ct_zone		)
		__field(	unsigned int,	flow_key_valid		)
		__field(	u8,		upcall_cmd		)
		__field(	u32,		upcall_port		)
		__field(	u16,		upcall_mru		)
	),

	TP_fast_assign(
		__entry->dpaddr = dp;
		__assign_str(dp_name);
		__assign_str(dev_name);
		__entry->skbaddr = skb;
		__entry->len = skb->len;
		__entry->data_len = skb->data_len;
		__entry->truesize = skb->truesize;
		__entry->nr_frags = skb_shinfo(skb)->nr_frags;
		__entry->gso_size = skb_shinfo(skb)->gso_size;
		__entry->gso_type = skb_shinfo(skb)->gso_type;
		__entry->ovs_flow_hash = key->ovs_flow_hash;
		__entry->recirc_id = key->recirc_id;
		__entry->keyaddr = key;
		__entry->key_eth_type = key->eth.type;
		__entry->key_ct_state = key->ct_state;
		__entry->key_ct_orig_proto = key->ct_orig_proto;
		__entry->key_ct_zone = key->ct_zone;
		__entry->flow_key_valid =  !(key->mac_proto & SW_FLOW_KEY_INVALID);
		__entry->upcall_cmd = upcall_info->cmd;
		__entry->upcall_port = upcall_info->portid;
		__entry->upcall_mru = upcall_info->mru;
	),

	TP_printk("dpaddr=%p dp_name=%s dev=%s skbaddr=%p len=%u data_len=%u truesize=%u nr_frags=%d gso_size=%d gso_type=%#x ovs_flow_hash=0x%08x recirc_id=0x%08x keyaddr=%p eth_type=0x%04x ct_state=%02x ct_orig_proto=%02x ct_zone=%04x flow_key_valid=%d upcall_cmd=%u upcall_port=%u upcall_mru=%u",
		  __entry->dpaddr, __get_str(dp_name), __get_str(dev_name),
		  __entry->skbaddr, __entry->len, __entry->data_len,
		  __entry->truesize, __entry->nr_frags, __entry->gso_size,
		  __entry->gso_type, __entry->ovs_flow_hash,
		  __entry->recirc_id, __entry->keyaddr, __entry->key_eth_type,
		  __entry->key_ct_state, __entry->key_ct_orig_proto,
		  __entry->key_ct_zone,
		  __entry->flow_key_valid,
		  __entry->upcall_cmd, __entry->upcall_port,
		  __entry->upcall_mru)
);

#endif /* _TRACE_OPENVSWITCH_H */

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE openvswitch_trace
#include <trace/define_trace.h>
