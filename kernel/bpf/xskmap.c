// SPDX-License-Identifier: GPL-2.0
/* XSKMAP used for AF_XDP sockets
 * Copyright(c) 2018 Intel Corporation.
 */

#include <linux/bpf.h>
#include <linux/capability.h>
#include <net/xdp_sock.h>
#include <linux/slab.h>
#include <linux/sched.h>

struct xsk_map {
	struct bpf_map map;
	struct xdp_sock **xsk_map;
	struct list_head __percpu *flush_list;
};

static struct bpf_map *xsk_map_alloc(union bpf_attr *attr)
{
	int cpu, err = -EINVAL;
	struct xsk_map *m;
	u64 cost;

	if (!capable(CAP_NET_ADMIN))
		return ERR_PTR(-EPERM);

	if (attr->max_entries == 0 || attr->key_size != 4 ||
	    attr->value_size != 4 ||
	    attr->map_flags & ~(BPF_F_NUMA_NODE | BPF_F_RDONLY | BPF_F_WRONLY))
		return ERR_PTR(-EINVAL);

	m = kzalloc(sizeof(*m), GFP_USER);
	if (!m)
		return ERR_PTR(-ENOMEM);

	bpf_map_init_from_attr(&m->map, attr);

	cost = (u64)m->map.max_entries * sizeof(struct xdp_sock *);
	cost += sizeof(struct list_head) * num_possible_cpus();
	if (cost >= U32_MAX - PAGE_SIZE)
		goto free_m;

	m->map.pages = round_up(cost, PAGE_SIZE) >> PAGE_SHIFT;

	/* Notice returns -EPERM on if map size is larger than memlock limit */
	err = bpf_map_precharge_memlock(m->map.pages);
	if (err)
		goto free_m;

	err = -ENOMEM;

	m->flush_list = alloc_percpu(struct list_head);
	if (!m->flush_list)
		goto free_m;

	for_each_possible_cpu(cpu)
		INIT_LIST_HEAD(per_cpu_ptr(m->flush_list, cpu));

	m->xsk_map = bpf_map_area_alloc(m->map.max_entries *
					sizeof(struct xdp_sock *),
					m->map.numa_node);
	if (!m->xsk_map)
		goto free_percpu;
	return &m->map;

free_percpu:
	free_percpu(m->flush_list);
free_m:
	kfree(m);
	return ERR_PTR(err);
}

static void xsk_map_free(struct bpf_map *map)
{
	struct xsk_map *m = container_of(map, struct xsk_map, map);
	int i;

	synchronize_net();

	for (i = 0; i < map->max_entries; i++) {
		struct xdp_sock *xs;

		xs = m->xsk_map[i];
		if (!xs)
			continue;

		sock_put((struct sock *)xs);
	}

	free_percpu(m->flush_list);
	bpf_map_area_free(m->xsk_map);
	kfree(m);
}

static int xsk_map_get_next_key(struct bpf_map *map, void *key, void *next_key)
{
	struct xsk_map *m = container_of(map, struct xsk_map, map);
	u32 index = key ? *(u32 *)key : U32_MAX;
	u32 *next = next_key;

	if (index >= m->map.max_entries) {
		*next = 0;
		return 0;
	}

	if (index == m->map.max_entries - 1)
		return -ENOENT;
	*next = index + 1;
	return 0;
}

struct xdp_sock *__xsk_map_lookup_elem(struct bpf_map *map, u32 key)
{
	struct xsk_map *m = container_of(map, struct xsk_map, map);
	struct xdp_sock *xs;

	if (key >= map->max_entries)
		return NULL;

	xs = READ_ONCE(m->xsk_map[key]);
	return xs;
}

int __xsk_map_redirect(struct bpf_map *map, struct xdp_buff *xdp,
		       struct xdp_sock *xs)
{
	struct xsk_map *m = container_of(map, struct xsk_map, map);
	struct list_head *flush_list = this_cpu_ptr(m->flush_list);
	int err;

	err = xsk_rcv(xs, xdp);
	if (err)
		return err;

	if (!xs->flush_node.prev)
		list_add(&xs->flush_node, flush_list);

	return 0;
}

void __xsk_map_flush(struct bpf_map *map)
{
	struct xsk_map *m = container_of(map, struct xsk_map, map);
	struct list_head *flush_list = this_cpu_ptr(m->flush_list);
	struct xdp_sock *xs, *tmp;

	list_for_each_entry_safe(xs, tmp, flush_list, flush_node) {
		xsk_flush(xs);
		__list_del(xs->flush_node.prev, xs->flush_node.next);
		xs->flush_node.prev = NULL;
	}
}

static void *xsk_map_lookup_elem(struct bpf_map *map, void *key)
{
	return NULL;
}

static int xsk_map_update_elem(struct bpf_map *map, void *key, void *value,
			       u64 map_flags)
{
	struct xsk_map *m = container_of(map, struct xsk_map, map);
	u32 i = *(u32 *)key, fd = *(u32 *)value;
	struct xdp_sock *xs, *old_xs;
	struct socket *sock;
	int err;

	if (unlikely(map_flags > BPF_EXIST))
		return -EINVAL;
	if (unlikely(i >= m->map.max_entries))
		return -E2BIG;
	if (unlikely(map_flags == BPF_NOEXIST))
		return -EEXIST;

	sock = sockfd_lookup(fd, &err);
	if (!sock)
		return err;

	if (sock->sk->sk_family != PF_XDP) {
		sockfd_put(sock);
		return -EOPNOTSUPP;
	}

	xs = (struct xdp_sock *)sock->sk;

	if (!xsk_is_setup_for_bpf_map(xs)) {
		sockfd_put(sock);
		return -EOPNOTSUPP;
	}

	sock_hold(sock->sk);

	old_xs = xchg(&m->xsk_map[i], xs);
	if (old_xs) {
		/* Make sure we've flushed everything. */
		synchronize_net();
		sock_put((struct sock *)old_xs);
	}

	sockfd_put(sock);
	return 0;
}

static int xsk_map_delete_elem(struct bpf_map *map, void *key)
{
	struct xsk_map *m = container_of(map, struct xsk_map, map);
	struct xdp_sock *old_xs;
	int k = *(u32 *)key;

	if (k >= map->max_entries)
		return -EINVAL;

	old_xs = xchg(&m->xsk_map[k], NULL);
	if (old_xs) {
		/* Make sure we've flushed everything. */
		synchronize_net();
		sock_put((struct sock *)old_xs);
	}

	return 0;
}

const struct bpf_map_ops xsk_map_ops = {
	.map_alloc = xsk_map_alloc,
	.map_free = xsk_map_free,
	.map_get_next_key = xsk_map_get_next_key,
	.map_lookup_elem = xsk_map_lookup_elem,
	.map_update_elem = xsk_map_update_elem,
	.map_delete_elem = xsk_map_delete_elem,
};


