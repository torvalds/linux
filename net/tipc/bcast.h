/*
 * net/tipc/bcast.h: Include file for TIPC broadcast code
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

#ifndef _TIPC_BCAST_H
#define _TIPC_BCAST_H

#define MAX_NODES 4096
#define WSIZE 32

/**
 * struct node_map - set of node identifiers
 * @count: # of nodes in set
 * @map: bitmap of node identifiers that are in the set
 */

struct node_map {
	u32 count;
	u32 map[MAX_NODES / WSIZE];
};


#define PLSIZE 32

/**
 * struct port_list - set of node local destination ports
 * @count: # of ports in set (only valid for first entry in list)
 * @next: pointer to next entry in list
 * @ports: array of port references
 */

struct port_list {
	int count;
	struct port_list *next;
	u32 ports[PLSIZE];
};


struct node;

extern char tipc_bclink_name[];


/**
 * nmap_get - determine if node exists in a node map
 */

static inline int tipc_nmap_get(struct node_map *nm_ptr, u32 node)
{
	int n = tipc_node(node);
	int w = n / WSIZE;
	int b = n % WSIZE;

	return nm_ptr->map[w] & (1 << b);
}

/**
 * nmap_add - add a node to a node map
 */

static inline void tipc_nmap_add(struct node_map *nm_ptr, u32 node)
{
	int n = tipc_node(node);
	int w = n / WSIZE;
	u32 mask = (1 << (n % WSIZE));

	if ((nm_ptr->map[w] & mask) == 0) {
		nm_ptr->count++;
		nm_ptr->map[w] |= mask;
	}
}

/** 
 * nmap_remove - remove a node from a node map
 */

static inline void tipc_nmap_remove(struct node_map *nm_ptr, u32 node)
{
	int n = tipc_node(node);
	int w = n / WSIZE;
	u32 mask = (1 << (n % WSIZE));

	if ((nm_ptr->map[w] & mask) != 0) {
		nm_ptr->map[w] &= ~mask;
		nm_ptr->count--;
	}
}

/**
 * nmap_equal - test for equality of node maps
 */

static inline int tipc_nmap_equal(struct node_map *nm_a, struct node_map *nm_b)
{
	return !memcmp(nm_a, nm_b, sizeof(*nm_a));
}

/**
 * nmap_diff - find differences between node maps
 * @nm_a: input node map A
 * @nm_b: input node map B
 * @nm_diff: output node map A-B (i.e. nodes of A that are not in B)
 */

static inline void tipc_nmap_diff(struct node_map *nm_a, struct node_map *nm_b,
				  struct node_map *nm_diff)
{
	int stop = sizeof(nm_a->map) / sizeof(u32);
	int w;
	int b;
	u32 map;

	memset(nm_diff, 0, sizeof(*nm_diff));
	for (w = 0; w < stop; w++) {
		map = nm_a->map[w] ^ (nm_a->map[w] & nm_b->map[w]);
		nm_diff->map[w] = map;
		if (map != 0) {
			for (b = 0 ; b < WSIZE; b++) {
				if (map & (1 << b))
					nm_diff->count++;
			}
		}
	}
}

/**
 * port_list_add - add a port to a port list, ensuring no duplicates
 */

static inline void tipc_port_list_add(struct port_list *pl_ptr, u32 port)
{
	struct port_list *item = pl_ptr;
	int i;
	int item_sz = PLSIZE;
	int cnt = pl_ptr->count;

	for (; ; cnt -= item_sz, item = item->next) {
		if (cnt < PLSIZE)
			item_sz = cnt;
		for (i = 0; i < item_sz; i++)
			if (item->ports[i] == port)
				return;
		if (i < PLSIZE) {
			item->ports[i] = port;
			pl_ptr->count++;
			return;
		}
		if (!item->next) {
			item->next = kmalloc(sizeof(*item), GFP_ATOMIC);
			if (!item->next) {
				warn("Memory squeeze: multicast destination port list is incomplete\n");
				return;
			}
			item->next->next = NULL;
		}
	}
}

/**
 * port_list_free - free dynamically created entries in port_list chain
 * 
 * Note: First item is on stack, so it doesn't need to be released
 */

static inline void tipc_port_list_free(struct port_list *pl_ptr)
{
	struct port_list *item;
	struct port_list *next;

	for (item = pl_ptr->next; item; item = next) {
		next = item->next;
		kfree(item);
	}
}


int  tipc_bclink_init(void);
void tipc_bclink_stop(void);
void tipc_bclink_acknowledge(struct node *n_ptr, u32 acked);
int  tipc_bclink_send_msg(struct sk_buff *buf);
void tipc_bclink_recv_pkt(struct sk_buff *buf);
u32  tipc_bclink_get_last_sent(void);
u32  tipc_bclink_acks_missing(struct node *n_ptr);
void tipc_bclink_check_gap(struct node *n_ptr, u32 seqno);
int  tipc_bclink_stats(char *stats_buf, const u32 buf_size);
int  tipc_bclink_reset_stats(void);
int  tipc_bclink_set_queue_limits(u32 limit);
void tipc_bcbearer_sort(void);
void tipc_bcbearer_push(void);

#endif
