// SPDX-License-Identifier: GPL-2.0-only
/*
 * Connection tracking protocol helper module for GRE.
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
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/timer.h>
#include <linux/list.h>
#include <linux/seq_file.h>
#include <linux/in.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <net/dst.h>
#include <net/net_namespace.h>
#include <net/netns/generic.h>
#include <net/netfilter/nf_conntrack_l4proto.h>
#include <net/netfilter/nf_conntrack_helper.h>
#include <net/netfilter/nf_conntrack_core.h>
#include <net/netfilter/nf_conntrack_timeout.h>
#include <linux/netfilter/nf_conntrack_proto_gre.h>
#include <linux/netfilter/nf_conntrack_pptp.h>

static const unsigned int gre_timeouts[GRE_CT_MAX] = {
	[GRE_CT_UNREPLIED]	= 30*HZ,
	[GRE_CT_REPLIED]	= 180*HZ,
};

/* used when expectation is added */
static DEFINE_SPINLOCK(keymap_lock);

static inline struct nf_gre_net *gre_pernet(struct net *net)
{
	return &net->ct.nf_ct_proto.gre;
}

static inline int gre_key_cmpfn(const struct nf_ct_gre_keymap *km,
				const struct nf_conntrack_tuple *t)
{
	return km->tuple.src.l3num == t->src.l3num &&
	       !memcmp(&km->tuple.src.u3, &t->src.u3, sizeof(t->src.u3)) &&
	       !memcmp(&km->tuple.dst.u3, &t->dst.u3, sizeof(t->dst.u3)) &&
	       km->tuple.dst.protonum == t->dst.protonum &&
	       km->tuple.dst.u.all == t->dst.u.all;
}

/* look up the source key for a given tuple */
static __be16 gre_keymap_lookup(struct net *net, struct nf_conntrack_tuple *t)
{
	struct nf_gre_net *net_gre = gre_pernet(net);
	struct nf_ct_gre_keymap *km;
	__be16 key = 0;

	list_for_each_entry_rcu(km, &net_gre->keymap_list, list) {
		if (gre_key_cmpfn(km, t)) {
			key = km->tuple.src.u.gre.key;
			break;
		}
	}

	pr_debug("lookup src key 0x%x for ", key);
	nf_ct_dump_tuple(t);

	return key;
}

/* add a single keymap entry, associate with specified master ct */
int nf_ct_gre_keymap_add(struct nf_conn *ct, enum ip_conntrack_dir dir,
			 struct nf_conntrack_tuple *t)
{
	struct net *net = nf_ct_net(ct);
	struct nf_gre_net *net_gre = gre_pernet(net);
	struct nf_ct_pptp_master *ct_pptp_info = nfct_help_data(ct);
	struct nf_ct_gre_keymap **kmp, *km;

	kmp = &ct_pptp_info->keymap[dir];
	if (*kmp) {
		/* check whether it's a retransmission */
		list_for_each_entry_rcu(km, &net_gre->keymap_list, list) {
			if (gre_key_cmpfn(km, t) && km == *kmp)
				return 0;
		}
		pr_debug("trying to override keymap_%s for ct %p\n",
			 dir == IP_CT_DIR_REPLY ? "reply" : "orig", ct);
		return -EEXIST;
	}

	km = kmalloc(sizeof(*km), GFP_ATOMIC);
	if (!km)
		return -ENOMEM;
	memcpy(&km->tuple, t, sizeof(*t));
	*kmp = km;

	pr_debug("adding new entry %p: ", km);
	nf_ct_dump_tuple(&km->tuple);

	spin_lock_bh(&keymap_lock);
	list_add_tail(&km->list, &net_gre->keymap_list);
	spin_unlock_bh(&keymap_lock);

	return 0;
}
EXPORT_SYMBOL_GPL(nf_ct_gre_keymap_add);

