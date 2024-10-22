/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *   Copyright (C) 2018 Samsung Electronics Co., Ltd.
 */

#ifndef __KSMBD_CONNECTION_H__
#define __KSMBD_CONNECTION_H__

#include <linux/list.h>
#include <linux/ip.h>
#include <net/sock.h>
#include <net/tcp.h>
#include <net/inet_connection_sock.h>
#include <net/request_sock.h>
#include <linux/kthread.h>
#include <linux/nls.h>
#include <linux/unicode.h>

#include "smb_common.h"
#include "ksmbd_work.h"

#define KSMBD_SOCKET_BACKLOG		16

enum {
	KSMBD_SESS_NEW = 0,
	KSMBD_SESS_GOOD,
	KSMBD_SESS_EXITING,
	KSMBD_SESS_NEED_RECONNECT,
	KSMBD_SESS_NEED_NEGOTIATE,
	KSMBD_SESS_RELEASING
};

struct ksmbd_stats {
	atomic_t			open_files_count;
	atomic64_t			request_served;
};

struct ksmbd_transport;

struct ksmbd_conn {
	struct smb_version_values	*vals;
	struct smb_version_ops		*ops;
	struct smb_version_cmds		*cmds;
	unsigned int			max_cmds;
	struct mutex			srv_mutex;
	int				status;
	unsigned int			cli_cap;
	char				*request_buf;
	struct ksmbd_transport		*transport;
	struct nls_table		*local_nls;
	struct unicode_map		*um;
	struct list_head		conns_list;
	struct rw_semaphore		session_lock;
	/* smb session 1 per user */
	struct xarray			sessions;
	unsigned long			last_active;
	/* How many request are running currently */
	atomic_t			req_running;
	/* References which are made for this Server object*/
	atomic_t			r_count;
	unsigned int			total_credits;
	unsigned int			outstanding_credits;
	spinlock_t			credits_lock;
	wait_queue_head_t		req_running_q;
	wait_queue_head_t		r_count_q;
	/* Lock to protect requests list*/
	spinlock_t			request_lock;
	struct list_head		requests;
	struct list_head		async_requests;
	int				connection_type;
	struct ksmbd_stats		stats;
	char				ClientGUID[SMB2_CLIENT_GUID_SIZE];
	struct ntlmssp_auth		ntlmssp;

	spinlock_t			llist_lock;
	struct list_head		lock_list;

	struct preauth_integrity_info	*preauth_info;

	bool				need_neg;
	unsigned int			auth_mechs;
	unsigned int			preferred_auth_mech;
	bool				sign;
	bool				use_spnego:1;
	__u16				cli_sec_mode;
	__u16				srv_sec_mode;
	/* dialect index that server chose */
	__u16				dialect;

	char				*mechToken;
	unsigned int			mechTokenLen;

	struct ksmbd_conn_ops	*conn_ops;

	/* Preauth Session Table */
	struct list_head		preauth_sess_table;

	struct sockaddr_storage		peer_addr;

	/* Identifier for async message */
	struct ida			async_ida;

	__le16				cipher_type;
	__le16				compress_algorithm;
	bool				posix_ext_supported;
	bool				signing_negotiated;
	__le16				signing_algorithm;
	bool				binding;
};

struct ksmbd_conn_ops {
	int	(*process_fn)(struct ksmbd_conn *conn);
	int	(*terminate_fn)(struct ksmbd_conn *conn);
};

struct ksmbd_transport_ops {
	int (*prepare)(struct ksmbd_transport *t);
	void (*disconnect)(struct ksmbd_transport *t);
	void (*shutdown)(struct ksmbd_transport *t);
	int (*read)(struct ksmbd_transport *t, char *buf,
		    unsigned int size, int max_retries);
	int (*writev)(struct ksmbd_transport *t, struct kvec *iovs, int niov,
		      int size, bool need_invalidate_rkey,
		      unsigned int remote_key);
	int (*rdma_read)(struct ksmbd_transport *t,
			 void *buf, unsigned int len,
			 struct smb2_buffer_desc_v1 *desc,
			 unsigned int desc_len);
	int (*rdma_write)(struct ksmbd_transport *t,
			  void *buf, unsigned int len,
			  struct smb2_buffer_desc_v1 *desc,
			  unsigned int desc_len);
};

