/* Amanda extension for IP connection tracking
 *
 * (C) 2002 by Brian J. Murrell <netfilter@interlinx.bc.ca>
 * based on HW's ip_conntrack_irc.c as well as other modules
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/textsearch.h>
#include <linux/skbuff.h>
#include <linux/in.h>
#include <linux/udp.h>
#include <linux/netfilter.h>

#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_conntrack_expect.h>
#include <net/netfilter/nf_conntrack_ecache.h>
#include <net/netfilter/nf_conntrack_helper.h>
#include <linux/netfilter/nf_conntrack_amanda.h>

static unsigned int master_timeout __read_mostly = 300;
static char *ts_algo = "kmp";

MODULE_AUTHOR("Brian J. Murrell <netfilter@interlinx.bc.ca>");
MODULE_DESCRIPTION("Amanda connection tracking module");
MODULE_LICENSE("GPL");
MODULE_ALIAS("ip_conntrack_amanda");

module_param(master_timeout, uint, 0600);
MODULE_PARM_DESC(master_timeout, "timeout for the master connection");
module_param(ts_algo, charp, 0400);
MODULE_PARM_DESC(ts_algo, "textsearch algorithm to use (default kmp)");

unsigned int (*nf_nat_amanda_hook)(struct sk_buff **pskb,
				   enum ip_conntrack_info ctinfo,
				   unsigned int matchoff,
				   unsigned int matchlen,
				   struct nf_conntrack_expect *exp)
				   __read_mostly;
EXPORT_SYMBOL_GPL(nf_nat_amanda_hook);

enum amanda_strings {
	SEARCH_CONNECT,
	SEARCH_NEWLINE,
	SEARCH_DATA,
	SEARCH_MESG,
	SEARCH_INDEX,
};

static struct {
	char			*string;
	size_t			len;
	struct ts_config	*ts;
} search[] __read_mostly = {
	[SEARCH_CONNECT] = {
		.string	= "CONNECT ",
		.len	= 8,
	},
	[SEARCH_NEWLINE] = {
		.string	= "\n",
		.len	= 1,
	},
	[SEARCH_DATA] = {
		.string	= "DATA ",
		.len	= 5,
	},
	[SEARCH_MESG] = {
		.string	= "MESG ",
		.len	= 5,
	},
	[SEARCH_INDEX] = {
		.string = "INDEX ",
		.len	= 6,
	},
};

static int amanda_help(struct sk_buff **pskb,
		       unsigned int protoff,
		       struct nf_conn *ct,
		       enum ip_conntrack_info ctinfo)
{
	struct ts_state ts;
	struct nf_conntrack_expect *exp;
	struct nf_conntrack_tuple *tuple;
	unsigned int dataoff, start, stop, off, i;
	char pbuf[sizeof("65535")], *tmp;
	u_int16_t len;
	__be16 port;
	int family = ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple.src.l3num;
	int ret = NF_ACCEPT;
	typeof(nf_nat_amanda_hook) nf_nat_amanda;

	/* Only look at packets from the Amanda server */
	if (CTINFO2DIR(ctinfo) == IP_CT_DIR_ORIGINAL)
		return NF_ACCEPT;

	/* increase the UDP timeout of the master connection as replies from
	 * Amanda clients to the server can be quite delayed */
	nf_ct_refresh(ct, *pskb, master_timeout * HZ);

	/* No data? */
	dataoff = protoff + sizeof(struct udphdr);
	if (dataoff >= (*pskb)->len) {
		if (net_ratelimit())
			printk("amanda_help: skblen = %u\n", (*pskb)->len);
		return NF_ACCEPT;
	}

	memset(&ts, 0, sizeof(ts));
	start = skb_find_text(*pskb, dataoff, (*pskb)->len,
			      search[SEARCH_CONNECT].ts, &ts);
	if (start == UINT_MAX)
		goto out;
	start += dataoff + search[SEARCH_CONNECT].len;

	memset(&ts, 0, sizeof(ts));
	stop = skb_find_text(*pskb, start, (*pskb)->len,
			     search[SEARCH_NEWLINE].ts, &ts);
	if (stop == UINT_MAX)
		goto out;
	stop += start;

	for (i = SEARCH_DATA; i <= SEARCH_INDEX; i++) {
		memset(&ts, 0, sizeof(ts));
		off = skb_find_text(*pskb, start, stop, search[i].ts, &ts);
		if (off == UINT_MAX)
			continue;
		off += start + search[i].len;

		len = min_t(unsigned int, sizeof(pbuf) - 1, stop - off);
		if (skb_copy_bits(*pskb, off, pbuf, len))
			break;
		pbuf[len] = '\0';

		port = htons(simple_strtoul(pbuf, &tmp, 10));
		len = tmp - pbuf;
		if (port == 0 || len > 5)
			break;

		exp = nf_conntrack_expect_alloc(ct);
		if (exp == NULL) {
			ret = NF_DROP;
			goto out;
		}
		tuple = &ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple;
		nf_conntrack_expect_init(exp, family,
					 &tuple->src.u3, &tuple->dst.u3,
					 IPPROTO_TCP, NULL, &port);

		nf_nat_amanda = rcu_dereference(nf_nat_amanda_hook);
		if (nf_nat_amanda && ct->status & IPS_NAT_MASK)
			ret = nf_nat_amanda(pskb, ctinfo, off - dataoff,
					    len, exp);
		else if (nf_conntrack_expect_related(exp) != 0)
			ret = NF_DROP;
		nf_conntrack_expect_put(exp);
	}

