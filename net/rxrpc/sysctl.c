/* sysctl.c: Rx RPC control
 *
 * Copyright (C) 2002 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/config.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/sysctl.h>
#include <rxrpc/types.h>
#include <rxrpc/rxrpc.h>
#include <asm/errno.h>
#include "internal.h"

int rxrpc_ktrace;
int rxrpc_kdebug;
int rxrpc_kproto;
int rxrpc_knet;

#ifdef CONFIG_SYSCTL
static struct ctl_table_header *rxrpc_sysctl = NULL;

static ctl_table rxrpc_sysctl_table[] = {
        {
		.ctl_name	= 1,
		.procname	= "kdebug",
		.data		= &rxrpc_kdebug,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec
	},
        {
		.ctl_name	= 2,
		.procname	= "ktrace",
		.data		= &rxrpc_ktrace,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec
	},
        {
		.ctl_name	= 3,
		.procname	= "kproto",
		.data		= &rxrpc_kproto,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec
	},
        {
		.ctl_name	= 4,
		.procname	= "knet",
		.data		= &rxrpc_knet,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec
	},
        {
		.ctl_name	= 5,
		.procname	= "peertimo",
		.data		= &rxrpc_peer_timeout,
		.maxlen		= sizeof(unsigned long),
		.mode		= 0644,
		.proc_handler	= &proc_doulongvec_minmax
	},
        {
		.ctl_name	= 6,
		.procname	= "conntimo",
		.data		= &rxrpc_conn_timeout,
		.maxlen		= sizeof(unsigned long),
		.mode		= 0644,
		.proc_handler	= &proc_doulongvec_minmax
	},
	{ .ctl_name = 0 }
};

static ctl_table rxrpc_dir_sysctl_table[] = {
	{
		.ctl_name	= 1,
		.procname	= "rxrpc",
		.maxlen		= 0,
		.mode		= 0555,
		.child		= rxrpc_sysctl_table
	},
	{ .ctl_name = 0 }
};
#endif /* CONFIG_SYSCTL */

/*****************************************************************************/
/*
 * initialise the sysctl stuff for Rx RPC
 */
int rxrpc_sysctl_init(void)
{
#ifdef CONFIG_SYSCTL
	rxrpc_sysctl = register_sysctl_table(rxrpc_dir_sysctl_table, 0);
	if (!rxrpc_sysctl)
		return -ENOMEM;
#endif /* CONFIG_SYSCTL */

	return 0;
} /* end rxrpc_sysctl_init() */

/*****************************************************************************/
/*
 * clean up the sysctl stuff for Rx RPC
 */
void rxrpc_sysctl_cleanup(void)
{
#ifdef CONFIG_SYSCTL
	if (rxrpc_sysctl) {
		unregister_sysctl_table(rxrpc_sysctl);
		rxrpc_sysctl = NULL;
	}
#endif /* CONFIG_SYSCTL */

} /* end rxrpc_sysctl_cleanup() */
