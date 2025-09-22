/*	$OpenBSD: npppctl.c,v 1.15 2024/11/21 13:43:10 claudio Exp $	*/

/*
 * Copyright (c) 2012 Internet Initiative Japan Inc.
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
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/uio.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <errno.h>
#include <netdb.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <imsg.h>

#include <unistd.h>
#include <err.h>

#include "parser.h"
#include "npppd_ctl.h"

#define MINIMUM(a, b)	(((a) < (b)) ? (a) : (b))

#ifndef nitems
#define nitems(_x)	(sizeof(_x) / sizeof(_x[0]))
#endif

#define	NMAX_DISCONNECT		2048

static void         usage (void);
static void         show_clear_session (struct parse_result *, FILE *);
static void         monitor_session (struct parse_result *, FILE *);
static void         clear_session (u_int[], int, int, FILE *);
static void         fprint_who_brief (int, struct npppd_who *, FILE *);
static void         fprint_who_packets (int, struct npppd_who *, FILE *);
static void         fprint_who_all (int, struct npppd_who *, FILE *);
static const char  *peerstr (struct sockaddr *, char *, int);
static const char  *humanize_duration (uint32_t, char *, int);
static const char  *humanize_bytes (double, char *, int);
static bool         filter_match(struct parse_result *, struct npppd_who *);
static int          imsg_wait_command_completion (void);

static int             nflag = 0;
static struct imsgbuf  ctl_ibuf;
static struct imsg     ctl_imsg;

static void
usage(void)
{
	extern char		*__progname;

	fprintf(stderr,
	    "usage: %s [-n] [-s socket] command [arg ...]\n", __progname);
}

int
main(int argc, char *argv[])
{
	int			 ch, ctlsock = -1;
	struct parse_result	*result;
	struct sockaddr_un	 sun;
	const char		*npppd_ctlpath = NPPPD_SOCKET;

	while ((ch = getopt(argc, argv, "ns:")) != -1)
		switch (ch) {
		case 'n':
			nflag = 1;
			break;
		case 's':
			npppd_ctlpath = optarg;
			break;
		default:
			usage();
			exit(EXIT_FAILURE);
		}

	argc -= optind;
	argv += optind;

	if ((result = parse(argc, argv)) == NULL)
		exit(EXIT_FAILURE);

	if ((ctlsock = socket(AF_UNIX, SOCK_STREAM, 0)) == -1)
		err(EXIT_FAILURE, "socket");
	memset(&sun, 0, sizeof(sun));
	sun.sun_family = AF_UNIX;
	strlcpy(sun.sun_path, npppd_ctlpath, sizeof(sun.sun_path));
	if (connect(ctlsock, (struct sockaddr *)&sun, sizeof(sun)) == -1)
		err(EXIT_FAILURE, "connect");

	if (imsgbuf_init(&ctl_ibuf, ctlsock) == -1)
		err(EXIT_FAILURE, "imsgbuf_init");

	switch (result->action) {
	case SESSION_BRIEF:
	case SESSION_PKTS:
	case SESSION_ALL:
		show_clear_session(result, stdout);
		break;
	case CLEAR_SESSION:
		if (!result->has_ppp_id)
			show_clear_session(result, stdout);
		else {
			u_int ids[1];
			ids[0] = result->ppp_id;
			clear_session(ids, 1, 1, stdout);
		}
		break;
	case MONITOR_SESSION:
		monitor_session(result, stdout);
		break;
	case NONE:
		break;
	}

	exit(EXIT_SUCCESS);
}

static void
show_clear_session(struct parse_result *result, FILE *out)
{
	int                    i, n, ppp_id_idx;
	struct npppd_who_list *res;
	u_int                  ppp_id[NMAX_DISCONNECT];

	if (imsg_compose(&ctl_ibuf, IMSG_CTL_WHO, 0, 0, -1, NULL, 0) == -1)
		err(EXIT_FAILURE, "failed to compose a message\n");
	if (imsg_wait_command_completion() < 0)
		errx(EXIT_FAILURE, "failed to get response");
	if (ctl_imsg.hdr.type != IMSG_CTL_OK)
		errx(EXIT_FAILURE, "command was fail");
	n = ppp_id_idx = 0;
	while (imsg_wait_command_completion() == IMSG_PPP_START) {
		res = (struct npppd_who_list *)ctl_imsg.data;
		if (ctl_imsg.hdr.len - IMSG_HEADER_SIZE <
		    offsetof(struct npppd_who_list,
			    entry[res->entry_count])) {
			errx(1, "response size %d is too small for "
			    "the entry count %d",
			    (int)(ctl_imsg.hdr.len - IMSG_HEADER_SIZE),
			    res->entry_count);
		}
		for (i = 0; i < res->entry_count; i++, n++) {
			switch (result->action) {
			case SESSION_BRIEF:
				fprint_who_brief(n, &res->entry[i], out);
				break;
			case SESSION_PKTS:
				fprint_who_packets(n, &res->entry[i], out);
				break;
			case SESSION_ALL:
				if (filter_match(result, &res->entry[i]))
					fprint_who_all(n, &res->entry[i], out);
				break;
			case CLEAR_SESSION:
				if (filter_match(result, &res->entry[i])) {
					if (ppp_id_idx < nitems(ppp_id))
						ppp_id[ppp_id_idx] =
						    res->entry[i].ppp_id;
					ppp_id_idx++;
				}
				break;
			default:
				warnx("must not reached here");
				abort();
			}
		}
		if (!res->more_data)
			break;
	}
	if (result->action == CLEAR_SESSION) {
		if (ppp_id_idx > nitems(ppp_id))
			warnx(
			    "Disconnection for %d sessions has been requested, "
			    "but cannot disconnect only %d sessions because of "
			    "the implementation limit.",
			    ppp_id_idx, (int)nitems(ppp_id));
		clear_session(ppp_id, MINIMUM(ppp_id_idx, nitems(ppp_id)),
			ppp_id_idx, out);
	}
}

const char *bar =
"------------------------------------------------------------------------\n";
static void
monitor_session(struct parse_result *result, FILE *out)
{
	int                    i, n;
	struct npppd_who_list *res;

	if (imsg_compose(&ctl_ibuf, IMSG_CTL_MONITOR, 0, 0, -1, NULL, 0) == -1)
		err(EXIT_FAILURE, "failed to compose a message");
	if (imsg_wait_command_completion() < 0)
		errx(EXIT_FAILURE, "failed to get response");
	if (ctl_imsg.hdr.type != IMSG_CTL_OK)
		errx(EXIT_FAILURE, "command was fail");
	do {
		if (imsg_wait_command_completion() < 0)
			break;
		n = 0;
		if (ctl_imsg.hdr.type == IMSG_PPP_START ||
		    ctl_imsg.hdr.type == IMSG_PPP_STOP) {
			res = (struct npppd_who_list *)ctl_imsg.data;
			for (i = 0; i < res->entry_count; i++) {
				if (!filter_match(result, &res->entry[i]))
					continue;
				if (n == 0)
					fprintf(out, "PPP %s\n%s",
					    (ctl_imsg.hdr.type ==
						    IMSG_PPP_START)
						    ? "Started"
						    : "Stopped", bar);
				fprint_who_all(n++, &res->entry[i], out);
			}
			if (n > 0)
				fputs(bar, out);
		} else {
			warnx("received unknown message type = %d",
			    ctl_imsg.hdr.type);
			break;
		}
	} while (true);

	return;
}

static void
fprint_who_brief(int i, struct npppd_who *w, FILE *out)
{
	char buf[BUFSIZ];

	if (i == 0)
		fputs(
"Ppp Id     Assigned IPv4   Username             Proto Tunnel From\n"
"---------- --------------- -------------------- ----- ------------------------"
"-\n",
		    out);
	fprintf(out, "%10u %-15s %-20s %-5s %s\n", w->ppp_id,
	    inet_ntoa(w->framed_ip_address), w->username, w->tunnel_proto,
	    peerstr((struct sockaddr *)&w->tunnel_peer, buf, sizeof(buf)));
}

static void
fprint_who_packets(int i, struct npppd_who *w, FILE *out)
{
	if (i == 0)
		fputs(
"Ppd Id     Username             In(Kbytes/pkts/errs)    Out(Kbytes/pkts/errs)"
"\n"
"---------- -------------------- ----------------------- ----------------------"
"-\n",
		    out);
	fprintf(out, "%10u %-20s %9.1f %7u %5u %9.1f %7u %5u\n", w->ppp_id,
	    w->username,
	    (double)w->ibytes/1024, w->ipackets, w->ierrors,
	    (double)w->obytes/1024, w->opackets, w->oerrors);
}

static void
fprint_who_all(int i, struct npppd_who *w, FILE *out)
{
	struct tm  tm;
	char       ibytes_buf[48], obytes_buf[48], peer_buf[48], time_buf[48];
	char       dur_buf[48];

	localtime_r(&w->time, &tm);
	strftime(time_buf, sizeof(time_buf), "%Y/%m/%d %T", &tm);
	if (i != 0)
		fputs("\n", out);

	fprintf(out,
	    "Ppp Id = %u\n"
	    "          Ppp Id                  : %u\n"
	    "          Username                : %s\n"
	    "          Realm Name              : %s\n"
	    "          Concentrated Interface  : %s\n"
	    "          Assigned IPv4 Address   : %s\n"
	    "          MRU                     : %u\n"
	    "          Tunnel Protocol         : %s\n"
	    "          Tunnel From             : %s\n"
	    "          Start Time              : %s\n"
	    "          Elapsed Time            : %lu sec %s\n"
	    "          Input Bytes             : %llu%s\n"
	    "          Input Packets           : %lu\n"
	    "          Input Errors            : %lu (%.1f%%)\n"
	    "          Output Bytes            : %llu%s\n"
	    "          Output Packets          : %lu\n"
	    "          Output Errors           : %lu (%.1f%%)\n",
	    w->ppp_id, w->ppp_id, w->username, w->rlmname, w->ifname,
	    inet_ntoa(w->framed_ip_address), (u_int)w->mru, w->tunnel_proto,
	    peerstr((struct sockaddr *)&w->tunnel_peer, peer_buf,
		sizeof(peer_buf)), time_buf,
	    (unsigned long)w->duration_sec,
	    humanize_duration(w->duration_sec, dur_buf, sizeof(dur_buf)),
	    (unsigned long long)w->ibytes,
	    humanize_bytes((double)w->ibytes, ibytes_buf, sizeof(ibytes_buf)),
	    (unsigned long)w->ipackets,
	    (unsigned long)w->ierrors,
	    ((w->ipackets + w->ierrors) <= 0)
		? 0.0 : (100.0 * w->ierrors) / (w->ierrors + w->ipackets),
	    (unsigned long long)w->obytes,
	    humanize_bytes((double)w->obytes, obytes_buf, sizeof(obytes_buf)),
	    (unsigned long)w->opackets,
	    (unsigned long)w->oerrors,
	    ((w->opackets + w->oerrors) <= 0)
		? 0.0 : (100.0 * w->oerrors) / (w->oerrors + w->opackets));
}

/***********************************************************************
 * clear session
 ***********************************************************************/
