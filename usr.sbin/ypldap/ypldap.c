/*	$OpenBSD: ypldap.c,v 1.16 2015/11/02 10:06:06 jmatthew Exp $ */
/*	$FreeBSD */

/*
 * Copyright (c) 2008 Pierre-Yves Ritschard <pyr@openbsd.org>
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
#include <sys/param.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/signal.h>
#include <sys/tree.h>
#include <sys/wait.h>

#include <netinet/in.h>
#include <arpa/inet.h>

#include <err.h>
#include <errno.h>
#include <event.h>
#include <unistd.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include "ypldap.h"

__dead2 void	 usage(void);
int		 check_child(pid_t, const char *);
void		 main_sig_handler(int, short, void *);
void		 main_shutdown(void);
void		 main_dispatch_client(int, short, void *);
void		 main_configure_client(struct env *);
void		 main_init_timer(int, short, void *);
void		 main_start_update(struct env *);
void		 main_trash_update(struct env *);
void		 main_end_update(struct env *);
int		 main_create_user_groups(struct env *);
void		 purge_config(struct env *);
void		 reconfigure(struct env *);

int		 pipe_main2client[2];

pid_t		 client_pid = 0;
char		*conffile = YPLDAP_CONF_FILE;
int		 opts = 0;

void
usage(void)
{
	extern const char	*__progname;

	fprintf(stderr, "usage: %s [-dnv] [-D macro=value] [-f file]\n",
	    __progname);
	exit(1);
}

int
check_child(pid_t pid, const char *pname)
{
	int	status;

	if (waitpid(pid, &status, WNOHANG) > 0) {
		if (WIFEXITED(status)) {
			log_warnx("check_child: lost child %s exited", pname);
			return (1);
		}
		if (WIFSIGNALED(status)) {
			log_warnx("check_child: lost child %s terminated; "
			    "signal %d", pname, WTERMSIG(status));
			return (1);
		}
	}
	return (0);
}

/* ARGUSED */
void
main_sig_handler(int sig, short event, void *p)
{
	int		 die = 0;

	switch (sig) {
	case SIGTERM:
	case SIGINT:
		die = 1;
		/* FALLTHROUGH */
	case SIGCHLD:
		if (check_child(client_pid, "ldap client")) {
			client_pid = 0;
			die = 1;
		}
		if (die)
			main_shutdown();
		break;
	case SIGHUP:
		/* reconfigure */
		break;
	default:
		fatalx("unexpected signal");
	}
}

void
main_shutdown(void)
{
	_exit(0);
}

void
main_start_update(struct env *env)
{
	env->update_trashed = 0;

	log_debug("starting directory update");
	env->sc_user_line_len = 0;
	env->sc_group_line_len = 0;
	if ((env->sc_user_names_t = calloc(1,
	    sizeof(*env->sc_user_names_t))) == NULL ||
	    (env->sc_group_names_t = calloc(1,
	    sizeof(*env->sc_group_names_t))) == NULL)
		fatal(NULL);
	RB_INIT(env->sc_user_names_t);
	RB_INIT(env->sc_group_names_t);
}

/*
 * XXX: Currently this function should only be called when updating is
 * finished. A notification should be send to ldapclient that it should stop
 * sending new pwd/grp entries before it can be called from different places.
 */
void
main_trash_update(struct env *env)
{
	struct userent	*ue;
	struct groupent	*ge;

	env->update_trashed = 1;

	while ((ue = RB_ROOT(env->sc_user_names_t)) != NULL) {
		RB_REMOVE(user_name_tree,
		    env->sc_user_names_t, ue);
		free(ue->ue_line);
		free(ue->ue_netid_line);
		free(ue);
	}
	free(env->sc_user_names_t);
	env->sc_user_names_t = NULL;
	while ((ge = RB_ROOT(env->sc_group_names_t))
	    != NULL) {
		RB_REMOVE(group_name_tree,
		    env->sc_group_names_t, ge);
		free(ge->ge_line);
		free(ge);
	}
	free(env->sc_group_names_t);
	env->sc_group_names_t = NULL;
}

