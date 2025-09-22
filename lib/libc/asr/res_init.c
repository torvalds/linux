/*	$OpenBSD: res_init.c,v 1.11 2019/06/17 05:54:45 otto Exp $	*/
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
#include <arpa/nameser.h>
#include <netinet/in.h>
#include <netdb.h>

#include <asr.h>
#include <resolv.h>
#include <string.h>

#include "asr_private.h"
#include "thread_private.h"


struct __res_state _res;
struct __res_state_ext _res_ext;

int h_errno;

int
res_init(void)
{
	static void *resinit_mutex;
	struct asr_ctx	*ac;
	int i;

	ac = _asr_use_resolver(NULL);

	/*
	 * The first thread to call res_init() will setup the global _res
	 * structure from the async context, not overriding fields set early
	 * by the user.
	 */
	_MUTEX_LOCK(&resinit_mutex);
	if (!(_res.options & RES_INIT)) {
		if (_res.retry == 0)
			_res.retry = ac->ac_nsretries;
		if (_res.retrans == 0)
			_res.retrans = ac->ac_nstimeout;
		if (_res.options == 0)
			_res.options = ac->ac_options;
		if (_res.lookups[0] == '\0')
			strlcpy(_res.lookups, ac->ac_db, sizeof(_res.lookups));

		for (i = 0; i < ac->ac_nscount && i < MAXNS; i++) {
			/*
			 * No need to check for length since we copy to a
			 * struct sockaddr_storage with a size of 256 bytes
			 * and sa_len has only 8 bits.
			 */
			memcpy(&_res_ext.nsaddr_list[i], ac->ac_ns[i],
			    ac->ac_ns[i]->sa_len);
			if (ac->ac_ns[i]->sa_len <= sizeof(_res.nsaddr_list[i]))
				memcpy(&_res.nsaddr_list[i], ac->ac_ns[i],
				    ac->ac_ns[i]->sa_len);
			else
				memset(&_res.nsaddr_list[i], 0,
				    sizeof(_res.nsaddr_list[i]));
		}
		_res.nscount = i;
		_res.options |= RES_INIT;
	}
	_MUTEX_UNLOCK(&resinit_mutex);

	/*
	 * If the program is not threaded, we want to reflect (some) changes
	 * made by the user to the global _res structure.
	 * This is a bit of a hack: if there is already an async query on
	 * this context, it might change things in its back.  It is ok
	 * as long as the user only uses the blocking resolver API.
	 * If needed we could consider cloning the context if there is
	 * a running query.
	 */
	if (!__isthreaded) {
		ac->ac_nsretries = _res.retry;
		ac->ac_nstimeout = _res.retrans;
		ac->ac_options = _res.options;
		strlcpy(ac->ac_db, _res.lookups, sizeof(ac->ac_db));
		ac->ac_dbcount = strlen(ac->ac_db);
	}

	_asr_ctx_unref(ac);

	return (0);
}
DEF_WEAK(res_init);
