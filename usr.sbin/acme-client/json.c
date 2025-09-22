/*	$Id: json.c,v 1.22 2025/09/16 15:06:02 sthen Exp $ */
/*
 * Copyright (c) 2016 Kristaps Dzonsons <kristaps@bsd.lv>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHORS DISCLAIM ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <assert.h>
#include <err.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "jsmn.h"
#include "extern.h"

struct	jsmnp;

/*
 * A node in the JSMN parse tree.
 * Each of this corresponds to an object in the original JSMN token
 * list, although the contents have been extracted properly.
 */
struct	jsmnn {
	struct parse	*p; /* parser object */
	union {
		char *str; /* JSMN_PRIMITIVE, JSMN_STRING */
		struct jsmnp *obj; /* JSMN_OBJECT */
		struct jsmnn **array; /* JSMN_ARRAY */
	} d;
	size_t		 fields; /* entries in "d" */
	jsmntype_t	 type; /* type of node */
};

/*
 * Objects consist of node pairs: the left-hand side (before the colon)
 * and the right-hand side---the data.
 */
struct	jsmnp {
	struct jsmnn	*lhs; /* left of colon */
	struct jsmnn	*rhs; /* right of colon */
};

/*
 * Object for converting the JSMN token array into a tree.
 */
struct	parse {
	struct jsmnn	*nodes; /* all nodes */
	size_t		 cur; /* current number */
	size_t		 max; /* nodes in "nodes" */
};

/*
 * Recursive part for convertin a JSMN token array into a tree.
 * See "example/jsondump.c" for its construction (it's the same except
 * for how it handles allocation errors).
 */
static ssize_t
build(struct parse *parse, struct jsmnn **np,
    jsmntok_t *t, const char *js, size_t sz)
{
	size_t		 i, j;
	struct jsmnn	*n;
	ssize_t		 tmp;

	if (sz == 0)
		return 0;

	assert(parse->cur < parse->max);
	n = *np = &parse->nodes[parse->cur++];
	n->p = parse;
	n->type = t->type;

	switch (t->type) {
	case JSMN_STRING:
		/* FALLTHROUGH */
	case JSMN_PRIMITIVE:
		n->fields = 1;
		n->d.str = strndup
			(js + t->start,
			 t->end - t->start);
		if (n->d.str == NULL)
			break;
		return 1;
	case JSMN_OBJECT:
		n->fields = t->size;
		n->d.obj = calloc(n->fields,
			sizeof(struct jsmnp));
		if (n->d.obj == NULL)
			break;
		for (i = j = 0; i < (size_t)t->size; i++) {
			tmp = build(parse,
				&n->d.obj[i].lhs,
				t + 1 + j, js, sz - j);
			if (tmp < 0)
				break;
			j += tmp;
			tmp = build(parse,
				&n->d.obj[i].rhs,
				t + 1 + j, js, sz - j);
			if (tmp < 0)
				break;
			j += tmp;
		}
		if (i < (size_t)t->size)
			break;
		return j + 1;
	case JSMN_ARRAY:
		n->fields = t->size;
		n->d.array = calloc(n->fields,
			sizeof(struct jsmnn *));
		if (n->d.array == NULL)
			break;
		for (i = j = 0; i < (size_t)t->size; i++) {
			tmp = build(parse,
				&n->d.array[i],
				t + 1 + j, js, sz - j);
			if (tmp < 0)
				break;
			j += tmp;
		}
		if (i < (size_t)t->size)
			break;
		return j + 1;
	default:
		break;
	}

	return -1;
}

/*
 * Fully free up a parse sequence.
 * This handles all nodes sequentially, not recursively.
 */
static void
jsmnparse_free(struct parse *p)
{
	size_t	 i;

	if (p == NULL)
		return;
	for (i = 0; i < p->max; i++) {
		struct jsmnn	*n = &p->nodes[i];
		switch (n->type) {
		case JSMN_ARRAY:
			free(n->d.array);
			break;
		case JSMN_OBJECT:
			free(n->d.obj);
			break;
		case JSMN_PRIMITIVE:
			free(n->d.str);
			break;
		case JSMN_STRING:
			free(n->d.str);
			break;
		case JSMN_UNDEFINED:
			break;
		}
	}
	free(p->nodes);
	free(p);
}

