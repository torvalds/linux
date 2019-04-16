// SPDX-License-Identifier: GPL-2.0
/* XDP user-space packet buffer
 * Copyright(c) 2018 Intel Corporation.
 */

#include <linux/init.h>
#include <linux/sched/mm.h>
#include <linux/sched/signal.h>
#include <linux/sched/task.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/bpf.h>
#include <linux/mm.h>
#include <linux/netdevice.h>
#include <linux/rtnetlink.h>
#include <linux/idr.h>

#include "xdp_umem.h"
#include "xsk_queue.h"

#define XDP_UMEM_MIN_CHUNK_SIZE 2048

static DEFINE_IDA(umem_ida);

void xdp_add_sk_umem(struct xdp_umem *umem, struct xdp_sock *xs)
{
	unsigned long flags;

	spin_lock_irqsave(&umem->xsk_list_lock, flags);
	list_add_rcu(&xs->list, &umem->xsk_list);
	spin_unlock_irqrestore(&umem->xsk_list_lock, flags);
}

void xdp_del_sk_umem(struct xdp_umem *umem, struct xdp_sock *xs)
{
	unsigned long flags;

	spin_lock_irqsave(&umem->xsk_list_lock, flags);
	list_del_rcu(&xs->list);
	spin_unlock_irqrestore(&umem->xsk_list_lock, flags);
}

/* The umem is stored both in the _rx struct and the _tx struct as we do
 * not know if the device has more tx queues than rx, or the opposite.
 * This might also change during run time.
 */
static int xdp_reg_umem_at_qid(struct net_device *dev, struct xdp_umem *umem,
			       u16 queue_id)
{
	if (queue_id >= max_t(unsigned int,
			      dev->real_num_rx_queues,
			      dev->real_num_tx_queues))
		return -EINVAL;

	if (queue_id < dev->real_num_rx_queues)
		dev->_rx[queue_id].umem = umem;
	if (queue_id < dev->real_num_tx_queues)
		dev->_tx[queue_id].umem = umem;

	return 0;
}

struct xdp_umem *xdp_get_umem_from_qid(struct net_device *dev,
				       u16 queue_id)
{
	if (queue_id < dev->real_num_rx_queues)
		return dev->_rx[queue_id].umem;
	if (queue_id < dev->real_num_tx_queues)
		return dev->_tx[queue_id].umem;

	return NULL;
}
EXPORT_SYMBOL(xdp_get_umem_from_qid);

static void xdp_clear_umem_at_qid(struct net_device *dev, u16 queue_id)
{
	if (queue_id < dev->real_num_rx_queues)
		dev->_rx[queue_id].umem = NULL;
	if (queue_id < dev->real_num_tx_queues)
		dev->_tx[queue_id].umem = NULL;
}

int xdp_umem_assign_dev(struct xdp_umem *umem, struct net_device *dev,
			u16 queue_id, u16 flags)
{
	bool force_zc, force_copy;
	struct netdev_bpf bpf;
	int err = 0;

	force_zc = flags & XDP_ZEROCOPY;
	force_copy = flags & XDP_COPY;

	if (force_zc && force_copy)
		return -EINVAL;

	rtnl_lock();
	if (xdp_get_umem_from_qid(dev, queue_id)) {
		err = -EBUSY;
		goto out_rtnl_unlock;
	}

	err = xdp_reg_umem_at_qid(dev, umem, queue_id);
	if (err)
		goto out_rtnl_unlock;

	umem->dev = dev;
	umem->queue_id = queue_id;
	if (force_copy)
		/* For copy-mode, we are done. */
		goto out_rtnl_unlock;

	if (!dev->netdev_ops->ndo_bpf ||
	    !dev->netdev_ops->ndo_xsk_async_xmit) {
		err = -EOPNOTSUPP;
		goto err_unreg_umem;
	}

	bpf.command = XDP_SETUP_XSK_UMEM;
	bpf.xsk.umem = umem;
	bpf.xsk.queue_id = queue_id;

	err = dev->netdev_ops->ndo_bpf(dev, &bpf);
	if (err)
		goto err_unreg_umem;
	rtnl_unlock();

	dev_hold(dev);
	umem->zc = true;
	return 0;

err_unreg_umem:
	if (!force_zc)
		err = 0; /* fallback to copy mode */
	if (err)
		xdp_clear_umem_at_qid(dev, queue_id);
out_rtnl_unlock:
	rtnl_unlock();
	return err;
}

static void xdp_umem_clear_dev(struct xdp_umem *umem)
{
	struct netdev_bpf bpf;
	int err;

	if (umem->zc) {
		bpf.command = XDP_SETUP_XSK_UMEM;
		bpf.xsk.umem = NULL;
		bpf.xsk.queue_id = umem->queue_id;

		rtnl_lock();
		err = umem->dev->netdev_ops->ndo_bpf(umem->dev, &bpf);
		rtnl_unlock();

		if (err)
			WARN(1, "failed to disable umem!\n");
	}

	if (umem->dev) {
		rtnl_lock();
		xdp_clear_umem_at_qid(umem->dev, umem->queue_id);
		rtnl_unlock();
	}

	if (umem->zc) {
		dev_put(umem->dev);
		umem->zc = false;
	}
}