/* destroy the keymap entries associated with specified master ct */
void nf_ct_gre_keymap_destroy(struct nf_conn *ct)
{
	struct nf_ct_pptp_master *ct_pptp_info = nfct_help_data(ct);
	enum ip_conntrack_dir dir;

	pr_debug("entering for ct %p\n", ct);

	spin_lock_bh(&keymap_lock);
	for (dir = IP_CT_DIR_ORIGINAL; dir < IP_CT_DIR_MAX; dir++) {
		if (ct_pptp_info->keymap[dir]) {
			pr_debug("removing %p from list\n",
				 ct_pptp_info->keymap[dir]);
			list_del_rcu(&ct_pptp_info->keymap[dir]->list);
			kfree_rcu(ct_pptp_info->keymap[dir], rcu);
			ct_pptp_info->keymap[dir] = NULL;
		}
	}
	spin_unlock_bh(&keymap_lock);
}
EXPORT_SYMBOL_GPL(nf_ct_gre_keymap_destroy);

/* PUBLIC CONNTRACK PROTO HELPER FUNCTIONS */

/* gre hdr info to tuple */
bool gre_pkt_to_tuple(const struct sk_buff *skb, unsigned int dataoff,
		      struct net *net, struct nf_conntrack_tuple *tuple)
{
	const struct pptp_gre_header *pgrehdr;
	struct pptp_gre_header _pgrehdr;
	__be16 srckey;
	const struct gre_base_hdr *grehdr;
	struct gre_base_hdr _grehdr;

	/* first only delinearize old RFC1701 GRE header */
	grehdr = skb_header_pointer(skb, dataoff, sizeof(_grehdr), &_grehdr);
	if (!grehdr || (grehdr->flags & GRE_VERSION) != GRE_VERSION_1) {
		/* try to behave like "nf_conntrack_proto_generic" */
		tuple->src.u.all = 0;
		tuple->dst.u.all = 0;
		return true;
	}

	/* PPTP header is variable length, only need up to the call_id field */
	pgrehdr = skb_header_pointer(skb, dataoff, 8, &_pgrehdr);
	if (!pgrehdr)
		return true;

	if (grehdr->protocol != GRE_PROTO_PPP) {
		pr_debug("Unsupported GRE proto(0x%x)\n", ntohs(grehdr->protocol));
		return false;
	}

	tuple->dst.u.gre.key = pgrehdr->call_id;
	srckey = gre_keymap_lookup(net, tuple);
	tuple->src.u.gre.key = srckey;

	return true;
}

#ifdef CONFIG_NF_CONNTRACK_PROCFS
/* print private data for conntrack */
static void gre_print_conntrack(struct seq_file *s, struct nf_conn *ct)
{
	seq_printf(s, "timeout=%u, stream_timeout=%u ",
		   (ct->proto.gre.timeout / HZ),
		   (ct->proto.gre.stream_timeout / HZ));
}
#endif

static unsigned int *gre_get_timeouts(struct net *net)
{
	return gre_pernet(net)->timeouts;
}

/* Returns verdict for packet, and may modify conntrack */
int nf_conntrack_gre_packet(struct nf_conn *ct,
			    struct sk_buff *skb,
			    unsigned int dataoff,
			    enum ip_conntrack_info ctinfo,
			    const struct nf_hook_state *state)
{
	if (!nf_ct_is_confirmed(ct)) {
		unsigned int *timeouts = nf_ct_timeout_lookup(ct);

		if (!timeouts)
			timeouts = gre_get_timeouts(nf_ct_net(ct));

		/* initialize to sane value.  Ideally a conntrack helper
		 * (e.g. in case of pptp) is increasing them */
		ct->proto.gre.stream_timeout = timeouts[GRE_CT_REPLIED];
		ct->proto.gre.timeout = timeouts[GRE_CT_UNREPLIED];
	}

	/* If we've seen traffic both ways, this is a GRE connection.
	 * Extend timeout. */
	if (ct->status & IPS_SEEN_REPLY) {
		nf_ct_refresh_acct(ct, ctinfo, skb,
				   ct->proto.gre.stream_timeout);
		/* Also, more likely to be important, and not a probe. */
		if (!test_and_set_bit(IPS_ASSURED_BIT, &ct->status))
			nf_conntrack_event_cache(IPCT_ASSURED, ct);
	} else
		nf_ct_refresh_acct(ct, ctinfo, skb,
				   ct->proto.gre.timeout);

	return NF_ACCEPT;
}

