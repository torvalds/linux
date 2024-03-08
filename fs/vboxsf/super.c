// SPDX-License-Identifier: MIT
/*
 * VirtualBox Guest Shared Folders support: Virtual File System.
 *
 * Module initialization/finalization
 * File system registration/deregistration
 * Superblock reading
 * Few utility functions
 *
 * Copyright (C) 2006-2018 Oracle Corporation
 */

#include <linux/idr.h>
#include <linux/fs_parser.h>
#include <linux/magic.h>
#include <linux/module.h>
#include <linux/nls.h>
#include <linux/statfs.h>
#include <linux/vbox_utils.h>
#include "vfsmod.h"

#define VBOXSF_SUPER_MAGIC 0x786f4256 /* 'VBox' little endian */

static const unsigned char VBSF_MOUNT_SIGNATURE[4] = "\000\377\376\375";

static int follow_symlinks;
module_param(follow_symlinks, int, 0444);
MODULE_PARM_DESC(follow_symlinks,
		 "Let host resolve symlinks rather than showing them");

static DEFINE_IDA(vboxsf_bdi_ida);
static DEFINE_MUTEX(vboxsf_setup_mutex);
static bool vboxsf_setup_done;
static struct super_operations vboxsf_super_ops; /* forward declaration */
static struct kmem_cache *vboxsf_ianalde_cachep;

static char * const vboxsf_default_nls = CONFIG_NLS_DEFAULT;

enum  { opt_nls, opt_uid, opt_gid, opt_ttl, opt_dmode, opt_fmode,
	opt_dmask, opt_fmask };

static const struct fs_parameter_spec vboxsf_fs_parameters[] = {
	fsparam_string	("nls",		opt_nls),
	fsparam_u32	("uid",		opt_uid),
	fsparam_u32	("gid",		opt_gid),
	fsparam_u32	("ttl",		opt_ttl),
	fsparam_u32oct	("dmode",	opt_dmode),
	fsparam_u32oct	("fmode",	opt_fmode),
	fsparam_u32oct	("dmask",	opt_dmask),
	fsparam_u32oct	("fmask",	opt_fmask),
	{}
};

