/*
 * util/netevent.h - event notification
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
 * This file contains event notification functions.
 *
 * There are three types of communication points
 *    o UDP socket - perthread buffer.
 *    o TCP-accept socket - array of TCP-sockets, socketcount.
 *    o TCP socket - own buffer, parent-TCPaccept, read/write state,
 *                   number of bytes read/written, timeout.
 *
 * There are sockets aimed towards our clients and towards the internet.
 *    o frontside - aimed towards our clients, queries come in, answers back.
 *    o behind - aimed towards internet, to the authoritative DNS servers.
 *
 * Several event types are available:
 *    o comm_base - for thread safety of the comm points, one per thread.
 *    o comm_point - udp and tcp networking, with callbacks.
 *    o comm_timer - a timeout with callback.
 *    o comm_signal - callbacks when signal is caught.
 *    o comm_reply - holds reply info during networking callback.
 *
 */

#ifndef NET_EVENT_H
#define NET_EVENT_H

#include <sys/time.h>
#include "dnscrypt/dnscrypt.h"
#ifdef HAVE_NGHTTP2_NGHTTP2_H
#include <nghttp2/nghttp2.h>
#endif
#ifdef HAVE_NGTCP2
#include <ngtcp2/ngtcp2.h>
#endif

struct sldns_buffer;
struct comm_point;
struct comm_reply;
struct tcl_list;
struct ub_event_base;
struct unbound_socket;
struct doq_server_socket;
struct doq_table;
struct doq_conn;
struct config_file;
struct ub_randstate;

struct mesh_state;
struct mesh_area;

/* internal event notification data storage structure. */
struct internal_event;
struct internal_base;
struct internal_timer; /* A sub struct of the comm_timer super struct */

enum listen_type;

/** callback from communication point function type */
typedef int comm_point_callback_type(struct comm_point*, void*, int,
	struct comm_reply*);

/** to pass no_error to callback function */
#define NETEVENT_NOERROR 0
/** to pass closed connection to callback function */
#define NETEVENT_CLOSED -1
/** to pass timeout happened to callback function */
#define NETEVENT_TIMEOUT -2
/** to pass fallback from capsforID to callback function; 0x20 failed */
#define NETEVENT_CAPSFAIL -3
/** to pass done transfer to callback function; http file is complete */
#define NETEVENT_DONE -4
/** to pass write of the write packet is done to callback function
 * used when tcp_write_and_read is enabled */
#define NETEVENT_PKT_WRITTEN -5

/** timeout to slow accept calls when not possible, in msec. */
#define NETEVENT_SLOW_ACCEPT_TIME 2000
/** timeout to slow down log print, so it does not spam the logs, in sec */
#define SLOW_LOG_TIME 10
/** for doq, the maximum dcid length, in ngtcp2 it is 20. */
#define DOQ_MAX_CIDLEN 24

/**
 * A communication point dispatcher. Thread specific.
 */
struct comm_base {
	/** behind the scenes structure. with say libevent info. alloced */
	struct internal_base* eb;
	/** callback to stop listening on accept sockets,
	 * performed when accept() will not function properly */
	void (*stop_accept)(void*);
	/** callback to start listening on accept sockets, performed
	 * after stop_accept() then a timeout has passed. */
	void (*start_accept)(void*);
	/** user argument for stop_accept and start_accept functions */
	void* cb_arg;
};

/**
 * Reply information for a communication point.
 */
struct comm_reply {
	/** the comm_point with fd to send reply on to. */
	struct comm_point* c;
	/** the address (for UDP based communication) */
	struct sockaddr_storage remote_addr;
	/** length of address */
	socklen_t remote_addrlen;
	/** return type 0 (none), 4(IP4), 6(IP6)
	 *  used only with listen_type_udp_ancil* */
	int srctype;
	/* DnsCrypt context */
#ifdef USE_DNSCRYPT
	uint8_t client_nonce[crypto_box_HALF_NONCEBYTES];
	uint8_t nmkey[crypto_box_BEFORENMBYTES];
	const dnsccert *dnsc_cert;
	int is_dnscrypted;
#endif
	/** the return source interface data */
	union {
#ifdef IPV6_PKTINFO
		struct in6_pktinfo v6info;
#endif
#ifdef IP_PKTINFO
		struct in_pktinfo v4info;
#elif defined(IP_RECVDSTADDR)
		struct in_addr v4addr;
#endif
	}
		/** variable with return source data */
		pktinfo;
	/** max udp size for udp packets */
	size_t max_udp_size;
	/* if set, the request came through a proxy */
	int is_proxied;
	/** the client address
	 *  the same as remote_addr if not proxied */
	struct sockaddr_storage client_addr;
	/** the original address length */
	socklen_t client_addrlen;
#ifdef HAVE_NGTCP2
	/** the doq ifindex, together with addr and localaddr in pktinfo,
	 * and dcid makes the doq_conn_key to find the connection */
	int doq_ifindex;
	/** the doq dcid, the connection id used to find the connection */
	uint8_t doq_dcid[DOQ_MAX_CIDLEN];
	/** the length of the doq dcid */
	size_t doq_dcidlen;
	/** the doq stream id where the query came in on */
	int64_t doq_streamid;
	/** port number for doq */
	int doq_srcport;
#endif /* HAVE_NGTCP2 */
};

