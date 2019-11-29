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
	spinlock_t lock; /* Synchronize map updates */
};

int xsk_map_inc(struct xsk_map *map)
{
	struct bpf_map *m = &map->map;

	m = bpf_map_inc(m, false);
	return PTR_ERR_OR_ZERO(m);
}

void xsk_map_put(struct xsk_map *map)
{
	bpf_map_put(&map->map);
}

static struct xsk_map_node *xsk_map_node_alloc(struct xsk_map *map,
					       struct xdp_sock **map_entry)
{
	struct xsk_map_node *node;
	int err;

	node = kzalloc(sizeof(*node), GFP_ATOMIC | __GFP_NOWARN);
	if (!node)
		return ERR_PTR(-ENOMEM);

	err = xsk_map_inc(map);
	if (err) {
		kfree(node);
		return ERR_PTR(err);
	}

	node->map = map;
	node->map_entry = map_entry;
	return node;
}

static void xsk_map_node_free(struct xsk_map_node *node)
{
	xsk_map_put(node->map);
	kfree(node);
}

static void xsk_map_sock_add(struct xdp_sock *xs, struct xsk_map_node *node)
{
	spin_lock_bh(&xs->map_list_lock);
	list_add_tail(&node->node, &xs->map_list);
	spin_unlock_bh(&xs->map_list_lock);
}

static void xsk_map_sock_delete(struct xdp_sock *xs,
				struct xdp_sock **map_entry)
{
	struct xsk_map_node *n, *tmp;

	spin_lock_bh(&xs->map_list_lock);
	list_for_each_entry_safe(n, tmp, &xs->map_list, node) {
		if (map_entry == n->map_entry) {
			list_del(&n->node);
			xsk_map_node_free(n);
		}
	}
	spin_unlock_bh(&xs->map_list_lock);
}

static struct bpf_map *xsk_map_alloc(union bpf_attr *attr)
{
	struct xsk_map *m;
	int cpu, err;
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
	spin_lock_init(&m->lock);

	cost = (u64)m->map.max_entries * sizeof(struct xdp_sock *);
	cost += sizeof(struct list_head) * num_possible_cpus();

	/* Notice returns -EPERM on if map size is larger than memlock limit */
	err = bpf_map_charge_init(&m->map.memory, cost);
	if (err)
		goto free_m;

	err = -ENOMEM;

	m->flush_list = alloc_percpu(struct list_head);
	if (!m->flush_list)
		goto free_charge;

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
free_charge:
	bpf_map_charge_finish(&m->map.memory);
free_m:
	kfree(m);
	return ERR_PTR(err);
}

static void xsk_map_free(struct bpf_map *map)
{
	struct xsk_map *m = container_of(map, struct xsk_map, map);

	bpf_clear_redirect_map(map);
	synchronize_net();
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
		__list_del_clearprev(&xs->flush_node);
	}
}

static void *xsk_map_lookup_elem(struct bpf_map *map, void *key)
{
	WARN_ON_ONCE(!rcu_read_lock_held());
	return __xsk_map_lookup_elem(map, *(u32 *)key);
}

static void *xsk_map_lookup_elem_sys_only(struct bpf_map *map, void *key)
{
	return ERR_PTR(-EOPNOTSUPP);
}

static int xsk_map_update_elem(struct bpf_map *map, void *key, void *value,
			       u64 map_flags)
{
	struct xsk_map *m = container_of(map, struct xsk_map, map);
	struct xdp_sock *xs, *old_xs, **map_entry;
	u32 i = *(u32 *)key, fd = *(u32 *)value;
	struct xsk_map_node *node;
	struct socket *sock;
	int err;

	if (unlikely(map_flags > BPF_EXIST))
		return -EINVAL;
	if (unlikely(i >= m->map.max_entries))
		return -E2BIG;

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

	map_entry = &m->xsk_map[i];
	node = xsk_map_node_alloc(m, map_entry);
	if (IS_ERR(node)) {
		sockfd_put(sock);
		return PTR_ERR(node);
	}

	spin_lock_bh(&m->lock);
	old_xs = READ_ONCE(*map_entry);
	if (old_xs == xs) {
		err = 0;
		goto out;
	} else if (old_xs && map_flags == BPF_NOEXIST) {
		err = -EEXIST;
		goto out;
	} else if (!old_xs && map_flags == BPF_EXIST) {
		err = -ENOENT;
		goto out;
	}
	xsk_map_sock_add(xs, node);
	WRITE_ONCE(*map_entry, xs);
	if (old_xs)
		xsk_map_sock_delete(old_xs, map_entry);
	spin_unlock_bh(&m->lock);
	sockfd_put(sock);
	return 0;

out:
	spin_unlock_bh(&m->lock);
	sockfd_put(sock);
	xsk_map_node_free(node);
	return err;
}

static int xsk_map_delete_elem(struct bpf_map *map, void *key)
{
	struct xsk_map *m = container_of(map, struct xsk_map, map);
	struct xdp_sock *old_xs, **map_entry;
	int k = *(u32 *)key;

	if (k >= map->max_entries)
		return -EINVAL;

	spin_lock_bh(&m->lock);
	map_entry = &m->xsk_map[k];
	old_xs = xchg(map_entry, NULL);
	if (old_xs)
		xsk_map_sock_delete(old_xs, map_entry);
	spin_unlock_bh(&m->lock);

	return 0;
}

void xsk_map_try_sock_delete(struct xsk_map *map, struct xdp_sock *xs,
			     struct xdp_sock **map_entry)
{
	spin_lock_bh(&map->lock);
	if (READ_ONCE(*map_entry) == xs) {
		WRITE_ONCE(*map_entry, NULL);
		xsk_map_sock_delete(xs, map_entry);
	}
	spin_unlock_bh(&map->lock);
}

const struct bpf_map_ops xsk_map_ops = {
	.map_alloc = xsk_map_alloc,
	.map_free = xsk_map_free,
	.map_get_next_key = xsk_map_get_next_key,
	.map_lookup_elem = xsk_map_lookup_elem,
	.map_lookup_elem_sys_only = xsk_map_lookup_elem_sys_only,
	.map_update_elem = xsk_map_update_elem,
	.map_delete_elem = xsk_map_delete_elem,
	.map_check_btf = map_check_no_btf,
};
