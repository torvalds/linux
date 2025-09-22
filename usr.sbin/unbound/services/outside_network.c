/*
 * services/outside_network.c - implement sending of queries and wait answer.
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
 * This file has functions to send queries to authoritative servers and
 * wait for the pending answer events.
 */
#include "config.h"
#include <ctype.h>
#ifdef HAVE_SYS_TYPES_H
#  include <sys/types.h>
#endif
#include <sys/time.h>
#include "services/outside_network.h"
#include "services/listen_dnsport.h"
#include "services/cache/infra.h"
#include "iterator/iterator.h"
#include "util/data/msgparse.h"
#include "util/data/msgreply.h"
#include "util/data/msgencode.h"
#include "util/data/dname.h"
#include "util/netevent.h"
#include "util/log.h"
#include "util/net_help.h"
#include "util/random.h"
#include "util/fptr_wlist.h"
#include "util/edns.h"
#include "sldns/sbuffer.h"
#include "dnstap/dnstap.h"
#ifdef HAVE_OPENSSL_SSL_H
#include <openssl/ssl.h>
#endif
#ifdef HAVE_X509_VERIFY_PARAM_SET1_HOST
#include <openssl/x509v3.h>
#endif

#ifdef HAVE_NETDB_H
#include <netdb.h>
#endif
#include <fcntl.h>

/** number of times to retry making a random ID that is unique. */
#define MAX_ID_RETRY 1000
/** number of times to retry finding interface, port that can be opened. */
#define MAX_PORT_RETRY 10000
/** number of retries on outgoing UDP queries */
#define OUTBOUND_UDP_RETRY 1

/** initiate TCP transaction for serviced query */
static void serviced_tcp_initiate(struct serviced_query* sq, sldns_buffer* buff);
/** with a fd available, randomize and send UDP */
static int randomize_and_send_udp(struct pending* pend, sldns_buffer* packet,
	int timeout);

/** select a DNS ID for a TCP stream */
static uint16_t tcp_select_id(struct outside_network* outnet,
	struct reuse_tcp* reuse);

/** Perform serviced query UDP sending operation */
static int serviced_udp_send(struct serviced_query* sq, sldns_buffer* buff);

/** Send serviced query over TCP return false on initial failure */
static int serviced_tcp_send(struct serviced_query* sq, sldns_buffer* buff);

/** call the callbacks for a serviced query */
static void serviced_callbacks(struct serviced_query* sq, int error,
	struct comm_point* c, struct comm_reply* rep);

int 
pending_cmp(const void* key1, const void* key2)
{
	struct pending *p1 = (struct pending*)key1;
	struct pending *p2 = (struct pending*)key2;
	if(p1->id < p2->id)
		return -1;
	if(p1->id > p2->id)
		return 1;
	log_assert(p1->id == p2->id);
	return sockaddr_cmp(&p1->addr, p1->addrlen, &p2->addr, p2->addrlen);
}

int 
serviced_cmp(const void* key1, const void* key2)
{
	struct serviced_query* q1 = (struct serviced_query*)key1;
	struct serviced_query* q2 = (struct serviced_query*)key2;
	int r;
	if(q1->qbuflen < q2->qbuflen)
		return -1;
	if(q1->qbuflen > q2->qbuflen)
		return 1;
	log_assert(q1->qbuflen == q2->qbuflen);
	log_assert(q1->qbuflen >= 15 /* 10 header, root, type, class */);
	/* alternate casing of qname is still the same query */
	if((r = memcmp(q1->qbuf, q2->qbuf, 10)) != 0)
		return r;
	if((r = memcmp(q1->qbuf+q1->qbuflen-4, q2->qbuf+q2->qbuflen-4, 4)) != 0)
		return r;
	if(q1->dnssec != q2->dnssec) {
		if(q1->dnssec < q2->dnssec)
			return -1;
		return 1;
	}
	if((r = query_dname_compare(q1->qbuf+10, q2->qbuf+10)) != 0)
		return r;
	if((r = edns_opt_list_compare(q1->opt_list, q2->opt_list)) != 0)
		return r;
	return sockaddr_cmp(&q1->addr, q1->addrlen, &q2->addr, q2->addrlen);
}

/** compare if the reuse element has the same address, port and same ssl-is
 * used-for-it characteristic */
static int
reuse_cmp_addrportssl(const void* key1, const void* key2)
{
	struct reuse_tcp* r1 = (struct reuse_tcp*)key1;
	struct reuse_tcp* r2 = (struct reuse_tcp*)key2;
	int r;
	/* compare address and port */
	r = sockaddr_cmp(&r1->addr, r1->addrlen, &r2->addr, r2->addrlen);
	if(r != 0)
		return r;

	/* compare if SSL-enabled */
	if(r1->is_ssl && !r2->is_ssl)
		return 1;
	if(!r1->is_ssl && r2->is_ssl)
		return -1;
	return 0;
}

int
reuse_cmp(const void* key1, const void* key2)
{
	int r;
	r = reuse_cmp_addrportssl(key1, key2);
	if(r != 0)
		return r;

	/* compare ptr value */
	if(key1 < key2) return -1;
	if(key1 > key2) return 1;
	return 0;
}

int reuse_id_cmp(const void* key1, const void* key2)
{
	struct waiting_tcp* w1 = (struct waiting_tcp*)key1;
	struct waiting_tcp* w2 = (struct waiting_tcp*)key2;
	if(w1->id < w2->id)
		return -1;
	if(w1->id > w2->id)
		return 1;
	return 0;
}

/** delete waiting_tcp entry. Does not unlink from waiting list. 
 * @param w: to delete.
 */
static void
waiting_tcp_delete(struct waiting_tcp* w)
{
	if(!w) return;
	if(w->timer)
		comm_timer_delete(w->timer);
	free(w);
}

/** 
 * Pick random outgoing-interface of that family, and bind it.
 * port set to 0 so OS picks a port number for us.
 * if it is the ANY address, do not bind.
 * @param pend: pending tcp structure, for storing the local address choice.
 * @param w: tcp structure with destination address.
 * @param s: socket fd.
 * @return false on error, socket closed.
 */
static int
pick_outgoing_tcp(struct pending_tcp* pend, struct waiting_tcp* w, int s)
{
	struct port_if* pi = NULL;
	int num;
	pend->pi = NULL;
#ifdef INET6
	if(addr_is_ip6(&w->addr, w->addrlen))
		num = w->outnet->num_ip6;
	else
#endif
		num = w->outnet->num_ip4;
	if(num == 0) {
		log_err("no TCP outgoing interfaces of family");
		log_addr(VERB_OPS, "for addr", &w->addr, w->addrlen);
		sock_close(s);
		return 0;
	}
#ifdef INET6
	if(addr_is_ip6(&w->addr, w->addrlen))
		pi = &w->outnet->ip6_ifs[ub_random_max(w->outnet->rnd, num)];
	else
#endif
		pi = &w->outnet->ip4_ifs[ub_random_max(w->outnet->rnd, num)];
	log_assert(pi);
	pend->pi = pi;
	if(addr_is_any(&pi->addr, pi->addrlen)) {
		/* binding to the ANY interface is for listening sockets */
		return 1;
	}
	/* set port to 0 */
	if(addr_is_ip6(&pi->addr, pi->addrlen))
		((struct sockaddr_in6*)&pi->addr)->sin6_port = 0;
	else	((struct sockaddr_in*)&pi->addr)->sin_port = 0;
	if(bind(s, (struct sockaddr*)&pi->addr, pi->addrlen) != 0) {
#ifndef USE_WINSOCK
#ifdef EADDRNOTAVAIL
		if(!(verbosity < 4 && errno == EADDRNOTAVAIL))
#endif
#else /* USE_WINSOCK */
		if(!(verbosity < 4 && WSAGetLastError() == WSAEADDRNOTAVAIL))
#endif
		    log_err("outgoing tcp: bind: %s", sock_strerror(errno));
		sock_close(s);
		return 0;
	}
	log_addr(VERB_ALGO, "tcp bound to src", &pi->addr, pi->addrlen);
	return 1;
}

/** get TCP file descriptor for address, returns -1 on failure,
 * tcp_mss is 0 or maxseg size to set for TCP packets. */
int
outnet_get_tcp_fd(struct sockaddr_storage* addr, socklen_t addrlen,
	int tcp_mss, int dscp, int nodelay)
{
	int s;
	int af;
	char* err;
#if defined(SO_REUSEADDR) || defined(IP_BIND_ADDRESS_NO_PORT)	\
	|| defined(TCP_NODELAY)
	int on = 1;
#endif
#ifdef INET6
	if(addr_is_ip6(addr, addrlen)){
		s = socket(PF_INET6, SOCK_STREAM, IPPROTO_TCP);
		af = AF_INET6;
	} else {
#else
	{
#endif
		af = AF_INET;
		s = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
	}
	if(s == -1) {
		log_err_addr("outgoing tcp: socket", sock_strerror(errno),
			addr, addrlen);
		return -1;
	}

#ifdef SO_REUSEADDR
	if(setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (void*)&on,
		(socklen_t)sizeof(on)) < 0) {
		verbose(VERB_ALGO, "outgoing tcp:"
			" setsockopt(.. SO_REUSEADDR ..) failed");
	}
#endif

	err = set_ip_dscp(s, af, dscp);
	if(err != NULL) {
		verbose(VERB_ALGO, "outgoing tcp:"
			"error setting IP DiffServ codepoint on socket");
	}

	if(tcp_mss > 0) {
#if defined(IPPROTO_TCP) && defined(TCP_MAXSEG)
		if(setsockopt(s, IPPROTO_TCP, TCP_MAXSEG,
			(void*)&tcp_mss, (socklen_t)sizeof(tcp_mss)) < 0) {
			verbose(VERB_ALGO, "outgoing tcp:"
				" setsockopt(.. TCP_MAXSEG ..) failed");
		}
#else
		verbose(VERB_ALGO, "outgoing tcp:"
			" setsockopt(TCP_MAXSEG) unsupported");
#endif /* defined(IPPROTO_TCP) && defined(TCP_MAXSEG) */
	}
#ifdef IP_BIND_ADDRESS_NO_PORT
	if(setsockopt(s, IPPROTO_IP, IP_BIND_ADDRESS_NO_PORT, (void*)&on,
		(socklen_t)sizeof(on)) < 0) {
		verbose(VERB_ALGO, "outgoing tcp:"
			" setsockopt(.. IP_BIND_ADDRESS_NO_PORT ..) failed");
	}
#endif /* IP_BIND_ADDRESS_NO_PORT */
	if(nodelay) {
#if defined(IPPROTO_TCP) && defined(TCP_NODELAY)
		if(setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (void*)&on,
			(socklen_t)sizeof(on)) < 0) {
			verbose(VERB_ALGO, "outgoing tcp:"
				" setsockopt(.. TCP_NODELAY ..) failed");
		}
#else
		verbose(VERB_ALGO, "outgoing tcp:"
			" setsockopt(.. TCP_NODELAY ..) unsupported");
#endif /* defined(IPPROTO_TCP) && defined(TCP_NODELAY) */
	}
	return s;
}

/** connect tcp connection to addr, 0 on failure */
int
outnet_tcp_connect(int s, struct sockaddr_storage* addr, socklen_t addrlen)
{
	if(connect(s, (struct sockaddr*)addr, addrlen) == -1) {
#ifndef USE_WINSOCK
#ifdef EINPROGRESS
		if(errno != EINPROGRESS) {
#endif
			if(tcp_connect_errno_needs_log(
				(struct sockaddr*)addr, addrlen))
				log_err_addr("outgoing tcp: connect",
					strerror(errno), addr, addrlen);
			close(s);
			return 0;
#ifdef EINPROGRESS
		}
#endif
#else /* USE_WINSOCK */
		if(WSAGetLastError() != WSAEINPROGRESS &&
			WSAGetLastError() != WSAEWOULDBLOCK) {
			closesocket(s);
			return 0;
		}
#endif
	}
	return 1;
}

/** log reuse item addr and ptr with message */
static void
log_reuse_tcp(enum verbosity_value v, const char* msg, struct reuse_tcp* reuse)
{
	uint16_t port;
	char addrbuf[128];
	if(verbosity < v) return;
	if(!reuse || !reuse->pending || !reuse->pending->c)
		return;
	addr_to_str(&reuse->addr, reuse->addrlen, addrbuf, sizeof(addrbuf));
	port = ntohs(((struct sockaddr_in*)&reuse->addr)->sin_port);
	verbose(v, "%s %s#%u fd %d", msg, addrbuf, (unsigned)port,
		reuse->pending->c->fd);
}

/** pop the first element from the writewait list */
struct waiting_tcp*
reuse_write_wait_pop(struct reuse_tcp* reuse)
{
	struct waiting_tcp* w = reuse->write_wait_first;
	if(!w)
		return NULL;
	log_assert(w->write_wait_queued);
	log_assert(!w->write_wait_prev);
	reuse->write_wait_first = w->write_wait_next;
	if(w->write_wait_next)
		w->write_wait_next->write_wait_prev = NULL;
	else	reuse->write_wait_last = NULL;
	w->write_wait_queued = 0;
	w->write_wait_next = NULL;
	w->write_wait_prev = NULL;
	return w;
}

/** remove the element from the writewait list */
void
reuse_write_wait_remove(struct reuse_tcp* reuse, struct waiting_tcp* w)
{
	log_assert(w);
	log_assert(w->write_wait_queued);
	if(!w)
		return;
	if(!w->write_wait_queued)
		return;
	if(w->write_wait_prev)
		w->write_wait_prev->write_wait_next = w->write_wait_next;
	else	reuse->write_wait_first = w->write_wait_next;
	log_assert(!w->write_wait_prev ||
		w->write_wait_prev->write_wait_next != w->write_wait_prev);
	if(w->write_wait_next)
		w->write_wait_next->write_wait_prev = w->write_wait_prev;
	else	reuse->write_wait_last = w->write_wait_prev;
	log_assert(!w->write_wait_next
		|| w->write_wait_next->write_wait_prev != w->write_wait_next);
	w->write_wait_queued = 0;
	w->write_wait_next = NULL;
	w->write_wait_prev = NULL;
}

/** push the element after the last on the writewait list */
void
reuse_write_wait_push_back(struct reuse_tcp* reuse, struct waiting_tcp* w)
{
	if(!w) return;
	log_assert(!w->write_wait_queued);
	if(reuse->write_wait_last) {
		reuse->write_wait_last->write_wait_next = w;
		log_assert(reuse->write_wait_last->write_wait_next !=
			reuse->write_wait_last);
		w->write_wait_prev = reuse->write_wait_last;
	} else {
		reuse->write_wait_first = w;
		w->write_wait_prev = NULL;
	}
	w->write_wait_next = NULL;
	reuse->write_wait_last = w;
	w->write_wait_queued = 1;
}

/** insert element in tree by id */
void
reuse_tree_by_id_insert(struct reuse_tcp* reuse, struct waiting_tcp* w)
{
#ifdef UNBOUND_DEBUG
	rbnode_type* added;
#endif
	log_assert(w->id_node.key == NULL);
	w->id_node.key = w;
#ifdef UNBOUND_DEBUG
	added =
#else
	(void)
#endif
	rbtree_insert(&reuse->tree_by_id, &w->id_node);
	log_assert(added);  /* should have been added */
}

/** find element in tree by id */
struct waiting_tcp*
reuse_tcp_by_id_find(struct reuse_tcp* reuse, uint16_t id)
{
	struct waiting_tcp key_w;
	rbnode_type* n;
	memset(&key_w, 0, sizeof(key_w));
	key_w.id_node.key = &key_w;
	key_w.id = id;
	n = rbtree_search(&reuse->tree_by_id, &key_w);
	if(!n) return NULL;
	return (struct waiting_tcp*)n->key;
}

/** return ID value of rbnode in tree_by_id */
static uint16_t
tree_by_id_get_id(rbnode_type* node)
{
	struct waiting_tcp* w = (struct waiting_tcp*)node->key;
	return w->id;
}

/** insert into reuse tcp tree and LRU, false on failure (duplicate) */
int
reuse_tcp_insert(struct outside_network* outnet, struct pending_tcp* pend_tcp)
{
	log_reuse_tcp(VERB_CLIENT, "reuse_tcp_insert", &pend_tcp->reuse);
	if(pend_tcp->reuse.item_on_lru_list) {
		if(!pend_tcp->reuse.node.key)
			log_err("internal error: reuse_tcp_insert: "
				"in lru list without key");
		return 1;
	}
	pend_tcp->reuse.node.key = &pend_tcp->reuse;
	pend_tcp->reuse.pending = pend_tcp;
	if(!rbtree_insert(&outnet->tcp_reuse, &pend_tcp->reuse.node)) {
		/* We are not in the LRU list but we are already in the
		 * tcp_reuse tree, strange.
		 * Continue to add ourselves to the LRU list. */
		log_err("internal error: reuse_tcp_insert: in lru list but "
			"not in the tree");
	}
	/* insert into LRU, first is newest */
	pend_tcp->reuse.lru_prev = NULL;
	if(outnet->tcp_reuse_first) {
		pend_tcp->reuse.lru_next = outnet->tcp_reuse_first;
		log_assert(pend_tcp->reuse.lru_next != &pend_tcp->reuse);
		outnet->tcp_reuse_first->lru_prev = &pend_tcp->reuse;
		log_assert(outnet->tcp_reuse_first->lru_prev !=
			outnet->tcp_reuse_first);
	} else {
		pend_tcp->reuse.lru_next = NULL;
		outnet->tcp_reuse_last = &pend_tcp->reuse;
	}
	outnet->tcp_reuse_first = &pend_tcp->reuse;
	pend_tcp->reuse.item_on_lru_list = 1;
	log_assert((!outnet->tcp_reuse_first && !outnet->tcp_reuse_last) ||
		(outnet->tcp_reuse_first && outnet->tcp_reuse_last));
	log_assert(outnet->tcp_reuse_first != outnet->tcp_reuse_first->lru_next &&
		outnet->tcp_reuse_first != outnet->tcp_reuse_first->lru_prev);
	log_assert(outnet->tcp_reuse_last != outnet->tcp_reuse_last->lru_next &&
		outnet->tcp_reuse_last != outnet->tcp_reuse_last->lru_prev);
	return 1;
}

