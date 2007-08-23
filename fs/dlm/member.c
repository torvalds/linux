/******************************************************************************
*******************************************************************************
**
**  Copyright (C) 2005-2007 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
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

/*
 * Following called by dlm_recoverd thread
 */

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

static int dlm_add_member(struct dlm_ls *ls, int nodeid)
{
	struct dlm_member *memb;
	int w;

	memb = kzalloc(sizeof(struct dlm_member), GFP_KERNEL);
	if (!memb)
		return -ENOMEM;

	w = dlm_node_weight(ls->ls_name, nodeid);
	if (w < 0) {
		kfree(memb);
		return w;
	}

	memb->nodeid = nodeid;
	memb->weight = w;
	add_ordered_member(ls, memb);
	ls->ls_num_nodes++;
	return 0;
}

static void dlm_remove_member(struct dlm_ls *ls, struct dlm_member *memb)
{
	list_move(&memb->list, &ls->ls_nodes_gone);
	ls->ls_num_nodes--;
}

static int dlm_is_member(struct dlm_ls *ls, int nodeid)
{
	struct dlm_member *memb;

	list_for_each_entry(memb, &ls->ls_nodes, list) {
		if (memb->nodeid == nodeid)
			return 1;
	}
	return 0;
}

int dlm_is_removed(struct dlm_ls *ls, int nodeid)
{
	struct dlm_member *memb;

	list_for_each_entry(memb, &ls->ls_nodes_gone, list) {
		if (memb->nodeid == nodeid)
			return 1;
	}
	return 0;
}

static void clear_memb_list(struct list_head *head)
{
	struct dlm_member *memb;

	while (!list_empty(head)) {
		memb = list_entry(head->next, struct dlm_member, list);
		list_del(&memb->list);
		kfree(memb);
	}
}

void dlm_clear_members(struct dlm_ls *ls)
{
	clear_memb_list(&ls->ls_nodes);
	ls->ls_num_nodes = 0;
}

void dlm_clear_members_gone(struct dlm_ls *ls)
{
	clear_memb_list(&ls->ls_nodes_gone);
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

	array = kmalloc(sizeof(int) * total, GFP_KERNEL);
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

static int ping_members(struct dlm_ls *ls)
{
	struct dlm_member *memb;
	int error = 0;

	list_for_each_entry(memb, &ls->ls_nodes, list) {
		error = dlm_recovery_stopped(ls);
		if (error)
			break;
		error = dlm_rcom_status(ls, memb->nodeid);
		if (error)
			break;
	}
	if (error)
		log_debug(ls, "ping_members aborted %d last nodeid %d",
			  error, ls->ls_recover_nodeid);
	return error;
}

int dlm_recover_members(struct dlm_ls *ls, struct dlm_recover *rv, int *neg_out)
{
	struct dlm_member *memb, *safe;
	int i, error, found, pos = 0, neg = 0, low = -1;

	/* previously removed members that we've not finished removing need to
	   count as a negative change so the "neg" recovery steps will happen */

	list_for_each_entry(memb, &ls->ls_nodes_gone, list) {
		log_debug(ls, "prev removed member %d", memb->nodeid);
		neg++;
	}

	/* move departed members from ls_nodes to ls_nodes_gone */

	list_for_each_entry_safe(memb, safe, &ls->ls_nodes, list) {
		found = 0;
		for (i = 0; i < rv->node_count; i++) {
			if (memb->nodeid == rv->nodeids[i]) {
				found = 1;
				break;
			}
		}

		if (!found) {
			neg++;
			dlm_remove_member(ls, memb);
			log_debug(ls, "remove member %d", memb->nodeid);
		}
	}

	/* add new members to ls_nodes */

	for (i = 0; i < rv->node_count; i++) {
		if (dlm_is_member(ls, rv->nodeids[i]))
			continue;
		dlm_add_member(ls, rv->nodeids[i]);
		pos++;
		log_debug(ls, "add member %d", rv->nodeids[i]);
	}

	list_for_each_entry(memb, &ls->ls_nodes, list) {
		if (low == -1 || memb->nodeid < low)
			low = memb->nodeid;
	}
	ls->ls_low_nodeid = low;

	make_member_array(ls);
	dlm_set_recover_status(ls, DLM_RS_NODES);
	*neg_out = neg;

	error = ping_members(ls);
	if (!error || error == -EPROTO) {
		/* new_lockspace() may be waiting to know if the config
		   is good or bad */
		ls->ls_members_result = error;
		complete(&ls->ls_members_done);
	}
	if (error)
		goto out;

	error = dlm_recover_members_wait(ls);
 out:
	log_debug(ls, "total members %d error %d", ls->ls_num_nodes, error);
	return error;
}

/*
 * Following called from lockspace.c
 */

int dlm_ls_stop(struct dlm_ls *ls)
{
	int new;

	/*
	 * A stop cancels any recovery that's in progress (see RECOVERY_STOP,
	 * dlm_recovery_stopped()) and prevents any new locks from being
	 * processed (see RUNNING, dlm_locking_stopped()).
	 */

	spin_lock(&ls->ls_recover_lock);
	set_bit(LSFL_RECOVERY_STOP, &ls->ls_flags);
	new = test_and_clear_bit(LSFL_RUNNING, &ls->ls_flags);
	ls->ls_recover_seq++;
	spin_unlock(&ls->ls_recover_lock);

	/*
	 * This in_recovery lock does two things:
	 *
	 * 1) Keeps this function from returning until all threads are out
	 *    of locking routines and locking is truely stopped.
	 * 2) Keeps any new requests from being processed until it's unlocked
	 *    when recovery is complete.
	 */

	if (new)
		down_write(&ls->ls_in_recovery);

	/*
	 * The recoverd suspend/resume makes sure that dlm_recoverd (if
	 * running) has noticed the clearing of RUNNING above and quit
	 * processing the previous recovery.  This will be true for all nodes
	 * before any nodes start the new recovery.
	 */

	dlm_recoverd_suspend(ls);
	ls->ls_recover_status = 0;
	dlm_recoverd_resume(ls);

	if (!ls->ls_recover_begin)
		ls->ls_recover_begin = jiffies;
	return 0;
}

int dlm_ls_start(struct dlm_ls *ls)
{
	struct dlm_recover *rv = NULL, *rv_old;
	int *ids = NULL;
	int error, count;

	rv = kzalloc(sizeof(struct dlm_recover), GFP_KERNEL);
	if (!rv)
		return -ENOMEM;

	error = count = dlm_nodeid_list(ls->ls_name, &ids);
	if (error <= 0)
		goto fail;

	spin_lock(&ls->ls_recover_lock);

	/* the lockspace needs to be stopped before it can be started */

	if (!dlm_locking_stopped(ls)) {
		spin_unlock(&ls->ls_recover_lock);
		log_error(ls, "start ignored: lockspace running");
		error = -EINVAL;
		goto fail;
	}

	rv->nodeids = ids;
	rv->node_count = count;
	rv->seq = ++ls->ls_recover_seq;
	rv_old = ls->ls_recover_args;
	ls->ls_recover_args = rv;
	spin_unlock(&ls->ls_recover_lock);

	if (rv_old) {
		kfree(rv_old->nodeids);
		kfree(rv_old);
	}

	dlm_recoverd_kick(ls);
	return 0;

 fail:
	kfree(rv);
	kfree(ids);
	return error;
}