/*
 * Allocate a tree representation of "t".
 * This returns NULL on allocation failure or when sz is zero, in which
 * case all resources allocated along the way are freed already.
 */
static struct jsmnn *
jsmntree_alloc(jsmntok_t *t, const char *js, size_t sz)
{
	struct jsmnn	*first;
	struct parse	*p;

	if (sz == 0)
		return NULL;

	p = calloc(1, sizeof(struct parse));
	if (p == NULL)
		return NULL;

	p->max = sz;
	p->nodes = calloc(p->max, sizeof(struct jsmnn));
	if (p->nodes == NULL) {
		free(p);
		return NULL;
	}

	if (build(p, &first, t, js, sz) < 0) {
		jsmnparse_free(p);
		first = NULL;
	}

	return first;
}

/*
 * Call through to free parse contents.
 */
void
json_free(struct jsmnn *first)
{

	if (first != NULL)
		jsmnparse_free(first->p);
}

/*
 * Just check that the array object is in fact an object.
 */
static struct jsmnn *
json_getarrayobj(struct jsmnn *n)
{

	return n->type != JSMN_OBJECT ? NULL : n;
}

/*
 * Get a string element from an array
 */
static char *
json_getarraystr(struct jsmnn *n)
{
	return n->type != JSMN_STRING ? NULL : n->d.str;
}

/*
 * Extract an array from the returned JSON object, making sure that it's
 * the correct type.
 * Returns NULL on failure.
 */
static struct jsmnn *
json_getarray(struct jsmnn *n, const char *name)
{
	size_t		 i;

	if (n->type != JSMN_OBJECT)
		return NULL;
	for (i = 0; i < n->fields; i++) {
		if (n->d.obj[i].lhs->type != JSMN_STRING &&
		    n->d.obj[i].lhs->type != JSMN_PRIMITIVE)
			continue;
		else if (strcmp(name, n->d.obj[i].lhs->d.str))
			continue;
		break;
	}
	if (i == n->fields)
		return NULL;
	if (n->d.obj[i].rhs->type != JSMN_ARRAY)
		return NULL;
	return n->d.obj[i].rhs;
}

/*
 * Extract subtree from the returned JSON object, making sure that it's
 * the correct type.
 * Returns NULL on failure.
 */
static struct jsmnn *
json_getobj(struct jsmnn *n, const char *name)
{
	size_t		 i;

	if (n->type != JSMN_OBJECT)
		return NULL;
	for (i = 0; i < n->fields; i++) {
		if (n->d.obj[i].lhs->type != JSMN_STRING &&
		    n->d.obj[i].lhs->type != JSMN_PRIMITIVE)
			continue;
		else if (strcmp(name, n->d.obj[i].lhs->d.str))
			continue;
		break;
	}
	if (i == n->fields)
		return NULL;
	if (n->d.obj[i].rhs->type != JSMN_OBJECT)
		return NULL;
	return n->d.obj[i].rhs;
}

/*
 * Extract a single string from the returned JSON object, making sure
 * that it's the correct type.
 * Returns NULL on failure.
 */
char *
json_getstr(struct jsmnn *n, const char *name)
{
	size_t		 i;
	char		*cp;

	if (n->type != JSMN_OBJECT)
		return NULL;
	for (i = 0; i < n->fields; i++) {
		if (n->d.obj[i].lhs->type != JSMN_STRING &&
		    n->d.obj[i].lhs->type != JSMN_PRIMITIVE)
			continue;
		else if (strcmp(name, n->d.obj[i].lhs->d.str))
			continue;
		break;
	}
	if (i == n->fields)
		return NULL;
	if (n->d.obj[i].rhs->type != JSMN_STRING &&
	    n->d.obj[i].rhs->type != JSMN_PRIMITIVE)
		return NULL;

	cp = strdup(n->d.obj[i].rhs->d.str);
	if (cp == NULL)
		warn("strdup");
	return cp;
}

/*
 * Completely free the challenge response body.
 */
void
json_free_challenge(struct chng *p)
{

	free(p->uri);
	free(p->token);
	p->uri = p->token = NULL;
}

/*
 * Parse the response from the ACME server when we're waiting to see
 * whether the challenge has been ok.
 */
