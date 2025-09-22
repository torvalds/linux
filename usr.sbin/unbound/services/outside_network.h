/*
 * services/outside_network.h - listen to answers from the network
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
 * This file has functions to send queries to authoritative servers,
 * and wait for the pending answer, with timeouts.
 */

#ifndef OUTSIDE_NETWORK_H
#define OUTSIDE_NETWORK_H

#include "util/alloc.h"
#include "util/rbtree.h"
#include "util/regional.h"
#include "util/netevent.h"
#include "dnstap/dnstap_config.h"
struct pending;
struct pending_timeout;
struct ub_randstate;
struct pending_tcp;
struct waiting_tcp;
struct waiting_udp;
struct reuse_tcp;
struct infra_cache;
struct port_comm;
struct port_if;
struct sldns_buffer;
struct serviced_query;
struct dt_env;
struct edns_option;
struct module_env;
struct module_qstate;
struct query_info;
struct config_file;

/**
 * Send queries to outside servers and wait for answers from servers.
 * Contains answer-listen sockets.
 */
struct outside_network {
	/** Base for select calls */
	struct comm_base* base;
	/** pointer to time in seconds */
	time_t* now_secs;
	/** pointer to time in microseconds */
	struct timeval* now_tv;

	/** buffer shared by UDP connections, since there is only one
	    datagram at any time. */
	struct sldns_buffer* udp_buff;
	/** serviced_callbacks malloc overhead when processing multiple
	 * identical serviced queries to the same server. */
	size_t svcd_overhead;
	/** use x20 bits to encode additional ID random bits */
	int use_caps_for_id;
	/** outside network wants to quit. Stop queued msgs from sent. */
	int want_to_quit;

	/** number of unwanted replies received (for statistics) */
	size_t unwanted_replies;
	/** cumulative total of unwanted replies (for defense) */
	size_t unwanted_total;
	/** threshold when to take defensive action. If 0 then never. */
	size_t unwanted_threshold;
	/** what action to take, called when defensive action is needed */
	void (*unwanted_action)(void*);
	/** user param for action */
	void* unwanted_param;

	/** linked list of available commpoints, unused file descriptors,
	 * for use as outgoing UDP ports. cp.fd=-1 in them. */
	struct port_comm* unused_fds;
	/** if udp is done */
	int do_udp;
	/** if udp is delay-closed (delayed answers do not meet closed port)*/
	int delayclose;
	/** timeout for delayclose */
	struct timeval delay_tv;
	/** if we perform udp-connect, connect() for UDP socket to mitigate
	 * ICMP side channel leakage */
	int udp_connect;
	/** number of udp packets sent. */
	size_t num_udp_outgoing;

	/** array of outgoing IP4 interfaces */
	struct port_if* ip4_ifs;
	/** number of outgoing IP4 interfaces */
	int num_ip4;

	/** array of outgoing IP6 interfaces */
	struct port_if* ip6_ifs;
	/** number of outgoing IP6 interfaces */
	int num_ip6;

	/** pending udp queries waiting to be sent out, waiting for fd */
	struct pending* udp_wait_first;
	/** last pending udp query in list */
	struct pending* udp_wait_last;

	/** pending udp answers. sorted by id, addr */
	rbtree_type* pending;
	/** serviced queries, sorted by qbuf, addr, dnssec */
	rbtree_type* serviced;
	/** host cache, pointer but not owned by outnet. */
	struct infra_cache* infra;
	/** where to get random numbers */
	struct ub_randstate* rnd;
	/** ssl context to create ssl wrapped TCP with DNS connections */
	void* sslctx;
	/** if SNI will be used for TLS connections */
	int tls_use_sni;
#ifdef USE_DNSTAP
	/** dnstap environment */
	struct dt_env* dtenv;
#endif
	/** maximum segment size of tcp socket */
	int tcp_mss;
	/** IP_TOS socket option requested on the sockets */
	int ip_dscp;

