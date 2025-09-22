/*	$OpenBSD: smtp_client.c,v 1.17 2022/12/28 21:30:18 jmc Exp $	*/

/*
 * Copyright (c) 2018 Eric Faurot <eric@openbsd.org>
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

#include <netinet/in.h>

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <resolv.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "log.h"
#include "ioev.h"
#include "smtp.h"

#define	TRACE_SMTPCLT	2
#define	TRACE_IO	3

enum {
	STATE_INIT = 0,
	STATE_BANNER,
	STATE_EHLO,
	STATE_HELO,
	STATE_LHLO,
	STATE_STARTTLS,
	STATE_AUTH,
	STATE_AUTH_PLAIN,
	STATE_AUTH_LOGIN,
	STATE_AUTH_LOGIN_USER,
	STATE_AUTH_LOGIN_PASS,
	STATE_READY,
	STATE_MAIL,
	STATE_RCPT,
	STATE_DATA,
	STATE_BODY,
	STATE_EOM,
	STATE_RSET,
	STATE_QUIT,

	STATE_LAST
};

#define base64_encode	__b64_ntop
#define base64_decode	__b64_pton

#define FLAG_TLS		0x01
#define FLAG_TLS_VERIFIED	0x02

#define SMTP_EXT_STARTTLS	0x01
#define SMTP_EXT_PIPELINING	0x02
#define SMTP_EXT_AUTH		0x04
#define SMTP_EXT_AUTH_PLAIN     0x08
#define SMTP_EXT_AUTH_LOGIN     0x10
#define SMTP_EXT_DSN		0x20
#define SMTP_EXT_SIZE		0x40

struct smtp_client {
	void			*tag;
	struct smtp_params	 params;

	int			 state;
	int			 flags;
	int			 ext;
	size_t			 ext_size;

	struct io		*io;
	char			*reply;
	size_t			 replysz;

	struct smtp_mail	*mail;
	int			 rcptidx;
	int			 rcptok;
};

void log_trace_verbose(int);
void log_trace(int, const char *, ...)
    __attribute__((format (printf, 2, 3)));

static void smtp_client_io(struct io *, int, void *);
static void smtp_client_free(struct smtp_client *);
static void smtp_client_state(struct smtp_client *, int);
static void smtp_client_abort(struct smtp_client *, int, const char *);
static void smtp_client_cancel(struct smtp_client *, int, const char *);
static void smtp_client_sendcmd(struct smtp_client *, char *, ...);
static void smtp_client_sendbody(struct smtp_client *);
static int smtp_client_readline(struct smtp_client *);
static int smtp_client_replycat(struct smtp_client *, const char *);
static void smtp_client_response(struct smtp_client *, const char *);
static void smtp_client_mail_abort(struct smtp_client *);
static void smtp_client_mail_status(struct smtp_client *, const char *);
static void smtp_client_rcpt_status(struct smtp_client *, struct smtp_rcpt *, const char *);

static const char *strstate[STATE_LAST] = {
	"INIT",
	"BANNER",
	"EHLO",
	"HELO",
	"LHLO",
	"STARTTLS",
	"AUTH",
	"AUTH_PLAIN",
	"AUTH_LOGIN",
	"AUTH_LOGIN_USER",
	"AUTH_LOGIN_PASS",
	"READY",
	"MAIL",
	"RCPT",
	"DATA",
	"BODY",
	"EOM",
	"RSET",
	"QUIT",
};

struct smtp_client *
smtp_connect(const struct smtp_params *params, void *tag)
{
	struct smtp_client *proto;

	proto = calloc(1, sizeof *proto);
	if (proto == NULL)
		return NULL;

	memmove(&proto->params, params, sizeof(*params));
	proto->tag = tag;
	proto->io = io_new();
	if (proto->io == NULL) {
		free(proto);
		return NULL;
	}
	io_set_callback(proto->io, smtp_client_io, proto);
	io_set_timeout(proto->io, proto->params.timeout);

	if (io_connect(proto->io, proto->params.dst, proto->params.src) == -1) {
		smtp_client_abort(proto, FAIL_CONN, io_error(proto->io));
		return NULL;
	}

	return proto;
}

void
smtp_cert_verified(struct smtp_client *proto, int verified)
{
	if (verified == CERT_OK)
		proto->flags |= FLAG_TLS_VERIFIED;

	else if (proto->params.tls_verify) {
		errno = EAUTH;
		smtp_client_abort(proto, FAIL_CONN,
		    "Invalid server certificate");
		return;
	}

	io_resume(proto->io, IO_IN);

	if (proto->state == STATE_INIT)
		smtp_client_state(proto, STATE_BANNER);
	else {
		/* Clear extensions before re-issueing an EHLO command. */
		proto->ext = 0;
		smtp_client_state(proto, STATE_EHLO);
	}
}

