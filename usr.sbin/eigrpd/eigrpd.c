/*	$OpenBSD: eigrpd.c,v 1.36 2024/11/21 13:38:14 claudio Exp $ */

/*
 * Copyright (c) 2015 Renato Westphal <renato@openbsd.org>
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
#include <sys/wait.h>
#include <sys/sysctl.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "eigrpd.h"
#include "eigrpe.h"
#include "rde.h"
#include "log.h"

static void		 main_sig_handler(int, short, void *);
static __dead void	 usage(void);
static __dead void	 eigrpd_shutdown(void);
static pid_t		 start_child(enum eigrpd_process, char *, int, int, int,
			    char *);
static void		 main_dispatch_eigrpe(int, short, void *);
static void		 main_dispatch_rde(int, short, void *);
static int		 main_imsg_send_ipc_sockets(struct imsgbuf *,
			    struct imsgbuf *);
static int		 main_imsg_send_config(struct eigrpd_conf *);
static int		 eigrp_reload(void);
static int		 eigrp_sendboth(enum imsg_type, void *, uint16_t);
static void		 merge_instances(struct eigrpd_conf *, struct eigrp *,
			    struct eigrp *);

struct eigrpd_conf	*eigrpd_conf;

static char		*conffile;
static struct imsgev	*iev_eigrpe;
static struct imsgev	*iev_rde;
static pid_t		 eigrpe_pid;
static pid_t		 rde_pid;

static void
main_sig_handler(int sig, short event, void *arg)
{
	/* signal handler rules don't apply, libevent decouples for us */
	switch (sig) {
	case SIGTERM:
	case SIGINT:
		eigrpd_shutdown();
		/* NOTREACHED */
	case SIGHUP:
		if (eigrp_reload() == -1)
			log_warnx("configuration reload failed");
		else
			log_debug("configuration reloaded");
		break;
	default:
		fatalx("unexpected signal");
		/* NOTREACHED */
	}
}

static __dead void
usage(void)
{
	extern char *__progname;

	fprintf(stderr, "usage: %s [-dnv] [-D macro=value]"
	    " [-f file] [-s socket]\n",
	    __progname);
	exit(1);
}

struct eigrpd_global global;

