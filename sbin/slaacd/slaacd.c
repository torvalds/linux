/*	$OpenBSD: slaacd.c,v 1.81 2025/08/06 16:50:53 florian Exp $	*/

/*
 * Copyright (c) 2017 Florian Obser <florian@openbsd.org>
 * Copyright (c) 2005 Claudio Jeker <claudio@openbsd.org>
 * Copyright (c) 2004 Esben Norby <norby@openbsd.org>
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
#include <sys/ioctl.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/syslog.h>
#include <sys/sysctl.h>
#include <sys/uio.h>
#include <sys/wait.h>

#include <net/if.h>
#include <net/route.h>
#include <netinet/in.h>
#include <netinet/if_ether.h>
#include <netinet6/in6_var.h>
#include <netinet/icmp6.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <event.h>
#include <imsg.h>
#include <netdb.h>
#include <pwd.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

#include "log.h"
#include "slaacd.h"
#include "frontend.h"
#include "engine.h"
#include "control.h"

enum slaacd_process {
	PROC_MAIN,
	PROC_ENGINE,
	PROC_FRONTEND
};

__dead void	usage(void);
__dead void	main_shutdown(void);

void	main_sig_handler(int, short, void *);

static pid_t	start_child(enum slaacd_process, char *, int, int, int);

void	main_dispatch_frontend(int, short, void *);
void	main_dispatch_engine(int, short, void *);
void	open_icmp6sock(int);
void	configure_interface(struct imsg_configure_address *);
void	delete_address(struct imsg_configure_address *);
void	configure_gateway(struct imsg_configure_dfr *, uint8_t);
void	add_gateway(struct imsg_configure_dfr *);
void	delete_gateway(struct imsg_configure_dfr *);
void	send_rdns_proposal(struct imsg_propose_rdns *);
void	read_soiikey(void);
int	parse_hex_char(char);
ssize_t	parse_hex_string(unsigned char *, size_t, const char *);

static int	main_imsg_send_ipc_sockets(struct imsgbuf *, struct imsgbuf *);
int		main_imsg_compose_frontend(int, int, void *, uint16_t);
int		main_imsg_compose_engine(int, pid_t, void *, uint16_t);

static struct imsgev	*iev_frontend;
static struct imsgev	*iev_engine;

pid_t			 frontend_pid;
pid_t			 engine_pid;

int			 routesock, ioctl_sock, rtm_seq = 0;
uint8_t			 soiikey[SLAACD_SOIIKEY_LEN];

void
main_sig_handler(int sig, short event, void *arg)
{
	/*
	 * Normal signal handler rules don't apply because libevent
	 * decouples for us.
	 */

	switch (sig) {
	case SIGTERM:
	case SIGINT:
		main_shutdown();
	default:
		fatalx("unexpected signal");
	}
}

__dead void
usage(void)
{
	extern char *__progname;

	fprintf(stderr, "usage: %s [-dv] [-s socket]\n",
	    __progname);
	exit(1);
}

