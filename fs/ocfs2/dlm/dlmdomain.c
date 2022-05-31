// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * dlmdomain.c
 *
 * defines domain join / leave apis
 *
 * Copyright (C) 2004 Oracle.  All rights reserved.
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/highmem.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/debugfs.h>
#include <linux/sched/signal.h>

#include "../cluster/heartbeat.h"
#include "../cluster/nodemanager.h"
#include "../cluster/tcp.h"

#include "dlmapi.h"
#include "dlmcommon.h"
#include "dlmdomain.h"
#include "dlmdebug.h"

#define MLOG_MASK_PREFIX (ML_DLM|ML_DLM_DOMAIN)
#include "../cluster/masklog.h"

/*
 * ocfs2 node maps are array of long int, which limits to send them freely
 * across the wire due to endianness issues. To workaround this, we convert
 * long ints to byte arrays. Following 3 routines are helper functions to
 * set/test/copy bits within those array of bytes
 */
static inline void byte_set_bit(u8 nr, u8 map[])
{
	map[nr >> 3] |= (1UL << (nr & 7));
}

static inline int byte_test_bit(u8 nr, u8 map[])
{
	return ((1UL << (nr & 7)) & (map[nr >> 3])) != 0;
}

static inline void byte_copymap(u8 dmap[], unsigned long smap[],
			unsigned int sz)
{
	unsigned int nn;

	if (!sz)
		return;

	memset(dmap, 0, ((sz + 7) >> 3));
	for (nn = 0 ; nn < sz; nn++)
		if (test_bit(nn, smap))
			byte_set_bit(nn, dmap);
}

static void dlm_free_pagevec(void **vec, int pages)
{
	while (pages--)
		free_page((unsigned long)vec[pages]);
	kfree(vec);
}

static void **dlm_alloc_pagevec(int pages)
{
	void **vec = kmalloc_array(pages, sizeof(void *), GFP_KERNEL);
	int i;

	if (!vec)
		return NULL;

	for (i = 0; i < pages; i++)
		if (!(vec[i] = (void *)__get_free_page(GFP_KERNEL)))
			goto out_free;

	mlog(0, "Allocated DLM hash pagevec; %d pages (%lu expected), %lu buckets per page\n",
	     pages, (unsigned long)DLM_HASH_PAGES,
	     (unsigned long)DLM_BUCKETS_PER_PAGE);
	return vec;
out_free:
	dlm_free_pagevec(vec, i);
	return NULL;
}

/*
 *
 * spinlock lock ordering: if multiple locks are needed, obey this ordering:
 *    dlm_domain_lock
 *    struct dlm_ctxt->spinlock
 *    struct dlm_lock_resource->spinlock
 *    struct dlm_ctxt->master_lock
 *    struct dlm_ctxt->ast_lock
 *    dlm_master_list_entry->spinlock
 *    dlm_lock->spinlock
 *
 */

DEFINE_SPINLOCK(dlm_domain_lock);
LIST_HEAD(dlm_domains);
static DECLARE_WAIT_QUEUE_HEAD(dlm_domain_events);

/*
 * The supported protocol version for DLM communication.  Running domains
 * will have a negotiated version with the same major number and a minor
 * number equal or smaller.  The dlm_ctxt->dlm_locking_proto field should
 * be used to determine what a running domain is actually using.
 *
 * New in version 1.1:
 *	- Message DLM_QUERY_REGION added to support global heartbeat
 *	- Message DLM_QUERY_NODEINFO added to allow online node removes
 * New in version 1.2:
 * 	- Message DLM_BEGIN_EXIT_DOMAIN_MSG added to mark start of exit domain
 * New in version 1.3:
 *	- Message DLM_DEREF_LOCKRES_DONE added to inform non-master that the
 *	  refmap is cleared
 */
static const struct dlm_protocol_version dlm_protocol = {
	.pv_major = 1,
	.pv_minor = 3,
};

#define DLM_DOMAIN_BACKOFF_MS 200

static int dlm_query_join_handler(struct o2net_msg *msg, u32 len, void *data,
				  void **ret_data);
static int dlm_assert_joined_handler(struct o2net_msg *msg, u32 len, void *data,
				     void **ret_data);
static int dlm_cancel_join_handler(struct o2net_msg *msg, u32 len, void *data,
				   void **ret_data);
static int dlm_query_region_handler(struct o2net_msg *msg, u32 len,
				    void *data, void **ret_data);
static int dlm_exit_domain_handler(struct o2net_msg *msg, u32 len, void *data,
				   void **ret_data);
static int dlm_protocol_compare(struct dlm_protocol_version *existing,
				struct dlm_protocol_version *request);

static void dlm_unregister_domain_handlers(struct dlm_ctxt *dlm);

void __dlm_unhash_lockres(struct dlm_ctxt *dlm, struct dlm_lock_resource *res)
{
	if (hlist_unhashed(&res->hash_node))
		return;

	mlog(0, "%s: Unhash res %.*s\n", dlm->name, res->lockname.len,
	     res->lockname.name);
	hlist_del_init(&res->hash_node);
	dlm_lockres_put(res);
}

void __dlm_insert_lockres(struct dlm_ctxt *dlm, struct dlm_lock_resource *res)
{
	struct hlist_head *bucket;

	assert_spin_locked(&dlm->spinlock);

	bucket = dlm_lockres_hash(dlm, res->lockname.hash);

	/* get a reference for our hashtable */
	dlm_lockres_get(res);

	hlist_add_head(&res->hash_node, bucket);

	mlog(0, "%s: Hash res %.*s\n", dlm->name, res->lockname.len,
	     res->lockname.name);
}

struct dlm_lock_resource * __dlm_lookup_lockres_full(struct dlm_ctxt *dlm,
						     const char *name,
						     unsigned int len,
						     unsigned int hash)
{
	struct hlist_head *bucket;
	struct dlm_lock_resource *res;

	mlog(0, "%.*s\n", len, name);

	assert_spin_locked(&dlm->spinlock);

	bucket = dlm_lockres_hash(dlm, hash);

	hlist_for_each_entry(res, bucket, hash_node) {
		if (res->lockname.name[0] != name[0])
			continue;
		if (unlikely(res->lockname.len != len))
			continue;
		if (memcmp(res->lockname.name + 1, name + 1, len - 1))
			continue;
		dlm_lockres_get(res);
		return res;
	}
	return NULL;
}

/* intended to be called by functions which do not care about lock
 * resources which are being purged (most net _handler functions).
 * this will return NULL for any lock resource which is found but
 * currently in the process of dropping its mastery reference.
 * use __dlm_lookup_lockres_full when you need the lock resource
 * regardless (e.g. dlm_get_lock_resource) */
struct dlm_lock_resource * __dlm_lookup_lockres(struct dlm_ctxt *dlm,
						const char *name,
						unsigned int len,
						unsigned int hash)
{
	struct dlm_lock_resource *res = NULL;

	mlog(0, "%.*s\n", len, name);

	assert_spin_locked(&dlm->spinlock);

	res = __dlm_lookup_lockres_full(dlm, name, len, hash);
	if (res) {
		spin_lock(&res->spinlock);
		if (res->state & DLM_LOCK_RES_DROPPING_REF) {
			spin_unlock(&res->spinlock);
			dlm_lockres_put(res);
			return NULL;
		}
		spin_unlock(&res->spinlock);
	}

	return res;
}

struct dlm_lock_resource * dlm_lookup_lockres(struct dlm_ctxt *dlm,
				    const char *name,
				    unsigned int len)
{
	struct dlm_lock_resource *res;
	unsigned int hash = dlm_lockid_hash(name, len);

	spin_lock(&dlm->spinlock);
	res = __dlm_lookup_lockres(dlm, name, len, hash);
	spin_unlock(&dlm->spinlock);
	return res;
}

static struct dlm_ctxt * __dlm_lookup_domain_full(const char *domain, int len)
{
	struct dlm_ctxt *tmp;

	assert_spin_locked(&dlm_domain_lock);

	/* tmp->name here is always NULL terminated,
	 * but domain may not be! */
	list_for_each_entry(tmp, &dlm_domains, list) {
		if (strlen(tmp->name) == len &&
		    memcmp(tmp->name, domain, len)==0)
			return tmp;
	}

	return NULL;
}

/* For null terminated domain strings ONLY */
static struct dlm_ctxt * __dlm_lookup_domain(const char *domain)
{
	assert_spin_locked(&dlm_domain_lock);

	return __dlm_lookup_domain_full(domain, strlen(domain));
}


/* returns true on one of two conditions:
 * 1) the domain does not exist
 * 2) the domain exists and it's state is "joined" */
static int dlm_wait_on_domain_helper(const char *domain)
{
	int ret = 0;
	struct dlm_ctxt *tmp = NULL;

	spin_lock(&dlm_domain_lock);

	tmp = __dlm_lookup_domain(domain);
	if (!tmp)
		ret = 1;
	else if (tmp->dlm_state == DLM_CTXT_JOINED)
		ret = 1;

	spin_unlock(&dlm_domain_lock);
	return ret;
}

static void dlm_free_ctxt_mem(struct dlm_ctxt *dlm)
{
	dlm_destroy_debugfs_subroot(dlm);

	if (dlm->lockres_hash)
		dlm_free_pagevec((void **)dlm->lockres_hash, DLM_HASH_PAGES);

	if (dlm->master_hash)
		dlm_free_pagevec((void **)dlm->master_hash, DLM_HASH_PAGES);

	kfree(dlm->name);
	kfree(dlm);
}

/* A little strange - this function will be called while holding
 * dlm_domain_lock and is expected to be holding it on the way out. We
 * will however drop and reacquire it multiple times */
