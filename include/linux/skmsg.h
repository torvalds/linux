/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2017 - 2018 Covalent IO, Inc. http://covalent.io */

#ifndef _LINUX_SKMSG_H
#define _LINUX_SKMSG_H

#include <linux/bpf.h>
#include <linux/filter.h>
#include <linux/scatterlist.h>
#include <linux/skbuff.h>

#include <net/sock.h>
#include <net/tcp.h>
#include <net/strparser.h>

#define MAX_MSG_FRAGS			MAX_SKB_FRAGS

enum __sk_action {
	__SK_DROP = 0,
	__SK_PASS,
	__SK_REDIRECT,
	__SK_NONE,
};

struct sk_msg_sg {
	u32				start;
	u32				curr;
	u32				end;
	u32				size;
	u32				copybreak;
	bool				copy[MAX_MSG_FRAGS];
	/* The extra element is used for chaining the front and sections when
	 * the list becomes partitioned (e.g. end < start). The crypto APIs
	 * require the chaining.
	 */
	struct scatterlist		data[MAX_MSG_FRAGS + 1];
};

/* UAPI in filter.c depends on struct sk_msg_sg being first element. */
struct sk_msg {
	struct sk_msg_sg		sg;
	void				*data;
	void				*data_end;
	u32				apply_bytes;
	u32				cork_bytes;
	u32				flags;
	struct sk_buff			*skb;
	struct sock			*sk_redir;
	struct sock			*sk;
	struct list_head		list;
};

struct sk_psock_progs {
	struct bpf_prog			*msg_parser;
	struct bpf_prog			*skb_parser;
	struct bpf_prog			*skb_verdict;
};

enum sk_psock_state_bits {
	SK_PSOCK_TX_ENABLED,
};

struct sk_psock_link {
	struct list_head		list;
	struct bpf_map			*map;
	void				*link_raw;
};

struct sk_psock_parser {
	struct strparser		strp;
	bool				enabled;
	void (*saved_data_ready)(struct sock *sk);
};

struct sk_psock_work_state {
	struct sk_buff			*skb;
	u32				len;
	u32				off;
};

struct sk_psock {
	struct sock			*sk;
	struct sock			*sk_redir;
	u32				apply_bytes;
	u32				cork_bytes;
	u32				eval;
	struct sk_msg			*cork;
	struct sk_psock_progs		progs;
	struct sk_psock_parser		parser;
	struct sk_buff_head		ingress_skb;
	struct list_head		ingress_msg;
	unsigned long			state;
	struct list_head		link;
	spinlock_t			link_lock;
	refcount_t			refcnt;
	void (*saved_unhash)(struct sock *sk);
	void (*saved_close)(struct sock *sk, long timeout);
	void (*saved_write_space)(struct sock *sk);
	struct proto			*sk_proto;
	struct sk_psock_work_state	work_state;
	struct work_struct		work;
	union {
		struct rcu_head		rcu;
		struct work_struct	gc;
	};
};

int sk_msg_alloc(struct sock *sk, struct sk_msg *msg, int len,
		 int elem_first_coalesce);
int sk_msg_clone(struct sock *sk, struct sk_msg *dst, struct sk_msg *src,
		 u32 off, u32 len);
void sk_msg_trim(struct sock *sk, struct sk_msg *msg, int len);
int sk_msg_free(struct sock *sk, struct sk_msg *msg);
int sk_msg_free_nocharge(struct sock *sk, struct sk_msg *msg);
void sk_msg_free_partial(struct sock *sk, struct sk_msg *msg, u32 bytes);
void sk_msg_free_partial_nocharge(struct sock *sk, struct sk_msg *msg,
				  u32 bytes);

void sk_msg_return(struct sock *sk, struct sk_msg *msg, int bytes);
void sk_msg_return_zero(struct sock *sk, struct sk_msg *msg, int bytes);

int sk_msg_zerocopy_from_iter(struct sock *sk, struct iov_iter *from,
			      struct sk_msg *msg, u32 bytes);
int sk_msg_memcopy_from_iter(struct sock *sk, struct iov_iter *from,
			     struct sk_msg *msg, u32 bytes);

static inline void sk_msg_check_to_free(struct sk_msg *msg, u32 i, u32 bytes)
{
	WARN_ON(i == msg->sg.end && bytes);
}

static inline void sk_msg_apply_bytes(struct sk_psock *psock, u32 bytes)
{
	if (psock->apply_bytes) {
		if (psock->apply_bytes < bytes)
			psock->apply_bytes = 0;
		else
			psock->apply_bytes -= bytes;
	}
}

