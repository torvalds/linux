/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * Copyright (C) 2005 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 */

#ifndef O2CLUSTER_TCP_INTERNAL_H
#define O2CLUSTER_TCP_INTERNAL_H

#define O2NET_MSG_MAGIC           ((u16)0xfa55)
#define O2NET_MSG_STATUS_MAGIC    ((u16)0xfa56)
#define O2NET_MSG_KEEP_REQ_MAGIC  ((u16)0xfa57)
#define O2NET_MSG_KEEP_RESP_MAGIC ((u16)0xfa58)

/* same as hb delay, we're waiting for another node to recognize our hb */
#define O2NET_RECONNECT_DELAY_MS	O2HB_REGION_TIMEOUT_MS

/* we're delaying our quorum decision so that heartbeat will have timed
 * out truly dead nodes by the time we come around to making decisions
 * on their number */
#define O2NET_QUORUM_DELAY_MS	((o2hb_dead_threshold + 2) * O2HB_REGION_TIMEOUT_MS)

#define O2NET_KEEPALIVE_DELAY_SECS	5
#define O2NET_IDLE_TIMEOUT_SECS		10

/* 
 * This version number represents quite a lot, unfortunately.  It not
 * only represents the raw network message protocol on the wire but also
 * locking semantics of the file system using the protocol.  It should 
 * be somewhere else, I'm sure, but right now it isn't.
 *
 * New in version 2:
 * 	- full 64 bit i_size in the metadata lock lvbs
 * 	- introduction of "rw" lock and pushing meta/data locking down
 */
#define O2NET_PROTOCOL_VERSION 2ULL
struct o2net_handshake {
	__be64	protocol_version;
	__be64	connector_id;
};

struct o2net_node {
	/* this is never called from int/bh */
	spinlock_t			nn_lock;

	/* set the moment an sc is allocated and a connect is started */
	struct o2net_sock_container	*nn_sc;
	/* _valid is only set after the handshake passes and tx can happen */
	unsigned			nn_sc_valid:1;
	/* if this is set tx just returns it */
	int				nn_persistent_error;

	/* threads waiting for an sc to arrive wait on the wq for generation
	 * to increase.  it is increased when a connecting socket succeeds
	 * or fails or when an accepted socket is attached. */
	wait_queue_head_t		nn_sc_wq;

	struct idr			nn_status_idr;
	struct list_head		nn_status_list;

	/* connects are attempted from when heartbeat comes up until either hb
	 * goes down, the node is unconfigured, no connect attempts succeed
	 * before O2NET_CONN_IDLE_DELAY, or a connect succeeds.  connect_work
	 * is queued from set_nn_state both from hb up and from itself if a
	 * connect attempt fails and so can be self-arming.  shutdown is
	 * careful to first mark the nn such that no connects will be attempted
	 * before canceling delayed connect work and flushing the queue. */
	struct work_struct		nn_connect_work;
	unsigned long			nn_last_connect_attempt;

	/* this is queued as nodes come up and is canceled when a connection is
	 * established.  this expiring gives up on the node and errors out
	 * transmits */
	struct work_struct		nn_connect_expired;

	/* after we give up on a socket we wait a while before deciding
	 * that it is still heartbeating and that we should do some
	 * quorum work */
	struct work_struct		nn_still_up;
};

struct o2net_sock_container {
	struct kref		sc_kref;
	/* the next two are vaild for the life time of the sc */
	struct socket		*sc_sock;
	struct o2nm_node	*sc_node;

	/* all of these sc work structs hold refs on the sc while they are
	 * queued.  they should not be able to ref a freed sc.  the teardown
	 * race is with o2net_wq destruction in o2net_stop_listening() */

	/* rx and connect work are generated from socket callbacks.  sc
	 * shutdown removes the callbacks and then flushes the work queue */
	struct work_struct	sc_rx_work;
	struct work_struct	sc_connect_work;
	/* shutdown work is triggered in two ways.  the simple way is
	 * for a code path calls ensure_shutdown which gets a lock, removes
	 * the sc from the nn, and queues the work.  in this case the
	 * work is single-shot.  the work is also queued from a sock
	 * callback, though, and in this case the work will find the sc
	 * still on the nn and will call ensure_shutdown itself.. this
	 * ends up triggering the shutdown work again, though nothing
	 * will be done in that second iteration.  so work queue teardown
	 * has to be careful to remove the sc from the nn before waiting
	 * on the work queue so that the shutdown work doesn't remove the
	 * sc and rearm itself.
	 */
	struct work_struct	sc_shutdown_work;

	struct timer_list	sc_idle_timeout;
	struct work_struct	sc_keepalive_work;

	unsigned		sc_handshake_ok:1;

	struct page 		*sc_page;
	size_t			sc_page_off;

	/* original handlers for the sockets */
	void			(*sc_state_change)(struct sock *sk);
	void			(*sc_data_ready)(struct sock *sk, int bytes);

	struct timeval 		sc_tv_timer;
	struct timeval 		sc_tv_data_ready;
	struct timeval 		sc_tv_advance_start;
	struct timeval 		sc_tv_advance_stop;
	struct timeval 		sc_tv_func_start;
	struct timeval 		sc_tv_func_stop;
	u32			sc_msg_key;
	u16			sc_msg_type;
};

struct o2net_msg_handler {
	struct rb_node		nh_node;
	u32			nh_max_len;
	u32			nh_msg_type;
	u32			nh_key;
	o2net_msg_handler_func	*nh_func;
	o2net_msg_handler_func	*nh_func_data;
	struct kref		nh_kref;
	struct list_head	nh_unregister_item;
};

enum o2net_system_error {
	O2NET_ERR_NONE = 0,
	O2NET_ERR_NO_HNDLR,
	O2NET_ERR_OVERFLOW,
	O2NET_ERR_DIED,
	O2NET_ERR_MAX
};

struct o2net_status_wait {
	enum o2net_system_error	ns_sys_status;
	s32			ns_status;
	int			ns_id;
	wait_queue_head_t	ns_wq;
	struct list_head	ns_node_item;
};

#endif /* O2CLUSTER_TCP_INTERNAL_H */
