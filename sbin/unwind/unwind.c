/*	$OpenBSD: unwind.c,v 1.77 2025/09/15 08:43:51 florian Exp $	*/

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
#include <sys/stat.h>
#include <sys/syslog.h>
#include <sys/wait.h>

#include <net/if.h>
#include <net/route.h>

#include <err.h>
#include <errno.h>
#include <event.h>
#include <fcntl.h>
#include <imsg.h>
#include <netdb.h>
#include <asr.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

#include "log.h"
#include "unwind.h"
#include "frontend.h"
#include "resolver.h"
#include "control.h"

#define	TRUST_ANCHOR_FILE	"/var/db/unwind.key"

enum uw_process {
	PROC_MAIN,
	PROC_RESOLVER,
	PROC_FRONTEND,
};

__dead void	usage(void);
__dead void	main_shutdown(void);

void		main_sig_handler(int, short, void *);

static pid_t	start_child(enum uw_process, char *, int, int, int);

void		main_dispatch_frontend(int, short, void *);
void		main_dispatch_resolver(int, short, void *);

static int	main_imsg_send_ipc_sockets(struct imsgbuf *, struct imsgbuf *);
static int	main_imsg_send_config(struct uw_conf *);

int		main_reload(void);
int		main_sendall(enum imsg_type, void *, uint16_t);
void		open_ports(void);
void		solicit_dns_proposals(void);
void		send_blocklist_fd(void);

