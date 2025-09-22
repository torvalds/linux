/*
 * services/listen_dnsport.c - listen on port 53 for incoming DNS queries.
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
#include "config.h"
#ifdef HAVE_SYS_TYPES_H
#  include <sys/types.h>
#endif
#include <sys/time.h>
#include <limits.h>
#ifdef USE_TCP_FASTOPEN
#include <netinet/tcp.h>
#endif
#include <ctype.h>
#include "services/listen_dnsport.h"
#include "services/outside_network.h"
#include "util/netevent.h"
#include "util/log.h"
#include "util/config_file.h"
#include "util/net_help.h"
#include "sldns/sbuffer.h"
#include "sldns/parseutil.h"
#include "sldns/wire2str.h"
#include "services/mesh.h"
#include "util/fptr_wlist.h"
#include "util/locks.h"
#include "util/timeval_func.h"

#ifdef HAVE_NETDB_H
#include <netdb.h>
#endif
#include <fcntl.h>

#ifdef HAVE_SYS_UN_H
#include <sys/un.h>
#endif

#ifdef HAVE_SYSTEMD
#include <systemd/sd-daemon.h>
#endif

#ifdef HAVE_IFADDRS_H
#include <ifaddrs.h>
#endif
#ifdef HAVE_NET_IF_H
#include <net/if.h>
#endif

#ifdef HAVE_TIME_H
#include <time.h>
#endif
#include <sys/time.h>

#ifdef HAVE_NGTCP2
#include <ngtcp2/ngtcp2.h>
#include <ngtcp2/ngtcp2_crypto.h>
#ifdef HAVE_NGTCP2_NGTCP2_CRYPTO_QUICTLS_H
#include <ngtcp2/ngtcp2_crypto_quictls.h>
#else
#include <ngtcp2/ngtcp2_crypto_openssl.h>
#endif
#endif

#ifdef HAVE_OPENSSL_SSL_H
#include <openssl/ssl.h>
#endif

#ifdef HAVE_LINUX_NET_TSTAMP_H
#include <linux/net_tstamp.h>
#endif

/** number of queued TCP connections for listen() */
#define TCP_BACKLOG 256

#ifndef THREADS_DISABLED
/** lock on the counter of stream buffer memory */
static lock_basic_type stream_wait_count_lock;
/** lock on the counter of HTTP2 query buffer memory */
static lock_basic_type http2_query_buffer_count_lock;
/** lock on the counter of HTTP2 response buffer memory */
static lock_basic_type http2_response_buffer_count_lock;
#endif
/** size (in bytes) of stream wait buffers */
static size_t stream_wait_count = 0;
/** is the lock initialised for stream wait buffers */
static int stream_wait_lock_inited = 0;
/** size (in bytes) of HTTP2 query buffers */
static size_t http2_query_buffer_count = 0;
/** is the lock initialised for HTTP2 query buffers */
static int http2_query_buffer_lock_inited = 0;
/** size (in bytes) of HTTP2 response buffers */
static size_t http2_response_buffer_count = 0;
/** is the lock initialised for HTTP2 response buffers */
static int http2_response_buffer_lock_inited = 0;

/**
 * Debug print of the getaddrinfo returned address.
 * @param addr: the address returned.
 * @param additional: additional text that describes the type of socket,
 * 	or NULL for no text.
 */
static void
verbose_print_addr(struct addrinfo *addr, const char* additional)
{
	if(verbosity >= VERB_ALGO) {
		char buf[100];
		void* sinaddr = &((struct sockaddr_in*)addr->ai_addr)->sin_addr;
#ifdef INET6
		if(addr->ai_family == AF_INET6)
			sinaddr = &((struct sockaddr_in6*)addr->ai_addr)->
				sin6_addr;
#endif /* INET6 */
		if(inet_ntop(addr->ai_family, sinaddr, buf,
			(socklen_t)sizeof(buf)) == 0) {
			(void)strlcpy(buf, "(null)", sizeof(buf));
		}
		buf[sizeof(buf)-1] = 0;
		verbose(VERB_ALGO, "creating %s%s socket %s %d%s%s",
			addr->ai_socktype==SOCK_DGRAM?"udp":
			addr->ai_socktype==SOCK_STREAM?"tcp":"otherproto",
			addr->ai_family==AF_INET?"4":
			addr->ai_family==AF_INET6?"6":
			"_otherfam", buf,
			ntohs(((struct sockaddr_in*)addr->ai_addr)->sin_port),
			(additional?" ":""), (additional?additional:""));
	}
}

void
verbose_print_unbound_socket(struct unbound_socket* ub_sock)
{
	if(verbosity >= VERB_ALGO) {
		char buf[256];
		log_info("listing of unbound_socket structure:");
		addr_to_str((void*)ub_sock->addr, ub_sock->addrlen, buf,
			sizeof(buf));
		log_info("%s s is: %d, fam is: %s, acl: %s", buf, ub_sock->s,
			ub_sock->fam == AF_INET?"AF_INET":"AF_INET6",
			ub_sock->acl?"yes":"no");
	}
}

#ifdef HAVE_SYSTEMD
static int
systemd_get_activated(int family, int socktype, int listen,
		      struct sockaddr *addr, socklen_t addrlen,
		      const char *path)
{
	int i = 0;
	int r = 0;
	int s = -1;
	const char* listen_pid, *listen_fds;

	/* We should use "listen" option only for stream protocols. For UDP it should be -1 */

	if((r = sd_booted()) < 1) {
		if(r == 0)
			log_warn("systemd is not running");
		else
			log_err("systemd sd_booted(): %s", strerror(-r));
		return -1;
	}

	listen_pid = getenv("LISTEN_PID");
	listen_fds = getenv("LISTEN_FDS");

	if (!listen_pid) {
		log_warn("Systemd mandatory ENV variable is not defined: LISTEN_PID");
		return -1;
	}

	if (!listen_fds) {
		log_warn("Systemd mandatory ENV variable is not defined: LISTEN_FDS");
		return -1;
	}

	if((r = sd_listen_fds(0)) < 1) {
		if(r == 0)
			log_warn("systemd: did not return socket, check unit configuration");
		else
			log_err("systemd sd_listen_fds(): %s", strerror(-r));
		return -1;
	}

	for(i = 0; i < r; i++) {
		if(sd_is_socket(SD_LISTEN_FDS_START + i, family, socktype, listen)) {
			s = SD_LISTEN_FDS_START + i;
			break;
		}
	}
	if (s == -1) {
		if (addr)
			log_err_addr("systemd sd_listen_fds()",
				     "no such socket",
				     (struct sockaddr_storage *)addr, addrlen);
		else
			log_err("systemd sd_listen_fds(): %s", path);
	}
	return s;
}
#endif

int
create_udp_sock(int family, int socktype, struct sockaddr* addr,
        socklen_t addrlen, int v6only, int* inuse, int* noproto,
	int rcv, int snd, int listen, int* reuseport, int transparent,
	int freebind, int use_systemd, int dscp)
{
	int s;
	char* err;
#if defined(SO_REUSEADDR) || defined(SO_REUSEPORT) || defined(IPV6_USE_MIN_MTU)  || defined(IP_TRANSPARENT) || defined(IP_BINDANY) || defined(IP_FREEBIND) || defined (SO_BINDANY)
	int on=1;
#endif
#ifdef IPV6_MTU
	int mtu = IPV6_MIN_MTU;
#endif
#if !defined(SO_RCVBUFFORCE) && !defined(SO_RCVBUF)
	(void)rcv;
#endif
#if !defined(SO_SNDBUFFORCE) && !defined(SO_SNDBUF)
	(void)snd;
#endif
#ifndef IPV6_V6ONLY
	(void)v6only;
#endif
#if !defined(IP_TRANSPARENT) && !defined(IP_BINDANY) && !defined(SO_BINDANY)
	(void)transparent;
#endif
#if !defined(IP_FREEBIND)
	(void)freebind;
#endif
#ifdef HAVE_SYSTEMD
	int got_fd_from_systemd = 0;

	if (!use_systemd
	    || (use_systemd
		&& (s = systemd_get_activated(family, socktype, -1, addr,
					      addrlen, NULL)) == -1)) {
#else
	(void)use_systemd;
#endif
	if((s = socket(family, socktype, 0)) == -1) {
		*inuse = 0;
#ifndef USE_WINSOCK
		if(errno == EAFNOSUPPORT || errno == EPROTONOSUPPORT) {
			*noproto = 1;
			return -1;
		}
#else
		if(WSAGetLastError() == WSAEAFNOSUPPORT ||
			WSAGetLastError() == WSAEPROTONOSUPPORT) {
			*noproto = 1;
			return -1;
		}
#endif
		log_err("can't create socket: %s", sock_strerror(errno));
		*noproto = 0;
		return -1;
	}
#ifdef HAVE_SYSTEMD
	} else {
		got_fd_from_systemd = 1;
	}
#endif
	if(listen) {
#ifdef SO_REUSEADDR
		if(setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (void*)&on,
			(socklen_t)sizeof(on)) < 0) {
			log_err("setsockopt(.. SO_REUSEADDR ..) failed: %s",
				sock_strerror(errno));
#ifndef USE_WINSOCK
			if(errno != ENOSYS) {
				close(s);
				*noproto = 0;
				*inuse = 0;
				return -1;
			}
#else
			closesocket(s);
			*noproto = 0;
			*inuse = 0;
			return -1;
#endif
		}
#endif /* SO_REUSEADDR */
#ifdef SO_REUSEPORT
#  ifdef SO_REUSEPORT_LB
		/* on FreeBSD 12 we have SO_REUSEPORT_LB that does loadbalance
		 * like SO_REUSEPORT on Linux.  This is what the users want
		 * with the config option in unbound.conf; if we actually
		 * need local address and port reuse they'll also need to
		 * have SO_REUSEPORT set for them, assume it was _LB they want.
		 */
		if (reuseport && *reuseport &&
		    setsockopt(s, SOL_SOCKET, SO_REUSEPORT_LB, (void*)&on,
			(socklen_t)sizeof(on)) < 0) {
#ifdef ENOPROTOOPT
			if(errno != ENOPROTOOPT || verbosity >= 3)
				log_warn("setsockopt(.. SO_REUSEPORT_LB ..) failed: %s",
					strerror(errno));
#endif
			/* this option is not essential, we can continue */
			*reuseport = 0;
		}
#  else /* no SO_REUSEPORT_LB */

		/* try to set SO_REUSEPORT so that incoming
		 * queries are distributed evenly among the receiving threads.
		 * Each thread must have its own socket bound to the same port,
		 * with SO_REUSEPORT set on each socket.
		 */
		if (reuseport && *reuseport &&
		    setsockopt(s, SOL_SOCKET, SO_REUSEPORT, (void*)&on,
			(socklen_t)sizeof(on)) < 0) {
#ifdef ENOPROTOOPT
			if(errno != ENOPROTOOPT || verbosity >= 3)
				log_warn("setsockopt(.. SO_REUSEPORT ..) failed: %s",
					strerror(errno));
#endif
			/* this option is not essential, we can continue */
			*reuseport = 0;
		}
#  endif /* SO_REUSEPORT_LB */
#else
		(void)reuseport;
#endif /* defined(SO_REUSEPORT) */
#ifdef IP_TRANSPARENT
		if (transparent &&
		    setsockopt(s, IPPROTO_IP, IP_TRANSPARENT, (void*)&on,
		    (socklen_t)sizeof(on)) < 0) {
			log_warn("setsockopt(.. IP_TRANSPARENT ..) failed: %s",
			strerror(errno));
		}
#elif defined(IP_BINDANY)
		if (transparent &&
		    setsockopt(s, (family==AF_INET6? IPPROTO_IPV6:IPPROTO_IP),
		    (family == AF_INET6? IPV6_BINDANY:IP_BINDANY),
		    (void*)&on, (socklen_t)sizeof(on)) < 0) {
			log_warn("setsockopt(.. IP%s_BINDANY ..) failed: %s",
			(family==AF_INET6?"V6":""), strerror(errno));
		}
#elif defined(SO_BINDANY)
		if (transparent &&
		    setsockopt(s, SOL_SOCKET, SO_BINDANY, (void*)&on,
		    (socklen_t)sizeof(on)) < 0) {
			log_warn("setsockopt(.. SO_BINDANY ..) failed: %s",
			strerror(errno));
		}
#endif /* IP_TRANSPARENT || IP_BINDANY || SO_BINDANY */
	}
#ifdef IP_FREEBIND
	if(freebind &&
	    setsockopt(s, IPPROTO_IP, IP_FREEBIND, (void*)&on,
	    (socklen_t)sizeof(on)) < 0) {
		log_warn("setsockopt(.. IP_FREEBIND ..) failed: %s",
		strerror(errno));
	}
#endif /* IP_FREEBIND */
	if(rcv) {
#ifdef SO_RCVBUF
		int got;
		socklen_t slen = (socklen_t)sizeof(got);
#  ifdef SO_RCVBUFFORCE
		/* Linux specific: try to use root permission to override
		 * system limits on rcvbuf. The limit is stored in
		 * /proc/sys/net/core/rmem_max or sysctl net.core.rmem_max */
		if(setsockopt(s, SOL_SOCKET, SO_RCVBUFFORCE, (void*)&rcv,
			(socklen_t)sizeof(rcv)) < 0) {
			if(errno != EPERM) {
				log_err("setsockopt(..., SO_RCVBUFFORCE, "
					"...) failed: %s", sock_strerror(errno));
				sock_close(s);
				*noproto = 0;
				*inuse = 0;
				return -1;
			}
#  endif /* SO_RCVBUFFORCE */
			if(setsockopt(s, SOL_SOCKET, SO_RCVBUF, (void*)&rcv,
				(socklen_t)sizeof(rcv)) < 0) {
				log_err("setsockopt(..., SO_RCVBUF, "
					"...) failed: %s", sock_strerror(errno));
				sock_close(s);
				*noproto = 0;
				*inuse = 0;
				return -1;
			}
			/* check if we got the right thing or if system
			 * reduced to some system max.  Warn if so */
			if(getsockopt(s, SOL_SOCKET, SO_RCVBUF, (void*)&got,
				&slen) >= 0 && got < rcv/2) {
				log_warn("so-rcvbuf %u was not granted. "
					"Got %u. To fix: start with "
					"root permissions(linux) or sysctl "
					"bigger net.core.rmem_max(linux) or "
					"kern.ipc.maxsockbuf(bsd) values.",
					(unsigned)rcv, (unsigned)got);
			}
#  ifdef SO_RCVBUFFORCE
		}
#  endif
#endif /* SO_RCVBUF */
	}
	/* first do RCVBUF as the receive buffer is more important */
	if(snd) {
#ifdef SO_SNDBUF
		int got;
		socklen_t slen = (socklen_t)sizeof(got);
#  ifdef SO_SNDBUFFORCE
		/* Linux specific: try to use root permission to override
		 * system limits on sndbuf. The limit is stored in
		 * /proc/sys/net/core/wmem_max or sysctl net.core.wmem_max */
		if(setsockopt(s, SOL_SOCKET, SO_SNDBUFFORCE, (void*)&snd,
			(socklen_t)sizeof(snd)) < 0) {
			if(errno != EPERM) {
				log_err("setsockopt(..., SO_SNDBUFFORCE, "
					"...) failed: %s", sock_strerror(errno));
				sock_close(s);
				*noproto = 0;
				*inuse = 0;
				return -1;
			}
#  endif /* SO_SNDBUFFORCE */
			if(setsockopt(s, SOL_SOCKET, SO_SNDBUF, (void*)&snd,
				(socklen_t)sizeof(snd)) < 0) {
				log_err("setsockopt(..., SO_SNDBUF, "
					"...) failed: %s", sock_strerror(errno));
				sock_close(s);
				*noproto = 0;
				*inuse = 0;
				return -1;
			}
			/* check if we got the right thing or if system
			 * reduced to some system max.  Warn if so */
			if(getsockopt(s, SOL_SOCKET, SO_SNDBUF, (void*)&got,
				&slen) >= 0 && got < snd/2) {
				log_warn("so-sndbuf %u was not granted. "
					"Got %u. To fix: start with "
					"root permissions(linux) or sysctl "
					"bigger net.core.wmem_max(linux) or "
					"kern.ipc.maxsockbuf(bsd) values.",
					(unsigned)snd, (unsigned)got);
			}
#  ifdef SO_SNDBUFFORCE
		}
#  endif
#endif /* SO_SNDBUF */
	}
	err = set_ip_dscp(s, family, dscp);
	if(err != NULL)
		log_warn("error setting IP DiffServ codepoint %d on UDP socket: %s", dscp, err);
	if(family == AF_INET6) {
# if defined(IPV6_MTU_DISCOVER) && defined(IP_PMTUDISC_DONT)
		int omit6_set = 0;
		int action;
# endif
# if defined(IPV6_V6ONLY)
		if(v6only
#   ifdef HAVE_SYSTEMD
			/* Systemd wants to control if the socket is v6 only
			 * or both, with BindIPv6Only=default, ipv6-only or
			 * both in systemd.socket, so it is not set here. */
			&& !got_fd_from_systemd
#   endif
			) {
			int val=(v6only==2)?0:1;
			if (setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY,
				(void*)&val, (socklen_t)sizeof(val)) < 0) {
				log_err("setsockopt(..., IPV6_V6ONLY"
					", ...) failed: %s", sock_strerror(errno));
				sock_close(s);
				*noproto = 0;
				*inuse = 0;
				return -1;
			}
		}
# endif
# if defined(IPV6_USE_MIN_MTU)
		/*
		 * There is no fragmentation of IPv6 datagrams
		 * during forwarding in the network. Therefore
		 * we do not send UDP datagrams larger than
		 * the minimum IPv6 MTU of 1280 octets. The
		 * EDNS0 message length can be larger if the
		 * network stack supports IPV6_USE_MIN_MTU.
		 */
		if (setsockopt(s, IPPROTO_IPV6, IPV6_USE_MIN_MTU,
			(void*)&on, (socklen_t)sizeof(on)) < 0) {
			log_err("setsockopt(..., IPV6_USE_MIN_MTU, "
				"...) failed: %s", sock_strerror(errno));
			sock_close(s);
			*noproto = 0;
			*inuse = 0;
			return -1;
		}
# elif defined(IPV6_MTU)
#   ifndef USE_WINSOCK
		/*
		 * On Linux, to send no larger than 1280, the PMTUD is
		 * disabled by default for datagrams anyway, so we set
		 * the MTU to use.
		 */
		if (setsockopt(s, IPPROTO_IPV6, IPV6_MTU,
			(void*)&mtu, (socklen_t)sizeof(mtu)) < 0) {
			log_err("setsockopt(..., IPV6_MTU, ...) failed: %s",
				sock_strerror(errno));
			sock_close(s);
			*noproto = 0;
			*inuse = 0;
			return -1;
		}
#   elif defined(IPV6_USER_MTU)
		/* As later versions of the mingw crosscompiler define
		 * IPV6_MTU, do the same for windows but use IPV6_USER_MTU
		 * instead which is writable; IPV6_MTU is readonly there. */
		if (setsockopt(s, IPPROTO_IPV6, IPV6_USER_MTU,
			(void*)&mtu, (socklen_t)sizeof(mtu)) < 0) {
			if (WSAGetLastError() != WSAENOPROTOOPT) {
				log_err("setsockopt(..., IPV6_USER_MTU, ...) failed: %s",
					wsa_strerror(WSAGetLastError()));
				sock_close(s);
				*noproto = 0;
				*inuse = 0;
				return -1;
			}
		}
#   endif /* USE_WINSOCK */
# endif /* IPv6 MTU */
# if defined(IPV6_MTU_DISCOVER) && defined(IP_PMTUDISC_DONT)
#  if defined(IP_PMTUDISC_OMIT)
		action = IP_PMTUDISC_OMIT;
		if (setsockopt(s, IPPROTO_IPV6, IPV6_MTU_DISCOVER,
			&action, (socklen_t)sizeof(action)) < 0) {

			if (errno != EINVAL) {
				log_err("setsockopt(..., IPV6_MTU_DISCOVER, IP_PMTUDISC_OMIT...) failed: %s",
					strerror(errno));
				sock_close(s);
				*noproto = 0;
				*inuse = 0;
				return -1;
			}
		}
		else
		{
		    omit6_set = 1;
		}
#  endif
		if (omit6_set == 0) {
			action = IP_PMTUDISC_DONT;
			if (setsockopt(s, IPPROTO_IPV6, IPV6_MTU_DISCOVER,
				&action, (socklen_t)sizeof(action)) < 0) {
				log_err("setsockopt(..., IPV6_MTU_DISCOVER, IP_PMTUDISC_DONT...) failed: %s",
					strerror(errno));
				sock_close(s);
				*noproto = 0;
				*inuse = 0;
				return -1;
			}
		}
# endif /* IPV6_MTU_DISCOVER */
	} else if(family == AF_INET) {
#  if defined(IP_MTU_DISCOVER) && defined(IP_PMTUDISC_DONT)
/* linux 3.15 has IP_PMTUDISC_OMIT, Hannes Frederic Sowa made it so that
 * PMTU information is not accepted, but fragmentation is allowed
 * if and only if the packet size exceeds the outgoing interface MTU
 * (and also uses the interface mtu to determine the size of the packets).
 * So there won't be any EMSGSIZE error.  Against DNS fragmentation attacks.
 * FreeBSD already has same semantics without setting the option. */
		int omit_set = 0;
		int action;
#   if defined(IP_PMTUDISC_OMIT)
		action = IP_PMTUDISC_OMIT;
		if (setsockopt(s, IPPROTO_IP, IP_MTU_DISCOVER,
			&action, (socklen_t)sizeof(action)) < 0) {

			if (errno != EINVAL) {
				log_err("setsockopt(..., IP_MTU_DISCOVER, IP_PMTUDISC_OMIT...) failed: %s",
					strerror(errno));
				sock_close(s);
				*noproto = 0;
				*inuse = 0;
				return -1;
			}
		}
		else
		{
		    omit_set = 1;
		}
#   endif
		if (omit_set == 0) {
   			action = IP_PMTUDISC_DONT;
			if (setsockopt(s, IPPROTO_IP, IP_MTU_DISCOVER,
				&action, (socklen_t)sizeof(action)) < 0) {
				log_err("setsockopt(..., IP_MTU_DISCOVER, IP_PMTUDISC_DONT...) failed: %s",
					strerror(errno));
				sock_close(s);
				*noproto = 0;
				*inuse = 0;
				return -1;
			}
		}
#  elif defined(IP_DONTFRAG) && !defined(__APPLE__)
		/* the IP_DONTFRAG option if defined in the 11.0 OSX headers,
		 * but does not work on that version, so we exclude it */
		/* a nonzero value disables fragmentation, according to
		 * docs.oracle.com for ip(4). */
		int off = 1;
		if (setsockopt(s, IPPROTO_IP, IP_DONTFRAG,
			&off, (socklen_t)sizeof(off)) < 0) {
			log_err("setsockopt(..., IP_DONTFRAG, ...) failed: %s",
				strerror(errno));
			sock_close(s);
			*noproto = 0;
			*inuse = 0;
			return -1;
		}
#  endif /* IPv4 MTU */
	}
	if(
#ifdef HAVE_SYSTEMD
		!got_fd_from_systemd &&
#endif
		bind(s, (struct sockaddr*)addr, addrlen) != 0) {
		*noproto = 0;
		*inuse = 0;
#ifndef USE_WINSOCK
#ifdef EADDRINUSE
		*inuse = (errno == EADDRINUSE);
		/* detect freebsd jail with no ipv6 permission */
		if(family==AF_INET6 && errno==EINVAL)
			*noproto = 1;
		else if(errno != EADDRINUSE &&
			!(errno == EACCES && verbosity < 4 && !listen)
#ifdef EADDRNOTAVAIL
			&& !(errno == EADDRNOTAVAIL && verbosity < 4 && !listen)
#endif
			) {
			log_err_addr("can't bind socket", strerror(errno),
				(struct sockaddr_storage*)addr, addrlen);
		}
#endif /* EADDRINUSE */
#else /* USE_WINSOCK */
		if(WSAGetLastError() != WSAEADDRINUSE &&
			WSAGetLastError() != WSAEADDRNOTAVAIL &&
			!(WSAGetLastError() == WSAEACCES && verbosity < 4 && !listen)) {
			log_err_addr("can't bind socket",
				wsa_strerror(WSAGetLastError()),
				(struct sockaddr_storage*)addr, addrlen);
		}
#endif /* USE_WINSOCK */
		sock_close(s);
		return -1;
	}
	if(!fd_set_nonblock(s)) {
		*noproto = 0;
		*inuse = 0;
		sock_close(s);
		return -1;
	}
	return s;
}