int
main(int argc, char *argv[])
{
	struct event		 ev_sigint, ev_sigterm, ev_sighup;
	char			*saved_argv0;
	int			 ch;
	int			 debug = 0, rflag = 0, eflag = 0;
	int			 ipforwarding;
	int			 mib[4];
	size_t			 len;
	char			*sockname;
	int			 pipe_parent2eigrpe[2];
	int			 pipe_parent2rde[2];

	conffile = CONF_FILE;
	log_procname = "parent";
	sockname = EIGRPD_SOCKET;

	log_init(1);	/* log to stderr until daemonized */
	log_verbose(1);

	saved_argv0 = argv[0];
	if (saved_argv0 == NULL)
		saved_argv0 = "eigrpd";

	while ((ch = getopt(argc, argv, "dD:f:ns:vRE")) != -1) {
		switch (ch) {
		case 'd':
			debug = 1;
			break;
		case 'D':
			if (cmdline_symset(optarg) < 0)
				log_warnx("could not parse macro definition %s",
				    optarg);
			break;
		case 'f':
			conffile = optarg;
			break;
		case 'n':
			global.cmd_opts |= EIGRPD_OPT_NOACTION;
			break;
		case 's':
			sockname = optarg;
			break;
		case 'v':
			if (global.cmd_opts & EIGRPD_OPT_VERBOSE)
				global.cmd_opts |= EIGRPD_OPT_VERBOSE2;
			global.cmd_opts |= EIGRPD_OPT_VERBOSE;
			break;
		case 'R':
			rflag = 1;
			break;
		case 'E':
			eflag = 1;
			break;
		default:
			usage();
			/* NOTREACHED */
		}
	}

	argc -= optind;
	argv += optind;
	if (argc > 0 || (rflag && eflag))
		usage();

	if (rflag)
		rde(debug, global.cmd_opts & EIGRPD_OPT_VERBOSE);
	else if (eflag)
		eigrpe(debug, global.cmd_opts & EIGRPD_OPT_VERBOSE, sockname);

	mib[0] = CTL_NET;
	mib[1] = PF_INET;
	mib[2] = IPPROTO_IP;
	mib[3] = IPCTL_FORWARDING;
	len = sizeof(ipforwarding);
	if (sysctl(mib, 4, &ipforwarding, &len, NULL, 0) == -1)
		log_warn("sysctl");

	if (ipforwarding != 1)
		log_warnx("WARNING: IP forwarding NOT enabled");

	/* fetch interfaces early */
	kif_init();

	/* parse config file */
	if ((eigrpd_conf = parse_config(conffile)) == NULL) {
		kif_clear();
		exit(1);
	}

	if (global.cmd_opts & EIGRPD_OPT_NOACTION) {
		if (global.cmd_opts & EIGRPD_OPT_VERBOSE)
			print_config(eigrpd_conf);
		else
			fprintf(stderr, "configuration OK\n");
		kif_clear();
		exit(0);
	}

	/* check for root privileges  */
	if (geteuid())
		errx(1, "need root privileges");

	/* check for eigrpd user */
	if (getpwnam(EIGRPD_USER) == NULL)
		errx(1, "unknown user %s", EIGRPD_USER);

	log_init(debug);
	log_verbose(global.cmd_opts & EIGRPD_OPT_VERBOSE);

	if (!debug)
		daemon(1, 0);

	log_info("startup");

	if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK,
	    PF_UNSPEC, pipe_parent2eigrpe) == -1)
		fatal("socketpair");
	if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK,
	    PF_UNSPEC, pipe_parent2rde) == -1)
		fatal("socketpair");

	/* start children */
	rde_pid = start_child(PROC_RDE_ENGINE, saved_argv0, pipe_parent2rde[1],
	    debug, global.cmd_opts & EIGRPD_OPT_VERBOSE, NULL);
	eigrpe_pid = start_child(PROC_EIGRP_ENGINE, saved_argv0,
	    pipe_parent2eigrpe[1], debug, global.cmd_opts & EIGRPD_OPT_VERBOSE,
	    sockname);

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
	if ((iev_eigrpe = malloc(sizeof(struct imsgev))) == NULL ||
	    (iev_rde = malloc(sizeof(struct imsgev))) == NULL)
		fatal(NULL);
	if (imsgbuf_init(&iev_eigrpe->ibuf, pipe_parent2eigrpe[0]) == -1)
		fatal(NULL);
	imsgbuf_allow_fdpass(&iev_eigrpe->ibuf);
	iev_eigrpe->handler = main_dispatch_eigrpe;
	if (imsgbuf_init(&iev_rde->ibuf, pipe_parent2rde[0]) == -1)
		fatal(NULL);
	imsgbuf_allow_fdpass(&iev_rde->ibuf);
	iev_rde->handler = main_dispatch_rde;

	/* setup event handler */
	iev_eigrpe->events = EV_READ;
	event_set(&iev_eigrpe->ev, iev_eigrpe->ibuf.fd, iev_eigrpe->events,
	    iev_eigrpe->handler, iev_eigrpe);
	event_add(&iev_eigrpe->ev, NULL);

	iev_rde->events = EV_READ;
	event_set(&iev_rde->ev, iev_rde->ibuf.fd, iev_rde->events,
	    iev_rde->handler, iev_rde);
	event_add(&iev_rde->ev, NULL);

	if (main_imsg_send_ipc_sockets(&iev_eigrpe->ibuf, &iev_rde->ibuf))
		fatal("could not establish imsg links");
	main_imsg_send_config(eigrpd_conf);

	/* notify eigrpe about existing interfaces and addresses */
	kif_redistribute();

	if (kr_init(!(eigrpd_conf->flags & EIGRPD_FLAG_NO_FIB_UPDATE),
	    eigrpd_conf->rdomain) == -1)
		fatalx("kr_init failed");

	if (pledge("stdio rpath inet sendfd", NULL) == -1)
		fatal("pledge");

	event_dispatch();

	eigrpd_shutdown();
	/* NOTREACHED */
	return (0);
}

static __dead void
eigrpd_shutdown(void)
{
	pid_t		 pid;
	int		 status;

	/* close pipes */
	imsgbuf_clear(&iev_eigrpe->ibuf);
	close(iev_eigrpe->ibuf.fd);
	imsgbuf_clear(&iev_rde->ibuf);
	close(iev_rde->ibuf.fd);

	kr_shutdown();
	config_clear(eigrpd_conf, PROC_MAIN);

	log_debug("waiting for children to terminate");
	do {
		pid = wait(&status);
		if (pid == -1) {
			if (errno != EINTR && errno != ECHILD)
				fatal("wait");
		} else if (WIFSIGNALED(status))
			log_warnx("%s terminated; signal %d",
			    (pid == rde_pid) ? "route decision engine" :
			    "eigrp engine", WTERMSIG(status));
	} while (pid != -1 || (pid == -1 && errno == EINTR));

	free(iev_eigrpe);
	free(iev_rde);

	log_info("terminating");
	exit(0);
}

static pid_t
start_child(enum eigrpd_process p, char *argv0, int fd, int debug, int verbose,
    char *sockname)
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
	case PROC_RDE_ENGINE:
		argv[argc++] = "-R";
		break;
	case PROC_EIGRP_ENGINE:
		argv[argc++] = "-E";
		break;
	}
	if (debug)
		argv[argc++] = "-d";
	if (verbose)
		argv[argc++] = "-v";
	if (sockname) {
		argv[argc++] = "-s";
		argv[argc++] = sockname;
	}
	argv[argc++] = NULL;

	execvp(argv0, argv);
	fatal("execvp");
}

