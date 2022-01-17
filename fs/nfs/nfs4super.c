// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2012 Bryan Schumaker <bjschuma@netapp.com>
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mount.h>
#include <linux/nfs4_mount.h>
#include <linux/nfs_fs.h>
#include <linux/nfs_ssc.h>
#include "delegation.h"
#include "internal.h"
#include "nfs4_fs.h"
#include "nfs4idmap.h"
#include "dns_resolve.h"
#include "pnfs.h"
#include "nfs.h"

#define NFSDBG_FACILITY		NFSDBG_VFS

static int nfs4_write_inode(struct inode *inode, struct writeback_control *wbc);
static void nfs4_evict_inode(struct inode *inode);

static const struct super_operations nfs4_sops = {
	.alloc_inode	= nfs_alloc_inode,
	.free_inode	= nfs_free_inode,
	.write_inode	= nfs4_write_inode,
	.drop_inode	= nfs_drop_inode,
	.statfs		= nfs_statfs,
	.evict_inode	= nfs4_evict_inode,
	.umount_begin	= nfs_umount_begin,
	.show_options	= nfs_show_options,
	.show_devname	= nfs_show_devname,
	.show_path	= nfs_show_path,
	.show_stats	= nfs_show_stats,
};

struct nfs_subversion nfs_v4 = {
	.owner		= THIS_MODULE,
	.nfs_fs		= &nfs4_fs_type,
	.rpc_vers	= &nfs_version4,
	.rpc_ops	= &nfs_v4_clientops,
	.sops		= &nfs4_sops,
	.xattr		= nfs4_xattr_handlers,
};

static int nfs4_write_inode(struct inode *inode, struct writeback_control *wbc)
{
	int ret = nfs_write_inode(inode, wbc);

	if (ret == 0)
		ret = pnfs_layoutcommit_inode(inode,
				wbc->sync_mode == WB_SYNC_ALL);
	return ret;
}

/*
 * Clean out any remaining NFSv4 state that might be left over due
 * to open() calls that passed nfs_atomic_lookup, but failed to call
 * nfs_open().
 */
static void nfs4_evict_inode(struct inode *inode)
{
	truncate_inode_pages_final(&inode->i_data);
	clear_inode(inode);
	/* If we are holding a delegation, return and free it */
	nfs_inode_evict_delegation(inode);
	/* Note that above delegreturn would trigger pnfs return-on-close */
	pnfs_return_layout(inode);
	pnfs_destroy_layout_final(NFS_I(inode));
	/* First call standard NFS clear_inode() code */
	nfs_clear_inode(inode);
	nfs4_xattr_cache_zap(inode);
}

struct nfs_referral_count {
	struct list_head list;
	const struct task_struct *task;
	unsigned int referral_count;
};

static LIST_HEAD(nfs_referral_count_list);
static DEFINE_SPINLOCK(nfs_referral_count_list_lock);

static struct nfs_referral_count *nfs_find_referral_count(void)
{
	struct nfs_referral_count *p;

	list_for_each_entry(p, &nfs_referral_count_list, list) {
		if (p->task == current)
			return p;
	}
	return NULL;
}

#define NFS_MAX_NESTED_REFERRALS 2

static int nfs_referral_loop_protect(void)
{
	struct nfs_referral_count *p, *new;
	int ret = -ENOMEM;

	new = kmalloc(sizeof(*new), GFP_KERNEL);
	if (!new)
		goto out;
	new->task = current;
	new->referral_count = 1;

	ret = 0;
	spin_lock(&nfs_referral_count_list_lock);
	p = nfs_find_referral_count();
	if (p != NULL) {
		if (p->referral_count >= NFS_MAX_NESTED_REFERRALS)
			ret = -ELOOP;
		else
			p->referral_count++;
	} else {
		list_add(&new->list, &nfs_referral_count_list);
		new = NULL;
	}
	spin_unlock(&nfs_referral_count_list_lock);
	kfree(new);
out:
	return ret;
}

static void nfs_referral_loop_unprotect(void)
{
	struct nfs_referral_count *p;

	spin_lock(&nfs_referral_count_list_lock);
	p = nfs_find_referral_count();
	p->referral_count--;
	if (p->referral_count == 0)
		list_del(&p->list);
	else
		p = NULL;
	spin_unlock(&nfs_referral_count_list_lock);
	kfree(p);
}