static void xdp_umem_unpin_pages(struct xdp_umem *umem)
{
	unsigned int i;

	for (i = 0; i < umem->npgs; i++) {
		struct page *page = umem->pgs[i];

		set_page_dirty_lock(page);
		put_page(page);
	}

	kfree(umem->pgs);
	umem->pgs = NULL;
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

	xdp_umem_clear_dev(umem);

	ida_simple_remove(&umem_ida, umem->id);

	if (umem->fq) {
		xskq_destroy(umem->fq);
		umem->fq = NULL;
	}

	if (umem->cq) {
		xskq_destroy(umem->cq);
		umem->cq = NULL;
	}

	xsk_reuseq_destroy(umem);

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
	kfree(umem->pages);
	umem->pages = NULL;

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
	refcount_inc(&umem->users);
}

void xdp_put_umem(struct xdp_umem *umem)
{
	if (!umem)
		return;

	if (refcount_dec_and_test(&umem->users)) {
		INIT_WORK(&umem->work, xdp_umem_release_deferred);
		schedule_work(&umem->work);
	}
}

static int xdp_umem_pin_pages(struct xdp_umem *umem)
{
	unsigned int gup_flags = FOLL_WRITE;
	long npgs;
	int err;

	umem->pgs = kcalloc(umem->npgs, sizeof(*umem->pgs),
			    GFP_KERNEL | __GFP_NOWARN);
	if (!umem->pgs)
		return -ENOMEM;

	down_read(&current->mm->mmap_sem);
	npgs = get_user_pages_longterm(umem->address, umem->npgs,
				       gup_flags, &umem->pgs[0], NULL);
	up_read(&current->mm->mmap_sem);

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

static int xdp_umem_reg(struct xdp_umem *umem, struct xdp_umem_reg *mr)
{
	u32 chunk_size = mr->chunk_size, headroom = mr->headroom;
	unsigned int chunks, chunks_per_page;
	u64 addr = mr->addr, size = mr->len;
	int size_chk, err, i;

	if (chunk_size < XDP_UMEM_MIN_CHUNK_SIZE || chunk_size > PAGE_SIZE) {
		/* Strictly speaking we could support this, if:
		 * - huge pages, or*
		 * - using an IOMMU, or
		 * - making sure the memory area is consecutive
		 * but for now, we simply say "computer says no".
		 */
		return -EINVAL;
	}

	if (!is_power_of_2(chunk_size))
		return -EINVAL;

	if (!PAGE_ALIGNED(addr)) {
		/* Memory area has to be page size aligned. For
		 * simplicity, this might change.
		 */
		return -EINVAL;
	}

	if ((addr + size) < addr)
		return -EINVAL;

	chunks = (unsigned int)div_u64(size, chunk_size);
	if (chunks == 0)
		return -EINVAL;

	chunks_per_page = PAGE_SIZE / chunk_size;
	if (chunks < chunks_per_page || chunks % chunks_per_page)
		return -EINVAL;

	headroom = ALIGN(headroom, 64);

	size_chk = chunk_size - headroom - XDP_PACKET_HEADROOM;
	if (size_chk < 0)
		return -EINVAL;

	umem->pid = get_task_pid(current, PIDTYPE_PID);
	umem->address = (unsigned long)addr;
	umem->chunk_mask = ~((u64)chunk_size - 1);
	umem->size = size;
	umem->headroom = headroom;
	umem->chunk_size_nohr = chunk_size - headroom;
	umem->npgs = size / PAGE_SIZE;
	umem->pgs = NULL;
	umem->user = NULL;
	INIT_LIST_HEAD(&umem->xsk_list);
	spin_lock_init(&umem->xsk_list_lock);

	refcount_set(&umem->users, 1);

	err = xdp_umem_account_pages(umem);
	if (err)
		goto out;

	err = xdp_umem_pin_pages(umem);
	if (err)
		goto out_account;

	umem->pages = kcalloc(umem->npgs, sizeof(*umem->pages), GFP_KERNEL);
	if (!umem->pages) {
		err = -ENOMEM;
		goto out_account;
	}

	for (i = 0; i < umem->npgs; i++)
		umem->pages[i].addr = page_address(umem->pgs[i]);

	return 0;

out_account:
	xdp_umem_unaccount_pages(umem);
out:
	put_pid(umem->pid);
	return err;
}

struct xdp_umem *xdp_umem_create(struct xdp_umem_reg *mr)
{
	struct xdp_umem *umem;
	int err;

	umem = kzalloc(sizeof(*umem), GFP_KERNEL);
	if (!umem)
		return ERR_PTR(-ENOMEM);

	err = ida_simple_get(&umem_ida, 0, 0, GFP_KERNEL);
	if (err < 0) {
		kfree(umem);
		return ERR_PTR(err);
	}
	umem->id = err;

	err = xdp_umem_reg(umem, mr);
	if (err) {
		ida_simple_remove(&umem_ida, umem->id);
		kfree(umem);
		return ERR_PTR(err);
	}

	return umem;
}

bool xdp_umem_validate_queues(struct xdp_umem *umem)
{
	return umem->fq && umem->cq;
}