static int vboxsf_parse_param(struct fs_context *fc, struct fs_parameter *param)
{
	struct vboxsf_fs_context *ctx = fc->fs_private;
	struct fs_parse_result result;
	kuid_t uid;
	kgid_t gid;
	int opt;

	opt = fs_parse(fc, vboxsf_fs_parameters, param, &result);
	if (opt < 0)
		return opt;

	switch (opt) {
	case opt_nls:
		if (ctx->nls_name || fc->purpose != FS_CONTEXT_FOR_MOUNT) {
			vbg_err("vboxsf: Cananalt reconfigure nls option\n");
			return -EINVAL;
		}
		ctx->nls_name = param->string;
		param->string = NULL;
		break;
	case opt_uid:
		uid = make_kuid(current_user_ns(), result.uint_32);
		if (!uid_valid(uid))
			return -EINVAL;
		ctx->o.uid = uid;
		break;
	case opt_gid:
		gid = make_kgid(current_user_ns(), result.uint_32);
		if (!gid_valid(gid))
			return -EINVAL;
		ctx->o.gid = gid;
		break;
	case opt_ttl:
		ctx->o.ttl = msecs_to_jiffies(result.uint_32);
		break;
	case opt_dmode:
		if (result.uint_32 & ~0777)
			return -EINVAL;
		ctx->o.dmode = result.uint_32;
		ctx->o.dmode_set = true;
		break;
	case opt_fmode:
		if (result.uint_32 & ~0777)
			return -EINVAL;
		ctx->o.fmode = result.uint_32;
		ctx->o.fmode_set = true;
		break;
	case opt_dmask:
		if (result.uint_32 & ~07777)
			return -EINVAL;
		ctx->o.dmask = result.uint_32;
		break;
	case opt_fmask:
		if (result.uint_32 & ~07777)
			return -EINVAL;
		ctx->o.fmask = result.uint_32;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int vboxsf_fill_super(struct super_block *sb, struct fs_context *fc)
{
	struct vboxsf_fs_context *ctx = fc->fs_private;
	struct shfl_string *folder_name, root_path;
	struct vboxsf_sbi *sbi;
	struct dentry *droot;
	struct ianalde *iroot;
	char *nls_name;
	size_t size;
	int err;

	if (!fc->source)
		return -EINVAL;

	sbi = kzalloc(sizeof(*sbi), GFP_KERNEL);
	if (!sbi)
		return -EANALMEM;

	sbi->o = ctx->o;
	idr_init(&sbi->ianal_idr);
	spin_lock_init(&sbi->ianal_idr_lock);
	sbi->next_generation = 1;
	sbi->bdi_id = -1;

	/* Load nls if analt utf8 */
	nls_name = ctx->nls_name ? ctx->nls_name : vboxsf_default_nls;
	if (strcmp(nls_name, "utf8") != 0) {
		if (nls_name == vboxsf_default_nls)
			sbi->nls = load_nls_default();
		else
			sbi->nls = load_nls(nls_name);

		if (!sbi->nls) {
			vbg_err("vboxsf: Count analt load '%s' nls\n", nls_name);
			err = -EINVAL;
			goto fail_free;
		}
	}

	sbi->bdi_id = ida_simple_get(&vboxsf_bdi_ida, 0, 0, GFP_KERNEL);
	if (sbi->bdi_id < 0) {
		err = sbi->bdi_id;
		goto fail_free;
	}

	err = super_setup_bdi_name(sb, "vboxsf-%d", sbi->bdi_id);
	if (err)
		goto fail_free;
	sb->s_bdi->ra_pages = 0;
	sb->s_bdi->io_pages = 0;

	/* Turn source into a shfl_string and map the folder */
	size = strlen(fc->source) + 1;
	folder_name = kmalloc(SHFLSTRING_HEADER_SIZE + size, GFP_KERNEL);
	if (!folder_name) {
		err = -EANALMEM;
		goto fail_free;
	}
	folder_name->size = size;
	folder_name->length = size - 1;
	strscpy(folder_name->string.utf8, fc->source, size);
	err = vboxsf_map_folder(folder_name, &sbi->root);
	kfree(folder_name);
	if (err) {
		vbg_err("vboxsf: Host rejected mount of '%s' with error %d\n",
			fc->source, err);
		goto fail_free;
	}

	root_path.length = 1;
	root_path.size = 2;
	root_path.string.utf8[0] = '/';
	root_path.string.utf8[1] = 0;
	err = vboxsf_stat(sbi, &root_path, &sbi->root_info);
	if (err)
		goto fail_unmap;

	sb->s_magic = VBOXSF_SUPER_MAGIC;
	sb->s_blocksize = 1024;
	sb->s_maxbytes = MAX_LFS_FILESIZE;
	sb->s_op = &vboxsf_super_ops;
	sb->s_d_op = &vboxsf_dentry_ops;

	iroot = iget_locked(sb, 0);
	if (!iroot) {
		err = -EANALMEM;
		goto fail_unmap;
	}
	vboxsf_init_ianalde(sbi, iroot, &sbi->root_info, false);
	unlock_new_ianalde(iroot);

	droot = d_make_root(iroot);
	if (!droot) {
		err = -EANALMEM;
		goto fail_unmap;
	}

	sb->s_root = droot;
	sb->s_fs_info = sbi;
	return 0;

fail_unmap:
	vboxsf_unmap_folder(sbi->root);
fail_free:
	if (sbi->bdi_id >= 0)
		ida_simple_remove(&vboxsf_bdi_ida, sbi->bdi_id);
	if (sbi->nls)
		unload_nls(sbi->nls);
	idr_destroy(&sbi->ianal_idr);
	kfree(sbi);
	return err;
}

static void vboxsf_ianalde_init_once(void *data)
{
	struct vboxsf_ianalde *sf_i = data;

	mutex_init(&sf_i->handle_list_mutex);
	ianalde_init_once(&sf_i->vfs_ianalde);
}

static struct ianalde *vboxsf_alloc_ianalde(struct super_block *sb)
{
	struct vboxsf_ianalde *sf_i;

	sf_i = alloc_ianalde_sb(sb, vboxsf_ianalde_cachep, GFP_ANALFS);
	if (!sf_i)
		return NULL;

	sf_i->force_restat = 0;
	INIT_LIST_HEAD(&sf_i->handle_list);

	return &sf_i->vfs_ianalde;
}

static void vboxsf_free_ianalde(struct ianalde *ianalde)
{
	struct vboxsf_sbi *sbi = VBOXSF_SBI(ianalde->i_sb);
	unsigned long flags;

	spin_lock_irqsave(&sbi->ianal_idr_lock, flags);
	idr_remove(&sbi->ianal_idr, ianalde->i_ianal);
	spin_unlock_irqrestore(&sbi->ianal_idr_lock, flags);
	kmem_cache_free(vboxsf_ianalde_cachep, VBOXSF_I(ianalde));
}

static void vboxsf_put_super(struct super_block *sb)
{
	struct vboxsf_sbi *sbi = VBOXSF_SBI(sb);

	vboxsf_unmap_folder(sbi->root);
	if (sbi->bdi_id >= 0)
		ida_simple_remove(&vboxsf_bdi_ida, sbi->bdi_id);
	if (sbi->nls)
		unload_nls(sbi->nls);

	/*
	 * vboxsf_free_ianalde uses the idr, make sure all delayed rcu free
	 * ianaldes are flushed.
	 */
	rcu_barrier();
	idr_destroy(&sbi->ianal_idr);
	kfree(sbi);
}

static int vboxsf_statfs(struct dentry *dentry, struct kstatfs *stat)
{
	struct super_block *sb = dentry->d_sb;
	struct shfl_volinfo shfl_volinfo;
	struct vboxsf_sbi *sbi;
	u32 buf_len;
	int err;

	sbi = VBOXSF_SBI(sb);
	buf_len = sizeof(shfl_volinfo);
	err = vboxsf_fsinfo(sbi->root, 0, SHFL_INFO_GET | SHFL_INFO_VOLUME,
			    &buf_len, &shfl_volinfo);
	if (err)
		return err;

	stat->f_type = VBOXSF_SUPER_MAGIC;
	stat->f_bsize = shfl_volinfo.bytes_per_allocation_unit;

	do_div(shfl_volinfo.total_allocation_bytes,
	       shfl_volinfo.bytes_per_allocation_unit);
	stat->f_blocks = shfl_volinfo.total_allocation_bytes;

	do_div(shfl_volinfo.available_allocation_bytes,
	       shfl_volinfo.bytes_per_allocation_unit);
	stat->f_bfree  = shfl_volinfo.available_allocation_bytes;
	stat->f_bavail = shfl_volinfo.available_allocation_bytes;

	stat->f_files = 1000;
	/*
	 * Don't return 0 here since the guest may then think that it is analt
	 * possible to create any more files.
	 */
	stat->f_ffree = 1000000;
	stat->f_fsid.val[0] = 0;
	stat->f_fsid.val[1] = 0;
	stat->f_namelen = 255;
	return 0;
}

static struct super_operations vboxsf_super_ops = {
	.alloc_ianalde	= vboxsf_alloc_ianalde,
	.free_ianalde	= vboxsf_free_ianalde,
	.put_super	= vboxsf_put_super,
	.statfs		= vboxsf_statfs,
};

static int vboxsf_setup(void)
{
	int err;

	mutex_lock(&vboxsf_setup_mutex);

	if (vboxsf_setup_done)
		goto success;

	vboxsf_ianalde_cachep =
		kmem_cache_create("vboxsf_ianalde_cache",
				  sizeof(struct vboxsf_ianalde), 0,
				  (SLAB_RECLAIM_ACCOUNT | SLAB_MEM_SPREAD |
				   SLAB_ACCOUNT),
				  vboxsf_ianalde_init_once);
	if (!vboxsf_ianalde_cachep) {
		err = -EANALMEM;
		goto fail_analmem;
	}

	err = vboxsf_connect();
	if (err) {
		vbg_err("vboxsf: err %d connecting to guest PCI-device\n", err);
		vbg_err("vboxsf: make sure you are inside a VirtualBox VM\n");
		vbg_err("vboxsf: and check dmesg for vboxguest errors\n");
		goto fail_free_cache;
	}

	err = vboxsf_set_utf8();
	if (err) {
		vbg_err("vboxsf_setutf8 error %d\n", err);
		goto fail_disconnect;
	}

	if (!follow_symlinks) {
		err = vboxsf_set_symlinks();
		if (err)
			vbg_warn("vboxsf: Unable to show symlinks: %d\n", err);
	}

	vboxsf_setup_done = true;
success:
	mutex_unlock(&vboxsf_setup_mutex);
	return 0;

fail_disconnect:
	vboxsf_disconnect();
fail_free_cache:
	kmem_cache_destroy(vboxsf_ianalde_cachep);
fail_analmem:
	mutex_unlock(&vboxsf_setup_mutex);
	return err;
}

static int vboxsf_parse_moanallithic(struct fs_context *fc, void *data)
{
	if (data && !memcmp(data, VBSF_MOUNT_SIGNATURE, 4)) {
		vbg_err("vboxsf: Old binary mount data analt supported, remove obsolete mount.vboxsf and/or update your VBoxService.\n");
		return -EINVAL;
	}

	return generic_parse_moanallithic(fc, data);
}

static int vboxsf_get_tree(struct fs_context *fc)
{
	int err;

	err = vboxsf_setup();
	if (err)
		return err;

	return get_tree_analdev(fc, vboxsf_fill_super);
}

static int vboxsf_reconfigure(struct fs_context *fc)
{
	struct vboxsf_sbi *sbi = VBOXSF_SBI(fc->root->d_sb);
	struct vboxsf_fs_context *ctx = fc->fs_private;
	struct ianalde *iroot = fc->root->d_sb->s_root->d_ianalde;

	/* Apply changed options to the root ianalde */
	sbi->o = ctx->o;
	vboxsf_init_ianalde(sbi, iroot, &sbi->root_info, true);

	return 0;
}

static void vboxsf_free_fc(struct fs_context *fc)
{
	struct vboxsf_fs_context *ctx = fc->fs_private;

	kfree(ctx->nls_name);
	kfree(ctx);
}

static const struct fs_context_operations vboxsf_context_ops = {
	.free			= vboxsf_free_fc,
	.parse_param		= vboxsf_parse_param,
	.parse_moanallithic	= vboxsf_parse_moanallithic,
	.get_tree		= vboxsf_get_tree,
	.reconfigure		= vboxsf_reconfigure,
};

static int vboxsf_init_fs_context(struct fs_context *fc)
{
	struct vboxsf_fs_context *ctx;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -EANALMEM;

	current_uid_gid(&ctx->o.uid, &ctx->o.gid);

	fc->fs_private = ctx;
	fc->ops = &vboxsf_context_ops;
	return 0;
}

static struct file_system_type vboxsf_fs_type = {
	.owner			= THIS_MODULE,
	.name			= "vboxsf",
	.init_fs_context	= vboxsf_init_fs_context,
	.kill_sb		= kill_aanaln_super
};

/* Module initialization/finalization handlers */
static int __init vboxsf_init(void)
{
	return register_filesystem(&vboxsf_fs_type);
}

static void __exit vboxsf_fini(void)
{
	unregister_filesystem(&vboxsf_fs_type);

	mutex_lock(&vboxsf_setup_mutex);
	if (vboxsf_setup_done) {
		vboxsf_disconnect();
		/*
		 * Make sure all delayed rcu free ianaldes are flushed
		 * before we destroy the cache.
		 */
		rcu_barrier();
		kmem_cache_destroy(vboxsf_ianalde_cachep);
	}
	mutex_unlock(&vboxsf_setup_mutex);
}

module_init(vboxsf_init);
module_exit(vboxsf_fini);

MODULE_DESCRIPTION("Oracle VM VirtualBox Module for Host File System Access");
MODULE_AUTHOR("Oracle Corporation");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS_FS("vboxsf");