static void dlm_ctxt_release(struct kref *kref)
{
	struct dlm_ctxt *dlm;

	dlm = container_of(kref, struct dlm_ctxt, dlm_refs);

	BUG_ON(dlm->num_joins);
	BUG_ON(dlm->dlm_state == DLM_CTXT_JOINED);

	/* we may still be in the list if we hit an error during join. */
	list_del_init(&dlm->list);

	spin_unlock(&dlm_domain_lock);

	mlog(0, "freeing memory from domain %s\n", dlm->name);

	wake_up(&dlm_domain_events);

	dlm_free_ctxt_mem(dlm);

	spin_lock(&dlm_domain_lock);
}

void dlm_put(struct dlm_ctxt *dlm)
{
	spin_lock(&dlm_domain_lock);
	kref_put(&dlm->dlm_refs, dlm_ctxt_release);
	spin_unlock(&dlm_domain_lock);
}

static void __dlm_get(struct dlm_ctxt *dlm)
{
	kref_get(&dlm->dlm_refs);
}

/* given a questionable reference to a dlm object, gets a reference if
 * it can find it in the list, otherwise returns NULL in which case
 * you shouldn't trust your pointer. */
struct dlm_ctxt *dlm_grab(struct dlm_ctxt *dlm)
{
	struct dlm_ctxt *target;
	struct dlm_ctxt *ret = NULL;

	spin_lock(&dlm_domain_lock);

	list_for_each_entry(target, &dlm_domains, list) {
		if (target == dlm) {
			__dlm_get(target);
			ret = target;
			break;
		}
	}

	spin_unlock(&dlm_domain_lock);

	return ret;
}

int dlm_domain_fully_joined(struct dlm_ctxt *dlm)
{
	int ret;

	spin_lock(&dlm_domain_lock);
	ret = (dlm->dlm_state == DLM_CTXT_JOINED) ||
		(dlm->dlm_state == DLM_CTXT_IN_SHUTDOWN);
	spin_unlock(&dlm_domain_lock);

	return ret;
}

static void dlm_destroy_dlm_worker(struct dlm_ctxt *dlm)
{
	if (dlm->dlm_worker) {
		destroy_workqueue(dlm->dlm_worker);
		dlm->dlm_worker = NULL;
	}
}

static void dlm_complete_dlm_shutdown(struct dlm_ctxt *dlm)
{
	dlm_unregister_domain_handlers(dlm);
	dlm_complete_thread(dlm);
	dlm_complete_recovery_thread(dlm);
	dlm_destroy_dlm_worker(dlm);

	/* We've left the domain. Now we can take ourselves out of the
	 * list and allow the kref stuff to help us free the
	 * memory. */
	spin_lock(&dlm_domain_lock);
	list_del_init(&dlm->list);
	spin_unlock(&dlm_domain_lock);

	/* Wake up anyone waiting for us to remove this domain */
	wake_up(&dlm_domain_events);
}

static int dlm_migrate_all_locks(struct dlm_ctxt *dlm)
{
	int i, num, n, ret = 0;
	struct dlm_lock_resource *res;
	struct hlist_node *iter;
	struct hlist_head *bucket;
	int dropped;

	mlog(0, "Migrating locks from domain %s\n", dlm->name);

	num = 0;
	spin_lock(&dlm->spinlock);
	for (i = 0; i < DLM_HASH_BUCKETS; i++) {
redo_bucket:
		n = 0;
		bucket = dlm_lockres_hash(dlm, i);
		iter = bucket->first;
		while (iter) {
			n++;
			res = hlist_entry(iter, struct dlm_lock_resource,
					  hash_node);
			dlm_lockres_get(res);
			/* migrate, if necessary.  this will drop the dlm
			 * spinlock and retake it if it does migration. */
			dropped = dlm_empty_lockres(dlm, res);

			spin_lock(&res->spinlock);
			if (dropped)
				__dlm_lockres_calc_usage(dlm, res);
			else
				iter = res->hash_node.next;
			spin_unlock(&res->spinlock);

			dlm_lockres_put(res);

			if (dropped) {
				cond_resched_lock(&dlm->spinlock);
				goto redo_bucket;
			}
		}
		cond_resched_lock(&dlm->spinlock);
		num += n;
	}

	if (!num) {
		if (dlm->reco.state & DLM_RECO_STATE_ACTIVE) {
			mlog(0, "%s: perhaps there are more lock resources "
			     "need to be migrated after dlm recovery\n", dlm->name);
			ret = -EAGAIN;
		} else {
			mlog(0, "%s: we won't do dlm recovery after migrating "
			     "all lock resources\n", dlm->name);
			dlm->migrate_done = 1;
		}
	}

	spin_unlock(&dlm->spinlock);
	wake_up(&dlm->dlm_thread_wq);

	/* let the dlm thread take care of purging, keep scanning until
	 * nothing remains in the hash */
	if (num) {
		mlog(0, "%s: %d lock resources in hash last pass\n",
		     dlm->name, num);
		ret = -EAGAIN;
	}
	mlog(0, "DONE Migrating locks from domain %s\n", dlm->name);
	return ret;
}

static int dlm_no_joining_node(struct dlm_ctxt *dlm)
{
	int ret;

	spin_lock(&dlm->spinlock);
	ret = dlm->joining_node == DLM_LOCK_RES_OWNER_UNKNOWN;
	spin_unlock(&dlm->spinlock);

	return ret;
}

static int dlm_begin_exit_domain_handler(struct o2net_msg *msg, u32 len,
					 void *data, void **ret_data)
{
	struct dlm_ctxt *dlm = data;
	unsigned int node;
	struct dlm_exit_domain *exit_msg = (struct dlm_exit_domain *) msg->buf;

	if (!dlm_grab(dlm))
		return 0;

	node = exit_msg->node_idx;
	mlog(0, "%s: Node %u sent a begin exit domain message\n", dlm->name, node);

	spin_lock(&dlm->spinlock);
	set_bit(node, dlm->exit_domain_map);
	spin_unlock(&dlm->spinlock);

	dlm_put(dlm);

	return 0;
}

static void dlm_mark_domain_leaving(struct dlm_ctxt *dlm)
{
	/* Yikes, a double spinlock! I need domain_lock for the dlm
	 * state and the dlm spinlock for join state... Sorry! */
again:
	spin_lock(&dlm_domain_lock);
	spin_lock(&dlm->spinlock);

	if (dlm->joining_node != DLM_LOCK_RES_OWNER_UNKNOWN) {
		mlog(0, "Node %d is joining, we wait on it.\n",
			  dlm->joining_node);
		spin_unlock(&dlm->spinlock);
		spin_unlock(&dlm_domain_lock);

		wait_event(dlm->dlm_join_events, dlm_no_joining_node(dlm));
		goto again;
	}

	dlm->dlm_state = DLM_CTXT_LEAVING;
	spin_unlock(&dlm->spinlock);
	spin_unlock(&dlm_domain_lock);
}

static void __dlm_print_nodes(struct dlm_ctxt *dlm)
{
	int node = -1, num = 0;

	assert_spin_locked(&dlm->spinlock);

	printk("( ");
	while ((node = find_next_bit(dlm->domain_map, O2NM_MAX_NODES,
				     node + 1)) < O2NM_MAX_NODES) {
		printk("%d ", node);
		++num;
	}
	printk(") %u nodes\n", num);
}

static int dlm_exit_domain_handler(struct o2net_msg *msg, u32 len, void *data,
				   void **ret_data)
{
	struct dlm_ctxt *dlm = data;
	unsigned int node;
	struct dlm_exit_domain *exit_msg = (struct dlm_exit_domain *) msg->buf;

	mlog(0, "%p %u %p", msg, len, data);

	if (!dlm_grab(dlm))
		return 0;

	node = exit_msg->node_idx;

	spin_lock(&dlm->spinlock);
	clear_bit(node, dlm->domain_map);
	clear_bit(node, dlm->exit_domain_map);
	printk(KERN_NOTICE "o2dlm: Node %u leaves domain %s ", node, dlm->name);
	__dlm_print_nodes(dlm);

	/* notify anything attached to the heartbeat events */
	dlm_hb_event_notify_attached(dlm, node, 0);

	spin_unlock(&dlm->spinlock);

	dlm_put(dlm);

	return 0;
}

static int dlm_send_one_domain_exit(struct dlm_ctxt *dlm, u32 msg_type,
				    unsigned int node)
{
	int status;
	struct dlm_exit_domain leave_msg;

	mlog(0, "%s: Sending domain exit message %u to node %u\n", dlm->name,
	     msg_type, node);

	memset(&leave_msg, 0, sizeof(leave_msg));
	leave_msg.node_idx = dlm->node_num;

	status = o2net_send_message(msg_type, dlm->key, &leave_msg,
				    sizeof(leave_msg), node, NULL);
	if (status < 0)
		mlog(ML_ERROR, "Error %d sending domain exit message %u "
		     "to node %u on domain %s\n", status, msg_type, node,
		     dlm->name);

	return status;
}

static void dlm_begin_exit_domain(struct dlm_ctxt *dlm)
{
	int node = -1;

	/* Support for begin exit domain was added in 1.2 */
	if (dlm->dlm_locking_proto.pv_major == 1 &&
	    dlm->dlm_locking_proto.pv_minor < 2)
		return;

	/*
	 * Unlike DLM_EXIT_DOMAIN_MSG, DLM_BEGIN_EXIT_DOMAIN_MSG is purely
	 * informational. Meaning if a node does not receive the message,
	 * so be it.
	 */
	spin_lock(&dlm->spinlock);
	while (1) {
		node = find_next_bit(dlm->domain_map, O2NM_MAX_NODES, node + 1);
		if (node >= O2NM_MAX_NODES)
			break;
		if (node == dlm->node_num)
			continue;

		spin_unlock(&dlm->spinlock);
		dlm_send_one_domain_exit(dlm, DLM_BEGIN_EXIT_DOMAIN_MSG, node);
		spin_lock(&dlm->spinlock);
	}
	spin_unlock(&dlm->spinlock);
}

