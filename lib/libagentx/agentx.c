/*	$OpenBSD: agentx.c,v 1.25 2025/09/08 08:43:39 jsg Exp $ */
/*
 * Copyright (c) 2019 Martijn van Duren <martijn@openbsd.org>
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
#include <netinet/in.h>

#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

#include "agentx_internal.h"
#include <agentx.h>

/*
 * ax:		struct agentx
 * axs:		struct agentx_session
 * axc:		struct agentx_context
 * axr:		struct agentx_region
 * axi:		struct agentx_index
 * axo:		struct agentx_object
 * axg:		struct agentx_get
 * axv:		struct agentx_varbind
 * axr:		struct agentx_request
 * cstate:	current state
 * dstate:	desired state
 */

enum agentx_index_type {
	AXI_TYPE_NEW,
	AXI_TYPE_ANY,
	AXI_TYPE_VALUE,
	AXI_TYPE_DYNAMIC
};

#define AGENTX_CONTEXT_CTX(axc) (axc->axc_name_default ? NULL : \
    &(axc->axc_name))

struct agentx_agentcaps {
	struct agentx_context *axa_axc;
	struct ax_oid axa_oid;
	struct ax_ostring axa_descr;
	enum agentx_cstate axa_cstate;
	enum agentx_dstate axa_dstate;
	TAILQ_ENTRY(agentx_agentcaps) axa_axc_agentcaps;
};

struct agentx_region {
	struct agentx_context *axr_axc;
	struct ax_oid axr_oid;
	uint8_t axr_timeout;
	uint8_t axr_priority;
	enum agentx_cstate axr_cstate;
	enum agentx_dstate axr_dstate;
	TAILQ_HEAD(, agentx_index) axr_indices;
	TAILQ_HEAD(, agentx_object) axr_objects;
	TAILQ_ENTRY(agentx_region) axr_axc_regions;
};

struct agentx_index {
	struct agentx_region *axi_axr;
	enum agentx_index_type axi_type;
	struct ax_varbind axi_vb;
	struct agentx_object **axi_object;
	size_t axi_objectlen;
	size_t axi_objectsize;
	enum agentx_cstate axi_cstate;
	enum agentx_dstate axi_dstate;
	TAILQ_ENTRY(agentx_index) axi_axr_indices;
};

struct agentx_object {
	struct agentx_region *axo_axr;
	struct ax_oid axo_oid;
	struct agentx_index *axo_index[AGENTX_OID_INDEX_MAX_LEN];
	size_t axo_indexlen;
	int axo_implied;
	uint8_t axo_timeout;
	/* Prevent freeing object while in use by get and set requesets */
	uint32_t axo_lock;
	void (*axo_get)(struct agentx_varbind *);
	enum agentx_cstate axo_cstate;
	enum agentx_dstate axo_dstate;
	RB_ENTRY(agentx_object) axo_axc_objects;
	TAILQ_ENTRY(agentx_object) axo_axr_objects;
};

struct agentx_varbind {
	struct agentx_get *axv_axg;
	struct agentx_object *axv_axo;
	struct agentx_varbind_index {
		struct agentx_index *axv_axi;
		union ax_data axv_idata;
	} axv_index[AGENTX_OID_INDEX_MAX_LEN];
	size_t axv_indexlen;
	int axv_initialized;
	int axv_include;
	struct ax_varbind axv_vb;
	struct ax_oid axv_start;
	struct ax_oid axv_end;
	enum ax_pdu_error axv_error;
};

#define AGENTX_GET_CTX(axg) (axg->axg_context_default ? NULL : \
    &(axg->axg_context))
struct agentx_request {
	uint32_t axr_packetid;
	int (*axr_cb)(struct ax_pdu *, void *);
	void *axr_cookie;
	RB_ENTRY(agentx_request) axr_ax_requests;
};

static void agentx_start(struct agentx *);
static void agentx_finalize(struct agentx *, int);
static void agentx_wantwritenow(struct agentx *, int);
void (*agentx_wantwrite)(struct agentx *, int) =
    agentx_wantwritenow;
static void agentx_reset(struct agentx *);
static void agentx_free_finalize(struct agentx *);
static int agentx_session_retry(struct agentx_session *);
static int agentx_session_start(struct agentx_session *);
static int agentx_session_finalize(struct ax_pdu *, void *);
static int agentx_session_close(struct agentx_session *,
    enum ax_close_reason);
static int agentx_session_close_finalize(struct ax_pdu *, void *);
static void agentx_session_free_finalize(struct agentx_session *);
static void agentx_session_reset(struct agentx_session *);
static int agentx_context_retry(struct agentx_context *);
static void agentx_context_start(struct agentx_context *);
static void agentx_context_free_finalize(struct agentx_context *);
static void agentx_context_reset(struct agentx_context *);
static int agentx_agentcaps_start(struct agentx_agentcaps *);
static int agentx_agentcaps_finalize(struct ax_pdu *, void *);
static int agentx_agentcaps_close(struct agentx_agentcaps *);
static int agentx_agentcaps_close_finalize(struct ax_pdu *, void *);
static void agentx_agentcaps_free_finalize(struct agentx_agentcaps *);
static void agentx_agentcaps_reset(struct agentx_agentcaps *);
static int agentx_region_retry(struct agentx_region *);
static int agentx_region_start(struct agentx_region *);
static int agentx_region_finalize(struct ax_pdu *, void *);
static int agentx_region_close(struct agentx_region *);
static int agentx_region_close_finalize(struct ax_pdu *, void *);
static void agentx_region_free_finalize(struct agentx_region *);
static void agentx_region_reset(struct agentx_region *);
static struct agentx_index *agentx_index(struct agentx_region *,
    struct ax_varbind *, enum agentx_index_type);
static int agentx_index_start(struct agentx_index *);
static int agentx_index_finalize(struct ax_pdu *, void *);
static void agentx_index_free_finalize(struct agentx_index *);
static void agentx_index_reset(struct agentx_index *);
static int agentx_index_close(struct agentx_index *);
static int agentx_index_close_finalize(struct ax_pdu *, void *);
static int agentx_object_start(struct agentx_object *);
static int agentx_object_finalize(struct ax_pdu *, void *);
static int agentx_object_lock(struct agentx_object *);
static void agentx_object_unlock(struct agentx_object *);
static int agentx_object_close(struct agentx_object *);
static int agentx_object_close_finalize(struct ax_pdu *, void *);
static void agentx_object_free_finalize(struct agentx_object *);
static void agentx_object_reset(struct agentx_object *);
static int agentx_object_cmp(struct agentx_object *,
    struct agentx_object *);
static void agentx_get_start(struct agentx_context *,
    struct ax_pdu *);
static void agentx_get_finalize(struct agentx_get *);
static void agentx_get_free(struct agentx_get *);
static void agentx_varbind_start(struct agentx_varbind *);
static void agentx_varbind_finalize(struct agentx_varbind *);
static void agentx_varbind_nosuchobject(struct agentx_varbind *);
static void agentx_varbind_nosuchinstance(struct agentx_varbind *);
static void agentx_varbind_endofmibview(struct agentx_varbind *);
static void agentx_varbind_error_type(struct agentx_varbind *,
    enum ax_pdu_error, int);
static int agentx_request(struct agentx *, uint32_t,
    int (*)(struct ax_pdu *, void *), void *);
static int agentx_request_cmp(struct agentx_request *,
    struct agentx_request *);
static int agentx_strcat(char **, const char *);
static int agentx_oidfill(struct ax_oid *, const uint32_t[], size_t,
    const char **);

RB_PROTOTYPE_STATIC(ax_requests, agentx_request, axr_ax_requests,
    agentx_request_cmp)
RB_PROTOTYPE_STATIC(axc_objects, agentx_object, axo_axc_objects,
    agentx_object_cmp)

struct agentx *
agentx(void (*nofd)(struct agentx *, void *, int), void *cookie)
{
	struct agentx *ax;

	if ((ax = calloc(1, sizeof(*ax))) == NULL)
		return NULL;

	ax->ax_nofd = nofd;
	ax->ax_cookie = cookie;
	ax->ax_fd = -1;
	ax->ax_cstate = AX_CSTATE_CLOSE;
	ax->ax_dstate = AX_DSTATE_OPEN;
	TAILQ_INIT(&(ax->ax_sessions));
	TAILQ_INIT(&(ax->ax_getreqs));
	RB_INIT(&(ax->ax_requests));

	agentx_start(ax);

	return ax;
}

/*
 * agentx_finalize is not a suitable name for a public API,
 * but use it internally for consistency
 */
void
agentx_connect(struct agentx *ax, int fd)
{
	agentx_finalize(ax, fd);
}

void
agentx_retry(struct agentx *ax)
{
	struct agentx_session *axs;

	if (ax->ax_fd == -1)
		return;

	TAILQ_FOREACH(axs, &(ax->ax_sessions), axs_ax_sessions) {
		if (axs->axs_cstate == AX_CSTATE_OPEN) {
			if (agentx_session_retry(axs) == -1)
				return;
		} else if (axs->axs_cstate == AX_CSTATE_CLOSE) {
			if (agentx_session_start(axs) == -1)
				return;
		}
	}
}

static void
agentx_start(struct agentx *ax)
{
#ifdef AX_DEBUG
	if (ax->ax_cstate != AX_CSTATE_CLOSE ||
	    ax->ax_dstate != AX_DSTATE_OPEN)
		agentx_log_ax_fatalx(ax, "%s: unexpected connect", __func__);
#endif
	ax->ax_cstate = AX_CSTATE_WAITOPEN;
	ax->ax_nofd(ax, ax->ax_cookie, 0);
}

static void
agentx_finalize(struct agentx *ax, int fd)
{
	struct agentx_session *axs;

	if (ax->ax_cstate != AX_CSTATE_WAITOPEN) {
#ifdef AX_DEBUG
		agentx_log_ax_fatalx(ax, "%s: agentx unexpected connect",
		    __func__);
#else
		agentx_log_ax_warnx(ax,
		    "%s: agentx unexpected connect: ignoring", __func__);
		return;
#endif
	}
	if ((ax->ax_ax = ax_new(fd)) == NULL) {
		agentx_log_ax_warn(ax, "failed to initialize");
		close(fd);
		agentx_reset(ax);
		return;
	}

	agentx_log_ax_info(ax, "new connection: %d", fd);

	ax->ax_fd = fd;
	ax->ax_cstate = AX_CSTATE_OPEN;

	TAILQ_FOREACH(axs, &(ax->ax_sessions), axs_ax_sessions) {
		if (agentx_session_start(axs) == -1)
			break;
	}
}

static void
agentx_wantwritenow(struct agentx *ax, int fd)
{
	agentx_write(ax);
}

static void
agentx_reset(struct agentx *ax)
{
	struct agentx_session *axs, *taxs;
	struct agentx_request *axr;
	struct agentx_get *axg;
	int axfree = ax->ax_free;

	ax_free(ax->ax_ax);
	ax->ax_ax = NULL;
	ax->ax_fd = -1;
	ax->ax_free = 1;

	ax->ax_cstate = AX_CSTATE_CLOSE;

	while ((axr = RB_MIN(ax_requests, &(ax->ax_requests))) != NULL) {
		RB_REMOVE(ax_requests, &(ax->ax_requests), axr);
		free(axr);
	}
	TAILQ_FOREACH_SAFE(axs, &(ax->ax_sessions), axs_ax_sessions, taxs)
		agentx_session_reset(axs);
	while (!TAILQ_EMPTY(&(ax->ax_getreqs))) {
		axg = TAILQ_FIRST(&(ax->ax_getreqs));
		axg->axg_axc = NULL;
		TAILQ_REMOVE(&(ax->ax_getreqs), axg, axg_ax_getreqs);
	}

	if (ax->ax_dstate == AX_DSTATE_OPEN)
		agentx_start(ax);

	if (!axfree)
		agentx_free_finalize(ax);
}

void
agentx_free(struct agentx *ax)
{
	struct agentx_session *axs, *taxs;
	int axfree;

	if (ax == NULL)
		return;

	axfree = ax->ax_free;
	ax->ax_free = 1;

	/* Malloc throws abort on invalid pointers as well */
	if (ax->ax_dstate == AX_DSTATE_CLOSE)
		agentx_log_ax_fatalx(ax, "%s: double free", __func__);
	ax->ax_dstate = AX_DSTATE_CLOSE;

	TAILQ_FOREACH_SAFE(axs, &(ax->ax_sessions), axs_ax_sessions, taxs) {
		if (axs->axs_dstate != AX_DSTATE_CLOSE)
			agentx_session_free(axs);
	}
	if (!axfree)
		agentx_free_finalize(ax);
}

static void
agentx_free_finalize(struct agentx *ax)
{
	struct agentx_session *axs, *taxs;

	ax->ax_free = 0;

	TAILQ_FOREACH_SAFE(axs, &(ax->ax_sessions), axs_ax_sessions, taxs)
		agentx_session_free_finalize(axs);

	if (!TAILQ_EMPTY(&(ax->ax_sessions)) ||
	    !RB_EMPTY(&(ax->ax_requests)) ||
	    ax->ax_dstate != AX_DSTATE_CLOSE)
		return;

	ax_free(ax->ax_ax);
	ax->ax_nofd(ax, ax->ax_cookie, 1);
	free(ax);
}

struct agentx_session *
agentx_session(struct agentx *ax, uint32_t oid[],
    size_t oidlen, const char *descr, uint8_t timeout)
{
	struct agentx_session *axs;
	const char *errstr;

	if ((axs = calloc(1, sizeof(*axs))) == NULL)
		return NULL;

	axs->axs_ax = ax;
	axs->axs_timeout = timeout;
	/* RFC 2741 section 6.2.1: may send a null Object Identifier */
	if (oidlen == 0)
		axs->axs_oid.aoi_idlen = oidlen;
	else {
		if (agentx_oidfill((&axs->axs_oid), oid, oidlen,
		    &errstr) == -1) {
#ifdef AX_DEBUG
			agentx_log_ax_fatalx(ax, "%s: %s", __func__, errstr);
#else
			return NULL;
#endif
		}
	}
	axs->axs_descr.aos_string = (unsigned char *)strdup(descr);
	if (axs->axs_descr.aos_string == NULL) {
		free(axs);
		return NULL;
	}
	axs->axs_descr.aos_slen = strlen(descr);
	axs->axs_cstate = AX_CSTATE_CLOSE;
	axs->axs_dstate = AX_DSTATE_OPEN;
	TAILQ_INIT(&(axs->axs_contexts));
	TAILQ_INSERT_HEAD(&(ax->ax_sessions), axs, axs_ax_sessions);

	if (ax->ax_cstate == AX_CSTATE_OPEN)
		(void) agentx_session_start(axs);

	return axs;
}

static int
agentx_session_retry(struct agentx_session *axs)
{
	struct agentx_context *axc;

#ifdef AX_DEBUG
	if (axs->axs_cstate != AX_CSTATE_OPEN)
		agentx_log_axs_fatalx(axs, "%s: unexpected retry", __func__);
#endif

	TAILQ_FOREACH(axc, &(axs->axs_contexts), axc_axs_contexts) {
		if (axc->axc_cstate == AX_CSTATE_OPEN) {
			if (agentx_context_retry(axc) == -1)
				return -1;
		} else if (axc->axc_cstate == AX_CSTATE_CLOSE)
			agentx_context_start(axc);
	}
	return 0;
}

static int
agentx_session_start(struct agentx_session *axs)
{
	struct agentx *ax = axs->axs_ax;
	uint32_t packetid;

#ifdef AX_DEBUG
	if (ax->ax_cstate != AX_CSTATE_OPEN ||
	    axs->axs_cstate != AX_CSTATE_CLOSE ||
	    axs->axs_dstate != AX_DSTATE_OPEN)
		agentx_log_ax_fatalx(ax, "%s: unexpected session open",
		    __func__);
#endif
	packetid = ax_open(ax->ax_ax, axs->axs_timeout, &(axs->axs_oid),
	    &(axs->axs_descr));
	if (packetid == 0) {
		agentx_log_ax_warn(ax, "couldn't generate %s",
		    ax_pdutype2string(AX_PDU_TYPE_OPEN));
		agentx_reset(ax);
		return -1;
	}
	axs->axs_packetid = packetid;
	agentx_log_ax_info(ax, "opening session");
	axs->axs_cstate = AX_CSTATE_WAITOPEN;
	return agentx_request(ax, packetid, agentx_session_finalize, axs);
}

static int
agentx_session_finalize(struct ax_pdu *pdu, void *cookie)
{
	struct agentx_session *axs = cookie;
	struct agentx *ax = axs->axs_ax;
	struct agentx_context *axc;

#ifdef AX_DEBUG
	if (axs->axs_cstate != AX_CSTATE_WAITOPEN)
		agentx_log_ax_fatalx(ax, "%s: not expecting new session",
		    __func__);
#endif

	if (pdu->ap_payload.ap_response.ap_error != AX_PDU_ERROR_NOERROR) {
		agentx_log_ax_warnx(ax, "failed to open session: %s",
		    ax_error2string(pdu->ap_payload.ap_response.ap_error));
		axs->axs_cstate = AX_CSTATE_CLOSE;
		return -1;
	}

	axs->axs_id = pdu->ap_header.aph_sessionid;
	axs->axs_cstate = AX_CSTATE_OPEN;

	if (axs->axs_dstate == AX_DSTATE_CLOSE) {
		agentx_session_close(axs, AX_CLOSE_SHUTDOWN);
		return 0;
	}

	agentx_log_axs_info(axs, "open");

	TAILQ_FOREACH(axc, &(axs->axs_contexts), axc_axs_contexts)
		agentx_context_start(axc);
	return 0;
}

static int
agentx_session_close(struct agentx_session *axs,
    enum ax_close_reason reason)
{
	struct agentx *ax = axs->axs_ax;
	uint32_t packetid;

#ifdef AX_DEBUG
	if (axs->axs_cstate != AX_CSTATE_OPEN)
		agentx_log_ax_fatalx(ax, "%s: unexpected session close",
		    __func__);
#endif
	if ((packetid = ax_close(ax->ax_ax, axs->axs_id, reason)) == 0) {
		agentx_log_axs_warn(axs, "couldn't generate %s",
		    ax_pdutype2string(AX_PDU_TYPE_CLOSE));
		agentx_reset(ax);
		return -1;
	}

	agentx_log_axs_info(axs, "closing session: %s",
	    ax_closereason2string(reason));

	axs->axs_cstate = AX_CSTATE_WAITCLOSE;
	return agentx_request(ax, packetid, agentx_session_close_finalize,
	    axs);
}

