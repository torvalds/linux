/*	$OpenBSD: smtp_session.c,v 1.444 2025/05/13 14:52:42 op Exp $	*/

/*
 * Copyright (c) 2008 Gilles Chehade <gilles@poolp.org>
 * Copyright (c) 2008 Pierre-Yves Ritschard <pyr@openbsd.org>
 * Copyright (c) 2008-2009 Jacek Masiulaniec <jacekm@dobremiasto.net>
 * Copyright (c) 2012 Eric Faurot <eric@openbsd.org>
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

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <tls.h>
#include <unistd.h>
#include <vis.h>

#include "smtpd.h"
#include "log.h"
#include "rfc5322.h"

#define	SMTP_LINE_MAX			65535
#define	DATA_HIWAT			65535
#define	APPEND_DOMAIN_BUFFER_SIZE	SMTP_LINE_MAX

enum smtp_state {
	STATE_NEW = 0,
	STATE_CONNECTED,
	STATE_TLS,
	STATE_HELO,
	STATE_AUTH_INIT,
	STATE_AUTH_USERNAME,
	STATE_AUTH_PASSWORD,
	STATE_AUTH_FINALIZE,
	STATE_BODY,
	STATE_QUIT,
};

enum session_flags {
	SF_EHLO			= 0x0001,
	SF_8BITMIME		= 0x0002,
	SF_SECURE		= 0x0004,
	SF_AUTHENTICATED	= 0x0008,
	SF_BOUNCE		= 0x0010,
	SF_VERIFIED		= 0x0020,
	SF_BADINPUT		= 0x0080,
};

enum {
	TX_OK = 0,
	TX_ERROR_ENVELOPE,
	TX_ERROR_SIZE,
	TX_ERROR_IO,
	TX_ERROR_LOOP,
	TX_ERROR_MALFORMED,
	TX_ERROR_RESOURCES,
	TX_ERROR_INTERNAL,
};

enum smtp_command {
	CMD_HELO = 0,
	CMD_EHLO,
	CMD_STARTTLS,
	CMD_AUTH,
	CMD_MAIL_FROM,
	CMD_RCPT_TO,
	CMD_DATA,
	CMD_RSET,
	CMD_QUIT,
	CMD_HELP,
	CMD_WIZ,
	CMD_NOOP,
	CMD_COMMIT,
};

struct smtp_rcpt {
	TAILQ_ENTRY(smtp_rcpt)	 entry;
	uint64_t		 evpid;
 	struct mailaddr		 maddr;
	size_t			 destcount;
};

struct smtp_tx {
	struct smtp_session	*session;
	uint32_t		 msgid;

	struct envelope		 evp;
	size_t			 rcptcount;
	size_t			 destcount;
	TAILQ_HEAD(, smtp_rcpt)	 rcpts;

	time_t			 time;
	int			 error;
	size_t			 datain;
	size_t			 odatalen;
	FILE			*ofile;
	struct io		*filter;
	struct rfc5322_parser	*parser;
	int			 rcvcount;
	int			 has_date;
	int			 has_message_id;

	uint8_t			 junk;
};

struct smtp_session {
	uint64_t		 id;
	struct io		*io;
	struct listener		*listener;
	void			*ssl_ctx;
	struct sockaddr_storage	 ss;
	char			 rdns[HOST_NAME_MAX+1];
	char			 smtpname[HOST_NAME_MAX+1];
	int			 fcrdns;

	int			 flags;
	enum smtp_state		 state;

	uint8_t			 banner_sent;
	char			 helo[LINE_MAX];
	char			 cmd[LINE_MAX];
	char			 username[SMTPD_MAXMAILADDRSIZE];

	size_t			 mailcount;
	struct event		 pause;

	struct smtp_tx		*tx;

	enum smtp_command	 last_cmd;
	enum filter_phase	 filter_phase;
	const char		*filter_param;

	uint8_t			 junk;
};

#define ADVERTISE_TLS(s) \
	((s)->listener->flags & F_STARTTLS && !((s)->flags & SF_SECURE))

#define ADVERTISE_AUTH(s) \
	((s)->listener->flags & F_AUTH && (s)->flags & SF_SECURE && \
	 !((s)->flags & SF_AUTHENTICATED))

#define ADVERTISE_EXT_DSN(s) \
	((s)->listener->flags & F_EXT_DSN)

#define	SESSION_FILTERED(s) \
	((s)->listener->flags & F_FILTERED)

#define	SESSION_DATA_FILTERED(s) \
	((s)->listener->flags & F_FILTERED)


static int smtp_mailaddr(struct mailaddr *, char *, int, char **, const char *);
static void smtp_session_init(void);
static void smtp_lookup_servername(struct smtp_session *);
static void smtp_getnameinfo_cb(void *, int, const char *, const char *);
static void smtp_getaddrinfo_cb(void *, int, struct addrinfo *);
static void smtp_connected(struct smtp_session *);
static void smtp_send_banner(struct smtp_session *);
static void smtp_tls_init(struct smtp_session *);
static void smtp_tls_started(struct smtp_session *);
static void smtp_io(struct io *, int, void *);
static void smtp_enter_state(struct smtp_session *, int);
static void smtp_reply(struct smtp_session *, char *, ...);
static void smtp_command(struct smtp_session *, char *);
static void smtp_rfc4954_auth_plain(struct smtp_session *, char *);
static void smtp_rfc4954_auth_login(struct smtp_session *, char *);
static void smtp_free(struct smtp_session *, const char *);
static const char *smtp_strstate(int);
static void smtp_auth_failure_pause(struct smtp_session *);
static void smtp_auth_failure_resume(int, short, void *);

static int  smtp_tx(struct smtp_session *);
static void smtp_tx_free(struct smtp_tx *);
static void smtp_tx_create_message(struct smtp_tx *);
static void smtp_tx_mail_from(struct smtp_tx *, const char *);
static void smtp_tx_rcpt_to(struct smtp_tx *, const char *);
static void smtp_tx_open_message(struct smtp_tx *);
static void smtp_tx_commit(struct smtp_tx *);
static void smtp_tx_rollback(struct smtp_tx *);
static int  smtp_tx_dataline(struct smtp_tx *, const char *);
static int  smtp_tx_filtered_dataline(struct smtp_tx *, const char *);
static void smtp_tx_eom(struct smtp_tx *);
static void smtp_filter_fd(struct smtp_tx *, int);
static int  smtp_message_fd(struct smtp_tx *, int);
static void smtp_message_begin(struct smtp_tx *);
static void smtp_message_end(struct smtp_tx *);
static int  smtp_filter_printf(struct smtp_tx *, const char *, ...)
    __attribute__((__format__ (printf, 2, 3)));
static int  smtp_message_printf(struct smtp_tx *, const char *, ...)
    __attribute__((__format__ (printf, 2, 3)));

static int  smtp_check_rset(struct smtp_session *, const char *);
static int  smtp_check_helo(struct smtp_session *, const char *);
static int  smtp_check_ehlo(struct smtp_session *, const char *);
static int  smtp_check_auth(struct smtp_session *s, const char *);
static int  smtp_check_starttls(struct smtp_session *, const char *);
static int  smtp_check_mail_from(struct smtp_session *, const char *);
static int  smtp_check_rcpt_to(struct smtp_session *, const char *);
static int  smtp_check_data(struct smtp_session *, const char *);
static int  smtp_check_noop(struct smtp_session *, const char *);
static int  smtp_check_noparam(struct smtp_session *, const char *);

static void smtp_filter_phase(enum filter_phase, struct smtp_session *, const char *);

static void smtp_proceed_connected(struct smtp_session *);
static void smtp_proceed_rset(struct smtp_session *, const char *);
static void smtp_proceed_helo(struct smtp_session *, const char *);
static void smtp_proceed_ehlo(struct smtp_session *, const char *);
static void smtp_proceed_auth(struct smtp_session *, const char *);
static void smtp_proceed_starttls(struct smtp_session *, const char *);
static void smtp_proceed_mail_from(struct smtp_session *, const char *);
static void smtp_proceed_rcpt_to(struct smtp_session *, const char *);
static void smtp_proceed_data(struct smtp_session *, const char *);
static void smtp_proceed_noop(struct smtp_session *, const char *);
static void smtp_proceed_help(struct smtp_session *, const char *);
static void smtp_proceed_wiz(struct smtp_session *, const char *);
static void smtp_proceed_quit(struct smtp_session *, const char *);
static void smtp_proceed_commit(struct smtp_session *, const char *);
static void smtp_proceed_rollback(struct smtp_session *, const char *);

static void smtp_filter_begin(struct smtp_session *);
static void smtp_filter_end(struct smtp_session *);
static void smtp_filter_data_begin(struct smtp_session *);
static void smtp_filter_data_end(struct smtp_session *);

static void smtp_report_link_connect(struct smtp_session *, const char *, int,
    const struct sockaddr_storage *,
    const struct sockaddr_storage *);
static void smtp_report_link_greeting(struct smtp_session *, const char *);
static void smtp_report_link_identify(struct smtp_session *, const char *, const char *);
static void smtp_report_link_tls(struct smtp_session *, const char *);
static void smtp_report_link_disconnect(struct smtp_session *);
static void smtp_report_link_auth(struct smtp_session *, const char *, const char *);
static void smtp_report_tx_reset(struct smtp_session *, uint32_t);
static void smtp_report_tx_begin(struct smtp_session *, uint32_t);
static void smtp_report_tx_mail(struct smtp_session *, uint32_t, const char *, int);
static void smtp_report_tx_rcpt(struct smtp_session *, uint32_t, const char *, int);
static void smtp_report_tx_envelope(struct smtp_session *, uint32_t, uint64_t);
static void smtp_report_tx_data(struct smtp_session *, uint32_t, int);
static void smtp_report_tx_commit(struct smtp_session *, uint32_t, size_t);
static void smtp_report_tx_rollback(struct smtp_session *, uint32_t);
static void smtp_report_protocol_client(struct smtp_session *, const char *);
static void smtp_report_protocol_server(struct smtp_session *, const char *);
static void smtp_report_filter_response(struct smtp_session *, int, int, const char *);
static void smtp_report_timeout(struct smtp_session *);


static struct {
	int code;
	enum filter_phase filter_phase;
	const char *cmd;

	int (*check)(struct smtp_session *, const char *);
	void (*proceed)(struct smtp_session *, const char *);
} commands[] = {
	{ CMD_HELO,             FILTER_HELO,            "HELO",         smtp_check_helo,        smtp_proceed_helo },
	{ CMD_EHLO,             FILTER_EHLO,            "EHLO",         smtp_check_ehlo,        smtp_proceed_ehlo },
	{ CMD_STARTTLS,         FILTER_STARTTLS,        "STARTTLS",     smtp_check_starttls,    smtp_proceed_starttls },
	{ CMD_AUTH,             FILTER_AUTH,            "AUTH",         smtp_check_auth,        smtp_proceed_auth },
	{ CMD_MAIL_FROM,        FILTER_MAIL_FROM,       "MAIL FROM",    smtp_check_mail_from,   smtp_proceed_mail_from },
	{ CMD_RCPT_TO,          FILTER_RCPT_TO,         "RCPT TO",      smtp_check_rcpt_to,     smtp_proceed_rcpt_to },
	{ CMD_DATA,             FILTER_DATA,            "DATA",         smtp_check_data,        smtp_proceed_data },
	{ CMD_RSET,             FILTER_RSET,            "RSET",         smtp_check_rset,        smtp_proceed_rset },
	{ CMD_QUIT,             FILTER_QUIT,            "QUIT",         smtp_check_noparam,     smtp_proceed_quit },
	{ CMD_NOOP,             FILTER_NOOP,            "NOOP",         smtp_check_noop,        smtp_proceed_noop },
	{ CMD_HELP,             FILTER_HELP,            "HELP",         smtp_check_noparam,     smtp_proceed_help },
	{ CMD_WIZ,              FILTER_WIZ,             "WIZ",          smtp_check_noparam,     smtp_proceed_wiz },
	{ CMD_COMMIT,  		FILTER_COMMIT,		".",		smtp_check_noparam,	smtp_proceed_commit },
	{ -1,                   0,                      NULL,           NULL },
};

static struct tree wait_lka_helo;
static struct tree wait_lka_mail;
static struct tree wait_lka_rcpt;
static struct tree wait_parent_auth;
static struct tree wait_queue_msg;
static struct tree wait_queue_fd;
static struct tree wait_queue_commit;
static struct tree wait_ssl_init;
static struct tree wait_ssl_verify;
static struct tree wait_filters;
static struct tree wait_filter_fd;

static void
header_append_domain_buffer(char *buffer, char *domain, size_t len)
{
	size_t	i;
	int	escape, quote, comment, bracket;
	int	has_domain, has_bracket, has_group;
	int	pos_bracket, pos_component, pos_insert;
	char	copy[APPEND_DOMAIN_BUFFER_SIZE];

	escape = quote = comment = bracket = 0;
	has_domain = has_bracket = has_group = 0;
	pos_bracket = pos_insert = pos_component = 0;
	for (i = 0; buffer[i]; ++i) {
		if (buffer[i] == '(' && !escape && !quote)
			comment++;
		if (buffer[i] == '"' && !escape && !comment)
			quote = !quote;
		if (buffer[i] == ')' && !escape && !quote && comment)
			comment--;
		if (buffer[i] == '\\' && !escape && !comment && !quote)
			escape = 1;
		else
			escape = 0;
		if (buffer[i] == '<' && !escape && !comment && !quote && !bracket) {
			bracket++;
			has_bracket = 1;
		}
		if (buffer[i] == '>' && !escape && !comment && !quote && bracket) {
			bracket--;
			pos_bracket = i;
		}
		if (buffer[i] == '@' && !escape && !comment && !quote)
			has_domain = 1;
		if (buffer[i] == ':' && !escape && !comment && !quote)
			has_group = 1;

		/* update insert point if not in comment and not on a whitespace */
		if (!comment && buffer[i] != ')' && !isspace((unsigned char)buffer[i]))
			pos_component = i;
	}

	/* parse error, do not attempt to modify */
	if (escape || quote || comment || bracket)
		return;

	/* domain already present, no need to modify */
	if (has_domain)
		return;

	/* address is group, skip */
	if (has_group)
		return;

	/* there's an address between brackets, just append domain */
	if (has_bracket) {
		pos_bracket--;
		while (isspace((unsigned char)buffer[pos_bracket]))
			pos_bracket--;
		if (buffer[pos_bracket] == '<')
			return;
		pos_insert = pos_bracket + 1;
	}
	else {
		/* otherwise append address to last component */
		pos_insert = pos_component + 1;

		/* empty address */
                if (buffer[pos_component] == '\0' ||
		    isspace((unsigned char)buffer[pos_component]))
                        return;
	}

	if (snprintf(copy, sizeof copy, "%.*s@%s%s",
		(int)pos_insert, buffer,
		domain,
		buffer+pos_insert) >= (int)sizeof copy)
		return;

	memcpy(buffer, copy, len);
}

