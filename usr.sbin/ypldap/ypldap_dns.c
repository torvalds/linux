/*	$OpenBSD: ypldap_dns.c,v 1.8 2015/01/16 06:40:22 deraadt Exp $ */
/*	$FreeBSD$ */

/*
 * Copyright (c) 2003-2008 Henning Brauer <henning@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF MIND, USE, DATA OR PROFITS, WHETHER
 * IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING
 * OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/tree.h>
#include <sys/queue.h>

#include <netinet/in.h>
#include <arpa/nameser.h>

#include <netdb.h>
#include <pwd.h>
#include <errno.h>
#include <event.h>
#include <resolv.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>

#include "ypldap.h"

volatile sig_atomic_t	 quit_dns = 0;
struct imsgev		*iev_dns;

void	dns_dispatch_imsg(int, short, void *);
void	dns_sig_handler(int, short, void *);
void	dns_shutdown(void);
int	host_dns(const char *, struct ypldap_addr_list *);

void
dns_sig_handler(int sig, short event, void *p)
{
	switch (sig) {
	case SIGINT:
	case SIGTERM:
		dns_shutdown();
		break;
	default:
		fatalx("unexpected signal");
	}
}

void
dns_shutdown(void)
{
	log_info("dns engine exiting");
	_exit(0);
}

pid_t
ypldap_dns(int pipe_ntp[2], struct passwd *pw)
{
	pid_t			 pid;
	struct event	 ev_sigint;
	struct event	 ev_sigterm;
	struct event	 ev_sighup;
	struct env	 env;

	switch (pid = fork()) {
	case -1:
		fatal("cannot fork");
		break;
	case 0:
		break;
	default:
		return (pid);
	}

	setproctitle("dns engine");
	close(pipe_ntp[0]);

	if (setgroups(1, &pw->pw_gid) ||
	    setresgid(pw->pw_gid, pw->pw_gid, pw->pw_gid) ||
	    setresuid(pw->pw_uid, pw->pw_uid, pw->pw_uid))
		fatal("can't drop privileges");
	endservent();

	event_init();
	signal_set(&ev_sigint, SIGINT, dns_sig_handler, NULL);
	signal_set(&ev_sigterm, SIGTERM, dns_sig_handler, NULL);
	signal_set(&ev_sighup, SIGHUP, dns_sig_handler, NULL);
	signal_add(&ev_sigint, NULL);
	signal_add(&ev_sigterm, NULL);
	signal_add(&ev_sighup, NULL);

	if ((env.sc_iev = calloc(1, sizeof(*env.sc_iev))) == NULL)
		fatal(NULL);

	env.sc_iev->events = EV_READ;
	env.sc_iev->data = &env;
	imsg_init(&env.sc_iev->ibuf, pipe_ntp[1]);
	env.sc_iev->handler = dns_dispatch_imsg;
	event_set(&env.sc_iev->ev, env.sc_iev->ibuf.fd, env.sc_iev->events,
	    env.sc_iev->handler, &env);
	event_add(&env.sc_iev->ev, NULL);

	event_dispatch();
	dns_shutdown();

	return (0);
}

void
dns_dispatch_imsg(int fd, short events, void *p)
{
	struct imsg		 imsg;
	int			 n, cnt;
	char			*name;
	struct ypldap_addr_list	hn = TAILQ_HEAD_INITIALIZER(hn);
	struct ypldap_addr	*h;
	struct ibuf		*buf;
	struct env		*env = p;
	struct imsgev		*iev = env->sc_iev;
	struct imsgbuf		*ibuf = &iev->ibuf;
	int			 shut = 0;

	if ((events & (EV_READ | EV_WRITE)) == 0)
		fatalx("unknown event");

	if (events & EV_READ) {
		if ((n = imsg_read(ibuf)) == -1 && errno != EAGAIN)
			fatal("imsg_read error");
		if (n == 0)
			shut = 1;
	}
	if (events & EV_WRITE) {
		if ((n = msgbuf_write(&ibuf->w)) == -1 && errno != EAGAIN)
			fatal("msgbuf_write");
		if (n == 0)
			shut = 1;
		goto done;
	}

	for (;;) {
		if ((n = imsg_get(ibuf, &imsg)) == -1)
			fatal("client_dispatch_imsg: imsg_get error");
		if (n == 0)
			break;

		switch (imsg.hdr.type) {
		case IMSG_HOST_DNS:
			name = imsg.data;
			if (imsg.hdr.len < 1 + IMSG_HEADER_SIZE)
				fatalx("invalid IMSG_HOST_DNS received");
			imsg.hdr.len -= 1 + IMSG_HEADER_SIZE;
			if (name[imsg.hdr.len] != '\0' ||
			    strlen(name) != imsg.hdr.len)
				fatalx("invalid IMSG_HOST_DNS received");
			if ((cnt = host_dns(name, &hn)) == -1)
				break;
			buf = imsg_create(ibuf, IMSG_HOST_DNS,
			    imsg.hdr.peerid, 0,
			    cnt * sizeof(struct sockaddr_storage));
			if (buf == NULL)
				break;
			if (cnt > 0) {
				while(!TAILQ_EMPTY(&hn)) {
					h = TAILQ_FIRST(&hn);
					TAILQ_REMOVE(&hn, h, next);
					imsg_add(buf, &h->ss, sizeof(h->ss));
					free(h);
				}
			}

			imsg_close(ibuf, buf);
			break;
		default:
			break;
		}
		imsg_free(&imsg);
	}

done:
	if (!shut)
		imsg_event_add(iev);
	else {
		/* this pipe is dead, so remove the event handler */
		event_del(&iev->ev);
		event_loopexit(NULL);
	}
}

int
host_dns(const char *s, struct ypldap_addr_list *hn)
{
	struct addrinfo		 hints, *res0, *res;
	int			 error, cnt = 0;
	struct sockaddr_in	*sa_in;
	struct sockaddr_in6	*sa_in6;
	struct ypldap_addr	*h;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = PF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM; /* DUMMY */
	error = getaddrinfo(s, NULL, &hints, &res0);
	if (error == EAI_AGAIN || error == EAI_NONAME)
			return (0);
	if (error) {
		log_warnx("could not parse \"%s\": %s", s,
		    gai_strerror(error));
		return (-1);
	}

	for (res = res0; res && cnt < MAX_SERVERS_DNS; res = res->ai_next) {
		if (res->ai_family != AF_INET &&
		    res->ai_family != AF_INET6)
			continue;
		if ((h = calloc(1, sizeof(struct ypldap_addr))) == NULL)
			fatal(NULL);
		h->ss.ss_family = res->ai_family;
		if (res->ai_family == AF_INET) {
			sa_in = (struct sockaddr_in *)&h->ss;
			sa_in->sin_len = sizeof(struct sockaddr_in);
			sa_in->sin_addr.s_addr = ((struct sockaddr_in *)
			    res->ai_addr)->sin_addr.s_addr;
		} else {
			sa_in6 = (struct sockaddr_in6 *)&h->ss;
			sa_in6->sin6_len = sizeof(struct sockaddr_in6);
			memcpy(&sa_in6->sin6_addr, &((struct sockaddr_in6 *)
			    res->ai_addr)->sin6_addr, sizeof(struct in6_addr));
		}

		TAILQ_INSERT_HEAD(hn, h, next);
		cnt++;
	}
	freeaddrinfo(res0);
	return (cnt);
}
