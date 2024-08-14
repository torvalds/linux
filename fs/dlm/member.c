// SPDX-License-Identifier: GPL-2.0-only
/******************************************************************************
*******************************************************************************
**
**  Copyright (C) 2005-2011 Red Hat, Inc.  All rights reserved.
**
**
*******************************************************************************
******************************************************************************/

#include "dlm_internal.h"
#include "lockspace.h"
#include "member.h"
#include "recoverd.h"
#include "recover.h"
#include "rcom.h"
#include "config.h"
#include "midcomms.h"
#include "lowcomms.h"

int dlm_slots_version(const struct dlm_header *h)
{
	if ((le32_to_cpu(h->h_version) & 0x0000FFFF) < DLM_HEADER_SLOTS)
		return 0;
	return 1;
}

void dlm_slot_save(struct dlm_ls *ls, struct dlm_rcom *rc,
		   struct dlm_member *memb)
{
	struct rcom_config *rf = (struct rcom_config *)rc->rc_buf;

	if (!dlm_slots_version(&rc->rc_header))
		return;

	memb->slot = le16_to_cpu(rf->rf_our_slot);
	memb->generation = le32_to_cpu(rf->rf_generation);
}

void dlm_slots_copy_out(struct dlm_ls *ls, struct dlm_rcom *rc)
{
	struct dlm_slot *slot;
	struct rcom_slot *ro;
	int i;

	ro = (struct rcom_slot *)(rc->rc_buf + sizeof(struct rcom_config));

	/* ls_slots array is sparse, but not rcom_slots */

	for (i = 0; i < ls->ls_slots_size; i++) {
		slot = &ls->ls_slots[i];
		if (!slot->nodeid)
			continue;
		ro->ro_nodeid = cpu_to_le32(slot->nodeid);
		ro->ro_slot = cpu_to_le16(slot->slot);
		ro++;
	}
}

#define SLOT_DEBUG_LINE 128

static void log_slots(struct dlm_ls *ls, uint32_t gen, int num_slots,
		      struct rcom_slot *ro0, struct dlm_slot *array,
		      int array_size)
{
	char line[SLOT_DEBUG_LINE];
	int len = SLOT_DEBUG_LINE - 1;
	int pos = 0;
	int ret, i;

	memset(line, 0, sizeof(line));

	if (array) {
		for (i = 0; i < array_size; i++) {
			if (!array[i].nodeid)
				continue;

			ret = snprintf(line + pos, len - pos, " %d:%d",
				       array[i].slot, array[i].nodeid);
			if (ret >= len - pos)
				break;
			pos += ret;
		}
	} else if (ro0) {
		for (i = 0; i < num_slots; i++) {
			ret = snprintf(line + pos, len - pos, " %d:%d",
				       ro0[i].ro_slot, ro0[i].ro_nodeid);
			if (ret >= len - pos)
				break;
			pos += ret;
		}
	}

	log_rinfo(ls, "generation %u slots %d%s", gen, num_slots, line);
}

int dlm_slots_copy_in(struct dlm_ls *ls)
{
	struct dlm_member *memb;
	struct dlm_rcom *rc = ls->ls_recover_buf;
	struct rcom_config *rf = (struct rcom_config *)rc->rc_buf;
	struct rcom_slot *ro0, *ro;
	int our_nodeid = dlm_our_nodeid();
	int i, num_slots;
	uint32_t gen;

	if (!dlm_slots_version(&rc->rc_header))
		return -1;

	gen = le32_to_cpu(rf->rf_generation);
	if (gen <= ls->ls_generation) {
		log_error(ls, "dlm_slots_copy_in gen %u old %u",
			  gen, ls->ls_generation);
	}
	ls->ls_generation = gen;

	num_slots = le16_to_cpu(rf->rf_num_slots);
	if (!num_slots)
		return -1;

	ro0 = (struct rcom_slot *)(rc->rc_buf + sizeof(struct rcom_config));

	log_slots(ls, gen, num_slots, ro0, NULL, 0);

	list_for_each_entry(memb, &ls->ls_nodes, list) {
		for (i = 0, ro = ro0; i < num_slots; i++, ro++) {
			if (le32_to_cpu(ro->ro_nodeid) != memb->nodeid)
				continue;
			memb->slot = le16_to_cpu(ro->ro_slot);
			memb->slot_prev = memb->slot;
			break;
		}

		if (memb->nodeid == our_nodeid) {
			if (ls->ls_slot && ls->ls_slot != memb->slot) {
				log_error(ls, "dlm_slots_copy_in our slot "
					  "changed %d %d", ls->ls_slot,
					  memb->slot);
				return -1;
			}

			if (!ls->ls_slot)
				ls->ls_slot = memb->slot;
		}

		if (!memb->slot) {
			log_error(ls, "dlm_slots_copy_in nodeid %d no slot",
				   memb->nodeid);
			return -1;
		}
	}

	return 0;
}