static void dlm_leave_domain(struct dlm_ctxt *dlm)
{
	int node, clear_node, status;

	/* At this point we've migrated away all our locks and won't
	 * accept mastership of new ones. The dlm is responsible for
	 * almost nothing now. We make sure not to confuse any joining
	 * nodes and then commence shutdown procedure. */

	spin_lock(&dlm->spinlock);
	/* Clear ourselves from the domain map */
	clear_bit(dlm->node_num, dlm->domain_map);
	while ((node = find_next_bit(dlm->domain_map, O2NM_MAX_NODES,
				     0)) < O2NM_MAX_NODES) {
		/* Drop the dlm spinlock. This is safe wrt the domain_map.
		 * -nodes cannot be added now as the
		 *   query_join_handlers knows to respond with OK_NO_MAP
		 * -we catch the right network errors if a node is
		 *   removed from the map while we're sending him the
		 *   exit message. */
		spin_unlock(&dlm->spinlock);

		clear_node = 1;

		status = dlm_send_one_domain_exit(dlm, DLM_EXIT_DOMAIN_MSG,
						  node);
		if (status < 0 &&
		    status != -ENOPROTOOPT &&
		    status != -ENOTCONN) {
			mlog(ML_NOTICE, "Error %d sending domain exit message "
			     "to node %d\n", status, node);

			/* Not sure what to do here but lets sleep for
			 * a bit in case this was a transient
			 * error... */
			msleep(DLM_DOMAIN_BACKOFF_MS);
			clear_node = 0;
		}

		spin_lock(&dlm->spinlock);
		/* If we're not clearing the node bit then we intend
		 * to loop back around to try again. */
		if (clear_node)
			clear_bit(node, dlm->domain_map);
	}
	spin_unlock(&dlm->spinlock);
}

void dlm_unregister_domain(struct dlm_ctxt *dlm)
{
	int leave = 0;
	struct dlm_lock_resource *res;

	spin_lock(&dlm_domain_lock);
	BUG_ON(dlm->dlm_state != DLM_CTXT_JOINED);
	BUG_ON(!dlm->num_joins);

	dlm->num_joins--;
	if (!dlm->num_joins) {
		/* We mark it "in shutdown" now so new register
		 * requests wait until we've completely left the
		 * domain. Don't use DLM_CTXT_LEAVING yet as we still
		 * want new domain joins to communicate with us at
		 * least until we've completed migration of our
		 * resources. */
		dlm->dlm_state = DLM_CTXT_IN_SHUTDOWN;
		leave = 1;
	}
	spin_unlock(&dlm_domain_lock);

	if (leave) {
		mlog(0, "shutting down domain %s\n", dlm->name);
		dlm_begin_exit_domain(dlm);

		/* We changed dlm state, notify the thread */
		dlm_kick_thread(dlm, NULL);

		while (dlm_migrate_all_locks(dlm)) {
			/* Give dlm_thread time to purge the lockres' */
			msleep(500);
			mlog(0, "%s: more migration to do\n", dlm->name);
		}

		/* This list should be empty. If not, print remaining lockres */
		if (!list_empty(&dlm->tracking_list)) {
			mlog(ML_ERROR, "Following lockres' are still on the "
			     "tracking list:\n");
			list_for_each_entry(res, &dlm->tracking_list, tracking)
				dlm_print_one_lock_resource(res);
		}

		dlm_mark_domain_leaving(dlm);
		dlm_leave_domain(dlm);
		printk(KERN_NOTICE "o2dlm: Leaving domain %s\n", dlm->name);
		dlm_force_free_mles(dlm);
		dlm_complete_dlm_shutdown(dlm);
	}
	dlm_put(dlm);
}
EXPORT_SYMBOL_GPL(dlm_unregister_domain);

static int dlm_query_join_proto_check(char *proto_type, int node,
				      struct dlm_protocol_version *ours,
				      struct dlm_protocol_version *request)
{
	int rc;
	struct dlm_protocol_version proto = *request;

	if (!dlm_protocol_compare(ours, &proto)) {
		mlog(0,
		     "node %u wanted to join with %s locking protocol "
		     "%u.%u, we respond with %u.%u\n",
		     node, proto_type,
		     request->pv_major,
		     request->pv_minor,
		     proto.pv_major, proto.pv_minor);
		request->pv_minor = proto.pv_minor;
		rc = 0;
	} else {
		mlog(ML_NOTICE,
		     "Node %u wanted to join with %s locking "
		     "protocol %u.%u, but we have %u.%u, disallowing\n",
		     node, proto_type,
		     request->pv_major,
		     request->pv_minor,
		     ours->pv_major,
		     ours->pv_minor);
		rc = 1;
	}

	return rc;
}

/*
 * struct dlm_query_join_packet is made up of four one-byte fields.  They
 * are effectively in big-endian order already.  However, little-endian
 * machines swap them before putting the packet on the wire (because
 * query_join's response is a status, and that status is treated as a u32
 * on the wire).  Thus, a big-endian and little-endian machines will treat
 * this structure differently.
 *
 * The solution is to have little-endian machines swap the structure when
 * converting from the structure to the u32 representation.  This will
 * result in the structure having the correct format on the wire no matter
 * the host endian format.
 */
static void dlm_query_join_packet_to_wire(struct dlm_query_join_packet *packet,
					  u32 *wire)
{
	union dlm_query_join_response response;

	response.packet = *packet;
	*wire = be32_to_cpu(response.intval);
}

static void dlm_query_join_wire_to_packet(u32 wire,
					  struct dlm_query_join_packet *packet)
{
	union dlm_query_join_response response;

	response.intval = cpu_to_be32(wire);
	*packet = response.packet;
}

static int dlm_query_join_handler(struct o2net_msg *msg, u32 len, void *data,
				  void **ret_data)
{
	struct dlm_query_join_request *query;
	struct dlm_query_join_packet packet = {
		.code = JOIN_DISALLOW,
	};
	struct dlm_ctxt *dlm = NULL;
	u32 response;
	u8 nodenum;

	query = (struct dlm_query_join_request *) msg->buf;

	mlog(0, "node %u wants to join domain %s\n", query->node_idx,
		  query->domain);

	/*
	 * If heartbeat doesn't consider the node live, tell it
	 * to back off and try again.  This gives heartbeat a chance
	 * to catch up.
	 */
	if (!o2hb_check_node_heartbeating_no_sem(query->node_idx)) {
		mlog(0, "node %u is not in our live map yet\n",
		     query->node_idx);

		packet.code = JOIN_DISALLOW;
		goto respond;
	}

	packet.code = JOIN_OK_NO_MAP;

	spin_lock(&dlm_domain_lock);
	dlm = __dlm_lookup_domain_full(query->domain, query->name_len);
	if (!dlm)
		goto unlock_respond;

	/*
	 * There is a small window where the joining node may not see the
	 * node(s) that just left but still part of the cluster. DISALLOW
	 * join request if joining node has different node map.
	 */
	nodenum=0;
	while (nodenum < O2NM_MAX_NODES) {
		if (test_bit(nodenum, dlm->domain_map)) {
			if (!byte_test_bit(nodenum, query->node_map)) {
				mlog(0, "disallow join as node %u does not "
				     "have node %u in its nodemap\n",
				     query->node_idx, nodenum);
				packet.code = JOIN_DISALLOW;
				goto unlock_respond;
			}
		}
		nodenum++;
	}

	/* Once the dlm ctxt is marked as leaving then we don't want
	 * to be put in someone's domain map.
	 * Also, explicitly disallow joining at certain troublesome
	 * times (ie. during recovery). */
	if (dlm->dlm_state != DLM_CTXT_LEAVING) {
		int bit = query->node_idx;
		spin_lock(&dlm->spinlock);

		if (dlm->dlm_state == DLM_CTXT_NEW &&
		    dlm->joining_node == DLM_LOCK_RES_OWNER_UNKNOWN) {
			/*If this is a brand new context and we
			 * haven't started our join process yet, then
			 * the other node won the race. */
			packet.code = JOIN_OK_NO_MAP;
		} else if (dlm->joining_node != DLM_LOCK_RES_OWNER_UNKNOWN) {
			/* Disallow parallel joins. */
			packet.code = JOIN_DISALLOW;
		} else if (dlm->reco.state & DLM_RECO_STATE_ACTIVE) {
			mlog(0, "node %u trying to join, but recovery "
			     "is ongoing.\n", bit);
			packet.code = JOIN_DISALLOW;
		} else if (test_bit(bit, dlm->recovery_map)) {
			mlog(0, "node %u trying to join, but it "
			     "still needs recovery.\n", bit);
			packet.code = JOIN_DISALLOW;
		} else if (test_bit(bit, dlm->domain_map)) {
			mlog(0, "node %u trying to join, but it "
			     "is still in the domain! needs recovery?\n",
			     bit);
			packet.code = JOIN_DISALLOW;
		} else {
			/* Alright we're fully a part of this domain
			 * so we keep some state as to who's joining
			 * and indicate to him that needs to be fixed
			 * up. */

			/* Make sure we speak compatible locking protocols.  */
			if (dlm_query_join_proto_check("DLM", bit,
						       &dlm->dlm_locking_proto,
						       &query->dlm_proto)) {
				packet.code = JOIN_PROTOCOL_MISMATCH;
			} else if (dlm_query_join_proto_check("fs", bit,
							      &dlm->fs_locking_proto,
							      &query->fs_proto)) {
				packet.code = JOIN_PROTOCOL_MISMATCH;
			} else {
				packet.dlm_minor = query->dlm_proto.pv_minor;
				packet.fs_minor = query->fs_proto.pv_minor;
				packet.code = JOIN_OK;
				__dlm_set_joining_node(dlm, query->node_idx);
			}
		}

		spin_unlock(&dlm->spinlock);
	}
unlock_respond:
	spin_unlock(&dlm_domain_lock);

respond:
	mlog(0, "We respond with %u\n", packet.code);

	dlm_query_join_packet_to_wire(&packet, &response);
	return response;
}