int
main(int argc, char *argv[])
{
	struct event		 ev_sigint, ev_sigterm;
	int			 ch;
	int			 debug = 0, engine_flag = 0, frontend_flag = 0;
	int			 verbose = 0;
	char			*saved_argv0;
	int			 pipe_main2frontend[2];
	int			 pipe_main2engine[2];
	int			 frontend_routesock, rtfilter, lockfd;
	int			 rtable_any = RTABLE_ANY;
	char			*csock = _PATH_SLAACD_SOCKET;
	struct imsg_propose_rdns rdns;
#ifndef SMALL
	int			 control_fd;
#endif /* SMALL */

	log_init(1, LOG_DAEMON);	/* Log to stderr until daemonized. */
	log_setverbose(1);

	saved_argv0 = argv[0];
	if (saved_argv0 == NULL)
		saved_argv0 = "slaacd";

	while ((ch = getopt(argc, argv, "dEFs:v")) != -1) {
		switch (ch) {
		case 'd':
			debug = 1;
			break;
		case 'E':
			engine_flag = 1;
			break;
		case 'F':
			frontend_flag = 1;
			break;
		case 's':
			csock = optarg;
			break;
		case 'v':
			verbose++;
			break;
		default:
			usage();
		}
	}

	argc -= optind;
	argv += optind;
	if (argc > 0 || (engine_flag && frontend_flag))
		usage();

	if (engine_flag)
		engine(debug, verbose);
	else if (frontend_flag)
		frontend(debug, verbose);

	/* Check for root privileges. */
	if (geteuid())
		errx(1, "need root privileges");

	lockfd = open(_PATH_LOCKFILE, O_CREAT|O_RDWR|O_EXLOCK|O_NONBLOCK, 0600);
	if (lockfd == -1) {
		if (errno == EAGAIN)
			errx(1, "already running");
		err(1, "%s", _PATH_LOCKFILE);
	}

	/* Check for assigned daemon user */
	if (getpwnam(SLAACD_USER) == NULL)
		errx(1, "unknown user %s", SLAACD_USER);

	log_init(debug, LOG_DAEMON);
	log_setverbose(verbose);

	if (!debug)
		daemon(0, 0);

	if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK,
	    PF_UNSPEC, pipe_main2frontend) == -1)
		fatal("main2frontend socketpair");
	if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK,
	    PF_UNSPEC, pipe_main2engine) == -1)
		fatal("main2engine socketpair");

	/* Start children. */
	engine_pid = start_child(PROC_ENGINE, saved_argv0, pipe_main2engine[1],
	    debug, verbose);
	frontend_pid = start_child(PROC_FRONTEND, saved_argv0,
	    pipe_main2frontend[1], debug, verbose);

	log_procinit("main");

	if ((routesock = socket(AF_ROUTE, SOCK_RAW | SOCK_CLOEXEC |
	    SOCK_NONBLOCK, AF_INET6)) == -1)
		fatal("route socket");
	shutdown(routesock, SHUT_RD);

	event_init();

	/* Setup signal handler. */
	signal_set(&ev_sigint, SIGINT, main_sig_handler, NULL);
	signal_set(&ev_sigterm, SIGTERM, main_sig_handler, NULL);
	signal_add(&ev_sigint, NULL);
	signal_add(&ev_sigterm, NULL);
	signal(SIGPIPE, SIG_IGN);
	signal(SIGHUP, SIG_IGN);

	/* Setup pipes to children. */

	if ((iev_frontend = malloc(sizeof(struct imsgev))) == NULL ||
	    (iev_engine = malloc(sizeof(struct imsgev))) == NULL)
		fatal(NULL);
	if (imsgbuf_init(&iev_frontend->ibuf, pipe_main2frontend[0]) == -1)
		fatal(NULL);
	imsgbuf_allow_fdpass(&iev_frontend->ibuf);
	iev_frontend->handler = main_dispatch_frontend;
	if (imsgbuf_init(&iev_engine->ibuf, pipe_main2engine[0]) == -1)
		fatal(NULL);
	imsgbuf_allow_fdpass(&iev_engine->ibuf);
	iev_engine->handler = main_dispatch_engine;

	/* Setup event handlers for pipes to engine & frontend. */
	iev_frontend->events = EV_READ;
	event_set(&iev_frontend->ev, iev_frontend->ibuf.fd,
	    iev_frontend->events, iev_frontend->handler, iev_frontend);
	event_add(&iev_frontend->ev, NULL);

	iev_engine->events = EV_READ;
	event_set(&iev_engine->ev, iev_engine->ibuf.fd, iev_engine->events,
	    iev_engine->handler, iev_engine);
	event_add(&iev_engine->ev, NULL);

	if (main_imsg_send_ipc_sockets(&iev_frontend->ibuf, &iev_engine->ibuf))
		fatal("could not establish imsg links");

	if ((ioctl_sock = socket(AF_INET6, SOCK_DGRAM | SOCK_CLOEXEC, 0)) == -1)
		fatal("socket");

	if ((frontend_routesock = socket(AF_ROUTE, SOCK_RAW | SOCK_CLOEXEC,
	    AF_INET6)) == -1)
		fatal("route socket");

	rtfilter = ROUTE_FILTER(RTM_IFINFO) | ROUTE_FILTER(RTM_NEWADDR) |
	    ROUTE_FILTER(RTM_DELADDR) | ROUTE_FILTER(RTM_DELETE) |
	    ROUTE_FILTER(RTM_CHGADDRATTR) | ROUTE_FILTER(RTM_PROPOSAL) |
	    ROUTE_FILTER(RTM_IFANNOUNCE);
	if (setsockopt(frontend_routesock, AF_ROUTE, ROUTE_MSGFILTER,
	    &rtfilter, sizeof(rtfilter)) == -1)
		fatal("setsockopt(ROUTE_MSGFILTER)");
	if (setsockopt(frontend_routesock, AF_ROUTE, ROUTE_TABLEFILTER,
	    &rtable_any, sizeof(rtable_any)) == -1)
		fatal("setsockopt(ROUTE_TABLEFILTER)");