int
create_tcp_accept_sock(struct addrinfo *addr, int v6only, int* noproto,
	int* reuseport, int transparent, int mss, int nodelay, int freebind,
	int use_systemd, int dscp, const char* additional)
{
	int s = -1;
	char* err;
#if defined(SO_REUSEADDR) || defined(SO_REUSEPORT)		\
	|| defined(IPV6_V6ONLY) || defined(IP_TRANSPARENT)	\
	|| defined(IP_BINDANY) || defined(IP_FREEBIND)		\
	|| defined(SO_BINDANY) || defined(TCP_NODELAY)
	int on = 1;
#endif
#ifdef HAVE_SYSTEMD
	int got_fd_from_systemd = 0;
#endif
#ifdef USE_TCP_FASTOPEN
	int qlen;
#endif
#if !defined(IP_TRANSPARENT) && !defined(IP_BINDANY) && !defined(SO_BINDANY)
	(void)transparent;
#endif
#if !defined(IP_FREEBIND)
	(void)freebind;
#endif
	verbose_print_addr(addr, additional);
	*noproto = 0;
#ifdef HAVE_SYSTEMD
	if (!use_systemd ||
	    (use_systemd
	     && (s = systemd_get_activated(addr->ai_family, addr->ai_socktype, 1,
					   addr->ai_addr, addr->ai_addrlen,
					   NULL)) == -1)) {
#else
	(void)use_systemd;
#endif
	if((s = socket(addr->ai_family, addr->ai_socktype, 0)) == -1) {
#ifndef USE_WINSOCK
		if(errno == EAFNOSUPPORT || errno == EPROTONOSUPPORT) {
			*noproto = 1;
			return -1;
		}
#else
		if(WSAGetLastError() == WSAEAFNOSUPPORT ||
			WSAGetLastError() == WSAEPROTONOSUPPORT) {
			*noproto = 1;
			return -1;
		}
#endif
		log_err("can't create socket: %s", sock_strerror(errno));
		return -1;
	}
	if(nodelay) {
#if defined(IPPROTO_TCP) && defined(TCP_NODELAY)
		if(setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (void*)&on,
			(socklen_t)sizeof(on)) < 0) {
			#ifndef USE_WINSOCK
			log_err(" setsockopt(.. TCP_NODELAY ..) failed: %s",
				strerror(errno));
			#else
			log_err(" setsockopt(.. TCP_NODELAY ..) failed: %s",
				wsa_strerror(WSAGetLastError()));
			#endif
		}
#else
		log_warn(" setsockopt(TCP_NODELAY) unsupported");
#endif /* defined(IPPROTO_TCP) && defined(TCP_NODELAY) */
	}
	if (mss > 0) {
#if defined(IPPROTO_TCP) && defined(TCP_MAXSEG)
		if(setsockopt(s, IPPROTO_TCP, TCP_MAXSEG, (void*)&mss,
			(socklen_t)sizeof(mss)) < 0) {
			log_err(" setsockopt(.. TCP_MAXSEG ..) failed: %s",
				sock_strerror(errno));
		} else {
			verbose(VERB_ALGO,
				" tcp socket mss set to %d", mss);
		}
#else
		log_warn(" setsockopt(TCP_MAXSEG) unsupported");
#endif /* defined(IPPROTO_TCP) && defined(TCP_MAXSEG) */
	}
#ifdef HAVE_SYSTEMD
	} else {
		got_fd_from_systemd = 1;
    }
#endif
#ifdef SO_REUSEADDR
	if(setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (void*)&on,
		(socklen_t)sizeof(on)) < 0) {
		log_err("setsockopt(.. SO_REUSEADDR ..) failed: %s",
			sock_strerror(errno));
		sock_close(s);
		return -1;
	}
#endif /* SO_REUSEADDR */
#ifdef IP_FREEBIND
	if (freebind && setsockopt(s, IPPROTO_IP, IP_FREEBIND, (void*)&on,
	    (socklen_t)sizeof(on)) < 0) {
		log_warn("setsockopt(.. IP_FREEBIND ..) failed: %s",
		strerror(errno));
	}
#endif /* IP_FREEBIND */
#ifdef SO_REUSEPORT
	/* try to set SO_REUSEPORT so that incoming
	 * connections are distributed evenly among the receiving threads.
	 * Each thread must have its own socket bound to the same port,
	 * with SO_REUSEPORT set on each socket.
	 */
	if (reuseport && *reuseport &&
		setsockopt(s, SOL_SOCKET, SO_REUSEPORT, (void*)&on,
		(socklen_t)sizeof(on)) < 0) {
#ifdef ENOPROTOOPT
		if(errno != ENOPROTOOPT || verbosity >= 3)
			log_warn("setsockopt(.. SO_REUSEPORT ..) failed: %s",
				strerror(errno));
#endif
		/* this option is not essential, we can continue */
		*reuseport = 0;
	}
#else
	(void)reuseport;
#endif /* defined(SO_REUSEPORT) */
#if defined(IPV6_V6ONLY)
	if(addr->ai_family == AF_INET6 && v6only
#  ifdef HAVE_SYSTEMD
		/* Systemd wants to control if the socket is v6 only
		 * or both, with BindIPv6Only=default, ipv6-only or
		 * both in systemd.socket, so it is not set here. */
		&& !got_fd_from_systemd
#  endif
		) {
		if(setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY,
			(void*)&on, (socklen_t)sizeof(on)) < 0) {
			log_err("setsockopt(..., IPV6_V6ONLY, ...) failed: %s",
				sock_strerror(errno));
			sock_close(s);
			return -1;
		}
	}
#else
	(void)v6only;
#endif /* IPV6_V6ONLY */
#ifdef IP_TRANSPARENT
	if (transparent &&
	    setsockopt(s, IPPROTO_IP, IP_TRANSPARENT, (void*)&on,
	    (socklen_t)sizeof(on)) < 0) {
		log_warn("setsockopt(.. IP_TRANSPARENT ..) failed: %s",
			strerror(errno));
	}
#elif defined(IP_BINDANY)
	if (transparent &&
	    setsockopt(s, (addr->ai_family==AF_INET6? IPPROTO_IPV6:IPPROTO_IP),
	    (addr->ai_family == AF_INET6? IPV6_BINDANY:IP_BINDANY),
	    (void*)&on, (socklen_t)sizeof(on)) < 0) {
		log_warn("setsockopt(.. IP%s_BINDANY ..) failed: %s",
		(addr->ai_family==AF_INET6?"V6":""), strerror(errno));
	}
#elif defined(SO_BINDANY)
	if (transparent &&
	    setsockopt(s, SOL_SOCKET, SO_BINDANY, (void*)&on, (socklen_t)
	    sizeof(on)) < 0) {
		log_warn("setsockopt(.. SO_BINDANY ..) failed: %s",
		strerror(errno));
	}
#endif /* IP_TRANSPARENT || IP_BINDANY || SO_BINDANY */
	err = set_ip_dscp(s, addr->ai_family, dscp);
	if(err != NULL)
		log_warn("error setting IP DiffServ codepoint %d on TCP socket: %s", dscp, err);
	if(
#ifdef HAVE_SYSTEMD
		!got_fd_from_systemd &&
#endif
        bind(s, addr->ai_addr, addr->ai_addrlen) != 0) {
#ifndef USE_WINSOCK
		/* detect freebsd jail with no ipv6 permission */
		if(addr->ai_family==AF_INET6 && errno==EINVAL)
			*noproto = 1;
		else {
			log_err_addr("can't bind socket", strerror(errno),
				(struct sockaddr_storage*)addr->ai_addr,
				addr->ai_addrlen);
		}
#else
		log_err_addr("can't bind socket",
			wsa_strerror(WSAGetLastError()),
			(struct sockaddr_storage*)addr->ai_addr,
			addr->ai_addrlen);
#endif
		sock_close(s);
		return -1;
	}
	if(!fd_set_nonblock(s)) {
		sock_close(s);
		return -1;
	}
	if(listen(s, TCP_BACKLOG) == -1) {
		log_err("can't listen: %s", sock_strerror(errno));
		sock_close(s);
		return -1;
	}
#ifdef USE_TCP_FASTOPEN
	/* qlen specifies how many outstanding TFO requests to allow. Limit is a defense
	   against IP spoofing attacks as suggested in RFC7413 */
#ifdef __APPLE__
	/* OS X implementation only supports qlen of 1 via this call. Actual
	   value is configured by the net.inet.tcp.fastopen_backlog kernel parm. */
	qlen = 1;
#else
	/* 5 is recommended on linux */
	qlen = 5;
#endif
	if ((setsockopt(s, IPPROTO_TCP, TCP_FASTOPEN, &qlen,
		  sizeof(qlen))) == -1 ) {
#ifdef ENOPROTOOPT
		/* squelch ENOPROTOOPT: freebsd server mode with kernel support
		   disabled, except when verbosity enabled for debugging */
		if(errno != ENOPROTOOPT || verbosity >= 3) {
#endif
		  if(errno == EPERM) {
		  	log_warn("Setting TCP Fast Open as server failed: %s ; this could likely be because sysctl net.inet.tcp.fastopen.enabled, net.inet.tcp.fastopen.server_enable, or net.ipv4.tcp_fastopen is disabled", strerror(errno));
		  } else {
		  	log_err("Setting TCP Fast Open as server failed: %s", strerror(errno));
		  }
#ifdef ENOPROTOOPT
		}
#endif
	}
#endif
	return s;
}

char*
set_ip_dscp(int socket, int addrfamily, int dscp)
{
	int ds;

	if(dscp == 0)
		return NULL;
	ds = dscp << 2;
	switch(addrfamily) {
	case AF_INET6:
	#ifdef IPV6_TCLASS
		if(setsockopt(socket, IPPROTO_IPV6, IPV6_TCLASS, (void*)&ds,
			sizeof(ds)) < 0)
			return sock_strerror(errno);
		break;
	#else
		return "IPV6_TCLASS not defined on this system";
	#endif
	default:
		if(setsockopt(socket, IPPROTO_IP, IP_TOS, (void*)&ds, sizeof(ds)) < 0)
			return sock_strerror(errno);
		break;
	}
	return NULL;
}

int
create_local_accept_sock(const char *path, int* noproto, int use_systemd)
{
#ifdef HAVE_SYSTEMD
	int ret;

	if (use_systemd && (ret = systemd_get_activated(AF_LOCAL, SOCK_STREAM, 1, NULL, 0, path)) != -1)
		return ret;
	else {
#endif
#ifdef HAVE_SYS_UN_H
	int s;
	struct sockaddr_un usock;
#ifndef HAVE_SYSTEMD
	(void)use_systemd;
#endif

	verbose(VERB_ALGO, "creating unix socket %s", path);
#ifdef HAVE_STRUCT_SOCKADDR_UN_SUN_LEN
	/* this member exists on BSDs, not Linux */
	usock.sun_len = (unsigned)sizeof(usock);
#endif
	usock.sun_family = AF_LOCAL;
	/* length is 92-108, 104 on FreeBSD */
	(void)strlcpy(usock.sun_path, path, sizeof(usock.sun_path));

	if ((s = socket(AF_LOCAL, SOCK_STREAM, 0)) == -1) {
		log_err("Cannot create local socket %s (%s)",
			path, strerror(errno));
		return -1;
	}

	if (unlink(path) && errno != ENOENT) {
		/* The socket already exists and cannot be removed */
		log_err("Cannot remove old local socket %s (%s)",
			path, strerror(errno));
		goto err;
	}

	if (bind(s, (struct sockaddr *)&usock,
		(socklen_t)sizeof(struct sockaddr_un)) == -1) {
		log_err("Cannot bind local socket %s (%s)",
			path, strerror(errno));
		goto err;
	}

	if (!fd_set_nonblock(s)) {
		log_err("Cannot set non-blocking mode");
		goto err;
	}

	if (listen(s, TCP_BACKLOG) == -1) {
		log_err("can't listen: %s", strerror(errno));
		goto err;
	}

	(void)noproto; /*unused*/
	return s;

err:
	sock_close(s);
	return -1;

#ifdef HAVE_SYSTEMD
	}
#endif
#else
	(void)use_systemd;
	(void)path;
	log_err("Local sockets are not supported");
	*noproto = 1;
	return -1;
#endif
}


/**
 * Create socket from getaddrinfo results
 */
static int
make_sock(int stype, const char* ifname, int port,
	struct addrinfo *hints, int v6only, int* noip6, size_t rcv, size_t snd,
	int* reuseport, int transparent, int tcp_mss, int nodelay, int freebind,
	int use_systemd, int dscp, struct unbound_socket* ub_sock,
	const char* additional)
{
	struct addrinfo *res = NULL;
	int r, s, inuse, noproto;
	char portbuf[32];
	snprintf(portbuf, sizeof(portbuf), "%d", port);
	hints->ai_socktype = stype;
	*noip6 = 0;
	if((r=getaddrinfo(ifname, portbuf, hints, &res)) != 0 || !res) {
#ifdef USE_WINSOCK
		if(r == EAI_NONAME && hints->ai_family == AF_INET6){
			*noip6 = 1; /* 'Host not found' for IP6 on winXP */
			return -1;
		}
#endif
		log_err("node %s:%s getaddrinfo: %s %s",
			ifname?ifname:"default", portbuf, gai_strerror(r),
#ifdef EAI_SYSTEM
			(r==EAI_SYSTEM?(char*)strerror(errno):"")
#else
			""
#endif
		);
		return -1;
	}
	if(stype == SOCK_DGRAM) {
		verbose_print_addr(res, additional);
		s = create_udp_sock(res->ai_family, res->ai_socktype,
			(struct sockaddr*)res->ai_addr, res->ai_addrlen,
			v6only, &inuse, &noproto, (int)rcv, (int)snd, 1,
			reuseport, transparent, freebind, use_systemd, dscp);
		if(s == -1 && inuse) {
			log_err("bind: address already in use");
		} else if(s == -1 && noproto && hints->ai_family == AF_INET6){
			*noip6 = 1;
		}
	} else	{
		s = create_tcp_accept_sock(res, v6only, &noproto, reuseport,
			transparent, tcp_mss, nodelay, freebind, use_systemd,
			dscp, additional);
		if(s == -1 && noproto && hints->ai_family == AF_INET6){
			*noip6 = 1;
		}
	}

	if(!res->ai_addr) {
		log_err("getaddrinfo returned no address");
		freeaddrinfo(res);
		sock_close(s);
		return -1;
	}
	ub_sock->addr = memdup(res->ai_addr, res->ai_addrlen);
	ub_sock->addrlen = res->ai_addrlen;
	if(!ub_sock->addr) {
		log_err("out of memory: allocate listening address");
		freeaddrinfo(res);
		sock_close(s);
		return -1;
	}
	freeaddrinfo(res);

	ub_sock->s = s;
	ub_sock->fam = hints->ai_family;
	ub_sock->acl = NULL;

	return s;
}

/** make socket and first see if ifname contains port override info */
static int
make_sock_port(int stype, const char* ifname, int port,
	struct addrinfo *hints, int v6only, int* noip6, size_t rcv, size_t snd,
	int* reuseport, int transparent, int tcp_mss, int nodelay, int freebind,
	int use_systemd, int dscp, struct unbound_socket* ub_sock,
	const char* additional)
{
	char* s = strchr(ifname, '@');
	if(s) {
		/* override port with ifspec@port */
		int port;
		char newif[128];
		if((size_t)(s-ifname) >= sizeof(newif)) {
			log_err("ifname too long: %s", ifname);
			*noip6 = 0;
			return -1;
		}
		port = atoi(s+1);
		if(port < 0 || 0 == port || port > 65535) {
			log_err("invalid portnumber in interface: %s", ifname);
			*noip6 = 0;
			return -1;
		}
		(void)strlcpy(newif, ifname, sizeof(newif));
		newif[s-ifname] = 0;
		return make_sock(stype, newif, port, hints, v6only, noip6, rcv,
			snd, reuseport, transparent, tcp_mss, nodelay, freebind,
			use_systemd, dscp, ub_sock, additional);
	}
	return make_sock(stype, ifname, port, hints, v6only, noip6, rcv, snd,
		reuseport, transparent, tcp_mss, nodelay, freebind, use_systemd,
		dscp, ub_sock, additional);
}

/**
 * Add port to open ports list.
 * @param list: list head. changed.
 * @param s: fd.
 * @param ftype: if fd is UDP.
 * @param pp2_enabled: if PROXYv2 is enabled for this port.
 * @param ub_sock: socket with address.
 * @return false on failure. list in unchanged then.
 */
static int
port_insert(struct listen_port** list, int s, enum listen_type ftype,
	int pp2_enabled, struct unbound_socket* ub_sock)
{
	struct listen_port* item = (struct listen_port*)malloc(
		sizeof(struct listen_port));
	if(!item)
		return 0;
	item->next = *list;
	item->fd = s;
	item->ftype = ftype;
	item->pp2_enabled = pp2_enabled;
	item->socket = ub_sock;
	*list = item;
	return 1;
}

/** set fd to receive software timestamps */
static int
set_recvtimestamp(int s)
{
#ifdef HAVE_LINUX_NET_TSTAMP_H
	int opt = SOF_TIMESTAMPING_RX_SOFTWARE | SOF_TIMESTAMPING_SOFTWARE;
	if (setsockopt(s, SOL_SOCKET, SO_TIMESTAMPNS, (void*)&opt, (socklen_t)sizeof(opt)) < 0) {
		log_err("setsockopt(..., SO_TIMESTAMPNS, ...) failed: %s",
			strerror(errno));
		return 0;
	}
	return 1;
#else
	log_err("packets timestamping is not supported on this platform");
	(void)s;
	return 0;
#endif
}

/** set fd to receive source address packet info */
static int
set_recvpktinfo(int s, int family)
{
#if defined(IPV6_RECVPKTINFO) || defined(IPV6_PKTINFO) || (defined(IP_RECVDSTADDR) && defined(IP_SENDSRCADDR)) || defined(IP_PKTINFO)
	int on = 1;
#else
	(void)s;
#endif
	if(family == AF_INET6) {
#           ifdef IPV6_RECVPKTINFO
		if(setsockopt(s, IPPROTO_IPV6, IPV6_RECVPKTINFO,
			(void*)&on, (socklen_t)sizeof(on)) < 0) {
			log_err("setsockopt(..., IPV6_RECVPKTINFO, ...) failed: %s",
				strerror(errno));
			return 0;
		}
#           elif defined(IPV6_PKTINFO)
		if(setsockopt(s, IPPROTO_IPV6, IPV6_PKTINFO,
			(void*)&on, (socklen_t)sizeof(on)) < 0) {
			log_err("setsockopt(..., IPV6_PKTINFO, ...) failed: %s",
				strerror(errno));
			return 0;
		}
#           else
		log_err("no IPV6_RECVPKTINFO and IPV6_PKTINFO options, please "
			"disable interface-automatic or do-ip6 in config");
		return 0;
#           endif /* defined IPV6_RECVPKTINFO */

	} else if(family == AF_INET) {
#           ifdef IP_PKTINFO
		if(setsockopt(s, IPPROTO_IP, IP_PKTINFO,
			(void*)&on, (socklen_t)sizeof(on)) < 0) {
			log_err("setsockopt(..., IP_PKTINFO, ...) failed: %s",
				strerror(errno));
			return 0;
		}
#           elif defined(IP_RECVDSTADDR) && defined(IP_SENDSRCADDR)
		if(setsockopt(s, IPPROTO_IP, IP_RECVDSTADDR,
			(void*)&on, (socklen_t)sizeof(on)) < 0) {
			log_err("setsockopt(..., IP_RECVDSTADDR, ...) failed: %s",
				strerror(errno));
			return 0;
		}
#           else
		log_err("no IP_SENDSRCADDR or IP_PKTINFO option, please disable "
			"interface-automatic or do-ip4 in config");
		return 0;
#           endif /* IP_PKTINFO */

	}
	return 1;
}

/**
 * Helper for ports_open. Creates one interface (or NULL for default).
 * @param ifname: The interface ip address.
 * @param do_auto: use automatic interface detection.
 * 	If enabled, then ifname must be the wildcard name.
 * @param do_udp: if udp should be used.
 * @param do_tcp: if tcp should be used.
 * @param hints: for getaddrinfo. family and flags have to be set by caller.
 * @param port: Port number to use.
 * @param list: list of open ports, appended to, changed to point to list head.
 * @param rcv: receive buffer size for UDP
 * @param snd: send buffer size for UDP
 * @param ssl_port: ssl service port number
 * @param tls_additional_port: list of additional ssl service port numbers.
 * @param https_port: DoH service port number
 * @param proxy_protocol_port: list of PROXYv2 port numbers.
 * @param reuseport: try to set SO_REUSEPORT if nonNULL and true.
 * 	set to false on exit if reuseport failed due to no kernel support.
 * @param transparent: set IP_TRANSPARENT socket option.
 * @param tcp_mss: maximum segment size of tcp socket. default if zero.
 * @param freebind: set IP_FREEBIND socket option.
 * @param http2_nodelay: set TCP_NODELAY on HTTP/2 connection
 * @param use_systemd: if true, fetch sockets from systemd.
 * @param dnscrypt_port: dnscrypt service port number
 * @param dscp: DSCP to use.
 * @param quic_port: dns over quic port number.
 * @param http_notls_downstream: if no tls is used for https downstream.
 * @param sock_queue_timeout: the sock_queue_timeout from config. Seconds to
 * 	wait to discard if UDP packets have waited for long in the socket
 * 	buffer.
 * @return: returns false on error.
 */
static int
ports_create_if(const char* ifname, int do_auto, int do_udp, int do_tcp,
	struct addrinfo *hints, int port, struct listen_port** list,
	size_t rcv, size_t snd, int ssl_port,
	struct config_strlist* tls_additional_port, int https_port,
	struct config_strlist* proxy_protocol_port,
	int* reuseport, int transparent, int tcp_mss, int freebind,
	int http2_nodelay, int use_systemd, int dnscrypt_port, int dscp,
	int quic_port, int http_notls_downstream, int sock_queue_timeout)
{
	int s, noip6=0;
	int is_ssl = if_is_ssl(ifname, port, ssl_port, tls_additional_port);
	int is_https = if_is_https(ifname, port, https_port);
	int is_dnscrypt = if_is_dnscrypt(ifname, port, dnscrypt_port);
	int is_pp2 = if_is_pp2(ifname, port, proxy_protocol_port);
	int is_doq = if_is_quic(ifname, port, quic_port);
	/* Always set TCP_NODELAY on TLS connection as it speeds up the TLS
	 * handshake. DoH had already such option so we respect it.
	 * Otherwise the server waits before sending more handshake data for
	 * the client ACK (Nagle's algorithm), which is delayed because the
	 * client waits for more data before ACKing (delayed ACK). */
	int nodelay = is_https?http2_nodelay:is_ssl; 
	struct unbound_socket* ub_sock;
	const char* add = NULL;

	if(!do_udp && !do_tcp)
		return 0;

	if(is_pp2) {
		if(is_dnscrypt) {
			fatal_exit("PROXYv2 and DNSCrypt combination not "
				"supported!");
		} else if(is_https) {
			fatal_exit("PROXYv2 and DoH combination not "
				"supported!");
		} else if(is_doq) {
			fatal_exit("PROXYv2 and DoQ combination not "
				"supported!");
		}
	}

	/* Check if both UDP and TCP ports should be open.
	 * In the case of encrypted channels, probably an unencrypted channel
	 * at the same port is not desired. */
	if((is_ssl || is_https) && !is_doq) do_udp = do_auto = 0;
	if((is_doq) && !(is_https || is_ssl)) do_tcp = 0;

	if(do_auto) {
		ub_sock = calloc(1, sizeof(struct unbound_socket));
		if(!ub_sock)
			return 0;
		if((s = make_sock_port(SOCK_DGRAM, ifname, port, hints, 1,
			&noip6, rcv, snd, reuseport, transparent,
			tcp_mss, nodelay, freebind, use_systemd, dscp, ub_sock,
			(is_dnscrypt?"udpancil_dnscrypt":"udpancil"))) == -1) {
			free(ub_sock->addr);
			free(ub_sock);
			if(noip6) {
				log_warn("IPv6 protocol not available");
				return 1;
			}
			return 0;
		}
		/* getting source addr packet info is highly non-portable */
		if(!set_recvpktinfo(s, hints->ai_family)) {
			sock_close(s);
			free(ub_sock->addr);
			free(ub_sock);
			return 0;
		}
		if (sock_queue_timeout && !set_recvtimestamp(s)) {
			log_warn("socket timestamping is not available");
		}
		if(!port_insert(list, s, is_dnscrypt
			?listen_type_udpancil_dnscrypt:listen_type_udpancil,
			is_pp2, ub_sock)) {
			sock_close(s);
			free(ub_sock->addr);
			free(ub_sock);
			return 0;
		}
	} else if(do_udp) {
		enum listen_type udp_port_type;
		ub_sock = calloc(1, sizeof(struct unbound_socket));
		if(!ub_sock)
			return 0;
		if(is_dnscrypt) {
			udp_port_type = listen_type_udp_dnscrypt;
			add = "dnscrypt";
		} else if(is_doq) {
			udp_port_type = listen_type_doq;
			add = "doq";
			if(if_listens_on(ifname, port, 53, NULL)) {
				log_err("DNS over QUIC is strictly not "
					"allowed on port 53 as per RFC 9250. "
					"Port 53 is for DNS datagrams. Error "
					"for interface '%s'.", ifname);
				free(ub_sock->addr);
				free(ub_sock);
				return 0;
			}
		} else {
			udp_port_type = listen_type_udp;
			add = NULL;
		}
		/* regular udp socket */
		if((s = make_sock_port(SOCK_DGRAM, ifname, port, hints, 1,
			&noip6, rcv, snd, reuseport, transparent,
			tcp_mss, nodelay, freebind, use_systemd, dscp, ub_sock,
			add)) == -1) {
			free(ub_sock->addr);
			free(ub_sock);
			if(noip6) {
				log_warn("IPv6 protocol not available");
				return 1;
			}
			return 0;
		}
		if(udp_port_type == listen_type_doq) {
			if(!set_recvpktinfo(s, hints->ai_family)) {
				sock_close(s);
				free(ub_sock->addr);
				free(ub_sock);
				return 0;
			}
		}
		if(udp_port_type == listen_type_udp && sock_queue_timeout)
			udp_port_type = listen_type_udpancil;
		if (sock_queue_timeout) {
			if(!set_recvtimestamp(s)) {
				log_warn("socket timestamping is not available");
			} else {
				if(udp_port_type == listen_type_udp)
					udp_port_type = listen_type_udpancil;
			}
		}
		if(!port_insert(list, s, udp_port_type, is_pp2, ub_sock)) {
			sock_close(s);
			free(ub_sock->addr);
			free(ub_sock);
			return 0;
		}
	}
	if(do_tcp) {
		enum listen_type port_type;
		ub_sock = calloc(1, sizeof(struct unbound_socket));
		if(!ub_sock)
			return 0;
		if(is_ssl) {
			port_type = listen_type_ssl;
			add = "tls";
		} else if(is_https) {
			port_type = listen_type_http;
			add = "https";
			if(http_notls_downstream)
				add = "http";
		} else if(is_dnscrypt) {
			port_type = listen_type_tcp_dnscrypt;
			add = "dnscrypt";
		} else {
			port_type = listen_type_tcp;
			add = NULL;
		}
		if((s = make_sock_port(SOCK_STREAM, ifname, port, hints, 1,
			&noip6, 0, 0, reuseport, transparent, tcp_mss, nodelay,
			freebind, use_systemd, dscp, ub_sock, add)) == -1) {
			free(ub_sock->addr);
			free(ub_sock);
			if(noip6) {
				/*log_warn("IPv6 protocol not available");*/
				return 1;
			}
			return 0;
		}
		if(is_ssl)
			verbose(VERB_ALGO, "setup TCP for SSL service");
		if(!port_insert(list, s, port_type, is_pp2, ub_sock)) {
			sock_close(s);
			free(ub_sock->addr);
			free(ub_sock);
			return 0;
		}
	}
	return 1;
}

