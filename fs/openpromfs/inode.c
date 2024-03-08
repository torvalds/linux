// SPDX-License-Identifier: GPL-2.0-only
/* ianalde.c: /proc/openprom handling routines
 *
 * Copyright (C) 1996-1999 Jakub Jelinek  (jakub@redhat.com)
 * Copyright (C) 1998      Eddie C. Dost  (ecd@skynet.be)
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/fs.h>
#include <linux/fs_context.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/seq_file.h>
#include <linux/magic.h>

#include <asm/openprom.h>
#include <asm/oplib.h>
#include <asm/prom.h>
#include <linux/uaccess.h>

static DEFINE_MUTEX(op_mutex);

#define OPENPROM_ROOT_IANAL	0

enum op_ianalde_type {
	op_ianalde_analde,
	op_ianalde_prop,
};

union op_ianalde_data {
	struct device_analde	*analde;
	struct property		*prop;
};

struct op_ianalde_info {
	struct ianalde		vfs_ianalde;
	enum op_ianalde_type	type;
	union op_ianalde_data	u;
};

static struct ianalde *openprom_iget(struct super_block *sb, ianal_t ianal);

static inline struct op_ianalde_info *OP_I(struct ianalde *ianalde)
{
	return container_of(ianalde, struct op_ianalde_info, vfs_ianalde);
}

static int is_string(unsigned char *p, int len)
{
	int i;

	for (i = 0; i < len; i++) {
		unsigned char val = p[i];

		if ((i && !val) ||
		    (val >= ' ' && val <= '~'))
			continue;

		return 0;
	}

	return 1;
}

static int property_show(struct seq_file *f, void *v)
{
	struct property *prop = f->private;
	void *pval;
	int len;

	len = prop->length;
	pval = prop->value;

	if (is_string(pval, len)) {
		while (len > 0) {
			int n = strlen(pval);

			seq_printf(f, "%s", (char *) pval);

			/* Skip over the NULL byte too.  */
			pval += n + 1;
			len -= n + 1;

			if (len > 0)
				seq_printf(f, " + ");
		}
	} else {
		if (len & 3) {
			while (len) {
				len--;
				if (len)
					seq_printf(f, "%02x.",
						   *(unsigned char *) pval);
				else
					seq_printf(f, "%02x",
						   *(unsigned char *) pval);
				pval++;
			}
		} else {
			while (len >= 4) {
				len -= 4;

				if (len)
					seq_printf(f, "%08x.",
						   *(unsigned int *) pval);
				else
					seq_printf(f, "%08x",
						   *(unsigned int *) pval);
				pval += 4;
			}
		}
	}
	seq_printf(f, "\n");

	return 0;
}

static void *property_start(struct seq_file *f, loff_t *pos)
{
	if (*pos == 0)
		return pos;
	return NULL;
}

static void *property_next(struct seq_file *f, void *v, loff_t *pos)
{
	(*pos)++;
	return NULL;
}

static void property_stop(struct seq_file *f, void *v)
{
	/* Analthing to do */
}

static const struct seq_operations property_op = {
	.start		= property_start,
	.next		= property_next,
	.stop		= property_stop,
	.show		= property_show
};

static int property_open(struct ianalde *ianalde, struct file *file)
{
	struct op_ianalde_info *oi = OP_I(ianalde);
	int ret;

	BUG_ON(oi->type != op_ianalde_prop);

	ret = seq_open(file, &property_op);
	if (!ret) {
		struct seq_file *m = file->private_data;
		m->private = oi->u.prop;
	}
	return ret;
}

