/*	$OpenBSD: cmds.c,v 1.26 2006/06/06 23:24:52 deraadt Exp $	*/
/*	$NetBSD: cmds.c,v 1.7 1997/02/11 09:24:03 mrg Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1983, 1993
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#ifndef lint
#if 0
static char sccsid[] = "@(#)cmds.c	8.1 (Berkeley) 6/6/93";
static const char rcsid[] = "$OpenBSD: cmds.c,v 1.26 2006/06/06 23:24:52 deraadt Exp $";
#endif
#endif /* not lint */

#include "tip.h"
#include "pathnames.h"

#include <vis.h>

/*
 * tip
 *
 * miscellaneous commands
 */

int	quant[] = { 60, 60, 24 };

char	null = '\0';
char	*sep[] = { "second", "minute", "hour" };
static char *argv[10];		/* argument vector for take and put */

static void	transfer(char *, int, char *);
static void	stopsnd(int);	/* SIGINT handler during file transfers */
static void	intcopy(int);	/* interrupt routine for file transfers */
static void	transmit(FILE *, char *, char *);
static void	send(int);
static void	execute(char *);
static int	args(char *, char **, int);
static void	prtime(char *, time_t);
static void	tandem(char *);
static void	hardwareflow(char *);
void		linedisc(char *);
static int	anyof(char *, char *);

/*
 * FTP - remote ==> local
 *  get a file from the remote host
 */
void
getfl(int c)
{
	char buf[256], *cp;

	putchar(c);
	/*
	 * get the UNIX receiving file's name
	 */
	if (prompt("Local file name? ", copyname, sizeof(copyname)))
		return;
	cp = expand(copyname);
	if ((sfd = creat(cp, 0666)) < 0) {
		printf("\r\n%s: cannot creat\r\n", copyname);
		return;
	}

	/*
	 * collect parameters
	 */
	if (prompt("List command for remote system? ", buf, sizeof(buf))) {
		unlink(copyname);
		return;
	}
	transfer(buf, sfd, value(EOFREAD));
}

/*
 * Cu-like take command
 */
void
cu_take(int c)
{
	int fd, argc;
	char line[BUFSIZ], *cp;

	if (prompt("[take] ", copyname, sizeof(copyname)))
		return;
	if ((argc = args(copyname, argv, sizeof(argv)/sizeof(argv[0]))) < 1 ||
	    argc > 2) {
		printf("usage: <take> from [to]\r\n");
		return;
	}
	if (argc == 1)
		argv[1] = argv[0];
	cp = expand(argv[1]);
	if ((fd = creat(cp, 0666)) < 0) {
		printf("\r\n%s: cannot create\r\n", argv[1]);
		return;
	}
	(void)snprintf(line, sizeof(line), "cat %s;echo ''|tr '\\012' '\\01'", argv[0]);
	transfer(line, fd, "\01");
}

static	jmp_buf intbuf;

/*
 * Bulk transfer routine --
 *  used by getfl(), cu_take(), and pipefile()
 */
static void
transfer(char *buf, int fd, char *eofchars)
{
	int ct, eof;
	char c, buffer[BUFSIZ];
	char *p = buffer;
	size_t cnt;
	time_t start;
	sig_t f;
	char r;

	if (number(value(FRAMESIZE)) > BUFSIZ || number(value(FRAMESIZE)) < 1) {
		printf("framesize must be >= 1 and <= %d\r\n", BUFSIZ);
		close(fd);
		return;
	}

	parwrite(FD, buf, size(buf));
	quit = 0;
	kill(tipout_pid, SIGIOT);
	read(repdes[0], (char *)&ccc, 1);  /* Wait until read process stops */

	/*
	 * finish command
	 */
	r = '\r';
	parwrite(FD, &r, 1);
	do
		read(FD, &c, 1);
	while ((c&STRIP_PAR) != '\n');
	tcsetattr(0, TCSAFLUSH, &defchars);

	(void) setjmp(intbuf);
	f = signal(SIGINT, intcopy);
	start = time(0);
	for (ct = 0; !quit;) {
		eof = read(FD, &c, 1) <= 0;
		c &= STRIP_PAR;
		if (quit)
			continue;
		if (eof || any(c, eofchars))
			break;
		if (c == 0)
			continue;	/* ignore nulls */
		if (c == '\r')
			continue;
		*p++ = c;

		if (c == '\n' && boolean(value(VERBOSE)))
			printf("\r%d", ++ct);
		if ((cnt = (p-buffer)) == (size_t)number(value(FRAMESIZE))) {
			if ((size_t)write(fd, buffer, cnt) != cnt) {
				printf("\r\nwrite error\r\n");
				quit = 1;
			}
			p = buffer;
		}
	}
	if ((cnt = (p-buffer)))
		if ((size_t)write(fd, buffer, cnt) != cnt)
			printf("\r\nwrite error\r\n");

	if (boolean(value(VERBOSE)))
		prtime(" lines transferred in ", time(0)-start);
	tcsetattr(0, TCSAFLUSH, &term);
	write(fildes[1], (char *)&ccc, 1);
	signal(SIGINT, f);
	close(fd);
}

