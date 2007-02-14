/*
 * sysctl_net_atalk.c: sysctl interface to net AppleTalk subsystem.
 *
 * Begun April 1, 1996, Mike Shaver.
 * Added /proc/sys/net/atalk directory entry (empty =) ). [MS]
 * Dynamic registration, added aarp entries. (5/30/97 Chris Horn)
 */

#include <linux/sysctl.h>
#include <net/sock.h>
#include <linux/atalk.h>

static struct ctl_table atalk_table[] = {
	{
		.ctl_name	= NET_ATALK_AARP_EXPIRY_TIME,
		.procname	= "aarp-expiry-time",
		.data		= &sysctl_aarp_expiry_time,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_jiffies,
		.strategy	= &sysctl_jiffies,
	},
	{
		.ctl_name	= NET_ATALK_AARP_TICK_TIME,
		.procname	= "aarp-tick-time",
		.data		= &sysctl_aarp_tick_time,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_jiffies,
		.strategy	= &sysctl_jiffies,
	},
	{
		.ctl_name	= NET_ATALK_AARP_RETRANSMIT_LIMIT,
		.procname	= "aarp-retransmit-limit",
		.data		= &sysctl_aarp_retransmit_limit,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{
		.ctl_name	= NET_ATALK_AARP_RESOLVE_TIME,
		.procname	= "aarp-resolve-time",
		.data		= &sysctl_aarp_resolve_time,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_jiffies,
		.strategy	= &sysctl_jiffies,
	},
	{ 0 },
};

static struct ctl_table atalk_dir_table[] = {
	{
		.ctl_name	= NET_ATALK,
		.procname	= "appletalk",
		.mode		= 0555,
		.child		= atalk_table,
	},
	{ 0 },
};

static struct ctl_table atalk_root_table[] = {
	{
		.ctl_name	= CTL_NET,
		.procname	= "net",
		.mode		= 0555,
		.child		= atalk_dir_table,
	},
	{ 0 },
};

static struct ctl_table_header *atalk_table_header;

void atalk_register_sysctl(void)
{
	atalk_table_header = register_sysctl_table(atalk_root_table);
}

void atalk_unregister_sysctl(void)
{
	unregister_sysctl_table(atalk_table_header);
}