	/**
	 * Array of tcp pending used for outgoing TCP connections.
	 * Each can be used to establish a TCP connection with a server.
	 * The file descriptors are -1 if they are free, and need to be 
	 * opened for the tcp connection. Can be used for ip4 and ip6.
	 */
	struct pending_tcp **tcp_conns;
	/** number of tcp communication points. */
	size_t num_tcp;
	/** number of tcp communication points in use. */
	size_t num_tcp_outgoing;
	/** max number of queries on a reuse connection */
	size_t max_reuse_tcp_queries;
	/** timeout for REUSE entries in milliseconds. */
	int tcp_reuse_timeout;
	/** timeout in milliseconds for TCP queries to auth servers. */
	int tcp_auth_query_timeout;
	/**
	 * tree of still-open and waiting tcp connections for reuse.
	 * can be closed and reopened to get a new tcp connection.
	 * or reused to the same destination again.  with timeout to close.
	 * Entries are of type struct reuse_tcp.
	 * The entries are both active and empty connections.
	 */
	rbtree_type tcp_reuse;
	/** max number of tcp_reuse entries we want to keep open */
	size_t tcp_reuse_max;
	/** first and last(oldest) in lru list of reuse connections.
	 * the oldest can be closed to get a new free pending_tcp if needed
	 * The list contains empty connections, that wait for timeout or
	 * a new query that can use the existing connection. */
	struct reuse_tcp* tcp_reuse_first, *tcp_reuse_last;
	/** list of tcp comm points that are free for use */
	struct pending_tcp* tcp_free;
	/** list of tcp queries waiting for a buffer */
	struct waiting_tcp* tcp_wait_first;
	/** last of waiting query list */
	struct waiting_tcp* tcp_wait_last;
};

/**
 * Outgoing interface. Ports available and currently used are tracked
 * per interface
 */
struct port_if {
	/** address ready to allocate new socket (except port no). */
	struct sockaddr_storage addr;
	/** length of addr field */
	socklen_t addrlen;

	/** prefix length of network address (in bits), for randomisation.
	 * if 0, no randomisation. */
	int pfxlen;

#ifndef DISABLE_EXPLICIT_PORT_RANDOMISATION
	/** the available ports array. These are unused.
	 * Only the first total-inuse part is filled. */
	int* avail_ports;
	/** the total number of available ports (size of the array) */
	int avail_total;
#endif

	/** array of the commpoints currently in use. 
	 * allocated for max number of fds, first part in use. */
	struct port_comm** out;
	/** max number of fds, size of out array */
	int maxout;
	/** number of commpoints (and thus also ports) in use */
	int inuse;
};

/**
 * Outgoing commpoint for UDP port.
 */
struct port_comm {
	/** next in free list */
	struct port_comm* next;
	/** which port number (when in use) */
	int number;
	/** interface it is used in */
	struct port_if* pif;
	/** index in the out array of the interface */
	int index;
	/** number of outstanding queries on this port */
	int num_outstanding;
	/** UDP commpoint, fd=-1 if not in use */
	struct comm_point* cp;
};

/**
 * Reuse TCP connection, still open can be used again.
 */
