/* Copyright 1988,1990,1993,1994 by Paul Vixie
 * All rights reserved
 *
 * Distribute freely, except: don't remove my name from the source or
 * documentation (don't take credit for my work), mark your changes (don't
 * get me blamed for your possible bugs), don't alter or remove this
 * notice.  May be sold if buildable source is provided to buyer.  No
 * warrantee of any kind, express or implied, is included with this
 * software; use at your own risk, responsibility for damages (if any) to
 * anyone resulting from the use of this software rests entirely with the
 * user.
 *
 * Send bug reports, bug fixes, enhancements, requests, flames, etc., and
 * I'll try to keep a version up to date.  I can be reached as follows:
 * Paul Vixie          <paul@vix.com>          uunet!decwrl!vixie!paul
 */

#if !defined(lint) && !defined(LINT)
static const char rcsid[] =
  "$FreeBSD$";
#endif


#include "cron.h"
#include <sys/signal.h>
#if defined(sequent)
# include <sys/universe.h>
#endif
#if defined(SYSLOG)
# include <syslog.h>
#endif
#if defined(LOGIN_CAP)
# include <login_cap.h>
#endif
#ifdef PAM
# include <security/pam_appl.h>
# include <security/openpam.h>
#endif


static void		child_process(entry *, user *),
			do_univ(user *);


void
do_command(e, u)
	entry	*e;
	user	*u;
{
	pid_t pid;

	Debug(DPROC, ("[%d] do_command(%s, (%s,%d,%d))\n",
		getpid(), e->cmd, u->name, e->uid, e->gid))

	/* fork to become asynchronous -- parent process is done immediately,
	 * and continues to run the normal cron code, which means return to
	 * tick().  the child and grandchild don't leave this function, alive.
	 *
	 * vfork() is unsuitable, since we have much to do, and the parent
	 * needs to be able to run off and fork other processes.
	 */
	switch ((pid = fork())) {
	case -1:
		log_it("CRON",getpid(),"error","can't fork");
		if (e->flags & INTERVAL)
			e->lastexit = time(NULL);
		break;
	case 0:
		/* child process */
		pidfile_close(pfh);
		child_process(e, u);
		Debug(DPROC, ("[%d] child process done, exiting\n", getpid()))
		_exit(OK_EXIT);
		break;
	default:
		/* parent process */
		Debug(DPROC, ("[%d] main process forked child #%d, "
		    "returning to work\n", getpid(), pid))
		if (e->flags & INTERVAL) {
			e->lastexit = 0;
			e->child = pid;
		}
		break;
	}
	Debug(DPROC, ("[%d] main process returning to work\n", getpid()))
}


