/*	$OpenBSD: do_command.c,v 1.63 2022/05/21 01:21:29 deraadt Exp $	*/

/* Copyright 1988,1990,1993,1994 by Paul Vixie
 * Copyright (c) 2004 by Internet Systems Consortium, Inc. ("ISC")
 * Copyright (c) 1997,2000 by Internet Software Consortium, Inc.
 * Copyright (c) 2018 Job Snijders <job@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND ISC DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS.  IN NO EVENT SHALL ISC BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT
 * OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>
#include <sys/wait.h>

#include <bitstring.h>		/* for structs.h */
#include <bsd_auth.h>
#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <login_cap.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <time.h>		/* for structs.h */
#include <unistd.h>
#include <vis.h>

#include "config.h"
#include "pathnames.h"
#include "macros.h"
#include "structs.h"
#include "funcs.h"

static void		child_process(entry *, user *);

pid_t
do_command(entry *e, user *u)
{
	pid_t pid;

	/* fork to become asynchronous -- parent process is done immediately,
	 * and continues to run the normal cron code, which means return to
	 * tick().  the child and grandchild don't leave this function, alive.
	 *
	 * vfork() is unsuitable, since we have much to do, and the parent
	 * needs to be able to run off and fork other processes.
	 */
	switch ((pid = fork())) {
	case -1:
		syslog(LOG_ERR, "(CRON) CAN'T FORK (%m)");
		break;
	case 0:
		/* child process */
		child_process(e, u);
		_exit(EXIT_SUCCESS);
		break;
	default:
		/* parent process */
		if ((e->flags & SINGLE_JOB) == 0)
			pid = -1;
		break;
	}

	/* only return pid if a singleton */
	return (pid);
}

