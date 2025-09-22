/*	$OpenBSD: bgpd.c,v 1.283 2025/04/24 20:24:12 claudio Exp $ */

/*
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
#include <sys/wait.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pwd.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include "bgpd.h"
#include "session.h"
#include "log.h"
#include "version.h"

void		sighdlr(int);
__dead void	usage(void);
int		main(int, char *[]);
pid_t		start_child(enum bgpd_process, char *, int, int, int);
int		send_filterset(struct imsgbuf *, struct filter_set_head *);
int		reconfigure(const char *, struct bgpd_config *);
int		send_config(struct bgpd_config *);
int		dispatch_imsg(struct imsgbuf *, int, struct bgpd_config *);
int		control_setup(struct bgpd_config *);
static void	getsockpair(int [2]);
int		imsg_send_sockets(struct imsgbuf *, struct imsgbuf *,
		    struct imsgbuf *);
void		bgpd_rtr_conn_setup(struct rtr_config *);
void		bgpd_rtr_conn_setup_done(int, struct bgpd_config *);
void		bgpd_rtr_conn_teardown(uint32_t);

int			 cflags;
volatile sig_atomic_t	 mrtdump;
volatile sig_atomic_t	 quit;
volatile sig_atomic_t	 reconfig;
pid_t			 reconfpid;
int			 reconfpending;
struct imsgbuf		*ibuf_se;
struct imsgbuf		*ibuf_rde;
struct imsgbuf		*ibuf_rtr;
struct rib_names	 ribnames = SIMPLEQ_HEAD_INITIALIZER(ribnames);
char			*cname;
char			*rcname;

struct connect_elm {
	TAILQ_ENTRY(connect_elm)	entry;
	struct auth_state		auth_state;
	uint32_t			id;
	int				fd;
};

TAILQ_HEAD(, connect_elm)	connect_queue = \
				    TAILQ_HEAD_INITIALIZER(connect_queue),
				socket_queue = \
				    TAILQ_HEAD_INITIALIZER(socket_queue);
u_int				connect_cnt;
#define MAX_CONNECT_CNT		32

void
sighdlr(int sig)
{
	switch (sig) {
	case SIGTERM:
	case SIGINT:
		quit = 1;
		break;
	case SIGHUP:
		reconfig = 1;
		break;
	case SIGALRM:
	case SIGUSR1:
		mrtdump = 1;
		break;
	}
}

__dead void
usage(void)
{
	extern char *__progname;

	fprintf(stderr, "usage: %s [-cdnvV] [-D macro=value] [-f file]\n",
	    __progname);
	exit(1);
}

#define PFD_PIPE_SESSION	0
#define PFD_PIPE_RDE		1
#define PFD_PIPE_RTR		2
#define PFD_SOCK_ROUTE		3
#define PFD_SOCK_PFKEY		4
#define PFD_CONNECT_START	5
#define MAX_TIMEOUT		(3600 * 1000)

int	 cmd_opts;

int
main(int argc, char *argv[])
{
	struct bgpd_config	*conf;
	enum bgpd_process	 proc = PROC_MAIN;
	struct rde_rib		*rr;
	struct peer		*p;
	struct pollfd		*pfd = NULL;
	struct connect_elm	*ce;
	time_t			 timeout;
	pid_t			 se_pid = 0, rde_pid = 0, rtr_pid = 0, pid;
	const char		*conffile;
	char			*saved_argv0;
	u_int			 pfd_elms = 0, npfd, i;
	int			 debug = 0;
	int			 rfd, keyfd;
	int			 ch, status;
	int			 pipe_m2s[2];
	int			 pipe_m2r[2];
	int			 pipe_m2roa[2];

	conffile = CONFFILE;

	log_init(1, LOG_DAEMON);	/* log to stderr until daemonized */
	log_procinit(log_procnames[PROC_MAIN]);
	log_setverbose(1);

	saved_argv0 = argv[0];
	if (saved_argv0 == NULL)
		saved_argv0 = "bgpd";

	while ((ch = getopt(argc, argv, "cdD:f:nRSTvV")) != -1) {
		switch (ch) {
		case 'c':
			cmd_opts |= BGPD_OPT_FORCE_DEMOTE;
			break;
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
			cmd_opts |= BGPD_OPT_NOACTION;
			break;
		case 'v':
			if (cmd_opts & BGPD_OPT_VERBOSE)
				cmd_opts |= BGPD_OPT_VERBOSE2;
			cmd_opts |= BGPD_OPT_VERBOSE;
			break;
		case 'R':
			proc = PROC_RDE;
			break;
		case 'S':
			proc = PROC_SE;
			break;
		case 'T':
			proc = PROC_RTR;
			break;
		case 'V':
			fprintf(stderr, "OpenBGPD %s\n", BGPD_VERSION);
			return 0;
		default:
			usage();
			/* NOTREACHED */
		}
	}

	argc -= optind;
	argv += optind;
	if (argc > 0)
		usage();

	if (cmd_opts & BGPD_OPT_NOACTION) {
		if ((conf = parse_config(conffile, NULL, NULL)) == NULL)
			exit(1);

		if (cmd_opts & BGPD_OPT_VERBOSE)
			print_config(conf, &ribnames);
		else
			fprintf(stderr, "configuration OK\n");

		while ((rr = SIMPLEQ_FIRST(&ribnames)) != NULL) {
			SIMPLEQ_REMOVE_HEAD(&ribnames, entry);
			free(rr);
		}
		free_config(conf);
		exit(0);
	}

	switch (proc) {
	case PROC_MAIN:
		break;
	case PROC_RDE:
		rde_main(debug, cmd_opts & BGPD_OPT_VERBOSE);
		/* NOTREACHED */
	case PROC_SE:
		session_main(debug, cmd_opts & BGPD_OPT_VERBOSE);
		/* NOTREACHED */
	case PROC_RTR:
		rtr_main(debug, cmd_opts & BGPD_OPT_VERBOSE);
		/* NOTREACHED */
	}

	if (geteuid())
		errx(1, "need root privileges");

	if (getpwnam(BGPD_USER) == NULL)
		errx(1, "unknown user %s", BGPD_USER);

	if ((conf = parse_config(conffile, NULL, NULL)) == NULL) {
		log_warnx("config file %s has errors", conffile);
		exit(1);
	}

	if (prepare_listeners(conf) == -1)
		exit(1);

	log_init(debug, LOG_DAEMON);
	log_setverbose(cmd_opts & BGPD_OPT_VERBOSE);

	if (!debug)
		daemon(1, 0);

	log_info("startup");

	getsockpair(pipe_m2s);
	getsockpair(pipe_m2r);
	getsockpair(pipe_m2roa);

	/* fork children */
	rde_pid = start_child(PROC_RDE, saved_argv0, pipe_m2r[1], debug,
	    cmd_opts & BGPD_OPT_VERBOSE);
	se_pid = start_child(PROC_SE, saved_argv0, pipe_m2s[1], debug,
	    cmd_opts & BGPD_OPT_VERBOSE);
	rtr_pid = start_child(PROC_RTR, saved_argv0, pipe_m2roa[1], debug,
	    cmd_opts & BGPD_OPT_VERBOSE);

	signal(SIGTERM, sighdlr);
	signal(SIGINT, sighdlr);
	signal(SIGHUP, sighdlr);
	signal(SIGALRM, sighdlr);
	signal(SIGUSR1, sighdlr);
	signal(SIGPIPE, SIG_IGN);

	if ((ibuf_se = malloc(sizeof(struct imsgbuf))) == NULL ||
	    (ibuf_rde = malloc(sizeof(struct imsgbuf))) == NULL ||
	    (ibuf_rtr = malloc(sizeof(struct imsgbuf))) == NULL)
		fatal(NULL);
	if (imsgbuf_init(ibuf_se, pipe_m2s[0]) == -1 ||
	    imsgbuf_set_maxsize(ibuf_se, MAX_BGPD_IMSGSIZE) == -1 ||
	    imsgbuf_init(ibuf_rde, pipe_m2r[0]) == -1 ||
	    imsgbuf_set_maxsize(ibuf_rde, MAX_BGPD_IMSGSIZE) == -1 ||
	    imsgbuf_init(ibuf_rtr, pipe_m2roa[0]) == -1 ||
	    imsgbuf_set_maxsize(ibuf_rtr, MAX_BGPD_IMSGSIZE) == -1)
		fatal(NULL);
	imsgbuf_allow_fdpass(ibuf_se);
	imsgbuf_allow_fdpass(ibuf_rde);
	imsgbuf_allow_fdpass(ibuf_rtr);
	mrt_init(ibuf_rde, ibuf_se);
	if (kr_init(&rfd, conf->fib_priority) == -1)
		quit = 1;
	keyfd = pfkey_init();

	/*
	 * rpath, read config file
	 * cpath, unlink control socket
	 * fattr, chmod on control socket
	 * wpath, needed if we are doing mrt dumps
	 *
	 * pledge placed here because kr_init() does a setsockopt on the
	 * routing socket thats not allowed at all.
	 */
