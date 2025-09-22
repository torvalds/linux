/*	$OpenBSD: check_script.c,v 1.22 2021/02/22 01:24:59 jmatthew Exp $	*/

/*
 * Copyright (c) 2007 - 2014 Reyk Floeter <reyk@openbsd.org>
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

#include <sys/wait.h>
#include <sys/time.h>

#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <pwd.h>

#include "relayd.h"

void	 script_sig_alarm(int);

pid_t			 child = -1;

void
check_script(struct relayd *env, struct host *host)
{
	struct ctl_script	 scr;
	struct table		*table;

	if ((host->flags & (F_CHECK_SENT|F_CHECK_DONE)) == F_CHECK_SENT)
		return;

	if ((table = table_find(env, host->conf.tableid)) == NULL)
		fatalx("%s: invalid table id", __func__);

	host->last_up = host->up;
	host->flags &= ~(F_CHECK_SENT|F_CHECK_DONE);

	scr.host = host->conf.id;
	if ((strlcpy(scr.name, host->conf.name,sizeof(scr.name)) >=
	    sizeof(scr.name)) ||
	    (strlcpy(scr.path, table->conf.path, sizeof(scr.path)) >=
	    sizeof(scr.path)))
		fatalx("invalid script path");
	memcpy(&scr.timeout, &table->conf.timeout, sizeof(scr.timeout));

	if (proc_compose(env->sc_ps, PROC_PARENT, IMSG_SCRIPT, &scr,
	    sizeof(scr)) == 0)
		host->flags |= F_CHECK_SENT;
}

void
script_done(struct relayd *env, struct ctl_script *scr)
{
	struct host		*host;

	if ((host = host_find(env, scr->host)) == NULL)
		fatalx("%s: invalid host id", __func__);

	if (scr->retval < 0)
		host->up = HOST_UNKNOWN;
	else if (scr->retval == 0)
		host->up = HOST_DOWN;
	else
		host->up = HOST_UP;
	host->flags |= F_CHECK_DONE;

	hce_notify_done(host, host->up == HOST_UP ?
	    HCE_SCRIPT_OK : HCE_SCRIPT_FAIL);
}

void
script_sig_alarm(int sig)
{
	int save_errno = errno;

	if (child != -1)
		kill(child, SIGKILL);
	errno = save_errno;
}

int
script_exec(struct relayd *env, struct ctl_script *scr)
{
	int			 status = 0, ret = 0;
	sig_t			 save_quit, save_int, save_chld;
	struct itimerval	 it;
	struct timeval		*tv;
	const char		*file, *arg;
	struct passwd		*pw;

	if ((env->sc_conf.flags & F_SCRIPT) == 0) {
		log_warnx("%s: script disabled", __func__);
		return (-1);
	}

	DPRINTF("%s: running script %s, host %s",
	    __func__, scr->path, scr->name);

	arg = scr->name;
	file = scr->path;
	tv = &scr->timeout;

	save_quit = signal(SIGQUIT, SIG_IGN);
	save_int = signal(SIGINT, SIG_IGN);
	save_chld = signal(SIGCHLD, SIG_DFL);

	switch (child = fork()) {
	case -1:
		ret = -1;
		goto done;
	case 0:
		signal(SIGQUIT, SIG_DFL);
		signal(SIGINT, SIG_DFL);
		signal(SIGCHLD, SIG_DFL);

		if ((pw = getpwnam(RELAYD_USER)) == NULL)
			fatal("%s: getpwnam", __func__);
		if (chdir("/") == -1)
			fatal("%s: chdir(\"/\")", __func__);
		if (setgroups(1, &pw->pw_gid) ||
		    setresgid(pw->pw_gid, pw->pw_gid, pw->pw_gid) ||
		    setresuid(pw->pw_uid, pw->pw_uid, pw->pw_uid))
			fatal("%s: can't drop privileges", __func__);

		/*
		 * close fds before executing an external program, to
		 * prevent access to internal fds, eg. IMSG connections
		 * of internal processes.
		 */
		closefrom(STDERR_FILENO + 1);

		execlp(file, file, arg, (char *)NULL);
		_exit(0);
		break;
	default:
		/* Kill the process after a timeout */
		signal(SIGALRM, script_sig_alarm);
		bzero(&it, sizeof(it));
		bcopy(tv, &it.it_value, sizeof(it.it_value));
		setitimer(ITIMER_REAL, &it, NULL);

		waitpid(child, &status, 0);
		break;
	}

	switch (ret) {
	case -1:
		ret = -1;
		break;
	default:
		if (WIFEXITED(status))
			ret = WEXITSTATUS(status);
		else
			ret = 0;
	}

 done:
	/* Disable the process timeout timer */
	bzero(&it, sizeof(it));
	setitimer(ITIMER_REAL, &it, NULL);
	child = -1;

	signal(SIGQUIT, save_quit);
	signal(SIGINT, save_int);
	signal(SIGCHLD, save_chld);
	signal(SIGALRM, SIG_DFL);

	return (ret);
}
