// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2017 Facebook
 */
#include <linux/bpf.h>
#include <linux/btf_ids.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/etherdevice.h>
#include <linux/filter.h>
#include <linux/rcupdate_trace.h>
#include <linux/sched/signal.h>
#include <net/bpf_sk_storage.h>
#include <net/sock.h>
#include <net/tcp.h>
#include <net/net_namespace.h>
#include <linux/error-injection.h>
#include <linux/smp.h>
#include <linux/sock_diag.h>
#include <net/xdp.h>

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

static bool bpf_test_timer_continue(struct bpf_test_timer *t, u32 repeat, int *err, u32 *duration)
	__must_hold(rcu)
{
	t->i++;
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
		if (xdp)
			*retval = bpf_prog_run_xdp(prog, ctx);
		else
			*retval = bpf_prog_run(prog, ctx);
	} while (bpf_test_timer_continue(&t, repeat, &ret, time));
	bpf_reset_run_ctx(old_ctx);
	bpf_test_timer_leave(&t);

	for_each_cgroup_storage_type(stype)
		bpf_cgroup_storage_free(item.cgroup_storage[stype]);

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
__diag_push();
__diag_ignore(GCC, 8, "-Wmissing-prototypes",
	      "Global functions as their definitions will be in vmlinux BTF");
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

int noinline bpf_modify_return_test(int a, int *b)
{
	*b += 1;
	return a + *b;
}

u64 noinline bpf_kfunc_call_test1(struct sock *sk, u32 a, u64 b, u32 c, u64 d)
{
	return a + b + c + d;
}

int noinline bpf_kfunc_call_test2(struct sock *sk, u32 a, u32 b)
{
	return a + b;
}

struct sock * noinline bpf_kfunc_call_test3(struct sock *sk)
{
	return sk;
}

__diag_pop();

ALLOW_ERROR_INJECTION(bpf_modify_return_test, ERRNO);

BTF_SET_START(test_sk_kfunc_ids)
BTF_ID(func, bpf_kfunc_call_test1)
BTF_ID(func, bpf_kfunc_call_test2)
BTF_ID(func, bpf_kfunc_call_test3)
BTF_SET_END(test_sk_kfunc_ids)

bool bpf_prog_test_check_kfunc_call(u32 kfunc_id)
{
	return btf_id_set_contains(&test_sk_kfunc_ids, kfunc_id);
}

static void *bpf_test_init(const union bpf_attr *kattr, u32 size,
			   u32 headroom, u32 tailroom)
{
	void __user *data_in = u64_to_user_ptr(kattr->test.data_in);
	u32 user_size = kattr->test.data_size_in;
	void *data;

	if (size < ETH_HLEN || size > PAGE_SIZE - headroom - tailroom)
		return ERR_PTR(-EINVAL);

	if (user_size > size)
		return ERR_PTR(-EMSGSIZE);

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

	if (kattr->test.flags || kattr->test.cpu)
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
		    bpf_fentry_test8(&arg) != 0)
			goto out;
		break;
	case BPF_MODIFY_RETURN:
		ret = bpf_modify_return_test(1, &b);
		if (b != 2)
			side_effect = 1;
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
	    kattr->test.repeat)
		return -EINVAL;

	if (ctx_size_in < prog->aux->max_ctx_offset ||
	    ctx_size_in > MAX_BPF_FUNC_ARGS * sizeof(u64))
		return -EINVAL;

	if ((kattr->test.flags & BPF_F_TEST_RUN_ON_CPU) == 0 && cpu != 0)
		return -EINVAL;

	if (ctx_size_in) {
		info.ctx = kzalloc(ctx_size_in, GFP_USER);
		if (!info.ctx)
			return -ENOMEM;
		if (copy_from_user(info.ctx, ctx_in, ctx_size_in)) {
			err = -EFAULT;
			goto out;
		}
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

out:
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

	if (!range_is_zero(__skb, offsetofend(struct __sk_buff, priority),
			   offsetof(struct __sk_buff, ifindex)))
		return -EINVAL;

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
	skb_shinfo(skb)->gso_size = __skb->gso_size;

	return 0;
}

static void convert_skb_to___skb(struct sk_buff *skb, struct __sk_buff *__skb)
{
	struct qdisc_skb_cb *cb = (struct qdisc_skb_cb *)skb->cb;

	if (!__skb)
		return;

	__skb->mark = skb->mark;
	__skb->priority = skb->priority;
	__skb->ifindex = skb->dev->ifindex;
	__skb->tstamp = skb->tstamp;
	memcpy(__skb->cb, &cb->data, QDISC_CB_PRIV_LEN);
	__skb->wire_len = cb->pkt_len;
	__skb->gso_segs = skb_shinfo(skb)->gso_segs;
}

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

	if (kattr->test.flags || kattr->test.cpu)
		return -EINVAL;

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
		fallthrough;
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
	sock_net_set(sk, net);
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
	ret = bpf_test_finish(kattr, uattr, skb->data, size, retval, duration);
	if (!ret)
		ret = bpf_ctx_finish(kattr, uattr, ctx,
				     sizeof(struct __sk_buff));
