/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM net

#if !defined(_TRACE_NET_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_NET_H

#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/if_vlan.h>
#include <linux/ip.h>
#include <linux/tracepoint.h>

TRACE_EVENT(net_dev_start_xmit,

	TP_PROTO(const struct sk_buff *skb, const struct net_device *dev),

	TP_ARGS(skb, dev),

	TP_STRUCT__entry(
		__string(	name,			dev->name	)
		__field(	u16,			queue_mapping	)
		__field(	const void *,		skbaddr		)
		__field(	bool,			vlan_tagged	)
		__field(	u16,			vlan_proto	)
		__field(	u16,			vlan_tci	)
		__field(	u16,			protocol	)
		__field(	u8,			ip_summed	)
		__field(	unsigned int,		len		)
		__field(	unsigned int,		data_len	)
		__field(	int,			network_offset	)
		__field(	bool,			transport_offset_valid)
		__field(	int,			transport_offset)
		__field(	u8,			tx_flags	)
		__field(	u16,			gso_size	)
		__field(	u16,			gso_segs	)
		__field(	u16,			gso_type	)
	),

	TP_fast_assign(
		__assign_str(name);
		__entry->queue_mapping = skb->queue_mapping;
		__entry->skbaddr = skb;
		__entry->vlan_tagged = skb_vlan_tag_present(skb);
		__entry->vlan_proto = ntohs(skb->vlan_proto);
		__entry->vlan_tci = skb_vlan_tag_get(skb);
		__entry->protocol = ntohs(skb->protocol);
		__entry->ip_summed = skb->ip_summed;
		__entry->len = skb->len;
		__entry->data_len = skb->data_len;
		__entry->network_offset = skb_network_offset(skb);
		__entry->transport_offset_valid =
			skb_transport_header_was_set(skb);
		__entry->transport_offset = skb_transport_header_was_set(skb) ?
			skb_transport_offset(skb) : 0;
		__entry->tx_flags = skb_shinfo(skb)->tx_flags;
		__entry->gso_size = skb_shinfo(skb)->gso_size;
		__entry->gso_segs = skb_shinfo(skb)->gso_segs;
		__entry->gso_type = skb_shinfo(skb)->gso_type;
	),

	TP_printk("dev=%s queue_mapping=%u skbaddr=%p vlan_tagged=%d vlan_proto=0x%04x vlan_tci=0x%04x protocol=0x%04x ip_summed=%d len=%u data_len=%u network_offset=%d transport_offset_valid=%d transport_offset=%d tx_flags=%d gso_size=%d gso_segs=%d gso_type=%#x",
		  __get_str(name), __entry->queue_mapping, __entry->skbaddr,
		  __entry->vlan_tagged, __entry->vlan_proto, __entry->vlan_tci,
		  __entry->protocol, __entry->ip_summed, __entry->len,
		  __entry->data_len,
		  __entry->network_offset, __entry->transport_offset_valid,
		  __entry->transport_offset, __entry->tx_flags,
		  __entry->gso_size, __entry->gso_segs, __entry->gso_type)
);

TRACE_EVENT(net_dev_xmit,

	TP_PROTO(struct sk_buff *skb,
		 int rc,
		 struct net_device *dev,
		 unsigned int skb_len),

	TP_ARGS(skb, rc, dev, skb_len),

	TP_STRUCT__entry(
		__field(	void *,		skbaddr		)
		__field(	unsigned int,	len		)
		__field(	int,		rc		)
		__string(	name,		dev->name	)
	),

	TP_fast_assign(
		__entry->skbaddr = skb;
		__entry->len = skb_len;
		__entry->rc = rc;
		__assign_str(name);
	),

	TP_printk("dev=%s skbaddr=%p len=%u rc=%d",
		__get_str(name), __entry->skbaddr, __entry->len, __entry->rc)
);

TRACE_EVENT(net_dev_xmit_timeout,

	TP_PROTO(struct net_device *dev,
		 int queue_index),

	TP_ARGS(dev, queue_index),

	TP_STRUCT__entry(
		__string(	name,		dev->name	)
		__string(	driver,		netdev_drivername(dev))
		__field(	int,		queue_index	)
	),

	TP_fast_assign(
		__assign_str(name);
		__assign_str(driver);
		__entry->queue_index = queue_index;
	),

	TP_printk("dev=%s driver=%s queue=%d",
		__get_str(name), __get_str(driver), __entry->queue_index)
);

DECLARE_EVENT_CLASS(net_dev_template,

	TP_PROTO(struct sk_buff *skb),

	TP_ARGS(skb),

	TP_STRUCT__entry(
		__field(	void *,		skbaddr		)
		__field(	unsigned int,	len		)
		__string(	name,		skb->dev->name	)
	),

	TP_fast_assign(
		__entry->skbaddr = skb;
		__entry->len = skb->len;
		__assign_str(name);
	),

	TP_printk("dev=%s skbaddr=%p len=%u",
		__get_str(name), __entry->skbaddr, __entry->len)
)

DEFINE_EVENT(net_dev_template, net_dev_queue,

	TP_PROTO(struct sk_buff *skb),

	TP_ARGS(skb)
);

DEFINE_EVENT(net_dev_template, netif_receive_skb,

	TP_PROTO(struct sk_buff *skb),

	TP_ARGS(skb)
);

