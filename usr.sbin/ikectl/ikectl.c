/*	$OpenBSD: ikectl.c,v 1.36 2024/11/21 13:38:14 claudio Exp $	*/

/*
 * Copyright (c) 2007-2013 Reyk Floeter <reyk@openbsd.org>
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
#include <sys/socket.h>
#include <sys/queue.h>
#include <sys/un.h>
#include <sys/tree.h>

#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <event.h>

#include "iked.h"
#include "parser.h"

__dead void	 usage(void);

struct imsgname {
	int type;
	char *name;
	void (*func)(struct imsg *);
};

struct imsgname *monitor_lookup(uint8_t);
int		 monitor(struct imsg *);

int		 show_string(struct imsg *);
int		 show_stats(struct imsg *, int);

int		 ca_opt(struct parse_result *);

struct imsgname imsgs[] = {
	{ IMSG_CTL_OK,			"ok",			NULL },
	{ IMSG_CTL_FAIL,		"fail",			NULL },
	{ IMSG_CTL_VERBOSE,		"verbose",		NULL },
	{ IMSG_CTL_RELOAD,		"reload",		NULL },
	{ IMSG_CTL_RESET,		"reset",		NULL },
	{ IMSG_CTL_SHOW_SA,		"show sa",		NULL },
	{ IMSG_CTL_SHOW_CERTSTORE,	"show certstore",	NULL },
	{ 0,				NULL,			NULL }

};
struct imsgname imsgunknown = {
	-1,				"<unknown>",		NULL
};

struct imsgbuf	*ibuf;

__dead void
usage(void)
{
	extern char *__progname;

	fprintf(stderr, "usage: %s [-q] [-s socket] command [arg ...]\n",
	    __progname);
	exit(1);
}

int
ca_opt(struct parse_result *res)
{
	struct ca	*ca;
	size_t		 len;
	char		*p;

	ca = ca_setup(res->caname, (res->action == CA_CREATE),
	    res->quiet, res->pass);
	if (ca == NULL)
		errx(1, "ca_setup failed");

	/* assume paths are relative to /etc if not absolute */
	if (res->path && (res->path[0] != '.') && (res->path[0] != '/')) {
		len = 5 + strlen(res->path) + 1;
		if ((p = malloc(len)) == NULL)
			err(1, "malloc");
		snprintf(p, len, "/etc/%s", res->path);
		free(res->path);
		res->path = p;
	}

	switch (res->action) {
	case CA_CREATE:
		ca_create(ca);
		break;
	case CA_DELETE:
		ca_delete(ca);
		break;
	case CA_INSTALL:
		ca_install(ca, res->path);
		break;
	case CA_EXPORT:
		ca_export(ca, NULL, res->peer, res->pass);
		break;
	case CA_CERT_CREATE:
	case CA_SERVER:
	case CA_CLIENT:
	case CA_OCSP:
		ca_certificate(ca, res->host, res->htype, res->action);
		break;
	case CA_CERT_DELETE:
		ca_delkey(ca, res->host);
		break;
	case CA_CERT_INSTALL:
		ca_cert_install(ca, res->host, res->path);
		break;
	case CA_CERT_EXPORT:
		ca_export(ca, res->host, res->peer, res->pass);
		break;
	case CA_CERT_REVOKE:
		ca_revoke(ca, res->host);
		break;
	case SHOW_CA_CERTIFICATES:
		ca_show_certs(ca, res->host);
		break;
	case CA_KEY_CREATE:
		ca_key_create(ca, res->host);
		break;
	case CA_KEY_DELETE:
		ca_key_delete(ca, res->host);
		break;
	case CA_KEY_INSTALL:
		ca_key_install(ca, res->host, res->path);
		break;
	case CA_KEY_IMPORT:
		ca_key_import(ca, res->host, res->path);
		break;
	default:
		break;
	}

	return (0);
}

