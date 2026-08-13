// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2026 Isovalent */

#include <linux/bpf.h>
#include <linux/bpf_ksock.h>
#include <linux/btf.h>
#include <linux/btf_ids.h>
#include <linux/in.h>
#include <linux/in6.h>
#include <linux/net.h>
#include <linux/refcount.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/socket.h>
#include <linux/unaligned.h>
#include <linux/workqueue.h>
#include <net/sock.h>

/**
 * struct bpf_ksock - refcounted BPF kernel socket context
 * @sock:	The underlying kernel socket.
 * @usage:	Reference counter.
 * @rwork:	RCU work for deferred cleanup (sock_release may sleep).
 */
struct bpf_ksock {
	struct socket *sock;
	refcount_t usage;
	struct rcu_work rwork;
};

static void ksock_release_work_fn(struct work_struct *work)
{
	struct bpf_ksock *ks;

	ks = container_of(to_rcu_work(work), struct bpf_ksock, rwork);
	sock_release(ks->sock);
	kfree(ks);
}

static bool bpf_ksock_has_user_task_context(void)
{
	/*
	 * Task work can run from do_exit() after exit_nsproxy_namespaces()
	 * cleared current->nsproxy, while current is still not a kthread.
	 */
	return !(current->flags & PF_KTHREAD) && current->nsproxy;
}

__bpf_kfunc_start_defs();

/**
 * bpf_ksock_create() - Create a BPF kernel socket.
 *
 * Allocates and creates a kernel socket.
 *
 * The returned context must either be stored in a map as a kptr, or
 * freed with bpf_ksock_release().
 *
 * This function may sleep (sock_create), so it can only be used
 * in sleepable BPF programs (SYSCALL).
 * It cannot be called from a BPF workqueue callback because that callback
 * does not retain the invoking task's namespace or security context.
 *
 * @opts:	Pointer to struct bpf_ksock_create_opts with socket parameters.
 * @opts__sz:	Size of the opts struct.
 * @err__uninit:	Integer to store error code when NULL is returned.
 */
__bpf_kfunc struct bpf_ksock *
bpf_ksock_create(const struct bpf_ksock_create_opts *opts, u32 opts__sz,
		 int *err__uninit)
{
	struct bpf_ksock_create_opts opts_copy;
	struct bpf_ksock *ks;
	int err;

	/*
	 * sock_create() derives the network namespace, credentials, and cgroup
	 * from current. Kernel threads, including BPF workqueue callbacks, do
	 * not carry the context of the task that invoked the BPF program.
	 */
	if (!bpf_ksock_has_user_task_context()) {
		err = -EOPNOTSUPP;
		goto err_out;
	}

	if (!opts || opts__sz != sizeof(struct bpf_ksock_create_opts)) {
		err = -EINVAL;
		goto err_out;
	}

	opts_copy = (struct bpf_ksock_create_opts){
		.family = READ_ONCE(opts->family),
		.type = READ_ONCE(opts->type),
		.protocol = READ_ONCE(opts->protocol),
		.reserved = READ_ONCE(opts->reserved),
	};

	if (opts_copy.reserved) {
		err = -EINVAL;
		goto err_out;
	}

	if (opts_copy.family != AF_INET && opts_copy.family != AF_INET6) {
		err = -EAFNOSUPPORT;
		goto err_out;
	}

	if (opts_copy.type != SOCK_DGRAM) {
		err = -EPROTONOSUPPORT;
		goto err_out;
	}

	if (opts_copy.protocol != IPPROTO_UDP && opts_copy.protocol != 0) {
		err = -EPROTONOSUPPORT;
		goto err_out;
	}

	ks = kzalloc_obj(*ks);
	if (!ks) {
		err = -ENOMEM;
		goto err_out;
	}

	/*
	 * Use the normal current-task socket path so LSM/cgroup policy,
	 * socket labels, and the active netns reference match a socket(2)
	 * created by the BPF program's caller.
	 */
	err = sock_create(opts_copy.family, opts_copy.type, opts_copy.protocol,
			  &ks->sock);
	if (err)
		goto err_free;

	ks->sock->sk->sk_rcvbuf = SOCK_MIN_RCVBUF;
	ks->sock->sk->sk_userlocks |= SOCK_RCVBUF_LOCK;

	refcount_set(&ks->usage, 1);
	put_unaligned(0, err__uninit);
	return ks;

err_free:
	kfree(ks);
err_out:
	put_unaligned(err, err__uninit);
	return NULL;
}

/**
 * bpf_ksock_connect() - Connect a BPF kernel socket to a remote address.
 * @ks:		The BPF kernel socket context.
 * @addr:	Pointer to an IPv4 or IPv6 socket address.
 * @addr__sz:	Size of the address union.
 *
 * Connects the socket to the specified remote address and port.
 *
 * This function may sleep while connecting the socket, so it can only be used
 * in sleepable BPF programs (SYSCALL).
 *
 * Return: 0 on success, negative errno on error.
 */
__bpf_kfunc int bpf_ksock_connect(struct bpf_ksock *ks,
				  const union bpf_ksock_addr *addr,
				  u32 addr__sz)
{
	struct sockaddr_storage sa;
	int addrlen;

	if (!bpf_ksock_has_user_task_context())
		return -EOPNOTSUPP;

	if (!addr || addr__sz != sizeof(*addr))
		return -EINVAL;

	/* Kfunc memory arguments may be unaligned. */
	memcpy(&sa, addr, sizeof(*addr));

	switch (sa.ss_family) {
	case AF_INET:
		addrlen = sizeof(struct sockaddr_in);
		break;
	case AF_INET6:
		addrlen = sizeof(struct sockaddr_in6);
		break;
	default:
		return -EAFNOSUPPORT;
	}

	return connect_socket(ks->sock, &sa, addrlen, 0);
}

