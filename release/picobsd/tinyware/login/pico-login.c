/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1980, 1987, 1988, 1991, 1993, 1994
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
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

#if 0
static char copyright[] =
"@(#) Copyright (c) 1980, 1987, 1988, 1991, 1993, 1994\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif

#ifndef lint
#if 0
static char sccsid[] = "@(#)login.c	8.4 (Berkeley) 4/2/94";
#endif
static const char rcsid[] =
  "$FreeBSD$";
#endif /* not lint */

/*
 * login [ name ]
 * login -h hostname	(for telnetd, etc.)
 * login -f name	(for pre-authenticated login: datakit, xterm, etc.)
 */

#include <sys/copyright.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/file.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <err.h>
#include <errno.h>
#include <grp.h>
#include <libutil.h>
#include <login_cap.h>
#include <netdb.h>
#include <pwd.h>
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <ttyent.h>
#include <unistd.h>
#include <utmpx.h>

#ifdef USE_PAM
#include <security/pam_appl.h>
#include <security/openpam.h>
#include <sys/wait.h>
#endif /* USE_PAM */

#include "pathnames.h"

void	 badlogin(char *);
void	 checknologin(void);
void	 dolastlog(int);
void	 getloginname(void);
void	 motd(const char *);
int	 rootterm(char *);
void	 sigint(int);
void	 sleepexit(int);
void	 refused(char *,char *,int);
char	*stypeof(char *);
void	 timedout(int);
int	 login_access(char *, char *);
void     login_fbtab(char *, uid_t, gid_t);

#ifdef USE_PAM
static int auth_pam(void);
static int export_pam_environment(void);
static int ok_to_export(const char *);

static pam_handle_t *pamh = NULL;
static char **environ_pam;

#define PAM_END { \
	if ((e = pam_setcred(pamh, PAM_DELETE_CRED)) != PAM_SUCCESS) \
		syslog(LOG_ERR, "pam_setcred: %s", pam_strerror(pamh, e)); \
	if ((e = pam_close_session(pamh,0)) != PAM_SUCCESS) \
		syslog(LOG_ERR, "pam_close_session: %s", pam_strerror(pamh, e)); \
	if ((e = pam_end(pamh, e)) != PAM_SUCCESS) \
		syslog(LOG_ERR, "pam_end: %s", pam_strerror(pamh, e)); \
}
#endif

static int auth_traditional(void);
static void usage(void);

#define	TTYGRPNAME	"tty"		/* name of group to own ttys */
#define	DEFAULT_BACKOFF	3
#define	DEFAULT_RETRIES	10
#define	DEFAULT_PROMPT		"login: "
#define	DEFAULT_PASSWD_PROMPT	"Password:"

/*
 * This bounds the time given to login.  Not a define so it can
 * be patched on machines where it's too small.
 */
u_int	timeout = 300;

/* Buffer for signal handling of timeout */
jmp_buf timeout_buf;

struct	passwd *pwd;
int	failures;
char	*term, *envinit[1], *hostname, *tty, *username;
const char *passwd_prompt, *prompt;
char    full_hostname[MAXHOSTNAMELEN];

