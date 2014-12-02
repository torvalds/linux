/*
 * net/tipc/name_table.c: TIPC name table code
 *
 * Copyright (c) 2000-2006, 2014, Ericsson AB
 * Copyright (c) 2004-2008, 2010-2014, Wind River Systems
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
#include "name_table.h"
#include "name_distr.h"
#include "subscr.h"

#define TIPC_NAMETBL_SIZE 1024		/* must be a power of 2 */

static const struct nla_policy
tipc_nl_name_table_policy[TIPC_NLA_NAME_TABLE_MAX + 1] = {
	[TIPC_NLA_NAME_TABLE_UNSPEC]	= { .type = NLA_UNSPEC },
	[TIPC_NLA_NAME_TABLE_PUBL]	= { .type = NLA_NESTED }
};

/**
 * struct name_info - name sequence publication info
 * @node_list: circular list of publications made by own node
 * @cluster_list: circular list of publications made by own cluster
 * @zone_list: circular list of publications made by own zone
 * @node_list_size: number of entries in "node_list"
 * @cluster_list_size: number of entries in "cluster_list"
 * @zone_list_size: number of entries in "zone_list"
 *
 * Note: The zone list always contains at least one entry, since all
 *       publications of the associated name sequence belong to it.
 *       (The cluster and node lists may be empty.)
 */
struct name_info {
	struct list_head node_list;
	struct list_head cluster_list;
	struct list_head zone_list;
	u32 node_list_size;
	u32 cluster_list_size;
	u32 zone_list_size;
};

/**
 * struct sub_seq - container for all published instances of a name sequence
 * @lower: name sequence lower bound
 * @upper: name sequence upper bound
 * @info: pointer to name sequence publication info
 */
struct sub_seq {
	u32 lower;
	u32 upper;
	struct name_info *info;
};

/**
 * struct name_seq - container for all published instances of a name type
 * @type: 32 bit 'type' value for name sequence
 * @sseq: pointer to dynamically-sized array of sub-sequences of this 'type';
 *        sub-sequences are sorted in ascending order
 * @alloc: number of sub-sequences currently in array
 * @first_free: array index of first unused sub-sequence entry
 * @ns_list: links to adjacent name sequences in hash chain
 * @subscriptions: list of subscriptions for this 'type'
 * @lock: spinlock controlling access to publication lists of all sub-sequences
 * @rcu: RCU callback head used for deferred freeing
 */
struct name_seq {
	u32 type;
	struct sub_seq *sseqs;
	u32 alloc;
	u32 first_free;
	struct hlist_node ns_list;
	struct list_head subscriptions;
	spinlock_t lock;
	struct rcu_head rcu;
};

struct name_table *tipc_nametbl;
DEFINE_SPINLOCK(tipc_nametbl_lock);

static int hash(int x)
{
	return x & (TIPC_NAMETBL_SIZE - 1);
}

/**
 * publ_create - create a publication structure
 */
static struct publication *publ_create(u32 type, u32 lower, u32 upper,
				       u32 scope, u32 node, u32 port_ref,
				       u32 key)
{
	struct publication *publ = kzalloc(sizeof(*publ), GFP_ATOMIC);
	if (publ == NULL) {
		pr_warn("Publication creation failure, no memory\n");
		return NULL;
	}

	publ->type = type;
	publ->lower = lower;
	publ->upper = upper;
	publ->scope = scope;
	publ->node = node;
	publ->ref = port_ref;
	publ->key = key;
	INIT_LIST_HEAD(&publ->pport_list);
	return publ;
}

/**
 * tipc_subseq_alloc - allocate a specified number of sub-sequence structures
 */
static struct sub_seq *tipc_subseq_alloc(u32 cnt)
{
	return kcalloc(cnt, sizeof(struct sub_seq), GFP_ATOMIC);
}

/**
 * tipc_nameseq_create - create a name sequence structure for the specified 'type'
 *
 * Allocates a single sub-sequence structure and sets it to all 0's.
 */
static struct name_seq *tipc_nameseq_create(u32 type, struct hlist_head *seq_head)
{
	struct name_seq *nseq = kzalloc(sizeof(*nseq), GFP_ATOMIC);
	struct sub_seq *sseq = tipc_subseq_alloc(1);

