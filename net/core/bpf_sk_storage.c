// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019 Facebook  */
#include <linux/rculist.h>
#include <linux/list.h>
#include <linux/hash.h>
#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/bpf.h>
#include <linux/btf.h>
#include <linux/btf_ids.h>
#include <linux/bpf_local_storage.h>
#include <net/bpf_sk_storage.h>
#include <net/sock.h>
#include <uapi/linux/sock_diag.h>
#include <uapi/linux/btf.h>

DEFINE_BPF_STORAGE_CACHE(sk_cache);

static struct bpf_local_storage_data *
bpf_sk_storage_lookup(struct sock *sk, struct bpf_map *map, bool cacheit_lockit)
{
	struct bpf_local_storage *sk_storage;
	struct bpf_local_storage_map *smap;

	sk_storage = rcu_dereference(sk->sk_bpf_storage);
	if (!sk_storage)
		return NULL;

	smap = (struct bpf_local_storage_map *)map;
	return bpf_local_storage_lookup(sk_storage, smap, cacheit_lockit);
}

static int bpf_sk_storage_del(struct sock *sk, struct bpf_map *map)
{
	struct bpf_local_storage_data *sdata;

	sdata = bpf_sk_storage_lookup(sk, map, false);
	if (!sdata)
		return -ENOENT;

	bpf_selem_unlink(SELEM(sdata));

	return 0;
}

/* Called by __sk_destruct() & bpf_sk_storage_clone() */
void bpf_sk_storage_free(struct sock *sk)
{
	struct bpf_local_storage_elem *selem;
	struct bpf_local_storage *sk_storage;
	bool free_sk_storage = false;
	struct hlist_node *n;

	rcu_read_lock();
	sk_storage = rcu_dereference(sk->sk_bpf_storage);
	if (!sk_storage) {
		rcu_read_unlock();
		return;
	}

	/* Netiher the bpf_prog nor the bpf-map's syscall
	 * could be modifying the sk_storage->list now.
	 * Thus, no elem can be added-to or deleted-from the
	 * sk_storage->list by the bpf_prog or by the bpf-map's syscall.
	 *
	 * It is racing with bpf_local_storage_map_free() alone
	 * when unlinking elem from the sk_storage->list and
	 * the map's bucket->list.
	 */
	raw_spin_lock_bh(&sk_storage->lock);
	hlist_for_each_entry_safe(selem, n, &sk_storage->list, snode) {
		/* Always unlink from map before unlinking from
		 * sk_storage.
		 */
		bpf_selem_unlink_map(selem);
		free_sk_storage = bpf_selem_unlink_storage_nolock(sk_storage,
								  selem, true);
	}
	raw_spin_unlock_bh(&sk_storage->lock);
	rcu_read_unlock();

	if (free_sk_storage)
		kfree_rcu(sk_storage, rcu);
}

static void bpf_sk_storage_map_free(struct bpf_map *map)
{
	struct bpf_local_storage_map *smap;

	smap = (struct bpf_local_storage_map *)map;
	bpf_local_storage_cache_idx_free(&sk_cache, smap->cache_idx);
	bpf_local_storage_map_free(smap, NULL);
}

static struct bpf_map *bpf_sk_storage_map_alloc(union bpf_attr *attr)
{
	struct bpf_local_storage_map *smap;

	smap = bpf_local_storage_map_alloc(attr);
	if (IS_ERR(smap))
		return ERR_CAST(smap);

	smap->cache_idx = bpf_local_storage_cache_idx_get(&sk_cache);
	return &smap->map;
}

static int notsupp_get_next_key(struct bpf_map *map, void *key,
				void *next_key)
{
	return -ENOTSUPP;
}

static void *bpf_fd_sk_storage_lookup_elem(struct bpf_map *map, void *key)
{
	struct bpf_local_storage_data *sdata;
	struct socket *sock;
	int fd, err;

	fd = *(int *)key;
	sock = sockfd_lookup(fd, &err);
	if (sock) {
		sdata = bpf_sk_storage_lookup(sock->sk, map, true);
		sockfd_put(sock);
		return sdata ? sdata->data : NULL;
	}

	return ERR_PTR(err);
}