/* for any nodes that do not support slots, we will not have set memb->slot
   in wait_status_all(), so memb->slot will remain -1, and we will not
   assign slots or set ls_num_slots here */

int dlm_slots_assign(struct dlm_ls *ls, int *num_slots, int *slots_size,
		     struct dlm_slot **slots_out, uint32_t *gen_out)
{
	struct dlm_member *memb;
	struct dlm_slot *array;
	int our_nodeid = dlm_our_nodeid();
	int array_size, max_slots, i;
	int need = 0;
	int max = 0;
	int num = 0;
	uint32_t gen = 0;

	/* our own memb struct will have slot -1 gen 0 */

	list_for_each_entry(memb, &ls->ls_nodes, list) {
		if (memb->nodeid == our_nodeid) {
			memb->slot = ls->ls_slot;
			memb->generation = ls->ls_generation;
			break;
		}
	}

	list_for_each_entry(memb, &ls->ls_nodes, list) {
		if (memb->generation > gen)
			gen = memb->generation;

		/* node doesn't support slots */

		if (memb->slot == -1)
			return -1;

		/* node needs a slot assigned */

		if (!memb->slot)
			need++;

		/* node has a slot assigned */

		num++;

		if (!max || max < memb->slot)
			max = memb->slot;

		/* sanity check, once slot is assigned it shouldn't change */

		if (memb->slot_prev && memb->slot && memb->slot_prev != memb->slot) {
			log_error(ls, "nodeid %d slot changed %d %d",
				  memb->nodeid, memb->slot_prev, memb->slot);
			return -1;
		}
		memb->slot_prev = memb->slot;
	}

	array_size = max + need;
	array = kcalloc(array_size, sizeof(*array), GFP_NOFS);
	if (!array)
		return -ENOMEM;

	num = 0;

	/* fill in slots (offsets) that are used */

	list_for_each_entry(memb, &ls->ls_nodes, list) {
		if (!memb->slot)
			continue;

		if (memb->slot > array_size) {
			log_error(ls, "invalid slot number %d", memb->slot);
			kfree(array);
			return -1;
		}

		array[memb->slot - 1].nodeid = memb->nodeid;
		array[memb->slot - 1].slot = memb->slot;
		num++;
	}

	/* assign new slots from unused offsets */

	list_for_each_entry(memb, &ls->ls_nodes, list) {
		if (memb->slot)
			continue;

		for (i = 0; i < array_size; i++) {
			if (array[i].nodeid)
				continue;

			memb->slot = i + 1;
			memb->slot_prev = memb->slot;
			array[i].nodeid = memb->nodeid;
			array[i].slot = memb->slot;
			num++;

			if (!ls->ls_slot && memb->nodeid == our_nodeid)
				ls->ls_slot = memb->slot;
			break;
		}

		if (!memb->slot) {
			log_error(ls, "no free slot found");
			kfree(array);
			return -1;
		}
	}

	gen++;

	log_slots(ls, gen, num, NULL, array, array_size);

	max_slots = (DLM_MAX_APP_BUFSIZE - sizeof(struct dlm_rcom) -
		     sizeof(struct rcom_config)) / sizeof(struct rcom_slot);

	if (num > max_slots) {
		log_error(ls, "num_slots %d exceeds max_slots %d",
			  num, max_slots);
		kfree(array);
		return -1;
	}

	*gen_out = gen;
	*slots_out = array;
	*slots_size = array_size;
	*num_slots = num;
	return 0;
}

static void add_ordered_member(struct dlm_ls *ls, struct dlm_member *new)
{
	struct dlm_member *memb = NULL;
	struct list_head *tmp;
	struct list_head *newlist = &new->list;
	struct list_head *head = &ls->ls_nodes;

	list_for_each(tmp, head) {
		memb = list_entry(tmp, struct dlm_member, list);
		if (new->nodeid < memb->nodeid)
			break;
	}

	if (!memb)
		list_add_tail(newlist, head);
	else {
		/* FIXME: can use list macro here */
		newlist->prev = tmp->prev;
		newlist->next = tmp;
		tmp->prev->next = newlist;
		tmp->prev = newlist;
	}
}

