/*
 * services/listen_dnsport.h - listen on port 53 for incoming DNS queries.
 *
 * Copyright (c) 2007, NLnet Labs. All rights reserved.
 *
 * This software is open source.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 * 
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 * 
 * Neither the name of the NLNET LABS nor the names of its contributors may
 * be used to endorse or promote products derived from this software without
 * specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * \file
 *
 * This file has functions to get queries from clients.
 */

#ifndef LISTEN_DNSPORT_H
#define LISTEN_DNSPORT_H

#include "util/netevent.h"
#include "util/rbtree.h"
#include "util/locks.h"
#include "daemon/acl_list.h"
#ifdef HAVE_NGHTTP2_NGHTTP2_H
#include <nghttp2/nghttp2.h>
#endif
#ifdef HAVE_NGTCP2
#include <ngtcp2/ngtcp2.h>
#include <ngtcp2/ngtcp2_crypto.h>
#endif
struct listen_list;
struct config_file;
struct addrinfo;
struct sldns_buffer;
struct tcl_list;

/**
 * Listening for queries structure.
 * Contains list of query-listen sockets.
 */
struct listen_dnsport {
	/** Base for select calls */
	struct comm_base* base;

	/** buffer shared by UDP connections, since there is only one
	    datagram at any time. */
	struct sldns_buffer* udp_buff;
#ifdef USE_DNSCRYPT
	struct sldns_buffer* dnscrypt_udp_buff;
#endif
	/** list of comm points used to get incoming events */
	struct listen_list* cps;
};

/**
 * Single linked list to store event points.
 */
struct listen_list {
	/** next in list */
	struct listen_list* next;
	/** event info */
	struct comm_point* com;
};

/**
 * type of ports
 */
enum listen_type {
	/** udp type */
	listen_type_udp,
	/** tcp type */
	listen_type_tcp,
	/** udp ipv6 (v4mapped) for use with ancillary data */
	listen_type_udpancil,
	/** ssl over tcp type */
	listen_type_ssl,
	/** udp type  + dnscrypt*/
	listen_type_udp_dnscrypt,
	/** tcp type + dnscrypt */
	listen_type_tcp_dnscrypt,
	/** udp ipv6 (v4mapped) for use with ancillary data + dnscrypt*/
	listen_type_udpancil_dnscrypt,
	/** HTTP(2) over TLS over TCP */
	listen_type_http,
	/** DNS over QUIC */
	listen_type_doq
};

/*
 * socket properties (just like NSD nsd_socket structure definition)
 */
struct unbound_socket {
	/** the address of the socket */
	struct sockaddr* addr;
	/** length of the address */
	socklen_t addrlen;
	/** socket descriptor returned by socket() syscall */
	int s;
	/** address family (AF_INET/AF_INET6) */
	int fam;
	/** ACL on the socket (listening interface) */
	struct acl_addr* acl;
};

/**
 * Single linked list to store shared ports that have been 
 * opened for use by all threads.
 */
struct listen_port {
	/** next in list */
	struct listen_port* next;
	/** file descriptor, open and ready for use */
	int fd;
	/** type of file descriptor, udp or tcp */
	enum listen_type ftype;
	/** if the port should support PROXYv2 */
	int pp2_enabled;
	/** fill in unbound_socket structure for every opened socket at
	 * Unbound startup */
	struct unbound_socket* socket;
};

/**
 * Create shared listening ports
 * Getaddrinfo, create socket, bind and listen to zero or more 
 * interfaces for IP4 and/or IP6, for UDP and/or TCP.
 * On the given port number. It creates the sockets.
 * @param cfg: settings on what ports to open.
 * @param ifs: interfaces to open, array of IP addresses, "ip[@port]".
 * @param num_ifs: length of ifs.
 * @param reuseport: set to true if you want reuseport, or NULL to not have it,
 *   set to false on exit if reuseport failed to apply (because of no
 *   kernel support).
 * @return: linked list of ports or NULL on error.
 */
struct listen_port* listening_ports_open(struct config_file* cfg,
	char** ifs, int num_ifs, int* reuseport);

/**
 * Close and delete the (list of) listening ports.
 */
void listening_ports_free(struct listen_port* list);