struct reuse_tcp {
	/** rbtree node with links in tcp_reuse tree. key is NULL when not
	 * in tree. Both active and empty connections are in the tree.
	 * key is a pointer to this structure, the members used to compare
	 * are the sockaddr and and then is-ssl bool, and then ptr value is
	 * used in case the same address exists several times in the tree
	 * when there are multiple connections to the same destination to
	 * make the rbtree items unique. */
	rbnode_type node;
	/** the key for the tcp_reuse tree. address of peer, ip4 or ip6,
	 * and port number of peer */
	struct sockaddr_storage addr;
	/** length of addr */
	socklen_t addrlen;
	/** also key for tcp_reuse tree, if ssl is used */
	int is_ssl;
	/** lru chain, so that the oldest can be removed to get a new
	 * connection when all are in (re)use. oldest is last in list.
	 * The lru only contains empty connections waiting for reuse,
	 * the ones with active queries are not on the list because they
	 * do not need to be closed to make space for others.  They already
	 * service a query so the close for another query does not help
	 * service a larger number of queries. */
	struct reuse_tcp* lru_next, *lru_prev;
	/** true if the reuse_tcp item is on the lru list with empty items */
	int item_on_lru_list;
	/** the connection to reuse, the fd is non-1 and is open.
	 * the addr and port determine where the connection is going,
	 * and is key to the rbtree.  The SSL ptr determines if it is
	 * a TLS connection or a plain TCP connection there.  And TLS
	 * or not is also part of the key to the rbtree.
	 * There is a timeout and read event on the fd, to close it. */
	struct pending_tcp* pending;
	/**
	 * The more read again value pointed to by the commpoint
	 * tcp_more_read_again pointer, so that it exists after commpoint
	 * delete
	 */
	int cp_more_read_again;
	/**
	 * The more write again value pointed to by the commpoint
	 * tcp_more_write_again pointer, so that it exists after commpoint
	 * delete
	 */
	int cp_more_write_again;
	/** rbtree with other queries waiting on the connection, by ID number,
	 * of type struct waiting_tcp. It is for looking up received
	 * answers to the structure for callback.  And also to see if ID
	 * numbers are unused and can be used for a new query.
	 * The write_wait elements are also in the tree, so that ID numbers
	 * can be looked up also for them.  They are bool write_wait_queued. */
	rbtree_type tree_by_id;
	/** list of queries waiting to be written on the channel,
	 * if NULL no queries are waiting to be written and the pending->query
	 * is the query currently serviced.  The first is the next in line.
	 * They are also in the tree_by_id. Once written, the are removed
	 * from this list, but stay in the tree. */
	struct waiting_tcp* write_wait_first, *write_wait_last;
	/** the outside network it is part of */
	struct outside_network* outnet;
};

/**
 * A query that has an answer pending for it.
 */
struct pending {
	/** redblacktree entry, key is the pending struct(id, addr). */
	rbnode_type node;
	/** the ID for the query. int so that a value out of range can
	 * be used to signify a pending that is for certain not present in
	 * the rbtree. (and for which deletion is safe). */
	unsigned int id;
	/** remote address. */
	struct sockaddr_storage addr;
	/** length of addr field in use. */
	socklen_t addrlen;
	/** comm point it was sent on (and reply must come back on). */
	struct port_comm* pc;
	/** timeout event */
	struct comm_timer* timer;
	/** callback for the timeout, error or reply to the message */
	comm_point_callback_type* cb;
	/** callback user argument */
	void* cb_arg;
	/** the outside network it is part of */
	struct outside_network* outnet;
	/** the corresponding serviced_query */
	struct serviced_query* sq;

	/*---- filled if udp pending is waiting -----*/
	/** next in waiting list. */
	struct pending* next_waiting;
	/** timeout in msec */
	int timeout;
	/** The query itself, the query packet to send. */
	uint8_t* pkt;
	/** length of query packet. */
	size_t pkt_len;
};

/**
 * Pending TCP query to server.
 */
struct pending_tcp {
	/** next in list of free tcp comm points, or NULL. */
	struct pending_tcp* next_free;
	/** port for of the outgoing interface that is used */
	struct port_if* pi;
	/** tcp comm point it was sent on (and reply must come back on). */
	struct comm_point* c;
	/** the query being serviced, NULL if the pending_tcp is unused. */
	struct waiting_tcp* query;
	/** the pre-allocated reuse tcp structure.  if ->pending is nonNULL
	 * it is in use and the connection is waiting for reuse.
	 * It is here for memory pre-allocation, and used to make this
	 * pending_tcp wait for reuse. */
	struct reuse_tcp reuse;
};

/**
 * Query waiting for TCP buffer.
 */