static int add_remote_member(int nodeid)
{
	int error;

	if (nodeid == dlm_our_nodeid())
		return 0;

	error = dlm_lowcomms_connect_node(nodeid);
	if (error < 0)
		return error;

	dlm_midcomms_add_member(nodeid);
	return 0;
}

static int dlm_add_member(struct dlm_ls *ls, struct dlm_config_node *node)
{
	struct dlm_member *memb;
	int error;

	memb = kzalloc(sizeof(*memb), GFP_NOFS);
	if (!memb)
		return -ENOMEM;

	memb->nodeid = node->nodeid;
	memb->weight = node->weight;
	memb->comm_seq = node->comm_seq;

	error = add_remote_member(node->nodeid);
	if (error < 0) {
		kfree(memb);
		return error;
	}

	add_ordered_member(ls, memb);
	ls->ls_num_nodes++;
	return 0;
}

static struct dlm_member *find_memb(struct list_head *head, int nodeid)
{
	struct dlm_member *memb;

	list_for_each_entry(memb, head, list) {
		if (memb->nodeid == nodeid)
			return memb;
	}
	return NULL;
}

int dlm_is_member(struct dlm_ls *ls, int nodeid)
{
	if (find_memb(&ls->ls_nodes, nodeid))
		return 1;
	return 0;
}

int dlm_is_removed(struct dlm_ls *ls, int nodeid)
{
	if (find_memb(&ls->ls_nodes_gone, nodeid))
		return 1;
	return 0;
}

static void clear_memb_list(struct list_head *head,
			    void (*after_del)(int nodeid))
{
	struct dlm_member *memb;

	while (!list_empty(head)) {
		memb = list_entry(head->next, struct dlm_member, list);
		list_del(&memb->list);
		if (after_del)
			after_del(memb->nodeid);
		kfree(memb);
	}
}

static void remove_remote_member(int nodeid)
{
	if (nodeid == dlm_our_nodeid())
		return;

	dlm_midcomms_remove_member(nodeid);
}

void dlm_clear_members(struct dlm_ls *ls)
{
	clear_memb_list(&ls->ls_nodes, remove_remote_member);
	ls->ls_num_nodes = 0;
}

void dlm_clear_members_gone(struct dlm_ls *ls)
{
	clear_memb_list(&ls->ls_nodes_gone, NULL);
}

static void make_member_array(struct dlm_ls *ls)
{
	struct dlm_member *memb;
	int i, w, x = 0, total = 0, all_zero = 0, *array;

	kfree(ls->ls_node_array);
	ls->ls_node_array = NULL;

	list_for_each_entry(memb, &ls->ls_nodes, list) {
		if (memb->weight)
			total += memb->weight;
	}

	/* all nodes revert to weight of 1 if all have weight 0 */

	if (!total) {
		total = ls->ls_num_nodes;
		all_zero = 1;
	}

	ls->ls_total_weight = total;
	array = kmalloc_array(total, sizeof(*array), GFP_NOFS);
	if (!array)
		return;

	list_for_each_entry(memb, &ls->ls_nodes, list) {
		if (!all_zero && !memb->weight)
			continue;

		if (all_zero)
			w = 1;
		else
			w = memb->weight;

		DLM_ASSERT(x < total, printk("total %d x %d\n", total, x););

		for (i = 0; i < w; i++)
			array[x++] = memb->nodeid;
	}

	ls->ls_node_array = array;
}

/* send a status request to all members just to establish comms connections */

static int ping_members(struct dlm_ls *ls, uint64_t seq)
{
	struct dlm_member *memb;
	int error = 0;

	list_for_each_entry(memb, &ls->ls_nodes, list) {
		if (dlm_recovery_stopped(ls)) {
			error = -EINTR;
			break;
		}
		error = dlm_rcom_status(ls, memb->nodeid, 0, seq);
		if (error)
			break;
	}
	if (error)
		log_rinfo(ls, "ping_members aborted %d last nodeid %d",
			  error, ls->ls_recover_nodeid);
	return error;
}

static void dlm_lsop_recover_prep(struct dlm_ls *ls)
{
	if (!ls->ls_ops || !ls->ls_ops->recover_prep)
		return;
	ls->ls_ops->recover_prep(ls->ls_ops_arg);
}