#ifndef SMALL
	if ((control_fd = control_init(csock)) == -1)
		warnx("control socket setup failed");

	read_soiikey();
#endif /* SMALL */

	if (pledge("stdio inet sendfd wroute", NULL) == -1)
		fatal("pledge");

	main_imsg_compose_frontend(IMSG_ROUTESOCK, frontend_routesock, NULL, 0);

#ifndef SMALL
	if (control_fd != -1)
		main_imsg_compose_frontend(IMSG_CONTROLFD, control_fd, NULL, 0);
#endif /* SMALL */

	main_imsg_compose_frontend(IMSG_STARTUP, -1, NULL, 0);

	/* we are taking over, clear all previos slaac proposals */
	memset(&rdns, 0, sizeof(rdns));
	rdns.if_index = 0;
	rdns.rdns_count = 0;
	send_rdns_proposal(&rdns);

	event_dispatch();

	main_shutdown();
	return (0);
}

__dead void
main_shutdown(void)
{
	pid_t	 pid;
	int	 status;

	/* Close pipes. */
	imsgbuf_clear(&iev_frontend->ibuf);
	close(iev_frontend->ibuf.fd);
	imsgbuf_clear(&iev_engine->ibuf);
	close(iev_engine->ibuf.fd);

	log_debug("waiting for children to terminate");
	do {
		pid = wait(&status);
		if (pid == -1) {
			if (errno != EINTR && errno != ECHILD)
				fatal("wait");
		} else if (WIFSIGNALED(status))
			log_warnx("%s terminated; signal %d",
			    (pid == engine_pid) ? "engine" :
			    "frontend", WTERMSIG(status));
	} while (pid != -1 || (pid == -1 && errno == EINTR));

	free(iev_frontend);
	free(iev_engine);

	log_info("terminating");
	exit(0);
}

static pid_t
start_child(enum slaacd_process p, char *argv0, int fd, int debug, int verbose)
{
	char	*argv[7];
	int	 argc = 0;
	pid_t	 pid;

	switch (pid = fork()) {
	case -1:
		fatal("cannot fork");
	case 0:
		break;
	default:
		close(fd);
		return (pid);
	}

	if (fd != 3) {
		if (dup2(fd, 3) == -1)
			fatal("cannot setup imsg fd");
	} else if (fcntl(fd, F_SETFD, 0) == -1)
		fatal("cannot setup imsg fd");

	argv[argc++] = argv0;
	switch (p) {
	case PROC_MAIN:
		fatalx("Can not start main process");
	case PROC_ENGINE:
		argv[argc++] = "-E";
		break;
	case PROC_FRONTEND:
		argv[argc++] = "-F";
		break;
	}
	if (debug)
		argv[argc++] = "-d";
	if (verbose)
		argv[argc++] = "-v";
	if (verbose > 1)
		argv[argc++] = "-v";
	argv[argc++] = NULL;

	execvp(argv0, argv);
	fatal("execvp");
}