int
main(argc, argv)
	int argc;
	char *argv[];
{
	extern char **environ;
	struct group *gr;
	struct stat st;
	struct utmpx utmp;
	int rootok, retries, backoff;
	int ask, ch, cnt, fflag, hflag, pflag, quietlog, rootlogin, rval;
	int changepass;
	time_t now, warntime;
	uid_t uid, euid;
	gid_t egid;
	char *p, *ttyn;
	char tbuf[MAXPATHLEN + 2];
	char tname[sizeof(_PATH_TTY) + 10];
	const char *shell = NULL;
	login_cap_t *lc = NULL;
	int UT_HOSTSIZE = sizeof(utmp.ut_host);
	int UT_NAMESIZE = sizeof(utmp.ut_user);
#ifdef USE_PAM
	pid_t pid;
	int e;
#endif /* USE_PAM */

	(void)signal(SIGQUIT, SIG_IGN);
	(void)signal(SIGINT, SIG_IGN);
	(void)signal(SIGHUP, SIG_IGN);
	if (setjmp(timeout_buf)) {
		if (failures)
			badlogin(tbuf);
		(void)fprintf(stderr, "Login timed out after %d seconds\n",
		    timeout);
		exit(0);
	}
	(void)signal(SIGALRM, timedout);
	(void)alarm(timeout);
	(void)setpriority(PRIO_PROCESS, 0, 0);

	openlog("login", LOG_ODELAY, LOG_AUTH);

	/*
	 * -p is used by getty to tell login not to destroy the environment
	 * -f is used to skip a second login authentication
	 * -h is used by other servers to pass the name of the remote
	 *    host to login so that it may be placed in utmp and wtmp
	 */
	*full_hostname = '\0';
	term = NULL;

	fflag = hflag = pflag = 0;
	uid = getuid();
	euid = geteuid();
	egid = getegid();
	while ((ch = getopt(argc, argv, "fh:p")) != -1)
		switch (ch) {
		case 'f':
			fflag = 1;
			break;
		case 'h':
			if (uid)
				errx(1, "-h option: %s", strerror(EPERM));
			hflag = 1;
			if (strlcpy(full_hostname, optarg,
			    sizeof(full_hostname)) >= sizeof(full_hostname))
				errx(1, "-h option: %s: exceeds maximum "
				    "hostname size", optarg);

			trimdomain(optarg, UT_HOSTSIZE);

			if (strlen(optarg) > UT_HOSTSIZE) {
				struct addrinfo hints, *res;
				int ga_err;
				
				memset(&hints, 0, sizeof(hints));
				hints.ai_family = AF_UNSPEC;
				ga_err = getaddrinfo(optarg, NULL, &hints,
				    &res);
				if (ga_err == 0) {
					char hostbuf[MAXHOSTNAMELEN];

					getnameinfo(res->ai_addr,
					    res->ai_addrlen,
					    hostbuf,
					    sizeof(hostbuf), NULL, 0,
					    NI_NUMERICHOST);
					optarg = strdup(hostbuf);
					if (optarg == NULL) {
						syslog(LOG_NOTICE,
						    "strdup(): %m");
						sleepexit(1);
					}
				} else
					optarg = "invalid hostname";
				if (res != NULL)
					freeaddrinfo(res);
			}
			hostname = optarg;
			break;
		case 'p':
			pflag = 1;
			break;
		case '?':
		default:
			if (!uid)
				syslog(LOG_ERR, "invalid flag %c", ch);
			usage();
		}
	argc -= optind;
	argv += optind;

	if (*argv) {
		username = *argv;
		ask = 0;
	} else
		ask = 1;

	for (cnt = getdtablesize(); cnt > 2; cnt--)
		(void)close(cnt);

	ttyn = ttyname(STDIN_FILENO);
	if (ttyn == NULL || *ttyn == '\0') {
		(void)snprintf(tname, sizeof(tname), "%s??", _PATH_TTY);
		ttyn = tname;
	}
	if ((tty = strrchr(ttyn, '/')) != NULL)
		++tty;
	else
		tty = ttyn;

	/*
	 * Get "login-retries" & "login-backoff" from default class
	 */
	lc = login_getclass(NULL);
	prompt = login_getcapstr(lc, "login_prompt",
	    DEFAULT_PROMPT, DEFAULT_PROMPT);
	passwd_prompt = login_getcapstr(lc, "passwd_prompt",
	    DEFAULT_PASSWD_PROMPT, DEFAULT_PASSWD_PROMPT);
	retries = login_getcapnum(lc, "login-retries", DEFAULT_RETRIES,
	    DEFAULT_RETRIES);
	backoff = login_getcapnum(lc, "login-backoff", DEFAULT_BACKOFF,
	    DEFAULT_BACKOFF);
	login_close(lc);
	lc = NULL;

	for (cnt = 0;; ask = 1) {
		if (ask) {
			fflag = 0;
			getloginname();
		}
		rootlogin = 0;
		rootok = rootterm(tty); /* Default (auth may change) */

		if (strlen(username) > UT_NAMESIZE)
			username[UT_NAMESIZE] = '\0';

		/*
		 * Note if trying multiple user names; log failures for
		 * previous user name, but don't bother logging one failure
		 * for nonexistent name (mistyped username).
		 */
		if (failures && strcmp(tbuf, username)) {
			if (failures > (pwd ? 0 : 1))
				badlogin(tbuf);
		}
		(void)strlcpy(tbuf, username, sizeof(tbuf));

		pwd = getpwnam(username);

		/*
		 * if we have a valid account name, and it doesn't have a
		 * password, or the -f option was specified and the caller
		 * is root or the caller isn't changing their uid, don't
		 * authenticate.
		 */
		if (pwd != NULL) {
			if (pwd->pw_uid == 0)
				rootlogin = 1;

			if (fflag && (uid == (uid_t)0 ||
			    uid == (uid_t)pwd->pw_uid)) {
				/* already authenticated */
				break;
			} else if (pwd->pw_passwd[0] == '\0') {
				if (!rootlogin || rootok) {
					/* pretend password okay */
					rval = 0;
					goto ttycheck;
				}
			}
		}

		fflag = 0;

		(void)setpriority(PRIO_PROCESS, 0, -4);

#ifdef USE_PAM
		/*
		 * Try to authenticate using PAM.  If a PAM system error
		 * occurs, perhaps because of a botched configuration,
		 * then fall back to using traditional Unix authentication.
		 */
		if ((rval = auth_pam()) == -1)
#endif /* USE_PAM */
			rval = auth_traditional();

		(void)setpriority(PRIO_PROCESS, 0, 0);

#ifdef USE_PAM
		/*
		 * PAM authentication may have changed "pwd" to the
		 * entry for the template user.  Check again to see if
		 * this is a root login after all.
		 */
		if (pwd != NULL && pwd->pw_uid == 0)
			rootlogin = 1;
#endif /* USE_PAM */

	ttycheck:
		/*
		 * If trying to log in as root without Kerberos,
		 * but with insecure terminal, refuse the login attempt.
		 */
		if (pwd && !rval) {
			if (rootlogin && !rootok)
				refused(NULL, "NOROOT", 0);
			else	/* valid password & authenticated */
				break;
		}

		(void)printf("Login incorrect\n");
		failures++;

		/*
		 * we allow up to 'retry' (10) tries,
		 * but after 'backoff' (3) we start backing off
		 */
		if (++cnt > backoff) {
			if (cnt >= retries) {
				badlogin(username);
				sleepexit(1);
			}
			sleep((u_int)((cnt - backoff) * 5));
		}
	}

	/* committed to login -- turn off timeout */
	(void)alarm((u_int)0);
	(void)signal(SIGHUP, SIG_DFL);

	endpwent();

	/*
	 * Establish the login class.
	 */
	lc = login_getpwclass(pwd);

	/* if user not super-user, check for disabled logins */
	if (!rootlogin)
		auth_checknologin(lc);

	quietlog = login_getcapbool(lc, "hushlogin", 0);
	/*
	 * Switching needed for NFS with root access disabled.
	 *
	 * XXX: This change fails to modify the additional groups for the
	 * process, and as such, may restrict rights normally granted
	 * through those groups.
	 */
	(void)setegid(pwd->pw_gid);
	(void)seteuid(rootlogin ? 0 : pwd->pw_uid);
	if (!*pwd->pw_dir || chdir(pwd->pw_dir) < 0) {
		if (login_getcapbool(lc, "requirehome", 0))
			refused("Home directory not available", "HOMEDIR", 1);
		if (chdir("/") < 0) 
			refused("Cannot find root directory", "ROOTDIR", 1);
		if (!quietlog || *pwd->pw_dir)
			printf("No home directory.\nLogging in with home = \"/\".\n");
		pwd->pw_dir = "/";
	}
	(void)seteuid(euid);
	(void)setegid(egid);
	if (!quietlog)
		quietlog = access(_PATH_HUSHLOGIN, F_OK) == 0;

	now = time(NULL);

#define	DEFAULT_WARN  (2L * 7L * 86400L)  /* Two weeks */

	warntime = login_getcaptime(lc, "warnexpire", DEFAULT_WARN,
	    DEFAULT_WARN);

	if (pwd->pw_expire) {
		if (now >= pwd->pw_expire) {
			refused("Sorry -- your account has expired", "EXPIRED",
			    1);
		} else if (pwd->pw_expire - now < warntime && !quietlog)
			(void)printf("Warning: your account expires on %s",
			    ctime(&pwd->pw_expire));
	}

	warntime = login_getcaptime(lc, "warnpassword", DEFAULT_WARN,
	    DEFAULT_WARN);

	changepass = 0;
	if (pwd->pw_change) {
		if (now >= pwd->pw_change) {
			(void)printf("Sorry -- your password has expired.\n");
			changepass = 1;
			syslog(LOG_INFO, "%s Password expired - forcing change",
			    pwd->pw_name);
		} else if (pwd->pw_change - now < warntime && !quietlog)
			(void)printf("Warning: your password expires on %s",
			    ctime(&pwd->pw_change));
	}

	if (lc != NULL) {
		if (hostname) {
			struct addrinfo hints, *res;
			int ga_err;

			memset(&hints, 0, sizeof(hints));
			hints.ai_family = AF_UNSPEC;
			ga_err = getaddrinfo(full_hostname, NULL, &hints,
					     &res);
			if (ga_err == 0) {
				char hostbuf[MAXHOSTNAMELEN];

				getnameinfo(res->ai_addr, res->ai_addrlen,
				    hostbuf, sizeof(hostbuf), NULL, 0,
				    NI_NUMERICHOST);
				if ((optarg = strdup(hostbuf)) == NULL) {
					syslog(LOG_NOTICE, "strdup(): %m");
					sleepexit(1);
				}
			} else
				optarg = NULL;
			if (res != NULL)
				freeaddrinfo(res);
			if (!auth_hostok(lc, full_hostname, optarg))
				refused("Permission denied", "HOST", 1);
		}

		if (!auth_ttyok(lc, tty))
			refused("Permission denied", "TTY", 1);

		if (!auth_timeok(lc, time(NULL)))
			refused("Logins not available right now", "TIME", 1);
	}
        shell = login_getcapstr(lc, "shell", pwd->pw_shell, pwd->pw_shell);
	if (*pwd->pw_shell == '\0')
		pwd->pw_shell = _PATH_BSHELL;
	if (*shell == '\0')   /* Not overridden */
		shell = pwd->pw_shell;
	if ((shell = strdup(shell)) == NULL) {
		syslog(LOG_NOTICE, "strdup(): %m");
		sleepexit(1);
	}

#ifdef LOGIN_ACCESS
	if (login_access(pwd->pw_name, hostname ? full_hostname : tty) == 0)
		refused("Permission denied", "ACCESS", 1);
#endif /* LOGIN_ACCESS */

#if 1
	ulog_login(tty, username, hostname);
#else
	/* Nothing else left to fail -- really log in. */
	memset((void *)&utmp, 0, sizeof(utmp));
	(void)gettimeofday(&utmp.ut_tv, NULL);
	(void)strncpy(utmp.ut_user, username, sizeof(utmp.ut_user));
	if (hostname)
		(void)strncpy(utmp.ut_host, hostname, sizeof(utmp.ut_host));
	(void)strncpy(utmp.ut_line, tty, sizeof(utmp.ut_line));
	login(&utmp);
#endif

	dolastlog(quietlog);

	/*
	 * Set device protections, depending on what terminal the
	 * user is logged in. This feature is used on Suns to give
	 * console users better privacy.
	 */
	login_fbtab(tty, pwd->pw_uid, pwd->pw_gid);

	/*
	 * Clear flags of the tty.  None should be set, and when the
	 * user sets them otherwise, this can cause the chown to fail.
	 * Since it isn't clear that flags are useful on character
	 * devices, we just clear them.
	 */
	if (chflags(ttyn, 0) && errno != EOPNOTSUPP)
		syslog(LOG_ERR, "chflags(%s): %m", ttyn);
	if (chown(ttyn, pwd->pw_uid,
	    (gr = getgrnam(TTYGRPNAME)) ? gr->gr_gid : pwd->pw_gid))
		syslog(LOG_ERR, "chown(%s): %m", ttyn);


	/*
	 * Preserve TERM if it happens to be already set.
	 */
	if ((term = getenv("TERM")) != NULL) {
		if ((term = strdup(term)) == NULL) {
			syslog(LOG_NOTICE,
			    "strdup(): %m");
			sleepexit(1);
		}
	}

	/*
	 * Exclude cons/vt/ptys only, assume dialup otherwise
	 * TODO: Make dialup tty determination a library call
	 * for consistency (finger etc.)
	 */
	if (hostname==NULL && isdialuptty(tty))
		syslog(LOG_INFO, "DIALUP %s, %s", tty, pwd->pw_name);

#ifdef LOGALL
	/*
	 * Syslog each successful login, so we don't have to watch hundreds
	 * of wtmp or lastlogin files.
	 */
	if (hostname)
		syslog(LOG_INFO, "login from %s on %s as %s",
		       full_hostname, tty, pwd->pw_name);
	else
		syslog(LOG_INFO, "login on %s as %s",
		       tty, pwd->pw_name);
#endif

	/*
	 * If fflag is on, assume caller/authenticator has logged root login.
	 */
	if (rootlogin && fflag == 0)
	{
		if (hostname)
			syslog(LOG_NOTICE, "ROOT LOGIN (%s) ON %s FROM %s",
			    username, tty, full_hostname);
		else
			syslog(LOG_NOTICE, "ROOT LOGIN (%s) ON %s",
			    username, tty);
	}

	/*
	 * Destroy environment unless user has requested its preservation.
	 * We need to do this before setusercontext() because that may
	 * set or reset some environment variables.
	 */
	if (!pflag)
		environ = envinit;

	/*
	 * PAM modules might add supplementary groups during pam_setcred().
	 */
	if (setusercontext(lc, pwd, pwd->pw_uid, LOGIN_SETGROUP) != 0) {
                syslog(LOG_ERR, "setusercontext() failed - exiting");
		exit(1);
	}

#ifdef USE_PAM
	if (pamh) {
		if ((e = pam_open_session(pamh, 0)) != PAM_SUCCESS) {
			syslog(LOG_ERR, "pam_open_session: %s",
			    pam_strerror(pamh, e));
		} else if ((e = pam_setcred(pamh, PAM_ESTABLISH_CRED))
		    != PAM_SUCCESS) {
			syslog(LOG_ERR, "pam_setcred: %s",
			    pam_strerror(pamh, e));
		}

	        /*
	         * Add any environmental variables that the
	         * PAM modules may have set.
		 * Call *after* opening session!
		 */
		if (pamh) {
		  environ_pam = pam_getenvlist(pamh);
		  if (environ_pam)
			export_pam_environment();
		}

		/*
		 * We must fork() before setuid() because we need to call
		 * pam_close_session() as root.
		 */
		pid = fork();
		if (pid < 0) {
			err(1, "fork");
			PAM_END;
			exit(0);
		} else if (pid) {
			/* parent - wait for child to finish, then cleanup
			   session */
			wait(NULL);
			PAM_END;
			exit(0);
		} else {
			if ((e = pam_end(pamh, 0)) != PAM_SUCCESS)
				syslog(LOG_ERR, "pam_end: %s",
				    pam_strerror(pamh, e));
		}
	}
#endif /* USE_PAM */

	/*
	 * We don't need to be root anymore, so
	 * set the user and session context
	 */
	if (setlogin(username) != 0) {
                syslog(LOG_ERR, "setlogin(%s): %m - exiting", username);
		exit(1);
	}
	if (setusercontext(lc, pwd, pwd->pw_uid,
	    LOGIN_SETALL & ~(LOGIN_SETLOGIN|LOGIN_SETGROUP)) != 0) {
                syslog(LOG_ERR, "setusercontext() failed - exiting");
		exit(1);
	}

	(void)setenv("SHELL", pwd->pw_shell, 1);
	(void)setenv("HOME", pwd->pw_dir, 1);
	if (term != NULL && *term != '\0')
		(void)setenv("TERM", term, 1);		/* Preset overrides */
	else {
		(void)setenv("TERM", stypeof(tty), 0);	/* Fallback doesn't */
	}
	(void)setenv("LOGNAME", username, 1);
	(void)setenv("USER", username, 1);
	(void)setenv("PATH", rootlogin ? _PATH_STDPATH : _PATH_DEFPATH, 0);

	if (!quietlog) {
		const char *cw;

		cw = login_getcapstr(lc, "copyright", NULL, NULL);
		if (cw != NULL && access(cw, F_OK) == 0)
			motd(cw);
		else
		    (void)printf("%s\n\t%s %s\n",
	"Copyright (c) 1980, 1983, 1986, 1988, 1990, 1991, 1993, 1994",
	"The Regents of the University of California. ",
	"All rights reserved.");

		(void)printf("\n");

		cw = login_getcapstr(lc, "welcome", NULL, NULL);
		if (cw == NULL || access(cw, F_OK) != 0)
			cw = _PATH_MOTDFILE;
		motd(cw);

		cw = getenv("MAIL");	/* $MAIL may have been set by class */
		if (cw != NULL)
			strlcpy(tbuf, cw, sizeof(tbuf));
		else
			snprintf(tbuf, sizeof(tbuf), "%s/%s", _PATH_MAILDIR,
			    pwd->pw_name);
		if (stat(tbuf, &st) == 0 && st.st_size != 0)
			(void)printf("You have %smail.\n",
			    (st.st_mtime > st.st_atime) ? "new " : "");
	}

	login_close(lc);

	(void)signal(SIGALRM, SIG_DFL);
	(void)signal(SIGQUIT, SIG_DFL);
	(void)signal(SIGINT, SIG_DFL);
	(void)signal(SIGTSTP, SIG_IGN);

	/*
	 * Login shells have a leading '-' in front of argv[0]
	 */
	if (snprintf(tbuf, sizeof(tbuf), "-%s",
	    (p = strrchr(pwd->pw_shell, '/')) ? p + 1 : pwd->pw_shell) >=
	    sizeof(tbuf)) {
		syslog(LOG_ERR, "user: %s: shell exceeds maximum pathname size",
		    username);
		errx(1, "shell exceeds maximum pathname size");
	}

	execlp(shell, tbuf, (char *)0);
	err(1, "%s", shell);
}