static void
header_address_rewrite_buffer(char *buffer, const char *address, size_t len)
{
	size_t	i;
	int	address_len;
	int	escape, quote, comment, bracket;
	int	has_bracket, has_group;
	int	pos_bracket_beg, pos_bracket_end, pos_component_beg, pos_component_end;
	int	insert_beg, insert_end;
	char	copy[APPEND_DOMAIN_BUFFER_SIZE];

	escape = quote = comment = bracket = 0;
	has_bracket = has_group = 0;
	pos_bracket_beg = pos_bracket_end = pos_component_beg = pos_component_end = 0;
	for (i = 0; buffer[i]; ++i) {
		if (buffer[i] == '(' && !escape && !quote)
			comment++;
		if (buffer[i] == '"' && !escape && !comment)
			quote = !quote;
		if (buffer[i] == ')' && !escape && !quote && comment)
			comment--;
		if (buffer[i] == '\\' && !escape && !comment && !quote)
			escape = 1;
		else
			escape = 0;
		if (buffer[i] == '<' && !escape && !comment && !quote && !bracket) {
			bracket++;
			has_bracket = 1;
			pos_bracket_beg = i+1;
		}
		if (buffer[i] == '>' && !escape && !comment && !quote && bracket) {
			bracket--;
			pos_bracket_end = i;
		}
		if (buffer[i] == ':' && !escape && !comment && !quote)
			has_group = 1;

		/* update insert point if not in comment and not on a whitespace */
		if (!comment && buffer[i] != ')' && !isspace((unsigned char)buffer[i]))
			pos_component_end = i;
	}

	/* parse error, do not attempt to modify */
	if (escape || quote || comment || bracket)
		return;

	/* address is group, skip */
	if (has_group)
		return;

	/* there's an address between brackets, just replace everything brackets */
	if (has_bracket) {
		insert_beg = pos_bracket_beg;
		insert_end = pos_bracket_end;
	}
	else {
		if (pos_component_end == 0)
			pos_component_beg = 0;
		else {
			for (pos_component_beg = pos_component_end; pos_component_beg >= 0; --pos_component_beg)
				if (buffer[pos_component_beg] == ')' || isspace((unsigned char)buffer[pos_component_beg]))
					break;
			pos_component_beg += 1;
			pos_component_end += 1;
		}
		insert_beg = pos_component_beg;
		insert_end = pos_component_end;
	}

	/* check that masquerade won' t overflow */
	address_len = strlen(address);
	if (strlen(buffer) - (insert_end - insert_beg) + address_len >= len)
		return;

	(void)strlcpy(copy, buffer, sizeof copy);
	(void)strlcpy(copy+insert_beg, address, sizeof (copy) - insert_beg);
	(void)strlcat(copy, buffer+insert_end, sizeof (copy));
	memcpy(buffer, copy, len);
}

static void
header_domain_append_callback(struct smtp_tx *tx, const char *hdr,
    const char *val)
{
	size_t			i, j, linelen;
	int			escape, quote, comment, skip;
	char			buffer[APPEND_DOMAIN_BUFFER_SIZE];
	const char *line, *end;

	if (smtp_message_printf(tx, "%s:", hdr) == -1)
		return;

	j = 0;
	escape = quote = comment = skip = 0;
	memset(buffer, 0, sizeof buffer);

	for (line = val; line; line = end) {
		end = strchr(line, '\n');
		if (end) {
			linelen = end - line;
			end++;
		}
		else
			linelen = strlen(line);

		for (i = 0; i < linelen; ++i) {
			if (line[i] == '(' && !escape && !quote)
				comment++;
			if (line[i] == '"' && !escape && !comment)
				quote = !quote;
			if (line[i] == ')' && !escape && !quote && comment)
				comment--;
			if (line[i] == '\\' && !escape && !comment)
				escape = 1;
			else
				escape = 0;

			/* found a separator, buffer contains a full address */
			if (line[i] == ',' && !escape && !quote && !comment) {
				if (!skip && j + strlen(tx->session->listener->hostname) + 1 < sizeof buffer) {
					header_append_domain_buffer(buffer, tx->session->listener->hostname, sizeof buffer);
					if (tx->session->flags & SF_AUTHENTICATED &&
					    tx->session->listener->sendertable[0] &&
					    tx->session->listener->flags & F_MASQUERADE &&
					    !(strcasecmp(hdr, "From")))
						header_address_rewrite_buffer(buffer, mailaddr_to_text(&tx->evp.sender),
						    sizeof buffer);
				}
				if (smtp_message_printf(tx, "%s,", buffer) == -1)
					return;
				j = 0;
				skip = 0;
				memset(buffer, 0, sizeof buffer);
			}
			else {
				if (skip) {
					if (smtp_message_printf(tx, "%c", line[i]) == -1)
						return;
				}
				else {
					buffer[j++] = line[i];
					if (j == sizeof (buffer) - 1) {
						if (smtp_message_printf(tx, "%s", buffer) == -1)
							return;
						skip = 1;
						j = 0;
						memset(buffer, 0, sizeof buffer);
					}
				}
			}
		}
		if (skip) {
			if (smtp_message_printf(tx, "\n") == -1)
				return;
		}
		else {
			buffer[j++] = '\n';
			if (j == sizeof (buffer) - 1) {
				if (smtp_message_printf(tx, "%s", buffer) == -1)
					return;
				skip = 1;
				j = 0;
				memset(buffer, 0, sizeof buffer);
			}
		}
	}

	/* end of header, if buffer is not empty we'll process it */
	if (buffer[0]) {
		if (j + strlen(tx->session->listener->hostname) + 1 < sizeof buffer) {
			header_append_domain_buffer(buffer, tx->session->listener->hostname, sizeof buffer);
			if (tx->session->flags & SF_AUTHENTICATED &&
			    tx->session->listener->sendertable[0] &&
			    tx->session->listener->flags & F_MASQUERADE &&
			    !(strcasecmp(hdr, "From")))
				header_address_rewrite_buffer(buffer, mailaddr_to_text(&tx->evp.sender),
				    sizeof buffer);
		}
		smtp_message_printf(tx, "%s", buffer);
	}
}

static void
smtp_session_init(void)
{
	static int	init = 0;

	if (!init) {
		tree_init(&wait_lka_helo);
		tree_init(&wait_lka_mail);
		tree_init(&wait_lka_rcpt);
		tree_init(&wait_parent_auth);
		tree_init(&wait_queue_msg);
		tree_init(&wait_queue_fd);
		tree_init(&wait_queue_commit);
		tree_init(&wait_ssl_init);
		tree_init(&wait_ssl_verify);
		tree_init(&wait_filters);
		tree_init(&wait_filter_fd);
		init = 1;
	}
}

int
smtp_session(struct listener *listener, int sock,
    const struct sockaddr_storage *ss, const char *hostname, struct io *io)
{
	struct smtp_session	*s;

	smtp_session_init();

	if ((s = calloc(1, sizeof(*s))) == NULL)
		return (-1);

	s->id = generate_uid();
	s->listener = listener;
	memmove(&s->ss, ss, sizeof(*ss));

	if (io != NULL)
		s->io = io;
	else
		s->io = io_new();

	io_set_callback(s->io, smtp_io, s);
	io_set_fd(s->io, sock);
	io_set_timeout(s->io, SMTPD_SESSION_TIMEOUT * 1000);
	io_set_write(s->io);
	s->state = STATE_NEW;

	(void)strlcpy(s->smtpname, listener->hostname, sizeof(s->smtpname));

	log_trace(TRACE_SMTP, "smtp: %p: connected to listener %p "
	    "[hostname=%s, port=%d, tag=%s]", s, listener,
	    listener->hostname, ntohs(listener->port), listener->tag);

	/* For local enqueueing, the hostname is already set */
	if (hostname) {
		s->flags |= SF_AUTHENTICATED;
		/* A bit of a hack */
		if (!strcmp(hostname, "localhost"))
			s->flags |= SF_BOUNCE;
		(void)strlcpy(s->rdns, hostname, sizeof(s->rdns));
		s->fcrdns = 1;
		smtp_lookup_servername(s);
	} else {
		resolver_getnameinfo((struct sockaddr *)&s->ss,
		    NI_NAMEREQD | NI_NUMERICSERV, smtp_getnameinfo_cb, s);
	}

	/* session may have been freed by now */

	return (0);
}

static void
smtp_getnameinfo_cb(void *arg, int gaierrno, const char *host, const char *serv)
{
	struct smtp_session *s = arg;
	struct addrinfo hints;

	if (gaierrno) {
		(void)strlcpy(s->rdns, "<unknown>", sizeof(s->rdns));

		if (gaierrno == EAI_NODATA || gaierrno == EAI_NONAME)
			s->fcrdns = 0;
		else {
			log_warnx("getnameinfo: %s: %s", ss_to_text(&s->ss),
			    gai_strerror(gaierrno));
			s->fcrdns = -1;
		}

		smtp_lookup_servername(s);
		return;
	}

	(void)strlcpy(s->rdns, host, sizeof(s->rdns));

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = s->ss.ss_family;
	hints.ai_socktype = SOCK_STREAM;
	resolver_getaddrinfo(s->rdns, NULL, &hints, smtp_getaddrinfo_cb, s);
}

static void
smtp_getaddrinfo_cb(void *arg, int gaierrno, struct addrinfo *ai0)
{
	struct smtp_session *s = arg;
	struct addrinfo *ai;
	char fwd[64], rev[64];

	if (gaierrno) {
		if (gaierrno == EAI_NODATA || gaierrno == EAI_NONAME)
			s->fcrdns = 0;
		else {
			log_warnx("getaddrinfo: %s: %s", s->rdns,
			    gai_strerror(gaierrno));
			s->fcrdns = -1;
		}
	}
	else {
		strlcpy(rev, ss_to_text(&s->ss), sizeof(rev));
		for (ai = ai0; ai; ai = ai->ai_next) {
			strlcpy(fwd, sa_to_text(ai->ai_addr), sizeof(fwd));
			if (!strcmp(fwd, rev)) {
				s->fcrdns = 1;
				break;
			}
		}
		freeaddrinfo(ai0);
	}

	smtp_lookup_servername(s);
}