void
main_dispatch_frontend(int fd, short event, void *bula)
{
	struct imsgev		*iev = bula;
	struct imsgbuf		*ibuf;
	struct imsg		 imsg;
	struct imsg_ifinfo	 imsg_ifinfo;
	ssize_t			 n;
	uint32_t		 type;
	int			 shut = 0;
	int			 rdomain;
#ifndef	SMALL
	int			 verbose;
#endif	/* SMALL */

	ibuf = &iev->ibuf;

	if (event & EV_READ) {
		if ((n = imsgbuf_read(ibuf)) == -1)
			fatal("imsgbuf_read error");
		if (n == 0)	/* Connection closed. */
			shut = 1;
	}
	if (event & EV_WRITE) {
		if (imsgbuf_write(ibuf) == -1) {
			if (errno == EPIPE)	/* Connection closed. */
				shut = 1;
			else
				fatal("imsgbuf_write");
		}
	}

	for (;;) {
		if ((n = imsg_get(ibuf, &imsg)) == -1)
			fatal("imsg_get");
		if (n == 0)	/* No more messages. */
			break;

		type = imsg_get_type(&imsg);

		switch (type) {
		case IMSG_OPEN_ICMP6SOCK:
			if (imsg_get_data(&imsg, &rdomain,
			    sizeof(rdomain)) == -1)
				fatalx("%s: invalid %s", __func__, i2s(type));

			open_icmp6sock(rdomain);
			break;
#ifndef	SMALL
		case IMSG_CTL_LOG_VERBOSE:
			if (imsg_get_data(&imsg, &verbose,
			    sizeof(verbose)) == -1)
				fatalx("%s: invalid %s", __func__, i2s(type));

			log_setverbose(verbose);
			break;
#endif	/* SMALL */
		case IMSG_UPDATE_IF:
			if (imsg_get_data(&imsg, &imsg_ifinfo,
			    sizeof(imsg_ifinfo)) == -1)
				fatalx("%s: invalid %s", __func__, i2s(type));

			memcpy(imsg_ifinfo.soiikey, soiikey,
			    SLAACD_SOIIKEY_LEN);
			main_imsg_compose_engine(IMSG_UPDATE_IF, 0,
			    &imsg_ifinfo, sizeof(imsg_ifinfo));
			break;
		default:
			log_debug("%s: error handling imsg %d", __func__, type);
			break;
		}
		imsg_free(&imsg);
	}
	if (!shut)
		imsg_event_add(iev);
	else {
		/* This pipe is dead. Remove its event handler */
		event_del(&iev->ev);
		event_loopexit(NULL);
	}
}

void
main_dispatch_engine(int fd, short event, void *bula)
{
	struct imsgev			*iev = bula;
	struct imsgbuf			*ibuf;
	struct imsg			 imsg;
	struct imsg_configure_address	 address;
	struct imsg_configure_dfr	 dfr;
	struct imsg_propose_rdns	 rdns;
	ssize_t				 n;
	uint32_t			 type;
	int				 shut = 0;

	ibuf = &iev->ibuf;

	if (event & EV_READ) {
		if ((n = imsgbuf_read(ibuf)) == -1)
			fatal("imsgbuf_read error");
		if (n == 0)	/* Connection closed. */
			shut = 1;
	}
	if (event & EV_WRITE) {
		if (imsgbuf_write(ibuf) == -1) {
			if (errno == EPIPE)	/* Connection closed. */
				shut = 1;
			else
				fatal("imsgbuf_write");
		}
	}

	for (;;) {
		if ((n = imsg_get(ibuf, &imsg)) == -1)
			fatal("imsg_get");
		if (n == 0)	/* No more messages. */
			break;

		type = imsg_get_type(&imsg);

		switch (type) {
		case IMSG_CONFIGURE_ADDRESS:
			if (imsg_get_data(&imsg, &address,
			    sizeof(address)) == -1)
				fatalx("%s: invalid %s", __func__, i2s(type));

			configure_interface(&address);
			break;
		case IMSG_WITHDRAW_ADDRESS:
			if (imsg_get_data(&imsg, &address,
			    sizeof(address)) == -1)
				fatalx("%s: invalid %s", __func__, i2s(type));

			delete_address(&address);
			break;
		case IMSG_CONFIGURE_DFR:
			if (imsg_get_data(&imsg, &dfr, sizeof(dfr)) == -1)
				fatalx("%s: invalid %s", __func__, i2s(type));

			add_gateway(&dfr);
			break;
		case IMSG_WITHDRAW_DFR:
			if (imsg_get_data(&imsg, &dfr, sizeof(dfr)) == -1)
				fatalx("%s: invalid %s", __func__, i2s(type));

			delete_gateway(&dfr);
			break;
		case IMSG_PROPOSE_RDNS:
			if (imsg_get_data(&imsg, &rdns, sizeof(rdns)) == -1)
				fatalx("%s: invalid %s", __func__, i2s(type));
			if ((2 + rdns.rdns_count * sizeof(struct in6_addr)) >
			    sizeof(struct sockaddr_rtdns))
				fatalx("%s: rdns_count too big: %d", __func__,
				    rdns.rdns_count);
			if (rdns.rdns_count > MAX_RDNS_COUNT)
				fatalx("%s: rdns_count too big: %d", __func__,
				    rdns.rdns_count);

			send_rdns_proposal(&rdns);
			break;
		default:
			log_debug("%s: error handling imsg %d", __func__, type);
			break;
		}
		imsg_free(&imsg);
	}
	if (!shut)
		imsg_event_add(iev);
	else {
		/* This pipe is dead. Remove its event handler. */
		event_del(&iev->ev);
		event_loopexit(NULL);
	}
}

