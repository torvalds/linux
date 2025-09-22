/*	$OpenBSD: ifstated.c,v 1.68 2024/04/23 13:34:51 jsg Exp $	*/

/*
 * Copyright (c) 2004 Marco Pfatschbacher <mpf@openbsd.org>
 * Copyright (c) 2004 Ryan McBride <mcbride@openbsd.org>
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

/*
 * ifstated listens to link_state transitions on interfaces
 * and executes predefined commands.
 */

#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/wait.h>

#include <net/if.h>
#include <net/route.h>
#include <netinet/in.h>

#include <paths.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <stdint.h>
#include <syslog.h>
#include <errno.h>
#include <event.h>
#include <unistd.h>
#include <ifaddrs.h>

#include "ifstated.h"
#include "log.h"

struct	 ifsd_config *conf, *newconf;

int	 opts;
int	 opt_inhibit;
char	*configfile = "/etc/ifstated.conf";
struct event	rt_msg_ev, sighup_ev, startup_ev, sigchld_ev;

void		startup_handler(int, short, void *);
void		sighup_handler(int, short, void *);
int		load_config(void);
void		sigchld_handler(int, short, void *);
void		rt_msg_handler(int, short, void *);
void		external_handler(int, short, void *);
void		external_exec(struct ifsd_external *, int);
void		check_external_status(struct ifsd_state *);
void		check_ifdeparture(void);
void		external_evtimer_setup(struct ifsd_state *, int);
void		scan_ifstate(const char *, int, int);
int		scan_ifstate_single(const char *, int, struct ifsd_state *);
void		fetch_ifstate(int);
__dead void	usage(void);
void		adjust_expressions(struct ifsd_expression_list *, int);
void		adjust_external_expressions(struct ifsd_state *);
void		eval_state(struct ifsd_state *);
int		state_change(void);
void		do_action(struct ifsd_action *);
void		remove_action(struct ifsd_action *, struct ifsd_state *);
void		remove_expression(struct ifsd_expression *,
		    struct ifsd_state *);

__dead void
usage(void)
{
	extern char *__progname;

	fprintf(stderr, "usage: %s [-dhinv] [-D macro=value] [-f file]\n",
	    __progname);
	exit(1);
}

int
main(int argc, char *argv[])
{
	struct timeval tv;
	int ch, rt_fd;
	int debug = 0;
	unsigned int rtfilter;

	log_init(1, LOG_DAEMON);	/* log to stderr until daemonized */
	log_setverbose(1);

	while ((ch = getopt(argc, argv, "dD:f:hniv")) != -1) {
		switch (ch) {
		case 'd':
			debug = 1;
			break;
		case 'D':
			if (cmdline_symset(optarg) < 0)
				fatalx("could not parse macro definition %s",
				    optarg);
			break;
		case 'f':
			configfile = optarg;
			break;
		case 'h':
			usage();
			break;
		case 'n':
			opts |= IFSD_OPT_NOACTION;
			break;
		case 'i':
			opt_inhibit = 1;
			break;
		case 'v':
			if (opts & IFSD_OPT_VERBOSE)
				opts |= IFSD_OPT_VERBOSE2;
			opts |= IFSD_OPT_VERBOSE;
			break;
		default:
			usage();
		}
	}

	argc -= optind;
	argv += optind;
	if (argc > 0)
		usage();

	if (opts & IFSD_OPT_NOACTION) {
		if ((newconf = parse_config(configfile, opts)) == NULL)
			exit(1);
		fprintf(stderr, "configuration OK\n");
		exit(0);
	}

	if (!debug)
		daemon(1, 0);

	event_init();
	log_init(debug, LOG_DAEMON);
	log_setverbose(opts & IFSD_OPT_VERBOSE);

	if ((rt_fd = socket(AF_ROUTE, SOCK_RAW, 0)) == -1)
		fatal("no routing socket");

	rtfilter = ROUTE_FILTER(RTM_IFINFO) | ROUTE_FILTER(RTM_IFANNOUNCE);
	if (setsockopt(rt_fd, AF_ROUTE, ROUTE_MSGFILTER,
	    &rtfilter, sizeof(rtfilter)) == -1)	/* not fatal */
		log_warn("%s: setsockopt msgfilter", __func__);

	rtfilter = RTABLE_ANY;
	if (setsockopt(rt_fd, AF_ROUTE, ROUTE_TABLEFILTER,
	    &rtfilter, sizeof(rtfilter)) == -1)	/* not fatal */
		log_warn("%s: setsockopt tablefilter", __func__);

	if (unveil(configfile, "r") == -1)
		fatal("unveil %s", configfile);
	if (unveil(_PATH_BSHELL, "x") == -1)
		fatal("unveil %s", _PATH_BSHELL);
	if (pledge("stdio rpath route proc exec", NULL) == -1)
		fatal("pledge");

	signal_set(&sigchld_ev, SIGCHLD, sigchld_handler, NULL);
	signal_add(&sigchld_ev, NULL);

	/* Loading the config needs to happen in the event loop */
	timerclear(&tv);
	evtimer_set(&startup_ev, startup_handler, (void *)(long)rt_fd);
	evtimer_add(&startup_ev, &tv);

	event_loop(0);
	exit(0);
}

