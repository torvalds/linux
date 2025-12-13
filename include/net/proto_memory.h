/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _PROTO_MEMORY_H
#define _PROTO_MEMORY_H

#include <net/sock.h>
#include <net/hotdata.h>

/* 1 MB per cpu, in page units */
#define SK_MEMORY_PCPU_RESERVE (1 << (20 - PAGE_SHIFT))

static inline bool sk_has_memory_pressure(const struct sock *sk)
{
	return sk->sk_prot->memory_pressure != NULL;
}

static inline bool
proto_memory_pressure(const struct proto *prot)
{
	if (!prot->memory_pressure)
		return false;
	return !!READ_ONCE(*prot->memory_pressure);
}

static inline bool sk_under_global_memory_pressure(const struct sock *sk)
{
	return proto_memory_pressure(sk->sk_prot);
}

static inline bool sk_under_memory_pressure(const struct sock *sk)
{
	if (!sk->sk_prot->memory_pressure)
		return false;

	if (mem_cgroup_sk_enabled(sk) &&
	    mem_cgroup_sk_under_memory_pressure(sk))
		return true;

	return !!READ_ONCE(*sk->sk_prot->memory_pressure);
}

static inline long
proto_memory_allocated(const struct proto *prot)
{
	return max(0L, atomic_long_read(prot->memory_allocated));
}

static inline long
sk_memory_allocated(const struct sock *sk)
{
	return proto_memory_allocated(sk->sk_prot);
}

static inline void proto_memory_pcpu_drain(struct proto *proto)
{
	int val = this_cpu_xchg(*proto->per_cpu_fw_alloc, 0);

	if (val)
		atomic_long_add(val, proto->memory_allocated);
}

static inline void
sk_memory_allocated_add(const struct sock *sk, int val)
{
	struct proto *proto = sk->sk_prot;

	val = this_cpu_add_return(*proto->per_cpu_fw_alloc, val);

	if (unlikely(val >= READ_ONCE(net_hotdata.sysctl_mem_pcpu_rsv)))
		proto_memory_pcpu_drain(proto);
}

static inline void
sk_memory_allocated_sub(const struct sock *sk, int val)
{
	struct proto *proto = sk->sk_prot;

	val = this_cpu_sub_return(*proto->per_cpu_fw_alloc, val);

	if (unlikely(val <= -READ_ONCE(net_hotdata.sysctl_mem_pcpu_rsv)))
		proto_memory_pcpu_drain(proto);
}

#endif /* _PROTO_MEMORY_H */
