// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2017 Facebook
 */
#include <linux/bpf.h>
#include <linux/btf.h>
#include <linux/btf_ids.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/vmalloc.h>
#include <linux/etherdevice.h>
#include <linux/filter.h>
#include <linux/rcupdate_trace.h>
#include <linux/sched/signal.h>
#include <net/bpf_sk_storage.h>
#include <net/sock.h>
#include <net/tcp.h>
#include <net/net_namespace.h>
#include <net/page_pool/helpers.h>
#include <linux/error-injection.h>
#include <linux/smp.h>
#include <linux/sock_diag.h>
#include <linux/netfilter.h>
#include <net/netdev_rx_queue.h>
#include <net/xdp.h>
#include <net/netfilter/nf_bpf_link.h>

#define CREATE_TRACE_POINTS
#include <trace/events/bpf_test_run.h>

struct bpf_test_timer {
	enum { NO_PREEMPT, NO_MIGRATE } mode;
	u32 i;
	u64 time_start, time_spent;
};

static void bpf_test_timer_enter(struct bpf_test_timer *t)
	__acquires(rcu)
{
	rcu_read_lock();
	if (t->mode == NO_PREEMPT)
		preempt_disable();
	else
		migrate_disable();

	t->time_start = ktime_get_ns();
}

static void bpf_test_timer_leave(struct bpf_test_timer *t)
	__releases(rcu)
{
	t->time_start = 0;

	if (t->mode == NO_PREEMPT)
		preempt_enable();
	else
		migrate_enable();
	rcu_read_unlock();
}

static bool bpf_test_timer_continue(struct bpf_test_timer *t, int iterations,
				    u32 repeat, int *err, u32 *duration)
	__must_hold(rcu)
{
	t->i += iterations;
	if (t->i >= repeat) {
		/* We're done. */
		t->time_spent += ktime_get_ns() - t->time_start;
		do_div(t->time_spent, t->i);
		*duration = t->time_spent > U32_MAX ? U32_MAX : (u32)t->time_spent;
		*err = 0;
		goto reset;
	}

	if (signal_pending(current)) {
		/* During iteration: we've been cancelled, abort. */
		*err = -EINTR;
		goto reset;
	}

	if (need_resched()) {
		/* During iteration: we need to reschedule between runs. */
		t->time_spent += ktime_get_ns() - t->time_start;
		bpf_test_timer_leave(t);
		cond_resched();
		bpf_test_timer_enter(t);
	}

	/* Do another round. */
	return true;

reset:
	t->i = 0;
	return false;
}

/* We put this struct at the head of each page with a context and frame
 * initialised when the page is allocated, so we don't have to do this on each
 * repetition of the test run.
 */
struct xdp_page_head {
	struct xdp_buff orig_ctx;
	struct xdp_buff ctx;
	union {
		/* ::data_hard_start starts here */
		DECLARE_FLEX_ARRAY(struct xdp_frame, frame);
		DECLARE_FLEX_ARRAY(u8, data);
	};
};

struct xdp_test_data {
	struct xdp_buff *orig_ctx;
	struct xdp_rxq_info rxq;
	struct net_device *dev;
	struct page_pool *pp;
	struct xdp_frame **frames;
	struct sk_buff **skbs;
	struct xdp_mem_info mem;
	u32 batch_size;
	u32 frame_cnt;
};

/* tools/testing/selftests/bpf/prog_tests/xdp_do_redirect.c:%MAX_PKT_SIZE
 * must be updated accordingly this gets changed, otherwise BPF selftests
 * will fail.
 */
#define TEST_XDP_FRAME_SIZE (PAGE_SIZE - sizeof(struct xdp_page_head))
#define TEST_XDP_MAX_BATCH 256

static void xdp_test_run_init_page(struct page *page, void *arg)
{
	struct xdp_page_head *head = phys_to_virt(page_to_phys(page));
	struct xdp_buff *new_ctx, *orig_ctx;
	u32 headroom = XDP_PACKET_HEADROOM;
	struct xdp_test_data *xdp = arg;
	size_t frm_len, meta_len;
	struct xdp_frame *frm;
	void *data;

	orig_ctx = xdp->orig_ctx;
	frm_len = orig_ctx->data_end - orig_ctx->data_meta;
	meta_len = orig_ctx->data - orig_ctx->data_meta;
	headroom -= meta_len;

	new_ctx = &head->ctx;
	frm = head->frame;
	data = head->data;
	memcpy(data + headroom, orig_ctx->data_meta, frm_len);

	xdp_init_buff(new_ctx, TEST_XDP_FRAME_SIZE, &xdp->rxq);
	xdp_prepare_buff(new_ctx, data, headroom, frm_len, true);
	new_ctx->data = new_ctx->data_meta + meta_len;

	xdp_update_frame_from_buff(new_ctx, frm);
	frm->mem = new_ctx->rxq->mem;

	memcpy(&head->orig_ctx, new_ctx, sizeof(head->orig_ctx));
}

static int xdp_test_run_setup(struct xdp_test_data *xdp, struct xdp_buff *orig_ctx)
{
	struct page_pool *pp;
	int err = -ENOMEM;
	struct page_pool_params pp_params = {
		.order = 0,
		.flags = 0,
		.pool_size = xdp->batch_size,
		.nid = NUMA_NO_NODE,
		.init_callback = xdp_test_run_init_page,
		.init_arg = xdp,
	};

	xdp->frames = kvmalloc_array(xdp->batch_size, sizeof(void *), GFP_KERNEL);
	if (!xdp->frames)
		return -ENOMEM;

	xdp->skbs = kvmalloc_array(xdp->batch_size, sizeof(void *), GFP_KERNEL);
	if (!xdp->skbs)
		goto err_skbs;

	pp = page_pool_create(&pp_params);
	if (IS_ERR(pp)) {
		err = PTR_ERR(pp);
		goto err_pp;
	}

	/* will copy 'mem.id' into pp->xdp_mem_id */
	err = xdp_reg_mem_model(&xdp->mem, MEM_TYPE_PAGE_POOL, pp);
	if (err)
		goto err_mmodel;

	xdp->pp = pp;

	/* We create a 'fake' RXQ referencing the original dev, but with an
	 * xdp_mem_info pointing to our page_pool
	 */
	xdp_rxq_info_reg(&xdp->rxq, orig_ctx->rxq->dev, 0, 0);
	xdp->rxq.mem.type = MEM_TYPE_PAGE_POOL;
	xdp->rxq.mem.id = pp->xdp_mem_id;
	xdp->dev = orig_ctx->rxq->dev;
	xdp->orig_ctx = orig_ctx;

	return 0;

err_mmodel:
	page_pool_destroy(pp);
err_pp:
	kvfree(xdp->skbs);
err_skbs:
	kvfree(xdp->frames);
	return err;
}

static void xdp_test_run_teardown(struct xdp_test_data *xdp)
{
	xdp_unreg_mem_model(&xdp->mem);
	page_pool_destroy(xdp->pp);
	kfree(xdp->frames);
	kfree(xdp->skbs);
}

