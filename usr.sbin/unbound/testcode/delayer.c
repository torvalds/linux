/*
 * testcode/delayer.c - debug program that delays queries to a server.
 *
 * Copyright (c) 2008, NLnet Labs. All rights reserved.
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
 * This program delays queries made. It performs as a proxy to another
 * server and delays queries to it.
 */

#include "config.h"
#ifdef HAVE_GETOPT_H
#include <getopt.h>
#endif
#ifdef HAVE_TIME_H
#include <time.h>
#endif
#include <sys/time.h>
#include "util/net_help.h"
#include "util/config_file.h"
#include "sldns/sbuffer.h"
#include <signal.h>

/** number of reads per select for delayer */
#define TRIES_PER_SELECT 100

/**
 * The ring buffer
 */
struct ringbuf {
	/** base of buffer */
	uint8_t* buf;
	/** size of buffer */
	size_t size;
	/** low mark, items start here */
	size_t low;
	/** high mark, items end here */
	size_t high;
};

/**
 * List of proxy fds that return replies from the server to our clients.
 */
struct proxy {
	/** the fd to listen for replies from server */
	int s;
	/** last time this was used */
	struct timeval lastuse;
	/** remote address */
	struct sockaddr_storage addr;
	/** length of addr */
	socklen_t addr_len;
	/** number of queries waiting (in total) */
	size_t numwait;
	/** number of queries sent to server (in total) */
	size_t numsent;
	/** numberof answers returned to client (in total) */
	size_t numreturn;
	/** how many times repurposed */
	size_t numreuse;
	/** next in proxylist */
	struct proxy* next;
};

/**
 * An item that has to be TCP relayed
 */
struct tcp_send_list {
	/** the data item */
	uint8_t* item;
	/** size of item */
	size_t len;
	/** time when the item can be transmitted on */
	struct timeval wait;
	/** how much of the item has already been transmitted */
	size_t done;
	/** next in list */
	struct tcp_send_list* next;
};

/**
 * List of TCP proxy fd pairs to TCP connect client to server 
 */
struct tcp_proxy {
	/** the fd to listen for client query */
	int client_s;
	/** the fd to listen for server answer */
	int server_s;

	/** remote client address */
	struct sockaddr_storage addr;
	/** length of address */
	socklen_t addr_len;
	/** timeout on this entry */
	struct timeval timeout;

	/** list of query items to send to server */
	struct tcp_send_list* querylist;
	/** last in query list */
	struct tcp_send_list* querylast;
	/** list of answer items to send to client */
	struct tcp_send_list* answerlist;
	/** last in answerlist */
	struct tcp_send_list* answerlast;

	/** next in list */
	struct tcp_proxy* next;
};

/** usage information for delayer */
static void usage(char* argv[])
{
	printf("usage: %s [options]\n", argv[0]);
	printf("	-f addr : use addr, forward to that server, @port.\n");
	printf("	-b addr : bind to this address to listen.\n");
	printf("	-p port : bind to this port (use 0 for random).\n");
	printf("	-m mem	: use this much memory for waiting queries.\n");
	printf("	-d delay: UDP queries are delayed n milliseconds.\n");
	printf("		  TCP is delayed twice (on send, on recv).\n");
	printf("	-h 	: this help message\n");
	exit(1);
}

/** timeval compare, t1 < t2 */
static int
dl_tv_smaller(struct timeval* t1, const struct timeval* t2) 
{
#ifndef S_SPLINT_S
	if(t1->tv_sec < t2->tv_sec)
		return 1;
	if(t1->tv_sec == t2->tv_sec &&
		t1->tv_usec < t2->tv_usec)
		return 1;
#endif
	return 0;
}

/** timeval add, t1 += t2 */
static void
dl_tv_add(struct timeval* t1, const struct timeval* t2) 
{
#ifndef S_SPLINT_S
	t1->tv_sec += t2->tv_sec;
	t1->tv_usec += t2->tv_usec;
	while(t1->tv_usec >= 1000000) {
		t1->tv_usec -= 1000000;
		t1->tv_sec++;
	}
#endif
}

/** timeval subtract, t1 -= t2 */
static void
dl_tv_subtract(struct timeval* t1, const struct timeval* t2) 
{
#ifndef S_SPLINT_S
	t1->tv_sec -= t2->tv_sec;
	if(t1->tv_usec >= t2->tv_usec) {
		t1->tv_usec -= t2->tv_usec;
	} else {
		t1->tv_sec--;
		t1->tv_usec = 1000000-(t2->tv_usec-t1->tv_usec);
	}
#endif
}


