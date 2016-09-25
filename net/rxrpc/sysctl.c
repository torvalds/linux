/* sysctls for configuring RxRPC operating parameters
 *
 * Copyright (C) 2014 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */

#include <linux/sysctl.h>
#include <net/sock.h>
#include <net/af_rxrpc.h>
#include "ar-internal.h"

static struct ctl_table_header *rxrpc_sysctl_reg_table;
static const unsigned int zero = 0;
static const unsigned int one = 1;
static const unsigned int four = 4;
static const unsigned int thirtytwo = 32;
static const unsigned int n_65535 = 65535;
static const unsigned int n_max_acks = RXRPC_RXTX_BUFF_SIZE - 1;

/*
 * RxRPC operating parameters.
 *
 * See Documentation/networking/rxrpc.txt and the variable definitions for more
 * information on the individual parameters.
 */
static struct ctl_table rxrpc_sysctl_table[] = {
	/* Values measured in milliseconds */
	{
		.procname	= "req_ack_delay",
		.data		= &rxrpc_requested_ack_delay,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_ms_jiffies,
		.extra1		= (void *)&zero,
	},
	{
		.procname	= "soft_ack_delay",
		.data		= &rxrpc_soft_ack_delay,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_ms_jiffies,
		.extra1		= (void *)&one,
	},
	{
		.procname	= "idle_ack_delay",
		.data		= &rxrpc_idle_ack_delay,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_ms_jiffies,
		.extra1		= (void *)&one,
	},
	{
		.procname	= "resend_timeout",
		.data		= &rxrpc_resend_timeout,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
		.extra1		= (void *)&one,
	},
	{
		.procname	= "idle_conn_expiry",
		.data		= &rxrpc_conn_idle_client_expiry,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_ms_jiffies,
		.extra1		= (void *)&one,
	},
	{
		.procname	= "idle_conn_fast_expiry",
		.data		= &rxrpc_conn_idle_client_fast_expiry,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_ms_jiffies,
		.extra1		= (void *)&one,
	},

	/* Values measured in seconds but used in jiffies */
	{
		.procname	= "max_call_lifetime",
		.data		= &rxrpc_max_call_lifetime,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_jiffies,
		.extra1		= (void *)&one,
	},

	/* Non-time values */
	{
		.procname	= "max_client_conns",
		.data		= &rxrpc_max_client_connections,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= (void *)&rxrpc_reap_client_connections,
	},
	{
		.procname	= "reap_client_conns",
		.data		= &rxrpc_reap_client_connections,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= (void *)&one,
		.extra2		= (void *)&rxrpc_max_client_connections,
	},
	{
		.procname	= "max_backlog",
		.data		= &rxrpc_max_backlog,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= (void *)&four,
		.extra2		= (void *)&thirtytwo,
	},
	{
		.procname	= "rx_window_size",
		.data		= &rxrpc_rx_window_size,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= (void *)&one,
		.extra2		= (void *)&n_max_acks,
	},
	{
		.procname	= "rx_mtu",
		.data		= &rxrpc_rx_mtu,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= (void *)&one,
		.extra2		= (void *)&n_65535,
	},
	{
		.procname	= "rx_jumbo_max",
		.data		= &rxrpc_rx_jumbo_max,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= (void *)&one,
		.extra2		= (void *)&four,
	},

	{ }
};

int __init rxrpc_sysctl_init(void)
{
	rxrpc_sysctl_reg_table = register_net_sysctl(&init_net, "net/rxrpc",
						     rxrpc_sysctl_table);
	if (!rxrpc_sysctl_reg_table)
		return -ENOMEM;
	return 0;
}

void rxrpc_sysctl_exit(void)
{
	if (rxrpc_sysctl_reg_table)
		unregister_net_sysctl_table(rxrpc_sysctl_reg_table);
}
