/*
 * include/net/9p/conn.h
 *
 * Connection Definitions
 *
 *  Copyright (C) 2005 by Latchesar Ionkov <lucho@ionkov.net>
 *  Copyright (C) 2004 by Eric Van Hensbergen <ericvh@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2
 *  as published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to:
 *  Free Software Foundation
 *  51 Franklin Street, Fifth Floor
 *  Boston, MA  02111-1301  USA
 *
 */

#ifndef NET_9P_CONN_H
#define NET_9P_CONN_H

#undef P9_NONBLOCK

struct p9_conn;
struct p9_req;

/**
 * p9_mux_req_callback - callback function that is called when the
 * response of a request is received. The callback is called from
 * a workqueue and shouldn't block.
 *
 * @req - request
 * @a - the pointer that was specified when the request was send to be
 *      passed to the callback
 */
typedef void (*p9_conn_req_callback)(struct p9_req *req, void *a);

struct p9_conn *p9_conn_create(struct p9_trans *trans, int msize,
							unsigned char *dotu);
void p9_conn_destroy(struct p9_conn *);
int p9_conn_rpc(struct p9_conn *m, struct p9_fcall *tc, struct p9_fcall **rc);

#ifdef P9_NONBLOCK
int p9_conn_rpcnb(struct p9_conn *m, struct p9_fcall *tc,
	p9_conn_req_callback cb, void *a);
#endif /* P9_NONBLOCK */

void p9_conn_cancel(struct p9_conn *m, int err);

#endif /* NET_9P_CONN_H */