/** create new ring buffer */
static struct ringbuf*
ring_create(size_t sz)
{
	struct ringbuf* r = (struct ringbuf*)calloc(1, sizeof(*r));
	if(!r) fatal_exit("out of memory");
	r->buf = (uint8_t*)malloc(sz);
	if(!r->buf) fatal_exit("out of memory");
	r->size = sz;
	r->low = 0;
	r->high = 0;
	return r;
}

/** delete ring buffer */
static void
ring_delete(struct ringbuf* r)
{
	if(!r) return;
	free(r->buf);
	free(r);
}

/** add entry to ringbuffer */
static void
ring_add(struct ringbuf* r, sldns_buffer* pkt, struct timeval* now, 
	struct timeval* delay, struct proxy* p)
{
	/* time -- proxy* -- 16bitlen -- message */
	uint16_t len = (uint16_t)sldns_buffer_limit(pkt);
	struct timeval when;
	size_t needed;
	uint8_t* where = NULL;
	log_assert(sldns_buffer_limit(pkt) <= 65535);
	needed = sizeof(when) + sizeof(p) + sizeof(len) + len;
	/* put item into ringbuffer */
	if(r->low < r->high) {
		/* used part is in the middle */
		if(r->size - r->high >= needed) {
			where = r->buf + r->high;
			r->high += needed;
		} else if(r->low > needed) {
			/* wrap around ringbuffer */
			/* make sure r->low == r->high means empty */
			/* so r->low == r->high cannot be used to signify
			 * a completely full ringbuf */
			if(r->size - r->high > sizeof(when)+sizeof(p)) {
				/* zero entry at end of buffer */
				memset(r->buf+r->high, 0, 
					sizeof(when)+sizeof(p));
			}
			where = r->buf;
			r->high = needed;
		} else {
			/* drop message */
			log_warn("warning: mem full, dropped message");
			return;
		}
	} else {
		/* empty */
		if(r->high == r->low) {
			where = r->buf;
			r->low = 0;
			r->high = needed;
		/* unused part is in the middle */
		/* so ringbuffer has wrapped around */
		} else if(r->low - r->high > needed) {
			where = r->buf + r->high;
			r->high += needed;
		} else {
			log_warn("warning: mem full, dropped message");
			return;
		}
	}
	when = *now;
	dl_tv_add(&when, delay);
	/* copy it at where part */
	log_assert(where != NULL);
	memmove(where, &when, sizeof(when));
	memmove(where+sizeof(when), &p, sizeof(p));
	memmove(where+sizeof(when)+sizeof(p), &len, sizeof(len));
	memmove(where+sizeof(when)+sizeof(p)+sizeof(len), 
		sldns_buffer_begin(pkt), len);
}

/** see if the ringbuffer is empty */
static int
ring_empty(struct ringbuf* r)
{
	return (r->low == r->high);
}

/** peek at timevalue for next item in ring */
static struct timeval*
ring_peek_time(struct ringbuf* r)
{
	if(ring_empty(r))
		return NULL;
	return (struct timeval*)&r->buf[r->low];
}

/** get entry from ringbuffer */
static int
ring_pop(struct ringbuf* r, sldns_buffer* pkt, struct timeval* tv, 
	struct proxy** p)
{
	/* time -- proxy* -- 16bitlen -- message */
	uint16_t len;
	uint8_t* where = NULL;
	size_t done;
	if(r->low == r->high)
		return 0;
	where = r->buf + r->low;
	memmove(tv, where, sizeof(*tv));
	memmove(p, where+sizeof(*tv), sizeof(*p));
	memmove(&len, where+sizeof(*tv)+sizeof(*p), sizeof(len));
	memmove(sldns_buffer_begin(pkt), 
		where+sizeof(*tv)+sizeof(*p)+sizeof(len), len);
	sldns_buffer_set_limit(pkt, (size_t)len);
	done = sizeof(*tv)+sizeof(*p)+sizeof(len)+len;
	/* move lowmark */
	if(r->low < r->high) {
		/* used part in middle */
		log_assert(r->high - r->low >= done);
		r->low += done;
	} else {
		/* unused part in middle */
		log_assert(r->size - r->low >= done);
		r->low += done;
		if(r->size - r->low > sizeof(*tv)+sizeof(*p)) {
			/* see if it is zeroed; means end of buffer */
			struct proxy* pz;
			memmove(&pz, r->buf+r->low+sizeof(*tv), sizeof(pz));
			if(pz == NULL)
				r->low = 0;
		} else r->low = 0;
	}
	if(r->low == r->high) {
		r->low = 0; /* reset if empty */
		r->high = 0;
	}
	return 1;
}
	