/**
 * Communication point to the network
 * These behaviours can be accomplished by setting the flags
 * and passing return values from the callback.
 *    udp frontside: called after readdone. sendafter.
 *    tcp frontside: called readdone, sendafter. close.
 *    udp behind: called after readdone. No send after.
 *    tcp behind: write done, read done, then called. No send after.
 */
struct comm_point {
	/** behind the scenes structure, with say libevent info. alloced. */
	struct internal_event* ev;
	/** if the event is added or not */
	int event_added;

	/** Reference to struct that is part of the listening ports,
	 * where for listening ports information is kept about the address. */
	struct unbound_socket* socket;

	/** file descriptor for communication point */
	int fd;

	/** timeout (NULL if it does not). Malloced. */
	struct timeval* timeout;

	/** buffer pointer. Either to perthread, or own buffer or NULL */
	struct sldns_buffer* buffer;

	/* -------- TCP Handler -------- */
	/** Read/Write state for TCP */
	int tcp_is_reading;
	/** The current read/write count for TCP */
	size_t tcp_byte_count;
	/** parent communication point (for TCP sockets) */
	struct comm_point* tcp_parent;
	/** sockaddr from peer, for TCP handlers */
	struct comm_reply repinfo;

	/* -------- TCP Accept -------- */
	/** the number of TCP handlers for this tcp-accept socket */
	int max_tcp_count;
	/** current number of tcp handler in-use for this accept socket */
	int cur_tcp_count;
	/** malloced array of tcp handlers for a tcp-accept,
	    of size max_tcp_count. */
	struct comm_point** tcp_handlers;
	/** linked list of free tcp_handlers to use for new queries.
	    For tcp_accept the first entry, for tcp_handlers the next one. */
	struct comm_point* tcp_free;

	/* -------- SSL TCP DNS ------- */
	/** the SSL object with rw bio (owned) or for commaccept ctx ref */
	void* ssl;
	/** handshake state for init and renegotiate */
	enum {
		/** no handshake, it has been done */
		comm_ssl_shake_none = 0,
		/** ssl initial handshake wants to read */
		comm_ssl_shake_read,
		/** ssl initial handshake wants to write */
		comm_ssl_shake_write,
		/** ssl_write wants to read */
		comm_ssl_shake_hs_read,
		/** ssl_read wants to write */
		comm_ssl_shake_hs_write
	} ssl_shake_state;

	/* -------- HTTP ------- */
	/** Do not allow connection to use HTTP version lower than this. 0=no
	 * minimum. */
	enum {
		http_version_none = 0,
		http_version_2 = 2
	} http_min_version;
	/** http endpoint */
	char* http_endpoint;
	/* -------- HTTP/1.1 ------- */
	/** Currently reading in http headers */
	int http_in_headers;
	/** Currently reading in chunk headers, 0=not, 1=firstline, 2=unused
	 * (more lines), 3=trailer headers after chunk */
	int http_in_chunk_headers;
	/** chunked transfer */
	int http_is_chunked;
	/** http temp buffer (shared buffer for temporary work) */
	struct sldns_buffer* http_temp;
	/** http stored content in buffer */
	size_t http_stored;
	/* -------- HTTP/2 ------- */
	/** http2 session */
	struct http2_session* h2_session;
	/** set to 1 if h2 is negotiated to be used (using alpn) */
	int use_h2;
	/** stream currently being handled */
	struct http2_stream* h2_stream;
	/** maximum allowed query buffer size, per stream */
	size_t http2_stream_max_qbuffer_size;
	/** maximum number of HTTP/2 streams per connection. Send in HTTP/2
	 * SETTINGS frame. */
	uint32_t http2_max_streams;
	/* -------- DoQ ------- */
#ifdef HAVE_NGTCP2
	/** the doq server socket, with list of doq connections */
	struct doq_server_socket* doq_socket;
#endif

	/* -------- dnstap ------- */
	/** the dnstap environment */
	struct dt_env* dtenv;

	/** is this a UDP, TCP-accept or TCP socket. */
	enum comm_point_type {
		/** UDP socket - handle datagrams. */
		comm_udp,
		/** TCP accept socket - only creates handlers if readable. */
		comm_tcp_accept,
		/** TCP handler socket - handle byteperbyte readwrite. */
		comm_tcp,
		/** HTTP handler socket */
		comm_http,
		/** DOQ handler socket */
		comm_doq,
		/** AF_UNIX socket - for internal commands. */
		comm_local,
		/** raw - not DNS format - for pipe readers and writers */
		comm_raw
	}
		/** variable with type of socket, UDP,TCP-accept,TCP,pipe */
		type;

	/* -------- PROXYv2 ------- */
	/** if set, PROXYv2 is expected on this connection */
	int pp2_enabled;
	/** header state for the PROXYv2 header (for TCP) */
	enum {
		/** no header encounter yet */
		pp2_header_none = 0,
		/** read the static part of the header */
		pp2_header_init,
		/** read the full header */
		pp2_header_done
	} pp2_header_state;

	/* ---------- Behaviour ----------- */
	/** if set the connection is NOT closed on delete. */
	int do_not_close;

	/** if set, the connection is closed on error, on timeout,
	    and after read/write completes. No callback is done. */
	int tcp_do_close;

	/** flag that indicates the stream is both written and read from. */
	int tcp_write_and_read;