static int bpf_fd_sk_storage_update_elem(struct bpf_map *map, void *key,
					 void *value, u64 map_flags)
{
	struct bpf_local_storage_data *sdata;
	struct socket *sock;
	int fd, err;

	fd = *(int *)key;
	sock = sockfd_lookup(fd, &err);
	if (sock) {
		sdata = bpf_local_storage_update(
			sock->sk, (struct bpf_local_storage_map *)map, value,
			map_flags);
		sockfd_put(sock);
		return PTR_ERR_OR_ZERO(sdata);
	}

	return err;
}

static int bpf_fd_sk_storage_delete_elem(struct bpf_map *map, void *key)
{
	struct socket *sock;
	int fd, err;

	fd = *(int *)key;
	sock = sockfd_lookup(fd, &err);
	if (sock) {
		err = bpf_sk_storage_del(sock->sk, map);
		sockfd_put(sock);
		return err;
	}

	return err;
}

static struct bpf_local_storage_elem *
bpf_sk_storage_clone_elem(struct sock *newsk,
			  struct bpf_local_storage_map *smap,
			  struct bpf_local_storage_elem *selem)
{
	struct bpf_local_storage_elem *copy_selem;

	copy_selem = bpf_selem_alloc(smap, newsk, NULL, true);
	if (!copy_selem)
		return NULL;

	if (map_value_has_spin_lock(&smap->map))
		copy_map_value_locked(&smap->map, SDATA(copy_selem)->data,
				      SDATA(selem)->data, true);
	else
		copy_map_value(&smap->map, SDATA(copy_selem)->data,
			       SDATA(selem)->data);

	return copy_selem;
}

int bpf_sk_storage_clone(const struct sock *sk, struct sock *newsk)
{
	struct bpf_local_storage *new_sk_storage = NULL;
	struct bpf_local_storage *sk_storage;
	struct bpf_local_storage_elem *selem;
	int ret = 0;

	RCU_INIT_POINTER(newsk->sk_bpf_storage, NULL);

	rcu_read_lock();
	sk_storage = rcu_dereference(sk->sk_bpf_storage);

	if (!sk_storage || hlist_empty(&sk_storage->list))
		goto out;

	hlist_for_each_entry_rcu(selem, &sk_storage->list, snode) {
		struct bpf_local_storage_elem *copy_selem;
		struct bpf_local_storage_map *smap;
		struct bpf_map *map;

		smap = rcu_dereference(SDATA(selem)->smap);
		if (!(smap->map.map_flags & BPF_F_CLONE))
			continue;

		/* Note that for lockless listeners adding new element
		 * here can race with cleanup in bpf_local_storage_map_free.
		 * Try to grab map refcnt to make sure that it's still
		 * alive and prevent concurrent removal.
		 */
		map = bpf_map_inc_not_zero(&smap->map);
		if (IS_ERR(map))
			continue;

		copy_selem = bpf_sk_storage_clone_elem(newsk, smap, selem);
		if (!copy_selem) {
			ret = -ENOMEM;
			bpf_map_put(map);
			goto out;
		}

		if (new_sk_storage) {
			bpf_selem_link_map(smap, copy_selem);
			bpf_selem_link_storage_nolock(new_sk_storage, copy_selem);
		} else {
			ret = bpf_local_storage_alloc(newsk, smap, copy_selem);
			if (ret) {
				kfree(copy_selem);
				atomic_sub(smap->elem_size,
					   &newsk->sk_omem_alloc);
				bpf_map_put(map);
				goto out;
			}

			new_sk_storage =
				rcu_dereference(copy_selem->local_storage);
		}
		bpf_map_put(map);
	}

out:
	rcu_read_unlock();

	/* In case of an error, don't free anything explicitly here, the
	 * caller is responsible to call bpf_sk_storage_free.
	 */

	return ret;
}

