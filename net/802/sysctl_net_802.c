/* -*- linux-c -*-
 *		sysctl_net_802.c: sysctl interface to net 802 subsystem.
 *
 *		Begun April 1, 1996, Mike Shaver.
 *		Added /proc/sys/net/802 directory entry (empty =) ). [MS]
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */

#include <linux/mm.h>
#include <linux/sysctl.h>
#include <linux/config.h>

#ifdef CONFIG_TR
extern int sysctl_tr_rif_timeout;
#endif

struct ctl_table tr_table[] = {
#ifdef CONFIG_TR
	{
		.ctl_name	= NET_TR_RIF_TIMEOUT,
		.procname	= "rif_timeout",
		.data		= &sysctl_tr_rif_timeout,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec
	},
#endif /* CONFIG_TR */
	{ 0 },
};
