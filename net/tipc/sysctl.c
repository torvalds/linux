/*
 * net/tipc/sysctl.c: sysctl interface to TIPC subsystem
 *
 * Copyright (c) 2013, Wind River Systems
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the names of the copyright holders nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "core.h"
#include "trace.h"
#include "crypto.h"
#include "bcast.h"
#include <linux/sysctl.h>

static struct ctl_table_header *tipc_ctl_hdr;

static struct ctl_table tipc_table[] = {
	{
		.procname	= "tipc_rmem",
		.data		= &sysctl_tipc_rmem,
		.maxlen		= sizeof(sysctl_tipc_rmem),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1         = SYSCTL_ONE,
	},
	{
		.procname	= "named_timeout",
		.data		= &sysctl_tipc_named_timeout,
		.maxlen		= sizeof(sysctl_tipc_named_timeout),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1         = SYSCTL_ZERO,
	},
	{
		.procname       = "sk_filter",
		.data           = &sysctl_tipc_sk_filter,
		.maxlen         = sizeof(sysctl_tipc_sk_filter),
		.mode           = 0644,
		.proc_handler   = proc_doulongvec_minmax,
	},
#ifdef CONFIG_TIPC_CRYPTO
	{
		.procname	= "max_tfms",
		.data		= &sysctl_tipc_max_tfms,
		.maxlen		= sizeof(sysctl_tipc_max_tfms),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1         = SYSCTL_ONE,
	},
	{
		.procname	= "key_exchange_enabled",
		.data		= &sysctl_tipc_key_exchange_enabled,
		.maxlen		= sizeof(sysctl_tipc_key_exchange_enabled),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1         = SYSCTL_ZERO,
		.extra2         = SYSCTL_ONE,
	},
#endif
	{
		.procname	= "bc_retruni",
		.data		= &sysctl_tipc_bc_retruni,
		.maxlen		= sizeof(sysctl_tipc_bc_retruni),
		.mode		= 0644,
		.proc_handler	= proc_doulongvec_minmax,
	},
	{}
};

int tipc_register_sysctl(void)
{
	tipc_ctl_hdr = register_net_sysctl(&init_net, "net/tipc", tipc_table);
	if (tipc_ctl_hdr == NULL)
		return -ENOMEM;
	return 0;
}

void tipc_unregister_sysctl(void)
{
	unregister_net_sysctl_table(tipc_ctl_hdr);
}
