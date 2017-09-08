/*
 * Copyright 2013 Red Hat Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Authors: JÃ©rÃ´me Glisse <jglisse@redhat.com>
 */
/*
 * Refer to include/linux/hmm.h for information about heterogeneous memory
 * management or HMM for short.
 */
#include <linux/mm.h>
#include <linux/hmm.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/mmu_notifier.h>


#ifdef CONFIG_HMM
static const struct mmu_notifier_ops hmm_mmu_notifier_ops;

/*
 * struct hmm - HMM per mm struct
 *
 * @mm: mm struct this HMM struct is bound to
 * @sequence: we track updates to the CPU page table with a sequence number
 * @mirrors: list of mirrors for this mm
 * @mmu_notifier: mmu notifier to track updates to CPU page table
 * @mirrors_sem: read/write semaphore protecting the mirrors list
 */
struct hmm {
	struct mm_struct	*mm;
	atomic_t		sequence;
	struct list_head	mirrors;
	struct mmu_notifier	mmu_notifier;
	struct rw_semaphore	mirrors_sem;
};

/*
 * hmm_register - register HMM against an mm (HMM internal)
 *
 * @mm: mm struct to attach to
 *
 * This is not intended to be used directly by device drivers. It allocates an
 * HMM struct if mm does not have one, and initializes it.
 */
static struct hmm *hmm_register(struct mm_struct *mm)
{
	struct hmm *hmm = READ_ONCE(mm->hmm);
	bool cleanup = false;

	/*
	 * The hmm struct can only be freed once the mm_struct goes away,
	 * hence we should always have pre-allocated an new hmm struct
	 * above.
	 */
	if (hmm)
		return hmm;

	hmm = kmalloc(sizeof(*hmm), GFP_KERNEL);
	if (!hmm)
		return NULL;
	INIT_LIST_HEAD(&hmm->mirrors);
	init_rwsem(&hmm->mirrors_sem);
	atomic_set(&hmm->sequence, 0);
	hmm->mmu_notifier.ops = NULL;
	hmm->mm = mm;

	/*
	 * We should only get here if hold the mmap_sem in write mode ie on
	 * registration of first mirror through hmm_mirror_register()
	 */
	hmm->mmu_notifier.ops = &hmm_mmu_notifier_ops;
	if (__mmu_notifier_register(&hmm->mmu_notifier, mm)) {
		kfree(hmm);
		return NULL;
	}

	spin_lock(&mm->page_table_lock);
	if (!mm->hmm)
		mm->hmm = hmm;
	else
		cleanup = true;
	spin_unlock(&mm->page_table_lock);

	if (cleanup) {
		mmu_notifier_unregister(&hmm->mmu_notifier, mm);
		kfree(hmm);
	}

	return mm->hmm;
}

void hmm_mm_destroy(struct mm_struct *mm)
{
	kfree(mm->hmm);
}
#endif /* CONFIG_HMM */

#if IS_ENABLED(CONFIG_HMM_MIRROR)
static void hmm_invalidate_range(struct hmm *hmm,
				 enum hmm_update_type action,
				 unsigned long start,
				 unsigned long end)
{
	struct hmm_mirror *mirror;

	down_read(&hmm->mirrors_sem);
	list_for_each_entry(mirror, &hmm->mirrors, list)
		mirror->ops->sync_cpu_device_pagetables(mirror, action,
							start, end);
	up_read(&hmm->mirrors_sem);
}

static void hmm_invalidate_range_start(struct mmu_notifier *mn,
				       struct mm_struct *mm,
				       unsigned long start,
				       unsigned long end)
{
	struct hmm *hmm = mm->hmm;

	VM_BUG_ON(!hmm);

	atomic_inc(&hmm->sequence);
}

static void hmm_invalidate_range_end(struct mmu_notifier *mn,
				     struct mm_struct *mm,
				     unsigned long start,
				     unsigned long end)
{
	struct hmm *hmm = mm->hmm;

	VM_BUG_ON(!hmm);

	hmm_invalidate_range(mm->hmm, HMM_UPDATE_INVALIDATE, start, end);
}

static const struct mmu_notifier_ops hmm_mmu_notifier_ops = {
	.invalidate_range_start	= hmm_invalidate_range_start,
	.invalidate_range_end	= hmm_invalidate_range_end,
};

/*
 * hmm_mirror_register() - register a mirror against an mm
 *
 * @mirror: new mirror struct to register
 * @mm: mm to register against
 *
 * To start mirroring a process address space, the device driver must register
 * an HMM mirror struct.
 *
 * THE mm->mmap_sem MUST BE HELD IN WRITE MODE !
 */
int hmm_mirror_register(struct hmm_mirror *mirror, struct mm_struct *mm)
{
	/* Sanity check */
	if (!mm || !mirror || !mirror->ops)
		return -EINVAL;

	mirror->hmm = hmm_register(mm);
	if (!mirror->hmm)
		return -ENOMEM;

	down_write(&mirror->hmm->mirrors_sem);
	list_add(&mirror->list, &mirror->hmm->mirrors);
	up_write(&mirror->hmm->mirrors_sem);

	return 0;
}
EXPORT_SYMBOL(hmm_mirror_register);

/*
 * hmm_mirror_unregister() - unregister a mirror
 *
 * @mirror: new mirror struct to register
 *
 * Stop mirroring a process address space, and cleanup.
 */
void hmm_mirror_unregister(struct hmm_mirror *mirror)
{
	struct hmm *hmm = mirror->hmm;

	down_write(&hmm->mirrors_sem);
	list_del(&mirror->list);
	up_write(&hmm->mirrors_sem);
}
EXPORT_SYMBOL(hmm_mirror_unregister);
#endif /* IS_ENABLED(CONFIG_HMM_MIRROR) */
