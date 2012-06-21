#ifndef __FS_CEPH_MESSENGER_H
#define __FS_CEPH_MESSENGER_H

#include <linux/kref.h>
#include <linux/mutex.h>
#include <linux/net.h>
#include <linux/radix-tree.h>
#include <linux/uio.h>
#include <linux/workqueue.h>

#include "types.h"
#include "buffer.h"

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
	int (*verify_authorizer_reply) (struct ceph_connection *con, int len);
	int (*invalidate_authorizer)(struct ceph_connection *con);

	/* there was some error on the socket (disconnect, whatever) */
	void (*fault) (struct ceph_connection *con);

	/* a remote host as terminated a message exchange session, and messages
	 * we sent (or they tried to send us) may be lost. */
	void (*peer_reset) (struct ceph_connection *con);

	struct ceph_msg * (*alloc_msg) (struct ceph_connection *con,
					struct ceph_msg_header *hdr,
					int *skip);
};

/* use format string %s%d */
#define ENTITY_NAME(n) ceph_entity_type_name((n).type), le64_to_cpu((n).num)

struct ceph_messenger {
	struct ceph_entity_inst inst;    /* my name+address */
	struct ceph_entity_addr my_enc_addr;

	bool nocrc;

	/*
	 * the global_seq counts connections i (attempt to) initiate
	 * in order to disambiguate certain connect race conditions.
	 */
	u32 global_seq;
	spinlock_t global_seq_lock;

	u32 supported_features;
	u32 required_features;
};

/*
 * a single message.  it contains a header (src, dest, message type, etc.),
 * footer (crc values, mainly), a "front" message body, and possibly a
 * data payload (stored in some number of pages).
 */
struct ceph_msg {
	struct ceph_msg_header hdr;	/* header */
	struct ceph_msg_footer footer;	/* footer */
	struct kvec front;              /* unaligned blobs of message */
	struct ceph_buffer *middle;
	struct page **pages;            /* data payload.  NOT OWNER. */
	unsigned nr_pages;              /* size of page array */
	unsigned page_alignment;        /* io offset in first page */
	struct ceph_pagelist *pagelist; /* instead of pages */

	struct ceph_connection *con;
	struct list_head list_head;

	struct kref kref;
	struct bio  *bio;		/* instead of pages/pagelist */
	struct bio  *bio_iter;		/* bio iterator */
	int bio_seg;			/* current bio segment */
	struct ceph_pagelist *trail;	/* the trailing part of the data */
	bool front_is_vmalloc;
	bool more_to_follow;
	bool needs_out_seq;
	int front_max;
	unsigned long ack_stamp;        /* tx: when we were acked */

	struct ceph_msgpool *pool;
};

struct ceph_msg_pos {
	int page, page_pos;  /* which page; offset in page */
	int data_pos;        /* offset in data payload */
	bool did_page_crc;   /* true if we've calculated crc for current page */
};

/* ceph connection fault delay defaults, for exponential backoff */
#define BASE_DELAY_INTERVAL	(HZ/2)
#define MAX_DELAY_INTERVAL	(5 * 60 * HZ)

/*
 * ceph_connection flag bits
 */

#define LOSSYTX         0  /* we can close channel or drop messages on errors */
#define KEEPALIVE_PENDING      3
#define WRITE_PENDING	4  /* we have data ready to send */
#define SOCK_CLOSED	11 /* socket state changed to closed */
#define BACKOFF         15

/*
 * ceph_connection states
 */
#define CONNECTING	1
#define NEGOTIATING	2
#define CONNECTED	5
#define STANDBY		8  /* no outgoing messages, socket closed.  we keep
			    * the ceph_connection around to maintain shared
			    * state with the peer. */
#define CLOSED		10 /* we've closed the connection */
#define OPENING         13 /* open connection w/ (possibly new) peer */

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

	unsigned peer_features;
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
	union {
		struct {  /* outgoing connection */
			struct ceph_msg_connect out_connect;
			struct ceph_msg_connect_reply in_reply;
		};
		struct {  /* incoming */
			struct ceph_msg_connect in_connect;
			struct ceph_msg_connect_reply out_reply;
		};
	};
	struct ceph_entity_addr actual_peer_addr;

	/* message out temps */
	struct ceph_msg *out_msg;        /* sending message (== tail of
					    out_sent) */
	bool out_msg_done;
	struct ceph_msg_pos out_msg_pos;

	struct kvec out_kvec[8],         /* sending header/footer data */
		*out_kvec_cur;
	int out_kvec_left;   /* kvec's left in out_kvec */
	int out_skip;        /* skip this many bytes */
	int out_kvec_bytes;  /* total bytes left */
	bool out_kvec_is_msg; /* kvec refers to out_msg */
	int out_more;        /* there is more data after the kvecs */
	__le64 out_temp_ack; /* for writing an ack */

	/* message in temps */
	struct ceph_msg_header in_hdr;
	struct ceph_msg *in_msg;
	struct ceph_msg_pos in_msg_pos;
	u32 in_front_crc, in_middle_crc, in_data_crc;  /* calculated crc */

	char in_tag;         /* protocol control byte */
	int in_base_pos;     /* bytes read */
	__le64 in_temp_ack;  /* for reading an ack */

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
			struct ceph_entity_addr *myaddr,
			u32 supported_features,
			u32 required_features,
			bool nocrc);

extern void ceph_con_init(struct ceph_connection *con, void *private,
			const struct ceph_connection_operations *ops,
			struct ceph_messenger *msgr, __u8 entity_type,
			__u64 entity_num);
extern void ceph_con_open(struct ceph_connection *con,
			  struct ceph_entity_addr *addr);
extern bool ceph_con_opened(struct ceph_connection *con);
extern void ceph_con_close(struct ceph_connection *con);
extern void ceph_con_send(struct ceph_connection *con, struct ceph_msg *msg);

extern void ceph_msg_revoke(struct ceph_msg *msg);
extern void ceph_msg_revoke_incoming(struct ceph_msg *msg);

extern void ceph_con_keepalive(struct ceph_connection *con);

extern struct ceph_msg *ceph_msg_new(int type, int front_len, gfp_t flags,
				     bool can_fail);
extern void ceph_msg_kfree(struct ceph_msg *m);


static inline struct ceph_msg *ceph_msg_get(struct ceph_msg *msg)
{
	kref_get(&msg->kref);
	return msg;
}
extern void ceph_msg_last_put(struct kref *kref);
static inline void ceph_msg_put(struct ceph_msg *msg)
{
	kref_put(&msg->kref, ceph_msg_last_put);
}

extern void ceph_msg_dump(struct ceph_msg *msg);

#endif
