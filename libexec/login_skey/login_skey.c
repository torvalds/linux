/*	$OpenBSD: login_skey.c,v 1.30 2023/03/08 04:43:05 guenther Exp $	*/

/*
 * Copyright (c) 2000, 2001, 2004 Todd C. Miller <millert@openbsd.org>
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

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/resource.h>

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <paths.h>
#include <pwd.h>
#include <readpassphrase.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <limits.h>
#include <err.h>

#include <login_cap.h>
#include <bsd_auth.h>
#include <skey.h>

#define	MODE_LOGIN	0
#define	MODE_CHALLENGE	1
#define	MODE_RESPONSE	2

void quit(int);
void send_fd(int);
void suspend(int);

volatile sig_atomic_t resumed;
struct skey skey;

int
main(int argc, char *argv[])
{
	FILE *back = NULL;
	char *user = NULL, *cp, *ep;
	char challenge[SKEY_MAX_CHALLENGE+17], response[SKEY_MAX_PW_LEN+1];
	const char *errstr;
	int ch, fd = -1, haskey = 0, mode = MODE_LOGIN;

	(void)signal(SIGINT, quit);
	(void)signal(SIGQUIT, quit);
	(void)signal(SIGALRM, quit);
	(void)signal(SIGTSTP, suspend);
	(void)setpriority(PRIO_PROCESS, 0, 0);

	if (pledge("stdio rpath wpath flock sendfd proc tty", NULL) == -1) {
		syslog(LOG_AUTH|LOG_ERR, "pledge: %m");
		exit(1);
	}

	openlog(NULL, LOG_ODELAY, LOG_AUTH);

	while ((ch = getopt(argc, argv, "ds:v:")) != -1) {
		switch (ch) {
		case 'd':
			back = stdout;
			break;
		case 's':	/* service */
			if (strcmp(optarg, "login") == 0)
				mode = MODE_LOGIN;
			else if (strcmp(optarg, "challenge") == 0)
				mode = MODE_CHALLENGE;
			else if (strcmp(optarg, "response") == 0)
				mode = MODE_RESPONSE;
			else {
				syslog(LOG_ERR, "%s: invalid service", optarg);
				exit(1);
			}
			break;
		case 'v':
			if (strncmp(optarg, "fd=", 3) == 0) {
				fd = strtonum(optarg + 3, 0, INT_MAX, &errstr);
				if (errstr != NULL) {
					syslog(LOG_ERR, "fd is %s: %s",
					    errstr, optarg + 3);
					fd = -1;
				}
			}
			/* silently ignore unsupported variables */
			break;
		default:
			syslog(LOG_ERR, "usage error");
			exit(1);
		}
	}
	argc -= optind;
	argv += optind;

	switch (argc) {
	case 2:	/* silently ignore class */
	case 1:
		user = *argv;
		break;
	default:
		syslog(LOG_ERR, "usage error");
		exit(1);
	}

	if (back == NULL && (back = fdopen(3, "r+")) == NULL)  {
		syslog(LOG_ERR, "reopening back channel: %m");
		exit(1);
	}

	/*
	 * Note: our skeychallenge2() will always fill in the challenge,
	 *       even if it has to create a fake one.
	 */
	switch (mode) {
	case MODE_LOGIN:
		haskey = (skeychallenge2(fd, &skey, user, challenge) == 0);
		strlcat(challenge, "\nS/Key Password:", sizeof(challenge));

		/* time out getting passphrase after 2 minutes to avoid a DoS */
		if (haskey)
			alarm(120);
		resumed = 0;
		if (!readpassphrase(challenge, response, sizeof(response), 0))
			exit(1);
		if (response[0] == '\0')
			readpassphrase("S/Key Password [echo on]: ",
			    response, sizeof(response), RPP_ECHO_ON);
		alarm(0);
		if (resumed) {
			/*
			 * We were suspended by the user.  Our lock is
			 * no longer valid so we must regain it so
			 * an attacker cannot do a partial guess of
			 * an S/Key response already in progress.
			 */
			haskey = (skeylookup(&skey, user) == 0);
		}
		break;

	case MODE_CHALLENGE:
		haskey = (skeychallenge2(fd, &skey, user, challenge) == 0);
		strlcat(challenge, "\nS/Key Password:", sizeof(challenge));
		cp = auth_mkvalue(challenge);
		if (cp == NULL) {
			(void)fprintf(back, BI_VALUE " errormsg %s\n",
			    "unable to allocate memory");
			(void)fprintf(back, BI_REJECT "\n");
			exit(1);
		}
		fprintf(back, BI_VALUE " challenge %s\n", cp);
		fprintf(back, BI_CHALLENGE "\n");
		if (haskey) {
			fprintf(back, BI_FDPASS "\n");
			fflush(back);
			send_fd(fileno(back));
		}
		exit(0);

	case MODE_RESPONSE:
		/* read challenge */
		mode = -1;
		cp = challenge;
		ep = challenge + sizeof(challenge);
		while (cp < ep && read(fileno(back), cp, 1) == 1) {
			if (*cp++ == '\0') {
				mode = MODE_CHALLENGE;
				break;
			}
		}
		if (mode != MODE_CHALLENGE) {
			syslog(LOG_ERR,
			    "protocol error: bad/missing challenge");
			exit(1);
		}

		/* read response */
		cp = response;
		ep = response + sizeof(response);
		while (cp < ep && read(fileno(back), cp, 1) == 1) {
			if (*cp++ == '\0') {
				mode = MODE_RESPONSE;
				break;
			}
		}
		if (mode != MODE_RESPONSE) {
			syslog(LOG_ERR,
			    "protocol error: bad/missing response");
			exit(1);
		}

		/*
		 * Since the entry is locked we do not need to compare
		 * the passed in challenge to the S/Key database but
		 * maybe we should anyway?
		 */
		haskey = (skeychallenge2(fd, &skey, user, challenge) == 0);
		break;
	}

	/*
	 * Ignore keyboard interrupt/suspend during database update.
	 */
	signal(SIGINT, SIG_IGN);
	signal(SIGQUIT, SIG_IGN);
	signal(SIGTSTP, SIG_IGN);

	if (haskey && skeyverify(&skey, response) == 0) {
		if (mode == MODE_LOGIN) {
			if (skey.n <= 1)
				printf("Warning! You MUST change your "
				    "S/Key password now!\n");
			else if (skey.n < 5)
				printf("Warning! Change S/Key password soon\n");
		}
		fprintf(back, BI_AUTH "\n");
		fprintf(back, BI_SECURE "\n");
		exit(0);
	}
	fprintf(back, BI_REJECT "\n");
	exit(1);
}

