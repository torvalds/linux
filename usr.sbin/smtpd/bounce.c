/*	$OpenBSD: bounce.c,v 1.90 2023/05/31 16:51:46 op Exp $	*/

/*
 * Copyright (c) 2009 Gilles Chehade <gilles@poolp.org>
 * Copyright (c) 2009 Jacek Masiulaniec <jacekm@dobremiasto.net>
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

#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "smtpd.h"
#include "log.h"

#define BOUNCE_MAXRUN	2
#define BOUNCE_HIWAT	65535

enum {
	BOUNCE_EHLO,
	BOUNCE_MAIL,
	BOUNCE_RCPT,
	BOUNCE_DATA,
	BOUNCE_DATA_NOTICE,
	BOUNCE_DATA_MESSAGE,
	BOUNCE_DATA_END,
	BOUNCE_QUIT,
	BOUNCE_CLOSE,
};

struct bounce_envelope {
	TAILQ_ENTRY(bounce_envelope)	 entry;
	uint64_t			 id;
	struct mailaddr			 dest;
	char				*report;
	uint8_t				 esc_class;
	uint8_t				 esc_code;
};

struct bounce_message {
	SPLAY_ENTRY(bounce_message)	 sp_entry;
	TAILQ_ENTRY(bounce_message)	 entry;
	uint32_t			 msgid;
	struct delivery_bounce		 bounce;
	char				*smtpname;
	char				*to;
	time_t				 timeout;
	TAILQ_HEAD(, bounce_envelope)	 envelopes;
};

struct bounce_session {
	char				*smtpname;
	struct bounce_message		*msg;
	FILE				*msgfp;
	int				 state;
	struct io			*io;
	uint64_t			 boundary;
};

SPLAY_HEAD(bounce_message_tree, bounce_message);
static int bounce_message_cmp(const struct bounce_message *,
    const struct bounce_message *);
SPLAY_PROTOTYPE(bounce_message_tree, bounce_message, sp_entry,
    bounce_message_cmp);

static void bounce_drain(void);
static void bounce_send(struct bounce_session *, const char *, ...)
	__attribute__((__format__ (printf, 2, 3)));
static int  bounce_next_message(struct bounce_session *);
static int  bounce_next(struct bounce_session *);
static void bounce_delivery(struct bounce_message *, int, const char *);
static void bounce_status(struct bounce_session *, const char *, ...)
	__attribute__((__format__ (printf, 2, 3)));
static void bounce_io(struct io *, int, void *);
static void bounce_timeout(int, short, void *);
static void bounce_free(struct bounce_session *);
static const char *action_str(const struct delivery_bounce *);

static struct tree			wait_fd;
static struct bounce_message_tree	messages;
static TAILQ_HEAD(, bounce_message)	pending;

static int				nmessage = 0;
static int				running = 0;
static struct event			ev_timer;

static void
bounce_init(void)
{
	static int	init = 0;

	if (init == 0) {
		TAILQ_INIT(&pending);
		SPLAY_INIT(&messages);
		tree_init(&wait_fd);
		evtimer_set(&ev_timer, bounce_timeout, NULL);
		init = 1;
	}
}

void
bounce_add(uint64_t evpid)
{
	char			 buf[LINE_MAX], *line;
	struct envelope		 evp;
	struct bounce_message	 key, *msg;
	struct bounce_envelope	*be;

	bounce_init();

	if (queue_envelope_load(evpid, &evp) == 0) {
		m_create(p_scheduler, IMSG_QUEUE_DELIVERY_PERMFAIL, 0, 0, -1);
		m_add_evpid(p_scheduler, evpid);
		m_close(p_scheduler);
		return;
	}

	if (evp.type != D_BOUNCE)
		fatalx("bounce: evp:%016" PRIx64 " is not of type D_BOUNCE!",
		    evp.id);

	key.msgid = evpid_to_msgid(evpid);
	key.bounce = evp.agent.bounce;
	key.smtpname = evp.smtpname;

	switch (evp.esc_class) {
	case ESC_STATUS_OK:
		key.bounce.type = B_DELIVERED;
		break;
	case ESC_STATUS_TEMPFAIL:
		key.bounce.type = B_DELAYED;
		break;
	default:
		key.bounce.type = B_FAILED;
	}

	key.bounce.dsn_ret = evp.dsn_ret;
	key.bounce.ttl = evp.ttl;
	msg = SPLAY_FIND(bounce_message_tree, &messages, &key);
	if (msg == NULL) {
		msg = xcalloc(1, sizeof(*msg));
		msg->msgid = key.msgid;
		msg->bounce = key.bounce;

		TAILQ_INIT(&msg->envelopes);

		msg->smtpname = xstrdup(evp.smtpname);
		(void)snprintf(buf, sizeof(buf), "%s@%s", evp.sender.user,
		    evp.sender.domain);
		msg->to = xstrdup(buf);
		nmessage += 1;
		SPLAY_INSERT(bounce_message_tree, &messages, msg);
		log_debug("debug: bounce: new message %08" PRIx32,
		    msg->msgid);
		stat_increment("bounce.message", 1);
	} else
		TAILQ_REMOVE(&pending, msg, entry);

	line = evp.errorline;
	if (strlen(line) > 4 && (*line == '1' || *line == '6'))
		line += 4;
	(void)snprintf(buf, sizeof(buf), "%s@%s: %s", evp.dest.user,
	    evp.dest.domain, line);

	be = xmalloc(sizeof *be);
	be->id = evpid;
	be->report = xstrdup(buf);
	(void)strlcpy(be->dest.user, evp.dest.user, sizeof(be->dest.user));
	(void)strlcpy(be->dest.domain, evp.dest.domain,
	    sizeof(be->dest.domain));
	be->esc_class = evp.esc_class;
	be->esc_code = evp.esc_code;
	TAILQ_INSERT_TAIL(&msg->envelopes, be, entry);
	log_debug("debug: bounce: adding report %16"PRIx64": %s", be->id, be->report);

	msg->timeout = time(NULL) + 1;
	TAILQ_INSERT_TAIL(&pending, msg, entry);

	stat_increment("bounce.envelope", 1);
	bounce_drain();
}

void
bounce_fd(int fd)
{
	struct bounce_session	*s;
	struct bounce_message	*msg;

	log_debug("debug: bounce: got enqueue socket %d", fd);

	if (fd == -1 || TAILQ_EMPTY(&pending)) {
		log_debug("debug: bounce: cancelling");
		if (fd != -1)
			close(fd);
		running -= 1;
		bounce_drain();
		return;
	}

	msg = TAILQ_FIRST(&pending);

	s = xcalloc(1, sizeof(*s));
	s->smtpname = xstrdup(msg->smtpname);
	s->state = BOUNCE_EHLO;
	s->io = io_new();
	io_set_callback(s->io, bounce_io, s);
	io_set_fd(s->io, fd);
	io_set_timeout(s->io, 30000);
	io_set_read(s->io);
	s->boundary = generate_uid();

	log_debug("debug: bounce: new session %p", s);
	stat_increment("bounce.session", 1);
}

static void
bounce_timeout(int fd, short ev, void *arg)
{
	log_debug("debug: bounce: timeout");

	bounce_drain();
}

static void
bounce_drain(void)
{
	struct bounce_message	*msg;
	struct timeval		 tv;
	time_t			 t;

	log_debug("debug: bounce: drain: nmessage=%d running=%d",
	    nmessage, running);

	while (1) {
		if (running >= BOUNCE_MAXRUN) {
			log_debug("debug: bounce: max session reached");
			return;
		}

		if (nmessage == 0) {
			log_debug("debug: bounce: no more messages");
			return;
		}

		if (running >= nmessage) {
			log_debug("debug: bounce: enough sessions running");
			return;
		}

		if ((msg = TAILQ_FIRST(&pending)) == NULL) {
			log_debug("debug: bounce: no more pending messages");
			return;
		}

		t = time(NULL);
		if (msg->timeout > t) {
			log_debug("debug: bounce: next message not ready yet");
			if (!evtimer_pending(&ev_timer, NULL)) {
				log_debug("debug: bounce: setting timer");
				tv.tv_sec = msg->timeout - t;
				tv.tv_usec = 0;
				evtimer_add(&ev_timer, &tv);
			}
			return;
		}

		log_debug("debug: bounce: requesting new enqueue socket...");
		m_compose(p_dispatcher, IMSG_QUEUE_SMTP_SESSION, 0, 0, -1, NULL, 0);

		running += 1;
	}
}

static void
bounce_send(struct bounce_session *s, const char *fmt, ...)
{
	va_list	 ap;
	char	*p;
	int	 len;

	va_start(ap, fmt);
	if ((len = vasprintf(&p, fmt, ap)) == -1)
		fatal("bounce: vasprintf");
	va_end(ap);

	log_trace(TRACE_BOUNCE, "bounce: %p: >>> %s", s, p);

	io_xprintf(s->io, "%s\r\n", p);

	free(p);
}

static const char *
bounce_duration(long long d)
{
	static char buf[32];

	if (d < 60) {
		(void)snprintf(buf, sizeof buf, "%lld second%s", d,
		    (d == 1) ? "" : "s");
	} else if (d < 3600) {
		d = d / 60;
		(void)snprintf(buf, sizeof buf, "%lld minute%s", d,
		    (d == 1) ? "" : "s");
	}
	else if (d < 3600 * 24) {
		d = d / 3600;
		(void)snprintf(buf, sizeof buf, "%lld hour%s", d,
		    (d == 1) ? "" : "s");
	}
	else {
		d = d / (3600 * 24);
		(void)snprintf(buf, sizeof buf, "%lld day%s", d,
		    (d == 1) ? "" : "s");
	}
	return (buf);
}

#define NOTICE_INTRO							    \
	"    Hi!\r\n\r\n"						    \
	"    This is the MAILER-DAEMON, please DO NOT REPLY to this email.\r\n"

const char *notice_error =
    "    An error has occurred while attempting to deliver a message for\r\n"
    "    the following list of recipients:\r\n\r\n";

const char *notice_warning =
    "    A message is delayed for more than %s for the following\r\n"
    "    list of recipients:\r\n\r\n";

const char *notice_warning2 =
    "    Please note that this is only a temporary failure report.\r\n"
    "    The message is kept in the queue for up to %s.\r\n"
    "    You DO NOT NEED to re-send the message to these recipients.\r\n\r\n";

const char *notice_success =
    "    Your message was successfully delivered to these recipients.\r\n\r\n";

const char *notice_relay =
    "    Your message was relayed to these recipients.\r\n\r\n";

static int
bounce_next_message(struct bounce_session *s)
{
	struct bounce_message	*msg;
	char			 buf[LINE_MAX];
	int			 fd;
	time_t			 now;

    again:

	now = time(NULL);

	TAILQ_FOREACH(msg, &pending, entry) {
		if (msg->timeout > now)
			continue;
		if (strcmp(msg->smtpname, s->smtpname))
			continue;
		break;
	}
	if (msg == NULL)
		return (0);

	TAILQ_REMOVE(&pending, msg, entry);
	SPLAY_REMOVE(bounce_message_tree, &messages, msg);

	if ((fd = queue_message_fd_r(msg->msgid)) == -1) {
		bounce_delivery(msg, IMSG_QUEUE_DELIVERY_TEMPFAIL,
		    "Could not open message fd");
		goto again;
	}

	if ((s->msgfp = fdopen(fd, "r")) == NULL) {
		(void)snprintf(buf, sizeof(buf), "fdopen: %s", strerror(errno));
		log_warn("warn: bounce: fdopen");
		close(fd);
		bounce_delivery(msg, IMSG_QUEUE_DELIVERY_TEMPFAIL, buf);
		goto again;
	}

	s->msg = msg;
	return (1);
}

static int
bounce_next(struct bounce_session *s)
{
	struct bounce_envelope	*evp;
	char			*line = NULL;
	size_t			 n, sz = 0;
	ssize_t			 len;

	switch (s->state) {
	case BOUNCE_EHLO:
		bounce_send(s, "EHLO %s", s->smtpname);
		s->state = BOUNCE_MAIL;
		break;

	case BOUNCE_MAIL:
	case BOUNCE_DATA_END:
		log_debug("debug: bounce: %p: getting next message...", s);
		if (bounce_next_message(s) == 0) {
			log_debug("debug: bounce: %p: no more messages", s);
			bounce_send(s, "QUIT");
			s->state = BOUNCE_CLOSE;
			break;
		}
		log_debug("debug: bounce: %p: found message %08"PRIx32,
		    s, s->msg->msgid);
		bounce_send(s, "MAIL FROM: <>");
		s->state = BOUNCE_RCPT;
		break;

	case BOUNCE_RCPT:
		bounce_send(s, "RCPT TO: <%s>", s->msg->to);
		s->state = BOUNCE_DATA;
		break;

	case BOUNCE_DATA:
		bounce_send(s, "DATA");
		s->state = BOUNCE_DATA_NOTICE;
		break;

	case BOUNCE_DATA_NOTICE:
		/* Construct an appropriate notice. */

		io_xprintf(s->io,
		    "Subject: Delivery status notification: %s\r\n"
		    "From: Mailer Daemon <MAILER-DAEMON@%s>\r\n"
		    "To: %s\r\n"
		    "Date: %s\r\n"
		    "MIME-Version: 1.0\r\n"
		    "Content-Type: multipart/mixed;"
		    "boundary=\"%16" PRIu64 "/%s\"\r\n"
		    "\r\n"
		    "This is a MIME-encapsulated message.\r\n"
		    "\r\n",
		    action_str(&s->msg->bounce),
		    s->smtpname,
		    s->msg->to,
		    time_to_text(time(NULL)),
		    s->boundary,
		    s->smtpname);

		io_xprintf(s->io,
		    "--%16" PRIu64 "/%s\r\n"
		    "Content-Description: Notification\r\n"
		    "Content-Type: text/plain; charset=us-ascii\r\n"
		    "\r\n"
		    NOTICE_INTRO
		    "\r\n",
		    s->boundary, s->smtpname);

		switch (s->msg->bounce.type) {
		case B_FAILED:
			io_xprint(s->io, notice_error);
			break;
		case B_DELAYED:
			io_xprintf(s->io, notice_warning,
			    bounce_duration(s->msg->bounce.delay));
			break;
		case B_DELIVERED:
			io_xprint(s->io, s->msg->bounce.mta_without_dsn ?
			    notice_relay : notice_success);
			break;
		default:
			log_warn("warn: bounce: unknown bounce_type");
		}

		TAILQ_FOREACH(evp, &s->msg->envelopes, entry) {
			io_xprint(s->io, evp->report);
			io_xprint(s->io, "\r\n");
		}
		io_xprint(s->io, "\r\n");

		if (s->msg->bounce.type == B_DELAYED)
			io_xprintf(s->io, notice_warning2,
			    bounce_duration(s->msg->bounce.ttl));

		io_xprintf(s->io,
		    "    Below is a copy of the original message:\r\n"
		    "\r\n");

		io_xprintf(s->io,
		    "--%16" PRIu64 "/%s\r\n"
		    "Content-Description: Delivery Report\r\n"
		    "Content-Type: message/delivery-status\r\n"
		    "\r\n",
		    s->boundary, s->smtpname);

		io_xprintf(s->io,
		    "Reporting-MTA: dns; %s\r\n"
		    "\r\n",
		    s->smtpname);

		TAILQ_FOREACH(evp, &s->msg->envelopes, entry) {
			io_xprintf(s->io,
			    "Final-Recipient: rfc822; %s@%s\r\n"
			    "Action: %s\r\n"
			    "Status: %s\r\n"
			    "\r\n",
			    evp->dest.user,
			    evp->dest.domain,
			    action_str(&s->msg->bounce),
			    esc_code(evp->esc_class, evp->esc_code));
		}

		log_trace(TRACE_BOUNCE, "bounce: %p: >>> [... %zu bytes ...]",
		    s, io_queued(s->io));

		s->state = BOUNCE_DATA_MESSAGE;
		break;

	case BOUNCE_DATA_MESSAGE:
		io_xprintf(s->io,
		    "--%16" PRIu64 "/%s\r\n"
		    "Content-Description: Message headers\r\n"
		    "Content-Type: text/rfc822-headers\r\n"
		    "\r\n",
		    s->boundary, s->smtpname);

		n = io_queued(s->io);
		while (io_queued(s->io) < BOUNCE_HIWAT) {
			if ((len = getline(&line, &sz, s->msgfp)) == -1)
				break;
			if (len == 1 && line[0] == '\n' && /* end of headers */
			    (s->msg->bounce.type != B_FAILED ||
			    s->msg->bounce.dsn_ret != DSN_RETFULL)) {
				free(line);
				fclose(s->msgfp);
				s->msgfp = NULL;
				io_xprintf(s->io,
				    "\r\n--%16" PRIu64 "/%s--\r\n", s->boundary,
				    s->smtpname);
				bounce_send(s, ".");
				s->state = BOUNCE_DATA_END;
				return (0);
			}
			line[len - 1] = '\0';
			io_xprintf(s->io, "%s%s\r\n",
			    (len == 2 && line[0] == '.') ? "." : "", line);
		}
		free(line);

		if (ferror(s->msgfp)) {
			fclose(s->msgfp);
			s->msgfp = NULL;
			bounce_delivery(s->msg, IMSG_QUEUE_DELIVERY_TEMPFAIL,
			    "Error reading message");
			s->msg = NULL;
			return (-1);
		}

		io_xprintf(s->io,
		    "\r\n--%16" PRIu64 "/%s--\r\n", s->boundary, s->smtpname);

		log_trace(TRACE_BOUNCE, "bounce: %p: >>> [... %zu bytes ...]",
		    s, io_queued(s->io) - n);

		if (feof(s->msgfp)) {
			fclose(s->msgfp);
			s->msgfp = NULL;
			bounce_send(s, ".");
			s->state = BOUNCE_DATA_END;
		}
		break;

	case BOUNCE_QUIT:
		bounce_send(s, "QUIT");
		s->state = BOUNCE_CLOSE;
		break;

	default:
		fatalx("bounce: bad state");
	}

	return (0);
}


