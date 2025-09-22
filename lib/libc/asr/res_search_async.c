/*	$OpenBSD: res_search_async.c,v 1.21 2017/02/27 10:44:46 jca Exp $	*/
/*
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

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <arpa/nameser.h>
#include <netdb.h>

#include <asr.h>
#include <errno.h>
#include <resolv.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "asr_private.h"

static int res_search_async_run(struct asr_query *, struct asr_result *);
static size_t domcat(const char *, const char *, char *, size_t);

/*
 * Unlike res_query_async(), this function returns a valid packet only if
 * h_errno is NETDB_SUCCESS.
 */
struct asr_query *
res_search_async(const char *name, int class, int type, void *asr)
{
	struct asr_ctx	 *ac;
	struct asr_query *as;

	DPRINT("asr: res_search_async(\"%s\", %i, %i)\n", name, class, type);

	ac = _asr_use_resolver(asr);
	as = _res_search_async_ctx(name, class, type, ac);
	_asr_ctx_unref(ac);

	return (as);
}
DEF_WEAK(res_search_async);

struct asr_query *
_res_search_async_ctx(const char *name, int class, int type, struct asr_ctx *ac)
{
	struct asr_query	*as;

	DPRINT("asr: res_search_async_ctx(\"%s\", %i, %i)\n", name, class,
	    type);

	if ((as = _asr_async_new(ac, ASR_SEARCH)) == NULL)
		goto err; /* errno set */
	as->as_run  = res_search_async_run;
	if ((as->as.search.name = strdup(name)) == NULL)
		goto err; /* errno set */

	as->as.search.class = class;
	as->as.search.type = type;

	return (as);
    err:
	if (as)
		_asr_async_free(as);
	return (NULL);
}

#define HERRNO_UNSET	-2

static int
res_search_async_run(struct asr_query *as, struct asr_result *ar)
{
	int	r;
	char	fqdn[MAXDNAME];

    next:
	switch (as->as_state) {

	case ASR_STATE_INIT:

		if (as->as.search.name[0] == '\0') {
			ar->ar_h_errno = NO_DATA;
			async_set_state(as, ASR_STATE_HALT);
			break;
		}

		as->as.search.saved_h_errno = HERRNO_UNSET;
		async_set_state(as, ASR_STATE_NEXT_DOMAIN);
		break;

	case ASR_STATE_NEXT_DOMAIN:
		/*
		 * Reset flags to be able to identify the case in
		 * STATE_SUBQUERY.
		 */
		as->as_dom_flags = 0;

		r = _asr_iter_domain(as, as->as.search.name, fqdn, sizeof(fqdn));
		if (r == -1) {
			async_set_state(as, ASR_STATE_NOT_FOUND);
			break;
		}
		if (r == 0) {
			ar->ar_errno = EINVAL;
			ar->ar_h_errno = NO_RECOVERY;
			ar->ar_datalen = -1;
			ar->ar_data = NULL;
			async_set_state(as, ASR_STATE_HALT);
			break;
		}
		as->as_subq = _res_query_async_ctx(fqdn,
		    as->as.search.class, as->as.search.type, as->as_ctx);
		if (as->as_subq == NULL) {
			ar->ar_errno = errno;
			if (errno == EINVAL)
				ar->ar_h_errno = NO_RECOVERY;
			else
				ar->ar_h_errno = NETDB_INTERNAL;
			ar->ar_datalen = -1;
			ar->ar_data = NULL;
			async_set_state(as, ASR_STATE_HALT);
			break;
		}
		async_set_state(as, ASR_STATE_SUBQUERY);
		break;

	case ASR_STATE_SUBQUERY:

		if ((r = asr_run(as->as_subq, ar)) == ASYNC_COND)
			return (ASYNC_COND);
		as->as_subq = NULL;

		if (ar->ar_h_errno == NETDB_SUCCESS) {
			async_set_state(as, ASR_STATE_HALT);
			break;
		}

		/*
		 * The original res_search() does this in the domain search
		 * loop, but only for ECONNREFUSED. I think we can do better
		 * because technically if we get an errno, it means
		 * we couldn't reach any nameserver, so there is no point
		 * in trying further.
		 */
		if (ar->ar_errno) {
			async_set_state(as, ASR_STATE_HALT);
			break;
		}

		free(ar->ar_data);

		/*
		 * The original resolver does something like this.
		 */
		if (as->as_dom_flags & ASYNC_DOM_NDOTS)
			as->as.search.saved_h_errno = ar->ar_h_errno;

		if (as->as_dom_flags & ASYNC_DOM_DOMAIN) {
			if (ar->ar_h_errno == NO_DATA)
				as->as_flags |= ASYNC_NODATA;
			else if (ar->ar_h_errno == TRY_AGAIN)
				as->as_flags |= ASYNC_AGAIN;
		}

		async_set_state(as, ASR_STATE_NEXT_DOMAIN);
		break;

	case ASR_STATE_NOT_FOUND:

		if (as->as.search.saved_h_errno != HERRNO_UNSET)
			ar->ar_h_errno = as->as.search.saved_h_errno;
		else if (as->as_flags & ASYNC_NODATA)
			ar->ar_h_errno = NO_DATA;
		else if (as->as_flags & ASYNC_AGAIN)
			ar->ar_h_errno = TRY_AGAIN;
		/*
		 * Else, we got the ar_h_errno value set by res_query_async()
		 * for the last domain.
		 */
		ar->ar_datalen = -1;
		ar->ar_data = NULL;
		async_set_state(as, ASR_STATE_HALT);
		break;

	case ASR_STATE_HALT:

		return (ASYNC_DONE);

	default:
		ar->ar_errno = EOPNOTSUPP;
		ar->ar_h_errno = NETDB_INTERNAL;
		async_set_state(as, ASR_STATE_HALT);
		break;
	}
	goto next;
}