static int
agentx_session_close_finalize(struct ax_pdu *pdu, void *cookie)
{
	struct agentx_session *axs = cookie;
	struct agentx *ax = axs->axs_ax;
	struct agentx_context *axc, *taxc;
	int axfree = ax->ax_free;

#ifdef AX_DEBUG
	if (axs->axs_cstate != AX_CSTATE_WAITCLOSE)
		agentx_log_axs_fatalx(axs, "%s: not expecting session close",
		    __func__);
#endif

	if (pdu->ap_payload.ap_response.ap_error != AX_PDU_ERROR_NOERROR) {
		agentx_log_axs_warnx(axs, "failed to close session: %s",
		    ax_error2string(pdu->ap_payload.ap_response.ap_error));
		agentx_reset(ax);
		return -1;
	}

	axs->axs_cstate = AX_CSTATE_CLOSE;
	ax->ax_free = 1;

	agentx_log_axs_info(axs, "closed");

	TAILQ_FOREACH_SAFE(axc, &(axs->axs_contexts), axc_axs_contexts, taxc)
		agentx_context_reset(axc);

	if (ax->ax_cstate == AX_CSTATE_OPEN &&
	    axs->axs_dstate == AX_DSTATE_OPEN)
		agentx_session_start(axs);
	if (!axfree)
		agentx_free_finalize(ax);
		
	return 0;
}

void
agentx_session_free(struct agentx_session *axs)
{
	struct agentx_context *axc, *taxc;
	struct agentx *ax;
	int axfree;

	if (axs == NULL)
		return;

	ax = axs->axs_ax;
	axfree = ax->ax_free;
	ax->ax_free = 1;

	if (axs->axs_dstate == AX_DSTATE_CLOSE)
		agentx_log_axs_fatalx(axs, "%s: double free", __func__);

	axs->axs_dstate = AX_DSTATE_CLOSE;

	if (axs->axs_cstate == AX_CSTATE_OPEN)
		(void) agentx_session_close(axs, AX_CLOSE_SHUTDOWN);

	TAILQ_FOREACH_SAFE(axc, &(axs->axs_contexts), axc_axs_contexts, taxc) {
		if (axc->axc_dstate != AX_DSTATE_CLOSE)
			agentx_context_free(axc);
	}

	if (!axfree)
		agentx_free_finalize(ax);
}

static void
agentx_session_free_finalize(struct agentx_session *axs)
{
	struct agentx *ax = axs->axs_ax;
	struct agentx_context *axc, *taxc;

	TAILQ_FOREACH_SAFE(axc, &(axs->axs_contexts), axc_axs_contexts, taxc)
		agentx_context_free_finalize(axc);

	if (!TAILQ_EMPTY(&(axs->axs_contexts)) ||
	    axs->axs_cstate != AX_CSTATE_CLOSE ||
	    axs->axs_dstate != AX_DSTATE_CLOSE)
		return;

	TAILQ_REMOVE(&(ax->ax_sessions), axs, axs_ax_sessions);
	free(axs->axs_descr.aos_string);
	free(axs);
}

static void
agentx_session_reset(struct agentx_session *axs)
{
	struct agentx_context *axc, *taxc;
	struct agentx *ax = axs->axs_ax;
	int axfree = ax->ax_free;

	ax->ax_free = 1;

	axs->axs_cstate = AX_CSTATE_CLOSE;

	TAILQ_FOREACH_SAFE(axc, &(axs->axs_contexts), axc_axs_contexts, taxc)
		agentx_context_reset(axc);

	if (!axfree)
		agentx_free_finalize(ax);
}

struct agentx_context *
agentx_context(struct agentx_session *axs, const char *name)
{
	struct agentx_context *axc;

	if (axs->axs_dstate == AX_DSTATE_CLOSE)
		agentx_log_axs_fatalx(axs, "%s: use after free", __func__);

	if ((axc = calloc(1, sizeof(*axc))) == NULL)
		return NULL;

	axc->axc_axs = axs;
	axc->axc_name_default = (name == NULL);
	if (name != NULL) {
		axc->axc_name.aos_string = (unsigned char *)strdup(name);
		if (axc->axc_name.aos_string == NULL) {
			free(axc);
			return NULL;
		}
		axc->axc_name.aos_slen = strlen(name);
	}
	axc->axc_cstate = axs->axs_cstate == AX_CSTATE_OPEN ?
	    AX_CSTATE_OPEN : AX_CSTATE_CLOSE;
	axc->axc_dstate = AX_DSTATE_OPEN;
	TAILQ_INIT(&(axc->axc_agentcaps));
	TAILQ_INIT(&(axc->axc_regions));

	TAILQ_INSERT_HEAD(&(axs->axs_contexts), axc, axc_axs_contexts);

	return axc;
}

static int
agentx_context_retry(struct agentx_context *axc)
{
	struct agentx_agentcaps *axa;
	struct agentx_region *axr;

#ifdef AX_DEBUG
	if (axc->axc_cstate != AX_CSTATE_OPEN)
		agentx_log_axc_fatalx(axc, "%s: unexpected retry", __func__);
#endif

	TAILQ_FOREACH(axa, &(axc->axc_agentcaps), axa_axc_agentcaps) {
		if (axa->axa_cstate == AX_CSTATE_CLOSE) {
			if (agentx_agentcaps_start(axa) == -1)
				return -1;
		}
	}
	TAILQ_FOREACH(axr, &(axc->axc_regions), axr_axc_regions) {
		if (axr->axr_cstate == AX_CSTATE_OPEN) {
			if (agentx_region_retry(axr) == -1)
				return -1;
		} else if (axr->axr_cstate == AX_CSTATE_CLOSE) {
			if (agentx_region_start(axr) == -1)
				return -1;
		}
	}
	return 0;
}


static void
agentx_context_start(struct agentx_context *axc)
{
	struct agentx_agentcaps *axa;
	struct agentx_region *axr;

#ifdef AX_DEBUG
	if (axc->axc_cstate != AX_CSTATE_CLOSE)
		agentx_log_axc_fatalx(axc, "%s: unexpected context start",
		    __func__);
#endif
	axc->axc_cstate = AX_CSTATE_OPEN;

	TAILQ_FOREACH(axa, &(axc->axc_agentcaps), axa_axc_agentcaps) {
		if (agentx_agentcaps_start(axa) == -1)
			return;
	}
	TAILQ_FOREACH(axr, &(axc->axc_regions), axr_axc_regions) {
		if (agentx_region_start(axr) == -1)
			return;
	}
}

uint32_t
agentx_context_uptime(struct agentx_context *axc)
{
	struct timespec cur, res;

	if (axc->axc_sysuptimespec.tv_sec == 0 &&
	    axc->axc_sysuptimespec.tv_nsec == 0)
		return 0;

	(void) clock_gettime(CLOCK_MONOTONIC, &cur);

	timespecsub(&cur, &(axc->axc_sysuptimespec), &res);

	return axc->axc_sysuptime +
	    (uint32_t) ((res.tv_sec * 100) + (res.tv_nsec / 10000000));
}

struct agentx_object *
agentx_context_object_find(struct agentx_context *axc,
    const uint32_t oid[], size_t oidlen, int active, int instance)
{
	struct agentx_object *axo, axo_search;
	const char *errstr;

	if (agentx_oidfill(&(axo_search.axo_oid), oid, oidlen, &errstr) == -1) {
		if (oidlen > AGENTX_OID_MIN_LEN) {
#ifdef AX_DEBUG
			agentx_log_axc_fatalx(axc, "%s: %s", __func__, errstr);
#else
			agentx_log_axc_warnx(axc, "%s: %s", __func__, errstr);
			return NULL;
		}
#endif
		if (oidlen == 1)
			axo_search.axo_oid.aoi_id[0] = oid[0];
		axo_search.axo_oid.aoi_idlen = oidlen;
	}

	axo = RB_FIND(axc_objects, &(axc->axc_objects), &axo_search);
	while (axo == NULL && !instance && axo_search.axo_oid.aoi_idlen > 0) {
		axo = RB_FIND(axc_objects, &(axc->axc_objects), &axo_search);
		axo_search.axo_oid.aoi_idlen--;
	}
	if (active && axo != NULL && axo->axo_cstate != AX_CSTATE_OPEN)
		return NULL;
	return axo;
}

struct agentx_object *
agentx_context_object_nfind(struct agentx_context *axc,
    const uint32_t oid[], size_t oidlen, int active, int inclusive)
{
	struct agentx_object *axo, axo_search;
	const char *errstr;

	if (agentx_oidfill(&(axo_search.axo_oid), oid, oidlen, &errstr) == -1) {
		if (oidlen > AGENTX_OID_MIN_LEN) {
#ifdef AX_DEBUG
			agentx_log_axc_fatalx(axc, "%s: %s", __func__, errstr);
#else
			agentx_log_axc_warnx(axc, "%s: %s", __func__, errstr);
			return NULL;
#endif
		}
		if (oidlen == 1)
			axo_search.axo_oid.aoi_id[0] = oid[0];
		axo_search.axo_oid.aoi_idlen = oidlen;
	}

	axo = RB_NFIND(axc_objects, &(axc->axc_objects), &axo_search);
	if (!inclusive && axo != NULL &&
	    ax_oid_cmp(&(axo->axo_oid), &(axo_search.axo_oid)) <= 0) {
		axo = RB_NEXT(axc_objects, &(axc->axc_objects), axo);
	}

	while (active && axo != NULL && axo->axo_cstate != AX_CSTATE_OPEN)
		axo = RB_NEXT(axc_objects, &(axc->axc_objects), axo);
	return axo;
}

void
agentx_context_free(struct agentx_context *axc)
{
	struct agentx_agentcaps *axa, *taxa;
	struct agentx_region *axr, *taxr;

	if (axc == NULL)
		return;

#ifdef AX_DEBUG
	if (axc->axc_dstate == AX_DSTATE_CLOSE)
		agentx_log_axc_fatalx(axc, "%s: double free", __func__);
#endif
	axc->axc_dstate = AX_DSTATE_CLOSE;

	TAILQ_FOREACH_SAFE(axa, &(axc->axc_agentcaps), axa_axc_agentcaps,
	    taxa) {
		if (axa->axa_dstate != AX_DSTATE_CLOSE)
			agentx_agentcaps_free(axa);
	}
	TAILQ_FOREACH_SAFE(axr, &(axc->axc_regions), axr_axc_regions, taxr) {
		if (axr->axr_dstate != AX_DSTATE_CLOSE)
			agentx_region_free(axr);
	}
}

static void
agentx_context_free_finalize(struct agentx_context *axc)
{
	struct agentx_session *axs = axc->axc_axs;
	struct agentx_region *axr, *taxr;
	struct agentx_agentcaps *axa, *taxa;

	TAILQ_FOREACH_SAFE(axa, &(axc->axc_agentcaps), axa_axc_agentcaps, taxa)
		agentx_agentcaps_free_finalize(axa);
	TAILQ_FOREACH_SAFE(axr, &(axc->axc_regions), axr_axc_regions, taxr)
		agentx_region_free_finalize(axr);

	if (!TAILQ_EMPTY(&(axc->axc_regions)) ||
	    !TAILQ_EMPTY(&(axc->axc_agentcaps)) ||
	    axc->axc_cstate != AX_CSTATE_CLOSE ||
	    axc->axc_dstate != AX_DSTATE_CLOSE)
		return;

	TAILQ_REMOVE(&(axs->axs_contexts), axc, axc_axs_contexts);
	free(axc->axc_name.aos_string);
	free(axc);
}

static void
agentx_context_reset(struct agentx_context *axc)
{
	struct agentx_agentcaps *axa, *taxa;
	struct agentx_region *axr, *taxr;
	struct agentx *ax = axc->axc_axs->axs_ax;
	int axfree = ax->ax_free;

	ax->ax_free = 1;

	axc->axc_cstate = AX_CSTATE_CLOSE;
	axc->axc_sysuptimespec.tv_sec = 0;
	axc->axc_sysuptimespec.tv_nsec = 0;

	TAILQ_FOREACH_SAFE(axa, &(axc->axc_agentcaps), axa_axc_agentcaps, taxa)
		agentx_agentcaps_reset(axa);
	TAILQ_FOREACH_SAFE(axr, &(axc->axc_regions), axr_axc_regions, taxr)
		agentx_region_reset(axr);

	if (!axfree)
		agentx_free_finalize(ax);
}

struct agentx_agentcaps *
agentx_agentcaps(struct agentx_context *axc, uint32_t oid[],
    size_t oidlen, const char *descr)
{
	struct agentx_agentcaps *axa;
	const char *errstr;

	if (axc->axc_dstate == AX_DSTATE_CLOSE)
		agentx_log_axc_fatalx(axc, "%s: use after free", __func__);

	if ((axa = calloc(1, sizeof(*axa))) == NULL)
		return NULL;

	axa->axa_axc = axc;
	if (agentx_oidfill(&(axa->axa_oid), oid, oidlen, &errstr) == -1) {
#ifdef AX_DEBUG
		agentx_log_axc_fatalx(axc, "%s: %s", __func__, errstr);
#else
		agentx_log_axc_warnx(axc, "%s: %s", __func__, errstr);
		return NULL;
#endif
	}
	axa->axa_descr.aos_string = (unsigned char *)strdup(descr);
	if (axa->axa_descr.aos_string == NULL) {
		free(axa);
		return NULL;
	}
	axa->axa_descr.aos_slen = strlen(descr);
	axa->axa_cstate = AX_CSTATE_CLOSE;
	axa->axa_dstate = AX_DSTATE_OPEN;

	TAILQ_INSERT_TAIL(&(axc->axc_agentcaps), axa, axa_axc_agentcaps);

	if (axc->axc_cstate == AX_CSTATE_OPEN)
		agentx_agentcaps_start(axa);

	return axa;
}

static int
agentx_agentcaps_start(struct agentx_agentcaps *axa)
{
	struct agentx_context *axc = axa->axa_axc;
	struct agentx_session *axs = axc->axc_axs;
	struct agentx *ax = axs->axs_ax;
	uint32_t packetid;

#ifdef AX_DEBUG
	if (axc->axc_cstate != AX_CSTATE_OPEN ||
	    axa->axa_cstate != AX_CSTATE_CLOSE ||
	    axa->axa_dstate != AX_DSTATE_OPEN)
		agentx_log_axc_fatalx(axc,
		    "%s: unexpected region registration", __func__);
#endif

	packetid = ax_addagentcaps(ax->ax_ax, axs->axs_id,
	    AGENTX_CONTEXT_CTX(axc), &(axa->axa_oid), &(axa->axa_descr));
	if (packetid == 0) {
		agentx_log_axc_warn(axc, "couldn't generate %s",
		    ax_pdutype2string(AX_PDU_TYPE_ADDAGENTCAPS));
		agentx_reset(ax);
		return -1;
	}
	agentx_log_axc_info(axc, "agentcaps %s: opening",
	    ax_oid2string(&(axa->axa_oid)));
	axa->axa_cstate = AX_CSTATE_WAITOPEN;
	return agentx_request(ax, packetid, agentx_agentcaps_finalize,
	    axa);
}

static int
agentx_agentcaps_finalize(struct ax_pdu *pdu, void *cookie)
{
	struct agentx_agentcaps *axa = cookie;
	struct agentx_context *axc = axa->axa_axc;

#ifdef AX_DEBUG
	if (axa->axa_cstate != AX_CSTATE_WAITOPEN)
		agentx_log_axc_fatalx(axc,
		    "%s: not expecting agentcaps open", __func__);
#endif

	if (pdu->ap_payload.ap_response.ap_error != AX_PDU_ERROR_NOERROR) {
		/* Agentcaps failing is nothing too serious */
		agentx_log_axc_warn(axc, "agentcaps %s: %s",
		    ax_oid2string(&(axa->axa_oid)),
		    ax_error2string(pdu->ap_payload.ap_response.ap_error));
		axa->axa_cstate = AX_CSTATE_CLOSE;
		return 0;
	}

	axa->axa_cstate = AX_CSTATE_OPEN;

	agentx_log_axc_info(axc, "agentcaps %s: open",
	    ax_oid2string(&(axa->axa_oid)));

	if (axa->axa_dstate == AX_DSTATE_CLOSE)
		agentx_agentcaps_close(axa);

	return 0;
}

static int
agentx_agentcaps_close(struct agentx_agentcaps *axa)
{
	struct agentx_context *axc = axa->axa_axc;
	struct agentx_session *axs = axc->axc_axs;
	struct agentx *ax = axs->axs_ax;
	uint32_t packetid;

#ifdef AX_DEBUG
	if (axa->axa_cstate != AX_CSTATE_OPEN)
		agentx_log_axc_fatalx(axc, "%s: unexpected agentcaps close",
		    __func__);
#endif

	axa->axa_cstate = AX_CSTATE_WAITCLOSE;
	if (axs->axs_cstate == AX_CSTATE_WAITCLOSE)
		return 0;

	packetid = ax_removeagentcaps(ax->ax_ax, axs->axs_id,
	    AGENTX_CONTEXT_CTX(axc), &(axa->axa_oid));
	if (packetid == 0) {
		agentx_log_axc_warn(axc, "couldn't generate %s",
		    ax_pdutype2string(AX_PDU_TYPE_REMOVEAGENTCAPS));
		agentx_reset(ax);
		return -1;
	}
	agentx_log_axc_info(axc, "agentcaps %s: closing",
	    ax_oid2string(&(axa->axa_oid)));
	return agentx_request(ax, packetid,
	    agentx_agentcaps_close_finalize, axa);
}

static int
agentx_agentcaps_close_finalize(struct ax_pdu *pdu, void *cookie)
{
	struct agentx_agentcaps *axa = cookie;
	struct agentx_context *axc = axa->axa_axc;
	struct agentx_session *axs = axc->axc_axs;
	struct agentx *ax = axs->axs_ax;
	int axfree = ax->ax_free;

#ifdef AX_DEBUG
	if (axa->axa_cstate != AX_CSTATE_WAITCLOSE)
		agentx_log_axc_fatalx(axc, "%s: unexpected agentcaps close",
		    __func__);
#endif

	if (pdu->ap_payload.ap_response.ap_error != AX_PDU_ERROR_NOERROR) {
		agentx_log_axc_warnx(axc, "agentcaps %s: %s",
		    ax_oid2string(&(axa->axa_oid)),
		    ax_error2string(pdu->ap_payload.ap_response.ap_error));
		agentx_reset(ax);
		return -1;
	}

	axa->axa_cstate = AX_CSTATE_CLOSE;
	ax->ax_free = 1;

	agentx_log_axc_info(axc, "agentcaps %s: closed",
	    ax_oid2string(&(axa->axa_oid)));

	if (axc->axc_cstate == AX_CSTATE_OPEN &&
	    axa->axa_dstate == AX_DSTATE_OPEN)
		agentx_agentcaps_start(axa);

	if (!axfree)
		agentx_free_finalize(ax);
	return 0;
}

void
agentx_agentcaps_free(struct agentx_agentcaps *axa)
{
	struct agentx *ax;
	int axfree;

	if (axa == NULL)
		return;

	ax = axa->axa_axc->axc_axs->axs_ax;

	axfree = ax->ax_free;
	ax->ax_free = 1;

	if (axa->axa_dstate == AX_DSTATE_CLOSE)
		agentx_log_axc_fatalx(axa->axa_axc, "%s: double free",
		    __func__);

	axa->axa_dstate = AX_DSTATE_CLOSE;

	if (axa->axa_cstate == AX_CSTATE_OPEN)
		agentx_agentcaps_close(axa);

	if (!axfree)
		agentx_free_finalize(ax);
}

