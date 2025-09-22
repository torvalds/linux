/*	$OpenBSD: ospf6d.c,v 1.61 2024/11/21 13:38:14 claudio Exp $ */

/*
 * Copyright (c) 2005 Claudio Jeker <claudio@openbsd.org>
 * Copyright (c) 2004, 2007 Esben Norby <norby@openbsd.org>
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
#include <sys/wait.h>
#include <sys/sysctl.h>
#include <syslog.h>

#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if_types.h>

#include <event.h>
#include <err.h>
#include <errno.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

#include "ospf6d.h"
#include "ospf6.h"
#include "ospfe.h"
#include "control.h"
#include "log.h"
#include "rde.h"

void		main_sig_handler(int, short, void *);
__dead void	usage(void);
__dead void	ospfd_shutdown(void);

void	main_dispatch_ospfe(int, short, void *);
void	main_dispatch_rde(int, short, void *);

int	ospf_reload(void);
int	ospf_sendboth(enum imsg_type, void *, u_int16_t);
int	merge_interfaces(struct area *, struct area *);
struct iface *iface_lookup(struct area *, struct iface *);

int	pipe_parent2ospfe[2];
int	pipe_parent2rde[2];
int	pipe_ospfe2rde[2];

enum ospfd_process	 ospfd_process;
struct ospfd_conf	*ospfd_conf = NULL;
static struct imsgev	*iev_ospfe;
static struct imsgev	*iev_rde;
char			*conffile;

pid_t			 ospfe_pid = 0;
pid_t			 rde_pid = 0;

void
main_sig_handler(int sig, short event, void *arg)
{
	/* signal handler rules don't apply, libevent decouples for us */
	switch (sig) {
	case SIGTERM:
	case SIGINT:
		ospfd_shutdown();
		/* NOTREACHED */
	case SIGHUP:
		if (ospf_reload() == -1)
			log_warnx("configuration reload failed");
		else
			log_debug("configuration reloaded");
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

	fprintf(stderr, "usage: %s [-dnv] [-D macro=value]"
	    " [-f file] [-s socket]\n",
	    __progname);
	exit(1);
}

