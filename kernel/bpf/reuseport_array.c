// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018 Facebook
 */
#include <linux/bpf.h>
#include <linux/err.h>
#include <linux/sock_diag.h>
#include <net/sock_reuseport.h>

struct reuseport_array {
	struct bpf_map map;
	struct sock __rcu *ptrs[];
};

static struct reuseport_array *reuseport_array(struct bpf_map *map)
{
	return (struct reuseport_array *)map;
}

/* The caller must hold the reuseport_lock */
void bpf_sk_reuseport_detach(struct sock *sk)
{
	uintptr_t sk_user_data;

	write_lock_bh(&sk->sk_callback_lock);
	sk_user_data = (uintptr_t)sk->sk_user_data;
	if (sk_user_data & SK_USER_DATA_BPF) {
		struct sock __rcu **socks;

		socks = (void *)(sk_user_data & SK_USER_DATA_PTRMASK);
		WRITE_ONCE(sk->sk_user_data, NULL);
		/*
		 * Do not move this NULL assignment outside of
		 * sk->sk_callback_lock because there is
		 * a race with reuseport_array_free()
		 * which does not hold the reuseport_lock.
		 */
		RCU_INIT_POINTER(*socks, NULL);
	}
	write_unlock_bh(&sk->sk_callback_lock);
}

static int reuseport_array_alloc_check(union bpf_attr *attr)
{
	if (attr->value_size != sizeof(u32) &&
	    attr->value_size != sizeof(u64))
		return -EINVAL;

	return array_map_alloc_check(attr);
}

static void *reuseport_array_lookup_elem(struct bpf_map *map, void *key)
{
	struct reuseport_array *array = reuseport_array(map);
	u32 index = *(u32 *)key;

	if (unlikely(index >= array->map.max_entries))
		return NULL;

	return rcu_dereference(array->ptrs[index]);
}

/* Called from syscall only */
static int reuseport_array_delete_elem(struct bpf_map *map, void *key)
{
	struct reuseport_array *array = reuseport_array(map);
	u32 index = *(u32 *)key;
	struct sock *sk;
	int err;

	if (index >= map->max_entries)
		return -E2BIG;

	if (!rcu_access_pointer(array->ptrs[index]))
		return -ENOENT;

	spin_lock_bh(&reuseport_lock);

	sk = rcu_dereference_protected(array->ptrs[index],
				       lockdep_is_held(&reuseport_lock));
	if (sk) {
		write_lock_bh(&sk->sk_callback_lock);
		WRITE_ONCE(sk->sk_user_data, NULL);
		RCU_INIT_POINTER(array->ptrs[index], NULL);
		write_unlock_bh(&sk->sk_callback_lock);
		err = 0;
	} else {
		err = -ENOENT;
	}

	spin_unlock_bh(&reuseport_lock);

	return err;
}

static void reuseport_array_free(struct bpf_map *map)
{
	struct reuseport_array *array = reuseport_array(map);
	struct sock *sk;
	u32 i;

	/*
	 * ops->map_*_elem() will not be able to access this
	 * array now. Hence, this function only races with
	 * bpf_sk_reuseport_detach() which was triggered by
	 * close() or disconnect().
	 *
	 * This function and bpf_sk_reuseport_detach() are
	 * both removing sk from "array".  Who removes it
	 * first does not matter.
	 *
	 * The only concern here is bpf_sk_reuseport_detach()
	 * may access "array" which is being freed here.
	 * bpf_sk_reuseport_detach() access this "array"
	 * through sk->sk_user_data _and_ with sk->sk_callback_lock
	 * held which is enough because this "array" is not freed
	 * until all sk->sk_user_data has stopped referencing this "array".
	 *
	 * Hence, due to the above, taking "reuseport_lock" is not
	 * needed here.
	 */

	/*
	 * Since reuseport_lock is not taken, sk is accessed under
	 * rcu_read_lock()
	 */
	rcu_read_lock();
	for (i = 0; i < map->max_entries; i++) {
		sk = rcu_dereference(array->ptrs[i]);
		if (sk) {
			write_lock_bh(&sk->sk_callback_lock);
			/*
			 * No need for WRITE_ONCE(). At this point,
			 * no one is reading it without taking the
			 * sk->sk_callback_lock.
			 */
			sk->sk_user_data = NULL;
			write_unlock_bh(&sk->sk_callback_lock);
			RCU_INIT_POINTER(array->ptrs[i], NULL);
		}
	}
	rcu_read_unlock();

	/*
	 * Once reaching here, all sk->sk_user_data is not
	 * referencing this "array". "array" can be freed now.
	 */
	bpf_map_area_free(array);
}

static struct bpf_map *reuseport_array_alloc(union bpf_attr *attr)
{
	int numa_node = bpf_map_attr_numa_node(attr);
	struct reuseport_array *array;

	if (!bpf_capable())
		return ERR_PTR(-EPERM);

	/* allocate all map elements and zero-initialize them */
	array = bpf_map_area_alloc(struct_size(array, ptrs, attr->max_entries), numa_node);
	if (!array)
		return ERR_PTR(-ENOMEM);

	/* copy mandatory map attributes */
	bpf_map_init_from_attr(&array->map, attr);

	return &array->map;
}

int bpf_fd_reuseport_array_lookup_elem(struct bpf_map *map, void *key,
				       void *value)
{
	struct sock *sk;
	int err;

	if (map->value_size != sizeof(u64))
		return -ENOSPC;

	rcu_read_lock();
	sk = reuseport_array_lookup_elem(map, key);
	if (sk) {
		*(u64 *)value = __sock_gen_cookie(sk);
		err = 0;
	} else {
		err = -ENOENT;
	}
	rcu_read_unlock();

	return err;
}

