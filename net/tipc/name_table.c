/*
 * net/tipc/name_table.c: TIPC name table code
 * 
 * Copyright (c) 2000-2006, Ericsson AB
 * Copyright (c) 2004-2005, Wind River Systems
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
#include "config.h"
#include "dbg.h"
#include "name_table.h"
#include "name_distr.h"
#include "addr.h"
#include "node_subscr.h"
#include "subscr.h"
#include "port.h"
#include "cluster.h"
#include "bcast.h"

static int tipc_nametbl_size = 1024;		/* must be a power of 2 */

/**
 * struct sub_seq - container for all published instances of a name sequence
 * @lower: name sequence lower bound
 * @upper: name sequence upper bound
 * @node_list: circular list of matching publications with >= node scope
 * @cluster_list: circular list of matching publications with >= cluster scope
 * @zone_list: circular list of matching publications with >= zone scope
 */

struct sub_seq {
	u32 lower;
	u32 upper;
	struct publication *node_list;
	struct publication *cluster_list;
	struct publication *zone_list;
};

/** 
 * struct name_seq - container for all published instances of a name type
 * @type: 32 bit 'type' value for name sequence
 * @sseq: pointer to dynamically-sized array of sub-sequences of this 'type';
 *        sub-sequences are sorted in ascending order
 * @alloc: number of sub-sequences currently in array
 * @first_free: upper bound of highest sub-sequence + 1
 * @ns_list: links to adjacent name sequences in hash chain
 * @subscriptions: list of subscriptions for this 'type'
 * @lock: spinlock controlling access to name sequence structure
 */

struct name_seq {
	u32 type;
	struct sub_seq *sseqs;
	u32 alloc;
	u32 first_free;
	struct hlist_node ns_list;
	struct list_head subscriptions;
	spinlock_t lock;
};

/**
 * struct name_table - table containing all existing port name publications
 * @types: pointer to fixed-sized array of name sequence lists, 
 *         accessed via hashing on 'type'; name sequence lists are *not* sorted
 * @local_publ_count: number of publications issued by this node
 */

struct name_table {
	struct hlist_head *types;
	u32 local_publ_count;
};

static struct name_table table = { NULL } ;
static atomic_t rsv_publ_ok = ATOMIC_INIT(0);
rwlock_t tipc_nametbl_lock = RW_LOCK_UNLOCKED;


static int hash(int x)
{
	return(x & (tipc_nametbl_size - 1));
}

/**
 * publ_create - create a publication structure
 */

static struct publication *publ_create(u32 type, u32 lower, u32 upper, 
				       u32 scope, u32 node, u32 port_ref,   
				       u32 key)
{
	struct publication *publ =
		(struct publication *)kmalloc(sizeof(*publ), GFP_ATOMIC);
	if (publ == NULL) {
		warn("Memory squeeze; failed to create publication\n");
		return NULL;
	}

	memset(publ, 0, sizeof(*publ));
	publ->type = type;
	publ->lower = lower;
	publ->upper = upper;
	publ->scope = scope;
	publ->node = node;
	publ->ref = port_ref;
	publ->key = key;
	INIT_LIST_HEAD(&publ->local_list);
	INIT_LIST_HEAD(&publ->pport_list);
	INIT_LIST_HEAD(&publ->subscr.nodesub_list);
	return publ;
}

/**
 * tipc_subseq_alloc - allocate a specified number of sub-sequence structures
 */

static struct sub_seq *tipc_subseq_alloc(u32 cnt)
{
	u32 sz = cnt * sizeof(struct sub_seq);
	struct sub_seq *sseq = (struct sub_seq *)kmalloc(sz, GFP_ATOMIC);

	if (sseq)
		memset(sseq, 0, sz);
	return sseq;
}

/**
 * tipc_nameseq_create - create a name sequence structure for the specified 'type'
 * 
 * Allocates a single sub-sequence structure and sets it to all 0's.
 */

static struct name_seq *tipc_nameseq_create(u32 type, struct hlist_head *seq_head)
{
	struct name_seq *nseq = 
		(struct name_seq *)kmalloc(sizeof(*nseq), GFP_ATOMIC);
	struct sub_seq *sseq = tipc_subseq_alloc(1);

