#include <net/tcp.h>
#include <net/tcp_memcontrol.h>
#include <net/sock.h>
#include <linux/memcontrol.h>
#include <linux/module.h>

static inline struct tcp_memcontrol *tcp_from_cgproto(struct cg_proto *cg_proto)
{
	return container_of(cg_proto, struct tcp_memcontrol, cg_proto);
}

static void memcg_tcp_enter_memory_pressure(struct sock *sk)
{
	if (!sk->sk_cgrp->memory_pressure)
		*sk->sk_cgrp->memory_pressure = 1;
}
EXPORT_SYMBOL(memcg_tcp_enter_memory_pressure);

int tcp_init_cgroup(struct cgroup *cgrp, struct cgroup_subsys *ss)
{
	/*
	 * The root cgroup does not use res_counters, but rather,
	 * rely on the data already collected by the network
	 * subsystem
	 */
	struct res_counter *res_parent = NULL;
	struct cg_proto *cg_proto, *parent_cg;
	struct tcp_memcontrol *tcp;
	struct mem_cgroup *memcg = mem_cgroup_from_cont(cgrp);
	struct mem_cgroup *parent = parent_mem_cgroup(memcg);

	cg_proto = tcp_prot.proto_cgroup(memcg);
	if (!cg_proto)
		return 0;

	tcp = tcp_from_cgproto(cg_proto);

	tcp->tcp_prot_mem[0] = sysctl_tcp_mem[0];
	tcp->tcp_prot_mem[1] = sysctl_tcp_mem[1];
	tcp->tcp_prot_mem[2] = sysctl_tcp_mem[2];
	tcp->tcp_memory_pressure = 0;

	parent_cg = tcp_prot.proto_cgroup(parent);
	if (parent_cg)
		res_parent = parent_cg->memory_allocated;

	res_counter_init(&tcp->tcp_memory_allocated, res_parent);
	percpu_counter_init(&tcp->tcp_sockets_allocated, 0);

	cg_proto->enter_memory_pressure = memcg_tcp_enter_memory_pressure;
	cg_proto->memory_pressure = &tcp->tcp_memory_pressure;
	cg_proto->sysctl_mem = tcp->tcp_prot_mem;
	cg_proto->memory_allocated = &tcp->tcp_memory_allocated;
	cg_proto->sockets_allocated = &tcp->tcp_sockets_allocated;
	cg_proto->memcg = memcg;

	return 0;
}
EXPORT_SYMBOL(tcp_init_cgroup);

void tcp_destroy_cgroup(struct cgroup *cgrp, struct cgroup_subsys *ss)
{
	struct mem_cgroup *memcg = mem_cgroup_from_cont(cgrp);
	struct cg_proto *cg_proto;
	struct tcp_memcontrol *tcp;

	cg_proto = tcp_prot.proto_cgroup(memcg);
	if (!cg_proto)
		return;

	tcp = tcp_from_cgproto(cg_proto);
	percpu_counter_destroy(&tcp->tcp_sockets_allocated);
}
EXPORT_SYMBOL(tcp_destroy_cgroup);