#if 0
	/*
	 * disabled because we do ioctls on /dev/pf and SIOCSIFGATTR
	 * this needs some redesign of bgpd to be fixed.
	 */
BROKEN	if (pledge("stdio rpath wpath cpath fattr unix route recvfd sendfd",
	    NULL) == -1)
		fatal("pledge");
#endif

	if (imsg_send_sockets(ibuf_se, ibuf_rde, ibuf_rtr))
		fatal("could not establish imsg links");
	/* control setup needs to happen late since it sends imsgs */
	if (control_setup(conf) == -1)
		quit = 1;
	if (send_config(conf) != 0)
		quit = 1;
	if (pftable_clear_all() != 0)
		quit = 1;

	while (quit == 0) {
		if (pfd_elms < PFD_CONNECT_START + connect_cnt) {
			struct pollfd *newp;

			if ((newp = reallocarray(pfd,
			    PFD_CONNECT_START + connect_cnt,
			    sizeof(struct pollfd))) == NULL) {
				log_warn("could not resize pfd from %u -> %u"
				    " entries", pfd_elms, PFD_CONNECT_START +
				    connect_cnt);
				fatalx("exiting");
			}
			pfd = newp;
			pfd_elms = PFD_CONNECT_START + connect_cnt;
		}
		memset(pfd, 0, sizeof(struct pollfd) * pfd_elms);

		timeout = mrt_timeout(conf->mrt);

		pfd[PFD_SOCK_ROUTE].fd = rfd;
		pfd[PFD_SOCK_ROUTE].events = POLLIN;

		pfd[PFD_SOCK_PFKEY].fd = keyfd;
		pfd[PFD_SOCK_PFKEY].events = POLLIN;

		set_pollfd(&pfd[PFD_PIPE_SESSION], ibuf_se);
		set_pollfd(&pfd[PFD_PIPE_RDE], ibuf_rde);
		set_pollfd(&pfd[PFD_PIPE_RTR], ibuf_rtr);

		npfd = PFD_CONNECT_START;
		TAILQ_FOREACH(ce, &connect_queue, entry) {
			pfd[npfd].fd = ce->fd;
			pfd[npfd++].events = POLLOUT;
			if (npfd > pfd_elms)
				fatalx("polli pfd overflow");
		}

		if (timeout < 0 || timeout > MAX_TIMEOUT)
			timeout = MAX_TIMEOUT;
		if (poll(pfd, npfd, timeout) == -1) {
			if (errno != EINTR) {
				log_warn("poll error");
				quit = 1;
			}
			goto next_loop;
		}

		if (handle_pollfd(&pfd[PFD_PIPE_SESSION], ibuf_se) == -1) {
			log_warnx("main: Lost connection to SE");
			imsgbuf_clear(ibuf_se);
			free(ibuf_se);
			ibuf_se = NULL;
			quit = 1;
		} else {
			if (dispatch_imsg(ibuf_se, PFD_PIPE_SESSION, conf) ==
			    -1)
				quit = 1;
		}

		if (handle_pollfd(&pfd[PFD_PIPE_RDE], ibuf_rde) == -1) {
			log_warnx("main: Lost connection to RDE");
			imsgbuf_clear(ibuf_rde);
			free(ibuf_rde);
			ibuf_rde = NULL;
			quit = 1;
		} else {
			if (dispatch_imsg(ibuf_rde, PFD_PIPE_RDE, conf) == -1)
				quit = 1;
		}

		if (handle_pollfd(&pfd[PFD_PIPE_RTR], ibuf_rtr) == -1) {
			log_warnx("main: Lost connection to RTR");
			imsgbuf_clear(ibuf_rtr);
			free(ibuf_rtr);
			ibuf_rtr = NULL;
			quit = 1;
		} else {
			if (dispatch_imsg(ibuf_rtr, PFD_PIPE_RTR, conf) == -1)
				quit = 1;
		}

		if (pfd[PFD_SOCK_ROUTE].revents & POLLIN) {
			if (kr_dispatch_msg() == -1)
				quit = 1;
		}

		if (pfd[PFD_SOCK_PFKEY].revents & POLLIN) {
			if (pfkey_read(keyfd, NULL) == -1) {
				log_warnx("pfkey_read failed, exiting...");
				quit = 1;
			}
		}

		for (i = PFD_CONNECT_START; i < npfd; i++)
			if (pfd[i].revents != 0)
				bgpd_rtr_conn_setup_done(pfd[i].fd, conf);

 next_loop:
		if (reconfig) {
			u_int	error;

			reconfig = 0;
			switch (reconfigure(conffile, conf)) {
			case -1:	/* fatal error */
				quit = 1;
				break;
			case 0:		/* all OK */
				error = 0;
				break;
			case 2:
				log_info("previous reload still running");
				error = CTL_RES_PENDING;
				break;
			default:	/* parse error */
				log_warnx("config file %s has errors, "
				    "not reloading", conffile);
				error = CTL_RES_PARSE_ERROR;
				break;
			}
			if (reconfpid != 0) {
				send_imsg_session(IMSG_CTL_RESULT, reconfpid,
				    &error, sizeof(error));
				reconfpid = 0;
			}
		}

		if (mrtdump) {
			mrtdump = 0;
			mrt_handler(conf->mrt);
		}
	}

	/* close pipes */
	if (ibuf_se) {
		imsgbuf_clear(ibuf_se);
		close(ibuf_se->fd);
		free(ibuf_se);
		ibuf_se = NULL;
	}
	if (ibuf_rde) {
		imsgbuf_clear(ibuf_rde);
		close(ibuf_rde->fd);
		free(ibuf_rde);
		ibuf_rde = NULL;
	}
	if (ibuf_rtr) {
		imsgbuf_clear(ibuf_rtr);
		close(ibuf_rtr->fd);
		free(ibuf_rtr);
		ibuf_rtr = NULL;
	}

	/* cleanup kernel data structures */
	carp_demote_shutdown();
	kr_shutdown();
	pftable_clear_all();

	RB_FOREACH(p, peer_head, &conf->peers)
		pfkey_remove(&p->auth_state);

	while ((rr = SIMPLEQ_FIRST(&ribnames)) != NULL) {
		SIMPLEQ_REMOVE_HEAD(&ribnames, entry);
		free(rr);
	}
	free_config(conf);

	log_debug("waiting for children to terminate");
	do {
		pid = wait(&status);
		if (pid == -1) {
			if (errno != EINTR && errno != ECHILD)
				fatal("wait");
		} else if (WIFSIGNALED(status)) {
			char *name = "unknown process";
			if (pid == rde_pid)
				name = "route decision engine";
			else if (pid == se_pid)
				name = "session engine";
			else if (pid == rtr_pid)
				name = "rtr engine";
			log_warnx("%s terminated; signal %d", name,
				WTERMSIG(status));
		}
	} while (pid != -1 || (pid == -1 && errno == EINTR));

	free(rcname);
	free(cname);

	log_info("terminating");
	return (0);
}

