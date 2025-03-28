// SPDX-License-Identifier: GPL-2.0
/*
 * /proc/sys support
 */
#include <linux/init.h>
#include <linux/sysctl.h>
#include <linux/poll.h>
#include <linux/proc_fs.h>
#include <linux/printk.h>
#include <linux/security.h>
#include <linux/sched.h>
#include <linux/cred.h>
#include <linux/namei.h>
#include <linux/mm.h>
#include <linux/uio.h>
#include <linux/module.h>
#include <linux/bpf-cgroup.h>
#include <linux/mount.h>
#include <linux/kmemleak.h>
#include <linux/lockdep.h>
#include "internal.h"

#define list_for_each_table_entry(entry, header)	\
	entry = header->ctl_table;			\
	for (size_t i = 0 ; i < header->ctl_table_size; ++i, entry++)

static const struct dentry_operations proc_sys_dentry_operations;
static const struct file_operations proc_sys_file_operations;
static const struct inode_operations proc_sys_inode_operations;
static const struct file_operations proc_sys_dir_file_operations;
static const struct inode_operations proc_sys_dir_operations;

/*
 * Support for permanently empty directories.
 * Must be non-empty to avoid sharing an address with other tables.
 */
static const struct ctl_table sysctl_mount_point[] = {
	{ }
};

/**
 * register_sysctl_mount_point() - registers a sysctl mount point
 * @path: path for the mount point
 *
 * Used to create a permanently empty directory to serve as mount point.
 * There are some subtle but important permission checks this allows in the
 * case of unprivileged mounts.
 */
struct ctl_table_header *register_sysctl_mount_point(const char *path)
{
	return register_sysctl_sz(path, sysctl_mount_point, 0);
}
EXPORT_SYMBOL(register_sysctl_mount_point);

#define sysctl_is_perm_empty_ctl_header(hptr)		\
	(hptr->type == SYSCTL_TABLE_TYPE_PERMANENTLY_EMPTY)
#define sysctl_set_perm_empty_ctl_header(hptr)		\
	(hptr->type = SYSCTL_TABLE_TYPE_PERMANENTLY_EMPTY)
#define sysctl_clear_perm_empty_ctl_header(hptr)	\
	(hptr->type = SYSCTL_TABLE_TYPE_DEFAULT)

void proc_sys_poll_notify(struct ctl_table_poll *poll)
{
	if (!poll)
		return;

	atomic_inc(&poll->event);
	wake_up_interruptible(&poll->wait);
}

static const struct ctl_table root_table[] = {
	{
		.procname = "",
		.mode = S_IFDIR|S_IRUGO|S_IXUGO,
	},
};
static struct ctl_table_root sysctl_table_root = {
	.default_set.dir.header = {
		{{.count = 1,
		  .nreg = 1,
		  .ctl_table = root_table }},
		.ctl_table_arg = root_table,
		.root = &sysctl_table_root,
		.set = &sysctl_table_root.default_set,
	},
};

static DEFINE_SPINLOCK(sysctl_lock);

static void drop_sysctl_table(struct ctl_table_header *header);
static int sysctl_follow_link(struct ctl_table_header **phead,
	const struct ctl_table **pentry);
static int insert_links(struct ctl_table_header *head);
static void put_links(struct ctl_table_header *header);

static void sysctl_print_dir(struct ctl_dir *dir)
{
	if (dir->header.parent)
		sysctl_print_dir(dir->header.parent);
	pr_cont("%s/", dir->header.ctl_table[0].procname);
}

static int namecmp(const char *name1, int len1, const char *name2, int len2)
{
	int cmp;

	cmp = memcmp(name1, name2, min(len1, len2));
	if (cmp == 0)
		cmp = len1 - len2;
	return cmp;
}

static const struct ctl_table *find_entry(struct ctl_table_header **phead,
	struct ctl_dir *dir, const char *name, int namelen)
{
	struct ctl_table_header *head;
	const struct ctl_table *entry;
	struct rb_node *node = dir->root.rb_node;

	lockdep_assert_held(&sysctl_lock);

	while (node)
	{
		struct ctl_node *ctl_node;
		const char *procname;
		int cmp;

		ctl_node = rb_entry(node, struct ctl_node, node);
		head = ctl_node->header;
		entry = &head->ctl_table[ctl_node - head->node];
		procname = entry->procname;

		cmp = namecmp(name, namelen, procname, strlen(procname));
		if (cmp < 0)
			node = node->rb_left;
		else if (cmp > 0)
			node = node->rb_right;
		else {
			*phead = head;
			return entry;
		}
	}
	return NULL;
}

static int insert_entry(struct ctl_table_header *head, const struct ctl_table *entry)
{
	struct rb_node *node = &head->node[entry - head->ctl_table].node;
	struct rb_node **p = &head->parent->root.rb_node;
	struct rb_node *parent = NULL;
	const char *name = entry->procname;
	int namelen = strlen(name);

	while (*p) {
		struct ctl_table_header *parent_head;
		const struct ctl_table *parent_entry;
		struct ctl_node *parent_node;
		const char *parent_name;
		int cmp;

		parent = *p;
		parent_node = rb_entry(parent, struct ctl_node, node);
		parent_head = parent_node->header;
		parent_entry = &parent_head->ctl_table[parent_node - parent_head->node];
		parent_name = parent_entry->procname;

		cmp = namecmp(name, namelen, parent_name, strlen(parent_name));
		if (cmp < 0)
			p = &(*p)->rb_left;
		else if (cmp > 0)
			p = &(*p)->rb_right;
		else {
			pr_err("sysctl duplicate entry: ");
			sysctl_print_dir(head->parent);
			pr_cont("%s\n", entry->procname);
			return -EEXIST;
		}
	}

	rb_link_node(node, parent, p);
	rb_insert_color(node, &head->parent->root);
	return 0;
}

static void erase_entry(struct ctl_table_header *head, const struct ctl_table *entry)
{
	struct rb_node *node = &head->node[entry - head->ctl_table].node;

	rb_erase(node, &head->parent->root);
}

static void init_header(struct ctl_table_header *head,
	struct ctl_table_root *root, struct ctl_table_set *set,
	struct ctl_node *node, const struct ctl_table *table, size_t table_size)
{
	head->ctl_table = table;
	head->ctl_table_size = table_size;
	head->ctl_table_arg = table;
	head->used = 0;
	head->count = 1;
	head->nreg = 1;
	head->unregistering = NULL;
	head->root = root;
	head->set = set;
	head->parent = NULL;
	head->node = node;
	INIT_HLIST_HEAD(&head->inodes);
	if (node) {
		const struct ctl_table *entry;

		list_for_each_table_entry(entry, head) {
			node->header = head;
			node++;
		}
	}
	if (table == sysctl_mount_point)
		sysctl_set_perm_empty_ctl_header(head);
}

