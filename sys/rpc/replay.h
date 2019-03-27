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
 *
 * $FreeBSD$
 */

#ifndef _RPC_REPLAY_H
#define _RPC_REPLAY_H

enum replay_state {
	RS_NEW,			/* new request - caller should execute */
	RS_DONE,		/* request was executed and reply sent */
	RS_INPROGRESS,		/* request is being executed now */
	RS_ERROR		/* allocation or other failure */
};

struct replay_cache;

/*
 * Create a new replay cache.
 */
struct replay_cache	*replay_newcache(size_t);

/*
 * Set the replay cache size.
 */
void			replay_setsize(struct replay_cache *, size_t);

/*
 * Free a replay cache. Caller must ensure that no cache entries are
 * in-progress.
 */
void			replay_freecache(struct replay_cache *rc);

/*
 * Check a replay cache for a message from a given address.
 *
 * If this is a new request, RS_NEW is returned. Caller should call
 * replay_setreply with the results of the request.
 *
 * If this is a request which is currently executing
 * (i.e. replay_setreply hasn't been called for it yet), RS_INPROGRESS
 * is returned. The caller should silently drop the request.
 *
 * If a reply to this message already exists, *repmsg and *mp are set
 * to point at the reply and, RS_DONE is returned. The caller should
 * re-send this reply.
 *
 * If the attempt to update the replay cache or copy a replay failed
 * for some reason (typically memory shortage), RS_ERROR is returned.
 */
enum replay_state	replay_find(struct replay_cache *rc,
    struct rpc_msg *msg, struct sockaddr *addr,
    struct rpc_msg *repmsg, struct mbuf **mp);

/*
 * Call this after executing a request to record the reply.
 */
void			replay_setreply(struct replay_cache *rc,
    struct rpc_msg *repmsg,  struct sockaddr *addr, struct mbuf *m);

#endif /* !_RPC_REPLAY_H */