static int dlm_assert_joined_handler(struct o2net_msg *msg, u32 len, void *data,
				     void **ret_data)
{
	struct dlm_assert_joined *assert;
	struct dlm_ctxt *dlm = NULL;

	assert = (struct dlm_assert_joined *) msg->buf;

	mlog(0, "node %u asserts join on domain %s\n", assert->node_idx,
		  assert->domain);

	spin_lock(&dlm_domain_lock);
	dlm = __dlm_lookup_domain_full(assert->domain, assert->name_len);
	/* XXX should we consider no dlm ctxt an error? */
	if (dlm) {
		spin_lock(&dlm->spinlock);

		/* Alright, this node has officially joined our
		 * domain. Set him in the map and clean up our
		 * leftover join state. */
		BUG_ON(dlm->joining_node != assert->node_idx);

		if (dlm->reco.state & DLM_RECO_STATE_ACTIVE) {
			mlog(0, "dlm recovery is ongoing, disallow join\n");
			spin_unlock(&dlm->spinlock);
			spin_unlock(&dlm_domain_lock);
			return -EAGAIN;
		}

		set_bit(assert->node_idx, dlm->domain_map);
		clear_bit(assert->node_idx, dlm->exit_domain_map);
		__dlm_set_joining_node(dlm, DLM_LOCK_RES_OWNER_UNKNOWN);

		printk(KERN_NOTICE "o2dlm: Node %u joins domain %s ",
		       assert->node_idx, dlm->name);
		__dlm_print_nodes(dlm);

		/* notify anything attached to the heartbeat events */
		dlm_hb_event_notify_attached(dlm, assert->node_idx, 1);

		spin_unlock(&dlm->spinlock);
	}
	spin_unlock(&dlm_domain_lock);

	return 0;
}

static int dlm_match_regions(struct dlm_ctxt *dlm,
			     struct dlm_query_region *qr,
			     char *local, int locallen)
{
	char *remote = qr->qr_regions;
	char *l, *r;
	int localnr, i, j, foundit;
	int status = 0;

	if (!o2hb_global_heartbeat_active()) {
		if (qr->qr_numregions) {
			mlog(ML_ERROR, "Domain %s: Joining node %d has global "
			     "heartbeat enabled but local node %d does not\n",
			     qr->qr_domain, qr->qr_node, dlm->node_num);
			status = -EINVAL;
		}
		goto bail;
	}

	if (o2hb_global_heartbeat_active() && !qr->qr_numregions) {
		mlog(ML_ERROR, "Domain %s: Local node %d has global "
		     "heartbeat enabled but joining node %d does not\n",
		     qr->qr_domain, dlm->node_num, qr->qr_node);
		status = -EINVAL;
		goto bail;
	}

	r = remote;
	for (i = 0; i < qr->qr_numregions; ++i) {
		mlog(0, "Region %.*s\n", O2HB_MAX_REGION_NAME_LEN, r);
		r += O2HB_MAX_REGION_NAME_LEN;
	}

	localnr = min(O2NM_MAX_REGIONS, locallen/O2HB_MAX_REGION_NAME_LEN);
	localnr = o2hb_get_all_regions(local, (u8)localnr);

	/* compare local regions with remote */
	l = local;
	for (i = 0; i < localnr; ++i) {
		foundit = 0;
		r = remote;
		for (j = 0; j <= qr->qr_numregions; ++j) {
			if (!memcmp(l, r, O2HB_MAX_REGION_NAME_LEN)) {
				foundit = 1;
				break;
			}
			r += O2HB_MAX_REGION_NAME_LEN;
		}
		if (!foundit) {
			status = -EINVAL;
			mlog(ML_ERROR, "Domain %s: Region '%.*s' registered "
			     "in local node %d but not in joining node %d\n",
			     qr->qr_domain, O2HB_MAX_REGION_NAME_LEN, l,
			     dlm->node_num, qr->qr_node);
			goto bail;
		}
		l += O2HB_MAX_REGION_NAME_LEN;
	}

	/* compare remote with local regions */
	r = remote;
	for (i = 0; i < qr->qr_numregions; ++i) {
		foundit = 0;
		l = local;
		for (j = 0; j < localnr; ++j) {
			if (!memcmp(r, l, O2HB_MAX_REGION_NAME_LEN)) {
				foundit = 1;
				break;
			}
			l += O2HB_MAX_REGION_NAME_LEN;
		}
		if (!foundit) {
			status = -EINVAL;
			mlog(ML_ERROR, "Domain %s: Region '%.*s' registered "
			     "in joining node %d but not in local node %d\n",
			     qr->qr_domain, O2HB_MAX_REGION_NAME_LEN, r,
			     qr->qr_node, dlm->node_num);
			goto bail;
		}
		r += O2HB_MAX_REGION_NAME_LEN;
	}

bail:
	return status;
}

static int dlm_send_regions(struct dlm_ctxt *dlm, unsigned long *node_map)
{
	struct dlm_query_region *qr = NULL;
	int status, ret = 0, i;
	char *p;

	if (find_first_bit(node_map, O2NM_MAX_NODES) >= O2NM_MAX_NODES)
		goto bail;

	qr = kzalloc(sizeof(struct dlm_query_region), GFP_KERNEL);
	if (!qr) {
		ret = -ENOMEM;
		mlog_errno(ret);
		goto bail;
	}

	qr->qr_node = dlm->node_num;
	qr->qr_namelen = strlen(dlm->name);
	memcpy(qr->qr_domain, dlm->name, qr->qr_namelen);
	/* if local hb, the numregions will be zero */
	if (o2hb_global_heartbeat_active())
		qr->qr_numregions = o2hb_get_all_regions(qr->qr_regions,
							 O2NM_MAX_REGIONS);

	p = qr->qr_regions;
	for (i = 0; i < qr->qr_numregions; ++i, p += O2HB_MAX_REGION_NAME_LEN)
		mlog(0, "Region %.*s\n", O2HB_MAX_REGION_NAME_LEN, p);

	i = -1;
	while ((i = find_next_bit(node_map, O2NM_MAX_NODES,
				  i + 1)) < O2NM_MAX_NODES) {
		if (i == dlm->node_num)
			continue;

		mlog(0, "Sending regions to node %d\n", i);

		ret = o2net_send_message(DLM_QUERY_REGION, DLM_MOD_KEY, qr,
					 sizeof(struct dlm_query_region),
					 i, &status);
		if (ret >= 0)
			ret = status;
		if (ret) {
			mlog(ML_ERROR, "Region mismatch %d, node %d\n",
			     ret, i);
			break;
		}
	}

bail:
	kfree(qr);
	return ret;
}

static int dlm_query_region_handler(struct o2net_msg *msg, u32 len,
				    void *data, void **ret_data)
{
	struct dlm_query_region *qr;
	struct dlm_ctxt *dlm = NULL;
	char *local = NULL;
	int status = 0;

	qr = (struct dlm_query_region *) msg->buf;

	mlog(0, "Node %u queries hb regions on domain %s\n", qr->qr_node,
	     qr->qr_domain);

	/* buffer used in dlm_mast_regions() */
	local = kmalloc(sizeof(qr->qr_regions), GFP_KERNEL);
	if (!local)
		return -ENOMEM;

	status = -EINVAL;

	spin_lock(&dlm_domain_lock);
	dlm = __dlm_lookup_domain_full(qr->qr_domain, qr->qr_namelen);
	if (!dlm) {
		mlog(ML_ERROR, "Node %d queried hb regions on domain %s "
		     "before join domain\n", qr->qr_node, qr->qr_domain);
		goto out_domain_lock;
	}

	spin_lock(&dlm->spinlock);
	if (dlm->joining_node != qr->qr_node) {
		mlog(ML_ERROR, "Node %d queried hb regions on domain %s "
		     "but joining node is %d\n", qr->qr_node, qr->qr_domain,
		     dlm->joining_node);
		goto out_dlm_lock;
	}

	/* Support for global heartbeat was added in 1.1 */
	if (dlm->dlm_locking_proto.pv_major == 1 &&
	    dlm->dlm_locking_proto.pv_minor == 0) {
		mlog(ML_ERROR, "Node %d queried hb regions on domain %s "
		     "but active dlm protocol is %d.%d\n", qr->qr_node,
		     qr->qr_domain, dlm->dlm_locking_proto.pv_major,
		     dlm->dlm_locking_proto.pv_minor);
		goto out_dlm_lock;
	}

	status = dlm_match_regions(dlm, qr, local, sizeof(qr->qr_regions));

out_dlm_lock:
	spin_unlock(&dlm->spinlock);

out_domain_lock:
	spin_unlock(&dlm_domain_lock);

	kfree(local);

	return status;
}

static int dlm_match_nodes(struct dlm_ctxt *dlm, struct dlm_query_nodeinfo *qn)
{
	struct o2nm_node *local;
	struct dlm_node_info *remote;
	int i, j;
	int status = 0;

	for (j = 0; j < qn->qn_numnodes; ++j)
		mlog(0, "Node %3d, %pI4:%u\n", qn->qn_nodes[j].ni_nodenum,
		     &(qn->qn_nodes[j].ni_ipv4_address),
		     ntohs(qn->qn_nodes[j].ni_ipv4_port));

	for (i = 0; i < O2NM_MAX_NODES && !status; ++i) {
		local = o2nm_get_node_by_num(i);
		remote = NULL;
		for (j = 0; j < qn->qn_numnodes; ++j) {
			if (qn->qn_nodes[j].ni_nodenum == i) {
				remote = &(qn->qn_nodes[j]);
				break;
			}
		}

		if (!local && !remote)
			continue;

		if ((local && !remote) || (!local && remote))
			status = -EINVAL;

		if (!status &&
		    ((remote->ni_nodenum != local->nd_num) ||
		     (remote->ni_ipv4_port != local->nd_ipv4_port) ||
		     (remote->ni_ipv4_address != local->nd_ipv4_address)))
			status = -EINVAL;

		if (status) {
			if (remote && !local)
				mlog(ML_ERROR, "Domain %s: Node %d (%pI4:%u) "
				     "registered in joining node %d but not in "
				     "local node %d\n", qn->qn_domain,
				     remote->ni_nodenum,
				     &(remote->ni_ipv4_address),
				     ntohs(remote->ni_ipv4_port),
				     qn->qn_nodenum, dlm->node_num);
			if (local && !remote)
				mlog(ML_ERROR, "Domain %s: Node %d (%pI4:%u) "
				     "registered in local node %d but not in "
				     "joining node %d\n", qn->qn_domain,
				     local->nd_num, &(local->nd_ipv4_address),
				     ntohs(local->nd_ipv4_port),
				     dlm->node_num, qn->qn_nodenum);
			BUG_ON((!local && !remote));
		}

		if (local)
			o2nm_node_put(local);
	}

	return status;
}