pid_t
start_child(enum bgpd_process p, char *argv0, int fd, int debug, int verbose)
{
	char *argv[5];
	int argc = 0;
	pid_t pid;

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
	case PROC_RDE:
		argv[argc++] = "-R";
		break;
	case PROC_SE:
		argv[argc++] = "-S";
		break;
	case PROC_RTR:
		argv[argc++] = "-T";
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

int
send_filterset(struct imsgbuf *i, struct filter_set_head *set)
{
	struct filter_set	*s;

	TAILQ_FOREACH(s, set, entry)
		if (imsg_compose(i, IMSG_FILTER_SET, 0, 0, -1, s,
		    sizeof(struct filter_set)) == -1)
			return (-1);
	return (0);
}

int
reconfigure(const char *conffile, struct bgpd_config *conf)
{
	struct bgpd_config	*new_conf;

	if (reconfpending)
		return (2);

	log_info("rereading config");
	if ((new_conf = parse_config(conffile, &conf->peers,
	    &conf->rtrs)) == NULL)
		return (1);

	merge_config(conf, new_conf);

	if (prepare_listeners(conf) == -1)
		return (1);

	if (control_setup(conf) == -1)
		return (1);

	return send_config(conf);
}

int
send_config(struct bgpd_config *conf)
{
	struct peer		*p;
	struct filter_rule	*r;
	struct listen_addr	*la;
	struct rde_rib		*rr;
	struct l3vpn		*vpn;
	struct as_set		*aset;
	struct prefixset	*ps;
	struct prefixset_item	*psi, *npsi;
	struct roa		*roa;
	struct aspa_set		*aspa;
	struct rtr_config	*rtr;
	struct flowspec_config	*f, *nf;

	reconfpending = 3;	/* one per child */

	expand_networks(conf, &conf->networks);
	SIMPLEQ_FOREACH(vpn, &conf->l3vpns, entry)
		expand_networks(conf, &vpn->net_l);

	cflags = conf->flags;

	/* start reconfiguration */
	if (imsg_compose(ibuf_se, IMSG_RECONF_CONF, 0, 0, -1,
	    conf, sizeof(*conf)) == -1)
		return (-1);
	if (imsg_compose(ibuf_rde, IMSG_RECONF_CONF, 0, 0, -1,
	    conf, sizeof(*conf)) == -1)
		return (-1);
	if (imsg_compose(ibuf_rtr, IMSG_RECONF_CONF, 0, 0, -1,
	    conf, sizeof(*conf)) == -1)
		return (-1);

	TAILQ_FOREACH(la, conf->listen_addrs, entry) {
		if (imsg_compose(ibuf_se, IMSG_RECONF_LISTENER, 0, 0, la->fd,
		    la, sizeof(*la)) == -1)
			return (-1);
		la->fd = -1;
	}

	/* adjust fib syncing on reload */
	ktable_preload();

	/* RIBs for the RDE */
	while ((rr = SIMPLEQ_FIRST(&ribnames))) {
		SIMPLEQ_REMOVE_HEAD(&ribnames, entry);
		if (ktable_update(rr->rtableid, rr->name, rr->flags) == -1) {
			log_warnx("failed to load routing table %d",
			    rr->rtableid);
			return (-1);
		}
		if (imsg_compose(ibuf_rde, IMSG_RECONF_RIB, 0, 0, -1,
		    rr, sizeof(*rr)) == -1)
			return (-1);
		free(rr);
	}

	/* send peer list to the SE */
	RB_FOREACH(p, peer_head, &conf->peers) {
		if (p->reconf_action == RECONF_DELETE)
			continue;

		if (imsg_compose(ibuf_se, IMSG_RECONF_PEER, p->conf.id, 0, -1,
		    &p->conf, sizeof(p->conf)) == -1)
			return (-1);
		if (pfkey_send_conf(ibuf_se, p->conf.id, &p->auth_conf) == -1)
			return (-1);

		if (p->reconf_action == RECONF_REINIT)
			if (pfkey_establish(&p->auth_state, &p->auth_conf,
			    session_localaddr(p), &p->conf.remote_addr) == -1)
				log_peer_warnx(&p->conf, "auth setup failed");
	}

	/* networks go via kroute to the RDE */
	kr_net_reload(conf->default_tableid, 0, &conf->networks);

	/* flowspec goes directly to the RDE, also remove old objects */
	RB_FOREACH_SAFE(f, flowspec_tree, &conf->flowspecs, nf) {
		if (f->reconf_action != RECONF_DELETE) {
			if (imsg_compose(ibuf_rde, IMSG_FLOWSPEC_ADD, 0, 0, -1,
			    f->flow, FLOWSPEC_SIZE + f->flow->len) == -1)
				return (-1);
			if (send_filterset(ibuf_rde, &f->attrset) == -1)
				return (-1);
			if (imsg_compose(ibuf_rde, IMSG_FLOWSPEC_DONE, 0, 0, -1,
			    NULL, 0) == -1)
				return (-1);
		} else {
			if (imsg_compose(ibuf_rde, IMSG_FLOWSPEC_REMOVE, 0, 0,
			    -1, f->flow, FLOWSPEC_SIZE + f->flow->len) == -1)
				return (-1);
			RB_REMOVE(flowspec_tree, &conf->flowspecs, f);
			flowspec_free(f);
		}
	}

	/* prefixsets for filters in the RDE */
	while ((ps = SIMPLEQ_FIRST(&conf->prefixsets)) != NULL) {
		SIMPLEQ_REMOVE_HEAD(&conf->prefixsets, entry);
		if (imsg_compose(ibuf_rde, IMSG_RECONF_PREFIX_SET, 0, 0, -1,
		    ps->name, sizeof(ps->name)) == -1)
			return (-1);
		RB_FOREACH_SAFE(psi, prefixset_tree, &ps->psitems, npsi) {
			RB_REMOVE(prefixset_tree, &ps->psitems, psi);
			if (imsg_compose(ibuf_rde, IMSG_RECONF_PREFIX_SET_ITEM,
			    0, 0, -1, psi, sizeof(*psi)) == -1)
				return (-1);
			free(psi);
		}
		free(ps);
	}

	/* originsets for filters in the RDE */
	while ((ps = SIMPLEQ_FIRST(&conf->originsets)) != NULL) {
		SIMPLEQ_REMOVE_HEAD(&conf->originsets, entry);
		if (imsg_compose(ibuf_rde, IMSG_RECONF_ORIGIN_SET, 0, 0, -1,
		    ps->name, sizeof(ps->name)) == -1)
			return (-1);
		RB_FOREACH(roa, roa_tree, &ps->roaitems) {
			if (imsg_compose(ibuf_rde, IMSG_RECONF_ROA_ITEM, 0, 0,
			    -1, roa, sizeof(*roa)) == -1)
				return (-1);
		}
		free_roatree(&ps->roaitems);
		free(ps);
	}

	/* roa table, aspa table and rtr config are sent to the RTR engine */
	RB_FOREACH(roa, roa_tree, &conf->roa) {
		if (imsg_compose(ibuf_rtr, IMSG_RECONF_ROA_ITEM, 0, 0,
		    -1, roa, sizeof(*roa)) == -1)
			return (-1);
	}
	free_roatree(&conf->roa);
	RB_FOREACH(aspa, aspa_tree, &conf->aspa) {
		if (imsg_compose(ibuf_rtr, IMSG_RECONF_ASPA, 0, 0,
		    -1, aspa, offsetof(struct aspa_set, tas)) == -1)
			return (-1);
		if (imsg_compose(ibuf_rtr, IMSG_RECONF_ASPA_TAS, 0, 0,
		    -1, aspa->tas, aspa->num * sizeof(*aspa->tas)) == -1)
			return (-1);
		if (imsg_compose(ibuf_rtr, IMSG_RECONF_ASPA_DONE, 0, 0, -1,
		    NULL, 0) == -1)
			return -1;
	}
	free_aspatree(&conf->aspa);
	SIMPLEQ_FOREACH(rtr, &conf->rtrs, entry) {
		struct rtr_config_msg rtrconf = { 0 };

		strlcpy(rtrconf.descr, rtr->descr, sizeof(rtrconf.descr));
		rtrconf.min_version = rtr->min_version;
		if (imsg_compose(ibuf_rtr, IMSG_RECONF_RTR_CONFIG, rtr->id,
		    0, -1, &rtrconf, sizeof(rtrconf)) == -1)
			return (-1);
	}

	/* as-sets for filters in the RDE */
	while ((aset = SIMPLEQ_FIRST(&conf->as_sets)) != NULL) {
		struct ibuf *wbuf;
		uint32_t *as;
		size_t i, l, n;

		SIMPLEQ_REMOVE_HEAD(&conf->as_sets, entry);

		as = set_get(aset->set, &n);
		if ((wbuf = imsg_create(ibuf_rde, IMSG_RECONF_AS_SET, 0, 0,
		    sizeof(n) + sizeof(aset->name))) == NULL)
			return -1;
		if (imsg_add(wbuf, &n, sizeof(n)) == -1 ||
		    imsg_add(wbuf, aset->name, sizeof(aset->name)) == -1)
			return -1;
		imsg_close(ibuf_rde, wbuf);

		for (i = 0; i < n; i += l) {
			l = (n - i > 1024 ? 1024 : n - i);
			if (imsg_compose(ibuf_rde, IMSG_RECONF_AS_SET_ITEMS,
			    0, 0, -1, as + i, l * sizeof(*as)) == -1)
				return -1;
		}

		if (imsg_compose(ibuf_rde, IMSG_RECONF_AS_SET_DONE, 0, 0, -1,
		    NULL, 0) == -1)
			return -1;

		set_free(aset->set);
		free(aset);
	}

	/* filters for the RDE */
	while ((r = TAILQ_FIRST(conf->filters)) != NULL) {
		TAILQ_REMOVE(conf->filters, r, entry);
		if (send_filterset(ibuf_rde, &r->set) == -1)
			return (-1);
		if (imsg_compose(ibuf_rde, IMSG_RECONF_FILTER, 0, 0, -1,
		    r, sizeof(struct filter_rule)) == -1)
			return (-1);
		filterset_free(&r->set);
		free(r);
	}

	while ((vpn = SIMPLEQ_FIRST(&conf->l3vpns)) != NULL) {
		SIMPLEQ_REMOVE_HEAD(&conf->l3vpns, entry);
		if (ktable_update(vpn->rtableid, vpn->descr, vpn->flags) ==
		    -1) {
			log_warnx("failed to load routing table %d",
			    vpn->rtableid);
			return (-1);
		}
		/* networks go via kroute to the RDE */
		kr_net_reload(vpn->rtableid, vpn->rd, &vpn->net_l);

		if (imsg_compose(ibuf_rde, IMSG_RECONF_VPN, 0, 0, -1,
		    vpn, sizeof(*vpn)) == -1)
			return (-1);

		/* export targets */
		if (send_filterset(ibuf_rde, &vpn->export) == -1)
			return (-1);
		if (imsg_compose(ibuf_rde, IMSG_RECONF_VPN_EXPORT, 0, 0,
		    -1, NULL, 0) == -1)
			return (-1);
		filterset_free(&vpn->export);

		/* import targets */
		if (send_filterset(ibuf_rde, &vpn->import) == -1)
			return (-1);
		if (imsg_compose(ibuf_rde, IMSG_RECONF_VPN_IMPORT, 0, 0,
		    -1, NULL, 0) == -1)
			return (-1);
		filterset_free(&vpn->import);

		if (imsg_compose(ibuf_rde, IMSG_RECONF_VPN_DONE, 0, 0,
		    -1, NULL, 0) == -1)
			return (-1);

		free(vpn);
	}

	/* send a drain message to know when all messages where processed */
	if (imsg_compose(ibuf_se, IMSG_RECONF_DRAIN, 0, 0, -1, NULL, 0) == -1)
		return (-1);
	if (imsg_compose(ibuf_rde, IMSG_RECONF_DRAIN, 0, 0, -1, NULL, 0) == -1)
		return (-1);
	if (imsg_compose(ibuf_rtr, IMSG_RECONF_DRAIN, 0, 0, -1, NULL, 0) == -1)
		return (-1);

	/* mrt changes can be sent out of bound */
	mrt_reconfigure(conf->mrt);
	return (0);
}

int
dispatch_imsg(struct imsgbuf *imsgbuf, int idx, struct bgpd_config *conf)
{
	struct imsg		 imsg;
	struct peer		*p;
	struct rtr_config	*r;
	struct kroute_full	 kf;
	struct bgpd_addr	 addr;
	struct pftable_msg	 pfmsg;
	struct demote_msg	 demote;
	char			 reason[REASON_LEN], ifname[IFNAMSIZ];
	ssize_t			 n;
	u_int			 rtableid;
	int			 rv, verbose;

	rv = 0;
	while (imsgbuf) {
		if ((n = imsg_get(imsgbuf, &imsg)) == -1)
			return (-1);

		if (n == 0)
			break;

		switch (imsg_get_type(&imsg)) {
		case IMSG_KROUTE_CHANGE:
			if (idx != PFD_PIPE_RDE)
				log_warnx("route request not from RDE");
			else if (imsg_get_data(&imsg, &kf, sizeof(kf)) == -1)
				log_warn("wrong imsg len");
			else if (kr_change(imsg_get_id(&imsg), &kf))
				rv = -1;
			break;
		case IMSG_KROUTE_DELETE:
			if (idx != PFD_PIPE_RDE)
				log_warnx("route request not from RDE");
			else if (imsg_get_data(&imsg, &kf, sizeof(kf)) == -1)
				log_warn("wrong imsg len");
			else if (kr_delete(imsg_get_id(&imsg), &kf))
				rv = -1;
			break;
		case IMSG_KROUTE_FLUSH:
			if (idx != PFD_PIPE_RDE)
				log_warnx("route request not from RDE");
			else if (kr_flush(imsg_get_id(&imsg)))
				rv = -1;
			break;
		case IMSG_NEXTHOP_ADD:
			if (idx != PFD_PIPE_RDE)
				log_warnx("nexthop request not from RDE");
			else if (imsg_get_data(&imsg, &addr, sizeof(addr)) ==
			    -1)
				log_warn("wrong imsg len");
			else {
				rtableid = conf->default_tableid;
				if (kr_nexthop_add(rtableid, &addr) == -1)
					rv = -1;
			}
			break;
		case IMSG_NEXTHOP_REMOVE:
			if (idx != PFD_PIPE_RDE)
				log_warnx("nexthop request not from RDE");
			else if (imsg_get_data(&imsg, &addr, sizeof(addr)) ==
			    -1)
				log_warn("wrong imsg len");
			else {
				rtableid = conf->default_tableid;
				kr_nexthop_delete(rtableid, &addr);
			}
			break;
		case IMSG_PFTABLE_ADD:
			if (idx != PFD_PIPE_RDE)
				log_warnx("pftable request not from RDE");
			else if (imsg_get_data(&imsg, &pfmsg, sizeof(pfmsg)) ==
			    -1)
				log_warn("wrong imsg len");
			else if (pftable_addr_add(&pfmsg) != 0)
				rv = -1;
			break;
		case IMSG_PFTABLE_REMOVE:
			if (idx != PFD_PIPE_RDE)
				log_warnx("pftable request not from RDE");
			else if (imsg_get_data(&imsg, &pfmsg, sizeof(pfmsg)) ==
			    -1)
				log_warn("wrong imsg len");
			else if (pftable_addr_remove(&pfmsg) != 0)
				rv = -1;
			break;
		case IMSG_PFTABLE_COMMIT:
			if (idx != PFD_PIPE_RDE)
				log_warnx("pftable request not from RDE");
			else if (pftable_commit() != 0)
				rv = -1;
			break;
		case IMSG_PFKEY_RELOAD:
			if (idx != PFD_PIPE_SESSION) {
				log_warnx("pfkey reload request not from SE");
				break;
			}
			p = getpeerbyid(conf, imsg_get_id(&imsg));
			if (p != NULL) {
				if (pfkey_establish(&p->auth_state,
				    &p->auth_conf, session_localaddr(p),
				    &p->conf.remote_addr) == -1)
					log_peer_warnx(&p->conf,
					    "pfkey setup failed");
			}
			break;
		case IMSG_CTL_RELOAD:
			if (idx != PFD_PIPE_SESSION)
				log_warnx("reload request not from SE");
			else {
				reconfig = 1;
				reconfpid = imsg_get_pid(&imsg);
				if (imsg_get_data(&imsg, reason,
				    sizeof(reason)) == 0 && reason[0] != '\0') {
					reason[sizeof(reason) - 1] = '\0';
					log_info("reload due to: %s",
					    log_reason(reason));
				}
			}
			break;
		case IMSG_CTL_FIB_COUPLE:
			if (idx != PFD_PIPE_SESSION)
				log_warnx("couple request not from SE");
			else
				kr_fib_couple(imsg_get_id(&imsg));
			break;
		case IMSG_CTL_FIB_DECOUPLE:
			if (idx != PFD_PIPE_SESSION)
				log_warnx("decouple request not from SE");
			else
				kr_fib_decouple(imsg_get_id(&imsg));
			break;
		case IMSG_CTL_KROUTE:
		case IMSG_CTL_KROUTE_ADDR:
		case IMSG_CTL_SHOW_NEXTHOP:
		case IMSG_CTL_SHOW_INTERFACE:
		case IMSG_CTL_SHOW_FIB_TABLES:
			if (idx != PFD_PIPE_SESSION)
				log_warnx("kroute request not from SE");
			else
				kr_show_route(&imsg);
			break;
		case IMSG_SESSION_DEPENDON:
			if (idx != PFD_PIPE_SESSION)
				log_warnx("DEPENDON request not from SE");
			else if (imsg_get_data(&imsg, ifname, sizeof(ifname)) ==
			    -1)
				log_warn("wrong imsg len");
			else
				kr_ifinfo(ifname);
			break;
		case IMSG_DEMOTE:
			if (idx != PFD_PIPE_SESSION)
				log_warnx("demote request not from SE");
			else if (imsg_get_data(&imsg, &demote, sizeof(demote))
			    == -1)
				log_warn("wrong imsg len");
			else {
				demote.demote_group[
				    sizeof(demote.demote_group) - 1] = '\0';
				carp_demote_set(demote.demote_group,
				    demote.level);
			}
			break;
		case IMSG_CTL_LOG_VERBOSE:
			/* already checked by SE */
			if (imsg_get_data(&imsg, &verbose, sizeof(verbose)) ==
			    -1)
				log_warn("wrong imsg len");
			else
				log_setverbose(verbose);
			break;
		case IMSG_RECONF_DONE:
			if (reconfpending == 0) {
				log_warnx("unexpected RECONF_DONE received");
				break;
			}
			if (idx == PFD_PIPE_SESSION) {
				/* RDE and RTR engine can reload concurrently */
				imsg_compose(ibuf_rtr, IMSG_RECONF_DONE, 0,
				    0, -1, NULL, 0);
				imsg_compose(ibuf_rde, IMSG_RECONF_DONE, 0,
				    0, -1, NULL, 0);

				/* finally fix kroute information */
				ktable_postload();

				/* redistribute list needs to be reloaded too */
				kr_reload();

				/* also remove old peers */
				free_deleted_peers(conf);
			}
			reconfpending--;
			break;
		case IMSG_RECONF_DRAIN:
			if (reconfpending == 0) {
				log_warnx("unexpected RECONF_DRAIN received");
				break;
			}
			reconfpending--;
			if (reconfpending == 0) {
				/*
				 * SE goes first to bring templated neighbors
				 * in sync.
				 */
				imsg_compose(ibuf_se, IMSG_RECONF_DONE, 0,
				    0, -1, NULL, 0);
				reconfpending = 3; /* expecting 2 DONE msg */
			}
			break;
		case IMSG_SOCKET_SETUP:
			if (idx != PFD_PIPE_RTR) {
				log_warnx("connect request not from RTR");
			} else {
				uint32_t rtrid = imsg_get_id(&imsg);
				SIMPLEQ_FOREACH(r, &conf->rtrs, entry) {
					if (rtrid == r->id)
						break;
				}
				if (r == NULL)
					log_warnx("unknown rtr id %d", rtrid);
				else
					bgpd_rtr_conn_setup(r);
			}
			break;
		case IMSG_SOCKET_TEARDOWN:
			if (idx != PFD_PIPE_RTR) {
				log_warnx("connect request not from RTR");
			} else {
				uint32_t rtrid = imsg_get_id(&imsg);
				bgpd_rtr_conn_teardown(rtrid);
			}
			break;
		case IMSG_CTL_SHOW_RTR:
			if (idx == PFD_PIPE_SESSION) {
				SIMPLEQ_FOREACH(r, &conf->rtrs, entry) {
					imsg_compose(ibuf_rtr,
					    IMSG_CTL_SHOW_RTR, r->id,
					    imsg_get_pid(&imsg), -1, NULL, 0);
				}
				imsg_compose(ibuf_rtr, IMSG_CTL_END,
				    0, imsg_get_pid(&imsg), -1, NULL, 0);
			} else if (idx == PFD_PIPE_RTR) {
				struct ctl_show_rtr rtr;
				if (imsg_get_data(&imsg, &rtr, sizeof(rtr)) ==
				    -1) {
					log_warn("wrong imsg len");
					break;
				}

				SIMPLEQ_FOREACH(r, &conf->rtrs, entry) {
					if (imsg_get_id(&imsg) == r->id)
						break;
				}
				if (r != NULL) {
					strlcpy(rtr.descr, r->descr,
					    sizeof(rtr.descr));
					rtr.local_addr = r->local_addr;
					rtr.remote_addr = r->remote_addr;
					rtr.remote_port = r->remote_port;

					imsg_compose(ibuf_se, IMSG_CTL_SHOW_RTR,
					    imsg_get_id(&imsg),
					    imsg_get_pid(&imsg), -1,
					    &rtr, sizeof(rtr));
				}
			}
			break;
		case IMSG_CTL_END:
		case IMSG_CTL_SHOW_TIMER:
			if (idx != PFD_PIPE_RTR) {
				log_warnx("connect request not from RTR");
				break;
			}
			imsg_forward(ibuf_se, &imsg);
			break;
		default:
			break;
		}
		imsg_free(&imsg);
		if (rv != 0)
			return (rv);
	}
	return (0);
}

void
send_nexthop_update(struct kroute_nexthop *msg)
{
	char	*gw = NULL;

	if (msg->gateway.aid)
		if (asprintf(&gw, ": via %s",
		    log_addr(&msg->gateway)) == -1) {
			log_warn("send_nexthop_update");
			quit = 1;
		}

	log_debug("nexthop %s now %s%s%s", log_addr(&msg->nexthop),
	    msg->valid ? "valid" : "invalid",
	    msg->connected ? ": directly connected" : "",
	    msg->gateway.aid ? gw : "");

	free(gw);

	if (imsg_compose(ibuf_rde, IMSG_NEXTHOP_UPDATE, 0, 0, -1,
	    msg, sizeof(struct kroute_nexthop)) == -1)
		quit = 1;
}

void
send_imsg_session(int type, pid_t pid, void *data, uint16_t datalen)
{
	imsg_compose(ibuf_se, type, 0, pid, -1, data, datalen);
}

int
send_network(int type, struct network_config *net, struct filter_set_head *h)
{
	if (quit)
		return (0);
	if (imsg_compose(ibuf_rde, type, 0, 0, -1, net,
	    sizeof(struct network_config)) == -1)
		return (-1);
	/* networks that get deleted don't need to send the filter set */
	if (type == IMSG_NETWORK_REMOVE)
		return (0);
	if (send_filterset(ibuf_rde, h) == -1)
		return (-1);
	if (imsg_compose(ibuf_rde, IMSG_NETWORK_DONE, 0, 0, -1, NULL, 0) == -1)
		return (-1);

	return (0);
}

/*
 * Return true if a route can be used for nexthop resolution.
 */
int
bgpd_oknexthop(struct kroute_full *kf)
{
	if (kf->flags & F_BGPD)
		return ((cflags & BGPD_FLAG_NEXTHOP_BGP) != 0);

	if (kf->prefixlen == 0)
		return ((cflags & BGPD_FLAG_NEXTHOP_DEFAULT) != 0);

	/* any other route is fine */
	return (1);
}

int
bgpd_has_bgpnh(void)
{
	return ((cflags & BGPD_FLAG_NEXTHOP_BGP) != 0);
}

int
control_setup(struct bgpd_config *conf)
{
	int fd, restricted;

	/* control socket is outside chroot */
	if (!cname || strcmp(cname, conf->csock)) {
		if (cname) {
			free(cname);
		}
		if ((cname = strdup(conf->csock)) == NULL)
			fatal("strdup");
		if (control_check(cname) == -1)
			return (-1);
		if ((fd = control_init(0, cname)) == -1)
			fatalx("control socket setup failed");
		if (control_listen(fd) == -1)
			fatalx("control socket setup failed");
		restricted = 0;
		if (imsg_compose(ibuf_se, IMSG_RECONF_CTRL, 0, 0, fd,
		    &restricted, sizeof(restricted)) == -1)
			return (-1);
	}
	if (!conf->rcsock) {
		/* remove restricted socket */
		free(rcname);
		rcname = NULL;
	} else if (!rcname || strcmp(rcname, conf->rcsock)) {
		if (rcname) {
			free(rcname);
		}
		if ((rcname = strdup(conf->rcsock)) == NULL)
			fatal("strdup");
		if (control_check(rcname) == -1)
			return (-1);
		if ((fd = control_init(1, rcname)) == -1)
			fatalx("control socket setup failed");
		if (control_listen(fd) == -1)
			fatalx("control socket setup failed");
		restricted = 1;
		if (imsg_compose(ibuf_se, IMSG_RECONF_CTRL, 0, 0, fd,
		    &restricted, sizeof(restricted)) == -1)
			return (-1);
	}
	return (0);
}

void
set_pollfd(struct pollfd *pfd, struct imsgbuf *i)
{
	if (i == NULL || i->fd == -1) {
		pfd->fd = -1;
		return;
	}
	pfd->fd = i->fd;
	pfd->events = POLLIN;
	if (imsgbuf_queuelen(i) > 0)
		pfd->events |= POLLOUT;
}

int
handle_pollfd(struct pollfd *pfd, struct imsgbuf *i)
{
	ssize_t n;

	if (i == NULL)
		return (0);

	if (pfd->revents & POLLOUT)
		if (imsgbuf_write(i) == -1) {
			log_warn("imsg write error");
			close(i->fd);
			i->fd = -1;
			return (-1);
		}

	if (pfd->revents & POLLIN) {
		if ((n = imsgbuf_read(i)) == -1) {
			log_warn("imsg read error");
			close(i->fd);
			i->fd = -1;
			return (-1);
		}
		if (n == 0) {
			log_warnx("peer closed imsg connection");
			close(i->fd);
			i->fd = -1;
			return (-1);
		}
	}
	return (0);
}

static void
getsockpair(int pipe[2])
{
	int bsize, i;

	if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK,
	    PF_UNSPEC, pipe) == -1)
		fatal("socketpair");

	for (i = 0; i < 2; i++) {
		bsize = MAX_SOCK_BUF;
		if (setsockopt(pipe[i], SOL_SOCKET, SO_RCVBUF,
		    &bsize, sizeof(bsize)) == -1) {
			if (errno != ENOBUFS)
				fatal("setsockopt(SO_RCVBUF, %d)",
				    bsize);
			log_warn("setsockopt(SO_RCVBUF, %d)", bsize);
		}
	}
	for (i = 0; i < 2; i++) {
		bsize = MAX_SOCK_BUF;
		if (setsockopt(pipe[i], SOL_SOCKET, SO_SNDBUF,
		    &bsize, sizeof(bsize)) == -1) {
			if (errno != ENOBUFS)
				fatal("setsockopt(SO_SNDBUF, %d)",
				    bsize);
			log_warn("setsockopt(SO_SNDBUF, %d)", bsize);
		}
	}
}

