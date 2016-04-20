#include <linux/errno.h>
#include <linux/ip.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/socket.h>
#include <linux/types.h>
#include <net/checksum.h>
#include <net/ip.h>
#include <net/ip6_fib.h>
#include <net/lwtunnel.h>
#include <net/protocol.h>
#include <uapi/linux/ila.h>
#include "ila.h"

static __wsum get_csum_diff(struct ipv6hdr *ip6h, struct ila_params *p)
{
	if (*(__be64 *)&ip6h->daddr == p->locator_match)
		return p->csum_diff;
	else
		return compute_csum_diff8((__be32 *)&ip6h->daddr,
					  (__be32 *)&p->locator);
}

void update_ipv6_locator(struct sk_buff *skb, struct ila_params *p)
{
	__wsum diff;
	struct ipv6hdr *ip6h = ipv6_hdr(skb);
	size_t nhoff = sizeof(struct ipv6hdr);

	/* First update checksum */
	switch (ip6h->nexthdr) {
	case NEXTHDR_TCP:
		if (likely(pskb_may_pull(skb, nhoff + sizeof(struct tcphdr)))) {
			struct tcphdr *th = (struct tcphdr *)
					(skb_network_header(skb) + nhoff);

			diff = get_csum_diff(ip6h, p);
			inet_proto_csum_replace_by_diff(&th->check, skb,
							diff, true);
		}
		break;
	case NEXTHDR_UDP:
		if (likely(pskb_may_pull(skb, nhoff + sizeof(struct udphdr)))) {
			struct udphdr *uh = (struct udphdr *)
					(skb_network_header(skb) + nhoff);

			if (uh->check || skb->ip_summed == CHECKSUM_PARTIAL) {
				diff = get_csum_diff(ip6h, p);
				inet_proto_csum_replace_by_diff(&uh->check, skb,
								diff, true);
				if (!uh->check)
					uh->check = CSUM_MANGLED_0;
			}
		}
		break;
	case NEXTHDR_ICMP:
		if (likely(pskb_may_pull(skb,
					 nhoff + sizeof(struct icmp6hdr)))) {
			struct icmp6hdr *ih = (struct icmp6hdr *)
					(skb_network_header(skb) + nhoff);

			diff = get_csum_diff(ip6h, p);
			inet_proto_csum_replace_by_diff(&ih->icmp6_cksum, skb,
							diff, true);
		}
		break;
	}

	/* Now change destination address */
	*(__be64 *)&ip6h->daddr = p->locator;
}

static int __init ila_init(void)
{
	int ret;

	ret = ila_lwt_init();

	if (ret)
		goto fail_lwt;

	ret = ila_xlat_init();
	if (ret)
		goto fail_xlat;

	return 0;
fail_xlat:
	ila_lwt_fini();
fail_lwt:
	return ret;
}

static void __exit ila_fini(void)
{
	ila_xlat_fini();
	ila_lwt_fini();
}

module_init(ila_init);
module_exit(ila_fini);
MODULE_ALIAS_RTNL_LWT(ILA);
MODULE_AUTHOR("Tom Herbert <tom@herbertland.com>");
MODULE_LICENSE("GPL");