	if (!nseq || !sseq) {
		warn("Memory squeeze; failed to create name sequence\n");
		kfree(nseq);
		kfree(sseq);
		return NULL;
	}

	memset(nseq, 0, sizeof(*nseq));
	nseq->lock = SPIN_LOCK_UNLOCKED;
	nseq->type = type;
	nseq->sseqs = sseq;
	dbg("tipc_nameseq_create() nseq = %x type %u, ssseqs %x, ff: %u\n",
	    nseq, type, nseq->sseqs, nseq->first_free);
	nseq->alloc = 1;
	INIT_HLIST_NODE(&nseq->ns_list);
	INIT_LIST_HEAD(&nseq->subscriptions);
	hlist_add_head(&nseq->ns_list, seq_head);
	return nseq;
}

/**
 * nameseq_find_subseq - find sub-sequence (if any) matching a name instance
 *  
 * Very time-critical, so binary searches through sub-sequence array.
 */

static struct sub_seq *nameseq_find_subseq(struct name_seq *nseq,
					   u32 instance)
{
	struct sub_seq *sseqs = nseq->sseqs;
	int low = 0;
	int high = nseq->first_free - 1;
	int mid;

	while (low <= high) {
		mid = (low + high) / 2;
		if (instance < sseqs[mid].lower)
			high = mid - 1;
		else if (instance > sseqs[mid].upper)
			low = mid + 1;
		else
			return &sseqs[mid];
	}
	return NULL;
}

/**
 * nameseq_locate_subseq - determine position of name instance in sub-sequence
 * 
 * Returns index in sub-sequence array of the entry that contains the specified
 * instance value; if no entry contains that value, returns the position
 * where a new entry for it would be inserted in the array.
 *
 * Note: Similar to binary search code for locating a sub-sequence.
 */

static u32 nameseq_locate_subseq(struct name_seq *nseq, u32 instance)
{
	struct sub_seq *sseqs = nseq->sseqs;
	int low = 0;
	int high = nseq->first_free - 1;
	int mid;

	while (low <= high) {
		mid = (low + high) / 2;
		if (instance < sseqs[mid].lower)
			high = mid - 1;
		else if (instance > sseqs[mid].upper)
			low = mid + 1;
		else
			return mid;
	}
	return low;
}

/**
 * tipc_nameseq_insert_publ - 
 */