struct waiting_tcp {
	/** 
	 * next in waiting list.
	 * if on_tcp_waiting_list==0, this points to the pending_tcp structure.
	 */
	struct waiting_tcp* next_waiting;
	/** if true the item is on the tcp waiting list and next_waiting
	 * is used for that.  If false, the next_waiting points to the
	 * pending_tcp */
	int on_tcp_waiting_list;
	/** next and prev in query waiting list for stream connection */
	struct waiting_tcp* write_wait_prev, *write_wait_next;
	/** true if the waiting_tcp structure is on the write_wait queue */
	int write_wait_queued;
	/** entry in reuse.tree_by_id, if key is NULL, not in tree, otherwise,
	 * this struct is key and sorted by ID (from waiting_tcp.id). */
	rbnode_type id_node;
	/** the ID for the query; checked in reply */
	uint16_t id;
	/** timeout event; timer keeps running whether the query is
	 * waiting for a buffer or the tcp reply is pending */
	struct comm_timer* timer;
	/** timeout in msec */
	int timeout;
	/** the outside network it is part of */
	struct outside_network* outnet;
	/** remote address. */
	struct sockaddr_storage addr;
	/** length of addr field in use. */
	socklen_t addrlen;
	/** 
	 * The query itself, the query packet to send.
	 * allocated after the waiting_tcp structure.
	 */
	uint8_t* pkt;
	/** length of query packet. */
	size_t pkt_len;
	/** callback for the timeout, error or reply to the message,
	 * or NULL if no user is waiting. the entry uses an ID number.
	 * a query that was written is no longer needed, but the ID number
	 * and a reply will come back and can be ignored if NULL */
	comm_point_callback_type* cb;
	/** callback user argument */
	void* cb_arg;
	/** if it uses ssl upstream */
	int ssl_upstream;
	/** ref to the tls_auth_name from the serviced_query */
	char* tls_auth_name;
	/** the packet was involved in an error, to stop looping errors */
	int error_count;
	/** if true, the item is at the cb_and_decommission stage */
	int in_cb_and_decommission;
#ifdef USE_DNSTAP
	/** serviced query pointer for dnstap to get logging info, if nonNULL*/
	struct serviced_query* sq;
#endif
};

/**
 * Callback to party interested in serviced query results.
 */
struct service_callback {
	/** next in callback list */
	struct service_callback* next;
	/** callback function */
	comm_point_callback_type* cb;
	/** user argument for callback function */
	void* cb_arg;
};

/** fallback size for fragmentation for EDNS in IPv4 */
#define EDNS_FRAG_SIZE_IP4 1472
/** fallback size for EDNS in IPv6, fits one fragment with ip6-tunnel-ids */
#define EDNS_FRAG_SIZE_IP6 1232

/**
 * Query service record.
 * Contains query and destination. UDP, TCP, EDNS are all tried.
 * complete with retries and timeouts. A number of interested parties can
 * receive a callback.
 */
struct serviced_query {
	/** The rbtree node, key is this record */
	rbnode_type node;
	/** The query that needs to be answered. Starts with flags u16,
	 * then qdcount, ..., including qname, qtype, qclass. Does not include
	 * EDNS record. */
	uint8_t* qbuf;
	/** length of qbuf. */
	size_t qbuflen;
	/** If an EDNS section is included, the DO/CD bit will be turned on. */
	int dnssec;
	/** We want signatures, or else the answer is likely useless */
	int want_dnssec;
	/** ignore capsforid */
	int nocaps;
	/** tcp upstream used, use tcp, or ssl_upstream for SSL */
	int tcp_upstream, ssl_upstream;
	/** the name of the tls authentication name, eg. 'ns.example.com'
	 * or NULL */
	char* tls_auth_name;
	/** where to send it */
	struct sockaddr_storage addr;
	/** length of addr field in use. */
	socklen_t addrlen;
	/** zone name, uncompressed domain name in wireformat */
	uint8_t* zone;
	/** length of zone name */
	size_t zonelen;
	/** qtype */
	int qtype;
	/** current status */
	enum serviced_query_status {
		/** initial status */
		serviced_initial,
		/** UDP with EDNS sent */
		serviced_query_UDP_EDNS,
		/** UDP without EDNS sent */
		serviced_query_UDP,
		/** TCP with EDNS sent */
		serviced_query_TCP_EDNS,
		/** TCP without EDNS sent */
		serviced_query_TCP,
		/** probe to test noEDNS0 (EDNS gives FORMERRorNOTIMP) */
		serviced_query_UDP_EDNS_fallback,
		/** probe to test TCP noEDNS0 (EDNS gives FORMERRorNOTIMP) */
		serviced_query_TCP_EDNS_fallback,
		/** send UDP query with EDNS1480 (or 1280) */
		serviced_query_UDP_EDNS_FRAG
	} 	
		/** variable with current status */ 
		status;
	/** true if serviced_query is scheduled for deletion already */
	int to_be_deleted;
	/** number of UDP retries */
	int retry;
	/** time last UDP was sent */
	struct timeval last_sent_time;
	/** rtt of last message */
	int last_rtt;
	/** do we know edns probe status already, for UDP_EDNS queries */
	int edns_lame_known;
	/** edns options to use for sending upstream packet */
	struct edns_option* opt_list;
	/** outside network this is part of */
	struct outside_network* outnet;
	/** list of interested parties that need callback on results. */
	struct service_callback* cblist;
	/** the UDP or TCP query that is pending, see status which */
	void* pending;
	/** block size with which to pad encrypted queries (default: 128) */
	size_t padding_block_size;
	/** region for this serviced query. Will be cleared when this
	 * serviced_query will be deleted */
	struct regional* region;
	/** allocation service for the region */
	struct alloc_cache* alloc;
	/** flash timer to start the net I/O as a separate event */
	struct comm_timer* timer;
	/** true if serviced_query is currently doing net I/O and may block */
	int busy;
};

