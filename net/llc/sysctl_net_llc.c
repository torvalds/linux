/*
 * sysctl_net_llc.c: sysctl interface to LLC net subsystem.
 *
 * Arnaldo Carvalho de Melo <acme@conectiva.com.br>
 */

#include <linux/mm.h>
#include <linux/init.h>
#include <linux/sysctl.h>
#include <net/net_namespace.h>
#include <net/llc.h>

#ifndef CONFIG_SYSCTL
#error This file should not be compiled without CONFIG_SYSCTL defined
#endif

static struct ctl_table llc2_timeout_table[] = {
	{
		.procname	= "ack",
		.data		= &sysctl_llc2_ack_timeout,
		.maxlen		= sizeof(sysctl_llc2_ack_timeout),
		.mode		= 0644,
		.proc_handler   = proc_dointvec_jiffies,
	},
	{
		.procname	= "busy",
		.data		= &sysctl_llc2_busy_timeout,
		.maxlen		= sizeof(sysctl_llc2_busy_timeout),
		.mode		= 0644,
		.proc_handler   = proc_dointvec_jiffies,
	},
	{
		.procname	= "p",
		.data		= &sysctl_llc2_p_timeout,
		.maxlen		= sizeof(sysctl_llc2_p_timeout),
		.mode		= 0644,
		.proc_handler   = proc_dointvec_jiffies,
	},
	{
		.procname	= "rej",
		.data		= &sysctl_llc2_rej_timeout,
		.maxlen		= sizeof(sysctl_llc2_rej_timeout),
		.mode		= 0644,
		.proc_handler   = proc_dointvec_jiffies,
	},
	{ },
};

static struct ctl_table llc_station_table[] = {
	{ },
};

static struct ctl_table_header *llc2_timeout_header;
static struct ctl_table_header *llc_station_header;

int __init llc_sysctl_init(void)
{
	llc2_timeout_header = register_net_sysctl(&init_net, "net/llc/llc2/timeout", llc2_timeout_table);
	llc_station_header = register_net_sysctl(&init_net, "net/llc/station", llc_station_table);

	if (!llc2_timeout_header || !llc_station_header) {
		llc_sysctl_exit();
		return -ENOMEM;
	}
	return 0;
}

void llc_sysctl_exit(void)
{
	if (llc2_timeout_header) {
		unregister_net_sysctl_table(llc2_timeout_header);
		llc2_timeout_header = NULL;
	}
	if (llc_station_header) {
		unregister_net_sysctl_table(llc_station_header);
		llc_station_header = NULL;
	}
}
