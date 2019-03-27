/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008 Isilon Inc http://www.isilon.com/
 * Authors: Doug Rabson <dfr@rabson.org>
 * Developed with Red Inc: Alfred Perlstein <alfred@freebsd.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
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

#include <sys/param.h>
#include <sys/hash.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/mutex.h>
#include <sys/queue.h>

#include <rpc/rpc.h>
#include <rpc/replay.h>

struct replay_cache_entry {
	int		rce_hash;
	struct rpc_msg	rce_msg;
	struct sockaddr_storage rce_addr;
	struct rpc_msg	rce_repmsg;
	struct mbuf	*rce_repbody;

	TAILQ_ENTRY(replay_cache_entry) rce_link;
	TAILQ_ENTRY(replay_cache_entry) rce_alllink;
};
TAILQ_HEAD(replay_cache_list, replay_cache_entry);

static struct replay_cache_entry *
		replay_alloc(struct replay_cache *rc, struct rpc_msg *msg,
		    struct sockaddr *addr, int h);
static void	replay_free(struct replay_cache *rc,
    struct replay_cache_entry *rce);
static void	replay_prune(struct replay_cache *rc);

#define REPLAY_HASH_SIZE	256
#define REPLAY_MAX		1024

struct replay_cache {
	struct replay_cache_list	rc_cache[REPLAY_HASH_SIZE];
	struct replay_cache_list	rc_all;
	struct mtx			rc_lock;
	int				rc_count;
	size_t				rc_size;
	size_t				rc_maxsize;
};

struct replay_cache *
replay_newcache(size_t maxsize)
{
	struct replay_cache *rc;
	int i;

	rc = malloc(sizeof(*rc), M_RPC, M_WAITOK|M_ZERO);
	for (i = 0; i < REPLAY_HASH_SIZE; i++)
		TAILQ_INIT(&rc->rc_cache[i]);
	TAILQ_INIT(&rc->rc_all);
	mtx_init(&rc->rc_lock, "rc_lock", NULL, MTX_DEF);
	rc->rc_maxsize = maxsize;

	return (rc);
}

void
replay_setsize(struct replay_cache *rc, size_t newmaxsize)
{

	mtx_lock(&rc->rc_lock);
	rc->rc_maxsize = newmaxsize;
	replay_prune(rc);
	mtx_unlock(&rc->rc_lock);
}

void
replay_freecache(struct replay_cache *rc)
{

	mtx_lock(&rc->rc_lock);
	while (TAILQ_FIRST(&rc->rc_all))
		replay_free(rc, TAILQ_FIRST(&rc->rc_all));
	mtx_destroy(&rc->rc_lock);
	free(rc, M_RPC);
}

static struct replay_cache_entry *
replay_alloc(struct replay_cache *rc,
    struct rpc_msg *msg, struct sockaddr *addr, int h)
{
	struct replay_cache_entry *rce;

	mtx_assert(&rc->rc_lock, MA_OWNED);

	rc->rc_count++;
	rce = malloc(sizeof(*rce), M_RPC, M_NOWAIT|M_ZERO);
	if (!rce)
		return (NULL);
	rce->rce_hash = h;
	rce->rce_msg = *msg;
	bcopy(addr, &rce->rce_addr, addr->sa_len);

	TAILQ_INSERT_HEAD(&rc->rc_cache[h], rce, rce_link);
	TAILQ_INSERT_HEAD(&rc->rc_all, rce, rce_alllink);

	return (rce);
}

static void
replay_free(struct replay_cache *rc, struct replay_cache_entry *rce)
{

	mtx_assert(&rc->rc_lock, MA_OWNED);

	rc->rc_count--;
	TAILQ_REMOVE(&rc->rc_cache[rce->rce_hash], rce, rce_link);
	TAILQ_REMOVE(&rc->rc_all, rce, rce_alllink);
	if (rce->rce_repbody) {
		rc->rc_size -= m_length(rce->rce_repbody, NULL);
		m_freem(rce->rce_repbody);
	}
	free(rce, M_RPC);
}

