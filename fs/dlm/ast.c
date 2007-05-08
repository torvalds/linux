/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  1997-2003  All rights reserved.
**  Copyright (C) 2004-2005 Red Hat, Inc.  All rights reserved.
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

void dlm_add_ast(struct dlm_lkb *lkb, int type)
{
	if (lkb->lkb_flags & DLM_IFL_USER) {
		dlm_user_add_ast(lkb, type);
		return;
	}
	DLM_ASSERT(lkb->lkb_astaddr != DLM_FAKE_USER_AST, dlm_print_lkb(lkb););

	spin_lock(&ast_queue_lock);
	if (!(lkb->lkb_ast_type & (AST_COMP | AST_BAST))) {
		kref_get(&lkb->lkb_ref);
		list_add_tail(&lkb->lkb_astqueue, &ast_queue);
	}
	lkb->lkb_ast_type |= type;
	spin_unlock(&ast_queue_lock);

	set_bit(WAKE_ASTS, &astd_wakeflags);
	wake_up_process(astd_task);
}

static void process_asts(void)
{
	struct dlm_ls *ls = NULL;
	struct dlm_rsb *r = NULL;
	struct dlm_lkb *lkb;
	void (*cast) (long param);
	void (*bast) (long param, int mode);
	int type = 0, found, bmode;

	for (;;) {
		found = 0;
		spin_lock(&ast_queue_lock);
		list_for_each_entry(lkb, &ast_queue, lkb_astqueue) {
			r = lkb->lkb_resource;
			ls = r->res_ls;

			if (dlm_locking_stopped(ls))
				continue;

			list_del(&lkb->lkb_astqueue);
			type = lkb->lkb_ast_type;
			lkb->lkb_ast_type = 0;
			found = 1;
			break;
		}
		spin_unlock(&ast_queue_lock);

		if (!found)
			break;

		cast = lkb->lkb_astaddr;
		bast = lkb->lkb_bastaddr;
		bmode = lkb->lkb_bastmode;

		if ((type & AST_COMP) && cast)
			cast(lkb->lkb_astparam);

		/* FIXME: Is it safe to look at lkb_grmode here
		   without doing a lock_rsb() ?
		   Look at other checks in v1 to avoid basts. */

		if ((type & AST_BAST) && bast)
			if (!dlm_modes_compat(lkb->lkb_grmode, bmode))
				bast(lkb->lkb_astparam, bmode);

		/* this removes the reference added by dlm_add_ast
		   and may result in the lkb being freed */
		dlm_put_lkb(lkb);

		schedule();
	}
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

