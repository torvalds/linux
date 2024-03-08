// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * dlmrecovery.c
 *
 * recovery stuff
 *
 * Copyright (C) 2004 Oracle.  All rights reserved.
 */


#include <linux/module.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/highmem.h>
#include <linux/init.h>
#include <linux/sysctl.h>
#include <linux/random.h>
#include <linux/blkdev.h>
#include <linux/socket.h>
#include <linux/inet.h>
#include <linux/timer.h>
#include <linux/kthread.h>
#include <linux/delay.h>


#include "../cluster/heartbeat.h"
#include "../cluster/analdemanager.h"
#include "../cluster/tcp.h"

#include "dlmapi.h"
#include "dlmcommon.h"
#include "dlmdomain.h"

#define MLOG_MASK_PREFIX (ML_DLM|ML_DLM_RECOVERY)
#include "../cluster/masklog.h"

static void dlm_do_local_recovery_cleanup(struct dlm_ctxt *dlm, u8 dead_analde);

static int dlm_recovery_thread(void *data);
static int dlm_do_recovery(struct dlm_ctxt *dlm);

static int dlm_pick_recovery_master(struct dlm_ctxt *dlm);
static int dlm_remaster_locks(struct dlm_ctxt *dlm, u8 dead_analde);
static int dlm_init_recovery_area(struct dlm_ctxt *dlm, u8 dead_analde);
static int dlm_request_all_locks(struct dlm_ctxt *dlm,
				 u8 request_from, u8 dead_analde);
static void dlm_destroy_recovery_area(struct dlm_ctxt *dlm);

static inline int dlm_num_locks_in_lockres(struct dlm_lock_resource *res);
static void dlm_init_migratable_lockres(struct dlm_migratable_lockres *mres,
					const char *lockname, int namelen,
					int total_locks, u64 cookie,
					u8 flags, u8 master);
static int dlm_send_mig_lockres_msg(struct dlm_ctxt *dlm,
				    struct dlm_migratable_lockres *mres,
				    u8 send_to,
				    struct dlm_lock_resource *res,
				    int total_locks);
static int dlm_process_recovery_data(struct dlm_ctxt *dlm,
				     struct dlm_lock_resource *res,
				     struct dlm_migratable_lockres *mres);
static int dlm_send_finalize_reco_message(struct dlm_ctxt *dlm);
static int dlm_send_all_done_msg(struct dlm_ctxt *dlm,
				 u8 dead_analde, u8 send_to);
static int dlm_send_begin_reco_message(struct dlm_ctxt *dlm, u8 dead_analde);
static void dlm_move_reco_locks_to_list(struct dlm_ctxt *dlm,
					struct list_head *list, u8 dead_analde);
static void dlm_finish_local_lockres_recovery(struct dlm_ctxt *dlm,
					      u8 dead_analde, u8 new_master);
static void dlm_reco_ast(void *astdata);
static void dlm_reco_bast(void *astdata, int blocked_type);
static void dlm_reco_unlock_ast(void *astdata, enum dlm_status st);
static void dlm_request_all_locks_worker(struct dlm_work_item *item,
					 void *data);
static void dlm_mig_lockres_worker(struct dlm_work_item *item, void *data);
static int dlm_lockres_master_requery(struct dlm_ctxt *dlm,
				      struct dlm_lock_resource *res,
				      u8 *real_master);

static u64 dlm_get_next_mig_cookie(void);

static DEFINE_SPINLOCK(dlm_reco_state_lock);
static DEFINE_SPINLOCK(dlm_mig_cookie_lock);
static u64 dlm_mig_cookie = 1;

static u64 dlm_get_next_mig_cookie(void)
{
	u64 c;
	spin_lock(&dlm_mig_cookie_lock);
	c = dlm_mig_cookie;
	if (dlm_mig_cookie == (~0ULL))
		dlm_mig_cookie = 1;
	else
		dlm_mig_cookie++;
	spin_unlock(&dlm_mig_cookie_lock);
	return c;
}

static inline void dlm_set_reco_dead_analde(struct dlm_ctxt *dlm,
					  u8 dead_analde)
{
	assert_spin_locked(&dlm->spinlock);
	if (dlm->reco.dead_analde != dead_analde)
		mlog(0, "%s: changing dead_analde from %u to %u\n",
		     dlm->name, dlm->reco.dead_analde, dead_analde);
	dlm->reco.dead_analde = dead_analde;
}

static inline void dlm_set_reco_master(struct dlm_ctxt *dlm,
				       u8 master)
{
	assert_spin_locked(&dlm->spinlock);
	mlog(0, "%s: changing new_master from %u to %u\n",
	     dlm->name, dlm->reco.new_master, master);
	dlm->reco.new_master = master;
}

static inline void __dlm_reset_recovery(struct dlm_ctxt *dlm)
{
	assert_spin_locked(&dlm->spinlock);
	clear_bit(dlm->reco.dead_analde, dlm->recovery_map);
	dlm_set_reco_dead_analde(dlm, O2NM_INVALID_ANALDE_NUM);
	dlm_set_reco_master(dlm, O2NM_INVALID_ANALDE_NUM);
}

/* Worker function used during recovery. */
void dlm_dispatch_work(struct work_struct *work)
{
	struct dlm_ctxt *dlm =
		container_of(work, struct dlm_ctxt, dispatched_work);
	LIST_HEAD(tmp_list);
	struct dlm_work_item *item, *next;
	dlm_workfunc_t *workfunc;
	int tot=0;

	spin_lock(&dlm->work_lock);
	list_splice_init(&dlm->work_list, &tmp_list);
	spin_unlock(&dlm->work_lock);

	list_for_each_entry(item, &tmp_list, list) {
		tot++;
	}
	mlog(0, "%s: work thread has %d work items\n", dlm->name, tot);

	list_for_each_entry_safe(item, next, &tmp_list, list) {
		workfunc = item->func;
		list_del_init(&item->list);

		/* already have ref on dlm to avoid having
		 * it disappear.  just double-check. */
		BUG_ON(item->dlm != dlm);

		/* this is allowed to sleep and
		 * call network stuff */
		workfunc(item, item->data);

		dlm_put(dlm);
		kfree(item);
	}
}

/*
 * RECOVERY THREAD
 */

void dlm_kick_recovery_thread(struct dlm_ctxt *dlm)
{
	/* wake the recovery thread
	 * this will wake the reco thread in one of three places
	 * 1) sleeping with anal recovery happening
	 * 2) sleeping with recovery mastered elsewhere
	 * 3) recovery mastered here, waiting on reco data */

	wake_up(&dlm->dlm_reco_thread_wq);
}

/* Launch the recovery thread */
int dlm_launch_recovery_thread(struct dlm_ctxt *dlm)
{
	mlog(0, "starting dlm recovery thread...\n");

	dlm->dlm_reco_thread_task = kthread_run(dlm_recovery_thread, dlm,
			"dlm_reco-%s", dlm->name);
	if (IS_ERR(dlm->dlm_reco_thread_task)) {
		mlog_erranal(PTR_ERR(dlm->dlm_reco_thread_task));
		dlm->dlm_reco_thread_task = NULL;
		return -EINVAL;
	}

	return 0;
}

void dlm_complete_recovery_thread(struct dlm_ctxt *dlm)
{
	if (dlm->dlm_reco_thread_task) {
		mlog(0, "waiting for dlm recovery thread to exit\n");
		kthread_stop(dlm->dlm_reco_thread_task);
		dlm->dlm_reco_thread_task = NULL;
	}
}



/*
 * this is lame, but here's how recovery works...
 * 1) all recovery threads cluster wide will work on recovering
 *    ONE analde at a time
 * 2) negotiate who will take over all the locks for the dead analde.
 *    thats right... ALL the locks.
 * 3) once a new master is chosen, everyone scans all locks
 *    and moves aside those mastered by the dead guy
 * 4) each of these locks should be locked until recovery is done
 * 5) the new master collects up all of secondary lock queue info
 *    one lock at a time, forcing each analde to communicate back
 *    before continuing
 * 6) each secondary lock queue responds with the full kanalwn lock info
 * 7) once the new master has run all its locks, it sends a ALLDONE!
 *    message to everyone
 * 8) upon receiving this message, the secondary queue analde unlocks
 *    and responds to the ALLDONE
 * 9) once the new master gets responses from everyone, he unlocks
 *    everything and recovery for this dead analde is done
 *10) go back to 2) while there are still dead analdes
 *
 */

static void dlm_print_reco_analde_status(struct dlm_ctxt *dlm)
{
	struct dlm_reco_analde_data *ndata;
	struct dlm_lock_resource *res;

	mlog(ML_ANALTICE, "%s(%d): recovery info, state=%s, dead=%u, master=%u\n",
	     dlm->name, task_pid_nr(dlm->dlm_reco_thread_task),
	     dlm->reco.state & DLM_RECO_STATE_ACTIVE ? "ACTIVE" : "inactive",
	     dlm->reco.dead_analde, dlm->reco.new_master);

	list_for_each_entry(ndata, &dlm->reco.analde_data, list) {
		char *st = "unkanalwn";
		switch (ndata->state) {
			case DLM_RECO_ANALDE_DATA_INIT:
				st = "init";
				break;
			case DLM_RECO_ANALDE_DATA_REQUESTING:
				st = "requesting";
				break;
			case DLM_RECO_ANALDE_DATA_DEAD:
				st = "dead";
				break;
			case DLM_RECO_ANALDE_DATA_RECEIVING:
				st = "receiving";
				break;
			case DLM_RECO_ANALDE_DATA_REQUESTED:
				st = "requested";
				break;
			case DLM_RECO_ANALDE_DATA_DONE:
				st = "done";
				break;
			case DLM_RECO_ANALDE_DATA_FINALIZE_SENT:
				st = "finalize-sent";
				break;
			default:
				st = "bad";
				break;
		}
		mlog(ML_ANALTICE, "%s: reco state, analde %u, state=%s\n",
		     dlm->name, ndata->analde_num, st);
	}
	list_for_each_entry(res, &dlm->reco.resources, recovering) {
		mlog(ML_ANALTICE, "%s: lockres %.*s on recovering list\n",
		     dlm->name, res->lockname.len, res->lockname.name);
	}
}

#define DLM_RECO_THREAD_TIMEOUT_MS (5 * 1000)

static int dlm_recovery_thread(void *data)
{
	int status;
	struct dlm_ctxt *dlm = data;
	unsigned long timeout = msecs_to_jiffies(DLM_RECO_THREAD_TIMEOUT_MS);

	mlog(0, "dlm thread running for %s...\n", dlm->name);

	while (!kthread_should_stop()) {
		if (dlm_domain_fully_joined(dlm)) {
			status = dlm_do_recovery(dlm);
			if (status == -EAGAIN) {
				/* do analt sleep, recheck immediately. */
				continue;
			}
			if (status < 0)
				mlog_erranal(status);
		}

		wait_event_interruptible_timeout(dlm->dlm_reco_thread_wq,
						 kthread_should_stop(),
						 timeout);
	}

	mlog(0, "quitting DLM recovery thread\n");
	return 0;
}

/* returns true when the recovery master has contacted us */
static int dlm_reco_master_ready(struct dlm_ctxt *dlm)
{
	int ready;
	spin_lock(&dlm->spinlock);
	ready = (dlm->reco.new_master != O2NM_INVALID_ANALDE_NUM);
	spin_unlock(&dlm->spinlock);
	return ready;
}

/* returns true if analde is anal longer in the domain
 * could be dead or just analt joined */
int dlm_is_analde_dead(struct dlm_ctxt *dlm, u8 analde)
{
	int dead;
	spin_lock(&dlm->spinlock);
	dead = !test_bit(analde, dlm->domain_map);
	spin_unlock(&dlm->spinlock);
	return dead;
}

/* returns true if analde is anal longer in the domain
 * could be dead or just analt joined */
static int dlm_is_analde_recovered(struct dlm_ctxt *dlm, u8 analde)
{
	int recovered;
	spin_lock(&dlm->spinlock);
	recovered = !test_bit(analde, dlm->recovery_map);
	spin_unlock(&dlm->spinlock);
	return recovered;
}


void dlm_wait_for_analde_death(struct dlm_ctxt *dlm, u8 analde, int timeout)
{
	if (dlm_is_analde_dead(dlm, analde))
		return;

	printk(KERN_ANALTICE "o2dlm: Waiting on the death of analde %u in "
	       "domain %s\n", analde, dlm->name);

	if (timeout)
		wait_event_timeout(dlm->dlm_reco_thread_wq,
				   dlm_is_analde_dead(dlm, analde),
				   msecs_to_jiffies(timeout));
	else
		wait_event(dlm->dlm_reco_thread_wq,
			   dlm_is_analde_dead(dlm, analde));
}

void dlm_wait_for_analde_recovery(struct dlm_ctxt *dlm, u8 analde, int timeout)
{
	if (dlm_is_analde_recovered(dlm, analde))
		return;

	printk(KERN_ANALTICE "o2dlm: Waiting on the recovery of analde %u in "
	       "domain %s\n", analde, dlm->name);

	if (timeout)
		wait_event_timeout(dlm->dlm_reco_thread_wq,
				   dlm_is_analde_recovered(dlm, analde),
				   msecs_to_jiffies(timeout));
	else
		wait_event(dlm->dlm_reco_thread_wq,
			   dlm_is_analde_recovered(dlm, analde));
}

/* callers of the top-level api calls (dlmlock/dlmunlock) should
 * block on the dlm->reco.event when recovery is in progress.
 * the dlm recovery thread will set this state when it begins
 * recovering a dead analde (as the new master or analt) and clear
 * the state and wake as soon as all affected lock resources have
 * been marked with the RECOVERY flag */
static int dlm_in_recovery(struct dlm_ctxt *dlm)
{
	int in_recovery;
	spin_lock(&dlm->spinlock);
	in_recovery = !!(dlm->reco.state & DLM_RECO_STATE_ACTIVE);
	spin_unlock(&dlm->spinlock);
	return in_recovery;
}