int
main(int argc, char *argv[])
{
	struct event		 ev_sigint, ev_sigterm, ev_sighup;
	int			 ch, opts = 0;
	int			 debug = 0;
	int			 ipforwarding;
	int			 mib[4];
	size_t			 len;
	char			*sockname = NULL;
	int			 control_fd;

	conffile = CONF_FILE;
	ospfd_process = PROC_MAIN;

	log_init(1, LOG_DAEMON);	/* log to stderr until daemonized */
	log_procinit(log_procnames[ospfd_process]);

	while ((ch = getopt(argc, argv, "cdD:f:s:nv")) != -1) {
		switch (ch) {
		case 'c':
			opts |= OSPFD_OPT_FORCE_DEMOTE;
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
			opts |= OSPFD_OPT_NOACTION;
			break;
		case 's':
			sockname = optarg;
			break;
		case 'v':
			if (opts & OSPFD_OPT_VERBOSE)
				opts |= OSPFD_OPT_VERBOSE2;
			opts |= OSPFD_OPT_VERBOSE;
			log_setverbose(1);
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

	mib[0] = CTL_NET;
	mib[1] = PF_INET6;
	mib[2] = IPPROTO_IPV6;
	mib[3] = IPCTL_FORWARDING;
	len = sizeof(ipforwarding);
	if (sysctl(mib, 4, &ipforwarding, &len, NULL, 0) == -1)
		err(1, "sysctl");

	if (ipforwarding != 1) {
		log_warnx("WARNING: IPv6 forwarding NOT enabled, "
		    "running as stub router");
		opts |= OSPFD_OPT_STUB_ROUTER;
	}

	/* prepare and fetch interfaces early */
	if_init();

	/* parse config file */
	if ((ospfd_conf = parse_config(conffile, opts)) == NULL )
		exit(1);

	if (sockname == NULL) {
		if (asprintf(&sockname, "%s.%d", OSPF6D_SOCKET,
		    ospfd_conf->rdomain) == -1)
			err(1, "asprintf");
	}

	ospfd_conf->csock = sockname;

	if (ospfd_conf->opts & OSPFD_OPT_NOACTION) {
		if (ospfd_conf->opts & OSPFD_OPT_VERBOSE)
			print_config(ospfd_conf);
		else
			fprintf(stderr, "configuration OK\n");
		exit(0);
	}

	/* check for root privileges  */
	if (geteuid())
		errx(1, "need root privileges");

	/* check for ospfd user */
	if (getpwnam(OSPF6D_USER) == NULL)
		errx(1, "unknown user %s", OSPF6D_USER);

	log_init(debug, LOG_DAEMON);
	log_setverbose(ospfd_conf->opts & OSPFD_OPT_VERBOSE);

	if ((control_check(ospfd_conf->csock)) == -1)
		fatalx("ospf6d already running");

	if (!debug)
		daemon(1, 0);

	log_info("startup");

	if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK,
	    PF_UNSPEC, pipe_parent2ospfe) == -1)
		fatal("socketpair");
	if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK,
	    PF_UNSPEC, pipe_parent2rde) == -1)
		fatal("socketpair");
	if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK,
	    PF_UNSPEC, pipe_ospfe2rde) == -1)
		fatal("socketpair");

	/* start children */
	rde_pid = rde(ospfd_conf, pipe_parent2rde, pipe_ospfe2rde,
	    pipe_parent2ospfe);
	ospfe_pid = ospfe(ospfd_conf, pipe_parent2ospfe, pipe_ospfe2rde,
	    pipe_parent2rde);

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
	close(pipe_parent2ospfe[1]);
	close(pipe_parent2rde[1]);
	close(pipe_ospfe2rde[0]);
	close(pipe_ospfe2rde[1]);

	if ((iev_ospfe = malloc(sizeof(struct imsgev))) == NULL ||
	    (iev_rde = malloc(sizeof(struct imsgev))) == NULL)
		fatal(NULL);
	if (imsgbuf_init(&iev_ospfe->ibuf, pipe_parent2ospfe[0]) == -1)
		fatal(NULL);
	imsgbuf_allow_fdpass(&iev_ospfe->ibuf);
	iev_ospfe->handler = main_dispatch_ospfe;
	if (imsgbuf_init(&iev_rde->ibuf, pipe_parent2rde[0]) == -1)
		fatal(NULL);
	iev_rde->handler = main_dispatch_rde;

	/* setup event handler */
	iev_ospfe->events = EV_READ;
	event_set(&iev_ospfe->ev, iev_ospfe->ibuf.fd, iev_ospfe->events,
	    iev_ospfe->handler, iev_ospfe);
	event_add(&iev_ospfe->ev, NULL);

	iev_rde->events = EV_READ;
	event_set(&iev_rde->ev, iev_rde->ibuf.fd, iev_rde->events,
	    iev_rde->handler, iev_rde);
	event_add(&iev_rde->ev, NULL);

	if ((control_fd = control_init(ospfd_conf->csock)) == -1)
		fatalx("control socket setup failed");
	main_imsg_compose_ospfe_fd(IMSG_CONTROLFD, 0, control_fd);

	/* no filesystem visibility */
	if (unveil("/", "") == -1)
		fatal("unveil /");
	if (unveil(NULL, NULL) == -1)
		fatal("unveil");

	if (kr_init(!(ospfd_conf->flags & OSPFD_FLAG_NO_FIB_UPDATE),
	    ospfd_conf->rdomain, ospfd_conf->redist_label_or_prefix,
	    ospfd_conf->fib_priority) == -1)
		fatalx("kr_init failed");

	event_dispatch();

	ospfd_shutdown();
	/* NOTREACHED */
	return (0);
}

