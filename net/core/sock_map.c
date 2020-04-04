// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2017 - 2018 Covalent IO, Inc. http://covalent.io */

#include <linux/bpf.h>
#include <linux/filter.h>
#include <linux/errno.h>
#include <linux/file.h>
#include <linux/net.h>
#include <linux/workqueue.h>
#include <linux/skmsg.h>
#include <linux/list.h>
#include <linux/jhash.h>

struct bpf_stab {
	struct bpf_map map;
	struct sock **sks;
	struct sk_psock_progs progs;
	raw_spinlock_t lock;
};

#define SOCK_CREATE_FLAG_MASK				\
	(BPF_F_NUMA_NODE | BPF_F_RDONLY | BPF_F_WRONLY)

static struct bpf_map *sock_map_alloc(union bpf_attr *attr)
{
	struct bpf_stab *stab;
	u64 cost;
	int err;

	if (!capable(CAP_NET_ADMIN))
		return ERR_PTR(-EPERM);
	if (attr->max_entries == 0 ||
	    attr->key_size    != 4 ||
	    attr->value_size  != 4 ||
	    attr->map_flags & ~SOCK_CREATE_FLAG_MASK)
		return ERR_PTR(-EINVAL);

	stab = kzalloc(sizeof(*stab), GFP_USER);
	if (!stab)
		return ERR_PTR(-ENOMEM);

	bpf_map_init_from_attr(&stab->map, attr);
	raw_spin_lock_init(&stab->lock);

	/* Make sure page count doesn't overflow. */
	cost = (u64) stab->map.max_entries * sizeof(struct sock *);
	err = bpf_map_charge_init(&stab->map.memory, cost);
	if (err)
		goto free_stab;

	stab->sks = bpf_map_area_alloc(stab->map.max_entries *
				       sizeof(struct sock *),
				       stab->map.numa_node);
	if (stab->sks)
		return &stab->map;
	err = -ENOMEM;
	bpf_map_charge_finish(&stab->map.memory);
free_stab:
	kfree(stab);
	return ERR_PTR(err);
}

int sock_map_get_from_fd(const union bpf_attr *attr, struct bpf_prog *prog)
{
	u32 ufd = attr->target_fd;
	struct bpf_map *map;
	struct fd f;
	int ret;

	f = fdget(ufd);
	map = __bpf_map_get(f);
	if (IS_ERR(map))
		return PTR_ERR(map);
	ret = sock_map_prog_update(map, prog, attr->attach_type);
	fdput(f);
	return ret;
}

static void sock_map_sk_acquire(struct sock *sk)
	__acquires(&sk->sk_lock.slock)
{
	lock_sock(sk);
	preempt_disable();
	rcu_read_lock();
}

static void sock_map_sk_release(struct sock *sk)
	__releases(&sk->sk_lock.slock)
{
	rcu_read_unlock();
	preempt_enable();
	release_sock(sk);
}

static void sock_map_add_link(struct sk_psock *psock,
			      struct sk_psock_link *link,
			      struct bpf_map *map, void *link_raw)
{
	link->link_raw = link_raw;
	link->map = map;
	spin_lock_bh(&psock->link_lock);
	list_add_tail(&link->list, &psock->link);
	spin_unlock_bh(&psock->link_lock);
}

static void sock_map_del_link(struct sock *sk,
			      struct sk_psock *psock, void *link_raw)
{
	struct sk_psock_link *link, *tmp;
	bool strp_stop = false;

	spin_lock_bh(&psock->link_lock);
	list_for_each_entry_safe(link, tmp, &psock->link, list) {
		if (link->link_raw == link_raw) {
			struct bpf_map *map = link->map;
			struct bpf_stab *stab = container_of(map, struct bpf_stab,
							     map);
			if (psock->parser.enabled && stab->progs.skb_parser)
				strp_stop = true;
			list_del(&link->list);
			sk_psock_free_link(link);
		}
	}
	spin_unlock_bh(&psock->link_lock);
	if (strp_stop) {
		write_lock_bh(&sk->sk_callback_lock);
		sk_psock_stop_strp(sk, psock);
		write_unlock_bh(&sk->sk_callback_lock);
	}
}

