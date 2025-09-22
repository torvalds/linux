/*	$OpenBSD: iscsid.c,v 1.25 2025/01/22 16:06:36 claudio Exp $ */

/*
 * Copyright (c) 2009 Claudio Jeker <claudio@openbsd.org>
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
#include <sys/sysctl.h>
#include <sys/time.h>
#include <sys/uio.h>

#include <err.h>
#include <event.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "iscsid.h"
#include "log.h"

void		main_sig_handler(int, short, void *);
__dead void	usage(void);
void		shutdown_cb(int, short, void *);

struct event exit_ev;
int exit_rounds;
#define ISCSI_EXIT_WAIT 5

const struct session_params	iscsi_sess_defaults = {
	.MaxBurstLength = 262144,
	.FirstBurstLength = 65536,
	.DefaultTime2Wait = 2,
	.DefaultTime2Retain = 20,
	.MaxOutstandingR2T = 1,
	.MaxConnections = 1,
	.InitialR2T = 1,
	.ImmediateData = 1,
	.DataPDUInOrder = 1,
	.DataSequenceInOrder = 1,
	.ErrorRecoveryLevel = 0,
};

const struct connection_params	iscsi_conn_defaults = {
	.MaxRecvDataSegmentLength = 8192,
	.HeaderDigest = DIGEST_NONE,
	.DataDigest = DIGEST_NONE,
};

int
main(int argc, char *argv[])
{
	struct event ev_sigint, ev_sigterm, ev_sighup;
	struct passwd *pw;
	char *ctrlsock = ISCSID_CONTROL;
	char *vscsidev = ISCSID_DEVICE;
	int name[] = { CTL_KERN, KERN_PROC_NOBROADCASTKILL, 0 };
	int ch, debug = 0, verbose = 0, nobkill = 1;

	log_procname = getprogname();

	log_init(1);    /* log to stderr until daemonized */
	log_verbose(1);

	while ((ch = getopt(argc, argv, "dn:s:v")) != -1) {
		switch (ch) {
		case 'd':
			debug = 1;
			break;
		case 'n':
			vscsidev = optarg;
			break;
		case 's':
			ctrlsock = optarg;
			break;
		case 'v':
			verbose = 1;
			break;
		default:
			usage();
			/* NOTREACHED */
		}
	}

	argc -= optind;
	argv += optind;

	if (argc > 0)
		usage();

	/* check for root privileges  */
	if (geteuid())
		errx(1, "need root privileges");

	log_init(debug);
	log_verbose(verbose);

	if (control_init(ctrlsock) == -1)
		fatalx("control socket setup failed");

	if (!debug)
		daemon(1, 0);
	log_info("startup");

	name[2] = getpid();
	if (sysctl(name, 3, NULL, 0, &nobkill, sizeof(nobkill)) != 0)
		fatal("sysctl");

	event_init();
	vscsi_open(vscsidev);

	/* chroot and drop to iscsid user */
	if ((pw = getpwnam(ISCSID_USER)) == NULL)
		errx(1, "unknown user %s", ISCSID_USER);

	if (chroot(pw->pw_dir) == -1)
		fatal("chroot");
	if (chdir("/") == -1)
		fatal("chdir(\"/\")");
	if (setgroups(1, &pw->pw_gid) ||
	    setresgid(pw->pw_gid, pw->pw_gid, pw->pw_gid) ||
	    setresuid(pw->pw_uid, pw->pw_uid, pw->pw_uid))
		fatal("can't drop privileges");

	/* setup signal handler */
	signal_set(&ev_sigint, SIGINT, main_sig_handler, NULL);
	signal_set(&ev_sigterm, SIGTERM, main_sig_handler, NULL);
	signal_set(&ev_sighup, SIGHUP, main_sig_handler, NULL);
	signal_add(&ev_sigint, NULL);
	signal_add(&ev_sigterm, NULL);
	signal_add(&ev_sighup, NULL);
	signal(SIGPIPE, SIG_IGN);

	control_event_init();
	initiator_init();

	event_dispatch();

	/* do some cleanup on the way out */
	control_cleanup(ctrlsock);
	initiator_cleanup();
	log_info("exiting.");
	return 0;
}