	/** byte count for written length over write channel, for when
	 * tcp_write_and_read is enabled.  When tcp_write_and_read is enabled,
	 * this is the counter for writing, the one for reading is in the
	 * commpoint.buffer sldns buffer.  The counter counts from 0 to
	 * 2+tcp_write_pkt_len, and includes the tcp length bytes. */
	size_t tcp_write_byte_count;

	/** packet to write currently over the write channel. for when
	 * tcp_write_and_read is enabled.  When tcp_write_and_read is enabled,
	 * this is the buffer for the written packet, the commpoint.buffer
	 * sldns buffer is the buffer for the received packet. */
	uint8_t* tcp_write_pkt;
	/** length of tcp_write_pkt in bytes */
	size_t tcp_write_pkt_len;

	/** if set try to read another packet again (over connection with
	 * multiple packets), once set, tries once, then zero again,
	 * so set it in the packet complete section.
	 * The pointer itself has to be set before the callback is invoked,
	 * when you set things up, and continue to exist also after the
	 * commpoint is closed and deleted in your callback.  So that after
	 * the callback cleans up netevent can see what it has to do.
	 * Or leave NULL if it is not used at all. */
	int* tcp_more_read_again;

	/** if set try to write another packet (over connection with
	 * multiple packets), once set, tries once, then zero again,
	 * so set it in the packet complete section.
	 * The pointer itself has to be set before the callback is invoked,
	 * when you set things up, and continue to exist also after the
	 * commpoint is closed and deleted in your callback.  So that after
	 * the callback cleans up netevent can see what it has to do.
	 * Or leave NULL if it is not used at all. */
	int* tcp_more_write_again;

	/** if set, read/write completes:
		read/write state of tcp is toggled.
		buffer reset/bytecount reset.
		this flag cleared.
	    So that when that is done the callback is called. */
	int tcp_do_toggle_rw;

	/** timeout in msec for TCP wait times for this connection */
	int tcp_timeout_msec;

	/** if set, tcp keepalive is enabled on this connection */
	int tcp_keepalive;

	/** if set, checks for pending error from nonblocking connect() call.*/
	int tcp_check_nb_connect;

	/** if set, check for connection limit on tcp accept. */
	struct tcl_list* tcp_conn_limit;
	/** the entry for the connection. */
	struct tcl_addr* tcl_addr;

	/** the structure to keep track of open requests on this channel */
	struct tcp_req_info* tcp_req_info;

#ifdef USE_MSG_FASTOPEN
	/** used to track if the sendto() call should be done when using TFO. */
	int tcp_do_fastopen;
#endif

#ifdef USE_DNSCRYPT
	/** Is this a dnscrypt channel */
	int dnscrypt;
	/** encrypted buffer pointer. Either to perthread, or own buffer or NULL */
	struct sldns_buffer* dnscrypt_buffer;
#endif
	/** number of queries outstanding on this socket, used by
	 * outside network for udp ports */
	int inuse;
	/** the timestamp when the packet was received by the kernel */
	struct timeval recv_tv;
	/** callback when done.
	    tcp_accept does not get called back, is NULL then.
	    If a timeout happens, callback with timeout=1 is called.
	    If an error happens, callback is called with error set
	    nonzero. If not NETEVENT_NOERROR, it is an errno value.
	    If the connection is closed (by remote end) then the
	    callback is called with error set to NETEVENT_CLOSED=-1.
	    If a timeout happens on the connection, the error is set to
	    NETEVENT_TIMEOUT=-2.
	    The reply_info can be copied if the reply needs to happen at a
	    later time. It consists of a struct with commpoint and address.
	    It can be passed to a msg send routine some time later.
	    Note the reply information is temporary and must be copied.
	    NULL is passed for_reply info, in cases where error happened.

	    declare as:
	    int my_callback(struct comm_point* c, void* my_arg, int error,
		struct comm_reply *reply_info);

	    if the routine returns 0, nothing is done.
	    Notzero, the buffer will be sent back to client.
	    		For UDP this is done without changing the commpoint.
			In TCP it sets write state.
	*/
	comm_point_callback_type* callback;
	/** argument to pass to callback. */
	void *cb_arg;
};

/**
 * Structure only for making timeout events.
 */
struct comm_timer {
	/** the internal event stuff (derived) */
	struct internal_timer* ev_timer;

	/** callback function, takes user arg only */
	void (*callback)(void*);

	/** callback user argument */
	void* cb_arg;
};

/**
 * Structure only for signal events.
 */
struct comm_signal {
	/** the communication base */
	struct comm_base* base;

	/** the internal event stuff */
	struct internal_signal* ev_signal;

	/** callback function, takes signal number and user arg */
	void (*callback)(int, void*);

	/** callback user argument */
	void* cb_arg;
};

/**
 * Create a new comm base.
 * @param sigs: if true it attempts to create a default loop for
 *   signal handling.
 * @return: the new comm base. NULL on error.
 */
struct comm_base* comm_base_create(int sigs);

/**
 * Create comm base that uses the given ub_event_base (underlying pluggable
 * event mechanism pointer).
 * @param base: underlying pluggable event base.
 * @return: the new comm base. NULL on error.
 */
struct comm_base* comm_base_create_event(struct ub_event_base* base);

/**
 * Delete comm base structure but not the underlying lib event base.
 * All comm points must have been deleted.
 * @param b: the base to delete.
 */
void comm_base_delete_no_base(struct comm_base* b);