static void sock_map_unref(struct sock *sk, void *link_raw)
{
	struct sk_psock *psock = sk_psock(sk);

	if (likely(psock)) {
		sock_map_del_link(sk, psock, link_raw);
		sk_psock_put(sk, psock);
	}
}

static int sock_map_link(struct bpf_map *map, struct sk_psock_progs *progs,
			 struct sock *sk)
{
	struct bpf_prog *msg_parser, *skb_parser, *skb_verdict;
	bool skb_progs, sk_psock_is_new = false;
	struct sk_psock *psock;
	int ret;

	skb_verdict = READ_ONCE(progs->skb_verdict);
	skb_parser = READ_ONCE(progs->skb_parser);
	skb_progs = skb_parser && skb_verdict;
	if (skb_progs) {
		skb_verdict = bpf_prog_inc_not_zero(skb_verdict);
		if (IS_ERR(skb_verdict))
			return PTR_ERR(skb_verdict);
		skb_parser = bpf_prog_inc_not_zero(skb_parser);
		if (IS_ERR(skb_parser)) {
			bpf_prog_put(skb_verdict);
			return PTR_ERR(skb_parser);
		}
	}

	msg_parser = READ_ONCE(progs->msg_parser);
	if (msg_parser) {
		msg_parser = bpf_prog_inc_not_zero(msg_parser);
		if (IS_ERR(msg_parser)) {
			ret = PTR_ERR(msg_parser);
			goto out;
		}
	}

	psock = sk_psock_get_checked(sk);
	if (IS_ERR(psock)) {
		ret = PTR_ERR(psock);
		goto out_progs;
	}

	if (psock) {
		if ((msg_parser && READ_ONCE(psock->progs.msg_parser)) ||
		    (skb_progs  && READ_ONCE(psock->progs.skb_parser))) {
			sk_psock_put(sk, psock);
			ret = -EBUSY;
			goto out_progs;
		}
	} else {
		psock = sk_psock_init(sk, map->numa_node);
		if (!psock) {
			ret = -ENOMEM;
			goto out_progs;
		}
		sk_psock_is_new = true;
	}

	if (msg_parser)
		psock_set_prog(&psock->progs.msg_parser, msg_parser);
	if (sk_psock_is_new) {
		ret = tcp_bpf_init(sk);
		if (ret < 0)
			goto out_drop;
	} else {
		tcp_bpf_reinit(sk);
	}

	write_lock_bh(&sk->sk_callback_lock);
	if (skb_progs && !psock->parser.enabled) {
		ret = sk_psock_init_strp(sk, psock);
		if (ret) {
			write_unlock_bh(&sk->sk_callback_lock);
			goto out_drop;
		}
		psock_set_prog(&psock->progs.skb_verdict, skb_verdict);
		psock_set_prog(&psock->progs.skb_parser, skb_parser);
		sk_psock_start_strp(sk, psock);
	}
	write_unlock_bh(&sk->sk_callback_lock);
	return 0;
out_drop:
	sk_psock_put(sk, psock);
out_progs:
	if (msg_parser)
		bpf_prog_put(msg_parser);
out:
	if (skb_progs) {
		bpf_prog_put(skb_verdict);
		bpf_prog_put(skb_parser);
	}
	return ret;
}

static void sock_map_free(struct bpf_map *map)
{
	struct bpf_stab *stab = container_of(map, struct bpf_stab, map);
	int i;

	/* After the sync no updates or deletes will be in-flight so it
	 * is safe to walk map and remove entries without risking a race
	 * in EEXIST update case.
	 */
	synchronize_rcu();
	for (i = 0; i < stab->map.max_entries; i++) {
		struct sock **psk = &stab->sks[i];
		struct sock *sk;

		sk = xchg(psk, NULL);
		if (sk) {
			lock_sock(sk);
			rcu_read_lock();
			sock_map_unref(sk, psk);
			rcu_read_unlock();
			release_sock(sk);
		}
	}

	/* wait for psock readers accessing its map link */
	synchronize_rcu();

	bpf_map_area_free(stab->sks);
	kfree(stab);
}

static void sock_map_release_progs(struct bpf_map *map)
{
	psock_progs_drop(&container_of(map, struct bpf_stab, map)->progs);
}

