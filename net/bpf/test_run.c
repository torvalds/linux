// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2017 Facebook
 */
#include <linux/bpf.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/etherdevice.h>
#include <linux/filter.h>
#include <linux/sched/signal.h>
#include <net/bpf_sk_storage.h>
#include <net/sock.h>
#include <net/tcp.h>

#define CREATE_TRACE_POINTS
#include <trace/events/bpf_test_run.h>

static int bpf_test_run(struct bpf_prog *prog, void *ctx, u32 repeat,
			u32 *retval, u32 *time, bool xdp)
{
	struct bpf_cgroup_storage *storage[MAX_BPF_CGROUP_STORAGE_TYPE] = { NULL };
	enum bpf_cgroup_storage_type stype;
	u64 time_start, time_spent = 0;
	int ret = 0;
	u32 i;

	for_each_cgroup_storage_type(stype) {
		storage[stype] = bpf_cgroup_storage_alloc(prog, stype);
		if (IS_ERR(storage[stype])) {
			storage[stype] = NULL;
			for_each_cgroup_storage_type(stype)
				bpf_cgroup_storage_free(storage[stype]);
			return -ENOMEM;
		}
	}

	if (!repeat)
		repeat = 1;

	rcu_read_lock();
	migrate_disable();
	time_start = ktime_get_ns();
	for (i = 0; i < repeat; i++) {
		bpf_cgroup_storage_set(storage);

		if (xdp)
			*retval = bpf_prog_run_xdp(prog, ctx);
		else
			*retval = BPF_PROG_RUN(prog, ctx);

		if (signal_pending(current)) {
			ret = -EINTR;
			break;
		}

		if (need_resched()) {
			time_spent += ktime_get_ns() - time_start;
			migrate_enable();
			rcu_read_unlock();

			cond_resched();

			rcu_read_lock();
			migrate_disable();
			time_start = ktime_get_ns();
		}
	}
	time_spent += ktime_get_ns() - time_start;
	migrate_enable();
	rcu_read_unlock();

	do_div(time_spent, repeat);
	*time = time_spent > U32_MAX ? U32_MAX : (u32)time_spent;

	for_each_cgroup_storage_type(stype)
		bpf_cgroup_storage_free(storage[stype]);

	return ret;
}

static int bpf_test_finish(const union bpf_attr *kattr,
			   union bpf_attr __user *uattr, const void *data,
			   u32 size, u32 retval, u32 duration)
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

	if (data_out && copy_to_user(data_out, data, copy_size))
		goto out;
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
int noinline bpf_fentry_test1(int a)
{
	return a + 1;
}

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

static void *bpf_test_init(const union bpf_attr *kattr, u32 size,
			   u32 headroom, u32 tailroom)
{
	void __user *data_in = u64_to_user_ptr(kattr->test.data_in);
	void *data;

	if (size < ETH_HLEN || size > PAGE_SIZE - headroom - tailroom)
		return ERR_PTR(-EINVAL);

	data = kzalloc(size + headroom + tailroom, GFP_USER);
	if (!data)
		return ERR_PTR(-ENOMEM);

	if (copy_from_user(data + headroom, data_in, size)) {
		kfree(data);
		return ERR_PTR(-EFAULT);
	}
	if (bpf_fentry_test1(1) != 2 ||
	    bpf_fentry_test2(2, 3) != 5 ||
	    bpf_fentry_test3(4, 5, 6) != 15 ||
	    bpf_fentry_test4((void *)7, 8, 9, 10) != 34 ||
	    bpf_fentry_test5(11, (void *)12, 13, 14, 15) != 65 ||
	    bpf_fentry_test6(16, (void *)17, 18, 19, (void *)20, 21) != 111) {
		kfree(data);
		return ERR_PTR(-EFAULT);
	}
	return data;
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
		err = bpf_check_uarg_tail_zero(data_in, max_size, size);
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

	if (!range_is_zero(__skb, offsetofend(struct __sk_buff, priority),
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
			   sizeof(struct __sk_buff)))
		return -EINVAL;

	skb->mark = __skb->mark;
	skb->priority = __skb->priority;
	skb->tstamp = __skb->tstamp;
	memcpy(&cb->data, __skb->cb, QDISC_CB_PRIV_LEN);

	if (__skb->wire_len == 0) {
		cb->pkt_len = skb->len;
	} else {
		if (__skb->wire_len < skb->len ||
		    __skb->wire_len > GSO_MAX_SIZE)
			return -EINVAL;
		cb->pkt_len = __skb->wire_len;
	}

	if (__skb->gso_segs > GSO_MAX_SEGS)
		return -EINVAL;
	skb_shinfo(skb)->gso_segs = __skb->gso_segs;

	return 0;
}

static void convert_skb_to___skb(struct sk_buff *skb, struct __sk_buff *__skb)
{
	struct qdisc_skb_cb *cb = (struct qdisc_skb_cb *)skb->cb;

	if (!__skb)
		return;

	__skb->mark = skb->mark;
	__skb->priority = skb->priority;
	__skb->tstamp = skb->tstamp;
	memcpy(__skb->cb, &cb->data, QDISC_CB_PRIV_LEN);
	__skb->wire_len = cb->pkt_len;
	__skb->gso_segs = skb_shinfo(skb)->gso_segs;
}