static struct publication *tipc_nameseq_insert_publ(struct name_seq *nseq,
						    u32 type, u32 lower, u32 upper,
						    u32 scope, u32 node, u32 port, u32 key)
{
	struct subscription *s;
	struct subscription *st;
	struct publication *publ;
	struct sub_seq *sseq;
	int created_subseq = 0;

	assert(nseq->first_free <= nseq->alloc);
	sseq = nameseq_find_subseq(nseq, lower);
	dbg("nameseq_ins: for seq %x,<%u,%u>, found sseq %x\n",
	    nseq, type, lower, sseq);
	if (sseq) {

		/* Lower end overlaps existing entry => need an exact match */

		if ((sseq->lower != lower) || (sseq->upper != upper)) {
			warn("Overlapping publ <%u,%u,%u>\n", type, lower, upper);
			return NULL;
		}
	} else {
		u32 inspos;
		struct sub_seq *freesseq;

		/* Find where lower end should be inserted */

		inspos = nameseq_locate_subseq(nseq, lower);

		/* Fail if upper end overlaps into an existing entry */

		if ((inspos < nseq->first_free) &&
		    (upper >= nseq->sseqs[inspos].lower)) {
			warn("Overlapping publ <%u,%u,%u>\n", type, lower, upper);
			return NULL;
		}

		/* Ensure there is space for new sub-sequence */

		if (nseq->first_free == nseq->alloc) {
			struct sub_seq *sseqs = nseq->sseqs;
			nseq->sseqs = tipc_subseq_alloc(nseq->alloc * 2);
			if (nseq->sseqs != NULL) {
				memcpy(nseq->sseqs, sseqs,
				       nseq->alloc * sizeof (struct sub_seq));
				kfree(sseqs);
				dbg("Allocated %u sseqs\n", nseq->alloc);
				nseq->alloc *= 2;
			} else {
				warn("Memory squeeze; failed to create sub-sequence\n");
				return NULL;
			}
		}
		dbg("Have %u sseqs for type %u\n", nseq->alloc, type);

		/* Insert new sub-sequence */

		dbg("ins in pos %u, ff = %u\n", inspos, nseq->first_free);
		sseq = &nseq->sseqs[inspos];
		freesseq = &nseq->sseqs[nseq->first_free];
		memmove(sseq + 1, sseq, (freesseq - sseq) * sizeof (*sseq));
		memset(sseq, 0, sizeof (*sseq));
		nseq->first_free++;
		sseq->lower = lower;
		sseq->upper = upper;
		created_subseq = 1;
	}
	dbg("inserting (%u %u %u) from %x:%u into sseq %x(%u,%u) of seq %x\n",
	    type, lower, upper, node, port, sseq,
	    sseq->lower, sseq->upper, nseq);

	/* Insert a publication: */

	publ = publ_create(type, lower, upper, scope, node, port, key);
	if (!publ)
		return NULL;
	dbg("inserting publ %x, node=%x publ->node=%x, subscr->node=%x\n",
	    publ, node, publ->node, publ->subscr.node);

	if (!sseq->zone_list)
		sseq->zone_list = publ->zone_list_next = publ;
	else {
		publ->zone_list_next = sseq->zone_list->zone_list_next;
		sseq->zone_list->zone_list_next = publ;
	}

	if (in_own_cluster(node)) {
		if (!sseq->cluster_list)
			sseq->cluster_list = publ->cluster_list_next = publ;
		else {
			publ->cluster_list_next =
			sseq->cluster_list->cluster_list_next;
			sseq->cluster_list->cluster_list_next = publ;
		}
	}

	if (node == tipc_own_addr) {
		if (!sseq->node_list)
			sseq->node_list = publ->node_list_next = publ;
		else {
			publ->node_list_next = sseq->node_list->node_list_next;
			sseq->node_list->node_list_next = publ;
		}
	}

	/* 
	 * Any subscriptions waiting for notification? 
	 */
	list_for_each_entry_safe(s, st, &nseq->subscriptions, nameseq_list) {
		dbg("calling report_overlap()\n");
		tipc_subscr_report_overlap(s,
					   publ->lower,
					   publ->upper,
					   TIPC_PUBLISHED,
					   publ->ref, 
					   publ->node,
					   created_subseq);
	}
	return publ;
}

/**
 * tipc_nameseq_remove_publ -
 */