void
smtp_session_imsg(struct mproc *p, struct imsg *imsg)
{
	struct smtp_session		*s;
	struct smtp_rcpt		*rcpt;
	char				 user[SMTPD_MAXMAILADDRSIZE];
	char				 tmp[SMTP_LINE_MAX];
	struct msg			 m;
	const char			*line, *helo;
	uint64_t			 reqid, evpid;
	uint32_t			 msgid;
	int				 status, success, fd;
	int                              filter_response;
	const char                      *filter_param;
	uint8_t                          i;

	switch (imsg->hdr.type) {

	case IMSG_SMTP_CHECK_SENDER:
		m_msg(&m, imsg);
		m_get_id(&m, &reqid);
		m_get_int(&m, &status);
		m_end(&m);
		s = tree_xpop(&wait_lka_mail, reqid);
		switch (status) {
		case LKA_OK:
			smtp_tx_create_message(s->tx);
			break;

		case LKA_PERMFAIL:
			smtp_tx_free(s->tx);
			smtp_reply(s, "%d %s", 530, "Sender rejected");
			break;
		case LKA_TEMPFAIL:
			smtp_tx_free(s->tx);
			smtp_reply(s, "421 %s Temporary Error",
			    esc_code(ESC_STATUS_TEMPFAIL, ESC_OTHER_MAIL_SYSTEM_STATUS));
			break;
		}
		return;

	case IMSG_SMTP_EXPAND_RCPT:
		m_msg(&m, imsg);
		m_get_id(&m, &reqid);
		m_get_int(&m, &status);
		m_get_string(&m, &line);
		m_end(&m);
		s = tree_xpop(&wait_lka_rcpt, reqid);

		tmp[0] = '\0';
		if (s->tx->evp.rcpt.user[0]) {
			(void)strlcpy(tmp, s->tx->evp.rcpt.user, sizeof tmp);
			if (s->tx->evp.rcpt.domain[0]) {
				(void)strlcat(tmp, "@", sizeof tmp);
				(void)strlcat(tmp, s->tx->evp.rcpt.domain,
				    sizeof tmp);
			}
		}

		switch (status) {
		case LKA_OK:
			fatalx("unexpected ok");
		case LKA_PERMFAIL:
			smtp_reply(s, "%s: <%s>", line, tmp);
			break;
		case LKA_TEMPFAIL:
			smtp_reply(s, "%s: <%s>", line, tmp);
			break;
		}
		return;

	case IMSG_SMTP_LOOKUP_HELO:
		m_msg(&m, imsg);
		m_get_id(&m, &reqid);
		s = tree_xpop(&wait_lka_helo, reqid);
		m_get_int(&m, &status);
		if (status == LKA_OK) {
			m_get_string(&m, &helo);
			(void)strlcpy(s->smtpname, helo, sizeof(s->smtpname));
		}
		m_end(&m);
		smtp_connected(s);
		return;

	case IMSG_SMTP_MESSAGE_CREATE:
		m_msg(&m, imsg);
		m_get_id(&m, &reqid);
		m_get_int(&m, &success);
		s = tree_xpop(&wait_queue_msg, reqid);
		if (success) {
			m_get_msgid(&m, &msgid);
			s->tx->msgid = msgid;
			s->tx->evp.id = msgid_to_evpid(msgid);
			s->tx->rcptcount = 0;
			smtp_reply(s, "250 %s Ok",
			    esc_code(ESC_STATUS_OK, ESC_OTHER_STATUS));
		} else {
			smtp_reply(s, "421 %s Temporary Error",
			    esc_code(ESC_STATUS_TEMPFAIL, ESC_OTHER_MAIL_SYSTEM_STATUS));
			smtp_tx_free(s->tx);
			smtp_enter_state(s, STATE_QUIT);
		}
		m_end(&m);
		return;

	case IMSG_SMTP_MESSAGE_OPEN:
		m_msg(&m, imsg);
		m_get_id(&m, &reqid);
		m_get_int(&m, &success);
		m_end(&m);

		fd = imsg_get_fd(imsg);
		s = tree_xpop(&wait_queue_fd, reqid);
		if (!success || fd == -1) {
			if (fd != -1)
				close(fd);
			smtp_reply(s, "421 %s Temporary Error",
			    esc_code(ESC_STATUS_TEMPFAIL, ESC_OTHER_MAIL_SYSTEM_STATUS));
			smtp_enter_state(s, STATE_QUIT);
			return;
		}

		log_debug("smtp: %p: fd %d from queue", s, fd);

		if (smtp_message_fd(s->tx, fd)) {
			if (!SESSION_DATA_FILTERED(s))
				smtp_message_begin(s->tx);
			else
				smtp_filter_data_begin(s);
		}
		return;

	case IMSG_FILTER_SMTP_DATA_BEGIN:
		m_msg(&m, imsg);
		m_get_id(&m, &reqid);
		m_get_int(&m, &success);
		m_end(&m);

		fd = imsg_get_fd(imsg);
		s = tree_xpop(&wait_filter_fd, reqid);
		if (!success || fd == -1) {
			if (fd != -1)
				close(fd);
			smtp_reply(s, "421 %s Temporary Error",
			    esc_code(ESC_STATUS_TEMPFAIL, ESC_OTHER_MAIL_SYSTEM_STATUS));
			smtp_enter_state(s, STATE_QUIT);
			return;
		}

		log_debug("smtp: %p: fd %d from lka", s, fd);

		smtp_filter_fd(s->tx, fd);
		smtp_message_begin(s->tx);
		return;

	case IMSG_QUEUE_ENVELOPE_SUBMIT:
		m_msg(&m, imsg);
		m_get_id(&m, &reqid);
		m_get_int(&m, &success);
		s = tree_xget(&wait_lka_rcpt, reqid);
		if (success) {
			m_get_evpid(&m, &evpid);
			s->tx->evp.id = evpid;
			s->tx->destcount++;
			smtp_report_tx_envelope(s, s->tx->msgid, evpid);
		}
		else
			s->tx->error = TX_ERROR_ENVELOPE;
		m_end(&m);
		return;

	case IMSG_QUEUE_ENVELOPE_COMMIT:
		m_msg(&m, imsg);
		m_get_id(&m, &reqid);
		m_get_int(&m, &success);
		m_end(&m);
		if (!success)
			fatalx("commit evp failed: not supposed to happen");
		s = tree_xpop(&wait_lka_rcpt, reqid);
		if (s->tx->error) {
			/*
			 * If an envelope failed, we can't cancel the last
			 * RCPT only so we must cancel the whole transaction
			 * and close the connection.
			 */
			smtp_reply(s, "421 %s Temporary failure",
			    esc_code(ESC_STATUS_TEMPFAIL, ESC_OTHER_MAIL_SYSTEM_STATUS));
			smtp_enter_state(s, STATE_QUIT);
		}
		else {
			rcpt = xcalloc(1, sizeof(*rcpt));
			rcpt->evpid = s->tx->evp.id;
			rcpt->destcount = s->tx->destcount;
			rcpt->maddr = s->tx->evp.rcpt;
			TAILQ_INSERT_TAIL(&s->tx->rcpts, rcpt, entry);

			s->tx->destcount = 0;
			s->tx->rcptcount++;
			smtp_reply(s, "250 %s %s: Recipient ok",
			    esc_code(ESC_STATUS_OK, ESC_DESTINATION_ADDRESS_VALID),
			    esc_description(ESC_DESTINATION_ADDRESS_VALID));
		}
		return;

	case IMSG_SMTP_MESSAGE_COMMIT:
		m_msg(&m, imsg);
		m_get_id(&m, &reqid);
		m_get_int(&m, &success);
		m_end(&m);
		s = tree_xpop(&wait_queue_commit, reqid);
		if (!success) {
			smtp_reply(s, "421 %s Temporary failure",
			    esc_code(ESC_STATUS_TEMPFAIL, ESC_OTHER_MAIL_SYSTEM_STATUS));
			smtp_tx_free(s->tx);
			smtp_enter_state(s, STATE_QUIT);
			return;
		}

		smtp_reply(s, "250 %s %08x Message accepted for delivery",
		    esc_code(ESC_STATUS_OK, ESC_OTHER_STATUS),
		    s->tx->msgid);
		smtp_report_tx_commit(s, s->tx->msgid, s->tx->odatalen);
		smtp_report_tx_reset(s, s->tx->msgid);

		log_info("%016"PRIx64" smtp message "
		    "msgid=%08x size=%zu nrcpt=%zu proto=%s",
		    s->id,
		    s->tx->msgid,
		    s->tx->odatalen,
		    s->tx->rcptcount,
		    s->flags & SF_EHLO ? "ESMTP" : "SMTP");
		TAILQ_FOREACH(rcpt, &s->tx->rcpts, entry) {
			log_info("%016"PRIx64" smtp envelope "
			    "evpid=%016"PRIx64" from=<%s%s%s> to=<%s%s%s>",
			    s->id,
			    rcpt->evpid,
			    s->tx->evp.sender.user,
			    s->tx->evp.sender.user[0] == '\0' ? "" : "@",
			    s->tx->evp.sender.domain,
			    rcpt->maddr.user,
			    rcpt->maddr.user[0] == '\0' ? "" : "@",
			    rcpt->maddr.domain);
		}
		smtp_tx_free(s->tx);
		s->mailcount++;
		smtp_enter_state(s, STATE_HELO);
		return;

	case IMSG_SMTP_AUTHENTICATE:
		m_msg(&m, imsg);
		m_get_id(&m, &reqid);
		m_get_int(&m, &success);
		m_end(&m);

		s = tree_xpop(&wait_parent_auth, reqid);
		strnvis(user, s->username, sizeof user, VIS_WHITE | VIS_SAFE);
		if (success == LKA_OK) {
			log_info("%016"PRIx64" smtp "
			    "authentication user=%s "
			    "result=ok",
			    s->id, user);
			s->flags |= SF_AUTHENTICATED;
			smtp_report_link_auth(s, user, "pass");
			smtp_reply(s, "235 %s Authentication succeeded",
			    esc_code(ESC_STATUS_OK, ESC_OTHER_STATUS));
		}
		else if (success == LKA_PERMFAIL) {
			log_info("%016"PRIx64" smtp "
			    "authentication user=%s "
			    "result=permfail",
			    s->id, user);
			smtp_report_link_auth(s, user, "fail");
			smtp_auth_failure_pause(s);
			return;
		}
		else if (success == LKA_TEMPFAIL) {
			log_info("%016"PRIx64" smtp "
			    "authentication user=%s "
			    "result=tempfail",
			    s->id, user);
			smtp_report_link_auth(s, user, "error");
			smtp_reply(s, "421 %s Temporary failure",
			    esc_code(ESC_STATUS_TEMPFAIL, ESC_OTHER_MAIL_SYSTEM_STATUS));
		}
		else
			fatalx("bad lka response");

		smtp_enter_state(s, STATE_HELO);
		return;

	case IMSG_FILTER_SMTP_PROTOCOL:
		m_msg(&m, imsg);
		m_get_id(&m, &reqid);
		m_get_int(&m, &filter_response);
		if (filter_response != FILTER_PROCEED &&
		    filter_response != FILTER_JUNK)
			m_get_string(&m, &filter_param);
		else
			filter_param = NULL;
		m_end(&m);

		s = tree_xpop(&wait_filters, reqid);

		switch (filter_response) {
		case FILTER_REJECT:
		case FILTER_DISCONNECT:
			if (!valid_smtp_response(filter_param) ||
			    (filter_param[0] != '4' && filter_param[0] != '5'))
				filter_param = "421 Internal server error";
			if (!strncmp(filter_param, "421", 3))
				filter_response = FILTER_DISCONNECT;

			smtp_report_filter_response(s, s->filter_phase,
			    filter_response, filter_param);

			smtp_reply(s, "%s", filter_param);

			if (filter_response == FILTER_DISCONNECT)
				smtp_enter_state(s, STATE_QUIT);
			else if (s->filter_phase == FILTER_COMMIT)
				smtp_proceed_rollback(s, NULL);
			break;


		case FILTER_JUNK:
			if (s->tx)
				s->tx->junk = 1;
			else
				s->junk = 1;
			/* fallthrough */

		case FILTER_PROCEED:
			filter_param = s->filter_param;
			/* fallthrough */

		case FILTER_REPORT:
		case FILTER_REWRITE:
			smtp_report_filter_response(s, s->filter_phase,
			    filter_response,
			    filter_param == s->filter_param ? NULL : filter_param);
			if (s->filter_phase == FILTER_CONNECT) {
				smtp_proceed_connected(s);
				return;
			}
			for (i = 0; i < nitems(commands); ++i)
				if (commands[i].filter_phase == s->filter_phase) {
					if (filter_response == FILTER_REWRITE)
						if (!commands[i].check(s, filter_param))
							break;
					commands[i].proceed(s, filter_param);
					break;
				}
			break;
		}
		return;
	}

	log_warnx("smtp_session_imsg: unexpected %s imsg",
	    imsg_to_str(imsg->hdr.type));
	fatalx(NULL);
}

static void
smtp_tls_init(struct smtp_session *s)
{
	io_set_read(s->io);
	if (io_accept_tls(s->io, s->listener->tls) == -1) {
		log_info("%016"PRIx64" smtp disconnected "
		    "reason=tls-accept-failed",
		    s->id);
		smtp_free(s, "accept failed");
	}
}

