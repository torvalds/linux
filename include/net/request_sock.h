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

struct open_request;
struct sk_buff;
struct dst_entry;
struct proto;

struct or_calltable {
	int		family;
	kmem_cache_t	*slab;
	int		obj_size;
	int		(*rtx_syn_ack)(struct sock *sk,
				       struct open_request *req,
				       struct dst_entry *dst);
	void		(*send_ack)(struct sk_buff *skb,
				    struct open_request *req);
	void		(*send_reset)(struct sk_buff *skb);
	void		(*destructor)(struct open_request *req);
};

/* struct open_request - mini sock to represent a connection request
 */
struct open_request {
	struct open_request		*dl_next; /* Must be first member! */
	u16				mss;
	u8				retrans;
	u8				__pad;
	/* The following two fields can be easily recomputed I think -AK */
	u32				window_clamp; /* window clamp at creation time */
	u32				rcv_wnd;	  /* rcv_wnd offered first time */
	u32				ts_recent;
	unsigned long			expires;
	struct or_calltable		*class;
	struct sock			*sk;
};

static inline struct open_request *tcp_openreq_alloc(struct or_calltable *class)
{
	struct open_request *req = kmem_cache_alloc(class->slab, SLAB_ATOMIC);

	if (req != NULL)
		req->class = class;

	return req;
}

static inline void tcp_openreq_fastfree(struct open_request *req)
{
	kmem_cache_free(req->class->slab, req);
}

static inline void tcp_openreq_free(struct open_request *req)
{
	req->class->destructor(req);
	tcp_openreq_fastfree(req);
}

#endif /* _REQUEST_SOCK_H */