struct config_strlist;
/**
 * Resolve interface names in config and store result IP addresses
 * @param ifs: array of interfaces.  The list of interface names, if not NULL.
 * @param num_ifs: length of ifs array.
 * @param list: if not NULL, this is used as the list of interface names.
 * @param resif: string array (malloced array of malloced strings) with
 * 	result.  NULL if cfg has none.
 * @param num_resif: length of resif.  Zero if cfg has zero num_ifs.
 * @return 0 on failure.
 */
int resolve_interface_names(char** ifs, int num_ifs,
	struct config_strlist* list, char*** resif, int* num_resif);

/**
 * Create commpoints with for this thread for the shared ports.
 * @param base: the comm_base that provides event functionality.
 *	for default all ifs.
 * @param ports: the list of shared ports.
 * @param bufsize: size of datagram buffer.
 * @param tcp_accept_count: max number of simultaneous TCP connections 
 * 	from clients.
 * @param tcp_idle_timeout: idle timeout for TCP connections in msec.
 * @param harden_large_queries: whether query size should be limited.
 * @param http_max_streams: maximum number of HTTP/2 streams per connection.
 * @param http_endpoint: HTTP endpoint to service queries on
 * @param http_notls: no TLS for http downstream
 * @param tcp_conn_limit: TCP connection limit info.
 * @param dot_sslctx: nonNULL if dot ssl context.
 * @param doh_sslctx: nonNULL if doh ssl context.
 * @param quic_sslctx: nonNULL if quic ssl context.
 * @param dtenv: nonNULL if dnstap enabled.
 * @param doq_table: the doq connection table, with shared information.
 * @param rnd: random state.
 * @param cfg: config file struct.
 * @param cb: callback function when a request arrives. It is passed
 *	  the packet and user argument. Return true to send a reply.
 * @param cb_arg: user data argument for callback function.
 * @return: the malloced listening structure, ready for use. NULL on error.
 */
struct listen_dnsport*
listen_create(struct comm_base* base, struct listen_port* ports,
	size_t bufsize, int tcp_accept_count, int tcp_idle_timeout,
	int harden_large_queries, uint32_t http_max_streams,
	char* http_endpoint, int http_notls, struct tcl_list* tcp_conn_limit,
	void* dot_sslctx, void* doh_sslctx, void* quic_sslctx,
	struct dt_env* dtenv,
	struct doq_table* doq_table,
	struct ub_randstate* rnd,struct config_file* cfg,
	comm_point_callback_type* cb, void *cb_arg);

/**
 * delete the listening structure
 * @param listen: listening structure.
 */
void listen_delete(struct listen_dnsport* listen);

/** setup the locks for the listen ports */
void listen_setup_locks(void);
/** desetup the locks for the listen ports */
void listen_desetup_locks(void);

/**
 * delete listen_list of commpoints. Calls commpointdelete() on items.
 * This may close the fds or not depending on flags.
 * @param list: to delete.
 */
void listen_list_delete(struct listen_list* list);

/**
 * get memory size used by the listening structs
 * @param listen: listening structure.
 * @return: size in bytes.
 */
size_t listen_get_mem(struct listen_dnsport* listen);

/**
 * stop accept handlers for TCP (until enabled again)
 * @param listen: listening structure.
 */
void listen_stop_accept(struct listen_dnsport* listen);

/**
 * start accept handlers for TCP (was stopped before)
 * @param listen: listening structure.
 */
void listen_start_accept(struct listen_dnsport* listen);

/**
 * Create and bind nonblocking UDP socket
 * @param family: for socket call.
 * @param socktype: for socket call.
 * @param addr: for bind call.
 * @param addrlen: for bind call.
 * @param v6only: if enabled, IP6 sockets get IP6ONLY option set.
 * 	if enabled with value 2 IP6ONLY option is disabled.
 * @param inuse: on error, this is set true if the port was in use.
 * @param noproto: on error, this is set true if cause is that the
	IPv6 proto (family) is not available.
 * @param rcv: set size on rcvbuf with socket option, if 0 it is not set.
 * @param snd: set size on sndbuf with socket option, if 0 it is not set.
 * @param listen: if true, this is a listening UDP port, eg port 53, and 
 * 	set SO_REUSEADDR on it.
 * @param reuseport: if nonNULL and true, try to set SO_REUSEPORT on
 * 	listening UDP port.  Set to false on return if it failed to do so.
 * @param transparent: set IP_TRANSPARENT socket option.
 * @param freebind: set IP_FREEBIND socket option.
 * @param use_systemd: if true, fetch sockets from systemd.
 * @param dscp: DSCP to use.
 * @return: the socket. -1 on error.
 */