void
shutdown_cb(int fd, short event, void *arg)
{
	struct timeval tv;

	if (exit_rounds++ >= ISCSI_EXIT_WAIT || initiator_isdown())
		event_loopexit(NULL);

	timerclear(&tv);
	tv.tv_sec = 1;

	if (evtimer_add(&exit_ev, &tv) == -1)
		fatal("shutdown_cb");
}

void
main_sig_handler(int sig, short event, void *arg)
{
	struct timeval tv;

	/* signal handler rules don't apply, libevent decouples for us */
	switch (sig) {
	case SIGTERM:
	case SIGINT:
	case SIGHUP:
		initiator_shutdown();
		evtimer_set(&exit_ev, shutdown_cb, NULL);
		timerclear(&tv);
		if (evtimer_add(&exit_ev, &tv) == -1)
			fatal("main_sig_handler");
		break;
	default:
		fatalx("unexpected signal");
		/* NOTREACHED */
	}
}

__dead void
usage(void)
{
	extern char *__progname;

	fprintf(stderr, "usage: %s [-dv] [-n device] [-s socket]\n",
	    __progname);
	exit(1);
}

void
iscsid_ctrl_dispatch(void *ch, struct pdu *pdu)
{
	struct ctrlmsghdr *cmh;
	struct initiator_config *ic;
	struct session_head *sh;
	struct session_config *sc;
	struct session *s;
	struct session_poll p = { 0 };
	int *valp;

	cmh = pdu_getbuf(pdu, NULL, 0);
	if (cmh == NULL)
		goto done;

	switch (cmh->type) {
	case CTRL_INITIATOR_CONFIG:
		if (cmh->len[0] != sizeof(*ic)) {
			log_warnx("CTRL_INITIATOR_CONFIG bad size");
			control_compose(ch, CTRL_FAILURE, NULL, 0);
			break;
		}
		ic = pdu_getbuf(pdu, NULL, 1);
		initiator_set_config(ic);
		control_compose(ch, CTRL_SUCCESS, NULL, 0);
		break;
	case CTRL_SESSION_CONFIG:
		if (cmh->len[0] != sizeof(*sc)) {
			log_warnx("CTRL_SESSION_CONFIG bad size");
			control_compose(ch, CTRL_FAILURE, NULL, 0);
			break;
		}
		sc = pdu_getbuf(pdu, NULL, 1);
		if (cmh->len[1])
			sc->TargetName = pdu_getbuf(pdu, NULL, 2);
		else if (sc->SessionType != SESSION_TYPE_DISCOVERY) {
			control_compose(ch, CTRL_FAILURE, NULL, 0);
			goto done;
		} else
			sc->TargetName = NULL;
		if (cmh->len[2])
			sc->InitiatorName = pdu_getbuf(pdu, NULL, 3);
		else
			sc->InitiatorName = NULL;

		s = initiator_find_session(sc->SessionName);
		if (s == NULL) {
			s = initiator_new_session(sc->SessionType);
			if (s == NULL) {
				control_compose(ch, CTRL_FAILURE, NULL, 0);
				goto done;
			}
		}

		session_config(s, sc);
		if (s->state == SESS_INIT)
			session_fsm(&s->sev, SESS_EV_START, 0);

		control_compose(ch, CTRL_SUCCESS, NULL, 0);
		break;
	case CTRL_LOG_VERBOSE:
		if (cmh->len[0] != sizeof(int)) {
			log_warnx("CTRL_LOG_VERBOSE bad size");
			control_compose(ch, CTRL_FAILURE, NULL, 0);
			break;
		}
		valp = pdu_getbuf(pdu, NULL, 1);
		log_verbose(*valp);
		control_compose(ch, CTRL_SUCCESS, NULL, 0);
		break;
	case CTRL_VSCSI_STATS:
		control_compose(ch, CTRL_VSCSI_STATS, vscsi_stats(),
		    sizeof(struct vscsi_stats));
		break;
	case CTRL_SHOW_SUM:
		ic = initiator_get_config();
		control_compose(ch, CTRL_INITIATOR_CONFIG, ic, sizeof(*ic));

		sh = initiator_get_sessions();
		TAILQ_FOREACH(s, sh, entry) {
			struct ctrldata cdv[3];
			bzero(cdv, sizeof(cdv));

			cdv[0].buf = &s->config;
			cdv[0].len = sizeof(s->config);

			if (s->config.TargetName) {
				cdv[1].buf = s->config.TargetName;
				cdv[1].len =
				    strlen(s->config.TargetName) + 1;
			}
			if (s->config.InitiatorName) {
				cdv[2].buf = s->config.InitiatorName;
				cdv[2].len =
				    strlen(s->config.InitiatorName) + 1;
			}

			control_build(ch, CTRL_SESSION_CONFIG,
			    nitems(cdv), cdv);
		}

		control_compose(ch, CTRL_SUCCESS, NULL, 0);
		break;
	case CTRL_SESS_POLL:
		sh = initiator_get_sessions();
		TAILQ_FOREACH(s, sh, entry)
			poll_session(&p, s);
		poll_finalize(&p);
		control_compose(ch, CTRL_SESS_POLL, &p, sizeof(p));
		break;
	default:
		log_warnx("unknown control message type %d", cmh->type);
		control_compose(ch, CTRL_FAILURE, NULL, 0);
		break;
	}

done:
	pdu_free(pdu);
}

