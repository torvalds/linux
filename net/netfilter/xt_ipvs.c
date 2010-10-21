/*
 *	xt_ipvs - kernel module to match IPVS connection properties
 *
 *	Author: Hannes Eder <heder@google.com>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/spinlock.h>
#include <linux/skbuff.h>
#ifdef CONFIG_IP_VS_IPV6
#include <net/ipv6.h>
#endif
#include <linux/ip_vs.h>
#include <linux/types.h>
#include <linux/netfilter/x_tables.h>
#include <linux/netfilter/xt_ipvs.h>
#include <net/netfilter/nf_conntrack.h>

#include <net/ip_vs.h>

MODULE_AUTHOR("Hannes Eder <heder@google.com>");
MODULE_DESCRIPTION("Xtables: match IPVS connection properties");
MODULE_LICENSE("GPL");
MODULE_ALIAS("ipt_ipvs");
MODULE_ALIAS("ip6t_ipvs");

/* borrowed from xt_conntrack */
static bool ipvs_mt_addrcmp(const union nf_inet_addr *kaddr,
			    const union nf_inet_addr *uaddr,
			    const union nf_inet_addr *umask,
			    unsigned int l3proto)
{
	if (l3proto == NFPROTO_IPV4)
		return ((kaddr->ip ^ uaddr->ip) & umask->ip) == 0;
#ifdef CONFIG_IP_VS_IPV6
	else if (l3proto == NFPROTO_IPV6)
		return ipv6_masked_addr_cmp(&kaddr->in6, &umask->in6,
		       &uaddr->in6) == 0;
#endif
	else
		return false;
}

static bool
ipvs_mt(const struct sk_buff *skb, struct xt_action_param *par)
{
	const struct xt_ipvs_mtinfo *data = par->matchinfo;
	/* ipvs_mt_check ensures that family is only NFPROTO_IPV[46]. */
	const u_int8_t family = par->family;
	struct ip_vs_iphdr iph;
	struct ip_vs_protocol *pp;
	struct ip_vs_conn *cp;
	bool match = true;

	if (data->bitmask == XT_IPVS_IPVS_PROPERTY) {
		match = skb->ipvs_property ^
			!!(data->invert & XT_IPVS_IPVS_PROPERTY);
		goto out;
	}

	/* other flags than XT_IPVS_IPVS_PROPERTY are set */
	if (!skb->ipvs_property) {
		match = false;
		goto out;
	}

	ip_vs_fill_iphdr(family, skb_network_header(skb), &iph);

	if (data->bitmask & XT_IPVS_PROTO)
		if ((iph.protocol == data->l4proto) ^
		    !(data->invert & XT_IPVS_PROTO)) {
			match = false;
			goto out;
		}

	pp = ip_vs_proto_get(iph.protocol);
	if (unlikely(!pp)) {
		match = false;
		goto out;
	}

	/*
	 * Check if the packet belongs to an existing entry
	 */
	cp = pp->conn_out_get(family, skb, pp, &iph, iph.len, 1 /* inverse */);
	if (unlikely(cp == NULL)) {
		match = false;
		goto out;
	}

	/*
	 * We found a connection, i.e. ct != 0, make sure to call
	 * __ip_vs_conn_put before returning.  In our case jump to out_put_con.
	 */

	if (data->bitmask & XT_IPVS_VPORT)
		if ((cp->vport == data->vport) ^
		    !(data->invert & XT_IPVS_VPORT)) {
			match = false;
			goto out_put_cp;
		}

	if (data->bitmask & XT_IPVS_VPORTCTL)
		if ((cp->control != NULL &&
		     cp->control->vport == data->vportctl) ^
		    !(data->invert & XT_IPVS_VPORTCTL)) {
			match = false;
			goto out_put_cp;
		}

	if (data->bitmask & XT_IPVS_DIR) {
		enum ip_conntrack_info ctinfo;
		struct nf_conn *ct = nf_ct_get(skb, &ctinfo);

		if (ct == NULL || nf_ct_is_untracked(ct)) {
			match = false;
			goto out_put_cp;
		}

		if ((ctinfo >= IP_CT_IS_REPLY) ^
		    !!(data->invert & XT_IPVS_DIR)) {
			match = false;
			goto out_put_cp;
		}
	}

	if (data->bitmask & XT_IPVS_METHOD)
		if (((cp->flags & IP_VS_CONN_F_FWD_MASK) == data->fwd_method) ^
		    !(data->invert & XT_IPVS_METHOD)) {
			match = false;
			goto out_put_cp;
		}

	if (data->bitmask & XT_IPVS_VADDR) {
		if (ipvs_mt_addrcmp(&cp->vaddr, &data->vaddr,
				    &data->vmask, family) ^
		    !(data->invert & XT_IPVS_VADDR)) {
			match = false;
			goto out_put_cp;
		}
	}

out_put_cp:
	__ip_vs_conn_put(cp);
out:
	pr_debug("match=%d\n", match);
	return match;
}

static int ipvs_mt_check(const struct xt_mtchk_param *par)
{
	if (par->family != NFPROTO_IPV4
#ifdef CONFIG_IP_VS_IPV6
	    && par->family != NFPROTO_IPV6
#endif
		) {
		pr_info("protocol family %u not supported\n", par->family);
		return -EINVAL;
	}

	return 0;
}

static struct xt_match xt_ipvs_mt_reg __read_mostly = {
	.name       = "ipvs",
	.revision   = 0,
	.family     = NFPROTO_UNSPEC,
	.match      = ipvs_mt,
	.checkentry = ipvs_mt_check,
	.matchsize  = XT_ALIGN(sizeof(struct xt_ipvs_mtinfo)),
	.me         = THIS_MODULE,
};

static int __init ipvs_mt_init(void)
{
	return xt_register_match(&xt_ipvs_mt_reg);
}

static void __exit ipvs_mt_exit(void)
{
	xt_unregister_match(&xt_ipvs_mt_reg);
}

module_init(ipvs_mt_init);
module_exit(ipvs_mt_exit);
