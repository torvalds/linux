/*
 * net/tipc/core.c: TIPC module code
 *
 * Copyright (c) 2003-2006, Ericsson AB
 * Copyright (c) 2005, Wind River Systems
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

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/random.h>

#include "core.h"
#include "dbg.h"
#include "ref.h"
#include "net.h"
#include "user_reg.h"
#include "name_table.h"
#include "subscr.h"
#include "config.h"

int  eth_media_start(void);
void eth_media_stop(void);
int  handler_start(void);
void handler_stop(void);
int  socket_init(void);
void socket_stop(void);
int  netlink_start(void);
void netlink_stop(void);

#define MOD_NAME "tipc_start: "

#ifndef CONFIG_TIPC_ZONES
#define CONFIG_TIPC_ZONES 3
#endif

#ifndef CONFIG_TIPC_CLUSTERS
#define CONFIG_TIPC_CLUSTERS 1
#endif

#ifndef CONFIG_TIPC_NODES
#define CONFIG_TIPC_NODES 255
#endif

#ifndef CONFIG_TIPC_SLAVE_NODES
#define CONFIG_TIPC_SLAVE_NODES 0
#endif

#ifndef CONFIG_TIPC_PORTS
#define CONFIG_TIPC_PORTS 8191
#endif

#ifndef CONFIG_TIPC_LOG
#define CONFIG_TIPC_LOG 0
#endif

/* global variables used by multiple sub-systems within TIPC */

int tipc_mode = TIPC_NOT_RUNNING;
int tipc_random;
atomic_t tipc_user_count = ATOMIC_INIT(0);

const char tipc_alphabet[] = 
	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_";

/* configurable TIPC parameters */

u32 tipc_own_addr;
int tipc_max_zones;
int tipc_max_clusters;
int tipc_max_nodes;
int tipc_max_slaves;
int tipc_max_ports;
int tipc_max_subscriptions;
int tipc_max_publications;
int tipc_net_id;
int tipc_remote_management;


int tipc_get_mode(void)
{
	return tipc_mode;
}

/**
 * stop_net - shut down TIPC networking sub-systems
 */

void stop_net(void)
{
	eth_media_stop();
	tipc_stop_net();
}

/**
 * start_net - start TIPC networking sub-systems
 */

int start_net(void)
{
	int res;

	if ((res = tipc_start_net()) ||
	    (res = eth_media_start())) {
		stop_net();
	}
	return res;
}

/**
 * stop_core - switch TIPC from SINGLE NODE to NOT RUNNING mode
 */

void stop_core(void)
{
	if (tipc_mode != TIPC_NODE_MODE)
		return;

	tipc_mode = TIPC_NOT_RUNNING;

	netlink_stop();
	handler_stop();
	cfg_stop();
	subscr_stop();
	reg_stop();
	nametbl_stop();
	ref_table_stop();
	socket_stop();
}

/**
 * start_core - switch TIPC from NOT RUNNING to SINGLE NODE mode
 */

int start_core(void)
{
	int res;

	if (tipc_mode != TIPC_NOT_RUNNING)
		return -ENOPROTOOPT;

	get_random_bytes(&tipc_random, sizeof(tipc_random));
	tipc_mode = TIPC_NODE_MODE;

	if ((res = handler_start()) || 
	    (res = ref_table_init(tipc_max_ports + tipc_max_subscriptions,
				  tipc_random)) ||
	    (res = reg_start()) ||
	    (res = nametbl_init()) ||
            (res = k_signal((Handler)subscr_start, 0)) ||
	    (res = k_signal((Handler)cfg_init, 0)) || 
	    (res = netlink_start()) ||
	    (res = socket_init())) {
		stop_core();
	}
	return res;
}