/**
 * Add items to commpoint list in front.
 * @param c: commpoint to add.
 * @param front: listen struct.
 * @return: false on failure.
 */
static int
listen_cp_insert(struct comm_point* c, struct listen_dnsport* front)
{
	struct listen_list* item = (struct listen_list*)malloc(
		sizeof(struct listen_list));
	if(!item)
		return 0;
	item->com = c;
	item->next = front->cps;
	front->cps = item;
	return 1;
}

void listen_setup_locks(void)
{
	if(!stream_wait_lock_inited) {
		lock_basic_init(&stream_wait_count_lock);
		stream_wait_lock_inited = 1;
	}
	if(!http2_query_buffer_lock_inited) {
		lock_basic_init(&http2_query_buffer_count_lock);
		http2_query_buffer_lock_inited = 1;
	}
	if(!http2_response_buffer_lock_inited) {
		lock_basic_init(&http2_response_buffer_count_lock);
		http2_response_buffer_lock_inited = 1;
	}
}

void listen_desetup_locks(void)
{
	if(stream_wait_lock_inited) {
		stream_wait_lock_inited = 0;
		lock_basic_destroy(&stream_wait_count_lock);
	}
	if(http2_query_buffer_lock_inited) {
		http2_query_buffer_lock_inited = 0;
		lock_basic_destroy(&http2_query_buffer_count_lock);
	}
	if(http2_response_buffer_lock_inited) {
		http2_response_buffer_lock_inited = 0;
		lock_basic_destroy(&http2_response_buffer_count_lock);
	}
}

struct listen_dnsport*
listen_create(struct comm_base* base, struct listen_port* ports,
	size_t bufsize, int tcp_accept_count, int tcp_idle_timeout,
	int harden_large_queries, uint32_t http_max_streams,
	char* http_endpoint, int http_notls, struct tcl_list* tcp_conn_limit,
	void* dot_sslctx, void* doh_sslctx, void* quic_sslctx,
	struct dt_env* dtenv,
	struct doq_table* doq_table,
	struct ub_randstate* rnd,struct config_file* cfg,
	comm_point_callback_type* cb, void *cb_arg)
{
	struct listen_dnsport* front = (struct listen_dnsport*)
		malloc(sizeof(struct listen_dnsport));
	if(!front)
		return NULL;
	front->cps = NULL;
	front->udp_buff = sldns_buffer_new(bufsize);
#ifdef USE_DNSCRYPT
	front->dnscrypt_udp_buff = NULL;
#endif
	if(!front->udp_buff) {
		free(front);
		return NULL;
	}

	/* create comm points as needed */
	while(ports) {
		struct comm_point* cp = NULL;
		if(ports->ftype == listen_type_udp ||
		   ports->ftype == listen_type_udp_dnscrypt) {
			cp = comm_point_create_udp(base, ports->fd,
				front->udp_buff, ports->pp2_enabled, cb,
				cb_arg, ports->socket);
		} else if(ports->ftype == listen_type_doq) {
#ifndef HAVE_NGTCP2
			log_warn("Unbound is not compiled with "
				"ngtcp2. This is required to use DNS "
				"over QUIC.");
#endif
			cp = comm_point_create_doq(base, ports->fd,
				front->udp_buff, cb, cb_arg, ports->socket,
				doq_table, rnd, quic_sslctx, cfg);
		} else if(ports->ftype == listen_type_tcp ||
				ports->ftype == listen_type_tcp_dnscrypt) {
			cp = comm_point_create_tcp(base, ports->fd,
				tcp_accept_count, tcp_idle_timeout,
				harden_large_queries, 0, NULL,
				tcp_conn_limit, bufsize, front->udp_buff,
				ports->ftype, ports->pp2_enabled, cb, cb_arg,
				ports->socket);
		} else if(ports->ftype == listen_type_ssl ||
			ports->ftype == listen_type_http) {
			cp = comm_point_create_tcp(base, ports->fd,
				tcp_accept_count, tcp_idle_timeout,
				harden_large_queries,
				http_max_streams, http_endpoint,
				tcp_conn_limit, bufsize, front->udp_buff,
				ports->ftype, ports->pp2_enabled, cb, cb_arg,
				ports->socket);
			if(ports->ftype == listen_type_http) {
				if(!doh_sslctx && !http_notls) {
					log_warn("HTTPS port configured, but "
						"no TLS tls-service-key or "
						"tls-service-pem set");
				}
#ifndef HAVE_SSL_CTX_SET_ALPN_SELECT_CB
				if(!http_notls) {
					log_warn("Unbound is not compiled "
						"with an OpenSSL version "
						"supporting ALPN "
						"(OpenSSL >= 1.0.2). This "
						"is required to use "
						"DNS-over-HTTPS");
				}
#endif
#ifndef HAVE_NGHTTP2_NGHTTP2_H
				log_warn("Unbound is not compiled with "
					"nghttp2. This is required to use "
					"DNS-over-HTTPS.");
#endif
			}
		} else if(ports->ftype == listen_type_udpancil ||
				  ports->ftype == listen_type_udpancil_dnscrypt) {
#if defined(AF_INET6) && defined(IPV6_PKTINFO) && defined(HAVE_RECVMSG)
			cp = comm_point_create_udp_ancil(base, ports->fd,
				front->udp_buff, ports->pp2_enabled, cb,
				cb_arg, ports->socket);
#else
			log_warn("This system does not support UDP ancilliary data.");
#endif
		}
		if(!cp) {
			log_err("can't create commpoint");
			listen_delete(front);
			return NULL;
		}
		if((http_notls && ports->ftype == listen_type_http) ||
			(ports->ftype == listen_type_tcp) ||
			(ports->ftype == listen_type_udp) ||
			(ports->ftype == listen_type_udpancil) ||
			(ports->ftype == listen_type_tcp_dnscrypt) ||
			(ports->ftype == listen_type_udp_dnscrypt) ||
			(ports->ftype == listen_type_udpancil_dnscrypt)) {
			cp->ssl = NULL;
		} else if(ports->ftype == listen_type_doq) {
			cp->ssl = quic_sslctx;
		} else if(ports->ftype == listen_type_http) {
			cp->ssl = doh_sslctx;
		} else {
			cp->ssl = dot_sslctx;
		}
		cp->dtenv = dtenv;
		cp->do_not_close = 1;
#ifdef USE_DNSCRYPT
		if (ports->ftype == listen_type_udp_dnscrypt ||
			ports->ftype == listen_type_tcp_dnscrypt ||
			ports->ftype == listen_type_udpancil_dnscrypt) {
			cp->dnscrypt = 1;
			cp->dnscrypt_buffer = sldns_buffer_new(bufsize);
			if(!cp->dnscrypt_buffer) {
				log_err("can't alloc dnscrypt_buffer");
				comm_point_delete(cp);
				listen_delete(front);
				return NULL;
			}
			front->dnscrypt_udp_buff = cp->dnscrypt_buffer;
		}
#endif
		if(!listen_cp_insert(cp, front)) {
			log_err("malloc failed");
			comm_point_delete(cp);
			listen_delete(front);
			return NULL;
		}
		ports = ports->next;
	}
	if(!front->cps) {
		log_err("Could not open sockets to accept queries.");
		listen_delete(front);
		return NULL;
	}

	return front;
}

void
listen_list_delete(struct listen_list* list)
{
	struct listen_list *p = list, *pn;
	while(p) {
		pn = p->next;
		comm_point_delete(p->com);
		free(p);
		p = pn;
	}
}

void
listen_delete(struct listen_dnsport* front)
{
	if(!front)
		return;
	listen_list_delete(front->cps);
#ifdef USE_DNSCRYPT
	if(front->dnscrypt_udp_buff &&
		front->udp_buff != front->dnscrypt_udp_buff) {
		sldns_buffer_free(front->dnscrypt_udp_buff);
	}
#endif
	sldns_buffer_free(front->udp_buff);
	free(front);
}

#ifdef HAVE_GETIFADDRS
static int
resolve_ifa_name(struct ifaddrs *ifas, const char *search_ifa, char ***ip_addresses, int *ip_addresses_size)
{
	struct ifaddrs *ifa;
	void *tmpbuf;
	int last_ip_addresses_size = *ip_addresses_size;

	for(ifa = ifas; ifa != NULL; ifa = ifa->ifa_next) {
		sa_family_t family;
		const char* atsign;
#ifdef INET6      /* |   address ip    | % |  ifa name  | @ |  port  | nul */
		char addr_buf[INET6_ADDRSTRLEN + 1 + IF_NAMESIZE + 1 + 16 + 1];
#else
		char addr_buf[INET_ADDRSTRLEN + 1 + 16 + 1];
#endif

		if((atsign=strrchr(search_ifa, '@')) != NULL) {
			if(strlen(ifa->ifa_name) != (size_t)(atsign-search_ifa)
			   || strncmp(ifa->ifa_name, search_ifa,
			   atsign-search_ifa) != 0)
				continue;
		} else {
			if(strcmp(ifa->ifa_name, search_ifa) != 0)
				continue;
			atsign = "";
		}

		if(ifa->ifa_addr == NULL)
			continue;

		family = ifa->ifa_addr->sa_family;
		if(family == AF_INET) {
			char a4[INET_ADDRSTRLEN + 1];
			struct sockaddr_in *in4 = (struct sockaddr_in *)
				ifa->ifa_addr;
			if(!inet_ntop(family, &in4->sin_addr, a4, sizeof(a4))) {
				log_err("inet_ntop failed");
				return 0;
			}
			snprintf(addr_buf, sizeof(addr_buf), "%s%s",
				a4, atsign);
		}
#ifdef INET6
		else if(family == AF_INET6) {
			struct sockaddr_in6 *in6 = (struct sockaddr_in6 *)
				ifa->ifa_addr;
			char a6[INET6_ADDRSTRLEN + 1];
			char if_index_name[IF_NAMESIZE + 1];
			if_index_name[0] = 0;
			if(!inet_ntop(family, &in6->sin6_addr, a6, sizeof(a6))) {
				log_err("inet_ntop failed");
				return 0;
			}
			(void)if_indextoname(in6->sin6_scope_id,
				(char *)if_index_name);
			if (strlen(if_index_name) != 0) {
				snprintf(addr_buf, sizeof(addr_buf),
					"%s%%%s%s", a6, if_index_name, atsign);
			} else {
				snprintf(addr_buf, sizeof(addr_buf), "%s%s",
					a6, atsign);
			}
		}
#endif
		else {
			continue;
		}
		verbose(4, "interface %s has address %s", search_ifa, addr_buf);

		tmpbuf = realloc(*ip_addresses, sizeof(char *) * (*ip_addresses_size + 1));
		if(!tmpbuf) {
			log_err("realloc failed: out of memory");
			return 0;
		} else {
			*ip_addresses = tmpbuf;
		}
		(*ip_addresses)[*ip_addresses_size] = strdup(addr_buf);
		if(!(*ip_addresses)[*ip_addresses_size]) {
			log_err("strdup failed: out of memory");
			return 0;
		}
		(*ip_addresses_size)++;
	}

	if (*ip_addresses_size == last_ip_addresses_size) {
		tmpbuf = realloc(*ip_addresses, sizeof(char *) * (*ip_addresses_size + 1));
		if(!tmpbuf) {
			log_err("realloc failed: out of memory");
			return 0;
		} else {
			*ip_addresses = tmpbuf;
		}
		(*ip_addresses)[*ip_addresses_size] = strdup(search_ifa);
		if(!(*ip_addresses)[*ip_addresses_size]) {
			log_err("strdup failed: out of memory");
			return 0;
		}
		(*ip_addresses_size)++;
	}
	return 1;
}
#endif /* HAVE_GETIFADDRS */

int resolve_interface_names(char** ifs, int num_ifs,
	struct config_strlist* list, char*** resif, int* num_resif)
{
#ifdef HAVE_GETIFADDRS
	struct ifaddrs *addrs = NULL;
	if(num_ifs == 0 && list == NULL) {
		*resif = NULL;
		*num_resif = 0;
		return 1;
	}
	if(getifaddrs(&addrs) == -1) {
		log_err("failed to list interfaces: getifaddrs: %s",
			strerror(errno));
		freeifaddrs(addrs);
		return 0;
	}
	if(ifs) {
		int i;
		for(i=0; i<num_ifs; i++) {
			if(!resolve_ifa_name(addrs, ifs[i], resif, num_resif)) {
				freeifaddrs(addrs);
				config_del_strarray(*resif, *num_resif);
				*resif = NULL;
				*num_resif = 0;
				return 0;
			}
		}
	}
	if(list) {
		struct config_strlist* p;
		for(p = list; p; p = p->next) {
			if(!resolve_ifa_name(addrs, p->str, resif, num_resif)) {
				freeifaddrs(addrs);
				config_del_strarray(*resif, *num_resif);
				*resif = NULL;
				*num_resif = 0;
				return 0;
			}
}
	}
	freeifaddrs(addrs);
	return 1;
#else
	struct config_strlist* p;
	if(num_ifs == 0 && list == NULL) {
		*resif = NULL;
		*num_resif = 0;
		return 1;
	}
	*num_resif = num_ifs;
	for(p = list; p; p = p->next) {
		(*num_resif)++;
	}
	*resif = calloc(*num_resif, sizeof(**resif));
	if(!*resif) {
		log_err("out of memory");
		return 0;
	}
	if(ifs) {
		int i;
		for(i=0; i<num_ifs; i++) {
			(*resif)[i] = strdup(ifs[i]);
			if(!((*resif)[i])) {
				log_err("out of memory");
				config_del_strarray(*resif, *num_resif);
				*resif = NULL;
				*num_resif = 0;
				return 0;
			}
		}
	}
	if(list) {
		int idx = num_ifs;
		for(p = list; p; p = p->next) {
			(*resif)[idx] = strdup(p->str);
			if(!((*resif)[idx])) {
				log_err("out of memory");
				config_del_strarray(*resif, *num_resif);
				*resif = NULL;
				*num_resif = 0;
				return 0;
			}
			idx++;
		}
	}
	return 1;
#endif /* HAVE_GETIFADDRS */
}

struct listen_port*
listening_ports_open(struct config_file* cfg, char** ifs, int num_ifs,
	int* reuseport)
{
	struct listen_port* list = NULL;
	struct addrinfo hints;
	int i, do_ip4, do_ip6;
	int do_tcp, do_auto;
	do_ip4 = cfg->do_ip4;
	do_ip6 = cfg->do_ip6;
	do_tcp = cfg->do_tcp;
	do_auto = cfg->if_automatic && cfg->do_udp;
	if(cfg->incoming_num_tcp == 0)
		do_tcp = 0;

	/* getaddrinfo */
	memset(&hints, 0, sizeof(hints));
	hints.ai_flags = AI_PASSIVE;
	/* no name lookups on our listening ports */
	if(num_ifs > 0)
		hints.ai_flags |= AI_NUMERICHOST;
	hints.ai_family = AF_UNSPEC;
#ifndef INET6
	do_ip6 = 0;
#endif
	if(!do_ip4 && !do_ip6) {
		return NULL;
	}
	/* create ip4 and ip6 ports so that return addresses are nice. */
	if(do_auto || num_ifs == 0) {
		if(do_auto && cfg->if_automatic_ports &&
			cfg->if_automatic_ports[0]!=0) {
			char* now = cfg->if_automatic_ports;
			while(now && *now) {
				char* after;
				int extraport;
				while(isspace((unsigned char)*now))
					now++;
				if(!*now)
					break;
				after = now;
				extraport = (int)strtol(now, &after, 10);
				if(extraport < 0 || extraport > 65535) {
					log_err("interface-automatic-ports port number out of range, at position %d of '%s'", (int)(now-cfg->if_automatic_ports)+1, cfg->if_automatic_ports);
					listening_ports_free(list);
					return NULL;
				}
				if(extraport == 0 && now == after) {
					log_err("interface-automatic-ports could not be parsed, at position %d of '%s'", (int)(now-cfg->if_automatic_ports)+1, cfg->if_automatic_ports);
					listening_ports_free(list);
					return NULL;
				}
				now = after;
				if(do_ip6) {
					hints.ai_family = AF_INET6;
					if(!ports_create_if("::0",
						do_auto, cfg->do_udp, do_tcp,
						&hints, extraport, &list,
						cfg->so_rcvbuf, cfg->so_sndbuf,
						cfg->ssl_port, cfg->tls_additional_port,
						cfg->https_port,
						cfg->proxy_protocol_port,
						reuseport, cfg->ip_transparent,
						cfg->tcp_mss, cfg->ip_freebind,
						cfg->http_nodelay, cfg->use_systemd,
						cfg->dnscrypt_port, cfg->ip_dscp,
						cfg->quic_port, cfg->http_notls_downstream,
						cfg->sock_queue_timeout)) {
						listening_ports_free(list);
						return NULL;
					}
				}
				if(do_ip4) {
					hints.ai_family = AF_INET;
					if(!ports_create_if("0.0.0.0",
						do_auto, cfg->do_udp, do_tcp,
						&hints, extraport, &list,
						cfg->so_rcvbuf, cfg->so_sndbuf,
						cfg->ssl_port, cfg->tls_additional_port,
						cfg->https_port,
						cfg->proxy_protocol_port,
						reuseport, cfg->ip_transparent,
						cfg->tcp_mss, cfg->ip_freebind,
						cfg->http_nodelay, cfg->use_systemd,
						cfg->dnscrypt_port, cfg->ip_dscp,
						cfg->quic_port, cfg->http_notls_downstream,
						cfg->sock_queue_timeout)) {
						listening_ports_free(list);
						return NULL;
					}
				}
			}
			return list;
		}
		if(do_ip6) {
			hints.ai_family = AF_INET6;
			if(!ports_create_if(do_auto?"::0":"::1",
				do_auto, cfg->do_udp, do_tcp,
				&hints, cfg->port, &list,
				cfg->so_rcvbuf, cfg->so_sndbuf,
				cfg->ssl_port, cfg->tls_additional_port,
				cfg->https_port, cfg->proxy_protocol_port,
				reuseport, cfg->ip_transparent,
				cfg->tcp_mss, cfg->ip_freebind,
				cfg->http_nodelay, cfg->use_systemd,
				cfg->dnscrypt_port, cfg->ip_dscp,
				cfg->quic_port, cfg->http_notls_downstream,
				cfg->sock_queue_timeout)) {
				listening_ports_free(list);
				return NULL;
			}
		}
		if(do_ip4) {
			hints.ai_family = AF_INET;
			if(!ports_create_if(do_auto?"0.0.0.0":"127.0.0.1",
				do_auto, cfg->do_udp, do_tcp,
				&hints, cfg->port, &list,
				cfg->so_rcvbuf, cfg->so_sndbuf,
				cfg->ssl_port, cfg->tls_additional_port,
				cfg->https_port, cfg->proxy_protocol_port,
				reuseport, cfg->ip_transparent,
				cfg->tcp_mss, cfg->ip_freebind,
				cfg->http_nodelay, cfg->use_systemd,
				cfg->dnscrypt_port, cfg->ip_dscp,
				cfg->quic_port, cfg->http_notls_downstream,
				cfg->sock_queue_timeout)) {
				listening_ports_free(list);
				return NULL;
			}
		}
	} else for(i = 0; i<num_ifs; i++) {
		if(str_is_ip6(ifs[i])) {
			if(!do_ip6)
				continue;
			hints.ai_family = AF_INET6;
			if(!ports_create_if(ifs[i], 0, cfg->do_udp,
				do_tcp, &hints, cfg->port, &list,
				cfg->so_rcvbuf, cfg->so_sndbuf,
				cfg->ssl_port, cfg->tls_additional_port,
				cfg->https_port, cfg->proxy_protocol_port,
				reuseport, cfg->ip_transparent,
				cfg->tcp_mss, cfg->ip_freebind,
				cfg->http_nodelay, cfg->use_systemd,
				cfg->dnscrypt_port, cfg->ip_dscp,
				cfg->quic_port, cfg->http_notls_downstream,
				cfg->sock_queue_timeout)) {
				listening_ports_free(list);
				return NULL;
			}
		} else {
			if(!do_ip4)
				continue;
			hints.ai_family = AF_INET;
			if(!ports_create_if(ifs[i], 0, cfg->do_udp,
				do_tcp, &hints, cfg->port, &list,
				cfg->so_rcvbuf, cfg->so_sndbuf,
				cfg->ssl_port, cfg->tls_additional_port,
				cfg->https_port, cfg->proxy_protocol_port,
				reuseport, cfg->ip_transparent,
				cfg->tcp_mss, cfg->ip_freebind,
				cfg->http_nodelay, cfg->use_systemd,
				cfg->dnscrypt_port, cfg->ip_dscp,
				cfg->quic_port, cfg->http_notls_downstream,
				cfg->sock_queue_timeout)) {
				listening_ports_free(list);
				return NULL;
			}
		}
	}

	return list;
}

void listening_ports_free(struct listen_port* list)
{
	struct listen_port* nx;
	while(list) {
		nx = list->next;
		if(list->fd != -1) {
			sock_close(list->fd);
		}
		/* rc_ports don't have ub_socket */
		if(list->socket) {
			free(list->socket->addr);
			free(list->socket);
		}
		free(list);
		list = nx;
	}
}

size_t listen_get_mem(struct listen_dnsport* listen)
{
	struct listen_list* p;
	size_t s = sizeof(*listen) + sizeof(*listen->base) +
		sizeof(*listen->udp_buff) +
		sldns_buffer_capacity(listen->udp_buff);
#ifdef USE_DNSCRYPT
	s += sizeof(*listen->dnscrypt_udp_buff);
	if(listen->udp_buff != listen->dnscrypt_udp_buff){
		s += sldns_buffer_capacity(listen->dnscrypt_udp_buff);
	}
#endif
	for(p = listen->cps; p; p = p->next) {
		s += sizeof(*p);
		s += comm_point_get_mem(p->com);
	}
	return s;
}

void listen_stop_accept(struct listen_dnsport* listen)
{
	/* do not stop the ones that have no tcp_free list
	 * (they have already stopped listening) */
	struct listen_list* p;
	for(p=listen->cps; p; p=p->next) {
		if(p->com->type == comm_tcp_accept &&
			p->com->tcp_free != NULL) {
			comm_point_stop_listening(p->com);
		}
	}
}

void listen_start_accept(struct listen_dnsport* listen)
{
	/* do not start the ones that have no tcp_free list, it is no
	 * use to listen to them because they have no free tcp handlers */
	struct listen_list* p;
	for(p=listen->cps; p; p=p->next) {
		if(p->com->type == comm_tcp_accept &&
			p->com->tcp_free != NULL) {
			comm_point_start_listening(p->com, -1, -1);
		}
	}
}

struct tcp_req_info*
tcp_req_info_create(struct sldns_buffer* spoolbuf)
{
	struct tcp_req_info* req = (struct tcp_req_info*)malloc(sizeof(*req));
	if(!req) {
		log_err("malloc failure for new stream outoforder processing structure");
		return NULL;
	}
	memset(req, 0, sizeof(*req));
	req->spool_buffer = spoolbuf;
	return req;
}

void
tcp_req_info_delete(struct tcp_req_info* req)
{
	if(!req) return;
	tcp_req_info_clear(req);
	/* cp is pointer back to commpoint that owns this struct and
	 * called delete on us */
	/* spool_buffer is shared udp buffer, not deleted here */
	free(req);
}

void tcp_req_info_clear(struct tcp_req_info* req)
{
	struct tcp_req_open_item* open, *nopen;
	struct tcp_req_done_item* item, *nitem;
	if(!req) return;

	/* free outstanding request mesh reply entries */
	open = req->open_req_list;
	while(open) {
		nopen = open->next;
		mesh_state_remove_reply(open->mesh, open->mesh_state, req->cp);
		free(open);
		open = nopen;
	}
	req->open_req_list = NULL;
	req->num_open_req = 0;

	/* free pending writable result packets */
	item = req->done_req_list;
	while(item) {
		nitem = item->next;
		lock_basic_lock(&stream_wait_count_lock);
		stream_wait_count -= (sizeof(struct tcp_req_done_item)
			+item->len);
		lock_basic_unlock(&stream_wait_count_lock);
		free(item->buf);
		free(item);
		item = nitem;
	}
	req->done_req_list = NULL;
	req->num_done_req = 0;
	req->read_is_closed = 0;
}

void
tcp_req_info_remove_mesh_state(struct tcp_req_info* req, struct mesh_state* m)
{
	struct tcp_req_open_item* open, *prev = NULL;
	if(!req || !m) return;
	open = req->open_req_list;
	while(open) {
		if(open->mesh_state == m) {
			struct tcp_req_open_item* next;
			if(prev) prev->next = open->next;
			else req->open_req_list = open->next;
			/* caller has to manage the mesh state reply entry */
			next = open->next;
			free(open);
			req->num_open_req --;

			/* prev = prev; */
			open = next;
			continue;
		}
		prev = open;
		open = open->next;
	}
}