void
smtp_set_tls(struct smtp_client *proto, void *ctx)
{
	io_connect_tls(proto->io, ctx, proto->params.tls_servname);
}

void
smtp_quit(struct smtp_client *proto)
{
	if (proto->state != STATE_READY)
		fatalx("connection is not ready");

	smtp_client_state(proto, STATE_QUIT);
}

void
smtp_sendmail(struct smtp_client *proto, struct smtp_mail *mail)
{
	if (proto->state != STATE_READY)
		fatalx("connection is not ready");

	proto->mail = mail;
	smtp_client_state(proto, STATE_MAIL);
}

static void
smtp_client_free(struct smtp_client *proto)
{
	if (proto->mail)
		fatalx("current task should have been deleted already");

	smtp_closed(proto->tag, proto);

	if (proto->io)
		io_free(proto->io);

	free(proto->reply);
	free(proto);
}

/*
 * End the session immediately.
 */
static void
smtp_client_abort(struct smtp_client *proto, int err, const char *reason)
{
	smtp_failed(proto->tag, proto, err, reason);

	if (proto->mail)
		smtp_client_mail_abort(proto);

	smtp_client_free(proto);
}

/*
 * Properly close the session.
 */
static void
smtp_client_cancel(struct smtp_client *proto, int err, const char *reason)
{
	if (proto->mail)
		fatal("not supposed to have a mail");

	smtp_failed(proto->tag, proto, err, reason);

	smtp_client_state(proto, STATE_QUIT);
}

