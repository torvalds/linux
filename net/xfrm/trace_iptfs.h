/* SPDX-License-Identifier: GPL-2.0 */
/* xfrm_trace_iptfs.h
 *
 * August 12 2023, Christian Hopps <chopps@labn.net>
 *
 * Copyright (c) 2023, LabN Consulting, L.L.C.
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM iptfs

#if !defined(_TRACE_IPTFS_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_IPTFS_H

#include <linux/kernel.h>
#include <linux/skbuff.h>
#include <linux/tracepoint.h>
#include <net/ip.h>

struct xfrm_iptfs_data;

TRACE_EVENT(iptfs_egress_recv,
	    TP_PROTO(struct sk_buff *skb, struct xfrm_iptfs_data *xtfs, u16 blkoff),
	    TP_ARGS(skb, xtfs, blkoff),
	    TP_STRUCT__entry(__field(struct sk_buff *, skb)
			     __field(void *, head)
			     __field(void *, head_pg_addr)
			     __field(void *, pg0addr)
			     __field(u32, skb_len)
			     __field(u32, data_len)
			     __field(u32, headroom)
			     __field(u32, tailroom)
			     __field(u32, tail)
			     __field(u32, end)
			     __field(u32, pg0off)
			     __field(u8, head_frag)
			     __field(u8, frag_list)
			     __field(u8, nr_frags)
			     __field(u16, blkoff)),
	    TP_fast_assign(__entry->skb = skb;
			   __entry->head = skb->head;
			   __entry->skb_len = skb->len;
			   __entry->data_len = skb->data_len;
			   __entry->headroom = skb_headroom(skb);
			   __entry->tailroom = skb_tailroom(skb);
			   __entry->tail = (u32)skb->tail;
			   __entry->end = (u32)skb->end;
			   __entry->head_frag = skb->head_frag;
			   __entry->frag_list = (bool)skb_shinfo(skb)->frag_list;
			   __entry->nr_frags = skb_shinfo(skb)->nr_frags;
			   __entry->blkoff = blkoff;
			   __entry->head_pg_addr = page_address(virt_to_head_page(skb->head));
			   __entry->pg0addr = (__entry->nr_frags
					       ? page_address(netmem_to_page(skb_shinfo(skb)->frags[0].netmem))
					       : NULL);
			   __entry->pg0off = (__entry->nr_frags
					      ? skb_shinfo(skb)->frags[0].offset
					      : 0);
		    ),
	    TP_printk("EGRESS: skb=%p len=%u data_len=%u headroom=%u head_frag=%u frag_list=%u nr_frags=%u blkoff=%u\n\t\ttailroom=%u tail=%u end=%u head=%p hdpgaddr=%p pg0->addr=%p pg0->data=%p pg0->off=%u",
		      __entry->skb, __entry->skb_len, __entry->data_len, __entry->headroom,
		      __entry->head_frag, __entry->frag_list, __entry->nr_frags, __entry->blkoff,
		      __entry->tailroom, __entry->tail, __entry->end, __entry->head,
		      __entry->head_pg_addr, __entry->pg0addr, __entry->pg0addr + __entry->pg0off,
		      __entry->pg0off)
	)

DECLARE_EVENT_CLASS(iptfs_ingress_preq_event,
		    TP_PROTO(struct sk_buff *skb, struct xfrm_iptfs_data *xtfs,
			     u32 pmtu, u8 was_gso),
		    TP_ARGS(skb, xtfs, pmtu, was_gso),
		    TP_STRUCT__entry(__field(struct sk_buff *, skb)
				     __field(u32, skb_len)
				     __field(u32, data_len)
				     __field(u32, pmtu)
				     __field(u32, queue_size)
				     __field(u32, proto_seq)
				     __field(u8, proto)
				     __field(u8, was_gso)
			    ),
		    TP_fast_assign(__entry->skb = skb;
				   __entry->skb_len = skb->len;
				   __entry->data_len = skb->data_len;
				   __entry->queue_size =
					xtfs->cfg.max_queue_size - xtfs->queue_size;
				   __entry->proto = __trace_ip_proto(ip_hdr(skb));
				   __entry->proto_seq = __trace_ip_proto_seq(ip_hdr(skb));
				   __entry->pmtu = pmtu;
				   __entry->was_gso = was_gso;
			    ),
		    TP_printk("INGRPREQ: skb=%p len=%u data_len=%u qsize=%u proto=%u proto_seq=%u pmtu=%u was_gso=%u",
			      __entry->skb, __entry->skb_len, __entry->data_len,
			      __entry->queue_size, __entry->proto, __entry->proto_seq,
			      __entry->pmtu, __entry->was_gso));

DEFINE_EVENT(iptfs_ingress_preq_event, iptfs_enqueue,
	     TP_PROTO(struct sk_buff *skb, struct xfrm_iptfs_data *xtfs, u32 pmtu, u8 was_gso),
	     TP_ARGS(skb, xtfs, pmtu, was_gso));

DEFINE_EVENT(iptfs_ingress_preq_event, iptfs_no_queue_space,
	     TP_PROTO(struct sk_buff *skb, struct xfrm_iptfs_data *xtfs, u32 pmtu, u8 was_gso),
	     TP_ARGS(skb, xtfs, pmtu, was_gso));

DEFINE_EVENT(iptfs_ingress_preq_event, iptfs_too_big,
	     TP_PROTO(struct sk_buff *skb, struct xfrm_iptfs_data *xtfs, u32 pmtu, u8 was_gso),
	     TP_ARGS(skb, xtfs, pmtu, was_gso));

DECLARE_EVENT_CLASS(iptfs_ingress_postq_event,
		    TP_PROTO(struct sk_buff *skb, u32 mtu, u16 blkoff, struct iphdr *iph),
		    TP_ARGS(skb, mtu, blkoff, iph),
		    TP_STRUCT__entry(__field(struct sk_buff *, skb)
				     __field(u32, skb_len)
				     __field(u32, data_len)
				     __field(u32, mtu)
				     __field(u32, proto_seq)
				     __field(u16, blkoff)
				     __field(u8, proto)),
		    TP_fast_assign(__entry->skb = skb;
				   __entry->skb_len = skb->len;
				   __entry->data_len = skb->data_len;
				   __entry->mtu = mtu;
				   __entry->blkoff = blkoff;
				   __entry->proto = iph ? __trace_ip_proto(iph) : 0;
				   __entry->proto_seq = iph ? __trace_ip_proto_seq(iph) : 0;
			    ),
		    TP_printk("INGRPSTQ: skb=%p len=%u data_len=%u mtu=%u blkoff=%u proto=%u proto_seq=%u",
			      __entry->skb, __entry->skb_len, __entry->data_len, __entry->mtu,
			      __entry->blkoff, __entry->proto, __entry->proto_seq));

DEFINE_EVENT(iptfs_ingress_postq_event, iptfs_first_dequeue,
	     TP_PROTO(struct sk_buff *skb, u32 mtu, u16 blkoff,
		      struct iphdr *iph),
	     TP_ARGS(skb, mtu, blkoff, iph));

DEFINE_EVENT(iptfs_ingress_postq_event, iptfs_first_fragmenting,
	     TP_PROTO(struct sk_buff *skb, u32 mtu, u16 blkoff,
		      struct iphdr *iph),
	     TP_ARGS(skb, mtu, blkoff, iph));

DEFINE_EVENT(iptfs_ingress_postq_event, iptfs_first_final_fragment,
	     TP_PROTO(struct sk_buff *skb, u32 mtu, u16 blkoff,
		      struct iphdr *iph),
	     TP_ARGS(skb, mtu, blkoff, iph));

DEFINE_EVENT(iptfs_ingress_postq_event, iptfs_first_toobig,
	     TP_PROTO(struct sk_buff *skb, u32 mtu, u16 blkoff,
		      struct iphdr *iph),
	     TP_ARGS(skb, mtu, blkoff, iph));

TRACE_EVENT(iptfs_ingress_nth_peek,
	    TP_PROTO(struct sk_buff *skb, u32 remaining),
	    TP_ARGS(skb, remaining),
	    TP_STRUCT__entry(__field(struct sk_buff *, skb)
			     __field(u32, skb_len)
			     __field(u32, remaining)),
	    TP_fast_assign(__entry->skb = skb;
			   __entry->skb_len = skb->len;
			   __entry->remaining = remaining;
		    ),
	    TP_printk("INGRPSTQ: NTHPEEK: skb=%p len=%u remaining=%u",
		      __entry->skb, __entry->skb_len, __entry->remaining));

TRACE_EVENT(iptfs_ingress_nth_add, TP_PROTO(struct sk_buff *skb, u8 share_ok),
	    TP_ARGS(skb, share_ok),
	    TP_STRUCT__entry(__field(struct sk_buff *, skb)
			     __field(u32, skb_len)
			     __field(u32, data_len)
			     __field(u8, share_ok)
			     __field(u8, head_frag)
			     __field(u8, pp_recycle)
			     __field(u8, cloned)
			     __field(u8, shared)
			     __field(u8, nr_frags)
			     __field(u8, frag_list)
		    ),
	    TP_fast_assign(__entry->skb = skb;
			   __entry->skb_len = skb->len;
			   __entry->data_len = skb->data_len;
			   __entry->share_ok = share_ok;
			   __entry->head_frag = skb->head_frag;
			   __entry->pp_recycle = skb->pp_recycle;
			   __entry->cloned = skb_cloned(skb);
			   __entry->shared = skb_shared(skb);
			   __entry->nr_frags = skb_shinfo(skb)->nr_frags;
			   __entry->frag_list = (bool)skb_shinfo(skb)->frag_list;
		    ),
	    TP_printk("INGRPSTQ: NTHADD: skb=%p len=%u data_len=%u share_ok=%u head_frag=%u pp_recycle=%u cloned=%u shared=%u nr_frags=%u frag_list=%u",
		      __entry->skb, __entry->skb_len, __entry->data_len, __entry->share_ok,
		      __entry->head_frag, __entry->pp_recycle, __entry->cloned, __entry->shared,
		      __entry->nr_frags, __entry->frag_list));

DECLARE_EVENT_CLASS(iptfs_timer_event,
		    TP_PROTO(struct xfrm_iptfs_data *xtfs, u64 time_val),
		    TP_ARGS(xtfs, time_val),
		    TP_STRUCT__entry(__field(u64, time_val)
				     __field(u64, set_time)),
		    TP_fast_assign(__entry->time_val = time_val;
				   __entry->set_time = xtfs->iptfs_settime;
			    ),
		    TP_printk("TIMER: set_time=%llu time_val=%llu",
			      __entry->set_time, __entry->time_val));

DEFINE_EVENT(iptfs_timer_event, iptfs_timer_start,
	     TP_PROTO(struct xfrm_iptfs_data *xtfs, u64 time_val),
	     TP_ARGS(xtfs, time_val));

DEFINE_EVENT(iptfs_timer_event, iptfs_timer_expire,
	     TP_PROTO(struct xfrm_iptfs_data *xtfs, u64 time_val),
	     TP_ARGS(xtfs, time_val));

#endif /* _TRACE_IPTFS_H */

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH ../../net/xfrm
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE trace_iptfs
#include <trace/define_trace.h>
