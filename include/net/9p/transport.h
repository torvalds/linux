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

#define P9_TRANS_PREF_PAYLOAD_MASK 0x1

/* Default. Add Payload to PDU before sending it down to transport layer */
#define P9_TRANS_PREF_PAYLOAD_DEF  0x0
/* Send pay load separately to transport layer along with PDU.*/
#define P9_TRANS_PREF_PAYLOAD_SEP  0x1

/**
 * struct p9_trans_module - transport module interface
 * @list: used to maintain a list of currently available transports
 * @name: the human-readable name of the transport
 * @maxsize: transport provided maximum packet size
 * @pref: Preferences of this transport
 * @def: set if this transport should be considered the default
 * @create: member function to create a new connection on this transport
 * @request: member function to issue a request to the transport
 * @cancel: member function to cancel a request (if it hasn't been sent)
 *
 * This is the basic API for a transport module which is registered by the
 * transport module with the 9P core network module and used by the client
 * to instantiate a new connection on a transport.
 *
 * BUGS: the transport module list isn't protected.
 */

struct p9_trans_module {
	struct list_head list;
	char *name;		/* name of transport */
	int maxsize;		/* max message size of transport */
	int pref;               /* Preferences of this transport */
	int def;		/* this transport should be default */
	struct module *owner;
	int (*create)(struct p9_client *, const char *, char *);
	void (*close) (struct p9_client *);
	int (*request) (struct p9_client *, struct p9_req_t *req);
	int (*cancel) (struct p9_client *, struct p9_req_t *req);
};

void v9fs_register_trans(struct p9_trans_module *m);
void v9fs_unregister_trans(struct p9_trans_module *m);
struct p9_trans_module *v9fs_get_trans_by_name(const substring_t *name);
struct p9_trans_module *v9fs_get_default_trans(void);
void v9fs_put_trans(struct p9_trans_module *m);
#endif /* NET_9P_TRANSPORT_H */