	if (!nseq || !sseq) {
		pr_warn("Name sequence creation failed, no memory\n");
		kfree(nseq);
		kfree(sseq);
		return NULL;
	}

	spin_lock_init(&nseq->lock);
	nseq->type = type;
	nseq->sseqs = sseq;
	nseq->alloc = 1;
	INIT_HLIST_NODE(&nseq->ns_list);
	INIT_LIST_HEAD(&nseq->subscriptions);
	hlist_add_head_rcu(&nseq->ns_list, seq_head);
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
 * tipc_nameseq_insert_publ
 */
static struct publication *tipc_nameseq_insert_publ(struct name_seq *nseq,
						    u32 type, u32 lower, u32 upper,
						    u32 scope, u32 node, u32 port, u32 key)
{
	struct tipc_subscription *s;
	struct tipc_subscription *st;
	struct publication *publ;
	struct sub_seq *sseq;
	struct name_info *info;
	int created_subseq = 0;

	sseq = nameseq_find_subseq(nseq, lower);
	if (sseq) {

		/* Lower end overlaps existing entry => need an exact match */
		if ((sseq->lower != lower) || (sseq->upper != upper)) {
			return NULL;
		}

		info = sseq->info;

		/* Check if an identical publication already exists */
		list_for_each_entry(publ, &info->zone_list, zone_list) {
			if ((publ->ref == port) && (publ->key == key) &&
			    (!publ->node || (publ->node == node)))
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
			return NULL;
		}

		/* Ensure there is space for new sub-sequence */
		if (nseq->first_free == nseq->alloc) {
			struct sub_seq *sseqs = tipc_subseq_alloc(nseq->alloc * 2);

			if (!sseqs) {
				pr_warn("Cannot publish {%u,%u,%u}, no memory\n",
					type, lower, upper);
				return NULL;
			}
			memcpy(sseqs, nseq->sseqs,
			       nseq->alloc * sizeof(struct sub_seq));
			kfree(nseq->sseqs);
			nseq->sseqs = sseqs;
			nseq->alloc *= 2;
		}

		info = kzalloc(sizeof(*info), GFP_ATOMIC);
		if (!info) {
			pr_warn("Cannot publish {%u,%u,%u}, no memory\n",
				type, lower, upper);
			return NULL;
		}

		INIT_LIST_HEAD(&info->node_list);
		INIT_LIST_HEAD(&info->cluster_list);
		INIT_LIST_HEAD(&info->zone_list);

		/* Insert new sub-sequence */
		sseq = &nseq->sseqs[inspos];
		freesseq = &nseq->sseqs[nseq->first_free];
		memmove(sseq + 1, sseq, (freesseq - sseq) * sizeof(*sseq));
		memset(sseq, 0, sizeof(*sseq));
		nseq->first_free++;
		sseq->lower = lower;
		sseq->upper = upper;
		sseq->info = info;
		created_subseq = 1;
	}

	/* Insert a publication */
	publ = publ_create(type, lower, upper, scope, node, port, key);
	if (!publ)
		return NULL;

	list_add(&publ->zone_list, &info->zone_list);
	info->zone_list_size++;

	if (in_own_cluster(node)) {
		list_add(&publ->cluster_list, &info->cluster_list);
		info->cluster_list_size++;
	}

	if (in_own_node(node)) {
		list_add(&publ->node_list, &info->node_list);
		info->node_list_size++;
	}

	/* Any subscriptions waiting for notification?  */
	list_for_each_entry_safe(s, st, &nseq->subscriptions, nameseq_list) {
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
 * tipc_nameseq_remove_publ
 *
 * NOTE: There may be cases where TIPC is asked to remove a publication
 * that is not in the name table.  For example, if another node issues a
 * publication for a name sequence that overlaps an existing name sequence
 * the publication will not be recorded, which means the publication won't
 * be found when the name sequence is later withdrawn by that node.
 * A failed withdraw request simply returns a failure indication and lets the
 * caller issue any error or warning messages associated with such a problem.
 */
static struct publication *tipc_nameseq_remove_publ(struct name_seq *nseq, u32 inst,
						    u32 node, u32 ref, u32 key)
{
	struct publication *publ;
	struct sub_seq *sseq = nameseq_find_subseq(nseq, inst);
	struct name_info *info;
	struct sub_seq *free;
	struct tipc_subscription *s, *st;
	int removed_subseq = 0;

	if (!sseq)
		return NULL;

	info = sseq->info;

	/* Locate publication, if it exists */
	list_for_each_entry(publ, &info->zone_list, zone_list) {
		if ((publ->key == key) && (publ->ref == ref) &&
		    (!publ->node || (publ->node == node)))
			goto found;
	}
	return NULL;

found:
	/* Remove publication from zone scope list */
	list_del(&publ->zone_list);
	info->zone_list_size--;

	/* Remove publication from cluster scope list, if present */
	if (in_own_cluster(node)) {
		list_del(&publ->cluster_list);
		info->cluster_list_size--;
	}

	/* Remove publication from node scope list, if present */
	if (in_own_node(node)) {
		list_del(&publ->node_list);
		info->node_list_size--;
	}

	/* Contract subseq list if no more publications for that subseq */
	if (list_empty(&info->zone_list)) {
		kfree(info);
		free = &nseq->sseqs[nseq->first_free--];
		memmove(sseq, sseq + 1, (free - (sseq + 1)) * sizeof(*sseq));
		removed_subseq = 1;
	}

	/* Notify any waiting subscriptions */
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
 * tipc_nameseq_subscribe - attach a subscription, and issue
 * the prescribed number of events if there is any sub-
 * sequence overlapping with the requested sequence
 */
static void tipc_nameseq_subscribe(struct name_seq *nseq,
				   struct tipc_subscription *s)
{
	struct sub_seq *sseq = nseq->sseqs;

	list_add(&s->nameseq_list, &nseq->subscriptions);

	if (!sseq)
		return;

	while (sseq != &nseq->sseqs[nseq->first_free]) {
		if (tipc_subscr_overlap(s, sseq->lower, sseq->upper)) {
			struct publication *crs;
			struct name_info *info = sseq->info;
			int must_report = 1;

			list_for_each_entry(crs, &info->zone_list, zone_list) {
				tipc_subscr_report_overlap(s,
							   sseq->lower,
							   sseq->upper,
							   TIPC_PUBLISHED,
							   crs->ref,
							   crs->node,
							   must_report);
				must_report = 0;
			}
		}
		sseq++;
	}
}

static struct name_seq *nametbl_find_seq(u32 type)
{
	struct hlist_head *seq_head;
	struct name_seq *ns;

	seq_head = &tipc_nametbl->seq_hlist[hash(type)];
	hlist_for_each_entry_rcu(ns, seq_head, ns_list) {
		if (ns->type == type)
			return ns;
	}

	return NULL;
};

struct publication *tipc_nametbl_insert_publ(u32 type, u32 lower, u32 upper,
					     u32 scope, u32 node, u32 port, u32 key)
{
	struct publication *publ;
	struct name_seq *seq = nametbl_find_seq(type);
	int index = hash(type);

	if ((scope < TIPC_ZONE_SCOPE) || (scope > TIPC_NODE_SCOPE) ||
	    (lower > upper)) {
		pr_debug("Failed to publish illegal {%u,%u,%u} with scope %u\n",
			 type, lower, upper, scope);
		return NULL;
	}

	if (!seq)
		seq = tipc_nameseq_create(type,
					  &tipc_nametbl->seq_hlist[index]);
	if (!seq)
		return NULL;

	spin_lock_bh(&seq->lock);
	publ = tipc_nameseq_insert_publ(seq, type, lower, upper,
					scope, node, port, key);
	spin_unlock_bh(&seq->lock);
	return publ;
}

struct publication *tipc_nametbl_remove_publ(u32 type, u32 lower,
					     u32 node, u32 ref, u32 key)
{
	struct publication *publ;
	struct name_seq *seq = nametbl_find_seq(type);

	if (!seq)
		return NULL;

	spin_lock_bh(&seq->lock);
	publ = tipc_nameseq_remove_publ(seq, lower, node, ref, key);
	if (!seq->first_free && list_empty(&seq->subscriptions)) {
		hlist_del_init_rcu(&seq->ns_list);
		kfree(seq->sseqs);
		spin_unlock_bh(&seq->lock);
		kfree_rcu(seq, rcu);
		return publ;
	}
	spin_unlock_bh(&seq->lock);
	return publ;
}

/**
 * tipc_nametbl_translate - perform name translation
 *
 * On entry, 'destnode' is the search domain used during translation.
 *
 * On exit:
 * - if name translation is deferred to another node/cluster/zone,
 *   leaves 'destnode' unchanged (will be non-zero) and returns 0
 * - if name translation is attempted and succeeds, sets 'destnode'
 *   to publishing node and returns port reference (will be non-zero)
 * - if name translation is attempted and fails, sets 'destnode' to 0
 *   and returns 0
 */
u32 tipc_nametbl_translate(u32 type, u32 instance, u32 *destnode)
{
	struct sub_seq *sseq;
	struct name_info *info;
	struct publication *publ;
	struct name_seq *seq;
	u32 ref = 0;
	u32 node = 0;

	if (!tipc_in_scope(*destnode, tipc_own_addr))
		return 0;

	rcu_read_lock();
	seq = nametbl_find_seq(type);
	if (unlikely(!seq))
		goto not_found;
	spin_lock_bh(&seq->lock);
	sseq = nameseq_find_subseq(seq, instance);
	if (unlikely(!sseq))
		goto no_match;
	info = sseq->info;

	/* Closest-First Algorithm */
	if (likely(!*destnode)) {
		if (!list_empty(&info->node_list)) {
			publ = list_first_entry(&info->node_list,
						struct publication,
						node_list);
			list_move_tail(&publ->node_list,
				       &info->node_list);
		} else if (!list_empty(&info->cluster_list)) {
			publ = list_first_entry(&info->cluster_list,
						struct publication,
						cluster_list);
			list_move_tail(&publ->cluster_list,
				       &info->cluster_list);
		} else {
			publ = list_first_entry(&info->zone_list,
						struct publication,
						zone_list);
			list_move_tail(&publ->zone_list,
				       &info->zone_list);
		}
	}

	/* Round-Robin Algorithm */
	else if (*destnode == tipc_own_addr) {
		if (list_empty(&info->node_list))
			goto no_match;
		publ = list_first_entry(&info->node_list, struct publication,
					node_list);
		list_move_tail(&publ->node_list, &info->node_list);
	} else if (in_own_cluster_exact(*destnode)) {
		if (list_empty(&info->cluster_list))
			goto no_match;
		publ = list_first_entry(&info->cluster_list, struct publication,
					cluster_list);
		list_move_tail(&publ->cluster_list, &info->cluster_list);
	} else {
		publ = list_first_entry(&info->zone_list, struct publication,
					zone_list);
		list_move_tail(&publ->zone_list, &info->zone_list);
	}

	ref = publ->ref;
	node = publ->node;
no_match:
	spin_unlock_bh(&seq->lock);
not_found:
	rcu_read_unlock();
	*destnode = node;
	return ref;
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
			      struct tipc_port_list *dports)
{
	struct name_seq *seq;
	struct sub_seq *sseq;
	struct sub_seq *sseq_stop;
	struct name_info *info;
	int res = 0;

	rcu_read_lock();
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

		info = sseq->info;
		list_for_each_entry(publ, &info->node_list, node_list) {
			if (publ->scope <= limit)
				tipc_port_list_add(dports, publ->ref);
		}

		if (info->cluster_list_size != info->node_list_size)
			res = 1;
	}
	spin_unlock_bh(&seq->lock);
exit:
	rcu_read_unlock();
	return res;
}

/*
 * tipc_nametbl_publish - add name publication to network name tables
 */
struct publication *tipc_nametbl_publish(u32 type, u32 lower, u32 upper,
					 u32 scope, u32 port_ref, u32 key)
{
	struct publication *publ;
	struct sk_buff *buf = NULL;

	spin_lock_bh(&tipc_nametbl_lock);
	if (tipc_nametbl->local_publ_count >= TIPC_MAX_PUBLICATIONS) {
		pr_warn("Publication failed, local publication limit reached (%u)\n",
			TIPC_MAX_PUBLICATIONS);
		spin_unlock_bh(&tipc_nametbl_lock);
		return NULL;
	}

	publ = tipc_nametbl_insert_publ(type, lower, upper, scope,
				   tipc_own_addr, port_ref, key);
	if (likely(publ)) {
		tipc_nametbl->local_publ_count++;
		buf = tipc_named_publish(publ);
		/* Any pending external events? */
		tipc_named_process_backlog();
	}
	spin_unlock_bh(&tipc_nametbl_lock);

	if (buf)
		named_cluster_distribute(buf);
	return publ;
}

/**
 * tipc_nametbl_withdraw - withdraw name publication from network name tables
 */
int tipc_nametbl_withdraw(u32 type, u32 lower, u32 ref, u32 key)
{
	struct publication *publ;
	struct sk_buff *skb = NULL;

	spin_lock_bh(&tipc_nametbl_lock);
	publ = tipc_nametbl_remove_publ(type, lower, tipc_own_addr, ref, key);
	if (likely(publ)) {
		tipc_nametbl->local_publ_count--;
		skb = tipc_named_withdraw(publ);
		/* Any pending external events? */
		tipc_named_process_backlog();
		list_del_init(&publ->pport_list);
		kfree_rcu(publ, rcu);
	} else {
		pr_err("Unable to remove local publication\n"
		       "(type=%u, lower=%u, ref=%u, key=%u)\n",
		       type, lower, ref, key);
	}
	spin_unlock_bh(&tipc_nametbl_lock);

	if (skb) {
		named_cluster_distribute(skb);
		return 1;
	}
	return 0;
}

/**
 * tipc_nametbl_subscribe - add a subscription object to the name table
 */
void tipc_nametbl_subscribe(struct tipc_subscription *s)
{
	u32 type = s->seq.type;
	int index = hash(type);
	struct name_seq *seq;

	spin_lock_bh(&tipc_nametbl_lock);
	seq = nametbl_find_seq(type);
	if (!seq)
		seq = tipc_nameseq_create(type,
					  &tipc_nametbl->seq_hlist[index]);
	if (seq) {
		spin_lock_bh(&seq->lock);
		tipc_nameseq_subscribe(seq, s);
		spin_unlock_bh(&seq->lock);
	} else {
		pr_warn("Failed to create subscription for {%u,%u,%u}\n",
			s->seq.type, s->seq.lower, s->seq.upper);
	}
	spin_unlock_bh(&tipc_nametbl_lock);
}

/**
 * tipc_nametbl_unsubscribe - remove a subscription object from name table
 */
void tipc_nametbl_unsubscribe(struct tipc_subscription *s)
{
	struct name_seq *seq;

	spin_lock_bh(&tipc_nametbl_lock);
	seq = nametbl_find_seq(s->seq.type);
	if (seq != NULL) {
		spin_lock_bh(&seq->lock);
		list_del_init(&s->nameseq_list);
		if (!seq->first_free && list_empty(&seq->subscriptions)) {
			hlist_del_init_rcu(&seq->ns_list);
			kfree(seq->sseqs);
			spin_unlock_bh(&seq->lock);
			kfree_rcu(seq, rcu);
		} else {
			spin_unlock_bh(&seq->lock);
		}
	}
	spin_unlock_bh(&tipc_nametbl_lock);
}

/**
 * subseq_list - print specified sub-sequence contents into the given buffer
 */
static int subseq_list(struct sub_seq *sseq, char *buf, int len, u32 depth,
		       u32 index)
{
	char portIdStr[27];
	const char *scope_str[] = {"", " zone", " cluster", " node"};
	struct publication *publ;
	struct name_info *info;
	int ret;

	ret = tipc_snprintf(buf, len, "%-10u %-10u ", sseq->lower, sseq->upper);

	if (depth == 2) {
		ret += tipc_snprintf(buf - ret, len + ret, "\n");
		return ret;
	}

	info = sseq->info;

	list_for_each_entry(publ, &info->zone_list, zone_list) {
		sprintf(portIdStr, "<%u.%u.%u:%u>",
			 tipc_zone(publ->node), tipc_cluster(publ->node),
			 tipc_node(publ->node), publ->ref);
		ret += tipc_snprintf(buf + ret, len - ret, "%-26s ", portIdStr);
		if (depth > 3) {
			ret += tipc_snprintf(buf + ret, len - ret, "%-10u %s",
					     publ->key, scope_str[publ->scope]);
		}
		if (!list_is_last(&publ->zone_list, &info->zone_list))
			ret += tipc_snprintf(buf + ret, len - ret,
					     "\n%33s", " ");
	}

	ret += tipc_snprintf(buf + ret, len - ret, "\n");
	return ret;
}

/**
 * nameseq_list - print specified name sequence contents into the given buffer
 */
static int nameseq_list(struct name_seq *seq, char *buf, int len, u32 depth,
			u32 type, u32 lowbound, u32 upbound, u32 index)
{
	struct sub_seq *sseq;
	char typearea[11];
	int ret = 0;

	if (seq->first_free == 0)
		return 0;

	sprintf(typearea, "%-10u", seq->type);

	if (depth == 1) {
		ret += tipc_snprintf(buf, len, "%s\n", typearea);
		return ret;
	}

	for (sseq = seq->sseqs; sseq != &seq->sseqs[seq->first_free]; sseq++) {
		if ((lowbound <= sseq->upper) && (upbound >= sseq->lower)) {
			ret += tipc_snprintf(buf + ret, len - ret, "%s ",
					    typearea);
			spin_lock_bh(&seq->lock);
			ret += subseq_list(sseq, buf + ret, len - ret,
					  depth, index);
			spin_unlock_bh(&seq->lock);
			sprintf(typearea, "%10s", " ");
		}
	}
	return ret;
}

/**
 * nametbl_header - print name table header into the given buffer
 */
static int nametbl_header(char *buf, int len, u32 depth)
{
	const char *header[] = {
		"Type       ",
		"Lower      Upper      ",
		"Port Identity              ",
		"Publication Scope"
	};

	int i;
	int ret = 0;

	if (depth > 4)
		depth = 4;
	for (i = 0; i < depth; i++)
		ret += tipc_snprintf(buf + ret, len - ret, header[i]);
	ret += tipc_snprintf(buf + ret, len - ret, "\n");
	return ret;
}

/**
 * nametbl_list - print specified name table contents into the given buffer
 */
static int nametbl_list(char *buf, int len, u32 depth_info,
			u32 type, u32 lowbound, u32 upbound)
{
	struct hlist_head *seq_head;
	struct name_seq *seq;
	int all_types;
	int ret = 0;
	u32 depth;
	u32 i;

	all_types = (depth_info & TIPC_NTQ_ALLTYPES);
	depth = (depth_info & ~TIPC_NTQ_ALLTYPES);

	if (depth == 0)
		return 0;

	if (all_types) {
		/* display all entries in name table to specified depth */
		ret += nametbl_header(buf, len, depth);
		lowbound = 0;
		upbound = ~0;
		for (i = 0; i < TIPC_NAMETBL_SIZE; i++) {
			seq_head = &tipc_nametbl->seq_hlist[i];
			hlist_for_each_entry_rcu(seq, seq_head, ns_list) {
				ret += nameseq_list(seq, buf + ret, len - ret,
						   depth, seq->type,
						   lowbound, upbound, i);
			}
		}
	} else {
		/* display only the sequence that matches the specified type */
		if (upbound < lowbound) {
			ret += tipc_snprintf(buf + ret, len - ret,
					"invalid name sequence specified\n");
			return ret;
		}
		ret += nametbl_header(buf + ret, len - ret, depth);
		i = hash(type);
		seq_head = &tipc_nametbl->seq_hlist[i];
		hlist_for_each_entry_rcu(seq, seq_head, ns_list) {
			if (seq->type == type) {
				ret += nameseq_list(seq, buf + ret, len - ret,
						   depth, type,
						   lowbound, upbound, i);
				break;
			}
		}
	}
	return ret;
}

struct sk_buff *tipc_nametbl_get(const void *req_tlv_area, int req_tlv_space)
{
	struct sk_buff *buf;
	struct tipc_name_table_query *argv;
	struct tlv_desc *rep_tlv;
	char *pb;
	int pb_len;
	int str_len;

	if (!TLV_CHECK(req_tlv_area, req_tlv_space, TIPC_TLV_NAME_TBL_QUERY))
		return tipc_cfg_reply_error_string(TIPC_CFG_TLV_ERROR);

	buf = tipc_cfg_reply_alloc(TLV_SPACE(ULTRA_STRING_MAX_LEN));
	if (!buf)
		return NULL;

	rep_tlv = (struct tlv_desc *)buf->data;
	pb = TLV_DATA(rep_tlv);
	pb_len = ULTRA_STRING_MAX_LEN;
	argv = (struct tipc_name_table_query *)TLV_DATA(req_tlv_area);
	rcu_read_lock();
	str_len = nametbl_list(pb, pb_len, ntohl(argv->depth),
			       ntohl(argv->type),
			       ntohl(argv->lowbound), ntohl(argv->upbound));
	rcu_read_unlock();
	str_len += 1;	/* for "\0" */
	skb_put(buf, TLV_SPACE(str_len));
	TLV_SET(rep_tlv, TIPC_TLV_ULTRA_STRING, NULL, str_len);

	return buf;
}

int tipc_nametbl_init(void)
{
	int i;

	tipc_nametbl = kzalloc(sizeof(*tipc_nametbl), GFP_ATOMIC);
	if (!tipc_nametbl)
		return -ENOMEM;

	for (i = 0; i < TIPC_NAMETBL_SIZE; i++)
		INIT_HLIST_HEAD(&tipc_nametbl->seq_hlist[i]);

	INIT_LIST_HEAD(&tipc_nametbl->publ_list[TIPC_ZONE_SCOPE]);
	INIT_LIST_HEAD(&tipc_nametbl->publ_list[TIPC_CLUSTER_SCOPE]);
	INIT_LIST_HEAD(&tipc_nametbl->publ_list[TIPC_NODE_SCOPE]);
	return 0;
}

/**
 * tipc_purge_publications - remove all publications for a given type
 *
 * tipc_nametbl_lock must be held when calling this function
 */
static void tipc_purge_publications(struct name_seq *seq)
{
	struct publication *publ, *safe;
	struct sub_seq *sseq;
	struct name_info *info;

	spin_lock_bh(&seq->lock);
	sseq = seq->sseqs;
	info = sseq->info;
	list_for_each_entry_safe(publ, safe, &info->zone_list, zone_list) {
		tipc_nametbl_remove_publ(publ->type, publ->lower, publ->node,
					 publ->ref, publ->key);
		kfree_rcu(publ, rcu);
	}
	hlist_del_init_rcu(&seq->ns_list);
	kfree(seq->sseqs);
	spin_lock_bh(&seq->lock);

	kfree_rcu(seq, rcu);
}

void tipc_nametbl_stop(void)
{
	u32 i;
	struct name_seq *seq;
	struct hlist_head *seq_head;

	/* Verify name table is empty and purge any lingering
	 * publications, then release the name table
	 */
	spin_lock_bh(&tipc_nametbl_lock);
	for (i = 0; i < TIPC_NAMETBL_SIZE; i++) {
		if (hlist_empty(&tipc_nametbl->seq_hlist[i]))
			continue;
		seq_head = &tipc_nametbl->seq_hlist[i];
		hlist_for_each_entry_rcu(seq, seq_head, ns_list) {
			tipc_purge_publications(seq);
		}
	}
	spin_unlock_bh(&tipc_nametbl_lock);

	synchronize_net();
	kfree(tipc_nametbl);

}

static int __tipc_nl_add_nametable_publ(struct tipc_nl_msg *msg,
					struct name_seq *seq,
					struct sub_seq *sseq, u32 *last_publ)
{
	void *hdr;
	struct nlattr *attrs;
	struct nlattr *publ;
	struct publication *p;

	if (*last_publ) {
		list_for_each_entry(p, &sseq->info->zone_list, zone_list)
			if (p->key == *last_publ)
				break;
		if (p->key != *last_publ)
			return -EPIPE;
	} else {
		p = list_first_entry(&sseq->info->zone_list, struct publication,
				     zone_list);
	}

	list_for_each_entry_from(p, &sseq->info->zone_list, zone_list) {
		*last_publ = p->key;

		hdr = genlmsg_put(msg->skb, msg->portid, msg->seq,
				  &tipc_genl_v2_family, NLM_F_MULTI,
				  TIPC_NL_NAME_TABLE_GET);
		if (!hdr)
			return -EMSGSIZE;

		attrs = nla_nest_start(msg->skb, TIPC_NLA_NAME_TABLE);
		if (!attrs)
			goto msg_full;

		publ = nla_nest_start(msg->skb, TIPC_NLA_NAME_TABLE_PUBL);
		if (!publ)
			goto attr_msg_full;

		if (nla_put_u32(msg->skb, TIPC_NLA_PUBL_TYPE, seq->type))
			goto publ_msg_full;
		if (nla_put_u32(msg->skb, TIPC_NLA_PUBL_LOWER, sseq->lower))
			goto publ_msg_full;
		if (nla_put_u32(msg->skb, TIPC_NLA_PUBL_UPPER, sseq->upper))
			goto publ_msg_full;
		if (nla_put_u32(msg->skb, TIPC_NLA_PUBL_SCOPE, p->scope))
			goto publ_msg_full;
		if (nla_put_u32(msg->skb, TIPC_NLA_PUBL_NODE, p->node))
			goto publ_msg_full;
		if (nla_put_u32(msg->skb, TIPC_NLA_PUBL_REF, p->ref))
			goto publ_msg_full;
		if (nla_put_u32(msg->skb, TIPC_NLA_PUBL_KEY, p->key))
			goto publ_msg_full;

		nla_nest_end(msg->skb, publ);
		nla_nest_end(msg->skb, attrs);
		genlmsg_end(msg->skb, hdr);
	}
	*last_publ = 0;

	return 0;

publ_msg_full:
	nla_nest_cancel(msg->skb, publ);
attr_msg_full:
	nla_nest_cancel(msg->skb, attrs);
msg_full:
	genlmsg_cancel(msg->skb, hdr);

	return -EMSGSIZE;
}

static int __tipc_nl_subseq_list(struct tipc_nl_msg *msg, struct name_seq *seq,
				 u32 *last_lower, u32 *last_publ)
{
	struct sub_seq *sseq;
	struct sub_seq *sseq_start;
	int err;

	if (*last_lower) {
		sseq_start = nameseq_find_subseq(seq, *last_lower);
		if (!sseq_start)
			return -EPIPE;
	} else {
		sseq_start = seq->sseqs;
	}

	for (sseq = sseq_start; sseq != &seq->sseqs[seq->first_free]; sseq++) {
		err = __tipc_nl_add_nametable_publ(msg, seq, sseq, last_publ);
		if (err) {
			*last_lower = sseq->lower;
			return err;
		}
	}
	*last_lower = 0;

	return 0;
}

static int __tipc_nl_seq_list(struct tipc_nl_msg *msg, u32 *last_type,
			      u32 *last_lower, u32 *last_publ)
{
	struct hlist_head *seq_head;
	struct name_seq *seq = NULL;
	int err;
	int i;

	if (*last_type)
		i = hash(*last_type);
	else
		i = 0;

	for (; i < TIPC_NAMETBL_SIZE; i++) {
		seq_head = &tipc_nametbl->seq_hlist[i];

		if (*last_type) {
			seq = nametbl_find_seq(*last_type);
			if (!seq)
				return -EPIPE;
		} else {
			hlist_for_each_entry_rcu(seq, seq_head, ns_list)
				break;
			if (!seq)
				continue;
		}

		hlist_for_each_entry_from_rcu(seq, ns_list) {
			spin_lock_bh(&seq->lock);
			err = __tipc_nl_subseq_list(msg, seq, last_lower,
						    last_publ);

			if (err) {
				*last_type = seq->type;
				spin_unlock_bh(&seq->lock);
				return err;
			}
			spin_unlock_bh(&seq->lock);
		}
		*last_type = 0;
	}
	return 0;
}

int tipc_nl_name_table_dump(struct sk_buff *skb, struct netlink_callback *cb)
{
	int err;
	int done = cb->args[3];
	u32 last_type = cb->args[0];
	u32 last_lower = cb->args[1];
	u32 last_publ = cb->args[2];
	struct tipc_nl_msg msg;

	if (done)
		return 0;

	msg.skb = skb;
	msg.portid = NETLINK_CB(cb->skb).portid;
	msg.seq = cb->nlh->nlmsg_seq;

	rcu_read_lock();
	err = __tipc_nl_seq_list(&msg, &last_type, &last_lower, &last_publ);
	if (!err) {
		done = 1;
	} else if (err != -EMSGSIZE) {
		/* We never set seq or call nl_dump_check_consistent() this
		 * means that setting prev_seq here will cause the consistence
		 * check to fail in the netlink callback handler. Resulting in
		 * the NLMSG_DONE message having the NLM_F_DUMP_INTR flag set if
		 * we got an error.
		 */
		cb->prev_seq = 1;
	}
	rcu_read_unlock();

	cb->args[0] = last_type;
	cb->args[1] = last_lower;
	cb->args[2] = last_publ;
	cb->args[3] = done;

	return skb->len;
}
