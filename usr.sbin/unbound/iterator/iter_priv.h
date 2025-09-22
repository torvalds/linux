/*
 * iterator/iter_priv.h - iterative resolver private address and domain store
 *
 * Copyright (c) 2008, NLnet Labs. All rights reserved.
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
 * Keep track of the private addresses and lookup fast.
 */

#ifndef ITERATOR_ITER_PRIV_H
#define ITERATOR_ITER_PRIV_H
#include "util/rbtree.h"
struct sldns_buffer;
struct iter_env;
struct config_file;
struct regional;
struct rrset_parse;

/**
 * Iterator priv structure
 */
struct iter_priv {
	/** regional for allocation */
	struct regional* region;
	/** 
	 * Tree of the address spans that are blocked.
	 * contents of type addr_tree_node.
	 * No further data need, only presence or absence.
	 */
	rbtree_type a;
	/** 
	 * Tree of the domains spans that are allowed to contain
	 * the blocked address spans.
	 * contents of type name_tree_node.
	 * No further data need, only presence or absence.
	 */
	rbtree_type n;
};

/**
 * Create priv structure 
 * @return new structure or NULL on error.
 */
struct iter_priv* priv_create(void);

/**
 * Delete priv structure.
 * @param priv: to delete.
 */
void priv_delete(struct iter_priv* priv);

/**
 * Process priv config.
 * @param priv: where to store.
 * @param cfg: config options.
 * @return 0 on error.
 */
int priv_apply_cfg(struct iter_priv* priv, struct config_file* cfg);

/**
 * See if rrset is bad.
 * Will remove individual RRs that are bad (if possible) to
 * sanitize the RRset without removing it completely.
 * @param priv: structure for private address storage.
 * @param pkt: packet to decompress rrset name in.
 * @param rrset: the rrset to examine, A or AAAA.
 * @return true if the rrset is bad and should be removed.
 */
int priv_rrset_bad(struct iter_priv* priv, struct sldns_buffer* pkt, 
	struct rrset_parse* rrset);

/**
 * Get memory used by priv structure.
 * @param priv: structure for address storage.
 * @return bytes in use.
 */
size_t priv_get_mem(struct iter_priv* priv);

#endif /* ITERATOR_ITER_PRIV_H */
