/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Generic netlink handshake service
 *
 * Author: Chuck Lever <chuck.lever@oracle.com>
 *
 * Copyright (c) 2023, Oracle and/or its affiliates.
 */

#ifndef _INTERNAL_HANDSHAKE_H
#define _INTERNAL_HANDSHAKE_H

/* Per-net namespace context */
struct handshake_net {
	spinlock_t		hn_lock;	/* protects next 3 fields */
	int			hn_pending;
	int			hn_pending_max;
	struct list_head	hn_requests;

	unsigned long		hn_flags;
};

enum hn_flags_bits {
	HANDSHAKE_F_NET_DRAINING,
};

struct handshake_proto;

/* One handshake request */
struct handshake_req {
	struct list_head		hr_list;
	struct rhash_head		hr_rhash;
	unsigned long			hr_flags;
	const struct handshake_proto	*hr_proto;
	struct sock			*hr_sk;
	void				(*hr_odestruct)(struct sock *sk);

	/* Always the last field */
	char				hr_priv[];
};

enum hr_flags_bits {
	HANDSHAKE_F_REQ_COMPLETED,
};

/* Invariants for all handshake requests for one transport layer
 * security protocol
 */
struct handshake_proto {
	int			hp_handler_class;
	size_t			hp_privsize;
	unsigned long		hp_flags;

	int			(*hp_accept)(struct handshake_req *req,
					     struct genl_info *info, int fd);
	void			(*hp_done)(struct handshake_req *req,
					   unsigned int status,
					   struct genl_info *info);
	void			(*hp_destroy)(struct handshake_req *req);
};

enum hp_flags_bits {
	HANDSHAKE_F_PROTO_NOTIFY,
};

/* netlink.c */
int handshake_genl_notify(struct net *net, const struct handshake_proto *proto,
			  gfp_t flags);
struct nlmsghdr *handshake_genl_put(struct sk_buff *msg,
				    struct genl_info *info);
struct handshake_net *handshake_pernet(struct net *net);

/* request.c */
struct handshake_req *handshake_req_alloc(const struct handshake_proto *proto,
					  gfp_t flags);
int handshake_req_hash_init(void);
void handshake_req_hash_destroy(void);
void *handshake_req_private(struct handshake_req *req);
struct handshake_req *handshake_req_hash_lookup(struct sock *sk);
struct handshake_req *handshake_req_next(struct handshake_net *hn, int class);
int handshake_req_submit(struct socket *sock, struct handshake_req *req,
			 gfp_t flags);
void handshake_complete(struct handshake_req *req, unsigned int status,
			struct genl_info *info);
bool handshake_req_cancel(struct sock *sk);

#endif /* _INTERNAL_HANDSHAKE_H */
