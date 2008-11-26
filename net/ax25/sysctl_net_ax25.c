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
#include <linux/spinlock.h>
#include <net/ax25.h>

static int min_ipdefmode[1],    	max_ipdefmode[] = {1};
static int min_axdefmode[1],            max_axdefmode[] = {1};
static int min_backoff[1],		max_backoff[] = {2};
static int min_conmode[1],		max_conmode[] = {2};
static int min_window[] = {1},		max_window[] = {7};
static int min_ewindow[] = {1},		max_ewindow[] = {63};
static int min_t1[] = {1},		max_t1[] = {30000};
static int min_t2[] = {1},		max_t2[] = {20000};
static int min_t3[1],			max_t3[] = {3600000};
static int min_idle[1],			max_idle[] = {65535000};
static int min_n2[] = {1},		max_n2[] = {31};
static int min_paclen[] = {1},		max_paclen[] = {512};
static int min_proto[1],		max_proto[] = { AX25_PROTO_MAX };
#ifdef CONFIG_AX25_DAMA_SLAVE
static int min_ds_timeout[1],		max_ds_timeout[] = {65535000};
#endif

static struct ctl_table_header *ax25_table_header;

static ctl_table *ax25_table;
static int ax25_table_size;

static struct ctl_path ax25_path[] = {
	{ .procname = "net", .ctl_name = CTL_NET, },
	{ .procname = "ax25", .ctl_name = NET_AX25, },
	{ }
};

static const ctl_table ax25_param_table[] = {
	{
		.ctl_name	= NET_AX25_IP_DEFAULT_MODE,
		.procname	= "ip_default_mode",
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.strategy	= sysctl_intvec,
		.extra1		= &min_ipdefmode,
		.extra2		= &max_ipdefmode
	},
	{
		.ctl_name	= NET_AX25_DEFAULT_MODE,
		.procname	= "ax25_default_mode",
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.strategy	= sysctl_intvec,
		.extra1		= &min_axdefmode,
		.extra2		= &max_axdefmode
	},
	{
		.ctl_name	= NET_AX25_BACKOFF_TYPE,
		.procname	= "backoff_type",
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.strategy	= sysctl_intvec,
		.extra1		= &min_backoff,
		.extra2		= &max_backoff
	},
	{
		.ctl_name	= NET_AX25_CONNECT_MODE,
		.procname	= "connect_mode",
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.strategy	= sysctl_intvec,
		.extra1		= &min_conmode,
		.extra2		= &max_conmode
	},
	{
		.ctl_name	= NET_AX25_STANDARD_WINDOW,
		.procname	= "standard_window_size",
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.strategy	= sysctl_intvec,
		.extra1		= &min_window,
		.extra2		= &max_window
	},
	{
		.ctl_name	= NET_AX25_EXTENDED_WINDOW,
		.procname	= "extended_window_size",
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.strategy	= sysctl_intvec,
		.extra1		= &min_ewindow,
		.extra2		= &max_ewindow
	},
	{
		.ctl_name	= NET_AX25_T1_TIMEOUT,
		.procname	= "t1_timeout",
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.strategy	= sysctl_intvec,
		.extra1		= &min_t1,
		.extra2		= &max_t1
	},
	{
		.ctl_name	= NET_AX25_T2_TIMEOUT,
		.procname	= "t2_timeout",
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.strategy	= sysctl_intvec,
		.extra1		= &min_t2,
		.extra2		= &max_t2
	},
	{
		.ctl_name	= NET_AX25_T3_TIMEOUT,
		.procname	= "t3_timeout",
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.strategy	= sysctl_intvec,
		.extra1		= &min_t3,
		.extra2		= &max_t3
	},
	{
		.ctl_name	= NET_AX25_IDLE_TIMEOUT,
		.procname	= "idle_timeout",
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.strategy	= sysctl_intvec,
		.extra1		= &min_idle,
		.extra2		= &max_idle
	},
	{
		.ctl_name	= NET_AX25_N2,
		.procname	= "maximum_retry_count",
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.strategy	= sysctl_intvec,
		.extra1		= &min_n2,
		.extra2		= &max_n2
	},
	{
		.ctl_name	= NET_AX25_PACLEN,
		.procname	= "maximum_packet_length",
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.strategy	= sysctl_intvec,
		.extra1		= &min_paclen,
		.extra2		= &max_paclen
	},
	{
		.ctl_name	= NET_AX25_PROTOCOL,
		.procname	= "protocol",
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.strategy	= sysctl_intvec,
		.extra1		= &min_proto,
		.extra2		= &max_proto
	},
#ifdef CONFIG_AX25_DAMA_SLAVE
	{
		.ctl_name	= NET_AX25_DAMA_SLAVE_TIMEOUT,
		.procname	= "dama_slave_timeout",
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.strategy	= sysctl_intvec,
		.extra1		= &min_ds_timeout,
		.extra2		= &max_ds_timeout
	},
#endif

	{ .ctl_name = 0 }	/* that's all, folks! */
};

void ax25_register_sysctl(void)
{
	ax25_dev *ax25_dev;
	int n, k;

	spin_lock_bh(&ax25_dev_lock);
	for (ax25_table_size = sizeof(ctl_table), ax25_dev = ax25_dev_list; ax25_dev != NULL; ax25_dev = ax25_dev->next)
		ax25_table_size += sizeof(ctl_table);

	if ((ax25_table = kzalloc(ax25_table_size, GFP_ATOMIC)) == NULL) {
		spin_unlock_bh(&ax25_dev_lock);
		return;
	}

	for (n = 0, ax25_dev = ax25_dev_list; ax25_dev != NULL; ax25_dev = ax25_dev->next) {
		struct ctl_table *child = kmemdup(ax25_param_table,
						  sizeof(ax25_param_table),
						  GFP_ATOMIC);
		if (!child) {
			while (n--)
				kfree(ax25_table[n].child);
			kfree(ax25_table);
			spin_unlock_bh(&ax25_dev_lock);
			return;
		}
		ax25_table[n].child = ax25_dev->systable = child;
		ax25_table[n].ctl_name     = n + 1;
		ax25_table[n].procname     = ax25_dev->dev->name;
		ax25_table[n].mode         = 0555;

		child[AX25_MAX_VALUES].ctl_name = 0;	/* just in case... */

		for (k = 0; k < AX25_MAX_VALUES; k++)
			child[k].data = &ax25_dev->values[k];

		n++;
	}
	spin_unlock_bh(&ax25_dev_lock);

	ax25_table_header = register_sysctl_paths(ax25_path, ax25_table);
}

void ax25_unregister_sysctl(void)
{
	ctl_table *p;
	unregister_sysctl_table(ax25_table_header);

	for (p = ax25_table; p->ctl_name; p++)
		kfree(p->child);
	kfree(ax25_table);
}
