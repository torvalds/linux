/*	$OpenBSD: lpd.c,v 1.3 2022/12/28 21:30:17 jmc Exp $	*/

/*
 * Copyright (c) 2017 Eric Faurot <eric@openbsd.org>
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
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>

#include <errno.h>
#include <getopt.h>
#include <pwd.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include "lpd.h"

#include "log.h"
#include "proc.h"

struct lpd_conf *env;
struct imsgproc *p_control;
struct imsgproc *p_engine;
struct imsgproc *p_frontend;
struct imsgproc *p_priv;

static void priv_dispatch_control(struct imsgproc *, struct imsg *, void *);
static void priv_dispatch_engine(struct imsgproc *, struct imsg *, void *);
static void priv_dispatch_frontend(struct imsgproc *, struct imsg *, void *);
static void priv_dispatch_printer(struct imsgproc *, struct imsg *, void *);
static void priv_open_listener(struct listener *);
static void priv_send_config(void);
static void priv_sighandler(int, short, void *);
static void priv_shutdown(void);
static void priv_run_printer(const char *);

static char **saved_argv;
static int saved_argc;

static void
usage(void)
{
	extern char *__progname;

	fprintf(stderr, "usage: %s [-dnv] [-D macro=value]  [-f file]\n",
	    __progname);
	exit(1);
}

int
main(int argc, char **argv)
{
	struct listener *l;
	struct event evt_sigchld, evt_sigint, evt_sigterm, evt_sighup;
	const char *conffile = LPD_CONFIG, *reexec = NULL;
	int sp[2], ch, debug = 0, nflag = 0, verbose = 1;

	saved_argv = argv;
	saved_argc = argc;

	log_init(1, LOG_LPR);
	log_setverbose(0);

	while ((ch = getopt(argc, argv, "D:df:nvX:")) != -1) {
		switch (ch) {
		case 'D':
			if (cmdline_symset(optarg) < 0)
				log_warnx("could not parse macro definition %s",
				    optarg);
			break;
		case 'd':
			debug = 1;
			break;
		case 'f':
			conffile = optarg;
			break;
		case 'n':
			nflag = 1;
			break;
		case 'v':
			verbose++;
			break;
		case 'X':
			reexec = optarg;
			break;
		default:
			usage();
		}
	}

	argv += optind;
	argc -= optind;

	if (argc || *argv)
		usage();

	if (reexec) {
		if (!strcmp(reexec, "control"))
			control(debug, verbose);
		if (!strcmp(reexec, "engine"))
			engine(debug, verbose);
		if (!strcmp(reexec, "frontend"))
			frontend(debug, verbose);
		if (!strncmp(reexec, "printer:", 8))
			printer(debug, verbose, strchr(reexec, ':') + 1);
		fatalx("unknown process %s", reexec);
	}

	/* Parse config file. */
	env = parse_config(conffile, verbose);
	if (env == NULL)
		exit(1);

	if (nflag) {
		fprintf(stderr, "configuration OK\n");
		exit(0);
	}

	/* Check for root privileges. */
	if (geteuid())
		fatalx("need root privileges");

	/* Check for assigned daemon user. */
	if (getpwnam(LPD_USER) == NULL)
		fatalx("unknown user %s", LPD_USER);

	log_init(debug, LOG_LPR);
	log_setverbose(verbose);
	log_procinit("priv");
	setproctitle("priv");

	if (!debug)
		if (daemon(1, 0) == -1)
			fatal("daemon");

        log_info("startup");

	TAILQ_FOREACH(l, &env->listeners, entry)
		priv_open_listener(l);

	event_init();

	signal_set(&evt_sigint, SIGINT, priv_sighandler, NULL);
	signal_add(&evt_sigint, NULL);
	signal_set(&evt_sigterm, SIGTERM, priv_sighandler, NULL);
	signal_add(&evt_sigterm, NULL);
	signal_set(&evt_sigchld, SIGCHLD, priv_sighandler, NULL);
	signal_add(&evt_sigchld, NULL);
	signal_set(&evt_sighup, SIGHUP, priv_sighandler, NULL);
	signal_add(&evt_sighup, NULL);
	signal(SIGPIPE, SIG_IGN);

	/* Fork and exec unprivileged processes. */
	argv = calloc(saved_argc + 3, sizeof(*argv));
	if (argv == NULL)
		fatal("calloc");
	for (argc = 0; argc < saved_argc; argc++)
		argv[argc] = saved_argv[argc];
	argv[argc++] = "-X";
	argv[argc++] = "";
	argv[argc++] = NULL;

	argv[argc - 2] = "control";
	p_control = proc_exec(PROC_CONTROL, argv);
	if (p_control == NULL)
		fatalx("cannot exec control process");
	proc_setcallback(p_control, priv_dispatch_control, NULL);
	proc_enable(p_control);

	argv[argc - 2] = "engine";
	p_engine = proc_exec(PROC_ENGINE, argv);
	if (p_engine == NULL)
		fatalx("cannot exec engine process");
	proc_setcallback(p_engine, priv_dispatch_engine, NULL);
	proc_enable(p_engine);

	argv[argc - 2] = "frontend";
	p_frontend = proc_exec(PROC_FRONTEND, argv);
	if (p_frontend == NULL)
		fatalx("cannot exec frontend process");
	proc_setcallback(p_frontend, priv_dispatch_frontend, NULL);
	proc_enable(p_frontend);

	/* Connect processes. */
	if (socketpair(AF_UNIX, SOCK_STREAM|SOCK_NONBLOCK, PF_UNSPEC, sp) == -1)
		fatal("socketpair");
	m_compose(p_engine, IMSG_SOCK_FRONTEND, 0, 0, sp[1], NULL, 0);
	m_compose(p_frontend, IMSG_SOCK_ENGINE, 0, 0, sp[0], NULL, 0);

	priv_send_config();

	if (pledge("stdio sendfd proc exec", NULL) == -1)
		fatal("pledge");

	event_dispatch();

	priv_shutdown();

	return (0);
}

