/*
 * xfrd-tcp.h - XFR (transfer) Daemon TCP system header file. Manages tcp conn.
 *
 * Copyright (c) 2001-2006, NLnet Labs. All rights reserved.
 *
 * See LICENSE for the license.
 *
 */

#ifndef XFRD_TCP_H
#define XFRD_TCP_H

#include "xfrd.h"
#ifdef HAVE_TLS_1_3
#include <openssl/ssl.h>
#endif


struct buffer;
struct xfrd_zone;
struct xfrd_soa;
struct xfrd_state;
struct region;
struct dname;
struct acl_options;

struct xfrd_tcp_pipeline;
typedef struct xfrd_tcp xfrd_tcp_type;
typedef struct xfrd_tcp_set xfrd_tcp_set_type;
/*
 * A set of xfrd tcp connections.
 */
struct xfrd_tcp_set {
	/* tcp connections, array, each has packet and read/wr state */
	struct xfrd_tcp_pipeline **tcp_state;
	/* max number of tcp connections, size of tcp_state array */
	int tcp_max;
	/* max number of simultaneous connections on a tcp_pipeline */
	int tcp_pipeline;
	/* number of TCP connections in use. */
	int tcp_count;
	/* TCP timeout. */
	int tcp_timeout;
	/* rbtree with pipelines sorted by master */
	rbtree_type* pipetree;
#ifdef HAVE_TLS_1_3
	/* XoT: SSL context */
	SSL_CTX* ssl_ctx;
#endif
	/* double linked list of zones waiting for a TCP connection */
	struct xfrd_zone *tcp_waiting_first, *tcp_waiting_last;
};

/*
 * Structure to keep track of an open tcp connection
 * The xfrd tcp connection is used to first make a request
 * Then to receive the answer packet(s).
 */
struct xfrd_tcp {
	/* tcp connection state */
	/* state: reading or writing */
	uint8_t is_reading;

	/* how many bytes have been read/written - total,
	   incl. tcp length bytes */
	uint32_t total_bytes;

	/* msg len bytes */
	uint16_t msglen;

	/* fd of connection. -1 means unconnected */
	int fd;

	/* packet buffer of connection */
	struct buffer* packet;
};

/* use illegal pointer value to denote skipped ID number.
 * if this does not work, we can allocate with malloc */
#define TCP_NULL_SKIP ((struct xfrd_zone*)-1)

/**
 * The per-id zone pointers, with TCP_NULL_SKIP or a zone pointer for the
 * ID value.
 */
struct xfrd_tcp_pipeline_id {
	/** rbtree node as first member, this is the key. */
	rbnode_type node;
	/** the ID of this member */
	uint16_t id;
	/** zone pointer or TCP_NULL_SKIP */
	struct xfrd_zone* zone;
	/** next free in free list */
	struct xfrd_tcp_pipeline_id* next_free;
};

/**
 * The tcp pipeline key structure. By ip_len, ip, num_unused and unique by
 * pointer value.
 */
struct xfrd_tcp_pipeline_key {
	/* the rbtree node, sorted by IP and nr of unused queries */
	rbnode_type node;
	/* destination IP address */
#ifdef INET6
	struct sockaddr_storage ip;
#else
	struct sockaddr_in ip;
#endif /* INET6 */
	socklen_t ip_len;
	/* number of unused IDs.  used IDs are waiting to send their query,
	 * or have been sent but not not all answer packets have been received.
	 * Sorted by num_unused, so a lookup smaller-equal for 65536 finds the
	 * connection to that master that has the most free IDs. */
	int num_unused;
	/* number of skip-set IDs (these are 'in-use') */
	int num_skip;
};

/**
 * Structure to keep track of a pipelined set of queries on
 * an open tcp connection.  The queries may be answered with
 * interleaved answer packets, the ID number disambiguates.
 * Sorted by the master IP address so you can use lookup with
 * smaller-or-equal to find the tcp connection most suitable.
 */
struct xfrd_tcp_pipeline {
	/* the key information for the tcp pipeline, in its own
	 * struct so it can be referenced on its own for comparison funcs */
	struct xfrd_tcp_pipeline_key key;

	int handler_added;
	/* the event handler for this pipe (it'll disambiguate by ID) */
	struct event handler;

	/* the tcp connection to use for reading */
	struct xfrd_tcp* tcp_r;
	/* the tcp connection to use for writing, if it is done successfully,
	 * then the first zone from the sendlist can be removed. */
	struct xfrd_tcp* tcp_w;
	/* once a byte has been written, handshake complete */
	int connection_established;
#ifdef HAVE_TLS_1_3
	/* XoT: SSL object */
	SSL *ssl;
	/* XoT: if SSL handshake is not done, handshake_want indicates the
	 * last error. This may be SSL_ERROR_WANT_READ or SSL_ERROR_WANT_WRITE
	 * when the handshake is still in progress.
	 */
	int  handshake_want;
	/* XoT: 1 if the SSL handshake has succeeded, 0 otherwise */
	int  handshake_done;
#endif

