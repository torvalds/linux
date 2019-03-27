/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1995
 *	Bill Paul <wpaul@ctr.columbia.edu>.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Bill Paul.
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY Bill Paul AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL Bill Paul OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <db.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <paths.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <rpcsvc/yp.h>
#include "yp_extern.h"

int ypdb_debug = 0;
enum ypstat yp_errno = YP_TRUE;

#define PERM_SECURE (S_IRUSR|S_IWUSR)
HASHINFO openinfo = {
	4096,		/* bsize */
	32,		/* ffactor */
	256,		/* nelem */
	2048 * 512, 	/* cachesize */
	NULL,		/* hash */
	0,		/* lorder */
};

#ifdef DB_CACHE
#include <sys/queue.h>

#ifndef MAXDBS
#define MAXDBS 20
#endif

static int numdbs = 0;

struct dbent {
	DB *dbp;
	char *name;
	char *key;
	int size;
	int flags;
};

static TAILQ_HEAD(circlehead, circleq_entry) qhead;

struct circleq_entry {
	struct dbent *dbptr;
	TAILQ_ENTRY(circleq_entry) links;
};

/*
 * Initialize the circular queue.
 */
void
yp_init_dbs(void)
{
	TAILQ_INIT(&qhead);
	return;
}

/*
 * Dynamically allocate an entry for the circular queue.
 * Return a NULL pointer on failure.
 */
static struct circleq_entry *
yp_malloc_qent(void)
{
	register struct circleq_entry *q;

	q = malloc(sizeof(struct circleq_entry));
	if (q == NULL) {
		yp_error("failed to malloc() circleq entry");
		return(NULL);
	}
	bzero((char *)q, sizeof(struct circleq_entry));
	q->dbptr = malloc(sizeof(struct dbent));
	if (q->dbptr == NULL) {
		yp_error("failed to malloc() circleq entry");
		free(q);
		return(NULL);
	}
	bzero((char *)q->dbptr, sizeof(struct dbent));

	return(q);
}

/*
 * Free a previously allocated circular queue
 * entry.
 */
static void
yp_free_qent(struct circleq_entry *q)
{
	/*
	 * First, close the database. In theory, this is also
	 * supposed to free the resources allocated by the DB
	 * package, including the memory pointed to by q->dbptr->key.
	 * This means we don't have to free q->dbptr->key here.
	 */
	if (q->dbptr->dbp) {
		(void)(q->dbptr->dbp->close)(q->dbptr->dbp);
		q->dbptr->dbp = NULL;
	}
	/*
	 * Then free the database name, which was strdup()'ed.
	 */
	free(q->dbptr->name);

	/*
	 * Free the rest of the dbent struct.
	 */
	free(q->dbptr);
	q->dbptr = NULL;

	/*
	 * Free the circleq struct.
	 */
	free(q);
	q = NULL;

	return;
}

/*
 * Zorch a single entry in the dbent queue and release
 * all its resources. (This always removes the last entry
 * in the queue.)
 */
static void
yp_flush(void)
{
	register struct circleq_entry *qptr;

	qptr = TAILQ_LAST(&qhead, circlehead);
	TAILQ_REMOVE(&qhead, qptr, links);
	yp_free_qent(qptr);
	numdbs--;

	return;
}

/*
 * Close all databases, erase all database names and empty the queue.
 */
void
yp_flush_all(void)
{
	register struct circleq_entry *qptr;

	while (!TAILQ_EMPTY(&qhead)) {
		qptr = TAILQ_FIRST(&qhead); /* save this */
		TAILQ_REMOVE(&qhead, qptr, links);
		yp_free_qent(qptr);
	}
	numdbs = 0;

	return;
}

static char *inter_string = "YP_INTERDOMAIN";
static char *secure_string = "YP_SECURE";
static int inter_sz = sizeof("YP_INTERDOMAIN") - 1;
static int secure_sz = sizeof("YP_SECURE") - 1;

static int
yp_setflags(DB *dbp)
{
	DBT key = { NULL, 0 }, data = { NULL, 0 };
	int flags = 0;

	key.data = inter_string;
	key.size = inter_sz;

	if (!(dbp->get)(dbp, &key, &data, 0))
		flags |= YP_INTERDOMAIN;

	key.data = secure_string;
	key.size = secure_sz;

	if (!(dbp->get)(dbp, &key, &data, 0))
		flags |= YP_SECURE;

	return(flags);
}