/*
 * FTP - remote ==> local process
 *   send remote input to local process via pipe
 */
/*ARGSUSED*/
void
pipefile(int c)
{
	int pdes[2];
	char buf[256];
	int status, p;
	pid_t cpid;

	if (prompt("Local command? ", buf, sizeof(buf)))
		return;

	if (pipe(pdes)) {
		printf("can't establish pipe\r\n");
		return;
	}

	if ((cpid = fork()) < 0) {
		printf("can't fork!\r\n");
		return;
	} else if (cpid) {
		if (prompt("List command for remote system? ", buf, sizeof(buf))) {
			close(pdes[0]), close(pdes[1]);
			kill (cpid, SIGKILL);
		} else {
			close(pdes[0]);
			signal(SIGPIPE, intcopy);
			transfer(buf, pdes[1], value(EOFREAD));
			signal(SIGPIPE, SIG_DFL);
			while ((p = wait(&status)) > 0 && p != cpid)
				;
		}
	} else {
		int f;

		dup2(pdes[0], 0);
		close(pdes[0]);
		for (f = 3; f < 20; f++)
			close(f);
		execute(buf);
		printf("can't execl!\r\n");
		exit(0);
	}
}

/*
 * Interrupt service routine for FTP
 */
/*ARGSUSED*/
static void
stopsnd(int signo)
{
	stop = 1;
	signal(SIGINT, SIG_IGN);
}

/*
 * FTP - local ==> remote
 *  send local file to remote host
 *  terminate transmission with pseudo EOF sequence
 */
void
sendfile(int c)
{
	FILE *fp;
	char *fnamex;

	putchar(c);
	/*
	 * get file name
	 */
	if (prompt("Local file name? ", fname, sizeof(fname)))
		return;

	/*
	 * look up file
	 */
	fnamex = expand(fname);
	if ((fp = fopen(fnamex, "r")) == NULL) {
		printf("%s: cannot open\r\n", fname);
		return;
	}
	transmit(fp, value(EOFWRITE), NULL);
	if (!boolean(value(ECHOCHECK)))
		tcdrain(FD);
}

/*
 * Bulk transfer routine to remote host --
 *   used by sendfile() and cu_put()
 */
static void
transmit(FILE *fp, char *eofchars, char *command)
{
	char *pc, lastc;
	int c, ccount, lcount;
	time_t start_t, stop_t;
	sig_t f;

	kill(tipout_pid, SIGIOT);	/* put TIPOUT into a wait state */
	stop = 0;
	f = signal(SIGINT, stopsnd);
	tcsetattr(0, TCSAFLUSH, &defchars);
	read(repdes[0], (char *)&ccc, 1);
	if (command != NULL) {
		for (pc = command; *pc; pc++)
			send(*pc);
		if (boolean(value(ECHOCHECK)))
			read(FD, (char *)&c, 1);	/* trailing \n */
		else {
			tcdrain(FD);
			sleep(5); /* wait for remote stty to take effect */
		}
	}
	lcount = 0;
	lastc = '\0';
	start_t = time(0);
	while (1) {
		ccount = 0;
		do {
			c = getc(fp);
			if (stop)
				goto out;
			if (c == EOF)
				goto out;
			if (c == 0177 && !boolean(value(RAWFTP)))
				continue;
			lastc = c;
			if (c < 040) {
				if (c == '\n') {
					if (!boolean(value(RAWFTP)))
						c = '\r';
				} else if (c == '\t') {
					if (!boolean(value(RAWFTP))) {
						if (boolean(value(TABEXPAND))) {
							send(' ');
							while ((++ccount % 8) != 0)
								send(' ');
							continue;
						}
					}
				} else
					if (!boolean(value(RAWFTP)))
						continue;
			}
			send(c);
		} while (c != '\r' && !boolean(value(RAWFTP)));
		if (boolean(value(VERBOSE)))
			printf("\r%d", ++lcount);
		if (boolean(value(ECHOCHECK))) {
			timedout = 0;
			alarm((unsigned int)lvalue(ETIMEOUT));
			do {	/* wait for prompt */
				read(FD, (char *)&c, 1);
				if (timedout || stop) {
					if (timedout)
						printf("\r\ntimed out at eol\r\n");
					alarm(0);
					goto out;
				}
			} while ((c&STRIP_PAR) != character(value(PROMPT)));
			alarm(0);
		}
	}
out:
	if (lastc != '\n' && !boolean(value(RAWFTP)))
		send('\r');
	if (eofchars) {
		for (pc = eofchars; *pc; pc++)
			send(*pc);
	}
	stop_t = time(0);
	fclose(fp);
	signal(SIGINT, f);
	if (boolean(value(VERBOSE))) {
		if (boolean(value(RAWFTP)))
			prtime(" chars transferred in ", stop_t-start_t);
		else
			prtime(" lines transferred in ", stop_t-start_t);
	}
	write(fildes[1], (char *)&ccc, 1);
	tcsetattr(0, TCSAFLUSH, &term);
}

