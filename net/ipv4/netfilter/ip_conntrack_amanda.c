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
#include <linux/netfilter.h>
#include <linux/ip.h>
#include <linux/moduleparam.h>
#include <net/checksum.h>
#include <net/udp.h>

#include <linux/netfilter_ipv4/ip_conntrack_helper.h>
#include <linux/netfilter_ipv4/ip_conntrack_amanda.h>

static unsigned int master_timeout = 300;

MODULE_AUTHOR("Brian J. Murrell <netfilter@interlinx.bc.ca>");
MODULE_DESCRIPTION("Amanda connection tracking module");
MODULE_LICENSE("GPL");
module_param(master_timeout, int, 0600);
MODULE_PARM_DESC(master_timeout, "timeout for the master connection");

static const char *conns[] = { "DATA ", "MESG ", "INDEX " };

/* This is slow, but it's simple. --RR */
static char *amanda_buffer;
static DEFINE_SPINLOCK(amanda_buffer_lock);

unsigned int (*ip_nat_amanda_hook)(struct sk_buff **pskb,
				   enum ip_conntrack_info ctinfo,
				   unsigned int matchoff,
				   unsigned int matchlen,
				   struct ip_conntrack_expect *exp);
EXPORT_SYMBOL_GPL(ip_nat_amanda_hook);

static int help(struct sk_buff **pskb,
                struct ip_conntrack *ct, enum ip_conntrack_info ctinfo)
{
	struct ip_conntrack_expect *exp;
	char *data, *data_limit, *tmp;
	unsigned int dataoff, i;
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

	spin_lock_bh(&amanda_buffer_lock);
	skb_copy_bits(*pskb, dataoff, amanda_buffer, (*pskb)->len - dataoff);
	data = amanda_buffer;
	data_limit = amanda_buffer + (*pskb)->len - dataoff;
	*data_limit = '\0';

	/* Search for the CONNECT string */
	data = strstr(data, "CONNECT ");
	if (!data)
		goto out;
	data += strlen("CONNECT ");

	/* Only search first line. */	
	if ((tmp = strchr(data, '\n')))
		*tmp = '\0';

	for (i = 0; i < ARRAY_SIZE(conns); i++) {
		char *match = strstr(data, conns[i]);
		if (!match)
			continue;
		tmp = data = match + strlen(conns[i]);
		port = simple_strtoul(data, &data, 10);
		len = data - tmp;
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
			ret = ip_nat_amanda_hook(pskb, ctinfo,
						 tmp - amanda_buffer,
						 len, exp);
		else if (ip_conntrack_expect_related(exp) != 0)
			ret = NF_DROP;
		ip_conntrack_expect_put(exp);
	}

out:
	spin_unlock_bh(&amanda_buffer_lock);
	return ret;
}

static struct ip_conntrack_helper amanda_helper = {
	.max_expected = ARRAY_SIZE(conns),
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

static void __exit fini(void)
{
	ip_conntrack_helper_unregister(&amanda_helper);
	kfree(amanda_buffer);
}

static int __init init(void)
{
	int ret;

	amanda_buffer = kmalloc(65536, GFP_KERNEL);
	if (!amanda_buffer)
		return -ENOMEM;

	ret = ip_conntrack_helper_register(&amanda_helper);
	if (ret < 0) {
		kfree(amanda_buffer);
		return ret;
	}
	return 0;


}

module_init(init);
module_exit(fini);