/** signal handler global info */
static volatile int do_quit = 0;

/** signal handler for user quit */
static RETSIGTYPE delayer_sigh(int sig)
{
	char str[] = "exit on signal   \n";
	str[15] = '0' + (sig/10)%10;
	str[16] = '0' + sig%10;
	/* simple cast to void will not silence Wunused-result */
	(void)!write(STDOUT_FILENO, str, strlen(str));
	do_quit = 1;
}

/** send out waiting packets */
static void
service_send(struct ringbuf* ring, struct timeval* now, sldns_buffer* pkt,
	struct sockaddr_storage* srv_addr, socklen_t srv_len)
{
	struct proxy* p;
	struct timeval tv;
	ssize_t sent;
	while(!ring_empty(ring) && 
		dl_tv_smaller(ring_peek_time(ring), now)) {
		/* this items needs to be sent out */
		if(!ring_pop(ring, pkt, &tv, &p))
			fatal_exit("ringbuf error: pop failed");
		verbose(1, "send out query %d.%6.6d", 
			(unsigned)tv.tv_sec, (unsigned)tv.tv_usec);
		log_addr(1, "from client", &p->addr, p->addr_len);
		/* send it */
		sent = sendto(p->s, (void*)sldns_buffer_begin(pkt), 
			sldns_buffer_limit(pkt), 0, 
			(struct sockaddr*)srv_addr, srv_len);
		if(sent == -1) {
			log_err("sendto: %s", sock_strerror(errno));
		} else if(sent != (ssize_t)sldns_buffer_limit(pkt)) {
			log_err("sendto: partial send");
		}
		p->lastuse = *now;
		p->numsent++;
	}
}

/** do proxy for one readable client */
static void
do_proxy(struct proxy* p, int retsock, sldns_buffer* pkt)
{
	int i;
	ssize_t r;
	for(i=0; i<TRIES_PER_SELECT; i++) {
		r = recv(p->s, (void*)sldns_buffer_begin(pkt), 
			sldns_buffer_capacity(pkt), 0);
		if(r == -1) {
#ifndef USE_WINSOCK
			if(errno == EAGAIN || errno == EINTR)
				return;
#else
			if(WSAGetLastError() == WSAEINPROGRESS ||
				WSAGetLastError() == WSAEWOULDBLOCK)
				return;
#endif
			log_err("recv: %s", sock_strerror(errno));
			return;
		}
		sldns_buffer_set_limit(pkt, (size_t)r);
		log_addr(1, "return reply to client", &p->addr, p->addr_len);
		/* send reply back to the real client */
		p->numreturn++;
		r = sendto(retsock, (void*)sldns_buffer_begin(pkt), (size_t)r, 
			0, (struct sockaddr*)&p->addr, p->addr_len);
		if(r == -1) {
			log_err("sendto: %s", sock_strerror(errno));
		}
	}
}

/** proxy return replies to clients */
static void
service_proxy(fd_set* rset, int retsock, struct proxy* proxies, 
	sldns_buffer* pkt, struct timeval* now)
{
	struct proxy* p;
	for(p = proxies; p; p = p->next) {
		if(FD_ISSET(p->s, rset)) {
			p->lastuse = *now;
			do_proxy(p, retsock, pkt);
		}
	}
}

/** find or else create proxy for this remote client */
static struct proxy*
find_create_proxy(struct sockaddr_storage* from, socklen_t from_len,
	fd_set* rorig, int* max, struct proxy** proxies, int serv_ip6,
	struct timeval* now, struct timeval* reuse_timeout)
{
	struct proxy* p;
	struct timeval t;
	for(p = *proxies; p; p = p->next) {
		if(sockaddr_cmp(from, from_len, &p->addr, p->addr_len)==0)
			return p;
	}
	/* possibly: reuse lapsed entries */
	for(p = *proxies; p; p = p->next) {
		if(p->numwait > p->numsent || p->numsent > p->numreturn)
			continue;
		t = *now;
		dl_tv_subtract(&t, &p->lastuse);
		if(dl_tv_smaller(&t, reuse_timeout))
			continue;
		/* yes! */
		verbose(1, "reuse existing entry");
		memmove(&p->addr, from, from_len);
		p->addr_len = from_len;
		p->numreuse++;
		return p;
	}
	/* create new */
	p = (struct proxy*)calloc(1, sizeof(*p));
	if(!p) fatal_exit("out of memory");
	p->s = socket(serv_ip6?AF_INET6:AF_INET, SOCK_DGRAM, 0);
	if(p->s == -1) {
		fatal_exit("socket: %s", sock_strerror(errno));
	}
	fd_set_nonblock(p->s);
	memmove(&p->addr, from, from_len);
	p->addr_len = from_len;
	p->next = *proxies;
	*proxies = p;
	FD_SET(FD_SET_T p->s, rorig);
	if(p->s+1 > *max)
		*max = p->s+1;
	return p;
}

