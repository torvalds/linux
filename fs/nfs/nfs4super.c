/*
 * Copyright (c) 2012 Bryan Schumaker <bjschuma@netapp.com>
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/nfs_idmap.h>
#include <linux/nfs4_mount.h>
#include <linux/nfs_fs.h>
#include "internal.h"
#include "nfs4_fs.h"

#define NFSDBG_FACILITY		NFSDBG_VFS

static struct dentry *nfs4_remote_mount(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *raw_data);
static struct dentry *nfs4_xdev_mount(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *raw_data);
static struct dentry *nfs4_referral_mount(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *raw_data);
static struct dentry *nfs4_remote_referral_mount(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *raw_data);

static struct file_system_type nfs4_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "nfs4",
	.mount		= nfs_fs_mount,
	.kill_sb	= nfs_kill_super,
	.fs_flags	= FS_RENAME_DOES_D_MOVE|FS_REVAL_DOT|FS_BINARY_MOUNTDATA,
};

static struct file_system_type nfs4_remote_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "nfs4",
	.mount		= nfs4_remote_mount,
	.kill_sb	= nfs_kill_super,
	.fs_flags	= FS_RENAME_DOES_D_MOVE|FS_REVAL_DOT|FS_BINARY_MOUNTDATA,
};

struct file_system_type nfs4_xdev_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "nfs4",
	.mount		= nfs4_xdev_mount,
	.kill_sb	= nfs_kill_super,
	.fs_flags	= FS_RENAME_DOES_D_MOVE|FS_REVAL_DOT|FS_BINARY_MOUNTDATA,
};

static struct file_system_type nfs4_remote_referral_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "nfs4",
	.mount		= nfs4_remote_referral_mount,
	.kill_sb	= nfs_kill_super,
	.fs_flags	= FS_RENAME_DOES_D_MOVE|FS_REVAL_DOT|FS_BINARY_MOUNTDATA,
};

struct file_system_type nfs4_referral_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "nfs4",
	.mount		= nfs4_referral_mount,
	.kill_sb	= nfs_kill_super,
	.fs_flags	= FS_RENAME_DOES_D_MOVE|FS_REVAL_DOT|FS_BINARY_MOUNTDATA,
};

static const struct super_operations nfs4_sops = {
	.alloc_inode	= nfs_alloc_inode,
	.destroy_inode	= nfs_destroy_inode,
	.write_inode	= nfs4_write_inode,
	.put_super	= nfs_put_super,
	.statfs		= nfs_statfs,
	.evict_inode	= nfs4_evict_inode,
	.umount_begin	= nfs_umount_begin,
	.show_options	= nfs_show_options,
	.show_devname	= nfs_show_devname,
	.show_path	= nfs_show_path,
	.show_stats	= nfs_show_stats,
	.remount_fs	= nfs_remount,
};

/*
 * Set up an NFS4 superblock
 */
static void nfs4_fill_super(struct super_block *sb,
			    struct nfs_mount_info *mount_info)
{
	sb->s_time_gran = 1;
	sb->s_op = &nfs4_sops;
	/*
	 * The VFS shouldn't apply the umask to mode bits. We will do
	 * so ourselves when necessary.
	 */
	sb->s_flags  |= MS_POSIXACL;
	sb->s_xattr = nfs4_xattr_handlers;
	nfs_initialise_sb(sb);
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

	mount_info->fill_super = nfs4_fill_super;
	mount_info->set_security = nfs_set_sb_security;

	/* Get a volume representation */
	server = nfs4_create_server(mount_info->parsed, mount_info->mntfh);
	if (IS_ERR(server)) {
		mntroot = ERR_CAST(server);
		goto out;
	}

	mntroot = nfs_fs_mount_common(fs_type, server, flags, dev_name, mount_info);

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
			 struct nfs_mount_info *mount_info)
{
	char *export_path;
	struct vfsmount *root_mnt;
	struct dentry *res;
	struct nfs_parsed_mount_data *data = mount_info->parsed;

	dfprintk(MOUNT, "--> nfs4_try_mount()\n");

	mount_info->fill_super = nfs4_fill_super;

	export_path = data->nfs_server.export_path;
	data->nfs_server.export_path = "/";
	root_mnt = nfs_do_root_mount(&nfs4_remote_fs_type, flags, mount_info,
			data->nfs_server.hostname);
	data->nfs_server.export_path = export_path;

	res = nfs_follow_remote_path(root_mnt, export_path);

	dfprintk(MOUNT, "<-- nfs4_try_mount() = %ld%s\n",
			IS_ERR(res) ? PTR_ERR(res) : 0,
			IS_ERR(res) ? " [error]" : "");
	return res;
}

/*
 * Clone an NFS4 server record on xdev traversal (FSID-change)
 */
static struct dentry *
nfs4_xdev_mount(struct file_system_type *fs_type, int flags,
		 const char *dev_name, void *raw_data)
{
	struct nfs_mount_info mount_info = {
		.fill_super = nfs_clone_super,
		.set_security = nfs_clone_sb_security,
		.cloned = raw_data,
	};
	return nfs_xdev_mount_common(&nfs4_fs_type, flags, dev_name, &mount_info);
}

static struct dentry *
nfs4_remote_referral_mount(struct file_system_type *fs_type, int flags,
			   const char *dev_name, void *raw_data)
{
	struct nfs_mount_info mount_info = {
		.fill_super = nfs4_fill_super,
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

	mntroot = nfs_fs_mount_common(&nfs4_fs_type, server, flags, dev_name, &mount_info);
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
	dprintk("<-- nfs4_referral_mount() = %ld%s\n",
			IS_ERR(res) ? PTR_ERR(res) : 0,
			IS_ERR(res) ? " [error]" : "");
	return res;
}


int __init init_nfs_v4(void)
{
	int err;

	err = nfs_idmap_init();
	if (err)
		goto out;

	err = nfs4_register_sysctl();
	if (err)
		goto out1;

	err = register_filesystem(&nfs4_fs_type);
	if (err < 0)
		goto out2;

	return 0;
out2:
	nfs4_unregister_sysctl();
out1:
	nfs_idmap_quit();
out:
	return err;
}

void exit_nfs_v4(void)
{
	unregister_filesystem(&nfs4_fs_type);
	nfs4_unregister_sysctl();
	nfs_idmap_quit();
}
