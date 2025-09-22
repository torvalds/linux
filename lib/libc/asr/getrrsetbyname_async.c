/*	$OpenBSD: getrrsetbyname_async.c,v 1.14 2024/05/07 23:40:53 djm Exp $	*/
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
#include <netinet/in.h>
#include <arpa/nameser.h>
#include <netdb.h>

#include <asr.h>
#include <errno.h>
#include <resolv.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "asr_private.h"

static int getrrsetbyname_async_run(struct asr_query *, struct asr_result *);
static void get_response(struct asr_result *, const char *, int);

struct asr_query *
getrrsetbyname_async(const char *hostname, unsigned int rdclass,
    unsigned int rdtype, unsigned int flags, void *asr)
{
	struct asr_ctx	 *ac;
	struct asr_query *as;

	ac = _asr_use_resolver(asr);
	if ((as = _asr_async_new(ac, ASR_GETRRSETBYNAME)) == NULL)
		goto abort; /* errno set */
	as->as_run = getrrsetbyname_async_run;

	as->as.rrset.flags = flags;
	as->as.rrset.class = rdclass;
	as->as.rrset.type = rdtype;
	as->as.rrset.name = strdup(hostname);
	if (as->as.rrset.name == NULL)
		goto abort; /* errno set */

	_asr_ctx_unref(ac);
	return (as);
    abort:
	if (as)
		_asr_async_free(as);

	_asr_ctx_unref(ac);
	return (NULL);
}
DEF_WEAK(getrrsetbyname_async);

static int
getrrsetbyname_async_run(struct asr_query *as, struct asr_result *ar)
{
    next:
	switch (as->as_state) {

	case ASR_STATE_INIT:

		/* Check for invalid class and type. */
		if (as->as.rrset.class > 0xffff || as->as.rrset.type > 0xffff) {
			ar->ar_rrset_errno = ERRSET_INVAL;
			async_set_state(as, ASR_STATE_HALT);
			break;
		}

		/* Do not allow queries of class or type ANY. */
		if (as->as.rrset.class == 0xff || as->as.rrset.type == 0xff) {
			ar->ar_rrset_errno = ERRSET_INVAL;
			async_set_state(as, ASR_STATE_HALT);
			break;
		}

		/* Do not allow flags yet, unimplemented. */
		if (as->as.rrset.flags) {
			ar->ar_rrset_errno = ERRSET_INVAL;
			async_set_state(as, ASR_STATE_HALT);
			break;
		}

		/* Create a delegate the lookup to a subquery. */
		as->as_subq = _res_query_async_ctx(
		    as->as.rrset.name,
		    as->as.rrset.class,
		    as->as.rrset.type,
		    as->as_ctx);
		if (as->as_subq == NULL) {
			ar->ar_rrset_errno = ERRSET_FAIL;
			async_set_state(as, ASR_STATE_HALT);
			break;
		}

		async_set_state(as, ASR_STATE_SUBQUERY);
		break;

	case ASR_STATE_SUBQUERY:

		if ((asr_run(as->as_subq, ar)) == ASYNC_COND)
			return (ASYNC_COND);

		as->as_subq = NULL;

		/* No packet received.*/
		if (ar->ar_datalen == -1) {
			switch (ar->ar_h_errno) {
			case HOST_NOT_FOUND:
				ar->ar_rrset_errno = ERRSET_NONAME;
				break;
			case NO_DATA:
				ar->ar_rrset_errno = ERRSET_NODATA;
				break;
			default:
				ar->ar_rrset_errno = ERRSET_FAIL;
				break;
			}
			async_set_state(as, ASR_STATE_HALT);
			break;
		}

		/* Got a packet but no answer. */
		if (ar->ar_count == 0) {
			free(ar->ar_data);
			switch (ar->ar_rcode) {
			case NXDOMAIN:
				ar->ar_rrset_errno = ERRSET_NONAME;
				break;
			case NOERROR:
				ar->ar_rrset_errno = ERRSET_NODATA;
				break;
			default:
				ar->ar_rrset_errno = ERRSET_FAIL;
				break;
			}
			async_set_state(as, ASR_STATE_HALT);
			break;
		}

		get_response(ar, ar->ar_data, ar->ar_datalen);
		free(ar->ar_data);
		async_set_state(as, ASR_STATE_HALT);
		break;

	case ASR_STATE_HALT:
		if (ar->ar_rrset_errno)
			ar->ar_rrsetinfo = NULL;
		return (ASYNC_DONE);

	default:
		ar->ar_rrset_errno = ERRSET_FAIL;
		async_set_state(as, ASR_STATE_HALT);
		break;
	}
	goto next;
}