int
main_imsg_compose_frontend(int type, int fd, void *data, uint16_t datalen)
{
	if (iev_frontend)
		return (imsg_compose_event(iev_frontend, type, 0, 0, fd, data,
		    datalen));
	else
		return (-1);
}

int
main_imsg_compose_engine(int type, pid_t pid, void *data, uint16_t datalen)
{
	if (iev_engine)
		return(imsg_compose_event(iev_engine, type, 0, pid, -1, data,
		    datalen));
	else
		return (-1);
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
imsg_compose_event(struct imsgev *iev, uint16_t type, uint32_t peerid,
    pid_t pid, int fd, void *data, uint16_t datalen)
{
	int	ret;

	if ((ret = imsg_compose(&iev->ibuf, type, peerid, pid, fd, data,
	    datalen)) != -1)
		imsg_event_add(iev);

	return (ret);
}

int
imsg_forward_event(struct imsgev *iev, struct imsg *imsg)
{
	int	ret;

	if ((ret = imsg_forward(&iev->ibuf, imsg)) != -1)
		imsg_event_add(iev);

	return (ret);
}
static int
main_imsg_send_ipc_sockets(struct imsgbuf *frontend_buf,
    struct imsgbuf *engine_buf)
{
	int pipe_frontend2engine[2];

	if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK,
	    PF_UNSPEC, pipe_frontend2engine) == -1)
		return (-1);

	if (imsg_compose(frontend_buf, IMSG_SOCKET_IPC, 0, 0,
	    pipe_frontend2engine[0], NULL, 0) == -1)
		return (-1);
	imsgbuf_flush(frontend_buf);
	if (imsg_compose(engine_buf, IMSG_SOCKET_IPC, 0, 0,
	    pipe_frontend2engine[1], NULL, 0) == -1)
		return (-1);
	imsgbuf_flush(engine_buf);
	return (0);
}

void
configure_interface(struct imsg_configure_address *address)
{

	struct in6_aliasreq	 in6_addreq;
	time_t			 t;
	char			*if_name;

	memset(&in6_addreq, 0, sizeof(in6_addreq));

	if_name = if_indextoname(address->if_index, in6_addreq.ifra_name);
	if (if_name == NULL) {
		log_warnx("%s: cannot find interface %d", __func__,
		    address->if_index);
		return;
	}

	memcpy(&in6_addreq.ifra_addr, &address->addr,
	    sizeof(in6_addreq.ifra_addr));
	memcpy(&in6_addreq.ifra_dstaddr, &address->gw,
	    sizeof(in6_addreq.ifra_dstaddr));
	memcpy(&in6_addreq.ifra_prefixmask.sin6_addr, &address->mask,
	    sizeof(in6_addreq.ifra_prefixmask.sin6_addr));
	in6_addreq.ifra_prefixmask.sin6_family = AF_INET6;
	in6_addreq.ifra_prefixmask.sin6_len =
	    sizeof(in6_addreq.ifra_prefixmask);

	t = time(NULL);

	in6_addreq.ifra_lifetime.ia6t_expire = t + address->vltime;
	in6_addreq.ifra_lifetime.ia6t_vltime = address->vltime;

	in6_addreq.ifra_lifetime.ia6t_preferred = t + address->pltime;
	in6_addreq.ifra_lifetime.ia6t_pltime = address->pltime;

	in6_addreq.ifra_flags |= IN6_IFF_AUTOCONF;

	if (address->temporary)
		in6_addreq.ifra_flags |= IN6_IFF_TEMPORARY;

	log_debug("%s: %s", __func__, if_name);

	if (ioctl(ioctl_sock, SIOCAIFADDR_IN6, &in6_addreq) == -1)
		log_warn("SIOCAIFADDR_IN6");

	if (address->mtu) {
		struct ifreq	 ifr;

		strlcpy(ifr.ifr_name, in6_addreq.ifra_name,
		    sizeof(ifr.ifr_name));
		ifr.ifr_mtu = address->mtu;
		log_debug("Setting MTU to %d", ifr.ifr_mtu);

		if (ioctl(ioctl_sock, SIOCSIFMTU, &ifr) == -1)
		    log_warn("failed to set MTU");
	}
}

