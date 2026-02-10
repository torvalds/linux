// SPDX-License-Identifier: GPL-2.0
/*
 * Code related to the io_uring_register() syscall
 *
 * Copyright (C) 2023 Jens Axboe
 */
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/syscalls.h>
#include <linux/refcount.h>
#include <linux/bits.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/nospec.h>
#include <linux/compat.h>
#include <linux/io_uring.h>
#include <linux/io_uring_types.h>

#include "filetable.h"
#include "io_uring.h"
#include "opdef.h"
#include "tctx.h"
#include "rsrc.h"
#include "sqpoll.h"
#include "register.h"
#include "cancel.h"
#include "kbuf.h"
#include "napi.h"
#include "eventfd.h"
#include "msg_ring.h"
#include "memmap.h"
#include "zcrx.h"
#include "query.h"
#include "bpf_filter.h"

#define IORING_MAX_RESTRICTIONS	(IORING_RESTRICTION_LAST + \
				 IORING_REGISTER_LAST + IORING_OP_LAST)

static __cold int io_probe(struct io_ring_ctx *ctx, void __user *arg,
			   unsigned nr_args)
{
	struct io_uring_probe *p;
	size_t size;
	int i, ret;

	if (nr_args > IORING_OP_LAST)
		nr_args = IORING_OP_LAST;

	size = struct_size(p, ops, nr_args);
	p = memdup_user(arg, size);
	if (IS_ERR(p))
		return PTR_ERR(p);
	ret = -EINVAL;
	if (memchr_inv(p, 0, size))
		goto out;

	p->last_op = IORING_OP_LAST - 1;

	for (i = 0; i < nr_args; i++) {
		p->ops[i].op = i;
		if (io_uring_op_supported(i))
			p->ops[i].flags = IO_URING_OP_SUPPORTED;
	}
	p->ops_len = i;

	ret = 0;
	if (copy_to_user(arg, p, size))
		ret = -EFAULT;
out:
	kfree(p);
	return ret;
}

int io_unregister_personality(struct io_ring_ctx *ctx, unsigned id)
{
	const struct cred *creds;

	creds = xa_erase(&ctx->personalities, id);
	if (creds) {
		put_cred(creds);
		return 0;
	}

	return -EINVAL;
}


static int io_register_personality(struct io_ring_ctx *ctx)
{
	const struct cred *creds;
	u32 id;
	int ret;

	creds = get_current_cred();

	ret = xa_alloc_cyclic(&ctx->personalities, &id, (void *)creds,
			XA_LIMIT(0, USHRT_MAX), &ctx->pers_next, GFP_KERNEL);
	if (ret < 0) {
		put_cred(creds);
		return ret;
	}
	return id;
}

/*
 * Returns number of restrictions parsed and added on success, or < 0 for
 * an error.
 */
static __cold int io_parse_restrictions(void __user *arg, unsigned int nr_args,
					struct io_restriction *restrictions)
{
	struct io_uring_restriction *res;
	size_t size;
	int i, ret;

	if (!arg || nr_args > IORING_MAX_RESTRICTIONS)
		return -EINVAL;

	size = array_size(nr_args, sizeof(*res));
	if (size == SIZE_MAX)
		return -EOVERFLOW;

	res = memdup_user(arg, size);
	if (IS_ERR(res))
		return PTR_ERR(res);

	ret = -EINVAL;

	for (i = 0; i < nr_args; i++) {
		switch (res[i].opcode) {
		case IORING_RESTRICTION_REGISTER_OP:
			if (res[i].register_op >= IORING_REGISTER_LAST)
				goto err;
			__set_bit(res[i].register_op, restrictions->register_op);
			restrictions->reg_registered = true;
			break;
		case IORING_RESTRICTION_SQE_OP:
			if (res[i].sqe_op >= IORING_OP_LAST)
				goto err;
			__set_bit(res[i].sqe_op, restrictions->sqe_op);
			restrictions->op_registered = true;
			break;
		case IORING_RESTRICTION_SQE_FLAGS_ALLOWED:
			restrictions->sqe_flags_allowed = res[i].sqe_flags;
			restrictions->op_registered = true;
			break;
		case IORING_RESTRICTION_SQE_FLAGS_REQUIRED:
			restrictions->sqe_flags_required = res[i].sqe_flags;
			restrictions->op_registered = true;
			break;
		default:
			goto err;
		}
	}
	ret = nr_args;
	if (!nr_args) {
		restrictions->op_registered = true;
		restrictions->reg_registered = true;
	}
err:
	kfree(res);
	return ret;
}