BPF_CALL_4(bpf_sk_storage_get, struct bpf_map *, map, struct sock *, sk,
	   void *, value, u64, flags)
{
	struct bpf_local_storage_data *sdata;

	if (!sk || !sk_fullsock(sk) || flags > BPF_SK_STORAGE_GET_F_CREATE)
		return (unsigned long)NULL;

	sdata = bpf_sk_storage_lookup(sk, map, true);
	if (sdata)
		return (unsigned long)sdata->data;

	if (flags == BPF_SK_STORAGE_GET_F_CREATE &&
	    /* Cannot add new elem to a going away sk.
	     * Otherwise, the new elem may become a leak
	     * (and also other memory issues during map
	     *  destruction).
	     */
	    refcount_inc_not_zero(&sk->sk_refcnt)) {
		sdata = bpf_local_storage_update(
			sk, (struct bpf_local_storage_map *)map, value,
			BPF_NOEXIST);
		/* sk must be a fullsock (guaranteed by verifier),
		 * so sock_gen_put() is unnecessary.
		 */
		sock_put(sk);
		return IS_ERR(sdata) ?
			(unsigned long)NULL : (unsigned long)sdata->data;
	}

	return (unsigned long)NULL;
}

BPF_CALL_2(bpf_sk_storage_delete, struct bpf_map *, map, struct sock *, sk)
{
	if (!sk || !sk_fullsock(sk))
		return -EINVAL;

	if (refcount_inc_not_zero(&sk->sk_refcnt)) {
		int err;

		err = bpf_sk_storage_del(sk, map);
		sock_put(sk);
		return err;
	}

	return -ENOENT;
}

static int bpf_sk_storage_charge(struct bpf_local_storage_map *smap,
				 void *owner, u32 size)
{
	struct sock *sk = (struct sock *)owner;

	/* same check as in sock_kmalloc() */
	if (size <= sysctl_optmem_max &&
	    atomic_read(&sk->sk_omem_alloc) + size < sysctl_optmem_max) {
		atomic_add(size, &sk->sk_omem_alloc);
		return 0;
	}

	return -ENOMEM;
}

static void bpf_sk_storage_uncharge(struct bpf_local_storage_map *smap,
				    void *owner, u32 size)
{
	struct sock *sk = owner;

	atomic_sub(size, &sk->sk_omem_alloc);
}

static struct bpf_local_storage __rcu **
bpf_sk_storage_ptr(void *owner)
{
	struct sock *sk = owner;

	return &sk->sk_bpf_storage;
}

static int sk_storage_map_btf_id;
const struct bpf_map_ops sk_storage_map_ops = {
	.map_meta_equal = bpf_map_meta_equal,
	.map_alloc_check = bpf_local_storage_map_alloc_check,
	.map_alloc = bpf_sk_storage_map_alloc,
	.map_free = bpf_sk_storage_map_free,
	.map_get_next_key = notsupp_get_next_key,
	.map_lookup_elem = bpf_fd_sk_storage_lookup_elem,
	.map_update_elem = bpf_fd_sk_storage_update_elem,
	.map_delete_elem = bpf_fd_sk_storage_delete_elem,
	.map_check_btf = bpf_local_storage_map_check_btf,
	.map_btf_name = "bpf_local_storage_map",
	.map_btf_id = &sk_storage_map_btf_id,
	.map_local_storage_charge = bpf_sk_storage_charge,
	.map_local_storage_uncharge = bpf_sk_storage_uncharge,
	.map_owner_storage_ptr = bpf_sk_storage_ptr,
};

const struct bpf_func_proto bpf_sk_storage_get_proto = {
	.func		= bpf_sk_storage_get,
	.gpl_only	= false,
	.ret_type	= RET_PTR_TO_MAP_VALUE_OR_NULL,
	.arg1_type	= ARG_CONST_MAP_PTR,
	.arg2_type	= ARG_PTR_TO_BTF_ID_SOCK_COMMON,
	.arg3_type	= ARG_PTR_TO_MAP_VALUE_OR_NULL,
	.arg4_type	= ARG_ANYTHING,
};