/** recv new waiting packets */
static void
service_recv(int s, struct ringbuf* ring, sldns_buffer* pkt, 
	fd_set* rorig, int* max, struct proxy** proxies,
	struct sockaddr_storage* srv_addr, socklen_t srv_len, 
	struct timeval* now, struct timeval* delay, struct timeval* reuse)
{
	int i;
	struct sockaddr_storage from;
	socklen_t from_len;
	ssize_t len;
	struct proxy* p;
	for(i=0; i<TRIES_PER_SELECT; i++) {
		from_len = (socklen_t)sizeof(from);
		len = recvfrom(s, (void*)sldns_buffer_begin(pkt),
			sldns_buffer_capacity(pkt), 0,
			(struct sockaddr*)&from, &from_len);
		if(len < 0) {
#ifndef USE_WINSOCK
			if(errno == EAGAIN || errno == EINTR)
				return;
#else
			if(WSAGetLastError() == WSAEWOULDBLOCK || 
				WSAGetLastError() == WSAEINPROGRESS)
				return;
#endif
			fatal_exit("recvfrom: %s", sock_strerror(errno));
		}
		sldns_buffer_set_limit(pkt, (size_t)len);
		/* find its proxy element */
		p = find_create_proxy(&from, from_len, rorig, max, proxies,
			addr_is_ip6(srv_addr, srv_len), now, reuse);
		if(!p) fatal_exit("error: cannot find or create proxy");
		p->lastuse = *now;
		ring_add(ring, pkt, now, delay, p);
		p->numwait++;
		log_addr(1, "recv from client", &p->addr, p->addr_len);
	}
}

/** delete tcp proxy */
static void
tcp_proxy_delete(struct tcp_proxy* p)
{
	struct tcp_send_list* s, *sn;
	if(!p)
		return;
	log_addr(1, "delete tcp proxy", &p->addr, p->addr_len);
	s = p->querylist;
	while(s) {
		sn = s->next;
		free(s->item);
		free(s);
		s = sn;
	}
	s = p->answerlist;
	while(s) {
		sn = s->next;
		free(s->item);
		free(s);
		s = sn;
	}
	sock_close(p->client_s);
	if(p->server_s != -1)
		sock_close(p->server_s);
	free(p);
}