static struct sock *__sock_map_lookup_elem(struct bpf_map *map, u32 key)
{
	struct bpf_stab *stab = container_of(map, struct bpf_stab, map);

	WARN_ON_ONCE(!rcu_read_lock_held());

	if (unlikely(key >= map->max_entries))
		return NULL;
	return READ_ONCE(stab->sks[key]);
}

static void *sock_map_lookup(struct bpf_map *map, void *key)
{
	return ERR_PTR(-EOPNOTSUPP);
}

static int __sock_map_delete(struct bpf_stab *stab, struct sock *sk_test,
			     struct sock **psk)
{
	struct sock *sk;
	int err = 0;

	raw_spin_lock_bh(&stab->lock);
	sk = *psk;
	if (!sk_test || sk_test == sk)
		sk = xchg(psk, NULL);

	if (likely(sk))
		sock_map_unref(sk, psk);
	else
		err = -EINVAL;

	raw_spin_unlock_bh(&stab->lock);
	return err;
}

static void sock_map_delete_from_link(struct bpf_map *map, struct sock *sk,
				      void *link_raw)
{
	struct bpf_stab *stab = container_of(map, struct bpf_stab, map);

	__sock_map_delete(stab, sk, link_raw);
}

static int sock_map_delete_elem(struct bpf_map *map, void *key)
{
	struct bpf_stab *stab = container_of(map, struct bpf_stab, map);
	u32 i = *(u32 *)key;
	struct sock **psk;

	if (unlikely(i >= map->max_entries))
		return -EINVAL;

	psk = &stab->sks[i];
	return __sock_map_delete(stab, NULL, psk);
}

static int sock_map_get_next_key(struct bpf_map *map, void *key, void *next)
{
	struct bpf_stab *stab = container_of(map, struct bpf_stab, map);
	u32 i = key ? *(u32 *)key : U32_MAX;
	u32 *key_next = next;

	if (i == stab->map.max_entries - 1)
		return -ENOENT;
	if (i >= stab->map.max_entries)
		*key_next = 0;
	else
		*key_next = i + 1;
	return 0;
}

static int sock_map_update_common(struct bpf_map *map, u32 idx,
				  struct sock *sk, u64 flags)
{
	struct bpf_stab *stab = container_of(map, struct bpf_stab, map);
	struct inet_connection_sock *icsk = inet_csk(sk);
	struct sk_psock_link *link;
	struct sk_psock *psock;
	struct sock *osk;
	int ret;

	WARN_ON_ONCE(!rcu_read_lock_held());
	if (unlikely(flags > BPF_EXIST))
		return -EINVAL;
	if (unlikely(idx >= map->max_entries))
		return -E2BIG;
	if (unlikely(rcu_access_pointer(icsk->icsk_ulp_data)))
		return -EINVAL;

	link = sk_psock_init_link();
	if (!link)
		return -ENOMEM;

	ret = sock_map_link(map, &stab->progs, sk);
	if (ret < 0)
		goto out_free;

	psock = sk_psock(sk);
	WARN_ON_ONCE(!psock);

	raw_spin_lock_bh(&stab->lock);
	osk = stab->sks[idx];
	if (osk && flags == BPF_NOEXIST) {
		ret = -EEXIST;
		goto out_unlock;
	} else if (!osk && flags == BPF_EXIST) {
		ret = -ENOENT;
		goto out_unlock;
	}

	sock_map_add_link(psock, link, map, &stab->sks[idx]);
	stab->sks[idx] = sk;
	if (osk)
		sock_map_unref(osk, &stab->sks[idx]);
	raw_spin_unlock_bh(&stab->lock);
	return 0;
out_unlock:
	raw_spin_unlock_bh(&stab->lock);
	if (psock)
		sk_psock_put(sk, psock);
out_free:
	sk_psock_free_link(link);
	return ret;
}

static bool sock_map_op_okay(const struct bpf_sock_ops_kern *ops)
{
	return ops->op == BPF_SOCK_OPS_PASSIVE_ESTABLISHED_CB ||
	       ops->op == BPF_SOCK_OPS_ACTIVE_ESTABLISHED_CB;
}