void
startup_handler(int fd, short event, void *arg)
{
	int rfd = (int)(long)arg;

	if (load_config() != 0) {
		log_warnx("unable to load config");
		exit(1);
	}

	event_set(&rt_msg_ev, rfd, EV_READ|EV_PERSIST, rt_msg_handler, NULL);
	event_add(&rt_msg_ev, NULL);

	signal_set(&sighup_ev, SIGHUP, sighup_handler, NULL);
	signal_add(&sighup_ev, NULL);

	log_info("started");
}

void
sighup_handler(int fd, short event, void *arg)
{
	log_info("reloading config");
	if (load_config() != 0)
		log_warnx("unable to reload config");
}

int
load_config(void)
{
	if ((newconf = parse_config(configfile, opts)) == NULL)
		return (-1);
	if (conf != NULL)
		clear_config(conf);
	conf = newconf;
	conf->initstate.entered = time(NULL);
	fetch_ifstate(0);
	external_evtimer_setup(&conf->initstate, IFSD_EVTIMER_ADD);
	adjust_external_expressions(&conf->initstate);
	eval_state(&conf->initstate);
	if (conf->curstate != NULL) {
		log_info("initial state: %s", conf->curstate->name);
		conf->curstate->entered = time(NULL);
		conf->nextstate = conf->curstate;
		conf->curstate = NULL;
		while (state_change()) {
			do_action(conf->curstate->init);
			do_action(conf->curstate->body);
		}
	}
	return (0);
}

void
rt_msg_handler(int fd, short event, void *arg)
{
	char msg[2048];
	struct rt_msghdr *rtm = (struct rt_msghdr *)&msg;
	struct if_msghdr ifm;
	struct if_announcemsghdr ifan;
	char ifnamebuf[IFNAMSIZ];
	char *ifname;
	ssize_t len;

	if ((len = read(fd, msg, sizeof(msg))) == -1) {
		if (errno == EAGAIN || errno == EINTR)
			return;
		fatal("%s: routing socket read error", __func__);
	}

	if (len == 0)
		fatal("%s: routing socket closed", __func__);

	if (rtm->rtm_version != RTM_VERSION)
		return;

	switch (rtm->rtm_type) {
	case RTM_IFINFO:
		memcpy(&ifm, rtm, sizeof(ifm));
		ifname = if_indextoname(ifm.ifm_index, ifnamebuf);
		/* ifname is NULL on interface departure */
		if (ifname != NULL)
			scan_ifstate(ifname, ifm.ifm_data.ifi_link_state, 1);
		break;
	case RTM_IFANNOUNCE:
		memcpy(&ifan, rtm, sizeof(ifan));
		switch (ifan.ifan_what) {
		case IFAN_DEPARTURE:
			log_warnx("interface %s departed", ifan.ifan_name);
			check_ifdeparture();
			break;
		case IFAN_ARRIVAL:
			log_warnx("interface %s arrived", ifan.ifan_name);
			fetch_ifstate(1);
			break;
		}
		break;
	case RTM_DESYNC:
		/* we lost some routing messages so rescan interfaces */
		check_ifdeparture();
		fetch_ifstate(1);
		break;
	}
	return;
}

void
sigchld_handler(int fd, short event, void *arg)
{
	check_external_status(&conf->initstate);
	if (conf->curstate != NULL)
		check_external_status(conf->curstate);
}