static void erase_header(struct ctl_table_header *head)
{
	const struct ctl_table *entry;

	list_for_each_table_entry(entry, head)
		erase_entry(head, entry);
}

static int insert_header(struct ctl_dir *dir, struct ctl_table_header *header)
{
	const struct ctl_table *entry;
	struct ctl_table_header *dir_h = &dir->header;
	int err;


	/* Is this a permanently empty directory? */
	if (sysctl_is_perm_empty_ctl_header(dir_h))
		return -EROFS;

	/* Am I creating a permanently empty directory? */
	if (sysctl_is_perm_empty_ctl_header(header)) {
		if (!RB_EMPTY_ROOT(&dir->root))
			return -EINVAL;
		sysctl_set_perm_empty_ctl_header(dir_h);
	}

	dir_h->nreg++;
	header->parent = dir;
	err = insert_links(header);
	if (err)
		goto fail_links;
	list_for_each_table_entry(entry, header) {
		err = insert_entry(header, entry);
		if (err)
			goto fail;
	}
	return 0;
fail:
	erase_header(header);
	put_links(header);
fail_links:
	if (header->ctl_table == sysctl_mount_point)
		sysctl_clear_perm_empty_ctl_header(dir_h);
	header->parent = NULL;
	drop_sysctl_table(dir_h);
	return err;
}

static int use_table(struct ctl_table_header *p)
{
	lockdep_assert_held(&sysctl_lock);

	if (unlikely(p->unregistering))
		return 0;
	p->used++;
	return 1;
}

static void unuse_table(struct ctl_table_header *p)
{
	lockdep_assert_held(&sysctl_lock);

	if (!--p->used)
		if (unlikely(p->unregistering))
			complete(p->unregistering);
}

static void proc_sys_invalidate_dcache(struct ctl_table_header *head)
{
	proc_invalidate_siblings_dcache(&head->inodes, &sysctl_lock);
}

static void start_unregistering(struct ctl_table_header *p)
{
	/* will reacquire if has to wait */
	lockdep_assert_held(&sysctl_lock);

	/*
	 * if p->used is 0, nobody will ever touch that entry again;
	 * we'll eliminate all paths to it before dropping sysctl_lock
	 */
	if (unlikely(p->used)) {
		struct completion wait;
		init_completion(&wait);
		p->unregistering = &wait;
		spin_unlock(&sysctl_lock);
		wait_for_completion(&wait);
	} else {
		/* anything non-NULL; we'll never dereference it */
		p->unregistering = ERR_PTR(-EINVAL);
		spin_unlock(&sysctl_lock);
	}
	/*
	 * Invalidate dentries for unregistered sysctls: namespaced sysctls
	 * can have duplicate names and contaminate dcache very badly.
	 */
	proc_sys_invalidate_dcache(p);
	/*
	 * do not remove from the list until nobody holds it; walking the
	 * list in do_sysctl() relies on that.
	 */
	spin_lock(&sysctl_lock);
	erase_header(p);
}

static struct ctl_table_header *sysctl_head_grab(struct ctl_table_header *head)
{
	BUG_ON(!head);
	spin_lock(&sysctl_lock);
	if (!use_table(head))
		head = ERR_PTR(-ENOENT);
	spin_unlock(&sysctl_lock);
	return head;
}

static void sysctl_head_finish(struct ctl_table_header *head)
{
	if (!head)
		return;
	spin_lock(&sysctl_lock);
	unuse_table(head);
	spin_unlock(&sysctl_lock);
}

static struct ctl_table_set *
lookup_header_set(struct ctl_table_root *root)
{
	struct ctl_table_set *set = &root->default_set;
	if (root->lookup)
		set = root->lookup(root);
	return set;
}

static const struct ctl_table *lookup_entry(struct ctl_table_header **phead,
					    struct ctl_dir *dir,
					    const char *name, int namelen)
{
	struct ctl_table_header *head;
	const struct ctl_table *entry;

	spin_lock(&sysctl_lock);
	entry = find_entry(&head, dir, name, namelen);
	if (entry && use_table(head))
		*phead = head;
	else
		entry = NULL;
	spin_unlock(&sysctl_lock);
	return entry;
}

static struct ctl_node *first_usable_entry(struct rb_node *node)
{
	struct ctl_node *ctl_node;

	for (;node; node = rb_next(node)) {
		ctl_node = rb_entry(node, struct ctl_node, node);
		if (use_table(ctl_node->header))
			return ctl_node;
	}
	return NULL;
}

static void first_entry(struct ctl_dir *dir,
	struct ctl_table_header **phead, const struct ctl_table **pentry)
{
	struct ctl_table_header *head = NULL;
	const struct ctl_table *entry = NULL;
	struct ctl_node *ctl_node;

	spin_lock(&sysctl_lock);
	ctl_node = first_usable_entry(rb_first(&dir->root));
	spin_unlock(&sysctl_lock);
	if (ctl_node) {
		head = ctl_node->header;
		entry = &head->ctl_table[ctl_node - head->node];
	}
	*phead = head;
	*pentry = entry;
}

static void next_entry(struct ctl_table_header **phead, const struct ctl_table **pentry)
{
	struct ctl_table_header *head = *phead;
	const struct ctl_table *entry = *pentry;
	struct ctl_node *ctl_node = &head->node[entry - head->ctl_table];

	spin_lock(&sysctl_lock);
	unuse_table(head);

	ctl_node = first_usable_entry(rb_next(&ctl_node->node));
	spin_unlock(&sysctl_lock);
	head = NULL;
	if (ctl_node) {
		head = ctl_node->header;
		entry = &head->ctl_table[ctl_node - head->node];
	}
	*phead = head;
	*pentry = entry;
}

/*
 * sysctl_perm does NOT grant the superuser all rights automatically, because
 * some sysctl variables are readonly even to root.
 */

static int test_perm(int mode, int op)
{
	if (uid_eq(current_euid(), GLOBAL_ROOT_UID))
		mode >>= 6;
	else if (in_egroup_p(GLOBAL_ROOT_GID))
		mode >>= 3;
	if ((op & ~mode & (MAY_READ|MAY_WRITE|MAY_EXEC)) == 0)
		return 0;
	return -EACCES;
}

static int sysctl_perm(struct ctl_table_header *head, const struct ctl_table *table, int op)
{
	struct ctl_table_root *root = head->root;
	int mode;

	if (root->permissions)
		mode = root->permissions(head, table);
	else
		mode = table->mode;

	return test_perm(mode, op);
}