static __cold int io_register_restrictions(struct io_ring_ctx *ctx,
					   void __user *arg, unsigned int nr_args)
{
	int ret;

	/* Restrictions allowed only if rings started disabled */
	if (!(ctx->flags & IORING_SETUP_R_DISABLED))
		return -EBADFD;

	/* We allow only a single restrictions registration */
	if (ctx->restrictions.op_registered || ctx->restrictions.reg_registered)
		return -EBUSY;

	ret = io_parse_restrictions(arg, nr_args, &ctx->restrictions);
	/* Reset all restrictions if an error happened */
	if (ret < 0) {
		memset(&ctx->restrictions, 0, sizeof(ctx->restrictions));
		return ret;
	}
	if (ctx->restrictions.op_registered)
		ctx->op_restricted = 1;
	if (ctx->restrictions.reg_registered)
		ctx->reg_restricted = 1;
	return 0;
}

static int io_register_restrictions_task(void __user *arg, unsigned int nr_args)
{
	struct io_uring_task_restriction __user *ures = arg;
	struct io_uring_task_restriction tres;
	struct io_restriction *res;
	int ret;

	/* Disallow if task already has registered restrictions */
	if (current->io_uring_restrict)
		return -EPERM;
	/*
	 * Similar to seccomp, disallow setting a filter if task_no_new_privs
	 * is true and we're not CAP_SYS_ADMIN.
	 */
	if (!task_no_new_privs(current) &&
	    !ns_capable_noaudit(current_user_ns(), CAP_SYS_ADMIN))
		return -EACCES;
	if (nr_args != 1)
		return -EINVAL;

	if (copy_from_user(&tres, arg, sizeof(tres)))
		return -EFAULT;

	if (tres.flags)
		return -EINVAL;
	if (!mem_is_zero(tres.resv, sizeof(tres.resv)))
		return -EINVAL;

	res = kzalloc(sizeof(*res), GFP_KERNEL_ACCOUNT);
	if (!res)
		return -ENOMEM;

	ret = io_parse_restrictions(ures->restrictions, tres.nr_res, res);
	if (ret < 0) {
		kfree(res);
		return ret;
	}
	current->io_uring_restrict = res;
	return 0;
}

static int io_register_bpf_filter_task(void __user *arg, unsigned int nr_args)
{
	struct io_restriction *res;
	int ret;

	/*
	 * Similar to seccomp, disallow setting a filter if task_no_new_privs
	 * is true and we're not CAP_SYS_ADMIN.
	 */
	if (!task_no_new_privs(current) &&
	    !ns_capable_noaudit(current_user_ns(), CAP_SYS_ADMIN))
		return -EACCES;

	if (nr_args != 1)
		return -EINVAL;

	/* If no task restrictions exist, setup a new set */
	res = current->io_uring_restrict;
	if (!res) {
		res = kzalloc(sizeof(*res), GFP_KERNEL_ACCOUNT);
		if (!res)
			return -ENOMEM;
	}

	ret = io_register_bpf_filter(res, arg);
	if (ret) {
		if (res != current->io_uring_restrict)
			kfree(res);
		return ret;
	}
	if (!current->io_uring_restrict)
		current->io_uring_restrict = res;
	return 0;
}

static int io_register_enable_rings(struct io_ring_ctx *ctx)
{
	if (!(ctx->flags & IORING_SETUP_R_DISABLED))
		return -EBADFD;

	if (ctx->flags & IORING_SETUP_SINGLE_ISSUER) {
		ctx->submitter_task = get_task_struct(current);
		/*
		 * Lazy activation attempts would fail if it was polled before
		 * submitter_task is set.
		 */
		if (wq_has_sleeper(&ctx->poll_wq))
			io_activate_pollwq(ctx);
	}

	/* Keep submitter_task store before clearing IORING_SETUP_R_DISABLED */
	smp_store_release(&ctx->flags, ctx->flags & ~IORING_SETUP_R_DISABLED);
	if (ctx->sq_data && wq_has_sleeper(&ctx->sq_data->wait))
		wake_up(&ctx->sq_data->wait);
	return 0;
}

