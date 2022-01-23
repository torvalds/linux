/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __FS_CEPH_MESSENGER_H
#define __FS_CEPH_MESSENGER_H

#include <linux/bvec.h>
#include <linux/crypto.h>
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
	int (*add_authorizer_challenge)(struct ceph_connection *con,
					void *challenge_buf,
					int challenge_buf_len);
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

	/* msgr2 authentication exchange */
	int (*get_auth_request)(struct ceph_connection *con,
				void *buf, int *buf_len,
				void **authorizer, int *authorizer_len);
	int (*handle_auth_reply_more)(struct ceph_connection *con,
				      void *reply, int reply_len,
				      void *buf, int *buf_len,
				      void **authorizer, int *authorizer_len);
	int (*handle_auth_done)(struct ceph_connection *con,
				u64 global_id, void *reply, int reply_len,
				u8 *session_key, int *session_key_len,
				u8 *con_secret, int *con_secret_len);
	int (*handle_auth_bad_method)(struct ceph_connection *con,
				      int used_proto, int result,
				      const int *allowed_protos, int proto_cnt,
				      const int *allowed_modes, int mode_cnt);
};

/* use format string %s%lld */
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
	CEPH_MSG_DATA_BVECS,	/* data source/destination is a bio_vec array */
};

#ifdef CONFIG_BLOCK

struct ceph_bio_iter {
	struct bio *bio;
	struct bvec_iter iter;
};

#define __ceph_bio_iter_advance_step(it, n, STEP) do {			      \
	unsigned int __n = (n), __cur_n;				      \
									      \
	while (__n) {							      \
		BUG_ON(!(it)->iter.bi_size);				      \
		__cur_n = min((it)->iter.bi_size, __n);			      \
		(void)(STEP);						      \
		bio_advance_iter((it)->bio, &(it)->iter, __cur_n);	      \
		if (!(it)->iter.bi_size && (it)->bio->bi_next) {	      \
			dout("__ceph_bio_iter_advance_step next bio\n");      \
			(it)->bio = (it)->bio->bi_next;			      \
			(it)->iter = (it)->bio->bi_iter;		      \
		}							      \
		__n -= __cur_n;						      \
	}								      \
} while (0)

/*
 * Advance @it by @n bytes.
 */
#define ceph_bio_iter_advance(it, n)					      \
	__ceph_bio_iter_advance_step(it, n, 0)

/*
 * Advance @it by @n bytes, executing BVEC_STEP for each bio_vec.
 */
#define ceph_bio_iter_advance_step(it, n, BVEC_STEP)			      \
	__ceph_bio_iter_advance_step(it, n, ({				      \
		struct bio_vec bv;					      \
		struct bvec_iter __cur_iter;				      \
									      \
		__cur_iter = (it)->iter;				      \
		__cur_iter.bi_size = __cur_n;				      \
		__bio_for_each_segment(bv, (it)->bio, __cur_iter, __cur_iter) \
			(void)(BVEC_STEP);				      \
	}))

#endif /* CONFIG_BLOCK */

struct ceph_bvec_iter {
	struct bio_vec *bvecs;
	struct bvec_iter iter;
};

#define __ceph_bvec_iter_advance_step(it, n, STEP) do {			      \
	BUG_ON((n) > (it)->iter.bi_size);				      \
	(void)(STEP);							      \
	bvec_iter_advance((it)->bvecs, &(it)->iter, (n));		      \
} while (0)

/*
 * Advance @it by @n bytes.
 */
#define ceph_bvec_iter_advance(it, n)					      \
	__ceph_bvec_iter_advance_step(it, n, 0)

/*
 * Advance @it by @n bytes, executing BVEC_STEP for each bio_vec.
 */
#define ceph_bvec_iter_advance_step(it, n, BVEC_STEP)			      \
	__ceph_bvec_iter_advance_step(it, n, ({				      \
		struct bio_vec bv;					      \
		struct bvec_iter __cur_iter;				      \
									      \
		__cur_iter = (it)->iter;				      \
		__cur_iter.bi_size = (n);				      \
		for_each_bvec(bv, (it)->bvecs, __cur_iter, __cur_iter)	      \
			(void)(BVEC_STEP);				      \
	}))

#define ceph_bvec_iter_shorten(it, n) do {				      \
	BUG_ON((n) > (it)->iter.bi_size);				      \
	(it)->iter.bi_size = (n);					      \
} while (0)