void
quit(int signo)
{

	_exit(1);
}

void
suspend(int signo)
{
	sigset_t nset;
	int save_errno = errno;

	/*
	 * Unlock the skey record so we don't sleep holding the lock.
	 * Unblock SIGTSTP, set it to the default action and then
	 * resend it so we are suspended properly.
	 * When we resume, reblock SIGTSTP, reset the signal handler,
	 * set a flag and restore errno.
	 */
	alarm(0);
	skey_unlock(&skey);
	(void)signal(signo, SIG_DFL);
	(void)sigemptyset(&nset);
	(void)sigaddset(&nset, signo);
	(void)sigprocmask(SIG_UNBLOCK, &nset, NULL);
	(void)kill(getpid(), signo);
	(void)sigprocmask(SIG_BLOCK, &nset, NULL);
	(void)signal(signo, suspend);
	resumed = 1;
	errno = save_errno;
}

void
send_fd(int sock)
{
	struct msghdr msg;
	struct cmsghdr *cmp;
	union {
		struct cmsghdr hdr;
		char buf[CMSG_SPACE(sizeof(int))];
	} cmsgbuf;

	memset(&msg, 0, sizeof(msg));
	msg.msg_control = &cmsgbuf.buf;
	msg.msg_controllen = sizeof(cmsgbuf.buf);

	cmp = CMSG_FIRSTHDR(&msg);
	cmp->cmsg_len = CMSG_LEN(sizeof(int));
	cmp->cmsg_level = SOL_SOCKET;
	cmp->cmsg_type = SCM_RIGHTS;

	*(int *)CMSG_DATA(cmp) = fileno(skey.keyfile);

	if (sendmsg(sock, &msg, 0) == -1)
		syslog(LOG_ERR, "sendmsg: %m");
}
