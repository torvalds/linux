/*	$OpenBSD: dispatcher.c,v 1.7 2022/02/18 16:57:36 millert Exp $	*/

/*
 * Copyright (c) 2014 Gilles Chehade <gilles@poolp.org>
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
#include <signal.h>
#include <unistd.h>

#include "smtpd.h"
#include "log.h"

void mda_imsg(struct mproc *, struct imsg *);
void mta_imsg(struct mproc *, struct imsg *);
void smtp_imsg(struct mproc *, struct imsg *);

static void dispatcher_shutdown(void);

void
dispatcher_imsg(struct mproc *p, struct imsg *imsg)
{
	struct msg	m;
	int		v;

	if (imsg == NULL)
		dispatcher_shutdown();

	switch (imsg->hdr.type) {

	case IMSG_GETADDRINFO:
	case IMSG_GETADDRINFO_END:
	case IMSG_GETNAMEINFO:
	case IMSG_RES_QUERY:
		resolver_dispatch_result(p, imsg);
		return;

	case IMSG_CONF_START:
		return;
	case IMSG_CONF_END:
		smtp_configure();
		return;
	case IMSG_CTL_VERBOSE:
		m_msg(&m, imsg);
		m_get_int(&m, &v);
		m_end(&m);
		log_trace_verbose(v);
		return;
	case IMSG_CTL_PROFILE:
		m_msg(&m, imsg);
		m_get_int(&m, &v);
		m_end(&m);
		profiling = v;
		return;

	/* smtp imsg */
	case IMSG_SMTP_CHECK_SENDER:
	case IMSG_SMTP_EXPAND_RCPT:
	case IMSG_SMTP_LOOKUP_HELO:
	case IMSG_SMTP_AUTHENTICATE:
	case IMSG_SMTP_MESSAGE_COMMIT:
	case IMSG_SMTP_MESSAGE_CREATE:
	case IMSG_SMTP_MESSAGE_OPEN:
	case IMSG_FILTER_SMTP_PROTOCOL:
	case IMSG_FILTER_SMTP_DATA_BEGIN:
	case IMSG_QUEUE_ENVELOPE_SUBMIT:
	case IMSG_QUEUE_ENVELOPE_COMMIT:
	case IMSG_QUEUE_SMTP_SESSION:
	case IMSG_CTL_SMTP_SESSION:
	case IMSG_CTL_PAUSE_SMTP:
	case IMSG_CTL_RESUME_SMTP:
		smtp_imsg(p, imsg);
		return;

        /* mta imsg */
	case IMSG_QUEUE_TRANSFER:
	case IMSG_MTA_OPEN_MESSAGE:
	case IMSG_MTA_LOOKUP_CREDENTIALS:
	case IMSG_MTA_LOOKUP_SMARTHOST:
	case IMSG_MTA_LOOKUP_SOURCE:
	case IMSG_MTA_LOOKUP_HELO:
	case IMSG_MTA_DNS_HOST:
	case IMSG_MTA_DNS_HOST_END:
	case IMSG_MTA_DNS_MX_PREFERENCE:
	case IMSG_CTL_RESUME_ROUTE:
	case IMSG_CTL_MTA_SHOW_HOSTS:
	case IMSG_CTL_MTA_SHOW_RELAYS:
	case IMSG_CTL_MTA_SHOW_ROUTES:
	case IMSG_CTL_MTA_SHOW_HOSTSTATS:
	case IMSG_CTL_MTA_BLOCK:
	case IMSG_CTL_MTA_UNBLOCK:
	case IMSG_CTL_MTA_SHOW_BLOCK:
		mta_imsg(p, imsg);
		return;

        /* mda imsg */
	case IMSG_MDA_LOOKUP_USERINFO:
	case IMSG_QUEUE_DELIVER:
	case IMSG_MDA_OPEN_MESSAGE:
	case IMSG_MDA_FORK:
	case IMSG_MDA_DONE:
		mda_imsg(p, imsg);
		return;
	default:
		break;
	}

	fatalx("session_imsg: unexpected %s imsg", imsg_to_str(imsg->hdr.type));
}

static void
dispatcher_shutdown(void)
{
	log_debug("debug: dispatcher agent exiting");
	_exit(0);
}

int
dispatcher(void)
{
	struct passwd	*pw;

	ca_engine_init();

	mda_postfork();
	mta_postfork();
	smtp_postfork();

	/* do not purge listeners and pki, they are purged
	 * in smtp_configure()
	 */
	purge_config(PURGE_TABLES|PURGE_RULES);

	if ((pw = getpwnam(SMTPD_USER)) == NULL)
		fatalx("unknown user " SMTPD_USER);

	if (chroot(PATH_CHROOT) == -1)
		fatal("dispatcher: chroot");
	if (chdir("/") == -1)
		fatal("dispatcher: chdir(\"/\")");

	config_process(PROC_DISPATCHER);

	if (setgroups(1, &pw->pw_gid) ||
	    setresgid(pw->pw_gid, pw->pw_gid, pw->pw_gid) ||
	    setresuid(pw->pw_uid, pw->pw_uid, pw->pw_uid))
		fatal("dispatcher: cannot drop privileges");

	imsg_callback = dispatcher_imsg;
	event_init();

	mda_postprivdrop();
	mta_postprivdrop();
	smtp_postprivdrop();

	signal(SIGINT, SIG_IGN);
	signal(SIGTERM, SIG_IGN);
	signal(SIGPIPE, SIG_IGN);
	signal(SIGHUP, SIG_IGN);

	config_peer(PROC_PARENT);
	config_peer(PROC_QUEUE);
	config_peer(PROC_LKA);
	config_peer(PROC_CONTROL);
	config_peer(PROC_CA);

	if (pledge("stdio inet unix recvfd sendfd", NULL) == -1)
		fatal("pledge");

	event_dispatch();
	fatalx("exited event loop");

	return (0);
}
