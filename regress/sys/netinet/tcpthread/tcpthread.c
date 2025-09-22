/*	$OpenBSD: tcpthread.c,v 1.5 2025/05/24 03:44:06 bluhm Exp $	*/

/*
 * Copyright (c) 2025 Alexander Bluhm <bluhm@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>
#include <sys/atomic.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/sysctl.h>

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netinet/tcp_timer.h>
#include <netinet/tcp_var.h>

#include <err.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const struct timespec time_1000ns = { 0, 1000 };
static const struct timeval time_1us = { 0, 1 };

union sockaddr_union {
	struct sockaddr		su_sa;
	struct sockaddr_in	su_sin;
	struct sockaddr_in6	su_sin6;
};

unsigned int run_time = 10;
unsigned int sock_num = 1;
unsigned int connect_num = 1, accept_num = 1, send_num = 1, recv_num = 1,
    close_num = 1, splice_num = 0, unsplice_num = 0, drop_num = 0;
unsigned int max_percent = 0, idle_percent = 0;
volatile unsigned long max_count = 0, idle_count = 0;
int *listen_socks, *splice_listen_socks;
volatile int *connect_socks, *accept_socks,
    *splice_accept_socks, *splice_connect_socks;
union sockaddr_union *listen_addrs, *splice_listen_addrs;
struct tcp_ident_mapping *accept_tims, *splice_accept_tims;
struct sockaddr_in sin_loopback;
struct sockaddr_in6 sin6_loopback;

static void __dead
usage(void)
{
	fprintf(stderr,
	    "tcpthread [-a accept] [-c connect] [-D drop] [-I idle] [-M max] "
	    "[-n num] [-o close] [-r recv] [-S splice] [-s send] [-t time] "
	    "[-U unsplice]\n"
	    "    -a accept    threads accepting sockets, default %u\n"
	    "    -c connect   threads connecting sockets, default %u\n"
	    "    -D drop      threads dropping TCP connections, default %u\n"
	    "    -I idle      percent with splice idle time, default %u\n"
	    "    -M max       percent with splice max lenght, default %u\n"
	    "    -n num       number of file descriptors, default %d\n"
	    "    -o close     threads closing sockets, default %u\n"
	    "    -r recv      threads receiving data, default %u\n"
	    "    -S splice    threads splicing sockets, default %u\n"
	    "    -s send      threads sending data, default %u\n"
	    "    -t time      run time in seconds, default %u\n"
	    "    -U unsplice  threads running unsplice, default %u\n",
	    accept_num, connect_num, drop_num, idle_percent, max_percent,
	    sock_num, close_num, recv_num, splice_num, send_num, run_time,
	    unsplice_num);
	exit(2);
}

static volatile int *
random_socket(void)
{
	static volatile int **sockets[] = {
	    &connect_socks,
	    &accept_socks,
	    &splice_accept_socks,
	    &splice_connect_socks,
	};
	volatile int **socksp, *sockp;
	unsigned int type, num;

	type = arc4random() % (splice_num > 0 ? 4 : 2);
	num = arc4random() % sock_num;
	socksp = sockets[type];
	sockp = &(*socksp)[num];

	return sockp;
}

static int
connect_socket(volatile int *connectp, struct sockaddr *addr)
{
	int sock;

	if (*connectp != -1) {
		/* still connected, not closed */
		return 0;
	}
	/* connect to random listen socket */
	sock = socket(addr->sa_family, SOCK_STREAM | SOCK_NONBLOCK,
	    IPPROTO_TCP);
	if (sock < 0)
		err(1, "%s: socket", __func__);
	if (connect(sock, addr, addr->sa_len) < 0) {
		if (errno == EADDRNOTAVAIL) {
			/* kernel did run out of ports, ignore error */
			if (close(sock) < 0)
				err(1, "%s: close %d", __func__, sock);
			return 0;
		}
		if (errno != EINPROGRESS)
			err(1, "%s: connect %d", __func__, sock);
	}
	if ((int)atomic_cas_uint(connectp, -1, sock) != -1) {
		/* another thread has connect slot n */
		if (close(sock) < 0)
			err(1, "%s: close %d", __func__, sock);
		return 0;
	}
	return 1;
}

