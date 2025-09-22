/*
 * util/netevent.c - event notification
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
 */
#include "config.h"
#include "util/netevent.h"
#include "util/ub_event.h"
#include "util/log.h"
#include "util/net_help.h"
#include "util/tcp_conn_limit.h"
#include "util/fptr_wlist.h"
#include "util/proxy_protocol.h"
#include "util/timeval_func.h"
#include "sldns/pkthdr.h"
#include "sldns/sbuffer.h"
#include "sldns/str2wire.h"
#include "dnstap/dnstap.h"
#include "dnscrypt/dnscrypt.h"
#include "services/listen_dnsport.h"
#include "util/random.h"
#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif
#ifdef HAVE_NETDB_H
#include <netdb.h>
#endif
#ifdef HAVE_POLL_H
#include <poll.h>
#endif

#ifdef HAVE_OPENSSL_SSL_H
#include <openssl/ssl.h>
#endif
#ifdef HAVE_OPENSSL_ERR_H
#include <openssl/err.h>
#endif

#ifdef HAVE_NGTCP2
#include <ngtcp2/ngtcp2.h>
#include <ngtcp2/ngtcp2_crypto.h>
#endif

#ifdef HAVE_LINUX_NET_TSTAMP_H
#include <linux/net_tstamp.h>
#endif

/* -------- Start of local definitions -------- */
/** if CMSG_ALIGN is not defined on this platform, a workaround */
#ifndef CMSG_ALIGN
#  ifdef __CMSG_ALIGN
#    define CMSG_ALIGN(n) __CMSG_ALIGN(n)
#  elif defined(CMSG_DATA_ALIGN)
#    define CMSG_ALIGN _CMSG_DATA_ALIGN
#  else
#    define CMSG_ALIGN(len) (((len)+sizeof(long)-1) & ~(sizeof(long)-1))
#  endif
#endif

/** if CMSG_LEN is not defined on this platform, a workaround */
#ifndef CMSG_LEN
#  define CMSG_LEN(len) (CMSG_ALIGN(sizeof(struct cmsghdr))+(len))
#endif

/** if CMSG_SPACE is not defined on this platform, a workaround */
#ifndef CMSG_SPACE
#  ifdef _CMSG_HDR_ALIGN
#    define CMSG_SPACE(l) (CMSG_ALIGN(l)+_CMSG_HDR_ALIGN(sizeof(struct cmsghdr)))
#  else
#    define CMSG_SPACE(l) (CMSG_ALIGN(l)+CMSG_ALIGN(sizeof(struct cmsghdr)))
#  endif
#endif

/** The TCP writing query timeout in milliseconds */
#define TCP_QUERY_TIMEOUT 120000
/** The minimum actual TCP timeout to use, regardless of what we advertise,
 * in msec */
#define TCP_QUERY_TIMEOUT_MINIMUM 200

#ifndef NONBLOCKING_IS_BROKEN
/** number of UDP reads to perform per read indication from select */
#define NUM_UDP_PER_SELECT 100
#else
#define NUM_UDP_PER_SELECT 1
#endif

/** timeout in millisec to wait for write to unblock, packets dropped after.*/
#define SEND_BLOCKED_WAIT_TIMEOUT 200
/** max number of times to wait for write to unblock, packets dropped after.*/
#define SEND_BLOCKED_MAX_RETRY 5

/** Let's make timestamping code cleaner and redefine SO_TIMESTAMP* */
#ifndef SO_TIMESTAMP
#define SO_TIMESTAMP 29
#endif
#ifndef SO_TIMESTAMPNS
#define SO_TIMESTAMPNS 35
#endif
#ifndef SO_TIMESTAMPING
#define SO_TIMESTAMPING 37
#endif
/**
 * The internal event structure for keeping ub_event info for the event.
 * Possibly other structures (list, tree) this is part of.
 */
struct internal_event {
	/** the comm base */
	struct comm_base* base;
	/** ub_event event type */
	struct ub_event* ev;
};

/**
 * Internal base structure, so that every thread has its own events.
 */
struct internal_base {
	/** ub_event event_base type. */
	struct ub_event_base* base;
	/** seconds time pointer points here */
	time_t secs;
	/** timeval with current time */
	struct timeval now;
	/** the event used for slow_accept timeouts */
	struct ub_event* slow_accept;
	/** true if slow_accept is enabled */
	int slow_accept_enabled;
	/** last log time for slow logging of file descriptor errors */
	time_t last_slow_log;
	/** last log time for slow logging of write wait failures */
	time_t last_writewait_log;
};

/**
 * Internal timer structure, to store timer event in.
 */
struct internal_timer {
	/** the super struct from which derived */
	struct comm_timer super;
	/** the comm base */
	struct comm_base* base;
	/** ub_event event type */
	struct ub_event* ev;
	/** is timer enabled */
	uint8_t enabled;
};

/**
 * Internal signal structure, to store signal event in.
 */
struct internal_signal {
	/** ub_event event type */
	struct ub_event* ev;
	/** next in signal list */
	struct internal_signal* next;
};

/** create a tcp handler with a parent */
static struct comm_point* comm_point_create_tcp_handler(
	struct comm_base *base, struct comm_point* parent, size_t bufsize,
	struct sldns_buffer* spoolbuf, comm_point_callback_type* callback,
	void* callback_arg, struct unbound_socket* socket);

/* -------- End of local definitions -------- */

struct comm_base*
comm_base_create(int sigs)
{
	struct comm_base* b = (struct comm_base*)calloc(1,
		sizeof(struct comm_base));
	const char *evnm="event", *evsys="", *evmethod="";

	if(!b)
		return NULL;
	b->eb = (struct internal_base*)calloc(1, sizeof(struct internal_base));
	if(!b->eb) {
		free(b);
		return NULL;
	}
	b->eb->base = ub_default_event_base(sigs, &b->eb->secs, &b->eb->now);
	if(!b->eb->base) {
		free(b->eb);
		free(b);
		return NULL;
	}
	ub_comm_base_now(b);
	ub_get_event_sys(b->eb->base, &evnm, &evsys, &evmethod);
	verbose(VERB_ALGO, "%s %s uses %s method.", evnm, evsys, evmethod);
	return b;
}

struct comm_base*
comm_base_create_event(struct ub_event_base* base)
{
	struct comm_base* b = (struct comm_base*)calloc(1,
		sizeof(struct comm_base));
	if(!b)
		return NULL;
	b->eb = (struct internal_base*)calloc(1, sizeof(struct internal_base));
	if(!b->eb) {
		free(b);
		return NULL;
	}
	b->eb->base = base;
	ub_comm_base_now(b);
	return b;
}

void
comm_base_delete(struct comm_base* b)
{
	if(!b)
		return;
	if(b->eb->slow_accept_enabled) {
		if(ub_event_del(b->eb->slow_accept) != 0) {
			log_err("could not event_del slow_accept");
		}
		ub_event_free(b->eb->slow_accept);
	}
	ub_event_base_free(b->eb->base);
	b->eb->base = NULL;
	free(b->eb);
	free(b);
}

void
comm_base_delete_no_base(struct comm_base* b)
{
	if(!b)
		return;
	if(b->eb->slow_accept_enabled) {
		if(ub_event_del(b->eb->slow_accept) != 0) {
			log_err("could not event_del slow_accept");
		}
		ub_event_free(b->eb->slow_accept);
	}
	b->eb->base = NULL;
	free(b->eb);
	free(b);
}

void
comm_base_timept(struct comm_base* b, time_t** tt, struct timeval** tv)
{
	*tt = &b->eb->secs;
	*tv = &b->eb->now;
}

void
comm_base_dispatch(struct comm_base* b)
{
	int retval;
	retval = ub_event_base_dispatch(b->eb->base);
	if(retval < 0) {
		fatal_exit("event_dispatch returned error %d, "
			"errno is %s", retval, strerror(errno));
	}
}

void comm_base_exit(struct comm_base* b)
{
	if(ub_event_base_loopexit(b->eb->base) != 0) {
		log_err("Could not loopexit");
	}
}

void comm_base_set_slow_accept_handlers(struct comm_base* b,
	void (*stop_acc)(void*), void (*start_acc)(void*), void* arg)
{
	b->stop_accept = stop_acc;
	b->start_accept = start_acc;
	b->cb_arg = arg;
}

struct ub_event_base* comm_base_internal(struct comm_base* b)
{
	return b->eb->base;
}

struct ub_event* comm_point_internal(struct comm_point* c)
{
	return c->ev->ev;
}

/** see if errno for udp has to be logged or not uses globals */
static int
udp_send_errno_needs_log(struct sockaddr* addr, socklen_t addrlen)
{
	/* do not log transient errors (unless high verbosity) */
#if defined(ENETUNREACH) || defined(EHOSTDOWN) || defined(EHOSTUNREACH) || defined(ENETDOWN)
	switch(errno) {
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
		case EPERM:
		case EACCES:
			if(verbosity < VERB_ALGO)
				return 0;
			break;
		default:
			break;
	}
#endif
	/* permission denied is gotten for every send if the
	 * network is disconnected (on some OS), squelch it */
	if( ((errno == EPERM)
#  ifdef EADDRNOTAVAIL
		/* 'Cannot assign requested address' also when disconnected */
		|| (errno == EADDRNOTAVAIL)
#  endif
		) && verbosity < VERB_ALGO)
		return 0;
#  ifdef EADDRINUSE
	/* If SO_REUSEADDR is set, we could try to connect to the same server
	 * from the same source port twice. */
	if(errno == EADDRINUSE && verbosity < VERB_DETAIL)
		return 0;
#  endif
	/* squelch errors where people deploy AAAA ::ffff:bla for
	 * authority servers, which we try for intranets. */
	if(errno == EINVAL && addr_is_ip4mapped(
		(struct sockaddr_storage*)addr, addrlen) &&
		verbosity < VERB_DETAIL)
		return 0;
	/* SO_BROADCAST sockopt can give access to 255.255.255.255,
	 * but a dns cache does not need it. */
	if(errno == EACCES && addr_is_broadcast(
		(struct sockaddr_storage*)addr, addrlen) &&
		verbosity < VERB_DETAIL)
		return 0;
#  ifdef ENOTCONN
	/* For 0.0.0.0, ::0 targets it can return that socket is not connected.
	 * This can be ignored, and the address skipped. It remains
	 * possible to send there for completeness in configuration. */
	if(errno == ENOTCONN && addr_is_any(
		(struct sockaddr_storage*)addr, addrlen) &&
		verbosity < VERB_DETAIL)
		return 0;
#  endif
	return 1;
}

int tcp_connect_errno_needs_log(struct sockaddr* addr, socklen_t addrlen)
{
	return udp_send_errno_needs_log(addr, addrlen);
}

/* send a UDP reply */
int
comm_point_send_udp_msg(struct comm_point *c, sldns_buffer* packet,
	struct sockaddr* addr, socklen_t addrlen, int is_connected)
{
	ssize_t sent;
	log_assert(c->fd != -1);
#ifdef UNBOUND_DEBUG
	if(sldns_buffer_remaining(packet) == 0)
		log_err("error: send empty UDP packet");
#endif
	log_assert(addr && addrlen > 0);
	if(!is_connected) {
		sent = sendto(c->fd, (void*)sldns_buffer_begin(packet),
			sldns_buffer_remaining(packet), 0,
			addr, addrlen);
	} else {
		sent = send(c->fd, (void*)sldns_buffer_begin(packet),
			sldns_buffer_remaining(packet), 0);
	}
	if(sent == -1) {
		/* try again and block, waiting for IO to complete,
		 * we want to send the answer, and we will wait for
		 * the ethernet interface buffer to have space. */
#ifndef USE_WINSOCK
		if(errno == EAGAIN || errno == EINTR ||
#  ifdef EWOULDBLOCK
			errno == EWOULDBLOCK ||
#  endif
			errno == ENOBUFS) {
#else
		if(WSAGetLastError() == WSAEINPROGRESS ||
			WSAGetLastError() == WSAEINTR ||
			WSAGetLastError() == WSAENOBUFS ||
			WSAGetLastError() == WSAEWOULDBLOCK) {
#endif
			int retries = 0;
			/* if we set the fd blocking, other threads suddenly
			 * have a blocking fd that they operate on */
			while(sent == -1 && retries < SEND_BLOCKED_MAX_RETRY && (
#ifndef USE_WINSOCK
				errno == EAGAIN || errno == EINTR ||
#  ifdef EWOULDBLOCK
				errno == EWOULDBLOCK ||
#  endif
				errno == ENOBUFS
#else
				WSAGetLastError() == WSAEINPROGRESS ||
				WSAGetLastError() == WSAEINTR ||
				WSAGetLastError() == WSAENOBUFS ||
				WSAGetLastError() == WSAEWOULDBLOCK
#endif
			)) {
#if defined(HAVE_POLL) || defined(USE_WINSOCK)
				int send_nobufs = (
#ifndef USE_WINSOCK
					errno == ENOBUFS
#else
					WSAGetLastError() == WSAENOBUFS
#endif
				);
				struct pollfd p;
				int pret;
				memset(&p, 0, sizeof(p));
				p.fd = c->fd;
				p.events = POLLOUT
#ifndef USE_WINSOCK
					| POLLERR | POLLHUP
#endif
					;
#  ifndef USE_WINSOCK
				pret = poll(&p, 1, SEND_BLOCKED_WAIT_TIMEOUT);
#  else
				pret = WSAPoll(&p, 1,
					SEND_BLOCKED_WAIT_TIMEOUT);
#  endif
				if(pret == 0) {
					/* timer expired */
					struct comm_base* b = c->ev->base;
					if(b->eb->last_writewait_log+SLOW_LOG_TIME <=
						b->eb->secs) {
						b->eb->last_writewait_log = b->eb->secs;
						verbose(VERB_OPS, "send udp blocked "
							"for long, dropping packet.");
					}
					return 0;
				} else if(pret < 0 &&
#ifndef USE_WINSOCK
					errno != EAGAIN && errno != EINTR &&
#  ifdef EWOULDBLOCK
					errno != EWOULDBLOCK &&
#  endif
					errno != ENOMEM && errno != ENOBUFS
#else
					WSAGetLastError() != WSAEINPROGRESS &&
					WSAGetLastError() != WSAEINTR &&
					WSAGetLastError() != WSAENOBUFS &&
					WSAGetLastError() != WSAEWOULDBLOCK
#endif
					) {
					log_err("poll udp out failed: %s",
						sock_strerror(errno));
					return 0;
				} else if((pret < 0 &&
#ifndef USE_WINSOCK
					( errno == ENOBUFS  /* Maybe some systems */
					|| errno == ENOMEM  /* Linux */
					|| errno == EAGAIN)  /* Macos, solaris, openbsd */
#else
					WSAGetLastError() == WSAENOBUFS
#endif
					) || (send_nobufs && retries > 0)) {
					/* ENOBUFS/ENOMEM/EAGAIN, and poll
					 * returned without
					 * a timeout. Or the retried send call
					 * returned ENOBUFS/ENOMEM/EAGAIN.
					 * It is good to wait a bit for the
					 * error to clear. */
					/* The timeout is 20*(2^(retries+1)),
					 * it increases exponentially, starting
					 * at 40 msec. After 5 tries, 1240 msec
					 * have passed in total, when poll
					 * returned the error, and 1200 msec
					 * when send returned the errors. */
#ifndef USE_WINSOCK
					pret = poll(NULL, 0, (SEND_BLOCKED_WAIT_TIMEOUT/10)<<(retries+1));
#else
					Sleep((SEND_BLOCKED_WAIT_TIMEOUT/10)<<(retries+1));
					pret = 0;
#endif
					if(pret < 0
#ifndef USE_WINSOCK
						&& errno != EAGAIN && errno != EINTR &&
#  ifdef EWOULDBLOCK
						errno != EWOULDBLOCK &&
#  endif
						errno != ENOMEM && errno != ENOBUFS
#else
						/* Sleep does not error */
#endif
					) {
						log_err("poll udp out timer failed: %s",
							sock_strerror(errno));
					}
				}
#endif /* defined(HAVE_POLL) || defined(USE_WINSOCK) */
				retries++;
				if (!is_connected) {
					sent = sendto(c->fd, (void*)sldns_buffer_begin(packet),
						sldns_buffer_remaining(packet), 0,
						addr, addrlen);
				} else {
					sent = send(c->fd, (void*)sldns_buffer_begin(packet),
						sldns_buffer_remaining(packet), 0);
				}
			}
		}
	}
	if(sent == -1) {
		if(!udp_send_errno_needs_log(addr, addrlen))
			return 0;
		if (!is_connected) {
			verbose(VERB_OPS, "sendto failed: %s", sock_strerror(errno));
		} else {
			verbose(VERB_OPS, "send failed: %s", sock_strerror(errno));
		}
		if(addr)
			log_addr(VERB_OPS, "remote address is",
				(struct sockaddr_storage*)addr, addrlen);
		return 0;
	} else if((size_t)sent != sldns_buffer_remaining(packet)) {
		log_err("sent %d in place of %d bytes",
			(int)sent, (int)sldns_buffer_remaining(packet));
		return 0;
	}
	return 1;
}

#if defined(AF_INET6) && defined(IPV6_PKTINFO) && (defined(HAVE_RECVMSG) || defined(HAVE_SENDMSG))
/** print debug ancillary info */
static void p_ancil(const char* str, struct comm_reply* r)
{
	if(r->srctype != 4 && r->srctype != 6) {
		log_info("%s: unknown srctype %d", str, r->srctype);
		return;
	}

	if(r->srctype == 6) {
#ifdef IPV6_PKTINFO
		char buf[1024];
		if(inet_ntop(AF_INET6, &r->pktinfo.v6info.ipi6_addr,
			buf, (socklen_t)sizeof(buf)) == 0) {
			(void)strlcpy(buf, "(inet_ntop error)", sizeof(buf));
		}
		buf[sizeof(buf)-1]=0;
		log_info("%s: %s %d", str, buf, r->pktinfo.v6info.ipi6_ifindex);
#endif
	} else if(r->srctype == 4) {
#ifdef IP_PKTINFO
		char buf1[1024], buf2[1024];
		if(inet_ntop(AF_INET, &r->pktinfo.v4info.ipi_addr,
			buf1, (socklen_t)sizeof(buf1)) == 0) {
			(void)strlcpy(buf1, "(inet_ntop error)", sizeof(buf1));
		}
		buf1[sizeof(buf1)-1]=0;
#ifdef HAVE_STRUCT_IN_PKTINFO_IPI_SPEC_DST
		if(inet_ntop(AF_INET, &r->pktinfo.v4info.ipi_spec_dst,
			buf2, (socklen_t)sizeof(buf2)) == 0) {
			(void)strlcpy(buf2, "(inet_ntop error)", sizeof(buf2));
		}
		buf2[sizeof(buf2)-1]=0;
#else
		buf2[0]=0;
#endif
		log_info("%s: %d %s %s", str, r->pktinfo.v4info.ipi_ifindex,
			buf1, buf2);
#elif defined(IP_RECVDSTADDR)
		char buf1[1024];
		if(inet_ntop(AF_INET, &r->pktinfo.v4addr,
			buf1, (socklen_t)sizeof(buf1)) == 0) {
			(void)strlcpy(buf1, "(inet_ntop error)", sizeof(buf1));
		}
		buf1[sizeof(buf1)-1]=0;
		log_info("%s: %s", str, buf1);
#endif /* IP_PKTINFO or PI_RECVDSTDADDR */
	}
}
#endif /* AF_INET6 && IPV6_PKTINFO && HAVE_RECVMSG||HAVE_SENDMSG */

/** send a UDP reply over specified interface*/
static int
comm_point_send_udp_msg_if(struct comm_point *c, sldns_buffer* packet,
	struct sockaddr* addr, socklen_t addrlen, struct comm_reply* r)
{
#if defined(AF_INET6) && defined(IPV6_PKTINFO) && defined(HAVE_SENDMSG)
	ssize_t sent;
	struct msghdr msg;
	struct iovec iov[1];
	union {
		struct cmsghdr hdr;
		char buf[256];
	} control;
#ifndef S_SPLINT_S
	struct cmsghdr *cmsg;
#endif /* S_SPLINT_S */

	log_assert(c->fd != -1);
#ifdef UNBOUND_DEBUG
	if(sldns_buffer_remaining(packet) == 0)
		log_err("error: send empty UDP packet");
#endif
	log_assert(addr && addrlen > 0);

