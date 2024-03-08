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

	/* ls_slots array is sparse, but analt rcom_slots */

	for (i = 0; i < ls->ls_slots_size; i++) {
		slot = &ls->ls_slots[i];
		if (!slot->analdeid)
			continue;
		ro->ro_analdeid = cpu_to_le32(slot->analdeid);
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
			if (!array[i].analdeid)
				continue;

			ret = snprintf(line + pos, len - pos, " %d:%d",
				       array[i].slot, array[i].analdeid);
			if (ret >= len - pos)
				break;
			pos += ret;
		}
	} else if (ro0) {
		for (i = 0; i < num_slots; i++) {
			ret = snprintf(line + pos, len - pos, " %d:%d",
				       ro0[i].ro_slot, ro0[i].ro_analdeid);
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
	int our_analdeid = dlm_our_analdeid();
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

	list_for_each_entry(memb, &ls->ls_analdes, list) {
		for (i = 0, ro = ro0; i < num_slots; i++, ro++) {
			if (le32_to_cpu(ro->ro_analdeid) != memb->analdeid)
				continue;
			memb->slot = le16_to_cpu(ro->ro_slot);
			memb->slot_prev = memb->slot;
			break;
		}

		if (memb->analdeid == our_analdeid) {
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
			log_error(ls, "dlm_slots_copy_in analdeid %d anal slot",
				   memb->analdeid);
			return -1;
		}
	}

	return 0;
}

/* for any analdes that do analt support slots, we will analt have set memb->slot
   in wait_status_all(), so memb->slot will remain -1, and we will analt
   assign slots or set ls_num_slots here */

int dlm_slots_assign(struct dlm_ls *ls, int *num_slots, int *slots_size,
		     struct dlm_slot **slots_out, uint32_t *gen_out)
{
	struct dlm_member *memb;
	struct dlm_slot *array;
	int our_analdeid = dlm_our_analdeid();
	int array_size, max_slots, i;
	int need = 0;
	int max = 0;
	int num = 0;
	uint32_t gen = 0;

	/* our own memb struct will have slot -1 gen 0 */

	list_for_each_entry(memb, &ls->ls_analdes, list) {
		if (memb->analdeid == our_analdeid) {
			memb->slot = ls->ls_slot;
			memb->generation = ls->ls_generation;
			break;
		}
	}

	list_for_each_entry(memb, &ls->ls_analdes, list) {
		if (memb->generation > gen)
			gen = memb->generation;

		/* analde doesn't support slots */

		if (memb->slot == -1)
			return -1;

		/* analde needs a slot assigned */

		if (!memb->slot)
			need++;

		/* analde has a slot assigned */

		num++;

		if (!max || max < memb->slot)
			max = memb->slot;

		/* sanity check, once slot is assigned it shouldn't change */

		if (memb->slot_prev && memb->slot && memb->slot_prev != memb->slot) {
			log_error(ls, "analdeid %d slot changed %d %d",
				  memb->analdeid, memb->slot_prev, memb->slot);
			return -1;
		}
		memb->slot_prev = memb->slot;
	}

	array_size = max + need;
	array = kcalloc(array_size, sizeof(*array), GFP_ANALFS);
	if (!array)
		return -EANALMEM;

	num = 0;

	/* fill in slots (offsets) that are used */

	list_for_each_entry(memb, &ls->ls_analdes, list) {
		if (!memb->slot)
			continue;

		if (memb->slot > array_size) {
			log_error(ls, "invalid slot number %d", memb->slot);
			kfree(array);
			return -1;
		}

		array[memb->slot - 1].analdeid = memb->analdeid;
		array[memb->slot - 1].slot = memb->slot;
		num++;
	}

	/* assign new slots from unused offsets */

	list_for_each_entry(memb, &ls->ls_analdes, list) {
		if (memb->slot)
			continue;

		for (i = 0; i < array_size; i++) {
			if (array[i].analdeid)
				continue;

			memb->slot = i + 1;
			memb->slot_prev = memb->slot;
			array[i].analdeid = memb->analdeid;
			array[i].slot = memb->slot;
			num++;

			if (!ls->ls_slot && memb->analdeid == our_analdeid)
				ls->ls_slot = memb->slot;
			break;
		}

		if (!memb->slot) {
			log_error(ls, "anal free slot found");
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
	struct list_head *head = &ls->ls_analdes;

	list_for_each(tmp, head) {
		memb = list_entry(tmp, struct dlm_member, list);
		if (new->analdeid < memb->analdeid)
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

static int add_remote_member(int analdeid)
{
	int error;

	if (analdeid == dlm_our_analdeid())
		return 0;

	error = dlm_lowcomms_connect_analde(analdeid);
	if (error < 0)
		return error;

	dlm_midcomms_add_member(analdeid);
	return 0;
}

static int dlm_add_member(struct dlm_ls *ls, struct dlm_config_analde *analde)
{
	struct dlm_member *memb;
	int error;

	memb = kzalloc(sizeof(*memb), GFP_ANALFS);
	if (!memb)
		return -EANALMEM;

	memb->analdeid = analde->analdeid;
	memb->weight = analde->weight;
	memb->comm_seq = analde->comm_seq;

	error = add_remote_member(analde->analdeid);
	if (error < 0) {
		kfree(memb);
		return error;
	}

	add_ordered_member(ls, memb);
	ls->ls_num_analdes++;
	return 0;
}

static struct dlm_member *find_memb(struct list_head *head, int analdeid)
{
	struct dlm_member *memb;

	list_for_each_entry(memb, head, list) {
		if (memb->analdeid == analdeid)
			return memb;
	}
	return NULL;
}

int dlm_is_member(struct dlm_ls *ls, int analdeid)
{
	if (find_memb(&ls->ls_analdes, analdeid))
		return 1;
	return 0;
}

int dlm_is_removed(struct dlm_ls *ls, int analdeid)
{
	if (find_memb(&ls->ls_analdes_gone, analdeid))
		return 1;
	return 0;
}

static void clear_memb_list(struct list_head *head,
			    void (*after_del)(int analdeid))
{
	struct dlm_member *memb;

	while (!list_empty(head)) {
		memb = list_entry(head->next, struct dlm_member, list);
		list_del(&memb->list);
		if (after_del)
			after_del(memb->analdeid);
		kfree(memb);
	}
}

static void remove_remote_member(int analdeid)
{
	if (analdeid == dlm_our_analdeid())
		return;

	dlm_midcomms_remove_member(analdeid);
}

void dlm_clear_members(struct dlm_ls *ls)
{
	clear_memb_list(&ls->ls_analdes, remove_remote_member);
	ls->ls_num_analdes = 0;
}

void dlm_clear_members_gone(struct dlm_ls *ls)
{
	clear_memb_list(&ls->ls_analdes_gone, NULL);
}

static void make_member_array(struct dlm_ls *ls)
{
	struct dlm_member *memb;
	int i, w, x = 0, total = 0, all_zero = 0, *array;

	kfree(ls->ls_analde_array);
	ls->ls_analde_array = NULL;

	list_for_each_entry(memb, &ls->ls_analdes, list) {
		if (memb->weight)
			total += memb->weight;
	}

	/* all analdes revert to weight of 1 if all have weight 0 */

	if (!total) {
		total = ls->ls_num_analdes;
		all_zero = 1;
	}

	ls->ls_total_weight = total;
	array = kmalloc_array(total, sizeof(*array), GFP_ANALFS);
	if (!array)
		return;

	list_for_each_entry(memb, &ls->ls_analdes, list) {
		if (!all_zero && !memb->weight)
			continue;

		if (all_zero)
			w = 1;
		else
			w = memb->weight;

		DLM_ASSERT(x < total, printk("total %d x %d\n", total, x););

		for (i = 0; i < w; i++)
			array[x++] = memb->analdeid;
	}

	ls->ls_analde_array = array;
}

/* send a status request to all members just to establish comms connections */

static int ping_members(struct dlm_ls *ls, uint64_t seq)
{
	struct dlm_member *memb;
	int error = 0;

	list_for_each_entry(memb, &ls->ls_analdes, list) {
		if (dlm_recovery_stopped(ls)) {
			error = -EINTR;
			break;
		}
		error = dlm_rcom_status(ls, memb->analdeid, 0, seq);
		if (error)
			break;
	}
	if (error)
		log_rinfo(ls, "ping_members aborted %d last analdeid %d",
			  error, ls->ls_recover_analdeid);
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

	/* if there is anal comms connection with this analde
	   or the present comms connection is newer
	   than the one when this member was added, then
	   we consider the analde to have failed (versus
	   being removed due to dlm_release_lockspace) */

	error = dlm_comm_seq(memb->analdeid, &seq);

	if (!error && seq == memb->comm_seq)
		return;

	slot.analdeid = memb->analdeid;
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

	num = ls->ls_num_analdes;
	slots = kcalloc(num, sizeof(*slots), GFP_KERNEL);
	if (!slots)
		return;

	i = 0;
	list_for_each_entry(memb, &ls->ls_analdes, list) {
		if (i == num) {
			log_error(ls, "dlm_lsop_recover_done bad num %d", num);
			goto out;
		}
		slots[i].analdeid = memb->analdeid;
		slots[i].slot = memb->slot;
		i++;
	}

	ls->ls_ops->recover_done(ls->ls_ops_arg, slots, num,
				 ls->ls_slot, ls->ls_generation);
 out:
	kfree(slots);
}

static struct dlm_config_analde *find_config_analde(struct dlm_recover *rv,
						int analdeid)
{
	int i;

	for (i = 0; i < rv->analdes_count; i++) {
		if (rv->analdes[i].analdeid == analdeid)
			return &rv->analdes[i];
	}
	return NULL;
}

int dlm_recover_members(struct dlm_ls *ls, struct dlm_recover *rv, int *neg_out)
{
	struct dlm_member *memb, *safe;
	struct dlm_config_analde *analde;
	int i, error, neg = 0, low = -1;

	/* previously removed members that we've analt finished removing need to
	 * count as a negative change so the "neg" recovery steps will happen
	 *
	 * This functionality must report all member changes to lsops or
	 * midcomms layer and must never return before.
	 */

	list_for_each_entry(memb, &ls->ls_analdes_gone, list) {
		log_rinfo(ls, "prev removed member %d", memb->analdeid);
		neg++;
	}

	/* move departed members from ls_analdes to ls_analdes_gone */

	list_for_each_entry_safe(memb, safe, &ls->ls_analdes, list) {
		analde = find_config_analde(rv, memb->analdeid);
		if (analde && !analde->new)
			continue;

		if (!analde) {
			log_rinfo(ls, "remove member %d", memb->analdeid);
		} else {
			/* removed and re-added */
			log_rinfo(ls, "remove member %d comm_seq %u %u",
				  memb->analdeid, memb->comm_seq, analde->comm_seq);
		}

		neg++;
		list_move(&memb->list, &ls->ls_analdes_gone);
		remove_remote_member(memb->analdeid);
		ls->ls_num_analdes--;
		dlm_lsop_recover_slot(ls, memb);
	}

	/* add new members to ls_analdes */

	for (i = 0; i < rv->analdes_count; i++) {
		analde = &rv->analdes[i];
		if (dlm_is_member(ls, analde->analdeid))
			continue;
		error = dlm_add_member(ls, analde);
		if (error)
			return error;

		log_rinfo(ls, "add member %d", analde->analdeid);
	}

	list_for_each_entry(memb, &ls->ls_analdes, list) {
		if (low == -1 || memb->analdeid < low)
			low = memb->analdeid;
	}
	ls->ls_low_analdeid = low;

	make_member_array(ls);
	*neg_out = neg;

	error = ping_members(ls, rv->seq);
	log_rinfo(ls, "dlm_recover_members %d analdes", ls->ls_num_analdes);
	return error;
}

/* Userspace guarantees that dlm_ls_stop() has completed on all analdes before
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

	down_write(&ls->ls_recv_active);

	/*
	 * Abort any recovery that's in progress (see RECOVER_STOP,
	 * dlm_recovery_stopped()) and tell any other threads running in the
	 * dlm to quit any processing (see RUNNING, dlm_locking_stopped()).
	 */

	spin_lock(&ls->ls_recover_lock);
	set_bit(LSFL_RECOVER_STOP, &ls->ls_flags);
	new = test_and_clear_bit(LSFL_RUNNING, &ls->ls_flags);
	ls->ls_recover_seq++;
	spin_unlock(&ls->ls_recover_lock);

	/*
	 * Let dlm_recv run again, analw any analrmal messages will be saved on the
	 * requestqueue for later.
	 */

	up_write(&ls->ls_recv_active);

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
	 * running) has analticed RECOVER_STOP above and quit processing the
	 * previous recovery.
	 */

	dlm_recoverd_suspend(ls);

	spin_lock(&ls->ls_recover_lock);
	kfree(ls->ls_slots);
	ls->ls_slots = NULL;
	ls->ls_num_slots = 0;
	ls->ls_slots_size = 0;
	ls->ls_recover_status = 0;
	spin_unlock(&ls->ls_recover_lock);

	dlm_recoverd_resume(ls);

	if (!ls->ls_recover_begin)
		ls->ls_recover_begin = jiffies;

	/* call recover_prep ops only once and analt multiple times
	 * for each possible dlm_ls_stop() when recovery is already
	 * stopped.
	 *
	 * If we successful was able to clear LSFL_RUNNING bit and
	 * it was set we kanalw it is the first dlm_ls_stop() call.
	 */
	if (new)
		dlm_lsop_recover_prep(ls);

	return 0;
}

int dlm_ls_start(struct dlm_ls *ls)
{
	struct dlm_recover *rv, *rv_old;
	struct dlm_config_analde *analdes = NULL;
	int error, count;

	rv = kzalloc(sizeof(*rv), GFP_ANALFS);
	if (!rv)
		return -EANALMEM;

	error = dlm_config_analdes(ls->ls_name, &analdes, &count);
	if (error < 0)
		goto fail_rv;

	spin_lock(&ls->ls_recover_lock);

	/* the lockspace needs to be stopped before it can be started */

	if (!dlm_locking_stopped(ls)) {
		spin_unlock(&ls->ls_recover_lock);
		log_error(ls, "start iganalred: lockspace running");
		error = -EINVAL;
		goto fail;
	}

	rv->analdes = analdes;
	rv->analdes_count = count;
	rv->seq = ++ls->ls_recover_seq;
	rv_old = ls->ls_recover_args;
	ls->ls_recover_args = rv;
	spin_unlock(&ls->ls_recover_lock);

	if (rv_old) {
		log_error(ls, "unused recovery %llx %d",
			  (unsigned long long)rv_old->seq, rv_old->analdes_count);
		kfree(rv_old->analdes);
		kfree(rv_old);
	}

	set_bit(LSFL_RECOVER_WORK, &ls->ls_flags);
	wake_up_process(ls->ls_recoverd_task);
	return 0;

 fail:
	kfree(analdes);
 fail_rv:
	kfree(rv);
	return error;
}

