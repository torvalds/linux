// SPDX-License-Identifier: GPL-2.0
/*
 * linux/fs/nfs/cache_lib.c
 *
 * Helper routines for the NFS client caches
 *
 * Copyright (c) 2009 Trond Myklebust <Trond.Myklebust@netapp.com>
 */
#include <linux/kmod.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/mount.h>
#include <linux/namei.h>
#include <linux/slab.h>
#include <linux/sunrpc/cache.h>
#include <linux/sunrpc/rpc_pipe_fs.h>
#include <net/net_namespace.h>

#include "cache_lib.h"

#define NFS_CACHE_UPCALL_PATHLEN 256
#define NFS_CACHE_UPCALL_TIMEOUT 15

static char nfs_cache_getent_prog[NFS_CACHE_UPCALL_PATHLEN] =
				"/sbin/nfs_cache_getent";
static unsigned long nfs_cache_getent_timeout = NFS_CACHE_UPCALL_TIMEOUT;

module_param_string(cache_getent, nfs_cache_getent_prog,
		sizeof(nfs_cache_getent_prog), 0600);
MODULE_PARM_DESC(cache_getent, "Path to the client cache upcall program");
module_param_named(cache_getent_timeout, nfs_cache_getent_timeout, ulong, 0600);
MODULE_PARM_DESC(cache_getent_timeout, "Timeout (in seconds) after which "
		"the cache upcall is assumed to have failed");

int nfs_cache_upcall(struct cache_detail *cd, char *entry_name)
{
	static char *envp[] = { "HOME=/",
		"TERM=linux",
		"PATH=/sbin:/usr/sbin:/bin:/usr/bin",
		NULL
	};
	char *argv[] = {
		nfs_cache_getent_prog,
		cd->name,
		entry_name,
		NULL
	};
	int ret = -EACCES;

	if (nfs_cache_getent_prog[0] == '\0')
		goto out;
	ret = call_usermodehelper(argv[0], argv, envp, UMH_WAIT_EXEC);
	/*
	 * Disable the upcall mechanism if we're getting an ENOENT or
	 * EACCES error. The admin can re-enable it on the fly by using
	 * sysfs to set the 'cache_getent' parameter once the problem
	 * has been fixed.
	 */
	if (ret == -ENOENT || ret == -EACCES)
		nfs_cache_getent_prog[0] = '\0';
out:
	return ret > 0 ? 0 : ret;
}

/*
 * Deferred request handling
 */
void nfs_cache_defer_req_put(struct nfs_cache_defer_req *dreq)
{
	if (refcount_dec_and_test(&dreq->count))
		kfree(dreq);
}

static void nfs_dns_cache_revisit(struct cache_deferred_req *d, int toomany)
{
	struct nfs_cache_defer_req *dreq;

	dreq = container_of(d, struct nfs_cache_defer_req, deferred_req);

	complete(&dreq->completion);
	nfs_cache_defer_req_put(dreq);
}

static struct cache_deferred_req *nfs_dns_cache_defer(struct cache_req *req)
{
	struct nfs_cache_defer_req *dreq;

	dreq = container_of(req, struct nfs_cache_defer_req, req);
	dreq->deferred_req.revisit = nfs_dns_cache_revisit;
	refcount_inc(&dreq->count);

	return &dreq->deferred_req;
}

struct nfs_cache_defer_req *nfs_cache_defer_req_alloc(void)
{
	struct nfs_cache_defer_req *dreq;

	dreq = kzalloc(sizeof(*dreq), GFP_KERNEL);
	if (dreq) {
		init_completion(&dreq->completion);
		refcount_set(&dreq->count, 1);
		dreq->req.defer = nfs_dns_cache_defer;
	}
	return dreq;
}

int nfs_cache_wait_for_upcall(struct nfs_cache_defer_req *dreq)
{
	if (wait_for_completion_timeout(&dreq->completion,
			nfs_cache_getent_timeout * HZ) == 0)
		return -ETIMEDOUT;
	return 0;
}

int nfs_cache_register_sb(struct super_block *sb, struct cache_detail *cd)
{
	int ret;
	struct dentry *dir;

	dir = rpc_d_lookup_sb(sb, "cache");
	ret = sunrpc_cache_register_pipefs(dir, cd->name, 0600, cd);
	dput(dir);
	return ret;
}

int nfs_cache_register_net(struct net *net, struct cache_detail *cd)
{
	struct super_block *pipefs_sb;
	int ret = 0;

	sunrpc_init_cache_detail(cd);
	pipefs_sb = rpc_get_sb_net(net);
	if (pipefs_sb) {
		ret = nfs_cache_register_sb(pipefs_sb, cd);
		rpc_put_sb_net(net);
		if (ret)
			sunrpc_destroy_cache_detail(cd);
	}
	return ret;
}

void nfs_cache_unregister_sb(struct super_block *sb, struct cache_detail *cd)
{
	sunrpc_cache_unregister_pipefs(cd);
}

void nfs_cache_unregister_net(struct net *net, struct cache_detail *cd)
{
	struct super_block *pipefs_sb;

	pipefs_sb = rpc_get_sb_net(net);
	if (pipefs_sb) {
		nfs_cache_unregister_sb(pipefs_sb, cd);
		rpc_put_sb_net(net);
	}
	sunrpc_destroy_cache_detail(cd);
}