	msg.msg_name = addr;
	msg.msg_namelen = addrlen;
	iov[0].iov_base = sldns_buffer_begin(packet);
	iov[0].iov_len = sldns_buffer_remaining(packet);
	msg.msg_iov = iov;
	msg.msg_iovlen = 1;
	msg.msg_control = control.buf;
#ifndef S_SPLINT_S
	msg.msg_controllen = sizeof(control.buf);
#endif /* S_SPLINT_S */
	msg.msg_flags = 0;

#ifndef S_SPLINT_S
	cmsg = CMSG_FIRSTHDR(&msg);
	if(r->srctype == 4) {
#ifdef IP_PKTINFO
		void* cmsg_data;
		msg.msg_controllen = CMSG_SPACE(sizeof(struct in_pktinfo));
		log_assert(msg.msg_controllen <= sizeof(control.buf));
		cmsg->cmsg_level = IPPROTO_IP;
		cmsg->cmsg_type = IP_PKTINFO;
		memmove(CMSG_DATA(cmsg), &r->pktinfo.v4info,
			sizeof(struct in_pktinfo));
		/* unset the ifindex to not bypass the routing tables */
		cmsg_data = CMSG_DATA(cmsg);
		((struct in_pktinfo *) cmsg_data)->ipi_ifindex = 0;
		cmsg->cmsg_len = CMSG_LEN(sizeof(struct in_pktinfo));
		/* zero the padding bytes inserted by the CMSG_LEN */
		if(sizeof(struct in_pktinfo) < cmsg->cmsg_len)
			memset(((uint8_t*)(CMSG_DATA(cmsg))) +
				sizeof(struct in_pktinfo), 0, cmsg->cmsg_len
				- sizeof(struct in_pktinfo));
#elif defined(IP_SENDSRCADDR)
		msg.msg_controllen = CMSG_SPACE(sizeof(struct in_addr));
		log_assert(msg.msg_controllen <= sizeof(control.buf));
		cmsg->cmsg_level = IPPROTO_IP;
		cmsg->cmsg_type = IP_SENDSRCADDR;
		memmove(CMSG_DATA(cmsg), &r->pktinfo.v4addr,
			sizeof(struct in_addr));
		cmsg->cmsg_len = CMSG_LEN(sizeof(struct in_addr));
		/* zero the padding bytes inserted by the CMSG_LEN */
		if(sizeof(struct in_addr) < cmsg->cmsg_len)
			memset(((uint8_t*)(CMSG_DATA(cmsg))) +
				sizeof(struct in_addr), 0, cmsg->cmsg_len
				- sizeof(struct in_addr));
#else
		verbose(VERB_ALGO, "no IP_PKTINFO or IP_SENDSRCADDR");
		msg.msg_control = NULL;
#endif /* IP_PKTINFO or IP_SENDSRCADDR */
	} else if(r->srctype == 6) {
		void* cmsg_data;
		msg.msg_controllen = CMSG_SPACE(sizeof(struct in6_pktinfo));
		log_assert(msg.msg_controllen <= sizeof(control.buf));
		cmsg->cmsg_level = IPPROTO_IPV6;
		cmsg->cmsg_type = IPV6_PKTINFO;
		memmove(CMSG_DATA(cmsg), &r->pktinfo.v6info,
			sizeof(struct in6_pktinfo));
		/* unset the ifindex to not bypass the routing tables */
		cmsg_data = CMSG_DATA(cmsg);
		((struct in6_pktinfo *) cmsg_data)->ipi6_ifindex = 0;
		cmsg->cmsg_len = CMSG_LEN(sizeof(struct in6_pktinfo));
		/* zero the padding bytes inserted by the CMSG_LEN */
		if(sizeof(struct in6_pktinfo) < cmsg->cmsg_len)
			memset(((uint8_t*)(CMSG_DATA(cmsg))) +
				sizeof(struct in6_pktinfo), 0, cmsg->cmsg_len
				- sizeof(struct in6_pktinfo));
	} else {
		/* try to pass all 0 to use default route */
		msg.msg_controllen = CMSG_SPACE(sizeof(struct in6_pktinfo));
		log_assert(msg.msg_controllen <= sizeof(control.buf));
		cmsg->cmsg_level = IPPROTO_IPV6;
		cmsg->cmsg_type = IPV6_PKTINFO;
		memset(CMSG_DATA(cmsg), 0, sizeof(struct in6_pktinfo));
		cmsg->cmsg_len = CMSG_LEN(sizeof(struct in6_pktinfo));
		/* zero the padding bytes inserted by the CMSG_LEN */
		if(sizeof(struct in6_pktinfo) < cmsg->cmsg_len)
			memset(((uint8_t*)(CMSG_DATA(cmsg))) +
				sizeof(struct in6_pktinfo), 0, cmsg->cmsg_len
				- sizeof(struct in6_pktinfo));
	}
#endif /* S_SPLINT_S */
	if(verbosity >= VERB_ALGO && r->srctype != 0)
		p_ancil("send_udp over interface", r);
	sent = sendmsg(c->fd, &msg, 0);
	if(sent == -1) {
		/* try again and block, waiting for IO to complete,
		 * we want to send the answer, and we will wait for
		 * the ethernet interface buffer to have space. */
#ifndef USE_WINSOCK
		if(errno == EAGAIN || errno == EINTR ||
#  ifdef EWOULDBLOCK
			errno == EWOULDBLOCK ||
#  endif
			errno == ENOBUFS) {
#else
		if(WSAGetLastError() == WSAEINPROGRESS ||
			WSAGetLastError() == WSAEINTR ||
			WSAGetLastError() == WSAENOBUFS ||
			WSAGetLastError() == WSAEWOULDBLOCK) {
#endif
			int retries = 0;
			while(sent == -1 && retries < SEND_BLOCKED_MAX_RETRY && (
#ifndef USE_WINSOCK
				errno == EAGAIN || errno == EINTR ||
#  ifdef EWOULDBLOCK
				errno == EWOULDBLOCK ||
#  endif
				errno == ENOBUFS
#else
				WSAGetLastError() == WSAEINPROGRESS ||
				WSAGetLastError() == WSAEINTR ||
				WSAGetLastError() == WSAENOBUFS ||
				WSAGetLastError() == WSAEWOULDBLOCK
#endif
			)) {
#if defined(HAVE_POLL) || defined(USE_WINSOCK)
				int send_nobufs = (
#ifndef USE_WINSOCK
					errno == ENOBUFS
#else
					WSAGetLastError() == WSAENOBUFS
#endif
				);
				struct pollfd p;
				int pret;
				memset(&p, 0, sizeof(p));
				p.fd = c->fd;
				p.events = POLLOUT
#ifndef USE_WINSOCK
					| POLLERR | POLLHUP
#endif
					;
#  ifndef USE_WINSOCK
				pret = poll(&p, 1, SEND_BLOCKED_WAIT_TIMEOUT);
#  else
				pret = WSAPoll(&p, 1,
					SEND_BLOCKED_WAIT_TIMEOUT);
#  endif
				if(pret == 0) {
					/* timer expired */
					struct comm_base* b = c->ev->base;
					if(b->eb->last_writewait_log+SLOW_LOG_TIME <=
						b->eb->secs) {
						b->eb->last_writewait_log = b->eb->secs;
						verbose(VERB_OPS, "send udp blocked "
							"for long, dropping packet.");
					}
					return 0;
				} else if(pret < 0 &&
#ifndef USE_WINSOCK
					errno != EAGAIN && errno != EINTR &&
#  ifdef EWOULDBLOCK
					errno != EWOULDBLOCK &&
#  endif
					errno != ENOMEM && errno != ENOBUFS
#else
					WSAGetLastError() != WSAEINPROGRESS &&
					WSAGetLastError() != WSAEINTR &&
					WSAGetLastError() != WSAENOBUFS &&
					WSAGetLastError() != WSAEWOULDBLOCK
#endif
					) {
					log_err("poll udp out failed: %s",
						sock_strerror(errno));
					return 0;
				} else if((pret < 0 &&
#ifndef USE_WINSOCK
					( errno == ENOBUFS  /* Maybe some systems */
					|| errno == ENOMEM  /* Linux */
					|| errno == EAGAIN)  /* Macos, solaris, openbsd */
#else
					WSAGetLastError() == WSAENOBUFS
#endif
					) || (send_nobufs && retries > 0)) {
					/* ENOBUFS/ENOMEM/EAGAIN, and poll
					 * returned without
					 * a timeout. Or the retried send call
					 * returned ENOBUFS/ENOMEM/EAGAIN.
					 * It is good to wait a bit for the
					 * error to clear. */
					/* The timeout is 20*(2^(retries+1)),
					 * it increases exponentially, starting
					 * at 40 msec. After 5 tries, 1240 msec
					 * have passed in total, when poll
					 * returned the error, and 1200 msec
					 * when send returned the errors. */
#ifndef USE_WINSOCK
					pret = poll(NULL, 0, (SEND_BLOCKED_WAIT_TIMEOUT/10)<<(retries+1));
#else
					Sleep((SEND_BLOCKED_WAIT_TIMEOUT/10)<<(retries+1));
					pret = 0;
#endif
					if(pret < 0
#ifndef USE_WINSOCK
						&& errno != EAGAIN && errno != EINTR &&
#  ifdef EWOULDBLOCK
						errno != EWOULDBLOCK &&
#  endif
						errno != ENOMEM && errno != ENOBUFS
#else  /* USE_WINSOCK */
						/* Sleep does not error */
#endif
					) {
						log_err("poll udp out timer failed: %s",
							sock_strerror(errno));
					}
				}
#endif /* defined(HAVE_POLL) || defined(USE_WINSOCK) */
				retries++;
				sent = sendmsg(c->fd, &msg, 0);
			}
		}
	}
	if(sent == -1) {
		if(!udp_send_errno_needs_log(addr, addrlen))
			return 0;
		verbose(VERB_OPS, "sendmsg failed: %s", strerror(errno));
		log_addr(VERB_OPS, "remote address is",
			(struct sockaddr_storage*)addr, addrlen);
#ifdef __NetBSD__
		/* netbsd 7 has IP_PKTINFO for recv but not send */
		if(errno == EINVAL && r->srctype == 4)
			log_err("sendmsg: No support for sendmsg(IP_PKTINFO). "
				"Please disable interface-automatic");
#endif
		return 0;
	} else if((size_t)sent != sldns_buffer_remaining(packet)) {
		log_err("sent %d in place of %d bytes",
			(int)sent, (int)sldns_buffer_remaining(packet));
		return 0;
	}
	return 1;
#else
	(void)c;
	(void)packet;
	(void)addr;
	(void)addrlen;
	(void)r;
	log_err("sendmsg: IPV6_PKTINFO not supported");
	return 0;
#endif /* AF_INET6 && IPV6_PKTINFO && HAVE_SENDMSG */
}

/** return true is UDP receive error needs to be logged */
static int udp_recv_needs_log(int err)
{
	switch(err) {
	case EACCES: /* some hosts send ICMP 'Permission Denied' */
#ifndef USE_WINSOCK
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
#else /* USE_WINSOCK */
	case WSAECONNREFUSED:
	case WSAENETUNREACH:
	case WSAEHOSTDOWN:
	case WSAEHOSTUNREACH:
	case WSAENETDOWN:
#endif
		if(verbosity >= VERB_ALGO)
			return 1;
		return 0;
	default:
		break;
	}
	return 1;
}

/** Parses the PROXYv2 header from buf and updates the comm_reply struct.
 *  Returns 1 on success, 0 on failure. */
static int consume_pp2_header(struct sldns_buffer* buf, struct comm_reply* rep,
	int stream) {
	size_t size;
	struct pp2_header *header;
	int err = pp2_read_header(sldns_buffer_begin(buf),
		sldns_buffer_remaining(buf));
	if(err) return 0;
	header = (struct pp2_header*)sldns_buffer_begin(buf);
	size = PP2_HEADER_SIZE + ntohs(header->len);
	if((header->ver_cmd & 0xF) == PP2_CMD_LOCAL) {
		/* A connection from the proxy itself.
		 * No need to do anything with addresses. */
		goto done;
	}
	if(header->fam_prot == PP2_UNSPEC_UNSPEC) {
		/* Unspecified family and protocol. This could be used for
		 * health checks by proxies.
		 * No need to do anything with addresses. */
		goto done;
	}
	/* Read the proxied address */
	switch(header->fam_prot) {
		case PP2_INET_STREAM:
		case PP2_INET_DGRAM:
			{
			struct sockaddr_in* addr =
				(struct sockaddr_in*)&rep->client_addr;
			addr->sin_family = AF_INET;
			addr->sin_addr.s_addr = header->addr.addr4.src_addr;
			addr->sin_port = header->addr.addr4.src_port;
			rep->client_addrlen = (socklen_t)sizeof(struct sockaddr_in);
			}
			/* Ignore the destination address; it should be us. */
			break;
		case PP2_INET6_STREAM:
		case PP2_INET6_DGRAM:
			{
			struct sockaddr_in6* addr =
				(struct sockaddr_in6*)&rep->client_addr;
			memset(addr, 0, sizeof(*addr));
			addr->sin6_family = AF_INET6;
			memcpy(&addr->sin6_addr,
				header->addr.addr6.src_addr, 16);
			addr->sin6_port = header->addr.addr6.src_port;
			rep->client_addrlen = (socklen_t)sizeof(struct sockaddr_in6);
			}
			/* Ignore the destination address; it should be us. */
			break;
		default:
			log_err("proxy_protocol: unsupported family and "
				"protocol 0x%x", (int)header->fam_prot);
			return 0;
	}
	rep->is_proxied = 1;
done:
	if(!stream) {
		/* We are reading a whole packet;
		 * Move the rest of the data to overwrite the PROXYv2 header */
		/* XXX can we do better to avoid memmove? */
		memmove(header, ((char*)header)+size,
			sldns_buffer_limit(buf)-size);
		sldns_buffer_set_limit(buf, sldns_buffer_limit(buf)-size);
	}
	return 1;
}

#if defined(AF_INET6) && defined(IPV6_PKTINFO) && defined(HAVE_RECVMSG)
void
comm_point_udp_ancil_callback(int fd, short event, void* arg)
{
	struct comm_reply rep;
	struct msghdr msg;
	struct iovec iov[1];
	ssize_t rcv;
	union {
		struct cmsghdr hdr;
		char buf[256];
	} ancil;
	int i;
#ifndef S_SPLINT_S
	struct cmsghdr* cmsg;
#endif /* S_SPLINT_S */
#ifdef HAVE_LINUX_NET_TSTAMP_H
	struct timespec *ts;
#endif /* HAVE_LINUX_NET_TSTAMP_H */

	rep.c = (struct comm_point*)arg;
	log_assert(rep.c->type == comm_udp);

	if(!(event&UB_EV_READ))
		return;
	log_assert(rep.c && rep.c->buffer && rep.c->fd == fd);
	ub_comm_base_now(rep.c->ev->base);
	for(i=0; i<NUM_UDP_PER_SELECT; i++) {
		sldns_buffer_clear(rep.c->buffer);
		timeval_clear(&rep.c->recv_tv);
		rep.remote_addrlen = (socklen_t)sizeof(rep.remote_addr);
		log_assert(fd != -1);
		log_assert(sldns_buffer_remaining(rep.c->buffer) > 0);
		msg.msg_name = &rep.remote_addr;
		msg.msg_namelen = (socklen_t)sizeof(rep.remote_addr);
		iov[0].iov_base = sldns_buffer_begin(rep.c->buffer);
		iov[0].iov_len = sldns_buffer_remaining(rep.c->buffer);
		msg.msg_iov = iov;
		msg.msg_iovlen = 1;
		msg.msg_control = ancil.buf;
#ifndef S_SPLINT_S
		msg.msg_controllen = sizeof(ancil.buf);
#endif /* S_SPLINT_S */
		msg.msg_flags = 0;
		rcv = recvmsg(fd, &msg, MSG_DONTWAIT);
		if(rcv == -1) {
			if(errno != EAGAIN && errno != EINTR
				&& udp_recv_needs_log(errno)) {
				log_err("recvmsg failed: %s", strerror(errno));
			}
			return;
		}
		rep.remote_addrlen = msg.msg_namelen;
		sldns_buffer_skip(rep.c->buffer, rcv);
		sldns_buffer_flip(rep.c->buffer);
		rep.srctype = 0;
		rep.is_proxied = 0;
#ifndef S_SPLINT_S
		for(cmsg = CMSG_FIRSTHDR(&msg); cmsg != NULL;
			cmsg = CMSG_NXTHDR(&msg, cmsg)) {
			if( cmsg->cmsg_level == IPPROTO_IPV6 &&
				cmsg->cmsg_type == IPV6_PKTINFO) {
				rep.srctype = 6;
				memmove(&rep.pktinfo.v6info, CMSG_DATA(cmsg),
					sizeof(struct in6_pktinfo));
				break;
#ifdef IP_PKTINFO
			} else if( cmsg->cmsg_level == IPPROTO_IP &&
				cmsg->cmsg_type == IP_PKTINFO) {
				rep.srctype = 4;
				memmove(&rep.pktinfo.v4info, CMSG_DATA(cmsg),
					sizeof(struct in_pktinfo));
				break;
#elif defined(IP_RECVDSTADDR)
			} else if( cmsg->cmsg_level == IPPROTO_IP &&
				cmsg->cmsg_type == IP_RECVDSTADDR) {
				rep.srctype = 4;
				memmove(&rep.pktinfo.v4addr, CMSG_DATA(cmsg),
					sizeof(struct in_addr));
				break;
#endif /* IP_PKTINFO or IP_RECVDSTADDR */
#ifdef HAVE_LINUX_NET_TSTAMP_H
			} else if( cmsg->cmsg_level == SOL_SOCKET &&
				cmsg->cmsg_type == SO_TIMESTAMPNS) {
				ts = (struct timespec *)CMSG_DATA(cmsg);
				TIMESPEC_TO_TIMEVAL(&rep.c->recv_tv, ts);
			} else if( cmsg->cmsg_level == SOL_SOCKET &&
				cmsg->cmsg_type == SO_TIMESTAMPING) {
				ts = (struct timespec *)CMSG_DATA(cmsg);
				TIMESPEC_TO_TIMEVAL(&rep.c->recv_tv, ts);
			} else if( cmsg->cmsg_level == SOL_SOCKET &&
				cmsg->cmsg_type == SO_TIMESTAMP) {
				memmove(&rep.c->recv_tv, CMSG_DATA(cmsg), sizeof(struct timeval));
#endif /* HAVE_LINUX_NET_TSTAMP_H */
			}
		}

		if(verbosity >= VERB_ALGO && rep.srctype != 0)
			p_ancil("receive_udp on interface", &rep);
#endif /* S_SPLINT_S */

		if(rep.c->pp2_enabled && !consume_pp2_header(rep.c->buffer,
			&rep, 0)) {
			log_err("proxy_protocol: could not consume PROXYv2 header");
			return;
		}
		if(!rep.is_proxied) {
			rep.client_addrlen = rep.remote_addrlen;
			memmove(&rep.client_addr, &rep.remote_addr,
				rep.remote_addrlen);
		}

		fptr_ok(fptr_whitelist_comm_point(rep.c->callback));
		if((*rep.c->callback)(rep.c, rep.c->cb_arg, NETEVENT_NOERROR, &rep)) {
			/* send back immediate reply */
			struct sldns_buffer *buffer;
#ifdef USE_DNSCRYPT
			buffer = rep.c->dnscrypt_buffer;
#else
			buffer = rep.c->buffer;
#endif
			(void)comm_point_send_udp_msg_if(rep.c, buffer,
				(struct sockaddr*)&rep.remote_addr,
				rep.remote_addrlen, &rep);
		}
		if(!rep.c || rep.c->fd == -1) /* commpoint closed */
			break;
	}
}
#endif /* AF_INET6 && IPV6_PKTINFO && HAVE_RECVMSG */

void
comm_point_udp_callback(int fd, short event, void* arg)
{
	struct comm_reply rep;
	ssize_t rcv;
	int i;
	struct sldns_buffer *buffer;

	rep.c = (struct comm_point*)arg;
	log_assert(rep.c->type == comm_udp);

	if(!(event&UB_EV_READ))
		return;
	log_assert(rep.c && rep.c->buffer && rep.c->fd == fd);
	ub_comm_base_now(rep.c->ev->base);
	for(i=0; i<NUM_UDP_PER_SELECT; i++) {
		sldns_buffer_clear(rep.c->buffer);
		rep.remote_addrlen = (socklen_t)sizeof(rep.remote_addr);
		log_assert(fd != -1);
		log_assert(sldns_buffer_remaining(rep.c->buffer) > 0);
		rcv = recvfrom(fd, (void*)sldns_buffer_begin(rep.c->buffer),
			sldns_buffer_remaining(rep.c->buffer), MSG_DONTWAIT,
			(struct sockaddr*)&rep.remote_addr, &rep.remote_addrlen);
		if(rcv == -1) {
#ifndef USE_WINSOCK
			if(errno != EAGAIN && errno != EINTR
				&& udp_recv_needs_log(errno))
				log_err("recvfrom %d failed: %s",
					fd, strerror(errno));
#else
			if(WSAGetLastError() != WSAEINPROGRESS &&
				WSAGetLastError() != WSAECONNRESET &&
				WSAGetLastError()!= WSAEWOULDBLOCK &&
				udp_recv_needs_log(WSAGetLastError()))
				log_err("recvfrom failed: %s",
					wsa_strerror(WSAGetLastError()));
#endif
			return;
		}
		sldns_buffer_skip(rep.c->buffer, rcv);
		sldns_buffer_flip(rep.c->buffer);
		rep.srctype = 0;
		rep.is_proxied = 0;

		if(rep.c->pp2_enabled && !consume_pp2_header(rep.c->buffer,
			&rep, 0)) {
			log_err("proxy_protocol: could not consume PROXYv2 header");
			return;
		}
		if(!rep.is_proxied) {
			rep.client_addrlen = rep.remote_addrlen;
			memmove(&rep.client_addr, &rep.remote_addr,
				rep.remote_addrlen);
		}

		fptr_ok(fptr_whitelist_comm_point(rep.c->callback));
		if((*rep.c->callback)(rep.c, rep.c->cb_arg, NETEVENT_NOERROR, &rep)) {
			/* send back immediate reply */
#ifdef USE_DNSCRYPT
			buffer = rep.c->dnscrypt_buffer;
#else
			buffer = rep.c->buffer;
#endif
			(void)comm_point_send_udp_msg(rep.c, buffer,
				(struct sockaddr*)&rep.remote_addr,
				rep.remote_addrlen, 0);
		}
		if(!rep.c || rep.c->fd != fd) /* commpoint closed to -1 or reused for
		another UDP port. Note rep.c cannot be reused with TCP fd. */
			break;
	}
}

#ifdef HAVE_NGTCP2
void
doq_pkt_addr_init(struct doq_pkt_addr* paddr)
{
	paddr->addrlen = (socklen_t)sizeof(paddr->addr);
	paddr->localaddrlen = (socklen_t)sizeof(paddr->localaddr);
	paddr->ifindex = 0;
}

/** set the ecn on the transmission */
static void
doq_set_ecn(int fd, int family, uint32_t ecn)
{
	unsigned int val = ecn;
	if(family == AF_INET6) {
		if(setsockopt(fd, IPPROTO_IPV6, IPV6_TCLASS, &val,
			(socklen_t)sizeof(val)) == -1) {
			log_err("setsockopt(.. IPV6_TCLASS ..): %s",
				strerror(errno));
		}
		return;
	}
	if(setsockopt(fd, IPPROTO_IP, IP_TOS, &val,
		(socklen_t)sizeof(val)) == -1) {
		log_err("setsockopt(.. IP_TOS ..): %s",
			strerror(errno));
	}
}

/** set the local address in the control ancillary data */
static void
doq_set_localaddr_cmsg(struct msghdr* msg, size_t control_size,
	struct doq_addr_storage* localaddr, socklen_t localaddrlen,
	int ifindex)
{
#ifndef S_SPLINT_S
	struct cmsghdr* cmsg;
#endif /* S_SPLINT_S */
#ifndef S_SPLINT_S
	cmsg = CMSG_FIRSTHDR(msg);
	if(localaddr->sockaddr.in.sin_family == AF_INET) {
#ifdef IP_PKTINFO
		struct sockaddr_in* sa = (struct sockaddr_in*)localaddr;
		struct in_pktinfo v4info;
		log_assert(localaddrlen >= sizeof(struct sockaddr_in));
		msg->msg_controllen = CMSG_SPACE(sizeof(struct in_pktinfo));
		memset(msg->msg_control, 0, msg->msg_controllen);
		log_assert(msg->msg_controllen <= control_size);
		cmsg->cmsg_level = IPPROTO_IP;
		cmsg->cmsg_type = IP_PKTINFO;
		memset(&v4info, 0, sizeof(v4info));
#  ifdef HAVE_STRUCT_IN_PKTINFO_IPI_SPEC_DST
		memmove(&v4info.ipi_spec_dst, &sa->sin_addr,
			sizeof(struct in_addr));
#  else
		memmove(&v4info.ipi_addr, &sa->sin_addr,
			sizeof(struct in_addr));
#  endif
		v4info.ipi_ifindex = ifindex;
		memmove(CMSG_DATA(cmsg), &v4info, sizeof(struct in_pktinfo));
		cmsg->cmsg_len = CMSG_LEN(sizeof(struct in_pktinfo));
#elif defined(IP_SENDSRCADDR)
		struct sockaddr_in* sa= (struct sockaddr_in*)localaddr;
		log_assert(localaddrlen >= sizeof(struct sockaddr_in));
		msg->msg_controllen = CMSG_SPACE(sizeof(struct in_addr));
		memset(msg->msg_control, 0, msg->msg_controllen);
		log_assert(msg->msg_controllen <= control_size);
		cmsg->cmsg_level = IPPROTO_IP;
		cmsg->cmsg_type = IP_SENDSRCADDR;
		memmove(CMSG_DATA(cmsg),  &sa->sin_addr,
			sizeof(struct in_addr));
		cmsg->cmsg_len = CMSG_LEN(sizeof(struct in_addr));
#endif
	} else {
		struct sockaddr_in6* sa6 = (struct sockaddr_in6*)localaddr;
		struct in6_pktinfo v6info;
		log_assert(localaddrlen >= sizeof(struct sockaddr_in6));
		msg->msg_controllen = CMSG_SPACE(sizeof(struct in6_pktinfo));
		memset(msg->msg_control, 0, msg->msg_controllen);
		log_assert(msg->msg_controllen <= control_size);
		cmsg->cmsg_level = IPPROTO_IPV6;
		cmsg->cmsg_type = IPV6_PKTINFO;
		memset(&v6info, 0, sizeof(v6info));
		memmove(&v6info.ipi6_addr, &sa6->sin6_addr,
			sizeof(struct in6_addr));
		v6info.ipi6_ifindex = ifindex;
		memmove(CMSG_DATA(cmsg), &v6info, sizeof(struct in6_pktinfo));
		cmsg->cmsg_len = CMSG_LEN(sizeof(struct in6_pktinfo));
	}
#endif /* S_SPLINT_S */
	/* Ignore unused variables, if no assertions are compiled. */
	(void)localaddrlen;
	(void)control_size;
}

/** write address and port into strings */
static int
doq_print_addr_port(struct doq_addr_storage* addr, socklen_t addrlen,
	char* host, size_t hostlen, char* port, size_t portlen)
{
	if(addr->sockaddr.in.sin_family == AF_INET) {
		struct sockaddr_in* sa = (struct sockaddr_in*)addr;
		log_assert(addrlen >= sizeof(*sa));
		if(inet_ntop(sa->sin_family, &sa->sin_addr, host,
			(socklen_t)hostlen) == 0) {
			log_hex("inet_ntop error: address", &sa->sin_addr,
				sizeof(sa->sin_addr));
			return 0;
		}
		snprintf(port, portlen, "%u", (unsigned)ntohs(sa->sin_port));
	} else if(addr->sockaddr.in.sin_family == AF_INET6) {
		struct sockaddr_in6* sa6 = (struct sockaddr_in6*)addr;
		log_assert(addrlen >= sizeof(*sa6));
		if(inet_ntop(sa6->sin6_family, &sa6->sin6_addr, host,
			(socklen_t)hostlen) == 0) {
			log_hex("inet_ntop error: address", &sa6->sin6_addr,
				sizeof(sa6->sin6_addr));
			return 0;
		}
		snprintf(port, portlen, "%u", (unsigned)ntohs(sa6->sin6_port));
	}
	return 1;
}

/** doq store the blocked packet when write has blocked */
static void
doq_store_blocked_pkt(struct comm_point* c, struct doq_pkt_addr* paddr,
	uint32_t ecn)
{
	if(c->doq_socket->have_blocked_pkt)
		return; /* should not happen that we write when there is
		already a blocked write, but if so, drop it. */
	if(sldns_buffer_limit(c->doq_socket->pkt_buf) >
		sldns_buffer_capacity(c->doq_socket->blocked_pkt))
		return; /* impossibly large, drop packet. impossible because
		pkt_buf and blocked_pkt are the same size. */
	c->doq_socket->have_blocked_pkt = 1;
	c->doq_socket->blocked_pkt_pi.ecn = ecn;
	memcpy(c->doq_socket->blocked_paddr, paddr,
		sizeof(*c->doq_socket->blocked_paddr));
	sldns_buffer_clear(c->doq_socket->blocked_pkt);
	sldns_buffer_write(c->doq_socket->blocked_pkt,
		sldns_buffer_begin(c->doq_socket->pkt_buf),
		sldns_buffer_limit(c->doq_socket->pkt_buf));
	sldns_buffer_flip(c->doq_socket->blocked_pkt);
}

void
doq_send_pkt(struct comm_point* c, struct doq_pkt_addr* paddr, uint32_t ecn)
{
	struct msghdr msg;
	struct iovec iov[1];
	union {
		struct cmsghdr hdr;
		char buf[256];
	} control;
	ssize_t ret;
	iov[0].iov_base = sldns_buffer_begin(c->doq_socket->pkt_buf);
	iov[0].iov_len = sldns_buffer_limit(c->doq_socket->pkt_buf);
	memset(&msg, 0, sizeof(msg));
	msg.msg_name = (void*)&paddr->addr;
	msg.msg_namelen = paddr->addrlen;
	msg.msg_iov = iov;
	msg.msg_iovlen = 1;
	msg.msg_control = control.buf;
#ifndef S_SPLINT_S
	msg.msg_controllen = sizeof(control.buf);
#endif /* S_SPLINT_S */
	msg.msg_flags = 0;

	doq_set_localaddr_cmsg(&msg, sizeof(control.buf), &paddr->localaddr,
		paddr->localaddrlen, paddr->ifindex);
	doq_set_ecn(c->fd, paddr->addr.sockaddr.in.sin_family, ecn);

	for(;;) {
		ret = sendmsg(c->fd, &msg, MSG_DONTWAIT);
		if(ret == -1 && errno == EINTR)
			continue;
		break;
	}
	if(ret == -1) {
#ifndef USE_WINSOCK
		if(errno == EAGAIN ||
#  ifdef EWOULDBLOCK
			errno == EWOULDBLOCK ||
#  endif
			errno == ENOBUFS)
#else
		if(WSAGetLastError() == WSAEINPROGRESS ||
			WSAGetLastError() == WSAENOBUFS ||
			WSAGetLastError() == WSAEWOULDBLOCK)
#endif
		{
			/* udp send has blocked */
			doq_store_blocked_pkt(c, paddr, ecn);
			return;
		}
		if(!udp_send_errno_needs_log((void*)&paddr->addr,
			paddr->addrlen))
			return;
		if(verbosity >= VERB_OPS) {
			char host[256], port[32];
			if(doq_print_addr_port(&paddr->addr, paddr->addrlen,
				host, sizeof(host), port, sizeof(port))) {
				verbose(VERB_OPS, "doq sendmsg to %s %s "
					"failed: %s", host, port,
					strerror(errno));
			} else {
				verbose(VERB_OPS, "doq sendmsg failed: %s",
					strerror(errno));
			}
		}
		return;
	} else if(ret != (ssize_t)sldns_buffer_limit(c->doq_socket->pkt_buf)) {
		char host[256], port[32];
		if(doq_print_addr_port(&paddr->addr, paddr->addrlen, host,
			sizeof(host), port, sizeof(port))) {
			log_err("doq sendmsg to %s %s failed: "
				"sent %d in place of %d bytes", 
				host, port, (int)ret,
				(int)sldns_buffer_limit(c->doq_socket->pkt_buf));
		} else {
			log_err("doq sendmsg failed: "
				"sent %d in place of %d bytes", 
				(int)ret, (int)sldns_buffer_limit(c->doq_socket->pkt_buf));
		}
		return;
	}
}

/** fetch port number */
static int
doq_sockaddr_get_port(struct doq_addr_storage* addr)
{
	if(addr->sockaddr.in.sin_family == AF_INET) {
		struct sockaddr_in* sa = (struct sockaddr_in*)addr;
		return ntohs(sa->sin_port);
	} else if(addr->sockaddr.in.sin_family == AF_INET6) {
		struct sockaddr_in6* sa6 = (struct sockaddr_in6*)addr;
		return ntohs(sa6->sin6_port);
	}
	return 0;
}

/** get local address from ancillary data headers */
static int
doq_get_localaddr_cmsg(struct comm_point* c, struct doq_pkt_addr* paddr,
	int* pkt_continue, struct msghdr* msg)
{
#ifndef S_SPLINT_S
	struct cmsghdr* cmsg;
#endif /* S_SPLINT_S */

	memset(&paddr->localaddr, 0, sizeof(paddr->localaddr));
#ifndef S_SPLINT_S
	for(cmsg = CMSG_FIRSTHDR(msg); cmsg != NULL;
		cmsg = CMSG_NXTHDR(msg, cmsg)) {
		if( cmsg->cmsg_level == IPPROTO_IPV6 &&
			cmsg->cmsg_type == IPV6_PKTINFO) {
			struct in6_pktinfo* v6info =
				(struct in6_pktinfo*)CMSG_DATA(cmsg);
			struct sockaddr_in6* sa= (struct sockaddr_in6*)
				&paddr->localaddr;
			struct sockaddr_in6* rema = (struct sockaddr_in6*)
				&paddr->addr;
			if(rema->sin6_family != AF_INET6) {
				log_err("doq cmsg family mismatch cmsg is ip6");
				*pkt_continue = 1;
				return 0;
			}
			sa->sin6_family = AF_INET6;
			sa->sin6_port = htons(doq_sockaddr_get_port(
				(void*)c->socket->addr));
			paddr->ifindex = v6info->ipi6_ifindex;
			memmove(&sa->sin6_addr, &v6info->ipi6_addr,
				sizeof(struct in6_addr));
			paddr->localaddrlen = sizeof(struct sockaddr_in6);
			break;
#ifdef IP_PKTINFO
		} else if( cmsg->cmsg_level == IPPROTO_IP &&
			cmsg->cmsg_type == IP_PKTINFO) {
			struct in_pktinfo* v4info =
				(struct in_pktinfo*)CMSG_DATA(cmsg);
			struct sockaddr_in* sa= (struct sockaddr_in*)
				&paddr->localaddr;
			struct sockaddr_in* rema = (struct sockaddr_in*)
				&paddr->addr;
			if(rema->sin_family != AF_INET) {
				log_err("doq cmsg family mismatch cmsg is ip4");
				*pkt_continue = 1;
				return 0;
			}
			sa->sin_family = AF_INET;
			sa->sin_port = htons(doq_sockaddr_get_port(
				(void*)c->socket->addr));
			paddr->ifindex = v4info->ipi_ifindex;
			memmove(&sa->sin_addr, &v4info->ipi_addr,
				sizeof(struct in_addr));
			paddr->localaddrlen = sizeof(struct sockaddr_in);
			break;
#elif defined(IP_RECVDSTADDR)
		} else if( cmsg->cmsg_level == IPPROTO_IP &&
			cmsg->cmsg_type == IP_RECVDSTADDR) {
			struct sockaddr_in* sa= (struct sockaddr_in*)
				&paddr->localaddr;
			struct sockaddr_in* rema = (struct sockaddr_in*)
				&paddr->addr;
			if(rema->sin_family != AF_INET) {
				log_err("doq cmsg family mismatch cmsg is ip4");
				*pkt_continue = 1;
				return 0;
			}
			sa->sin_family = AF_INET;
			sa->sin_port = htons(doq_sockaddr_get_port(
				(void*)c->socket->addr));
			paddr->ifindex = 0;
			memmove(&sa.sin_addr, CMSG_DATA(cmsg),
				sizeof(struct in_addr));
			paddr->localaddrlen = sizeof(struct sockaddr_in);
			break;
#endif /* IP_PKTINFO or IP_RECVDSTADDR */
		}
	}
#endif /* S_SPLINT_S */

return 1;
}

/** get packet ecn information */
static uint32_t
msghdr_get_ecn(struct msghdr* msg, int family)
{
#ifndef S_SPLINT_S
	struct cmsghdr* cmsg;
	if(family == AF_INET6) {
		for(cmsg = CMSG_FIRSTHDR(msg); cmsg != NULL;
			cmsg = CMSG_NXTHDR(msg, cmsg)) {
			if(cmsg->cmsg_level == IPPROTO_IPV6 &&
				cmsg->cmsg_type == IPV6_TCLASS &&
				cmsg->cmsg_len != 0) {
				uint8_t* ecn = (uint8_t*)CMSG_DATA(cmsg);
				return *ecn;
			}
		}
		return 0;
	}
	for(cmsg = CMSG_FIRSTHDR(msg); cmsg != NULL;
		cmsg = CMSG_NXTHDR(msg, cmsg)) {
		if(cmsg->cmsg_level == IPPROTO_IP &&
			cmsg->cmsg_type == IP_TOS &&
			cmsg->cmsg_len != 0) {
			uint8_t* ecn = (uint8_t*)CMSG_DATA(cmsg);
			return *ecn;
		}
	}
#endif /* S_SPLINT_S */
	return 0;
}

/** receive packet for DoQ on UDP. get ancillary data for addresses,
 * return false if failed and the callback can stop receiving UDP packets
 * if pkt_continue is false. */
static int
doq_recv(struct comm_point* c, struct doq_pkt_addr* paddr, int* pkt_continue,
	struct ngtcp2_pkt_info* pi)
{
	struct msghdr msg;
	struct iovec iov[1];
	ssize_t rcv;
	union {
		struct cmsghdr hdr;
		char buf[256];
	} ancil;

	msg.msg_name = &paddr->addr;
	msg.msg_namelen = (socklen_t)sizeof(paddr->addr);
	iov[0].iov_base = sldns_buffer_begin(c->doq_socket->pkt_buf);
	iov[0].iov_len = sldns_buffer_remaining(c->doq_socket->pkt_buf);
	msg.msg_iov = iov;
	msg.msg_iovlen = 1;
	msg.msg_control = ancil.buf;
#ifndef S_SPLINT_S
	msg.msg_controllen = sizeof(ancil.buf);
#endif /* S_SPLINT_S */
	msg.msg_flags = 0;

	rcv = recvmsg(c->fd, &msg, MSG_DONTWAIT);
	if(rcv == -1) {
		if(errno != EAGAIN && errno != EINTR
			&& udp_recv_needs_log(errno)) {
			log_err("recvmsg failed for doq: %s", strerror(errno));
		}
		*pkt_continue = 0;
		return 0;
	}

	paddr->addrlen = msg.msg_namelen;
	sldns_buffer_skip(c->doq_socket->pkt_buf, rcv);
	sldns_buffer_flip(c->doq_socket->pkt_buf);
	if(!doq_get_localaddr_cmsg(c, paddr, pkt_continue, &msg))
		return 0;
	pi->ecn = msghdr_get_ecn(&msg, paddr->addr.sockaddr.in.sin_family);
	return 1;
}

/** send the version negotiation for doq. scid and dcid are flipped around
 * to send back to the client. */
static void
doq_send_version_negotiation(struct comm_point* c, struct doq_pkt_addr* paddr,
	const uint8_t* dcid, size_t dcidlen, const uint8_t* scid,
	size_t scidlen)
{
	uint32_t versions[2];
	size_t versions_len = 0;
	ngtcp2_ssize ret;
	uint8_t unused_random;

	/* fill the array with supported versions */
	versions[0] = NGTCP2_PROTO_VER_V1;
	versions_len = 1;
	unused_random = ub_random_max(c->doq_socket->rnd, 256);
	sldns_buffer_clear(c->doq_socket->pkt_buf);
	ret = ngtcp2_pkt_write_version_negotiation(
		sldns_buffer_begin(c->doq_socket->pkt_buf),
		sldns_buffer_capacity(c->doq_socket->pkt_buf), unused_random,
		dcid, dcidlen, scid, scidlen, versions, versions_len);
	if(ret < 0) {
		log_err("ngtcp2_pkt_write_version_negotiation failed: %s",
			ngtcp2_strerror(ret));
		return;
	}
	sldns_buffer_set_position(c->doq_socket->pkt_buf, ret);
	sldns_buffer_flip(c->doq_socket->pkt_buf);
	doq_send_pkt(c, paddr, 0);
}

/** Find the doq_conn object by remote address and dcid */
static struct doq_conn*
doq_conn_find(struct doq_table* table, struct doq_addr_storage* addr,
	socklen_t addrlen, struct doq_addr_storage* localaddr,
	socklen_t localaddrlen, int ifindex, const uint8_t* dcid,
	size_t dcidlen)
{
	struct rbnode_type* node;
	struct doq_conn key;
	memset(&key.node, 0, sizeof(key.node));
	key.node.key = &key;
	memmove(&key.key.paddr.addr, addr, addrlen);
	key.key.paddr.addrlen = addrlen;
	memmove(&key.key.paddr.localaddr, localaddr, localaddrlen);
	key.key.paddr.localaddrlen = localaddrlen;
	key.key.paddr.ifindex = ifindex;
	key.key.dcid = (void*)dcid;
	key.key.dcidlen = dcidlen;
	node = rbtree_search(table->conn_tree, &key);
	if(node)
		return (struct doq_conn*)node->key;
	return NULL;
}

/** find the doq_con by the connection id */
static struct doq_conn*
doq_conn_find_by_id(struct doq_table* table, const uint8_t* dcid,
	size_t dcidlen)
{
	struct doq_conid* conid;
	lock_rw_rdlock(&table->conid_lock);
	conid = doq_conid_find(table, dcid, dcidlen);
	if(conid) {
		/* make a copy of the key */
		struct doq_conn* conn;
		struct doq_conn_key key = conid->key;
		uint8_t cid[NGTCP2_MAX_CIDLEN];
		log_assert(conid->key.dcidlen <= NGTCP2_MAX_CIDLEN);
		memcpy(cid, conid->key.dcid, conid->key.dcidlen);
		key.dcid = cid;
		lock_rw_unlock(&table->conid_lock);

		/* now that the conid lock is released, look up the conn */
		lock_rw_rdlock(&table->lock);
		conn = doq_conn_find(table, &key.paddr.addr,
			key.paddr.addrlen, &key.paddr.localaddr,
			key.paddr.localaddrlen, key.paddr.ifindex, key.dcid,
			key.dcidlen);
		if(!conn) {
			/* The connection got deleted between the conid lookup
			 * and the connection lock grab, it no longer exists,
			 * so return null. */
			lock_rw_unlock(&table->lock);
			return NULL;
		}
		lock_basic_lock(&conn->lock);
		if(conn->is_deleted) {
			lock_rw_unlock(&table->lock);
			lock_basic_unlock(&conn->lock);
			return NULL;
		}
		lock_rw_unlock(&table->lock);
		return conn;
	}
	lock_rw_unlock(&table->conid_lock);
	return NULL;
}

/** Find the doq_conn, by addr or by connection id */
static struct doq_conn*
doq_conn_find_by_addr_or_cid(struct doq_table* table,
	struct doq_pkt_addr* paddr, const uint8_t* dcid, size_t dcidlen)
{
	struct doq_conn* conn;
	lock_rw_rdlock(&table->lock);
	conn = doq_conn_find(table, &paddr->addr, paddr->addrlen,
		&paddr->localaddr, paddr->localaddrlen, paddr->ifindex,
		dcid, dcidlen);
	if(conn && conn->is_deleted) {
		conn = NULL;
	}
	if(conn) {
		lock_basic_lock(&conn->lock);
		lock_rw_unlock(&table->lock);
		verbose(VERB_ALGO, "doq: found connection by address, dcid");
	} else {
		lock_rw_unlock(&table->lock);
		conn = doq_conn_find_by_id(table, dcid, dcidlen);
		if(conn) {
			verbose(VERB_ALGO, "doq: found connection by dcid");
		}
	}
	return conn;
}

/** decode doq packet header, false on handled or failure, true to continue
 * to process the packet */
static int
doq_decode_pkt_header_negotiate(struct comm_point* c,
	struct doq_pkt_addr* paddr, struct doq_conn** conn)
{
#ifdef HAVE_STRUCT_NGTCP2_VERSION_CID
	struct ngtcp2_version_cid vc;
#else
	uint32_t version;
	const uint8_t *dcid, *scid;
	size_t dcidlen, scidlen;
#endif
	int rv;

#ifdef HAVE_STRUCT_NGTCP2_VERSION_CID
	rv = ngtcp2_pkt_decode_version_cid(&vc,
		sldns_buffer_begin(c->doq_socket->pkt_buf),
		sldns_buffer_limit(c->doq_socket->pkt_buf),
		c->doq_socket->sv_scidlen);
#else
	rv = ngtcp2_pkt_decode_version_cid(&version, &dcid, &dcidlen,
		&scid, &scidlen, sldns_buffer_begin(c->doq_socket->pkt_buf),
		sldns_buffer_limit(c->doq_socket->pkt_buf), c->doq_socket->sv_scidlen);
#endif
	if(rv != 0) {
		if(rv == NGTCP2_ERR_VERSION_NEGOTIATION) {
			/* send the version negotiation */
			doq_send_version_negotiation(c, paddr,
#ifdef HAVE_STRUCT_NGTCP2_VERSION_CID
			vc.scid, vc.scidlen, vc.dcid, vc.dcidlen
#else
			scid, scidlen, dcid, dcidlen
#endif
			);
			return 0;
		}
		verbose(VERB_ALGO, "doq: could not decode version "
			"and CID from QUIC packet header: %s",
			ngtcp2_strerror(rv));
		return 0;
	}

