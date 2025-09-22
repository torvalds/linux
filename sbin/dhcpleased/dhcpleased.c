/*	$OpenBSD: dhcpleased.c,v 1.41 2025/04/26 17:58:02 florian Exp $	*/

/*
 * Copyright (c) 2017, 2021 Florian Obser <florian@openbsd.org>
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
#include <sys/stat.h>
#include <sys/syslog.h>
#include <sys/sysctl.h>
#include <sys/uio.h>
#include <sys/wait.h>

#include <net/if.h>
#include <net/route.h>
#include <net/if_dl.h>
#include <netinet/in.h>
#include <netinet/if_ether.h>
#include <netinet/in_var.h>

#include <arpa/inet.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <event.h>
#include <ifaddrs.h>
#include <imsg.h>
#include <netdb.h>
#include <pwd.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

#include "bpf.h"
#include "log.h"
#include "dhcpleased.h"
#include "frontend.h"
#include "engine.h"
#include "control.h"

enum dhcpleased_process {
	PROC_MAIN,
	PROC_ENGINE,
	PROC_FRONTEND
};

__dead void	usage(void);
__dead void	main_shutdown(void);

void	main_sig_handler(int, short, void *);

static pid_t	start_child(enum dhcpleased_process, char *, int, int, int);

void	 main_dispatch_frontend(int, short, void *);
void	 main_dispatch_engine(int, short, void *);
void	 open_bpfsock(uint32_t);
void	 configure_interface(struct imsg_configure_interface *);
void	 deconfigure_interface(struct imsg_configure_interface *);
void	 propose_rdns(struct imsg_propose_rdns *);
void	 configure_routes(uint8_t, struct imsg_configure_interface *);
void	 configure_route(uint8_t, uint32_t, int, struct sockaddr_in *, struct
	     sockaddr_in *, struct sockaddr_in *, struct sockaddr_in *, int);
void	 read_lease_file(struct imsg_ifinfo *);

static int	main_imsg_send_ipc_sockets(struct imsgbuf *, struct imsgbuf *);
int		main_imsg_compose_frontend(int, int, void *, uint16_t);
int		main_imsg_compose_engine(int, int, void *, uint16_t);

#ifndef SMALL
int		main_imsg_send_config(struct dhcpleased_conf *);
#endif /* SMALL */
int	main_reload(void);

static struct imsgev	*iev_frontend;
static struct imsgev	*iev_engine;

#ifndef SMALL
struct dhcpleased_conf	*main_conf;
#endif
const char		 default_conffile[] = _PATH_CONF_FILE;
const char		*conffile = default_conffile;
pid_t			 frontend_pid;
pid_t			 engine_pid;

int			 routesock, ioctl_sock, rtm_seq, no_lease_files;

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
	case SIGHUP:
#ifndef SMALL
		if (main_reload() == -1)
			log_warnx("configuration reload failed");
		else
			log_debug("configuration reloaded");
#endif /* SMALL */
		break;
	default:
		fatalx("unexpected signal");
	}
}

__dead void
usage(void)
{
	extern char *__progname;

	fprintf(stderr, "usage: %s [-dnv] [-f file] [-s socket]\n",
	    __progname);
	exit(1);
}