/**
 * Create outside_network structure with N udp ports.
 * @param base: the communication base to use for event handling.
 * @param bufsize: size for network buffers.
 * @param num_ports: number of udp ports to open per interface.
 * @param ifs: interface names (or NULL for default interface).
 *    These interfaces must be able to access all authoritative servers.
 * @param num_ifs: number of names in array ifs.
 * @param do_ip4: service IP4.
 * @param do_ip6: service IP6.
 * @param num_tcp: number of outgoing tcp buffers to preallocate.
 * @param dscp: DSCP to use.
 * @param infra: pointer to infra cached used for serviced queries.
 * @param rnd: stored to create random numbers for serviced queries.
 * @param use_caps_for_id: enable to use 0x20 bits to encode id randomness.
 * @param availports: array of available ports. 
 * @param numavailports: number of available ports in array.
 * @param unwanted_threshold: when to take defensive action.
 * @param unwanted_action: the action to take.
 * @param unwanted_param: user parameter to action.
 * @param tcp_mss: maximum segment size of tcp socket.
 * @param do_udp: if udp is done.
 * @param sslctx: context to create outgoing connections with (if enabled).
 * @param delayclose: if not 0, udp sockets are delayed before timeout closure.
 * 	msec to wait on timeouted udp sockets.
 * @param tls_use_sni: if SNI is used for TLS connections.
 * @param dtenv: environment to send dnstap events with (if enabled).
 * @param udp_connect: if the udp_connect option is enabled.
 * @param max_reuse_tcp_queries: max number of queries on a reuse connection.
 * @param tcp_reuse_timeout: timeout for REUSE entries in milliseconds.
 * @param tcp_auth_query_timeout: timeout in milliseconds for TCP queries to auth servers.
 * @return: the new structure (with no pending answers) or NULL on error.
 */
struct outside_network* outside_network_create(struct comm_base* base,
	size_t bufsize, size_t num_ports, char** ifs, int num_ifs,
	int do_ip4, int do_ip6, size_t num_tcp, int dscp, struct infra_cache* infra, 
	struct ub_randstate* rnd, int use_caps_for_id, int* availports, 
	int numavailports, size_t unwanted_threshold, int tcp_mss,
	void (*unwanted_action)(void*), void* unwanted_param, int do_udp,
	void* sslctx, int delayclose, int tls_use_sni, struct dt_env *dtenv,
	int udp_connect, int max_reuse_tcp_queries, int tcp_reuse_timeout,
	int tcp_auth_query_timeout);

/**
 * Delete outside_network structure.
 * @param outnet: object to delete.
 */
void outside_network_delete(struct outside_network* outnet);