/** find reuse tcp stream to destination for query, or NULL if none */
static struct reuse_tcp*
reuse_tcp_find(struct outside_network* outnet, struct sockaddr_storage* addr,
	socklen_t addrlen, int use_ssl)
{
	struct waiting_tcp key_w;
	struct pending_tcp key_p;
	struct comm_point c;
	rbnode_type* result = NULL, *prev;
	verbose(VERB_CLIENT, "reuse_tcp_find");
	memset(&key_w, 0, sizeof(key_w));
	memset(&key_p, 0, sizeof(key_p));
	memset(&c, 0, sizeof(c));
	key_p.query = &key_w;
	key_p.c = &c;
	key_p.reuse.pending = &key_p;
	key_p.reuse.node.key = &key_p.reuse;
	if(use_ssl)
		key_p.reuse.is_ssl = 1;
	if(addrlen > (socklen_t)sizeof(key_p.reuse.addr))
		return NULL;
	memmove(&key_p.reuse.addr, addr, addrlen);
	key_p.reuse.addrlen = addrlen;

	verbose(VERB_CLIENT, "reuse_tcp_find: num reuse streams %u",
		(unsigned)outnet->tcp_reuse.count);
	if(outnet->tcp_reuse.root == NULL ||
		outnet->tcp_reuse.root == RBTREE_NULL)
		return NULL;
	if(rbtree_find_less_equal(&outnet->tcp_reuse, &key_p.reuse,
		&result)) {
		/* exact match */
		/* but the key is on stack, and ptr is compared, impossible */
		log_assert(&key_p.reuse != (struct reuse_tcp*)result);
		log_assert(&key_p != ((struct reuse_tcp*)result)->pending);
	}

	/* It is possible that we search for something before the first element
	 * in the tree. Replace a null pointer with the first element.
	 */
	if (!result) {
		verbose(VERB_CLIENT, "reuse_tcp_find: taking first");
		result = rbtree_first(&outnet->tcp_reuse);
	}

	/* not found, return null */
	if(!result || result == RBTREE_NULL)
		return NULL;

	/* It is possible that we got the previous address, but that the
	 * address we are looking for is in the tree. If the address we got
	 * is less than the address we are looking, then take the next entry.
	 */
	if (reuse_cmp_addrportssl(result->key, &key_p.reuse) < 0) {
		verbose(VERB_CLIENT, "reuse_tcp_find: key too low");
		result = rbtree_next(result);
	}

	verbose(VERB_CLIENT, "reuse_tcp_find check inexact match");
	/* inexact match, find one of possibly several connections to the
	 * same destination address, with the correct port, ssl, and
	 * also less than max number of open queries, or else, fail to open
	 * a new one */
	/* rewind to start of sequence of same address,port,ssl */
	prev = rbtree_previous(result);
	while(prev && prev != RBTREE_NULL &&
		reuse_cmp_addrportssl(prev->key, &key_p.reuse) == 0) {
		result = prev;
		prev = rbtree_previous(result);
	}

	/* loop to find first one that has correct characteristics */
	while(result && result != RBTREE_NULL &&
		reuse_cmp_addrportssl(result->key, &key_p.reuse) == 0) {
		if(((struct reuse_tcp*)result)->tree_by_id.count <
			outnet->max_reuse_tcp_queries) {
			/* same address, port, ssl-yes-or-no, and has
			 * space for another query */
			return (struct reuse_tcp*)result;
		}
		result = rbtree_next(result);
	}
	return NULL;
}

/** use the buffer to setup writing the query */
static void
outnet_tcp_take_query_setup(int s, struct pending_tcp* pend,
	struct waiting_tcp* w)
{
	struct timeval tv;
	verbose(VERB_CLIENT, "outnet_tcp_take_query_setup: setup packet to write "
		"len %d timeout %d msec",
		(int)w->pkt_len, w->timeout);
	pend->c->tcp_write_pkt = w->pkt;
	pend->c->tcp_write_pkt_len = w->pkt_len;
	pend->c->tcp_write_and_read = 1;
	pend->c->tcp_write_byte_count = 0;
	pend->c->tcp_is_reading = 0;
	comm_point_start_listening(pend->c, s, -1);
	/* set timer on the waiting_tcp entry, this is the write timeout
	 * for the written packet.  The timer on pend->c is the timer
	 * for when there is no written packet and we have readtimeouts */
#ifndef S_SPLINT_S
	tv.tv_sec = w->timeout/1000;
	tv.tv_usec = (w->timeout%1000)*1000;
#endif
	/* if the waiting_tcp was previously waiting for a buffer in the
	 * outside_network.tcpwaitlist, then the timer is reset now that
	 * we start writing it */
	comm_timer_set(w->timer, &tv);
}