static bool sock_map_sk_is_suitable(const struct sock *sk)
{
	return sk->sk_type == SOCK_STREAM &&
	       sk->sk_protocol == IPPROTO_TCP;
}

static int sock_map_update_elem(struct bpf_map *map, void *key,
				void *value, u64 flags)
{
	u32 ufd = *(u32 *)value;
	u32 idx = *(u32 *)key;
	struct socket *sock;
	struct sock *sk;
	int ret;

	sock = sockfd_lookup(ufd, &ret);
	if (!sock)
		return ret;
	sk = sock->sk;
	if (!sk) {
		ret = -EINVAL;
		goto out;
	}
	if (!sock_map_sk_is_suitable(sk)) {
		ret = -EOPNOTSUPP;
		goto out;
	}

	sock_map_sk_acquire(sk);
	if (sk->sk_state != TCP_ESTABLISHED)
		ret = -EOPNOTSUPP;
	else
		ret = sock_map_update_common(map, idx, sk, flags);
	sock_map_sk_release(sk);
out:
	fput(sock->file);
	return ret;
}

BPF_CALL_4(bpf_sock_map_update, struct bpf_sock_ops_kern *, sops,
	   struct bpf_map *, map, void *, key, u64, flags)
{
	WARN_ON_ONCE(!rcu_read_lock_held());

	if (likely(sock_map_sk_is_suitable(sops->sk) &&
		   sock_map_op_okay(sops)))
		return sock_map_update_common(map, *(u32 *)key, sops->sk,
					      flags);
	return -EOPNOTSUPP;
}

const struct bpf_func_proto bpf_sock_map_update_proto = {
	.func		= bpf_sock_map_update,
	.gpl_only	= false,
	.pkt_access	= true,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_CONST_MAP_PTR,
	.arg3_type	= ARG_PTR_TO_MAP_KEY,
	.arg4_type	= ARG_ANYTHING,
};

BPF_CALL_4(bpf_sk_redirect_map, struct sk_buff *, skb,
	   struct bpf_map *, map, u32, key, u64, flags)
{
	struct tcp_skb_cb *tcb = TCP_SKB_CB(skb);

	if (unlikely(flags & ~(BPF_F_INGRESS)))
		return SK_DROP;
	tcb->bpf.flags = flags;
	tcb->bpf.sk_redir = __sock_map_lookup_elem(map, key);
	if (!tcb->bpf.sk_redir)
		return SK_DROP;
	return SK_PASS;
}

const struct bpf_func_proto bpf_sk_redirect_map_proto = {
	.func           = bpf_sk_redirect_map,
	.gpl_only       = false,
	.ret_type       = RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type      = ARG_CONST_MAP_PTR,
	.arg3_type      = ARG_ANYTHING,
	.arg4_type      = ARG_ANYTHING,
};

BPF_CALL_4(bpf_msg_redirect_map, struct sk_msg *, msg,
	   struct bpf_map *, map, u32, key, u64, flags)
{
	if (unlikely(flags & ~(BPF_F_INGRESS)))
		return SK_DROP;
	msg->flags = flags;
	msg->sk_redir = __sock_map_lookup_elem(map, key);
	if (!msg->sk_redir)
		return SK_DROP;
	return SK_PASS;
}

const struct bpf_func_proto bpf_msg_redirect_map_proto = {
	.func           = bpf_msg_redirect_map,
	.gpl_only       = false,
	.ret_type       = RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type      = ARG_CONST_MAP_PTR,
	.arg3_type      = ARG_ANYTHING,
	.arg4_type      = ARG_ANYTHING,
};

const struct bpf_map_ops sock_map_ops = {
	.map_alloc		= sock_map_alloc,
	.map_free		= sock_map_free,
	.map_get_next_key	= sock_map_get_next_key,
	.map_update_elem	= sock_map_update_elem,
	.map_delete_elem	= sock_map_delete_elem,
	.map_lookup_elem	= sock_map_lookup,
	.map_release_uref	= sock_map_release_progs,
	.map_check_btf		= map_check_no_btf,
};

struct bpf_htab_elem {
	struct rcu_head rcu;
	u32 hash;
	struct sock *sk;
	struct hlist_node node;
	u8 key[0];
};

struct bpf_htab_bucket {
	struct hlist_head head;
	raw_spinlock_t lock;
};

