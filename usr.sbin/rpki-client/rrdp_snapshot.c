/*	$OpenBSD: rrdp_snapshot.c,v 1.12 2025/06/13 12:34:14 tb Exp $ */
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

#include "extern.h"
#include "rrdp.h"

enum snapshot_scope {
	SNAPSHOT_SCOPE_NONE,
	SNAPSHOT_SCOPE_SNAPSHOT,
	SNAPSHOT_SCOPE_PUBLISH,
	SNAPSHOT_SCOPE_END
};

struct snapshot_xml {
	XML_Parser		 parser;
	struct rrdp_session	*current;
	struct rrdp		*rrdp;
	struct publish_xml	*pxml;
	char			*session_id;
	long long		 serial;
	int			 version;
	enum snapshot_scope	 scope;
};

static void
start_snapshot_elem(struct snapshot_xml *sxml, const char **attr)
{
	XML_Parser p = sxml->parser;
	int has_xmlns = 0;
	int i;

	if (sxml->scope != SNAPSHOT_SCOPE_NONE)
		PARSE_FAIL(p,
		    "parse failed - entered snapshot elem unexpectedely");
	for (i = 0; attr[i]; i += 2) {
		const char *errstr;
		if (strcmp("xmlns", attr[i]) == 0 &&
		    strcmp(RRDP_XMLNS, attr[i + 1]) == 0 && has_xmlns++ == 0)
			continue;
		if (strcmp("version", attr[i]) == 0 && sxml->version == 0) {
			sxml->version = strtonum(attr[i + 1],
			    1, MAX_VERSION, &errstr);
			if (errstr == NULL)
				continue;
		}
		if (strcmp("session_id", attr[i]) == 0 &&
		    valid_uuid(attr[i + 1]) && sxml->session_id == NULL) {
			sxml->session_id = xstrdup(attr[i + 1]);
			continue;
		}
		if (strcmp("serial", attr[i]) == 0 && sxml->serial == 0) {
			sxml->serial = strtonum(attr[i + 1],
			    1, LLONG_MAX, &errstr);
			if (errstr == NULL)
				continue;
		}
		PARSE_FAIL(p,
		    "parse failed - non conforming "
		    "attribute '%s' found in snapshot elem", attr[i]);
	}
	if (!(has_xmlns && sxml->version && sxml->session_id && sxml->serial))
		PARSE_FAIL(p,
		    "parse failed - incomplete snapshot attributes");
	if (strcmp(sxml->current->session_id, sxml->session_id) != 0)
		PARSE_FAIL(p, "parse failed - session_id mismatch");
	if (sxml->current->serial != sxml->serial)
		PARSE_FAIL(p, "parse failed - serial mismatch");

	sxml->scope = SNAPSHOT_SCOPE_SNAPSHOT;
}

static void
end_snapshot_elem(struct snapshot_xml *sxml)
{
	XML_Parser p = sxml->parser;

	if (sxml->scope != SNAPSHOT_SCOPE_SNAPSHOT)
		PARSE_FAIL(p, "parse failed - exited snapshot "
		    "elem unexpectedely");
	sxml->scope = SNAPSHOT_SCOPE_END;
}

static void
start_publish_elem(struct snapshot_xml *sxml, const char **attr)
{
	XML_Parser p = sxml->parser;
	char *uri = NULL;
	int i, hasUri = 0;

	if (sxml->scope != SNAPSHOT_SCOPE_SNAPSHOT)
		PARSE_FAIL(p,
		    "parse failed - entered publish elem unexpectedely");
	for (i = 0; attr[i]; i += 2) {
		if (strcmp("uri", attr[i]) == 0 && hasUri++ == 0) {
			if (valid_uri(attr[i + 1], strlen(attr[i + 1]),
			    RSYNC_PROTO)) {
				uri = xstrdup(attr[i + 1]);
				continue;
			}
		}
		/*
		 * XXX it seems people can not write proper XML, ignore
		 * bogus xmlns attribute on publish elements.
		 */
		if (strcmp("xmlns", attr[i]) == 0)
			continue;
		free(uri);
		PARSE_FAIL(p, "parse failed - non conforming"
		    " attribute '%s' found in publish elem", attr[i]);
	}
	if (hasUri != 1) {
		free(uri);
		PARSE_FAIL(p, "parse failed - incomplete publish attributes");
	}
	sxml->pxml = new_publish_xml(PUB_ADD, uri, NULL, 0);
	sxml->scope = SNAPSHOT_SCOPE_PUBLISH;
}

