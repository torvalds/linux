/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * VMware vSockets Driver
 *
 * Copyright (C) 2007-2013 VMware, Inc. All rights reserved.
 */

#ifndef __AF_VSOCK_H__
#define __AF_VSOCK_H__

#include <linux/kernel.h>
#include <linux/workqueue.h>
#include <net/sock.h>
#include <uapi/linux/vm_sockets.h>

#include "vsock_addr.h"

#define LAST_RESERVED_PORT 1023

#define VSOCK_HASH_SIZE         251
extern struct list_head vsock_bind_table[VSOCK_HASH_SIZE + 1];
extern struct list_head vsock_connected_table[VSOCK_HASH_SIZE];
extern spinlock_t vsock_table_lock;

#define vsock_sk(__sk)    ((struct vsock_sock *)__sk)
#define sk_vsock(__vsk)   (&(__vsk)->sk)

struct vsock_sock {
	/* sk must be the first member. */
	struct sock sk;
	const struct vsock_transport *transport;
	struct sockaddr_vm local_addr;
	struct sockaddr_vm remote_addr;
	/* Links for the global tables of bound and connected sockets. */
	struct list_head bound_table;
	struct list_head connected_table;
	/* Accessed without the socket lock held. This means it can never be
	 * modified outsided of socket create or destruct.
	 */
	bool trusted;
	bool cached_peer_allow_dgram;	/* Dgram communication allowed to
					 * cached peer?
					 */
	u32 cached_peer;  /* Context ID of last dgram destination check. */
	const struct cred *owner;
	/* Rest are SOCK_STREAM only. */
	long connect_timeout;
	/* Listening socket that this came from. */
	struct sock *listener;
	/* Used for pending list and accept queue during connection handshake.
	 * The listening socket is the head for both lists.  Sockets created
	 * for connection requests are placed in the pending list until they
	 * are connected, at which point they are put in the accept queue list
	 * so they can be accepted in accept().  If accept() cannot accept the
	 * connection, it is marked as rejected so the cleanup function knows
	 * to clean up the socket.
	 */
	struct list_head pending_links;
	struct list_head accept_queue;
	bool rejected;
	struct delayed_work connect_work;
	struct delayed_work pending_work;
	struct delayed_work close_work;
	bool close_work_scheduled;
	u32 peer_shutdown;
	bool sent_request;
	bool ignore_connecting_rst;

	/* Protected by lock_sock(sk) */
	u64 buffer_size;
	u64 buffer_min_size;
	u64 buffer_max_size;

	/* Private to transport. */
	void *trans;
};

s64 vsock_connectible_has_data(struct vsock_sock *vsk);
s64 vsock_stream_has_data(struct vsock_sock *vsk);
s64 vsock_stream_has_space(struct vsock_sock *vsk);
struct sock *vsock_create_connected(struct sock *parent);
void vsock_data_ready(struct sock *sk);

/**** TRANSPORT ****/

struct vsock_transport_recv_notify_data {
	u64 data1; /* Transport-defined. */
	u64 data2; /* Transport-defined. */
	bool notify_on_block;
};

struct vsock_transport_send_notify_data {
	u64 data1; /* Transport-defined. */
	u64 data2; /* Transport-defined. */
};

/* Transport features flags */
/* Transport provides host->guest communication */
#define VSOCK_TRANSPORT_F_H2G		0x00000001
/* Transport provides guest->host communication */
#define VSOCK_TRANSPORT_F_G2H		0x00000002
/* Transport provides DGRAM communication */
#define VSOCK_TRANSPORT_F_DGRAM		0x00000004
/* Transport provides local (loopback) communication */
#define VSOCK_TRANSPORT_F_LOCAL		0x00000008

struct vsock_transport {
	struct module *module;

	/* Initialize/tear-down socket. */
	int (*init)(struct vsock_sock *, struct vsock_sock *);
	void (*destruct)(struct vsock_sock *);
	void (*release)(struct vsock_sock *);

	/* Cancel all pending packets sent on vsock. */
	int (*cancel_pkt)(struct vsock_sock *vsk);

	/* Connections. */
	int (*connect)(struct vsock_sock *);

	/* DGRAM. */
	int (*dgram_bind)(struct vsock_sock *, struct sockaddr_vm *);
	int (*dgram_dequeue)(struct vsock_sock *vsk, struct msghdr *msg,
			     size_t len, int flags);
	int (*dgram_enqueue)(struct vsock_sock *, struct sockaddr_vm *,
			     struct msghdr *, size_t len);
	bool (*dgram_allow)(u32 cid, u32 port);

	/* STREAM. */
	/* TODO: stream_bind() */
	ssize_t (*stream_dequeue)(struct vsock_sock *, struct msghdr *,
				  size_t len, int flags);
	ssize_t (*stream_enqueue)(struct vsock_sock *, struct msghdr *,
				  size_t len);
	s64 (*stream_has_data)(struct vsock_sock *);
	s64 (*stream_has_space)(struct vsock_sock *);
	u64 (*stream_rcvhiwat)(struct vsock_sock *);
	bool (*stream_is_active)(struct vsock_sock *);
	bool (*stream_allow)(u32 cid, u32 port);