static bool frame_was_changed(const struct xdp_page_head *head)
{
	/* xdp_scrub_frame() zeroes the data pointer, flags is the last field,
	 * i.e. has the highest chances to be overwritten. If those two are
	 * untouched, it's most likely safe to skip the context reset.
	 */
	return head->frame->data != head->orig_ctx.data ||
	       head->frame->flags != head->orig_ctx.flags;
}

static bool ctx_was_changed(struct xdp_page_head *head)
{
	return head->orig_ctx.data != head->ctx.data ||
		head->orig_ctx.data_meta != head->ctx.data_meta ||
		head->orig_ctx.data_end != head->ctx.data_end;
}

static void reset_ctx(struct xdp_page_head *head)
{
	if (likely(!frame_was_changed(head) && !ctx_was_changed(head)))
		return;

	head->ctx.data = head->orig_ctx.data;
	head->ctx.data_meta = head->orig_ctx.data_meta;
	head->ctx.data_end = head->orig_ctx.data_end;
	xdp_update_frame_from_buff(&head->ctx, head->frame);
}

static int xdp_recv_frames(struct xdp_frame **frames, int nframes,
			   struct sk_buff **skbs,
			   struct net_device *dev)
{
	gfp_t gfp = __GFP_ZERO | GFP_ATOMIC;
	int i, n;
	LIST_HEAD(list);

	n = kmem_cache_alloc_bulk(skbuff_cache, gfp, nframes, (void **)skbs);
	if (unlikely(n == 0)) {
		for (i = 0; i < nframes; i++)
			xdp_return_frame(frames[i]);
		return -ENOMEM;
	}

	for (i = 0; i < nframes; i++) {
		struct xdp_frame *xdpf = frames[i];
		struct sk_buff *skb = skbs[i];

		skb = __xdp_build_skb_from_frame(xdpf, skb, dev);
		if (!skb) {
			xdp_return_frame(xdpf);
			continue;
		}

		list_add_tail(&skb->list, &list);
	}
	netif_receive_skb_list(&list);

	return 0;
}

static int xdp_test_run_batch(struct xdp_test_data *xdp, struct bpf_prog *prog,
			      u32 repeat)
{
	struct bpf_redirect_info *ri = this_cpu_ptr(&bpf_redirect_info);
	int err = 0, act, ret, i, nframes = 0, batch_sz;
	struct xdp_frame **frames = xdp->frames;
	struct xdp_page_head *head;
	struct xdp_frame *frm;
	bool redirect = false;
	struct xdp_buff *ctx;
	struct page *page;

	batch_sz = min_t(u32, repeat, xdp->batch_size);

	local_bh_disable();
	xdp_set_return_frame_no_direct();

	for (i = 0; i < batch_sz; i++) {
		page = page_pool_dev_alloc_pages(xdp->pp);
		if (!page) {
			err = -ENOMEM;
			goto out;
		}

		head = phys_to_virt(page_to_phys(page));
		reset_ctx(head);
		ctx = &head->ctx;
		frm = head->frame;
		xdp->frame_cnt++;

		act = bpf_prog_run_xdp(prog, ctx);

		/* if program changed pkt bounds we need to update the xdp_frame */
		if (unlikely(ctx_was_changed(head))) {
			ret = xdp_update_frame_from_buff(ctx, frm);
			if (ret) {
				xdp_return_buff(ctx);
				continue;
			}
		}

		switch (act) {
		case XDP_TX:
			/* we can't do a real XDP_TX since we're not in the
			 * driver, so turn it into a REDIRECT back to the same
			 * index
			 */
			ri->tgt_index = xdp->dev->ifindex;
			ri->map_id = INT_MAX;
			ri->map_type = BPF_MAP_TYPE_UNSPEC;
			fallthrough;
		case XDP_REDIRECT:
			redirect = true;
			ret = xdp_do_redirect_frame(xdp->dev, ctx, frm, prog);
			if (ret)
				xdp_return_buff(ctx);
			break;
		case XDP_PASS:
			frames[nframes++] = frm;
			break;
		default:
			bpf_warn_invalid_xdp_action(NULL, prog, act);
			fallthrough;
		case XDP_DROP:
			xdp_return_buff(ctx);
			break;
		}
	}

out:
	if (redirect)
		xdp_do_flush();
	if (nframes) {
		ret = xdp_recv_frames(frames, nframes, xdp->skbs, xdp->dev);
		if (ret)
			err = ret;
	}

	xdp_clear_return_frame_no_direct();
	local_bh_enable();
	return err;
}

static int bpf_test_run_xdp_live(struct bpf_prog *prog, struct xdp_buff *ctx,
				 u32 repeat, u32 batch_size, u32 *time)

{
	struct xdp_test_data xdp = { .batch_size = batch_size };
	struct bpf_test_timer t = { .mode = NO_MIGRATE };
	int ret;

	if (!repeat)
		repeat = 1;

	ret = xdp_test_run_setup(&xdp, ctx);
	if (ret)
		return ret;

	bpf_test_timer_enter(&t);
	do {
		xdp.frame_cnt = 0;
		ret = xdp_test_run_batch(&xdp, prog, repeat - t.i);
		if (unlikely(ret < 0))
			break;
	} while (bpf_test_timer_continue(&t, xdp.frame_cnt, repeat, &ret, time));
	bpf_test_timer_leave(&t);

	xdp_test_run_teardown(&xdp);
	return ret;
}

static int bpf_test_run(struct bpf_prog *prog, void *ctx, u32 repeat,
			u32 *retval, u32 *time, bool xdp)
{
	struct bpf_prog_array_item item = {.prog = prog};
	struct bpf_run_ctx *old_ctx;
	struct bpf_cg_run_ctx run_ctx;
	struct bpf_test_timer t = { NO_MIGRATE };
	enum bpf_cgroup_storage_type stype;
	int ret;

	for_each_cgroup_storage_type(stype) {
		item.cgroup_storage[stype] = bpf_cgroup_storage_alloc(prog, stype);
		if (IS_ERR(item.cgroup_storage[stype])) {
			item.cgroup_storage[stype] = NULL;
			for_each_cgroup_storage_type(stype)
				bpf_cgroup_storage_free(item.cgroup_storage[stype]);
			return -ENOMEM;
		}
	}

	if (!repeat)
		repeat = 1;

	bpf_test_timer_enter(&t);
	old_ctx = bpf_set_run_ctx(&run_ctx.run_ctx);
	do {
		run_ctx.prog_item = &item;
		local_bh_disable();
		if (xdp)
			*retval = bpf_prog_run_xdp(prog, ctx);
		else
			*retval = bpf_prog_run(prog, ctx);
		local_bh_enable();
	} while (bpf_test_timer_continue(&t, 1, repeat, &ret, time));
	bpf_reset_run_ctx(old_ctx);
	bpf_test_timer_leave(&t);

	for_each_cgroup_storage_type(stype)
		bpf_cgroup_storage_free(item.cgroup_storage[stype]);

	return ret;
}