#ifdef CONFIG_NF_CONNTRACK_TIMEOUT

#include <linux/netfilter/nfnetlink.h>
#include <linux/netfilter/nfnetlink_cttimeout.h>

static int gre_timeout_nlattr_to_obj(struct nlattr *tb[],
				     struct net *net, void *data)
{
	unsigned int *timeouts = data;
	struct nf_gre_net *net_gre = gre_pernet(net);

	if (!timeouts)
		timeouts = gre_get_timeouts(net);
	/* set default timeouts for GRE. */
	timeouts[GRE_CT_UNREPLIED] = net_gre->timeouts[GRE_CT_UNREPLIED];
	timeouts[GRE_CT_REPLIED] = net_gre->timeouts[GRE_CT_REPLIED];

	if (tb[CTA_TIMEOUT_GRE_UNREPLIED]) {
		timeouts[GRE_CT_UNREPLIED] =
			ntohl(nla_get_be32(tb[CTA_TIMEOUT_GRE_UNREPLIED])) * HZ;
	}
	if (tb[CTA_TIMEOUT_GRE_REPLIED]) {
		timeouts[GRE_CT_REPLIED] =
			ntohl(nla_get_be32(tb[CTA_TIMEOUT_GRE_REPLIED])) * HZ;
	}
	return 0;
}

static int
gre_timeout_obj_to_nlattr(struct sk_buff *skb, const void *data)
{
	const unsigned int *timeouts = data;

	if (nla_put_be32(skb, CTA_TIMEOUT_GRE_UNREPLIED,
			 htonl(timeouts[GRE_CT_UNREPLIED] / HZ)) ||
	    nla_put_be32(skb, CTA_TIMEOUT_GRE_REPLIED,
			 htonl(timeouts[GRE_CT_REPLIED] / HZ)))
		goto nla_put_failure;
	return 0;

nla_put_failure:
	return -ENOSPC;
}

static const struct nla_policy
gre_timeout_nla_policy[CTA_TIMEOUT_GRE_MAX+1] = {
	[CTA_TIMEOUT_GRE_UNREPLIED]	= { .type = NLA_U32 },
	[CTA_TIMEOUT_GRE_REPLIED]	= { .type = NLA_U32 },
};
#endif /* CONFIG_NF_CONNTRACK_TIMEOUT */

void nf_conntrack_gre_init_net(struct net *net)
{
	struct nf_gre_net *net_gre = gre_pernet(net);
	int i;

	INIT_LIST_HEAD(&net_gre->keymap_list);
	for (i = 0; i < GRE_CT_MAX; i++)
		net_gre->timeouts[i] = gre_timeouts[i];
}

/* protocol helper struct */
const struct nf_conntrack_l4proto nf_conntrack_l4proto_gre = {
	.l4proto	 = IPPROTO_GRE,
#ifdef CONFIG_NF_CONNTRACK_PROCFS
	.print_conntrack = gre_print_conntrack,
#endif
#if IS_ENABLED(CONFIG_NF_CT_NETLINK)
	.tuple_to_nlattr = nf_ct_port_tuple_to_nlattr,
	.nlattr_tuple_size = nf_ct_port_nlattr_tuple_size,
	.nlattr_to_tuple = nf_ct_port_nlattr_to_tuple,
	.nla_policy	 = nf_ct_port_nla_policy,
#endif
#ifdef CONFIG_NF_CONNTRACK_TIMEOUT
	.ctnl_timeout    = {
		.nlattr_to_obj	= gre_timeout_nlattr_to_obj,
		.obj_to_nlattr	= gre_timeout_obj_to_nlattr,
		.nlattr_max	= CTA_TIMEOUT_GRE_MAX,
		.obj_size	= sizeof(unsigned int) * GRE_CT_MAX,
		.nla_policy	= gre_timeout_nla_policy,
	},
#endif /* CONFIG_NF_CONNTRACK_TIMEOUT */
};