static int dlm_send_nodeinfo(struct dlm_ctxt *dlm, unsigned long *node_map)
{
	struct dlm_query_nodeinfo *qn = NULL;
	struct o2nm_node *node;
	int ret = 0, status, count, i;

	if (find_first_bit(node_map, O2NM_MAX_NODES) >= O2NM_MAX_NODES)
		goto bail;

	qn = kzalloc(sizeof(struct dlm_query_nodeinfo), GFP_KERNEL);
	if (!qn) {
		ret = -ENOMEM;
		mlog_errno(ret);
		goto bail;
	}

	for (i = 0, count = 0; i < O2NM_MAX_NODES; ++i) {
		node = o2nm_get_node_by_num(i);
		if (!node)
			continue;
		qn->qn_nodes[count].ni_nodenum = node->nd_num;
		qn->qn_nodes[count].ni_ipv4_port = node->nd_ipv4_port;
		qn->qn_nodes[count].ni_ipv4_address = node->nd_ipv4_address;
		mlog(0, "Node %3d, %pI4:%u\n", node->nd_num,
		     &(node->nd_ipv4_address), ntohs(node->nd_ipv4_port));
		++count;
		o2nm_node_put(node);
	}

	qn->qn_nodenum = dlm->node_num;
	qn->qn_numnodes = count;
	qn->qn_namelen = strlen(dlm->name);
	memcpy(qn->qn_domain, dlm->name, qn->qn_namelen);

	i = -1;
	while ((i = find_next_bit(node_map, O2NM_MAX_NODES,
				  i + 1)) < O2NM_MAX_NODES) {
		if (i == dlm->node_num)
			continue;

		mlog(0, "Sending nodeinfo to node %d\n", i);

		ret = o2net_send_message(DLM_QUERY_NODEINFO, DLM_MOD_KEY,
					 qn, sizeof(struct dlm_query_nodeinfo),
					 i, &status);
		if (ret >= 0)
			ret = status;
		if (ret) {
			mlog(ML_ERROR, "node mismatch %d, node %d\n", ret, i);
			break;
		}
	}

bail:
	kfree(qn);
	return ret;
}

static int dlm_query_nodeinfo_handler(struct o2net_msg *msg, u32 len,
				      void *data, void **ret_data)
{
	struct dlm_query_nodeinfo *qn;
	struct dlm_ctxt *dlm = NULL;
	int locked = 0, status = -EINVAL;

	qn = (struct dlm_query_nodeinfo *) msg->buf;

	mlog(0, "Node %u queries nodes on domain %s\n", qn->qn_nodenum,
	     qn->qn_domain);

	spin_lock(&dlm_domain_lock);
	dlm = __dlm_lookup_domain_full(qn->qn_domain, qn->qn_namelen);
	if (!dlm) {
		mlog(ML_ERROR, "Node %d queried nodes on domain %s before "
		     "join domain\n", qn->qn_nodenum, qn->qn_domain);
		goto bail;
	}

	spin_lock(&dlm->spinlock);
	locked = 1;
	if (dlm->joining_node != qn->qn_nodenum) {
		mlog(ML_ERROR, "Node %d queried nodes on domain %s but "
		     "joining node is %d\n", qn->qn_nodenum, qn->qn_domain,
		     dlm->joining_node);
		goto bail;
	}

	/* Support for node query was added in 1.1 */
	if (dlm->dlm_locking_proto.pv_major == 1 &&
	    dlm->dlm_locking_proto.pv_minor == 0) {
		mlog(ML_ERROR, "Node %d queried nodes on domain %s "
		     "but active dlm protocol is %d.%d\n", qn->qn_nodenum,
		     qn->qn_domain, dlm->dlm_locking_proto.pv_major,
		     dlm->dlm_locking_proto.pv_minor);
		goto bail;
	}

	status = dlm_match_nodes(dlm, qn);

bail:
	if (locked)
		spin_unlock(&dlm->spinlock);
	spin_unlock(&dlm_domain_lock);

	return status;
}

static int dlm_cancel_join_handler(struct o2net_msg *msg, u32 len, void *data,
				   void **ret_data)
{
	struct dlm_cancel_join *cancel;
	struct dlm_ctxt *dlm = NULL;

	cancel = (struct dlm_cancel_join *) msg->buf;

	mlog(0, "node %u cancels join on domain %s\n", cancel->node_idx,
		  cancel->domain);

	spin_lock(&dlm_domain_lock);
	dlm = __dlm_lookup_domain_full(cancel->domain, cancel->name_len);

	if (dlm) {
		spin_lock(&dlm->spinlock);

		/* Yikes, this guy wants to cancel his join. No
		 * problem, we simply cleanup our join state. */
		BUG_ON(dlm->joining_node != cancel->node_idx);
		__dlm_set_joining_node(dlm, DLM_LOCK_RES_OWNER_UNKNOWN);

		spin_unlock(&dlm->spinlock);
	}
	spin_unlock(&dlm_domain_lock);

	return 0;
}

static int dlm_send_one_join_cancel(struct dlm_ctxt *dlm,
				    unsigned int node)
{
	int status;
	struct dlm_cancel_join cancel_msg;

	memset(&cancel_msg, 0, sizeof(cancel_msg));
	cancel_msg.node_idx = dlm->node_num;
	cancel_msg.name_len = strlen(dlm->name);
	memcpy(cancel_msg.domain, dlm->name, cancel_msg.name_len);

	status = o2net_send_message(DLM_CANCEL_JOIN_MSG, DLM_MOD_KEY,
				    &cancel_msg, sizeof(cancel_msg), node,
				    NULL);
	if (status < 0) {
		mlog(ML_ERROR, "Error %d when sending message %u (key 0x%x) to "
		     "node %u\n", status, DLM_CANCEL_JOIN_MSG, DLM_MOD_KEY,
		     node);
		goto bail;
	}

bail:
	return status;
}

/* map_size should be in bytes. */
static int dlm_send_join_cancels(struct dlm_ctxt *dlm,
				 unsigned long *node_map,
				 unsigned int map_size)
{
	int status, tmpstat;
	int node;

	if (map_size != (BITS_TO_LONGS(O2NM_MAX_NODES) *
			 sizeof(unsigned long))) {
		mlog(ML_ERROR,
		     "map_size %u != BITS_TO_LONGS(O2NM_MAX_NODES) %u\n",
		     map_size, (unsigned)BITS_TO_LONGS(O2NM_MAX_NODES));
		return -EINVAL;
	}

	status = 0;
	node = -1;
	while ((node = find_next_bit(node_map, O2NM_MAX_NODES,
				     node + 1)) < O2NM_MAX_NODES) {
		if (node == dlm->node_num)
			continue;

		tmpstat = dlm_send_one_join_cancel(dlm, node);
		if (tmpstat) {
			mlog(ML_ERROR, "Error return %d cancelling join on "
			     "node %d\n", tmpstat, node);
			if (!status)
				status = tmpstat;
		}
	}

	if (status)
		mlog_errno(status);
	return status;
}

static int dlm_request_join(struct dlm_ctxt *dlm,
			    int node,
			    enum dlm_query_join_response_code *response)
{
	int status;
	struct dlm_query_join_request join_msg;
	struct dlm_query_join_packet packet;
	u32 join_resp;

	mlog(0, "querying node %d\n", node);

	memset(&join_msg, 0, sizeof(join_msg));
	join_msg.node_idx = dlm->node_num;
	join_msg.name_len = strlen(dlm->name);
	memcpy(join_msg.domain, dlm->name, join_msg.name_len);
	join_msg.dlm_proto = dlm->dlm_locking_proto;
	join_msg.fs_proto = dlm->fs_locking_proto;

	/* copy live node map to join message */
	byte_copymap(join_msg.node_map, dlm->live_nodes_map, O2NM_MAX_NODES);

	status = o2net_send_message(DLM_QUERY_JOIN_MSG, DLM_MOD_KEY, &join_msg,
				    sizeof(join_msg), node, &join_resp);
	if (status < 0 && status != -ENOPROTOOPT) {
		mlog(ML_ERROR, "Error %d when sending message %u (key 0x%x) to "
		     "node %u\n", status, DLM_QUERY_JOIN_MSG, DLM_MOD_KEY,
		     node);
		goto bail;
	}
	dlm_query_join_wire_to_packet(join_resp, &packet);