static void
end_publish_elem(struct snapshot_xml *sxml)
{
	XML_Parser p = sxml->parser;

	if (sxml->scope != SNAPSHOT_SCOPE_PUBLISH)
		PARSE_FAIL(p, "parse failed - exited publish "
		    "elem unexpectedely");

	if (publish_done(sxml->rrdp, sxml->pxml) != 0)
		PARSE_FAIL(p, "parse failed - bad publish elem");
	sxml->pxml = NULL;

	sxml->scope = SNAPSHOT_SCOPE_SNAPSHOT;
}

static void
snapshot_xml_elem_start(void *data, const char *el, const char **attr)
{
	struct snapshot_xml *sxml = data;
	XML_Parser p = sxml->parser;

	/*
	 * Can only enter here once as we should have no ways to get back to
	 * NONE scope
	 */
	if (strcmp("snapshot", el) == 0)
		start_snapshot_elem(sxml, attr);
	/*
	 * Will enter here multiple times, BUT never nested. will start
	 * collecting character data in that handler mem is cleared in end
	 * block, (TODO or on parse failure)
	 */
	else if (strcmp("publish", el) == 0)
		start_publish_elem(sxml, attr);
	else
		PARSE_FAIL(p, "parse failed - unexpected elem exit found");
}

static void
snapshot_xml_elem_end(void *data, const char *el)
{
	struct snapshot_xml *sxml = data;
	XML_Parser p = sxml->parser;

	if (strcmp("snapshot", el) == 0)
		end_snapshot_elem(sxml);
	else if (strcmp("publish", el) == 0)
		end_publish_elem(sxml);
	else
		PARSE_FAIL(p, "parse failed - unexpected elem exit found");
}

static void
snapshot_content_handler(void *data, const char *content, int length)
{
	struct snapshot_xml *sxml = data;
	XML_Parser p = sxml->parser;

	if (sxml->scope == SNAPSHOT_SCOPE_PUBLISH)
		if (publish_add_content(sxml->pxml, content, length) == -1)
			PARSE_FAIL(p, "parse failed, snapshot element for %s "
			    "too big", sxml->pxml->uri);
}

static void
snapshot_doctype_handler(void *data, const char *doctypeName,
    const char *sysid, const char *pubid, int subset)
{
	struct snapshot_xml *sxml = data;
	XML_Parser p = sxml->parser;

	PARSE_FAIL(p, "parse failed - DOCTYPE not allowed");
}

struct snapshot_xml *
new_snapshot_xml(XML_Parser p, struct rrdp_session *rs, struct rrdp *r)
{
	struct snapshot_xml *sxml;

	if ((sxml = calloc(1, sizeof(*sxml))) == NULL)
		err(1, "%s", __func__);
	sxml->parser = p;
	sxml->current = rs;
	sxml->rrdp = r;

	if (XML_ParserReset(sxml->parser, "US-ASCII") != XML_TRUE)
		errx(1, "%s: XML_ParserReset failed", __func__);

	XML_SetElementHandler(sxml->parser, snapshot_xml_elem_start,
	    snapshot_xml_elem_end);
	XML_SetCharacterDataHandler(sxml->parser, snapshot_content_handler);
	XML_SetUserData(sxml->parser, sxml);
	XML_SetDoctypeDeclHandler(sxml->parser, snapshot_doctype_handler,
	    NULL);

	return sxml;
}

void
free_snapshot_xml(struct snapshot_xml *sxml)
{
	if (sxml == NULL)
		return;

	free(sxml->session_id);
	free_publish_xml(sxml->pxml);
	free(sxml);
}

/* Used in regress. */
void
log_snapshot_xml(struct snapshot_xml *sxml)
{
	logx("scope: %d", sxml->scope);
	logx("version: %d", sxml->version);
	logx("session_id: %s serial: %lld", sxml->session_id, sxml->serial);
}