static void
priv_sighandler(int sig, short ev, void *arg)
{
	pid_t pid;
	int status;

	switch (sig) {
	case SIGTERM:
	case SIGINT:
		event_loopbreak();
		break;
	case SIGCHLD:
		do {
			pid = waitpid(-1, &status, WNOHANG);
			if (pid <= 0)
				continue;
			if (WIFSIGNALED(status))
				log_warnx("process %d terminated by signal %d",
				    (int)pid, WTERMSIG(status));
			else if (WIFEXITED(status) && WEXITSTATUS(status))
				log_warnx("process %d exited with status %d",
				    (int)pid, WEXITSTATUS(status));
			else if (WIFEXITED(status))
				log_debug("process %d exited normally",
				    (int)pid);
			else
				/* WIFSTOPPED or WIFCONTINUED */
				continue;
		} while (pid > 0 || (pid == -1 && errno == EINTR));
		break;
	default:
		fatalx("signal %d", sig);
	}
}

static void
priv_shutdown(void)
{
	pid_t pid;

	proc_free(p_control);
	proc_free(p_engine);
	proc_free(p_frontend);

	do {
		pid = waitpid(WAIT_MYPGRP, NULL, 0);
	} while (pid != -1 || (pid == -1 && errno == EINTR));

	log_info("exiting");

	exit(0);
}

static void
priv_open_listener(struct listener *l)
{
	struct sockaddr_un *su;
	struct sockaddr *sa;
	const char *path;
	mode_t old_umask;
	int opt, sock, r;

	sa = (struct sockaddr *)&l->ss;

	sock = socket(sa->sa_family, SOCK_STREAM | SOCK_NONBLOCK, 0);
	if (sock == -1) {
		if (errno == EAFNOSUPPORT) {
			log_warn("%s: socket", __func__);
			return;
		}
		fatal("%s: socket", __func__);
	}

	switch (sa->sa_family) {
	case AF_LOCAL:
		su = (struct sockaddr_un *)sa;
		path = su->sun_path;
		if (connect(sock, sa, sa->sa_len) == 0)
			fatalx("%s already in use", path);

		if (unlink(path) == -1)
			if (errno != ENOENT)
				fatal("unlink: %s", path);

		old_umask = umask(S_IXUSR|S_IXGRP|S_IWOTH|S_IROTH|S_IXOTH);
		r = bind(sock, sa, sizeof(*su));
		(void)umask(old_umask);

		if (r == -1)
			fatal("bind: %s", path);
		break;

	case AF_INET:
	case AF_INET6:
		opt = 1;
		if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt,
		    sizeof(opt)) == -1)
			fatal("setsockopt: %s", log_fmt_sockaddr(sa));

		if (bind(sock, sa, sa->sa_len) == -1)
			fatal("bind: %s", log_fmt_sockaddr(sa));
		break;

	default:
		fatalx("bad address family %d", sa->sa_family);
	}

	l->sock = sock;
}