static int
auth_traditional()
{
	int rval;
	char *p;
	char *ep;
	char *salt;

	rval = 1;
	salt = pwd != NULL ? pwd->pw_passwd : "xx";

	p = getpass(passwd_prompt);
	ep = crypt(p, salt);

	if (pwd) {
		if (!p[0] && pwd->pw_passwd[0])
			ep = ":";
		if (strcmp(ep, pwd->pw_passwd) == 0)
			rval = 0;
	}

	/* clear entered password */
	memset(p, 0, strlen(p));
	return rval;
}

#ifdef USE_PAM
/*
 * Attempt to authenticate the user using PAM.  Returns 0 if the user is
 * authenticated, or 1 if not authenticated.  If some sort of PAM system
 * error occurs (e.g., the "/etc/pam.conf" file is missing) then this
 * function returns -1.  This can be used as an indication that we should
 * fall back to a different authentication mechanism.
 */
static int
auth_pam()
{
	const char *tmpl_user;
	const void *item;
	int rval;
	int e;
	static struct pam_conv conv = { openpam_ttyconv, NULL };

	if ((e = pam_start("login", username, &conv, &pamh)) != PAM_SUCCESS) {
		syslog(LOG_ERR, "pam_start: %s", pam_strerror(pamh, e));
		return -1;
	}
	if ((e = pam_set_item(pamh, PAM_TTY, tty)) != PAM_SUCCESS) {
		syslog(LOG_ERR, "pam_set_item(PAM_TTY): %s",
		    pam_strerror(pamh, e));
		return -1;
	}
	if (hostname != NULL &&
	    (e = pam_set_item(pamh, PAM_RHOST, full_hostname)) != PAM_SUCCESS) {
		syslog(LOG_ERR, "pam_set_item(PAM_RHOST): %s",
		    pam_strerror(pamh, e));
		return -1;
	}
	e = pam_authenticate(pamh, 0);
	switch (e) {

	case PAM_SUCCESS:
		/*
		 * With PAM we support the concept of a "template"
		 * user.  The user enters a login name which is
		 * authenticated by PAM, usually via a remote service
		 * such as RADIUS or TACACS+.  If authentication
		 * succeeds, a different but related "template" name
		 * is used for setting the credentials, shell, and
		 * home directory.  The name the user enters need only
		 * exist on the remote authentication server, but the
		 * template name must be present in the local password
		 * database.
		 *
		 * This is supported by two various mechanisms in the
		 * individual modules.  However, from the application's
		 * point of view, the template user is always passed
		 * back as a changed value of the PAM_USER item.
		 */
		if ((e = pam_get_item(pamh, PAM_USER, &item)) ==
		    PAM_SUCCESS) {
			tmpl_user = (const char *) item;
			if (strcmp(username, tmpl_user) != 0)
				pwd = getpwnam(tmpl_user);
		} else
			syslog(LOG_ERR, "Couldn't get PAM_USER: %s",
			    pam_strerror(pamh, e));
		rval = 0;
		break;

	case PAM_AUTH_ERR:
	case PAM_USER_UNKNOWN:
	case PAM_MAXTRIES:
		rval = 1;
		break;

	default:
		syslog(LOG_ERR, "pam_authenticate: %s", pam_strerror(pamh, e));
		rval = -1;
		break;
	}

	if (rval == 0) {
		e = pam_acct_mgmt(pamh, 0);
		if (e == PAM_NEW_AUTHTOK_REQD) {
			e = pam_chauthtok(pamh, PAM_CHANGE_EXPIRED_AUTHTOK);
			if (e != PAM_SUCCESS) {
				syslog(LOG_ERR, "pam_chauthtok: %s",
				    pam_strerror(pamh, e));
				rval = 1;
			}
		} else if (e != PAM_SUCCESS) {
			rval = 1;
		}
	}

	if (rval != 0) {
		if ((e = pam_end(pamh, e)) != PAM_SUCCESS) {
			syslog(LOG_ERR, "pam_end: %s", pam_strerror(pamh, e));
		}
		pamh = NULL;
	}
	return rval;
}

