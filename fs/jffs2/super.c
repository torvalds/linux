/*
 * JFFS2 -- Journalling Flash File System, Version 2.
 *
 * Copyright © 2001-2007 Red Hat, Inc.
 *
 * Created by David Woodhouse <dwmw2@infradead.org>
 *
 * For licensing information, see the file 'LICENCE' in this directory.
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/fs.h>
#include <linux/err.h>
#include <linux/mount.h>
#include <linux/fs_context.h>
#include <linux/fs_parser.h>
#include <linux/jffs2.h>
#include <linux/pagemap.h>
#include <linux/mtd/super.h>
#include <linux/ctype.h>
#include <linux/namei.h>
#include <linux/seq_file.h>
#include <linux/exportfs.h>
#include "compr.h"
#include "nodelist.h"

static void jffs2_put_super(struct super_block *);

static struct kmem_cache *jffs2_inode_cachep;

static struct inode *jffs2_alloc_inode(struct super_block *sb)
{
	struct jffs2_inode_info *f;

	f = alloc_inode_sb(sb, jffs2_inode_cachep, GFP_KERNEL);
	if (!f)
		return NULL;
	return &f->vfs_inode;
}

static void jffs2_free_inode(struct inode *inode)
{
	struct jffs2_inode_info *f = JFFS2_INODE_INFO(inode);

	kfree(f->target);
	kmem_cache_free(jffs2_inode_cachep, f);
}

static void jffs2_i_init_once(void *foo)
{
	struct jffs2_inode_info *f = foo;

	mutex_init(&f->sem);
	inode_init_once(&f->vfs_inode);
}

static const char *jffs2_compr_name(unsigned int compr)
{
	switch (compr) {
	case JFFS2_COMPR_MODE_NONE:
		return "none";
#ifdef CONFIG_JFFS2_LZO
	case JFFS2_COMPR_MODE_FORCELZO:
		return "lzo";
#endif
#ifdef CONFIG_JFFS2_ZLIB
	case JFFS2_COMPR_MODE_FORCEZLIB:
		return "zlib";
#endif
	default:
		/* should never happen; programmer error */
		WARN_ON(1);
		return "";
	}
}

static int jffs2_show_options(struct seq_file *s, struct dentry *root)
{
	struct jffs2_sb_info *c = JFFS2_SB_INFO(root->d_sb);
	struct jffs2_mount_opts *opts = &c->mount_opts;

	if (opts->override_compr)
		seq_printf(s, ",compr=%s", jffs2_compr_name(opts->compr));
	if (opts->set_rp_size)
		seq_printf(s, ",rp_size=%u", opts->rp_size / 1024);

	return 0;
}

static int jffs2_sync_fs(struct super_block *sb, int wait)
{
	struct jffs2_sb_info *c = JFFS2_SB_INFO(sb);

#ifdef CONFIG_JFFS2_FS_WRITEBUFFER
	if (jffs2_is_writebuffered(c))
		cancel_delayed_work_sync(&c->wbuf_dwork);
#endif

	mutex_lock(&c->alloc_sem);
	jffs2_flush_wbuf_pad(c);
	mutex_unlock(&c->alloc_sem);
	return 0;
}

static struct inode *jffs2_nfs_get_inode(struct super_block *sb, uint64_t ino,
					 uint32_t generation)
{
	/* We don't care about i_generation. We'll destroy the flash
	   before we start re-using inode numbers anyway. And even
	   if that wasn't true, we'd have other problems...*/
	return jffs2_iget(sb, ino);
}

static struct dentry *jffs2_fh_to_dentry(struct super_block *sb, struct fid *fid,
					 int fh_len, int fh_type)
{
        return generic_fh_to_dentry(sb, fid, fh_len, fh_type,
                                    jffs2_nfs_get_inode);
}

static struct dentry *jffs2_fh_to_parent(struct super_block *sb, struct fid *fid,
					 int fh_len, int fh_type)
{
        return generic_fh_to_parent(sb, fid, fh_len, fh_type,
                                    jffs2_nfs_get_inode);
}

static struct dentry *jffs2_get_parent(struct dentry *child)
{
	struct jffs2_inode_info *f;
	uint32_t pino;

	BUG_ON(!d_is_dir(child));

	f = JFFS2_INODE_INFO(d_inode(child));

	pino = f->inocache->pino_nlink;

	JFFS2_DEBUG("Parent of directory ino #%u is #%u\n",
		    f->inocache->ino, pino);

	return d_obtain_alias(jffs2_iget(child->d_sb, pino));
}

static const struct export_operations jffs2_export_ops = {
	.encode_fh = generic_encode_ino32_fh,
	.get_parent = jffs2_get_parent,
	.fh_to_dentry = jffs2_fh_to_dentry,
	.fh_to_parent = jffs2_fh_to_parent,
};

