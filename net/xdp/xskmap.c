// SPDX-License-Identifier: GPL-2.0
/* XSKMAP used for AF_XDP sockets
 * Copyright(c) 2018 Intel Corporation.
 */

#include <linux/bpf.h>
#include <linux/filter.h>
#include <net/xdp_sock.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/btf_ids.h>

#include "xsk.h"

static struct xsk_map_node *xsk_map_node_alloc(struct xsk_map *map,
					       struct xdp_sock __rcu **map_entry)
{
	struct xsk_map_node *node;

	node = bpf_map_kzalloc(&map->map, sizeof(*node),
			       GFP_ATOMIC | __GFP_NOWARN);
	if (!node)
		return ERR_PTR(-ENOMEM);

	bpf_map_inc(&map->map);
	atomic_inc(&map->count);

	node->map = map;
	node->map_entry = map_entry;
	return node;
}

static void xsk_map_node_free(struct xsk_map_node *node)
{
	struct xsk_map *map = node->map;

	bpf_map_put(&node->map->map);
	kfree(node);
	atomic_dec(&map->count);
}

static void xsk_map_sock_add(struct xdp_sock *xs, struct xsk_map_node *node)
{
	spin_lock_bh(&xs->map_list_lock);
	list_add_tail(&node->node, &xs->map_list);
	spin_unlock_bh(&xs->map_list_lock);
}

static void xsk_map_sock_delete(struct xdp_sock *xs,
				struct xdp_sock __rcu **map_entry)
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
	int numa_node;
	u64 size;

	if (attr->max_entries == 0 || attr->key_size != 4 ||
	    attr->value_size != 4 ||
	    attr->map_flags & ~(BPF_F_NUMA_NODE | BPF_F_RDONLY | BPF_F_WRONLY))
		return ERR_PTR(-EINVAL);

	numa_node = bpf_map_attr_numa_node(attr);
	size = struct_size(m, xsk_map, attr->max_entries);

	m = bpf_map_area_alloc(size, numa_node);
	if (!m)
		return ERR_PTR(-ENOMEM);

	bpf_map_init_from_attr(&m->map, attr);
	spin_lock_init(&m->lock);

	return &m->map;
}

static u64 xsk_map_mem_usage(const struct bpf_map *map)
{
	struct xsk_map *m = container_of(map, struct xsk_map, map);

	return struct_size(m, xsk_map, map->max_entries) +
		   (u64)atomic_read(&m->count) * sizeof(struct xsk_map_node);
}

