// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2017 - 2018 Covalent IO, Inc. http://covalent.io */

#include <linux/bpf.h>
#include <linux/btf_ids.h>
#include <linux/filter.h>
#include <linux/errno.h>
#include <linux/file.h>
#include <linux/net.h>
#include <linux/workqueue.h>
#include <linux/skmsg.h>
#include <linux/list.h>
#include <linux/jhash.h>
#include <linux/sock_diag.h>
#include <net/udp.h>

struct bpf_stab {
	struct bpf_map map;
	struct sock **sks;
	struct sk_psock_progs progs;
	raw_spinlock_t lock;
};

#define SOCK_CREATE_FLAG_MASK				\
	(BPF_F_NUMA_NODE | BPF_F_RDONLY | BPF_F_WRONLY)

static int sock_map_prog_update(struct bpf_map *map, struct bpf_prog *prog,
				struct bpf_prog *old, u32 which);
static struct sk_psock_progs *sock_map_progs(struct bpf_map *map);

static struct bpf_map *sock_map_alloc(union bpf_attr *attr)
{
	struct bpf_stab *stab;

	if (!capable(CAP_NET_ADMIN))
		return ERR_PTR(-EPERM);
	if (attr->max_entries == 0 ||
	    attr->key_size    != 4 ||
	    (attr->value_size != sizeof(u32) &&
	     attr->value_size != sizeof(u64)) ||
	    attr->map_flags & ~SOCK_CREATE_FLAG_MASK)
		return ERR_PTR(-EINVAL);

	stab = bpf_map_area_alloc(sizeof(*stab), NUMA_NO_NODE);
	if (!stab)
		return ERR_PTR(-ENOMEM);

	bpf_map_init_from_attr(&stab->map, attr);
	raw_spin_lock_init(&stab->lock);

	stab->sks = bpf_map_area_alloc((u64) stab->map.max_entries *
				       sizeof(struct sock *),
				       stab->map.numa_node);
	if (!stab->sks) {
		bpf_map_area_free(stab);
		return ERR_PTR(-ENOMEM);
	}

	return &stab->map;
}

int sock_map_get_from_fd(const union bpf_attr *attr, struct bpf_prog *prog)
{
	u32 ufd = attr->target_fd;
	struct bpf_map *map;
	struct fd f;
	int ret;

	if (attr->attach_flags || attr->replace_bpf_fd)
		return -EINVAL;

	f = fdget(ufd);
	map = __bpf_map_get(f);
	if (IS_ERR(map))
		return PTR_ERR(map);
	ret = sock_map_prog_update(map, prog, NULL, attr->attach_type);
	fdput(f);
	return ret;
}

int sock_map_prog_detach(const union bpf_attr *attr, enum bpf_prog_type ptype)
{
	u32 ufd = attr->target_fd;
	struct bpf_prog *prog;
	struct bpf_map *map;
	struct fd f;
	int ret;

	if (attr->attach_flags || attr->replace_bpf_fd)
		return -EINVAL;

	f = fdget(ufd);
	map = __bpf_map_get(f);
	if (IS_ERR(map))
		return PTR_ERR(map);

	prog = bpf_prog_get(attr->attach_bpf_fd);
	if (IS_ERR(prog)) {
		ret = PTR_ERR(prog);
		goto put_map;
	}

	if (prog->type != ptype) {
		ret = -EINVAL;
		goto put_prog;
	}

	ret = sock_map_prog_update(map, NULL, prog, attr->attach_type);
put_prog:
	bpf_prog_put(prog);
put_map:
	fdput(f);
	return ret;
}

static void sock_map_sk_acquire(struct sock *sk)
	__acquires(&sk->sk_lock.slock)
{
	lock_sock(sk);
	rcu_read_lock();
}

