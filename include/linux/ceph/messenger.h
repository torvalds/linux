/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __FS_CEPH_MESSENGER_H
#define __FS_CEPH_MESSENGER_H

#include <linux/bvec.h>
#include <linux/kref.h>
#include <linux/mutex.h>
#include <linux/net.h>
#include <linux/radix-tree.h>
#include <linux/uio.h>
#include <linux/workqueue.h>
#include <net/net_namespace.h>

#include <linux/ceph/types.h>
#include <linux/ceph/buffer.h>

struct ceph_msg;
struct ceph_connection;

/*
 * Ceph defines these callbacks for handling connection events.
 */
struct ceph_connection_operations {
	struct ceph_connection *(*get)(struct ceph_connection *);
	void (*put)(struct ceph_connection *);

	/* handle an incoming message. */
	void (*dispatch) (struct ceph_connection *con, struct ceph_msg *m);

	/* authorize an outgoing connection */
	struct ceph_auth_handshake *(*get_authorizer) (
				struct ceph_connection *con,
			       int *proto, int force_new);
	int (*verify_authorizer_reply) (struct ceph_connection *con);
	int (*invalidate_authorizer)(struct ceph_connection *con);

	/* there was some error on the socket (disconnect, whatever) */
	void (*fault) (struct ceph_connection *con);

	/* a remote host as terminated a message exchange session, and messages
	 * we sent (or they tried to send us) may be lost. */
	void (*peer_reset) (struct ceph_connection *con);

	struct ceph_msg * (*alloc_msg) (struct ceph_connection *con,
					struct ceph_msg_header *hdr,
					int *skip);

	void (*reencode_message) (struct ceph_msg *msg);

	int (*sign_message) (struct ceph_msg *msg);
	int (*check_message_signature) (struct ceph_msg *msg);
};

/* use format string %s%d */
#define ENTITY_NAME(n) ceph_entity_type_name((n).type), le64_to_cpu((n).num)

struct ceph_messenger {
	struct ceph_entity_inst inst;    /* my name+address */
	struct ceph_entity_addr my_enc_addr;

	atomic_t stopping;
	possible_net_t net;

	/*
	 * the global_seq counts connections i (attempt to) initiate
	 * in order to disambiguate certain connect race conditions.
	 */
	u32 global_seq;
	spinlock_t global_seq_lock;
};

enum ceph_msg_data_type {
	CEPH_MSG_DATA_NONE,	/* message contains no data payload */
	CEPH_MSG_DATA_PAGES,	/* data source/destination is a page array */
	CEPH_MSG_DATA_PAGELIST,	/* data source/destination is a pagelist */
#ifdef CONFIG_BLOCK
	CEPH_MSG_DATA_BIO,	/* data source/destination is a bio list */
#endif /* CONFIG_BLOCK */
};

static __inline__ bool ceph_msg_data_type_valid(enum ceph_msg_data_type type)
{
	switch (type) {
	case CEPH_MSG_DATA_NONE:
	case CEPH_MSG_DATA_PAGES:
	case CEPH_MSG_DATA_PAGELIST:
#ifdef CONFIG_BLOCK
	case CEPH_MSG_DATA_BIO:
#endif /* CONFIG_BLOCK */
		return true;
	default:
		return false;
	}
}

struct ceph_msg_data {
	struct list_head		links;	/* ceph_msg->data */
	enum ceph_msg_data_type		type;
	union {
#ifdef CONFIG_BLOCK
		struct {
			struct bio	*bio;
			size_t		bio_length;
		};
#endif /* CONFIG_BLOCK */
		struct {
			struct page	**pages;	/* NOT OWNER. */
			size_t		length;		/* total # bytes */
			unsigned int	alignment;	/* first page */
		};
		struct ceph_pagelist	*pagelist;
	};
};

struct ceph_msg_data_cursor {
	size_t			total_resid;	/* across all data items */
	struct list_head	*data_head;	/* = &ceph_msg->data */

	struct ceph_msg_data	*data;		/* current data item */
	size_t			resid;		/* bytes not yet consumed */
	bool			last_piece;	/* current is last piece */
	bool			need_crc;	/* crc update needed */
	union {
#ifdef CONFIG_BLOCK
		struct {				/* bio */
			struct bio	*bio;		/* bio from list */
			struct bvec_iter bvec_iter;
		};
#endif /* CONFIG_BLOCK */
		struct {				/* pages */
			unsigned int	page_offset;	/* offset in page */
			unsigned short	page_index;	/* index in array */
			unsigned short	page_count;	/* pages in array */
		};
		struct {				/* pagelist */
			struct page	*page;		/* page from list */
			size_t		offset;		/* bytes from list */
		};
	};
};

/*
 * a single message.  it contains a header (src, dest, message type, etc.),
 * footer (crc values, mainly), a "front" message body, and possibly a
 * data payload (stored in some number of pages).
 */
struct ceph_msg {
	struct ceph_msg_header hdr;	/* header */
	union {
		struct ceph_msg_footer footer;		/* footer */
		struct ceph_msg_footer_old old_footer;	/* old format footer */
	};
	struct kvec front;              /* unaligned blobs of message */
	struct ceph_buffer *middle;

	size_t				data_length;
	struct list_head		data;
	struct ceph_msg_data_cursor	cursor;

	struct ceph_connection *con;
	struct list_head list_head;	/* links for connection lists */