static int do_nfs4_mount(struct nfs_server *server,
			 struct fs_context *fc,
			 const char *hostname,
			 const char *export_path)
{
	struct nfs_fs_context *root_ctx;
	struct fs_context *root_fc;
	struct vfsmount *root_mnt;
	struct dentry *dentry;
	size_t len;
	int ret;

	struct fs_parameter param = {
		.key	= "source",
		.type	= fs_value_is_string,
		.dirfd	= -1,
	};

	if (IS_ERR(server))
		return PTR_ERR(server);

	root_fc = vfs_dup_fs_context(fc);
	if (IS_ERR(root_fc)) {
		nfs_free_server(server);
		return PTR_ERR(root_fc);
	}
	kfree(root_fc->source);
	root_fc->source = NULL;

	root_ctx = nfs_fc2context(root_fc);
	root_ctx->internal = true;
	root_ctx->server = server;
	/* We leave export_path unset as it's not used to find the root. */

	len = strlen(hostname) + 5;
	param.string = kmalloc(len, GFP_KERNEL);
	if (param.string == NULL) {
		put_fs_context(root_fc);
		return -ENOMEM;
	}

	/* Does hostname needs to be enclosed in brackets? */
	if (strchr(hostname, ':'))
		param.size = snprintf(param.string, len, "[%s]:/", hostname);
	else
		param.size = snprintf(param.string, len, "%s:/", hostname);
	ret = vfs_parse_fs_param(root_fc, &param);
	kfree(param.string);
	if (ret < 0) {
		put_fs_context(root_fc);
		return ret;
	}
	root_mnt = fc_mount(root_fc);
	put_fs_context(root_fc);

	if (IS_ERR(root_mnt))
		return PTR_ERR(root_mnt);

	ret = nfs_referral_loop_protect();
	if (ret) {
		mntput(root_mnt);
		return ret;
	}

	dentry = mount_subtree(root_mnt, export_path);
	nfs_referral_loop_unprotect();

	if (IS_ERR(dentry))
		return PTR_ERR(dentry);

	fc->root = dentry;
	return 0;
}

int nfs4_try_get_tree(struct fs_context *fc)
{
	struct nfs_fs_context *ctx = nfs_fc2context(fc);
	int err;

	dfprintk(MOUNT, "--> nfs4_try_get_tree()\n");

	/* We create a mount for the server's root, walk to the requested
	 * location and then create another mount for that.
	 */
	err= do_nfs4_mount(nfs4_create_server(fc),
			   fc, ctx->nfs_server.hostname,
			   ctx->nfs_server.export_path);
	if (err) {
		nfs_ferrorf(fc, MOUNT, "NFS4: Couldn't follow remote path");
		dfprintk(MOUNT, "<-- nfs4_try_get_tree() = %d [error]\n", err);
	} else {
		dfprintk(MOUNT, "<-- nfs4_try_get_tree() = 0\n");
	}
	return err;
}

/*
 * Create an NFS4 server record on referral traversal
 */
int nfs4_get_referral_tree(struct fs_context *fc)
{
	struct nfs_fs_context *ctx = nfs_fc2context(fc);
	int err;

	dprintk("--> nfs4_referral_mount()\n");

	/* create a new volume representation */
	err = do_nfs4_mount(nfs4_create_referral_server(fc),
			    fc, ctx->nfs_server.hostname,
			    ctx->nfs_server.export_path);
	if (err) {
		nfs_ferrorf(fc, MOUNT, "NFS4: Couldn't follow remote path");
		dfprintk(MOUNT, "<-- nfs4_get_referral_tree() = %d [error]\n", err);
	} else {
		dfprintk(MOUNT, "<-- nfs4_get_referral_tree() = 0\n");
	}
	return err;
}

static int __init init_nfs_v4(void)
{
	int err;

	err = nfs_dns_resolver_init();
	if (err)
		goto out;

	err = nfs_idmap_init();
	if (err)
		goto out1;

#ifdef CONFIG_NFS_V4_2
	err = nfs4_xattr_cache_init();
	if (err)
		goto out2;
#endif

	err = nfs4_register_sysctl();
	if (err)
		goto out2;

#ifdef CONFIG_NFS_V4_2
	nfs42_ssc_register_ops();
#endif
	register_nfs_version(&nfs_v4);
	return 0;
out2:
	nfs_idmap_quit();
out1:
	nfs_dns_resolver_destroy();
out:
	return err;
}

static void __exit exit_nfs_v4(void)
{
	/* Not called in the _init(), conditionally loaded */
	nfs4_pnfs_v3_ds_connect_unload();

	unregister_nfs_version(&nfs_v4);
#ifdef CONFIG_NFS_V4_2
	nfs4_xattr_cache_exit();
	nfs42_ssc_unregister_ops();
#endif
	nfs4_unregister_sysctl();
	nfs_idmap_quit();
	nfs_dns_resolver_destroy();
}

MODULE_LICENSE("GPL");
MODULE_IMPORT_NS(ANDROID_GKI_VFS_EXPORT_ONLY);

module_init(init_nfs_v4);
module_exit(exit_nfs_v4);