	if(verbosity >= VERB_ALGO) {
		verbose(VERB_ALGO, "ngtcp2_pkt_decode_version_cid packet has "
			"QUIC protocol version %u", (unsigned)
#ifdef HAVE_STRUCT_NGTCP2_VERSION_CID
			vc.
#endif
			version
			);
		log_hex("dcid",
#ifdef HAVE_STRUCT_NGTCP2_VERSION_CID
			(void*)vc.dcid, vc.dcidlen
#else
			(void*)dcid, dcidlen
#endif
			);
		log_hex("scid",
#ifdef HAVE_STRUCT_NGTCP2_VERSION_CID
			(void*)vc.scid, vc.scidlen
#else
			(void*)scid, scidlen
#endif
			);
	}
	*conn = doq_conn_find_by_addr_or_cid(c->doq_socket->table, paddr,
#ifdef HAVE_STRUCT_NGTCP2_VERSION_CID
		vc.dcid, vc.dcidlen
#else
		dcid, dcidlen
#endif
		);
	if(*conn)
		(*conn)->doq_socket = c->doq_socket;
	return 1;
}

/** fill cid structure with random data */
static void doq_cid_randfill(struct ngtcp2_cid* cid, size_t datalen,
	struct ub_randstate* rnd)
{
	uint8_t buf[32];
	if(datalen > sizeof(buf))
		datalen = sizeof(buf);
	doq_fill_rand(rnd, buf, datalen);
	ngtcp2_cid_init(cid, buf, datalen);
}

/** send retry packet for doq connection. */
static void
doq_send_retry(struct comm_point* c, struct doq_pkt_addr* paddr,
	struct ngtcp2_pkt_hd* hd)
{
	char host[256], port[32];
	struct ngtcp2_cid scid;
	uint8_t token[NGTCP2_CRYPTO_MAX_RETRY_TOKENLEN];
	ngtcp2_tstamp ts;
	ngtcp2_ssize tokenlen, ret;

	if(!doq_print_addr_port(&paddr->addr, paddr->addrlen, host,
		sizeof(host), port, sizeof(port))) {
		log_err("doq_send_retry failed");
		return;
	}
	verbose(VERB_ALGO, "doq: sending retry packet to %s %s", host, port);

	/* the server chosen source connection ID */
	scid.datalen = c->doq_socket->sv_scidlen;
	doq_cid_randfill(&scid, scid.datalen, c->doq_socket->rnd);

	ts = doq_get_timestamp_nanosec();

	tokenlen = ngtcp2_crypto_generate_retry_token(token,
		c->doq_socket->static_secret, c->doq_socket->static_secret_len,
		hd->version, (void*)&paddr->addr, paddr->addrlen, &scid,
		&hd->dcid, ts);
	if(tokenlen < 0) {
		log_err("ngtcp2_crypto_generate_retry_token failed: %s",
			ngtcp2_strerror(tokenlen));
		return;
	}

	sldns_buffer_clear(c->doq_socket->pkt_buf);
	ret = ngtcp2_crypto_write_retry(sldns_buffer_begin(c->doq_socket->pkt_buf),
		sldns_buffer_capacity(c->doq_socket->pkt_buf), hd->version,
		&hd->scid, &scid, &hd->dcid, token, tokenlen);
	if(ret < 0) {
		log_err("ngtcp2_crypto_write_retry failed: %s",
			ngtcp2_strerror(ret));
		return;
	}
	sldns_buffer_set_position(c->doq_socket->pkt_buf, ret);
	sldns_buffer_flip(c->doq_socket->pkt_buf);
	doq_send_pkt(c, paddr, 0);
}

/** doq send stateless connection close */
static void
doq_send_stateless_connection_close(struct comm_point* c,
	struct doq_pkt_addr* paddr, struct ngtcp2_pkt_hd* hd,
	uint64_t error_code)
{
	ngtcp2_ssize ret;
	sldns_buffer_clear(c->doq_socket->pkt_buf);
	ret = ngtcp2_crypto_write_connection_close(
		sldns_buffer_begin(c->doq_socket->pkt_buf),
		sldns_buffer_capacity(c->doq_socket->pkt_buf), hd->version, &hd->scid,
		&hd->dcid, error_code, NULL, 0);
	if(ret < 0) {
		log_err("ngtcp2_crypto_write_connection_close failed: %s",
			ngtcp2_strerror(ret));
		return;
	}
	sldns_buffer_set_position(c->doq_socket->pkt_buf, ret);
	sldns_buffer_flip(c->doq_socket->pkt_buf);
	doq_send_pkt(c, paddr, 0);
}

/** doq verify retry token, false on failure */
static int
doq_verify_retry_token(struct comm_point* c, struct doq_pkt_addr* paddr,
	struct ngtcp2_cid* ocid, struct ngtcp2_pkt_hd* hd)
{
	char host[256], port[32];
	ngtcp2_tstamp ts;
	if(!doq_print_addr_port(&paddr->addr, paddr->addrlen, host,
		sizeof(host), port, sizeof(port))) {
		log_err("doq_verify_retry_token failed");
		return 0;
	}
	ts = doq_get_timestamp_nanosec();
	verbose(VERB_ALGO, "doq: verifying retry token from %s %s", host,
		port);
	if(ngtcp2_crypto_verify_retry_token(ocid,
#ifdef HAVE_STRUCT_NGTCP2_PKT_HD_TOKENLEN
		hd->token, hd->tokenlen,
#else
		hd->token.base, hd->token.len,
#endif
		c->doq_socket->static_secret,
		c->doq_socket->static_secret_len, hd->version,
		(void*)&paddr->addr, paddr->addrlen, &hd->dcid,
		10*NGTCP2_SECONDS, ts) != 0) {
		verbose(VERB_ALGO, "doq: could not verify retry token "
			"from %s %s", host, port);
		return 0;
	}
	verbose(VERB_ALGO, "doq: verified retry token from %s %s", host, port);
	return 1;
}

/** doq verify token, false on failure */
static int
doq_verify_token(struct comm_point* c, struct doq_pkt_addr* paddr,
	struct ngtcp2_pkt_hd* hd)
{
	char host[256], port[32];
	ngtcp2_tstamp ts;
	if(!doq_print_addr_port(&paddr->addr, paddr->addrlen, host,
		sizeof(host), port, sizeof(port))) {
		log_err("doq_verify_token failed");
		return 0;
	}
	ts = doq_get_timestamp_nanosec();
	verbose(VERB_ALGO, "doq: verifying token from %s %s", host, port);
	if(ngtcp2_crypto_verify_regular_token(
#ifdef HAVE_STRUCT_NGTCP2_PKT_HD_TOKENLEN
		hd->token, hd->tokenlen,
#else
		hd->token.base, hd->token.len,
#endif
		c->doq_socket->static_secret, c->doq_socket->static_secret_len,
		(void*)&paddr->addr, paddr->addrlen, 3600*NGTCP2_SECONDS,
		ts) != 0) {
		verbose(VERB_ALGO, "doq: could not verify token from %s %s",
			host, port);
		return 0;
	}
	verbose(VERB_ALGO, "doq: verified token from %s %s", host, port);
	return 1;
}

/** delete and remove from the lookup tree the doq_conn connection */
static void
doq_delete_connection(struct comm_point* c, struct doq_conn* conn)
{
	struct doq_conn copy;
	uint8_t cid[NGTCP2_MAX_CIDLEN];
	rbnode_type* node;
	if(!conn)
		return;
	/* Copy the key and set it deleted. */
	conn->is_deleted = 1;
	doq_conn_write_disable(conn);
	copy.key = conn->key;
	log_assert(conn->key.dcidlen <= NGTCP2_MAX_CIDLEN);
	memcpy(cid, conn->key.dcid, conn->key.dcidlen);
	copy.key.dcid = cid;
	copy.node.key = &copy;
	lock_basic_unlock(&conn->lock);

	/* Now get the table lock to delete it from the tree */
	lock_rw_wrlock(&c->doq_socket->table->lock);
	node = rbtree_delete(c->doq_socket->table->conn_tree, copy.node.key);
	if(node) {
		conn = (struct doq_conn*)node->key;
		lock_basic_lock(&conn->lock);
		doq_conn_write_list_remove(c->doq_socket->table, conn);
		if(conn->timer.timer_in_list) {
			/* Remove timer from list first, because finding the
			 * rbnode element of the setlist of same timeouts
			 * needs tree lookup. Edit the tree structure after
			 * that lookup. */
			doq_timer_list_remove(c->doq_socket->table,
				&conn->timer);
		}
		if(conn->timer.timer_in_tree)
			doq_timer_tree_remove(c->doq_socket->table,
				&conn->timer);
	}
	lock_rw_unlock(&c->doq_socket->table->lock);
	if(node) {
		lock_basic_unlock(&conn->lock);
		doq_table_quic_size_subtract(c->doq_socket->table,
			sizeof(*conn)+conn->key.dcidlen);
		doq_conn_delete(conn, c->doq_socket->table);
	}
}

/** create and setup a new doq connection, to a new destination, or with
 * a new dcid. It has a new set of streams. It is inserted in the lookup tree.
 * Returns NULL on failure. */
static struct doq_conn*
doq_setup_new_conn(struct comm_point* c, struct doq_pkt_addr* paddr,
	struct ngtcp2_pkt_hd* hd, struct ngtcp2_cid* ocid)
{
	struct doq_conn* conn;
	if(!doq_table_quic_size_available(c->doq_socket->table,
		c->doq_socket->cfg, sizeof(*conn)+hd->dcid.datalen
		+ sizeof(struct doq_stream)
		+ 100 /* estimated input query */
		+ 1200 /* estimated output query */)) {
		verbose(VERB_ALGO, "doq: no mem available for new connection");
		doq_send_stateless_connection_close(c, paddr, hd,
			NGTCP2_CONNECTION_REFUSED);
		return NULL;
	}
	conn = doq_conn_create(c, paddr, hd->dcid.data, hd->dcid.datalen,
		hd->version);
	if(!conn) {
		log_err("doq: could not allocate doq_conn");
		return NULL;
	}
	lock_rw_wrlock(&c->doq_socket->table->lock);
	lock_basic_lock(&conn->lock);
	if(!rbtree_insert(c->doq_socket->table->conn_tree, &conn->node)) {
		lock_rw_unlock(&c->doq_socket->table->lock);
		log_err("doq: duplicate connection");
		/* conn has no entry in writelist, and no timer yet. */
		lock_basic_unlock(&conn->lock);
		doq_conn_delete(conn, c->doq_socket->table);
		return NULL;
	}
	lock_rw_unlock(&c->doq_socket->table->lock);
	doq_table_quic_size_add(c->doq_socket->table,
		sizeof(*conn)+conn->key.dcidlen);
	verbose(VERB_ALGO, "doq: created new connection");

	/* the scid and dcid switch meaning from the accepted client
	 * connection to the server connection. The 'source' and 'destination'
	 * meaning is reversed. */
	if(!doq_conn_setup(conn, hd->scid.data, hd->scid.datalen,
		(ocid?ocid->data:NULL), (ocid?ocid->datalen:0),
#ifdef HAVE_STRUCT_NGTCP2_PKT_HD_TOKENLEN
		hd->token, hd->tokenlen
#else
		hd->token.base, hd->token.len
#endif
		)) {
		log_err("doq: could not set up connection");
		doq_delete_connection(c, conn);
		return NULL;
	}
	return conn;
}

/** perform doq address validation */
static int
doq_address_validation(struct comm_point* c, struct doq_pkt_addr* paddr,
	struct ngtcp2_pkt_hd* hd, struct ngtcp2_cid* ocid,
	struct ngtcp2_cid** pocid)
{
#ifdef HAVE_STRUCT_NGTCP2_PKT_HD_TOKENLEN
	const uint8_t* token = hd->token;
	size_t tokenlen = hd->tokenlen;
#else
	const uint8_t* token = hd->token.base;
	size_t tokenlen = hd->token.len;
#endif
	verbose(VERB_ALGO, "doq stateless address validation");

