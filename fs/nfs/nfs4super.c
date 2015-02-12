/*
 * Copyright (c) 2012 Bryan Schumaker <bjschuma@netapp.com>
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/nfs_idmap.h>
#include <linux/nfs4_mount.h>
#include <linux/nfs_fs.h>
#include "delegation.h"
#include "internal.h"
#include "nfs4_fs.h"
#include "dns_resolve.h"
#include "pnfs.h"
#include "nfs.h"

#define NFSDBG_FACILITY		NFSDBG_VFS

static int nfs4_write_inode(struct inode *inode, struct writeback_control *wbc);
static void nfs4_evict_inode(struct inode *inode);
static struct dentry *nfs4_remote_mount(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *raw_data);
static struct dentry *nfs4_referral_mount(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *raw_data);
static struct dentry *nfs4_remote_referral_mount(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *raw_data);

static struct file_system_type nfs4_remote_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "nfs4",
	.mount		= nfs4_remote_mount,
	.kill_sb	= nfs_kill_super,
	.fs_flags	= FS_RENAME_DOES_D_MOVE|FS_BINARY_MOUNTDATA,
};

static struct file_system_type nfs4_remote_referral_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "nfs4",
	.mount		= nfs4_remote_referral_mount,
	.kill_sb	= nfs_kill_super,
	.fs_flags	= FS_RENAME_DOES_D_MOVE|FS_BINARY_MOUNTDATA,
};

struct file_system_type nfs4_referral_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "nfs4",
	.mount		= nfs4_referral_mount,
	.kill_sb	= nfs_kill_super,
	.fs_flags	= FS_RENAME_DOES_D_MOVE|FS_BINARY_MOUNTDATA,
};

static const struct super_operations nfs4_sops = {
	.alloc_inode	= nfs_alloc_inode,
	.destroy_inode	= nfs_destroy_inode,
	.write_inode	= nfs4_write_inode,
	.drop_inode	= nfs_drop_inode,
	.statfs		= nfs_statfs,
	.evict_inode	= nfs4_evict_inode,
	.umount_begin	= nfs_umount_begin,
	.show_options	= nfs_show_options,
	.show_devname	= nfs_show_devname,
	.show_path	= nfs_show_path,
	.show_stats	= nfs_show_stats,
	.remount_fs	= nfs_remount,
};

struct nfs_subversion nfs_v4 = {
	.owner = THIS_MODULE,
	.nfs_fs   = &nfs4_fs_type,
	.rpc_vers = &nfs_version4,
	.rpc_ops  = &nfs_v4_clientops,
	.sops     = &nfs4_sops,
	.xattr    = nfs4_xattr_handlers,
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
	pnfs_return_layout(inode);
	pnfs_destroy_layout(NFS_I(inode));
	/* If we are holding a delegation, return it! */
	nfs_inode_return_delegation_noreclaim(inode);
	/* First call standard NFS clear_inode() code */
	nfs_clear_inode(inode);
}

/*
 * Get the superblock for the NFS4 root partition
 */
static struct dentry *
nfs4_remote_mount(struct file_system_type *fs_type, int flags,
		  const char *dev_name, void *info)
{
	struct nfs_mount_info *mount_info = info;
	struct nfs_server *server;
	struct dentry *mntroot = ERR_PTR(-ENOMEM);

	mount_info->set_security = nfs_set_sb_security;

	/* Get a volume representation */
	server = nfs4_create_server(mount_info, &nfs_v4);
	if (IS_ERR(server)) {
		mntroot = ERR_CAST(server);
		goto out;
	}

	mntroot = nfs_fs_mount_common(server, flags, dev_name, mount_info, &nfs_v4);

out:
	return mntroot;
}

