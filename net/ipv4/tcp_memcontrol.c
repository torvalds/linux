#include <net/tcp.h>
#include <net/tcp_memcontrol.h>
#include <net/sock.h>
#include <net/ip.h>
#include <linux/nsproxy.h>
#include <linux/memcontrol.h>
#include <linux/module.h>

int tcp_init_cgroup(struct mem_cgroup *memcg, struct cgroup_subsys *ss)
{
	/*
	 * The root cgroup does not use page_counters, but rather,
	 * rely on the data already collected by the network
	 * subsystem
	 */
	struct mem_cgroup *parent = parent_mem_cgroup(memcg);
	struct page_counter *counter_parent = NULL;
	struct cg_proto *cg_proto, *parent_cg;

	cg_proto = tcp_prot.proto_cgroup(memcg);
	if (!cg_proto)
		return 0;

	cg_proto->sysctl_mem[0] = sysctl_tcp_mem[0];
	cg_proto->sysctl_mem[1] = sysctl_tcp_mem[1];
	cg_proto->sysctl_mem[2] = sysctl_tcp_mem[2];
	cg_proto->memory_pressure = 0;
	cg_proto->memcg = memcg;

	parent_cg = tcp_prot.proto_cgroup(parent);
	if (parent_cg)
		counter_parent = &parent_cg->memory_allocated;

	page_counter_init(&cg_proto->memory_allocated, counter_parent);
	percpu_counter_init(&cg_proto->sockets_allocated, 0, GFP_KERNEL);

	return 0;
}
EXPORT_SYMBOL(tcp_init_cgroup);

void tcp_destroy_cgroup(struct mem_cgroup *memcg)
{
	struct cg_proto *cg_proto;

	cg_proto = tcp_prot.proto_cgroup(memcg);
	if (!cg_proto)
		return;

	percpu_counter_destroy(&cg_proto->sockets_allocated);

	if (test_bit(MEMCG_SOCK_ACTIVATED, &cg_proto->flags))
		static_key_slow_dec(&memcg_socket_limit_enabled);

}
EXPORT_SYMBOL(tcp_destroy_cgroup);

static int tcp_update_limit(struct mem_cgroup *memcg, unsigned long nr_pages)
{
	struct cg_proto *cg_proto;
	int i;
	int ret;

	cg_proto = tcp_prot.proto_cgroup(memcg);
	if (!cg_proto)
		return -EINVAL;

	ret = page_counter_limit(&cg_proto->memory_allocated, nr_pages);
	if (ret)
		return ret;

	for (i = 0; i < 3; i++)
		cg_proto->sysctl_mem[i] = min_t(long, nr_pages,
						sysctl_tcp_mem[i]);

	if (nr_pages == PAGE_COUNTER_MAX)
		clear_bit(MEMCG_SOCK_ACTIVE, &cg_proto->flags);
	else {
		/*
		 * The active bit needs to be written after the static_key
		 * update. This is what guarantees that the socket activation
		 * function is the last one to run. See sock_update_memcg() for
		 * details, and note that we don't mark any socket as belonging
		 * to this memcg until that flag is up.
		 *
		 * We need to do this, because static_keys will span multiple
		 * sites, but we can't control their order. If we mark a socket
		 * as accounted, but the accounting functions are not patched in
		 * yet, we'll lose accounting.
		 *
		 * We never race with the readers in sock_update_memcg(),
		 * because when this value change, the code to process it is not
		 * patched in yet.
		 *
		 * The activated bit is used to guarantee that no two writers
		 * will do the update in the same memcg. Without that, we can't
		 * properly shutdown the static key.
		 */
		if (!test_and_set_bit(MEMCG_SOCK_ACTIVATED, &cg_proto->flags))
			static_key_slow_inc(&memcg_socket_limit_enabled);
		set_bit(MEMCG_SOCK_ACTIVE, &cg_proto->flags);
	}

	return 0;
}

enum {
	RES_USAGE,
	RES_LIMIT,
	RES_MAX_USAGE,
	RES_FAILCNT,
};

static DEFINE_MUTEX(tcp_limit_mutex);

static ssize_t tcp_cgroup_write(struct kernfs_open_file *of,
				char *buf, size_t nbytes, loff_t off)
{
	struct mem_cgroup *memcg = mem_cgroup_from_css(of_css(of));
	unsigned long nr_pages;
	int ret = 0;

	buf = strstrip(buf);

	switch (of_cft(of)->private) {
	case RES_LIMIT:
		/* see memcontrol.c */
		ret = page_counter_memparse(buf, "-1", &nr_pages);
		if (ret)
			break;
		mutex_lock(&tcp_limit_mutex);
		ret = tcp_update_limit(memcg, nr_pages);
		mutex_unlock(&tcp_limit_mutex);
		break;
	default:
		ret = -EINVAL;
		break;
	}
	return ret ?: nbytes;
}

static u64 tcp_cgroup_read(struct cgroup_subsys_state *css, struct cftype *cft)
{
	struct mem_cgroup *memcg = mem_cgroup_from_css(css);
	struct cg_proto *cg_proto = tcp_prot.proto_cgroup(memcg);
	u64 val;

	switch (cft->private) {
	case RES_LIMIT:
		if (!cg_proto)
			return PAGE_COUNTER_MAX;
		val = cg_proto->memory_allocated.limit;
		val *= PAGE_SIZE;
		break;
	case RES_USAGE:
		if (!cg_proto)
			val = atomic_long_read(&tcp_memory_allocated);
		else
			val = page_counter_read(&cg_proto->memory_allocated);
		val *= PAGE_SIZE;
		break;
	case RES_FAILCNT:
		if (!cg_proto)
			return 0;
		val = cg_proto->memory_allocated.failcnt;
		break;
	case RES_MAX_USAGE:
		if (!cg_proto)
			return 0;
		val = cg_proto->memory_allocated.watermark;
		val *= PAGE_SIZE;
		break;
	default:
		BUG();
	}
	return val;
}

static ssize_t tcp_cgroup_reset(struct kernfs_open_file *of,
				char *buf, size_t nbytes, loff_t off)
{
	struct mem_cgroup *memcg;
	struct cg_proto *cg_proto;

	memcg = mem_cgroup_from_css(of_css(of));
	cg_proto = tcp_prot.proto_cgroup(memcg);
	if (!cg_proto)
		return nbytes;

	switch (of_cft(of)->private) {
	case RES_MAX_USAGE:
		page_counter_reset_watermark(&cg_proto->memory_allocated);
		break;
	case RES_FAILCNT:
		cg_proto->memory_allocated.failcnt = 0;
		break;
	}

	return nbytes;
}

static struct cftype tcp_files[] = {
	{
		.name = "kmem.tcp.limit_in_bytes",
		.write = tcp_cgroup_write,
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
		.write = tcp_cgroup_reset,
		.read_u64 = tcp_cgroup_read,
	},
	{
		.name = "kmem.tcp.max_usage_in_bytes",
		.private = RES_MAX_USAGE,
		.write = tcp_cgroup_reset,
		.read_u64 = tcp_cgroup_read,
	},
	{ }	/* terminate */
};

static int __init tcp_memcontrol_init(void)
{
	WARN_ON(cgroup_add_legacy_cftypes(&memory_cgrp_subsys, tcp_files));
	return 0;
}
__initcall(tcp_memcontrol_init);