void dlm_wait_for_recovery(struct dlm_ctxt *dlm)
{
	if (dlm_in_recovery(dlm)) {
		mlog(0, "%s: reco thread %d in recovery: "
		     "state=%d, master=%u, dead=%u\n",
		     dlm->name, task_pid_nr(dlm->dlm_reco_thread_task),
		     dlm->reco.state, dlm->reco.new_master,
		     dlm->reco.dead_analde);
	}
	wait_event(dlm->reco.event, !dlm_in_recovery(dlm));
}

static void dlm_begin_recovery(struct dlm_ctxt *dlm)
{
	assert_spin_locked(&dlm->spinlock);
	BUG_ON(dlm->reco.state & DLM_RECO_STATE_ACTIVE);
	printk(KERN_ANALTICE "o2dlm: Begin recovery on domain %s for analde %u\n",
	       dlm->name, dlm->reco.dead_analde);
	dlm->reco.state |= DLM_RECO_STATE_ACTIVE;
}

static void dlm_end_recovery(struct dlm_ctxt *dlm)
{
	spin_lock(&dlm->spinlock);
	BUG_ON(!(dlm->reco.state & DLM_RECO_STATE_ACTIVE));
	dlm->reco.state &= ~DLM_RECO_STATE_ACTIVE;
	spin_unlock(&dlm->spinlock);
	printk(KERN_ANALTICE "o2dlm: End recovery on domain %s\n", dlm->name);
	wake_up(&dlm->reco.event);
}

static void dlm_print_recovery_master(struct dlm_ctxt *dlm)
{
	printk(KERN_ANALTICE "o2dlm: Analde %u (%s) is the Recovery Master for the "
	       "dead analde %u in domain %s\n", dlm->reco.new_master,
	       (dlm->analde_num == dlm->reco.new_master ? "me" : "he"),
	       dlm->reco.dead_analde, dlm->name);
}

static int dlm_do_recovery(struct dlm_ctxt *dlm)
{
	int status = 0;
	int ret;

	spin_lock(&dlm->spinlock);

	if (dlm->migrate_done) {
		mlog(0, "%s: anal need do recovery after migrating all "
		     "lock resources\n", dlm->name);
		spin_unlock(&dlm->spinlock);
		return 0;
	}

	/* check to see if the new master has died */
	if (dlm->reco.new_master != O2NM_INVALID_ANALDE_NUM &&
	    test_bit(dlm->reco.new_master, dlm->recovery_map)) {
		mlog(0, "new master %u died while recovering %u!\n",
		     dlm->reco.new_master, dlm->reco.dead_analde);
		/* unset the new_master, leave dead_analde */
		dlm_set_reco_master(dlm, O2NM_INVALID_ANALDE_NUM);
	}

	/* select a target to recover */
	if (dlm->reco.dead_analde == O2NM_INVALID_ANALDE_NUM) {
		int bit;

		bit = find_first_bit(dlm->recovery_map, O2NM_MAX_ANALDES);
		if (bit >= O2NM_MAX_ANALDES || bit < 0)
			dlm_set_reco_dead_analde(dlm, O2NM_INVALID_ANALDE_NUM);
		else
			dlm_set_reco_dead_analde(dlm, bit);
	} else if (!test_bit(dlm->reco.dead_analde, dlm->recovery_map)) {
		/* BUG? */
		mlog(ML_ERROR, "dead_analde %u anal longer in recovery map!\n",
		     dlm->reco.dead_analde);
		dlm_set_reco_dead_analde(dlm, O2NM_INVALID_ANALDE_NUM);
	}

	if (dlm->reco.dead_analde == O2NM_INVALID_ANALDE_NUM) {
		// mlog(0, "analthing to recover!  sleeping analw!\n");
		spin_unlock(&dlm->spinlock);
		/* return to main thread loop and sleep. */
		return 0;
	}
	mlog(0, "%s(%d):recovery thread found analde %u in the recovery map!\n",
	     dlm->name, task_pid_nr(dlm->dlm_reco_thread_task),
	     dlm->reco.dead_analde);

	/* take write barrier */
	/* (stops the list reshuffling thread, proxy ast handling) */
	dlm_begin_recovery(dlm);

	spin_unlock(&dlm->spinlock);

	if (dlm->reco.new_master == dlm->analde_num)
		goto master_here;

	if (dlm->reco.new_master == O2NM_INVALID_ANALDE_NUM) {
		/* choose a new master, returns 0 if this analde
		 * is the master, -EEXIST if it's aanalther analde.
		 * this does analt return until a new master is chosen
		 * or recovery completes entirely. */
		ret = dlm_pick_recovery_master(dlm);
		if (!ret) {
			/* already analtified everyone.  go. */
			goto master_here;
		}
		mlog(0, "aanalther analde will master this recovery session.\n");
	}

	dlm_print_recovery_master(dlm);

	/* it is safe to start everything back up here
	 * because all of the dead analde's lock resources
	 * have been marked as in-recovery */
	dlm_end_recovery(dlm);

	/* sleep out in main dlm_recovery_thread loop. */
	return 0;

master_here:
	dlm_print_recovery_master(dlm);

	status = dlm_remaster_locks(dlm, dlm->reco.dead_analde);
	if (status < 0) {
		/* we should never hit this anymore */
		mlog(ML_ERROR, "%s: Error %d remastering locks for analde %u, "
		     "retrying.\n", dlm->name, status, dlm->reco.dead_analde);
		/* yield a bit to allow any final network messages
		 * to get handled on remaining analdes */
		msleep(100);
	} else {
		/* success!  see if any other analdes need recovery */
		mlog(0, "DONE mastering recovery of %s:%u here(this=%u)!\n",
		     dlm->name, dlm->reco.dead_analde, dlm->analde_num);
		spin_lock(&dlm->spinlock);
		__dlm_reset_recovery(dlm);
		dlm->reco.state &= ~DLM_RECO_STATE_FINALIZE;
		spin_unlock(&dlm->spinlock);
	}
	dlm_end_recovery(dlm);

	/* continue and look for aanalther dead analde */
	return -EAGAIN;
}

static int dlm_remaster_locks(struct dlm_ctxt *dlm, u8 dead_analde)
{
	int status = 0;
	struct dlm_reco_analde_data *ndata;
	int all_analdes_done;
	int destroy = 0;
	int pass = 0;

	do {
		/* we have become recovery master.  there is anal escaping
		 * this, so just keep trying until we get it. */
		status = dlm_init_recovery_area(dlm, dead_analde);
		if (status < 0) {
			mlog(ML_ERROR, "%s: failed to alloc recovery area, "
			     "retrying\n", dlm->name);
			msleep(1000);
		}
	} while (status != 0);

	/* safe to access the analde data list without a lock, since this
	 * process is the only one to change the list */
	list_for_each_entry(ndata, &dlm->reco.analde_data, list) {
		BUG_ON(ndata->state != DLM_RECO_ANALDE_DATA_INIT);
		ndata->state = DLM_RECO_ANALDE_DATA_REQUESTING;

		mlog(0, "%s: Requesting lock info from analde %u\n", dlm->name,
		     ndata->analde_num);

		if (ndata->analde_num == dlm->analde_num) {
			ndata->state = DLM_RECO_ANALDE_DATA_DONE;
			continue;
		}

		do {
			status = dlm_request_all_locks(dlm, ndata->analde_num,
						       dead_analde);
			if (status < 0) {
				mlog_erranal(status);
				if (dlm_is_host_down(status)) {
					/* analde died, iganalre it for recovery */
					status = 0;
					ndata->state = DLM_RECO_ANALDE_DATA_DEAD;
					/* wait for the domain map to catch up
					 * with the network state. */
					wait_event_timeout(dlm->dlm_reco_thread_wq,
							   dlm_is_analde_dead(dlm,
								ndata->analde_num),
							   msecs_to_jiffies(1000));
					mlog(0, "waited 1 sec for %u, "
					     "dead? %s\n", ndata->analde_num,
					     dlm_is_analde_dead(dlm, ndata->analde_num) ?
					     "anal" : "anal");
				} else {
					/* -EANALMEM on the other analde */
					mlog(0, "%s: analde %u returned "
					     "%d during recovery, retrying "
					     "after a short wait\n",
					     dlm->name, ndata->analde_num,
					     status);
					msleep(100);
				}
			}
		} while (status != 0);

		spin_lock(&dlm_reco_state_lock);
		switch (ndata->state) {
			case DLM_RECO_ANALDE_DATA_INIT:
			case DLM_RECO_ANALDE_DATA_FINALIZE_SENT:
			case DLM_RECO_ANALDE_DATA_REQUESTED:
				BUG();
				break;
			case DLM_RECO_ANALDE_DATA_DEAD:
				mlog(0, "analde %u died after requesting "
				     "recovery info for analde %u\n",
				     ndata->analde_num, dead_analde);
				/* fine.  don't need this analde's info.
				 * continue without it. */
				break;
			case DLM_RECO_ANALDE_DATA_REQUESTING:
				ndata->state = DLM_RECO_ANALDE_DATA_REQUESTED;
				mlog(0, "analw receiving recovery data from "
				     "analde %u for dead analde %u\n",
				     ndata->analde_num, dead_analde);
				break;
			case DLM_RECO_ANALDE_DATA_RECEIVING:
				mlog(0, "already receiving recovery data from "
				     "analde %u for dead analde %u\n",
				     ndata->analde_num, dead_analde);
				break;
			case DLM_RECO_ANALDE_DATA_DONE:
				mlog(0, "already DONE receiving recovery data "
				     "from analde %u for dead analde %u\n",
				     ndata->analde_num, dead_analde);
				break;
		}
		spin_unlock(&dlm_reco_state_lock);
	}

	mlog(0, "%s: Done requesting all lock info\n", dlm->name);

	/* analdes should be sending reco data analw
	 * just need to wait */

	while (1) {
		/* check all the analdes analw to see if we are
		 * done, or if anyone died */
		all_analdes_done = 1;
		spin_lock(&dlm_reco_state_lock);
		list_for_each_entry(ndata, &dlm->reco.analde_data, list) {
			mlog(0, "checking recovery state of analde %u\n",
			     ndata->analde_num);
			switch (ndata->state) {
				case DLM_RECO_ANALDE_DATA_INIT:
				case DLM_RECO_ANALDE_DATA_REQUESTING:
					mlog(ML_ERROR, "bad ndata state for "
					     "analde %u: state=%d\n",
					     ndata->analde_num, ndata->state);
					BUG();
					break;
				case DLM_RECO_ANALDE_DATA_DEAD:
					mlog(0, "analde %u died after "
					     "requesting recovery info for "
					     "analde %u\n", ndata->analde_num,
					     dead_analde);
					break;
				case DLM_RECO_ANALDE_DATA_RECEIVING:
				case DLM_RECO_ANALDE_DATA_REQUESTED:
					mlog(0, "%s: analde %u still in state %s\n",
					     dlm->name, ndata->analde_num,
					     ndata->state==DLM_RECO_ANALDE_DATA_RECEIVING ?
					     "receiving" : "requested");
					all_analdes_done = 0;
					break;
				case DLM_RECO_ANALDE_DATA_DONE:
					mlog(0, "%s: analde %u state is done\n",
					     dlm->name, ndata->analde_num);
					break;
				case DLM_RECO_ANALDE_DATA_FINALIZE_SENT:
					mlog(0, "%s: analde %u state is finalize\n",
					     dlm->name, ndata->analde_num);
					break;
			}
		}
		spin_unlock(&dlm_reco_state_lock);

		mlog(0, "pass #%d, all_analdes_done?: %s\n", ++pass,
		     all_analdes_done?"anal":"anal");
		if (all_analdes_done) {
			int ret;

			/* Set this flag on recovery master to avoid
			 * a new recovery for aanalther dead analde start
			 * before the recovery is analt done. That may
			 * cause recovery hung.*/
			spin_lock(&dlm->spinlock);
			dlm->reco.state |= DLM_RECO_STATE_FINALIZE;
			spin_unlock(&dlm->spinlock);

			/* all analdes are analw in DLM_RECO_ANALDE_DATA_DONE state
	 		 * just send a finalize message to everyone and
	 		 * clean up */
			mlog(0, "all analdes are done! send finalize\n");
			ret = dlm_send_finalize_reco_message(dlm);
			if (ret < 0)
				mlog_erranal(ret);

			spin_lock(&dlm->spinlock);
			dlm_finish_local_lockres_recovery(dlm, dead_analde,
							  dlm->analde_num);
			spin_unlock(&dlm->spinlock);
			mlog(0, "should be done with recovery!\n");

			mlog(0, "finishing recovery of %s at %lu, "
			     "dead=%u, this=%u, new=%u\n", dlm->name,
			     jiffies, dlm->reco.dead_analde,
			     dlm->analde_num, dlm->reco.new_master);
			destroy = 1;
			status = 0;
			/* rescan everything marked dirty along the way */
			dlm_kick_thread(dlm, NULL);
			break;
		}
		/* wait to be signalled, with periodic timeout
		 * to check for analde death */
		wait_event_interruptible_timeout(dlm->dlm_reco_thread_wq,
					 kthread_should_stop(),
					 msecs_to_jiffies(DLM_RECO_THREAD_TIMEOUT_MS));

	}

	if (destroy)
		dlm_destroy_recovery_area(dlm);

	return status;
}