struct uw_conf		*main_conf;
static struct imsgev	*iev_frontend;
static struct imsgev	*iev_resolver;
char			*conffile;
pid_t			 frontend_pid;
pid_t			 resolver_pid;
uint32_t		 cmd_opts;
int			 routesock;

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
	struct event	 ev_sigint, ev_sigterm, ev_sighup;
	int		 ch, debug = 0, resolver_flag = 0, frontend_flag = 0;
	int		 frontend_routesock, rtfilter;
	int		 pipe_main2frontend[2], pipe_main2resolver[2];
	int		 control_fd, ta_fd;
	char		*csock, *saved_argv0;

	csock = _PATH_UNWIND_SOCKET;

	log_init(1, LOG_DAEMON);	/* Log to stderr until daemonized. */
	log_setverbose(1);

	saved_argv0 = argv[0];
	if (saved_argv0 == NULL)
		saved_argv0 = "unwind";

	while ((ch = getopt(argc, argv, "dEFf:ns:v")) != -1) {
		switch (ch) {
		case 'd':
			debug = 1;
			break;
		case 'E':
			resolver_flag = 1;
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
			if (cmd_opts & OPT_VERBOSE2)
				cmd_opts |= OPT_VERBOSE3;
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
	if (argc > 0 || (resolver_flag && frontend_flag))
		usage();

	if (resolver_flag)
		resolver(debug, cmd_opts & (OPT_VERBOSE | OPT_VERBOSE2 |
		    OPT_VERBOSE3));
	else if (frontend_flag)
		frontend(debug, cmd_opts & (OPT_VERBOSE | OPT_VERBOSE2 |
		    OPT_VERBOSE3));

	if ((main_conf = parse_config(conffile)) == NULL)
		exit(1);

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
	if (getpwnam(UNWIND_USER) == NULL)
		errx(1, "unknown user %s", UNWIND_USER);

	log_init(debug, LOG_DAEMON);
	log_setverbose(cmd_opts & (OPT_VERBOSE | OPT_VERBOSE2 | OPT_VERBOSE3));

	if (!debug)
		daemon(1, 0);

	if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK,
	    PF_UNSPEC, pipe_main2frontend) == -1)
		fatal("main2frontend socketpair");
	if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK,
	    PF_UNSPEC, pipe_main2resolver) == -1)
		fatal("main2resolver socketpair");

	/* Start children. */
	resolver_pid = start_child(PROC_RESOLVER, saved_argv0,
	    pipe_main2resolver[1], debug, cmd_opts & (OPT_VERBOSE |
	    OPT_VERBOSE2 | OPT_VERBOSE3));
	frontend_pid = start_child(PROC_FRONTEND, saved_argv0,
	    pipe_main2frontend[1], debug, cmd_opts & (OPT_VERBOSE |
	    OPT_VERBOSE2 | OPT_VERBOSE3));

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
	    (iev_resolver = malloc(sizeof(struct imsgev))) == NULL)
		fatal(NULL);
	if (imsgbuf_init(&iev_frontend->ibuf, pipe_main2frontend[0]) == -1)
		fatal(NULL);
	imsgbuf_allow_fdpass(&iev_frontend->ibuf);
	iev_frontend->handler = main_dispatch_frontend;
	if (imsgbuf_init(&iev_resolver->ibuf, pipe_main2resolver[0]) == -1)
		fatal(NULL);
	imsgbuf_allow_fdpass(&iev_resolver->ibuf);
	iev_resolver->handler = main_dispatch_resolver;

	/* Setup event handlers for pipes. */
	iev_frontend->events = EV_READ;
	event_set(&iev_frontend->ev, iev_frontend->ibuf.fd,
	    iev_frontend->events, iev_frontend->handler, iev_frontend);
	event_add(&iev_frontend->ev, NULL);

	iev_resolver->events = EV_READ;
	event_set(&iev_resolver->ev, iev_resolver->ibuf.fd,
	    iev_resolver->events, iev_resolver->handler, iev_resolver);
	event_add(&iev_resolver->ev, NULL);

	if (main_imsg_send_ipc_sockets(&iev_frontend->ibuf,
	    &iev_resolver->ibuf))
		fatal("could not establish imsg links");

	open_ports();

	if ((control_fd = control_init(csock)) == -1)
		fatalx("control socket setup failed");

	if ((frontend_routesock = socket(AF_ROUTE, SOCK_RAW | SOCK_CLOEXEC |
	    SOCK_NONBLOCK, 0)) == -1)
		fatal("route socket");

	rtfilter = ROUTE_FILTER(RTM_IFINFO) | ROUTE_FILTER(RTM_PROPOSAL)
	    | ROUTE_FILTER(RTM_IFANNOUNCE) | ROUTE_FILTER(RTM_NEWADDR)
	    | ROUTE_FILTER(RTM_DELADDR);
	if (setsockopt(frontend_routesock, AF_ROUTE, ROUTE_MSGFILTER,
	    &rtfilter, sizeof(rtfilter)) == -1)
		fatal("setsockopt(ROUTE_MSGFILTER)");

	if ((routesock = socket(AF_ROUTE, SOCK_RAW | SOCK_CLOEXEC |
	    SOCK_NONBLOCK, 0)) == -1)
		fatal("route socket");
	shutdown(routesock, SHUT_RD);

	if ((ta_fd = open(TRUST_ANCHOR_FILE, O_RDWR | O_CREAT, 0644)) == -1)
		log_warn("%s", TRUST_ANCHOR_FILE);

	/* receiver handles failed open correctly */
	main_imsg_compose_frontend_fd(IMSG_TAFD, 0, ta_fd);

	main_imsg_compose_frontend_fd(IMSG_CONTROLFD, 0, control_fd);
	main_imsg_compose_frontend_fd(IMSG_ROUTESOCK, 0, frontend_routesock);
	main_imsg_send_config(main_conf);

	if (main_conf->blocklist_file != NULL)
		send_blocklist_fd();

	if (pledge("stdio rpath sendfd", NULL) == -1)
		fatal("pledge");

	main_imsg_compose_frontend(IMSG_STARTUP, 0, NULL, 0);
	main_imsg_compose_resolver(IMSG_STARTUP, 0, NULL, 0);

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
	imsgbuf_clear(&iev_resolver->ibuf);
	close(iev_resolver->ibuf.fd);

	config_clear(main_conf);

	log_debug("waiting for children to terminate");
	do {
		pid = wait(&status);
		if (pid == -1) {
			if (errno != EINTR && errno != ECHILD)
				fatal("wait");
		} else if (WIFSIGNALED(status))
			log_warnx("%s terminated; signal %d",
			    (pid == resolver_pid) ? "resolver" :
			    "frontend", WTERMSIG(status));
	} while (pid != -1 || (pid == -1 && errno == EINTR));

	free(iev_frontend);
	free(iev_resolver);

	log_info("terminating");
	exit(0);
}

static pid_t
start_child(enum uw_process p, char *argv0, int fd, int debug, int verbose)
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
	case PROC_RESOLVER:
		argv[argc++] = "-E";
		break;
	case PROC_FRONTEND:
		argv[argc++] = "-F";
		break;
	}
	if (debug)
		argv[argc++] = "-d";
	if (verbose & OPT_VERBOSE)
		argv[argc++] = "-v";
	if (verbose & OPT_VERBOSE2)
		argv[argc++] = "-v";
	if (verbose & OPT_VERBOSE3)
		argv[argc++] = "-v";
	argv[argc++] = NULL;

	execvp(argv0, argv);
	fatal("execvp");
}

