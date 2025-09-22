/*	$OpenBSD: rrdp_delta.c,v 1.16 2025/06/13 12:34:14 tb Exp $ */
/*
 * Copyright (c) 2020 Nils Fisher <nils_fisher@hotmail.com>
 * Copyright (c) 2021 Claudio Jeker <claudio@openbsd.org>
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

#include <err.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

#include <expat.h>
#include <openssl/sha.h>

#include "extern.h"
#include "rrdp.h"

enum delta_scope {
	DELTA_SCOPE_NONE,
	DELTA_SCOPE_EMPTY_DELTA,
	DELTA_SCOPE_DELTA,
	DELTA_SCOPE_PUBLISH,
	DELTA_SCOPE_END
};

struct delta_xml {
	XML_Parser		 parser;
	struct rrdp_session	*current;
	struct rrdp		*rrdp;
	struct publish_xml	*pxml;
	char			*session_id;
	long long		 serial;
	int			 version;
	enum delta_scope	 scope;
};

static void
start_delta_elem(struct delta_xml *dxml, const char **attr)
{
	XML_Parser p = dxml->parser;
	int has_xmlns = 0;
	int i;

	if (dxml->scope != DELTA_SCOPE_NONE)
		PARSE_FAIL(p,
		    "parse failed - entered delta elem unexpectedely");
	for (i = 0; attr[i]; i += 2) {
		const char *errstr;
		if (strcmp("xmlns", attr[i]) == 0 &&
		    strcmp(RRDP_XMLNS, attr[i + 1]) == 0 && has_xmlns++ == 0)
			continue;
		if (strcmp("version", attr[i]) == 0 && dxml->version == 0) {
			dxml->version = strtonum(attr[i + 1],
			    1, MAX_VERSION, &errstr);
			if (errstr == NULL)
				continue;
		}
		if (strcmp("session_id", attr[i]) == 0 &&
		    valid_uuid(attr[i + 1]) && dxml->session_id == NULL) {
			dxml->session_id = xstrdup(attr[i + 1]);
			continue;
		}
		if (strcmp("serial", attr[i]) == 0 && dxml->serial == 0) {
			dxml->serial = strtonum(attr[i + 1],
			    1, LLONG_MAX, &errstr);
			if (errstr == NULL)
				continue;
		}
		PARSE_FAIL(p, "parse failed - non conforming "
		    "attribute '%s' found in delta elem", attr[i]);
	}
	if (!(has_xmlns && dxml->version && dxml->session_id && dxml->serial))
		PARSE_FAIL(p, "parse failed - incomplete delta attributes");
	if (strcmp(dxml->current->session_id, dxml->session_id) != 0)
		PARSE_FAIL(p, "parse failed - session_id mismatch");
	if (dxml->current->serial != dxml->serial)
		PARSE_FAIL(p, "parse failed - serial mismatch");

	dxml->scope = DELTA_SCOPE_EMPTY_DELTA;
}

static void
end_delta_elem(struct delta_xml *dxml)
{
	XML_Parser p = dxml->parser;

	if (dxml->scope == DELTA_SCOPE_EMPTY_DELTA)
		PARSE_FAIL(p, "parse failed - empty delta");
	if (dxml->scope != DELTA_SCOPE_DELTA)
		PARSE_FAIL(p, "parse failed - exited delta "
		    "elem unexpectedely");
	dxml->scope = DELTA_SCOPE_END;
}

static void
start_publish_withdraw_elem(struct delta_xml *dxml, const char **attr,
    int withdraw)
{
	XML_Parser p = dxml->parser;
	char *uri = NULL, hash[SHA256_DIGEST_LENGTH];
	int i, hasUri = 0, hasHash = 0;
	enum publish_type pub = PUB_UPD;

	if (dxml->scope != DELTA_SCOPE_EMPTY_DELTA &&
	    dxml->scope != DELTA_SCOPE_DELTA)
		PARSE_FAIL(p, "parse failed - entered publish/withdraw "
		    "elem unexpectedely");
	for (i = 0; attr[i]; i += 2) {
		if (strcmp("uri", attr[i]) == 0 && hasUri++ == 0) {
			if (valid_uri(attr[i + 1], strlen(attr[i + 1]),
			    RSYNC_PROTO)) {
				uri = xstrdup(attr[i + 1]);
				continue;
			}
		}
		if (strcmp("hash", attr[i]) == 0 && hasHash++ == 0) {
			if (hex_decode(attr[i + 1], hash, sizeof(hash)) == 0)
				continue;
		}
		free(uri);
		PARSE_FAIL(p, "parse failed - non conforming "
		    "attribute '%s' found in publish/withdraw elem", attr[i]);
	}
	if (hasUri != 1) {
		free(uri);
		PARSE_FAIL(p,
		    "parse failed - incomplete publish/withdraw attributes");
	}
	if (withdraw && hasHash != 1) {
		free(uri);
		PARSE_FAIL(p, "parse failed - incomplete withdraw attributes");
	}

	if (withdraw)
		pub = PUB_DEL;
	else if (hasHash == 0)
		pub = PUB_ADD;
	dxml->pxml = new_publish_xml(pub, uri, hash,
	    hasHash ? sizeof(hash) : 0);
	dxml->scope = DELTA_SCOPE_PUBLISH;
}

static void
end_publish_withdraw_elem(struct delta_xml *dxml, int withdraw)
{
	XML_Parser p = dxml->parser;

	if (dxml->scope != DELTA_SCOPE_PUBLISH)
		PARSE_FAIL(p, "parse failed - exited publish/withdraw "
		    "elem unexpectedely");

	if (publish_done(dxml->rrdp, dxml->pxml) != 0)
		PARSE_FAIL(p, "parse failed - bad publish/withdraw elem");
	dxml->pxml = NULL;

	dxml->scope = DELTA_SCOPE_DELTA;
}

static void
delta_xml_elem_start(void *data, const char *el, const char **attr)
{
	struct delta_xml *dxml = data;
	XML_Parser p = dxml->parser;

	/*
	 * Can only enter here once as we should have no ways to get back to
	 * NONE scope
	 */
	if (strcmp("delta", el) == 0)
		start_delta_elem(dxml, attr);
	/*
	 * Will enter here multiple times, BUT never nested. will start
	 * collecting character data in that handler
	 * mem is cleared in end block, (TODO or on parse failure)
	 */
	else if (strcmp("publish", el) == 0)
		start_publish_withdraw_elem(dxml, attr, 0);
	else if (strcmp("withdraw", el) == 0)
		start_publish_withdraw_elem(dxml, attr, 1);
	else
		PARSE_FAIL(p, "parse failed - unexpected elem exit found");
}