/**
 * Prepare for quit. Sends no more queries, even if queued up.
 * @param outnet: object to prepare for removal
 */
void outside_network_quit_prepare(struct outside_network* outnet);

/**
 * Send UDP query, create pending answer.
 * Changes the ID for the query to be random and unique for that destination.
 * @param sq: serviced query.
 * @param packet: wireformat query to send to destination.
 * @param timeout: in milliseconds from now.
 * @param callback: function to call on error, timeout or reply.
 * @param callback_arg: user argument for callback function.
 * @return: NULL on error for malloc or socket. Else the pending query object.
 */
struct pending* pending_udp_query(struct serviced_query* sq,
	struct sldns_buffer* packet, int timeout, comm_point_callback_type* callback,
	void* callback_arg);

/**
 * Send TCP query. May wait for TCP buffer. Selects ID to be random, and 
 * checks id.
 * @param sq: serviced query.
 * @param packet: wireformat query to send to destination. copied from.
 * @param timeout: in milliseconds from now.
 *    Timer starts running now. Timer may expire if all buffers are used,
 *    without any query been sent to the server yet.
 * @param callback: function to call on error, timeout or reply.
 * @param callback_arg: user argument for callback function.
 * @return: false on error for malloc or socket. Else the pending TCP object.
 */
struct waiting_tcp* pending_tcp_query(struct serviced_query* sq,
	struct sldns_buffer* packet, int timeout, comm_point_callback_type* callback,
	void* callback_arg);

/**
 * Delete pending answer.
 * @param outnet: outside network the pending query is part of.
 *    Internal feature: if outnet is NULL, p is not unlinked from rbtree.
 * @param p: deleted
 */
void pending_delete(struct outside_network* outnet, struct pending* p);

/**
 * Perform a serviced query to the authoritative servers.
 * Duplicate efforts are detected, and EDNS, TCP and UDP retry is performed.
 * @param outnet: outside network, with rbtree of serviced queries.
 * @param qinfo: query info.
 * @param flags: flags u16 (host format), includes opcode, CD bit.
 * @param dnssec: if set, DO bit is set in EDNS queries.
 *	If the value includes BIT_CD, CD bit is set when in EDNS queries.
 *	If the value includes BIT_DO, DO bit is set when in EDNS queries.
 * @param want_dnssec: signatures are needed, without EDNS the answer is
 * 	likely to be useless.
 * @param nocaps: ignore use_caps_for_id and use unperturbed qname.
 * @param check_ratelimit: if set, will check ratelimit before sending out.
 * @param tcp_upstream: use TCP for upstream queries.
 * @param ssl_upstream: use SSL for upstream queries.
 * @param tls_auth_name: when ssl_upstream is true, use this name to check
 * 	the server's peer certificate.
 * @param addr: to which server to send the query.
 * @param addrlen: length of addr.
 * @param zone: name of the zone of the delegation point. wireformat dname.
	This is the delegation point name for which the server is deemed
	authoritative.
 * @param zonelen: length of zone.
 * @param qstate: module qstate. Mainly for inspecting the available
 *	edns_opts_lists.
 * @param callback: callback function.
 * @param callback_arg: user argument to callback function.
 * @param buff: scratch buffer to create query contents in. Empty on exit.
 * @param env: the module environment.
 * @param was_ratelimited: it will signal back if the query failed to pass the
 *	ratelimit check.
 * @return 0 on error, or pointer to serviced query that is used to answer
 *	this serviced query may be shared with other callbacks as well.
 */
struct serviced_query* outnet_serviced_query(struct outside_network* outnet,
	struct query_info* qinfo, uint16_t flags, int dnssec, int want_dnssec,
	int nocaps, int check_ratelimit, int tcp_upstream, int ssl_upstream,
	char* tls_auth_name, struct sockaddr_storage* addr, socklen_t addrlen,
	uint8_t* zone, size_t zonelen, struct module_qstate* qstate,
	comm_point_callback_type* callback, void* callback_arg,
	struct sldns_buffer* buff, struct module_env* env, int* was_ratelimited);