/**
 * Destroy a comm base.
 * All comm points must have been deleted.
 * @param b: the base to delete.
 */
void comm_base_delete(struct comm_base* b);

/**
 * Obtain two pointers. The pointers never change (until base_delete()).
 * The pointers point to time values that are updated regularly.
 * @param b: the communication base that will update the time values.
 * @param tt: pointer to time in seconds is returned.
 * @param tv: pointer to time in microseconds is returned.
 */
void comm_base_timept(struct comm_base* b, time_t** tt, struct timeval** tv);

/**
 * Dispatch the comm base events.
 * @param b: the communication to perform.
 */
void comm_base_dispatch(struct comm_base* b);

/**
 * Exit from dispatch loop.
 * @param b: the communication base that is in dispatch().
 */
void comm_base_exit(struct comm_base* b);

/**
 * Set the slow_accept mode handlers.  You can not provide these if you do
 * not perform accept() calls.
 * @param b: comm base
 * @param stop_accept: function that stops listening to accept fds.
 * @param start_accept: function that resumes listening to accept fds.
 * @param arg: callback arg to pass to the functions.
 */
void comm_base_set_slow_accept_handlers(struct comm_base* b,
	void (*stop_accept)(void*), void (*start_accept)(void*), void* arg);

/**
 * Access internal data structure (for util/tube.c on windows)
 * @param b: comm base
 * @return ub_event_base.
 */
struct ub_event_base* comm_base_internal(struct comm_base* b);

/**
 * Access internal event structure. It is for use with
 * ub_winsock_tcp_wouldblock on windows.
 * @param c: comm point.
 * @return event.
 */
struct ub_event* comm_point_internal(struct comm_point* c);

/**
 * Create an UDP comm point. Calls malloc.
 * setups the structure with the parameters you provide.
 * @param base: in which base to alloc the commpoint.
 * @param fd: file descriptor of open UDP socket.
 * @param buffer: shared buffer by UDP sockets from this thread.
 * @param pp2_enabled: if the comm point will support PROXYv2.
 * @param callback: callback function pointer.
 * @param callback_arg: will be passed to your callback function.
 * @param socket: and opened socket properties will be passed to your callback function.
 * @return: returns the allocated communication point. NULL on error.
 * Sets timeout to NULL. Turns off TCP options.
 */
struct comm_point* comm_point_create_udp(struct comm_base* base,
	int fd, struct sldns_buffer* buffer, int pp2_enabled,
	comm_point_callback_type* callback, void* callback_arg, struct unbound_socket* socket);

/**
 * Create an UDP with ancillary data comm point. Calls malloc.
 * Uses recvmsg instead of recv to get udp message.
 * setups the structure with the parameters you provide.
 * @param base: in which base to alloc the commpoint.
 * @param fd: file descriptor of open UDP socket.
 * @param buffer: shared buffer by UDP sockets from this thread.
 * @param pp2_enabled: if the comm point will support PROXYv2.
 * @param callback: callback function pointer.
 * @param callback_arg: will be passed to your callback function.
 * @param socket: and opened socket properties will be passed to your callback function.
 * @return: returns the allocated communication point. NULL on error.
 * Sets timeout to NULL. Turns off TCP options.
 */
struct comm_point* comm_point_create_udp_ancil(struct comm_base* base,
	int fd, struct sldns_buffer* buffer, int pp2_enabled,
	comm_point_callback_type* callback, void* callback_arg, struct unbound_socket* socket);

/**
 * Create an UDP comm point for DoQ. Calls malloc.
 * setups the structure with the parameters you provide.
 * @param base: in which base to alloc the commpoint.
 * @param fd : file descriptor of open UDP socket.
 * @param buffer: shared buffer by UDP sockets from this thread.
 * @param callback: callback function pointer.
 * @param callback_arg: will be passed to your callback function.
 * @param socket: and opened socket properties will be passed to your callback function.
 * @param table: the doq connection table for the host.
 * @param rnd: random generator to use.
 * @param quic_sslctx: the quic ssl context.
 * @param cfg: config file struct.
 * @return: returns the allocated communication point. NULL on error.
 * Sets timeout to NULL. Turns off TCP options.
 */
struct comm_point* comm_point_create_doq(struct comm_base* base,
	int fd, struct sldns_buffer* buffer,
	comm_point_callback_type* callback, void* callback_arg,
	struct unbound_socket* socket, struct doq_table* table,
	struct ub_randstate* rnd, const void* quic_sslctx,
	struct config_file* cfg);

/**
 * Create a TCP listener comm point. Calls malloc.
 * Setups the structure with the parameters you provide.
 * Also Creates TCP Handlers, pre allocated for you.
 * Uses the parameters you provide.
 * @param base: in which base to alloc the commpoint.
 * @param fd: file descriptor of open TCP socket set to listen nonblocking.
 * @param num: becomes max_tcp_count, the routine allocates that
 *	many tcp handler commpoints.
 * @param idle_timeout: TCP idle timeout in ms.
 * @param harden_large_queries: whether query size should be limited.
 * @param http_max_streams: maximum number of HTTP/2 streams per connection.
 * @param http_endpoint: HTTP endpoint to service queries on
 * @param tcp_conn_limit: TCP connection limit info.
 * @param bufsize: size of buffer to create for handlers.
 * @param spoolbuf: shared spool buffer for tcp_req_info structures.
 * 	or NULL to not create those structures in the tcp handlers.
 * @param port_type: the type of port we are creating a TCP listener for. Used
 * 	to select handler type to use.
 * @param pp2_enabled: if the comm point will support PROXYv2.
 * @param callback: callback function pointer for TCP handlers.
 * @param callback_arg: will be passed to your callback function.
 * @param socket: and opened socket properties will be passed to your callback function.
 * @return: returns the TCP listener commpoint. You can find the
 *  	TCP handlers in the array inside the listener commpoint.
 *	returns NULL on error.
 * Inits timeout to NULL. All handlers are on the free list.
 */
