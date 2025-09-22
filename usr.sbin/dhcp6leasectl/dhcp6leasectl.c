/*	$OpenBSD: dhcp6leasectl.c,v 1.6 2024/11/21 13:38:14 claudio Exp $	*/

/*
 * Copyright (c) 2021, 2024 Florian Obser <florian@openbsd.org>
 * Copyright (c) 2005 Claudio Jeker <claudio@openbsd.org>
 * Copyright (c) 2004, 2005 Esben Norby <norby@openbsd.org>
 * Copyright (c) 2003 Henning Brauer <henning@openbsd.org>
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

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>

#include <arpa/inet.h>

#include <net/if.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <err.h>
#include <errno.h>
#include <event.h>
#include <imsg.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "dhcp6leased.h"

__dead void	 usage(void);
void		 show_interface_msg(struct ctl_engine_info *);

struct imsgbuf	*ibuf;

__dead void
usage(void)
{
	extern char *__progname;

	fprintf(stderr, "usage: %s [-l] [-s socket] [-w maxwait] interface\n",
	    __progname);
	exit(1);
}

int
main(int argc, char *argv[])
{
	struct sockaddr_un	 sun;
	struct imsg		 imsg;
	struct ctl_engine_info	*cei;
	int			 ctl_sock;
	int			 n, lFlag = 0, maxwait_set = 0, didot = 0;
	int			 ch, if_index = 0, maxwait = 10, bound = 0;
	char			*sockname;
	const char		*errstr;

	sockname = _PATH_CTRL_SOCKET;
	while ((ch = getopt(argc, argv, "ls:w:")) != -1) {
		switch (ch) {
		case 'l':
			lFlag = 1;
			break;
		case 's':
			sockname = optarg;
			break;
		case 'w':
			maxwait_set = 1;
			maxwait = strtonum(optarg, 1, INT_MAX, &errstr);
			if (errstr)
				errx(1, "maxwait value is %s: %s",
				    errstr, optarg);
			break;

		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (argc != 1)
		usage();

	if ((if_index = if_nametoindex(argv[0])) == 0)
		errx(1, "unknown interface");

	if (lFlag && !maxwait_set)
		maxwait = 0;

	/* Connect to control socket. */
	if ((ctl_sock = socket(AF_UNIX, SOCK_STREAM, 0)) == -1)
		err(1, "socket");

	memset(&sun, 0, sizeof(sun));
	sun.sun_family = AF_UNIX;
	strlcpy(sun.sun_path, sockname, sizeof(sun.sun_path));

	if (connect(ctl_sock, (struct sockaddr *)&sun, sizeof(sun)) == -1)
		err(1, "connect: %s", sockname);

	if (pledge("stdio", NULL) == -1)
		err(1, "pledge");

	if ((ibuf = malloc(sizeof(struct imsgbuf))) == NULL)
		err(1, NULL);
	if (imsgbuf_init(ibuf, ctl_sock) == -1)
		err(1, NULL);

	if (!lFlag) {
		imsg_compose(ibuf, IMSG_CTL_SEND_REQUEST, 0, 0, -1,
		    &if_index, sizeof(if_index));
		if (imsgbuf_flush(ibuf) == -1)
			err(1, "write error");

	}

	for(;;) {
		imsg_compose(ibuf, IMSG_CTL_SHOW_INTERFACE_INFO, 0, 0, -1,
		    &if_index, sizeof(if_index));

		if (imsgbuf_flush(ibuf) == -1)
			err(1, "write error");

		if ((n = imsgbuf_read(ibuf)) == -1)
			err(1, "read error");
		if (n == 0)
			errx(1, "pipe closed");

		if ((n = imsg_get(ibuf, &imsg)) == -1)
			errx(1, "imsg_get error");
		if (n == 0)
			break;

		if (imsg.hdr.type == IMSG_CTL_END) {
			if (lFlag)
				errx(1, "non-autoconf interface %s", argv[0]);
			else if (--maxwait < 0)
				break;
			else
				continue;
		}

		cei = imsg.data;
		if (strcmp(cei->state, "Bound") == 0)
			bound = 1;

		if (bound || --maxwait < 0) {
			if (didot)
				putchar('\n');
			show_interface_msg(cei);
			break;
		} else {
			didot = 1;
			putchar('.');
			fflush(stdout);
		}
		imsg_free(&imsg);
		sleep(1);
	}
	close(ctl_sock);
	free(ibuf);

	return (0);
}

void
show_interface_msg(struct ctl_engine_info *cei)
{
	struct timespec		 now, diff;
	time_t			 d, h, m, s;
	int			 i, has_pd = 0;
	char			 buf[IF_NAMESIZE], *bufp;
	char			 ntopbuf[INET6_ADDRSTRLEN];

	bufp = if_indextoname(cei->if_index, buf);
	printf("%s [%s]\n", bufp != NULL ? bufp : "unknown", cei->state);

	for (i = 0; i < MAX_IA; i++) {
		if (cei->pds[i].prefix_len == 0)
			continue;
		has_pd = 1;
		printf ("\tIA_PD %d: %s/%d\n", i, inet_ntop(AF_INET6,
		    &cei->pds[i], ntopbuf, INET6_ADDRSTRLEN),
		    cei->pds[i].prefix_len);
	}

	if (has_pd) {
		clock_gettime(CLOCK_MONOTONIC, &now);
		timespecsub(&now, &cei->request_time, &diff);
		s = cei->lease_time - diff.tv_sec;
		if (s < 0)
			s = 0;

		if ( s > 86400 ) {
			d = s / 86400;

			/* round up */
			if (s - d * 86400 > 43200)
				d++;
			printf("\tlease %lld day%s\n", d, d  > 1 ? "s" : "");
		} else if (s > 3600) {
			h = s / 3600;

			/* round up */
			if (s - h * 3600 > 1800)
				h++;
			printf("\tlease %lld hour%s\n", h, h > 1 ? "s" : "");
		} else if (s > 60) {
			m = s / 60;

			/* round up */
			if (s - m * 60 > 30)
				m++;
			printf("\tlease %lld minute%s\n", m, m > 1 ? "s" : "");
		} else
			printf("\tlease %lld second%s\n", s, s > 1 ? "s" : "");

	}
}
