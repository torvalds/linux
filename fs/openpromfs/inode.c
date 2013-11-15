/* inode.c: /proc/openprom handling routines
 *
 * Copyright (C) 1996-1999 Jakub Jelinek  (jakub@redhat.com)
 * Copyright (C) 1998      Eddie C. Dost  (ecd@skynet.be)
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/seq_file.h>
#include <linux/magic.h>

#include <asm/openprom.h>
#include <asm/oplib.h>
#include <asm/prom.h>
#include <asm/uaccess.h>

static DEFINE_MUTEX(op_mutex);

#define OPENPROM_ROOT_INO	0

enum op_inode_type {
	op_inode_node,
	op_inode_prop,
};

union op_inode_data {
	struct device_node	*node;
	struct property		*prop;
};

struct op_inode_info {
	struct inode		vfs_inode;
	enum op_inode_type	type;
	union op_inode_data	u;
};

static struct inode *openprom_iget(struct super_block *sb, ino_t ino);

static inline struct op_inode_info *OP_I(struct inode *inode)
{
	return container_of(inode, struct op_inode_info, vfs_inode);
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
	/* Nothing to do */
}

static const struct seq_operations property_op = {
	.start		= property_start,
	.next		= property_next,
	.stop		= property_stop,
	.show		= property_show
};

static int property_open(struct inode *inode, struct file *file)
{
	struct op_inode_info *oi = OP_I(inode);
	int ret;

	BUG_ON(oi->type != op_inode_prop);

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
	.iterate	= openpromfs_readdir,
	.llseek		= generic_file_llseek,
};

static struct dentry *openpromfs_lookup(struct inode *, struct dentry *, unsigned int);

static const struct inode_operations openprom_inode_operations = {
	.lookup		= openpromfs_lookup,
};

static struct dentry *openpromfs_lookup(struct inode *dir, struct dentry *dentry, unsigned int flags)
{
	struct op_inode_info *ent_oi, *oi = OP_I(dir);
	struct device_node *dp, *child;
	struct property *prop;
	enum op_inode_type ent_type;
	union op_inode_data ent_data;
	const char *name;
	struct inode *inode;
	unsigned int ino;
	int len;
	
	BUG_ON(oi->type != op_inode_node);

	dp = oi->u.node;

	name = dentry->d_name.name;
	len = dentry->d_name.len;

	mutex_lock(&op_mutex);

	child = dp->child;
	while (child) {
		int n = strlen(child->path_component_name);

		if (len == n &&
		    !strncmp(child->path_component_name, name, len)) {
			ent_type = op_inode_node;
			ent_data.node = child;
			ino = child->unique_id;
			goto found;
		}
		child = child->sibling;
	}

	prop = dp->properties;
	while (prop) {
		int n = strlen(prop->name);

		if (len == n && !strncmp(prop->name, name, len)) {
			ent_type = op_inode_prop;
			ent_data.prop = prop;
			ino = prop->unique_id;
			goto found;
		}

		prop = prop->next;
	}

	mutex_unlock(&op_mutex);
	return ERR_PTR(-ENOENT);

found:
	inode = openprom_iget(dir->i_sb, ino);
	mutex_unlock(&op_mutex);
	if (IS_ERR(inode))
		return ERR_CAST(inode);
	ent_oi = OP_I(inode);
	ent_oi->type = ent_type;
	ent_oi->u = ent_data;

	switch (ent_type) {
	case op_inode_node:
		inode->i_mode = S_IFDIR | S_IRUGO | S_IXUGO;
		inode->i_op = &openprom_inode_operations;
		inode->i_fop = &openprom_operations;
		set_nlink(inode, 2);
		break;
	case op_inode_prop:
		if (!strcmp(dp->name, "options") && (len == 17) &&
		    !strncmp (name, "security-password", 17))
			inode->i_mode = S_IFREG | S_IRUSR | S_IWUSR;
		else
			inode->i_mode = S_IFREG | S_IRUGO;
		inode->i_fop = &openpromfs_prop_ops;
		set_nlink(inode, 1);
		inode->i_size = ent_oi->u.prop->length;
		break;
	}

	d_add(dentry, inode);
	return NULL;
}