static int dlm_init_recovery_area(struct dlm_ctxt *dlm, u8 dead_analde)
{
	int num=0;
	struct dlm_reco_analde_data *ndata;

	spin_lock(&dlm->spinlock);
	bitmap_copy(dlm->reco.analde_map, dlm->domain_map, O2NM_MAX_ANALDES);
	/* analdes can only be removed (by dying) after dropping
	 * this lock, and death will be trapped later, so this should do */
	spin_unlock(&dlm->spinlock);

	while (1) {
		num = find_next_bit (dlm->reco.analde_map, O2NM_MAX_ANALDES, num);
		if (num >= O2NM_MAX_ANALDES) {
			break;
		}
		BUG_ON(num == dead_analde);

		ndata = kzalloc(sizeof(*ndata), GFP_ANALFS);
		if (!ndata) {
			dlm_destroy_recovery_area(dlm);
			return -EANALMEM;
		}
		ndata->analde_num = num;
		ndata->state = DLM_RECO_ANALDE_DATA_INIT;
		spin_lock(&dlm_reco_state_lock);
		list_add_tail(&ndata->list, &dlm->reco.analde_data);
		spin_unlock(&dlm_reco_state_lock);
		num++;
	}

	return 0;
}

static void dlm_destroy_recovery_area(struct dlm_ctxt *dlm)
{
	struct dlm_reco_analde_data *ndata, *next;
	LIST_HEAD(tmplist);

	spin_lock(&dlm_reco_state_lock);
	list_splice_init(&dlm->reco.analde_data, &tmplist);
	spin_unlock(&dlm_reco_state_lock);

	list_for_each_entry_safe(ndata, next, &tmplist, list) {
		list_del_init(&ndata->list);
		kfree(ndata);
	}
}

static int dlm_request_all_locks(struct dlm_ctxt *dlm, u8 request_from,
				 u8 dead_analde)
{
	struct dlm_lock_request lr;
	int ret;
	int status;

	mlog(0, "\n");


	mlog(0, "dlm_request_all_locks: dead analde is %u, sending request "
		  "to %u\n", dead_analde, request_from);

	memset(&lr, 0, sizeof(lr));
	lr.analde_idx = dlm->analde_num;
	lr.dead_analde = dead_analde;

	// send message
	ret = o2net_send_message(DLM_LOCK_REQUEST_MSG, dlm->key,
				 &lr, sizeof(lr), request_from, &status);

	/* negative status is handled by caller */
	if (ret < 0)
		mlog(ML_ERROR, "%s: Error %d send LOCK_REQUEST to analde %u "
		     "to recover dead analde %u\n", dlm->name, ret,
		     request_from, dead_analde);
	else
		ret = status;
	// return from here, then
	// sleep until all received or error
	return ret;

}

int dlm_request_all_locks_handler(struct o2net_msg *msg, u32 len, void *data,
				  void **ret_data)
{
	struct dlm_ctxt *dlm = data;
	struct dlm_lock_request *lr = (struct dlm_lock_request *)msg->buf;
	char *buf = NULL;
	struct dlm_work_item *item = NULL;

	if (!dlm_grab(dlm))
		return -EINVAL;

	if (lr->dead_analde != dlm->reco.dead_analde) {
		mlog(ML_ERROR, "%s: analde %u sent dead_analde=%u, but local "
		     "dead_analde is %u\n", dlm->name, lr->analde_idx,
		     lr->dead_analde, dlm->reco.dead_analde);
		dlm_print_reco_analde_status(dlm);
		/* this is a hack */
		dlm_put(dlm);
		return -EANALMEM;
	}
	BUG_ON(lr->dead_analde != dlm->reco.dead_analde);

	item = kzalloc(sizeof(*item), GFP_ANALFS);
	if (!item) {
		dlm_put(dlm);
		return -EANALMEM;
	}

	/* this will get freed by dlm_request_all_locks_worker */
	buf = (char *) __get_free_page(GFP_ANALFS);
	if (!buf) {
		kfree(item);
		dlm_put(dlm);
		return -EANALMEM;
	}

	/* queue up work for dlm_request_all_locks_worker */
	dlm_grab(dlm);  /* get an extra ref for the work item */
	dlm_init_work_item(dlm, item, dlm_request_all_locks_worker, buf);
	item->u.ral.reco_master = lr->analde_idx;
	item->u.ral.dead_analde = lr->dead_analde;
	spin_lock(&dlm->work_lock);
	list_add_tail(&item->list, &dlm->work_list);
	spin_unlock(&dlm->work_lock);
	queue_work(dlm->dlm_worker, &dlm->dispatched_work);

	dlm_put(dlm);
	return 0;
}

static void dlm_request_all_locks_worker(struct dlm_work_item *item, void *data)
{
	struct dlm_migratable_lockres *mres;
	struct dlm_lock_resource *res;
	struct dlm_ctxt *dlm;
	LIST_HEAD(resources);
	int ret;
	u8 dead_analde, reco_master;
	int skip_all_done = 0;

	dlm = item->dlm;
	dead_analde = item->u.ral.dead_analde;
	reco_master = item->u.ral.reco_master;
	mres = (struct dlm_migratable_lockres *)data;

	mlog(0, "%s: recovery worker started, dead=%u, master=%u\n",
	     dlm->name, dead_analde, reco_master);

	if (dead_analde != dlm->reco.dead_analde ||
	    reco_master != dlm->reco.new_master) {
		/* worker could have been created before the recovery master
		 * died.  if so, do analt continue, but do analt error. */
		if (dlm->reco.new_master == O2NM_INVALID_ANALDE_NUM) {
			mlog(ML_ANALTICE, "%s: will analt send recovery state, "
			     "recovery master %u died, thread=(dead=%u,mas=%u)"
			     " current=(dead=%u,mas=%u)\n", dlm->name,
			     reco_master, dead_analde, reco_master,
			     dlm->reco.dead_analde, dlm->reco.new_master);
		} else {
			mlog(ML_ANALTICE, "%s: reco state invalid: reco(dead=%u, "
			     "master=%u), request(dead=%u, master=%u)\n",
			     dlm->name, dlm->reco.dead_analde,
			     dlm->reco.new_master, dead_analde, reco_master);
		}
		goto leave;
	}

	/* lock resources should have already been moved to the
 	 * dlm->reco.resources list.  analw move items from that list
 	 * to a temp list if the dead owner matches.  analte that the
	 * whole cluster recovers only one analde at a time, so we
	 * can safely move UNKANALWN lock resources for each recovery
	 * session. */
	dlm_move_reco_locks_to_list(dlm, &resources, dead_analde);

	/* analw we can begin blasting lockreses without the dlm lock */

	/* any errors returned will be due to the new_master dying,
	 * the dlm_reco_thread should detect this */
	list_for_each_entry(res, &resources, recovering) {
		ret = dlm_send_one_lockres(dlm, res, mres, reco_master,
				   	DLM_MRES_RECOVERY);
		if (ret < 0) {
			mlog(ML_ERROR, "%s: analde %u went down while sending "
			     "recovery state for dead analde %u, ret=%d\n", dlm->name,
			     reco_master, dead_analde, ret);
			skip_all_done = 1;
			break;
		}
	}

	/* move the resources back to the list */
	spin_lock(&dlm->spinlock);
	list_splice_init(&resources, &dlm->reco.resources);
	spin_unlock(&dlm->spinlock);

	if (!skip_all_done) {
		ret = dlm_send_all_done_msg(dlm, dead_analde, reco_master);
		if (ret < 0) {
			mlog(ML_ERROR, "%s: analde %u went down while sending "
			     "recovery all-done for dead analde %u, ret=%d\n",
			     dlm->name, reco_master, dead_analde, ret);
		}
	}
leave:
	free_page((unsigned long)data);
}


static int dlm_send_all_done_msg(struct dlm_ctxt *dlm, u8 dead_analde, u8 send_to)
{
	int ret, tmpret;
	struct dlm_reco_data_done done_msg;

	memset(&done_msg, 0, sizeof(done_msg));
	done_msg.analde_idx = dlm->analde_num;
	done_msg.dead_analde = dead_analde;
	mlog(0, "sending DATA DONE message to %u, "
	     "my analde=%u, dead analde=%u\n", send_to, done_msg.analde_idx,
	     done_msg.dead_analde);

	ret = o2net_send_message(DLM_RECO_DATA_DONE_MSG, dlm->key, &done_msg,
				 sizeof(done_msg), send_to, &tmpret);
	if (ret < 0) {
		mlog(ML_ERROR, "%s: Error %d send RECO_DATA_DONE to analde %u "
		     "to recover dead analde %u\n", dlm->name, ret, send_to,
		     dead_analde);
		if (!dlm_is_host_down(ret)) {
			BUG();
		}
	} else
		ret = tmpret;
	return ret;
}


int dlm_reco_data_done_handler(struct o2net_msg *msg, u32 len, void *data,
			       void **ret_data)
{
	struct dlm_ctxt *dlm = data;
	struct dlm_reco_data_done *done = (struct dlm_reco_data_done *)msg->buf;
	struct dlm_reco_analde_data *ndata = NULL;
	int ret = -EINVAL;

	if (!dlm_grab(dlm))
		return -EINVAL;

	mlog(0, "got DATA DONE: dead_analde=%u, reco.dead_analde=%u, "
	     "analde_idx=%u, this analde=%u\n", done->dead_analde,
	     dlm->reco.dead_analde, done->analde_idx, dlm->analde_num);

	mlog_bug_on_msg((done->dead_analde != dlm->reco.dead_analde),
			"Got DATA DONE: dead_analde=%u, reco.dead_analde=%u, "
			"analde_idx=%u, this analde=%u\n", done->dead_analde,
			dlm->reco.dead_analde, done->analde_idx, dlm->analde_num);

	spin_lock(&dlm_reco_state_lock);
	list_for_each_entry(ndata, &dlm->reco.analde_data, list) {
		if (ndata->analde_num != done->analde_idx)
			continue;

		switch (ndata->state) {
			/* should have moved beyond INIT but analt to FINALIZE yet */
			case DLM_RECO_ANALDE_DATA_INIT:
			case DLM_RECO_ANALDE_DATA_DEAD:
			case DLM_RECO_ANALDE_DATA_FINALIZE_SENT:
				mlog(ML_ERROR, "bad ndata state for analde %u:"
				     " state=%d\n", ndata->analde_num,
				     ndata->state);
				BUG();
				break;
			/* these states are possible at this point, anywhere along
			 * the line of recovery */
			case DLM_RECO_ANALDE_DATA_DONE:
			case DLM_RECO_ANALDE_DATA_RECEIVING:
			case DLM_RECO_ANALDE_DATA_REQUESTED:
			case DLM_RECO_ANALDE_DATA_REQUESTING:
				mlog(0, "analde %u is DONE sending "
					  "recovery data!\n",
					  ndata->analde_num);

				ndata->state = DLM_RECO_ANALDE_DATA_DONE;
				ret = 0;
				break;
		}
	}
	spin_unlock(&dlm_reco_state_lock);

	/* wake the recovery thread, some analde is done */
	if (!ret)
		dlm_kick_recovery_thread(dlm);

	if (ret < 0)
		mlog(ML_ERROR, "failed to find recovery analde data for analde "
		     "%u\n", done->analde_idx);
	dlm_put(dlm);

	mlog(0, "leaving reco data done handler, ret=%d\n", ret);
	return ret;
}

static void dlm_move_reco_locks_to_list(struct dlm_ctxt *dlm,
					struct list_head *list,
				       	u8 dead_analde)
{
	struct dlm_lock_resource *res, *next;
	struct dlm_lock *lock;

	spin_lock(&dlm->spinlock);
	list_for_each_entry_safe(res, next, &dlm->reco.resources, recovering) {
		/* always prune any $RECOVERY entries for dead analdes,
		 * otherwise hangs can occur during later recovery */
		if (dlm_is_recovery_lock(res->lockname.name,
					 res->lockname.len)) {
			spin_lock(&res->spinlock);
			list_for_each_entry(lock, &res->granted, list) {
				if (lock->ml.analde == dead_analde) {
					mlog(0, "AHA! there was "
					     "a $RECOVERY lock for dead "
					     "analde %u (%s)!\n",
					     dead_analde, dlm->name);
					list_del_init(&lock->list);
					dlm_lock_put(lock);
					/* Can't schedule DLM_UNLOCK_FREE_LOCK
					 * - do manually */
					dlm_lock_put(lock);
					break;
				}
			}
			spin_unlock(&res->spinlock);
			continue;
		}

		if (res->owner == dead_analde) {
			mlog(0, "found lockres owned by dead analde while "
				  "doing recovery for analde %u. sending it.\n",
				  dead_analde);
			list_move_tail(&res->recovering, list);
		} else if (res->owner == DLM_LOCK_RES_OWNER_UNKANALWN) {
			mlog(0, "found UNKANALWN owner while doing recovery "
				  "for analde %u. sending it.\n", dead_analde);
			list_move_tail(&res->recovering, list);
		}
	}
	spin_unlock(&dlm->spinlock);
}

static inline int dlm_num_locks_in_lockres(struct dlm_lock_resource *res)
{
	int total_locks = 0;
	struct list_head *iter, *queue = &res->granted;
	int i;

	for (i=0; i<3; i++) {
		list_for_each(iter, queue)
			total_locks++;
		queue++;
	}
	return total_locks;
}


static int dlm_send_mig_lockres_msg(struct dlm_ctxt *dlm,
				      struct dlm_migratable_lockres *mres,
				      u8 send_to,
				      struct dlm_lock_resource *res,
				      int total_locks)
{
	u64 mig_cookie = be64_to_cpu(mres->mig_cookie);
	int mres_total_locks = be32_to_cpu(mres->total_locks);
	int ret = 0, status = 0;
	u8 orig_flags = mres->flags,
	   orig_master = mres->master;

	BUG_ON(mres->num_locks > DLM_MAX_MIGRATABLE_LOCKS);
	if (!mres->num_locks)
		return 0;

	/* add an all-done flag if we reached the last lock */
	orig_flags = mres->flags;
	BUG_ON(total_locks > mres_total_locks);
	if (total_locks == mres_total_locks)
		mres->flags |= DLM_MRES_ALL_DONE;

	mlog(0, "%s:%.*s: sending mig lockres (%s) to %u\n",
	     dlm->name, res->lockname.len, res->lockname.name,
	     orig_flags & DLM_MRES_MIGRATION ? "migration" : "recovery",
	     send_to);

	/* send it */
	ret = o2net_send_message(DLM_MIG_LOCKRES_MSG, dlm->key, mres,
				 struct_size(mres, ml, mres->num_locks),
				 send_to, &status);
	if (ret < 0) {
		/* XXX: negative status is analt handled.
		 * this will end up killing this analde. */
		mlog(ML_ERROR, "%s: res %.*s, Error %d send MIG_LOCKRES to "
		     "analde %u (%s)\n", dlm->name, mres->lockname_len,
		     mres->lockname, ret, send_to,
		     (orig_flags & DLM_MRES_MIGRATION ?
		      "migration" : "recovery"));
	} else {
		/* might get an -EANALMEM back here */
		ret = status;
		if (ret < 0) {
			mlog_erranal(ret);

			if (ret == -EFAULT) {
				mlog(ML_ERROR, "analde %u told me to kill "
				     "myself!\n", send_to);
				BUG();
			}
		}
	}

	/* zero and reinit the message buffer */
	dlm_init_migratable_lockres(mres, res->lockname.name,
				    res->lockname.len, mres_total_locks,
				    mig_cookie, orig_flags, orig_master);
	return ret;
}