static int bpf_test_finish(const union bpf_attr *kattr,
			   union bpf_attr __user *uattr, const void *data,
			   struct skb_shared_info *sinfo, u32 size,
			   u32 retval, u32 duration)
{
	void __user *data_out = u64_to_user_ptr(kattr->test.data_out);
	int err = -EFAULT;
	u32 copy_size = size;

	/* Clamp copy if the user has provided a size hint, but copy the full
	 * buffer if not to retain old behaviour.
	 */
	if (kattr->test.data_size_out &&
	    copy_size > kattr->test.data_size_out) {
		copy_size = kattr->test.data_size_out;
		err = -ENOSPC;
	}

	if (data_out) {
		int len = sinfo ? copy_size - sinfo->xdp_frags_size : copy_size;

		if (len < 0) {
			err = -ENOSPC;
			goto out;
		}

		if (copy_to_user(data_out, data, len))
			goto out;

		if (sinfo) {
			int i, offset = len;
			u32 data_len;

			for (i = 0; i < sinfo->nr_frags; i++) {
				skb_frag_t *frag = &sinfo->frags[i];

				if (offset >= copy_size) {
					err = -ENOSPC;
					break;
				}

				data_len = min_t(u32, copy_size - offset,
						 skb_frag_size(frag));

				if (copy_to_user(data_out + offset,
						 skb_frag_address(frag),
						 data_len))
					goto out;

				offset += data_len;
			}
		}
	}

	if (copy_to_user(&uattr->test.data_size_out, &size, sizeof(size)))
		goto out;
	if (copy_to_user(&uattr->test.retval, &retval, sizeof(retval)))
		goto out;
	if (copy_to_user(&uattr->test.duration, &duration, sizeof(duration)))
		goto out;
	if (err != -ENOSPC)
		err = 0;
out:
	trace_bpf_test_finish(&err);
	return err;
}

/* Integer types of various sizes and pointer combinations cover variety of
 * architecture dependent calling conventions. 7+ can be supported in the
 * future.
 */
__diag_push();
__diag_ignore_all("-Wmissing-prototypes",
		  "Global functions as their definitions will be in vmlinux BTF");
__bpf_kfunc int bpf_fentry_test1(int a)
{
	return a + 1;
}
EXPORT_SYMBOL_GPL(bpf_fentry_test1);

int noinline bpf_fentry_test2(int a, u64 b)
{
	return a + b;
}

int noinline bpf_fentry_test3(char a, int b, u64 c)
{
	return a + b + c;
}

int noinline bpf_fentry_test4(void *a, char b, int c, u64 d)
{
	return (long)a + b + c + d;
}

int noinline bpf_fentry_test5(u64 a, void *b, short c, int d, u64 e)
{
	return a + (long)b + c + d + e;
}

int noinline bpf_fentry_test6(u64 a, void *b, short c, int d, void *e, u64 f)
{
	return a + (long)b + c + d + (long)e + f;
}

struct bpf_fentry_test_t {
	struct bpf_fentry_test_t *a;
};

int noinline bpf_fentry_test7(struct bpf_fentry_test_t *arg)
{
	return (long)arg;
}

int noinline bpf_fentry_test8(struct bpf_fentry_test_t *arg)
{
	return (long)arg->a;
}

__bpf_kfunc u32 bpf_fentry_test9(u32 *a)
{
	return *a;
}

void noinline bpf_fentry_test_sinfo(struct skb_shared_info *sinfo)
{
}

__bpf_kfunc int bpf_modify_return_test(int a, int *b)
{
	*b += 1;
	return a + *b;
}

__bpf_kfunc int bpf_modify_return_test2(int a, int *b, short c, int d,
					void *e, char f, int g)
{
	*b += 1;
	return a + *b + c + d + (long)e + f + g;
}

int noinline bpf_fentry_shadow_test(int a)
{
	return a + 1;
}

struct prog_test_member1 {
	int a;
};

struct prog_test_member {
	struct prog_test_member1 m;
	int c;
};

struct prog_test_ref_kfunc {
	int a;
	int b;
	struct prog_test_member memb;
	struct prog_test_ref_kfunc *next;
	refcount_t cnt;
};

__bpf_kfunc void bpf_kfunc_call_test_release(struct prog_test_ref_kfunc *p)
{
	refcount_dec(&p->cnt);
}

__bpf_kfunc void bpf_kfunc_call_memb_release(struct prog_test_member *p)
{
}

__diag_pop();

BTF_SET8_START(bpf_test_modify_return_ids)
BTF_ID_FLAGS(func, bpf_modify_return_test)
BTF_ID_FLAGS(func, bpf_modify_return_test2)
BTF_ID_FLAGS(func, bpf_fentry_test1, KF_SLEEPABLE)
BTF_SET8_END(bpf_test_modify_return_ids)

static const struct btf_kfunc_id_set bpf_test_modify_return_set = {
	.owner = THIS_MODULE,
	.set   = &bpf_test_modify_return_ids,
};

BTF_SET8_START(test_sk_check_kfunc_ids)
BTF_ID_FLAGS(func, bpf_kfunc_call_test_release, KF_RELEASE)
BTF_ID_FLAGS(func, bpf_kfunc_call_memb_release, KF_RELEASE)
BTF_SET8_END(test_sk_check_kfunc_ids)

static void *bpf_test_init(const union bpf_attr *kattr, u32 user_size,
			   u32 size, u32 headroom, u32 tailroom)
{
	void __user *data_in = u64_to_user_ptr(kattr->test.data_in);
	void *data;

	if (size < ETH_HLEN || size > PAGE_SIZE - headroom - tailroom)
		return ERR_PTR(-EINVAL);

	if (user_size > size)
		return ERR_PTR(-EMSGSIZE);

	size = SKB_DATA_ALIGN(size);
	data = kzalloc(size + headroom + tailroom, GFP_USER);
	if (!data)
		return ERR_PTR(-ENOMEM);

	if (copy_from_user(data + headroom, data_in, user_size)) {
		kfree(data);
		return ERR_PTR(-EFAULT);
	}

	return data;
}

int bpf_prog_test_run_tracing(struct bpf_prog *prog,
			      const union bpf_attr *kattr,
			      union bpf_attr __user *uattr)
{
	struct bpf_fentry_test_t arg = {};
	u16 side_effect = 0, ret = 0;
	int b = 2, err = -EFAULT;
	u32 retval = 0;

	if (kattr->test.flags || kattr->test.cpu || kattr->test.batch_size)
		return -EINVAL;

