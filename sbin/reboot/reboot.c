/*	$OpenBSD: reboot.c,v 1.38 2017/08/22 00:30:16 sf Exp $	*/
/*	$NetBSD: reboot.c,v 1.8 1995/10/05 05:36:22 mycroft Exp $	*/

/*
 * Copyright (c) 1980, 1986, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/types.h>
#include <sys/reboot.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <machine/cpu.h>
#include <signal.h>
#include <pwd.h>
#include <errno.h>
#include <err.h>
#include <fcntl.h>
#include <termios.h>
#include <syslog.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <paths.h>
#include <util.h>

void	usage(void);
extern char *__progname;

int	dohalt;

#define _PATH_RC	"/etc/rc"

static void
sleep_while_procs(int seconds)
{
	while (seconds > 0) {
		if (kill(-1, 0) == -1 && errno == ESRCH)
			return;
		sleep(1);
		seconds--;
	}
}

int
main(int argc, char *argv[])
{
	unsigned int i;
	struct passwd *pw;
	int ch, howto, lflag, nflag, pflag, qflag;
	char *p, *user;
	sigset_t mask;

	p = __progname;

	/* Nuke login shell */
	if (*p == '-')
		p++;

	howto = dohalt = lflag = nflag = pflag = qflag = 0;
	if (!strcmp(p, "halt")) {
		dohalt = 1;
		howto = RB_HALT;
	}

	while ((ch = getopt(argc, argv, "dlnpq")) != -1)
		switch (ch) {
		case 'd':
			howto |= RB_DUMP;
			break;
		case 'l':	/* Undocumented; used by shutdown. */
			lflag = 1;
			break;
		case 'n':
			nflag = 1;
			howto |= RB_NOSYNC;
			break;
		case 'p':
			/* Only works if we're called as halt. */
			if (dohalt) {
				pflag = 1;
				howto |= RB_POWERDOWN;
			}
			break;
		case 'q':
			qflag = 1;
			break;
		default:
			usage();
		}
	argc -= optind;
	argv += optind;

	if (argc)
		usage();

	if (geteuid())
		errx(1, "%s", strerror(EPERM));

#ifdef CPU_LIDACTION
	if (howto & RB_POWERDOWN) {
		/* Disable suspending on laptop lid close */
		int mib[] = {CTL_MACHDEP, CPU_LIDACTION};
		int lidaction = 0;

		if (sysctl(mib, 2, NULL, NULL, &lidaction,
		    sizeof(lidaction)) == -1 && errno != EOPNOTSUPP)
			warn("sysctl");
	}
#endif /* CPU_LIDACTION */

	if (qflag) {
		reboot(howto);
		err(1, "reboot");
	}

	/* Log the reboot. */
	if (!lflag)  {
		if ((user = getlogin()) == NULL)
			user = (pw = getpwuid(getuid())) ?
			    pw->pw_name : "???";
		if (dohalt) {
			openlog("halt", 0, LOG_AUTH | LOG_CONS);
			if (pflag) {
				syslog(LOG_CRIT,
					"halted (with powerdown) by %s", user);
			} else {
				syslog(LOG_CRIT, "halted by %s", user);
			}
		} else {
			openlog("reboot", 0, LOG_AUTH | LOG_CONS);
			syslog(LOG_CRIT, "rebooted by %s", user);
		}
	}
	logwtmp("~", "shutdown", "");

	/*
	 * Do a sync early on, so disks start transfers while we're off
	 * killing processes.  Don't worry about writes done before the
	 * processes die, the reboot system call syncs the disks.
	 */
	if (!nflag)
		sync();

	/* Just stop init -- if we fail, we'll restart it. */
	if (kill(1, SIGTSTP) == -1)
		err(1, "SIGTSTP init");

	/* Ignore the SIGHUP we get when our parent shell dies. */
	(void)signal(SIGHUP, SIG_IGN);

	/*
	 * If we're running in a pipeline, we don't want to die
	 * after killing whatever we're writing to.
	 */
	(void)signal(SIGPIPE, SIG_IGN);

	if (access(_PATH_RC, R_OK) != -1) {
		pid_t pid;
		struct termios t;
		int fd, status;

		switch ((pid = fork())) {
		case -1:
			break;
		case 0:
			if (revoke(_PATH_CONSOLE) == -1)
				warn("revoke");
			if (setsid() == -1)
				warn("setsid");
			fd = open(_PATH_CONSOLE, O_RDWR);
			if (fd == -1)
				warn("open");
			dup2(fd, 0);
			dup2(fd, 1);
			dup2(fd, 2);
			if (fd > 2)
				close(fd);

			/* At a minimum... */
			tcgetattr(0, &t);
			t.c_oflag |= (ONLCR | OPOST);
			tcsetattr(0, TCSANOW, &t);

			execl(_PATH_BSHELL, "sh", _PATH_RC, "shutdown", (char *)NULL);
			_exit(1);
		default:
			/* rc exits 2 if powerdown=YES in rc.shutdown */
			waitpid(pid, &status, 0);
			if (dohalt && WIFEXITED(status) && WEXITSTATUS(status) == 2)
				howto |= RB_POWERDOWN;
		}
	}

	/*
	 * Point of no return, block all signals so we are sure to
	 * reach the call to reboot(2) unmolested.
	 */
	sigfillset(&mask);
	sigprocmask(SIG_BLOCK, &mask, NULL);

	/* Send a SIGTERM first, a chance to save the buffers. */
	if (kill(-1, SIGTERM) == -1) {
		/*
		 * If ESRCH, everything's OK: we're the only non-system
		 * process!  That can happen e.g. via 'exec reboot' in
		 * single-user mode.
		 */
		if (errno != ESRCH) {
			warn("SIGTERM processes");
			goto restart;
		}
	}

	/*
	 * After the processes receive the signal, start the rest of the
	 * buffers on their way.  Wait 5 seconds between the SIGTERM and
	 * the SIGKILL to give everybody a chance.
	 */
	sleep_while_procs(2);
	if (!nflag)
		sync();
	sleep_while_procs(3);

	for (i = 1;; ++i) {
		if (kill(-1, SIGKILL) == -1) {
			if (errno == ESRCH)
				break;
			goto restart;
		}
		if (i > 5) {
			warnx("WARNING: some process(es) wouldn't die");
			break;
		}
		sleep_while_procs(2 * i);
	}

	reboot(howto);
	/* FALLTHROUGH */

restart:
	errx(1, kill(1, SIGHUP) == -1 ? "(can't restart init): " : "");
	/* NOTREACHED */
}

void
usage(void)
{
	fprintf(stderr, "usage: %s [-dn%sq]\n", __progname,
	    dohalt ? "p" : "");
	exit(1);
}