static __cold int __io_register_iowq_aff(struct io_ring_ctx *ctx,
					 cpumask_var_t new_mask)
{
	int ret;

	if (!(ctx->flags & IORING_SETUP_SQPOLL)) {
		ret = io_wq_cpu_affinity(current->io_uring, new_mask);
	} else {
		mutex_unlock(&ctx->uring_lock);
		ret = io_sqpoll_wq_cpu_affinity(ctx, new_mask);
		mutex_lock(&ctx->uring_lock);
	}

	return ret;
}

static __cold int io_register_iowq_aff(struct io_ring_ctx *ctx,
				       void __user *arg, unsigned len)
{
	cpumask_var_t new_mask;
	int ret;

	if (!alloc_cpumask_var(&new_mask, GFP_KERNEL))
		return -ENOMEM;

	cpumask_clear(new_mask);
	if (len > cpumask_size())
		len = cpumask_size();

#ifdef CONFIG_COMPAT
	if (in_compat_syscall())
		ret = compat_get_bitmap(cpumask_bits(new_mask),
					(const compat_ulong_t __user *)arg,
					len * 8 /* CHAR_BIT */);
	else
#endif
		ret = copy_from_user(new_mask, arg, len);

	if (ret) {
		free_cpumask_var(new_mask);
		return -EFAULT;
	}

	ret = __io_register_iowq_aff(ctx, new_mask);
	free_cpumask_var(new_mask);
	return ret;
}

static __cold int io_unregister_iowq_aff(struct io_ring_ctx *ctx)
{
	return __io_register_iowq_aff(ctx, NULL);
}

static __cold int io_register_iowq_max_workers(struct io_ring_ctx *ctx,
					       void __user *arg)
	__must_hold(&ctx->uring_lock)
{
	struct io_tctx_node *node;
	struct io_uring_task *tctx = NULL;
	struct io_sq_data *sqd = NULL;
	__u32 new_count[2];
	int i, ret;

	if (copy_from_user(new_count, arg, sizeof(new_count)))
		return -EFAULT;
	for (i = 0; i < ARRAY_SIZE(new_count); i++)
		if (new_count[i] > INT_MAX)
			return -EINVAL;

	if (ctx->flags & IORING_SETUP_SQPOLL) {
		sqd = ctx->sq_data;
		if (sqd) {
			struct task_struct *tsk;

			/*
			 * Observe the correct sqd->lock -> ctx->uring_lock
			 * ordering. Fine to drop uring_lock here, we hold
			 * a ref to the ctx.
			 */
			refcount_inc(&sqd->refs);
			mutex_unlock(&ctx->uring_lock);
			mutex_lock(&sqd->lock);
			mutex_lock(&ctx->uring_lock);
			tsk = sqpoll_task_locked(sqd);
			if (tsk)
				tctx = tsk->io_uring;
		}
	} else {
		tctx = current->io_uring;
	}

	BUILD_BUG_ON(sizeof(new_count) != sizeof(ctx->iowq_limits));

	for (i = 0; i < ARRAY_SIZE(new_count); i++)
		if (new_count[i])
			ctx->iowq_limits[i] = new_count[i];
	ctx->iowq_limits_set = true;

	if (tctx && tctx->io_wq) {
		ret = io_wq_max_workers(tctx->io_wq, new_count);
		if (ret)
			goto err;
	} else {
		memset(new_count, 0, sizeof(new_count));
	}

	if (sqd) {
		mutex_unlock(&ctx->uring_lock);
		mutex_unlock(&sqd->lock);
		io_put_sq_data(sqd);
		mutex_lock(&ctx->uring_lock);
	}

	if (copy_to_user(arg, new_count, sizeof(new_count)))
		return -EFAULT;

	/* that's it for SQPOLL, only the SQPOLL task creates requests */
	if (sqd)
		return 0;

	/* now propagate the restriction to all registered users */
	mutex_lock(&ctx->tctx_lock);
	list_for_each_entry(node, &ctx->tctx_list, ctx_node) {
		tctx = node->task->io_uring;
		if (WARN_ON_ONCE(!tctx->io_wq))
			continue;

		for (i = 0; i < ARRAY_SIZE(new_count); i++)
			new_count[i] = ctx->iowq_limits[i];
		/* ignore errors, it always returns zero anyway */
		(void)io_wq_max_workers(tctx->io_wq, new_count);
	}
	mutex_unlock(&ctx->tctx_lock);
	return 0;
err:
	if (sqd) {
		mutex_unlock(&ctx->uring_lock);
		mutex_unlock(&sqd->lock);
		io_put_sq_data(sqd);
		mutex_lock(&ctx->uring_lock);
	}
	return ret;
}