	/* -ENOPROTOOPT from the net code means the other side isn't
	    listening for our message type -- that's fine, it means
	    his dlm isn't up, so we can consider him a 'yes' but not
	    joined into the domain.  */
	if (status == -ENOPROTOOPT) {
		status = 0;
		*response = JOIN_OK_NO_MAP;
	} else {
		*response = packet.code;
		switch (packet.code) {
		case JOIN_DISALLOW:
		case JOIN_OK_NO_MAP:
			break;
		case JOIN_PROTOCOL_MISMATCH:
			mlog(ML_NOTICE,
			     "This node requested DLM locking protocol %u.%u and "
			     "filesystem locking protocol %u.%u.  At least one of "
			     "the protocol versions on node %d is not compatible, "
			     "disconnecting\n",
			     dlm->dlm_locking_proto.pv_major,
			     dlm->dlm_locking_proto.pv_minor,
			     dlm->fs_locking_proto.pv_major,
			     dlm->fs_locking_proto.pv_minor,
			     node);
			status = -EPROTO;
			break;
		case JOIN_OK:
			/* Use the same locking protocol as the remote node */
			dlm->dlm_locking_proto.pv_minor = packet.dlm_minor;
			dlm->fs_locking_proto.pv_minor = packet.fs_minor;
			mlog(0,
			     "Node %d responds JOIN_OK with DLM locking protocol "
			     "%u.%u and fs locking protocol %u.%u\n",
			     node,
			     dlm->dlm_locking_proto.pv_major,
			     dlm->dlm_locking_proto.pv_minor,
			     dlm->fs_locking_proto.pv_major,
			     dlm->fs_locking_proto.pv_minor);
			break;
		default:
			status = -EINVAL;
			mlog(ML_ERROR, "invalid response %d from node %u\n",
			     packet.code, node);
			/* Reset response to JOIN_DISALLOW */
			*response = JOIN_DISALLOW;
			break;
		}
	}

	mlog(0, "status %d, node %d response is %d\n", status, node,
	     *response);

bail:
	return status;
}

static int dlm_send_one_join_assert(struct dlm_ctxt *dlm,
				    unsigned int node)
{
	int status;
	int ret;
	struct dlm_assert_joined assert_msg;

	mlog(0, "Sending join assert to node %u\n", node);

	memset(&assert_msg, 0, sizeof(assert_msg));
	assert_msg.node_idx = dlm->node_num;
	assert_msg.name_len = strlen(dlm->name);
	memcpy(assert_msg.domain, dlm->name, assert_msg.name_len);

	status = o2net_send_message(DLM_ASSERT_JOINED_MSG, DLM_MOD_KEY,
				    &assert_msg, sizeof(assert_msg), node,
				    &ret);
	if (status < 0)
		mlog(ML_ERROR, "Error %d when sending message %u (key 0x%x) to "
		     "node %u\n", status, DLM_ASSERT_JOINED_MSG, DLM_MOD_KEY,
		     node);
	else
		status = ret;

	return status;
}

static void dlm_send_join_asserts(struct dlm_ctxt *dlm,
				  unsigned long *node_map)
{
	int status, node, live;

	status = 0;
	node = -1;
	while ((node = find_next_bit(node_map, O2NM_MAX_NODES,
				     node + 1)) < O2NM_MAX_NODES) {
		if (node == dlm->node_num)
			continue;

		do {
			/* It is very important that this message be
			 * received so we spin until either the node
			 * has died or it gets the message. */
			status = dlm_send_one_join_assert(dlm, node);

			spin_lock(&dlm->spinlock);
			live = test_bit(node, dlm->live_nodes_map);
			spin_unlock(&dlm->spinlock);

			if (status) {
				mlog(ML_ERROR, "Error return %d asserting "
				     "join on node %d\n", status, node);

				/* give us some time between errors... */
				if (live)
					msleep(DLM_DOMAIN_BACKOFF_MS);
			}
		} while (status && live);
	}
}

struct domain_join_ctxt {
	unsigned long live_map[BITS_TO_LONGS(O2NM_MAX_NODES)];
	unsigned long yes_resp_map[BITS_TO_LONGS(O2NM_MAX_NODES)];
};

static int dlm_should_restart_join(struct dlm_ctxt *dlm,
				   struct domain_join_ctxt *ctxt,
				   enum dlm_query_join_response_code response)
{
	int ret;

	if (response == JOIN_DISALLOW) {
		mlog(0, "Latest response of disallow -- should restart\n");
		return 1;
	}

	spin_lock(&dlm->spinlock);
	/* For now, we restart the process if the node maps have
	 * changed at all */
	ret = memcmp(ctxt->live_map, dlm->live_nodes_map,
		     sizeof(dlm->live_nodes_map));
	spin_unlock(&dlm->spinlock);

	if (ret)
		mlog(0, "Node maps changed -- should restart\n");

	return ret;
}

static int dlm_try_to_join_domain(struct dlm_ctxt *dlm)
{
	int status = 0, tmpstat, node;
	struct domain_join_ctxt *ctxt;
	enum dlm_query_join_response_code response = JOIN_DISALLOW;

	mlog(0, "%p", dlm);

	ctxt = kzalloc(sizeof(*ctxt), GFP_KERNEL);
	if (!ctxt) {
		status = -ENOMEM;
		mlog_errno(status);
		goto bail;
	}

	/* group sem locking should work for us here -- we're already
	 * registered for heartbeat events so filling this should be
	 * atomic wrt getting those handlers called. */
	o2hb_fill_node_map(dlm->live_nodes_map, sizeof(dlm->live_nodes_map));

	spin_lock(&dlm->spinlock);
	memcpy(ctxt->live_map, dlm->live_nodes_map, sizeof(ctxt->live_map));

	__dlm_set_joining_node(dlm, dlm->node_num);

	spin_unlock(&dlm->spinlock);

	node = -1;
	while ((node = find_next_bit(ctxt->live_map, O2NM_MAX_NODES,
				     node + 1)) < O2NM_MAX_NODES) {
		if (node == dlm->node_num)
			continue;

		status = dlm_request_join(dlm, node, &response);
		if (status < 0) {
			mlog_errno(status);
			goto bail;
		}

		/* Ok, either we got a response or the node doesn't have a
		 * dlm up. */
		if (response == JOIN_OK)
			set_bit(node, ctxt->yes_resp_map);

		if (dlm_should_restart_join(dlm, ctxt, response)) {
			status = -EAGAIN;
			goto bail;
		}
	}

	mlog(0, "Yay, done querying nodes!\n");

	/* Yay, everyone agree's we can join the domain. My domain is
	 * comprised of all nodes who were put in the
	 * yes_resp_map. Copy that into our domain map and send a join
	 * assert message to clean up everyone elses state. */
	spin_lock(&dlm->spinlock);
	memcpy(dlm->domain_map, ctxt->yes_resp_map,
	       sizeof(ctxt->yes_resp_map));
	set_bit(dlm->node_num, dlm->domain_map);
	spin_unlock(&dlm->spinlock);

	/* Support for global heartbeat and node info was added in 1.1 */
	if (dlm->dlm_locking_proto.pv_major > 1 ||
	    dlm->dlm_locking_proto.pv_minor > 0) {
		status = dlm_send_nodeinfo(dlm, ctxt->yes_resp_map);
		if (status) {
			mlog_errno(status);
			goto bail;
		}
		status = dlm_send_regions(dlm, ctxt->yes_resp_map);
		if (status) {
			mlog_errno(status);
			goto bail;
		}
	}

	dlm_send_join_asserts(dlm, ctxt->yes_resp_map);

	/* Joined state *must* be set before the joining node
	 * information, otherwise the query_join handler may read no
	 * current joiner but a state of NEW and tell joining nodes
	 * we're not in the domain. */
	spin_lock(&dlm_domain_lock);
	dlm->dlm_state = DLM_CTXT_JOINED;
	dlm->num_joins++;
	spin_unlock(&dlm_domain_lock);

bail:
	spin_lock(&dlm->spinlock);
	__dlm_set_joining_node(dlm, DLM_LOCK_RES_OWNER_UNKNOWN);
	if (!status) {
		printk(KERN_NOTICE "o2dlm: Joining domain %s ", dlm->name);
		__dlm_print_nodes(dlm);
	}
	spin_unlock(&dlm->spinlock);

	if (ctxt) {
		/* Do we need to send a cancel message to any nodes? */
		if (status < 0) {
			tmpstat = dlm_send_join_cancels(dlm,
							ctxt->yes_resp_map,
							sizeof(ctxt->yes_resp_map));
			if (tmpstat < 0)
				mlog_errno(tmpstat);
		}
		kfree(ctxt);
	}

	mlog(0, "returning %d\n", status);
	return status;
}

static void dlm_unregister_domain_handlers(struct dlm_ctxt *dlm)
{
	o2hb_unregister_callback(dlm->name, &dlm->dlm_hb_up);
	o2hb_unregister_callback(dlm->name, &dlm->dlm_hb_down);
	o2net_unregister_handler_list(&dlm->dlm_domain_handlers);
}