static void
child_process(e, u)
	entry	*e;
	user	*u;
{
	int		stdin_pipe[2], stdout_pipe[2];
	register char	*input_data;
	char		*usernm, *mailto;
	int		children = 0;
# if defined(LOGIN_CAP)
	struct passwd	*pwd;
	login_cap_t *lc;
# endif

	Debug(DPROC, ("[%d] child_process('%s')\n", getpid(), e->cmd))

	/* mark ourselves as different to PS command watchers by upshifting
	 * our program name.  This has no effect on some kernels.
	 */
	setproctitle("running job");

	/* discover some useful and important environment settings
	 */
	usernm = env_get("LOGNAME", e->envp);
	mailto = env_get("MAILTO", e->envp);

#ifdef PAM
	/* use PAM to see if the user's account is available,
	 * i.e., not locked or expired or whatever.  skip this
	 * for system tasks from /etc/crontab -- they can run
	 * as any user.
	 */
	if (strcmp(u->name, SYS_NAME)) {	/* not equal */
		pam_handle_t *pamh = NULL;
		int pam_err;
		struct pam_conv pamc = {
			.conv = openpam_nullconv,
			.appdata_ptr = NULL
		};

		Debug(DPROC, ("[%d] checking account with PAM\n", getpid()))

		/* u->name keeps crontab owner name while LOGNAME is the name
		 * of user to run command on behalf of.  they should be the
		 * same for a task from a per-user crontab.
		 */
		if (strcmp(u->name, usernm)) {
			log_it(usernm, getpid(), "username ambiguity", u->name);
			exit(ERROR_EXIT);
		}

		pam_err = pam_start("cron", usernm, &pamc, &pamh);
		if (pam_err != PAM_SUCCESS) {
			log_it("CRON", getpid(), "error", "can't start PAM");
			exit(ERROR_EXIT);
		}

		pam_err = pam_acct_mgmt(pamh, PAM_SILENT);
		/* Expired password shouldn't prevent the job from running. */
		if (pam_err != PAM_SUCCESS && pam_err != PAM_NEW_AUTHTOK_REQD) {
			log_it(usernm, getpid(), "USER", "account unavailable");
			exit(ERROR_EXIT);
		}

		pam_end(pamh, pam_err);
	}
#endif

#ifdef USE_SIGCHLD
	/* our parent is watching for our death by catching SIGCHLD.  we
	 * do not care to watch for our children's deaths this way -- we
	 * use wait() explicitly.  so we have to disable the signal (which
	 * was inherited from the parent).
	 */
	(void) signal(SIGCHLD, SIG_DFL);
#else
	/* on system-V systems, we are ignoring SIGCLD.  we have to stop
	 * ignoring it now or the wait() in cron_pclose() won't work.
	 * because of this, we have to wait() for our children here, as well.
	 */
	(void) signal(SIGCLD, SIG_DFL);
#endif /*BSD*/

	/* create some pipes to talk to our future child
	 */
	if (pipe(stdin_pipe) != 0 || pipe(stdout_pipe) != 0) {
		log_it("CRON", getpid(), "error", "can't pipe");
		exit(ERROR_EXIT);
	}

	/* since we are a forked process, we can diddle the command string
	 * we were passed -- nobody else is going to use it again, right?
	 *
	 * if a % is present in the command, previous characters are the
	 * command, and subsequent characters are the additional input to
	 * the command.  Subsequent %'s will be transformed into newlines,
	 * but that happens later.
	 *
	 * If there are escaped %'s, remove the escape character.
	 */
	/*local*/{
		register int escaped = FALSE;
		register int ch;
		register char *p;

		for (input_data = p = e->cmd; (ch = *input_data);
		     input_data++, p++) {
			if (p != input_data)
			    *p = ch;
			if (escaped) {
				if (ch == '%' || ch == '\\')
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
	switch (vfork()) {
	case -1:
		log_it("CRON",getpid(),"error","can't vfork");
		exit(ERROR_EXIT);
		/*NOTREACHED*/
	case 0:
		Debug(DPROC, ("[%d] grandchild process Vfork()'ed\n",
			      getpid()))

		if (e->uid == ROOT_UID)
			Jitter = RootJitter;
		if (Jitter != 0) {
			srandom(getpid());
			sleep(random() % Jitter);
		}

		/* write a log message.  we've waited this long to do it
		 * because it was not until now that we knew the PID that
		 * the actual user command shell was going to get and the
		 * PID is part of the log message.
		 */
		/*local*/{
			char *x = mkprints((u_char *)e->cmd, strlen(e->cmd));

			log_it(usernm, getpid(), "CMD", x);
			free(x);
		}

		/* that's the last thing we'll log.  close the log files.
		 */
#ifdef SYSLOG
		closelog();
#endif

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
		close(STDIN);	dup2(stdin_pipe[READ_PIPE], STDIN);
		close(STDOUT);	dup2(stdout_pipe[WRITE_PIPE], STDOUT);
		close(STDERR);	dup2(STDOUT, STDERR);

		/* close the pipes we just dup'ed.  The resources will remain.
		 */
		close(stdin_pipe[READ_PIPE]);
		close(stdout_pipe[WRITE_PIPE]);

		/* set our login universe.  Do this in the grandchild
		 * so that the child can invoke /usr/lib/sendmail
		 * without surprises.
		 */
		do_univ(u);

# if defined(LOGIN_CAP)
		/* Set user's entire context, but skip the environment
		 * as cron provides a separate interface for this
		 */
		if ((pwd = getpwnam(usernm)) == NULL)
			pwd = getpwuid(e->uid);
		lc = NULL;
		if (pwd != NULL) {
			pwd->pw_gid = e->gid;
			if (e->class != NULL)
				lc = login_getclass(e->class);
		}
		if (pwd &&
		    setusercontext(lc, pwd, e->uid,
			    LOGIN_SETALL & ~(LOGIN_SETPATH|LOGIN_SETENV)) == 0)
			(void) endpwent();
		else {
			/* fall back to the old method */
			(void) endpwent();
# endif
			/* set our directory, uid and gid.  Set gid first,
			 * since once we set uid, we've lost root privileges.
			 */
			if (setgid(e->gid) != 0) {
				log_it(usernm, getpid(),
				    "error", "setgid failed");
				exit(ERROR_EXIT);
			}
# if defined(BSD)
			if (initgroups(usernm, e->gid) != 0) {
				log_it(usernm, getpid(),
				    "error", "initgroups failed");
				exit(ERROR_EXIT);
			}
# endif
			if (setlogin(usernm) != 0) {
				log_it(usernm, getpid(),
				    "error", "setlogin failed");
				exit(ERROR_EXIT);
			}
			if (setuid(e->uid) != 0) {
				log_it(usernm, getpid(),
				    "error", "setuid failed");
				exit(ERROR_EXIT);
			}
			/* we aren't root after this..*/
#if defined(LOGIN_CAP)
		}
		if (lc != NULL)
			login_close(lc);
#endif
		chdir(env_get("HOME", e->envp));

		/* exec the command.
		 */
		{
			char	*shell = env_get("SHELL", e->envp);

# if DEBUGGING
			if (DebugFlags & DTEST) {
				fprintf(stderr,
				"debug DTEST is on, not exec'ing command.\n");
				fprintf(stderr,
				"\tcmd='%s' shell='%s'\n", e->cmd, shell);
				_exit(OK_EXIT);
			}
# endif /*DEBUGGING*/
			execle(shell, shell, "-c", e->cmd, (char *)NULL,
			    e->envp);
			warn("execle: couldn't exec `%s'", shell);
			_exit(ERROR_EXIT);
		}
		break;
	default:
		/* parent process */
		break;
	}

	children++;

	/* middle process, child of original cron, parent of process running
	 * the user's command.
	 */

	Debug(DPROC, ("[%d] child continues, closing pipes\n", getpid()))

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

	if (*input_data && fork() == 0) {
		register FILE	*out = fdopen(stdin_pipe[WRITE_PIPE], "w");
		register int	need_newline = FALSE;
		register int	escaped = FALSE;
		register int	ch;

		if (out == NULL) {
			warn("fdopen failed in child2");
			_exit(ERROR_EXIT);
		}

		Debug(DPROC, ("[%d] child2 sending data to grandchild\n", getpid()))

		/* close the pipe we don't use, since we inherited it and
		 * are part of its reference count now.
		 */
		close(stdout_pipe[READ_PIPE]);

		/* translation:
		 *	\% -> %
		 *	%  -> \n
		 *	\x -> \x	for all x != %
		 */
		while ((ch = *input_data++)) {
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

		Debug(DPROC, ("[%d] child2 done sending to grandchild\n", getpid()))
		exit(0);
	}

	/* close the pipe to the grandkiddie's stdin, since its wicked uncle
	 * ernie back there has it open and will close it when he's done.
	 */
	close(stdin_pipe[WRITE_PIPE]);

	children++;

	/*
	 * read output from the grandchild.  it's stderr has been redirected to
	 * it's stdout, which has been redirected to our pipe.  if there is any
	 * output, we'll be mailing it to the user whose crontab this is...
	 * when the grandchild exits, we'll get EOF.
	 */

	Debug(DPROC, ("[%d] child reading output from grandchild\n", getpid()))

	/*local*/{
		register FILE	*in = fdopen(stdout_pipe[READ_PIPE], "r");
		register int	ch;

		if (in == NULL) {
			warn("fdopen failed in child");
			_exit(ERROR_EXIT);
		}

		ch = getc(in);
		if (ch != EOF) {
			register FILE	*mail;
			register int	bytes = 1;
			int		status = 0;

			Debug(DPROC|DEXT,
				("[%d] got data (%x:%c) from grandchild\n",
					getpid(), ch, ch))

			/* get name of recipient.  this is MAILTO if set to a
			 * valid local username; USER otherwise.
			 */
			if (mailto == NULL) {
				/* MAILTO not present, set to USER,
				 * unless globally overriden.
				 */
				if (defmailto)
					mailto = defmailto;
				else
					mailto = usernm;
			}
			if (mailto && *mailto == '\0')
				mailto = NULL;

			/* if we are supposed to be mailing, MAILTO will
			 * be non-NULL.  only in this case should we set
			 * up the mail command and subjects and stuff...
			 */

			if (mailto) {
				register char	**env;
				auto char	mailcmd[MAX_COMMAND];
				auto char	hostname[MAXHOSTNAMELEN];

				if (gethostname(hostname, MAXHOSTNAMELEN) == -1)
					hostname[0] = '\0';
				hostname[sizeof(hostname) - 1] = '\0';
				(void) snprintf(mailcmd, sizeof(mailcmd),
					       MAILARGS, MAILCMD);
				if (!(mail = cron_popen(mailcmd, "w", e))) {
					warn("%s", MAILCMD);
					(void) _exit(ERROR_EXIT);
				}
				fprintf(mail, "From: Cron Daemon <%s@%s>\n",
					usernm, hostname);
				fprintf(mail, "To: %s\n", mailto);
				fprintf(mail, "Subject: Cron <%s@%s> %s\n",
					usernm, first_word(hostname, "."),
					e->cmd);
# if defined(MAIL_DATE)
				fprintf(mail, "Date: %s\n",
					arpadate(&TargetTime));
# endif /* MAIL_DATE */
				for (env = e->envp;  *env;  env++)
					fprintf(mail, "X-Cron-Env: <%s>\n",
						*env);
				fprintf(mail, "\n");

				/* this was the first char from the pipe
				 */
				putc(ch, mail);
			}

			/* we have to read the input pipe no matter whether
			 * we mail or not, but obviously we only write to
			 * mail pipe if we ARE mailing.
			 */

			while (EOF != (ch = getc(in))) {
				bytes++;
				if (mailto)
					putc(ch, mail);
			}

			/* only close pipe if we opened it -- i.e., we're
			 * mailing...
			 */

			if (mailto) {
				Debug(DPROC, ("[%d] closing pipe to mail\n",
					getpid()))
				/* Note: the pclose will probably see
				 * the termination of the grandchild
				 * in addition to the mail process, since
				 * it (the grandchild) is likely to exit
				 * after closing its stdout.
				 */
				status = cron_pclose(mail);
			}

			/* if there was output and we could not mail it,
			 * log the facts so the poor user can figure out
			 * what's going on.
			 */
			if (mailto && status) {
				char buf[MAX_TEMPSTR];

				snprintf(buf, sizeof(buf),
			"mailed %d byte%s of output but got status 0x%04x\n",
					bytes, (bytes==1)?"":"s",
					status);
				log_it(usernm, getpid(), "MAIL", buf);
			}

		} /*if data from grandchild*/

		Debug(DPROC, ("[%d] got EOF from grandchild\n", getpid()))

		fclose(in);	/* also closes stdout_pipe[READ_PIPE] */
	}

	/* wait for children to die.
	 */
	for (;  children > 0;  children--)
	{
		WAIT_T		waiter;
		PID_T		pid;

		Debug(DPROC, ("[%d] waiting for grandchild #%d to finish\n",
			getpid(), children))
		pid = wait(&waiter);
		if (pid < OK) {
			Debug(DPROC, ("[%d] no more grandchildren--mail written?\n",
				getpid()))
			break;
		}
		Debug(DPROC, ("[%d] grandchild #%d finished, status=%04x",
			getpid(), pid, WEXITSTATUS(waiter)))
		if (WIFSIGNALED(waiter) && WCOREDUMP(waiter))
			Debug(DPROC, (", dumped core"))
		Debug(DPROC, ("\n"))
	}
}


static void
do_univ(u)
	user	*u;
{
#if defined(sequent)
/* Dynix (Sequent) hack to put the user associated with
 * the passed user structure into the ATT universe if
 * necessary.  We have to dig the gecos info out of
 * the user's password entry to see if the magic
 * "universe(att)" string is present.
 */

	struct	passwd	*p;
	char	*s;
	int	i;

	p = getpwuid(u->uid);
	(void) endpwent();

	if (p == NULL)
		return;

	s = p->pw_gecos;

	for (i = 0; i < 4; i++)
	{
		if ((s = strchr(s, ',')) == NULL)
			return;
		s++;
	}
	if (strcmp(s, "universe(att)"))
		return;

	(void) universe(U_ATT);
#endif
}
