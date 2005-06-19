/*
 * NET		Generic infrastructure for Network protocols.
 *
 *		Definitions for request_sock 
 *
 * Authors:	Arnaldo Carvalho de Melo <acme@conectiva.com.br>
 *
 * 		From code originally in include/net/tcp.h
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */
#ifndef _REQUEST_SOCK_H
#define _REQUEST_SOCK_H

#include <linux/slab.h>
#include <linux/types.h>
#include <net/sock.h>

struct request_sock;
struct sk_buff;
struct dst_entry;
struct proto;

struct request_sock_ops {
	int		family;
	kmem_cache_t	*slab;
	int		obj_size;
	int		(*rtx_syn_ack)(struct sock *sk,
				       struct request_sock *req,
				       struct dst_entry *dst);
	void		(*send_ack)(struct sk_buff *skb,
				    struct request_sock *req);
	void		(*send_reset)(struct sk_buff *skb);
	void		(*destructor)(struct request_sock *req);
};

/* struct request_sock - mini sock to represent a connection request
 */
struct request_sock {
	struct request_sock		*dl_next; /* Must be first member! */
	u16				mss;
	u8				retrans;
	u8				__pad;
	/* The following two fields can be easily recomputed I think -AK */
	u32				window_clamp; /* window clamp at creation time */
	u32				rcv_wnd;	  /* rcv_wnd offered first time */
	u32				ts_recent;
	unsigned long			expires;
	struct request_sock_ops		*rsk_ops;
	struct sock			*sk;
};

static inline struct request_sock *reqsk_alloc(struct request_sock_ops *ops)
{
	struct request_sock *req = kmem_cache_alloc(ops->slab, SLAB_ATOMIC);

	if (req != NULL)
		req->rsk_ops = ops;

	return req;
}

static inline void __reqsk_free(struct request_sock *req)
{
	kmem_cache_free(req->rsk_ops->slab, req);
}

static inline void reqsk_free(struct request_sock *req)
{
	req->rsk_ops->destructor(req);
	__reqsk_free(req);
}

#endif /* _REQUEST_SOCK_H */