/** setup listening for read or write */
static void
tcp_req_info_setup_listen(struct tcp_req_info* req)
{
	int wr = 0;
	int rd = 0;

	if(req->cp->tcp_byte_count != 0) {
		/* cannot change, halfway through */
		return;
	}

	if(!req->cp->tcp_is_reading)
		wr = 1;
	if(!req->read_is_closed)
		rd = 1;

	if(wr) {
		req->cp->tcp_is_reading = 0;
		comm_point_stop_listening(req->cp);
		comm_point_start_listening(req->cp, -1,
			adjusted_tcp_timeout(req->cp));
	} else if(rd) {
		req->cp->tcp_is_reading = 1;
		comm_point_stop_listening(req->cp);
		comm_point_start_listening(req->cp, -1,
			adjusted_tcp_timeout(req->cp));
		/* and also read it (from SSL stack buffers), so
		 * no event read event is expected since the remainder of
		 * the TLS frame is sitting in the buffers. */
		req->read_again = 1;
	} else {
		comm_point_stop_listening(req->cp);
		comm_point_start_listening(req->cp, -1,
			adjusted_tcp_timeout(req->cp));
		comm_point_listen_for_rw(req->cp, 0, 0);
	}
}

/** remove first item from list of pending results */
static struct tcp_req_done_item*
tcp_req_info_pop_done(struct tcp_req_info* req)
{
	struct tcp_req_done_item* item;
	log_assert(req->num_done_req > 0 && req->done_req_list);
	item = req->done_req_list;
	lock_basic_lock(&stream_wait_count_lock);
	stream_wait_count -= (sizeof(struct tcp_req_done_item)+item->len);
	lock_basic_unlock(&stream_wait_count_lock);
	req->done_req_list = req->done_req_list->next;
	req->num_done_req --;
	return item;
}

/** Send given buffer and setup to write */
static void
tcp_req_info_start_write_buf(struct tcp_req_info* req, uint8_t* buf,
	size_t len)
{
	sldns_buffer_clear(req->cp->buffer);
	sldns_buffer_write(req->cp->buffer, buf, len);
	sldns_buffer_flip(req->cp->buffer);

	req->cp->tcp_is_reading = 0; /* we are now writing */
}

/** pick up the next result and start writing it to the channel */
static void
tcp_req_pickup_next_result(struct tcp_req_info* req)
{
	if(req->num_done_req > 0) {
		/* unlist the done item from the list of pending results */
		struct tcp_req_done_item* item = tcp_req_info_pop_done(req);
		tcp_req_info_start_write_buf(req, item->buf, item->len);
		free(item->buf);
		free(item);
	}
}

/** the read channel has closed */
int
tcp_req_info_handle_read_close(struct tcp_req_info* req)
{
	verbose(VERB_ALGO, "tcp channel read side closed %d", req->cp->fd);
	/* reset byte count for (potential) partial read */
	req->cp->tcp_byte_count = 0;
	/* if we still have results to write, pick up next and write it */
	if(req->num_done_req != 0) {
		tcp_req_pickup_next_result(req);
		tcp_req_info_setup_listen(req);
		return 1;
	}
	/* if nothing to do, this closes the connection */
	if(req->num_open_req == 0 && req->num_done_req == 0)
		return 0;
	/* otherwise, we must be waiting for dns resolve, wait with timeout */
	req->read_is_closed = 1;
	tcp_req_info_setup_listen(req);
	return 1;
}

void
tcp_req_info_handle_writedone(struct tcp_req_info* req)
{
	/* back to reading state, we finished this write event */
	sldns_buffer_clear(req->cp->buffer);
	if(req->num_done_req == 0 && req->read_is_closed) {
		/* no more to write and nothing to read, close it */
		comm_point_drop_reply(&req->cp->repinfo);
		return;
	}
	req->cp->tcp_is_reading = 1;
	/* see if another result needs writing */
	tcp_req_pickup_next_result(req);

	/* see if there is more to write, if not stop_listening for writing */
	/* see if new requests are allowed, if so, start_listening
	 * for reading */
	tcp_req_info_setup_listen(req);
}

void
tcp_req_info_handle_readdone(struct tcp_req_info* req)
{
	struct comm_point* c = req->cp;

	/* we want to read up several requests, unless there are
	 * pending answers */

	req->is_drop = 0;
	req->is_reply = 0;
	req->in_worker_handle = 1;
	sldns_buffer_set_limit(req->spool_buffer, 0);
	/* handle the current request */
	/* this calls the worker handle request routine that could give
	 * a cache response, or localdata response, or drop the reply,
	 * or schedule a mesh entry for later */
	fptr_ok(fptr_whitelist_comm_point(c->callback));
	if( (*c->callback)(c, c->cb_arg, NETEVENT_NOERROR, &c->repinfo) ) {
		req->in_worker_handle = 0;
		/* there is an answer, put it up.  It is already in the
		 * c->buffer, just send it. */
		/* since we were just reading a query, the channel is
		 * clear to write to */
	send_it:
		c->tcp_is_reading = 0;
		comm_point_stop_listening(c);
		comm_point_start_listening(c, -1, adjusted_tcp_timeout(c));
		return;
	}
	req->in_worker_handle = 0;
	/* it should be waiting in the mesh for recursion.
	 * If mesh failed to add a new entry and called commpoint_drop_reply.
	 * Then the mesh state has been cleared. */
	if(req->is_drop) {
		/* the reply has been dropped, stream has been closed. */
		return;
	}
	/* If mesh failed(mallocfail) and called commpoint_send_reply with
	 * something like servfail then we pick up that reply below. */
	if(req->is_reply) {
		goto send_it;
	}

	sldns_buffer_clear(c->buffer);
	/* if pending answers, pick up an answer and start sending it */
	tcp_req_pickup_next_result(req);

	/* if answers pending, start sending answers */
	/* read more requests if we can have more requests */
	tcp_req_info_setup_listen(req);
}

int
tcp_req_info_add_meshstate(struct tcp_req_info* req,
	struct mesh_area* mesh, struct mesh_state* m)
{
	struct tcp_req_open_item* item;
	log_assert(req && mesh && m);
	item = (struct tcp_req_open_item*)malloc(sizeof(*item));
	if(!item) return 0;
	item->next = req->open_req_list;
	item->mesh = mesh;
	item->mesh_state = m;
	req->open_req_list = item;
	req->num_open_req++;
	return 1;
}

/** Add a result to the result list.  At the end. */
static int
tcp_req_info_add_result(struct tcp_req_info* req, uint8_t* buf, size_t len)
{
	struct tcp_req_done_item* last = NULL;
	struct tcp_req_done_item* item;
	size_t space;

	/* see if we have space */
	space = sizeof(struct tcp_req_done_item) + len;
	lock_basic_lock(&stream_wait_count_lock);
	if(stream_wait_count + space > stream_wait_max) {
		lock_basic_unlock(&stream_wait_count_lock);
		verbose(VERB_ALGO, "drop stream reply, no space left, in stream-wait-size");
		return 0;
	}
	stream_wait_count += space;
	lock_basic_unlock(&stream_wait_count_lock);

	/* find last element */
	last = req->done_req_list;
	while(last && last->next)
		last = last->next;

	/* create new element */
	item = (struct tcp_req_done_item*)malloc(sizeof(*item));
	if(!item) {
		log_err("malloc failure, for stream result list");
		return 0;
	}
	item->next = NULL;
	item->len = len;
	item->buf = memdup(buf, len);
	if(!item->buf) {
		free(item);
		log_err("malloc failure, adding reply to stream result list");
		return 0;
	}

	/* link in */
	if(last) last->next = item;
	else req->done_req_list = item;
	req->num_done_req++;
	return 1;
}

void
tcp_req_info_send_reply(struct tcp_req_info* req)
{
	if(req->in_worker_handle) {
		/* reply from mesh is in the spool_buffer */
		/* copy now, so that the spool buffer is free for other tasks
		 * before the callback is done */
		sldns_buffer_clear(req->cp->buffer);
		sldns_buffer_write(req->cp->buffer,
			sldns_buffer_begin(req->spool_buffer),
			sldns_buffer_limit(req->spool_buffer));
		sldns_buffer_flip(req->cp->buffer);
		req->is_reply = 1;
		return;
	}
	/* now that the query has been handled, that mesh_reply entry
	 * should be removed, from the tcp_req_info list,
	 * the mesh state cleanup removes then with region_cleanup and
	 * replies_sent true. */
	/* see if we can send it straight away (we are not doing
	 * anything else).  If so, copy to buffer and start */
	if(req->cp->tcp_is_reading && req->cp->tcp_byte_count == 0) {
		/* buffer is free, and was ready to read new query into,
		 * but we are now going to use it to send this answer */
		tcp_req_info_start_write_buf(req,
			sldns_buffer_begin(req->spool_buffer),
			sldns_buffer_limit(req->spool_buffer));
		/* switch to listen to write events */
		comm_point_stop_listening(req->cp);
		comm_point_start_listening(req->cp, -1,
			adjusted_tcp_timeout(req->cp));
		return;
	}
	/* queue up the answer behind the others already pending */
	if(!tcp_req_info_add_result(req, sldns_buffer_begin(req->spool_buffer),
		sldns_buffer_limit(req->spool_buffer))) {
		/* drop the connection, we are out of resources */
		comm_point_drop_reply(&req->cp->repinfo);
	}
}

size_t tcp_req_info_get_stream_buffer_size(void)
{
	size_t s;
	if(!stream_wait_lock_inited)
		return stream_wait_count;
	lock_basic_lock(&stream_wait_count_lock);
	s = stream_wait_count;
	lock_basic_unlock(&stream_wait_count_lock);
	return s;
}

size_t http2_get_query_buffer_size(void)
{
	size_t s;
	if(!http2_query_buffer_lock_inited)
		return http2_query_buffer_count;
	lock_basic_lock(&http2_query_buffer_count_lock);
	s = http2_query_buffer_count;
	lock_basic_unlock(&http2_query_buffer_count_lock);
	return s;
}

size_t http2_get_response_buffer_size(void)
{
	size_t s;
	if(!http2_response_buffer_lock_inited)
		return http2_response_buffer_count;
	lock_basic_lock(&http2_response_buffer_count_lock);
	s = http2_response_buffer_count;
	lock_basic_unlock(&http2_response_buffer_count_lock);
	return s;
}

#ifdef HAVE_NGHTTP2
/** nghttp2 callback. Used to copy response from rbuffer to nghttp2 session */
static ssize_t http2_submit_response_read_callback(
	nghttp2_session* ATTR_UNUSED(session),
	int32_t stream_id, uint8_t* buf, size_t length, uint32_t* data_flags,
	nghttp2_data_source* source, void* ATTR_UNUSED(cb_arg))
{
	struct http2_stream* h2_stream;
	struct http2_session* h2_session = source->ptr;
	size_t copylen = length;
	if(!(h2_stream = nghttp2_session_get_stream_user_data(
		h2_session->session, stream_id))) {
		verbose(VERB_QUERY, "http2: cannot get stream data, closing "
			"stream");
		return NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE;
	}
	if(!h2_stream->rbuffer ||
		sldns_buffer_remaining(h2_stream->rbuffer) == 0) {
		verbose(VERB_QUERY, "http2: cannot submit buffer. No data "
			"available in rbuffer");
		/* rbuffer will be free'd in frame close cb */
		return NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE;
	}

	if(copylen > sldns_buffer_remaining(h2_stream->rbuffer))
		copylen = sldns_buffer_remaining(h2_stream->rbuffer);
	if(copylen > SSIZE_MAX)
		copylen = SSIZE_MAX; /* will probably never happen */

	memcpy(buf, sldns_buffer_current(h2_stream->rbuffer), copylen);
	sldns_buffer_skip(h2_stream->rbuffer, copylen);

	if(sldns_buffer_remaining(h2_stream->rbuffer) == 0) {
		*data_flags |= NGHTTP2_DATA_FLAG_EOF;
		lock_basic_lock(&http2_response_buffer_count_lock);
		http2_response_buffer_count -=
			sldns_buffer_capacity(h2_stream->rbuffer);
		lock_basic_unlock(&http2_response_buffer_count_lock);
		sldns_buffer_free(h2_stream->rbuffer);
		h2_stream->rbuffer = NULL;
	}

	return copylen;
}

/**
 * Send RST_STREAM frame for stream.
 * @param h2_session: http2 session to submit frame to
 * @param h2_stream: http2 stream containing frame ID to use in RST_STREAM
 * @return 0 on error, 1 otherwise
 */
static int http2_submit_rst_stream(struct http2_session* h2_session,
		struct http2_stream* h2_stream)
{
	int ret = nghttp2_submit_rst_stream(h2_session->session,
		NGHTTP2_FLAG_NONE, h2_stream->stream_id,
		NGHTTP2_INTERNAL_ERROR);
	if(ret) {
		verbose(VERB_QUERY, "http2: nghttp2_submit_rst_stream failed, "
			"error: %s", nghttp2_strerror(ret));
		return 0;
	}
	return 1;
}

/**
 * DNS response ready to be submitted to nghttp2, to be prepared for sending
 * out. Response is stored in c->buffer. Copy to rbuffer because the c->buffer
 * might be used before this will be sent out.
 * @param h2_session: http2 session, containing c->buffer which contains answer
 * @return 0 on error, 1 otherwise
 */
int http2_submit_dns_response(struct http2_session* h2_session)
{
	int ret;
	nghttp2_data_provider data_prd;
	char status[4];
	nghttp2_nv headers[3];
	struct http2_stream* h2_stream = h2_session->c->h2_stream;
	size_t rlen;
	char rlen_str[32];

	if(h2_stream->rbuffer) {
		log_err("http2 submit response error: rbuffer already "
			"exists");
		return 0;
	}
	if(sldns_buffer_remaining(h2_session->c->buffer) == 0) {
		log_err("http2 submit response error: c->buffer not complete");
		return 0;
	}

	if(snprintf(status, 4, "%d", h2_stream->status) != 3) {
		verbose(VERB_QUERY, "http2: submit response error: "
			"invalid status");
		return 0;
	}

	rlen = sldns_buffer_remaining(h2_session->c->buffer);
	snprintf(rlen_str, sizeof(rlen_str), "%u", (unsigned)rlen);

	lock_basic_lock(&http2_response_buffer_count_lock);
	if(http2_response_buffer_count + rlen > http2_response_buffer_max) {
		lock_basic_unlock(&http2_response_buffer_count_lock);
		verbose(VERB_ALGO, "reset HTTP2 stream, no space left, "
			"in https-response-buffer-size");
		return http2_submit_rst_stream(h2_session, h2_stream);
	}
	http2_response_buffer_count += rlen;
	lock_basic_unlock(&http2_response_buffer_count_lock);

	if(!(h2_stream->rbuffer = sldns_buffer_new(rlen))) {
		lock_basic_lock(&http2_response_buffer_count_lock);
		http2_response_buffer_count -= rlen;
		lock_basic_unlock(&http2_response_buffer_count_lock);
		log_err("http2 submit response error: malloc failure");
		return 0;
	}

	headers[0].name = (uint8_t*)":status";
	headers[0].namelen = 7;
	headers[0].value = (uint8_t*)status;
	headers[0].valuelen = 3;
	headers[0].flags = NGHTTP2_NV_FLAG_NONE;

	headers[1].name = (uint8_t*)"content-type";
	headers[1].namelen = 12;
	headers[1].value = (uint8_t*)"application/dns-message";
	headers[1].valuelen = 23;
	headers[1].flags = NGHTTP2_NV_FLAG_NONE;

	headers[2].name = (uint8_t*)"content-length";
	headers[2].namelen = 14;
	headers[2].value = (uint8_t*)rlen_str;
	headers[2].valuelen = strlen(rlen_str);
	headers[2].flags = NGHTTP2_NV_FLAG_NONE;

	sldns_buffer_write(h2_stream->rbuffer,
		sldns_buffer_current(h2_session->c->buffer),
		sldns_buffer_remaining(h2_session->c->buffer));
	sldns_buffer_flip(h2_stream->rbuffer);

	data_prd.source.ptr = h2_session;
	data_prd.read_callback = http2_submit_response_read_callback;
	ret = nghttp2_submit_response(h2_session->session, h2_stream->stream_id,
		headers, 3, &data_prd);
	if(ret) {
		verbose(VERB_QUERY, "http2: set_stream_user_data failed, "
			"error: %s", nghttp2_strerror(ret));
		return 0;
	}
	return 1;
}
#else
int http2_submit_dns_response(void* ATTR_UNUSED(v))
{
	return 0;
}
#endif

#ifdef HAVE_NGHTTP2
/** HTTP status to descriptive string */
static char* http_status_to_str(enum http_status s)
{
	switch(s) {
		case HTTP_STATUS_OK:
			return "OK";
		case HTTP_STATUS_BAD_REQUEST:
			return "Bad Request";
		case HTTP_STATUS_NOT_FOUND:
			return "Not Found";
		case HTTP_STATUS_PAYLOAD_TOO_LARGE:
			return "Payload Too Large";
		case HTTP_STATUS_URI_TOO_LONG:
			return "URI Too Long";
		case HTTP_STATUS_UNSUPPORTED_MEDIA_TYPE:
			return "Unsupported Media Type";
		case HTTP_STATUS_NOT_IMPLEMENTED:
			return "Not Implemented";
	}
	return "Status Unknown";
}

/** nghttp2 callback. Used to copy error message to nghttp2 session */
static ssize_t http2_submit_error_read_callback(
	nghttp2_session* ATTR_UNUSED(session),
	int32_t stream_id, uint8_t* buf, size_t length, uint32_t* data_flags,
	nghttp2_data_source* source, void* ATTR_UNUSED(cb_arg))
{
	struct http2_stream* h2_stream;
	struct http2_session* h2_session = source->ptr;
	char* msg;
	if(!(h2_stream = nghttp2_session_get_stream_user_data(
		h2_session->session, stream_id))) {
		verbose(VERB_QUERY, "http2: cannot get stream data, closing "
			"stream");
		return NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE;
	}
	*data_flags |= NGHTTP2_DATA_FLAG_EOF;
	msg = http_status_to_str(h2_stream->status);
	if(length < strlen(msg))
		return 0; /* not worth trying over multiple frames */
	memcpy(buf, msg, strlen(msg));
	return strlen(msg);

}

/**
 * HTTP error response ready to be submitted to nghttp2, to be prepared for
 * sending out. Message body will contain descriptive string for HTTP status.
 * @param h2_session: http2 session to submit to
 * @param h2_stream: http2 stream containing HTTP status to use for error
 * @return 0 on error, 1 otherwise
 */
static int http2_submit_error(struct http2_session* h2_session,
	struct http2_stream* h2_stream)
{
	int ret;
	char status[4];
	nghttp2_data_provider data_prd;
	nghttp2_nv headers[1]; /* will be copied by nghttp */
	if(snprintf(status, 4, "%d", h2_stream->status) != 3) {
		verbose(VERB_QUERY, "http2: submit error failed, "
			"invalid status");
		return 0;
	}
	headers[0].name = (uint8_t*)":status";
	headers[0].namelen = 7;
	headers[0].value = (uint8_t*)status;
	headers[0].valuelen = 3;
	headers[0].flags = NGHTTP2_NV_FLAG_NONE;

	data_prd.source.ptr = h2_session;
	data_prd.read_callback = http2_submit_error_read_callback;

	ret = nghttp2_submit_response(h2_session->session, h2_stream->stream_id,
		headers, 1, &data_prd);
	if(ret) {
		verbose(VERB_QUERY, "http2: submit error failed, "
			"error: %s", nghttp2_strerror(ret));
		return 0;
	}
	return 1;
}

/**
 * Start query handling. Query is stored in the stream, and will be free'd here.
 * @param h2_session: http2 session, containing comm point
 * @param h2_stream: stream containing buffered query
 * @return: -1 on error, 1 if answer is stored in c->buffer, 0 if there is no
 * reply available (yet).
 */
static int http2_query_read_done(struct http2_session* h2_session,
	struct http2_stream* h2_stream)
{
	log_assert(h2_stream->qbuffer);

	if(h2_session->c->h2_stream) {
		verbose(VERB_ALGO, "http2_query_read_done failure: shared "
			"buffer already assigned to stream");
		return -1;
	}

    /* the c->buffer might be used by mesh_send_reply and no be cleard
	 * need to be cleared before use */
	sldns_buffer_clear(h2_session->c->buffer);
	if(sldns_buffer_remaining(h2_session->c->buffer) <
		sldns_buffer_remaining(h2_stream->qbuffer)) {
		/* qbuffer will be free'd in frame close cb */
		sldns_buffer_clear(h2_session->c->buffer);
		verbose(VERB_ALGO, "http2_query_read_done failure: can't fit "
			"qbuffer in c->buffer");
		return -1;
	}

	sldns_buffer_write(h2_session->c->buffer,
		sldns_buffer_current(h2_stream->qbuffer),
		sldns_buffer_remaining(h2_stream->qbuffer));

	lock_basic_lock(&http2_query_buffer_count_lock);
	http2_query_buffer_count -= sldns_buffer_capacity(h2_stream->qbuffer);
	lock_basic_unlock(&http2_query_buffer_count_lock);
	sldns_buffer_free(h2_stream->qbuffer);
	h2_stream->qbuffer = NULL;

	sldns_buffer_flip(h2_session->c->buffer);
	h2_session->c->h2_stream = h2_stream;
	fptr_ok(fptr_whitelist_comm_point(h2_session->c->callback));
	if((*h2_session->c->callback)(h2_session->c, h2_session->c->cb_arg,
		NETEVENT_NOERROR, &h2_session->c->repinfo)) {
		return 1; /* answer in c->buffer */
	}
	sldns_buffer_clear(h2_session->c->buffer);
	h2_session->c->h2_stream = NULL;
	return 0; /* mesh state added, or dropped */
}

/** nghttp2 callback. Used to check if the received frame indicates the end of a
 * stream. Gather collected request data and start query handling. */
static int http2_req_frame_recv_cb(nghttp2_session* session,
	const nghttp2_frame* frame, void* cb_arg)
{
	struct http2_session* h2_session = (struct http2_session*)cb_arg;
	struct http2_stream* h2_stream;
	int query_read_done;

	if((frame->hd.type != NGHTTP2_DATA &&
		frame->hd.type != NGHTTP2_HEADERS) ||
		!(frame->hd.flags & NGHTTP2_FLAG_END_STREAM)) {
			return 0;
	}

	if(!(h2_stream = nghttp2_session_get_stream_user_data(
		session, frame->hd.stream_id)))
		return 0;

	if(h2_stream->invalid_endpoint) {
		h2_stream->status = HTTP_STATUS_NOT_FOUND;
		goto submit_http_error;
	}

	if(h2_stream->invalid_content_type) {
		h2_stream->status = HTTP_STATUS_UNSUPPORTED_MEDIA_TYPE;
		goto submit_http_error;
	}

	if(h2_stream->http_method != HTTP_METHOD_GET &&
		h2_stream->http_method != HTTP_METHOD_POST) {
		h2_stream->status = HTTP_STATUS_NOT_IMPLEMENTED;
		goto submit_http_error;
	}

	if(h2_stream->query_too_large) {
		if(h2_stream->http_method == HTTP_METHOD_POST)
			h2_stream->status = HTTP_STATUS_PAYLOAD_TOO_LARGE;
		else
			h2_stream->status = HTTP_STATUS_URI_TOO_LONG;
		goto submit_http_error;
	}

	if(!h2_stream->qbuffer) {
		h2_stream->status = HTTP_STATUS_BAD_REQUEST;
		goto submit_http_error;
	}

	if(h2_stream->status) {
submit_http_error:
		verbose(VERB_QUERY, "http2 request invalid, returning :status="
			"%d", h2_stream->status);
		if(!http2_submit_error(h2_session, h2_stream)) {
			return NGHTTP2_ERR_CALLBACK_FAILURE;
		}
		return 0;
	}
	h2_stream->status = HTTP_STATUS_OK;

	sldns_buffer_flip(h2_stream->qbuffer);
	h2_session->postpone_drop = 1;
	query_read_done = http2_query_read_done(h2_session, h2_stream);
	if(query_read_done < 0)
		return NGHTTP2_ERR_CALLBACK_FAILURE;
	else if(!query_read_done) {
		if(h2_session->is_drop) {
			/* connection needs to be closed. Return failure to make
			 * sure no other action are taken anymore on comm point.
			 * failure will result in reclaiming (and closing)
			 * of comm point. */
			verbose(VERB_QUERY, "http2 query dropped in worker cb");
			h2_session->postpone_drop = 0;
			return NGHTTP2_ERR_CALLBACK_FAILURE;
		}
		/* nothing to submit right now, query added to mesh. */
		h2_session->postpone_drop = 0;
		return 0;
	}
	if(!http2_submit_dns_response(h2_session)) {
		sldns_buffer_clear(h2_session->c->buffer);
		h2_session->c->h2_stream = NULL;
		return NGHTTP2_ERR_CALLBACK_FAILURE;
	}
	verbose(VERB_QUERY, "http2 query submitted to session");
	sldns_buffer_clear(h2_session->c->buffer);
	h2_session->c->h2_stream = NULL;
	return 0;
}

/** nghttp2 callback. Used to detect start of new streams. */
static int http2_req_begin_headers_cb(nghttp2_session* session,
	const nghttp2_frame* frame, void* cb_arg)
{
	struct http2_session* h2_session = (struct http2_session*)cb_arg;
	struct http2_stream* h2_stream;
	int ret;
	if(frame->hd.type != NGHTTP2_HEADERS ||
		frame->headers.cat != NGHTTP2_HCAT_REQUEST) {
		/* only interested in request headers */
		return 0;
	}
	if(!(h2_stream = http2_stream_create(frame->hd.stream_id))) {
		log_err("malloc failure while creating http2 stream");
		return NGHTTP2_ERR_CALLBACK_FAILURE;
	}
	http2_session_add_stream(h2_session, h2_stream);
	ret = nghttp2_session_set_stream_user_data(session,
		frame->hd.stream_id, h2_stream);
	if(ret) {
		/* stream does not exist */
		verbose(VERB_QUERY, "http2: set_stream_user_data failed, "
			"error: %s", nghttp2_strerror(ret));
		return NGHTTP2_ERR_CALLBACK_FAILURE;
	}

	return 0;
}