struct comm_point* comm_point_create_tcp(struct comm_base* base,
	int fd, int num, int idle_timeout, int harden_large_queries,
	uint32_t http_max_streams, char* http_endpoint,
	struct tcl_list* tcp_conn_limit,
	size_t bufsize, struct sldns_buffer* spoolbuf,
	enum listen_type port_type, int pp2_enabled,
	comm_point_callback_type* callback, void* callback_arg, struct unbound_socket* socket);

/**
 * Create an outgoing TCP commpoint. No file descriptor is opened, left at -1.
 * @param base: in which base to alloc the commpoint.
 * @param bufsize: size of buffer to create for handlers.
 * @param callback: callback function pointer for the handler.
 * @param callback_arg: will be passed to your callback function.
 * @return: the commpoint or NULL on error.
 */
struct comm_point* comm_point_create_tcp_out(struct comm_base* base,
	size_t bufsize, comm_point_callback_type* callback, void* callback_arg);

/**
 * Create an outgoing HTTP commpoint. No file descriptor is opened, left at -1.
 * @param base: in which base to alloc the commpoint.
 * @param bufsize: size of buffer to create for handlers.
 * @param callback: callback function pointer for the handler.
 * @param callback_arg: will be passed to your callback function.
 * @param temp: sldns buffer, shared between other http_out commpoints, for
 * 	temporary data when performing callbacks.
 * @return: the commpoint or NULL on error.
 */
struct comm_point* comm_point_create_http_out(struct comm_base* base,
	size_t bufsize, comm_point_callback_type* callback,
	void* callback_arg, struct sldns_buffer* temp);

/**
 * Create commpoint to listen to a local domain file descriptor.
 * @param base: in which base to alloc the commpoint.
 * @param fd: file descriptor of open AF_UNIX socket set to listen nonblocking.
 * @param bufsize: size of buffer to create for handlers.
 * @param callback: callback function pointer for the handler.
 * @param callback_arg: will be passed to your callback function.
 * @return: the commpoint or NULL on error.
 */
struct comm_point* comm_point_create_local(struct comm_base* base,
	int fd, size_t bufsize,
	comm_point_callback_type* callback, void* callback_arg);

/**
 * Create commpoint to listen to a local domain pipe descriptor.
 * @param base: in which base to alloc the commpoint.
 * @param fd: file descriptor.
 * @param writing: true if you want to listen to writes, false for reads.
 * @param callback: callback function pointer for the handler.
 * @param callback_arg: will be passed to your callback function.
 * @return: the commpoint or NULL on error.
 */
struct comm_point* comm_point_create_raw(struct comm_base* base,
	int fd, int writing,
	comm_point_callback_type* callback, void* callback_arg);

/**
 * Close a comm point fd.
 * @param c: comm point to close.
 */
void comm_point_close(struct comm_point* c);

/**
 * Close and deallocate (free) the comm point. If the comm point is
 * a tcp-accept point, also its tcp-handler points are deleted.
 * @param c: comm point to delete.
 */
void comm_point_delete(struct comm_point* c);

/**
 * Send reply. Put message into commpoint buffer.
 * @param repinfo: The reply info copied from a commpoint callback call.
 */
void comm_point_send_reply(struct comm_reply* repinfo);

/**
 * Drop reply. Cleans up.
 * @param repinfo: The reply info copied from a commpoint callback call.
 */
void comm_point_drop_reply(struct comm_reply* repinfo);

/**
 * Send an udp message over a commpoint.
 * @param c: commpoint to send it from.
 * @param packet: what to send.
 * @param addr: where to send it to.   If NULL, send is performed,
 * 	for connected sockets, to the connected address.
 * @param addrlen: length of addr.
 * @param is_connected: if the UDP socket is connect()ed.
 * @return: false on a failure.
 */
int comm_point_send_udp_msg(struct comm_point* c, struct sldns_buffer* packet,
	struct sockaddr* addr, socklen_t addrlen,int is_connected);

/**
 * Stop listening for input on the commpoint. No callbacks will happen.
 * @param c: commpoint to disable. The fd is not closed.
 */
void comm_point_stop_listening(struct comm_point* c);

/**
 * Start listening again for input on the comm point.
 * @param c: commpoint to enable again.
 * @param newfd: new fd, or -1 to leave fd be.
 * @param msec: timeout in milliseconds, or -1 for no (change to the) timeout.
 *	So seconds*1000.
 */
void comm_point_start_listening(struct comm_point* c, int newfd, int msec);

/**
 * Stop listening and start listening again for reading or writing.
 * @param c: commpoint
 * @param rd: if true, listens for reading.
 * @param wr: if true, listens for writing.
 */
void comm_point_listen_for_rw(struct comm_point* c, int rd, int wr);