static void
agentx_agentcaps_free_finalize(struct agentx_agentcaps *axa)
{
	struct agentx_context *axc = axa->axa_axc;

	if (axa->axa_dstate != AX_DSTATE_CLOSE ||
	    axa->axa_cstate != AX_CSTATE_CLOSE)
		return;

	TAILQ_REMOVE(&(axc->axc_agentcaps), axa, axa_axc_agentcaps);
	free(axa->axa_descr.aos_string);
	free(axa);
}

static void
agentx_agentcaps_reset(struct agentx_agentcaps *axa)
{
	struct agentx *ax = axa->axa_axc->axc_axs->axs_ax;

	axa->axa_cstate = AX_CSTATE_CLOSE;

	if (!ax->ax_free)
		agentx_free_finalize(ax);
}

struct agentx_region *
agentx_region(struct agentx_context *axc, uint32_t oid[],
    size_t oidlen, uint8_t timeout)
{
	struct agentx_region *axr;
	struct ax_oid tmpoid;
	const char *errstr;

	if (axc->axc_dstate == AX_DSTATE_CLOSE)
		agentx_log_axc_fatalx(axc, "%s: use after free", __func__);

	if (agentx_oidfill(&tmpoid, oid, oidlen, &errstr) == -1) {
#ifdef AX_DEBUG
		agentx_log_axc_fatalx(axc, "%s: %s", __func__, errstr);
#else
		return NULL;
#endif
		
	}
	TAILQ_FOREACH(axr, &(axc->axc_regions), axr_axc_regions) {
		if (ax_oid_cmp(&(axr->axr_oid), &tmpoid) == 0) {
#ifdef AX_DEBUG
			agentx_log_axc_fatalx(axc,
			    "%s: duplicate region registration", __func__);
#else
			errno = EINVAL;
			return NULL;
#endif
		}
	}

	if ((axr = calloc(1, sizeof(*axr))) == NULL)
		return NULL;

	axr->axr_axc = axc;
	axr->axr_timeout = timeout;
	axr->axr_priority = AX_PRIORITY_DEFAULT;
	bcopy(&tmpoid, &(axr->axr_oid), sizeof(axr->axr_oid));
	axr->axr_cstate = AX_CSTATE_CLOSE;
	axr->axr_dstate = AX_DSTATE_OPEN;
	TAILQ_INIT(&(axr->axr_indices));
	TAILQ_INIT(&(axr->axr_objects));

	TAILQ_INSERT_HEAD(&(axc->axc_regions), axr, axr_axc_regions);

	if (axc->axc_cstate == AX_CSTATE_OPEN)
		(void) agentx_region_start(axr);

	return axr;
}

static int
agentx_region_retry(struct agentx_region *axr)
{
	struct agentx_index *axi;
	struct agentx_object *axo;

#ifdef AX_DEBUG
	if (axr->axr_cstate != AX_CSTATE_OPEN)
		agentx_log_axc_fatalx(axr->axr_axc,
		    "%s: unexpected retry", __func__);
#endif

	TAILQ_FOREACH(axi, &(axr->axr_indices), axi_axr_indices) {
		if (axi->axi_cstate == AX_CSTATE_CLOSE) {
			if (agentx_index_start(axi) == -1)
				return -1;
		}
	}
	TAILQ_FOREACH(axo, &(axr->axr_objects), axo_axr_objects) {
		if (axo->axo_cstate == AX_CSTATE_CLOSE) {
			if (agentx_object_start(axo) == -1)
				return -1;
		}
	}
	return 0;
}

static int
agentx_region_start(struct agentx_region *axr)
{
	struct agentx_context *axc = axr->axr_axc;
	struct agentx_session *axs = axc->axc_axs;
	struct agentx *ax = axs->axs_ax;
	uint32_t packetid;

#ifdef AX_DEBUG
	if (axc->axc_cstate != AX_CSTATE_OPEN ||
	    axr->axr_cstate != AX_CSTATE_CLOSE ||
	    axr->axr_dstate != AX_DSTATE_OPEN)
		agentx_log_axc_fatalx(axc,
		    "%s: unexpected region registration", __func__);
#endif

	packetid = ax_register(ax->ax_ax, 0, axs->axs_id,
	    AGENTX_CONTEXT_CTX(axc), axr->axr_timeout, axr->axr_priority,
	    0, &(axr->axr_oid), 0);
	if (packetid == 0) {
		agentx_log_axc_warn(axc, "couldn't generate %s",
		    ax_pdutype2string(AX_PDU_TYPE_REGISTER));
		agentx_reset(ax);
		return -1;
	}
	agentx_log_axc_info(axc, "region %s: opening",
	    ax_oid2string(&(axr->axr_oid)));
	axr->axr_cstate = AX_CSTATE_WAITOPEN;
	return agentx_request(ax, packetid, agentx_region_finalize, axr);
}

static int
agentx_region_finalize(struct ax_pdu *pdu, void *cookie)
{
	struct agentx_region *axr = cookie;
	struct agentx_context *axc = axr->axr_axc;
	struct agentx_index *axi;
	struct agentx_object *axo;

#ifdef AX_DEBUG
	if (axr->axr_cstate != AX_CSTATE_WAITOPEN)
		agentx_log_axc_fatalx(axc, "%s: not expecting region open",
		    __func__);
#endif

	if (pdu->ap_payload.ap_response.ap_error == AX_PDU_ERROR_NOERROR) {
		axr->axr_cstate = AX_CSTATE_OPEN;
		agentx_log_axc_info(axc, "region %s: open",
		    ax_oid2string(&(axr->axr_oid)));
	} else if (pdu->ap_payload.ap_response.ap_error ==
	    AX_PDU_ERROR_DUPLICATEREGISTRATION) {
		axr->axr_cstate = AX_CSTATE_CLOSE;
		/* Try at lower priority: first come first serve */
		if ((++axr->axr_priority) != 0) {
			agentx_log_axc_warnx(axc, "region %s: duplicate, "
			    "reducing priority",
			    ax_oid2string(&(axr->axr_oid)));
			return agentx_region_start(axr);
		}
		agentx_log_axc_info(axc, "region %s: duplicate, can't "
		    "reduce priority, ignoring",
		    ax_oid2string(&(axr->axr_oid)));
	} else {
		axr->axr_cstate = AX_CSTATE_CLOSE;
		agentx_log_axc_warnx(axc, "region %s: %s",
		     ax_oid2string(&(axr->axr_oid)),
		     ax_error2string(pdu->ap_payload.ap_response.ap_error));
		return -1;
	}

	if (axr->axr_dstate == AX_DSTATE_CLOSE) {
		if (agentx_region_close(axr) == -1)
			return -1;
	} else {
		TAILQ_FOREACH(axi, &(axr->axr_indices), axi_axr_indices) {
			if (agentx_index_start(axi) == -1)
				return -1;
		}
		TAILQ_FOREACH(axo, &(axr->axr_objects), axo_axr_objects) {
			if (agentx_object_start(axo) == -1)
				return -1;
		}
	}
	return 0;
}

static int
agentx_region_close(struct agentx_region *axr)
{
	struct agentx_context *axc = axr->axr_axc;
	struct agentx_session *axs = axc->axc_axs;
	struct agentx *ax = axs->axs_ax;
	uint32_t packetid;

#ifdef AX_DEBUG
	if (axr->axr_cstate != AX_CSTATE_OPEN)
		agentx_log_axc_fatalx(axc, "%s: unexpected region close",
		    __func__);
#endif

	axr->axr_cstate = AX_CSTATE_WAITCLOSE;
	if (axs->axs_cstate == AX_CSTATE_WAITCLOSE)
		return 0;

	packetid = ax_unregister(ax->ax_ax, axs->axs_id,
	    AGENTX_CONTEXT_CTX(axc), axr->axr_priority, 0, &(axr->axr_oid),
	    0);
	if (packetid == 0) {
		agentx_log_axc_warn(axc, "couldn't generate %s",
		    ax_pdutype2string(AX_PDU_TYPE_UNREGISTER));
		agentx_reset(ax);
		return -1;
	}
	agentx_log_axc_info(axc, "region %s: closing",
	    ax_oid2string(&(axr->axr_oid)));
	return agentx_request(ax, packetid, agentx_region_close_finalize,
	    axr);
}

static int
agentx_region_close_finalize(struct ax_pdu *pdu, void *cookie)
{
	struct agentx_region *axr = cookie;
	struct agentx_context *axc = axr->axr_axc;
	struct agentx_session *axs = axc->axc_axs;
	struct agentx *ax = axs->axs_ax;
	int axfree = ax->ax_free;

#ifdef AX_DEBUG
	if (axr->axr_cstate != AX_CSTATE_WAITCLOSE)
		agentx_log_axc_fatalx(axc, "%s: unexpected region close",
		    __func__);
#endif

	if (pdu->ap_payload.ap_response.ap_error != AX_PDU_ERROR_NOERROR) {
		agentx_log_axc_warnx(axc, "closing %s: %s",
		    ax_oid2string(&(axr->axr_oid)),
		    ax_error2string(pdu->ap_payload.ap_response.ap_error));
		agentx_reset(ax);
		return -1;
	}

	ax->ax_free = 1;
	axr->axr_priority = AX_PRIORITY_DEFAULT;
	axr->axr_cstate = AX_CSTATE_CLOSE;

	agentx_log_axc_info(axc, "region %s: closed",
	    ax_oid2string(&(axr->axr_oid)));

	if (axc->axc_cstate == AX_CSTATE_OPEN &&
	    axr->axr_dstate == AX_DSTATE_OPEN)
		agentx_region_start(axr);

	if (!axfree)
		agentx_free_finalize(ax);
	return 0;
}

void
agentx_region_free(struct agentx_region *axr)
{
	struct agentx_index *axi, *taxi;
	struct agentx_object *axo, *taxo;
	struct agentx *ax;
	int axfree;

	if (axr == NULL)
		return;

	ax = axr->axr_axc->axc_axs->axs_ax;
	axfree = ax->ax_free;
	ax->ax_free = 1;

	if (axr->axr_dstate == AX_DSTATE_CLOSE)
		agentx_log_axc_fatalx(axr->axr_axc, "%s: double free",
		    __func__);

	axr->axr_dstate = AX_DSTATE_CLOSE;

	TAILQ_FOREACH_SAFE(axi, &(axr->axr_indices), axi_axr_indices, taxi) {
		if (axi->axi_dstate != AX_DSTATE_CLOSE)
			agentx_index_free(axi);
	}

	TAILQ_FOREACH_SAFE(axo, &(axr->axr_objects), axo_axr_objects, taxo) {
		if (axo->axo_dstate != AX_DSTATE_CLOSE)
			agentx_object_free(axo);
	}

	if (axr->axr_cstate == AX_CSTATE_OPEN)
		agentx_region_close(axr);

	if (!axfree)
		agentx_free_finalize(ax);
}

static void
agentx_region_free_finalize(struct agentx_region *axr)
{
	struct agentx_context *axc = axr->axr_axc;
	struct agentx_index *axi, *taxi;
	struct agentx_object *axo, *taxo;

	TAILQ_FOREACH_SAFE(axo, &(axr->axr_objects), axo_axr_objects, taxo)
		agentx_object_free_finalize(axo);
	TAILQ_FOREACH_SAFE(axi, &(axr->axr_indices), axi_axr_indices, taxi)
		agentx_index_free_finalize(axi);

	if (!TAILQ_EMPTY(&(axr->axr_indices)) ||
	    !TAILQ_EMPTY(&(axr->axr_objects)) ||
	    axr->axr_cstate != AX_CSTATE_CLOSE ||
	    axr->axr_dstate != AX_DSTATE_CLOSE)
		return;

	TAILQ_REMOVE(&(axc->axc_regions), axr, axr_axc_regions);
	free(axr);
}

static void
agentx_region_reset(struct agentx_region *axr)
{
	struct agentx_index *axi, *taxi;
	struct agentx_object *axo, *taxo;
	struct agentx *ax = axr->axr_axc->axc_axs->axs_ax;
	int axfree = ax->ax_free;

	axr->axr_cstate = AX_CSTATE_CLOSE;
	axr->axr_priority = AX_PRIORITY_DEFAULT;
	ax->ax_free = 1;

	TAILQ_FOREACH_SAFE(axi, &(axr->axr_indices), axi_axr_indices, taxi)
		agentx_index_reset(axi);
	TAILQ_FOREACH_SAFE(axo, &(axr->axr_objects), axo_axr_objects, taxo)
		agentx_object_reset(axo);

	if (!axfree)
		agentx_free_finalize(ax);
}

struct agentx_index *
agentx_index_integer_new(struct agentx_region *axr, uint32_t oid[],
    size_t oidlen)
{
	struct ax_varbind vb;
	const char *errstr;

	vb.avb_type = AX_DATA_TYPE_INTEGER;
	if (agentx_oidfill(&(vb.avb_oid), oid, oidlen, &errstr) == -1) {
#ifdef AX_DEBUG
		agentx_log_axc_fatalx(axr->axr_axc, "%s: %s", __func__, errstr);
#else
		agentx_log_axc_warnx(axr->axr_axc, "%s: %s", __func__, errstr);
		return NULL;
#endif
	}
	vb.avb_data.avb_int32 = 0;

	return agentx_index(axr, &vb, AXI_TYPE_NEW);
}

struct agentx_index *
agentx_index_integer_any(struct agentx_region *axr, uint32_t oid[],
    size_t oidlen)
{
	struct ax_varbind vb;
	const char *errstr;

	vb.avb_type = AX_DATA_TYPE_INTEGER;
	if (agentx_oidfill(&(vb.avb_oid), oid, oidlen, &errstr) == -1) {
#ifdef AX_DEBUG
		agentx_log_axc_fatalx(axr->axr_axc, "%s: %s", __func__, errstr);
#else
		agentx_log_axc_warnx(axr->axr_axc, "%s: %s", __func__, errstr);
		return NULL;
#endif
	}
	vb.avb_data.avb_int32 = 0;

	return agentx_index(axr, &vb, AXI_TYPE_ANY);
}

struct agentx_index *
agentx_index_integer_value(struct agentx_region *axr, uint32_t oid[],
    size_t oidlen, int32_t value)
{
	struct ax_varbind vb;
	const char *errstr;

	if (value < 0) {
#ifdef AX_DEBUG
		agentx_log_axc_fatalx(axr->axr_axc, "%s: value < 0", __func__);
#else
		agentx_log_axc_warnx(axr->axr_axc, "%s: value < 0", __func__);
		errno = EINVAL;
		return NULL;
#endif
	}

	vb.avb_type = AX_DATA_TYPE_INTEGER;
	if (agentx_oidfill(&(vb.avb_oid), oid, oidlen, &errstr) == -1) {
#ifdef AX_DEBUG
		agentx_log_axc_fatalx(axr->axr_axc, "%s: %s", __func__, errstr);
#else
		agentx_log_axc_warnx(axr->axr_axc, "%s: %s", __func__, errstr);
		return NULL;
#endif
	}
	vb.avb_data.avb_int32 = value;

	return agentx_index(axr, &vb, AXI_TYPE_VALUE);
}

struct agentx_index *
agentx_index_integer_dynamic(struct agentx_region *axr, uint32_t oid[],
    size_t oidlen)
{
	struct ax_varbind vb;
	const char *errstr;

	vb.avb_type = AX_DATA_TYPE_INTEGER;
	if (agentx_oidfill(&(vb.avb_oid), oid, oidlen, &errstr) == -1) {
#ifdef AX_DEBUG
		agentx_log_axc_fatalx(axr->axr_axc, "%s: %s", __func__, errstr);
#else
		agentx_log_axc_warnx(axr->axr_axc, "%s: %s", __func__, errstr);
		return NULL;
#endif
	}

	return agentx_index(axr, &vb, AXI_TYPE_DYNAMIC);
}

struct agentx_index *
agentx_index_string_dynamic(struct agentx_region *axr, uint32_t oid[],
    size_t oidlen)
{
	struct ax_varbind vb;
	const char *errstr;

	vb.avb_type = AX_DATA_TYPE_OCTETSTRING;
	if (agentx_oidfill(&(vb.avb_oid), oid, oidlen, &errstr) == -1) {
#ifdef AX_DEBUG
		agentx_log_axc_fatalx(axr->axr_axc, "%s: %s", __func__, errstr);
#else
		agentx_log_axc_warnx(axr->axr_axc, "%s: %s", __func__, errstr);
		return NULL;
#endif
	}
	vb.avb_data.avb_ostring.aos_slen = 0;
	vb.avb_data.avb_ostring.aos_string = NULL;

	return agentx_index(axr, &vb, AXI_TYPE_DYNAMIC);
}

struct agentx_index *
agentx_index_nstring_dynamic(struct agentx_region *axr, uint32_t oid[],
    size_t oidlen, size_t vlen)
{
	struct ax_varbind vb;
	const char *errstr;

	if (vlen == 0 || vlen > AGENTX_OID_MAX_LEN) {
#ifdef AX_DEBUG
		agentx_log_axc_fatalx(axr->axr_axc, "%s: invalid string "
		    "length: %zu\n", __func__, vlen);
#else
		agentx_log_axc_warnx(axr->axr_axc, "%s: invalid string "
		    "length: %zu\n", __func__, vlen);
		errno = EINVAL;
		return NULL;
#endif
	}

	vb.avb_type = AX_DATA_TYPE_OCTETSTRING;
	if (agentx_oidfill(&(vb.avb_oid), oid, oidlen, &errstr) == -1) {
#ifdef AX_DEBUG
		agentx_log_axc_fatalx(axr->axr_axc, "%s: %s", __func__, errstr);
#else
		agentx_log_axc_warnx(axr->axr_axc, "%s: %s", __func__, errstr);
		return NULL;
#endif
	}
	vb.avb_data.avb_ostring.aos_slen = vlen;
	vb.avb_data.avb_ostring.aos_string = NULL;

	return agentx_index(axr, &vb, AXI_TYPE_DYNAMIC);
}

struct agentx_index *
agentx_index_oid_dynamic(struct agentx_region *axr, uint32_t oid[],
    size_t oidlen)
{
	struct ax_varbind vb;
	const char *errstr;

	vb.avb_type = AX_DATA_TYPE_OID;
	if (agentx_oidfill(&(vb.avb_oid), oid, oidlen, &errstr) == -1) {
#ifdef AX_DEBUG
		agentx_log_axc_fatalx(axr->axr_axc, "%s: %s", __func__, errstr);
#else
		agentx_log_axc_warnx(axr->axr_axc, "%s: %s", __func__, errstr);
		return NULL;
#endif
	}
	vb.avb_data.avb_oid.aoi_idlen = 0;

	return agentx_index(axr, &vb, AXI_TYPE_DYNAMIC);
}

struct agentx_index *
agentx_index_noid_dynamic(struct agentx_region *axr, uint32_t oid[],
    size_t oidlen, size_t vlen)
{
	struct ax_varbind vb;
	const char *errstr;

	if (vlen < AGENTX_OID_MIN_LEN || vlen > AGENTX_OID_MAX_LEN) {
#ifdef AX_DEBUG
		agentx_log_axc_fatalx(axr->axr_axc, "%s: invalid string "
		    "length: %zu\n", __func__, vlen);
#else
		agentx_log_axc_warnx(axr->axr_axc, "%s: invalid string "
		    "length: %zu\n", __func__, vlen);
		errno = EINVAL;
		return NULL;
#endif
	}

	vb.avb_type = AX_DATA_TYPE_OID;
	if (agentx_oidfill(&(vb.avb_oid), oid, oidlen, &errstr) == -1) {
#ifdef AX_DEBUG
		agentx_log_axc_fatalx(axr->axr_axc, "%s: %s", __func__, errstr);
#else
		agentx_log_axc_warnx(axr->axr_axc, "%s: %s", __func__, errstr);
		return NULL;
#endif
	}
	vb.avb_data.avb_oid.aoi_idlen = vlen;

	return agentx_index(axr, &vb, AXI_TYPE_DYNAMIC);
}

