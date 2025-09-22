/*	$OpenBSD: dvmrpd.c,v 1.34 2024/11/21 13:38:14 claudio Exp $ */

/*
 * Copyright (c) 2005 Claudio Jeker <claudio@openbsd.org>
 * Copyright (c) 2005, 2006 Esben Norby <norby@openbsd.org>
 * Copyright (c) 2003, 2004 Henning Brauer <henning@openbsd.org>
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
#include <sys/socket.h>
#include <sys/queue.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/wait.h>

#include <netinet/in.h>
#include <arpa/inet.h>

#include <event.h>
#include <err.h>
#include <errno.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <util.h>

#include "igmp.h"
#include "dvmrpd.h"
#include "dvmrp.h"
#include "dvmrpe.h"
#include "control.h"
#include "log.h"
#include "rde.h"

void		main_sig_handler(int, short, void *);
__dead void	usage(void);
__dead void	dvmrpd_shutdown(void);

void	main_dispatch_dvmrpe(int, short, void *);
void	main_dispatch_rde(int, short, void *);
void	main_imsg_compose_dvmrpe(int, pid_t, void *, u_int16_t);
void	main_imsg_compose_rde(int, pid_t, void *, u_int16_t);

int	pipe_parent2dvmrpe[2];
int	pipe_parent2rde[2];
int	pipe_dvmrpe2rde[2];

struct dvmrpd_conf	*conf = NULL;
static struct imsgev	*iev_dvmrpe;
static struct imsgev	*iev_rde;

pid_t			 dvmrpe_pid;
pid_t			 rde_pid;

void
main_sig_handler(int sig, short event, void *arg)
{
	/* signal handler rules don't apply, libevent decouples for us */
	switch (sig) {
	case SIGTERM:
	case SIGINT:
		dvmrpd_shutdown();
		/* NOTREACHED */
	case SIGHUP:
		/* reconfigure */
		/* ... */
		break;
	default:
		fatalx("unexpected signal");
		/* NOTREACHED */
	}
}

__dead void
usage(void)
{
	extern char *__progname;

	fprintf(stderr, "usage: %s [-dnv] [-f file]\n", __progname);
	exit(1);
}