/**
 * Remove service query callback.
 * If that leads to zero callbacks, the query is completely cancelled.
 * @param sq: serviced query to adjust.
 * @param cb_arg: callback argument of callback that needs removal.
 *	same as the callback_arg to outnet_serviced_query().
 */
void outnet_serviced_query_stop(struct serviced_query* sq, void* cb_arg);

/**
 * Get memory size in use by outside network.
 * Counts buffers and outstanding query (serviced queries) malloced data.
 * @param outnet: outside network structure.
 * @return size in bytes.
 */
size_t outnet_get_mem(struct outside_network* outnet);

/**
 * Get memory size in use by serviced query while it is servicing callbacks.
 * This takes into account the pre-deleted status of it; it will be deleted
 * when the callbacks are done.
 * @param sq: serviced query. 
 * @return size in bytes.
 */
size_t serviced_get_mem(struct serviced_query* sq);

/** Pick random ID value for a tcp stream, avoids existing IDs. */
uint16_t reuse_tcp_select_id(struct reuse_tcp* reuse,
	struct outside_network* outnet);

/** find element in tree by id */
struct waiting_tcp* reuse_tcp_by_id_find(struct reuse_tcp* reuse, uint16_t id);

/** insert element in tree by id */
void reuse_tree_by_id_insert(struct reuse_tcp* reuse, struct waiting_tcp* w);

/** insert element in tcp_reuse tree and LRU list */
int reuse_tcp_insert(struct outside_network* outnet,
	struct pending_tcp* pend_tcp);

/** touch the LRU of the element */
void reuse_tcp_lru_touch(struct outside_network* outnet,
	struct reuse_tcp* reuse);

/** remove element from tree and LRU list */
void reuse_tcp_remove_tree_list(struct outside_network* outnet,
	struct reuse_tcp* reuse);

/** snip the last reuse_tcp element off of the LRU list if any */
struct reuse_tcp* reuse_tcp_lru_snip(struct outside_network* outnet);

/** delete readwait waiting_tcp elements, deletes the elements in the list */
void reuse_del_readwait(rbtree_type* tree_by_id);

/** remove waiting tcp from the outnet waiting list */
void outnet_waiting_tcp_list_remove(struct outside_network* outnet,
	struct waiting_tcp* w);

/** pop the first waiting tcp from the outnet waiting list */
struct waiting_tcp* outnet_waiting_tcp_list_pop(struct outside_network* outnet);

/** add waiting_tcp element to the outnet tcp waiting list */
void outnet_waiting_tcp_list_add(struct outside_network* outnet,
	struct waiting_tcp* w, int set_timer);

/** add waiting_tcp element as first to the outnet tcp waiting list */
void outnet_waiting_tcp_list_add_first(struct outside_network* outnet,
	struct waiting_tcp* w, int reset_timer);

/** pop the first element from the writewait list */
struct waiting_tcp* reuse_write_wait_pop(struct reuse_tcp* reuse);

/** remove the element from the writewait list */
void reuse_write_wait_remove(struct reuse_tcp* reuse, struct waiting_tcp* w);

/** push the element after the last on the writewait list */
void reuse_write_wait_push_back(struct reuse_tcp* reuse, struct waiting_tcp* w);

/** get TCP file descriptor for address, returns -1 on failure,
 * tcp_mss is 0 or maxseg size to set for TCP packets,
 * nodelay (TCP_NODELAY) should be set for TLS connections to speed up the TLS
 * handshake.*/
int outnet_get_tcp_fd(struct sockaddr_storage* addr, socklen_t addrlen,
	int tcp_mss, int dscp, int nodelay);

/**
 * Create udp commpoint suitable for sending packets to the destination.
 * @param outnet: outside_network with the comm_base it is attached to,
 * 	with the outgoing interfaces chosen from, and rnd gen for random.
 * @param cb: callback function for the commpoint.
 * @param cb_arg: callback argument for cb.
 * @param to_addr: intended destination.
 * @param to_addrlen: length of to_addr.
 * @return commpoint that you can comm_point_send_udp_msg with, or NULL.
 */
