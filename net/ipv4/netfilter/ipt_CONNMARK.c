/* This kernel module is used to modify the connection mark values, or
 * to optionally restore the skb nfmark from the connection mark
 *
 * Copyright (C) 2002,2004 MARA Systems AB <http://www.marasystems.com>
 * by Henrik Nordstrom <hno@marasystems.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/ip.h>
#include <net/checksum.h>

MODULE_AUTHOR("Henrik Nordstrom <hno@marasytems.com>");
MODULE_DESCRIPTION("IP tables CONNMARK matching module");
MODULE_LICENSE("GPL");

#include <linux/netfilter_ipv4/ip_tables.h>
#include <linux/netfilter_ipv4/ipt_CONNMARK.h>
#include <linux/netfilter_ipv4/ip_conntrack.h>

static unsigned int
target(struct sk_buff **pskb,
       const struct net_device *in,
       const struct net_device *out,
       unsigned int hooknum,
       const void *targinfo,
       void *userinfo)
{
	const struct ipt_connmark_target_info *markinfo = targinfo;
	u_int32_t diff;
	u_int32_t nfmark;
	u_int32_t newmark;

	enum ip_conntrack_info ctinfo;
	struct ip_conntrack *ct = ip_conntrack_get((*pskb), &ctinfo);
	if (ct) {
	    switch(markinfo->mode) {
	    case IPT_CONNMARK_SET:
		newmark = (ct->mark & ~markinfo->mask) | markinfo->mark;
		if (newmark != ct->mark)
		    ct->mark = newmark;
		break;
	    case IPT_CONNMARK_SAVE:
		newmark = (ct->mark & ~markinfo->mask) | ((*pskb)->nfmark & markinfo->mask);
		if (ct->mark != newmark)
		    ct->mark = newmark;
		break;
	    case IPT_CONNMARK_RESTORE:
		nfmark = (*pskb)->nfmark;
		diff = (ct->mark ^ nfmark) & markinfo->mask;
		if (diff != 0)
		    (*pskb)->nfmark = nfmark ^ diff;
		break;
	    }
	}

	return IPT_CONTINUE;
}

static int
checkentry(const char *tablename,
	   const struct ipt_entry *e,
	   void *targinfo,
	   unsigned int targinfosize,
	   unsigned int hook_mask)
{
	struct ipt_connmark_target_info *matchinfo = targinfo;
	if (targinfosize != IPT_ALIGN(sizeof(struct ipt_connmark_target_info))) {
		printk(KERN_WARNING "CONNMARK: targinfosize %u != %Zu\n",
		       targinfosize,
		       IPT_ALIGN(sizeof(struct ipt_connmark_target_info)));
		return 0;
	}

	if (matchinfo->mode == IPT_CONNMARK_RESTORE) {
	    if (strcmp(tablename, "mangle") != 0) {
		    printk(KERN_WARNING "CONNMARK: restore can only be called from \"mangle\" table, not \"%s\"\n", tablename);
		    return 0;
	    }
	}

	if (matchinfo->mark > 0xffffffff || matchinfo->mask > 0xffffffff) {
		printk(KERN_WARNING "CONNMARK: Only supports 32bit mark\n");
		return 0;
	}

	return 1;
}

static struct ipt_target ipt_connmark_reg = {
	.name = "CONNMARK",
	.target = &target,
	.checkentry = &checkentry,
	.me = THIS_MODULE
};

static int __init init(void)
{
	need_ip_conntrack();
	return ipt_register_target(&ipt_connmark_reg);
}

static void __exit fini(void)
{
	ipt_unregister_target(&ipt_connmark_reg);
}

module_init(init);
module_exit(fini);
