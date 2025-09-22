/*
 * iterator/iter_donotq.h - iterative resolver donotqueryaddresses storage.
 *
 * Copyright (c) 2007, NLnet Labs. All rights reserved.
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
 * This file contains functions to assist the iterator module.
 * Keep track of the donotquery addresses and lookup fast.
 */

#ifndef ITERATOR_ITER_DONOTQ_H
#define ITERATOR_ITER_DONOTQ_H
#include "util/storage/dnstree.h"
struct iter_env;
struct config_file;
struct regional;

/**
 * Iterator donotqueryaddresses structure
 */
struct iter_donotq {
	/** regional for allocation */
	struct regional* region;
	/** 
	 * Tree of the address spans that are blocked.
	 * contents of type addr_tree_node. Each node is an address span 
	 * that must not be used to send queries to.
	 */
	rbtree_type tree;
};

/**
 * Create donotqueryaddresses structure 
 * @return new structure or NULL on error.
 */
struct iter_donotq* donotq_create(void);

/**
 * Delete donotqueryaddresses structure.
 * @param donotq: to delete.
 */
void donotq_delete(struct iter_donotq* donotq);

/**
 * Process donotqueryaddresses config.
 * @param donotq: where to store.
 * @param cfg: config options.
 * @return 0 on error.
 */
int donotq_apply_cfg(struct iter_donotq* donotq, struct config_file* cfg);

/**
 * See if an address is blocked.
 * @param donotq: structure for address storage.
 * @param addr: address to check
 * @param addrlen: length of addr.
 * @return: true if the address must not be queried. false if unlisted.
 */
int donotq_lookup(struct iter_donotq* donotq, struct sockaddr_storage* addr,
	socklen_t addrlen);

/**
 * Get memory used by donotqueryaddresses structure.
 * @param donotq: structure for address storage.
 * @return bytes in use.
 */
size_t donotq_get_mem(struct iter_donotq* donotq);

#endif /* ITERATOR_ITER_DONOTQ_H */