static void
bounce_delivery(struct bounce_message *msg, int delivery, const char *status)
{
	struct bounce_envelope	*be;
	struct envelope		 evp;
	size_t			 n;
	const char		*f;

	n = 0;
	while ((be = TAILQ_FIRST(&msg->envelopes))) {
		if (delivery == IMSG_QUEUE_DELIVERY_TEMPFAIL) {
			if (queue_envelope_load(be->id, &evp) == 0) {
				fatalx("could not reload envelope!");
			}
			evp.retry++;
			evp.lasttry = msg->timeout;
			envelope_set_errormsg(&evp, "%s", status);
			queue_envelope_update(&evp);
			m_create(p_scheduler, delivery, 0, 0, -1);
			m_add_envelope(p_scheduler, &evp);
			m_close(p_scheduler);
		} else {
			m_create(p_scheduler, delivery, 0, 0, -1);
			m_add_evpid(p_scheduler, be->id);
			m_close(p_scheduler);
			queue_envelope_delete(be->id);
		}
		TAILQ_REMOVE(&msg->envelopes, be, entry);
		free(be->report);
		free(be);
		n += 1;
	}


	if (delivery == IMSG_QUEUE_DELIVERY_TEMPFAIL)
		f = "TempFail";
	else if (delivery == IMSG_QUEUE_DELIVERY_PERMFAIL)
		f = "PermFail";
	else
		f = NULL;

	if (f)
		log_warnx("warn: %s injecting failure report on message %08"
		    PRIx32 " to <%s> for %zu envelope%s: %s",
		    f, msg->msgid, msg->to, n, n > 1 ? "s":"", status);

	nmessage -= 1;
	stat_decrement("bounce.message", 1);
	stat_decrement("bounce.envelope", n);
	free(msg->smtpname);
	free(msg->to);
	free(msg);
}

