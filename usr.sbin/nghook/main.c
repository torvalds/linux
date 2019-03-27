/*
 * main.c
 *
 * Copyright (c) 1996-1999 Whistle Communications, Inc.
 * All rights reserved.
 * 
 * Subject to the following obligations and disclaimer of warranty, use and
 * redistribution of this software, in source or object code forms, with or
 * without modifications are expressly permitted by Whistle Communications;
 * provided, however, that:
 * 1. Any and all reproductions of the source or object code must include the
 *    copyright notice above and the following disclaimer of warranties; and
 * 2. No rights are granted, in any manner or form, to use Whistle
 *    Communications, Inc. trademarks, including the mark "WHISTLE
 *    COMMUNICATIONS" on advertising, endorsements, or otherwise except as
 *    such appears in the above copyright notice or in the software.
 * 
 * THIS SOFTWARE IS BEING PROVIDED BY WHISTLE COMMUNICATIONS "AS IS", AND
 * TO THE MAXIMUM EXTENT PERMITTED BY LAW, WHISTLE COMMUNICATIONS MAKES NO
 * REPRESENTATIONS OR WARRANTIES, EXPRESS OR IMPLIED, REGARDING THIS SOFTWARE,
 * INCLUDING WITHOUT LIMITATION, ANY AND ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, OR NON-INFRINGEMENT.
 * WHISTLE COMMUNICATIONS DOES NOT WARRANT, GUARANTEE, OR MAKE ANY
 * REPRESENTATIONS REGARDING THE USE OF, OR THE RESULTS OF THE USE OF THIS
 * SOFTWARE IN TERMS OF ITS CORRECTNESS, ACCURACY, RELIABILITY OR OTHERWISE.
 * IN NO EVENT SHALL WHISTLE COMMUNICATIONS BE LIABLE FOR ANY DAMAGES
 * RESULTING FROM OR ARISING OUT OF ANY USE OF THIS SOFTWARE, INCLUDING
 * WITHOUT LIMITATION, ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
 * PUNITIVE, OR CONSEQUENTIAL DAMAGES, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES, LOSS OF USE, DATA OR PROFITS, HOWEVER CAUSED AND UNDER ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF WHISTLE COMMUNICATIONS IS ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 *
 * $Whistle: main.c,v 1.9 1999/01/20 00:26:26 archie Exp $
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <sysexits.h>
#include <errno.h>
#include <err.h>
#include <stringlist.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>

#include <netgraph.h>

#define DEFAULT_HOOKNAME	"debug"
#define NG_SOCK_HOOK_NAME	"hook"

#define BUF_SIZE		(64 * 1024)

static void	WriteAscii(u_char * buf, int len);
static void	Usage(void);
static void	send_msgs(int, const char *);

static int outfd = STDOUT_FILENO;
static int infd = STDIN_FILENO;

static StringList *msgs;

/*
 * main()
 */
