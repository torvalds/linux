/*
 * include/net/9p/transport.h
 *
 * Transport Definition
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

#ifndef NET_9P_TRANSPORT_H
#define NET_9P_TRANSPORT_H

enum p9_transport_status {
	Connected,
	Disconnected,
	Hung,
};

struct p9_transport {
	enum p9_transport_status status;
	void *priv;

	int (*write) (struct p9_transport *, void *, int);
	int (*read) (struct p9_transport *, void *, int);
	void (*close) (struct p9_transport *);
	unsigned int (*poll)(struct p9_transport *, struct poll_table_struct *);
};

struct p9_transport *p9_trans_create_tcp(const char *addr, int port);
struct p9_transport *p9_trans_create_unix(const char *addr);
struct p9_transport *p9_trans_create_fd(int rfd, int wfd);

#endif /* NET_9P_TRANSPORT_H */