static void
bounce_status(struct bounce_session *s, const char *fmt, ...)
{
	va_list		 ap;
	char		*status;
	int		 len, delivery;

	/* Ignore if there is no message */
	if (s->msg == NULL)
		return;

	va_start(ap, fmt);
	if ((len = vasprintf(&status, fmt, ap)) == -1)
		fatal("bounce: vasprintf");
	va_end(ap);

	if (*status == '2')
		delivery = IMSG_QUEUE_DELIVERY_OK;
	else if (*status == '5' || *status == '6')
		delivery = IMSG_QUEUE_DELIVERY_PERMFAIL;
	else
		delivery = IMSG_QUEUE_DELIVERY_TEMPFAIL;

	bounce_delivery(s->msg, delivery, status);
	s->msg = NULL;
	if (s->msgfp)
		fclose(s->msgfp);

	free(status);
}

static void
bounce_free(struct bounce_session *s)
{
	log_debug("debug: bounce: %p: deleting session", s);

	io_free(s->io);

	free(s->smtpname);
	free(s);

	running -= 1;
	stat_decrement("bounce.session", 1);
	bounce_drain();
}

static void
bounce_io(struct io *io, int evt, void *arg)
{
	struct bounce_session	*s = arg;
	const char		*error;
	char			*line, *msg;
	int			 cont;
	size_t			 len;

	log_trace(TRACE_IO, "bounce: %p: %s %s", s, io_strevent(evt),
	    io_strio(io));

	switch (evt) {
	case IO_DATAIN:
	    nextline:
		line = io_getline(s->io, &len);
		if (line == NULL && io_datalen(s->io) >= LINE_MAX) {
			bounce_status(s, "Input too long");
			bounce_free(s);
			return;
		}

		if (line == NULL)
			break;

		/* Strip trailing '\r' */
		if (len && line[len - 1] == '\r')
			line[--len] = '\0';

		log_trace(TRACE_BOUNCE, "bounce: %p: <<< %s", s, line);

		if ((error = parse_smtp_response(line, len, &msg, &cont))) {
			bounce_status(s, "Bad response: %s", error);
			bounce_free(s);
			return;
		}
		if (cont)
			goto nextline;

		if (s->state == BOUNCE_CLOSE) {
			bounce_free(s);
			return;
		}

		if (line[0] != '2' && line[0] != '3') {		/* fail */
			bounce_status(s, "%s", line);
			s->state = BOUNCE_QUIT;
		} else if (s->state == BOUNCE_DATA_END) {	/* accepted */
			bounce_status(s, "%s", line);
		}

		if (bounce_next(s) == -1) {
			bounce_free(s);
			return;
		}

		io_set_write(io);
		break;

	case IO_LOWAT:
		if (s->state == BOUNCE_DATA_MESSAGE)
			if (bounce_next(s) == -1) {
				bounce_free(s);
				return;
			}
		if (io_queued(s->io) == 0)
			io_set_read(io);
		break;

	default:
		bounce_status(s, "442 i/o error %d", evt);
		bounce_free(s);
		break;
	}
}

static int
bounce_message_cmp(const struct bounce_message *a,
    const struct bounce_message *b)
{
	int r;

	if (a->msgid < b->msgid)
		return (-1);
	if (a->msgid > b->msgid)
		return (1);
	if ((r = strcmp(a->smtpname, b->smtpname)))
		return (r);

	return memcmp(&a->bounce, &b->bounce, sizeof (a->bounce));
}

static const char *
action_str(const struct delivery_bounce *b)
{
	switch (b->type) {
	case B_FAILED:
		return ("failed");
	case B_DELAYED:
		return ("delayed");
	case B_DELIVERED:
		if (b->mta_without_dsn)
			return ("relayed");

		return ("delivered");
	default:
		log_warn("warn: bounce: unknown bounce_type");
		return ("");
	}
}

SPLAY_GENERATE(bounce_message_tree, bounce_message, sp_entry,
    bounce_message_cmp);
