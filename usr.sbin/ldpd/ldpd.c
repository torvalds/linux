/*	$OpenBSD: ldpd.c,v 1.79 2024/11/21 13:38:14 claudio Exp $ */

/*
 * Copyright (c) 2013, 2016 Renato Westphal <renato@openbsd.org>
 * Copyright (c) 2005 Claudio Jeker <claudio@openbsd.org>
 * Copyright (c) 2004, 2008 Esben Norby <norby@openbsd.org>
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
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

#include "ldpd.h"
#include "ldpe.h"
#include "lde.h"
#include "log.h"

static void		 main_sig_handler(int, short, void *);
static __dead void	 usage(void);
static __dead void	 ldpd_shutdown(void);
static pid_t		 start_child(enum ldpd_process, char *, int, int, int,
			    char *);
static void		 main_dispatch_ldpe(int, short, void *);
static void		 main_dispatch_lde(int, short, void *);
static int		 main_imsg_compose_both(enum imsg_type, void *,
			    uint16_t);
static int		 main_imsg_send_ipc_sockets(struct imsgbuf *,
			    struct imsgbuf *);
static void		 main_imsg_send_net_sockets(int);
static void		 main_imsg_send_net_socket(int, enum socket_type);
static int		 main_imsg_send_config(struct ldpd_conf *);
static int		 ldp_reload(void);
static void		 merge_global(struct ldpd_conf *, struct ldpd_conf *);
static void		 merge_af(int, struct ldpd_af_conf *,
			    struct ldpd_af_conf *);
static void		 merge_ifaces(struct ldpd_conf *, struct ldpd_conf *);
static void		 merge_iface_af(struct iface_af *, struct iface_af *);
static void		 merge_tnbrs(struct ldpd_conf *, struct ldpd_conf *);
static void		 merge_nbrps(struct ldpd_conf *, struct ldpd_conf *);
static void		 merge_l2vpns(struct ldpd_conf *, struct ldpd_conf *);
static void		 merge_l2vpn(struct ldpd_conf *, struct l2vpn *,
			    struct l2vpn *);
static void		 merge_auths(struct ldpd_conf *, struct ldpd_conf *);

enum ldpd_process	 ldpd_process;
struct ldpd_global	 global;
struct ldpd_conf	*ldpd_conf;

static char		*conffile;
static struct imsgev	*iev_ldpe;
static struct imsgev	*iev_lde;
static pid_t		 ldpe_pid;
static pid_t		 lde_pid;

static void
main_sig_handler(int sig, short event, void *arg)
{
	/* signal handler rules don't apply, libevent decouples for us */
	switch (sig) {
	case SIGTERM:
	case SIGINT:
		ldpd_shutdown();
		/* NOTREACHED */
	case SIGHUP:
		if (ldp_reload() == -1)
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

	fprintf(stderr, "usage: %s [-dnv] [-D macro=value] [-f file]"
	    " [-s socket]\n", __progname);
	exit(1);
}