const struct bpf_func_proto bpf_sk_storage_get_cg_sock_proto = {
	.func		= bpf_sk_storage_get,
	.gpl_only	= false,
	.ret_type	= RET_PTR_TO_MAP_VALUE_OR_NULL,
	.arg1_type	= ARG_CONST_MAP_PTR,
	.arg2_type	= ARG_PTR_TO_CTX, /* context is 'struct sock' */
	.arg3_type	= ARG_PTR_TO_MAP_VALUE_OR_NULL,
	.arg4_type	= ARG_ANYTHING,
};

const struct bpf_func_proto bpf_sk_storage_delete_proto = {
	.func		= bpf_sk_storage_delete,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_CONST_MAP_PTR,
	.arg2_type	= ARG_PTR_TO_BTF_ID_SOCK_COMMON,
};

static bool bpf_sk_storage_tracing_allowed(const struct bpf_prog *prog)
{
	const struct btf *btf_vmlinux;
	const struct btf_type *t;
	const char *tname;
	u32 btf_id;

	if (prog->aux->dst_prog)
		return false;

	/* Ensure the tracing program is not tracing
	 * any bpf_sk_storage*() function and also
	 * use the bpf_sk_storage_(get|delete) helper.
	 */
	switch (prog->expected_attach_type) {
	case BPF_TRACE_ITER:
	case BPF_TRACE_RAW_TP:
		/* bpf_sk_storage has no trace point */
		return true;
	case BPF_TRACE_FENTRY:
	case BPF_TRACE_FEXIT:
		btf_vmlinux = bpf_get_btf_vmlinux();
		btf_id = prog->aux->attach_btf_id;
		t = btf_type_by_id(btf_vmlinux, btf_id);
		tname = btf_name_by_offset(btf_vmlinux, t->name_off);
		return !!strncmp(tname, "bpf_sk_storage",
				 strlen("bpf_sk_storage"));
	default:
		return false;
	}

	return false;
}

BPF_CALL_4(bpf_sk_storage_get_tracing, struct bpf_map *, map, struct sock *, sk,
	   void *, value, u64, flags)
{
	if (in_hardirq() || in_nmi())
		return (unsigned long)NULL;

	return (unsigned long)____bpf_sk_storage_get(map, sk, value, flags);
}

BPF_CALL_2(bpf_sk_storage_delete_tracing, struct bpf_map *, map,
	   struct sock *, sk)
{
	if (in_hardirq() || in_nmi())
		return -EPERM;

	return ____bpf_sk_storage_delete(map, sk);
}

const struct bpf_func_proto bpf_sk_storage_get_tracing_proto = {
	.func		= bpf_sk_storage_get_tracing,
	.gpl_only	= false,
	.ret_type	= RET_PTR_TO_MAP_VALUE_OR_NULL,
	.arg1_type	= ARG_CONST_MAP_PTR,
	.arg2_type	= ARG_PTR_TO_BTF_ID,
	.arg2_btf_id	= &btf_sock_ids[BTF_SOCK_TYPE_SOCK_COMMON],
	.arg3_type	= ARG_PTR_TO_MAP_VALUE_OR_NULL,
	.arg4_type	= ARG_ANYTHING,
	.allowed	= bpf_sk_storage_tracing_allowed,
};

const struct bpf_func_proto bpf_sk_storage_delete_tracing_proto = {
	.func		= bpf_sk_storage_delete_tracing,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_CONST_MAP_PTR,
	.arg2_type	= ARG_PTR_TO_BTF_ID,
	.arg2_btf_id	= &btf_sock_ids[BTF_SOCK_TYPE_SOCK_COMMON],
	.allowed	= bpf_sk_storage_tracing_allowed,
};

struct bpf_sk_storage_diag {
	u32 nr_maps;
	struct bpf_map *maps[];
};

