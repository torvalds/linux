/*	$OpenBSD: namespace.c,v 1.20 2020/03/05 07:39:25 martijn Exp $ */

/*
 * Copyright (c) 2009, 2010 Martin Hedenfalk <martin@bzero.se>
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
#include <sys/queue.h>

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "ldapd.h"
#include "log.h"

extern char		*datadir;

/* Maximum number of requests to queue per namespace during compaction.
 * After this many requests, we return LDAP_BUSY.
 */
#define MAX_REQUEST_QUEUE	 10000

static struct btval	*namespace_find(struct namespace *ns, char *dn);
static void		 namespace_queue_replay(int fd, short event, void *arg);
static int		 namespace_set_fd(struct namespace *ns,
			    struct btree **bt, int fd, unsigned int flags);

int
namespace_begin_txn(struct namespace *ns, struct btree_txn **data_txn,
    struct btree_txn **indx_txn, int rdonly)
{
	if (ns->data_db == NULL || ns->indx_db == NULL) {
		errno = EBUSY;	/* namespace is being reopened */
		return -1;
	}

	if ((*data_txn = btree_txn_begin(ns->data_db, rdonly)) == NULL ||
	    (*indx_txn = btree_txn_begin(ns->indx_db, rdonly)) == NULL) {
		if (errno == ESTALE) {
			if (*data_txn == NULL)
				namespace_reopen_data(ns);
			else
				namespace_reopen_indx(ns);
			errno = EBUSY;
		}
		log_warn("failed to open transaction");
		btree_txn_abort(*data_txn);
		*data_txn = NULL;
		return -1;
	}

	return 0;
}

int
namespace_begin(struct namespace *ns)
{
	return namespace_begin_txn(ns, &ns->data_txn, &ns->indx_txn, 0);
}

int
namespace_commit(struct namespace *ns)
{
	if (ns->indx_txn != NULL &&
	    btree_txn_commit(ns->indx_txn) != BT_SUCCESS) {
		log_warn("%s(indx): commit failed", ns->suffix);
		btree_txn_abort(ns->data_txn);
		ns->indx_txn = ns->data_txn = NULL;
		return -1;
	}
	ns->indx_txn = NULL;

	if (ns->data_txn != NULL &&
	    btree_txn_commit(ns->data_txn) != BT_SUCCESS) {
		log_warn("%s(data): commit failed", ns->suffix);
		ns->data_txn = NULL;
		return -1;
	}
	ns->data_txn = NULL;

	return 0;
}

void
namespace_abort(struct namespace *ns)
{
	btree_txn_abort(ns->data_txn);
	ns->data_txn = NULL;

	btree_txn_abort(ns->indx_txn);
	ns->indx_txn = NULL;
}

int
namespace_open(struct namespace *ns)
{
	unsigned int	 db_flags = 0;

	assert(ns);
	assert(ns->suffix);

	if (ns->sync == 0)
		db_flags |= BT_NOSYNC;

	if (asprintf(&ns->data_path, "%s/%s_data.db", datadir, ns->suffix) == -1)
		return -1;
	log_info("opening namespace %s", ns->suffix);
	ns->data_db = btree_open(ns->data_path, db_flags | BT_REVERSEKEY, 0644);
	if (ns->data_db == NULL)
		return -1;

	btree_set_cache_size(ns->data_db, ns->cache_size);

	if (asprintf(&ns->indx_path, "%s/%s_indx.db", datadir, ns->suffix) == -1)
		return -1;
	ns->indx_db = btree_open(ns->indx_path, db_flags, 0644);
	if (ns->indx_db == NULL)
		return -1;

	btree_set_cache_size(ns->indx_db, ns->index_cache_size);

	/* prepare request queue scheduler */
	evtimer_set(&ns->ev_queue, namespace_queue_replay, ns);

	return 0;
}

static int
namespace_reopen(const char *path)
{
	struct open_req		 req;

	log_debug("asking parent to open %s", path);

	memset(&req, 0, sizeof(req));
	if (strlcpy(req.path, path, sizeof(req.path)) >= sizeof(req.path)) {
		log_warnx("%s: path truncated", __func__);
		return -1;
	}

	return imsgev_compose(iev_ldapd, IMSG_LDAPD_OPEN, 0, 0, -1, &req,
	    sizeof(req));
}

int
namespace_reopen_data(struct namespace *ns)
{
	if (ns->data_db != NULL) {
		btree_close(ns->data_db);
		ns->data_db = NULL;
		return namespace_reopen(ns->data_path);
	}
	return 1;
}

int
namespace_reopen_indx(struct namespace *ns)
{
	if (ns->indx_db != NULL) {
		btree_close(ns->indx_db);
		ns->indx_db = NULL;
		return namespace_reopen(ns->indx_path);
	}
	return 1;
}