DEFINE_EVENT(net_dev_template, netif_rx,

	TP_PROTO(struct sk_buff *skb),

	TP_ARGS(skb)
);

DECLARE_EVENT_CLASS(net_dev_rx_verbose_template,

	TP_PROTO(const struct sk_buff *skb),

	TP_ARGS(skb),

	TP_STRUCT__entry(
		__string(	name,			skb->dev->name	)
		__field(	unsigned int,		napi_id		)
		__field(	u16,			queue_mapping	)
		__field(	const void *,		skbaddr		)
		__field(	bool,			vlan_tagged	)
		__field(	u16,			vlan_proto	)
		__field(	u16,			vlan_tci	)
		__field(	u16,			protocol	)
		__field(	u8,			ip_summed	)
		__field(	u32,			hash		)
		__field(	bool,			l4_hash		)
		__field(	unsigned int,		len		)
		__field(	unsigned int,		data_len	)
		__field(	unsigned int,		truesize	)
		__field(	bool,			mac_header_valid)
		__field(	int,			mac_header	)
		__field(	unsigned char,		nr_frags	)
		__field(	u16,			gso_size	)
		__field(	u16,			gso_type	)
	),

	TP_fast_assign(
		__assign_str(name);
#ifdef CONFIG_NET_RX_BUSY_POLL
		__entry->napi_id = skb->napi_id;
#else
		__entry->napi_id = 0;
#endif
		__entry->queue_mapping = skb->queue_mapping;
		__entry->skbaddr = skb;
		__entry->vlan_tagged = skb_vlan_tag_present(skb);
		__entry->vlan_proto = ntohs(skb->vlan_proto);
		__entry->vlan_tci = skb_vlan_tag_get(skb);
		__entry->protocol = ntohs(skb->protocol);
		__entry->ip_summed = skb->ip_summed;
		__entry->hash = skb->hash;
		__entry->l4_hash = skb->l4_hash;
		__entry->len = skb->len;
		__entry->data_len = skb->data_len;
		__entry->truesize = skb->truesize;
		__entry->mac_header_valid = skb_mac_header_was_set(skb);
		__entry->mac_header = skb_mac_header(skb) - skb->data;
		__entry->nr_frags = skb_shinfo(skb)->nr_frags;
		__entry->gso_size = skb_shinfo(skb)->gso_size;
		__entry->gso_type = skb_shinfo(skb)->gso_type;
	),

	TP_printk("dev=%s napi_id=%#x queue_mapping=%u skbaddr=%p vlan_tagged=%d vlan_proto=0x%04x vlan_tci=0x%04x protocol=0x%04x ip_summed=%d hash=0x%08x l4_hash=%d len=%u data_len=%u truesize=%u mac_header_valid=%d mac_header=%d nr_frags=%d gso_size=%d gso_type=%#x",
		  __get_str(name), __entry->napi_id, __entry->queue_mapping,
		  __entry->skbaddr, __entry->vlan_tagged, __entry->vlan_proto,
		  __entry->vlan_tci, __entry->protocol, __entry->ip_summed,
		  __entry->hash, __entry->l4_hash, __entry->len,
		  __entry->data_len, __entry->truesize,
		  __entry->mac_header_valid, __entry->mac_header,
		  __entry->nr_frags, __entry->gso_size, __entry->gso_type)
);

DEFINE_EVENT(net_dev_rx_verbose_template, napi_gro_frags_entry,

	TP_PROTO(const struct sk_buff *skb),

	TP_ARGS(skb)
);

DEFINE_EVENT(net_dev_rx_verbose_template, napi_gro_receive_entry,

	TP_PROTO(const struct sk_buff *skb),

	TP_ARGS(skb)
);

DEFINE_EVENT(net_dev_rx_verbose_template, netif_receive_skb_entry,

	TP_PROTO(const struct sk_buff *skb),

	TP_ARGS(skb)
);

DEFINE_EVENT(net_dev_rx_verbose_template, netif_receive_skb_list_entry,

	TP_PROTO(const struct sk_buff *skb),

	TP_ARGS(skb)
);

DEFINE_EVENT(net_dev_rx_verbose_template, netif_rx_entry,

	TP_PROTO(const struct sk_buff *skb),

	TP_ARGS(skb)
);

DECLARE_EVENT_CLASS(net_dev_rx_exit_template,

	TP_PROTO(int ret),

	TP_ARGS(ret),

	TP_STRUCT__entry(
		__field(int,	ret)
	),

	TP_fast_assign(
		__entry->ret = ret;
	),

	TP_printk("ret=%d", __entry->ret)
);

DEFINE_EVENT(net_dev_rx_exit_template, napi_gro_frags_exit,

	TP_PROTO(int ret),

	TP_ARGS(ret)
);

DEFINE_EVENT(net_dev_rx_exit_template, napi_gro_receive_exit,

	TP_PROTO(int ret),

	TP_ARGS(ret)
);

DEFINE_EVENT(net_dev_rx_exit_template, netif_receive_skb_exit,

	TP_PROTO(int ret),

	TP_ARGS(ret)
);

DEFINE_EVENT(net_dev_rx_exit_template, netif_rx_exit,

	TP_PROTO(int ret),

	TP_ARGS(ret)
);

DEFINE_EVENT(net_dev_rx_exit_template, netif_receive_skb_list_exit,

	TP_PROTO(int ret),

	TP_ARGS(ret)
);

#endif /* _TRACE_NET_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