static void dlm_lsop_recover_slot(struct dlm_ls *ls, struct dlm_member *memb)
{
	struct dlm_slot slot;
	uint32_t seq;
	int error;

	if (!ls->ls_ops || !ls->ls_ops->recover_slot)
		return;

	/* if there is no comms connection with this node
	   or the present comms connection is newer
	   than the one when this member was added, then
	   we consider the node to have failed (versus
	   being removed due to dlm_release_lockspace) */

	error = dlm_comm_seq(memb->nodeid, &seq);

	if (!error && seq == memb->comm_seq)
		return;

	slot.nodeid = memb->nodeid;
	slot.slot = memb->slot;

	ls->ls_ops->recover_slot(ls->ls_ops_arg, &slot);
}

void dlm_lsop_recover_done(struct dlm_ls *ls)
{
	struct dlm_member *memb;
	struct dlm_slot *slots;
	int i, num;

	if (!ls->ls_ops || !ls->ls_ops->recover_done)
		return;

	num = ls->ls_num_nodes;
	slots = kcalloc(num, sizeof(*slots), GFP_KERNEL);
	if (!slots)
		return;

	i = 0;
	list_for_each_entry(memb, &ls->ls_nodes, list) {
		if (i == num) {
			log_error(ls, "dlm_lsop_recover_done bad num %d", num);
			goto out;
		}
		slots[i].nodeid = memb->nodeid;
		slots[i].slot = memb->slot;
		i++;
	}

	ls->ls_ops->recover_done(ls->ls_ops_arg, slots, num,
				 ls->ls_slot, ls->ls_generation);
 out:
	kfree(slots);
}

static struct dlm_config_node *find_config_node(struct dlm_recover *rv,
						int nodeid)
{
	int i;

	for (i = 0; i < rv->nodes_count; i++) {
		if (rv->nodes[i].nodeid == nodeid)
			return &rv->nodes[i];
	}
	return NULL;
}

int dlm_recover_members(struct dlm_ls *ls, struct dlm_recover *rv, int *neg_out)
{
	struct dlm_member *memb, *safe;
	struct dlm_config_node *node;
	int i, error, neg = 0, low = -1;

	/* previously removed members that we've not finished removing need to
	 * count as a negative change so the "neg" recovery steps will happen
	 *
	 * This functionality must report all member changes to lsops or
	 * midcomms layer and must never return before.
	 */

	list_for_each_entry(memb, &ls->ls_nodes_gone, list) {
		log_rinfo(ls, "prev removed member %d", memb->nodeid);
		neg++;
	}

	/* move departed members from ls_nodes to ls_nodes_gone */

	list_for_each_entry_safe(memb, safe, &ls->ls_nodes, list) {
		node = find_config_node(rv, memb->nodeid);
		if (node && !node->new)
			continue;

		if (!node) {
			log_rinfo(ls, "remove member %d", memb->nodeid);
		} else {
			/* removed and re-added */
			log_rinfo(ls, "remove member %d comm_seq %u %u",
				  memb->nodeid, memb->comm_seq, node->comm_seq);
		}

		neg++;
		list_move(&memb->list, &ls->ls_nodes_gone);
		remove_remote_member(memb->nodeid);
		ls->ls_num_nodes--;
		dlm_lsop_recover_slot(ls, memb);
	}

	/* add new members to ls_nodes */

	for (i = 0; i < rv->nodes_count; i++) {
		node = &rv->nodes[i];
		if (dlm_is_member(ls, node->nodeid))
			continue;
		error = dlm_add_member(ls, node);
		if (error)
			return error;

		log_rinfo(ls, "add member %d", node->nodeid);
	}

	list_for_each_entry(memb, &ls->ls_nodes, list) {
		if (low == -1 || memb->nodeid < low)
			low = memb->nodeid;
	}
	ls->ls_low_nodeid = low;

	make_member_array(ls);
	*neg_out = neg;

	error = ping_members(ls, rv->seq);
	log_rinfo(ls, "dlm_recover_members %d nodes", ls->ls_num_nodes);
	return error;
}

/* Userspace guarantees that dlm_ls_stop() has completed on all nodes before
   dlm_ls_start() is called on any of them to start the new recovery. */

