/* Accouting handling for netfilter. */

/*
 * (C) 2008 Krzysztof Piotr Oledzki <ole@ans.pl>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/netfilter.h>
#include <linux/kernel.h>
#include <linux/moduleparam.h>

#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_conntrack_extend.h>
#include <net/netfilter/nf_conntrack_acct.h>

#ifdef CONFIG_NF_CT_ACCT
#define NF_CT_ACCT_DEFAULT 1
#else
#define NF_CT_ACCT_DEFAULT 0
#endif

int nf_ct_acct __read_mostly = NF_CT_ACCT_DEFAULT;
EXPORT_SYMBOL_GPL(nf_ct_acct);

module_param_named(acct, nf_ct_acct, bool, 0644);
MODULE_PARM_DESC(acct, "Enable connection tracking flow accounting.");

#ifdef CONFIG_SYSCTL
static struct ctl_table_header *acct_sysctl_header;
static struct ctl_table acct_sysctl_table[] = {
	{
		.ctl_name	= CTL_UNNUMBERED,
		.procname	= "nf_conntrack_acct",
		.data		= &nf_ct_acct,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{}
};
#endif /* CONFIG_SYSCTL */

unsigned int
seq_print_acct(struct seq_file *s, const struct nf_conn *ct, int dir)
{
	struct nf_conn_counter *acct;

	acct = nf_conn_acct_find(ct);
	if (!acct)
		return 0;

	return seq_printf(s, "packets=%llu bytes=%llu ",
			  (unsigned long long)acct[dir].packets,
			  (unsigned long long)acct[dir].bytes);
};
EXPORT_SYMBOL_GPL(seq_print_acct);

static struct nf_ct_ext_type acct_extend __read_mostly = {
	.len	= sizeof(struct nf_conn_counter[IP_CT_DIR_MAX]),
	.align	= __alignof__(struct nf_conn_counter[IP_CT_DIR_MAX]),
	.id	= NF_CT_EXT_ACCT,
};

int nf_conntrack_acct_init(void)
{
	int ret;

#ifdef CONFIG_NF_CT_ACCT
	printk(KERN_WARNING "CONFIG_NF_CT_ACCT is deprecated and will be removed soon. Plase use\n");
	printk(KERN_WARNING "nf_conntrack.acct=1 kernel paramater, acct=1 nf_conntrack module option or\n");
	printk(KERN_WARNING "sysctl net.netfilter.nf_conntrack_acct=1 to enable it.\n");
#endif

	ret = nf_ct_extend_register(&acct_extend);
	if (ret < 0) {
		printk(KERN_ERR "nf_conntrack_acct: Unable to register extension\n");
		return ret;
	}

#ifdef CONFIG_SYSCTL
	acct_sysctl_header = register_sysctl_paths(nf_net_netfilter_sysctl_path,
				acct_sysctl_table);

	if (!acct_sysctl_header) {
		nf_ct_extend_unregister(&acct_extend);

		printk(KERN_ERR "nf_conntrack_acct: can't register to sysctl.\n");
		return -ENOMEM;
	}
#endif

	return 0;
}

void nf_conntrack_acct_fini(void)
{
#ifdef CONFIG_SYSCTL
	unregister_sysctl_table(acct_sysctl_header);
#endif
	nf_ct_extend_unregister(&acct_extend);
}