enum chngstatus
json_parse_response(struct jsmnn *n)
{
	char		*resp;
	enum chngstatus	 rc;

	if (n == NULL)
		return CHNG_INVALID;
	if ((resp = json_getstr(n, "status")) == NULL)
		return CHNG_INVALID;

	if (strcmp(resp, "valid") == 0)
		rc = CHNG_VALID;
	else if (strcmp(resp, "pending") == 0)
		rc = CHNG_PENDING;
	else if (strcmp(resp, "processing") == 0)
		rc = CHNG_PROCESSING;
	else
		rc = CHNG_INVALID;

	free(resp);
	return rc;
}

/*
 * Parse the response from a new-authz, which consists of challenge
 * information, into a structure.
 * We only care about the HTTP-01 response.
 */
int
json_parse_challenge(struct jsmnn *n, struct chng *p)
{
	struct jsmnn	*array, *obj, *error;
	size_t		 i;
	int		 rc;
	char		*type;

	if (n == NULL)
		return 0;

	array = json_getarray(n, "challenges");
	if (array == NULL)
		return 0;

	for (i = 0; i < array->fields; i++) {
		obj = json_getarrayobj(array->d.array[i]);
		if (obj == NULL)
			continue;
		type = json_getstr(obj, "type");
		if (type == NULL)
			continue;
		rc = strcmp(type, "http-01");
		free(type);
		if (rc)
			continue;
		p->uri = json_getstr(obj, "url");
		p->token = json_getstr(obj, "token");
		p->status = json_parse_response(obj);
		if (p->status == CHNG_INVALID) {
			error = json_getobj(obj, "error");
			p->error = json_getstr(error, "detail");
		}
		return p->uri != NULL && p->token != NULL;
	}

	return 0;
}

static enum orderstatus
json_parse_order_status(struct jsmnn *n)
{
	char	*status;

	if (n == NULL)
		return ORDER_INVALID;

	if ((status = json_getstr(n, "status")) == NULL)
		return ORDER_INVALID;

	if (strcmp(status, "pending") == 0)
		return ORDER_PENDING;
	else if (strcmp(status, "ready") == 0)
		return ORDER_READY;
	else if (strcmp(status, "processing") == 0)
		return ORDER_PROCESSING;
	else if (strcmp(status, "valid") == 0)
		return ORDER_VALID;
	else if (strcmp(status, "invalid") == 0)
		return ORDER_INVALID;
	else
		return ORDER_INVALID;
}

/*
 * Parse the response from a newOrder, which consists of a status
 * a list of authorization urls and a finalize url into a struct.
 */
int
json_parse_order(struct jsmnn *n, struct order *order)
{
	struct jsmnn	*array;
	size_t		 i;
	char		*finalize, *str;

	order->status = json_parse_order_status(n);

	if (n == NULL)
		return 0;

	if ((finalize = json_getstr(n, "finalize")) == NULL) {
		warnx("no finalize field in order response");
		return 0;
	}

	if ((order->finalize = strdup(finalize)) == NULL)
		goto err;

	if ((array = json_getarray(n, "authorizations")) == NULL)
		goto err;

	if (array->fields > 0) {
		order->auths = calloc(array->fields, sizeof(*order->auths));
		if (order->auths == NULL) {
			warn("malloc");
			goto err;
		}
		order->authsz = array->fields;
	}

	for (i = 0; i < array->fields; i++) {
		str = json_getarraystr(array->d.array[i]);
		if (str == NULL)
			continue;
		if ((order->auths[i] = strdup(str)) == NULL) {
			warn("strdup");
			goto err;
		}
	}
	return 1;
err:
	json_free_order(order);
	return 0;
}

int
json_parse_upd_order(struct jsmnn *n, struct order *order)
{
	char	*certificate;
	order->status = json_parse_order_status(n);
	if ((certificate = json_getstr(n, "certificate")) != NULL) {
		if ((order->certificate = strdup(certificate)) == NULL)
			return 0;
	}
	return 1;
}

void
json_free_order(struct order *order)
{
	size_t i;

	free(order->finalize);
	order->finalize = NULL;
	for(i = 0; i < order->authsz; i++)
		free(order->auths[i]);
	free(order->auths);

	order->finalize = NULL;
	order->auths = NULL;
	order->authsz = 0;
}

/*
 * Extract the CA paths from the JSON response object.
 * Return zero on failure, non-zero on success.
 */
