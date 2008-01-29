/*
 * sysctl_net_llc.c: sysctl interface to LLC net subsystem.
 *
 * Arnaldo Carvalho de Melo <acme@conectiva.com.br>
 */

#include <linux/mm.h>
#include <linux/init.h>
#include <linux/sysctl.h>
#include <net/llc.h>

#ifndef CONFIG_SYSCTL
#error This file should not be compiled without CONFIG_SYSCTL defined
#endif

static struct ctl_table llc2_timeout_table[] = {
	{
		.ctl_name	= NET_LLC2_ACK_TIMEOUT,
		.procname	= "ack",
		.data		= &sysctl_llc2_ack_timeout,
		.maxlen		= sizeof(long),
		.mode		= 0644,
		.proc_handler   = &proc_dointvec_jiffies,
		.strategy       = &sysctl_jiffies,
	},
	{
		.ctl_name	= NET_LLC2_BUSY_TIMEOUT,
		.procname	= "busy",
		.data		= &sysctl_llc2_busy_timeout,
		.maxlen		= sizeof(long),
		.mode		= 0644,
		.proc_handler   = &proc_dointvec_jiffies,
		.strategy       = &sysctl_jiffies,
	},
	{
		.ctl_name	= NET_LLC2_P_TIMEOUT,
		.procname	= "p",
		.data		= &sysctl_llc2_p_timeout,
		.maxlen		= sizeof(long),
		.mode		= 0644,
		.proc_handler   = &proc_dointvec_jiffies,
		.strategy       = &sysctl_jiffies,
	},
	{
		.ctl_name	= NET_LLC2_REJ_TIMEOUT,
		.procname	= "rej",
		.data		= &sysctl_llc2_rej_timeout,
		.maxlen		= sizeof(long),
		.mode		= 0644,
		.proc_handler   = &proc_dointvec_jiffies,
		.strategy       = &sysctl_jiffies,
	},
	{ 0 },
};

static struct ctl_table llc_station_table[] = {
	{
		.ctl_name	= NET_LLC_STATION_ACK_TIMEOUT,
		.procname	= "ack_timeout",
		.data		= &sysctl_llc_station_ack_timeout,
		.maxlen		= sizeof(long),
		.mode		= 0644,
		.proc_handler   = &proc_dointvec_jiffies,
		.strategy       = &sysctl_jiffies,
	},
	{ 0 },
};

static struct ctl_table llc2_dir_timeout_table[] = {
	{
		.ctl_name	= NET_LLC2,
		.procname	= "timeout",
		.mode		= 0555,
		.child		= llc2_timeout_table,
	},
	{ 0 },
};

static struct ctl_table llc_table[] = {
	{
		.ctl_name	= NET_LLC2,
		.procname	= "llc2",
		.mode		= 0555,
		.child		= llc2_dir_timeout_table,
	},
	{
		.ctl_name       = NET_LLC_STATION,
		.procname       = "station",
		.mode           = 0555,
		.child          = llc_station_table,
	},
	{ 0 },
};

static struct ctl_path llc_path[] = {
	{ .procname = "net", .ctl_name = CTL_NET, },
	{ .procname = "llc", .ctl_name = NET_LLC, },
	{ }
};

static struct ctl_table_header *llc_table_header;

int __init llc_sysctl_init(void)
{
	llc_table_header = register_sysctl_paths(llc_path, llc_table);

	return llc_table_header ? 0 : -ENOMEM;
}

void llc_sysctl_exit(void)
{
	if (llc_table_header) {
		unregister_sysctl_table(llc_table_header);
		llc_table_header = NULL;
	}
}