	switch (prog->expected_attach_type) {
	case BPF_TRACE_FENTRY:
	case BPF_TRACE_FEXIT:
		if (bpf_fentry_test1(1) != 2 ||
		    bpf_fentry_test2(2, 3) != 5 ||
		    bpf_fentry_test3(4, 5, 6) != 15 ||
		    bpf_fentry_test4((void *)7, 8, 9, 10) != 34 ||
		    bpf_fentry_test5(11, (void *)12, 13, 14, 15) != 65 ||
		    bpf_fentry_test6(16, (void *)17, 18, 19, (void *)20, 21) != 111 ||
		    bpf_fentry_test7((struct bpf_fentry_test_t *)0) != 0 ||
		    bpf_fentry_test8(&arg) != 0 ||
		    bpf_fentry_test9(&retval) != 0)
			goto out;
		break;
	case BPF_MODIFY_RETURN:
		ret = bpf_modify_return_test(1, &b);
		if (b != 2)
			side_effect++;
		b = 2;
		ret += bpf_modify_return_test2(1, &b, 3, 4, (void *)5, 6, 7);
		if (b != 2)
			side_effect++;
		break;
	default:
		goto out;
	}

	retval = ((u32)side_effect << 16) | ret;
	if (copy_to_user(&uattr->test.retval, &retval, sizeof(retval)))
		goto out;

	err = 0;
out:
	trace_bpf_test_finish(&err);
	return err;
}

struct bpf_raw_tp_test_run_info {
	struct bpf_prog *prog;
	void *ctx;
	u32 retval;
};

static void
__bpf_prog_test_run_raw_tp(void *data)
{
	struct bpf_raw_tp_test_run_info *info = data;

	rcu_read_lock();
	info->retval = bpf_prog_run(info->prog, info->ctx);
	rcu_read_unlock();
}

int bpf_prog_test_run_raw_tp(struct bpf_prog *prog,
			     const union bpf_attr *kattr,
			     union bpf_attr __user *uattr)
{
	void __user *ctx_in = u64_to_user_ptr(kattr->test.ctx_in);
	__u32 ctx_size_in = kattr->test.ctx_size_in;
	struct bpf_raw_tp_test_run_info info;
	int cpu = kattr->test.cpu, err = 0;
	int current_cpu;

	/* doesn't support data_in/out, ctx_out, duration, or repeat */
	if (kattr->test.data_in || kattr->test.data_out ||
	    kattr->test.ctx_out || kattr->test.duration ||
	    kattr->test.repeat || kattr->test.batch_size)
		return -EINVAL;

	if (ctx_size_in < prog->aux->max_ctx_offset ||
	    ctx_size_in > MAX_BPF_FUNC_ARGS * sizeof(u64))
		return -EINVAL;

	if ((kattr->test.flags & BPF_F_TEST_RUN_ON_CPU) == 0 && cpu != 0)
		return -EINVAL;

	if (ctx_size_in) {
		info.ctx = memdup_user(ctx_in, ctx_size_in);
		if (IS_ERR(info.ctx))
			return PTR_ERR(info.ctx);
	} else {
		info.ctx = NULL;
	}

	info.prog = prog;

	current_cpu = get_cpu();
	if ((kattr->test.flags & BPF_F_TEST_RUN_ON_CPU) == 0 ||
	    cpu == current_cpu) {
		__bpf_prog_test_run_raw_tp(&info);
	} else if (cpu >= nr_cpu_ids || !cpu_online(cpu)) {
		/* smp_call_function_single() also checks cpu_online()
		 * after csd_lock(). However, since cpu is from user
		 * space, let's do an extra quick check to filter out
		 * invalid value before smp_call_function_single().
		 */
		err = -ENXIO;
	} else {
		err = smp_call_function_single(cpu, __bpf_prog_test_run_raw_tp,
					       &info, 1);
	}
	put_cpu();

	if (!err &&
	    copy_to_user(&uattr->test.retval, &info.retval, sizeof(u32)))
		err = -EFAULT;

	kfree(info.ctx);
	return err;
}

static void *bpf_ctx_init(const union bpf_attr *kattr, u32 max_size)
{
	void __user *data_in = u64_to_user_ptr(kattr->test.ctx_in);
	void __user *data_out = u64_to_user_ptr(kattr->test.ctx_out);
	u32 size = kattr->test.ctx_size_in;
	void *data;
	int err;

	if (!data_in && !data_out)
		return NULL;

	data = kzalloc(max_size, GFP_USER);
	if (!data)
		return ERR_PTR(-ENOMEM);

	if (data_in) {
		err = bpf_check_uarg_tail_zero(USER_BPFPTR(data_in), max_size, size);
		if (err) {
			kfree(data);
			return ERR_PTR(err);
		}

		size = min_t(u32, max_size, size);
		if (copy_from_user(data, data_in, size)) {
			kfree(data);
			return ERR_PTR(-EFAULT);
		}
	}
	return data;
}

static int bpf_ctx_finish(const union bpf_attr *kattr,
			  union bpf_attr __user *uattr, const void *data,
			  u32 size)
{
	void __user *data_out = u64_to_user_ptr(kattr->test.ctx_out);
	int err = -EFAULT;
	u32 copy_size = size;

	if (!data || !data_out)
		return 0;

	if (copy_size > kattr->test.ctx_size_out) {
		copy_size = kattr->test.ctx_size_out;
		err = -ENOSPC;
	}

	if (copy_to_user(data_out, data, copy_size))
		goto out;
	if (copy_to_user(&uattr->test.ctx_size_out, &size, sizeof(size)))
		goto out;
	if (err != -ENOSPC)
		err = 0;
out:
	return err;
}

/**
 * range_is_zero - test whether buffer is initialized
 * @buf: buffer to check
 * @from: check from this position
 * @to: check up until (excluding) this position
 *
 * This function returns true if the there is a non-zero byte
 * in the buf in the range [from,to).
 */
static inline bool range_is_zero(void *buf, size_t from, size_t to)
{
	return !memchr_inv((u8 *)buf + from, 0, to - from);
}

