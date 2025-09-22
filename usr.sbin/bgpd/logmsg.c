/*	$OpenBSD: logmsg.c,v 1.16 2025/09/09 12:42:04 claudio Exp $ */

/*
 * Copyright (c) 2003, 2004 Henning Brauer <henning@openbsd.org>
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
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <syslog.h>

#include "bgpd.h"
#include "session.h"
#include "log.h"

char *
log_fmt_peer(const struct peer_config *peer)
{
	const char	*ip;
	char		*pfmt, *p;

	ip = log_addr(&peer->remote_addr);
	if ((peer->remote_addr.aid == AID_INET && peer->remote_masklen != 32) ||
	    (peer->remote_addr.aid == AID_INET6 &&
	    peer->remote_masklen != 128)) {
		if (asprintf(&p, "%s/%u", ip, peer->remote_masklen) == -1)
			fatal(NULL);
	} else {
		if ((p = strdup(ip)) == NULL)
			fatal(NULL);
	}

	if (peer->descr[0]) {
		if (asprintf(&pfmt, "neighbor %s (%s)", p, peer->descr) ==
		    -1)
			fatal(NULL);
	} else {
		if (asprintf(&pfmt, "neighbor %s", p) == -1)
			fatal(NULL);
	}
	free(p);
	return (pfmt);
}

void
log_peer_info(const struct peer_config *peer, const char *emsg, ...)
{
	char	*p, *msg;
	va_list	 ap;

	p = log_fmt_peer(peer);
	va_start(ap, emsg);
	if (vasprintf(&msg, emsg, ap) == -1)
		fatal(NULL);
	va_end(ap);
	logit(LOG_INFO, "%s: %s", p, msg);
	free(msg);
	free(p);
}

void
log_peer_warn(const struct peer_config *peer, const char *emsg, ...)
{
	char	*p, *msg;
	va_list	 ap;
	int	 saved_errno = errno;

	p = log_fmt_peer(peer);
	if (emsg == NULL) {
		logit(LOG_ERR, "%s: %s", p, strerror(saved_errno));
	} else {
		va_start(ap, emsg);
		if (vasprintf(&msg, emsg, ap) == -1)
			fatal(NULL);
		va_end(ap);
		logit(LOG_ERR, "%s: %s: %s", p, msg, strerror(saved_errno));
		free(msg);
	}
	free(p);
}

void
log_peer_warnx(const struct peer_config *peer, const char *emsg, ...)
{
	char	*p, *msg;
	va_list	 ap;

	p = log_fmt_peer(peer);
	va_start(ap, emsg);
	if (vasprintf(&msg, emsg, ap) == -1)
		fatal(NULL);
	va_end(ap);
	logit(LOG_ERR, "%s: %s", p, msg);
	free(msg);
	free(p);
}

void
log_statechange(struct peer *peer, enum session_state ostate,
    enum session_events event)
{
	/* don't clutter the logs with constant Connect -> Active -> Connect */
	if (peer->state == STATE_CONNECT && peer->prev_state == STATE_ACTIVE &&
	    ostate == STATE_CONNECT)
		return;
	if (peer->state == STATE_ACTIVE && peer->prev_state == STATE_CONNECT &&
	    ostate == STATE_ACTIVE)
		return;

	peer->lasterr = 0;
	log_peer_info(&peer->conf, "state change %s -> %s, reason: %s",
	    statenames[peer->prev_state], statenames[peer->state],
	    eventnames[event]);
}

static const char *
tohex(const unsigned char *in, size_t len)
{
	const char hex[] = "0123456789ABCDEF";
	static char out[(16 + 1) * 3];
	size_t i, o = 0;

	if (len > 16)
		len = 16;
	for (i = 0; i < len; i++) {
		out[o++] = hex[in[i] >> 4];
		out[o++] = hex[in[i] & 0xf];
		out[o++] = ' ';
		if (i == 7)
			out[o++] = ' ';
	}
	out[o - 1] = '\0';

	return out;
}