struct ceph_msg_data {
	enum ceph_msg_data_type		type;
	union {
#ifdef CONFIG_BLOCK
		struct {
			struct ceph_bio_iter	bio_pos;
			u32			bio_length;
		};
#endif /* CONFIG_BLOCK */
		struct ceph_bvec_iter	bvec_pos;
		struct {
			struct page	**pages;
			size_t		length;		/* total # bytes */
			unsigned int	alignment;	/* first page */
			bool		own_pages;
		};
		struct ceph_pagelist	*pagelist;
	};
};

struct ceph_msg_data_cursor {
	size_t			total_resid;	/* across all data items */

	struct ceph_msg_data	*data;		/* current data item */
	size_t			resid;		/* bytes not yet consumed */
	bool			last_piece;	/* current is last piece */
	bool			need_crc;	/* crc update needed */
	union {
#ifdef CONFIG_BLOCK
		struct ceph_bio_iter	bio_iter;
#endif /* CONFIG_BLOCK */
		struct bvec_iter	bvec_iter;
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
	struct ceph_msg_data		*data;
	int				num_data_items;
	int				max_data_items;
	struct ceph_msg_data_cursor	cursor;

	struct ceph_connection *con;
	struct list_head list_head;	/* links for connection lists */

	struct kref kref;
	bool more_to_follow;
	bool needs_out_seq;
	int front_alloc_len;

	struct ceph_msgpool *pool;
};

/*
 * connection states
 */
#define CEPH_CON_S_CLOSED		1
#define CEPH_CON_S_PREOPEN		2
#define CEPH_CON_S_V1_BANNER		3
#define CEPH_CON_S_V1_CONNECT_MSG	4
#define CEPH_CON_S_V2_BANNER_PREFIX	5
#define CEPH_CON_S_V2_BANNER_PAYLOAD	6
#define CEPH_CON_S_V2_HELLO		7
#define CEPH_CON_S_V2_AUTH		8
#define CEPH_CON_S_V2_AUTH_SIGNATURE	9
#define CEPH_CON_S_V2_SESSION_CONNECT	10
#define CEPH_CON_S_V2_SESSION_RECONNECT	11
#define CEPH_CON_S_OPEN			12
#define CEPH_CON_S_STANDBY		13

/*
 * ceph_connection flag bits
 */
#define CEPH_CON_F_LOSSYTX		0  /* we can close channel or drop
					      messages on errors */
#define CEPH_CON_F_KEEPALIVE_PENDING	1  /* we need to send a keepalive */
#define CEPH_CON_F_WRITE_PENDING	2  /* we have data ready to send */
#define CEPH_CON_F_SOCK_CLOSED		3  /* socket state changed to closed */
#define CEPH_CON_F_BACKOFF		4  /* need to retry queuing delayed
					      work */

/* ceph connection fault delay defaults, for exponential backoff */
#define BASE_DELAY_INTERVAL	(HZ / 4)
#define MAX_DELAY_INTERVAL	(15 * HZ)

struct ceph_connection_v1_info {
	struct kvec out_kvec[8],         /* sending header/footer data */
		*out_kvec_cur;
	int out_kvec_left;   /* kvec's left in out_kvec */
	int out_skip;        /* skip this many bytes */
	int out_kvec_bytes;  /* total bytes left */
	bool out_more;       /* there is more data after the kvecs */
	bool out_msg_done;

	struct ceph_auth_handshake *auth;
	int auth_retry;       /* true if we need a newer authorizer */

	/* connection negotiation temps */
	u8 in_banner[CEPH_BANNER_MAX_LEN];
	struct ceph_entity_addr actual_peer_addr;
	struct ceph_entity_addr peer_addr_for_me;
	struct ceph_msg_connect out_connect;
	struct ceph_msg_connect_reply in_reply;

	int in_base_pos;     /* bytes read */

	/* message in temps */
	u8 in_tag;           /* protocol control byte */
	struct ceph_msg_header in_hdr;
	__le64 in_temp_ack;  /* for reading an ack */

	/* message out temps */
	struct ceph_msg_header out_hdr;
	__le64 out_temp_ack;  /* for writing an ack */
	struct ceph_timespec out_temp_keepalive2;  /* for writing keepalive2
						      stamp */

	u32 connect_seq;      /* identify the most recent connection
				 attempt for this session */
	u32 peer_global_seq;  /* peer's global seq for this connection */
};

#define CEPH_CRC_LEN			4
#define CEPH_GCM_KEY_LEN		16
#define CEPH_GCM_IV_LEN			sizeof(struct ceph_gcm_nonce)
#define CEPH_GCM_BLOCK_LEN		16
#define CEPH_GCM_TAG_LEN		16

#define CEPH_PREAMBLE_LEN		32
#define CEPH_PREAMBLE_INLINE_LEN	48
#define CEPH_PREAMBLE_PLAIN_LEN		CEPH_PREAMBLE_LEN
#define CEPH_PREAMBLE_SECURE_LEN	(CEPH_PREAMBLE_LEN +		\
					 CEPH_PREAMBLE_INLINE_LEN +	\
					 CEPH_GCM_TAG_LEN)