int
imsg_send_sockets(struct imsgbuf *se, struct imsgbuf *rde, struct imsgbuf *rtr)
{
	int pipe_s2r[2];
	int pipe_s2r_ctl[2];
	int pipe_r2r[2];

	getsockpair(pipe_s2r);
	getsockpair(pipe_s2r_ctl);
	getsockpair(pipe_r2r);

	if (imsg_compose(se, IMSG_SOCKET_CONN, 0, 0, pipe_s2r[0],
	    NULL, 0) == -1)
		return (-1);
	if (imsg_compose(rde, IMSG_SOCKET_CONN, 0, 0, pipe_s2r[1],
	    NULL, 0) == -1)
		return (-1);

	if (imsg_compose(se, IMSG_SOCKET_CONN_CTL, 0, 0, pipe_s2r_ctl[0],
	    NULL, 0) == -1)
		return (-1);
	if (imsg_compose(rde, IMSG_SOCKET_CONN_CTL, 0, 0, pipe_s2r_ctl[1],
	    NULL, 0) == -1)
		return (-1);

	if (imsg_compose(rtr, IMSG_SOCKET_CONN_RTR, 0, 0, pipe_r2r[0],
	    NULL, 0) == -1)
		return (-1);
	if (imsg_compose(rde, IMSG_SOCKET_CONN_RTR, 0, 0, pipe_r2r[1],
	    NULL, 0) == -1)
		return (-1);

	return (0);
}