struct agentx_index *
agentx_index_ipaddress_dynamic(struct agentx_region *axr, uint32_t oid[],
    size_t oidlen)
{
	struct ax_varbind vb;
	const char *errstr;

	vb.avb_type = AX_DATA_TYPE_IPADDRESS;
	if (agentx_oidfill(&(vb.avb_oid), oid, oidlen, &errstr) == -1) {
#ifdef AX_DEBUG
		agentx_log_axc_fatalx(axr->axr_axc, "%s: %s", __func__, errstr);
#else
		agentx_log_axc_warnx(axr->axr_axc, "%s: %s", __func__, errstr);
		return NULL;
#endif
	}
	vb.avb_data.avb_ostring.aos_string = NULL;

	return agentx_index(axr, &vb, AXI_TYPE_DYNAMIC);
}

static struct agentx_index *
agentx_index(struct agentx_region *axr, struct ax_varbind *vb,
    enum agentx_index_type type)
{
	struct agentx_index *axi;

	if (axr->axr_dstate == AX_DSTATE_CLOSE)
		agentx_log_axc_fatalx(axr->axr_axc, "%s: use after free",
		    __func__);
	if (ax_oid_cmp(&(axr->axr_oid), &(vb->avb_oid)) != -2) {
#ifdef AX_DEBUG
		agentx_log_axc_fatalx(axr->axr_axc, "%s: oid is not child "
		    "of region %s", __func__,
		    ax_oid2string(&(vb->avb_oid)));
#else
		agentx_log_axc_warnx(axr->axr_axc, "%s: oid is not child of "
		    "region %s", __func__, ax_oid2string(&(vb->avb_oid)));
		errno = EINVAL;
		return NULL;
#endif
	}

	if ((axi = calloc(1, sizeof(*axi))) == NULL)
		return NULL;

	axi->axi_axr = axr;
	axi->axi_type = type;
	bcopy(vb, &(axi->axi_vb), sizeof(*vb));
	axi->axi_cstate = AX_CSTATE_CLOSE;
	axi->axi_dstate = AX_DSTATE_OPEN;
	TAILQ_INSERT_HEAD(&(axr->axr_indices), axi, axi_axr_indices);

	if (axr->axr_cstate == AX_CSTATE_OPEN)
		agentx_index_start(axi);

	return axi;
}

static int
agentx_index_start(struct agentx_index *axi)
{
	struct agentx_region *axr = axi->axi_axr;
	struct agentx_context *axc = axr->axr_axc;
	struct agentx_session *axs = axc->axc_axs;
	struct agentx *ax = axs->axs_ax;
	uint32_t packetid;
	int flags = 0;

#ifdef AX_DEBUG
	if (axr->axr_cstate != AX_CSTATE_OPEN ||
	    axi->axi_cstate != AX_CSTATE_CLOSE ||
	    axi->axi_dstate != AX_DSTATE_OPEN)
		agentx_log_axc_fatalx(axc, "%s: unexpected index allocation",
		    __func__);
#endif

	axi->axi_cstate = AX_CSTATE_WAITOPEN;

	if (axi->axi_type == AXI_TYPE_NEW)
		flags = AX_PDU_FLAG_NEW_INDEX;
	else if (axi->axi_type == AXI_TYPE_ANY)
		flags = AX_PDU_FLAG_ANY_INDEX;
	else if (axi->axi_type == AXI_TYPE_DYNAMIC) {
		agentx_index_finalize(NULL, axi);
		return 0;
	}

	/* We might be able to bundle, but if we fail we'd have to reorganise */
	packetid = ax_indexallocate(ax->ax_ax, flags, axs->axs_id,
	    AGENTX_CONTEXT_CTX(axc), &(axi->axi_vb), 1);
	if (packetid == 0) {
		agentx_log_axc_warn(axc, "couldn't generate %s",
		    ax_pdutype2string(AX_PDU_TYPE_INDEXDEALLOCATE));
		agentx_reset(ax);
		return -1;
	}
	if (axi->axi_type == AXI_TYPE_VALUE)
		agentx_log_axc_info(axc, "index %s: allocating '%d'",
		    ax_oid2string(&(axi->axi_vb.avb_oid)),
		    axi->axi_vb.avb_data.avb_int32);
	else if (axi->axi_type == AXI_TYPE_ANY)
		agentx_log_axc_info(axc, "index %s: allocating any index",
		    ax_oid2string(&(axi->axi_vb.avb_oid)));
	else if (axi->axi_type == AXI_TYPE_NEW)
		agentx_log_axc_info(axc, "index %s: allocating new index",
		    ax_oid2string(&(axi->axi_vb.avb_oid)));

	return agentx_request(ax, packetid, agentx_index_finalize, axi);
}

static int
agentx_index_finalize(struct ax_pdu *pdu, void *cookie)
{
	struct agentx_index *axi = cookie;
	struct agentx_region *axr = axi->axi_axr;
	struct agentx_context *axc = axr->axr_axc;
	struct ax_pdu_response *resp;
	size_t i;

#ifdef AX_DEBUG
	if (axi->axi_cstate != AX_CSTATE_WAITOPEN)
		agentx_log_axc_fatalx(axc,
		    "%s: not expecting index allocate", __func__);
#endif
	if (axi->axi_type == AXI_TYPE_DYNAMIC) {
		axi->axi_cstate = AX_CSTATE_OPEN;
		goto objects_start;
	}

	resp = &(pdu->ap_payload.ap_response);
	if (resp->ap_error != AX_PDU_ERROR_NOERROR) {
		axi->axi_cstate = AX_CSTATE_CLOSE;
		agentx_log_axc_warnx(axc, "index %s: %s",
		    ax_oid2string(&(axr->axr_oid)),
		    ax_error2string(resp->ap_error));
		return 0;
	}
	axi->axi_cstate = AX_CSTATE_OPEN;
	if (resp->ap_nvarbind != 1) {
		agentx_log_axc_warnx(axc, "index %s: unexpected number of "
		    "indices", ax_oid2string(&(axr->axr_oid)));
		axi->axi_cstate = AX_CSTATE_CLOSE;
		return -1;
	}
	if (resp->ap_varbindlist[0].avb_type != axi->axi_vb.avb_type) {
		agentx_log_axc_warnx(axc, "index %s: unexpected index type",
		    ax_oid2string(&(axr->axr_oid)));
		axi->axi_cstate = AX_CSTATE_CLOSE;
		return -1;
	}
	if (ax_oid_cmp(&(resp->ap_varbindlist[0].avb_oid),
	    &(axi->axi_vb.avb_oid)) != 0) {
		agentx_log_axc_warnx(axc, "index %s: unexpected oid",
		    ax_oid2string(&(axr->axr_oid)));
		axi->axi_cstate = AX_CSTATE_CLOSE;
		return -1;
	}

	switch (axi->axi_vb.avb_type) {
	case AX_DATA_TYPE_INTEGER:
		if (axi->axi_type == AXI_TYPE_NEW ||
		    axi->axi_type == AXI_TYPE_ANY)
			axi->axi_vb.avb_data.avb_int32 =
			    resp->ap_varbindlist[0].avb_data.avb_int32;
		else if (axi->axi_vb.avb_data.avb_int32 !=
		    resp->ap_varbindlist[0].avb_data.avb_int32) {
			agentx_log_axc_warnx(axc, "index %s: unexpected "
			    "index value", ax_oid2string(&(axr->axr_oid)));
			axi->axi_cstate = AX_CSTATE_CLOSE;
			return -1;
		}
		agentx_log_axc_info(axc, "index %s: allocated '%d'",
		    ax_oid2string(&(axi->axi_vb.avb_oid)),
		    axi->axi_vb.avb_data.avb_int32);
		break;
	default:
		agentx_log_axc_fatalx(axc, "%s: Unsupported index type",
		    __func__);
	}

	if (axi->axi_dstate == AX_DSTATE_CLOSE)
		return agentx_index_close(axi);

 objects_start:
	/* TODO Make use of range_subid register */
	for (i = 0; i < axi->axi_objectlen; i++) {
		if (axi->axi_object[i]->axo_dstate == AX_DSTATE_OPEN) {
			if (agentx_object_start(axi->axi_object[i]) == -1)
				return -1;
		}
	}
	return 0;
}

void
agentx_index_free(struct agentx_index *axi)
{
	size_t i;
	struct agentx_object *axo;
	struct agentx *ax;
	int axfree;

	if (axi == NULL)
		return;

	ax = axi->axi_axr->axr_axc->axc_axs->axs_ax;
	axfree = ax->ax_free;
	ax->ax_free = 1;

	if (axi->axi_dstate == AX_DSTATE_CLOSE)
		agentx_log_axc_fatalx(axi->axi_axr->axr_axc,
		    "%s: double free", __func__);

	/* TODO Do a range_subid unregister before freeing */
	for (i = 0; i < axi->axi_objectlen; i++) {
		axo = axi->axi_object[i];
		if (axo->axo_dstate != AX_DSTATE_CLOSE) {
			agentx_object_free(axo);
			if (axi->axi_object[i] != axo)
				i--;
		}
	}

	axi->axi_dstate = AX_DSTATE_CLOSE;

	if (axi->axi_cstate == AX_CSTATE_OPEN)
		(void) agentx_index_close(axi);
	if (!axfree)
		agentx_free_finalize(ax);
}

static void
agentx_index_free_finalize(struct agentx_index *axi)
{
	struct agentx_region *axr = axi->axi_axr;

	if (axi->axi_cstate != AX_CSTATE_CLOSE ||
	    axi->axi_dstate != AX_DSTATE_CLOSE ||
	    axi->axi_objectlen != 0)
		return;

	TAILQ_REMOVE(&(axr->axr_indices), axi, axi_axr_indices);
	ax_varbind_free(&(axi->axi_vb));
	free(axi->axi_object);
	free(axi);
}

static void
agentx_index_reset(struct agentx_index *axi)
{
	struct agentx *ax = axi->axi_axr->axr_axc->axc_axs->axs_ax;

	axi->axi_cstate = AX_CSTATE_CLOSE;

	if (!ax->ax_free)
		agentx_free_finalize(ax);
}

static int
agentx_index_close(struct agentx_index *axi)
{
	struct agentx_region *axr = axi->axi_axr;
	struct agentx_context *axc = axr->axr_axc;
	struct agentx_session *axs = axc->axc_axs;
	struct agentx *ax = axs->axs_ax;
	uint32_t packetid;

#ifdef AX_DEBUG
	if (axi->axi_cstate != AX_CSTATE_OPEN)
		agentx_log_axc_fatalx(axc,
		    "%s: unexpected index deallocation", __func__);
#endif

	axi->axi_cstate = AX_CSTATE_WAITCLOSE;
	if (axs->axs_cstate == AX_CSTATE_WAITCLOSE)
		return 0;

	/* We might be able to bundle, but if we fail we'd have to reorganise */
	packetid = ax_indexdeallocate(ax->ax_ax, axs->axs_id,
	    AGENTX_CONTEXT_CTX(axc), &(axi->axi_vb), 1);
	if (packetid == 0) {
		agentx_log_axc_warn(axc, "couldn't generate %s",
		    ax_pdutype2string(AX_PDU_TYPE_INDEXDEALLOCATE));
		agentx_reset(ax);
		return -1;
	}
	agentx_log_axc_info(axc, "index %s: deallocating",
	    ax_oid2string(&(axi->axi_vb.avb_oid)));
	return agentx_request(ax, packetid, agentx_index_close_finalize,
	    axi);
}

static int
agentx_index_close_finalize(struct ax_pdu *pdu, void *cookie)
{
	struct agentx_index *axi = cookie;
	struct agentx_region *axr = axi->axi_axr;
	struct agentx_context *axc = axr->axr_axc;
	struct agentx_session *axs = axc->axc_axs;
	struct agentx *ax = axs->axs_ax;
	struct ax_pdu_response *resp = &(pdu->ap_payload.ap_response);
	int axfree = ax->ax_free;

#ifdef AX_DEBUG
	if (axi->axi_cstate != AX_CSTATE_WAITCLOSE)
		agentx_log_axc_fatalx(axc, "%s: unexpected indexdeallocate",
		    __func__);
#endif

	if (pdu->ap_payload.ap_response.ap_error != AX_PDU_ERROR_NOERROR) {
		agentx_log_axc_warnx(axc,
		    "index %s: couldn't deallocate: %s",
		    ax_oid2string(&(axi->axi_vb.avb_oid)),
		    ax_error2string(resp->ap_error));
		agentx_reset(ax);
		return -1;
	}

	if (resp->ap_nvarbind != 1) {
		agentx_log_axc_warnx(axc,
		    "index %s: unexpected number of indices",
		    ax_oid2string(&(axr->axr_oid)));
		agentx_reset(ax);
		return -1;
	}
	if (resp->ap_varbindlist[0].avb_type != axi->axi_vb.avb_type) {
		agentx_log_axc_warnx(axc, "index %s: unexpected index type",
		    ax_oid2string(&(axr->axr_oid)));
		agentx_reset(ax);
		return -1;
	}
	if (ax_oid_cmp(&(resp->ap_varbindlist[0].avb_oid),
	    &(axi->axi_vb.avb_oid)) != 0) {
		agentx_log_axc_warnx(axc, "index %s: unexpected oid",
		    ax_oid2string(&(axr->axr_oid)));
		agentx_reset(ax);
		return -1;
	}
	switch (axi->axi_vb.avb_type) {
	case AX_DATA_TYPE_INTEGER:
		if (axi->axi_vb.avb_data.avb_int32 !=
		    resp->ap_varbindlist[0].avb_data.avb_int32) {
			agentx_log_axc_warnx(axc,
			    "index %s: unexpected index value",
			    ax_oid2string(&(axr->axr_oid)));
			agentx_reset(ax);
			return -1;
		}
		break;
	default:
		agentx_log_axc_fatalx(axc, "%s: Unsupported index type",
		    __func__);
	}

	axi->axi_cstate = AX_CSTATE_CLOSE;
	ax->ax_free = 1;

	agentx_log_axc_info(axc, "index %s: deallocated",
	    ax_oid2string(&(axi->axi_vb.avb_oid)));

	if (axr->axr_cstate == AX_CSTATE_OPEN &&
	    axi->axi_dstate == AX_DSTATE_OPEN)
		agentx_index_start(axi);

	if (!axfree)
		agentx_free_finalize(ax);
	return 0;
}

struct agentx_object *
agentx_object(struct agentx_region *axr, uint32_t oid[], size_t oidlen,
    struct agentx_index *axi[], size_t axilen, int implied,
    void (*get)(struct agentx_varbind *))
{
	struct agentx_object *axo, **taxo, axo_search;
	struct agentx_index *laxi;
	const char *errstr;
	int ready = 1;
	size_t i, j;

	if (axr->axr_dstate == AX_DSTATE_CLOSE)
		agentx_log_axc_fatalx(axr->axr_axc, "%s: use after free",
		    __func__);
	if (axilen > AGENTX_OID_INDEX_MAX_LEN) {
#ifdef AX_DEBUG
		agentx_log_axc_fatalx(axr->axr_axc, "%s: indexlen > %d",
		    __func__, AGENTX_OID_INDEX_MAX_LEN);
#else
		agentx_log_axc_warnx(axr->axr_axc, "%s: indexlen > %d",
		    __func__, AGENTX_OID_INDEX_MAX_LEN);
		errno = EINVAL;
		return NULL;
#endif
	}

	if (agentx_oidfill(&(axo_search.axo_oid), oid, oidlen, &errstr) == -1) {
#ifdef AX_DEBUG
		agentx_log_axc_fatalx(axr->axr_axc, "%s: %s", __func__, errstr);
#else
		agentx_log_axc_warnx(axr->axr_axc, "%s: %s", __func__, errstr);
		return NULL;
#endif
	}

	do {
		if (RB_FIND(axc_objects, &(axr->axr_axc->axc_objects),
		    &axo_search) != NULL) {
#ifdef AX_DEBUG
			agentx_log_axc_fatalx(axr->axr_axc, "%s: invalid "
			    "parent child object relationship", __func__);
#else
			agentx_log_axc_warnx(axr->axr_axc, "%s: invalid "
			    "parent child object relationship", __func__);
			errno = EINVAL;
			return NULL;
#endif
		}
		axo_search.axo_oid.aoi_idlen--;
	} while (axo_search.axo_oid.aoi_idlen > 0);
	axo_search.axo_oid.aoi_idlen = oidlen;
	axo = RB_NFIND(axc_objects, &(axr->axr_axc->axc_objects), &axo_search);
	if (axo != NULL &&
	    ax_oid_cmp(&(axo->axo_oid), &(axo_search.axo_oid)) == 2) {
#ifdef AX_DEBUG
		agentx_log_axc_fatalx(axr->axr_axc, "%s: invalid parent "
		    "child object relationship", __func__);
#else
		agentx_log_axc_warnx(axr->axr_axc, "%s: invalid parent "
		    "child object relationship", __func__);
		errno = EINVAL;
		return NULL;
#endif
	}
	if (implied == 1) {
		laxi = axi[axilen - 1];
		if (laxi->axi_vb.avb_type == AX_DATA_TYPE_OCTETSTRING) {
			if (laxi->axi_vb.avb_data.avb_ostring.aos_slen != 0) {
#ifdef AX_DEBUG
				agentx_log_axc_fatalx(axr->axr_axc,
				    "%s: implied can only be used on strings "
				    "of dynamic length", __func__);
#else
				agentx_log_axc_warnx(axr->axr_axc,
				    "%s: implied can only be used on strings "
				    "of dynamic length", __func__);
				errno = EINVAL;
				return NULL;
#endif
			}
		} else if (laxi->axi_vb.avb_type == AX_DATA_TYPE_OID) {
			if (laxi->axi_vb.avb_data.avb_oid.aoi_idlen != 0) {
#ifdef AX_DEBUG
				agentx_log_axc_fatalx(axr->axr_axc,
				    "%s: implied can only be used on oids of "
				    "dynamic length", __func__);
#else
				agentx_log_axc_warnx(axr->axr_axc,
				    "%s: implied can only be used on oids of "
				    "dynamic length", __func__);
				errno = EINVAL;
				return NULL;
#endif
			}
		} else {
#ifdef AX_DEBUG
			agentx_log_axc_fatalx(axr->axr_axc, "%s: implied "
			    "can only be set on oid and string indices",
			    __func__);
#else
			agentx_log_axc_warnx(axr->axr_axc, "%s: implied can "
			    "only be set on oid and string indices", __func__);
			errno = EINVAL;
			return NULL;
#endif
		}
	}

	ready = axr->axr_cstate == AX_CSTATE_OPEN;
	if ((axo = calloc(1, sizeof(*axo))) == NULL)
		return NULL;
	axo->axo_axr = axr;
	bcopy(&(axo_search.axo_oid), &(axo->axo_oid), sizeof(axo->axo_oid));
	for (i = 0; i < axilen; i++) {
		axo->axo_index[i] = axi[i];
		if (axi[i]->axi_objectlen == axi[i]->axi_objectsize) {
			taxo = recallocarray(axi[i]->axi_object,
			    axi[i]->axi_objectlen, axi[i]->axi_objectlen + 1,
			    sizeof(*axi[i]->axi_object));
			if (taxo == NULL) {
				free(axo);
				return NULL;
			}
			axi[i]->axi_object = taxo;
			axi[i]->axi_objectsize = axi[i]->axi_objectlen + 1;
		}
		for (j = 0; j < axi[i]->axi_objectlen; j++) {
			if (ax_oid_cmp(&(axo->axo_oid),
			    &(axi[i]->axi_object[j]->axo_oid)) < 0) {
				memmove(&(axi[i]->axi_object[j + 1]),
				    &(axi[i]->axi_object[j]),
				    sizeof(*(axi[i]->axi_object)) *
				    (axi[i]->axi_objectlen - j));
				break;
			}
		}
		axi[i]->axi_object[j] = axo;
		axi[i]->axi_objectlen++;
		if (axi[i]->axi_cstate != AX_CSTATE_OPEN)
			ready = 0;
	}
	axo->axo_indexlen = axilen;
	axo->axo_implied = implied;
	axo->axo_timeout = 0;
	axo->axo_lock = 0;
	axo->axo_get = get;
	axo->axo_cstate = AX_CSTATE_CLOSE;
	axo->axo_dstate = AX_DSTATE_OPEN;

	TAILQ_INSERT_TAIL(&(axr->axr_objects), axo, axo_axr_objects);
	RB_INSERT(axc_objects, &(axr->axr_axc->axc_objects), axo);

	if (ready)
		agentx_object_start(axo);

	return axo;
}

