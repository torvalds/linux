// SPDX-License-Identifier: GPL-2.0
#include <linux/export.h>
#include <linux/icmpv6.h>
#include <linux/mutex.h>
#include <linux/netdevice.h>
#include <linux/spinlock.h>

#include <net/ipv6.h>

#if IS_ENABLED(CONFIG_IPV6) && IS_ENABLED(CONFIG_NF_NAT)

#include <net/netfilter/nf_conntrack.h>
void icmpv6_ndo_send(struct sk_buff *skb_in, u8 type, u8 code, __u32 info)
{
	struct inet6_skb_parm parm = { 0 };
	struct sk_buff *cloned_skb = NULL;
	enum ip_conntrack_info ctinfo;
	enum ip_conntrack_dir dir;
	struct in6_addr orig_ip;
	struct nf_conn *ct;

	ct = nf_ct_get(skb_in, &ctinfo);
	if (!ct || !(READ_ONCE(ct->status) & IPS_NAT_MASK)) {
		icmp6_send(skb_in, type, code, info, NULL, &parm);
		return;
	}

	if (skb_shared(skb_in))
		skb_in = cloned_skb = skb_clone(skb_in, GFP_ATOMIC);

	if (unlikely(!skb_in || skb_network_header(skb_in) < skb_in->head ||
	    (skb_network_header(skb_in) + sizeof(struct ipv6hdr)) >
	    skb_tail_pointer(skb_in) || skb_ensure_writable(skb_in,
	    skb_network_offset(skb_in) + sizeof(struct ipv6hdr))))
		goto out;

	orig_ip = ipv6_hdr(skb_in)->saddr;
	dir = CTINFO2DIR(ctinfo);
	ipv6_hdr(skb_in)->saddr = ct->tuplehash[dir].tuple.src.u3.in6;
	icmp6_send(skb_in, type, code, info, NULL, &parm);
	ipv6_hdr(skb_in)->saddr = orig_ip;
out:
	consume_skb(cloned_skb);
}
EXPORT_SYMBOL(icmpv6_ndo_send);
#endif
