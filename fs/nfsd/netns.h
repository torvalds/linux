/*
 * per net namespace data structures for nfsd
 *
 * Copyright (C) 2012, Jeff Layton <jlayton@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#ifndef __NFSD_NETNS_H__
#define __NFSD_NETNS_H__

#include <net/net_namespace.h>
#include <net/netns/generic.h>

/* Hash tables for nfs4_clientid state */
#define CLIENT_HASH_BITS                 4
#define CLIENT_HASH_SIZE                (1 << CLIENT_HASH_BITS)
#define CLIENT_HASH_MASK                (CLIENT_HASH_SIZE - 1)

struct cld_net;

struct nfsd_net {
	struct cld_net *cld_net;

	struct cache_detail *svc_expkey_cache;
	struct cache_detail *svc_export_cache;

	struct cache_detail *idtoname_cache;
	struct cache_detail *nametoid_cache;

	struct lock_manager nfsd4_manager;
	bool grace_ended;
	time_t boot_time;

	/*
	 * reclaim_str_hashtbl[] holds known client info from previous reset/reboot
	 * used in reboot/reset lease grace period processing
	 *
	 * conf_id_hashtbl[], and conf_name_tree hold confirmed
	 * setclientid_confirmed info.
	 */
	struct list_head *reclaim_str_hashtbl;
	int reclaim_str_hashtbl_size;
	struct list_head *conf_id_hashtbl;
	struct rb_root conf_name_tree;
	struct list_head *unconf_id_hashtbl;
};

extern int nfsd_net_id;
#endif /* __NFSD_NETNS_H__ */
