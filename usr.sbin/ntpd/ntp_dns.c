/*	$OpenBSD: ntp_dns.c,v 1.36 2024/11/21 13:38:14 claudio Exp $ */

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
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/nameser.h>
#include <resolv.h>

#include <err.h>
#include <errno.h>
#include <poll.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include "ntpd.h"

volatile sig_atomic_t	 quit_dns = 0;
static struct imsgbuf	*ibuf_dns;
extern int		 non_numeric;

void	sighdlr_dns(int);
int	dns_dispatch_imsg(struct ntpd_conf *);
int	probe_root_ns(void);
void	probe_root(void);

void
sighdlr_dns(int sig)
{
	switch (sig) {
	case SIGTERM:
	case SIGINT:
		quit_dns = 1;
		break;
	}
}

void
ntp_dns(struct ntpd_conf *nconf, struct passwd *pw)
{
	struct pollfd		 pfd[1];
	int			 nfds, nullfd;

	res_init();
	if (setpriority(PRIO_PROCESS, 0, 0) == -1)
		log_warn("could not set priority");

	log_init(nconf->debug ? LOG_TO_STDERR : LOG_TO_SYSLOG, nconf->verbose,
	    LOG_DAEMON);
	if (!nconf->debug && setsid() == -1)
		fatal("setsid");
	log_procinit("dns");

	if ((nullfd = open("/dev/null", O_RDWR)) == -1)
		fatal(NULL);

	if (!nconf->debug) {
		dup2(nullfd, STDIN_FILENO);
		dup2(nullfd, STDOUT_FILENO);
		dup2(nullfd, STDERR_FILENO);
	}
	close(nullfd);

	setproctitle("dns engine");

	if (setgroups(1, &pw->pw_gid) ||
	    setresgid(pw->pw_gid, pw->pw_gid, pw->pw_gid) ||
	    setresuid(pw->pw_uid, pw->pw_uid, pw->pw_uid))
		fatal("can't drop privileges");

	signal(SIGTERM, sighdlr_dns);
	signal(SIGINT, sighdlr_dns);
	signal(SIGHUP, SIG_IGN);

	if ((ibuf_dns = malloc(sizeof(struct imsgbuf))) == NULL)
		fatal(NULL);
	if (imsgbuf_init(ibuf_dns, PARENT_SOCK_FILENO) == -1)
		fatal(NULL);

	if (pledge("stdio dns", NULL) == -1)
		err(1, "pledge");

	if (non_numeric)
		probe_root();
	else
		log_debug("all addresses numeric, no dns probe");

	while (quit_dns == 0) {
		pfd[0].fd = ibuf_dns->fd;
		pfd[0].events = POLLIN;
		if (imsgbuf_queuelen(ibuf_dns) > 0)
			pfd[0].events |= POLLOUT;

		if ((nfds = poll(pfd, 1, INFTIM)) == -1)
			if (errno != EINTR) {
				log_warn("poll error");
				quit_dns = 1;
			}

		if (nfds > 0 && (pfd[0].revents & POLLOUT))
			if (imsgbuf_write(ibuf_dns) == -1) {
				log_warn("pipe write error (to ntp engine)");
				quit_dns = 1;
			}

		if (nfds > 0 && pfd[0].revents & POLLIN) {
			nfds--;
			if (dns_dispatch_imsg(nconf) == -1)
				quit_dns = 1;
		}
	}

	imsgbuf_clear(ibuf_dns);
	free(ibuf_dns);
	exit(0);
}

int
dns_dispatch_imsg(struct ntpd_conf *nconf)
{
	struct imsg		 imsg;
	int			 n, cnt;
	char			*name;
	struct ntp_addr		*h, *hn;
	struct ibuf		*buf;
	const char		*str;
	size_t			 len;

	if (imsgbuf_read(ibuf_dns) != 1)
		return (-1);

	for (;;) {
		if ((n = imsg_get(ibuf_dns, &imsg)) == -1)
			return (-1);

		if (n == 0)
			break;

		switch (imsg.hdr.type) {
		case IMSG_HOST_DNS:
		case IMSG_CONSTRAINT_DNS:
			if (imsg.hdr.type == IMSG_HOST_DNS)
				str = "IMSG_HOST_DNS";
			else
				str = "IMSG_CONSTRAINT_DNS";
			name = imsg.data;
			if (imsg.hdr.len < 1 + IMSG_HEADER_SIZE)
				fatalx("invalid %s received", str);
			len = imsg.hdr.len - 1 - IMSG_HEADER_SIZE;
			if (name[len] != '\0' ||
			    strlen(name) != len)
				fatalx("invalid %s received", str);
			if ((cnt = host_dns(name, nconf->status.synced,
			    &hn)) == -1)
				break;
			buf = imsg_create(ibuf_dns, imsg.hdr.type,
			    imsg.hdr.peerid, 0,
			    cnt * (sizeof(struct sockaddr_storage) + sizeof(int)));
			if (cnt > 0) {
				if (buf) {
					for (h = hn; h != NULL; h = h->next) {
						if (imsg_add(buf, &h->ss,
						    sizeof(h->ss)) == -1) {
							buf = NULL;
							break;
						}
						if (imsg_add(buf, &h->notauth,
						    sizeof(int)) == -1) {
							buf = NULL;
							break;
						}
					}
				}
				host_dns_free(hn);
				hn = NULL;
			}
			if (buf)
				imsg_close(ibuf_dns, buf);
			break;
		case IMSG_SYNCED:
			nconf->status.synced = 1;
			break;
		case IMSG_UNSYNCED:
			nconf->status.synced = 0;
			break;
		default:
			break;
		}
		imsg_free(&imsg);
	}
	return (0);
}

int
probe_root_ns(void)
{
	int ret;
	int old_retrans, old_retry, old_options;
	unsigned char buf[4096];

	old_retrans = _res.retrans;
	old_retry = _res.retry;
	old_options = _res.options;
	_res.retrans = 1;
	_res.retry = 1;
	_res.options |= RES_USE_CD;
		
	ret = res_query(".", C_IN, T_NS, buf, sizeof(buf));

	_res.retrans = old_retrans;
	_res.retry = old_retry;
	_res.options = old_options;
	
	return ret;
}

void
probe_root(void)
{
	int		i, n;
	struct timespec	start, probe_start, probe_end;
	struct timespec	duration;

	clock_gettime(CLOCK_MONOTONIC, &start);
	for (i = 0; ; i++) {
		clock_gettime(CLOCK_MONOTONIC, &probe_start);
		n = probe_root_ns();
		clock_gettime(CLOCK_MONOTONIC, &probe_end);
		if (n >= 0)
			break;
		timespecsub(&probe_end, &start, &duration);
		if (duration.tv_sec > 5)
			break;
		timespecsub(&probe_end, &probe_start, &duration);
		/* normally the probe takes 1s * nscount, but
		   sleep a little if the probe returned quickly */
		if (duration.tv_sec == 0)
			sleep(1);
	}
	if (i > 0)
		log_warnx("DNS root probe failed %d times (%s)", i,
		    n >= 0 ? "eventually succeeded": "gave up");
	if (imsg_compose(ibuf_dns, IMSG_PROBE_ROOT, 0, 0, -1, &n,
	    sizeof(int)) == -1)
		fatalx("probe_root");
}