/* The reply will be like:
 * INET_DIAG_BPF_SK_STORAGES (nla_nest)
 *	SK_DIAG_BPF_STORAGE (nla_nest)
 *		SK_DIAG_BPF_STORAGE_MAP_ID (nla_put_u32)
 *		SK_DIAG_BPF_STORAGE_MAP_VALUE (nla_reserve_64bit)
 *	SK_DIAG_BPF_STORAGE (nla_nest)
 *		SK_DIAG_BPF_STORAGE_MAP_ID (nla_put_u32)
 *		SK_DIAG_BPF_STORAGE_MAP_VALUE (nla_reserve_64bit)
 *	....
 */
static int nla_value_size(u32 value_size)
{
	/* SK_DIAG_BPF_STORAGE (nla_nest)
	 *	SK_DIAG_BPF_STORAGE_MAP_ID (nla_put_u32)
	 *	SK_DIAG_BPF_STORAGE_MAP_VALUE (nla_reserve_64bit)
	 */
	return nla_total_size(0) + nla_total_size(sizeof(u32)) +
		nla_total_size_64bit(value_size);
}

void bpf_sk_storage_diag_free(struct bpf_sk_storage_diag *diag)
{
	u32 i;

	if (!diag)
		return;

	for (i = 0; i < diag->nr_maps; i++)
		bpf_map_put(diag->maps[i]);

	kfree(diag);
}
EXPORT_SYMBOL_GPL(bpf_sk_storage_diag_free);

static bool diag_check_dup(const struct bpf_sk_storage_diag *diag,
			   const struct bpf_map *map)
{
	u32 i;

	for (i = 0; i < diag->nr_maps; i++) {
		if (diag->maps[i] == map)
			return true;
	}

	return false;
}

struct bpf_sk_storage_diag *
bpf_sk_storage_diag_alloc(const struct nlattr *nla_stgs)
{
	struct bpf_sk_storage_diag *diag;
	struct nlattr *nla;
	u32 nr_maps = 0;
	int rem, err;

	/* bpf_local_storage_map is currently limited to CAP_SYS_ADMIN as
	 * the map_alloc_check() side also does.
	 */
	if (!bpf_capable())
		return ERR_PTR(-EPERM);

	nla_for_each_nested(nla, nla_stgs, rem) {
		if (nla_type(nla) == SK_DIAG_BPF_STORAGE_REQ_MAP_FD)
			nr_maps++;
	}

	diag = kzalloc(struct_size(diag, maps, nr_maps), GFP_KERNEL);
	if (!diag)
		return ERR_PTR(-ENOMEM);

	nla_for_each_nested(nla, nla_stgs, rem) {
		struct bpf_map *map;
		int map_fd;

		if (nla_type(nla) != SK_DIAG_BPF_STORAGE_REQ_MAP_FD)
			continue;

		map_fd = nla_get_u32(nla);
		map = bpf_map_get(map_fd);
		if (IS_ERR(map)) {
			err = PTR_ERR(map);
			goto err_free;
		}
		if (map->map_type != BPF_MAP_TYPE_SK_STORAGE) {
			bpf_map_put(map);
			err = -EINVAL;
			goto err_free;
		}
		if (diag_check_dup(diag, map)) {
			bpf_map_put(map);
			err = -EEXIST;
			goto err_free;
		}
		diag->maps[diag->nr_maps++] = map;
	}

	return diag;

err_free:
	bpf_sk_storage_diag_free(diag);
	return ERR_PTR(err);
}
EXPORT_SYMBOL_GPL(bpf_sk_storage_diag_alloc);

static int diag_get(struct bpf_local_storage_data *sdata, struct sk_buff *skb)
{
	struct nlattr *nla_stg, *nla_value;
	struct bpf_local_storage_map *smap;

	/* It cannot exceed max nlattr's payload */
	BUILD_BUG_ON(U16_MAX - NLA_HDRLEN < BPF_LOCAL_STORAGE_MAX_VALUE_SIZE);

	nla_stg = nla_nest_start(skb, SK_DIAG_BPF_STORAGE);
	if (!nla_stg)
		return -EMSGSIZE;

	smap = rcu_dereference(sdata->smap);
	if (nla_put_u32(skb, SK_DIAG_BPF_STORAGE_MAP_ID, smap->map.id))
		goto errout;

	nla_value = nla_reserve_64bit(skb, SK_DIAG_BPF_STORAGE_MAP_VALUE,
				      smap->map.value_size,
				      SK_DIAG_BPF_STORAGE_PAD);
	if (!nla_value)
		goto errout;

	if (map_value_has_spin_lock(&smap->map))
		copy_map_value_locked(&smap->map, nla_data(nla_value),
				      sdata->data, true);
	else
		copy_map_value(&smap->map, nla_data(nla_value), sdata->data);

	nla_nest_end(skb, nla_stg);
	return 0;

errout:
	nla_nest_cancel(skb, nla_stg);
	return -EMSGSIZE;
}