static void
smtp_client_state(struct smtp_client *proto, int newstate)
{
	struct smtp_rcpt *rcpt;
	char ibuf[LINE_MAX], obuf[LINE_MAX];
	size_t n;
	int oldstate;

	if (proto->reply)
		proto->reply[0] = '\0';

    again:
	oldstate = proto->state;
	proto->state = newstate;

	log_trace(TRACE_SMTPCLT, "%p: %s -> %s", proto,
	    strstate[oldstate],
	    strstate[newstate]);

	/* don't try this at home! */
#define smtp_client_state(_s, _st) do { newstate = _st; goto again; } while (0)

	switch (proto->state) {
	case STATE_BANNER:
		io_set_read(proto->io);
		break;

	case STATE_EHLO:
		smtp_client_sendcmd(proto, "EHLO %s", proto->params.helo);
		break;

	case STATE_HELO:
		smtp_client_sendcmd(proto, "HELO %s", proto->params.helo);
		break;

	case STATE_LHLO:
		smtp_client_sendcmd(proto, "LHLO %s", proto->params.helo);
		break;

	case STATE_STARTTLS:
		if (proto->params.tls_req == TLS_NO || proto->flags & FLAG_TLS)
			smtp_client_state(proto, STATE_AUTH);
		else if (proto->ext & SMTP_EXT_STARTTLS)
			smtp_client_sendcmd(proto, "STARTTLS");
		else if (proto->params.tls_req == TLS_FORCE)
			smtp_client_cancel(proto, FAIL_IMPL,
			    "TLS not supported by remote host");
		else
			smtp_client_state(proto, STATE_AUTH);
		break;

	case STATE_AUTH:
		if (!proto->params.auth_user)
			smtp_client_state(proto, STATE_READY);
		else if ((proto->flags & FLAG_TLS) == 0)
			smtp_client_cancel(proto, FAIL_IMPL,
			    "Authentication requires TLS");
		else if ((proto->ext & SMTP_EXT_AUTH) == 0)
			smtp_client_cancel(proto, FAIL_IMPL,
			    "AUTH not supported by remote host");
		else if (proto->ext & SMTP_EXT_AUTH_PLAIN)
			smtp_client_state(proto, STATE_AUTH_PLAIN);
		else if (proto->ext & SMTP_EXT_AUTH_LOGIN)
			smtp_client_state(proto, STATE_AUTH_LOGIN);
		else
			smtp_client_cancel(proto, FAIL_IMPL,
			    "No supported AUTH method");
		break;

	case STATE_AUTH_PLAIN:
		(void)strlcpy(ibuf, "-", sizeof(ibuf));
		(void)strlcat(ibuf, proto->params.auth_user, sizeof(ibuf));
		if (strlcat(ibuf, ":", sizeof(ibuf)) >= sizeof(ibuf)) {
			errno = EMSGSIZE;
			smtp_client_cancel(proto, FAIL_INTERNAL,
			    "credentials too large");
			break;
		}
		n = strlcat(ibuf, proto->params.auth_pass, sizeof(ibuf));
		if (n >= sizeof(ibuf)) {
			errno = EMSGSIZE;
			smtp_client_cancel(proto, FAIL_INTERNAL,
			    "credentials too large");
			break;
		}
		*strchr(ibuf, ':') = '\0';
		ibuf[0] = '\0';
		if (base64_encode(ibuf, n, obuf, sizeof(obuf)) == -1) {
			errno = EMSGSIZE;
			smtp_client_cancel(proto, FAIL_INTERNAL,
			    "credentials too large");
			break;
		}
		smtp_client_sendcmd(proto, "AUTH PLAIN %s", obuf);
		explicit_bzero(ibuf, sizeof ibuf);
		explicit_bzero(obuf, sizeof obuf);
		break;

	case STATE_AUTH_LOGIN:
		smtp_client_sendcmd(proto, "AUTH LOGIN");
		break;

	case STATE_AUTH_LOGIN_USER:
		if (base64_encode(proto->params.auth_user,
		    strlen(proto->params.auth_user), obuf,
		    sizeof(obuf)) == -1) {
			errno = EMSGSIZE;
			smtp_client_cancel(proto, FAIL_INTERNAL,
			    "credentials too large");
			break;
		}
		smtp_client_sendcmd(proto, "%s", obuf);
		explicit_bzero(obuf, sizeof obuf);
		break;

	case STATE_AUTH_LOGIN_PASS:
		if (base64_encode(proto->params.auth_pass,
		    strlen(proto->params.auth_pass), obuf,
		    sizeof(obuf)) == -1) {
			errno = EMSGSIZE;
			smtp_client_cancel(proto, FAIL_INTERNAL,
			    "credentials too large");
			break;
		}
		smtp_client_sendcmd(proto, "%s", obuf);
		explicit_bzero(obuf, sizeof obuf);
		break;

	case STATE_READY:
		smtp_ready(proto->tag, proto);
		break;

	case STATE_MAIL:
		if (proto->ext & SMTP_EXT_DSN)
			smtp_client_sendcmd(proto, "MAIL FROM:<%s>%s%s%s%s",
			    proto->mail->from,
			    proto->mail->dsn_ret ? " RET=" : "",
			    proto->mail->dsn_ret ? proto->mail->dsn_ret : "",
			    proto->mail->dsn_envid ? " ENVID=" : "",
			    proto->mail->dsn_envid ? proto->mail->dsn_envid : "");
		else
			smtp_client_sendcmd(proto, "MAIL FROM:<%s>",
			    proto->mail->from);
		break;

	case STATE_RCPT:
		if (proto->rcptidx == proto->mail->rcptcount) {
			smtp_client_state(proto, STATE_DATA);
			break;
		}
		rcpt = &proto->mail->rcpt[proto->rcptidx];
		if (proto->ext & SMTP_EXT_DSN)
			smtp_client_sendcmd(proto, "RCPT TO:<%s>%s%s%s%s",
			    rcpt->to,
			    rcpt->dsn_notify ? " NOTIFY=" : "",
			    rcpt->dsn_notify ? rcpt->dsn_notify : "",
			    rcpt->dsn_orcpt ? " ORCPT=" : "",
			    rcpt->dsn_orcpt ? rcpt->dsn_orcpt : "");
		else
			smtp_client_sendcmd(proto, "RCPT TO:<%s>", rcpt->to);
		break;

	case STATE_DATA:
		if (proto->rcptok == 0) {
			smtp_client_mail_abort(proto);
			smtp_client_state(proto, STATE_RSET);
		}
		else
			smtp_client_sendcmd(proto, "DATA");
		break;

	case STATE_BODY:
		fseek(proto->mail->fp, 0, SEEK_SET);
		smtp_client_sendbody(proto);
		break;

	case STATE_EOM:
		smtp_client_sendcmd(proto, ".");
		break;

	case STATE_RSET:
		smtp_client_sendcmd(proto, "RSET");
		break;

	case STATE_QUIT:
		smtp_client_sendcmd(proto, "QUIT");
		break;

	default:
		fatalx("%s: bad state %d", __func__, proto->state);
	}
#undef smtp_client_state
}