	/* SEQ_PACKET. */
	ssize_t (*seqpacket_dequeue)(struct vsock_sock *vsk, struct msghdr *msg,
				     int flags);
	int (*seqpacket_enqueue)(struct vsock_sock *vsk, struct msghdr *msg,
				 size_t len);
	bool (*seqpacket_allow)(u32 remote_cid);
	u32 (*seqpacket_has_data)(struct vsock_sock *vsk);

	/* Notification. */
	int (*notify_poll_in)(struct vsock_sock *, size_t, bool *);
	int (*notify_poll_out)(struct vsock_sock *, size_t, bool *);
	int (*notify_recv_init)(struct vsock_sock *, size_t,
		struct vsock_transport_recv_notify_data *);
	int (*notify_recv_pre_block)(struct vsock_sock *, size_t,
		struct vsock_transport_recv_notify_data *);
	int (*notify_recv_pre_dequeue)(struct vsock_sock *, size_t,
		struct vsock_transport_recv_notify_data *);
	int (*notify_recv_post_dequeue)(struct vsock_sock *, size_t,
		ssize_t, bool, struct vsock_transport_recv_notify_data *);
	int (*notify_send_init)(struct vsock_sock *,
		struct vsock_transport_send_notify_data *);
	int (*notify_send_pre_block)(struct vsock_sock *,
		struct vsock_transport_send_notify_data *);
	int (*notify_send_pre_enqueue)(struct vsock_sock *,
		struct vsock_transport_send_notify_data *);
	int (*notify_send_post_enqueue)(struct vsock_sock *, ssize_t,
		struct vsock_transport_send_notify_data *);
	/* sk_lock held by the caller */
	void (*notify_buffer_size)(struct vsock_sock *, u64 *);
	int (*notify_set_rcvlowat)(struct vsock_sock *vsk, int val);

	/* Shutdown. */
	int (*shutdown)(struct vsock_sock *, int);

	/* Addressing. */
	u32 (*get_local_cid)(void);

	/* Read a single skb */
	int (*read_skb)(struct vsock_sock *, skb_read_actor_t);

	/* Zero-copy. */
	bool (*msgzerocopy_allow)(void);
};

/**** CORE ****/

int vsock_core_register(const struct vsock_transport *t, int features);
void vsock_core_unregister(const struct vsock_transport *t);

/* The transport may downcast this to access transport-specific functions */
const struct vsock_transport *vsock_core_get_transport(struct vsock_sock *vsk);

/**** UTILS ****/

/* vsock_table_lock must be held */
static inline bool __vsock_in_bound_table(struct vsock_sock *vsk)
{
	return !list_empty(&vsk->bound_table);
}

/* vsock_table_lock must be held */
static inline bool __vsock_in_connected_table(struct vsock_sock *vsk)
{
	return !list_empty(&vsk->connected_table);
}

void vsock_add_pending(struct sock *listener, struct sock *pending);
void vsock_remove_pending(struct sock *listener, struct sock *pending);
void vsock_enqueue_accept(struct sock *listener, struct sock *connected);
void vsock_insert_connected(struct vsock_sock *vsk);
void vsock_remove_bound(struct vsock_sock *vsk);
void vsock_remove_connected(struct vsock_sock *vsk);
struct sock *vsock_find_bound_socket(struct sockaddr_vm *addr);
struct sock *vsock_find_connected_socket(struct sockaddr_vm *src,
					 struct sockaddr_vm *dst);
void vsock_remove_sock(struct vsock_sock *vsk);
void vsock_for_each_connected_socket(struct vsock_transport *transport,
				     void (*fn)(struct sock *sk));
int vsock_assign_transport(struct vsock_sock *vsk, struct vsock_sock *psk);
bool vsock_find_cid(unsigned int cid);

/**** TAP ****/

struct vsock_tap {
	struct net_device *dev;
	struct module *module;
	struct list_head list;
};

int vsock_add_tap(struct vsock_tap *vt);
int vsock_remove_tap(struct vsock_tap *vt);
void vsock_deliver_tap(struct sk_buff *build_skb(void *opaque), void *opaque);
int vsock_connectible_recvmsg(struct socket *sock, struct msghdr *msg, size_t len,
			      int flags);
int vsock_dgram_recvmsg(struct socket *sock, struct msghdr *msg,
			size_t len, int flags);

#ifdef CONFIG_BPF_SYSCALL
extern struct proto vsock_proto;
int vsock_bpf_update_proto(struct sock *sk, struct sk_psock *psock, bool restore);
void __init vsock_bpf_build_proto(void);
#else
static inline void __init vsock_bpf_build_proto(void)
{}
#endif

static inline bool vsock_msgzerocopy_allow(const struct vsock_transport *t)
{
	return t->msgzerocopy_allow && t->msgzerocopy_allow();
}
#endif /* __AF_VSOCK_H__ */