/* The rest of this file is taken from the original implementation. */

/* $OpenBSD: getrrsetbyname_async.c,v 1.14 2024/05/07 23:40:53 djm Exp $ */

/*
 * Copyright (c) 2001 Jakob Schlyter. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Portions Copyright (c) 1999-2001 Internet Software Consortium.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND INTERNET SOFTWARE CONSORTIUM
 * DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL
 * INTERNET SOFTWARE CONSORTIUM BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING
 * FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
 * NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION
 * WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#define MAXPACKET 1024*64

struct dns_query {
	char			*name;
	u_int16_t		type;
	u_int16_t		class;
	struct dns_query	*next;
};

struct dns_rr {
	char			*name;
	u_int16_t		type;
	u_int16_t		class;
	u_int16_t		ttl;
	u_int16_t		size;
	void			*rdata;
	struct dns_rr		*next;
};

struct dns_response {
	HEADER			header;
	struct dns_query	*query;
	struct dns_rr		*answer;
	struct dns_rr		*authority;
	struct dns_rr		*additional;
};

static struct dns_response *parse_dns_response(const u_char *, int);
static struct dns_query *parse_dns_qsection(const u_char *, int,
    const u_char **, int);
static struct dns_rr *parse_dns_rrsection(const u_char *, int, const u_char **,
    int);

static void free_dns_query(struct dns_query *);
static void free_dns_rr(struct dns_rr *);
static void free_dns_response(struct dns_response *);

static int count_dns_rr(struct dns_rr *, u_int16_t, u_int16_t);

static void
get_response(struct asr_result *ar, const char *pkt, int pktlen)
{
	struct rrsetinfo *rrset = NULL;
	struct dns_response *response = NULL;
	struct dns_rr *rr;
	struct rdatainfo *rdata;
	unsigned int index_ans, index_sig;

	/* parse result */
	response = parse_dns_response(pkt, pktlen);
	if (response == NULL) {
		ar->ar_rrset_errno = ERRSET_FAIL;
		goto fail;
	}

	if (response->header.qdcount != 1) {
		ar->ar_rrset_errno = ERRSET_FAIL;
		goto fail;
	}

	/* initialize rrset */
	rrset = calloc(1, sizeof(struct rrsetinfo));
	if (rrset == NULL) {
		ar->ar_rrset_errno = ERRSET_NOMEMORY;
		goto fail;
	}
	rrset->rri_rdclass = response->query->class;
	rrset->rri_rdtype = response->query->type;
	rrset->rri_ttl = response->answer->ttl;
	rrset->rri_nrdatas = response->header.ancount;

	/* check for authenticated data */
	if (response->header.ad == 1)
		rrset->rri_flags |= RRSET_VALIDATED;

	/* copy name from answer section */
	rrset->rri_name = strdup(response->answer->name);
	if (rrset->rri_name == NULL) {
		ar->ar_rrset_errno = ERRSET_NOMEMORY;
		goto fail;
	}

	/* count answers */
	rrset->rri_nrdatas = count_dns_rr(response->answer, rrset->rri_rdclass,
	    rrset->rri_rdtype);
	rrset->rri_nsigs = count_dns_rr(response->answer, rrset->rri_rdclass,
	    T_RRSIG);

	/* allocate memory for answers */
	rrset->rri_rdatas = calloc(rrset->rri_nrdatas,
	    sizeof(struct rdatainfo));
	if (rrset->rri_rdatas == NULL) {
		ar->ar_rrset_errno = ERRSET_NOMEMORY;
		goto fail;
	}

	/* allocate memory for signatures */
	rrset->rri_sigs = calloc(rrset->rri_nsigs, sizeof(struct rdatainfo));
	if (rrset->rri_sigs == NULL) {
		ar->ar_rrset_errno = ERRSET_NOMEMORY;
		goto fail;
	}

	/* copy answers & signatures */
	for (rr = response->answer, index_ans = 0, index_sig = 0;
	    rr; rr = rr->next) {

		rdata = NULL;

		if (rr->class == rrset->rri_rdclass &&
		    rr->type  == rrset->rri_rdtype)
			rdata = &rrset->rri_rdatas[index_ans++];

		if (rr->class == rrset->rri_rdclass &&
		    rr->type  == T_RRSIG)
			rdata = &rrset->rri_sigs[index_sig++];

		if (rdata) {
			rdata->rdi_length = rr->size;
			if (rr->size != 0) {
				rdata->rdi_data = malloc(rr->size);
				if (rdata->rdi_data == NULL) {
					ar->ar_rrset_errno = ERRSET_NOMEMORY;
					goto fail;
				}
				memcpy(rdata->rdi_data, rr->rdata, rr->size);
			}
		}
	}
	free_dns_response(response);

	ar->ar_rrsetinfo = rrset;
	ar->ar_rrset_errno = ERRSET_SUCCESS;
	return;

