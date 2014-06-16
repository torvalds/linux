/*
 * kvm asynchronous fault support
 *
 * Copyright 2010 Red Hat, Inc.
 *
 * Author:
 *      Gleb Natapov <gleb@redhat.com>
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include <linux/kvm_host.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/mmu_context.h>

#include "async_pf.h"
#include <trace/events/kvm.h>

static inline void kvm_async_page_present_sync(struct kvm_vcpu *vcpu,
					       struct kvm_async_pf *work)
{
#ifdef CONFIG_KVM_ASYNC_PF_SYNC
	kvm_arch_async_page_present(vcpu, work);
#endif
}
static inline void kvm_async_page_present_async(struct kvm_vcpu *vcpu,
						struct kvm_async_pf *work)
{
#ifndef CONFIG_KVM_ASYNC_PF_SYNC
	kvm_arch_async_page_present(vcpu, work);
#endif
}

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
	if (async_pf_cache)
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
	struct mm_struct *mm = apf->mm;
	struct kvm_vcpu *vcpu = apf->vcpu;
	unsigned long addr = apf->addr;
	gva_t gva = apf->gva;

	might_sleep();

	down_read(&mm->mmap_sem);
	get_user_pages(NULL, mm, addr, 1, 1, 0, NULL, NULL);
	up_read(&mm->mmap_sem);
	kvm_async_page_present_sync(vcpu, apf);

	spin_lock(&vcpu->async_pf.lock);
	list_add_tail(&apf->link, &vcpu->async_pf.done);
	spin_unlock(&vcpu->async_pf.lock);

	/*
	 * apf may be freed by kvm_check_async_pf_completion() after
	 * this point
	 */

	trace_kvm_async_pf_completed(addr, gva);

	if (waitqueue_active(&vcpu->wq))
		wake_up_interruptible(&vcpu->wq);

	mmput(mm);
	kvm_put_kvm(vcpu->kvm);
}

void kvm_clear_async_pf_completion_queue(struct kvm_vcpu *vcpu)
{
	/* cancel outstanding work queue item */
	while (!list_empty(&vcpu->async_pf.queue)) {
		struct kvm_async_pf *work =
			list_entry(vcpu->async_pf.queue.next,
				   typeof(*work), queue);
		list_del(&work->queue);

#ifdef CONFIG_KVM_ASYNC_PF_SYNC
		flush_work(&work->work);
#else
		if (cancel_work_sync(&work->work)) {
			mmput(work->mm);
			kvm_put_kvm(vcpu->kvm); /* == work->vcpu->kvm */
			kmem_cache_free(async_pf_cache, work);
		}
#endif
	}

	spin_lock(&vcpu->async_pf.lock);
	while (!list_empty(&vcpu->async_pf.done)) {
		struct kvm_async_pf *work =
			list_entry(vcpu->async_pf.done.next,
				   typeof(*work), link);
		list_del(&work->link);
		kmem_cache_free(async_pf_cache, work);
	}
	spin_unlock(&vcpu->async_pf.lock);

	vcpu->async_pf.queued = 0;
}

void kvm_check_async_pf_completion(struct kvm_vcpu *vcpu)
{
	struct kvm_async_pf *work;

	while (!list_empty_careful(&vcpu->async_pf.done) &&
	      kvm_arch_can_inject_async_page_present(vcpu)) {
		spin_lock(&vcpu->async_pf.lock);
		work = list_first_entry(&vcpu->async_pf.done, typeof(*work),
					      link);
		list_del(&work->link);
		spin_unlock(&vcpu->async_pf.lock);

		kvm_arch_async_page_ready(vcpu, work);
		kvm_async_page_present_async(vcpu, work);

		list_del(&work->queue);
		vcpu->async_pf.queued--;
		kmem_cache_free(async_pf_cache, work);
	}
}

int kvm_setup_async_pf(struct kvm_vcpu *vcpu, gva_t gva, unsigned long hva,
		       struct kvm_arch_async_pf *arch)
{
	struct kvm_async_pf *work;

	if (vcpu->async_pf.queued >= ASYNC_PF_PER_VCPU)
		return 0;

	/* setup delayed work */

	/*
	 * do alloc nowait since if we are going to sleep anyway we
	 * may as well sleep faulting in page
	 */
	work = kmem_cache_zalloc(async_pf_cache, GFP_NOWAIT);
	if (!work)
		return 0;

	work->wakeup_all = false;
	work->vcpu = vcpu;
	work->gva = gva;
	work->addr = hva;
	work->arch = *arch;
	work->mm = current->mm;
	atomic_inc(&work->mm->mm_users);
	kvm_get_kvm(work->vcpu->kvm);

	/* this can't really happen otherwise gfn_to_pfn_async
	   would succeed */
	if (unlikely(kvm_is_error_hva(work->addr)))
		goto retry_sync;

	INIT_WORK(&work->work, async_pf_execute);
	if (!schedule_work(&work->work))
		goto retry_sync;

	list_add_tail(&work->queue, &vcpu->async_pf.queue);
	vcpu->async_pf.queued++;
	kvm_arch_async_page_not_present(vcpu, work);
	return 1;
retry_sync:
	kvm_put_kvm(work->vcpu->kvm);
	mmput(work->mm);
	kmem_cache_free(async_pf_cache, work);
	return 0;
}

int kvm_async_pf_wakeup_all(struct kvm_vcpu *vcpu)
{
	struct kvm_async_pf *work;

	if (!list_empty_careful(&vcpu->async_pf.done))
		return 0;

	work = kmem_cache_zalloc(async_pf_cache, GFP_ATOMIC);
	if (!work)
		return -ENOMEM;

	work->wakeup_all = true;
	INIT_LIST_HEAD(&work->queue); /* for list_del to work */

	spin_lock(&vcpu->async_pf.lock);
	list_add_tail(&work->link, &vcpu->async_pf.done);
	spin_unlock(&vcpu->async_pf.lock);

	vcpu->async_pf.queued++;
	return 0;
}