static void sock_map_sk_release(struct sock *sk)
	__releases(&sk->sk_lock.slock)
{
	rcu_read_unlock();
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
	bool strp_stop = false, verdict_stop = false;
	struct sk_psock_link *link, *tmp;

	spin_lock_bh(&psock->link_lock);
	list_for_each_entry_safe(link, tmp, &psock->link, list) {
		if (link->link_raw == link_raw) {
			struct bpf_map *map = link->map;
			struct sk_psock_progs *progs = sock_map_progs(map);

			if (psock->saved_data_ready && progs->stream_parser)
				strp_stop = true;
			if (psock->saved_data_ready && progs->stream_verdict)
				verdict_stop = true;
			if (psock->saved_data_ready && progs->skb_verdict)
				verdict_stop = true;
			list_del(&link->list);
			sk_psock_free_link(link);
		}
	}
	spin_unlock_bh(&psock->link_lock);
	if (strp_stop || verdict_stop) {
		write_lock_bh(&sk->sk_callback_lock);
		if (strp_stop)
			sk_psock_stop_strp(sk, psock);
		if (verdict_stop)
			sk_psock_stop_verdict(sk, psock);

		if (psock->psock_update_sk_prot)
			psock->psock_update_sk_prot(sk, psock, false);
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

static int sock_map_init_proto(struct sock *sk, struct sk_psock *psock)
{
	if (!sk->sk_prot->psock_update_sk_prot)
		return -EINVAL;
	psock->psock_update_sk_prot = sk->sk_prot->psock_update_sk_prot;
	return sk->sk_prot->psock_update_sk_prot(sk, psock, false);
}

static struct sk_psock *sock_map_psock_get_checked(struct sock *sk)
{
	struct sk_psock *psock;

	rcu_read_lock();
	psock = sk_psock(sk);
	if (psock) {
		if (sk->sk_prot->close != sock_map_close) {
			psock = ERR_PTR(-EBUSY);
			goto out;
		}

		if (!refcount_inc_not_zero(&psock->refcnt))
			psock = ERR_PTR(-EBUSY);
	}
out:
	rcu_read_unlock();
	return psock;
}

static int sock_map_link(struct bpf_map *map, struct sock *sk)
{
	struct sk_psock_progs *progs = sock_map_progs(map);
	struct bpf_prog *stream_verdict = NULL;
	struct bpf_prog *stream_parser = NULL;
	struct bpf_prog *skb_verdict = NULL;
	struct bpf_prog *msg_parser = NULL;
	struct sk_psock *psock;
	int ret;

	stream_verdict = READ_ONCE(progs->stream_verdict);
	if (stream_verdict) {
		stream_verdict = bpf_prog_inc_not_zero(stream_verdict);
		if (IS_ERR(stream_verdict))
			return PTR_ERR(stream_verdict);
	}

	stream_parser = READ_ONCE(progs->stream_parser);
	if (stream_parser) {
		stream_parser = bpf_prog_inc_not_zero(stream_parser);
		if (IS_ERR(stream_parser)) {
			ret = PTR_ERR(stream_parser);
			goto out_put_stream_verdict;
		}
	}

	msg_parser = READ_ONCE(progs->msg_parser);
	if (msg_parser) {
		msg_parser = bpf_prog_inc_not_zero(msg_parser);
		if (IS_ERR(msg_parser)) {
			ret = PTR_ERR(msg_parser);
			goto out_put_stream_parser;
		}
	}

	skb_verdict = READ_ONCE(progs->skb_verdict);
	if (skb_verdict) {
		skb_verdict = bpf_prog_inc_not_zero(skb_verdict);
		if (IS_ERR(skb_verdict)) {
			ret = PTR_ERR(skb_verdict);
			goto out_put_msg_parser;
		}
	}

	psock = sock_map_psock_get_checked(sk);
	if (IS_ERR(psock)) {
		ret = PTR_ERR(psock);
		goto out_progs;
	}

	if (psock) {
		if ((msg_parser && READ_ONCE(psock->progs.msg_parser)) ||
		    (stream_parser  && READ_ONCE(psock->progs.stream_parser)) ||
		    (skb_verdict && READ_ONCE(psock->progs.skb_verdict)) ||
		    (skb_verdict && READ_ONCE(psock->progs.stream_verdict)) ||
		    (stream_verdict && READ_ONCE(psock->progs.skb_verdict)) ||
		    (stream_verdict && READ_ONCE(psock->progs.stream_verdict))) {
			sk_psock_put(sk, psock);
			ret = -EBUSY;
			goto out_progs;
		}
	} else {
		psock = sk_psock_init(sk, map->numa_node);
		if (IS_ERR(psock)) {
			ret = PTR_ERR(psock);
			goto out_progs;
		}
	}

	if (msg_parser)
		psock_set_prog(&psock->progs.msg_parser, msg_parser);
	if (stream_parser)
		psock_set_prog(&psock->progs.stream_parser, stream_parser);
	if (stream_verdict)
		psock_set_prog(&psock->progs.stream_verdict, stream_verdict);
	if (skb_verdict)
		psock_set_prog(&psock->progs.skb_verdict, skb_verdict);

	/* msg_* and stream_* programs references tracked in psock after this
	 * point. Reference dec and cleanup will occur through psock destructor
	 */
	ret = sock_map_init_proto(sk, psock);
	if (ret < 0) {
		sk_psock_put(sk, psock);
		goto out;
	}

	write_lock_bh(&sk->sk_callback_lock);
	if (stream_parser && stream_verdict && !psock->saved_data_ready) {
		ret = sk_psock_init_strp(sk, psock);
		if (ret) {
			write_unlock_bh(&sk->sk_callback_lock);
			sk_psock_put(sk, psock);
			goto out;
		}
		sk_psock_start_strp(sk, psock);
	} else if (!stream_parser && stream_verdict && !psock->saved_data_ready) {
		sk_psock_start_verdict(sk,psock);
	} else if (!stream_verdict && skb_verdict && !psock->saved_data_ready) {
		sk_psock_start_verdict(sk, psock);
	}
	write_unlock_bh(&sk->sk_callback_lock);
	return 0;
out_progs:
	if (skb_verdict)
		bpf_prog_put(skb_verdict);
out_put_msg_parser:
	if (msg_parser)
		bpf_prog_put(msg_parser);
out_put_stream_parser:
	if (stream_parser)
		bpf_prog_put(stream_parser);
out_put_stream_verdict:
	if (stream_verdict)
		bpf_prog_put(stream_verdict);
out:
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
			sock_hold(sk);
			lock_sock(sk);
			rcu_read_lock();
			sock_map_unref(sk, psk);
			rcu_read_unlock();
			release_sock(sk);
			sock_put(sk);
		}
	}

	/* wait for psock readers accessing its map link */
	synchronize_rcu();

	bpf_map_area_free(stab->sks);
	bpf_map_area_free(stab);
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
	struct sock *sk;

	sk = __sock_map_lookup_elem(map, *(u32 *)key);
	if (!sk)
		return NULL;
	if (sk_is_refcounted(sk) && !refcount_inc_not_zero(&sk->sk_refcnt))
		return NULL;
	return sk;
}

