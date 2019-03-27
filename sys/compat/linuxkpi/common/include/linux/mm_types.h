/*-
 * Copyright (c) 2017 Mellanox Technologies, Ltd.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _LINUX_MM_TYPES_H_
#define	_LINUX_MM_TYPES_H_

#include <linux/types.h>
#include <linux/page.h>
#include <linux/rwsem.h>

#include <asm/atomic.h>

struct vm_area_struct;
struct task_struct;

struct mm_struct {
	struct vm_area_struct *mmap;
	atomic_t mm_count;
	atomic_t mm_users;
	size_t pinned_vm;
	struct rw_semaphore mmap_sem;
};

extern void linux_mm_dtor(struct mm_struct *mm);

static inline void
mmdrop(struct mm_struct *mm)
{
	if (__predict_false(atomic_dec_and_test(&mm->mm_count)))
		linux_mm_dtor(mm);
}

static inline void
mmput(struct mm_struct *mm)
{
	if (__predict_false(atomic_dec_and_test(&mm->mm_users)))
		mmdrop(mm);
}

static inline void
mmgrab(struct mm_struct *mm)
{
	atomic_inc(&mm->mm_count);
}

extern struct mm_struct *linux_get_task_mm(struct task_struct *);
#define	get_task_mm(task) linux_get_task_mm(task)

#endif					/* _LINUX_MM_TYPES_H_ */