static int
export_pam_environment()
{
	char	**pp;

	for (pp = environ_pam; *pp != NULL; pp++) {
		if (ok_to_export(*pp))
			(void) putenv(*pp);
		free(*pp);
	}
	return PAM_SUCCESS;
}

/*
 * Sanity checks on PAM environmental variables:
 * - Make sure there is an '=' in the string.
 * - Make sure the string doesn't run on too long.
 * - Do not export certain variables.  This list was taken from the
 *   Solaris pam_putenv(3) man page.
 */
static int
ok_to_export(s)
	const char *s;
{
	static const char *noexport[] = {
		"SHELL", "HOME", "LOGNAME", "MAIL", "CDPATH",
		"IFS", "PATH", NULL
	};
	const char **pp;
	size_t n;

	if (strlen(s) > 1024 || strchr(s, '=') == NULL)
		return 0;
	if (strncmp(s, "LD_", 3) == 0)
		return 0;
	for (pp = noexport; *pp != NULL; pp++) {
		n = strlen(*pp);
		if (s[n] == '=' && strncmp(s, *pp, n) == 0)
			return 0;
	}
	return 1;
}
#endif /* USE_PAM */

static void
usage()
{

	(void)fprintf(stderr, "usage: login [-fp] [-h hostname] [username]\n");
	exit(1);
}

