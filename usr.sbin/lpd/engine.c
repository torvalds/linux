/*	$OpenBSD: engine.c,v 1.4 2024/11/21 13:34:51 claudio Exp $	*/

/*
 * Copyright (c) 2017 Eric Faurot <eric@openbsd.org>
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

#include <pwd.h>
#include <stdlib.h>
#include <signal.h>
#include <syslog.h>
#include <unistd.h>

#include "lpd.h"
#include "lp.h"

#include "log.h"
#include "proc.h"

static void engine_shutdown(void);
static void engine_dispatch_priv(struct imsgproc *, struct imsg *, void *);
static void engine_dispatch_frontend(struct imsgproc *, struct imsg *, void *);

char *lpd_hostname;

void
engine(int debug, int verbose)
{
	struct passwd *pw;

	/* Early initialisation. */
	log_init(debug, LOG_LPR);
	log_setverbose(verbose);
	log_procinit("engine");
	setproctitle("engine");

	if ((lpd_hostname = malloc(HOST_NAME_MAX+1)) == NULL)
		fatal("%s: malloc", __func__);
	gethostname(lpd_hostname, HOST_NAME_MAX + 1);

	/* Drop privileges. */
	if ((pw = getpwnam(LPD_USER)) == NULL)
		fatal("%s: getpwnam: %s", __func__, LPD_USER);

	if (setgroups(1, &pw->pw_gid) ||
	    setresgid(pw->pw_gid, pw->pw_gid, pw->pw_gid) ||
	    setresuid(pw->pw_uid, pw->pw_uid, pw->pw_uid))
		fatal("%s: cannot drop privileges", __func__);

	/* We need proc for kill(2) in lp_getcurrtask(). */
	if (pledge("stdio rpath wpath cpath flock dns sendfd recvfd proc",
	    NULL) == -1)
		fatal("%s: pledge", __func__);

	event_init();

	signal(SIGPIPE, SIG_IGN);

	/* Setup parent imsg socket. */
	p_priv = proc_attach(PROC_PRIV, 3);
	if (p_priv == NULL)
		fatal("%s: proc_attach", __func__);
	proc_setcallback(p_priv, engine_dispatch_priv, NULL);
	proc_enable(p_priv);

	event_dispatch();

	engine_shutdown();
}

static void
engine_shutdown()
{
	lpr_shutdown();

	log_debug("exiting");

	exit(0);
}

static void
engine_dispatch_priv(struct imsgproc *proc, struct imsg *imsg, void *arg)
{
	struct lp_printer lp;
	int fd;

	if (imsg == NULL) {
		log_debug("%s: imsg connection lost", __func__);
		event_loopexit(NULL);
		return;
	}

	if (log_getverbose() > LOGLEVEL_IMSG)
		log_imsg(proc, imsg);

	switch (imsg->hdr.type) {
	case IMSG_SOCK_FRONTEND:
		m_end(proc);

		if ((fd = imsg_get_fd(imsg)) == -1)
			fatalx("failed to receive frontend socket");

		p_frontend = proc_attach(PROC_FRONTEND, fd);
		proc_setcallback(p_frontend, engine_dispatch_frontend, NULL);
		proc_enable(p_frontend);
		break;

	case IMSG_CONF_START:
		m_end(proc);
		break;

	case IMSG_CONF_END:
		m_end(proc);

		/* Fork a printer process for every queue. */
		while (lp_scanprinters(&lp) == 1) {
			lpr_printjob(lp.lp_name);
			lp_clearprinter(&lp);
		}
		break;

	default:
		fatalx("%s: unexpected imsg %s", __func__,
		    log_fmt_imsgtype(imsg->hdr.type));
	}
}

static void
engine_dispatch_frontend(struct imsgproc *proc, struct imsg *imsg, void *arg)
{
	if (imsg == NULL) {
		log_debug("%s: imsg connection lost", __func__);
		event_loopexit(NULL);
		return;
	}

	if (log_getverbose() > LOGLEVEL_IMSG)
		log_imsg(proc, imsg);

	switch (imsg->hdr.type) {
	case IMSG_GETADDRINFO:
	case IMSG_GETNAMEINFO:
		resolver_dispatch_request(proc, imsg);
		break;

	case IMSG_LPR_ALLOWEDHOST:
	case IMSG_LPR_DISPLAYQ:
	case IMSG_LPR_PRINTJOB:
	case IMSG_LPR_RECVJOB:
	case IMSG_LPR_RECVJOB_CLEAR:
	case IMSG_LPR_RECVJOB_CF:
	case IMSG_LPR_RECVJOB_DF:
	case IMSG_LPR_RECVJOB_COMMIT:
	case IMSG_LPR_RECVJOB_ROLLBACK:
	case IMSG_LPR_RMJOB:
		lpr_dispatch_frontend(proc, imsg);
		break;

	default:
		fatalx("%s: unexpected imsg %s", __func__,
		    log_fmt_imsgtype(imsg->hdr.type));
	}
}