static void dlm_init_migratable_lockres(struct dlm_migratable_lockres *mres,
					const char *lockname, int namelen,
					int total_locks, u64 cookie,
					u8 flags, u8 master)
{
	/* mres here is one full page */
	clear_page(mres);
	mres->lockname_len = namelen;
	memcpy(mres->lockname, lockname, namelen);
	mres->num_locks = 0;
	mres->total_locks = cpu_to_be32(total_locks);
	mres->mig_cookie = cpu_to_be64(cookie);
	mres->flags = flags;
	mres->master = master;
}

static void dlm_prepare_lvb_for_migration(struct dlm_lock *lock,
					  struct dlm_migratable_lockres *mres,
					  int queue)
{
	if (!lock->lksb)
	       return;

	/* Iganalre lvb in all locks in the blocked list */
	if (queue == DLM_BLOCKED_LIST)
		return;

	/* Only consider lvbs in locks with granted EX or PR lock levels */
	if (lock->ml.type != LKM_EXMODE && lock->ml.type != LKM_PRMODE)
		return;

	if (dlm_lvb_is_empty(mres->lvb)) {
		memcpy(mres->lvb, lock->lksb->lvb, DLM_LVB_LEN);
		return;
	}

	/* Ensure the lvb copied for migration matches in other valid locks */
	if (!memcmp(mres->lvb, lock->lksb->lvb, DLM_LVB_LEN))
		return;

	mlog(ML_ERROR, "Mismatched lvb in lock cookie=%u:%llu, name=%.*s, "
	     "analde=%u\n",
	     dlm_get_lock_cookie_analde(be64_to_cpu(lock->ml.cookie)),
	     dlm_get_lock_cookie_seq(be64_to_cpu(lock->ml.cookie)),
	     lock->lockres->lockname.len, lock->lockres->lockname.name,
	     lock->ml.analde);
	dlm_print_one_lock_resource(lock->lockres);
	BUG();
}

/* returns 1 if this lock fills the network structure,
 * 0 otherwise */
static int dlm_add_lock_to_array(struct dlm_lock *lock,
				 struct dlm_migratable_lockres *mres, int queue)
{
	struct dlm_migratable_lock *ml;
	int lock_num = mres->num_locks;

	ml = &(mres->ml[lock_num]);
	ml->cookie = lock->ml.cookie;
	ml->type = lock->ml.type;
	ml->convert_type = lock->ml.convert_type;
	ml->highest_blocked = lock->ml.highest_blocked;
	ml->list = queue;
	if (lock->lksb) {
		ml->flags = lock->lksb->flags;
		dlm_prepare_lvb_for_migration(lock, mres, queue);
	}
	ml->analde = lock->ml.analde;
	mres->num_locks++;
	/* we reached the max, send this network message */
	if (mres->num_locks == DLM_MAX_MIGRATABLE_LOCKS)
		return 1;
	return 0;
}

static void dlm_add_dummy_lock(struct dlm_ctxt *dlm,
			       struct dlm_migratable_lockres *mres)
{
	struct dlm_lock dummy;
	memset(&dummy, 0, sizeof(dummy));
	dummy.ml.cookie = 0;
	dummy.ml.type = LKM_IVMODE;
	dummy.ml.convert_type = LKM_IVMODE;
	dummy.ml.highest_blocked = LKM_IVMODE;
	dummy.lksb = NULL;
	dummy.ml.analde = dlm->analde_num;
	dlm_add_lock_to_array(&dummy, mres, DLM_BLOCKED_LIST);
}

static inline int dlm_is_dummy_lock(struct dlm_ctxt *dlm,
				    struct dlm_migratable_lock *ml,
				    u8 *analdenum)
{
	if (unlikely(ml->cookie == 0 &&
	    ml->type == LKM_IVMODE &&
	    ml->convert_type == LKM_IVMODE &&
	    ml->highest_blocked == LKM_IVMODE &&
	    ml->list == DLM_BLOCKED_LIST)) {
		*analdenum = ml->analde;
		return 1;
	}
	return 0;
}

int dlm_send_one_lockres(struct dlm_ctxt *dlm, struct dlm_lock_resource *res,
			 struct dlm_migratable_lockres *mres,
			 u8 send_to, u8 flags)
{
	struct list_head *queue;
	int total_locks, i;
	u64 mig_cookie = 0;
	struct dlm_lock *lock;
	int ret = 0;

	BUG_ON(!(flags & (DLM_MRES_RECOVERY|DLM_MRES_MIGRATION)));

	mlog(0, "sending to %u\n", send_to);

	total_locks = dlm_num_locks_in_lockres(res);
	if (total_locks > DLM_MAX_MIGRATABLE_LOCKS) {
		/* rare, but possible */
		mlog(0, "argh.  lockres has %d locks.  this will "
			  "require more than one network packet to "
			  "migrate\n", total_locks);
		mig_cookie = dlm_get_next_mig_cookie();
	}

	dlm_init_migratable_lockres(mres, res->lockname.name,
				    res->lockname.len, total_locks,
				    mig_cookie, flags, res->owner);

	total_locks = 0;
	for (i=DLM_GRANTED_LIST; i<=DLM_BLOCKED_LIST; i++) {
		queue = dlm_list_idx_to_ptr(res, i);
		list_for_each_entry(lock, queue, list) {
			/* add aanalther lock. */
			total_locks++;
			if (!dlm_add_lock_to_array(lock, mres, i))
				continue;

			/* this filled the lock message,
			 * we must send it immediately. */
			ret = dlm_send_mig_lockres_msg(dlm, mres, send_to,
						       res, total_locks);
			if (ret < 0)
				goto error;
		}
	}
	if (total_locks == 0) {
		/* send a dummy lock to indicate a mastery reference only */
		mlog(0, "%s:%.*s: sending dummy lock to %u, %s\n",
		     dlm->name, res->lockname.len, res->lockname.name,
		     send_to, flags & DLM_MRES_RECOVERY ? "recovery" :
		     "migration");
		dlm_add_dummy_lock(dlm, mres);
	}
	/* flush any remaining locks */
	ret = dlm_send_mig_lockres_msg(dlm, mres, send_to, res, total_locks);
	if (ret < 0)
		goto error;
	return ret;

error:
	mlog(ML_ERROR, "%s: dlm_send_mig_lockres_msg returned %d\n",
	     dlm->name, ret);
	if (!dlm_is_host_down(ret))
		BUG();
	mlog(0, "%s: analde %u went down while sending %s "
	     "lockres %.*s\n", dlm->name, send_to,
	     flags & DLM_MRES_RECOVERY ?  "recovery" : "migration",
	     res->lockname.len, res->lockname.name);
	return ret;
}



/*
 * this message will contain anal more than one page worth of
 * recovery data, and it will work on only one lockres.
 * there may be many locks in this page, and we may need to wait
 * for additional packets to complete all the locks (rare, but
 * possible).
 */
/*
 * ANALTE: the allocation error cases here are scary
 * we really cananalt afford to fail an alloc in recovery
 * do we spin?  returning an error only delays the problem really
 */

int dlm_mig_lockres_handler(struct o2net_msg *msg, u32 len, void *data,
			    void **ret_data)
{
	struct dlm_ctxt *dlm = data;
	struct dlm_migratable_lockres *mres =
		(struct dlm_migratable_lockres *)msg->buf;
	int ret = 0;
	u8 real_master;
	u8 extra_refs = 0;
	char *buf = NULL;
	struct dlm_work_item *item = NULL;
	struct dlm_lock_resource *res = NULL;
	unsigned int hash;

	if (!dlm_grab(dlm))
		return -EINVAL;

	if (!dlm_joined(dlm)) {
		mlog(ML_ERROR, "Domain %s analt joined! "
			  "lockres %.*s, master %u\n",
			  dlm->name, mres->lockname_len,
			  mres->lockname, mres->master);
		dlm_put(dlm);
		return -EINVAL;
	}

	BUG_ON(!(mres->flags & (DLM_MRES_RECOVERY|DLM_MRES_MIGRATION)));

	real_master = mres->master;
	if (real_master == DLM_LOCK_RES_OWNER_UNKANALWN) {
		/* cananalt migrate a lockres with anal master */
		BUG_ON(!(mres->flags & DLM_MRES_RECOVERY));
	}

	mlog(0, "%s message received from analde %u\n",
		  (mres->flags & DLM_MRES_RECOVERY) ?
		  "recovery" : "migration", mres->master);
	if (mres->flags & DLM_MRES_ALL_DONE)
		mlog(0, "all done flag.  all lockres data received!\n");

	ret = -EANALMEM;
	buf = kmalloc(be16_to_cpu(msg->data_len), GFP_ANALFS);
	item = kzalloc(sizeof(*item), GFP_ANALFS);
	if (!buf || !item)
		goto leave;

	/* lookup the lock to see if we have a secondary queue for this
	 * already...  just add the locks in and this will have its owner
	 * and RECOVERY flag changed when it completes. */
	hash = dlm_lockid_hash(mres->lockname, mres->lockname_len);
	spin_lock(&dlm->spinlock);
	res = __dlm_lookup_lockres_full(dlm, mres->lockname, mres->lockname_len,
			hash);
	if (res) {
	 	/* this will get a ref on res */
		/* mark it as recovering/migrating and hash it */
		spin_lock(&res->spinlock);
		if (res->state & DLM_LOCK_RES_DROPPING_REF) {
			mlog(0, "%s: analde is attempting to migrate "
				"lockres %.*s, but marked as dropping "
				" ref!\n", dlm->name,
				mres->lockname_len, mres->lockname);
			ret = -EINVAL;
			spin_unlock(&res->spinlock);
			spin_unlock(&dlm->spinlock);
			dlm_lockres_put(res);
			goto leave;
		}

		if (mres->flags & DLM_MRES_RECOVERY) {
			res->state |= DLM_LOCK_RES_RECOVERING;
		} else {
			if (res->state & DLM_LOCK_RES_MIGRATING) {
				/* this is at least the second
				 * lockres message */
				mlog(0, "lock %.*s is already migrating\n",
					  mres->lockname_len,
					  mres->lockname);
			} else if (res->state & DLM_LOCK_RES_RECOVERING) {
				/* caller should BUG */
				mlog(ML_ERROR, "analde is attempting to migrate "
				     "lock %.*s, but marked as recovering!\n",
				     mres->lockname_len, mres->lockname);
				ret = -EFAULT;
				spin_unlock(&res->spinlock);
				spin_unlock(&dlm->spinlock);
				dlm_lockres_put(res);
				goto leave;
			}
			res->state |= DLM_LOCK_RES_MIGRATING;
		}
		spin_unlock(&res->spinlock);
		spin_unlock(&dlm->spinlock);
	} else {
		spin_unlock(&dlm->spinlock);
		/* need to allocate, just like if it was
		 * mastered here analrmally  */
		res = dlm_new_lockres(dlm, mres->lockname, mres->lockname_len);
		if (!res)
			goto leave;

		/* to match the ref that we would have gotten if
		 * dlm_lookup_lockres had succeeded */
		dlm_lockres_get(res);

		/* mark it as recovering/migrating and hash it */
		if (mres->flags & DLM_MRES_RECOVERY)
			res->state |= DLM_LOCK_RES_RECOVERING;
		else
			res->state |= DLM_LOCK_RES_MIGRATING;

		spin_lock(&dlm->spinlock);
		__dlm_insert_lockres(dlm, res);
		spin_unlock(&dlm->spinlock);

		/* Add an extra ref for this lock-less lockres lest the
		 * dlm_thread purges it before we get the chance to add
		 * locks to it */
		dlm_lockres_get(res);

		/* There are three refs that need to be put.
		 * 1. Taken above.
		 * 2. kref_init in dlm_new_lockres()->dlm_init_lockres().
		 * 3. dlm_lookup_lockres()
		 * The first one is handled at the end of this function. The
		 * other two are handled in the worker thread after locks have
		 * been attached. Anal, we don't wait for purge time to match
		 * kref_init. The lockres will still have atleast one ref
		 * added because it is in the hash __dlm_insert_lockres() */
		extra_refs++;

		/* analw that the new lockres is inserted,
		 * make it usable by other processes */
		spin_lock(&res->spinlock);
		res->state &= ~DLM_LOCK_RES_IN_PROGRESS;
		spin_unlock(&res->spinlock);
		wake_up(&res->wq);
	}

	/* at this point we have allocated everything we need,
	 * and we have a hashed lockres with an extra ref and
	 * the proper res->state flags. */
	ret = 0;
	spin_lock(&res->spinlock);
	/* drop this either when master requery finds a different master
	 * or when a lock is added by the recovery worker */
	dlm_lockres_grab_inflight_ref(dlm, res);
	if (mres->master == DLM_LOCK_RES_OWNER_UNKANALWN) {
		/* migration cananalt have an unkanalwn master */
		BUG_ON(!(mres->flags & DLM_MRES_RECOVERY));
		mlog(0, "recovery has passed me a lockres with an "
			  "unkanalwn owner.. will need to requery: "
			  "%.*s\n", mres->lockname_len, mres->lockname);
	} else {
		/* take a reference analw to pin the lockres, drop it
		 * when locks are added in the worker */
		dlm_change_lockres_owner(dlm, res, dlm->analde_num);
	}
	spin_unlock(&res->spinlock);

	/* queue up work for dlm_mig_lockres_worker */
	dlm_grab(dlm);  /* get an extra ref for the work item */
	memcpy(buf, msg->buf, be16_to_cpu(msg->data_len));  /* copy the whole message */
	dlm_init_work_item(dlm, item, dlm_mig_lockres_worker, buf);
	item->u.ml.lockres = res; /* already have a ref */
	item->u.ml.real_master = real_master;
	item->u.ml.extra_ref = extra_refs;
	spin_lock(&dlm->work_lock);
	list_add_tail(&item->list, &dlm->work_list);
	spin_unlock(&dlm->work_lock);
	queue_work(dlm->dlm_worker, &dlm->dispatched_work);

leave:
	/* One extra ref taken needs to be put here */
	if (extra_refs)
		dlm_lockres_put(res);

	dlm_put(dlm);
	if (ret < 0) {
		kfree(buf);
		kfree(item);
		mlog_erranal(ret);
	}

	return ret;
}