void
delete_address(struct imsg_configure_address *address)
{

	struct in6_ifreq	 in6_ridreq;
	char			*if_name;

	memset(&in6_ridreq, 0, sizeof(in6_ridreq));

	if_name = if_indextoname(address->if_index, in6_ridreq.ifr_name);
	if (if_name == NULL) {
		log_warnx("%s: cannot find interface %d", __func__,
		    address->if_index);
		return;
	}

	memcpy(&in6_ridreq.ifr_addr, &address->addr,
	    sizeof(in6_ridreq.ifr_addr));

	log_debug("%s: %s", __func__, if_name);

	if (ioctl(ioctl_sock, SIOCDIFADDR_IN6, &in6_ridreq) == -1 &&
	    errno != EADDRNOTAVAIL)
		log_warn("%s: cannot remove address", __func__);

}

#define	ROUNDUP(a)							\
    (((a) & (sizeof(long) - 1)) ? (1 + ((a) | (sizeof(long) - 1))) : (a))

void
configure_gateway(struct imsg_configure_dfr *dfr, uint8_t rtm_type)
{
	struct rt_msghdr		 rtm;
	struct sockaddr_rtlabel		 rl;
	struct sockaddr_in6		 dst, gw, mask;
	struct iovec			 iov[10];
	long				 pad = 0;
	int				 iovcnt = 0, padlen;

	memset(&rtm, 0, sizeof(rtm));

	rtm.rtm_version = RTM_VERSION;
	rtm.rtm_type = rtm_type;
	rtm.rtm_msglen = sizeof(rtm);
	rtm.rtm_tableid = dfr->rdomain;
	rtm.rtm_index = dfr->if_index;
	rtm.rtm_seq = ++rtm_seq;
	rtm.rtm_priority = RTP_NONE;
	rtm.rtm_addrs = RTA_DST | RTA_GATEWAY | RTA_NETMASK | RTA_LABEL;
	rtm.rtm_flags = RTF_UP | RTF_GATEWAY | RTF_STATIC | RTF_MPATH;

	iov[iovcnt].iov_base = &rtm;
	iov[iovcnt++].iov_len = sizeof(rtm);

	memset(&dst, 0, sizeof(mask));
	dst.sin6_family = AF_INET6;
	dst.sin6_len = sizeof(struct sockaddr_in6);

	iov[iovcnt].iov_base = &dst;
	iov[iovcnt++].iov_len = sizeof(dst);
	rtm.rtm_msglen += sizeof(dst);
	padlen = ROUNDUP(sizeof(dst)) - sizeof(dst);
	if (padlen > 0) {
		iov[iovcnt].iov_base = &pad;
		iov[iovcnt++].iov_len = padlen;
		rtm.rtm_msglen += padlen;
	}

	memcpy(&gw, &dfr->addr, sizeof(gw));
#ifdef __KAME__
	/* from route(8) getaddr()*/
	*(u_int16_t *)& gw.sin6_addr.s6_addr[2] = htons(gw.sin6_scope_id);
	gw.sin6_scope_id = 0;
#endif
	iov[iovcnt].iov_base = &gw;
	iov[iovcnt++].iov_len = sizeof(gw);
	rtm.rtm_msglen += sizeof(gw);
	padlen = ROUNDUP(sizeof(gw)) - sizeof(gw);
	if (padlen > 0) {
		iov[iovcnt].iov_base = &pad;
		iov[iovcnt++].iov_len = padlen;
		rtm.rtm_msglen += padlen;
	}

	memset(&mask, 0, sizeof(mask));
	mask.sin6_family = AF_INET6;
	mask.sin6_len = sizeof(struct sockaddr_in6);
	iov[iovcnt].iov_base = &mask;
	iov[iovcnt++].iov_len = sizeof(mask);
	rtm.rtm_msglen += sizeof(mask);
	padlen = ROUNDUP(sizeof(mask)) - sizeof(mask);
	if (padlen > 0) {
		iov[iovcnt].iov_base = &pad;
		iov[iovcnt++].iov_len = padlen;
		rtm.rtm_msglen += padlen;
	}

	memset(&rl, 0, sizeof(rl));
	rl.sr_len = sizeof(rl);
	rl.sr_family = AF_UNSPEC;
	(void)snprintf(rl.sr_label, sizeof(rl.sr_label), "%s",
	    SLAACD_RTA_LABEL);
	iov[iovcnt].iov_base = &rl;
	iov[iovcnt++].iov_len = sizeof(rl);
	rtm.rtm_msglen += sizeof(rl);
	padlen = ROUNDUP(sizeof(rl)) - sizeof(rl);
	if (padlen > 0) {
		iov[iovcnt].iov_base = &pad;
		iov[iovcnt++].iov_len = padlen;
		rtm.rtm_msglen += padlen;
	}

	if (writev(routesock, iov, iovcnt) == -1)
		log_warn("failed to send route message");
}