/*
 * Handle a response to an SMTP command
 */
static void
smtp_client_response(struct smtp_client *proto, const char *line)
{
	struct smtp_rcpt *rcpt;
	int i, seen;

	switch (proto->state) {
	case STATE_BANNER:
		if (line[0] != '2')
			smtp_client_abort(proto, FAIL_RESP, line);
		else if (proto->params.lmtp)
			smtp_client_state(proto, STATE_LHLO);
		else
			smtp_client_state(proto, STATE_EHLO);
		break;

	case STATE_EHLO:
		if (line[0] != '2') {
			/*
			 * Either rejected or not implemented.  If we want to
			 * use EHLO extensions, report an SMTP error.
			 * Otherwise, fallback to using HELO.
			 */
			if ((proto->params.tls_req == TLS_FORCE) ||
			    (proto->params.auth_user))
				smtp_client_cancel(proto, FAIL_RESP, line);
			else
				smtp_client_state(proto, STATE_HELO);
			break;
		}
		smtp_client_state(proto, STATE_STARTTLS);
		break;

	case STATE_HELO:
		if (line[0] != '2')
			smtp_client_cancel(proto, FAIL_RESP, line);
		else
			smtp_client_state(proto, STATE_READY);
		break;

	case STATE_LHLO:
		if (line[0] != '2')
			smtp_client_cancel(proto, FAIL_RESP, line);
		else
			smtp_client_state(proto, STATE_READY);
		break;

	case STATE_STARTTLS:
		if (line[0] != '2') {
			if ((proto->params.tls_req == TLS_FORCE) ||
			    (proto->params.auth_user)) {
				smtp_client_cancel(proto, FAIL_RESP, line);
				break;
			}
			smtp_client_state(proto, STATE_AUTH);
		}
		else
			smtp_require_tls(proto->tag, proto);
		break;

	case STATE_AUTH_PLAIN:
		if (line[0] != '2')
			smtp_client_cancel(proto, FAIL_RESP, line);
		else
			smtp_client_state(proto, STATE_READY);
		break;

	case STATE_AUTH_LOGIN:
		if (strncmp(line, "334 ", 4))
			smtp_client_cancel(proto, FAIL_RESP, line);
		else
			smtp_client_state(proto, STATE_AUTH_LOGIN_USER);
		break;

	case STATE_AUTH_LOGIN_USER:
		if (strncmp(line, "334 ", 4))
			smtp_client_cancel(proto, FAIL_RESP, line);
		else
			smtp_client_state(proto, STATE_AUTH_LOGIN_PASS);
		break;

	case STATE_AUTH_LOGIN_PASS:
		if (line[0] != '2')
			smtp_client_cancel(proto, FAIL_RESP, line);
		else
			smtp_client_state(proto, STATE_READY);
		break;

	case STATE_MAIL:
		if (line[0] != '2') {
			smtp_client_mail_status(proto, line);
			smtp_client_state(proto, STATE_RSET);
		}
		else
			smtp_client_state(proto, STATE_RCPT);
		break;

	case STATE_RCPT:
		rcpt = &proto->mail->rcpt[proto->rcptidx++];
		if (line[0] != '2')
			smtp_client_rcpt_status(proto, rcpt, line);
		else {
			proto->rcptok++;
			smtp_client_state(proto, STATE_RCPT);
		}
		break;

	case STATE_DATA:
		if (line[0] != '2' && line[0] != '3') {
			smtp_client_mail_status(proto, line);
			smtp_client_state(proto, STATE_RSET);
		}
		else
			smtp_client_state(proto, STATE_BODY);
		break;

	case STATE_EOM:
		if (proto->params.lmtp) {
			/*
			 * LMTP reports a status of each accepted RCPT.
			 * Report status for the first pending RCPT and read
			 * more lines if another rcpt needs a status.
			 */
			for (i = 0, seen = 0; i < proto->mail->rcptcount; i++) {
				rcpt = &proto->mail->rcpt[i];
				if (rcpt->done)
					continue;
				if (seen) {
					io_set_read(proto->io);
					return;
				}
				smtp_client_rcpt_status(proto,
				    &proto->mail->rcpt[i], line);
				seen = 1;
			}
		}
		smtp_client_mail_status(proto, line);
		smtp_client_state(proto, STATE_READY);
		break;

	case STATE_RSET:
		if (line[0] != '2')
			smtp_client_cancel(proto, FAIL_RESP, line);
		else
			smtp_client_state(proto, STATE_READY);
		break;

	case STATE_QUIT:
		smtp_client_free(proto);
		break;

	default:
		fatalx("%s: bad state %d", __func__, proto->state);
	}
}