out:
	if (dev && dev != net->loopback_dev)
		dev_put(dev);
	kfree_skb(skb);
	bpf_sk_storage_free(sk);
	kfree(sk);
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
	u32 tailroom = SKB_DATA_ALIGN(sizeof(struct skb_shared_info));
	u32 headroom = XDP_PACKET_HEADROOM;
	u32 size = kattr->test.data_size_in;
	u32 repeat = kattr->test.repeat;
	struct netdev_rx_queue *rxqueue;
	struct xdp_buff xdp = {};
	u32 retval, duration;
	struct xdp_md *ctx;
	u32 max_data_sz;
	void *data;
	int ret = -EINVAL;

	if (prog->expected_attach_type == BPF_XDP_DEVMAP ||
	    prog->expected_attach_type == BPF_XDP_CPUMAP)
		return -EINVAL;

	ctx = bpf_ctx_init(kattr, sizeof(struct xdp_md));
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	if (ctx) {
		/* There can't be user provided data before the meta data */
		if (ctx->data_meta || ctx->data_end != size ||
		    ctx->data > ctx->data_end ||
		    unlikely(xdp_metalen_invalid(ctx->data)))
			goto free_ctx;
		/* Meta data is allocated from the headroom */
		headroom -= ctx->data;
	}

	/* XDP have extra tailroom as (most) drivers use full page */
	max_data_sz = 4096 - headroom - tailroom;

	data = bpf_test_init(kattr, max_data_sz, headroom, tailroom);
	if (IS_ERR(data)) {
		ret = PTR_ERR(data);
		goto free_ctx;
	}

	rxqueue = __netif_get_rx_queue(current->nsproxy->net_ns->loopback_dev, 0);
	xdp_init_buff(&xdp, headroom + max_data_sz + tailroom,
		      &rxqueue->xdp_rxq);
	xdp_prepare_buff(&xdp, data, headroom, size, true);

	ret = xdp_convert_md_to_buff(ctx, &xdp);
	if (ret)
		goto free_data;

	bpf_prog_change_xdp(NULL, prog);
	ret = bpf_test_run(prog, &xdp, repeat, &retval, &duration, true);
	/* We convert the xdp_buff back to an xdp_md before checking the return
	 * code so the reference count of any held netdevice will be decremented
	 * even if the test run failed.
	 */
	xdp_convert_buff_to_md(&xdp, ctx);
	if (ret)
		goto out;

	if (xdp.data_meta != data + headroom ||
	    xdp.data_end != xdp.data_meta + size)
		size = xdp.data_end - xdp.data_meta;

	ret = bpf_test_finish(kattr, uattr, xdp.data_meta, size, retval,
			      duration);
	if (!ret)
		ret = bpf_ctx_finish(kattr, uattr, ctx,
				     sizeof(struct xdp_md));

out:
	bpf_prog_change_xdp(prog, NULL);
free_data:
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

	if (prog->type != BPF_PROG_TYPE_FLOW_DISSECTOR)
		return -EINVAL;

	if (kattr->test.flags || kattr->test.cpu)
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

	bpf_test_timer_enter(&t);
	do {
		retval = bpf_flow_dissect(prog, &ctx, eth->h_proto, ETH_HLEN,
					  size, flags);
	} while (bpf_test_timer_continue(&t, repeat, &ret, &duration));
	bpf_test_timer_leave(&t);

	if (ret < 0)
		goto out;

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

	if (prog->type != BPF_PROG_TYPE_SK_LOOKUP)
		return -EINVAL;

	if (kattr->test.flags || kattr->test.cpu)
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

	if (user_ctx->local_port > U16_MAX || user_ctx->remote_port > U16_MAX) {
		ret = -ERANGE;
		goto out;
	}

	ctx.family = (u16)user_ctx->family;
	ctx.protocol = (u16)user_ctx->protocol;
	ctx.dport = (u16)user_ctx->local_port;
	ctx.sport = (__force __be16)user_ctx->remote_port;

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
	} while (bpf_test_timer_continue(&t, repeat, &ret, &duration));
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

	ret = bpf_test_finish(kattr, uattr, NULL, 0, retval, duration);
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
	    kattr->test.repeat || kattr->test.flags)
		return -EINVAL;

	if (ctx_size_in < prog->aux->max_ctx_offset ||
	    ctx_size_in > U16_MAX)
		return -EINVAL;

	if (ctx_size_in) {
		ctx = kzalloc(ctx_size_in, GFP_USER);
		if (!ctx)
			return -ENOMEM;
		if (copy_from_user(ctx, ctx_in, ctx_size_in)) {
			err = -EFAULT;
			goto out;
		}
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
