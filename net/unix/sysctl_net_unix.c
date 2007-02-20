/*
 * NET4:	Sysctl interface to net af_unix subsystem.
 *
 * Authors:	Mike Shaver.
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */

#include <linux/mm.h>
#include <linux/sysctl.h>

#include <net/af_unix.h>

static ctl_table unix_table[] = {
	{
		.ctl_name	= NET_UNIX_MAX_DGRAM_QLEN,
		.procname	= "max_dgram_qlen",
		.data		= &sysctl_unix_max_dgram_qlen,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec
	},
	{ .ctl_name = 0 }
};

static ctl_table unix_net_table[] = {
	{
		.ctl_name	= NET_UNIX,
		.procname	= "unix",
		.mode		= 0555,
		.child		= unix_table
	},
	{ .ctl_name = 0 }
};

static ctl_table unix_root_table[] = {
	{
		.ctl_name	= CTL_NET,
		.procname	= "net",
		.mode		= 0555,
		.child		= unix_net_table
	},
	{ .ctl_name = 0 }
};

static struct ctl_table_header * unix_sysctl_header;

void unix_sysctl_register(void)
{
	unix_sysctl_header = register_sysctl_table(unix_root_table);
}

void unix_sysctl_unregister(void)
{
	unregister_sysctl_table(unix_sysctl_header);
}

