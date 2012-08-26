/* IRC extension for IP connection tracking, Version 1.21
 * (C) 2000-2002 by Harald Welte <laforge@gnumonks.org>
 * based on RR's ip_conntrack_ftp.c
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/skbuff.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/netfilter.h>
#include <linux/slab.h>

#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_conntrack_expect.h>
#include <net/netfilter/nf_conntrack_helper.h>
#include <linux/netfilter/nf_conntrack_irc.h>

#define MAX_PORTS 8
static unsigned short ports[MAX_PORTS];
static unsigned int ports_c;
static unsigned int max_dcc_channels = 8;
static unsigned int dcc_timeout __read_mostly = 300;
/* This is slow, but it's simple. --RR */
static char *irc_buffer;
static DEFINE_SPINLOCK(irc_buffer_lock);

unsigned int (*nf_nat_irc_hook)(struct sk_buff *skb,
				enum ip_conntrack_info ctinfo,
				unsigned int protoff,
				unsigned int matchoff,
				unsigned int matchlen,
				struct nf_conntrack_expect *exp) __read_mostly;
EXPORT_SYMBOL_GPL(nf_nat_irc_hook);

MODULE_AUTHOR("Harald Welte <laforge@netfilter.org>");
MODULE_DESCRIPTION("IRC (DCC) connection tracking helper");
MODULE_LICENSE("GPL");
MODULE_ALIAS("ip_conntrack_irc");
MODULE_ALIAS_NFCT_HELPER("irc");

module_param_array(ports, ushort, &ports_c, 0400);
MODULE_PARM_DESC(ports, "port numbers of IRC servers");
module_param(max_dcc_channels, uint, 0400);
MODULE_PARM_DESC(max_dcc_channels, "max number of expected DCC channels per "
				   "IRC session");
module_param(dcc_timeout, uint, 0400);
MODULE_PARM_DESC(dcc_timeout, "timeout on for unestablished DCC channels");

static const char *const dccprotos[] = {
	"SEND ", "CHAT ", "MOVE ", "TSEND ", "SCHAT "
};

#define MINMATCHLEN	5

/* tries to get the ip_addr and port out of a dcc command
 * return value: -1 on failure, 0 on success
 *	data		pointer to first byte of DCC command data
 *	data_end	pointer to last byte of dcc command data
 *	ip		returns parsed ip of dcc command
 *	port		returns parsed port of dcc command
 *	ad_beg_p	returns pointer to first byte of addr data
 *	ad_end_p	returns pointer to last byte of addr data
 */
static int parse_dcc(char *data, const char *data_end, __be32 *ip,
		     u_int16_t *port, char **ad_beg_p, char **ad_end_p)
{
	char *tmp;

	/* at least 12: "AAAAAAAA P\1\n" */
	while (*data++ != ' ')
		if (data > data_end - 12)
			return -1;

	/* Make sure we have a newline character within the packet boundaries
	 * because simple_strtoul parses until the first invalid character. */
	for (tmp = data; tmp <= data_end; tmp++)
		if (*tmp == '\n')
			break;
	if (tmp > data_end || *tmp != '\n')
		return -1;

	*ad_beg_p = data;
	*ip = cpu_to_be32(simple_strtoul(data, &data, 10));

	/* skip blanks between ip and port */
	while (*data == ' ') {
		if (data >= data_end)
			return -1;
		data++;
	}

	*port = simple_strtoul(data, &data, 10);
	*ad_end_p = data;

	return 0;
}

static int help(struct sk_buff *skb, unsigned int protoff,
		struct nf_conn *ct, enum ip_conntrack_info ctinfo)
{
	unsigned int dataoff;
	const struct iphdr *iph;
	const struct tcphdr *th;
	struct tcphdr _tcph;
	const char *data_limit;
	char *data, *ib_ptr;
	int dir = CTINFO2DIR(ctinfo);
	struct nf_conntrack_expect *exp;
	struct nf_conntrack_tuple *tuple;
	__be32 dcc_ip;
	u_int16_t dcc_port;
	__be16 port;
	int i, ret = NF_ACCEPT;
	char *addr_beg_p, *addr_end_p;
	typeof(nf_nat_irc_hook) nf_nat_irc;

	/* If packet is coming from IRC server */
	if (dir == IP_CT_DIR_REPLY)
		return NF_ACCEPT;

	/* Until there's been traffic both ways, don't look in packets. */
	if (ctinfo != IP_CT_ESTABLISHED && ctinfo != IP_CT_ESTABLISHED_REPLY)
		return NF_ACCEPT;

	/* Not a full tcp header? */
	th = skb_header_pointer(skb, protoff, sizeof(_tcph), &_tcph);
	if (th == NULL)
		return NF_ACCEPT;

	/* No data? */
	dataoff = protoff + th->doff*4;
	if (dataoff >= skb->len)
		return NF_ACCEPT;

	spin_lock_bh(&irc_buffer_lock);
	ib_ptr = skb_header_pointer(skb, dataoff, skb->len - dataoff,
				    irc_buffer);
	BUG_ON(ib_ptr == NULL);