/*
 * JFFS2 mount options.
 *
 * Opt_source: The source device
 * Opt_override_compr: override default compressor
 * Opt_rp_size: size of reserved pool in KiB
 */
enum {
	Opt_override_compr,
	Opt_rp_size,
};

static const struct constant_table jffs2_param_compr[] = {
	{"none",	JFFS2_COMPR_MODE_NONE },
#ifdef CONFIG_JFFS2_LZO
	{"lzo",		JFFS2_COMPR_MODE_FORCELZO },
#endif
#ifdef CONFIG_JFFS2_ZLIB
	{"zlib",	JFFS2_COMPR_MODE_FORCEZLIB },
#endif
	{}
};

static const struct fs_parameter_spec jffs2_fs_parameters[] = {
	fsparam_enum	("compr",	Opt_override_compr, jffs2_param_compr),
	fsparam_u32	("rp_size",	Opt_rp_size),
	{}
};

static int jffs2_parse_param(struct fs_context *fc, struct fs_parameter *param)
{
	struct fs_parse_result result;
	struct jffs2_sb_info *c = fc->s_fs_info;
	int opt;

	opt = fs_parse(fc, jffs2_fs_parameters, param, &result);
	if (opt < 0)
		return opt;

	switch (opt) {
	case Opt_override_compr:
		c->mount_opts.compr = result.uint_32;
		c->mount_opts.override_compr = true;
		break;
	case Opt_rp_size:
		if (result.uint_32 > UINT_MAX / 1024)
			return invalf(fc, "jffs2: rp_size unrepresentable");
		c->mount_opts.rp_size = result.uint_32 * 1024;
		c->mount_opts.set_rp_size = true;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static inline void jffs2_update_mount_opts(struct fs_context *fc)
{
	struct jffs2_sb_info *new_c = fc->s_fs_info;
	struct jffs2_sb_info *c = JFFS2_SB_INFO(fc->root->d_sb);

	mutex_lock(&c->alloc_sem);
	if (new_c->mount_opts.override_compr) {
		c->mount_opts.override_compr = new_c->mount_opts.override_compr;
		c->mount_opts.compr = new_c->mount_opts.compr;
	}
	if (new_c->mount_opts.set_rp_size) {
		c->mount_opts.set_rp_size = new_c->mount_opts.set_rp_size;
		c->mount_opts.rp_size = new_c->mount_opts.rp_size;
	}
	mutex_unlock(&c->alloc_sem);
}

static int jffs2_reconfigure(struct fs_context *fc)
{
	struct super_block *sb = fc->root->d_sb;

	sync_filesystem(sb);
	jffs2_update_mount_opts(fc);

	return jffs2_do_remount_fs(sb, fc);
}

static const struct super_operations jffs2_super_operations =
{
	.alloc_inode =	jffs2_alloc_inode,
	.free_inode =	jffs2_free_inode,
	.put_super =	jffs2_put_super,
	.statfs =	jffs2_statfs,
	.evict_inode =	jffs2_evict_inode,
	.dirty_inode =	jffs2_dirty_inode,
	.show_options =	jffs2_show_options,
	.sync_fs =	jffs2_sync_fs,
};

/*
 * fill in the superblock
 */
static int jffs2_fill_super(struct super_block *sb, struct fs_context *fc)
{
	struct jffs2_sb_info *c = sb->s_fs_info;

	jffs2_dbg(1, "jffs2_get_sb_mtd():"
		  " New superblock for device %d (\"%s\")\n",
		  sb->s_mtd->index, sb->s_mtd->name);

	c->mtd = sb->s_mtd;
	c->os_priv = sb;

	if (c->mount_opts.rp_size > c->mtd->size)
		return invalf(fc, "jffs2: Too large reserve pool specified, max is %llu KB",
			      c->mtd->size / 1024);

	/* Initialize JFFS2 superblock locks, the further initialization will
	 * be done later */
	mutex_init(&c->alloc_sem);
	mutex_init(&c->erase_free_sem);
	init_waitqueue_head(&c->erase_wait);
	init_waitqueue_head(&c->inocache_wq);
	spin_lock_init(&c->erase_completion_lock);
	spin_lock_init(&c->inocache_lock);

	sb->s_op = &jffs2_super_operations;
	sb->s_export_op = &jffs2_export_ops;
	sb->s_flags = sb->s_flags | SB_NOATIME;
	sb->s_xattr = jffs2_xattr_handlers;
#ifdef CONFIG_JFFS2_FS_POSIX_ACL
	sb->s_flags |= SB_POSIXACL;
#endif
	return jffs2_do_fill_super(sb, fc);
}

static int jffs2_get_tree(struct fs_context *fc)
{
	return get_tree_mtd(fc, jffs2_fill_super);
}

static void jffs2_free_fc(struct fs_context *fc)
{
	kfree(fc->s_fs_info);
}

static const struct fs_context_operations jffs2_context_ops = {
	.free		= jffs2_free_fc,
	.parse_param	= jffs2_parse_param,
	.get_tree	= jffs2_get_tree,
	.reconfigure	= jffs2_reconfigure,
};

static int jffs2_init_fs_context(struct fs_context *fc)
{
	struct jffs2_sb_info *ctx;

	ctx = kzalloc(sizeof(struct jffs2_sb_info), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	fc->s_fs_info = ctx;
	fc->ops = &jffs2_context_ops;
	return 0;
}

static void jffs2_put_super (struct super_block *sb)
{
	struct jffs2_sb_info *c = JFFS2_SB_INFO(sb);

	jffs2_dbg(2, "%s()\n", __func__);

	mutex_lock(&c->alloc_sem);
	jffs2_flush_wbuf_pad(c);
	mutex_unlock(&c->alloc_sem);

	jffs2_sum_exit(c);

	jffs2_free_ino_caches(c);
	jffs2_free_raw_node_refs(c);
	kvfree(c->blocks);
	jffs2_flash_cleanup(c);
	kfree(c->inocache_list);
	jffs2_clear_xattr_subsystem(c);
	mtd_sync(c->mtd);
	jffs2_dbg(1, "%s(): returning\n", __func__);
}

static void jffs2_kill_sb(struct super_block *sb)
{
	struct jffs2_sb_info *c = JFFS2_SB_INFO(sb);
	if (c && !sb_rdonly(sb))
		jffs2_stop_garbage_collect_thread(c);
	kill_mtd_super(sb);
	kfree(c);
}

static struct file_system_type jffs2_fs_type = {
	.owner =	THIS_MODULE,
	.name =		"jffs2",
	.init_fs_context = jffs2_init_fs_context,
	.parameters =	jffs2_fs_parameters,
	.kill_sb =	jffs2_kill_sb,
};
MODULE_ALIAS_FS("jffs2");

static int __init init_jffs2_fs(void)
{
	int ret;

	/* Paranoia checks for on-medium structures. If we ask GCC
	   to pack them with __attribute__((packed)) then it _also_
	   assumes that they're not aligned -- so it emits crappy
	   code on some architectures. Ideally we want an attribute
	   which means just 'no padding', without the alignment
	   thing. But GCC doesn't have that -- we have to just
	   hope the structs are the right sizes, instead. */
	BUILD_BUG_ON(sizeof(struct jffs2_unknown_node) != 12);
	BUILD_BUG_ON(sizeof(struct jffs2_raw_dirent) != 40);
	BUILD_BUG_ON(sizeof(struct jffs2_raw_inode) != 68);
	BUILD_BUG_ON(sizeof(struct jffs2_raw_summary) != 32);

	pr_info("version 2.2."
#ifdef CONFIG_JFFS2_FS_WRITEBUFFER
	       " (NAND)"
#endif
#ifdef CONFIG_JFFS2_SUMMARY
	       " (SUMMARY) "
#endif
	       " © 2001-2006 Red Hat, Inc.\n");

	jffs2_inode_cachep = kmem_cache_create("jffs2_i",
					     sizeof(struct jffs2_inode_info),
					     0, (SLAB_RECLAIM_ACCOUNT|
						SLAB_ACCOUNT),
					     jffs2_i_init_once);
	if (!jffs2_inode_cachep) {
		pr_err("error: Failed to initialise inode cache\n");
		return -ENOMEM;
	}
	ret = jffs2_compressors_init();
	if (ret) {
		pr_err("error: Failed to initialise compressors\n");
		goto out;
	}
	ret = jffs2_create_slab_caches();
	if (ret) {
		pr_err("error: Failed to initialise slab caches\n");
		goto out_compressors;
	}
	ret = register_filesystem(&jffs2_fs_type);
	if (ret) {
		pr_err("error: Failed to register filesystem\n");
		goto out_slab;
	}
	return 0;

 out_slab:
	jffs2_destroy_slab_caches();
 out_compressors:
	jffs2_compressors_exit();
 out:
	kmem_cache_destroy(jffs2_inode_cachep);
	return ret;
}

static void __exit exit_jffs2_fs(void)
{
	unregister_filesystem(&jffs2_fs_type);
	jffs2_destroy_slab_caches();
	jffs2_compressors_exit();

	/*
	 * Make sure all delayed rcu free inodes are flushed before we
	 * destroy cache.
	 */
	rcu_barrier();
	kmem_cache_destroy(jffs2_inode_cachep);
}

module_init(init_jffs2_fs);
module_exit(exit_jffs2_fs);

MODULE_DESCRIPTION("The Journalling Flash File System, v2");
MODULE_AUTHOR("Red Hat, Inc.");
MODULE_LICENSE("GPL"); // Actually dual-licensed, but it doesn't matter for
		       // the sake of this tag. It's Free Software.
