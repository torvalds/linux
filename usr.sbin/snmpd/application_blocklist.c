/*	$OpenBSD: application_blocklist.c,v 1.2 2023/12/21 12:43:31 martijn Exp $	*/

/*
 * Copyright (c) 2022 Martijn van Duren <martijn@openbsd.org>
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

#include <stddef.h>

#include <ber.h>
#include <stdint.h>
#include <stdlib.h>

#include "application.h"
#include "log.h"
#include "snmpd.h"

struct appl_varbind *appl_blocklist_response(size_t);
void appl_blocklist_get(struct appl_backend *, int32_t, int32_t, const char *,
    struct appl_varbind *);
void appl_blocklist_getnext(struct appl_backend *, int32_t, int32_t,
    const char *, struct appl_varbind *);

struct appl_backend_functions appl_blocklist_functions = {
	.ab_get = appl_blocklist_get,
	.ab_getnext = appl_blocklist_getnext,
	.ab_getbulk = NULL,
};

struct appl_backend appl_blocklist = {
	.ab_name = "blocklist",
	.ab_cookie = NULL,
	.ab_retries = 0,
	.ab_fn = &appl_blocklist_functions
};

static struct appl_varbind *response = NULL;
static size_t responsesz = 0;

struct appl_varbind *
appl_blocklist_response(size_t nvarbind)
{
	struct appl_varbind *tmp;
	size_t i;

	if (responsesz < nvarbind) {
		if ((tmp = recallocarray(response, responsesz, nvarbind,
		    sizeof(*response))) == NULL) {
			log_warn(NULL);
			return NULL;
		}
		responsesz = nvarbind;
		response = tmp;
	}
	for (i = 0; i < nvarbind; i++)
		response[i].av_next = i + 1 == nvarbind ?
		    NULL : &(response[i + 1]);
	return response;
}

void
appl_blocklist_init(void)
{
	extern struct snmpd *snmpd_env;
	size_t i;

	for (i = 0; i < snmpd_env->sc_nblocklist; i++)
		appl_register(NULL, 150, 1, &(snmpd_env->sc_blocklist[i]),
		    0, 1, 0, 0, &appl_blocklist);
}

void
appl_blocklist_shutdown(void)
{
	appl_close(&appl_blocklist);
	free(response);
}

void
appl_blocklist_get(struct appl_backend *backend, __unused int32_t transactionid,
    int32_t requestid, __unused const char *ctx, struct appl_varbind *vblist)
{
	struct appl_varbind *vb, *rvb, *rvblist;
	size_t i;

	for (i = 0, vb = vblist; vb != NULL; vb = vb->av_next)
		i++;
	if ((rvblist = appl_blocklist_response(i)) == NULL)
		goto fail;
	rvb = rvblist;
	for (vb = vblist; vb != NULL; vb = vb->av_next, rvb = rvb->av_next) {
		rvb->av_oid = vb->av_oid;
		rvb->av_value = appl_exception(APPL_EXC_NOSUCHOBJECT);
		if (rvb->av_value == NULL)
			goto fail;
	}

	appl_response(backend, requestid, APPL_ERROR_NOERROR, 0, rvblist);
	return;
 fail:
	for (rvb = rvblist; rvb != NULL && rvb->av_value != NULL;
	    rvb = rvb->av_next)
		ober_free_elements(rvb->av_value);
	appl_response(backend, requestid, APPL_ERROR_GENERR, 1, vb);
}

void
appl_blocklist_getnext(struct appl_backend *backend,
    __unused int32_t transactionid, int32_t requestid, __unused const char *ctx,
    struct appl_varbind *vblist)
{
	struct appl_varbind *vb, *rvb, *rvblist;
	size_t i;

	for (i = 0, vb = vblist; vb != NULL; vb = vb->av_next)
		i++;
	if ((rvblist = appl_blocklist_response(i)) == NULL)
		goto fail;
	rvb = rvblist;
	for (vb = vblist; vb != NULL; vb = vb->av_next, rvb = rvb->av_next) {
		rvb->av_oid = vb->av_oid;
		rvb->av_value = appl_exception(APPL_EXC_ENDOFMIBVIEW);
		if (rvb->av_value == NULL)
			goto fail;
	}

	appl_response(backend, requestid, APPL_ERROR_NOERROR, 0, rvblist);
	return;
 fail:
	for (rvb = rvblist; rvb != NULL && rvb->av_value != NULL;
	    rvb = rvb->av_next)
		ober_free_elements(rvb->av_value);
	appl_response(backend, requestid, APPL_ERROR_GENERR, 1, vb);
}