#define sk_msg_iter_var_prev(var)			\
	do {						\
		if (var == 0)				\
			var = MAX_MSG_FRAGS - 1;	\
		else					\
			var--;				\
	} while (0)

#define sk_msg_iter_var_next(var)			\
	do {						\
		var++;					\
		if (var == MAX_MSG_FRAGS)		\
			var = 0;			\
	} while (0)

#define sk_msg_iter_prev(msg, which)			\
	sk_msg_iter_var_prev(msg->sg.which)

#define sk_msg_iter_next(msg, which)			\
	sk_msg_iter_var_next(msg->sg.which)

static inline void sk_msg_clear_meta(struct sk_msg *msg)
{
	memset(&msg->sg, 0, offsetofend(struct sk_msg_sg, copy));
}

static inline void sk_msg_init(struct sk_msg *msg)
{
	BUILD_BUG_ON(ARRAY_SIZE(msg->sg.data) - 1 != MAX_MSG_FRAGS);
	memset(msg, 0, sizeof(*msg));
	sg_init_marker(msg->sg.data, MAX_MSG_FRAGS);
}

static inline void sk_msg_xfer(struct sk_msg *dst, struct sk_msg *src,
			       int which, u32 size)
{
	dst->sg.data[which] = src->sg.data[which];
	dst->sg.data[which].length  = size;
	dst->sg.size		   += size;
	src->sg.data[which].length -= size;
	src->sg.data[which].offset += size;
}

static inline void sk_msg_xfer_full(struct sk_msg *dst, struct sk_msg *src)
{
	memcpy(dst, src, sizeof(*src));
	sk_msg_init(src);
}

static inline bool sk_msg_full(const struct sk_msg *msg)
{
	return (msg->sg.end == msg->sg.start) && msg->sg.size;
}

static inline u32 sk_msg_elem_used(const struct sk_msg *msg)
{
	if (sk_msg_full(msg))
		return MAX_MSG_FRAGS;

	return msg->sg.end >= msg->sg.start ?
		msg->sg.end - msg->sg.start :
		msg->sg.end + (MAX_MSG_FRAGS - msg->sg.start);
}

static inline struct scatterlist *sk_msg_elem(struct sk_msg *msg, int which)
{
	return &msg->sg.data[which];
}

static inline struct scatterlist sk_msg_elem_cpy(struct sk_msg *msg, int which)
{
	return msg->sg.data[which];
}

static inline struct page *sk_msg_page(struct sk_msg *msg, int which)
{
	return sg_page(sk_msg_elem(msg, which));
}

static inline bool sk_msg_to_ingress(const struct sk_msg *msg)
{
	return msg->flags & BPF_F_INGRESS;
}

static inline void sk_msg_compute_data_pointers(struct sk_msg *msg)
{
	struct scatterlist *sge = sk_msg_elem(msg, msg->sg.start);

	if (msg->sg.copy[msg->sg.start]) {
		msg->data = NULL;
		msg->data_end = NULL;
	} else {
		msg->data = sg_virt(sge);
		msg->data_end = msg->data + sge->length;
	}
}

static inline void sk_msg_page_add(struct sk_msg *msg, struct page *page,
				   u32 len, u32 offset)
{
	struct scatterlist *sge;

	get_page(page);
	sge = sk_msg_elem(msg, msg->sg.end);
	sg_set_page(sge, page, len, offset);
	sg_unmark_end(sge);

	msg->sg.copy[msg->sg.end] = true;
	msg->sg.size += len;
	sk_msg_iter_next(msg, end);
}

static inline void sk_msg_sg_copy(struct sk_msg *msg, u32 i, bool copy_state)
{
	do {
		msg->sg.copy[i] = copy_state;
		sk_msg_iter_var_next(i);
		if (i == msg->sg.end)
			break;
	} while (1);
}

static inline void sk_msg_sg_copy_set(struct sk_msg *msg, u32 start)
{
	sk_msg_sg_copy(msg, start, true);
}

static inline void sk_msg_sg_copy_clear(struct sk_msg *msg, u32 start)
{
	sk_msg_sg_copy(msg, start, false);
}

static inline struct sk_psock *sk_psock(const struct sock *sk)
{
	return rcu_dereference_sk_user_data(sk);
}

static inline void sk_psock_queue_msg(struct sk_psock *psock,
				      struct sk_msg *msg)
{
	list_add_tail(&msg->list, &psock->ingress_msg);
}

static inline bool sk_psock_queue_empty(const struct sk_psock *psock)
{
	return psock ? list_empty(&psock->ingress_msg) : true;
}

