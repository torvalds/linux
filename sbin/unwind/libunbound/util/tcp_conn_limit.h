/*
 * daemon/tcp_conn_limit.h - client TCP connection limit storage for the server.
 *
 * Copyright (c) 2018, NLnet Labs. All rights reserved.
 *
 * This software is open source.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * Neither the name of the NLNET LABS nor the names of its contributors may
 * be used to endorse or promote products derived from this software without
 * specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * \file
 *
 * This file keeps track of the limit on the number of TCP connections
 * each client makes the server.
 */

#ifndef DAEMON_TCP_CONN_LIMIT_H
#define DAEMON_TCP_CONN_LIMIT_H
#include "util/storage/dnstree.h"
#include "util/locks.h"
struct config_file;
struct regional;

/**
 * TCP connection limit storage structure
 */
struct tcl_list {
	/** regional for allocation */
	struct regional* region;
	/**
	 * Tree of the addresses that are TCP connection limited.
	 * contents of type tcl_addr.
	 */
	rbtree_type tree;
};

/**
 *
 * An address span with connection limit information
 */
struct tcl_addr {
	/** node in address tree */
	struct addr_tree_node node;
	/** lock on structure data */
	lock_quick_type lock;
	/** connection limit on this netblock */
	uint32_t limit;
	/** current connection count on this netblock */
	uint32_t count;
};

/**
 * Create TCP connection limit structure
 * @return new structure or NULL on error.
 */
struct tcl_list* tcl_list_create(void);

/**
 * Delete TCP connection limit structure.
 * @param tcl: to delete.
 */
void tcl_list_delete(struct tcl_list* tcl);

/**
 * Process TCP connection limit config.
 * @param tcl: where to store.
 * @param cfg: config options.
 * @return 0 on error.
 */
int tcl_list_apply_cfg(struct tcl_list* tcl, struct config_file* cfg);

/**
 * Increment TCP connection count if found, provided the
 * count was below the limit.
 * @param tcl: structure for tcl storage, or NULL.
 * @return: 0 if limit reached, 1 if tcl was NULL or limit not reached.
 */
int tcl_new_connection(struct tcl_addr* tcl);

/**
 * Decrement TCP connection count if found.
 * @param tcl: structure for tcl storage, or NULL.
 */
void tcl_close_connection(struct tcl_addr* tcl);

/**
 * Lookup address to see its TCP connection limit structure
 * @param tcl: structure for address storage.
 * @param addr: address to check
 * @param addrlen: length of addr.
 * @return: tcl structure from this address.
 */
struct tcl_addr*
tcl_addr_lookup(struct tcl_list* tcl, struct sockaddr_storage* addr,
        socklen_t addrlen);

/**
 * Get memory used by TCP connection limit structure.
 * @param tcl: structure for address storage.
 * @return bytes in use.
 */
size_t tcl_list_get_mem(struct tcl_list* tcl);

/**
 * Swap internal tree with preallocated entries. Caller should manage
 * tcl_addr item locks.
 * @param tcl: the tcp connection list structure.
 * @param data: the data structure used to take elements from. This contains
 * 	the old elements on return.
 */
void tcl_list_swap_tree(struct tcl_list* tcl, struct tcl_list* data);

#endif /* DAEMON_TCP_CONN_LIMIT_H */
