/*
 * Copyright (c) 2016 Vincent Gross <vincent.gross@kilob.yt>
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

#include <err.h>
#include <errno.h>
#include <getopt.h>
#include <netdb.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <netinet/in.h>

#include <arpa/inet.h>

#define PORTNUM "23000"

int
main(int argc, char *argv[])
{
	struct addrinfo		  hints;
	struct addrinfo		 *in6ai;

	struct sockaddr_in6	 *null_sin6 = NULL;
	struct sockaddr_in6	**next_sin6_p = NULL;
	struct sockaddr_in6	**first_sin6p = &null_sin6;
	struct sockaddr_in6	**bind_sin6p = &null_sin6;
	struct sockaddr_in6	**sendmsg_sin6p = &null_sin6;
	struct sockaddr_in6	**setsockopt_sin6p = &null_sin6;
	struct sockaddr_in6	**dst_sin6p = &null_sin6;

	int			  ch, rc, wstat, expected = -1;
	int			  first_sock;
	int			  reuse_addr = 0;
	pid_t			  pid;

	const char		 *numerr;
	char			  adrbuf[40];
	const char		 *adrp;


	bzero(&hints, sizeof(hints));
	hints.ai_family = AF_INET6;
	hints.ai_socktype = SOCK_DGRAM;

	do {
		if (next_sin6_p == NULL)
			next_sin6_p = malloc(sizeof(*next_sin6_p));
		if (next_sin6_p == NULL)
			err(2, "malloc()");
		*next_sin6_p = NULL;
		while ((ch = getopt(argc, argv, "dfbmoe:")) != -1) {
			switch(ch) {
			case 'd':
				dst_sin6p = next_sin6_p;
				break;
			case 'f':
				first_sin6p = next_sin6_p;
				break;
			case 'b':
				bind_sin6p = next_sin6_p;
				break;
			case 'm':
				sendmsg_sin6p = next_sin6_p;
				break;
			case 'o':
				setsockopt_sin6p = next_sin6_p;
				break;
			case 'e':
				expected = strtonum(optarg, 0, 255, &numerr);
				if (numerr != NULL)
					errx(2, "strtonum(%s): %s", optarg, numerr);
				break;
			}
		}
		if (optind < argc) {
			rc = getaddrinfo(argv[optind], PORTNUM, &hints, &in6ai);
			if (rc)
				errx(2, "getaddrinfo(%s) = %d: %s",
				    argv[0], rc, gai_strerror(rc));
			*next_sin6_p = (struct sockaddr_in6 *)in6ai->ai_addr;
			next_sin6_p = NULL;
		}
		optreset = 1; optind++;
	} while (optind < argc);

	if (*bind_sin6p == NULL)
		errx(2, "bind_sin6p == NULL");

	if (*dst_sin6p == NULL)
		errx(2, "dst_sin6p == NULL");

	if (expected < 0)
		errx(2, "need expected");

	if (*first_sin6p) {
		first_sock = udp6_first(*first_sin6p);
		reuse_addr = 1;
	}

	pid = fork();
	if (pid == 0) {
		return udp6_override(*dst_sin6p, *bind_sin6p,
		    *setsockopt_sin6p, *sendmsg_sin6p, reuse_addr);
	}
	(void)wait(&wstat);

	if (*first_sin6p)
		close(first_sock);

	if (! WIFEXITED(wstat))
		errx(2, "error setting up override");

	if (WEXITSTATUS(wstat) != expected)
		errx(2, "expected %d, got %d", expected, WEXITSTATUS(wstat));

	return EXIT_SUCCESS;
}


int
udp6_first(struct sockaddr_in6 *src)
{
	int s_con;

	s_con = socket(AF_INET6, SOCK_DGRAM, 0);
	if (s_con == -1)
		err(2, "udp6_bind: socket()");

	if (bind(s_con, (struct sockaddr *)src, src->sin6_len))
		err(2, "udp6_bind: bind()");

	return s_con;
}


int
udp6_override(struct sockaddr_in6 *dst, struct sockaddr_in6 *src_bind,
    struct sockaddr_in6 *src_setsockopt, struct sockaddr_in6 *src_sendmsg,
    int reuse_addr)
{
	int			 s, optval, error, saved_errno;
	ssize_t			 send_rc;
	struct msghdr		 msg;
	struct iovec		 iov;
	struct cmsghdr		*cmsg;
	struct in6_pktinfo	*pi_sendmsg;
	struct in6_pktinfo	 pi_setsockopt;
	union {
		struct cmsghdr	hdr;
		unsigned char	buf[CMSG_SPACE(sizeof(struct in6_pktinfo))];
	} cmsgbuf;

	bzero(&msg, sizeof(msg));
	bzero(&cmsgbuf, sizeof(cmsgbuf));
	bzero(&pi_setsockopt, sizeof(pi_setsockopt));

	s = socket(AF_INET6, SOCK_DGRAM, 0);
	if (s == -1) {
		warn("udp6_override: socket()");
		kill(getpid(), SIGTERM);
	}

	if (reuse_addr) {
		optval = 1;
		if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(int))) {
			warn("udp6_override: setsockopt(SO_REUSEADDR)");
			kill(getpid(), SIGTERM);
		}
	}

	if (bind(s, (struct sockaddr *)src_bind, src_bind->sin6_len)) {
		warn("udp6_override: bind()");
		kill(getpid(), SIGTERM);
	}

	if (src_setsockopt != NULL) {
		memcpy(&pi_setsockopt.ipi6_addr, &src_setsockopt->sin6_addr, sizeof(struct in6_addr));
		if (setsockopt(s, IPPROTO_IPV6, IPV6_PKTINFO, &pi_setsockopt, sizeof(pi_setsockopt))) {
			warn("udp6_override: setsockopt(IPV6_PKTINFO)");
			kill(getpid(), SIGTERM);
		}
	}

	iov.iov_base = "payload";
	iov.iov_len = 8;
	msg.msg_name = dst;
	msg.msg_namelen = dst->sin6_len;
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;

	if (src_sendmsg) {
		msg.msg_control = &cmsgbuf.buf;
		msg.msg_controllen = sizeof(cmsgbuf.buf);
		cmsg = CMSG_FIRSTHDR(&msg);
		cmsg->cmsg_len = CMSG_LEN(sizeof(struct in6_pktinfo));
		cmsg->cmsg_level = IPPROTO_IPV6;
		cmsg->cmsg_type = IPV6_PKTINFO;
		pi_sendmsg = (struct in6_pktinfo *)CMSG_DATA(cmsg);
		memcpy(&pi_sendmsg->ipi6_addr, &src_sendmsg->sin6_addr, sizeof(struct in6_addr));
	}

	send_rc = sendmsg(s, &msg, 0);
	saved_errno = errno;

	close(s);

	if (send_rc == iov.iov_len)
		return 0;
	return saved_errno;
}