/** accept new TCP connections, and set them up */
static void
service_tcp_listen(int s, fd_set* rorig, int* max, struct tcp_proxy** proxies,
	struct sockaddr_storage* srv_addr, socklen_t srv_len, 
	struct timeval* now, struct timeval* tcp_timeout)
{
	int newfd;
	struct sockaddr_storage addr;
	struct tcp_proxy* p;
	socklen_t addr_len;
	newfd = accept(s, (struct sockaddr*)&addr, &addr_len);
	if(newfd == -1) {
#ifndef USE_WINSOCK
		if(errno == EAGAIN || errno == EINTR)
			return;
#else
		if(WSAGetLastError() == WSAEWOULDBLOCK || 
			WSAGetLastError() == WSAEINPROGRESS ||
			WSAGetLastError() == WSAECONNRESET)
			return;
#endif
		fatal_exit("accept: %s", sock_strerror(errno));
	}
	p = (struct tcp_proxy*)calloc(1, sizeof(*p));
	if(!p) fatal_exit("out of memory");
	memmove(&p->addr, &addr, addr_len);
	p->addr_len = addr_len;
	log_addr(1, "new tcp proxy", &p->addr, p->addr_len);
	p->client_s = newfd;
	p->server_s = socket(addr_is_ip6(srv_addr, srv_len)?AF_INET6:AF_INET,
		SOCK_STREAM, 0);
	if(p->server_s == -1) {
		fatal_exit("tcp socket: %s", sock_strerror(errno));
	}
	fd_set_nonblock(p->client_s);
	fd_set_nonblock(p->server_s);
	if(connect(p->server_s, (struct sockaddr*)srv_addr, srv_len) == -1) {
#ifndef USE_WINSOCK
		if(errno != EINPROGRESS) {
			log_err("tcp connect: %s", strerror(errno));
#else
		if(WSAGetLastError() != WSAEWOULDBLOCK &&
			WSAGetLastError() != WSAEINPROGRESS) {
			log_err("tcp connect: %s", 
				wsa_strerror(WSAGetLastError()));
#endif
			sock_close(p->server_s);
			sock_close(p->client_s);
			free(p);
			return;
		}
	}
	p->timeout = *now;
	dl_tv_add(&p->timeout, tcp_timeout);

	/* listen to client and server */
	FD_SET(FD_SET_T p->client_s, rorig);
	FD_SET(FD_SET_T p->server_s, rorig);
	if(p->client_s+1 > *max)
		*max = p->client_s+1;
	if(p->server_s+1 > *max)
		*max = p->server_s+1;

	/* add into proxy list */
	p->next = *proxies;
	*proxies = p;
}

/** relay TCP, read a part */
static int
tcp_relay_read(int s, struct tcp_send_list** first, 
	struct tcp_send_list** last, struct timeval* now, 
	struct timeval* delay, sldns_buffer* pkt)
{
	struct tcp_send_list* item;
	ssize_t r = recv(s, (void*)sldns_buffer_begin(pkt), 
		sldns_buffer_capacity(pkt), 0);
	if(r == -1) {
#ifndef USE_WINSOCK
		if(errno == EINTR || errno == EAGAIN)
			return 1;
#else
		if(WSAGetLastError() == WSAEINPROGRESS || 
			WSAGetLastError() == WSAEWOULDBLOCK)
			return 1;
#endif
		log_err("tcp read: %s", sock_strerror(errno));
		return 0;
	} else if(r == 0) {
		/* connection closed */
		return 0;
	}
	item = (struct tcp_send_list*)malloc(sizeof(*item));
	if(!item) {
		log_err("out of memory");
		return 0;
	}
	verbose(1, "read item len %d", (int)r);
	item->len = (size_t)r;
	item->item = memdup(sldns_buffer_begin(pkt), item->len);
	if(!item->item) {
		free(item);
		log_err("out of memory");
		return 0;
	}
	item->done = 0;
	item->wait = *now;
	dl_tv_add(&item->wait, delay);
	item->next = NULL;
	