int
main(int argc, char *argv[])
{
	struct event	 ev_sigint, ev_sigterm, ev_sighup;
	char		*conffile;
	int		 ch, opts = 0;
	int		 debug = 0;
	int		 ipmforwarding;
	int		 mib[4];
	size_t		 len;

	conffile = CONF_FILE;
	log_procname = "parent";

	log_init(1);	/* log to stderr until daemonized */
	log_verbose(1);

	while ((ch = getopt(argc, argv, "df:nv")) != -1) {
		switch (ch) {
		case 'd':
			debug = 1;
			break;
		case 'f':
			conffile = optarg;
			break;
		case 'n':
			opts |= DVMRPD_OPT_NOACTION;
			break;
		case 'v':
			if (opts & DVMRPD_OPT_VERBOSE)
				opts |= DVMRPD_OPT_VERBOSE2;
			opts |= DVMRPD_OPT_VERBOSE;
			log_verbose(1);
			break;
		default:
			usage();
			/* NOTREACHED */
		}
	}

	argc -= optind;
	argv += optind;
	if (argc > 0)
		usage();

	log_init(debug);
	log_verbose(opts & DVMRPD_OPT_VERBOSE);

	/* multicast IP forwarding must be enabled */
	mib[0] = CTL_NET;
	mib[1] = PF_INET;
	mib[2] = IPPROTO_IP;
	mib[3] = IPCTL_MFORWARDING;
	len = sizeof(ipmforwarding);
	if (sysctl(mib, 4, &ipmforwarding, &len, NULL, 0) == -1)
		err(1, "sysctl");

	if (!ipmforwarding)
		errx(1, "multicast IP forwarding not enabled");

	/* fetch interfaces early */
	kif_init();

	/* parse config file */
	if ((conf = parse_config(conffile, opts)) == NULL )
		exit(1);

	if (conf->opts & DVMRPD_OPT_NOACTION) {
		if (conf->opts & DVMRPD_OPT_VERBOSE)
			print_config(conf);
		else
			fprintf(stderr, "configuration OK\n");
		exit(0);
	}

	/* check for root privileges  */
	if (geteuid())
		errx(1, "need root privileges");

	/* check for dvmrpd user */
	if (getpwnam(DVMRPD_USER) == NULL)
		errx(1, "unknown user %s", DVMRPD_USER);

	/* start logging */
	log_init(1);

	if (!debug)
		daemon(1, 0);

	log_info("startup");

	if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK,
	    PF_UNSPEC, pipe_parent2dvmrpe) == -1)
		fatal("socketpair");
	if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK,
	    PF_UNSPEC, pipe_parent2rde) == -1)
		fatal("socketpair");
	if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK,
	    PF_UNSPEC, pipe_dvmrpe2rde) == -1)
		fatal("socketpair");

	/* start children */
	rde_pid = rde(conf, pipe_parent2rde, pipe_dvmrpe2rde,
	    pipe_parent2dvmrpe);
	dvmrpe_pid = dvmrpe(conf, pipe_parent2dvmrpe, pipe_dvmrpe2rde,
	    pipe_parent2rde);

	/* create the raw ip socket */
	if ((conf->mroute_socket = socket(AF_INET,
	    SOCK_RAW | SOCK_CLOEXEC | SOCK_NONBLOCK,
	    IPPROTO_IGMP)) == -1)
		fatal("error creating raw socket");

	if_set_recvbuf(conf->mroute_socket);

	if (mrt_init(conf->mroute_socket))
		fatal("multicast routing not enabled in kernel");

	event_init();

	/* setup signal handler */
	signal_set(&ev_sigint, SIGINT, main_sig_handler, NULL);
	signal_set(&ev_sigterm, SIGTERM, main_sig_handler, NULL);
	signal_set(&ev_sighup, SIGHUP, main_sig_handler, NULL);
	signal_add(&ev_sigint, NULL);
	signal_add(&ev_sigterm, NULL);
	signal_add(&ev_sighup, NULL);
	signal(SIGPIPE, SIG_IGN);

	/* setup pipes to children */
	close(pipe_parent2dvmrpe[1]);
	close(pipe_parent2rde[1]);
	close(pipe_dvmrpe2rde[0]);
	close(pipe_dvmrpe2rde[1]);

	if ((iev_dvmrpe = malloc(sizeof(struct imsgev))) == NULL ||
	    (iev_rde = malloc(sizeof(struct imsgev))) == NULL)
		fatal(NULL);
	if (imsgbuf_init(&iev_dvmrpe->ibuf, pipe_parent2dvmrpe[0]) == -1 ||
	    imsgbuf_init(&iev_rde->ibuf, pipe_parent2rde[0]) == -1)
		fatal(NULL);
	iev_dvmrpe->handler =  main_dispatch_dvmrpe;
	iev_rde->handler = main_dispatch_rde;

	/* setup event handler */
	iev_dvmrpe->events = EV_READ;
	event_set(&iev_dvmrpe->ev, iev_dvmrpe->ibuf.fd, iev_dvmrpe->events,
	    iev_dvmrpe->handler, iev_dvmrpe);
	event_add(&iev_dvmrpe->ev, NULL);

	iev_rde->events = EV_READ;
	event_set(&iev_rde->ev, iev_rde->ibuf.fd, iev_rde->events,
	    iev_rde->handler, iev_rde);
	event_add(&iev_rde->ev, NULL);

	if (kmr_init(!(conf->flags & DVMRPD_FLAG_NO_FIB_UPDATE)) == -1)
		dvmrpd_shutdown();
	if (kr_init() == -1)
		dvmrpd_shutdown();

	event_set(&conf->ev, conf->mroute_socket, EV_READ|EV_PERSIST,
	    kmr_recv_msg, conf);
	event_add(&conf->ev, NULL);

	event_dispatch();

	dvmrpd_shutdown();
	/* NOTREACHED */
	return (0);
}

__dead void
dvmrpd_shutdown(void)
{
	struct iface	*iface;
	pid_t		 pid;
	int		 status;

	/* close pipes */
	imsgbuf_clear(&iev_dvmrpe->ibuf);
	close(iev_dvmrpe->ibuf.fd);
	imsgbuf_clear(&iev_rde->ibuf);
	close(iev_rde->ibuf.fd);

	control_cleanup();
	kmr_shutdown();
	kr_shutdown();
	LIST_FOREACH(iface, &conf->iface_list, entry) {
		if_del(iface);
	}
	mrt_done(conf->mroute_socket);

	log_debug("waiting for children to terminate");
	do {
		pid = wait(&status);
		if (pid == -1) {
			if (errno != EINTR && errno != ECHILD)
				fatal("wait");
		} else if (WIFSIGNALED(status))
			log_warnx("%s terminated; signal %d",
			    (pid == rde_pid) ? "route decision engine" :
			    "dvmrp engine", WTERMSIG(status));
	} while (pid != -1 || (pid == -1 && errno == EINTR));

	free(iev_dvmrpe);
	free(iev_rde);

	log_info("terminating");
	exit(0);
}