int create_udp_sock(int family, int socktype, struct sockaddr* addr, 
	socklen_t addrlen, int v6only, int* inuse, int* noproto, int rcv,
	int snd, int listen, int* reuseport, int transparent, int freebind, int use_systemd, int dscp);

/**
 * Create and bind TCP listening socket
 * @param addr: address info ready to make socket.
 * @param v6only: enable ip6 only flag on ip6 sockets.
 * @param noproto: if error caused by lack of protocol support.
 * @param reuseport: if nonNULL and true, try to set SO_REUSEPORT on
 * 	listening UDP port.  Set to false on return if it failed to do so.
 * @param transparent: set IP_TRANSPARENT socket option.
 * @param mss: maximum segment size of the socket. if zero, leaves the default. 
 * @param nodelay: if true set TCP_NODELAY and TCP_QUICKACK socket options.
 * @param freebind: set IP_FREEBIND socket option.
 * @param use_systemd: if true, fetch sockets from systemd.
 * @param dscp: DSCP to use.
 * @param additional: additional log information for the socket type.
 * @return: the socket. -1 on error.
 */
int create_tcp_accept_sock(struct addrinfo *addr, int v6only, int* noproto,
	int* reuseport, int transparent, int mss, int nodelay, int freebind,
	int use_systemd, int dscp, const char* additional);

/**
 * Create and bind local listening socket
 * @param path: path to the socket.
 * @param noproto: on error, this is set true if cause is that local sockets
 *	are not supported.
 * @param use_systemd: if true, fetch sockets from systemd.
 * @return: the socket. -1 on error.
 */
int create_local_accept_sock(const char* path, int* noproto, int use_systemd);

/**
 * TCP request info.  List of requests outstanding on the channel, that
 * are asked for but not yet answered back.
 */
struct tcp_req_info {
	/** the TCP comm point for this.  Its buffer is used for read/write */
	struct comm_point* cp;
	/** the buffer to use to spool reply from mesh into,
	 * it can then be copied to the result list and written.
	 * it is a pointer to the shared udp buffer. */
	struct sldns_buffer* spool_buffer;
	/** are we in worker_handle function call (for recursion callback)*/
	int in_worker_handle;
	/** is the comm point dropped (by worker handle).
	 * That means we have to disconnect the channel. */
	int is_drop;
	/** is the comm point set to send_reply (by mesh new client in worker
	 * handle), if so answer is available in c.buffer */
	int is_reply;
	/** read channel has closed, just write pending results */
	int read_is_closed;
	/** read again */
	int read_again;
	/** number of outstanding requests */
	int num_open_req;
	/** list of outstanding requests */
	struct tcp_req_open_item* open_req_list;
	/** number of pending writeable results */
	int num_done_req;
	/** list of pending writable result packets, malloced one at a time */
	struct tcp_req_done_item* done_req_list;
};

/**
 * List of open items in TCP channel
 */
struct tcp_req_open_item {
	/** next in list */
	struct tcp_req_open_item* next;
	/** the mesh area of the mesh_state */
	struct mesh_area* mesh;
	/** the mesh state */
	struct mesh_state* mesh_state;
};

/**
 * List of done items in TCP channel
 */
struct tcp_req_done_item {
	/** next in list */
	struct tcp_req_done_item* next;
	/** the buffer with packet contents */
	uint8_t* buf;
	/** length of the buffer */
	size_t len;
};

/**
 * Create tcp request info structure that keeps track of open
 * requests on the TCP channel that are resolved at the same time,
 * and the pending results that have to get written back to that client.
 * @param spoolbuf: shared buffer
 * @return new structure or NULL on alloc failure.
 */
struct tcp_req_info* tcp_req_info_create(struct sldns_buffer* spoolbuf);

/**
 * Delete tcp request structure.  Called by owning commpoint.
 * Removes mesh entry references and stored results from the lists.
 * @param req: the tcp request info
 */
void tcp_req_info_delete(struct tcp_req_info* req);

/**
 * Clear tcp request structure.  Removes list entries, sets it up ready
 * for the next connection.
 * @param req: tcp request info structure.
 */
void tcp_req_info_clear(struct tcp_req_info* req);

/**
 * Remove mesh state entry from list in tcp_req_info.
 * caller has to manage the mesh state reply entry in the mesh state.
 * @param req: the tcp req info that has the entry removed from the list.
 * @param m: the state removed from the list.
 */
