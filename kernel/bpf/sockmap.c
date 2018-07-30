/* Copyright (c) 2017 Covalent IO, Inc. http://covalent.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 */

/* A BPF sock_map is used to store sock objects. This is primarly used
 * for doing socket redirect with BPF helper routines.
 *
 * A sock map may have BPF programs attached to it, currently a program
 * used to parse packets and a program to provide a verdict and redirect
 * decision on the packet are supported. Any programs attached to a sock
 * map are inherited by sock objects when they are added to the map. If
 * no BPF programs are attached the sock object may only be used for sock
 * redirect.
 *
 * A sock object may be in multiple maps, but can only inherit a single
 * parse or verdict program. If adding a sock object to a map would result
 * in having multiple parsing programs the update will return an EBUSY error.
 *
 * For reference this program is similar to devmap used in XDP context
 * reviewing these together may be useful. For an example please review
 * ./samples/bpf/sockmap/.
 */
#include <linux/bpf.h>
#include <net/sock.h>
#include <linux/filter.h>
#include <linux/errno.h>
#include <linux/file.h>
#include <linux/kernel.h>
#include <linux/net.h>
#include <linux/skbuff.h>
#include <linux/workqueue.h>
#include <linux/list.h>
#include <linux/mm.h>
#include <net/strparser.h>
#include <net/tcp.h>
#include <linux/ptr_ring.h>
#include <net/inet_common.h>
#include <linux/sched/signal.h>

#define SOCK_CREATE_FLAG_MASK \
	(BPF_F_NUMA_NODE | BPF_F_RDONLY | BPF_F_WRONLY)

struct bpf_sock_progs {
	struct bpf_prog *bpf_tx_msg;
	struct bpf_prog *bpf_parse;
	struct bpf_prog *bpf_verdict;
};

struct bpf_stab {
	struct bpf_map map;
	struct sock **sock_map;
	struct bpf_sock_progs progs;
};

struct bucket {
	struct hlist_head head;
	raw_spinlock_t lock;
};

struct bpf_htab {
	struct bpf_map map;
	struct bucket *buckets;
	atomic_t count;
	u32 n_buckets;
	u32 elem_size;
	struct bpf_sock_progs progs;
	struct rcu_head rcu;
};

struct htab_elem {
	struct rcu_head rcu;
	struct hlist_node hash_node;
	u32 hash;
	struct sock *sk;
	char key[0];
};

enum smap_psock_state {
	SMAP_TX_RUNNING,
};

struct smap_psock_map_entry {
	struct list_head list;
	struct sock **entry;
	struct htab_elem __rcu *hash_link;
	struct bpf_htab __rcu *htab;
};

struct smap_psock {
	struct rcu_head	rcu;
	refcount_t refcnt;

	/* datapath variables */
	struct sk_buff_head rxqueue;
	bool strp_enabled;

	/* datapath error path cache across tx work invocations */
	int save_rem;
	int save_off;
	struct sk_buff *save_skb;

	/* datapath variables for tx_msg ULP */
	struct sock *sk_redir;
	int apply_bytes;
	int cork_bytes;
	int sg_size;
	int eval;
	struct sk_msg_buff *cork;
	struct list_head ingress;

	struct strparser strp;
	struct bpf_prog *bpf_tx_msg;
	struct bpf_prog *bpf_parse;
	struct bpf_prog *bpf_verdict;
	struct list_head maps;
	spinlock_t maps_lock;

	/* Back reference used when sock callback trigger sockmap operations */
	struct sock *sock;
	unsigned long state;

	struct work_struct tx_work;
	struct work_struct gc_work;

	struct proto *sk_proto;
	void (*save_close)(struct sock *sk, long timeout);
	void (*save_data_ready)(struct sock *sk);
	void (*save_write_space)(struct sock *sk);
};

static void smap_release_sock(struct smap_psock *psock, struct sock *sock);
static int bpf_tcp_recvmsg(struct sock *sk, struct msghdr *msg, size_t len,
			   int nonblock, int flags, int *addr_len);
static int bpf_tcp_sendmsg(struct sock *sk, struct msghdr *msg, size_t size);
static int bpf_tcp_sendpage(struct sock *sk, struct page *page,
			    int offset, size_t size, int flags);
static void bpf_tcp_close(struct sock *sk, long timeout);

static inline struct smap_psock *smap_psock_sk(const struct sock *sk)
{
	return rcu_dereference_sk_user_data(sk);
}

static bool bpf_tcp_stream_read(const struct sock *sk)
{
	struct smap_psock *psock;
	bool empty = true;

	rcu_read_lock();
	psock = smap_psock_sk(sk);
	if (unlikely(!psock))
		goto out;
	empty = list_empty(&psock->ingress);
out:
	rcu_read_unlock();
	return !empty;
}

enum {
	SOCKMAP_IPV4,
	SOCKMAP_IPV6,
	SOCKMAP_NUM_PROTS,
};

enum {
	SOCKMAP_BASE,
	SOCKMAP_TX,
	SOCKMAP_NUM_CONFIGS,
};

static struct proto *saved_tcpv6_prot __read_mostly;
static DEFINE_SPINLOCK(tcpv6_prot_lock);
static struct proto bpf_tcp_prots[SOCKMAP_NUM_PROTS][SOCKMAP_NUM_CONFIGS];
static void build_protos(struct proto prot[SOCKMAP_NUM_CONFIGS],
			 struct proto *base)
{
	prot[SOCKMAP_BASE]			= *base;
	prot[SOCKMAP_BASE].close		= bpf_tcp_close;
	prot[SOCKMAP_BASE].recvmsg		= bpf_tcp_recvmsg;
	prot[SOCKMAP_BASE].stream_memory_read	= bpf_tcp_stream_read;

	prot[SOCKMAP_TX]			= prot[SOCKMAP_BASE];
	prot[SOCKMAP_TX].sendmsg		= bpf_tcp_sendmsg;
	prot[SOCKMAP_TX].sendpage		= bpf_tcp_sendpage;
}

static void update_sk_prot(struct sock *sk, struct smap_psock *psock)
{
	int family = sk->sk_family == AF_INET6 ? SOCKMAP_IPV6 : SOCKMAP_IPV4;
	int conf = psock->bpf_tx_msg ? SOCKMAP_TX : SOCKMAP_BASE;

	sk->sk_prot = &bpf_tcp_prots[family][conf];
}

static int bpf_tcp_init(struct sock *sk)
{
	struct smap_psock *psock;

	rcu_read_lock();
	psock = smap_psock_sk(sk);
	if (unlikely(!psock)) {
		rcu_read_unlock();
		return -EINVAL;
	}

	if (unlikely(psock->sk_proto)) {
		rcu_read_unlock();
		return -EBUSY;
	}

	psock->save_close = sk->sk_prot->close;
	psock->sk_proto = sk->sk_prot;

	/* Build IPv6 sockmap whenever the address of tcpv6_prot changes */
	if (sk->sk_family == AF_INET6 &&
	    unlikely(sk->sk_prot != smp_load_acquire(&saved_tcpv6_prot))) {
		spin_lock_bh(&tcpv6_prot_lock);
		if (likely(sk->sk_prot != saved_tcpv6_prot)) {
			build_protos(bpf_tcp_prots[SOCKMAP_IPV6], sk->sk_prot);
			smp_store_release(&saved_tcpv6_prot, sk->sk_prot);
		}
		spin_unlock_bh(&tcpv6_prot_lock);
	}
	update_sk_prot(sk, psock);
	rcu_read_unlock();
	return 0;
}

static void smap_release_sock(struct smap_psock *psock, struct sock *sock);
static int free_start_sg(struct sock *sk, struct sk_msg_buff *md);

static void bpf_tcp_release(struct sock *sk)
{
	struct smap_psock *psock;

	rcu_read_lock();
	psock = smap_psock_sk(sk);
	if (unlikely(!psock))
		goto out;

	if (psock->cork) {
		free_start_sg(psock->sock, psock->cork);
		kfree(psock->cork);
		psock->cork = NULL;
	}

	if (psock->sk_proto) {
		sk->sk_prot = psock->sk_proto;
		psock->sk_proto = NULL;
	}
out:
	rcu_read_unlock();
}

static struct htab_elem *lookup_elem_raw(struct hlist_head *head,
					 u32 hash, void *key, u32 key_size)
{
	struct htab_elem *l;

	hlist_for_each_entry_rcu(l, head, hash_node) {
		if (l->hash == hash && !memcmp(&l->key, key, key_size))
			return l;
	}

	return NULL;
}

static inline struct bucket *__select_bucket(struct bpf_htab *htab, u32 hash)
{
	return &htab->buckets[hash & (htab->n_buckets - 1)];
}

static inline struct hlist_head *select_bucket(struct bpf_htab *htab, u32 hash)
{
	return &__select_bucket(htab, hash)->head;
}

static void free_htab_elem(struct bpf_htab *htab, struct htab_elem *l)
{
	atomic_dec(&htab->count);
	kfree_rcu(l, rcu);
}

static struct smap_psock_map_entry *psock_map_pop(struct sock *sk,
						  struct smap_psock *psock)
{
	struct smap_psock_map_entry *e;

	spin_lock_bh(&psock->maps_lock);
	e = list_first_entry_or_null(&psock->maps,
				     struct smap_psock_map_entry,
				     list);
	if (e)
		list_del(&e->list);
	spin_unlock_bh(&psock->maps_lock);
	return e;
}

