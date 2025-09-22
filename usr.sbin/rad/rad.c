/*	$OpenBSD: rad.c,v 1.38 2024/11/21 13:38:15 claudio Exp $	*/

/*
 * Copyright (c) 2018 Florian Obser <florian@openbsd.org>
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
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/syslog.h>
#include <sys/uio.h>
#include <sys/wait.h>

#include <netinet/in.h>
#include <net/if.h>
#include <net/route.h>
#include <netinet/if_ether.h>
#include <netinet6/in6_var.h>
#include <netinet/icmp6.h>

#include <err.h>
#include <errno.h>
#include <event.h>
#include <fcntl.h>
#include <imsg.h>
#include <netdb.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

#include "log.h"
#include "rad.h"
#include "frontend.h"
#include "engine.h"
#include "control.h"

enum rad_process {
	PROC_MAIN,
	PROC_ENGINE,
	PROC_FRONTEND
};

__dead void	usage(void);
__dead void	main_shutdown(void);

void	main_sig_handler(int, short, void *);

static pid_t	start_child(enum rad_process, char *, int, int, int);

void	main_dispatch_frontend(int, short, void *);
void	main_dispatch_engine(int, short, void *);
void	open_icmp6sock(int);

static int	main_imsg_send_ipc_sockets(struct imsgbuf *, struct imsgbuf *);
static int	main_imsg_send_config(struct rad_conf *);

int	main_reload(void);
int	main_sendboth(enum imsg_type, void *, uint16_t);

struct rad_conf		*main_conf;
static struct imsgev	*iev_frontend;
static struct imsgev	*iev_engine;
char			*conffile;
pid_t			 frontend_pid;
pid_t			 engine_pid;
uint32_t		 cmd_opts;

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
		break;
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
	struct event		 ev_sigint, ev_sigterm, ev_sighup;
	int			 ch;
	int			 debug = 0, engine_flag = 0, frontend_flag = 0;
	char			*saved_argv0;
	int			 pipe_main2frontend[2];
	int			 pipe_main2engine[2];
	int			 frontend_routesock, rtfilter;
	int			 rtable_any = RTABLE_ANY;
	int			 control_fd;
	char			*csock;

	conffile = _PATH_CONF_FILE;
	csock = _PATH_RAD_SOCKET;

	log_init(1, LOG_DAEMON);	/* Log to stderr until daemonized. */
	log_setverbose(1);

	saved_argv0 = argv[0];
	if (saved_argv0 == NULL)
		saved_argv0 = "rad";

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
			cmd_opts |= OPT_NOACTION;
			break;
		case 's':
			csock = optarg;
			break;
		case 'v':
			if (cmd_opts & OPT_VERBOSE)
				cmd_opts |= OPT_VERBOSE2;
			cmd_opts |= OPT_VERBOSE;
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
		engine(debug, cmd_opts & OPT_VERBOSE);
	else if (frontend_flag)
		frontend(debug, cmd_opts & OPT_VERBOSE);

	/* parse config file */
	if ((main_conf = parse_config(conffile)) == NULL) {
		exit(1);
	}

	if (cmd_opts & OPT_NOACTION) {
		if (cmd_opts & OPT_VERBOSE)
			print_config(main_conf);
		else
			fprintf(stderr, "configuration OK\n");
		exit(0);
	}

	/* Check for root privileges. */
	if (geteuid())
		errx(1, "need root privileges");

	/* Check for assigned daemon user */
	if (getpwnam(RAD_USER) == NULL)
		errx(1, "unknown user %s", RAD_USER);

	log_init(debug, LOG_DAEMON);
	log_setverbose(cmd_opts & OPT_VERBOSE);

	if (!debug)
		daemon(1, 0);

	log_info("startup");

	if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK,
	    PF_UNSPEC, pipe_main2frontend) == -1)
		fatal("main2frontend socketpair");
	if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK,
	    PF_UNSPEC, pipe_main2engine) == -1)
		fatal("main2engine socketpair");

	/* Start children. */
	engine_pid = start_child(PROC_ENGINE, saved_argv0, pipe_main2engine[1],
	    debug, cmd_opts & OPT_VERBOSE);
	frontend_pid = start_child(PROC_FRONTEND, saved_argv0,
	    pipe_main2frontend[1], debug, cmd_opts & OPT_VERBOSE);

	log_procinit("main");

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

	if ((frontend_routesock = socket(AF_ROUTE, SOCK_RAW | SOCK_CLOEXEC,
	    AF_INET6)) == -1)
		fatal("route socket");

	rtfilter = ROUTE_FILTER(RTM_IFINFO) | ROUTE_FILTER(RTM_NEWADDR) |
	    ROUTE_FILTER(RTM_DELADDR) | ROUTE_FILTER(RTM_CHGADDRATTR);
	if (setsockopt(frontend_routesock, AF_ROUTE, ROUTE_MSGFILTER,
	    &rtfilter, sizeof(rtfilter)) == -1)
		fatal("setsockopt(ROUTE_MSGFILTER)");
	if (setsockopt(frontend_routesock, AF_ROUTE, ROUTE_TABLEFILTER,
	    &rtable_any, sizeof(rtable_any)) == -1)
		fatal("setsockopt(ROUTE_TABLEFILTER)");

	if ((control_fd = control_init(csock)) == -1)
		fatalx("control socket setup failed");

	main_imsg_compose_frontend(IMSG_ROUTESOCK, frontend_routesock,
	    NULL, 0);
	main_imsg_compose_frontend(IMSG_CONTROLFD, control_fd, NULL, 0);
	main_imsg_send_config(main_conf);

	if (pledge("stdio inet rpath sendfd mcast wroute", NULL) == -1)
		fatal("pledge");

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
start_child(enum rad_process p, char *argv0, int fd, int debug, int verbose)
{
	char	*argv[6];
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
	ssize_t			 n;
	int			 shut = 0, verbose;
	int			 rdomain;

	ibuf = &iev->ibuf;

	if (event & EV_READ) {
		if ((n = imsgbuf_read(ibuf)) == -1)
			fatal("imsgbuf_read error");
		if (n == 0)	/* Connection closed. */
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
		if (n == 0)	/* No more messages. */
			break;

		switch (imsg.hdr.type) {
		case IMSG_OPEN_ICMP6SOCK:
			log_debug("IMSG_OPEN_ICMP6SOCK");
			if (IMSG_DATA_SIZE(imsg) != sizeof(rdomain))
				fatalx("%s: IMSG_OPEN_ICMP6SOCK wrong length: "
				    "%lu", __func__, IMSG_DATA_SIZE(imsg));
			memcpy(&rdomain, imsg.data, sizeof(rdomain));
			open_icmp6sock(rdomain);
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
	struct imsgev	*iev = bula;
	struct imsgbuf  *ibuf;
	struct imsg	 imsg;
	ssize_t		 n;
	int		 shut = 0;

	ibuf = &iev->ibuf;

	if (event & EV_READ) {
		if ((n = imsgbuf_read(ibuf)) == -1)
			fatal("imsgbuf_read error");
		if (n == 0)	/* Connection closed. */
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
		if (n == 0)	/* No more messages. */
			break;

		switch (imsg.hdr.type) {
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

void
main_imsg_compose_engine(int type, pid_t pid, void *data, uint16_t datalen)
{
	if (iev_engine)
		imsg_compose_event(iev_engine, type, 0, pid, -1, data,
		    datalen);
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
	if (imsg_compose(engine_buf, IMSG_SOCKET_IPC, 0, 0,
	    pipe_frontend2engine[1], NULL, 0) == -1)
		return (-1);

	return (0);
}

int
main_reload(void)
{
	struct rad_conf *xconf;

	if ((xconf = parse_config(conffile)) == NULL)
		return (-1);

	if (main_imsg_send_config(xconf) == -1)
		return (-1);

	merge_config(main_conf, xconf);

	return (0);
}

int
main_imsg_send_config(struct rad_conf *xconf)
{
	struct ra_iface_conf	*ra_iface_conf;
	struct ra_prefix_conf	*ra_prefix_conf;
	struct ra_rdnss_conf	*ra_rdnss_conf;
	struct ra_dnssl_conf	*ra_dnssl_conf;
	struct ra_pref64_conf	*pref64;

	/* Send fixed part of config to children. */
	if (main_sendboth(IMSG_RECONF_CONF, xconf, sizeof(*xconf)) == -1)
		return (-1);

	/* send global dns options to children */
	SIMPLEQ_FOREACH(ra_rdnss_conf, &xconf->ra_options.ra_rdnss_list,
	    entry) {
		if (main_sendboth(IMSG_RECONF_RA_RDNSS, ra_rdnss_conf,
		    sizeof(*ra_rdnss_conf)) == -1)
			return (-1);
	}
	SIMPLEQ_FOREACH(ra_dnssl_conf, &xconf->ra_options.ra_dnssl_list,
	    entry) {
		if (main_sendboth(IMSG_RECONF_RA_DNSSL, ra_dnssl_conf,
		    sizeof(*ra_dnssl_conf)) == -1)
			return (-1);
	}

	/* send global pref64 list to children */
	SIMPLEQ_FOREACH(pref64, &xconf->ra_options.ra_pref64_list,
	    entry) {
		if (main_sendboth(IMSG_RECONF_RA_PREF64, pref64,
		    sizeof(*pref64)) == -1)
			return (-1);
	}

	/* Send the interface list to children. */
	SIMPLEQ_FOREACH(ra_iface_conf, &xconf->ra_iface_list, entry) {
		if (main_sendboth(IMSG_RECONF_RA_IFACE, ra_iface_conf,
		    sizeof(*ra_iface_conf)) == -1)
			return (-1);
		if (ra_iface_conf->autoprefix) {
			if (main_sendboth(IMSG_RECONF_RA_AUTOPREFIX,
			    ra_iface_conf->autoprefix,
			    sizeof(*ra_iface_conf->autoprefix)) == -1)
				return (-1);
		}
		SIMPLEQ_FOREACH(ra_prefix_conf, &ra_iface_conf->ra_prefix_list,
		    entry) {
			if (main_sendboth(IMSG_RECONF_RA_PREFIX,
			    ra_prefix_conf, sizeof(*ra_prefix_conf)) == -1)
				return (-1);
		}
		SIMPLEQ_FOREACH(ra_rdnss_conf,
		    &ra_iface_conf->ra_options.ra_rdnss_list, entry) {
			if (main_sendboth(IMSG_RECONF_RA_RDNSS, ra_rdnss_conf,
			    sizeof(*ra_rdnss_conf)) == -1)
				return (-1);
		}
		SIMPLEQ_FOREACH(ra_dnssl_conf,
		    &ra_iface_conf->ra_options.ra_dnssl_list, entry) {
			if (main_sendboth(IMSG_RECONF_RA_DNSSL, ra_dnssl_conf,
			    sizeof(*ra_dnssl_conf)) == -1)
				return (-1);
		}
		SIMPLEQ_FOREACH(pref64,
		    &ra_iface_conf->ra_options.ra_pref64_list, entry) {
			if (main_sendboth(IMSG_RECONF_RA_PREF64, pref64,
			    sizeof(*pref64)) == -1)
				return (-1);
		}
	}

	/* Tell children the revised config is now complete. */
	if (main_sendboth(IMSG_RECONF_END, NULL, 0) == -1)
		return (-1);

	return (0);
}

int
main_sendboth(enum imsg_type type, void *buf, uint16_t len)
{
	if (imsg_compose_event(iev_frontend, type, 0, 0, -1, buf, len) == -1)
		return (-1);
	if (imsg_compose_event(iev_engine, type, 0, 0, -1, buf, len) == -1)
		return (-1);
	return (0);
}

void
free_ra_iface_conf(struct ra_iface_conf *ra_iface_conf)
{
	struct ra_prefix_conf	*prefix;
	struct ra_pref64_conf	*pref64;

	if (!ra_iface_conf)
		return;

	free(ra_iface_conf->autoprefix);

	while ((prefix = SIMPLEQ_FIRST(&ra_iface_conf->ra_prefix_list)) !=
	    NULL) {
		SIMPLEQ_REMOVE_HEAD(&ra_iface_conf->ra_prefix_list, entry);
		free(prefix);
	}

	free_dns_options(&ra_iface_conf->ra_options);

	while ((pref64 =
	    SIMPLEQ_FIRST(&ra_iface_conf->ra_options.ra_pref64_list)) != NULL) {
		SIMPLEQ_REMOVE_HEAD(&ra_iface_conf->ra_options.ra_pref64_list,
		    entry);
		free(pref64);
	}

	free(ra_iface_conf);
}

void
free_dns_options(struct ra_options_conf *ra_options)
{
	struct ra_rdnss_conf	*ra_rdnss;
	struct ra_dnssl_conf	*ra_dnssl;

	while ((ra_rdnss = SIMPLEQ_FIRST(&ra_options->ra_rdnss_list)) != NULL) {
		SIMPLEQ_REMOVE_HEAD(&ra_options->ra_rdnss_list, entry);
		free(ra_rdnss);
	}
	ra_options->rdnss_count = 0;

	while ((ra_dnssl = SIMPLEQ_FIRST(&ra_options->ra_dnssl_list)) != NULL) {
		SIMPLEQ_REMOVE_HEAD(&ra_options->ra_dnssl_list, entry);
		free(ra_dnssl);
	}
	ra_options->dnssl_len = 0;
}

void
merge_config(struct rad_conf *conf, struct rad_conf *xconf)
{
	struct ra_iface_conf	*ra_iface_conf;
	struct ra_pref64_conf	*pref64;

	/* Remove & discard existing interfaces. */
	while ((ra_iface_conf = SIMPLEQ_FIRST(&conf->ra_iface_list)) != NULL) {
		SIMPLEQ_REMOVE_HEAD(&conf->ra_iface_list, entry);
		free_ra_iface_conf(ra_iface_conf);
	}
	free_dns_options(&conf->ra_options);

	while ((pref64 = SIMPLEQ_FIRST(&conf->ra_options.ra_pref64_list))
	    != NULL) {
		SIMPLEQ_REMOVE_HEAD(&conf->ra_options.ra_pref64_list, entry);
		free(pref64);
	}

	conf->ra_options = xconf->ra_options;
	SIMPLEQ_INIT(&conf->ra_options.ra_rdnss_list);
	SIMPLEQ_INIT(&conf->ra_options.ra_dnssl_list);
	SIMPLEQ_INIT(&conf->ra_options.ra_pref64_list);

	/* Add new interfaces. */
	SIMPLEQ_CONCAT(&conf->ra_iface_list, &xconf->ra_iface_list);

	/* Add dns options */
	SIMPLEQ_CONCAT(&conf->ra_options.ra_rdnss_list,
	    &xconf->ra_options.ra_rdnss_list);
	SIMPLEQ_CONCAT(&conf->ra_options.ra_dnssl_list,
	    &xconf->ra_options.ra_dnssl_list);
	SIMPLEQ_CONCAT(&conf->ra_options.ra_pref64_list,
	    &xconf->ra_options.ra_pref64_list);
	free(xconf);
}

struct rad_conf *
config_new_empty(void)
{
	struct rad_conf	*xconf;

	xconf = calloc(1, sizeof(*xconf));
	if (xconf == NULL)
		fatal(NULL);

	SIMPLEQ_INIT(&xconf->ra_iface_list);

	xconf->ra_options.dfr = 1;
	xconf->ra_options.cur_hl = 0;
	xconf->ra_options.m_flag = 0;
	xconf->ra_options.o_flag = 0;
	xconf->ra_options.router_lifetime = ADV_DEFAULT_LIFETIME;
	xconf->ra_options.reachable_time = 0;
	xconf->ra_options.retrans_timer = 0;
	xconf->ra_options.source_link_addr = 1;
	xconf->ra_options.mtu = 0;
	xconf->ra_options.rdns_lifetime = DEFAULT_RDNS_LIFETIME;
	SIMPLEQ_INIT(&xconf->ra_options.ra_rdnss_list);
	SIMPLEQ_INIT(&xconf->ra_options.ra_dnssl_list);
	SIMPLEQ_INIT(&xconf->ra_options.ra_pref64_list);

	return (xconf);
}

void
config_clear(struct rad_conf *conf)
{
	struct rad_conf	*xconf;

	/* Merge current config with an empty config. */
	xconf = config_new_empty();
	merge_config(conf, xconf);

	free(conf);
}

void
mask_prefix(struct in6_addr* in6, int len)
{
	uint8_t	bitmask[8] = {0x00, 0x80, 0xc0, 0xe0, 0xf0, 0xf8, 0xfc, 0xfe};
	int	i, skip;

	if (len < 0 || len > 128)
		fatalx("invalid prefix length: %d", len);

	skip = len / 8;

	if (skip < 16)
		in6->s6_addr[skip] &= bitmask[len % 8];

	for (i = skip + 1; i < 16; i++)
		in6->s6_addr[i] = 0;
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

const char*
in6_to_str(struct in6_addr *in6)
{

	struct sockaddr_in6	sin6;

	memset(&sin6, 0, sizeof(sin6));
	sin6.sin6_len = sizeof(sin6);
	sin6.sin6_family = AF_INET6;
	sin6.sin6_addr = *in6;

	return (sin6_to_str(&sin6));
}

void
open_icmp6sock(int rdomain)
{
	int			 icmp6sock, on = 1, off = 0;

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

	if (setsockopt(icmp6sock, IPPROTO_IPV6, IPV6_MULTICAST_LOOP, &off,
	    sizeof(off)) == -1)
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
