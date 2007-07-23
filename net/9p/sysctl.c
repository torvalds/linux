/*
 *  net/9p/sysctl.c
 *
 *  9P sysctl interface
 *
 *  Copyright (C) 2007 by Latchesar Ionkov <lucho@ionkov.net>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2
 *  as published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to:
 *  Free Software Foundation
 *  51 Franklin Street, Fifth Floor
 *  Boston, MA  02111-1301  USA
 *
 */

#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/sysctl.h>
#include <linux/init.h>
#include <net/9p/9p.h>

static struct ctl_table p9_table[] = {
#ifdef CONFIG_NET_9P_DEBUG
	{
		.ctl_name       = CTL_UNNUMBERED,
		.procname       = "debug",
		.data           = &p9_debug_level,
		.maxlen         = sizeof(int),
		.mode           = 0644,
		.proc_handler   = &proc_dointvec
	},
#endif
	{},
};

static struct ctl_table p9_net_table[] = {
	{
		.ctl_name	= CTL_UNNUMBERED,
		.procname	= "9p",
		.maxlen		= 0,
		.mode		= 0555,
		.child		= p9_table,
	},
	{},
};

static struct ctl_table p9_ctl_table[] = {
	{
		.ctl_name	= CTL_NET,
		.procname	= "net",
		.maxlen		= 0,
		.mode		= 0555,
		.child		= p9_net_table,
	},
	{},
};

static struct ctl_table_header *p9_table_header;

int __init p9_sysctl_register(void)
{
	p9_table_header = register_sysctl_table(p9_ctl_table);
	if (!p9_table_header)
		return -ENOMEM;

	return 0;
}

void __exit p9_sysctl_unregister(void)
{
	 unregister_sysctl_table(p9_table_header);
}