static int io_register_clock(struct io_ring_ctx *ctx,
			     struct io_uring_clock_register __user *arg)
{
	struct io_uring_clock_register reg;

	if (copy_from_user(&reg, arg, sizeof(reg)))
		return -EFAULT;
	if (memchr_inv(&reg.__resv, 0, sizeof(reg.__resv)))
		return -EINVAL;

	switch (reg.clockid) {
	case CLOCK_MONOTONIC:
		ctx->clock_offset = 0;
		break;
	case CLOCK_BOOTTIME:
		ctx->clock_offset = TK_OFFS_BOOT;
		break;
	default:
		return -EINVAL;
	}

	ctx->clockid = reg.clockid;
	return 0;
}

/*
 * State to maintain until we can swap. Both new and old state, used for
 * either mapping or freeing.
 */
struct io_ring_ctx_rings {
	struct io_rings *rings;
	struct io_uring_sqe *sq_sqes;

	struct io_mapped_region sq_region;
	struct io_mapped_region ring_region;
};

static void io_register_free_rings(struct io_ring_ctx *ctx,
				   struct io_ring_ctx_rings *r)
{
	io_free_region(ctx->user, &r->sq_region);
	io_free_region(ctx->user, &r->ring_region);
}

#define swap_old(ctx, o, n, field)		\
	do {					\
		(o).field = (ctx)->field;	\
		(ctx)->field = (n).field;	\
	} while (0)

#define RESIZE_FLAGS	(IORING_SETUP_CQSIZE | IORING_SETUP_CLAMP)
#define COPY_FLAGS	(IORING_SETUP_NO_SQARRAY | IORING_SETUP_SQE128 | \
			 IORING_SETUP_CQE32 | IORING_SETUP_NO_MMAP | \
			 IORING_SETUP_CQE_MIXED | IORING_SETUP_SQE_MIXED)