static struct publication *tipc_nameseq_remove_publ(struct name_seq *nseq, u32 inst,
						    u32 node, u32 ref, u32 key)
{
	struct publication *publ;
	struct publication *prev;
	struct sub_seq *sseq = nameseq_find_subseq(nseq, inst);
	struct sub_seq *free;
	struct subscription *s, *st;
	int removed_subseq = 0;

	assert(nseq);

	if (!sseq) {
		int i;

		warn("Withdraw unknown <%u,%u>?\n", nseq->type, inst);
		assert(nseq->sseqs);
		dbg("Dumping subseqs %x for %x, alloc = %u,ff=%u\n",
		    nseq->sseqs, nseq, nseq->alloc, 
		    nseq->first_free);
		for (i = 0; i < nseq->first_free; i++) {
			dbg("Subseq %u(%x): lower = %u,upper = %u\n",
			    i, &nseq->sseqs[i], nseq->sseqs[i].lower,
			    nseq->sseqs[i].upper);
		}
		return NULL;
	}
	dbg("nameseq_remove: seq: %x, sseq %x, <%u,%u> key %u\n",
	    nseq, sseq, nseq->type, inst, key);

	prev = sseq->zone_list;
	publ = sseq->zone_list->zone_list_next;
	while ((publ->key != key) || (publ->ref != ref) || 
	       (publ->node && (publ->node != node))) {
		prev = publ;
		publ = publ->zone_list_next;
		assert(prev != sseq->zone_list);
	}
	if (publ != sseq->zone_list)
		prev->zone_list_next = publ->zone_list_next;
	else if (publ->zone_list_next != publ) {
		prev->zone_list_next = publ->zone_list_next;
		sseq->zone_list = publ->zone_list_next;
	} else {
		sseq->zone_list = NULL;
	}

	if (in_own_cluster(node)) {
		prev = sseq->cluster_list;
		publ = sseq->cluster_list->cluster_list_next;
		while ((publ->key != key) || (publ->ref != ref) || 
		       (publ->node && (publ->node != node))) {
			prev = publ;
			publ = publ->cluster_list_next;
			assert(prev != sseq->cluster_list);
		}
		if (publ != sseq->cluster_list)
			prev->cluster_list_next = publ->cluster_list_next;
		else if (publ->cluster_list_next != publ) {
			prev->cluster_list_next = publ->cluster_list_next;
			sseq->cluster_list = publ->cluster_list_next;
		} else {
			sseq->cluster_list = NULL;
		}
	}

	if (node == tipc_own_addr) {
		prev = sseq->node_list;
		publ = sseq->node_list->node_list_next;
		while ((publ->key != key) || (publ->ref != ref) || 
		       (publ->node && (publ->node != node))) {
			prev = publ;
			publ = publ->node_list_next;
			assert(prev != sseq->node_list);
		}
		if (publ != sseq->node_list)
			prev->node_list_next = publ->node_list_next;
		else if (publ->node_list_next != publ) {
			prev->node_list_next = publ->node_list_next;
			sseq->node_list = publ->node_list_next;
		} else {
			sseq->node_list = NULL;
		}
	}
	assert(!publ->node || (publ->node == node));
	assert(publ->ref == ref);
	assert(publ->key == key);

	/* 
	 * Contract subseq list if no more publications:
	 */
	if (!sseq->node_list && !sseq->cluster_list && !sseq->zone_list) {
		free = &nseq->sseqs[nseq->first_free--];
		memmove(sseq, sseq + 1, (free - (sseq + 1)) * sizeof (*sseq));
		removed_subseq = 1;
	}

	/* 
	 * Any subscriptions waiting ? 
	 */
	list_for_each_entry_safe(s, st, &nseq->subscriptions, nameseq_list) {
		tipc_subscr_report_overlap(s,
					   publ->lower,
					   publ->upper,
					   TIPC_WITHDRAWN, 
					   publ->ref, 
					   publ->node,
					   removed_subseq);
	}
	return publ;
}

/**
 * tipc_nameseq_subscribe: attach a subscription, and issue
 * the prescribed number of events if there is any sub-
 * sequence overlapping with the requested sequence
 */

void tipc_nameseq_subscribe(struct name_seq *nseq, struct subscription *s)
{
	struct sub_seq *sseq = nseq->sseqs;

	list_add(&s->nameseq_list, &nseq->subscriptions);

	if (!sseq)
		return;

	while (sseq != &nseq->sseqs[nseq->first_free]) {
		struct publication *zl = sseq->zone_list;
		if (zl && tipc_subscr_overlap(s,sseq->lower,sseq->upper)) {
			struct publication *crs = zl;
			int must_report = 1;

			do {
				tipc_subscr_report_overlap(s, 
							   sseq->lower, 
							   sseq->upper,
							   TIPC_PUBLISHED,
							   crs->ref,
							   crs->node,
							   must_report);
				must_report = 0;
				crs = crs->zone_list_next;
			} while (crs != zl);
		}
		sseq++;
	}
}

static struct name_seq *nametbl_find_seq(u32 type)
{
	struct hlist_head *seq_head;
	struct hlist_node *seq_node;
	struct name_seq *ns;

	dbg("find_seq %u,(%u,0x%x) table = %p, hash[type] = %u\n",
	    type, ntohl(type), type, table.types, hash(type));

	seq_head = &table.types[hash(type)];
	hlist_for_each_entry(ns, seq_node, seq_head, ns_list) {
		if (ns->type == type) {
			dbg("found %x\n", ns);
			return ns;
		}
	}

	return NULL;
};

