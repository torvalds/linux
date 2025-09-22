/*	$OpenBSD: lka.c,v 1.250 2024/06/11 16:30:06 tb Exp $	*/

/*
 * Copyright (c) 2008 Pierre-Yves Ritschard <pyr@openbsd.org>
 * Copyright (c) 2008 Gilles Chehade <gilles@poolp.org>
 * Copyright (c) 2012 Eric Faurot <eric@faurot.net>
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

#include <sys/wait.h>

#include <errno.h>
#include <pwd.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>

#include "smtpd.h"
#include "log.h"

static void lka_imsg(struct mproc *, struct imsg *);
static void lka_shutdown(void);
static void lka_sig_handler(int, short, void *);
static int lka_authenticate(const char *, const char *, const char *);
static int lka_credentials(const char *, const char *, char *, size_t);
static int lka_userinfo(const char *, const char *, struct userinfo *);
static int lka_addrname(const char *, const struct sockaddr *,
    struct addrname *);
static int lka_mailaddrmap(const char *, const char *, const struct mailaddr *);

static void proc_timeout(int fd, short event, void *p);

struct event	 ev_proc_ready;

static void
lka_imsg(struct mproc *p, struct imsg *imsg)
{
	struct table		*table;
	int			 ret, fd;
	struct sockaddr_storage	 ss;
	struct userinfo		 userinfo;
	struct addrname		 addrname;
	struct envelope		 evp;
	struct mailaddr		 maddr;
	struct msg		 m;
	union lookup		 lk;
	char			 buf[LINE_MAX];
	const char		*tablename, *username, *password, *label, *procname;
	uint64_t		 reqid;
	int			 v;
	struct timeval		 tv;
	const char		*direction;
	const char		*rdns;
	const char		*command;
	const char		*response;
	const char		*ciphers;
	const char		*address;
	const char		*domain;
	const char		*helomethod;
	const char		*heloname;
	const char		*filter_name;
	const char		*result;
	struct sockaddr_storage	ss_src, ss_dest;
	int                      filter_response;
	int                      filter_phase;
	const char              *filter_param;
	uint32_t		 msgid;
	uint32_t		 subsystems;
	uint64_t		 evpid;
	size_t			 msgsz;
	int			 ok;
	int			 fcrdns;

	if (imsg == NULL)
		lka_shutdown();

	switch (imsg->hdr.type) {

	case IMSG_GETADDRINFO:
	case IMSG_GETNAMEINFO:
	case IMSG_RES_QUERY:
		resolver_dispatch_request(p, imsg);
		return;

	case IMSG_MTA_DNS_HOST:
	case IMSG_MTA_DNS_MX:
	case IMSG_MTA_DNS_MX_PREFERENCE:
		dns_imsg(p, imsg);
		return;

	case IMSG_SMTP_CHECK_SENDER:
		m_msg(&m, imsg);
		m_get_id(&m, &reqid);
		m_get_string(&m, &tablename);
		m_get_string(&m, &username);
		m_get_mailaddr(&m, &maddr);
		m_end(&m);

		ret = lka_mailaddrmap(tablename, username, &maddr);

		m_create(p, IMSG_SMTP_CHECK_SENDER, 0, 0, -1);
		m_add_id(p, reqid);
		m_add_int(p, ret);
		m_close(p);
		return;

	case IMSG_SMTP_EXPAND_RCPT:
		m_msg(&m, imsg);
		m_get_id(&m, &reqid);
		m_get_envelope(&m, &evp);
		m_end(&m);
		lka_session(reqid, &evp);
		return;

	case IMSG_SMTP_LOOKUP_HELO:
		m_msg(&m, imsg);
		m_get_id(&m, &reqid);
		m_get_string(&m, &tablename);
		m_get_sockaddr(&m, (struct sockaddr *)&ss);
		m_end(&m);

		ret = lka_addrname(tablename, (struct sockaddr*)&ss,
		    &addrname);

		m_create(p, IMSG_SMTP_LOOKUP_HELO, 0, 0, -1);
		m_add_id(p, reqid);
		m_add_int(p, ret);
		if (ret == LKA_OK)
			m_add_string(p, addrname.name);
		m_close(p);
		return;

	case IMSG_SMTP_AUTHENTICATE:
		m_msg(&m, imsg);
		m_get_id(&m, &reqid);
		m_get_string(&m, &tablename);
		m_get_string(&m, &username);
		m_get_string(&m, &password);
		m_end(&m);

		if (!tablename[0]) {
			m_create(p_parent, IMSG_LKA_AUTHENTICATE,
			    0, 0, -1);
			m_add_id(p_parent, reqid);
			m_add_string(p_parent, username);
			m_add_string(p_parent, password);
			m_close(p_parent);
			return;
		}

		ret = lka_authenticate(tablename, username, password);

		m_create(p, IMSG_SMTP_AUTHENTICATE, 0, 0, -1);
		m_add_id(p, reqid);
		m_add_int(p, ret);
		m_close(p);
		return;

	case IMSG_MDA_LOOKUP_USERINFO:
		m_msg(&m, imsg);
		m_get_id(&m, &reqid);
		m_get_string(&m, &tablename);
		m_get_string(&m, &username);
		m_end(&m);

		ret = lka_userinfo(tablename, username, &userinfo);

		m_create(p, IMSG_MDA_LOOKUP_USERINFO, 0, 0, -1);
		m_add_id(p, reqid);
		m_add_int(p, ret);
		if (ret == LKA_OK)
			m_add_data(p, &userinfo, sizeof(userinfo));
		m_close(p);
		return;

	case IMSG_MTA_LOOKUP_CREDENTIALS:
		m_msg(&m, imsg);
		m_get_id(&m, &reqid);
		m_get_string(&m, &tablename);
		m_get_string(&m, &label);
		m_end(&m);

		lka_credentials(tablename, label, buf, sizeof(buf));

		m_create(p, IMSG_MTA_LOOKUP_CREDENTIALS, 0, 0, -1);
		m_add_id(p, reqid);
		m_add_string(p, buf);
		m_close(p);
		return;

	case IMSG_MTA_LOOKUP_SOURCE:
		m_msg(&m, imsg);
		m_get_id(&m, &reqid);
		m_get_string(&m, &tablename);
		m_end(&m);

		table = table_find(env, tablename);

		m_create(p, IMSG_MTA_LOOKUP_SOURCE, 0, 0, -1);
		m_add_id(p, reqid);

		if (table == NULL) {
			log_warn("warn: source address table %s missing",
			    tablename);
			m_add_int(p, LKA_TEMPFAIL);
		}
		else {
			ret = table_fetch(table, K_SOURCE, &lk);
			if (ret == -1)
				m_add_int(p, LKA_TEMPFAIL);
			else if (ret == 0)
				m_add_int(p, LKA_PERMFAIL);
			else {
				m_add_int(p, LKA_OK);
				m_add_sockaddr(p,
				    (struct sockaddr *)&lk.source.addr);
			}
		}
		m_close(p);
		return;

	case IMSG_MTA_LOOKUP_HELO:
		m_msg(&m, imsg);
		m_get_id(&m, &reqid);
		m_get_string(&m, &tablename);
		m_get_sockaddr(&m, (struct sockaddr *)&ss);
		m_end(&m);

		ret = lka_addrname(tablename, (struct sockaddr*)&ss,
		    &addrname);

		m_create(p, IMSG_MTA_LOOKUP_HELO, 0, 0, -1);
		m_add_id(p, reqid);
		m_add_int(p, ret);
		if (ret == LKA_OK)
			m_add_string(p, addrname.name);
		m_close(p);
		return;

	case IMSG_MTA_LOOKUP_SMARTHOST:
		m_msg(&m, imsg);
		m_get_id(&m, &reqid);
		m_get_string(&m, &domain);
		m_get_string(&m, &tablename);
		m_end(&m);

		table = table_find(env, tablename);

		m_create(p, IMSG_MTA_LOOKUP_SMARTHOST, 0, 0, -1);
		m_add_id(p, reqid);

		if (table == NULL) {
			log_warn("warn: smarthost table %s missing", tablename);
			m_add_int(p, LKA_TEMPFAIL);
		}
		else {
			if (domain == NULL)
				ret = table_fetch(table, K_RELAYHOST, &lk);
			else
				ret = table_lookup(table, K_RELAYHOST, domain, &lk);

			if (ret == -1)
				m_add_int(p, LKA_TEMPFAIL);
			else if (ret == 0)
				m_add_int(p, LKA_PERMFAIL);
			else {
				m_add_int(p, LKA_OK);
				m_add_string(p, lk.relayhost);
			}
		}
		m_close(p);
		return;

	case IMSG_CONF_START:
		return;

	case IMSG_CONF_END:
		if (tracing & TRACE_TABLES)
			table_dump_all(env);

		/* fork & exec tables that need it */
		table_open_all(env);

		/* revoke proc & exec */
		if (pledge("stdio rpath inet dns getpw recvfd sendfd",
		    NULL) == -1)
			fatal("pledge");

		/* setup proc registering task */
		evtimer_set(&ev_proc_ready, proc_timeout, &ev_proc_ready);
		tv.tv_sec = 0;
		tv.tv_usec = 10;
		evtimer_add(&ev_proc_ready, &tv);
		return;

	case IMSG_LKA_OPEN_FORWARD:
		lka_session_forward_reply(imsg->data, imsg_get_fd(imsg));
		return;

	case IMSG_LKA_AUTHENTICATE:
		imsg->hdr.type = IMSG_SMTP_AUTHENTICATE;
		m_forward(p_dispatcher, imsg);
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

	case IMSG_CTL_UPDATE_TABLE:
		ret = 0;
		table = table_find(env, imsg->data);
		if (table == NULL) {
			log_warnx("warn: Lookup table not found: "
			    "\"%s\"", (char *)imsg->data);
		} else 
			ret = table_update(table);

		m_compose(p_control,
		    (ret == 1) ? IMSG_CTL_OK : IMSG_CTL_FAIL,
		    imsg->hdr.peerid, 0, -1, NULL, 0);
		return;

	case IMSG_LKA_PROCESSOR_FORK:
		m_msg(&m, imsg);
		m_get_string(&m, &procname);
		m_get_u32(&m, &subsystems);
		m_end(&m);

		m_create(p, IMSG_LKA_PROCESSOR_ERRFD, 0, 0, -1);
		m_add_string(p, procname);
		m_close(p);

		lka_proc_forked(procname, subsystems, imsg_get_fd(imsg));
		return;

	case IMSG_LKA_PROCESSOR_ERRFD:
		m_msg(&m, imsg);
		m_get_string(&m, &procname);
		m_end(&m);

		fd = imsg_get_fd(imsg);
		lka_proc_errfd(procname, fd);
		shutdown(fd, SHUT_WR);
		return;

	case IMSG_REPORT_SMTP_LINK_CONNECT:
		m_msg(&m, imsg);
		m_get_string(&m, &direction);
		m_get_timeval(&m, &tv);
		m_get_id(&m, &reqid);
		m_get_string(&m, &rdns);
		m_get_int(&m, &fcrdns);
		m_get_sockaddr(&m, (struct sockaddr *)&ss_src);
		m_get_sockaddr(&m, (struct sockaddr *)&ss_dest);
		m_end(&m);

		lka_report_smtp_link_connect(direction, &tv, reqid, rdns, fcrdns, &ss_src, &ss_dest);
		return;

	case IMSG_REPORT_SMTP_LINK_GREETING:
		m_msg(&m, imsg);
		m_get_string(&m, &direction);
		m_get_timeval(&m, &tv);
		m_get_id(&m, &reqid);
		m_get_string(&m, &domain);
		m_end(&m);

		lka_report_smtp_link_greeting(direction, reqid, &tv, domain);
		return;

	case IMSG_REPORT_SMTP_LINK_DISCONNECT:
		m_msg(&m, imsg);
		m_get_string(&m, &direction);
		m_get_timeval(&m, &tv);
		m_get_id(&m, &reqid);
		m_end(&m);

		lka_report_smtp_link_disconnect(direction, &tv, reqid);
		return;

	case IMSG_REPORT_SMTP_LINK_IDENTIFY:
		m_msg(&m, imsg);
		m_get_string(&m, &direction);
		m_get_timeval(&m, &tv);
		m_get_id(&m, &reqid);
		m_get_string(&m, &helomethod);
		m_get_string(&m, &heloname);
		m_end(&m);

		lka_report_smtp_link_identify(direction, &tv, reqid, helomethod, heloname);
		return;

	case IMSG_REPORT_SMTP_LINK_TLS:
		m_msg(&m, imsg);
		m_get_string(&m, &direction);
		m_get_timeval(&m, &tv);
		m_get_id(&m, &reqid);
		m_get_string(&m, &ciphers);
		m_end(&m);

		lka_report_smtp_link_tls(direction, &tv, reqid, ciphers);
		return;

	case IMSG_REPORT_SMTP_LINK_AUTH:
		m_msg(&m, imsg);
		m_get_string(&m, &direction);
		m_get_timeval(&m, &tv);
		m_get_id(&m, &reqid);
		m_get_string(&m, &username);
		m_get_string(&m, &result);
		m_end(&m);

		lka_report_smtp_link_auth(direction, &tv, reqid, username, result);
		return;

	case IMSG_REPORT_SMTP_TX_RESET:
		m_msg(&m, imsg);
		m_get_string(&m, &direction);
		m_get_timeval(&m, &tv);
		m_get_id(&m, &reqid);
		m_get_u32(&m, &msgid);
		m_end(&m);

		lka_report_smtp_tx_reset(direction, &tv, reqid, msgid);
		return;

	case IMSG_REPORT_SMTP_TX_BEGIN:
		m_msg(&m, imsg);
		m_get_string(&m, &direction);
		m_get_timeval(&m, &tv);
		m_get_id(&m, &reqid);
		m_get_u32(&m, &msgid);
		m_end(&m);

		lka_report_smtp_tx_begin(direction, &tv, reqid, msgid);
		return;

	case IMSG_REPORT_SMTP_TX_MAIL:
		m_msg(&m, imsg);
		m_get_string(&m, &direction);
		m_get_timeval(&m, &tv);
		m_get_id(&m, &reqid);
		m_get_u32(&m, &msgid);
		m_get_string(&m, &address);
		m_get_int(&m, &ok);
		m_end(&m);

		lka_report_smtp_tx_mail(direction, &tv, reqid, msgid, address, ok);
		return;

	case IMSG_REPORT_SMTP_TX_RCPT:
		m_msg(&m, imsg);
		m_get_string(&m, &direction);
		m_get_timeval(&m, &tv);
		m_get_id(&m, &reqid);
		m_get_u32(&m, &msgid);
		m_get_string(&m, &address);
		m_get_int(&m, &ok);
		m_end(&m);

		lka_report_smtp_tx_rcpt(direction, &tv, reqid, msgid, address, ok);
		return;

	case IMSG_REPORT_SMTP_TX_ENVELOPE:
		m_msg(&m, imsg);
		m_get_string(&m, &direction);
		m_get_timeval(&m, &tv);
		m_get_id(&m, &reqid);
		m_get_u32(&m, &msgid);
		m_get_id(&m, &evpid);
		m_end(&m);

		lka_report_smtp_tx_envelope(direction, &tv, reqid, msgid, evpid);
		return;

	case IMSG_REPORT_SMTP_TX_DATA:
		m_msg(&m, imsg);
		m_get_string(&m, &direction);
		m_get_timeval(&m, &tv);
		m_get_id(&m, &reqid);
		m_get_u32(&m, &msgid);
		m_get_int(&m, &ok);
		m_end(&m);

		lka_report_smtp_tx_data(direction, &tv, reqid, msgid, ok);
		return;

	case IMSG_REPORT_SMTP_TX_COMMIT:
		m_msg(&m, imsg);
		m_get_string(&m, &direction);
		m_get_timeval(&m, &tv);
		m_get_id(&m, &reqid);
		m_get_u32(&m, &msgid);
		m_get_size(&m, &msgsz);
		m_end(&m);

		lka_report_smtp_tx_commit(direction, &tv, reqid, msgid, msgsz);
		return;

	case IMSG_REPORT_SMTP_TX_ROLLBACK:
		m_msg(&m, imsg);
		m_get_string(&m, &direction);
		m_get_timeval(&m, &tv);
		m_get_id(&m, &reqid);
		m_get_u32(&m, &msgid);
		m_end(&m);

		lka_report_smtp_tx_rollback(direction, &tv, reqid, msgid);
		return;

	case IMSG_REPORT_SMTP_PROTOCOL_CLIENT:
		m_msg(&m, imsg);
		m_get_string(&m, &direction);
		m_get_timeval(&m, &tv);
		m_get_id(&m, &reqid);
		m_get_string(&m, &command);
		m_end(&m);

		lka_report_smtp_protocol_client(direction, &tv, reqid, command);
		return;

	case IMSG_REPORT_SMTP_PROTOCOL_SERVER:
		m_msg(&m, imsg);
		m_get_string(&m, &direction);
		m_get_timeval(&m, &tv);
		m_get_id(&m, &reqid);
		m_get_string(&m, &response);
		m_end(&m);

		lka_report_smtp_protocol_server(direction, &tv, reqid, response);
		return;

	case IMSG_REPORT_SMTP_FILTER_RESPONSE:
		m_msg(&m, imsg);
		m_get_string(&m, &direction);
		m_get_timeval(&m, &tv);
		m_get_id(&m, &reqid);
		m_get_int(&m, &filter_phase);
		m_get_int(&m, &filter_response);
		m_get_string(&m, &filter_param);
		m_end(&m);

		lka_report_smtp_filter_response(direction, &tv, reqid,
		    filter_phase, filter_response, filter_param);
		return;

	case IMSG_REPORT_SMTP_TIMEOUT:
		m_msg(&m, imsg);
		m_get_string(&m, &direction);
		m_get_timeval(&m, &tv);
		m_get_id(&m, &reqid);
		m_end(&m);

		lka_report_smtp_timeout(direction, &tv, reqid);
		return;

	case IMSG_FILTER_SMTP_PROTOCOL:
		m_msg(&m, imsg);
		m_get_id(&m, &reqid);
		m_get_int(&m, &filter_phase);
		m_get_string(&m, &filter_param);
		m_end(&m);

		lka_filter_protocol(reqid, filter_phase, filter_param);
		return;

	case IMSG_FILTER_SMTP_BEGIN:
		m_msg(&m, imsg);
		m_get_id(&m, &reqid);
		m_get_string(&m, &filter_name);
		m_end(&m);

		lka_filter_begin(reqid, filter_name);
		return;

	case IMSG_FILTER_SMTP_END:
		m_msg(&m, imsg);
		m_get_id(&m, &reqid);
		m_end(&m);

		lka_filter_end(reqid);
		return;

	case IMSG_FILTER_SMTP_DATA_BEGIN:
		m_msg(&m, imsg);
		m_get_id(&m, &reqid);
		m_end(&m);

		lka_filter_data_begin(reqid);
		return;

	case IMSG_FILTER_SMTP_DATA_END:
		m_msg(&m, imsg);
		m_get_id(&m, &reqid);
		m_end(&m);

		lka_filter_data_end(reqid);
		return;

	}

	fatalx("lka_imsg: unexpected %s imsg", imsg_to_str(imsg->hdr.type));
}