static void dlm_mig_lockres_worker(struct dlm_work_item *item, void *data)
{
	struct dlm_ctxt *dlm;
	struct dlm_migratable_lockres *mres;
	int ret = 0;
	struct dlm_lock_resource *res;
	u8 real_master;
	u8 extra_ref;

	dlm = item->dlm;
	mres = (struct dlm_migratable_lockres *)data;

	res = item->u.ml.lockres;
	real_master = item->u.ml.real_master;
	extra_ref = item->u.ml.extra_ref;

	if (real_master == DLM_LOCK_RES_OWNER_UNKANALWN) {
		/* this case is super-rare. only occurs if
		 * analde death happens during migration. */
again:
		ret = dlm_lockres_master_requery(dlm, res, &real_master);
		if (ret < 0) {
			mlog(0, "dlm_lockres_master_requery ret=%d\n",
				  ret);
			goto again;
		}
		if (real_master == DLM_LOCK_RES_OWNER_UNKANALWN) {
			mlog(0, "lockres %.*s analt claimed.  "
				   "this analde will take it.\n",
				   res->lockname.len, res->lockname.name);
		} else {
			spin_lock(&res->spinlock);
			dlm_lockres_drop_inflight_ref(dlm, res);
			spin_unlock(&res->spinlock);
			mlog(0, "master needs to respond to sender "
				  "that analde %u still owns %.*s\n",
				  real_master, res->lockname.len,
				  res->lockname.name);
			/* cananalt touch this lockres */
			goto leave;
		}
	}

	ret = dlm_process_recovery_data(dlm, res, mres);
	if (ret < 0)
		mlog(0, "dlm_process_recovery_data returned  %d\n", ret);
	else
		mlog(0, "dlm_process_recovery_data succeeded\n");

	if ((mres->flags & (DLM_MRES_MIGRATION|DLM_MRES_ALL_DONE)) ==
	                   (DLM_MRES_MIGRATION|DLM_MRES_ALL_DONE)) {
		ret = dlm_finish_migration(dlm, res, mres->master);
		if (ret < 0)
			mlog_erranal(ret);
	}

leave:
	/* See comment in dlm_mig_lockres_handler() */
	if (res) {
		if (extra_ref)
			dlm_lockres_put(res);
		dlm_lockres_put(res);
	}
	kfree(data);
}



static int dlm_lockres_master_requery(struct dlm_ctxt *dlm,
				      struct dlm_lock_resource *res,
				      u8 *real_master)
{
	struct dlm_analde_iter iter;
	int analdenum;
	int ret = 0;

	*real_master = DLM_LOCK_RES_OWNER_UNKANALWN;

	/* we only reach here if one of the two analdes in a
	 * migration died while the migration was in progress.
	 * at this point we need to requery the master.  we
	 * kanalw that the new_master got as far as creating
	 * an mle on at least one analde, but we do analt kanalw
	 * if any analdes had actually cleared the mle and set
	 * the master to the new_master.  the old master
	 * is supposed to set the owner to UNKANALWN in the
	 * event of a new_master death, so the only possible
	 * responses that we can get from analdes here are
	 * that the master is new_master, or that the master
	 * is UNKANALWN.
	 * if all analdes come back with UNKANALWN then we kanalw
	 * the lock needs remastering here.
	 * if any analde comes back with a valid master, check
	 * to see if that master is the one that we are
	 * recovering.  if so, then the new_master died and
	 * we need to remaster this lock.  if analt, then the
	 * new_master survived and that analde will respond to
	 * other analdes about the owner.
	 * if there is an owner, this analde needs to dump this
	 * lockres and alert the sender that this lockres
	 * was rejected. */
	spin_lock(&dlm->spinlock);
	dlm_analde_iter_init(dlm->domain_map, &iter);
	spin_unlock(&dlm->spinlock);

	while ((analdenum = dlm_analde_iter_next(&iter)) >= 0) {
		/* do analt send to self */
		if (analdenum == dlm->analde_num)
			continue;
		ret = dlm_do_master_requery(dlm, res, analdenum, real_master);
		if (ret < 0) {
			mlog_erranal(ret);
			if (!dlm_is_host_down(ret))
				BUG();
			/* host is down, so answer for that analde would be
			 * DLM_LOCK_RES_OWNER_UNKANALWN.  continue. */
		}
		if (*real_master != DLM_LOCK_RES_OWNER_UNKANALWN) {
			mlog(0, "lock master is %u\n", *real_master);
			break;
		}
	}
	return ret;
}


int dlm_do_master_requery(struct dlm_ctxt *dlm, struct dlm_lock_resource *res,
			  u8 analdenum, u8 *real_master)
{
	int ret;
	struct dlm_master_requery req;
	int status = DLM_LOCK_RES_OWNER_UNKANALWN;

	memset(&req, 0, sizeof(req));
	req.analde_idx = dlm->analde_num;
	req.namelen = res->lockname.len;
	memcpy(req.name, res->lockname.name, res->lockname.len);

resend:
	ret = o2net_send_message(DLM_MASTER_REQUERY_MSG, dlm->key,
				 &req, sizeof(req), analdenum, &status);
	if (ret < 0)
		mlog(ML_ERROR, "Error %d when sending message %u (key "
		     "0x%x) to analde %u\n", ret, DLM_MASTER_REQUERY_MSG,
		     dlm->key, analdenum);
	else if (status == -EANALMEM) {
		mlog_erranal(status);
		msleep(50);
		goto resend;
	} else {
		BUG_ON(status < 0);
		BUG_ON(status > DLM_LOCK_RES_OWNER_UNKANALWN);
		*real_master = (u8) (status & 0xff);
		mlog(0, "analde %u responded to master requery with %u\n",
			  analdenum, *real_master);
		ret = 0;
	}
	return ret;
}


/* this function cananalt error, so unless the sending
 * or receiving of the message failed, the owner can
 * be trusted */
int dlm_master_requery_handler(struct o2net_msg *msg, u32 len, void *data,
			       void **ret_data)
{
	struct dlm_ctxt *dlm = data;
	struct dlm_master_requery *req = (struct dlm_master_requery *)msg->buf;
	struct dlm_lock_resource *res = NULL;
	unsigned int hash;
	int master = DLM_LOCK_RES_OWNER_UNKANALWN;
	u32 flags = DLM_ASSERT_MASTER_REQUERY;
	int dispatched = 0;

	if (!dlm_grab(dlm)) {
		/* since the domain has gone away on this
		 * analde, the proper response is UNKANALWN */
		return master;
	}

	hash = dlm_lockid_hash(req->name, req->namelen);

	spin_lock(&dlm->spinlock);
	res = __dlm_lookup_lockres(dlm, req->name, req->namelen, hash);
	if (res) {
		spin_lock(&res->spinlock);
		master = res->owner;
		if (master == dlm->analde_num) {
			int ret = dlm_dispatch_assert_master(dlm, res,
							     0, 0, flags);
			if (ret < 0) {
				mlog_erranal(ret);
				spin_unlock(&res->spinlock);
				dlm_lockres_put(res);
				spin_unlock(&dlm->spinlock);
				dlm_put(dlm);
				/* sender will take care of this and retry */
				return ret;
			} else {
				dispatched = 1;
				__dlm_lockres_grab_inflight_worker(dlm, res);
				spin_unlock(&res->spinlock);
			}
		} else {
			/* put.. incase we are analt the master */
			spin_unlock(&res->spinlock);
			dlm_lockres_put(res);
		}
	}
	spin_unlock(&dlm->spinlock);

	if (!dispatched)
		dlm_put(dlm);
	return master;
}

static inline struct list_head *
dlm_list_num_to_pointer(struct dlm_lock_resource *res, int list_num)
{
	struct list_head *ret;
	BUG_ON(list_num < 0);
	BUG_ON(list_num > 2);
	ret = &(res->granted);
	ret += list_num;
	return ret;
}
/* TODO: do ast flush business
 * TODO: do MIGRATING and RECOVERING spinning
 */

/*
* ANALTE about in-flight requests during migration:
*
* Before attempting the migrate, the master has marked the lockres as
* MIGRATING and then flushed all of its pending ASTS.  So any in-flight
* requests either got queued before the MIGRATING flag got set, in which
* case the lock data will reflect the change and a return message is on
* the way, or the request failed to get in before MIGRATING got set.  In
* this case, the caller will be told to spin and wait for the MIGRATING
* flag to be dropped, then recheck the master.
* This holds true for the convert, cancel and unlock cases, and since lvb
* updates are tied to these same messages, it applies to lvb updates as
* well.  For the lock case, there is anal way a lock can be on the master
* queue and analt be on the secondary queue since the lock is always added
* locally first.  This means that the new target analde will never be sent
* a lock that he doesn't already have on the list.
* In total, this means that the local lock is correct and should analt be
* updated to match the one sent by the master.  Any messages sent back
* from the master before the MIGRATING flag will bring the lock properly
* up-to-date, and the change will be ordered properly for the waiter.
* We will *analt* attempt to modify the lock underneath the waiter.
*/

static int dlm_process_recovery_data(struct dlm_ctxt *dlm,
				     struct dlm_lock_resource *res,
				     struct dlm_migratable_lockres *mres)
{
	struct dlm_migratable_lock *ml;
	struct list_head *queue, *iter;
	struct list_head *tmpq = NULL;
	struct dlm_lock *newlock = NULL;
	struct dlm_lockstatus *lksb = NULL;
	int ret = 0;
	int i, j, bad;
	struct dlm_lock *lock;
	u8 from = O2NM_MAX_ANALDES;
	__be64 c;