static void
clear_session(u_int ppp_id[], int ppp_id_count, int total, FILE *out)
{
	int                               succ, fail, i, n, nmax;
	struct iovec                      iov[2];
	struct npppd_disconnect_request   req;
	struct npppd_disconnect_response *res;

	succ = fail = 0;
	if (ppp_id_count > 0) {
		nmax = (MAX_IMSGSIZE - IMSG_HEADER_SIZE -
		    offsetof(struct npppd_disconnect_request, ppp_id[0])) /
		    sizeof(u_int);
		for (i = 0; i < ppp_id_count; i += n) {
			n = MINIMUM(nmax, ppp_id_count - i);
			req.count = n;
			iov[0].iov_base = &req;
			iov[0].iov_len = offsetof(
			    struct npppd_disconnect_request, ppp_id[0]);
			iov[1].iov_base = &ppp_id[i];
			iov[1].iov_len = sizeof(u_int) * n;

			if (imsg_composev(&ctl_ibuf, IMSG_CTL_DISCONNECT, 0, 0,
			    -1, iov, 2) == -1)
				err(EXIT_FAILURE,
				    "Failed to compose a message");
			if (imsg_wait_command_completion() < 0)
				errx(EXIT_FAILURE, "failed to get response");
			if (ctl_imsg.hdr.type != IMSG_CTL_OK)
				errx(EXIT_FAILURE,
				    "Command was fail: msg type = %d",
				    ctl_imsg.hdr.type);
			if (ctl_imsg.hdr.len - IMSG_HEADER_SIZE <
			    sizeof(struct npppd_disconnect_response))
				err(EXIT_FAILURE, "response is corrupted");
			res = (struct npppd_disconnect_response *)ctl_imsg.data;
			succ += res->count;
		}
		fail = total - succ;
	}
	if (succ > 0)
		fprintf(out, "Successfully disconnected %d session%s.\n",
		    succ, (succ > 1)? "s" : "");
	if (fail > 0)
		fprintf(out, "Failed to disconnect %d session%s.\n",
		    fail, (fail > 1)? "s" : "");
	if (succ == 0 && fail == 0)
		fprintf(out, "No session to disconnect.\n");
}