static void
smtp_tls_started(struct smtp_session *s)
{
	if (tls_peer_cert_provided(io_tls(s->io))) {
		log_info("%016"PRIx64" smtp "
		    "cert-check result=\"%s\" fingerprint=\"%s\"",
		    s->id,
		    (s->flags & SF_VERIFIED) ? "verified" : "unchecked",
		    tls_peer_cert_hash(io_tls(s->io)));
	}

	if (s->listener->flags & F_SMTPS) {
		stat_increment("smtp.smtps", 1);
		io_set_write(s->io);
		smtp_send_banner(s);
	}
	else {
		stat_increment("smtp.tls", 1);
		smtp_enter_state(s, STATE_HELO);
	}
}

static void
smtp_io(struct io *io, int evt, void *arg)
{
	struct smtp_session    *s = arg;
	char		       *line;
	size_t			len;
	int			eom;

	log_trace(TRACE_IO, "smtp: %p: %s %s", s, io_strevent(evt),
	    io_strio(io));

	switch (evt) {

	case IO_TLSREADY:
		log_info("%016"PRIx64" smtp tls ciphers=%s",
		    s->id, tls_to_text(io_tls(s->io)));

		smtp_report_link_tls(s, tls_to_text(io_tls(s->io)));

		s->flags |= SF_SECURE;
		if (s->listener->flags & F_TLS_VERIFY)
			s->flags |= SF_VERIFIED;
		s->helo[0] = '\0';

		smtp_tls_started(s);
		break;

	case IO_DATAIN:
	    nextline:
		line = io_getline(s->io, &len);
		if ((line == NULL && io_datalen(s->io) >= SMTP_LINE_MAX) ||
		    (line && len >= SMTP_LINE_MAX)) {
			s->flags |= SF_BADINPUT;
			smtp_reply(s, "500 %s Line too long",
			    esc_code(ESC_STATUS_PERMFAIL, ESC_OTHER_STATUS));
			smtp_enter_state(s, STATE_QUIT);
			io_set_write(io);
			return;
		}

		/* No complete line received */
		if (line == NULL)
			return;

		/* Strip trailing '\r' */
		if (len && line[len - 1] == '\r')
			line[--len] = '\0';

		/* Message body */
		eom = 0;
		if (s->state == STATE_BODY) {
			if (strcmp(line, ".")) {
				s->tx->datain += strlen(line) + 1;
				if (s->tx->datain > env->sc_maxsize)
					s->tx->error = TX_ERROR_SIZE;
			}
			eom = (s->tx->filter == NULL) ?
			    smtp_tx_dataline(s->tx, line) :
			    smtp_tx_filtered_dataline(s->tx, line);
			if (eom == 0)
				goto nextline;
		}

		/* Pipelining not supported */
		if (io_datalen(s->io)) {
			s->flags |= SF_BADINPUT;
			smtp_reply(s, "500 %s %s: Pipelining not supported",
			    esc_code(ESC_STATUS_PERMFAIL, ESC_INVALID_COMMAND),
			    esc_description(ESC_INVALID_COMMAND));
			smtp_enter_state(s, STATE_QUIT);
			io_set_write(io);
			return;
		}

		if (eom) {
			io_set_write(io);
			if (s->tx->filter == NULL)
				smtp_tx_eom(s->tx);
			return;
		}

		/* Must be a command */
		if (strlcpy(s->cmd, line, sizeof(s->cmd)) >= sizeof(s->cmd)) {
			s->flags |= SF_BADINPUT;
			smtp_reply(s, "500 %s Command line too long",
			    esc_code(ESC_STATUS_PERMFAIL, ESC_OTHER_STATUS));
			smtp_enter_state(s, STATE_QUIT);
			io_set_write(io);
			return;
		}
		io_set_write(io);
		smtp_command(s, line);
		break;

	case IO_LOWAT:
		if (s->state == STATE_QUIT) {
			log_info("%016"PRIx64" smtp disconnected "
			    "reason=quit",
			    s->id);
			smtp_free(s, "done");
			break;
		}

		/* Wait for the client to start tls */
		if (s->state == STATE_TLS) {
			smtp_tls_init(s);
			break;
		}

		io_set_read(io);
		break;

	case IO_TIMEOUT:
		log_info("%016"PRIx64" smtp disconnected "
		    "reason=timeout",
		    s->id);
		smtp_report_timeout(s);
		smtp_free(s, "timeout");
		break;

	case IO_DISCONNECTED:
		log_info("%016"PRIx64" smtp disconnected "
		    "reason=disconnect",
		    s->id);
		smtp_free(s, "disconnected");
		break;

	case IO_ERROR:
		log_info("%016"PRIx64" smtp disconnected "
		    "reason=\"io-error: %s\"",
		    s->id, io_error(io));
		smtp_free(s, "IO error");
		break;

	default:
		fatalx("smtp_io()");
	}
}

static void
smtp_command(struct smtp_session *s, char *line)
{
	char			       *args;
	int				cmd, i;

	log_trace(TRACE_SMTP, "smtp: %p: <<< %s", s, line);

	/*
	 * These states are special.
	 */
	if (s->state == STATE_AUTH_INIT) {
		smtp_report_protocol_client(s, "********");
		smtp_rfc4954_auth_plain(s, line);
		return;
	}
	if (s->state == STATE_AUTH_USERNAME || s->state == STATE_AUTH_PASSWORD) {
		smtp_report_protocol_client(s, "********");
		smtp_rfc4954_auth_login(s, line);
		return;
	}

	if (s->state == STATE_HELO && strncasecmp(line, "AUTH PLAIN ", 11) == 0)
		smtp_report_protocol_client(s, "AUTH PLAIN ********");
	else
		smtp_report_protocol_client(s, line);


	/*
	 * Unlike other commands, "mail from" and "rcpt to" contain a
	 * space in the command name.
	 */
	if (strncasecmp("mail from:", line, 10) == 0 ||
	    strncasecmp("rcpt to:", line, 8) == 0)
		args = strchr(line, ':');
	else
		args = strchr(line, ' ');

	if (args) {
		*args++ = '\0';
		while (isspace((unsigned char)*args))
			args++;
	}

	cmd = -1;
	for (i = 0; commands[i].code != -1; i++)
		if (!strcasecmp(line, commands[i].cmd)) {
			cmd = commands[i].code;
			break;
		}

	s->last_cmd = cmd;
	switch (cmd) {
	/*
	 * INIT
	 */
	case CMD_HELO:
		if (!smtp_check_helo(s, args))
			break;
		smtp_filter_phase(FILTER_HELO, s, args);
		break;

	case CMD_EHLO:
		if (!smtp_check_ehlo(s, args))
			break;
		smtp_filter_phase(FILTER_EHLO, s, args);
		break;

	/*
	 * SETUP
	 */
	case CMD_STARTTLS:
		if (!smtp_check_starttls(s, args))
			break;

		smtp_filter_phase(FILTER_STARTTLS, s, NULL);
		break;

	case CMD_AUTH:
		if (!smtp_check_auth(s, args))
			break;
		smtp_filter_phase(FILTER_AUTH, s, args);
		break;

	case CMD_MAIL_FROM:
		if (!smtp_check_mail_from(s, args))
			break;
		smtp_filter_phase(FILTER_MAIL_FROM, s, args);
		break;

	/*
	 * TRANSACTION
	 */
	case CMD_RCPT_TO:
		if (!smtp_check_rcpt_to(s, args))
			break;
		smtp_filter_phase(FILTER_RCPT_TO, s, args);
		break;

	case CMD_RSET:
		if (!smtp_check_rset(s, args))
			break;
		smtp_filter_phase(FILTER_RSET, s, NULL);
		break;

	case CMD_DATA:
		if (!smtp_check_data(s, args))
			break;
		smtp_filter_phase(FILTER_DATA, s, NULL);
		break;

	/*
	 * ANY
	 */
	case CMD_QUIT:
		if (!smtp_check_noparam(s, args))
			break;
		smtp_filter_phase(FILTER_QUIT, s, NULL);
		break;

	case CMD_NOOP:
		if (!smtp_check_noop(s, args))
			break;
		smtp_filter_phase(FILTER_NOOP, s, NULL);
		break;

	case CMD_HELP:
		if (!smtp_check_noparam(s, args))
			break;
		smtp_proceed_help(s, NULL);
		break;

	case CMD_WIZ:
		if (!smtp_check_noparam(s, args))
			break;
		smtp_proceed_wiz(s, NULL);
		break;

	default:
		smtp_reply(s, "500 %s %s: Command unrecognized",
			    esc_code(ESC_STATUS_PERMFAIL, ESC_INVALID_COMMAND),
			    esc_description(ESC_INVALID_COMMAND));
		break;
	}
}

static int
smtp_check_rset(struct smtp_session *s, const char *args)
{
	if (!smtp_check_noparam(s, args))
		return 0;

	if (s->helo[0] == '\0') {
		smtp_reply(s, "503 %s %s: Command not allowed at this point.",
		    esc_code(ESC_STATUS_PERMFAIL, ESC_INVALID_COMMAND),
		    esc_description(ESC_INVALID_COMMAND));
		return 0;
	}
	return 1;
}

static int
smtp_check_helo(struct smtp_session *s, const char *args)
{
	if (!s->banner_sent) {
		smtp_reply(s, "503 %s %s: Command not allowed at this point.",
		    esc_code(ESC_STATUS_PERMFAIL, ESC_INVALID_COMMAND),
		    esc_description(ESC_INVALID_COMMAND));
		return 0;
	}

	if (s->helo[0]) {
		smtp_reply(s, "503 %s %s: Already identified",
		    esc_code(ESC_STATUS_PERMFAIL, ESC_INVALID_COMMAND),
		    esc_description(ESC_INVALID_COMMAND));
		return 0;
	}

	if (args == NULL) {
		smtp_reply(s, "501 %s %s: HELO requires domain name",
		    esc_code(ESC_STATUS_PERMFAIL, ESC_INVALID_COMMAND),
		    esc_description(ESC_INVALID_COMMAND));
		return 0;
	}

	if (!valid_domainpart(args)) {
		smtp_reply(s, "501 %s %s: Invalid domain name",
		    esc_code(ESC_STATUS_PERMFAIL, ESC_INVALID_COMMAND_ARGUMENTS),
		    esc_description(ESC_INVALID_COMMAND_ARGUMENTS));
		return 0;
	}

	return 1;
}

static int
smtp_check_ehlo(struct smtp_session *s, const char *args)
{
	if (!s->banner_sent) {
		smtp_reply(s, "503 %s %s: Command not allowed at this point.",
		    esc_code(ESC_STATUS_PERMFAIL, ESC_INVALID_COMMAND),
		    esc_description(ESC_INVALID_COMMAND));
		return 0;
	}

	if (s->helo[0]) {
		smtp_reply(s, "503 %s %s: Already identified",
		    esc_code(ESC_STATUS_PERMFAIL, ESC_INVALID_COMMAND),
		    esc_description(ESC_INVALID_COMMAND));
		return 0;
	}

	if (args == NULL) {
		smtp_reply(s, "501 %s %s: EHLO requires domain name",
		    esc_code(ESC_STATUS_PERMFAIL, ESC_INVALID_COMMAND),
		    esc_description(ESC_INVALID_COMMAND));
		return 0;
	}

	if (!valid_domainpart(args)) {
		smtp_reply(s, "501 %s %s: Invalid domain name",
		    esc_code(ESC_STATUS_PERMFAIL, ESC_INVALID_COMMAND_ARGUMENTS),
		    esc_description(ESC_INVALID_COMMAND_ARGUMENTS));
		return 0;
	}

	return 1;
}

static int
smtp_check_auth(struct smtp_session *s, const char *args)
{
	if (s->helo[0] == '\0' || s->tx) {
		smtp_reply(s, "503 %s %s: Command not allowed at this point.",
		    esc_code(ESC_STATUS_PERMFAIL, ESC_INVALID_COMMAND),
		    esc_description(ESC_INVALID_COMMAND));
		return 0;
	}

	if (s->flags & SF_AUTHENTICATED) {
		smtp_reply(s, "503 %s %s: Already authenticated",
		    esc_code(ESC_STATUS_PERMFAIL, ESC_INVALID_COMMAND),
		    esc_description(ESC_INVALID_COMMAND));
		return 0;
	}

	if (!ADVERTISE_AUTH(s)) {
		smtp_reply(s, "503 %s %s: Command not supported",
		    esc_code(ESC_STATUS_PERMFAIL, ESC_INVALID_COMMAND),
		    esc_description(ESC_INVALID_COMMAND));
		return 0;
	}

	if (args == NULL) {
		smtp_reply(s, "501 %s %s: No parameters given",
		    esc_code(ESC_STATUS_PERMFAIL, ESC_INVALID_COMMAND_ARGUMENTS),
		    esc_description(ESC_INVALID_COMMAND_ARGUMENTS));
		return 0;
	}

	return 1;
}

