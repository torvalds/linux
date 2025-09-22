/*	$OpenBSD: queue_backend.c,v 1.69 2023/05/31 16:51:46 op Exp $	*/

/*
 * Copyright (c) 2011 Gilles Chehade <gilles@poolp.org>
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

#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <inttypes.h>
#include <pwd.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "smtpd.h"
#include "log.h"

static const char* envelope_validate(struct envelope *);

extern struct queue_backend	queue_backend_fs;
extern struct queue_backend	queue_backend_null;
extern struct queue_backend	queue_backend_proc;
extern struct queue_backend	queue_backend_ram;

static void queue_envelope_cache_add(struct envelope *);
static void queue_envelope_cache_update(struct envelope *);
static void queue_envelope_cache_del(uint64_t evpid);

TAILQ_HEAD(evplst, envelope);

static struct tree		evpcache_tree;
static struct evplst		evpcache_list;
static struct queue_backend	*backend;

static int (*handler_close)(void);
static int (*handler_message_create)(uint32_t *);
static int (*handler_message_commit)(uint32_t, const char*);
static int (*handler_message_delete)(uint32_t);
static int (*handler_message_fd_r)(uint32_t);
static int (*handler_envelope_create)(uint32_t, const char *, size_t, uint64_t *);
static int (*handler_envelope_delete)(uint64_t);
static int (*handler_envelope_update)(uint64_t, const char *, size_t);
static int (*handler_envelope_load)(uint64_t, char *, size_t);
static int (*handler_envelope_walk)(uint64_t *, char *, size_t);
static int (*handler_message_walk)(uint64_t *, char *, size_t,
    uint32_t, int *, void **);

#ifdef QUEUE_PROFILING

static struct {
	struct timespec	 t0;
	const char	*name;
} profile;

static inline void profile_enter(const char *name)
{
	if ((profiling & PROFILE_QUEUE) == 0)
		return;

	profile.name = name;
	clock_gettime(CLOCK_MONOTONIC, &profile.t0);
}

static inline void profile_leave(void)
{
	struct timespec	 t1, dt;

	if ((profiling & PROFILE_QUEUE) == 0)
		return;

	clock_gettime(CLOCK_MONOTONIC, &t1);
	timespecsub(&t1, &profile.t0, &dt);
	log_debug("profile-queue: %s %lld.%09ld", profile.name,
	    (long long)dt.tv_sec, dt.tv_nsec);
}
#else
#define profile_enter(x)	do {} while (0)
#define profile_leave()		do {} while (0)
#endif

static int
queue_message_path(uint32_t msgid, char *buf, size_t len)
{
	return bsnprintf(buf, len, "%s/%08"PRIx32, PATH_TEMPORARY, msgid);
}

int
queue_init(const char *name, int server)
{
	struct passwd	*pwq;
	struct group	*gr;
	int		 r;

	pwq = getpwnam(SMTPD_QUEUE_USER);
	if (pwq == NULL)
		fatalx("unknown user %s", SMTPD_QUEUE_USER);

	gr = getgrnam(SMTPD_QUEUE_GROUP);
	if (gr == NULL)
		fatalx("unknown group %s", SMTPD_QUEUE_GROUP);

	tree_init(&evpcache_tree);
	TAILQ_INIT(&evpcache_list);

	if (!strcmp(name, "fs"))
		backend = &queue_backend_fs;
	else if (!strcmp(name, "null"))
		backend = &queue_backend_null;
	else if (!strcmp(name, "ram"))
		backend = &queue_backend_ram;
	else
		backend = &queue_backend_proc;

	if (server) {
		if (ckdir(PATH_SPOOL, 0711, 0, 0, 1) == 0)
			fatalx("error in spool directory setup");
		if (ckdir(PATH_SPOOL PATH_OFFLINE, 0770, 0, gr->gr_gid, 1) == 0)
			fatalx("error in offline directory setup");
		if (ckdir(PATH_SPOOL PATH_PURGE, 0700, pwq->pw_uid, 0, 1) == 0)
			fatalx("error in purge directory setup");

		mvpurge(PATH_SPOOL PATH_TEMPORARY, PATH_SPOOL PATH_PURGE);

		if (ckdir(PATH_SPOOL PATH_TEMPORARY, 0700, pwq->pw_uid, 0, 1) == 0)
			fatalx("error in purge directory setup");
	}

	r = backend->init(pwq, server, name);

	log_trace(TRACE_QUEUE, "queue-backend: queue_init(%d) -> %d", server, r);

	return (r);
}

int
queue_close(void)
{
	if (handler_close)
		return (handler_close());

	return (1);
}

int
queue_message_create(uint32_t *msgid)
{
	int	r;

	profile_enter("queue_message_create");
	r = handler_message_create(msgid);
	profile_leave();

	log_trace(TRACE_QUEUE,
	    "queue-backend: queue_message_create() -> %d (%08"PRIx32")",
	    r, *msgid);

	return (r);
}

int
queue_message_delete(uint32_t msgid)
{
	char	msgpath[PATH_MAX];
	uint64_t evpid;
	void   *iter;
	int	r;

	profile_enter("queue_message_delete");
	r = handler_message_delete(msgid);
	profile_leave();

	/* in case the message is incoming */
	queue_message_path(msgid, msgpath, sizeof(msgpath));
	unlink(msgpath);

	/* remove remaining envelopes from the cache if any (on rollback) */
	evpid = msgid_to_evpid(msgid);
	for (;;) {
		iter = NULL;
		if (!tree_iterfrom(&evpcache_tree, &iter, evpid, &evpid, NULL))
			break;
		if (evpid_to_msgid(evpid) != msgid)
			break;
		queue_envelope_cache_del(evpid);
	}

	log_trace(TRACE_QUEUE,
	    "queue-backend: queue_message_delete(%08"PRIx32") -> %d", msgid, r);

	return (r);
}