__dead void
ospfd_shutdown(void)
{
	pid_t	pid;
	int	status;

	/* close pipes */
	imsgbuf_clear(&iev_ospfe->ibuf);
	close(iev_ospfe->ibuf.fd);
	imsgbuf_clear(&iev_rde->ibuf);
	close(iev_rde->ibuf.fd);

	control_cleanup();
	kr_shutdown();
	carp_demote_shutdown();

	log_debug("waiting for children to terminate");
	do {
		pid = wait(&status);
		if (pid == -1) {
			if (errno != EINTR && errno != ECHILD)
				fatal("wait");
		} else if (WIFSIGNALED(status))
			log_warnx("%s terminated; signal %d",
			    (pid == rde_pid) ? "route decision engine" :
			    "ospf engine", WTERMSIG(status));
	} while (pid != -1 || (pid == -1 && errno == EINTR));

	free(iev_ospfe);
	free(iev_rde);
	free(ospfd_conf);

	log_info("terminating");
	exit(0);
}

/* imsg handling */
void
main_dispatch_ospfe(int fd, short event, void *bula)
{
	struct imsgev		*iev = bula;
	struct imsgbuf		*ibuf = &iev->ibuf;
	struct imsg		 imsg;
	struct demote_msg	 dmsg;
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
		case IMSG_CTL_RELOAD:
			if (ospf_reload() == -1)
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
		case IMSG_CTL_FIB_RELOAD:
			kr_fib_reload();
			break;
		case IMSG_CTL_KROUTE:
		case IMSG_CTL_KROUTE_ADDR:
			kr_show_route(&imsg);
			break;
		case IMSG_DEMOTE:
			if (imsg.hdr.len - IMSG_HEADER_SIZE != sizeof(dmsg))
				fatalx("invalid size of OE request");
			memcpy(&dmsg, imsg.data, sizeof(dmsg));
			carp_demote_set(dmsg.demote_group, dmsg.level);
			break;
		case IMSG_CTL_LOG_VERBOSE:
			/* already checked by ospfe */
			memcpy(&verbose, imsg.data, sizeof(verbose));
			log_setverbose(verbose);
			break;
		default:
			log_debug("main_dispatch_ospfe: error handling imsg %d",
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
main_dispatch_rde(int fd, short event, void *bula)
{
	struct imsgev	*iev = bula;
	struct imsgbuf	*ibuf = &iev->ibuf;
	struct imsg	 imsg;
	ssize_t		 n;
	int		 count, shut = 0;

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
			count = (imsg.hdr.len - IMSG_HEADER_SIZE) /
			    sizeof(struct kroute);
			if (kr_change(imsg.data, count))
				log_warn("main_dispatch_rde: error changing "
				    "route");
			break;
		case IMSG_KROUTE_DELETE:
			if (kr_delete(imsg.data))
				log_warn("main_dispatch_rde: error deleting "
				    "route");
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
main_imsg_compose_ospfe(int type, pid_t pid, void *data, u_int16_t datalen)
{
	if (iev_ospfe == NULL)
		return;
	imsg_compose_event(iev_ospfe, type, 0, pid, -1, data, datalen);
}

void
main_imsg_compose_ospfe_fd(int type, pid_t pid, int fd)
{
	if (iev_ospfe == NULL)
		return;
	imsg_compose_event(iev_ospfe, type, 0, pid, fd, NULL, 0);
}

void
main_imsg_compose_rde(int type, pid_t pid, void *data, u_int16_t datalen)
{
	if (iev_rde == NULL)
		return;
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
imsg_compose_event(struct imsgev *iev, u_int16_t type, u_int32_t peerid,
    pid_t pid, int fd, void *data, u_int16_t datalen)
{
	int	ret;

	if ((ret = imsg_compose(&iev->ibuf, type, peerid,
	    pid, fd, data, datalen)) != -1)
		imsg_event_add(iev);
	return (ret);
}

int
ospf_redistribute(struct kroute *kr, u_int32_t *metric)
{
	struct redistribute	*r;
	struct in6_addr		 ina, inb;
	struct iface		*iface;
	u_int8_t		 is_default = 0;
	int			 depend_ok;

	/* only allow ::/0 via REDIST_DEFAULT */
	if (IN6_IS_ADDR_UNSPECIFIED(&kr->prefix) && kr->prefixlen == 0)
		is_default = 1;

	SIMPLEQ_FOREACH(r, &ospfd_conf->redist_list, entry) {
		if (r->dependon[0] != '\0') {
			if ((iface = if_findname(r->dependon)))
				depend_ok = ifstate_is_up(iface);
			else
				depend_ok = 0;
		} else
			depend_ok = 1;

		switch (r->type & ~REDIST_NO) {
		case REDIST_LABEL:
			if (kr->rtlabel == r->label) {
				*metric = depend_ok ? r->metric :
				    r->metric | MAX_METRIC;
				return (r->type & REDIST_NO ? 0 : 1);
			}
			break;
		case REDIST_STATIC:
			/*
			 * Dynamic routes are not redistributable. Placed here
			 * so that link local addresses can be redistributed
			 * via a rtlabel.
			 */
			if (is_default)
				continue;
			if (kr->flags & F_DYNAMIC)
				continue;
			if (kr->flags & F_STATIC) {
				*metric = depend_ok ? r->metric :
				    r->metric | MAX_METRIC;
				return (r->type & REDIST_NO ? 0 : 1);
			}
			break;
		case REDIST_CONNECTED:
			if (is_default)
				continue;
			if (kr->flags & F_DYNAMIC)
				continue;
			if (kr->flags & F_CONNECTED) {
				*metric = depend_ok ? r->metric :
				    r->metric | MAX_METRIC;
				return (r->type & REDIST_NO ? 0 : 1);
			}
			break;
		case REDIST_ADDR:
			if (kr->flags & F_DYNAMIC)
				continue;

			if (IN6_IS_ADDR_UNSPECIFIED(&r->addr) &&
			    r->prefixlen == 0) {
				if (is_default) {
					*metric = depend_ok ? r->metric :
					    r->metric | MAX_METRIC;
					return (r->type & REDIST_NO ? 0 : 1);
				} else
					return (0);
			}

			inet6applymask(&ina, &kr->prefix, r->prefixlen);
			inet6applymask(&inb, &r->addr, r->prefixlen);
			if (IN6_ARE_ADDR_EQUAL(&ina, &inb) &&
			    kr->prefixlen >= r->prefixlen) {
				*metric = depend_ok ? r->metric :
				    r->metric | MAX_METRIC;
				return (r->type & REDIST_NO ? 0 : 1);
			}
			break;
		case REDIST_DEFAULT:
			if (is_default) {
				*metric = depend_ok ? r->metric :
				    r->metric | MAX_METRIC;
				return (r->type & REDIST_NO ? 0 : 1);
			}
			break;
		}
	}

	return (0);
}

int
ospf_reload(void)
{
#ifdef notyet
	struct area		*area;
	struct ospfd_conf	*xconf;

	if ((xconf = parse_config(conffile, ospfd_conf->opts)) == NULL)
		return (-1);

	/* XXX bail out if router-id changed */

	/* send config to childs */
	if (ospf_sendboth(IMSG_RECONF_CONF, xconf, sizeof(*xconf)) == -1)
		return (-1);

	/* send areas, interfaces happen out of band */
	LIST_FOREACH(area, &xconf->area_list, entry) {
		if (ospf_sendboth(IMSG_RECONF_AREA, area, sizeof(*area)) == -1)
			return (-1);
	}

	if (ospf_sendboth(IMSG_RECONF_END, NULL, 0) == -1)
		return (-1);

	/* XXX send newly available interfaces to the childs */

	merge_config(ospfd_conf, xconf);
	/* update redistribute lists */
	kr_reload(ospfd_conf->redist_label_or_prefix);
	return (0);
#else
	return (-1);
#endif
}

int
ospf_sendboth(enum imsg_type type, void *buf, u_int16_t len)
{
	if (imsg_compose_event(iev_ospfe, type, 0, 0, -1, buf, len) == -1)
		return (-1);
	if (imsg_compose_event(iev_rde, type, 0, 0, -1, buf, len) == -1)
		return (-1);
	return (0);
}

void
merge_config(struct ospfd_conf *conf, struct ospfd_conf *xconf)
{
	struct area		*a, *xa, *na;
	struct iface		*iface;
	struct redistribute	*r;
	int			 rchange = 0;

	/* change of rtr_id needs a restart */
	conf->flags = xconf->flags;
	conf->spf_delay = xconf->spf_delay;
	conf->spf_hold_time = xconf->spf_hold_time;
	if (SIMPLEQ_EMPTY(&conf->redist_list) !=
	    SIMPLEQ_EMPTY(&xconf->redist_list))
		rchange = 1;
	conf->redist_label_or_prefix = xconf->redist_label_or_prefix;

	if (ospfd_process == PROC_MAIN) {
		/* main process does neither use areas nor interfaces */
		while ((r = SIMPLEQ_FIRST(&conf->redist_list)) != NULL) {
			SIMPLEQ_REMOVE_HEAD(&conf->redist_list, entry);
			free(r);
		}
		SIMPLEQ_CONCAT(&conf->redist_list, &xconf->redist_list);

		/* adjust FIB priority if changed */
		if (conf->fib_priority != xconf->fib_priority) {
			kr_fib_decouple();
			kr_fib_update_prio(xconf->fib_priority);
			conf->fib_priority = xconf->fib_priority;
			kr_fib_couple();
		}

		goto done;
	}

	/* merge areas and interfaces */
	for (a = LIST_FIRST(&conf->area_list); a != NULL; a = na) {
		na = LIST_NEXT(a, entry);
		/* find deleted areas */
		if ((xa = area_find(xconf, a->id)) == NULL) {
			if (ospfd_process == PROC_OSPF_ENGINE) {
				LIST_FOREACH(iface, &a->iface_list, entry)
					if_fsm(iface, IF_EVT_DOWN);
			}
			LIST_REMOVE(a, entry);
			area_del(a);
		}
	}

	for (xa = LIST_FIRST(&xconf->area_list); xa != NULL; xa = na) {
		na = LIST_NEXT(xa, entry);
		if ((a = area_find(conf, xa->id)) == NULL) {
			LIST_REMOVE(xa, entry);
			LIST_INSERT_HEAD(&conf->area_list, xa, entry);
			if (ospfd_process == PROC_OSPF_ENGINE) {
				/* start interfaces */
				ospfe_demote_area(xa, 0);
				LIST_FOREACH(iface, &xa->iface_list, entry)
					if_start(conf, iface);
			}
			/* no need to merge interfaces */
			continue;
		}
		/*
		 * stub is not yet used but switching between stub and normal
		 * will be another painful job.
		 */
		a->stub = xa->stub;
		a->stub_default_cost = xa->stub_default_cost;
		if (ospfd_process == PROC_RDE_ENGINE)
			a->dirty = 1; /* force SPF tree recalculation */

		/* merge interfaces */
		if (merge_interfaces(a, xa) &&
		    ospfd_process == PROC_OSPF_ENGINE)
			a->dirty = 1; /* force rtr LSA update */
	}

	if (ospfd_process == PROC_OSPF_ENGINE) {
		LIST_FOREACH(a, &conf->area_list, entry) {
			LIST_FOREACH(iface, &a->iface_list, entry) {
				if (iface->state == IF_STA_NEW) {
					iface->state = IF_STA_DOWN;
					if_start(conf, iface);
				}
			}
			if (a->dirty || rchange) {
				a->dirty = 0;
				orig_rtr_lsa(LIST_FIRST(&a->iface_list)->area);
			}
		}
	}

done:
	while ((a = LIST_FIRST(&xconf->area_list)) != NULL) {
		LIST_REMOVE(a, entry);
		area_del(a);
	}
	free(xconf);
}

int
merge_interfaces(struct area *a, struct area *xa)
{
	struct iface	*i, *xi, *ni;
	int		 dirty = 0;

	/* problems:
	 * - new interfaces (easy)
	 * - deleted interfaces (needs to be done via fsm?)
	 * - changing passive (painful?)
	 */
	for (i = LIST_FIRST(&a->iface_list); i != NULL; i = ni) {
		ni = LIST_NEXT(i, entry);
		if (iface_lookup(xa, i) == NULL) {
			log_debug("merge_interfaces: proc %d area %s removing "
			    "interface %s", ospfd_process, inet_ntoa(a->id),
			    i->name);
			if (ospfd_process == PROC_OSPF_ENGINE)
				if_fsm(i, IF_EVT_DOWN);
			LIST_REMOVE(i, entry);
			if_del(i);
		}
	}

	for (xi = LIST_FIRST(&xa->iface_list); xi != NULL; xi = ni) {
		ni = LIST_NEXT(xi, entry);
		if ((i = iface_lookup(a, xi)) == NULL) {
			/* new interface but delay initialisation */
			log_debug("merge_interfaces: proc %d area %s adding "
			    "interface %s", ospfd_process, inet_ntoa(a->id),
			    xi->name);
			LIST_REMOVE(xi, entry);
			LIST_INSERT_HEAD(&a->iface_list, xi, entry);
			if (ospfd_process == PROC_OSPF_ENGINE)
				xi->state = IF_STA_NEW;
			continue;
		}
		log_debug("merge_interfaces: proc %d area %s merging "
		    "interface %s", ospfd_process, inet_ntoa(a->id), i->name);
		i->addr = xi->addr;
		i->dst = xi->dst;
		i->abr_id = xi->abr_id;
		i->baudrate = xi->baudrate;
		i->dead_interval = xi->dead_interval;
		i->mtu = xi->mtu;
		i->transmit_delay = xi->transmit_delay;
		i->hello_interval = xi->hello_interval;
		i->rxmt_interval = xi->rxmt_interval;
		if (i->metric != xi->metric)
			dirty = 1;
		i->metric = xi->metric;
		i->priority = xi->priority;
		if (i->self)
			i->self->priority = i->priority;
		i->flags = xi->flags; /* needed? */
		i->type = xi->type; /* needed? */
		i->if_type = xi->if_type; /* needed? */
		i->linkstate = xi->linkstate; /* needed? */

#if 0 /* XXX needs some kind of love */
		if (i->passive != xi->passive) {
			/* need to restart interface to cope with this change */
			if (ospfd_process == PROC_OSPF_ENGINE)
				if_fsm(i, IF_EVT_DOWN);
			i->passive = xi->passive;
			if (ospfd_process == PROC_OSPF_ENGINE)
				if_fsm(i, IF_EVT_UP);
		}
#endif
	}
	return (dirty);
}

struct iface *
iface_lookup(struct area *area, struct iface *iface)
{
	struct iface	*i;

	LIST_FOREACH(i, &area->iface_list, entry)
		if (i->ifindex == iface->ifindex)
			return (i);
	return (NULL);
}

int
ifstate_is_up(struct iface *iface)
{
	if (!(iface->flags & IFF_UP))
		return (0);
	if (iface->if_type == IFT_CARP &&
	    iface->linkstate == LINK_STATE_UNKNOWN)
		return (0);
	return LINK_STATE_IS_UP(iface->linkstate);
}