static struct inode *proc_sys_make_inode(struct super_block *sb,
		struct ctl_table_header *head, const struct ctl_table *table)
{
	struct ctl_table_root *root = head->root;
	struct inode *inode;
	struct proc_inode *ei;

	inode = new_inode(sb);
	if (!inode)
		return ERR_PTR(-ENOMEM);

	inode->i_ino = get_next_ino();

	ei = PROC_I(inode);

	spin_lock(&sysctl_lock);
	if (unlikely(head->unregistering)) {
		spin_unlock(&sysctl_lock);
		iput(inode);
		return ERR_PTR(-ENOENT);
	}
	ei->sysctl = head;
	ei->sysctl_entry = table;
	hlist_add_head_rcu(&ei->sibling_inodes, &head->inodes);
	head->count++;
	spin_unlock(&sysctl_lock);

	simple_inode_init_ts(inode);
	inode->i_mode = table->mode;
	if (!S_ISDIR(table->mode)) {
		inode->i_mode |= S_IFREG;
		inode->i_op = &proc_sys_inode_operations;
		inode->i_fop = &proc_sys_file_operations;
	} else {
		inode->i_mode |= S_IFDIR;
		inode->i_op = &proc_sys_dir_operations;
		inode->i_fop = &proc_sys_dir_file_operations;
		if (sysctl_is_perm_empty_ctl_header(head))
			make_empty_dir_inode(inode);
	}

	inode->i_uid = GLOBAL_ROOT_UID;
	inode->i_gid = GLOBAL_ROOT_GID;
	if (root->set_ownership)
		root->set_ownership(head, &inode->i_uid, &inode->i_gid);

	return inode;
}

void proc_sys_evict_inode(struct inode *inode, struct ctl_table_header *head)
{
	spin_lock(&sysctl_lock);
	hlist_del_init_rcu(&PROC_I(inode)->sibling_inodes);
	if (!--head->count)
		kfree_rcu(head, rcu);
	spin_unlock(&sysctl_lock);
}

static struct ctl_table_header *grab_header(struct inode *inode)
{
	struct ctl_table_header *head = PROC_I(inode)->sysctl;
	if (!head)
		head = &sysctl_table_root.default_set.dir.header;
	return sysctl_head_grab(head);
}

static struct dentry *proc_sys_lookup(struct inode *dir, struct dentry *dentry,
					unsigned int flags)
{
	struct ctl_table_header *head = grab_header(dir);
	struct ctl_table_header *h = NULL;
	const struct qstr *name = &dentry->d_name;
	const struct ctl_table *p;
	struct inode *inode;
	struct dentry *err = ERR_PTR(-ENOENT);
	struct ctl_dir *ctl_dir;
	int ret;

	if (IS_ERR(head))
		return ERR_CAST(head);

	ctl_dir = container_of(head, struct ctl_dir, header);

	p = lookup_entry(&h, ctl_dir, name->name, name->len);
	if (!p)
		goto out;

	if (S_ISLNK(p->mode)) {
		ret = sysctl_follow_link(&h, &p);
		err = ERR_PTR(ret);
		if (ret)
			goto out;
	}

	d_set_d_op(dentry, &proc_sys_dentry_operations);
	inode = proc_sys_make_inode(dir->i_sb, h ? h : head, p);
	err = d_splice_alias(inode, dentry);

out:
	if (h)
		sysctl_head_finish(h);
	sysctl_head_finish(head);
	return err;
}

static ssize_t proc_sys_call_handler(struct kiocb *iocb, struct iov_iter *iter,
		int write)
{
	struct inode *inode = file_inode(iocb->ki_filp);
	struct ctl_table_header *head = grab_header(inode);
	const struct ctl_table *table = PROC_I(inode)->sysctl_entry;
	size_t count = iov_iter_count(iter);
	char *kbuf;
	ssize_t error;

	if (IS_ERR(head))
		return PTR_ERR(head);

	/*
	 * At this point we know that the sysctl was not unregistered
	 * and won't be until we finish.
	 */
	error = -EPERM;
	if (sysctl_perm(head, table, write ? MAY_WRITE : MAY_READ))
		goto out;

	/* if that can happen at all, it should be -EINVAL, not -EISDIR */
	error = -EINVAL;
	if (!table->proc_handler)
		goto out;

	/* don't even try if the size is too large */
	error = -ENOMEM;
	if (count >= KMALLOC_MAX_SIZE)
		goto out;
	kbuf = kvzalloc(count + 1, GFP_KERNEL);
	if (!kbuf)
		goto out;

	if (write) {
		error = -EFAULT;
		if (!copy_from_iter_full(kbuf, count, iter))
			goto out_free_buf;
		kbuf[count] = '\0';
	}

	error = BPF_CGROUP_RUN_PROG_SYSCTL(head, table, write, &kbuf, &count,
					   &iocb->ki_pos);
	if (error)
		goto out_free_buf;

	/* careful: calling conventions are nasty here */
	error = table->proc_handler(table, write, kbuf, &count, &iocb->ki_pos);
	if (error)
		goto out_free_buf;

	if (!write) {
		error = -EFAULT;
		if (copy_to_iter(kbuf, count, iter) < count)
			goto out_free_buf;
	}

	error = count;
out_free_buf:
	kvfree(kbuf);
out:
	sysctl_head_finish(head);

	return error;
}

static ssize_t proc_sys_read(struct kiocb *iocb, struct iov_iter *iter)
{
	return proc_sys_call_handler(iocb, iter, 0);
}

static ssize_t proc_sys_write(struct kiocb *iocb, struct iov_iter *iter)
{
	return proc_sys_call_handler(iocb, iter, 1);
}

static int proc_sys_open(struct inode *inode, struct file *filp)
{
	struct ctl_table_header *head = grab_header(inode);
	const struct ctl_table *table = PROC_I(inode)->sysctl_entry;

	/* sysctl was unregistered */
	if (IS_ERR(head))
		return PTR_ERR(head);

	if (table->poll)
		filp->private_data = proc_sys_poll_event(table->poll);

	sysctl_head_finish(head);

	return 0;
}

static __poll_t proc_sys_poll(struct file *filp, poll_table *wait)
{
	struct inode *inode = file_inode(filp);
	struct ctl_table_header *head = grab_header(inode);
	const struct ctl_table *table = PROC_I(inode)->sysctl_entry;
	__poll_t ret = DEFAULT_POLLMASK;
	unsigned long event;

	/* sysctl was unregistered */
	if (IS_ERR(head))
		return EPOLLERR | EPOLLHUP;

	if (!table->proc_handler)
		goto out;

	if (!table->poll)
		goto out;

	event = (unsigned long)filp->private_data;
	poll_wait(filp, &table->poll->wait, wait);

	if (event != atomic_read(&table->poll->event)) {
		filp->private_data = proc_sys_poll_event(table->poll);
		ret = EPOLLIN | EPOLLRDNORM | EPOLLERR | EPOLLPRI;
	}

out:
	sysctl_head_finish(head);

	return ret;
}