/**
 * bpf_ksock_acquire() - Acquire a reference to a BPF kernel socket.
 * @ks:	The BPF kernel socket context to acquire. Must be a
 *	trusted pointer (e.g. RCU-protected kptr from a map).
 *
 * The acquired context must either be stored in a map as a kptr, or
 * freed with bpf_ksock_release().
 */
__bpf_kfunc struct bpf_ksock *bpf_ksock_acquire(struct bpf_ksock *ks)
{
	if (!refcount_inc_not_zero(&ks->usage))
		return NULL;
	return ks;
}

/**
 * bpf_ksock_release() - Release a BPF kernel socket.
 * @ks:	The BPF kernel socket context to release.
 *
 * When the final reference is released, the socket is cleaned up via
 * queue_rcu_work() (since sock_release may sleep).
 */
__bpf_kfunc void bpf_ksock_release(struct bpf_ksock *ks)
{
	if (refcount_dec_and_test(&ks->usage)) {
		INIT_RCU_WORK(&ks->rwork, ksock_release_work_fn);
		queue_rcu_work(system_dfl_wq, &ks->rwork);
	}
}

__bpf_kfunc void bpf_ksock_release_dtor(void *ks)
{
	bpf_ksock_release(ks);
}
CFI_NOSEAL(bpf_ksock_release_dtor);

/**
 * bpf_ksock_send() - Send data through a BPF kernel socket.
 * @ks:		The BPF kernel socket context. Must be an acquired reference.
 * @data:	Pointer to the data to send.
 * @data__sz:	Size of the data to send.
 *
 * Sends data on a connected socket, best-effort and nonblocking. This may sleep
 * (kernel_sendmsg), so it can only be called from sleepable BPF programs.
 *
 * Return: Number of bytes sent on success, negative errno on error.
 */
__bpf_kfunc int bpf_ksock_send(struct bpf_ksock *ks, const void *data,
			       u32 data__sz)
{
	struct msghdr msg = {
		.msg_flags = MSG_DONTWAIT,
	};
	struct kvec iov = {
		.iov_base = (void *)data,
		.iov_len = data__sz,
	};
	int ret;

	if (!bpf_ksock_has_user_task_context())
		return -EOPNOTSUPP;

	ret = kernel_sendmsg(ks->sock, &msg, &iov, 1, data__sz);

	return ret;
}

__bpf_kfunc_end_defs();

BTF_KFUNCS_START(ksock_init_kfunc_btf_ids)
BTF_ID_FLAGS(func, bpf_ksock_create, KF_ACQUIRE | KF_RET_NULL | KF_SLEEPABLE)
BTF_ID_FLAGS(func, bpf_ksock_connect, KF_SLEEPABLE)
BTF_KFUNCS_END(ksock_init_kfunc_btf_ids)

static const struct btf_kfunc_id_set ksock_init_kfunc_set = {
	.owner = THIS_MODULE,
	.set = &ksock_init_kfunc_btf_ids,
};

BTF_KFUNCS_START(ksock_kfunc_btf_ids)
BTF_ID_FLAGS(func, bpf_ksock_release, KF_RELEASE)
BTF_ID_FLAGS(func, bpf_ksock_acquire, KF_ACQUIRE | KF_RCU | KF_RET_NULL)
BTF_ID_FLAGS(func, bpf_ksock_send, KF_SLEEPABLE)
BTF_KFUNCS_END(ksock_kfunc_btf_ids)

#ifdef CONFIG_BPF_LSM
BTF_ID_LIST_SINGLE(bpf_lsm_socket_sendmsg_id, func, bpf_lsm_socket_sendmsg)
#endif

static int bpf_ksock_kfunc_filter(const struct bpf_prog *prog, u32 kfunc_id)
{
	if (!btf_id_set8_contains(&ksock_kfunc_btf_ids, kfunc_id))
		return 0;

	if (prog->type == BPF_PROG_TYPE_SYSCALL)
		return 0;

#ifdef CONFIG_BPF_LSM
	if (prog->type == BPF_PROG_TYPE_LSM &&
	    prog->aux->attach_btf_id != bpf_lsm_socket_sendmsg_id[0])
		return 0;
#endif

	return -EACCES;
}

static const struct btf_kfunc_id_set ksock_kfunc_set = {
	.owner = THIS_MODULE,
	.set = &ksock_kfunc_btf_ids,
	.filter = bpf_ksock_kfunc_filter,
};

BTF_ID_LIST(bpf_ksock_dtor_ids)
BTF_ID(struct, bpf_ksock)
BTF_ID(func, bpf_ksock_release_dtor)

static int __init bpf_ksock_kfunc_init(void)
{
	int ret;
	const struct btf_id_dtor_kfunc bpf_ksock_dtors[] = {
		{
			.btf_id = bpf_ksock_dtor_ids[0],
			.kfunc_btf_id = bpf_ksock_dtor_ids[1],
		},
	};

	ret = register_btf_kfunc_id_set(BPF_PROG_TYPE_SYSCALL,
					&ksock_init_kfunc_set);
	ret = ret ?: register_btf_kfunc_id_set(BPF_PROG_TYPE_SYSCALL,
					       &ksock_kfunc_set);
	ret = ret ?: register_btf_kfunc_id_set(BPF_PROG_TYPE_LSM,
					       &ksock_kfunc_set);
	return ret ?: register_btf_id_dtor_kfuncs(bpf_ksock_dtors,
						  ARRAY_SIZE(bpf_ksock_dtors),
						  THIS_MODULE);
}

late_initcall(bpf_ksock_kfunc_init);