/*
 * Allow for authentication style and/or kerberos instance
 */

#define	NBUFSIZ		128	// XXX was UT_NAMESIZE + 64

void
getloginname()
{
	int ch;
	char *p;
	static char nbuf[NBUFSIZ];

	for (;;) {
		(void)printf("%s", prompt);
		for (p = nbuf; (ch = getchar()) != '\n'; ) {
			if (ch == EOF) {
				badlogin(username);
				exit(0);
			}
			if (p < nbuf + (NBUFSIZ - 1))
				*p++ = ch;
		}
		if (p > nbuf) {
			if (nbuf[0] == '-')
				(void)fprintf(stderr,
				    "login names may not start with '-'.\n");
			else {
				*p = '\0';
				username = nbuf;
				break;
			}
		}
	}
}

int
rootterm(ttyn)
	char *ttyn;
{
	struct ttyent *t;

	return ((t = getttynam(ttyn)) && t->ty_status & TTY_SECURE);
}

volatile int motdinterrupt;

void
sigint(signo)
	int signo __unused;
{
	motdinterrupt = 1;
}

void
motd(motdfile)
	const char *motdfile;
{
	int fd, nchars;
	sig_t oldint;
	char tbuf[256];

	if ((fd = open(motdfile, O_RDONLY, 0)) < 0)
		return;
	motdinterrupt = 0;
	oldint = signal(SIGINT, sigint);
	while ((nchars = read(fd, tbuf, sizeof(tbuf))) > 0 && !motdinterrupt)
		(void)write(fileno(stdout), tbuf, nchars);
	(void)signal(SIGINT, oldint);
	(void)close(fd);
}