static void
lka_sig_handler(int sig, short event, void *p)
{
	int status;
	pid_t pid;

	switch (sig) {
	case SIGCHLD:
		do {
			pid = waitpid(-1, &status, WNOHANG);
		} while (pid > 0 || (pid == -1 && errno == EINTR));
		break;
	default:
		fatalx("lka_sig_handler: unexpected signal");
	}
}

void
lka_shutdown(void)
{
	log_debug("debug: lookup agent exiting");
	_exit(0);
}

int
lka(void)
{
	struct passwd	*pw;
	struct event	 ev_sigchld;

	purge_config(PURGE_LISTENERS);

	if ((pw = getpwnam(SMTPD_USER)) == NULL)
		fatalx("unknown user " SMTPD_USER);

	config_process(PROC_LKA);

	if (initgroups(pw->pw_name, pw->pw_gid) ||
	    setresgid(pw->pw_gid, pw->pw_gid, pw->pw_gid) ||
	    setresuid(pw->pw_uid, pw->pw_uid, pw->pw_uid))
		fatal("lka: cannot drop privileges");

	imsg_callback = lka_imsg;
	event_init();

	signal_set(&ev_sigchld, SIGCHLD, lka_sig_handler, NULL);
	signal_add(&ev_sigchld, NULL);
	signal(SIGINT, SIG_IGN);
	signal(SIGTERM, SIG_IGN);
	signal(SIGPIPE, SIG_IGN);
	signal(SIGHUP, SIG_IGN);

	config_peer(PROC_PARENT);
	config_peer(PROC_QUEUE);
	config_peer(PROC_CONTROL);
	config_peer(PROC_DISPATCHER);

	/* Ignore them until we get our config */
	mproc_disable(p_dispatcher);

	lka_report_init();
	lka_filter_init();

	/* proc & exec will be revoked before serving requests */
	if (pledge("stdio rpath inet dns getpw recvfd sendfd proc exec", NULL) == -1)
		fatal("pledge");

	event_dispatch();
	fatalx("exited event loop");

	return (0);
}