/** use next free buffer to service a tcp query */
static int
outnet_tcp_take_into_use(struct waiting_tcp* w)
{
	struct pending_tcp* pend = w->outnet->tcp_free;
	int s;
	log_assert(pend);
	log_assert(w->pkt);
	log_assert(w->pkt_len > 0);
	log_assert(w->addrlen > 0);
	pend->c->tcp_do_toggle_rw = 0;
	pend->c->tcp_do_close = 0;

	/* Consistency check, if we have ssl_upstream but no sslctx, then
	 * log an error and return failure.
	 */
	if (w->ssl_upstream && !w->outnet->sslctx) {
		log_err("SSL upstream requested but no SSL context");
		return 0;
	}

	/* open socket */
	s = outnet_get_tcp_fd(&w->addr, w->addrlen, w->outnet->tcp_mss,
		w->outnet->ip_dscp, w->ssl_upstream);

	if(s == -1)
		return 0;

	if(!pick_outgoing_tcp(pend, w, s))
		return 0;

	fd_set_nonblock(s);
#ifdef USE_OSX_MSG_FASTOPEN
	/* API for fast open is different here. We use a connectx() function and 
	   then writes can happen as normal even using SSL.*/
	/* connectx requires that the len be set in the sockaddr struct*/
	struct sockaddr_in *addr_in = (struct sockaddr_in *)&w->addr;
	addr_in->sin_len = w->addrlen;
	sa_endpoints_t endpoints;
	endpoints.sae_srcif = 0;
	endpoints.sae_srcaddr = NULL;
	endpoints.sae_srcaddrlen = 0;
	endpoints.sae_dstaddr = (struct sockaddr *)&w->addr;
	endpoints.sae_dstaddrlen = w->addrlen;
	if (connectx(s, &endpoints, SAE_ASSOCID_ANY,  
	             CONNECT_DATA_IDEMPOTENT | CONNECT_RESUME_ON_READ_WRITE,
	             NULL, 0, NULL, NULL) == -1) {
		/* if fails, failover to connect for OSX 10.10 */
#ifdef EINPROGRESS
		if(errno != EINPROGRESS) {
#else
		if(1) {
#endif
			if(connect(s, (struct sockaddr*)&w->addr, w->addrlen) == -1) {
#else /* USE_OSX_MSG_FASTOPEN*/
#ifdef USE_MSG_FASTOPEN
	pend->c->tcp_do_fastopen = 1;
	/* Only do TFO for TCP in which case no connect() is required here.
	   Don't combine client TFO with SSL, since OpenSSL can't 
	   currently support doing a handshake on fd that already isn't connected*/
	if (w->outnet->sslctx && w->ssl_upstream) {
		if(connect(s, (struct sockaddr*)&w->addr, w->addrlen) == -1) {
#else /* USE_MSG_FASTOPEN*/
	if(connect(s, (struct sockaddr*)&w->addr, w->addrlen) == -1) {
#endif /* USE_MSG_FASTOPEN*/
#endif /* USE_OSX_MSG_FASTOPEN*/
#ifndef USE_WINSOCK
#ifdef EINPROGRESS
		if(errno != EINPROGRESS) {
#else
		if(1) {
#endif
			if(tcp_connect_errno_needs_log(
				(struct sockaddr*)&w->addr, w->addrlen))
				log_err_addr("outgoing tcp: connect",
					strerror(errno), &w->addr, w->addrlen);
			close(s);
#else /* USE_WINSOCK */
		if(WSAGetLastError() != WSAEINPROGRESS &&
			WSAGetLastError() != WSAEWOULDBLOCK) {
			closesocket(s);
#endif
			return 0;
		}
	}
#ifdef USE_MSG_FASTOPEN
	}
#endif /* USE_MSG_FASTOPEN */
#ifdef USE_OSX_MSG_FASTOPEN
		}
	}
#endif /* USE_OSX_MSG_FASTOPEN */
	if(w->outnet->sslctx && w->ssl_upstream) {
		pend->c->ssl = outgoing_ssl_fd(w->outnet->sslctx, s);
		if(!pend->c->ssl) {
			pend->c->fd = s;
			comm_point_close(pend->c);
			return 0;
		}
		verbose(VERB_ALGO, "the query is using TLS encryption, for %s",
			(w->tls_auth_name?w->tls_auth_name:"an unauthenticated connection"));
#ifdef USE_WINSOCK
		comm_point_tcp_win_bio_cb(pend->c, pend->c->ssl);
#endif
		pend->c->ssl_shake_state = comm_ssl_shake_write;
		if(!set_auth_name_on_ssl(pend->c->ssl, w->tls_auth_name,
			w->outnet->tls_use_sni)) {
			pend->c->fd = s;
#ifdef HAVE_SSL
			SSL_free(pend->c->ssl);
#endif
			pend->c->ssl = NULL;
			comm_point_close(pend->c);
			return 0;
		}
	}
	w->next_waiting = (void*)pend;
	w->outnet->num_tcp_outgoing++;
	w->outnet->tcp_free = pend->next_free;
	pend->next_free = NULL;
	pend->query = w;
	pend->reuse.outnet = w->outnet;
	pend->c->repinfo.remote_addrlen = w->addrlen;
	pend->c->tcp_more_read_again = &pend->reuse.cp_more_read_again;
	pend->c->tcp_more_write_again = &pend->reuse.cp_more_write_again;
	pend->reuse.cp_more_read_again = 0;
	pend->reuse.cp_more_write_again = 0;
	memcpy(&pend->c->repinfo.remote_addr, &w->addr, w->addrlen);
	pend->reuse.pending = pend;

	/* Remove from tree in case the is_ssl will be different and causes the
	 * identity of the reuse_tcp to change; could result in nodes not being
	 * deleted from the tree (because the new identity does not match the
	 * previous node) but their ->key would be changed to NULL. */
	if(pend->reuse.node.key)
		reuse_tcp_remove_tree_list(w->outnet, &pend->reuse);

	if(pend->c->ssl)
		pend->reuse.is_ssl = 1;
	else	pend->reuse.is_ssl = 0;
	/* insert in reuse by address tree if not already inserted there */
	(void)reuse_tcp_insert(w->outnet, pend);
	reuse_tree_by_id_insert(&pend->reuse, w);
	outnet_tcp_take_query_setup(s, pend, w);
	return 1;
}

/** Touch the lru of a reuse_tcp element, it is in use.
 * This moves it to the front of the list, where it is not likely to
 * be closed.  Items at the back of the list are closed to make space. */
void
reuse_tcp_lru_touch(struct outside_network* outnet, struct reuse_tcp* reuse)
{
	if(!reuse->item_on_lru_list) {
		log_err("internal error: we need to touch the lru_list but item not in list");
		return; /* not on the list, no lru to modify */
	}
	log_assert(reuse->lru_prev ||
		(!reuse->lru_prev && outnet->tcp_reuse_first == reuse));
	if(!reuse->lru_prev)
		return; /* already first in the list */
	/* remove at current position */
	/* since it is not first, there is a previous element */
	reuse->lru_prev->lru_next = reuse->lru_next;
	log_assert(reuse->lru_prev->lru_next != reuse->lru_prev);
	if(reuse->lru_next)
		reuse->lru_next->lru_prev = reuse->lru_prev;
	else	outnet->tcp_reuse_last = reuse->lru_prev;
	log_assert(!reuse->lru_next || reuse->lru_next->lru_prev != reuse->lru_next);
	log_assert(outnet->tcp_reuse_last != outnet->tcp_reuse_last->lru_next &&
		outnet->tcp_reuse_last != outnet->tcp_reuse_last->lru_prev);
	/* insert at the front */
	reuse->lru_prev = NULL;
	reuse->lru_next = outnet->tcp_reuse_first;
	if(outnet->tcp_reuse_first) {
		outnet->tcp_reuse_first->lru_prev = reuse;
	}
	log_assert(reuse->lru_next != reuse);
	/* since it is not first, it is not the only element and
	 * lru_next is thus not NULL and thus reuse is now not the last in
	 * the list, so outnet->tcp_reuse_last does not need to be modified */
	outnet->tcp_reuse_first = reuse;
	log_assert(outnet->tcp_reuse_first != outnet->tcp_reuse_first->lru_next &&
		outnet->tcp_reuse_first != outnet->tcp_reuse_first->lru_prev);
	log_assert((!outnet->tcp_reuse_first && !outnet->tcp_reuse_last) ||
		(outnet->tcp_reuse_first && outnet->tcp_reuse_last));
}

/** Snip the last reuse_tcp element off of the LRU list */
struct reuse_tcp*
reuse_tcp_lru_snip(struct outside_network* outnet)
{
	struct reuse_tcp* reuse = outnet->tcp_reuse_last;
	if(!reuse) return NULL;
	/* snip off of LRU */
	log_assert(reuse->lru_next == NULL);
	if(reuse->lru_prev) {
		outnet->tcp_reuse_last = reuse->lru_prev;
		reuse->lru_prev->lru_next = NULL;
	} else {
		outnet->tcp_reuse_last = NULL;
		outnet->tcp_reuse_first = NULL;
	}
	log_assert((!outnet->tcp_reuse_first && !outnet->tcp_reuse_last) ||
		(outnet->tcp_reuse_first && outnet->tcp_reuse_last));
	reuse->item_on_lru_list = 0;
	reuse->lru_next = NULL;
	reuse->lru_prev = NULL;
	return reuse;
}

/** remove waiting tcp from the outnet waiting list */
void
outnet_waiting_tcp_list_remove(struct outside_network* outnet, struct waiting_tcp* w)
{
	struct waiting_tcp* p = outnet->tcp_wait_first, *prev = NULL;
	w->on_tcp_waiting_list = 0;
	while(p) {
		if(p == w) {
			/* remove w */
			if(prev)
				prev->next_waiting = w->next_waiting;
			else	outnet->tcp_wait_first = w->next_waiting;
			if(outnet->tcp_wait_last == w)
				outnet->tcp_wait_last = prev;
			w->next_waiting = NULL;
			return;
		}
		prev = p;
		p = p->next_waiting;
	}
	/* outnet_waiting_tcp_list_remove is currently called only with items
	 * that are already in the waiting list. */
	log_assert(0);
}

/** pop the first waiting tcp from the outnet waiting list */
struct waiting_tcp*
outnet_waiting_tcp_list_pop(struct outside_network* outnet)
{
	struct waiting_tcp* w = outnet->tcp_wait_first;
	if(!outnet->tcp_wait_first) return NULL;
	log_assert(w->on_tcp_waiting_list);
	outnet->tcp_wait_first = w->next_waiting;
	if(outnet->tcp_wait_last == w)
		outnet->tcp_wait_last = NULL;
	w->on_tcp_waiting_list = 0;
	w->next_waiting = NULL;
	return w;
}

/** add waiting_tcp element to the outnet tcp waiting list */
void
outnet_waiting_tcp_list_add(struct outside_network* outnet,
	struct waiting_tcp* w, int set_timer)
{
	struct timeval tv;
	log_assert(!w->on_tcp_waiting_list);
	if(w->on_tcp_waiting_list)
		return;
	w->next_waiting = NULL;
	if(outnet->tcp_wait_last)
		outnet->tcp_wait_last->next_waiting = w;
	else	outnet->tcp_wait_first = w;
	outnet->tcp_wait_last = w;
	w->on_tcp_waiting_list = 1;
	if(set_timer) {
#ifndef S_SPLINT_S
		tv.tv_sec = w->timeout/1000;
		tv.tv_usec = (w->timeout%1000)*1000;
#endif
		comm_timer_set(w->timer, &tv);
	}
}

/** add waiting_tcp element as first to the outnet tcp waiting list */
void
outnet_waiting_tcp_list_add_first(struct outside_network* outnet,
	struct waiting_tcp* w, int reset_timer)
{
	struct timeval tv;
	log_assert(!w->on_tcp_waiting_list);
	if(w->on_tcp_waiting_list)
		return;
	w->next_waiting = outnet->tcp_wait_first;
	log_assert(w->next_waiting != w);
	if(!outnet->tcp_wait_last)
		outnet->tcp_wait_last = w;
	outnet->tcp_wait_first = w;
	w->on_tcp_waiting_list = 1;
	if(reset_timer) {
#ifndef S_SPLINT_S
		tv.tv_sec = w->timeout/1000;
		tv.tv_usec = (w->timeout%1000)*1000;
#endif
		comm_timer_set(w->timer, &tv);
	}
	log_assert(
		(!outnet->tcp_reuse_first && !outnet->tcp_reuse_last) ||
		(outnet->tcp_reuse_first && outnet->tcp_reuse_last));
}

/** call callback on waiting_tcp, if not NULL */
static void
waiting_tcp_callback(struct waiting_tcp* w, struct comm_point* c, int error,
	struct comm_reply* reply_info)
{
	if(w && w->cb) {
		fptr_ok(fptr_whitelist_pending_tcp(w->cb));
		(void)(*w->cb)(c, w->cb_arg, error, reply_info);
	}
}

/** see if buffers can be used to service TCP queries */
static void
use_free_buffer(struct outside_network* outnet)
{
	struct waiting_tcp* w;
	while(outnet->tcp_wait_first && !outnet->want_to_quit) {
#ifdef USE_DNSTAP
		struct pending_tcp* pend_tcp = NULL;
#endif
		struct reuse_tcp* reuse = NULL;
		w = outnet_waiting_tcp_list_pop(outnet);
		log_assert(
			(!outnet->tcp_reuse_first && !outnet->tcp_reuse_last) ||
			(outnet->tcp_reuse_first && outnet->tcp_reuse_last));
		reuse = reuse_tcp_find(outnet, &w->addr, w->addrlen,
			w->ssl_upstream);
		/* re-select an ID when moving to a new TCP buffer */
		w->id = tcp_select_id(outnet, reuse);
		LDNS_ID_SET(w->pkt, w->id);
		if(reuse) {
			log_reuse_tcp(VERB_CLIENT, "use free buffer for waiting tcp: "
				"found reuse", reuse);
#ifdef USE_DNSTAP
			pend_tcp = reuse->pending;
#endif
			reuse_tcp_lru_touch(outnet, reuse);
			comm_timer_disable(w->timer);
			w->next_waiting = (void*)reuse->pending;
			reuse_tree_by_id_insert(reuse, w);
			if(reuse->pending->query) {
				/* on the write wait list */
				reuse_write_wait_push_back(reuse, w);
			} else {
				/* write straight away */
				/* stop the timer on read of the fd */
				comm_point_stop_listening(reuse->pending->c);
				reuse->pending->query = w;
				outnet_tcp_take_query_setup(
					reuse->pending->c->fd, reuse->pending,
					w);
			}
		} else if(outnet->tcp_free) {
			struct pending_tcp* pend = w->outnet->tcp_free;
			rbtree_init(&pend->reuse.tree_by_id, reuse_id_cmp);
			pend->reuse.pending = pend;
			memcpy(&pend->reuse.addr, &w->addr, w->addrlen);
			pend->reuse.addrlen = w->addrlen;
			if(!outnet_tcp_take_into_use(w)) {
				waiting_tcp_callback(w, NULL, NETEVENT_CLOSED,
					NULL);
				waiting_tcp_delete(w);
#ifdef USE_DNSTAP
				w = NULL;
#endif
			}
#ifdef USE_DNSTAP
			pend_tcp = pend;
#endif
		} else {
			/* no reuse and no free buffer, put back at the start */
			outnet_waiting_tcp_list_add_first(outnet, w, 0);
			break;
		}
#ifdef USE_DNSTAP
		if(outnet->dtenv && pend_tcp && w && w->sq &&
			(outnet->dtenv->log_resolver_query_messages ||
			outnet->dtenv->log_forwarder_query_messages)) {
			sldns_buffer tmp;
			sldns_buffer_init_frm_data(&tmp, w->pkt, w->pkt_len);
			dt_msg_send_outside_query(outnet->dtenv, &w->sq->addr,
				&pend_tcp->pi->addr, comm_tcp, NULL, w->sq->zone,
				w->sq->zonelen, &tmp);
		}
#endif
	}
}

/** delete element from tree by id */
static void
reuse_tree_by_id_delete(struct reuse_tcp* reuse, struct waiting_tcp* w)
{
#ifdef UNBOUND_DEBUG
	rbnode_type* rem;
#endif
	log_assert(w->id_node.key != NULL);
#ifdef UNBOUND_DEBUG
	rem =
#else
	(void)
#endif
	rbtree_delete(&reuse->tree_by_id, w);
	log_assert(rem);  /* should have been there */
	w->id_node.key = NULL;
}

/** move writewait list to go for another connection. */
static void
reuse_move_writewait_away(struct outside_network* outnet,
	struct pending_tcp* pend)
{
	/* the writewait list has not been written yet, so if the
	 * stream was closed, they have not actually been failed, only
	 * the queries written.  Other queries can get written to another
	 * stream.  For upstreams that do not support multiple queries
	 * and answers, the stream can get closed, and then the queries
	 * can get written on a new socket */
	struct waiting_tcp* w;
	if(pend->query && pend->query->error_count == 0 &&
		pend->c->tcp_write_pkt == pend->query->pkt &&
		pend->c->tcp_write_pkt_len == pend->query->pkt_len) {
		/* since the current query is not written, it can also
		 * move to a free buffer */
		if(verbosity >= VERB_CLIENT && pend->query->pkt_len > 12+2+2 &&
			LDNS_QDCOUNT(pend->query->pkt) > 0 &&
			dname_valid(pend->query->pkt+12, pend->query->pkt_len-12)) {
			char buf[LDNS_MAX_DOMAINLEN];
			dname_str(pend->query->pkt+12, buf);
			verbose(VERB_CLIENT, "reuse_move_writewait_away current %s %d bytes were written",
				buf, (int)pend->c->tcp_write_byte_count);
		}
		pend->c->tcp_write_pkt = NULL;
		pend->c->tcp_write_pkt_len = 0;
		pend->c->tcp_write_and_read = 0;
		pend->reuse.cp_more_read_again = 0;
		pend->reuse.cp_more_write_again = 0;
		pend->c->tcp_is_reading = 1;
		w = pend->query;
		pend->query = NULL;
		/* increase error count, so that if the next socket fails too
		 * the server selection is run again with this query failed
		 * and it can select a different server (if possible), or
		 * fail the query */
		w->error_count ++;
		reuse_tree_by_id_delete(&pend->reuse, w);
		outnet_waiting_tcp_list_add(outnet, w, 1);
	}
	while((w = reuse_write_wait_pop(&pend->reuse)) != NULL) {
		if(verbosity >= VERB_CLIENT && w->pkt_len > 12+2+2 &&
			LDNS_QDCOUNT(w->pkt) > 0 &&
			dname_valid(w->pkt+12, w->pkt_len-12)) {
			char buf[LDNS_MAX_DOMAINLEN];
			dname_str(w->pkt+12, buf);
			verbose(VERB_CLIENT, "reuse_move_writewait_away item %s", buf);
		}
		reuse_tree_by_id_delete(&pend->reuse, w);
		outnet_waiting_tcp_list_add(outnet, w, 1);
	}
}

/** remove reused element from tree and lru list */
void
reuse_tcp_remove_tree_list(struct outside_network* outnet,
	struct reuse_tcp* reuse)
{
	verbose(VERB_CLIENT, "reuse_tcp_remove_tree_list");
	if(reuse->node.key) {
		/* delete it from reuse tree */
		if(!rbtree_delete(&outnet->tcp_reuse, reuse)) {
			/* should not be possible, it should be there */
			char buf[256];
			addr_to_str(&reuse->addr, reuse->addrlen, buf,
				sizeof(buf));
			log_err("reuse tcp delete: node not present, internal error, %s ssl %d lru %d", buf, reuse->is_ssl, reuse->item_on_lru_list);
		}
		reuse->node.key = NULL;
		/* defend against loops on broken tree by zeroing the
		 * rbnode structure */
		memset(&reuse->node, 0, sizeof(reuse->node));
	}
	/* delete from reuse list */
	if(reuse->item_on_lru_list) {
		if(reuse->lru_prev) {
			/* assert that members of the lru list are waiting
			 * and thus have a pending pointer to the struct */
			log_assert(reuse->lru_prev->pending);
			reuse->lru_prev->lru_next = reuse->lru_next;
			log_assert(reuse->lru_prev->lru_next != reuse->lru_prev);
		} else {
			log_assert(!reuse->lru_next || reuse->lru_next->pending);
			outnet->tcp_reuse_first = reuse->lru_next;
			log_assert(!outnet->tcp_reuse_first ||
				(outnet->tcp_reuse_first !=
				 outnet->tcp_reuse_first->lru_next &&
				 outnet->tcp_reuse_first !=
				 outnet->tcp_reuse_first->lru_prev));
		}
		if(reuse->lru_next) {
			/* assert that members of the lru list are waiting
			 * and thus have a pending pointer to the struct */
			log_assert(reuse->lru_next->pending);
			reuse->lru_next->lru_prev = reuse->lru_prev;
			log_assert(reuse->lru_next->lru_prev != reuse->lru_next);
		} else {
			log_assert(!reuse->lru_prev || reuse->lru_prev->pending);
			outnet->tcp_reuse_last = reuse->lru_prev;
			log_assert(!outnet->tcp_reuse_last ||
				(outnet->tcp_reuse_last !=
				 outnet->tcp_reuse_last->lru_next &&
				 outnet->tcp_reuse_last !=
				 outnet->tcp_reuse_last->lru_prev));
		}
		log_assert((!outnet->tcp_reuse_first && !outnet->tcp_reuse_last) ||
			(outnet->tcp_reuse_first && outnet->tcp_reuse_last));
		reuse->item_on_lru_list = 0;
		reuse->lru_next = NULL;
		reuse->lru_prev = NULL;
	}
	reuse->pending = NULL;
}

/** helper function that deletes an element from the tree of readwait
 * elements in tcp reuse structure */
static void reuse_del_readwait_elem(rbnode_type* node, void* ATTR_UNUSED(arg))
{
	struct waiting_tcp* w = (struct waiting_tcp*)node->key;
	waiting_tcp_delete(w);
}

/** delete readwait waiting_tcp elements, deletes the elements in the list */
void reuse_del_readwait(rbtree_type* tree_by_id)
{
	if(tree_by_id->root == NULL ||
		tree_by_id->root == RBTREE_NULL)
		return;
	traverse_postorder(tree_by_id, &reuse_del_readwait_elem, NULL);
	rbtree_init(tree_by_id, reuse_id_cmp);
}

/** decommission a tcp buffer, closes commpoint and frees waiting_tcp entry */
static void
decommission_pending_tcp(struct outside_network* outnet, 
	struct pending_tcp* pend)
{
	verbose(VERB_CLIENT, "decommission_pending_tcp");
	/* A certain code path can lead here twice for the same pending_tcp
	 * creating a loop in the free pending_tcp list. */
	if(outnet->tcp_free != pend) {
		pend->next_free = outnet->tcp_free;
		outnet->tcp_free = pend;
	}
	if(pend->reuse.node.key) {
		/* needs unlink from the reuse tree to get deleted */
		reuse_tcp_remove_tree_list(outnet, &pend->reuse);
	}
	/* free SSL structure after remove from outnet tcp reuse tree,
	 * because the c->ssl null or not is used for sorting in the tree */
	if(pend->c->ssl) {
#ifdef HAVE_SSL
		SSL_shutdown(pend->c->ssl);
		SSL_free(pend->c->ssl);
		pend->c->ssl = NULL;
#endif
	}
	comm_point_close(pend->c);
	pend->reuse.cp_more_read_again = 0;
	pend->reuse.cp_more_write_again = 0;
	/* unlink the query and writewait list, it is part of the tree
	 * nodes and is deleted */
	pend->query = NULL;
	pend->reuse.write_wait_first = NULL;
	pend->reuse.write_wait_last = NULL;
	reuse_del_readwait(&pend->reuse.tree_by_id);
}

/** perform failure callbacks for waiting queries in reuse read rbtree */
static void reuse_cb_readwait_for_failure(rbtree_type* tree_by_id, int err)
{
	rbnode_type* node;
	if(tree_by_id->root == NULL ||
		tree_by_id->root == RBTREE_NULL)
		return;
	node = rbtree_first(tree_by_id);
	while(node && node != RBTREE_NULL) {
		struct waiting_tcp* w = (struct waiting_tcp*)node->key;
		waiting_tcp_callback(w, NULL, err, NULL);
		node = rbtree_next(node);
	}
}

/** mark the entry for being in the cb_and_decommission stage */
static void mark_for_cb_and_decommission(rbnode_type* node,
	void* ATTR_UNUSED(arg))
{
	struct waiting_tcp* w = (struct waiting_tcp*)node->key;
	/* Mark the waiting_tcp to signal later code (serviced_delete) that
	 * this item is part of the backed up tree_by_id and will be deleted
	 * later. */
	w->in_cb_and_decommission = 1;
	/* Mark the serviced_query for deletion so that later code through
	 * callbacks (iter_clear .. outnet_serviced_query_stop) won't
	 * prematurely delete it. */
	if(w->cb)
		((struct serviced_query*)w->cb_arg)->to_be_deleted = 1;
}

/** perform callbacks for failure and also decommission pending tcp.
 * the callbacks remove references in sq->pending to the waiting_tcp
 * members of the tree_by_id in the pending tcp.  The pending_tcp is
 * removed before the callbacks, so that the callbacks do not modify
 * the pending_tcp due to its reference in the outside_network reuse tree */
static void reuse_cb_and_decommission(struct outside_network* outnet,
	struct pending_tcp* pend, int error)
{
	rbtree_type store;
	store = pend->reuse.tree_by_id;
	pend->query = NULL;
	rbtree_init(&pend->reuse.tree_by_id, reuse_id_cmp);
	pend->reuse.write_wait_first = NULL;
	pend->reuse.write_wait_last = NULL;
	decommission_pending_tcp(outnet, pend);
	if(store.root != NULL && store.root != RBTREE_NULL) {
		traverse_postorder(&store, &mark_for_cb_and_decommission, NULL);
	}
	reuse_cb_readwait_for_failure(&store, error);
	reuse_del_readwait(&store);
}

/** set timeout on tcp fd and setup read event to catch incoming dns msgs */
static void
reuse_tcp_setup_timeout(struct pending_tcp* pend_tcp, int tcp_reuse_timeout)
{
	log_reuse_tcp(VERB_CLIENT, "reuse_tcp_setup_timeout", &pend_tcp->reuse);
	comm_point_start_listening(pend_tcp->c, -1, tcp_reuse_timeout);
}

/** set timeout on tcp fd and setup read event to catch incoming dns msgs */
static void
reuse_tcp_setup_read_and_timeout(struct pending_tcp* pend_tcp, int tcp_reuse_timeout)
{
	log_reuse_tcp(VERB_CLIENT, "reuse_tcp_setup_readtimeout", &pend_tcp->reuse);
	sldns_buffer_clear(pend_tcp->c->buffer);
	pend_tcp->c->tcp_is_reading = 1;
	pend_tcp->c->tcp_byte_count = 0;
	comm_point_stop_listening(pend_tcp->c);
	comm_point_start_listening(pend_tcp->c, -1, tcp_reuse_timeout);
}

int 
outnet_tcp_cb(struct comm_point* c, void* arg, int error,
	struct comm_reply *reply_info)
{
	struct pending_tcp* pend = (struct pending_tcp*)arg;
	struct outside_network* outnet = pend->reuse.outnet;
	struct waiting_tcp* w = NULL;
	log_assert(pend->reuse.item_on_lru_list && pend->reuse.node.key);
	verbose(VERB_ALGO, "outnettcp cb");
	if(error == NETEVENT_TIMEOUT) {
		if(pend->c->tcp_write_and_read) {
			verbose(VERB_QUERY, "outnettcp got tcp timeout "
				"for read, ignored because write underway");
			/* if we are writing, ignore readtimer, wait for write timer
			 * or write is done */
			return 0;
		} else {
			verbose(VERB_QUERY, "outnettcp got tcp timeout %s",
				(pend->reuse.tree_by_id.count?"for reading pkt":
				"for keepalive for reuse"));
		}
		/* must be timeout for reading or keepalive reuse,
		 * close it. */
		reuse_tcp_remove_tree_list(outnet, &pend->reuse);
	} else if(error == NETEVENT_PKT_WRITTEN) {
		/* the packet we want to write has been written. */
		verbose(VERB_ALGO, "outnet tcp pkt was written event");
		log_assert(c == pend->c);
		log_assert(pend->query->pkt == pend->c->tcp_write_pkt);
		log_assert(pend->query->pkt_len == pend->c->tcp_write_pkt_len);
		pend->c->tcp_write_pkt = NULL;
		pend->c->tcp_write_pkt_len = 0;
		/* the pend.query is already in tree_by_id */
		log_assert(pend->query->id_node.key);
		pend->query = NULL;
		/* setup to write next packet or setup read timeout */
		if(pend->reuse.write_wait_first) {
			verbose(VERB_ALGO, "outnet tcp setup next pkt");
			/* we can write it straight away perhaps, set flag
			 * because this callback called after a tcp write
			 * succeeded and likely more buffer space is available
			 * and we can write some more. */
			pend->reuse.cp_more_write_again = 1;
			pend->query = reuse_write_wait_pop(&pend->reuse);
			comm_point_stop_listening(pend->c);
			outnet_tcp_take_query_setup(pend->c->fd, pend,
				pend->query);
		} else {
			verbose(VERB_ALGO, "outnet tcp writes done, wait");
			pend->c->tcp_write_and_read = 0;
			pend->reuse.cp_more_read_again = 0;
			pend->reuse.cp_more_write_again = 0;
			pend->c->tcp_is_reading = 1;
			comm_point_stop_listening(pend->c);
			reuse_tcp_setup_timeout(pend, outnet->tcp_reuse_timeout);
		}
		return 0;
	} else if(error != NETEVENT_NOERROR) {
		verbose(VERB_QUERY, "outnettcp got tcp error %d", error);
		reuse_move_writewait_away(outnet, pend);
		/* pass error below and exit */
	} else {
		/* check ID */
		if(sldns_buffer_limit(c->buffer) < sizeof(uint16_t)) {
			log_addr(VERB_QUERY, 
				"outnettcp: bad ID in reply, too short, from:",
				&pend->reuse.addr, pend->reuse.addrlen);
			error = NETEVENT_CLOSED;
		} else {
			uint16_t id = LDNS_ID_WIRE(sldns_buffer_begin(
				c->buffer));
			/* find the query the reply is for */
			w = reuse_tcp_by_id_find(&pend->reuse, id);
			/* Make sure that the reply we got is at least for a
			 * sent query with the same ID; the waiting_tcp that
			 * gets a reply is assumed to not be waiting to be
			 * sent. */
			if(w && (w->on_tcp_waiting_list || w->write_wait_queued))
				w = NULL;
		}
	}
	if(error == NETEVENT_NOERROR && !w) {
		/* no struct waiting found in tree, no reply to call */
		log_addr(VERB_QUERY, "outnettcp: bad ID in reply, from:",
			&pend->reuse.addr, pend->reuse.addrlen);
		error = NETEVENT_CLOSED;
	}
	if(error == NETEVENT_NOERROR) {
		/* add to reuse tree so it can be reused, if not a failure.
		 * This is possible if the state machine wants to make a tcp
		 * query again to the same destination. */
		if(outnet->tcp_reuse.count < outnet->tcp_reuse_max) {
			(void)reuse_tcp_insert(outnet, pend);
		}
	}
	if(w) {
		log_assert(!w->on_tcp_waiting_list);
		log_assert(!w->write_wait_queued);
		reuse_tree_by_id_delete(&pend->reuse, w);
		verbose(VERB_CLIENT, "outnet tcp callback query err %d buflen %d",
			error, (int)sldns_buffer_limit(c->buffer));
		waiting_tcp_callback(w, c, error, reply_info);
		waiting_tcp_delete(w);
	}
	verbose(VERB_CLIENT, "outnet_tcp_cb reuse after cb");
	if(error == NETEVENT_NOERROR && pend->reuse.node.key) {
		verbose(VERB_CLIENT, "outnet_tcp_cb reuse after cb: keep it");
		/* it is in the reuse_tcp tree, with other queries, or
		 * on the empty list. do not decommission it */
		/* if there are more outstanding queries, we could try to
		 * read again, to see if it is on the input,
		 * because this callback called after a successful read
		 * and there could be more bytes to read on the input */
		if(pend->reuse.tree_by_id.count != 0)
			pend->reuse.cp_more_read_again = 1;
		reuse_tcp_setup_read_and_timeout(pend, outnet->tcp_reuse_timeout);
		return 0;
	}
	verbose(VERB_CLIENT, "outnet_tcp_cb reuse after cb: decommission it");
	/* no queries on it, no space to keep it. or timeout or closed due
	 * to error.  Close it */
	reuse_cb_and_decommission(outnet, pend, (error==NETEVENT_TIMEOUT?
		NETEVENT_TIMEOUT:NETEVENT_CLOSED));
	use_free_buffer(outnet);
	return 0;
}

/** lower use count on pc, see if it can be closed */
static void
portcomm_loweruse(struct outside_network* outnet, struct port_comm* pc)
{
	struct port_if* pif;
	pc->num_outstanding--;
	if(pc->num_outstanding > 0) {
		return;
	}
	/* close it and replace in unused list */
	verbose(VERB_ALGO, "close of port %d", pc->number);
	comm_point_close(pc->cp);
	pif = pc->pif;
	log_assert(pif->inuse > 0);
#ifndef DISABLE_EXPLICIT_PORT_RANDOMISATION
	pif->avail_ports[pif->avail_total - pif->inuse] = pc->number;
#endif
	pif->inuse--;
	pif->out[pc->index] = pif->out[pif->inuse];
	pif->out[pc->index]->index = pc->index;
	pc->next = outnet->unused_fds;
	outnet->unused_fds = pc;
}

/** try to send waiting UDP queries */
static void
outnet_send_wait_udp(struct outside_network* outnet)
{
	struct pending* pend;
	/* process waiting queries */
	while(outnet->udp_wait_first && outnet->unused_fds
		&& !outnet->want_to_quit) {
		pend = outnet->udp_wait_first;
		outnet->udp_wait_first = pend->next_waiting;
		if(!pend->next_waiting) outnet->udp_wait_last = NULL;
		sldns_buffer_clear(outnet->udp_buff);
		sldns_buffer_write(outnet->udp_buff, pend->pkt, pend->pkt_len);
		sldns_buffer_flip(outnet->udp_buff);
		free(pend->pkt); /* freeing now makes get_mem correct */
		pend->pkt = NULL;
		pend->pkt_len = 0;
		log_assert(!pend->sq->busy);
		pend->sq->busy = 1;
		if(!randomize_and_send_udp(pend, outnet->udp_buff,
			pend->timeout)) {
			/* callback error on pending */
			if(pend->cb) {
				fptr_ok(fptr_whitelist_pending_udp(pend->cb));
				(void)(*pend->cb)(outnet->unused_fds->cp, pend->cb_arg, 
					NETEVENT_CLOSED, NULL);
			}
			pending_delete(outnet, pend);
		} else {
			pend->sq->busy = 0;
		}
	}
}

int 
outnet_udp_cb(struct comm_point* c, void* arg, int error,
	struct comm_reply *reply_info)
{
	struct outside_network* outnet = (struct outside_network*)arg;
	struct pending key;
	struct pending* p;
	verbose(VERB_ALGO, "answer cb");

	if(error != NETEVENT_NOERROR) {
		verbose(VERB_QUERY, "outnetudp got udp error %d", error);
		return 0;
	}
	if(sldns_buffer_limit(c->buffer) < LDNS_HEADER_SIZE) {
		verbose(VERB_QUERY, "outnetudp udp too short");
		return 0;
	}
	log_assert(reply_info);

	/* setup lookup key */
	key.id = (unsigned)LDNS_ID_WIRE(sldns_buffer_begin(c->buffer));
	memcpy(&key.addr, &reply_info->remote_addr, reply_info->remote_addrlen);
	key.addrlen = reply_info->remote_addrlen;
	verbose(VERB_ALGO, "Incoming reply id = %4.4x", key.id);
	log_addr(VERB_ALGO, "Incoming reply addr =", 
		&reply_info->remote_addr, reply_info->remote_addrlen);

	/* find it, see if this thing is a valid query response */
	verbose(VERB_ALGO, "lookup size is %d entries", (int)outnet->pending->count);
	p = (struct pending*)rbtree_search(outnet->pending, &key);
	if(!p) {
		verbose(VERB_QUERY, "received unwanted or unsolicited udp reply dropped.");
		log_buf(VERB_ALGO, "dropped message", c->buffer);
		outnet->unwanted_replies++;
		if(outnet->unwanted_threshold && ++outnet->unwanted_total 
			>= outnet->unwanted_threshold) {
			log_warn("unwanted reply total reached threshold (%u)"
				" you may be under attack."
				" defensive action: clearing the cache",
				(unsigned)outnet->unwanted_threshold);
			fptr_ok(fptr_whitelist_alloc_cleanup(
				outnet->unwanted_action));
			(*outnet->unwanted_action)(outnet->unwanted_param);
			outnet->unwanted_total = 0;
		}
		return 0;
	}

	verbose(VERB_ALGO, "received udp reply.");
	log_buf(VERB_ALGO, "udp message", c->buffer);
	if(p->pc->cp != c) {
		verbose(VERB_QUERY, "received reply id,addr on wrong port. "
			"dropped.");
		outnet->unwanted_replies++;
		if(outnet->unwanted_threshold && ++outnet->unwanted_total 
			>= outnet->unwanted_threshold) {
			log_warn("unwanted reply total reached threshold (%u)"
				" you may be under attack."
				" defensive action: clearing the cache",
				(unsigned)outnet->unwanted_threshold);
			fptr_ok(fptr_whitelist_alloc_cleanup(
				outnet->unwanted_action));
			(*outnet->unwanted_action)(outnet->unwanted_param);
			outnet->unwanted_total = 0;
		}
		return 0;
	}
	comm_timer_disable(p->timer);
	verbose(VERB_ALGO, "outnet handle udp reply");
	/* delete from tree first in case callback creates a retry */
	(void)rbtree_delete(outnet->pending, p->node.key);
	if(p->cb) {
		fptr_ok(fptr_whitelist_pending_udp(p->cb));
		(void)(*p->cb)(p->pc->cp, p->cb_arg, NETEVENT_NOERROR, reply_info);
	}
	portcomm_loweruse(outnet, p->pc);
	pending_delete(NULL, p);
	outnet_send_wait_udp(outnet);
	return 0;
}

/** calculate number of ip4 and ip6 interfaces*/
static void 
calc_num46(char** ifs, int num_ifs, int do_ip4, int do_ip6, 
	int* num_ip4, int* num_ip6)
{
	int i;
	*num_ip4 = 0;
	*num_ip6 = 0;
	if(num_ifs <= 0) {
		if(do_ip4)
			*num_ip4 = 1;
		if(do_ip6)
			*num_ip6 = 1;
		return;
	}
	for(i=0; i<num_ifs; i++)
	{
		if(str_is_ip6(ifs[i])) {
			if(do_ip6)
				(*num_ip6)++;
		} else {
			if(do_ip4)
				(*num_ip4)++;
		}
	}
}

void
pending_udp_timer_delay_cb(void* arg)
{
	struct pending* p = (struct pending*)arg;
	struct outside_network* outnet = p->outnet;
	verbose(VERB_ALGO, "timeout udp with delay");
	portcomm_loweruse(outnet, p->pc);
	pending_delete(outnet, p);
	outnet_send_wait_udp(outnet);
}

void 
pending_udp_timer_cb(void *arg)
{
	struct pending* p = (struct pending*)arg;
	struct outside_network* outnet = p->outnet;
	/* it timed out */
	verbose(VERB_ALGO, "timeout udp");
	if(p->cb) {
		fptr_ok(fptr_whitelist_pending_udp(p->cb));
		(void)(*p->cb)(p->pc->cp, p->cb_arg, NETEVENT_TIMEOUT, NULL);
	}
	/* if delayclose, keep port open for a longer time.
	 * But if the udpwaitlist exists, then we are struggling to
	 * keep up with demand for sockets, so do not wait, but service
	 * the customer (customer service more important than portICMPs) */
	if(outnet->delayclose && !outnet->udp_wait_first) {
		p->cb = NULL;
		p->timer->callback = &pending_udp_timer_delay_cb;
		comm_timer_set(p->timer, &outnet->delay_tv);
		return;
	}
	portcomm_loweruse(outnet, p->pc);
	pending_delete(outnet, p);
	outnet_send_wait_udp(outnet);
}

/** create pending_tcp buffers */
static int
create_pending_tcp(struct outside_network* outnet, size_t bufsize)
{
	size_t i;
	if(outnet->num_tcp == 0)
		return 1; /* no tcp needed, nothing to do */
	if(!(outnet->tcp_conns = (struct pending_tcp **)calloc(
			outnet->num_tcp, sizeof(struct pending_tcp*))))
		return 0;
	for(i=0; i<outnet->num_tcp; i++) {
		if(!(outnet->tcp_conns[i] = (struct pending_tcp*)calloc(1, 
			sizeof(struct pending_tcp))))
			return 0;
		outnet->tcp_conns[i]->next_free = outnet->tcp_free;
		outnet->tcp_free = outnet->tcp_conns[i];
		outnet->tcp_conns[i]->c = comm_point_create_tcp_out(
			outnet->base, bufsize, outnet_tcp_cb, 
			outnet->tcp_conns[i]);
		if(!outnet->tcp_conns[i]->c)
			return 0;
	}
	return 1;
}

/** setup an outgoing interface, ready address */
static int setup_if(struct port_if* pif, const char* addrstr, 
	int* avail, int numavail, size_t numfd)
{
#ifndef DISABLE_EXPLICIT_PORT_RANDOMISATION
	pif->avail_total = numavail;
	pif->avail_ports = (int*)memdup(avail, (size_t)numavail*sizeof(int));
	if(!pif->avail_ports)
		return 0;
#endif
	if(!ipstrtoaddr(addrstr, UNBOUND_DNS_PORT, &pif->addr, &pif->addrlen) &&
	   !netblockstrtoaddr(addrstr, UNBOUND_DNS_PORT,
			      &pif->addr, &pif->addrlen, &pif->pfxlen))
		return 0;
	pif->maxout = (int)numfd;
	pif->inuse = 0;
	pif->out = (struct port_comm**)calloc(numfd, 
		sizeof(struct port_comm*));
	if(!pif->out)
		return 0;
	return 1;
}

struct outside_network* 
outside_network_create(struct comm_base *base, size_t bufsize, 
	size_t num_ports, char** ifs, int num_ifs, int do_ip4, 
	int do_ip6, size_t num_tcp, int dscp, struct infra_cache* infra,
	struct ub_randstate* rnd, int use_caps_for_id, int* availports, 
	int numavailports, size_t unwanted_threshold, int tcp_mss,
	void (*unwanted_action)(void*), void* unwanted_param, int do_udp,
	void* sslctx, int delayclose, int tls_use_sni, struct dt_env* dtenv,
	int udp_connect, int max_reuse_tcp_queries, int tcp_reuse_timeout,
	int tcp_auth_query_timeout)
{
	struct outside_network* outnet = (struct outside_network*)
		calloc(1, sizeof(struct outside_network));
	size_t k;
	if(!outnet) {
		log_err("malloc failed");
		return NULL;
	}
	comm_base_timept(base, &outnet->now_secs, &outnet->now_tv);
	outnet->base = base;
	outnet->num_tcp = num_tcp;
	outnet->max_reuse_tcp_queries = max_reuse_tcp_queries;
	outnet->tcp_reuse_timeout= tcp_reuse_timeout;
	outnet->tcp_auth_query_timeout = tcp_auth_query_timeout;
	outnet->num_tcp_outgoing = 0;
	outnet->num_udp_outgoing = 0;
	outnet->infra = infra;
	outnet->rnd = rnd;
	outnet->sslctx = sslctx;
	outnet->tls_use_sni = tls_use_sni;
#ifdef USE_DNSTAP
	outnet->dtenv = dtenv;
#else
	(void)dtenv;
#endif
	outnet->svcd_overhead = 0;
	outnet->want_to_quit = 0;
	outnet->unwanted_threshold = unwanted_threshold;
	outnet->unwanted_action = unwanted_action;
	outnet->unwanted_param = unwanted_param;
	outnet->use_caps_for_id = use_caps_for_id;
	outnet->do_udp = do_udp;
	outnet->tcp_mss = tcp_mss;
	outnet->ip_dscp = dscp;
#ifndef S_SPLINT_S
	if(delayclose) {
		outnet->delayclose = 1;
		outnet->delay_tv.tv_sec = delayclose/1000;
		outnet->delay_tv.tv_usec = (delayclose%1000)*1000;
	}
#endif
	if(udp_connect) {
		outnet->udp_connect = 1;
	}
	if(numavailports == 0 || num_ports == 0) {
		log_err("no outgoing ports available");
		outside_network_delete(outnet);
		return NULL;
	}
#ifndef INET6
	do_ip6 = 0;
#endif
	calc_num46(ifs, num_ifs, do_ip4, do_ip6, 
		&outnet->num_ip4, &outnet->num_ip6);
	if(outnet->num_ip4 != 0) {
		if(!(outnet->ip4_ifs = (struct port_if*)calloc(
			(size_t)outnet->num_ip4, sizeof(struct port_if)))) {
			log_err("malloc failed");
			outside_network_delete(outnet);
			return NULL;
		}
	}
	if(outnet->num_ip6 != 0) {
		if(!(outnet->ip6_ifs = (struct port_if*)calloc(
			(size_t)outnet->num_ip6, sizeof(struct port_if)))) {
			log_err("malloc failed");
			outside_network_delete(outnet);
			return NULL;
		}
	}
	if(	!(outnet->udp_buff = sldns_buffer_new(bufsize)) ||
		!(outnet->pending = rbtree_create(pending_cmp)) ||
		!(outnet->serviced = rbtree_create(serviced_cmp)) ||
		!create_pending_tcp(outnet, bufsize)) {
		log_err("malloc failed");
		outside_network_delete(outnet);
		return NULL;
	}
	rbtree_init(&outnet->tcp_reuse, reuse_cmp);
	outnet->tcp_reuse_max = num_tcp;

	/* allocate commpoints */
	for(k=0; k<num_ports; k++) {
		struct port_comm* pc;
		pc = (struct port_comm*)calloc(1, sizeof(*pc));
		if(!pc) {
			log_err("malloc failed");
			outside_network_delete(outnet);
			return NULL;
		}
		pc->cp = comm_point_create_udp(outnet->base, -1, 
			outnet->udp_buff, 0, outnet_udp_cb, outnet, NULL);
		if(!pc->cp) {
			log_err("malloc failed");
			free(pc);
			outside_network_delete(outnet);
			return NULL;
		}
		pc->next = outnet->unused_fds;
		outnet->unused_fds = pc;
	}

	/* allocate interfaces */
	if(num_ifs == 0) {
		if(do_ip4 && !setup_if(&outnet->ip4_ifs[0], "0.0.0.0", 
			availports, numavailports, num_ports)) {
			log_err("malloc failed");
			outside_network_delete(outnet);
			return NULL;
		}
		if(do_ip6 && !setup_if(&outnet->ip6_ifs[0], "::", 
			availports, numavailports, num_ports)) {
			log_err("malloc failed");
			outside_network_delete(outnet);
			return NULL;
		}
	} else {
		size_t done_4 = 0, done_6 = 0;
		int i;
		for(i=0; i<num_ifs; i++) {
			if(str_is_ip6(ifs[i]) && do_ip6) {
				if(!setup_if(&outnet->ip6_ifs[done_6], ifs[i],
					availports, numavailports, num_ports)){
					log_err("malloc failed");
					outside_network_delete(outnet);
					return NULL;
				}
				done_6++;
			}
			if(!str_is_ip6(ifs[i]) && do_ip4) {
				if(!setup_if(&outnet->ip4_ifs[done_4], ifs[i],
					availports, numavailports, num_ports)){
					log_err("malloc failed");
					outside_network_delete(outnet);
					return NULL;
				}
				done_4++;
			}
		}
	}
	return outnet;
}

/** helper pending delete */
static void
pending_node_del(rbnode_type* node, void* arg)
{
	struct pending* pend = (struct pending*)node;
	struct outside_network* outnet = (struct outside_network*)arg;
	pending_delete(outnet, pend);
}

/** helper serviced delete */
static void
serviced_node_del(rbnode_type* node, void* ATTR_UNUSED(arg))
{
	struct serviced_query* sq = (struct serviced_query*)node;
	alloc_reg_release(sq->alloc, sq->region);
	if(sq->timer)
		comm_timer_delete(sq->timer);
	free(sq);
}

void 
outside_network_quit_prepare(struct outside_network* outnet)
{
	if(!outnet)
		return;
	/* prevent queued items from being sent */
	outnet->want_to_quit = 1; 
}

void 
outside_network_delete(struct outside_network* outnet)
{
	if(!outnet)
		return;
	outnet->want_to_quit = 1;
	/* check every element, since we can be called on malloc error */
	if(outnet->pending) {
		/* free pending elements, but do no unlink from tree. */
		traverse_postorder(outnet->pending, pending_node_del, NULL);
		free(outnet->pending);
	}
	if(outnet->serviced) {
		traverse_postorder(outnet->serviced, serviced_node_del, NULL);
		free(outnet->serviced);
	}
	if(outnet->udp_buff)
		sldns_buffer_free(outnet->udp_buff);
	if(outnet->unused_fds) {
		struct port_comm* p = outnet->unused_fds, *np;
		while(p) {
			np = p->next;
			comm_point_delete(p->cp);
			free(p);
			p = np;
		}
		outnet->unused_fds = NULL;
	}
	if(outnet->ip4_ifs) {
		int i, k;
		for(i=0; i<outnet->num_ip4; i++) {
			for(k=0; k<outnet->ip4_ifs[i].inuse; k++) {
				struct port_comm* pc = outnet->ip4_ifs[i].
					out[k];
				comm_point_delete(pc->cp);
				free(pc);
			}
#ifndef DISABLE_EXPLICIT_PORT_RANDOMISATION
			free(outnet->ip4_ifs[i].avail_ports);
#endif
			free(outnet->ip4_ifs[i].out);
		}
		free(outnet->ip4_ifs);
	}
	if(outnet->ip6_ifs) {
		int i, k;
		for(i=0; i<outnet->num_ip6; i++) {
			for(k=0; k<outnet->ip6_ifs[i].inuse; k++) {
				struct port_comm* pc = outnet->ip6_ifs[i].
					out[k];
				comm_point_delete(pc->cp);
				free(pc);
			}
#ifndef DISABLE_EXPLICIT_PORT_RANDOMISATION
			free(outnet->ip6_ifs[i].avail_ports);
#endif
			free(outnet->ip6_ifs[i].out);
		}
		free(outnet->ip6_ifs);
	}
	if(outnet->tcp_conns) {
		size_t i;
		for(i=0; i<outnet->num_tcp; i++)
			if(outnet->tcp_conns[i]) {
				struct pending_tcp* pend;
				pend = outnet->tcp_conns[i];
				if(pend->reuse.item_on_lru_list) {
					/* delete waiting_tcp elements that
					 * the tcp conn is working on */
					decommission_pending_tcp(outnet, pend);
				}
				comm_point_delete(outnet->tcp_conns[i]->c);
				free(outnet->tcp_conns[i]);
				outnet->tcp_conns[i] = NULL;
			}
		free(outnet->tcp_conns);
		outnet->tcp_conns = NULL;
	}
	if(outnet->tcp_wait_first) {
		struct waiting_tcp* p = outnet->tcp_wait_first, *np;
		while(p) {
			np = p->next_waiting;
			waiting_tcp_delete(p);
			p = np;
		}
	}
	/* was allocated in struct pending that was deleted above */
	rbtree_init(&outnet->tcp_reuse, reuse_cmp);
	outnet->tcp_reuse_first = NULL;
	outnet->tcp_reuse_last = NULL;
	if(outnet->udp_wait_first) {
		struct pending* p = outnet->udp_wait_first, *np;
		while(p) {
			np = p->next_waiting;
			pending_delete(NULL, p);
			p = np;
		}
	}
	free(outnet);
}

void 
pending_delete(struct outside_network* outnet, struct pending* p)
{
	if(!p)
		return;
	if(outnet && outnet->udp_wait_first &&
		(p->next_waiting || p == outnet->udp_wait_last) ) {
		/* delete from waiting list, if it is in the waiting list */
		struct pending* prev = NULL, *x = outnet->udp_wait_first;
		while(x && x != p) {
			prev = x;
			x = x->next_waiting;
		}
		if(x) {
			log_assert(x == p);
			if(prev)
				prev->next_waiting = p->next_waiting;
			else	outnet->udp_wait_first = p->next_waiting;
			if(outnet->udp_wait_last == p)
				outnet->udp_wait_last = prev;
		}
	}
	if(outnet) {
		(void)rbtree_delete(outnet->pending, p->node.key);
	}
	if(p->timer)
		comm_timer_delete(p->timer);
	free(p->pkt);
	free(p);
}

static void
sai6_putrandom(struct sockaddr_in6 *sa, int pfxlen, struct ub_randstate *rnd)
{
	int i, last;
	if(!(pfxlen > 0 && pfxlen < 128))
		return;
	for(i = 0; i < (128 - pfxlen) / 8; i++) {
		sa->sin6_addr.s6_addr[15-i] = (uint8_t)ub_random_max(rnd, 256);
	}
	last = pfxlen & 7;
	if(last != 0) {
		sa->sin6_addr.s6_addr[15-i] |=
			((0xFF >> last) & ub_random_max(rnd, 256));
	}
}

/**
 * Try to open a UDP socket for outgoing communication.
 * Sets sockets options as needed.
 * @param addr: socket address.
 * @param addrlen: length of address.
 * @param pfxlen: length of network prefix (for address randomisation).
 * @param port: port override for addr.
 * @param inuse: if -1 is returned, this bool means the port was in use.
 * @param rnd: random state (for address randomisation).
 * @param dscp: DSCP to use.
 * @return fd or -1
 */
static int
udp_sockport(struct sockaddr_storage* addr, socklen_t addrlen, int pfxlen,
	int port, int* inuse, struct ub_randstate* rnd, int dscp)
{
	int fd, noproto;
	if(addr_is_ip6(addr, addrlen)) {
		int freebind = 0;
		struct sockaddr_in6 sa = *(struct sockaddr_in6*)addr;
		sa.sin6_port = (in_port_t)htons((uint16_t)port);
		sa.sin6_flowinfo = 0;
		sa.sin6_scope_id = 0;
		if(pfxlen != 0) {
			freebind = 1;
			sai6_putrandom(&sa, pfxlen, rnd);
		}
		fd = create_udp_sock(AF_INET6, SOCK_DGRAM, 
			(struct sockaddr*)&sa, addrlen, 1, inuse, &noproto,
			0, 0, 0, NULL, 0, freebind, 0, dscp);
	} else {
		struct sockaddr_in* sa = (struct sockaddr_in*)addr;
		sa->sin_port = (in_port_t)htons((uint16_t)port);
		fd = create_udp_sock(AF_INET, SOCK_DGRAM, 
			(struct sockaddr*)addr, addrlen, 1, inuse, &noproto,
			0, 0, 0, NULL, 0, 0, 0, dscp);
	}
	return fd;
}

/** Select random ID */
static int
select_id(struct outside_network* outnet, struct pending* pend,
	sldns_buffer* packet)
{
	int id_tries = 0;
	pend->id = GET_RANDOM_ID(outnet->rnd);
	LDNS_ID_SET(sldns_buffer_begin(packet), pend->id);

	/* insert in tree */
	pend->node.key = pend;
	while(!rbtree_insert(outnet->pending, &pend->node)) {
		/* change ID to avoid collision */
		pend->id = GET_RANDOM_ID(outnet->rnd);
		LDNS_ID_SET(sldns_buffer_begin(packet), pend->id);
		id_tries++;
		if(id_tries == MAX_ID_RETRY) {
			pend->id=99999; /* non existent ID */
			log_err("failed to generate unique ID, drop msg");
			return 0;
		}
	}
	verbose(VERB_ALGO, "inserted new pending reply id=%4.4x", pend->id);
	return 1;
}

/** return true is UDP connect error needs to be logged */
static int udp_connect_needs_log(int err, struct sockaddr_storage* addr,
	socklen_t addrlen)
{
	switch(err) {
	case ECONNREFUSED:
#  ifdef ENETUNREACH
	case ENETUNREACH:
#  endif
#  ifdef EHOSTDOWN
	case EHOSTDOWN:
#  endif
#  ifdef EHOSTUNREACH
	case EHOSTUNREACH:
#  endif
#  ifdef ENETDOWN
	case ENETDOWN:
#  endif
#  ifdef EADDRNOTAVAIL
	case EADDRNOTAVAIL:
#  endif
	case EPERM:
	case EACCES:
		if(verbosity >= VERB_ALGO)
			return 1;
		return 0;
	case EINVAL:
		/* Stop 'Invalid argument for fe80::/10' addresses appearing
		 * in the logs, at low verbosity. They cannot be sent to. */
		if(addr_is_ip6linklocal(addr, addrlen)) {
			if(verbosity >= VERB_ALGO)
				return 1;
			return 0;
		}
		break;
	default:
		break;
	}
	return 1;
}


/** Select random interface and port */
static int
select_ifport(struct outside_network* outnet, struct pending* pend,
	int num_if, struct port_if* ifs)
{
	int my_if, my_port, fd, portno, inuse, tries=0;
	struct port_if* pif;
	/* randomly select interface and port */
	if(num_if == 0) {
		verbose(VERB_QUERY, "Need to send query but have no "
			"outgoing interfaces of that family");
		return 0;
	}
	log_assert(outnet->unused_fds);
	tries = 0;
	while(1) {
		my_if = ub_random_max(outnet->rnd, num_if);
		pif = &ifs[my_if];
#ifndef DISABLE_EXPLICIT_PORT_RANDOMISATION
		if(outnet->udp_connect) {
			/* if we connect() we cannot reuse fds for a port */
			if(pif->inuse >= pif->avail_total) {
				tries++;
				if(tries < MAX_PORT_RETRY)
					continue;
				log_err("failed to find an open port, drop msg");
				return 0;
			}
			my_port = pif->inuse + ub_random_max(outnet->rnd,
				pif->avail_total - pif->inuse);
		} else  {
			my_port = ub_random_max(outnet->rnd, pif->avail_total);
			if(my_port < pif->inuse) {
				/* port already open */
				pend->pc = pif->out[my_port];
				verbose(VERB_ALGO, "using UDP if=%d port=%d",
					my_if, pend->pc->number);
				break;
			}
		}
		/* try to open new port, if fails, loop to try again */
		log_assert(pif->inuse < pif->maxout);
		portno = pif->avail_ports[my_port - pif->inuse];
#else
		my_port = portno = 0;
#endif
		fd = udp_sockport(&pif->addr, pif->addrlen, pif->pfxlen,
			portno, &inuse, outnet->rnd, outnet->ip_dscp);
		if(fd == -1 && !inuse) {
			/* nonrecoverable error making socket */
			return 0;
		}
		if(fd != -1) {
			verbose(VERB_ALGO, "opened UDP if=%d port=%d", 
				my_if, portno);
			if(outnet->udp_connect) {
				/* connect() to the destination */
				if(connect(fd, (struct sockaddr*)&pend->addr,
					pend->addrlen) < 0) {
					if(udp_connect_needs_log(errno,
						&pend->addr, pend->addrlen)) {
						log_err_addr("udp connect failed",
							strerror(errno), &pend->addr,
							pend->addrlen);
					}
					sock_close(fd);
					return 0;
				}
			}
			/* grab fd */
			pend->pc = outnet->unused_fds;
			outnet->unused_fds = pend->pc->next;

			/* setup portcomm */
			pend->pc->next = NULL;
			pend->pc->number = portno;
			pend->pc->pif = pif;
			pend->pc->index = pif->inuse;
			pend->pc->num_outstanding = 0;
			comm_point_start_listening(pend->pc->cp, fd, -1);

			/* grab port in interface */
			pif->out[pif->inuse] = pend->pc;
#ifndef DISABLE_EXPLICIT_PORT_RANDOMISATION
			pif->avail_ports[my_port - pif->inuse] =
				pif->avail_ports[pif->avail_total-pif->inuse-1];
#endif
			pif->inuse++;
			break;
		}
		/* failed, already in use */
		verbose(VERB_QUERY, "port %d in use, trying another", portno);
		tries++;
		if(tries == MAX_PORT_RETRY) {
			log_err("failed to find an open port, drop msg");
			return 0;
		}
	}
	log_assert(pend->pc);
	pend->pc->num_outstanding++;

	return 1;
}

static int
randomize_and_send_udp(struct pending* pend, sldns_buffer* packet, int timeout)
{
	struct timeval tv;
	struct outside_network* outnet = pend->sq->outnet;

	/* select id */
	if(!select_id(outnet, pend, packet)) {
		return 0;
	}

	/* select src_if, port */
	if(addr_is_ip6(&pend->addr, pend->addrlen)) {
		if(!select_ifport(outnet, pend, 
			outnet->num_ip6, outnet->ip6_ifs))
			return 0;
	} else {
		if(!select_ifport(outnet, pend, 
			outnet->num_ip4, outnet->ip4_ifs))
			return 0;
	}
	log_assert(pend->pc && pend->pc->cp);

	/* send it over the commlink */
	if(!comm_point_send_udp_msg(pend->pc->cp, packet,
		(struct sockaddr*)&pend->addr, pend->addrlen, outnet->udp_connect)) {
		portcomm_loweruse(outnet, pend->pc);
		return 0;
	}
	outnet->num_udp_outgoing++;

	/* system calls to set timeout after sending UDP to make roundtrip
	   smaller. */
#ifndef S_SPLINT_S
	tv.tv_sec = timeout/1000;
	tv.tv_usec = (timeout%1000)*1000;
#endif
	comm_timer_set(pend->timer, &tv);

#ifdef USE_DNSTAP
	/*
	 * sending src (local service)/dst (upstream) addresses over DNSTAP
	 * There are no chances to get the src (local service) addr if unbound
	 * is not configured with specific outgoing IP-addresses. So we will
	 * pass 0.0.0.0 (::) to argument for
	 * dt_msg_send_outside_query()/dt_msg_send_outside_response() calls.
	 */
	if(outnet->dtenv &&
	   (outnet->dtenv->log_resolver_query_messages ||
		outnet->dtenv->log_forwarder_query_messages)) {
			log_addr(VERB_ALGO, "from local addr", &pend->pc->pif->addr, pend->pc->pif->addrlen);
			log_addr(VERB_ALGO, "request to upstream", &pend->addr, pend->addrlen);
			dt_msg_send_outside_query(outnet->dtenv, &pend->addr, &pend->pc->pif->addr, comm_udp, NULL,
				pend->sq->zone, pend->sq->zonelen, packet);
	}
#endif
	return 1;
}

struct pending* 
pending_udp_query(struct serviced_query* sq, struct sldns_buffer* packet,
	int timeout, comm_point_callback_type* cb, void* cb_arg)
{
	struct pending* pend = (struct pending*)calloc(1, sizeof(*pend));
	if(!pend) return NULL;
	pend->outnet = sq->outnet;
	pend->sq = sq;
	pend->addrlen = sq->addrlen;
	memmove(&pend->addr, &sq->addr, sq->addrlen);
	pend->cb = cb;
	pend->cb_arg = cb_arg;
	pend->node.key = pend;
	pend->timer = comm_timer_create(sq->outnet->base, pending_udp_timer_cb,
		pend);
	if(!pend->timer) {
		free(pend);
		return NULL;
	}

	if(sq->outnet->unused_fds == NULL) {
		/* no unused fd, cannot create a new port (randomly) */
		verbose(VERB_ALGO, "no fds available, udp query waiting");
		pend->timeout = timeout;
		pend->pkt_len = sldns_buffer_limit(packet);
		pend->pkt = (uint8_t*)memdup(sldns_buffer_begin(packet),
			pend->pkt_len);
		if(!pend->pkt) {
			comm_timer_delete(pend->timer);
			free(pend);
			return NULL;
		}
		/* put at end of waiting list */
		if(sq->outnet->udp_wait_last)
			sq->outnet->udp_wait_last->next_waiting = pend;
		else 
			sq->outnet->udp_wait_first = pend;
		sq->outnet->udp_wait_last = pend;
		return pend;
	}
	log_assert(!sq->busy);
	sq->busy = 1;
	if(!randomize_and_send_udp(pend, packet, timeout)) {
		pending_delete(sq->outnet, pend);
		return NULL;
	}
	sq->busy = 0;
	return pend;
}

void
outnet_tcptimer(void* arg)
{
	struct waiting_tcp* w = (struct waiting_tcp*)arg;
	struct outside_network* outnet = w->outnet;
	verbose(VERB_CLIENT, "outnet_tcptimer");
	if(w->on_tcp_waiting_list) {
		/* it is on the waiting list */
		outnet_waiting_tcp_list_remove(outnet, w);
		waiting_tcp_callback(w, NULL, NETEVENT_TIMEOUT, NULL);
		waiting_tcp_delete(w);
	} else {
		/* it was in use */
		struct pending_tcp* pend=(struct pending_tcp*)w->next_waiting;
		reuse_cb_and_decommission(outnet, pend, NETEVENT_TIMEOUT);
	}
	use_free_buffer(outnet);
}

/** close the oldest reuse_tcp connection to make a fd and struct pend
 * available for a new stream connection */
static void
reuse_tcp_close_oldest(struct outside_network* outnet)
{
	struct reuse_tcp* reuse;
	verbose(VERB_CLIENT, "reuse_tcp_close_oldest");
	reuse = reuse_tcp_lru_snip(outnet);
	if(!reuse) return;
	/* free up */
	reuse_cb_and_decommission(outnet, reuse->pending, NETEVENT_CLOSED);
}

static uint16_t
tcp_select_id(struct outside_network* outnet, struct reuse_tcp* reuse)
{
	if(reuse)
		return reuse_tcp_select_id(reuse, outnet);
	return GET_RANDOM_ID(outnet->rnd);
}

/** find spare ID value for reuse tcp stream.  That is random and also does
 * not collide with an existing query ID that is in use or waiting */
uint16_t
reuse_tcp_select_id(struct reuse_tcp* reuse, struct outside_network* outnet)
{
	uint16_t id = 0, curid, nextid;
	const int try_random = 2000;
	int i;
	unsigned select, count, space;
	rbnode_type* node;

	/* make really sure the tree is not empty */
	if(reuse->tree_by_id.count == 0) {
		id = GET_RANDOM_ID(outnet->rnd);
		return id;
	}

	/* try to find random empty spots by picking them */
	for(i = 0; i<try_random; i++) {
		id = GET_RANDOM_ID(outnet->rnd);
		if(!reuse_tcp_by_id_find(reuse, id)) {
			return id;
		}
	}

	/* equally pick a random unused element from the tree that is
	 * not in use.  Pick a the n-th index of an unused number,
	 * then loop over the empty spaces in the tree and find it */
	log_assert(reuse->tree_by_id.count < 0xffff);
	select = ub_random_max(outnet->rnd, 0xffff - reuse->tree_by_id.count);
	/* select value now in 0 .. num free - 1 */

	count = 0; /* number of free spaces passed by */
	node = rbtree_first(&reuse->tree_by_id);
	log_assert(node && node != RBTREE_NULL); /* tree not empty */
	/* see if select is before first node */
	if(select < (unsigned)tree_by_id_get_id(node))
		return select;
	count += tree_by_id_get_id(node);
	/* perhaps select is between nodes */
	while(node && node != RBTREE_NULL) {
		rbnode_type* next = rbtree_next(node);
		if(next && next != RBTREE_NULL) {
			curid = tree_by_id_get_id(node);
			nextid = tree_by_id_get_id(next);
			log_assert(curid < nextid);
			if(curid != 0xffff && curid + 1 < nextid) {
				/* space between nodes */
				space = nextid - curid - 1;
				log_assert(select >= count);
				if(select < count + space) {
					/* here it is */
					return curid + 1 + (select - count);
				}
				count += space;
			}
		}
		node = next;
	}

	/* select is after the last node */
	/* count is the number of free positions before the nodes in the
	 * tree */
	node = rbtree_last(&reuse->tree_by_id);
	log_assert(node && node != RBTREE_NULL); /* tree not empty */
	curid = tree_by_id_get_id(node);
	log_assert(count + (0xffff-curid) + reuse->tree_by_id.count == 0xffff);
	return curid + 1 + (select - count);
}

struct waiting_tcp*
pending_tcp_query(struct serviced_query* sq, sldns_buffer* packet,
	int timeout, comm_point_callback_type* callback, void* callback_arg)
{
	struct pending_tcp* pend = sq->outnet->tcp_free;
	struct reuse_tcp* reuse = NULL;
	struct waiting_tcp* w;

	verbose(VERB_CLIENT, "pending_tcp_query");
	if(sldns_buffer_limit(packet) < sizeof(uint16_t)) {
		verbose(VERB_ALGO, "pending tcp query with too short buffer < 2");
		return NULL;
	}

	/* find out if a reused stream to the target exists */
	/* if so, take it into use */
	reuse = reuse_tcp_find(sq->outnet, &sq->addr, sq->addrlen,
		sq->ssl_upstream);
	if(reuse) {
		log_reuse_tcp(VERB_CLIENT, "pending_tcp_query: found reuse", reuse);
		log_assert(reuse->pending);
		pend = reuse->pending;
		reuse_tcp_lru_touch(sq->outnet, reuse);
	}

	log_assert(!reuse || (reuse && pend));
	/* if !pend but we have reuse streams, close a reuse stream
	 * to be able to open a new one to this target, no use waiting
	 * to reuse a file descriptor while another query needs to use
	 * that buffer and file descriptor now. */
	if(!pend) {
		reuse_tcp_close_oldest(sq->outnet);
		pend = sq->outnet->tcp_free;
		log_assert(!reuse || (pend == reuse->pending));
	}

	/* allocate space to store query */
	w = (struct waiting_tcp*)malloc(sizeof(struct waiting_tcp) 
		+ sldns_buffer_limit(packet));
	if(!w) {
		return NULL;
	}
	if(!(w->timer = comm_timer_create(sq->outnet->base, outnet_tcptimer, w))) {
		free(w);
		return NULL;
	}
	w->pkt = (uint8_t*)w + sizeof(struct waiting_tcp);
	w->pkt_len = sldns_buffer_limit(packet);
	memmove(w->pkt, sldns_buffer_begin(packet), w->pkt_len);
	w->id = tcp_select_id(sq->outnet, reuse);
	LDNS_ID_SET(w->pkt, w->id);
	memcpy(&w->addr, &sq->addr, sq->addrlen);
	w->addrlen = sq->addrlen;
	w->outnet = sq->outnet;
	w->on_tcp_waiting_list = 0;
	w->next_waiting = NULL;
	w->cb = callback;
	w->cb_arg = callback_arg;
	w->ssl_upstream = sq->ssl_upstream;
	w->tls_auth_name = sq->tls_auth_name;
	w->timeout = timeout;
	w->id_node.key = NULL;
	w->write_wait_prev = NULL;
	w->write_wait_next = NULL;
	w->write_wait_queued = 0;
	w->error_count = 0;
#ifdef USE_DNSTAP
	w->sq = NULL;
#endif
	w->in_cb_and_decommission = 0;
	if(pend) {
		/* we have a buffer available right now */
		if(reuse) {
			log_assert(reuse == &pend->reuse);
			/* reuse existing fd, write query and continue */
			/* store query in tree by id */
			verbose(VERB_CLIENT, "pending_tcp_query: reuse, store");
			w->next_waiting = (void*)pend;
			reuse_tree_by_id_insert(&pend->reuse, w);
			/* can we write right now? */
			if(pend->query == NULL) {
				/* write straight away */
				/* stop the timer on read of the fd */
				comm_point_stop_listening(pend->c);
				pend->query = w;
				outnet_tcp_take_query_setup(pend->c->fd, pend,
					w);
			} else {
				/* put it in the waiting list for
				 * this stream */
				reuse_write_wait_push_back(&pend->reuse, w);
			}
		} else {
			/* create new fd and connect to addr, setup to
			 * write query */
			verbose(VERB_CLIENT, "pending_tcp_query: new fd, connect");
			rbtree_init(&pend->reuse.tree_by_id, reuse_id_cmp);
			pend->reuse.pending = pend;
			memcpy(&pend->reuse.addr, &sq->addr, sq->addrlen);
			pend->reuse.addrlen = sq->addrlen;
			if(!outnet_tcp_take_into_use(w)) {
				waiting_tcp_delete(w);
				return NULL;
			}
		}
#ifdef USE_DNSTAP
		if(sq->outnet->dtenv &&
		   (sq->outnet->dtenv->log_resolver_query_messages ||
		    sq->outnet->dtenv->log_forwarder_query_messages)) {
			/* use w->pkt, because it has the ID value */
			sldns_buffer tmp;
			sldns_buffer_init_frm_data(&tmp, w->pkt, w->pkt_len);
			dt_msg_send_outside_query(sq->outnet->dtenv, &sq->addr,
				&pend->pi->addr, comm_tcp, NULL, sq->zone,
				sq->zonelen, &tmp);
		}
#endif
	} else {
		/* queue up */
		/* waiting for a buffer on the outside network buffer wait
		 * list */
		verbose(VERB_CLIENT, "pending_tcp_query: queue to wait");
#ifdef USE_DNSTAP
		w->sq = sq;
#endif
		outnet_waiting_tcp_list_add(sq->outnet, w, 1);
	}
	return w;
}

/** create query for serviced queries */
static void
serviced_gen_query(sldns_buffer* buff, uint8_t* qname, size_t qnamelen, 
	uint16_t qtype, uint16_t qclass, uint16_t flags)
{
	sldns_buffer_clear(buff);
	/* skip id */
	sldns_buffer_write_u16(buff, flags);
	sldns_buffer_write_u16(buff, 1); /* qdcount */
	sldns_buffer_write_u16(buff, 0); /* ancount */
	sldns_buffer_write_u16(buff, 0); /* nscount */
	sldns_buffer_write_u16(buff, 0); /* arcount */
	sldns_buffer_write(buff, qname, qnamelen);
	sldns_buffer_write_u16(buff, qtype);
	sldns_buffer_write_u16(buff, qclass);
	sldns_buffer_flip(buff);
}

/** lookup serviced query in serviced query rbtree */
static struct serviced_query*
lookup_serviced(struct outside_network* outnet, sldns_buffer* buff, int dnssec,
	struct sockaddr_storage* addr, socklen_t addrlen,
	struct edns_option* opt_list)
{
	struct serviced_query key;
	key.node.key = &key;
	key.qbuf = sldns_buffer_begin(buff);
	key.qbuflen = sldns_buffer_limit(buff);
	key.dnssec = dnssec;
	memcpy(&key.addr, addr, addrlen);
	key.addrlen = addrlen;
	key.outnet = outnet;
	key.opt_list = opt_list;
	return (struct serviced_query*)rbtree_search(outnet->serviced, &key);
}

void
serviced_timer_cb(void* arg)
{
	struct serviced_query* sq = (struct serviced_query*)arg;
	struct outside_network* outnet = sq->outnet;
	verbose(VERB_ALGO, "serviced send timer");
	/* By the time this cb is called, if we don't have any registered
	 * callbacks for this serviced_query anymore; do not send. */
	if(!sq->cblist)
		goto delete;
	/* perform first network action */
	if(outnet->do_udp && !(sq->tcp_upstream || sq->ssl_upstream)) {
		if(!serviced_udp_send(sq, outnet->udp_buff))
			goto delete;
	} else {
		if(!serviced_tcp_send(sq, outnet->udp_buff))
			goto delete;
	}
	/* Maybe by this time we don't have callbacks attached anymore. Don't
	 * proactively try to delete; let it run and maybe another callback
	 * will get attached by the time we get an answer. */
	return;
delete:
	serviced_callbacks(sq, NETEVENT_CLOSED, NULL, NULL);
}

/** Create new serviced entry */
static struct serviced_query*
serviced_create(struct outside_network* outnet, sldns_buffer* buff, int dnssec,
	int want_dnssec, int nocaps, int tcp_upstream, int ssl_upstream,
	char* tls_auth_name, struct sockaddr_storage* addr, socklen_t addrlen,
	uint8_t* zone, size_t zonelen, int qtype, struct edns_option* opt_list,
	size_t pad_queries_block_size, struct alloc_cache* alloc,
	struct regional* region)
{
	struct serviced_query* sq = (struct serviced_query*)malloc(sizeof(*sq));
	struct timeval t;
#ifdef UNBOUND_DEBUG
	rbnode_type* ins;
#endif
	if(!sq) {
		alloc_reg_release(alloc, region);
		return NULL;
	}
	sq->node.key = sq;
	sq->alloc = alloc;
	sq->region = region;
	sq->qbuf = regional_alloc_init(region, sldns_buffer_begin(buff),
		sldns_buffer_limit(buff));
	if(!sq->qbuf) {
		alloc_reg_release(alloc, region);
		free(sq);
		return NULL;
	}
	sq->qbuflen = sldns_buffer_limit(buff);
	sq->zone = regional_alloc_init(region, zone, zonelen);
	if(!sq->zone) {
		alloc_reg_release(alloc, region);
		free(sq);
		return NULL;
	}
	sq->zonelen = zonelen;
	sq->qtype = qtype;
	sq->dnssec = dnssec;
	sq->want_dnssec = want_dnssec;
	sq->nocaps = nocaps;
	sq->tcp_upstream = tcp_upstream;
	sq->ssl_upstream = ssl_upstream;
	if(tls_auth_name) {
		sq->tls_auth_name = regional_strdup(region, tls_auth_name);
		if(!sq->tls_auth_name) {
			alloc_reg_release(alloc, region);
			free(sq);
			return NULL;
		}
	} else {
		sq->tls_auth_name = NULL;
	}
	memcpy(&sq->addr, addr, addrlen);
	sq->addrlen = addrlen;
	sq->opt_list = opt_list;
	sq->busy = 0;
	sq->timer = comm_timer_create(outnet->base, serviced_timer_cb, sq);
	if(!sq->timer) {
		alloc_reg_release(alloc, region);
		free(sq);
		return NULL;
	}
	memset(&t, 0, sizeof(t));
	comm_timer_set(sq->timer, &t);
	sq->outnet = outnet;
	sq->cblist = NULL;
	sq->pending = NULL;
	sq->status = serviced_initial;
	sq->retry = 0;
	sq->to_be_deleted = 0;
	sq->padding_block_size = pad_queries_block_size;
#ifdef UNBOUND_DEBUG
	ins =
#else
	(void)
#endif
	rbtree_insert(outnet->serviced, &sq->node);
	log_assert(ins != NULL); /* must not be already present */
	return sq;
}

/** reuse tcp stream, remove serviced query from stream,
 * return true if the stream is kept, false if it is to be closed */
static int
reuse_tcp_remove_serviced_keep(struct waiting_tcp* w,
	struct serviced_query* sq)
{
	struct pending_tcp* pend_tcp = (struct pending_tcp*)w->next_waiting;
	verbose(VERB_CLIENT, "reuse_tcp_remove_serviced_keep");
	/* remove the callback. let query continue to write to not cancel
	 * the stream itself.  also keep it as an entry in the tree_by_id,
	 * in case the answer returns (that we no longer want), but we cannot
	 * pick the same ID number meanwhile */
	w->cb = NULL;
	/* see if can be entered in reuse tree
	 * for that the FD has to be non-1 */
	if(pend_tcp->c->fd == -1) {
		verbose(VERB_CLIENT, "reuse_tcp_remove_serviced_keep: -1 fd");
		return 0;
	}
	/* if in tree and used by other queries */
	if(pend_tcp->reuse.node.key) {
		verbose(VERB_CLIENT, "reuse_tcp_remove_serviced_keep: in use by other queries");
		/* do not reset the keepalive timer, for that
		 * we'd need traffic, and this is where the serviced is
		 * removed due to state machine internal reasons,
		 * eg. iterator no longer interested in this query */
		return 1;
	}
	/* if still open and want to keep it open */
	if(pend_tcp->c->fd != -1 && sq->outnet->tcp_reuse.count <
		sq->outnet->tcp_reuse_max) {
		verbose(VERB_CLIENT, "reuse_tcp_remove_serviced_keep: keep open");
		/* set a keepalive timer on it */
		if(!reuse_tcp_insert(sq->outnet, pend_tcp)) {
			return 0;
		}
		reuse_tcp_setup_timeout(pend_tcp, sq->outnet->tcp_reuse_timeout);
		return 1;
	}
	return 0;
}

/** cleanup serviced query entry */
static void
serviced_delete(struct serviced_query* sq)
{
	verbose(VERB_CLIENT, "serviced_delete");
	if(sq->pending) {
		/* clear up the pending query */
		if(sq->status == serviced_query_UDP_EDNS ||
			sq->status == serviced_query_UDP ||
			sq->status == serviced_query_UDP_EDNS_FRAG ||
			sq->status == serviced_query_UDP_EDNS_fallback) {
			struct pending* p = (struct pending*)sq->pending;
			verbose(VERB_CLIENT, "serviced_delete: UDP");
			if(p->pc)
				portcomm_loweruse(sq->outnet, p->pc);
			pending_delete(sq->outnet, p);
			/* this call can cause reentrant calls back into the
			 * mesh */
			outnet_send_wait_udp(sq->outnet);
		} else {
			struct waiting_tcp* w = (struct waiting_tcp*)
				sq->pending;
			verbose(VERB_CLIENT, "serviced_delete: TCP");
			log_assert(!(w->write_wait_queued && w->on_tcp_waiting_list));
			/* if on stream-write-waiting list then
			 * remove from waiting list and waiting_tcp_delete */
			if(w->write_wait_queued) {
				struct pending_tcp* pend =
					(struct pending_tcp*)w->next_waiting;
				verbose(VERB_CLIENT, "serviced_delete: writewait");
				if(!w->in_cb_and_decommission)
					reuse_tree_by_id_delete(&pend->reuse, w);
				reuse_write_wait_remove(&pend->reuse, w);
				if(!w->in_cb_and_decommission)
					waiting_tcp_delete(w);
			} else if(!w->on_tcp_waiting_list) {
				struct pending_tcp* pend =
					(struct pending_tcp*)w->next_waiting;
				verbose(VERB_CLIENT, "serviced_delete: tcpreusekeep");
				/* w needs to stay on tree_by_id to not assign
				 * the same ID; remove the callback since its
				 * serviced_query will be gone. */
				w->cb = NULL;
				if(!reuse_tcp_remove_serviced_keep(w, sq)) {
					if(!w->in_cb_and_decommission)
						reuse_cb_and_decommission(sq->outnet,
							pend, NETEVENT_CLOSED);
					use_free_buffer(sq->outnet);
				}
				sq->pending = NULL;
			} else {
				verbose(VERB_CLIENT, "serviced_delete: tcpwait");
				outnet_waiting_tcp_list_remove(sq->outnet, w);
				if(!w->in_cb_and_decommission)
					waiting_tcp_delete(w);
			}
		}
	}
	/* does not delete from tree, caller has to do that */
	serviced_node_del(&sq->node, NULL);
}

/** perturb a dname capitalization randomly */
static void
serviced_perturb_qname(struct ub_randstate* rnd, uint8_t* qbuf, size_t len)
{
	uint8_t lablen;
	uint8_t* d = qbuf + 10;
	long int random = 0;
	int bits = 0;
	log_assert(len >= 10 + 5 /* offset qname, root, qtype, qclass */);
	(void)len;
	lablen = *d++;
	while(lablen) {
		while(lablen--) {
			/* only perturb A-Z, a-z */
			if(isalpha((unsigned char)*d)) {
				/* get a random bit */	
				if(bits == 0) {
					random = ub_random(rnd);
					bits = 30;
				}
				if(random & 0x1) {
					*d = (uint8_t)toupper((unsigned char)*d);
				} else {
					*d = (uint8_t)tolower((unsigned char)*d);
				}
				random >>= 1;
				bits--;
			}
			d++;
		}
		lablen = *d++;
	}
	if(verbosity >= VERB_ALGO) {
		char buf[LDNS_MAX_DOMAINLEN];
		dname_str(qbuf+10, buf);
		verbose(VERB_ALGO, "qname perturbed to %s", buf);
	}
}

static uint16_t
serviced_query_udp_size(struct serviced_query* sq, enum serviced_query_status status) {
	uint16_t udp_size;
	if(status == serviced_query_UDP_EDNS_FRAG) {
		if(addr_is_ip6(&sq->addr, sq->addrlen)) {
			if(EDNS_FRAG_SIZE_IP6 < EDNS_ADVERTISED_SIZE)
				udp_size = EDNS_FRAG_SIZE_IP6;
			else	udp_size = EDNS_ADVERTISED_SIZE;
		} else {
			if(EDNS_FRAG_SIZE_IP4 < EDNS_ADVERTISED_SIZE)
				udp_size = EDNS_FRAG_SIZE_IP4;
			else	udp_size = EDNS_ADVERTISED_SIZE;
		}
	} else {
		udp_size = EDNS_ADVERTISED_SIZE;
	}
	return udp_size;
}

/** put serviced query into a buffer */
static void
serviced_encode(struct serviced_query* sq, sldns_buffer* buff, int with_edns)
{
	/* if we are using 0x20 bits for ID randomness, perturb them */
	if(sq->outnet->use_caps_for_id && !sq->nocaps) {
		serviced_perturb_qname(sq->outnet->rnd, sq->qbuf, sq->qbuflen);
	}
	/* generate query */
	sldns_buffer_clear(buff);
	sldns_buffer_write_u16(buff, 0); /* id placeholder */
	sldns_buffer_write(buff, sq->qbuf, sq->qbuflen);
	sldns_buffer_flip(buff);
	if(with_edns) {
		/* add edns section */
		struct edns_data edns;
		struct edns_option padding_option;
		edns.edns_present = 1;
		edns.ext_rcode = 0;
		edns.edns_version = EDNS_ADVERTISED_VERSION;
		edns.opt_list_in = NULL;
		edns.opt_list_out = sq->opt_list;
		edns.opt_list_inplace_cb_out = NULL;
		edns.udp_size = serviced_query_udp_size(sq, sq->status);
		edns.bits = 0;
		if(sq->dnssec & EDNS_DO)
			edns.bits = EDNS_DO;
		if(sq->dnssec & BIT_CD)
			LDNS_CD_SET(sldns_buffer_begin(buff));
		if (sq->ssl_upstream && sq->padding_block_size) {
			padding_option.opt_code = LDNS_EDNS_PADDING;
			padding_option.opt_len = 0;
			padding_option.opt_data = NULL;
			padding_option.next = edns.opt_list_out;
			edns.opt_list_out = &padding_option;
			edns.padding_block_size = sq->padding_block_size;
		}
		attach_edns_record(buff, &edns);
	}
}

/**
 * Perform serviced query UDP sending operation.
 * Sends UDP with EDNS, unless infra host marked non EDNS.
 * @param sq: query to send.
 * @param buff: buffer scratch space.
 * @return 0 on error.
 */
static int
serviced_udp_send(struct serviced_query* sq, sldns_buffer* buff)
{
	int rtt, vs;
	uint8_t edns_lame_known;
	time_t now = *sq->outnet->now_secs;

	if(!infra_host(sq->outnet->infra, &sq->addr, sq->addrlen, sq->zone,
		sq->zonelen, now, &vs, &edns_lame_known, &rtt))
		return 0;
	sq->last_rtt = rtt;
	verbose(VERB_ALGO, "EDNS lookup known=%d vs=%d", edns_lame_known, vs);
	if(sq->status == serviced_initial) {
		if(vs != -1) {
			sq->status = serviced_query_UDP_EDNS;
		} else { 	
			sq->status = serviced_query_UDP; 
		}
	}
	serviced_encode(sq, buff, (sq->status == serviced_query_UDP_EDNS) ||
		(sq->status == serviced_query_UDP_EDNS_FRAG));
	sq->last_sent_time = *sq->outnet->now_tv;
	sq->edns_lame_known = (int)edns_lame_known;
	verbose(VERB_ALGO, "serviced query UDP timeout=%d msec", rtt);
	sq->pending = pending_udp_query(sq, buff, rtt,
		serviced_udp_callback, sq);
	if(!sq->pending)
		return 0;
	return 1;
}

/** check that perturbed qname is identical */
static int
serviced_check_qname(sldns_buffer* pkt, uint8_t* qbuf, size_t qbuflen)
{
	uint8_t* d1 = sldns_buffer_begin(pkt)+12;
	uint8_t* d2 = qbuf+10;
	uint8_t len1, len2;
	int count = 0;
	if(sldns_buffer_limit(pkt) < 12+1+4) /* packet too small for qname */
		return 0;
	log_assert(qbuflen >= 15 /* 10 header, root, type, class */);
	len1 = *d1++;
	len2 = *d2++;
	while(len1 != 0 || len2 != 0) {
		if(LABEL_IS_PTR(len1)) {
			/* check if we can read *d1 with compression ptr rest */
			if(d1 >= sldns_buffer_at(pkt, sldns_buffer_limit(pkt)))
				return 0;
			d1 = sldns_buffer_begin(pkt)+PTR_OFFSET(len1, *d1);
			/* check if we can read the destination *d1 */
			if(d1 >= sldns_buffer_at(pkt, sldns_buffer_limit(pkt)))
				return 0;
			len1 = *d1++;
			if(count++ > MAX_COMPRESS_PTRS)
				return 0;
			continue;
		}
		if(d2 > qbuf+qbuflen)
			return 0;
		if(len1 != len2)
			return 0;
		if(len1 > LDNS_MAX_LABELLEN)
			return 0;
		/* check len1 + 1(next length) are okay to read */
		if(d1+len1 >= sldns_buffer_at(pkt, sldns_buffer_limit(pkt)))
			return 0;
		log_assert(len1 <= LDNS_MAX_LABELLEN);
		log_assert(len2 <= LDNS_MAX_LABELLEN);
		log_assert(len1 == len2 && len1 != 0);
		/* compare the labels - bitwise identical */
		if(memcmp(d1, d2, len1) != 0)
			return 0;
		d1 += len1;
		d2 += len2;
		len1 = *d1++;
		len2 = *d2++;
	}
	return 1;
}

/** call the callbacks for a serviced query */
static void
serviced_callbacks(struct serviced_query* sq, int error, struct comm_point* c,
	struct comm_reply* rep)
{
	struct service_callback* p;
	int dobackup = (sq->cblist && sq->cblist->next); /* >1 cb*/
	uint8_t *backup_p = NULL;
	size_t backlen = 0;
#ifdef UNBOUND_DEBUG
	rbnode_type* rem =
#else
	(void)
#endif
	/* remove from tree, and schedule for deletion, so that callbacks
	 * can safely deregister themselves and even create new serviced
	 * queries that are identical to this one. */
	rbtree_delete(sq->outnet->serviced, sq);
	log_assert(rem); /* should have been present */
	sq->to_be_deleted = 1; 
	verbose(VERB_ALGO, "svcd callbacks start");
	if(sq->outnet->use_caps_for_id && error == NETEVENT_NOERROR && c &&
		!sq->nocaps && sq->qtype != LDNS_RR_TYPE_PTR) {
		/* for type PTR do not check perturbed name in answer,
		 * compatibility with cisco dns guard boxes that mess up
		 * reverse queries 0x20 contents */
		/* noerror and nxdomain must have a qname in reply */
		if(sldns_buffer_read_u16_at(c->buffer, 4) == 0 &&
			(LDNS_RCODE_WIRE(sldns_buffer_begin(c->buffer))
				== LDNS_RCODE_NOERROR || 
			 LDNS_RCODE_WIRE(sldns_buffer_begin(c->buffer))
				== LDNS_RCODE_NXDOMAIN)) {
			verbose(VERB_DETAIL, "no qname in reply to check 0x20ID");
			log_addr(VERB_DETAIL, "from server", 
				&sq->addr, sq->addrlen);
			log_buf(VERB_DETAIL, "for packet", c->buffer);
			error = NETEVENT_CLOSED;
			c = NULL;
		} else if(sldns_buffer_read_u16_at(c->buffer, 4) > 0 &&
			!serviced_check_qname(c->buffer, sq->qbuf, 
			sq->qbuflen)) {
			verbose(VERB_DETAIL, "wrong 0x20-ID in reply qname");
			log_addr(VERB_DETAIL, "from server", 
				&sq->addr, sq->addrlen);
			log_buf(VERB_DETAIL, "for packet", c->buffer);
			error = NETEVENT_CAPSFAIL;
			/* and cleanup too */
			pkt_dname_tolower(c->buffer, 
				sldns_buffer_at(c->buffer, 12));
		} else {
			verbose(VERB_ALGO, "good 0x20-ID in reply qname");
			/* cleanup caps, prettier cache contents. */
			pkt_dname_tolower(c->buffer, 
				sldns_buffer_at(c->buffer, 12));
		}
	}
	if(dobackup && c) {
		/* make a backup of the query, since the querystate processing
		 * may send outgoing queries that overwrite the buffer.
		 * use secondary buffer to store the query.
		 * This is a data copy, but faster than packet to server */
		backlen = sldns_buffer_limit(c->buffer);
		backup_p = regional_alloc_init(sq->region,
			sldns_buffer_begin(c->buffer), backlen);
		if(!backup_p) {
			log_err("malloc failure in serviced query callbacks");
			error = NETEVENT_CLOSED;
			c = NULL;
		}
		sq->outnet->svcd_overhead = backlen;
	}
	/* test the actual sq->cblist, because the next elem could be deleted*/
	while((p=sq->cblist) != NULL) {
		sq->cblist = p->next; /* remove this element */
		if(dobackup && c) {
			sldns_buffer_clear(c->buffer);
			sldns_buffer_write(c->buffer, backup_p, backlen);
			sldns_buffer_flip(c->buffer);
		}
		fptr_ok(fptr_whitelist_serviced_query(p->cb));
		(void)(*p->cb)(c, p->cb_arg, error, rep);
	}
	if(backup_p) {
		sq->outnet->svcd_overhead = 0;
	}
	verbose(VERB_ALGO, "svcd callbacks end");
	log_assert(sq->cblist == NULL);
	serviced_delete(sq);
}

int 
serviced_tcp_callback(struct comm_point* c, void* arg, int error,
        struct comm_reply* rep)
{
	struct serviced_query* sq = (struct serviced_query*)arg;
	struct comm_reply r2;
#ifdef USE_DNSTAP
	struct waiting_tcp* w = (struct waiting_tcp*)sq->pending;
	struct pending_tcp* pend_tcp = NULL;
	struct port_if* pi = NULL;
	if(w && !w->on_tcp_waiting_list && w->next_waiting) {
		pend_tcp = (struct pending_tcp*)w->next_waiting;
		pi = pend_tcp->pi;
	}
#endif
	sq->pending = NULL; /* removed after this callback */
	if(error != NETEVENT_NOERROR)
		log_addr(VERB_QUERY, "tcp error for address", 
			&sq->addr, sq->addrlen);
	if(error==NETEVENT_NOERROR)
		infra_update_tcp_works(sq->outnet->infra, &sq->addr,
			sq->addrlen, sq->zone, sq->zonelen);
#ifdef USE_DNSTAP
	/*
	 * sending src (local service)/dst (upstream) addresses over DNSTAP
	 */
	if(error==NETEVENT_NOERROR && pi && sq->outnet->dtenv &&
	   (sq->outnet->dtenv->log_resolver_response_messages ||
	    sq->outnet->dtenv->log_forwarder_response_messages)) {
		log_addr(VERB_ALGO, "response from upstream", &sq->addr, sq->addrlen);
		log_addr(VERB_ALGO, "to local addr", &pi->addr, pi->addrlen);
		dt_msg_send_outside_response(sq->outnet->dtenv, &sq->addr,
			&pi->addr, c->type, c->ssl, sq->zone, sq->zonelen, sq->qbuf,
			sq->qbuflen, &sq->last_sent_time, sq->outnet->now_tv,
			c->buffer);
	}
#endif
	if(error==NETEVENT_NOERROR && sq->status == serviced_query_TCP_EDNS &&
		(LDNS_RCODE_WIRE(sldns_buffer_begin(c->buffer)) == 
		LDNS_RCODE_FORMERR || LDNS_RCODE_WIRE(sldns_buffer_begin(
		c->buffer)) == LDNS_RCODE_NOTIMPL) ) {
		/* attempt to fallback to nonEDNS */
		sq->status = serviced_query_TCP_EDNS_fallback;
		serviced_tcp_initiate(sq, c->buffer);
		return 0;
	} else if(error==NETEVENT_NOERROR && 
		sq->status == serviced_query_TCP_EDNS_fallback &&
			(LDNS_RCODE_WIRE(sldns_buffer_begin(c->buffer)) == 
			LDNS_RCODE_NOERROR || LDNS_RCODE_WIRE(
			sldns_buffer_begin(c->buffer)) == LDNS_RCODE_NXDOMAIN 
			|| LDNS_RCODE_WIRE(sldns_buffer_begin(c->buffer)) 
			== LDNS_RCODE_YXDOMAIN)) {
		/* the fallback produced a result that looks promising, note
		 * that this server should be approached without EDNS */
		/* only store noEDNS in cache if domain is noDNSSEC */
		if(!sq->want_dnssec)
		  if(!infra_edns_update(sq->outnet->infra, &sq->addr, 
			sq->addrlen, sq->zone, sq->zonelen, -1,
			*sq->outnet->now_secs))
			log_err("Out of memory caching no edns for host");
		sq->status = serviced_query_TCP;
	}
	if(sq->tcp_upstream || sq->ssl_upstream) {
	    struct timeval now = *sq->outnet->now_tv;
	    if(error!=NETEVENT_NOERROR) {
	        if(!infra_rtt_update(sq->outnet->infra, &sq->addr,
		    sq->addrlen, sq->zone, sq->zonelen, sq->qtype,
		    -1, sq->last_rtt, (time_t)now.tv_sec))
		    log_err("out of memory in TCP exponential backoff.");
	    } else if(now.tv_sec > sq->last_sent_time.tv_sec ||
		(now.tv_sec == sq->last_sent_time.tv_sec &&
		now.tv_usec > sq->last_sent_time.tv_usec)) {
		/* convert from microseconds to milliseconds */
		int roundtime = ((int)(now.tv_sec - sq->last_sent_time.tv_sec))*1000
		  + ((int)now.tv_usec - (int)sq->last_sent_time.tv_usec)/1000;
		verbose(VERB_ALGO, "measured TCP-time at %d msec", roundtime);
		log_assert(roundtime >= 0);
		/* only store if less then AUTH_TIMEOUT seconds, it could be
		 * huge due to system-hibernated and we woke up */
		if(roundtime < 60000) {
		    if(!infra_rtt_update(sq->outnet->infra, &sq->addr,
			sq->addrlen, sq->zone, sq->zonelen, sq->qtype,
			roundtime, sq->last_rtt, (time_t)now.tv_sec))
			log_err("out of memory noting rtt.");
		}
	    }
	}
	/* insert address into reply info */
	if(!rep) {
		/* create one if there isn't (on errors) */
		rep = &r2;
		r2.c = c;
	}
	memcpy(&rep->remote_addr, &sq->addr, sq->addrlen);
	rep->remote_addrlen = sq->addrlen;
	serviced_callbacks(sq, error, c, rep);
	return 0;
}

static void
serviced_tcp_initiate(struct serviced_query* sq, sldns_buffer* buff)
{
	verbose(VERB_ALGO, "initiate TCP query %s", 
		sq->status==serviced_query_TCP_EDNS?"EDNS":"");
	serviced_encode(sq, buff, sq->status == serviced_query_TCP_EDNS);
	sq->last_sent_time = *sq->outnet->now_tv;
	log_assert(!sq->busy);
	sq->busy = 1;
	sq->pending = pending_tcp_query(sq, buff, sq->outnet->tcp_auth_query_timeout,
		serviced_tcp_callback, sq);
	sq->busy = 0;
	if(!sq->pending) {
		/* delete from tree so that a retry by above layer does not
		 * clash with this entry */
		verbose(VERB_ALGO, "serviced_tcp_initiate: failed to send tcp query");
		serviced_callbacks(sq, NETEVENT_CLOSED, NULL, NULL);
	}
}

/** Send serviced query over TCP return false on initial failure */
static int
serviced_tcp_send(struct serviced_query* sq, sldns_buffer* buff)
{
	int vs, rtt, timeout;
	uint8_t edns_lame_known;
	if(!infra_host(sq->outnet->infra, &sq->addr, sq->addrlen, sq->zone,
		sq->zonelen, *sq->outnet->now_secs, &vs, &edns_lame_known,
		&rtt))
		return 0;
	sq->last_rtt = rtt;
	if(vs != -1)
		sq->status = serviced_query_TCP_EDNS;
	else 	sq->status = serviced_query_TCP;
	serviced_encode(sq, buff, sq->status == serviced_query_TCP_EDNS);
	sq->last_sent_time = *sq->outnet->now_tv;
	if(sq->tcp_upstream || sq->ssl_upstream) {
		timeout = rtt;
		if(rtt >= UNKNOWN_SERVER_NICENESS && rtt < sq->outnet->tcp_auth_query_timeout)
			timeout = sq->outnet->tcp_auth_query_timeout;
	} else {
		timeout = sq->outnet->tcp_auth_query_timeout;
	}
	log_assert(!sq->busy);
	sq->busy = 1;
	sq->pending = pending_tcp_query(sq, buff, timeout,
		serviced_tcp_callback, sq);
	sq->busy = 0;
	return sq->pending != NULL;
}

/* see if packet is edns malformed; got zeroes at start.
 * This is from servers that return malformed packets to EDNS0 queries,
 * but they return good packets for nonEDNS0 queries.
 * We try to detect their output; without resorting to a full parse or
 * check for too many bytes after the end of the packet. */
static int
packet_edns_malformed(struct sldns_buffer* buf, int qtype)
{
	size_t len;
	if(sldns_buffer_limit(buf) < LDNS_HEADER_SIZE)
		return 1; /* malformed */
	/* they have NOERROR rcode, 1 answer. */
	if(LDNS_RCODE_WIRE(sldns_buffer_begin(buf)) != LDNS_RCODE_NOERROR)
		return 0;
	/* one query (to skip) and answer records */
	if(LDNS_QDCOUNT(sldns_buffer_begin(buf)) != 1 ||
		LDNS_ANCOUNT(sldns_buffer_begin(buf)) == 0)
		return 0;
	/* skip qname */
	len = dname_valid(sldns_buffer_at(buf, LDNS_HEADER_SIZE),
		sldns_buffer_limit(buf)-LDNS_HEADER_SIZE);
	if(len == 0)
		return 0;
	if(len == 1 && qtype == 0)
		return 0; /* we asked for '.' and type 0 */
	/* and then 4 bytes (type and class of query) */
	if(sldns_buffer_limit(buf) < LDNS_HEADER_SIZE + len + 4 + 3)
		return 0;

	/* and start with 11 zeroes as the answer RR */
	/* so check the qtype of the answer record, qname=0, type=0 */
	if(sldns_buffer_at(buf, LDNS_HEADER_SIZE+len+4)[0] == 0 &&
	   sldns_buffer_at(buf, LDNS_HEADER_SIZE+len+4)[1] == 0 &&
	   sldns_buffer_at(buf, LDNS_HEADER_SIZE+len+4)[2] == 0)
		return 1;
	return 0;
}

int 
serviced_udp_callback(struct comm_point* c, void* arg, int error,
        struct comm_reply* rep)
{
	struct serviced_query* sq = (struct serviced_query*)arg;
	struct outside_network* outnet = sq->outnet;
	struct timeval now = *sq->outnet->now_tv;
#ifdef USE_DNSTAP
	struct pending* p = (struct pending*)sq->pending;
#endif

	sq->pending = NULL; /* removed after callback */
	if(error == NETEVENT_TIMEOUT) {
		if(sq->status == serviced_query_UDP_EDNS && sq->last_rtt < 5000 &&
		   (serviced_query_udp_size(sq, serviced_query_UDP_EDNS_FRAG) < serviced_query_udp_size(sq, serviced_query_UDP_EDNS))) {
			/* fallback to 1480/1280 */
			sq->status = serviced_query_UDP_EDNS_FRAG;
			log_name_addr(VERB_ALGO, "try edns1xx0", sq->qbuf+10,
				&sq->addr, sq->addrlen);
			if(!serviced_udp_send(sq, c->buffer)) {
				serviced_callbacks(sq, NETEVENT_CLOSED, c, rep);
			}
			return 0;
		}
		if(sq->status == serviced_query_UDP_EDNS_FRAG) {
			/* fragmentation size did not fix it */
			sq->status = serviced_query_UDP_EDNS;
		}
		sq->retry++;
		if(!infra_rtt_update(outnet->infra, &sq->addr, sq->addrlen,
			sq->zone, sq->zonelen, sq->qtype, -1, sq->last_rtt,
			(time_t)now.tv_sec))
			log_err("out of memory in UDP exponential backoff");
		if(sq->retry < OUTBOUND_UDP_RETRY) {
			log_name_addr(VERB_ALGO, "retry query", sq->qbuf+10,
				&sq->addr, sq->addrlen);
			if(!serviced_udp_send(sq, c->buffer)) {
				serviced_callbacks(sq, NETEVENT_CLOSED, c, rep);
			}
			return 0;
		}
	}
	if(error != NETEVENT_NOERROR) {
		/* udp returns error (due to no ID or interface available) */
		serviced_callbacks(sq, error, c, rep);
		return 0;
	}
#ifdef USE_DNSTAP
	/*
	 * sending src (local service)/dst (upstream) addresses over DNSTAP
	 */
	if(error == NETEVENT_NOERROR && outnet->dtenv && p->pc &&
		(outnet->dtenv->log_resolver_response_messages ||
		outnet->dtenv->log_forwarder_response_messages)) {
		log_addr(VERB_ALGO, "response from upstream", &sq->addr, sq->addrlen);
		log_addr(VERB_ALGO, "to local addr", &p->pc->pif->addr,
			p->pc->pif->addrlen);
		dt_msg_send_outside_response(outnet->dtenv, &sq->addr,
			&p->pc->pif->addr, c->type, c->ssl, sq->zone, sq->zonelen,
			sq->qbuf, sq->qbuflen, &sq->last_sent_time,
			sq->outnet->now_tv, c->buffer);
	}
#endif
	if( (sq->status == serviced_query_UDP_EDNS 
		||sq->status == serviced_query_UDP_EDNS_FRAG)
		&& (LDNS_RCODE_WIRE(sldns_buffer_begin(c->buffer)) 
			== LDNS_RCODE_FORMERR || LDNS_RCODE_WIRE(
			sldns_buffer_begin(c->buffer)) == LDNS_RCODE_NOTIMPL
		    || packet_edns_malformed(c->buffer, sq->qtype)
			)) {
		/* try to get an answer by falling back without EDNS */
		verbose(VERB_ALGO, "serviced query: attempt without EDNS");
		sq->status = serviced_query_UDP_EDNS_fallback;
		sq->retry = 0;
		if(!serviced_udp_send(sq, c->buffer)) {
			serviced_callbacks(sq, NETEVENT_CLOSED, c, rep);
		}
		return 0;
	} else if(sq->status == serviced_query_UDP_EDNS && 
		!sq->edns_lame_known) {
		/* now we know that edns queries received answers store that */
		log_addr(VERB_ALGO, "serviced query: EDNS works for",
			&sq->addr, sq->addrlen);
		if(!infra_edns_update(outnet->infra, &sq->addr, sq->addrlen, 
			sq->zone, sq->zonelen, 0, (time_t)now.tv_sec)) {
			log_err("Out of memory caching edns works");
		}
		sq->edns_lame_known = 1;
	} else if(sq->status == serviced_query_UDP_EDNS_fallback &&
		!sq->edns_lame_known && (LDNS_RCODE_WIRE(
		sldns_buffer_begin(c->buffer)) == LDNS_RCODE_NOERROR || 
		LDNS_RCODE_WIRE(sldns_buffer_begin(c->buffer)) == 
		LDNS_RCODE_NXDOMAIN || LDNS_RCODE_WIRE(sldns_buffer_begin(
		c->buffer)) == LDNS_RCODE_YXDOMAIN)) {
		/* the fallback produced a result that looks promising, note
		 * that this server should be approached without EDNS */
		/* only store noEDNS in cache if domain is noDNSSEC */
		if(!sq->want_dnssec) {
		  log_addr(VERB_ALGO, "serviced query: EDNS fails for",
			&sq->addr, sq->addrlen);
		  if(!infra_edns_update(outnet->infra, &sq->addr, sq->addrlen,
			sq->zone, sq->zonelen, -1, (time_t)now.tv_sec)) {
			log_err("Out of memory caching no edns for host");
		  }
		} else {
		  log_addr(VERB_ALGO, "serviced query: EDNS fails, but "
			"not stored because need DNSSEC for", &sq->addr,
			sq->addrlen);
		}
		sq->status = serviced_query_UDP;
	}
	if(now.tv_sec > sq->last_sent_time.tv_sec ||
		(now.tv_sec == sq->last_sent_time.tv_sec &&
		now.tv_usec > sq->last_sent_time.tv_usec)) {
		/* convert from microseconds to milliseconds */
		int roundtime = ((int)(now.tv_sec - sq->last_sent_time.tv_sec))*1000
		  + ((int)now.tv_usec - (int)sq->last_sent_time.tv_usec)/1000;
		verbose(VERB_ALGO, "measured roundtrip at %d msec", roundtime);
		log_assert(roundtime >= 0);
		/* in case the system hibernated, do not enter a huge value,
		 * above this value gives trouble with server selection */
		if(roundtime < 60000) {
		    if(!infra_rtt_update(outnet->infra, &sq->addr, sq->addrlen, 
			sq->zone, sq->zonelen, sq->qtype, roundtime,
			sq->last_rtt, (time_t)now.tv_sec))
			log_err("out of memory noting rtt.");
		}
	}
	/* perform TC flag check and TCP fallback after updating our
	 * cache entries for EDNS status and RTT times */
	if(LDNS_TC_WIRE(sldns_buffer_begin(c->buffer))) {
		/* fallback to TCP */
		/* this discards partial UDP contents */
		if(sq->status == serviced_query_UDP_EDNS ||
			sq->status == serviced_query_UDP_EDNS_FRAG ||
			sq->status == serviced_query_UDP_EDNS_fallback)
			/* if we have unfinished EDNS_fallback, start again */
			sq->status = serviced_query_TCP_EDNS;
		else	sq->status = serviced_query_TCP;
		serviced_tcp_initiate(sq, c->buffer);
		return 0;
	}
	/* yay! an answer */
	serviced_callbacks(sq, error, c, rep);
	return 0;
}

struct serviced_query* 
outnet_serviced_query(struct outside_network* outnet,
	struct query_info* qinfo, uint16_t flags, int dnssec, int want_dnssec,
	int nocaps, int check_ratelimit, int tcp_upstream, int ssl_upstream,
	char* tls_auth_name, struct sockaddr_storage* addr, socklen_t addrlen,
	uint8_t* zone, size_t zonelen, struct module_qstate* qstate,
	comm_point_callback_type* callback, void* callback_arg,
	sldns_buffer* buff, struct module_env* env, int* was_ratelimited)
{
	struct serviced_query* sq;
	struct service_callback* cb;
	struct edns_string_addr* client_string_addr;
	struct regional* region;
	struct edns_option* backed_up_opt_list = qstate->edns_opts_back_out;
	struct edns_option* per_upstream_opt_list = NULL;
	time_t timenow = 0;

	/* If we have an already populated EDNS option list make a copy since
	 * we may now add upstream specific EDNS options. */
	/* Use a region that could be attached to a serviced_query, if it needs
	 * to be created. If an existing one is found then this region will be
	 * destroyed here. */
	region = alloc_reg_obtain(env->alloc);
	if(!region) return NULL;
	if(qstate->edns_opts_back_out) {
		per_upstream_opt_list = edns_opt_copy_region(
			qstate->edns_opts_back_out, region);
		if(!per_upstream_opt_list) {
			alloc_reg_release(env->alloc, region);
			return NULL;
		}
		qstate->edns_opts_back_out = per_upstream_opt_list;
	}

	if(!inplace_cb_query_call(env, qinfo, flags, addr, addrlen, zone,
		zonelen, qstate, region)) {
		alloc_reg_release(env->alloc, region);
		return NULL;
	}
	/* Restore the option list; we can explicitly use the copied one from
	 * now on. */
	per_upstream_opt_list = qstate->edns_opts_back_out;
	qstate->edns_opts_back_out = backed_up_opt_list;

	if((client_string_addr = edns_string_addr_lookup(
		&env->edns_strings->client_strings, addr, addrlen))) {
		edns_opt_list_append(&per_upstream_opt_list,
			env->edns_strings->client_string_opcode,
			client_string_addr->string_len,
			client_string_addr->string, region);
	}

	serviced_gen_query(buff, qinfo->qname, qinfo->qname_len, qinfo->qtype,
		qinfo->qclass, flags);
	sq = lookup_serviced(outnet, buff, dnssec, addr, addrlen,
		per_upstream_opt_list);
	if(!sq) {
		/* Check ratelimit only for new serviced_query */
		if(check_ratelimit) {
			timenow = *env->now;
			if(!infra_ratelimit_inc(env->infra_cache, zone,
				zonelen, timenow, env->cfg->ratelimit_backoff,
				&qstate->qinfo,
				qstate->mesh_info->reply_list
					?&qstate->mesh_info->reply_list->query_reply
					:NULL)) {
				/* Can we pass through with slip factor? */
				if(env->cfg->ratelimit_factor == 0 ||
					ub_random_max(env->rnd,
					env->cfg->ratelimit_factor) != 1) {
					*was_ratelimited = 1;
					alloc_reg_release(env->alloc, region);
					return NULL;
				}
				log_nametypeclass(VERB_ALGO,
					"ratelimit allowed through for "
					"delegation point", zone,
					LDNS_RR_TYPE_NS, LDNS_RR_CLASS_IN);
			}
		}
		/* make new serviced query entry */
		sq = serviced_create(outnet, buff, dnssec, want_dnssec, nocaps,
			tcp_upstream, ssl_upstream, tls_auth_name, addr,
			addrlen, zone, zonelen, (int)qinfo->qtype,
			per_upstream_opt_list,
			( ssl_upstream && env->cfg->pad_queries
			? env->cfg->pad_queries_block_size : 0 ),
			env->alloc, region);
		if(!sq) {
			if(check_ratelimit) {
				infra_ratelimit_dec(env->infra_cache,
					zone, zonelen, timenow);
			}
			return NULL;
		}
		if(!(cb = (struct service_callback*)regional_alloc(
			sq->region, sizeof(*cb)))) {
			if(check_ratelimit) {
				infra_ratelimit_dec(env->infra_cache,
					zone, zonelen, timenow);
			}
			(void)rbtree_delete(outnet->serviced, sq);
			serviced_node_del(&sq->node, NULL);
			return NULL;
		}
		/* No network action at this point; it will be invoked with the
		 * serviced_query timer instead to run outside of the mesh. */
	} else {
		/* We don't need this region anymore. */
		alloc_reg_release(env->alloc, region);
		/* duplicate entries are included in the callback list, because
		 * there is a counterpart registration by our caller that needs
		 * to be doubly-removed (with callbacks perhaps). */
		if(!(cb = (struct service_callback*)regional_alloc(
			sq->region, sizeof(*cb)))) {
			return NULL;
		}
	}
	/* add callback to list of callbacks */
	cb->cb = callback;
	cb->cb_arg = callback_arg;
	cb->next = sq->cblist;
	sq->cblist = cb;
	return sq;
}

/** remove callback from list */
static void
callback_list_remove(struct serviced_query* sq, void* cb_arg)
{
	struct service_callback** pp = &sq->cblist;
	while(*pp) {
		if((*pp)->cb_arg == cb_arg) {
			struct service_callback* del = *pp;
			*pp = del->next;
			return;
		}
		pp = &(*pp)->next;
	}
}

void outnet_serviced_query_stop(struct serviced_query* sq, void* cb_arg)
{
	if(!sq)
		return;
	callback_list_remove(sq, cb_arg);
	/* if callbacks() routine scheduled deletion, let it do that */
	if(!sq->cblist && !sq->busy && !sq->to_be_deleted) {
		(void)rbtree_delete(sq->outnet->serviced, sq);
		serviced_delete(sq);
	}
}

/** create fd to send to this destination */
static int
fd_for_dest(struct outside_network* outnet, struct sockaddr_storage* to_addr,
	socklen_t to_addrlen)
{
	struct sockaddr_storage* addr;
	socklen_t addrlen;
	int i, try, pnum, dscp;
	struct port_if* pif;

	/* create fd */
	dscp = outnet->ip_dscp;
	for(try = 0; try<1000; try++) {
		int port = 0;
		int freebind = 0;
		int noproto = 0;
		int inuse = 0;
		int fd = -1;

		/* select interface */
		if(addr_is_ip6(to_addr, to_addrlen)) {
			if(outnet->num_ip6 == 0) {
				char to[64];
				addr_to_str(to_addr, to_addrlen, to, sizeof(to));
				verbose(VERB_QUERY, "need ipv6 to send, but no ipv6 outgoing interfaces, for %s", to);
				return -1;
			}
			i = ub_random_max(outnet->rnd, outnet->num_ip6);
			pif = &outnet->ip6_ifs[i];
		} else {
			if(outnet->num_ip4 == 0) {
				char to[64];
				addr_to_str(to_addr, to_addrlen, to, sizeof(to));
				verbose(VERB_QUERY, "need ipv4 to send, but no ipv4 outgoing interfaces, for %s", to);
				return -1;
			}
			i = ub_random_max(outnet->rnd, outnet->num_ip4);
			pif = &outnet->ip4_ifs[i];
		}
		addr = &pif->addr;
		addrlen = pif->addrlen;
#ifndef DISABLE_EXPLICIT_PORT_RANDOMISATION
		pnum = ub_random_max(outnet->rnd, pif->avail_total);
		if(pnum < pif->inuse) {
			/* port already open */
			port = pif->out[pnum]->number;
		} else {
			/* unused ports in start part of array */
			port = pif->avail_ports[pnum - pif->inuse];
		}
#else
		pnum = port = 0;
#endif
		if(addr_is_ip6(to_addr, to_addrlen)) {
			struct sockaddr_in6 sa = *(struct sockaddr_in6*)addr;
			sa.sin6_port = (in_port_t)htons((uint16_t)port);
			fd = create_udp_sock(AF_INET6, SOCK_DGRAM,
				(struct sockaddr*)&sa, addrlen, 1, &inuse, &noproto,
				0, 0, 0, NULL, 0, freebind, 0, dscp);
		} else {
			struct sockaddr_in* sa = (struct sockaddr_in*)addr;
			sa->sin_port = (in_port_t)htons((uint16_t)port);
			fd = create_udp_sock(AF_INET, SOCK_DGRAM, 
				(struct sockaddr*)addr, addrlen, 1, &inuse, &noproto,
				0, 0, 0, NULL, 0, freebind, 0, dscp);
		}
		if(fd != -1) {
			return fd;
		}
		if(!inuse) {
			return -1;
		}
	}
	/* too many tries */
	log_err("cannot send probe, ports are in use");
	return -1;
}

struct comm_point*
outnet_comm_point_for_udp(struct outside_network* outnet,
	comm_point_callback_type* cb, void* cb_arg,
	struct sockaddr_storage* to_addr, socklen_t to_addrlen)
{
	struct comm_point* cp;
	int fd = fd_for_dest(outnet, to_addr, to_addrlen);
	if(fd == -1) {
		return NULL;
	}
	cp = comm_point_create_udp(outnet->base, fd, outnet->udp_buff, 0,
		cb, cb_arg, NULL);
	if(!cp) {
		log_err("malloc failure");
		close(fd);
		return NULL;
	}
	return cp;
}

/** setup SSL for comm point */
static int
setup_comm_ssl(struct comm_point* cp, struct outside_network* outnet,
	int fd, char* host)
{
	cp->ssl = outgoing_ssl_fd(outnet->sslctx, fd);
	if(!cp->ssl) {
		log_err("cannot create SSL object");
		return 0;
	}
#ifdef USE_WINSOCK
	comm_point_tcp_win_bio_cb(cp, cp->ssl);
#endif
	cp->ssl_shake_state = comm_ssl_shake_write;
	/* https verification */
#ifdef HAVE_SSL
	if(outnet->tls_use_sni) {
		(void)SSL_set_tlsext_host_name(cp->ssl, host);
	}
#endif
#ifdef HAVE_SSL_SET1_HOST
	if((SSL_CTX_get_verify_mode(outnet->sslctx)&SSL_VERIFY_PEER)) {
		/* because we set SSL_VERIFY_PEER, in netevent in
		 * ssl_handshake, it'll check if the certificate
		 * verification has succeeded */
		/* SSL_VERIFY_PEER is set on the sslctx */
		/* and the certificates to verify with are loaded into
		 * it with SSL_load_verify_locations or
		 * SSL_CTX_set_default_verify_paths */
		/* setting the hostname makes openssl verify the
		 * host name in the x509 certificate in the
		 * SSL connection*/
		if(!SSL_set1_host(cp->ssl, host)) {
			log_err("SSL_set1_host failed");
			return 0;
		}
	}
#elif defined(HAVE_X509_VERIFY_PARAM_SET1_HOST)
	/* openssl 1.0.2 has this function that can be used for
	 * set1_host like verification */
	if((SSL_CTX_get_verify_mode(outnet->sslctx)&SSL_VERIFY_PEER)) {
		X509_VERIFY_PARAM* param = SSL_get0_param(cp->ssl);
#  ifdef X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS
		X509_VERIFY_PARAM_set_hostflags(param, X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);
#  endif
		if(!X509_VERIFY_PARAM_set1_host(param, host, strlen(host))) {
			log_err("X509_VERIFY_PARAM_set1_host failed");
			return 0;
		}
	}
#else
	(void)host;
#endif /* HAVE_SSL_SET1_HOST */
	return 1;
}

struct comm_point*
outnet_comm_point_for_tcp(struct outside_network* outnet,
	comm_point_callback_type* cb, void* cb_arg,
	struct sockaddr_storage* to_addr, socklen_t to_addrlen,
	sldns_buffer* query, int timeout, int ssl, char* host)
{
	struct comm_point* cp;
	int fd = outnet_get_tcp_fd(to_addr, to_addrlen, outnet->tcp_mss,
		outnet->ip_dscp, ssl);
	if(fd == -1) {
		return 0;
	}
	fd_set_nonblock(fd);
	if(!outnet_tcp_connect(fd, to_addr, to_addrlen)) {
		/* outnet_tcp_connect has closed fd on error for us */
		return 0;
	}
	cp = comm_point_create_tcp_out(outnet->base, 65552, cb, cb_arg);
	if(!cp) {
		log_err("malloc failure");
		close(fd);
		return 0;
	}
	cp->repinfo.remote_addrlen = to_addrlen;
	memcpy(&cp->repinfo.remote_addr, to_addr, to_addrlen);

	/* setup for SSL (if needed) */
	if(ssl) {
		if(!setup_comm_ssl(cp, outnet, fd, host)) {
			log_err("cannot setup XoT");
			comm_point_delete(cp);
			return NULL;
		}
	}

	/* set timeout on TCP connection */
	comm_point_start_listening(cp, fd, timeout);
	/* copy scratch buffer to cp->buffer */
	sldns_buffer_copy(cp->buffer, query);
	return cp;
}

/** setup the User-Agent HTTP header based on http-user-agent configuration */
static void
setup_http_user_agent(sldns_buffer* buf, struct config_file* cfg)
{
	if(cfg->hide_http_user_agent) return;
	if(cfg->http_user_agent==NULL || cfg->http_user_agent[0] == 0) {
		sldns_buffer_printf(buf, "User-Agent: %s/%s\r\n", PACKAGE_NAME,
			PACKAGE_VERSION);
	} else {
		sldns_buffer_printf(buf, "User-Agent: %s\r\n", cfg->http_user_agent);
	}
}

/** setup http request headers in buffer for sending query to destination */
static int
setup_http_request(sldns_buffer* buf, char* host, char* path,
	struct config_file* cfg)
{
	sldns_buffer_clear(buf);
	sldns_buffer_printf(buf, "GET /%s HTTP/1.1\r\n", path);
	sldns_buffer_printf(buf, "Host: %s\r\n", host);
	setup_http_user_agent(buf, cfg);
	/* We do not really do multiple queries per connection,
	 * but this header setting is also not needed.
	 * sldns_buffer_printf(buf, "Connection: close\r\n") */
	sldns_buffer_printf(buf, "\r\n");
	if(sldns_buffer_position(buf)+10 > sldns_buffer_capacity(buf))
		return 0; /* somehow buffer too short, but it is about 60K
		and the request is only a couple bytes long. */
	sldns_buffer_flip(buf);
	return 1;
}

struct comm_point*
outnet_comm_point_for_http(struct outside_network* outnet,
	comm_point_callback_type* cb, void* cb_arg,
	struct sockaddr_storage* to_addr, socklen_t to_addrlen, int timeout,
	int ssl, char* host, char* path, struct config_file* cfg)
{
	/* cp calls cb with err=NETEVENT_DONE when transfer is done */
	struct comm_point* cp;
	int fd = outnet_get_tcp_fd(to_addr, to_addrlen, outnet->tcp_mss,
		outnet->ip_dscp, ssl);
	if(fd == -1) {
		return 0;
	}
	fd_set_nonblock(fd);
	if(!outnet_tcp_connect(fd, to_addr, to_addrlen)) {
		/* outnet_tcp_connect has closed fd on error for us */
		return 0;
	}
	cp = comm_point_create_http_out(outnet->base, 65552, cb, cb_arg,
		outnet->udp_buff);
	if(!cp) {
		log_err("malloc failure");
		close(fd);
		return 0;
	}
	cp->repinfo.remote_addrlen = to_addrlen;
	memcpy(&cp->repinfo.remote_addr, to_addr, to_addrlen);

	/* setup for SSL (if needed) */
	if(ssl) {
		if(!setup_comm_ssl(cp, outnet, fd, host)) {
			log_err("cannot setup https");
			comm_point_delete(cp);
			return NULL;
		}
	}

	/* set timeout on TCP connection */
	comm_point_start_listening(cp, fd, timeout);

	/* setup http request in cp->buffer */
	if(!setup_http_request(cp->buffer, host, path, cfg)) {
		log_err("error setting up http request");
		comm_point_delete(cp);
		return NULL;
	}
	return cp;
}

/** get memory used by waiting tcp entry (in use or not) */
static size_t
waiting_tcp_get_mem(struct waiting_tcp* w)
{
	size_t s;
	if(!w) return 0;
	s = sizeof(*w) + w->pkt_len;
	if(w->timer)
		s += comm_timer_get_mem(w->timer);
	return s;
}

/** get memory used by port if */
static size_t
if_get_mem(struct port_if* pif)
{
	size_t s;
	int i;
	s = sizeof(*pif) +
#ifndef DISABLE_EXPLICIT_PORT_RANDOMISATION
	    sizeof(int)*pif->avail_total +
#endif
		sizeof(struct port_comm*)*pif->maxout;
	for(i=0; i<pif->inuse; i++)
		s += sizeof(*pif->out[i]) + 
			comm_point_get_mem(pif->out[i]->cp);
	return s;
}

/** get memory used by waiting udp */
static size_t
waiting_udp_get_mem(struct pending* w)
{
	size_t s;
	s = sizeof(*w) + comm_timer_get_mem(w->timer) + w->pkt_len;
	return s;
}

size_t outnet_get_mem(struct outside_network* outnet)
{
	size_t i;
	int k;
	struct waiting_tcp* w;
	struct pending* u;
	struct serviced_query* sq;
	struct service_callback* sb;
	struct port_comm* pc;
	size_t s = sizeof(*outnet) + sizeof(*outnet->base) + 
		sizeof(*outnet->udp_buff) + 
		sldns_buffer_capacity(outnet->udp_buff);
	/* second buffer is not ours */
	for(pc = outnet->unused_fds; pc; pc = pc->next) {
		s += sizeof(*pc) + comm_point_get_mem(pc->cp);
	}
	for(k=0; k<outnet->num_ip4; k++)
		s += if_get_mem(&outnet->ip4_ifs[k]);
	for(k=0; k<outnet->num_ip6; k++)
		s += if_get_mem(&outnet->ip6_ifs[k]);
	for(u=outnet->udp_wait_first; u; u=u->next_waiting)
		s += waiting_udp_get_mem(u);
	
	s += sizeof(struct pending_tcp*)*outnet->num_tcp;
	for(i=0; i<outnet->num_tcp; i++) {
		s += sizeof(struct pending_tcp);
		s += comm_point_get_mem(outnet->tcp_conns[i]->c);
		if(outnet->tcp_conns[i]->query)
			s += waiting_tcp_get_mem(outnet->tcp_conns[i]->query);
	}
	for(w=outnet->tcp_wait_first; w; w = w->next_waiting)
		s += waiting_tcp_get_mem(w);
	s += sizeof(*outnet->pending);
	s += (sizeof(struct pending) + comm_timer_get_mem(NULL)) * 
		outnet->pending->count;
	s += sizeof(*outnet->serviced);
	s += outnet->svcd_overhead;
	RBTREE_FOR(sq, struct serviced_query*, outnet->serviced) {
		s += sizeof(*sq) + sq->qbuflen;
		for(sb = sq->cblist; sb; sb = sb->next)
			s += sizeof(*sb);
	}
	return s;
}

size_t 
serviced_get_mem(struct serviced_query* sq)
{
	struct service_callback* sb;
	size_t s;
	s = sizeof(*sq) + sq->qbuflen;
	for(sb = sq->cblist; sb; sb = sb->next)
		s += sizeof(*sb);
	if(sq->status == serviced_query_UDP_EDNS ||
		sq->status == serviced_query_UDP ||
		sq->status == serviced_query_UDP_EDNS_FRAG ||
		sq->status == serviced_query_UDP_EDNS_fallback) {
		s += sizeof(struct pending);
		s += comm_timer_get_mem(NULL);
	} else {
		/* does not have size of the pkt pointer */
		/* always has a timer except on malloc failures */

		/* these sizes are part of the main outside network mem */
		/*
		s += sizeof(struct waiting_tcp);
		s += comm_timer_get_mem(NULL);
		*/
	}
	return s;
}