void
log_notification(const struct peer *peer, uint8_t errcode, uint8_t subcode,
    const struct ibuf *data, const char *dir)
{
	struct ibuf	 ibuf;
	char		*p;
	const char	*suberrname = NULL;
	int		 uk = 0, dump = 0;

	if (data != NULL)
		ibuf_from_ibuf(&ibuf, data);
	else
		ibuf_from_buffer(&ibuf, NULL, 0);

	p = log_fmt_peer(&peer->conf);
	switch (errcode) {
	case ERR_HEADER:
		if (subcode >= sizeof(suberr_header_names) / sizeof(char *) ||
		    suberr_header_names[subcode] == NULL)
			uk = 1;
		else
			suberrname = suberr_header_names[subcode];
		break;
	case ERR_OPEN:
		if (subcode >= sizeof(suberr_open_names) / sizeof(char *) ||
		    suberr_open_names[subcode] == NULL)
			uk = 1;
		else
			suberrname = suberr_open_names[subcode];
		if (errcode == ERR_OPEN && subcode == ERR_OPEN_CAPA) {
			uint8_t capa_code;

			if (ibuf_get_n8(&ibuf, &capa_code) == -1)
				break;

			logit(LOG_ERR, "%s: %s notification: %s, %s: %s",
			    p, dir, errnames[errcode], suberrname,
			    log_capability(capa_code));
			free(p);
			return;
		}
		break;
	case ERR_UPDATE:
		if (subcode >= sizeof(suberr_update_names) / sizeof(char *) ||
		    suberr_update_names[subcode] == NULL)
			uk = 1;
		else
			suberrname = suberr_update_names[subcode];
		dump = 1;
		break;
	case ERR_CEASE:
		if (subcode >= sizeof(suberr_cease_names) / sizeof(char *) ||
		    suberr_cease_names[subcode] == NULL)
			uk = 1;
		else
			suberrname = suberr_cease_names[subcode];

		if (subcode == ERR_CEASE_ADMIN_DOWN ||
		    subcode == ERR_CEASE_ADMIN_RESET) {
			uint8_t len;
			/* check if shutdown reason is included */
			if (ibuf_get_n8(&ibuf, &len) != -1 && len != 0) {
				char *s;
				if ((s = ibuf_get_string(&ibuf, len)) != NULL) {
					logit(LOG_ERR, "%s: %s notification: "
					    "%s, %s: reason \"%s\"", p, dir,
					    errnames[errcode], suberrname,
					    log_reason(s));
					free(s);
					free(p);
					return;
				}
			}
		}
		break;
	case ERR_HOLDTIMEREXPIRED:
		if (subcode != 0)
			uk = 1;
		break;
	case ERR_FSM:
		if (subcode >= sizeof(suberr_fsm_names) / sizeof(char *) ||
		    suberr_fsm_names[subcode] == NULL)
			uk = 1;
		else
			suberrname = suberr_fsm_names[subcode];
		break;
	case ERR_RREFRESH:
		if (subcode >= sizeof(suberr_rrefresh_names) / sizeof(char *) ||
		    suberr_rrefresh_names[subcode] == NULL)
			uk = 1;
		else
			suberrname = suberr_rrefresh_names[subcode];
		break;
	default:
		logit(LOG_ERR, "%s: %s notification, unknown errcode "
		    "%u, subcode %u", p, dir, errcode, subcode);
		free(p);
		return;
	}

	if (uk)
		logit(LOG_ERR, "%s: %s notification: %s, unknown subcode %u",
		    p, dir, errnames[errcode], subcode);
	else {
		if (suberrname == NULL)
			logit(LOG_ERR, "%s: %s notification: %s", p,
			    dir, errnames[errcode]);
		else
			logit(LOG_ERR, "%s: %s notification: %s, %s",
			    p, dir, errnames[errcode], suberrname);
	}

	if (dump && log_getverbose() && ibuf_size(&ibuf) > 0) {
		size_t off = 0;
		logit(LOG_INFO, "%s: notification data", p);
		while (ibuf_size(&ibuf) > 0) {
			unsigned char buf[16];
			size_t len = sizeof(buf);
			if (ibuf_size(&ibuf) < len)
				len = ibuf_size(&ibuf);
			if (ibuf_get(&ibuf, buf, len) == -1) {
				break;
			}
			logit(LOG_INFO, "   %5zu: %s", off, tohex(buf, len));
			off += len;
		}
	}

	free(p);
}

void
log_conn_attempt(const struct peer *peer, struct sockaddr *sa, socklen_t len)
{
	char		*p;

	if (peer == NULL) {	/* connection from non-peer, drop */
		if (log_getverbose())
			logit(LOG_INFO, "connection from non-peer %s refused",
			    log_sockaddr(sa, len));
	} else {
		/* only log if there is a chance that the session may come up */
		if (peer->conf.down && peer->state == STATE_IDLE)
			return;
		p = log_fmt_peer(&peer->conf);
		logit(LOG_INFO, "Connection attempt from %s while session is "
		    "in state %s", p, statenames[peer->state]);
		free(p);
	}
}