static bool proc_sys_fill_cache(struct file *file,
				struct dir_context *ctx,
				struct ctl_table_header *head,
				const struct ctl_table *table)
{
	struct dentry *child, *dir = file->f_path.dentry;
	struct inode *inode;
	struct qstr qname;
	ino_t ino = 0;
	unsigned type = DT_UNKNOWN;

	qname.name = table->procname;
	qname.len  = strlen(table->procname);
	qname.hash = full_name_hash(dir, qname.name, qname.len);

	child = d_lookup(dir, &qname);
	if (!child) {
		DECLARE_WAIT_QUEUE_HEAD_ONSTACK(wq);
		child = d_alloc_parallel(dir, &qname, &wq);
		if (IS_ERR(child))
			return false;
		if (d_in_lookup(child)) {
			struct dentry *res;
			d_set_d_op(child, &proc_sys_dentry_operations);
			inode = proc_sys_make_inode(dir->d_sb, head, table);
			res = d_splice_alias(inode, child);
			d_lookup_done(child);
			if (unlikely(res)) {
				dput(child);

				if (IS_ERR(res))
					return false;

				child = res;
			}
		}
	}
	inode = d_inode(child);
	ino  = inode->i_ino;
	type = inode->i_mode >> 12;
	dput(child);
	return dir_emit(ctx, qname.name, qname.len, ino, type);
}

static bool proc_sys_link_fill_cache(struct file *file,
				    struct dir_context *ctx,
				    struct ctl_table_header *head,
				    const struct ctl_table *table)
{
	bool ret = true;

	head = sysctl_head_grab(head);
	if (IS_ERR(head))
		return false;

	/* It is not an error if we can not follow the link ignore it */
	if (sysctl_follow_link(&head, &table))
		goto out;

	ret = proc_sys_fill_cache(file, ctx, head, table);
out:
	sysctl_head_finish(head);
	return ret;
}

static int scan(struct ctl_table_header *head, const struct ctl_table *table,
		unsigned long *pos, struct file *file,
		struct dir_context *ctx)
{
	bool res;

	if ((*pos)++ < ctx->pos)
		return true;

	if (unlikely(S_ISLNK(table->mode)))
		res = proc_sys_link_fill_cache(file, ctx, head, table);
	else
		res = proc_sys_fill_cache(file, ctx, head, table);

	if (res)
		ctx->pos = *pos;

	return res;
}

static int proc_sys_readdir(struct file *file, struct dir_context *ctx)
{
	struct ctl_table_header *head = grab_header(file_inode(file));
	struct ctl_table_header *h = NULL;
	const struct ctl_table *entry;
	struct ctl_dir *ctl_dir;
	unsigned long pos;

	if (IS_ERR(head))
		return PTR_ERR(head);

	ctl_dir = container_of(head, struct ctl_dir, header);

	if (!dir_emit_dots(file, ctx))
		goto out;

	pos = 2;

	for (first_entry(ctl_dir, &h, &entry); h; next_entry(&h, &entry)) {
		if (!scan(h, entry, &pos, file, ctx)) {
			sysctl_head_finish(h);
			break;
		}
	}
out:
	sysctl_head_finish(head);
	return 0;
}

static int proc_sys_permission(struct mnt_idmap *idmap,
			       struct inode *inode, int mask)
{
	/*
	 * sysctl entries that are not writeable,
	 * are _NOT_ writeable, capabilities or not.
	 */
	struct ctl_table_header *head;
	const struct ctl_table *table;
	int error;

	/* Executable files are not allowed under /proc/sys/ */
	if ((mask & MAY_EXEC) && S_ISREG(inode->i_mode))
		return -EACCES;

	head = grab_header(inode);
	if (IS_ERR(head))
		return PTR_ERR(head);

	table = PROC_I(inode)->sysctl_entry;
	if (!table) /* global root - r-xr-xr-x */
		error = mask & MAY_WRITE ? -EACCES : 0;
	else /* Use the permissions on the sysctl table entry */
		error = sysctl_perm(head, table, mask & ~MAY_NOT_BLOCK);

	sysctl_head_finish(head);
	return error;
}

static int proc_sys_setattr(struct mnt_idmap *idmap,
			    struct dentry *dentry, struct iattr *attr)
{
	struct inode *inode = d_inode(dentry);
	int error;

	if (attr->ia_valid & (ATTR_MODE | ATTR_UID | ATTR_GID))
		return -EPERM;

	error = setattr_prepare(&nop_mnt_idmap, dentry, attr);
	if (error)
		return error;

	setattr_copy(&nop_mnt_idmap, inode, attr);
	return 0;
}

static int proc_sys_getattr(struct mnt_idmap *idmap,
			    const struct path *path, struct kstat *stat,
			    u32 request_mask, unsigned int query_flags)
{
	struct inode *inode = d_inode(path->dentry);
	struct ctl_table_header *head = grab_header(inode);
	const struct ctl_table *table = PROC_I(inode)->sysctl_entry;

	if (IS_ERR(head))
		return PTR_ERR(head);

	generic_fillattr(&nop_mnt_idmap, request_mask, inode, stat);
	if (table)
		stat->mode = (stat->mode & S_IFMT) | table->mode;

	sysctl_head_finish(head);
	return 0;
}

static const struct file_operations proc_sys_file_operations = {
	.open		= proc_sys_open,
	.poll		= proc_sys_poll,
	.read_iter	= proc_sys_read,
	.write_iter	= proc_sys_write,
	.splice_read	= copy_splice_read,
	.splice_write	= iter_file_splice_write,
	.llseek		= default_llseek,
};

static const struct file_operations proc_sys_dir_file_operations = {
	.read		= generic_read_dir,
	.iterate_shared	= proc_sys_readdir,
	.llseek		= generic_file_llseek,
};

static const struct inode_operations proc_sys_inode_operations = {
	.permission	= proc_sys_permission,
	.setattr	= proc_sys_setattr,
	.getattr	= proc_sys_getattr,
};

static const struct inode_operations proc_sys_dir_operations = {
	.lookup		= proc_sys_lookup,
	.permission	= proc_sys_permission,
	.setattr	= proc_sys_setattr,
	.getattr	= proc_sys_getattr,
};

static int proc_sys_revalidate(struct inode *dir, const struct qstr *name,
			       struct dentry *dentry, unsigned int flags)
{
	if (flags & LOOKUP_RCU)
		return -ECHILD;
	return !PROC_I(d_inode(dentry))->sysctl->unregistering;
}