static int convert___skb_to_skb(struct sk_buff *skb, struct __sk_buff *__skb)
{
	struct qdisc_skb_cb *cb = (struct qdisc_skb_cb *)skb->cb;

	if (!__skb)
		return 0;

	/* make sure the fields we don't use are zeroed */
	if (!range_is_zero(__skb, 0, offsetof(struct __sk_buff, mark)))
		return -EINVAL;

	/* mark is allowed */

	if (!range_is_zero(__skb, offsetofend(struct __sk_buff, mark),
			   offsetof(struct __sk_buff, priority)))
		return -EINVAL;

	/* priority is allowed */
	/* ingress_ifindex is allowed */
	/* ifindex is allowed */

	if (!range_is_zero(__skb, offsetofend(struct __sk_buff, ifindex),
			   offsetof(struct __sk_buff, cb)))
		return -EINVAL;

	/* cb is allowed */

	if (!range_is_zero(__skb, offsetofend(struct __sk_buff, cb),
			   offsetof(struct __sk_buff, tstamp)))
		return -EINVAL;

	/* tstamp is allowed */
	/* wire_len is allowed */
	/* gso_segs is allowed */

	if (!range_is_zero(__skb, offsetofend(struct __sk_buff, gso_segs),
			   offsetof(struct __sk_buff, gso_size)))
		return -EINVAL;

	/* gso_size is allowed */

	if (!range_is_zero(__skb, offsetofend(struct __sk_buff, gso_size),
			   offsetof(struct __sk_buff, hwtstamp)))
		return -EINVAL;

	/* hwtstamp is allowed */

	if (!range_is_zero(__skb, offsetofend(struct __sk_buff, hwtstamp),
			   sizeof(struct __sk_buff)))
		return -EINVAL;

	skb->mark = __skb->mark;
	skb->priority = __skb->priority;
	skb->skb_iif = __skb->ingress_ifindex;
	skb->tstamp = __skb->tstamp;
	memcpy(&cb->data, __skb->cb, QDISC_CB_PRIV_LEN);

	if (__skb->wire_len == 0) {
		cb->pkt_len = skb->len;
	} else {
		if (__skb->wire_len < skb->len ||
		    __skb->wire_len > GSO_LEGACY_MAX_SIZE)
			return -EINVAL;
		cb->pkt_len = __skb->wire_len;
	}

	if (__skb->gso_segs > GSO_MAX_SEGS)
		return -EINVAL;
	skb_shinfo(skb)->gso_segs = __skb->gso_segs;
	skb_shinfo(skb)->gso_size = __skb->gso_size;
	skb_shinfo(skb)->hwtstamps.hwtstamp = __skb->hwtstamp;

	return 0;
}

static void convert_skb_to___skb(struct sk_buff *skb, struct __sk_buff *__skb)
{
	struct qdisc_skb_cb *cb = (struct qdisc_skb_cb *)skb->cb;

	if (!__skb)
		return;

	__skb->mark = skb->mark;
	__skb->priority = skb->priority;
	__skb->ingress_ifindex = skb->skb_iif;
	__skb->ifindex = skb->dev->ifindex;
	__skb->tstamp = skb->tstamp;
	memcpy(__skb->cb, &cb->data, QDISC_CB_PRIV_LEN);
	__skb->wire_len = cb->pkt_len;
	__skb->gso_segs = skb_shinfo(skb)->gso_segs;
	__skb->hwtstamp = skb_shinfo(skb)->hwtstamps.hwtstamp;
}

static struct proto bpf_dummy_proto = {
	.name   = "bpf_dummy",
	.owner  = THIS_MODULE,
	.obj_size = sizeof(struct sock),
};

int bpf_prog_test_run_skb(struct bpf_prog *prog, const union bpf_attr *kattr,
			  union bpf_attr __user *uattr)
{
	bool is_l2 = false, is_direct_pkt_access = false;
	struct net *net = current->nsproxy->net_ns;
	struct net_device *dev = net->loopback_dev;
	u32 size = kattr->test.data_size_in;
	u32 repeat = kattr->test.repeat;
	struct __sk_buff *ctx = NULL;
	u32 retval, duration;
	int hh_len = ETH_HLEN;
	struct sk_buff *skb;
	struct sock *sk;
	void *data;
	int ret;

	if (kattr->test.flags || kattr->test.cpu || kattr->test.batch_size)
		return -EINVAL;

	data = bpf_test_init(kattr, kattr->test.data_size_in,
			     size, NET_SKB_PAD + NET_IP_ALIGN,
			     SKB_DATA_ALIGN(sizeof(struct skb_shared_info)));
	if (IS_ERR(data))
		return PTR_ERR(data);

	ctx = bpf_ctx_init(kattr, sizeof(struct __sk_buff));
	if (IS_ERR(ctx)) {
		kfree(data);
		return PTR_ERR(ctx);
	}

	switch (prog->type) {
	case BPF_PROG_TYPE_SCHED_CLS:
	case BPF_PROG_TYPE_SCHED_ACT:
		is_l2 = true;
		fallthrough;
	case BPF_PROG_TYPE_LWT_IN:
	case BPF_PROG_TYPE_LWT_OUT:
	case BPF_PROG_TYPE_LWT_XMIT:
		is_direct_pkt_access = true;
		break;
	default:
		break;
	}

	sk = sk_alloc(net, AF_UNSPEC, GFP_USER, &bpf_dummy_proto, 1);
	if (!sk) {
		kfree(data);
		kfree(ctx);
		return -ENOMEM;
	}
	sock_init_data(NULL, sk);

	skb = slab_build_skb(data);
	if (!skb) {
		kfree(data);
		kfree(ctx);
		sk_free(sk);
		return -ENOMEM;
	}
	skb->sk = sk;

	skb_reserve(skb, NET_SKB_PAD + NET_IP_ALIGN);
	__skb_put(skb, size);
	if (ctx && ctx->ifindex > 1) {
		dev = dev_get_by_index(net, ctx->ifindex);
		if (!dev) {
			ret = -ENODEV;
			goto out;
		}
	}
	skb->protocol = eth_type_trans(skb, dev);
	skb_reset_network_header(skb);

	switch (skb->protocol) {
	case htons(ETH_P_IP):
		sk->sk_family = AF_INET;
		if (sizeof(struct iphdr) <= skb_headlen(skb)) {
			sk->sk_rcv_saddr = ip_hdr(skb)->saddr;
			sk->sk_daddr = ip_hdr(skb)->daddr;
		}
		break;
#if IS_ENABLED(CONFIG_IPV6)
	case htons(ETH_P_IPV6):
		sk->sk_family = AF_INET6;
		if (sizeof(struct ipv6hdr) <= skb_headlen(skb)) {
			sk->sk_v6_rcv_saddr = ipv6_hdr(skb)->saddr;
			sk->sk_v6_daddr = ipv6_hdr(skb)->daddr;
		}
		break;
#endif
	default:
		break;
	}

	if (is_l2)
		__skb_push(skb, hh_len);
	if (is_direct_pkt_access)
		bpf_compute_data_pointers(skb);
	ret = convert___skb_to_skb(skb, ctx);
	if (ret)
		goto out;
	ret = bpf_test_run(prog, skb, repeat, &retval, &duration, false);
	if (ret)
		goto out;
	if (!is_l2) {
		if (skb_headroom(skb) < hh_len) {
			int nhead = HH_DATA_ALIGN(hh_len - skb_headroom(skb));

			if (pskb_expand_head(skb, nhead, 0, GFP_USER)) {
				ret = -ENOMEM;
				goto out;
			}
		}
		memset(__skb_push(skb, hh_len), 0, hh_len);
	}
	convert_skb_to___skb(skb, ctx);

	size = skb->len;
	/* bpf program can never convert linear skb to non-linear */
	if (WARN_ON_ONCE(skb_is_nonlinear(skb)))
		size = skb_headlen(skb);
	ret = bpf_test_finish(kattr, uattr, skb->data, NULL, size, retval,
			      duration);
	if (!ret)
		ret = bpf_ctx_finish(kattr, uattr, ctx,
				     sizeof(struct __sk_buff));
out:
	if (dev && dev != net->loopback_dev)
		dev_put(dev);
	kfree_skb(skb);
	sk_free(sk);
	kfree(ctx);
	return ret;
}