static int
smtp_check_starttls(struct smtp_session *s, const char *args)
{
	if (s->helo[0] == '\0' || s->tx) {
		smtp_reply(s, "503 %s %s: Command not allowed at this point.",
		    esc_code(ESC_STATUS_PERMFAIL, ESC_INVALID_COMMAND),
		    esc_description(ESC_INVALID_COMMAND));
		return 0;
	}

	if (!(s->listener->flags & F_STARTTLS)) {
		smtp_reply(s, "503 %s %s: Command not supported",
		    esc_code(ESC_STATUS_PERMFAIL, ESC_INVALID_COMMAND),
		    esc_description(ESC_INVALID_COMMAND));
		return 0;
	}

	if (s->flags & SF_SECURE) {
		smtp_reply(s, "503 %s %s: Channel already secured",
		    esc_code(ESC_STATUS_PERMFAIL, ESC_INVALID_COMMAND),
		    esc_description(ESC_INVALID_COMMAND));
		return 0;
	}

	if (args != NULL) {
		smtp_reply(s, "501 %s %s: No parameters allowed",
		    esc_code(ESC_STATUS_PERMFAIL, ESC_INVALID_COMMAND_ARGUMENTS),
		    esc_description(ESC_INVALID_COMMAND_ARGUMENTS));
		return 0;
	}

	return 1;
}

static int
smtp_check_mail_from(struct smtp_session *s, const char *args)
{
	char *copy;
	char tmp[SMTP_LINE_MAX];
	struct mailaddr	sender;

	(void)strlcpy(tmp, args, sizeof tmp);
	copy = tmp;

	if (s->helo[0] == '\0' || s->tx) {
		smtp_reply(s, "503 %s %s: Command not allowed at this point.",
		    esc_code(ESC_STATUS_PERMFAIL, ESC_INVALID_COMMAND),
		    esc_description(ESC_INVALID_COMMAND));
		return 0;
	}

	if (s->listener->flags & F_STARTTLS_REQUIRE &&
	    !(s->flags & SF_SECURE)) {
		smtp_reply(s,
		    "530 %s %s: Must issue a STARTTLS command first",
		    esc_code(ESC_STATUS_PERMFAIL, ESC_INVALID_COMMAND),
		    esc_description(ESC_INVALID_COMMAND));
		return 0;
	}

	if (s->listener->flags & F_AUTH_REQUIRE &&
	    !(s->flags & SF_AUTHENTICATED)) {
		smtp_reply(s,
		    "530 %s %s: Must issue an AUTH command first",
		    esc_code(ESC_STATUS_PERMFAIL, ESC_INVALID_COMMAND),
		    esc_description(ESC_INVALID_COMMAND));
		return 0;
	}

	if (s->mailcount >= env->sc_session_max_mails) {
		/* we can pretend we had too many recipients */
		smtp_reply(s, "452 %s %s: Too many messages sent",
		    esc_code(ESC_STATUS_TEMPFAIL, ESC_TOO_MANY_RECIPIENTS),
		    esc_description(ESC_TOO_MANY_RECIPIENTS));
		return 0;
	}

	if (smtp_mailaddr(&sender, copy, 1, &copy,
		s->smtpname) == 0) {
		smtp_reply(s, "553 %s Sender address syntax error",
		    esc_code(ESC_STATUS_PERMFAIL, ESC_OTHER_ADDRESS_STATUS));
		return 0;
	}

	return 1;
}

static int
smtp_check_rcpt_to(struct smtp_session *s, const char *args)
{
	char *copy;
	char tmp[SMTP_LINE_MAX];

	(void)strlcpy(tmp, args, sizeof tmp);
	copy = tmp;

	if (s->tx == NULL) {
		smtp_reply(s, "503 %s %s: Command not allowed at this point.",
		    esc_code(ESC_STATUS_PERMFAIL, ESC_INVALID_COMMAND),
		    esc_description(ESC_INVALID_COMMAND));
		return 0;
	}

	if (s->tx->rcptcount >= env->sc_session_max_rcpt) {
		smtp_reply(s->tx->session, "451 %s %s: Too many recipients",
		    esc_code(ESC_STATUS_TEMPFAIL, ESC_TOO_MANY_RECIPIENTS),
		    esc_description(ESC_TOO_MANY_RECIPIENTS));
		return 0;
	}

	if (smtp_mailaddr(&s->tx->evp.rcpt, copy, 0, &copy,
		s->tx->session->smtpname) == 0) {
		smtp_reply(s->tx->session,
		    "501 %s Recipient address syntax error",
		    esc_code(ESC_STATUS_PERMFAIL,
		        ESC_BAD_DESTINATION_MAILBOX_ADDRESS_SYNTAX));
		return 0;
	}

	return 1;
}

static int
smtp_check_data(struct smtp_session *s, const char *args)
{
	if (!smtp_check_noparam(s, args))
		return 0;

	if (s->tx == NULL) {
		smtp_reply(s, "503 %s %s: Command not allowed at this point.",
		    esc_code(ESC_STATUS_PERMFAIL, ESC_INVALID_COMMAND),
		    esc_description(ESC_INVALID_COMMAND));
		return 0;
	}

	if (s->tx->rcptcount == 0) {
		smtp_reply(s, "503 %s %s: No recipient specified",
		    esc_code(ESC_STATUS_PERMFAIL, ESC_INVALID_COMMAND_ARGUMENTS),
		    esc_description(ESC_INVALID_COMMAND_ARGUMENTS));
		return 0;
	}

	return 1;
}

static int
smtp_check_noop(struct smtp_session *s, const char *args)
{
	return 1;
}

static int
smtp_check_noparam(struct smtp_session *s, const char *args)
{
	if (args != NULL) {
		smtp_reply(s, "500 %s %s: command does not accept arguments.",
		    esc_code(ESC_STATUS_PERMFAIL, ESC_INVALID_COMMAND_ARGUMENTS),
		    esc_description(ESC_INVALID_COMMAND_ARGUMENTS));
		return 0;
	}
	return 1;
}

static void
smtp_query_filters(enum filter_phase phase, struct smtp_session *s, const char *args)
{
	m_create(p_lka, IMSG_FILTER_SMTP_PROTOCOL, 0, 0, -1);
	m_add_id(p_lka, s->id);
	m_add_int(p_lka, phase);
	m_add_string(p_lka, args);
	m_close(p_lka);
	tree_xset(&wait_filters, s->id, s);
}

static void
smtp_filter_begin(struct smtp_session *s)
{
	if (!SESSION_FILTERED(s))
		return;

	m_create(p_lka, IMSG_FILTER_SMTP_BEGIN, 0, 0, -1);
	m_add_id(p_lka, s->id);
	m_add_string(p_lka, s->listener->filter_name);
	m_close(p_lka);
}

static void
smtp_filter_end(struct smtp_session *s)
{
	if (!SESSION_FILTERED(s))
		return;

	m_create(p_lka, IMSG_FILTER_SMTP_END, 0, 0, -1);
	m_add_id(p_lka, s->id);
	m_close(p_lka);
}

static void
smtp_filter_data_begin(struct smtp_session *s)
{
	if (!SESSION_FILTERED(s))
		return;

	m_create(p_lka, IMSG_FILTER_SMTP_DATA_BEGIN, 0, 0, -1);
	m_add_id(p_lka, s->id);
	m_close(p_lka);
	tree_xset(&wait_filter_fd, s->id, s);
}

static void
smtp_filter_data_end(struct smtp_session *s)
{
	if (!SESSION_FILTERED(s))
		return;

	if (s->tx->filter == NULL)
		return;

	io_free(s->tx->filter);
	s->tx->filter = NULL;

	m_create(p_lka, IMSG_FILTER_SMTP_DATA_END, 0, 0, -1);
	m_add_id(p_lka, s->id);
	m_close(p_lka);
}

static void
smtp_filter_phase(enum filter_phase phase, struct smtp_session *s, const char *param)
{
	uint8_t i;

	s->filter_phase = phase;
	s->filter_param = param;

	if (SESSION_FILTERED(s)) {
		smtp_query_filters(phase, s, param ? param : "");
		return;
	}

	if (s->filter_phase == FILTER_CONNECT) {
		smtp_proceed_connected(s);
		return;
	}

	for (i = 0; i < nitems(commands); ++i)
		if (commands[i].filter_phase == s->filter_phase) {
			commands[i].proceed(s, param);
			break;
		}
}

static void
smtp_proceed_rset(struct smtp_session *s, const char *args)
{
	smtp_reply(s, "250 %s Reset state",
	    esc_code(ESC_STATUS_OK, ESC_OTHER_STATUS));

	if (s->tx) {
		if (s->tx->msgid)
			smtp_tx_rollback(s->tx);
		smtp_tx_free(s->tx);
	}
}

static void
smtp_proceed_helo(struct smtp_session *s, const char *args)
{
	(void)strlcpy(s->helo, args, sizeof(s->helo));
	s->flags &= SF_SECURE | SF_AUTHENTICATED | SF_VERIFIED;

	smtp_report_link_identify(s, "HELO", s->helo);

	smtp_enter_state(s, STATE_HELO);

	smtp_reply(s, "250 %s Hello %s %s%s%s, pleased to meet you",
	    s->smtpname,
	    s->helo,
	    s->ss.ss_family == AF_INET6 ? "" : "[",
	    ss_to_text(&s->ss),
	    s->ss.ss_family == AF_INET6 ? "" : "]");
}

static void
smtp_proceed_ehlo(struct smtp_session *s, const char *args)
{
	(void)strlcpy(s->helo, args, sizeof(s->helo));
	s->flags &= SF_SECURE | SF_AUTHENTICATED | SF_VERIFIED;
	s->flags |= SF_EHLO;
	s->flags |= SF_8BITMIME;

	smtp_report_link_identify(s, "EHLO", s->helo);

	smtp_enter_state(s, STATE_HELO);
	smtp_reply(s, "250-%s Hello %s %s%s%s, pleased to meet you",
	    s->smtpname,
	    s->helo,
	    s->ss.ss_family == AF_INET6 ? "" : "[",
	    ss_to_text(&s->ss),
	    s->ss.ss_family == AF_INET6 ? "" : "]");

	smtp_reply(s, "250-8BITMIME");
	smtp_reply(s, "250-ENHANCEDSTATUSCODES");
	smtp_reply(s, "250-SIZE %zu", env->sc_maxsize);
	if (ADVERTISE_EXT_DSN(s))
		smtp_reply(s, "250-DSN");
	if (ADVERTISE_TLS(s))
		smtp_reply(s, "250-STARTTLS");
	if (ADVERTISE_AUTH(s))
		smtp_reply(s, "250-AUTH PLAIN LOGIN");
	smtp_reply(s, "250 HELP");
}

static void
smtp_proceed_auth(struct smtp_session *s, const char *args)
{
	char tmp[SMTP_LINE_MAX];
	char *eom, *method;

	(void)strlcpy(tmp, args, sizeof tmp);

	method = tmp;
	eom = strchr(tmp, ' ');
	if (eom == NULL)
		eom = strchr(tmp, '\t');
	if (eom != NULL)
		*eom++ = '\0';
	if (strcasecmp(method, "PLAIN") == 0)
		smtp_rfc4954_auth_plain(s, eom);
	else if (strcasecmp(method, "LOGIN") == 0)
		smtp_rfc4954_auth_login(s, eom);
	else
		smtp_reply(s, "504 %s %s: AUTH method \"%s\" not supported",
		    esc_code(ESC_STATUS_PERMFAIL, ESC_SECURITY_FEATURES_NOT_SUPPORTED),
		    esc_description(ESC_SECURITY_FEATURES_NOT_SUPPORTED),
		    method);
}

static void
smtp_proceed_starttls(struct smtp_session *s, const char *args)
{
	smtp_reply(s, "220 %s Ready to start TLS",
	    esc_code(ESC_STATUS_OK, ESC_OTHER_STATUS));
	smtp_enter_state(s, STATE_TLS);
}

static void
smtp_proceed_mail_from(struct smtp_session *s, const char *args)
{
	char *copy;
	char tmp[SMTP_LINE_MAX];

	(void)strlcpy(tmp, args, sizeof tmp);
	copy = tmp;

       	if (!smtp_tx(s)) {
		smtp_reply(s, "421 %s Temporary Error",
		    esc_code(ESC_STATUS_TEMPFAIL, ESC_OTHER_MAIL_SYSTEM_STATUS));
		smtp_enter_state(s, STATE_QUIT);
		return;
	}

	if (smtp_mailaddr(&s->tx->evp.sender, copy, 1, &copy,
		s->smtpname) == 0) {
		smtp_reply(s, "553 %s Sender address syntax error",
		    esc_code(ESC_STATUS_PERMFAIL, ESC_OTHER_ADDRESS_STATUS));
		smtp_tx_free(s->tx);
		return;
	}

	smtp_tx_mail_from(s->tx, args);
}

static void
smtp_proceed_rcpt_to(struct smtp_session *s, const char *args)
{
	smtp_tx_rcpt_to(s->tx, args);
}

static void
smtp_proceed_data(struct smtp_session *s, const char *args)
{
	smtp_tx_open_message(s->tx);
}

static void
smtp_proceed_quit(struct smtp_session *s, const char *args)
{
	smtp_reply(s, "221 %s Bye",
	    esc_code(ESC_STATUS_OK, ESC_OTHER_STATUS));
	smtp_enter_state(s, STATE_QUIT);
}