void tcp_req_info_remove_mesh_state(struct tcp_req_info* req,
	struct mesh_state* m);

/**
 * Handle write done of the last result packet
 * @param req: the tcp req info.
 */
void tcp_req_info_handle_writedone(struct tcp_req_info* req);

/**
 * Handle read done of a new request from the client
 * @param req: the tcp req info.
 */
void tcp_req_info_handle_readdone(struct tcp_req_info* req);

/**
 * Add mesh state to the tcp req list of open requests.
 * So the comm_reply can be removed off the mesh reply list when
 * the tcp channel has to be closed (for other reasons then that that
 * request was done, eg. channel closed by client or some format error).
 * @param req: tcp req info structure.  It keeps track of the simultaneous
 * 	requests and results on a tcp (or TLS) channel.
 * @param mesh: mesh area for the state.
 * @param m: mesh state to add.
 * @return 0 on failure (malloc failure).
 */
int tcp_req_info_add_meshstate(struct tcp_req_info* req,
	struct mesh_area* mesh, struct mesh_state* m);

/**
 * Send reply on tcp simultaneous answer channel.  May queue it up.
 * @param req: request info structure.
 */
void tcp_req_info_send_reply(struct tcp_req_info* req);

/** the read channel has closed
 * @param req: request. remaining queries are looked up and answered. 
 * @return zero if nothing to do, just close the tcp.
 */
int tcp_req_info_handle_read_close(struct tcp_req_info* req);

/** get the size of currently used tcp stream wait buffers (in bytes) */
size_t tcp_req_info_get_stream_buffer_size(void);

/** get the size of currently used HTTP2 query buffers (in bytes) */
size_t http2_get_query_buffer_size(void);
/** get the size of currently used HTTP2 response buffers (in bytes) */
size_t http2_get_response_buffer_size(void);

#ifdef HAVE_NGHTTP2
/** 
 * Create nghttp2 callbacks to handle HTTP2 requests.
 * @return malloc'ed struct, NULL on failure
 */
nghttp2_session_callbacks* http2_req_callbacks_create(void);

/** Free http2 stream buffers and decrease buffer counters */
void http2_req_stream_clear(struct http2_stream* h2_stream);

/**
 * DNS response ready to be submitted to nghttp2, to be prepared for sending
 * out. Response is stored in c->buffer. Copy to rbuffer because the c->buffer
 * might be used before this will be send out.
 * @param h2_session: http2 session, containing c->buffer which contains answer
 * @param h2_stream: http2 stream, containing buffer to store answer in
 * @return 0 on error, 1 otherwise
 */
int http2_submit_dns_response(struct http2_session* h2_session);
#else
int http2_submit_dns_response(void* v);
#endif /* HAVE_NGHTTP2 */

#ifdef HAVE_NGTCP2
struct doq_conid;
struct doq_server_socket;

/**
 * DoQ shared connection table. This is the connections for the host.
 * And some config parameter values for connections. The host has to
 * respond on that ip,port for those connections, so they are shared
 * between threads.
 */
struct doq_table {
	/** the lock on the tree and config elements. insert and deletion,
	 * also lookup in the tree needs to hold the lock. */
	lock_rw_type lock;
	/** rbtree of doq_conn, the connections to different destination
	 * addresses, and can be found by dcid. */
	struct rbtree_type* conn_tree;
	/** lock for the conid tree, needed for the conid tree and also
	 * the conid elements */
	lock_rw_type conid_lock;
	/** rbtree of doq_conid, connections can be found by their
	 * connection ids. Lookup by connection id, finds doq_conn. */
	struct rbtree_type* conid_tree;
	/** the server scid length */
	int sv_scidlen;
	/** the static secret for the server */
	uint8_t* static_secret;
	/** length of the static secret */
	size_t static_secret_len;
	/** the idle timeout in nanoseconds */
	uint64_t idle_timeout;
	/** the list of write interested connections, hold the doq_table.lock
	 * to change them */
	struct doq_conn* write_list_first, *write_list_last;
	/** rbtree of doq_timer. */
	struct rbtree_type* timer_tree;
	/** lock on the current_size counter. */
	lock_basic_type size_lock;
	/** current use, in bytes, of QUIC buffers.
	 * The doq_conn ngtcp2_conn structure, SSL structure and conid structs
	 * are not counted. */
	size_t current_size;
};

