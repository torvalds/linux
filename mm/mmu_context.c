/* Copyright (C) 2009 Red Hat, Inc.
 *
 * See ../COPYING for licensing terms.
 */

#include <linux/mm.h>
#include <linux/mmu_context.h>
#include <linux/sched.h>

#include <asm/mmu_context.h>

/*
 * use_mm
 *	Makes the calling kernel thread take on the specified
 *	mm context.
 *	Called by the retry thread execute retries within the
 *	iocb issuer's mm context, so that copy_from/to_user
 *	operations work seamlessly for aio.
 *	(Note: this routine is intended to be called only
 *	from a kernel thread context)
 */
void use_mm(struct mm_struct *mm)
{
	struct mm_struct *active_mm;
	struct task_struct *tsk = current;

	task_lock(tsk);
	active_mm = tsk->active_mm;
	if (active_mm != mm) {
		atomic_inc(&mm->mm_count);
		tsk->active_mm = mm;
	}
	tsk->mm = mm;
	switch_mm(active_mm, mm, tsk);
	task_unlock(tsk);

	if (active_mm != mm)
		mmdrop(active_mm);
}

/*
 * unuse_mm
 *	Reverses the effect of use_mm, i.e. releases the
 *	specified mm context which was earlier taken on
 *	by the calling kernel thread
 *	(Note: this routine is intended to be called only
 *	from a kernel thread context)
 */
void unuse_mm(struct mm_struct *mm)
{
	struct task_struct *tsk = current;

	task_lock(tsk);
	tsk->mm = NULL;
	/* active_mm is still 'mm' */
	enter_lazy_tlb(mm, tsk);
	task_unlock(tsk);
}
