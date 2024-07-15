// SPDX-License-Identifier: GPL-2.0

#include "vmlinux.h"
#include "bpf_tracing_net.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

char _license[] SEC("license") = "GPL";

extern bool CONFIG_SECURITY_SELINUX __kconfig __weak;
extern bool CONFIG_SECURITY_SMACK __kconfig __weak;
extern bool CONFIG_SECURITY_APPARMOR __kconfig __weak;

#ifndef AF_PACKET
#define AF_PACKET 17
#endif

#ifndef AF_UNIX
#define AF_UNIX 1
#endif

#ifndef EPERM
#define EPERM 1
#endif

struct {
	__uint(type, BPF_MAP_TYPE_CGROUP_STORAGE);
	__type(key, __u64);
	__type(value, __u64);
} cgroup_storage SEC(".maps");

int called_socket_post_create;
int called_socket_post_create2;
int called_socket_bind;
int called_socket_bind2;
int called_socket_alloc;
int called_socket_clone;

static __always_inline int test_local_storage(void)
{
	__u64 *val;

	val = bpf_get_local_storage(&cgroup_storage, 0);
	if (!val)
		return 0;
	*val += 1;

	return 1;
}

static __always_inline int real_create(struct socket *sock, int family,
				       int protocol)
{
	struct sock *sk;
	int prio = 123;

	/* Reject non-tx-only AF_PACKET. */
	if (family == AF_PACKET && protocol != 0)
		return 0; /* EPERM */

	sk = sock->sk;
	if (!sk)
		return 1;

	/* The rest of the sockets get default policy. */
	if (bpf_setsockopt(sk, SOL_SOCKET, SO_PRIORITY, &prio, sizeof(prio)))
		return 0; /* EPERM */

	/* Make sure bpf_getsockopt is allowed and works. */
	prio = 0;
	if (bpf_getsockopt(sk, SOL_SOCKET, SO_PRIORITY, &prio, sizeof(prio)))
		return 0; /* EPERM */
	if (prio != 123)
		return 0; /* EPERM */

	/* Can access cgroup local storage. */
	if (!test_local_storage())
		return 0; /* EPERM */

	return 1;
}

/* __cgroup_bpf_run_lsm_socket */
SEC("lsm_cgroup/socket_post_create")
int BPF_PROG(socket_post_create, struct socket *sock, int family,
	     int type, int protocol, int kern)
{
	called_socket_post_create++;
	return real_create(sock, family, protocol);
}

/* __cgroup_bpf_run_lsm_socket */
SEC("lsm_cgroup/socket_post_create")
int BPF_PROG(socket_post_create2, struct socket *sock, int family,
	     int type, int protocol, int kern)
{
	called_socket_post_create2++;
	return real_create(sock, family, protocol);
}

static __always_inline int real_bind(struct socket *sock,
				     struct sockaddr *address,
				     int addrlen)
{
	struct sockaddr_ll sa = {};
	struct sock *sk = sock->sk;

	if (!sk)
		return 1;

	if (sk->__sk_common.skc_family != AF_PACKET)
		return 1;

	if (sk->sk_kern_sock)
		return 1;

	bpf_probe_read_kernel(&sa, sizeof(sa), address);
	if (sa.sll_protocol)
		return 0; /* EPERM */

	/* Can access cgroup local storage. */
	if (!test_local_storage())
		return 0; /* EPERM */

	return 1;
}

/* __cgroup_bpf_run_lsm_socket */
SEC("lsm_cgroup/socket_bind")
int BPF_PROG(socket_bind, struct socket *sock, struct sockaddr *address,
	     int addrlen)
{
	called_socket_bind++;
	return real_bind(sock, address, addrlen);
}

/* __cgroup_bpf_run_lsm_socket */
SEC("lsm_cgroup/socket_bind")
int BPF_PROG(socket_bind2, struct socket *sock, struct sockaddr *address,
	     int addrlen)
{
	called_socket_bind2++;
	return real_bind(sock, address, addrlen);
}

/* __cgroup_bpf_run_lsm_current (via bpf_lsm_current_hooks) */
SEC("lsm_cgroup/sk_alloc_security")
int BPF_PROG(socket_alloc, struct sock *sk, int family, gfp_t priority)
{
	called_socket_alloc++;
	/* if already have non-bpf lsms installed, EPERM will cause memory leak of non-bpf lsms */
	if (CONFIG_SECURITY_SELINUX || CONFIG_SECURITY_SMACK || CONFIG_SECURITY_APPARMOR)
		return 1;

	if (family == AF_UNIX)
		return 0; /* EPERM */

	/* Can access cgroup local storage. */
	if (!test_local_storage())
		return 0; /* EPERM */

	return 1;
}

/* __cgroup_bpf_run_lsm_sock */
SEC("lsm_cgroup/inet_csk_clone")
int BPF_PROG(socket_clone, struct sock *newsk, const struct request_sock *req)
{
	int prio = 234;

	if (!newsk)
		return 1;

	/* Accepted request sockets get a different priority. */
	if (bpf_setsockopt(newsk, SOL_SOCKET, SO_PRIORITY, &prio, sizeof(prio)))
		return 1;

	/* Make sure bpf_getsockopt is allowed and works. */
	prio = 0;
	if (bpf_getsockopt(newsk, SOL_SOCKET, SO_PRIORITY, &prio, sizeof(prio)))
		return 1;
	if (prio != 234)
		return 1;

	/* Can access cgroup local storage. */
	if (!test_local_storage())
		return 1;

	called_socket_clone++;

	return 1;
}
