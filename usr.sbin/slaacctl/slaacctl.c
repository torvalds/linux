/*	$OpenBSD: slaacctl.c,v 1.28 2024/11/21 13:38:15 claudio Exp $	*/

/*
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
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <arpa/inet.h>

#include <net/if.h>
#include <net/if_media.h>
#include <net/if_types.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>
#include <netinet6/nd6.h>

#include <err.h>
#include <errno.h>
#include <event.h>
#include <imsg.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "slaacd.h"
#include "frontend.h"
#include "parser.h"

__dead void	 usage(void);
int		 show_interface_msg(struct imsg *);

struct imsgbuf	*ibuf;

__dead void
usage(void)
{
	extern char *__progname;

	fprintf(stderr, "usage: %s [-s socket] command [argument ...]\n",
	    __progname);
	exit(1);
}

int
main(int argc, char *argv[])
{
	struct sockaddr_un	 sun;
	struct parse_result	*res;
	struct imsg		 imsg;
	int			 ctl_sock;
	int			 done = 0;
	int			 n, verbose = 0;
	int			 ch;
	char			*sockname;

	sockname = _PATH_SLAACD_SOCKET;
	while ((ch = getopt(argc, argv, "s:")) != -1) {
		switch (ch) {
		case 's':
			sockname = optarg;
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (pledge("stdio unix", NULL) == -1)
		err(1, "pledge");

	/* Parse command line. */
	if ((res = parse(argc, argv)) == NULL)
		exit(1);

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
	done = 0;

	/* Process user request. */
	switch (res->action) {
	case LOG_VERBOSE:
		verbose = 1;
		/* FALLTHROUGH */
	case LOG_BRIEF:
		imsg_compose(ibuf, IMSG_CTL_LOG_VERBOSE, 0, 0, -1,
		    &verbose, sizeof(verbose));
		printf("logging request sent.\n");
		done = 1;
		break;
	case SHOW_INTERFACE:
		imsg_compose(ibuf, IMSG_CTL_SHOW_INTERFACE_INFO, 0, 0, -1,
		    &res->if_index, sizeof(res->if_index));
		break;
	case SEND_SOLICITATION:
		imsg_compose(ibuf, IMSG_CTL_SEND_SOLICITATION, 0, 0, -1,
		    &res->if_index, sizeof(res->if_index));
		done = 1;
		break;
	default:
		usage();
	}

	if (imsgbuf_flush(ibuf) == -1)
		err(1, "write error");

	while (!done) {
		if ((n = imsgbuf_read(ibuf)) == -1)
			err(1, "read error");
		if (n == 0)
			errx(1, "pipe closed");

		while (!done) {
			if ((n = imsg_get(ibuf, &imsg)) == -1)
				errx(1, "imsg_get error");
			if (n == 0)
				break;

			switch (res->action) {
			case SHOW_INTERFACE:
				done = show_interface_msg(&imsg);
				break;
			default:
				break;
			}

			imsg_free(&imsg);
		}
	}
	close(ctl_sock);
	free(ibuf);

	return (0);
}

