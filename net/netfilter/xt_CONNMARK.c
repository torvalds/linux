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
MODULE_ALIAS("ipt_CONNMARK");

#include <linux/netfilter/x_tables.h>
#include <linux/netfilter/xt_CONNMARK.h>
#include <net/netfilter/nf_conntrack_compat.h>

static unsigned int
target(struct sk_buff **pskb,
       const struct net_device *in,
       const struct net_device *out,
       unsigned int hooknum,
       const struct xt_target *target,
       const void *targinfo,
       void *userinfo)
{
	const struct xt_connmark_target_info *markinfo = targinfo;
	u_int32_t diff;
	u_int32_t nfmark;
	u_int32_t newmark;
	u_int32_t ctinfo;
	u_int32_t *ctmark = nf_ct_get_mark(*pskb, &ctinfo);

	if (ctmark) {
	    switch(markinfo->mode) {
	    case XT_CONNMARK_SET:
		newmark = (*ctmark & ~markinfo->mask) | markinfo->mark;
		if (newmark != *ctmark)
		    *ctmark = newmark;
		break;
	    case XT_CONNMARK_SAVE:
		newmark = (*ctmark & ~markinfo->mask) | ((*pskb)->nfmark & markinfo->mask);
		if (*ctmark != newmark)
		    *ctmark = newmark;
		break;
	    case XT_CONNMARK_RESTORE:
		nfmark = (*pskb)->nfmark;
		diff = (*ctmark ^ nfmark) & markinfo->mask;
		if (diff != 0)
		    (*pskb)->nfmark = nfmark ^ diff;
		break;
	    }
	}

	return XT_CONTINUE;
}

static int
checkentry(const char *tablename,
	   const void *entry,
	   const struct xt_target *target,
	   void *targinfo,
	   unsigned int targinfosize,
	   unsigned int hook_mask)
{
	struct xt_connmark_target_info *matchinfo = targinfo;

	if (matchinfo->mode == XT_CONNMARK_RESTORE) {
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

static struct xt_target connmark_reg = {
	.name		= "CONNMARK",
	.target		= target,
	.targetsize	= sizeof(struct xt_connmark_target_info),
	.checkentry	= checkentry,
	.family		= AF_INET,
	.me		= THIS_MODULE
};

static struct xt_target connmark6_reg = {
	.name		= "CONNMARK",
	.target		= target,
	.targetsize	= sizeof(struct xt_connmark_target_info),
	.checkentry	= checkentry,
	.family		= AF_INET6,
	.me		= THIS_MODULE
};

static int __init xt_connmark_init(void)
{
	int ret;

	need_conntrack();

	ret = xt_register_target(&connmark_reg);
	if (ret)
		return ret;

	ret = xt_register_target(&connmark6_reg);
	if (ret)
		xt_unregister_target(&connmark_reg);

	return ret;
}

static void __exit xt_connmark_fini(void)
{
	xt_unregister_target(&connmark_reg);
	xt_unregister_target(&connmark6_reg);
}

module_init(xt_connmark_init);
module_exit(xt_connmark_fini);