static void bpf_tcp_close(struct sock *sk, long timeout)
{
	void (*close_fun)(struct sock *sk, long timeout);
	struct smap_psock_map_entry *e;
	struct sk_msg_buff *md, *mtmp;
	struct smap_psock *psock;
	struct sock *osk;

	lock_sock(sk);
	rcu_read_lock();
	psock = smap_psock_sk(sk);
	if (unlikely(!psock)) {
		rcu_read_unlock();
		release_sock(sk);
		return sk->sk_prot->close(sk, timeout);
	}

	/* The psock may be destroyed anytime after exiting the RCU critial
	 * section so by the time we use close_fun the psock may no longer
	 * be valid. However, bpf_tcp_close is called with the sock lock
	 * held so the close hook and sk are still valid.
	 */
	close_fun = psock->save_close;

	if (psock->cork) {
		free_start_sg(psock->sock, psock->cork);
		kfree(psock->cork);
		psock->cork = NULL;
	}

	list_for_each_entry_safe(md, mtmp, &psock->ingress, list) {
		list_del(&md->list);
		free_start_sg(psock->sock, md);
		kfree(md);
	}

	e = psock_map_pop(sk, psock);
	while (e) {
		if (e->entry) {
			osk = cmpxchg(e->entry, sk, NULL);
			if (osk == sk) {
				smap_release_sock(psock, sk);
			}
		} else {
			struct htab_elem *link = rcu_dereference(e->hash_link);
			struct bpf_htab *htab = rcu_dereference(e->htab);
			struct hlist_head *head;
			struct htab_elem *l;
			struct bucket *b;

			b = __select_bucket(htab, link->hash);
			head = &b->head;
			raw_spin_lock_bh(&b->lock);
			l = lookup_elem_raw(head,
					    link->hash, link->key,
					    htab->map.key_size);
			/* If another thread deleted this object skip deletion.
			 * The refcnt on psock may or may not be zero.
			 */
			if (l) {
				hlist_del_rcu(&link->hash_node);
				smap_release_sock(psock, link->sk);
				free_htab_elem(htab, link);
			}
			raw_spin_unlock_bh(&b->lock);
		}
		e = psock_map_pop(sk, psock);
	}
	rcu_read_unlock();
	release_sock(sk);
	close_fun(sk, timeout);
}

enum __sk_action {
	__SK_DROP = 0,
	__SK_PASS,
	__SK_REDIRECT,
	__SK_NONE,
};

static struct tcp_ulp_ops bpf_tcp_ulp_ops __read_mostly = {
	.name		= "bpf_tcp",
	.uid		= TCP_ULP_BPF,
	.user_visible	= false,
	.owner		= NULL,
	.init		= bpf_tcp_init,
	.release	= bpf_tcp_release,
};

static int memcopy_from_iter(struct sock *sk,
			     struct sk_msg_buff *md,
			     struct iov_iter *from, int bytes)
{
	struct scatterlist *sg = md->sg_data;
	int i = md->sg_curr, rc = -ENOSPC;

	do {
		int copy;
		char *to;

		if (md->sg_copybreak >= sg[i].length) {
			md->sg_copybreak = 0;

			if (++i == MAX_SKB_FRAGS)
				i = 0;

			if (i == md->sg_end)
				break;
		}

		copy = sg[i].length - md->sg_copybreak;
		to = sg_virt(&sg[i]) + md->sg_copybreak;
		md->sg_copybreak += copy;

		if (sk->sk_route_caps & NETIF_F_NOCACHE_COPY)
			rc = copy_from_iter_nocache(to, copy, from);
		else
			rc = copy_from_iter(to, copy, from);

		if (rc != copy) {
			rc = -EFAULT;
			goto out;
		}

		bytes -= copy;
		if (!bytes)
			break;

		md->sg_copybreak = 0;
		if (++i == MAX_SKB_FRAGS)
			i = 0;
	} while (i != md->sg_end);
out:
	md->sg_curr = i;
	return rc;
}

static int bpf_tcp_push(struct sock *sk, int apply_bytes,
			struct sk_msg_buff *md,
			int flags, bool uncharge)
{
	bool apply = apply_bytes;
	struct scatterlist *sg;
	int offset, ret = 0;
	struct page *p;
	size_t size;

	while (1) {
		sg = md->sg_data + md->sg_start;
		size = (apply && apply_bytes < sg->length) ?
			apply_bytes : sg->length;
		offset = sg->offset;

		tcp_rate_check_app_limited(sk);
		p = sg_page(sg);
retry:
		ret = do_tcp_sendpages(sk, p, offset, size, flags);
		if (ret != size) {
			if (ret > 0) {
				if (apply)
					apply_bytes -= ret;

				sg->offset += ret;
				sg->length -= ret;
				size -= ret;
				offset += ret;
				if (uncharge)
					sk_mem_uncharge(sk, ret);
				goto retry;
			}

			return ret;
		}

		if (apply)
			apply_bytes -= ret;
		sg->offset += ret;
		sg->length -= ret;
		if (uncharge)
			sk_mem_uncharge(sk, ret);

		if (!sg->length) {
			put_page(p);
			md->sg_start++;
			if (md->sg_start == MAX_SKB_FRAGS)
				md->sg_start = 0;
			sg_init_table(sg, 1);

			if (md->sg_start == md->sg_end)
				break;
		}

		if (apply && !apply_bytes)
			break;
	}
	return 0;
}

static inline void bpf_compute_data_pointers_sg(struct sk_msg_buff *md)
{
	struct scatterlist *sg = md->sg_data + md->sg_start;

	if (md->sg_copy[md->sg_start]) {
		md->data = md->data_end = 0;
	} else {
		md->data = sg_virt(sg);
		md->data_end = md->data + sg->length;
	}
}

static void return_mem_sg(struct sock *sk, int bytes, struct sk_msg_buff *md)
{
	struct scatterlist *sg = md->sg_data;
	int i = md->sg_start;

	do {
		int uncharge = (bytes < sg[i].length) ? bytes : sg[i].length;

		sk_mem_uncharge(sk, uncharge);
		bytes -= uncharge;
		if (!bytes)
			break;
		i++;
		if (i == MAX_SKB_FRAGS)
			i = 0;
	} while (i != md->sg_end);
}

static void free_bytes_sg(struct sock *sk, int bytes,
			  struct sk_msg_buff *md, bool charge)
{
	struct scatterlist *sg = md->sg_data;
	int i = md->sg_start, free;

	while (bytes && sg[i].length) {
		free = sg[i].length;
		if (bytes < free) {
			sg[i].length -= bytes;
			sg[i].offset += bytes;
			if (charge)
				sk_mem_uncharge(sk, bytes);
			break;
		}

		if (charge)
			sk_mem_uncharge(sk, sg[i].length);
		put_page(sg_page(&sg[i]));
		bytes -= sg[i].length;
		sg[i].length = 0;
		sg[i].page_link = 0;
		sg[i].offset = 0;
		i++;

		if (i == MAX_SKB_FRAGS)
			i = 0;
	}
	md->sg_start = i;
}

static int free_sg(struct sock *sk, int start, struct sk_msg_buff *md)
{
	struct scatterlist *sg = md->sg_data;
	int i = start, free = 0;

	while (sg[i].length) {
		free += sg[i].length;
		sk_mem_uncharge(sk, sg[i].length);
		if (!md->skb)
			put_page(sg_page(&sg[i]));
		sg[i].length = 0;
		sg[i].page_link = 0;
		sg[i].offset = 0;
		i++;

		if (i == MAX_SKB_FRAGS)
			i = 0;
	}
	if (md->skb)
		consume_skb(md->skb);

	return free;
}

static int free_start_sg(struct sock *sk, struct sk_msg_buff *md)
{
	int free = free_sg(sk, md->sg_start, md);

	md->sg_start = md->sg_end;
	return free;
}

static int free_curr_sg(struct sock *sk, struct sk_msg_buff *md)
{
	return free_sg(sk, md->sg_curr, md);
}

static int bpf_map_msg_verdict(int _rc, struct sk_msg_buff *md)
{
	return ((_rc == SK_PASS) ?
	       (md->sk_redir ? __SK_REDIRECT : __SK_PASS) :
	       __SK_DROP);
}

static unsigned int smap_do_tx_msg(struct sock *sk,
				   struct smap_psock *psock,
				   struct sk_msg_buff *md)
{
	struct bpf_prog *prog;
	unsigned int rc, _rc;

	preempt_disable();
	rcu_read_lock();

	/* If the policy was removed mid-send then default to 'accept' */
	prog = READ_ONCE(psock->bpf_tx_msg);
	if (unlikely(!prog)) {
		_rc = SK_PASS;
		goto verdict;
	}

	bpf_compute_data_pointers_sg(md);
	md->sk = sk;
	rc = (*prog->bpf_func)(md, prog->insnsi);
	psock->apply_bytes = md->apply_bytes;

	/* Moving return codes from UAPI namespace into internal namespace */
	_rc = bpf_map_msg_verdict(rc, md);

	/* The psock has a refcount on the sock but not on the map and because
	 * we need to drop rcu read lock here its possible the map could be
	 * removed between here and when we need it to execute the sock
	 * redirect. So do the map lookup now for future use.
	 */
	if (_rc == __SK_REDIRECT) {
		if (psock->sk_redir)
			sock_put(psock->sk_redir);
		psock->sk_redir = do_msg_redirect_map(md);
		if (!psock->sk_redir) {
			_rc = __SK_DROP;
			goto verdict;
		}
		sock_hold(psock->sk_redir);
	}
verdict:
	rcu_read_unlock();
	preempt_enable();

	return _rc;
}