static int __init tipc_init(void)
{
	int res;

	log_reinit(CONFIG_TIPC_LOG);
	info("Activated (compiled " __DATE__ " " __TIME__ ")\n");

	tipc_own_addr = 0;
	tipc_remote_management = 1;
	tipc_max_publications = 10000;
	tipc_max_subscriptions = 2000;
	tipc_max_ports = delimit(CONFIG_TIPC_PORTS, 127, 65536);
	tipc_max_zones = delimit(CONFIG_TIPC_ZONES, 1, 511);
	tipc_max_clusters = delimit(CONFIG_TIPC_CLUSTERS, 1, 1);
	tipc_max_nodes = delimit(CONFIG_TIPC_NODES, 8, 2047);
	tipc_max_slaves = delimit(CONFIG_TIPC_SLAVE_NODES, 0, 2047);
	tipc_net_id = 4711;

	if ((res = start_core()))
		err("Unable to start in single node mode\n");
	else	
		info("Started in single node mode\n");
        return res;
}

static void __exit tipc_exit(void)
{
	stop_net();
	stop_core();
	info("Deactivated\n");
	log_stop();
}

module_init(tipc_init);
module_exit(tipc_exit);

MODULE_DESCRIPTION("TIPC: Transparent Inter Process Communication");
MODULE_LICENSE("Dual BSD/GPL");

/* Native TIPC API for kernel-space applications (see tipc.h) */

EXPORT_SYMBOL(tipc_attach);
EXPORT_SYMBOL(tipc_detach);
EXPORT_SYMBOL(tipc_get_addr);
EXPORT_SYMBOL(tipc_get_mode);
EXPORT_SYMBOL(tipc_createport);
EXPORT_SYMBOL(tipc_deleteport);
EXPORT_SYMBOL(tipc_ownidentity);
EXPORT_SYMBOL(tipc_portimportance);
EXPORT_SYMBOL(tipc_set_portimportance);
EXPORT_SYMBOL(tipc_portunreliable);
EXPORT_SYMBOL(tipc_set_portunreliable);
EXPORT_SYMBOL(tipc_portunreturnable);
EXPORT_SYMBOL(tipc_set_portunreturnable);
EXPORT_SYMBOL(tipc_publish);
EXPORT_SYMBOL(tipc_withdraw);
EXPORT_SYMBOL(tipc_connect2port);
EXPORT_SYMBOL(tipc_disconnect);
EXPORT_SYMBOL(tipc_shutdown);
EXPORT_SYMBOL(tipc_isconnected);
EXPORT_SYMBOL(tipc_peer);
EXPORT_SYMBOL(tipc_ref_valid);
EXPORT_SYMBOL(tipc_send);
EXPORT_SYMBOL(tipc_send_buf);
EXPORT_SYMBOL(tipc_send2name);
EXPORT_SYMBOL(tipc_forward2name);
EXPORT_SYMBOL(tipc_send_buf2name);
EXPORT_SYMBOL(tipc_forward_buf2name);
EXPORT_SYMBOL(tipc_send2port);
EXPORT_SYMBOL(tipc_forward2port);
EXPORT_SYMBOL(tipc_send_buf2port);
EXPORT_SYMBOL(tipc_forward_buf2port);
EXPORT_SYMBOL(tipc_multicast);
/* EXPORT_SYMBOL(tipc_multicast_buf); not available yet */
EXPORT_SYMBOL(tipc_ispublished);
EXPORT_SYMBOL(tipc_available_nodes);

/* TIPC API for external bearers (see tipc_bearer.h) */

EXPORT_SYMBOL(tipc_block_bearer);
EXPORT_SYMBOL(tipc_continue); 
EXPORT_SYMBOL(tipc_disable_bearer);
EXPORT_SYMBOL(tipc_enable_bearer);
EXPORT_SYMBOL(tipc_recv_msg);
EXPORT_SYMBOL(tipc_register_media); 

/* TIPC API for external APIs (see tipc_port.h) */

EXPORT_SYMBOL(tipc_createport_raw);
EXPORT_SYMBOL(tipc_set_msg_option);
EXPORT_SYMBOL(tipc_reject_msg);
EXPORT_SYMBOL(tipc_send_buf_fast);
EXPORT_SYMBOL(tipc_acknowledge);
EXPORT_SYMBOL(tipc_get_port);
EXPORT_SYMBOL(tipc_get_handle);