static void
smtp_client_io(struct io *io, int evt, void *arg)
{
	struct smtp_client *proto = arg;

	log_trace(TRACE_IO, "%p: %s %s", proto, io_strevent(evt), io_strio(io));

	switch (evt) {
	case IO_CONNECTED:
		if (proto->params.tls_req == TLS_SMTPS) {
			io_set_write(io);
			smtp_require_tls(proto->tag, proto);
		}
		else
			smtp_client_state(proto, STATE_BANNER);
		break;

	case IO_TLSREADY:
		proto->flags |= FLAG_TLS;
		if (proto->state == STATE_INIT)
			smtp_client_state(proto, STATE_BANNER);
		else {
			/* Clear extensions before re-issueing an EHLO command. */
			proto->ext = 0;
			smtp_client_state(proto, STATE_EHLO);
		}
		break;

	case IO_DATAIN:
		while (smtp_client_readline(proto))
			;
		break;

	case IO_LOWAT:
		if (proto->state == STATE_BODY)
			smtp_client_sendbody(proto);
		else
			io_set_read(io);
		break;

	case IO_TIMEOUT:
		errno = ETIMEDOUT;
		smtp_client_abort(proto, FAIL_CONN, "Connection timeout");
		break;

	case IO_ERROR:
		smtp_client_abort(proto, FAIL_CONN, io_error(io));
		break;

	case IO_DISCONNECTED:
		smtp_client_abort(proto, FAIL_CONN, io_error(io));
		break;

	default:
		fatalx("%s: bad event %d", __func__, evt);
	}
}