/*
 * Concatenate a name and a domain name. The result has no trailing dot.
 * Return the resulting string length, or 0 in case of error.
 */
static size_t
domcat(const char *name, const char *domain, char *buf, size_t buflen)
{
	size_t	r;

	r = _asr_make_fqdn(name, domain, buf, buflen);
	if (r == 0)
		return (0);
	buf[r - 1] = '\0';

	return (r - 1);
}

enum {
	DOM_INIT,
	DOM_DOMAIN,
	DOM_DONE
};

/*
 * Implement the search domain strategy.
 *
 * This function works as a generator that constructs complete domains in
 * buffer "buf" of size "len" for the given host name "name", according to the
 * search rules defined by the resolving context.  It is supposed to be called
 * multiple times (with the same name) to generate the next possible domain
 * name, if any.
 *
 * It returns -1 if all possibilities have been exhausted, 0 if there was an
 * error generating the next name, or the resulting name length.
 */
int
_asr_iter_domain(struct asr_query *as, const char *name, char * buf, size_t len)
{
	const char	*c;
	int		 dots;

	switch (as->as_dom_step) {

	case DOM_INIT:
		/* First call */

		/*
		 * If "name" is an FQDN, that's the only result and we
		 * don't try anything else.
		 */
		if (strlen(name) && name[strlen(name) - 1] ==  '.') {
			DPRINT("asr: iter_domain(\"%s\") fqdn\n", name);
			as->as_dom_flags |= ASYNC_DOM_FQDN;
			as->as_dom_step = DOM_DONE;
			return (domcat(name, NULL, buf, len));
		}

		/*
		 * Otherwise, we iterate through the specified search domains.
		 */
		as->as_dom_step = DOM_DOMAIN;
		as->as_dom_idx = 0;

		/*
		 * If "name" as enough dots, use it as-is first, as indicated
		 * in resolv.conf(5).
		 */
		dots = 0;
		for (c = name; *c; c++)
			dots += (*c == '.');
		if (dots >= as->as_ctx->ac_ndots) {
			DPRINT("asr: iter_domain(\"%s\") ndots\n", name);
			as->as_dom_flags |= ASYNC_DOM_NDOTS;
			if (strlcpy(buf, name, len) >= len)
				return (0);
			return (strlen(buf));
		}
		/* Otherwise, starts using the search domains */
		/* FALLTHROUGH */

	case DOM_DOMAIN:
		if (as->as_dom_idx < as->as_ctx->ac_domcount &&
		    (as->as_ctx->ac_options & RES_DNSRCH || (
			as->as_ctx->ac_options & RES_DEFNAMES &&
			as->as_dom_idx == 0 &&
			strchr(name, '.') == NULL))) {
			DPRINT("asr: iter_domain(\"%s\") domain \"%s\"\n",
			    name, as->as_ctx->ac_dom[as->as_dom_idx]);
			as->as_dom_flags |= ASYNC_DOM_DOMAIN;
			return (domcat(name,
			    as->as_ctx->ac_dom[as->as_dom_idx++], buf, len));
		}

		/* No more domain to try. */

		as->as_dom_step = DOM_DONE;

		/*
		 * If the name was not tried as an absolute name before,
		 * do it now.
		 */
		if (!(as->as_dom_flags & ASYNC_DOM_NDOTS)) {
			DPRINT("asr: iter_domain(\"%s\") as is\n", name);
			as->as_dom_flags |= ASYNC_DOM_ASIS;
			if (strlcpy(buf, name, len) >= len)
				return (0);
			return (strlen(buf));
		}
		/* Otherwise, we are done. */

	case DOM_DONE:
	default:
		DPRINT("asr: iter_domain(\"%s\") done\n", name);
		return (-1);
	}
}