int
yp_testflag(char *map, char *domain, int flag)
{
	char buf[MAXPATHLEN + 2];
	register struct circleq_entry *qptr;

	if (map == NULL || domain == NULL)
		return(0);

	strcpy(buf, domain);
	strcat(buf, "/");
	strcat(buf, map);

	TAILQ_FOREACH(qptr, &qhead, links) {
		if (!strcmp(qptr->dbptr->name, buf)) {
			if (qptr->dbptr->flags & flag)
				return(1);
			else
				return(0);
		}
	}

	if (yp_open_db_cache(domain, map, NULL, 0) == NULL)
		return(0);

	if (TAILQ_FIRST(&qhead)->dbptr->flags & flag)
		return(1);

	return(0);
}

/*
 * Add a DB handle and database name to the cache. We only maintain
 * fixed number of entries in the cache, so if we're asked to store
 * a new entry when all our slots are already filled, we have to kick
 * out the entry in the last slot to make room.
 */
static int
yp_cache_db(DB *dbp, char *name, int size)
{
	register struct circleq_entry *qptr;

	if (numdbs == MAXDBS) {
		if (ypdb_debug)
			yp_error("queue overflow -- releasing last slot");
		yp_flush();
	}

	/*
	 * Allocate a new queue entry.
	 */

	if ((qptr = yp_malloc_qent()) == NULL) {
		yp_error("failed to allocate a new cache entry");
		return(1);
	}

	qptr->dbptr->dbp = dbp;
	qptr->dbptr->name = strdup(name);
	qptr->dbptr->size = size;
	qptr->dbptr->key = NULL;

	qptr->dbptr->flags = yp_setflags(dbp);

	TAILQ_INSERT_HEAD(&qhead, qptr, links);
	numdbs++;

	return(0);
}

/*
 * Search the list for a database matching 'name.' If we find it,
 * move it to the head of the list and return its DB handle. If
 * not, just fail: yp_open_db_cache() will subsequently try to open
 * the database itself and call yp_cache_db() to add it to the
 * list.
 *
 * The search works like this:
 *
 * - The caller specifies the name of a database to locate. We try to
 *   find an entry in our queue with a matching name.
 *
 * - If the caller doesn't specify a key or size, we assume that the
 *   first entry that we encounter with a matching name is returned.
 *   This will result in matches regardless of the key/size values
 *   stored in the queue entry.
 *
 * - If the caller also specifies a key and length, we check to see
 *   if the key and length saved in the queue entry also matches.
 *   This lets us return a DB handle that's already positioned at the
 *   correct location within a database.
 *
 * - Once we have a match, it gets migrated to the top of the queue
 *   so that it will be easier to find if another request for
 *   the same database comes in later.
 */
static DB *
yp_find_db(const char *name, const char *key, int size)
{
	register struct circleq_entry *qptr;

	TAILQ_FOREACH(qptr, &qhead, links) {
		if (!strcmp(qptr->dbptr->name, name)) {
			if (size) {
				if (size != qptr->dbptr->size ||
				   strncmp(qptr->dbptr->key, key, size))
					continue;
			} else {
				if (qptr->dbptr->size)
					continue;
			}
			if (qptr != TAILQ_FIRST(&qhead)) {
				TAILQ_REMOVE(&qhead, qptr, links);
				TAILQ_INSERT_HEAD(&qhead, qptr, links);
			}
			return(qptr->dbptr->dbp);
		}
	}

	return(NULL);
}

/*
 * Open a DB database and cache the handle for later use. We first
 * check the cache to see if the required database is already open.
 * If so, we fetch the handle from the cache. If not, we try to open
 * the database and save the handle in the cache for later use.
 */
DB *
yp_open_db_cache(const char *domain, const char *map, const char *key,
    const int size)
{
	DB *dbp = NULL;
	char buf[MAXPATHLEN + 2];
/*
	snprintf(buf, sizeof(buf), "%s/%s", domain, map);
*/
	yp_errno = YP_TRUE;

	strcpy(buf, domain);
	strcat(buf, "/");
	strcat(buf, map);

	if ((dbp = yp_find_db(buf, key, size)) != NULL) {
		return(dbp);
	} else {
		if ((dbp = yp_open_db(domain, map)) != NULL) {
			if (yp_cache_db(dbp, buf, size)) {
				(void)(dbp->close)(dbp);
				yp_errno = YP_YPERR;
				return(NULL);
			}
		}
	}

	return (dbp);
}
#endif

/*
 * Open a DB database.
 */