static int proc_sys_delete(const struct dentry *dentry)
{
	return !!PROC_I(d_inode(dentry))->sysctl->unregistering;
}

static int sysctl_is_seen(struct ctl_table_header *p)
{
	struct ctl_table_set *set = p->set;
	int res;
	spin_lock(&sysctl_lock);
	if (p->unregistering)
		res = 0;
	else if (!set->is_seen)
		res = 1;
	else
		res = set->is_seen(set);
	spin_unlock(&sysctl_lock);
	return res;
}

static int proc_sys_compare(const struct dentry *dentry,
		unsigned int len, const char *str, const struct qstr *name)
{
	struct ctl_table_header *head;
	struct inode *inode;

	/* Although proc doesn't have negative dentries, rcu-walk means
	 * that inode here can be NULL */
	/* AV: can it, indeed? */
	inode = d_inode_rcu(dentry);
	if (!inode)
		return 1;
	if (name->len != len)
		return 1;
	if (memcmp(name->name, str, len))
		return 1;
	head = rcu_dereference(PROC_I(inode)->sysctl);
	return !head || !sysctl_is_seen(head);
}

static const struct dentry_operations proc_sys_dentry_operations = {
	.d_revalidate	= proc_sys_revalidate,
	.d_delete	= proc_sys_delete,
	.d_compare	= proc_sys_compare,
};

static struct ctl_dir *find_subdir(struct ctl_dir *dir,
				   const char *name, int namelen)
{
	struct ctl_table_header *head;
	const struct ctl_table *entry;

	entry = find_entry(&head, dir, name, namelen);
	if (!entry)
		return ERR_PTR(-ENOENT);
	if (!S_ISDIR(entry->mode))
		return ERR_PTR(-ENOTDIR);
	return container_of(head, struct ctl_dir, header);
}

static struct ctl_dir *new_dir(struct ctl_table_set *set,
			       const char *name, int namelen)
{
	struct ctl_table *table;
	struct ctl_dir *new;
	struct ctl_node *node;
	char *new_name;

	new = kzalloc(sizeof(*new) + sizeof(struct ctl_node) +
		      sizeof(struct ctl_table) +  namelen + 1,
		      GFP_KERNEL);
	if (!new)
		return NULL;

	node = (struct ctl_node *)(new + 1);
	table = (struct ctl_table *)(node + 1);
	new_name = (char *)(table + 1);
	memcpy(new_name, name, namelen);
	table[0].procname = new_name;
	table[0].mode = S_IFDIR|S_IRUGO|S_IXUGO;
	init_header(&new->header, set->dir.header.root, set, node, table, 1);

	return new;
}

/**
 * get_subdir - find or create a subdir with the specified name.
 * @dir:  Directory to create the subdirectory in
 * @name: The name of the subdirectory to find or create
 * @namelen: The length of name
 *
 * Takes a directory with an elevated reference count so we know that
 * if we drop the lock the directory will not go away.  Upon success
 * the reference is moved from @dir to the returned subdirectory.
 * Upon error an error code is returned and the reference on @dir is
 * simply dropped.
 */
static struct ctl_dir *get_subdir(struct ctl_dir *dir,
				  const char *name, int namelen)
{
	struct ctl_table_set *set = dir->header.set;
	struct ctl_dir *subdir, *new = NULL;
	int err;

	spin_lock(&sysctl_lock);
	subdir = find_subdir(dir, name, namelen);
	if (!IS_ERR(subdir))
		goto found;
	if (PTR_ERR(subdir) != -ENOENT)
		goto failed;

	spin_unlock(&sysctl_lock);
	new = new_dir(set, name, namelen);
	spin_lock(&sysctl_lock);
	subdir = ERR_PTR(-ENOMEM);
	if (!new)
		goto failed;

	/* Was the subdir added while we dropped the lock? */
	subdir = find_subdir(dir, name, namelen);
	if (!IS_ERR(subdir))
		goto found;
	if (PTR_ERR(subdir) != -ENOENT)
		goto failed;

	/* Nope.  Use the our freshly made directory entry. */
	err = insert_header(dir, &new->header);
	subdir = ERR_PTR(err);
	if (err)
		goto failed;
	subdir = new;
found:
	subdir->header.nreg++;
failed:
	if (IS_ERR(subdir)) {
		pr_err("sysctl could not get directory: ");
		sysctl_print_dir(dir);
		pr_cont("%*.*s %ld\n", namelen, namelen, name,
			PTR_ERR(subdir));
	}
	drop_sysctl_table(&dir->header);
	if (new)
		drop_sysctl_table(&new->header);
	spin_unlock(&sysctl_lock);
	return subdir;
}

static struct ctl_dir *xlate_dir(struct ctl_table_set *set, struct ctl_dir *dir)
{
	struct ctl_dir *parent;
	const char *procname;
	if (!dir->header.parent)
		return &set->dir;
	parent = xlate_dir(set, dir->header.parent);
	if (IS_ERR(parent))
		return parent;
	procname = dir->header.ctl_table[0].procname;
	return find_subdir(parent, procname, strlen(procname));
}

static int sysctl_follow_link(struct ctl_table_header **phead,
	const struct ctl_table **pentry)
{
	struct ctl_table_header *head;
	const struct ctl_table *entry;
	struct ctl_table_root *root;
	struct ctl_table_set *set;
	struct ctl_dir *dir;
	int ret;

	spin_lock(&sysctl_lock);
	root = (*pentry)->data;
	set = lookup_header_set(root);
	dir = xlate_dir(set, (*phead)->parent);
	if (IS_ERR(dir))
		ret = PTR_ERR(dir);
	else {
		const char *procname = (*pentry)->procname;
		head = NULL;
		entry = find_entry(&head, dir, procname, strlen(procname));
		ret = -ENOENT;
		if (entry && use_table(head)) {
			unuse_table(*phead);
			*phead = head;
			*pentry = entry;
			ret = 0;
		}
	}

	spin_unlock(&sysctl_lock);
	return ret;
}

static int sysctl_err(const char *path, const struct ctl_table *table, char *fmt, ...)
{
	struct va_format vaf;
	va_list args;

	va_start(args, fmt);
	vaf.fmt = fmt;
	vaf.va = &args;

	pr_err("sysctl table check failed: %s/%s %pV\n",
	       path, table->procname, &vaf);

	va_end(args);
	return -EINVAL;
}

