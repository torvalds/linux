/*
 * net/tipc/core.c: TIPC module code
 *
 * Copyright (c) 2003-2006, 2013, Ericsson AB
 * Copyright (c) 2005-2006, 2010-2013, Wind River Systems
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

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include "core.h"
#include "name_table.h"
#include "subscr.h"
#include "bearer.h"
#include "net.h"
#include "socket.h"
#include "bcast.h"

#include <linux/module.h>

/* configurable TIPC parameters */
int tipc_net_id __read_mostly;
int sysctl_tipc_rmem[3] __read_mostly;	/* min/default/max */

static int __net_init tipc_init_net(struct net *net)
{
	struct tipc_net *tn = net_generic(net, tipc_net_id);
	int err;

	tn->net_id = 4711;
	tn->own_addr = 0;
	tn->mon_threshold = TIPC_DEF_MON_THRESHOLD;
	get_random_bytes(&tn->random, sizeof(int));
	INIT_LIST_HEAD(&tn->node_list);
	spin_lock_init(&tn->node_list_lock);

	err = tipc_sk_rht_init(net);
	if (err)
		goto out_sk_rht;

	err = tipc_nametbl_init(net);
	if (err)
		goto out_nametbl;

	INIT_LIST_HEAD(&tn->dist_queue);
	err = tipc_topsrv_start(net);
	if (err)
		goto out_subscr;

	err = tipc_bcast_init(net);
	if (err)
		goto out_bclink;

	return 0;

out_bclink:
	tipc_bcast_stop(net);
out_subscr:
	tipc_nametbl_stop(net);
out_nametbl:
	tipc_sk_rht_destroy(net);
out_sk_rht:
	return err;
}

static void __net_exit tipc_exit_net(struct net *net)
{
	tipc_topsrv_stop(net);
	tipc_net_stop(net);
	tipc_bcast_stop(net);
	tipc_nametbl_stop(net);
	tipc_sk_rht_destroy(net);
}

static struct pernet_operations tipc_net_ops = {
	.init = tipc_init_net,
	.exit = tipc_exit_net,
	.id   = &tipc_net_id,
	.size = sizeof(struct tipc_net),
};

static int __init tipc_init(void)
{
	int err;

	pr_info("Activated (version " TIPC_MOD_VER ")\n");

	sysctl_tipc_rmem[0] = RCVBUF_MIN;
	sysctl_tipc_rmem[1] = RCVBUF_DEF;
	sysctl_tipc_rmem[2] = RCVBUF_MAX;

	err = tipc_netlink_start();
	if (err)
		goto out_netlink;

	err = tipc_netlink_compat_start();
	if (err)
		goto out_netlink_compat;

	err = tipc_socket_init();
	if (err)
		goto out_socket;

	err = tipc_register_sysctl();
	if (err)
		goto out_sysctl;

	err = register_pernet_subsys(&tipc_net_ops);
	if (err)
		goto out_pernet;

	err = tipc_bearer_setup();
	if (err)
		goto out_bearer;

	pr_info("Started in single node mode\n");
	return 0;
out_bearer:
	unregister_pernet_subsys(&tipc_net_ops);
out_pernet:
	tipc_unregister_sysctl();
out_sysctl:
	tipc_socket_stop();
out_socket:
	tipc_netlink_compat_stop();
out_netlink_compat:
	tipc_netlink_stop();
out_netlink:
	pr_err("Unable to start in single node mode\n");
	return err;
}

static void __exit tipc_exit(void)
{
	tipc_bearer_cleanup();
	unregister_pernet_subsys(&tipc_net_ops);
	tipc_netlink_stop();
	tipc_netlink_compat_stop();
	tipc_socket_stop();
	tipc_unregister_sysctl();

	pr_info("Deactivated\n");
}

module_init(tipc_init);
module_exit(tipc_exit);

MODULE_DESCRIPTION("TIPC: Transparent Inter Process Communication");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_VERSION(TIPC_MOD_VER);
