// SPDX-License-Identifier: GPL-2.0-only
/*
 * kvm asynchronous fault support
 *
 * Copyright 2010 Red Hat, Inc.
 *
 * Author:
 *      Gleb Natapov <gleb@redhat.com>
 */

#include <linux/kvm_host.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/mmu_context.h>
#include <linux/sched/mm.h>

#include "async_pf.h"
#include <trace/events/kvm.h>

static struct kmem_cache *async_pf_cache;

int kvm_async_pf_init(void)
{
	async_pf_cache = KMEM_CACHE(kvm_async_pf, 0);

	if (!async_pf_cache)
		return -ENOMEM;

	return 0;
}

void kvm_async_pf_deinit(void)
{
	kmem_cache_destroy(async_pf_cache);
	async_pf_cache = NULL;
}

void kvm_async_pf_vcpu_init(struct kvm_vcpu *vcpu)
{
	INIT_LIST_HEAD(&vcpu->async_pf.done);
	INIT_LIST_HEAD(&vcpu->async_pf.queue);
	spin_lock_init(&vcpu->async_pf.lock);
}

static void async_pf_execute(struct work_struct *work)
{
	struct kvm_async_pf *apf =
		container_of(work, struct kvm_async_pf, work);
	struct kvm_vcpu *vcpu = apf->vcpu;
	struct mm_struct *mm = vcpu->kvm->mm;
	unsigned long addr = apf->addr;
	gpa_t cr2_or_gpa = apf->cr2_or_gpa;
	int locked = 1;
	bool first;

	might_sleep();

	/*
	 * Attempt to pin the VM's host address space, and simply skip gup() if
	 * acquiring a pin fail, i.e. if the process is exiting.  Note, KVM
	 * holds a reference to its associated mm_struct until the very end of
	 * kvm_destroy_vm(), i.e. the struct itself won't be freed before this
	 * work item is fully processed.
	 */
	if (mmget_not_zero(mm)) {
		mmap_read_lock(mm);
		get_user_pages_remote(mm, addr, 1, FOLL_WRITE, NULL, &locked);
		if (locked)
			mmap_read_unlock(mm);
		mmput(mm);
	}

	/*
	 * Notify and kick the vCPU even if faulting in the page failed, e.g.
	 * so that the vCPU can retry the fault synchronously.
	 */
	if (IS_ENABLED(CONFIG_KVM_ASYNC_PF_SYNC))
		kvm_arch_async_page_present(vcpu, apf);

	spin_lock(&vcpu->async_pf.lock);
	first = list_empty(&vcpu->async_pf.done);
	list_add_tail(&apf->link, &vcpu->async_pf.done);
	spin_unlock(&vcpu->async_pf.lock);

	/*
	 * The apf struct may be freed by kvm_check_async_pf_completion() as
	 * soon as the lock is dropped.  Nullify it to prevent improper usage.
	 */
	apf = NULL;

	if (!IS_ENABLED(CONFIG_KVM_ASYNC_PF_SYNC) && first)
		kvm_arch_async_page_present_queued(vcpu);

	trace_kvm_async_pf_completed(addr, cr2_or_gpa);

	__kvm_vcpu_wake_up(vcpu);
}

static void kvm_flush_and_free_async_pf_work(struct kvm_async_pf *work)
{
	/*
	 * The async #PF is "done", but KVM must wait for the work item itself,
	 * i.e. async_pf_execute(), to run to completion.  If KVM is a module,
	 * KVM must ensure *no* code owned by the KVM (the module) can be run
	 * after the last call to module_put().  Note, flushing the work item
	 * is always required when the item is taken off the completion queue.
	 * E.g. even if the vCPU handles the item in the "normal" path, the VM
	 * could be terminated before async_pf_execute() completes.
	 *
	 * Wake all events skip the queue and go straight done, i.e. don't
	 * need to be flushed (but sanity check that the work wasn't queued).
	 */
	if (work->wakeup_all)
		WARN_ON_ONCE(work->work.func);
	else
		flush_work(&work->work);
	kmem_cache_free(async_pf_cache, work);
}

