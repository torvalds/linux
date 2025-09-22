/*	$OpenBSD: gen_traffic.c,v 1.3 2022/05/31 19:01:46 mbuhl Exp $ */
/*
 * Copyright (c) 2013 Florian Obser <florian@openbsd.org>
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
#include <sys/ioctl.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <err.h>
#include <ctype.h>
#include <errno.h>
#include <event.h>
#include <netdb.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

__dead void	usage(void);
void		gen_traffic_paused(int, short, void*);
void		gen_traffic_accept(int, short, void*);
void		gen_traffic_request(int, short, void*);
void		gen_traffic_write(int, short, void*);
void		gen_traffic_sender_paused(int, short, void*);

struct listener {
	struct event	ev, pause;
};

struct reader {
	struct event	ev;
	int		fd;
};

struct sender {
	struct event	ev, pause;
	int		fd;
};

__dead void
usage(void)
{
	extern char *__progname;
	fprintf(stderr, "usage: %s 4|6\n", __progname);
	exit(1);
}


int on = 1;

int
main(int argc, char *argv[])
{
	struct addrinfo  hints, *sender_res, *server_res;
	struct listener *l;
	struct sender	*sender;
	char		*ip;
	int		 error, s;

	if (argc != 2)
		usage();
	if (strncmp(argv[1], "4", 1) == 0)
		ip = "10.11.12.13";
	else if (strncmp(argv[1], "6", 1) == 0)
		ip = "2001:db8::13";
	else
		usage();

	event_init();

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = PF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	error = getaddrinfo(ip, "12346", &hints, &server_res);
	if (error)
		errx(1, "%s", gai_strerror(error));
	s = socket(server_res->ai_family, server_res->ai_socktype,
		    server_res->ai_protocol);
	if (s == -1)
		err(1, "%s", "bind");
	if (bind(s, server_res->ai_addr, server_res->ai_addrlen) < 0)
		err(1, "%s", "bind");
	if (ioctl(s, FIONBIO, &on) == -1)
		err(1, "%s", "listener ioctl(FIONBIO)");
	if (listen(s, 5) == -1)
		err(1, "%s", "listen");

	l = calloc(1, sizeof(*l));
	if (l == NULL)
		errx(1, "calloc");
	event_set(&l->ev, s, EV_READ | EV_PERSIST, gen_traffic_accept, l);
	event_add(&l->ev, NULL);
	evtimer_set(&l->pause, gen_traffic_paused, l);

	error = getaddrinfo(ip, "12345", &hints, &sender_res);
	if (error)
		errx(1, "%s", gai_strerror(error));
	s = socket(sender_res->ai_family, sender_res->ai_socktype,
		    sender_res->ai_protocol);
	if (s == -1)
		err(1, "%s", "bind");
	if (bind(s, sender_res->ai_addr, sender_res->ai_addrlen) < 0)
		err(1, "%s", "bind");
	if (ioctl(s, FIONBIO, &on) == -1)
		err(1, "%s", "sender ioctl(FIONBIO)");
	sender = calloc(1, sizeof(*sender));
	if (sender == NULL)
		errx(1, "calloc");
	if (connect(s, server_res->ai_addr, server_res->ai_addrlen) == -1 &&
	    errno != EINPROGRESS)
		err(1, "%s", "connect");
	event_set(&sender->ev, s, EV_WRITE | EV_PERSIST, gen_traffic_write,
	    sender);
	event_add(&sender->ev, NULL);
	evtimer_set(&sender->pause, gen_traffic_sender_paused, sender);
	event_dispatch();
	exit(0);
}

void
gen_traffic_paused(int fd, short events, void *arg)
{
	struct listener	*l = arg;
	event_add(&l->ev, NULL);
}

void
gen_traffic_sender_paused(int fd, short events, void *arg)
{
	struct sender	*s = arg;
	event_add(&s->ev, NULL);
}

void
gen_traffic_accept(int fd, short events, void *arg)
{
	struct listener		*l;
	struct sockaddr_storage	 ss;
	struct timeval		 pause;
	struct reader		*r;
	socklen_t		 len;
	int			 s;

	l = arg;
	pause.tv_sec = 1; pause.tv_usec = 0;

	len = sizeof(ss);
	s = accept(fd, (struct sockaddr *)&ss, &len);
	if (s == -1) {
		switch (errno) {
		case EINTR:
		case EWOULDBLOCK:
		case ECONNABORTED:
			return;
		case EMFILE:
		case ENFILE:
			event_del(&l->ev);
			evtimer_add(&l->pause, &pause);
			return;
		default:
			err(1, "%s", "accept");
		}
	}

	if (ioctl(s, FIONBIO, &on) == -1)
		err(1, "%s", "reader ioctl(FIONBIO)");

	r = calloc(1, sizeof(*r));
	if (r == NULL)
		errx(1, "%s", "cannot calloc reader");

	r->fd = s;

	event_set(&r->ev, s, EV_READ | EV_PERSIST, gen_traffic_request, r);
	event_add(&r->ev, NULL);
}

void
gen_traffic_request(int fd, short events, void *arg)
{
	static size_t	 total = 0;
	struct reader	*r;
	size_t		 n;
	uint8_t		 buf[4096];

	r = arg;

	n = read(fd, buf, 4096);

	switch (n) {
	case -1:
		switch (errno) {
		case EINTR:
		case EAGAIN:
			return;
		default:
			err(1, "%s", "read");
		}
		break;

	case 0:
		exit(0);
	default:
		total += n;
		/* warnx("read: %lld - %lld", n, total); */
		break;
	}
}

void
gen_traffic_write(int fd, short events, void *arg)
{
	static int	 count = 0;
	struct timeval	 pause;
	struct sender	*s;
	uint8_t		 buf[4096];

	s = arg;
	pause.tv_sec = 1;
	pause.tv_usec = 0;
	event_del(&s->ev);
	if (count++ >= 10) {
		/* warnx("%s", "done writing"); */
		close(fd);
		return;
	}
	if (write(fd, buf, 4096) == -1)
		err(1, "%s", "write");
	evtimer_add(&s->pause, &pause);
}