static int xdp_convert_md_to_buff(struct xdp_md *xdp_md, struct xdp_buff *xdp)
{
	unsigned int ingress_ifindex, rx_queue_index;
	struct netdev_rx_queue *rxqueue;
	struct net_device *device;

	if (!xdp_md)
		return 0;

	if (xdp_md->egress_ifindex != 0)
		return -EINVAL;

	ingress_ifindex = xdp_md->ingress_ifindex;
	rx_queue_index = xdp_md->rx_queue_index;

	if (!ingress_ifindex && rx_queue_index)
		return -EINVAL;

	if (ingress_ifindex) {
		device = dev_get_by_index(current->nsproxy->net_ns,
					  ingress_ifindex);
		if (!device)
			return -ENODEV;

		if (rx_queue_index >= device->real_num_rx_queues)
			goto free_dev;

		rxqueue = __netif_get_rx_queue(device, rx_queue_index);

		if (!xdp_rxq_info_is_reg(&rxqueue->xdp_rxq))
			goto free_dev;

		xdp->rxq = &rxqueue->xdp_rxq;
		/* The device is now tracked in the xdp->rxq for later
		 * dev_put()
		 */
	}

	xdp->data = xdp->data_meta + xdp_md->data;
	return 0;

free_dev:
	dev_put(device);
	return -EINVAL;
}

static void xdp_convert_buff_to_md(struct xdp_buff *xdp, struct xdp_md *xdp_md)
{
	if (!xdp_md)
		return;

	xdp_md->data = xdp->data - xdp->data_meta;
	xdp_md->data_end = xdp->data_end - xdp->data_meta;

	if (xdp_md->ingress_ifindex)
		dev_put(xdp->rxq->dev);
}

int bpf_prog_test_run_xdp(struct bpf_prog *prog, const union bpf_attr *kattr,
			  union bpf_attr __user *uattr)
{
	bool do_live = (kattr->test.flags & BPF_F_TEST_XDP_LIVE_FRAMES);
	u32 tailroom = SKB_DATA_ALIGN(sizeof(struct skb_shared_info));
	u32 batch_size = kattr->test.batch_size;
	u32 retval = 0, duration, max_data_sz;
	u32 size = kattr->test.data_size_in;
	u32 headroom = XDP_PACKET_HEADROOM;
	u32 repeat = kattr->test.repeat;
	struct netdev_rx_queue *rxqueue;
	struct skb_shared_info *sinfo;
	struct xdp_buff xdp = {};
	int i, ret = -EINVAL;
	struct xdp_md *ctx;
	void *data;

	if (prog->expected_attach_type == BPF_XDP_DEVMAP ||
	    prog->expected_attach_type == BPF_XDP_CPUMAP)
		return -EINVAL;

	if (kattr->test.flags & ~BPF_F_TEST_XDP_LIVE_FRAMES)
		return -EINVAL;

	if (bpf_prog_is_dev_bound(prog->aux))
		return -EINVAL;

	if (do_live) {
		if (!batch_size)
			batch_size = NAPI_POLL_WEIGHT;
		else if (batch_size > TEST_XDP_MAX_BATCH)
			return -E2BIG;

		headroom += sizeof(struct xdp_page_head);
	} else if (batch_size) {
		return -EINVAL;
	}

	ctx = bpf_ctx_init(kattr, sizeof(struct xdp_md));
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	if (ctx) {
		/* There can't be user provided data before the meta data */
		if (ctx->data_meta || ctx->data_end != size ||
		    ctx->data > ctx->data_end ||
		    unlikely(xdp_metalen_invalid(ctx->data)) ||
		    (do_live && (kattr->test.data_out || kattr->test.ctx_out)))
			goto free_ctx;
		/* Meta data is allocated from the headroom */
		headroom -= ctx->data;
	}

	max_data_sz = 4096 - headroom - tailroom;
	if (size > max_data_sz) {
		/* disallow live data mode for jumbo frames */
		if (do_live)
			goto free_ctx;
		size = max_data_sz;
	}

	data = bpf_test_init(kattr, size, max_data_sz, headroom, tailroom);
	if (IS_ERR(data)) {
		ret = PTR_ERR(data);
		goto free_ctx;
	}

	rxqueue = __netif_get_rx_queue(current->nsproxy->net_ns->loopback_dev, 0);
	rxqueue->xdp_rxq.frag_size = headroom + max_data_sz + tailroom;
	xdp_init_buff(&xdp, rxqueue->xdp_rxq.frag_size, &rxqueue->xdp_rxq);
	xdp_prepare_buff(&xdp, data, headroom, size, true);
	sinfo = xdp_get_shared_info_from_buff(&xdp);

	ret = xdp_convert_md_to_buff(ctx, &xdp);
	if (ret)
		goto free_data;

	if (unlikely(kattr->test.data_size_in > size)) {
		void __user *data_in = u64_to_user_ptr(kattr->test.data_in);

		while (size < kattr->test.data_size_in) {
			struct page *page;
			skb_frag_t *frag;
			u32 data_len;

			if (sinfo->nr_frags == MAX_SKB_FRAGS) {
				ret = -ENOMEM;
				goto out;
			}

			page = alloc_page(GFP_KERNEL);
			if (!page) {
				ret = -ENOMEM;
				goto out;
			}

			frag = &sinfo->frags[sinfo->nr_frags++];

			data_len = min_t(u32, kattr->test.data_size_in - size,
					 PAGE_SIZE);
			skb_frag_fill_page_desc(frag, page, 0, data_len);

			if (copy_from_user(page_address(page), data_in + size,
					   data_len)) {
				ret = -EFAULT;
				goto out;
			}
			sinfo->xdp_frags_size += data_len;
			size += data_len;
		}
		xdp_buff_set_frags_flag(&xdp);
	}

	if (repeat > 1)
		bpf_prog_change_xdp(NULL, prog);

	if (do_live)
		ret = bpf_test_run_xdp_live(prog, &xdp, repeat, batch_size, &duration);
	else
		ret = bpf_test_run(prog, &xdp, repeat, &retval, &duration, true);
	/* We convert the xdp_buff back to an xdp_md before checking the return
	 * code so the reference count of any held netdevice will be decremented
	 * even if the test run failed.
	 */
	xdp_convert_buff_to_md(&xdp, ctx);
	if (ret)
		goto out;

	size = xdp.data_end - xdp.data_meta + sinfo->xdp_frags_size;
	ret = bpf_test_finish(kattr, uattr, xdp.data_meta, sinfo, size,
			      retval, duration);
	if (!ret)
		ret = bpf_ctx_finish(kattr, uattr, ctx,
				     sizeof(struct xdp_md));

out:
	if (repeat > 1)
		bpf_prog_change_xdp(prog, NULL);
free_data:
	for (i = 0; i < sinfo->nr_frags; i++)
		__free_page(skb_frag_page(&sinfo->frags[i]));
	kfree(data);
free_ctx:
	kfree(ctx);
	return ret;
}