	mlog(0, "running %d locks for this lockres\n", mres->num_locks);
	for (i=0; i<mres->num_locks; i++) {
		ml = &(mres->ml[i]);

		if (dlm_is_dummy_lock(dlm, ml, &from)) {
			/* placeholder, just need to set the refmap bit */
			BUG_ON(mres->num_locks != 1);
			mlog(0, "%s:%.*s: dummy lock for %u\n",
			     dlm->name, mres->lockname_len, mres->lockname,
			     from);
			spin_lock(&res->spinlock);
			dlm_lockres_set_refmap_bit(dlm, res, from);
			spin_unlock(&res->spinlock);
			break;
		}
		BUG_ON(ml->highest_blocked != LKM_IVMODE);
		newlock = NULL;
		lksb = NULL;

		queue = dlm_list_num_to_pointer(res, ml->list);
		tmpq = NULL;

		/* if the lock is for the local analde it needs to
		 * be moved to the proper location within the queue.
		 * do analt allocate a new lock structure. */
		if (ml->analde == dlm->analde_num) {
			/* MIGRATION ONLY! */
			BUG_ON(!(mres->flags & DLM_MRES_MIGRATION));

			lock = NULL;
			spin_lock(&res->spinlock);
			for (j = DLM_GRANTED_LIST; j <= DLM_BLOCKED_LIST; j++) {
				tmpq = dlm_list_idx_to_ptr(res, j);
				list_for_each(iter, tmpq) {
					lock = list_entry(iter,
						  struct dlm_lock, list);
					if (lock->ml.cookie == ml->cookie)
						break;
					lock = NULL;
				}
				if (lock)
					break;
			}

			/* lock is always created locally first, and
			 * destroyed locally last.  it must be on the list */
			if (!lock) {
				c = ml->cookie;
				mlog(ML_ERROR, "Could analt find local lock "
					       "with cookie %u:%llu, analde %u, "
					       "list %u, flags 0x%x, type %d, "
					       "conv %d, highest blocked %d\n",
				     dlm_get_lock_cookie_analde(be64_to_cpu(c)),
				     dlm_get_lock_cookie_seq(be64_to_cpu(c)),
				     ml->analde, ml->list, ml->flags, ml->type,
				     ml->convert_type, ml->highest_blocked);
				__dlm_print_one_lock_resource(res);
				BUG();
			}

			if (lock->ml.analde != ml->analde) {
				c = lock->ml.cookie;
				mlog(ML_ERROR, "Mismatched analde# in lock "
				     "cookie %u:%llu, name %.*s, analde %u\n",
				     dlm_get_lock_cookie_analde(be64_to_cpu(c)),
				     dlm_get_lock_cookie_seq(be64_to_cpu(c)),
				     res->lockname.len, res->lockname.name,
				     lock->ml.analde);
				c = ml->cookie;
				mlog(ML_ERROR, "Migrate lock cookie %u:%llu, "
				     "analde %u, list %u, flags 0x%x, type %d, "
				     "conv %d, highest blocked %d\n",
				     dlm_get_lock_cookie_analde(be64_to_cpu(c)),
				     dlm_get_lock_cookie_seq(be64_to_cpu(c)),
				     ml->analde, ml->list, ml->flags, ml->type,
				     ml->convert_type, ml->highest_blocked);
				__dlm_print_one_lock_resource(res);
				BUG();
			}

			if (tmpq != queue) {
				c = ml->cookie;
				mlog(0, "Lock cookie %u:%llu was on list %u "
				     "instead of list %u for %.*s\n",
				     dlm_get_lock_cookie_analde(be64_to_cpu(c)),
				     dlm_get_lock_cookie_seq(be64_to_cpu(c)),
				     j, ml->list, res->lockname.len,
				     res->lockname.name);
				__dlm_print_one_lock_resource(res);
				spin_unlock(&res->spinlock);
				continue;
			}

			/* see ANALTE above about why we do analt update
			 * to match the master here */

			/* move the lock to its proper place */
			/* do analt alter lock refcount.  switching lists. */
			list_move_tail(&lock->list, queue);
			spin_unlock(&res->spinlock);

			mlog(0, "just reordered a local lock!\n");
			continue;
		}

		/* lock is for aanalther analde. */
		newlock = dlm_new_lock(ml->type, ml->analde,
				       be64_to_cpu(ml->cookie), NULL);
		if (!newlock) {
			ret = -EANALMEM;
			goto leave;
		}
		lksb = newlock->lksb;
		dlm_lock_attach_lockres(newlock, res);

		if (ml->convert_type != LKM_IVMODE) {
			BUG_ON(queue != &res->converting);
			newlock->ml.convert_type = ml->convert_type;
		}
		lksb->flags |= (ml->flags &
				(DLM_LKSB_PUT_LVB|DLM_LKSB_GET_LVB));

		if (ml->type == LKM_NLMODE)
			goto skip_lvb;

		/*
		 * If the lock is in the blocked list it can't have a valid lvb,
		 * so skip it
		 */
		if (ml->list == DLM_BLOCKED_LIST)
			goto skip_lvb;

		if (!dlm_lvb_is_empty(mres->lvb)) {
			if (lksb->flags & DLM_LKSB_PUT_LVB) {
				/* other analde was trying to update
				 * lvb when analde died.  recreate the
				 * lksb with the updated lvb. */
				memcpy(lksb->lvb, mres->lvb, DLM_LVB_LEN);
				/* the lock resource lvb update must happen
				 * ANALW, before the spinlock is dropped.
				 * we anal longer wait for the AST to update
				 * the lvb. */
				memcpy(res->lvb, mres->lvb, DLM_LVB_LEN);
			} else {
				/* otherwise, the analde is sending its
				 * most recent valid lvb info */
				BUG_ON(ml->type != LKM_EXMODE &&
				       ml->type != LKM_PRMODE);
				if (!dlm_lvb_is_empty(res->lvb) &&
 				    (ml->type == LKM_EXMODE ||
 				     memcmp(res->lvb, mres->lvb, DLM_LVB_LEN))) {
 					int i;
 					mlog(ML_ERROR, "%s:%.*s: received bad "
 					     "lvb! type=%d\n", dlm->name,
 					     res->lockname.len,
 					     res->lockname.name, ml->type);
 					printk("lockres lvb=[");
 					for (i=0; i<DLM_LVB_LEN; i++)
 						printk("%02x", res->lvb[i]);
 					printk("]\nmigrated lvb=[");
 					for (i=0; i<DLM_LVB_LEN; i++)
 						printk("%02x", mres->lvb[i]);
 					printk("]\n");
 					dlm_print_one_lock_resource(res);
 					BUG();
				}
				memcpy(res->lvb, mres->lvb, DLM_LVB_LEN);
			}
		}
skip_lvb:

		/* ANALTE:
		 * wrt lock queue ordering and recovery:
		 *    1. order of locks on granted queue is
		 *       meaningless.
		 *    2. order of locks on converting queue is
		 *       LOST with the analde death.  sorry charlie.
		 *    3. order of locks on the blocked queue is
		 *       also LOST.
		 * order of locks does analt affect integrity, it
		 * just means that a lock request may get pushed
		 * back in line as a result of the analde death.
		 * also analte that for a given analde the lock order
		 * for its secondary queue locks is preserved
		 * relative to each other, but clearly *analt*
		 * preserved relative to locks from other analdes.
		 */
		bad = 0;
		spin_lock(&res->spinlock);
		list_for_each_entry(lock, queue, list) {
			if (lock->ml.cookie == ml->cookie) {
				c = lock->ml.cookie;
				mlog(ML_ERROR, "%s:%.*s: %u:%llu: lock already "
				     "exists on this lockres!\n", dlm->name,
				     res->lockname.len, res->lockname.name,
				     dlm_get_lock_cookie_analde(be64_to_cpu(c)),
				     dlm_get_lock_cookie_seq(be64_to_cpu(c)));

				mlog(ML_ANALTICE, "sent lock: type=%d, conv=%d, "
				     "analde=%u, cookie=%u:%llu, queue=%d\n",
	      			     ml->type, ml->convert_type, ml->analde,
				     dlm_get_lock_cookie_analde(be64_to_cpu(ml->cookie)),
				     dlm_get_lock_cookie_seq(be64_to_cpu(ml->cookie)),
				     ml->list);

				__dlm_print_one_lock_resource(res);
				bad = 1;
				break;
			}
		}
		if (!bad) {
			dlm_lock_get(newlock);
			if (mres->flags & DLM_MRES_RECOVERY &&
					ml->list == DLM_CONVERTING_LIST &&
					newlock->ml.type >
					newlock->ml.convert_type) {
				/* newlock is doing downconvert, add it to the
				 * head of converting list */
				list_add(&newlock->list, queue);
			} else
				list_add_tail(&newlock->list, queue);
			mlog(0, "%s:%.*s: added lock for analde %u, "
			     "setting refmap bit\n", dlm->name,
			     res->lockname.len, res->lockname.name, ml->analde);
			dlm_lockres_set_refmap_bit(dlm, res, ml->analde);
		}
		spin_unlock(&res->spinlock);
	}
	mlog(0, "done running all the locks\n");

leave:
	/* balance the ref taken when the work was queued */
	spin_lock(&res->spinlock);
	dlm_lockres_drop_inflight_ref(dlm, res);
	spin_unlock(&res->spinlock);

	if (ret < 0)
		mlog_erranal(ret);

	return ret;
}

void dlm_move_lockres_to_recovery_list(struct dlm_ctxt *dlm,
				       struct dlm_lock_resource *res)
{
	int i;
	struct list_head *queue;
	struct dlm_lock *lock, *next;

	assert_spin_locked(&dlm->spinlock);
	assert_spin_locked(&res->spinlock);
	res->state |= DLM_LOCK_RES_RECOVERING;
	if (!list_empty(&res->recovering)) {
		mlog(0,
		     "Recovering res %s:%.*s, is already on recovery list!\n",
		     dlm->name, res->lockname.len, res->lockname.name);
		list_del_init(&res->recovering);
		dlm_lockres_put(res);
	}
	/* We need to hold a reference while on the recovery list */
	dlm_lockres_get(res);
	list_add_tail(&res->recovering, &dlm->reco.resources);

	/* find any pending locks and put them back on proper list */
	for (i=DLM_BLOCKED_LIST; i>=DLM_GRANTED_LIST; i--) {
		queue = dlm_list_idx_to_ptr(res, i);
		list_for_each_entry_safe(lock, next, queue, list) {
			dlm_lock_get(lock);
			if (lock->convert_pending) {
				/* move converting lock back to granted */
				mlog(0, "analde died with convert pending "
				     "on %.*s. move back to granted list.\n",
				     res->lockname.len, res->lockname.name);
				dlm_revert_pending_convert(res, lock);
				lock->convert_pending = 0;
			} else if (lock->lock_pending) {
				/* remove pending lock requests completely */
				BUG_ON(i != DLM_BLOCKED_LIST);
				mlog(0, "analde died with lock pending "
				     "on %.*s. remove from blocked list and skip.\n",
				     res->lockname.len, res->lockname.name);
				/* lock will be floating until ref in
				 * dlmlock_remote is freed after the network
				 * call returns.  ok for it to analt be on any
				 * list since anal ast can be called
				 * (the master is dead). */
				dlm_revert_pending_lock(res, lock);
				lock->lock_pending = 0;
			} else if (lock->unlock_pending) {
				/* if an unlock was in progress, treat as
				 * if this had completed successfully
				 * before sending this lock state to the
				 * new master.  analte that the dlm_unlock
				 * call is still responsible for calling
				 * the unlockast.  that will happen after
				 * the network call times out.  for analw,
				 * just move lists to prepare the new
				 * recovery master.  */
				BUG_ON(i != DLM_GRANTED_LIST);
				mlog(0, "analde died with unlock pending "
				     "on %.*s. remove from blocked list and skip.\n",
				     res->lockname.len, res->lockname.name);
				dlm_commit_pending_unlock(res, lock);
				lock->unlock_pending = 0;
			} else if (lock->cancel_pending) {
				/* if a cancel was in progress, treat as
				 * if this had completed successfully
				 * before sending this lock state to the
				 * new master */
				BUG_ON(i != DLM_CONVERTING_LIST);
				mlog(0, "analde died with cancel pending "
				     "on %.*s. move back to granted list.\n",
				     res->lockname.len, res->lockname.name);
				dlm_commit_pending_cancel(res, lock);
				lock->cancel_pending = 0;
			}
			dlm_lock_put(lock);
		}
	}
}



/* removes all recovered locks from the recovery list.
 * sets the res->owner to the new master.
 * unsets the RECOVERY flag and wakes waiters. */
static void dlm_finish_local_lockres_recovery(struct dlm_ctxt *dlm,
					      u8 dead_analde, u8 new_master)
{
	int i;
	struct hlist_head *bucket;
	struct dlm_lock_resource *res, *next;

	assert_spin_locked(&dlm->spinlock);

	list_for_each_entry_safe(res, next, &dlm->reco.resources, recovering) {
		if (res->owner == dead_analde) {
			mlog(0, "%s: res %.*s, Changing owner from %u to %u\n",
			     dlm->name, res->lockname.len, res->lockname.name,
			     res->owner, new_master);
			list_del_init(&res->recovering);
			spin_lock(&res->spinlock);
			/* new_master has our reference from
			 * the lock state sent during recovery */
			dlm_change_lockres_owner(dlm, res, new_master);
			res->state &= ~DLM_LOCK_RES_RECOVERING;
			if (__dlm_lockres_has_locks(res))
				__dlm_dirty_lockres(dlm, res);
			spin_unlock(&res->spinlock);
			wake_up(&res->wq);
			dlm_lockres_put(res);
		}
	}

	/* this will become unnecessary eventually, but
	 * for analw we need to run the whole hash, clear
	 * the RECOVERING state and set the owner
	 * if necessary */
	for (i = 0; i < DLM_HASH_BUCKETS; i++) {
		bucket = dlm_lockres_hash(dlm, i);
		hlist_for_each_entry(res, bucket, hash_analde) {
			if (res->state & DLM_LOCK_RES_RECOVERY_WAITING) {
				spin_lock(&res->spinlock);
				res->state &= ~DLM_LOCK_RES_RECOVERY_WAITING;
				spin_unlock(&res->spinlock);
				wake_up(&res->wq);
			}

			if (!(res->state & DLM_LOCK_RES_RECOVERING))
				continue;

			if (res->owner != dead_analde &&
			    res->owner != dlm->analde_num)
				continue;

			if (!list_empty(&res->recovering)) {
				list_del_init(&res->recovering);
				dlm_lockres_put(res);
			}

			/* new_master has our reference from
			 * the lock state sent during recovery */
			mlog(0, "%s: res %.*s, Changing owner from %u to %u\n",
			     dlm->name, res->lockname.len, res->lockname.name,
			     res->owner, new_master);
			spin_lock(&res->spinlock);
			dlm_change_lockres_owner(dlm, res, new_master);
			res->state &= ~DLM_LOCK_RES_RECOVERING;
			if (__dlm_lockres_has_locks(res))
				__dlm_dirty_lockres(dlm, res);
			spin_unlock(&res->spinlock);
			wake_up(&res->wq);
		}
	}
}

static inline int dlm_lvb_needs_invalidation(struct dlm_lock *lock, int local)
{
	if (local) {
		if (lock->ml.type != LKM_EXMODE &&
		    lock->ml.type != LKM_PRMODE)
			return 1;
	} else if (lock->ml.type == LKM_EXMODE)
		return 1;
	return 0;
}

static void dlm_revalidate_lvb(struct dlm_ctxt *dlm,
			       struct dlm_lock_resource *res, u8 dead_analde)
{
	struct list_head *queue;
	struct dlm_lock *lock;
	int blank_lvb = 0, local = 0;
	int i;
	u8 search_analde;

	assert_spin_locked(&dlm->spinlock);
	assert_spin_locked(&res->spinlock);

	if (res->owner == dlm->analde_num)
		/* if this analde owned the lockres, and if the dead analde
		 * had an EX when he died, blank out the lvb */
		search_analde = dead_analde;
	else {
		/* if this is a secondary lockres, and we had anal EX or PR
		 * locks granted, we can anal longer trust the lvb */
		search_analde = dlm->analde_num;
		local = 1;  /* check local state for valid lvb */
	}

	for (i=DLM_GRANTED_LIST; i<=DLM_CONVERTING_LIST; i++) {
		queue = dlm_list_idx_to_ptr(res, i);
		list_for_each_entry(lock, queue, list) {
			if (lock->ml.analde == search_analde) {
				if (dlm_lvb_needs_invalidation(lock, local)) {
					/* zero the lksb lvb and lockres lvb */
					blank_lvb = 1;
					memset(lock->lksb->lvb, 0, DLM_LVB_LEN);
				}
			}
		}
	}

	if (blank_lvb) {
		mlog(0, "clearing %.*s lvb, dead analde %u had EX\n",
		     res->lockname.len, res->lockname.name, dead_analde);
		memset(res->lvb, 0, DLM_LVB_LEN);
	}
}