static int bpf_tcp_ingress(struct sock *sk, int apply_bytes,
			   struct smap_psock *psock,
			   struct sk_msg_buff *md, int flags)
{
	bool apply = apply_bytes;
	size_t size, copied = 0;
	struct sk_msg_buff *r;
	int err = 0, i;

	r = kzalloc(sizeof(struct sk_msg_buff), __GFP_NOWARN | GFP_KERNEL);
	if (unlikely(!r))
		return -ENOMEM;

	lock_sock(sk);
	r->sg_start = md->sg_start;
	i = md->sg_start;

	do {
		size = (apply && apply_bytes < md->sg_data[i].length) ?
			apply_bytes : md->sg_data[i].length;

		if (!sk_wmem_schedule(sk, size)) {
			if (!copied)
				err = -ENOMEM;
			break;
		}

		sk_mem_charge(sk, size);
		r->sg_data[i] = md->sg_data[i];
		r->sg_data[i].length = size;
		md->sg_data[i].length -= size;
		md->sg_data[i].offset += size;
		copied += size;

		if (md->sg_data[i].length) {
			get_page(sg_page(&r->sg_data[i]));
			r->sg_end = (i + 1) == MAX_SKB_FRAGS ? 0 : i + 1;
		} else {
			i++;
			if (i == MAX_SKB_FRAGS)
				i = 0;
			r->sg_end = i;
		}

		if (apply) {
			apply_bytes -= size;
			if (!apply_bytes)
				break;
		}
	} while (i != md->sg_end);

	md->sg_start = i;

	if (!err) {
		list_add_tail(&r->list, &psock->ingress);
		sk->sk_data_ready(sk);
	} else {
		free_start_sg(sk, r);
		kfree(r);
	}

	release_sock(sk);
	return err;
}

static int bpf_tcp_sendmsg_do_redirect(struct sock *sk, int send,
				       struct sk_msg_buff *md,
				       int flags)
{
	bool ingress = !!(md->flags & BPF_F_INGRESS);
	struct smap_psock *psock;
	struct scatterlist *sg;
	int err = 0;

	sg = md->sg_data;

	rcu_read_lock();
	psock = smap_psock_sk(sk);
	if (unlikely(!psock))
		goto out_rcu;

	if (!refcount_inc_not_zero(&psock->refcnt))
		goto out_rcu;

	rcu_read_unlock();

	if (ingress) {
		err = bpf_tcp_ingress(sk, send, psock, md, flags);
	} else {
		lock_sock(sk);
		err = bpf_tcp_push(sk, send, md, flags, false);
		release_sock(sk);
	}
	smap_release_sock(psock, sk);
	if (unlikely(err))
		goto out;
	return 0;
out_rcu:
	rcu_read_unlock();
out:
	free_bytes_sg(NULL, send, md, false);
	return err;
}

static inline void bpf_md_init(struct smap_psock *psock)
{
	if (!psock->apply_bytes) {
		psock->eval =  __SK_NONE;
		if (psock->sk_redir) {
			sock_put(psock->sk_redir);
			psock->sk_redir = NULL;
		}
	}
}

static void apply_bytes_dec(struct smap_psock *psock, int i)
{
	if (psock->apply_bytes) {
		if (psock->apply_bytes < i)
			psock->apply_bytes = 0;
		else
			psock->apply_bytes -= i;
	}
}

static int bpf_exec_tx_verdict(struct smap_psock *psock,
			       struct sk_msg_buff *m,
			       struct sock *sk,
			       int *copied, int flags)
{
	bool cork = false, enospc = (m->sg_start == m->sg_end);
	struct sock *redir;
	int err = 0;
	int send;

more_data:
	if (psock->eval == __SK_NONE)
		psock->eval = smap_do_tx_msg(sk, psock, m);

	if (m->cork_bytes &&
	    m->cork_bytes > psock->sg_size && !enospc) {
		psock->cork_bytes = m->cork_bytes - psock->sg_size;
		if (!psock->cork) {
			psock->cork = kcalloc(1,
					sizeof(struct sk_msg_buff),
					GFP_ATOMIC | __GFP_NOWARN);

			if (!psock->cork) {
				err = -ENOMEM;
				goto out_err;
			}
		}
		memcpy(psock->cork, m, sizeof(*m));
		goto out_err;
	}

	send = psock->sg_size;
	if (psock->apply_bytes && psock->apply_bytes < send)
		send = psock->apply_bytes;

	switch (psock->eval) {
	case __SK_PASS:
		err = bpf_tcp_push(sk, send, m, flags, true);
		if (unlikely(err)) {
			*copied -= free_start_sg(sk, m);
			break;
		}

		apply_bytes_dec(psock, send);
		psock->sg_size -= send;
		break;
	case __SK_REDIRECT:
		redir = psock->sk_redir;
		apply_bytes_dec(psock, send);

		if (psock->cork) {
			cork = true;
			psock->cork = NULL;
		}

		return_mem_sg(sk, send, m);
		release_sock(sk);

		err = bpf_tcp_sendmsg_do_redirect(redir, send, m, flags);
		lock_sock(sk);

		if (unlikely(err < 0)) {
			free_start_sg(sk, m);
			psock->sg_size = 0;
			if (!cork)
				*copied -= send;
		} else {
			psock->sg_size -= send;
		}

		if (cork) {
			free_start_sg(sk, m);
			psock->sg_size = 0;
			kfree(m);
			m = NULL;
			err = 0;
		}
		break;
	case __SK_DROP:
	default:
		free_bytes_sg(sk, send, m, true);
		apply_bytes_dec(psock, send);
		*copied -= send;
		psock->sg_size -= send;
		err = -EACCES;
		break;
	}

	if (likely(!err)) {
		bpf_md_init(psock);
		if (m &&
		    m->sg_data[m->sg_start].page_link &&
		    m->sg_data[m->sg_start].length)
			goto more_data;
	}

out_err:
	return err;
}

static int bpf_wait_data(struct sock *sk,
			 struct smap_psock *psk, int flags,
			 long timeo, int *err)
{
	int rc;

	DEFINE_WAIT_FUNC(wait, woken_wake_function);

	add_wait_queue(sk_sleep(sk), &wait);
	sk_set_bit(SOCKWQ_ASYNC_WAITDATA, sk);
	rc = sk_wait_event(sk, &timeo,
			   !list_empty(&psk->ingress) ||
			   !skb_queue_empty(&sk->sk_receive_queue),
			   &wait);
	sk_clear_bit(SOCKWQ_ASYNC_WAITDATA, sk);
	remove_wait_queue(sk_sleep(sk), &wait);

	return rc;
}

static int bpf_tcp_recvmsg(struct sock *sk, struct msghdr *msg, size_t len,
			   int nonblock, int flags, int *addr_len)
{
	struct iov_iter *iter = &msg->msg_iter;
	struct smap_psock *psock;
	int copied = 0;

	if (unlikely(flags & MSG_ERRQUEUE))
		return inet_recv_error(sk, msg, len, addr_len);

	rcu_read_lock();
	psock = smap_psock_sk(sk);
	if (unlikely(!psock))
		goto out;

	if (unlikely(!refcount_inc_not_zero(&psock->refcnt)))
		goto out;
	rcu_read_unlock();

	if (!skb_queue_empty(&sk->sk_receive_queue))
		return tcp_recvmsg(sk, msg, len, nonblock, flags, addr_len);

	lock_sock(sk);
bytes_ready:
	while (copied != len) {
		struct scatterlist *sg;
		struct sk_msg_buff *md;
		int i;

		md = list_first_entry_or_null(&psock->ingress,
					      struct sk_msg_buff, list);
		if (unlikely(!md))
			break;
		i = md->sg_start;
		do {
			struct page *page;
			int n, copy;

			sg = &md->sg_data[i];
			copy = sg->length;
			page = sg_page(sg);

			if (copied + copy > len)
				copy = len - copied;

			n = copy_page_to_iter(page, sg->offset, copy, iter);
			if (n != copy) {
				md->sg_start = i;
				release_sock(sk);
				smap_release_sock(psock, sk);
				return -EFAULT;
			}

			copied += copy;
			sg->offset += copy;
			sg->length -= copy;
			sk_mem_uncharge(sk, copy);

			if (!sg->length) {
				i++;
				if (i == MAX_SKB_FRAGS)
					i = 0;
				if (!md->skb)
					put_page(page);
			}
			if (copied == len)
				break;
		} while (i != md->sg_end);
		md->sg_start = i;

		if (!sg->length && md->sg_start == md->sg_end) {
			list_del(&md->list);
			if (md->skb)
				consume_skb(md->skb);
			kfree(md);
		}
	}

	if (!copied) {
		long timeo;
		int data;
		int err = 0;

		timeo = sock_rcvtimeo(sk, nonblock);
		data = bpf_wait_data(sk, psock, flags, timeo, &err);

		if (data) {
			if (!skb_queue_empty(&sk->sk_receive_queue)) {
				release_sock(sk);
				smap_release_sock(psock, sk);
				copied = tcp_recvmsg(sk, msg, len, nonblock, flags, addr_len);
				return copied;
			}
			goto bytes_ready;
		}

		if (err)
			copied = err;
	}

	release_sock(sk);
	smap_release_sock(psock, sk);
	return copied;
out:
	rcu_read_unlock();
	return tcp_recvmsg(sk, msg, len, nonblock, flags, addr_len);
}