static int
agentx_object_start(struct agentx_object *axo)
{
	struct agentx_region *axr = axo->axo_axr;
	struct agentx_context *axc = axr->axr_axc;
	struct agentx_session *axs = axc->axc_axs;
	struct agentx *ax = axs->axs_ax;
	struct ax_oid oid;
	char oids[1024];
	size_t i;
	int needregister = 0;
	uint32_t packetid;
	uint8_t flags = AX_PDU_FLAG_INSTANCE_REGISTRATION;

#ifdef AX_DEBUG
	if (axr->axr_cstate != AX_CSTATE_OPEN ||
	    axo->axo_cstate != AX_CSTATE_CLOSE ||
	    axo->axo_dstate != AX_DSTATE_OPEN)
		agentx_log_axc_fatalx(axc,
		    "%s: unexpected object registration", __func__);
#endif

	if (axo->axo_timeout != 0)
		needregister = 1;
	for (i = 0; i < axo->axo_indexlen; i++) {
		if (axo->axo_index[i]->axi_cstate != AX_CSTATE_OPEN)
			return 0;
		if (axo->axo_index[i]->axi_type != AXI_TYPE_DYNAMIC)
			needregister = 1;
	}
	if (!needregister) {
		axo->axo_cstate = AX_CSTATE_WAITOPEN;
		agentx_object_finalize(NULL, axo);
		return 0;
	}

	bcopy(&(axo->axo_oid), &(oid), sizeof(oid));
	for (i = 0; i < axo->axo_indexlen; i++) {
		if (axo->axo_index[i]->axi_type == AXI_TYPE_DYNAMIC) {
			flags = 0;
			break;
		}
#ifdef AX_DEBUG
		if (axo->axo_index[i]->axi_vb.avb_type !=
		    AX_DATA_TYPE_INTEGER)
			agentx_log_axc_fatalx(axc,
			    "%s: Unsupported allocated index type", __func__);
#endif
		oid.aoi_id[oid.aoi_idlen++] =
		    axo->axo_index[i]->axi_vb.avb_data.avb_int32;
	}
	packetid = ax_register(ax->ax_ax, flags, axs->axs_id,
	    AGENTX_CONTEXT_CTX(axc), axo->axo_timeout,
	    AX_PRIORITY_DEFAULT, 0, &oid, 0);
	if (packetid == 0) {
		agentx_log_axc_warn(axc, "couldn't generate %s",
		    ax_pdutype2string(AX_PDU_TYPE_REGISTER));
		agentx_reset(ax);
		return -1;
	}
	strlcpy(oids, ax_oid2string(&(axo->axo_oid)), sizeof(oids));
	agentx_log_axc_info(axc, "object %s (%s %s): opening",
	    oids, flags ? "instance" : "region", ax_oid2string(&(oid)));
	axo->axo_cstate = AX_CSTATE_WAITOPEN;
	return agentx_request(ax, packetid, agentx_object_finalize, axo);
}

static int
agentx_object_finalize(struct ax_pdu *pdu, void *cookie)
{
	struct agentx_object *axo = cookie;
	struct agentx_context *axc = axo->axo_axr->axr_axc;
	struct ax_oid oid;
	char oids[1024];
	size_t i;
	uint8_t flags = 1;

#ifdef AX_DEBUG
	if (axo->axo_cstate != AX_CSTATE_WAITOPEN)
		agentx_log_axc_fatalx(axc, "%s: not expecting object open",
		    __func__);
#endif

	if (pdu == NULL) {
		axo->axo_cstate = AX_CSTATE_OPEN;
		return 0;
	}

	bcopy(&(axo->axo_oid), &oid, sizeof(oid));
	for (i = 0; i < axo->axo_indexlen; i++) {
		if (axo->axo_index[i]->axi_type == AXI_TYPE_DYNAMIC) {
			flags = 0;
			break;
		}
#ifdef AX_DEBUG
		if (axo->axo_index[i]->axi_vb.avb_type !=
		    AX_DATA_TYPE_INTEGER)
			agentx_log_axc_fatalx(axc,
			    "%s: Unsupported allocated index type", __func__);
#endif

		oid.aoi_id[oid.aoi_idlen++] =
		    axo->axo_index[i]->axi_vb.avb_data.avb_int32;
	}
	strlcpy(oids, ax_oid2string(&(axo->axo_oid)), sizeof(oids));

	/*
	 * We should only be here for table objects with registered indices.
	 * If we fail here something is misconfigured and the admin should fix
	 * it.
	 */
	if (pdu->ap_payload.ap_response.ap_error != AX_PDU_ERROR_NOERROR) {
		axo->axo_cstate = AX_CSTATE_CLOSE;
		agentx_log_axc_info(axc, "object %s (%s %s): %s",
		    oids, flags ? "instance" : "region", ax_oid2string(&oid),
		    ax_error2string(pdu->ap_payload.ap_response.ap_error));
		return 0;
	}
	axo->axo_cstate = AX_CSTATE_OPEN;
	agentx_log_axc_info(axc, "object %s (%s %s): open", oids,
	    flags ? "instance" : "region", ax_oid2string(&oid));

	if (axo->axo_dstate == AX_DSTATE_CLOSE)
		return agentx_object_close(axo);

	return 0;
}

static int
agentx_object_lock(struct agentx_object *axo)
{
	if (axo->axo_lock == UINT32_MAX) {
		agentx_log_axc_warnx(axo->axo_axr->axr_axc,
		    "%s: axo_lock == %u", __func__, UINT32_MAX);
		return -1;
	}
	axo->axo_lock++;
	return 0;
}

static void
agentx_object_unlock(struct agentx_object *axo)
{
	struct agentx *ax = axo->axo_axr->axr_axc->axc_axs->axs_ax;

#ifdef AX_DEBUG
	if (axo->axo_lock == 0)
		agentx_log_axc_fatalx(axo->axo_axr->axr_axc,
		    "%s: axo_lock == 0", __func__);
#endif
	axo->axo_lock--;
	if (axo->axo_lock == 0) {
		if (!ax->ax_free)
			agentx_free_finalize(ax);
	}
}

static int
agentx_object_close(struct agentx_object *axo)
{
	struct agentx_context *axc = axo->axo_axr->axr_axc;
	struct agentx_session *axs = axc->axc_axs;
	struct agentx *ax = axs->axs_ax;
	struct ax_oid oid;
	char oids[1024];
	size_t i;
	int needclose = 0;
	uint32_t packetid;
	uint8_t flags = 1;

#ifdef AX_DEBUG
	if (axo->axo_cstate != AX_CSTATE_OPEN)
		agentx_log_axc_fatalx(axc, "%s: unexpected object close",
		    __func__);
#endif

	for (i = 0; i < axo->axo_indexlen; i++) {
#ifdef AX_DEBUG
		if (axo->axo_index[i]->axi_cstate != AX_CSTATE_OPEN)
			agentx_log_axc_fatalx(axc,
			    "%s: Object open while index closed", __func__);
#endif
		if (axo->axo_index[i]->axi_type != AXI_TYPE_DYNAMIC)
			needclose = 1;
	}
	axo->axo_cstate = AX_CSTATE_WAITCLOSE;
	if (axs->axs_cstate == AX_CSTATE_WAITCLOSE)
		return 0;
	if (!needclose) {
		agentx_object_close_finalize(NULL, axo);
		return 0;
	}

	bcopy(&(axo->axo_oid), &(oid), sizeof(oid));
	for (i = 0; i < axo->axo_indexlen; i++) {
		if (axo->axo_index[i]->axi_type == AXI_TYPE_DYNAMIC) {
			flags = 0;
			break;
		}
#ifdef AX_DEBUG
		if (axo->axo_index[i]->axi_vb.avb_type !=
		    AX_DATA_TYPE_INTEGER)
			agentx_log_axc_fatalx(axc,
			    "%s: Unsupported allocated index type", __func__);
#endif
		oid.aoi_id[oid.aoi_idlen++] =
		    axo->axo_index[i]->axi_vb.avb_data.avb_int32;
	}
	packetid = ax_unregister(ax->ax_ax, axs->axs_id,
	    AGENTX_CONTEXT_CTX(axc), AX_PRIORITY_DEFAULT, 0, &oid, 0);
	if (packetid == 0) {
		agentx_log_axc_warn(axc, "couldn't generate %s",
		    ax_pdutype2string(AX_PDU_TYPE_UNREGISTER));
		agentx_reset(ax);
		return -1;
	}
	strlcpy(oids, ax_oid2string(&(axo->axo_oid)), sizeof(oids));
	agentx_log_axc_info(axc, "object %s (%s %s): closing",
	    oids, flags ? "instance" : "region", ax_oid2string(&(oid)));
	return agentx_request(ax, packetid, agentx_object_close_finalize,
	    axo);
}

static int
agentx_object_close_finalize(struct ax_pdu *pdu, void *cookie)
{
	struct agentx_object *axo = cookie;
	struct agentx_region *axr = axo->axo_axr;
	struct agentx_context *axc = axr->axr_axc;
	struct agentx_session *axs = axc->axc_axs;
	struct agentx *ax = axs->axs_ax;
	struct ax_oid oid;
	char oids[1024];
	uint8_t flags = 1;
	size_t i;
	int axfree = ax->ax_free;

#ifdef AX_DEBUG
	if (axo->axo_cstate != AX_CSTATE_WAITCLOSE)
		agentx_log_axc_fatalx(axc,
		    "%s: unexpected object unregister", __func__);
#endif

	if (pdu != NULL) {
		bcopy(&(axo->axo_oid), &(oid), sizeof(oid));
		for (i = 0; i < axo->axo_indexlen; i++) {
			if (axo->axo_index[i]->axi_type == AXI_TYPE_DYNAMIC) {
				flags = 0;
				break;
			}
#ifdef AX_DEBUG
			if (axo->axo_index[i]->axi_vb.avb_type !=
			    AX_DATA_TYPE_INTEGER)
				agentx_log_axc_fatalx(axc,
				    "%s: Unsupported allocated index type",
				    __func__);
#endif
			oid.aoi_id[oid.aoi_idlen++] =
			    axo->axo_index[i]->axi_vb.avb_data.avb_int32;
		}
		strlcpy(oids, ax_oid2string(&(axo->axo_oid)), sizeof(oids));
		if (pdu->ap_payload.ap_response.ap_error !=
		    AX_PDU_ERROR_NOERROR) {
			agentx_log_axc_warnx(axc,
			    "closing object %s (%s %s): %s", oids,
			    flags ? "instance" : "region",
			    ax_oid2string(&oid), ax_error2string(
			    pdu->ap_payload.ap_response.ap_error));
			agentx_reset(ax);
			return -1;
		}
		agentx_log_axc_info(axc, "object %s (%s %s): closed", oids,
		    flags ? "instance" : "region", ax_oid2string(&oid));
	}

	ax->ax_free = 1;
	if (axr->axr_cstate == AX_CSTATE_OPEN &&
	    axo->axo_dstate == AX_DSTATE_OPEN)
		agentx_object_start(axo);

	if (!axfree)
		agentx_free_finalize(ax);

	return 0;
}

void
agentx_object_free(struct agentx_object *axo)
{
	struct agentx *ax;
	int axfree;

	if (axo == NULL)
		return;

	ax = axo->axo_axr->axr_axc->axc_axs->axs_ax;
	axfree = ax->ax_free;
	ax->ax_free = 1;

	if (axo->axo_dstate == AX_DSTATE_CLOSE)
		agentx_log_axc_fatalx(axo->axo_axr->axr_axc,
		    "%s: double free", __func__);

	axo->axo_dstate = AX_DSTATE_CLOSE;

	if (axo->axo_cstate == AX_CSTATE_OPEN)
		agentx_object_close(axo);
	if (!axfree)
		agentx_free_finalize(ax);
}

static void
agentx_object_free_finalize(struct agentx_object *axo)
{
#ifdef AX_DEBUG
	struct agentx *ax = axo->axo_axr->axr_axc->axc_axs->axs_ax;
#endif
	size_t i, j;
	int found;

	if (axo->axo_dstate != AX_DSTATE_CLOSE ||
	    axo->axo_cstate != AX_CSTATE_CLOSE ||
	    axo->axo_lock != 0)
		return;

	RB_REMOVE(axc_objects, &(axo->axo_axr->axr_axc->axc_objects), axo);
	TAILQ_REMOVE(&(axo->axo_axr->axr_objects), axo, axo_axr_objects);

	for (i = 0; i < axo->axo_indexlen; i++) {
		found = 0;
		for (j = 0; j < axo->axo_index[i]->axi_objectlen; j++) {
			if (axo->axo_index[i]->axi_object[j] == axo)
				found = 1;
			if (found && j + 1 != axo->axo_index[i]->axi_objectlen)
				axo->axo_index[i]->axi_object[j] =
				    axo->axo_index[i]->axi_object[j + 1];
		}
#ifdef AX_DEBUG
		if (!found)
			agentx_log_axc_fatalx(axo->axo_axr->axr_axc,
			    "%s: object not found in index", __func__);
#endif
		axo->axo_index[i]->axi_objectlen--;
	}

	free(axo);
}

static void
agentx_object_reset(struct agentx_object *axo)
{
	struct agentx *ax = axo->axo_axr->axr_axc->axc_axs->axs_ax;

	axo->axo_cstate = AX_CSTATE_CLOSE;

	if (!ax->ax_free)
		agentx_free_finalize(ax);
}

static int
agentx_object_cmp(struct agentx_object *o1, struct agentx_object *o2)
{
	return ax_oid_cmp(&(o1->axo_oid), &(o2->axo_oid));
}

static int
agentx_object_implied(struct agentx_object *axo,
    struct agentx_index *axi)
{
	size_t i = 0;
	struct ax_varbind *vb;

	for (i = 0; i < axo->axo_indexlen; i++) {
		if (axo->axo_index[i] == axi) {
			vb = &axi->axi_vb;
			if (vb->avb_type == AX_DATA_TYPE_OCTETSTRING &&
			    vb->avb_data.avb_ostring.aos_slen != 0)
				return 1;
			else if (vb->avb_type == AX_DATA_TYPE_OID &&
			    vb->avb_data.avb_oid.aoi_idlen != 0)
				return 1;
			else if (i == axo->axo_indexlen - 1)
				return axo->axo_implied;
			return 0;
		}
	}
#ifdef AX_DEBUG
	agentx_log_axc_fatalx(axo->axo_axr->axr_axc, "%s: unsupported index",
	    __func__);
#endif
	return 0;
}

static void
agentx_get_start(struct agentx_context *axc, struct ax_pdu *pdu)
{
	struct agentx_session *axs = axc->axc_axs;
	struct agentx *ax = axs->axs_ax;
	struct agentx_get *axg, taxg;
	struct ax_pdu_searchrangelist *srl;
	char *logmsg = NULL;
	size_t i, j;
	int fail = 0;

	if ((axg = calloc(1, sizeof(*axg))) == NULL) {
		taxg.axg_sessionid = pdu->ap_header.aph_sessionid;
		taxg.axg_transactionid = pdu->ap_header.aph_transactionid;
		taxg.axg_packetid = pdu->ap_header.aph_packetid;
		taxg.axg_context_default = axc->axc_name_default;
		taxg.axg_fd = axc->axc_axs->axs_ax->ax_fd;
		agentx_log_axg_warn(&taxg, "Couldn't parse request");
		agentx_reset(ax);
		return;
	}

	axg->axg_sessionid = pdu->ap_header.aph_sessionid;
	axg->axg_transactionid = pdu->ap_header.aph_transactionid;
	axg->axg_packetid = pdu->ap_header.aph_packetid;
	axg->axg_context_default = axc->axc_name_default;
	axg->axg_fd = axc->axc_axs->axs_ax->ax_fd;
	if (!axc->axc_name_default) {
		axg->axg_context.aos_string =
		    (unsigned char *)strdup((char *)axc->axc_name.aos_string);
		if (axg->axg_context.aos_string == NULL) {
			agentx_log_axg_warn(axg, "Couldn't parse request");
			free(axg);
			agentx_reset(ax);
			return;
		}
	}
	axg->axg_context.aos_slen = axc->axc_name.aos_slen;
	axg->axg_type = pdu->ap_header.aph_type;
	axg->axg_axc = axc;
	TAILQ_INSERT_TAIL(&(ax->ax_getreqs), axg, axg_ax_getreqs);
	if (axg->axg_type == AX_PDU_TYPE_GET ||
	    axg->axg_type == AX_PDU_TYPE_GETNEXT) {
		srl = &(pdu->ap_payload.ap_srl);
		axg->axg_nvarbind = srl->ap_nsr;
	} else {
		axg->axg_nonrep = pdu->ap_payload.ap_getbulk.ap_nonrep;
		axg->axg_maxrep = pdu->ap_payload.ap_getbulk.ap_maxrep;
		srl = &(pdu->ap_payload.ap_getbulk.ap_srl);
		axg->axg_nvarbind = ((srl->ap_nsr - axg->axg_nonrep) *
		    axg->axg_maxrep) + axg->axg_nonrep;
	}

	if ((axg->axg_varbind = calloc(axg->axg_nvarbind,
	    sizeof(*(axg->axg_varbind)))) == NULL) {
		agentx_log_axg_warn(axg, "Couldn't parse request");
		agentx_get_free(axg);
		agentx_reset(ax);
		return;
	}

	/* XXX net-snmp doesn't use getbulk, so untested */
	/* Two loops: varbind after needs to be initialized */
	for (i = 0; i < srl->ap_nsr; i++) {
		if (i < axg->axg_nonrep ||
		    axg->axg_type != AX_PDU_TYPE_GETBULK)
			j = i;
		else if (axg->axg_maxrep == 0)
			break;
		else
			j = (axg->axg_maxrep * i) + axg->axg_nonrep;
		bcopy(&(srl->ap_sr[i].asr_start),
		    &(axg->axg_varbind[j].axv_vb.avb_oid),
		    sizeof(srl->ap_sr[i].asr_start));
		bcopy(&(srl->ap_sr[i].asr_start),
		    &(axg->axg_varbind[j].axv_start),
		    sizeof(srl->ap_sr[i].asr_start));
		bcopy(&(srl->ap_sr[i].asr_stop),
		    &(axg->axg_varbind[j].axv_end),
		    sizeof(srl->ap_sr[i].asr_stop));
		axg->axg_varbind[j].axv_initialized = 1;
		axg->axg_varbind[j].axv_axg = axg;
		axg->axg_varbind[j].axv_include =
		    srl->ap_sr[i].asr_start.aoi_include;
		if (j == 0)
			fail |= agentx_strcat(&logmsg, " {");
		else
			fail |= agentx_strcat(&logmsg, ",{");
		fail |= agentx_strcat(&logmsg,
		    ax_oid2string(&(srl->ap_sr[i].asr_start)));
		if (srl->ap_sr[i].asr_start.aoi_include)
			fail |= agentx_strcat(&logmsg, " (inclusive)");
		if (srl->ap_sr[i].asr_stop.aoi_idlen != 0) {
			fail |= agentx_strcat(&logmsg, " - ");
			fail |= agentx_strcat(&logmsg,
			    ax_oid2string(&(srl->ap_sr[i].asr_stop)));
		}
		fail |= agentx_strcat(&logmsg, "}");
		if (fail) {
			agentx_log_axg_warn(axg, "Couldn't parse request");
			free(logmsg);
			agentx_get_free(axg);
			agentx_reset(ax);
			return;
		}
	}

	agentx_log_axg_debug(axg, "%s:%s",
	    ax_pdutype2string(axg->axg_type), logmsg);
	free(logmsg);

	for (i = 0; i < srl->ap_nsr; i++) {
		if (i < axg->axg_nonrep ||
		    axg->axg_type != AX_PDU_TYPE_GETBULK)
			j = i;
		else if (axg->axg_maxrep == 0)
			break;
		else
			j = (axg->axg_maxrep * i) + axg->axg_nonrep;
		agentx_varbind_start(&(axg->axg_varbind[j]));
	}
}