static int dlm_register_domain_handlers(struct dlm_ctxt *dlm)
{
	int status;

	mlog(0, "registering handlers.\n");

	o2hb_setup_callback(&dlm->dlm_hb_down, O2HB_NODE_DOWN_CB,
			    dlm_hb_node_down_cb, dlm, DLM_HB_NODE_DOWN_PRI);
	o2hb_setup_callback(&dlm->dlm_hb_up, O2HB_NODE_UP_CB,
			    dlm_hb_node_up_cb, dlm, DLM_HB_NODE_UP_PRI);

	status = o2hb_register_callback(dlm->name, &dlm->dlm_hb_down);
	if (status)
		goto bail;

	status = o2hb_register_callback(dlm->name, &dlm->dlm_hb_up);
	if (status)
		goto bail;

	status = o2net_register_handler(DLM_MASTER_REQUEST_MSG, dlm->key,
					sizeof(struct dlm_master_request),
					dlm_master_request_handler,
					dlm, NULL, &dlm->dlm_domain_handlers);
	if (status)
		goto bail;

	status = o2net_register_handler(DLM_ASSERT_MASTER_MSG, dlm->key,
					sizeof(struct dlm_assert_master),
					dlm_assert_master_handler,
					dlm, dlm_assert_master_post_handler,
					&dlm->dlm_domain_handlers);
	if (status)
		goto bail;

	status = o2net_register_handler(DLM_CREATE_LOCK_MSG, dlm->key,
					sizeof(struct dlm_create_lock),
					dlm_create_lock_handler,
					dlm, NULL, &dlm->dlm_domain_handlers);
	if (status)
		goto bail;

	status = o2net_register_handler(DLM_CONVERT_LOCK_MSG, dlm->key,
					DLM_CONVERT_LOCK_MAX_LEN,
					dlm_convert_lock_handler,
					dlm, NULL, &dlm->dlm_domain_handlers);
	if (status)
		goto bail;

	status = o2net_register_handler(DLM_UNLOCK_LOCK_MSG, dlm->key,
					DLM_UNLOCK_LOCK_MAX_LEN,
					dlm_unlock_lock_handler,
					dlm, NULL, &dlm->dlm_domain_handlers);
	if (status)
		goto bail;

	status = o2net_register_handler(DLM_PROXY_AST_MSG, dlm->key,
					DLM_PROXY_AST_MAX_LEN,
					dlm_proxy_ast_handler,
					dlm, NULL, &dlm->dlm_domain_handlers);
	if (status)
		goto bail;

	status = o2net_register_handler(DLM_EXIT_DOMAIN_MSG, dlm->key,
					sizeof(struct dlm_exit_domain),
					dlm_exit_domain_handler,
					dlm, NULL, &dlm->dlm_domain_handlers);
	if (status)
		goto bail;

	status = o2net_register_handler(DLM_DEREF_LOCKRES_MSG, dlm->key,
					sizeof(struct dlm_deref_lockres),
					dlm_deref_lockres_handler,
					dlm, NULL, &dlm->dlm_domain_handlers);
	if (status)
		goto bail;

	status = o2net_register_handler(DLM_MIGRATE_REQUEST_MSG, dlm->key,
					sizeof(struct dlm_migrate_request),
					dlm_migrate_request_handler,
					dlm, NULL, &dlm->dlm_domain_handlers);
	if (status)
		goto bail;

	status = o2net_register_handler(DLM_MIG_LOCKRES_MSG, dlm->key,
					DLM_MIG_LOCKRES_MAX_LEN,
					dlm_mig_lockres_handler,
					dlm, NULL, &dlm->dlm_domain_handlers);
	if (status)
		goto bail;

	status = o2net_register_handler(DLM_MASTER_REQUERY_MSG, dlm->key,
					sizeof(struct dlm_master_requery),
					dlm_master_requery_handler,
					dlm, NULL, &dlm->dlm_domain_handlers);
	if (status)
		goto bail;

	status = o2net_register_handler(DLM_LOCK_REQUEST_MSG, dlm->key,
					sizeof(struct dlm_lock_request),
					dlm_request_all_locks_handler,
					dlm, NULL, &dlm->dlm_domain_handlers);
	if (status)
		goto bail;

	status = o2net_register_handler(DLM_RECO_DATA_DONE_MSG, dlm->key,
					sizeof(struct dlm_reco_data_done),
					dlm_reco_data_done_handler,
					dlm, NULL, &dlm->dlm_domain_handlers);
	if (status)
		goto bail;

	status = o2net_register_handler(DLM_BEGIN_RECO_MSG, dlm->key,
					sizeof(struct dlm_begin_reco),
					dlm_begin_reco_handler,
					dlm, NULL, &dlm->dlm_domain_handlers);
	if (status)
		goto bail;

	status = o2net_register_handler(DLM_FINALIZE_RECO_MSG, dlm->key,
					sizeof(struct dlm_finalize_reco),
					dlm_finalize_reco_handler,
					dlm, NULL, &dlm->dlm_domain_handlers);
	if (status)
		goto bail;

	status = o2net_register_handler(DLM_BEGIN_EXIT_DOMAIN_MSG, dlm->key,
					sizeof(struct dlm_exit_domain),
					dlm_begin_exit_domain_handler,
					dlm, NULL, &dlm->dlm_domain_handlers);
	if (status)
		goto bail;

	status = o2net_register_handler(DLM_DEREF_LOCKRES_DONE, dlm->key,
					sizeof(struct dlm_deref_lockres_done),
					dlm_deref_lockres_done_handler,
					dlm, NULL, &dlm->dlm_domain_handlers);
bail:
	if (status)
		dlm_unregister_domain_handlers(dlm);

	return status;
}

static int dlm_join_domain(struct dlm_ctxt *dlm)
{
	int status;
	unsigned int backoff;
	unsigned int total_backoff = 0;
	char wq_name[O2NM_MAX_NAME_LEN];

	BUG_ON(!dlm);

	mlog(0, "Join domain %s\n", dlm->name);

	status = dlm_register_domain_handlers(dlm);
	if (status) {
		mlog_errno(status);
		goto bail;
	}

	status = dlm_launch_thread(dlm);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

	status = dlm_launch_recovery_thread(dlm);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

	dlm_debug_init(dlm);

	snprintf(wq_name, O2NM_MAX_NAME_LEN, "dlm_wq-%s", dlm->name);
	dlm->dlm_worker = alloc_workqueue(wq_name, WQ_MEM_RECLAIM, 0);
	if (!dlm->dlm_worker) {
		status = -ENOMEM;
		mlog_errno(status);
		goto bail;
	}

	do {
		status = dlm_try_to_join_domain(dlm);

		/* If we're racing another node to the join, then we
		 * need to back off temporarily and let them
		 * complete. */
#define	DLM_JOIN_TIMEOUT_MSECS	90000
		if (status == -EAGAIN) {
			if (signal_pending(current)) {
				status = -ERESTARTSYS;
				goto bail;
			}

			if (total_backoff > DLM_JOIN_TIMEOUT_MSECS) {
				status = -ERESTARTSYS;
				mlog(ML_NOTICE, "Timed out joining dlm domain "
				     "%s after %u msecs\n", dlm->name,
				     total_backoff);
				goto bail;
			}

			/*
			 * <chip> After you!
			 * <dale> No, after you!
			 * <chip> I insist!
			 * <dale> But you first!
			 * ...
			 */
			backoff = (unsigned int)(jiffies & 0x3);
			backoff *= DLM_DOMAIN_BACKOFF_MS;
			total_backoff += backoff;
			mlog(0, "backoff %d\n", backoff);
			msleep(backoff);
		}
	} while (status == -EAGAIN);

	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

	status = 0;
bail:
	wake_up(&dlm_domain_events);

	if (status) {
		dlm_unregister_domain_handlers(dlm);
		dlm_complete_thread(dlm);
		dlm_complete_recovery_thread(dlm);
		dlm_destroy_dlm_worker(dlm);
	}

	return status;
}

static struct dlm_ctxt *dlm_alloc_ctxt(const char *domain,
				u32 key)
{
	int i;
	int ret;
	struct dlm_ctxt *dlm = NULL;

	dlm = kzalloc(sizeof(*dlm), GFP_KERNEL);
	if (!dlm) {
		ret = -ENOMEM;
		mlog_errno(ret);
		goto leave;
	}

	dlm->name = kstrdup(domain, GFP_KERNEL);
	if (dlm->name == NULL) {
		ret = -ENOMEM;
		mlog_errno(ret);
		goto leave;
	}

	dlm->lockres_hash = (struct hlist_head **)dlm_alloc_pagevec(DLM_HASH_PAGES);
	if (!dlm->lockres_hash) {
		ret = -ENOMEM;
		mlog_errno(ret);
		goto leave;
	}

	for (i = 0; i < DLM_HASH_BUCKETS; i++)
		INIT_HLIST_HEAD(dlm_lockres_hash(dlm, i));

	dlm->master_hash = (struct hlist_head **)
				dlm_alloc_pagevec(DLM_HASH_PAGES);
	if (!dlm->master_hash) {
		ret = -ENOMEM;
		mlog_errno(ret);
		goto leave;
	}

	for (i = 0; i < DLM_HASH_BUCKETS; i++)
		INIT_HLIST_HEAD(dlm_master_hash(dlm, i));

	dlm->key = key;
	dlm->node_num = o2nm_this_node();

	dlm_create_debugfs_subroot(dlm);

	spin_lock_init(&dlm->spinlock);
	spin_lock_init(&dlm->master_lock);
	spin_lock_init(&dlm->ast_lock);
	spin_lock_init(&dlm->track_lock);
	INIT_LIST_HEAD(&dlm->list);
	INIT_LIST_HEAD(&dlm->dirty_list);
	INIT_LIST_HEAD(&dlm->reco.resources);
	INIT_LIST_HEAD(&dlm->reco.node_data);
	INIT_LIST_HEAD(&dlm->purge_list);
	INIT_LIST_HEAD(&dlm->dlm_domain_handlers);
	INIT_LIST_HEAD(&dlm->tracking_list);
	dlm->reco.state = 0;

	INIT_LIST_HEAD(&dlm->pending_asts);
	INIT_LIST_HEAD(&dlm->pending_basts);

	mlog(0, "dlm->recovery_map=%p, &(dlm->recovery_map[0])=%p\n",
		  dlm->recovery_map, &(dlm->recovery_map[0]));

	memset(dlm->recovery_map, 0, sizeof(dlm->recovery_map));
	memset(dlm->live_nodes_map, 0, sizeof(dlm->live_nodes_map));
	memset(dlm->domain_map, 0, sizeof(dlm->domain_map));

	dlm->dlm_thread_task = NULL;
	dlm->dlm_reco_thread_task = NULL;
	dlm->dlm_worker = NULL;
	init_waitqueue_head(&dlm->dlm_thread_wq);
	init_waitqueue_head(&dlm->dlm_reco_thread_wq);
	init_waitqueue_head(&dlm->reco.event);
	init_waitqueue_head(&dlm->ast_wq);
	init_waitqueue_head(&dlm->migration_wq);
	INIT_LIST_HEAD(&dlm->mle_hb_events);

	dlm->joining_node = DLM_LOCK_RES_OWNER_UNKNOWN;
	init_waitqueue_head(&dlm->dlm_join_events);

	dlm->migrate_done = 0;

	dlm->reco.new_master = O2NM_INVALID_NODE_NUM;
	dlm->reco.dead_node = O2NM_INVALID_NODE_NUM;

	atomic_set(&dlm->res_tot_count, 0);
	atomic_set(&dlm->res_cur_count, 0);
	for (i = 0; i < DLM_MLE_NUM_TYPES; ++i) {
		atomic_set(&dlm->mle_tot_count[i], 0);
		atomic_set(&dlm->mle_cur_count[i], 0);
	}

	spin_lock_init(&dlm->work_lock);
	INIT_LIST_HEAD(&dlm->work_list);
	INIT_WORK(&dlm->dispatched_work, dlm_dispatch_work);

	kref_init(&dlm->dlm_refs);
	dlm->dlm_state = DLM_CTXT_NEW;

	INIT_LIST_HEAD(&dlm->dlm_eviction_callbacks);

	mlog(0, "context init: refcount %u\n",
		  kref_read(&dlm->dlm_refs));

	ret = 0;
leave:
	if (ret < 0 && dlm) {
		if (dlm->master_hash)
			dlm_free_pagevec((void **)dlm->master_hash,
					DLM_HASH_PAGES);

		if (dlm->lockres_hash)
			dlm_free_pagevec((void **)dlm->lockres_hash,
					DLM_HASH_PAGES);

		kfree(dlm->name);
		kfree(dlm);
		dlm = NULL;
	}
	return dlm;
}