static int bpf_sk_storage_diag_put_all(struct sock *sk, struct sk_buff *skb,
				       int stg_array_type,
				       unsigned int *res_diag_size)
{
	/* stg_array_type (e.g. INET_DIAG_BPF_SK_STORAGES) */
	unsigned int diag_size = nla_total_size(0);
	struct bpf_local_storage *sk_storage;
	struct bpf_local_storage_elem *selem;
	struct bpf_local_storage_map *smap;
	struct nlattr *nla_stgs;
	unsigned int saved_len;
	int err = 0;

	rcu_read_lock();

	sk_storage = rcu_dereference(sk->sk_bpf_storage);
	if (!sk_storage || hlist_empty(&sk_storage->list)) {
		rcu_read_unlock();
		return 0;
	}

	nla_stgs = nla_nest_start(skb, stg_array_type);
	if (!nla_stgs)
		/* Continue to learn diag_size */
		err = -EMSGSIZE;

	saved_len = skb->len;
	hlist_for_each_entry_rcu(selem, &sk_storage->list, snode) {
		smap = rcu_dereference(SDATA(selem)->smap);
		diag_size += nla_value_size(smap->map.value_size);

		if (nla_stgs && diag_get(SDATA(selem), skb))
			/* Continue to learn diag_size */
			err = -EMSGSIZE;
	}

	rcu_read_unlock();

	if (nla_stgs) {
		if (saved_len == skb->len)
			nla_nest_cancel(skb, nla_stgs);
		else
			nla_nest_end(skb, nla_stgs);
	}

	if (diag_size == nla_total_size(0)) {
		*res_diag_size = 0;
		return 0;
	}

	*res_diag_size = diag_size;
	return err;
}

int bpf_sk_storage_diag_put(struct bpf_sk_storage_diag *diag,
			    struct sock *sk, struct sk_buff *skb,
			    int stg_array_type,
			    unsigned int *res_diag_size)
{
	/* stg_array_type (e.g. INET_DIAG_BPF_SK_STORAGES) */
	unsigned int diag_size = nla_total_size(0);
	struct bpf_local_storage *sk_storage;
	struct bpf_local_storage_data *sdata;
	struct nlattr *nla_stgs;
	unsigned int saved_len;
	int err = 0;
	u32 i;

	*res_diag_size = 0;

	/* No map has been specified.  Dump all. */
	if (!diag->nr_maps)
		return bpf_sk_storage_diag_put_all(sk, skb, stg_array_type,
						   res_diag_size);

	rcu_read_lock();
	sk_storage = rcu_dereference(sk->sk_bpf_storage);
	if (!sk_storage || hlist_empty(&sk_storage->list)) {
		rcu_read_unlock();
		return 0;
	}

	nla_stgs = nla_nest_start(skb, stg_array_type);
	if (!nla_stgs)
		/* Continue to learn diag_size */
		err = -EMSGSIZE;

	saved_len = skb->len;
	for (i = 0; i < diag->nr_maps; i++) {
		sdata = bpf_local_storage_lookup(sk_storage,
				(struct bpf_local_storage_map *)diag->maps[i],
				false);

		if (!sdata)
			continue;

		diag_size += nla_value_size(diag->maps[i]->value_size);

		if (nla_stgs && diag_get(sdata, skb))
			/* Continue to learn diag_size */
			err = -EMSGSIZE;
	}
	rcu_read_unlock();

	if (nla_stgs) {
		if (saved_len == skb->len)
			nla_nest_cancel(skb, nla_stgs);
		else
			nla_nest_end(skb, nla_stgs);
	}

	if (diag_size == nla_total_size(0)) {
		*res_diag_size = 0;
		return 0;
	}

	*res_diag_size = diag_size;
	return err;
}
EXPORT_SYMBOL_GPL(bpf_sk_storage_diag_put);