int
show_interface_msg(struct imsg *imsg)
{
	static int				 if_count = 0;
	struct ctl_engine_info			*cei;
	struct ctl_engine_info_ra		*cei_ra;
	struct ctl_engine_info_ra_prefix	*cei_ra_prefix;
	struct ctl_engine_info_ra_rdns		*cei_ra_rdns;
	struct ctl_engine_info_address_proposal	*cei_addr_proposal;
	struct ctl_engine_info_dfr_proposal	*cei_dfr_proposal;
	struct ctl_engine_info_rdns_proposal	*cei_rdns_proposal;
	struct tm				*t;
	struct timespec				 now, diff;
	int					 i;
	char					 buf[IF_NAMESIZE], *bufp;
	char					 hbuf[NI_MAXHOST], whenbuf[255];
	char					 ntopbuf[INET6_ADDRSTRLEN];

	switch (imsg->hdr.type) {
	case IMSG_CTL_SHOW_INTERFACE_INFO:
		cei = imsg->data;

		if (if_count++ > 0)
			printf("\n");

		bufp = if_indextoname(cei->if_index, buf);
		printf("%s:\n", bufp != NULL ? bufp : "unknown");
		printf("\t index: %3u ", cei->if_index);
		printf("running: %3s ", cei->running ? "yes" : "no");
		printf("temporary: %3s\n", cei->temporary ? "yes" :
		    "no");
		printf("\tlladdr: %s\n", ether_ntoa(&cei->hw_address));
		if (getnameinfo((struct sockaddr *)&cei->ll_address,
		    cei->ll_address.sin6_len, hbuf, sizeof(hbuf), NULL, 0,
		    NI_NUMERICHOST | NI_NUMERICSERV))
			err(1, "cannot get link local address");
		printf("\t inet6: %s\n", hbuf);
		break;
	case IMSG_CTL_SHOW_INTERFACE_INFO_RA:
		cei_ra = imsg->data;

		if (getnameinfo((struct sockaddr *)&cei_ra->from,
		    cei_ra->from.sin6_len, hbuf, sizeof(hbuf), NULL, 0,
		    NI_NUMERICHOST | NI_NUMERICSERV))
			err(1, "cannot get router IP");

		if (clock_gettime(CLOCK_MONOTONIC, &now))
			err(1, "clock_gettime");

		timespecsub(&now, &cei_ra->uptime, &diff);

		t = localtime(&cei_ra->when.tv_sec);
		strftime(whenbuf, sizeof(whenbuf), "%F %T", t);
		printf("\tRouter Advertisement from %s\n", hbuf);
		printf("\t\treceived: %s; %llds ago\n", whenbuf, diff.tv_sec);
		printf("\t\tCur Hop Limit: %3u, M: %d, O: %d, Router Lifetime:"
		    " %5us\n", cei_ra->curhoplimit, cei_ra->managed ? 1: 0,
		    cei_ra->other ? 1 : 0, cei_ra->router_lifetime);
		printf("\t\tDefault Router Preference: %s\n", cei_ra->rpref);
		printf("\t\tReachable Time: %9ums, Retrans Timer: %9ums\n",
		    cei_ra->reachable_time, cei_ra->retrans_time);
		if (cei_ra->mtu)
			printf("\t\tMTU: %u bytes\n", cei_ra->mtu);
		break;
	case IMSG_CTL_SHOW_INTERFACE_INFO_RA_PREFIX:
		cei_ra_prefix = imsg->data;
		printf("\t\tprefix: %s/%u\n", inet_ntop(AF_INET6,
		    &cei_ra_prefix->prefix, ntopbuf, INET6_ADDRSTRLEN),
			    cei_ra_prefix->prefix_len);
		printf("\t\t\tOn-link: %d, Autonomous address-configuration: %d"
		    "\n", cei_ra_prefix->onlink ? 1 : 0,
		    cei_ra_prefix->autonomous ? 1 : 0);
		if (cei_ra_prefix->vltime == ND6_INFINITE_LIFETIME)
			printf("\t\t\tvltime: %10s, ", "infinity");
		else
			printf("\t\t\tvltime: %10u, ", cei_ra_prefix->vltime);
		if (cei_ra_prefix->pltime == ND6_INFINITE_LIFETIME)
			printf("pltime: %10s\n", "infinity");
		else
			printf("pltime: %10u\n", cei_ra_prefix->pltime);
		break;
	case IMSG_CTL_SHOW_INTERFACE_INFO_RA_RDNS:
		cei_ra_rdns = imsg->data;
		printf("\t\trdns: %s, lifetime: %u\n", inet_ntop(AF_INET6,
		    &cei_ra_rdns->rdns, ntopbuf, INET6_ADDRSTRLEN),
		    cei_ra_rdns->lifetime);
		break;
	case IMSG_CTL_SHOW_INTERFACE_INFO_ADDR_PROPOSALS:
		printf("\tAddress proposals\n");
		break;
	case IMSG_CTL_SHOW_INTERFACE_INFO_ADDR_PROPOSAL:
		cei_addr_proposal = imsg->data;

		if (getnameinfo((struct sockaddr *)&cei_addr_proposal->addr,
		    cei_addr_proposal->addr.sin6_len, hbuf, sizeof(hbuf),
		    NULL, 0, NI_NUMERICHOST | NI_NUMERICSERV))
			err(1, "cannot get proposal IP");

		printf("\t\tid: %4lld, state: %15s, temporary: %s\n",
		    cei_addr_proposal->id, cei_addr_proposal->state,
		    cei_addr_proposal->temporary ? "y" : "n");

		if (clock_gettime(CLOCK_MONOTONIC, &now))
			err(1, "clock_gettime");

		timespecsub(&now, &cei_addr_proposal->uptime, &diff);

		if (cei_addr_proposal->vltime == ND6_INFINITE_LIFETIME)
			printf("\t\tvltime: %10s, ", "infinity");
		else
			printf("\t\tvltime: %10u, ", cei_addr_proposal->vltime);
		if (cei_addr_proposal->pltime == ND6_INFINITE_LIFETIME)
			printf("pltime: %10s", "infinity");
		else
			printf("pltime: %10u", cei_addr_proposal->pltime);
		if (cei_addr_proposal->next_timeout != 0)
			printf(", timeout: %10llds\n",
			    cei_addr_proposal->next_timeout - diff.tv_sec);
		else
			printf("\n");

		t = localtime(&cei_addr_proposal->when.tv_sec);
		strftime(whenbuf, sizeof(whenbuf), "%F %T", t);
		printf("\t\tupdated: %s; %llds ago\n", whenbuf, diff.tv_sec);
		printf("\t\t%s, %s/%u\n", hbuf, inet_ntop(AF_INET6,
		    &cei_addr_proposal->prefix, ntopbuf, INET6_ADDRSTRLEN),
		    cei_addr_proposal->prefix_len);
		break;
	case IMSG_CTL_SHOW_INTERFACE_INFO_DFR_PROPOSALS:
		printf("\tDefault router proposals\n");
		break;
	case IMSG_CTL_SHOW_INTERFACE_INFO_DFR_PROPOSAL:
		cei_dfr_proposal = imsg->data;

		if (getnameinfo((struct sockaddr *)&cei_dfr_proposal->addr,
		    cei_dfr_proposal->addr.sin6_len, hbuf, sizeof(hbuf),
		    NULL, 0, NI_NUMERICHOST | NI_NUMERICSERV))
			err(1, "cannot get router IP");

		printf("\t\tid: %4lld, state: %15s\n",
		    cei_dfr_proposal->id, cei_dfr_proposal->state);
		printf("\t\trouter: %s\n", hbuf);
		printf("\t\trouter lifetime: %10u\n",
		    cei_dfr_proposal->router_lifetime);
		printf("\t\tPreference: %s\n", cei_dfr_proposal->rpref);
		if (clock_gettime(CLOCK_MONOTONIC, &now))
			err(1, "clock_gettime");

		timespecsub(&now, &cei_dfr_proposal->uptime, &diff);

		t = localtime(&cei_dfr_proposal->when.tv_sec);
		strftime(whenbuf, sizeof(whenbuf), "%F %T", t);
		printf("\t\tupdated: %s; %llds ago", whenbuf, diff.tv_sec);
		if (cei_dfr_proposal->next_timeout != 0)
			printf(", timeout: %10llds\n",
			    cei_dfr_proposal->next_timeout - diff.tv_sec);
		else
			printf("\n");

		break;
	case IMSG_CTL_SHOW_INTERFACE_INFO_RDNS_PROPOSALS:
		printf("\trDNS proposals\n");
		break;
	case IMSG_CTL_SHOW_INTERFACE_INFO_RDNS_PROPOSAL:
		cei_rdns_proposal = imsg->data;

		if (getnameinfo((struct sockaddr *)&cei_rdns_proposal->from,
		    cei_rdns_proposal->from.sin6_len, hbuf, sizeof(hbuf),
		    NULL, 0, NI_NUMERICHOST | NI_NUMERICSERV))
			err(1, "cannot get router IP");

		printf("\t\tid: %4lld, state: %15s\n",
		    cei_rdns_proposal->id, cei_rdns_proposal->state);
		printf("\t\trouter: %s\n", hbuf);
		printf("\t\trdns lifetime: %10u\n",
		    cei_rdns_proposal->rdns_lifetime);
		printf("\t\trdns:\n");
		for (i = 0; i < cei_rdns_proposal->rdns_count; i++) {
			printf("\t\t\t%s\n", inet_ntop(AF_INET6,
			    &cei_rdns_proposal->rdns[i], ntopbuf,
			    INET6_ADDRSTRLEN));
		}

		if (clock_gettime(CLOCK_MONOTONIC, &now))
			err(1, "clock_gettime");

		timespecsub(&now, &cei_rdns_proposal->uptime, &diff);

		t = localtime(&cei_rdns_proposal->when.tv_sec);
		strftime(whenbuf, sizeof(whenbuf), "%F %T", t);
		printf("\t\tupdated: %s; %llds ago", whenbuf, diff.tv_sec);
		if (cei_rdns_proposal->next_timeout != 0)
			printf(", timeout: %10llds\n",
			    cei_rdns_proposal->next_timeout - diff.tv_sec);
		else
			printf("\n");

		break;
	case IMSG_CTL_END:
		return (1);
	default:
		break;
	}

	return (0);
}