/**
 * base64url decode, store in qbuffer
 * @param h2_session: http2 session
 * @param h2_stream: http2 stream
 * @param start: start of the base64 string
 * @param length: length of the base64 string
 * @return: 0 on error, 1 otherwise. query will be stored in h2_stream->qbuffer,
 * buffer will be NULL is unparseble.
 */
static int http2_buffer_uri_query(struct http2_session* h2_session,
	struct http2_stream* h2_stream, const uint8_t* start, size_t length)
{
	size_t expectb64len;
	int b64len;
	if(h2_stream->http_method == HTTP_METHOD_POST)
		return 1;
	if(length == 0)
		return 1;
	if(h2_stream->qbuffer) {
		verbose(VERB_ALGO, "http2_req_header fail, "
			"qbuffer already set");
		return 0;
	}

	/* calculate size, might be a bit bigger than the real
	 * decoded buffer size */
	expectb64len = sldns_b64_pton_calculate_size(length);
	log_assert(expectb64len > 0);
	if(expectb64len >
		h2_session->c->http2_stream_max_qbuffer_size) {
		h2_stream->query_too_large = 1;
		return 1;
	}

	lock_basic_lock(&http2_query_buffer_count_lock);
	if(http2_query_buffer_count + expectb64len > http2_query_buffer_max) {
		lock_basic_unlock(&http2_query_buffer_count_lock);
		verbose(VERB_ALGO, "reset HTTP2 stream, no space left, "
			"in http2-query-buffer-size");
		return http2_submit_rst_stream(h2_session, h2_stream);
	}
	http2_query_buffer_count += expectb64len;
	lock_basic_unlock(&http2_query_buffer_count_lock);
	if(!(h2_stream->qbuffer = sldns_buffer_new(expectb64len))) {
		lock_basic_lock(&http2_query_buffer_count_lock);
		http2_query_buffer_count -= expectb64len;
		lock_basic_unlock(&http2_query_buffer_count_lock);
		log_err("http2_req_header fail, qbuffer "
			"malloc failure");
		return 0;
	}

	if(sldns_b64_contains_nonurl((char const*)start, length)) {
		char buf[65536+4];
		verbose(VERB_ALGO, "HTTP2 stream contains wrong b64 encoding");
		/* copy to the scratch buffer temporarily to terminate the
		 * string with a zero */
		if(length+1 > sizeof(buf)) {
			/* too long */
			lock_basic_lock(&http2_query_buffer_count_lock);
			http2_query_buffer_count -= expectb64len;
			lock_basic_unlock(&http2_query_buffer_count_lock);
			sldns_buffer_free(h2_stream->qbuffer);
			h2_stream->qbuffer = NULL;
			return 1;
		}
		memmove(buf, start, length);
		buf[length] = 0;
		if(!(b64len = sldns_b64_pton(buf, sldns_buffer_current(
			h2_stream->qbuffer), expectb64len)) || b64len < 0) {
			lock_basic_lock(&http2_query_buffer_count_lock);
			http2_query_buffer_count -= expectb64len;
			lock_basic_unlock(&http2_query_buffer_count_lock);
			sldns_buffer_free(h2_stream->qbuffer);
			h2_stream->qbuffer = NULL;
			return 1;
		}
	} else {
		if(!(b64len = sldns_b64url_pton(
			(char const *)start, length,
			sldns_buffer_current(h2_stream->qbuffer),
			expectb64len)) || b64len < 0) {
			lock_basic_lock(&http2_query_buffer_count_lock);
			http2_query_buffer_count -= expectb64len;
			lock_basic_unlock(&http2_query_buffer_count_lock);
			sldns_buffer_free(h2_stream->qbuffer);
			h2_stream->qbuffer = NULL;
			/* return without error, method can be an
			 * unknown POST */
			return 1;
		}
	}
	sldns_buffer_skip(h2_stream->qbuffer, (size_t)b64len);
	return 1;
}

/** nghttp2 callback. Used to parse headers from HEADER frames. */
static int http2_req_header_cb(nghttp2_session* session,
	const nghttp2_frame* frame, const uint8_t* name, size_t namelen,
	const uint8_t* value, size_t valuelen, uint8_t ATTR_UNUSED(flags),
	void* cb_arg)
{
	struct http2_stream* h2_stream = NULL;
	struct http2_session* h2_session = (struct http2_session*)cb_arg;
	/* nghttp2 deals with CONTINUATION frames and provides them as part of
	 * the HEADER */
	if(frame->hd.type != NGHTTP2_HEADERS ||
		frame->headers.cat != NGHTTP2_HCAT_REQUEST) {
		/* only interested in request headers */
		return 0;
	}
	if(!(h2_stream = nghttp2_session_get_stream_user_data(session,
		frame->hd.stream_id)))
		return 0;

	/* earlier checks already indicate we can stop handling this query */
	if(h2_stream->http_method == HTTP_METHOD_UNSUPPORTED ||
		h2_stream->invalid_content_type ||
		h2_stream->invalid_endpoint)
		return 0;


	/* nghttp2 performs some sanity checks in the headers, including:
	 * name and value are guaranteed to be null terminated
	 * name is guaranteed to be lowercase
	 * content-length value is guaranteed to contain digits
	 */

	if(!h2_stream->http_method && namelen == 7 &&
		memcmp(":method", name, namelen) == 0) {
		/* Case insensitive check on :method value to be on the safe
		 * side. I failed to find text about case sensitivity in specs.
		 */
		if(valuelen == 3 && strcasecmp("GET", (const char*)value) == 0)
			h2_stream->http_method = HTTP_METHOD_GET;
		else if(valuelen == 4 &&
			strcasecmp("POST", (const char*)value) == 0) {
			h2_stream->http_method = HTTP_METHOD_POST;
			if(h2_stream->qbuffer) {
				/* POST method uses query from DATA frames */
				lock_basic_lock(&http2_query_buffer_count_lock);
				http2_query_buffer_count -=
					sldns_buffer_capacity(h2_stream->qbuffer);
				lock_basic_unlock(&http2_query_buffer_count_lock);
				sldns_buffer_free(h2_stream->qbuffer);
				h2_stream->qbuffer = NULL;
			}
		} else
			h2_stream->http_method = HTTP_METHOD_UNSUPPORTED;
		return 0;
	}
	if(namelen == 5 && memcmp(":path", name, namelen) == 0) {
		/* :path may contain DNS query, depending on method. Method might
		 * not be known yet here, so check after finishing receiving
		 * stream. */
#define	HTTP_QUERY_PARAM "?dns="
		size_t el = strlen(h2_session->c->http_endpoint);
		size_t qpl = strlen(HTTP_QUERY_PARAM);

		if(valuelen < el || memcmp(h2_session->c->http_endpoint,
			value, el) != 0) {
			h2_stream->invalid_endpoint = 1;
			return 0;
		}
		/* larger than endpoint only allowed if it is for the query
		 * parameter */
		if(valuelen <= el+qpl ||
			memcmp(HTTP_QUERY_PARAM, value+el, qpl) != 0) {
			if(valuelen != el)
				h2_stream->invalid_endpoint = 1;
			return 0;
		}

		if(!http2_buffer_uri_query(h2_session, h2_stream,
			value+(el+qpl), valuelen-(el+qpl))) {
			return NGHTTP2_ERR_CALLBACK_FAILURE;
		}
		return 0;
	}
	/* Content type is a SHOULD (rfc7231#section-3.1.1.5) when using POST,
	 * and not needed when using GET. Don't enfore.
	 * If set only allow lowercase "application/dns-message".
	 *
	 * Clients SHOULD (rfc8484#section-4.1) set an accept header, but MUST
	 * be able to handle "application/dns-message". Since that is the only
	 * content-type supported we can ignore the accept header.
	 */
	if((namelen == 12 && memcmp("content-type", name, namelen) == 0)) {
		if(valuelen != 23 || memcmp("application/dns-message", value,
			valuelen) != 0) {
			h2_stream->invalid_content_type = 1;
		}
	}

	/* Only interested in content-lentg for POST (on not yet known) method.
	 */
	if((!h2_stream->http_method ||
		h2_stream->http_method == HTTP_METHOD_POST) &&
		!h2_stream->content_length && namelen  == 14 &&
		memcmp("content-length", name, namelen) == 0) {
		if(valuelen > 5) {
			h2_stream->query_too_large = 1;
			return 0;
		}
		/* guaranteed to only contain digits and be null terminated */
		h2_stream->content_length = atoi((const char*)value);
		if(h2_stream->content_length >
			h2_session->c->http2_stream_max_qbuffer_size) {
			h2_stream->query_too_large = 1;
			return 0;
		}
	}
	return 0;
}

/** nghttp2 callback. Used to get data from DATA frames, which can contain
 * queries in POST requests. */
static int http2_req_data_chunk_recv_cb(nghttp2_session* ATTR_UNUSED(session),
	uint8_t ATTR_UNUSED(flags), int32_t stream_id, const uint8_t* data,
	size_t len, void* cb_arg)
{
	struct http2_session* h2_session = (struct http2_session*)cb_arg;
	struct http2_stream* h2_stream;
	size_t qlen = 0;

	if(!(h2_stream = nghttp2_session_get_stream_user_data(
		h2_session->session, stream_id))) {
		return 0;
	}

	if(h2_stream->query_too_large)
		return 0;

	if(!h2_stream->qbuffer) {
		if(h2_stream->content_length) {
			if(h2_stream->content_length < len)
				/* getting more data in DATA frame than
				 * advertised in content-length header. */
				return NGHTTP2_ERR_CALLBACK_FAILURE;
			qlen = h2_stream->content_length;
		} else if(len <= h2_session->c->http2_stream_max_qbuffer_size) {
			/* setting this to msg-buffer-size can result in a lot
			 * of memory consuption. Most queries should fit in a
			 * single DATA frame, and most POST queries will
			 * contain content-length which does not impose this
			 * limit. */
			qlen = len;
		}
	}
	if(!h2_stream->qbuffer && qlen) {
		lock_basic_lock(&http2_query_buffer_count_lock);
		if(http2_query_buffer_count + qlen > http2_query_buffer_max) {
			lock_basic_unlock(&http2_query_buffer_count_lock);
			verbose(VERB_ALGO, "reset HTTP2 stream, no space left, "
				"in http2-query-buffer-size");
			return http2_submit_rst_stream(h2_session, h2_stream);
		}
		http2_query_buffer_count += qlen;
		lock_basic_unlock(&http2_query_buffer_count_lock);
		if(!(h2_stream->qbuffer = sldns_buffer_new(qlen))) {
			lock_basic_lock(&http2_query_buffer_count_lock);
			http2_query_buffer_count -= qlen;
			lock_basic_unlock(&http2_query_buffer_count_lock);
		}
	}

	if(!h2_stream->qbuffer ||
		sldns_buffer_remaining(h2_stream->qbuffer) < len) {
		verbose(VERB_ALGO, "http2 data_chunck_recv failed. Not enough "
			"buffer space for POST query. Can happen on multi "
			"frame requests without content-length header");
		h2_stream->query_too_large = 1;
		return 0;
	}

	sldns_buffer_write(h2_stream->qbuffer, data, len);

	return 0;
}

void http2_req_stream_clear(struct http2_stream* h2_stream)
{
	if(h2_stream->qbuffer) {
		lock_basic_lock(&http2_query_buffer_count_lock);
		http2_query_buffer_count -=
			sldns_buffer_capacity(h2_stream->qbuffer);
		lock_basic_unlock(&http2_query_buffer_count_lock);
		sldns_buffer_free(h2_stream->qbuffer);
		h2_stream->qbuffer = NULL;
	}
	if(h2_stream->rbuffer) {
		lock_basic_lock(&http2_response_buffer_count_lock);
		http2_response_buffer_count -=
			sldns_buffer_capacity(h2_stream->rbuffer);
		lock_basic_unlock(&http2_response_buffer_count_lock);
		sldns_buffer_free(h2_stream->rbuffer);
		h2_stream->rbuffer = NULL;
	}
}

nghttp2_session_callbacks* http2_req_callbacks_create(void)
{
	nghttp2_session_callbacks *callbacks;
	if(nghttp2_session_callbacks_new(&callbacks) == NGHTTP2_ERR_NOMEM) {
		log_err("failed to initialize nghttp2 callback");
		return NULL;
	}
	/* reception of header block started, used to create h2_stream */
	nghttp2_session_callbacks_set_on_begin_headers_callback(callbacks,
		http2_req_begin_headers_cb);
	/* complete frame received, used to get data from stream if frame
	 * has end stream flag, and start processing query */
	nghttp2_session_callbacks_set_on_frame_recv_callback(callbacks,
		http2_req_frame_recv_cb);
	/* get request info from headers */
	nghttp2_session_callbacks_set_on_header_callback(callbacks,
		http2_req_header_cb);
	/* get data from DATA frames, containing POST query */
	nghttp2_session_callbacks_set_on_data_chunk_recv_callback(callbacks,
		http2_req_data_chunk_recv_cb);

	/* generic HTTP2 callbacks */
	nghttp2_session_callbacks_set_recv_callback(callbacks, http2_recv_cb);
	nghttp2_session_callbacks_set_send_callback(callbacks, http2_send_cb);
	nghttp2_session_callbacks_set_on_stream_close_callback(callbacks,
		http2_stream_close_cb);

	return callbacks;
}
#endif /* HAVE_NGHTTP2 */

#ifdef HAVE_NGTCP2
struct doq_table*
doq_table_create(struct config_file* cfg, struct ub_randstate* rnd)
{
	struct doq_table* table = calloc(1, sizeof(*table));
	if(!table)
		return NULL;
	table->idle_timeout = ((uint64_t)cfg->tcp_idle_timeout)*
		NGTCP2_MILLISECONDS;
	table->sv_scidlen = 16;
	table->static_secret_len = 16;
	table->static_secret = malloc(table->static_secret_len);
	if(!table->static_secret) {
		free(table);
		return NULL;
	}
	doq_fill_rand(rnd, table->static_secret, table->static_secret_len);
	table->conn_tree = rbtree_create(doq_conn_cmp);
	if(!table->conn_tree) {
		free(table->static_secret);
		free(table);
		return NULL;
	}
	table->conid_tree = rbtree_create(doq_conid_cmp);
	if(!table->conid_tree) {
		free(table->static_secret);
		free(table->conn_tree);
		free(table);
		return NULL;
	}
	table->timer_tree = rbtree_create(doq_timer_cmp);
	if(!table->timer_tree) {
		free(table->static_secret);
		free(table->conn_tree);
		free(table->conid_tree);
		free(table);
		return NULL;
	}
	lock_rw_init(&table->lock);
	lock_rw_init(&table->conid_lock);
	lock_basic_init(&table->size_lock);
	lock_protect(&table->lock, &table->static_secret,
		sizeof(table->static_secret));
	lock_protect(&table->lock, &table->static_secret_len,
		sizeof(table->static_secret_len));
	lock_protect(&table->lock, table->static_secret,
		table->static_secret_len);
	lock_protect(&table->lock, &table->sv_scidlen,
		sizeof(table->sv_scidlen));
	lock_protect(&table->lock, &table->idle_timeout,
		sizeof(table->idle_timeout));
	lock_protect(&table->lock, &table->conn_tree, sizeof(table->conn_tree));
	lock_protect(&table->lock, table->conn_tree, sizeof(*table->conn_tree));
	lock_protect(&table->conid_lock, table->conid_tree,
		sizeof(*table->conid_tree));
	lock_protect(&table->lock, table->timer_tree,
		sizeof(*table->timer_tree));
	lock_protect(&table->size_lock, &table->current_size,
		sizeof(table->current_size));
	return table;
}

/** delete elements from the connection tree */
static void
conn_tree_del(rbnode_type* node, void* arg)
{
	struct doq_table* table = (struct doq_table*)arg;
	struct doq_conn* conn;
	if(!node)
		return;
	conn = (struct doq_conn*)node->key;
	if(conn->timer.timer_in_list) {
		/* Remove timer from list first, because finding the rbnode
		 * element of the setlist of same timeouts needs tree lookup.
		 * Edit the tree structure after that lookup. */
		doq_timer_list_remove(conn->table, &conn->timer);
	}
	if(conn->timer.timer_in_tree)
		doq_timer_tree_remove(conn->table, &conn->timer);
	doq_table_quic_size_subtract(table, sizeof(*conn)+conn->key.dcidlen);
	doq_conn_delete(conn, table);
}

/** delete elements from the connection id tree */
static void
conid_tree_del(rbnode_type* node, void* ATTR_UNUSED(arg))
{
	if(!node)
		return;
	doq_conid_delete((struct doq_conid*)node->key);
}

void
doq_table_delete(struct doq_table* table)
{
	if(!table)
		return;
	lock_rw_destroy(&table->lock);
	free(table->static_secret);
	if(table->conn_tree) {
		traverse_postorder(table->conn_tree, conn_tree_del, table);
		free(table->conn_tree);
	}
	lock_rw_destroy(&table->conid_lock);
	if(table->conid_tree) {
		/* The tree should be empty, because the doq_conn_delete calls
		 * above should have also removed their conid elements. */
		traverse_postorder(table->conid_tree, conid_tree_del, NULL);
		free(table->conid_tree);
	}
	lock_basic_destroy(&table->size_lock);
	if(table->timer_tree) {
		/* The tree should be empty, because the conn_tree_del calls
		 * above should also have removed them. Also the doq_timer
		 * is part of the doq_conn struct, so is already freed. */
		free(table->timer_tree);
	}
	table->write_list_first = NULL;
	table->write_list_last = NULL;
	free(table);
}

struct doq_timer*
doq_timer_find_time(struct doq_table* table, struct timeval* tv)
{
	struct doq_timer key;
	struct rbnode_type* node;
	memset(&key, 0, sizeof(key));
	key.time.tv_sec = tv->tv_sec;
	key.time.tv_usec = tv->tv_usec;
	node = rbtree_search(table->timer_tree, &key);
	if(node)
		return (struct doq_timer*)node->key;
	return NULL;
}

void
doq_timer_tree_remove(struct doq_table* table, struct doq_timer* timer)
{
	if(!timer->timer_in_tree)
		return;
	rbtree_delete(table->timer_tree, timer);
	timer->timer_in_tree = 0;
	/* This item could have more timers in the same set. */
	if(timer->setlist_first) {
		struct doq_timer* rb_timer = timer->setlist_first;
		/* del first element from setlist */
		if(rb_timer->setlist_next)
			rb_timer->setlist_next->setlist_prev = NULL;
		else
			timer->setlist_last = NULL;
		timer->setlist_first = rb_timer->setlist_next;
		rb_timer->setlist_prev = NULL;
		rb_timer->setlist_next = NULL;
		rb_timer->timer_in_list = 0;
		/* insert it into the tree as new rb element */
		memset(&rb_timer->node, 0, sizeof(rb_timer->node));
		rb_timer->node.key = rb_timer;
		rbtree_insert(table->timer_tree, &rb_timer->node);
		rb_timer->timer_in_tree = 1;
		/* the setlist, if any remainder, moves to the rb element */
		rb_timer->setlist_first = timer->setlist_first;
		rb_timer->setlist_last = timer->setlist_last;
		timer->setlist_first = NULL;
		timer->setlist_last = NULL;
		rb_timer->worker_doq_socket = timer->worker_doq_socket;
	}
	timer->worker_doq_socket = NULL;
}

void
doq_timer_list_remove(struct doq_table* table, struct doq_timer* timer)
{
	struct doq_timer* rb_timer;
	if(!timer->timer_in_list)
		return;
	/* The item in the rbtree has the list start and end. */
	rb_timer = doq_timer_find_time(table, &timer->time);
	if(rb_timer) {
		if(timer->setlist_prev)
			timer->setlist_prev->setlist_next = timer->setlist_next;
		else
			rb_timer->setlist_first = timer->setlist_next;
		if(timer->setlist_next)
			timer->setlist_next->setlist_prev = timer->setlist_prev;
		else
			rb_timer->setlist_last = timer->setlist_prev;
		timer->setlist_prev = NULL;
		timer->setlist_next = NULL;
	}
	timer->timer_in_list = 0;
}

/** doq append timer to setlist */
static void
doq_timer_list_append(struct doq_timer* rb_timer, struct doq_timer* timer)
{
	log_assert(timer->timer_in_list == 0);
	timer->timer_in_list = 1;
	timer->setlist_next = NULL;
	timer->setlist_prev = rb_timer->setlist_last;
	if(rb_timer->setlist_last)
		rb_timer->setlist_last->setlist_next = timer;
	else
		rb_timer->setlist_first = timer;
	rb_timer->setlist_last = timer;
}

void
doq_timer_unset(struct doq_table* table, struct doq_timer* timer)
{
	if(timer->timer_in_list) {
		/* Remove timer from list first, because finding the rbnode
		 * element of the setlist of same timeouts needs tree lookup.
		 * Edit the tree structure after that lookup. */
		doq_timer_list_remove(table, timer);
	}
	if(timer->timer_in_tree)
		doq_timer_tree_remove(table, timer);
	timer->worker_doq_socket = NULL;
}

void doq_timer_set(struct doq_table* table, struct doq_timer* timer,
	struct doq_server_socket* worker_doq_socket, struct timeval* tv)
{
	struct doq_timer* rb_timer;
	if(verbosity >= VERB_ALGO && timer->conn) {
		char a[256];
		struct timeval rel;
		addr_to_str((void*)&timer->conn->key.paddr.addr,
			timer->conn->key.paddr.addrlen, a, sizeof(a));
		timeval_subtract(&rel, tv, worker_doq_socket->now_tv);
		verbose(VERB_ALGO, "doq %s timer set %d.%6.6d in %d.%6.6d",
			a, (int)tv->tv_sec, (int)tv->tv_usec,
			(int)rel.tv_sec, (int)rel.tv_usec);
	}
	if(timer->timer_in_tree || timer->timer_in_list) {
		if(timer->time.tv_sec == tv->tv_sec &&
			timer->time.tv_usec == tv->tv_usec)
			return; /* already set on that time */
		doq_timer_unset(table, timer);
	}
	timer->time.tv_sec = tv->tv_sec;
	timer->time.tv_usec = tv->tv_usec;
	rb_timer = doq_timer_find_time(table, tv);
	if(rb_timer) {
		/* There is a timeout already with this value. Timer is
		 * added to the setlist. */
		doq_timer_list_append(rb_timer, timer);
	} else {
		/* There is no timeout with this value. Make timer a new
		 * tree element. */
		memset(&timer->node, 0, sizeof(timer->node));
		timer->node.key = timer;
		rbtree_insert(table->timer_tree, &timer->node);
		timer->timer_in_tree = 1;
		timer->setlist_first = NULL;
		timer->setlist_last = NULL;
		timer->worker_doq_socket = worker_doq_socket;
	}
}

struct doq_conn*
doq_conn_create(struct comm_point* c, struct doq_pkt_addr* paddr,
	const uint8_t* dcid, size_t dcidlen, uint32_t version)
{
	struct doq_conn* conn = calloc(1, sizeof(*conn));
	if(!conn)
		return NULL;
	conn->node.key = conn;
	conn->doq_socket = c->doq_socket;
	conn->table = c->doq_socket->table;
	memmove(&conn->key.paddr.addr, &paddr->addr, paddr->addrlen);
	conn->key.paddr.addrlen = paddr->addrlen;
	memmove(&conn->key.paddr.localaddr, &paddr->localaddr,
		paddr->localaddrlen);
	conn->key.paddr.localaddrlen = paddr->localaddrlen;
	conn->key.paddr.ifindex = paddr->ifindex;
	conn->key.dcid = memdup((void*)dcid, dcidlen);
	if(!conn->key.dcid) {
		free(conn);
		return NULL;
	}
	conn->key.dcidlen = dcidlen;
	conn->version = version;
#ifdef HAVE_NGTCP2_CCERR_DEFAULT
	ngtcp2_ccerr_default(&conn->ccerr);
#else
	ngtcp2_connection_close_error_default(&conn->last_error);
#endif
	rbtree_init(&conn->stream_tree, &doq_stream_cmp);
	conn->timer.conn = conn;
	lock_basic_init(&conn->lock);
	lock_protect(&conn->lock, &conn->key, sizeof(conn->key));
	lock_protect(&conn->lock, &conn->doq_socket, sizeof(conn->doq_socket));
	lock_protect(&conn->lock, &conn->table, sizeof(conn->table));
	lock_protect(&conn->lock, &conn->is_deleted, sizeof(conn->is_deleted));
	lock_protect(&conn->lock, &conn->version, sizeof(conn->version));
	lock_protect(&conn->lock, &conn->conn, sizeof(conn->conn));
	lock_protect(&conn->lock, &conn->conid_list, sizeof(conn->conid_list));
#ifdef HAVE_NGTCP2_CCERR_DEFAULT
	lock_protect(&conn->lock, &conn->ccerr, sizeof(conn->ccerr));
#else
	lock_protect(&conn->lock, &conn->last_error, sizeof(conn->last_error));
#endif
	lock_protect(&conn->lock, &conn->tls_alert, sizeof(conn->tls_alert));
	lock_protect(&conn->lock, &conn->ssl, sizeof(conn->ssl));
	lock_protect(&conn->lock, &conn->close_pkt, sizeof(conn->close_pkt));
	lock_protect(&conn->lock, &conn->close_pkt_len, sizeof(conn->close_pkt_len));
	lock_protect(&conn->lock, &conn->close_ecn, sizeof(conn->close_ecn));
	lock_protect(&conn->lock, &conn->stream_tree, sizeof(conn->stream_tree));
	lock_protect(&conn->lock, &conn->stream_write_first, sizeof(conn->stream_write_first));
	lock_protect(&conn->lock, &conn->stream_write_last, sizeof(conn->stream_write_last));
	lock_protect(&conn->lock, &conn->write_interest, sizeof(conn->write_interest));
	lock_protect(&conn->lock, &conn->on_write_list, sizeof(conn->on_write_list));
	lock_protect(&conn->lock, &conn->write_prev, sizeof(conn->write_prev));
	lock_protect(&conn->lock, &conn->write_next, sizeof(conn->write_next));
	return conn;
}

