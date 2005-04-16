/* -*- linux-c -*-
 * sysctl_net_core.c: sysctl interface to net core subsystem.
 *
 * Begun April 1, 1996, Mike Shaver.
 * Added /proc/sys/net/core directory entry (empty =) ). [MS]
 */

#include <linux/mm.h>
#include <linux/sysctl.h>
#include <linux/config.h>
#include <linux/module.h>

#ifdef CONFIG_SYSCTL

extern int netdev_max_backlog;
extern int weight_p;
extern int no_cong_thresh;
extern int no_cong;
extern int lo_cong;
extern int mod_cong;
extern int netdev_fastroute;
extern int net_msg_cost;
extern int net_msg_burst;

extern __u32 sysctl_wmem_max;
extern __u32 sysctl_rmem_max;
extern __u32 sysctl_wmem_default;
extern __u32 sysctl_rmem_default;

extern int sysctl_core_destroy_delay;
extern int sysctl_optmem_max;
extern int sysctl_somaxconn;

#ifdef CONFIG_NET_DIVERT
extern char sysctl_divert_version[];
#endif /* CONFIG_NET_DIVERT */

/*
 * This strdup() is used for creating copies of network 
 * device names to be handed over to sysctl.
 */
 
char *net_sysctl_strdup(const char *s)
{
	char *rv = kmalloc(strlen(s)+1, GFP_KERNEL);
	if (rv)
		strcpy(rv, s);
	return rv;
}

ctl_table core_table[] = {
#ifdef CONFIG_NET
	{
		.ctl_name	= NET_CORE_WMEM_MAX,
		.procname	= "wmem_max",
		.data		= &sysctl_wmem_max,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec
	},
	{
		.ctl_name	= NET_CORE_RMEM_MAX,
		.procname	= "rmem_max",
		.data		= &sysctl_rmem_max,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec
	},
	{
		.ctl_name	= NET_CORE_WMEM_DEFAULT,
		.procname	= "wmem_default",
		.data		= &sysctl_wmem_default,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec
	},
	{
		.ctl_name	= NET_CORE_RMEM_DEFAULT,
		.procname	= "rmem_default",
		.data		= &sysctl_rmem_default,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec
	},
	{
		.ctl_name	= NET_CORE_DEV_WEIGHT,
		.procname	= "dev_weight",
		.data		= &weight_p,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec
	},
	{
		.ctl_name	= NET_CORE_MAX_BACKLOG,
		.procname	= "netdev_max_backlog",
		.data		= &netdev_max_backlog,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec
	},
	{
		.ctl_name	= NET_CORE_NO_CONG_THRESH,
		.procname	= "no_cong_thresh",
		.data		= &no_cong_thresh,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec
	},
	{
		.ctl_name	= NET_CORE_NO_CONG,
		.procname	= "no_cong",
		.data		= &no_cong,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec
	},
	{
		.ctl_name	= NET_CORE_LO_CONG,
		.procname	= "lo_cong",
		.data		= &lo_cong,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec
	},
	{
		.ctl_name	= NET_CORE_MOD_CONG,
		.procname	= "mod_cong",
		.data		= &mod_cong,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec
	},
	{
		.ctl_name	= NET_CORE_MSG_COST,
		.procname	= "message_cost",
		.data		= &net_msg_cost,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_jiffies,
		.strategy	= &sysctl_jiffies,
	},
	{
		.ctl_name	= NET_CORE_MSG_BURST,
		.procname	= "message_burst",
		.data		= &net_msg_burst,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{
		.ctl_name	= NET_CORE_OPTMEM_MAX,
		.procname	= "optmem_max",
		.data		= &sysctl_optmem_max,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec
	},
#ifdef CONFIG_NET_DIVERT
	{
		.ctl_name	= NET_CORE_DIVERT_VERSION,
		.procname	= "divert_version",
		.data		= (void *)sysctl_divert_version,
		.maxlen		= 32,
		.mode		= 0444,
		.proc_handler	= &proc_dostring
	},
#endif /* CONFIG_NET_DIVERT */
#endif /* CONFIG_NET */
	{
		.ctl_name	= NET_CORE_SOMAXCONN,
		.procname	= "somaxconn",
		.data		= &sysctl_somaxconn,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec
	},
	{ .ctl_name = 0 }
};

EXPORT_SYMBOL(net_sysctl_strdup);

#endif