int
main_create_user_groups(struct env *env)
{
	struct userent		*ue;
	struct userent		 ukey;
	struct groupent		*ge;
	gid_t			 pw_gid;
	char			*bp, *cp;
	char			*p;
	const char		*errstr = NULL;
	size_t			 len;

	RB_FOREACH(ue, user_name_tree, env->sc_user_names_t) {
		bp = cp = ue->ue_line;

		/* name */
		bp += strlen(bp) + 1;

		/* password */
		bp += strcspn(bp, ":") + 1;

		/* uid */
		bp += strcspn(bp, ":") + 1;

		/* gid */
		bp[strcspn(bp, ":")] = '\0';

		pw_gid = (gid_t)strtonum(bp, 0, GID_MAX, &errstr);
		if (errstr) {
			log_warnx("main: failed to parse gid for uid: %d\n", ue->ue_uid);
			return (-1);
		}

		/* bring gid column back to its proper state */
		bp[strlen(bp)] = ':';

		if ((ue->ue_netid_line = calloc(1, LINE_WIDTH)) == NULL) {
			return (-1);
		}

		if (snprintf(ue->ue_netid_line, LINE_WIDTH-1, "%d:%d", ue->ue_uid, pw_gid) >= LINE_WIDTH) {

			return (-1);
		}

		ue->ue_gid = pw_gid;
	}

	RB_FOREACH(ge, group_name_tree, env->sc_group_names_t) {
		bp = cp = ge->ge_line;

		/* name */
		bp += strlen(bp) + 1;

		/* password */
		bp += strcspn(bp, ":") + 1;

		/* gid */
		bp += strcspn(bp, ":") + 1;

		cp = bp;
		if (*bp == '\0')
			continue;
		bp = cp;
		for (;;) { 
			if (!(cp = strsep(&bp, ",")))
				break;
			ukey.ue_line = cp;
			if ((ue = RB_FIND(user_name_tree, env->sc_user_names_t,
			    &ukey)) == NULL) {
				/* User not found */
				log_warnx("main: unknown user %s in group %s\n",
				   ukey.ue_line, ge->ge_line);
				if (bp != NULL)
					*(bp-1) = ',';
				continue;
			}
			if (bp != NULL)
				*(bp-1) = ',';

			/* Make sure the new group doesn't equal to the main gid */
			if (ge->ge_gid == ue->ue_gid)
				continue;

			len = strlen(ue->ue_netid_line);
			p = ue->ue_netid_line + len;

			if ((snprintf(p, LINE_WIDTH-len-1, ",%d",
				ge->ge_gid)) >= (int)(LINE_WIDTH-len)) {
				return (-1);
			}
		}
	}

	return (0);
}

void
main_end_update(struct env *env)
{
	struct userent		*ue;
	struct groupent		*ge;

	if (env->update_trashed)
		return;

	log_debug("updates are over, cleaning up trees now");

	if (main_create_user_groups(env) == -1) {
		main_trash_update(env);
		return;
	}

	if (env->sc_user_names == NULL) {
		env->sc_user_names = env->sc_user_names_t;
		env->sc_user_lines = NULL;
		env->sc_user_names_t = NULL;

		env->sc_group_names = env->sc_group_names_t;
		env->sc_group_lines = NULL;
		env->sc_group_names_t = NULL;

		flatten_entries(env);
		goto make_uids;
	}

	/*
	 * clean previous tree.
	 */
	while ((ue = RB_ROOT(env->sc_user_names)) != NULL) {
		RB_REMOVE(user_name_tree, env->sc_user_names,
		    ue);
		free(ue->ue_netid_line);
		free(ue);
	}
	free(env->sc_user_names);
	free(env->sc_user_lines);

	env->sc_user_names = env->sc_user_names_t;
	env->sc_user_lines = NULL;
	env->sc_user_names_t = NULL;

	while ((ge = RB_ROOT(env->sc_group_names)) != NULL) {
		RB_REMOVE(group_name_tree,
		    env->sc_group_names, ge);
		free(ge);
	}
	free(env->sc_group_names);
	free(env->sc_group_lines);

	env->sc_group_names = env->sc_group_names_t;
	env->sc_group_lines = NULL;
	env->sc_group_names_t = NULL;


	flatten_entries(env);

	/*
	 * trees are flat now. build up uid, gid and netid trees.
	 */

make_uids:
	RB_INIT(&env->sc_user_uids);
	RB_INIT(&env->sc_group_gids);
	RB_FOREACH(ue, user_name_tree, env->sc_user_names)
		RB_INSERT(user_uid_tree,
		    &env->sc_user_uids, ue);
	RB_FOREACH(ge, group_name_tree, env->sc_group_names)
		RB_INSERT(group_gid_tree,
		    &env->sc_group_gids, ge);

}

