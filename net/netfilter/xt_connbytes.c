/* Kernel module to match connection tracking byte counter.
 * GPL (C) 2002 Martin Devera (devik@cdi.cz).
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/module.h>
#include <linux/bitops.h>
#include <linux/skbuff.h>
#include <linux/math64.h>
#include <linux/netfilter/x_tables.h>
#include <linux/netfilter/xt_connbytes.h>
#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_conntrack_acct.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Harald Welte <laforge@netfilter.org>");
MODULE_DESCRIPTION("Xtables: Number of packets/bytes per connection matching");
MODULE_ALIAS("ipt_connbytes");
MODULE_ALIAS("ip6t_connbytes");

static bool
connbytes_mt(const struct sk_buff *skb, struct xt_action_param *par)
{
	const struct xt_connbytes_info *sinfo = par->matchinfo;
	const struct nf_conn *ct;
	enum ip_conntrack_info ctinfo;
	u_int64_t what = 0;	/* initialize to make gcc happy */
	u_int64_t bytes = 0;
	u_int64_t pkts = 0;
	const struct nf_conn_counter *counters;

	ct = nf_ct_get(skb, &ctinfo);
	if (!ct)
		return false;

	counters = nf_conn_acct_find(ct);
	if (!counters)
		return false;

	switch (sinfo->what) {
	case XT_CONNBYTES_PKTS:
		switch (sinfo->direction) {
		case XT_CONNBYTES_DIR_ORIGINAL:
			what = atomic64_read(&counters[IP_CT_DIR_ORIGINAL].packets);
			break;
		case XT_CONNBYTES_DIR_REPLY:
			what = atomic64_read(&counters[IP_CT_DIR_REPLY].packets);
			break;
		case XT_CONNBYTES_DIR_BOTH:
			what = atomic64_read(&counters[IP_CT_DIR_ORIGINAL].packets);
			what += atomic64_read(&counters[IP_CT_DIR_REPLY].packets);
			break;
		}
		break;
	case XT_CONNBYTES_BYTES:
		switch (sinfo->direction) {
		case XT_CONNBYTES_DIR_ORIGINAL:
			what = atomic64_read(&counters[IP_CT_DIR_ORIGINAL].bytes);
			break;
		case XT_CONNBYTES_DIR_REPLY:
			what = atomic64_read(&counters[IP_CT_DIR_REPLY].bytes);
			break;
		case XT_CONNBYTES_DIR_BOTH:
			what = atomic64_read(&counters[IP_CT_DIR_ORIGINAL].bytes);
			what += atomic64_read(&counters[IP_CT_DIR_REPLY].bytes);
			break;
		}
		break;
	case XT_CONNBYTES_AVGPKT:
		switch (sinfo->direction) {
		case XT_CONNBYTES_DIR_ORIGINAL:
			bytes = atomic64_read(&counters[IP_CT_DIR_ORIGINAL].bytes);
			pkts  = atomic64_read(&counters[IP_CT_DIR_ORIGINAL].packets);
			break;
		case XT_CONNBYTES_DIR_REPLY:
			bytes = atomic64_read(&counters[IP_CT_DIR_REPLY].bytes);
			pkts  = atomic64_read(&counters[IP_CT_DIR_REPLY].packets);
			break;
		case XT_CONNBYTES_DIR_BOTH:
			bytes = atomic64_read(&counters[IP_CT_DIR_ORIGINAL].bytes) +
				atomic64_read(&counters[IP_CT_DIR_REPLY].bytes);
			pkts  = atomic64_read(&counters[IP_CT_DIR_ORIGINAL].packets) +
				atomic64_read(&counters[IP_CT_DIR_REPLY].packets);
			break;
		}
		if (pkts != 0)
			what = div64_u64(bytes, pkts);
		break;
	}

	if (sinfo->count.to)
		return what <= sinfo->count.to && what >= sinfo->count.from;
	else
		return what >= sinfo->count.from;
}

static int connbytes_mt_check(const struct xt_mtchk_param *par)
{
	const struct xt_connbytes_info *sinfo = par->matchinfo;
	int ret;

	if (sinfo->what != XT_CONNBYTES_PKTS &&
	    sinfo->what != XT_CONNBYTES_BYTES &&
	    sinfo->what != XT_CONNBYTES_AVGPKT)
		return -EINVAL;

	if (sinfo->direction != XT_CONNBYTES_DIR_ORIGINAL &&
	    sinfo->direction != XT_CONNBYTES_DIR_REPLY &&
	    sinfo->direction != XT_CONNBYTES_DIR_BOTH)
		return -EINVAL;

	ret = nf_ct_l3proto_try_module_get(par->family);
	if (ret < 0)
		pr_info("cannot load conntrack support for proto=%u\n",
			par->family);

	/*
	 * This filter cannot function correctly unless connection tracking
	 * accounting is enabled, so complain in the hope that someone notices.
	 */
	if (!nf_ct_acct_enabled(par->net)) {
		pr_warning("Forcing CT accounting to be enabled\n");
		nf_ct_set_acct(par->net, true);
	}

	return ret;
}

static void connbytes_mt_destroy(const struct xt_mtdtor_param *par)
{
	nf_ct_l3proto_module_put(par->family);
}

static struct xt_match connbytes_mt_reg __read_mostly = {
	.name       = "connbytes",
	.revision   = 0,
	.family     = NFPROTO_UNSPEC,
	.checkentry = connbytes_mt_check,
	.match      = connbytes_mt,
	.destroy    = connbytes_mt_destroy,
	.matchsize  = sizeof(struct xt_connbytes_info),
	.me         = THIS_MODULE,
};

static int __init connbytes_mt_init(void)
{
	return xt_register_match(&connbytes_mt_reg);
}

static void __exit connbytes_mt_exit(void)
{
	xt_unregister_match(&connbytes_mt_reg);
}

module_init(connbytes_mt_init);
module_exit(connbytes_mt_exit);
