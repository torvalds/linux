/*
 * net/tipc/addr.c: TIPC address utility routines
 *
 * Copyright (c) 2000-2006, 2018, Ericsson AB
 * Copyright (c) 2004-2005, 2010-2011, Wind River Systems
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

#include "addr.h"
#include "core.h"

bool tipc_in_scope(bool legacy_format, u32 domain, u32 addr)
{
	if (!domain || (domain == addr))
		return true;
	if (!legacy_format)
		return false;
	if (domain == tipc_cluster_mask(addr)) /* domain <Z.C.0> */
		return true;
	if (domain == (addr & TIPC_ZONE_CLUSTER_MASK)) /* domain <Z.C.0> */
		return true;
	if (domain == (addr & TIPC_ZONE_MASK)) /* domain <Z.0.0> */
		return true;
	return false;
}

void tipc_set_node_id(struct net *net, u8 *id)
{
	struct tipc_net *tn = tipc_net(net);
	u32 *tmp = (u32 *)id;

	memcpy(tn->node_id, id, NODE_ID_LEN);
	tipc_nodeid2string(tn->node_id_string, id);
	tn->trial_addr = tmp[0] ^ tmp[1] ^ tmp[2] ^ tmp[3];
	pr_info("Own node identity %s, cluster identity %u\n",
		tipc_own_id_string(net), tn->net_id);
}

void tipc_set_node_addr(struct net *net, u32 addr)
{
	struct tipc_net *tn = tipc_net(net);
	u8 node_id[NODE_ID_LEN] = {0,};

	tn->node_addr = addr;
	if (!tipc_own_id(net)) {
		sprintf(node_id, "%x", addr);
		tipc_set_node_id(net, node_id);
	}
	tn->trial_addr = addr;
	pr_info("32-bit node address hash set to %x\n", addr);
}

char *tipc_nodeid2string(char *str, u8 *id)
{
	int i;
	u8 c;

	/* Already a string ? */
	for (i = 0; i < NODE_ID_LEN; i++) {
		c = id[i];
		if (c >= '0' && c <= '9')
			continue;
		if (c >= 'A' && c <= 'Z')
			continue;
		if (c >= 'a' && c <= 'z')
			continue;
		if (c == '.')
			continue;
		if (c == ':')
			continue;
		if (c == '_')
			continue;
		if (c == '-')
			continue;
		if (c == '@')
			continue;
		if (c != 0)
			break;
	}
	if (i == NODE_ID_LEN) {
		memcpy(str, id, NODE_ID_LEN);
		str[NODE_ID_LEN] = 0;
		return str;
	}

	/* Translate to hex string */
	for (i = 0; i < NODE_ID_LEN; i++)
		sprintf(&str[2 * i], "%02x", id[i]);

	/* Strip off trailing zeroes */
	for (i = NODE_ID_STR_LEN - 2; str[i] == '0'; i--)
		str[i] = 0;

	return str;
}
