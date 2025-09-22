/*	$OpenBSD: unwindctl.c,v 1.34 2024/11/21 13:38:15 claudio Exp $	*/

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
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <net/if_media.h>
#include <net/if_types.h>
#include <net/route.h>

#include <err.h>
#include <errno.h>
#include <event.h>
#include <imsg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "unwind.h"
#include "frontend.h"
#include "resolver.h"
#include "parser.h"

__dead void	 usage(void);
int		 show_status_msg(struct imsg *);
int		 show_autoconf_msg(struct imsg *);
int		 show_mem_msg(struct imsg *);
void		 histogram_header(void);
void		 print_histogram(const char *name, int64_t[], size_t);
const char	*prio2str(int);

struct imsgbuf		*ibuf;
int		 	 info_cnt;
struct ctl_resolver_info info[UW_RES_NONE];

const char *
prio2str(int prio)
{
	switch(prio) {
	case RTP_PROPOSAL_DHCLIENT:
		return "DHCP";
	case RTP_PROPOSAL_SLAAC:
		return "SLAAC";
	case RTP_PROPOSAL_STATIC:
		return "STATIC";
	case RTP_PROPOSAL_UMB:
		return "UMB";
	case RTP_PROPOSAL_PPP:
		return "PPP";
	}
	return "OTHER";
}

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
	struct sockaddr_un		 sun;
	struct parse_result		*res;
	struct imsg			 imsg;
	struct ctl_resolver_info	*cri;
	int				 ctl_sock;
	int				 done = 0;
	int				 i, j, k, n, verbose = 0;
	int				 ch, column_offset;
	char				*sockname;

	sockname = _PATH_UNWIND_SOCKET;
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

	/* Check for root-only actions */
	switch (res->action) {
	case LOG_DEBUG:
	case LOG_VERBOSE:
	case LOG_BRIEF:
	case RELOAD:
		if (geteuid() != 0)
			errx(1, "need root privileges");
		break;
	default:
		break;
	}

	/* Process user request. */
	switch (res->action) {
	case LOG_DEBUG:
		verbose |= OPT_VERBOSE2;
		/* FALLTHROUGH */
	case LOG_VERBOSE:
		verbose |= OPT_VERBOSE;
		/* FALLTHROUGH */
	case LOG_BRIEF:
		imsg_compose(ibuf, IMSG_CTL_LOG_VERBOSE, 0, 0, -1,
		    &verbose, sizeof(verbose));
		printf("logging request sent.\n");
		done = 1;
		break;
	case RELOAD:
		imsg_compose(ibuf, IMSG_CTL_RELOAD, 0, 0, -1, NULL, 0);
		printf("reload request sent.\n");
		done = 1;
		break;
	case STATUS:
		imsg_compose(ibuf, IMSG_CTL_STATUS, 0, 0, -1, NULL, 0);
		break;
	case AUTOCONF:
		imsg_compose(ibuf, IMSG_CTL_AUTOCONF, 0, 0, -1, NULL, 0);
		break;
	case MEM:
		imsg_compose(ibuf, IMSG_CTL_MEM, 0, 0, -1, NULL, 0);
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
			case STATUS:
				done = show_status_msg(&imsg);
				break;
			case AUTOCONF:
				done = show_autoconf_msg(&imsg);
				break;
			case MEM:
				done = show_mem_msg(&imsg);
				break;
			default:
				break;
			}
			imsg_free(&imsg);
		}
	}
	close(ctl_sock);
	free(ibuf);

	column_offset = info_cnt / 2;
	if (info_cnt % 2 == 1)
		column_offset++;

	for (i = 0; i < column_offset; i++) {
		for (j = 0; j < 2; j++) {
			k = i + j * column_offset;
			if (k >= info_cnt)
				break;

			cri = &info[k];
			printf("%d. %-15s %10s, ", k + 1,
			    uw_resolver_type_str[cri->type],
			    uw_resolver_state_str[cri->state]);
			if (cri->median == 0)
				printf("%5s", "N/A");
			else if (cri->median == INT64_MAX)
				printf("%5s", "Inf");
			else
				printf("%3lldms", cri->median);
			if (j == 0)
				printf("   ");
		}
		printf("\n");
	}

	if (info_cnt)
		histogram_header();
	for (i = 0; i < info_cnt; i++) {
		cri = &info[i];
		print_histogram(uw_resolver_type_short[cri->type],
		    cri->histogram, nitems(cri->histogram));
		print_histogram("", cri->latest_histogram,
		    nitems(cri->latest_histogram));
	}
	return (0);
}

