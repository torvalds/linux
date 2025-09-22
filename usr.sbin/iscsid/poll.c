/*
 * Copyright (c) 2021 Dr Ashton Fagg <ashton@fagg.id.au>
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
#include <sys/socket.h>
#include <sys/uio.h>

#include <event.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "iscsid.h"
#include "log.h"

void
poll_session(struct session_poll *p, struct session *s)
{
	if (!s)
		fatal("poll_session failed: invalid session");

	++p->session_count;

	/*
	 * If SESS_RUNNING, the session has either been brought up
	 * successfully, or has failed.
	 */
	if (s->state & SESS_RUNNING) {
		struct connection *c;
		int is_logged_in = 0;
		int is_failed = 0;
		int is_waiting = 0;

		++p->session_running_count;

		/* Next, check what state the connections are in. */
		TAILQ_FOREACH(c, &s->connections, entry) {
			if (c->state & CONN_LOGGED_IN)
				++is_logged_in;
			else if (c->state & CONN_FAILED)
				++is_failed;
			else if (c->state & CONN_NEVER_LOGGED_IN)
				++is_waiting;

		}

		/*
		 * Potentially, we have multiple connections for each session.
		 * Handle this by saying that a single connection logging
		 * in takes precedent over a failed connection. Only say
		 * the session login has failed if none of the connections
		 * have logged in and nothing is in flight.
		 */

		if (is_logged_in)
			++p->conn_logged_in_count;
		if (is_failed)
			++p->conn_failed_count;
		if (is_waiting)
			++p->conn_waiting_count;

	/* Sessions in SESS_INIT need to be waited on. */
	} else if (s->state & SESS_INIT)
		++p->session_init_count;
	else
		fatal("poll_session: unknown state.");
}

/*
 * Set session_poll sess_conn_status based on the number of sessions
 * versus number of logged in or failed sessions. If the numbers are
 * equal iscsictl can stop polling since all sessions reached a stable state.
 */
void
poll_finalize(struct session_poll *p)
{
	p->sess_conn_status = (p->session_count == p->conn_logged_in_count +
	    p->conn_failed_count);
}