struct publication *tipc_nametbl_insert_publ(u32 type, u32 lower, u32 upper,
					     u32 scope, u32 node, u32 port, u32 key)
{
	struct name_seq *seq = nametbl_find_seq(type);

	dbg("ins_publ: <%u,%x,%x> found %x\n", type, lower, upper, seq);
	if (lower > upper) {
		warn("Failed to publish illegal <%u,%u,%u>\n",
		     type, lower, upper);
		return NULL;
	}

	dbg("Publishing <%u,%u,%u> from %x\n", type, lower, upper, node);
	if (!seq) {
		seq = tipc_nameseq_create(type, &table.types[hash(type)]);
		dbg("tipc_nametbl_insert_publ: created %x\n", seq);
	}
	if (!seq)
		return NULL;

	assert(seq->type == type);
	return tipc_nameseq_insert_publ(seq, type, lower, upper,
					scope, node, port, key);
}

struct publication *tipc_nametbl_remove_publ(u32 type, u32 lower, 
					     u32 node, u32 ref, u32 key)
{
	struct publication *publ;
	struct name_seq *seq = nametbl_find_seq(type);

	if (!seq)
		return NULL;

	dbg("Withdrawing <%u,%u> from %x\n", type, lower, node);
	publ = tipc_nameseq_remove_publ(seq, lower, node, ref, key);

	if (!seq->first_free && list_empty(&seq->subscriptions)) {
		hlist_del_init(&seq->ns_list);
		kfree(seq->sseqs);
		kfree(seq);
	}
	return publ;
}

/*
 * tipc_nametbl_translate(): Translate tipc_name -> tipc_portid.
 *                      Very time-critical.
 *
 * Note: on entry 'destnode' is the search domain used during translation;
 *       on exit it passes back the node address of the matching port (if any)
 */

u32 tipc_nametbl_translate(u32 type, u32 instance, u32 *destnode)
{
	struct sub_seq *sseq;
	struct publication *publ = NULL;
	struct name_seq *seq;
	u32 ref;

	if (!in_scope(*destnode, tipc_own_addr))
		return 0;

	read_lock_bh(&tipc_nametbl_lock);
	seq = nametbl_find_seq(type);
	if (unlikely(!seq))
		goto not_found;
	sseq = nameseq_find_subseq(seq, instance);
	if (unlikely(!sseq))
		goto not_found;
	spin_lock_bh(&seq->lock);

	/* Closest-First Algorithm: */
	if (likely(!*destnode)) {
		publ = sseq->node_list;
		if (publ) {
			sseq->node_list = publ->node_list_next;
found:
			ref = publ->ref;
			*destnode = publ->node;
			spin_unlock_bh(&seq->lock);
			read_unlock_bh(&tipc_nametbl_lock);
			return ref;
		}
		publ = sseq->cluster_list;
		if (publ) {
			sseq->cluster_list = publ->cluster_list_next;
			goto found;
		}
		publ = sseq->zone_list;
		if (publ) {
			sseq->zone_list = publ->zone_list_next;
			goto found;
		}
	}

	/* Round-Robin Algorithm: */
	else if (*destnode == tipc_own_addr) {
		publ = sseq->node_list;
		if (publ) {
			sseq->node_list = publ->node_list_next;
			goto found;
		}
	} else if (in_own_cluster(*destnode)) {
		publ = sseq->cluster_list;
		if (publ) {
			sseq->cluster_list = publ->cluster_list_next;
			goto found;
		}
	} else {
		publ = sseq->zone_list;
		if (publ) {
			sseq->zone_list = publ->zone_list_next;
			goto found;
		}
	}
	spin_unlock_bh(&seq->lock);
not_found:
	*destnode = 0;
	read_unlock_bh(&tipc_nametbl_lock);
	return 0;
}

/**
 * tipc_nametbl_mc_translate - find multicast destinations
 * 
 * Creates list of all local ports that overlap the given multicast address;
 * also determines if any off-node ports overlap.
 *
 * Note: Publications with a scope narrower than 'limit' are ignored.
 * (i.e. local node-scope publications mustn't receive messages arriving
 * from another node, even if the multcast link brought it here)
 * 
 * Returns non-zero if any off-node ports overlap
 */

int tipc_nametbl_mc_translate(u32 type, u32 lower, u32 upper, u32 limit,
			      struct port_list *dports)
{
	struct name_seq *seq;
	struct sub_seq *sseq;
	struct sub_seq *sseq_stop;
	int res = 0;