void
external_handler(int fd, short event, void *arg)
{
	struct ifsd_external *external = (struct ifsd_external *)arg;
	struct timeval tv;

	/* re-schedule */
	timerclear(&tv);
	tv.tv_sec = external->frequency;
	evtimer_set(&external->ev, external_handler, external);
	evtimer_add(&external->ev, &tv);

	/* execute */
	external_exec(external, 1);
}

void
external_exec(struct ifsd_external *external, int async)
{
	char *argp[] = {"sh", "-c", NULL, NULL};
	pid_t pid;
	int s;

	if (external->pid > 0) {
		log_debug("previous command %s [%d] still running, killing it",
		    external->command, external->pid);
		kill(external->pid, SIGKILL);
		waitpid(external->pid, &s, 0);
		external->pid = 0;
	}

	argp[2] = external->command;
	log_debug("running %s", external->command);
	pid = fork();
	if (pid == -1) {
		log_warn("fork error");
	} else if (pid == 0) {
		execv(_PATH_BSHELL, argp);
		_exit(1);
		/* NOTREACHED */
	} else {
		external->pid = pid;
	}
	if (!async) {
		waitpid(external->pid, &s, 0);
		external->pid = 0;
		if (WIFEXITED(s))
			external->prevstatus = WEXITSTATUS(s);
	}
}

void
adjust_external_expressions(struct ifsd_state *state)
{
	struct ifsd_external *external;
	struct ifsd_expression_list expressions;

	TAILQ_INIT(&expressions);
	TAILQ_FOREACH(external, &state->external_tests, entries) {
		struct ifsd_expression *expression;

		if (external->prevstatus == -1)
			continue;

		TAILQ_FOREACH(expression, &external->expressions, entries) {
			TAILQ_INSERT_TAIL(&expressions,
			    expression, eval);
			expression->truth = !external->prevstatus;
		}
		adjust_expressions(&expressions, conf->maxdepth);
	}
}

void
check_external_status(struct ifsd_state *state)
{
	struct ifsd_external *external, *end = NULL;
	int status, s, changed = 0;

	/* Do this manually; change ordering so the oldest is first */
	external = TAILQ_FIRST(&state->external_tests);
	while (external != NULL && external != end) {
		struct ifsd_external *newexternal;

		newexternal = TAILQ_NEXT(external, entries);

		if (external->pid <= 0)
			goto loop;

		if (wait4(external->pid, &s, WNOHANG, NULL) == 0)
			goto loop;

		external->pid = 0;
		if (end == NULL)
			end = external;
		if (WIFEXITED(s))
			status = WEXITSTATUS(s);
		else {
			log_warnx("%s exited abnormally", external->command);
			goto loop;
		}

		if (external->prevstatus != status &&
		    (external->prevstatus != -1 || !opt_inhibit)) {
			changed = 1;
			external->prevstatus = status;
		}
		external->lastexec = time(NULL);
		TAILQ_REMOVE(&state->external_tests, external, entries);
		TAILQ_INSERT_TAIL(&state->external_tests, external, entries);
loop:
		external = newexternal;
	}

	if (changed) {
		adjust_external_expressions(state);
		eval_state(state);
	}
}

void
external_evtimer_setup(struct ifsd_state *state, int action)
{
	struct ifsd_external *external;
	int s;

	if (state != NULL) {
		switch (action) {
		case IFSD_EVTIMER_ADD:
			TAILQ_FOREACH(external,
			    &state->external_tests, entries) {
				struct timeval tv;

				/* run it once right away */
				external_exec(external, 0);

				/* schedule it for later */
				timerclear(&tv);
				tv.tv_sec = external->frequency;
				evtimer_set(&external->ev, external_handler,
				    external);
				evtimer_add(&external->ev, &tv);
			}
			break;
		case IFSD_EVTIMER_DEL:
			TAILQ_FOREACH(external,
			    &state->external_tests, entries) {
				if (external->pid > 0) {
					kill(external->pid, SIGKILL);
					waitpid(external->pid, &s, 0);
					external->pid = 0;
				}
				evtimer_del(&external->ev);
			}
			break;
		}
	}
}

#define	LINK_STATE_IS_DOWN(_s)		(!LINK_STATE_IS_UP((_s)))