static int sysctl_check_table_array(const char *path, const struct ctl_table *table)
{
	unsigned int extra;
	int err = 0;

	if ((table->proc_handler == proc_douintvec) ||
	    (table->proc_handler == proc_douintvec_minmax)) {
		if (table->maxlen != sizeof(unsigned int))
			err |= sysctl_err(path, table, "array not allowed");
	}

	if (table->proc_handler == proc_dou8vec_minmax) {
		if (table->maxlen != sizeof(u8))
			err |= sysctl_err(path, table, "array not allowed");

		if (table->extra1) {
			extra = *(unsigned int *) table->extra1;
			if (extra > 255U)
				err |= sysctl_err(path, table,
						"range value too large for proc_dou8vec_minmax");
		}
		if (table->extra2) {
			extra = *(unsigned int *) table->extra2;
			if (extra > 255U)
				err |= sysctl_err(path, table,
						"range value too large for proc_dou8vec_minmax");
		}
	}

	if (table->proc_handler == proc_dobool) {
		if (table->maxlen != sizeof(bool))
			err |= sysctl_err(path, table, "array not allowed");
	}

	return err;
}

static int sysctl_check_table(const char *path, struct ctl_table_header *header)
{
	const struct ctl_table *entry;
	int err = 0;
	list_for_each_table_entry(entry, header) {
		if (!entry->procname)
			err |= sysctl_err(path, entry, "procname is null");
		if ((entry->proc_handler == proc_dostring) ||
		    (entry->proc_handler == proc_dobool) ||
		    (entry->proc_handler == proc_dointvec) ||
		    (entry->proc_handler == proc_douintvec) ||
		    (entry->proc_handler == proc_douintvec_minmax) ||
		    (entry->proc_handler == proc_dointvec_minmax) ||
		    (entry->proc_handler == proc_dou8vec_minmax) ||
		    (entry->proc_handler == proc_dointvec_jiffies) ||
		    (entry->proc_handler == proc_dointvec_userhz_jiffies) ||
		    (entry->proc_handler == proc_dointvec_ms_jiffies) ||
		    (entry->proc_handler == proc_doulongvec_minmax) ||
		    (entry->proc_handler == proc_doulongvec_ms_jiffies_minmax)) {
			if (!entry->data)
				err |= sysctl_err(path, entry, "No data");
			if (!entry->maxlen)
				err |= sysctl_err(path, entry, "No maxlen");
			else
				err |= sysctl_check_table_array(path, entry);
		}
		if (!entry->proc_handler)
			err |= sysctl_err(path, entry, "No proc_handler");

		if ((entry->mode & (S_IRUGO|S_IWUGO)) != entry->mode)
			err |= sysctl_err(path, entry, "bogus .mode 0%o",
				entry->mode);
	}
	return err;
}

static struct ctl_table_header *new_links(struct ctl_dir *dir, struct ctl_table_header *head)
{
	struct ctl_table *link_table, *link;
	struct ctl_table_header *links;
	const struct ctl_table *entry;
	struct ctl_node *node;
	char *link_name;
	int name_bytes;

	name_bytes = 0;
	list_for_each_table_entry(entry, head) {
		name_bytes += strlen(entry->procname) + 1;
	}

	links = kzalloc(sizeof(struct ctl_table_header) +
			sizeof(struct ctl_node)*head->ctl_table_size +
			sizeof(struct ctl_table)*head->ctl_table_size +
			name_bytes,
			GFP_KERNEL);

	if (!links)
		return NULL;

	node = (struct ctl_node *)(links + 1);
	link_table = (struct ctl_table *)(node + head->ctl_table_size);
	link_name = (char *)(link_table + head->ctl_table_size);
	link = link_table;

	list_for_each_table_entry(entry, head) {
		int len = strlen(entry->procname) + 1;
		memcpy(link_name, entry->procname, len);
		link->procname = link_name;
		link->mode = S_IFLNK|S_IRWXUGO;
		link->data = head->root;
		link_name += len;
		link++;
	}
	init_header(links, dir->header.root, dir->header.set, node, link_table,
		    head->ctl_table_size);
	links->nreg = head->ctl_table_size;

	return links;
}

static bool get_links(struct ctl_dir *dir,
		      struct ctl_table_header *header,
		      struct ctl_table_root *link_root)
{
	struct ctl_table_header *tmp_head;
	const struct ctl_table *entry, *link;

	if (header->ctl_table_size == 0 ||
	    sysctl_is_perm_empty_ctl_header(header))
		return true;

	/* Are there links available for every entry in table? */
	list_for_each_table_entry(entry, header) {
		const char *procname = entry->procname;
		link = find_entry(&tmp_head, dir, procname, strlen(procname));
		if (!link)
			return false;
		if (S_ISDIR(link->mode) && S_ISDIR(entry->mode))
			continue;
		if (S_ISLNK(link->mode) && (link->data == link_root))
			continue;
		return false;
	}

	/* The checks passed.  Increase the registration count on the links */
	list_for_each_table_entry(entry, header) {
		const char *procname = entry->procname;
		link = find_entry(&tmp_head, dir, procname, strlen(procname));
		tmp_head->nreg++;
	}
	return true;
}

static int insert_links(struct ctl_table_header *head)
{
	struct ctl_table_set *root_set = &sysctl_table_root.default_set;
	struct ctl_dir *core_parent;
	struct ctl_table_header *links;
	int err;

	if (head->set == root_set)
		return 0;

	core_parent = xlate_dir(root_set, head->parent);
	if (IS_ERR(core_parent))
		return 0;

	if (get_links(core_parent, head, head->root))
		return 0;

	core_parent->header.nreg++;
	spin_unlock(&sysctl_lock);

	links = new_links(core_parent, head);

	spin_lock(&sysctl_lock);
	err = -ENOMEM;
	if (!links)
		goto out;

	err = 0;
	if (get_links(core_parent, head, head->root)) {
		kfree(links);
		goto out;
	}

	err = insert_header(core_parent, links);
	if (err)
		kfree(links);
out:
	drop_sysctl_table(&core_parent->header);
	return err;
}

/* Find the directory for the ctl_table. If one is not found create it. */
static struct ctl_dir *sysctl_mkdir_p(struct ctl_dir *dir, const char *path)
{
	const char *name, *nextname;

	for (name = path; name; name = nextname) {
		int namelen;
		nextname = strchr(name, '/');
		if (nextname) {
			namelen = nextname - name;
			nextname++;
		} else {
			namelen = strlen(name);
		}
		if (namelen == 0)
			continue;

		/*
		 * namelen ensures if name is "foo/bar/yay" only foo is
		 * registered first. We traverse as if using mkdir -p and
		 * return a ctl_dir for the last directory entry.
		 */
		dir = get_subdir(dir, name, namelen);
		if (IS_ERR(dir))
			break;
	}
	return dir;
}