static int verify_user_bpf_flow_keys(struct bpf_flow_keys *ctx)
{
	/* make sure the fields we don't use are zeroed */
	if (!range_is_zero(ctx, 0, offsetof(struct bpf_flow_keys, flags)))
		return -EINVAL;

	/* flags is allowed */

	if (!range_is_zero(ctx, offsetofend(struct bpf_flow_keys, flags),
			   sizeof(struct bpf_flow_keys)))
		return -EINVAL;

	return 0;
}

int bpf_prog_test_run_flow_dissector(struct bpf_prog *prog,
				     const union bpf_attr *kattr,
				     union bpf_attr __user *uattr)
{
	struct bpf_test_timer t = { NO_PREEMPT };
	u32 size = kattr->test.data_size_in;
	struct bpf_flow_dissector ctx = {};
	u32 repeat = kattr->test.repeat;
	struct bpf_flow_keys *user_ctx;
	struct bpf_flow_keys flow_keys;
	const struct ethhdr *eth;
	unsigned int flags = 0;
	u32 retval, duration;
	void *data;
	int ret;

	if (kattr->test.flags || kattr->test.cpu || kattr->test.batch_size)
		return -EINVAL;

	if (size < ETH_HLEN)
		return -EINVAL;

	data = bpf_test_init(kattr, kattr->test.data_size_in, size, 0, 0);
	if (IS_ERR(data))
		return PTR_ERR(data);

	eth = (struct ethhdr *)data;

	if (!repeat)
		repeat = 1;

	user_ctx = bpf_ctx_init(kattr, sizeof(struct bpf_flow_keys));
	if (IS_ERR(user_ctx)) {
		kfree(data);
		return PTR_ERR(user_ctx);
	}
	if (user_ctx) {
		ret = verify_user_bpf_flow_keys(user_ctx);
		if (ret)
			goto out;
		flags = user_ctx->flags;
	}

	ctx.flow_keys = &flow_keys;
	ctx.data = data;
	ctx.data_end = (__u8 *)data + size;

	bpf_test_timer_enter(&t);
	do {
		retval = bpf_flow_dissect(prog, &ctx, eth->h_proto, ETH_HLEN,
					  size, flags);
	} while (bpf_test_timer_continue(&t, 1, repeat, &ret, &duration));
	bpf_test_timer_leave(&t);

	if (ret < 0)
		goto out;

	ret = bpf_test_finish(kattr, uattr, &flow_keys, NULL,
			      sizeof(flow_keys), retval, duration);
	if (!ret)
		ret = bpf_ctx_finish(kattr, uattr, user_ctx,
				     sizeof(struct bpf_flow_keys));

out:
	kfree(user_ctx);
	kfree(data);
	return ret;
}

int bpf_prog_test_run_sk_lookup(struct bpf_prog *prog, const union bpf_attr *kattr,
				union bpf_attr __user *uattr)
{
	struct bpf_test_timer t = { NO_PREEMPT };
	struct bpf_prog_array *progs = NULL;
	struct bpf_sk_lookup_kern ctx = {};
	u32 repeat = kattr->test.repeat;
	struct bpf_sk_lookup *user_ctx;
	u32 retval, duration;
	int ret = -EINVAL;

	if (kattr->test.flags || kattr->test.cpu || kattr->test.batch_size)
		return -EINVAL;

	if (kattr->test.data_in || kattr->test.data_size_in || kattr->test.data_out ||
	    kattr->test.data_size_out)
		return -EINVAL;

	if (!repeat)
		repeat = 1;

	user_ctx = bpf_ctx_init(kattr, sizeof(*user_ctx));
	if (IS_ERR(user_ctx))
		return PTR_ERR(user_ctx);

	if (!user_ctx)
		return -EINVAL;

	if (user_ctx->sk)
		goto out;

	if (!range_is_zero(user_ctx, offsetofend(typeof(*user_ctx), local_port), sizeof(*user_ctx)))
		goto out;

	if (user_ctx->local_port > U16_MAX) {
		ret = -ERANGE;
		goto out;
	}

	ctx.family = (u16)user_ctx->family;
	ctx.protocol = (u16)user_ctx->protocol;
	ctx.dport = (u16)user_ctx->local_port;
	ctx.sport = user_ctx->remote_port;

	switch (ctx.family) {
	case AF_INET:
		ctx.v4.daddr = (__force __be32)user_ctx->local_ip4;
		ctx.v4.saddr = (__force __be32)user_ctx->remote_ip4;
		break;

#if IS_ENABLED(CONFIG_IPV6)
	case AF_INET6:
		ctx.v6.daddr = (struct in6_addr *)user_ctx->local_ip6;
		ctx.v6.saddr = (struct in6_addr *)user_ctx->remote_ip6;
		break;
#endif

	default:
		ret = -EAFNOSUPPORT;
		goto out;
	}

	progs = bpf_prog_array_alloc(1, GFP_KERNEL);
	if (!progs) {
		ret = -ENOMEM;
		goto out;
	}

	progs->items[0].prog = prog;

	bpf_test_timer_enter(&t);
	do {
		ctx.selected_sk = NULL;
		retval = BPF_PROG_SK_LOOKUP_RUN_ARRAY(progs, ctx, bpf_prog_run);
	} while (bpf_test_timer_continue(&t, 1, repeat, &ret, &duration));
	bpf_test_timer_leave(&t);

	if (ret < 0)
		goto out;

	user_ctx->cookie = 0;
	if (ctx.selected_sk) {
		if (ctx.selected_sk->sk_reuseport && !ctx.no_reuseport) {
			ret = -EOPNOTSUPP;
			goto out;
		}

		user_ctx->cookie = sock_gen_cookie(ctx.selected_sk);
	}

	ret = bpf_test_finish(kattr, uattr, NULL, NULL, 0, retval, duration);
	if (!ret)
		ret = bpf_ctx_finish(kattr, uattr, user_ctx, sizeof(*user_ctx));

out:
	bpf_prog_array_free(progs);
	kfree(user_ctx);
	return ret;
}

int bpf_prog_test_run_syscall(struct bpf_prog *prog,
			      const union bpf_attr *kattr,
			      union bpf_attr __user *uattr)
{
	void __user *ctx_in = u64_to_user_ptr(kattr->test.ctx_in);
	__u32 ctx_size_in = kattr->test.ctx_size_in;
	void *ctx = NULL;
	u32 retval;
	int err = 0;

	/* doesn't support data_in/out, ctx_out, duration, or repeat or flags */
	if (kattr->test.data_in || kattr->test.data_out ||
	    kattr->test.ctx_out || kattr->test.duration ||
	    kattr->test.repeat || kattr->test.flags ||
	    kattr->test.batch_size)
		return -EINVAL;

