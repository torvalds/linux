/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2004-2010 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#include "dlm_internal.h"
#include "lock.h"
#include "user.h"
#include "ast.h"

#define WAKE_ASTS  0

static struct list_head		ast_queue;
static spinlock_t		ast_queue_lock;
static struct task_struct *	astd_task;
static unsigned long		astd_wakeflags;
static struct mutex		astd_running;


void dlm_del_ast(struct dlm_lkb *lkb)
{
	spin_lock(&ast_queue_lock);
	if (lkb->lkb_ast_type & (AST_COMP | AST_BAST))
		list_del(&lkb->lkb_astqueue);
	spin_unlock(&ast_queue_lock);
}

void dlm_add_ast(struct dlm_lkb *lkb, int type, int mode)
{
	if (lkb->lkb_flags & DLM_IFL_USER) {
		dlm_user_add_ast(lkb, type, mode);
		return;
	}

	spin_lock(&ast_queue_lock);
	if (!(lkb->lkb_ast_type & (AST_COMP | AST_BAST))) {
		kref_get(&lkb->lkb_ref);
		list_add_tail(&lkb->lkb_astqueue, &ast_queue);
		lkb->lkb_ast_first = type;
	}

	/* sanity check, this should not happen */

	if ((type == AST_COMP) && (lkb->lkb_ast_type & AST_COMP))
		log_print("repeat cast %d castmode %d lock %x %s",
			  mode, lkb->lkb_castmode,
			  lkb->lkb_id, lkb->lkb_resource->res_name);

	lkb->lkb_ast_type |= type;
	if (type == AST_BAST)
		lkb->lkb_bastmode = mode;
	else
		lkb->lkb_castmode = mode;
	spin_unlock(&ast_queue_lock);

	set_bit(WAKE_ASTS, &astd_wakeflags);
	wake_up_process(astd_task);
}

static void process_asts(void)
{
	struct dlm_ls *ls = NULL;
	struct dlm_rsb *r = NULL;
	struct dlm_lkb *lkb;
	void (*castfn) (void *astparam);
	void (*bastfn) (void *astparam, int mode);
	int type, first, bastmode, castmode, do_bast, do_cast, last_castmode;

repeat:
	spin_lock(&ast_queue_lock);
	list_for_each_entry(lkb, &ast_queue, lkb_astqueue) {
		r = lkb->lkb_resource;
		ls = r->res_ls;

		if (dlm_locking_stopped(ls))
			continue;

		list_del(&lkb->lkb_astqueue);
		type = lkb->lkb_ast_type;
		lkb->lkb_ast_type = 0;
		first = lkb->lkb_ast_first;
		lkb->lkb_ast_first = 0;
		bastmode = lkb->lkb_bastmode;
		castmode = lkb->lkb_castmode;
		castfn = lkb->lkb_astfn;
		bastfn = lkb->lkb_bastfn;
		spin_unlock(&ast_queue_lock);

		do_cast = (type & AST_COMP) && castfn;
		do_bast = (type & AST_BAST) && bastfn;

		/* Skip a bast if its blocking mode is compatible with the
		   granted mode of the preceding cast. */

		if (do_bast) {
			if (first == AST_COMP)
				last_castmode = castmode;
			else
				last_castmode = lkb->lkb_castmode_done;
			if (dlm_modes_compat(bastmode, last_castmode))
				do_bast = 0;
		}

		if (first == AST_COMP) {
			if (do_cast)
				castfn(lkb->lkb_astparam);
			if (do_bast)
				bastfn(lkb->lkb_astparam, bastmode);
		} else if (first == AST_BAST) {
			if (do_bast)
				bastfn(lkb->lkb_astparam, bastmode);
			if (do_cast)
				castfn(lkb->lkb_astparam);
		} else {
			log_error(ls, "bad ast_first %d ast_type %d",
				  first, type);
		}

		if (do_cast)
			lkb->lkb_castmode_done = castmode;
		if (do_bast)
			lkb->lkb_bastmode_done = bastmode;

		/* this removes the reference added by dlm_add_ast
		   and may result in the lkb being freed */
		dlm_put_lkb(lkb);

		cond_resched();
		goto repeat;
	}
	spin_unlock(&ast_queue_lock);
}

static inline int no_asts(void)
{
	int ret;

	spin_lock(&ast_queue_lock);
	ret = list_empty(&ast_queue);
	spin_unlock(&ast_queue_lock);
	return ret;
}

static int dlm_astd(void *data)
{
	while (!kthread_should_stop()) {
		set_current_state(TASK_INTERRUPTIBLE);
		if (!test_bit(WAKE_ASTS, &astd_wakeflags))
			schedule();
		set_current_state(TASK_RUNNING);

		mutex_lock(&astd_running);
		if (test_and_clear_bit(WAKE_ASTS, &astd_wakeflags))
			process_asts();
		mutex_unlock(&astd_running);
	}
	return 0;
}

void dlm_astd_wake(void)
{
	if (!no_asts()) {
		set_bit(WAKE_ASTS, &astd_wakeflags);
		wake_up_process(astd_task);
	}
}

int dlm_astd_start(void)
{
	struct task_struct *p;
	int error = 0;

	INIT_LIST_HEAD(&ast_queue);
	spin_lock_init(&ast_queue_lock);
	mutex_init(&astd_running);

	p = kthread_run(dlm_astd, NULL, "dlm_astd");
	if (IS_ERR(p))
		error = PTR_ERR(p);
	else
		astd_task = p;
	return error;
}

void dlm_astd_stop(void)
{
	kthread_stop(astd_task);
}

void dlm_astd_suspend(void)
{
	mutex_lock(&astd_running);
}

void dlm_astd_resume(void)
{
	mutex_unlock(&astd_running);
}