static struct vfsmount *nfs_do_root_mount(struct file_system_type *fs_type,
		int flags, void *data, const char *hostname)
{
	struct vfsmount *root_mnt;
	char *root_devname;
	size_t len;

	len = strlen(hostname) + 5;
	root_devname = kmalloc(len, GFP_KERNEL);
	if (root_devname == NULL)
		return ERR_PTR(-ENOMEM);
	/* Does hostname needs to be enclosed in brackets? */
	if (strchr(hostname, ':'))
		snprintf(root_devname, len, "[%s]:/", hostname);
	else
		snprintf(root_devname, len, "%s:/", hostname);
	root_mnt = vfs_kern_mount(fs_type, flags, root_devname, data);
	kfree(root_devname);
	return root_mnt;
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

static struct dentry *nfs_follow_remote_path(struct vfsmount *root_mnt,
		const char *export_path)
{
	struct dentry *dentry;
	int err;

	if (IS_ERR(root_mnt))
		return ERR_CAST(root_mnt);

	err = nfs_referral_loop_protect();
	if (err) {
		mntput(root_mnt);
		return ERR_PTR(err);
	}

	dentry = mount_subtree(root_mnt, export_path);
	nfs_referral_loop_unprotect();

	return dentry;
}

struct dentry *nfs4_try_mount(int flags, const char *dev_name,
			      struct nfs_mount_info *mount_info,
			      struct nfs_subversion *nfs_mod)
{
	char *export_path;
	struct vfsmount *root_mnt;
	struct dentry *res;
	struct nfs_parsed_mount_data *data = mount_info->parsed;

	dfprintk(MOUNT, "--> nfs4_try_mount()\n");

	export_path = data->nfs_server.export_path;
	data->nfs_server.export_path = "/";
	root_mnt = nfs_do_root_mount(&nfs4_remote_fs_type, flags, mount_info,
			data->nfs_server.hostname);
	data->nfs_server.export_path = export_path;

	res = nfs_follow_remote_path(root_mnt, export_path);

	dfprintk(MOUNT, "<-- nfs4_try_mount() = %d%s\n",
		 PTR_ERR_OR_ZERO(res),
		 IS_ERR(res) ? " [error]" : "");
	return res;
}

static struct dentry *
nfs4_remote_referral_mount(struct file_system_type *fs_type, int flags,
			   const char *dev_name, void *raw_data)
{
	struct nfs_mount_info mount_info = {
		.fill_super = nfs_fill_super,
		.set_security = nfs_clone_sb_security,
		.cloned = raw_data,
	};
	struct nfs_server *server;
	struct dentry *mntroot = ERR_PTR(-ENOMEM);

	dprintk("--> nfs4_referral_get_sb()\n");

	mount_info.mntfh = nfs_alloc_fhandle();
	if (mount_info.cloned == NULL || mount_info.mntfh == NULL)
		goto out;

	/* create a new volume representation */
	server = nfs4_create_referral_server(mount_info.cloned, mount_info.mntfh);
	if (IS_ERR(server)) {
		mntroot = ERR_CAST(server);
		goto out;
	}

	mntroot = nfs_fs_mount_common(server, flags, dev_name, &mount_info, &nfs_v4);
out:
	nfs_free_fhandle(mount_info.mntfh);
	return mntroot;
}

/*
 * Create an NFS4 server record on referral traversal
 */
static struct dentry *nfs4_referral_mount(struct file_system_type *fs_type,
		int flags, const char *dev_name, void *raw_data)
{
	struct nfs_clone_mount *data = raw_data;
	char *export_path;
	struct vfsmount *root_mnt;
	struct dentry *res;

	dprintk("--> nfs4_referral_mount()\n");

	export_path = data->mnt_path;
	data->mnt_path = "/";

	root_mnt = nfs_do_root_mount(&nfs4_remote_referral_fs_type,
			flags, data, data->hostname);
	data->mnt_path = export_path;

	res = nfs_follow_remote_path(root_mnt, export_path);
	dprintk("<-- nfs4_referral_mount() = %d%s\n",
		PTR_ERR_OR_ZERO(res),
		IS_ERR(res) ? " [error]" : "");
	return res;
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

	err = nfs4_register_sysctl();
	if (err)
		goto out2;

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
	nfs4_unregister_sysctl();
	nfs_idmap_quit();
	nfs_dns_resolver_destroy();
}

MODULE_LICENSE("GPL");

module_init(init_nfs_v4);
module_exit(exit_nfs_v4);