struct comm_point* outnet_comm_point_for_udp(struct outside_network* outnet,
	comm_point_callback_type* cb, void* cb_arg,
	struct sockaddr_storage* to_addr, socklen_t to_addrlen);

/**
 * Create tcp commpoint suitable for communication to the destination.
 * It also performs connect() to the to_addr.
 * @param outnet: outside_network with the comm_base it is attached to,
 * 	and the tcp_mss.
 * @param cb: callback function for the commpoint.
 * @param cb_arg: callback argument for cb.
 * @param to_addr: intended destination.
 * @param to_addrlen: length of to_addr.
 * @param query: initial packet to send writing, in buffer.  It is copied
 * 	to the commpoint buffer that is created.
 * @param timeout: timeout for the TCP connection.
 * 	timeout in milliseconds, or -1 for no (change to the) timeout.
 *	So seconds*1000.
 * @param ssl: set to true for TLS.
 * @param host: hostname for host name verification of TLS (or NULL if no TLS).
 * @return tcp_out commpoint, or NULL.
 */
struct comm_point* outnet_comm_point_for_tcp(struct outside_network* outnet,
	comm_point_callback_type* cb, void* cb_arg,
	struct sockaddr_storage* to_addr, socklen_t to_addrlen,
	struct sldns_buffer* query, int timeout, int ssl, char* host);

/**
 * Create http commpoint suitable for communication to the destination.
 * Creates the http request buffer. It also performs connect() to the to_addr.
 * @param outnet: outside_network with the comm_base it is attached to,
 * 	and the tcp_mss.
 * @param cb: callback function for the commpoint.
 * @param cb_arg: callback argument for cb.
 * @param to_addr: intended destination.
 * @param to_addrlen: length of to_addr.
 * @param timeout: timeout for the TCP connection.
 * 	timeout in milliseconds, or -1 for no (change to the) timeout.
 *	So seconds*1000.
 * @param ssl: set to true for https.
 * @param host: hostname to use for the destination. part of http request.
 * @param path: pathname to lookup, eg. name of the file on the destination.
 * @param cfg: running configuration for User-Agent setup.
 * @return http_out commpoint, or NULL.
 */
struct comm_point* outnet_comm_point_for_http(struct outside_network* outnet,
	comm_point_callback_type* cb, void* cb_arg,
	struct sockaddr_storage* to_addr, socklen_t to_addrlen, int timeout,
	int ssl, char* host, char* path, struct config_file* cfg);

/** connect tcp connection to addr, 0 on failure */
int outnet_tcp_connect(int s, struct sockaddr_storage* addr, socklen_t addrlen);

/** callback for incoming udp answers from the network */
int outnet_udp_cb(struct comm_point* c, void* arg, int error,
	struct comm_reply *reply_info);

/** callback for pending tcp connections */
int outnet_tcp_cb(struct comm_point* c, void* arg, int error,
	struct comm_reply *reply_info);

/** callback for udp timeout */
void pending_udp_timer_cb(void *arg);

/** callback for udp delay for timeout */
void pending_udp_timer_delay_cb(void *arg);

/** callback for outgoing TCP timer event */
void outnet_tcptimer(void* arg);

/** callback to send serviced queries */
void serviced_timer_cb(void *arg);

/** callback for serviced query UDP answers */
int serviced_udp_callback(struct comm_point* c, void* arg, int error,
        struct comm_reply* rep);

/** TCP reply or error callback for serviced queries */
int serviced_tcp_callback(struct comm_point* c, void* arg, int error,
        struct comm_reply* rep);

/** compare function of pending rbtree */
int pending_cmp(const void* key1, const void* key2);

/** compare function of serviced query rbtree */
int serviced_cmp(const void* key1, const void* key2);

/** compare function of reuse_tcp rbtree in outside_network struct */
int reuse_cmp(const void* key1, const void* key2);

/** compare function of reuse_tcp tree_by_id rbtree */
int reuse_id_cmp(const void* key1, const void* key2);

#endif /* OUTSIDE_NETWORK_H */