	/* list of queries that want to send, first to get write event,
	 * if NULL, no write event interest */
	struct xfrd_zone* tcp_send_first, *tcp_send_last;

	/* size of the id and unused arrays. */
	int pipe_num;
	/* list of free xfrd_tcp_pipeline_id nodes, these are not in the
	 * zone_per_id tree. preallocated at pipe_num amount. */
	struct xfrd_tcp_pipeline_id* pipe_id_free_list;
	/* The xfrd_zone pointers, per id number.
	 * The key is struct xfrd_tcp_pipeline_id.
	 * per-ID number the queries that have this ID number, every
	 * query owns one ID numbers (until it is done). NULL: unused
	 * When a query is done but not all answer-packets have been
	 * consumed for that ID number, the rest is skipped, this
	 * is denoted with the pointer-value TCP_NULL_SKIP, the ids that
	 * are skipped are not on the unused list.  They may be
	 * removed once the last answer packet is skipped.
	 * pipe_num-num_unused values are in the tree (either
	 * a zone pointer or SKIP) */
	rbtree_type* zone_per_id;
	/* Array of uint16_t, with ID values.
	 * unused ID numbers; the first part of the array contains the IDs */
	uint16_t* unused;
};

/* create set of tcp connections */
struct xfrd_tcp_set* xfrd_tcp_set_create(struct region* region, const char *tls_cert_bundle, int tcp_max, int tcp_pipeline);

/* init tcp state */
struct xfrd_tcp* xfrd_tcp_create(struct region* region, size_t bufsize);
/* obtain tcp connection for a zone (or wait) */
void xfrd_tcp_obtain(struct xfrd_tcp_set* set, struct xfrd_zone* zone);
/* release tcp connection for a zone (starts waiting) */
void xfrd_tcp_release(struct xfrd_tcp_set* set, struct xfrd_zone* zone);
/* release tcp pipe entirely (does not stop the zones inside it) */
void xfrd_tcp_pipe_release(struct xfrd_tcp_set* set,
	struct xfrd_tcp_pipeline* tp, int conn);
/* use tcp connection to start xfr */
void xfrd_tcp_setup_write_packet(struct xfrd_tcp_pipeline* tp,
	struct xfrd_zone* zone);
/* initialize tcp_state for a zone. Opens the connection. true on success.*/
int xfrd_tcp_open(struct xfrd_tcp_set* set, struct xfrd_tcp_pipeline* tp,
	struct xfrd_zone* zone);
/* read data from tcp, maybe partial read */
void xfrd_tcp_read(struct xfrd_tcp_pipeline* tp);
/* write data to tcp, maybe a partial write */
void xfrd_tcp_write(struct xfrd_tcp_pipeline* tp, struct xfrd_zone* zone);
/* handle tcp pipe events */
void xfrd_handle_tcp_pipe(int fd, short event, void* arg);

/*
 * Read from a stream connection (size16)+packet into buffer.
 * returns value is
 *	-1 on error.
 *	0 on short read, call back later.
 *	1 on completed read.
 * On first call, make sure total_bytes = 0, msglen=0, buffer_clear().
 * and the packet and fd need to be set.
 */
int conn_read(struct xfrd_tcp* conn);
/*
 * Write to a stream connection (size16)+packet.
 * return value is
 * -1 on error. 0 on short write, call back later. 1 completed write.
 * On first call, make sure total_bytes=0, msglen=buffer_limit(),
 * buffer_flipped(). packet and fd need to be set.
 */
int conn_write(struct xfrd_tcp* conn);

/* setup DNS packet for a query of this type */
void xfrd_setup_packet(struct buffer* packet,
        uint16_t type, uint16_t klass, const struct dname* dname, uint16_t qid,
	int* apex_compress);
/* write soa in network format to the packet buffer */
void xfrd_write_soa_buffer(struct buffer* packet,
        const struct dname* apex, struct xfrd_soa* soa, int apex_compress);
/* use acl address to setup sockaddr struct, returns length of addr. */
socklen_t xfrd_acl_sockaddr_to(struct acl_options* acl,
#ifdef INET6
	struct sockaddr_storage *to);
#else
	struct sockaddr_in *to);
#endif /* INET6 */

socklen_t xfrd_acl_sockaddr_frm(struct acl_options* acl,
#ifdef INET6
	struct sockaddr_storage *frm);
#else
	struct sockaddr_in *frm);
#endif /* INET6 */

/* create pipeline tcp structure */
struct xfrd_tcp_pipeline* xfrd_tcp_pipeline_create(region_type* region,
	int tcp_pipeline);
/* pick num uint16_t values, from 0..max-1, store in array */
void pick_id_values(uint16_t* array, int num, int max);

#ifdef HAVE_SSL
void get_cert_info(SSL* ssl, region_type* region, char** cert_serial,
	char** key_id, char** cert_algorithm, char** tls_version);
#endif

#endif /* XFRD_TCP_H */