	if(tokenlen == 0 || token == NULL) {
		doq_send_retry(c, paddr, hd);
		return 0;
	}
	if(token[0] != NGTCP2_CRYPTO_TOKEN_MAGIC_RETRY &&
		hd->dcid.datalen < NGTCP2_MIN_INITIAL_DCIDLEN) {
		doq_send_stateless_connection_close(c, paddr, hd,
			NGTCP2_INVALID_TOKEN);
		return 0;
	}
	if(token[0] == NGTCP2_CRYPTO_TOKEN_MAGIC_RETRY) {
		if(!doq_verify_retry_token(c, paddr, ocid, hd)) {
			doq_send_stateless_connection_close(c, paddr, hd,
				NGTCP2_INVALID_TOKEN);
			return 0;
		}
		*pocid = ocid;
	} else if(token[0] == NGTCP2_CRYPTO_TOKEN_MAGIC_REGULAR) {
		if(!doq_verify_token(c, paddr, hd)) {
			doq_send_retry(c, paddr, hd);
			return 0;
		}
#ifdef HAVE_STRUCT_NGTCP2_PKT_HD_TOKENLEN
		hd->token = NULL;
		hd->tokenlen = 0;
#else
		hd->token.base = NULL;
		hd->token.len = 0;
#endif
	} else {
		verbose(VERB_ALGO, "doq address validation: unrecognised "
			"token in hd.token.base with magic byte 0x%2.2x",
			(int)token[0]);
		if(c->doq_socket->validate_addr) {
			doq_send_retry(c, paddr, hd);
			return 0;
		}
#ifdef HAVE_STRUCT_NGTCP2_PKT_HD_TOKENLEN
		hd->token = NULL;
		hd->tokenlen = 0;
#else
		hd->token.base = NULL;
		hd->token.len = 0;
#endif
	}
	return 1;
}

/** the doq accept, returns false if no further processing of content */
static int
doq_accept(struct comm_point* c, struct doq_pkt_addr* paddr,
	struct doq_conn** conn, struct ngtcp2_pkt_info* pi)
{
	int rv;
	struct ngtcp2_pkt_hd hd;
	struct ngtcp2_cid ocid, *pocid=NULL;
	int err_retry;
	memset(&hd, 0, sizeof(hd));
	rv = ngtcp2_accept(&hd, sldns_buffer_begin(c->doq_socket->pkt_buf),
		sldns_buffer_limit(c->doq_socket->pkt_buf));
	if(rv != 0) {
		if(rv == NGTCP2_ERR_RETRY) {
			doq_send_retry(c, paddr, &hd);
			return 0;
		}
		log_err("doq: initial packet failed, ngtcp2_accept failed: %s",
			ngtcp2_strerror(rv));
		return 0;
	}
	if(c->doq_socket->validate_addr ||
#ifdef HAVE_STRUCT_NGTCP2_PKT_HD_TOKENLEN
		hd.tokenlen
#else
		hd.token.len
#endif
		) {
		if(!doq_address_validation(c, paddr, &hd, &ocid, &pocid))
			return 0;
	}
	*conn = doq_setup_new_conn(c, paddr, &hd, pocid);
	if(!*conn)
		return 0;
	(*conn)->doq_socket = c->doq_socket;
	if(!doq_conn_recv(c, paddr, *conn, pi, &err_retry, NULL)) {
		if(err_retry)
			doq_send_retry(c, paddr, &hd);
		doq_delete_connection(c, *conn);
		*conn = NULL;
		return 0;
	}
	return 1;
}

/** doq pickup a timer to wait for for the worker. If any timer exists. */
static void
doq_pickup_timer(struct comm_point* c)
{
	struct doq_timer* t;
	struct timeval tv;
	int have_time = 0;
	memset(&tv, 0, sizeof(tv));

	lock_rw_wrlock(&c->doq_socket->table->lock);
	RBTREE_FOR(t, struct doq_timer*, c->doq_socket->table->timer_tree) {
		if(t->worker_doq_socket == NULL ||
			t->worker_doq_socket == c->doq_socket) {
			/* pick up this element */
			t->worker_doq_socket = c->doq_socket;
			have_time = 1;
			memcpy(&tv, &t->time, sizeof(tv));
			break;
		}
	}
	lock_rw_unlock(&c->doq_socket->table->lock);

	if(have_time) {
		struct timeval rel;
		timeval_subtract(&rel, &tv, c->doq_socket->now_tv);
		comm_timer_set(c->doq_socket->timer, &rel);
		memcpy(&c->doq_socket->marked_time, &tv,
			sizeof(c->doq_socket->marked_time));
		verbose(VERB_ALGO, "doq pickup timer at %d.%6.6d in %d.%6.6d",
			(int)tv.tv_sec, (int)tv.tv_usec, (int)rel.tv_sec,
			(int)rel.tv_usec);
	} else {
		if(comm_timer_is_set(c->doq_socket->timer))
			comm_timer_disable(c->doq_socket->timer);
		memset(&c->doq_socket->marked_time, 0,
			sizeof(c->doq_socket->marked_time));
		verbose(VERB_ALGO, "doq timer disabled");
	}
}

/** doq done with connection, release locks and setup timer and write */
static void
doq_done_setup_timer_and_write(struct comm_point* c, struct doq_conn* conn)
{
	struct doq_conn copy;
	uint8_t cid[NGTCP2_MAX_CIDLEN];
	rbnode_type* node;
	struct timeval new_tv;
	int write_change = 0, timer_change = 0;

	/* No longer in callbacks, so the pointer to doq_socket is back
	 * to NULL. */
	conn->doq_socket = NULL;

	if(doq_conn_check_timer(conn, &new_tv))
		timer_change = 1;
	if( (conn->write_interest && !conn->on_write_list) ||
		(!conn->write_interest && conn->on_write_list))
		write_change = 1;

	if(!timer_change && !write_change) {
		/* Nothing to do. */
		lock_basic_unlock(&conn->lock);
		return;
	}

	/* The table lock is needed to change the write list and timer tree.
	 * So the connection lock is release and then the connection is
	 * looked up again. */
	copy.key = conn->key;
	log_assert(conn->key.dcidlen <= NGTCP2_MAX_CIDLEN);
	memcpy(cid, conn->key.dcid, conn->key.dcidlen);
	copy.key.dcid = cid;
	copy.node.key = &copy;
	lock_basic_unlock(&conn->lock);

	lock_rw_wrlock(&c->doq_socket->table->lock);
	node = rbtree_search(c->doq_socket->table->conn_tree, copy.node.key);
	if(!node) {
		lock_rw_unlock(&c->doq_socket->table->lock);
		/* Must have been deleted in the mean time. */
		return;
	}
	conn = (struct doq_conn*)node->key;
	lock_basic_lock(&conn->lock);
	if(conn->is_deleted) {
		/* It is deleted now. */
		lock_rw_unlock(&c->doq_socket->table->lock);
		lock_basic_unlock(&conn->lock);
		return;
	}

	if(write_change) {
		/* Edit the write lists, we are holding the table.lock and can
		 * edit the list first,last and also prev,next and on_list
		 * elements in the doq_conn structures. */
		doq_conn_set_write_list(c->doq_socket->table, conn);
	}
	if(timer_change) {
		doq_timer_set(c->doq_socket->table, &conn->timer,
			c->doq_socket, &new_tv);
	}
	lock_rw_unlock(&c->doq_socket->table->lock);
	lock_basic_unlock(&conn->lock);
}

/** doq done with connection callbacks, release locks and setup write */
static void
doq_done_with_conn_cb(struct comm_point* c, struct doq_conn* conn)
{
	struct doq_conn copy;
	uint8_t cid[NGTCP2_MAX_CIDLEN];
	rbnode_type* node;

	/* no longer in callbacks, so the pointer to doq_socket is back
	 * to NULL. */
	conn->doq_socket = NULL;

	if( (conn->write_interest && conn->on_write_list) ||
		(!conn->write_interest && !conn->on_write_list)) {
		/* The connection already has the required write list
		 * status. */
		lock_basic_unlock(&conn->lock);
		return;
	}

	/* To edit the write list of connections we have to hold the table
	 * lock, so we release the connection and then look it up again. */
	copy.key = conn->key;
	log_assert(conn->key.dcidlen <= NGTCP2_MAX_CIDLEN);
	memcpy(cid, conn->key.dcid, conn->key.dcidlen);
	copy.key.dcid = cid;
	copy.node.key = &copy;
	lock_basic_unlock(&conn->lock);

	lock_rw_wrlock(&c->doq_socket->table->lock);
	node = rbtree_search(c->doq_socket->table->conn_tree, copy.node.key);
	if(!node) {
		lock_rw_unlock(&c->doq_socket->table->lock);
		/* must have been deleted in the mean time */
		return;
	}
	conn = (struct doq_conn*)node->key;
	lock_basic_lock(&conn->lock);
	if(conn->is_deleted) {
		/* it is deleted now. */
		lock_rw_unlock(&c->doq_socket->table->lock);
		lock_basic_unlock(&conn->lock);
		return;
	}

	/* edit the write lists, we are holding the table.lock and can
	 * edit the list first,last and also prev,next and on_list elements
	 * in the doq_conn structures. */
	doq_conn_set_write_list(c->doq_socket->table, conn);
	lock_rw_unlock(&c->doq_socket->table->lock);
	lock_basic_unlock(&conn->lock);
}

/** doq count the length of the write list */
static size_t
doq_write_list_length(struct comm_point* c)
{
	size_t count = 0;
	struct doq_conn* conn;
	lock_rw_rdlock(&c->doq_socket->table->lock);
	conn = c->doq_socket->table->write_list_first;
	while(conn) {
		count++;
		conn = conn->write_next;
	}
	lock_rw_unlock(&c->doq_socket->table->lock);
	return count;
}

/** doq pop the first element from the write list to have write events */
static struct doq_conn*
doq_pop_write_conn(struct comm_point* c)
{
	struct doq_conn* conn;
	lock_rw_wrlock(&c->doq_socket->table->lock);
	conn = doq_table_pop_first(c->doq_socket->table);
	while(conn && conn->is_deleted) {
		lock_basic_unlock(&conn->lock);
		conn = doq_table_pop_first(c->doq_socket->table);
	}
	lock_rw_unlock(&c->doq_socket->table->lock);
	if(conn)
		conn->doq_socket = c->doq_socket;
	return conn;
}

/** doq the connection is done with write callbacks, release it. */
static void
doq_done_with_write_cb(struct comm_point* c, struct doq_conn* conn,
	int delete_it)
{
	if(delete_it) {
		doq_delete_connection(c, conn);
		return;
	}
	doq_done_setup_timer_and_write(c, conn);
}

/** see if the doq socket wants to write packets */
static int
doq_socket_want_write(struct comm_point* c)
{
	int want_write = 0;
	if(c->doq_socket->have_blocked_pkt)
		return 1;
	lock_rw_rdlock(&c->doq_socket->table->lock);
	if(c->doq_socket->table->write_list_first)
		want_write = 1;
	lock_rw_unlock(&c->doq_socket->table->lock);
	return want_write;
}

/** enable write event for the doq server socket fd */
static void
doq_socket_write_enable(struct comm_point* c)
{
	verbose(VERB_ALGO, "doq socket want write");
	if(c->doq_socket->event_has_write)
		return;
	comm_point_listen_for_rw(c, 1, 1);
	c->doq_socket->event_has_write = 1;
}

/** disable write event for the doq server socket fd */
static void
doq_socket_write_disable(struct comm_point* c)
{
	verbose(VERB_ALGO, "doq socket want no write");
	if(!c->doq_socket->event_has_write)
		return;
	comm_point_listen_for_rw(c, 1, 0);
	c->doq_socket->event_has_write = 0;
}

/** write blocked packet, if possible. returns false if failed, again. */
static int
doq_write_blocked_pkt(struct comm_point* c)
{
	struct doq_pkt_addr paddr;
	if(!c->doq_socket->have_blocked_pkt)
		return 1;
	c->doq_socket->have_blocked_pkt = 0;
	if(sldns_buffer_limit(c->doq_socket->blocked_pkt) >
		sldns_buffer_remaining(c->doq_socket->pkt_buf))
		return 1; /* impossibly large, drop it.
		impossible since pkt_buf is same size as blocked_pkt buf. */
	sldns_buffer_clear(c->doq_socket->pkt_buf);
	sldns_buffer_write(c->doq_socket->pkt_buf,
		sldns_buffer_begin(c->doq_socket->blocked_pkt),
		sldns_buffer_limit(c->doq_socket->blocked_pkt));
	sldns_buffer_flip(c->doq_socket->pkt_buf);
	memcpy(&paddr, c->doq_socket->blocked_paddr, sizeof(paddr));
	doq_send_pkt(c, &paddr, c->doq_socket->blocked_pkt_pi.ecn);
	if(c->doq_socket->have_blocked_pkt)
		return 0;
	return 1;
}

/** doq find a timer that timeouted and return the conn, locked. */
static struct doq_conn*
doq_timer_timeout_conn(struct doq_server_socket* doq_socket)
{
	struct doq_conn* conn = NULL;
	struct rbnode_type* node;
	lock_rw_wrlock(&doq_socket->table->lock);
	node = rbtree_first(doq_socket->table->timer_tree);
	if(node && node != RBTREE_NULL) {
		struct doq_timer* t = (struct doq_timer*)node;
		conn = t->conn;

		/* If now < timer then no further timeouts in tree. */
		if(timeval_smaller(doq_socket->now_tv, &t->time)) {
			lock_rw_unlock(&doq_socket->table->lock);
			return NULL;
		}

		lock_basic_lock(&conn->lock);
		conn->doq_socket = doq_socket;

		/* Now that the timer is fired, remove it. */
		doq_timer_unset(doq_socket->table, t);
		lock_rw_unlock(&doq_socket->table->lock);
		return conn;
	}
	lock_rw_unlock(&doq_socket->table->lock);
	return NULL;
}

/** doq timer erase the marker that said which timer the worker uses. */
static void
doq_timer_erase_marker(struct doq_server_socket* doq_socket)
{
	struct doq_timer* t;
	lock_rw_wrlock(&doq_socket->table->lock);
	t = doq_timer_find_time(doq_socket->table, &doq_socket->marked_time);
	if(t && t->worker_doq_socket == doq_socket)
		t->worker_doq_socket = NULL;
	lock_rw_unlock(&doq_socket->table->lock);
	memset(&doq_socket->marked_time, 0, sizeof(doq_socket->marked_time));
}

void
doq_timer_cb(void* arg)
{
	struct doq_server_socket* doq_socket = (struct doq_server_socket*)arg;
	struct doq_conn* conn;
	verbose(VERB_ALGO, "doq timer callback");

	doq_timer_erase_marker(doq_socket);

	while((conn = doq_timer_timeout_conn(doq_socket)) != NULL) {
		if(conn->is_deleted ||
#ifdef HAVE_NGTCP2_CONN_IN_CLOSING_PERIOD
			ngtcp2_conn_in_closing_period(conn->conn) ||
#else
			ngtcp2_conn_is_in_closing_period(conn->conn) ||
#endif
#ifdef HAVE_NGTCP2_CONN_IN_DRAINING_PERIOD
			ngtcp2_conn_in_draining_period(conn->conn)
#else
			ngtcp2_conn_is_in_draining_period(conn->conn)
#endif
			) {
			if(verbosity >= VERB_ALGO) {
				char remotestr[256];
				addr_to_str((void*)&conn->key.paddr.addr,
					conn->key.paddr.addrlen, remotestr,
					sizeof(remotestr));
				verbose(VERB_ALGO, "doq conn %s is deleted "
					"after timeout", remotestr);
			}
			doq_delete_connection(doq_socket->cp, conn);
			continue;
		}
		if(!doq_conn_handle_timeout(conn))
			doq_delete_connection(doq_socket->cp, conn);
		else doq_done_setup_timer_and_write(doq_socket->cp, conn);
	}

	if(doq_socket_want_write(doq_socket->cp))
		doq_socket_write_enable(doq_socket->cp);
	else doq_socket_write_disable(doq_socket->cp);
	doq_pickup_timer(doq_socket->cp);
}

void
comm_point_doq_callback(int fd, short event, void* arg)
{
	struct comm_point* c;
	struct doq_pkt_addr paddr;
	int i, pkt_continue, err_drop;
	struct doq_conn* conn;
	struct ngtcp2_pkt_info pi;
	size_t count, num_len;

	c = (struct comm_point*)arg;
	log_assert(c->type == comm_doq);

	log_assert(c && c->doq_socket->pkt_buf && c->fd == fd);
	ub_comm_base_now(c->ev->base);

	/* see if there is a blocked packet, and send that if possible.
	 * do not attempt to read yet, even if possible, that would just
	 * push more answers in reply to those read packets onto the list
	 * of written replies. First attempt to clear the write content out.
	 * That keeps the memory usage from bloating up. */
	if(c->doq_socket->have_blocked_pkt) {
		if(!doq_write_blocked_pkt(c)) {
			/* this write has also blocked, attempt to write
			 * later. Make sure the event listens to write
			 * events. */
			if(!c->doq_socket->event_has_write)
				doq_socket_write_enable(c);
			doq_pickup_timer(c);
			return;
		}
	}

	/* see if there is write interest */
	count = 0;
	num_len = doq_write_list_length(c);
	while((conn = doq_pop_write_conn(c)) != NULL) {
		if(conn->is_deleted ||
#ifdef HAVE_NGTCP2_CONN_IN_CLOSING_PERIOD
			ngtcp2_conn_in_closing_period(conn->conn) ||
#else
			ngtcp2_conn_is_in_closing_period(conn->conn) ||
#endif
#ifdef HAVE_NGTCP2_CONN_IN_DRAINING_PERIOD
			ngtcp2_conn_in_draining_period(conn->conn)
#else
			ngtcp2_conn_is_in_draining_period(conn->conn)
#endif
			) {
			conn->doq_socket = NULL;
			lock_basic_unlock(&conn->lock);
			if(c->doq_socket->have_blocked_pkt) {
				if(!c->doq_socket->event_has_write)
					doq_socket_write_enable(c);
				doq_pickup_timer(c);
				return;
			}
			if(++count > num_len*2)
				break;
			continue;
		}
		if(verbosity >= VERB_ALGO) {
			char remotestr[256];
			addr_to_str((void*)&conn->key.paddr.addr,
				conn->key.paddr.addrlen, remotestr,
				sizeof(remotestr));
			verbose(VERB_ALGO, "doq write connection %s %d",
				remotestr, doq_sockaddr_get_port(
				&conn->key.paddr.addr));
		}
		if(doq_conn_write_streams(c, conn, &err_drop))
			err_drop = 0;
		doq_done_with_write_cb(c, conn, err_drop);
		if(c->doq_socket->have_blocked_pkt) {
			if(!c->doq_socket->event_has_write)
				doq_socket_write_enable(c);
			doq_pickup_timer(c);
			return;
		}
		/* Stop overly long write lists that are created
		 * while we are processing. Do those next time there
		 * is a write callback. Stops long loops, and keeps
		 * fair for other events. */
		if(++count > num_len*2)
			break;
	}

	/* check for data to read */
	if((event&UB_EV_READ)!=0)
	  for(i=0; i<NUM_UDP_PER_SELECT; i++) {
		/* there may be a blocked write packet and if so, stop
		 * reading because the reply cannot get written. The
		 * blocked packet could be written during the conn_recv
		 * handling of replies, or for a connection close. */
		if(c->doq_socket->have_blocked_pkt) {
			if(!c->doq_socket->event_has_write)
				doq_socket_write_enable(c);
			doq_pickup_timer(c);
			return;
		}
		sldns_buffer_clear(c->doq_socket->pkt_buf);
		doq_pkt_addr_init(&paddr);
		log_assert(fd != -1);
		log_assert(sldns_buffer_remaining(c->doq_socket->pkt_buf) > 0);
		if(!doq_recv(c, &paddr, &pkt_continue, &pi)) {
			if(pkt_continue)
				continue;
			break;
		}

		/* handle incoming packet from remote addr to localaddr */
		if(verbosity >= VERB_ALGO) {
			char remotestr[256], localstr[256];
			addr_to_str((void*)&paddr.addr, paddr.addrlen,
				remotestr, sizeof(remotestr));
			addr_to_str((void*)&paddr.localaddr,
				paddr.localaddrlen, localstr,
				sizeof(localstr));
			log_info("incoming doq packet from %s port %d on "
				"%s port %d ifindex %d",
				remotestr, doq_sockaddr_get_port(&paddr.addr),
				localstr,
				doq_sockaddr_get_port(&paddr.localaddr),
				paddr.ifindex);
			log_info("doq_recv length %d ecn 0x%x",
				(int)sldns_buffer_limit(c->doq_socket->pkt_buf),
				(int)pi.ecn);
		}

		if(sldns_buffer_limit(c->doq_socket->pkt_buf) == 0)
			continue;

		conn = NULL;
		if(!doq_decode_pkt_header_negotiate(c, &paddr, &conn))
			continue;
		if(!conn) {
			if(!doq_accept(c, &paddr, &conn, &pi))
				continue;
			if(!doq_conn_write_streams(c, conn, NULL)) {
				doq_delete_connection(c, conn);
				continue;
			}
			doq_done_setup_timer_and_write(c, conn);
			continue;
		}
		if(
#ifdef HAVE_NGTCP2_CONN_IN_CLOSING_PERIOD
			ngtcp2_conn_in_closing_period(conn->conn)
#else
			ngtcp2_conn_is_in_closing_period(conn->conn)
#endif
			) {
			if(!doq_conn_send_close(c, conn)) {
				doq_delete_connection(c, conn);
			} else {
				doq_done_setup_timer_and_write(c, conn);
			}
			continue;
		}
		if(
#ifdef HAVE_NGTCP2_CONN_IN_DRAINING_PERIOD
			ngtcp2_conn_in_draining_period(conn->conn)
#else
			ngtcp2_conn_is_in_draining_period(conn->conn)
#endif
			) {
			doq_done_setup_timer_and_write(c, conn);
			continue;
		}
		if(!doq_conn_recv(c, &paddr, conn, &pi, NULL, &err_drop)) {
			/* The receive failed, and if it also failed to send
			 * a close, drop the connection. That means it is not
			 * in the closing period. */
			if(err_drop) {
				doq_delete_connection(c, conn);
			} else {
				doq_done_setup_timer_and_write(c, conn);
			}
			continue;
		}
		if(!doq_conn_write_streams(c, conn, &err_drop)) {
			if(err_drop) {
				doq_delete_connection(c, conn);
			} else {
				doq_done_setup_timer_and_write(c, conn);
			}
			continue;
		}
		doq_done_setup_timer_and_write(c, conn);
	}

	/* see if we want to have more write events */
	verbose(VERB_ALGO, "doq check write enable");
	if(doq_socket_want_write(c))
		doq_socket_write_enable(c);
	else doq_socket_write_disable(c);
	doq_pickup_timer(c);
}

/** create new doq server socket structure */
static struct doq_server_socket*
doq_server_socket_create(struct doq_table* table, struct ub_randstate* rnd,
	const void* quic_sslctx, struct comm_point* c, struct comm_base* base,
	struct config_file* cfg)
{
	size_t doq_buffer_size = 4096; /* bytes buffer size, for one packet. */
	struct doq_server_socket* doq_socket;
	doq_socket = calloc(1, sizeof(*doq_socket));
	if(!doq_socket) {
		return NULL;
	}
	doq_socket->table = table;
	doq_socket->rnd = rnd;
	doq_socket->validate_addr = 1;
	/* the doq_socket has its own copy of the static secret, as
	 * well as other config values, so that they do not need table.lock */
	doq_socket->static_secret_len = table->static_secret_len;
	doq_socket->static_secret = memdup(table->static_secret,
		table->static_secret_len);
	if(!doq_socket->static_secret) {
		free(doq_socket);
		return NULL;
	}
	doq_socket->ctx = (SSL_CTX*)quic_sslctx;
	doq_socket->idle_timeout = table->idle_timeout;
	doq_socket->sv_scidlen = table->sv_scidlen;
	doq_socket->cp = c;
	doq_socket->pkt_buf = sldns_buffer_new(doq_buffer_size);
	if(!doq_socket->pkt_buf) {
		free(doq_socket->static_secret);
		free(doq_socket);
		return NULL;
	}
	doq_socket->blocked_pkt = sldns_buffer_new(
		sldns_buffer_capacity(doq_socket->pkt_buf));
	if(!doq_socket->pkt_buf) {
		free(doq_socket->static_secret);
		sldns_buffer_free(doq_socket->pkt_buf);
		free(doq_socket);
		return NULL;
	}
	doq_socket->blocked_paddr = calloc(1,
		sizeof(*doq_socket->blocked_paddr));
	if(!doq_socket->blocked_paddr) {
		free(doq_socket->static_secret);
		sldns_buffer_free(doq_socket->pkt_buf);
		sldns_buffer_free(doq_socket->blocked_pkt);
		free(doq_socket);
		return NULL;
	}
	doq_socket->timer = comm_timer_create(base, doq_timer_cb, doq_socket);
	if(!doq_socket->timer) {
		free(doq_socket->static_secret);
		sldns_buffer_free(doq_socket->pkt_buf);
		sldns_buffer_free(doq_socket->blocked_pkt);
		free(doq_socket->blocked_paddr);
		free(doq_socket);
		return NULL;
	}
	memset(&doq_socket->marked_time, 0, sizeof(doq_socket->marked_time));
	comm_base_timept(base, &doq_socket->now_tt, &doq_socket->now_tv);
	doq_socket->cfg = cfg;
	return doq_socket;
}

/** delete doq server socket structure */
static void
doq_server_socket_delete(struct doq_server_socket* doq_socket)
{
	if(!doq_socket)
		return;
	free(doq_socket->static_secret);
#ifndef HAVE_NGTCP2_CRYPTO_QUICTLS_CONFIGURE_SERVER_CONTEXT
	free(doq_socket->quic_method);
#endif
	sldns_buffer_free(doq_socket->pkt_buf);
	sldns_buffer_free(doq_socket->blocked_pkt);
	free(doq_socket->blocked_paddr);
	comm_timer_delete(doq_socket->timer);
	free(doq_socket);
}

/** find repinfo in the doq table */
static struct doq_conn*
doq_lookup_repinfo(struct doq_table* table, struct comm_reply* repinfo)
{
	struct doq_conn* conn;
	struct doq_conn_key key;
	doq_conn_key_from_repinfo(&key, repinfo);
	lock_rw_rdlock(&table->lock);
	conn = doq_conn_find(table, &key.paddr.addr,
		key.paddr.addrlen, &key.paddr.localaddr,
		key.paddr.localaddrlen, key.paddr.ifindex, key.dcid,
		key.dcidlen);
	if(conn) {
		lock_basic_lock(&conn->lock);
		lock_rw_unlock(&table->lock);
		return conn;
	}
	lock_rw_unlock(&table->lock);
	return NULL;
}

/** doq find connection and stream. From inside callbacks from worker. */
static int
doq_lookup_conn_stream(struct comm_reply* repinfo, struct comm_point* c,
	struct doq_conn** conn, struct doq_stream** stream)
{
	log_assert(c->doq_socket);
	if(c->doq_socket->current_conn) {
		*conn = c->doq_socket->current_conn;
	} else {
		*conn = doq_lookup_repinfo(c->doq_socket->table, repinfo);
		if((*conn) && (*conn)->is_deleted) {
			lock_basic_unlock(&(*conn)->lock);
			*conn = NULL;
		}
		if(*conn) {
			(*conn)->doq_socket = c->doq_socket;
		}
	}
	if(!*conn) {
		*stream = NULL;
		return 0;
	}
	*stream = doq_stream_find(*conn, repinfo->doq_streamid);
	if(!*stream) {
		if(!c->doq_socket->current_conn) {
			/* Not inside callbacks, we have our own lock on conn.
			 * Release it. */
			lock_basic_unlock(&(*conn)->lock);
		}
		return 0;
	}
	if((*stream)->is_closed) {
		/* stream is closed, ignore reply or drop */
		if(!c->doq_socket->current_conn) {
			/* Not inside callbacks, we have our own lock on conn.
			 * Release it. */
			lock_basic_unlock(&(*conn)->lock);
		}
		return 0;
	}
	return 1;
}

/** doq send a reply from a comm reply */
static void
doq_socket_send_reply(struct comm_reply* repinfo)
{
	struct doq_conn* conn;
	struct doq_stream* stream;
	log_assert(repinfo->c->type == comm_doq);
	if(!doq_lookup_conn_stream(repinfo, repinfo->c, &conn, &stream)) {
		verbose(VERB_ALGO, "doq: send_reply but %s is gone",
			(conn?"stream":"connection"));
		/* No stream, it may have been closed. */
		/* Drop the reply, it cannot be sent. */
		return;
	}
	if(!doq_stream_send_reply(conn, stream, repinfo->c->buffer))
		doq_stream_close(conn, stream, 1);
	if(!repinfo->c->doq_socket->current_conn) {
		/* Not inside callbacks, we have our own lock on conn.
		 * Release it. */
		doq_done_with_conn_cb(repinfo->c, conn);
		/* since we sent a reply, or closed it, the assumption is
		 * that there is something to write, so enable write event.
		 * It waits until the write event happens to write the
		 * streams with answers, this allows some answers to be
		 * answered before the event loop reaches the doq fd, in
		 * repinfo->c->fd, and that collates answers. That would
		 * not happen if we write doq packets right now. */
		doq_socket_write_enable(repinfo->c);
	}
}

/** doq drop a reply from a comm reply */
static void
doq_socket_drop_reply(struct comm_reply* repinfo)
{
	struct doq_conn* conn;
	struct doq_stream* stream;
	log_assert(repinfo->c->type == comm_doq);
	if(!doq_lookup_conn_stream(repinfo, repinfo->c, &conn, &stream)) {
		verbose(VERB_ALGO, "doq: drop_reply but %s is gone",
			(conn?"stream":"connection"));
		/* The connection or stream is already gone. */
		return;
	}
	doq_stream_close(conn, stream, 1);
	if(!repinfo->c->doq_socket->current_conn) {
		/* Not inside callbacks, we have our own lock on conn.
		 * Release it. */
		doq_done_with_conn_cb(repinfo->c, conn);
		doq_socket_write_enable(repinfo->c);
	}
}
#endif /* HAVE_NGTCP2 */

int adjusted_tcp_timeout(struct comm_point* c)
{
	if(c->tcp_timeout_msec < TCP_QUERY_TIMEOUT_MINIMUM)
		return TCP_QUERY_TIMEOUT_MINIMUM;
	return c->tcp_timeout_msec;
}

/** Use a new tcp handler for new query fd, set to read query */
static void
setup_tcp_handler(struct comm_point* c, int fd, int cur, int max)
{
	int handler_usage;
	log_assert(c->type == comm_tcp || c->type == comm_http);
	log_assert(c->fd == -1);
	sldns_buffer_clear(c->buffer);
#ifdef USE_DNSCRYPT
	if (c->dnscrypt)
		sldns_buffer_clear(c->dnscrypt_buffer);
#endif
	c->tcp_is_reading = 1;
	c->tcp_byte_count = 0;
	c->tcp_keepalive = 0;
	/* if more than half the tcp handlers are in use, use a shorter
	 * timeout for this TCP connection, we need to make space for
	 * other connections to be able to get attention */
	/* If > 50% TCP handler structures in use, set timeout to 1/100th
	 * 	configured value.
	 * If > 65%TCP handler structures in use, set to 1/500th configured
	 * 	value.
	 * If > 80% TCP handler structures in use, set to 0.
	 *
	 * If the timeout to use falls below 200 milliseconds, an actual
	 * timeout of 200ms is used.
	 */
	handler_usage = (cur * 100) / max;
	if(handler_usage > 50 && handler_usage <= 65)
		c->tcp_timeout_msec /= 100;
	else if (handler_usage > 65 && handler_usage <= 80)
		c->tcp_timeout_msec /= 500;
	else if (handler_usage > 80)
		c->tcp_timeout_msec = 0;
	comm_point_start_listening(c, fd, adjusted_tcp_timeout(c));
}

void comm_base_handle_slow_accept(int ATTR_UNUSED(fd),
	short ATTR_UNUSED(event), void* arg)
{
	struct comm_base* b = (struct comm_base*)arg;
	/* timeout for the slow accept, re-enable accepts again */
	if(b->start_accept) {
		verbose(VERB_ALGO, "wait is over, slow accept disabled");
		fptr_ok(fptr_whitelist_start_accept(b->start_accept));
		(*b->start_accept)(b->cb_arg);
		b->eb->slow_accept_enabled = 0;
	}
}

int comm_point_perform_accept(struct comm_point* c,
	struct sockaddr_storage* addr, socklen_t* addrlen)
{
	int new_fd;
	*addrlen = (socklen_t)sizeof(*addr);
#ifndef HAVE_ACCEPT4
	new_fd = accept(c->fd, (struct sockaddr*)addr, addrlen);
#else
	/* SOCK_NONBLOCK saves extra calls to fcntl for the same result */
	new_fd = accept4(c->fd, (struct sockaddr*)addr, addrlen, SOCK_NONBLOCK);
#endif
	if(new_fd == -1) {
#ifndef USE_WINSOCK
		/* EINTR is signal interrupt. others are closed connection. */
		if(	errno == EINTR || errno == EAGAIN
#ifdef EWOULDBLOCK
			|| errno == EWOULDBLOCK
#endif
#ifdef ECONNABORTED
			|| errno == ECONNABORTED
#endif
#ifdef EPROTO
			|| errno == EPROTO
#endif /* EPROTO */
			)
			return -1;
#if defined(ENFILE) && defined(EMFILE)
		if(errno == ENFILE || errno == EMFILE) {
			/* out of file descriptors, likely outside of our
			 * control. stop accept() calls for some time */
			if(c->ev->base->stop_accept) {
				struct comm_base* b = c->ev->base;
				struct timeval tv;
				verbose(VERB_ALGO, "out of file descriptors: "
					"slow accept");
				ub_comm_base_now(b);
				if(b->eb->last_slow_log+SLOW_LOG_TIME <=
					b->eb->secs) {
					b->eb->last_slow_log = b->eb->secs;
					verbose(VERB_OPS, "accept failed, "
						"slow down accept for %d "
						"msec: %s",
						NETEVENT_SLOW_ACCEPT_TIME,
						sock_strerror(errno));
				}
				b->eb->slow_accept_enabled = 1;
				fptr_ok(fptr_whitelist_stop_accept(
					b->stop_accept));
				(*b->stop_accept)(b->cb_arg);
				/* set timeout, no mallocs */
				tv.tv_sec = NETEVENT_SLOW_ACCEPT_TIME/1000;
				tv.tv_usec = (NETEVENT_SLOW_ACCEPT_TIME%1000)*1000;
				b->eb->slow_accept = ub_event_new(b->eb->base,
					-1, UB_EV_TIMEOUT,
					comm_base_handle_slow_accept, b);
				if(b->eb->slow_accept == NULL) {
					/* we do not want to log here, because
					 * that would spam the logfiles.
					 * error: "event_base_set failed." */
				}
				else if(ub_event_add(b->eb->slow_accept, &tv)
					!= 0) {
					/* we do not want to log here,
					 * error: "event_add failed." */
				}
			} else {
				log_err("accept, with no slow down, "
					"failed: %s", sock_strerror(errno));
			}
			return -1;
		}
#endif
#else /* USE_WINSOCK */
		if(WSAGetLastError() == WSAEINPROGRESS ||
			WSAGetLastError() == WSAECONNRESET)
			return -1;
		if(WSAGetLastError() == WSAEWOULDBLOCK) {
			ub_winsock_tcp_wouldblock(c->ev->ev, UB_EV_READ);
			return -1;
		}
#endif
		log_err_addr("accept failed", sock_strerror(errno), addr,
			*addrlen);
		return -1;
	}
	if(c->tcp_conn_limit && c->type == comm_tcp_accept) {
		c->tcl_addr = tcl_addr_lookup(c->tcp_conn_limit, addr, *addrlen);
		if(!tcl_new_connection(c->tcl_addr)) {
			if(verbosity >= 3)
				log_err_addr("accept rejected",
				"connection limit exceeded", addr, *addrlen);
			sock_close(new_fd);
			return -1;
		}
	}
#ifndef HAVE_ACCEPT4
	fd_set_nonblock(new_fd);
#endif
	return new_fd;
}

#ifdef USE_WINSOCK
static long win_bio_cb(BIO *b, int oper, const char* ATTR_UNUSED(argp),
#ifdef HAVE_BIO_SET_CALLBACK_EX
	size_t ATTR_UNUSED(len),
#endif
        int ATTR_UNUSED(argi), long argl,
#ifndef HAVE_BIO_SET_CALLBACK_EX
	long retvalue
#else
	int retvalue, size_t* ATTR_UNUSED(processed)
#endif
	)
{
	int wsa_err = WSAGetLastError(); /* store errcode before it is gone */
	verbose(VERB_ALGO, "bio_cb %d, %s %s %s", oper,
		(oper&BIO_CB_RETURN)?"return":"before",
		(oper&BIO_CB_READ)?"read":((oper&BIO_CB_WRITE)?"write":"other"),
		wsa_err==WSAEWOULDBLOCK?"wsawb":"");
	/* on windows, check if previous operation caused EWOULDBLOCK */
	if( (oper == (BIO_CB_READ|BIO_CB_RETURN) && argl == 0) ||
		(oper == (BIO_CB_GETS|BIO_CB_RETURN) && argl == 0)) {
		if(wsa_err == WSAEWOULDBLOCK)
			ub_winsock_tcp_wouldblock((struct ub_event*)
				BIO_get_callback_arg(b), UB_EV_READ);
	}
	if( (oper == (BIO_CB_WRITE|BIO_CB_RETURN) && argl == 0) ||
		(oper == (BIO_CB_PUTS|BIO_CB_RETURN) && argl == 0)) {
		if(wsa_err == WSAEWOULDBLOCK)
			ub_winsock_tcp_wouldblock((struct ub_event*)
				BIO_get_callback_arg(b), UB_EV_WRITE);
	}
	/* return original return value */
	return retvalue;
}

/** set win bio callbacks for nonblocking operations */
void
comm_point_tcp_win_bio_cb(struct comm_point* c, void* thessl)
{
	SSL* ssl = (SSL*)thessl;
	/* set them both just in case, but usually they are the same BIO */
#ifdef HAVE_BIO_SET_CALLBACK_EX
	BIO_set_callback_ex(SSL_get_rbio(ssl), &win_bio_cb);
#else
	BIO_set_callback(SSL_get_rbio(ssl), &win_bio_cb);
#endif
	BIO_set_callback_arg(SSL_get_rbio(ssl), (char*)c->ev->ev);
#ifdef HAVE_BIO_SET_CALLBACK_EX
	BIO_set_callback_ex(SSL_get_wbio(ssl), &win_bio_cb);
#else
	BIO_set_callback(SSL_get_wbio(ssl), &win_bio_cb);
#endif
	BIO_set_callback_arg(SSL_get_wbio(ssl), (char*)c->ev->ev);
}
#endif

#ifdef HAVE_NGHTTP2
/** Create http2 session server.  Per connection, after TCP accepted.*/
static int http2_session_server_create(struct http2_session* h2_session)
{
	log_assert(h2_session->callbacks);
	h2_session->is_drop = 0;
	if(nghttp2_session_server_new(&h2_session->session,
			h2_session->callbacks,
		h2_session) == NGHTTP2_ERR_NOMEM) {
		log_err("failed to create nghttp2 session server");
		return 0;
	}

	return 1;
}

/** Submit http2 setting to session. Once per session. */
static int http2_submit_settings(struct http2_session* h2_session)
{
	int ret;
	nghttp2_settings_entry settings[1] = {
		{NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS,
		 h2_session->c->http2_max_streams}};

	ret = nghttp2_submit_settings(h2_session->session, NGHTTP2_FLAG_NONE,
		settings, 1);
	if(ret) {
		verbose(VERB_QUERY, "http2: submit_settings failed, "
			"error: %s", nghttp2_strerror(ret));
		return 0;
	}
	return 1;
}
#endif /* HAVE_NGHTTP2 */

#ifdef HAVE_NGHTTP2
/** Delete http2 stream. After session delete or stream close callback */
static void http2_stream_delete(struct http2_session* h2_session,
	struct http2_stream* h2_stream)
{
	if(h2_stream->mesh_state) {
		mesh_state_remove_reply(h2_stream->mesh, h2_stream->mesh_state,
			h2_session->c);
		h2_stream->mesh_state = NULL;
	}
	http2_req_stream_clear(h2_stream);
	free(h2_stream);
}
#endif /* HAVE_NGHTTP2 */

/** delete http2 session server. After closing connection. */
static void http2_session_server_delete(struct http2_session* h2_session)
{
#ifdef HAVE_NGHTTP2
	struct http2_stream* h2_stream, *next;
	nghttp2_session_del(h2_session->session); /* NULL input is fine */
	h2_session->session = NULL;
	for(h2_stream = h2_session->first_stream; h2_stream;) {
		next = h2_stream->next;
		http2_stream_delete(h2_session, h2_stream);
		h2_stream = next;
	}
	h2_session->first_stream = NULL;
	h2_session->is_drop = 0;
	h2_session->postpone_drop = 0;
	h2_session->c->h2_stream = NULL;
#endif
	(void)h2_session;
}

void
comm_point_tcp_accept_callback(int fd, short event, void* arg)
{
	struct comm_point* c = (struct comm_point*)arg, *c_hdl;
	int new_fd;
	log_assert(c->type == comm_tcp_accept);
	if(!(event & UB_EV_READ)) {
		log_info("ignoring tcp accept event %d", (int)event);
		return;
	}
	ub_comm_base_now(c->ev->base);
	/* find free tcp handler. */
	if(!c->tcp_free) {
		log_warn("accepted too many tcp, connections full");
		return;
	}
	/* accept incoming connection. */
	c_hdl = c->tcp_free;
	/* clear leftover flags from previous use, and then set the
	 * correct event base for the event structure for libevent */
	ub_event_free(c_hdl->ev->ev);
	c_hdl->ev->ev = NULL;
	if((c_hdl->type == comm_tcp && c_hdl->tcp_req_info) ||
		c_hdl->type == comm_local || c_hdl->type == comm_raw)
		c_hdl->tcp_do_toggle_rw = 0;
	else	c_hdl->tcp_do_toggle_rw = 1;

	if(c_hdl->type == comm_http) {
#ifdef HAVE_NGHTTP2
		if(!c_hdl->h2_session ||
			!http2_session_server_create(c_hdl->h2_session)) {
			log_warn("failed to create nghttp2");
			return;
		}
		if(!c_hdl->h2_session ||
			!http2_submit_settings(c_hdl->h2_session)) {
			log_warn("failed to submit http2 settings");
			if(c_hdl->h2_session)
				http2_session_server_delete(c_hdl->h2_session);
			return;
		}
		if(!c->ssl) {
			c_hdl->tcp_do_toggle_rw = 0;
			c_hdl->use_h2 = 1;
		}
#endif
		c_hdl->ev->ev = ub_event_new(c_hdl->ev->base->eb->base, -1,
			UB_EV_PERSIST | UB_EV_READ | UB_EV_TIMEOUT,
			comm_point_http_handle_callback, c_hdl);
	} else {
		c_hdl->ev->ev = ub_event_new(c_hdl->ev->base->eb->base, -1,
			UB_EV_PERSIST | UB_EV_READ | UB_EV_TIMEOUT,
			comm_point_tcp_handle_callback, c_hdl);
	}
	if(!c_hdl->ev->ev) {
		log_warn("could not ub_event_new, dropped tcp");
#ifdef HAVE_NGHTTP2
		if(c_hdl->type == comm_http && c_hdl->h2_session)
			http2_session_server_delete(c_hdl->h2_session);
#endif
		return;
	}
	log_assert(fd != -1);
	(void)fd;
	new_fd = comm_point_perform_accept(c, &c_hdl->repinfo.remote_addr,
		&c_hdl->repinfo.remote_addrlen);
	if(new_fd == -1) {
#ifdef HAVE_NGHTTP2
		if(c_hdl->type == comm_http && c_hdl->h2_session)
			http2_session_server_delete(c_hdl->h2_session);
#endif
		return;
	}
	/* Copy remote_address to client_address.
	 * Simplest way/time for streams to do that. */
	c_hdl->repinfo.client_addrlen = c_hdl->repinfo.remote_addrlen;
	memmove(&c_hdl->repinfo.client_addr,
		&c_hdl->repinfo.remote_addr,
		c_hdl->repinfo.remote_addrlen);
	if(c->ssl) {
		c_hdl->ssl = incoming_ssl_fd(c->ssl, new_fd);
		if(!c_hdl->ssl) {
			c_hdl->fd = new_fd;
			comm_point_close(c_hdl);
			return;
		}
		c_hdl->ssl_shake_state = comm_ssl_shake_read;
#ifdef USE_WINSOCK
		comm_point_tcp_win_bio_cb(c_hdl, c_hdl->ssl);
#endif
	}

	/* grab the tcp handler buffers */
	c->cur_tcp_count++;
	c->tcp_free = c_hdl->tcp_free;
	c_hdl->tcp_free = NULL;
	if(!c->tcp_free) {
		/* stop accepting incoming queries for now. */
		comm_point_stop_listening(c);
	}
	setup_tcp_handler(c_hdl, new_fd, c->cur_tcp_count, c->max_tcp_count);
}

/** Make tcp handler free for next assignment */
static void
reclaim_tcp_handler(struct comm_point* c)
{
	log_assert(c->type == comm_tcp);
	if(c->ssl) {
#ifdef HAVE_SSL
		SSL_shutdown(c->ssl);
		SSL_free(c->ssl);
		c->ssl = NULL;
#endif
	}
	comm_point_close(c);
	if(c->tcp_parent) {
		if(c != c->tcp_parent->tcp_free) {
			c->tcp_parent->cur_tcp_count--;
			c->tcp_free = c->tcp_parent->tcp_free;
			c->tcp_parent->tcp_free = c;
		}
		if(!c->tcp_free) {
			/* re-enable listening on accept socket */
			comm_point_start_listening(c->tcp_parent, -1, -1);
		}
	}
	c->tcp_more_read_again = NULL;
	c->tcp_more_write_again = NULL;
	c->tcp_byte_count = 0;
	c->pp2_header_state = pp2_header_none;
	sldns_buffer_clear(c->buffer);
}

/** do the callback when writing is done */
static void
tcp_callback_writer(struct comm_point* c)
{
	log_assert(c->type == comm_tcp);
	if(!c->tcp_write_and_read) {
		sldns_buffer_clear(c->buffer);
		c->tcp_byte_count = 0;
	}
	if(c->tcp_do_toggle_rw)
		c->tcp_is_reading = 1;
	/* switch from listening(write) to listening(read) */
	if(c->tcp_req_info) {
		tcp_req_info_handle_writedone(c->tcp_req_info);
	} else {
		comm_point_stop_listening(c);
		if(c->tcp_write_and_read) {
			fptr_ok(fptr_whitelist_comm_point(c->callback));
			if( (*c->callback)(c, c->cb_arg, NETEVENT_PKT_WRITTEN,
				&c->repinfo) ) {
				comm_point_start_listening(c, -1,
					adjusted_tcp_timeout(c));
			}
		} else {
			comm_point_start_listening(c, -1,
					adjusted_tcp_timeout(c));
		}
	}
}

/** do the callback when reading is done */
static void
tcp_callback_reader(struct comm_point* c)
{
	log_assert(c->type == comm_tcp || c->type == comm_local);
	sldns_buffer_flip(c->buffer);
	if(c->tcp_do_toggle_rw)
		c->tcp_is_reading = 0;
	c->tcp_byte_count = 0;
	if(c->tcp_req_info) {
		tcp_req_info_handle_readdone(c->tcp_req_info);
	} else {
		if(c->type == comm_tcp)
			comm_point_stop_listening(c);
		fptr_ok(fptr_whitelist_comm_point(c->callback));
		if( (*c->callback)(c, c->cb_arg, NETEVENT_NOERROR, &c->repinfo) ) {
			comm_point_start_listening(c, -1,
					adjusted_tcp_timeout(c));
		}
	}
}

#ifdef HAVE_SSL
/** true if the ssl handshake error has to be squelched from the logs */
int
squelch_err_ssl_handshake(unsigned long err)
{
	if(verbosity >= VERB_QUERY)
		return 0; /* only squelch on low verbosity */
	if(ERR_GET_LIB(err) == ERR_LIB_SSL &&
		(ERR_GET_REASON(err) == SSL_R_HTTPS_PROXY_REQUEST ||
		 ERR_GET_REASON(err) == SSL_R_HTTP_REQUEST ||
		 ERR_GET_REASON(err) == SSL_R_WRONG_VERSION_NUMBER ||
		 ERR_GET_REASON(err) == SSL_R_SSLV3_ALERT_BAD_CERTIFICATE
#ifdef SSL_F_TLS_POST_PROCESS_CLIENT_HELLO
		 || ERR_GET_REASON(err) == SSL_R_NO_SHARED_CIPHER
#endif
#ifdef SSL_F_TLS_EARLY_POST_PROCESS_CLIENT_HELLO
		 || ERR_GET_REASON(err) == SSL_R_UNKNOWN_PROTOCOL
		 || ERR_GET_REASON(err) == SSL_R_UNSUPPORTED_PROTOCOL
#  ifdef SSL_R_VERSION_TOO_LOW
		 || ERR_GET_REASON(err) == SSL_R_VERSION_TOO_LOW
#  endif
#endif
		))
		return 1;
	return 0;
}
#endif /* HAVE_SSL */

/** continue ssl handshake */
#ifdef HAVE_SSL
static int
ssl_handshake(struct comm_point* c)
{
	int r;
	if(c->ssl_shake_state == comm_ssl_shake_hs_read) {
		/* read condition satisfied back to writing */
		comm_point_listen_for_rw(c, 0, 1);
		c->ssl_shake_state = comm_ssl_shake_none;
		return 1;
	}
	if(c->ssl_shake_state == comm_ssl_shake_hs_write) {
		/* write condition satisfied, back to reading */
		comm_point_listen_for_rw(c, 1, 0);
		c->ssl_shake_state = comm_ssl_shake_none;
		return 1;
	}

	ERR_clear_error();
	r = SSL_do_handshake(c->ssl);
	if(r != 1) {
		int want = SSL_get_error(c->ssl, r);
		if(want == SSL_ERROR_WANT_READ) {
			if(c->ssl_shake_state == comm_ssl_shake_read)
				return 1;
			c->ssl_shake_state = comm_ssl_shake_read;
			comm_point_listen_for_rw(c, 1, 0);
			return 1;
		} else if(want == SSL_ERROR_WANT_WRITE) {
			if(c->ssl_shake_state == comm_ssl_shake_write)
				return 1;
			c->ssl_shake_state = comm_ssl_shake_write;
			comm_point_listen_for_rw(c, 0, 1);
			return 1;
		} else if(r == 0) {
			return 0; /* closed */
		} else if(want == SSL_ERROR_SYSCALL) {
			/* SYSCALL and errno==0 means closed uncleanly */
#ifdef EPIPE
			if(errno == EPIPE && verbosity < 2)
				return 0; /* silence 'broken pipe' */
#endif
#ifdef ECONNRESET
			if(errno == ECONNRESET && verbosity < 2)
				return 0; /* silence reset by peer */
#endif
			if(!tcp_connect_errno_needs_log(
				(struct sockaddr*)&c->repinfo.remote_addr,
				c->repinfo.remote_addrlen))
				return 0; /* silence connect failures that
				show up because after connect this is the
				first system call that accesses the socket */
			if(errno != 0)
				log_err("SSL_handshake syscall: %s",
					strerror(errno));
			return 0;
		} else {
			unsigned long err = ERR_get_error();
			if(!squelch_err_ssl_handshake(err)) {
				long vr;
				log_crypto_err_io_code("ssl handshake failed",
					want, err);
				if((vr=SSL_get_verify_result(c->ssl)) != 0)
					log_err("ssl handshake cert error: %s",
						X509_verify_cert_error_string(
						vr));
				log_addr(VERB_OPS, "ssl handshake failed",
					&c->repinfo.remote_addr,
					c->repinfo.remote_addrlen);
			}
			return 0;
		}
	}
	/* this is where peer verification could take place */
	if((SSL_get_verify_mode(c->ssl)&SSL_VERIFY_PEER)) {
		/* verification */
		if(SSL_get_verify_result(c->ssl) == X509_V_OK) {
#ifdef HAVE_SSL_GET1_PEER_CERTIFICATE
			X509* x = SSL_get1_peer_certificate(c->ssl);
#else
			X509* x = SSL_get_peer_certificate(c->ssl);
#endif
			if(!x) {
				log_addr(VERB_ALGO, "SSL connection failed: "
					"no certificate",
					&c->repinfo.remote_addr,
					c->repinfo.remote_addrlen);
				return 0;
			}
			log_cert(VERB_ALGO, "peer certificate", x);
#ifdef HAVE_SSL_GET0_PEERNAME
			if(SSL_get0_peername(c->ssl)) {
				char buf[255];
				snprintf(buf, sizeof(buf), "SSL connection "
					"to %s authenticated",
					SSL_get0_peername(c->ssl));
				log_addr(VERB_ALGO, buf, &c->repinfo.remote_addr,
					c->repinfo.remote_addrlen);
			} else {
#endif
				log_addr(VERB_ALGO, "SSL connection "
					"authenticated", &c->repinfo.remote_addr,
					c->repinfo.remote_addrlen);
#ifdef HAVE_SSL_GET0_PEERNAME
			}
#endif
			X509_free(x);
		} else {
#ifdef HAVE_SSL_GET1_PEER_CERTIFICATE
			X509* x = SSL_get1_peer_certificate(c->ssl);
#else
			X509* x = SSL_get_peer_certificate(c->ssl);
#endif
			if(x) {
				log_cert(VERB_ALGO, "peer certificate", x);
				X509_free(x);
			}
			log_addr(VERB_ALGO, "SSL connection failed: "
				"failed to authenticate",
				&c->repinfo.remote_addr,
				c->repinfo.remote_addrlen);
			return 0;
		}
	} else {
		/* unauthenticated, the verify peer flag was not set
		 * in c->ssl when the ssl object was created from ssl_ctx */
		log_addr(VERB_ALGO, "SSL connection", &c->repinfo.remote_addr,
			c->repinfo.remote_addrlen);
	}

#ifdef HAVE_SSL_GET0_ALPN_SELECTED
	/* check if http2 use is negotiated */
	if(c->type == comm_http && c->h2_session) {
		const unsigned char *alpn;
		unsigned int alpnlen = 0;
		SSL_get0_alpn_selected(c->ssl, &alpn, &alpnlen);
		if(alpnlen == 2 && memcmp("h2", alpn, 2) == 0) {
			/* connection upgraded to HTTP2 */
			c->tcp_do_toggle_rw = 0;
			c->use_h2 = 1;
		} else {
			verbose(VERB_ALGO, "client doesn't support HTTP/2");
			return 0;
		}
	}
#endif

	/* setup listen rw correctly */
	if(c->tcp_is_reading) {
		if(c->ssl_shake_state != comm_ssl_shake_read)
			comm_point_listen_for_rw(c, 1, 0);
	} else {
		comm_point_listen_for_rw(c, 0, 1);
	}
	c->ssl_shake_state = comm_ssl_shake_none;
	return 1;
}
#endif /* HAVE_SSL */

/** ssl read callback on TCP */
static int
ssl_handle_read(struct comm_point* c)
{
#ifdef HAVE_SSL
	int r;
	if(c->ssl_shake_state != comm_ssl_shake_none) {
		if(!ssl_handshake(c))
			return 0;
		if(c->ssl_shake_state != comm_ssl_shake_none)
			return 1;
	}
	if(c->pp2_enabled && c->pp2_header_state != pp2_header_done) {
		struct pp2_header* header = NULL;
		size_t want_read_size = 0;
		size_t current_read_size = 0;
		if(c->pp2_header_state == pp2_header_none) {
			want_read_size = PP2_HEADER_SIZE;
			if(sldns_buffer_remaining(c->buffer)<want_read_size) {
				log_err_addr("proxy_protocol: not enough "
					"buffer size to read PROXYv2 header", "",
					&c->repinfo.remote_addr,
					c->repinfo.remote_addrlen);
				return 0;
			}
			verbose(VERB_ALGO, "proxy_protocol: reading fixed "
				"part of PROXYv2 header (len %lu)",
				(unsigned long)want_read_size);
			current_read_size = want_read_size;
			if(c->tcp_byte_count < current_read_size) {
				ERR_clear_error();
				if((r=SSL_read(c->ssl, (void*)sldns_buffer_at(
					c->buffer, c->tcp_byte_count),
					current_read_size -
					c->tcp_byte_count)) <= 0) {
					int want = SSL_get_error(c->ssl, r);
					if(want == SSL_ERROR_ZERO_RETURN) {
						if(c->tcp_req_info)
							return tcp_req_info_handle_read_close(c->tcp_req_info);
						return 0; /* shutdown, closed */
					} else if(want == SSL_ERROR_WANT_READ) {
#ifdef USE_WINSOCK
						ub_winsock_tcp_wouldblock(c->ev->ev, UB_EV_READ);
#endif
						return 1; /* read more later */
					} else if(want == SSL_ERROR_WANT_WRITE) {
						c->ssl_shake_state = comm_ssl_shake_hs_write;
						comm_point_listen_for_rw(c, 0, 1);
						return 1;
					} else if(want == SSL_ERROR_SYSCALL) {
#ifdef ECONNRESET
						if(errno == ECONNRESET && verbosity < 2)
							return 0; /* silence reset by peer */
#endif
						if(errno != 0)
							log_err("SSL_read syscall: %s",
								strerror(errno));
						return 0;
					}
					log_crypto_err_io("could not SSL_read",
						want);
					return 0;
				}
				c->tcp_byte_count += r;
				sldns_buffer_skip(c->buffer, r);
				if(c->tcp_byte_count != current_read_size) return 1;
				c->pp2_header_state = pp2_header_init;
			}
		}
		if(c->pp2_header_state == pp2_header_init) {
			int err;
			err = pp2_read_header(
				sldns_buffer_begin(c->buffer),
				sldns_buffer_limit(c->buffer));
			if(err) {
				log_err("proxy_protocol: could not parse "
					"PROXYv2 header (%s)",
					pp_lookup_error(err));
				return 0;
			}
			header = (struct pp2_header*)sldns_buffer_begin(c->buffer);
			want_read_size = ntohs(header->len);
			if(sldns_buffer_limit(c->buffer) <
				PP2_HEADER_SIZE + want_read_size) {
				log_err_addr("proxy_protocol: not enough "
					"buffer size to read PROXYv2 header", "",
					&c->repinfo.remote_addr,
					c->repinfo.remote_addrlen);
				return 0;
			}
			verbose(VERB_ALGO, "proxy_protocol: reading variable "
				"part of PROXYv2 header (len %lu)",
				(unsigned long)want_read_size);
			current_read_size = PP2_HEADER_SIZE + want_read_size;
			if(want_read_size == 0) {
				/* nothing more to read; header is complete */
				c->pp2_header_state = pp2_header_done;
			} else if(c->tcp_byte_count < current_read_size) {
				ERR_clear_error();
				if((r=SSL_read(c->ssl, (void*)sldns_buffer_at(
					c->buffer, c->tcp_byte_count),
					current_read_size -
					c->tcp_byte_count)) <= 0) {
					int want = SSL_get_error(c->ssl, r);
					if(want == SSL_ERROR_ZERO_RETURN) {
						if(c->tcp_req_info)
							return tcp_req_info_handle_read_close(c->tcp_req_info);
						return 0; /* shutdown, closed */
					} else if(want == SSL_ERROR_WANT_READ) {
#ifdef USE_WINSOCK
						ub_winsock_tcp_wouldblock(c->ev->ev, UB_EV_READ);
#endif
						return 1; /* read more later */
					} else if(want == SSL_ERROR_WANT_WRITE) {
						c->ssl_shake_state = comm_ssl_shake_hs_write;
						comm_point_listen_for_rw(c, 0, 1);
						return 1;
					} else if(want == SSL_ERROR_SYSCALL) {
#ifdef ECONNRESET
						if(errno == ECONNRESET && verbosity < 2)
							return 0; /* silence reset by peer */
#endif
						if(errno != 0)
							log_err("SSL_read syscall: %s",
								strerror(errno));
						return 0;
					}
					log_crypto_err_io("could not SSL_read",
						want);
					return 0;
				}
				c->tcp_byte_count += r;
				sldns_buffer_skip(c->buffer, r);
				if(c->tcp_byte_count != current_read_size) return 1;
				c->pp2_header_state = pp2_header_done;
			}
		}
		if(c->pp2_header_state != pp2_header_done || !header) {
			log_err_addr("proxy_protocol: wrong state for the "
				"PROXYv2 header", "", &c->repinfo.remote_addr,
				c->repinfo.remote_addrlen);
			return 0;
		}
		sldns_buffer_flip(c->buffer);
		if(!consume_pp2_header(c->buffer, &c->repinfo, 1)) {
			log_err_addr("proxy_protocol: could not consume "
				"PROXYv2 header", "", &c->repinfo.remote_addr,
				c->repinfo.remote_addrlen);
			return 0;
		}
		verbose(VERB_ALGO, "proxy_protocol: successful read of "
			"PROXYv2 header");
		/* Clear and reset the buffer to read the following
		 * DNS packet(s). */
		sldns_buffer_clear(c->buffer);
		c->tcp_byte_count = 0;
		return 1;
	}
	if(c->tcp_byte_count < sizeof(uint16_t)) {
		/* read length bytes */
		ERR_clear_error();
		if((r=SSL_read(c->ssl, (void*)sldns_buffer_at(c->buffer,
			c->tcp_byte_count), (int)(sizeof(uint16_t) -
			c->tcp_byte_count))) <= 0) {
			int want = SSL_get_error(c->ssl, r);
			if(want == SSL_ERROR_ZERO_RETURN) {
				if(c->tcp_req_info)
					return tcp_req_info_handle_read_close(c->tcp_req_info);
				return 0; /* shutdown, closed */
			} else if(want == SSL_ERROR_WANT_READ) {
#ifdef USE_WINSOCK
				ub_winsock_tcp_wouldblock(c->ev->ev, UB_EV_READ);
#endif
				return 1; /* read more later */
			} else if(want == SSL_ERROR_WANT_WRITE) {
				c->ssl_shake_state = comm_ssl_shake_hs_write;
				comm_point_listen_for_rw(c, 0, 1);
				return 1;
			} else if(want == SSL_ERROR_SYSCALL) {
#ifdef ECONNRESET
				if(errno == ECONNRESET && verbosity < 2)
					return 0; /* silence reset by peer */
#endif
				if(errno != 0)
					log_err("SSL_read syscall: %s",
						strerror(errno));
				return 0;
			}
			log_crypto_err_io("could not SSL_read", want);
			return 0;
		}
		c->tcp_byte_count += r;
		if(c->tcp_byte_count < sizeof(uint16_t))
			return 1;
		if(sldns_buffer_read_u16_at(c->buffer, 0) >
			sldns_buffer_capacity(c->buffer)) {
			verbose(VERB_QUERY, "ssl: dropped larger than buffer");
			return 0;
		}
		sldns_buffer_set_limit(c->buffer,
			sldns_buffer_read_u16_at(c->buffer, 0));
		if(sldns_buffer_limit(c->buffer) < LDNS_HEADER_SIZE) {
			verbose(VERB_QUERY, "ssl: dropped bogus too short.");
			return 0;
		}
		sldns_buffer_skip(c->buffer, (ssize_t)(c->tcp_byte_count-sizeof(uint16_t)));
		verbose(VERB_ALGO, "Reading ssl tcp query of length %d",
			(int)sldns_buffer_limit(c->buffer));
	}
	if(sldns_buffer_remaining(c->buffer) > 0) {
		ERR_clear_error();
		r = SSL_read(c->ssl, (void*)sldns_buffer_current(c->buffer),
			(int)sldns_buffer_remaining(c->buffer));
		if(r <= 0) {
			int want = SSL_get_error(c->ssl, r);
			if(want == SSL_ERROR_ZERO_RETURN) {
				if(c->tcp_req_info)
					return tcp_req_info_handle_read_close(c->tcp_req_info);
				return 0; /* shutdown, closed */
			} else if(want == SSL_ERROR_WANT_READ) {
#ifdef USE_WINSOCK
				ub_winsock_tcp_wouldblock(c->ev->ev, UB_EV_READ);
#endif
				return 1; /* read more later */
			} else if(want == SSL_ERROR_WANT_WRITE) {
				c->ssl_shake_state = comm_ssl_shake_hs_write;
				comm_point_listen_for_rw(c, 0, 1);
				return 1;
			} else if(want == SSL_ERROR_SYSCALL) {
#ifdef ECONNRESET
				if(errno == ECONNRESET && verbosity < 2)
					return 0; /* silence reset by peer */
#endif
				if(errno != 0)
					log_err("SSL_read syscall: %s",
						strerror(errno));
				return 0;
			}
			log_crypto_err_io("could not SSL_read", want);
			return 0;
		}
		sldns_buffer_skip(c->buffer, (ssize_t)r);
	}
	if(sldns_buffer_remaining(c->buffer) <= 0) {
		tcp_callback_reader(c);
	}
	return 1;
#else
	(void)c;
	return 0;
#endif /* HAVE_SSL */
}

/** ssl write callback on TCP */
static int
ssl_handle_write(struct comm_point* c)
{
#ifdef HAVE_SSL
	int r;
	if(c->ssl_shake_state != comm_ssl_shake_none) {
		if(!ssl_handshake(c))
			return 0;
		if(c->ssl_shake_state != comm_ssl_shake_none)
			return 1;
	}
	/* ignore return, if fails we may simply block */
	(void)SSL_set_mode(c->ssl, (long)SSL_MODE_ENABLE_PARTIAL_WRITE);
	if((c->tcp_write_and_read?c->tcp_write_byte_count:c->tcp_byte_count) < sizeof(uint16_t)) {
		uint16_t len = htons(c->tcp_write_and_read?c->tcp_write_pkt_len:sldns_buffer_limit(c->buffer));
		ERR_clear_error();
		if(c->tcp_write_and_read) {
			if(c->tcp_write_pkt_len + 2 < LDNS_RR_BUF_SIZE) {
				/* combine the tcp length and the query for
				 * write, this emulates writev */
				uint8_t buf[LDNS_RR_BUF_SIZE];
				memmove(buf, &len, sizeof(uint16_t));
				memmove(buf+sizeof(uint16_t),
					c->tcp_write_pkt,
					c->tcp_write_pkt_len);
				r = SSL_write(c->ssl,
					(void*)(buf+c->tcp_write_byte_count),
					c->tcp_write_pkt_len + 2 -
					c->tcp_write_byte_count);
			} else {
				r = SSL_write(c->ssl,
					(void*)(((uint8_t*)&len)+c->tcp_write_byte_count),
					(int)(sizeof(uint16_t)-c->tcp_write_byte_count));
			}
		} else if(sizeof(uint16_t)+sldns_buffer_remaining(c->buffer) <
			LDNS_RR_BUF_SIZE) {
			/* combine the tcp length and the query for write,
			 * this emulates writev */
			uint8_t buf[LDNS_RR_BUF_SIZE];
			memmove(buf, &len, sizeof(uint16_t));
			memmove(buf+sizeof(uint16_t),
				sldns_buffer_current(c->buffer),
				sldns_buffer_remaining(c->buffer));
			r = SSL_write(c->ssl, (void*)(buf+c->tcp_byte_count),
				(int)(sizeof(uint16_t)+
				sldns_buffer_remaining(c->buffer)
				- c->tcp_byte_count));
		} else {
			r = SSL_write(c->ssl,
				(void*)(((uint8_t*)&len)+c->tcp_byte_count),
				(int)(sizeof(uint16_t)-c->tcp_byte_count));
		}
		if(r <= 0) {
			int want = SSL_get_error(c->ssl, r);
			if(want == SSL_ERROR_ZERO_RETURN) {
				return 0; /* closed */
			} else if(want == SSL_ERROR_WANT_READ) {
				c->ssl_shake_state = comm_ssl_shake_hs_read;
				comm_point_listen_for_rw(c, 1, 0);
				return 1; /* wait for read condition */
			} else if(want == SSL_ERROR_WANT_WRITE) {
#ifdef USE_WINSOCK
				ub_winsock_tcp_wouldblock(c->ev->ev, UB_EV_WRITE);
#endif
				return 1; /* write more later */
			} else if(want == SSL_ERROR_SYSCALL) {
#ifdef EPIPE
				if(errno == EPIPE && verbosity < 2)
					return 0; /* silence 'broken pipe' */
#endif
				if(errno != 0)
					log_err("SSL_write syscall: %s",
						strerror(errno));
				return 0;
			}
			log_crypto_err_io("could not SSL_write", want);
			return 0;
		}
		if(c->tcp_write_and_read) {
			c->tcp_write_byte_count += r;
			if(c->tcp_write_byte_count < sizeof(uint16_t))
				return 1;
		} else {
			c->tcp_byte_count += r;
			if(c->tcp_byte_count < sizeof(uint16_t))
				return 1;
			sldns_buffer_set_position(c->buffer, c->tcp_byte_count -
				sizeof(uint16_t));
		}
		if((!c->tcp_write_and_read && sldns_buffer_remaining(c->buffer) == 0) || (c->tcp_write_and_read && c->tcp_write_byte_count == c->tcp_write_pkt_len + 2)) {
			tcp_callback_writer(c);
			return 1;
		}
	}
	log_assert(c->tcp_write_and_read || sldns_buffer_remaining(c->buffer) > 0);
	log_assert(!c->tcp_write_and_read || c->tcp_write_byte_count < c->tcp_write_pkt_len + 2);
	ERR_clear_error();
	if(c->tcp_write_and_read) {
		r = SSL_write(c->ssl, (void*)(c->tcp_write_pkt + c->tcp_write_byte_count - 2),
			(int)(c->tcp_write_pkt_len + 2 - c->tcp_write_byte_count));
	} else {
		r = SSL_write(c->ssl, (void*)sldns_buffer_current(c->buffer),
			(int)sldns_buffer_remaining(c->buffer));
	}
	if(r <= 0) {
		int want = SSL_get_error(c->ssl, r);
		if(want == SSL_ERROR_ZERO_RETURN) {
			return 0; /* closed */
		} else if(want == SSL_ERROR_WANT_READ) {
			c->ssl_shake_state = comm_ssl_shake_hs_read;
			comm_point_listen_for_rw(c, 1, 0);
			return 1; /* wait for read condition */
		} else if(want == SSL_ERROR_WANT_WRITE) {
#ifdef USE_WINSOCK
			ub_winsock_tcp_wouldblock(c->ev->ev, UB_EV_WRITE);
#endif
			return 1; /* write more later */
		} else if(want == SSL_ERROR_SYSCALL) {
#ifdef EPIPE
			if(errno == EPIPE && verbosity < 2)
				return 0; /* silence 'broken pipe' */
#endif
			if(errno != 0)
				log_err("SSL_write syscall: %s",
					strerror(errno));
			return 0;
		}
		log_crypto_err_io("could not SSL_write", want);
		return 0;
	}
	if(c->tcp_write_and_read) {
		c->tcp_write_byte_count += r;
	} else {
		sldns_buffer_skip(c->buffer, (ssize_t)r);
	}

	if((!c->tcp_write_and_read && sldns_buffer_remaining(c->buffer) == 0) || (c->tcp_write_and_read && c->tcp_write_byte_count == c->tcp_write_pkt_len + 2)) {
		tcp_callback_writer(c);
	}
	return 1;
#else
	(void)c;
	return 0;
#endif /* HAVE_SSL */
}

/** handle ssl tcp connection with dns contents */
static int
ssl_handle_it(struct comm_point* c, int is_write)
{
	/* handle case where renegotiation wants read during write call
	 * or write during read calls */
	if(is_write && c->ssl_shake_state == comm_ssl_shake_hs_write)
		return ssl_handle_read(c);
	else if(!is_write && c->ssl_shake_state == comm_ssl_shake_hs_read)
		return ssl_handle_write(c);
	/* handle read events for read operation and write events for a
	 * write operation */
	else if(!is_write)
		return ssl_handle_read(c);
	return ssl_handle_write(c);
}

/**
 * Handle tcp reading callback.
 * @param fd: file descriptor of socket.
 * @param c: comm point to read from into buffer.
 * @param short_ok: if true, very short packets are OK (for comm_local).
 * @return: 0 on error
 */
static int
comm_point_tcp_handle_read(int fd, struct comm_point* c, int short_ok)
{
	ssize_t r;
	int recv_initial = 0;
	log_assert(c->type == comm_tcp || c->type == comm_local);
	if(c->ssl)
		return ssl_handle_it(c, 0);
	if(!c->tcp_is_reading && !c->tcp_write_and_read)
		return 0;

	log_assert(fd != -1);
	if(c->pp2_enabled && c->pp2_header_state != pp2_header_done) {
		struct pp2_header* header = NULL;
		size_t want_read_size = 0;
		size_t current_read_size = 0;
		if(c->pp2_header_state == pp2_header_none) {
			want_read_size = PP2_HEADER_SIZE;
			if(sldns_buffer_remaining(c->buffer)<want_read_size) {
				log_err_addr("proxy_protocol: not enough "
					"buffer size to read PROXYv2 header", "",
					&c->repinfo.remote_addr,
					c->repinfo.remote_addrlen);
				return 0;
			}
			verbose(VERB_ALGO, "proxy_protocol: reading fixed "
				"part of PROXYv2 header (len %lu)",
				(unsigned long)want_read_size);
			current_read_size = want_read_size;
			if(c->tcp_byte_count < current_read_size) {
				r = recv(fd, (void*)sldns_buffer_at(c->buffer,
					c->tcp_byte_count),
					current_read_size-c->tcp_byte_count, MSG_DONTWAIT);
				if(r == 0) {
					if(c->tcp_req_info)
						return tcp_req_info_handle_read_close(c->tcp_req_info);
					return 0;
				} else if(r == -1) {
					goto recv_error_initial;
				}
				c->tcp_byte_count += r;
				sldns_buffer_skip(c->buffer, r);
				if(c->tcp_byte_count != current_read_size) return 1;
				c->pp2_header_state = pp2_header_init;
			}
		}
		if(c->pp2_header_state == pp2_header_init) {
			int err;
			err = pp2_read_header(
				sldns_buffer_begin(c->buffer),
				sldns_buffer_limit(c->buffer));
			if(err) {
				log_err("proxy_protocol: could not parse "
					"PROXYv2 header (%s)",
					pp_lookup_error(err));
				return 0;
			}
			header = (struct pp2_header*)sldns_buffer_begin(c->buffer);
			want_read_size = ntohs(header->len);
			if(sldns_buffer_limit(c->buffer) <
				PP2_HEADER_SIZE + want_read_size) {
				log_err_addr("proxy_protocol: not enough "
					"buffer size to read PROXYv2 header", "",
					&c->repinfo.remote_addr,
					c->repinfo.remote_addrlen);
				return 0;
			}
			verbose(VERB_ALGO, "proxy_protocol: reading variable "
				"part of PROXYv2 header (len %lu)",
				(unsigned long)want_read_size);
			current_read_size = PP2_HEADER_SIZE + want_read_size;
			if(want_read_size == 0) {
				/* nothing more to read; header is complete */
				c->pp2_header_state = pp2_header_done;
			} else if(c->tcp_byte_count < current_read_size) {
				r = recv(fd, (void*)sldns_buffer_at(c->buffer,
					c->tcp_byte_count),
					current_read_size-c->tcp_byte_count, MSG_DONTWAIT);
				if(r == 0) {
					if(c->tcp_req_info)
						return tcp_req_info_handle_read_close(c->tcp_req_info);
					return 0;
				} else if(r == -1) {
					goto recv_error;
				}
				c->tcp_byte_count += r;
				sldns_buffer_skip(c->buffer, r);
				if(c->tcp_byte_count != current_read_size) return 1;
				c->pp2_header_state = pp2_header_done;
			}
		}
		if(c->pp2_header_state != pp2_header_done || !header) {
			log_err_addr("proxy_protocol: wrong state for the "
				"PROXYv2 header", "", &c->repinfo.remote_addr,
				c->repinfo.remote_addrlen);
			return 0;
		}
		sldns_buffer_flip(c->buffer);
		if(!consume_pp2_header(c->buffer, &c->repinfo, 1)) {
			log_err_addr("proxy_protocol: could not consume "
				"PROXYv2 header", "", &c->repinfo.remote_addr,
				c->repinfo.remote_addrlen);
			return 0;
		}
		verbose(VERB_ALGO, "proxy_protocol: successful read of "
			"PROXYv2 header");
		/* Clear and reset the buffer to read the following
		    * DNS packet(s). */
		sldns_buffer_clear(c->buffer);
		c->tcp_byte_count = 0;
		return 1;
	}

	if(c->tcp_byte_count < sizeof(uint16_t)) {
		/* read length bytes */
		r = recv(fd,(void*)sldns_buffer_at(c->buffer,c->tcp_byte_count),
			sizeof(uint16_t)-c->tcp_byte_count, MSG_DONTWAIT);
		if(r == 0) {
			if(c->tcp_req_info)
				return tcp_req_info_handle_read_close(c->tcp_req_info);
			return 0;
		} else if(r == -1) {
			if(c->pp2_enabled) goto recv_error;
			goto recv_error_initial;
		}
		c->tcp_byte_count += r;
		if(c->tcp_byte_count != sizeof(uint16_t))
			return 1;
		if(sldns_buffer_read_u16_at(c->buffer, 0) >
			sldns_buffer_capacity(c->buffer)) {
			verbose(VERB_QUERY, "tcp: dropped larger than buffer");
			return 0;
		}
		sldns_buffer_set_limit(c->buffer,
			sldns_buffer_read_u16_at(c->buffer, 0));
		if(!short_ok &&
			sldns_buffer_limit(c->buffer) < LDNS_HEADER_SIZE) {
			verbose(VERB_QUERY, "tcp: dropped bogus too short.");
			return 0;
		}
		verbose(VERB_ALGO, "Reading tcp query of length %d",
			(int)sldns_buffer_limit(c->buffer));
	}

	if(sldns_buffer_remaining(c->buffer) == 0)
		log_err("in comm_point_tcp_handle_read buffer_remaining is "
			"not > 0 as expected, continuing with (harmless) 0 "
			"length recv");
	r = recv(fd, (void*)sldns_buffer_current(c->buffer),
		sldns_buffer_remaining(c->buffer), MSG_DONTWAIT);
	if(r == 0) {
		if(c->tcp_req_info)
			return tcp_req_info_handle_read_close(c->tcp_req_info);
		return 0;
	} else if(r == -1) {
		goto recv_error;
	}
	sldns_buffer_skip(c->buffer, r);
	if(sldns_buffer_remaining(c->buffer) <= 0) {
		tcp_callback_reader(c);
	}
	return 1;

recv_error_initial:
	recv_initial = 1;
recv_error:
#ifndef USE_WINSOCK
	if(errno == EINTR || errno == EAGAIN)
		return 1;
#ifdef ECONNRESET
		if(errno == ECONNRESET && verbosity < 2)
			return 0; /* silence reset by peer */
#endif
	if(recv_initial) {
#ifdef ECONNREFUSED
		if(errno == ECONNREFUSED && verbosity < 2)
			return 0; /* silence reset by peer */
#endif
#ifdef ENETUNREACH
		if(errno == ENETUNREACH && verbosity < 2)
			return 0; /* silence it */
#endif
#ifdef EHOSTDOWN
		if(errno == EHOSTDOWN && verbosity < 2)
			return 0; /* silence it */
#endif
#ifdef EHOSTUNREACH
		if(errno == EHOSTUNREACH && verbosity < 2)
			return 0; /* silence it */
#endif
#ifdef ENETDOWN
		if(errno == ENETDOWN && verbosity < 2)
			return 0; /* silence it */
#endif
#ifdef EACCES
		if(errno == EACCES && verbosity < 2)
			return 0; /* silence it */
#endif
#ifdef ENOTCONN
		if(errno == ENOTCONN) {
			log_err_addr("read (in tcp initial) failed and this "
				"could be because TCP Fast Open is "
				"enabled [--disable-tfo-client "
				"--disable-tfo-server] but does not "
				"work", sock_strerror(errno),
				&c->repinfo.remote_addr,
				c->repinfo.remote_addrlen);
			return 0;
		}
#endif
	}
#else /* USE_WINSOCK */
	if(recv_initial) {
		if(WSAGetLastError() == WSAECONNREFUSED && verbosity < 2)
			return 0;
		if(WSAGetLastError() == WSAEHOSTDOWN && verbosity < 2)
			return 0;
		if(WSAGetLastError() == WSAEHOSTUNREACH && verbosity < 2)
			return 0;
		if(WSAGetLastError() == WSAENETDOWN && verbosity < 2)
			return 0;
		if(WSAGetLastError() == WSAENETUNREACH && verbosity < 2)
			return 0;
	}
	if(WSAGetLastError() == WSAECONNRESET)
		return 0;
	if(WSAGetLastError() == WSAEINPROGRESS)
		return 1;
	if(WSAGetLastError() == WSAEWOULDBLOCK) {
		ub_winsock_tcp_wouldblock(c->ev->ev,
			UB_EV_READ);
		return 1;
	}
#endif
	log_err_addr((recv_initial?"read (in tcp initial)":"read (in tcp)"),
		sock_strerror(errno), &c->repinfo.remote_addr,
		c->repinfo.remote_addrlen);
	return 0;
}

/**
 * Handle tcp writing callback.
 * @param fd: file descriptor of socket.
 * @param c: comm point to write buffer out of.
 * @return: 0 on error
 */
static int
comm_point_tcp_handle_write(int fd, struct comm_point* c)
{
	ssize_t r;
	struct sldns_buffer *buffer;
	log_assert(c->type == comm_tcp);
#ifdef USE_DNSCRYPT
	buffer = c->dnscrypt_buffer;
#else
	buffer = c->buffer;
#endif
	if(c->tcp_is_reading && !c->ssl && !c->tcp_write_and_read)
		return 0;
	log_assert(fd != -1);
	if(((!c->tcp_write_and_read && c->tcp_byte_count == 0) || (c->tcp_write_and_read && c->tcp_write_byte_count == 0)) && c->tcp_check_nb_connect) {
		/* check for pending error from nonblocking connect */
		/* from Stevens, unix network programming, vol1, 3rd ed, p450*/
		int error = 0;
		socklen_t len = (socklen_t)sizeof(error);
		if(getsockopt(fd, SOL_SOCKET, SO_ERROR, (void*)&error,
			&len) < 0){
#ifndef USE_WINSOCK
			error = errno; /* on solaris errno is error */
#else /* USE_WINSOCK */
			error = WSAGetLastError();
#endif
		}
#ifndef USE_WINSOCK
#if defined(EINPROGRESS) && defined(EWOULDBLOCK)
		if(error == EINPROGRESS || error == EWOULDBLOCK)
			return 1; /* try again later */
		else
#endif
		if(error != 0 && verbosity < 2)
			return 0; /* silence lots of chatter in the logs */
                else if(error != 0) {
			log_err_addr("tcp connect", strerror(error),
				&c->repinfo.remote_addr,
				c->repinfo.remote_addrlen);
#else /* USE_WINSOCK */
		/* examine error */
		if(error == WSAEINPROGRESS)
			return 1;
		else if(error == WSAEWOULDBLOCK) {
			ub_winsock_tcp_wouldblock(c->ev->ev, UB_EV_WRITE);
			return 1;
		} else if(error != 0 && verbosity < 2)
			return 0;
		else if(error != 0) {
			log_err_addr("tcp connect", wsa_strerror(error),
				&c->repinfo.remote_addr,
				c->repinfo.remote_addrlen);
#endif /* USE_WINSOCK */
			return 0;
		}
	}
	if(c->ssl)
		return ssl_handle_it(c, 1);

#ifdef USE_MSG_FASTOPEN
	/* Only try this on first use of a connection that uses tfo,
	   otherwise fall through to normal write */
	/* Also, TFO support on WINDOWS not implemented at the moment */
	if(c->tcp_do_fastopen == 1) {
		/* this form of sendmsg() does both a connect() and send() so need to
		   look for various flavours of error*/
		uint16_t len = htons(c->tcp_write_and_read?c->tcp_write_pkt_len:sldns_buffer_limit(buffer));
		struct msghdr msg;
		struct iovec iov[2];
		c->tcp_do_fastopen = 0;
		memset(&msg, 0, sizeof(msg));
		if(c->tcp_write_and_read) {
			iov[0].iov_base = (uint8_t*)&len + c->tcp_write_byte_count;
			iov[0].iov_len = sizeof(uint16_t) - c->tcp_write_byte_count;
			iov[1].iov_base = c->tcp_write_pkt;
			iov[1].iov_len = c->tcp_write_pkt_len;
		} else {
			iov[0].iov_base = (uint8_t*)&len + c->tcp_byte_count;
			iov[0].iov_len = sizeof(uint16_t) - c->tcp_byte_count;
			iov[1].iov_base = sldns_buffer_begin(buffer);
			iov[1].iov_len = sldns_buffer_limit(buffer);
		}
		log_assert(iov[0].iov_len > 0);
		msg.msg_name = &c->repinfo.remote_addr;
		msg.msg_namelen = c->repinfo.remote_addrlen;
		msg.msg_iov = iov;
		msg.msg_iovlen = 2;
		r = sendmsg(fd, &msg, MSG_FASTOPEN);
		if (r == -1) {
#if defined(EINPROGRESS) && defined(EWOULDBLOCK)
			/* Handshake is underway, maybe because no TFO cookie available.
			   Come back to write the message*/
			if(errno == EINPROGRESS || errno == EWOULDBLOCK)
				return 1;
#endif
			if(errno == EINTR || errno == EAGAIN)
				return 1;
			/* Not handling EISCONN here as shouldn't ever hit that case.*/
			if(errno != EPIPE
#ifdef EOPNOTSUPP
				/* if /proc/sys/net/ipv4/tcp_fastopen is
				 * disabled on Linux, sendmsg may return
				 * 'Operation not supported', if so
				 * fallthrough to ordinary connect. */
				&& errno != EOPNOTSUPP
#endif
				&& errno != 0) {
				if(verbosity < 2)
					return 0; /* silence lots of chatter in the logs */
				log_err_addr("tcp sendmsg", strerror(errno),
					&c->repinfo.remote_addr,
					c->repinfo.remote_addrlen);
				return 0;
			}
			verbose(VERB_ALGO, "tcp sendmsg for fastopen failed (with %s), try normal connect", strerror(errno));
			/* fallthrough to nonFASTOPEN
			 * (MSG_FASTOPEN on Linux 3 produces EPIPE)
			 * we need to perform connect() */
			if(connect(fd, (struct sockaddr *)&c->repinfo.remote_addr,
				c->repinfo.remote_addrlen) == -1) {
#ifdef EINPROGRESS
				if(errno == EINPROGRESS)
					return 1; /* wait until connect done*/
#endif
#ifdef USE_WINSOCK
				if(WSAGetLastError() == WSAEINPROGRESS ||
					WSAGetLastError() == WSAEWOULDBLOCK)
					return 1; /* wait until connect done*/
#endif
				if(tcp_connect_errno_needs_log(
					(struct sockaddr *)&c->repinfo.remote_addr,
					c->repinfo.remote_addrlen)) {
					log_err_addr("outgoing tcp: connect after EPIPE for fastopen",
						strerror(errno),
						&c->repinfo.remote_addr,
						c->repinfo.remote_addrlen);
				}
				return 0;
			}

		} else {
			if(c->tcp_write_and_read) {
				c->tcp_write_byte_count += r;
				if(c->tcp_write_byte_count < sizeof(uint16_t))
					return 1;
			} else {
				c->tcp_byte_count += r;
				if(c->tcp_byte_count < sizeof(uint16_t))
					return 1;
				sldns_buffer_set_position(buffer, c->tcp_byte_count -
					sizeof(uint16_t));
			}
			if((!c->tcp_write_and_read && sldns_buffer_remaining(buffer) == 0) || (c->tcp_write_and_read && c->tcp_write_byte_count == c->tcp_write_pkt_len + 2)) {
				tcp_callback_writer(c);
				return 1;
			}
		}
	}
#endif /* USE_MSG_FASTOPEN */

	if((c->tcp_write_and_read?c->tcp_write_byte_count:c->tcp_byte_count) < sizeof(uint16_t)) {
		uint16_t len = htons(c->tcp_write_and_read?c->tcp_write_pkt_len:sldns_buffer_limit(buffer));
#ifdef HAVE_WRITEV
		struct iovec iov[2];
		if(c->tcp_write_and_read) {
			iov[0].iov_base = (uint8_t*)&len + c->tcp_write_byte_count;
			iov[0].iov_len = sizeof(uint16_t) - c->tcp_write_byte_count;
			iov[1].iov_base = c->tcp_write_pkt;
			iov[1].iov_len = c->tcp_write_pkt_len;
		} else {
			iov[0].iov_base = (uint8_t*)&len + c->tcp_byte_count;
			iov[0].iov_len = sizeof(uint16_t) - c->tcp_byte_count;
			iov[1].iov_base = sldns_buffer_begin(buffer);
			iov[1].iov_len = sldns_buffer_limit(buffer);
		}
		log_assert(iov[0].iov_len > 0);
		r = writev(fd, iov, 2);
#else /* HAVE_WRITEV */
		if(c->tcp_write_and_read) {
			r = send(fd, (void*)(((uint8_t*)&len)+c->tcp_write_byte_count),
				sizeof(uint16_t)-c->tcp_write_byte_count, 0);
		} else {
			r = send(fd, (void*)(((uint8_t*)&len)+c->tcp_byte_count),
				sizeof(uint16_t)-c->tcp_byte_count, 0);
		}
#endif /* HAVE_WRITEV */
		if(r == -1) {
#ifndef USE_WINSOCK
#  ifdef EPIPE
                	if(errno == EPIPE && verbosity < 2)
                        	return 0; /* silence 'broken pipe' */
  #endif
			if(errno == EINTR || errno == EAGAIN)
				return 1;
#ifdef ECONNRESET
			if(errno == ECONNRESET && verbosity < 2)
				return 0; /* silence reset by peer */
#endif
#  ifdef HAVE_WRITEV
			log_err_addr("tcp writev", strerror(errno),
				&c->repinfo.remote_addr,
				c->repinfo.remote_addrlen);
#  else /* HAVE_WRITEV */
			log_err_addr("tcp send s", strerror(errno),
				&c->repinfo.remote_addr,
				c->repinfo.remote_addrlen);
#  endif /* HAVE_WRITEV */
#else
			if(WSAGetLastError() == WSAENOTCONN)
				return 1;
			if(WSAGetLastError() == WSAEINPROGRESS)
				return 1;
			if(WSAGetLastError() == WSAEWOULDBLOCK) {
				ub_winsock_tcp_wouldblock(c->ev->ev,
					UB_EV_WRITE);
				return 1;
			}
			if(WSAGetLastError() == WSAECONNRESET && verbosity < 2)
				return 0; /* silence reset by peer */
			log_err_addr("tcp send s",
				wsa_strerror(WSAGetLastError()),
				&c->repinfo.remote_addr,
				c->repinfo.remote_addrlen);
#endif
			return 0;
		}
		if(c->tcp_write_and_read) {
			c->tcp_write_byte_count += r;
			if(c->tcp_write_byte_count < sizeof(uint16_t))
				return 1;
		} else {
			c->tcp_byte_count += r;
			if(c->tcp_byte_count < sizeof(uint16_t))
				return 1;
			sldns_buffer_set_position(buffer, c->tcp_byte_count -
				sizeof(uint16_t));
		}
		if((!c->tcp_write_and_read && sldns_buffer_remaining(buffer) == 0) || (c->tcp_write_and_read && c->tcp_write_byte_count == c->tcp_write_pkt_len + 2)) {
			tcp_callback_writer(c);
			return 1;
		}
	}
	log_assert(c->tcp_write_and_read || sldns_buffer_remaining(buffer) > 0);
	log_assert(!c->tcp_write_and_read || c->tcp_write_byte_count < c->tcp_write_pkt_len + 2);
	if(c->tcp_write_and_read) {
		r = send(fd, (void*)(c->tcp_write_pkt + c->tcp_write_byte_count - 2),
			c->tcp_write_pkt_len + 2 - c->tcp_write_byte_count, 0);
	} else {
		r = send(fd, (void*)sldns_buffer_current(buffer),
			sldns_buffer_remaining(buffer), 0);
	}
	if(r == -1) {
#ifndef USE_WINSOCK
		if(errno == EINTR || errno == EAGAIN)
			return 1;
#ifdef ECONNRESET
		if(errno == ECONNRESET && verbosity < 2)
			return 0; /* silence reset by peer */
#endif
#else
		if(WSAGetLastError() == WSAEINPROGRESS)
			return 1;
		if(WSAGetLastError() == WSAEWOULDBLOCK) {
			ub_winsock_tcp_wouldblock(c->ev->ev, UB_EV_WRITE);
			return 1;
		}
		if(WSAGetLastError() == WSAECONNRESET && verbosity < 2)
			return 0; /* silence reset by peer */
#endif
		log_err_addr("tcp send r", sock_strerror(errno),
			&c->repinfo.remote_addr,
			c->repinfo.remote_addrlen);
		return 0;
	}
	if(c->tcp_write_and_read) {
		c->tcp_write_byte_count += r;
	} else {
		sldns_buffer_skip(buffer, r);
	}

	if((!c->tcp_write_and_read && sldns_buffer_remaining(buffer) == 0) || (c->tcp_write_and_read && c->tcp_write_byte_count == c->tcp_write_pkt_len + 2)) {
		tcp_callback_writer(c);
	}

	return 1;
}

/** read again to drain buffers when there could be more to read, returns 0
 * on failure which means the comm point is closed. */
static int
tcp_req_info_read_again(int fd, struct comm_point* c)
{
	while(c->tcp_req_info->read_again) {
		int r;
		c->tcp_req_info->read_again = 0;
		if(c->tcp_is_reading)
			r = comm_point_tcp_handle_read(fd, c, 0);
		else 	r = comm_point_tcp_handle_write(fd, c);
		if(!r) {
			reclaim_tcp_handler(c);
			if(!c->tcp_do_close) {
				fptr_ok(fptr_whitelist_comm_point(
					c->callback));
				(void)(*c->callback)(c, c->cb_arg,
					NETEVENT_CLOSED, NULL);
			}
			return 0;
		}
	}
	return 1;
}

/** read again to drain buffers when there could be more to read */
static void
tcp_more_read_again(int fd, struct comm_point* c)
{
	/* if the packet is done, but another one could be waiting on
	 * the connection, the callback signals this, and we try again */
	/* this continues until the read routines get EAGAIN or so,
	 * and thus does not call the callback, and the bool is 0 */
	int* moreread = c->tcp_more_read_again;
	while(moreread && *moreread) {
		*moreread = 0;
		if(!comm_point_tcp_handle_read(fd, c, 0)) {
			reclaim_tcp_handler(c);
			if(!c->tcp_do_close) {
				fptr_ok(fptr_whitelist_comm_point(
					c->callback));
				(void)(*c->callback)(c, c->cb_arg,
					NETEVENT_CLOSED, NULL);
			}
			return;
		}
	}
}

/** write again to fill up when there could be more to write */
static void
tcp_more_write_again(int fd, struct comm_point* c)
{
	/* if the packet is done, but another is waiting to be written,
	 * the callback signals it and we try again. */
	/* this continues until the write routines get EAGAIN or so,
	 * and thus does not call the callback, and the bool is 0 */
	int* morewrite = c->tcp_more_write_again;
	while(morewrite && *morewrite) {
		*morewrite = 0;
		if(!comm_point_tcp_handle_write(fd, c)) {
			reclaim_tcp_handler(c);
			if(!c->tcp_do_close) {
				fptr_ok(fptr_whitelist_comm_point(
					c->callback));
				(void)(*c->callback)(c, c->cb_arg,
					NETEVENT_CLOSED, NULL);
			}
			return;
		}
	}
}

void
comm_point_tcp_handle_callback(int fd, short event, void* arg)
{
	struct comm_point* c = (struct comm_point*)arg;
	log_assert(c->type == comm_tcp);
	ub_comm_base_now(c->ev->base);

	if(c->fd == -1 || c->fd != fd)
		return; /* duplicate event, but commpoint closed. */

#ifdef USE_DNSCRYPT
	/* Initialize if this is a dnscrypt socket */
	if(c->tcp_parent) {
		c->dnscrypt = c->tcp_parent->dnscrypt;
	}
	if(c->dnscrypt && c->dnscrypt_buffer == c->buffer) {
		c->dnscrypt_buffer = sldns_buffer_new(sldns_buffer_capacity(c->buffer));
		if(!c->dnscrypt_buffer) {
			log_err("Could not allocate dnscrypt buffer");
			reclaim_tcp_handler(c);
			if(!c->tcp_do_close) {
				fptr_ok(fptr_whitelist_comm_point(
					c->callback));
				(void)(*c->callback)(c, c->cb_arg,
					NETEVENT_CLOSED, NULL);
			}
			return;
		}
	}
#endif

	if(event&UB_EV_TIMEOUT) {
		verbose(VERB_QUERY, "tcp took too long, dropped");
		reclaim_tcp_handler(c);
		if(!c->tcp_do_close) {
			fptr_ok(fptr_whitelist_comm_point(c->callback));
			(void)(*c->callback)(c, c->cb_arg,
				NETEVENT_TIMEOUT, NULL);
		}
		return;
	}
	if(event&UB_EV_READ
#ifdef USE_MSG_FASTOPEN
		&& !(c->tcp_do_fastopen && (event&UB_EV_WRITE))
#endif
		) {
		int has_tcpq = (c->tcp_req_info != NULL);
		int* moreread = c->tcp_more_read_again;
		if(!comm_point_tcp_handle_read(fd, c, 0)) {
			reclaim_tcp_handler(c);
			if(!c->tcp_do_close) {
				fptr_ok(fptr_whitelist_comm_point(
					c->callback));
				(void)(*c->callback)(c, c->cb_arg,
					NETEVENT_CLOSED, NULL);
			}
			return;
		}
		if(has_tcpq && c->tcp_req_info && c->tcp_req_info->read_again) {
			if(!tcp_req_info_read_again(fd, c))
				return;
		}
		if(moreread && *moreread)
			tcp_more_read_again(fd, c);
		return;
	}
	if(event&UB_EV_WRITE) {
		int has_tcpq = (c->tcp_req_info != NULL);
		int* morewrite = c->tcp_more_write_again;
		if(!comm_point_tcp_handle_write(fd, c)) {
			reclaim_tcp_handler(c);
			if(!c->tcp_do_close) {
				fptr_ok(fptr_whitelist_comm_point(
					c->callback));
				(void)(*c->callback)(c, c->cb_arg,
					NETEVENT_CLOSED, NULL);
			}
			return;
		}
		if(has_tcpq && c->tcp_req_info && c->tcp_req_info->read_again) {
			if(!tcp_req_info_read_again(fd, c))
				return;
		}
		if(morewrite && *morewrite)
			tcp_more_write_again(fd, c);
		return;
	}
	log_err("Ignored event %d for tcphdl.", event);
}

/** Make http handler free for next assignment */
static void
reclaim_http_handler(struct comm_point* c)
{
	log_assert(c->type == comm_http);
	if(c->ssl) {
#ifdef HAVE_SSL
		SSL_shutdown(c->ssl);
		SSL_free(c->ssl);
		c->ssl = NULL;
#endif
	}
	comm_point_close(c);
	if(c->tcp_parent) {
		if(c != c->tcp_parent->tcp_free) {
			c->tcp_parent->cur_tcp_count--;
			c->tcp_free = c->tcp_parent->tcp_free;
			c->tcp_parent->tcp_free = c;
		}
		if(!c->tcp_free) {
			/* re-enable listening on accept socket */
			comm_point_start_listening(c->tcp_parent, -1, -1);
		}
	}
}

/** read more data for http (with ssl) */
static int
ssl_http_read_more(struct comm_point* c)
{
#ifdef HAVE_SSL
	int r;
	log_assert(sldns_buffer_remaining(c->buffer) > 0);
	ERR_clear_error();
	r = SSL_read(c->ssl, (void*)sldns_buffer_current(c->buffer),
		(int)sldns_buffer_remaining(c->buffer));
	if(r <= 0) {
		int want = SSL_get_error(c->ssl, r);
		if(want == SSL_ERROR_ZERO_RETURN) {
			return 0; /* shutdown, closed */
		} else if(want == SSL_ERROR_WANT_READ) {
			return 1; /* read more later */
		} else if(want == SSL_ERROR_WANT_WRITE) {
			c->ssl_shake_state = comm_ssl_shake_hs_write;
			comm_point_listen_for_rw(c, 0, 1);
			return 1;
		} else if(want == SSL_ERROR_SYSCALL) {
#ifdef ECONNRESET
			if(errno == ECONNRESET && verbosity < 2)
				return 0; /* silence reset by peer */
#endif
			if(errno != 0)
				log_err("SSL_read syscall: %s",
					strerror(errno));
			return 0;
		}
		log_crypto_err_io("could not SSL_read", want);
		return 0;
	}
	verbose(VERB_ALGO, "ssl http read more skip to %d + %d",
		(int)sldns_buffer_position(c->buffer), (int)r);
	sldns_buffer_skip(c->buffer, (ssize_t)r);
	return 1;
#else
	(void)c;
	return 0;
#endif /* HAVE_SSL */
}

/** read more data for http */
static int
http_read_more(int fd, struct comm_point* c)
{
	ssize_t r;
	log_assert(sldns_buffer_remaining(c->buffer) > 0);
	r = recv(fd, (void*)sldns_buffer_current(c->buffer),
		sldns_buffer_remaining(c->buffer), MSG_DONTWAIT);
	if(r == 0) {
		return 0;
	} else if(r == -1) {
#ifndef USE_WINSOCK
		if(errno == EINTR || errno == EAGAIN)
			return 1;
#else /* USE_WINSOCK */
		if(WSAGetLastError() == WSAECONNRESET)
			return 0;
		if(WSAGetLastError() == WSAEINPROGRESS)
			return 1;
		if(WSAGetLastError() == WSAEWOULDBLOCK) {
			ub_winsock_tcp_wouldblock(c->ev->ev, UB_EV_READ);
			return 1;
		}
#endif
		log_err_addr("read (in http r)", sock_strerror(errno),
			&c->repinfo.remote_addr, c->repinfo.remote_addrlen);
		return 0;
	}
	verbose(VERB_ALGO, "http read more skip to %d + %d",
		(int)sldns_buffer_position(c->buffer), (int)r);
	sldns_buffer_skip(c->buffer, r);
	return 1;
}

/** return true if http header has been read (one line complete) */
static int
http_header_done(sldns_buffer* buf)
{
	size_t i;
	for(i=sldns_buffer_position(buf); i<sldns_buffer_limit(buf); i++) {
		/* there was a \r before the \n, but we ignore that */
		if((char)sldns_buffer_read_u8_at(buf, i) == '\n')
			return 1;
	}
	return 0;
}

/** return character string into buffer for header line, moves buffer
 * past that line and puts zero terminator into linefeed-newline */
static char*
http_header_line(sldns_buffer* buf)
{
	char* result = (char*)sldns_buffer_current(buf);
	size_t i;
	for(i=sldns_buffer_position(buf); i<sldns_buffer_limit(buf); i++) {
		/* terminate the string on the \r */
		if((char)sldns_buffer_read_u8_at(buf, i) == '\r')
			sldns_buffer_write_u8_at(buf, i, 0);
		/* terminate on the \n and skip past the it and done */
		if((char)sldns_buffer_read_u8_at(buf, i) == '\n') {
			sldns_buffer_write_u8_at(buf, i, 0);
			sldns_buffer_set_position(buf, i+1);
			return result;
		}
	}
	return NULL;
}

/** move unread buffer to start and clear rest for putting the rest into it */
static void
http_moveover_buffer(sldns_buffer* buf)
{
	size_t pos = sldns_buffer_position(buf);
	size_t len = sldns_buffer_remaining(buf);
	sldns_buffer_clear(buf);
	memmove(sldns_buffer_begin(buf), sldns_buffer_at(buf, pos), len);
	sldns_buffer_set_position(buf, len);
}

/** a http header is complete, process it */
static int
http_process_initial_header(struct comm_point* c)
{
	char* line = http_header_line(c->buffer);
	if(!line) return 1;
	verbose(VERB_ALGO, "http header: %s", line);
	if(strncasecmp(line, "HTTP/1.1 ", 9) == 0) {
		/* check returncode */
		if(line[9] != '2') {
			verbose(VERB_ALGO, "http bad status %s", line+9);
			return 0;
		}
	} else if(strncasecmp(line, "Content-Length: ", 16) == 0) {
		if(!c->http_is_chunked)
			c->tcp_byte_count = (size_t)atoi(line+16);
	} else if(strncasecmp(line, "Transfer-Encoding: chunked", 19+7) == 0) {
		c->tcp_byte_count = 0;
		c->http_is_chunked = 1;
	} else if(line[0] == 0) {
		/* end of initial headers */
		c->http_in_headers = 0;
		if(c->http_is_chunked)
			c->http_in_chunk_headers = 1;
		/* remove header text from front of buffer
		 * the buffer is going to be used to return the data segment
		 * itself and we don't want the header to get returned
		 * prepended with it */
		http_moveover_buffer(c->buffer);
		sldns_buffer_flip(c->buffer);
		return 1;
	}
	/* ignore other headers */
	return 1;
}

/** a chunk header is complete, process it, return 0=fail, 1=continue next
 * header line, 2=done with chunked transfer*/
static int
http_process_chunk_header(struct comm_point* c)
{
	char* line = http_header_line(c->buffer);
	if(!line) return 1;
	if(c->http_in_chunk_headers == 3) {
		verbose(VERB_ALGO, "http chunk trailer: %s", line);
		/* are we done ? */
		if(line[0] == 0 && c->tcp_byte_count == 0) {
			/* callback of http reader when NETEVENT_DONE,
			 * end of data, with no data in buffer */
			sldns_buffer_set_position(c->buffer, 0);
			sldns_buffer_set_limit(c->buffer, 0);
			fptr_ok(fptr_whitelist_comm_point(c->callback));
			(void)(*c->callback)(c, c->cb_arg, NETEVENT_DONE, NULL);
			/* return that we are done */
			return 2;
		}
		if(line[0] == 0) {
			/* continue with header of the next chunk */
			c->http_in_chunk_headers = 1;
			/* remove header text from front of buffer */
			http_moveover_buffer(c->buffer);
			sldns_buffer_flip(c->buffer);
			return 1;
		}
		/* ignore further trail headers */
		return 1;
	}
	verbose(VERB_ALGO, "http chunk header: %s", line);
	if(c->http_in_chunk_headers == 1) {
		/* read chunked start line */
		char* end = NULL;
		c->tcp_byte_count = (size_t)strtol(line, &end, 16);
		if(end == line)
			return 0;
		c->http_in_chunk_headers = 0;
		/* remove header text from front of buffer */
		http_moveover_buffer(c->buffer);
		sldns_buffer_flip(c->buffer);
		if(c->tcp_byte_count == 0) {
			/* done with chunks, process chunk_trailer lines */
			c->http_in_chunk_headers = 3;
		}
		return 1;
	}
	/* ignore other headers */
	return 1;
}

/** handle nonchunked data segment, 0=fail, 1=wait */
static int
http_nonchunk_segment(struct comm_point* c)
{
	/* c->buffer at position..limit has new data we read in.
	 * the buffer itself is full of nonchunked data.
	 * we are looking to read tcp_byte_count more data
	 * and then the transfer is done. */
	size_t remainbufferlen;
	size_t got_now = sldns_buffer_limit(c->buffer);
	if(c->tcp_byte_count <= got_now) {
		/* done, this is the last data fragment */
		c->http_stored = 0;
		sldns_buffer_set_position(c->buffer, 0);
		fptr_ok(fptr_whitelist_comm_point(c->callback));
		(void)(*c->callback)(c, c->cb_arg, NETEVENT_DONE, NULL);
		return 1;
	}
	/* if we have the buffer space,
	 * read more data collected into the buffer */
	remainbufferlen = sldns_buffer_capacity(c->buffer) -
		sldns_buffer_limit(c->buffer);
	if(remainbufferlen+got_now >= c->tcp_byte_count ||
		remainbufferlen >= (size_t)(c->ssl?16384:2048)) {
		size_t total = sldns_buffer_limit(c->buffer);
		sldns_buffer_clear(c->buffer);
		sldns_buffer_set_position(c->buffer, total);
		c->http_stored = total;
		/* return and wait to read more */
		return 1;
	}
	/* call callback with this data amount, then
	 * wait for more */
	c->tcp_byte_count -= got_now;
	c->http_stored = 0;
	sldns_buffer_set_position(c->buffer, 0);
	fptr_ok(fptr_whitelist_comm_point(c->callback));
	(void)(*c->callback)(c, c->cb_arg, NETEVENT_NOERROR, NULL);
	/* c->callback has to buffer_clear(c->buffer). */
	/* return and wait to read more */
	return 1;
}

/** handle chunked data segment, return 0=fail, 1=wait, 2=process more */
static int
http_chunked_segment(struct comm_point* c)
{
	/* the c->buffer has from position..limit new data we read. */
	/* the current chunk has length tcp_byte_count.
	 * once we read that read more chunk headers.
	 */
	size_t remainbufferlen;
	size_t got_now = sldns_buffer_limit(c->buffer) - c->http_stored;
	verbose(VERB_ALGO, "http_chunked_segment: got now %d, tcpbytcount %d, http_stored %d, buffer pos %d, buffer limit %d", (int)got_now, (int)c->tcp_byte_count, (int)c->http_stored, (int)sldns_buffer_position(c->buffer), (int)sldns_buffer_limit(c->buffer));
	if(c->tcp_byte_count <= got_now) {
		/* the chunk has completed (with perhaps some extra data
		 * from next chunk header and next chunk) */
		/* save too much info into temp buffer */
		size_t fraglen;
		struct comm_reply repinfo;
		c->http_stored = 0;
		sldns_buffer_skip(c->buffer, (ssize_t)c->tcp_byte_count);
		sldns_buffer_clear(c->http_temp);
		sldns_buffer_write(c->http_temp,
			sldns_buffer_current(c->buffer),
			sldns_buffer_remaining(c->buffer));
		sldns_buffer_flip(c->http_temp);

		/* callback with this fragment */
		fraglen = sldns_buffer_position(c->buffer);
		sldns_buffer_set_position(c->buffer, 0);
		sldns_buffer_set_limit(c->buffer, fraglen);
		repinfo = c->repinfo;
		fptr_ok(fptr_whitelist_comm_point(c->callback));
		(void)(*c->callback)(c, c->cb_arg, NETEVENT_NOERROR, &repinfo);
		/* c->callback has to buffer_clear(). */

		/* is commpoint deleted? */
		if(!repinfo.c) {
			return 1;
		}
		/* copy waiting info */
		sldns_buffer_clear(c->buffer);
		sldns_buffer_write(c->buffer,
			sldns_buffer_begin(c->http_temp),
			sldns_buffer_remaining(c->http_temp));
		sldns_buffer_flip(c->buffer);
		/* process end of chunk trailer header lines, until
		 * an empty line */
		c->http_in_chunk_headers = 3;
		/* process more data in buffer (if any) */
		return 2;
	}
	c->tcp_byte_count -= got_now;

	/* if we have the buffer space,
	 * read more data collected into the buffer */
	remainbufferlen = sldns_buffer_capacity(c->buffer) -
		sldns_buffer_limit(c->buffer);
	if(remainbufferlen >= c->tcp_byte_count ||
		remainbufferlen >= 2048) {
		size_t total = sldns_buffer_limit(c->buffer);
		sldns_buffer_clear(c->buffer);
		sldns_buffer_set_position(c->buffer, total);
		c->http_stored = total;
		/* return and wait to read more */
		return 1;
	}

	/* callback of http reader for a new part of the data */
	c->http_stored = 0;
	sldns_buffer_set_position(c->buffer, 0);
	fptr_ok(fptr_whitelist_comm_point(c->callback));
	(void)(*c->callback)(c, c->cb_arg, NETEVENT_NOERROR, NULL);
	/* c->callback has to buffer_clear(c->buffer). */
	/* return and wait to read more */
	return 1;
}

#ifdef HAVE_NGHTTP2
/** Create new http2 session. Called when creating handling comm point. */
static struct http2_session* http2_session_create(struct comm_point* c)
{
	struct http2_session* session = calloc(1, sizeof(*session));
	if(!session) {
		log_err("malloc failure while creating http2 session");
		return NULL;
	}
	session->c = c;

	return session;
}
#endif

/** Delete http2 session. After closing connection or on error */
static void http2_session_delete(struct http2_session* h2_session)
{
#ifdef HAVE_NGHTTP2
	if(h2_session->callbacks)
		nghttp2_session_callbacks_del(h2_session->callbacks);
	free(h2_session);
#else
	(void)h2_session;
#endif
}

#ifdef HAVE_NGHTTP2
struct http2_stream* http2_stream_create(int32_t stream_id)
{
	struct http2_stream* h2_stream = calloc(1, sizeof(*h2_stream));
	if(!h2_stream) {
		log_err("malloc failure while creating http2 stream");
		return NULL;
	}
	h2_stream->stream_id = stream_id;
	return h2_stream;
}
#endif

void http2_stream_add_meshstate(struct http2_stream* h2_stream,
	struct mesh_area* mesh, struct mesh_state* m)
{
	h2_stream->mesh = mesh;
	h2_stream->mesh_state = m;
}

void http2_stream_remove_mesh_state(struct http2_stream* h2_stream)
{
	if(!h2_stream)
		return;
	h2_stream->mesh_state = NULL;
}

#ifdef HAVE_NGHTTP2
void http2_session_add_stream(struct http2_session* h2_session,
	struct http2_stream* h2_stream)
{
	if(h2_session->first_stream)
		h2_session->first_stream->prev = h2_stream;
	h2_stream->next = h2_session->first_stream;
	h2_session->first_stream = h2_stream;
}

/** remove stream from session linked list. After stream close callback or
 * closing connection */
static void http2_session_remove_stream(struct http2_session* h2_session,
	struct http2_stream* h2_stream)
{
	if(h2_stream->prev)
		h2_stream->prev->next = h2_stream->next;
	else
		h2_session->first_stream = h2_stream->next;
	if(h2_stream->next)
		h2_stream->next->prev = h2_stream->prev;

}

int http2_stream_close_cb(nghttp2_session* ATTR_UNUSED(session),
	int32_t stream_id, uint32_t ATTR_UNUSED(error_code), void* cb_arg)
{
	struct http2_stream* h2_stream;
	struct http2_session* h2_session = (struct http2_session*)cb_arg;
	if(!(h2_stream = nghttp2_session_get_stream_user_data(
		h2_session->session, stream_id))) {
		return 0;
	}
	http2_session_remove_stream(h2_session, h2_stream);
	http2_stream_delete(h2_session, h2_stream);
	return 0;
}

ssize_t http2_recv_cb(nghttp2_session* ATTR_UNUSED(session), uint8_t* buf,
	size_t len, int ATTR_UNUSED(flags), void* cb_arg)
{
	struct http2_session* h2_session = (struct http2_session*)cb_arg;
	ssize_t ret;

	log_assert(h2_session->c->type == comm_http);
	log_assert(h2_session->c->h2_session);

#ifdef HAVE_SSL
	if(h2_session->c->ssl) {
		int r;
		ERR_clear_error();
		r = SSL_read(h2_session->c->ssl, buf, len);
		if(r <= 0) {
			int want = SSL_get_error(h2_session->c->ssl, r);
			if(want == SSL_ERROR_ZERO_RETURN) {
				return NGHTTP2_ERR_EOF;
			} else if(want == SSL_ERROR_WANT_READ) {
				return NGHTTP2_ERR_WOULDBLOCK;
			} else if(want == SSL_ERROR_WANT_WRITE) {
				h2_session->c->ssl_shake_state = comm_ssl_shake_hs_write;
				comm_point_listen_for_rw(h2_session->c, 0, 1);
				return NGHTTP2_ERR_WOULDBLOCK;
			} else if(want == SSL_ERROR_SYSCALL) {
#ifdef ECONNRESET
				if(errno == ECONNRESET && verbosity < 2)
					return NGHTTP2_ERR_CALLBACK_FAILURE;
#endif
				if(errno != 0)
					log_err("SSL_read syscall: %s",
						strerror(errno));
				return NGHTTP2_ERR_CALLBACK_FAILURE;
			}
			log_crypto_err_io("could not SSL_read", want);
			return NGHTTP2_ERR_CALLBACK_FAILURE;
		}
		return r;
	}
#endif /* HAVE_SSL */

	ret = recv(h2_session->c->fd, buf, len, MSG_DONTWAIT);
	if(ret == 0) {
		return NGHTTP2_ERR_EOF;
	} else if(ret < 0) {
#ifndef USE_WINSOCK
		if(errno == EINTR || errno == EAGAIN)
			return NGHTTP2_ERR_WOULDBLOCK;
#ifdef ECONNRESET
		if(errno == ECONNRESET && verbosity < 2)
			return NGHTTP2_ERR_CALLBACK_FAILURE;
#endif
		log_err_addr("could not http2 recv: %s", strerror(errno),
			&h2_session->c->repinfo.remote_addr,
			h2_session->c->repinfo.remote_addrlen);
#else /* USE_WINSOCK */
		if(WSAGetLastError() == WSAECONNRESET)
			return NGHTTP2_ERR_CALLBACK_FAILURE;
		if(WSAGetLastError() == WSAEINPROGRESS)
			return NGHTTP2_ERR_WOULDBLOCK;
		if(WSAGetLastError() == WSAEWOULDBLOCK) {
			ub_winsock_tcp_wouldblock(h2_session->c->ev->ev,
				UB_EV_READ);
			return NGHTTP2_ERR_WOULDBLOCK;
		}
		log_err_addr("could not http2 recv: %s",
			wsa_strerror(WSAGetLastError()),
			&h2_session->c->repinfo.remote_addr,
			h2_session->c->repinfo.remote_addrlen);
#endif
		return NGHTTP2_ERR_CALLBACK_FAILURE;
	}
	return ret;
}
#endif /* HAVE_NGHTTP2 */

/** Handle http2 read */
static int
comm_point_http2_handle_read(int ATTR_UNUSED(fd), struct comm_point* c)
{
#ifdef HAVE_NGHTTP2
	int ret;
	log_assert(c->h2_session);

	/* reading until recv cb returns NGHTTP2_ERR_WOULDBLOCK */
	ret = nghttp2_session_recv(c->h2_session->session);
	if(ret) {
		if(ret != NGHTTP2_ERR_EOF &&
			ret != NGHTTP2_ERR_CALLBACK_FAILURE) {
			char a[256];
			addr_to_str(&c->repinfo.remote_addr,
				c->repinfo.remote_addrlen, a, sizeof(a));
			verbose(VERB_QUERY, "http2: session_recv from %s failed, "
				"error: %s", a, nghttp2_strerror(ret));
		}
		return 0;
	}
	if(nghttp2_session_want_write(c->h2_session->session)) {
		c->tcp_is_reading = 0;
		comm_point_stop_listening(c);
		comm_point_start_listening(c, -1, adjusted_tcp_timeout(c));
	} else if(!nghttp2_session_want_read(c->h2_session->session))
		return 0; /* connection can be closed */
	return 1;
#else
	(void)c;
	return 0;
#endif
}

/**
 * Handle http reading callback.
 * @param fd: file descriptor of socket.
 * @param c: comm point to read from into buffer.
 * @return: 0 on error
 */
static int
comm_point_http_handle_read(int fd, struct comm_point* c)
{
	log_assert(c->type == comm_http);
	log_assert(fd != -1);

	/* if we are in ssl handshake, handle SSL handshake */
#ifdef HAVE_SSL
	if(c->ssl && c->ssl_shake_state != comm_ssl_shake_none) {
		if(!ssl_handshake(c))
			return 0;
		if(c->ssl_shake_state != comm_ssl_shake_none)
			return 1;
	}
#endif /* HAVE_SSL */

	if(!c->tcp_is_reading)
		return 1;

	if(c->use_h2) {
		return comm_point_http2_handle_read(fd, c);
	}

	/* http version is <= http/1.1 */

	if(c->http_min_version >= http_version_2) {
		/* HTTP/2 failed, not allowed to use lower version. */
		return 0;
	}

	/* read more data */
	if(c->ssl) {
		if(!ssl_http_read_more(c))
			return 0;
	} else {
		if(!http_read_more(fd, c))
			return 0;
	}

	if(c->http_stored >= sldns_buffer_position(c->buffer)) {
		/* read did not work but we wanted more data, there is
		 * no bytes to process now. */
		return 1;
	}
	sldns_buffer_flip(c->buffer);
	/* if we are partway in a segment of data, position us at the point
	 * where we left off previously */
	if(c->http_stored < sldns_buffer_limit(c->buffer))
		sldns_buffer_set_position(c->buffer, c->http_stored);
	else	sldns_buffer_set_position(c->buffer, sldns_buffer_limit(c->buffer));

	while(sldns_buffer_remaining(c->buffer) > 0) {
		/* Handle HTTP/1.x data */
		/* if we are reading headers, read more headers */
		if(c->http_in_headers || c->http_in_chunk_headers) {
			/* if header is done, process the header */
			if(!http_header_done(c->buffer)) {
				/* copy remaining data to front of buffer
				 * and set rest for writing into it */
				http_moveover_buffer(c->buffer);
				/* return and wait to read more */
				return 1;
			}
			if(!c->http_in_chunk_headers) {
				/* process initial headers */
				if(!http_process_initial_header(c))
					return 0;
			} else {
				/* process chunk headers */
				int r = http_process_chunk_header(c);
				if(r == 0) return 0;
				if(r == 2) return 1; /* done */
				/* r == 1, continue */
			}
			/* see if we have more to process */
			continue;
		}

		if(!c->http_is_chunked) {
			/* if we are reading nonchunks, process that*/
			return http_nonchunk_segment(c);
		} else {
			/* if we are reading chunks, read the chunk */
			int r = http_chunked_segment(c);
			if(r == 0) return 0;
			if(r == 1) return 1;
			continue;
		}
	}
	/* broke out of the loop; could not process header instead need
	 * to read more */
	/* moveover any remaining data and read more data */
	http_moveover_buffer(c->buffer);
	/* return and wait to read more */
	return 1;
}

/** check pending connect for http */
static int
http_check_connect(int fd, struct comm_point* c)
{
	/* check for pending error from nonblocking connect */
	/* from Stevens, unix network programming, vol1, 3rd ed, p450*/
	int error = 0;
	socklen_t len = (socklen_t)sizeof(error);
	if(getsockopt(fd, SOL_SOCKET, SO_ERROR, (void*)&error,
		&len) < 0){
#ifndef USE_WINSOCK
		error = errno; /* on solaris errno is error */
#else /* USE_WINSOCK */
		error = WSAGetLastError();
#endif
	}
#ifndef USE_WINSOCK
#if defined(EINPROGRESS) && defined(EWOULDBLOCK)
	if(error == EINPROGRESS || error == EWOULDBLOCK)
		return 1; /* try again later */
	else
#endif
	if(error != 0 && verbosity < 2)
		return 0; /* silence lots of chatter in the logs */
	else if(error != 0) {
		log_err_addr("http connect", strerror(error),
			&c->repinfo.remote_addr, c->repinfo.remote_addrlen);
#else /* USE_WINSOCK */
	/* examine error */
	if(error == WSAEINPROGRESS)
		return 1;
	else if(error == WSAEWOULDBLOCK) {
		ub_winsock_tcp_wouldblock(c->ev->ev, UB_EV_WRITE);
		return 1;
	} else if(error != 0 && verbosity < 2)
		return 0;
	else if(error != 0) {
		log_err_addr("http connect", wsa_strerror(error),
			&c->repinfo.remote_addr, c->repinfo.remote_addrlen);
#endif /* USE_WINSOCK */
		return 0;
	}
	/* keep on processing this socket */
	return 2;
}

/** write more data for http (with ssl) */
static int
ssl_http_write_more(struct comm_point* c)
{
#ifdef HAVE_SSL
	int r;
	log_assert(sldns_buffer_remaining(c->buffer) > 0);
	ERR_clear_error();
	r = SSL_write(c->ssl, (void*)sldns_buffer_current(c->buffer),
		(int)sldns_buffer_remaining(c->buffer));
	if(r <= 0) {
		int want = SSL_get_error(c->ssl, r);
		if(want == SSL_ERROR_ZERO_RETURN) {
			return 0; /* closed */
		} else if(want == SSL_ERROR_WANT_READ) {
			c->ssl_shake_state = comm_ssl_shake_hs_read;
			comm_point_listen_for_rw(c, 1, 0);
			return 1; /* wait for read condition */
		} else if(want == SSL_ERROR_WANT_WRITE) {
			return 1; /* write more later */
		} else if(want == SSL_ERROR_SYSCALL) {
#ifdef EPIPE
			if(errno == EPIPE && verbosity < 2)
				return 0; /* silence 'broken pipe' */
#endif
			if(errno != 0)
				log_err("SSL_write syscall: %s",
					strerror(errno));
			return 0;
		}
		log_crypto_err_io("could not SSL_write", want);
		return 0;
	}
	sldns_buffer_skip(c->buffer, (ssize_t)r);
	return 1;
#else
	(void)c;
	return 0;
#endif /* HAVE_SSL */
}

/** write more data for http */
static int
http_write_more(int fd, struct comm_point* c)
{
	ssize_t r;
	log_assert(sldns_buffer_remaining(c->buffer) > 0);
	r = send(fd, (void*)sldns_buffer_current(c->buffer),
		sldns_buffer_remaining(c->buffer), 0);
	if(r == -1) {
#ifndef USE_WINSOCK
		if(errno == EINTR || errno == EAGAIN)
			return 1;
#else
		if(WSAGetLastError() == WSAEINPROGRESS)
			return 1;
		if(WSAGetLastError() == WSAEWOULDBLOCK) {
			ub_winsock_tcp_wouldblock(c->ev->ev, UB_EV_WRITE);
			return 1;
		}
#endif
		log_err_addr("http send r", sock_strerror(errno),
			&c->repinfo.remote_addr, c->repinfo.remote_addrlen);
		return 0;
	}
	sldns_buffer_skip(c->buffer, r);
	return 1;
}

#ifdef HAVE_NGHTTP2
ssize_t http2_send_cb(nghttp2_session* ATTR_UNUSED(session), const uint8_t* buf,
	size_t len, int ATTR_UNUSED(flags), void* cb_arg)
{
	ssize_t ret;
	struct http2_session* h2_session = (struct http2_session*)cb_arg;
	log_assert(h2_session->c->type == comm_http);
	log_assert(h2_session->c->h2_session);

#ifdef HAVE_SSL
	if(h2_session->c->ssl) {
		int r;
		ERR_clear_error();
		r = SSL_write(h2_session->c->ssl, buf, len);
		if(r <= 0) {
			int want = SSL_get_error(h2_session->c->ssl, r);
			if(want == SSL_ERROR_ZERO_RETURN) {
				return NGHTTP2_ERR_CALLBACK_FAILURE;
			} else if(want == SSL_ERROR_WANT_READ) {
				h2_session->c->ssl_shake_state = comm_ssl_shake_hs_read;
				comm_point_listen_for_rw(h2_session->c, 1, 0);
				return NGHTTP2_ERR_WOULDBLOCK;
			} else if(want == SSL_ERROR_WANT_WRITE) {
				return NGHTTP2_ERR_WOULDBLOCK;
			} else if(want == SSL_ERROR_SYSCALL) {
#ifdef EPIPE
				if(errno == EPIPE && verbosity < 2)
					return NGHTTP2_ERR_CALLBACK_FAILURE;
#endif
				if(errno != 0)
					log_err("SSL_write syscall: %s",
						strerror(errno));
				return NGHTTP2_ERR_CALLBACK_FAILURE;
			}
			log_crypto_err_io("could not SSL_write", want);
			return NGHTTP2_ERR_CALLBACK_FAILURE;
		}
		return r;
	}
#endif /* HAVE_SSL */

	ret = send(h2_session->c->fd, buf, len, 0);
	if(ret == 0) {
		return NGHTTP2_ERR_CALLBACK_FAILURE;
	} else if(ret < 0) {
#ifndef USE_WINSOCK
		if(errno == EINTR || errno == EAGAIN)
			return NGHTTP2_ERR_WOULDBLOCK;
#ifdef EPIPE
		if(errno == EPIPE && verbosity < 2)
			return NGHTTP2_ERR_CALLBACK_FAILURE;
#endif
#ifdef ECONNRESET
		if(errno == ECONNRESET && verbosity < 2)
			return NGHTTP2_ERR_CALLBACK_FAILURE;
#endif
		log_err_addr("could not http2 write: %s", strerror(errno),
			&h2_session->c->repinfo.remote_addr,
			h2_session->c->repinfo.remote_addrlen);
#else /* USE_WINSOCK */
		if(WSAGetLastError() == WSAENOTCONN)
			return NGHTTP2_ERR_WOULDBLOCK;
		if(WSAGetLastError() == WSAEINPROGRESS)
			return NGHTTP2_ERR_WOULDBLOCK;
		if(WSAGetLastError() == WSAEWOULDBLOCK) {
			ub_winsock_tcp_wouldblock(h2_session->c->ev->ev,
				UB_EV_WRITE);
			return NGHTTP2_ERR_WOULDBLOCK;
		}
		if(WSAGetLastError() == WSAECONNRESET && verbosity < 2)
			return NGHTTP2_ERR_CALLBACK_FAILURE;
		log_err_addr("could not http2 write: %s",
			wsa_strerror(WSAGetLastError()),
			&h2_session->c->repinfo.remote_addr,
			h2_session->c->repinfo.remote_addrlen);
#endif
		return NGHTTP2_ERR_CALLBACK_FAILURE;
	}
	return ret;
}
#endif /* HAVE_NGHTTP2 */

/** Handle http2 writing */
static int
comm_point_http2_handle_write(int ATTR_UNUSED(fd), struct comm_point* c)
{
#ifdef HAVE_NGHTTP2
	int ret;
	log_assert(c->h2_session);

	ret = nghttp2_session_send(c->h2_session->session);
	if(ret) {
		verbose(VERB_QUERY, "http2: session_send failed, "
			"error: %s", nghttp2_strerror(ret));
		return 0;
	}

	if(nghttp2_session_want_read(c->h2_session->session)) {
		c->tcp_is_reading = 1;
		comm_point_stop_listening(c);
		comm_point_start_listening(c, -1, adjusted_tcp_timeout(c));
	} else if(!nghttp2_session_want_write(c->h2_session->session))
		return 0; /* connection can be closed */
	return 1;
#else
	(void)c;
	return 0;
#endif
}

/**
 * Handle http writing callback.
 * @param fd: file descriptor of socket.
 * @param c: comm point to write buffer out of.
 * @return: 0 on error
 */
static int
comm_point_http_handle_write(int fd, struct comm_point* c)
{
	log_assert(c->type == comm_http);
	log_assert(fd != -1);

	/* check pending connect errors, if that fails, we wait for more,
	 * or we can continue to write contents */
	if(c->tcp_check_nb_connect) {
		int r = http_check_connect(fd, c);
		if(r == 0) return 0;
		if(r == 1) return 1;
		c->tcp_check_nb_connect = 0;
	}
	/* if we are in ssl handshake, handle SSL handshake */
#ifdef HAVE_SSL
	if(c->ssl && c->ssl_shake_state != comm_ssl_shake_none) {
		if(!ssl_handshake(c))
			return 0;
		if(c->ssl_shake_state != comm_ssl_shake_none)
			return 1;
	}
#endif /* HAVE_SSL */
	if(c->tcp_is_reading)
		return 1;

	if(c->use_h2) {
		return comm_point_http2_handle_write(fd, c);
	}

	/* http version is <= http/1.1 */

	if(c->http_min_version >= http_version_2) {
		/* HTTP/2 failed, not allowed to use lower version. */
		return 0;
	}

	/* if we are writing, write more */
	if(c->ssl) {
		if(!ssl_http_write_more(c))
			return 0;
	} else {
		if(!http_write_more(fd, c))
			return 0;
	}

	/* we write a single buffer contents, that can contain
	 * the http request, and then flip to read the results */
	/* see if write is done */
	if(sldns_buffer_remaining(c->buffer) == 0) {
		sldns_buffer_clear(c->buffer);
		if(c->tcp_do_toggle_rw)
			c->tcp_is_reading = 1;
		c->tcp_byte_count = 0;
		/* switch from listening(write) to listening(read) */
		comm_point_stop_listening(c);
		comm_point_start_listening(c, -1, -1);
	}
	return 1;
}

void
comm_point_http_handle_callback(int fd, short event, void* arg)
{
	struct comm_point* c = (struct comm_point*)arg;
	log_assert(c->type == comm_http);
	ub_comm_base_now(c->ev->base);

	if(event&UB_EV_TIMEOUT) {
		verbose(VERB_QUERY, "http took too long, dropped");
		reclaim_http_handler(c);
		if(!c->tcp_do_close) {
			fptr_ok(fptr_whitelist_comm_point(c->callback));
			(void)(*c->callback)(c, c->cb_arg,
				NETEVENT_TIMEOUT, NULL);
		}
		return;
	}
	if(event&UB_EV_READ) {
		if(!comm_point_http_handle_read(fd, c)) {
			reclaim_http_handler(c);
			if(!c->tcp_do_close) {
				fptr_ok(fptr_whitelist_comm_point(
					c->callback));
				(void)(*c->callback)(c, c->cb_arg,
					NETEVENT_CLOSED, NULL);
			}
		}
		return;
	}
	if(event&UB_EV_WRITE) {
		if(!comm_point_http_handle_write(fd, c)) {
			reclaim_http_handler(c);
			if(!c->tcp_do_close) {
				fptr_ok(fptr_whitelist_comm_point(
					c->callback));
				(void)(*c->callback)(c, c->cb_arg,
					NETEVENT_CLOSED, NULL);
			}
		}
		return;
	}
	log_err("Ignored event %d for httphdl.", event);
}

void comm_point_local_handle_callback(int fd, short event, void* arg)
{
	struct comm_point* c = (struct comm_point*)arg;
	log_assert(c->type == comm_local);
	ub_comm_base_now(c->ev->base);

	if(event&UB_EV_READ) {
		if(!comm_point_tcp_handle_read(fd, c, 1)) {
			fptr_ok(fptr_whitelist_comm_point(c->callback));
			(void)(*c->callback)(c, c->cb_arg, NETEVENT_CLOSED,
				NULL);
		}
		return;
	}
	log_err("Ignored event %d for localhdl.", event);
}

void comm_point_raw_handle_callback(int ATTR_UNUSED(fd),
	short event, void* arg)
{
	struct comm_point* c = (struct comm_point*)arg;
	int err = NETEVENT_NOERROR;
	log_assert(c->type == comm_raw);
	ub_comm_base_now(c->ev->base);

	if(event&UB_EV_TIMEOUT)
		err = NETEVENT_TIMEOUT;
	fptr_ok(fptr_whitelist_comm_point_raw(c->callback));
	(void)(*c->callback)(c, c->cb_arg, err, NULL);
}

struct comm_point*
comm_point_create_udp(struct comm_base *base, int fd, sldns_buffer* buffer,
	int pp2_enabled, comm_point_callback_type* callback,
	void* callback_arg, struct unbound_socket* socket)
{
	struct comm_point* c = (struct comm_point*)calloc(1,
		sizeof(struct comm_point));
	short evbits;
	if(!c)
		return NULL;
	c->ev = (struct internal_event*)calloc(1,
		sizeof(struct internal_event));
	if(!c->ev) {
		free(c);
		return NULL;
	}
	c->ev->base = base;
	c->fd = fd;
	c->buffer = buffer;
	c->timeout = NULL;
	c->tcp_is_reading = 0;
	c->tcp_byte_count = 0;
	c->tcp_parent = NULL;
	c->max_tcp_count = 0;
	c->cur_tcp_count = 0;
	c->tcp_handlers = NULL;
	c->tcp_free = NULL;
	c->type = comm_udp;
	c->tcp_do_close = 0;
	c->do_not_close = 0;
	c->tcp_do_toggle_rw = 0;
	c->tcp_check_nb_connect = 0;
#ifdef USE_MSG_FASTOPEN
	c->tcp_do_fastopen = 0;
#endif
#ifdef USE_DNSCRYPT
	c->dnscrypt = 0;
	c->dnscrypt_buffer = buffer;
#endif
	c->inuse = 0;
	c->callback = callback;
	c->cb_arg = callback_arg;
	c->socket = socket;
	c->pp2_enabled = pp2_enabled;
	c->pp2_header_state = pp2_header_none;
	evbits = UB_EV_READ | UB_EV_PERSIST;
	/* ub_event stuff */
	c->ev->ev = ub_event_new(base->eb->base, c->fd, evbits,
		comm_point_udp_callback, c);
	if(c->ev->ev == NULL) {
		log_err("could not baseset udp event");
		comm_point_delete(c);
		return NULL;
	}
	if(fd!=-1 && ub_event_add(c->ev->ev, c->timeout) != 0 ) {
		log_err("could not add udp event");
		comm_point_delete(c);
		return NULL;
	}
	c->event_added = 1;
	return c;
}

#if defined(AF_INET6) && defined(IPV6_PKTINFO) && defined(HAVE_RECVMSG)
struct comm_point*
comm_point_create_udp_ancil(struct comm_base *base, int fd,
	sldns_buffer* buffer, int pp2_enabled,
	comm_point_callback_type* callback, void* callback_arg, struct unbound_socket* socket)
{
	struct comm_point* c = (struct comm_point*)calloc(1,
		sizeof(struct comm_point));
	short evbits;
	if(!c)
		return NULL;
	c->ev = (struct internal_event*)calloc(1,
		sizeof(struct internal_event));
	if(!c->ev) {
		free(c);
		return NULL;
	}
	c->ev->base = base;
	c->fd = fd;
	c->buffer = buffer;
	c->timeout = NULL;
	c->tcp_is_reading = 0;
	c->tcp_byte_count = 0;
	c->tcp_parent = NULL;
	c->max_tcp_count = 0;
	c->cur_tcp_count = 0;
	c->tcp_handlers = NULL;
	c->tcp_free = NULL;
	c->type = comm_udp;
	c->tcp_do_close = 0;
	c->do_not_close = 0;
#ifdef USE_DNSCRYPT
	c->dnscrypt = 0;
	c->dnscrypt_buffer = buffer;
#endif
	c->inuse = 0;
	c->tcp_do_toggle_rw = 0;
	c->tcp_check_nb_connect = 0;
#ifdef USE_MSG_FASTOPEN
	c->tcp_do_fastopen = 0;
#endif
	c->callback = callback;
	c->cb_arg = callback_arg;
	c->socket = socket;
	c->pp2_enabled = pp2_enabled;
	c->pp2_header_state = pp2_header_none;
	evbits = UB_EV_READ | UB_EV_PERSIST;
	/* ub_event stuff */
	c->ev->ev = ub_event_new(base->eb->base, c->fd, evbits,
		comm_point_udp_ancil_callback, c);
	if(c->ev->ev == NULL) {
		log_err("could not baseset udp event");
		comm_point_delete(c);
		return NULL;
	}
	if(fd!=-1 && ub_event_add(c->ev->ev, c->timeout) != 0 ) {
		log_err("could not add udp event");
		comm_point_delete(c);
		return NULL;
	}
	c->event_added = 1;
	return c;
}
#endif

struct comm_point*
comm_point_create_doq(struct comm_base *base, int fd, sldns_buffer* buffer,
	comm_point_callback_type* callback, void* callback_arg,
	struct unbound_socket* socket, struct doq_table* table,
	struct ub_randstate* rnd, const void* quic_sslctx,
	struct config_file* cfg)
{
#ifdef HAVE_NGTCP2
	struct comm_point* c = (struct comm_point*)calloc(1,
		sizeof(struct comm_point));
	short evbits;
	if(!c)
		return NULL;
	c->ev = (struct internal_event*)calloc(1,
		sizeof(struct internal_event));
	if(!c->ev) {
		free(c);
		return NULL;
	}
	c->ev->base = base;
	c->fd = fd;
	c->buffer = buffer;
	c->timeout = NULL;
	c->tcp_is_reading = 0;
	c->tcp_byte_count = 0;
	c->tcp_parent = NULL;
	c->max_tcp_count = 0;
	c->cur_tcp_count = 0;
	c->tcp_handlers = NULL;
	c->tcp_free = NULL;
	c->type = comm_doq;
	c->tcp_do_close = 0;
	c->do_not_close = 0;
	c->tcp_do_toggle_rw = 0;
	c->tcp_check_nb_connect = 0;
#ifdef USE_MSG_FASTOPEN
	c->tcp_do_fastopen = 0;
#endif
#ifdef USE_DNSCRYPT
	c->dnscrypt = 0;
	c->dnscrypt_buffer = NULL;
#endif
	c->doq_socket = doq_server_socket_create(table, rnd, quic_sslctx, c,
		base, cfg);
	if(!c->doq_socket) {
		log_err("could not create doq comm_point");
		comm_point_delete(c);
		return NULL;
	}
	c->inuse = 0;
	c->callback = callback;
	c->cb_arg = callback_arg;
	c->socket = socket;
	c->pp2_enabled = 0;
	c->pp2_header_state = pp2_header_none;
	evbits = UB_EV_READ | UB_EV_PERSIST;
	/* ub_event stuff */
	c->ev->ev = ub_event_new(base->eb->base, c->fd, evbits,
		comm_point_doq_callback, c);
	if(c->ev->ev == NULL) {
		log_err("could not baseset udp event");
		comm_point_delete(c);
		return NULL;
	}
	if(fd!=-1 && ub_event_add(c->ev->ev, c->timeout) != 0 ) {
		log_err("could not add udp event");
		comm_point_delete(c);
		return NULL;
	}
	c->event_added = 1;
	return c;
#else
	/* no libngtcp2, so no QUIC support */
	(void)base;
	(void)buffer;
	(void)callback;
	(void)callback_arg;
	(void)socket;
	(void)rnd;
	(void)table;
	(void)quic_sslctx;
	(void)cfg;
	sock_close(fd);
	return NULL;
#endif /* HAVE_NGTCP2 */
}

static struct comm_point*
comm_point_create_tcp_handler(struct comm_base *base,
	struct comm_point* parent, size_t bufsize,
	struct sldns_buffer* spoolbuf, comm_point_callback_type* callback,
	void* callback_arg, struct unbound_socket* socket)
{
	struct comm_point* c = (struct comm_point*)calloc(1,
		sizeof(struct comm_point));
	short evbits;
	if(!c)
		return NULL;
	c->ev = (struct internal_event*)calloc(1,
		sizeof(struct internal_event));
	if(!c->ev) {
		free(c);
		return NULL;
	}
	c->ev->base = base;
	c->fd = -1;
	c->buffer = sldns_buffer_new(bufsize);
	if(!c->buffer) {
		free(c->ev);
		free(c);
		return NULL;
	}
	c->timeout = (struct timeval*)malloc(sizeof(struct timeval));
	if(!c->timeout) {
		sldns_buffer_free(c->buffer);
		free(c->ev);
		free(c);
		return NULL;
	}
	c->tcp_is_reading = 0;
	c->tcp_byte_count = 0;
	c->tcp_parent = parent;
	c->tcp_timeout_msec = parent->tcp_timeout_msec;
	c->tcp_conn_limit = parent->tcp_conn_limit;
	c->tcl_addr = NULL;
	c->tcp_keepalive = 0;
	c->max_tcp_count = 0;
	c->cur_tcp_count = 0;
	c->tcp_handlers = NULL;
	c->tcp_free = NULL;
	c->type = comm_tcp;
	c->tcp_do_close = 0;
	c->do_not_close = 0;
	c->tcp_do_toggle_rw = 1;
	c->tcp_check_nb_connect = 0;
#ifdef USE_MSG_FASTOPEN
	c->tcp_do_fastopen = 0;
#endif
#ifdef USE_DNSCRYPT
	c->dnscrypt = 0;
	/* We don't know just yet if this is a dnscrypt channel. Allocation
	 * will be done when handling the callback. */
	c->dnscrypt_buffer = c->buffer;
#endif
	c->repinfo.c = c;
	c->callback = callback;
	c->cb_arg = callback_arg;
	c->socket = socket;
	c->pp2_enabled = parent->pp2_enabled;
	c->pp2_header_state = pp2_header_none;
	if(spoolbuf) {
		c->tcp_req_info = tcp_req_info_create(spoolbuf);
		if(!c->tcp_req_info) {
			log_err("could not create tcp commpoint");
			sldns_buffer_free(c->buffer);
			free(c->timeout);
			free(c->ev);
			free(c);
			return NULL;
		}
		c->tcp_req_info->cp = c;
		c->tcp_do_close = 1;
		c->tcp_do_toggle_rw = 0;
	}
	/* add to parent free list */
	c->tcp_free = parent->tcp_free;
	parent->tcp_free = c;
	/* ub_event stuff */
	evbits = UB_EV_PERSIST | UB_EV_READ | UB_EV_TIMEOUT;
	c->ev->ev = ub_event_new(base->eb->base, c->fd, evbits,
		comm_point_tcp_handle_callback, c);
	if(c->ev->ev == NULL)
	{
		log_err("could not basetset tcphdl event");
		parent->tcp_free = c->tcp_free;
		tcp_req_info_delete(c->tcp_req_info);
		sldns_buffer_free(c->buffer);
		free(c->timeout);
		free(c->ev);
		free(c);
		return NULL;
	}
	return c;
}

static struct comm_point*
comm_point_create_http_handler(struct comm_base *base,
	struct comm_point* parent, size_t bufsize, int harden_large_queries,
	uint32_t http_max_streams, char* http_endpoint,
	comm_point_callback_type* callback, void* callback_arg,
	struct unbound_socket* socket)
{
	struct comm_point* c = (struct comm_point*)calloc(1,
		sizeof(struct comm_point));
	short evbits;
	if(!c)
		return NULL;
	c->ev = (struct internal_event*)calloc(1,
		sizeof(struct internal_event));
	if(!c->ev) {
		free(c);
		return NULL;
	}
	c->ev->base = base;
	c->fd = -1;
	c->buffer = sldns_buffer_new(bufsize);
	if(!c->buffer) {
		free(c->ev);
		free(c);
		return NULL;
	}
	c->timeout = (struct timeval*)malloc(sizeof(struct timeval));
	if(!c->timeout) {
		sldns_buffer_free(c->buffer);
		free(c->ev);
		free(c);
		return NULL;
	}
	c->tcp_is_reading = 0;
	c->tcp_byte_count = 0;
	c->tcp_parent = parent;
	c->tcp_timeout_msec = parent->tcp_timeout_msec;
	c->tcp_conn_limit = parent->tcp_conn_limit;
	c->tcl_addr = NULL;
	c->tcp_keepalive = 0;
	c->max_tcp_count = 0;
	c->cur_tcp_count = 0;
	c->tcp_handlers = NULL;
	c->tcp_free = NULL;
	c->type = comm_http;
	c->tcp_do_close = 1;
	c->do_not_close = 0;
	c->tcp_do_toggle_rw = 1; /* will be set to 0 after http2 upgrade */
	c->tcp_check_nb_connect = 0;
#ifdef USE_MSG_FASTOPEN
	c->tcp_do_fastopen = 0;
#endif
#ifdef USE_DNSCRYPT
	c->dnscrypt = 0;
	c->dnscrypt_buffer = NULL;
#endif
	c->repinfo.c = c;
	c->callback = callback;
	c->cb_arg = callback_arg;
	c->socket = socket;
	c->pp2_enabled = 0;
	c->pp2_header_state = pp2_header_none;

	c->http_min_version = http_version_2;
	c->http2_stream_max_qbuffer_size = bufsize;
	if(harden_large_queries && bufsize > 512)
		c->http2_stream_max_qbuffer_size = 512;
	c->http2_max_streams = http_max_streams;
	if(!(c->http_endpoint = strdup(http_endpoint))) {
		log_err("could not strdup http_endpoint");
		sldns_buffer_free(c->buffer);
		free(c->timeout);
		free(c->ev);
		free(c);
		return NULL;
	}
	c->use_h2 = 0;
#ifdef HAVE_NGHTTP2
	if(!(c->h2_session = http2_session_create(c))) {
		log_err("could not create http2 session");
		free(c->http_endpoint);
		sldns_buffer_free(c->buffer);
		free(c->timeout);
		free(c->ev);
		free(c);
		return NULL;
	}
	if(!(c->h2_session->callbacks = http2_req_callbacks_create())) {
		log_err("could not create http2 callbacks");
		http2_session_delete(c->h2_session);
		free(c->http_endpoint);
		sldns_buffer_free(c->buffer);
		free(c->timeout);
		free(c->ev);
		free(c);
		return NULL;
	}
#endif

	/* add to parent free list */
	c->tcp_free = parent->tcp_free;
	parent->tcp_free = c;
	/* ub_event stuff */
	evbits = UB_EV_PERSIST | UB_EV_READ | UB_EV_TIMEOUT;
	c->ev->ev = ub_event_new(base->eb->base, c->fd, evbits,
		comm_point_http_handle_callback, c);
	if(c->ev->ev == NULL)
	{
		log_err("could not set http handler event");
		parent->tcp_free = c->tcp_free;
		http2_session_delete(c->h2_session);
		sldns_buffer_free(c->buffer);
		free(c->timeout);
		free(c->ev);
		free(c);
		return NULL;
	}
	return c;
}

struct comm_point*
comm_point_create_tcp(struct comm_base *base, int fd, int num,
	int idle_timeout, int harden_large_queries,
	uint32_t http_max_streams, char* http_endpoint,
	struct tcl_list* tcp_conn_limit, size_t bufsize,
	struct sldns_buffer* spoolbuf, enum listen_type port_type,
	int pp2_enabled, comm_point_callback_type* callback,
	void* callback_arg, struct unbound_socket* socket)
{
	struct comm_point* c = (struct comm_point*)calloc(1,
		sizeof(struct comm_point));
	short evbits;
	int i;
	/* first allocate the TCP accept listener */
	if(!c)
		return NULL;
	c->ev = (struct internal_event*)calloc(1,
		sizeof(struct internal_event));
	if(!c->ev) {
		free(c);
		return NULL;
	}
	c->ev->base = base;
	c->fd = fd;
	c->buffer = NULL;
	c->timeout = NULL;
	c->tcp_is_reading = 0;
	c->tcp_byte_count = 0;
	c->tcp_timeout_msec = idle_timeout;
	c->tcp_conn_limit = tcp_conn_limit;
	c->tcl_addr = NULL;
	c->tcp_keepalive = 0;
	c->tcp_parent = NULL;
	c->max_tcp_count = num;
	c->cur_tcp_count = 0;
	c->tcp_handlers = (struct comm_point**)calloc((size_t)num,
		sizeof(struct comm_point*));
	if(!c->tcp_handlers) {
		free(c->ev);
		free(c);
		return NULL;
	}
	c->tcp_free = NULL;
	c->type = comm_tcp_accept;
	c->tcp_do_close = 0;
	c->do_not_close = 0;
	c->tcp_do_toggle_rw = 0;
	c->tcp_check_nb_connect = 0;
#ifdef USE_MSG_FASTOPEN
	c->tcp_do_fastopen = 0;
#endif
#ifdef USE_DNSCRYPT
	c->dnscrypt = 0;
	c->dnscrypt_buffer = NULL;
#endif
	c->callback = NULL;
	c->cb_arg = NULL;
	c->socket = socket;
	c->pp2_enabled = (port_type==listen_type_http?0:pp2_enabled);
	c->pp2_header_state = pp2_header_none;
	evbits = UB_EV_READ | UB_EV_PERSIST;
	/* ub_event stuff */
	c->ev->ev = ub_event_new(base->eb->base, c->fd, evbits,
		comm_point_tcp_accept_callback, c);
	if(c->ev->ev == NULL) {
		log_err("could not baseset tcpacc event");
		comm_point_delete(c);
		return NULL;
	}
	if (ub_event_add(c->ev->ev, c->timeout) != 0) {
		log_err("could not add tcpacc event");
		comm_point_delete(c);
		return NULL;
	}
	c->event_added = 1;
	/* now prealloc the handlers */
	for(i=0; i<num; i++) {
		if(port_type == listen_type_tcp ||
			port_type == listen_type_ssl ||
			port_type == listen_type_tcp_dnscrypt) {
			c->tcp_handlers[i] = comm_point_create_tcp_handler(base,
				c, bufsize, spoolbuf, callback, callback_arg, socket);
		} else if(port_type == listen_type_http) {
			c->tcp_handlers[i] = comm_point_create_http_handler(
				base, c, bufsize, harden_large_queries,
				http_max_streams, http_endpoint,
				callback, callback_arg, socket);
		}
		else {
			log_err("could not create tcp handler, unknown listen "
				"type");
			return NULL;
		}
		if(!c->tcp_handlers[i]) {
			comm_point_delete(c);
			return NULL;
		}
	}

	return c;
}

struct comm_point*
comm_point_create_tcp_out(struct comm_base *base, size_t bufsize,
        comm_point_callback_type* callback, void* callback_arg)
{
	struct comm_point* c = (struct comm_point*)calloc(1,
		sizeof(struct comm_point));
	short evbits;
	if(!c)
		return NULL;
	c->ev = (struct internal_event*)calloc(1,
		sizeof(struct internal_event));
	if(!c->ev) {
		free(c);
		return NULL;
	}
	c->ev->base = base;
	c->fd = -1;
	c->buffer = sldns_buffer_new(bufsize);
	if(!c->buffer) {
		free(c->ev);
		free(c);
		return NULL;
	}
	c->timeout = NULL;
	c->tcp_is_reading = 0;
	c->tcp_byte_count = 0;
	c->tcp_timeout_msec = TCP_QUERY_TIMEOUT;
	c->tcp_conn_limit = NULL;
	c->tcl_addr = NULL;
	c->tcp_keepalive = 0;
	c->tcp_parent = NULL;
	c->max_tcp_count = 0;
	c->cur_tcp_count = 0;
	c->tcp_handlers = NULL;
	c->tcp_free = NULL;
	c->type = comm_tcp;
	c->tcp_do_close = 0;
	c->do_not_close = 0;
	c->tcp_do_toggle_rw = 1;
	c->tcp_check_nb_connect = 1;
#ifdef USE_MSG_FASTOPEN
	c->tcp_do_fastopen = 1;
#endif
#ifdef USE_DNSCRYPT
	c->dnscrypt = 0;
	c->dnscrypt_buffer = c->buffer;
#endif
	c->repinfo.c = c;
	c->callback = callback;
	c->cb_arg = callback_arg;
	c->pp2_enabled = 0;
	c->pp2_header_state = pp2_header_none;
	evbits = UB_EV_PERSIST | UB_EV_WRITE;
	c->ev->ev = ub_event_new(base->eb->base, c->fd, evbits,
		comm_point_tcp_handle_callback, c);
	if(c->ev->ev == NULL)
	{
		log_err("could not baseset tcpout event");
		sldns_buffer_free(c->buffer);
		free(c->ev);
		free(c);
		return NULL;
	}

	return c;
}

struct comm_point*
comm_point_create_http_out(struct comm_base *base, size_t bufsize,
        comm_point_callback_type* callback, void* callback_arg,
	sldns_buffer* temp)
{
	struct comm_point* c = (struct comm_point*)calloc(1,
		sizeof(struct comm_point));
	short evbits;
	if(!c)
		return NULL;
	c->ev = (struct internal_event*)calloc(1,
		sizeof(struct internal_event));
	if(!c->ev) {
		free(c);
		return NULL;
	}
	c->ev->base = base;
	c->fd = -1;
	c->buffer = sldns_buffer_new(bufsize);
	if(!c->buffer) {
		free(c->ev);
		free(c);
		return NULL;
	}
	c->timeout = NULL;
	c->tcp_is_reading = 0;
	c->tcp_byte_count = 0;
	c->tcp_parent = NULL;
	c->max_tcp_count = 0;
	c->cur_tcp_count = 0;
	c->tcp_handlers = NULL;
	c->tcp_free = NULL;
	c->type = comm_http;
	c->tcp_do_close = 0;
	c->do_not_close = 0;
	c->tcp_do_toggle_rw = 1;
	c->tcp_check_nb_connect = 1;
	c->http_in_headers = 1;
	c->http_in_chunk_headers = 0;
	c->http_is_chunked = 0;
	c->http_temp = temp;
#ifdef USE_MSG_FASTOPEN
	c->tcp_do_fastopen = 1;
#endif
#ifdef USE_DNSCRYPT
	c->dnscrypt = 0;
	c->dnscrypt_buffer = c->buffer;
#endif
	c->repinfo.c = c;
	c->callback = callback;
	c->cb_arg = callback_arg;
	c->pp2_enabled = 0;
	c->pp2_header_state = pp2_header_none;
	evbits = UB_EV_PERSIST | UB_EV_WRITE;
	c->ev->ev = ub_event_new(base->eb->base, c->fd, evbits,
		comm_point_http_handle_callback, c);
	if(c->ev->ev == NULL)
	{
		log_err("could not baseset tcpout event");
#ifdef HAVE_SSL
		SSL_free(c->ssl);
#endif
		sldns_buffer_free(c->buffer);
		free(c->ev);
		free(c);
		return NULL;
	}

	return c;
}

struct comm_point*
comm_point_create_local(struct comm_base *base, int fd, size_t bufsize,
        comm_point_callback_type* callback, void* callback_arg)
{
	struct comm_point* c = (struct comm_point*)calloc(1,
		sizeof(struct comm_point));
	short evbits;
	if(!c)
		return NULL;
	c->ev = (struct internal_event*)calloc(1,
		sizeof(struct internal_event));
	if(!c->ev) {
		free(c);
		return NULL;
	}
	c->ev->base = base;
	c->fd = fd;
	c->buffer = sldns_buffer_new(bufsize);
	if(!c->buffer) {
		free(c->ev);
		free(c);
		return NULL;
	}
	c->timeout = NULL;
	c->tcp_is_reading = 1;
	c->tcp_byte_count = 0;
	c->tcp_parent = NULL;
	c->max_tcp_count = 0;
	c->cur_tcp_count = 0;
	c->tcp_handlers = NULL;
	c->tcp_free = NULL;
	c->type = comm_local;
	c->tcp_do_close = 0;
	c->do_not_close = 1;
	c->tcp_do_toggle_rw = 0;
	c->tcp_check_nb_connect = 0;
#ifdef USE_MSG_FASTOPEN
	c->tcp_do_fastopen = 0;
#endif
#ifdef USE_DNSCRYPT
	c->dnscrypt = 0;
	c->dnscrypt_buffer = c->buffer;
#endif
	c->callback = callback;
	c->cb_arg = callback_arg;
	c->pp2_enabled = 0;
	c->pp2_header_state = pp2_header_none;
	/* ub_event stuff */
	evbits = UB_EV_PERSIST | UB_EV_READ;
	c->ev->ev = ub_event_new(base->eb->base, c->fd, evbits,
		comm_point_local_handle_callback, c);
	if(c->ev->ev == NULL) {
		log_err("could not baseset localhdl event");
		free(c->ev);
		free(c);
		return NULL;
	}
	if (ub_event_add(c->ev->ev, c->timeout) != 0) {
		log_err("could not add localhdl event");
		ub_event_free(c->ev->ev);
		free(c->ev);
		free(c);
		return NULL;
	}
	c->event_added = 1;
	return c;
}

struct comm_point*
comm_point_create_raw(struct comm_base* base, int fd, int writing,
	comm_point_callback_type* callback, void* callback_arg)
{
	struct comm_point* c = (struct comm_point*)calloc(1,
		sizeof(struct comm_point));
	short evbits;
	if(!c)
		return NULL;
	c->ev = (struct internal_event*)calloc(1,
		sizeof(struct internal_event));
	if(!c->ev) {
		free(c);
		return NULL;
	}
	c->ev->base = base;
	c->fd = fd;
	c->buffer = NULL;
	c->timeout = NULL;
	c->tcp_is_reading = 0;
	c->tcp_byte_count = 0;
	c->tcp_parent = NULL;
	c->max_tcp_count = 0;
	c->cur_tcp_count = 0;
	c->tcp_handlers = NULL;
	c->tcp_free = NULL;
	c->type = comm_raw;
	c->tcp_do_close = 0;
	c->do_not_close = 1;
	c->tcp_do_toggle_rw = 0;
	c->tcp_check_nb_connect = 0;
#ifdef USE_MSG_FASTOPEN
	c->tcp_do_fastopen = 0;
#endif
#ifdef USE_DNSCRYPT
	c->dnscrypt = 0;
	c->dnscrypt_buffer = c->buffer;
#endif
	c->callback = callback;
	c->cb_arg = callback_arg;
	c->pp2_enabled = 0;
	c->pp2_header_state = pp2_header_none;
	/* ub_event stuff */
	if(writing)
		evbits = UB_EV_PERSIST | UB_EV_WRITE;
	else 	evbits = UB_EV_PERSIST | UB_EV_READ;
	c->ev->ev = ub_event_new(base->eb->base, c->fd, evbits,
		comm_point_raw_handle_callback, c);
	if(c->ev->ev == NULL) {
		log_err("could not baseset rawhdl event");
		free(c->ev);
		free(c);
		return NULL;
	}
	if (ub_event_add(c->ev->ev, c->timeout) != 0) {
		log_err("could not add rawhdl event");
		ub_event_free(c->ev->ev);
		free(c->ev);
		free(c);
		return NULL;
	}
	c->event_added = 1;
	return c;
}

void
comm_point_close(struct comm_point* c)
{
	if(!c)
		return;
	if(c->fd != -1) {
		verbose(5, "comm_point_close of %d: event_del", c->fd);
		if(c->event_added) {
			if(ub_event_del(c->ev->ev) != 0) {
				log_err("could not event_del on close");
			}
			c->event_added = 0;
		}
	}
	tcl_close_connection(c->tcl_addr);
	if(c->tcp_req_info)
		tcp_req_info_clear(c->tcp_req_info);
	if(c->h2_session)
		http2_session_server_delete(c->h2_session);
	/* stop the comm point from reading or writing after it is closed. */
	if(c->tcp_more_read_again && *c->tcp_more_read_again)
		*c->tcp_more_read_again = 0;
	if(c->tcp_more_write_again && *c->tcp_more_write_again)
		*c->tcp_more_write_again = 0;

	/* close fd after removing from event lists, or epoll.. is messed up */
	if(c->fd != -1 && !c->do_not_close) {
#ifdef USE_WINSOCK
		if(c->type == comm_tcp || c->type == comm_http) {
			/* delete sticky events for the fd, it gets closed */
			ub_winsock_tcp_wouldblock(c->ev->ev, UB_EV_READ);
			ub_winsock_tcp_wouldblock(c->ev->ev, UB_EV_WRITE);
		}
#endif
		verbose(VERB_ALGO, "close fd %d", c->fd);
		sock_close(c->fd);
	}
	c->fd = -1;
}

void
comm_point_delete(struct comm_point* c)
{
	if(!c)
		return;
	if((c->type == comm_tcp || c->type == comm_http) && c->ssl) {
#ifdef HAVE_SSL
		SSL_shutdown(c->ssl);
		SSL_free(c->ssl);
#endif
	}
	if(c->type == comm_http && c->http_endpoint) {
		free(c->http_endpoint);
		c->http_endpoint = NULL;
	}
	comm_point_close(c);
	if(c->tcp_handlers) {
		int i;
		for(i=0; i<c->max_tcp_count; i++)
			comm_point_delete(c->tcp_handlers[i]);
		free(c->tcp_handlers);
	}
	free(c->timeout);
	if(c->type == comm_tcp || c->type == comm_local || c->type == comm_http) {
		sldns_buffer_free(c->buffer);
#ifdef USE_DNSCRYPT
		if(c->dnscrypt && c->dnscrypt_buffer != c->buffer) {
			sldns_buffer_free(c->dnscrypt_buffer);
		}
#endif
		if(c->tcp_req_info) {
			tcp_req_info_delete(c->tcp_req_info);
		}
		if(c->h2_session) {
			http2_session_delete(c->h2_session);
		}
	}
#ifdef HAVE_NGTCP2
	if(c->doq_socket)
		doq_server_socket_delete(c->doq_socket);
#endif
	ub_event_free(c->ev->ev);
	free(c->ev);
	free(c);
}

#ifdef USE_DNSTAP
static void
send_reply_dnstap(struct dt_env* dtenv,
	struct sockaddr* addr, socklen_t addrlen,
	struct sockaddr_storage* client_addr, socklen_t client_addrlen,
	enum comm_point_type type, void* ssl, sldns_buffer* buffer)
{
	log_addr(VERB_ALGO, "from local addr", (void*)addr, addrlen);
	log_addr(VERB_ALGO, "response to client", client_addr, client_addrlen);
	dt_msg_send_client_response(dtenv, client_addr,
		(struct sockaddr_storage*)addr, type, ssl, buffer);
}
#endif

void
comm_point_send_reply(struct comm_reply *repinfo)
{
	struct sldns_buffer* buffer;
	log_assert(repinfo && repinfo->c);
#ifdef USE_DNSCRYPT
	buffer = repinfo->c->dnscrypt_buffer;
	if(!dnsc_handle_uncurved_request(repinfo)) {
		return;
	}
#else
	buffer = repinfo->c->buffer;
#endif
	if(repinfo->c->type == comm_udp) {
		if(repinfo->srctype)
			comm_point_send_udp_msg_if(repinfo->c, buffer,
				(struct sockaddr*)&repinfo->remote_addr,
				repinfo->remote_addrlen, repinfo);
		else
			comm_point_send_udp_msg(repinfo->c, buffer,
				(struct sockaddr*)&repinfo->remote_addr,
				repinfo->remote_addrlen, 0);
#ifdef USE_DNSTAP
		/*
		 * sending src (client)/dst (local service) addresses over
		 * DNSTAP from udp callback
		 */
		if(repinfo->c->dtenv != NULL && repinfo->c->dtenv->log_client_response_messages) {
			send_reply_dnstap(repinfo->c->dtenv,
				repinfo->c->socket->addr,
				repinfo->c->socket->addrlen,
				&repinfo->client_addr, repinfo->client_addrlen,
				repinfo->c->type, repinfo->c->ssl,
				repinfo->c->buffer);
		}
#endif
	} else {
#ifdef USE_DNSTAP
		struct dt_env* dtenv =
#ifdef HAVE_NGTCP2
			repinfo->c->doq_socket
			?repinfo->c->dtenv:
#endif
			repinfo->c->tcp_parent->dtenv;
		struct sldns_buffer* dtbuffer = repinfo->c->tcp_req_info
			?repinfo->c->tcp_req_info->spool_buffer
			:repinfo->c->buffer;
#ifdef USE_DNSCRYPT
		if(repinfo->c->dnscrypt && repinfo->is_dnscrypted)
			dtbuffer = repinfo->c->buffer;
#endif
		/*
		 * sending src (client)/dst (local service) addresses over
		 * DNSTAP from other callbacks
		 */
		if(dtenv != NULL && dtenv->log_client_response_messages) {
			send_reply_dnstap(dtenv,
				repinfo->c->socket->addr,
				repinfo->c->socket->addrlen,
				&repinfo->client_addr, repinfo->client_addrlen,
				repinfo->c->type, repinfo->c->ssl,
				dtbuffer);
		}
#endif
		if(repinfo->c->tcp_req_info) {
			tcp_req_info_send_reply(repinfo->c->tcp_req_info);
		} else if(repinfo->c->use_h2) {
			if(!http2_submit_dns_response(repinfo->c->h2_session)) {
				comm_point_drop_reply(repinfo);
				return;
			}
			repinfo->c->h2_stream = NULL;
			repinfo->c->tcp_is_reading = 0;
			comm_point_stop_listening(repinfo->c);
			comm_point_start_listening(repinfo->c, -1,
				adjusted_tcp_timeout(repinfo->c));
			return;
#ifdef HAVE_NGTCP2
		} else if(repinfo->c->doq_socket) {
			doq_socket_send_reply(repinfo);
#endif
		} else {
			comm_point_start_listening(repinfo->c, -1,
				adjusted_tcp_timeout(repinfo->c));
		}
	}
}

void
comm_point_drop_reply(struct comm_reply* repinfo)
{
	if(!repinfo)
		return;
	log_assert(repinfo->c);
	log_assert(repinfo->c->type != comm_tcp_accept);
	if(repinfo->c->type == comm_udp)
		return;
	if(repinfo->c->tcp_req_info)
		repinfo->c->tcp_req_info->is_drop = 1;
	if(repinfo->c->type == comm_http) {
		if(repinfo->c->h2_session) {
			repinfo->c->h2_session->is_drop = 1;
			if(!repinfo->c->h2_session->postpone_drop)
				reclaim_http_handler(repinfo->c);
			return;
		}
		reclaim_http_handler(repinfo->c);
		return;
#ifdef HAVE_NGTCP2
	} else if(repinfo->c->doq_socket) {
		doq_socket_drop_reply(repinfo);
		return;
#endif
	}
	reclaim_tcp_handler(repinfo->c);
}

void
comm_point_stop_listening(struct comm_point* c)
{
	verbose(VERB_ALGO, "comm point stop listening %d", c->fd);
	if(c->event_added) {
		if(ub_event_del(c->ev->ev) != 0) {
			log_err("event_del error to stoplisten");
		}
		c->event_added = 0;
	}
}

void
comm_point_start_listening(struct comm_point* c, int newfd, int msec)
{
	verbose(VERB_ALGO, "comm point start listening %d (%d msec)",
		c->fd==-1?newfd:c->fd, msec);
	if(c->type == comm_tcp_accept && !c->tcp_free) {
		/* no use to start listening no free slots. */
		return;
	}
	if(c->event_added) {
		if(ub_event_del(c->ev->ev) != 0) {
			log_err("event_del error to startlisten");
		}
		c->event_added = 0;
	}
	if(msec != -1 && msec != 0) {
		if(!c->timeout) {
			c->timeout = (struct timeval*)malloc(sizeof(
				struct timeval));
			if(!c->timeout) {
				log_err("cpsl: malloc failed. No net read.");
				return;
			}
		}
		ub_event_add_bits(c->ev->ev, UB_EV_TIMEOUT);
#ifndef S_SPLINT_S /* splint fails on struct timeval. */
		c->timeout->tv_sec = msec/1000;
		c->timeout->tv_usec = (msec%1000)*1000;
#endif /* S_SPLINT_S */
	} else {
		if(msec == 0 || !c->timeout) {
			ub_event_del_bits(c->ev->ev, UB_EV_TIMEOUT);
		}
	}
	if(c->type == comm_tcp || c->type == comm_http) {
		ub_event_del_bits(c->ev->ev, UB_EV_READ|UB_EV_WRITE);
		if(c->tcp_write_and_read) {
			verbose(5, "startlistening %d mode rw", (newfd==-1?c->fd:newfd));
			ub_event_add_bits(c->ev->ev, UB_EV_READ|UB_EV_WRITE);
		} else if(c->tcp_is_reading) {
			verbose(5, "startlistening %d mode r", (newfd==-1?c->fd:newfd));
			ub_event_add_bits(c->ev->ev, UB_EV_READ);
		} else	{
			verbose(5, "startlistening %d mode w", (newfd==-1?c->fd:newfd));
			ub_event_add_bits(c->ev->ev, UB_EV_WRITE);
		}
	}
	if(newfd != -1) {
		if(c->fd != -1 && c->fd != newfd) {
			verbose(5, "cpsl close of fd %d for %d", c->fd, newfd);
			sock_close(c->fd);
		}
		c->fd = newfd;
		ub_event_set_fd(c->ev->ev, c->fd);
	}
	if(ub_event_add(c->ev->ev, msec==0?NULL:c->timeout) != 0) {
		log_err("event_add failed. in cpsl.");
		return;
	}
	c->event_added = 1;
}

void comm_point_listen_for_rw(struct comm_point* c, int rd, int wr)
{
	verbose(VERB_ALGO, "comm point listen_for_rw %d %d", c->fd, wr);
	if(c->event_added) {
		if(ub_event_del(c->ev->ev) != 0) {
			log_err("event_del error to cplf");
		}
		c->event_added = 0;
	}
	if(!c->timeout) {
		ub_event_del_bits(c->ev->ev, UB_EV_TIMEOUT);
	}
	ub_event_del_bits(c->ev->ev, UB_EV_READ|UB_EV_WRITE);
	if(rd) ub_event_add_bits(c->ev->ev, UB_EV_READ);
	if(wr) ub_event_add_bits(c->ev->ev, UB_EV_WRITE);
	if(ub_event_add(c->ev->ev, c->timeout) != 0) {
		log_err("event_add failed. in cplf.");
		return;
	}
	c->event_added = 1;
}

size_t comm_point_get_mem(struct comm_point* c)
{
	size_t s;
	if(!c)
		return 0;
	s = sizeof(*c) + sizeof(*c->ev);
	if(c->timeout)
		s += sizeof(*c->timeout);
	if(c->type == comm_tcp || c->type == comm_local) {
		s += sizeof(*c->buffer) + sldns_buffer_capacity(c->buffer);
#ifdef USE_DNSCRYPT
		s += sizeof(*c->dnscrypt_buffer);
		if(c->buffer != c->dnscrypt_buffer) {
			s += sldns_buffer_capacity(c->dnscrypt_buffer);
		}
#endif
	}
	if(c->type == comm_tcp_accept) {
		int i;
		for(i=0; i<c->max_tcp_count; i++)
			s += comm_point_get_mem(c->tcp_handlers[i]);
	}
	return s;
}

struct comm_timer*
comm_timer_create(struct comm_base* base, void (*cb)(void*), void* cb_arg)
{
	struct internal_timer *tm = (struct internal_timer*)calloc(1,
		sizeof(struct internal_timer));
	if(!tm) {
		log_err("malloc failed");
		return NULL;
	}
	tm->super.ev_timer = tm;
	tm->base = base;
	tm->super.callback = cb;
	tm->super.cb_arg = cb_arg;
	tm->ev = ub_event_new(base->eb->base, -1, UB_EV_TIMEOUT,
		comm_timer_callback, &tm->super);
	if(tm->ev == NULL) {
		log_err("timer_create: event_base_set failed.");
		free(tm);
		return NULL;
	}
	return &tm->super;
}

void
comm_timer_disable(struct comm_timer* timer)
{
	if(!timer)
		return;
	ub_timer_del(timer->ev_timer->ev);
	timer->ev_timer->enabled = 0;
}

void
comm_timer_set(struct comm_timer* timer, struct timeval* tv)
{
	log_assert(tv);
	if(timer->ev_timer->enabled)
		comm_timer_disable(timer);
	if(ub_timer_add(timer->ev_timer->ev, timer->ev_timer->base->eb->base,
		comm_timer_callback, timer, tv) != 0)
		log_err("comm_timer_set: evtimer_add failed.");
	timer->ev_timer->enabled = 1;
}

void
comm_timer_delete(struct comm_timer* timer)
{
	if(!timer)
		return;
	comm_timer_disable(timer);
	/* Free the sub struct timer->ev_timer derived from the super struct timer.
	 * i.e. assert(timer == timer->ev_timer)
	 */
	ub_event_free(timer->ev_timer->ev);
	free(timer->ev_timer);
}

void
comm_timer_callback(int ATTR_UNUSED(fd), short event, void* arg)
{
	struct comm_timer* tm = (struct comm_timer*)arg;
	if(!(event&UB_EV_TIMEOUT))
		return;
	ub_comm_base_now(tm->ev_timer->base);
	tm->ev_timer->enabled = 0;
	fptr_ok(fptr_whitelist_comm_timer(tm->callback));
	(*tm->callback)(tm->cb_arg);
}

int
comm_timer_is_set(struct comm_timer* timer)
{
	return (int)timer->ev_timer->enabled;
}

size_t
comm_timer_get_mem(struct comm_timer* timer)
{
	if(!timer) return 0;
	return sizeof(struct internal_timer);
}

struct comm_signal*
comm_signal_create(struct comm_base* base,
        void (*callback)(int, void*), void* cb_arg)
{
	struct comm_signal* com = (struct comm_signal*)malloc(
		sizeof(struct comm_signal));
	if(!com) {
		log_err("malloc failed");
		return NULL;
	}
	com->base = base;
	com->callback = callback;
	com->cb_arg = cb_arg;
	com->ev_signal = NULL;
	return com;
}

void
comm_signal_callback(int sig, short event, void* arg)
{
	struct comm_signal* comsig = (struct comm_signal*)arg;
	if(!(event & UB_EV_SIGNAL))
		return;
	ub_comm_base_now(comsig->base);
	fptr_ok(fptr_whitelist_comm_signal(comsig->callback));
	(*comsig->callback)(sig, comsig->cb_arg);
}

int
comm_signal_bind(struct comm_signal* comsig, int sig)
{
	struct internal_signal* entry = (struct internal_signal*)calloc(1,
		sizeof(struct internal_signal));
	if(!entry) {
		log_err("malloc failed");
		return 0;
	}
	log_assert(comsig);
	/* add signal event */
	entry->ev = ub_signal_new(comsig->base->eb->base, sig,
		comm_signal_callback, comsig);
	if(entry->ev == NULL) {
		log_err("Could not create signal event");
		free(entry);
		return 0;
	}
	if(ub_signal_add(entry->ev, NULL) != 0) {
		log_err("Could not add signal handler");
		ub_event_free(entry->ev);
		free(entry);
		return 0;
	}
	/* link into list */
	entry->next = comsig->ev_signal;
	comsig->ev_signal = entry;
	return 1;
}

void
comm_signal_delete(struct comm_signal* comsig)
{
	struct internal_signal* p, *np;
	if(!comsig)
		return;
	p=comsig->ev_signal;
	while(p) {
		np = p->next;
		ub_signal_del(p->ev);
		ub_event_free(p->ev);
		free(p);
		p = np;
	}
	free(comsig);
}
