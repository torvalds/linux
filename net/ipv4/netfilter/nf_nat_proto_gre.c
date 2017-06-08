/*
 * nf_nat_proto_gre.c
 *
 * NAT protocol helper module for GRE.
 *
 * GRE is a generic encapsulation protocol, which is generally not very
 * suited for NAT, as it has no protocol-specific part as port numbers.
 *
 * It has an optional key field, which may help us distinguishing two
 * connections between the same two hosts.
 *
 * GRE is defined in RFC 1701 and RFC 1702, as well as RFC 2784
 *
 * PPTP is built on top of a modified version of GRE, and has a mandatory
 * field called "CallID", which serves us for the same purpose as the key
 * field in plain GRE.
 *
 * Documentation about PPTP can be found in RFC 2637
 *
 * (C) 2000-2005 by Harald Welte <laforge@gnumonks.org>
 *
 * Development of this code funded by Astaro AG (http://www.astaro.com/)
 *
 * (C) 2006-2012 Patrick McHardy <kaber@trash.net>
 *
 */

#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/ip.h>

#include <net/netfilter/nf_nat.h>
#include <net/netfilter/nf_nat_l4proto.h>
#include <linux/netfilter/nf_conntrack_proto_gre.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Harald Welte <laforge@gnumonks.org>");
MODULE_DESCRIPTION("Netfilter NAT protocol helper module for GRE");

/* generate unique tuple ... */
static void
gre_unique_tuple(const struct nf_nat_l3proto *l3proto,
		 struct nf_conntrack_tuple *tuple,
		 const struct nf_nat_range *range,
		 enum nf_nat_manip_type maniptype,
		 const struct nf_conn *ct)
{
	static u_int16_t key;
	__be16 *keyptr;
	unsigned int min, i, range_size;

	/* If there is no master conntrack we are not PPTP,
	   do not change tuples */
	if (!ct->master)
		return;

	if (maniptype == NF_NAT_MANIP_SRC)
		keyptr = &tuple->src.u.gre.key;
	else
		keyptr = &tuple->dst.u.gre.key;

	if (!(range->flags & NF_NAT_RANGE_PROTO_SPECIFIED)) {
		pr_debug("%p: NATing GRE PPTP\n", ct);
		min = 1;
		range_size = 0xffff;
	} else {
		min = ntohs(range->min_proto.gre.key);
		range_size = ntohs(range->max_proto.gre.key) - min + 1;
	}

	pr_debug("min = %u, range_size = %u\n", min, range_size);

	for (i = 0; ; ++key) {
		*keyptr = htons(min + key % range_size);
		if (++i == range_size || !nf_nat_used_tuple(tuple, ct))
			return;
	}

	pr_debug("%p: no NAT mapping\n", ct);
	return;
}

/* manipulate a GRE packet according to maniptype */
static bool
gre_manip_pkt(struct sk_buff *skb,
	      const struct nf_nat_l3proto *l3proto,
	      unsigned int iphdroff, unsigned int hdroff,
	      const struct nf_conntrack_tuple *tuple,
	      enum nf_nat_manip_type maniptype)
{
	const struct gre_base_hdr *greh;
	struct pptp_gre_header *pgreh;

	/* pgreh includes two optional 32bit fields which are not required
	 * to be there.  That's where the magic '8' comes from */
	if (!skb_make_writable(skb, hdroff + sizeof(*pgreh) - 8))
		return false;

	greh = (void *)skb->data + hdroff;
	pgreh = (struct pptp_gre_header *)greh;

	/* we only have destination manip of a packet, since 'source key'
	 * is not present in the packet itself */
	if (maniptype != NF_NAT_MANIP_DST)
		return true;

	switch (greh->flags & GRE_VERSION) {
	case GRE_VERSION_0:
		/* We do not currently NAT any GREv0 packets.
		 * Try to behave like "nf_nat_proto_unknown" */
		break;
	case GRE_VERSION_1:
		pr_debug("call_id -> 0x%04x\n", ntohs(tuple->dst.u.gre.key));
		pgreh->call_id = tuple->dst.u.gre.key;
		break;
	default:
		pr_debug("can't nat unknown GRE version\n");
		return false;
	}
	return true;
}

static const struct nf_nat_l4proto gre = {
	.l4proto		= IPPROTO_GRE,
	.manip_pkt		= gre_manip_pkt,
	.in_range		= nf_nat_l4proto_in_range,
	.unique_tuple		= gre_unique_tuple,
#if IS_ENABLED(CONFIG_NF_CT_NETLINK)
	.nlattr_to_range	= nf_nat_l4proto_nlattr_to_range,
#endif
};

static int __init nf_nat_proto_gre_init(void)
{
	return nf_nat_l4proto_register(NFPROTO_IPV4, &gre);
}

static void __exit nf_nat_proto_gre_fini(void)
{
	nf_nat_l4proto_unregister(NFPROTO_IPV4, &gre);
}

module_init(nf_nat_proto_gre_init);
module_exit(nf_nat_proto_gre_fini);

void nf_nat_need_gre(void)
{
	return;
}
EXPORT_SYMBOL_GPL(nf_nat_need_gre);