/*
 * return 1 if a new  line is expected.
 */
static int
smtp_client_readline(struct smtp_client *proto)
{
	const char *e;
	size_t len;
	char *line, *msg, *p;
	int cont;

	line = io_getline(proto->io, &len);
	if (line == NULL) {
		if (io_datalen(proto->io) >= proto->params.linemax)
			smtp_client_abort(proto, FAIL_PROTO, "Line too long");
		return 0;
	}

	/* Strip trailing '\r' */
	if (len && line[len - 1] == '\r')
		line[--len] = '\0';

	log_trace(TRACE_SMTPCLT, "%p: <<< %s", proto, line);

	/* Validate SMTP  */
	if (len > 3) {
		msg = line + 4;
		cont = (line[3] == '-');
	} else if (len == 3) {
		msg = line + 3;
		cont = 0;
	} else {
		smtp_client_abort(proto, FAIL_PROTO, "Response too short");
		return 0;
	}

	/* Validate reply code. */
	if (line[0] < '2' || line[0] > '5' || !isdigit((unsigned char)line[1]) ||
	    !isdigit((unsigned char)line[2])) {
		smtp_client_abort(proto, FAIL_PROTO, "Invalid reply code");
		return 0;
	}

	/* Validate reply message. */
	for (p = msg; *p; p++)
		if (!isprint((unsigned char)*p)) {
			smtp_client_abort(proto, FAIL_PROTO,
			    "Non-printable characters in response");
			return 0;
	}

	/* Read extensions. */
	if (proto->state == STATE_EHLO) {
		if (strcmp(msg, "STARTTLS") == 0)
			proto->ext |= SMTP_EXT_STARTTLS;
		else if (strncmp(msg, "AUTH ", 5) == 0) {
			proto->ext |= SMTP_EXT_AUTH;
			if ((p = strstr(msg, " PLAIN")) &&
			    (*(p+6) == '\0' || *(p+6) == ' '))
				proto->ext |= SMTP_EXT_AUTH_PLAIN;
			if ((p = strstr(msg, " LOGIN")) &&
			    (*(p+6) == '\0' || *(p+6) == ' '))
				proto->ext |= SMTP_EXT_AUTH_LOGIN;
			}
		else if (strcmp(msg, "PIPELINING") == 0)
			proto->ext |= SMTP_EXT_PIPELINING;
		else if (strcmp(msg, "DSN") == 0)
			proto->ext |= SMTP_EXT_DSN;
		else if (strncmp(msg, "SIZE ", 5) == 0) {
			proto->ext_size = strtonum(msg + 5, 0, SIZE_T_MAX, &e);
			if (e == NULL)
				proto->ext |= SMTP_EXT_SIZE;
		}
	}

	if (smtp_client_replycat(proto, line) == -1) {
		smtp_client_abort(proto, FAIL_INTERNAL, NULL);
		return 0;
	}

	if (cont)
		return 1;

	if (io_datalen(proto->io)) {
		/*
		 * There should be no pending data after a response is read,
		 * except for the multiple status lines after a LMTP message.
		 * It can also happen with pipelineing, but we don't do that
		 * for now.
		 */
		if (!(proto->params.lmtp && proto->state == STATE_EOM)) {
			smtp_client_abort(proto, FAIL_PROTO, "Trailing data");
			return 0;
		}
	}

	io_set_write(proto->io);
	smtp_client_response(proto, proto->reply);
	return 0;
}

/*
 * Concatenate the given response line.
 */