static void
agentx_get_finalize(struct agentx_get *axg)
{
	struct agentx_context *axc = axg->axg_axc;
	struct agentx_session *axs;
	struct agentx *ax;
	size_t i, j, nvarbind = 0;
	uint16_t error = 0, index = 0;
	struct ax_varbind *vbl;
	char *logmsg = NULL;
	int fail = 0;

	for (i = 0; i < axg->axg_nvarbind; i++) {
		if (axg->axg_varbind[i].axv_initialized) {
			if (axg->axg_varbind[i].axv_vb.avb_type == 0)
				return;
			nvarbind++;
		}
	}

	if (axc == NULL) {
		agentx_get_free(axg);
		return;
	}

	axs = axc->axc_axs;
	ax = axs->axs_ax;

	if ((vbl = calloc(nvarbind, sizeof(*vbl))) == NULL) {
		agentx_log_axg_warn(axg, "Couldn't parse request");
		agentx_get_free(axg);
		agentx_reset(ax);
		return;
	}
	for (i = 0, j = 0; i < axg->axg_nvarbind; i++) {
		if (axg->axg_varbind[i].axv_initialized) {
			memcpy(&(vbl[j]), &(axg->axg_varbind[i].axv_vb),
			    sizeof(*vbl));
			if (error == 0 && axg->axg_varbind[i].axv_error !=
			    AX_PDU_ERROR_NOERROR) {
				error = axg->axg_varbind[i].axv_error;
				index = j + 1;
			}
			if (j == 0)
				fail |= agentx_strcat(&logmsg, " {");
			else
				fail |= agentx_strcat(&logmsg, ",{");
			fail |= agentx_strcat(&logmsg,
			    ax_varbind2string(&(vbl[j])));
			if (axg->axg_varbind[i].axv_error !=
			    AX_PDU_ERROR_NOERROR) {
				fail |= agentx_strcat(&logmsg, "(");
				fail |= agentx_strcat(&logmsg,
				    ax_error2string(
				    axg->axg_varbind[i].axv_error));
				fail |= agentx_strcat(&logmsg, ")");
			}
			fail |= agentx_strcat(&logmsg, "}");
			if (fail) {
				agentx_log_axg_warn(axg,
				    "Couldn't parse request");
				free(logmsg);
				agentx_get_free(axg);
				return;
			}
			j++;
		}
	}
	agentx_log_axg_debug(axg, "response:%s", logmsg);
	free(logmsg);

	if (ax_response(ax->ax_ax, axs->axs_id, axg->axg_transactionid,
	    axg->axg_packetid, 0, error, index, vbl, nvarbind) == -1) {
		agentx_log_axg_warn(axg, "Couldn't parse request");
		agentx_reset(ax);
	} else
		agentx_wantwrite(ax, ax->ax_fd);
	free(vbl);
	agentx_get_free(axg);
}

void
agentx_get_free(struct agentx_get *axg)
{
	struct agentx_varbind *axv;
	struct agentx_object *axo;
	struct agentx *ax;
	struct agentx_varbind_index *index;
	size_t i, j;

	if (axg->axg_axc != NULL) {
		ax = axg->axg_axc->axc_axs->axs_ax;
		TAILQ_REMOVE(&(ax->ax_getreqs), axg, axg_ax_getreqs);
	}

	for (i = 0; i < axg->axg_nvarbind; i++) {
		axv = &(axg->axg_varbind[i]);
		for (j = 0; axv->axv_axo != NULL &&
		    j < axv->axv_axo->axo_indexlen; j++) {
			axo = axv->axv_axo;
			index = &(axv->axv_index[j]);
			if (axo->axo_index[j]->axi_vb.avb_type ==
			    AX_DATA_TYPE_OCTETSTRING ||
			    axo->axo_index[j]->axi_vb.avb_type ==
			    AX_DATA_TYPE_IPADDRESS)
				free(index->axv_idata.avb_ostring.aos_string);
		}
		ax_varbind_free(&(axg->axg_varbind[i].axv_vb));
	}

	free(axg->axg_context.aos_string);
	free(axg->axg_varbind);
	free(axg);
}

static void
agentx_varbind_start(struct agentx_varbind *axv)
{
	struct agentx_get *axg = axv->axv_axg;
	struct agentx_context *axc = axg->axg_axc;
	struct agentx_object *axo, axo_search;
	struct agentx_varbind_index *index;
	struct ax_oid *oid;
	union ax_data *data;
	struct in_addr *ipaddress;
	unsigned char *ipbytes;
	size_t i, j, k;
	int overflow = 0, dynamic;

#ifdef AX_DEBUG
	if (!axv->axv_initialized)
		agentx_log_axg_fatalx(axv->axv_axg,
		    "%s: axv_initialized not set", __func__);
#endif

	if (axc == NULL) {
		agentx_varbind_error_type(axv, AX_PDU_ERROR_PROCESSINGERROR, 1);
		return;
	}

	bcopy(&(axv->axv_vb.avb_oid), &(axo_search.axo_oid),
	    sizeof(axo_search.axo_oid));

	do {
		axo = RB_FIND(axc_objects, &(axc->axc_objects), &axo_search);
		if (axo_search.axo_oid.aoi_idlen > 0)
			axo_search.axo_oid.aoi_idlen--;
	} while (axo == NULL && axo_search.axo_oid.aoi_idlen > 0);
	if (axo == NULL || axo->axo_cstate != AX_CSTATE_OPEN) {
		axv->axv_include = 1;
		if (axv->axv_axg->axg_type == AX_PDU_TYPE_GET) {
			agentx_varbind_nosuchobject(axv);
			return;
		}
		bcopy(&(axv->axv_vb.avb_oid), &(axo_search.axo_oid),
		    sizeof(axo_search.axo_oid));
		axo = RB_NFIND(axc_objects, &(axc->axc_objects), &axo_search);
getnext:
		while (axo != NULL && axo->axo_cstate != AX_CSTATE_OPEN)
			axo = RB_NEXT(axc_objects, &(axc->axc_objects), axo);
		if (axo == NULL ||
		    ax_oid_cmp(&(axo->axo_oid), &(axv->axv_end)) >= 0) {
			agentx_varbind_endofmibview(axv);
			return;
		}
		bcopy(&(axo->axo_oid), &(axv->axv_vb.avb_oid),
		    sizeof(axo->axo_oid));
	}
	axv->axv_axo = axo;
	axv->axv_indexlen = axo->axo_indexlen;
	if (agentx_object_lock(axo) == -1) {
		agentx_varbind_error_type(axv,
		    AX_PDU_ERROR_PROCESSINGERROR, 1);
		return;
	}

	oid = &(axv->axv_vb.avb_oid);
	if (axo->axo_indexlen == 0) {
		if (axv->axv_axg->axg_type == AX_PDU_TYPE_GET) {
			if (oid->aoi_idlen != axo->axo_oid.aoi_idlen + 1 ||
			    oid->aoi_id[oid->aoi_idlen - 1] != 0) {
				agentx_varbind_nosuchinstance(axv);
				return;
			}
		} else {
			if (oid->aoi_idlen == axo->axo_oid.aoi_idlen) {
				oid->aoi_id[oid->aoi_idlen++] = 0;
				axv->axv_include = 1;
			} else {
				axv->axv_axo = NULL;
				agentx_object_unlock(axo);
				axo = RB_NEXT(axc_objects, &(axc->axc_objects),
				    axo);
				goto getnext;
			}
		}
		j = oid->aoi_idlen;
	} else
		j = axo->axo_oid.aoi_idlen;
/*
 * We can't trust what the client gives us, so sometimes we need to map it to
 * index type.
 * - AX_PDU_TYPE_GET: we always return AX_DATA_TYPE_NOSUCHINSTANCE
 * - AX_PDU_TYPE_GETNEXT:
 *   - Missing OID digits to match indices or !dynamic indices
 *     (AX_DATA_TYPE_INTEGER) underflows will result in the following indices to
 *     be NUL-initialized and the request type will be set to
 *     AGENTX_REQUEST_TYPE_GETNEXTINCLUSIVE
 *   - An overflow can happen on AX_DATA_TYPE_OCTETSTRING and
 *     AX_DATA_TYPE_IPADDRESS data, and AX_DATA_TYPE_OCTETSTRING and
 *     AX_DATA_TYPE_OID length. This results in request type being set to
 *     AGENTX_REQUEST_TYPE_GETNEXT and will set the index to its maximum
 *     value:
 *     - AX_DATA_TYPE_INTEGER: UINT32_MAX
 *     - AX_DATA_TYPE_OCTETSTRING: aos_slen = UINT32_MAX and
 *       aos_string = NULL
 *     - AX_DATA_TYPE_OID: aoi_idlen = UINT32_MAX and aoi_id[x] = UINT32_MAX
 *     - AX_DATA_TYPE_IPADDRESS: 255.255.255.255
 */
	for (dynamic = 0, i = 0; i < axo->axo_indexlen; i++, j++) {
		index = &(axv->axv_index[i]);
		index->axv_axi = axo->axo_index[i];
		data = &(index->axv_idata);
		if (axo->axo_index[i]->axi_type == AXI_TYPE_DYNAMIC)
			dynamic = 1;
		switch (axo->axo_index[i]->axi_vb.avb_type) {
		case AX_DATA_TYPE_INTEGER:
			if (index->axv_axi->axi_type != AXI_TYPE_DYNAMIC) {
				index->axv_idata.avb_int32 =
				    index->axv_axi->axi_vb.avb_data.avb_int32;
				if (overflow == 0) {
					if ((uint32_t)index->axv_idata.avb_int32 >
					    oid->aoi_id[j])
						overflow = -1;
					else if ((uint32_t)index->axv_idata.avb_int32 <
					    oid->aoi_id[j])
						overflow = 1;
				}
			} else if (overflow == 1)
				index->axv_idata.avb_int32 = INT32_MAX;
			else if (j >= oid->aoi_idlen || overflow == -1)
				index->axv_idata.avb_int32 = 0;
			else {
				if (oid->aoi_id[j] > INT32_MAX) {
					index->axv_idata.avb_int32 = INT32_MAX;
					overflow = 1;
				} else
					index->axv_idata.avb_int32 =
					    oid->aoi_id[j];
			}
			break;
		case AX_DATA_TYPE_OCTETSTRING:
			if (overflow == 1) {
				data->avb_ostring.aos_slen = UINT32_MAX;
				data->avb_ostring.aos_string = NULL;
				continue;
			} else if (j >= oid->aoi_idlen || overflow == -1) {
				data->avb_ostring.aos_slen = 0;
				data->avb_ostring.aos_string = NULL;
				continue;
			}
			if (agentx_object_implied(axo, index->axv_axi))
				data->avb_ostring.aos_slen = oid->aoi_idlen - j;
			else {
				data->avb_ostring.aos_slen = oid->aoi_id[j++];
				if (data->avb_ostring.aos_slen >=
				    AGENTX_OID_MAX_LEN - j) {
					data->avb_ostring.aos_slen = UINT32_MAX;
					overflow = 1;
				}
			}
			if (data->avb_ostring.aos_slen == UINT32_MAX ||
			    data->avb_ostring.aos_slen == 0) {
				data->avb_ostring.aos_string = NULL;
				continue;
			}
			data->avb_ostring.aos_string =
			    malloc(data->avb_ostring.aos_slen + 1);
			if (data->avb_ostring.aos_string == NULL) {
				agentx_log_axg_warn(axg,
				    "Failed to bind string index");
				agentx_varbind_error_type(axv,
				    AX_PDU_ERROR_PROCESSINGERROR, 1);
				return;
			}
			for (k = 0; k < data->avb_ostring.aos_slen; k++, j++) {
				if (j < oid->aoi_idlen && oid->aoi_id[j] > 0xff)
					overflow = 1;
				if (overflow == 1)
					data->avb_ostring.aos_string[k] = 0xff;
				else if (j >= oid->aoi_idlen || overflow == -1)
					data->avb_ostring.aos_string[k] = '\0';
				else
					data->avb_ostring.aos_string[k] =
					    oid->aoi_id[j];
			}
			data->avb_ostring.aos_string[k] = '\0';
			j--;
			break;
		case AX_DATA_TYPE_OID:
			if (overflow == 1) {
				data->avb_oid.aoi_idlen = UINT32_MAX;
				continue;
			} else if (j >= oid->aoi_idlen || overflow == -1) {
				data->avb_oid.aoi_idlen = 0;
				continue;
			}
			if (agentx_object_implied(axo, index->axv_axi))
				data->avb_oid.aoi_idlen = oid->aoi_idlen - j;
			else {
				data->avb_oid.aoi_idlen = oid->aoi_id[j++];
				if (data->avb_oid.aoi_idlen >=
				    AGENTX_OID_MAX_LEN - j) {
					data->avb_oid.aoi_idlen = UINT32_MAX;
					overflow = 1;
				}
			}
			if (data->avb_oid.aoi_idlen == UINT32_MAX ||
			    data->avb_oid.aoi_idlen == 0)
				continue;
			for (k = 0; k < data->avb_oid.aoi_idlen; k++, j++) {
				if (overflow == 1)
					data->avb_oid.aoi_id[k] = UINT32_MAX;
				else if (j >= oid->aoi_idlen || overflow == -1)
					data->avb_oid.aoi_id[k] = 0;
				else
					data->avb_oid.aoi_id[k] =
					    oid->aoi_id[j];
			}
			j--;
			break;
		case AX_DATA_TYPE_IPADDRESS:
			ipaddress = malloc(sizeof(*ipaddress));
			if (ipaddress == NULL) {
				agentx_log_axg_warn(axg,
				    "Failed to bind ipaddress index");
				agentx_varbind_error_type(axv,
				    AX_PDU_ERROR_PROCESSINGERROR, 1);
				return;
			}
			ipbytes = (unsigned char *)ipaddress;
			for (k = 0; k < 4; k++, j++) {
				if (j < oid->aoi_idlen && oid->aoi_id[j] > 255)
					overflow = 1;
				if (overflow == 1)
					ipbytes[k] = 255;
				else if (j >= oid->aoi_idlen || overflow == -1)
					ipbytes[k] = 0;
				else
					ipbytes[k] = oid->aoi_id[j];
			}
			j--;
			data->avb_ostring.aos_slen = sizeof(*ipaddress);
			data->avb_ostring.aos_string =
			    (unsigned char *)ipaddress;
			break;
		default:
#ifdef AX_DEBUG
			agentx_log_axg_fatalx(axg,
			    "%s: unexpected index type", __func__);
#else
			agentx_log_axg_warnx(axg,
			    "%s: unexpected index type", __func__);
			agentx_varbind_error_type(axv,
			    AX_PDU_ERROR_PROCESSINGERROR, 1);
			return;
#endif
		}
	}
	if (axv->axv_axg->axg_type == AX_PDU_TYPE_GET) {
		if (j != oid->aoi_idlen || overflow) {
			agentx_varbind_nosuchinstance(axv);
			return;
		}
	}

	if (overflow == 1) {
		axv->axv_include = 0;
	} else if (overflow == -1) {
		axv->axv_include = 1;
	} else if (j < oid->aoi_idlen)
		axv->axv_include = 0;
	else if (j > oid->aoi_idlen)
		axv->axv_include = 1;
	if (agentx_varbind_request(axv) == AGENTX_REQUEST_TYPE_GETNEXT &&
	    !dynamic) {
		agentx_varbind_endofmibview(axv);
		return;
	}

	axo->axo_get(axv);
}

void
agentx_varbind_integer(struct agentx_varbind *axv, int32_t value)
{
	axv->axv_vb.avb_type = AX_DATA_TYPE_INTEGER;
	axv->axv_vb.avb_data.avb_int32 = value;

	agentx_varbind_finalize(axv);
}

void
agentx_varbind_string(struct agentx_varbind *axv, const char *value)
{
	agentx_varbind_nstring(axv, (const unsigned char *)value,
	    strlen(value));
}

void
agentx_varbind_nstring(struct agentx_varbind *axv,
    const unsigned char *value, size_t slen)
{
	axv->axv_vb.avb_data.avb_ostring.aos_string = malloc(slen);
	if (axv->axv_vb.avb_data.avb_ostring.aos_string == NULL) {
		agentx_log_axg_warn(axv->axv_axg, "Couldn't bind string");
		agentx_varbind_error_type(axv,
		    AX_PDU_ERROR_PROCESSINGERROR, 1);
		return;
	}
	axv->axv_vb.avb_type = AX_DATA_TYPE_OCTETSTRING;
	memcpy(axv->axv_vb.avb_data.avb_ostring.aos_string, value, slen);
	axv->axv_vb.avb_data.avb_ostring.aos_slen = slen;

	agentx_varbind_finalize(axv);
}