int
main(int argc, char *argv[])
{
	struct event		 ev_sigint, ev_sigterm, ev_sighup;
	char			*saved_argv0;
	int			 ch;
	int			 debug = 0, lflag = 0, eflag = 0;
	char			*sockname;
	int			 pipe_parent2ldpe[2];
	int			 pipe_parent2lde[2];

	conffile = CONF_FILE;
	ldpd_process = PROC_MAIN;
	log_procname = "parent";
	sockname = LDPD_SOCKET;

	log_init(1);	/* log to stderr until daemonized */
	log_verbose(1);

	saved_argv0 = argv[0];
	if (saved_argv0 == NULL)
		saved_argv0 = "ldpd";

	while ((ch = getopt(argc, argv, "dD:f:ns:vLE")) != -1) {
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
			global.cmd_opts |= LDPD_OPT_NOACTION;
			break;
		case 's':
			sockname = optarg;
			break;
		case 'v':
			if (global.cmd_opts & LDPD_OPT_VERBOSE)
				global.cmd_opts |= LDPD_OPT_VERBOSE2;
			global.cmd_opts |= LDPD_OPT_VERBOSE;
			break;
		case 'L':
			lflag = 1;
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
	if (argc > 0 || (lflag && eflag))
		usage();

	if (lflag)
		lde(debug, global.cmd_opts & LDPD_OPT_VERBOSE);
	else if (eflag)
		ldpe(debug, global.cmd_opts & LDPD_OPT_VERBOSE, sockname);

	/* fetch interfaces early */
	kif_init();

	/* parse config file */
	if ((ldpd_conf = parse_config(conffile)) == NULL ) {
		kif_clear();
		exit(1);
	}

	if (global.cmd_opts & LDPD_OPT_NOACTION) {
		if (global.cmd_opts & LDPD_OPT_VERBOSE)
			print_config(ldpd_conf);
		else
			fprintf(stderr, "configuration OK\n");
		kif_clear();
		exit(0);
	}

	/* check for root privileges  */
	if (geteuid())
		errx(1, "need root privileges");

	/* check for ldpd user */
	if (getpwnam(LDPD_USER) == NULL)
		errx(1, "unknown user %s", LDPD_USER);

	log_init(debug);
	log_verbose(global.cmd_opts & (LDPD_OPT_VERBOSE | LDPD_OPT_VERBOSE2));

	if (!debug)
		daemon(1, 0);

	log_info("startup");

	if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC,
	    PF_UNSPEC, pipe_parent2ldpe) == -1)
		fatal("socketpair");
	if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC,
	    PF_UNSPEC, pipe_parent2lde) == -1)
		fatal("socketpair");

	/* start children */
	lde_pid = start_child(PROC_LDE_ENGINE, saved_argv0,
	    pipe_parent2lde[1], debug, global.cmd_opts & LDPD_OPT_VERBOSE,
	    NULL);
	ldpe_pid = start_child(PROC_LDP_ENGINE, saved_argv0,
	    pipe_parent2ldpe[1], debug, global.cmd_opts & LDPD_OPT_VERBOSE,
	    sockname);

	if (unveil("/", "r") == -1)
		fatal("unveil /");
	if (unveil(NULL, NULL) == -1)
		fatal("unveil");

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
	if ((iev_ldpe = malloc(sizeof(struct imsgev))) == NULL ||
	    (iev_lde = malloc(sizeof(struct imsgev))) == NULL)
		fatal(NULL);
	if (imsgbuf_init(&iev_ldpe->ibuf, pipe_parent2ldpe[0]) == -1)
		fatal(NULL);
	imsgbuf_allow_fdpass(&iev_ldpe->ibuf);
	iev_ldpe->handler = main_dispatch_ldpe;
	if (imsgbuf_init(&iev_lde->ibuf, pipe_parent2lde[0]) == -1)
		fatal(NULL);
	imsgbuf_allow_fdpass(&iev_lde->ibuf);
	iev_lde->handler = main_dispatch_lde;

	/* setup event handler */
	iev_ldpe->events = EV_READ;
	event_set(&iev_ldpe->ev, iev_ldpe->ibuf.fd, iev_ldpe->events,
	    iev_ldpe->handler, iev_ldpe);
	event_add(&iev_ldpe->ev, NULL);

	iev_lde->events = EV_READ;
	event_set(&iev_lde->ev, iev_lde->ibuf.fd, iev_lde->events,
	    iev_lde->handler, iev_lde);
	event_add(&iev_lde->ev, NULL);

	if (main_imsg_send_ipc_sockets(&iev_ldpe->ibuf, &iev_lde->ibuf))
		fatal("could not establish imsg links");
	main_imsg_send_config(ldpd_conf);

	if (kr_init(!(ldpd_conf->flags & F_LDPD_NO_FIB_UPDATE),
	    ldpd_conf->rdomain) == -1)
		fatalx("kr_init failed");

	/* notify ldpe about existing interfaces and addresses */
	kif_redistribute(NULL);

	if (ldpd_conf->ipv4.flags & F_LDPD_AF_ENABLED)
		main_imsg_send_net_sockets(AF_INET);
	if (ldpd_conf->ipv6.flags & F_LDPD_AF_ENABLED)
		main_imsg_send_net_sockets(AF_INET6);

	/* remove unneeded stuff from config */
		/* ... */

	event_dispatch();

	ldpd_shutdown();
	/* NOTREACHED */
	return (0);
}

static __dead void
ldpd_shutdown(void)
{
	pid_t		 pid;
	int		 status;

	/* close pipes */
	imsgbuf_clear(&iev_ldpe->ibuf);
	close(iev_ldpe->ibuf.fd);
	imsgbuf_clear(&iev_lde->ibuf);
	close(iev_lde->ibuf.fd);

	kr_shutdown();
	config_clear(ldpd_conf);

	log_debug("waiting for children to terminate");
	do {
		pid = wait(&status);
		if (pid == -1) {
			if (errno != EINTR && errno != ECHILD)
				fatal("wait");
		} else if (WIFSIGNALED(status))
			log_warnx("%s terminated; signal %d",
			    (pid == lde_pid) ? "label decision engine" :
			    "ldp engine", WTERMSIG(status));
	} while (pid != -1 || (pid == -1 && errno == EINTR));

	free(iev_ldpe);
	free(iev_lde);

	log_info("terminating");
	exit(0);
}