static void
priv_send_config(void)
{
	struct listener *l;

	m_compose(p_control, IMSG_CONF_START, 0, 0, -1, NULL, 0);
	m_compose(p_control, IMSG_CONF_END, 0, 0, -1, NULL, 0);

	m_compose(p_engine, IMSG_CONF_START, 0, 0, -1, NULL, 0);
	m_compose(p_engine, IMSG_CONF_END, 0, 0, -1, NULL, 0);

	m_compose(p_frontend, IMSG_CONF_START, 0, 0, -1, NULL, 0);
	TAILQ_FOREACH(l, &env->listeners, entry) {
		m_create(p_frontend, IMSG_CONF_LISTENER, 0, 0, l->sock);
		m_add_int(p_frontend, l->proto);
		m_add_sockaddr(p_frontend, (struct sockaddr *)(&l->ss));
		m_close(p_frontend);
	}
	m_compose(p_frontend, IMSG_CONF_END, 0, 0, -1, NULL, 0);
}

static void
priv_dispatch_control(struct imsgproc *proc, struct imsg *imsg, void *arg)
{
	if (imsg == NULL)
		fatalx("%s: imsg connection lost", __func__);

	if (log_getverbose() > LOGLEVEL_IMSG)
		log_imsg(proc, imsg);

	switch (imsg->hdr.type) {
	default:
		fatalx("%s: unexpected imsg %s", __func__,
		    log_fmt_imsgtype(imsg->hdr.type));
	}
}

static void
priv_dispatch_engine(struct imsgproc *proc, struct imsg *imsg, void *arg)
{
	const char *prn;

	if (imsg == NULL)
		fatalx("%s: imsg connection lost", __func__);

	if (log_getverbose() > LOGLEVEL_IMSG)
		log_imsg(proc, imsg);

	switch (imsg->hdr.type) {
	case IMSG_LPR_PRINTJOB:
		m_get_string(proc, &prn);
		m_end(proc);
		priv_run_printer(prn);
		break;
	default:
		fatalx("%s: unexpected imsg %s", __func__,
		    log_fmt_imsgtype(imsg->hdr.type));
	}
}

static void
priv_dispatch_frontend(struct imsgproc *proc, struct imsg *imsg, void *arg)
{
	if (imsg == NULL)
		fatalx("%s: imsg connection lost", __func__);

	if (log_getverbose() > LOGLEVEL_IMSG)
		log_imsg(proc, imsg);

	switch (imsg->hdr.type) {
	default:
		fatalx("%s: unexpected imsg %s", __func__,
		    log_fmt_imsgtype(imsg->hdr.type));
	}
}

static void
priv_dispatch_printer(struct imsgproc *proc, struct imsg *imsg, void *arg)
{
	if (imsg == NULL) {
		log_debug("printer process ended, pid=%d, printer=%s",
		     proc_getpid(proc), proc_gettitle(proc));
		proc_free(proc);
		return;
	}

	if (log_getverbose() > LOGLEVEL_IMSG)
		log_imsg(proc, imsg);

	switch (imsg->hdr.type) {
	default:
		fatalx("%s: unexpected imsg %s", __func__,
		    log_fmt_imsgtype(imsg->hdr.type));
	}
}

static void
priv_run_printer(const char *prn)
{
	struct imsgproc *p;
	char **argv, *buf;
	int argc;

	if (asprintf(&buf, "printer:%s", prn) == -1) {
		log_warn("%s: asprintf", __func__);
		return;
	}

	argv = calloc(saved_argc + 4, sizeof(*argv));
	if (argv == NULL) {
		log_warn("%s: calloc", __func__);
		free(buf);
		return;
	}
	for (argc = 0; argc < saved_argc; argc++)
		argv[argc] = saved_argv[argc];
	argv[argc++] = "-X";
	argv[argc++] = buf;
	argv[argc++] = NULL;

	p = proc_exec(PROC_PRINTER, argv);
	if (p == NULL)
		log_warnx("%s: cannot exec printer process", __func__);
	else {
		proc_settitle(p, prn);
		proc_setcallback(p, priv_dispatch_printer, p);
		proc_enable(p);
	}

	free(argv);
	free(buf);
}
