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
	struct ila_addr *iaddr = ila_a2i(&ip6h->daddr);

	if (p->locator_match.v64)
		return p->csum_diff;
	else
		return compute_csum_diff8((__be32 *)&iaddr->loc,
					  (__be32 *)&p->locator);
}

static void ila_csum_do_neutral(struct ila_addr *iaddr,
				struct ila_params *p)
{
	__sum16 *adjust = (__force __sum16 *)&iaddr->ident.v16[3];
	__wsum diff, fval;

	/* Check if checksum adjust value has been cached */
	if (p->locator_match.v64) {
		diff = p->csum_diff;
	} else {
		diff = compute_csum_diff8((__be32 *)&p->locator,
					  (__be32 *)iaddr);
	}

	fval = (__force __wsum)(ila_csum_neutral_set(iaddr->ident) ?
			CSUM_NEUTRAL_FLAG : ~CSUM_NEUTRAL_FLAG);

	diff = csum_add(diff, fval);

	*adjust = ~csum_fold(csum_add(diff, csum_unfold(*adjust)));

	/* Flip the csum-neutral bit. Either we are doing a SIR->ILA
	 * translation with ILA_CSUM_NEUTRAL_MAP as the csum_method
	 * and the C-bit is not set, or we are doing an ILA-SIR
	 * tranlsation and the C-bit is set.
	 */
	iaddr->ident.csum_neutral ^= 1;
}

static void ila_csum_adjust_transport(struct sk_buff *skb,
				      struct ila_params *p)
{
	__wsum diff;
	struct ipv6hdr *ip6h = ipv6_hdr(skb);
	struct ila_addr *iaddr = ila_a2i(&ip6h->daddr);
	size_t nhoff = sizeof(struct ipv6hdr);

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
	iaddr->loc = p->locator;
}

void ila_update_ipv6_locator(struct sk_buff *skb, struct ila_params *p,
			     bool set_csum_neutral)
{
	struct ipv6hdr *ip6h = ipv6_hdr(skb);
	struct ila_addr *iaddr = ila_a2i(&ip6h->daddr);

	/* First deal with the transport checksum */
	if (ila_csum_neutral_set(iaddr->ident)) {
		/* C-bit is set in the locator indicating that this
		 * is a locator being translated to a SIR address.
		 * Perform (receiver) checksum-neutral translation.
		 */
		if (!set_csum_neutral)
			ila_csum_do_neutral(iaddr, p);
	} else {
		switch (p->csum_mode) {
		case ILA_CSUM_ADJUST_TRANSPORT:
			ila_csum_adjust_transport(skb, p);
			break;
		case ILA_CSUM_NEUTRAL_MAP:
			ila_csum_do_neutral(iaddr, p);
			break;
		case ILA_CSUM_NO_ACTION:
			break;
		}
	}

	/* Now change destination address */
	iaddr->loc = p->locator;
}

void ila_init_saved_csum(struct ila_params *p)
{
	if (!p->locator_match.v64)
		return;

	p->csum_diff = compute_csum_diff8(
				(__be32 *)&p->locator,
				(__be32 *)&p->locator_match);
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
MODULE_AUTHOR("Tom Herbert <tom@herbertland.com>");
MODULE_LICENSE("GPL");