/* imsg handling */
static void
main_dispatch_eigrpe(int fd, short event, void *bula)
{
	struct imsgev		*iev = bula;
	struct imsgbuf		*ibuf;
	struct imsg		 imsg;
	ssize_t			 n;
	int			 shut = 0, verbose;

	ibuf = &iev->ibuf;

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
			if (eigrp_reload() == -1)
				log_warnx("configuration reload failed");
			else
				log_debug("configuration reloaded");
			break;
		case IMSG_CTL_FIB_COUPLE:
			kr_fib_couple();
			break;
		case IMSG_CTL_FIB_DECOUPLE:
			kr_fib_decouple();
			break;
		case IMSG_CTL_KROUTE:
			kr_show_route(&imsg);
			break;
		case IMSG_CTL_IFINFO:
			if (imsg.hdr.len == IMSG_HEADER_SIZE)
				kr_ifinfo(NULL, imsg.hdr.pid);
			else if (imsg.hdr.len == IMSG_HEADER_SIZE + IFNAMSIZ)
				kr_ifinfo(imsg.data, imsg.hdr.pid);
			else
				log_warnx("IFINFO request with wrong len");
			break;
		case IMSG_CTL_LOG_VERBOSE:
			/* already checked by eigrpe */
			memcpy(&verbose, imsg.data, sizeof(verbose));
			log_verbose(verbose);
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
		/* this pipe is dead, so remove the event handler */
		event_del(&iev->ev);
		event_loopexit(NULL);
	}
}

static void
main_dispatch_rde(int fd, short event, void *bula)
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
		case IMSG_KROUTE_CHANGE:
			if (imsg.hdr.len - IMSG_HEADER_SIZE !=
			    sizeof(struct kroute))
				fatalx("invalid size of IMSG_KROUTE_CHANGE");
			if (kr_change(imsg.data))
				log_warnx("%s: error changing route", __func__);
			break;
		case IMSG_KROUTE_DELETE:
			if (imsg.hdr.len - IMSG_HEADER_SIZE !=
			    sizeof(struct kroute))
				fatalx("invalid size of IMSG_KROUTE_DELETE");
			if (kr_delete(imsg.data))
				log_warnx("%s: error deleting route", __func__);
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
		/* this pipe is dead, so remove the event handler */
		event_del(&iev->ev);
		event_loopexit(NULL);
	}
}

int
main_imsg_compose_eigrpe(int type, pid_t pid, void *data, uint16_t datalen)
{
	if (iev_eigrpe == NULL)
		return (-1);
	return (imsg_compose_event(iev_eigrpe, type, 0, pid, -1, data, datalen));
}

int
main_imsg_compose_rde(int type, pid_t pid, void *data, uint16_t datalen)
{
	if (iev_rde == NULL)
		return (-1);
	return (imsg_compose_event(iev_rde, type, 0, pid, -1, data, datalen));
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

	if ((ret = imsg_compose(&iev->ibuf, type, peerid,
	    pid, fd, data, datalen)) != -1)
		imsg_event_add(iev);
	return (ret);
}

static int
main_imsg_send_ipc_sockets(struct imsgbuf *eigrpe_buf, struct imsgbuf *rde_buf)
{
	int pipe_eigrpe2rde[2];

	if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK,
	    PF_UNSPEC, pipe_eigrpe2rde) == -1)
		return (-1);

	if (imsg_compose(eigrpe_buf, IMSG_SOCKET_IPC, 0, 0, pipe_eigrpe2rde[0],
	    NULL, 0) == -1)
		return (-1);
	if (imsg_compose(rde_buf, IMSG_SOCKET_IPC, 0, 0, pipe_eigrpe2rde[1],
	    NULL, 0) == -1)
		return (-1);

	return (0);
}

struct eigrp *
eigrp_find(struct eigrpd_conf *xconf, int af, uint16_t as)
{
	struct eigrp	*eigrp;

	TAILQ_FOREACH(eigrp, &xconf->instances, entry)
		if (eigrp->af == af && eigrp->as == as)
			return (eigrp);

	return (NULL);
}