int
main(int argc, char *argv[])
{
	struct event		 ev_sigint, ev_sigterm, ev_sighup;
	int			 ch;
	int			 debug = 0, engine_flag = 0, frontend_flag = 0;
	int			 verbose = 0, no_action = 0;
	char			*saved_argv0;
	int			 pipe_main2frontend[2];
	int			 pipe_main2engine[2];
	int			 frontend_routesock, rtfilter, lockfd;
	int			 rtable_any = RTABLE_ANY;
	char			*csock = _PATH_DHCPLEASED_SOCKET;
#ifndef SMALL
	int			 control_fd;
#endif /* SMALL */

	log_init(1, LOG_DAEMON);	/* Log to stderr until daemonized. */
	log_setverbose(1);

	saved_argv0 = argv[0];
	if (saved_argv0 == NULL)
		saved_argv0 = "dhcpleased";

	while ((ch = getopt(argc, argv, "dEFf:ns:v")) != -1) {
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
		case 'f':
			conffile = optarg;
			break;
		case 'n':
			no_action = 1;
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

#ifndef SMALL
	/* parse config file */
	if ((main_conf = parse_config(conffile)) == NULL)
		exit(1);

	if (no_action) {
		if (verbose)
			print_config(main_conf);
		else
			fprintf(stderr, "configuration OK\n");
		exit(0);
	}
#endif /* SMALL */

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
	if (getpwnam(DHCPLEASED_USER) == NULL)
		errx(1, "unknown user %s", DHCPLEASED_USER);

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
	    SOCK_NONBLOCK, AF_INET)) == -1)
		fatal("route socket");
	shutdown(routesock, SHUT_RD);

	event_init();

	/* Setup signal handler. */
	signal_set(&ev_sigint, SIGINT, main_sig_handler, NULL);
	signal_set(&ev_sigterm, SIGTERM, main_sig_handler, NULL);
	signal_set(&ev_sighup, SIGHUP, main_sig_handler, NULL);
	signal_add(&ev_sigint, NULL);
	signal_add(&ev_sigterm, NULL);
	signal_add(&ev_sighup, NULL);
	signal(SIGPIPE, SIG_IGN);

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

	if ((ioctl_sock = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0)) == -1)
		fatal("socket");

	if ((frontend_routesock = socket(AF_ROUTE, SOCK_RAW | SOCK_CLOEXEC,
	    AF_INET)) == -1)
		fatal("route socket");

	rtfilter = ROUTE_FILTER(RTM_IFINFO) | ROUTE_FILTER(RTM_PROPOSAL) |
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
#endif /* SMALL */

	if (unveil(conffile, "r") == -1)
		fatal("unveil %s", conffile);
	if (unveil("/dev/bpf", "rw") == -1)
		fatal("unveil /dev/bpf");

	if (unveil(_PATH_LEASE, "rwc") == -1) {
		no_lease_files = 1;
		log_warn("disabling lease files, unveil " _PATH_LEASE);
	}

	if (unveil(NULL, NULL) == -1)
		fatal("unveil");
#if notyet
	if (pledge("stdio inet rpath wpath sendfd wroute bpf", NULL) == -1)
		fatal("pledge");
#endif
	main_imsg_compose_frontend(IMSG_ROUTESOCK, frontend_routesock, NULL, 0);

#ifndef SMALL
	if (control_fd != -1)
		main_imsg_compose_frontend(IMSG_CONTROLFD, control_fd, NULL, 0);
	main_imsg_send_config(main_conf);
#endif /* SMALL */

	main_imsg_compose_frontend(IMSG_STARTUP, -1, NULL, 0);

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

#ifndef SMALL
	config_clear(main_conf);
#endif /* SMALL */

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
start_child(enum dhcpleased_process p, char *argv0, int fd, int debug, int
    verbose)
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
	int			 shut = 0;
	uint32_t		 if_index, type;
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
		case IMSG_OPEN_BPFSOCK:
			if (imsg_get_data(&imsg, &if_index,
			    sizeof(if_index)) == -1)
				fatalx("%s: invalid %s", __func__, i2s(type));

			open_bpfsock(if_index);
			break;