void
agentx_varbind_printf(struct agentx_varbind *axv, const char *fmt, ...)
{
	va_list ap;
	int r;

	axv->axv_vb.avb_type = AX_DATA_TYPE_OCTETSTRING;
	va_start(ap, fmt);
	r = vasprintf((char **)&(axv->axv_vb.avb_data.avb_ostring.aos_string),
	    fmt, ap);
	va_end(ap);
	if (r == -1) {
		axv->axv_vb.avb_data.avb_ostring.aos_string = NULL;
		agentx_log_axg_warn(axv->axv_axg, "Couldn't bind string");
		agentx_varbind_error_type(axv, AX_PDU_ERROR_PROCESSINGERROR, 1);
		return;
	}
	axv->axv_vb.avb_data.avb_ostring.aos_slen = r;

	agentx_varbind_finalize(axv);
}

void
agentx_varbind_null(struct agentx_varbind *axv)
{
	axv->axv_vb.avb_type = AX_DATA_TYPE_NULL;

	agentx_varbind_finalize(axv);
}

void
agentx_varbind_oid(struct agentx_varbind *axv, const uint32_t oid[],
    size_t oidlen)
{
	const char *errstr;

	axv->axv_vb.avb_type = AX_DATA_TYPE_OID;

	if (agentx_oidfill(&(axv->axv_vb.avb_data.avb_oid),
	    oid, oidlen, &errstr) == -1) {
#ifdef AX_DEBUG
		agentx_log_axg_fatalx(axv->axv_axg, "%s: %s", __func__, errstr);
#else
		agentx_log_axg_warnx(axv->axv_axg, "%s: %s", __func__, errstr);
		agentx_varbind_error_type(axv, AX_PDU_ERROR_PROCESSINGERROR, 1);
		return;
#endif
	}

	agentx_varbind_finalize(axv);
}

void
agentx_varbind_object(struct agentx_varbind *axv,
    struct agentx_object *axo)
{
	agentx_varbind_oid(axv, axo->axo_oid.aoi_id,
	    axo->axo_oid.aoi_idlen);
}

void
agentx_varbind_index(struct agentx_varbind *axv,
    struct agentx_index *axi)
{
	agentx_varbind_oid(axv, axi->axi_vb.avb_oid.aoi_id,
	    axi->axi_vb.avb_oid.aoi_idlen);
}


void
agentx_varbind_ipaddress(struct agentx_varbind *axv,
    const struct in_addr *value)
{
	axv->axv_vb.avb_type = AX_DATA_TYPE_IPADDRESS;
	axv->axv_vb.avb_data.avb_ostring.aos_string = malloc(4);
	if (axv->axv_vb.avb_data.avb_ostring.aos_string == NULL) {
		agentx_log_axg_warn(axv->axv_axg, "Couldn't bind ipaddress");
		agentx_varbind_error_type(axv,
		    AX_PDU_ERROR_PROCESSINGERROR, 1);
		return;
	}
	memcpy(axv->axv_vb.avb_data.avb_ostring.aos_string, value, 4);
	axv->axv_vb.avb_data.avb_ostring.aos_slen = 4;

	agentx_varbind_finalize(axv);
}

void
agentx_varbind_counter32(struct agentx_varbind *axv, uint32_t value)
{
	axv->axv_vb.avb_type = AX_DATA_TYPE_COUNTER32;
	axv->axv_vb.avb_data.avb_uint32 = value;

	agentx_varbind_finalize(axv);
}

void
agentx_varbind_gauge32(struct agentx_varbind *axv, uint32_t value)
{
	axv->axv_vb.avb_type = AX_DATA_TYPE_GAUGE32;
	axv->axv_vb.avb_data.avb_uint32 = value;

	agentx_varbind_finalize(axv);
}

void
agentx_varbind_unsigned32(struct agentx_varbind *axv, uint32_t value)
{
	agentx_varbind_gauge32(axv, value);
}

void
agentx_varbind_timeticks(struct agentx_varbind *axv, uint32_t value)
{
	axv->axv_vb.avb_type = AX_DATA_TYPE_TIMETICKS;
	axv->axv_vb.avb_data.avb_uint32 = value;

	agentx_varbind_finalize(axv);
}

void
agentx_varbind_opaque(struct agentx_varbind *axv, const char *string,
    size_t strlen)
{
	axv->axv_vb.avb_type = AX_DATA_TYPE_OPAQUE;
	axv->axv_vb.avb_data.avb_ostring.aos_string = malloc(strlen);
	if (axv->axv_vb.avb_data.avb_ostring.aos_string == NULL) {
		agentx_log_axg_warn(axv->axv_axg, "Couldn't bind opaque");
		agentx_varbind_error_type(axv,
		    AX_PDU_ERROR_PROCESSINGERROR, 1);
		return;
	}
	memcpy(axv->axv_vb.avb_data.avb_ostring.aos_string, string, strlen);
	axv->axv_vb.avb_data.avb_ostring.aos_slen = strlen;

	agentx_varbind_finalize(axv);
}

void
agentx_varbind_counter64(struct agentx_varbind *axv, uint64_t value)
{
	axv->axv_vb.avb_type = AX_DATA_TYPE_COUNTER64;
	axv->axv_vb.avb_data.avb_uint64 = value;

	agentx_varbind_finalize(axv);
}

void
agentx_varbind_notfound(struct agentx_varbind *axv)
{
	if (axv->axv_indexlen == 0) {
#ifdef AX_DEBUG
		agentx_log_axg_fatalx(axv->axv_axg, "%s invalid call",
		    __func__);
#else
		agentx_log_axg_warnx(axv->axv_axg, "%s invalid call",
		    __func__);
		agentx_varbind_error_type(axv,
		    AX_PDU_ERROR_GENERR, 1);
#endif
	} else if (axv->axv_axg->axg_type == AX_PDU_TYPE_GET)
		agentx_varbind_nosuchinstance(axv);
	else
		agentx_varbind_endofmibview(axv);
}

void
agentx_varbind_error(struct agentx_varbind *axv)
{
	agentx_varbind_error_type(axv, AX_PDU_ERROR_GENERR, 1);
}

static void
agentx_varbind_error_type(struct agentx_varbind *axv,
    enum ax_pdu_error error, int done)
{
	if (axv->axv_error == AX_PDU_ERROR_NOERROR) {
		axv->axv_error = error;
	}

	if (done) {
		axv->axv_vb.avb_type = AX_DATA_TYPE_NULL;

		agentx_varbind_finalize(axv);
	}
}

static void
agentx_varbind_finalize(struct agentx_varbind *axv)
{
	struct agentx_get *axg = axv->axv_axg;
	struct ax_oid oid;
	union ax_data *data;
	size_t i, j;
	int cmp;

	if (axv->axv_error != AX_PDU_ERROR_NOERROR) {
		bcopy(&(axv->axv_start), &(axv->axv_vb.avb_oid),
		    sizeof(axv->axv_start));
		goto done;
	}
	bcopy(&(axv->axv_axo->axo_oid), &oid, sizeof(oid));
	if (axv->axv_indexlen == 0)
		ax_oid_add(&oid, 0);
	for (i = 0; i < axv->axv_indexlen; i++) {
		data = &(axv->axv_index[i].axv_idata);
		switch (axv->axv_index[i].axv_axi->axi_vb.avb_type) {
		case AX_DATA_TYPE_INTEGER:
			if (ax_oid_add(&oid, data->avb_int32) == -1)
				goto fail;
			break;
		case AX_DATA_TYPE_OCTETSTRING:
			if (!agentx_object_implied(axv->axv_axo,
			    axv->axv_index[i].axv_axi)) {
				if (ax_oid_add(&oid,
				    data->avb_ostring.aos_slen) == -1)
					goto fail;
			}
			for (j = 0; j < data->avb_ostring.aos_slen; j++) {
				if (ax_oid_add(&oid,
				    (uint8_t)data->avb_ostring.aos_string[j]) ==
				    -1)
					goto fail;
			}
			break;
		case AX_DATA_TYPE_OID:
			if (!agentx_object_implied(axv->axv_axo,
			    axv->axv_index[i].axv_axi)) {
				if (ax_oid_add(&oid,
				    data->avb_oid.aoi_idlen) == -1)
					goto fail;
			}
			for (j = 0; j < data->avb_oid.aoi_idlen; j++) {
				if (ax_oid_add(&oid,
				    data->avb_oid.aoi_id[j]) == -1)
					goto fail;
			}
			break;
		case AX_DATA_TYPE_IPADDRESS:
			for (j = 0; j < 4; j++) {
				if (ax_oid_add(&oid,
				    data->avb_ostring.aos_string == NULL ? 0 :
				    (uint8_t)data->avb_ostring.aos_string[j]) ==
				    -1)
					goto fail;
			}
			break;
		default:
#ifdef AX_DEBUG
			agentx_log_axg_fatalx(axg,
			    "%s: unsupported index type", __func__);
#else
			bcopy(&(axv->axv_start), &(axv->axv_vb.avb_oid),
			    sizeof(axv->axv_start));
			axv->axv_error = AX_PDU_ERROR_PROCESSINGERROR;
			agentx_object_unlock(axv->axv_axo);
			agentx_get_finalize(axv->axv_axg);
			return;
#endif
		}
	}
	cmp = ax_oid_cmp(&oid, &(axv->axv_vb.avb_oid));
	switch (agentx_varbind_request(axv)) {
	case AGENTX_REQUEST_TYPE_GET:
		if (cmp != 0) {
#ifdef AX_DEBUG
			agentx_log_axg_fatalx(axg, "index changed");
#else
			agentx_log_axg_warnx(axg, "index changed");
			bcopy(&(axv->axv_start), &(axv->axv_vb.avb_oid),
			    sizeof(axv->axv_start));
			axv->axv_error = AX_PDU_ERROR_GENERR;
			break;
#endif
		}
		break;
	case AGENTX_REQUEST_TYPE_GETNEXT:
		if (cmp <= 0) {
#ifdef AX_DEBUG
			agentx_log_axg_fatalx(axg, "indices not incremented");
#else
			agentx_log_axg_warnx(axg, "indices not incremented");
			bcopy(&(axv->axv_start), &(axv->axv_vb.avb_oid),
			    sizeof(axv->axv_start));
			axv->axv_error = AX_PDU_ERROR_GENERR;
			break;
#endif
		}
		/* FALLTHROUGH */
	case AGENTX_REQUEST_TYPE_GETNEXTINCLUSIVE:
		if (cmp < 0) {
#ifdef AX_DEBUG
			agentx_log_axg_fatalx(axg, "index decremented");
#else
			agentx_log_axg_warnx(axg, "index decremented");
			bcopy(&(axv->axv_start), &(axv->axv_vb.avb_oid),
			    sizeof(axv->axv_start));
			axv->axv_error = AX_PDU_ERROR_GENERR;
			break;
#endif
		}
		if (axv->axv_end.aoi_idlen != 0 &&
		    ax_oid_cmp(&oid, &(axv->axv_end)) >= 0) {
			agentx_varbind_endofmibview(axv);
			return;
		}
		bcopy(&oid, &(axv->axv_vb.avb_oid), sizeof(oid));
	}
done:
	agentx_object_unlock(axv->axv_axo);
	agentx_get_finalize(axv->axv_axg);
	return;

fail:
	agentx_log_axg_warnx(axg, "oid too large");
	bcopy(&(axv->axv_start), &(axv->axv_vb.avb_oid),
	    sizeof(axv->axv_start));
	axv->axv_error = AX_PDU_ERROR_GENERR;
	agentx_object_unlock(axv->axv_axo);
	agentx_get_finalize(axv->axv_axg);
}

static void
agentx_varbind_nosuchobject(struct agentx_varbind *axv)
{
	axv->axv_vb.avb_type = AX_DATA_TYPE_NOSUCHOBJECT;

	if (axv->axv_axo != NULL)
		agentx_object_unlock(axv->axv_axo);
	agentx_get_finalize(axv->axv_axg);
}

static void
agentx_varbind_nosuchinstance(struct agentx_varbind *axv)
{
	axv->axv_vb.avb_type = AX_DATA_TYPE_NOSUCHINSTANCE;

	if (axv->axv_axo != NULL)
		agentx_object_unlock(axv->axv_axo);
	agentx_get_finalize(axv->axv_axg);
}

static void
agentx_varbind_endofmibview(struct agentx_varbind *axv)
{
	struct agentx_object *axo;
	struct ax_varbind *vb;
	struct agentx_varbind_index *index;
	size_t i;

#ifdef AX_DEBUG
	if (axv->axv_axg->axg_type != AX_PDU_TYPE_GETNEXT &&
	    axv->axv_axg->axg_type != AX_PDU_TYPE_GETBULK)
		agentx_log_axg_fatalx(axv->axv_axg,
		    "%s: invalid request type", __func__);
#endif

	if (axv->axv_axo != NULL &&
	    (axo = RB_NEXT(axc_objects, &(axc->axc_objects),
	    axv->axv_axo)) != NULL &&
	    ax_oid_cmp(&(axo->axo_oid), &(axv->axv_end)) < 0) {
		bcopy(&(axo->axo_oid), &(axv->axv_vb.avb_oid),
		    sizeof(axo->axo_oid));
		axv->axv_include = 1;
		for (i = 0; i < axv->axv_indexlen; i++) {
			index = &(axv->axv_index[i]);
			vb = &(index->axv_axi->axi_vb);
			if (vb->avb_type == AX_DATA_TYPE_OCTETSTRING ||
			    vb->avb_type == AX_DATA_TYPE_IPADDRESS)
				free(index->axv_idata.avb_ostring.aos_string);
		}
		bzero(&(axv->axv_index), sizeof(axv->axv_index));
		agentx_object_unlock(axv->axv_axo);
		agentx_varbind_start(axv);
		return;
	}

	bcopy(&(axv->axv_start), &(axv->axv_vb.avb_oid),
	    sizeof(axv->axv_start));
	axv->axv_vb.avb_type = AX_DATA_TYPE_ENDOFMIBVIEW;

	if (axv->axv_axo != NULL)
		agentx_object_unlock(axv->axv_axo);
	agentx_get_finalize(axv->axv_axg);
}

enum agentx_request_type
agentx_varbind_request(struct agentx_varbind *axv)
{
	if (axv->axv_axg->axg_type == AX_PDU_TYPE_GET)
		return AGENTX_REQUEST_TYPE_GET;
	if (axv->axv_include)
		return AGENTX_REQUEST_TYPE_GETNEXTINCLUSIVE;
	return AGENTX_REQUEST_TYPE_GETNEXT;
}

struct agentx_object *
agentx_varbind_get_object(struct agentx_varbind *axv)
{
	return axv->axv_axo;
}

int32_t
agentx_varbind_get_index_integer(struct agentx_varbind *axv,
    struct agentx_index *axi)
{
	size_t i;

	if (axi->axi_vb.avb_type != AX_DATA_TYPE_INTEGER) {
#ifdef AX_DEBUG
		agentx_log_axg_fatalx(axv->axv_axg, "invalid index type");
#else
		agentx_log_axg_warnx(axv->axv_axg, "invalid index type");
		agentx_varbind_error_type(axv, AX_PDU_ERROR_GENERR, 0);
		return 0;
#endif
	}

	for (i = 0; i < axv->axv_indexlen; i++) {
		if (axv->axv_index[i].axv_axi == axi)
			return axv->axv_index[i].axv_idata.avb_int32;
	}
#ifdef AX_DEBUG
	agentx_log_axg_fatalx(axv->axv_axg, "invalid index");
#else
	agentx_log_axg_warnx(axv->axv_axg, "invalid index");
	agentx_varbind_error_type(axv, AX_PDU_ERROR_GENERR, 0);
	return 0;
#endif
}

const unsigned char *
agentx_varbind_get_index_string(struct agentx_varbind *axv,
    struct agentx_index *axi, size_t *slen, int *implied)
{
	struct agentx_varbind_index *index;
	size_t i;

	if (axi->axi_vb.avb_type != AX_DATA_TYPE_OCTETSTRING) {
#ifdef AX_DEBUG
		agentx_log_axg_fatalx(axv->axv_axg, "invalid index type");
#else
		agentx_log_axg_warnx(axv->axv_axg, "invalid index type");
		agentx_varbind_error_type(axv, AX_PDU_ERROR_GENERR, 0);
		*slen = 0;
		*implied = 0;
		return NULL;
#endif
	}

	for (i = 0; i < axv->axv_indexlen; i++) {
		if (axv->axv_index[i].axv_axi == axi) {
			index = &(axv->axv_index[i]);
			*slen = index->axv_idata.avb_ostring.aos_slen;
			*implied = agentx_object_implied(axv->axv_axo, axi);
			return index->axv_idata.avb_ostring.aos_string;
		}
	}

#ifdef AX_DEBUG
	agentx_log_axg_fatalx(axv->axv_axg, "invalid index");
#else
	agentx_log_axg_warnx(axv->axv_axg, "invalid index");
	agentx_varbind_error_type(axv, AX_PDU_ERROR_GENERR, 0);
	*slen = 0;
	*implied = 0;
	return NULL;
#endif
}

const uint32_t *
agentx_varbind_get_index_oid(struct agentx_varbind *axv,
    struct agentx_index *axi, size_t *oidlen, int *implied)
{
	struct agentx_varbind_index *index;
	size_t i;

	if (axi->axi_vb.avb_type != AX_DATA_TYPE_OID) {
#ifdef AX_DEBUG
		agentx_log_axg_fatalx(axv->axv_axg, "invalid index type");
#else
		agentx_log_axg_warnx(axv->axv_axg, "invalid index type");
		agentx_varbind_error_type(axv, AX_PDU_ERROR_GENERR, 0);
		*oidlen = 0;
		*implied = 0;
		return NULL;
#endif
	}

	for (i = 0; i < axv->axv_indexlen; i++) {
		if (axv->axv_index[i].axv_axi == axi) {
			index = &(axv->axv_index[i]);
			*oidlen = index->axv_idata.avb_oid.aoi_idlen;
			*implied = agentx_object_implied(axv->axv_axo, axi);
			return index->axv_idata.avb_oid.aoi_id;
		}
	}

#ifdef AX_DEBUG
	agentx_log_axg_fatalx(axv->axv_axg, "invalid index");
#else
	agentx_log_axg_warnx(axv->axv_axg, "invalid index");
	agentx_varbind_error_type(axv, AX_PDU_ERROR_GENERR, 0);
	*oidlen = 0;
	*implied = 0;
	return NULL;
#endif
}

const struct in_addr *
agentx_varbind_get_index_ipaddress(struct agentx_varbind *axv,
    struct agentx_index *axi)
{
	static struct in_addr nuladdr = {0};
	struct agentx_varbind_index *index;
	size_t i;

	if (axi->axi_vb.avb_type != AX_DATA_TYPE_IPADDRESS) {
#ifdef AX_DEBUG
		agentx_log_axg_fatalx(axv->axv_axg, "invalid index type");
#else
		agentx_log_axg_warnx(axv->axv_axg, "invalid index type");
		agentx_varbind_error_type(axv, AX_PDU_ERROR_GENERR, 0);
		return NULL;
#endif
	}