static int
main_imsg_send_config(struct eigrpd_conf *xconf)
{
	struct eigrp		*eigrp;
	struct eigrp_iface	*ei;

	if (eigrp_sendboth(IMSG_RECONF_CONF, xconf, sizeof(*xconf)) == -1)
		return (-1);

	TAILQ_FOREACH(eigrp, &xconf->instances, entry) {
		if (eigrp_sendboth(IMSG_RECONF_INSTANCE, eigrp,
		    sizeof(*eigrp)) == -1)
			return (-1);

		TAILQ_FOREACH(ei, &eigrp->ei_list, e_entry) {
			if (eigrp_sendboth(IMSG_RECONF_IFACE, ei->iface,
			    sizeof(struct iface)) == -1)
				return (-1);

			if (eigrp_sendboth(IMSG_RECONF_EIGRP_IFACE, ei,
			    sizeof(*ei)) == -1)
				return (-1);
		}
	}

	if (eigrp_sendboth(IMSG_RECONF_END, NULL, 0) == -1)
		return (-1);

	return (0);
}

static int
eigrp_reload(void)
{
	struct eigrpd_conf	*xconf;

	if ((xconf = parse_config(conffile)) == NULL)
		return (-1);

	if (main_imsg_send_config(xconf) == -1)
		return (-1);

	merge_config(eigrpd_conf, xconf, PROC_MAIN);

	return (0);
}

static int
eigrp_sendboth(enum imsg_type type, void *buf, uint16_t len)
{
	if (main_imsg_compose_eigrpe(type, 0, buf, len) == -1)
		return (-1);
	if (main_imsg_compose_rde(type, 0, buf, len) == -1)
		return (-1);
	return (0);
}

void
merge_config(struct eigrpd_conf *conf, struct eigrpd_conf *xconf,
    enum eigrpd_process proc)
{
	struct iface		*iface, *itmp, *xi;
	struct eigrp		*eigrp, *etmp, *xe;

	conf->rtr_id = xconf->rtr_id;
	conf->flags = xconf->flags;
	conf->rdomain= xconf->rdomain;
	conf->fib_priority_internal = xconf->fib_priority_internal;
	conf->fib_priority_external = xconf->fib_priority_external;
	conf->fib_priority_summary = xconf->fib_priority_summary;

	/* merge instances */
	TAILQ_FOREACH_SAFE(eigrp, &conf->instances, entry, etmp) {
		/* find deleted instances */
		if ((xe = eigrp_find(xconf, eigrp->af, eigrp->as)) == NULL) {
			TAILQ_REMOVE(&conf->instances, eigrp, entry);

			switch (proc) {
			case PROC_RDE_ENGINE:
				rde_instance_del(eigrp);
				break;
			case PROC_EIGRP_ENGINE:
				eigrpe_instance_del(eigrp);
				break;
			case PROC_MAIN:
				free(eigrp);
				break;
			}
		}
	}
	TAILQ_FOREACH_SAFE(xe, &xconf->instances, entry, etmp) {
		/* find new instances */
		if ((eigrp = eigrp_find(conf, xe->af, xe->as)) == NULL) {
			TAILQ_REMOVE(&xconf->instances, xe, entry);
			TAILQ_INSERT_TAIL(&conf->instances, xe, entry);

			switch (proc) {
			case PROC_RDE_ENGINE:
				rde_instance_init(xe);
				break;
			case PROC_EIGRP_ENGINE:
				eigrpe_instance_init(xe);
				break;
			case PROC_MAIN:
				break;
			}
			continue;
		}

		/* update existing instances */
		merge_instances(conf, eigrp, xe);
	}

	/* merge interfaces */
	TAILQ_FOREACH_SAFE(iface, &conf->iface_list, entry, itmp) {
		/* find deleted ifaces */
		if ((xi = if_lookup(xconf, iface->ifindex)) == NULL) {
			TAILQ_REMOVE(&conf->iface_list, iface, entry);
			free(iface);
		}
	}
	TAILQ_FOREACH_SAFE(xi, &xconf->iface_list, entry, itmp) {
		/* find new ifaces */
		if ((iface = if_lookup(conf, xi->ifindex)) == NULL) {
			TAILQ_REMOVE(&xconf->iface_list, xi, entry);
			TAILQ_INSERT_TAIL(&conf->iface_list, xi, entry);
			continue;
		}

		/* TODO update existing ifaces */
	}	

	/* resend addresses to activate new interfaces */
	if (proc == PROC_MAIN)
		kif_redistribute();

	free(xconf);
}

static void
merge_instances(struct eigrpd_conf *xconf, struct eigrp *eigrp, struct eigrp *xe)
{
	/* TODO */
}

struct eigrpd_conf *
config_new_empty(void)
{
	struct eigrpd_conf	*xconf;

	xconf = calloc(1, sizeof(*xconf));
	if (xconf == NULL)
		fatal(NULL);

	TAILQ_INIT(&xconf->instances);
	TAILQ_INIT(&xconf->iface_list);

	return (xconf);
}

void
config_clear(struct eigrpd_conf *conf, enum eigrpd_process proc)
{
	struct eigrpd_conf	*xconf;

	/* merge current config with an empty config */
	xconf = config_new_empty();
	merge_config(conf, xconf, proc);

	free(conf);
}