/**
 * create SSL context for QUIC
 * @param key: private key file.
 * @param pem: public key cert.
 * @param verifypem: if nonNULL, verifylocation file.
 * return SSL_CTX* or NULL on failure (logged).
 */
void* quic_sslctx_create(char* key, char* pem, char* verifypem);

/** create doq table */
struct doq_table* doq_table_create(struct config_file* cfg,
	struct ub_randstate* rnd);

/** delete doq table */
void doq_table_delete(struct doq_table* table);

/**
 * Timer information for doq timer.
 */
struct doq_timer {
	/** The rbnode in the tree sorted by timeout value. Key this struct. */
	struct rbnode_type node;
	/** The timeout value. Absolute time value. */
	struct timeval time;
	/** If the timer is in the time tree, with the node. */
	int timer_in_tree;
	/** If there are more timers with the exact same timeout value,
	 * they form a set of timers. The rbnode timer has a link to the list
	 * with the other timers in the set. The rbnode timer is not a
	 * member of the list with the other timers. The other timers are not
	 * linked into the tree. */
	struct doq_timer* setlist_first, *setlist_last;
	/** If the timer is on the setlist. */
	int timer_in_list;
	/** If in the setlist, the next and prev element. */
	struct doq_timer* setlist_next, *setlist_prev;
	/** The connection that is timeouted. */
	struct doq_conn* conn;
	/** The worker that is waiting for the timeout event.
	 * Set for the rbnode tree linked element. If a worker is waiting
	 * for the event. If NULL, no worker is waiting for this timeout. */
	struct doq_server_socket* worker_doq_socket;
};

/**
 * Key information that makes a doq_conn node in the tree lookup.
 */
struct doq_conn_key {
	/** the remote endpoint and local endpoint and ifindex */
	struct doq_pkt_addr paddr;
	/** the doq connection dcid */
	uint8_t* dcid;
	/** length of dcid */
	size_t dcidlen;
};

/**
 * DoQ connection, for DNS over QUIC. One connection to a remote endpoint
 * with a number of streams in it. Every stream is like a tcp stream with
 * a uint16_t length, query read, and a uint16_t length and answer written.
 */
struct doq_conn {
	/** rbtree node, key is addresses and dcid */
	struct rbnode_type node;
	/** lock on the connection */
	lock_basic_type lock;
	/** the key information, with dcid and address endpoint */
	struct doq_conn_key key;
	/** the doq server socket for inside callbacks */
	struct doq_server_socket* doq_socket;
	/** the doq table this connection is part of */
	struct doq_table* table;
	/** if the connection is about to be deleted. */
	uint8_t is_deleted;
	/** the version, the client chosen version of QUIC */
	uint32_t version;
	/** the ngtcp2 connection, a server connection */
	struct ngtcp2_conn* conn;
	/** the connection ids that are associated with this doq_conn.
	 * There can be a number, that can change. They are linked here,
	 * so that upon removal, the list of actually associated conid
	 * elements can be removed as well. */
	struct doq_conid* conid_list;
	/** the ngtcp2 last error for the connection */
#ifdef HAVE_NGTCP2_CCERR_DEFAULT
	struct ngtcp2_ccerr ccerr;
#else
	struct ngtcp2_connection_close_error last_error;
#endif
	/** the recent tls alert error code */
	uint8_t tls_alert;
	/** the ssl context, SSL* */
	void* ssl;
#ifdef HAVE_NGTCP2_CRYPTO_QUICTLS_CONFIGURE_SERVER_CONTEXT
	/** the connection reference for ngtcp2_conn and userdata in ssl */
	struct ngtcp2_crypto_conn_ref conn_ref;
#endif
	/** closure packet, if any */
	uint8_t* close_pkt;
	/** length of closure packet. */
	size_t close_pkt_len;
	/** closure ecn */
	uint32_t close_ecn;
	/** the streams for this connection, of type doq_stream */
	struct rbtree_type stream_tree;
	/** the streams that want write, they have something to write.
	 * The list is ordered, the last have to wait for the first to
	 * get their data written. */
	struct doq_stream* stream_write_first, *stream_write_last;
	/** the conn has write interest if true, no write interest if false. */
	uint8_t write_interest;
	/** if the conn is on the connection write list */
	uint8_t on_write_list;
	/** the connection write list prev and next, if on the write list */
	struct doq_conn* write_prev, *write_next;
	/** The timer for the connection. If unused, it is not in the tree
	 * and not in the list. It is alloced here, so that it is prealloced.
	 * It has to be set after every read and write on the connection, so
	 * this improves performance, but also the allocation does not fail. */
	struct doq_timer timer;
};