static void
replay_prune(struct replay_cache *rc)
{
	struct replay_cache_entry *rce;

	mtx_assert(&rc->rc_lock, MA_OWNED);

	if (rc->rc_count < REPLAY_MAX && rc->rc_size <= rc->rc_maxsize)
		return;

	do {
		/*
		 * Try to free an entry. Don't free in-progress entries.
		 */
		TAILQ_FOREACH_REVERSE(rce, &rc->rc_all, replay_cache_list,
		    rce_alllink) {
			if (rce->rce_repmsg.rm_xid)
				break;
		}
		if (rce)
			replay_free(rc, rce);
	} while (rce && (rc->rc_count >= REPLAY_MAX
	    || rc->rc_size > rc->rc_maxsize));
}

enum replay_state
replay_find(struct replay_cache *rc, struct rpc_msg *msg,
    struct sockaddr *addr, struct rpc_msg *repmsg, struct mbuf **mp)
{
	int h = HASHSTEP(HASHINIT, msg->rm_xid) % REPLAY_HASH_SIZE;
	struct replay_cache_entry *rce;

	mtx_lock(&rc->rc_lock);
	TAILQ_FOREACH(rce, &rc->rc_cache[h], rce_link) {
		if (rce->rce_msg.rm_xid == msg->rm_xid
		    && rce->rce_msg.rm_call.cb_prog == msg->rm_call.cb_prog	
		    && rce->rce_msg.rm_call.cb_vers == msg->rm_call.cb_vers
		    && rce->rce_msg.rm_call.cb_proc == msg->rm_call.cb_proc
		    && rce->rce_addr.ss_len == addr->sa_len
		    && bcmp(&rce->rce_addr, addr, addr->sa_len) == 0) {
			if (rce->rce_repmsg.rm_xid) {
				/*
				 * We have a reply for this
				 * message. Copy it and return. Keep
				 * replay_all LRU sorted
				 */
				TAILQ_REMOVE(&rc->rc_all, rce, rce_alllink);
				TAILQ_INSERT_HEAD(&rc->rc_all, rce,
				    rce_alllink);
				*repmsg = rce->rce_repmsg;
				if (rce->rce_repbody) {
					*mp = m_copym(rce->rce_repbody,
					    0, M_COPYALL, M_NOWAIT);
					mtx_unlock(&rc->rc_lock);
					if (!*mp)
						return (RS_ERROR);
				} else {
					mtx_unlock(&rc->rc_lock);
				}
				return (RS_DONE);
			} else {
				mtx_unlock(&rc->rc_lock);
				return (RS_INPROGRESS);
			}
		}
	}

	replay_prune(rc);

	rce = replay_alloc(rc, msg, addr, h);

	mtx_unlock(&rc->rc_lock);

	if (!rce)
		return (RS_ERROR);
	else
		return (RS_NEW);
}

void
replay_setreply(struct replay_cache *rc,
    struct rpc_msg *repmsg, struct sockaddr *addr, struct mbuf *m)
{
	int h = HASHSTEP(HASHINIT, repmsg->rm_xid) % REPLAY_HASH_SIZE;
	struct replay_cache_entry *rce;

	/*
	 * Copy the reply before the lock so we can sleep.
	 */
	if (m)
		m = m_copym(m, 0, M_COPYALL, M_WAITOK);

	mtx_lock(&rc->rc_lock);
	TAILQ_FOREACH(rce, &rc->rc_cache[h], rce_link) {
		if (rce->rce_msg.rm_xid == repmsg->rm_xid
		    && rce->rce_addr.ss_len == addr->sa_len
		    && bcmp(&rce->rce_addr, addr, addr->sa_len) == 0) {
			break;
		}
	}
	if (rce) {
		rce->rce_repmsg = *repmsg;
		rce->rce_repbody = m;
		if (m)
			rc->rc_size += m_length(m, NULL);
	}
	mtx_unlock(&rc->rc_lock);
}