	read_lock_bh(&tipc_nametbl_lock);
	seq = nametbl_find_seq(type);
	if (!seq)
		goto exit;

	spin_lock_bh(&seq->lock);

	sseq = seq->sseqs + nameseq_locate_subseq(seq, lower);
	sseq_stop = seq->sseqs + seq->first_free;
	for (; sseq != sseq_stop; sseq++) {
		struct publication *publ;

		if (sseq->lower > upper)
			break;
		publ = sseq->cluster_list;
		if (publ && (publ->scope <= limit))
			do {
				if (publ->node == tipc_own_addr)
					tipc_port_list_add(dports, publ->ref);
				else
					res = 1;
				publ = publ->cluster_list_next;
			} while (publ != sseq->cluster_list);
	}

	spin_unlock_bh(&seq->lock);
exit:
	read_unlock_bh(&tipc_nametbl_lock);
	return res;
}

/**
 * tipc_nametbl_publish_rsv - publish port name using a reserved name type
 */

int tipc_nametbl_publish_rsv(u32 ref, unsigned int scope, 
			struct tipc_name_seq const *seq)
{
	int res;

	atomic_inc(&rsv_publ_ok);
	res = tipc_publish(ref, scope, seq);
	atomic_dec(&rsv_publ_ok);
	return res;
}

/**
 * tipc_nametbl_publish - add name publication to network name tables
 */

struct publication *tipc_nametbl_publish(u32 type, u32 lower, u32 upper, 
				    u32 scope, u32 port_ref, u32 key)
{
	struct publication *publ;

	if (table.local_publ_count >= tipc_max_publications) {
		warn("Failed publish: max %u local publication\n", 
		     tipc_max_publications);
		return NULL;
	}
	if ((type < TIPC_RESERVED_TYPES) && !atomic_read(&rsv_publ_ok)) {
		warn("Failed to publish reserved name <%u,%u,%u>\n",
		     type, lower, upper);
		return NULL;
	}

	write_lock_bh(&tipc_nametbl_lock);
	table.local_publ_count++;
	publ = tipc_nametbl_insert_publ(type, lower, upper, scope,
				   tipc_own_addr, port_ref, key);
	if (publ && (scope != TIPC_NODE_SCOPE)) {
		tipc_named_publish(publ);
	}
	write_unlock_bh(&tipc_nametbl_lock);
	return publ;
}

/**
 * tipc_nametbl_withdraw - withdraw name publication from network name tables
 */

int tipc_nametbl_withdraw(u32 type, u32 lower, u32 ref, u32 key)
{
	struct publication *publ;

	dbg("tipc_nametbl_withdraw:<%d,%d,%d>\n", type, lower, key);
	write_lock_bh(&tipc_nametbl_lock);
	publ = tipc_nametbl_remove_publ(type, lower, tipc_own_addr, ref, key);
	if (publ) {
		table.local_publ_count--;
		if (publ->scope != TIPC_NODE_SCOPE)
			tipc_named_withdraw(publ);
		write_unlock_bh(&tipc_nametbl_lock);
		list_del_init(&publ->pport_list);
		kfree(publ);
		return 1;
	}
	write_unlock_bh(&tipc_nametbl_lock);
	return 0;
}

/**
 * tipc_nametbl_subscribe - add a subscription object to the name table
 */

void
tipc_nametbl_subscribe(struct subscription *s)
{
	u32 type = s->seq.type;
	struct name_seq *seq;

        write_lock_bh(&tipc_nametbl_lock);
	seq = nametbl_find_seq(type);
	if (!seq) {
		seq = tipc_nameseq_create(type, &table.types[hash(type)]);
	}
        if (seq){
                spin_lock_bh(&seq->lock);
                dbg("tipc_nametbl_subscribe:found %x for <%u,%u,%u>\n",
                    seq, type, s->seq.lower, s->seq.upper);
                assert(seq->type == type);
                tipc_nameseq_subscribe(seq, s);
                spin_unlock_bh(&seq->lock);
        }
        write_unlock_bh(&tipc_nametbl_lock);
}

/**
 * tipc_nametbl_unsubscribe - remove a subscription object from name table
 */