DB *
yp_open_db(const char *domain, const char *map)
{
	DB *dbp = NULL;
	char buf[MAXPATHLEN + 2];

	yp_errno = YP_TRUE;

	if (map[0] == '.' || strchr(map, '/')) {
		yp_errno = YP_BADARGS;
		return (NULL);
	}

#ifdef DB_CACHE
	if (yp_validdomain(domain)) {
		yp_errno = YP_NODOM;
		return(NULL);
	}
#endif
	snprintf(buf, sizeof(buf), "%s/%s/%s", yp_dir, domain, map);

#ifdef DB_CACHE
again:
#endif
	dbp = dbopen(buf, O_RDONLY, PERM_SECURE, DB_HASH, NULL);

	if (dbp == NULL) {
		switch (errno) {
#ifdef DB_CACHE
		case ENFILE:
			/*
			 * We ran out of file descriptors. Nuke an
			 * open one and try again.
			 */
			yp_error("ran out of file descriptors");
			yp_flush();
			goto again;
			break;
#endif
		case ENOENT:
			yp_errno = YP_NOMAP;
			break;
		case EFTYPE:
			yp_errno = YP_BADDB;
			break;
		default:
			yp_errno = YP_YPERR;
			break;
		}
	}

	return (dbp);
}

/*
 * Database access routines.
 *
 * - yp_get_record(): retrieve an arbitrary key/data pair given one key
 *                 to match against.
 *
 * - yp_first_record(): retrieve first key/data base in a database.
 *
 * - yp_next_record(): retrieve key/data pair that sequentially follows
 *                   the supplied key value in the database.
 */

#ifdef DB_CACHE
int
yp_get_record(DB *dbp, const DBT *key, DBT *data, int allow)
#else
int
yp_get_record(const char *domain, const char *map,
    const DBT *key, DBT *data, int allow)
#endif
{
#ifndef DB_CACHE
	DB *dbp;
#endif
	int rval = 0;
#ifndef DB_CACHE
	static unsigned char buf[YPMAXRECORD];
#endif

	if (ypdb_debug)
		yp_error("looking up key [%.*s]",
		    (int)key->size, (char *)key->data);

	/*
	 * Avoid passing back magic "YP_*" entries unless
	 * the caller specifically requested them by setting
	 * the 'allow' flag.
	 */
	if (!allow && !strncmp(key->data, "YP_", 3))
		return(YP_NOKEY);

#ifndef DB_CACHE
	if ((dbp = yp_open_db(domain, map)) == NULL) {
		return(yp_errno);
	}
#endif

	if ((rval = (dbp->get)(dbp, key, data, 0)) != 0) {
#ifdef DB_CACHE
		TAILQ_FIRST(&qhead)->dbptr->size = 0;
#else
		(void)(dbp->close)(dbp);
#endif
		if (rval == 1)
			return(YP_NOKEY);
		else
			return(YP_BADDB);
	}

	if (ypdb_debug)
		yp_error("result of lookup: key: [%.*s] data: [%.*s]",
		    (int)key->size, (char *)key->data,
		    (int)data->size, (char *)data->data);

#ifdef DB_CACHE
	if (TAILQ_FIRST(&qhead)->dbptr->size) {
		TAILQ_FIRST(&qhead)->dbptr->key = "";
		TAILQ_FIRST(&qhead)->dbptr->size = 0;
	}
#else
	bcopy(data->data, &buf, data->size);
	data->data = &buf;
	(void)(dbp->close)(dbp);
#endif

	return(YP_TRUE);
}

int
yp_first_record(const DB *dbp, DBT *key, DBT *data, int allow)
{
	int rval;
#ifndef DB_CACHE
	static unsigned char buf[YPMAXRECORD];
#endif

	if (ypdb_debug)
		yp_error("retrieving first key in map");

	if ((rval = (dbp->seq)(dbp,key,data,R_FIRST)) != 0) {
#ifdef DB_CACHE
		TAILQ_FIRST(&qhead)->dbptr->size = 0;
#endif
		if (rval == 1)
			return(YP_NOKEY);
		else
			return(YP_BADDB);
	}

	/* Avoid passing back magic "YP_*" records. */
	while (!strncmp(key->data, "YP_", 3) && !allow) {
		if ((rval = (dbp->seq)(dbp,key,data,R_NEXT)) != 0) {
#ifdef DB_CACHE
			TAILQ_FIRST(&qhead)->dbptr->size = 0;
#endif
			if (rval == 1)
				return(YP_NOKEY);
			else
				return(YP_BADDB);
		}
	}

	if (ypdb_debug)
		yp_error("result of lookup: key: [%.*s] data: [%.*s]",
		    (int)key->size, (char *)key->data,
		    (int)data->size, (char *)data->data);

#ifdef DB_CACHE
	if (TAILQ_FIRST(&qhead)->dbptr->size) {
		TAILQ_FIRST(&qhead)->dbptr->key = key->data;
		TAILQ_FIRST(&qhead)->dbptr->size = key->size;
	}
#else
	bcopy(data->data, &buf, data->size);
	data->data = &buf;
#endif

	return(YP_TRUE);
}