/* ARGSUSED */
void
timedout(signo)
	int signo;
{

	longjmp(timeout_buf, signo);
}


void
dolastlog(quiet)
	int quiet;
{
#if 0	/* XXX not implemented after utmp->utmpx change */
	struct lastlog ll;
	int fd;

	if ((fd = open(_PATH_LASTLOG, O_RDWR, 0)) >= 0) {
		(void)lseek(fd, (off_t)pwd->pw_uid * sizeof(ll), L_SET);
		if (!quiet) {
			if (read(fd, (char *)&ll, sizeof(ll)) == sizeof(ll) &&
			    ll.ll_time != 0) {
				(void)printf("Last login: %.*s ",
				    24-5, (char *)ctime(&ll.ll_time));
				if (*ll.ll_host != '\0')
					(void)printf("from %.*s\n",
					    (int)sizeof(ll.ll_host),
					    ll.ll_host);
				else
					(void)printf("on %.*s\n",
					    (int)sizeof(ll.ll_line),
					    ll.ll_line);
			}
			(void)lseek(fd, (off_t)pwd->pw_uid * sizeof(ll), L_SET);
		}
		memset((void *)&ll, 0, sizeof(ll));
		(void)time(&ll.ll_time);
		(void)strncpy(ll.ll_line, tty, sizeof(ll.ll_line));
		if (hostname)
			(void)strncpy(ll.ll_host, hostname, sizeof(ll.ll_host));
		(void)write(fd, (char *)&ll, sizeof(ll));
		(void)close(fd);
	} else {
		syslog(LOG_ERR, "cannot open %s: %m", _PATH_LASTLOG);
	}
#endif
}