static int io_register_resize_rings(struct io_ring_ctx *ctx, void __user *arg)
{
	struct io_ctx_config config;
	struct io_uring_region_desc rd;
	struct io_ring_ctx_rings o = { }, n = { }, *to_free = NULL;
	unsigned i, tail, old_head;
	struct io_uring_params *p = &config.p;
	struct io_rings_layout *rl = &config.layout;
	int ret;

	memset(&config, 0, sizeof(config));

	/* limited to DEFER_TASKRUN for now */
	if (!(ctx->flags & IORING_SETUP_DEFER_TASKRUN))
		return -EINVAL;
	if (copy_from_user(p, arg, sizeof(*p)))
		return -EFAULT;
	if (p->flags & ~RESIZE_FLAGS)
		return -EINVAL;

	/* properties that are always inherited */
	p->flags |= (ctx->flags & COPY_FLAGS);

	ret = io_prepare_config(&config);
	if (unlikely(ret))
		return ret;

	memset(&rd, 0, sizeof(rd));
	rd.size = PAGE_ALIGN(rl->rings_size);
	if (p->flags & IORING_SETUP_NO_MMAP) {
		rd.user_addr = p->cq_off.user_addr;
		rd.flags |= IORING_MEM_REGION_TYPE_USER;
	}
	ret = io_create_region(ctx, &n.ring_region, &rd, IORING_OFF_CQ_RING);
	if (ret)
		return ret;

	n.rings = io_region_get_ptr(&n.ring_region);

	/*
	 * At this point n.rings is shared with userspace, just like o.rings
	 * is as well. While we don't expect userspace to modify it while
	 * a resize is in progress, and it's most likely that userspace will
	 * shoot itself in the foot if it does, we can't always assume good
	 * intent... Use read/write once helpers from here on to indicate the
	 * shared nature of it.
	 */
	WRITE_ONCE(n.rings->sq_ring_mask, p->sq_entries - 1);
	WRITE_ONCE(n.rings->cq_ring_mask, p->cq_entries - 1);
	WRITE_ONCE(n.rings->sq_ring_entries, p->sq_entries);
	WRITE_ONCE(n.rings->cq_ring_entries, p->cq_entries);

	if (copy_to_user(arg, p, sizeof(*p))) {
		io_register_free_rings(ctx, &n);
		return -EFAULT;
	}

	memset(&rd, 0, sizeof(rd));
	rd.size = PAGE_ALIGN(rl->sq_size);
	if (p->flags & IORING_SETUP_NO_MMAP) {
		rd.user_addr = p->sq_off.user_addr;
		rd.flags |= IORING_MEM_REGION_TYPE_USER;
	}
	ret = io_create_region(ctx, &n.sq_region, &rd, IORING_OFF_SQES);
	if (ret) {
		io_register_free_rings(ctx, &n);
		return ret;
	}
	n.sq_sqes = io_region_get_ptr(&n.sq_region);

	/*
	 * If using SQPOLL, park the thread
	 */
	if (ctx->sq_data) {
		mutex_unlock(&ctx->uring_lock);
		io_sq_thread_park(ctx->sq_data);
		mutex_lock(&ctx->uring_lock);
	}

	/*
	 * We'll do the swap. Grab the ctx->mmap_lock, which will exclude
	 * any new mmap's on the ring fd. Clear out existing mappings to prevent
	 * mmap from seeing them, as we'll unmap them. Any attempt to mmap
	 * existing rings beyond this point will fail. Not that it could proceed
	 * at this point anyway, as the io_uring mmap side needs go grab the
	 * ctx->mmap_lock as well. Likewise, hold the completion lock over the
	 * duration of the actual swap.
	 */
	mutex_lock(&ctx->mmap_lock);
	spin_lock(&ctx->completion_lock);
	o.rings = ctx->rings;
	ctx->rings = NULL;
	o.sq_sqes = ctx->sq_sqes;
	ctx->sq_sqes = NULL;

	/*
	 * Now copy SQ and CQ entries, if any. If either of the destination
	 * rings can't hold what is already there, then fail the operation.
	 */
	tail = READ_ONCE(o.rings->sq.tail);
	old_head = READ_ONCE(o.rings->sq.head);
	if (tail - old_head > p->sq_entries)
		goto overflow;
	for (i = old_head; i < tail; i++) {
		unsigned src_head = i & (ctx->sq_entries - 1);
		unsigned dst_head = i & (p->sq_entries - 1);

		n.sq_sqes[dst_head] = o.sq_sqes[src_head];
	}
	WRITE_ONCE(n.rings->sq.head, old_head);
	WRITE_ONCE(n.rings->sq.tail, tail);

	tail = READ_ONCE(o.rings->cq.tail);
	old_head = READ_ONCE(o.rings->cq.head);
	if (tail - old_head > p->cq_entries) {
overflow:
		/* restore old rings, and return -EOVERFLOW via cleanup path */
		ctx->rings = o.rings;
		ctx->sq_sqes = o.sq_sqes;
		to_free = &n;
		ret = -EOVERFLOW;
		goto out;
	}
	for (i = old_head; i < tail; i++) {
		unsigned src_head = i & (ctx->cq_entries - 1);
		unsigned dst_head = i & (p->cq_entries - 1);

		n.rings->cqes[dst_head] = o.rings->cqes[src_head];
	}
	WRITE_ONCE(n.rings->cq.head, old_head);
	WRITE_ONCE(n.rings->cq.tail, tail);
	/* invalidate cached cqe refill */
	ctx->cqe_cached = ctx->cqe_sentinel = NULL;

	WRITE_ONCE(n.rings->sq_dropped, READ_ONCE(o.rings->sq_dropped));
	atomic_set(&n.rings->sq_flags, atomic_read(&o.rings->sq_flags));
	WRITE_ONCE(n.rings->cq_flags, READ_ONCE(o.rings->cq_flags));
	WRITE_ONCE(n.rings->cq_overflow, READ_ONCE(o.rings->cq_overflow));

	/* all done, store old pointers and assign new ones */
	if (!(ctx->flags & IORING_SETUP_NO_SQARRAY))
		ctx->sq_array = (u32 *)((char *)n.rings + rl->sq_array_offset);

	ctx->sq_entries = p->sq_entries;
	ctx->cq_entries = p->cq_entries;

	ctx->rings = n.rings;
	ctx->sq_sqes = n.sq_sqes;
	swap_old(ctx, o, n, ring_region);
	swap_old(ctx, o, n, sq_region);
	to_free = &o;
	ret = 0;
out:
	spin_unlock(&ctx->completion_lock);
	mutex_unlock(&ctx->mmap_lock);
	io_register_free_rings(ctx, to_free);

	if (ctx->sq_data)
		io_sq_thread_unpark(ctx->sq_data);

	return ret;
}