void
tipc_nametbl_unsubscribe(struct subscription *s)
{
	struct name_seq *seq;

        write_lock_bh(&tipc_nametbl_lock);
        seq = nametbl_find_seq(s->seq.type);
	if (seq != NULL){
                spin_lock_bh(&seq->lock);
                list_del_init(&s->nameseq_list);
                spin_unlock_bh(&seq->lock);
                if ((seq->first_free == 0) && list_empty(&seq->subscriptions)) {
                        hlist_del_init(&seq->ns_list);
                        kfree(seq->sseqs);
                        kfree(seq);
                }
        }
        write_unlock_bh(&tipc_nametbl_lock);
}


/**
 * subseq_list: print specified sub-sequence contents into the given buffer
 */

static void subseq_list(struct sub_seq *sseq, struct print_buf *buf, u32 depth,
			u32 index)
{
	char portIdStr[27];
	char *scopeStr;
	struct publication *publ = sseq->zone_list;

	tipc_printf(buf, "%-10u %-10u ", sseq->lower, sseq->upper);

	if (depth == 2 || !publ) {
		tipc_printf(buf, "\n");
		return;
	}

	do {
		sprintf (portIdStr, "<%u.%u.%u:%u>",
			 tipc_zone(publ->node), tipc_cluster(publ->node),
			 tipc_node(publ->node), publ->ref);
		tipc_printf(buf, "%-26s ", portIdStr);
		if (depth > 3) {
			if (publ->node != tipc_own_addr)
				scopeStr = "";
			else if (publ->scope == TIPC_NODE_SCOPE)
				scopeStr = "node";
			else if (publ->scope == TIPC_CLUSTER_SCOPE)
				scopeStr = "cluster";
			else
				scopeStr = "zone";
			tipc_printf(buf, "%-10u %s", publ->key, scopeStr);
		}

		publ = publ->zone_list_next;
		if (publ == sseq->zone_list)
			break;

		tipc_printf(buf, "\n%33s", " ");
	} while (1);

	tipc_printf(buf, "\n");
}

/**
 * nameseq_list: print specified name sequence contents into the given buffer
 */

static void nameseq_list(struct name_seq *seq, struct print_buf *buf, u32 depth,
			 u32 type, u32 lowbound, u32 upbound, u32 index)
{
	struct sub_seq *sseq;
	char typearea[11];

	sprintf(typearea, "%-10u", seq->type);

	if (depth == 1) {
		tipc_printf(buf, "%s\n", typearea);
		return;
	}

	for (sseq = seq->sseqs; sseq != &seq->sseqs[seq->first_free]; sseq++) {
		if ((lowbound <= sseq->upper) && (upbound >= sseq->lower)) {
			tipc_printf(buf, "%s ", typearea);
			subseq_list(sseq, buf, depth, index);
			sprintf(typearea, "%10s", " ");
		}
	}
}

/**
 * nametbl_header - print name table header into the given buffer
 */

static void nametbl_header(struct print_buf *buf, u32 depth)
{
	tipc_printf(buf, "Type       ");

	if (depth > 1)
		tipc_printf(buf, "Lower      Upper      ");
	if (depth > 2)
		tipc_printf(buf, "Port Identity              ");
	if (depth > 3)
		tipc_printf(buf, "Publication");

	tipc_printf(buf, "\n-----------");

	if (depth > 1)
		tipc_printf(buf, "--------------------- ");
	if (depth > 2)
		tipc_printf(buf, "-------------------------- ");
	if (depth > 3)
		tipc_printf(buf, "------------------");

	tipc_printf(buf, "\n");
}

/**
 * nametbl_list - print specified name table contents into the given buffer
 */