static int openpromfs_readdir(struct file *file, struct dir_context *ctx)
{
	struct inode *inode = file_inode(file);
	struct op_inode_info *oi = OP_I(inode);
	struct device_node *dp = oi->u.node;
	struct device_node *child;
	struct property *prop;
	int i;

	mutex_lock(&op_mutex);
	
	if (ctx->pos == 0) {
		if (!dir_emit(ctx, ".", 1, inode->i_ino, DT_DIR))
			goto out;
		ctx->pos = 1;
	}
	if (ctx->pos == 1) {
		if (!dir_emit(ctx, "..", 2,
			    (dp->parent == NULL ?
			     OPENPROM_ROOT_INO :
			     dp->parent->unique_id), DT_DIR))
			goto out;
		ctx->pos = 2;
	}
	i = ctx->pos - 2;

	/* First, the children nodes as directories.  */
	child = dp->child;
	while (i && child) {
		child = child->sibling;
		i--;
	}
	while (child) {
		if (!dir_emit(ctx,
			    child->path_component_name,
			    strlen(child->path_component_name),
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

static struct kmem_cache *op_inode_cachep;

static struct inode *openprom_alloc_inode(struct super_block *sb)
{
	struct op_inode_info *oi;

	oi = kmem_cache_alloc(op_inode_cachep, GFP_KERNEL);
	if (!oi)
		return NULL;

	return &oi->vfs_inode;
}

static void openprom_i_callback(struct rcu_head *head)
{
	struct inode *inode = container_of(head, struct inode, i_rcu);
	kmem_cache_free(op_inode_cachep, OP_I(inode));
}

static void openprom_destroy_inode(struct inode *inode)
{
	call_rcu(&inode->i_rcu, openprom_i_callback);
}

static struct inode *openprom_iget(struct super_block *sb, ino_t ino)
{
	struct inode *inode;

	inode = iget_locked(sb, ino);
	if (!inode)
		return ERR_PTR(-ENOMEM);
	if (inode->i_state & I_NEW) {
		inode->i_mtime = inode->i_atime = inode->i_ctime = CURRENT_TIME;
		if (inode->i_ino == OPENPROM_ROOT_INO) {
			inode->i_op = &openprom_inode_operations;
			inode->i_fop = &openprom_operations;
			inode->i_mode = S_IFDIR | S_IRUGO | S_IXUGO;
		}
		unlock_new_inode(inode);
	}
	return inode;
}

static int openprom_remount(struct super_block *sb, int *flags, char *data)
{
	*flags |= MS_NOATIME;
	return 0;
}

static const struct super_operations openprom_sops = {
	.alloc_inode	= openprom_alloc_inode,
	.destroy_inode	= openprom_destroy_inode,
	.statfs		= simple_statfs,
	.remount_fs	= openprom_remount,
};

static int openprom_fill_super(struct super_block *s, void *data, int silent)
{
	struct inode *root_inode;
	struct op_inode_info *oi;
	int ret;

	s->s_flags |= MS_NOATIME;
	s->s_blocksize = 1024;
	s->s_blocksize_bits = 10;
	s->s_magic = OPENPROM_SUPER_MAGIC;
	s->s_op = &openprom_sops;
	s->s_time_gran = 1;
	root_inode = openprom_iget(s, OPENPROM_ROOT_INO);
	if (IS_ERR(root_inode)) {
		ret = PTR_ERR(root_inode);
		goto out_no_root;
	}

	oi = OP_I(root_inode);
	oi->type = op_inode_node;
	oi->u.node = of_find_node_by_path("/");

	s->s_root = d_make_root(root_inode);
	if (!s->s_root)
		goto out_no_root_dentry;
	return 0;

out_no_root_dentry:
	ret = -ENOMEM;
out_no_root:
	printk("openprom_fill_super: get root inode failed\n");
	return ret;
}

static struct dentry *openprom_mount(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data)
{
	return mount_single(fs_type, flags, data, openprom_fill_super);
}

static struct file_system_type openprom_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "openpromfs",
	.mount		= openprom_mount,
	.kill_sb	= kill_anon_super,
};
MODULE_ALIAS_FS("openpromfs");

static void op_inode_init_once(void *data)
{
	struct op_inode_info *oi = (struct op_inode_info *) data;

	inode_init_once(&oi->vfs_inode);
}

static int __init init_openprom_fs(void)
{
	int err;

	op_inode_cachep = kmem_cache_create("op_inode_cache",
					    sizeof(struct op_inode_info),
					    0,
					    (SLAB_RECLAIM_ACCOUNT |
					     SLAB_MEM_SPREAD),
					    op_inode_init_once);
	if (!op_inode_cachep)
		return -ENOMEM;

	err = register_filesystem(&openprom_fs_type);
	if (err)
		kmem_cache_destroy(op_inode_cachep);

	return err;
}

static void __exit exit_openprom_fs(void)
{
	unregister_filesystem(&openprom_fs_type);
	/*
	 * Make sure all delayed rcu free inodes are flushed before we
	 * destroy cache.
	 */
	rcu_barrier();
	kmem_cache_destroy(op_inode_cachep);
}

module_init(init_openprom_fs)
module_exit(exit_openprom_fs)
MODULE_LICENSE("GPL");