out:
	return ret;
}

static struct nf_conntrack_helper amanda_helper[2] __read_mostly = {
	{
		.name			= "amanda",
		.max_expected		= 3,
		.timeout		= 180,
		.me			= THIS_MODULE,
		.help			= amanda_help,
		.tuple.src.l3num	= AF_INET,
		.tuple.src.u.udp.port	= __constant_htons(10080),
		.tuple.dst.protonum	= IPPROTO_UDP,
		.mask.src.l3num		= 0xFFFF,
		.mask.src.u.udp.port	= __constant_htons(0xFFFF),
		.mask.dst.protonum	= 0xFF,
	},
	{
		.name			= "amanda",
		.max_expected		= 3,
		.timeout		= 180,
		.me			= THIS_MODULE,
		.help			= amanda_help,
		.tuple.src.l3num	= AF_INET6,
		.tuple.src.u.udp.port	= __constant_htons(10080),
		.tuple.dst.protonum	= IPPROTO_UDP,
		.mask.src.l3num		= 0xFFFF,
		.mask.src.u.udp.port	= __constant_htons(0xFFFF),
		.mask.dst.protonum	= 0xFF,
	},
};

static void __exit nf_conntrack_amanda_fini(void)
{
	int i;

	nf_conntrack_helper_unregister(&amanda_helper[0]);
	nf_conntrack_helper_unregister(&amanda_helper[1]);
	for (i = 0; i < ARRAY_SIZE(search); i++)
		textsearch_destroy(search[i].ts);
}

static int __init nf_conntrack_amanda_init(void)
{
	int ret, i;

	ret = -ENOMEM;
	for (i = 0; i < ARRAY_SIZE(search); i++) {
		search[i].ts = textsearch_prepare(ts_algo, search[i].string,
						  search[i].len,
						  GFP_KERNEL, TS_AUTOLOAD);
		if (search[i].ts == NULL)
			goto err1;
	}
	ret = nf_conntrack_helper_register(&amanda_helper[0]);
	if (ret < 0)
		goto err1;
	ret = nf_conntrack_helper_register(&amanda_helper[1]);
	if (ret < 0)
		goto err2;
	return 0;

err2:
	nf_conntrack_helper_unregister(&amanda_helper[0]);
err1:
	for (; i >= 0; i--) {
		if (search[i].ts)
			textsearch_destroy(search[i].ts);
	}
	return ret;
}

module_init(nf_conntrack_amanda_init);
module_exit(nf_conntrack_amanda_fini);