static void *
connect_routine(void *arg)
{
	volatile int *run = arg;
	unsigned long count = 0;
	struct sockaddr *addr;
	unsigned int type, num;
	unsigned int n;
	int connected;

	while (*run) {
		connected = 0;
		for (n = 0; n < sock_num; n++) {
			type = splice_num > 0;
			num = arc4random() % sock_num;
			addr = &(type ? splice_listen_addrs : listen_addrs)
			    [num].su_sa;
			if (!connect_socket(&connect_socks[n], addr))
				continue;
			connected = 1;
			count++;
		}
		if (!connected) {
			/* all sockets were connected, wait a bit */
			if (nanosleep(&time_1000ns, NULL) < 0)
				err(1, "%s: nanosleep", __func__);
		}
	}

	return (void *)count;
}

static int
accept_socket(volatile int *acceptp, int *listens,
    struct tcp_ident_mapping *tim, union sockaddr_union *addrs)
{
	struct sockaddr *sa;
	socklen_t len;
	unsigned int n;
	int sock;

	if (*acceptp != -1) {
		/* still accepted, not closed */
		return 0;
	}
	sock = -1;
	for (n = 0; n < sock_num; n++) {
		sa = (struct sockaddr *)&tim->faddr;
		len = sizeof(tim->faddr);
		sock = accept4(listens[n], sa, &len, SOCK_NONBLOCK);
		if (sock < 0) {
			if (errno == EWOULDBLOCK) {
				/* no connection to accept */
				continue;
			}
			if (errno == ECONNABORTED) {
				/* accepted socket was disconnected */
				continue;
			}
			err(1, "%s: accept %d", __func__, listens[n]);
		}
		sa = &addrs[n].su_sa;
		memcpy(&tim->laddr, sa, sa->sa_len);
		break;
	}
	if (sock == -1) {
		/* all listen sockets block, wait a bit */
		if (nanosleep(&time_1000ns, NULL) < 0)
			err(1, "%s: nanosleep", __func__);
		return 0;
	}
	membar_producer();
	if ((int)atomic_cas_uint(acceptp, -1, sock) != -1) {
		/* another thread has accepted slot n */
		if (close(sock) < 0)
			err(1, "%s: close %d", __func__, sock);
		return 0;
	}
	return 1;
}

static void *
accept_routine(void *arg)
{
	volatile int *run = arg;
	unsigned long count = 0;
	unsigned int n;
	int accepted;

	while (*run) {
		accepted = 0;
		for (n = 0; n < sock_num; n++) {
			if (!accept_socket(&accept_socks[n], listen_socks,
			    &accept_tims[n], listen_addrs))
				continue;
			accepted = 1;
			count++;
		}
		if (!accepted) {
			/* all sockets were accepted, wait a bit */
			if (nanosleep(&time_1000ns, NULL) < 0)
				err(1, "%s: nanosleep", __func__);
		}
	}

	return (void *)count;
}

static void *
send_routine(void *arg)
{
	volatile int *run = arg;
	unsigned long count = 0;
	char buf[1024];  /* 1 KB */
	volatile int *sockp;
	int sock;

	while (*run) {
		sockp = random_socket();
		sock = *sockp;
		if (sock == -1)
			continue;
		if (send(sock, buf, sizeof(buf), 0) < 0) {
			if (errno == EWOULDBLOCK)
				continue;
			if (errno == EFBIG)
				atomic_inc_long(&max_count);
			if (errno == ETIMEDOUT)
				atomic_inc_long(&idle_count);
			if ((int)atomic_cas_uint(sockp, sock, -1) != sock) {
				/* another thread has closed sockp */
				continue;
			}
			if (close(sock) < 0)
				err(1, "%s: close %d", __func__, sock);
		}
		count++;
	}

	return (void *)count;
}