int
scan_ifstate_single(const char *ifname, int s, struct ifsd_state *state)
{
	struct ifsd_ifstate *ifstate;
	struct ifsd_expression_list expressions;
	int changed = 0;

	TAILQ_INIT(&expressions);

	TAILQ_FOREACH(ifstate, &state->interface_states, entries) {
		if (strcmp(ifstate->ifname, ifname) == 0) {
			if (ifstate->prevstate != s &&
			    (ifstate->prevstate != -1 || !opt_inhibit)) {
				struct ifsd_expression *expression;
				int truth;

				truth =
				    (ifstate->ifstate == IFSD_LINKUNKNOWN &&
				    s == LINK_STATE_UNKNOWN) ||
				    (ifstate->ifstate == IFSD_LINKDOWN &&
				    LINK_STATE_IS_DOWN(s)) ||
				    (ifstate->ifstate == IFSD_LINKUP &&
				    LINK_STATE_IS_UP(s));

				TAILQ_FOREACH(expression,
				    &ifstate->expressions, entries) {
					expression->truth = truth;
					TAILQ_INSERT_TAIL(&expressions,
					    expression, eval);
					changed = 1;
				}
				ifstate->prevstate = s;
			}
		}
	}

	if (changed)
		adjust_expressions(&expressions, conf->maxdepth);
	return (changed);
}

void
scan_ifstate(const char *ifname, int s, int do_eval)
{
	struct ifsd_state *state;
	int cur_eval = 0;

	if (scan_ifstate_single(ifname, s, &conf->initstate) && do_eval)
		eval_state(&conf->initstate);
	TAILQ_FOREACH(state, &conf->states, entries) {
		if (scan_ifstate_single(ifname, s, state) &&
		    (do_eval && state == conf->curstate))
			cur_eval = 1;
	}
	/* execute actions _after_ all expressions have been adjusted */
	if (cur_eval)
		eval_state(conf->curstate);
}

/*
 * Do a bottom-up adjustment of the expression tree's truth value,
 * level-by-level to ensure that each expression's subexpressions have been
 * evaluated.
 */
void
adjust_expressions(struct ifsd_expression_list *expressions, int depth)
{
	struct ifsd_expression_list nexpressions;
	struct ifsd_expression *expression;

	TAILQ_INIT(&nexpressions);
	while ((expression = TAILQ_FIRST(expressions)) != NULL) {
		TAILQ_REMOVE(expressions, expression, eval);
		if (expression->depth == depth) {
			struct ifsd_expression *te;

			switch (expression->type) {
			case IFSD_OPER_AND:
				expression->truth = expression->left->truth &&
				    expression->right->truth;
				break;
			case IFSD_OPER_OR:
				expression->truth = expression->left->truth ||
				    expression->right->truth;
				break;
			case IFSD_OPER_NOT:
				expression->truth = !expression->right->truth;
				break;
			default:
				break;
			}
			if (expression->parent != NULL) {
				if (TAILQ_EMPTY(&nexpressions))
					te = NULL;
				TAILQ_FOREACH(te, &nexpressions, eval)
					if (expression->parent == te)
						break;
				if (te == NULL)
					TAILQ_INSERT_TAIL(&nexpressions,
					    expression->parent, eval);
			}
		} else
			TAILQ_INSERT_TAIL(&nexpressions, expression, eval);
	}
	if (depth > 0)
		adjust_expressions(&nexpressions, depth - 1);
}

void
eval_state(struct ifsd_state *state)
{
	struct ifsd_external *external;

	external = TAILQ_FIRST(&state->external_tests);
	if (external == NULL || external->lastexec >= state->entered ||
	    external->lastexec == 0) {
		do_action(state->body);
		while (state_change()) {
			do_action(conf->curstate->init);
			do_action(conf->curstate->body);
		}
	}
}

int
state_change(void)
{
	if (conf->nextstate != NULL && conf->curstate != conf->nextstate) {
		log_info("changing state to %s", conf->nextstate->name);
		if (conf->curstate != NULL) {
			evtimer_del(&conf->curstate->ev);
			external_evtimer_setup(conf->curstate,
			    IFSD_EVTIMER_DEL);
		}
		conf->curstate = conf->nextstate;
		conf->nextstate = NULL;
		conf->curstate->entered = time(NULL);
		external_evtimer_setup(conf->curstate, IFSD_EVTIMER_ADD);
		adjust_external_expressions(conf->curstate);
		return (1);
	}
	return (0);
}

/*
 * Run recursively through the tree of actions.
 */