/** delete stream tree node */
static void
stream_tree_del(rbnode_type* node, void* arg)
{
	struct doq_table* table = (struct doq_table*)arg;
	struct doq_stream* stream;
	if(!node)
		return;
	stream = (struct doq_stream*)node;
	if(stream->in)
		doq_table_quic_size_subtract(table, stream->inlen);
	if(stream->out)
		doq_table_quic_size_subtract(table, stream->outlen);
	doq_table_quic_size_subtract(table, sizeof(*stream));
	doq_stream_delete(stream);
}

void
doq_conn_delete(struct doq_conn* conn, struct doq_table* table)
{
	if(!conn)
		return;
	lock_basic_destroy(&conn->lock);
	lock_rw_wrlock(&conn->table->conid_lock);
	doq_conn_clear_conids(conn);
	lock_rw_unlock(&conn->table->conid_lock);
	ngtcp2_conn_del(conn->conn);
	if(conn->stream_tree.count != 0) {
		traverse_postorder(&conn->stream_tree, stream_tree_del, table);
	}
	free(conn->key.dcid);
	SSL_free(conn->ssl);
	free(conn->close_pkt);
	free(conn);
}

int
doq_conn_cmp(const void* key1, const void* key2)
{
	struct doq_conn* c = (struct doq_conn*)key1;
	struct doq_conn* d = (struct doq_conn*)key2;
	int r;
	/* Compared in the order destination address, then
	 * local address, ifindex and then dcid.
	 * So that for a search for findlessorequal for the destination
	 * address will find connections to that address, with different
	 * dcids.
	 * Also a printout in sorted order prints the connections by IP
	 * address of destination, and then a number of them depending on the
	 * dcids. */
	if(c->key.paddr.addrlen != d->key.paddr.addrlen) {
		if(c->key.paddr.addrlen < d->key.paddr.addrlen)
			return -1;
		return 1;
	}
	if((r=memcmp(&c->key.paddr.addr, &d->key.paddr.addr,
		c->key.paddr.addrlen))!=0)
		return r;
	if(c->key.paddr.localaddrlen != d->key.paddr.localaddrlen) {
		if(c->key.paddr.localaddrlen < d->key.paddr.localaddrlen)
			return -1;
		return 1;
	}
	if((r=memcmp(&c->key.paddr.localaddr, &d->key.paddr.localaddr,
		c->key.paddr.localaddrlen))!=0)
		return r;
	if(c->key.paddr.ifindex != d->key.paddr.ifindex) {
		if(c->key.paddr.ifindex < d->key.paddr.ifindex)
			return -1;
		return 1;
	}
	if(c->key.dcidlen != d->key.dcidlen) {
		if(c->key.dcidlen < d->key.dcidlen)
			return -1;
		return 1;
	}
	if((r=memcmp(c->key.dcid, d->key.dcid, c->key.dcidlen))!=0)
		return r;
	return 0;
}

int doq_conid_cmp(const void* key1, const void* key2)
{
	struct doq_conid* c = (struct doq_conid*)key1;
	struct doq_conid* d = (struct doq_conid*)key2;
	if(c->cidlen != d->cidlen) {
		if(c->cidlen < d->cidlen)
			return -1;
		return 1;
	}
	return memcmp(c->cid, d->cid, c->cidlen);
}

int doq_timer_cmp(const void* key1, const void* key2)
{
	struct doq_timer* e = (struct doq_timer*)key1;
	struct doq_timer* f = (struct doq_timer*)key2;
	if(e->time.tv_sec < f->time.tv_sec)
		return -1;
	if(e->time.tv_sec > f->time.tv_sec)
		return 1;
	if(e->time.tv_usec < f->time.tv_usec)
		return -1;
	if(e->time.tv_usec > f->time.tv_usec)
		return 1;
	return 0;
}

int doq_stream_cmp(const void* key1, const void* key2)
{
	struct doq_stream* c = (struct doq_stream*)key1;
	struct doq_stream* d = (struct doq_stream*)key2;
	if(c->stream_id != d->stream_id) {
		if(c->stream_id < d->stream_id)
			return -1;
		return 1;
	}
	return 0;
}

/** doq store a local address in repinfo */
static void
doq_repinfo_store_localaddr(struct comm_reply* repinfo,
	struct doq_addr_storage* localaddr, socklen_t localaddrlen)
{
	/* use the pktinfo that we have for ancillary udp data otherwise,
	 * this saves space for a sockaddr */
	memset(&repinfo->pktinfo, 0, sizeof(repinfo->pktinfo));
	if(addr_is_ip6((void*)localaddr, localaddrlen)) {
#ifdef IPV6_PKTINFO
		struct sockaddr_in6* sa6 = (struct sockaddr_in6*)localaddr;
		memmove(&repinfo->pktinfo.v6info.ipi6_addr,
			&sa6->sin6_addr, sizeof(struct in6_addr));
		repinfo->doq_srcport = sa6->sin6_port;
#endif
		repinfo->srctype = 6;
	} else {
#ifdef IP_PKTINFO
		struct sockaddr_in* sa = (struct sockaddr_in*)localaddr;
		memmove(&repinfo->pktinfo.v4info.ipi_addr,
			&sa->sin_addr, sizeof(struct in_addr));
		repinfo->doq_srcport = sa->sin_port;
#elif defined(IP_RECVDSTADDR)
		struct sockaddr_in* sa = (struct sockaddr_in*)localaddr;
		memmove(&repinfo->pktinfo.v4addr, &sa->sin_addr,
			sizeof(struct in_addr));
		repinfo->doq_srcport = sa->sin_port;
#endif
		repinfo->srctype = 4;
	}
}

/** doq retrieve localaddr from repinfo */
static void
doq_repinfo_retrieve_localaddr(struct comm_reply* repinfo,
	struct doq_addr_storage* localaddr, socklen_t* localaddrlen)
{
	if(repinfo->srctype == 6) {
#ifdef IPV6_PKTINFO
		struct sockaddr_in6* sa6 = (struct sockaddr_in6*)localaddr;
		*localaddrlen = (socklen_t)sizeof(struct sockaddr_in6);
		memset(sa6, 0, *localaddrlen);
		sa6->sin6_family = AF_INET6;
		memmove(&sa6->sin6_addr, &repinfo->pktinfo.v6info.ipi6_addr,
			*localaddrlen);
		sa6->sin6_port = repinfo->doq_srcport;
#endif
	} else {
#ifdef IP_PKTINFO
		struct sockaddr_in* sa = (struct sockaddr_in*)localaddr;
		*localaddrlen = (socklen_t)sizeof(struct sockaddr_in);
		memset(sa, 0, *localaddrlen);
		sa->sin_family = AF_INET;
		memmove(&sa->sin_addr, &repinfo->pktinfo.v4info.ipi_addr,
			*localaddrlen);
		sa->sin_port = repinfo->doq_srcport;
#elif defined(IP_RECVDSTADDR)
		struct sockaddr_in* sa = (struct sockaddr_in*)localaddr;
		*localaddrlen = (socklen_t)sizeof(struct sockaddr_in);
		memset(sa, 0, *localaddrlen);
		sa->sin_family = AF_INET;
		memmove(&sa->sin_addr, &repinfo->pktinfo.v4addr,
			sizeof(struct in_addr));
		sa->sin_port = repinfo->doq_srcport;
#endif
	}
}

/** doq write a connection key into repinfo, false if it does not fit */
static int
doq_conn_key_store_repinfo(struct doq_conn_key* key,
	struct comm_reply* repinfo)
{
	repinfo->is_proxied = 0;
	repinfo->doq_ifindex = key->paddr.ifindex;
	repinfo->remote_addrlen = key->paddr.addrlen;
	memmove(&repinfo->remote_addr, &key->paddr.addr,
		repinfo->remote_addrlen);
	repinfo->client_addrlen = key->paddr.addrlen;
	memmove(&repinfo->client_addr, &key->paddr.addr,
		repinfo->client_addrlen);
	doq_repinfo_store_localaddr(repinfo, &key->paddr.localaddr,
		key->paddr.localaddrlen);
	if(key->dcidlen > sizeof(repinfo->doq_dcid))
		return 0;
	repinfo->doq_dcidlen = key->dcidlen;
	memmove(repinfo->doq_dcid, key->dcid, key->dcidlen);
	return 1;
}

void
doq_conn_key_from_repinfo(struct doq_conn_key* key, struct comm_reply* repinfo)
{
	key->paddr.ifindex = repinfo->doq_ifindex;
	key->paddr.addrlen = repinfo->remote_addrlen;
	memmove(&key->paddr.addr, &repinfo->remote_addr,
		repinfo->remote_addrlen);
	doq_repinfo_retrieve_localaddr(repinfo, &key->paddr.localaddr,
		&key->paddr.localaddrlen);
	key->dcidlen = repinfo->doq_dcidlen;
	key->dcid = repinfo->doq_dcid;
}

/** doq add a stream to the connection */
static void
doq_conn_add_stream(struct doq_conn* conn, struct doq_stream* stream)
{
	(void)rbtree_insert(&conn->stream_tree, &stream->node);
}

/** doq delete a stream from the connection */
static void
doq_conn_del_stream(struct doq_conn* conn, struct doq_stream* stream)
{
	(void)rbtree_delete(&conn->stream_tree, &stream->node);
}

/** doq create new stream */
static struct doq_stream*
doq_stream_create(int64_t stream_id)
{
	struct doq_stream* stream = calloc(1, sizeof(*stream));
	if(!stream)
		return NULL;
	stream->node.key = stream;
	stream->stream_id = stream_id;
	return stream;
}

void doq_stream_delete(struct doq_stream* stream)
{
	if(!stream)
		return;
	free(stream->in);
	free(stream->out);
	free(stream);
}

struct doq_stream*
doq_stream_find(struct doq_conn* conn, int64_t stream_id)
{
	rbnode_type* node;
	struct doq_stream key;
	key.node.key = &key;
	key.stream_id = stream_id;
	node = rbtree_search(&conn->stream_tree, &key);
	if(node)
		return (struct doq_stream*)node->key;
	return NULL;
}

/** doq put stream on the conn write list */
static void
doq_stream_on_write_list(struct doq_conn* conn, struct doq_stream* stream)
{
	if(stream->on_write_list)
		return;
	stream->write_prev = conn->stream_write_last;
	if(conn->stream_write_last)
		conn->stream_write_last->write_next = stream;
	else
		conn->stream_write_first = stream;
	conn->stream_write_last = stream;
	stream->write_next = NULL;
	stream->on_write_list = 1;
}

/** doq remove stream from the conn write list */
static void
doq_stream_off_write_list(struct doq_conn* conn, struct doq_stream* stream)
{
	if(!stream->on_write_list)
		return;
	if(stream->write_next)
		stream->write_next->write_prev = stream->write_prev;
	else conn->stream_write_last = stream->write_prev;
	if(stream->write_prev)
		stream->write_prev->write_next = stream->write_next;
	else conn->stream_write_first = stream->write_next;
	stream->write_prev = NULL;
	stream->write_next = NULL;
	stream->on_write_list = 0;
}

/** doq stream remove in buffer */
static void
doq_stream_remove_in_buffer(struct doq_stream* stream, struct doq_table* table)
{
	if(stream->in) {
		doq_table_quic_size_subtract(table, stream->inlen);
		free(stream->in);
		stream->in = NULL;
		stream->inlen = 0;
	}
}

/** doq stream remove out buffer */
static void
doq_stream_remove_out_buffer(struct doq_stream* stream,
	struct doq_table* table)
{
	if(stream->out) {
		doq_table_quic_size_subtract(table, stream->outlen);
		free(stream->out);
		stream->out = NULL;
		stream->outlen = 0;
	}
}

int
doq_stream_close(struct doq_conn* conn, struct doq_stream* stream,
	int send_shutdown)
{
	int ret;
	if(stream->is_closed)
		return 1;
	stream->is_closed = 1;
	doq_stream_off_write_list(conn, stream);
	if(send_shutdown) {
		verbose(VERB_ALGO, "doq: shutdown stream_id %d with app_error_code %d",
			(int)stream->stream_id, (int)DOQ_APP_ERROR_CODE);
		ret = ngtcp2_conn_shutdown_stream(conn->conn,
#ifdef HAVE_NGTCP2_CONN_SHUTDOWN_STREAM4
			0,
#endif
			stream->stream_id, DOQ_APP_ERROR_CODE);
		if(ret != 0) {
			log_err("doq ngtcp2_conn_shutdown_stream %d failed: %s",
				(int)stream->stream_id, ngtcp2_strerror(ret));
			return 0;
		}
		doq_conn_write_enable(conn);
	}
	verbose(VERB_ALGO, "doq: conn extend max streams bidi by 1");
	ngtcp2_conn_extend_max_streams_bidi(conn->conn, 1);
	doq_conn_write_enable(conn);
	doq_stream_remove_in_buffer(stream, conn->doq_socket->table);
	doq_stream_remove_out_buffer(stream, conn->doq_socket->table);
	doq_table_quic_size_subtract(conn->doq_socket->table, sizeof(*stream));
	doq_conn_del_stream(conn, stream);
	doq_stream_delete(stream);
	return 1;
}

/** doq stream pick up answer data from buffer */
static int
doq_stream_pickup_answer(struct doq_stream* stream, struct sldns_buffer* buf)
{
	stream->is_answer_available = 1;
	if(stream->out) {
		free(stream->out);
		stream->out = NULL;
		stream->outlen = 0;
	}
	stream->nwrite = 0;
	stream->outlen = sldns_buffer_limit(buf);
	/* For quic the output bytes have to stay allocated and available,
	 * for potential resends, until the remote end has acknowledged them.
	 * This includes the tcplen start uint16_t, in outlen_wire. */
	stream->outlen_wire = htons(stream->outlen);
	stream->out = memdup(sldns_buffer_begin(buf), sldns_buffer_limit(buf));
	if(!stream->out) {
		log_err("doq could not send answer: out of memory");
		return 0;
	}
	return 1;
}

int
doq_stream_send_reply(struct doq_conn* conn, struct doq_stream* stream,
	struct sldns_buffer* buf)
{
	if(verbosity >= VERB_ALGO) {
		char* s = sldns_wire2str_pkt(sldns_buffer_begin(buf),
			sldns_buffer_limit(buf));
		verbose(VERB_ALGO, "doq stream %d response\n%s",
			(int)stream->stream_id, (s?s:"null"));
		free(s);
	}
	if(stream->out)
		doq_table_quic_size_subtract(conn->doq_socket->table,
			stream->outlen);
	if(!doq_stream_pickup_answer(stream, buf))
		return 0;
	doq_table_quic_size_add(conn->doq_socket->table, stream->outlen);
	doq_stream_on_write_list(conn, stream);
	doq_conn_write_enable(conn);
	return 1;
}

/** doq stream data length has completed, allocations can be done. False on
 * allocation failure. */
static int
doq_stream_datalen_complete(struct doq_stream* stream, struct doq_table* table)
{
	if(stream->inlen > 1024*1024) {
		log_err("doq stream in length too large %d",
			(int)stream->inlen);
		return 0;
	}
	stream->in = calloc(1, stream->inlen);
	if(!stream->in) {
		log_err("doq could not read stream, calloc failed: "
			"out of memory");
		return 0;
	}
	doq_table_quic_size_add(table, stream->inlen);
	return 1;
}

/** doq stream data is complete, the input data has been received. */
static int
doq_stream_data_complete(struct doq_conn* conn, struct doq_stream* stream)
{
	struct comm_point* c;
	if(verbosity >= VERB_ALGO) {
		char* s = sldns_wire2str_pkt(stream->in, stream->inlen);
		char a[128];
		addr_to_str((void*)&conn->key.paddr.addr,
			conn->key.paddr.addrlen, a, sizeof(a));
		verbose(VERB_ALGO, "doq %s stream %d incoming query\n%s",
			a, (int)stream->stream_id, (s?s:"null"));
		free(s);
	}
	stream->is_query_complete = 1;
	c = conn->doq_socket->cp;
	if(!stream->in) {
		verbose(VERB_ALGO, "doq_stream_data_complete: no in buffer");
		return 0;
	}
	if(stream->inlen > sldns_buffer_capacity(c->buffer)) {
		verbose(VERB_ALGO, "doq_stream_data_complete: query too long");
		return 0;
	}
	sldns_buffer_clear(c->buffer);
	sldns_buffer_write(c->buffer, stream->in, stream->inlen);
	sldns_buffer_flip(c->buffer);
	c->repinfo.c = c;
	if(!doq_conn_key_store_repinfo(&conn->key, &c->repinfo)) {
		verbose(VERB_ALGO, "doq_stream_data_complete: connection "
			"DCID too long");
		return 0;
	}
	c->repinfo.doq_streamid = stream->stream_id;
	conn->doq_socket->current_conn = conn;
	fptr_ok(fptr_whitelist_comm_point(c->callback));
	if( (*c->callback)(c, c->cb_arg, NETEVENT_NOERROR, &c->repinfo)) {
		conn->doq_socket->current_conn = NULL;
		if(!doq_stream_send_reply(conn, stream, c->buffer)) {
			verbose(VERB_ALGO, "doq: failed to send_reply");
			return 0;
		}
		return 1;
	}
	conn->doq_socket->current_conn = NULL;
	return 1;
}

/** doq receive data for a stream, more bytes of the incoming data */
static int
doq_stream_recv_data(struct doq_stream* stream, const uint8_t* data,
	size_t datalen, int* recv_done, struct doq_table* table)
{
	int got_data = 0;
	/* read the tcplength uint16_t at the start */
	if(stream->nread < 2) {
		uint16_t tcplen = 0;
		size_t todolen = 2 - stream->nread;

		if(stream->nread > 0) {
			/* put in the already read byte if there is one */
			tcplen = stream->inlen;
		}
		if(datalen < todolen)
			todolen = datalen;
		memmove(((uint8_t*)&tcplen)+stream->nread, data, todolen);
		stream->nread += todolen;
		data += todolen;
		datalen -= todolen;
		if(stream->nread == 2) {
			/* the initial length value is completed */
			stream->inlen = ntohs(tcplen);
			if(!doq_stream_datalen_complete(stream, table))
				return 0;
		} else {
			/* store for later */
			stream->inlen = tcplen;
			return 1;
		}
	}
	/* if there are more data bytes */
	if(datalen > 0) {
		size_t to_write = datalen;
		if(stream->nread-2 > stream->inlen) {
			verbose(VERB_ALGO, "doq stream buffer too small");
			return 0;
		}
		if(datalen > stream->inlen - (stream->nread-2))
			to_write = stream->inlen - (stream->nread-2);
		if(to_write > 0) {
			if(!stream->in) {
				verbose(VERB_ALGO, "doq: stream has "
					"no buffer");
				return 0;
			}
			memmove(stream->in+(stream->nread-2), data, to_write);
			stream->nread += to_write;
			data += to_write;
			datalen -= to_write;
			got_data = 1;
		}
	}
	/* Are there extra bytes received after the end? If so, log them. */
	if(datalen > 0) {
		if(verbosity >= VERB_ALGO)
			log_hex("doq stream has extra bytes received after end",
				(void*)data, datalen);
	}
	/* Is the input data complete? */
	if(got_data && stream->nread >= stream->inlen+2) {
		if(!stream->in) {
			verbose(VERB_ALGO, "doq: completed stream has "
				"no buffer");
			return 0;
		}
		*recv_done = 1;
	}
	return 1;
}

/** doq receive FIN for a stream. No more bytes are going to arrive. */
static int
doq_stream_recv_fin(struct doq_conn* conn, struct doq_stream* stream, int
	recv_done)
{
	if(!stream->is_query_complete && !recv_done) {
		verbose(VERB_ALGO, "doq: stream recv FIN, but is "
			"not complete, have %d of %d bytes",
			((int)stream->nread)-2, (int)stream->inlen);
		if(!doq_stream_close(conn, stream, 1))
			return 0;
	}
	return 1;
}

void doq_fill_rand(struct ub_randstate* rnd, uint8_t* buf, size_t len)
{
	size_t i;
	for(i=0; i<len; i++)
		buf[i] = ub_random(rnd)&0xff;
}

/** generate new connection id, checks for duplicates.
 * caller must hold lock on conid tree. */
static int
doq_conn_generate_new_conid(struct doq_conn* conn, uint8_t* data,
	size_t datalen)
{
	int max_try = 100;
	int i;
	for(i=0; i<max_try; i++) {
		doq_fill_rand(conn->doq_socket->rnd, data, datalen);
		if(!doq_conid_find(conn->table, data, datalen)) {
			/* Found an unused connection id. */
			return 1;
		}
	}
	verbose(VERB_ALGO, "doq_conn_generate_new_conid failed: could not "
		"generate random unused connection id value in %d attempts.",
		max_try);
	return 0;
}

/** ngtcp2 rand callback function */
static void
doq_rand_cb(uint8_t* dest, size_t destlen, const ngtcp2_rand_ctx* rand_ctx)
{
	struct ub_randstate* rnd = (struct ub_randstate*)
		rand_ctx->native_handle;
	doq_fill_rand(rnd, dest, destlen);
}

/** ngtcp2 get_new_connection_id callback function */
static int
doq_get_new_connection_id_cb(ngtcp2_conn* ATTR_UNUSED(conn), ngtcp2_cid* cid,
	uint8_t* token, size_t cidlen, void* user_data)
{
	struct doq_conn* doq_conn = (struct doq_conn*)user_data;
	/* Lock the conid tree, so we can check for duplicates while
	 * generating the id, and then insert it, whilst keeping the tree
	 * locked against other modifications, guaranteeing uniqueness. */
	lock_rw_wrlock(&doq_conn->table->conid_lock);
	if(!doq_conn_generate_new_conid(doq_conn, cid->data, cidlen)) {
		lock_rw_unlock(&doq_conn->table->conid_lock);
		return NGTCP2_ERR_CALLBACK_FAILURE;
	}
	cid->datalen = cidlen;
	if(ngtcp2_crypto_generate_stateless_reset_token(token,
		doq_conn->doq_socket->static_secret,
		doq_conn->doq_socket->static_secret_len, cid) != 0) {
		lock_rw_unlock(&doq_conn->table->conid_lock);
		return NGTCP2_ERR_CALLBACK_FAILURE;
	}
	if(!doq_conn_associate_conid(doq_conn, cid->data, cid->datalen)) {
		lock_rw_unlock(&doq_conn->table->conid_lock);
		return NGTCP2_ERR_CALLBACK_FAILURE;
	}
	lock_rw_unlock(&doq_conn->table->conid_lock);
	return 0;
}

/** ngtcp2 remove_connection_id callback function */
static int
doq_remove_connection_id_cb(ngtcp2_conn* ATTR_UNUSED(conn),
	const ngtcp2_cid* cid, void* user_data)
{
	struct doq_conn* doq_conn = (struct doq_conn*)user_data;
	lock_rw_wrlock(&doq_conn->table->conid_lock);
	doq_conn_dissociate_conid(doq_conn, cid->data, cid->datalen);
	lock_rw_unlock(&doq_conn->table->conid_lock);
	return 0;
}

/** doq submit a new token */
static int
doq_submit_new_token(struct doq_conn* conn)
{
	uint8_t token[NGTCP2_CRYPTO_MAX_REGULAR_TOKENLEN];
	ngtcp2_ssize tokenlen;
	int ret;
	const ngtcp2_path* path = ngtcp2_conn_get_path(conn->conn);
	ngtcp2_tstamp ts = doq_get_timestamp_nanosec();

	tokenlen = ngtcp2_crypto_generate_regular_token(token,
		conn->doq_socket->static_secret,
		conn->doq_socket->static_secret_len, path->remote.addr,
		path->remote.addrlen, ts);
	if(tokenlen < 0) {
		log_err("doq ngtcp2_crypto_generate_regular_token failed");
		return 1;
	}

	verbose(VERB_ALGO, "doq submit new token");
	ret = ngtcp2_conn_submit_new_token(conn->conn, token, tokenlen);
	if(ret != 0) {
		log_err("doq ngtcp2_conn_submit_new_token failed: %s",
			ngtcp2_strerror(ret));
		return 0;
	}
	return 1;
}

/** ngtcp2 handshake_completed callback function */
static int
doq_handshake_completed_cb(ngtcp2_conn* ATTR_UNUSED(conn), void* user_data)
{
	struct doq_conn* doq_conn = (struct doq_conn*)user_data;
	verbose(VERB_ALGO, "doq handshake_completed callback");
	verbose(VERB_ALGO, "ngtcp2_conn_get_max_data_left is %d",
		(int)ngtcp2_conn_get_max_data_left(doq_conn->conn));
#ifdef HAVE_NGTCP2_CONN_GET_MAX_LOCAL_STREAMS_UNI
	verbose(VERB_ALGO, "ngtcp2_conn_get_max_local_streams_uni is %d",
		(int)ngtcp2_conn_get_max_local_streams_uni(doq_conn->conn));
#endif
	verbose(VERB_ALGO, "ngtcp2_conn_get_streams_uni_left is %d",
		(int)ngtcp2_conn_get_streams_uni_left(doq_conn->conn));
	verbose(VERB_ALGO, "ngtcp2_conn_get_streams_bidi_left is %d",
		(int)ngtcp2_conn_get_streams_bidi_left(doq_conn->conn));
	verbose(VERB_ALGO, "negotiated cipher name is %s",
		SSL_get_cipher_name(doq_conn->ssl));
	if(verbosity > VERB_ALGO) {
		const unsigned char* alpn = NULL;
		unsigned int alpnlen = 0;
		char alpnstr[128];
		SSL_get0_alpn_selected(doq_conn->ssl, &alpn, &alpnlen);
		if(alpnlen > sizeof(alpnstr)-1)
			alpnlen = sizeof(alpnstr)-1;
		memmove(alpnstr, alpn, alpnlen);
		alpnstr[alpnlen]=0;
		verbose(VERB_ALGO, "negotiated ALPN is '%s'", alpnstr);
	}

	if(!doq_submit_new_token(doq_conn))
		return -1;
	return 0;
}

