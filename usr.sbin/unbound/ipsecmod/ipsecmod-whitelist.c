/*
 * ipsecmod/ipsecmod-whitelist.h - White listed domains for the ipsecmod to
 * operate on.
 *
 * Copyright (c) 2017, NLnet Labs. All rights reserved.
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
 * Keep track of the white listed domains for ipsecmod.
 */

#include "config.h"

#ifdef USE_IPSECMOD
#include "ipsecmod/ipsecmod.h"
#include "ipsecmod/ipsecmod-whitelist.h"
#include "util/regional.h"
#include "util/log.h"
#include "util/config_file.h"
#include "util/rbtree.h"
#include "util/data/dname.h"
#include "util/storage/dnstree.h"
#include "sldns/str2wire.h"

/** Apply ipsecmod-whitelist string. */
static int
whitelist_str_cfg(rbtree_type* whitelist, const char* name)
{
    struct name_tree_node* n;
    size_t len;
    uint8_t* nm = sldns_str2wire_dname(name, &len);
    if(!nm) {
        log_err("ipsecmod: could not parse %s for whitelist.", name);
        return 0;
    }
    n = (struct name_tree_node*)calloc(1, sizeof(*n));
    if(!n) {
        log_err("ipsecmod: out of memory while creating whitelist.");
        free(nm);
        return 0;
    }
    n->node.key = n;
    n->name = nm;
    n->len = len;
    n->labs = dname_count_labels(nm);
    n->dclass = LDNS_RR_CLASS_IN;
    if(!name_tree_insert(whitelist, n, nm, len, n->labs, n->dclass)) {
        /* duplicate element ignored, idempotent */
        free(n->name);
        free(n);
    }
    return 1;
}

/** Read ipsecmod-whitelist config. */
static int
read_whitelist(rbtree_type* whitelist, struct config_file* cfg)
{
	struct config_strlist* p;
	for(p = cfg->ipsecmod_whitelist; p; p = p->next) {
		log_assert(p->str);
		if(!whitelist_str_cfg(whitelist, p->str))
			return 0;
	}
	return 1;
}

int
ipsecmod_whitelist_apply_cfg(struct ipsecmod_env* ie,
	struct config_file* cfg)
{
	ie->whitelist = rbtree_create(name_tree_compare);
	if(!read_whitelist(ie->whitelist, cfg))
		return 0;
	name_tree_init_parents(ie->whitelist);
	return 1;
}

/** Delete ipsecmod_env->whitelist element. */
static void
whitelist_free(struct rbnode_type* n, void* ATTR_UNUSED(d))
{
	if(n) {
		free(((struct name_tree_node*)n)->name);
		free(n);
	}
}

/** Get memory usage of ipsecmod_env->whitelist element. */
static void
whitelist_get_mem(struct rbnode_type* n, void* arg)
{
	struct name_tree_node* node = (struct name_tree_node*)n;
	size_t* size = (size_t*) arg;
	if(node) {
		*size += sizeof(node) + node->len;
	}
}

void
ipsecmod_whitelist_delete(rbtree_type* whitelist)
{
	if(whitelist) {
		traverse_postorder(whitelist, whitelist_free, NULL);
		free(whitelist);
	}
}

int
ipsecmod_domain_is_whitelisted(struct ipsecmod_env* ie, uint8_t* dname,
	size_t dname_len, uint16_t qclass)
{
	if(!ie->whitelist) return 1; /* No whitelist, treat as whitelisted. */
	return name_tree_lookup(ie->whitelist, dname, dname_len,
		dname_count_labels(dname), qclass) != NULL;
}

size_t
ipsecmod_whitelist_get_mem(rbtree_type* whitelist)
{
	size_t size = 0;
	if(whitelist) {
		traverse_postorder(whitelist, whitelist_get_mem, &size);
	}
	return size;
}

#endif /* USE_IPSECMOD */