static int
smtp_client_replycat(struct smtp_client *proto, const char *line)
{
	size_t len;
	char *tmp;
	int first;

	if (proto->reply && proto->reply[0]) {
		/*
		 * If the line is the continuation of an multi-line response,
		 * skip the status and ESC parts. First, skip the status, then
		 * skip the separator amd ESC if found.
		 */
		first = 0;
		line += 3;
		if (line[0]) {
			line += 1;
			if (isdigit((unsigned char)line[0]) && line[1] == '.' &&
			    isdigit((unsigned char)line[2]) && line[3] == '.' &&
			    isdigit((unsigned char)line[4]) &&
			    isspace((unsigned char)line[5]))
				line += 5;
		}
	} else
		first = 1;

	if (proto->reply) {
		len = strlcat(proto->reply, line, proto->replysz);
		if (len < proto->replysz)
			return 0;
	}
	else
		len = strlen(line);

	if (len > proto->params.ibufmax) {
		errno = EMSGSIZE;
		return -1;
	}

	/* Allocate by multiples of 2^8 */
	len += (len % 256) ? (256 - (len % 256)) : 0;

	tmp = realloc(proto->reply, len);
	if (tmp == NULL)
		return -1;
	if (proto->reply == NULL)
		tmp[0] = '\0';

	proto->reply = tmp;
	proto->replysz = len;
	(void)strlcat(proto->reply, line, proto->replysz);

	/* Replace the separator with a space for the first line. */
	if (first && proto->reply[3])
		proto->reply[3] = ' ';

	return 0;
}

static void
smtp_client_sendbody(struct smtp_client *proto)
{
	ssize_t len;
	size_t sz = 0, total, w;
	char *ln = NULL;
	int n;

	total = io_queued(proto->io);
	w = 0;

	while (total < proto->params.obufmax) {
		if ((len = getline(&ln, &sz, proto->mail->fp)) == -1)
			break;
		if (ln[len - 1] == '\n')
			ln[len - 1] = '\0';
		n = io_printf(proto->io, "%s%s\r\n", *ln == '.'?".":"", ln);
		if (n == -1) {
			free(ln);
			smtp_client_abort(proto, FAIL_INTERNAL, NULL);
			return;
		}
		total += n;
		w += n;
	}
	free(ln);

	if (ferror(proto->mail->fp)) {
		smtp_client_abort(proto, FAIL_INTERNAL, "Cannot read message");
		return;
	}

	log_trace(TRACE_SMTPCLT, "%p: >>> [...%zd bytes...]", proto, w);

	if (feof(proto->mail->fp))
		smtp_client_state(proto, STATE_EOM);
}

static void
smtp_client_sendcmd(struct smtp_client *proto, char *fmt, ...)
{
	va_list ap;
	char *p;
	int len;

	va_start(ap, fmt);
	len = vasprintf(&p, fmt, ap);
	va_end(ap);

	if (len == -1) {
		smtp_client_abort(proto, FAIL_INTERNAL, NULL);
		return;
	}

	log_trace(TRACE_SMTPCLT, "mta: %p: >>> %s", proto, p);

	len = io_printf(proto->io, "%s\r\n", p);
	free(p);

	if (len == -1)
		smtp_client_abort(proto, FAIL_INTERNAL, NULL);
}

static void
smtp_client_mail_status(struct smtp_client *proto, const char *status)
{
	int i;

	for (i = 0; i < proto->mail->rcptcount; i++)
		smtp_client_rcpt_status(proto, &proto->mail->rcpt[i], status);

	smtp_done(proto->tag, proto, proto->mail);
	proto->mail = NULL;
}

static void
smtp_client_mail_abort(struct smtp_client *proto)
{
	smtp_done(proto->tag, proto, proto->mail);
	proto->mail = NULL;
}

static void
smtp_client_rcpt_status(struct smtp_client *proto, struct smtp_rcpt *rcpt, const char *line)
{
	struct smtp_status status;

	if (rcpt->done)
		return;

	rcpt->done = 1;
	status.rcpt = rcpt;
	status.cmd = strstate[proto->state];
	status.status = line;
	smtp_status(proto->tag, proto, &status);
}