void
badlogin(name)
	char *name;
{

	if (failures == 0)
		return;
	if (hostname) {
		syslog(LOG_NOTICE, "%d LOGIN FAILURE%s FROM %s",
		    failures, failures > 1 ? "S" : "", full_hostname);
		syslog(LOG_AUTHPRIV|LOG_NOTICE,
		    "%d LOGIN FAILURE%s FROM %s, %s",
		    failures, failures > 1 ? "S" : "", full_hostname, name);
	} else {
		syslog(LOG_NOTICE, "%d LOGIN FAILURE%s ON %s",
		    failures, failures > 1 ? "S" : "", tty);
		syslog(LOG_AUTHPRIV|LOG_NOTICE,
		    "%d LOGIN FAILURE%s ON %s, %s",
		    failures, failures > 1 ? "S" : "", tty, name);
	}
	failures = 0;
}

#undef	UNKNOWN
#define	UNKNOWN	"su"

char *
stypeof(ttyid)
	char *ttyid;
{
	struct ttyent *t;

	if (ttyid != NULL && *ttyid != '\0') {
		t = getttynam(ttyid);
		if (t != NULL && t->ty_type != NULL)
			return (t->ty_type);
	}
	return (UNKNOWN);
}

void
refused(msg, rtype, lout)
	char *msg;
	char *rtype;
	int lout;
{

	if (msg != NULL)
	    printf("%s.\n", msg);
	if (hostname)
		syslog(LOG_NOTICE, "LOGIN %s REFUSED (%s) FROM %s ON TTY %s",
		    pwd->pw_name, rtype, full_hostname, tty);
	else
		syslog(LOG_NOTICE, "LOGIN %s REFUSED (%s) ON TTY %s",
		    pwd->pw_name, rtype, tty);
	if (lout)
		sleepexit(1);
}

void
sleepexit(eval)
	int eval;
{

	(void)sleep(5);
	exit(eval);
}
