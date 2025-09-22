/*	$OpenBSD: resolver.c,v 1.4 2019/04/06 10:35:48 eric Exp $	*/

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

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/tree.h>
#include <sys/queue.h>
#include <netinet/in.h>

#include <asr.h>
#include <ctype.h>
#include <errno.h>
#include <imsg.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "lpd.h"

#include "log.h"
#include "proc.h"

#define p_resolver p_engine

struct request {
	SPLAY_ENTRY(request)	 entry;
	uint32_t		 id;
	void			(*cb_ai)(void *, int, struct addrinfo *);
	void			(*cb_ni)(void *, int, const char *, const char *);
	void			*arg;
	struct addrinfo		*ai;
};

struct session {
	uint32_t	 reqid;
	struct imsgproc	*proc;
	char		*host;
	char		*serv;
};

SPLAY_HEAD(reqtree, request);

static void resolver_init(void);
static void resolver_getaddrinfo_cb(struct asr_result *, void *);
static void resolver_getnameinfo_cb(struct asr_result *, void *);

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
resolver_dispatch_request(struct imsgproc *proc, struct imsg *imsg)
{
	const char *hostname, *servname;
	struct session *s;
	struct asr_query *q;
	struct addrinfo hints;
	struct sockaddr_storage ss;
	struct sockaddr *sa;
	uint32_t reqid;
	int flags, save_errno;

	reqid = imsg->hdr.peerid;

	switch (imsg->hdr.type) {

	case IMSG_GETADDRINFO:
		servname = NULL;
		memset(&hints, 0 , sizeof(hints));
		m_get_int(proc, &hints.ai_flags);
		m_get_int(proc, &hints.ai_family);
		m_get_int(proc, &hints.ai_socktype);
		m_get_int(proc, &hints.ai_protocol);
		m_get_string(proc, &hostname);
		m_get_string(proc, &servname);
		m_end(proc);

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
		m_get_sockaddr(proc, sa);
		m_get_int(proc, &flags);
		m_end(proc);

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

	default:
		fatalx("%s: %s", __func__, log_fmt_imsgtype(imsg->hdr.type));
	}
}

void
resolver_dispatch_result(struct imsgproc *proc, struct imsg *imsg)
{
	struct request key, *req;
	struct sockaddr_storage ss;
	struct addrinfo *ai;
	const char *cname, *host, *serv;
	int gai_errno;

	key.id = imsg->hdr.peerid;
	req = SPLAY_FIND(reqtree, &reqs, &key);
	if (req == NULL)
		fatalx("%s: unknown request %08x", __func__, imsg->hdr.peerid);

	switch (imsg->hdr.type) {

	case IMSG_GETADDRINFO:
		ai = calloc(1, sizeof(*ai));
		if (ai == NULL) {
			log_warn("%s: calloc", __func__);
			break;
		}
		m_get_int(proc, &ai->ai_flags);
		m_get_int(proc, &ai->ai_family);
		m_get_int(proc, &ai->ai_socktype);
		m_get_int(proc, &ai->ai_protocol);
		m_get_sockaddr(proc, (struct sockaddr *)&ss);
		m_get_string(proc, &cname);
		m_end(proc);

		ai->ai_addr = malloc(ss.ss_len);
		if (ai->ai_addr == NULL) {
			log_warn("%s: malloc", __func__);
			free(ai);
			break;
		}

		memmove(ai->ai_addr, &ss, ss.ss_len);

		if (cname) {
			ai->ai_canonname = strdup(cname);
			if (ai->ai_canonname == NULL) {
				log_warn("%s: strdup", __func__);
				free(ai->ai_addr);
				free(ai);
				break;
			}
		}

		ai->ai_next = req->ai;
		req->ai = ai;
		break;

	case IMSG_GETADDRINFO_END:
		m_get_int(proc, &gai_errno);
		m_get_int(proc, &errno);
		m_end(proc);

		SPLAY_REMOVE(reqtree, &reqs, req);
		req->cb_ai(req->arg, gai_errno, req->ai);
		free(req);
		break;

	case IMSG_GETNAMEINFO:
		m_get_int(proc, &gai_errno);
		m_get_int(proc, &errno);
		m_get_string(proc, &host);
		m_get_string(proc, &serv);
		m_end(proc);

		SPLAY_REMOVE(reqtree, &reqs, req);
		req->cb_ni(req->arg, gai_errno, host, serv);
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