/** ngtcp2 stream_open callback function */
static int
doq_stream_open_cb(ngtcp2_conn* ATTR_UNUSED(conn), int64_t stream_id,
	void* user_data)
{
	struct doq_conn* doq_conn = (struct doq_conn*)user_data;
	struct doq_stream* stream;
	verbose(VERB_ALGO, "doq new stream %x", (int)stream_id);
	if(doq_stream_find(doq_conn, stream_id)) {
		verbose(VERB_ALGO, "doq: stream with this id already exists");
		return 0;
	}
	if(stream_id != 0 && stream_id != 4 && /* allow one stream on a new connection */
		!doq_table_quic_size_available(doq_conn->doq_socket->table,
		doq_conn->doq_socket->cfg, sizeof(*stream)
		+ 100 /* estimated query in */
		+ 512 /* estimated response out */
		)) {
		int rv;
		verbose(VERB_ALGO, "doq: no mem for new stream");
		rv = ngtcp2_conn_shutdown_stream(doq_conn->conn,
#ifdef HAVE_NGTCP2_CONN_SHUTDOWN_STREAM4
			0,
#endif
			stream_id, NGTCP2_CONNECTION_REFUSED);
		if(rv != 0) {
			log_err("ngtcp2_conn_shutdown_stream failed: %s",
				ngtcp2_strerror(rv));
			return NGTCP2_ERR_CALLBACK_FAILURE;
		}
		return 0;
	}
	stream = doq_stream_create(stream_id);
	if(!stream) {
		log_err("doq: could not doq_stream_create: out of memory");
		return NGTCP2_ERR_CALLBACK_FAILURE;
	}
	doq_table_quic_size_add(doq_conn->doq_socket->table, sizeof(*stream));
	doq_conn_add_stream(doq_conn, stream);
	return 0;
}

/** ngtcp2 recv_stream_data callback function */
static int
doq_recv_stream_data_cb(ngtcp2_conn* ATTR_UNUSED(conn), uint32_t flags,
	int64_t stream_id, uint64_t offset, const uint8_t* data,
	size_t datalen, void* user_data, void* ATTR_UNUSED(stream_user_data))
{
	int recv_done = 0;
	struct doq_conn* doq_conn = (struct doq_conn*)user_data;
	struct doq_stream* stream;
	verbose(VERB_ALGO, "doq recv stream data stream id %d offset %d "
		"datalen %d%s%s", (int)stream_id, (int)offset, (int)datalen,
		((flags&NGTCP2_STREAM_DATA_FLAG_FIN)!=0?" FIN":""),
#ifdef NGTCP2_STREAM_DATA_FLAG_0RTT
		((flags&NGTCP2_STREAM_DATA_FLAG_0RTT)!=0?" 0RTT":"")
#else
		((flags&NGTCP2_STREAM_DATA_FLAG_EARLY)!=0?" EARLY":"")
#endif
		);
	stream = doq_stream_find(doq_conn, stream_id);
	if(!stream) {
		verbose(VERB_ALGO, "doq: received stream data for "
			"unknown stream %d", (int)stream_id);
		return 0;
	}
	if(stream->is_closed) {
		verbose(VERB_ALGO, "doq: stream is closed, ignore recv data");
		return 0;
	}
	if(datalen != 0) {
		if(!doq_stream_recv_data(stream, data, datalen, &recv_done,
			doq_conn->doq_socket->table))
			return NGTCP2_ERR_CALLBACK_FAILURE;
	}
	if((flags&NGTCP2_STREAM_DATA_FLAG_FIN)!=0) {
		if(!doq_stream_recv_fin(doq_conn, stream, recv_done))
			return NGTCP2_ERR_CALLBACK_FAILURE;
	}
	ngtcp2_conn_extend_max_stream_offset(doq_conn->conn, stream_id,
		datalen);
	ngtcp2_conn_extend_max_offset(doq_conn->conn, datalen);
	if(recv_done) {
		if(!doq_stream_data_complete(doq_conn, stream))
			return NGTCP2_ERR_CALLBACK_FAILURE;
	}
	return 0;
}

/** ngtcp2 stream_close callback function */
static int
doq_stream_close_cb(ngtcp2_conn* ATTR_UNUSED(conn), uint32_t flags,
	int64_t stream_id, uint64_t app_error_code, void* user_data,
	void* ATTR_UNUSED(stream_user_data))
{
	struct doq_conn* doq_conn = (struct doq_conn*)user_data;
	struct doq_stream* stream;
	if((flags&NGTCP2_STREAM_CLOSE_FLAG_APP_ERROR_CODE_SET)!=0)
		verbose(VERB_ALGO, "doq stream close for stream id %d %sapp_error_code %d",
		(int)stream_id,
		(((flags&NGTCP2_STREAM_CLOSE_FLAG_APP_ERROR_CODE_SET)!=0)?
		"APP_ERROR_CODE_SET ":""),
		(int)app_error_code);
	else
		verbose(VERB_ALGO, "doq stream close for stream id %d",
			(int)stream_id);

	stream = doq_stream_find(doq_conn, stream_id);
	if(!stream) {
		verbose(VERB_ALGO, "doq: stream close for "
			"unknown stream %d", (int)stream_id);
		return 0;
	}
	if(!doq_stream_close(doq_conn, stream, 0))
		return NGTCP2_ERR_CALLBACK_FAILURE;
	return 0;
}

/** ngtcp2 stream_reset callback function */
static int
doq_stream_reset_cb(ngtcp2_conn* ATTR_UNUSED(conn), int64_t stream_id,
	uint64_t final_size, uint64_t app_error_code, void* user_data,
	void* ATTR_UNUSED(stream_user_data))
{
	struct doq_conn* doq_conn = (struct doq_conn*)user_data;
	struct doq_stream* stream;
	verbose(VERB_ALGO, "doq stream reset for stream id %d final_size %d "
		"app_error_code %d", (int)stream_id, (int)final_size,
		(int)app_error_code);

	stream = doq_stream_find(doq_conn, stream_id);
	if(!stream) {
		verbose(VERB_ALGO, "doq: stream reset for "
			"unknown stream %d", (int)stream_id);
		return 0;
	}
	if(!doq_stream_close(doq_conn, stream, 0))
		return NGTCP2_ERR_CALLBACK_FAILURE;
	return 0;
}

/** ngtcp2 acked_stream_data_offset callback function */
static int
doq_acked_stream_data_offset_cb(ngtcp2_conn* ATTR_UNUSED(conn),
	int64_t stream_id, uint64_t offset, uint64_t datalen, void* user_data,
	void* ATTR_UNUSED(stream_user_data))
{
	struct doq_conn* doq_conn = (struct doq_conn*)user_data;
	struct doq_stream* stream;
	verbose(VERB_ALGO, "doq stream acked data for stream id %d offset %d "
		"datalen %d", (int)stream_id, (int)offset, (int)datalen);

	stream = doq_stream_find(doq_conn, stream_id);
	if(!stream) {
		verbose(VERB_ALGO, "doq: stream acked data for "
			"unknown stream %d", (int)stream_id);
		return 0;
	}
	/* Acked the data from [offset .. offset+datalen). */
	if(stream->is_closed)
		return 0;
	if(offset+datalen >= stream->outlen) {
		doq_stream_remove_in_buffer(stream,
			doq_conn->doq_socket->table);
		doq_stream_remove_out_buffer(stream,
			doq_conn->doq_socket->table);
	}
	return 0;
}

/** ngtc2p log_printf callback function */
static void
doq_log_printf_cb(void* ATTR_UNUSED(user_data), const char* fmt, ...)
{
	char buf[1024];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	verbose(VERB_ALGO, "libngtcp2: %s", buf);
	va_end(ap);
}

#ifndef HAVE_NGTCP2_CRYPTO_QUICTLS_CONFIGURE_SERVER_CONTEXT
/** the doq application tx key callback, false on failure */
static int
doq_application_tx_key_cb(struct doq_conn* conn)
{
	verbose(VERB_ALGO, "doq application tx key cb");
	/* The server does not want to open streams to the client,
	 * the client instead initiates by opening bidi streams. */
	verbose(VERB_ALGO, "doq ngtcp2_conn_get_max_data_left is %d",
		(int)ngtcp2_conn_get_max_data_left(conn->conn));
#ifdef HAVE_NGTCP2_CONN_GET_MAX_LOCAL_STREAMS_UNI
	verbose(VERB_ALGO, "doq ngtcp2_conn_get_max_local_streams_uni is %d",
		(int)ngtcp2_conn_get_max_local_streams_uni(conn->conn));
#endif
	verbose(VERB_ALGO, "doq ngtcp2_conn_get_streams_uni_left is %d",
		(int)ngtcp2_conn_get_streams_uni_left(conn->conn));
	verbose(VERB_ALGO, "doq ngtcp2_conn_get_streams_bidi_left is %d",
		(int)ngtcp2_conn_get_streams_bidi_left(conn->conn));
	return 1;
}

/** quic_method set_encryption_secrets function */
static int
doq_set_encryption_secrets(SSL *ssl, OSSL_ENCRYPTION_LEVEL ossl_level,
	const uint8_t *read_secret, const uint8_t *write_secret,
	size_t secret_len)
{
	struct doq_conn* doq_conn = (struct doq_conn*)SSL_get_app_data(ssl);
#ifdef HAVE_NGTCP2_ENCRYPTION_LEVEL
	ngtcp2_encryption_level
#else
	ngtcp2_crypto_level
#endif
		level =
#ifdef HAVE_NGTCP2_CRYPTO_QUICTLS_FROM_OSSL_ENCRYPTION_LEVEL
		ngtcp2_crypto_quictls_from_ossl_encryption_level(ossl_level);
#else
		ngtcp2_crypto_openssl_from_ossl_encryption_level(ossl_level);
#endif

	if(read_secret) {
		verbose(VERB_ALGO, "doq: ngtcp2_crypto_derive_and_install_rx_key for level %d ossl %d", (int)level, (int)ossl_level);
		if(ngtcp2_crypto_derive_and_install_rx_key(doq_conn->conn,
			NULL, NULL, NULL, level, read_secret, secret_len)
			!= 0) {
			log_err("ngtcp2_crypto_derive_and_install_rx_key "
				"failed");
			return 0;
		}
	}

	if(write_secret) {
		verbose(VERB_ALGO, "doq: ngtcp2_crypto_derive_and_install_tx_key for level %d ossl %d", (int)level, (int)ossl_level);
		if(ngtcp2_crypto_derive_and_install_tx_key(doq_conn->conn,
			NULL, NULL, NULL, level, write_secret, secret_len)
			!= 0) {
			log_err("ngtcp2_crypto_derive_and_install_tx_key "
				"failed");
			return 0;
		}
		if(level == NGTCP2_CRYPTO_LEVEL_APPLICATION) {
			if(!doq_application_tx_key_cb(doq_conn))
				return 0;
		}
	}
	return 1;
}

/** quic_method add_handshake_data function */
static int
doq_add_handshake_data(SSL *ssl, OSSL_ENCRYPTION_LEVEL ossl_level,
	const uint8_t *data, size_t len)
{
	struct doq_conn* doq_conn = (struct doq_conn*)SSL_get_app_data(ssl);
#ifdef HAVE_NGTCP2_ENCRYPTION_LEVEL
	ngtcp2_encryption_level
#else
	ngtcp2_crypto_level
#endif
		level =
#ifdef HAVE_NGTCP2_CRYPTO_QUICTLS_FROM_OSSL_ENCRYPTION_LEVEL
		ngtcp2_crypto_quictls_from_ossl_encryption_level(ossl_level);
#else
		ngtcp2_crypto_openssl_from_ossl_encryption_level(ossl_level);
#endif
	int rv;

	verbose(VERB_ALGO, "doq_add_handshake_data: "
		"ngtcp2_con_submit_crypto_data level %d", (int)level);
	rv = ngtcp2_conn_submit_crypto_data(doq_conn->conn, level, data, len);
	if(rv != 0) {
		log_err("ngtcp2_conn_submit_crypto_data failed: %s",
			ngtcp2_strerror(rv));
		ngtcp2_conn_set_tls_error(doq_conn->conn, rv);
		return 0;
	}
	return 1;
}

/** quic_method flush_flight function */
static int
doq_flush_flight(SSL* ATTR_UNUSED(ssl))
{
	return 1;
}

/** quic_method send_alert function */
static int
doq_send_alert(SSL *ssl, enum ssl_encryption_level_t ATTR_UNUSED(level),
	uint8_t alert)
{
	struct doq_conn* doq_conn = (struct doq_conn*)SSL_get_app_data(ssl);
	doq_conn->tls_alert = alert;
	return 1;
}
#endif /* HAVE_NGTCP2_CRYPTO_QUICTLS_CONFIGURE_SERVER_CONTEXT */

/** ALPN select callback for the doq SSL context */
static int
doq_alpn_select_cb(SSL* ATTR_UNUSED(ssl), const unsigned char** out,
	unsigned char* outlen, const unsigned char* in, unsigned int inlen,
	void* ATTR_UNUSED(arg))
{
	/* select "doq" */
	int ret = SSL_select_next_proto((void*)out, outlen,
		(const unsigned char*)"\x03""doq", 4, in, inlen);
	if(ret == OPENSSL_NPN_NEGOTIATED)
		return SSL_TLSEXT_ERR_OK;
	verbose(VERB_ALGO, "doq alpn_select_cb: ALPN from client does "
		"not have 'doq'");
	return SSL_TLSEXT_ERR_ALERT_FATAL;
}

void* quic_sslctx_create(char* key, char* pem, char* verifypem)
{
#ifdef HAVE_NGTCP2
	char* sid_ctx = "unbound server";
#ifndef HAVE_NGTCP2_CRYPTO_QUICTLS_CONFIGURE_SERVER_CONTEXT
	SSL_QUIC_METHOD* quic_method;
#endif
	SSL_CTX* ctx = SSL_CTX_new(TLS_server_method());
	if(!ctx) {
		log_crypto_err("Could not SSL_CTX_new");
		return NULL;
	}
	if(!key || key[0] == 0) {
		log_err("doq: error, no tls-service-key file specified");
		SSL_CTX_free(ctx);
		return NULL;
	}
	if(!pem || pem[0] == 0) {
		log_err("doq: error, no tls-service-pem file specified");
		SSL_CTX_free(ctx);
		return NULL;
	}
	SSL_CTX_set_options(ctx,
		(SSL_OP_ALL & ~SSL_OP_DONT_INSERT_EMPTY_FRAGMENTS) |
		SSL_OP_SINGLE_ECDH_USE |
		SSL_OP_CIPHER_SERVER_PREFERENCE |
		SSL_OP_NO_ANTI_REPLAY);
	SSL_CTX_set_mode(ctx, SSL_MODE_RELEASE_BUFFERS);
	SSL_CTX_set_min_proto_version(ctx, TLS1_3_VERSION);
	SSL_CTX_set_max_proto_version(ctx, TLS1_3_VERSION);
#ifdef HAVE_SSL_CTX_SET_ALPN_SELECT_CB
	SSL_CTX_set_alpn_select_cb(ctx, doq_alpn_select_cb, NULL);
#endif
	SSL_CTX_set_default_verify_paths(ctx);
	if(!SSL_CTX_use_certificate_chain_file(ctx, pem)) {
		log_err("doq: error for cert file: %s", pem);
		log_crypto_err("doq: error in "
			"SSL_CTX_use_certificate_chain_file");
		SSL_CTX_free(ctx);
		return NULL;
	}
	if(!SSL_CTX_use_PrivateKey_file(ctx, key, SSL_FILETYPE_PEM)) {
		log_err("doq: error for private key file: %s", key);
		log_crypto_err("doq: error in SSL_CTX_use_PrivateKey_file");
		SSL_CTX_free(ctx);
		return NULL;
	}
	if(!SSL_CTX_check_private_key(ctx)) {
		log_err("doq: error for key file: %s", key);
		log_crypto_err("doq: error in SSL_CTX_check_private_key");
		SSL_CTX_free(ctx);
		return NULL;
	}
	SSL_CTX_set_session_id_context(ctx, (void*)sid_ctx, strlen(sid_ctx));
	if(verifypem && verifypem[0]) {
		if(!SSL_CTX_load_verify_locations(ctx, verifypem, NULL)) {
			log_err("doq: error for verify pem file: %s",
				verifypem);
			log_crypto_err("doq: error in "
				"SSL_CTX_load_verify_locations");
			SSL_CTX_free(ctx);
			return NULL;
		}
		SSL_CTX_set_client_CA_list(ctx, SSL_load_client_CA_file(
			verifypem));
		SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER|
			SSL_VERIFY_CLIENT_ONCE|
			SSL_VERIFY_FAIL_IF_NO_PEER_CERT, NULL);
	}

	SSL_CTX_set_max_early_data(ctx, 0xffffffff);
#ifdef HAVE_NGTCP2_CRYPTO_QUICTLS_CONFIGURE_SERVER_CONTEXT
	if(ngtcp2_crypto_quictls_configure_server_context(ctx) != 0) {
		log_err("ngtcp2_crypto_quictls_configure_server_context failed");
		SSL_CTX_free(ctx);
		return NULL;
	}
#else /* HAVE_NGTCP2_CRYPTO_QUICTLS_CONFIGURE_SERVER_CONTEXT */
	/* The quic_method needs to remain valid during the SSL_CTX
	 * lifetime, so we allocate it. It is freed with the
	 * doq_server_socket. */
	quic_method = calloc(1, sizeof(SSL_QUIC_METHOD));
	if(!quic_method) {
		log_err("calloc failed: out of memory");
		SSL_CTX_free(ctx);
		return NULL;
	}
	doq_socket->quic_method = quic_method;
	quic_method->set_encryption_secrets = doq_set_encryption_secrets;
	quic_method->add_handshake_data = doq_add_handshake_data;
	quic_method->flush_flight = doq_flush_flight;
	quic_method->send_alert = doq_send_alert;
	SSL_CTX_set_quic_method(ctx, doq_socket->quic_method);
#endif
	return ctx;
#else /* HAVE_NGTCP2 */
	(void)key; (void)pem; (void)verifypem;
	return NULL;
#endif /* HAVE_NGTCP2 */
}

/** Get the ngtcp2_conn from ssl userdata of type ngtcp2_conn_ref */
static ngtcp2_conn* doq_conn_ref_get_conn(ngtcp2_crypto_conn_ref* conn_ref)
{
	struct doq_conn* conn = (struct doq_conn*)conn_ref->user_data;
	return conn->conn;
}

/** create new SSL session for server connection */
static SSL*
doq_ssl_server_setup(SSL_CTX* ctx, struct doq_conn* conn)
{
	SSL* ssl = SSL_new(ctx);
	if(!ssl) {
		log_crypto_err("doq: SSL_new failed");
		return NULL;
	}
#ifdef HAVE_NGTCP2_CRYPTO_QUICTLS_CONFIGURE_SERVER_CONTEXT
	conn->conn_ref.get_conn = &doq_conn_ref_get_conn;
	conn->conn_ref.user_data = conn;
	SSL_set_app_data(ssl, &conn->conn_ref);
#else
	SSL_set_app_data(ssl, conn);
#endif
	SSL_set_accept_state(ssl);
	SSL_set_quic_early_data_enabled(ssl, 1);
	return ssl;
}

int
doq_conn_setup(struct doq_conn* conn, uint8_t* scid, size_t scidlen,
	uint8_t* ocid, size_t ocidlen, const uint8_t* token, size_t tokenlen)
{
	int rv;
	struct ngtcp2_cid dcid, sv_scid, scid_cid;
	struct ngtcp2_path path;
	struct ngtcp2_callbacks callbacks;
	struct ngtcp2_settings settings;
	struct ngtcp2_transport_params params;
	memset(&dcid, 0, sizeof(dcid));
	memset(&sv_scid, 0, sizeof(sv_scid));
	memset(&scid_cid, 0, sizeof(scid_cid));
	memset(&path, 0, sizeof(path));
	memset(&callbacks, 0, sizeof(callbacks));
	memset(&settings, 0, sizeof(settings));
	memset(&params, 0, sizeof(params));

	ngtcp2_cid_init(&scid_cid, scid, scidlen);
	ngtcp2_cid_init(&dcid, conn->key.dcid, conn->key.dcidlen);

	path.remote.addr = (struct sockaddr*)&conn->key.paddr.addr;
	path.remote.addrlen = conn->key.paddr.addrlen;
	path.local.addr = (struct sockaddr*)&conn->key.paddr.localaddr;
	path.local.addrlen = conn->key.paddr.localaddrlen;

	callbacks.recv_client_initial = ngtcp2_crypto_recv_client_initial_cb;
	callbacks.recv_crypto_data = ngtcp2_crypto_recv_crypto_data_cb;
	callbacks.encrypt = ngtcp2_crypto_encrypt_cb;
	callbacks.decrypt = ngtcp2_crypto_decrypt_cb;
	callbacks.hp_mask = ngtcp2_crypto_hp_mask;
	callbacks.update_key = ngtcp2_crypto_update_key_cb;
	callbacks.delete_crypto_aead_ctx =
		ngtcp2_crypto_delete_crypto_aead_ctx_cb;
	callbacks.delete_crypto_cipher_ctx =
		ngtcp2_crypto_delete_crypto_cipher_ctx_cb;
	callbacks.get_path_challenge_data =
		ngtcp2_crypto_get_path_challenge_data_cb;
	callbacks.version_negotiation = ngtcp2_crypto_version_negotiation_cb;
	callbacks.rand = doq_rand_cb;
	callbacks.get_new_connection_id = doq_get_new_connection_id_cb;
	callbacks.remove_connection_id = doq_remove_connection_id_cb;
	callbacks.handshake_completed = doq_handshake_completed_cb;
	callbacks.stream_open = doq_stream_open_cb;
	callbacks.stream_close = doq_stream_close_cb;
	callbacks.stream_reset = doq_stream_reset_cb;
	callbacks.acked_stream_data_offset = doq_acked_stream_data_offset_cb;
	callbacks.recv_stream_data = doq_recv_stream_data_cb;

	ngtcp2_settings_default(&settings);
	if(verbosity >= VERB_ALGO) {
		settings.log_printf = doq_log_printf_cb;
	}
	settings.rand_ctx.native_handle = conn->doq_socket->rnd;
	settings.initial_ts = doq_get_timestamp_nanosec();
	settings.max_stream_window = 6*1024*1024;
	settings.max_window = 6*1024*1024;
#ifdef HAVE_STRUCT_NGTCP2_SETTINGS_TOKENLEN
	settings.token = (void*)token;
	settings.tokenlen = tokenlen;
#else
	settings.token.base = (void*)token;
	settings.token.len = tokenlen;
#endif

	ngtcp2_transport_params_default(&params);
	params.max_idle_timeout = conn->doq_socket->idle_timeout;
	params.active_connection_id_limit = 7;
	params.initial_max_stream_data_bidi_local = 256*1024;
	params.initial_max_stream_data_bidi_remote = 256*1024;
	params.initial_max_data = 1024*1024;
	/* DoQ uses bidi streams, so we allow 0 uni streams. */
	params.initial_max_streams_uni = 0;
	/* Initial max on number of bidi streams the remote end can open.
	 * That is the number of queries it can make, at first. */
	params.initial_max_streams_bidi = 10;
	if(ocid) {
		ngtcp2_cid_init(&params.original_dcid, ocid, ocidlen);
		ngtcp2_cid_init(&params.retry_scid, conn->key.dcid,
			conn->key.dcidlen);
		params.retry_scid_present = 1;
	} else {
		ngtcp2_cid_init(&params.original_dcid, conn->key.dcid,
			conn->key.dcidlen);
	}
#ifdef HAVE_STRUCT_NGTCP2_TRANSPORT_PARAMS_ORIGINAL_DCID_PRESENT
	params.original_dcid_present = 1;
#endif
	doq_fill_rand(conn->doq_socket->rnd, params.stateless_reset_token,
		sizeof(params.stateless_reset_token));
	sv_scid.datalen = conn->doq_socket->sv_scidlen;
	lock_rw_wrlock(&conn->table->conid_lock);
	if(!doq_conn_generate_new_conid(conn, sv_scid.data, sv_scid.datalen)) {
		lock_rw_unlock(&conn->table->conid_lock);
		return 0;
	}

	rv = ngtcp2_conn_server_new(&conn->conn, &scid_cid, &sv_scid, &path,
		conn->version, &callbacks, &settings, &params, NULL, conn);
	if(rv != 0) {
		lock_rw_unlock(&conn->table->conid_lock);
		log_err("ngtcp2_conn_server_new failed: %s",
			ngtcp2_strerror(rv));
		return 0;
	}
	if(!doq_conn_setup_conids(conn)) {
		lock_rw_unlock(&conn->table->conid_lock);
		log_err("doq_conn_setup_conids failed: out of memory");
		return 0;
	}
	lock_rw_unlock(&conn->table->conid_lock);
	conn->ssl = doq_ssl_server_setup((SSL_CTX*)conn->doq_socket->ctx,
		conn);
	if(!conn->ssl) {
		log_err("doq_ssl_server_setup failed");
		return 0;
	}
	ngtcp2_conn_set_tls_native_handle(conn->conn, conn->ssl);
	doq_conn_write_enable(conn);
	return 1;
}