struct bpf_iter_seq_sk_storage_map_info {
	struct bpf_map *map;
	unsigned int bucket_id;
	unsigned skip_elems;
};

static struct bpf_local_storage_elem *
bpf_sk_storage_map_seq_find_next(struct bpf_iter_seq_sk_storage_map_info *info,
				 struct bpf_local_storage_elem *prev_selem)
	__acquires(RCU) __releases(RCU)
{
	struct bpf_local_storage *sk_storage;
	struct bpf_local_storage_elem *selem;
	u32 skip_elems = info->skip_elems;
	struct bpf_local_storage_map *smap;
	u32 bucket_id = info->bucket_id;
	u32 i, count, n_buckets;
	struct bpf_local_storage_map_bucket *b;

	smap = (struct bpf_local_storage_map *)info->map;
	n_buckets = 1U << smap->bucket_log;
	if (bucket_id >= n_buckets)
		return NULL;

	/* try to find next selem in the same bucket */
	selem = prev_selem;
	count = 0;
	while (selem) {
		selem = hlist_entry_safe(rcu_dereference(hlist_next_rcu(&selem->map_node)),
					 struct bpf_local_storage_elem, map_node);
		if (!selem) {
			/* not found, unlock and go to the next bucket */
			b = &smap->buckets[bucket_id++];
			rcu_read_unlock();
			skip_elems = 0;
			break;
		}
		sk_storage = rcu_dereference(selem->local_storage);
		if (sk_storage) {
			info->skip_elems = skip_elems + count;
			return selem;
		}
		count++;
	}

	for (i = bucket_id; i < (1U << smap->bucket_log); i++) {
		b = &smap->buckets[i];
		rcu_read_lock();
		count = 0;
		hlist_for_each_entry_rcu(selem, &b->list, map_node) {
			sk_storage = rcu_dereference(selem->local_storage);
			if (sk_storage && count >= skip_elems) {
				info->bucket_id = i;
				info->skip_elems = count;
				return selem;
			}
			count++;
		}
		rcu_read_unlock();
		skip_elems = 0;
	}

	info->bucket_id = i;
	info->skip_elems = 0;
	return NULL;
}

static void *bpf_sk_storage_map_seq_start(struct seq_file *seq, loff_t *pos)
{
	struct bpf_local_storage_elem *selem;

	selem = bpf_sk_storage_map_seq_find_next(seq->private, NULL);
	if (!selem)
		return NULL;

	if (*pos == 0)
		++*pos;
	return selem;
}

static void *bpf_sk_storage_map_seq_next(struct seq_file *seq, void *v,
					 loff_t *pos)
{
	struct bpf_iter_seq_sk_storage_map_info *info = seq->private;

	++*pos;
	++info->skip_elems;
	return bpf_sk_storage_map_seq_find_next(seq->private, v);
}

struct bpf_iter__bpf_sk_storage_map {
	__bpf_md_ptr(struct bpf_iter_meta *, meta);
	__bpf_md_ptr(struct bpf_map *, map);
	__bpf_md_ptr(struct sock *, sk);
	__bpf_md_ptr(void *, value);
};

DEFINE_BPF_ITER_FUNC(bpf_sk_storage_map, struct bpf_iter_meta *meta,
		     struct bpf_map *map, struct sock *sk,
		     void *value)

static int __bpf_sk_storage_map_seq_show(struct seq_file *seq,
					 struct bpf_local_storage_elem *selem)
{
	struct bpf_iter_seq_sk_storage_map_info *info = seq->private;
	struct bpf_iter__bpf_sk_storage_map ctx = {};
	struct bpf_local_storage *sk_storage;
	struct bpf_iter_meta meta;
	struct bpf_prog *prog;
	int ret = 0;

