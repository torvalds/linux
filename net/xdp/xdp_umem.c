// SPDX-License-Identifier: GPL-2.0
/* XDP user-space packet buffer
 * Copyright(c) 2018 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include <linux/init.h>
#include <linux/sched/mm.h>
#include <linux/sched/signal.h>
#include <linux/sched/task.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/bpf.h>
#include <linux/mm.h>

#include "xdp_umem.h"

#define XDP_UMEM_MIN_FRAME_SIZE 2048

int xdp_umem_create(struct xdp_umem **umem)
{
	*umem = kzalloc(sizeof(**umem), GFP_KERNEL);

	if (!(*umem))
		return -ENOMEM;

	return 0;
}

static void xdp_umem_unpin_pages(struct xdp_umem *umem)
{
	unsigned int i;

	if (umem->pgs) {
		for (i = 0; i < umem->npgs; i++) {
			struct page *page = umem->pgs[i];

			set_page_dirty_lock(page);
			put_page(page);
		}

		kfree(umem->pgs);
		umem->pgs = NULL;
	}
}

static void xdp_umem_unaccount_pages(struct xdp_umem *umem)
{
	if (umem->user) {
		atomic_long_sub(umem->npgs, &umem->user->locked_vm);
		free_uid(umem->user);
	}
}

static void xdp_umem_release(struct xdp_umem *umem)
{
	struct task_struct *task;
	struct mm_struct *mm;

	if (umem->fq) {
		xskq_destroy(umem->fq);
		umem->fq = NULL;
	}

	if (umem->pgs) {
		xdp_umem_unpin_pages(umem);

		task = get_pid_task(umem->pid, PIDTYPE_PID);
		put_pid(umem->pid);
		if (!task)
			goto out;
		mm = get_task_mm(task);
		put_task_struct(task);
		if (!mm)
			goto out;

		mmput(mm);
		umem->pgs = NULL;
	}

	xdp_umem_unaccount_pages(umem);
out:
	kfree(umem);
}

static void xdp_umem_release_deferred(struct work_struct *work)
{
	struct xdp_umem *umem = container_of(work, struct xdp_umem, work);

	xdp_umem_release(umem);
}

void xdp_get_umem(struct xdp_umem *umem)
{
	atomic_inc(&umem->users);
}

void xdp_put_umem(struct xdp_umem *umem)
{
	if (!umem)
		return;

	if (atomic_dec_and_test(&umem->users)) {
		INIT_WORK(&umem->work, xdp_umem_release_deferred);
		schedule_work(&umem->work);
	}
}

static int xdp_umem_pin_pages(struct xdp_umem *umem)
{
	unsigned int gup_flags = FOLL_WRITE;
	long npgs;
	int err;

	umem->pgs = kcalloc(umem->npgs, sizeof(*umem->pgs), GFP_KERNEL);
	if (!umem->pgs)
		return -ENOMEM;

	down_write(&current->mm->mmap_sem);
	npgs = get_user_pages(umem->address, umem->npgs,
			      gup_flags, &umem->pgs[0], NULL);
	up_write(&current->mm->mmap_sem);

	if (npgs != umem->npgs) {
		if (npgs >= 0) {
			umem->npgs = npgs;
			err = -ENOMEM;
			goto out_pin;
		}
		err = npgs;
		goto out_pgs;
	}
	return 0;

out_pin:
	xdp_umem_unpin_pages(umem);
out_pgs:
	kfree(umem->pgs);
	umem->pgs = NULL;
	return err;
}

static int xdp_umem_account_pages(struct xdp_umem *umem)
{
	unsigned long lock_limit, new_npgs, old_npgs;

	if (capable(CAP_IPC_LOCK))
		return 0;

	lock_limit = rlimit(RLIMIT_MEMLOCK) >> PAGE_SHIFT;
	umem->user = get_uid(current_user());

	do {
		old_npgs = atomic_long_read(&umem->user->locked_vm);
		new_npgs = old_npgs + umem->npgs;
		if (new_npgs > lock_limit) {
			free_uid(umem->user);
			umem->user = NULL;
			return -ENOBUFS;
		}
	} while (atomic_long_cmpxchg(&umem->user->locked_vm, old_npgs,
				     new_npgs) != old_npgs);
	return 0;
}

int xdp_umem_reg(struct xdp_umem *umem, struct xdp_umem_reg *mr)
{
	u32 frame_size = mr->frame_size, frame_headroom = mr->frame_headroom;
	u64 addr = mr->addr, size = mr->len;
	unsigned int nframes, nfpp;
	int size_chk, err;

	if (!umem)
		return -EINVAL;

	if (frame_size < XDP_UMEM_MIN_FRAME_SIZE || frame_size > PAGE_SIZE) {
		/* Strictly speaking we could support this, if:
		 * - huge pages, or*
		 * - using an IOMMU, or
		 * - making sure the memory area is consecutive
		 * but for now, we simply say "computer says no".
		 */
		return -EINVAL;
	}

	if (!is_power_of_2(frame_size))
		return -EINVAL;

	if (!PAGE_ALIGNED(addr)) {
		/* Memory area has to be page size aligned. For
		 * simplicity, this might change.
		 */
		return -EINVAL;
	}

	if ((addr + size) < addr)
		return -EINVAL;

	nframes = size / frame_size;
	if (nframes == 0 || nframes > UINT_MAX)
		return -EINVAL;

	nfpp = PAGE_SIZE / frame_size;
	if (nframes < nfpp || nframes % nfpp)
		return -EINVAL;

	frame_headroom = ALIGN(frame_headroom, 64);

	size_chk = frame_size - frame_headroom - XDP_PACKET_HEADROOM;
	if (size_chk < 0)
		return -EINVAL;

	umem->pid = get_task_pid(current, PIDTYPE_PID);
	umem->size = (size_t)size;
	umem->address = (unsigned long)addr;
	umem->props.frame_size = frame_size;
	umem->props.nframes = nframes;
	umem->frame_headroom = frame_headroom;
	umem->npgs = size / PAGE_SIZE;
	umem->pgs = NULL;
	umem->user = NULL;

	umem->frame_size_log2 = ilog2(frame_size);
	umem->nfpp_mask = nfpp - 1;
	umem->nfpplog2 = ilog2(nfpp);
	atomic_set(&umem->users, 1);

	err = xdp_umem_account_pages(umem);
	if (err)
		goto out;

	err = xdp_umem_pin_pages(umem);
	if (err)
		goto out_account;
	return 0;

out_account:
	xdp_umem_unaccount_pages(umem);
out:
	put_pid(umem->pid);
	return err;
}

bool xdp_umem_validate_queues(struct xdp_umem *umem)
{
	return umem->fq;
}