static pid_t
start_child(enum ldpd_process p, char *argv0, int fd, int debug, int verbose,
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
	case PROC_LDE_ENGINE:
		argv[argc++] = "-L";
		break;
	case PROC_LDP_ENGINE:
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
main_dispatch_ldpe(int fd, short event, void *bula)
{
	struct imsgev		*iev = bula;
	struct imsgbuf		*ibuf = &iev->ibuf;
	struct imsg		 imsg;
	int			 af;
	ssize_t			 n;
	int			 shut = 0, verbose;

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
		case IMSG_REQUEST_SOCKETS:
			af = imsg.hdr.pid;
			main_imsg_send_net_sockets(af);
			break;
		case IMSG_CTL_RELOAD:
			if (ldp_reload() == -1)
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
		case IMSG_CTL_KROUTE_ADDR:
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
			/* already checked by ldpe */
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
main_dispatch_lde(int fd, short event, void *bula)
{
	struct imsgev	*iev = bula;
	struct imsgbuf	*ibuf = &iev->ibuf;
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
		case IMSG_KLABEL_CHANGE:
			if (imsg.hdr.len - IMSG_HEADER_SIZE !=
			    sizeof(struct kroute))
				fatalx("invalid size of IMSG_KLABEL_CHANGE");
			if (kr_change(imsg.data))
				log_warnx("%s: error changing route", __func__);
			break;
		case IMSG_KLABEL_DELETE:
			if (imsg.hdr.len - IMSG_HEADER_SIZE !=
			    sizeof(struct kroute))
				fatalx("invalid size of IMSG_KLABEL_DELETE");
			if (kr_delete(imsg.data))
				log_warnx("%s: error deleting route", __func__);
			break;
		case IMSG_KPWLABEL_CHANGE:
			if (imsg.hdr.len - IMSG_HEADER_SIZE !=
			    sizeof(struct kpw))
				fatalx("invalid size of IMSG_KPWLABEL_CHANGE");
			if (kmpw_set(imsg.data))
				log_warnx("%s: error changing pseudowire",
				    __func__);
			break;
		case IMSG_KPWLABEL_DELETE:
			if (imsg.hdr.len - IMSG_HEADER_SIZE !=
			    sizeof(struct kpw))
				fatalx("invalid size of IMSG_KPWLABEL_DELETE");
			if (kmpw_unset(imsg.data))
				log_warnx("%s: error unsetting pseudowire",
				    __func__);
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

void
main_imsg_compose_ldpe(int type, pid_t pid, void *data, uint16_t datalen)
{
	if (iev_ldpe == NULL)
		return;
	imsg_compose_event(iev_ldpe, type, 0, pid, -1, data, datalen);
}

void
main_imsg_compose_lde(int type, pid_t pid, void *data, uint16_t datalen)
{
	imsg_compose_event(iev_lde, type, 0, pid, -1, data, datalen);
}

static int
main_imsg_compose_both(enum imsg_type type, void *buf, uint16_t len)
{
	if (imsg_compose_event(iev_ldpe, type, 0, 0, -1, buf, len) == -1)
		return (-1);
	if (imsg_compose_event(iev_lde, type, 0, 0, -1, buf, len) == -1)
		return (-1);
	return (0);
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

void
evbuf_enqueue(struct evbuf *eb, struct ibuf *buf)
{
	ibuf_close(eb->wbuf, buf);
	evbuf_event_add(eb);
}

void
evbuf_event_add(struct evbuf *eb)
{
	if (msgbuf_queuelen(eb->wbuf) > 0)
		event_add(&eb->ev, NULL);
}

void
evbuf_init(struct evbuf *eb, int fd, void (*handler)(int, short, void *),
    void *arg)
{
	if (eb->wbuf != NULL)
		fatalx("evbuf_init: msgbuf already set");
	if ((eb->wbuf = msgbuf_new()) == NULL)
		fatal(__func__);
	event_set(&eb->ev, fd, EV_WRITE, handler, arg);
}

void
evbuf_clear(struct evbuf *eb)
{
	event_del(&eb->ev);
	msgbuf_free(eb->wbuf);
	eb->wbuf = NULL;
}

static int
main_imsg_send_ipc_sockets(struct imsgbuf *ldpe_buf, struct imsgbuf *lde_buf)
{
	int pipe_ldpe2lde[2];

	if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK,
	    PF_UNSPEC, pipe_ldpe2lde) == -1)
		return (-1);

	if (imsg_compose(ldpe_buf, IMSG_SOCKET_IPC, 0, 0, pipe_ldpe2lde[0],
	    NULL, 0) == -1)
		return (-1);
	if (imsg_compose(lde_buf, IMSG_SOCKET_IPC, 0, 0, pipe_ldpe2lde[1],
	    NULL, 0) == -1)
		return (-1);

	return (0);
}

static void
main_imsg_send_net_sockets(int af)
{
	main_imsg_send_net_socket(af, LDP_SOCKET_DISC);
	main_imsg_send_net_socket(af, LDP_SOCKET_EDISC);
	main_imsg_send_net_socket(af, LDP_SOCKET_SESSION);
	imsg_compose_event(iev_ldpe, IMSG_SETUP_SOCKETS, af, 0, -1, NULL, 0);
}

static void
main_imsg_send_net_socket(int af, enum socket_type type)
{
	int			 fd;

	fd = ldp_create_socket(af, type);
	if (fd == -1) {
		log_warnx("%s: failed to create %s socket for address-family "
		    "%s", __func__, socket_name(type), af_name(af));
		return;
	}

	imsg_compose_event(iev_ldpe, IMSG_SOCKET_NET, af, 0, fd, &type,
	    sizeof(type));
}

struct ldpd_af_conf *
ldp_af_conf_get(struct ldpd_conf *xconf, int af)
{
	switch (af) {
	case AF_INET:
		return (&xconf->ipv4);
	case AF_INET6:
		return (&xconf->ipv6);
	default:
		fatalx("ldp_af_conf_get: unknown af");
	}
}

struct ldpd_af_global *
ldp_af_global_get(struct ldpd_global *xglobal, int af)
{
	switch (af) {
	case AF_INET:
		return (&xglobal->ipv4);
	case AF_INET6:
		return (&xglobal->ipv6);
	default:
		fatalx("ldp_af_global_get: unknown af");
	}
}

int
ldp_is_dual_stack(struct ldpd_conf *xconf)
{
	return ((xconf->ipv4.flags & F_LDPD_AF_ENABLED) &&
	    (xconf->ipv6.flags & F_LDPD_AF_ENABLED));
}

static int
main_imsg_send_config(struct ldpd_conf *xconf)
{
	struct iface		*iface;
	struct tnbr		*tnbr;
	struct nbr_params	*nbrp;
	struct l2vpn		*l2vpn;
	struct l2vpn_if		*lif;
	struct l2vpn_pw		*pw;
	struct ldp_auth		*auth;

	if (main_imsg_compose_both(IMSG_RECONF_CONF, xconf,
	    sizeof(*xconf)) == -1)
		return (-1);

	LIST_FOREACH(auth, &xconf->auth_list, entry) {
		if (main_imsg_compose_both(IMSG_RECONF_CONF_AUTH,
		    auth, sizeof(*auth)) == -1)
			return (-1);
	}

	LIST_FOREACH(iface, &xconf->iface_list, entry) {
		if (main_imsg_compose_both(IMSG_RECONF_IFACE, iface,
		    sizeof(*iface)) == -1)
			return (-1);
	}

	LIST_FOREACH(tnbr, &xconf->tnbr_list, entry) {
		if (main_imsg_compose_both(IMSG_RECONF_TNBR, tnbr,
		    sizeof(*tnbr)) == -1)
			return (-1);
	}

	LIST_FOREACH(nbrp, &xconf->nbrp_list, entry) {
		if (main_imsg_compose_both(IMSG_RECONF_NBRP, nbrp,
		    sizeof(*nbrp)) == -1)
			return (-1);
	}

	LIST_FOREACH(l2vpn, &xconf->l2vpn_list, entry) {
		if (main_imsg_compose_both(IMSG_RECONF_L2VPN, l2vpn,
		    sizeof(*l2vpn)) == -1)
			return (-1);

		LIST_FOREACH(lif, &l2vpn->if_list, entry) {
			if (main_imsg_compose_both(IMSG_RECONF_L2VPN_IF, lif,
			    sizeof(*lif)) == -1)
				return (-1);
		}
		LIST_FOREACH(pw, &l2vpn->pw_list, entry) {
			if (main_imsg_compose_both(IMSG_RECONF_L2VPN_PW, pw,
			    sizeof(*pw)) == -1)
				return (-1);
		}
	}

	if (main_imsg_compose_both(IMSG_RECONF_END, NULL, 0) == -1)
		return (-1);

	return (0);
}

static int
ldp_reload(void)
{
	struct ldpd_conf	*xconf;

	if ((xconf = parse_config(conffile)) == NULL)
		return (-1);

	if (main_imsg_send_config(xconf) == -1)
		return (-1);

	merge_config(ldpd_conf, xconf);

	return (0);
}

void
merge_config(struct ldpd_conf *conf, struct ldpd_conf *xconf)
{
	merge_global(conf, xconf);
	merge_auths(conf, xconf);
	merge_af(AF_INET, &conf->ipv4, &xconf->ipv4);
	merge_af(AF_INET6, &conf->ipv6, &xconf->ipv6);
	merge_ifaces(conf, xconf);
	merge_tnbrs(conf, xconf);
	merge_nbrps(conf, xconf);
	merge_l2vpns(conf, xconf);
	free(xconf);
}

static void
merge_global(struct ldpd_conf *conf, struct ldpd_conf *xconf)
{
	/* change of router-id requires resetting all neighborships */
	if (conf->rtr_id.s_addr != xconf->rtr_id.s_addr) {
		if (ldpd_process == PROC_LDP_ENGINE) {
			ldpe_reset_nbrs(AF_INET);
			ldpe_reset_nbrs(AF_INET6);
			if (conf->rtr_id.s_addr == INADDR_ANY ||
			    xconf->rtr_id.s_addr == INADDR_ANY) {
				if_update_all(AF_UNSPEC);
				tnbr_update_all(AF_UNSPEC);
			}
		}
		conf->rtr_id = xconf->rtr_id;
	}

	conf->rdomain= xconf->rdomain;

	if (conf->trans_pref != xconf->trans_pref) {
		if (ldpd_process == PROC_LDP_ENGINE)
			ldpe_reset_ds_nbrs();
		conf->trans_pref = xconf->trans_pref;
	}

	if ((conf->flags & F_LDPD_DS_CISCO_INTEROP) !=
	    (xconf->flags & F_LDPD_DS_CISCO_INTEROP)) {
		if (ldpd_process == PROC_LDP_ENGINE)
			ldpe_reset_ds_nbrs();
	}

	conf->flags = xconf->flags;
}

static void
merge_af(int af, struct ldpd_af_conf *af_conf, struct ldpd_af_conf *xa)
{
	int			 egress_label_changed = 0;
	int			 update_sockets = 0;

	if (af_conf->keepalive != xa->keepalive) {
		af_conf->keepalive = xa->keepalive;
		if (ldpd_process == PROC_LDP_ENGINE)
			ldpe_stop_init_backoff(af);
	}
	af_conf->thello_holdtime = xa->thello_holdtime;
	af_conf->thello_interval = xa->thello_interval;

	/* update flags */
	if (ldpd_process == PROC_LDP_ENGINE &&
	    (af_conf->flags & F_LDPD_AF_THELLO_ACCEPT) &&
	    !(xa->flags & F_LDPD_AF_THELLO_ACCEPT))
		ldpe_remove_dynamic_tnbrs(af);

	if ((af_conf->flags & F_LDPD_AF_NO_GTSM) !=
	    (xa->flags & F_LDPD_AF_NO_GTSM)) {
		if (af == AF_INET6)
			/* need to set/unset IPV6_MINHOPCOUNT */
			update_sockets = 1;
		else if (ldpd_process == PROC_LDP_ENGINE)
			/* for LDPv4 just resetting the neighbors is enough */
			ldpe_reset_nbrs(af);
	}

	if ((af_conf->flags & F_LDPD_AF_EXPNULL) !=
	    (xa->flags & F_LDPD_AF_EXPNULL))
		egress_label_changed = 1;

	af_conf->flags = xa->flags;

	if (egress_label_changed) {
		switch (ldpd_process) {
		case PROC_LDE_ENGINE:
			lde_change_egress_label(af, af_conf->flags &
			    F_LDPD_AF_EXPNULL);
			break;
		case PROC_MAIN:
			kr_change_egress_label(af, af_conf->flags &
			    F_LDPD_AF_EXPNULL);
			break;
		default:
			break;
		}
	}

	if (ldp_addrcmp(af, &af_conf->trans_addr, &xa->trans_addr)) {
		af_conf->trans_addr = xa->trans_addr;
		update_sockets = 1;
	}

	if (ldpd_process == PROC_MAIN && update_sockets)
		imsg_compose_event(iev_ldpe, IMSG_CLOSE_SOCKETS, af, 0, -1,
		    NULL, 0);
}

static void
merge_ifaces(struct ldpd_conf *conf, struct ldpd_conf *xconf)
{
	struct iface		*iface, *itmp, *xi;

	LIST_FOREACH_SAFE(iface, &conf->iface_list, entry, itmp) {
		/* find deleted interfaces */
		if ((xi = if_lookup(xconf, iface->ifindex)) == NULL) {
			LIST_REMOVE(iface, entry);
			if (ldpd_process == PROC_LDP_ENGINE)
				if_exit(iface);
			free(iface);
		}
	}
	LIST_FOREACH_SAFE(xi, &xconf->iface_list, entry, itmp) {
		/* find new interfaces */
		if ((iface = if_lookup(conf, xi->ifindex)) == NULL) {
			LIST_REMOVE(xi, entry);
			LIST_INSERT_HEAD(&conf->iface_list, xi, entry);

			/* resend addresses to activate new interfaces */
			if (ldpd_process == PROC_MAIN)
				kif_redistribute(xi->name);
			continue;
		}

		/* update existing interfaces */
		merge_iface_af(&iface->ipv4, &xi->ipv4);
		merge_iface_af(&iface->ipv6, &xi->ipv6);
		LIST_REMOVE(xi, entry);
		free(xi);
	}
}

static void
merge_iface_af(struct iface_af *ia, struct iface_af *xi)
{
	if (ia->enabled != xi->enabled) {
		ia->enabled = xi->enabled;
		if (ldpd_process == PROC_LDP_ENGINE)
			if_update(ia->iface, ia->af);
	}
	ia->hello_holdtime = xi->hello_holdtime;
	ia->hello_interval = xi->hello_interval;
}

static void
merge_tnbrs(struct ldpd_conf *conf, struct ldpd_conf *xconf)
{
	struct tnbr		*tnbr, *ttmp, *xt;

	LIST_FOREACH_SAFE(tnbr, &conf->tnbr_list, entry, ttmp) {
		if (!(tnbr->flags & F_TNBR_CONFIGURED))
			continue;

		/* find deleted tnbrs */
		if ((xt = tnbr_find(xconf, tnbr->af, &tnbr->addr)) == NULL) {
			if (ldpd_process == PROC_LDP_ENGINE) {
				tnbr->flags &= ~F_TNBR_CONFIGURED;
				tnbr_check(tnbr);
			} else {
				LIST_REMOVE(tnbr, entry);
				free(tnbr);
			}
		}
	}
	LIST_FOREACH_SAFE(xt, &xconf->tnbr_list, entry, ttmp) {
		/* find new tnbrs */
		if ((tnbr = tnbr_find(conf, xt->af, &xt->addr)) == NULL) {
			LIST_REMOVE(xt, entry);
			LIST_INSERT_HEAD(&conf->tnbr_list, xt, entry);

			if (ldpd_process == PROC_LDP_ENGINE)
				tnbr_update(xt);
			continue;
		}

		/* update existing tnbrs */
		if (!(tnbr->flags & F_TNBR_CONFIGURED))
			tnbr->flags |= F_TNBR_CONFIGURED;
		tnbr->hello_holdtime = xt->hello_holdtime;
		tnbr->hello_interval = xt->hello_interval;
		LIST_REMOVE(xt, entry);
		free(xt);
	}
}

static void
merge_nbrps(struct ldpd_conf *conf, struct ldpd_conf *xconf)
{
	struct nbr_params	*nbrp, *ntmp, *xn;
	struct nbr		*nbr;
	int			 nbrp_changed;

	LIST_FOREACH_SAFE(nbrp, &conf->nbrp_list, entry, ntmp) {
		/* find deleted nbrps */
		if ((xn = nbr_params_find(xconf, nbrp->lsr_id)) == NULL) {
			if (ldpd_process == PROC_LDP_ENGINE) {
				nbr = nbr_find_ldpid(nbrp->lsr_id.s_addr);
				if (nbr) {
					session_shutdown(nbr, S_SHUTDOWN, 0, 0);
					pfkey_remove(nbr);
					if (nbr_session_active_role(nbr))
						nbr_establish_connection(nbr);
				}
			}
			LIST_REMOVE(nbrp, entry);
			free(nbrp);
		}
	}
	LIST_FOREACH_SAFE(xn, &xconf->nbrp_list, entry, ntmp) {
		/* find new nbrps */
		if ((nbrp = nbr_params_find(conf, xn->lsr_id)) == NULL) {
			LIST_REMOVE(xn, entry);
			LIST_INSERT_HEAD(&conf->nbrp_list, xn, entry);

			if (ldpd_process == PROC_LDP_ENGINE) {
				nbr = nbr_find_ldpid(xn->lsr_id.s_addr);
				if (nbr) {
					session_shutdown(nbr, S_SHUTDOWN, 0, 0);
					if (pfkey_establish(conf, nbr) == -1)
						fatalx("pfkey setup failed");
					if (nbr_session_active_role(nbr))
						nbr_establish_connection(nbr);
				}
			}
			continue;
		}

		/* update existing nbrps */
		if (nbrp->flags != xn->flags ||
		    nbrp->keepalive != xn->keepalive ||
		    nbrp->gtsm_enabled != xn->gtsm_enabled ||
		    nbrp->gtsm_hops != xn->gtsm_hops)
			nbrp_changed = 1;
		else
			nbrp_changed = 0;

		nbrp->keepalive = xn->keepalive;
		nbrp->gtsm_enabled = xn->gtsm_enabled;
		nbrp->gtsm_hops = xn->gtsm_hops;
		nbrp->flags = xn->flags;

		if (ldpd_process == PROC_LDP_ENGINE) {
			nbr = nbr_find_ldpid(nbrp->lsr_id.s_addr);
			if (nbr && nbrp_changed) {
				session_shutdown(nbr, S_SHUTDOWN, 0, 0);
				pfkey_remove(nbr);
				if (pfkey_establish(conf, nbr) == -1)
					fatalx("pfkey setup failed");
				if (nbr_session_active_role(nbr))
					nbr_establish_connection(nbr);
			}
		}
		LIST_REMOVE(xn, entry);
		free(xn);
	}
}

static void
merge_l2vpns(struct ldpd_conf *conf, struct ldpd_conf *xconf)
{
	struct l2vpn		*l2vpn, *ltmp, *xl;

	LIST_FOREACH_SAFE(l2vpn, &conf->l2vpn_list, entry, ltmp) {
		/* find deleted l2vpns */
		if ((xl = l2vpn_find(xconf, l2vpn->name)) == NULL) {
			LIST_REMOVE(l2vpn, entry);

			switch (ldpd_process) {
			case PROC_LDE_ENGINE:
				l2vpn_exit(l2vpn);
				break;
			case PROC_LDP_ENGINE:
				ldpe_l2vpn_exit(l2vpn);
				break;
			case PROC_MAIN:
				break;
			}
			l2vpn_del(l2vpn);
		}
	}
	LIST_FOREACH_SAFE(xl, &xconf->l2vpn_list, entry, ltmp) {
		/* find new l2vpns */
		if ((l2vpn = l2vpn_find(conf, xl->name)) == NULL) {
			LIST_REMOVE(xl, entry);
			LIST_INSERT_HEAD(&conf->l2vpn_list, xl, entry);

			switch (ldpd_process) {
			case PROC_LDE_ENGINE:
				l2vpn_init(xl);
				break;
			case PROC_LDP_ENGINE:
				ldpe_l2vpn_init(xl);
				break;
			case PROC_MAIN:
				break;
			}
			continue;
		}

		/* update existing l2vpns */
		merge_l2vpn(conf, l2vpn, xl);
		LIST_REMOVE(xl, entry);
		free(xl);
	}
}

static void
merge_l2vpn(struct ldpd_conf *xconf, struct l2vpn *l2vpn, struct l2vpn *xl)
{
	struct l2vpn_if		*lif, *ftmp, *xf;
	struct l2vpn_pw		*pw, *ptmp, *xp;
	struct nbr		*nbr;
	int			 reset_nbr, reinstall_pwfec, reinstall_tnbr;
	int			 previous_pw_type, previous_mtu;

	previous_pw_type = l2vpn->pw_type;
	previous_mtu = l2vpn->mtu;

	/* merge intefaces */
	LIST_FOREACH_SAFE(lif, &l2vpn->if_list, entry, ftmp) {
		/* find deleted interfaces */
		if ((xf = l2vpn_if_find(xl, lif->ifindex)) == NULL) {
			LIST_REMOVE(lif, entry);
			free(lif);
		}
	}
	LIST_FOREACH_SAFE(xf, &xl->if_list, entry, ftmp) {
		/* find new interfaces */
		if ((lif = l2vpn_if_find(l2vpn, xf->ifindex)) == NULL) {
			LIST_REMOVE(xf, entry);
			LIST_INSERT_HEAD(&l2vpn->if_list, xf, entry);
			xf->l2vpn = l2vpn;
			continue;
		}

		LIST_REMOVE(xf, entry);
		free(xf);
	}

	/* merge pseudowires */
	LIST_FOREACH_SAFE(pw, &l2vpn->pw_list, entry, ptmp) {
		/* find deleted pseudowires */
		if ((xp = l2vpn_pw_find(xl, pw->ifindex)) == NULL) {
			switch (ldpd_process) {
			case PROC_LDE_ENGINE:
				l2vpn_pw_exit(pw);
				break;
			case PROC_LDP_ENGINE:
				ldpe_l2vpn_pw_exit(pw);
				break;
			case PROC_MAIN:
				break;
			}

			LIST_REMOVE(pw, entry);
			free(pw);
		}
	}
	LIST_FOREACH_SAFE(xp, &xl->pw_list, entry, ptmp) {
		/* find new pseudowires */
		if ((pw = l2vpn_pw_find(l2vpn, xp->ifindex)) == NULL) {
			LIST_REMOVE(xp, entry);
			LIST_INSERT_HEAD(&l2vpn->pw_list, xp, entry);
			xp->l2vpn = l2vpn;

			switch (ldpd_process) {
			case PROC_LDE_ENGINE:
				l2vpn_pw_init(xp);
				break;
			case PROC_LDP_ENGINE:
				ldpe_l2vpn_pw_init(xp);
				break;
			case PROC_MAIN:
				break;
			}
			continue;
		}

		/* update existing pseudowire */
    		if (pw->af != xp->af ||
		    ldp_addrcmp(pw->af, &pw->addr, &xp->addr))
			reinstall_tnbr = 1;
		else
			reinstall_tnbr = 0;

		/* changes that require a session restart */
		if ((pw->flags & (F_PW_STATUSTLV_CONF|F_PW_CWORD_CONF)) !=
		    (xp->flags & (F_PW_STATUSTLV_CONF|F_PW_CWORD_CONF)))
			reset_nbr = 1;
		else
			reset_nbr = 0;

		if (l2vpn->pw_type != xl->pw_type || l2vpn->mtu != xl->mtu ||
		    pw->pwid != xp->pwid || reinstall_tnbr || reset_nbr ||
		    pw->lsr_id.s_addr != xp->lsr_id.s_addr)
			reinstall_pwfec = 1;
		else
			reinstall_pwfec = 0;

		if (ldpd_process == PROC_LDP_ENGINE) {
			if (reinstall_tnbr)
				ldpe_l2vpn_pw_exit(pw);
			if (reset_nbr) {
				nbr = nbr_find_ldpid(pw->lsr_id.s_addr);
				if (nbr && nbr->state == NBR_STA_OPER)
					session_shutdown(nbr, S_SHUTDOWN, 0, 0);
			}
		}
		if (ldpd_process == PROC_LDE_ENGINE &&
		    !reset_nbr && reinstall_pwfec)
			l2vpn_pw_exit(pw);
		pw->lsr_id = xp->lsr_id;
		pw->af = xp->af;
		pw->addr = xp->addr;
		pw->pwid = xp->pwid;
		strlcpy(pw->ifname, xp->ifname, sizeof(pw->ifname));
		pw->ifindex = xp->ifindex;
		if (xp->flags & F_PW_CWORD_CONF)
			pw->flags |= F_PW_CWORD_CONF;
		else
			pw->flags &= ~F_PW_CWORD_CONF;
		if (xp->flags & F_PW_STATUSTLV_CONF)
			pw->flags |= F_PW_STATUSTLV_CONF;
		else
			pw->flags &= ~F_PW_STATUSTLV_CONF;
		if (ldpd_process == PROC_LDP_ENGINE && reinstall_tnbr)
			ldpe_l2vpn_pw_init(pw);
		if (ldpd_process == PROC_LDE_ENGINE &&
		    !reset_nbr && reinstall_pwfec) {
			l2vpn->pw_type = xl->pw_type;
			l2vpn->mtu = xl->mtu;
			l2vpn_pw_init(pw);
			l2vpn->pw_type = previous_pw_type;
			l2vpn->mtu = previous_mtu;
		}

		LIST_REMOVE(xp, entry);
		free(xp);
	}

	l2vpn->pw_type = xl->pw_type;
	l2vpn->mtu = xl->mtu;
	strlcpy(l2vpn->br_ifname, xl->br_ifname, sizeof(l2vpn->br_ifname));
	l2vpn->br_ifindex = xl->br_ifindex;
}

static struct ldp_auth *
auth_find(struct ldpd_conf *conf, const struct ldp_auth *needle)
{
	struct ldp_auth *auth;

	LIST_FOREACH(auth, &conf->auth_list, entry) {
		in_addr_t mask;
		if (needle->md5key_len != auth->md5key_len)
			continue;
		if (needle->idlen != auth->idlen)
			continue;

		if (memcmp(needle->md5key, auth->md5key,
		    needle->md5key_len) != 0)
			continue;

		mask = prefixlen2mask(auth->idlen);
		if ((needle->id.s_addr & mask) != (auth->id.s_addr & mask))
			continue;

		return (auth);
	}

	return (NULL);
}

static void
merge_auths(struct ldpd_conf *conf, struct ldpd_conf *xconf)
{
	struct ldp_auth		*auth, *nauth, *xauth;

	/* find deleted auths */
	LIST_FOREACH_SAFE(auth, &conf->auth_list, entry, nauth) {
		xauth = auth_find(xconf, auth);
		if (xauth == NULL)
			continue;

		LIST_REMOVE(auth, entry);

		free(auth);
	}

	/* find new auths */
	LIST_FOREACH_SAFE(xauth, &xconf->auth_list, entry, nauth) {
		LIST_REMOVE(xauth, entry);

		auth = auth_find(conf, xauth);
		if (auth == NULL) {
			LIST_INSERT_HEAD(&conf->auth_list, xauth, entry);
			continue;
		}

		free(xauth);
	}
}

struct ldpd_conf *
config_new_empty(void)
{
	struct ldpd_conf	*xconf;

	xconf = calloc(1, sizeof(*xconf));
	if (xconf == NULL)
		fatal(NULL);

	LIST_INIT(&xconf->iface_list);
	LIST_INIT(&xconf->tnbr_list);
	LIST_INIT(&xconf->nbrp_list);
	LIST_INIT(&xconf->l2vpn_list);
	LIST_INIT(&xconf->auth_list);

	return (xconf);
}

void
config_clear(struct ldpd_conf *conf)
{
	struct ldpd_conf	*xconf;

	/*
	 * Merge current config with an empty config, this will deactivate
	 * and deallocate all the interfaces, pseudowires and so on. Before
	 * merging, copy the router-id and other variables to avoid some
	 * unnecessary operations, like trying to reset the neighborships.
	 */
	xconf = config_new_empty();
	xconf->ipv4 = conf->ipv4;
	xconf->ipv6 = conf->ipv6;
	xconf->rtr_id = conf->rtr_id;
	xconf->trans_pref = conf->trans_pref;
	xconf->flags = conf->flags;
	merge_config(conf, xconf);
	free(conf);
}
