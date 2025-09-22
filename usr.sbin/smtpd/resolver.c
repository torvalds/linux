/*	$OpenBSD: resolver.c,v 1.7 2021/06/14 17:58:16 eric Exp $	*/

/*
 * Copyright (c) 2017-2018 Eric Faurot <eric@openbsd.org>
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

#include <sys/socket.h>

#include <netinet/in.h>

#include <asr.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "smtpd.h"
#include "log.h"

#define p_resolver p_lka

struct request {
	SPLAY_ENTRY(request)	 entry;
	uint32_t		 id;
	void			(*cb_ai)(void *, int, struct addrinfo *);
	void			(*cb_ni)(void *, int, const char *, const char *);
	void			(*cb_res)(void *, int, int, int, const void *, int);
	void			*arg;
	struct addrinfo		*ai;
};

struct session {
	uint32_t	 reqid;
	struct mproc	*proc;
	char		*host;
	char		*serv;
};

SPLAY_HEAD(reqtree, request);

static void resolver_init(void);
static void resolver_getaddrinfo_cb(struct asr_result *, void *);
static void resolver_getnameinfo_cb(struct asr_result *, void *);
static void resolver_res_query_cb(struct asr_result *, void *);

static int request_cmp(struct request *, struct request *);
SPLAY_PROTOTYPE(reqtree, request, entry, request_cmp);

static struct reqtree reqs;

void
resolver_getaddrinfo(const char *hostname, const char *servname,
    const struct addrinfo *hints, void (*cb)(void *, int, struct addrinfo *),
    void *arg)
{
	struct request *req;

	resolver_init();

	req = calloc(1, sizeof(*req));
	if (req == NULL) {
		cb(arg, EAI_MEMORY, NULL);
		return;
	}

	while (req->id == 0 || SPLAY_FIND(reqtree, &reqs, req))
		req->id = arc4random();
	req->cb_ai = cb;
	req->arg = arg;

	SPLAY_INSERT(reqtree, &reqs, req);

	m_create(p_resolver, IMSG_GETADDRINFO, req->id, 0, -1);
	m_add_int(p_resolver, hints ? hints->ai_flags : 0);
	m_add_int(p_resolver, hints ? hints->ai_family : 0);
	m_add_int(p_resolver, hints ? hints->ai_socktype : 0);
	m_add_int(p_resolver, hints ? hints->ai_protocol : 0);
	m_add_string(p_resolver, hostname);
	m_add_string(p_resolver, servname);
	m_close(p_resolver);
}

void
resolver_getnameinfo(const struct sockaddr *sa, int flags,
    void(*cb)(void *, int, const char *, const char *), void *arg)
{
	struct request *req;

	resolver_init();

	req = calloc(1, sizeof(*req));
	if (req == NULL) {
		cb(arg, EAI_MEMORY, NULL, NULL);
		return;
	}

	while (req->id == 0 || SPLAY_FIND(reqtree, &reqs, req))
		req->id = arc4random();
	req->cb_ni = cb;
	req->arg = arg;

	SPLAY_INSERT(reqtree, &reqs, req);

	m_create(p_resolver, IMSG_GETNAMEINFO, req->id, 0, -1);
	m_add_sockaddr(p_resolver, sa);
	m_add_int(p_resolver, flags);
	m_close(p_resolver);
}

void
resolver_res_query(const char *dname, int class, int type,
    void (*cb)(void *, int, int, int, const void *, int), void *arg)
{
	struct request *req;

	resolver_init();

	req = calloc(1, sizeof(*req));
	if (req == NULL) {
		cb(arg, NETDB_INTERNAL, 0, 0, NULL, 0);
		return;
	}

	while (req->id == 0 || SPLAY_FIND(reqtree, &reqs, req))
		req->id = arc4random();
	req->cb_res = cb;
	req->arg = arg;

	SPLAY_INSERT(reqtree, &reqs, req);

	m_create(p_resolver, IMSG_RES_QUERY, req->id, 0, -1);
	m_add_string(p_resolver, dname);
	m_add_int(p_resolver, class);
	m_add_int(p_resolver, type);
	m_close(p_resolver);
}

void
resolver_dispatch_request(struct mproc *proc, struct imsg *imsg)
{
	const char *hostname, *servname, *dname;
	struct session *s;
	struct asr_query *q;
	struct addrinfo hints;
	struct sockaddr_storage ss;
	struct sockaddr *sa;
	struct msg m;
	uint32_t reqid;
	int class, type, flags, save_errno;

	reqid = imsg->hdr.peerid;
	m_msg(&m, imsg);

	switch (imsg->hdr.type) {

	case IMSG_GETADDRINFO:
		servname = NULL;
		memset(&hints, 0 , sizeof(hints));
		m_get_int(&m, &hints.ai_flags);
		m_get_int(&m, &hints.ai_family);
		m_get_int(&m, &hints.ai_socktype);
		m_get_int(&m, &hints.ai_protocol);
		m_get_string(&m, &hostname);
		m_get_string(&m, &servname);
		m_end(&m);

		s = NULL;
		q = NULL;
		if ((s = calloc(1, sizeof(*s))) &&
		    (q = getaddrinfo_async(hostname, servname, &hints, NULL)) &&
		    (event_asr_run(q, resolver_getaddrinfo_cb, s))) {
			s->reqid = reqid;
			s->proc = proc;
			break;
		}
		save_errno = errno;

		if (q)
			asr_abort(q);
		if (s)
			free(s);

		m_create(proc, IMSG_GETADDRINFO_END, reqid, 0, -1);
		m_add_int(proc, EAI_SYSTEM);
		m_add_int(proc, save_errno);
		m_close(proc);
		break;

	case IMSG_GETNAMEINFO:
		sa = (struct sockaddr*)&ss;
		m_get_sockaddr(&m, sa);
		m_get_int(&m, &flags);
		m_end(&m);

		s = NULL;
		q = NULL;
		if ((s = calloc(1, sizeof(*s))) &&
		    (s->host = malloc(NI_MAXHOST)) &&
		    (s->serv = malloc(NI_MAXSERV)) &&
		    (q = getnameinfo_async(sa, sa->sa_len, s->host, NI_MAXHOST,
			s->serv, NI_MAXSERV, flags, NULL)) &&
		    (event_asr_run(q, resolver_getnameinfo_cb, s))) {
			s->reqid = reqid;
			s->proc = proc;
			break;
		}
		save_errno = errno;

		if (q)
			asr_abort(q);
		if (s) {
			free(s->host);
			free(s->serv);
			free(s);
		}

		m_create(proc, IMSG_GETNAMEINFO, reqid, 0, -1);
		m_add_int(proc, EAI_SYSTEM);
		m_add_int(proc, save_errno);
		m_add_string(proc, NULL);
		m_add_string(proc, NULL);
		m_close(proc);
		break;

	case IMSG_RES_QUERY:
		m_get_string(&m, &dname);
		m_get_int(&m, &class);
		m_get_int(&m, &type);
		m_end(&m);

		s = NULL;
		q = NULL;
		if ((s = calloc(1, sizeof(*s))) &&
		    (q = res_query_async(dname, class, type, NULL)) &&
		    (event_asr_run(q, resolver_res_query_cb, s))) {
			s->reqid = reqid;
			s->proc = proc;
			break;
		}
		save_errno = errno;

		if (q)
			asr_abort(q);
		if (s)
			free(s);

		m_create(proc, IMSG_RES_QUERY, reqid, 0, -1);
		m_add_int(proc, NETDB_INTERNAL);
		m_add_int(proc, save_errno);
		m_add_int(proc, 0);
		m_add_int(proc, 0);
		m_add_data(proc, NULL, 0);
		m_close(proc);
		break;

	default:
		fatalx("%s: %s", __func__, imsg_to_str(imsg->hdr.type));
	}
}

static struct addrinfo *
_alloc_addrinfo(const struct addrinfo *ai0, const struct sockaddr *sa,
    const char *cname)
{
	struct addrinfo *ai;

	ai = calloc(1, sizeof(*ai) + sa->sa_len);
	if (ai == NULL) {
		log_warn("%s: calloc", __func__);
		return NULL;
	}
	*ai = *ai0;
	ai->ai_addr = (void *)(ai + 1);
	memcpy(ai->ai_addr, sa, sa->sa_len);

	if (cname) {
		ai->ai_canonname = strdup(cname);
		if (ai->ai_canonname == NULL) {
			log_warn("%s: strdup", __func__);
			free(ai);
			return NULL;
		}
	}

	return ai;
}

void
resolver_dispatch_result(struct mproc *proc, struct imsg *imsg)
{
	struct request key, *req;
	struct sockaddr_storage ss;
	struct addrinfo *ai, tai;
	struct msg m;
	const char *cname, *host, *serv;
	const void *data;
	size_t datalen;
	int gai_errno, herrno, rcode, count;

	key.id = imsg->hdr.peerid;
	req = SPLAY_FIND(reqtree, &reqs, &key);
	if (req == NULL)
		fatalx("%s: unknown request %08x", __func__, imsg->hdr.peerid);

	m_msg(&m, imsg);

	switch (imsg->hdr.type) {

	case IMSG_GETADDRINFO:
		memset(&tai, 0, sizeof(tai));
		m_get_int(&m, &tai.ai_flags);
		m_get_int(&m, &tai.ai_family);
		m_get_int(&m, &tai.ai_socktype);
		m_get_int(&m, &tai.ai_protocol);
		m_get_sockaddr(&m, (struct sockaddr *)&ss);
		m_get_string(&m, &cname);
		m_end(&m);

		ai = _alloc_addrinfo(&tai, (struct sockaddr *)&ss, cname);
		if (ai) {
			ai->ai_next = req->ai;
			req->ai = ai;
		}
		break;

	case IMSG_GETADDRINFO_END:
		m_get_int(&m, &gai_errno);
		m_get_int(&m, &errno);
		m_end(&m);

		SPLAY_REMOVE(reqtree, &reqs, req);
		req->cb_ai(req->arg, gai_errno, req->ai);
		free(req);
		break;

	case IMSG_GETNAMEINFO:
		m_get_int(&m, &gai_errno);
		m_get_int(&m, &errno);
		m_get_string(&m, &host);
		m_get_string(&m, &serv);
		m_end(&m);

		SPLAY_REMOVE(reqtree, &reqs, req);
		req->cb_ni(req->arg, gai_errno, host, serv);
		free(req);
		break;

	case IMSG_RES_QUERY:
		m_get_int(&m, &herrno);
		m_get_int(&m, &errno);
		m_get_int(&m, &rcode);
		m_get_int(&m, &count);
		m_get_data(&m, &data, &datalen);
		m_end(&m);

		SPLAY_REMOVE(reqtree, &reqs, req);
		req->cb_res(req->arg, herrno, rcode, count, data, datalen);
		free(req);
		break;
	}
}

static void
resolver_init(void)
{
	static int init = 0;

	if (init == 0) {
		SPLAY_INIT(&reqs);
		init = 1;
	}
}

static void
resolver_getaddrinfo_cb(struct asr_result *ar, void *arg)
{
	struct session *s = arg;
	struct addrinfo *ai;

	for (ai = ar->ar_addrinfo; ai; ai = ai->ai_next) {
		m_create(s->proc, IMSG_GETADDRINFO, s->reqid, 0, -1);
		m_add_int(s->proc, ai->ai_flags);
		m_add_int(s->proc, ai->ai_family);
		m_add_int(s->proc, ai->ai_socktype);
		m_add_int(s->proc, ai->ai_protocol);
		m_add_sockaddr(s->proc, ai->ai_addr);
		m_add_string(s->proc, ai->ai_canonname);
		m_close(s->proc);
	}

	m_create(s->proc, IMSG_GETADDRINFO_END, s->reqid, 0, -1);
	m_add_int(s->proc, ar->ar_gai_errno);
	m_add_int(s->proc, ar->ar_errno);
	m_close(s->proc);

	if (ar->ar_addrinfo)
		freeaddrinfo(ar->ar_addrinfo);
	free(s);
}

static void
resolver_getnameinfo_cb(struct asr_result *ar, void *arg)
{
	struct session *s = arg;

	m_create(s->proc, IMSG_GETNAMEINFO, s->reqid, 0, -1);
	m_add_int(s->proc, ar->ar_gai_errno);
	m_add_int(s->proc, ar->ar_errno);
	m_add_string(s->proc, ar->ar_gai_errno ? NULL : s->host);
	m_add_string(s->proc, ar->ar_gai_errno ? NULL : s->serv);
	m_close(s->proc);

	free(s->host);
	free(s->serv);
	free(s);
}

static void
resolver_res_query_cb(struct asr_result *ar, void *arg)
{
	struct session *s = arg;

	m_create(s->proc, IMSG_RES_QUERY, s->reqid, 0, -1);
	m_add_int(s->proc, ar->ar_h_errno);
	m_add_int(s->proc, ar->ar_errno);
	m_add_int(s->proc, ar->ar_rcode);
	m_add_int(s->proc, ar->ar_count);
	m_add_data(s->proc, ar->ar_data, ar->ar_datalen);
	m_close(s->proc);

	free(ar->ar_data);
	free(s);
}

static int
request_cmp(struct request *a, struct request *b)
{
	if (a->id < b->id)
		return -1;
	if (a->id > b->id)
		return 1;
	return 0;
}

SPLAY_GENERATE(reqtree, request, entry, request_cmp);