	struct kref kref;
	bool more_to_follow;
	bool needs_out_seq;
	int front_alloc_len;
	unsigned long ack_stamp;        /* tx: when we were acked */

	struct ceph_msgpool *pool;
};

/* ceph connection fault delay defaults, for exponential backoff */
#define BASE_DELAY_INTERVAL	(HZ/2)
#define MAX_DELAY_INTERVAL	(5 * 60 * HZ)

/*
 * A single connection with another host.
 *
 * We maintain a queue of outgoing messages, and some session state to
 * ensure that we can preserve the lossless, ordered delivery of
 * messages in the case of a TCP disconnect.
 */
struct ceph_connection {
	void *private;

	const struct ceph_connection_operations *ops;

	struct ceph_messenger *msgr;

	atomic_t sock_state;
	struct socket *sock;
	struct ceph_entity_addr peer_addr; /* peer address */
	struct ceph_entity_addr peer_addr_for_me;

	unsigned long flags;
	unsigned long state;
	const char *error_msg;  /* error message, if any */

	struct ceph_entity_name peer_name; /* peer name */

	u64 peer_features;
	u32 connect_seq;      /* identify the most recent connection
				 attempt for this connection, client */
	u32 peer_global_seq;  /* peer's global seq for this connection */

	int auth_retry;       /* true if we need a newer authorizer */
	void *auth_reply_buf;   /* where to put the authorizer reply */
	int auth_reply_buf_len;

	struct mutex mutex;

	/* out queue */
	struct list_head out_queue;
	struct list_head out_sent;   /* sending or sent but unacked */
	u64 out_seq;		     /* last message queued for send */

	u64 in_seq, in_seq_acked;  /* last message received, acked */

	/* connection negotiation temps */
	char in_banner[CEPH_BANNER_MAX_LEN];
	struct ceph_msg_connect out_connect;
	struct ceph_msg_connect_reply in_reply;
	struct ceph_entity_addr actual_peer_addr;

	/* message out temps */
	struct ceph_msg_header out_hdr;
	struct ceph_msg *out_msg;        /* sending message (== tail of
					    out_sent) */
	bool out_msg_done;

	struct kvec out_kvec[8],         /* sending header/footer data */
		*out_kvec_cur;
	int out_kvec_left;   /* kvec's left in out_kvec */
	int out_skip;        /* skip this many bytes */
	int out_kvec_bytes;  /* total bytes left */
	int out_more;        /* there is more data after the kvecs */
	__le64 out_temp_ack; /* for writing an ack */
	struct ceph_timespec out_temp_keepalive2; /* for writing keepalive2
						     stamp */

	/* message in temps */
	struct ceph_msg_header in_hdr;
	struct ceph_msg *in_msg;
	u32 in_front_crc, in_middle_crc, in_data_crc;  /* calculated crc */

	char in_tag;         /* protocol control byte */
	int in_base_pos;     /* bytes read */
	__le64 in_temp_ack;  /* for reading an ack */

	struct timespec last_keepalive_ack; /* keepalive2 ack stamp */

	struct delayed_work work;	    /* send|recv work */
	unsigned long       delay;          /* current delay interval */
};


extern const char *ceph_pr_addr(const struct sockaddr_storage *ss);
extern int ceph_parse_ips(const char *c, const char *end,
			  struct ceph_entity_addr *addr,
			  int max_count, int *count);


extern int ceph_msgr_init(void);
extern void ceph_msgr_exit(void);
extern void ceph_msgr_flush(void);

extern void ceph_messenger_init(struct ceph_messenger *msgr,
				struct ceph_entity_addr *myaddr);
extern void ceph_messenger_fini(struct ceph_messenger *msgr);

extern void ceph_con_init(struct ceph_connection *con, void *private,
			const struct ceph_connection_operations *ops,
			struct ceph_messenger *msgr);
extern void ceph_con_open(struct ceph_connection *con,
			  __u8 entity_type, __u64 entity_num,
			  struct ceph_entity_addr *addr);
extern bool ceph_con_opened(struct ceph_connection *con);
extern void ceph_con_close(struct ceph_connection *con);
extern void ceph_con_send(struct ceph_connection *con, struct ceph_msg *msg);

extern void ceph_msg_revoke(struct ceph_msg *msg);
extern void ceph_msg_revoke_incoming(struct ceph_msg *msg);

extern void ceph_con_keepalive(struct ceph_connection *con);
extern bool ceph_con_keepalive_expired(struct ceph_connection *con,
				       unsigned long interval);

extern void ceph_msg_data_add_pages(struct ceph_msg *msg, struct page **pages,
				size_t length, size_t alignment);
extern void ceph_msg_data_add_pagelist(struct ceph_msg *msg,
				struct ceph_pagelist *pagelist);
#ifdef CONFIG_BLOCK
extern void ceph_msg_data_add_bio(struct ceph_msg *msg, struct bio *bio,
				size_t length);
#endif /* CONFIG_BLOCK */

extern struct ceph_msg *ceph_msg_new(int type, int front_len, gfp_t flags,
				     bool can_fail);

extern struct ceph_msg *ceph_msg_get(struct ceph_msg *msg);
extern void ceph_msg_put(struct ceph_msg *msg);

extern void ceph_msg_dump(struct ceph_msg *msg);

#endif
