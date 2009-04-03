/* NFS filesystem cache interface
 *
 * Copyright (C) 2008 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/nfs_fs.h>
#include <linux/nfs_fs_sb.h>
#include <linux/in6.h>
#include <linux/seq_file.h>

#include "internal.h"
#include "fscache.h"

#define NFSDBG_FACILITY		NFSDBG_FSCACHE

static struct rb_root nfs_fscache_keys = RB_ROOT;
static DEFINE_SPINLOCK(nfs_fscache_keys_lock);

/*
 * Get the per-client index cookie for an NFS client if the appropriate mount
 * flag was set
 * - We always try and get an index cookie for the client, but get filehandle
 *   cookies on a per-superblock basis, depending on the mount flags
 */
void nfs_fscache_get_client_cookie(struct nfs_client *clp)
{
	/* create a cache index for looking up filehandles */
	clp->fscache = fscache_acquire_cookie(nfs_fscache_netfs.primary_index,
					      &nfs_fscache_server_index_def,
					      clp);
	dfprintk(FSCACHE, "NFS: get client cookie (0x%p/0x%p)\n",
		 clp, clp->fscache);
}

/*
 * Dispose of a per-client cookie
 */
void nfs_fscache_release_client_cookie(struct nfs_client *clp)
{
	dfprintk(FSCACHE, "NFS: releasing client cookie (0x%p/0x%p)\n",
		 clp, clp->fscache);

	fscache_relinquish_cookie(clp->fscache, 0);
	clp->fscache = NULL;
}

/*
 * Get the cache cookie for an NFS superblock.  We have to handle
 * uniquification here because the cache doesn't do it for us.
 */
void nfs_fscache_get_super_cookie(struct super_block *sb,
				  struct nfs_parsed_mount_data *data)
{
	struct nfs_fscache_key *key, *xkey;
	struct nfs_server *nfss = NFS_SB(sb);
	struct rb_node **p, *parent;
	const char *uniq = data->fscache_uniq ?: "";
	int diff, ulen;

	ulen = strlen(uniq);
	key = kzalloc(sizeof(*key) + ulen, GFP_KERNEL);
	if (!key)
		return;

	key->nfs_client = nfss->nfs_client;
	key->key.super.s_flags = sb->s_flags & NFS_MS_MASK;
	key->key.nfs_server.flags = nfss->flags;
	key->key.nfs_server.rsize = nfss->rsize;
	key->key.nfs_server.wsize = nfss->wsize;
	key->key.nfs_server.acregmin = nfss->acregmin;
	key->key.nfs_server.acregmax = nfss->acregmax;
	key->key.nfs_server.acdirmin = nfss->acdirmin;
	key->key.nfs_server.acdirmax = nfss->acdirmax;
	key->key.nfs_server.fsid = nfss->fsid;
	key->key.rpc_auth.au_flavor = nfss->client->cl_auth->au_flavor;

	key->key.uniq_len = ulen;
	memcpy(key->key.uniquifier, uniq, ulen);

	spin_lock(&nfs_fscache_keys_lock);
	p = &nfs_fscache_keys.rb_node;
	parent = NULL;
	while (*p) {
		parent = *p;
		xkey = rb_entry(parent, struct nfs_fscache_key, node);

		if (key->nfs_client < xkey->nfs_client)
			goto go_left;
		if (key->nfs_client > xkey->nfs_client)
			goto go_right;

		diff = memcmp(&key->key, &xkey->key, sizeof(key->key));
		if (diff < 0)
			goto go_left;
		if (diff > 0)
			goto go_right;

		if (key->key.uniq_len == 0)
			goto non_unique;
		diff = memcmp(key->key.uniquifier,
			      xkey->key.uniquifier,
			      key->key.uniq_len);
		if (diff < 0)
			goto go_left;
		if (diff > 0)
			goto go_right;
		goto non_unique;

	go_left:
		p = &(*p)->rb_left;
		continue;
	go_right:
		p = &(*p)->rb_right;
	}

	rb_link_node(&key->node, parent, p);
	rb_insert_color(&key->node, &nfs_fscache_keys);
	spin_unlock(&nfs_fscache_keys_lock);
	nfss->fscache_key = key;

	/* create a cache index for looking up filehandles */
	nfss->fscache = fscache_acquire_cookie(nfss->nfs_client->fscache,
					       &nfs_fscache_super_index_def,
					       nfss);
	dfprintk(FSCACHE, "NFS: get superblock cookie (0x%p/0x%p)\n",
		 nfss, nfss->fscache);
	return;

non_unique:
	spin_unlock(&nfs_fscache_keys_lock);
	kfree(key);
	nfss->fscache_key = NULL;
	nfss->fscache = NULL;
	printk(KERN_WARNING "NFS:"
	       " Cache request denied due to non-unique superblock keys\n");
}

/*
 * release a per-superblock cookie
 */
void nfs_fscache_release_super_cookie(struct super_block *sb)
{
	struct nfs_server *nfss = NFS_SB(sb);

	dfprintk(FSCACHE, "NFS: releasing superblock cookie (0x%p/0x%p)\n",
		 nfss, nfss->fscache);

	fscache_relinquish_cookie(nfss->fscache, 0);
	nfss->fscache = NULL;

	if (nfss->fscache_key) {
		spin_lock(&nfs_fscache_keys_lock);
		rb_erase(&nfss->fscache_key->node, &nfs_fscache_keys);
		spin_unlock(&nfs_fscache_keys_lock);
		kfree(nfss->fscache_key);
		nfss->fscache_key = NULL;
	}
}