static void
smtp_proceed_noop(struct smtp_session *s, const char *args)
{
	smtp_reply(s, "250 %s Ok",
	    esc_code(ESC_STATUS_OK, ESC_OTHER_STATUS));
}

static void
smtp_proceed_help(struct smtp_session *s, const char *args)
{
	const char *code = esc_code(ESC_STATUS_OK, ESC_OTHER_STATUS);

	smtp_reply(s, "214-%s This is " SMTPD_NAME, code);
	smtp_reply(s, "214-%s To report bugs in the implementation, "
	    "please contact bugs@openbsd.org", code);
	smtp_reply(s, "214-%s with full details", code);
	smtp_reply(s, "214 %s End of HELP info", code);
}

static void
smtp_proceed_wiz(struct smtp_session *s, const char *args)
{
	smtp_reply(s, "500 %s %s: this feature is not supported yet ;-)",
	    esc_code(ESC_STATUS_PERMFAIL, ESC_INVALID_COMMAND),
	    esc_description(ESC_INVALID_COMMAND));
}

static void
smtp_proceed_commit(struct smtp_session *s, const char *args)
{
	smtp_message_end(s->tx);
}

static void
smtp_proceed_rollback(struct smtp_session *s, const char *args)
{
	struct smtp_tx *tx;

	tx = s->tx;

	fclose(tx->ofile);
	tx->ofile = NULL;

	smtp_tx_rollback(tx);
	smtp_tx_free(tx);
	smtp_enter_state(s, STATE_HELO);
}

static void
smtp_rfc4954_auth_plain(struct smtp_session *s, char *arg)
{
	char		 buf[1024], *user, *pass;
	int		 len;

	switch (s->state) {
	case STATE_HELO:
		if (arg == NULL) {
			smtp_enter_state(s, STATE_AUTH_INIT);
			smtp_reply(s, "334 ");
			return;
		}
		smtp_enter_state(s, STATE_AUTH_INIT);
		/* FALLTHROUGH */

	case STATE_AUTH_INIT:
		/* String is not NUL terminated, leave room. */
		if ((len = base64_decode(arg, (unsigned char *)buf,
			    sizeof(buf) - 1)) == -1)
			goto abort;
		/* buf is a byte string, NUL terminate. */
		buf[len] = '\0';

		/*
		 * Skip "foo" in "foo\0user\0pass", if present.
		 */
		user = memchr(buf, '\0', len);
		if (user == NULL || user >= buf + len - 2)
			goto abort;
		user++; /* skip NUL */
		if (strlcpy(s->username, user, sizeof(s->username))
		    >= sizeof(s->username))
			goto abort;

		pass = memchr(user, '\0', len - (user - buf));
		if (pass == NULL || pass >= buf + len - 1)
			goto abort;
		pass++; /* skip NUL */

		m_create(p_lka,  IMSG_SMTP_AUTHENTICATE, 0, 0, -1);
		m_add_id(p_lka, s->id);
		m_add_string(p_lka, s->listener->authtable);
		m_add_string(p_lka, user);
		m_add_string(p_lka, pass);
		m_close(p_lka);
		tree_xset(&wait_parent_auth, s->id, s);
		return;

	default:
		fatal("smtp_rfc4954_auth_plain: unknown state");
	}

abort:
	smtp_reply(s, "501 %s %s: Syntax error",
	    esc_code(ESC_STATUS_PERMFAIL, ESC_SYNTAX_ERROR),
	    esc_description(ESC_SYNTAX_ERROR));
	smtp_enter_state(s, STATE_HELO);
}

static void
smtp_rfc4954_auth_login(struct smtp_session *s, char *arg)
{
	char		buf[LINE_MAX];

	switch (s->state) {
	case STATE_HELO:
		smtp_enter_state(s, STATE_AUTH_USERNAME);
		if (arg != NULL && *arg != '\0') {
			smtp_rfc4954_auth_login(s, arg);
			return;
		}
		smtp_reply(s, "334 VXNlcm5hbWU6");
		return;

	case STATE_AUTH_USERNAME:
		memset(s->username, 0, sizeof(s->username));
		if (base64_decode(arg, (unsigned char *)s->username,
				  sizeof(s->username) - 1) == -1)
			goto abort;

		smtp_enter_state(s, STATE_AUTH_PASSWORD);
		smtp_reply(s, "334 UGFzc3dvcmQ6");
		return;

	case STATE_AUTH_PASSWORD:
		memset(buf, 0, sizeof(buf));
		if (base64_decode(arg, (unsigned char *)buf,
				  sizeof(buf)-1) == -1)
			goto abort;

		m_create(p_lka,  IMSG_SMTP_AUTHENTICATE, 0, 0, -1);
		m_add_id(p_lka, s->id);
		m_add_string(p_lka, s->listener->authtable);
		m_add_string(p_lka, s->username);
		m_add_string(p_lka, buf);
		m_close(p_lka);
		tree_xset(&wait_parent_auth, s->id, s);
		return;

	default:
		fatal("smtp_rfc4954_auth_login: unknown state");
	}

abort:
	smtp_reply(s, "501 %s %s: Syntax error",
	    esc_code(ESC_STATUS_PERMFAIL, ESC_SYNTAX_ERROR),
	    esc_description(ESC_SYNTAX_ERROR));
	smtp_enter_state(s, STATE_HELO);
}

static void
smtp_lookup_servername(struct smtp_session *s)
{
	if (s->listener->hostnametable[0]) {
		m_create(p_lka, IMSG_SMTP_LOOKUP_HELO, 0, 0, -1);
		m_add_id(p_lka, s->id);
		m_add_string(p_lka, s->listener->hostnametable);
		m_add_sockaddr(p_lka, (struct sockaddr*)&s->listener->ss);
		m_close(p_lka);
		tree_xset(&wait_lka_helo, s->id, s);
		return;
	}

	smtp_connected(s);
}

static void
smtp_connected(struct smtp_session *s)
{
	smtp_enter_state(s, STATE_CONNECTED);

	log_info("%016"PRIx64" smtp connected address=%s host=%s",
	    s->id, ss_to_text(&s->ss), s->rdns);

	smtp_filter_begin(s);

	smtp_report_link_connect(s, s->rdns, s->fcrdns, &s->ss,
	    &s->listener->ss);

	smtp_filter_phase(FILTER_CONNECT, s, ss_to_text(&s->ss));
}

static void
smtp_proceed_connected(struct smtp_session *s)
{
	if (s->listener->flags & F_SMTPS)
		smtp_tls_init(s);
	else
		smtp_send_banner(s);
}

static void
smtp_send_banner(struct smtp_session *s)
{
	smtp_reply(s, "220 %s ESMTP %s", s->smtpname, SMTPD_NAME);
	s->banner_sent = 1;
	smtp_report_link_greeting(s, s->smtpname);
}

void
smtp_enter_state(struct smtp_session *s, int newstate)
{
	log_trace(TRACE_SMTP, "smtp: %p: %s -> %s", s,
	    smtp_strstate(s->state),
	    smtp_strstate(newstate));

	s->state = newstate;
}

static void
smtp_reply(struct smtp_session *s, char *fmt, ...)
{
	va_list	 ap;
	int	 n;
	char	 buf[LINE_MAX*2], tmp[LINE_MAX*2];

	va_start(ap, fmt);
	n = vsnprintf(buf, sizeof buf, fmt, ap);
	va_end(ap);
	if (n < 0)
		fatalx("smtp_reply: response format error");
	if (n < 4)
		fatalx("smtp_reply: response too short");
	if (n >= (int)sizeof buf) {
		/* only first three bytes are used by SMTP logic,
		 * so if _our_ reply does not fit entirely in the
		 * buffer, it's ok to truncate.
		 */
	}

	log_trace(TRACE_SMTP, "smtp: %p: >>> %s", s, buf);
	smtp_report_protocol_server(s, buf);

	switch (buf[0]) {
	case '2':
		if (s->tx) {
			if (s->last_cmd == CMD_MAIL_FROM) {
				smtp_report_tx_begin(s, s->tx->msgid);
				smtp_report_tx_mail(s, s->tx->msgid, s->cmd + 10, 1);
			}
			else if (s->last_cmd == CMD_RCPT_TO)
				smtp_report_tx_rcpt(s, s->tx->msgid, s->cmd + 8, 1);
		}
		break;
	case '3':
		if (s->tx) {
			if (s->last_cmd == CMD_DATA)
				smtp_report_tx_data(s, s->tx->msgid, 1);
		}
		break;
	case '5':
	case '4':
		/* do not report smtp_tx_mail/smtp_tx_rcpt errors
		 * if they happened outside of a transaction.
		 */
		if (s->tx) {
			if (s->last_cmd == CMD_MAIL_FROM)
				smtp_report_tx_mail(s, s->tx->msgid,
				    s->cmd + 10, buf[0] == '4' ? -1 : 0);
			else if (s->last_cmd == CMD_RCPT_TO)
				smtp_report_tx_rcpt(s,
				    s->tx->msgid, s->cmd + 8, buf[0] == '4' ? -1 : 0);
			else if (s->last_cmd == CMD_DATA && s->tx->rcptcount)
				smtp_report_tx_data(s, s->tx->msgid,
				    buf[0] == '4' ? -1 : 0);
		}

		if (s->flags & SF_BADINPUT) {
			log_info("%016"PRIx64" smtp "
			    "bad-input result=\"%.*s\"",
			    s->id, n, buf);
		}
		else if (s->state == STATE_AUTH_INIT) {
			log_info("%016"PRIx64" smtp "
			    "failed-command "
			    "command=\"AUTH PLAIN (...)\" result=\"%.*s\"",
			    s->id, n, buf);
		}
		else if (s->state == STATE_AUTH_USERNAME) {
			log_info("%016"PRIx64" smtp "
			    "failed-command "
			    "command=\"AUTH LOGIN (username)\" result=\"%.*s\"",
			    s->id, n, buf);
		}
		else if (s->state == STATE_AUTH_PASSWORD) {
			log_info("%016"PRIx64" smtp "
			    "failed-command "
			    "command=\"AUTH LOGIN (password)\" result=\"%.*s\"",
			    s->id, n, buf);
		}
		else {
			strnvis(tmp, s->cmd, sizeof tmp, VIS_SAFE | VIS_CSTYLE);
			log_info("%016"PRIx64" smtp "
			    "failed-command command=\"%s\" "
			    "result=\"%.*s\"",
			    s->id, tmp, n, buf);
		}
		break;
	}

	io_xprintf(s->io, "%s\r\n", buf);
}

static void
smtp_free(struct smtp_session *s, const char * reason)
{
	if (s->tx) {
		if (s->tx->msgid)
			smtp_tx_rollback(s->tx);
		smtp_tx_free(s->tx);
	}

	smtp_report_link_disconnect(s);
	smtp_filter_end(s);

	if (s->flags & SF_SECURE && s->listener->flags & F_SMTPS)
		stat_decrement("smtp.smtps", 1);
	if (s->flags & SF_SECURE && s->listener->flags & F_STARTTLS)
		stat_decrement("smtp.tls", 1);

	io_free(s->io);
	free(s);

	smtp_collect();
}

static int
smtp_mailaddr(struct mailaddr *maddr, char *line, int mailfrom, char **args,
    const char *domain)
{
	char   *p, *e;

	if (line == NULL)
		return (0);

	if (*line != '<')
		return (0);

	e = strchr(line, '>');
	if (e == NULL)
		return (0);
	*e++ = '\0';
	while (*e == ' ')
		e++;
	*args = e;

	if (!text_to_mailaddr(maddr, line + 1))
		return (0);

	p = strchr(maddr->user, ':');
	if (p != NULL) {
		p++;
		memmove(maddr->user, p, strlen(p) + 1);
	}

	/* accept empty return-path in MAIL FROM, required for bounces */
	if (mailfrom && maddr->user[0] == '\0' && maddr->domain[0] == '\0')
		return (1);

	/* no or invalid user-part, reject */
	if (maddr->user[0] == '\0' || !valid_localpart(maddr->user))
		return (0);

	/* no domain part, local user */
	if (maddr->domain[0] == '\0') {
		(void)strlcpy(maddr->domain, domain,
			sizeof(maddr->domain));
	}

	if (!valid_domainpart(maddr->domain))
		return (0);

	return (1);
}

static void
smtp_auth_failure_resume(int fd, short event, void *p)
{
	struct smtp_session *s = p;

	smtp_reply(s, "535 Authentication failed");
	smtp_enter_state(s, STATE_HELO);
}

static void
smtp_auth_failure_pause(struct smtp_session *s)
{
	struct timeval	tv;

	tv.tv_sec = 0;
	tv.tv_usec = arc4random_uniform(1000000);
	log_trace(TRACE_SMTP, "smtp: timing-attack protection triggered, "
	    "will defer answer for %lu microseconds", (long)tv.tv_usec);
	evtimer_set(&s->pause, smtp_auth_failure_resume, s);
	evtimer_add(&s->pause, &tv);
}