static int bpf_tcp_sendmsg(struct sock *sk, struct msghdr *msg, size_t size)
{
	int flags = msg->msg_flags | MSG_NO_SHARED_FRAGS;
	struct sk_msg_buff md = {0};
	unsigned int sg_copy = 0;
	struct smap_psock *psock;
	int copied = 0, err = 0;
	struct scatterlist *sg;
	long timeo;

	/* Its possible a sock event or user removed the psock _but_ the ops
	 * have not been reprogrammed yet so we get here. In this case fallback
	 * to tcp_sendmsg. Note this only works because we _only_ ever allow
	 * a single ULP there is no hierarchy here.
	 */
	rcu_read_lock();
	psock = smap_psock_sk(sk);
	if (unlikely(!psock)) {
		rcu_read_unlock();
		return tcp_sendmsg(sk, msg, size);
	}

	/* Increment the psock refcnt to ensure its not released while sending a
	 * message. Required because sk lookup and bpf programs are used in
	 * separate rcu critical sections. Its OK if we lose the map entry
	 * but we can't lose the sock reference.
	 */
	if (!refcount_inc_not_zero(&psock->refcnt)) {
		rcu_read_unlock();
		return tcp_sendmsg(sk, msg, size);
	}

	sg = md.sg_data;
	sg_init_marker(sg, MAX_SKB_FRAGS);
	rcu_read_unlock();

	lock_sock(sk);
	timeo = sock_sndtimeo(sk, msg->msg_flags & MSG_DONTWAIT);

	while (msg_data_left(msg)) {
		struct sk_msg_buff *m;
		bool enospc = false;
		int copy;

		if (sk->sk_err) {
			err = sk->sk_err;
			goto out_err;
		}

		copy = msg_data_left(msg);
		if (!sk_stream_memory_free(sk))
			goto wait_for_sndbuf;

		m = psock->cork_bytes ? psock->cork : &md;
		m->sg_curr = m->sg_copybreak ? m->sg_curr : m->sg_end;
		err = sk_alloc_sg(sk, copy, m->sg_data,
				  m->sg_start, &m->sg_end, &sg_copy,
				  m->sg_end - 1);
		if (err) {
			if (err != -ENOSPC)
				goto wait_for_memory;
			enospc = true;
			copy = sg_copy;
		}

		err = memcopy_from_iter(sk, m, &msg->msg_iter, copy);
		if (err < 0) {
			free_curr_sg(sk, m);
			goto out_err;
		}

		psock->sg_size += copy;
		copied += copy;
		sg_copy = 0;

		/* When bytes are being corked skip running BPF program and
		 * applying verdict unless there is no more buffer space. In
		 * the ENOSPC case simply run BPF prorgram with currently
		 * accumulated data. We don't have much choice at this point
		 * we could try extending the page frags or chaining complex
		 * frags but even in these cases _eventually_ we will hit an
		 * OOM scenario. More complex recovery schemes may be
		 * implemented in the future, but BPF programs must handle
		 * the case where apply_cork requests are not honored. The
		 * canonical method to verify this is to check data length.
		 */
		if (psock->cork_bytes) {
			if (copy > psock->cork_bytes)
				psock->cork_bytes = 0;
			else
				psock->cork_bytes -= copy;

			if (psock->cork_bytes && !enospc)
				goto out_cork;

			/* All cork bytes accounted for re-run filter */
			psock->eval = __SK_NONE;
			psock->cork_bytes = 0;
		}

		err = bpf_exec_tx_verdict(psock, m, sk, &copied, flags);
		if (unlikely(err < 0))
			goto out_err;
		continue;
wait_for_sndbuf:
		set_bit(SOCK_NOSPACE, &sk->sk_socket->flags);
wait_for_memory:
		err = sk_stream_wait_memory(sk, &timeo);
		if (err)
			goto out_err;
	}
out_err:
	if (err < 0)
		err = sk_stream_error(sk, msg->msg_flags, err);
out_cork:
	release_sock(sk);
	smap_release_sock(psock, sk);
	return copied ? copied : err;
}

static int bpf_tcp_sendpage(struct sock *sk, struct page *page,
			    int offset, size_t size, int flags)
{
	struct sk_msg_buff md = {0}, *m = NULL;
	int err = 0, copied = 0;
	struct smap_psock *psock;
	struct scatterlist *sg;
	bool enospc = false;

	rcu_read_lock();
	psock = smap_psock_sk(sk);
	if (unlikely(!psock))
		goto accept;

	if (!refcount_inc_not_zero(&psock->refcnt))
		goto accept;
	rcu_read_unlock();

	lock_sock(sk);

	if (psock->cork_bytes) {
		m = psock->cork;
		sg = &m->sg_data[m->sg_end];
	} else {
		m = &md;
		sg = m->sg_data;
		sg_init_marker(sg, MAX_SKB_FRAGS);
	}

	/* Catch case where ring is full and sendpage is stalled. */
	if (unlikely(m->sg_end == m->sg_start &&
	    m->sg_data[m->sg_end].length))
		goto out_err;

	psock->sg_size += size;
	sg_set_page(sg, page, size, offset);
	get_page(page);
	m->sg_copy[m->sg_end] = true;
	sk_mem_charge(sk, size);
	m->sg_end++;
	copied = size;

	if (m->sg_end == MAX_SKB_FRAGS)
		m->sg_end = 0;

	if (m->sg_end == m->sg_start)
		enospc = true;

	if (psock->cork_bytes) {
		if (size > psock->cork_bytes)
			psock->cork_bytes = 0;
		else
			psock->cork_bytes -= size;

		if (psock->cork_bytes && !enospc)
			goto out_err;

		/* All cork bytes accounted for re-run filter */
		psock->eval = __SK_NONE;
		psock->cork_bytes = 0;
	}

	err = bpf_exec_tx_verdict(psock, m, sk, &copied, flags);
out_err:
	release_sock(sk);
	smap_release_sock(psock, sk);
	return copied ? copied : err;
accept:
	rcu_read_unlock();
	return tcp_sendpage(sk, page, offset, size, flags);
}

static void bpf_tcp_msg_add(struct smap_psock *psock,
			    struct sock *sk,
			    struct bpf_prog *tx_msg)
{
	struct bpf_prog *orig_tx_msg;

	orig_tx_msg = xchg(&psock->bpf_tx_msg, tx_msg);
	if (orig_tx_msg)
		bpf_prog_put(orig_tx_msg);
}

static int bpf_tcp_ulp_register(void)
{
	build_protos(bpf_tcp_prots[SOCKMAP_IPV4], &tcp_prot);
	/* Once BPF TX ULP is registered it is never unregistered. It
	 * will be in the ULP list for the lifetime of the system. Doing
	 * duplicate registers is not a problem.
	 */
	return tcp_register_ulp(&bpf_tcp_ulp_ops);
}

static int smap_verdict_func(struct smap_psock *psock, struct sk_buff *skb)
{
	struct bpf_prog *prog = READ_ONCE(psock->bpf_verdict);
	int rc;

	if (unlikely(!prog))
		return __SK_DROP;

	skb_orphan(skb);
	/* We need to ensure that BPF metadata for maps is also cleared
	 * when we orphan the skb so that we don't have the possibility
	 * to reference a stale map.
	 */
	TCP_SKB_CB(skb)->bpf.sk_redir = NULL;
	skb->sk = psock->sock;
	bpf_compute_data_end_sk_skb(skb);
	preempt_disable();
	rc = (*prog->bpf_func)(skb, prog->insnsi);
	preempt_enable();
	skb->sk = NULL;

	/* Moving return codes from UAPI namespace into internal namespace */
	return rc == SK_PASS ?
		(TCP_SKB_CB(skb)->bpf.sk_redir ? __SK_REDIRECT : __SK_PASS) :
		__SK_DROP;
}

static int smap_do_ingress(struct smap_psock *psock, struct sk_buff *skb)
{
	struct sock *sk = psock->sock;
	int copied = 0, num_sg;
	struct sk_msg_buff *r;

	r = kzalloc(sizeof(struct sk_msg_buff), __GFP_NOWARN | GFP_ATOMIC);
	if (unlikely(!r))
		return -EAGAIN;

	if (!sk_rmem_schedule(sk, skb, skb->len)) {
		kfree(r);
		return -EAGAIN;
	}

	sg_init_table(r->sg_data, MAX_SKB_FRAGS);
	num_sg = skb_to_sgvec(skb, r->sg_data, 0, skb->len);
	if (unlikely(num_sg < 0)) {
		kfree(r);
		return num_sg;
	}
	sk_mem_charge(sk, skb->len);
	copied = skb->len;
	r->sg_start = 0;
	r->sg_end = num_sg == MAX_SKB_FRAGS ? 0 : num_sg;
	r->skb = skb;
	list_add_tail(&r->list, &psock->ingress);
	sk->sk_data_ready(sk);
	return copied;
}

static void smap_do_verdict(struct smap_psock *psock, struct sk_buff *skb)
{
	struct smap_psock *peer;
	struct sock *sk;
	__u32 in;
	int rc;

	rc = smap_verdict_func(psock, skb);
	switch (rc) {
	case __SK_REDIRECT:
		sk = do_sk_redirect_map(skb);
		if (!sk) {
			kfree_skb(skb);
			break;
		}

		peer = smap_psock_sk(sk);
		in = (TCP_SKB_CB(skb)->bpf.flags) & BPF_F_INGRESS;

		if (unlikely(!peer || sock_flag(sk, SOCK_DEAD) ||
			     !test_bit(SMAP_TX_RUNNING, &peer->state))) {
			kfree_skb(skb);
			break;
		}

		if (!in && sock_writeable(sk)) {
			skb_set_owner_w(skb, sk);
			skb_queue_tail(&peer->rxqueue, skb);
			schedule_work(&peer->tx_work);
			break;
		} else if (in &&
			   atomic_read(&sk->sk_rmem_alloc) <= sk->sk_rcvbuf) {
			skb_queue_tail(&peer->rxqueue, skb);
			schedule_work(&peer->tx_work);
			break;
		}
	/* Fall through and free skb otherwise */
	case __SK_DROP:
	default:
		kfree_skb(skb);
	}
}