void kvm_clear_async_pf_completion_queue(struct kvm_vcpu *vcpu)
{
	/* cancel outstanding work queue item */
	while (!list_empty(&vcpu->async_pf.queue)) {
		struct kvm_async_pf *work =
			list_first_entry(&vcpu->async_pf.queue,
					 typeof(*work), queue);
		list_del(&work->queue);

#ifdef CONFIG_KVM_ASYNC_PF_SYNC
		flush_work(&work->work);
#else
		if (cancel_work_sync(&work->work))
			kmem_cache_free(async_pf_cache, work);
#endif
	}

	spin_lock(&vcpu->async_pf.lock);
	while (!list_empty(&vcpu->async_pf.done)) {
		struct kvm_async_pf *work =
			list_first_entry(&vcpu->async_pf.done,
					 typeof(*work), link);
		list_del(&work->link);

		spin_unlock(&vcpu->async_pf.lock);
		kvm_flush_and_free_async_pf_work(work);
		spin_lock(&vcpu->async_pf.lock);
	}
	spin_unlock(&vcpu->async_pf.lock);

	vcpu->async_pf.queued = 0;
}

void kvm_check_async_pf_completion(struct kvm_vcpu *vcpu)
{
	struct kvm_async_pf *work;

	while (!list_empty_careful(&vcpu->async_pf.done) &&
	      kvm_arch_can_dequeue_async_page_present(vcpu)) {
		spin_lock(&vcpu->async_pf.lock);
		work = list_first_entry(&vcpu->async_pf.done, typeof(*work),
					      link);
		list_del(&work->link);
		spin_unlock(&vcpu->async_pf.lock);

		kvm_arch_async_page_ready(vcpu, work);
		if (!IS_ENABLED(CONFIG_KVM_ASYNC_PF_SYNC))
			kvm_arch_async_page_present(vcpu, work);

		list_del(&work->queue);
		vcpu->async_pf.queued--;
		kvm_flush_and_free_async_pf_work(work);
	}
}

/*
 * Try to schedule a job to handle page fault asynchronously. Returns 'true' on
 * success, 'false' on failure (page fault has to be handled synchronously).
 */
bool kvm_setup_async_pf(struct kvm_vcpu *vcpu, gpa_t cr2_or_gpa,
			unsigned long hva, struct kvm_arch_async_pf *arch)
{
	struct kvm_async_pf *work;

	if (vcpu->async_pf.queued >= ASYNC_PF_PER_VCPU)
		return false;

	/* Arch specific code should not do async PF in this case */
	if (unlikely(kvm_is_error_hva(hva)))
		return false;

	/*
	 * do alloc nowait since if we are going to sleep anyway we
	 * may as well sleep faulting in page
	 */
	work = kmem_cache_zalloc(async_pf_cache, GFP_NOWAIT);
	if (!work)
		return false;

	work->wakeup_all = false;
	work->vcpu = vcpu;
	work->cr2_or_gpa = cr2_or_gpa;
	work->addr = hva;
	work->arch = *arch;

	INIT_WORK(&work->work, async_pf_execute);

	list_add_tail(&work->queue, &vcpu->async_pf.queue);
	vcpu->async_pf.queued++;
	work->notpresent_injected = kvm_arch_async_page_not_present(vcpu, work);

	schedule_work(&work->work);

	return true;
}

int kvm_async_pf_wakeup_all(struct kvm_vcpu *vcpu)
{
	struct kvm_async_pf *work;
	bool first;

	if (!list_empty_careful(&vcpu->async_pf.done))
		return 0;

	work = kmem_cache_zalloc(async_pf_cache, GFP_ATOMIC);
	if (!work)
		return -ENOMEM;

	work->wakeup_all = true;
	INIT_LIST_HEAD(&work->queue); /* for list_del to work */

	spin_lock(&vcpu->async_pf.lock);
	first = list_empty(&vcpu->async_pf.done);
	list_add_tail(&work->link, &vcpu->async_pf.done);
	spin_unlock(&vcpu->async_pf.lock);

	if (!IS_ENABLED(CONFIG_KVM_ASYNC_PF_SYNC) && first)
		kvm_arch_async_page_present_queued(vcpu);

	vcpu->async_pf.queued++;
	return 0;
}