static void *
recv_routine(void *arg)
{
	volatile int *run = arg;
	unsigned long count = 0;
	unsigned int type, num;
	char buf[10*1024];  /* 10 KB */
	volatile int *sockp;
	int sock;

	while (*run) {
		type = arc4random() % 2;
		num = arc4random() % sock_num;
		sockp = &(type ? connect_socks : accept_socks)[num];
		sock = *sockp;
		if (sock == -1)
			continue;
		errno = 0;
		if (recv(sock, buf, sizeof(buf), 0) <= 0) {
			if (errno == EWOULDBLOCK)
				continue;
			if (errno == EFBIG)
				atomic_inc_long(&max_count);
			if (errno == ETIMEDOUT)
				atomic_inc_long(&idle_count);
			if ((int)atomic_cas_uint(sockp, sock, -1) != sock) {
				/* another thread has closed sockp */
				continue;
			}
			if (close(sock) < 0)
				err(1, "%s: close %d", __func__, sock);
		}
		count++;
	}

	return (void *)count;
}

static void *
close_routine(void *arg)
{
	volatile int *run = arg;
	unsigned long count = 0;
	volatile int *sockp;
	int sock;

	while (*run) {
		sockp = random_socket();
		if (*sockp == -1)
			continue;
		sock = atomic_swap_uint(sockp, -1);
		if (sock == -1) {
			/* another thread has closed the socket, wait a bit */
			if (nanosleep(&time_1000ns, NULL) < 0)
				err(1, "%s: nanosleep", __func__);
			continue;
		}
		if (close(sock) < 0)
			err(1, "%s: close %d", __func__, sock);
		count++;
	}

	return (void *)count;
}

static void *
splice_routine(void *arg)
{
	volatile int *run = arg;
	unsigned long count = 0;
	struct sockaddr *addr;
	unsigned int num, percent;
	unsigned int n;
	int sock;
	struct splice accept_splice, connect_splice;
	int spliced;

	while (*run) {
		spliced = 0;
		for (n = 0; n < sock_num; n++) {
			if (!accept_socket(&splice_accept_socks[n],
			    splice_listen_socks,
			    &splice_accept_tims[n], splice_listen_addrs))
				continue;
			/* free the matching connect slot */
			sock = atomic_swap_uint(&splice_connect_socks[n], -1);
			if (sock != -1) {
				if (close(sock) < 0)
					err(1, "%s: close %d", __func__, sock);
			}
			num = arc4random() % sock_num;
			addr = &listen_addrs[num].su_sa;
			if (!connect_socket(&splice_connect_socks[n], addr)) {
				/* close the accepted socket */
				sock = atomic_swap_uint(
				    &splice_accept_socks[n], -1);
				if (sock != -1) {
					if (close(sock) < 0) {
						err(1, "%s: close %d",
						    __func__, sock);
					}
				}
				continue;
			}
			memset(&accept_splice, 0, sizeof(accept_splice));
			memset(&connect_splice, 0, sizeof(connect_splice));
			accept_splice.sp_fd = splice_accept_socks[n];
			connect_splice.sp_fd = splice_connect_socks[n];
			percent = arc4random() % 100;
			if (percent < max_percent) {
				accept_splice.sp_max = 1;
				connect_splice.sp_max = 1;
			}
			percent = arc4random() % 100;
			if (percent < idle_percent) {
				accept_splice.sp_idle = time_1us;
				connect_splice.sp_idle = time_1us;
			}
			if (accept_splice.sp_fd == -1 ||
			    connect_splice.sp_fd == -1 ||
			    setsockopt(accept_splice.sp_fd,
			    SOL_SOCKET, SO_SPLICE,
			    &connect_splice, sizeof(connect_splice)) < 0 ||
			    setsockopt(connect_splice.sp_fd,
			    SOL_SOCKET, SO_SPLICE,
			    &accept_splice, sizeof(accept_splice)) < 0) {
				/* close the accepted and connected socket */
				sock = atomic_swap_uint(
				    &splice_accept_socks[n], -1);
				if (sock != -1) {
					if (close(sock) < 0) {
						err(1, "%s: close %d",
						    __func__, sock);
					}
				}
				sock = atomic_swap_uint(
				    &splice_connect_socks[n], -1);
				if (sock != -1) {
					if (close(sock) < 0) {
						err(1, "%s: close %d",
						    __func__, sock);
					}
				}
				continue;
			}
			spliced = 1;
			count++;
		}
		if (!spliced) {
			/* splicing for all sockets failed, wait a bit */
			if (nanosleep(&time_1000ns, NULL) < 0)
				err(1, "%s: nanosleep", __func__);
		}
	}

	return (void *)count;
}