	if (ctx_size_in < prog->aux->max_ctx_offset ||
	    ctx_size_in > U16_MAX)
		return -EINVAL;

	if (ctx_size_in) {
		ctx = memdup_user(ctx_in, ctx_size_in);
		if (IS_ERR(ctx))
			return PTR_ERR(ctx);
	}

	rcu_read_lock_trace();
	retval = bpf_prog_run_pin_on_cpu(prog, ctx);
	rcu_read_unlock_trace();

	if (copy_to_user(&uattr->test.retval, &retval, sizeof(u32))) {
		err = -EFAULT;
		goto out;
	}
	if (ctx_size_in)
		if (copy_to_user(ctx_in, ctx, ctx_size_in))
			err = -EFAULT;
out:
	kfree(ctx);
	return err;
}

static int verify_and_copy_hook_state(struct nf_hook_state *state,
				      const struct nf_hook_state *user,
				      struct net_device *dev)
{
	if (user->in || user->out)
		return -EINVAL;

	if (user->net || user->sk || user->okfn)
		return -EINVAL;

	switch (user->pf) {
	case NFPROTO_IPV4:
	case NFPROTO_IPV6:
		switch (state->hook) {
		case NF_INET_PRE_ROUTING:
			state->in = dev;
			break;
		case NF_INET_LOCAL_IN:
			state->in = dev;
			break;
		case NF_INET_FORWARD:
			state->in = dev;
			state->out = dev;
			break;
		case NF_INET_LOCAL_OUT:
			state->out = dev;
			break;
		case NF_INET_POST_ROUTING:
			state->out = dev;
			break;
		}

		break;
	default:
		return -EINVAL;
	}

	state->pf = user->pf;
	state->hook = user->hook;

	return 0;
}

static __be16 nfproto_eth(int nfproto)
{
	switch (nfproto) {
	case NFPROTO_IPV4:
		return htons(ETH_P_IP);
	case NFPROTO_IPV6:
		break;
	}

	return htons(ETH_P_IPV6);
}

int bpf_prog_test_run_nf(struct bpf_prog *prog,
			 const union bpf_attr *kattr,
			 union bpf_attr __user *uattr)
{
	struct net *net = current->nsproxy->net_ns;
	struct net_device *dev = net->loopback_dev;
	struct nf_hook_state *user_ctx, hook_state = {
		.pf = NFPROTO_IPV4,
		.hook = NF_INET_LOCAL_OUT,
	};
	u32 size = kattr->test.data_size_in;
	u32 repeat = kattr->test.repeat;
	struct bpf_nf_ctx ctx = {
		.state = &hook_state,
	};
	struct sk_buff *skb = NULL;
	u32 retval, duration;
	void *data;
	int ret;

	if (kattr->test.flags || kattr->test.cpu || kattr->test.batch_size)
		return -EINVAL;

	if (size < sizeof(struct iphdr))
		return -EINVAL;

	data = bpf_test_init(kattr, kattr->test.data_size_in, size,
			     NET_SKB_PAD + NET_IP_ALIGN,
			     SKB_DATA_ALIGN(sizeof(struct skb_shared_info)));
	if (IS_ERR(data))
		return PTR_ERR(data);

	if (!repeat)
		repeat = 1;

	user_ctx = bpf_ctx_init(kattr, sizeof(struct nf_hook_state));
	if (IS_ERR(user_ctx)) {
		kfree(data);
		return PTR_ERR(user_ctx);
	}

	if (user_ctx) {
		ret = verify_and_copy_hook_state(&hook_state, user_ctx, dev);
		if (ret)
			goto out;
	}

	skb = slab_build_skb(data);
	if (!skb) {
		ret = -ENOMEM;
		goto out;
	}

	data = NULL; /* data released via kfree_skb */

	skb_reserve(skb, NET_SKB_PAD + NET_IP_ALIGN);
	__skb_put(skb, size);

	ret = -EINVAL;

	if (hook_state.hook != NF_INET_LOCAL_OUT) {
		if (size < ETH_HLEN + sizeof(struct iphdr))
			goto out;

		skb->protocol = eth_type_trans(skb, dev);
		switch (skb->protocol) {
		case htons(ETH_P_IP):
			if (hook_state.pf == NFPROTO_IPV4)
				break;
			goto out;
		case htons(ETH_P_IPV6):
			if (size < ETH_HLEN + sizeof(struct ipv6hdr))
				goto out;
			if (hook_state.pf == NFPROTO_IPV6)
				break;
			goto out;
		default:
			ret = -EPROTO;
			goto out;
		}

		skb_reset_network_header(skb);
	} else {
		skb->protocol = nfproto_eth(hook_state.pf);
	}

	ctx.skb = skb;

	ret = bpf_test_run(prog, &ctx, repeat, &retval, &duration, false);
	if (ret)
		goto out;

	ret = bpf_test_finish(kattr, uattr, NULL, NULL, 0, retval, duration);

out:
	kfree(user_ctx);
	kfree_skb(skb);
	kfree(data);
	return ret;
}

static const struct btf_kfunc_id_set bpf_prog_test_kfunc_set = {
	.owner = THIS_MODULE,
	.set   = &test_sk_check_kfunc_ids,
};

BTF_ID_LIST(bpf_prog_test_dtor_kfunc_ids)
BTF_ID(struct, prog_test_ref_kfunc)
BTF_ID(func, bpf_kfunc_call_test_release)
BTF_ID(struct, prog_test_member)
BTF_ID(func, bpf_kfunc_call_memb_release)

static int __init bpf_prog_test_run_init(void)
{
	const struct btf_id_dtor_kfunc bpf_prog_test_dtor_kfunc[] = {
		{
		  .btf_id       = bpf_prog_test_dtor_kfunc_ids[0],
		  .kfunc_btf_id = bpf_prog_test_dtor_kfunc_ids[1]
		},
		{
		  .btf_id	= bpf_prog_test_dtor_kfunc_ids[2],
		  .kfunc_btf_id = bpf_prog_test_dtor_kfunc_ids[3],
		},
	};
	int ret;

	ret = register_btf_fmodret_id_set(&bpf_test_modify_return_set);
	ret = ret ?: register_btf_kfunc_id_set(BPF_PROG_TYPE_SCHED_CLS, &bpf_prog_test_kfunc_set);
	ret = ret ?: register_btf_kfunc_id_set(BPF_PROG_TYPE_TRACING, &bpf_prog_test_kfunc_set);
	ret = ret ?: register_btf_kfunc_id_set(BPF_PROG_TYPE_SYSCALL, &bpf_prog_test_kfunc_set);
	return ret ?: register_btf_id_dtor_kfuncs(bpf_prog_test_dtor_kfunc,
						  ARRAY_SIZE(bpf_prog_test_dtor_kfunc),
						  THIS_MODULE);
}
late_initcall(bpf_prog_test_run_init);