static int
smtp_tx(struct smtp_session *s)
{
	struct smtp_tx *tx;

	tx = calloc(1, sizeof(*tx));
	if (tx == NULL)
		return 0;

	TAILQ_INIT(&tx->rcpts);

	s->tx = tx;
	tx->session = s;

	/* setup the envelope */
	tx->evp.ss = s->ss;
	(void)strlcpy(tx->evp.tag, s->listener->tag, sizeof(tx->evp.tag));
	(void)strlcpy(tx->evp.smtpname, s->smtpname, sizeof(tx->evp.smtpname));
	(void)strlcpy(tx->evp.hostname, s->rdns, sizeof tx->evp.hostname);
	(void)strlcpy(tx->evp.helo, s->helo, sizeof(tx->evp.helo));
	(void)strlcpy(tx->evp.username, s->username, sizeof(tx->evp.username));

	if (s->flags & SF_BOUNCE)
		tx->evp.flags |= EF_BOUNCE;
	if (s->flags & SF_AUTHENTICATED)
		tx->evp.flags |= EF_AUTHENTICATED;

	if ((tx->parser = rfc5322_parser_new()) == NULL) {
		free(tx);
		return 0;
	}

	return 1;
}

static void
smtp_tx_free(struct smtp_tx *tx)
{
	struct smtp_rcpt *rcpt;

	rfc5322_free(tx->parser);

	while ((rcpt = TAILQ_FIRST(&tx->rcpts))) {
		TAILQ_REMOVE(&tx->rcpts, rcpt, entry);
		free(rcpt);
	}

	if (tx->ofile)
		fclose(tx->ofile);

	tx->session->tx = NULL;

	free(tx);
}

static void
smtp_tx_mail_from(struct smtp_tx *tx, const char *line)
{
	char *opt;
	char *copy;
	char tmp[SMTP_LINE_MAX];

	(void)strlcpy(tmp, line, sizeof tmp);
	copy = tmp;

	if (smtp_mailaddr(&tx->evp.sender, copy, 1, &copy,
		tx->session->smtpname) == 0) {
		smtp_reply(tx->session, "553 %s Sender address syntax error",
		    esc_code(ESC_STATUS_PERMFAIL, ESC_OTHER_ADDRESS_STATUS));
		smtp_tx_free(tx);
		return;
	}

	while ((opt = strsep(&copy, " "))) {
		if (*opt == '\0')
			continue;

		if (strncasecmp(opt, "AUTH=", 5) == 0)
			log_debug("debug: smtp: AUTH in MAIL FROM command");
		else if (strncasecmp(opt, "SIZE=", 5) == 0)
			log_debug("debug: smtp: SIZE in MAIL FROM command");
		else if (strcasecmp(opt, "BODY=7BIT") == 0)
			/* XXX only for this transaction */
			tx->session->flags &= ~SF_8BITMIME;
		else if (strcasecmp(opt, "BODY=8BITMIME") == 0)
			;
		else if (ADVERTISE_EXT_DSN(tx->session) && strncasecmp(opt, "RET=", 4) == 0) {
			opt += 4;
			if (strcasecmp(opt, "HDRS") == 0)
				tx->evp.dsn_ret = DSN_RETHDRS;
			else if (strcasecmp(opt, "FULL") == 0)
				tx->evp.dsn_ret = DSN_RETFULL;
		} else if (ADVERTISE_EXT_DSN(tx->session) && strncasecmp(opt, "ENVID=", 6) == 0) {
			opt += 6;
			if (strlcpy(tx->evp.dsn_envid, opt, sizeof(tx->evp.dsn_envid))
			    >= sizeof(tx->evp.dsn_envid)) {
				smtp_reply(tx->session,
				    "503 %s %s: option too large, truncated: %s",
				    esc_code(ESC_STATUS_PERMFAIL, ESC_INVALID_COMMAND_ARGUMENTS),
				    esc_description(ESC_INVALID_COMMAND_ARGUMENTS), opt);
				smtp_tx_free(tx);
				return;
			}
		} else {
			smtp_reply(tx->session, "503 %s %s: Unsupported option %s",
			    esc_code(ESC_STATUS_PERMFAIL, ESC_INVALID_COMMAND_ARGUMENTS),
			    esc_description(ESC_INVALID_COMMAND_ARGUMENTS), opt);
			smtp_tx_free(tx);
			return;
		}
	}

	/* only check sendertable if defined and user has authenticated */
	if (tx->session->flags & SF_AUTHENTICATED &&
	    tx->session->listener->sendertable[0]) {
		m_create(p_lka, IMSG_SMTP_CHECK_SENDER, 0, 0, -1);
		m_add_id(p_lka, tx->session->id);
		m_add_string(p_lka, tx->session->listener->sendertable);
		m_add_string(p_lka, tx->session->username);
		m_add_mailaddr(p_lka, &tx->evp.sender);
		m_close(p_lka);
		tree_xset(&wait_lka_mail, tx->session->id, tx->session);
	}
	else
		smtp_tx_create_message(tx);
}

static void
smtp_tx_create_message(struct smtp_tx *tx)
{
	m_create(p_queue, IMSG_SMTP_MESSAGE_CREATE, 0, 0, -1);
	m_add_id(p_queue, tx->session->id);
	m_close(p_queue);
	tree_xset(&wait_queue_msg, tx->session->id, tx->session);
}

static void
smtp_tx_rcpt_to(struct smtp_tx *tx, const char *line)
{
	char *opt, *p;
	char *copy;
	char tmp[SMTP_LINE_MAX];

	(void)strlcpy(tmp, line, sizeof tmp);
	copy = tmp;

	if (tx->rcptcount >= env->sc_session_max_rcpt) {
		smtp_reply(tx->session, "451 %s %s: Too many recipients",
		    esc_code(ESC_STATUS_TEMPFAIL, ESC_TOO_MANY_RECIPIENTS),
		    esc_description(ESC_TOO_MANY_RECIPIENTS));
		return;
	}

	if (smtp_mailaddr(&tx->evp.rcpt, copy, 0, &copy,
	    tx->session->smtpname) == 0) {
		smtp_reply(tx->session,
		    "501 %s Recipient address syntax error",
		    esc_code(ESC_STATUS_PERMFAIL,
		        ESC_BAD_DESTINATION_MAILBOX_ADDRESS_SYNTAX));
		return;
	}

	while ((opt = strsep(&copy, " "))) {
		if (*opt == '\0')
			continue;

		if (ADVERTISE_EXT_DSN(tx->session) && strncasecmp(opt, "NOTIFY=", 7) == 0) {
			opt += 7;
			while ((p = strsep(&opt, ","))) {
				if (strcasecmp(p, "SUCCESS") == 0)
					tx->evp.dsn_notify |= DSN_SUCCESS;
				else if (strcasecmp(p, "FAILURE") == 0)
					tx->evp.dsn_notify |= DSN_FAILURE;
				else if (strcasecmp(p, "DELAY") == 0)
					tx->evp.dsn_notify |= DSN_DELAY;
				else if (strcasecmp(p, "NEVER") == 0)
					tx->evp.dsn_notify |= DSN_NEVER;
			}

			if (tx->evp.dsn_notify & DSN_NEVER &&
			    tx->evp.dsn_notify & (DSN_SUCCESS | DSN_FAILURE |
			    DSN_DELAY)) {
				smtp_reply(tx->session,
				    "553 NOTIFY option NEVER cannot be"
				    " combined with other options");
				return;
			}
		} else if (ADVERTISE_EXT_DSN(tx->session) &&
		    strncasecmp(opt, "ORCPT=", 6) == 0) {
			size_t len = sizeof(tx->evp.dsn_orcpt);

			opt += 6;

			if ((p = strchr(opt, ';')) == NULL ||
			    !valid_xtext(p + 1) ||
			    strlcpy(tx->evp.dsn_orcpt, opt, len) >= len) {
				smtp_reply(tx->session,
				    "553 ORCPT address syntax error");
				return;
			}
		} else {
			smtp_reply(tx->session, "503 Unsupported option %s", opt);
			return;
		}
	}

	m_create(p_lka, IMSG_SMTP_EXPAND_RCPT, 0, 0, -1);
	m_add_id(p_lka, tx->session->id);
	m_add_envelope(p_lka, &tx->evp);
	m_close(p_lka);
	tree_xset(&wait_lka_rcpt, tx->session->id, tx->session);
}

static void
smtp_tx_open_message(struct smtp_tx *tx)
{
	m_create(p_queue, IMSG_SMTP_MESSAGE_OPEN, 0, 0, -1);
	m_add_id(p_queue, tx->session->id);
	m_add_msgid(p_queue, tx->msgid);
	m_close(p_queue);
	tree_xset(&wait_queue_fd, tx->session->id, tx->session);
}

static void
smtp_tx_commit(struct smtp_tx *tx)
{
	m_create(p_queue, IMSG_SMTP_MESSAGE_COMMIT, 0, 0, -1);
	m_add_id(p_queue, tx->session->id);
	m_add_msgid(p_queue, tx->msgid);
	m_close(p_queue);
	tree_xset(&wait_queue_commit, tx->session->id, tx->session);
	smtp_filter_data_end(tx->session);
}

static void
smtp_tx_rollback(struct smtp_tx *tx)
{
	m_create(p_queue, IMSG_SMTP_MESSAGE_ROLLBACK, 0, 0, -1);
	m_add_msgid(p_queue, tx->msgid);
	m_close(p_queue);
	smtp_report_tx_rollback(tx->session, tx->msgid);
	smtp_report_tx_reset(tx->session, tx->msgid);
	smtp_filter_data_end(tx->session);
}

static int
smtp_tx_dataline(struct smtp_tx *tx, const char *line)
{
	struct rfc5322_result res;
	int r;

	log_trace(TRACE_SMTP, "<<< [MSG] %s", line);

	if (!strcmp(line, ".")) {
		smtp_report_protocol_client(tx->session, ".");
		log_trace(TRACE_SMTP, "<<< [EOM]");
		if (tx->error)
			return 1;
		line = NULL;
	}
	else {
		/* ignore data line if an error is set */
		if (tx->error)
			return 0;

		/* escape lines starting with a '.' */
		if (line[0] == '.')
			line += 1;
	}

	if (rfc5322_push(tx->parser, line) == -1) {
		log_warnx("failed to push dataline");
		tx->error = TX_ERROR_INTERNAL;
		return 0;
	}

	for(;;) {
		r = rfc5322_next(tx->parser, &res);
		switch (r) {
		case -1:
			if (errno == ENOMEM)
				tx->error = TX_ERROR_INTERNAL;
			else
				tx->error = TX_ERROR_MALFORMED;
			return 0;

		case RFC5322_NONE:
			/* Need more data */
			return 0;

		case RFC5322_HEADER_START:
			/* ignore bcc */
			if (!strcasecmp("Bcc", res.hdr))
				continue;

			if (!strcasecmp("To", res.hdr) ||
			    !strcasecmp("Cc", res.hdr) ||
			    !strcasecmp("From", res.hdr)) {
				rfc5322_unfold_header(tx->parser);
				continue;
			}

			if (!strcasecmp("Received", res.hdr)) {
				if (++tx->rcvcount >= MAX_HOPS_COUNT) {
					log_warnx("warn: loop detected");
					tx->error = TX_ERROR_LOOP;
					return 0;
				}
			}
			else if (!tx->has_date && !strcasecmp("Date", res.hdr))
				tx->has_date = 1;
			else if (!tx->has_message_id &&
			    !strcasecmp("Message-Id", res.hdr))
				tx->has_message_id = 1;

			smtp_message_printf(tx, "%s:%s\n", res.hdr, res.value);
			break;

		case RFC5322_HEADER_CONT:

			if (!strcasecmp("Bcc", res.hdr) ||
			    !strcasecmp("To", res.hdr) ||
			    !strcasecmp("Cc", res.hdr) ||
			    !strcasecmp("From", res.hdr))
				continue;

			smtp_message_printf(tx, "%s\n", res.value);
			break;

		case RFC5322_HEADER_END:
			if (!strcasecmp("To", res.hdr) ||
			    !strcasecmp("Cc", res.hdr) ||
			    !strcasecmp("From", res.hdr))
				header_domain_append_callback(tx, res.hdr,
				    res.value);
			break;

		case RFC5322_END_OF_HEADERS:
			if (tx->session->listener->local ||
			    tx->session->listener->port == htons(587)) {

				if (!tx->has_date) {
					log_debug("debug: %p: adding Date", tx);
					smtp_message_printf(tx, "Date: %s\n",
					    time_to_text(tx->time));
				}

				if (!tx->has_message_id) {
					log_debug("debug: %p: adding Message-ID", tx);
					smtp_message_printf(tx,
					    "Message-ID: <%016"PRIx64"@%s>\n",
					    generate_uid(),
					    tx->session->listener->hostname);
				}
			}
			break;

		case RFC5322_BODY_START:
		case RFC5322_BODY:
			smtp_message_printf(tx, "%s\n", res.value);
			break;

		case RFC5322_END_OF_MESSAGE:
			return 1;

		default:
			fatalx("%s", __func__);
		}
	}
}