static void *sock_map_lookup_sys(struct bpf_map *map, void *key)
{
	struct sock *sk;

	if (map->value_size != sizeof(u64))
		return ERR_PTR(-ENOSPC);

	sk = __sock_map_lookup_elem(map, *(u32 *)key);
	if (!sk)
		return ERR_PTR(-ENOENT);

	__sock_gen_cookie(sk);
	return &sk->sk_cookie;
}

static int __sock_map_delete(struct bpf_stab *stab, struct sock *sk_test,
			     struct sock **psk)
{
	struct sock *sk;
	int err = 0;

	if (irqs_disabled())
		return -EOPNOTSUPP; /* locks here are hardirq-unsafe */

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
	struct sk_psock_link *link;
	struct sk_psock *psock;
	struct sock *osk;
	int ret;

	WARN_ON_ONCE(!rcu_read_lock_held());
	if (unlikely(flags > BPF_EXIST))
		return -EINVAL;
	if (unlikely(idx >= map->max_entries))
		return -E2BIG;

	link = sk_psock_init_link();
	if (!link)
		return -ENOMEM;

	ret = sock_map_link(map, sk);
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
	       ops->op == BPF_SOCK_OPS_ACTIVE_ESTABLISHED_CB ||
	       ops->op == BPF_SOCK_OPS_TCP_LISTEN_CB;
}

static bool sock_map_redirect_allowed(const struct sock *sk)
{
	if (sk_is_tcp(sk))
		return sk->sk_state != TCP_LISTEN;
	else
		return sk->sk_state == TCP_ESTABLISHED;
}

static bool sock_map_sk_is_suitable(const struct sock *sk)
{
	return !!sk->sk_prot->psock_update_sk_prot;
}

static bool sock_map_sk_state_allowed(const struct sock *sk)
{
	if (sk_is_tcp(sk))
		return (1 << sk->sk_state) & (TCPF_ESTABLISHED | TCPF_LISTEN);
	if (sk_is_stream_unix(sk))
		return (1 << sk->sk_state) & TCPF_ESTABLISHED;
	return true;
}

static int sock_hash_update_common(struct bpf_map *map, void *key,
				   struct sock *sk, u64 flags);

int sock_map_update_elem_sys(struct bpf_map *map, void *key, void *value,
			     u64 flags)
{
	struct socket *sock;
	struct sock *sk;
	int ret;
	u64 ufd;

	if (map->value_size == sizeof(u64))
		ufd = *(u64 *)value;
	else
		ufd = *(u32 *)value;
	if (ufd > S32_MAX)
		return -EINVAL;

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
	if (!sock_map_sk_state_allowed(sk))
		ret = -EOPNOTSUPP;
	else if (map->map_type == BPF_MAP_TYPE_SOCKMAP)
		ret = sock_map_update_common(map, *(u32 *)key, sk, flags);
	else
		ret = sock_hash_update_common(map, key, sk, flags);
	sock_map_sk_release(sk);
out:
	sockfd_put(sock);
	return ret;
}

