#include <linux/sysctl.h>
#include <linux/slab.h>
#include <net/net_namespace.h>
#include <net/xfrm.h>

static void __net_init __xfrm_sysctl_init(struct net *net)
{
	net->xfrm.sysctl_aevent_etime = XFRM_AE_ETIME;
	net->xfrm.sysctl_aevent_rseqth = XFRM_AE_SEQT_SIZE;
	net->xfrm.sysctl_larval_drop = 1;
	net->xfrm.sysctl_acq_expires = 30;
}

#ifdef CONFIG_SYSCTL
static struct ctl_table xfrm_table[] = {
	{
		.procname	= "xfrm_aevent_etime",
		.maxlen		= sizeof(u32),
		.mode		= 0644,
		.proc_handler	= proc_dointvec
	},
	{
		.procname	= "xfrm_aevent_rseqth",
		.maxlen		= sizeof(u32),
		.mode		= 0644,
		.proc_handler	= proc_dointvec
	},
	{
		.procname	= "xfrm_larval_drop",
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec
	},
	{
		.procname	= "xfrm_acq_expires",
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec
	},
	{}
};

int __net_init xfrm_sysctl_init(struct net *net)
{
	struct ctl_table *table;

	__xfrm_sysctl_init(net);

	table = kmemdup(xfrm_table, sizeof(xfrm_table), GFP_KERNEL);
	if (!table)
		goto out_kmemdup;
	table[0].data = &net->xfrm.sysctl_aevent_etime;
	table[1].data = &net->xfrm.sysctl_aevent_rseqth;
	table[2].data = &net->xfrm.sysctl_larval_drop;
	table[3].data = &net->xfrm.sysctl_acq_expires;

	net->xfrm.sysctl_hdr = register_net_sysctl_table(net, net_core_path, table);
	if (!net->xfrm.sysctl_hdr)
		goto out_register;
	return 0;

out_register:
	kfree(table);
out_kmemdup:
	return -ENOMEM;
}

void __net_exit xfrm_sysctl_fini(struct net *net)
{
	struct ctl_table *table;

	table = net->xfrm.sysctl_hdr->ctl_table_arg;
	unregister_net_sysctl_table(net->xfrm.sysctl_hdr);
	kfree(table);
}
#else
int __net_init xfrm_sysctl_init(struct net *net)
{
	__xfrm_sysctl_init(net);
	return 0;
}
#endif