void
add_gateway(struct imsg_configure_dfr *dfr)
{
	configure_gateway(dfr, RTM_ADD);
}

void
delete_gateway(struct imsg_configure_dfr *dfr)
{
	configure_gateway(dfr, RTM_DELETE);
}

void
send_rdns_proposal(struct imsg_propose_rdns *rdns)
{
	struct rt_msghdr		 rtm;
	struct sockaddr_rtdns		 rtdns;
	struct iovec			 iov[3];
	long				 pad = 0;
	int				 iovcnt = 0, padlen;

	memset(&rtm, 0, sizeof(rtm));

	rtm.rtm_version = RTM_VERSION;
	rtm.rtm_type = RTM_PROPOSAL;
	rtm.rtm_msglen = sizeof(rtm);
	rtm.rtm_tableid = rdns->rdomain;
	rtm.rtm_index = rdns->if_index;
	rtm.rtm_seq = ++rtm_seq;
	rtm.rtm_priority = RTP_PROPOSAL_SLAAC;
	rtm.rtm_addrs = RTA_DNS;
	rtm.rtm_flags = RTF_UP;

	iov[iovcnt].iov_base = &rtm;
	iov[iovcnt++].iov_len = sizeof(rtm);

	memset(&rtdns, 0, sizeof(rtdns));
	rtdns.sr_family = AF_INET6;
	rtdns.sr_len = 2 + rdns->rdns_count * sizeof(struct in6_addr);
	memcpy(rtdns.sr_dns, rdns->rdns, sizeof(rtdns.sr_dns));

	iov[iovcnt].iov_base = &rtdns;
	iov[iovcnt++].iov_len = sizeof(rtdns);
	rtm.rtm_msglen += sizeof(rtdns);
	padlen = ROUNDUP(sizeof(rtdns)) - sizeof(rtdns);
	if (padlen > 0) {
		iov[iovcnt].iov_base = &pad;
		iov[iovcnt++].iov_len = padlen;
		rtm.rtm_msglen += padlen;
	}

	if (writev(routesock, iov, iovcnt) == -1)
		log_warn("failed to send route message");
}

#ifndef	SMALL
const char*
sin6_to_str(struct sockaddr_in6 *sin6)
{
	static char hbuf[NI_MAXHOST];
	int error;

	error = getnameinfo((struct sockaddr *)sin6, sin6->sin6_len, hbuf,
	    sizeof(hbuf), NULL, 0, NI_NUMERICHOST | NI_NUMERICSERV);
	if (error) {
		log_warnx("%s", gai_strerror(error));
		strlcpy(hbuf, "unknown", sizeof(hbuf));
	}
	return hbuf;
}

int
parse_hex_char(char ch)
{
	if (ch >= '0' && ch <= '9')
		return (ch - '0');

	ch = tolower((unsigned char)ch);
	if (ch >= 'a' && ch <= 'f')
		return (ch - 'a' + 10);

	return (-1);
}

ssize_t
parse_hex_string(unsigned char *dst, size_t dstlen, const char *src)
{
	size_t len = 0;
	int digit;

	memset(dst, 0, dstlen);
	while (len < dstlen) {
		if (*src == '\0')
			return (len);

		digit = parse_hex_char(*src++);
		if (digit == -1)
			return (-1);
		dst[len] = digit << 4;

		digit = parse_hex_char(*src++);
		if (digit == -1)
			return (-1);

		dst[len] |= digit;
		len++;
	}

	while (*src != '\0') {
		if (parse_hex_char(*src++) == -1 ||
		    parse_hex_char(*src++) == -1)
			return (-1);

		len++;
	}

	return (len);
}