struct bpf_htab {
	struct bpf_map map;
	struct bpf_htab_bucket *buckets;
	u32 buckets_num;
	u32 elem_size;
	struct sk_psock_progs progs;
	atomic_t count;
};

static inline u32 sock_hash_bucket_hash(const void *key, u32 len)
{
	return jhash(key, len, 0);
}

static struct bpf_htab_bucket *sock_hash_select_bucket(struct bpf_htab *htab,
						       u32 hash)
{
	return &htab->buckets[hash & (htab->buckets_num - 1)];
}

static struct bpf_htab_elem *
sock_hash_lookup_elem_raw(struct hlist_head *head, u32 hash, void *key,
			  u32 key_size)
{
	struct bpf_htab_elem *elem;

	hlist_for_each_entry_rcu(elem, head, node) {
		if (elem->hash == hash &&
		    !memcmp(&elem->key, key, key_size))
			return elem;
	}

	return NULL;
}

static struct sock *__sock_hash_lookup_elem(struct bpf_map *map, void *key)
{
	struct bpf_htab *htab = container_of(map, struct bpf_htab, map);
	u32 key_size = map->key_size, hash;
	struct bpf_htab_bucket *bucket;
	struct bpf_htab_elem *elem;

	WARN_ON_ONCE(!rcu_read_lock_held());

	hash = sock_hash_bucket_hash(key, key_size);
	bucket = sock_hash_select_bucket(htab, hash);
	elem = sock_hash_lookup_elem_raw(&bucket->head, hash, key, key_size);

	return elem ? elem->sk : NULL;
}

static void sock_hash_free_elem(struct bpf_htab *htab,
				struct bpf_htab_elem *elem)
{
	atomic_dec(&htab->count);
	kfree_rcu(elem, rcu);
}

static void sock_hash_delete_from_link(struct bpf_map *map, struct sock *sk,
				       void *link_raw)
{
	struct bpf_htab *htab = container_of(map, struct bpf_htab, map);
	struct bpf_htab_elem *elem_probe, *elem = link_raw;
	struct bpf_htab_bucket *bucket;

	WARN_ON_ONCE(!rcu_read_lock_held());
	bucket = sock_hash_select_bucket(htab, elem->hash);

	/* elem may be deleted in parallel from the map, but access here
	 * is okay since it's going away only after RCU grace period.
	 * However, we need to check whether it's still present.
	 */
	raw_spin_lock_bh(&bucket->lock);
	elem_probe = sock_hash_lookup_elem_raw(&bucket->head, elem->hash,
					       elem->key, map->key_size);
	if (elem_probe && elem_probe == elem) {
		hlist_del_rcu(&elem->node);
		sock_map_unref(elem->sk, elem);
		sock_hash_free_elem(htab, elem);
	}
	raw_spin_unlock_bh(&bucket->lock);
}

static int sock_hash_delete_elem(struct bpf_map *map, void *key)
{
	struct bpf_htab *htab = container_of(map, struct bpf_htab, map);
	u32 hash, key_size = map->key_size;
	struct bpf_htab_bucket *bucket;
	struct bpf_htab_elem *elem;
	int ret = -ENOENT;

	hash = sock_hash_bucket_hash(key, key_size);
	bucket = sock_hash_select_bucket(htab, hash);

	raw_spin_lock_bh(&bucket->lock);
	elem = sock_hash_lookup_elem_raw(&bucket->head, hash, key, key_size);
	if (elem) {
		hlist_del_rcu(&elem->node);
		sock_map_unref(elem->sk, elem);
		sock_hash_free_elem(htab, elem);
		ret = 0;
	}
	raw_spin_unlock_bh(&bucket->lock);
	return ret;
}

static struct bpf_htab_elem *sock_hash_alloc_elem(struct bpf_htab *htab,
						  void *key, u32 key_size,
						  u32 hash, struct sock *sk,
						  struct bpf_htab_elem *old)
{
	struct bpf_htab_elem *new;

	if (atomic_inc_return(&htab->count) > htab->map.max_entries) {
		if (!old) {
			atomic_dec(&htab->count);
			return ERR_PTR(-E2BIG);
		}
	}