/**
 * Connection ID and the doq_conn that is that connection. A connection
 * has an original dcid, and then more connection ids associated.
 */
struct doq_conid {
	/** rbtree node, key is the connection id. */
	struct rbnode_type node;
	/** the next and prev in the list of conids for the doq_conn */
	struct doq_conid* next, *prev;
	/** key to the doq_conn that is the connection */
	struct doq_conn_key key;
	/** the connection id, byte string */
	uint8_t* cid;
	/** the length of cid */
	size_t cidlen;
};

/**
 * DoQ stream, for DNS over QUIC.
 */
struct doq_stream {
	/** the rbtree node for the stream, key is the stream_id */
	rbnode_type node;
	/** the stream id */
	int64_t stream_id;
	/** if the stream is closed */
	uint8_t is_closed;
	/** if the query is complete */
	uint8_t is_query_complete;
	/** the number of bytes read on the stream, up to querylen+2. */
	size_t nread;
	/** the length of the input query bytes */
	size_t inlen;
	/** the input bytes */
	uint8_t* in;
	/** does the stream have an answer to send */
	uint8_t is_answer_available;
	/** the answer bytes sent, up to outlen+2. */
	size_t nwrite;
	/** the length of the output answer bytes */
	size_t outlen;
	/** the output length in network wireformat */
	uint16_t outlen_wire;
	/** the output packet bytes */
	uint8_t* out;
	/** if the stream is on the write list */
	uint8_t on_write_list;
	/** the prev and next on the write list, if on the list */
	struct doq_stream* write_prev, *write_next;
};

/** doq application error code that is sent when a stream is closed */
#define DOQ_APP_ERROR_CODE 1

/**
 * Create the doq connection.
 * @param c: the comm point for the listening doq socket.
 * @param paddr: with remote and local address and ifindex for the
 * 	connection destination. This is where packets are sent.
 * @param dcid: the dcid, Destination Connection ID.
 * @param dcidlen: length of dcid.
 * @param version: client chosen version.
 * @return new doq connection or NULL on allocation failure.
 */
struct doq_conn* doq_conn_create(struct comm_point* c,
	struct doq_pkt_addr* paddr, const uint8_t* dcid, size_t dcidlen,
	uint32_t version);

/**
 * Delete the doq connection structure.
 * @param conn: to delete.
 * @param table: with memory size.
 */
void doq_conn_delete(struct doq_conn* conn, struct doq_table* table);

/** compare function of doq_conn */
int doq_conn_cmp(const void* key1, const void* key2);

/** compare function of doq_conid */
int doq_conid_cmp(const void* key1, const void* key2);

/** compare function of doq_timer */
int doq_timer_cmp(const void* key1, const void* key2);

/** compare function of doq_stream */
int doq_stream_cmp(const void* key1, const void* key2);

/** setup the doq connection callbacks, and settings. */
int doq_conn_setup(struct doq_conn* conn, uint8_t* scid, size_t scidlen,
	uint8_t* ocid, size_t ocidlen, const uint8_t* token, size_t tokenlen);

/** fill a buffer with random data */
void doq_fill_rand(struct ub_randstate* rnd, uint8_t* buf, size_t len);

/** delete a doq_conid */
void doq_conid_delete(struct doq_conid* conid);

/** add a connection id to the doq_conn.
 * caller must hold doq_table.conid_lock. */
int doq_conn_associate_conid(struct doq_conn* conn, uint8_t* data,
	size_t datalen);

/** remove a connection id from the doq_conn.
 * caller must hold doq_table.conid_lock. */
void doq_conn_dissociate_conid(struct doq_conn* conn, const uint8_t* data,
	size_t datalen);

/** initial setup to link current connection ids to the doq_conn */
int doq_conn_setup_conids(struct doq_conn* conn);

/** remove the connection ids from the doq_conn.
 * caller must hold doq_table.conid_lock. */
void doq_conn_clear_conids(struct doq_conn* conn);

/** find a conid in the doq_conn connection.
 * caller must hold table.conid_lock. */