static void
delta_xml_elem_end(void *data, const char *el)
{
	struct delta_xml *dxml = data;
	XML_Parser p = dxml->parser;

	if (strcmp("delta", el) == 0)
		end_delta_elem(dxml);
	/*
	 * TODO does this allow <publish></withdraw> or is that caught by basic
	 * xml parsing
	 */
	else if (strcmp("publish", el) == 0)
		end_publish_withdraw_elem(dxml, 0);
	else if (strcmp("withdraw", el) == 0)
		end_publish_withdraw_elem(dxml, 1);
	else
		PARSE_FAIL(p, "parse failed - unexpected elem exit found");
}

static void
delta_content_handler(void *data, const char *content, int length)
{
	struct delta_xml *dxml = data;
	XML_Parser p = dxml->parser;

	if (dxml->scope == DELTA_SCOPE_PUBLISH)
		if (publish_add_content(dxml->pxml, content, length) == -1)
			PARSE_FAIL(p, "parse failed, delta element for %s too "
			    "big", dxml->pxml->uri);
}

static void
delta_doctype_handler(void *data, const char *doctypeName,
    const char *sysid, const char *pubid, int subset)
{
	struct delta_xml *dxml = data;
	XML_Parser p = dxml->parser;

	PARSE_FAIL(p, "parse failed - DOCTYPE not allowed");
}

struct delta_xml *
new_delta_xml(XML_Parser p, struct rrdp_session *rs, struct rrdp *r)
{
	struct delta_xml *dxml;

	if ((dxml = calloc(1, sizeof(*dxml))) == NULL)
		err(1, "%s", __func__);
	dxml->parser = p;
	dxml->current = rs;
	dxml->rrdp = r;

	if (XML_ParserReset(dxml->parser, "US-ASCII") != XML_TRUE)
		errx(1, "%s: XML_ParserReset failed", __func__);

	XML_SetElementHandler(dxml->parser, delta_xml_elem_start,
	    delta_xml_elem_end);
	XML_SetCharacterDataHandler(dxml->parser, delta_content_handler);
	XML_SetUserData(dxml->parser, dxml);
	XML_SetDoctypeDeclHandler(dxml->parser, delta_doctype_handler, NULL);

	return dxml;
}

void
free_delta_xml(struct delta_xml *dxml)
{
	if (dxml == NULL)
		return;

	free(dxml->session_id);
	free_publish_xml(dxml->pxml);
	free(dxml);
}

/* Used in regress. */
void
log_delta_xml(struct delta_xml *dxml)
{
	logx("version: %d", dxml->version);
	logx("session_id: %s serial: %lld", dxml->session_id, dxml->serial);
}