static int io_register_mem_region(struct io_ring_ctx *ctx, void __user *uarg)
{
	struct io_uring_mem_region_reg __user *reg_uptr = uarg;
	struct io_uring_mem_region_reg reg;
	struct io_uring_region_desc __user *rd_uptr;
	struct io_uring_region_desc rd;
	struct io_mapped_region region = {};
	int ret;

	if (io_region_is_set(&ctx->param_region))
		return -EBUSY;
	if (copy_from_user(&reg, reg_uptr, sizeof(reg)))
		return -EFAULT;
	rd_uptr = u64_to_user_ptr(reg.region_uptr);
	if (copy_from_user(&rd, rd_uptr, sizeof(rd)))
		return -EFAULT;
	if (memchr_inv(&reg.__resv, 0, sizeof(reg.__resv)))
		return -EINVAL;
	if (reg.flags & ~IORING_MEM_REGION_REG_WAIT_ARG)
		return -EINVAL;

	/*
	 * This ensures there are no waiters. Waiters are unlocked and it's
	 * hard to synchronise with them, especially if we need to initialise
	 * the region.
	 */
	if ((reg.flags & IORING_MEM_REGION_REG_WAIT_ARG) &&
	    !(ctx->flags & IORING_SETUP_R_DISABLED))
		return -EINVAL;

	ret = io_create_region(ctx, &region, &rd, IORING_MAP_OFF_PARAM_REGION);
	if (ret)
		return ret;
	if (copy_to_user(rd_uptr, &rd, sizeof(rd))) {
		io_free_region(ctx->user, &region);
		return -EFAULT;
	}

	if (reg.flags & IORING_MEM_REGION_REG_WAIT_ARG) {
		ctx->cq_wait_arg = io_region_get_ptr(&region);
		ctx->cq_wait_size = rd.size;
	}

	io_region_publish(ctx, &region, &ctx->param_region);
	return 0;
}