/***********************************************************************
 * common functions
 ***********************************************************************/
static bool
filter_match(struct parse_result *result, struct npppd_who *who)
{
	if (result->has_ppp_id && result->ppp_id != who->ppp_id)
		return (false);

	switch (result->address.ss_family) {
	case AF_INET:
		if (((struct sockaddr_in *)&result->address)->sin_addr.
		    s_addr != who->framed_ip_address.s_addr)
			return (false);
		break;
	case AF_INET6:
		/* npppd doesn't support IPv6 yet */
		return (false);
	}

	if (result->interface != NULL &&
	    strcmp(result->interface, who->ifname) != 0)
		return (false);

	if (result->protocol != PROTO_UNSPEC &&
	    result->protocol != parse_protocol(who->tunnel_proto) )
		return (false);

	if (result->realm != NULL && strcmp(result->realm, who->rlmname) != 0)
		return (false);

	if (result->username != NULL &&
	    strcmp(result->username, who->username) != 0)
		return (false);

	return (true);
}

static const char *
peerstr(struct sockaddr *sa, char *buf, int lbuf)
{
	int   niflags, hasserv;
	char  hoststr[NI_MAXHOST], servstr[NI_MAXSERV];

	niflags = hasserv = 0;
	if (nflag)
		niflags |= NI_NUMERICHOST;
	if (sa->sa_family == AF_INET || sa->sa_family ==AF_INET6) {
		hasserv = 1;
		niflags |= NI_NUMERICSERV;
	}

	if (sa->sa_family == AF_LINK)
		snprintf(hoststr, sizeof(hoststr),
		    "%02x:%02x:%02x:%02x:%02x:%02x",
		    LLADDR((struct sockaddr_dl *)sa)[0] & 0xff,
		    LLADDR((struct sockaddr_dl *)sa)[1] & 0xff,
		    LLADDR((struct sockaddr_dl *)sa)[2] & 0xff,
		    LLADDR((struct sockaddr_dl *)sa)[3] & 0xff,
		    LLADDR((struct sockaddr_dl *)sa)[4] & 0xff,
		    LLADDR((struct sockaddr_dl *)sa)[5] & 0xff);
	else
		getnameinfo(sa, sa->sa_len, hoststr, sizeof(hoststr), servstr,
		    sizeof(servstr), niflags);

	strlcpy(buf, hoststr, lbuf);
	if (hasserv) {
		strlcat(buf, ":", lbuf);
		strlcat(buf, servstr, lbuf);
	}

	return (buf);
}