static int
namespace_set_fd(struct namespace *ns, struct btree **bt, int fd,
    unsigned int flags)
{
	log_info("reopening namespace %s (entries)", ns->suffix);
	btree_close(*bt);
	if (ns->sync == 0)
		flags |= BT_NOSYNC;
	*bt = btree_open_fd(fd, flags);
	if (*bt == NULL)
		return -1;
	return 0;
}

int
namespace_set_data_fd(struct namespace *ns, int fd)
{
	return namespace_set_fd(ns, &ns->data_db, fd, BT_REVERSEKEY);
}

int
namespace_set_indx_fd(struct namespace *ns, int fd)
{
	return namespace_set_fd(ns, &ns->indx_db, fd, 0);
}

void
namespace_close(struct namespace *ns)
{
	struct conn		*conn;
	struct search		*search, *next;
	struct request		*req;

	/* Cancel any queued requests for this namespace.
	 */
	if (ns->queued_requests > 0) {
		log_warnx("cancelling %u queued requests on namespace %s",
		    ns->queued_requests, ns->suffix);
		while ((req = TAILQ_FIRST(&ns->request_queue)) != NULL) {
			TAILQ_REMOVE(&ns->request_queue, req, next);
			ldap_respond(req, LDAP_UNAVAILABLE);
		}
	}

	/* Cancel any searches on this namespace.
	 */
	TAILQ_FOREACH(conn, &conn_list, next) {
		for (search = TAILQ_FIRST(&conn->searches); search != NULL;
		    search = next) {
			next = TAILQ_NEXT(search, next);
			if (search->ns == ns)
				search_close(search);
		}
	}

	free(ns->suffix);
	btree_close(ns->data_db);
	btree_close(ns->indx_db);
	if (evtimer_pending(&ns->ev_queue, NULL))
		evtimer_del(&ns->ev_queue);
	free(ns->data_path);
	free(ns->indx_path);
	free(ns);
}

void
namespace_remove(struct namespace *ns)
{
	TAILQ_REMOVE(&conf->namespaces, ns, next);
	namespace_close(ns);
}

static struct btval *
namespace_find(struct namespace *ns, char *dn)
{
	struct btval		 key;
	static struct btval	 val;

	if (ns->data_db == NULL) {
		errno = EBUSY;	/* namespace is being reopened */
		return NULL;
	}

	memset(&key, 0, sizeof(key));
	memset(&val, 0, sizeof(val));

	key.data = dn;
	key.size = strlen(dn);

	if (btree_txn_get(ns->data_db, ns->data_txn, &key, &val) != 0) {
		if (errno == ENOENT)
			log_debug("%s: dn not found", dn);
		else
			log_warn("%s", dn);

		if (errno == ESTALE)
			namespace_reopen_data(ns);

		return NULL;
	}

	return &val;
}

struct ber_element *
namespace_get(struct namespace *ns, char *dn)
{
	struct ber_element	*elm;
	struct btval		*val;

	if ((val = namespace_find(ns, dn)) == NULL)
		return NULL;

	elm = namespace_db2ber(ns, val);
	btval_reset(val);
	return elm;
}

int
namespace_exists(struct namespace *ns, char *dn)
{
	struct btval		*val;

	if ((val = namespace_find(ns, dn)) == NULL)
		return 0;
	btval_reset(val);
	return 1;
}

int
namespace_ber2db(struct namespace *ns, struct ber_element *root,
    struct btval *val)
{
	return ber2db(root, val, ns->compression_level);
}

struct ber_element *
namespace_db2ber(struct namespace *ns, struct btval *val)
{
	return db2ber(val, ns->compression_level);
}

static int
namespace_put(struct namespace *ns, char *dn, struct ber_element *root,
    int update)
{
	int			 rc;
	struct btval		 key, val;

	assert(ns != NULL);
	assert(ns->data_txn != NULL);
	assert(ns->indx_txn != NULL);

	memset(&key, 0, sizeof(key));
	key.data = dn;
	key.size = strlen(dn);

	if (namespace_ber2db(ns, root, &val) != 0)
		return BT_FAIL;

	rc = btree_txn_put(NULL, ns->data_txn, &key, &val,
	    update ? 0 : BT_NOOVERWRITE);
	if (rc != BT_SUCCESS) {
		if (errno == EEXIST)
			log_debug("%s: already exists", dn);
		else
			log_warn("%s", dn);
		goto done;
	}

	/* FIXME: if updating, try harder to just update changed indices.
	 */
	if (update && (rc = unindex_entry(ns, &key, root)) != BT_SUCCESS)
		goto done;

	rc = index_entry(ns, &key, root);

done:
	btval_reset(&val);
	return rc;
}

int
namespace_add(struct namespace *ns, char *dn, struct ber_element *root)
{
	return namespace_put(ns, dn, root, 0);
}

int
namespace_update(struct namespace *ns, char *dn, struct ber_element *root)
{
	return namespace_put(ns, dn, root, 1);
}