static int __io_uring_register(struct io_ring_ctx *ctx, unsigned opcode,
			       void __user *arg, unsigned nr_args)
	__releases(ctx->uring_lock)
	__acquires(ctx->uring_lock)
{
	int ret;

	/*
	 * We don't quiesce the refs for register anymore and so it can't be
	 * dying as we're holding a file ref here.
	 */
	if (WARN_ON_ONCE(percpu_ref_is_dying(&ctx->refs)))
		return -ENXIO;

	if (ctx->submitter_task && ctx->submitter_task != current)
		return -EEXIST;

	if (ctx->reg_restricted && !(ctx->flags & IORING_SETUP_R_DISABLED)) {
		opcode = array_index_nospec(opcode, IORING_REGISTER_LAST);
		if (!test_bit(opcode, ctx->restrictions.register_op))
			return -EACCES;
	}

	switch (opcode) {
	case IORING_REGISTER_BUFFERS:
		ret = -EFAULT;
		if (!arg)
			break;
		ret = io_sqe_buffers_register(ctx, arg, nr_args, NULL);
		break;
	case IORING_UNREGISTER_BUFFERS:
		ret = -EINVAL;
		if (arg || nr_args)
			break;
		ret = io_sqe_buffers_unregister(ctx);
		break;
	case IORING_REGISTER_FILES:
		ret = -EFAULT;
		if (!arg)
			break;
		ret = io_sqe_files_register(ctx, arg, nr_args, NULL);
		break;
	case IORING_UNREGISTER_FILES:
		ret = -EINVAL;
		if (arg || nr_args)
			break;
		ret = io_sqe_files_unregister(ctx);
		break;
	case IORING_REGISTER_FILES_UPDATE:
		ret = io_register_files_update(ctx, arg, nr_args);
		break;
	case IORING_REGISTER_EVENTFD:
		ret = -EINVAL;
		if (nr_args != 1)
			break;
		ret = io_eventfd_register(ctx, arg, 0);
		break;
	case IORING_REGISTER_EVENTFD_ASYNC:
		ret = -EINVAL;
		if (nr_args != 1)
			break;
		ret = io_eventfd_register(ctx, arg, 1);
		break;
	case IORING_UNREGISTER_EVENTFD:
		ret = -EINVAL;
		if (arg || nr_args)
			break;
		ret = io_eventfd_unregister(ctx);
		break;
	case IORING_REGISTER_PROBE:
		ret = -EINVAL;
		if (!arg || nr_args > 256)
			break;
		ret = io_probe(ctx, arg, nr_args);
		break;
	case IORING_REGISTER_PERSONALITY:
		ret = -EINVAL;
		if (arg || nr_args)
			break;
		ret = io_register_personality(ctx);
		break;
	case IORING_UNREGISTER_PERSONALITY:
		ret = -EINVAL;
		if (arg)
			break;
		ret = io_unregister_personality(ctx, nr_args);
		break;
	case IORING_REGISTER_ENABLE_RINGS:
		ret = -EINVAL;
		if (arg || nr_args)
			break;
		ret = io_register_enable_rings(ctx);
		break;
	case IORING_REGISTER_RESTRICTIONS:
		ret = io_register_restrictions(ctx, arg, nr_args);
		break;
	case IORING_REGISTER_FILES2:
		ret = io_register_rsrc(ctx, arg, nr_args, IORING_RSRC_FILE);
		break;
	case IORING_REGISTER_FILES_UPDATE2:
		ret = io_register_rsrc_update(ctx, arg, nr_args,
					      IORING_RSRC_FILE);
		break;
	case IORING_REGISTER_BUFFERS2:
		ret = io_register_rsrc(ctx, arg, nr_args, IORING_RSRC_BUFFER);
		break;
	case IORING_REGISTER_BUFFERS_UPDATE:
		ret = io_register_rsrc_update(ctx, arg, nr_args,
					      IORING_RSRC_BUFFER);
		break;
	case IORING_REGISTER_IOWQ_AFF:
		ret = -EINVAL;
		if (!arg || !nr_args)
			break;
		ret = io_register_iowq_aff(ctx, arg, nr_args);
		break;
	case IORING_UNREGISTER_IOWQ_AFF:
		ret = -EINVAL;
		if (arg || nr_args)
			break;
		ret = io_unregister_iowq_aff(ctx);
		break;
	case IORING_REGISTER_IOWQ_MAX_WORKERS:
		ret = -EINVAL;
		if (!arg || nr_args != 2)
			break;
		ret = io_register_iowq_max_workers(ctx, arg);
		break;
	case IORING_REGISTER_RING_FDS:
		ret = io_ringfd_register(ctx, arg, nr_args);
		break;
	case IORING_UNREGISTER_RING_FDS:
		ret = io_ringfd_unregister(ctx, arg, nr_args);
		break;
	case IORING_REGISTER_PBUF_RING:
		ret = -EINVAL;
		if (!arg || nr_args != 1)
			break;
		ret = io_register_pbuf_ring(ctx, arg);
		break;
	case IORING_UNREGISTER_PBUF_RING:
		ret = -EINVAL;
		if (!arg || nr_args != 1)
			break;
		ret = io_unregister_pbuf_ring(ctx, arg);
		break;
	case IORING_REGISTER_SYNC_CANCEL:
		ret = -EINVAL;
		if (!arg || nr_args != 1)
			break;
		ret = io_sync_cancel(ctx, arg);
		break;
	case IORING_REGISTER_FILE_ALLOC_RANGE:
		ret = -EINVAL;
		if (!arg || nr_args)
			break;
		ret = io_register_file_alloc_range(ctx, arg);
		break;
	case IORING_REGISTER_PBUF_STATUS:
		ret = -EINVAL;
		if (!arg || nr_args != 1)
			break;
		ret = io_register_pbuf_status(ctx, arg);
		break;
	case IORING_REGISTER_NAPI:
		ret = -EINVAL;
		if (!arg || nr_args != 1)
			break;
		ret = io_register_napi(ctx, arg);
		break;
	case IORING_UNREGISTER_NAPI:
		ret = -EINVAL;
		if (nr_args != 1)
			break;
		ret = io_unregister_napi(ctx, arg);
		break;
	case IORING_REGISTER_CLOCK:
		ret = -EINVAL;
		if (!arg || nr_args)
			break;
		ret = io_register_clock(ctx, arg);
		break;
	case IORING_REGISTER_CLONE_BUFFERS:
		ret = -EINVAL;
		if (!arg || nr_args != 1)
			break;
		ret = io_register_clone_buffers(ctx, arg);
		break;
	case IORING_REGISTER_ZCRX_IFQ:
		ret = -EINVAL;
		if (!arg || nr_args != 1)
			break;
		ret = io_register_zcrx_ifq(ctx, arg);
		break;
	case IORING_REGISTER_RESIZE_RINGS:
		ret = -EINVAL;
		if (!arg || nr_args != 1)
			break;
		ret = io_register_resize_rings(ctx, arg);
		break;
	case IORING_REGISTER_MEM_REGION:
		ret = -EINVAL;
		if (!arg || nr_args != 1)
			break;
		ret = io_register_mem_region(ctx, arg);
		break;
	case IORING_REGISTER_QUERY:
		ret = io_query(arg, nr_args);
		break;
	case IORING_REGISTER_ZCRX_CTRL:
		ret = io_zcrx_ctrl(ctx, arg, nr_args);
		break;
	case IORING_REGISTER_BPF_FILTER:
		ret = -EINVAL;

		if (nr_args != 1)
			break;
		ret = io_register_bpf_filter(&ctx->restrictions, arg);
		if (!ret)
			WRITE_ONCE(ctx->bpf_filters,
				   ctx->restrictions.bpf_filters->filters);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

/*
 * Given an 'fd' value, return the ctx associated with if. If 'registered' is
 * true, then the registered index is used. Otherwise, the normal fd table.
 * Caller must call fput() on the returned file, unless it's an ERR_PTR.
 */
struct file *io_uring_register_get_file(unsigned int fd, bool registered)
{
	struct file *file;

	if (registered) {
		/*
		 * Ring fd has been registered via IORING_REGISTER_RING_FDS, we
		 * need only dereference our task private array to find it.
		 */
		struct io_uring_task *tctx = current->io_uring;

		if (unlikely(!tctx || fd >= IO_RINGFD_REG_MAX))
			return ERR_PTR(-EINVAL);
		fd = array_index_nospec(fd, IO_RINGFD_REG_MAX);
		file = tctx->registered_rings[fd];
		if (file)
			get_file(file);
	} else {
		file = fget(fd);
	}

	if (unlikely(!file))
		return ERR_PTR(-EBADF);
	if (io_is_uring_fops(file))
		return file;
	fput(file);
	return ERR_PTR(-EOPNOTSUPP);
}

static int io_uring_register_send_msg_ring(void __user *arg, unsigned int nr_args)
{
	struct io_uring_sqe sqe;

	if (!arg || nr_args != 1)
		return -EINVAL;
	if (copy_from_user(&sqe, arg, sizeof(sqe)))
		return -EFAULT;
	/* no flags supported */
	if (sqe.flags)
		return -EINVAL;
	if (sqe.opcode != IORING_OP_MSG_RING)
		return -EINVAL;

	return io_uring_sync_msg_ring(&sqe);
}

/*
 * "blind" registration opcodes are ones where there's no ring given, and
 * hence the source fd must be -1.
 */
static int io_uring_register_blind(unsigned int opcode, void __user *arg,
				   unsigned int nr_args)
{
	switch (opcode) {
	case IORING_REGISTER_SEND_MSG_RING:
		return io_uring_register_send_msg_ring(arg, nr_args);
	case IORING_REGISTER_QUERY:
		return io_query(arg, nr_args);
	case IORING_REGISTER_RESTRICTIONS:
		return io_register_restrictions_task(arg, nr_args);
	case IORING_REGISTER_BPF_FILTER:
		return io_register_bpf_filter_task(arg, nr_args);
	}
	return -EINVAL;
}

SYSCALL_DEFINE4(io_uring_register, unsigned int, fd, unsigned int, opcode,
		void __user *, arg, unsigned int, nr_args)
{
	struct io_ring_ctx *ctx;
	long ret = -EBADF;
	struct file *file;
	bool use_registered_ring;

	use_registered_ring = !!(opcode & IORING_REGISTER_USE_REGISTERED_RING);
	opcode &= ~IORING_REGISTER_USE_REGISTERED_RING;

	if (opcode >= IORING_REGISTER_LAST)
		return -EINVAL;

	if (fd == -1)
		return io_uring_register_blind(opcode, arg, nr_args);

	file = io_uring_register_get_file(fd, use_registered_ring);
	if (IS_ERR(file))
		return PTR_ERR(file);
	ctx = file->private_data;

	mutex_lock(&ctx->uring_lock);
	ret = __io_uring_register(ctx, opcode, arg, nr_args);

	trace_io_uring_register(ctx, opcode, ctx->file_table.data.nr,
				ctx->buf_table.nr, ret);
	mutex_unlock(&ctx->uring_lock);

	fput(file);
	return ret;
}