struct ksmbd_transport {
	struct ksmbd_conn			*conn;
	const struct ksmbd_transport_ops	*ops;
};

#define KSMBD_TCP_RECV_TIMEOUT	(7 * HZ)
#define KSMBD_TCP_SEND_TIMEOUT	(5 * HZ)
#define KSMBD_TCP_PEER_SOCKADDR(c)	((struct sockaddr *)&((c)->peer_addr))

extern struct list_head conn_list;
extern struct rw_semaphore conn_list_lock;

bool ksmbd_conn_alive(struct ksmbd_conn *conn);
void ksmbd_conn_wait_idle(struct ksmbd_conn *conn);
int ksmbd_conn_wait_idle_sess_id(struct ksmbd_conn *curr_conn, u64 sess_id);
struct ksmbd_conn *ksmbd_conn_alloc(void);
void ksmbd_conn_free(struct ksmbd_conn *conn);
bool ksmbd_conn_lookup_dialect(struct ksmbd_conn *c);
int ksmbd_conn_write(struct ksmbd_work *work);
int ksmbd_conn_rdma_read(struct ksmbd_conn *conn,
			 void *buf, unsigned int buflen,
			 struct smb2_buffer_desc_v1 *desc,
			 unsigned int desc_len);
int ksmbd_conn_rdma_write(struct ksmbd_conn *conn,
			  void *buf, unsigned int buflen,
			  struct smb2_buffer_desc_v1 *desc,
			  unsigned int desc_len);
void ksmbd_conn_enqueue_request(struct ksmbd_work *work);
void ksmbd_conn_try_dequeue_request(struct ksmbd_work *work);
void ksmbd_conn_init_server_callbacks(struct ksmbd_conn_ops *ops);
int ksmbd_conn_handler_loop(void *p);
int ksmbd_conn_transport_init(void);
void ksmbd_conn_transport_destroy(void);
void ksmbd_conn_lock(struct ksmbd_conn *conn);
void ksmbd_conn_unlock(struct ksmbd_conn *conn);

/*
 * WARNING
 *
 * This is a hack. We will move status to a proper place once we land
 * a multi-sessions support.
 */
static inline bool ksmbd_conn_good(struct ksmbd_conn *conn)
{
	return READ_ONCE(conn->status) == KSMBD_SESS_GOOD;
}

static inline bool ksmbd_conn_need_negotiate(struct ksmbd_conn *conn)
{
	return READ_ONCE(conn->status) == KSMBD_SESS_NEED_NEGOTIATE;
}

static inline bool ksmbd_conn_need_reconnect(struct ksmbd_conn *conn)
{
	return READ_ONCE(conn->status) == KSMBD_SESS_NEED_RECONNECT;
}

static inline bool ksmbd_conn_exiting(struct ksmbd_conn *conn)
{
	return READ_ONCE(conn->status) == KSMBD_SESS_EXITING;
}

static inline bool ksmbd_conn_releasing(struct ksmbd_conn *conn)
{
	return READ_ONCE(conn->status) == KSMBD_SESS_RELEASING;
}

static inline void ksmbd_conn_set_new(struct ksmbd_conn *conn)
{
	WRITE_ONCE(conn->status, KSMBD_SESS_NEW);
}

static inline void ksmbd_conn_set_good(struct ksmbd_conn *conn)
{
	WRITE_ONCE(conn->status, KSMBD_SESS_GOOD);
}

static inline void ksmbd_conn_set_need_negotiate(struct ksmbd_conn *conn)
{
	WRITE_ONCE(conn->status, KSMBD_SESS_NEED_NEGOTIATE);
}

static inline void ksmbd_conn_set_need_reconnect(struct ksmbd_conn *conn)
{
	WRITE_ONCE(conn->status, KSMBD_SESS_NEED_RECONNECT);
}

static inline void ksmbd_conn_set_exiting(struct ksmbd_conn *conn)
{
	WRITE_ONCE(conn->status, KSMBD_SESS_EXITING);
}

static inline void ksmbd_conn_set_releasing(struct ksmbd_conn *conn)
{
	WRITE_ONCE(conn->status, KSMBD_SESS_RELEASING);
}

void ksmbd_all_conn_set_status(u64 sess_id, u32 status);
#endif /* __CONNECTION_H__ */