	new = kmalloc_node(htab->elem_size, GFP_ATOMIC | __GFP_NOWARN,
			   htab->map.numa_node);
	if (!new) {
		atomic_dec(&htab->count);
		return ERR_PTR(-ENOMEM);
	}
	memcpy(new->key, key, key_size);
	new->sk = sk;
	new->hash = hash;
	return new;
}

static int sock_hash_update_common(struct bpf_map *map, void *key,
				   struct sock *sk, u64 flags)
{
	struct bpf_htab *htab = container_of(map, struct bpf_htab, map);
	struct inet_connection_sock *icsk = inet_csk(sk);
	u32 key_size = map->key_size, hash;
	struct bpf_htab_elem *elem, *elem_new;
	struct bpf_htab_bucket *bucket;
	struct sk_psock_link *link;
	struct sk_psock *psock;
	int ret;

	WARN_ON_ONCE(!rcu_read_lock_held());
	if (unlikely(flags > BPF_EXIST))
		return -EINVAL;
	if (unlikely(icsk->icsk_ulp_data))
		return -EINVAL;

	link = sk_psock_init_link();
	if (!link)
		return -ENOMEM;

	ret = sock_map_link(map, &htab->progs, sk);
	if (ret < 0)
		goto out_free;

	psock = sk_psock(sk);
	WARN_ON_ONCE(!psock);

	hash = sock_hash_bucket_hash(key, key_size);
	bucket = sock_hash_select_bucket(htab, hash);

	raw_spin_lock_bh(&bucket->lock);
	elem = sock_hash_lookup_elem_raw(&bucket->head, hash, key, key_size);
	if (elem && flags == BPF_NOEXIST) {
		ret = -EEXIST;
		goto out_unlock;
	} else if (!elem && flags == BPF_EXIST) {
		ret = -ENOENT;
		goto out_unlock;
	}

	elem_new = sock_hash_alloc_elem(htab, key, key_size, hash, sk, elem);
	if (IS_ERR(elem_new)) {
		ret = PTR_ERR(elem_new);
		goto out_unlock;
	}

	sock_map_add_link(psock, link, map, elem_new);
	/* Add new element to the head of the list, so that
	 * concurrent search will find it before old elem.
	 */
	hlist_add_head_rcu(&elem_new->node, &bucket->head);
	if (elem) {
		hlist_del_rcu(&elem->node);
		sock_map_unref(elem->sk, elem);
		sock_hash_free_elem(htab, elem);
	}
	raw_spin_unlock_bh(&bucket->lock);
	return 0;
out_unlock:
	raw_spin_unlock_bh(&bucket->lock);
	sk_psock_put(sk, psock);
out_free:
	sk_psock_free_link(link);
	return ret;
}

static int sock_hash_update_elem(struct bpf_map *map, void *key,
				 void *value, u64 flags)
{
	u32 ufd = *(u32 *)value;
	struct socket *sock;
	struct sock *sk;
	int ret;

	sock = sockfd_lookup(ufd, &ret);
	if (!sock)
		return ret;
	sk = sock->sk;
	if (!sk) {
		ret = -EINVAL;
		goto out;
	}
	if (!sock_map_sk_is_suitable(sk)) {
		ret = -EOPNOTSUPP;
		goto out;
	}

	sock_map_sk_acquire(sk);
	if (sk->sk_state != TCP_ESTABLISHED)
		ret = -EOPNOTSUPP;
	else
		ret = sock_hash_update_common(map, key, sk, flags);
	sock_map_sk_release(sk);
out:
	fput(sock->file);
	return ret;
}

