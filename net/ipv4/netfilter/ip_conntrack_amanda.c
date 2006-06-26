/* Amanda extension for IP connection tracking, Version 0.2
 * (C) 2002 by Brian J. Murrell <netfilter@interlinx.bc.ca>
 * based on HW's ip_conntrack_irc.c as well as other modules
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 *
 *	Module load syntax:
 * 	insmod ip_conntrack_amanda.o [master_timeout=n]
 *	
 *	Where master_timeout is the timeout (in seconds) of the master
 *	connection (port 10080).  This defaults to 5 minutes but if
 *	your clients take longer than 5 minutes to do their work
 *	before getting back to the Amanda server, you can increase
 *	this value.
 *
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/textsearch.h>
#include <linux/skbuff.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/udp.h>

#include <linux/netfilter.h>
#include <linux/netfilter_ipv4/ip_conntrack_helper.h>
#include <linux/netfilter_ipv4/ip_conntrack_amanda.h>

static unsigned int master_timeout = 300;
static char *ts_algo = "kmp";

MODULE_AUTHOR("Brian J. Murrell <netfilter@interlinx.bc.ca>");
MODULE_DESCRIPTION("Amanda connection tracking module");
MODULE_LICENSE("GPL");
module_param(master_timeout, uint, 0600);
MODULE_PARM_DESC(master_timeout, "timeout for the master connection");
module_param(ts_algo, charp, 0400);
MODULE_PARM_DESC(ts_algo, "textsearch algorithm to use (default kmp)");

unsigned int (*ip_nat_amanda_hook)(struct sk_buff **pskb,
				   enum ip_conntrack_info ctinfo,
				   unsigned int matchoff,
				   unsigned int matchlen,
				   struct ip_conntrack_expect *exp);
EXPORT_SYMBOL_GPL(ip_nat_amanda_hook);

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
} search[] = {
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

static int help(struct sk_buff **pskb,
                struct ip_conntrack *ct, enum ip_conntrack_info ctinfo)
{
	struct ts_state ts;
	struct ip_conntrack_expect *exp;
	unsigned int dataoff, start, stop, off, i;
	char pbuf[sizeof("65535")], *tmp;
	u_int16_t port, len;
	int ret = NF_ACCEPT;

	/* Only look at packets from the Amanda server */
	if (CTINFO2DIR(ctinfo) == IP_CT_DIR_ORIGINAL)
		return NF_ACCEPT;

	/* increase the UDP timeout of the master connection as replies from
	 * Amanda clients to the server can be quite delayed */
	ip_ct_refresh(ct, *pskb, master_timeout * HZ);

	/* No data? */
	dataoff = (*pskb)->nh.iph->ihl*4 + sizeof(struct udphdr);
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

		port = simple_strtoul(pbuf, &tmp, 10);
		len = tmp - pbuf;
		if (port == 0 || len > 5)
			break;

		exp = ip_conntrack_expect_alloc(ct);
		if (exp == NULL) {
			ret = NF_DROP;
			goto out;
		}

		exp->expectfn = NULL;
		exp->flags = 0;

		exp->tuple.src.ip = ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple.src.ip;
		exp->tuple.src.u.tcp.port = 0;
		exp->tuple.dst.ip = ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple.dst.ip;
		exp->tuple.dst.protonum = IPPROTO_TCP;
		exp->tuple.dst.u.tcp.port = htons(port);

		exp->mask.src.ip = 0xFFFFFFFF;
		exp->mask.src.u.tcp.port = 0;
		exp->mask.dst.ip = 0xFFFFFFFF;
		exp->mask.dst.protonum = 0xFF;
		exp->mask.dst.u.tcp.port = 0xFFFF;

		if (ip_nat_amanda_hook)
			ret = ip_nat_amanda_hook(pskb, ctinfo, off - dataoff,
						 len, exp);
		else if (ip_conntrack_expect_related(exp) != 0)
			ret = NF_DROP;
		ip_conntrack_expect_put(exp);
	}

out:
	return ret;
}

static struct ip_conntrack_helper amanda_helper = {
	.max_expected = 3,
	.timeout = 180,
	.me = THIS_MODULE,
	.help = help,
	.name = "amanda",

	.tuple = { .src = { .u = { __constant_htons(10080) } },
		   .dst = { .protonum = IPPROTO_UDP },
	},
	.mask = { .src = { .u = { 0xFFFF } },
		 .dst = { .protonum = 0xFF },
	},
};

static void __exit ip_conntrack_amanda_fini(void)
{
	int i;

	ip_conntrack_helper_unregister(&amanda_helper);
	for (i = 0; i < ARRAY_SIZE(search); i++)
		textsearch_destroy(search[i].ts);
}

static int __init ip_conntrack_amanda_init(void)
{
	int ret, i;

	ret = -ENOMEM;
	for (i = 0; i < ARRAY_SIZE(search); i++) {
		search[i].ts = textsearch_prepare(ts_algo, search[i].string,
						  search[i].len,
						  GFP_KERNEL, TS_AUTOLOAD);
		if (search[i].ts == NULL)
			goto err;
	}
	ret = ip_conntrack_helper_register(&amanda_helper);
	if (ret < 0)
		goto err;
	return 0;

err:
	for (; i >= 0; i--) {
		if (search[i].ts)
			textsearch_destroy(search[i].ts);
	}
	return ret;
}

module_init(ip_conntrack_amanda_init);
module_exit(ip_conntrack_amanda_fini);