/* imsg handling */
void
main_dispatch_dvmrpe(int fd, short event, void *bula)
{
	struct imsgev	*iev = bula;
	struct imsgbuf  *ibuf = &iev->ibuf;
	struct imsg	 imsg;
	ssize_t		 n;
	int		 shut = 0, verbose;

	if (event & EV_READ) {
		if ((n = imsgbuf_read(ibuf)) == -1)
			fatal("imsgbuf_read error");
		if (n == 0)	/* connection closed */
			shut = 1;
	}
	if (event & EV_WRITE) {
		if (imsgbuf_write(ibuf) == -1) {
			if (errno == EPIPE)	/* connection closed */
				shut = 1;
			else
				fatal("imsgbuf_write");
		}
	}

	for (;;) {
		if ((n = imsg_get(ibuf, &imsg)) == -1)
			fatal("imsg_get");

		if (n == 0)
			break;

		switch (imsg.hdr.type) {
		case IMSG_CTL_RELOAD:
			log_debug("main_dispatch_dvmrpe: IMSG_CTL_RELOAD");
			/* reconfig */
			break;
		case IMSG_CTL_MFC_COUPLE:
			kmr_mfc_couple();
			break;
		case IMSG_CTL_MFC_DECOUPLE:
			kmr_mfc_decouple();
			break;
		case IMSG_CTL_LOG_VERBOSE:
			/* already checked by dvmrpe */
			memcpy(&verbose, imsg.data, sizeof(verbose));
			log_verbose(verbose);
			break;
		default:
			log_debug("main_dispatch_dvmrpe: error handling "
			    "imsg %d", imsg.hdr.type);
			break;
		}
		imsg_free(&imsg);
	}
	if (!shut)
		imsg_event_add(iev);
	else {
		/* this pipe is dead, so remove the event handler */
		event_del(&iev->ev);
		event_loopexit(NULL);
	}
}

void
main_dispatch_rde(int fd, short event, void *bula)
{
	struct mfc	 mfc;
	struct imsgev	*iev = bula;
	struct imsgbuf  *ibuf = &iev->ibuf;
	struct imsg	 imsg;
	ssize_t		 n;
	int		 shut = 0;

	if (event & EV_READ) {
		if ((n = imsgbuf_read(ibuf)) == -1)
			fatal("imsgbuf_read error");
		if (n == 0)	/* connection closed */
			shut = 1;
	}
	if (event & EV_WRITE) {
		if (imsgbuf_write(ibuf) == -1) {
			if (errno == EPIPE)	/* connection closed */
				shut = 1;
			else
				fatal("imsgbuf_write");
		}
	}

	for (;;) {
		if ((n = imsg_get(ibuf, &imsg)) == -1)
			fatal("imsg_get");

		if (n == 0)
			break;

		switch (imsg.hdr.type) {
		case IMSG_MFC_ADD:
			if (imsg.hdr.len - IMSG_HEADER_SIZE != sizeof(mfc))
				fatalx("invalid size of RDE request");
			memcpy(&mfc, imsg.data, sizeof(mfc));

			/* add to MFC */
			mrt_add_mfc(conf->mroute_socket, &mfc);
			break;
		case IMSG_MFC_DEL:
			if (imsg.hdr.len - IMSG_HEADER_SIZE != sizeof(mfc))
				fatalx("invalid size of RDE request");
			memcpy(&mfc, imsg.data, sizeof(mfc));

			/* remove from MFC */
			mrt_del_mfc(conf->mroute_socket, &mfc);
			break;
		default:
			log_debug("main_dispatch_rde: error handling imsg %d",
			    imsg.hdr.type);
			break;
		}
		imsg_free(&imsg);
	}
	if (!shut)
		imsg_event_add(iev);
	else {
		/* this pipe is dead, so remove the event handler */
		event_del(&iev->ev);
		event_loopexit(NULL);
	}
}

void
main_imsg_compose_dvmrpe(int type, pid_t pid, void *data, u_int16_t datalen)
{
	imsg_compose_event(iev_dvmrpe, type, 0, pid, -1, data, datalen);
}

void
main_imsg_compose_rde(int type, pid_t pid, void *data, u_int16_t datalen)
{
	imsg_compose_event(iev_rde, type, 0, pid, -1, data, datalen);
}

void
imsg_event_add(struct imsgev *iev)
{
	iev->events = EV_READ;
	if (imsgbuf_queuelen(&iev->ibuf) > 0)
		iev->events |= EV_WRITE;

	event_del(&iev->ev);
	event_set(&iev->ev, iev->ibuf.fd, iev->events, iev->handler, iev);
	event_add(&iev->ev, NULL);
}

int
imsg_compose_event(struct imsgev *iev, u_int16_t type,
    u_int32_t peerid, pid_t pid, int fd, void *data, u_int16_t datalen)
{
	int	ret;

	if ((ret = imsg_compose(&iev->ibuf, type, peerid,
	    pid, fd, data, datalen)) != -1)
		imsg_event_add(iev);
	return (ret);
}