static int sock_map_update_elem(struct bpf_map *map, void *key,
				void *value, u64 flags)
{
	struct sock *sk = (struct sock *)value;
	int ret;

	if (unlikely(!sk || !sk_fullsock(sk)))
		return -EINVAL;

	if (!sock_map_sk_is_suitable(sk))
		return -EOPNOTSUPP;

	local_bh_disable();
	bh_lock_sock(sk);
	if (!sock_map_sk_state_allowed(sk))
		ret = -EOPNOTSUPP;
	else if (map->map_type == BPF_MAP_TYPE_SOCKMAP)
		ret = sock_map_update_common(map, *(u32 *)key, sk, flags);
	else
		ret = sock_hash_update_common(map, key, sk, flags);
	bh_unlock_sock(sk);
	local_bh_enable();
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
	struct sock *sk;

	if (unlikely(flags & ~(BPF_F_INGRESS)))
		return SK_DROP;

	sk = __sock_map_lookup_elem(map, key);
	if (unlikely(!sk || !sock_map_redirect_allowed(sk)))
		return SK_DROP;

	skb_bpf_set_redir(skb, sk, flags & BPF_F_INGRESS);
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
	struct sock *sk;

	if (unlikely(flags & ~(BPF_F_INGRESS)))
		return SK_DROP;

	sk = __sock_map_lookup_elem(map, key);
	if (unlikely(!sk || !sock_map_redirect_allowed(sk)))
		return SK_DROP;
	if (!(flags & BPF_F_INGRESS) && !sk_is_tcp(sk))
		return SK_DROP;

	msg->flags = flags;
	msg->sk_redir = sk;
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

struct sock_map_seq_info {
	struct bpf_map *map;
	struct sock *sk;
	u32 index;
};

struct bpf_iter__sockmap {
	__bpf_md_ptr(struct bpf_iter_meta *, meta);
	__bpf_md_ptr(struct bpf_map *, map);
	__bpf_md_ptr(void *, key);
	__bpf_md_ptr(struct sock *, sk);
};

DEFINE_BPF_ITER_FUNC(sockmap, struct bpf_iter_meta *meta,
		     struct bpf_map *map, void *key,
		     struct sock *sk)

static void *sock_map_seq_lookup_elem(struct sock_map_seq_info *info)
{
	if (unlikely(info->index >= info->map->max_entries))
		return NULL;

	info->sk = __sock_map_lookup_elem(info->map, info->index);

	/* can't return sk directly, since that might be NULL */
	return info;
}

static void *sock_map_seq_start(struct seq_file *seq, loff_t *pos)
	__acquires(rcu)
{
	struct sock_map_seq_info *info = seq->private;

	if (*pos == 0)
		++*pos;

	/* pairs with sock_map_seq_stop */
	rcu_read_lock();
	return sock_map_seq_lookup_elem(info);
}

static void *sock_map_seq_next(struct seq_file *seq, void *v, loff_t *pos)
	__must_hold(rcu)
{
	struct sock_map_seq_info *info = seq->private;

	++*pos;
	++info->index;

	return sock_map_seq_lookup_elem(info);
}

static int sock_map_seq_show(struct seq_file *seq, void *v)
	__must_hold(rcu)
{
	struct sock_map_seq_info *info = seq->private;
	struct bpf_iter__sockmap ctx = {};
	struct bpf_iter_meta meta;
	struct bpf_prog *prog;

	meta.seq = seq;
	prog = bpf_iter_get_info(&meta, !v);
	if (!prog)
		return 0;

	ctx.meta = &meta;
	ctx.map = info->map;
	if (v) {
		ctx.key = &info->index;
		ctx.sk = info->sk;
	}

	return bpf_iter_run_prog(prog, &ctx);
}

static void sock_map_seq_stop(struct seq_file *seq, void *v)
	__releases(rcu)
{
	if (!v)
		(void)sock_map_seq_show(seq, NULL);

	/* pairs with sock_map_seq_start */
	rcu_read_unlock();
}

static const struct seq_operations sock_map_seq_ops = {
	.start	= sock_map_seq_start,
	.next	= sock_map_seq_next,
	.stop	= sock_map_seq_stop,
	.show	= sock_map_seq_show,
};

static int sock_map_init_seq_private(void *priv_data,
				     struct bpf_iter_aux_info *aux)
{
	struct sock_map_seq_info *info = priv_data;

	bpf_map_inc_with_uref(aux->map);
	info->map = aux->map;
	return 0;
}

static void sock_map_fini_seq_private(void *priv_data)
{
	struct sock_map_seq_info *info = priv_data;

	bpf_map_put_with_uref(info->map);
}

static const struct bpf_iter_seq_info sock_map_iter_seq_info = {
	.seq_ops		= &sock_map_seq_ops,
	.init_seq_private	= sock_map_init_seq_private,
	.fini_seq_private	= sock_map_fini_seq_private,
	.seq_priv_size		= sizeof(struct sock_map_seq_info),
};

BTF_ID_LIST_SINGLE(sock_map_btf_ids, struct, bpf_stab)
const struct bpf_map_ops sock_map_ops = {
	.map_meta_equal		= bpf_map_meta_equal,
	.map_alloc		= sock_map_alloc,
	.map_free		= sock_map_free,
	.map_get_next_key	= sock_map_get_next_key,
	.map_lookup_elem_sys_only = sock_map_lookup_sys,
	.map_update_elem	= sock_map_update_elem,
	.map_delete_elem	= sock_map_delete_elem,
	.map_lookup_elem	= sock_map_lookup,
	.map_release_uref	= sock_map_release_progs,
	.map_check_btf		= map_check_no_btf,
	.map_btf_id		= &sock_map_btf_ids[0],
	.iter_seq_info		= &sock_map_iter_seq_info,
};

struct bpf_shtab_elem {
	struct rcu_head rcu;
	u32 hash;
	struct sock *sk;
	struct hlist_node node;
	u8 key[];
};

struct bpf_shtab_bucket {
	struct hlist_head head;
	raw_spinlock_t lock;
};

struct bpf_shtab {
	struct bpf_map map;
	struct bpf_shtab_bucket *buckets;
	u32 buckets_num;
	u32 elem_size;
	struct sk_psock_progs progs;
	atomic_t count;
};

static inline u32 sock_hash_bucket_hash(const void *key, u32 len)
{
	return jhash(key, len, 0);
}

static struct bpf_shtab_bucket *sock_hash_select_bucket(struct bpf_shtab *htab,
							u32 hash)
{
	return &htab->buckets[hash & (htab->buckets_num - 1)];
}

static struct bpf_shtab_elem *
sock_hash_lookup_elem_raw(struct hlist_head *head, u32 hash, void *key,
			  u32 key_size)
{
	struct bpf_shtab_elem *elem;

	hlist_for_each_entry_rcu(elem, head, node) {
		if (elem->hash == hash &&
		    !memcmp(&elem->key, key, key_size))
			return elem;
	}

	return NULL;
}

static struct sock *__sock_hash_lookup_elem(struct bpf_map *map, void *key)
{
	struct bpf_shtab *htab = container_of(map, struct bpf_shtab, map);
	u32 key_size = map->key_size, hash;
	struct bpf_shtab_bucket *bucket;
	struct bpf_shtab_elem *elem;

	WARN_ON_ONCE(!rcu_read_lock_held());

	hash = sock_hash_bucket_hash(key, key_size);
	bucket = sock_hash_select_bucket(htab, hash);
	elem = sock_hash_lookup_elem_raw(&bucket->head, hash, key, key_size);

	return elem ? elem->sk : NULL;
}

static void sock_hash_free_elem(struct bpf_shtab *htab,
				struct bpf_shtab_elem *elem)
{
	atomic_dec(&htab->count);
	kfree_rcu(elem, rcu);
}

static void sock_hash_delete_from_link(struct bpf_map *map, struct sock *sk,
				       void *link_raw)
{
	struct bpf_shtab *htab = container_of(map, struct bpf_shtab, map);
	struct bpf_shtab_elem *elem_probe, *elem = link_raw;
	struct bpf_shtab_bucket *bucket;

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
	struct bpf_shtab *htab = container_of(map, struct bpf_shtab, map);
	u32 hash, key_size = map->key_size;
	struct bpf_shtab_bucket *bucket;
	struct bpf_shtab_elem *elem;
	int ret = -ENOENT;

	if (irqs_disabled())
		return -EOPNOTSUPP; /* locks here are hardirq-unsafe */

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

static struct bpf_shtab_elem *sock_hash_alloc_elem(struct bpf_shtab *htab,
						   void *key, u32 key_size,
						   u32 hash, struct sock *sk,
						   struct bpf_shtab_elem *old)
{
	struct bpf_shtab_elem *new;

	if (atomic_inc_return(&htab->count) > htab->map.max_entries) {
		if (!old) {
			atomic_dec(&htab->count);
			return ERR_PTR(-E2BIG);
		}
	}

	new = bpf_map_kmalloc_node(&htab->map, htab->elem_size,
				   GFP_ATOMIC | __GFP_NOWARN,
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
	struct bpf_shtab *htab = container_of(map, struct bpf_shtab, map);
	u32 key_size = map->key_size, hash;
	struct bpf_shtab_elem *elem, *elem_new;
	struct bpf_shtab_bucket *bucket;
	struct sk_psock_link *link;
	struct sk_psock *psock;
	int ret;

	WARN_ON_ONCE(!rcu_read_lock_held());
	if (unlikely(flags > BPF_EXIST))
		return -EINVAL;

	link = sk_psock_init_link();
	if (!link)
		return -ENOMEM;

	ret = sock_map_link(map, sk);
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

static int sock_hash_get_next_key(struct bpf_map *map, void *key,
				  void *key_next)
{
	struct bpf_shtab *htab = container_of(map, struct bpf_shtab, map);
	struct bpf_shtab_elem *elem, *elem_next;
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

	elem_next = hlist_entry_safe(rcu_dereference(hlist_next_rcu(&elem->node)),
				     struct bpf_shtab_elem, node);
	if (elem_next) {
		memcpy(key_next, elem_next->key, key_size);
		return 0;
	}

	i = hash & (htab->buckets_num - 1);
	i++;
find_first_elem:
	for (; i < htab->buckets_num; i++) {
		head = &sock_hash_select_bucket(htab, i)->head;
		elem_next = hlist_entry_safe(rcu_dereference(hlist_first_rcu(head)),
					     struct bpf_shtab_elem, node);
		if (elem_next) {
			memcpy(key_next, elem_next->key, key_size);
			return 0;
		}
	}

	return -ENOENT;
}

static struct bpf_map *sock_hash_alloc(union bpf_attr *attr)
{
	struct bpf_shtab *htab;
	int i, err;

	if (!capable(CAP_NET_ADMIN))
		return ERR_PTR(-EPERM);
	if (attr->max_entries == 0 ||
	    attr->key_size    == 0 ||
	    (attr->value_size != sizeof(u32) &&
	     attr->value_size != sizeof(u64)) ||
	    attr->map_flags & ~SOCK_CREATE_FLAG_MASK)
		return ERR_PTR(-EINVAL);
	if (attr->key_size > MAX_BPF_STACK)
		return ERR_PTR(-E2BIG);

	htab = bpf_map_area_alloc(sizeof(*htab), NUMA_NO_NODE);
	if (!htab)
		return ERR_PTR(-ENOMEM);

	bpf_map_init_from_attr(&htab->map, attr);

	htab->buckets_num = roundup_pow_of_two(htab->map.max_entries);
	htab->elem_size = sizeof(struct bpf_shtab_elem) +
			  round_up(htab->map.key_size, 8);
	if (htab->buckets_num == 0 ||
	    htab->buckets_num > U32_MAX / sizeof(struct bpf_shtab_bucket)) {
		err = -EINVAL;
		goto free_htab;
	}

	htab->buckets = bpf_map_area_alloc(htab->buckets_num *
					   sizeof(struct bpf_shtab_bucket),
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
	bpf_map_area_free(htab);
	return ERR_PTR(err);
}

static void sock_hash_free(struct bpf_map *map)
{
	struct bpf_shtab *htab = container_of(map, struct bpf_shtab, map);
	struct bpf_shtab_bucket *bucket;
	struct hlist_head unlink_list;
	struct bpf_shtab_elem *elem;
	struct hlist_node *node;
	int i;

	/* After the sync no updates or deletes will be in-flight so it
	 * is safe to walk map and remove entries without risking a race
	 * in EEXIST update case.
	 */
	synchronize_rcu();
	for (i = 0; i < htab->buckets_num; i++) {
		bucket = sock_hash_select_bucket(htab, i);

		/* We are racing with sock_hash_delete_from_link to
		 * enter the spin-lock critical section. Every socket on
		 * the list is still linked to sockhash. Since link
		 * exists, psock exists and holds a ref to socket. That
		 * lets us to grab a socket ref too.
		 */
		raw_spin_lock_bh(&bucket->lock);
		hlist_for_each_entry(elem, &bucket->head, node)
			sock_hold(elem->sk);
		hlist_move_list(&bucket->head, &unlink_list);
		raw_spin_unlock_bh(&bucket->lock);

		/* Process removed entries out of atomic context to
		 * block for socket lock before deleting the psock's
		 * link to sockhash.
		 */
		hlist_for_each_entry_safe(elem, node, &unlink_list, node) {
			hlist_del(&elem->node);
			lock_sock(elem->sk);
			rcu_read_lock();
			sock_map_unref(elem->sk, elem);
			rcu_read_unlock();
			release_sock(elem->sk);
			sock_put(elem->sk);
			sock_hash_free_elem(htab, elem);
		}
	}

	/* wait for psock readers accessing its map link */
	synchronize_rcu();

	bpf_map_area_free(htab->buckets);
	bpf_map_area_free(htab);
}

static void *sock_hash_lookup_sys(struct bpf_map *map, void *key)
{
	struct sock *sk;

	if (map->value_size != sizeof(u64))
		return ERR_PTR(-ENOSPC);

	sk = __sock_hash_lookup_elem(map, key);
	if (!sk)
		return ERR_PTR(-ENOENT);

	__sock_gen_cookie(sk);
	return &sk->sk_cookie;
}

static void *sock_hash_lookup(struct bpf_map *map, void *key)
{
	struct sock *sk;

	sk = __sock_hash_lookup_elem(map, key);
	if (!sk)
		return NULL;
	if (sk_is_refcounted(sk) && !refcount_inc_not_zero(&sk->sk_refcnt))
		return NULL;
	return sk;
}

static void sock_hash_release_progs(struct bpf_map *map)
{
	psock_progs_drop(&container_of(map, struct bpf_shtab, map)->progs);
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
	struct sock *sk;

	if (unlikely(flags & ~(BPF_F_INGRESS)))
		return SK_DROP;

	sk = __sock_hash_lookup_elem(map, key);
	if (unlikely(!sk || !sock_map_redirect_allowed(sk)))
		return SK_DROP;

	skb_bpf_set_redir(skb, sk, flags & BPF_F_INGRESS);
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
	struct sock *sk;

	if (unlikely(flags & ~(BPF_F_INGRESS)))
		return SK_DROP;

	sk = __sock_hash_lookup_elem(map, key);
	if (unlikely(!sk || !sock_map_redirect_allowed(sk)))
		return SK_DROP;
	if (!(flags & BPF_F_INGRESS) && !sk_is_tcp(sk))
		return SK_DROP;

	msg->flags = flags;
	msg->sk_redir = sk;
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

struct sock_hash_seq_info {
	struct bpf_map *map;
	struct bpf_shtab *htab;
	u32 bucket_id;
};

static void *sock_hash_seq_find_next(struct sock_hash_seq_info *info,
				     struct bpf_shtab_elem *prev_elem)
{
	const struct bpf_shtab *htab = info->htab;
	struct bpf_shtab_bucket *bucket;
	struct bpf_shtab_elem *elem;
	struct hlist_node *node;

	/* try to find next elem in the same bucket */
	if (prev_elem) {
		node = rcu_dereference(hlist_next_rcu(&prev_elem->node));
		elem = hlist_entry_safe(node, struct bpf_shtab_elem, node);
		if (elem)
			return elem;

		/* no more elements, continue in the next bucket */
		info->bucket_id++;
	}

	for (; info->bucket_id < htab->buckets_num; info->bucket_id++) {
		bucket = &htab->buckets[info->bucket_id];
		node = rcu_dereference(hlist_first_rcu(&bucket->head));
		elem = hlist_entry_safe(node, struct bpf_shtab_elem, node);
		if (elem)
			return elem;
	}

	return NULL;
}

static void *sock_hash_seq_start(struct seq_file *seq, loff_t *pos)
	__acquires(rcu)
{
	struct sock_hash_seq_info *info = seq->private;

	if (*pos == 0)
		++*pos;

	/* pairs with sock_hash_seq_stop */
	rcu_read_lock();
	return sock_hash_seq_find_next(info, NULL);
}

static void *sock_hash_seq_next(struct seq_file *seq, void *v, loff_t *pos)
	__must_hold(rcu)
{
	struct sock_hash_seq_info *info = seq->private;

	++*pos;
	return sock_hash_seq_find_next(info, v);
}

static int sock_hash_seq_show(struct seq_file *seq, void *v)
	__must_hold(rcu)
{
	struct sock_hash_seq_info *info = seq->private;
	struct bpf_iter__sockmap ctx = {};
	struct bpf_shtab_elem *elem = v;
	struct bpf_iter_meta meta;
	struct bpf_prog *prog;

	meta.seq = seq;
	prog = bpf_iter_get_info(&meta, !elem);
	if (!prog)
		return 0;

	ctx.meta = &meta;
	ctx.map = info->map;
	if (elem) {
		ctx.key = elem->key;
		ctx.sk = elem->sk;
	}

	return bpf_iter_run_prog(prog, &ctx);
}

static void sock_hash_seq_stop(struct seq_file *seq, void *v)
	__releases(rcu)
{
	if (!v)
		(void)sock_hash_seq_show(seq, NULL);

	/* pairs with sock_hash_seq_start */
	rcu_read_unlock();
}

static const struct seq_operations sock_hash_seq_ops = {
	.start	= sock_hash_seq_start,
	.next	= sock_hash_seq_next,
	.stop	= sock_hash_seq_stop,
	.show	= sock_hash_seq_show,
};

static int sock_hash_init_seq_private(void *priv_data,
				      struct bpf_iter_aux_info *aux)
{
	struct sock_hash_seq_info *info = priv_data;

	bpf_map_inc_with_uref(aux->map);
	info->map = aux->map;
	info->htab = container_of(aux->map, struct bpf_shtab, map);
	return 0;
}

static void sock_hash_fini_seq_private(void *priv_data)
{
	struct sock_hash_seq_info *info = priv_data;

	bpf_map_put_with_uref(info->map);
}

static const struct bpf_iter_seq_info sock_hash_iter_seq_info = {
	.seq_ops		= &sock_hash_seq_ops,
	.init_seq_private	= sock_hash_init_seq_private,
	.fini_seq_private	= sock_hash_fini_seq_private,
	.seq_priv_size		= sizeof(struct sock_hash_seq_info),
};

BTF_ID_LIST_SINGLE(sock_hash_map_btf_ids, struct, bpf_shtab)
const struct bpf_map_ops sock_hash_ops = {
	.map_meta_equal		= bpf_map_meta_equal,
	.map_alloc		= sock_hash_alloc,
	.map_free		= sock_hash_free,
	.map_get_next_key	= sock_hash_get_next_key,
	.map_update_elem	= sock_map_update_elem,
	.map_delete_elem	= sock_hash_delete_elem,
	.map_lookup_elem	= sock_hash_lookup,
	.map_lookup_elem_sys_only = sock_hash_lookup_sys,
	.map_release_uref	= sock_hash_release_progs,
	.map_check_btf		= map_check_no_btf,
	.map_btf_id		= &sock_hash_map_btf_ids[0],
	.iter_seq_info		= &sock_hash_iter_seq_info,
};

static struct sk_psock_progs *sock_map_progs(struct bpf_map *map)
{
	switch (map->map_type) {
	case BPF_MAP_TYPE_SOCKMAP:
		return &container_of(map, struct bpf_stab, map)->progs;
	case BPF_MAP_TYPE_SOCKHASH:
		return &container_of(map, struct bpf_shtab, map)->progs;
	default:
		break;
	}

	return NULL;
}

static int sock_map_prog_lookup(struct bpf_map *map, struct bpf_prog ***pprog,
				u32 which)
{
	struct sk_psock_progs *progs = sock_map_progs(map);

	if (!progs)
		return -EOPNOTSUPP;

	switch (which) {
	case BPF_SK_MSG_VERDICT:
		*pprog = &progs->msg_parser;
		break;
#if IS_ENABLED(CONFIG_BPF_STREAM_PARSER)
	case BPF_SK_SKB_STREAM_PARSER:
		*pprog = &progs->stream_parser;
		break;
#endif
	case BPF_SK_SKB_STREAM_VERDICT:
		if (progs->skb_verdict)
			return -EBUSY;
		*pprog = &progs->stream_verdict;
		break;
	case BPF_SK_SKB_VERDICT:
		if (progs->stream_verdict)
			return -EBUSY;
		*pprog = &progs->skb_verdict;
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static int sock_map_prog_update(struct bpf_map *map, struct bpf_prog *prog,
				struct bpf_prog *old, u32 which)
{
	struct bpf_prog **pprog;
	int ret;

	ret = sock_map_prog_lookup(map, &pprog, which);
	if (ret)
		return ret;

	if (old)
		return psock_replace_prog(pprog, prog, old);

	psock_set_prog(pprog, prog);
	return 0;
}

int sock_map_bpf_prog_query(const union bpf_attr *attr,
			    union bpf_attr __user *uattr)
{
	__u32 __user *prog_ids = u64_to_user_ptr(attr->query.prog_ids);
	u32 prog_cnt = 0, flags = 0, ufd = attr->target_fd;
	struct bpf_prog **pprog;
	struct bpf_prog *prog;
	struct bpf_map *map;
	struct fd f;
	u32 id = 0;
	int ret;

	if (attr->query.query_flags)
		return -EINVAL;

	f = fdget(ufd);
	map = __bpf_map_get(f);
	if (IS_ERR(map))
		return PTR_ERR(map);

	rcu_read_lock();

	ret = sock_map_prog_lookup(map, &pprog, attr->query.attach_type);
	if (ret)
		goto end;

	prog = *pprog;
	prog_cnt = !prog ? 0 : 1;

	if (!attr->query.prog_cnt || !prog_ids || !prog_cnt)
		goto end;

	/* we do not hold the refcnt, the bpf prog may be released
	 * asynchronously and the id would be set to 0.
	 */
	id = data_race(prog->aux->id);
	if (id == 0)
		prog_cnt = 0;

end:
	rcu_read_unlock();

	if (copy_to_user(&uattr->query.attach_flags, &flags, sizeof(flags)) ||
	    (id != 0 && copy_to_user(prog_ids, &id, sizeof(u32))) ||
	    copy_to_user(&uattr->query.prog_cnt, &prog_cnt, sizeof(prog_cnt)))
		ret = -EFAULT;

	fdput(f);
	return ret;
}

static void sock_map_unlink(struct sock *sk, struct sk_psock_link *link)
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

static void sock_map_remove_links(struct sock *sk, struct sk_psock *psock)
{
	struct sk_psock_link *link;

	while ((link = sk_psock_link_pop(psock))) {
		sock_map_unlink(sk, link);
		sk_psock_free_link(link);
	}
}

void sock_map_unhash(struct sock *sk)
{
	void (*saved_unhash)(struct sock *sk);
	struct sk_psock *psock;

	rcu_read_lock();
	psock = sk_psock(sk);
	if (unlikely(!psock)) {
		rcu_read_unlock();
		saved_unhash = READ_ONCE(sk->sk_prot)->unhash;
	} else {
		saved_unhash = psock->saved_unhash;
		sock_map_remove_links(sk, psock);
		rcu_read_unlock();
	}
	if (WARN_ON_ONCE(saved_unhash == sock_map_unhash))
		return;
	if (saved_unhash)
		saved_unhash(sk);
}
EXPORT_SYMBOL_GPL(sock_map_unhash);

void sock_map_destroy(struct sock *sk)
{
	void (*saved_destroy)(struct sock *sk);
	struct sk_psock *psock;

	rcu_read_lock();
	psock = sk_psock_get(sk);
	if (unlikely(!psock)) {
		rcu_read_unlock();
		saved_destroy = READ_ONCE(sk->sk_prot)->destroy;
	} else {
		saved_destroy = psock->saved_destroy;
		sock_map_remove_links(sk, psock);
		rcu_read_unlock();
		sk_psock_stop(psock);
		sk_psock_put(sk, psock);
	}
	if (WARN_ON_ONCE(saved_destroy == sock_map_destroy))
		return;
	if (saved_destroy)
		saved_destroy(sk);
}
EXPORT_SYMBOL_GPL(sock_map_destroy);

void sock_map_close(struct sock *sk, long timeout)
{
	void (*saved_close)(struct sock *sk, long timeout);
	struct sk_psock *psock;

	lock_sock(sk);
	rcu_read_lock();
	psock = sk_psock_get(sk);
	if (unlikely(!psock)) {
		rcu_read_unlock();
		release_sock(sk);
		saved_close = READ_ONCE(sk->sk_prot)->close;
	} else {
		saved_close = psock->saved_close;
		sock_map_remove_links(sk, psock);
		rcu_read_unlock();
		sk_psock_stop(psock);
		release_sock(sk);
		cancel_delayed_work_sync(&psock->work);
		sk_psock_put(sk, psock);
	}

	/* Make sure we do not recurse. This is a bug.
	 * Leak the socket instead of crashing on a stack overflow.
	 */
	if (WARN_ON_ONCE(saved_close == sock_map_close))
		return;
	saved_close(sk, timeout);
}
EXPORT_SYMBOL_GPL(sock_map_close);

static int sock_map_iter_attach_target(struct bpf_prog *prog,
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

	if (map->map_type != BPF_MAP_TYPE_SOCKMAP &&
	    map->map_type != BPF_MAP_TYPE_SOCKHASH)
		goto put_map;

	if (prog->aux->max_rdonly_access > map->key_size) {
		err = -EACCES;
		goto put_map;
	}

	aux->map = map;
	return 0;

put_map:
	bpf_map_put_with_uref(map);
	return err;
}

static void sock_map_iter_detach_target(struct bpf_iter_aux_info *aux)
{
	bpf_map_put_with_uref(aux->map);
}

static struct bpf_iter_reg sock_map_iter_reg = {
	.target			= "sockmap",
	.attach_target		= sock_map_iter_attach_target,
	.detach_target		= sock_map_iter_detach_target,
	.show_fdinfo		= bpf_iter_map_show_fdinfo,
	.fill_link_info		= bpf_iter_map_fill_link_info,
	.ctx_arg_info_size	= 2,
	.ctx_arg_info		= {
		{ offsetof(struct bpf_iter__sockmap, key),
		  PTR_TO_BUF | PTR_MAYBE_NULL | MEM_RDONLY },
		{ offsetof(struct bpf_iter__sockmap, sk),
		  PTR_TO_BTF_ID_OR_NULL },
	},
};

static int __init bpf_sockmap_iter_init(void)
{
	sock_map_iter_reg.ctx_arg_info[1].btf_id =
		btf_sock_ids[BTF_SOCK_TYPE_SOCK];
	return bpf_iter_reg_target(&sock_map_iter_reg);
}
late_initcall(bpf_sockmap_iter_init);