/**
 * For TCP handlers that use c->tcp_timeout_msec, this routine adjusts
 * it with the minimum.  Otherwise, a 0 value advertised without the
 * minimum applied moves to a 0 in comm_point_start_listening and that
 * routine treats it as no timeout, listen forever, which is not wanted.
 * @param c: comm point to use the tcp_timeout_msec of.
 * @return adjusted tcp_timeout_msec value with the minimum if smaller.
 */
int adjusted_tcp_timeout(struct comm_point* c);

/**
 * Get size of memory used by comm point.
 * For TCP handlers this includes subhandlers.
 * For UDP handlers, this does not include the (shared) UDP buffer.
 * @param c: commpoint.
 * @return size in bytes.
 */
size_t comm_point_get_mem(struct comm_point* c);

/**
 * create timer. Not active upon creation.
 * @param base: event handling base.
 * @param cb: callback function: void myfunc(void* myarg);
 * @param cb_arg: user callback argument.
 * @return: the new timer or NULL on error.
 */
struct comm_timer* comm_timer_create(struct comm_base* base,
	void (*cb)(void*), void* cb_arg);

/**
 * disable timer. Stops callbacks from happening.
 * @param timer: to disable.
 */
void comm_timer_disable(struct comm_timer* timer);

/**
 * reset timevalue for timer.
 * @param timer: timer to (re)set.
 * @param tv: when the timer should activate. if NULL timer is disabled.
 */
void comm_timer_set(struct comm_timer* timer, struct timeval* tv);

/**
 * delete timer.
 * @param timer: to delete.
 */
void comm_timer_delete(struct comm_timer* timer);

/**
 * see if timeout has been set to a value.
 * @param timer: the timer to examine.
 * @return: false if disabled or not set.
 */
int comm_timer_is_set(struct comm_timer* timer);

/**
 * Get size of memory used by comm timer.
 * @param timer: the timer to examine.
 * @return size in bytes.
 */
size_t comm_timer_get_mem(struct comm_timer* timer);

/**
 * Create a signal handler. Call signal_bind() later to bind to a signal.
 * @param base: communication base to use.
 * @param callback: called when signal is caught.
 * @param cb_arg: user argument to callback
 * @return: the signal struct or NULL on error.
 */
struct comm_signal* comm_signal_create(struct comm_base* base,
	void (*callback)(int, void*), void* cb_arg);

/**
 * Bind signal struct to catch a signal. A single comm_signal can be bound
 * to multiple signals, calling comm_signal_bind multiple times.
 * @param comsig: the communication point, with callback information.
 * @param sig: signal number.
 * @return: true on success. false on error.
 */
int comm_signal_bind(struct comm_signal* comsig, int sig);

/**
 * Delete the signal communication point.
 * @param comsig: to delete.
 */
void comm_signal_delete(struct comm_signal* comsig);

/**
 * perform accept(2) with error checking.
 * @param c: commpoint with accept fd.
 * @param addr: remote end returned here.
 * @param addrlen: length of remote end returned here.
 * @return new fd, or -1 on error.
 *	if -1, error message has been printed if necessary, simply drop
 *	out of the reading handler.
 */
int comm_point_perform_accept(struct comm_point* c,
	struct sockaddr_storage* addr, socklen_t* addrlen);

/**** internal routines ****/

/**
 * This routine is published for checks and tests, and is only used internally.
 * handle libevent callback for udp comm point.
 * @param fd: file descriptor.
 * @param event: event bits from libevent:
 *	EV_READ, EV_WRITE, EV_SIGNAL, EV_TIMEOUT.
 * @param arg: the comm_point structure.
 */
void comm_point_udp_callback(int fd, short event, void* arg);

/**
 * This routine is published for checks and tests, and is only used internally.
 * handle libevent callback for udp ancillary data comm point.
 * @param fd: file descriptor.
 * @param event: event bits from libevent:
 *	EV_READ, EV_WRITE, EV_SIGNAL, EV_TIMEOUT.
 * @param arg: the comm_point structure.
 */
void comm_point_udp_ancil_callback(int fd, short event, void* arg);

/**
 * This routine is published for checks and tests, and is only used internally.
 * handle libevent callback for doq comm point.
 * @param fd: file descriptor.
 * @param event: event bits from libevent:
 *	EV_READ, EV_WRITE, EV_SIGNAL, EV_TIMEOUT.
 * @param arg: the comm_point structure.
 */
void comm_point_doq_callback(int fd, short event, void* arg);

/**
 * This routine is published for checks and tests, and is only used internally.
 * handle libevent callback for tcp accept comm point
 * @param fd: file descriptor.
 * @param event: event bits from libevent:
 *	EV_READ, EV_WRITE, EV_SIGNAL, EV_TIMEOUT.
 * @param arg: the comm_point structure.
 */
void comm_point_tcp_accept_callback(int fd, short event, void* arg);

/**
 * This routine is published for checks and tests, and is only used internally.
 * handle libevent callback for tcp data comm point
 * @param fd: file descriptor.
 * @param event: event bits from libevent:
 *	EV_READ, EV_WRITE, EV_SIGNAL, EV_TIMEOUT.
 * @param arg: the comm_point structure.
 */
void comm_point_tcp_handle_callback(int fd, short event, void* arg);