	/* link in */
	if(*first) {
		(*last)->next = item;
	} else {
		*first = item;
	}
	*last = item;
	return 1;
}

/** relay TCP, write a part */
static int
tcp_relay_write(int s, struct tcp_send_list** first, 
	struct tcp_send_list** last, struct timeval* now)
{
	ssize_t r;
	struct tcp_send_list* p;
	while(*first) {
		p = *first;
		/* is the item ready? */
		if(!dl_tv_smaller(&p->wait, now))
			return 1;
		/* write it */
		r = send(s, (void*)(p->item + p->done), p->len - p->done, 0);
		if(r == -1) {
#ifndef USE_WINSOCK
			if(errno == EAGAIN || errno == EINTR)
				return 1;
#else
			if(WSAGetLastError() == WSAEWOULDBLOCK || 
				WSAGetLastError() == WSAEINPROGRESS)
				return 1;
#endif
			log_err("tcp write: %s", sock_strerror(errno));
			return 0;
		} else if(r == 0) {
			/* closed */
			return 0;
		}
		/* account it */
		p->done += (size_t)r;
		verbose(1, "write item %d of %d", (int)p->done, (int)p->len);
		if(p->done >= p->len) {
			free(p->item);
			*first = p->next;
			if(!*first)
				*last = NULL;
			free(p);
		} else {
			/* partial write */
			return 1;
		}
	}
	return 1;
}

/** perform TCP relaying */
static void
service_tcp_relay(struct tcp_proxy** tcp_proxies, struct timeval* now,
	struct timeval* delay, struct timeval* tcp_timeout, sldns_buffer* pkt,
	fd_set* rset, fd_set* rorig, fd_set* worig)
{
	struct tcp_proxy* p, **prev;
	struct timeval tout;
	int delete_it;
	p = *tcp_proxies;
	prev = tcp_proxies;
	tout = *now;
	dl_tv_add(&tout, tcp_timeout);

	while(p) {
		delete_it = 0;
		/* can we receive further queries? */
		if(!delete_it && FD_ISSET(p->client_s, rset)) {
			p->timeout = tout;
			log_addr(1, "read tcp query", &p->addr, p->addr_len);
			if(!tcp_relay_read(p->client_s, &p->querylist, 
				&p->querylast, now, delay, pkt))
				delete_it = 1;
		}
		/* can we receive further answers? */
		if(!delete_it && p->server_s != -1 &&
			FD_ISSET(p->server_s, rset)) {
			p->timeout = tout;
			log_addr(1, "read tcp answer", &p->addr, p->addr_len);
			if(!tcp_relay_read(p->server_s, &p->answerlist, 
				&p->answerlast, now, delay, pkt)) {
				sock_close(p->server_s);
				FD_CLR(FD_SET_T p->server_s, worig);
				FD_CLR(FD_SET_T p->server_s, rorig);
				p->server_s = -1;
			}
		}
		/* can we send on further queries */
		if(!delete_it && p->querylist && p->server_s != -1) {
			p->timeout = tout;
			if(dl_tv_smaller(&p->querylist->wait, now))
				log_addr(1, "write tcp query", 
					&p->addr, p->addr_len);
			if(!tcp_relay_write(p->server_s, &p->querylist, 
				&p->querylast, now))
				delete_it = 1;
			if(p->querylist &&
				dl_tv_smaller(&p->querylist->wait, now))
				FD_SET(FD_SET_T p->server_s, worig);
			else 	FD_CLR(FD_SET_T p->server_s, worig);
		}

		/* can we send on further answers */
		if(!delete_it && p->answerlist) {
			p->timeout = tout;
			if(dl_tv_smaller(&p->answerlist->wait, now))
				log_addr(1, "write tcp answer", 
					&p->addr, p->addr_len);
			if(!tcp_relay_write(p->client_s, &p->answerlist, 
				&p->answerlast, now))
				delete_it = 1;
			if(p->answerlist && dl_tv_smaller(&p->answerlist->wait,
				now))
				FD_SET(FD_SET_T p->client_s, worig);
			else 	FD_CLR(FD_SET_T p->client_s, worig);
			if(!p->answerlist && p->server_s == -1)
				delete_it = 1;
		}

		/* does this entry timeout? (unused too long) */
		if(dl_tv_smaller(&p->timeout, now)) {
			delete_it = 1;
		}
		if(delete_it) {
			struct tcp_proxy* np = p->next;
			*prev = np;
			FD_CLR(FD_SET_T p->client_s, rorig);
			FD_CLR(FD_SET_T p->client_s, worig);
			if(p->server_s != -1) {
				FD_CLR(FD_SET_T p->server_s, rorig);
				FD_CLR(FD_SET_T p->server_s, worig);
			}
			tcp_proxy_delete(p);
			p = np;
			continue;
		}

		prev = &p->next;
		p = p->next;
	}
}

/** find waiting time */
static int
service_findwait(struct timeval* now, struct timeval* wait, 
	struct ringbuf* ring, struct tcp_proxy* tcplist)
{
	/* first item is the time to wait */
	struct timeval* peek = ring_peek_time(ring);
	struct timeval tcv;
	int have_tcpval = 0;
	struct tcp_proxy* p;