static void smap_report_sk_error(struct smap_psock *psock, int err)
{
	struct sock *sk = psock->sock;

	sk->sk_err = err;
	sk->sk_error_report(sk);
}

static void smap_read_sock_strparser(struct strparser *strp,
				     struct sk_buff *skb)
{
	struct smap_psock *psock;

	rcu_read_lock();
	psock = container_of(strp, struct smap_psock, strp);
	smap_do_verdict(psock, skb);
	rcu_read_unlock();
}

/* Called with lock held on socket */
static void smap_data_ready(struct sock *sk)
{
	struct smap_psock *psock;

	rcu_read_lock();
	psock = smap_psock_sk(sk);
	if (likely(psock)) {
		write_lock_bh(&sk->sk_callback_lock);
		strp_data_ready(&psock->strp);
		write_unlock_bh(&sk->sk_callback_lock);
	}
	rcu_read_unlock();
}

static void smap_tx_work(struct work_struct *w)
{
	struct smap_psock *psock;
	struct sk_buff *skb;
	int rem, off, n;

	psock = container_of(w, struct smap_psock, tx_work);

	/* lock sock to avoid losing sk_socket at some point during loop */
	lock_sock(psock->sock);
	if (psock->save_skb) {
		skb = psock->save_skb;
		rem = psock->save_rem;
		off = psock->save_off;
		psock->save_skb = NULL;
		goto start;
	}

	while ((skb = skb_dequeue(&psock->rxqueue))) {
		__u32 flags;

		rem = skb->len;
		off = 0;
start:
		flags = (TCP_SKB_CB(skb)->bpf.flags) & BPF_F_INGRESS;
		do {
			if (likely(psock->sock->sk_socket)) {
				if (flags)
					n = smap_do_ingress(psock, skb);
				else
					n = skb_send_sock_locked(psock->sock,
								 skb, off, rem);
			} else {
				n = -EINVAL;
			}

			if (n <= 0) {
				if (n == -EAGAIN) {
					/* Retry when space is available */
					psock->save_skb = skb;
					psock->save_rem = rem;
					psock->save_off = off;
					goto out;
				}
				/* Hard errors break pipe and stop xmit */
				smap_report_sk_error(psock, n ? -n : EPIPE);
				clear_bit(SMAP_TX_RUNNING, &psock->state);
				kfree_skb(skb);
				goto out;
			}
			rem -= n;
			off += n;
		} while (rem);

		if (!flags)
			kfree_skb(skb);
	}
out:
	release_sock(psock->sock);
}

static void smap_write_space(struct sock *sk)
{
	struct smap_psock *psock;

	rcu_read_lock();
	psock = smap_psock_sk(sk);
	if (likely(psock && test_bit(SMAP_TX_RUNNING, &psock->state)))
		schedule_work(&psock->tx_work);
	rcu_read_unlock();
}

static void smap_stop_sock(struct smap_psock *psock, struct sock *sk)
{
	if (!psock->strp_enabled)
		return;
	sk->sk_data_ready = psock->save_data_ready;
	sk->sk_write_space = psock->save_write_space;
	psock->save_data_ready = NULL;
	psock->save_write_space = NULL;
	strp_stop(&psock->strp);
	psock->strp_enabled = false;
}

static void smap_destroy_psock(struct rcu_head *rcu)
{
	struct smap_psock *psock = container_of(rcu,
						  struct smap_psock, rcu);

	/* Now that a grace period has passed there is no longer
	 * any reference to this sock in the sockmap so we can
	 * destroy the psock, strparser, and bpf programs. But,
	 * because we use workqueue sync operations we can not
	 * do it in rcu context
	 */
	schedule_work(&psock->gc_work);
}

static void smap_release_sock(struct smap_psock *psock, struct sock *sock)
{
	if (refcount_dec_and_test(&psock->refcnt)) {
		tcp_cleanup_ulp(sock);
		write_lock_bh(&sock->sk_callback_lock);
		smap_stop_sock(psock, sock);
		write_unlock_bh(&sock->sk_callback_lock);
		clear_bit(SMAP_TX_RUNNING, &psock->state);
		rcu_assign_sk_user_data(sock, NULL);
		call_rcu_sched(&psock->rcu, smap_destroy_psock);
	}
}

static int smap_parse_func_strparser(struct strparser *strp,
				       struct sk_buff *skb)
{
	struct smap_psock *psock;
	struct bpf_prog *prog;
	int rc;

	rcu_read_lock();
	psock = container_of(strp, struct smap_psock, strp);
	prog = READ_ONCE(psock->bpf_parse);

	if (unlikely(!prog)) {
		rcu_read_unlock();
		return skb->len;
	}

	/* Attach socket for bpf program to use if needed we can do this
	 * because strparser clones the skb before handing it to a upper
	 * layer, meaning skb_orphan has been called. We NULL sk on the
	 * way out to ensure we don't trigger a BUG_ON in skb/sk operations
	 * later and because we are not charging the memory of this skb to
	 * any socket yet.
	 */
	skb->sk = psock->sock;
	bpf_compute_data_end_sk_skb(skb);
	rc = (*prog->bpf_func)(skb, prog->insnsi);
	skb->sk = NULL;
	rcu_read_unlock();
	return rc;
}

static int smap_read_sock_done(struct strparser *strp, int err)
{
	return err;
}

static int smap_init_sock(struct smap_psock *psock,
			  struct sock *sk)
{
	static const struct strp_callbacks cb = {
		.rcv_msg = smap_read_sock_strparser,
		.parse_msg = smap_parse_func_strparser,
		.read_sock_done = smap_read_sock_done,
	};

	return strp_init(&psock->strp, sk, &cb);
}

static void smap_init_progs(struct smap_psock *psock,
			    struct bpf_prog *verdict,
			    struct bpf_prog *parse)
{
	struct bpf_prog *orig_parse, *orig_verdict;

	orig_parse = xchg(&psock->bpf_parse, parse);
	orig_verdict = xchg(&psock->bpf_verdict, verdict);

	if (orig_verdict)
		bpf_prog_put(orig_verdict);
	if (orig_parse)
		bpf_prog_put(orig_parse);
}

static void smap_start_sock(struct smap_psock *psock, struct sock *sk)
{
	if (sk->sk_data_ready == smap_data_ready)
		return;
	psock->save_data_ready = sk->sk_data_ready;
	psock->save_write_space = sk->sk_write_space;
	sk->sk_data_ready = smap_data_ready;
	sk->sk_write_space = smap_write_space;
	psock->strp_enabled = true;
}

static void sock_map_remove_complete(struct bpf_stab *stab)
{
	bpf_map_area_free(stab->sock_map);
	kfree(stab);
}

static void smap_gc_work(struct work_struct *w)
{
	struct smap_psock_map_entry *e, *tmp;
	struct sk_msg_buff *md, *mtmp;
	struct smap_psock *psock;

	psock = container_of(w, struct smap_psock, gc_work);

	/* no callback lock needed because we already detached sockmap ops */
	if (psock->strp_enabled)
		strp_done(&psock->strp);

	cancel_work_sync(&psock->tx_work);
	__skb_queue_purge(&psock->rxqueue);

	/* At this point all strparser and xmit work must be complete */
	if (psock->bpf_parse)
		bpf_prog_put(psock->bpf_parse);
	if (psock->bpf_verdict)
		bpf_prog_put(psock->bpf_verdict);
	if (psock->bpf_tx_msg)
		bpf_prog_put(psock->bpf_tx_msg);

	if (psock->cork) {
		free_start_sg(psock->sock, psock->cork);
		kfree(psock->cork);
	}

	list_for_each_entry_safe(md, mtmp, &psock->ingress, list) {
		list_del(&md->list);
		free_start_sg(psock->sock, md);
		kfree(md);
	}

	list_for_each_entry_safe(e, tmp, &psock->maps, list) {
		list_del(&e->list);
		kfree(e);
	}

	if (psock->sk_redir)
		sock_put(psock->sk_redir);

	sock_put(psock->sock);
	kfree(psock);
}

static struct smap_psock *smap_init_psock(struct sock *sock, int node)
{
	struct smap_psock *psock;

	psock = kzalloc_node(sizeof(struct smap_psock),
			     GFP_ATOMIC | __GFP_NOWARN,
			     node);
	if (!psock)
		return ERR_PTR(-ENOMEM);

	psock->eval =  __SK_NONE;
	psock->sock = sock;
	skb_queue_head_init(&psock->rxqueue);
	INIT_WORK(&psock->tx_work, smap_tx_work);
	INIT_WORK(&psock->gc_work, smap_gc_work);
	INIT_LIST_HEAD(&psock->maps);
	INIT_LIST_HEAD(&psock->ingress);
	refcount_set(&psock->refcnt, 1);
	spin_lock_init(&psock->maps_lock);

	rcu_assign_sk_user_data(sock, psock);
	sock_hold(sock);
	return psock;
}

static struct bpf_map *sock_map_alloc(union bpf_attr *attr)
{
	struct bpf_stab *stab;
	u64 cost;
	int err;

	if (!capable(CAP_NET_ADMIN))
		return ERR_PTR(-EPERM);

