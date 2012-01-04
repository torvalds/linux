/*
 * net/tipc/core.c: TIPC module code
 *
 * Copyright (c) 2003-2006, Ericsson AB
 * Copyright (c) 2005-2006, 2010-2011, Wind River Systems
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

#include <linux/module.h>

#include "core.h"
#include "ref.h"
#include "name_table.h"
#include "subscr.h"
#include "config.h"


#ifndef CONFIG_TIPC_PORTS
#define CONFIG_TIPC_PORTS 8191
#endif

#ifndef CONFIG_TIPC_LOG
#define CONFIG_TIPC_LOG 0
#endif

/* global variables used by multiple sub-systems within TIPC */

int tipc_mode = TIPC_NOT_RUNNING;
int tipc_random;

const char tipc_alphabet[] =
	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_.";

/* configurable TIPC parameters */

u32 tipc_own_addr;
int tipc_max_ports;
int tipc_max_subscriptions;
int tipc_max_publications;
int tipc_net_id;
int tipc_remote_management;


/**
 * tipc_buf_acquire - creates a TIPC message buffer
 * @size: message size (including TIPC header)
 *
 * Returns a new buffer with data pointers set to the specified size.
 *
 * NOTE: Headroom is reserved to allow prepending of a data link header.
 *       There may also be unrequested tailroom present at the buffer's end.
 */

struct sk_buff *tipc_buf_acquire(u32 size)
{
	struct sk_buff *skb;
	unsigned int buf_size = (BUF_HEADROOM + size + 3) & ~3u;

	skb = alloc_skb_fclone(buf_size, GFP_ATOMIC);
	if (skb) {
		skb_reserve(skb, BUF_HEADROOM);
		skb_put(skb, size);
		skb->next = NULL;
	}
	return skb;
}

/**
 * tipc_core_stop_net - shut down TIPC networking sub-systems
 */

static void tipc_core_stop_net(void)
{
	tipc_eth_media_stop();
	tipc_net_stop();
}

/**
 * start_net - start TIPC networking sub-systems
 */

int tipc_core_start_net(unsigned long addr)
{
	int res;

	res = tipc_net_start(addr);
	if (!res)
		res = tipc_eth_media_start();
	if (res)
		tipc_core_stop_net();
	return res;
}

/**
 * tipc_core_stop - switch TIPC from SINGLE NODE to NOT RUNNING mode
 */

static void tipc_core_stop(void)
{
	if (tipc_mode != TIPC_NODE_MODE)
		return;

	tipc_mode = TIPC_NOT_RUNNING;

	tipc_netlink_stop();
	tipc_handler_stop();
	tipc_cfg_stop();
	tipc_subscr_stop();
	tipc_nametbl_stop();
	tipc_ref_table_stop();
	tipc_socket_stop();
	tipc_log_resize(0);
}

/**
 * tipc_core_start - switch TIPC from NOT RUNNING to SINGLE NODE mode
 */

static int tipc_core_start(void)
{
	int res;

	if (tipc_mode != TIPC_NOT_RUNNING)
		return -ENOPROTOOPT;

	get_random_bytes(&tipc_random, sizeof(tipc_random));
	tipc_mode = TIPC_NODE_MODE;

	res = tipc_handler_start();
	if (!res)
		res = tipc_ref_table_init(tipc_max_ports, tipc_random);
	if (!res)
		res = tipc_nametbl_init();
	if (!res)
		res = tipc_k_signal((Handler)tipc_subscr_start, 0);
	if (!res)
		res = tipc_k_signal((Handler)tipc_cfg_init, 0);
	if (!res)
		res = tipc_netlink_start();
	if (!res)
		res = tipc_socket_init();
	if (res)
		tipc_core_stop();

	return res;
}


static int __init tipc_init(void)
{
	int res;

	if (tipc_log_resize(CONFIG_TIPC_LOG) != 0)
		warn("Unable to create log buffer\n");

	info("Activated (version " TIPC_MOD_VER ")\n");

	tipc_own_addr = 0;
	tipc_remote_management = 1;
	tipc_max_publications = 10000;
	tipc_max_subscriptions = 2000;
	tipc_max_ports = CONFIG_TIPC_PORTS;
	tipc_net_id = 4711;

	res = tipc_core_start();
	if (res)
		err("Unable to start in single node mode\n");
	else
		info("Started in single node mode\n");
	return res;
}

static void __exit tipc_exit(void)
{
	tipc_core_stop_net();
	tipc_core_stop();
	info("Deactivated\n");
}

module_init(tipc_init);
module_exit(tipc_exit);

MODULE_DESCRIPTION("TIPC: Transparent Inter Process Communication");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_VERSION(TIPC_MOD_VER);