/*
 * Cu-like put command
 */
/*ARGSUSED*/
void
cu_put(int c)
{
	FILE *fp;
	char line[BUFSIZ];
	int argc;
	char *copynamex;

	if (prompt("[put] ", copyname, sizeof(copyname)))
		return;
	if ((argc = args(copyname, argv, sizeof(argv)/sizeof(argv[0]))) < 1 ||
	    argc > 2) {
		printf("usage: <put> from [to]\r\n");
		return;
	}
	if (argc == 1)
		argv[1] = argv[0];
	copynamex = expand(argv[0]);
	if ((fp = fopen(copynamex, "r")) == NULL) {
		printf("%s: cannot open\r\n", copynamex);
		return;
	}
	if (boolean(value(ECHOCHECK)))
		(void)snprintf(line, sizeof(line), "cat>%s\r", argv[1]);
	else
		(void)snprintf(line, sizeof(line),
		    "stty -echo;cat>%s;stty echo\r", argv[1]);
	transmit(fp, "\04", line);
}

/*
 * FTP - send single character
 *  wait for echo & handle timeout
 */
static void
send(int c)
{
	char cc;
	int retry = 0;

	cc = c;
	parwrite(FD, &cc, 1);
	if (number(value(CDELAY)) > 0 && c != '\r')
		usleep(number(value(CDELAY)));
	if (!boolean(value(ECHOCHECK))) {
		if (number(value(LDELAY)) > 0 && c == '\r')
			usleep(number(value(LDELAY)));
		return;
	}
tryagain:
	timedout = 0;
	alarm((unsigned int)lvalue(ETIMEOUT));
	read(FD, &cc, 1);
	alarm(0);
	if (timedout) {
		printf("\r\ntimeout error (%s)\r\n", ctrl(c));
		if (retry++ > 3)
			return;
		parwrite(FD, &null, 1); /* poke it */
		goto tryagain;
	}
}

/*ARGSUSED*/
void
timeout(int signo)
{
	signal(SIGALRM, timeout);
	timedout = 1;
}

/*
 * Stolen from consh() -- puts a remote file on the output of a local command.
 *	Identical to consh() except for where stdout goes.
 */
void
pipeout(int c)
{
	char buf[256];
	int status, p;
	pid_t cpid;
	time_t start = time(NULL);

	putchar(c);
	if (prompt("Local command? ", buf, sizeof(buf)))
		return;
	kill(tipout_pid, SIGIOT);	/* put TIPOUT into a wait state */
	signal(SIGINT, SIG_IGN);
	signal(SIGQUIT, SIG_IGN);
	tcsetattr(0, TCSAFLUSH, &defchars);
	read(repdes[0], (char *)&ccc, 1);
	/*
	 * Set up file descriptors in the child and
	 *  let it go...
	 */
	if ((cpid = fork()) < 0)
		printf("can't fork!\r\n");
	else if (cpid) {
		start = time(NULL);
		while ((p = wait(&status)) > 0 && p != cpid)
			;
	} else {
		int i;

		dup2(FD, 1);
		for (i = 3; i < 20; i++)
			close(i);
		signal(SIGINT, SIG_DFL);
		signal(SIGQUIT, SIG_DFL);
		execute(buf);
		printf("can't find `%s'\r\n", buf);
		exit(0);
	}
	if (boolean(value(VERBOSE)))
		prtime("away for ", time(0)-start);
	write(fildes[1], (char *)&ccc, 1);
	tcsetattr(0, TCSAFLUSH, &term);
	signal(SIGINT, SIG_DFL);
	signal(SIGQUIT, SIG_DFL);
}