void
bgpd_rtr_conn_setup(struct rtr_config *r)
{
	struct connect_elm *ce;
	struct sockaddr *sa;
	socklen_t len;
	int nodelay = 1;
	int pre = IPTOS_PREC_INTERNETCONTROL;

	if (connect_cnt >= MAX_CONNECT_CNT) {
		log_warnx("rtr %s: too many concurrent connection requests",
		    r->descr);
		return;
	}

	if ((ce = calloc(1, sizeof(*ce))) == NULL) {
		log_warn("rtr %s", r->descr);
		return;
	}

	if (pfkey_establish(&ce->auth_state, &r->auth,
	    &r->local_addr, &r->remote_addr) == -1)
		log_warnx("rtr %s: pfkey setup failed", r->descr);

	ce->id = r->id;
	ce->fd = socket(aid2af(r->remote_addr.aid),
	    SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, IPPROTO_TCP);
	if (ce->fd == -1) {
		log_warn("rtr %s", r->descr);
		goto fail;
	}

	switch (r->remote_addr.aid) {
	case AID_INET:
		if (setsockopt(ce->fd, IPPROTO_IP, IP_TOS, &pre, sizeof(pre)) ==
		    -1) {
			log_warn("rtr %s: setsockopt IP_TOS", r->descr);
			return;
		}
		break;
	case AID_INET6:
		if (setsockopt(ce->fd, IPPROTO_IPV6, IPV6_TCLASS, &pre,
		    sizeof(pre)) == -1) {
			log_warn("rtr %s: setsockopt IP_TOS", r->descr);
			return;
		}
		break;
	}

	if (setsockopt(ce->fd, IPPROTO_TCP, TCP_NODELAY, &nodelay,
	    sizeof(nodelay)) == -1) {
		log_warn("rtr %s: setsockopt TCP_NODELAY", r->descr);
		return;
	}

	if (tcp_md5_set(ce->fd, &r->auth, &r->remote_addr) == -1)
		log_warn("rtr %s: setting md5sig", r->descr);

	if ((sa = addr2sa(&r->local_addr, 0, &len)) != NULL) {
		if (bind(ce->fd, sa, len) == -1) {
			log_warn("rtr %s: bind to %s", r->descr,
			    log_addr(&r->local_addr));
			goto fail;
		}
	}

	sa = addr2sa(&r->remote_addr, r->remote_port, &len);
	if (connect(ce->fd, sa, len) == -1) {
		if (errno != EINPROGRESS) {
			log_warn("rtr %s: connect to %s:%u", r->descr,
			    log_addr(&r->remote_addr), r->remote_port);
			goto fail;
		}
		TAILQ_INSERT_TAIL(&connect_queue, ce, entry);
		connect_cnt++;
		return;
	}

	imsg_compose(ibuf_rtr, IMSG_SOCKET_SETUP, ce->id, 0, ce->fd, NULL, 0);
	TAILQ_INSERT_TAIL(&socket_queue, ce, entry);
	return;

 fail:
	if (ce->fd != -1)
		close(ce->fd);
	free(ce);
}