int bpf_prog_test_run_skb(struct bpf_prog *prog, const union bpf_attr *kattr,
			  union bpf_attr __user *uattr)
{
	bool is_l2 = false, is_direct_pkt_access = false;
	u32 size = kattr->test.data_size_in;
	u32 repeat = kattr->test.repeat;
	struct __sk_buff *ctx = NULL;
	u32 retval, duration;
	int hh_len = ETH_HLEN;
	struct sk_buff *skb;
	struct sock *sk;
	void *data;
	int ret;

	data = bpf_test_init(kattr, size, NET_SKB_PAD + NET_IP_ALIGN,
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
		/* fall through */
	case BPF_PROG_TYPE_LWT_IN:
	case BPF_PROG_TYPE_LWT_OUT:
	case BPF_PROG_TYPE_LWT_XMIT:
		is_direct_pkt_access = true;
		break;
	default:
		break;
	}

	sk = kzalloc(sizeof(struct sock), GFP_USER);
	if (!sk) {
		kfree(data);
		kfree(ctx);
		return -ENOMEM;
	}
	sock_net_set(sk, current->nsproxy->net_ns);
	sock_init_data(NULL, sk);

	skb = build_skb(data, 0);
	if (!skb) {
		kfree(data);
		kfree(ctx);
		kfree(sk);
		return -ENOMEM;
	}
	skb->sk = sk;

	skb_reserve(skb, NET_SKB_PAD + NET_IP_ALIGN);
	__skb_put(skb, size);
	skb->protocol = eth_type_trans(skb, current->nsproxy->net_ns->loopback_dev);
	skb_reset_network_header(skb);

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
	ret = bpf_test_finish(kattr, uattr, skb->data, size, retval, duration);
	if (!ret)
		ret = bpf_ctx_finish(kattr, uattr, ctx,
				     sizeof(struct __sk_buff));
out:
	kfree_skb(skb);
	bpf_sk_storage_free(sk);
	kfree(sk);
	kfree(ctx);
	return ret;
}

int bpf_prog_test_run_xdp(struct bpf_prog *prog, const union bpf_attr *kattr,
			  union bpf_attr __user *uattr)
{
	u32 size = kattr->test.data_size_in;
	u32 repeat = kattr->test.repeat;
	struct netdev_rx_queue *rxqueue;
	struct xdp_buff xdp = {};
	u32 retval, duration;
	void *data;
	int ret;

	if (kattr->test.ctx_in || kattr->test.ctx_out)
		return -EINVAL;

	data = bpf_test_init(kattr, size, XDP_PACKET_HEADROOM + NET_IP_ALIGN, 0);
	if (IS_ERR(data))
		return PTR_ERR(data);

	xdp.data_hard_start = data;
	xdp.data = data + XDP_PACKET_HEADROOM + NET_IP_ALIGN;
	xdp.data_meta = xdp.data;
	xdp.data_end = xdp.data + size;

	rxqueue = __netif_get_rx_queue(current->nsproxy->net_ns->loopback_dev, 0);
	xdp.rxq = &rxqueue->xdp_rxq;
	bpf_prog_change_xdp(NULL, prog);
	ret = bpf_test_run(prog, &xdp, repeat, &retval, &duration, true);
	if (ret)
		goto out;
	if (xdp.data != data + XDP_PACKET_HEADROOM + NET_IP_ALIGN ||
	    xdp.data_end != xdp.data + size)
		size = xdp.data_end - xdp.data;
	ret = bpf_test_finish(kattr, uattr, xdp.data, size, retval, duration);
out:
	bpf_prog_change_xdp(prog, NULL);
	kfree(data);
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
	u32 size = kattr->test.data_size_in;
	struct bpf_flow_dissector ctx = {};
	u32 repeat = kattr->test.repeat;
	struct bpf_flow_keys *user_ctx;
	struct bpf_flow_keys flow_keys;
	u64 time_start, time_spent = 0;
	const struct ethhdr *eth;
	unsigned int flags = 0;
	u32 retval, duration;
	void *data;
	int ret;
	u32 i;

	if (prog->type != BPF_PROG_TYPE_FLOW_DISSECTOR)
		return -EINVAL;

	if (size < ETH_HLEN)
		return -EINVAL;

	data = bpf_test_init(kattr, size, 0, 0);
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

	rcu_read_lock();
	preempt_disable();
	time_start = ktime_get_ns();
	for (i = 0; i < repeat; i++) {
		retval = bpf_flow_dissect(prog, &ctx, eth->h_proto, ETH_HLEN,
					  size, flags);

		if (signal_pending(current)) {
			preempt_enable();
			rcu_read_unlock();

			ret = -EINTR;
			goto out;
		}

		if (need_resched()) {
			time_spent += ktime_get_ns() - time_start;
			preempt_enable();
			rcu_read_unlock();

			cond_resched();

			rcu_read_lock();
			preempt_disable();
			time_start = ktime_get_ns();
		}
	}
	time_spent += ktime_get_ns() - time_start;
	preempt_enable();
	rcu_read_unlock();

	do_div(time_spent, repeat);
	duration = time_spent > U32_MAX ? U32_MAX : (u32)time_spent;

	ret = bpf_test_finish(kattr, uattr, &flow_keys, sizeof(flow_keys),
			      retval, duration);
	if (!ret)
		ret = bpf_ctx_finish(kattr, uattr, user_ctx,
				     sizeof(struct bpf_flow_keys));

out:
	kfree(user_ctx);
	kfree(data);
	return ret;
}
