/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2024 Mike Snitzer <snitzer@hammerspace.com>
 * Copyright (C) 2024 NeilBrown <neilb@suse.de>
 */
#ifndef __LINUX_NFSLOCALIO_H
#define __LINUX_NFSLOCALIO_H

#include <linux/module.h>
#include <linux/list.h>
#include <linux/uuid.h>
#include <linux/sunrpc/svcauth.h>
#include <linux/nfs.h>
#include <net/net_namespace.h>

/*
 * Useful to allow a client to negotiate if localio
 * possible with its server.
 *
 * See Documentation/filesystems/nfs/localio.rst for more detail.
 */
typedef struct {
	uuid_t uuid;
	struct list_head list;
	struct net *net; /* nfsd's network namespace */
	struct auth_domain *dom; /* auth_domain for localio */
} nfs_uuid_t;

void nfs_uuid_begin(nfs_uuid_t *);
void nfs_uuid_end(nfs_uuid_t *);
void nfs_uuid_is_local(const uuid_t *, struct list_head *,
		       struct net *, struct auth_domain *, struct module *);
void nfs_uuid_invalidate_clients(struct list_head *list);
void nfs_uuid_invalidate_one_client(nfs_uuid_t *nfs_uuid);

#endif  /* __LINUX_NFSLOCALIO_H */