void
bgpd_rtr_conn_setup_done(int fd, struct bgpd_config *conf)
{
	struct rtr_config *r;
	struct connect_elm *ce;
	int error = 0;
	socklen_t len;

	TAILQ_FOREACH(ce, &connect_queue, entry) {
		if (ce->fd == fd)
			break;
	}
	if (ce == NULL)
		fatalx("connect entry not found");

	TAILQ_REMOVE(&connect_queue, ce, entry);
	connect_cnt--;

	SIMPLEQ_FOREACH(r, &conf->rtrs, entry) {
		if (ce->id == r->id)
			break;
	}
	if (r == NULL) {
		log_warnx("rtr id %d no longer exists", ce->id);
		goto fail;
	}

	len = sizeof(error);
	if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &len) == -1) {
		log_warn("rtr %s: getsockopt SO_ERROR", r->descr);
		goto fail;
	}

	if (error != 0) {
		errno = error;
		log_warn("rtr %s: connect to %s:%u", r->descr,
		    log_addr(&r->remote_addr), r->remote_port);
		goto fail;
	}

	imsg_compose(ibuf_rtr, IMSG_SOCKET_SETUP, ce->id, 0, ce->fd, NULL, 0);
	TAILQ_INSERT_TAIL(&socket_queue, ce, entry);
	return;

fail:
	close(fd);
	free(ce);
}

void
bgpd_rtr_conn_teardown(uint32_t id)
{
	struct connect_elm *ce;

	TAILQ_FOREACH(ce, &socket_queue, entry) {
		if (ce->id == id) {
			pfkey_remove(&ce->auth_state);
			TAILQ_REMOVE(&socket_queue, ce, entry);
			free(ce);
			return;
		}
	}
}