int
main(int ac, char *av[])
{
	struct ngm_connect ngc;
	const char *path = NULL;
	const char *hook = DEFAULT_HOOKNAME;
	int     csock, dsock;
	int     asciiFlag = 0;
	int     loopFlag = 0;
	int	noInput = 0;
	int	execFlag = 0;
	int	ch;

	if ((msgs = sl_init()) == NULL)
		err(EX_OSERR, NULL);

	/* Parse flags */
	while ((ch = getopt(ac, av, "aedlm:nsS")) != -1) {
		switch (ch) {
		case 'a':
			asciiFlag = 1;
			break;
		case 'd':
			NgSetDebug(NgSetDebug(-1) + 1);
			break;
		case 'e':
			execFlag = 1;
			break;
		case 'l':
			loopFlag = 1;
			break;
		case 'n':
			noInput = 1;
			break;
		case 'm':
			if (sl_add(msgs, optarg) == -1)
				err(EX_OSERR, NULL);
			break;
		case 's':
			outfd = STDIN_FILENO;
			break;
		case 'S':
			infd = STDOUT_FILENO;
			break;
		case '?':
		default:
			Usage();
		}
	}
	ac -= optind;
	av += optind;

	if (execFlag) {
		if (asciiFlag || loopFlag) {
			fprintf(stderr, "conflicting options\n");
			Usage();
		}
		if (ac < 3)
			Usage();
		path = av[0];
		hook = av[1];
		av += 2;
		ac -= 2;
	} else {
		/* Get params */
		switch (ac) {
		case 2:
			hook = av[1];
			/* FALLTHROUGH */
		case 1:
			path = av[0];
			break;
		default:
			Usage();
		}
	}

	/* Get sockets */
	if (NgMkSockNode(NULL, &csock, &dsock) < 0)
		errx(EX_OSERR, "can't get sockets");

	/* Connect socket node to specified node */
	snprintf(ngc.path, sizeof(ngc.path), "%s", path);
	snprintf(ngc.ourhook, sizeof(ngc.ourhook), NG_SOCK_HOOK_NAME);
	snprintf(ngc.peerhook, sizeof(ngc.peerhook), "%s", hook);

	if (NgSendMsg(csock, ".",
	    NGM_GENERIC_COOKIE, NGM_CONNECT, &ngc, sizeof(ngc)) < 0)
		errx(EX_OSERR, "can't connect to node");

	if (execFlag) {
		/* move dsock to fd 0 and 1 */
		(void)close(0);
		(void)close(1);
		if (!noInput)
			(void)dup2(dsock, 0);
		(void)dup2(dsock, 1);

		send_msgs(csock, path);

		/* try executing the program */
		(void)execv(av[0], av);
		err(EX_OSERR, "%s", av[0]);

	} else
		send_msgs(csock, path);

	/* Close standard input if not reading from it */
	if (noInput)
		fclose(stdin);

	/* Relay data */
	while (1) {
		fd_set  rfds;

		/* Setup bits */
		FD_ZERO(&rfds);
		if (!noInput)
			FD_SET(infd, &rfds);
		FD_SET(dsock, &rfds);

		/* Wait for something to happen */
		if (select(FD_SETSIZE, &rfds, NULL, NULL, NULL) < 0)
			err(EX_OSERR, "select");

		/* Check data from socket */
		if (FD_ISSET(dsock, &rfds)) {
			char    buf[BUF_SIZE];
			int     rl, wl;

			/* Read packet from socket */
			if ((rl = NgRecvData(dsock,
			    buf, sizeof(buf), NULL)) < 0)
				err(EX_OSERR, "read(hook)");
			if (rl == 0)
				errx(EX_OSERR, "read EOF from hook?!");

			/* Write packet to stdout */
			if (asciiFlag)
				WriteAscii((u_char *) buf, rl);
			else if ((wl = write(outfd, buf, rl)) != rl) {
				if (wl < 0) {
					err(EX_OSERR, "write(stdout)");
				} else {
					errx(EX_OSERR,
					    "stdout: read %d, wrote %d",
					    rl, wl);
				}
			}
			/* Loopback */
			if (loopFlag) {
				if (NgSendData(dsock, NG_SOCK_HOOK_NAME, buf, rl) < 0)
					err(EX_OSERR, "write(hook)");
			}
		}

		/* Check data from stdin */
		if (FD_ISSET(infd, &rfds)) {
			char    buf[BUF_SIZE];
			int     rl;

			/* Read packet from stdin */
			if ((rl = read(infd, buf, sizeof(buf))) < 0)
				err(EX_OSERR, "read(stdin)");
			if (rl == 0)
				errx(EX_OSERR, "EOF(stdin)");

			/* Write packet to socket */
			if (NgSendData(dsock, NG_SOCK_HOOK_NAME, buf, rl) < 0)
				err(EX_OSERR, "write(hook)");
		}
	}
}

/*
 * Dump data in hex and ASCII form
 */
static void
WriteAscii(u_char *buf, int len)
{
	char    ch, sbuf[100];
	int     k, count;

	for (count = 0; count < len; count += 16) {
		snprintf(sbuf, sizeof(sbuf), "%04x:  ", count);
		for (k = 0; k < 16; k++)
			if (count + k < len)
				snprintf(sbuf + strlen(sbuf),
				    sizeof(sbuf) - strlen(sbuf),
				    "%02x ", buf[count + k]);
			else
				snprintf(sbuf + strlen(sbuf),
				    sizeof(sbuf) - strlen(sbuf), "   ");
		snprintf(sbuf + strlen(sbuf), sizeof(sbuf) - strlen(sbuf), " ");
		for (k = 0; k < 16; k++)
			if (count + k < len) {
				ch = isprint(buf[count + k]) ?
				    buf[count + k] : '.';
				snprintf(sbuf + strlen(sbuf),
				    sizeof(sbuf) - strlen(sbuf), "%c", ch);
			} else
				snprintf(sbuf + strlen(sbuf),
				    sizeof(sbuf) - strlen(sbuf), " ");
		snprintf(sbuf + strlen(sbuf),
		    sizeof(sbuf) - strlen(sbuf), "\n");
		(void) write(outfd, sbuf, strlen(sbuf));
	}
	ch = '\n';
	write(outfd, &ch, 1);
}

/*
 * Display usage and exit
 */
static void
Usage(void)
{
	fprintf(stderr, "usage: nghook [-adlnsS] path [hookname]\n");
	fprintf(stderr, "   or: nghook -e [-n] [-m msg]* path hookname prog "
	    "[args...]\n");
	exit(EX_USAGE);
}

/*
 * Send the messages to the node
 */
static void
send_msgs(int cs, const char *path)
{
	u_int	i;

	for (i = 0; i < msgs->sl_cur; i++)
		if (NgSendAsciiMsg(cs, path, "%s", msgs->sl_str[i]) == -1)
			err(EX_OSERR, "sending message '%s'", msgs->sl_str[i]);
}