#ifdef CONNECT
/*
 * Fork a program with:
 *  0 <-> remote tty in
 *  1 <-> remote tty out
 *  2 <-> local tty stderr
 */
void
consh(int c)
{
	char buf[256];
	int status, p;
	pid_t cpid;
	time_t start = time(NULL);

	putchar(c);
	if (prompt("Local command? ", buf, sizeof(buf)))
		return;
	kill(tipout_pid, SIGIOT);	/* put TIPOUT into a wait state */
	signal(SIGINT, SIG_IGN);
	signal(SIGQUIT, SIG_IGN);
	tcsetattr(0, TCSAFLUSH, &defchars);
	read(repdes[0], (char *)&ccc, 1);
	/*
	 * Set up file descriptors in the child and
	 *  let it go...
	 */
	if ((cpid = fork()) < 0)
		printf("can't fork!\r\n");
	else if (cpid) {
		start = time(0);
		while ((p = wait(&status)) > 0 && p != cpid)
			;
	} else {
		int i;

		dup2(FD, 0);
		dup2(3, 1);
		for (i = 3; i < 20; i++)
			close(i);
		signal(SIGINT, SIG_DFL);
		signal(SIGQUIT, SIG_DFL);
		execute(buf);
		printf("can't find `%s'\r\n", buf);
		exit(0);
	}
	if (boolean(value(VERBOSE)))
		prtime("away for ", time(0)-start);
	write(fildes[1], (char *)&ccc, 1);
	tcsetattr(0, TCSAFLUSH, &term);
	signal(SIGINT, SIG_DFL);
	signal(SIGQUIT, SIG_DFL);
}
#endif

/*
 * Escape to local shell
 */
/*ARGSUSED*/
void
shell(int c)
{
	int status;
	char *cp;
	pid_t shpid;

	printf("[sh]\r\n");
	signal(SIGINT, SIG_IGN);
	signal(SIGQUIT, SIG_IGN);
	unraw();
	if ((shpid = fork())) {
		while (shpid != wait(&status));
		raw();
		printf("\r\n!\r\n");
		signal(SIGINT, SIG_DFL);
		signal(SIGQUIT, SIG_DFL);
		return;
	} else {
		signal(SIGQUIT, SIG_DFL);
		signal(SIGINT, SIG_DFL);
		if ((cp = strrchr(value(SHELL), '/')) == NULL)
			cp = value(SHELL);
		else
			cp++;
		shell_uid();
		execl(value(SHELL), cp, (char *)NULL);
		printf("\r\ncan't execl!\r\n");
		exit(1);
	}
}

/*
 * TIPIN portion of scripting
 *   initiate the conversation with TIPOUT
 */
void
setscript(void)
{
	char c;

	/*
	 * enable TIPOUT side for dialogue
	 */
	kill(tipout_pid, SIGEMT);
	if (boolean(value(SCRIPT)))
		write(fildes[1], value(RECORD), size(value(RECORD)));
	write(fildes[1], "\n", 1);
	/*
	 * wait for TIPOUT to finish
	 */
	read(repdes[0], &c, 1);
	if (c == 'n')
		printf("can't create %s\r\n", value(RECORD));
}

/*
 * Change current working directory of
 *   local portion of tip
 */
/*ARGSUSED*/
void
chdirectory(int c)
{
	char dirname[PATH_MAX];
	char *cp = dirname;

	if (prompt("[cd] ", dirname, sizeof(dirname))) {
		if (stoprompt)
			return;
		cp = value(HOME);
	}
	if (chdir(cp) < 0)
		printf("%s: bad directory\r\n", cp);
	printf("!\r\n");
}

