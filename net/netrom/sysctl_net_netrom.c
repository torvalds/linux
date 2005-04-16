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
#include <net/netrom.h>

/*
 *	Values taken from NET/ROM documentation.
 */
static int min_quality[] = {0}, max_quality[] = {255};
static int min_obs[]     = {0}, max_obs[]     = {255};
static int min_ttl[]     = {0}, max_ttl[]     = {255};
static int min_t1[]      = {5 * HZ};
static int max_t1[]      = {600 * HZ};
static int min_n2[]      = {2}, max_n2[]      = {127};
static int min_t2[]      = {1 * HZ};
static int max_t2[]      = {60 * HZ};
static int min_t4[]      = {1 * HZ};
static int max_t4[]      = {1000 * HZ};
static int min_window[]  = {1}, max_window[]  = {127};
static int min_idle[]    = {0 * HZ};
static int max_idle[]    = {65535 * HZ};
static int min_route[]   = {0}, max_route[]   = {1};
static int min_fails[]   = {1}, max_fails[]   = {10};

static struct ctl_table_header *nr_table_header;

static ctl_table nr_table[] = {
        {
		.ctl_name	= NET_NETROM_DEFAULT_PATH_QUALITY,
		.procname	= "default_path_quality",
		.data		= &sysctl_netrom_default_path_quality,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_minmax,
		.strategy	= &sysctl_intvec,
		.extra1		= &min_quality,
		.extra2		= &max_quality
	},
        {
		.ctl_name	= NET_NETROM_OBSOLESCENCE_COUNT_INITIALISER,
		.procname	= "obsolescence_count_initialiser",
		.data		= &sysctl_netrom_obsolescence_count_initialiser,
		.maxlen		= sizeof(int),
		.mode		= 0644,
 		.proc_handler	= &proc_dointvec_minmax,
		.strategy	= &sysctl_intvec,
		.extra1		= &min_obs,
		.extra2		= &max_obs
	},
        {
		.ctl_name	= NET_NETROM_NETWORK_TTL_INITIALISER,
		.procname	= "network_ttl_initialiser",
		.data		= &sysctl_netrom_network_ttl_initialiser,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_minmax,
		.strategy	= &sysctl_intvec,
		.extra1		= &min_ttl,
		.extra2		= &max_ttl
	},
        {
		.ctl_name	= NET_NETROM_TRANSPORT_TIMEOUT,
		.procname	= "transport_timeout",
		.data		= &sysctl_netrom_transport_timeout,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_minmax,
		.strategy	= &sysctl_intvec,
		.extra1		= &min_t1,
		.extra2		= &max_t1
	},
        {
		.ctl_name	= NET_NETROM_TRANSPORT_MAXIMUM_TRIES,
		.procname	= "transport_maximum_tries",
		.data		= &sysctl_netrom_transport_maximum_tries,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_minmax,
		.strategy	= &sysctl_intvec,
		.extra1		= &min_n2,
		.extra2		= &max_n2
	},
        {
		.ctl_name	= NET_NETROM_TRANSPORT_ACKNOWLEDGE_DELAY,
		.procname	= "transport_acknowledge_delay",
		.data		= &sysctl_netrom_transport_acknowledge_delay,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_minmax,
		.strategy	= &sysctl_intvec,
		.extra1		= &min_t2,
		.extra2		= &max_t2
	},
        {
		.ctl_name	= NET_NETROM_TRANSPORT_BUSY_DELAY,
		.procname	= "transport_busy_delay",
		.data		= &sysctl_netrom_transport_busy_delay,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_minmax,
		.strategy	= &sysctl_intvec,
		.extra1		= &min_t4,
		.extra2		= &max_t4
	},
        {
		.ctl_name	= NET_NETROM_TRANSPORT_REQUESTED_WINDOW_SIZE,
		.procname	= "transport_requested_window_size",
		.data		= &sysctl_netrom_transport_requested_window_size,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_minmax,
		.strategy	= &sysctl_intvec,
		.extra1		= &min_window,
		.extra2		= &max_window
	},
        {
		.ctl_name	= NET_NETROM_TRANSPORT_NO_ACTIVITY_TIMEOUT,
		.procname	= "transport_no_activity_timeout",
		.data		= &sysctl_netrom_transport_no_activity_timeout,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_minmax,
		.strategy	= &sysctl_intvec,
		.extra1		= &min_idle,
		.extra2		= &max_idle
	},
        {
		.ctl_name	= NET_NETROM_ROUTING_CONTROL,
		.procname	= "routing_control",
		.data		= &sysctl_netrom_routing_control,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_minmax,
		.strategy	= &sysctl_intvec,
		.extra1		= &min_route,
		.extra2		= &max_route
	},
        {
		.ctl_name	= NET_NETROM_LINK_FAILS_COUNT,
		.procname	= "link_fails_count",
		.data		= &sysctl_netrom_link_fails_count,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_minmax,
		.strategy	= &sysctl_intvec,
		.extra1		= &min_fails,
		.extra2		= &max_fails
	},
	{ .ctl_name = 0 }
};

static ctl_table nr_dir_table[] = {
	{
		.ctl_name	= NET_NETROM,
		.procname	= "netrom",
		.mode		= 0555,
		.child		= nr_table
	},
	{ .ctl_name = 0 }
};

static ctl_table nr_root_table[] = {
	{
		.ctl_name	= CTL_NET,
		.procname	= "net",
		.mode		= 0555,
		.child		= nr_dir_table
	},
	{ .ctl_name = 0 }
};

void __init nr_register_sysctl(void)
{
	nr_table_header = register_sysctl_table(nr_root_table, 1);
}

void nr_unregister_sysctl(void)
{
	unregister_sysctl_table(nr_table_header);
}