int
queue_message_commit(uint32_t msgid)
{
	int	r;
	char	msgpath[PATH_MAX];
	char	tmppath[PATH_MAX];
	FILE	*ifp = NULL;
	FILE	*ofp = NULL;

	profile_enter("queue_message_commit");

	queue_message_path(msgid, msgpath, sizeof(msgpath));

	if (env->sc_queue_flags & QUEUE_COMPRESSION) {
		bsnprintf(tmppath, sizeof tmppath, "%s.comp", msgpath);
		ifp = fopen(msgpath, "r");
		ofp = fopen(tmppath, "w+");
		if (ifp == NULL || ofp == NULL)
			goto err;
		if (!compress_file(ifp, ofp))
			goto err;
		fclose(ifp);
		fclose(ofp);
		ifp = NULL;
		ofp = NULL;

		if (rename(tmppath, msgpath) == -1) {
			if (errno == ENOSPC)
				return (0);
			unlink(tmppath);
			log_warn("rename");
			return (0);
		}
	}

	if (env->sc_queue_flags & QUEUE_ENCRYPTION) {
		bsnprintf(tmppath, sizeof tmppath, "%s.enc", msgpath);
		ifp = fopen(msgpath, "r");
		ofp = fopen(tmppath, "w+");
		if (ifp == NULL || ofp == NULL)
			goto err;
		if (!crypto_encrypt_file(ifp, ofp))
			goto err;
		fclose(ifp);
		fclose(ofp);
		ifp = NULL;
		ofp = NULL;

		if (rename(tmppath, msgpath) == -1) {
			if (errno == ENOSPC)
				return (0);
			unlink(tmppath);
			log_warn("rename");
			return (0);
		}
	}

	r = handler_message_commit(msgid, msgpath);
	profile_leave();

	/* in case it's not done by the backend */
	unlink(msgpath);

	log_trace(TRACE_QUEUE,
	    "queue-backend: queue_message_commit(%08"PRIx32") -> %d",
	    msgid, r);

	return (r);

err:
	if (ifp)
		fclose(ifp);
	if (ofp)
		fclose(ofp);
	return 0;
}