int
main(int argc, char *argv[])
{
	struct sockaddr_un	 s_un;
	struct parse_result	*res;
	struct imsg		 imsg;
	int			 ctl_sock;
	int			 done = 1;
	int			 n;
	int			 ch;
	int			 v = 0;
	int			 quiet = 0;
	const char		*sock = IKED_SOCKET;

	while ((ch = getopt(argc, argv, "qs:")) != -1) {
		switch (ch) {
		case 'q':
			quiet = 1;
			break;
		case 's':
			sock = optarg;
			break;
		default:
			usage();
			/* NOTREACHED */
		}
	}
	argc -= optind;
	argv += optind;

	/* parse options */
	if ((res = parse(argc, argv)) == NULL)
		exit(1);

	res->quiet = quiet;

	switch (res->action) {
	case CA_CREATE:
	case CA_DELETE:
	case CA_INSTALL:
	case CA_EXPORT:
	case CA_CERT_CREATE:
	case CA_CLIENT:
	case CA_SERVER:
	case CA_OCSP:
	case CA_CERT_DELETE:
	case CA_CERT_INSTALL:
	case CA_CERT_EXPORT:
	case CA_CERT_REVOKE:
	case SHOW_CA:
	case SHOW_CA_CERTIFICATES:
	case CA_KEY_CREATE:
	case CA_KEY_DELETE:
	case CA_KEY_INSTALL:
	case CA_KEY_IMPORT:
		if (pledge("stdio proc exec rpath wpath cpath fattr tty", NULL)
		    == -1)
			err(1, "pledge");
		ca_opt(res);
		break;
	case NONE:
		usage();
		break;
	default:
		goto connect;
	}

	return (0);

 connect:
	/* connect to iked control socket */
	if ((ctl_sock = socket(AF_UNIX, SOCK_STREAM, 0)) == -1)
		err(1, "socket");

	bzero(&s_un, sizeof(s_un));
	s_un.sun_family = AF_UNIX;
	strlcpy(s_un.sun_path, sock, sizeof(s_un.sun_path));
 reconnect:
	if (connect(ctl_sock, (struct sockaddr *)&s_un, sizeof(s_un)) == -1) {
		/* Keep retrying if running in monitor mode */
		if (res->action == MONITOR &&
		    (errno == ENOENT || errno == ECONNREFUSED)) {
			usleep(100);
			goto reconnect;
		}
		err(1, "connect: %s", sock);
	}

	if (pledge("stdio", NULL) == -1)
		err(1, "pledge");

	if (res->ibuf != NULL)
		ibuf = res->ibuf;
	else
		if ((ibuf = malloc(sizeof(struct imsgbuf))) == NULL)
			err(1, "malloc");
	if (imsgbuf_init(ibuf, ctl_sock) == -1)
		err(1, "imsgbuf_init");

	/* process user request */
	switch (res->action) {
	case RESETALL:
		v = RESET_ALL;
		break;
	case RESETCA:
		v = RESET_CA;
		break;
	case RESETPOLICY:
		v = RESET_POLICY;
		break;
	case RESETSA:
		v = RESET_SA;
		break;
	case RESETUSER:
		v = RESET_USER;
		break;
	case LOG_VERBOSE:
		v = 2;
		break;
	case LOG_BRIEF:
	default:
		v = 0;
		break;
	}

	switch (res->action) {
	case NONE:
		usage();
		/* NOTREACHED */
		break;
	case RESETALL:
	case RESETCA:
	case RESETPOLICY:
	case RESETSA:
	case RESETUSER:
		imsg_compose(ibuf, IMSG_CTL_RESET, 0, 0, -1, &v, sizeof(v));
		printf("reset request sent.\n");
		break;
	case LOAD:
		imsg_compose(ibuf, IMSG_CTL_RELOAD, 0, 0, -1,
		    res->path, strlen(res->path));
		break;
	case RESET_ID:
		imsg_compose(ibuf, IMSG_CTL_RESET_ID, 0, 0, -1,
		    res->id, strlen(res->id));
		break;
	case SHOW_SA:
		imsg_compose(ibuf, IMSG_CTL_SHOW_SA, 0, 0, -1, NULL, 0);
		done = 0;
		break;
	case SHOW_STATS:
		imsg_compose(ibuf, IMSG_CTL_SHOW_STATS, 0, 0, -1, NULL, 0);
		done = 0;
		break;
	case SHOW_CERTSTORE:
		imsg_compose(ibuf, IMSG_CTL_SHOW_CERTSTORE, 0, 0, -1, NULL, 0);
		done = 0;
		break;
	case RELOAD:
		imsg_compose(ibuf, IMSG_CTL_RELOAD, 0, 0, -1, NULL, 0);
		break;
	case MONITOR:
		imsg_compose(ibuf, IMSG_CTL_NOTIFY, 0, 0, -1, NULL, 0);
		done = 0;
		break;
	case COUPLE:
		imsg_compose(ibuf, IMSG_CTL_COUPLE, 0, 0, -1, NULL, 0);
		break;
	case DECOUPLE:
		imsg_compose(ibuf, IMSG_CTL_DECOUPLE, 0, 0, -1, NULL, 0);
		break;
	case ACTIVE:
		imsg_compose(ibuf, IMSG_CTL_ACTIVE, 0, 0, -1, NULL, 0);
		break;
	case PASSIVE:
		imsg_compose(ibuf, IMSG_CTL_PASSIVE, 0, 0, -1, NULL, 0);
		break;
	case LOG_VERBOSE:
	case LOG_BRIEF:
		imsg_compose(ibuf, IMSG_CTL_VERBOSE, 0, 0, -1, &v, sizeof(v));
		printf("logging request sent.\n");
		break;
	default:
		break;
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
			case MONITOR:
				done = monitor(&imsg);
				break;
			case SHOW_STATS:
				done = show_stats(&imsg, quiet);
				break;
			case SHOW_SA:
			case SHOW_CERTSTORE:
				done = show_string(&imsg);
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

struct imsgname *
monitor_lookup(uint8_t type)
{
	int i;

	for (i = 0; imsgs[i].name != NULL; i++)
		if (imsgs[i].type == type)
			return (&imsgs[i]);
	return (&imsgunknown);
}

int
monitor(struct imsg *imsg)
{
	time_t			 now;
	int			 done = 0;
	struct imsgname		*imn;

	now = time(NULL);

	imn = monitor_lookup(imsg->hdr.type);
	printf("%s: imsg type %u len %u peerid %u pid %d\n", imn->name,
	    imsg->hdr.type, imsg->hdr.len, imsg->hdr.peerid, imsg->hdr.pid);
	printf("\ttimestamp: %lld, %s", (long long)now, ctime(&now));
	if (imn->type == -1)
		done = 1;
	if (imn->func != NULL)
		(*imn->func)(imsg);

	return (done);
}

int
show_string(struct imsg *imsg)
{
	int	done = 0;

	switch (imsg->hdr.type) {
	case IMSG_CTL_SHOW_SA:
	case IMSG_CTL_SHOW_CERTSTORE:
		break;
	default:
		return (done);
	}
	if (IMSG_DATA_SIZE(imsg) > 0)
		printf("%s", (char *)imsg->data);
	else
		done = 1;

	return (done);
}

static char *
plural(uint64_t n)
{
        return (n != 1 ? "s" : "");
}

/*
 * Dump IKE statistics structure.
 */
int
show_stats(struct imsg *imsg, int quiet)
{
	struct iked_stats *stat;
	int		 done = 1;

	if (IMSG_DATA_SIZE(imsg) != sizeof(*stat))
		return (done);
	stat = imsg->data;
	printf("ike:\n");
#define p(f, m) if (stat->f || !quiet) \
	printf(m, stat->f, plural(stat->f))

	p(ikes_sa_created, "\t%llu IKE SA%s created\n");
	p(ikes_sa_established_total, "\t%llu IKE SA%s established\n");
	p(ikes_sa_established_current, "\t%llu IKE SA%s currently established\n");
	p(ikes_sa_established_failures, "\t%llu IKE SA%s failed to establish\n");
	p(ikes_sa_proposals_negotiate_failures, "\t%llu failed proposal negotiation%s\n");
	p(ikes_sa_rekeyed, "\t%llu IKE SA%s rekeyed\n");
	p(ikes_sa_removed, "\t%llu IKE SA%s removed\n");
	p(ikes_csa_created, "\t%llu Child SA%s created\n");
	p(ikes_csa_removed, "\t%llu Child SA%s removed\n");
	p(ikes_msg_sent, "\t%llu message%s sent\n");
	p(ikes_msg_send_failures, "\t%llu message%s could not be sent\n");
	p(ikes_msg_rcvd, "\t%llu message%s received\n");
	p(ikes_msg_rcvd_dropped, "\t%llu message%s dropped\n");
	p(ikes_msg_rcvd_busy, "\t%llu request%s dropped, response being worked on\n");
	p(ikes_retransmit_response, "\t%llu response%s retransmitted\n");
	p(ikes_retransmit_request, "\t%llu request%s retransmitted\n");
	p(ikes_retransmit_limit, "\t%llu request%s timed out\n");
	p(ikes_frag_sent, "\t%llu fragment%s sent\n");
	p(ikes_frag_send_failures, "\t%llu fragment%s could not be sent\n");
	p(ikes_frag_rcvd, "\t%llu fragment%s received\n");
	p(ikes_frag_rcvd_drop, "\t%llu fragment%s dropped\n");
	p(ikes_frag_reass_ok, "\t%llu fragment%s reassembled\n");
	p(ikes_frag_reass_drop, "\t%llu fragment%s could not be reassembled\n");
	p(ikes_update_addresses_sent, "\t%llu update addresses request%s sent\n");
	p(ikes_dpd_sent, "\t%llu dpd request%s sent\n");
	p(ikes_keepalive_sent, "\t%llu keepalive message%s sent\n");
#undef p
	return (done);
}