static void
proc_timeout(int fd, short event, void *p)
{
	struct event	*ev = p;
	struct timeval	 tv;

	if (!lka_proc_ready())
		goto reset;

	lka_filter_ready();
	mproc_enable(p_dispatcher);
	return;

reset:
	tv.tv_sec = 0;
	tv.tv_usec = 10;
	evtimer_add(ev, &tv);
}


static int
lka_authenticate(const char *tablename, const char *user, const char *password)
{
	struct table		*table;
	char	       		 offloadkey[LINE_MAX];
	union lookup		 lk;

	log_debug("debug: lka: authenticating for %s:%s", tablename, user);
	table = table_find(env, tablename);
	if (table == NULL) {
		log_warnx("warn: could not find table %s needed for authentication",
		    tablename);
		return (LKA_TEMPFAIL);
	}

	/* table backend supports authentication offloading */
	if (table_check_service(table, K_AUTH)) {
		if (!bsnprintf(offloadkey, sizeof(offloadkey), "%s:%s",
		    user, password)) {
			log_warnx("warn: key serialization failed for %s:%s",
			    tablename, user);
			return (LKA_TEMPFAIL);
		}
		switch (table_match(table, K_AUTH, offloadkey)) {
		case -1:
			log_warnx("warn: user credentials lookup fail for %s:%s",
			    tablename, user);
			return (LKA_TEMPFAIL);
		case 0:
			return (LKA_PERMFAIL);
		default:
			return (LKA_OK);
		}
	}

	switch (table_lookup(table, K_CREDENTIALS, user, &lk)) {
	case -1:
		log_warnx("warn: user credentials lookup fail for %s:%s",
		    tablename, user);
		return (LKA_TEMPFAIL);
	case 0:
		return (LKA_PERMFAIL);
	default:
		if (crypt_checkpass(password, lk.creds.password) == 0)
			return (LKA_OK);
		return (LKA_PERMFAIL);
	}
}