/**
 * __register_sysctl_table - register a leaf sysctl table
 * @set: Sysctl tree to register on
 * @path: The path to the directory the sysctl table is in.
 *
 * @table: the top-level table structure. This table should not be free'd
 *         after registration. So it should not be used on stack. It can either
 *         be a global or dynamically allocated by the caller and free'd later
 *         after sysctl unregistration.
 * @table_size : The number of elements in table
 *
 * Register a sysctl table hierarchy. @table should be a filled in ctl_table
 * array.
 *
 * The members of the &struct ctl_table structure are used as follows:
 * procname - the name of the sysctl file under /proc/sys. Set to %NULL to not
 *            enter a sysctl file
 * data     - a pointer to data for use by proc_handler
 * maxlen   - the maximum size in bytes of the data
 * mode     - the file permissions for the /proc/sys file
 * type     - Defines the target type (described in struct definition)
 * proc_handler - the text handler routine (described below)
 *
 * extra1, extra2 - extra pointers usable by the proc handler routines
 * XXX: we should eventually modify these to use long min / max [0]
 * [0] https://lkml.kernel.org/87zgpte9o4.fsf@email.froward.int.ebiederm.org
 *
 * Leaf nodes in the sysctl tree will be represented by a single file
 * under /proc; non-leaf nodes are not allowed.
 *
 * There must be a proc_handler routine for any terminal nodes.
 * Several default handlers are available to cover common cases -
 *
 * proc_dostring(), proc_dointvec(), proc_dointvec_jiffies(),
 * proc_dointvec_userhz_jiffies(), proc_dointvec_minmax(),
 * proc_doulongvec_ms_jiffies_minmax(), proc_doulongvec_minmax()
 *
 * It is the handler's job to read the input buffer from user memory
 * and process it. The handler should return 0 on success.
 *
 * This routine returns %NULL on a failure to register, and a pointer
 * to the table header on success.
 */
struct ctl_table_header *__register_sysctl_table(
	struct ctl_table_set *set,
	const char *path, const struct ctl_table *table, size_t table_size)
{
	struct ctl_table_root *root = set->dir.header.root;
	struct ctl_table_header *header;
	struct ctl_dir *dir;
	struct ctl_node *node;

	header = kzalloc(sizeof(struct ctl_table_header) +
			 sizeof(struct ctl_node)*table_size, GFP_KERNEL_ACCOUNT);
	if (!header)
		return NULL;

	node = (struct ctl_node *)(header + 1);
	init_header(header, root, set, node, table, table_size);
	if (sysctl_check_table(path, header))
		goto fail;

	spin_lock(&sysctl_lock);
	dir = &set->dir;
	/* Reference moved down the directory tree get_subdir */
	dir->header.nreg++;
	spin_unlock(&sysctl_lock);

	dir = sysctl_mkdir_p(dir, path);
	if (IS_ERR(dir))
		goto fail;
	spin_lock(&sysctl_lock);
	if (insert_header(dir, header))
		goto fail_put_dir_locked;

	drop_sysctl_table(&dir->header);
	spin_unlock(&sysctl_lock);

	return header;

fail_put_dir_locked:
	drop_sysctl_table(&dir->header);
	spin_unlock(&sysctl_lock);
fail:
	kfree(header);
	return NULL;
}

/**
 * register_sysctl_sz - register a sysctl table
 * @path: The path to the directory the sysctl table is in. If the path
 * 	doesn't exist we will create it for you.
 * @table: the table structure. The calller must ensure the life of the @table
 * 	will be kept during the lifetime use of the syctl. It must not be freed
 * 	until unregister_sysctl_table() is called with the given returned table
 * 	with this registration. If your code is non modular then you don't need
 * 	to call unregister_sysctl_table() and can instead use something like
 * 	register_sysctl_init() which does not care for the result of the syctl
 * 	registration.
 * @table_size: The number of elements in table.
 *
 * Register a sysctl table. @table should be a filled in ctl_table
 * array. A completely 0 filled entry terminates the table.
 *
 * See __register_sysctl_table for more details.
 */
struct ctl_table_header *register_sysctl_sz(const char *path, const struct ctl_table *table,
					    size_t table_size)
{
	return __register_sysctl_table(&sysctl_table_root.default_set,
					path, table, table_size);
}
EXPORT_SYMBOL(register_sysctl_sz);

/**
 * __register_sysctl_init() - register sysctl table to path
 * @path: path name for sysctl base. If that path doesn't exist we will create
 * 	it for you.
 * @table: This is the sysctl table that needs to be registered to the path.
 * 	The caller must ensure the life of the @table will be kept during the
 * 	lifetime use of the sysctl.
 * @table_name: The name of sysctl table, only used for log printing when
 *              registration fails
 * @table_size: The number of elements in table
 *
 * The sysctl interface is used by userspace to query or modify at runtime
 * a predefined value set on a variable. These variables however have default
 * values pre-set. Code which depends on these variables will always work even
 * if register_sysctl() fails. If register_sysctl() fails you'd just loose the
 * ability to query or modify the sysctls dynamically at run time. Chances of
 * register_sysctl() failing on init are extremely low, and so for both reasons
 * this function does not return any error as it is used by initialization code.
 *
 * Context: if your base directory does not exist it will be created for you.
 */
void __init __register_sysctl_init(const char *path, const struct ctl_table *table,
				 const char *table_name, size_t table_size)
{
	struct ctl_table_header *hdr = register_sysctl_sz(path, table, table_size);

	if (unlikely(!hdr)) {
		pr_err("failed when register_sysctl_sz %s to %s\n", table_name, path);
		return;
	}
	kmemleak_not_leak(hdr);
}

static void put_links(struct ctl_table_header *header)
{
	struct ctl_table_set *root_set = &sysctl_table_root.default_set;
	struct ctl_table_root *root = header->root;
	struct ctl_dir *parent = header->parent;
	struct ctl_dir *core_parent;
	const struct ctl_table *entry;

	if (header->set == root_set)
		return;

	core_parent = xlate_dir(root_set, parent);
	if (IS_ERR(core_parent))
		return;

	list_for_each_table_entry(entry, header) {
		struct ctl_table_header *link_head;
		const struct ctl_table *link;
		const char *name = entry->procname;

		link = find_entry(&link_head, core_parent, name, strlen(name));
		if (link &&
		    ((S_ISDIR(link->mode) && S_ISDIR(entry->mode)) ||
		     (S_ISLNK(link->mode) && (link->data == root)))) {
			drop_sysctl_table(link_head);
		}
		else {
			pr_err("sysctl link missing during unregister: ");
			sysctl_print_dir(parent);
			pr_cont("%s\n", name);
		}
	}
}

