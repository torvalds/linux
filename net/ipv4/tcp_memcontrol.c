#include <net/tcp.h>
#include <net/tcp_memcontrol.h>
#include <net/sock.h>
#include <net/ip.h>
#include <linux/nsproxy.h>
#include <linux/memcontrol.h>
#include <linux/module.h>

static inline struct tcp_memcontrol *tcp_from_cgproto(struct cg_proto *cg_proto)
{
	return container_of(cg_proto, struct tcp_memcontrol, cg_proto);
}

static void memcg_tcp_enter_memory_pressure(struct sock *sk)
{
	if (sk->sk_cgrp->memory_pressure)
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
	struct net *net = current->nsproxy->net_ns;

	cg_proto = tcp_prot.proto_cgroup(memcg);
	if (!cg_proto)
		return 0;

	tcp = tcp_from_cgproto(cg_proto);

	tcp->tcp_prot_mem[0] = net->ipv4.sysctl_tcp_mem[0];
	tcp->tcp_prot_mem[1] = net->ipv4.sysctl_tcp_mem[1];
	tcp->tcp_prot_mem[2] = net->ipv4.sysctl_tcp_mem[2];
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

void tcp_destroy_cgroup(struct cgroup *cgrp)
{
	struct mem_cgroup *memcg = mem_cgroup_from_cont(cgrp);
	struct cg_proto *cg_proto;
	struct tcp_memcontrol *tcp;
	u64 val;

	cg_proto = tcp_prot.proto_cgroup(memcg);
	if (!cg_proto)
		return;

	tcp = tcp_from_cgproto(cg_proto);
	percpu_counter_destroy(&tcp->tcp_sockets_allocated);

	val = res_counter_read_u64(&tcp->tcp_memory_allocated, RES_LIMIT);

	if (val != RESOURCE_MAX)
		static_key_slow_dec(&memcg_socket_limit_enabled);
}
EXPORT_SYMBOL(tcp_destroy_cgroup);

static int tcp_update_limit(struct mem_cgroup *memcg, u64 val)
{
	struct net *net = current->nsproxy->net_ns;
	struct tcp_memcontrol *tcp;
	struct cg_proto *cg_proto;
	u64 old_lim;
	int i;
	int ret;

	cg_proto = tcp_prot.proto_cgroup(memcg);
	if (!cg_proto)
		return -EINVAL;

	if (val > RESOURCE_MAX)
		val = RESOURCE_MAX;

	tcp = tcp_from_cgproto(cg_proto);

	old_lim = res_counter_read_u64(&tcp->tcp_memory_allocated, RES_LIMIT);
	ret = res_counter_set_limit(&tcp->tcp_memory_allocated, val);
	if (ret)
		return ret;

	for (i = 0; i < 3; i++)
		tcp->tcp_prot_mem[i] = min_t(long, val >> PAGE_SHIFT,
					     net->ipv4.sysctl_tcp_mem[i]);

	if (val == RESOURCE_MAX && old_lim != RESOURCE_MAX)
		static_key_slow_dec(&memcg_socket_limit_enabled);
	else if (old_lim == RESOURCE_MAX && val != RESOURCE_MAX)
		static_key_slow_inc(&memcg_socket_limit_enabled);

	return 0;
}

static int tcp_cgroup_write(struct cgroup *cont, struct cftype *cft,
			    const char *buffer)
{
	struct mem_cgroup *memcg = mem_cgroup_from_cont(cont);
	unsigned long long val;
	int ret = 0;

	switch (cft->private) {
	case RES_LIMIT:
		/* see memcontrol.c */
		ret = res_counter_memparse_write_strategy(buffer, &val);
		if (ret)
			break;
		ret = tcp_update_limit(memcg, val);
		break;
	default:
		ret = -EINVAL;
		break;
	}
	return ret;
}

static u64 tcp_read_stat(struct mem_cgroup *memcg, int type, u64 default_val)
{
	struct tcp_memcontrol *tcp;
	struct cg_proto *cg_proto;

	cg_proto = tcp_prot.proto_cgroup(memcg);
	if (!cg_proto)
		return default_val;

	tcp = tcp_from_cgproto(cg_proto);
	return res_counter_read_u64(&tcp->tcp_memory_allocated, type);
}

static u64 tcp_read_usage(struct mem_cgroup *memcg)
{
	struct tcp_memcontrol *tcp;
	struct cg_proto *cg_proto;

	cg_proto = tcp_prot.proto_cgroup(memcg);
	if (!cg_proto)
		return atomic_long_read(&tcp_memory_allocated) << PAGE_SHIFT;

	tcp = tcp_from_cgproto(cg_proto);
	return res_counter_read_u64(&tcp->tcp_memory_allocated, RES_USAGE);
}

static u64 tcp_cgroup_read(struct cgroup *cont, struct cftype *cft)
{
	struct mem_cgroup *memcg = mem_cgroup_from_cont(cont);
	u64 val;

	switch (cft->private) {
	case RES_LIMIT:
		val = tcp_read_stat(memcg, RES_LIMIT, RESOURCE_MAX);
		break;
	case RES_USAGE:
		val = tcp_read_usage(memcg);
		break;
	case RES_FAILCNT:
	case RES_MAX_USAGE:
		val = tcp_read_stat(memcg, cft->private, 0);
		break;
	default:
		BUG();
	}
	return val;
}

static int tcp_cgroup_reset(struct cgroup *cont, unsigned int event)
{
	struct mem_cgroup *memcg;
	struct tcp_memcontrol *tcp;
	struct cg_proto *cg_proto;

	memcg = mem_cgroup_from_cont(cont);
	cg_proto = tcp_prot.proto_cgroup(memcg);
	if (!cg_proto)
		return 0;
	tcp = tcp_from_cgproto(cg_proto);

	switch (event) {
	case RES_MAX_USAGE:
		res_counter_reset_max(&tcp->tcp_memory_allocated);
		break;
	case RES_FAILCNT:
		res_counter_reset_failcnt(&tcp->tcp_memory_allocated);
		break;
	}

	return 0;
}

unsigned long long tcp_max_memory(const struct mem_cgroup *memcg)
{
	struct tcp_memcontrol *tcp;
	struct cg_proto *cg_proto;

	cg_proto = tcp_prot.proto_cgroup((struct mem_cgroup *)memcg);
	if (!cg_proto)
		return 0;

	tcp = tcp_from_cgproto(cg_proto);
	return res_counter_read_u64(&tcp->tcp_memory_allocated, RES_LIMIT);
}

void tcp_prot_mem(struct mem_cgroup *memcg, long val, int idx)
{
	struct tcp_memcontrol *tcp;
	struct cg_proto *cg_proto;

	cg_proto = tcp_prot.proto_cgroup(memcg);
	if (!cg_proto)
		return;

	tcp = tcp_from_cgproto(cg_proto);

	tcp->tcp_prot_mem[idx] = val;
}

static struct cftype tcp_files[] = {
	{
		.name = "kmem.tcp.limit_in_bytes",
		.write_string = tcp_cgroup_write,
		.read_u64 = tcp_cgroup_read,
		.private = RES_LIMIT,
	},
	{
		.name = "kmem.tcp.usage_in_bytes",
		.read_u64 = tcp_cgroup_read,
		.private = RES_USAGE,
	},
	{
		.name = "kmem.tcp.failcnt",
		.private = RES_FAILCNT,
		.trigger = tcp_cgroup_reset,
		.read_u64 = tcp_cgroup_read,
	},
	{
		.name = "kmem.tcp.max_usage_in_bytes",
		.private = RES_MAX_USAGE,
		.trigger = tcp_cgroup_reset,
		.read_u64 = tcp_cgroup_read,
	},
	{ }	/* terminate */
};

static int __init tcp_memcontrol_init(void)
{
	WARN_ON(cgroup_add_cftypes(&mem_cgroup_subsys, tcp_files));
	return 0;
}
__initcall(tcp_memcontrol_init);