void
do_action(struct ifsd_action *action)
{
	struct ifsd_action *subaction;

	switch (action->type) {
	case IFSD_ACTION_COMMAND:
		log_debug("running %s", action->act.command);
		system(action->act.command);
		break;
	case IFSD_ACTION_CHANGESTATE:
		conf->nextstate = action->act.nextstate;
		break;
	case IFSD_ACTION_CONDITION:
		if ((action->act.c.expression != NULL &&
		    action->act.c.expression->truth) ||
		    action->act.c.expression == NULL) {
			TAILQ_FOREACH(subaction, &action->act.c.actions,
			    entries)
				do_action(subaction);
		}
		break;
	default:
		log_debug("%s: unknown action %d", __func__, action->type);
		break;
	}
}

/*
 * Fetch the current link states.
 */
void
fetch_ifstate(int do_eval)
{
	struct ifaddrs *ifap, *ifa;

	if (getifaddrs(&ifap) != 0)
		fatal("getifaddrs");

	for (ifa = ifap; ifa; ifa = ifa->ifa_next) {
		if (ifa->ifa_addr != NULL &&
		    ifa->ifa_addr->sa_family == AF_LINK) {
			struct if_data *ifdata = ifa->ifa_data;
			scan_ifstate(ifa->ifa_name, ifdata->ifi_link_state,
			    do_eval);
		}
	}

	freeifaddrs(ifap);
}

void
check_ifdeparture(void)
{
	struct ifsd_state *state;
	struct ifsd_ifstate *ifstate;

	TAILQ_FOREACH(state, &conf->states, entries) {
		TAILQ_FOREACH(ifstate, &state->interface_states, entries) {
			if (if_nametoindex(ifstate->ifname) == 0)
				scan_ifstate(ifstate->ifname,
				    LINK_STATE_DOWN, 1);
		}
	}
}

void
clear_config(struct ifsd_config *oconf)
{
	struct ifsd_state *state;

	external_evtimer_setup(&conf->initstate, IFSD_EVTIMER_DEL);
	if (conf != NULL && conf->curstate != NULL)
		external_evtimer_setup(conf->curstate, IFSD_EVTIMER_DEL);
	while ((state = TAILQ_FIRST(&oconf->states)) != NULL) {
		TAILQ_REMOVE(&oconf->states, state, entries);
		remove_action(state->init, state);
		remove_action(state->body, state);
		free(state->name);
		free(state);
	}
	remove_action(oconf->initstate.init, &oconf->initstate);
	remove_action(oconf->initstate.body, &oconf->initstate);
	free(oconf);
}

void
remove_action(struct ifsd_action *action, struct ifsd_state *state)
{
	struct ifsd_action *subaction;

	if (action == NULL || state == NULL)
		return;

	switch (action->type) {
	case IFSD_ACTION_COMMAND:
		free(action->act.command);
		break;
	case IFSD_ACTION_CHANGESTATE:
		break;
	case IFSD_ACTION_CONDITION:
		if (action->act.c.expression != NULL)
			remove_expression(action->act.c.expression, state);
		while ((subaction =
		    TAILQ_FIRST(&action->act.c.actions)) != NULL) {
			TAILQ_REMOVE(&action->act.c.actions,
			    subaction, entries);
			remove_action(subaction, state);
		}
	}
	free(action);
}

void
remove_expression(struct ifsd_expression *expression,
    struct ifsd_state *state)
{
	switch (expression->type) {
	case IFSD_OPER_IFSTATE:
		TAILQ_REMOVE(&expression->u.ifstate->expressions, expression,
		    entries);
		if (--expression->u.ifstate->refcount == 0) {
			TAILQ_REMOVE(&state->interface_states,
			    expression->u.ifstate, entries);
			free(expression->u.ifstate);
		}
		break;
	case IFSD_OPER_EXTERNAL:
		TAILQ_REMOVE(&expression->u.external->expressions, expression,
		    entries);
		if (--expression->u.external->refcount == 0) {
			TAILQ_REMOVE(&state->external_tests,
			    expression->u.external, entries);
			free(expression->u.external->command);
			event_del(&expression->u.external->ev);
			free(expression->u.external);
		}
		break;
	default:
		if (expression->left != NULL)
			remove_expression(expression->left, state);
		if (expression->right != NULL)
			remove_expression(expression->right, state);
		break;
	}
	free(expression);
}