static const char *
humanize_duration(uint32_t sec, char *buf, int lbuf)
{
	char  fbuf[128];
	int   hour, min;

	hour = sec / (60 * 60);
	min = sec / 60;
	min %= 60;

	if (lbuf <= 0)
		return (buf);
	buf[0] = '\0';
	if (hour || min) {
		strlcat(buf, "(", lbuf);
		if (hour) {
			snprintf(fbuf, sizeof(fbuf),
			    "%d hour%s", hour, (hour > 1)? "s" : "");
			strlcat(buf, fbuf, lbuf);
		}
		if (hour && min)
			strlcat(buf, " and ", lbuf);
		if (min) {
			snprintf(fbuf, sizeof(fbuf),
			    "%d minute%s", min, (min > 1)? "s" : "");
			strlcat(buf, fbuf, lbuf);
		}
		strlcat(buf, ")", lbuf);
	}

	return (buf);
}

static const char *
humanize_bytes(double val, char *buf, int lbuf)
{
	if (lbuf <= 0)
		return (buf);

	if (val >= 1000 * 1024 * 1024)
		snprintf(buf, lbuf, " (%.1f GB)",
		    (double)val / (1024 * 1024 * 1024));
	else if (val >= 1000 * 1024)
		snprintf(buf, lbuf, " (%.1f MB)", (double)val / (1024 * 1024));
	else if (val >= 1000)
		snprintf(buf, lbuf, " (%.1f KB)", (double)val / 1024);
	else
		buf[0] = '\0';

	return (buf);
}

static int
imsg_wait_command_completion(void)
{
	int  n;

	if (imsgbuf_flush(&ctl_ibuf) == -1)
		return (-1);
	do {
		if ((n = imsg_get(&ctl_ibuf, &ctl_imsg)) == -1)
			return (-1);
		if (n != 0)
			break;
		if (imsgbuf_read(&ctl_ibuf) != 1)
			return (-1);
	} while (1);

	return (ctl_imsg.hdr.type);
}