void
tipabort(char *msg)
{

	signal(SIGTERM, SIG_IGN);
	kill(tipout_pid, SIGTERM);
	disconnect(msg);
	if (msg != NOSTR)
		printf("\r\n%s", msg);
	printf("\r\n[EOT]\r\n");
	daemon_uid();
	(void)uu_unlock(uucplock);
	unraw();
	unexcl();
	exit(0);
}

/*ARGSUSED*/
void
finish(int c)
{
	char *dismsg;

	if ((dismsg = value(DISCONNECT)) != NOSTR) {
		write(FD, dismsg, strlen(dismsg));
		sleep(5);
	}
	tipabort(NOSTR);
}

/*ARGSUSED*/
static void
intcopy(int signo)
{
	raw();
	quit = 1;
	longjmp(intbuf, 1);
}

static void
execute(char *s)
{
	char *cp;

	if ((cp = strrchr(value(SHELL), '/')) == NULL)
		cp = value(SHELL);
	else
		cp++;
	shell_uid();
	execl(value(SHELL), cp, "-c", s, (char *)NULL);
}

static int
args(char *buf, char *a[], int num)
{
	char *p = buf, *start;
	char **parg = a;
	int n = 0;

	do {
		while (*p && (*p == ' ' || *p == '\t'))
			p++;
		start = p;
		if (*p)
			*parg = p;
		while (*p && (*p != ' ' && *p != '\t'))
			p++;
		if (p != start)
			parg++, n++;
		if (*p)
			*p++ = '\0';
	} while (*p && n < num);

	return(n);
}

static void
prtime(char *s, time_t a)
{
	int i;
	int nums[3];

	for (i = 0; i < 3; i++) {
		nums[i] = (int)(a % quant[i]);
		a /= quant[i];
	}
	printf("%s", s);
	while (--i >= 0)
		if (nums[i] || (i == 0 && nums[1] == 0 && nums[2] == 0))
			printf("%d %s%c ", nums[i], sep[i],
				nums[i] == 1 ? '\0' : 's');
	printf("\r\n!\r\n");
}

/*ARGSUSED*/
void
variable(int c)
{
	char	buf[256];

	if (prompt("[set] ", buf, sizeof(buf)))
		return;
	vlex(buf);
	if (vtable[BEAUTIFY].v_access&CHANGED) {
		vtable[BEAUTIFY].v_access &= ~CHANGED;
		kill(tipout_pid, SIGSYS);
	}
	if (vtable[SCRIPT].v_access&CHANGED) {
		vtable[SCRIPT].v_access &= ~CHANGED;
		setscript();
		/*
		 * So that "set record=blah script" doesn't
		 *  cause two transactions to occur.
		 */
		if (vtable[RECORD].v_access&CHANGED)
			vtable[RECORD].v_access &= ~CHANGED;
	}
	if (vtable[RECORD].v_access&CHANGED) {
		vtable[RECORD].v_access &= ~CHANGED;
		if (boolean(value(SCRIPT)))
			setscript();
	}
	if (vtable[TAND].v_access&CHANGED) {
		vtable[TAND].v_access &= ~CHANGED;
		if (boolean(value(TAND)))
			tandem("on");
		else
			tandem("off");
	}
	if (vtable[LECHO].v_access&CHANGED) {
		vtable[LECHO].v_access &= ~CHANGED;
		HD = boolean(value(LECHO));
	}
	if (vtable[PARITY].v_access&CHANGED) {
		vtable[PARITY].v_access &= ~CHANGED;
		setparity(NOSTR);
	}
	if (vtable[HARDWAREFLOW].v_access&CHANGED) {
		vtable[HARDWAREFLOW].v_access &= ~CHANGED;
		if (boolean(value(HARDWAREFLOW)))
			hardwareflow("on");
		else
			hardwareflow("off");
	}
	if (vtable[LINEDISC].v_access&CHANGED) {
		vtable[LINEDISC].v_access &= ~CHANGED;
		linedisc(NOSTR);
	}
}

/*ARGSUSED*/
void
listvariables(int c)
{
	value_t *p;
	char *buf;
	char charbuf[5];	/* for vis(3), 4 chars for encoding, plus nul */

	puts("v\r");
	for (p = vtable; p->v_name; p++) {
		fputs(p->v_name, stdout);
		switch (p->v_type&TMASK) {
		case STRING:
			if (p->v_value) {
				buf = malloc(4*strlen(p->v_value) + 1);
				if (buf == NULL) {
					fprintf(stderr, "Unable to malloc()\n");
					abort();
				}
				strvis(buf, p->v_value, VIS_WHITE);
				printf(" %s", buf);
				free(buf);
			}
			putchar('\r');
			putchar('\n');
			break;
		case NUMBER:
			printf(" %ld\r\n", number(p->v_value));
			break;
		case BOOL:
			printf(" %s\r\n",
			    !boolean(p->v_value) ? "false" : "true");
			break;
		case CHAR:
			vis(charbuf, character(p->v_value), VIS_WHITE, 0);
			printf(" %s\r\n", charbuf);
			break;
		}
	}
}

