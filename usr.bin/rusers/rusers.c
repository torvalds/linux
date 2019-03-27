/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1993, John Brezak
 * All rights reserved.
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

#include <sys/cdefs.h>

__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/socket.h>

#include <rpc/rpc.h>
#include <rpc/pmap_clnt.h>
#include <rpcsvc/rnusers.h>

#include <arpa/inet.h>

#include <err.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <timeconv.h>
#include <unistd.h>

#define MAX_INT		0x7fffffff
#define HOST_WIDTH	20
#define LINE_WIDTH	15

static int longopt;
static int allopt;

static struct host_list {
	struct	host_list *next;
	struct	in_addr addr;
} *hosts;

static int
search_host(struct in_addr addr)
{
	struct host_list *hp;

	if (hosts == NULL)
		return (0);

	for (hp = hosts; hp != NULL; hp = hp->next) {
		if (hp->addr.s_addr == addr.s_addr)
			return (1);
	}
	return (0);
}

static void
remember_host(struct in_addr addr)
{
	struct host_list *hp;

	if ((hp = (struct host_list *)malloc(sizeof(struct host_list))) == NULL)
		errx(1, "no memory");
	hp->addr.s_addr = addr.s_addr;
	hp->next = hosts;
	hosts = hp;
}

static int
rusers_reply(void *replyp, struct sockaddr_in *raddrp)
{
	unsigned int x;
	int idle;
	char date[32], idle_time[64], remote[64];
	struct hostent *hp;
	utmpidlearr *up, u;
	char *host;
	int days, hours, minutes, seconds;

	up = &u;
	memcpy(up, replyp, sizeof(*up));
	if (search_host(raddrp->sin_addr))
		return (0);

	if (!allopt && up->utmpidlearr_len == 0)
		return (0);

	hp = gethostbyaddr((char *)&raddrp->sin_addr.s_addr,
	    sizeof(struct in_addr), AF_INET);
	if (hp != NULL)
		host = hp->h_name;
	else
		host = inet_ntoa(raddrp->sin_addr);

	if (!longopt)
		printf("%-*s ", HOST_WIDTH, host);

	for (x = 0; x < up->utmpidlearr_len; x++) {
		time_t t = _int_to_time(up->utmpidlearr_val[x].ui_utmp.ut_time);
		strncpy(date, &(ctime(&t)[4]), sizeof(date) - 1);

		idle = up->utmpidlearr_val[x].ui_idle;
		sprintf(idle_time, "  :%02d", idle);
		if (idle == MAX_INT)
			strcpy(idle_time, "??");
		else if (idle == 0)
			strcpy(idle_time, "");
		else {
			seconds = idle;
			days = seconds / (60 * 60 * 24);
			seconds %= (60 * 60 * 24);
			hours = seconds / (60 * 60);
			seconds %= (60 * 60);
			minutes = seconds / 60;
			seconds %= 60;
			if (idle > 60)
				sprintf(idle_time, "%d:%02d", minutes, seconds);
			if (idle >= (60 * 60))
				sprintf(idle_time, "%d:%02d:%02d",
				    hours, minutes, seconds);
			if (idle >= (24 * 60 * 60))
				sprintf(idle_time, "%d days, %d:%02d:%02d",
				    days, hours, minutes, seconds);
		}

		strncpy(remote, up->utmpidlearr_val[x].ui_utmp.ut_host,
		    sizeof(remote) - 1);
		if (strlen(remote) != 0)
			sprintf(remote, "(%.16s)",
			    up->utmpidlearr_val[x].ui_utmp.ut_host);

		if (longopt)
			printf("%-8.8s %*s:%-*.*s %-12.12s  %6s %.18s\n",
			    up->utmpidlearr_val[x].ui_utmp.ut_name,
			    HOST_WIDTH, host, LINE_WIDTH, LINE_WIDTH,
			    up->utmpidlearr_val[x].ui_utmp.ut_line, date,
			    idle_time, remote );
		else
			printf("%s ",
			    up->utmpidlearr_val[x].ui_utmp.ut_name);
	}
	if (!longopt)
		putchar('\n');

	remember_host(raddrp->sin_addr);
	return (0);
}

static void
onehost(char *host)
{
	utmpidlearr up;
	CLIENT *rusers_clnt;
	struct sockaddr_in addr;
	struct hostent *hp;
	struct timeval tv;

	hp = gethostbyname(host);
	if (hp == NULL)
		errx(1, "unknown host \"%s\"", host);

	rusers_clnt = clnt_create(host, RUSERSPROG, RUSERSVERS_IDLE, "udp");
	if (rusers_clnt == NULL)
		errx(1, "%s", clnt_spcreateerror(""));

	memset(&up, 0, sizeof(up));
	tv.tv_sec = 15;	/* XXX ?? */
	tv.tv_usec = 0;
	if (clnt_call(rusers_clnt, RUSERSPROC_NAMES, (xdrproc_t)xdr_void, NULL,
	    (xdrproc_t)xdr_utmpidlearr, &up, tv) != RPC_SUCCESS)
		errx(1, "%s", clnt_sperror(rusers_clnt, ""));
	memcpy(&addr.sin_addr.s_addr, hp->h_addr, sizeof(addr.sin_addr.s_addr));
	rusers_reply(&up, &addr);
	clnt_destroy(rusers_clnt);
}

static void
allhosts(void)
{
	utmpidlearr up;
	enum clnt_stat clnt_stat;

	memset(&up, 0, sizeof(up));
	clnt_stat = clnt_broadcast(RUSERSPROG, RUSERSVERS_IDLE,
	    RUSERSPROC_NAMES, (xdrproc_t)xdr_void, NULL,
	    (xdrproc_t)xdr_utmpidlearr, (char *)&up,
	    (resultproc_t)rusers_reply);
	if (clnt_stat != RPC_SUCCESS && clnt_stat != RPC_TIMEDOUT)
		errx(1, "%s", clnt_sperrno(clnt_stat));
}

static void
usage(void)
{

	fprintf(stderr, "usage: rusers [-al] [host ...]\n");
	exit(1);
}

int
main(int argc, char *argv[])
{
	int ch;

	while ((ch = getopt(argc, argv, "al")) != -1)
		switch (ch) {
		case 'a':
			allopt++;
			break;
		case 'l':
			longopt++;
			break;
		default:
			usage();
			/* NOTREACHED */
		}

	setlinebuf(stdout);
	if (argc == optind)
		allhosts();
	else {
		for (; optind < argc; optind++)
			(void)onehost(argv[optind]);
	}
	exit(0);
}
