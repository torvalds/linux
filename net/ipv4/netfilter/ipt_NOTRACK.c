/* This is a module which is used for setting up fake conntracks
 * on packets so that they are not seen by the conntrack/NAT code.
 */
#include <linux/module.h>
#include <linux/skbuff.h>

#include <linux/netfilter_ipv4/ip_tables.h>
#include <linux/netfilter_ipv4/ip_conntrack.h>

static unsigned int
target(struct sk_buff **pskb,
       const struct net_device *in,
       const struct net_device *out,
       unsigned int hooknum,
       const void *targinfo,
       void *userinfo)
{
	/* Previously seen (loopback)? Ignore. */
	if ((*pskb)->nfct != NULL)
		return IPT_CONTINUE;

	/* Attach fake conntrack entry. 
	   If there is a real ct entry correspondig to this packet, 
	   it'll hang aroun till timing out. We don't deal with it
	   for performance reasons. JK */
	(*pskb)->nfct = &ip_conntrack_untracked.ct_general;
	(*pskb)->nfctinfo = IP_CT_NEW;
	nf_conntrack_get((*pskb)->nfct);

	return IPT_CONTINUE;
}

static int
checkentry(const char *tablename,
	   const struct ipt_entry *e,
           void *targinfo,
           unsigned int targinfosize,
           unsigned int hook_mask)
{
	if (targinfosize != 0) {
		printk(KERN_WARNING "NOTRACK: targinfosize %u != 0\n",
		       targinfosize);
		return 0;
	}

	if (strcmp(tablename, "raw") != 0) {
		printk(KERN_WARNING "NOTRACK: can only be called from \"raw\" table, not \"%s\"\n", tablename);
		return 0;
	}

	return 1;
}

static struct ipt_target ipt_notrack_reg = { 
	.name = "NOTRACK", 
	.target = target, 
	.checkentry = checkentry,
	.me = THIS_MODULE 
};

static int __init init(void)
{
	if (ipt_register_target(&ipt_notrack_reg))
		return -EINVAL;

	return 0;
}

static void __exit fini(void)
{
	ipt_unregister_target(&ipt_notrack_reg);
}

module_init(init);
module_exit(fini);
MODULE_LICENSE("GPL");