	for (i = 0; i < axv->axv_indexlen; i++) {
		if (axv->axv_index[i].axv_axi == axi) {
			index = &(axv->axv_index[i]);
			if (index->axv_idata.avb_ostring.aos_string == NULL)
				return &nuladdr;
			return (struct in_addr *)
			    index->axv_idata.avb_ostring.aos_string;
		}
	}

#ifdef AX_DEBUG
	agentx_log_axg_fatalx(axv->axv_axg, "invalid index");
#else
	agentx_log_axg_warnx(axv->axv_axg, "invalid index");
	agentx_varbind_error_type(axv, AX_PDU_ERROR_GENERR, 0);
	return NULL;
#endif
}

void
agentx_varbind_set_index_integer(struct agentx_varbind *axv,
    struct agentx_index *axi, int32_t value)
{
	size_t i;

	if (axi->axi_vb.avb_type != AX_DATA_TYPE_INTEGER) {
#ifdef AX_DEBUG
		agentx_log_axg_fatalx(axv->axv_axg, "invalid index type");
#else
		agentx_log_axg_warnx(axv->axv_axg, "invalid index type");
		agentx_varbind_error_type(axv, AX_PDU_ERROR_GENERR, 0);
		return;
#endif
	}

	if (value < 0) {
#ifdef AX_DEBUG
		agentx_log_axg_fatalx(axv->axv_axg, "invalid index value");
#else
		agentx_log_axg_warnx(axv->axv_axg, "invalid index value");
		agentx_varbind_error_type(axv, AX_PDU_ERROR_GENERR, 0);
		return;
#endif
	}

	for (i = 0; i < axv->axv_indexlen; i++) {
		if (axv->axv_index[i].axv_axi == axi) {
			if (axv->axv_axg->axg_type == AX_PDU_TYPE_GET &&
			    axv->axv_index[i].axv_idata.avb_int32 != value) {
#ifdef AX_DEBUG
				agentx_log_axg_fatalx(axv->axv_axg,
				    "can't change index on GET");
#else
				agentx_log_axg_warnx(axv->axv_axg,
				    "can't change index on GET");
				agentx_varbind_error_type(axv,
				    AX_PDU_ERROR_GENERR, 0);
				return;
#endif
			}
			axv->axv_index[i].axv_idata.avb_int32 = value;
			return;
		}
	}
#ifdef AX_DEBUG
	agentx_log_axg_fatalx(axv->axv_axg, "invalid index");
#else
	agentx_log_axg_warnx(axv->axv_axg, "invalid index");
	agentx_varbind_error_type(axv, AX_PDU_ERROR_GENERR, 0);
#endif
}

void
agentx_varbind_set_index_string(struct agentx_varbind *axv,
    struct agentx_index *axi, const char *value)
{
	agentx_varbind_set_index_nstring(axv, axi,
	    (const unsigned char *)value, strlen(value));
}

void
agentx_varbind_set_index_nstring(struct agentx_varbind *axv,
    struct agentx_index *axi, const unsigned char *value, size_t slen)
{
	struct ax_ostring *curvalue;
	unsigned char *nstring;
	size_t i;

	if (axi->axi_vb.avb_type != AX_DATA_TYPE_OCTETSTRING) {
#ifdef AX_DEBUG
		agentx_log_axg_fatalx(axv->axv_axg, "invalid index type");
#else
		agentx_log_axg_warnx(axv->axv_axg, "invalid index type");
		agentx_varbind_error_type(axv, AX_PDU_ERROR_GENERR, 0);
		return;
#endif
	}

	for (i = 0; i < axv->axv_indexlen; i++) {
		if (axv->axv_index[i].axv_axi == axi) {
			if (axi->axi_vb.avb_data.avb_ostring.aos_slen != 0 &&
			    axi->axi_vb.avb_data.avb_ostring.aos_slen != slen) {
#ifdef AX_DEBUG
				agentx_log_axg_fatalx(axv->axv_axg,
				    "invalid string length on explicit length "
				    "string");
#else
				agentx_log_axg_warnx(axv->axv_axg,
				    "invalid string length on explicit length "
				    "string");
				agentx_varbind_error_type(axv,
				    AX_PDU_ERROR_GENERR, 0);
				return;
#endif
			}
			curvalue = &(axv->axv_index[i].axv_idata.avb_ostring);
			if (axv->axv_axg->axg_type == AX_PDU_TYPE_GET &&
			    (curvalue->aos_slen != slen ||
			    memcmp(curvalue->aos_string, value, slen) != 0)) {
#ifdef AX_DEBUG
				agentx_log_axg_fatalx(axv->axv_axg,
				    "can't change index on GET");
#else
				agentx_log_axg_warnx(axv->axv_axg,
				    "can't change index on GET");
				agentx_varbind_error_type(axv,
				    AX_PDU_ERROR_GENERR, 0);
				return;
#endif
			}
			if ((nstring = recallocarray(curvalue->aos_string,
			    curvalue->aos_slen + 1, slen + 1, 1)) == NULL) {
				agentx_log_axg_warn(axv->axv_axg,
				    "Failed to bind string index");
				agentx_varbind_error_type(axv,
				    AX_PDU_ERROR_PROCESSINGERROR, 0);
				return;
			}
			curvalue->aos_string = nstring;
			memcpy(nstring, value, slen);
			curvalue->aos_slen = slen;
			return;
		}
	}
#ifdef AX_DEBUG
	agentx_log_axg_fatalx(axv->axv_axg, "invalid index");
#else
	agentx_log_axg_warnx(axv->axv_axg, "invalid index");
	agentx_varbind_error_type(axv, AX_PDU_ERROR_GENERR, 0);
#endif
}

void
agentx_varbind_set_index_oid(struct agentx_varbind *axv,
    struct agentx_index *axi, const uint32_t *value, size_t oidlen)
{
	struct ax_oid *curvalue, oid;
	const char *errstr;
	size_t i;

	if (axi->axi_vb.avb_type != AX_DATA_TYPE_OID) {
#ifdef AX_DEBUG
		agentx_log_axg_fatalx(axv->axv_axg, "invalid index type");
#else
		agentx_log_axg_warnx(axv->axv_axg, "invalid index type");
		agentx_varbind_error_type(axv, AX_PDU_ERROR_GENERR, 0);
		return;
#endif
	}

	for (i = 0; i < axv->axv_indexlen; i++) {
		if (axv->axv_index[i].axv_axi == axi) {
			if (axi->axi_vb.avb_data.avb_oid.aoi_idlen != 0 &&
			    axi->axi_vb.avb_data.avb_oid.aoi_idlen != oidlen) {
#ifdef AX_DEBUG
				agentx_log_axg_fatalx(axv->axv_axg,
				    "invalid oid length on explicit length "
				    "oid");
#else
				agentx_log_axg_warnx(axv->axv_axg,
				    "invalid oid length on explicit length "
				    "oid");
				agentx_varbind_error_type(axv,
				    AX_PDU_ERROR_GENERR, 0);
				return;
#endif
			}
			curvalue = &(axv->axv_index[i].axv_idata.avb_oid);
			if (agentx_oidfill(&oid, value,
			    oidlen, &errstr) == -1) {
#ifdef AX_DEBUG
				agentx_log_axg_fatalx(axv->axv_axg, "%s: %s",
				    __func__, errstr);
#else
				agentx_log_axg_warnx(axv->axv_axg, "%s: %s",
				     __func__, errstr);
				agentx_varbind_error_type(axv,
				     AX_PDU_ERROR_PROCESSINGERROR, 1);
				return;
#endif
			}

			if (axv->axv_axg->axg_type == AX_PDU_TYPE_GET &&
			    ax_oid_cmp(&oid, curvalue) != 0) {
#ifdef AX_DEBUG
				agentx_log_axg_fatalx(axv->axv_axg,
				    "can't change index on GET");
#else
				agentx_log_axg_warnx(axv->axv_axg,
				    "can't change index on GET");
				agentx_varbind_error_type(axv,
				    AX_PDU_ERROR_GENERR, 0);
				return;
#endif
			}
			
			*curvalue = oid;
			return;
		}
	}
#ifdef AX_DEBUG
	agentx_log_axg_fatalx(axv->axv_axg, "invalid index");
#else
	agentx_log_axg_warnx(axv->axv_axg, "invalid index");
	agentx_varbind_error_type(axv, AX_PDU_ERROR_GENERR, 0);
#endif
}

void
agentx_varbind_set_index_object(struct agentx_varbind *axv,
    struct agentx_index *axi, struct agentx_object *axo)
{
	agentx_varbind_set_index_oid(axv, axi, axo->axo_oid.aoi_id,
	    axo->axo_oid.aoi_idlen);
}

void
agentx_varbind_set_index_ipaddress(struct agentx_varbind *axv,
    struct agentx_index *axi, const struct in_addr *addr)
{
	struct ax_ostring *curvalue;
	size_t i;

	if (axi->axi_vb.avb_type != AX_DATA_TYPE_IPADDRESS) {
#ifdef AX_DEBUG
		agentx_log_axg_fatalx(axv->axv_axg, "invalid index type");
#else
		agentx_log_axg_warnx(axv->axv_axg, "invalid index type");
		agentx_varbind_error_type(axv, AX_PDU_ERROR_GENERR, 0);
		return;
#endif
	}

	for (i = 0; i < axv->axv_indexlen; i++) {
		if (axv->axv_index[i].axv_axi == axi) {
			curvalue = &(axv->axv_index[i].axv_idata.avb_ostring);
			if (curvalue->aos_string == NULL)
				curvalue->aos_string = calloc(1, sizeof(*addr));
			if (curvalue->aos_string == NULL) {
				agentx_log_axg_warn(axv->axv_axg,
				    "Failed to bind ipaddress index");
				agentx_varbind_error_type(axv,
				    AX_PDU_ERROR_PROCESSINGERROR, 0);
				return;
			}
			if (axv->axv_axg->axg_type == AX_PDU_TYPE_GET &&
			    memcmp(addr, curvalue->aos_string,
			    sizeof(*addr)) != 0) {
#ifdef AX_DEBUG
				agentx_log_axg_fatalx(axv->axv_axg,
				    "can't change index on GET");
#else
				agentx_log_axg_warnx(axv->axv_axg,
				    "can't change index on GET");
				agentx_varbind_error_type(axv,
				    AX_PDU_ERROR_GENERR, 0);
				return;
#endif
			}
			bcopy(addr, curvalue->aos_string, sizeof(*addr));
			return;
		}
	}
#ifdef AX_DEBUG
	agentx_log_axg_fatalx(axv->axv_axg, "invalid index");
#else
	agentx_log_axg_warnx(axv->axv_axg, "invalid index");
	agentx_varbind_error_type(axv, AX_PDU_ERROR_GENERR, 0);
#endif
}

static int
agentx_request(struct agentx *ax, uint32_t packetid,
    int (*cb)(struct ax_pdu *, void *), void *cookie)
{
	struct agentx_request *axr;

#ifdef AX_DEBUG
	if (ax->ax_ax->ax_wblen == 0)
		agentx_log_ax_fatalx(ax, "%s: no data to be written",
		    __func__);
#endif

	if ((axr = calloc(1, sizeof(*axr))) == NULL) {
		agentx_log_ax_warn(ax, "couldn't create request context");
		agentx_reset(ax);
		return -1;
	}

	axr->axr_packetid = packetid;
	axr->axr_cb = cb;
	axr->axr_cookie = cookie;
	if (RB_INSERT(ax_requests, &(ax->ax_requests), axr) != NULL) {
#ifdef AX_DEBUG
		agentx_log_ax_fatalx(ax, "%s: duplicate packetid", __func__);
#else
		agentx_log_ax_warnx(ax, "%s: duplicate packetid", __func__);
		free(axr);
		agentx_reset(ax);
		return -1;
#endif
	}

	agentx_wantwrite(ax, ax->ax_fd);
	return 0;
}

static int
agentx_request_cmp(struct agentx_request *r1,
    struct agentx_request *r2)
{
	return r1->axr_packetid < r2->axr_packetid ? -1 :
	    r1->axr_packetid > r2->axr_packetid;
}

static int
agentx_strcat(char **dst, const char *src)
{
	char *tmp;
	size_t dstlen = 0, buflen = 0, srclen, nbuflen;

	if (*dst != NULL) {
		dstlen = strlen(*dst);
		buflen = ((dstlen / 512) + 1) * 512;
	}

	srclen = strlen(src);
	if (*dst == NULL || dstlen + srclen > buflen) {
		nbuflen = (((dstlen + srclen) / 512) + 1) * 512;
		tmp = recallocarray(*dst, buflen, nbuflen, sizeof(*tmp));
		if (tmp == NULL)
			return -1;
		*dst = tmp;
		buflen = nbuflen;
	}

	(void)strlcat(*dst, src, buflen);
	return 0;
}

static int
agentx_oidfill(struct ax_oid *oid, const uint32_t oidval[], size_t oidlen,
    const char **errstr)
{
	size_t i;

	if (oidlen < AGENTX_OID_MIN_LEN) {
		*errstr = "oidlen < 2";
		errno = EINVAL;
		return -1;
	}
	if (oidlen > AGENTX_OID_MAX_LEN) {
		*errstr = "oidlen > 128";
		errno = EINVAL;
		return -1;
	}

	for (i = 0; i < oidlen; i++)
		oid->aoi_id[i] = oidval[i];
	oid->aoi_idlen = oidlen;
	return 0;
}

void
agentx_read(struct agentx *ax)
{
	struct agentx_session *axs;
	struct agentx_context *axc;
	struct agentx_request axr_search, *axr;
	struct ax_pdu *pdu;
	int error;

	if ((pdu = ax_recv(ax->ax_ax)) == NULL) {
		if (errno == EAGAIN)
			return;
		agentx_log_ax_warn(ax, "lost connection");
		agentx_reset(ax);
		return;
	}

	TAILQ_FOREACH(axs, &(ax->ax_sessions), axs_ax_sessions) {
		if (axs->axs_id == pdu->ap_header.aph_sessionid)
			break;
		if (axs->axs_cstate == AX_CSTATE_WAITOPEN &&
		    axs->axs_packetid == pdu->ap_header.aph_packetid)
			break;
	}
	if (axs == NULL) {
		agentx_log_ax_warnx(ax, "received unexpected session: %d",
		    pdu->ap_header.aph_sessionid);
		ax_pdu_free(pdu);
		agentx_reset(ax);
		return;
	}
	TAILQ_FOREACH(axc, &(axs->axs_contexts), axc_axs_contexts) {
		if ((pdu->ap_header.aph_flags &
		    AX_PDU_FLAG_NON_DEFAULT_CONTEXT) == 0 &&
		    axc->axc_name_default == 1)
			break;
		if (pdu->ap_header.aph_flags &
		    AX_PDU_FLAG_NON_DEFAULT_CONTEXT &&
		    axc->axc_name_default == 0 &&
		    pdu->ap_context.aos_slen == axc->axc_name.aos_slen &&
		    memcmp(pdu->ap_context.aos_string,
		    axc->axc_name.aos_string, axc->axc_name.aos_slen) == 0)
			break;
	}
	if (pdu->ap_header.aph_type != AX_PDU_TYPE_RESPONSE) {
		if (axc == NULL) {
			agentx_log_ax_warnx(ax, "%s: invalid context",
			    pdu->ap_context.aos_string);
			ax_pdu_free(pdu);
			agentx_reset(ax);
			return;
		}
	}

	switch (pdu->ap_header.aph_type) {
	case AX_PDU_TYPE_GET:
	case AX_PDU_TYPE_GETNEXT:
	case AX_PDU_TYPE_GETBULK:
		agentx_get_start(axc, pdu);
		break;
	/* Add stubs for set functions */
	case AX_PDU_TYPE_TESTSET:
	case AX_PDU_TYPE_COMMITSET:
	case AX_PDU_TYPE_UNDOSET:
		if (pdu->ap_header.aph_type == AX_PDU_TYPE_TESTSET)
			error = AX_PDU_ERROR_NOTWRITABLE;
		else if (pdu->ap_header.aph_type == AX_PDU_TYPE_COMMITSET)
			error = AX_PDU_ERROR_COMMITFAILED;
		else
			error = AX_PDU_ERROR_UNDOFAILED;

		agentx_log_axc_debug(axc, "unsupported call: %s",
		    ax_pdutype2string(pdu->ap_header.aph_type));
		if (ax_response(ax->ax_ax, axs->axs_id,
		    pdu->ap_header.aph_transactionid,
		    pdu->ap_header.aph_packetid,
		    0, error, 1, NULL, 0) == -1)
			agentx_log_axc_warn(axc,
			    "transaction: %u packetid: %u: failed to send "
			    "reply", pdu->ap_header.aph_transactionid,
			    pdu->ap_header.aph_packetid);
		if (ax->ax_ax->ax_wblen > 0)
			agentx_wantwrite(ax, ax->ax_fd);
		break;
	case AX_PDU_TYPE_CLEANUPSET:
		agentx_log_ax_debug(ax, "unsupported call: %s",
		    ax_pdutype2string(pdu->ap_header.aph_type));
		break;
	case AX_PDU_TYPE_RESPONSE:
		axr_search.axr_packetid = pdu->ap_header.aph_packetid;
		axr = RB_FIND(ax_requests, &(ax->ax_requests), &axr_search);
		if (axr == NULL) {
			if (axc == NULL)
				agentx_log_ax_warnx(ax, "received "
				    "response on non-request");
			else
				agentx_log_axc_warnx(axc, "received "
				    "response on non-request");
			break;
		}
		if (axc != NULL && pdu->ap_payload.ap_response.ap_error == 0) {
			axc->axc_sysuptime =
			    pdu->ap_payload.ap_response.ap_uptime;
			(void) clock_gettime(CLOCK_MONOTONIC,
			    &(axc->axc_sysuptimespec));
		}
		RB_REMOVE(ax_requests, &(ax->ax_requests), axr);
		(void) axr->axr_cb(pdu, axr->axr_cookie);
		free(axr);
		break;
	default:
		if (axc == NULL)
			agentx_log_ax_warnx(ax, "unsupported call: %s",
			    ax_pdutype2string(pdu->ap_header.aph_type));
		else
			agentx_log_axc_warnx(axc, "unsupported call: %s",
			    ax_pdutype2string(pdu->ap_header.aph_type));
		agentx_reset(ax);
		break;
	}
	ax_pdu_free(pdu);
}

void
agentx_write(struct agentx *ax)
{
	ssize_t send;

	if ((send = ax_send(ax->ax_ax)) == -1) {
		if (errno == EAGAIN) {
			agentx_wantwrite(ax, ax->ax_fd);
			return;
		}
		agentx_log_ax_warn(ax, "lost connection");
		agentx_reset(ax);
		return;
	}
	if (send > 0)
		agentx_wantwrite(ax, ax->ax_fd);
}

RB_GENERATE_STATIC(ax_requests, agentx_request, axr_ax_requests,
    agentx_request_cmp)
RB_GENERATE_STATIC(axc_objects, agentx_object, axo_axc_objects,
    agentx_object_cmp)