fail:
	if (rrset != NULL)
		freerrset(rrset);
	if (response != NULL)
		free_dns_response(response);
}

/*
 * DNS response parsing routines
 */
static struct dns_response *
parse_dns_response(const u_char *answer, int size)
{
	struct dns_response *resp;
	const u_char *cp;

	if (size <= HFIXEDSZ)
		return (NULL);

	/* allocate memory for the response */
	resp = calloc(1, sizeof(*resp));
	if (resp == NULL)
		return (NULL);

	/* initialize current pointer */
	cp = answer;

	/* copy header */
	memcpy(&resp->header, cp, HFIXEDSZ);
	cp += HFIXEDSZ;

	/* fix header byte order */
	resp->header.qdcount = ntohs(resp->header.qdcount);
	resp->header.ancount = ntohs(resp->header.ancount);
	resp->header.nscount = ntohs(resp->header.nscount);
	resp->header.arcount = ntohs(resp->header.arcount);

	/* there must be at least one query */
	if (resp->header.qdcount < 1) {
		free_dns_response(resp);
		return (NULL);
	}

	/* parse query section */
	resp->query = parse_dns_qsection(answer, size, &cp,
	    resp->header.qdcount);
	if (resp->header.qdcount && resp->query == NULL) {
		free_dns_response(resp);
		return (NULL);
	}

	/* parse answer section */
	resp->answer = parse_dns_rrsection(answer, size, &cp,
	    resp->header.ancount);
	if (resp->header.ancount && resp->answer == NULL) {
		free_dns_response(resp);
		return (NULL);
	}

	/* parse authority section */
	resp->authority = parse_dns_rrsection(answer, size, &cp,
	    resp->header.nscount);
	if (resp->header.nscount && resp->authority == NULL) {
		free_dns_response(resp);
		return (NULL);
	}

	/* parse additional section */
	resp->additional = parse_dns_rrsection(answer, size, &cp,
	    resp->header.arcount);
	if (resp->header.arcount && resp->additional == NULL) {
		free_dns_response(resp);
		return (NULL);
	}

	return (resp);
}