int
queue_message_fd_r(uint32_t msgid)
{
	int	fdin = -1, fdout = -1, fd = -1;
	FILE	*ifp = NULL;
	FILE	*ofp = NULL;

	profile_enter("queue_message_fd_r");
	fdin = handler_message_fd_r(msgid);
	profile_leave();

	log_trace(TRACE_QUEUE,
	    "queue-backend: queue_message_fd_r(%08"PRIx32") -> %d", msgid, fdin);

	if (fdin == -1)
		return (-1);

	if (env->sc_queue_flags & QUEUE_ENCRYPTION) {
		if ((fdout = mktmpfile()) == -1)
			goto err;
		if ((fd = dup(fdout)) == -1)
			goto err;
		if ((ifp = fdopen(fdin, "r")) == NULL)
			goto err;
		fdin = fd;
		fd = -1;
		if ((ofp = fdopen(fdout, "w+")) == NULL)
			goto err;

		if (!crypto_decrypt_file(ifp, ofp))
			goto err;

		fclose(ifp);
		ifp = NULL;
		fclose(ofp);
		ofp = NULL;
		lseek(fdin, SEEK_SET, 0);
	}

	if (env->sc_queue_flags & QUEUE_COMPRESSION) {
		if ((fdout = mktmpfile()) == -1)
			goto err;
		if ((fd = dup(fdout)) == -1)
			goto err;
		if ((ifp = fdopen(fdin, "r")) == NULL)
			goto err;
		fdin = fd;
		fd = -1;
		if ((ofp = fdopen(fdout, "w+")) == NULL)
			goto err;

		if (!uncompress_file(ifp, ofp))
			goto err;

		fclose(ifp);
		ifp = NULL;
		fclose(ofp);
		ofp = NULL;
		lseek(fdin, SEEK_SET, 0);
	}

	return (fdin);

err:
	if (fd != -1)
		close(fd);
	if (fdin != -1)
		close(fdin);
	if (fdout != -1)
		close(fdout);
	if (ifp)
		fclose(ifp);
	if (ofp)
		fclose(ofp);
	return -1;
}

int
queue_message_fd_rw(uint32_t msgid)
{
	char buf[PATH_MAX];

	queue_message_path(msgid, buf, sizeof(buf));

	return open(buf, O_RDWR | O_CREAT | O_EXCL, 0600);
}

static int
queue_envelope_dump_buffer(struct envelope *ep, char *evpbuf, size_t evpbufsize)
{
	char   *evp;
	size_t	evplen;
	size_t	complen;
	char	compbuf[sizeof(struct envelope)];
	size_t	enclen;
	char	encbuf[sizeof(struct envelope)];

	evp = evpbuf;
	evplen = envelope_dump_buffer(ep, evpbuf, evpbufsize);
	if (evplen == 0)
		return (0);

	if (env->sc_queue_flags & QUEUE_COMPRESSION) {
		complen = compress_chunk(evp, evplen, compbuf, sizeof compbuf);
		if (complen == 0)
			return (0);
		evp = compbuf;
		evplen = complen;
	}

	if (env->sc_queue_flags & QUEUE_ENCRYPTION) {
		enclen = crypto_encrypt_buffer(evp, evplen, encbuf, sizeof encbuf);
		if (enclen == 0)
			return (0);
		evp = encbuf;
		evplen = enclen;
	}

	memmove(evpbuf, evp, evplen);

	return (evplen);
}

static int
queue_envelope_load_buffer(struct envelope *ep, char *evpbuf, size_t evpbufsize)
{
	char		*evp;
	size_t		 evplen;
	char		 compbuf[sizeof(struct envelope)];
	size_t		 complen;
	char		 encbuf[sizeof(struct envelope)];
	size_t		 enclen;

	evp = evpbuf;
	evplen = evpbufsize;

	if (env->sc_queue_flags & QUEUE_ENCRYPTION) {
		enclen = crypto_decrypt_buffer(evp, evplen, encbuf, sizeof encbuf);
		if (enclen == 0)
			return (0);
		evp = encbuf;
		evplen = enclen;
	}

	if (env->sc_queue_flags & QUEUE_COMPRESSION) {
		complen = uncompress_chunk(evp, evplen, compbuf, sizeof compbuf);
		if (complen == 0)
			return (0);
		evp = compbuf;
		evplen = complen;
	}

	return (envelope_load_buffer(ep, evp, evplen));
}

static void
queue_envelope_cache_add(struct envelope *e)
{
	struct envelope *cached;

	while (tree_count(&evpcache_tree) >= env->sc_queue_evpcache_size)
		queue_envelope_cache_del(TAILQ_LAST(&evpcache_list, evplst)->id);

	cached = xcalloc(1, sizeof *cached);
	*cached = *e;
	TAILQ_INSERT_HEAD(&evpcache_list, cached, entry);
	tree_xset(&evpcache_tree, e->id, cached);
	stat_increment("queue.evpcache.size", 1);
}

static void
queue_envelope_cache_update(struct envelope *e)
{
	struct envelope *cached;

	if ((cached = tree_get(&evpcache_tree, e->id)) == NULL) {
		queue_envelope_cache_add(e);
		stat_increment("queue.evpcache.update.missed", 1);
	} else {
		TAILQ_REMOVE(&evpcache_list, cached, entry);
		*cached = *e;
		TAILQ_INSERT_HEAD(&evpcache_list, cached, entry);
		stat_increment("queue.evpcache.update.hit", 1);
	}
}