	/* also for TCP list the first in sendlists is the time to wait */
	for(p=tcplist; p; p=p->next) {
		if(!have_tcpval)
			tcv = p->timeout;
		have_tcpval = 1;
		if(dl_tv_smaller(&p->timeout, &tcv))
			tcv = p->timeout;
		if(p->querylist && dl_tv_smaller(&p->querylist->wait, &tcv))
			tcv = p->querylist->wait;
		if(p->answerlist && dl_tv_smaller(&p->answerlist->wait, &tcv))
			tcv = p->answerlist->wait;
	}
	if(peek) {
		/* peek can be unaligned */
		/* use wait as a temp variable */
		memmove(wait, peek, sizeof(*wait));
		if(!have_tcpval)
			tcv = *wait;
		else if(dl_tv_smaller(wait, &tcv))
			tcv = *wait;
		have_tcpval = 1;
	}
	if(have_tcpval) {
		*wait = tcv;
		dl_tv_subtract(wait, now);
		return 1;
	}
	/* nothing, block */
	return 0;
}

/** clear proxy list */
static void
proxy_list_clear(struct proxy* p)
{
	char from[109];
	struct proxy* np;
	int i=0, port;
	while(p) {
		np = p->next;
		port = (int)ntohs(((struct sockaddr_in*)&p->addr)->sin_port);
		if(addr_is_ip6(&p->addr, p->addr_len)) {
			if(inet_ntop(AF_INET6, 
				&((struct sockaddr_in6*)&p->addr)->sin6_addr,
				from, (socklen_t)sizeof(from)) == 0)
				(void)strlcpy(from, "err", sizeof(from));
		} else {
			if(inet_ntop(AF_INET, 
				&((struct sockaddr_in*)&p->addr)->sin_addr,
				from, (socklen_t)sizeof(from)) == 0)
				(void)strlcpy(from, "err", sizeof(from));
		}
		printf("client[%d]: last %s@%d of %d : %u in, %u out, "
			"%u returned\n", i++, from, port, (int)p->numreuse+1,
			(unsigned)p->numwait, (unsigned)p->numsent, 
			(unsigned)p->numreturn);
		sock_close(p->s);
		free(p);
		p = np;
	}
}

/** clear TCP proxy list */
static void
tcp_proxy_list_clear(struct tcp_proxy* p)
{
	struct tcp_proxy* np;
	while(p) {
		np = p->next;
		tcp_proxy_delete(p);
		p = np;
	}
}

/** delayer service loop */
static void
service_loop(int udp_s, int listen_s, struct ringbuf* ring, 
	struct timeval* delay, struct timeval* reuse,
	struct sockaddr_storage* srv_addr, socklen_t srv_len, 
	sldns_buffer* pkt)
{
	fd_set rset, rorig;
	fd_set wset, worig;
	struct timeval now, wait;
	int max, have_wait = 0;
	struct proxy* proxies = NULL;
	struct tcp_proxy* tcp_proxies = NULL;
	struct timeval tcp_timeout;
	tcp_timeout.tv_sec = 120;
	tcp_timeout.tv_usec = 0;
#ifndef S_SPLINT_S
	FD_ZERO(&rorig);
	FD_ZERO(&worig);
	FD_SET(FD_SET_T udp_s, &rorig);
	FD_SET(FD_SET_T listen_s, &rorig);
#endif
	max = udp_s + 1;
	if(listen_s + 1 > max) max = listen_s + 1;
	while(!do_quit) {
		/* wait for events */
		rset = rorig;
		wset = worig;
		if(have_wait)
			verbose(1, "wait for %d.%6.6d",
			(unsigned)wait.tv_sec, (unsigned)wait.tv_usec);
		else	verbose(1, "wait");
		if(select(max, &rset, &wset, NULL, have_wait?&wait:NULL) < 0) {
			if(errno == EAGAIN || errno == EINTR)
				continue;
			fatal_exit("select: %s", strerror(errno));
		}
		/* get current time */
		if(gettimeofday(&now, NULL) < 0) {
			if(errno == EAGAIN || errno == EINTR)
				continue;
			fatal_exit("gettimeofday: %s", strerror(errno));
		}
		verbose(1, "process at %u.%6.6u\n", 
			(unsigned)now.tv_sec, (unsigned)now.tv_usec);
		/* sendout delayed queries to master server (frees up buffer)*/
		service_send(ring, &now, pkt, srv_addr, srv_len);
		/* proxy return replies */
		service_proxy(&rset, udp_s, proxies, pkt, &now);
		/* see what can be received to start waiting */
		service_recv(udp_s, ring, pkt, &rorig, &max, &proxies,
			srv_addr, srv_len, &now, delay, reuse);
		/* see if there are new tcp connections */
		service_tcp_listen(listen_s, &rorig, &max, &tcp_proxies,
			srv_addr, srv_len, &now, &tcp_timeout);
		/* service tcp connections */
		service_tcp_relay(&tcp_proxies, &now, delay, &tcp_timeout, 
			pkt, &rset, &rorig, &worig);
		/* see what next timeout is (if any) */
		have_wait = service_findwait(&now, &wait, ring, tcp_proxies);
	}
	proxy_list_clear(proxies);
	tcp_proxy_list_clear(tcp_proxies);
}

/** delayer main service routine */
static void
service(const char* bind_str, int bindport, const char* serv_str, 
	size_t memsize, int delay_msec)
{
	struct sockaddr_storage bind_addr, srv_addr;
	socklen_t bind_len, srv_len;
	struct ringbuf* ring = ring_create(memsize);
	struct timeval delay, reuse;
	sldns_buffer* pkt;
	int i, s, listen_s;
#ifndef S_SPLINT_S
	delay.tv_sec = delay_msec / 1000;
	delay.tv_usec = (delay_msec % 1000)*1000;
#endif
	reuse = delay; /* reuse is max(4*delay, 1 second) */
	dl_tv_add(&reuse, &delay);
	dl_tv_add(&reuse, &delay);
	dl_tv_add(&reuse, &delay);
	if(reuse.tv_sec == 0)
		reuse.tv_sec = 1;
	if(!extstrtoaddr(serv_str, &srv_addr, &srv_len, UNBOUND_DNS_PORT)) {
		printf("cannot parse forward address: %s\n", serv_str);
		exit(1);
	}
	pkt = sldns_buffer_new(65535);
	if(!pkt)
		fatal_exit("out of memory");
	if( signal(SIGINT, delayer_sigh) == SIG_ERR ||
#ifdef SIGHUP
		signal(SIGHUP, delayer_sigh) == SIG_ERR ||
#endif
#ifdef SIGQUIT
		signal(SIGQUIT, delayer_sigh) == SIG_ERR ||
#endif
#ifdef SIGBREAK
		signal(SIGBREAK, delayer_sigh) == SIG_ERR ||
#endif
#ifdef SIGALRM
		signal(SIGALRM, delayer_sigh) == SIG_ERR ||
#endif
		signal(SIGTERM, delayer_sigh) == SIG_ERR)
		fatal_exit("could not bind to signal");
	/* bind UDP port */
	if((s = socket(str_is_ip6(bind_str)?AF_INET6:AF_INET,
		SOCK_DGRAM, 0)) == -1) {
		fatal_exit("socket: %s", sock_strerror(errno));
	}
	i=0;
	if(bindport == 0) {
		bindport = 1024 + ((int)arc4random())%64000;
		i = 100;
	}
	while(1) {
		if(!ipstrtoaddr(bind_str, bindport, &bind_addr, &bind_len)) {
			printf("cannot parse listen address: %s\n", bind_str);
			exit(1);
		}
		if(bind(s, (struct sockaddr*)&bind_addr, bind_len) == -1) {
			log_err("bind: %s", sock_strerror(errno));
			if(i--==0)
				fatal_exit("cannot bind any port");
			bindport = 1024 + ((int)arc4random())%64000;
		} else break;
	}
	fd_set_nonblock(s);
	/* and TCP port */
	if((listen_s = socket(str_is_ip6(bind_str)?AF_INET6:AF_INET,
		SOCK_STREAM, 0)) == -1) {
		fatal_exit("tcp socket: %s", sock_strerror(errno));
	}
#ifdef SO_REUSEADDR
	if(1) {
		int on = 1;
		if(setsockopt(listen_s, SOL_SOCKET, SO_REUSEADDR, (void*)&on,
			(socklen_t)sizeof(on)) < 0)
			fatal_exit("setsockopt(.. SO_REUSEADDR ..) failed: %s",
				sock_strerror(errno));
	}
#endif
	if(bind(listen_s, (struct sockaddr*)&bind_addr, bind_len) == -1) {
		fatal_exit("tcp bind: %s", sock_strerror(errno));
	}
	if(listen(listen_s, 5) == -1) {
		fatal_exit("tcp listen: %s", sock_strerror(errno));
	}
	fd_set_nonblock(listen_s);
	printf("listening on port: %d\n", bindport);

	/* process loop */
	do_quit = 0;
	service_loop(s, listen_s, ring, &delay, &reuse, &srv_addr, srv_len, 
		pkt);

	/* cleanup */
	verbose(1, "cleanup");
	sock_close(s);
	sock_close(listen_s);
	sldns_buffer_free(pkt);
	ring_delete(ring);
}

/** getopt global, in case header files fail to declare it. */
extern int optind;
/** getopt global, in case header files fail to declare it. */
extern char* optarg;

/** main program for delayer */
int main(int argc, char** argv) 
{
	int c;		/* defaults */
	const char* server = "127.0.0.1@53";
	const char* bindto = "0.0.0.0";
	int bindport = 0;
	size_t memsize = 10*1024*1024;
	int delay = 100;

	verbosity = 0;
	log_init(0, 0, 0);
	log_ident_set("delayer");
	if(argc == 1) usage(argv);
	while( (c=getopt(argc, argv, "b:d:f:hm:p:")) != -1) {
		switch(c) {
			case 'b':
				bindto = optarg;
				break;
			case 'd':
				if(atoi(optarg)==0 && strcmp(optarg,"0")!=0) {
					printf("bad delay: %s\n", optarg);
					return 1;
				}
				delay = atoi(optarg);
				break;
			case 'f':
				server = optarg;
				break;
			case 'm':
				if(!cfg_parse_memsize(optarg, &memsize)) {
					printf("bad memsize: %s\n", optarg);
					return 1;
				}
				break;
			case 'p':
				if(atoi(optarg)==0 && strcmp(optarg,"0")!=0) {
					printf("bad port nr: %s\n", optarg);
					return 1;
				}
				bindport = atoi(optarg);
				break;
			case 'h':
			case '?':
			default:
				usage(argv);
		}
	}
	argc -= optind;
	argv += optind;
	if(argc != 0)
		usage(argv);

	printf("bind to %s @ %d and forward to %s after %d msec\n", 
		bindto, bindport, server, delay);
	service(bindto, bindport, server, memsize, delay);
	return 0;
}