	/* check sanity of attributes */
	if (attr->max_entries == 0 || attr->key_size != 4 ||
	    attr->value_size != 4 || attr->map_flags & ~SOCK_CREATE_FLAG_MASK)
		return ERR_PTR(-EINVAL);

	err = bpf_tcp_ulp_register();
	if (err && err != -EEXIST)
		return ERR_PTR(err);

	stab = kzalloc(sizeof(*stab), GFP_USER);
	if (!stab)
		return ERR_PTR(-ENOMEM);

	bpf_map_init_from_attr(&stab->map, attr);

	/* make sure page count doesn't overflow */
	cost = (u64) stab->map.max_entries * sizeof(struct sock *);
	err = -EINVAL;
	if (cost >= U32_MAX - PAGE_SIZE)
		goto free_stab;

	stab->map.pages = round_up(cost, PAGE_SIZE) >> PAGE_SHIFT;

	/* if map size is larger than memlock limit, reject it early */
	err = bpf_map_precharge_memlock(stab->map.pages);
	if (err)
		goto free_stab;

	err = -ENOMEM;
	stab->sock_map = bpf_map_area_alloc(stab->map.max_entries *
					    sizeof(struct sock *),
					    stab->map.numa_node);
	if (!stab->sock_map)
		goto free_stab;

	return &stab->map;
free_stab:
	kfree(stab);
	return ERR_PTR(err);
}

static void smap_list_map_remove(struct smap_psock *psock,
				 struct sock **entry)
{
	struct smap_psock_map_entry *e, *tmp;

	spin_lock_bh(&psock->maps_lock);
	list_for_each_entry_safe(e, tmp, &psock->maps, list) {
		if (e->entry == entry)
			list_del(&e->list);
	}
	spin_unlock_bh(&psock->maps_lock);
}

static void smap_list_hash_remove(struct smap_psock *psock,
				  struct htab_elem *hash_link)
{
	struct smap_psock_map_entry *e, *tmp;

	spin_lock_bh(&psock->maps_lock);
	list_for_each_entry_safe(e, tmp, &psock->maps, list) {
		struct htab_elem *c = rcu_dereference(e->hash_link);

		if (c == hash_link)
			list_del(&e->list);
	}
	spin_unlock_bh(&psock->maps_lock);
}

static void sock_map_free(struct bpf_map *map)
{
	struct bpf_stab *stab = container_of(map, struct bpf_stab, map);
	int i;

	synchronize_rcu();

	/* At this point no update, lookup or delete operations can happen.
	 * However, be aware we can still get a socket state event updates,
	 * and data ready callabacks that reference the psock from sk_user_data
	 * Also psock worker threads are still in-flight. So smap_release_sock
	 * will only free the psock after cancel_sync on the worker threads
	 * and a grace period expire to ensure psock is really safe to remove.
	 */
	rcu_read_lock();
	for (i = 0; i < stab->map.max_entries; i++) {
		struct smap_psock *psock;
		struct sock *sock;

		sock = xchg(&stab->sock_map[i], NULL);
		if (!sock)
			continue;

		psock = smap_psock_sk(sock);
		/* This check handles a racing sock event that can get the
		 * sk_callback_lock before this case but after xchg happens
		 * causing the refcnt to hit zero and sock user data (psock)
		 * to be null and queued for garbage collection.
		 */
		if (likely(psock)) {
			smap_list_map_remove(psock, &stab->sock_map[i]);
			smap_release_sock(psock, sock);
		}
	}
	rcu_read_unlock();

	sock_map_remove_complete(stab);
}

static int sock_map_get_next_key(struct bpf_map *map, void *key, void *next_key)
{
	struct bpf_stab *stab = container_of(map, struct bpf_stab, map);
	u32 i = key ? *(u32 *)key : U32_MAX;
	u32 *next = (u32 *)next_key;

	if (i >= stab->map.max_entries) {
		*next = 0;
		return 0;
	}

	if (i == stab->map.max_entries - 1)
		return -ENOENT;

	*next = i + 1;
	return 0;
}

struct sock  *__sock_map_lookup_elem(struct bpf_map *map, u32 key)
{
	struct bpf_stab *stab = container_of(map, struct bpf_stab, map);

	if (key >= map->max_entries)
		return NULL;

	return READ_ONCE(stab->sock_map[key]);
}

static int sock_map_delete_elem(struct bpf_map *map, void *key)
{
	struct bpf_stab *stab = container_of(map, struct bpf_stab, map);
	struct smap_psock *psock;
	int k = *(u32 *)key;
	struct sock *sock;

	if (k >= map->max_entries)
		return -EINVAL;

	sock = xchg(&stab->sock_map[k], NULL);
	if (!sock)
		return -EINVAL;

	psock = smap_psock_sk(sock);
	if (!psock)
		goto out;

	if (psock->bpf_parse)
		smap_stop_sock(psock, sock);
	smap_list_map_remove(psock, &stab->sock_map[k]);
	smap_release_sock(psock, sock);
out:
	return 0;
}

/* Locking notes: Concurrent updates, deletes, and lookups are allowed and are
 * done inside rcu critical sections. This ensures on updates that the psock
 * will not be released via smap_release_sock() until concurrent updates/deletes
 * complete. All operations operate on sock_map using cmpxchg and xchg
 * operations to ensure we do not get stale references. Any reads into the
 * map must be done with READ_ONCE() because of this.
 *
 * A psock is destroyed via call_rcu and after any worker threads are cancelled
 * and syncd so we are certain all references from the update/lookup/delete
 * operations as well as references in the data path are no longer in use.
 *
 * Psocks may exist in multiple maps, but only a single set of parse/verdict
 * programs may be inherited from the maps it belongs to. A reference count
 * is kept with the total number of references to the psock from all maps. The
 * psock will not be released until this reaches zero. The psock and sock
 * user data data use the sk_callback_lock to protect critical data structures
 * from concurrent access. This allows us to avoid two updates from modifying
 * the user data in sock and the lock is required anyways for modifying
 * callbacks, we simply increase its scope slightly.
 *
 * Rules to follow,
 *  - psock must always be read inside RCU critical section
 *  - sk_user_data must only be modified inside sk_callback_lock and read
 *    inside RCU critical section.
 *  - psock->maps list must only be read & modified inside sk_callback_lock
 *  - sock_map must use READ_ONCE and (cmp)xchg operations
 *  - BPF verdict/parse programs must use READ_ONCE and xchg operations
 */

static int __sock_map_ctx_update_elem(struct bpf_map *map,
				      struct bpf_sock_progs *progs,
				      struct sock *sock,
				      struct sock **map_link,
				      void *key)
{
	struct bpf_prog *verdict, *parse, *tx_msg;
	struct smap_psock_map_entry *e = NULL;
	struct smap_psock *psock;
	bool new = false;
	int err = 0;

	/* 1. If sock map has BPF programs those will be inherited by the
	 * sock being added. If the sock is already attached to BPF programs
	 * this results in an error.
	 */
	verdict = READ_ONCE(progs->bpf_verdict);
	parse = READ_ONCE(progs->bpf_parse);
	tx_msg = READ_ONCE(progs->bpf_tx_msg);

	if (parse && verdict) {
		/* bpf prog refcnt may be zero if a concurrent attach operation
		 * removes the program after the above READ_ONCE() but before
		 * we increment the refcnt. If this is the case abort with an
		 * error.
		 */
		verdict = bpf_prog_inc_not_zero(verdict);
		if (IS_ERR(verdict))
			return PTR_ERR(verdict);

		parse = bpf_prog_inc_not_zero(parse);
		if (IS_ERR(parse)) {
			bpf_prog_put(verdict);
			return PTR_ERR(parse);
		}
	}

	if (tx_msg) {
		tx_msg = bpf_prog_inc_not_zero(tx_msg);
		if (IS_ERR(tx_msg)) {
			if (parse && verdict) {
				bpf_prog_put(parse);
				bpf_prog_put(verdict);
			}
			return PTR_ERR(tx_msg);
		}
	}

	psock = smap_psock_sk(sock);

	/* 2. Do not allow inheriting programs if psock exists and has
	 * already inherited programs. This would create confusion on
	 * which parser/verdict program is running. If no psock exists
	 * create one. Inside sk_callback_lock to ensure concurrent create
	 * doesn't update user data.
	 */
	if (psock) {
		if (READ_ONCE(psock->bpf_parse) && parse) {
			err = -EBUSY;
			goto out_progs;
		}
		if (READ_ONCE(psock->bpf_tx_msg) && tx_msg) {
			err = -EBUSY;
			goto out_progs;
		}
		if (!refcount_inc_not_zero(&psock->refcnt)) {
			err = -EAGAIN;
			goto out_progs;
		}
	} else {
		psock = smap_init_psock(sock, map->numa_node);
		if (IS_ERR(psock)) {
			err = PTR_ERR(psock);
			goto out_progs;
		}

		set_bit(SMAP_TX_RUNNING, &psock->state);
		new = true;
	}

	if (map_link) {
		e = kzalloc(sizeof(*e), GFP_ATOMIC | __GFP_NOWARN);
		if (!e) {
			err = -ENOMEM;
			goto out_free;
		}
	}

	/* 3. At this point we have a reference to a valid psock that is
	 * running. Attach any BPF programs needed.
	 */
	if (tx_msg)
		bpf_tcp_msg_add(psock, sock, tx_msg);
	if (new) {
		err = tcp_set_ulp_id(sock, TCP_ULP_BPF);
		if (err)
			goto out_free;
	}

	if (parse && verdict && !psock->strp_enabled) {
		err = smap_init_sock(psock, sock);
		if (err)
			goto out_free;
		smap_init_progs(psock, verdict, parse);
		write_lock_bh(&sock->sk_callback_lock);
		smap_start_sock(psock, sock);
		write_unlock_bh(&sock->sk_callback_lock);
	}

	/* 4. Place psock in sockmap for use and stop any programs on
	 * the old sock assuming its not the same sock we are replacing
	 * it with. Because we can only have a single set of programs if
	 * old_sock has a strp we can stop it.
	 */
	if (map_link) {
		e->entry = map_link;
		spin_lock_bh(&psock->maps_lock);
		list_add_tail(&e->list, &psock->maps);
		spin_unlock_bh(&psock->maps_lock);
	}
	return err;
out_free:
	smap_release_sock(psock, sock);
out_progs:
	if (parse && verdict) {
		bpf_prog_put(parse);
		bpf_prog_put(verdict);
	}
	if (tx_msg)
		bpf_prog_put(tx_msg);
	kfree(e);
	return err;
}