static int sock_hash_get_next_key(struct bpf_map *map, void *key,
				  void *key_next)
{
	struct bpf_htab *htab = container_of(map, struct bpf_htab, map);
	struct bpf_htab_elem *elem, *elem_next;
	u32 hash, key_size = map->key_size;
	struct hlist_head *head;
	int i = 0;

	if (!key)
		goto find_first_elem;
	hash = sock_hash_bucket_hash(key, key_size);
	head = &sock_hash_select_bucket(htab, hash)->head;
	elem = sock_hash_lookup_elem_raw(head, hash, key, key_size);
	if (!elem)
		goto find_first_elem;

	elem_next = hlist_entry_safe(rcu_dereference_raw(hlist_next_rcu(&elem->node)),
				     struct bpf_htab_elem, node);
	if (elem_next) {
		memcpy(key_next, elem_next->key, key_size);
		return 0;
	}

	i = hash & (htab->buckets_num - 1);
	i++;
find_first_elem:
	for (; i < htab->buckets_num; i++) {
		head = &sock_hash_select_bucket(htab, i)->head;
		elem_next = hlist_entry_safe(rcu_dereference_raw(hlist_first_rcu(head)),
					     struct bpf_htab_elem, node);
		if (elem_next) {
			memcpy(key_next, elem_next->key, key_size);
			return 0;
		}
	}

	return -ENOENT;
}

static struct bpf_map *sock_hash_alloc(union bpf_attr *attr)
{
	struct bpf_htab *htab;
	int i, err;
	u64 cost;

	if (!capable(CAP_NET_ADMIN))
		return ERR_PTR(-EPERM);
	if (attr->max_entries == 0 ||
	    attr->key_size    == 0 ||
	    attr->value_size  != 4 ||
	    attr->map_flags & ~SOCK_CREATE_FLAG_MASK)
		return ERR_PTR(-EINVAL);
	if (attr->key_size > MAX_BPF_STACK)
		return ERR_PTR(-E2BIG);

	htab = kzalloc(sizeof(*htab), GFP_USER);
	if (!htab)
		return ERR_PTR(-ENOMEM);

	bpf_map_init_from_attr(&htab->map, attr);

	htab->buckets_num = roundup_pow_of_two(htab->map.max_entries);
	htab->elem_size = sizeof(struct bpf_htab_elem) +
			  round_up(htab->map.key_size, 8);
	if (htab->buckets_num == 0 ||
	    htab->buckets_num > U32_MAX / sizeof(struct bpf_htab_bucket)) {
		err = -EINVAL;
		goto free_htab;
	}

	cost = (u64) htab->buckets_num * sizeof(struct bpf_htab_bucket) +
	       (u64) htab->elem_size * htab->map.max_entries;
	if (cost >= U32_MAX - PAGE_SIZE) {
		err = -EINVAL;
		goto free_htab;
	}

	htab->buckets = bpf_map_area_alloc(htab->buckets_num *
					   sizeof(struct bpf_htab_bucket),
					   htab->map.numa_node);
	if (!htab->buckets) {
		err = -ENOMEM;
		goto free_htab;
	}

	for (i = 0; i < htab->buckets_num; i++) {
		INIT_HLIST_HEAD(&htab->buckets[i].head);
		raw_spin_lock_init(&htab->buckets[i].lock);
	}

	return &htab->map;
free_htab:
	kfree(htab);
	return ERR_PTR(err);
}

static void sock_hash_free(struct bpf_map *map)
{
	struct bpf_htab *htab = container_of(map, struct bpf_htab, map);
	struct bpf_htab_bucket *bucket;
	struct bpf_htab_elem *elem;
	struct hlist_node *node;
	int i;

	/* After the sync no updates or deletes will be in-flight so it
	 * is safe to walk map and remove entries without risking a race
	 * in EEXIST update case.
	 */
	synchronize_rcu();
	for (i = 0; i < htab->buckets_num; i++) {
		bucket = sock_hash_select_bucket(htab, i);
		hlist_for_each_entry_safe(elem, node, &bucket->head, node) {
			hlist_del_rcu(&elem->node);
			lock_sock(elem->sk);
			rcu_read_lock();
			sock_map_unref(elem->sk, elem);
			rcu_read_unlock();
			release_sock(elem->sk);
		}
	}

	/* wait for psock readers accessing its map link */
	synchronize_rcu();

	bpf_map_area_free(htab->buckets);
	kfree(htab);
}

static void sock_hash_release_progs(struct bpf_map *map)
{
	psock_progs_drop(&container_of(map, struct bpf_htab, map)->progs);
}

BPF_CALL_4(bpf_sock_hash_update, struct bpf_sock_ops_kern *, sops,
	   struct bpf_map *, map, void *, key, u64, flags)
{
	WARN_ON_ONCE(!rcu_read_lock_held());