struct doq_conid*
doq_conid_find(struct doq_table* table, const uint8_t* data, size_t datalen)
{
	struct rbnode_type* node;
	struct doq_conid key;
	key.node.key = &key;
	key.cid = (void*)data;
	key.cidlen = datalen;
	node = rbtree_search(table->conid_tree, &key);
	if(node)
		return (struct doq_conid*)node->key;
	return NULL;
}

/** insert conid in the conid list */
static void
doq_conid_list_insert(struct doq_conn* conn, struct doq_conid* conid)
{
	conid->prev = NULL;
	conid->next = conn->conid_list;
	if(conn->conid_list)
		conn->conid_list->prev = conid;
	conn->conid_list = conid;
}

/** remove conid from the conid list */
static void
doq_conid_list_remove(struct doq_conn* conn, struct doq_conid* conid)
{
	if(conid->prev)
		conid->prev->next = conid->next;
	else	conn->conid_list = conid->next;
	if(conid->next)
		conid->next->prev = conid->prev;
}

/** create a doq_conid */
static struct doq_conid*
doq_conid_create(uint8_t* data, size_t datalen, struct doq_conn_key* key)
{
	struct doq_conid* conid;
	conid = calloc(1, sizeof(*conid));
	if(!conid)
		return NULL;
	conid->cid = memdup(data, datalen);
	if(!conid->cid) {
		free(conid);
		return NULL;
	}
	conid->cidlen = datalen;
	conid->node.key = conid;
	conid->key = *key;
	conid->key.dcid = memdup(key->dcid, key->dcidlen);
	if(!conid->key.dcid) {
		free(conid->cid);
		free(conid);
		return NULL;
	}
	return conid;
}

void
doq_conid_delete(struct doq_conid* conid)
{
	if(!conid)
		return;
	free(conid->key.dcid);
	free(conid->cid);
	free(conid);
}

/** return true if the conid is for the conn. */
static int
conid_is_for_conn(struct doq_conn* conn, struct doq_conid* conid)
{
	if(conid->key.dcidlen == conn->key.dcidlen &&
		memcmp(conid->key.dcid, conn->key.dcid, conid->key.dcidlen)==0
		&& conid->key.paddr.addrlen == conn->key.paddr.addrlen &&
		memcmp(&conid->key.paddr.addr, &conn->key.paddr.addr,
			conid->key.paddr.addrlen) == 0 &&
		conid->key.paddr.localaddrlen == conn->key.paddr.localaddrlen &&
		memcmp(&conid->key.paddr.localaddr, &conn->key.paddr.localaddr,
			conid->key.paddr.localaddrlen) == 0 &&
		conid->key.paddr.ifindex == conn->key.paddr.ifindex)
		return 1;
	return 0;
}

int
doq_conn_associate_conid(struct doq_conn* conn, uint8_t* data, size_t datalen)
{
	struct doq_conid* conid;
	conid = doq_conid_find(conn->table, data, datalen);
	if(conid && !conid_is_for_conn(conn, conid)) {
		verbose(VERB_ALGO, "doq connection id already exists for "
			"another doq_conn. Ignoring second connection id.");
		/* Already exists to another conn, ignore it.
		 * This works, in that the conid is listed in the doq_conn
		 * conid_list element, and removed from there. So our conid
		 * tree and list are fine, when created and removed.
		 * The tree now does not have the lookup element pointing
		 * to this connection. */
		return 1;
	}
	if(conid)
		return 1; /* already inserted */
	conid = doq_conid_create(data, datalen, &conn->key);
	if(!conid)
		return 0;
	doq_conid_list_insert(conn, conid);
	(void)rbtree_insert(conn->table->conid_tree, &conid->node);
	return 1;
}

void
doq_conn_dissociate_conid(struct doq_conn* conn, const uint8_t* data,
	size_t datalen)
{
	struct doq_conid* conid;
	conid = doq_conid_find(conn->table, data, datalen);
	if(conid && !conid_is_for_conn(conn, conid))
		return;
	if(conid) {
		(void)rbtree_delete(conn->table->conid_tree,
			conid->node.key);
		doq_conid_list_remove(conn, conid);
		doq_conid_delete(conid);
	}
}

/** associate the scid array and also the dcid.
 * caller must hold the locks on conn and doq_table.conid_lock. */
static int
doq_conn_setup_id_array_and_dcid(struct doq_conn* conn,
	struct ngtcp2_cid* scids, size_t num_scid)
{
	size_t i;
	for(i=0; i<num_scid; i++) {
		if(!doq_conn_associate_conid(conn, scids[i].data,
			scids[i].datalen))
			return 0;
	}
	if(!doq_conn_associate_conid(conn, conn->key.dcid, conn->key.dcidlen))
		return 0;
	return 1;
}

int
doq_conn_setup_conids(struct doq_conn* conn)
{
	size_t num_scid =
#ifndef HAVE_NGTCP2_CONN_GET_NUM_SCID
		ngtcp2_conn_get_scid(conn->conn, NULL);
#else
		ngtcp2_conn_get_num_scid(conn->conn);
#endif
	if(num_scid <= 4) {
		struct ngtcp2_cid ids[4];
		/* Usually there are not that many scids when just accepted,
		 * like only 2. */
		ngtcp2_conn_get_scid(conn->conn, ids);
		return doq_conn_setup_id_array_and_dcid(conn, ids, num_scid);
	} else {
		struct ngtcp2_cid *scids = calloc(num_scid,
			sizeof(struct ngtcp2_cid));
		if(!scids)
			return 0;
		ngtcp2_conn_get_scid(conn->conn, scids);
		if(!doq_conn_setup_id_array_and_dcid(conn, scids, num_scid)) {
			free(scids);
			return 0;
		}
		free(scids);
	}
	return 1;
}

void
doq_conn_clear_conids(struct doq_conn* conn)
{
	struct doq_conid* p, *next;
	if(!conn)
		return;
	p = conn->conid_list;
	while(p) {
		next = p->next;
		(void)rbtree_delete(conn->table->conid_tree, p->node.key);
		doq_conid_delete(p);
		p = next;
	}
	conn->conid_list = NULL;
}

ngtcp2_tstamp doq_get_timestamp_nanosec(void)
{
#ifdef CLOCK_REALTIME
	struct timespec tp;
	memset(&tp, 0, sizeof(tp));
	/* Get a nanosecond time, that can be compared with the event base. */
	if(clock_gettime(CLOCK_REALTIME, &tp) == -1) {
		log_err("clock_gettime failed: %s", strerror(errno));
	}
	return ((uint64_t)tp.tv_sec)*((uint64_t)1000000000) +
		((uint64_t)tp.tv_nsec);
#else
	struct timeval tv;
	if(gettimeofday(&tv, NULL) < 0) {
		log_err("gettimeofday failed: %s", strerror(errno));
	}
	return ((uint64_t)tv.tv_sec)*((uint64_t)1000000000) +
		((uint64_t)tv.tv_usec)*((uint64_t)1000);
#endif /* CLOCK_REALTIME */
}

/** doq start the closing period for the connection. */
static int
doq_conn_start_closing_period(struct comm_point* c, struct doq_conn* conn)
{
	struct ngtcp2_path_storage ps;
	struct ngtcp2_pkt_info pi;
	ngtcp2_ssize ret;
	if(!conn)
		return 1;
	if(
#ifdef HAVE_NGTCP2_CONN_IN_CLOSING_PERIOD
		ngtcp2_conn_in_closing_period(conn->conn)
#else
		ngtcp2_conn_is_in_closing_period(conn->conn)
#endif
		)
		return 1;
	if(
#ifdef HAVE_NGTCP2_CONN_IN_DRAINING_PERIOD
		ngtcp2_conn_in_draining_period(conn->conn)
#else
		ngtcp2_conn_is_in_draining_period(conn->conn)
#endif
		) {
		doq_conn_write_disable(conn);
		return 1;
	}
	ngtcp2_path_storage_zero(&ps);
	sldns_buffer_clear(c->doq_socket->pkt_buf);
	/* the call to ngtcp2_conn_write_connection_close causes the
	 * conn to be closed. It is now in the closing period. */
	ret = ngtcp2_conn_write_connection_close(conn->conn, &ps.path,
		&pi, sldns_buffer_begin(c->doq_socket->pkt_buf),
		sldns_buffer_remaining(c->doq_socket->pkt_buf),
#ifdef HAVE_NGTCP2_CCERR_DEFAULT
		&conn->ccerr
#else
		&conn->last_error
#endif
		, doq_get_timestamp_nanosec());
	if(ret < 0) {
		log_err("doq ngtcp2_conn_write_connection_close failed: %s",
			ngtcp2_strerror(ret));
		return 0;
	}
	if(ret == 0) {
		return 0;
	}
	sldns_buffer_set_position(c->doq_socket->pkt_buf, ret);
	sldns_buffer_flip(c->doq_socket->pkt_buf);

	/* The close packet is allocated, because it may have to be repeated.
	 * When incoming packets have this connection dcid. */
	conn->close_pkt = memdup(sldns_buffer_begin(c->doq_socket->pkt_buf),
		sldns_buffer_limit(c->doq_socket->pkt_buf));
	if(!conn->close_pkt) {
		log_err("doq: could not allocate close packet: out of memory");
		return 0;
	}
	conn->close_pkt_len = sldns_buffer_limit(c->doq_socket->pkt_buf);
	conn->close_ecn = pi.ecn;
	return 1;
}

/** doq send the close packet for the connection, perhaps again. */
int
doq_conn_send_close(struct comm_point* c, struct doq_conn* conn)
{
	if(!conn)
		return 0;
	if(!conn->close_pkt)
		return 0;
	if(conn->close_pkt_len > sldns_buffer_capacity(c->doq_socket->pkt_buf))
		return 0;
	sldns_buffer_clear(c->doq_socket->pkt_buf);
	sldns_buffer_write(c->doq_socket->pkt_buf, conn->close_pkt, conn->close_pkt_len);
	sldns_buffer_flip(c->doq_socket->pkt_buf);
	verbose(VERB_ALGO, "doq send connection close");
	doq_send_pkt(c, &conn->key.paddr, conn->close_ecn);
	doq_conn_write_disable(conn);
	return 1;
}

/** doq close the connection on error. If it returns a failure, it
 * does not wait to send a close, and the connection can be dropped. */
static int
doq_conn_close_error(struct comm_point* c, struct doq_conn* conn)
{
#ifdef HAVE_NGTCP2_CCERR_DEFAULT
	if(conn->ccerr.type == NGTCP2_CCERR_TYPE_IDLE_CLOSE)
		return 0;
#else
	if(conn->last_error.type ==
		NGTCP2_CONNECTION_CLOSE_ERROR_CODE_TYPE_TRANSPORT_IDLE_CLOSE)
		return 0;
#endif
	if(!doq_conn_start_closing_period(c, conn))
		return 0;
	if(
#ifdef HAVE_NGTCP2_CONN_IN_DRAINING_PERIOD
		ngtcp2_conn_in_draining_period(conn->conn)
#else
		ngtcp2_conn_is_in_draining_period(conn->conn)
#endif
		) {
		doq_conn_write_disable(conn);
		return 1;
	}
	doq_conn_write_enable(conn);
	if(!doq_conn_send_close(c, conn))
		return 0;
	return 1;
}

int
doq_conn_recv(struct comm_point* c, struct doq_pkt_addr* paddr,
	struct doq_conn* conn, struct ngtcp2_pkt_info* pi, int* err_retry,
	int* err_drop)
{
	int ret;
	ngtcp2_tstamp ts;
	struct ngtcp2_path path;
	memset(&path, 0, sizeof(path));
	path.remote.addr = (struct sockaddr*)&paddr->addr;
	path.remote.addrlen = paddr->addrlen;
	path.local.addr = (struct sockaddr*)&paddr->localaddr;
	path.local.addrlen = paddr->localaddrlen;
	ts = doq_get_timestamp_nanosec();

	ret = ngtcp2_conn_read_pkt(conn->conn, &path, pi,
		sldns_buffer_begin(c->doq_socket->pkt_buf),
		sldns_buffer_limit(c->doq_socket->pkt_buf), ts);
	if(ret != 0) {
		if(err_retry)
			*err_retry = 0;
		if(err_drop)
			*err_drop = 0;
		if(ret == NGTCP2_ERR_DRAINING) {
			verbose(VERB_ALGO, "ngtcp2_conn_read_pkt returned %s",
				ngtcp2_strerror(ret));
			doq_conn_write_disable(conn);
			return 0;
		} else if(ret == NGTCP2_ERR_DROP_CONN) {
			verbose(VERB_ALGO, "ngtcp2_conn_read_pkt returned %s",
				ngtcp2_strerror(ret));
			if(err_drop)
				*err_drop = 1;
			return 0;
		} else if(ret == NGTCP2_ERR_RETRY) {
			verbose(VERB_ALGO, "ngtcp2_conn_read_pkt returned %s",
				ngtcp2_strerror(ret));
			if(err_retry)
				*err_retry = 1;
			if(err_drop)
				*err_drop = 1;
			return 0;
		} else if(ret == NGTCP2_ERR_CRYPTO) {
			if(
#ifdef HAVE_NGTCP2_CCERR_DEFAULT
				!conn->ccerr.error_code
#else
				!conn->last_error.error_code
#endif
				) {
				/* in picotls the tls alert may need to be
				 * copied, but this is with openssl. And there
				 * is conn->tls_alert. */
#ifdef HAVE_NGTCP2_CCERR_DEFAULT
				ngtcp2_ccerr_set_tls_alert(&conn->ccerr,
					conn->tls_alert, NULL, 0);
#else
				ngtcp2_connection_close_error_set_transport_error_tls_alert(
					&conn->last_error, conn->tls_alert,
					NULL, 0);
#endif
			}
		} else {
			if(
#ifdef HAVE_NGTCP2_CCERR_DEFAULT
				!conn->ccerr.error_code
#else
				!conn->last_error.error_code
#endif
				) {
#ifdef HAVE_NGTCP2_CCERR_DEFAULT
				ngtcp2_ccerr_set_liberr(&conn->ccerr, ret,
					NULL, 0);
#else
				ngtcp2_connection_close_error_set_transport_error_liberr(
					&conn->last_error, ret, NULL, 0);
#endif
			}
		}
		log_err("ngtcp2_conn_read_pkt failed: %s",
			ngtcp2_strerror(ret));
		if(!doq_conn_close_error(c, conn)) {
			if(err_drop)
				*err_drop = 1;
		}
		return 0;
	}
	doq_conn_write_enable(conn);
	return 1;
}

/** doq stream write is done */
static void
doq_stream_write_is_done(struct doq_conn* conn, struct doq_stream* stream)
{
	/* Cannot deallocate, the buffer may be needed for resends. */
	doq_stream_off_write_list(conn, stream);
}

int
doq_conn_write_streams(struct comm_point* c, struct doq_conn* conn,
	int* err_drop)
{
	struct doq_stream* stream = conn->stream_write_first;
	ngtcp2_path_storage ps;
	ngtcp2_tstamp ts = doq_get_timestamp_nanosec();
	size_t num_packets = 0, max_packets = 65535;
	ngtcp2_path_storage_zero(&ps);

	for(;;) {
		int64_t stream_id;
		uint32_t flags = 0;
		ngtcp2_pkt_info pi;
		ngtcp2_vec datav[2];
		size_t datav_count = 0;
		ngtcp2_ssize ret, ndatalen = 0;
		int fin;

		if(stream) {
			/* data to send */
			verbose(VERB_ALGO, "doq: doq_conn write stream %d",
				(int)stream->stream_id);
			stream_id = stream->stream_id;
			fin = 1;
			if(stream->nwrite < 2) {
				datav[0].base = ((uint8_t*)&stream->
					outlen_wire) + stream->nwrite;
				datav[0].len = 2 - stream->nwrite;
				datav[1].base = stream->out;
				datav[1].len = stream->outlen;
				datav_count = 2;
			} else {
				datav[0].base = stream->out +
					(stream->nwrite-2);
				datav[0].len = stream->outlen -
					(stream->nwrite-2);
				datav_count = 1;
			}
		} else {
			/* no data to send */
			verbose(VERB_ALGO, "doq: doq_conn write stream -1");
			stream_id = -1;
			fin = 0;
			datav[0].base = NULL;
			datav[0].len = 0;
			datav_count = 1;
		}

		/* if more streams, set it to write more */
		if(stream && stream->write_next)
			flags |= NGTCP2_WRITE_STREAM_FLAG_MORE;
		if(fin)
			flags |= NGTCP2_WRITE_STREAM_FLAG_FIN;

		sldns_buffer_clear(c->doq_socket->pkt_buf);
		ret = ngtcp2_conn_writev_stream(conn->conn, &ps.path, &pi,
			sldns_buffer_begin(c->doq_socket->pkt_buf),
			sldns_buffer_remaining(c->doq_socket->pkt_buf),
			&ndatalen, flags, stream_id, datav, datav_count, ts);
		if(ret < 0) {
			if(ret == NGTCP2_ERR_WRITE_MORE) {
				verbose(VERB_ALGO, "doq: write more, ndatalen %d", (int)ndatalen);
				if(stream) {
					if(ndatalen >= 0)
						stream->nwrite += ndatalen;
					if(stream->nwrite >= stream->outlen+2)
						doq_stream_write_is_done(
							conn, stream);
					stream = stream->write_next;
				}
				continue;
			} else if(ret == NGTCP2_ERR_STREAM_DATA_BLOCKED) {
				verbose(VERB_ALGO, "doq: ngtcp2_conn_writev_stream returned NGTCP2_ERR_STREAM_DATA_BLOCKED");
#ifdef HAVE_NGTCP2_CCERR_DEFAULT
				ngtcp2_ccerr_set_application_error(
					&conn->ccerr, -1, NULL, 0);
#else
				ngtcp2_connection_close_error_set_application_error(&conn->last_error, -1, NULL, 0);
#endif
				if(err_drop)
					*err_drop = 0;
				if(!doq_conn_close_error(c, conn)) {
					if(err_drop)
						*err_drop = 1;
				}
				return 0;
			} else if(ret == NGTCP2_ERR_STREAM_SHUT_WR) {
				verbose(VERB_ALGO, "doq: ngtcp2_conn_writev_stream returned NGTCP2_ERR_STREAM_SHUT_WR");
#ifdef HAVE_NGTCP2_CCERR_DEFAULT
				ngtcp2_ccerr_set_application_error(
					&conn->ccerr, -1, NULL, 0);
#else
				ngtcp2_connection_close_error_set_application_error(&conn->last_error, -1, NULL, 0);
#endif
				if(err_drop)
					*err_drop = 0;
				if(!doq_conn_close_error(c, conn)) {
					if(err_drop)
						*err_drop = 1;
				}
				return 0;
			}

			log_err("doq: ngtcp2_conn_writev_stream failed: %s",
				ngtcp2_strerror(ret));
#ifdef HAVE_NGTCP2_CCERR_DEFAULT
			ngtcp2_ccerr_set_liberr(&conn->ccerr, ret, NULL, 0);
#else
			ngtcp2_connection_close_error_set_transport_error_liberr(
				&conn->last_error, ret, NULL, 0);
#endif
			if(err_drop)
				*err_drop = 0;
			if(!doq_conn_close_error(c, conn)) {
				if(err_drop)
					*err_drop = 1;
			}
			return 0;
		}
		verbose(VERB_ALGO, "doq: writev_stream pkt size %d ndatawritten %d",
			(int)ret, (int)ndatalen);

		if(ndatalen >= 0 && stream) {
			stream->nwrite += ndatalen;
			if(stream->nwrite >= stream->outlen+2)
				doq_stream_write_is_done(conn, stream);
		}
		if(ret == 0) {
			/* congestion limited */
			doq_conn_write_disable(conn);
			ngtcp2_conn_update_pkt_tx_time(conn->conn, ts);
			return 1;
		}
		sldns_buffer_set_position(c->doq_socket->pkt_buf, ret);
		sldns_buffer_flip(c->doq_socket->pkt_buf);
		doq_send_pkt(c, &conn->key.paddr, pi.ecn);

		if(c->doq_socket->have_blocked_pkt)
			break;
		if(++num_packets == max_packets)
			break;
		if(stream)
			stream = stream->write_next;
	}
	ngtcp2_conn_update_pkt_tx_time(conn->conn, ts);
	return 1;
}

void
doq_conn_write_enable(struct doq_conn* conn)
{
	conn->write_interest = 1;
}

void
doq_conn_write_disable(struct doq_conn* conn)
{
	conn->write_interest = 0;
}

/** doq append the connection to the write list */
static void
doq_conn_write_list_append(struct doq_table* table, struct doq_conn* conn)
{
	if(conn->on_write_list)
		return;
	conn->write_prev = table->write_list_last;
	if(table->write_list_last)
		table->write_list_last->write_next = conn;
	else table->write_list_first = conn;
	conn->write_next = NULL;
	table->write_list_last = conn;
	conn->on_write_list = 1;
}

void
doq_conn_write_list_remove(struct doq_table* table, struct doq_conn* conn)
{
	if(!conn->on_write_list)
		return;
	if(conn->write_next)
		conn->write_next->write_prev = conn->write_prev;
	else table->write_list_last = conn->write_prev;
	if(conn->write_prev)
		conn->write_prev->write_next = conn->write_next;
	else table->write_list_first = conn->write_next;
	conn->write_prev = NULL;
	conn->write_next = NULL;
	conn->on_write_list = 0;
}

void
doq_conn_set_write_list(struct doq_table* table, struct doq_conn* conn)
{
	if(conn->write_interest && conn->on_write_list)
		return;
	if(!conn->write_interest && !conn->on_write_list)
		return;
	if(conn->write_interest)
		doq_conn_write_list_append(table, conn);
	else doq_conn_write_list_remove(table, conn);
}

struct doq_conn*
doq_table_pop_first(struct doq_table* table)
{
	struct doq_conn* conn = table->write_list_first;
	if(!conn)
		return NULL;
	lock_basic_lock(&conn->lock);
	table->write_list_first = conn->write_next;
	if(conn->write_next)
		conn->write_next->write_prev = NULL;
	else table->write_list_last = NULL;
	conn->write_next = NULL;
	conn->write_prev = NULL;
	conn->on_write_list = 0;
	return conn;
}

int
doq_conn_check_timer(struct doq_conn* conn, struct timeval* tv)
{
	ngtcp2_tstamp expiry = ngtcp2_conn_get_expiry(conn->conn);
	ngtcp2_tstamp now = doq_get_timestamp_nanosec();
	ngtcp2_tstamp t;

	if(expiry <= now) {
		/* The timer has already expired, add with zero timeout.
		 * This should call the callback straight away. Calling it
		 * from the event callbacks is cleaner than calling it here,
		 * because then it is always called with the same locks and
		 * so on. This routine only has the conn.lock. */
		t = now;
	} else {
		t = expiry;
	}

	/* convert to timeval */
	memset(tv, 0, sizeof(*tv));
	tv->tv_sec = t / NGTCP2_SECONDS;
	tv->tv_usec = (t / NGTCP2_MICROSECONDS)%1000000;

	/* If we already have a timer, is it the right value? */
	if(conn->timer.timer_in_tree || conn->timer.timer_in_list) {
		if(conn->timer.time.tv_sec == tv->tv_sec &&
			conn->timer.time.tv_usec == tv->tv_usec)
			return 0;
	}
	return 1;
}

/* doq print connection log */
static void
doq_conn_log_line(struct doq_conn* conn, char* s)
{
	char remotestr[256], localstr[256];
	addr_to_str((void*)&conn->key.paddr.addr, conn->key.paddr.addrlen,
		remotestr, sizeof(remotestr));
	addr_to_str((void*)&conn->key.paddr.localaddr,
		conn->key.paddr.localaddrlen, localstr, sizeof(localstr));
	log_info("doq conn %s %s %s", remotestr, localstr, s);
}

int
doq_conn_handle_timeout(struct doq_conn* conn)
{
	ngtcp2_tstamp now = doq_get_timestamp_nanosec();
	int rv;

	if(verbosity >= VERB_ALGO)
		doq_conn_log_line(conn, "timeout");

	rv = ngtcp2_conn_handle_expiry(conn->conn, now);
	if(rv != 0) {
		verbose(VERB_ALGO, "ngtcp2_conn_handle_expiry failed: %s",
			ngtcp2_strerror(rv));
#ifdef HAVE_NGTCP2_CCERR_DEFAULT
		ngtcp2_ccerr_set_liberr(&conn->ccerr, rv, NULL, 0);
#else
		ngtcp2_connection_close_error_set_transport_error_liberr(
			&conn->last_error, rv, NULL, 0);
#endif
		if(!doq_conn_close_error(conn->doq_socket->cp, conn)) {
			/* failed, return for deletion */
			return 0;
		}
		return 1;
	}
	doq_conn_write_enable(conn);
	if(!doq_conn_write_streams(conn->doq_socket->cp, conn, NULL)) {
		/* failed, return for deletion. */
		return 0;
	}
	return 1;
}

void
doq_table_quic_size_add(struct doq_table* table, size_t add)
{
	lock_basic_lock(&table->size_lock);
	table->current_size += add;
	lock_basic_unlock(&table->size_lock);
}

void
doq_table_quic_size_subtract(struct doq_table* table, size_t subtract)
{
	lock_basic_lock(&table->size_lock);
	if(table->current_size < subtract)
		table->current_size = 0;
	else	table->current_size -= subtract;
	lock_basic_unlock(&table->size_lock);
}

int
doq_table_quic_size_available(struct doq_table* table,
	struct config_file* cfg, size_t mem)
{
	size_t cur;
	lock_basic_lock(&table->size_lock);
	cur = table->current_size;
	lock_basic_unlock(&table->size_lock);

	if(cur + mem > cfg->quic_size)
		return 0;
	return 1;
}

size_t doq_table_quic_size_get(struct doq_table* table)
{
	size_t sz;
	if(!table)
		return 0;
	lock_basic_lock(&table->size_lock);
	sz = table->current_size;
	lock_basic_unlock(&table->size_lock);
	return sz;
}
#endif /* HAVE_NGTCP2 */