static void
queue_envelope_cache_del(uint64_t evpid)
{
	struct envelope *cached;

	if ((cached = tree_pop(&evpcache_tree, evpid)) == NULL)
		return;

	TAILQ_REMOVE(&evpcache_list, cached, entry);
	free(cached);
	stat_decrement("queue.evpcache.size", 1);
}

int
queue_envelope_create(struct envelope *ep)
{
	int		 r;
	char		 evpbuf[sizeof(struct envelope)];
	size_t		 evplen;
	uint64_t	 evpid;
	uint32_t	 msgid;

	ep->creation = time(NULL);
	evplen = queue_envelope_dump_buffer(ep, evpbuf, sizeof evpbuf);
	if (evplen == 0)
		return (0);

	evpid = ep->id;
	msgid = evpid_to_msgid(evpid);

	profile_enter("queue_envelope_create");
	r = handler_envelope_create(msgid, evpbuf, evplen, &ep->id);
	profile_leave();

	log_trace(TRACE_QUEUE,
	    "queue-backend: queue_envelope_create(%016"PRIx64", %zu) -> %d (%016"PRIx64")",
	    evpid, evplen, r, ep->id);

	if (!r) {
		ep->creation = 0;
		ep->id = 0;
	}

	if (r && env->sc_queue_flags & QUEUE_EVPCACHE)
		queue_envelope_cache_add(ep);

	return (r);
}

int
queue_envelope_delete(uint64_t evpid)
{
	int	r;

	if (env->sc_queue_flags & QUEUE_EVPCACHE)
		queue_envelope_cache_del(evpid);

	profile_enter("queue_envelope_delete");
	r = handler_envelope_delete(evpid);
	profile_leave();

	log_trace(TRACE_QUEUE,
	    "queue-backend: queue_envelope_delete(%016"PRIx64") -> %d",
	    evpid, r);

	return (r);
}

int
queue_envelope_load(uint64_t evpid, struct envelope *ep)
{
	const char	*e;
	char		 evpbuf[sizeof(struct envelope)];
	size_t		 evplen;
	struct envelope	*cached;

	if ((env->sc_queue_flags & QUEUE_EVPCACHE) &&
	    (cached = tree_get(&evpcache_tree, evpid))) {
		*ep = *cached;
		stat_increment("queue.evpcache.load.hit", 1);
		return (1);
	}

	ep->id = evpid;
	profile_enter("queue_envelope_load");
	evplen = handler_envelope_load(ep->id, evpbuf, sizeof evpbuf);
	profile_leave();

	log_trace(TRACE_QUEUE,
	    "queue-backend: queue_envelope_load(%016"PRIx64") -> %zu",
	    evpid, evplen);

	if (evplen == 0)
		return (0);

	if (queue_envelope_load_buffer(ep, evpbuf, evplen)) {
		if ((e = envelope_validate(ep)) == NULL) {
			ep->id = evpid;
			if (env->sc_queue_flags & QUEUE_EVPCACHE) {
				queue_envelope_cache_add(ep);
				stat_increment("queue.evpcache.load.missed", 1);
			}
			return (1);
		}
		log_warnx("warn: invalid envelope %016" PRIx64 ": %s",
		    evpid, e);
	}
	return (0);
}

int
queue_envelope_update(struct envelope *ep)
{
	char	evpbuf[sizeof(struct envelope)];
	size_t	evplen;
	int	r;

	evplen = queue_envelope_dump_buffer(ep, evpbuf, sizeof evpbuf);
	if (evplen == 0)
		return (0);

	profile_enter("queue_envelope_update");
	r = handler_envelope_update(ep->id, evpbuf, evplen);
	profile_leave();

	if (r && env->sc_queue_flags & QUEUE_EVPCACHE)
		queue_envelope_cache_update(ep);

	log_trace(TRACE_QUEUE,
	    "queue-backend: queue_envelope_update(%016"PRIx64") -> %d",
	    ep->id, r);

	return (r);
}

