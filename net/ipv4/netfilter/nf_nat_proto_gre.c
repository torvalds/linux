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
 */

#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/ip.h>

#include <net/netfilter/nf_nat.h>
#include <net/netfilter/nf_nat_rule.h>
#include <net/netfilter/nf_nat_protocol.h>
#include <linux/netfilter/nf_conntrack_proto_gre.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Harald Welte <laforge@gnumonks.org>");
MODULE_DESCRIPTION("Netfilter NAT protocol helper module for GRE");

/* is key in given range between min and max */
static int
gre_in_range(const struct nf_conntrack_tuple *tuple,
	     enum nf_nat_manip_type maniptype,
	     const union nf_conntrack_man_proto *min,
	     const union nf_conntrack_man_proto *max)
{
	__be16 key;

	if (maniptype == IP_NAT_MANIP_SRC)
		key = tuple->src.u.gre.key;
	else
		key = tuple->dst.u.gre.key;

	return ntohs(key) >= ntohs(min->gre.key) &&
	       ntohs(key) <= ntohs(max->gre.key);
}

/* generate unique tuple ... */
static int
gre_unique_tuple(struct nf_conntrack_tuple *tuple,
		 const struct nf_nat_range *range,
		 enum nf_nat_manip_type maniptype,
		 const struct nf_conn *conntrack)
{
	static u_int16_t key;
	__be16 *keyptr;
	unsigned int min, i, range_size;

	/* If there is no master conntrack we are not PPTP,
	   do not change tuples */
	if (!conntrack->master)
		return 0;

	if (maniptype == IP_NAT_MANIP_SRC)
		keyptr = &tuple->src.u.gre.key;
	else
		keyptr = &tuple->dst.u.gre.key;

	if (!(range->flags & IP_NAT_RANGE_PROTO_SPECIFIED)) {
		pr_debug("%p: NATing GRE PPTP\n", conntrack);
		min = 1;
		range_size = 0xffff;
	} else {
		min = ntohs(range->min.gre.key);
		range_size = ntohs(range->max.gre.key) - min + 1;
	}

	pr_debug("min = %u, range_size = %u\n", min, range_size);

	for (i = 0; i < range_size; i++, key++) {
		*keyptr = htons(min + key % range_size);
		if (!nf_nat_used_tuple(tuple, conntrack))
			return 1;
	}

	pr_debug("%p: no NAT mapping\n", conntrack);
	return 0;
}

/* manipulate a GRE packet according to maniptype */
static int
gre_manip_pkt(struct sk_buff *skb, unsigned int iphdroff,
	      const struct nf_conntrack_tuple *tuple,
	      enum nf_nat_manip_type maniptype)
{
	struct gre_hdr *greh;
	struct gre_hdr_pptp *pgreh;
	struct iphdr *iph = (struct iphdr *)(skb->data + iphdroff);
	unsigned int hdroff = iphdroff + iph->ihl * 4;

	/* pgreh includes two optional 32bit fields which are not required
	 * to be there.  That's where the magic '8' comes from */
	if (!skb_make_writable(skb, hdroff + sizeof(*pgreh) - 8))
		return 0;

	greh = (void *)skb->data + hdroff;
	pgreh = (struct gre_hdr_pptp *)greh;

	/* we only have destination manip of a packet, since 'source key'
	 * is not present in the packet itself */
	if (maniptype != IP_NAT_MANIP_DST)
		return 1;
	switch (greh->version) {
	case GRE_VERSION_1701:
		/* We do not currently NAT any GREv0 packets.
		 * Try to behave like "nf_nat_proto_unknown" */
		break;
	case GRE_VERSION_PPTP:
		pr_debug("call_id -> 0x%04x\n", ntohs(tuple->dst.u.gre.key));
		pgreh->call_id = tuple->dst.u.gre.key;
		break;
	default:
		pr_debug("can't nat unknown GRE version\n");
		return 0;
	}
	return 1;
}

static struct nf_nat_protocol gre __read_mostly = {
	.name			= "GRE",
	.protonum		= IPPROTO_GRE,
	.manip_pkt		= gre_manip_pkt,
	.in_range		= gre_in_range,
	.unique_tuple		= gre_unique_tuple,
#if defined(CONFIG_NF_CT_NETLINK) || defined(CONFIG_NF_CT_NETLINK_MODULE)
	.range_to_nlattr	= nf_nat_port_range_to_nlattr,
	.nlattr_to_range	= nf_nat_port_nlattr_to_range,
#endif
};

int __init nf_nat_proto_gre_init(void)
{
	return nf_nat_protocol_register(&gre);
}

void __exit nf_nat_proto_gre_fini(void)
{
	nf_nat_protocol_unregister(&gre);
}

module_init(nf_nat_proto_gre_init);
module_exit(nf_nat_proto_gre_fini);

void nf_nat_need_gre(void)
{
	return;
}
EXPORT_SYMBOL_GPL(nf_nat_need_gre);