	if (likely(sock_map_sk_is_suitable(sops->sk) &&
		   sock_map_op_okay(sops)))
		return sock_hash_update_common(map, key, sops->sk, flags);
	return -EOPNOTSUPP;
}

const struct bpf_func_proto bpf_sock_hash_update_proto = {
	.func		= bpf_sock_hash_update,
	.gpl_only	= false,
	.pkt_access	= true,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_CONST_MAP_PTR,
	.arg3_type	= ARG_PTR_TO_MAP_KEY,
	.arg4_type	= ARG_ANYTHING,
};

BPF_CALL_4(bpf_sk_redirect_hash, struct sk_buff *, skb,
	   struct bpf_map *, map, void *, key, u64, flags)
{
	struct tcp_skb_cb *tcb = TCP_SKB_CB(skb);

	if (unlikely(flags & ~(BPF_F_INGRESS)))
		return SK_DROP;
	tcb->bpf.flags = flags;
	tcb->bpf.sk_redir = __sock_hash_lookup_elem(map, key);
	if (!tcb->bpf.sk_redir)
		return SK_DROP;
	return SK_PASS;
}

const struct bpf_func_proto bpf_sk_redirect_hash_proto = {
	.func           = bpf_sk_redirect_hash,
	.gpl_only       = false,
	.ret_type       = RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type      = ARG_CONST_MAP_PTR,
	.arg3_type      = ARG_PTR_TO_MAP_KEY,
	.arg4_type      = ARG_ANYTHING,
};

BPF_CALL_4(bpf_msg_redirect_hash, struct sk_msg *, msg,
	   struct bpf_map *, map, void *, key, u64, flags)
{
	if (unlikely(flags & ~(BPF_F_INGRESS)))
		return SK_DROP;
	msg->flags = flags;
	msg->sk_redir = __sock_hash_lookup_elem(map, key);
	if (!msg->sk_redir)
		return SK_DROP;
	return SK_PASS;
}

const struct bpf_func_proto bpf_msg_redirect_hash_proto = {
	.func           = bpf_msg_redirect_hash,
	.gpl_only       = false,
	.ret_type       = RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type      = ARG_CONST_MAP_PTR,
	.arg3_type      = ARG_PTR_TO_MAP_KEY,
	.arg4_type      = ARG_ANYTHING,
};

const struct bpf_map_ops sock_hash_ops = {
	.map_alloc		= sock_hash_alloc,
	.map_free		= sock_hash_free,
	.map_get_next_key	= sock_hash_get_next_key,
	.map_update_elem	= sock_hash_update_elem,
	.map_delete_elem	= sock_hash_delete_elem,
	.map_lookup_elem	= sock_map_lookup,
	.map_release_uref	= sock_hash_release_progs,
	.map_check_btf		= map_check_no_btf,
};

static struct sk_psock_progs *sock_map_progs(struct bpf_map *map)
{
	switch (map->map_type) {
	case BPF_MAP_TYPE_SOCKMAP:
		return &container_of(map, struct bpf_stab, map)->progs;
	case BPF_MAP_TYPE_SOCKHASH:
		return &container_of(map, struct bpf_htab, map)->progs;
	default:
		break;
	}

	return NULL;
}

int sock_map_prog_update(struct bpf_map *map, struct bpf_prog *prog,
			 u32 which)
{
	struct sk_psock_progs *progs = sock_map_progs(map);

	if (!progs)
		return -EOPNOTSUPP;

	switch (which) {
	case BPF_SK_MSG_VERDICT:
		psock_set_prog(&progs->msg_parser, prog);
		break;
	case BPF_SK_SKB_STREAM_PARSER:
		psock_set_prog(&progs->skb_parser, prog);
		break;
	case BPF_SK_SKB_STREAM_VERDICT:
		psock_set_prog(&progs->skb_verdict, prog);
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

void sk_psock_unlink(struct sock *sk, struct sk_psock_link *link)
{
	switch (link->map->map_type) {
	case BPF_MAP_TYPE_SOCKMAP:
		return sock_map_delete_from_link(link->map, sk,
						 link->link_raw);
	case BPF_MAP_TYPE_SOCKHASH:
		return sock_hash_delete_from_link(link->map, sk,
						  link->link_raw);
	default:
		break;
	}
}