/**
 * This routine is published for checks and tests, and is only used internally.
 * handle libevent callback for tcp data comm point
 * @param fd: file descriptor.
 * @param event: event bits from libevent:
 *	EV_READ, EV_WRITE, EV_SIGNAL, EV_TIMEOUT.
 * @param arg: the comm_point structure.
 */
void comm_point_http_handle_callback(int fd, short event, void* arg);

/**
 * HTTP2 session.  HTTP2 related info per comm point.
 */
struct http2_session {
	/** first item in list of streams */
	struct http2_stream* first_stream;
#ifdef HAVE_NGHTTP2
	/** nghttp2 session */
	nghttp2_session *session;
	/** store nghttp2 callbacks for easy reuse */
	nghttp2_session_callbacks* callbacks;
#endif
	/** comm point containing buffer used to build answer in worker or
	 * module */
	struct comm_point* c;
	/** session is instructed to get dropped (comm port will be closed) */
	int is_drop;
	/** postpone dropping the session, can be used to prevent dropping
	 * while being in a callback */
	int postpone_drop;
};

/** enum of HTTP status */
enum http_status {
	HTTP_STATUS_OK = 200,
	HTTP_STATUS_BAD_REQUEST = 400,
	HTTP_STATUS_NOT_FOUND = 404,
	HTTP_STATUS_PAYLOAD_TOO_LARGE = 413,
	HTTP_STATUS_URI_TOO_LONG = 414,
	HTTP_STATUS_UNSUPPORTED_MEDIA_TYPE = 415,
	HTTP_STATUS_NOT_IMPLEMENTED = 501
};

/**
 * HTTP stream. Part of list of HTTP2 streams per session.
 */
struct http2_stream {
	/** next stream in list per session */
	struct http2_stream* next;
	/** previous stream in list per session */
	struct http2_stream* prev;
	/** HTTP2 stream ID is an unsigned 31-bit integer */
	int32_t stream_id;
	/** HTTP method used for this stream */
	enum {
		HTTP_METHOD_POST = 1,
		HTTP_METHOD_GET,
		HTTP_METHOD_UNSUPPORTED
	} http_method;
	/** message contains invalid content type */
	int invalid_content_type;
	/** message body content type */
	size_t content_length;
	/** HTTP response status */
	enum http_status status;
	/** request for non existing endpoint */
	int invalid_endpoint;
	/** query in request is too large */
	int query_too_large;
	/** buffer to store query into. Can't use session shared buffer as query
	 * can arrive in parts, intertwined with frames for other queries. */
	struct sldns_buffer* qbuffer;
	/** buffer to store response into. Can't use shared buffer as a next
	 * query read callback can overwrite it before it is send out. */
	struct sldns_buffer* rbuffer;
	/** mesh area containing mesh state */
	struct mesh_area* mesh;
	/** mesh state for query. Used to remove mesh reply before closing
	 * stream. */
	struct mesh_state* mesh_state;
};

#ifdef HAVE_NGHTTP2
/** nghttp2 receive cb. Read from SSL connection into nghttp2 buffer */
ssize_t http2_recv_cb(nghttp2_session* session, uint8_t* buf,
	size_t len, int flags, void* cb_arg);
/** nghttp2 send callback. Send from nghttp2 buffer to ssl socket */
ssize_t http2_send_cb(nghttp2_session* session, const uint8_t* buf,
	size_t len, int flags, void* cb_arg);
/** nghttp2 callback on closing stream */
int http2_stream_close_cb(nghttp2_session* session, int32_t stream_id,
	uint32_t error_code, void* cb_arg);
#endif

/**
 * Create new http2 stream
 * @param stream_id: ID for stream to create.
 * @return malloc'ed stream, NULL on error
 */
struct http2_stream* http2_stream_create(int32_t stream_id);

/**
 * Add new stream to session linked list
 * @param h2_session: http2 session to add stream to
 * @param h2_stream: stream to add to session list
 */
void http2_session_add_stream(struct http2_session* h2_session,
	struct http2_stream* h2_stream);

/** Add mesh state to stream. To be able to remove mesh reply on stream closure
 */
void http2_stream_add_meshstate(struct http2_stream* h2_stream,
	struct mesh_area* mesh, struct mesh_state* m);

/** Remove mesh state from stream. When the mesh state has been removed. */
void http2_stream_remove_mesh_state(struct http2_stream* h2_stream);

/**
 * DoQ socket address storage for IP4 or IP6 address. Smaller than
 * the sockaddr_storage because not with af_unix pathnames.
 */
struct doq_addr_storage {
	union {
		struct sockaddr_in in;
#ifdef AF_INET6
		struct sockaddr_in6 in6;
#endif
	} sockaddr;
};

/**
 * The DoQ server socket information, for DNS over QUIC.
 */