void
read_soiikey(void)
{
	int	 fd = -1;
	char	 buf[33];

	if ((fd = open("/etc/soii.key", O_RDONLY)) == -1)
		goto err;
	memset(buf, 0, sizeof(buf));
	if (read(fd, buf, sizeof(buf) - 1) == -1)
		goto err;
	close(fd);
	fd = -1;
	if (parse_hex_string(soiikey, sizeof(soiikey), buf) == -1)
		goto err;
	return;
 err:
	memset(soiikey, 0, sizeof(soiikey));
	if (fd != -1)
		close(fd);
}

#endif	/* SMALL */

void
open_icmp6sock(int rdomain)
{
	int			 icmp6sock, on = 1;

	log_debug("%s: %d", __func__, rdomain);

	if ((icmp6sock = socket(AF_INET6, SOCK_RAW | SOCK_CLOEXEC,
	    IPPROTO_ICMPV6)) == -1)
		fatal("ICMPv6 socket");

	if (setsockopt(icmp6sock, IPPROTO_IPV6, IPV6_RECVPKTINFO, &on,
	    sizeof(on)) == -1)
		fatal("IPV6_RECVPKTINFO");

	if (setsockopt(icmp6sock, IPPROTO_IPV6, IPV6_RECVHOPLIMIT, &on,
	    sizeof(on)) == -1)
		fatal("IPV6_RECVHOPLIMIT");

	if (setsockopt(icmp6sock, SOL_SOCKET, SO_RTABLE, &rdomain,
	    sizeof(rdomain)) == -1) {
		/* we might race against removal of the rdomain */
		log_warn("setsockopt SO_RTABLE");
		close(icmp6sock);
		return;
	}

	main_imsg_compose_frontend(IMSG_ICMP6SOCK, icmp6sock, &rdomain,
	    sizeof(rdomain));
}

#ifndef	SMALL

#define	I2S(x) case x: return #x

const char*
i2s(uint32_t type)
{
	static char	unknown[sizeof("IMSG_4294967295")];

	switch (type) {
	I2S(IMSG_NONE);
	I2S(IMSG_CTL_LOG_VERBOSE);
	I2S(IMSG_CTL_SHOW_INTERFACE_INFO);
	I2S(IMSG_CTL_SHOW_INTERFACE_INFO_RA);
	I2S(IMSG_CTL_SHOW_INTERFACE_INFO_RA_PREFIX);
	I2S(IMSG_CTL_SHOW_INTERFACE_INFO_RA_RDNS);
	I2S(IMSG_CTL_SHOW_INTERFACE_INFO_ADDR_PROPOSALS);
	I2S(IMSG_CTL_SHOW_INTERFACE_INFO_ADDR_PROPOSAL);
	I2S(IMSG_CTL_SHOW_INTERFACE_INFO_DFR_PROPOSALS);
	I2S(IMSG_CTL_SHOW_INTERFACE_INFO_DFR_PROPOSAL);
	I2S(IMSG_CTL_SHOW_INTERFACE_INFO_RDNS_PROPOSALS);
	I2S(IMSG_CTL_SHOW_INTERFACE_INFO_RDNS_PROPOSAL);
	I2S(IMSG_CTL_END);
	I2S(IMSG_PROPOSE_RDNS);
	I2S(IMSG_REPROPOSE_RDNS);
	I2S(IMSG_CTL_SEND_SOLICITATION);
	I2S(IMSG_SOCKET_IPC);
	I2S(IMSG_OPEN_ICMP6SOCK);
	I2S(IMSG_ICMP6SOCK);
	I2S(IMSG_ROUTESOCK);
	I2S(IMSG_CONTROLFD);
	I2S(IMSG_STARTUP);
	I2S(IMSG_UPDATE_IF);
	I2S(IMSG_REMOVE_IF);
	I2S(IMSG_RA);
	I2S(IMSG_CONFIGURE_ADDRESS);
	I2S(IMSG_WITHDRAW_ADDRESS);
	I2S(IMSG_DEL_ADDRESS);
	I2S(IMSG_DEL_ROUTE);
	I2S(IMSG_CONFIGURE_DFR);
	I2S(IMSG_WITHDRAW_DFR);
	I2S(IMSG_DUP_ADDRESS);
	default:
		snprintf(unknown, sizeof(unknown), "IMSG_%u", type);
		return unknown;
	}
}
#undef	I2S

#endif	/* SMALL */