static int
lka_credentials(const char *tablename, const char *label, char *dst, size_t sz)
{
	struct table		*table;
	union lookup		 lk;
	char			*buf;
	int			 buflen, r;

	table = table_find(env, tablename);
	if (table == NULL) {
		log_warnx("warn: credentials table %s missing", tablename);
		return (LKA_TEMPFAIL);
	}

	dst[0] = '\0';

	switch (table_lookup(table, K_CREDENTIALS, label, &lk)) {
	case -1:
		log_warnx("warn: credentials lookup fail for %s:%s",
		    tablename, label);
		return (LKA_TEMPFAIL);
	case 0:
		log_warnx("warn: credentials not found for %s:%s",
		    tablename, label);
		return (LKA_PERMFAIL);
	default:
		if ((buflen = asprintf(&buf, "%c%s%c%s", '\0',
		    lk.creds.username, '\0', lk.creds.password)) == -1) {
			log_warn("warn");
			return (LKA_TEMPFAIL);
		}

		r = base64_encode((unsigned char *)buf, buflen, dst, sz);
		free(buf);

		if (r == -1) {
			log_warnx("warn: credentials parse error for %s:%s",
			    tablename, label);
			return (LKA_TEMPFAIL);
		}
		return (LKA_OK);
	}
}

static int
lka_userinfo(const char *tablename, const char *username, struct userinfo *res)
{
	struct table	*table;
	union lookup	 lk;

	log_debug("debug: lka: userinfo %s:%s", tablename, username);
	table = table_find(env, tablename);
	if (table == NULL) {
		log_warnx("warn: cannot find user table %s", tablename);
		return (LKA_TEMPFAIL);
	}

	switch (table_lookup(table, K_USERINFO, username, &lk)) {
	case -1:
		log_warnx("warn: failure during userinfo lookup %s:%s",
		    tablename, username);
		return (LKA_TEMPFAIL);
	case 0:
		return (LKA_PERMFAIL);
	default:
		*res = lk.userinfo;
		return (LKA_OK);
	}
}