static void
child_process(entry *e, user *u)
{
	FILE *in;
	int stdin_pipe[2], stdout_pipe[2];
	char **p, *input_data, *usernm;
	auth_session_t *as;
	login_cap_t *lc;
	extern char **environ;

	/* mark ourselves as different to PS command watchers */
	setproctitle("running job");

	/* close sockets from parent (i.e. cronSock) */
	closefrom(3);

	/* discover some useful and important environment settings
	 */
	usernm = e->pwd->pw_name;

	/* our parent is watching for our death by catching SIGCHLD.  we
	 * do not care to watch for our children's deaths this way -- we
	 * use wait() explicitly.  so we have to reset the signal (which
	 * was inherited from the parent).
	 */
	(void) signal(SIGCHLD, SIG_DFL);

	/* create some pipes to talk to our future child
	 */
	if (pipe(stdin_pipe) != 0 || pipe(stdout_pipe) != 0) {
		syslog(LOG_ERR, "(CRON) PIPE (%m)");
		_exit(EXIT_FAILURE);
	}

	/* since we are a forked process, we can diddle the command string
	 * we were passed -- nobody else is going to use it again, right?
	 *
	 * if a % is present in the command, previous characters are the
	 * command, and subsequent characters are the additional input to
	 * the command.  An escaped % will have the escape character stripped
	 * from it.  Subsequent %'s will be transformed into newlines,
	 * but that happens later.
	 */
	/*local*/{
		int escaped = FALSE;
		int ch;
		char *p;

		for (input_data = p = e->cmd;
		    (ch = *input_data) != '\0';
		    input_data++, p++) {
			if (p != input_data)
				*p = ch;
			if (escaped) {
				if (ch == '%')
					*--p = ch;
				escaped = FALSE;
				continue;
			}
			if (ch == '\\') {
				escaped = TRUE;
				continue;
			}
			if (ch == '%') {
				*input_data++ = '\0';
				break;
			}
		}
		*p = '\0';
	}

	/* fork again, this time so we can exec the user's command.
	 */

	pid_t	jobpid;
	switch (jobpid = fork()) {
	case -1:
		syslog(LOG_ERR, "(CRON) CAN'T FORK (%m)");
		_exit(EXIT_FAILURE);
		/*NOTREACHED*/
	case 0:
		/* write a log message.  we've waited this long to do it
		 * because it was not until now that we knew the PID that
		 * the actual user command shell was going to get and the
		 * PID is part of the log message.
		 */
		if ((e->flags & DONT_LOG) == 0) {
			char *x;
			if (stravis(&x, e->cmd, 0) != -1) {
				syslog(LOG_INFO, "(%s) CMD (%s)", usernm, x);
				free(x);
			}
		}

		/* get new pgrp, void tty, etc.
		 */
		(void) setsid();

		/* close the pipe ends that we won't use.  this doesn't affect
		 * the parent, who has to read and write them; it keeps the
		 * kernel from recording us as a potential client TWICE --
		 * which would keep it from sending SIGPIPE in otherwise
		 * appropriate circumstances.
		 */
		close(stdin_pipe[WRITE_PIPE]);
		close(stdout_pipe[READ_PIPE]);

		/* grandchild process.  make std{in,out} be the ends of
		 * pipes opened by our daddy; make stderr go to stdout.
		 */
		if (stdin_pipe[READ_PIPE] != STDIN_FILENO) {
			dup2(stdin_pipe[READ_PIPE], STDIN_FILENO);
			close(stdin_pipe[READ_PIPE]);
		}
		if (stdout_pipe[WRITE_PIPE] != STDOUT_FILENO) {
			dup2(stdout_pipe[WRITE_PIPE], STDOUT_FILENO);
			close(stdout_pipe[WRITE_PIPE]);
		}
		dup2(STDOUT_FILENO, STDERR_FILENO);

		/*
		 * From this point on, anything written to stderr will be
		 * mailed to the user as output.
		 */

		/* XXX - should just pass in a login_cap_t * */
		if ((lc = login_getclass(e->pwd->pw_class)) == NULL) {
			warnx("unable to get login class for %s",
			    e->pwd->pw_name);
			syslog(LOG_ERR, "(CRON) CAN'T GET LOGIN CLASS (%s)",
			    e->pwd->pw_name);
			_exit(EXIT_FAILURE);
		}
		if (setusercontext(lc, e->pwd, e->pwd->pw_uid, LOGIN_SETALL) == -1) {
			warn("setusercontext failed for %s", e->pwd->pw_name);
			syslog(LOG_ERR, "(%s) SETUSERCONTEXT FAILED (%m)",
			    e->pwd->pw_name);
			_exit(EXIT_FAILURE);
		}
		as = auth_open();
		if (as == NULL || auth_setpwd(as, e->pwd) != 0) {
			warn("auth_setpwd");
			syslog(LOG_ERR, "(%s) AUTH_SETPWD FAILED (%m)",
			    e->pwd->pw_name);
			_exit(EXIT_FAILURE);
		}
		if (auth_approval(as, lc, usernm, "cron") <= 0) {
			warnx("approval failed for %s", e->pwd->pw_name);
			syslog(LOG_ERR, "(%s) APPROVAL FAILED (cron)",
			    e->pwd->pw_name);
			_exit(EXIT_FAILURE);
		}
		auth_close(as);
		login_close(lc);

		/* If no PATH specified in crontab file but
		 * we just added one via login.conf, add it to
		 * the crontab environment.
		 */
		if (env_get("PATH", e->envp) == NULL && environ != NULL) {
			for (p = environ; *p; p++) {
				if (strncmp(*p, "PATH=", 5) == 0) {
					e->envp = env_set(e->envp, *p);
					break;
				}
			}
		}
		chdir(env_get("HOME", e->envp));

		(void) signal(SIGPIPE, SIG_DFL);

		/*
		 * Exec the command.
		 */
		{
			char	*shell = env_get("SHELL", e->envp);

			execle(shell, shell, "-c", e->cmd, (char *)NULL, e->envp);
			warn("unable to execute %s", shell);
			syslog(LOG_ERR, "(%s) CAN'T EXEC (%s: %m)",
			    e->pwd->pw_name, shell);
			_exit(EXIT_FAILURE);
		}
		break;
	default:
		/* parent process */
		break;
	}

	/* middle process, child of original cron, parent of process running
	 * the user's command.
	 */

	/* close the ends of the pipe that will only be referenced in the
	 * grandchild process...
	 */
	close(stdin_pipe[READ_PIPE]);
	close(stdout_pipe[WRITE_PIPE]);

	/*
	 * write, to the pipe connected to child's stdin, any input specified
	 * after a % in the crontab entry.  while we copy, convert any
	 * additional %'s to newlines.  when done, if some characters were
	 * written and the last one wasn't a newline, write a newline.
	 *
	 * Note that if the input data won't fit into one pipe buffer (2K
	 * or 4K on most BSD systems), and the child doesn't read its stdin,
	 * we would block here.  thus we must fork again.
	 */

	pid_t	stdinjob;
	if (*input_data && (stdinjob = fork()) == 0) {
		FILE *out = fdopen(stdin_pipe[WRITE_PIPE], "w");
		int need_newline = FALSE;
		int escaped = FALSE;
		int ch;

		/* close the pipe we don't use, since we inherited it and
		 * are part of its reference count now.
		 */
		close(stdout_pipe[READ_PIPE]);

		/* translation:
		 *	\% -> %
		 *	%  -> \n
		 *	\x -> \x	for all x != %
		 */
		while ((ch = *input_data++) != '\0') {
			if (escaped) {
				if (ch != '%')
					putc('\\', out);
			} else {
				if (ch == '%')
					ch = '\n';
			}

			if (!(escaped = (ch == '\\'))) {
				putc(ch, out);
				need_newline = (ch != '\n');
			}
		}
		if (escaped)
			putc('\\', out);
		if (need_newline)
			putc('\n', out);

		/* close the pipe, causing an EOF condition.  fclose causes
		 * stdin_pipe[WRITE_PIPE] to be closed, too.
		 */
		fclose(out);

		_exit(EXIT_SUCCESS);
	}

	/* close the pipe to the grandkiddie's stdin, since its wicked uncle
	 * ernie back there has it open and will close it when he's done.
	 */
	close(stdin_pipe[WRITE_PIPE]);

	/*
	 * read output from the grandchild.  Its stderr has been redirected to
	 * its stdout, which has been redirected to our pipe.  if there is any
	 * output, we'll be mailing it to the user whose crontab this is...
	 * when the grandchild exits, we'll get EOF.
	 */

	(void) signal(SIGPIPE, SIG_IGN);
	in = fdopen(stdout_pipe[READ_PIPE], "r");

	char	*mailto;
	FILE	*mail = NULL;
	int	status = 0;
	pid_t	mailpid;
	size_t	bytes = 1;

	if (in != NULL) {
		int	ch = getc(in);

		if (ch != EOF) {

			/* get name of recipient.  this is MAILTO if set to a
			 * valid local username; USER otherwise.
			 */
			mailto = env_get("MAILTO", e->envp);
			if (!mailto) {
				/* MAILTO not present, set to USER.
				 */
				mailto = usernm;
			} else if (!*mailto || !safe_p(usernm, mailto)) {
				mailto = NULL;
			}

			/* if we are supposed to be mailing, MAILTO will
			 * be non-NULL.  only in this case should we set
			 * up the mail command and subjects and stuff...
			 */

			if (mailto) {
				char	**env;
				char	mailcmd[MAX_COMMAND];
				char	hostname[HOST_NAME_MAX + 1];

				gethostname(hostname, sizeof(hostname));
				if (snprintf(mailcmd, sizeof mailcmd,  MAILFMT,
				    MAILARG) >= sizeof mailcmd) {
					syslog(LOG_ERR,
					    "(%s) ERROR (mailcmd too long)",
					    e->pwd->pw_name);
					(void) _exit(EXIT_FAILURE);
				}
				if (!(mail = cron_popen(mailcmd, "w", e->pwd,
				    &mailpid))) {
					syslog(LOG_ERR, "(%s) POPEN (%s)",
					    e->pwd->pw_name, mailcmd);
					(void) _exit(EXIT_FAILURE);
				}
				fprintf(mail, "From: root (Cron Daemon)\n");
				fprintf(mail, "To: %s\n", mailto);
				fprintf(mail, "Subject: Cron <%s@%s> %s\n",
					usernm, first_word(hostname, "."),
					e->cmd);
				fprintf(mail, "Auto-Submitted: auto-generated\n");
				for (env = e->envp;  *env;  env++)
					fprintf(mail, "X-Cron-Env: <%s>\n",
						*env);
				fprintf(mail, "\n");

				/* this was the first char from the pipe
				 */
				fputc(ch, mail);
			}

			/* we have to read the input pipe no matter whether
			 * we mail or not, but obviously we only write to
			 * mail pipe if we ARE mailing.
			 */

			while ((ch = getc(in)) != EOF) {
				bytes++;
				if (mail)
					fputc(ch, mail);
			}

		} /*if data from grandchild*/

		fclose(in);	/* also closes stdout_pipe[READ_PIPE] */
	}

	/* wait for children to die.
	 */
	int waiter;
	if (jobpid > 0) {
		while (waitpid(jobpid, &waiter, 0) == -1 && errno == EINTR)
			;

		/* If everything went well, and -n was set, _and_ we have mail,
		 * we won't be mailing... so shoot the messenger!
		 */
		if (WIFEXITED(waiter) && WEXITSTATUS(waiter) == 0
		    && (e->flags & MAIL_WHEN_ERR) == MAIL_WHEN_ERR
		    && mail) {
			kill(mailpid, SIGKILL);
			(void)fclose(mail);
			mail = NULL;
		}

		/* only close pipe if we opened it -- i.e., we're mailing... */
		if (mail) {
			/*
			 * Note: the pclose will probably see the termination
			 * of the grandchild in addition to the mail process,
			 * since it (the grandchild) is likely to exit after
			 * closing its stdout.
			 */
			status = cron_pclose(mail, mailpid);
		}

		/* if there was output and we could not mail it,
		 * log the facts so the poor user can figure out
		 * what's going on.
		 */
		if (mail && status) {
			syslog(LOG_NOTICE, "(%s) MAIL (mailed %zu byte"
			    "%s of output but got status 0x%04x)", usernm,
			    bytes, (bytes == 1) ? "" : "s", status);
		}
	}

	if (stdinjob > 0)
		while (waitpid(stdinjob, &waiter, 0) == -1 && errno == EINTR)
			;
}

int
safe_p(const char *usernm, const char *s)
{
	static const char safe_delim[] = "@!:%+-.,";	/* conservative! */
	const char *t;
	int ch, first;

	for (t = s, first = 1; (ch = (unsigned char)*t++) != '\0'; first = 0) {
		if (isascii(ch) && isprint(ch) &&
		    (isalnum(ch) || ch == '_' ||
		    (!first && strchr(safe_delim, ch))))
			continue;
		syslog(LOG_WARNING, "(%s) UNSAFE (%s)", usernm, s);
		return (FALSE);
	}
	return (TRUE);
}
