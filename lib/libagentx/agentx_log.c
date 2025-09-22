/*	$OpenBSD: agentx_log.c,v 1.2 2020/10/26 16:02:16 tb Exp $ */
/*
 * Copyright (c) 2020 Martijn van Duren <martijn@openbsd.org>
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

#include "agentx_internal.h"

#define AGENTX_CONTEXT_NAME(axc) (axc->axc_name_default ? "<default>" : \
    (char *)axc->axc_name.aos_string)
#define AGENTX_GET_CTXNAME(axg) (axg->axg_context_default ? "<default>" : \
    (char *)axg->axg_context.aos_string)

enum agentx_log_type {
	AGENTX_LOG_TYPE_FATAL,
	AGENTX_LOG_TYPE_WARN,
	AGENTX_LOG_TYPE_INFO,
	AGENTX_LOG_TYPE_DEBUG
};

void (*agentx_log_fatal)(const char *, ...)
    __attribute__((__format__ (printf, 1, 2))) = NULL;
void (*agentx_log_warn)(const char *, ...)
    __attribute__((__format__ (printf, 1, 2))) = NULL;
void (*agentx_log_info)(const char *, ...)
    __attribute__((__format__ (printf, 1, 2))) = NULL;
void (*agentx_log_debug)(const char *, ...)
    __attribute__((__format__ (printf, 1, 2))) = NULL;


static void
agentx_log_do(enum agentx_log_type, const char *, va_list, int,
    struct agentx *, struct agentx_session *, struct agentx_context *,
    struct agentx_get *);

void
agentx_log_ax_fatalx(struct agentx *ax, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	agentx_log_do(AGENTX_LOG_TYPE_FATAL, fmt, ap, 0, ax, NULL, NULL,
	    NULL);
	va_end(ap);
	abort();
}

void
agentx_log_ax_warn(struct agentx *ax, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	agentx_log_do(AGENTX_LOG_TYPE_WARN, fmt, ap, 1, ax, NULL, NULL,
	    NULL);
	va_end(ap);
}

void
agentx_log_ax_warnx(struct agentx *ax, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	agentx_log_do(AGENTX_LOG_TYPE_WARN, fmt, ap, 0, ax, NULL, NULL,
	    NULL);
	va_end(ap);
}

void
agentx_log_ax_info(struct agentx *ax, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	agentx_log_do(AGENTX_LOG_TYPE_INFO, fmt, ap, 0, ax, NULL, NULL,
	    NULL);
	va_end(ap);
}

void
agentx_log_ax_debug(struct agentx *ax, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	agentx_log_do(AGENTX_LOG_TYPE_DEBUG, fmt, ap, 0, ax, NULL, NULL,
	    NULL);
	va_end(ap);
}

void
agentx_log_axs_fatalx(struct agentx_session *axs, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	agentx_log_do(AGENTX_LOG_TYPE_FATAL, fmt, ap, 0, NULL, axs, NULL,
	    NULL);
	va_end(ap);
	abort();
}

void
agentx_log_axs_warnx(struct agentx_session *axs, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	agentx_log_do(AGENTX_LOG_TYPE_WARN, fmt, ap, 0, NULL, axs, NULL,
	    NULL);
	va_end(ap);
}

void
agentx_log_axs_warn(struct agentx_session *axs, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	agentx_log_do(AGENTX_LOG_TYPE_WARN, fmt, ap, 1, NULL, axs, NULL,
	    NULL);
	va_end(ap);
}

void
agentx_log_axs_info(struct agentx_session *axs, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	agentx_log_do(AGENTX_LOG_TYPE_INFO, fmt, ap, 0, NULL, axs, NULL,
	    NULL);
	va_end(ap);
}

void
agentx_log_axc_fatalx(struct agentx_context *axc, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	agentx_log_do(AGENTX_LOG_TYPE_FATAL, fmt, ap, 0, NULL, NULL, axc,
	    NULL);
	va_end(ap);
	abort();
}

void
agentx_log_axc_warnx(struct agentx_context *axc, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	agentx_log_do(AGENTX_LOG_TYPE_WARN, fmt, ap, 0, NULL, NULL, axc,
	    NULL);
	va_end(ap);
}

void
agentx_log_axc_warn(struct agentx_context *axc, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	agentx_log_do(AGENTX_LOG_TYPE_WARN, fmt, ap, 1, NULL, NULL, axc,
	    NULL);
	va_end(ap);
}

void
agentx_log_axc_info(struct agentx_context *axc, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	agentx_log_do(AGENTX_LOG_TYPE_INFO, fmt, ap, 0, NULL, NULL, axc,
	    NULL);
	va_end(ap);
}

void
agentx_log_axc_debug(struct agentx_context *axc, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	agentx_log_do(AGENTX_LOG_TYPE_DEBUG, fmt, ap, 0, NULL, NULL, axc,
	    NULL);
	va_end(ap);
}

void
agentx_log_axg_fatalx(struct agentx_get *axg, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	agentx_log_do(AGENTX_LOG_TYPE_FATAL, fmt, ap, 0, NULL, NULL, NULL,
	    axg);
	va_end(ap);
	abort();
}

void
agentx_log_axg_warnx(struct agentx_get *axg, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	agentx_log_do(AGENTX_LOG_TYPE_WARN, fmt, ap, 0, NULL, NULL, NULL,
	    axg);
	va_end(ap);
}

void
agentx_log_axg_warn(struct agentx_get *axg, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	agentx_log_do(AGENTX_LOG_TYPE_WARN, fmt, ap, 1, NULL, NULL, NULL,
	    axg);
	va_end(ap);
}

void
agentx_log_axg_debug(struct agentx_get *axg, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	agentx_log_do(AGENTX_LOG_TYPE_DEBUG, fmt, ap, 0, NULL, NULL, NULL,
	    axg);
	va_end(ap);
}

static void
agentx_log_do(enum agentx_log_type type, const char *fmt, va_list ap,
    int useerrno, struct agentx *ax, struct agentx_session *axs,
    struct agentx_context *axc, struct agentx_get *axg)
{
	void (*agentx_log)(const char *, ...);
	char buf[1500];

	if (type == AGENTX_LOG_TYPE_FATAL)
		agentx_log = agentx_log_fatal;
	else if (type == AGENTX_LOG_TYPE_WARN)
		agentx_log = agentx_log_warn;
	else if (type == AGENTX_LOG_TYPE_INFO)
		agentx_log = agentx_log_info;
	else
		agentx_log = agentx_log_debug;
	if (agentx_log == NULL)
		return;

	vsnprintf(buf, sizeof(buf), fmt, ap);

	if (axg != NULL) {
		if (useerrno)
			agentx_log("[fd:%d sess:%u ctx:%s trid:%u pid:%u]: "
			    "%s: %s", axg->axg_fd, axg->axg_sessionid,
			    AGENTX_GET_CTXNAME(axg), axg->axg_transactionid,
			    axg->axg_packetid, buf, strerror(errno));
		else
			agentx_log("[fd:%d sess:%u ctx:%s trid:%u pid:%u]: "
			    "%s", axg->axg_fd, axg->axg_sessionid,
			    AGENTX_GET_CTXNAME(axg), axg->axg_transactionid,
			    axg->axg_packetid, buf);
	} else if (axc != NULL) {
		axs = axc->axc_axs;
		ax = axs->axs_ax;
		if (useerrno)
			agentx_log("[fd:%d sess:%u ctx:%s]: %s: %s",
			    ax->ax_fd, axs->axs_id, AGENTX_CONTEXT_NAME(axc),
			    buf, strerror(errno));
		else
			agentx_log("[fd:%d sess:%u ctx:%s]: %s", ax->ax_fd,
			    axs->axs_id, AGENTX_CONTEXT_NAME(axc), buf);
	} else if (axs != NULL) {
		ax = axs->axs_ax;
		if (useerrno)
			agentx_log("[fd:%d sess:%u]: %s: %s", ax->ax_fd,
			    axs->axs_id, buf, strerror(errno));
		else
			agentx_log("[fd:%d sess:%u]: %s", ax->ax_fd,
			    axs->axs_id, buf);
	} else if (ax->ax_fd == -1) {
		if (useerrno)
			agentx_log("%s: %s", buf, strerror(errno));
		else
			agentx_log("%s", buf);
	} else {
		if (useerrno)
			agentx_log("[fd:%d]: %s: %s", ax->ax_fd, buf,
			    strerror(errno));
		else
			agentx_log("[fd:%d]: %s", ax->ax_fd, buf);
	}
}