int
json_parse_capaths(struct jsmnn *n, struct capaths *p)
{
	if (n == NULL)
		return 0;

	p->newaccount = json_getstr(n, "newAccount");
	p->newnonce = json_getstr(n, "newNonce");
	p->neworder = json_getstr(n, "newOrder");
	p->revokecert = json_getstr(n, "revokeCert");

	return p->newaccount != NULL && p->newnonce != NULL &&
	    p->neworder != NULL && p->revokecert != NULL;
}

/*
 * Free up all of our CA-noted paths (which may all be NULL).
 */
void
json_free_capaths(struct capaths *p)
{

	free(p->newaccount);
	free(p->newnonce);
	free(p->neworder);
	free(p->revokecert);
	memset(p, 0, sizeof(struct capaths));
}

/*
 * Parse an HTTP response body from a buffer of size "sz".
 * Returns an opaque pointer on success, otherwise NULL on error.
 */
struct jsmnn *
json_parse(const char *buf, size_t sz)
{
	struct jsmnn	*n;
	jsmn_parser	 p;
	jsmntok_t	*tok, *ntok;
	int		 r;
	size_t		 tokcount;

	jsmn_init(&p);
	tokcount = 128;

	if ((tok = calloc(tokcount, sizeof(jsmntok_t))) == NULL) {
		warn("calloc");
		return NULL;
	}

	/* Do this until we don't need any more tokens. */
again:
	/* Actually try to parse the JSON into the tokens. */
	r = jsmn_parse(&p, buf, sz, tok, tokcount);
	if (r < 0 && r == JSMN_ERROR_NOMEM) {
		if ((ntok = recallocarray(tok, tokcount, tokcount * 2,
		    sizeof(jsmntok_t))) == NULL) {
			warn("calloc");
			free(tok);
			return NULL;
		}
		tok = ntok;
		tokcount *= 2;
		goto again;
	} else if (r < 0) {
		warnx("jsmn_parse: %d", r);
		free(tok);
		return NULL;
	}

	/* Now parse the tokens into a tree. */

	n = jsmntree_alloc(tok, buf, r);
	free(tok);
	return n;
}

/*
 * Format the "newAccount" resource request to check if the account exist.
 */
char *
json_fmt_chkacc(void)
{
	int	 c;
	char	*p;

	c = asprintf(&p, "{"
	    "\"termsOfServiceAgreed\": true, "
	    "\"onlyReturnExisting\": true"
	    "}");
	if (c == -1) {
		warn("asprintf");
		p = NULL;
	}
	return p;
}

/*
 * Format the "newAccount" resource request.
 */
char *
json_fmt_newacc(const char *contact)
{
	int	 c;
	char	*p, *cnt = NULL;

	if (contact != NULL) {
		c = asprintf(&cnt, "\"contact\": [ \"%s\" ], ", contact);
		if (c == -1) {
			warn("asprintf");
			return NULL;
		}
	}

	c = asprintf(&p, "{"
	    "%s"
	    "\"termsOfServiceAgreed\": true"
	    "}", cnt == NULL ? "" : cnt);
	free(cnt);
	if (c == -1) {
		warn("asprintf");
		p = NULL;
	}
	return p;
}

/*
 * Format the "newOrder" resource request
 */
char *
json_fmt_neworder(const char *const *alts, size_t altsz, const char *profile)
{
	size_t	 i;
	int	 c;
	char	*p, *t;

	if ((p = strdup("{ \"identifiers\": [")) == NULL)
		goto err;

	t = p;
	for (i = 0; i < altsz; i++) {
		c = asprintf(&p,
		    "%s { \"type\": \"dns\", \"value\": \"%s\" }%s",
		    t, alts[i], i + 1 == altsz ? "" : ",");
		free(t);
		if (c == -1) {
			warn("asprintf");
			p = NULL;
			goto err;
		}
		t = p;
	}
	if (profile == NULL)
		c = asprintf(&p, "%s ] }", t);
	else
		c = asprintf(&p, "%s ], \"profile\": \"%s\" }", t, profile);
	free(t);
	if (c == -1) {
		warn("asprintf");
		p = NULL;
	}
	return p;
err:
	free(p);
	return NULL;
}

/*
 * Format the revoke resource request.
 */