int
queue_message_walk(struct envelope *ep, uint32_t msgid, int *done, void **data)
{
	char		 evpbuf[sizeof(struct envelope)];
	uint64_t	 evpid;
	int		 r;
	const char	*e;

	profile_enter("queue_message_walk");
	r = handler_message_walk(&evpid, evpbuf, sizeof evpbuf,
	    msgid, done, data);
	profile_leave();

	log_trace(TRACE_QUEUE,
	    "queue-backend: queue_message_walk() -> %d (%016"PRIx64")",
	    r, evpid);

	if (r == -1)
		return (r);

	if (r && queue_envelope_load_buffer(ep, evpbuf, (size_t)r)) {
		if ((e = envelope_validate(ep)) == NULL) {
			ep->id = evpid;
			/*
			 * do not cache the envelope here, while discovering
			 * envelopes one could re-run discover on already
			 * scheduled envelopes which leads to triggering of
			 * strict checks in caching. Envelopes could anyway
			 * be loaded from backend if it isn't cached.
			 */
			return (1);
		}
		log_warnx("warn: invalid envelope %016" PRIx64 ": %s",
		    evpid, e);
	}
	return (0);
}

int
queue_envelope_walk(struct envelope *ep)
{
	const char	*e;
	uint64_t	 evpid;
	char		 evpbuf[sizeof(struct envelope)];
	int		 r;

	profile_enter("queue_envelope_walk");
	r = handler_envelope_walk(&evpid, evpbuf, sizeof evpbuf);
	profile_leave();

	log_trace(TRACE_QUEUE,
	    "queue-backend: queue_envelope_walk() -> %d (%016"PRIx64")",
	    r, evpid);

	if (r == -1)
		return (r);

	if (r && queue_envelope_load_buffer(ep, evpbuf, (size_t)r)) {
		if ((e = envelope_validate(ep)) == NULL) {
			ep->id = evpid;
			if (env->sc_queue_flags & QUEUE_EVPCACHE)
				queue_envelope_cache_add(ep);
			return (1);
		}
		log_warnx("warn: invalid envelope %016" PRIx64 ": %s",
		    evpid, e);
	}
	return (0);
}

uint32_t
queue_generate_msgid(void)
{
	uint32_t msgid;

	while ((msgid = arc4random()) == 0)
		;

	return msgid;
}

uint64_t
queue_generate_evpid(uint32_t msgid)
{
	uint32_t rnd;
	uint64_t evpid;

	while ((rnd = arc4random()) == 0)
		;

	evpid = msgid;
	evpid <<= 32;
	evpid |= rnd;

	return evpid;
}

static const char*
envelope_validate(struct envelope *ep)
{
	if (ep->version != SMTPD_ENVELOPE_VERSION)
		return "version mismatch";

	if (memchr(ep->helo, '\0', sizeof(ep->helo)) == NULL)
		return "invalid helo";
	if (ep->helo[0] == '\0')
		return "empty helo";

	if (memchr(ep->hostname, '\0', sizeof(ep->hostname)) == NULL)
		return "invalid hostname";
	if (ep->hostname[0] == '\0')
		return "empty hostname";

	if (memchr(ep->errorline, '\0', sizeof(ep->errorline)) == NULL)
		return "invalid error line";

	if (dict_get(env->sc_dispatchers, ep->dispatcher) == NULL)
		return "unknown dispatcher";

	return NULL;
}

void
queue_api_on_close(int(*cb)(void))
{
	handler_close = cb;
}

void
queue_api_on_message_create(int(*cb)(uint32_t *))
{
	handler_message_create = cb;
}

void
queue_api_on_message_commit(int(*cb)(uint32_t, const char *))
{
	handler_message_commit = cb;
}

void
queue_api_on_message_delete(int(*cb)(uint32_t))
{
	handler_message_delete = cb;
}

void
queue_api_on_message_fd_r(int(*cb)(uint32_t))
{
	handler_message_fd_r = cb;
}

void
queue_api_on_envelope_create(int(*cb)(uint32_t, const char *, size_t, uint64_t *))
{
	handler_envelope_create = cb;
}

void
queue_api_on_envelope_delete(int(*cb)(uint64_t))
{
	handler_envelope_delete = cb;
}

void
queue_api_on_envelope_update(int(*cb)(uint64_t, const char *, size_t))
{
	handler_envelope_update = cb;
}

void
queue_api_on_envelope_load(int(*cb)(uint64_t, char *, size_t))
{
	handler_envelope_load = cb;
}

void
queue_api_on_envelope_walk(int(*cb)(uint64_t *, char *, size_t))
{
	handler_envelope_walk = cb;
}

void
queue_api_on_message_walk(int(*cb)(uint64_t *, char *, size_t,
    uint32_t, int *, void **))
{
	handler_message_walk = cb;
}