int
yp_next_record(const DB *dbp, DBT *key, DBT *data, int all, int allow)
{
	static DBT lkey = { NULL, 0 };
	static DBT ldata = { NULL, 0 };
	int rval;
#ifndef DB_CACHE
	static unsigned char keybuf[YPMAXRECORD];
	static unsigned char datbuf[YPMAXRECORD];
#endif

	if (key == NULL || !key->size || key->data == NULL) {
		rval = yp_first_record(dbp,key,data,allow);
		if (rval == YP_NOKEY)
			return(YP_NOMORE);
		else {
#ifdef DB_CACHE
			TAILQ_FIRST(&qhead)->dbptr->key = key->data;
			TAILQ_FIRST(&qhead)->dbptr->size = key->size;
#endif
			return(rval);
		}
	}

	if (ypdb_debug)
		yp_error("retrieving next key, previous was: [%.*s]",
		    (int)key->size, (char *)key->data);

	if (!all) {
#ifdef DB_CACHE
		if (TAILQ_FIRST(&qhead)->dbptr->key == NULL) {
#endif
			(dbp->seq)(dbp,&lkey,&ldata,R_FIRST);
			while (key->size != lkey.size ||
			    strncmp(key->data, lkey.data,
			    (int)key->size))
				if ((dbp->seq)(dbp,&lkey,&ldata,R_NEXT)) {
#ifdef DB_CACHE
					TAILQ_FIRST(&qhead)->dbptr->size = 0;
#endif
					return(YP_NOKEY);
				}

#ifdef DB_CACHE
		}
#endif
	}

	if ((dbp->seq)(dbp,key,data,R_NEXT)) {
#ifdef DB_CACHE
		TAILQ_FIRST(&qhead)->dbptr->size = 0;
#endif
		return(YP_NOMORE);
	}

	/* Avoid passing back magic "YP_*" records. */
	while (!strncmp(key->data, "YP_", 3) && !allow)
		if ((dbp->seq)(dbp,key,data,R_NEXT)) {
#ifdef DB_CACHE
		TAILQ_FIRST(&qhead)->dbptr->size = 0;
#endif
			return(YP_NOMORE);
		}

	if (ypdb_debug)
		yp_error("result of lookup: key: [%.*s] data: [%.*s]",
		    (int)key->size, (char *)key->data,
		    (int)data->size, (char *)data->data);

#ifdef DB_CACHE
	if (TAILQ_FIRST(&qhead)->dbptr->size) {
		TAILQ_FIRST(&qhead)->dbptr->key = key->data;
		TAILQ_FIRST(&qhead)->dbptr->size = key->size;
	}
#else
	bcopy(key->data, &keybuf, key->size);
	lkey.data = &keybuf;
	lkey.size = key->size;
	bcopy(data->data, &datbuf, data->size);
	data->data = &datbuf;
#endif

	return(YP_TRUE);
}

#ifdef DB_CACHE
/*
 * Database glue functions.
 */

static DB *yp_currmap_db = NULL;
static int yp_allow_db = 0;

ypstat
yp_select_map(char *map, char *domain, keydat *key, int allow)
{
	if (key == NULL)
		yp_currmap_db = yp_open_db_cache(domain, map, NULL, 0);
	else
		yp_currmap_db = yp_open_db_cache(domain, map,
						 key->keydat_val,
						 key->keydat_len);

	yp_allow_db = allow;
	return(yp_errno);
}

ypstat
yp_getbykey(keydat *key, valdat *val)
{
	DBT db_key = { NULL, 0 }, db_val = { NULL, 0 };
	ypstat rval;

	db_key.data = key->keydat_val;
	db_key.size = key->keydat_len;

	rval = yp_get_record(yp_currmap_db,
				&db_key, &db_val, yp_allow_db);

	if (rval == YP_TRUE) {
		val->valdat_val = db_val.data;
		val->valdat_len = db_val.size;
	}

	return(rval);
}

ypstat
yp_firstbykey(keydat *key, valdat *val)
{
	DBT db_key = { NULL, 0 }, db_val = { NULL, 0 };
	ypstat rval;

	rval = yp_first_record(yp_currmap_db, &db_key, &db_val, yp_allow_db);

	if (rval == YP_TRUE) {
		key->keydat_val = db_key.data;
		key->keydat_len = db_key.size;
		val->valdat_val = db_val.data;
		val->valdat_len = db_val.size;
	}

	return(rval);
}

ypstat
yp_nextbykey(keydat *key, valdat *val)
{
	DBT db_key = { NULL, 0 }, db_val = { NULL, 0 };
	ypstat rval;

	db_key.data = key->keydat_val;
	db_key.size = key->keydat_len;

	rval = yp_next_record(yp_currmap_db, &db_key, &db_val, 0, yp_allow_db);

	if (rval == YP_TRUE) {
		key->keydat_val = db_key.data;
		key->keydat_len = db_key.size;
		val->valdat_val = db_val.data;
		val->valdat_len = db_val.size;
	}

	return(rval);
}
#endif