static int sock_map_ctx_update_elem(struct bpf_sock_ops_kern *skops,
				    struct bpf_map *map,
				    void *key, u64 flags)
{
	struct bpf_stab *stab = container_of(map, struct bpf_stab, map);
	struct bpf_sock_progs *progs = &stab->progs;
	struct sock *osock, *sock;
	u32 i = *(u32 *)key;
	int err;

	if (unlikely(flags > BPF_EXIST))
		return -EINVAL;

	if (unlikely(i >= stab->map.max_entries))
		return -E2BIG;

	sock = READ_ONCE(stab->sock_map[i]);
	if (flags == BPF_EXIST && !sock)
		return -ENOENT;
	else if (flags == BPF_NOEXIST && sock)
		return -EEXIST;

	sock = skops->sk;
	err = __sock_map_ctx_update_elem(map, progs, sock, &stab->sock_map[i],
					 key);
	if (err)
		goto out;

	osock = xchg(&stab->sock_map[i], sock);
	if (osock) {
		struct smap_psock *opsock = smap_psock_sk(osock);

		smap_list_map_remove(opsock, &stab->sock_map[i]);
		smap_release_sock(opsock, osock);
	}
out:
	return err;
}

int sock_map_prog(struct bpf_map *map, struct bpf_prog *prog, u32 type)
{
	struct bpf_sock_progs *progs;
	struct bpf_prog *orig;

	if (map->map_type == BPF_MAP_TYPE_SOCKMAP) {
		struct bpf_stab *stab = container_of(map, struct bpf_stab, map);

		progs = &stab->progs;
	} else if (map->map_type == BPF_MAP_TYPE_SOCKHASH) {
		struct bpf_htab *htab = container_of(map, struct bpf_htab, map);

		progs = &htab->progs;
	} else {
		return -EINVAL;
	}

	switch (type) {
	case BPF_SK_MSG_VERDICT:
		orig = xchg(&progs->bpf_tx_msg, prog);
		break;
	case BPF_SK_SKB_STREAM_PARSER:
		orig = xchg(&progs->bpf_parse, prog);
		break;
	case BPF_SK_SKB_STREAM_VERDICT:
		orig = xchg(&progs->bpf_verdict, prog);
		break;
	default:
		return -EOPNOTSUPP;
	}

	if (orig)
		bpf_prog_put(orig);

	return 0;
}

int sockmap_get_from_fd(const union bpf_attr *attr, int type,
			struct bpf_prog *prog)
{
	int ufd = attr->target_fd;
	struct bpf_map *map;
	struct fd f;
	int err;

	f = fdget(ufd);
	map = __bpf_map_get(f);
	if (IS_ERR(map))
		return PTR_ERR(map);

	err = sock_map_prog(map, prog, attr->attach_type);
	fdput(f);
	return err;
}

static void *sock_map_lookup(struct bpf_map *map, void *key)
{
	return NULL;
}

static int sock_map_update_elem(struct bpf_map *map,
				void *key, void *value, u64 flags)
{
	struct bpf_sock_ops_kern skops;
	u32 fd = *(u32 *)value;
	struct socket *socket;
	int err;

	socket = sockfd_lookup(fd, &err);
	if (!socket)
		return err;

	skops.sk = socket->sk;
	if (!skops.sk) {
		fput(socket->file);
		return -EINVAL;
	}

	if (skops.sk->sk_type != SOCK_STREAM ||
	    skops.sk->sk_protocol != IPPROTO_TCP) {
		fput(socket->file);
		return -EOPNOTSUPP;
	}

	lock_sock(skops.sk);
	preempt_disable();
	rcu_read_lock();
	err = sock_map_ctx_update_elem(&skops, map, key, flags);
	rcu_read_unlock();
	preempt_enable();
	release_sock(skops.sk);
	fput(socket->file);
	return err;
}

static void sock_map_release(struct bpf_map *map)
{
	struct bpf_sock_progs *progs;
	struct bpf_prog *orig;

	if (map->map_type == BPF_MAP_TYPE_SOCKMAP) {
		struct bpf_stab *stab = container_of(map, struct bpf_stab, map);

		progs = &stab->progs;
	} else {
		struct bpf_htab *htab = container_of(map, struct bpf_htab, map);

		progs = &htab->progs;
	}

	orig = xchg(&progs->bpf_parse, NULL);
	if (orig)
		bpf_prog_put(orig);
	orig = xchg(&progs->bpf_verdict, NULL);
	if (orig)
		bpf_prog_put(orig);

	orig = xchg(&progs->bpf_tx_msg, NULL);
	if (orig)
		bpf_prog_put(orig);
}

static struct bpf_map *sock_hash_alloc(union bpf_attr *attr)
{
	struct bpf_htab *htab;
	int i, err;
	u64 cost;

	if (!capable(CAP_NET_ADMIN))
		return ERR_PTR(-EPERM);

	/* check sanity of attributes */
	if (attr->max_entries == 0 || attr->value_size != 4 ||
	    attr->map_flags & ~SOCK_CREATE_FLAG_MASK)
		return ERR_PTR(-EINVAL);

	if (attr->key_size > MAX_BPF_STACK)
		/* eBPF programs initialize keys on stack, so they cannot be
		 * larger than max stack size
		 */
		return ERR_PTR(-E2BIG);

	err = bpf_tcp_ulp_register();
	if (err && err != -EEXIST)
		return ERR_PTR(err);

	htab = kzalloc(sizeof(*htab), GFP_USER);
	if (!htab)
		return ERR_PTR(-ENOMEM);

	bpf_map_init_from_attr(&htab->map, attr);

	htab->n_buckets = roundup_pow_of_two(htab->map.max_entries);
	htab->elem_size = sizeof(struct htab_elem) +
			  round_up(htab->map.key_size, 8);
	err = -EINVAL;
	if (htab->n_buckets == 0 ||
	    htab->n_buckets > U32_MAX / sizeof(struct bucket))
		goto free_htab;

	cost = (u64) htab->n_buckets * sizeof(struct bucket) +
	       (u64) htab->elem_size * htab->map.max_entries;

	if (cost >= U32_MAX - PAGE_SIZE)
		goto free_htab;

	htab->map.pages = round_up(cost, PAGE_SIZE) >> PAGE_SHIFT;
	err = bpf_map_precharge_memlock(htab->map.pages);
	if (err)
		goto free_htab;

	err = -ENOMEM;
	htab->buckets = bpf_map_area_alloc(
				htab->n_buckets * sizeof(struct bucket),
				htab->map.numa_node);
	if (!htab->buckets)
		goto free_htab;

	for (i = 0; i < htab->n_buckets; i++) {
		INIT_HLIST_HEAD(&htab->buckets[i].head);
		raw_spin_lock_init(&htab->buckets[i].lock);
	}

	return &htab->map;
free_htab:
	kfree(htab);
	return ERR_PTR(err);
}

static void __bpf_htab_free(struct rcu_head *rcu)
{
	struct bpf_htab *htab;

	htab = container_of(rcu, struct bpf_htab, rcu);
	bpf_map_area_free(htab->buckets);
	kfree(htab);
}

static void sock_hash_free(struct bpf_map *map)
{
	struct bpf_htab *htab = container_of(map, struct bpf_htab, map);
	int i;

	synchronize_rcu();

	/* At this point no update, lookup or delete operations can happen.
	 * However, be aware we can still get a socket state event updates,
	 * and data ready callabacks that reference the psock from sk_user_data
	 * Also psock worker threads are still in-flight. So smap_release_sock
	 * will only free the psock after cancel_sync on the worker threads
	 * and a grace period expire to ensure psock is really safe to remove.
	 */
	rcu_read_lock();
	for (i = 0; i < htab->n_buckets; i++) {
		struct bucket *b = __select_bucket(htab, i);
		struct hlist_head *head;
		struct hlist_node *n;
		struct htab_elem *l;

		raw_spin_lock_bh(&b->lock);
		head = &b->head;
		hlist_for_each_entry_safe(l, n, head, hash_node) {
			struct sock *sock = l->sk;
			struct smap_psock *psock;

			hlist_del_rcu(&l->hash_node);
			psock = smap_psock_sk(sock);
			/* This check handles a racing sock event that can get
			 * the sk_callback_lock before this case but after xchg
			 * causing the refcnt to hit zero and sock user data
			 * (psock) to be null and queued for garbage collection.
			 */
			if (likely(psock)) {
				smap_list_hash_remove(psock, l);
				smap_release_sock(psock, sock);
			}
			free_htab_elem(htab, l);
		}
		raw_spin_unlock_bh(&b->lock);
	}
	rcu_read_unlock();
	call_rcu(&htab->rcu, __bpf_htab_free);
}