void
main_dispatch_client(int fd, short events, void *p)
{
	int		 n;
	int		 shut = 0;
	struct env	*env = p;
	struct imsgev	*iev = env->sc_iev;
	struct imsgbuf	*ibuf = &iev->ibuf;
	struct idm_req	 ir;
	struct imsg	 imsg;

	if ((events & (EV_READ | EV_WRITE)) == 0)
		fatalx("unknown event");

	if (events & EV_READ) {
		if ((n = imsg_read(ibuf)) == -1 && errno != EAGAIN)
			fatal("imsg_read error");
		if (n == 0)
			shut = 1;
	}
	if (events & EV_WRITE) {
		if ((n = msgbuf_write(&ibuf->w)) == -1 && errno != EAGAIN)
			fatal("msgbuf_write");
		if (n == 0)
			shut = 1;
		goto done;
	}

	for (;;) {
		if ((n = imsg_get(ibuf, &imsg)) == -1)
			fatal("main_dispatch_client: imsg_get error");
		if (n == 0)
			break;

		switch (imsg.hdr.type) {
		case IMSG_START_UPDATE:
			main_start_update(env);
			break;
		case IMSG_PW_ENTRY: {
			struct userent	*ue;
			size_t		 len;

			if (env->update_trashed)
				break;

			(void)memcpy(&ir, imsg.data, sizeof(ir));
			if ((ue = calloc(1, sizeof(*ue))) == NULL ||
			    (ue->ue_line = strdup(ir.ir_line)) == NULL) {
				/*
				 * should cancel tree update instead.
				 */
				fatal("out of memory");
			}
			ue->ue_uid = ir.ir_key.ik_uid;
			len = strlen(ue->ue_line) + 1;
			ue->ue_line[strcspn(ue->ue_line, ":")] = '\0';
			if (RB_INSERT(user_name_tree, env->sc_user_names_t,
			    ue) != NULL) { /* dup */
				free(ue->ue_line);
				free(ue);
			} else
				env->sc_user_line_len += len;
			break;
		}
		case IMSG_GRP_ENTRY: {
			struct groupent	*ge;
			size_t		 len;

			if (env->update_trashed)
				break;

			(void)memcpy(&ir, imsg.data, sizeof(ir));
			if ((ge = calloc(1, sizeof(*ge))) == NULL ||
			    (ge->ge_line = strdup(ir.ir_line)) == NULL) {
				/*
				 * should cancel tree update instead.
				 */
				fatal("out of memory");
			}
			ge->ge_gid = ir.ir_key.ik_gid;
			len = strlen(ge->ge_line) + 1;
			ge->ge_line[strcspn(ge->ge_line, ":")] = '\0';
			if (RB_INSERT(group_name_tree, env->sc_group_names_t,
			    ge) != NULL) { /* dup */
				free(ge->ge_line);
				free(ge);
			} else
				env->sc_group_line_len += len;
			break;
		}
		case IMSG_TRASH_UPDATE:
			main_trash_update(env);
			break;
		case IMSG_END_UPDATE: {
			main_end_update(env);
			break;
		}
		default:
			log_debug("main_dispatch_client: unexpected imsg %d",
			   imsg.hdr.type);
			break;
		}
		imsg_free(&imsg);
	}

done:
	if (!shut)
		imsg_event_add(iev);
	else {
		log_debug("king bula sez: ran into dead pipe");
		event_del(&iev->ev);
		event_loopexit(NULL);
	}
}

void
main_configure_client(struct env *env)
{
	struct idm	*idm;
	struct imsgev	*iev = env->sc_iev;

	imsg_compose_event(iev, IMSG_CONF_START, 0, 0, -1, env, sizeof(*env));
	TAILQ_FOREACH(idm, &env->sc_idms, idm_entry) {
		imsg_compose_event(iev, IMSG_CONF_IDM, 0, 0, -1,
		    idm, sizeof(*idm));
	}
	imsg_compose_event(iev, IMSG_CONF_END, 0, 0, -1, NULL, 0);
}

void
main_init_timer(int fd, short event, void *p)
{
	struct env	*env = p;

	main_configure_client(env);
}