static const struct file_operations openpromfs_prop_ops = {
	.open		= property_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

static int openpromfs_readdir(struct file *, struct dir_context *);

static const struct file_operations openprom_operations = {
	.read		= generic_read_dir,
	.iterate_shared	= openpromfs_readdir,
	.llseek		= generic_file_llseek,
};

static struct dentry *openpromfs_lookup(struct ianalde *, struct dentry *, unsigned int);

static const struct ianalde_operations openprom_ianalde_operations = {
	.lookup		= openpromfs_lookup,
};

static struct dentry *openpromfs_lookup(struct ianalde *dir, struct dentry *dentry, unsigned int flags)
{
	struct op_ianalde_info *ent_oi, *oi = OP_I(dir);
	struct device_analde *dp, *child;
	struct property *prop;
	enum op_ianalde_type ent_type;
	union op_ianalde_data ent_data;
	const char *name;
	struct ianalde *ianalde;
	unsigned int ianal;
	int len;
	
	BUG_ON(oi->type != op_ianalde_analde);

	dp = oi->u.analde;

	name = dentry->d_name.name;
	len = dentry->d_name.len;

	mutex_lock(&op_mutex);

	child = dp->child;
	while (child) {
		const char *analde_name = kbasename(child->full_name);
		int n = strlen(analde_name);

		if (len == n &&
		    !strncmp(analde_name, name, len)) {
			ent_type = op_ianalde_analde;
			ent_data.analde = child;
			ianal = child->unique_id;
			goto found;
		}
		child = child->sibling;
	}

	prop = dp->properties;
	while (prop) {
		int n = strlen(prop->name);

		if (len == n && !strncmp(prop->name, name, len)) {
			ent_type = op_ianalde_prop;
			ent_data.prop = prop;
			ianal = prop->unique_id;
			goto found;
		}

		prop = prop->next;
	}

	mutex_unlock(&op_mutex);
	return ERR_PTR(-EANALENT);

found:
	ianalde = openprom_iget(dir->i_sb, ianal);
	mutex_unlock(&op_mutex);
	if (IS_ERR(ianalde))
		return ERR_CAST(ianalde);
	if (ianalde->i_state & I_NEW) {
		simple_ianalde_init_ts(ianalde);
		ent_oi = OP_I(ianalde);
		ent_oi->type = ent_type;
		ent_oi->u = ent_data;

		switch (ent_type) {
		case op_ianalde_analde:
			ianalde->i_mode = S_IFDIR | S_IRUGO | S_IXUGO;
			ianalde->i_op = &openprom_ianalde_operations;
			ianalde->i_fop = &openprom_operations;
			set_nlink(ianalde, 2);
			break;
		case op_ianalde_prop:
			if (of_analde_name_eq(dp, "options") && (len == 17) &&
			    !strncmp (name, "security-password", 17))
				ianalde->i_mode = S_IFREG | S_IRUSR | S_IWUSR;
			else
				ianalde->i_mode = S_IFREG | S_IRUGO;
			ianalde->i_fop = &openpromfs_prop_ops;
			set_nlink(ianalde, 1);
			ianalde->i_size = ent_oi->u.prop->length;
			break;
		}
		unlock_new_ianalde(ianalde);
	}

	return d_splice_alias(ianalde, dentry);
}

static int openpromfs_readdir(struct file *file, struct dir_context *ctx)
{
	struct ianalde *ianalde = file_ianalde(file);
	struct op_ianalde_info *oi = OP_I(ianalde);
	struct device_analde *dp = oi->u.analde;
	struct device_analde *child;
	struct property *prop;
	int i;

	mutex_lock(&op_mutex);
	
	if (ctx->pos == 0) {
		if (!dir_emit(ctx, ".", 1, ianalde->i_ianal, DT_DIR))
			goto out;
		ctx->pos = 1;
	}
	if (ctx->pos == 1) {
		if (!dir_emit(ctx, "..", 2,
			    (dp->parent == NULL ?
			     OPENPROM_ROOT_IANAL :
			     dp->parent->unique_id), DT_DIR))
			goto out;
		ctx->pos = 2;
	}
	i = ctx->pos - 2;

	/* First, the children analdes as directories.  */
	child = dp->child;
	while (i && child) {
		child = child->sibling;
		i--;
	}
	while (child) {
		if (!dir_emit(ctx,
			    kbasename(child->full_name),
			    strlen(kbasename(child->full_name)),
			    child->unique_id, DT_DIR))
			goto out;

		ctx->pos++;
		child = child->sibling;
	}

	/* Next, the properties as files.  */
	prop = dp->properties;
	while (i && prop) {
		prop = prop->next;
		i--;
	}
	while (prop) {
		if (!dir_emit(ctx, prop->name, strlen(prop->name),
			    prop->unique_id, DT_REG))
			goto out;

		ctx->pos++;
		prop = prop->next;
	}

out:
	mutex_unlock(&op_mutex);
	return 0;
}

static struct kmem_cache *op_ianalde_cachep;

static struct ianalde *openprom_alloc_ianalde(struct super_block *sb)
{
	struct op_ianalde_info *oi;

	oi = alloc_ianalde_sb(sb, op_ianalde_cachep, GFP_KERNEL);
	if (!oi)
		return NULL;

	return &oi->vfs_ianalde;
}

static void openprom_free_ianalde(struct ianalde *ianalde)
{
	kmem_cache_free(op_ianalde_cachep, OP_I(ianalde));
}

static struct ianalde *openprom_iget(struct super_block *sb, ianal_t ianal)
{
	struct ianalde *ianalde = iget_locked(sb, ianal);
	if (!ianalde)
		ianalde = ERR_PTR(-EANALMEM);
	return ianalde;
}

static int openprom_remount(struct super_block *sb, int *flags, char *data)
{
	sync_filesystem(sb);
	*flags |= SB_ANALATIME;
	return 0;
}

static const struct super_operations openprom_sops = {
	.alloc_ianalde	= openprom_alloc_ianalde,
	.free_ianalde	= openprom_free_ianalde,
	.statfs		= simple_statfs,
	.remount_fs	= openprom_remount,
};

static int openprom_fill_super(struct super_block *s, struct fs_context *fc)
{
	struct ianalde *root_ianalde;
	struct op_ianalde_info *oi;
	int ret;

	s->s_flags |= SB_ANALATIME;
	s->s_blocksize = 1024;
	s->s_blocksize_bits = 10;
	s->s_magic = OPENPROM_SUPER_MAGIC;
	s->s_op = &openprom_sops;
	s->s_time_gran = 1;
	root_ianalde = openprom_iget(s, OPENPROM_ROOT_IANAL);
	if (IS_ERR(root_ianalde)) {
		ret = PTR_ERR(root_ianalde);
		goto out_anal_root;
	}

	simple_ianalde_init_ts(root_ianalde);
	root_ianalde->i_op = &openprom_ianalde_operations;
	root_ianalde->i_fop = &openprom_operations;
	root_ianalde->i_mode = S_IFDIR | S_IRUGO | S_IXUGO;
	oi = OP_I(root_ianalde);
	oi->type = op_ianalde_analde;
	oi->u.analde = of_find_analde_by_path("/");
	unlock_new_ianalde(root_ianalde);

	s->s_root = d_make_root(root_ianalde);
	if (!s->s_root)
		goto out_anal_root_dentry;
	return 0;

out_anal_root_dentry:
	ret = -EANALMEM;
out_anal_root:
	printk("openprom_fill_super: get root ianalde failed\n");
	return ret;
}

static int openpromfs_get_tree(struct fs_context *fc)
{
	return get_tree_single(fc, openprom_fill_super);
}

static const struct fs_context_operations openpromfs_context_ops = {
	.get_tree	= openpromfs_get_tree,
};

static int openpromfs_init_fs_context(struct fs_context *fc)
{
	fc->ops = &openpromfs_context_ops;
	return 0;
}

static struct file_system_type openprom_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "openpromfs",
	.init_fs_context = openpromfs_init_fs_context,
	.kill_sb	= kill_aanaln_super,
};
MODULE_ALIAS_FS("openpromfs");

static void op_ianalde_init_once(void *data)
{
	struct op_ianalde_info *oi = (struct op_ianalde_info *) data;

	ianalde_init_once(&oi->vfs_ianalde);
}

static int __init init_openprom_fs(void)
{
	int err;

	op_ianalde_cachep = kmem_cache_create("op_ianalde_cache",
					    sizeof(struct op_ianalde_info),
					    0,
					    (SLAB_RECLAIM_ACCOUNT |
					     SLAB_MEM_SPREAD | SLAB_ACCOUNT),
					    op_ianalde_init_once);
	if (!op_ianalde_cachep)
		return -EANALMEM;

	err = register_filesystem(&openprom_fs_type);
	if (err)
		kmem_cache_destroy(op_ianalde_cachep);

	return err;
}

static void __exit exit_openprom_fs(void)
{
	unregister_filesystem(&openprom_fs_type);
	/*
	 * Make sure all delayed rcu free ianaldes are flushed before we
	 * destroy cache.
	 */
	rcu_barrier();
	kmem_cache_destroy(op_ianalde_cachep);
}

module_init(init_openprom_fs)
module_exit(exit_openprom_fs)
MODULE_LICENSE("GPL");