static int
lka_addrname(const char *tablename, const struct sockaddr *sa,
    struct addrname *res)
{
	struct table	*table;
	union lookup	 lk;
	const char	*source;

	source = sa_to_text(sa);

	log_debug("debug: lka: helo %s:%s", tablename, source);
	table = table_find(env, tablename);
	if (table == NULL) {
		log_warnx("warn: cannot find helo table %s", tablename);
		return (LKA_TEMPFAIL);
	}

	switch (table_lookup(table, K_ADDRNAME, source, &lk)) {
	case -1:
		log_warnx("warn: failure during helo lookup %s:%s",
		    tablename, source);
		return (LKA_TEMPFAIL);
	case 0:
		return (LKA_PERMFAIL);
	default:
		*res = lk.addrname;
		return (LKA_OK);
	}
}

static int
lka_mailaddrmap(const char *tablename, const char *username, const struct mailaddr *maddr)
{
	struct table	       *table;
	struct maddrnode       *mn;
	union lookup		lk;
	int			found;

	log_debug("debug: lka: mailaddrmap %s:%s", tablename, username);
	table = table_find(env, tablename);
	if (table == NULL) {
		log_warnx("warn: cannot find mailaddrmap table %s", tablename);
		return (LKA_TEMPFAIL);
	}

	switch (table_lookup(table, K_MAILADDRMAP, username, &lk)) {
	case -1:
		log_warnx("warn: failure during mailaddrmap lookup %s:%s",
		    tablename, username);
		return (LKA_TEMPFAIL);
	case 0:
		return (LKA_PERMFAIL);
	default:
		found = 0;
		TAILQ_FOREACH(mn, &lk.maddrmap->queue, entries) {
			if (!mailaddr_match(maddr, &mn->mailaddr))
				continue;
			found = 1;
			break;
		}
		maddrmap_free(lk.maddrmap);
		if (found)
			return (LKA_OK);
		return (LKA_PERMFAIL);
	}
	return (LKA_OK);
}