#define CEPH_EPILOGUE_PLAIN_LEN		(1 + 3 * CEPH_CRC_LEN)
#define CEPH_EPILOGUE_SECURE_LEN	(CEPH_GCM_BLOCK_LEN + CEPH_GCM_TAG_LEN)

#define CEPH_FRAME_MAX_SEGMENT_COUNT	4

struct ceph_frame_desc {
	int fd_tag;  /* FRAME_TAG_* */
	int fd_seg_cnt;
	int fd_lens[CEPH_FRAME_MAX_SEGMENT_COUNT];  /* logical */
	int fd_aligns[CEPH_FRAME_MAX_SEGMENT_COUNT];
};

struct ceph_gcm_nonce {
	__le32 fixed;
	__le64 counter __packed;
};

struct ceph_connection_v2_info {
	struct iov_iter in_iter;
	struct kvec in_kvecs[5];  /* recvmsg */
	struct bio_vec in_bvec;  /* recvmsg (in_cursor) */
	int in_kvec_cnt;
	int in_state;  /* IN_S_* */

	struct iov_iter out_iter;
	struct kvec out_kvecs[8];  /* sendmsg */
	struct bio_vec out_bvec;  /* sendpage (out_cursor, out_zero),
				     sendmsg (out_enc_pages) */
	int out_kvec_cnt;
	int out_state;  /* OUT_S_* */

	int out_zero;  /* # of zero bytes to send */
	bool out_iter_sendpage;  /* use sendpage if possible */

	struct ceph_frame_desc in_desc;
	struct ceph_msg_data_cursor in_cursor;
	struct ceph_msg_data_cursor out_cursor;

	struct crypto_shash *hmac_tfm;  /* post-auth signature */
	struct crypto_aead *gcm_tfm;  /* on-wire encryption */
	struct aead_request *gcm_req;
	struct crypto_wait gcm_wait;
	struct ceph_gcm_nonce in_gcm_nonce;
	struct ceph_gcm_nonce out_gcm_nonce;

	struct page **in_enc_pages;
	int in_enc_page_cnt;
	int in_enc_resid;
	int in_enc_i;
	struct page **out_enc_pages;
	int out_enc_page_cnt;
	int out_enc_resid;
	int out_enc_i;

	int con_mode;  /* CEPH_CON_MODE_* */

	void *conn_bufs[16];
	int conn_buf_cnt;

	struct kvec in_sign_kvecs[8];
	struct kvec out_sign_kvecs[8];
	int in_sign_kvec_cnt;
	int out_sign_kvec_cnt;

	u64 client_cookie;
	u64 server_cookie;
	u64 global_seq;
	u64 connect_seq;
	u64 peer_global_seq;

	u8 in_buf[CEPH_PREAMBLE_SECURE_LEN];
	u8 out_buf[CEPH_PREAMBLE_SECURE_LEN];
	struct {
		u8 late_status;  /* FRAME_LATE_STATUS_* */
		union {
			struct {
				u32 front_crc;
				u32 middle_crc;
				u32 data_crc;
			} __packed;
			u8 pad[CEPH_GCM_BLOCK_LEN - 1];
		};
	} out_epil;
};

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

	int state;  /* CEPH_CON_S_* */
	atomic_t sock_state;
	struct socket *sock;

	unsigned long flags;  /* CEPH_CON_F_* */
	const char *error_msg;  /* error message, if any */

	struct ceph_entity_name peer_name; /* peer name */
	struct ceph_entity_addr peer_addr; /* peer address */
	u64 peer_features;

	struct mutex mutex;

	/* out queue */
	struct list_head out_queue;
	struct list_head out_sent;   /* sending or sent but unacked */
	u64 out_seq;		     /* last message queued for send */

	u64 in_seq, in_seq_acked;  /* last message received, acked */

	struct ceph_msg *in_msg;
	struct ceph_msg *out_msg;        /* sending message (== tail of
					    out_sent) */

	u32 in_front_crc, in_middle_crc, in_data_crc;  /* calculated crc */

	struct timespec64 last_keepalive_ack; /* keepalive2 ack stamp */

	struct delayed_work work;	    /* send|recv work */
	unsigned long       delay;          /* current delay interval */

	union {
		struct ceph_connection_v1_info v1;
		struct ceph_connection_v2_info v2;
	};
};

extern struct page *ceph_zero_page;