char *
json_fmt_revokecert(const char *cert)
{
	int	 c;
	char	*p;

	c = asprintf(&p, "{"
	    "\"certificate\": \"%s\""
	    "}",
	    cert);
	if (c == -1) {
		warn("asprintf");
		p = NULL;
	}
	return p;
}

/*
 * Format the "new-cert" resource request.
 */
char *
json_fmt_newcert(const char *cert)
{
	int	 c;
	char	*p;

	c = asprintf(&p, "{"
	    "\"csr\": \"%s\""
	    "}",
	    cert);
	if (c == -1) {
		warn("asprintf");
		p = NULL;
	}
	return p;
}

/*
 * Protected component of json_fmt_signed().
 */
char *
json_fmt_protected_rsa(const char *exp, const char *mod, const char *nce,
    const char *url)
{
	int	 c;
	char	*p;

	c = asprintf(&p, "{"
	    "\"alg\": \"RS256\", "
	    "\"jwk\": "
	    "{\"e\": \"%s\", \"kty\": \"RSA\", \"n\": \"%s\"}, "
	    "\"nonce\": \"%s\", "
	    "\"url\": \"%s\""
	    "}",
	    exp, mod, nce, url);
	if (c == -1) {
		warn("asprintf");
		p = NULL;
	}
	return p;
}

/*
 * Protected component of json_fmt_signed().
 */
char *
json_fmt_protected_ec(const char *x, const char *y, const char *nce,
    const char *url)
{
	int	 c;
	char	*p;

	c = asprintf(&p, "{"
	    "\"alg\": \"ES384\", "
	    "\"jwk\": "
	    "{\"crv\": \"P-384\", \"kty\": \"EC\", \"x\": \"%s\", "
	    "\"y\": \"%s\"}, \"nonce\": \"%s\", \"url\": \"%s\""
	    "}",
	    x, y, nce, url);
	if (c == -1) {
		warn("asprintf");
		p = NULL;
	}
	return p;
}

/*
 * Protected component of json_fmt_signed().
 */
char *
json_fmt_protected_kid(const char *alg, const char *kid, const char *nce,
    const char *url)
{
	int	 c;
	char	*p;

	c = asprintf(&p, "{"
	    "\"alg\": \"%s\", "
	    "\"kid\": \"%s\", "
	    "\"nonce\": \"%s\", "
	    "\"url\": \"%s\""
	    "}",
	    alg, kid, nce, url);
	if (c == -1) {
		warn("asprintf");
		p = NULL;
	}
	return p;
}

/*
 * Signed message contents for the CA server.
 */
char *
json_fmt_signed(const char *protected, const char *payload, const char *digest)
{
	int	 c;
	char	*p;

	c = asprintf(&p, "{"
	    "\"protected\": \"%s\", "
	    "\"payload\": \"%s\", "
	    "\"signature\": \"%s\""
	    "}",
	    protected, payload, digest);
	if (c == -1) {
		warn("asprintf");
		p = NULL;
	}
	return p;
}

/*
 * Produce thumbprint input.
 * This isn't technically a JSON string--it's the input we'll use for
 * hashing and digesting.
 * However, it's in the form of a JSON string, so do it here.
 */
char *
json_fmt_thumb_rsa(const char *exp, const char *mod)
{
	int	 c;
	char	*p;

	/*NOTE: WHITESPACE IS IMPORTANT. */

	c = asprintf(&p, "{\"e\":\"%s\",\"kty\":\"RSA\",\"n\":\"%s\"}",
	    exp, mod);
	if (c == -1) {
		warn("asprintf");
		p = NULL;
	}
	return p;
}

/*
 * Produce thumbprint input.
 * This isn't technically a JSON string--it's the input we'll use for
 * hashing and digesting.
 * However, it's in the form of a JSON string, so do it here.
 */
char *
json_fmt_thumb_ec(const char *x, const char *y)
{
	int	 c;
	char	*p;

	/*NOTE: WHITESPACE IS IMPORTANT. */

	c = asprintf(&p, "{\"crv\":\"P-384\",\"kty\":\"EC\",\"x\":\"%s\","
	    "\"y\":\"%s\"}",
	    x, y);
	if (c == -1) {
		warn("asprintf");
		p = NULL;
	}
	return p;
}