static inline void sk_psock_report_error(struct sk_psock *psock, int err)
{
	struct sock *sk = psock->sk;

	sk->sk_err = err;
	sk->sk_error_report(sk);
}

struct sk_psock *sk_psock_init(struct sock *sk, int node);

int sk_psock_init_strp(struct sock *sk, struct sk_psock *psock);
void sk_psock_start_strp(struct sock *sk, struct sk_psock *psock);
void sk_psock_stop_strp(struct sock *sk, struct sk_psock *psock);

int sk_psock_msg_verdict(struct sock *sk, struct sk_psock *psock,
			 struct sk_msg *msg);

static inline struct sk_psock_link *sk_psock_init_link(void)
{
	return kzalloc(sizeof(struct sk_psock_link),
		       GFP_ATOMIC | __GFP_NOWARN);
}

static inline void sk_psock_free_link(struct sk_psock_link *link)
{
	kfree(link);
}

struct sk_psock_link *sk_psock_link_pop(struct sk_psock *psock);
#if defined(CONFIG_BPF_STREAM_PARSER)
void sk_psock_unlink(struct sock *sk, struct sk_psock_link *link);
#else
static inline void sk_psock_unlink(struct sock *sk,
				   struct sk_psock_link *link)
{
}
#endif

void __sk_psock_purge_ingress_msg(struct sk_psock *psock);

static inline void sk_psock_cork_free(struct sk_psock *psock)
{
	if (psock->cork) {
		sk_msg_free(psock->sk, psock->cork);
		kfree(psock->cork);
		psock->cork = NULL;
	}
}

static inline void sk_psock_update_proto(struct sock *sk,
					 struct sk_psock *psock,
					 struct proto *ops)
{
	psock->saved_unhash = sk->sk_prot->unhash;
	psock->saved_close = sk->sk_prot->close;
	psock->saved_write_space = sk->sk_write_space;

	psock->sk_proto = sk->sk_prot;
	sk->sk_prot = ops;
}

static inline void sk_psock_restore_proto(struct sock *sk,
					  struct sk_psock *psock)
{
	sk->sk_write_space = psock->saved_write_space;

	if (psock->sk_proto) {
		sk->sk_prot = psock->sk_proto;
		psock->sk_proto = NULL;
	}
}

static inline void sk_psock_set_state(struct sk_psock *psock,
				      enum sk_psock_state_bits bit)
{
	set_bit(bit, &psock->state);
}

static inline void sk_psock_clear_state(struct sk_psock *psock,
					enum sk_psock_state_bits bit)
{
	clear_bit(bit, &psock->state);
}

static inline bool sk_psock_test_state(const struct sk_psock *psock,
				       enum sk_psock_state_bits bit)
{
	return test_bit(bit, &psock->state);
}

static inline struct sk_psock *sk_psock_get_checked(struct sock *sk)
{
	struct sk_psock *psock;

	rcu_read_lock();
	psock = sk_psock(sk);
	if (psock) {
		if (sk->sk_prot->recvmsg != tcp_bpf_recvmsg) {
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

static inline struct sk_psock *sk_psock_get(struct sock *sk)
{
	struct sk_psock *psock;

	rcu_read_lock();
	psock = sk_psock(sk);
	if (psock && !refcount_inc_not_zero(&psock->refcnt))
		psock = NULL;
	rcu_read_unlock();
	return psock;
}

void sk_psock_stop(struct sock *sk, struct sk_psock *psock);
void sk_psock_destroy(struct rcu_head *rcu);
void sk_psock_drop(struct sock *sk, struct sk_psock *psock);

static inline void sk_psock_put(struct sock *sk, struct sk_psock *psock)
{
	if (refcount_dec_and_test(&psock->refcnt))
		sk_psock_drop(sk, psock);
}

static inline void sk_psock_data_ready(struct sock *sk, struct sk_psock *psock)
{
	if (psock->parser.enabled)
		psock->parser.saved_data_ready(sk);
	else
		sk->sk_data_ready(sk);
}

static inline void psock_set_prog(struct bpf_prog **pprog,
				  struct bpf_prog *prog)
{
	prog = xchg(pprog, prog);
	if (prog)
		bpf_prog_put(prog);
}

static inline void psock_progs_drop(struct sk_psock_progs *progs)
{
	psock_set_prog(&progs->msg_parser, NULL);
	psock_set_prog(&progs->skb_parser, NULL);
	psock_set_prog(&progs->skb_verdict, NULL);
}

#endif /* _LINUX_SKMSG_H */