void
purge_config(struct env *env)
{
	struct idm	*idm;

	while ((idm = TAILQ_FIRST(&env->sc_idms)) != NULL) {
		TAILQ_REMOVE(&env->sc_idms, idm, idm_entry);
		free(idm);
	}
}

int
main(int argc, char *argv[])
{
	int		 c;
	int		 debug;
	struct passwd	*pw;
	struct env	 env;
	struct event	 ev_sigint;
	struct event	 ev_sigterm;
	struct event	 ev_sigchld;
	struct event	 ev_sighup;
	struct event	 ev_timer;
	struct timeval	 tv;

	debug = 0;
	ypldap_process = PROC_MAIN;

	log_init(1);

	while ((c = getopt(argc, argv, "dD:nf:v")) != -1) {
		switch (c) {
		case 'd':
			debug = 2;
			break;
		case 'D':
			if (cmdline_symset(optarg) < 0)
				log_warnx("could not parse macro definition %s",
				    optarg);
			break;
		case 'n':
			debug = 2;
			opts |= YPLDAP_OPT_NOACTION;
			break;
		case 'f':
			conffile = optarg;
			break;
		case 'v':
			opts |= YPLDAP_OPT_VERBOSE;
			break;
		default:
			usage();
		}
	}

	argc -= optind;
	argv += optind;

	if (argc)
		usage();

	RB_INIT(&env.sc_user_uids);
	RB_INIT(&env.sc_group_gids);

	if (parse_config(&env, conffile, opts))
		exit(1);
	if (opts & YPLDAP_OPT_NOACTION) {
		fprintf(stderr, "configuration OK\n");
		exit(0);
	}

	if (geteuid())
		errx(1, "need root privileges");

	log_init(debug);

	if (!debug) {
		if (daemon(1, 0) == -1)
			err(1, "failed to daemonize");
	}

	log_info("startup%s", (debug > 1)?" [debug mode]":"");

	if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, PF_UNSPEC,
	    pipe_main2client) == -1)
		fatal("socketpair");

	client_pid = ldapclient(pipe_main2client);

	setproctitle("parent");
	event_init();

	signal_set(&ev_sigint, SIGINT, main_sig_handler, &env);
	signal_set(&ev_sigterm, SIGTERM, main_sig_handler, &env);
	signal_set(&ev_sighup, SIGHUP, main_sig_handler, &env);
	signal_set(&ev_sigchld, SIGCHLD, main_sig_handler, &env);
	signal_add(&ev_sigint, NULL);
	signal_add(&ev_sigterm, NULL);
	signal_add(&ev_sighup, NULL);
	signal_add(&ev_sigchld, NULL);

	close(pipe_main2client[1]);
	if ((env.sc_iev = calloc(1, sizeof(*env.sc_iev))) == NULL)
		fatal(NULL);
	imsg_init(&env.sc_iev->ibuf, pipe_main2client[0]);
	env.sc_iev->handler = main_dispatch_client;

	env.sc_iev->events = EV_READ;
	env.sc_iev->data = &env;
	event_set(&env.sc_iev->ev, env.sc_iev->ibuf.fd, env.sc_iev->events,
	     env.sc_iev->handler, &env);
	event_add(&env.sc_iev->ev, NULL);

	yp_init(&env);

	if ((pw = getpwnam(YPLDAP_USER)) == NULL)
		fatal("getpwnam");

#ifndef DEBUG
	if (setgroups(1, &pw->pw_gid) ||
	    setresgid(pw->pw_gid, pw->pw_gid, pw->pw_gid) ||
	    setresuid(pw->pw_uid, pw->pw_uid, pw->pw_uid))
		fatal("cannot drop privileges");
#else
#warning disabling privilege revocation in debug mode
#endif

	memset(&tv, 0, sizeof(tv));
	evtimer_set(&ev_timer, main_init_timer, &env);
	evtimer_add(&ev_timer, &tv);

	yp_enable_events();
	event_dispatch();
	main_shutdown();

	return (0);
}

void
imsg_event_add(struct imsgev *iev)
{
	if (iev->handler == NULL) {
		imsg_flush(&iev->ibuf);
		return;
	}

	iev->events = EV_READ;
	if (iev->ibuf.w.queued)
		iev->events |= EV_WRITE;

	event_del(&iev->ev);
	event_set(&iev->ev, iev->ibuf.fd, iev->events, iev->handler, iev->data);
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