static struct dns_query *
parse_dns_qsection(const u_char *answer, int size, const u_char **cp, int count)
{
	struct dns_query *head, *curr, *prev;
	int i, length;
	char name[MAXDNAME];

#define NEED(need) \
	do { \
		if (*cp + need > answer + size) \
			goto fail; \
	} while (0)

	for (i = 1, head = NULL, prev = NULL; i <= count; i++, prev = curr) {
		if (*cp >= answer + size) {
 fail:
			free_dns_query(head);
			return (NULL);
		}
		/* allocate and initialize struct */
		curr = calloc(1, sizeof(struct dns_query));
		if (curr == NULL)
			goto fail;
		if (head == NULL)
			head = curr;
		if (prev != NULL)
			prev->next = curr;

		/* name */
		length = dn_expand(answer, answer + size, *cp, name,
		    sizeof(name));
		if (length < 0) {
			free_dns_query(head);
			return (NULL);
		}
		curr->name = strdup(name);
		if (curr->name == NULL) {
			free_dns_query(head);
			return (NULL);
		}
		NEED(length);
		*cp += length;

		/* type */
		NEED(INT16SZ);
		curr->type = _getshort(*cp);
		*cp += INT16SZ;

		/* class */
		NEED(INT16SZ);
		curr->class = _getshort(*cp);
		*cp += INT16SZ;
	}
#undef NEED

	return (head);
}

static struct dns_rr *
parse_dns_rrsection(const u_char *answer, int size, const u_char **cp,
    int count)
{
	struct dns_rr *head, *curr, *prev;
	int i, length;
	char name[MAXDNAME];

#define NEED(need) \
	do { \
		if (*cp + need > answer + size) \
			goto fail; \
	} while (0)

	for (i = 1, head = NULL, prev = NULL; i <= count; i++, prev = curr) {
		if (*cp >= answer + size) {
 fail:
			free_dns_rr(head);
			return (NULL);
		}

		/* allocate and initialize struct */
		curr = calloc(1, sizeof(struct dns_rr));
		if (curr == NULL)
			goto fail;
		if (head == NULL)
			head = curr;
		if (prev != NULL)
			prev->next = curr;

		/* name */
		length = dn_expand(answer, answer + size, *cp, name,
		    sizeof(name));
		if (length < 0) {
			free_dns_rr(head);
			return (NULL);
		}
		curr->name = strdup(name);
		if (curr->name == NULL) {
			free_dns_rr(head);
			return (NULL);
		}
		NEED(length);
		*cp += length;

		/* type */
		NEED(INT16SZ);
		curr->type = _getshort(*cp);
		*cp += INT16SZ;

		/* class */
		NEED(INT16SZ);
		curr->class = _getshort(*cp);
		*cp += INT16SZ;

		/* ttl */
		NEED(INT32SZ);
		curr->ttl = _getlong(*cp);
		*cp += INT32SZ;

		/* rdata size */
		NEED(INT16SZ);
		curr->size = _getshort(*cp);
		*cp += INT16SZ;

		/* rdata itself */
		NEED(curr->size);
		if (curr->size != 0) {
			if ((curr->rdata = malloc(curr->size)) == NULL) {
				free_dns_rr(head);
				return (NULL);
			}
			memcpy(curr->rdata, *cp, curr->size);
		}
		*cp += curr->size;
	}
#undef NEED

	return (head);
}

static void
free_dns_query(struct dns_query *p)
{
	if (p == NULL)
		return;

	if (p->name)
		free(p->name);
	free_dns_query(p->next);
	free(p);
}

static void
free_dns_rr(struct dns_rr *p)
{
	if (p == NULL)
		return;

	if (p->name)
		free(p->name);
	if (p->rdata)
		free(p->rdata);
	free_dns_rr(p->next);
	free(p);
}

static void
free_dns_response(struct dns_response *p)
{
	if (p == NULL)
		return;

	free_dns_query(p->query);
	free_dns_rr(p->answer);
	free_dns_rr(p->authority);
	free_dns_rr(p->additional);
	free(p);
}

static int
count_dns_rr(struct dns_rr *p, u_int16_t class, u_int16_t type)
{
	int n = 0;

	while (p) {
		if (p->class == class && p->type == type)
			n++;
		p = p->next;
	}

	return (n);
}