#ifndef	SMALL
		case IMSG_CTL_RELOAD:
			if (main_reload() == -1)
				log_warnx("configuration reload failed");
			else
				log_warnx("configuration reloaded");
			break;
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

			read_lease_file(&imsg_ifinfo);
			main_imsg_compose_engine(IMSG_UPDATE_IF, -1,
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
		case IMSG_CONFIGURE_INTERFACE: {
			struct imsg_configure_interface imsg_interface;

			if (imsg_get_data(&imsg, &imsg_interface,
			    sizeof(imsg_interface)) == -1)
				fatalx("%s: invalid %s", __func__, i2s(type));
			if (imsg_interface.file[
			    sizeof(imsg_interface.file) - 1] != '\0')
				fatalx("%s: invalid %s", __func__, i2s(type));
			if (imsg_interface.domainname[
			    sizeof(imsg_interface.domainname) - 1] != '\0')
				fatalx("%s: invalid %s", __func__, i2s(type));
			if (imsg_interface.hostname[
			    sizeof(imsg_interface.hostname) - 1] != '\0')
				fatalx("%s: invalid %s", __func__, i2s(type));
			if (imsg_interface.routes_len >= MAX_DHCP_ROUTES)
				fatalx("%s: too many routes in imsg", __func__);

			configure_interface(&imsg_interface);
			break;
		}
		case IMSG_DECONFIGURE_INTERFACE: {
			struct imsg_configure_interface imsg_interface;

			if (imsg_get_data(&imsg, &imsg_interface,
			    sizeof(imsg_interface)) == -1)
				fatalx("%s: invalid %s", __func__, i2s(type));
			if (imsg_interface.file[
			    sizeof(imsg_interface.file) - 1] != '\0')
				fatalx("%s: invalid %s", __func__, i2s(type));
			if (imsg_interface.domainname[
			    sizeof(imsg_interface.domainname) - 1] != '\0')
				fatalx("%s: invalid %s", __func__, i2s(type));
			if (imsg_interface.hostname[
			    sizeof(imsg_interface.hostname) - 1] != '\0')
				fatalx("%s: invalid %s", __func__, i2s(type));
			if (imsg_interface.routes_len >= MAX_DHCP_ROUTES)
				fatalx("%s: too many routes in imsg", __func__);

			deconfigure_interface(&imsg_interface);
			main_imsg_compose_frontend(IMSG_CLOSE_UDPSOCK, -1,
			    &imsg_interface.if_index,
			    sizeof(imsg_interface.if_index));
			break;
		}
		case IMSG_WITHDRAW_ROUTES: {
			struct imsg_configure_interface imsg_interface;

			if (imsg_get_data(&imsg, &imsg_interface,
			    sizeof(imsg_interface)) == -1)
				fatalx("%s: invalid %s", __func__, i2s(type));
			if (imsg_interface.file[
			    sizeof(imsg_interface.file) - 1] != '\0')
				fatalx("%s: invalid %s", __func__, i2s(type));
			if (imsg_interface.domainname[
			    sizeof(imsg_interface.domainname) - 1] != '\0')
				fatalx("%s: invalid %s", __func__, i2s(type));
			if (imsg_interface.hostname[
			    sizeof(imsg_interface.hostname) - 1] != '\0')
				fatalx("%s: invalid %s", __func__, i2s(type));
			if (imsg_interface.routes_len >= MAX_DHCP_ROUTES)
				fatalx("%s: too many routes in imsg", __func__);

			if (imsg_interface.routes_len > 0)
				configure_routes(RTM_DELETE, &imsg_interface);
			break;
		}
		case IMSG_PROPOSE_RDNS: {
			struct imsg_propose_rdns	 rdns;

			if (imsg_get_data(&imsg, &rdns, sizeof(rdns)) == -1)
				fatalx("%s: invalid %s", __func__, i2s(type));
			if ((2 + rdns.rdns_count * sizeof(struct in_addr)) >
			    sizeof(struct sockaddr_rtdns))
				fatalx("%s: rdns_count too big: %d", __func__,
				    rdns.rdns_count);
			if (rdns.rdns_count > MAX_RDNS_COUNT)
				fatalx("%s: rdns_count too big: %d", __func__,
				    rdns.rdns_count);

			propose_rdns(&rdns);
			break;
		}
		case IMSG_WITHDRAW_RDNS: {
			struct imsg_propose_rdns	 rdns;

			if (imsg_get_data(&imsg, &rdns, sizeof(rdns)) == -1)
				fatalx("%s: invalid %s", __func__, i2s(type));
			if (rdns.rdns_count != 0)
				fatalx("%s: expected rdns_count == 0: %d",
				    __func__, rdns.rdns_count);

			propose_rdns(&rdns);
			break;
		}
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
main_imsg_compose_engine(int type, int fd, void *data, uint16_t datalen)
{
	if (iev_engine)
		return(imsg_compose_event(iev_engine, type, 0, 0, fd, data,
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

#ifndef SMALL
int
main_reload(void)
{
	struct dhcpleased_conf *xconf;

	if ((xconf = parse_config(conffile)) == NULL)
		return (-1);

	if (main_imsg_send_config(xconf) == -1)
		return (-1);

	merge_config(main_conf, xconf);

	return (0);
}

int
main_imsg_send_config(struct dhcpleased_conf *xconf)
{
	struct iface_conf	*iface_conf;

	main_imsg_compose_frontend(IMSG_RECONF_CONF, -1, NULL, 0);
	main_imsg_compose_engine(IMSG_RECONF_CONF, -1, NULL, 0);

	/* Send the interface list to the frontend & engine. */
	SIMPLEQ_FOREACH(iface_conf, &xconf->iface_list, entry) {
		main_imsg_compose_frontend(IMSG_RECONF_IFACE, -1, iface_conf,
		    sizeof(*iface_conf));
		main_imsg_compose_engine(IMSG_RECONF_IFACE, -1, iface_conf,
		    sizeof(*iface_conf));
		if (iface_conf->vc_id_len) {
			main_imsg_compose_frontend(IMSG_RECONF_VC_ID, -1,
			    iface_conf->vc_id, iface_conf->vc_id_len);
			main_imsg_compose_engine(IMSG_RECONF_VC_ID, -1,
			    iface_conf->vc_id, iface_conf->vc_id_len);
		}
		if (iface_conf->c_id_len) {
			main_imsg_compose_frontend(IMSG_RECONF_C_ID, -1,
			    iface_conf->c_id, iface_conf->c_id_len);
			main_imsg_compose_engine(IMSG_RECONF_C_ID, -1,
			    iface_conf->c_id, iface_conf->c_id_len);
		}
		if (iface_conf->h_name != NULL)
			main_imsg_compose_frontend(IMSG_RECONF_H_NAME, -1,
			    iface_conf->h_name, strlen(iface_conf->h_name) + 1);
	}

	/* Config is now complete. */
	main_imsg_compose_frontend(IMSG_RECONF_END, -1, NULL, 0);
	main_imsg_compose_engine(IMSG_RECONF_END, -1, NULL, 0);

	return (0);
}
#endif /* SMALL */

void
configure_interface(struct imsg_configure_interface *imsg)
{
	struct ifaliasreq	 ifaliasreq;
	struct ifaddrs		*ifap, *ifa;
	struct sockaddr_in	*req_sin_addr, *req_sin_mask;
	int			 found = 0, udpsock, opt = 1, len, fd = -1;
	char			*if_name;
	char			 ip_ntop_buf[INET_ADDRSTRLEN];
	char			 nextserver_ntop_buf[INET_ADDRSTRLEN];
	char			 lease_buf[LEASE_SIZE];
	char			 lease_file_buf[sizeof(_PATH_LEASE) +
	    IF_NAMESIZE];
	char			 tmpl[] = _PATH_LEASE"XXXXXXXXXX";

	memset(&ifaliasreq, 0, sizeof(ifaliasreq));

	if_name = if_indextoname(imsg->if_index, ifaliasreq.ifra_name);
	if (if_name == NULL) {
		log_warnx("%s: cannot find interface %d", __func__,
		    imsg->if_index);
		return;
	}

	log_debug("%s %s", __func__, if_name);

	if (getifaddrs(&ifap) != 0)
		fatal("getifaddrs");

	req_sin_addr = (struct sockaddr_in *)&ifaliasreq.ifra_addr;
	req_sin_addr->sin_family = AF_INET;
	req_sin_addr->sin_len = sizeof(*req_sin_addr);

	for (ifa = ifap; ifa != NULL; ifa = ifa->ifa_next) {
		struct in_addr	 addr, mask;

		if (strcmp(if_name, ifa->ifa_name) != 0)
			continue;
		if (ifa->ifa_addr == NULL)
			continue;
		if (ifa->ifa_addr->sa_family != AF_INET)
			continue;

		addr = ((struct sockaddr_in *)ifa->ifa_addr)->sin_addr;
		mask = ((struct sockaddr_in *)ifa->ifa_netmask)->sin_addr;

		if (imsg->addr.s_addr == addr.s_addr) {
			if (imsg->mask.s_addr == mask.s_addr)
				found = 1;
			else {
				req_sin_addr->sin_addr = addr;
				if (ioctl(ioctl_sock, SIOCDIFADDR, &ifaliasreq)
				    == -1) {
					if (errno != EADDRNOTAVAIL)
						log_warn("SIOCDIFADDR");
				}
			}
			break;
		}
	}
	freeifaddrs(ifap);

	req_sin_addr->sin_addr = imsg->addr;
	if (!found) {
		req_sin_mask = (struct sockaddr_in *)&ifaliasreq.ifra_mask;
		req_sin_mask->sin_family = AF_INET;
		req_sin_mask->sin_len = sizeof(*req_sin_mask);
		req_sin_mask->sin_addr = imsg->mask;
		if (ioctl(ioctl_sock, SIOCAIFADDR, &ifaliasreq) == -1)
			log_warn("SIOCAIFADDR");
	}
	if (imsg->routes_len > 0)
		configure_routes(RTM_ADD, imsg);

	req_sin_addr->sin_port = ntohs(CLIENT_PORT);
	if ((udpsock = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
		log_warn("socket");
		return;
	}
	if (setsockopt(udpsock, SOL_SOCKET, SO_REUSEADDR, &opt,
	    sizeof(opt)) == -1)
		log_warn("setting SO_REUSEADDR on socket");

	if (setsockopt(udpsock, SOL_SOCKET, SO_RTABLE, &imsg->rdomain,
	    sizeof(imsg->rdomain)) == -1) {
		/* we might race against removal of the rdomain */
		log_warn("setsockopt SO_RTABLE");
		close(udpsock);
		return;
	}

	if (bind(udpsock, (struct sockaddr *)req_sin_addr,
	    sizeof(*req_sin_addr)) == -1) {
		close(udpsock);
		return;
	}

	shutdown(udpsock, SHUT_RD);

	main_imsg_compose_frontend(IMSG_UDPSOCK, udpsock,
	    &imsg->if_index, sizeof(imsg->if_index));

	if (no_lease_files)
		return;

	if (inet_ntop(AF_INET, &imsg->addr, ip_ntop_buf, sizeof(ip_ntop_buf)) ==
	    NULL) {
		log_warn("%s: inet_ntop", __func__);
		return;
	}

	if (imsg->siaddr.s_addr == INADDR_ANY)
		nextserver_ntop_buf[0] = '\0';
	else {
		if (inet_ntop(AF_INET, &imsg->siaddr, nextserver_ntop_buf,
		    sizeof(nextserver_ntop_buf)) == NULL) {
			log_warn("%s: inet_ntop", __func__);
			return;
		}
	}
	len = snprintf(lease_file_buf, sizeof(lease_file_buf), "%s%s",
	    _PATH_LEASE, if_name);
	if ( len == -1 || (size_t) len >= sizeof(lease_file_buf)) {
		log_warnx("%s: failed to encode lease path for %s", __func__,
		    if_name);
		return;
	}

	len = snprintf(lease_buf, sizeof(lease_buf),
	    "%s\n%s%s\n%s%s\n%s%s\n%s%s\n%s%s\n",
	    LEASE_VERSION, LEASE_IP_PREFIX, ip_ntop_buf,
	    LEASE_NEXTSERVER_PREFIX, nextserver_ntop_buf, LEASE_BOOTFILE_PREFIX,
	    imsg->file, LEASE_HOSTNAME_PREFIX, imsg->hostname,
	    LEASE_DOMAIN_PREFIX, imsg->domainname);
	if ( len == -1 || (size_t) len >= sizeof(lease_buf)) {
		log_warnx("%s: failed to encode lease for %s", __func__,
		    ip_ntop_buf);
		return;
	}

	if ((fd = mkstemp(tmpl)) == -1) {
		log_warn("%s: mkstemp", __func__);
		return;
	}

	if (write(fd, lease_buf, len) < len)
		goto err;

	if (fchmod(fd, 0644) == -1)
		goto err;

	if (close(fd) == -1)
		goto err;
	fd = -1;

	if (rename(tmpl, lease_file_buf) == -1)
		goto err;
	return;
 err:
	log_warn("%s", __func__);
	if (fd != -1)
		close(fd);
	unlink(tmpl);
}

void
deconfigure_interface(struct imsg_configure_interface *imsg)
{
	struct ifaliasreq	 ifaliasreq;
	struct sockaddr_in	*req_sin_addr;

	memset(&ifaliasreq, 0, sizeof(ifaliasreq));

#if 0
	/*
	 * When two default routes have the same gateway the kernel always
	 * deletes the first which might be the wrong one. When we then
	 * deconfigure the IP address from the interface the kernel deletes
	 * all routes pointing out that interface and we end up with no
	 * default.
	 * This can happen with a wired & wireless interface on the same
	 * layer 2 network and the user issues ifconfig $WIFI inet -autoconf.
	 * Work around this issue by not deleting the default route and let
	 * the kernel handle it when we remove the IP address a few lines
	 * down.
	 */
	if (imsg->routes_len > 0)
		configure_routes(RTM_DELETE, imsg);
#endif

	if (if_indextoname(imsg->if_index, ifaliasreq.ifra_name) == NULL) {
		log_warnx("%s: cannot find interface %d", __func__,
		    imsg->if_index);
		return;
	}

	log_debug("%s %s", __func__, ifaliasreq.ifra_name);

	req_sin_addr = (struct sockaddr_in *)&ifaliasreq.ifra_addr;
	req_sin_addr->sin_family = AF_INET;
	req_sin_addr->sin_len = sizeof(*req_sin_addr);

	req_sin_addr->sin_addr = imsg->addr;
	if (ioctl(ioctl_sock, SIOCDIFADDR, &ifaliasreq) == -1) {
		if (errno != EADDRNOTAVAIL)
			log_warn("SIOCDIFADDR");
	}
}

void
configure_routes(uint8_t rtm_type, struct imsg_configure_interface *imsg)
{
	struct sockaddr_in	 dst, mask, gw, ifa;
	in_addr_t		 addrnet, gwnet;
	int			 i;

	memset(&ifa, 0, sizeof(ifa));
	ifa.sin_family = AF_INET;
	ifa.sin_len = sizeof(ifa);
	ifa.sin_addr = imsg->addr;

	memset(&dst, 0, sizeof(dst));
	dst.sin_family = AF_INET;
	dst.sin_len = sizeof(dst);

	memset(&mask, 0, sizeof(mask));
	mask.sin_family = AF_INET;
	mask.sin_len = sizeof(mask);

	memset(&gw, 0, sizeof(gw));
	gw.sin_family = AF_INET;
	gw.sin_len = sizeof(gw);

	addrnet = imsg->addr.s_addr & imsg->mask.s_addr;

	for (i = 0; i < imsg->routes_len; i++) {
		dst.sin_addr = imsg->routes[i].dst;
		mask.sin_addr = imsg->routes[i].mask;
		gw.sin_addr = imsg->routes[i].gw;

		if (gw.sin_addr.s_addr == INADDR_ANY) {
			/* direct route */
			configure_route(rtm_type, imsg->if_index,
			    imsg->rdomain, &dst, &mask, &ifa, NULL,
			    RTF_CLONING);
		} else if (mask.sin_addr.s_addr == INADDR_ANY) {
			/* default route */
			gwnet =  gw.sin_addr.s_addr & imsg->mask.s_addr;
			if (addrnet != gwnet) {
				/*
				 * The gateway for the default route is outside
				 * the configured prefix. Install a direct
				 * cloning route for the gateway to make the
				 * default route reachable.
				 */
				mask.sin_addr.s_addr = 0xffffffff;
				configure_route(rtm_type, imsg->if_index,
				    imsg->rdomain, &gw, &mask, &ifa, NULL,
				    RTF_CLONING);
				mask.sin_addr = imsg->routes[i].mask;
			}

			if (gw.sin_addr.s_addr == ifa.sin_addr.s_addr) {
				/* directly connected default */
				configure_route(rtm_type, imsg->if_index,
				    imsg->rdomain, &dst, &mask, &gw, NULL, 0);
			} else {
				/* default route via gateway */
				configure_route(rtm_type, imsg->if_index,
				    imsg->rdomain, &dst, &mask, &gw, &ifa,
				    RTF_GATEWAY);
			}
		} else {
			/* non-default via gateway */
			configure_route(rtm_type, imsg->if_index, imsg->rdomain,
			    &dst, &mask, &gw, NULL, RTF_GATEWAY);
		}
	}
}

#define	ROUNDUP(a)	\
    (((a) & (sizeof(long) - 1)) ? (1 + ((a) | (sizeof(long) - 1))) : (a))
void
configure_route(uint8_t rtm_type, uint32_t if_index, int rdomain, struct
    sockaddr_in *dst, struct sockaddr_in *mask, struct sockaddr_in *gw,
    struct sockaddr_in *ifa, int rtm_flags)
{
	struct rt_msghdr		 rtm;
	struct sockaddr_dl		 ifp;
	struct sockaddr_rtlabel		 rl;
	struct iovec			 iov[14];
	long				 pad = 0;
	int				 iovcnt = 0, padlen;

	memset(&rtm, 0, sizeof(rtm));

	rtm.rtm_version = RTM_VERSION;
	rtm.rtm_type = rtm_type;
	rtm.rtm_msglen = sizeof(rtm);
	rtm.rtm_index = if_index;
	rtm.rtm_tableid = rdomain;
	rtm.rtm_seq = ++rtm_seq;
	rtm.rtm_priority = RTP_NONE;
	rtm.rtm_addrs = RTA_DST | RTA_GATEWAY | RTA_NETMASK | RTA_IFP |
	    RTA_LABEL;
	rtm.rtm_flags = RTF_UP | RTF_STATIC | RTF_MPATH | rtm_flags;

	if (ifa)
		rtm.rtm_addrs |= RTA_IFA;

	iov[iovcnt].iov_base = &rtm;
	iov[iovcnt++].iov_len = sizeof(rtm);

	iov[iovcnt].iov_base = dst;
	iov[iovcnt++].iov_len = dst->sin_len;
	rtm.rtm_msglen += dst->sin_len;
	padlen = ROUNDUP(dst->sin_len) - dst->sin_len;
	if (padlen > 0) {
		iov[iovcnt].iov_base = &pad;
		iov[iovcnt++].iov_len = padlen;
		rtm.rtm_msglen += padlen;
	}

	iov[iovcnt].iov_base = gw;
	iov[iovcnt++].iov_len = gw->sin_len;
	rtm.rtm_msglen += gw->sin_len;
	padlen = ROUNDUP(gw->sin_len) - gw->sin_len;
	if (padlen > 0) {
		iov[iovcnt].iov_base = &pad;
		iov[iovcnt++].iov_len = padlen;
		rtm.rtm_msglen += padlen;
	}

	iov[iovcnt].iov_base = mask;
	iov[iovcnt++].iov_len = mask->sin_len;
	rtm.rtm_msglen += mask->sin_len;
	padlen = ROUNDUP(mask->sin_len) - mask->sin_len;
	if (padlen > 0) {
		iov[iovcnt].iov_base = &pad;
		iov[iovcnt++].iov_len = padlen;
		rtm.rtm_msglen += padlen;
	}

	memset(&ifp, 0, sizeof(ifp));
	ifp.sdl_len = sizeof(struct sockaddr_dl);
	ifp.sdl_family = AF_LINK;
	ifp.sdl_index = if_index;
	iov[iovcnt].iov_base = &ifp;
	iov[iovcnt++].iov_len = sizeof(ifp);
	rtm.rtm_msglen += sizeof(ifp);
	padlen = ROUNDUP(sizeof(ifp)) - sizeof(ifp);
	if (padlen > 0) {
		iov[iovcnt].iov_base = &pad;
		iov[iovcnt++].iov_len = padlen;
		rtm.rtm_msglen += padlen;
	}

	if (ifa) {
		iov[iovcnt].iov_base = ifa;
		iov[iovcnt++].iov_len = ifa->sin_len;
		rtm.rtm_msglen += ifa->sin_len;
		padlen = ROUNDUP(ifa->sin_len) - ifa->sin_len;
		if (padlen > 0) {
			iov[iovcnt].iov_base = &pad;
			iov[iovcnt++].iov_len = padlen;
			rtm.rtm_msglen += padlen;
		}
	}

	memset(&rl, 0, sizeof(rl));
	rl.sr_len = sizeof(rl);
	rl.sr_family = AF_UNSPEC;
	(void)snprintf(rl.sr_label, sizeof(rl.sr_label), "%s",
	    DHCPLEASED_RTA_LABEL);
	iov[iovcnt].iov_base = &rl;
	iov[iovcnt++].iov_len = sizeof(rl);
	rtm.rtm_msglen += sizeof(rl);
	padlen = ROUNDUP(sizeof(rl)) - sizeof(rl);
	if (padlen > 0) {
		iov[iovcnt].iov_base = &pad;
		iov[iovcnt++].iov_len = padlen;
		rtm.rtm_msglen += padlen;
	}

	if (writev(routesock, iov, iovcnt) == -1) {
		if (errno != EEXIST)
			log_warn("failed to send route message");
	}
}

#ifndef	SMALL
const char*
sin_to_str(struct sockaddr_in *sin)
{
	static char hbuf[NI_MAXHOST];
	int error;

	error = getnameinfo((struct sockaddr *)sin, sin->sin_len, hbuf,
	    sizeof(hbuf), NULL, 0, NI_NUMERICHOST | NI_NUMERICSERV);
	if (error) {
		log_warnx("%s", gai_strerror(error));
		strlcpy(hbuf, "unknown", sizeof(hbuf));
	}
	return hbuf;
}
#endif	/* SMALL */

void
open_bpfsock(uint32_t if_index)
{
	int		 bpfsock;
	char		 ifname[IF_NAMESIZE];

	if (if_indextoname(if_index, ifname) == NULL) {
		log_warnx("%s: cannot find interface %d", __func__, if_index);
		return;
	}

	if ((bpfsock = get_bpf_sock(ifname)) == -1)
		return;

	main_imsg_compose_frontend(IMSG_BPFSOCK, bpfsock, &if_index,
	    sizeof(if_index));
}

void
propose_rdns(struct imsg_propose_rdns *rdns)
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
	rtm.rtm_priority = RTP_PROPOSAL_DHCLIENT;
	rtm.rtm_addrs = RTA_DNS;
	rtm.rtm_flags = RTF_UP;

	iov[iovcnt].iov_base = &rtm;
	iov[iovcnt++].iov_len = sizeof(rtm);

	memset(&rtdns, 0, sizeof(rtdns));
	rtdns.sr_family = AF_INET;
	rtdns.sr_len = 2 + rdns->rdns_count * sizeof(struct in_addr);
	memcpy(rtdns.sr_dns, rdns->rdns, rtdns.sr_len - 2);

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
		log_warn("failed to propose nameservers");
}

void
read_lease_file(struct imsg_ifinfo *imsg_ifinfo)
{
	int	 len, fd;
	char	 if_name[IF_NAMESIZE];
	char	 lease_file_buf[sizeof(_PATH_LEASE) + IF_NAMESIZE];

	if (no_lease_files)
		return;

	memset(imsg_ifinfo->lease, 0, sizeof(imsg_ifinfo->lease));

	if (if_indextoname(imsg_ifinfo->if_index, if_name) == NULL) {
		log_warnx("%s: cannot find interface %d", __func__,
		    imsg_ifinfo->if_index);
		return;
	}

	len = snprintf(lease_file_buf, sizeof(lease_file_buf), "%s%s",
	    _PATH_LEASE, if_name);
	if ( len == -1 || (size_t) len >= sizeof(lease_file_buf)) {
		log_warnx("%s: failed to encode lease path for %s", __func__,
		    if_name);
		return;
	}

	if ((fd = open(lease_file_buf, O_RDONLY)) == -1)
		return;

	/* no need for error handling, we'll just do a DHCP discover */
	read(fd, imsg_ifinfo->lease, sizeof(imsg_ifinfo->lease) - 1);
	close(fd);
}

#ifndef SMALL
void
merge_config(struct dhcpleased_conf *conf, struct dhcpleased_conf *xconf)
{
	struct iface_conf	*iface_conf;

	/* Remove & discard existing interfaces. */
	while ((iface_conf = SIMPLEQ_FIRST(&conf->iface_list)) != NULL) {
		SIMPLEQ_REMOVE_HEAD(&conf->iface_list, entry);
		free(iface_conf->vc_id);
		free(iface_conf->c_id);
		free(iface_conf->h_name);
		free(iface_conf);
	}

	/* Add new interfaces. */
	SIMPLEQ_CONCAT(&conf->iface_list, &xconf->iface_list);

	free(xconf);
}

struct dhcpleased_conf *
config_new_empty(void)
{
	struct dhcpleased_conf	*xconf;

	xconf = calloc(1, sizeof(*xconf));
	if (xconf == NULL)
		fatal(NULL);

	SIMPLEQ_INIT(&xconf->iface_list);

	return (xconf);
}

void
config_clear(struct dhcpleased_conf *conf)
{
	struct dhcpleased_conf	*xconf;

	/* Merge current config with an empty config. */
	xconf = config_new_empty();
	merge_config(conf, xconf);

	free(conf);
}

#define	I2S(x) case x: return #x

const char*
i2s(uint32_t type)
{
	static char	unknown[sizeof("IMSG_4294967295")];

	switch (type) {
	I2S(IMSG_NONE);
	I2S(IMSG_CTL_LOG_VERBOSE);
	I2S(IMSG_CTL_SHOW_INTERFACE_INFO);
	I2S(IMSG_CTL_SEND_REQUEST);
	I2S(IMSG_CTL_RELOAD);
	I2S(IMSG_CTL_END);
	I2S(IMSG_RECONF_CONF);
	I2S(IMSG_RECONF_IFACE);
	I2S(IMSG_RECONF_VC_ID);
	I2S(IMSG_RECONF_C_ID);
	I2S(IMSG_RECONF_H_NAME);
	I2S(IMSG_RECONF_END);
	I2S(IMSG_SEND_DISCOVER);
	I2S(IMSG_SEND_REQUEST);
	I2S(IMSG_SOCKET_IPC);
	I2S(IMSG_OPEN_BPFSOCK);
	I2S(IMSG_BPFSOCK);
	I2S(IMSG_UDPSOCK);
	I2S(IMSG_CLOSE_UDPSOCK);
	I2S(IMSG_ROUTESOCK);
	I2S(IMSG_CONTROLFD);
	I2S(IMSG_STARTUP);
	I2S(IMSG_UPDATE_IF);
	I2S(IMSG_REMOVE_IF);
	I2S(IMSG_DHCP);
	I2S(IMSG_CONFIGURE_INTERFACE);
	I2S(IMSG_DECONFIGURE_INTERFACE);
	I2S(IMSG_PROPOSE_RDNS);
	I2S(IMSG_WITHDRAW_RDNS);
	I2S(IMSG_WITHDRAW_ROUTES);
	I2S(IMSG_REPROPOSE_RDNS);
	I2S(IMSG_REQUEST_REBOOT);
	default:
		snprintf(unknown, sizeof(unknown), "IMSG_%u", type);
		return unknown;
	}
}
#undef	I2S

#endif /* SMALL */