static void xsk_map_free(struct bpf_map *map)
{
	struct xsk_map *m = container_of(map, struct xsk_map, map);

	synchronize_net();
	bpf_map_area_free(m);
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

static int xsk_map_gen_lookup(struct bpf_map *map, struct bpf_insn *insn_buf)
{
	const int ret = BPF_REG_0, mp = BPF_REG_1, index = BPF_REG_2;
	struct bpf_insn *insn = insn_buf;

	*insn++ = BPF_LDX_MEM(BPF_W, ret, index, 0);
	*insn++ = BPF_JMP_IMM(BPF_JGE, ret, map->max_entries, 5);
	*insn++ = BPF_ALU64_IMM(BPF_LSH, ret, ilog2(sizeof(struct xsk_sock *)));
	*insn++ = BPF_ALU64_IMM(BPF_ADD, mp, offsetof(struct xsk_map, xsk_map));
	*insn++ = BPF_ALU64_REG(BPF_ADD, ret, mp);
	*insn++ = BPF_LDX_MEM(BPF_SIZEOF(struct xsk_sock *), ret, ret, 0);
	*insn++ = BPF_JMP_IMM(BPF_JA, 0, 0, 1);
	*insn++ = BPF_MOV64_IMM(ret, 0);
	return insn - insn_buf;
}

/* Elements are kept alive by RCU; either by rcu_read_lock() (from syscall) or
 * by local_bh_disable() (from XDP calls inside NAPI). The
 * rcu_read_lock_bh_held() below makes lockdep accept both.
 */
static void *__xsk_map_lookup_elem(struct bpf_map *map, u32 key)
{
	struct xsk_map *m = container_of(map, struct xsk_map, map);

	if (key >= map->max_entries)
		return NULL;

	return rcu_dereference_check(m->xsk_map[key], rcu_read_lock_bh_held());
}

static void *xsk_map_lookup_elem(struct bpf_map *map, void *key)
{
	return __xsk_map_lookup_elem(map, *(u32 *)key);
}

static void *xsk_map_lookup_elem_sys_only(struct bpf_map *map, void *key)
{
	return ERR_PTR(-EOPNOTSUPP);
}

static long xsk_map_update_elem(struct bpf_map *map, void *key, void *value,
				u64 map_flags)
{
	struct xsk_map *m = container_of(map, struct xsk_map, map);
	struct xdp_sock __rcu **map_entry;
	struct xdp_sock *xs, *old_xs;
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

	map_entry = &m->xsk_map[i];
	node = xsk_map_node_alloc(m, map_entry);
	if (IS_ERR(node)) {
		sockfd_put(sock);
		return PTR_ERR(node);
	}

	spin_lock_bh(&m->lock);
	old_xs = rcu_dereference_protected(*map_entry, lockdep_is_held(&m->lock));
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
	rcu_assign_pointer(*map_entry, xs);
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

static long xsk_map_delete_elem(struct bpf_map *map, void *key)
{
	struct xsk_map *m = container_of(map, struct xsk_map, map);
	struct xdp_sock __rcu **map_entry;
	struct xdp_sock *old_xs;
	u32 k = *(u32 *)key;

	if (k >= map->max_entries)
		return -EINVAL;

	spin_lock_bh(&m->lock);
	map_entry = &m->xsk_map[k];
	old_xs = unrcu_pointer(xchg(map_entry, NULL));
	if (old_xs)
		xsk_map_sock_delete(old_xs, map_entry);
	spin_unlock_bh(&m->lock);

	return 0;
}

static long xsk_map_redirect(struct bpf_map *map, u64 index, u64 flags)
{
	return __bpf_xdp_redirect_map(map, index, flags, 0,
				      __xsk_map_lookup_elem);
}

void xsk_map_try_sock_delete(struct xsk_map *map, struct xdp_sock *xs,
			     struct xdp_sock __rcu **map_entry)
{
	spin_lock_bh(&map->lock);
	if (rcu_access_pointer(*map_entry) == xs) {
		rcu_assign_pointer(*map_entry, NULL);
		xsk_map_sock_delete(xs, map_entry);
	}
	spin_unlock_bh(&map->lock);
}

static bool xsk_map_meta_equal(const struct bpf_map *meta0,
			       const struct bpf_map *meta1)
{
	return meta0->max_entries == meta1->max_entries &&
		bpf_map_meta_equal(meta0, meta1);
}

BTF_ID_LIST_SINGLE(xsk_map_btf_ids, struct, xsk_map)
const struct bpf_map_ops xsk_map_ops = {
	.map_meta_equal = xsk_map_meta_equal,
	.map_alloc = xsk_map_alloc,
	.map_free = xsk_map_free,
	.map_get_next_key = xsk_map_get_next_key,
	.map_lookup_elem = xsk_map_lookup_elem,
	.map_gen_lookup = xsk_map_gen_lookup,
	.map_lookup_elem_sys_only = xsk_map_lookup_elem_sys_only,
	.map_update_elem = xsk_map_update_elem,
	.map_delete_elem = xsk_map_delete_elem,
	.map_check_btf = map_check_no_btf,
	.map_mem_usage = xsk_map_mem_usage,
	.map_btf_id = &xsk_map_btf_ids[0],
	.map_redirect = xsk_map_redirect,
};