	meta.seq = seq;
	prog = bpf_iter_get_info(&meta, selem == NULL);
	if (prog) {
		ctx.meta = &meta;
		ctx.map = info->map;
		if (selem) {
			sk_storage = rcu_dereference(selem->local_storage);
			ctx.sk = sk_storage->owner;
			ctx.value = SDATA(selem)->data;
		}
		ret = bpf_iter_run_prog(prog, &ctx);
	}

	return ret;
}

static int bpf_sk_storage_map_seq_show(struct seq_file *seq, void *v)
{
	return __bpf_sk_storage_map_seq_show(seq, v);
}

static void bpf_sk_storage_map_seq_stop(struct seq_file *seq, void *v)
	__releases(RCU)
{
	if (!v)
		(void)__bpf_sk_storage_map_seq_show(seq, v);
	else
		rcu_read_unlock();
}

static int bpf_iter_init_sk_storage_map(void *priv_data,
					struct bpf_iter_aux_info *aux)
{
	struct bpf_iter_seq_sk_storage_map_info *seq_info = priv_data;

	seq_info->map = aux->map;
	return 0;
}

static int bpf_iter_attach_map(struct bpf_prog *prog,
			       union bpf_iter_link_info *linfo,
			       struct bpf_iter_aux_info *aux)
{
	struct bpf_map *map;
	int err = -EINVAL;

	if (!linfo->map.map_fd)
		return -EBADF;

	map = bpf_map_get_with_uref(linfo->map.map_fd);
	if (IS_ERR(map))
		return PTR_ERR(map);

	if (map->map_type != BPF_MAP_TYPE_SK_STORAGE)
		goto put_map;

	if (prog->aux->max_rdonly_access > map->value_size) {
		err = -EACCES;
		goto put_map;
	}

	aux->map = map;
	return 0;

put_map:
	bpf_map_put_with_uref(map);
	return err;
}

static void bpf_iter_detach_map(struct bpf_iter_aux_info *aux)
{
	bpf_map_put_with_uref(aux->map);
}

static const struct seq_operations bpf_sk_storage_map_seq_ops = {
	.start  = bpf_sk_storage_map_seq_start,
	.next   = bpf_sk_storage_map_seq_next,
	.stop   = bpf_sk_storage_map_seq_stop,
	.show   = bpf_sk_storage_map_seq_show,
};

static const struct bpf_iter_seq_info iter_seq_info = {
	.seq_ops		= &bpf_sk_storage_map_seq_ops,
	.init_seq_private	= bpf_iter_init_sk_storage_map,
	.fini_seq_private	= NULL,
	.seq_priv_size		= sizeof(struct bpf_iter_seq_sk_storage_map_info),
};

static struct bpf_iter_reg bpf_sk_storage_map_reg_info = {
	.target			= "bpf_sk_storage_map",
	.attach_target		= bpf_iter_attach_map,
	.detach_target		= bpf_iter_detach_map,
	.show_fdinfo		= bpf_iter_map_show_fdinfo,
	.fill_link_info		= bpf_iter_map_fill_link_info,
	.ctx_arg_info_size	= 2,
	.ctx_arg_info		= {
		{ offsetof(struct bpf_iter__bpf_sk_storage_map, sk),
		  PTR_TO_BTF_ID_OR_NULL },
		{ offsetof(struct bpf_iter__bpf_sk_storage_map, value),
		  PTR_TO_BUF | PTR_MAYBE_NULL },
	},
	.seq_info		= &iter_seq_info,
};

static int __init bpf_sk_storage_map_iter_init(void)
{
	bpf_sk_storage_map_reg_info.ctx_arg_info[0].btf_id =
		btf_sock_ids[BTF_SOCK_TYPE_SOCK];
	return bpf_iter_reg_target(&bpf_sk_storage_map_reg_info);
}
late_initcall(bpf_sk_storage_map_iter_init);