void
main_dispatch_frontend(int fd, short event, void *bula)
{
	struct imsgev	*iev = bula;
	struct imsgbuf	*ibuf;
	struct imsg	 imsg;
	ssize_t		 n;
	int		 shut = 0, verbose;

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
		case IMSG_STARTUP_DONE:
			solicit_dns_proposals();
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
main_dispatch_resolver(int fd, short event, void *bula)
{
	struct imsgev		*iev = bula;
	struct imsgbuf		*ibuf;
	struct imsg		 imsg;
	ssize_t			 n;
	int			 shut = 0;

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

void
main_imsg_compose_frontend(int type, pid_t pid, void *data, uint16_t datalen)
{
	if (iev_frontend)
		imsg_compose_event(iev_frontend, type, 0, pid, -1, data,
		    datalen);
}

void
main_imsg_compose_frontend_fd(int type, pid_t pid, int fd)
{
	if (iev_frontend)
		imsg_compose_event(iev_frontend, type, 0, pid, fd, NULL, 0);
}

void
main_imsg_compose_resolver(int type, pid_t pid, void *data, uint16_t datalen)
{
	if (iev_resolver)
		imsg_compose_event(iev_resolver, type, 0, pid, -1, data,
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
    struct imsgbuf *resolver_buf)
{
	int pipe_frontend2resolver[2];

	if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK,
	    PF_UNSPEC, pipe_frontend2resolver) == -1)
		return (-1);

	if (imsg_compose(frontend_buf, IMSG_SOCKET_IPC_RESOLVER, 0, 0,
	    pipe_frontend2resolver[0], NULL, 0) == -1)
		return (-1);
	if (imsg_compose(resolver_buf, IMSG_SOCKET_IPC_FRONTEND, 0, 0,
	    pipe_frontend2resolver[1], NULL, 0) == -1)
		return (-1);

	return (0);
}

int
main_reload(void)
{
	struct uw_conf	*xconf;

	if ((xconf = parse_config(conffile)) == NULL)
		return (-1);

	if (main_imsg_send_config(xconf) == -1)
		return (-1);

	merge_config(main_conf, xconf);

	if (main_conf->blocklist_file != NULL)
		send_blocklist_fd();

	return (0);
}

int
main_imsg_send_config(struct uw_conf *xconf)
{
	struct uw_forwarder		*uw_forwarder;
	struct force_tree_entry	*force_entry;

	/* Send fixed part of config to children. */
	if (main_sendall(IMSG_RECONF_CONF, xconf, sizeof(*xconf)) == -1)
		return (-1);

	if (xconf->blocklist_file != NULL) {
		if (main_sendall(IMSG_RECONF_BLOCKLIST_FILE,
		    xconf->blocklist_file, strlen(xconf->blocklist_file) + 1)
		    == -1)
			return (-1);
	}

	/* send static forwarders to children */
	TAILQ_FOREACH(uw_forwarder, &xconf->uw_forwarder_list, entry) {
		if (main_sendall(IMSG_RECONF_FORWARDER, uw_forwarder,
		    sizeof(*uw_forwarder)) == -1)
			return (-1);
	}

	/* send static DoT forwarders to children */
	TAILQ_FOREACH(uw_forwarder, &xconf->uw_dot_forwarder_list,
	    entry) {
		if (main_sendall(IMSG_RECONF_DOT_FORWARDER, uw_forwarder,
		    sizeof(*uw_forwarder)) == -1)
			return (-1);
	}
	RB_FOREACH(force_entry, force_tree, &xconf->force) {
		if (main_sendall(IMSG_RECONF_FORCE, force_entry,
		    sizeof(*force_entry)) == -1)
			return (-1);
	}

	/* Tell children the revised config is now complete. */
	if (main_sendall(IMSG_RECONF_END, NULL, 0) == -1)
		return (-1);

	return (0);
}

int
main_sendall(enum imsg_type type, void *buf, uint16_t len)
{
	if (imsg_compose_event(iev_frontend, type, 0, 0, -1, buf, len) == -1)
		return (-1);
	if (imsg_compose_event(iev_resolver, type, 0, 0, -1, buf, len) == -1)
		return (-1);
	return (0);
}

void
merge_config(struct uw_conf *conf, struct uw_conf *xconf)
{
	struct uw_forwarder		*uw_forwarder;
	struct force_tree_entry	*n, *nxt;

	/* Remove & discard existing forwarders. */
	while ((uw_forwarder = TAILQ_FIRST(&conf->uw_forwarder_list)) !=
	    NULL) {
		TAILQ_REMOVE(&conf->uw_forwarder_list, uw_forwarder, entry);
		free(uw_forwarder);
	}
	while ((uw_forwarder = TAILQ_FIRST(&conf->uw_dot_forwarder_list)) !=
	    NULL) {
		TAILQ_REMOVE(&conf->uw_dot_forwarder_list, uw_forwarder, entry);
		free(uw_forwarder);
	}

	/* Remove & discard existing force tree. */
	RB_FOREACH_SAFE(n, force_tree, &conf->force, nxt) {
		RB_REMOVE(force_tree, &conf->force, n);
		free(n);
	}

	memcpy(&conf->enabled_resolvers, &xconf->enabled_resolvers,
	    sizeof(conf->enabled_resolvers));
	memcpy(&conf->force_resolvers, &xconf->force_resolvers,
	    sizeof(conf->force_resolvers));

	memcpy(&conf->res_pref, &xconf->res_pref,
	    sizeof(conf->res_pref));

	free(conf->blocklist_file);
	conf->blocklist_file = xconf->blocklist_file;
	conf->blocklist_log = xconf->blocklist_log;

	/* Add new forwarders. */
	TAILQ_CONCAT(&conf->uw_forwarder_list, &xconf->uw_forwarder_list,
	    entry);
	TAILQ_CONCAT(&conf->uw_dot_forwarder_list,
	    &xconf->uw_dot_forwarder_list, entry);

	RB_FOREACH_SAFE(n, force_tree, &xconf->force, nxt) {
		RB_REMOVE(force_tree, &xconf->force, n);
		RB_INSERT(force_tree, &conf->force, n);
	}

	free(xconf);
}

struct uw_conf *
config_new_empty(void)
{
	struct uw_conf			*xconf;

	xconf = calloc(1, sizeof(*xconf));
	if (xconf == NULL)
		fatal(NULL);

	TAILQ_INIT(&xconf->uw_forwarder_list);
	TAILQ_INIT(&xconf->uw_dot_forwarder_list);

	RB_INIT(&xconf->force);

	return (xconf);
}

void
config_clear(struct uw_conf *conf)
{
	struct uw_conf	*xconf;

	/* Merge current config with an empty config. */
	xconf = config_new_empty();
	merge_config(conf, xconf);

	free(conf);
}

void
open_ports(void)
{
	struct addrinfo	 hints, *res0;
	int		 udp4sock = -1, udp6sock = -1, error, bsize = 65535;
	int		 tcp4sock = -1, tcp6sock = -1;
	int		 opt = 1;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_flags = AI_NUMERICHOST | AI_PASSIVE;

	error = getaddrinfo("127.0.0.1", "domain", &hints, &res0);
	if (!error && res0) {
		if ((udp4sock = socket(res0->ai_family, res0->ai_socktype,
		    res0->ai_protocol)) != -1) {
			if (setsockopt(udp4sock, SOL_SOCKET, SO_REUSEADDR,
			    &opt, sizeof(opt)) == -1)
				log_warn("setting SO_REUSEADDR on socket");
			if (setsockopt(udp4sock, SOL_SOCKET, SO_SNDBUF, &bsize,
			    sizeof(bsize)) == -1)
				log_warn("setting SO_SNDBUF on socket");
			if (bind(udp4sock, res0->ai_addr, res0->ai_addrlen)
			    == -1) {
				close(udp4sock);
				udp4sock = -1;
			}
		}
	}
	if (res0)
		freeaddrinfo(res0);

	hints.ai_family = AF_INET6;
	error = getaddrinfo("::1", "domain", &hints, &res0);
	if (!error && res0) {
		if ((udp6sock = socket(res0->ai_family, res0->ai_socktype,
		    res0->ai_protocol)) != -1) {
			if (setsockopt(udp6sock, SOL_SOCKET, SO_REUSEADDR,
			    &opt, sizeof(opt)) == -1)
				log_warn("setting SO_REUSEADDR on socket");
			if (setsockopt(udp6sock, SOL_SOCKET, SO_SNDBUF, &bsize,
			    sizeof(bsize)) == -1)
				log_warn("setting SO_SNDBUF on socket");
			if (bind(udp6sock, res0->ai_addr, res0->ai_addrlen)
			    == -1) {
				close(udp6sock);
				udp6sock = -1;
			}
		}
	}
	if (res0)
		freeaddrinfo(res0);

	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;

	error = getaddrinfo("127.0.0.1", "domain", &hints, &res0);
	if (!error && res0) {
		if ((tcp4sock = socket(res0->ai_family,
		    res0->ai_socktype | SOCK_NONBLOCK,
		    res0->ai_protocol)) != -1) {
			if (setsockopt(tcp4sock, SOL_SOCKET, SO_REUSEADDR,
			    &opt, sizeof(opt)) == -1)
				log_warn("setting SO_REUSEADDR on socket");
			if (setsockopt(tcp4sock, SOL_SOCKET, SO_SNDBUF, &bsize,
			    sizeof(bsize)) == -1)
				log_warn("setting SO_SNDBUF on socket");
			if (bind(tcp4sock, res0->ai_addr, res0->ai_addrlen)
			    == -1) {
				close(tcp4sock);
				tcp4sock = -1;
			}
			if (listen(tcp4sock, 5) == -1) {
				close(tcp4sock);
				tcp4sock = -1;
			}
		}
	}
	if (res0)
		freeaddrinfo(res0);

	hints.ai_family = AF_INET6;
	error = getaddrinfo("::1", "domain", &hints, &res0);
	if (!error && res0) {
		if ((tcp6sock = socket(res0->ai_family,
		    res0->ai_socktype | SOCK_NONBLOCK,
		    res0->ai_protocol)) != -1) {
			if (setsockopt(tcp6sock, SOL_SOCKET, SO_REUSEADDR,
			    &opt, sizeof(opt)) == -1)
				log_warn("setting SO_REUSEADDR on socket");
			if (setsockopt(tcp6sock, SOL_SOCKET, SO_SNDBUF, &bsize,
			    sizeof(bsize)) == -1)
				log_warn("setting SO_SNDBUF on socket");
			if (bind(tcp6sock, res0->ai_addr, res0->ai_addrlen)
			    == -1) {
				close(tcp6sock);
				tcp6sock = -1;
			}
			if (listen(tcp6sock, 5) == -1) {
				close(tcp6sock);
				tcp6sock = -1;
			}
		}
	}
	if (res0)
		freeaddrinfo(res0);

	if ((udp4sock == -1 || tcp4sock == -1) && (udp6sock == -1 ||
	    tcp6sock == -1))
		fatalx("could not bind to 127.0.0.1 or ::1 on port 53");

	if (udp4sock != -1)
		main_imsg_compose_frontend_fd(IMSG_UDP4SOCK, 0, udp4sock);
	if (udp6sock != -1)
		main_imsg_compose_frontend_fd(IMSG_UDP6SOCK, 0, udp6sock);
	if (tcp4sock != -1)
		main_imsg_compose_frontend_fd(IMSG_TCP4SOCK, 0, tcp4sock);
	if (tcp6sock != -1)
		main_imsg_compose_frontend_fd(IMSG_TCP6SOCK, 0, tcp6sock);
}

void
solicit_dns_proposals(void)
{
	struct rt_msghdr		 rtm;
	struct iovec			 iov[1];
	int				 iovcnt = 0;

	memset(&rtm, 0, sizeof(rtm));

	rtm.rtm_version = RTM_VERSION;
	rtm.rtm_type = RTM_PROPOSAL;
	rtm.rtm_msglen = sizeof(rtm);
	rtm.rtm_tableid = 0;
	rtm.rtm_index = 0;
	rtm.rtm_seq = arc4random();
	rtm.rtm_priority = RTP_PROPOSAL_SOLICIT;

	iov[iovcnt].iov_base = &rtm;
	iov[iovcnt++].iov_len = sizeof(rtm);

	if (writev(routesock, iov, iovcnt) == -1)
		log_warn("failed to send solicitation");
}

void
send_blocklist_fd(void)
{
	int	bl_fd;

	if ((bl_fd = open(main_conf->blocklist_file, O_RDONLY)) != -1)
		main_imsg_compose_frontend_fd(IMSG_BLFD, 0, bl_fd);
	else
		log_warn("%s", main_conf->blocklist_file);
}

void
imsg_receive_config(struct imsg *imsg, struct uw_conf **xconf)
{
	struct uw_conf			*nconf;
	struct uw_forwarder		*uw_forwarder;
	struct force_tree_entry	*force_entry;

	nconf = *xconf;

	switch (imsg->hdr.type) {
	case IMSG_RECONF_CONF:
		if (nconf != NULL)
			fatalx("%s: IMSG_RECONF_CONF already in "
			    "progress", __func__);
		if (IMSG_DATA_SIZE(*imsg) != sizeof(struct uw_conf))
			fatalx("%s: IMSG_RECONF_CONF wrong length: %lu",
			    __func__, IMSG_DATA_SIZE(*imsg));
		if ((*xconf = malloc(sizeof(struct uw_conf))) == NULL)
			fatal(NULL);
		nconf = *xconf;
		memcpy(nconf, imsg->data, sizeof(struct uw_conf));
		TAILQ_INIT(&nconf->uw_forwarder_list);
		TAILQ_INIT(&nconf->uw_dot_forwarder_list);
		RB_INIT(&nconf->force);
		break;
	case IMSG_RECONF_BLOCKLIST_FILE:
		if (((char *)imsg->data)[IMSG_DATA_SIZE(*imsg) - 1] != '\0')
			fatalx("Invalid blocklist file");
		if ((nconf->blocklist_file = strdup(imsg->data)) ==
		    NULL)
			fatal("%s: strdup", __func__);
		break;
	case IMSG_RECONF_FORWARDER:
		if (IMSG_DATA_SIZE(*imsg) != sizeof(struct uw_forwarder))
			fatalx("%s: IMSG_RECONF_FORWARDER wrong length:"
			    " %lu", __func__, IMSG_DATA_SIZE(*imsg));
		if ((uw_forwarder = malloc(sizeof(struct
		    uw_forwarder))) == NULL)
			fatal(NULL);
		memcpy(uw_forwarder, imsg->data, sizeof(struct
		    uw_forwarder));
		if (uw_forwarder->ip[sizeof(uw_forwarder->ip) - 1] != '\0')
			fatalx("%s: invalid uw_forwarder", __func__);
		if (uw_forwarder->auth_name[
		    sizeof(uw_forwarder->auth_name) - 1] != '\0')
			fatalx("%s: invalid uw_forwarder", __func__);

		TAILQ_INSERT_TAIL(&nconf->uw_forwarder_list,
		    uw_forwarder, entry);
		break;
	case IMSG_RECONF_DOT_FORWARDER:
		if (IMSG_DATA_SIZE(*imsg) != sizeof(struct uw_forwarder))
			fatalx("%s: IMSG_RECONF_DOT_FORWARDER wrong "
			    "length: %lu", __func__,
			    IMSG_DATA_SIZE(*imsg));
		if ((uw_forwarder = malloc(sizeof(struct
		    uw_forwarder))) == NULL)
			fatal(NULL);
		memcpy(uw_forwarder, imsg->data, sizeof(struct
		    uw_forwarder));
		if (uw_forwarder->ip[sizeof(uw_forwarder->ip) - 1] != '\0')
			fatalx("%s: invalid uw_forwarder", __func__);
		if (uw_forwarder->auth_name[
		    sizeof(uw_forwarder->auth_name) - 1] != '\0')
			fatalx("%s: invalid uw_forwarder", __func__);

		TAILQ_INSERT_TAIL(&nconf->uw_dot_forwarder_list,
		    uw_forwarder, entry);
		break;
	case IMSG_RECONF_FORCE:
		if (IMSG_DATA_SIZE(*imsg) != sizeof(struct force_tree_entry))
			fatalx("%s: IMSG_RECONF_FORCE wrong "
			    "length: %lu", __func__,
			    IMSG_DATA_SIZE(*imsg));
		if ((force_entry = malloc(sizeof(struct
		    force_tree_entry))) == NULL)
			fatal(NULL);
		memcpy(force_entry, imsg->data, sizeof(struct
		    force_tree_entry));
		if (force_entry->domain[
		    sizeof(force_entry->domain) - 1] != '\0')
			fatalx("%s: invalid force_entry", __func__);

		if (RB_INSERT(force_tree, &nconf->force, force_entry) != NULL) {
			free(force_entry);
			fatalx("%s: IMSG_RECONF_FORCE duplicate entry",
			    __func__);
		}
		break;
	default:
		log_debug("%s: error handling imsg %d", __func__,
		    imsg->hdr.type);
		break;
	}
}