static void nametbl_list(struct print_buf *buf, u32 depth_info, 
			 u32 type, u32 lowbound, u32 upbound)
{
	struct hlist_head *seq_head;
	struct hlist_node *seq_node;
	struct name_seq *seq;
	int all_types;
	u32 depth;
	u32 i;

	all_types = (depth_info & TIPC_NTQ_ALLTYPES);
	depth = (depth_info & ~TIPC_NTQ_ALLTYPES);

	if (depth == 0)
		return;

	if (all_types) {
		/* display all entries in name table to specified depth */
		nametbl_header(buf, depth);
		lowbound = 0;
		upbound = ~0;
		for (i = 0; i < tipc_nametbl_size; i++) {
			seq_head = &table.types[i];
			hlist_for_each_entry(seq, seq_node, seq_head, ns_list) {
				nameseq_list(seq, buf, depth, seq->type, 
					     lowbound, upbound, i);
			}
		}
	} else {
		/* display only the sequence that matches the specified type */
		if (upbound < lowbound) {
			tipc_printf(buf, "invalid name sequence specified\n");
			return;
		}
		nametbl_header(buf, depth);
		i = hash(type);
		seq_head = &table.types[i];
		hlist_for_each_entry(seq, seq_node, seq_head, ns_list) {
			if (seq->type == type) {
				nameseq_list(seq, buf, depth, type, 
					     lowbound, upbound, i);
				break;
			}
		}
	}
}

#if 0
void tipc_nametbl_print(struct print_buf *buf, const char *str)
{
	tipc_printf(buf, str);
	read_lock_bh(&tipc_nametbl_lock);
	nametbl_list(buf, 0, 0, 0, 0);
	read_unlock_bh(&tipc_nametbl_lock);
}
#endif

#define MAX_NAME_TBL_QUERY 32768

struct sk_buff *tipc_nametbl_get(const void *req_tlv_area, int req_tlv_space)
{
	struct sk_buff *buf;
	struct tipc_name_table_query *argv;
	struct tlv_desc *rep_tlv;
	struct print_buf b;
	int str_len;

	if (!TLV_CHECK(req_tlv_area, req_tlv_space, TIPC_TLV_NAME_TBL_QUERY))
		return tipc_cfg_reply_error_string(TIPC_CFG_TLV_ERROR);

	buf = tipc_cfg_reply_alloc(TLV_SPACE(MAX_NAME_TBL_QUERY));
	if (!buf)
		return NULL;

	rep_tlv = (struct tlv_desc *)buf->data;
	tipc_printbuf_init(&b, TLV_DATA(rep_tlv), MAX_NAME_TBL_QUERY);
	argv = (struct tipc_name_table_query *)TLV_DATA(req_tlv_area);
	read_lock_bh(&tipc_nametbl_lock);
	nametbl_list(&b, ntohl(argv->depth), ntohl(argv->type), 
		     ntohl(argv->lowbound), ntohl(argv->upbound));
	read_unlock_bh(&tipc_nametbl_lock);
	str_len = tipc_printbuf_validate(&b);

	skb_put(buf, TLV_SPACE(str_len));
	TLV_SET(rep_tlv, TIPC_TLV_ULTRA_STRING, NULL, str_len);

	return buf;
}

#if 0
void tipc_nametbl_dump(void)
{
	nametbl_list(TIPC_CONS, 0, 0, 0, 0);
}
#endif

int tipc_nametbl_init(void)
{
	int array_size = sizeof(struct hlist_head) * tipc_nametbl_size;

	table.types = (struct hlist_head *)kmalloc(array_size, GFP_ATOMIC);
	if (!table.types)
		return -ENOMEM;

	write_lock_bh(&tipc_nametbl_lock);
	memset(table.types, 0, array_size);
	table.local_publ_count = 0;
	write_unlock_bh(&tipc_nametbl_lock);
	return 0;
}

void tipc_nametbl_stop(void)
{
	struct hlist_head *seq_head;
	struct hlist_node *seq_node;
	struct hlist_node *tmp;
	struct name_seq *seq;
	u32 i;

	if (!table.types)
		return;

	write_lock_bh(&tipc_nametbl_lock);
	for (i = 0; i < tipc_nametbl_size; i++) {
		seq_head = &table.types[i];
		hlist_for_each_entry_safe(seq, seq_node, tmp, seq_head, ns_list) {
			struct sub_seq *sseq = seq->sseqs;

			for (; sseq != &seq->sseqs[seq->first_free]; sseq++) {
				struct publication *publ = sseq->zone_list;
				assert(publ);
				do {
					struct publication *next =
						publ->zone_list_next;
					kfree(publ);
					publ = next;
				}
				while (publ != sseq->zone_list);
			}
		}
	}
	kfree(table.types);
	table.types = NULL;
	write_unlock_bh(&tipc_nametbl_lock);
}
