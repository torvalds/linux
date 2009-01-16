/*
 * File: sysctl.c
 *
 * Phonet /proc/sys/net/phonet interface implementation
 *
 * Copyright (C) 2008 Nokia Corporation.
 *
 * Contact: Remi Denis-Courmont <remi.denis-courmont@nokia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

#include <linux/seqlock.h>
#include <linux/sysctl.h>
#include <linux/errno.h>
#include <linux/init.h>

#define DYNAMIC_PORT_MIN	0x40
#define DYNAMIC_PORT_MAX	0x7f

static DEFINE_SEQLOCK(local_port_range_lock);
static int local_port_range_min[2] = {0, 0};
static int local_port_range_max[2] = {1023, 1023};
static int local_port_range[2] = {DYNAMIC_PORT_MIN, DYNAMIC_PORT_MAX};
static struct ctl_table_header *phonet_table_hrd;

static void set_local_port_range(int range[2])
{
	write_seqlock(&local_port_range_lock);
	local_port_range[0] = range[0];
	local_port_range[1] = range[1];
	write_sequnlock(&local_port_range_lock);
}

void phonet_get_local_port_range(int *min, int *max)
{
	unsigned seq;
	do {
		seq = read_seqbegin(&local_port_range_lock);
		if (min)
			*min = local_port_range[0];
		if (max)
			*max = local_port_range[1];
	} while (read_seqretry(&local_port_range_lock, seq));
}

static int proc_local_port_range(ctl_table *table, int write, struct file *filp,
				void __user *buffer,
				size_t *lenp, loff_t *ppos)
{
	int ret;
	int range[2] = {local_port_range[0], local_port_range[1]};
	ctl_table tmp = {
		.data = &range,
		.maxlen = sizeof(range),
		.mode = table->mode,
		.extra1 = &local_port_range_min,
		.extra2 = &local_port_range_max,
	};

	ret = proc_dointvec_minmax(&tmp, write, filp, buffer, lenp, ppos);

	if (write && ret == 0) {
		if (range[1] < range[0])
			ret = -EINVAL;
		else
			set_local_port_range(range);
	}

	return ret;
}

static struct ctl_table phonet_table[] = {
	{
		.ctl_name	= CTL_UNNUMBERED,
		.procname	= "local_port_range",
		.data		= &local_port_range,
		.maxlen		= sizeof(local_port_range),
		.mode		= 0644,
		.proc_handler	= proc_local_port_range,
		.strategy	= NULL,
	},
	{ .ctl_name = 0 }
};

static struct ctl_path phonet_ctl_path[] = {
	{ .procname = "net", .ctl_name = CTL_NET, },
	{ .procname = "phonet", .ctl_name = CTL_UNNUMBERED, },
	{ },
};

int __init phonet_sysctl_init(void)
{
	phonet_table_hrd = register_sysctl_paths(phonet_ctl_path, phonet_table);
	return phonet_table_hrd == NULL ? -ENOMEM : 0;
}

void phonet_sysctl_exit(void)
{
	unregister_sysctl_table(phonet_table_hrd);
}
