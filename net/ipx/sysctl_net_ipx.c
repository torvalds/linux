// SPDX-License-Identifier: GPL-2.0
/* -*- linux-c -*-
 * sysctl_net_ipx.c: sysctl interface to net IPX subsystem.
 *
 * Begun April 1, 1996, Mike Shaver.
 * Added /proc/sys/net/ipx directory entry (empty =) ). [MS]
 * Added /proc/sys/net/ipx/ipx_pprop_broadcasting - acme March 4, 2001
 */

#include <linux/mm.h>
#include <linux/sysctl.h>
#include <net/net_namespace.h>
#include <net/ipx.h>

#ifndef CONFIG_SYSCTL
#error This file should not be compiled without CONFIG_SYSCTL defined
#endif

static struct ctl_table ipx_table[] = {
	{
		.procname	= "ipx_pprop_broadcasting",
		.data		= &sysctl_ipx_pprop_broadcasting,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{ },
};

static struct ctl_table_header *ipx_table_header;

void ipx_register_sysctl(void)
{
	ipx_table_header = register_net_sysctl(&init_net, "net/ipx", ipx_table);
}

void ipx_unregister_sysctl(void)
{
	unregister_net_sysctl_table(ipx_table_header);
}
