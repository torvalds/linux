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

enum p9_trans_status {
	Connected,
	Disconnected,
	Hung,
};

struct p9_trans {
	enum p9_trans_status status;
	void *priv;
	int (*write) (struct p9_trans *, void *, int);
	int (*read) (struct p9_trans *, void *, int);
	void (*close) (struct p9_trans *);
	unsigned int (*poll)(struct p9_trans *, struct poll_table_struct *);
};

struct p9_trans_module {
	struct list_head list;
	char *name;		/* name of transport */
	int maxsize;		/* max message size of transport */
	int def;		/* this transport should be default */
	struct p9_trans * (*create)(const char *devname, char *options);
};

void v9fs_register_trans(struct p9_trans_module *m);
struct p9_trans_module *v9fs_match_trans(const substring_t *name);
struct p9_trans_module *v9fs_default_trans(void);

#endif /* NET_9P_TRANSPORT_H */