int
show_status_msg(struct imsg *imsg)
{
	static char			 fwd_line[80];

	switch (imsg->hdr.type) {
	case IMSG_CTL_RESOLVER_INFO:
		memcpy(&info[info_cnt++], imsg->data, sizeof(info[0]));
		break;
	case IMSG_CTL_END:
		if (fwd_line[0] != '\0')
			printf("%s\n", fwd_line);
		return (1);
	default:
		break;
	}

	return (0);
}

int
show_autoconf_msg(struct imsg *imsg)
{
	static int			 autoconf_forwarders, last_src;
	static int			 label_len, line_len;
	static uint32_t			 last_if_index;
	static char			 fwd_line[80];
	struct ctl_forwarder_info	*cfi;
	char				 ifnamebuf[IFNAMSIZ];
	char				*if_name;

	switch (imsg->hdr.type) {
	case IMSG_CTL_AUTOCONF_RESOLVER_INFO:
		cfi = imsg->data;
		if (!autoconf_forwarders++)
			printf("autoconfiguration forwarders:\n");
		if (cfi->if_index != last_if_index || cfi->src != last_src) {
			if_name = if_indextoname(cfi->if_index, ifnamebuf);
			if (fwd_line[0] != '\0') {
				printf("%s\n", fwd_line);
				fwd_line[0] = '\0';
			}
			label_len = snprintf(fwd_line, sizeof(fwd_line),
			    "%6s[%s]:", prio2str(cfi->src),
			    if_name ? if_name : "unknown");
			line_len = label_len;
			last_if_index = cfi->if_index;
			last_src = cfi->src;
		}

		if (line_len + 1 + strlen(cfi->ip) > sizeof(fwd_line)) {
			printf("%s\n", fwd_line);
			snprintf(fwd_line, sizeof(fwd_line), "%*s", label_len,
			    " ");
		}
		strlcat(fwd_line, " ", sizeof(fwd_line));
		line_len = strlcat(fwd_line, cfi->ip, sizeof(fwd_line));
		break;
	case IMSG_CTL_END:
		if (fwd_line[0] != '\0')
			printf("%s\n", fwd_line);
		return (1);
	default:
		break;
	}

	return (0);
}

void
histogram_header(void)
{
	const char	 head[] = "histograms: lifetime[ms], decaying[ms]";
	char	 	 buf[10];
	size_t	 	 i;

	printf("\n%*s%*s\n%*s", 5, "",
	    (int)(72/2 + (sizeof(head)-1)/2), head, 6, "");
	for(i = 0; i < nitems(histogram_limits) - 1; i++) {
		snprintf(buf, sizeof(buf), "<%lld", histogram_limits[i]);
		printf("%6s", buf);
	}
	printf("%6s\n", ">");
}

void
print_histogram(const char *name, int64_t histogram[], size_t n)
{
	size_t	 i;

	printf("%5s ", name);
	for(i = 0; i < n; i++)
		printf("%6lld", histogram[i]);
	printf("\n");
}

int
show_mem_msg(struct imsg *imsg)
{
	struct ctl_mem_info	*cmi;

	switch (imsg->hdr.type) {
	case IMSG_CTL_MEM_INFO:
		cmi = imsg->data;
		printf("msg-cache:   %zu / %zu (%.2f%%)\n", cmi->msg_cache_used,
		    cmi->msg_cache_max, 100.0 * cmi->msg_cache_used /
		    cmi->msg_cache_max);
		printf("rrset-cache: %zu / %zu (%.2f%%)\n",
		    cmi->rrset_cache_used, cmi->rrset_cache_max, 100.0 *
		    cmi->rrset_cache_used / cmi->rrset_cache_max);
		printf("key-cache: %zu / %zu (%.2f%%)\n", cmi->key_cache_used,
		    cmi->key_cache_max, 100.0 * cmi->key_cache_used /
		    cmi->key_cache_max);
		printf("neg-cache: %zu / %zu (%.2f%%)\n", cmi->neg_cache_used,
		    cmi->neg_cache_max, 100.0 * cmi->neg_cache_used /
		    cmi->neg_cache_max);
		break;
	default:
		break;
	}

	return 1;
}