struct doq_server_socket {
	/** the doq connection table */
	struct doq_table* table;
	/** random generator */
	struct ub_randstate* rnd;
	/** if address validation is enabled */
	uint8_t validate_addr;
	/** the server scid length */
	int sv_scidlen;
	/** the idle timeout in nanoseconds */
	uint64_t idle_timeout;
	/** the static secret for the server */
	uint8_t* static_secret;
	/** length of the static secret */
	size_t static_secret_len;
	/** ssl context, SSL_CTX* */
	void* ctx;
#ifndef HAVE_NGTCP2_CRYPTO_QUICTLS_CONFIGURE_SERVER_CONTEXT
	/** quic method functions, SSL_QUIC_METHOD* */
	void* quic_method;
#endif
	/** the comm point for this doq server socket */
	struct comm_point* cp;
	/** the buffer for packets, doq in and out */
	struct sldns_buffer* pkt_buf;
	/** the current doq connection when we are in callbacks to worker,
	 * so that we have the already locked structure at our disposal. */
	struct doq_conn* current_conn;
	/** if the callback event on the fd has write flags */
	uint8_t event_has_write;
	/** if there is a blocked packet in the blocked_pkt buffer */
	int have_blocked_pkt;
	/** store blocked packet, a packet that could not be send on the
	 * nonblocking socket. It has to be sent later, when the write on
	 * the udp socket unblocks. */
	struct sldns_buffer* blocked_pkt;
#ifdef HAVE_NGTCP2
	/** the ecn info for the blocked packet, congestion information. */
	struct ngtcp2_pkt_info blocked_pkt_pi;
#endif
	/** the packet destination for the blocked packet. */
	struct doq_pkt_addr* blocked_paddr;
	/** timer for this worker on this comm_point to wait on. */
	struct comm_timer* timer;
	/** the timer that is marked by the doq_socket as waited on. */
	struct timeval marked_time;
	/** the current time for use by time functions, time_t. */
	time_t* now_tt;
	/** the current time for use by time functions, timeval. */
	struct timeval* now_tv;
	/** config file for the worker. */
	struct config_file* cfg;
};

/**
 * DoQ packet address information. From pktinfo, stores local and remote
 * address and ifindex, so the packet can be sent there.
 */
struct doq_pkt_addr {
	/** the remote addr, and local addr */
	struct doq_addr_storage addr, localaddr;
	/** length of addr and length of localaddr */
	socklen_t addrlen, localaddrlen;
	/** interface index from pktinfo ancillary information */
	int ifindex;
};

/** Initialize the pkt addr with lengths set to sizeof. That is ready for
 * a call to recv. */
void doq_pkt_addr_init(struct doq_pkt_addr* paddr);

/** send doq packet over UDP. */
void doq_send_pkt(struct comm_point* c, struct doq_pkt_addr* paddr,
	uint32_t ecn);

/** doq timer callback function. */
void doq_timer_cb(void* arg);

/**
 * This routine is published for checks and tests, and is only used internally.
 * handle libevent callback for timer comm.
 * @param fd: file descriptor (always -1).
 * @param event: event bits from libevent:
 *	EV_READ, EV_WRITE, EV_SIGNAL, EV_TIMEOUT.
 * @param arg: the comm_timer structure.
 */
void comm_timer_callback(int fd, short event, void* arg);

/**
 * This routine is published for checks and tests, and is only used internally.
 * handle libevent callback for signal comm.
 * @param fd: file descriptor (used for the signal number).
 * @param event: event bits from libevent:
 *	EV_READ, EV_WRITE, EV_SIGNAL, EV_TIMEOUT.
 * @param arg: the internal commsignal structure.
 */
void comm_signal_callback(int fd, short event, void* arg);

/**
 * This routine is published for checks and tests, and is only used internally.
 * libevent callback for AF_UNIX fds
 * @param fd: file descriptor.
 * @param event: event bits from libevent:
 *	EV_READ, EV_WRITE, EV_SIGNAL, EV_TIMEOUT.
 * @param arg: the comm_point structure.
 */
void comm_point_local_handle_callback(int fd, short event, void* arg);

/**
 * This routine is published for checks and tests, and is only used internally.
 * libevent callback for raw fd access.
 * @param fd: file descriptor.
 * @param event: event bits from libevent:
 *	EV_READ, EV_WRITE, EV_SIGNAL, EV_TIMEOUT.
 * @param arg: the comm_point structure.
 */
void comm_point_raw_handle_callback(int fd, short event, void* arg);

/**
 * This routine is published for checks and tests, and is only used internally.
 * libevent callback for timeout on slow accept.
 * @param fd: file descriptor.
 * @param event: event bits from libevent:
 *	EV_READ, EV_WRITE, EV_SIGNAL, EV_TIMEOUT.
 * @param arg: the comm_point structure.
 */
void comm_base_handle_slow_accept(int fd, short event, void* arg);

#ifdef USE_WINSOCK
/**
 * Callback for openssl BIO to on windows detect WSAEWOULDBLOCK and notify
 * the winsock_event of this for proper TCP nonblocking implementation.
 * @param c: comm_point, fd must be set its struct event is registered.
 * @param ssl: openssl SSL, fd must be set so it has a bio.
 */
void comm_point_tcp_win_bio_cb(struct comm_point* c, void* ssl);
#endif

/**
 * See if errno for tcp connect has to be logged or not. This uses errno
 * @param addr: apart from checking errno, the addr is checked for ip4mapped
 * 	and broadcast type, hence passed.
 * @param addrlen: length of the addr parameter.
 * @return true if it needs to be logged.
 */
int tcp_connect_errno_needs_log(struct sockaddr* addr, socklen_t addrlen);

#ifdef HAVE_SSL
/**
 * True if the ssl handshake error has to be squelched from the logs
 * @param err: the error returned by the openssl routine, ERR_get_error.
 * 	This is a packed structure with elements that are examined.
 * @return true if the error is squelched (not logged).
 */
int squelch_err_ssl_handshake(unsigned long err);
#endif

#endif /* NET_EVENT_H */