static void *
unsplice_routine(void *arg)
{
	volatile int *run = arg;
	unsigned long count = 0;
	unsigned int type, num;
	volatile int *sockp;
	int sock;

	while (*run) {
		type = arc4random() % 2;
		num = arc4random() % sock_num;
		sockp = &(type ? splice_accept_socks : splice_connect_socks)
		    [num];
		sock = *sockp;
		if (sock == -1)
			continue;
		if (setsockopt(sock, SOL_SOCKET, SO_SPLICE, NULL, 0) < 0) {
			if ((int)atomic_cas_uint(sockp, sock, -1) != sock) {
				/* another thread has closed sockp */
				continue;
			}
			if (close(sock) < 0)
				err(1, "%s: close %d", __func__, sock);
		}
		count++;
	}

	return (void *)count;
}

static void *
drop_routine(void *arg)
{
	static const int mib[] = { CTL_NET, PF_INET, IPPROTO_TCP, TCPCTL_DROP };
	volatile int *run = arg;
	unsigned long count = 0;
	unsigned int type, num;
	volatile int *socks;
	struct tcp_ident_mapping *tims;

	while (*run) {
		type = splice_num > 0 ? (arc4random() % 2) : 0;
		if (type) {
			socks = splice_accept_socks;
			tims = splice_accept_tims;
		} else {
			socks = accept_socks;
			tims = accept_tims;
		}
		num = arc4random() % sock_num;
		if (socks[num] == -1)
			continue;
		membar_consumer();
		/* accept_tims is not MP safe, but only ESRCH may happen */
		if (sysctl(mib, sizeof(mib) / sizeof(mib[0]), NULL, NULL,
		    &tims[num], sizeof(tims[0])) < 0) {
			if (errno == ESRCH || errno == EAFNOSUPPORT)
				continue;
			err(1, "sysctl TCPCTL_DROP");
		}
		count++;
	}

	return (void *)count;
}