static void drop_sysctl_table(struct ctl_table_header *header)
{
	struct ctl_dir *parent = header->parent;

	if (--header->nreg)
		return;

	if (parent) {
		put_links(header);
		start_unregistering(header);
	}

	if (!--header->count)
		kfree_rcu(header, rcu);

	if (parent)
		drop_sysctl_table(&parent->header);
}

/**
 * unregister_sysctl_table - unregister a sysctl table hierarchy
 * @header: the header returned from register_sysctl or __register_sysctl_table
 *
 * Unregisters the sysctl table and all children. proc entries may not
 * actually be removed until they are no longer used by anyone.
 */
void unregister_sysctl_table(struct ctl_table_header * header)
{
	might_sleep();

	if (header == NULL)
		return;

	spin_lock(&sysctl_lock);
	drop_sysctl_table(header);
	spin_unlock(&sysctl_lock);
}
EXPORT_SYMBOL(unregister_sysctl_table);

void setup_sysctl_set(struct ctl_table_set *set,
	struct ctl_table_root *root,
	int (*is_seen)(struct ctl_table_set *))
{
	memset(set, 0, sizeof(*set));
	set->is_seen = is_seen;
	init_header(&set->dir.header, root, set, NULL, root_table, 1);
}

void retire_sysctl_set(struct ctl_table_set *set)
{
	WARN_ON(!RB_EMPTY_ROOT(&set->dir.root));
}

int __init proc_sys_init(void)
{
	struct proc_dir_entry *proc_sys_root;

	proc_sys_root = proc_mkdir("sys", NULL);
	proc_sys_root->proc_iops = &proc_sys_dir_operations;
	proc_sys_root->proc_dir_ops = &proc_sys_dir_file_operations;
	proc_sys_root->nlink = 0;

	return sysctl_init_bases();
}

struct sysctl_alias {
	const char *kernel_param;
	const char *sysctl_param;
};

/*
 * Historically some settings had both sysctl and a command line parameter.
 * With the generic sysctl. parameter support, we can handle them at a single
 * place and only keep the historical name for compatibility. This is not meant
 * to add brand new aliases. When adding existing aliases, consider whether
 * the possibly different moment of changing the value (e.g. from early_param
 * to the moment do_sysctl_args() is called) is an issue for the specific
 * parameter.
 */
static const struct sysctl_alias sysctl_aliases[] = {
	{"hardlockup_all_cpu_backtrace",	"kernel.hardlockup_all_cpu_backtrace" },
	{"hung_task_panic",			"kernel.hung_task_panic" },
	{"numa_zonelist_order",			"vm.numa_zonelist_order" },
	{"softlockup_all_cpu_backtrace",	"kernel.softlockup_all_cpu_backtrace" },
	{ }
};

static const char *sysctl_find_alias(char *param)
{
	const struct sysctl_alias *alias;

	for (alias = &sysctl_aliases[0]; alias->kernel_param != NULL; alias++) {
		if (strcmp(alias->kernel_param, param) == 0)
			return alias->sysctl_param;
	}

	return NULL;
}

bool sysctl_is_alias(char *param)
{
	const char *alias = sysctl_find_alias(param);

	return alias != NULL;
}

/* Set sysctl value passed on kernel command line. */
static int process_sysctl_arg(char *param, char *val,
			       const char *unused, void *arg)
{
	char *path;
	struct vfsmount **proc_mnt = arg;
	struct file_system_type *proc_fs_type;
	struct file *file;
	int len;
	int err;
	loff_t pos = 0;
	ssize_t wret;

	if (strncmp(param, "sysctl", sizeof("sysctl") - 1) == 0) {
		param += sizeof("sysctl") - 1;

		if (param[0] != '/' && param[0] != '.')
			return 0;

		param++;
	} else {
		param = (char *) sysctl_find_alias(param);
		if (!param)
			return 0;
	}

	if (!val)
		return -EINVAL;
	len = strlen(val);
	if (len == 0)
		return -EINVAL;

	/*
	 * To set sysctl options, we use a temporary mount of proc, look up the
	 * respective sys/ file and write to it. To avoid mounting it when no
	 * options were given, we mount it only when the first sysctl option is
	 * found. Why not a persistent mount? There are problems with a
	 * persistent mount of proc in that it forces userspace not to use any
	 * proc mount options.
	 */
	if (!*proc_mnt) {
		proc_fs_type = get_fs_type("proc");
		if (!proc_fs_type) {
			pr_err("Failed to find procfs to set sysctl from command line\n");
			return 0;
		}
		*proc_mnt = kern_mount(proc_fs_type);
		put_filesystem(proc_fs_type);
		if (IS_ERR(*proc_mnt)) {
			pr_err("Failed to mount procfs to set sysctl from command line\n");
			return 0;
		}
	}

	path = kasprintf(GFP_KERNEL, "sys/%s", param);
	if (!path)
		panic("%s: Failed to allocate path for %s\n", __func__, param);
	strreplace(path, '.', '/');

	file = file_open_root_mnt(*proc_mnt, path, O_WRONLY, 0);
	if (IS_ERR(file)) {
		err = PTR_ERR(file);
		if (err == -ENOENT)
			pr_err("Failed to set sysctl parameter '%s=%s': parameter not found\n",
				param, val);
		else if (err == -EACCES)
			pr_err("Failed to set sysctl parameter '%s=%s': permission denied (read-only?)\n",
				param, val);
		else
			pr_err("Error %pe opening proc file to set sysctl parameter '%s=%s'\n",
				file, param, val);
		goto out;
	}
	wret = kernel_write(file, val, len, &pos);
	if (wret < 0) {
		err = wret;
		if (err == -EINVAL)
			pr_err("Failed to set sysctl parameter '%s=%s': invalid value\n",
				param, val);
		else
			pr_err("Error %pe writing to proc file to set sysctl parameter '%s=%s'\n",
				ERR_PTR(err), param, val);
	} else if (wret != len) {
		pr_err("Wrote only %zd bytes of %d writing to proc file %s to set sysctl parameter '%s=%s\n",
			wret, len, path, param, val);
	}

	err = filp_close(file, NULL);
	if (err)
		pr_err("Error %pe closing proc file to set sysctl parameter '%s=%s\n",
			ERR_PTR(err), param, val);
out:
	kfree(path);
	return 0;
}

void do_sysctl_args(void)
{
	char *command_line;
	struct vfsmount *proc_mnt = NULL;

	command_line = kstrdup(saved_command_line, GFP_KERNEL);
	if (!command_line)
		panic("%s: Failed to allocate copy of command line\n", __func__);

	parse_args("Setting sysctl args", command_line,
		   NULL, 0, -1, -1, &proc_mnt, process_sysctl_arg);

	if (proc_mnt)
		kern_unmount(proc_mnt);

	kfree(command_line);
}