	data = ib_ptr;
	data_limit = ib_ptr + skb->len - dataoff;

	/* strlen("\1DCC SENT t AAAAAAAA P\1\n")=24
	 * 5+MINMATCHLEN+strlen("t AAAAAAAA P\1\n")=14 */
	while (data < data_limit - (19 + MINMATCHLEN)) {
		if (memcmp(data, "\1DCC ", 5)) {
			data++;
			continue;
		}
		data += 5;
		/* we have at least (19+MINMATCHLEN)-5 bytes valid data left */

		iph = ip_hdr(skb);
		pr_debug("DCC found in master %pI4:%u %pI4:%u\n",
			 &iph->saddr, ntohs(th->source),
			 &iph->daddr, ntohs(th->dest));

		for (i = 0; i < ARRAY_SIZE(dccprotos); i++) {
			if (memcmp(data, dccprotos[i], strlen(dccprotos[i]))) {
				/* no match */
				continue;
			}
			data += strlen(dccprotos[i]);
			pr_debug("DCC %s detected\n", dccprotos[i]);

			/* we have at least
			 * (19+MINMATCHLEN)-5-dccprotos[i].matchlen bytes valid
			 * data left (== 14/13 bytes) */
			if (parse_dcc(data, data_limit, &dcc_ip,
				       &dcc_port, &addr_beg_p, &addr_end_p)) {
				pr_debug("unable to parse dcc command\n");
				continue;
			}

			pr_debug("DCC bound ip/port: %pI4:%u\n",
				 &dcc_ip, dcc_port);

			/* dcc_ip can be the internal OR external (NAT'ed) IP */
			tuple = &ct->tuplehash[dir].tuple;
			if (tuple->src.u3.ip != dcc_ip &&
			    tuple->dst.u3.ip != dcc_ip) {
				net_warn_ratelimited("Forged DCC command from %pI4: %pI4:%u\n",
						     &tuple->src.u3.ip,
						     &dcc_ip, dcc_port);
				continue;
			}

			exp = nf_ct_expect_alloc(ct);
			if (exp == NULL) {
				ret = NF_DROP;
				goto out;
			}
			tuple = &ct->tuplehash[!dir].tuple;
			port = htons(dcc_port);
			nf_ct_expect_init(exp, NF_CT_EXPECT_CLASS_DEFAULT,
					  tuple->src.l3num,
					  NULL, &tuple->dst.u3,
					  IPPROTO_TCP, NULL, &port);

			nf_nat_irc = rcu_dereference(nf_nat_irc_hook);
			if (nf_nat_irc && ct->status & IPS_NAT_MASK)
				ret = nf_nat_irc(skb, ctinfo, protoff,
						 addr_beg_p - ib_ptr,
						 addr_end_p - addr_beg_p,
						 exp);
			else if (nf_ct_expect_related(exp) != 0)
				ret = NF_DROP;
			nf_ct_expect_put(exp);
			goto out;
		}
	}
 out:
	spin_unlock_bh(&irc_buffer_lock);
	return ret;
}

static struct nf_conntrack_helper irc[MAX_PORTS] __read_mostly;
static struct nf_conntrack_expect_policy irc_exp_policy;

static void nf_conntrack_irc_fini(void);

static int __init nf_conntrack_irc_init(void)
{
	int i, ret;

	if (max_dcc_channels < 1) {
		printk(KERN_ERR "nf_ct_irc: max_dcc_channels must not be zero\n");
		return -EINVAL;
	}

	irc_exp_policy.max_expected = max_dcc_channels;
	irc_exp_policy.timeout = dcc_timeout;

	irc_buffer = kmalloc(65536, GFP_KERNEL);
	if (!irc_buffer)
		return -ENOMEM;

	/* If no port given, default to standard irc port */
	if (ports_c == 0)
		ports[ports_c++] = IRC_PORT;

	for (i = 0; i < ports_c; i++) {
		irc[i].tuple.src.l3num = AF_INET;
		irc[i].tuple.src.u.tcp.port = htons(ports[i]);
		irc[i].tuple.dst.protonum = IPPROTO_TCP;
		irc[i].expect_policy = &irc_exp_policy;
		irc[i].me = THIS_MODULE;
		irc[i].help = help;

		if (ports[i] == IRC_PORT)
			sprintf(irc[i].name, "irc");
		else
			sprintf(irc[i].name, "irc-%u", i);

		ret = nf_conntrack_helper_register(&irc[i]);
		if (ret) {
			printk(KERN_ERR "nf_ct_irc: failed to register helper "
			       "for pf: %u port: %u\n",
			       irc[i].tuple.src.l3num, ports[i]);
			nf_conntrack_irc_fini();
			return ret;
		}
	}
	return 0;
}

/* This function is intentionally _NOT_ defined as __exit, because
 * it is needed by the init function */
static void nf_conntrack_irc_fini(void)
{
	int i;

	for (i = 0; i < ports_c; i++)
		nf_conntrack_helper_unregister(&irc[i]);
	kfree(irc_buffer);
}

module_init(nf_conntrack_irc_init);
module_exit(nf_conntrack_irc_fini);