/*
 * Turn tandem mode on or off for remote tty.
 */
static void
tandem(char *option)
{
	struct termios	rmtty;

	tcgetattr(FD, &rmtty);
	if (strcmp(option, "on") == 0) {
		rmtty.c_iflag |= IXOFF;
		term.c_iflag |= IXOFF;
	} else {
		rmtty.c_iflag &= ~IXOFF;
		term.c_iflag &= ~IXOFF;
	}
	tcsetattr(FD, TCSADRAIN, &rmtty);
	tcsetattr(0, TCSADRAIN, &term);
}

/*
 * Turn hardware flow control on or off for remote tty.
 */
static void
hardwareflow(char *option)
{
	struct termios	rmtty;

	tcgetattr(FD, &rmtty);
	if (strcmp(option, "on") == 0)
		rmtty.c_iflag |= CRTSCTS;
	else
		rmtty.c_iflag &= ~CRTSCTS;
	tcsetattr(FD, TCSADRAIN, &rmtty);
}

/*
 * Change line discipline to the specified one.
 */
void
linedisc(char *option)
{
	int ld = (int)(intptr_t)value(LINEDISC);

	ioctl(FD, TIOCSETD, &ld);
}

/*
 * Send a break.
 */
/*ARGSUSED*/
void
genbrk(int c)
{
	ioctl(FD, TIOCSBRK, NULL);
	sleep(1);
	ioctl(FD, TIOCCBRK, NULL);
}

/*
 * Suspend tip
 */
void
suspend(int c)
{
	unraw();
	kill(c == CTRL('y') ? getpid() : 0, SIGTSTP);
	raw();
}

/*
 *	expand a file name if it includes shell meta characters
 */
char *
expand(char name[])
{
	static char xname[BUFSIZ];
	char cmdbuf[BUFSIZ];
	int l;
	char *cp, *Shell;
	int s, pivec[2];
	pid_t pid;

	if (!anyof(name, "~{[*?$`'\"\\"))
		return(name);
	/* sigint = signal(SIGINT, SIG_IGN); */
	if (pipe(pivec) < 0) {
		perror("pipe");
		/* signal(SIGINT, sigint) */
		return(name);
	}
	(void)snprintf(cmdbuf, sizeof(cmdbuf), "echo %s", name);
	if ((pid = vfork()) == 0) {
		Shell = value(SHELL);
		if (Shell == NOSTR)
			Shell = _PATH_BSHELL;
		close(pivec[0]);
		close(1);
		dup(pivec[1]);
		close(pivec[1]);
		close(2);
		shell_uid();
		execl(Shell, Shell, "-c", cmdbuf, (char *)NULL);
		_exit(1);
	}
	if (pid == -1) {
		perror("fork");
		close(pivec[0]);
		close(pivec[1]);
		return(NOSTR);
	}
	close(pivec[1]);
	l = read(pivec[0], xname, BUFSIZ);
	close(pivec[0]);
	while (wait(&s) != pid);
		;
	s &= 0377;
	if (s != 0 && s != SIGPIPE) {
		fprintf(stderr, "\"Echo\" failed\n");
		return(NOSTR);
	}
	if (l < 0) {
		perror("read");
		return(NOSTR);
	}
	if (l == 0) {
		fprintf(stderr, "\"%s\": No match\n", name);
		return(NOSTR);
	}
	if (l == BUFSIZ) {
		fprintf(stderr, "Buffer overflow expanding \"%s\"\n", name);
		return(NOSTR);
	}
	xname[l] = 0;
	for (cp = &xname[l-1]; *cp == '\n' && cp > xname; cp--)
		;
	*++cp = '\0';
	return(xname);
}

/*
 * Are any of the characters in the two strings the same?
 */
static int
anyof(char *s1, char *s2)
{
	int c;

	while ((c = *s1++))
		if (any(c, s2))
			return(1);
	return(0);
}
