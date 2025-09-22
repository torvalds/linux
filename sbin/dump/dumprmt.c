/*	$OpenBSD: dumprmt.c,v 1.33 2023/12/21 08:01:21 otto Exp $	*/
/*	$NetBSD: dumprmt.c,v 1.17 1997/06/05 16:10:47 mrg Exp $	*/

/*-
 * Copyright (c) 1980, 1993
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

#include <sys/param.h>	/* MAXBSIZE */
#include <sys/mtio.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <ufs/ufs/dinode.h>

#include <netinet/in.h>
#include <netinet/tcp.h>

#include <protocols/dumprestore.h>

#include <ctype.h>
#include <err.h>
#include <netdb.h>
#include <errno.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>

#include "pathnames.h"
#include "dump.h"

#define	TS_CLOSED	0
#define	TS_OPEN		1

static	int rmtstate = TS_CLOSED;
static	int rmtape;
static	char *rmtpeer;

static	int okname(char *);
static	int rmtcall(char *, char *);
static	void rmtconnaborted(int);
static	int rmtgetb(void);
static	void rmtgetconn(void);
static	void rmtgets(char *, int);
static	int rmtreply(char *);

int	rmthost(char *host);
int	rmtopen(char *tape, int mode);
int	rmtread(char *buf, int count);
int	rmtwrite(char *buf, int count);
int	rmtseek(int offset, int pos);
int	rmtioctl(int cmd, int count);
void	rmtclose(void);


int
rmthost(char *host)
{
	int len = strlen(host) + 1;

	rmtpeer = malloc(len);
	if (rmtpeer)
		strlcpy(rmtpeer, host, len);
	else
		return (0);
	signal(SIGPIPE, rmtconnaborted);
	rmtgetconn();
	if (rmtape < 0)
		return (0);
	return (1);
}

static void
rmtconnaborted(int signo)
{
	/* XXX signal race */
	errx(X_ABORT, "Lost connection to remote host.");
}

void
rmtgetconn(void)
{
	char *cp;
	static struct servent *sp = NULL;
	static struct passwd *pwd = NULL;
	static int on = 1;
	char *tuser, *name;
	int size;
	int maxseg;

	if (sp == NULL) {
		sp = getservbyname("shell", "tcp");
		if (sp == NULL)
			errx(X_STARTUP, "shell/tcp: unknown service");
		pwd = getpwuid(getuid());
		if (pwd == NULL)
			errx(X_STARTUP, "who are you?");
	}
	if ((name = strdup(pwd->pw_name)) == NULL)
		err(X_STARTUP, "malloc");
	if ((cp = strchr(rmtpeer, '@')) != NULL) {
		tuser = rmtpeer;
		*cp = '\0';
		if (!okname(tuser))
			exit(X_STARTUP);
		rmtpeer = ++cp;
	} else
		tuser = name;

	rmtape = rcmd(&rmtpeer, sp->s_port, name, tuser, _PATH_RMT, NULL);
	(void)free(name);
	if (rmtape < 0)
		return;

	size = ntrec * TP_BSIZE;
	if (size > 60 * 1024)		/* XXX */
		size = 60 * 1024;
	/* Leave some space for rmt request/response protocol */
	size += 2 * 1024;
	while (size > TP_BSIZE &&
	    setsockopt(rmtape, SOL_SOCKET, SO_SNDBUF, &size, sizeof(size)) == -1)
		    size -= TP_BSIZE;
	(void)setsockopt(rmtape, SOL_SOCKET, SO_RCVBUF, &size, sizeof(size));

	maxseg = 1024;
	(void)setsockopt(rmtape, IPPROTO_TCP, TCP_MAXSEG, &maxseg,
		sizeof(maxseg));

	(void) setsockopt(rmtape, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on));
}

static int
okname(char *cp0)
{
	char *cp;
	unsigned char c;

	for (cp = cp0; *cp; cp++) {
		c = *cp;
		if (!isascii(c) || !(isalnum(c) || c == '_' || c == '-')) {
			warnx("invalid user name: %s", cp0);
			return (0);
		}
	}
	return (1);
}

int
rmtopen(char *tape, int mode)
{
	char buf[256];

	(void)snprintf(buf, sizeof(buf), "O%s\n%d\n", tape, mode);
	rmtstate = TS_OPEN;
	return (rmtcall(tape, buf));
}

void
rmtclose(void)
{

	if (rmtstate != TS_OPEN)
		return;
	rmtcall("close", "C\n");
	rmtstate = TS_CLOSED;
}

int
rmtread(char *buf, int count)
{
	char line[30];
	int n, i, cc;

	(void)snprintf(line, sizeof(line), "R%d\n", count);
	n = rmtcall("read", line);
	if (n < 0) {
		errno = n;
		return (-1);
	}
	for (i = 0; i < n; i += cc) {
		cc = read(rmtape, buf+i, n - i);
		if (cc <= 0) {
			rmtconnaborted(0);
		}
	}
	return (n);
}

int
rmtwrite(char *buf, int count)
{
	char line[30];

	(void)snprintf(line, sizeof(line), "W%d\n", count);
	write(rmtape, line, strlen(line));
	write(rmtape, buf, count);
	return (rmtreply("write"));
}

int
rmtseek(int offset, int pos)
{
	char line[80];

	(void)snprintf(line, sizeof(line), "L%d\n%d\n", offset, pos);
	return (rmtcall("seek", line));
}

int
rmtioctl(int cmd, int count)
{
	char buf[256];

	if (count < 0)
		return (-1);
	(void)snprintf(buf, sizeof(buf), "I%d\n%d\n", cmd, count);
	return (rmtcall("ioctl", buf));
}

static int
rmtcall(char *cmd, char *buf)
{

	if (write(rmtape, buf, strlen(buf)) != strlen(buf))
		rmtconnaborted(0);
	return (rmtreply(cmd));
}

static int
rmtreply(char *cmd)
{
	char *cp;
	char code[30], emsg[BUFSIZ];

	rmtgets(code, sizeof(code));
	if (*code == 'E' || *code == 'F') {
		rmtgets(emsg, sizeof(emsg));
		msg("%s: %s", cmd, emsg);
		errno = atoi(&code[1]);
		if (*code == 'F')
			rmtstate = TS_CLOSED;
		return (-1);
	}
	if (*code != 'A') {
		/* Kill trailing newline */
		cp = code + strlen(code);
		if (cp > code && *--cp == '\n')
			*cp = '\0';

		msg("Protocol to remote tape server botched (code \"%s\").\n",
		    code);
		rmtconnaborted(0);
	}
	return (atoi(code + 1));
}

int
rmtgetb(void)
{
	char c;

	if (read(rmtape, &c, 1) != 1)
		rmtconnaborted(0);
	return (c);
}

/* Get a line (guaranteed to have a trailing newline). */
void
rmtgets(char *line, int len)
{
	char *cp = line;

	while (len > 1) {
		*cp = rmtgetb();
		if (*cp == '\n') {
			cp[1] = '\0';
			return;
		}
		cp++;
		len--;
	}
	*cp = '\0';
	msg("Protocol to remote tape server botched.\n");
	msg("(rmtgets got \"%s\").\n", line);
	rmtconnaborted(0);
}
