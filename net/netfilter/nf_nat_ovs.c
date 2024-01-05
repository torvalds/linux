// SPDX-License-Identifier: GPL-2.0-only
/* Support nat functions for openvswitch and used by OVS and TC conntrack. */

#include <net/netfilter/nf_nat.h>

/* Modelled after nf_nat_ipv[46]_fn().
 * range is only used for new, uninitialized NAT state.
 * Returns either NF_ACCEPT or NF_DROP.
 */
static int nf_ct_nat_execute(struct sk_buff *skb, struct nf_conn *ct,
			     enum ip_conntrack_info ctinfo, int *action,
			     const struct nf_nat_range2 *range,
			     enum nf_nat_manip_type maniptype)
{
	__be16 proto = skb_protocol(skb, true);
	int hooknum, err = NF_ACCEPT;

	/* See HOOK2MANIP(). */
	if (maniptype == NF_NAT_MANIP_SRC)
		hooknum = NF_INET_LOCAL_IN; /* Source NAT */
	else
		hooknum = NF_INET_LOCAL_OUT; /* Destination NAT */

	switch (ctinfo) {
	case IP_CT_RELATED:
	case IP_CT_RELATED_REPLY:
		if (proto == htons(ETH_P_IP) &&
		    ip_hdr(skb)->protocol == IPPROTO_ICMP) {
			if (!nf_nat_icmp_reply_translation(skb, ct, ctinfo,
							   hooknum))
				err = NF_DROP;
			goto out;
		} else if (IS_ENABLED(CONFIG_IPV6) && proto == htons(ETH_P_IPV6)) {
			__be16 frag_off;
			u8 nexthdr = ipv6_hdr(skb)->nexthdr;
			int hdrlen = ipv6_skip_exthdr(skb,
						      sizeof(struct ipv6hdr),
						      &nexthdr, &frag_off);

			if (hdrlen >= 0 && nexthdr == IPPROTO_ICMPV6) {
				if (!nf_nat_icmpv6_reply_translation(skb, ct,
								     ctinfo,
								     hooknum,
								     hdrlen))
					err = NF_DROP;
				goto out;
			}
		}
		/* Non-ICMP, fall thru to initialize if needed. */
		fallthrough;
	case IP_CT_NEW:
		/* Seen it before?  This can happen for loopback, retrans,
		 * or local packets.
		 */
		if (!nf_nat_initialized(ct, maniptype)) {
			/* Initialize according to the NAT action. */
			err = (range && range->flags & NF_NAT_RANGE_MAP_IPS)
				/* Action is set up to establish a new
				 * mapping.
				 */
				? nf_nat_setup_info(ct, range, maniptype)
				: nf_nat_alloc_null_binding(ct, hooknum);
			if (err != NF_ACCEPT)
				goto out;
		}
		break;

	case IP_CT_ESTABLISHED:
	case IP_CT_ESTABLISHED_REPLY:
		break;

	default:
		err = NF_DROP;
		goto out;
	}

	err = nf_nat_packet(ct, ctinfo, hooknum, skb);
out:
	if (err == NF_ACCEPT)
		*action |= BIT(maniptype);

	return err;
}

int nf_ct_nat(struct sk_buff *skb, struct nf_conn *ct,
	      enum ip_conntrack_info ctinfo, int *action,
	      const struct nf_nat_range2 *range, bool commit)
{
	enum nf_nat_manip_type maniptype;
	int err, ct_action = *action;

	*action = 0;

	/* Add NAT extension if not confirmed yet. */
	if (!nf_ct_is_confirmed(ct) && !nf_ct_nat_ext_add(ct))
		return NF_DROP;   /* Can't NAT. */

	if (ctinfo != IP_CT_NEW && (ct->status & IPS_NAT_MASK) &&
	    (ctinfo != IP_CT_RELATED || commit)) {
		/* NAT an established or related connection like before. */
		if (CTINFO2DIR(ctinfo) == IP_CT_DIR_REPLY)
			/* This is the REPLY direction for a connection
			 * for which NAT was applied in the forward
			 * direction.  Do the reverse NAT.
			 */
			maniptype = ct->status & IPS_SRC_NAT
				? NF_NAT_MANIP_DST : NF_NAT_MANIP_SRC;
		else
			maniptype = ct->status & IPS_SRC_NAT
				? NF_NAT_MANIP_SRC : NF_NAT_MANIP_DST;
	} else if (ct_action & BIT(NF_NAT_MANIP_SRC)) {
		maniptype = NF_NAT_MANIP_SRC;
	} else if (ct_action & BIT(NF_NAT_MANIP_DST)) {
		maniptype = NF_NAT_MANIP_DST;
	} else {
		return NF_ACCEPT;
	}

	err = nf_ct_nat_execute(skb, ct, ctinfo, action, range, maniptype);
	if (err == NF_ACCEPT && ct->status & IPS_DST_NAT) {
		if (ct->status & IPS_SRC_NAT) {
			if (maniptype == NF_NAT_MANIP_SRC)
				maniptype = NF_NAT_MANIP_DST;
			else
				maniptype = NF_NAT_MANIP_SRC;

			err = nf_ct_nat_execute(skb, ct, ctinfo, action, range,
						maniptype);
		} else if (CTINFO2DIR(ctinfo) == IP_CT_DIR_ORIGINAL) {
			err = nf_ct_nat_execute(skb, ct, ctinfo, action, NULL,
						NF_NAT_MANIP_SRC);
		}
	}
	return err;
}
EXPORT_SYMBOL_GPL(nf_ct_nat);