int dlm_ls_stop(struct dlm_ls *ls)
{
	int new;

	/*
	 * Prevent dlm_recv from being in the middle of something when we do
	 * the stop.  This includes ensuring dlm_recv isn't processing a
	 * recovery message (rcom), while dlm_recoverd is aborting and
	 * resetting things from an in-progress recovery.  i.e. we want
	 * dlm_recoverd to abort its recovery without worrying about dlm_recv
	 * processing an rcom at the same time.  Stopping dlm_recv also makes
	 * it easy for dlm_receive_message() to check locking stopped and add a
	 * message to the requestqueue without races.
	 */

	write_lock_bh(&ls->ls_recv_active);

	/*
	 * Abort any recovery that's in progress (see RECOVER_STOP,
	 * dlm_recovery_stopped()) and tell any other threads running in the
	 * dlm to quit any processing (see RUNNING, dlm_locking_stopped()).
	 */

	spin_lock_bh(&ls->ls_recover_lock);
	set_bit(LSFL_RECOVER_STOP, &ls->ls_flags);
	new = test_and_clear_bit(LSFL_RUNNING, &ls->ls_flags);
	if (new)
		timer_delete_sync(&ls->ls_scan_timer);
	ls->ls_recover_seq++;

	/* activate requestqueue and stop processing */
	write_lock_bh(&ls->ls_requestqueue_lock);
	set_bit(LSFL_RECV_MSG_BLOCKED, &ls->ls_flags);
	write_unlock_bh(&ls->ls_requestqueue_lock);
	spin_unlock_bh(&ls->ls_recover_lock);

	/*
	 * Let dlm_recv run again, now any normal messages will be saved on the
	 * requestqueue for later.
	 */

	write_unlock_bh(&ls->ls_recv_active);

	/*
	 * This in_recovery lock does two things:
	 * 1) Keeps this function from returning until all threads are out
	 *    of locking routines and locking is truly stopped.
	 * 2) Keeps any new requests from being processed until it's unlocked
	 *    when recovery is complete.
	 */

	if (new) {
		set_bit(LSFL_RECOVER_DOWN, &ls->ls_flags);
		wake_up_process(ls->ls_recoverd_task);
		wait_event(ls->ls_recover_lock_wait,
			   test_bit(LSFL_RECOVER_LOCK, &ls->ls_flags));
	}

	/*
	 * The recoverd suspend/resume makes sure that dlm_recoverd (if
	 * running) has noticed RECOVER_STOP above and quit processing the
	 * previous recovery.
	 */

	dlm_recoverd_suspend(ls);

	spin_lock_bh(&ls->ls_recover_lock);
	kfree(ls->ls_slots);
	ls->ls_slots = NULL;
	ls->ls_num_slots = 0;
	ls->ls_slots_size = 0;
	ls->ls_recover_status = 0;
	spin_unlock_bh(&ls->ls_recover_lock);

	dlm_recoverd_resume(ls);

	if (!ls->ls_recover_begin)
		ls->ls_recover_begin = jiffies;

	/* call recover_prep ops only once and not multiple times
	 * for each possible dlm_ls_stop() when recovery is already
	 * stopped.
	 *
	 * If we successful was able to clear LSFL_RUNNING bit and
	 * it was set we know it is the first dlm_ls_stop() call.
	 */
	if (new)
		dlm_lsop_recover_prep(ls);

	return 0;
}

int dlm_ls_start(struct dlm_ls *ls)
{
	struct dlm_recover *rv, *rv_old;
	struct dlm_config_node *nodes = NULL;
	int error, count;

	rv = kzalloc(sizeof(*rv), GFP_NOFS);
	if (!rv)
		return -ENOMEM;

	error = dlm_config_nodes(ls->ls_name, &nodes, &count);
	if (error < 0)
		goto fail_rv;

	spin_lock_bh(&ls->ls_recover_lock);

	/* the lockspace needs to be stopped before it can be started */

	if (!dlm_locking_stopped(ls)) {
		spin_unlock_bh(&ls->ls_recover_lock);
		log_error(ls, "start ignored: lockspace running");
		error = -EINVAL;
		goto fail;
	}

	rv->nodes = nodes;
	rv->nodes_count = count;
	rv->seq = ++ls->ls_recover_seq;
	rv_old = ls->ls_recover_args;
	ls->ls_recover_args = rv;
	spin_unlock_bh(&ls->ls_recover_lock);

	if (rv_old) {
		log_error(ls, "unused recovery %llx %d",
			  (unsigned long long)rv_old->seq, rv_old->nodes_count);
		kfree(rv_old->nodes);
		kfree(rv_old);
	}

	set_bit(LSFL_RECOVER_WORK, &ls->ls_flags);
	wake_up_process(ls->ls_recoverd_task);
	return 0;

 fail:
	kfree(nodes);
 fail_rv:
	kfree(rv);
	return error;
}

