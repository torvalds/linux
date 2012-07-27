/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Copyright (C) 1996 Mike Shaver (shaver@zeroknowledge.com)
 */
#include <linux/mm.h>
#include <linux/sysctl.h>
#include <linux/init.h>
#include <net/ax25.h>
#include <net/rose.h>

static int min_timer[]  = {1 * HZ};
static int max_timer[]  = {300 * HZ};
static int min_idle[]   = {0 * HZ};
static int max_idle[]   = {65535 * HZ};
static int min_route[1],       max_route[] = {1};
static int min_ftimer[] = {60 * HZ};
static int max_ftimer[] = {600 * HZ};
static int min_maxvcs[] = {1}, max_maxvcs[] = {254};
static int min_window[] = {1}, max_window[] = {7};

static struct ctl_table_header *rose_table_header;

static ctl_table rose_table[] = {
	{
		.procname	= "restart_request_timeout",
		.data		= &sysctl_rose_restart_request_timeout,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &min_timer,
		.extra2		= &max_timer
	},
	{
		.procname	= "call_request_timeout",
		.data		= &sysctl_rose_call_request_timeout,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &min_timer,
		.extra2		= &max_timer
	},
	{
		.procname	= "reset_request_timeout",
		.data		= &sysctl_rose_reset_request_timeout,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &min_timer,
		.extra2		= &max_timer
	},
	{
		.procname	= "clear_request_timeout",
		.data		= &sysctl_rose_clear_request_timeout,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &min_timer,
		.extra2		= &max_timer
	},
	{
		.procname	= "no_activity_timeout",
		.data		= &sysctl_rose_no_activity_timeout,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &min_idle,
		.extra2		= &max_idle
	},
	{
		.procname	= "acknowledge_hold_back_timeout",
		.data		= &sysctl_rose_ack_hold_back_timeout,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &min_timer,
		.extra2		= &max_timer
	},
	{
		.procname	= "routing_control",
		.data		= &sysctl_rose_routing_control,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &min_route,
		.extra2		= &max_route
	},
	{
		.procname	= "link_fail_timeout",
		.data		= &sysctl_rose_link_fail_timeout,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &min_ftimer,
		.extra2		= &max_ftimer
	},
	{
		.procname	= "maximum_virtual_circuits",
		.data		= &sysctl_rose_maximum_vcs,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &min_maxvcs,
		.extra2		= &max_maxvcs
	},
	{
		.procname	= "window_size",
		.data		= &sysctl_rose_window_size,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &min_window,
		.extra2		= &max_window
	},
	{ }
};

void __init rose_register_sysctl(void)
{
	rose_table_header = register_net_sysctl(&init_net, "net/rose", rose_table);
}

void rose_unregister_sysctl(void)
{
	unregister_net_sysctl_table(rose_table_header);
}
