/*	$OpenBSD: agentx_internal.h,v 1.3 2021/10/23 14:39:35 martijn Exp $ */
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
#include <sys/queue.h>
#include <sys/time.h>
#include <sys/tree.h>

#include "ax.h"

enum agentx_cstate {		/* Current state */
	AX_CSTATE_CLOSE,	/* Closed */
	AX_CSTATE_WAITOPEN,	/* Connection requested */
	AX_CSTATE_OPEN,		/* Open */
	AX_CSTATE_WAITCLOSE	/* Close requested */
};

enum agentx_dstate {		/* Desired state */
	AX_DSTATE_OPEN,		/* Open */
	AX_DSTATE_CLOSE		/* Close/free */
};

struct agentx {
	void (*ax_nofd)(struct agentx *, void *, int);
	void *ax_cookie;
	int ax_fd;
	enum agentx_cstate ax_cstate;
	enum agentx_dstate ax_dstate;
	int ax_free;		/* Freeing already planned */
	struct ax *ax_ax;
	TAILQ_HEAD(, agentx_session) ax_sessions;
	TAILQ_HEAD(, agentx_get) ax_getreqs;
	RB_HEAD(ax_requests, agentx_request) ax_requests;
};

struct agentx_session {
	struct agentx *axs_ax;
	uint32_t axs_id;
	uint32_t axs_timeout;
	struct ax_oid axs_oid;
	struct ax_ostring axs_descr;
	enum agentx_cstate axs_cstate;
	enum agentx_dstate axs_dstate;
	uint32_t axs_packetid;
	TAILQ_HEAD(, agentx_context) axs_contexts;
	TAILQ_ENTRY(agentx_session) axs_ax_sessions;
};

struct agentx_context {
	struct agentx_session *axc_axs;
	int axc_name_default;
	struct ax_ostring axc_name;
	uint32_t axc_sysuptime;
	struct timespec axc_sysuptimespec;
	enum agentx_cstate axc_cstate;
	enum agentx_dstate axc_dstate;
	TAILQ_HEAD(, agentx_agentcaps) axc_agentcaps;
	TAILQ_HEAD(, agentx_region) axc_regions;
	RB_HEAD(axc_objects, agentx_object) axc_objects;
	TAILQ_ENTRY(agentx_context) axc_axs_contexts;
};

struct agentx_get {
	struct agentx_context *axg_axc;
	int axg_fd;				/* Only used for logging */
	uint32_t axg_sessionid;
	uint32_t axg_transactionid;
	uint32_t axg_packetid;
	int axg_context_default;
	struct ax_ostring axg_context;
	enum ax_pdu_type axg_type;
	uint16_t axg_nonrep;
	uint16_t axg_maxrep;
	size_t axg_nvarbind;
	struct agentx_varbind *axg_varbind;
	TAILQ_ENTRY(agentx_get) axg_ax_getreqs;
};

__dead void agentx_log_ax_fatalx(struct agentx *, const char *, ...)
    __attribute__((__format__ (printf, 2, 3)));
void agentx_log_ax_warn(struct agentx *, const char *, ...)
    __attribute__((__format__ (printf, 2, 3)));
void agentx_log_ax_warnx(struct agentx *, const char *, ...)
    __attribute__((__format__ (printf, 2, 3)));
void agentx_log_ax_info(struct agentx *, const char *, ...)
    __attribute__((__format__ (printf, 2, 3)));
void agentx_log_ax_debug(struct agentx *, const char *, ...)
    __attribute__((__format__ (printf, 2, 3)));
__dead void agentx_log_axs_fatalx(struct agentx_session *, const char *,
    ...) __attribute__((__format__ (printf, 2, 3)));
void agentx_log_axs_warnx(struct agentx_session *, const char *, ...)
    __attribute__((__format__ (printf, 2, 3)));
void agentx_log_axs_warn(struct agentx_session *, const char *, ...)
    __attribute__((__format__ (printf, 2, 3)));
void agentx_log_axs_info(struct agentx_session *, const char *, ...)
    __attribute__((__format__ (printf, 2, 3)));
__dead void agentx_log_axc_fatalx(struct agentx_context *, const char *,
    ...) __attribute__((__format__ (printf, 2, 3)));
void agentx_log_axc_warnx(struct agentx_context *, const char *, ...)
    __attribute__((__format__ (printf, 2, 3)));
void agentx_log_axc_warn(struct agentx_context *, const char *, ...)
    __attribute__((__format__ (printf, 2, 3)));
void agentx_log_axc_info(struct agentx_context *, const char *, ...)
    __attribute__((__format__ (printf, 2, 3)));
void agentx_log_axc_debug(struct agentx_context *, const char *, ...)
    __attribute__((__format__ (printf, 2, 3)));
__dead void agentx_log_axg_fatalx(struct agentx_get *, const char *,
    ...) __attribute__((__format__ (printf, 2, 3)));
void agentx_log_axg_warnx(struct agentx_get *, const char *, ...)
    __attribute__((__format__ (printf, 2, 3)));
void agentx_log_axg_warn(struct agentx_get *, const char *, ...)
    __attribute__((__format__ (printf, 2, 3)));
void agentx_log_axg_debug(struct agentx_get *, const char *, ...)
    __attribute__((__format__ (printf, 2, 3)));
