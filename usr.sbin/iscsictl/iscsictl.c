/*	$OpenBSD: iscsictl.c,v 1.13 2023/02/21 15:45:40 mbuhl Exp $ */

/*
 * Copyright (c) 2010 Claudio Jeker <claudio@openbsd.org>
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
#include <sys/uio.h>
#include <sys/un.h>

#include <event.h>
#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <util.h>

#include "iscsid.h"
#include "iscsictl.h"

__dead void	 usage(void);
void		 run(void);
void		 run_command(struct pdu *);
struct pdu	*ctl_getpdu(char *, size_t);
int		 ctl_sendpdu(int, struct pdu *);
void		 show_config(struct ctrlmsghdr *, struct pdu *);
void		 show_vscsi_stats(struct ctrlmsghdr *, struct pdu *);
void             poll_and_wait(void);
void             poll_session_status(void);
void             register_poll(struct ctrlmsghdr *, struct pdu *);
void             poll_print(struct session_poll *);

char		cbuf[CONTROL_READ_SIZE];

struct control {
	struct pduq	channel;
	int		fd;
} control;


struct session_poll poll_result;
#define POLL_DELAY_SEC	1
#define POLL_ATTEMPTS	10

__dead void
usage(void)
{
	extern char *__progname;

	fprintf(stderr,"usage: %s [-s socket] command [argument ...]\n",
	    __progname);
	exit(1);
}

int
main (int argc, char* argv[])
{
	struct sockaddr_un sun;
	struct session_config sc;
	struct parse_result *res;
	char *confname = ISCSID_CONFIG;
	char *sockname = ISCSID_CONTROL;
	struct session_ctlcfg *s;
	struct iscsi_config *cf;
	int ch, poll = 0, val = 0;

	/* check flags */
	while ((ch = getopt(argc, argv, "f:s:")) != -1) {
		switch (ch) {
		case 'f':
			confname = optarg;
			break;
		case 's':
			sockname = optarg;
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

	/* connect to iscsid control socket */
	TAILQ_INIT(&control.channel);
	if ((control.fd = socket(AF_UNIX, SOCK_SEQPACKET, 0)) == -1)
		err(1, "socket");

	bzero(&sun, sizeof(sun));
	sun.sun_family = AF_UNIX;
	strlcpy(sun.sun_path, sockname, sizeof(sun.sun_path));

	if (connect(control.fd, (struct sockaddr *)&sun, sizeof(sun)) == -1)
		err(1, "connect: %s", sockname);

	if (pledge("stdio rpath dns", NULL) == -1)
		err(1, "pledge");

	switch (res->action) {
	case NONE:
	case LOG_VERBOSE:
		val = 1;
		/* FALLTHROUGH */
	case LOG_BRIEF:
		if (control_compose(NULL, CTRL_LOG_VERBOSE,
		    &val, sizeof(int)) == -1)
			err(1, "control_compose");
		break;
	case SHOW_SUM:
		if (control_compose(NULL, CTRL_SHOW_SUM, NULL, 0) == -1)
			err(1, "control_compose");
		break;
	case SHOW_SESS:
		usage();
		/* NOTREACHED */
	case SHOW_VSCSI_STATS:
		if (control_compose(NULL, CTRL_VSCSI_STATS, NULL, 0) == -1)
			err(1, "control_compose");
		break;
	case RELOAD:
		if ((cf = parse_config(confname)) == NULL)
			errx(1, "errors while loading configuration file.");
		if (cf->initiator.isid_base != 0) {
			if (control_compose(NULL, CTRL_INITIATOR_CONFIG,
			    &cf->initiator, sizeof(cf->initiator)) == -1)
				err(1, "control_compose");
		}

		SIMPLEQ_FOREACH(s, &cf->sessions, entry) {
			struct ctrldata cdv[3];
			bzero(cdv, sizeof(cdv));

			cdv[0].buf = &s->session;
			cdv[0].len = sizeof(s->session);

			if (s->session.TargetName) {
				cdv[1].buf = s->session.TargetName;
				cdv[1].len =
				    strlen(s->session.TargetName) + 1;
			}
			if (s->session.InitiatorName) {
				cdv[2].buf = s->session.InitiatorName;
				cdv[2].len =
				    strlen(s->session.InitiatorName) + 1;
			}

			if (control_build(NULL, CTRL_SESSION_CONFIG,
			    nitems(cdv), cdv) == -1)
				err(1, "control_build");
		}

		/* Reloading, so poll for connection establishment. */
		poll = 1;
		break;
	case DISCOVERY:
		printf("discover %s\n", log_sockaddr(&res->addr));

		bzero(&sc, sizeof(sc));
		snprintf(sc.SessionName, sizeof(sc.SessionName),
		    "discovery.%d", (int)getpid());
		bcopy(&res->addr, &sc.connection.TargetAddr, res->addr.ss_len);
		sc.SessionType = SESSION_TYPE_DISCOVERY;

		if (control_compose(NULL, CTRL_SESSION_CONFIG,
		    &sc, sizeof(sc)) == -1)
			err(1, "control_compose");
	}

	run();

	if (poll)
		poll_and_wait();

	close(control.fd);
	return (0);
}

void
control_queue(void *ch, struct pdu *pdu)
{
	TAILQ_INSERT_TAIL(&control.channel, pdu, entry);
}

void
run(void)
{
	struct pdu *pdu;

	while ((pdu = TAILQ_FIRST(&control.channel)) != NULL) {
		TAILQ_REMOVE(&control.channel, pdu, entry);
		run_command(pdu);
	}
}

void
run_command(struct pdu *pdu)
{
	struct ctrlmsghdr *cmh;
	int done = 0;
	ssize_t n;

	if (ctl_sendpdu(control.fd, pdu) == -1)
		err(1, "send");
	while (!done) {
		if ((n = recv(control.fd, cbuf, sizeof(cbuf), 0)) == -1 &&
		    !(errno == EAGAIN || errno == EINTR))
			err(1, "recv");

		if (n == 0)
			errx(1, "connection to iscsid closed");

		pdu = ctl_getpdu(cbuf, n);
		cmh = pdu_getbuf(pdu, NULL, 0);
		if (cmh == NULL)
			break;
		switch (cmh->type) {
		case CTRL_SUCCESS:
			printf("command successful\n");
			done = 1;
			break;
		case CTRL_FAILURE:
			printf("command failed\n");
			done = 1;
			break;
		case CTRL_INPROGRESS:
			printf("command in progress...\n");
			break;
		case CTRL_INITIATOR_CONFIG:
		case CTRL_SESSION_CONFIG:
			show_config(cmh, pdu);
			break;
		case CTRL_VSCSI_STATS:
			show_vscsi_stats(cmh, pdu);
			done = 1;
			break;
		case CTRL_SESS_POLL:
			register_poll(cmh, pdu);
			done = 1;
			break;

		}
	}
}

struct pdu *
ctl_getpdu(char *buf, size_t len)
{
	struct pdu *p;
	struct ctrlmsghdr *cmh;
	void *data;
	size_t n;
	int i;

	if (len < sizeof(*cmh))
		return NULL;

	if (!(p = pdu_new()))
		return NULL;

	n = sizeof(*cmh);
	cmh = pdu_alloc(n);
	bcopy(buf, cmh, n);
	buf += n;
	len -= n;

	if (pdu_addbuf(p, cmh, n, 0)) {
		free(cmh);
fail:
		pdu_free(p);
		return NULL;
	}

	for (i = 0; i < 3; i++) {
		n = cmh->len[i];
		if (n == 0)
			continue;
		if (PDU_LEN(n) > len)
			goto fail;
		if (!(data = pdu_alloc(n)))
			goto fail;
		bcopy(buf, data, n);
		if (pdu_addbuf(p, data, n, i + 1)) {
			free(data);
			goto fail;
		}
		buf += PDU_LEN(n);
		len -= PDU_LEN(n);
	}

	return p;
}

int
ctl_sendpdu(int fd, struct pdu *pdu)
{
	struct iovec iov[PDU_MAXIOV];
	struct msghdr msg;
	unsigned int niov = 0;

	for (niov = 0; niov < PDU_MAXIOV; niov++) {
		iov[niov].iov_base = pdu->iov[niov].iov_base;
		iov[niov].iov_len = pdu->iov[niov].iov_len;
	}
	bzero(&msg, sizeof(msg));
	msg.msg_iov = iov;
	msg.msg_iovlen = niov;
	if (sendmsg(fd, &msg, 0) == -1)
		return -1;
	return 0;
}

void
show_config(struct ctrlmsghdr *cmh, struct pdu *pdu)
{
	struct initiator_config	*ic;
	struct session_config	*sc;
	char *name;

	switch (cmh->type) {
	case CTRL_INITIATOR_CONFIG:
		if (cmh->len[0] != sizeof(*ic))
			errx(1, "bad size of response");
		ic = pdu_getbuf(pdu, NULL, 1);
		if (ic == NULL)
			return;

		printf("Initiator: ISID base %x qualifier %hx\n",
		    ic->isid_base, ic->isid_qual);
		break;
	case CTRL_SESSION_CONFIG:
		if (cmh->len[0] != sizeof(*sc))
			errx(1, "bad size of response");
		sc = pdu_getbuf(pdu, NULL, 1);
		if (sc == NULL)
			return;

		printf("\nSession '%s':%s\n", sc->SessionName,
		    sc->disabled ? " disabled" : "");
		printf("    SessionType: %s\tMaxConnections: %hd\n",
		    sc->SessionType == SESSION_TYPE_DISCOVERY ? "discovery" :
		    "normal", sc->MaxConnections);
		if ((name = pdu_getbuf(pdu, NULL, 2)))
			printf("    TargetName: %s\n", name);
		printf("    TargetAddr: %s\n",
		    log_sockaddr(&sc->connection.TargetAddr));
		if ((name = pdu_getbuf(pdu, NULL, 3)))
			printf("    InitiatorName: %s\n", name);
		printf("    InitiatorAddr: %s\n",
		    log_sockaddr(&sc->connection.LocalAddr));
		break;
	}
}

void
show_vscsi_stats(struct ctrlmsghdr *cmh, struct pdu *pdu)
{
	struct vscsi_stats *vs;
	char buf[FMT_SCALED_STRSIZE];

	if (cmh->len[0] != sizeof(struct vscsi_stats))
		errx(1, "bad size of response");
	vs = pdu_getbuf(pdu, NULL, 1);
	if (vs == NULL)
		return;

	printf("VSCSI ioctl statistics:\n");
	printf("%u probe calls and %u detach calls\n",
	    vs->cnt_probe, vs->cnt_detach);
	printf("%llu I2T calls (%llu read, %llu writes)\n",
	    vs->cnt_i2t,
	    vs->cnt_i2t_dir[1], 
	    vs->cnt_i2t_dir[2]);

	if (fmt_scaled(vs->bytes_rd, buf) != 0)
		(void)strlcpy(buf, "NaN", sizeof(buf));
	printf("%llu data reads (%s bytes read)\n", vs->cnt_read, buf);
	if (fmt_scaled(vs->bytes_wr, buf) != 0)
		(void)strlcpy(buf, "NaN", sizeof(buf));
	printf("%llu data writes (%s bytes written)\n", vs->cnt_write, buf);

	printf("%llu T2I calls (%llu done, %llu sense errors, %llu errors)\n",
	    vs->cnt_t2i,
	    vs->cnt_t2i_status[0], 
	    vs->cnt_t2i_status[1], 
	    vs->cnt_t2i_status[2]);
}

void
poll_session_status(void)
{
	struct pdu *pdu;

	if (control_compose(NULL, CTRL_SESS_POLL, NULL, 0) == -1)
		err(1, "control_compose");

	while ((pdu = TAILQ_FIRST(&control.channel)) != NULL) {
		TAILQ_REMOVE(&control.channel, pdu, entry);
		run_command(pdu);
	}
}

void
poll_and_wait(void)
{
	int attempts;

	printf("waiting for config to settle..");
	fflush(stdout);

	for (attempts = 0; attempts < POLL_ATTEMPTS; attempts++) {

		poll_session_status();

		/* Poll says we are good to go. */
		if (poll_result.sess_conn_status != 0) {
			printf("ok\n");
			/* wait a bit longer so all is settled. */
			sleep(POLL_DELAY_SEC);
			return;
		}

		/* Poll says we should wait... */
		printf(".");
		fflush(stdout);
		sleep(POLL_DELAY_SEC);
	}

	printf("giving up.\n");

	poll_print(&poll_result);
}

void
register_poll(struct ctrlmsghdr *cmh, struct pdu *pdu)
{
	if (cmh->len[0] != sizeof(poll_result))
		errx(1, "poll: bad size of response");

	poll_result = *((struct session_poll *)pdu_getbuf(pdu, NULL, 1));
}

void
poll_print(struct session_poll *p)
{
	printf("Configured sessions: %d\n", p->session_count);
	printf("Sessions initializing: %d\n", p->session_init_count);
	printf("Sessions started/failed: %d\n", p->session_running_count);
	printf("Sessions logged in: %d\n", p->conn_logged_in_count);
	printf("Sessions with failed connections: %d\n", p->conn_failed_count);
	printf("Sessions still attempting to connect: %d\n",
	    p->conn_waiting_count);
}
