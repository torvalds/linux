/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM icmp

#if !defined(_TRACE_ICMP_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_ICMP_H

#include <linux/icmp.h>
#include <linux/tracepoint.h>

TRACE_EVENT(icmp_send,

		TP_PROTO(const struct sk_buff *skb, int type, int code),

		TP_ARGS(skb, type, code),

		TP_STRUCT__entry(
			__field(const void *, skbaddr)
			__field(int, type)
			__field(int, code)
			__array(__u8, saddr, 4)
			__array(__u8, daddr, 4)
			__field(__u16, sport)
			__field(__u16, dport)
			__field(unsigned short, ulen)
		),

		TP_fast_assign(
			struct iphdr *iph = ip_hdr(skb);
			struct udphdr *uh = udp_hdr(skb);
			int proto_4 = iph->protocol;
			__be32 *p32;

			__entry->skbaddr = skb;
			__entry->type = type;
			__entry->code = code;

			if (proto_4 != IPPROTO_UDP || (u8 *)uh < skb->head ||
				(u8 *)uh + sizeof(struct udphdr)
				> skb_tail_pointer(skb)) {
				__entry->sport = 0;
				__entry->dport = 0;
				__entry->ulen = 0;
			} else {
				__entry->sport = ntohs(uh->source);
				__entry->dport = ntohs(uh->dest);
				__entry->ulen = ntohs(uh->len);
			}

			p32 = (__be32 *) __entry->saddr;
			*p32 = iph->saddr;

			p32 = (__be32 *) __entry->daddr;
			*p32 = iph->daddr;
		),

		TP_printk("icmp_send: type=%d, code=%d. From %pI4:%u to %pI4:%u ulen=%d skbaddr=%p",
			__entry->type, __entry->code,
			__entry->saddr, __entry->sport, __entry->daddr,
			__entry->dport, __entry->ulen, __entry->skbaddr)
);

#endif /* _TRACE_ICMP_H */

/* This part must be outside protection */
#include <trace/define_trace.h>