static int
reuseport_array_update_check(const struct reuseport_array *array,
			     const struct sock *nsk,
			     const struct sock *osk,
			     const struct sock_reuseport *nsk_reuse,
			     u32 map_flags)
{
	if (osk && map_flags == BPF_NOEXIST)
		return -EEXIST;

	if (!osk && map_flags == BPF_EXIST)
		return -ENOENT;

	if (nsk->sk_protocol != IPPROTO_UDP && nsk->sk_protocol != IPPROTO_TCP)
		return -ENOTSUPP;

	if (nsk->sk_family != AF_INET && nsk->sk_family != AF_INET6)
		return -ENOTSUPP;

	if (nsk->sk_type != SOCK_STREAM && nsk->sk_type != SOCK_DGRAM)
		return -ENOTSUPP;

	/*
	 * sk must be hashed (i.e. listening in the TCP case or binded
	 * in the UDP case) and
	 * it must also be a SO_REUSEPORT sk (i.e. reuse cannot be NULL).
	 *
	 * Also, sk will be used in bpf helper that is protected by
	 * rcu_read_lock().
	 */
	if (!sock_flag(nsk, SOCK_RCU_FREE) || !sk_hashed(nsk) || !nsk_reuse)
		return -EINVAL;

	/* READ_ONCE because the sk->sk_callback_lock may not be held here */
	if (READ_ONCE(nsk->sk_user_data))
		return -EBUSY;

	return 0;
}

/*
 * Called from syscall only.
 * The "nsk" in the fd refcnt.
 * The "osk" and "reuse" are protected by reuseport_lock.
 */
int bpf_fd_reuseport_array_update_elem(struct bpf_map *map, void *key,
				       void *value, u64 map_flags)
{
	struct reuseport_array *array = reuseport_array(map);
	struct sock *free_osk = NULL, *osk, *nsk;
	struct sock_reuseport *reuse;
	u32 index = *(u32 *)key;
	uintptr_t sk_user_data;
	struct socket *socket;
	int err, fd;

	if (map_flags > BPF_EXIST)
		return -EINVAL;

	if (index >= map->max_entries)
		return -E2BIG;

	if (map->value_size == sizeof(u64)) {
		u64 fd64 = *(u64 *)value;

		if (fd64 > S32_MAX)
			return -EINVAL;
		fd = fd64;
	} else {
		fd = *(int *)value;
	}

	socket = sockfd_lookup(fd, &err);
	if (!socket)
		return err;

	nsk = socket->sk;
	if (!nsk) {
		err = -EINVAL;
		goto put_file;
	}

	/* Quick checks before taking reuseport_lock */
	err = reuseport_array_update_check(array, nsk,
					   rcu_access_pointer(array->ptrs[index]),
					   rcu_access_pointer(nsk->sk_reuseport_cb),
					   map_flags);
	if (err)
		goto put_file;

	spin_lock_bh(&reuseport_lock);
	/*
	 * Some of the checks only need reuseport_lock
	 * but it is done under sk_callback_lock also
	 * for simplicity reason.
	 */
	write_lock_bh(&nsk->sk_callback_lock);

	osk = rcu_dereference_protected(array->ptrs[index],
					lockdep_is_held(&reuseport_lock));
	reuse = rcu_dereference_protected(nsk->sk_reuseport_cb,
					  lockdep_is_held(&reuseport_lock));
	err = reuseport_array_update_check(array, nsk, osk, reuse, map_flags);
	if (err)
		goto put_file_unlock;

	sk_user_data = (uintptr_t)&array->ptrs[index] | SK_USER_DATA_NOCOPY |
		SK_USER_DATA_BPF;
	WRITE_ONCE(nsk->sk_user_data, (void *)sk_user_data);
	rcu_assign_pointer(array->ptrs[index], nsk);
	free_osk = osk;
	err = 0;

put_file_unlock:
	write_unlock_bh(&nsk->sk_callback_lock);

	if (free_osk) {
		write_lock_bh(&free_osk->sk_callback_lock);
		WRITE_ONCE(free_osk->sk_user_data, NULL);
		write_unlock_bh(&free_osk->sk_callback_lock);
	}

	spin_unlock_bh(&reuseport_lock);
put_file:
	fput(socket->file);
	return err;
}

/* Called from syscall */
static int reuseport_array_get_next_key(struct bpf_map *map, void *key,
					void *next_key)
{
	struct reuseport_array *array = reuseport_array(map);
	u32 index = key ? *(u32 *)key : U32_MAX;
	u32 *next = (u32 *)next_key;

	if (index >= array->map.max_entries) {
		*next = 0;
		return 0;
	}

	if (index == array->map.max_entries - 1)
		return -ENOENT;

	*next = index + 1;
	return 0;
}

static int reuseport_array_map_btf_id;
const struct bpf_map_ops reuseport_array_ops = {
	.map_meta_equal = bpf_map_meta_equal,
	.map_alloc_check = reuseport_array_alloc_check,
	.map_alloc = reuseport_array_alloc,
	.map_free = reuseport_array_free,
	.map_lookup_elem = reuseport_array_lookup_elem,
	.map_get_next_key = reuseport_array_get_next_key,
	.map_delete_elem = reuseport_array_delete_elem,
	.map_btf_name = "reuseport_array",
	.map_btf_id = &reuseport_array_map_btf_id,
};