void ceph_con_flag_clear(struct ceph_connection *con, unsigned long con_flag);
void ceph_con_flag_set(struct ceph_connection *con, unsigned long con_flag);
bool ceph_con_flag_test(struct ceph_connection *con, unsigned long con_flag);
bool ceph_con_flag_test_and_clear(struct ceph_connection *con,
				  unsigned long con_flag);
bool ceph_con_flag_test_and_set(struct ceph_connection *con,
				unsigned long con_flag);

void ceph_encode_my_addr(struct ceph_messenger *msgr);

int ceph_tcp_connect(struct ceph_connection *con);
int ceph_con_close_socket(struct ceph_connection *con);
void ceph_con_reset_session(struct ceph_connection *con);

u32 ceph_get_global_seq(struct ceph_messenger *msgr, u32 gt);
void ceph_con_discard_sent(struct ceph_connection *con, u64 ack_seq);
void ceph_con_discard_requeued(struct ceph_connection *con, u64 reconnect_seq);

void ceph_msg_data_cursor_init(struct ceph_msg_data_cursor *cursor,
			       struct ceph_msg *msg, size_t length);
struct page *ceph_msg_data_next(struct ceph_msg_data_cursor *cursor,
				size_t *page_offset, size_t *length,
				bool *last_piece);
void ceph_msg_data_advance(struct ceph_msg_data_cursor *cursor, size_t bytes);

u32 ceph_crc32c_page(u32 crc, struct page *page, unsigned int page_offset,
		     unsigned int length);

bool ceph_addr_is_blank(const struct ceph_entity_addr *addr);
int ceph_addr_port(const struct ceph_entity_addr *addr);
void ceph_addr_set_port(struct ceph_entity_addr *addr, int p);

void ceph_con_process_message(struct ceph_connection *con);
int ceph_con_in_msg_alloc(struct ceph_connection *con,
			  struct ceph_msg_header *hdr, int *skip);
void ceph_con_get_out_msg(struct ceph_connection *con);

/* messenger_v1.c */
int ceph_con_v1_try_read(struct ceph_connection *con);
int ceph_con_v1_try_write(struct ceph_connection *con);
void ceph_con_v1_revoke(struct ceph_connection *con);
void ceph_con_v1_revoke_incoming(struct ceph_connection *con);
bool ceph_con_v1_opened(struct ceph_connection *con);
void ceph_con_v1_reset_session(struct ceph_connection *con);
void ceph_con_v1_reset_protocol(struct ceph_connection *con);

/* messenger_v2.c */
int ceph_con_v2_try_read(struct ceph_connection *con);
int ceph_con_v2_try_write(struct ceph_connection *con);
void ceph_con_v2_revoke(struct ceph_connection *con);
void ceph_con_v2_revoke_incoming(struct ceph_connection *con);
bool ceph_con_v2_opened(struct ceph_connection *con);
void ceph_con_v2_reset_session(struct ceph_connection *con);
void ceph_con_v2_reset_protocol(struct ceph_connection *con);


extern const char *ceph_pr_addr(const struct ceph_entity_addr *addr);

extern int ceph_parse_ips(const char *c, const char *end,
			  struct ceph_entity_addr *addr,
			  int max_count, int *count, char delim);

extern int ceph_msgr_init(void);
extern void ceph_msgr_exit(void);
extern void ceph_msgr_flush(void);

extern void ceph_messenger_init(struct ceph_messenger *msgr,
				struct ceph_entity_addr *myaddr);
extern void ceph_messenger_fini(struct ceph_messenger *msgr);
extern void ceph_messenger_reset_nonce(struct ceph_messenger *msgr);

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

void ceph_msg_data_add_pages(struct ceph_msg *msg, struct page **pages,
			     size_t length, size_t alignment, bool own_pages);
extern void ceph_msg_data_add_pagelist(struct ceph_msg *msg,
				struct ceph_pagelist *pagelist);
#ifdef CONFIG_BLOCK
void ceph_msg_data_add_bio(struct ceph_msg *msg, struct ceph_bio_iter *bio_pos,
			   u32 length);
#endif /* CONFIG_BLOCK */
void ceph_msg_data_add_bvecs(struct ceph_msg *msg,
			     struct ceph_bvec_iter *bvec_pos);

struct ceph_msg *ceph_msg_new2(int type, int front_len, int max_data_items,
			       gfp_t flags, bool can_fail);
extern struct ceph_msg *ceph_msg_new(int type, int front_len, gfp_t flags,
				     bool can_fail);

extern struct ceph_msg *ceph_msg_get(struct ceph_msg *msg);
extern void ceph_msg_put(struct ceph_msg *msg);

extern void ceph_msg_dump(struct ceph_msg *msg);

#endif