static struct htab_elem *alloc_sock_hash_elem(struct bpf_htab *htab,
					      void *key, u32 key_size, u32 hash,
					      struct sock *sk,
					      struct htab_elem *old_elem)
{
	struct htab_elem *l_new;

	if (atomic_inc_return(&htab->count) > htab->map.max_entries) {
		if (!old_elem) {
			atomic_dec(&htab->count);
			return ERR_PTR(-E2BIG);
		}
	}
	l_new = kmalloc_node(htab->elem_size, GFP_ATOMIC | __GFP_NOWARN,
			     htab->map.numa_node);
	if (!l_new)
		return ERR_PTR(-ENOMEM);

	memcpy(l_new->key, key, key_size);
	l_new->sk = sk;
	l_new->hash = hash;
	return l_new;
}

static inline u32 htab_map_hash(const void *key, u32 key_len)
{
	return jhash(key, key_len, 0);
}

static int sock_hash_get_next_key(struct bpf_map *map,
				  void *key, void *next_key)
{
	struct bpf_htab *htab = container_of(map, struct bpf_htab, map);
	struct htab_elem *l, *next_l;
	struct hlist_head *h;
	u32 hash, key_size;
	int i = 0;

	WARN_ON_ONCE(!rcu_read_lock_held());

	key_size = map->key_size;
	if (!key)
		goto find_first_elem;
	hash = htab_map_hash(key, key_size);
	h = select_bucket(htab, hash);

	l = lookup_elem_raw(h, hash, key, key_size);
	if (!l)
		goto find_first_elem;
	next_l = hlist_entry_safe(
		     rcu_dereference_raw(hlist_next_rcu(&l->hash_node)),
		     struct htab_elem, hash_node);
	if (next_l) {
		memcpy(next_key, next_l->key, key_size);
		return 0;
	}

	/* no more elements in this hash list, go to the next bucket */
	i = hash & (htab->n_buckets - 1);
	i++;

find_first_elem:
	/* iterate over buckets */
	for (; i < htab->n_buckets; i++) {
		h = select_bucket(htab, i);

		/* pick first element in the bucket */
		next_l = hlist_entry_safe(
				rcu_dereference_raw(hlist_first_rcu(h)),
				struct htab_elem, hash_node);
		if (next_l) {
			/* if it's not empty, just return it */
			memcpy(next_key, next_l->key, key_size);
			return 0;
		}
	}

	/* iterated over all buckets and all elements */
	return -ENOENT;
}

static int sock_hash_ctx_update_elem(struct bpf_sock_ops_kern *skops,
				     struct bpf_map *map,
				     void *key, u64 map_flags)
{
	struct bpf_htab *htab = container_of(map, struct bpf_htab, map);
	struct bpf_sock_progs *progs = &htab->progs;
	struct htab_elem *l_new = NULL, *l_old;
	struct smap_psock_map_entry *e = NULL;
	struct hlist_head *head;
	struct smap_psock *psock;
	u32 key_size, hash;
	struct sock *sock;
	struct bucket *b;
	int err;

	sock = skops->sk;

	if (sock->sk_type != SOCK_STREAM ||
	    sock->sk_protocol != IPPROTO_TCP)
		return -EOPNOTSUPP;

	if (unlikely(map_flags > BPF_EXIST))
		return -EINVAL;

	e = kzalloc(sizeof(*e), GFP_ATOMIC | __GFP_NOWARN);
	if (!e)
		return -ENOMEM;

	WARN_ON_ONCE(!rcu_read_lock_held());
	key_size = map->key_size;
	hash = htab_map_hash(key, key_size);
	b = __select_bucket(htab, hash);
	head = &b->head;

	err = __sock_map_ctx_update_elem(map, progs, sock, NULL, key);
	if (err)
		goto err;

	/* psock is valid here because otherwise above *ctx_update_elem would
	 * have thrown an error. It is safe to skip error check.
	 */
	psock = smap_psock_sk(sock);
	raw_spin_lock_bh(&b->lock);
	l_old = lookup_elem_raw(head, hash, key, key_size);
	if (l_old && map_flags == BPF_NOEXIST) {
		err = -EEXIST;
		goto bucket_err;
	}
	if (!l_old && map_flags == BPF_EXIST) {
		err = -ENOENT;
		goto bucket_err;
	}

	l_new = alloc_sock_hash_elem(htab, key, key_size, hash, sock, l_old);
	if (IS_ERR(l_new)) {
		err = PTR_ERR(l_new);
		goto bucket_err;
	}

	rcu_assign_pointer(e->hash_link, l_new);
	rcu_assign_pointer(e->htab,
			   container_of(map, struct bpf_htab, map));
	spin_lock_bh(&psock->maps_lock);
	list_add_tail(&e->list, &psock->maps);
	spin_unlock_bh(&psock->maps_lock);

	/* add new element to the head of the list, so that
	 * concurrent search will find it before old elem
	 */
	hlist_add_head_rcu(&l_new->hash_node, head);
	if (l_old) {
		psock = smap_psock_sk(l_old->sk);

		hlist_del_rcu(&l_old->hash_node);
		smap_list_hash_remove(psock, l_old);
		smap_release_sock(psock, l_old->sk);
		free_htab_elem(htab, l_old);
	}
	raw_spin_unlock_bh(&b->lock);
	return 0;
bucket_err:
	smap_release_sock(psock, sock);
	raw_spin_unlock_bh(&b->lock);
err:
	kfree(e);
	return err;
}

static int sock_hash_update_elem(struct bpf_map *map,
				void *key, void *value, u64 flags)
{
	struct bpf_sock_ops_kern skops;
	u32 fd = *(u32 *)value;
	struct socket *socket;
	int err;

	socket = sockfd_lookup(fd, &err);
	if (!socket)
		return err;

	skops.sk = socket->sk;
	if (!skops.sk) {
		fput(socket->file);
		return -EINVAL;
	}

	lock_sock(skops.sk);
	preempt_disable();
	rcu_read_lock();
	err = sock_hash_ctx_update_elem(&skops, map, key, flags);
	rcu_read_unlock();
	preempt_enable();
	release_sock(skops.sk);
	fput(socket->file);
	return err;
}

static int sock_hash_delete_elem(struct bpf_map *map, void *key)
{
	struct bpf_htab *htab = container_of(map, struct bpf_htab, map);
	struct hlist_head *head;
	struct bucket *b;
	struct htab_elem *l;
	u32 hash, key_size;
	int ret = -ENOENT;

	key_size = map->key_size;
	hash = htab_map_hash(key, key_size);
	b = __select_bucket(htab, hash);
	head = &b->head;

	raw_spin_lock_bh(&b->lock);
	l = lookup_elem_raw(head, hash, key, key_size);
	if (l) {
		struct sock *sock = l->sk;
		struct smap_psock *psock;

		hlist_del_rcu(&l->hash_node);
		psock = smap_psock_sk(sock);
		/* This check handles a racing sock event that can get the
		 * sk_callback_lock before this case but after xchg happens
		 * causing the refcnt to hit zero and sock user data (psock)
		 * to be null and queued for garbage collection.
		 */
		if (likely(psock)) {
			smap_list_hash_remove(psock, l);
			smap_release_sock(psock, sock);
		}
		free_htab_elem(htab, l);
		ret = 0;
	}
	raw_spin_unlock_bh(&b->lock);
	return ret;
}

struct sock  *__sock_hash_lookup_elem(struct bpf_map *map, void *key)
{
	struct bpf_htab *htab = container_of(map, struct bpf_htab, map);
	struct hlist_head *head;
	struct htab_elem *l;
	u32 key_size, hash;
	struct bucket *b;
	struct sock *sk;

	key_size = map->key_size;
	hash = htab_map_hash(key, key_size);
	b = __select_bucket(htab, hash);
	head = &b->head;

	l = lookup_elem_raw(head, hash, key, key_size);
	sk = l ? l->sk : NULL;
	return sk;
}

const struct bpf_map_ops sock_map_ops = {
	.map_alloc = sock_map_alloc,
	.map_free = sock_map_free,
	.map_lookup_elem = sock_map_lookup,
	.map_get_next_key = sock_map_get_next_key,
	.map_update_elem = sock_map_update_elem,
	.map_delete_elem = sock_map_delete_elem,
	.map_release_uref = sock_map_release,
};

const struct bpf_map_ops sock_hash_ops = {
	.map_alloc = sock_hash_alloc,
	.map_free = sock_hash_free,
	.map_lookup_elem = sock_map_lookup,
	.map_get_next_key = sock_hash_get_next_key,
	.map_update_elem = sock_hash_update_elem,
	.map_delete_elem = sock_hash_delete_elem,
	.map_release_uref = sock_map_release,
};

BPF_CALL_4(bpf_sock_map_update, struct bpf_sock_ops_kern *, bpf_sock,
	   struct bpf_map *, map, void *, key, u64, flags)
{
	WARN_ON_ONCE(!rcu_read_lock_held());
	return sock_map_ctx_update_elem(bpf_sock, map, key, flags);
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

BPF_CALL_4(bpf_sock_hash_update, struct bpf_sock_ops_kern *, bpf_sock,
	   struct bpf_map *, map, void *, key, u64, flags)
{
	WARN_ON_ONCE(!rcu_read_lock_held());
	return sock_hash_ctx_update_elem(bpf_sock, map, key, flags);
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