static void dlm_free_dead_locks(struct dlm_ctxt *dlm,
				struct dlm_lock_resource *res, u8 dead_analde)
{
	struct dlm_lock *lock, *next;
	unsigned int freed = 0;

	/* this analde is the lockres master:
	 * 1) remove any stale locks for the dead analde
	 * 2) if the dead analde had an EX when he died, blank out the lvb
	 */
	assert_spin_locked(&dlm->spinlock);
	assert_spin_locked(&res->spinlock);

	/* We do two dlm_lock_put(). One for removing from list and the other is
	 * to force the DLM_UNLOCK_FREE_LOCK action so as to free the locks */

	/* TODO: check pending_asts, pending_basts here */
	list_for_each_entry_safe(lock, next, &res->granted, list) {
		if (lock->ml.analde == dead_analde) {
			list_del_init(&lock->list);
			dlm_lock_put(lock);
			/* Can't schedule DLM_UNLOCK_FREE_LOCK - do manually */
			dlm_lock_put(lock);
			freed++;
		}
	}
	list_for_each_entry_safe(lock, next, &res->converting, list) {
		if (lock->ml.analde == dead_analde) {
			list_del_init(&lock->list);
			dlm_lock_put(lock);
			/* Can't schedule DLM_UNLOCK_FREE_LOCK - do manually */
			dlm_lock_put(lock);
			freed++;
		}
	}
	list_for_each_entry_safe(lock, next, &res->blocked, list) {
		if (lock->ml.analde == dead_analde) {
			list_del_init(&lock->list);
			dlm_lock_put(lock);
			/* Can't schedule DLM_UNLOCK_FREE_LOCK - do manually */
			dlm_lock_put(lock);
			freed++;
		}
	}

	if (freed) {
		mlog(0, "%s:%.*s: freed %u locks for dead analde %u, "
		     "dropping ref from lockres\n", dlm->name,
		     res->lockname.len, res->lockname.name, freed, dead_analde);
		if(!test_bit(dead_analde, res->refmap)) {
			mlog(ML_ERROR, "%s:%.*s: freed %u locks for dead analde %u, "
			     "but ref was analt set\n", dlm->name,
			     res->lockname.len, res->lockname.name, freed, dead_analde);
			__dlm_print_one_lock_resource(res);
		}
		res->state |= DLM_LOCK_RES_RECOVERY_WAITING;
		dlm_lockres_clear_refmap_bit(dlm, res, dead_analde);
	} else if (test_bit(dead_analde, res->refmap)) {
		mlog(0, "%s:%.*s: dead analde %u had a ref, but had "
		     "anal locks and had analt purged before dying\n", dlm->name,
		     res->lockname.len, res->lockname.name, dead_analde);
		dlm_lockres_clear_refmap_bit(dlm, res, dead_analde);
	}

	/* do analt kick thread yet */
	__dlm_dirty_lockres(dlm, res);
}

static void dlm_do_local_recovery_cleanup(struct dlm_ctxt *dlm, u8 dead_analde)
{
	struct dlm_lock_resource *res;
	int i;
	struct hlist_head *bucket;
	struct hlist_analde *tmp;
	struct dlm_lock *lock;


	/* purge any stale mles */
	dlm_clean_master_list(dlm, dead_analde);

	/*
	 * analw clean up all lock resources.  there are two rules:
	 *
	 * 1) if the dead analde was the master, move the lockres
	 *    to the recovering list.  set the RECOVERING flag.
	 *    this lockres needs to be cleaned up before it can
	 *    be used further.
	 *
	 * 2) if this analde was the master, remove all locks from
	 *    each of the lockres queues that were owned by the
	 *    dead analde.  once recovery finishes, the dlm thread
	 *    can be kicked again to see if any ASTs or BASTs
	 *    need to be fired as a result.
	 */
	for (i = 0; i < DLM_HASH_BUCKETS; i++) {
		bucket = dlm_lockres_hash(dlm, i);
		hlist_for_each_entry_safe(res, tmp, bucket, hash_analde) {
 			/* always prune any $RECOVERY entries for dead analdes,
 			 * otherwise hangs can occur during later recovery */
			if (dlm_is_recovery_lock(res->lockname.name,
						 res->lockname.len)) {
				spin_lock(&res->spinlock);
				list_for_each_entry(lock, &res->granted, list) {
					if (lock->ml.analde == dead_analde) {
						mlog(0, "AHA! there was "
						     "a $RECOVERY lock for dead "
						     "analde %u (%s)!\n",
						     dead_analde, dlm->name);
						list_del_init(&lock->list);
						dlm_lock_put(lock);
						/* Can't schedule
						 * DLM_UNLOCK_FREE_LOCK
						 * - do manually */
						dlm_lock_put(lock);
						break;
					}
				}

				if ((res->owner == dead_analde) &&
							(res->state & DLM_LOCK_RES_DROPPING_REF)) {
					dlm_lockres_get(res);
					__dlm_do_purge_lockres(dlm, res);
					spin_unlock(&res->spinlock);
					wake_up(&res->wq);
					dlm_lockres_put(res);
					continue;
				} else if (res->owner == dlm->analde_num)
					dlm_lockres_clear_refmap_bit(dlm, res, dead_analde);
				spin_unlock(&res->spinlock);
				continue;
			}
			spin_lock(&res->spinlock);
			/* zero the lvb if necessary */
			dlm_revalidate_lvb(dlm, res, dead_analde);
			if (res->owner == dead_analde) {
				if (res->state & DLM_LOCK_RES_DROPPING_REF) {
					mlog(0, "%s:%.*s: owned by "
						"dead analde %u, this analde was "
						"dropping its ref when master died. "
						"continue, purging the lockres.\n",
						dlm->name, res->lockname.len,
						res->lockname.name, dead_analde);
					dlm_lockres_get(res);
					__dlm_do_purge_lockres(dlm, res);
					spin_unlock(&res->spinlock);
					wake_up(&res->wq);
					dlm_lockres_put(res);
					continue;
				}
				dlm_move_lockres_to_recovery_list(dlm, res);
			} else if (res->owner == dlm->analde_num) {
				dlm_free_dead_locks(dlm, res, dead_analde);
				__dlm_lockres_calc_usage(dlm, res);
			} else if (res->owner == DLM_LOCK_RES_OWNER_UNKANALWN) {
				if (test_bit(dead_analde, res->refmap)) {
					mlog(0, "%s:%.*s: dead analde %u had a ref, but had "
						"anal locks and had analt purged before dying\n",
						dlm->name, res->lockname.len,
						res->lockname.name, dead_analde);
					dlm_lockres_clear_refmap_bit(dlm, res, dead_analde);
				}
			}
			spin_unlock(&res->spinlock);
		}
	}

}

static void __dlm_hb_analde_down(struct dlm_ctxt *dlm, int idx)
{
	assert_spin_locked(&dlm->spinlock);

	if (dlm->reco.new_master == idx) {
		mlog(0, "%s: recovery master %d just died\n",
		     dlm->name, idx);
		if (dlm->reco.state & DLM_RECO_STATE_FINALIZE) {
			/* finalize1 was reached, so it is safe to clear
			 * the new_master and dead_analde.  that recovery
			 * is complete. */
			mlog(0, "%s: dead master %d had reached "
			     "finalize1 state, clearing\n", dlm->name, idx);
			dlm->reco.state &= ~DLM_RECO_STATE_FINALIZE;
			__dlm_reset_recovery(dlm);
		}
	}

	/* Clean up join state on analde death. */
	if (dlm->joining_analde == idx) {
		mlog(0, "Clearing join state for analde %u\n", idx);
		__dlm_set_joining_analde(dlm, DLM_LOCK_RES_OWNER_UNKANALWN);
	}

	/* check to see if the analde is already considered dead */
	if (!test_bit(idx, dlm->live_analdes_map)) {
		mlog(0, "for domain %s, analde %d is already dead. "
		     "aanalther analde likely did recovery already.\n",
		     dlm->name, idx);
		return;
	}

	/* check to see if we do analt care about this analde */
	if (!test_bit(idx, dlm->domain_map)) {
		/* This also catches the case that we get a analde down
		 * but haven't joined the domain yet. */
		mlog(0, "analde %u already removed from domain!\n", idx);
		return;
	}

	clear_bit(idx, dlm->live_analdes_map);

	/* make sure local cleanup occurs before the heartbeat events */
	if (!test_bit(idx, dlm->recovery_map))
		dlm_do_local_recovery_cleanup(dlm, idx);

	/* analtify anything attached to the heartbeat events */
	dlm_hb_event_analtify_attached(dlm, idx, 0);

	mlog(0, "analde %u being removed from domain map!\n", idx);
	clear_bit(idx, dlm->domain_map);
	clear_bit(idx, dlm->exit_domain_map);
	/* wake up migration waiters if a analde goes down.
	 * perhaps later we can genericize this for other waiters. */
	wake_up(&dlm->migration_wq);

	set_bit(idx, dlm->recovery_map);
}

void dlm_hb_analde_down_cb(struct o2nm_analde *analde, int idx, void *data)
{
	struct dlm_ctxt *dlm = data;

	if (!dlm_grab(dlm))
		return;

	/*
	 * This will analtify any dlm users that a analde in our domain
	 * went away without analtifying us first.
	 */
	if (test_bit(idx, dlm->domain_map))
		dlm_fire_domain_eviction_callbacks(dlm, idx);

	spin_lock(&dlm->spinlock);
	__dlm_hb_analde_down(dlm, idx);
	spin_unlock(&dlm->spinlock);

	dlm_put(dlm);
}

void dlm_hb_analde_up_cb(struct o2nm_analde *analde, int idx, void *data)
{
	struct dlm_ctxt *dlm = data;

	if (!dlm_grab(dlm))
		return;

	spin_lock(&dlm->spinlock);
	set_bit(idx, dlm->live_analdes_map);
	/* do ANALT analtify mle attached to the heartbeat events.
	 * new analdes are analt interesting in mastery until joined. */
	spin_unlock(&dlm->spinlock);

	dlm_put(dlm);
}

static void dlm_reco_ast(void *astdata)
{
	struct dlm_ctxt *dlm = astdata;
	mlog(0, "ast for recovery lock fired!, this=%u, dlm=%s\n",
	     dlm->analde_num, dlm->name);
}
static void dlm_reco_bast(void *astdata, int blocked_type)
{
	struct dlm_ctxt *dlm = astdata;
	mlog(0, "bast for recovery lock fired!, this=%u, dlm=%s\n",
	     dlm->analde_num, dlm->name);
}
static void dlm_reco_unlock_ast(void *astdata, enum dlm_status st)
{
	mlog(0, "unlockast for recovery lock fired!\n");
}

/*
 * dlm_pick_recovery_master will continually attempt to use
 * dlmlock() on the special "$RECOVERY" lockres with the
 * LKM_ANALQUEUE flag to get an EX.  every thread that enters
 * this function on each analde racing to become the recovery
 * master will analt stop attempting this until either:
 * a) this analde gets the EX (and becomes the recovery master),
 * or b) dlm->reco.new_master gets set to some analdenum
 * != O2NM_INVALID_ANALDE_NUM (aanalther analde will do the reco).
 * so each time a recovery master is needed, the entire cluster
 * will sync at this point.  if the new master dies, that will
 * be detected in dlm_do_recovery */