struct doq_conid* doq_conid_find(struct doq_table* doq_table,
	const uint8_t* data, size_t datalen);

/** receive a packet for a connection */
int doq_conn_recv(struct comm_point* c, struct doq_pkt_addr* paddr,
	struct doq_conn* conn, struct ngtcp2_pkt_info* pi, int* err_retry,
	int* err_drop);

/** send packets for a connection */
int doq_conn_write_streams(struct comm_point* c, struct doq_conn* conn,
	int* err_drop);

/** send the close packet for the connection, perhaps again. */
int doq_conn_send_close(struct comm_point* c, struct doq_conn* conn);

/** delete doq stream */
void doq_stream_delete(struct doq_stream* stream);

/** doq read a connection key from repinfo. It is not malloced, but points
 * into the repinfo for the dcid. */
void doq_conn_key_from_repinfo(struct doq_conn_key* key,
	struct comm_reply* repinfo);

/** doq find a stream in the connection */
struct doq_stream* doq_stream_find(struct doq_conn* conn, int64_t stream_id);

/** doq shutdown the stream. */
int doq_stream_close(struct doq_conn* conn, struct doq_stream* stream,
	int send_shutdown);

/** send reply for a connection */
int doq_stream_send_reply(struct doq_conn* conn, struct doq_stream* stream,
	struct sldns_buffer* buf);

/** the connection has write interest, wants to write packets */
void doq_conn_write_enable(struct doq_conn* conn);

/** the connection has no write interest, does not want to write packets */
void doq_conn_write_disable(struct doq_conn* conn);

/** set the connection on or off the write list, depending on write interest */
void doq_conn_set_write_list(struct doq_table* table, struct doq_conn* conn);

/** doq remove the connection from the write list */
void doq_conn_write_list_remove(struct doq_table* table,
	struct doq_conn* conn);

/** doq get the first conn from the write list, if any, popped from list.
 * Locks the conn that is returned. */
struct doq_conn* doq_table_pop_first(struct doq_table* table);

/**
 * doq check if the timer for the conn needs to be changed.
 * @param conn: connection, caller must hold lock on it.
 * @param tv: time value, absolute time, returned.
 * @return true if timer needs to be set to tv, false if no change is needed
 * 	to the timer. The timer is already set to the right time in that case.
 */
int doq_conn_check_timer(struct doq_conn* conn, struct timeval* tv);

/** doq remove timer from tree */
void doq_timer_tree_remove(struct doq_table* table, struct doq_timer* timer);

/** doq remove timer from list */
void doq_timer_list_remove(struct doq_table* table, struct doq_timer* timer);

/** doq unset the timer if it was set. */
void doq_timer_unset(struct doq_table* table, struct doq_timer* timer);

/** doq set the timer and add it. */
void doq_timer_set(struct doq_table* table, struct doq_timer* timer,
	struct doq_server_socket* worker_doq_socket, struct timeval* tv);

/** doq find a timeout in the timer tree */
struct doq_timer* doq_timer_find_time(struct doq_table* table,
	struct timeval* tv);

/** doq handle timeout for a connection. Pass conn locked. Returns false for
 * deletion. */
int doq_conn_handle_timeout(struct doq_conn* conn);

/** doq add size to the current quic buffer counter */
void doq_table_quic_size_add(struct doq_table* table, size_t add);

/** doq subtract size from the current quic buffer counter */
void doq_table_quic_size_subtract(struct doq_table* table, size_t subtract);

/** doq check if mem is available for quic. */
int doq_table_quic_size_available(struct doq_table* table,
	struct config_file* cfg, size_t mem);

/** doq get the quic size value */
size_t doq_table_quic_size_get(struct doq_table* table);
#endif /* HAVE_NGTCP2 */

char* set_ip_dscp(int socket, int addrfamily, int ds);

/** for debug and profiling purposes only
 * @param ub_sock: the structure containing created socket info we want to print or log for
 */
void verbose_print_unbound_socket(struct unbound_socket* ub_sock);

/** event callback for testcode/doqclient */
void doq_client_event_cb(int fd, short event, void* arg);

/** timer event callback for testcode/doqclient */
void doq_client_timer_cb(int fd, short event, void* arg);

#ifdef HAVE_NGTCP2
/** get a timestamp in nanoseconds */
ngtcp2_tstamp doq_get_timestamp_nanosec(void);
#endif
#endif /* LISTEN_DNSPORT_H */