int
namespace_del(struct namespace *ns, char *dn)
{
	int			 rc;
	struct ber_element	*root;
	struct btval		 key, data;

	assert(ns != NULL);
	assert(ns->indx_txn != NULL);
	assert(ns->data_txn != NULL);

	memset(&key, 0, sizeof(key));
	memset(&data, 0, sizeof(data));

	key.data = dn;
	key.size = strlen(key.data);

	rc = btree_txn_del(NULL, ns->data_txn, &key, &data);
	if (rc == BT_SUCCESS && (root = namespace_db2ber(ns, &data)) != NULL)
		rc = unindex_entry(ns, &key, root);

	btval_reset(&data);
	return rc;
}

int
namespace_has_referrals(struct namespace *ns)
{
	return !SLIST_EMPTY(&ns->referrals);
}

struct namespace *
namespace_lookup_base(const char *basedn, int include_referrals)
{
	size_t			 blen, slen;
	struct namespace	*ns, *matched_ns = NULL;

	assert(basedn);
	blen = strlen(basedn);

	TAILQ_FOREACH(ns, &conf->namespaces, next) {
		slen = strlen(ns->suffix);
		if ((include_referrals || !namespace_has_referrals(ns)) &&
		    blen >= slen &&
		    bcmp(basedn + blen - slen, ns->suffix, slen) == 0) {
			/* Match the longest namespace suffix. */
			if (matched_ns == NULL ||
			    strlen(ns->suffix) > strlen(matched_ns->suffix))
				matched_ns = ns;
		}
	}

	return matched_ns;
}

struct namespace *
namespace_for_base(const char *basedn)
{
	return namespace_lookup_base(basedn, 0);
}

struct referrals *
namespace_referrals(const char *basedn)
{
	struct namespace	*ns;

	if ((ns = namespace_lookup_base(basedn, 1)) != NULL &&
	    namespace_has_referrals(ns))
		return &ns->referrals;

	if (!SLIST_EMPTY(&conf->referrals))
		return &conf->referrals;

	return NULL;
}

int
namespace_has_index(struct namespace *ns, const char *attr,
    enum index_type type)
{
	struct attr_index	*ai;

	assert(ns);
	assert(attr);
	TAILQ_FOREACH(ai, &ns->indices, next) {
		if (strcasecmp(attr, ai->attr) == 0 && ai->type == type)
			return 1;
	}

	return 0;
}

/* Queues modification requests while the namespace is being reopened.
 */
int
namespace_queue_request(struct namespace *ns, struct request *req)
{
	if (ns->queued_requests > MAX_REQUEST_QUEUE) {
		log_warn("%u requests already queued, sorry",
		    ns->queued_requests);
		return -1;
	}

	TAILQ_INSERT_TAIL(&ns->request_queue, req, next);
	ns->queued_requests++;

	if (!evtimer_pending(&ns->ev_queue, NULL))
		namespace_queue_schedule(ns, 250000);

	return 0;
}

static void
namespace_queue_replay(int fd, short event, void *data)
{
	struct namespace	*ns = data;
	struct request		*req;

	if (ns->data_db == NULL || ns->indx_db == NULL) {
		log_debug("%s: database is being reopened", ns->suffix);
		return;		/* Database is being reopened. */
	}

	if ((req = TAILQ_FIRST(&ns->request_queue)) == NULL)
		return;
	TAILQ_REMOVE(&ns->request_queue, req, next);

	log_debug("replaying queued request");
	req->replayed = 1;
	request_dispatch(req);
	ns->queued_requests--;

	if (!evtimer_pending(&ns->ev_queue, NULL))
		namespace_queue_schedule(ns, 0);
}

void
namespace_queue_schedule(struct namespace *ns, unsigned int usec)
{
	struct timeval	 tv;

	tv.tv_sec = 0;
	tv.tv_usec = usec;
	evtimer_add(&ns->ev_queue, &tv);
}

/* Cancel all queued requests from the given connection. Drops matching
 * requests from all namespaces without sending a response.
 */
void
namespace_cancel_conn(struct conn *conn)
{
	struct namespace	*ns;
	struct request		*req, *next;

	TAILQ_FOREACH(ns, &conf->namespaces, next) {
		for (req = TAILQ_FIRST(&ns->request_queue); req != NULL;
		    req = next) {
			next = TAILQ_NEXT(req, next);

			if (req->conn == conn) {
				TAILQ_REMOVE(&ns->request_queue, req, next);
				request_free(req);
			}
		}
	}
}

int
namespace_conn_queue_count(struct conn *conn)
{
	struct namespace	*ns;
	struct request		*req;
	int			 count = 0;

	TAILQ_FOREACH(ns, &conf->namespaces, next) {
		TAILQ_FOREACH(req, &ns->request_queue, next) {
			if (req->conn == conn)
				count++;
		}
	}

	return count;
}