#define MERGE_MIN(r, a, b, v)				\
	r->v = (a->v < b->v ? a->v : b->v)
#define MERGE_MAX(r, a, b, v)				\
	r->v = (a->v > b->v ? a->v : b->v)
#define MERGE_OR(r, a, b, v)				\
	r->v = (a->v || b->v)
#define MERGE_AND(r, a, b, v)				\
	r->v = (a->v && b->v)

void
iscsi_merge_sess_params(struct session_params *res,
    struct session_params *mine, struct session_params *his)
{
	memset(res, 0, sizeof(*res));

	MERGE_MIN(res, mine, his, MaxBurstLength);
	MERGE_MIN(res, mine, his, FirstBurstLength);
	MERGE_MAX(res, mine, his, DefaultTime2Wait);
	MERGE_MIN(res, mine, his, DefaultTime2Retain);
	MERGE_MIN(res, mine, his, MaxOutstandingR2T);
	res->TargetPortalGroupTag = his->TargetPortalGroupTag;
	MERGE_MIN(res, mine, his, MaxConnections);
	MERGE_OR(res, mine, his, InitialR2T);
	MERGE_AND(res, mine, his, ImmediateData);
	MERGE_OR(res, mine, his, DataPDUInOrder);
	MERGE_OR(res, mine, his, DataSequenceInOrder);
	MERGE_MIN(res, mine, his, ErrorRecoveryLevel);

}

void
iscsi_merge_conn_params(struct connection_params *res,
    struct connection_params *mine, struct connection_params *his)
{
	int mask;

	memset(res, 0, sizeof(*res));

	res->MaxRecvDataSegmentLength = his->MaxRecvDataSegmentLength;

	/* for digest select first bit that is set in both his and mine */
	mask = mine->HeaderDigest & his->HeaderDigest;
	mask = ffs(mask) - 1;
	if (mask == -1)
		res->HeaderDigest = 0;
	else
		res->HeaderDigest = 1 << mask;
		
	mask = mine->DataDigest & his->DataDigest;
	mask = ffs(mask) - 1;
	if (mask == -1)
		res->DataDigest = 0;
	else
		res->DataDigest = 1 << mask;
}

#undef MERGE_MIN
#undef MERGE_MAX
#undef MERGE_OR
#undef MERGE_AND