int
main(int argc, char *argv[])
{
	pthread_t *connect_thread, *accept_thread, *send_thread, *recv_thread,
	    *close_thread, *splice_thread, *unsplice_thread, *drop_thread;
	struct sockaddr *sa;
	const char *errstr;
	int ch, run;
	unsigned int n;
	unsigned long connect_count, accept_count, send_count, recv_count,
	    close_count, splice_count, unsplice_count, drop_count;
	socklen_t len;

	while ((ch = getopt(argc, argv, "a:c:D:I:M:n:o:r:S:s:t:U:")) != -1) {
		switch (ch) {
		case 'a':
			accept_num = strtonum(optarg, 0, UINT_MAX, &errstr);
			if (errstr != NULL)
				errx(1, "accept is %s: %s", errstr, optarg);
			break;
		case 'c':
			connect_num = strtonum(optarg, 0, UINT_MAX, &errstr);
			if (errstr != NULL)
				errx(1, "connect is %s: %s", errstr, optarg);
			break;
		case 'D':
			drop_num = strtonum(optarg, 0, UINT_MAX, &errstr);
			if (errstr != NULL)
				errx(1, "drop is %s: %s", errstr, optarg);
			break;
		case 'I':
			idle_percent = strtonum(optarg, 0, 100, &errstr);
			if (errstr != NULL)
				errx(1, "idle is %s: %s", errstr, optarg);
			break;
		case 'M':
			max_percent = strtonum(optarg, 0, 100, &errstr);
			if (errstr != NULL)
				errx(1, "max is %s: %s", errstr, optarg);
			break;
		case 'n':
			sock_num = strtonum(optarg, 1, INT_MAX, &errstr);
			if (errstr != NULL)
				errx(1, "num is %s: %s", errstr, optarg);
			break;
		case 'o':
			close_num = strtonum(optarg, 0, UINT_MAX, &errstr);
			if (errstr != NULL)
				errx(1, "close is %s: %s", errstr, optarg);
			break;
		case 'r':
			recv_num = strtonum(optarg, 0, UINT_MAX, &errstr);
			if (errstr != NULL)
				errx(1, "recv is %s: %s", errstr, optarg);
			break;
		case 'S':
			splice_num = strtonum(optarg, 0, UINT_MAX, &errstr);
			if (errstr != NULL)
				errx(1, "splice is %s: %s", errstr, optarg);
			break;
		case 's':
			send_num = strtonum(optarg, 0, UINT_MAX, &errstr);
			if (errstr != NULL)
				errx(1, "send is %s: %s", errstr, optarg);
			break;
		case 't':
			run_time = strtonum(optarg, 0, UINT_MAX, &errstr);
			if (errstr != NULL)
				errx(1, "time is %s: %s", errstr, optarg);
			break;
		case 'U':
			unsplice_num = strtonum(optarg, 0, UINT_MAX, &errstr);
			if (errstr != NULL)
				errx(1, "unsplice is %s: %s", errstr, optarg);
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;
	if (argc > 0)
		usage();

	if (signal(SIGPIPE, SIG_IGN) == SIG_ERR)
		err(1, "signal");

	sin_loopback.sin_family = AF_INET;
	sin_loopback.sin_len = sizeof(sin_loopback);
	sin_loopback.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

	sin6_loopback.sin6_family = AF_INET6;
	sin6_loopback.sin6_len = sizeof(sin6_loopback);
	sin6_loopback.sin6_addr = in6addr_loopback;

	listen_socks = reallocarray(NULL, sock_num, sizeof(int));
	if (listen_socks == NULL)
		err(1, "listen_socks");
	connect_socks = reallocarray(NULL, sock_num, sizeof(int));
	if (connect_socks == NULL)
		err(1, "connect_socks");
	accept_socks = reallocarray(NULL, sock_num, sizeof(int));
	if (accept_socks == NULL)
		err(1, "accept_socks");
	for (n = 0; n < sock_num; n++)
		listen_socks[n] = connect_socks[n] = accept_socks[n] = -1;
	listen_addrs = calloc(sock_num, sizeof(listen_addrs[0]));
	if (listen_addrs == NULL)
		err(1, "listen_addrs");
	accept_tims = calloc(sock_num, sizeof(accept_tims[0]));
	if (accept_tims == NULL)
		err(1, "accept_tims");
	if (splice_num > 0) {
		splice_listen_socks = reallocarray(NULL, sock_num, sizeof(int));
		if (splice_listen_socks == NULL)
			err(1, "splice_listen_socks");
		splice_accept_socks = reallocarray(NULL, sock_num, sizeof(int));
		if (splice_accept_socks == NULL)
			err(1, "splice_accept_socks");
		splice_connect_socks =
		    reallocarray(NULL, sock_num, sizeof(int));
		if (splice_connect_socks == NULL)
			err(1, "splice_connect_socks");
		for (n = 0; n < sock_num; n++) {
			splice_listen_socks[n] = splice_accept_socks[n] =
			    splice_connect_socks[n] = -1;
		}
		splice_listen_addrs = calloc(sock_num,
		    sizeof(splice_listen_addrs[0]));
		if (splice_listen_addrs == NULL)
			err(1, "splice_listen_addrs");
		splice_accept_tims = calloc(sock_num,
		    sizeof(splice_accept_tims[0]));
		if (splice_accept_tims == NULL)
			err(1, "splice_accept_tims");
	}

	for (n = 0; n < sock_num; n++) {
		unsigned int family;
		int af;

		family = arc4random() % 2;
		af = family ? AF_INET : AF_INET6;
		listen_socks[n] = socket(af, SOCK_STREAM | SOCK_NONBLOCK,
		    IPPROTO_TCP);
		if (listen_socks[n] < 0)
			err(1, "socket");
		if (af == AF_INET)
			sa = (struct sockaddr *)&sin_loopback;
		if (af == AF_INET6)
			sa = (struct sockaddr *)&sin6_loopback;
		if (bind(listen_socks[n], sa, sa->sa_len) < 0)
			err(1, "bind");
		len = sizeof(listen_addrs[n]);
		if (getsockname(listen_socks[n], &listen_addrs[n].su_sa, &len)
		    < 0)
			err(1, "getsockname");
		if (listen(listen_socks[n], 128) < 0)
			err(1, "listen");

		if (splice_num > 0) {
			family = arc4random() % 2;
			af = family ? AF_INET : AF_INET6;
			splice_listen_socks[n] = socket(af,
			    SOCK_STREAM | SOCK_NONBLOCK, IPPROTO_TCP);
			if (splice_listen_socks[n] < 0)
				err(1, "socket");
			if (af == AF_INET)
				sa = (struct sockaddr *)&sin_loopback;
			if (af == AF_INET6)
				sa = (struct sockaddr *)&sin6_loopback;
			if (bind(splice_listen_socks[n], sa, sa->sa_len) < 0)
				err(1, "bind");
			len = sizeof(splice_listen_addrs[n]);
			if (getsockname(splice_listen_socks[n],
			    &splice_listen_addrs[n].su_sa, &len) < 0)
				err(1, "getsockname");
			if (listen(splice_listen_socks[n], 128) < 0)
				err(1, "listen");
		}
	}

	run = 1;

	connect_thread = calloc(connect_num, sizeof(pthread_t));
	if (connect_thread == NULL)
		err(1, "connect_thread");
	for (n = 0; n < connect_num; n++) {
		errno = pthread_create(&connect_thread[n], NULL,
		    connect_routine, &run);
		if (errno)
			err(1, "pthread_create connect %u", n);
	}

	accept_thread = calloc(accept_num, sizeof(pthread_t));
	if (accept_thread == NULL)
		err(1, "accept_thread");
	for (n = 0; n < accept_num; n++) {
		errno = pthread_create(&accept_thread[n], NULL,
		    accept_routine, &run);
		if (errno)
			err(1, "pthread_create accept %u", n);
	}

	send_thread = calloc(send_num, sizeof(pthread_t));
	if (send_thread == NULL)
		err(1, "send_thread");
	for (n = 0; n < send_num; n++) {
		errno = pthread_create(&send_thread[n], NULL,
		    send_routine, &run);
		if (errno)
			err(1, "pthread_create send %u", n);
	}

	recv_thread = calloc(recv_num, sizeof(pthread_t));
	if (recv_thread == NULL)
		err(1, "recv_thread");
	for (n = 0; n < recv_num; n++) {
		errno = pthread_create(&recv_thread[n], NULL,
		    recv_routine, &run);
		if (errno)
			err(1, "pthread_create recv %u", n);
	}

	close_thread = calloc(close_num, sizeof(pthread_t));
	if (close_thread == NULL)
		err(1, "close_thread");
	for (n = 0; n < close_num; n++) {
		errno = pthread_create(&close_thread[n], NULL,
		    close_routine, &run);
		if (errno)
			err(1, "pthread_create close %u", n);
	}

	if (splice_num > 0) {
		splice_thread = calloc(splice_num, sizeof(pthread_t));
		if (splice_thread == NULL)
			err(1, "splice_thread");
		for (n = 0; n < splice_num; n++) {
			errno = pthread_create(&splice_thread[n], NULL,
			    splice_routine, &run);
			if (errno)
				err(1, "pthread_create splice %u", n);
		}

		unsplice_thread = calloc(unsplice_num, sizeof(pthread_t));
		if (unsplice_thread == NULL)
			err(1, "unsplice_thread");
		for (n = 0; n < unsplice_num; n++) {
			errno = pthread_create(&unsplice_thread[n], NULL,
			    unsplice_routine, &run);
			if (errno)
				err(1, "pthread_create unsplice %u", n);
		}
	}
	drop_thread = calloc(drop_num, sizeof(pthread_t));
	if (drop_thread == NULL)
		err(1, "drop_thread");
	for (n = 0; n < drop_num; n++) {
		errno = pthread_create(&drop_thread[n], NULL,
		    drop_routine, &run);
		if (errno)
			err(1, "pthread_create drop %u", n);
	}

	if (run_time > 0) {
		if (sleep(run_time) < 0)
			err(1, "sleep %u", run_time);
	}
	run = 0;

	connect_count = 0;
	for (n = 0; n < connect_num; n++) {
		unsigned long count;

		errno = pthread_join(connect_thread[n], (void **)&count);
		if (errno)
			err(1, "pthread_join connect %u", n);
		connect_count += count;
	}
	free(connect_thread);

	accept_count = 0;
	for (n = 0; n < accept_num; n++) {
		unsigned long count;

		errno = pthread_join(accept_thread[n], (void **)&count);
		if (errno)
			err(1, "pthread_join accept %u", n);
		accept_count += count;
	}
	free(accept_thread);

	send_count = 0;
	for (n = 0; n < send_num; n++) {
		unsigned long count;

		errno = pthread_join(send_thread[n], (void **)&count);
		if (errno)
			err(1, "pthread_join send %u", n);
		send_count += count;
	}
	free(send_thread);

	recv_count = 0;
	for (n = 0; n < recv_num; n++) {
		unsigned long count;

		errno = pthread_join(recv_thread[n], (void **)&count);
		if (errno)
			err(1, "pthread_join recv %u", n);
		recv_count += count;
	}
	free(recv_thread);

	close_count = 0;
	for (n = 0; n < close_num; n++) {
		unsigned long count;

		errno = pthread_join(close_thread[n], (void **)&count);
		if (errno)
			err(1, "pthread_join close %u", n);
		close_count += count;
	}
	free(close_thread);

	if (splice_num > 0) {
		splice_count = 0;
		for (n = 0; n < splice_num; n++) {
			unsigned long count;

			errno = pthread_join(splice_thread[n], (void **)&count);
			if (errno)
				err(1, "pthread_join splice %u", n);
			splice_count += count;
		}
		free(splice_thread);

		unsplice_count = 0;
		for (n = 0; n < unsplice_num; n++) {
			unsigned long count;

			errno = pthread_join(unsplice_thread[n],
			    (void **)&count);
			if (errno)
				err(1, "pthread_join unsplice %u", n);
			unsplice_count += count;
		}
		free(unsplice_thread);
	}
	drop_count = 0;
	for (n = 0; n < drop_num; n++) {
		unsigned long count;

		errno = pthread_join(drop_thread[n], (void **)&count);
		if (errno)
			err(1, "pthread_join drop %u", n);
		drop_count += count;
	}
	free(drop_thread);

	free((int *)listen_socks);
	free((int *)connect_socks);
	free((int *)accept_socks);
	free(listen_addrs);
	free(accept_tims);
	if (splice_num > 0) {
		free((int *)splice_listen_socks);
		free((int *)splice_accept_socks);
		free((int *)splice_connect_socks);
		free(splice_listen_addrs);
		free(splice_accept_tims);
	}

	printf("count: connect %lu, ", connect_count);
	if (splice_num > 0) {
		printf("splice %lu, unsplice %lu, max %lu, idle %lu, ",
		    splice_count, unsplice_count, max_count, idle_count);
	}
	printf("accept %lu, send %lu, recv %lu, close %lu, drop %lu\n",
	    accept_count, send_count, recv_count, close_count, drop_count);

	return 0;
}
