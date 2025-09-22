/*	$OpenBSD: dhcp6leased.c,v 1.20 2025/09/18 11:49:23 florian Exp $	*/

/*
 * Copyright (c) 2017, 2021, 2024 Florian Obser <florian@openbsd.org>
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
#include <netinet6/in6_var.h>

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
#include <uuid.h>

#include "log.h"
#include "dhcp6leased.h"
#include "frontend.h"
#include "engine.h"
#include "control.h"

enum dhcp6leased_process {
	PROC_MAIN,
	PROC_ENGINE,
	PROC_FRONTEND
};

__dead void	usage(void);
__dead void	main_shutdown(void);

void	main_sig_handler(int, short, void *);

static pid_t	start_child(enum dhcp6leased_process, char *, int, int, int);

void	 main_dispatch_frontend(int, short, void *);
void	 main_dispatch_engine(int, short, void *);
void	 open_udpsock(uint32_t);
void	 configure_address(struct imsg_configure_address *);
void	 deconfigure_address(struct imsg_configure_address *);
void	 configure_reject_route(struct imsg_configure_reject_route *, uint8_t);
void	 read_lease_file(struct imsg_ifinfo *);
uint8_t	*get_uuid(void);
void	 write_lease_file(struct imsg_lease_info *);

int	 main_imsg_send_ipc_sockets(struct imsgbuf *, struct imsgbuf *);
int	 main_imsg_compose_frontend(int, int, void *, uint16_t);
int	 main_imsg_compose_engine(int, int, void *, uint16_t);
int	 main_imsg_send_config(struct dhcp6leased_conf *);
int	 main_reload(void);

static struct imsgev	*iev_frontend;
static struct imsgev	*iev_engine;

struct dhcp6leased_conf	*main_conf;
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
		if (main_reload() == -1)
			log_warnx("configuration reload failed");
		else
			log_debug("configuration reloaded");
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
	struct event	 ev_sigint, ev_sigterm, ev_sighup;
	int		 ch;
	int		 debug = 0, engine_flag = 0, frontend_flag = 0;
	int		 verbose = 0, no_action = 0;
	char		*saved_argv0;
	int		 pipe_main2frontend[2];
	int		 pipe_main2engine[2];
	int		 frontend_routesock, rtfilter, lockfd;
	int		 rtable_any = RTABLE_ANY;
	char		*csock = _PATH_CTRL_SOCKET;
	int		 control_fd;
	uint8_t		*uuid;

	log_init(1, LOG_DAEMON);	/* Log to stderr until daemonized. */
	log_setverbose(1);

	saved_argv0 = argv[0];
	if (saved_argv0 == NULL)
		saved_argv0 = "dhcp6leased";

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

	/* parse config file */
	if ((main_conf = parse_config(conffile)) == NULL)
		exit(1);

	if (no_action) {
		if (verbose)
			print_config(main_conf, verbose);
		else
			fprintf(stderr, "configuration OK\n");
		exit(0);
	}

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
	if (getpwnam(DHCP6LEASED_USER) == NULL)
		errx(1, "unknown user %s", DHCP6LEASED_USER);

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

	if ((ioctl_sock = socket(AF_INET6, SOCK_DGRAM | SOCK_CLOEXEC, 0)) == -1)
		fatal("socket");

	if ((frontend_routesock = socket(AF_ROUTE, SOCK_RAW | SOCK_CLOEXEC,
	    AF_INET)) == -1)
		fatal("route socket");

	rtfilter = ROUTE_FILTER(RTM_IFINFO) | ROUTE_FILTER(RTM_IFANNOUNCE);
	if (setsockopt(frontend_routesock, AF_ROUTE, ROUTE_MSGFILTER,
	    &rtfilter, sizeof(rtfilter)) == -1)
		fatal("setsockopt(ROUTE_MSGFILTER)");
	if (setsockopt(frontend_routesock, AF_ROUTE, ROUTE_TABLEFILTER,
	    &rtable_any, sizeof(rtable_any)) == -1)
		fatal("setsockopt(ROUTE_TABLEFILTER)");

	uuid = get_uuid();

	if ((control_fd = control_init(csock)) == -1)
		warnx("control socket setup failed");

	if (unveil(conffile, "r") == -1)
		fatal("unveil %s", conffile);

	if (unveil(_PATH_LEASE, "rwc") == -1) {
		no_lease_files = 1;
		log_warn("disabling lease files, unveil " _PATH_LEASE);
	}

	if (unveil(NULL, NULL) == -1)
		fatal("unveil");

	if (pledge("stdio inet rpath wpath cpath fattr sendfd wroute", NULL)
	    == -1)
		fatal("pledge");

	main_imsg_compose_frontend(IMSG_ROUTESOCK, frontend_routesock, NULL, 0);

	main_imsg_compose_frontend(IMSG_UUID, -1, uuid, UUID_SIZE);
	main_imsg_compose_engine(IMSG_UUID, -1, uuid, UUID_SIZE);

	if (control_fd != -1)
		main_imsg_compose_frontend(IMSG_CONTROLFD, control_fd, NULL, 0);
	main_imsg_send_config(main_conf);

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

	config_clear(main_conf);

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
start_child(enum dhcp6leased_process p, char *argv0, int fd, int debug, int
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
	uint32_t		 if_index;
	int			 verbose;

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

		switch (imsg.hdr.type) {
		case IMSG_OPEN_UDPSOCK:
			if (IMSG_DATA_SIZE(imsg) != sizeof(if_index))
				fatalx("%s: IMSG_OPEN_UDPSOCK wrong length: "
				    "%lu", __func__, IMSG_DATA_SIZE(imsg));
			memcpy(&if_index, imsg.data, sizeof(if_index));
			open_udpsock(if_index);
			break;
		case IMSG_CTL_RELOAD:
			if (main_reload() == -1)
				log_warnx("configuration reload failed");
			else
				log_warnx("configuration reloaded");
			break;
		case IMSG_CTL_LOG_VERBOSE:
			if (IMSG_DATA_SIZE(imsg) != sizeof(verbose))
				fatalx("%s: IMSG_CTL_LOG_VERBOSE wrong length: "
				    "%lu", __func__, IMSG_DATA_SIZE(imsg));
			memcpy(&verbose, imsg.data, sizeof(verbose));
			log_setverbose(verbose);
			break;
		case IMSG_UPDATE_IF:
			if (IMSG_DATA_SIZE(imsg) != sizeof(imsg_ifinfo))
				fatalx("%s: IMSG_UPDATE_IF wrong length: %lu",
				    __func__, IMSG_DATA_SIZE(imsg));
			memcpy(&imsg_ifinfo, imsg.data, sizeof(imsg_ifinfo));
			read_lease_file(&imsg_ifinfo);
			main_imsg_compose_engine(IMSG_UPDATE_IF, -1,
			    &imsg_ifinfo, sizeof(imsg_ifinfo));
			break;
		default:
			log_debug("%s: error handling imsg %d", __func__,
			    imsg.hdr.type);
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
	int				 shut = 0;

	ibuf = &iev->ibuf;

	if (event & EV_READ) {
		if ((n = imsgbuf_read(ibuf)) == -1 && errno != EAGAIN)
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

		switch (imsg.hdr.type) {
		case IMSG_CONFIGURE_ADDRESS: {
			struct imsg_configure_address imsg_configure_address;
			if (IMSG_DATA_SIZE(imsg) !=
			    sizeof(imsg_configure_address))
				fatalx("%s: IMSG_CONFIGURE_ADDRESS wrong "
				    "length: %lu", __func__,
				    IMSG_DATA_SIZE(imsg));
			memcpy(&imsg_configure_address, imsg.data,
			    sizeof(imsg_configure_address));
			configure_address(&imsg_configure_address);
			break;
		}
		case IMSG_DECONFIGURE_ADDRESS: {
			struct imsg_configure_address imsg_configure_address;
			if (IMSG_DATA_SIZE(imsg) !=
			    sizeof(imsg_configure_address))
				fatalx("%s: IMSG_CONFIGURE_ADDRESS wrong "
				    "length: %lu", __func__,
				    IMSG_DATA_SIZE(imsg));
			memcpy(&imsg_configure_address, imsg.data,
			    sizeof(imsg_configure_address));
			deconfigure_address(&imsg_configure_address);
			break;
		}
		case IMSG_CONFIGURE_REJECT_ROUTE: {
			struct imsg_configure_reject_route imsg_crr;
			if (IMSG_DATA_SIZE(imsg) != sizeof(imsg_crr))
				fatalx("%s: IMSG_CONFIGURE_REJECT_ROUTE wrong "
				    "length: %lu", __func__,
				    IMSG_DATA_SIZE(imsg));
			memcpy(&imsg_crr, imsg.data, sizeof(imsg_crr));
			configure_reject_route(&imsg_crr, RTM_ADD);
			break;
		}
		case IMSG_DECONFIGURE_REJECT_ROUTE: {
			struct imsg_configure_reject_route imsg_crr;
			if (IMSG_DATA_SIZE(imsg) != sizeof(imsg_crr))
				fatalx("%s: IMSG_CONFIGURE_REJECT_ROUTE wrong "
				    "length: %lu", __func__,
				    IMSG_DATA_SIZE(imsg));
			memcpy(&imsg_crr, imsg.data, sizeof(imsg_crr));
			configure_reject_route(&imsg_crr, RTM_DELETE);
			break;
		}
		case IMSG_WRITE_LEASE: {
			struct imsg_lease_info imsg_lease_info;
			if (IMSG_DATA_SIZE(imsg) !=
			    sizeof(imsg_lease_info))
				fatalx("%s: IMSG_WRITE_LEASE wrong length: %lu",
				    __func__, IMSG_DATA_SIZE(imsg));
			memcpy(&imsg_lease_info, imsg.data,
			    sizeof(imsg_lease_info));
			write_lease_file(&imsg_lease_info);
			break;
		}
		default:
			log_debug("%s: error handling imsg %d", __func__,
			    imsg.hdr.type);
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

int
main_reload(void)
{
	struct dhcp6leased_conf *xconf;

	if ((xconf = parse_config(conffile)) == NULL)
		return (-1);

	if (main_imsg_send_config(xconf) == -1)
		return (-1);

	merge_config(main_conf, xconf);

	return (0);
}

int
main_imsg_send_config(struct dhcp6leased_conf *xconf)
{
	struct iface_conf	*iface_conf;
	struct iface_ia_conf	*ia_conf;
	struct iface_pd_conf	*pd_conf;

	main_imsg_compose_frontend(IMSG_RECONF_CONF, -1, xconf, sizeof(*xconf));
	main_imsg_compose_engine(IMSG_RECONF_CONF, -1, xconf, sizeof(*xconf));

	/* Send the interface list to the frontend & engine. */
	SIMPLEQ_FOREACH(iface_conf, &xconf->iface_list, entry) {
		main_imsg_compose_frontend(IMSG_RECONF_IFACE, -1, iface_conf,
		    sizeof(*iface_conf));
		main_imsg_compose_engine(IMSG_RECONF_IFACE, -1, iface_conf,
		    sizeof(*iface_conf));
		SIMPLEQ_FOREACH(ia_conf, &iface_conf->iface_ia_list,
		    entry) {
			main_imsg_compose_frontend(IMSG_RECONF_IFACE_IA, -1,
			    ia_conf, sizeof(*ia_conf));
			main_imsg_compose_engine(IMSG_RECONF_IFACE_IA, -1,
			    ia_conf, sizeof(*ia_conf));
			SIMPLEQ_FOREACH(pd_conf, &ia_conf->iface_pd_list,
			    entry) {
				main_imsg_compose_frontend(IMSG_RECONF_IFACE_PD,
				    -1, pd_conf, sizeof(*pd_conf));
				main_imsg_compose_engine(IMSG_RECONF_IFACE_PD,
				    -1, pd_conf, sizeof(*pd_conf));
			}
			main_imsg_compose_frontend(IMSG_RECONF_IFACE_IA_END,
			    -1, NULL, 0);
			main_imsg_compose_engine(IMSG_RECONF_IFACE_IA_END,
			    -1, NULL, 0);
		}
		main_imsg_compose_frontend(IMSG_RECONF_IFACE_END, -1, NULL, 0);
		main_imsg_compose_engine(IMSG_RECONF_IFACE_END, -1, NULL, 0);

	}

	/* Config is now complete. */
	main_imsg_compose_frontend(IMSG_RECONF_END, -1, NULL, 0);
	main_imsg_compose_engine(IMSG_RECONF_END, -1, NULL, 0);

	return (0);
}

void
configure_address(struct imsg_configure_address *address)
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

	log_debug("%s: %s", __func__, if_name);

	if (ioctl(ioctl_sock, SIOCAIFADDR_IN6, &in6_addreq) == -1)
		log_warn("SIOCAIFADDR_IN6");
}

void
deconfigure_address(struct imsg_configure_address *address)
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
configure_reject_route(struct imsg_configure_reject_route *reject_route,
    uint8_t rtm_type)
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
	rtm.rtm_tableid = reject_route->rdomain;
	rtm.rtm_index = reject_route->if_index;
	rtm.rtm_seq = ++rtm_seq;
	rtm.rtm_priority = RTP_DEFAULT;
	rtm.rtm_addrs = RTA_DST | RTA_GATEWAY | RTA_NETMASK | RTA_LABEL;
	rtm.rtm_flags = RTF_UP | RTF_REJECT | RTF_GATEWAY | RTF_STATIC;

	iov[iovcnt].iov_base = &rtm;
	iov[iovcnt++].iov_len = sizeof(rtm);

	memset(&dst, 0, sizeof(dst));
	dst.sin6_family = AF_INET6;
	dst.sin6_len = sizeof(struct sockaddr_in6);
	memcpy(&dst.sin6_addr, &reject_route->prefix, sizeof(dst.sin6_addr));

	iov[iovcnt].iov_base = &dst;
	iov[iovcnt++].iov_len = sizeof(dst);
	rtm.rtm_msglen += sizeof(dst);
	padlen = ROUNDUP(sizeof(dst)) - sizeof(dst);
	if (padlen > 0) {
		iov[iovcnt].iov_base = &pad;
		iov[iovcnt++].iov_len = padlen;
		rtm.rtm_msglen += padlen;
	}

	memset(&gw, 0, sizeof(gw));
	gw.sin6_family = AF_INET6;
	gw.sin6_len = sizeof(struct sockaddr_in6);
	memcpy(&gw.sin6_addr, &in6addr_loopback, sizeof(gw.sin6_addr));

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
	memcpy(&mask.sin6_addr, &reject_route->mask, sizeof(mask.sin6_addr));

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
	    DHCP6LEASED_RTA_LABEL);
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

void
open_udpsock(uint32_t if_index)
{
	struct ifaddrs		*ifap, *ifa;
	struct sockaddr_in6	*sin6 = NULL;
	int			 udpsock = -1, rdomain = -1, opt = 1;
	char			 if_name[IF_NAMESIZE];

	if (if_indextoname(if_index, if_name) == NULL) {
		log_warnx("%s: cannot find interface %d", __func__, if_index);
		return;
	}

	if (getifaddrs(&ifap) != 0)
		fatal("getifaddrs");
	for (ifa = ifap; ifa != NULL; ifa = ifa->ifa_next) {
		if (strcmp(if_name, ifa->ifa_name) != 0)
			continue;
		if (ifa->ifa_addr == NULL)
			continue;
		switch (ifa->ifa_addr->sa_family) {
		case AF_LINK: {
			struct if_data		*if_data;

			if_data = (struct if_data *)ifa->ifa_data;
			rdomain = if_data->ifi_rdomain;
			break;
		}
		case AF_INET6: {
			struct sockaddr_in6 *s6;
			s6 = (struct sockaddr_in6 *)ifa->ifa_addr;
#ifdef __KAME__
			if (IN6_IS_ADDR_LINKLOCAL(&s6->sin6_addr) &&
			    s6->sin6_scope_id == 0) {
				s6->sin6_scope_id = ntohs(*(u_int16_t *)
				    &s6->sin6_addr.s6_addr[2]);
				s6->sin6_addr.s6_addr[2] =
				    s6->sin6_addr.s6_addr[3] = 0;
			}
#endif
			if (IN6_IS_ADDR_LINKLOCAL(&s6->sin6_addr))
				sin6 = s6;
			break;
		}
		default:
			break;

		}
	}

	if (sin6 == NULL) {
		log_warnx("%s: missing link-local address on %s", __func__,
		    if_name);
		goto out;
	}

	if (rdomain == -1) {
		log_warnx("%s: cannot find rdomain for %s", __func__,
		    if_name);
		goto out;
	}

	sin6->sin6_port = htons(CLIENT_PORT);
	log_debug("%s: %s rdomain: %d", __func__, sin6_to_str(sin6),
	    rdomain);

	if ((udpsock = socket(AF_INET6, SOCK_DGRAM, 0)) == -1) {
		log_warn("socket");
		goto out;
	}
	if (setsockopt(udpsock, SOL_SOCKET, SO_REUSEADDR, &opt,
	    sizeof(opt)) == -1)
		log_warn("setting SO_REUSEADDR on socket");

	if (setsockopt(udpsock, SOL_SOCKET, SO_RTABLE, &rdomain,
	    sizeof(rdomain)) == -1) {
		/* we might race against removal of the rdomain */
		log_warn("setsockopt SO_RTABLE");
		close(udpsock);
		goto out;
	}

	if (bind(udpsock, (struct sockaddr *)sin6, sizeof(*sin6)) == -1) {
		close(udpsock);
		goto out;
	}

	main_imsg_compose_frontend(IMSG_UDPSOCK, udpsock, &if_index,
	    sizeof(if_index));
 out:
	freeifaddrs(ifap);
}

void
write_lease_file(struct imsg_lease_info *imsg_lease_info)
{
	struct iface_conf	*iface_conf;
	uint32_t		 i;
	int			 len, fd, rem;
	char			 if_name[IF_NAMESIZE];
	char			 lease_buf[LEASE_SIZE];
	char			 lease_file_buf[sizeof(_PATH_LEASE) +
	    IF_NAMESIZE];
	char			 tmpl[] = _PATH_LEASE"XXXXXXXXXX";
	char			 ntopbuf[INET6_ADDRSTRLEN];
	char			*p;

	if (no_lease_files)
		return;

	if (if_indextoname(imsg_lease_info->if_index, if_name) == NULL) {
		log_warnx("%s: cannot find interface %d", __func__,
		    imsg_lease_info->if_index);
		return;
	}

	if ((iface_conf = find_iface_conf(&main_conf->iface_list, if_name))
	    == NULL) {
		log_debug("%s: no interface configuration for %s", __func__,
		    if_name);
		return;
	}

	len = snprintf(lease_file_buf, sizeof(lease_file_buf), "%s%s",
	    _PATH_LEASE, if_name);
	if (len == -1 || (size_t) len >= sizeof(lease_file_buf)) {
		log_warnx("%s: failed to encode lease path for %s", __func__,
		    if_name);
		return;
	}

	p = lease_buf;
	rem = sizeof(lease_buf);

	for (i = 0; i < iface_conf->ia_count; i++) {
		if (imsg_lease_info->pds[i].prefix_len == 0)
			continue;

		len = snprintf(p, rem, "%s%d %s %d\n", LEASE_IA_PD_PREFIX,
		    i, inet_ntop(AF_INET6, &imsg_lease_info->pds[i].prefix,
		    ntopbuf, INET6_ADDRSTRLEN),
		    imsg_lease_info->pds[i].prefix_len);
		if (len == -1 || len >= rem) {
			log_warnx("%s: failed to encode lease for %s", __func__,
			    if_name);
			return;
		}
		p += len;
		rem -= len;
	}

	len = sizeof(lease_buf) - rem;

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
read_lease_file(struct imsg_ifinfo *imsg_ifinfo)
{
	int	 len;
	char	 if_name[IF_NAMESIZE];
	char	 lease_file_buf[sizeof(_PATH_LEASE) + IF_NAMESIZE];

	if (no_lease_files)
		return;

	memset(imsg_ifinfo->pds, 0, sizeof(imsg_ifinfo->pds));

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
	parse_lease(lease_file_buf, imsg_ifinfo);

	if (log_getverbose() > 1) {
		int	 i;
		char	 ntopbuf[INET6_ADDRSTRLEN];

		for (i = 0; i < MAX_IA; i++) {
			if (imsg_ifinfo->pds[i].prefix_len == 0)
				continue;

			log_debug("%s: %s: %d %s/%d", __func__, if_name, i,
			    inet_ntop(AF_INET6, &imsg_ifinfo->pds[i].prefix,
			    ntopbuf, INET6_ADDRSTRLEN),
			    imsg_ifinfo->pds[i].prefix_len);
		}
	}
}

void
merge_config(struct dhcp6leased_conf *conf, struct dhcp6leased_conf *xconf)
{
	struct iface_conf	*iface_conf;
	struct iface_ia_conf	*ia_conf;
	struct iface_pd_conf	*pd_conf;

	/* Remove & discard existing interfaces. */
	while ((iface_conf = SIMPLEQ_FIRST(&conf->iface_list)) != NULL) {
		SIMPLEQ_REMOVE_HEAD(&conf->iface_list, entry);
		while ((ia_conf =
		    SIMPLEQ_FIRST(&iface_conf->iface_ia_list)) != NULL) {
			SIMPLEQ_REMOVE_HEAD(&iface_conf->iface_ia_list,
			    entry);
			while ((pd_conf =
			    SIMPLEQ_FIRST(&ia_conf->iface_pd_list)) != NULL) {
				SIMPLEQ_REMOVE_HEAD(&ia_conf->iface_pd_list,
				    entry);
				free(pd_conf);
			}
			free(ia_conf);
		}
		free(iface_conf);
	}

	conf->rapid_commit = xconf->rapid_commit;

	/* Add new interfaces. */
	SIMPLEQ_CONCAT(&conf->iface_list, &xconf->iface_list);

	free(xconf);
}

struct dhcp6leased_conf *
config_new_empty(void)
{
	struct dhcp6leased_conf	*xconf;

	xconf = calloc(1, sizeof(*xconf));
	if (xconf == NULL)
		fatal(NULL);

	SIMPLEQ_INIT(&xconf->iface_list);

	return (xconf);
}

void
config_clear(struct dhcp6leased_conf *conf)
{
	struct dhcp6leased_conf	*xconf;

	/* Merge current config with an empty config. */
	xconf = config_new_empty();
	merge_config(conf, xconf);

	free(conf);
}

uint8_t*
get_uuid(void) {
	static uint8_t	 uuid_buf[UUID_SIZE];
	uuid_t		 uuid;
	uint32_t	 status;
	int		 fd, len;
	char		*str;
	char		 strbuf[UUID_STR_SIZE];
	char		 tmpl[] = _PATH_UUID"XXXXXXXXXX";

	if ((fd = open(_PATH_UUID, O_RDONLY)) == -1) {
 gen:
		uuid_create(&uuid, NULL);
		uuid_to_string(&uuid, &str, &status);
		if (status != uuid_s_ok)
			fatalx("failed to generate uuid string representation");

		len = snprintf(strbuf, sizeof(strbuf), "%s\n", str);
		if (len < 0 || (size_t)len >= sizeof(strbuf))
			fatalx("uuid string too long");
		free(str);

		if ((fd = mkstemp(tmpl)) == -1) {
			log_warn("mkstemp");
			goto err;
		}
		if (write(fd, strbuf, len) < len) {
			log_warn("write");
			goto err;
		}
		if (fchmod(fd, 0644) == -1) {
			log_warn("fchmod");
			goto err;
		}

		if (close(fd) == -1) {
			log_warn("fchmod");
			goto err;
		}
		fd = -1;
		if (rename(tmpl, _PATH_UUID) == -1) {
			log_warn("rename");
			goto err;
		}
	} else {
		read(fd, strbuf, sizeof(strbuf));
		close(fd);
		strbuf[sizeof(strbuf) - 2] = '\0';

		uuid_from_string(strbuf, &uuid, &status);

		if (status != uuid_s_ok) {
			log_warnx("failed to convert string to uuid: %s - %d",
			    strbuf, status);
			goto gen;
		}
	}
	uuid_enc_be(uuid_buf, &uuid);
	return (uuid_buf);

 err:
	if (fd != -1)
		close(fd);
	unlink(tmpl);
	fatal("Could not read or create UUID");
}