static int
smtp_tx_filtered_dataline(struct smtp_tx *tx, const char *line)
{
	if (!strcmp(line, "."))
		line = NULL;
	else {
		/* ignore data line if an error is set */
		if (tx->error)
			return 0;
	}
	io_printf(tx->filter, "%s\n", line ? line : ".");
	return line ? 0 : 1;
}

static void
smtp_tx_eom(struct smtp_tx *tx)
{
	smtp_filter_phase(FILTER_COMMIT, tx->session, NULL);
}

static int
smtp_message_fd(struct smtp_tx *tx, int fd)
{
	struct smtp_session *s;

	s = tx->session;

	log_debug("smtp: %p: message fd %d", s, fd);

	if ((tx->ofile = fdopen(fd, "w")) == NULL) {
		close(fd);
		smtp_reply(s, "421 %s Temporary Error",
		    esc_code(ESC_STATUS_TEMPFAIL, ESC_OTHER_MAIL_SYSTEM_STATUS));
		smtp_enter_state(s, STATE_QUIT);
		return 0;
	}
	return 1;
}

static void
filter_session_io(struct io *io, int evt, void *arg)
{
	struct smtp_tx*tx = arg;
	char*line = NULL;
	ssize_t len;

	log_trace(TRACE_IO, "filter session io (smtp): %p: %s %s", tx, io_strevent(evt),
	    io_strio(io));

	switch (evt) {
	case IO_DATAIN:
	nextline:
		line = io_getline(tx->filter, &len);
		/* No complete line received */
		if (line == NULL)
			return;

		if (smtp_tx_dataline(tx, line)) {
			smtp_tx_eom(tx);
			return;
		}

		goto nextline;
	}
}

static void
smtp_filter_fd(struct smtp_tx *tx, int fd)
{
	struct smtp_session *s;

	s = tx->session;

	log_debug("smtp: %p: filter fd %d", s, fd);

	tx->filter = io_new();
	io_set_fd(tx->filter, fd);
	io_set_callback(tx->filter, filter_session_io, tx);
}

static void
smtp_message_begin(struct smtp_tx *tx)
{
	struct smtp_session *s;
	struct smtp_rcpt *rcpt;
	int	(*m_printf)(struct smtp_tx *, const char *, ...);

	m_printf = smtp_message_printf;
	if (tx->filter)
		m_printf = smtp_filter_printf;

	s = tx->session;

	log_debug("smtp: %p: message begin", s);

	smtp_reply(s, "354 Enter mail, end with \".\""
	    " on a line by itself");

	if (s->junk || (s->tx && s->tx->junk))
		m_printf(tx, "X-Spam: Yes\n");

	m_printf(tx, "Received: ");
	if (!(s->listener->flags & F_MASK_SOURCE)) {
		m_printf(tx, "from %s (%s %s%s%s)",
		    s->helo,
		    s->rdns,
		    s->ss.ss_family == AF_INET6 ? "" : "[",
		    ss_to_text(&s->ss),
		    s->ss.ss_family == AF_INET6 ? "" : "]");
	}
	m_printf(tx, "\n\tby %s (%s) with %sSMTP%s%s id %08x",
	    s->smtpname,
	    SMTPD_NAME,
	    s->flags & SF_EHLO ? "E" : "",
	    s->flags & SF_SECURE ? "S" : "",
	    s->flags & SF_AUTHENTICATED ? "A" : "",
	    tx->msgid);

	if (s->flags & SF_SECURE) {
		m_printf(tx, " (%s:%s:%d:%s)",
		    tls_conn_version(io_tls(s->io)),
		    tls_conn_cipher(io_tls(s->io)),
		    tls_conn_cipher_strength(io_tls(s->io)),
		    (s->flags & SF_VERIFIED) ? "YES" : "NO");

		if (s->listener->flags & F_RECEIVEDAUTH) {
			m_printf(tx, " auth=%s",
			    s->username[0] ? "yes" : "no");
			if (s->username[0])
				m_printf(tx, " user=%s", s->username);
		}
	}

	if (tx->rcptcount == 1) {
		rcpt = TAILQ_FIRST(&tx->rcpts);
		m_printf(tx, "\n\tfor <%s@%s>",
		    rcpt->maddr.user,
		    rcpt->maddr.domain);
	}

	m_printf(tx, ";\n\t%s\n", time_to_text(time(&tx->time)));

	smtp_enter_state(s, STATE_BODY);
}

static void
smtp_message_end(struct smtp_tx *tx)
{
	struct smtp_session *s;

	s = tx->session;

	log_debug("debug: %p: end of message, error=%d", s, tx->error);

	fclose(tx->ofile);
	tx->ofile = NULL;

	switch(tx->error) {
	case TX_OK:
		smtp_tx_commit(tx);
		return;

	case TX_ERROR_SIZE:
		smtp_reply(s, "554 %s %s: Transaction failed, message too big",
		    esc_code(ESC_STATUS_PERMFAIL, ESC_MESSAGE_TOO_BIG_FOR_SYSTEM),
		    esc_description(ESC_MESSAGE_TOO_BIG_FOR_SYSTEM));
		break;

	case TX_ERROR_LOOP:
		smtp_reply(s, "500 %s %s: Loop detected",
		   esc_code(ESC_STATUS_PERMFAIL, ESC_ROUTING_LOOP_DETECTED),
		   esc_description(ESC_ROUTING_LOOP_DETECTED));
		break;

	case TX_ERROR_MALFORMED:
		smtp_reply(s, "550 %s %s: Message is not RFC 2822 compliant",
		    esc_code(ESC_STATUS_PERMFAIL, ESC_DELIVERY_NOT_AUTHORIZED_MESSAGE_REFUSED),
		    esc_description(ESC_DELIVERY_NOT_AUTHORIZED_MESSAGE_REFUSED));
		break;

	case TX_ERROR_IO:
	case TX_ERROR_RESOURCES:
		smtp_reply(s, "421 %s Temporary Error",
		    esc_code(ESC_STATUS_TEMPFAIL, ESC_OTHER_MAIL_SYSTEM_STATUS));
		break;

	default:
		/* fatal? */
		smtp_reply(s, "421 Internal server error");
	}

	smtp_tx_rollback(tx);
	smtp_tx_free(tx);
	smtp_enter_state(s, STATE_HELO);
}

static int
smtp_filter_printf(struct smtp_tx *tx, const char *fmt, ...)
{
	va_list	ap;
	int	len;

	if (tx->error)
		return -1;

	va_start(ap, fmt);
	len = io_vprintf(tx->filter, fmt, ap);
	va_end(ap);

	if (len < 0) {
		log_warn("smtp-in: session %016"PRIx64": vfprintf", tx->session->id);
		tx->error = TX_ERROR_IO;
	}
	else
		tx->odatalen += len;

	return len;
}

static int
smtp_message_printf(struct smtp_tx *tx, const char *fmt, ...)
{
	va_list	ap;
	int	len;

	if (tx->error)
		return -1;

	va_start(ap, fmt);
	len = vfprintf(tx->ofile, fmt, ap);
	va_end(ap);

	if (len == -1) {
		log_warn("smtp-in: session %016"PRIx64": vfprintf", tx->session->id);
		tx->error = TX_ERROR_IO;
	}
	else
		tx->odatalen += len;

	return len;
}

#define CASE(x) case x : return #x

const char *
smtp_strstate(int state)
{
	static char	buf[32];

	switch (state) {
	CASE(STATE_NEW);
	CASE(STATE_CONNECTED);
	CASE(STATE_TLS);
	CASE(STATE_HELO);
	CASE(STATE_AUTH_INIT);
	CASE(STATE_AUTH_USERNAME);
	CASE(STATE_AUTH_PASSWORD);
	CASE(STATE_AUTH_FINALIZE);
	CASE(STATE_BODY);
	CASE(STATE_QUIT);
	default:
		(void)snprintf(buf, sizeof(buf), "STATE_??? (%d)", state);
		return (buf);
	}
}


static void
smtp_report_link_connect(struct smtp_session *s, const char *rdns, int fcrdns,
    const struct sockaddr_storage *ss_src,
    const struct sockaddr_storage *ss_dest)
{
	if (! SESSION_FILTERED(s))
		return;

	report_smtp_link_connect("smtp-in", s->id, rdns, fcrdns, ss_src, ss_dest);
}

static void
smtp_report_link_greeting(struct smtp_session *s,
    const char *domain)
{
	if (! SESSION_FILTERED(s))
		return;

	report_smtp_link_greeting("smtp-in", s->id, domain);
}

static void
smtp_report_link_identify(struct smtp_session *s, const char *method, const char *identity)
{
	if (! SESSION_FILTERED(s))
		return;

	report_smtp_link_identify("smtp-in", s->id, method, identity);
}

static void
smtp_report_link_tls(struct smtp_session *s, const char *ssl)
{
	if (! SESSION_FILTERED(s))
		return;

	report_smtp_link_tls("smtp-in", s->id, ssl);
}

static void
smtp_report_link_disconnect(struct smtp_session *s)
{
	if (! SESSION_FILTERED(s))
		return;

	report_smtp_link_disconnect("smtp-in", s->id);
}

static void
smtp_report_link_auth(struct smtp_session *s, const char *user, const char *result)
{
	if (! SESSION_FILTERED(s))
		return;

	report_smtp_link_auth("smtp-in", s->id, user, result);
}

static void
smtp_report_tx_reset(struct smtp_session *s, uint32_t msgid)
{
	if (! SESSION_FILTERED(s))
		return;

	report_smtp_tx_reset("smtp-in", s->id, msgid);
}

static void
smtp_report_tx_begin(struct smtp_session *s, uint32_t msgid)
{
	if (! SESSION_FILTERED(s))
		return;

	report_smtp_tx_begin("smtp-in", s->id, msgid);
}

static void
smtp_report_tx_mail(struct smtp_session *s, uint32_t msgid, const char *address, int ok)
{
	char	mailaddr[SMTPD_MAXMAILADDRSIZE];
	char    *p;

	if (! SESSION_FILTERED(s))
		return;

	if ((p = strchr(address, '<')) == NULL)
		return;
	(void)strlcpy(mailaddr, p + 1, sizeof mailaddr);
	if ((p = strchr(mailaddr, '>')) == NULL)
		return;
	*p = '\0';

	report_smtp_tx_mail("smtp-in", s->id, msgid, mailaddr, ok);
}

static void
smtp_report_tx_rcpt(struct smtp_session *s, uint32_t msgid, const char *address, int ok)
{
	char	mailaddr[SMTPD_MAXMAILADDRSIZE];
	char    *p;

	if (! SESSION_FILTERED(s))
		return;

	if ((p = strchr(address, '<')) == NULL)
		return;
	(void)strlcpy(mailaddr, p + 1, sizeof mailaddr);
	if ((p = strchr(mailaddr, '>')) == NULL)
		return;
	*p = '\0';

	report_smtp_tx_rcpt("smtp-in", s->id, msgid, mailaddr, ok);
}

static void
smtp_report_tx_envelope(struct smtp_session *s, uint32_t msgid, uint64_t evpid)
{
	if (! SESSION_FILTERED(s))
		return;

	report_smtp_tx_envelope("smtp-in", s->id, msgid, evpid);
}

static void
smtp_report_tx_data(struct smtp_session *s, uint32_t msgid, int ok)
{
	if (! SESSION_FILTERED(s))
		return;

	report_smtp_tx_data("smtp-in", s->id, msgid, ok);
}

static void
smtp_report_tx_commit(struct smtp_session *s, uint32_t msgid, size_t msgsz)
{
	if (! SESSION_FILTERED(s))
		return;

	report_smtp_tx_commit("smtp-in", s->id, msgid, msgsz);
}

static void
smtp_report_tx_rollback(struct smtp_session *s, uint32_t msgid)
{
	if (! SESSION_FILTERED(s))
		return;

	report_smtp_tx_rollback("smtp-in", s->id, msgid);
}

static void
smtp_report_protocol_client(struct smtp_session *s, const char *command)
{
	if (! SESSION_FILTERED(s))
		return;

	report_smtp_protocol_client("smtp-in", s->id, command);
}

static void
smtp_report_protocol_server(struct smtp_session *s, const char *response)
{
	if (! SESSION_FILTERED(s))
		return;

	report_smtp_protocol_server("smtp-in", s->id, response);
}

static void
smtp_report_filter_response(struct smtp_session *s, int phase, int response, const char *param)
{
	if (! SESSION_FILTERED(s))
		return;

	report_smtp_filter_response("smtp-in", s->id, phase, response, param);
}

static void
smtp_report_timeout(struct smtp_session *s)
{
	if (! SESSION_FILTERED(s))
		return;

	report_smtp_timeout("smtp-in", s->id);
}