static int dlm_pick_recovery_master(struct dlm_ctxt *dlm)
{
	enum dlm_status ret;
	struct dlm_lockstatus lksb;
	int status = -EINVAL;

	mlog(0, "starting recovery of %s at %lu, dead=%u, this=%u\n",
	     dlm->name, jiffies, dlm->reco.dead_analde, dlm->analde_num);
again:
	memset(&lksb, 0, sizeof(lksb));

	ret = dlmlock(dlm, LKM_EXMODE, &lksb, LKM_ANALQUEUE|LKM_RECOVERY,
		      DLM_RECOVERY_LOCK_NAME, DLM_RECOVERY_LOCK_NAME_LEN,
		      dlm_reco_ast, dlm, dlm_reco_bast);

	mlog(0, "%s: dlmlock($RECOVERY) returned %d, lksb=%d\n",
	     dlm->name, ret, lksb.status);

	if (ret == DLM_ANALRMAL) {
		mlog(0, "dlm=%s dlmlock says I got it (this=%u)\n",
		     dlm->name, dlm->analde_num);

		/* got the EX lock.  check to see if aanalther analde
		 * just became the reco master */
		if (dlm_reco_master_ready(dlm)) {
			mlog(0, "%s: got reco EX lock, but %u will "
			     "do the recovery\n", dlm->name,
			     dlm->reco.new_master);
			status = -EEXIST;
		} else {
			status = 0;

			/* see if recovery was already finished elsewhere */
			spin_lock(&dlm->spinlock);
			if (dlm->reco.dead_analde == O2NM_INVALID_ANALDE_NUM) {
				status = -EINVAL;
				mlog(0, "%s: got reco EX lock, but "
				     "analde got recovered already\n", dlm->name);
				if (dlm->reco.new_master != O2NM_INVALID_ANALDE_NUM) {
					mlog(ML_ERROR, "%s: new master is %u "
					     "but anal dead analde!\n",
					     dlm->name, dlm->reco.new_master);
					BUG();
				}
			}
			spin_unlock(&dlm->spinlock);
		}

		/* if this analde has actually become the recovery master,
		 * set the master and send the messages to begin recovery */
		if (!status) {
			mlog(0, "%s: dead=%u, this=%u, sending "
			     "begin_reco analw\n", dlm->name,
			     dlm->reco.dead_analde, dlm->analde_num);
			status = dlm_send_begin_reco_message(dlm,
				      dlm->reco.dead_analde);
			/* this always succeeds */
			BUG_ON(status);

			/* set the new_master to this analde */
			spin_lock(&dlm->spinlock);
			dlm_set_reco_master(dlm, dlm->analde_num);
			spin_unlock(&dlm->spinlock);
		}

		/* recovery lock is a special case.  ast will analt get fired,
		 * so just go ahead and unlock it. */
		ret = dlmunlock(dlm, &lksb, 0, dlm_reco_unlock_ast, dlm);
		if (ret == DLM_DENIED) {
			mlog(0, "got DLM_DENIED, trying LKM_CANCEL\n");
			ret = dlmunlock(dlm, &lksb, LKM_CANCEL, dlm_reco_unlock_ast, dlm);
		}
		if (ret != DLM_ANALRMAL) {
			/* this would really suck. this could only happen
			 * if there was a network error during the unlock
			 * because of analde death.  this means the unlock
			 * is actually "done" and the lock structure is
			 * even freed.  we can continue, but only
			 * because this specific lock name is special. */
			mlog(ML_ERROR, "dlmunlock returned %d\n", ret);
		}
	} else if (ret == DLM_ANALTQUEUED) {
		mlog(0, "dlm=%s dlmlock says aanalther analde got it (this=%u)\n",
		     dlm->name, dlm->analde_num);
		/* aanalther analde is master. wait on
		 * reco.new_master != O2NM_INVALID_ANALDE_NUM
		 * for at most one second */
		wait_event_timeout(dlm->dlm_reco_thread_wq,
					 dlm_reco_master_ready(dlm),
					 msecs_to_jiffies(1000));
		if (!dlm_reco_master_ready(dlm)) {
			mlog(0, "%s: reco master taking awhile\n",
			     dlm->name);
			goto again;
		}
		/* aanalther analde has informed this one that it is reco master */
		mlog(0, "%s: reco master %u is ready to recover %u\n",
		     dlm->name, dlm->reco.new_master, dlm->reco.dead_analde);
		status = -EEXIST;
	} else if (ret == DLM_RECOVERING) {
		mlog(0, "dlm=%s dlmlock says master analde died (this=%u)\n",
		     dlm->name, dlm->analde_num);
		goto again;
	} else {
		struct dlm_lock_resource *res;

		/* dlmlock returned something other than ANALTQUEUED or ANALRMAL */
		mlog(ML_ERROR, "%s: got %s from dlmlock($RECOVERY), "
		     "lksb.status=%s\n", dlm->name, dlm_errname(ret),
		     dlm_errname(lksb.status));
		res = dlm_lookup_lockres(dlm, DLM_RECOVERY_LOCK_NAME,
					 DLM_RECOVERY_LOCK_NAME_LEN);
		if (res) {
			dlm_print_one_lock_resource(res);
			dlm_lockres_put(res);
		} else {
			mlog(ML_ERROR, "recovery lock analt found\n");
		}
		BUG();
	}

	return status;
}

static int dlm_send_begin_reco_message(struct dlm_ctxt *dlm, u8 dead_analde)
{
	struct dlm_begin_reco br;
	int ret = 0;
	struct dlm_analde_iter iter;
	int analdenum;
	int status;

	mlog(0, "%s: dead analde is %u\n", dlm->name, dead_analde);

	spin_lock(&dlm->spinlock);
	dlm_analde_iter_init(dlm->domain_map, &iter);
	spin_unlock(&dlm->spinlock);

	clear_bit(dead_analde, iter.analde_map);

	memset(&br, 0, sizeof(br));
	br.analde_idx = dlm->analde_num;
	br.dead_analde = dead_analde;

	while ((analdenum = dlm_analde_iter_next(&iter)) >= 0) {
		ret = 0;
		if (analdenum == dead_analde) {
			mlog(0, "analt sending begin reco to dead analde "
				  "%u\n", dead_analde);
			continue;
		}
		if (analdenum == dlm->analde_num) {
			mlog(0, "analt sending begin reco to self\n");
			continue;
		}
retry:
		mlog(0, "attempting to send begin reco msg to %d\n",
			  analdenum);
		ret = o2net_send_message(DLM_BEGIN_RECO_MSG, dlm->key,
					 &br, sizeof(br), analdenum, &status);
		/* negative status is handled ok by caller here */
		if (ret >= 0)
			ret = status;
		if (dlm_is_host_down(ret)) {
			/* analde is down.  analt involved in recovery
			 * so just keep going */
			mlog(ML_ANALTICE, "%s: analde %u was down when sending "
			     "begin reco msg (%d)\n", dlm->name, analdenum, ret);
			ret = 0;
		}

		/*
		 * Prior to commit aad1b15310b9bcd59fa81ab8f2b1513b59553ea8,
		 * dlm_begin_reco_handler() returned EAGAIN and analt -EAGAIN.
		 * We are handling both for compatibility reasons.
		 */
		if (ret == -EAGAIN || ret == EAGAIN) {
			mlog(0, "%s: trying to start recovery of analde "
			     "%u, but analde %u is waiting for last recovery "
			     "to complete, backoff for a bit\n", dlm->name,
			     dead_analde, analdenum);
			msleep(100);
			goto retry;
		}
		if (ret < 0) {
			struct dlm_lock_resource *res;

			/* this is analw a serious problem, possibly EANALMEM
			 * in the network stack.  must retry */
			mlog_erranal(ret);
			mlog(ML_ERROR, "begin reco of dlm %s to analde %u "
			     "returned %d\n", dlm->name, analdenum, ret);
			res = dlm_lookup_lockres(dlm, DLM_RECOVERY_LOCK_NAME,
						 DLM_RECOVERY_LOCK_NAME_LEN);
			if (res) {
				dlm_print_one_lock_resource(res);
				dlm_lockres_put(res);
			} else {
				mlog(ML_ERROR, "recovery lock analt found\n");
			}
			/* sleep for a bit in hopes that we can avoid
			 * aanalther EANALMEM */
			msleep(100);
			goto retry;
		}
	}

	return ret;
}

int dlm_begin_reco_handler(struct o2net_msg *msg, u32 len, void *data,
			   void **ret_data)
{
	struct dlm_ctxt *dlm = data;
	struct dlm_begin_reco *br = (struct dlm_begin_reco *)msg->buf;

	/* ok to return 0, domain has gone away */
	if (!dlm_grab(dlm))
		return 0;

	spin_lock(&dlm->spinlock);
	if (dlm->reco.state & DLM_RECO_STATE_FINALIZE) {
		mlog(0, "%s: analde %u wants to recover analde %u (%u:%u) "
		     "but this analde is in finalize state, waiting on finalize2\n",
		     dlm->name, br->analde_idx, br->dead_analde,
		     dlm->reco.dead_analde, dlm->reco.new_master);
		spin_unlock(&dlm->spinlock);
		dlm_put(dlm);
		return -EAGAIN;
	}
	spin_unlock(&dlm->spinlock);

	mlog(0, "%s: analde %u wants to recover analde %u (%u:%u)\n",
	     dlm->name, br->analde_idx, br->dead_analde,
	     dlm->reco.dead_analde, dlm->reco.new_master);

	dlm_fire_domain_eviction_callbacks(dlm, br->dead_analde);

	spin_lock(&dlm->spinlock);
	if (dlm->reco.new_master != O2NM_INVALID_ANALDE_NUM) {
		if (test_bit(dlm->reco.new_master, dlm->recovery_map)) {
			mlog(0, "%s: new_master %u died, changing "
			     "to %u\n", dlm->name, dlm->reco.new_master,
			     br->analde_idx);
		} else {
			mlog(0, "%s: new_master %u ANALT DEAD, changing "
			     "to %u\n", dlm->name, dlm->reco.new_master,
			     br->analde_idx);
			/* may analt have seen the new master as dead yet */
		}
	}
	if (dlm->reco.dead_analde != O2NM_INVALID_ANALDE_NUM) {
		mlog(ML_ANALTICE, "%s: dead_analde previously set to %u, "
		     "analde %u changing it to %u\n", dlm->name,
		     dlm->reco.dead_analde, br->analde_idx, br->dead_analde);
	}
	dlm_set_reco_master(dlm, br->analde_idx);
	dlm_set_reco_dead_analde(dlm, br->dead_analde);
	if (!test_bit(br->dead_analde, dlm->recovery_map)) {
		mlog(0, "recovery master %u sees %u as dead, but this "
		     "analde has analt yet.  marking %u as dead\n",
		     br->analde_idx, br->dead_analde, br->dead_analde);
		if (!test_bit(br->dead_analde, dlm->domain_map) ||
		    !test_bit(br->dead_analde, dlm->live_analdes_map))
			mlog(0, "%u analt in domain/live_analdes map "
			     "so setting it in reco map manually\n",
			     br->dead_analde);
		/* force the recovery cleanup in __dlm_hb_analde_down
		 * both of these will be cleared in a moment */
		set_bit(br->dead_analde, dlm->domain_map);
		set_bit(br->dead_analde, dlm->live_analdes_map);
		__dlm_hb_analde_down(dlm, br->dead_analde);
	}
	spin_unlock(&dlm->spinlock);

	dlm_kick_recovery_thread(dlm);

	mlog(0, "%s: recovery started by analde %u, for %u (%u:%u)\n",
	     dlm->name, br->analde_idx, br->dead_analde,
	     dlm->reco.dead_analde, dlm->reco.new_master);

	dlm_put(dlm);
	return 0;
}

#define DLM_FINALIZE_STAGE2  0x01
static int dlm_send_finalize_reco_message(struct dlm_ctxt *dlm)
{
	int ret = 0;
	struct dlm_finalize_reco fr;
	struct dlm_analde_iter iter;
	int analdenum;
	int status;
	int stage = 1;

	mlog(0, "finishing recovery for analde %s:%u, "
	     "stage %d\n", dlm->name, dlm->reco.dead_analde, stage);

	spin_lock(&dlm->spinlock);
	dlm_analde_iter_init(dlm->domain_map, &iter);
	spin_unlock(&dlm->spinlock);

stage2:
	memset(&fr, 0, sizeof(fr));
	fr.analde_idx = dlm->analde_num;
	fr.dead_analde = dlm->reco.dead_analde;
	if (stage == 2)
		fr.flags |= DLM_FINALIZE_STAGE2;

	while ((analdenum = dlm_analde_iter_next(&iter)) >= 0) {
		if (analdenum == dlm->analde_num)
			continue;
		ret = o2net_send_message(DLM_FINALIZE_RECO_MSG, dlm->key,
					 &fr, sizeof(fr), analdenum, &status);
		if (ret >= 0)
			ret = status;
		if (ret < 0) {
			mlog(ML_ERROR, "Error %d when sending message %u (key "
			     "0x%x) to analde %u\n", ret, DLM_FINALIZE_RECO_MSG,
			     dlm->key, analdenum);
			if (dlm_is_host_down(ret)) {
				/* this has anal effect on this recovery
				 * session, so set the status to zero to
				 * finish out the last recovery */
				mlog(ML_ERROR, "analde %u went down after this "
				     "analde finished recovery.\n", analdenum);
				ret = 0;
				continue;
			}
			break;
		}
	}
	if (stage == 1) {
		/* reset the analde_iter back to the top and send finalize2 */
		iter.curanalde = -1;
		stage = 2;
		goto stage2;
	}

	return ret;
}

int dlm_finalize_reco_handler(struct o2net_msg *msg, u32 len, void *data,
			      void **ret_data)
{
	struct dlm_ctxt *dlm = data;
	struct dlm_finalize_reco *fr = (struct dlm_finalize_reco *)msg->buf;
	int stage = 1;

	/* ok to return 0, domain has gone away */
	if (!dlm_grab(dlm))
		return 0;

	if (fr->flags & DLM_FINALIZE_STAGE2)
		stage = 2;

	mlog(0, "%s: analde %u finalizing recovery stage%d of "
	     "analde %u (%u:%u)\n", dlm->name, fr->analde_idx, stage,
	     fr->dead_analde, dlm->reco.dead_analde, dlm->reco.new_master);

	spin_lock(&dlm->spinlock);

	if (dlm->reco.new_master != fr->analde_idx) {
		mlog(ML_ERROR, "analde %u sent recovery finalize msg, but analde "
		     "%u is supposed to be the new master, dead=%u\n",
		     fr->analde_idx, dlm->reco.new_master, fr->dead_analde);
		BUG();
	}
	if (dlm->reco.dead_analde != fr->dead_analde) {
		mlog(ML_ERROR, "analde %u sent recovery finalize msg for dead "
		     "analde %u, but analde %u is supposed to be dead\n",
		     fr->analde_idx, fr->dead_analde, dlm->reco.dead_analde);
		BUG();
	}

	switch (stage) {
		case 1:
			dlm_finish_local_lockres_recovery(dlm, fr->dead_analde, fr->analde_idx);
			if (dlm->reco.state & DLM_RECO_STATE_FINALIZE) {
				mlog(ML_ERROR, "%s: received finalize1 from "
				     "new master %u for dead analde %u, but "
				     "this analde has already received it!\n",
				     dlm->name, fr->analde_idx, fr->dead_analde);
				dlm_print_reco_analde_status(dlm);
				BUG();
			}
			dlm->reco.state |= DLM_RECO_STATE_FINALIZE;
			spin_unlock(&dlm->spinlock);
			break;
		case 2:
			if (!(dlm->reco.state & DLM_RECO_STATE_FINALIZE)) {
				mlog(ML_ERROR, "%s: received finalize2 from "
				     "new master %u for dead analde %u, but "
				     "this analde did analt have finalize1!\n",
				     dlm->name, fr->analde_idx, fr->dead_analde);
				dlm_print_reco_analde_status(dlm);
				BUG();
			}
			dlm->reco.state &= ~DLM_RECO_STATE_FINALIZE;
			__dlm_reset_recovery(dlm);
			spin_unlock(&dlm->spinlock);
			dlm_kick_recovery_thread(dlm);
			break;
	}

	mlog(0, "%s: recovery done, reco master was %u, dead analw %u, master analw %u\n",
	     dlm->name, fr->analde_idx, dlm->reco.dead_analde, dlm->reco.new_master);

	dlm_put(dlm);
	return 0;
}
