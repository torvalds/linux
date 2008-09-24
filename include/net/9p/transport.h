/*
 * include/net/9p/transport.h
 *
 * Transport Definition
 *
 *  Copyright (C) 2005 by Latchesar Ionkov <lucho@ionkov.net>
 *  Copyright (C) 2004-2008 by Eric Van Hensbergen <ericvh@gmail.com>
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

#include <linux/module.h>

/**
 * enum p9_trans_status - different states of underlying transports
 * @Connected: transport is connected and healthy
 * @Disconnected: transport has been disconnected
 * @Hung: transport is connected by wedged
 *
 * This enumeration details the various states a transport
 * instatiation can be in.
 */

enum p9_trans_status {
	Connected,
	Disconnected,
	Hung,
};

/**
 * struct p9_trans - per-transport state and API
 * @status: transport &p9_trans_status
 * @msize: negotiated maximum packet size (duplicate from client)
 * @extended: negotiated protocol extensions (duplicate from client)
 * @priv: transport private data
 * @close: member function to disconnect and close the transport
 * @rpc: member function to issue a request to the transport
 *
 * This is the basic API for a transport instance.  It is used as
 * a handle by the client to issue requests.  This interface is currently
 * in flux during reorganization.
 *
 * Bugs: there is lots of duplicated data here and its not clear that
 * the member functions need to be per-instance versus per transport
 * module.
 */

struct p9_trans {
	enum p9_trans_status status;
	int msize;
	unsigned char extended;
	void *priv;
	void (*close) (struct p9_trans *);
	int (*rpc) (struct p9_trans *t, struct p9_fcall *tc,
							struct p9_fcall **rc);
};

/**
 * struct p9_trans_module - transport module interface
 * @list: used to maintain a list of currently available transports
 * @name: the human-readable name of the transport
 * @maxsize: transport provided maximum packet size
 * @def: set if this transport should be considered the default
 * @create: member function to create a new connection on this transport
 *
 * This is the basic API for a transport module which is registered by the
 * transport module with the 9P core network module and used by the client
 * to instantiate a new connection on a transport.
 *
 * Bugs: the transport module list isn't protected.
 */

struct p9_trans_module {
	struct list_head list;
	char *name;		/* name of transport */
	int maxsize;		/* max message size of transport */
	int def;		/* this transport should be default */
	struct p9_trans * (*create)(const char *, char *, int, unsigned char);
	struct module *owner;
};

void v9fs_register_trans(struct p9_trans_module *m);
void v9fs_unregister_trans(struct p9_trans_module *m);
struct p9_trans_module *v9fs_get_trans_by_name(const substring_t *name);
struct p9_trans_module *v9fs_get_default_trans(void);
void v9fs_put_trans(struct p9_trans_module *m);
#endif /* NET_9P_TRANSPORT_H */