/*
 * Compare a requested locking protocol version against the current one.
 *
 * If the major numbers are different, they are incompatible.
 * If the current minor is greater than the request, they are incompatible.
 * If the current minor is less than or equal to the request, they are
 * compatible, and the requester should run at the current minor version.
 */
static int dlm_protocol_compare(struct dlm_protocol_version *existing,
				struct dlm_protocol_version *request)
{
	if (existing->pv_major != request->pv_major)
		return 1;

	if (existing->pv_minor > request->pv_minor)
		return 1;

	if (existing->pv_minor < request->pv_minor)
		request->pv_minor = existing->pv_minor;

	return 0;
}

/*
 * dlm_register_domain: one-time setup per "domain".
 *
 * The filesystem passes in the requested locking version via proto.
 * If registration was successful, proto will contain the negotiated
 * locking protocol.
 */
struct dlm_ctxt * dlm_register_domain(const char *domain,
			       u32 key,
			       struct dlm_protocol_version *fs_proto)
{
	int ret;
	struct dlm_ctxt *dlm = NULL;
	struct dlm_ctxt *new_ctxt = NULL;

	if (strlen(domain) >= O2NM_MAX_NAME_LEN) {
		ret = -ENAMETOOLONG;
		mlog(ML_ERROR, "domain name length too long\n");
		goto leave;
	}

	mlog(0, "register called for domain \"%s\"\n", domain);

retry:
	dlm = NULL;
	if (signal_pending(current)) {
		ret = -ERESTARTSYS;
		mlog_errno(ret);
		goto leave;
	}

	spin_lock(&dlm_domain_lock);

	dlm = __dlm_lookup_domain(domain);
	if (dlm) {
		if (dlm->dlm_state != DLM_CTXT_JOINED) {
			spin_unlock(&dlm_domain_lock);

			mlog(0, "This ctxt is not joined yet!\n");
			wait_event_interruptible(dlm_domain_events,
						 dlm_wait_on_domain_helper(
							 domain));
			goto retry;
		}

		if (dlm_protocol_compare(&dlm->fs_locking_proto, fs_proto)) {
			spin_unlock(&dlm_domain_lock);
			mlog(ML_ERROR,
			     "Requested locking protocol version is not "
			     "compatible with already registered domain "
			     "\"%s\"\n", domain);
			ret = -EPROTO;
			goto leave;
		}

		__dlm_get(dlm);
		dlm->num_joins++;

		spin_unlock(&dlm_domain_lock);

		ret = 0;
		goto leave;
	}

	/* doesn't exist */
	if (!new_ctxt) {
		spin_unlock(&dlm_domain_lock);

		new_ctxt = dlm_alloc_ctxt(domain, key);
		if (new_ctxt)
			goto retry;

		ret = -ENOMEM;
		mlog_errno(ret);
		goto leave;
	}

	/* a little variable switch-a-roo here... */
	dlm = new_ctxt;
	new_ctxt = NULL;

	/* add the new domain */
	list_add_tail(&dlm->list, &dlm_domains);
	spin_unlock(&dlm_domain_lock);

	/*
	 * Pass the locking protocol version into the join.  If the join
	 * succeeds, it will have the negotiated protocol set.
	 */
	dlm->dlm_locking_proto = dlm_protocol;
	dlm->fs_locking_proto = *fs_proto;

	ret = dlm_join_domain(dlm);
	if (ret) {
		mlog_errno(ret);
		dlm_put(dlm);
		goto leave;
	}

	/* Tell the caller what locking protocol we negotiated */
	*fs_proto = dlm->fs_locking_proto;

	ret = 0;
leave:
	if (new_ctxt)
		dlm_free_ctxt_mem(new_ctxt);

	if (ret < 0)
		dlm = ERR_PTR(ret);

	return dlm;
}
EXPORT_SYMBOL_GPL(dlm_register_domain);

static LIST_HEAD(dlm_join_handlers);

static void dlm_unregister_net_handlers(void)
{
	o2net_unregister_handler_list(&dlm_join_handlers);
}

static int dlm_register_net_handlers(void)
{
	int status = 0;

	status = o2net_register_handler(DLM_QUERY_JOIN_MSG, DLM_MOD_KEY,
					sizeof(struct dlm_query_join_request),
					dlm_query_join_handler,
					NULL, NULL, &dlm_join_handlers);
	if (status)
		goto bail;

	status = o2net_register_handler(DLM_ASSERT_JOINED_MSG, DLM_MOD_KEY,
					sizeof(struct dlm_assert_joined),
					dlm_assert_joined_handler,
					NULL, NULL, &dlm_join_handlers);
	if (status)
		goto bail;

	status = o2net_register_handler(DLM_CANCEL_JOIN_MSG, DLM_MOD_KEY,
					sizeof(struct dlm_cancel_join),
					dlm_cancel_join_handler,
					NULL, NULL, &dlm_join_handlers);
	if (status)
		goto bail;

	status = o2net_register_handler(DLM_QUERY_REGION, DLM_MOD_KEY,
					sizeof(struct dlm_query_region),
					dlm_query_region_handler,
					NULL, NULL, &dlm_join_handlers);

	if (status)
		goto bail;

	status = o2net_register_handler(DLM_QUERY_NODEINFO, DLM_MOD_KEY,
					sizeof(struct dlm_query_nodeinfo),
					dlm_query_nodeinfo_handler,
					NULL, NULL, &dlm_join_handlers);
bail:
	if (status < 0)
		dlm_unregister_net_handlers();

	return status;
}

/* Domain eviction callback handling.
 *
 * The file system requires notification of node death *before* the
 * dlm completes it's recovery work, otherwise it may be able to
 * acquire locks on resources requiring recovery. Since the dlm can
 * evict a node from it's domain *before* heartbeat fires, a similar
 * mechanism is required. */

/* Eviction is not expected to happen often, so a per-domain lock is
 * not necessary. Eviction callbacks are allowed to sleep for short
 * periods of time. */
static DECLARE_RWSEM(dlm_callback_sem);

void dlm_fire_domain_eviction_callbacks(struct dlm_ctxt *dlm,
					int node_num)
{
	struct dlm_eviction_cb *cb;

	down_read(&dlm_callback_sem);
	list_for_each_entry(cb, &dlm->dlm_eviction_callbacks, ec_item) {
		cb->ec_func(node_num, cb->ec_data);
	}
	up_read(&dlm_callback_sem);
}

void dlm_setup_eviction_cb(struct dlm_eviction_cb *cb,
			   dlm_eviction_func *f,
			   void *data)
{
	INIT_LIST_HEAD(&cb->ec_item);
	cb->ec_func = f;
	cb->ec_data = data;
}
EXPORT_SYMBOL_GPL(dlm_setup_eviction_cb);

void dlm_register_eviction_cb(struct dlm_ctxt *dlm,
			      struct dlm_eviction_cb *cb)
{
	down_write(&dlm_callback_sem);
	list_add_tail(&cb->ec_item, &dlm->dlm_eviction_callbacks);
	up_write(&dlm_callback_sem);
}
EXPORT_SYMBOL_GPL(dlm_register_eviction_cb);

void dlm_unregister_eviction_cb(struct dlm_eviction_cb *cb)
{
	down_write(&dlm_callback_sem);
	list_del_init(&cb->ec_item);
	up_write(&dlm_callback_sem);
}
EXPORT_SYMBOL_GPL(dlm_unregister_eviction_cb);

static int __init dlm_init(void)
{
	int status;

	status = dlm_init_mle_cache();
	if (status) {
		mlog(ML_ERROR, "Could not create o2dlm_mle slabcache\n");
		goto error;
	}

	status = dlm_init_master_caches();
	if (status) {
		mlog(ML_ERROR, "Could not create o2dlm_lockres and "
		     "o2dlm_lockname slabcaches\n");
		goto error;
	}

	status = dlm_init_lock_cache();
	if (status) {
		mlog(ML_ERROR, "Count not create o2dlm_lock slabcache\n");
		goto error;
	}

	status = dlm_register_net_handlers();
	if (status) {
		mlog(ML_ERROR, "Unable to register network handlers\n");
		goto error;
	}

	dlm_create_debugfs_root();

	return 0;
error:
	dlm_unregister_net_handlers();
	dlm_destroy_lock_cache();
	dlm_destroy_master_caches();
	dlm_destroy_mle_cache();
	return -1;
}

static void __exit dlm_exit (void)
{
	dlm_destroy_debugfs_root();
	dlm_unregister_net_handlers();
	dlm_destroy_lock_cache();
	dlm_destroy_master_caches();
	dlm_destroy_mle_cache();
}

MODULE_AUTHOR("Oracle");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("OCFS2 Distributed Lock Management");

module_init(dlm_init);
module_exit(dlm_exit);
