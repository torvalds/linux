/*
 * /proc/sys support
 */
#include <linux/init.h>
#include <linux/sysctl.h>
#include <linux/poll.h>
#include <linux/proc_fs.h>
#include <linux/security.h>
#include <linux/namei.h>
#include <linux/module.h>
#include "internal.h"

static const struct dentry_operations proc_sys_dentry_operations;
static const struct file_operations proc_sys_file_operations;
static const struct inode_operations proc_sys_inode_operations;
static const struct file_operations proc_sys_dir_file_operations;
static const struct inode_operations proc_sys_dir_operations;

void proc_sys_poll_notify(struct ctl_table_poll *poll)
{
	if (!poll)
		return;

	atomic_inc(&poll->event);
	wake_up_interruptible(&poll->wait);
}

static struct ctl_table root_table[1];
static struct ctl_table_root sysctl_table_root;
static struct ctl_table_header root_table_header = {
	{{.count = 1,
	.ctl_table = root_table,
	.ctl_entry = LIST_HEAD_INIT(sysctl_table_root.default_set.list),}},
	.root = &sysctl_table_root,
	.set = &sysctl_table_root.default_set,
};
static struct ctl_table_root sysctl_table_root = {
	.root_list = LIST_HEAD_INIT(sysctl_table_root.root_list),
	.default_set.list = LIST_HEAD_INIT(root_table_header.ctl_entry),
};

static DEFINE_SPINLOCK(sysctl_lock);

/* called under sysctl_lock */
static int use_table(struct ctl_table_header *p)
{
	if (unlikely(p->unregistering))
		return 0;
	p->used++;
	return 1;
}

/* called under sysctl_lock */
static void unuse_table(struct ctl_table_header *p)
{
	if (!--p->used)
		if (unlikely(p->unregistering))
			complete(p->unregistering);
}

/* called under sysctl_lock, will reacquire if has to wait */
static void start_unregistering(struct ctl_table_header *p)
{
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
		spin_lock(&sysctl_lock);
	} else {
		/* anything non-NULL; we'll never dereference it */
		p->unregistering = ERR_PTR(-EINVAL);
	}
	/*
	 * do not remove from the list until nobody holds it; walking the
	 * list in do_sysctl() relies on that.
	 */
	list_del_init(&p->ctl_entry);
}

static void sysctl_head_get(struct ctl_table_header *head)
{
	spin_lock(&sysctl_lock);
	head->count++;
	spin_unlock(&sysctl_lock);
}

void sysctl_head_put(struct ctl_table_header *head)
{
	spin_lock(&sysctl_lock);
	if (!--head->count)
		kfree_rcu(head, rcu);
	spin_unlock(&sysctl_lock);
}

static struct ctl_table_header *sysctl_head_grab(struct ctl_table_header *head)
{
	if (!head)
		BUG();
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
lookup_header_set(struct ctl_table_root *root, struct nsproxy *namespaces)
{
	struct ctl_table_set *set = &root->default_set;
	if (root->lookup)
		set = root->lookup(root, namespaces);
	return set;
}

static struct list_head *
lookup_header_list(struct ctl_table_root *root, struct nsproxy *namespaces)
{
	struct ctl_table_set *set = lookup_header_set(root, namespaces);
	return &set->list;
}

static struct ctl_table_header *__sysctl_head_next(struct nsproxy *namespaces,
						struct ctl_table_header *prev)
{
	struct ctl_table_root *root;
	struct list_head *header_list;
	struct ctl_table_header *head;
	struct list_head *tmp;

	spin_lock(&sysctl_lock);
	if (prev) {
		head = prev;
		tmp = &prev->ctl_entry;
		unuse_table(prev);
		goto next;
	}
	tmp = &root_table_header.ctl_entry;
	for (;;) {
		head = list_entry(tmp, struct ctl_table_header, ctl_entry);

		if (!use_table(head))
			goto next;
		spin_unlock(&sysctl_lock);
		return head;
	next:
		root = head->root;
		tmp = tmp->next;
		header_list = lookup_header_list(root, namespaces);
		if (tmp != header_list)
			continue;

		do {
			root = list_entry(root->root_list.next,
					struct ctl_table_root, root_list);
			if (root == &sysctl_table_root)
				goto out;
			header_list = lookup_header_list(root, namespaces);
		} while (list_empty(header_list));
		tmp = header_list->next;
	}
out:
	spin_unlock(&sysctl_lock);
	return NULL;
}

static struct ctl_table_header *sysctl_head_next(struct ctl_table_header *prev)
{
	return __sysctl_head_next(current->nsproxy, prev);
}

void register_sysctl_root(struct ctl_table_root *root)
{
	spin_lock(&sysctl_lock);
	list_add_tail(&root->root_list, &sysctl_table_root.root_list);
	spin_unlock(&sysctl_lock);
}

/*
 * sysctl_perm does NOT grant the superuser all rights automatically, because
 * some sysctl variables are readonly even to root.
 */

static int test_perm(int mode, int op)
{
	if (!current_euid())
		mode >>= 6;
	else if (in_egroup_p(0))
		mode >>= 3;
	if ((op & ~mode & (MAY_READ|MAY_WRITE|MAY_EXEC)) == 0)
		return 0;
	return -EACCES;
}

static int sysctl_perm(struct ctl_table_root *root, struct ctl_table *table, int op)
{
	int mode;

	if (root->permissions)
		mode = root->permissions(root, current->nsproxy, table);
	else
		mode = table->mode;

	return test_perm(mode, op);
}

static void sysctl_set_parent(struct ctl_table *parent, struct ctl_table *table)
{
	for (; table->procname; table++) {
		table->parent = parent;
		if (table->child)
			sysctl_set_parent(table, table->child);
	}
}


static struct inode *proc_sys_make_inode(struct super_block *sb,
		struct ctl_table_header *head, struct ctl_table *table)
{
	struct inode *inode;
	struct proc_inode *ei;

	inode = new_inode(sb);
	if (!inode)
		goto out;

	inode->i_ino = get_next_ino();

	sysctl_head_get(head);
	ei = PROC_I(inode);
	ei->sysctl = head;
	ei->sysctl_entry = table;

	inode->i_mtime = inode->i_atime = inode->i_ctime = CURRENT_TIME;
	inode->i_mode = table->mode;
	if (!table->child) {
		inode->i_mode |= S_IFREG;
		inode->i_op = &proc_sys_inode_operations;
		inode->i_fop = &proc_sys_file_operations;
	} else {
		inode->i_mode |= S_IFDIR;
		inode->i_op = &proc_sys_dir_operations;
		inode->i_fop = &proc_sys_dir_file_operations;
	}
out:
	return inode;
}

static struct ctl_table *find_in_table(struct ctl_table *p, struct qstr *name)
{
	for ( ; p->procname; p++) {
		if (strlen(p->procname) != name->len)
			continue;

		if (memcmp(p->procname, name->name, name->len) != 0)
			continue;

		/* I have a match */
		return p;
	}
	return NULL;
}

static struct ctl_table_header *grab_header(struct inode *inode)
{
	if (PROC_I(inode)->sysctl)
		return sysctl_head_grab(PROC_I(inode)->sysctl);
	else
		return sysctl_head_next(NULL);
}

static struct dentry *proc_sys_lookup(struct inode *dir, struct dentry *dentry,
					struct nameidata *nd)
{
	struct ctl_table_header *head = grab_header(dir);
	struct ctl_table *table = PROC_I(dir)->sysctl_entry;
	struct ctl_table_header *h = NULL;
	struct qstr *name = &dentry->d_name;
	struct ctl_table *p;
	struct inode *inode;
	struct dentry *err = ERR_PTR(-ENOENT);

	if (IS_ERR(head))
		return ERR_CAST(head);

	if (table && !table->child) {
		WARN_ON(1);
		goto out;
	}

	table = table ? table->child : head->ctl_table;

	p = find_in_table(table, name);
	if (!p) {
		for (h = sysctl_head_next(NULL); h; h = sysctl_head_next(h)) {
			if (h->attached_to != table)
				continue;
			p = find_in_table(h->attached_by, name);
			if (p)
				break;
		}
	}

	if (!p)
		goto out;

	err = ERR_PTR(-ENOMEM);
	inode = proc_sys_make_inode(dir->i_sb, h ? h : head, p);
	if (h)
		sysctl_head_finish(h);

	if (!inode)
		goto out;

	err = NULL;
	d_set_d_op(dentry, &proc_sys_dentry_operations);
	d_add(dentry, inode);

out:
	sysctl_head_finish(head);
	return err;
}

static ssize_t proc_sys_call_handler(struct file *filp, void __user *buf,
		size_t count, loff_t *ppos, int write)
{
	struct inode *inode = filp->f_path.dentry->d_inode;
	struct ctl_table_header *head = grab_header(inode);
	struct ctl_table *table = PROC_I(inode)->sysctl_entry;
	ssize_t error;
	size_t res;

	if (IS_ERR(head))
		return PTR_ERR(head);

	/*
	 * At this point we know that the sysctl was not unregistered
	 * and won't be until we finish.
	 */
	error = -EPERM;
	if (sysctl_perm(head->root, table, write ? MAY_WRITE : MAY_READ))
		goto out;

	/* if that can happen at all, it should be -EINVAL, not -EISDIR */
	error = -EINVAL;
	if (!table->proc_handler)
		goto out;

	/* careful: calling conventions are nasty here */
	res = count;
	error = table->proc_handler(table, write, buf, &res, ppos);
	if (!error)
		error = res;
out:
	sysctl_head_finish(head);

	return error;
}

static ssize_t proc_sys_read(struct file *filp, char __user *buf,
				size_t count, loff_t *ppos)
{
	return proc_sys_call_handler(filp, (void __user *)buf, count, ppos, 0);
}

static ssize_t proc_sys_write(struct file *filp, const char __user *buf,
				size_t count, loff_t *ppos)
{
	return proc_sys_call_handler(filp, (void __user *)buf, count, ppos, 1);
}

static int proc_sys_open(struct inode *inode, struct file *filp)
{
	struct ctl_table *table = PROC_I(inode)->sysctl_entry;

	if (table->poll)
		filp->private_data = proc_sys_poll_event(table->poll);

	return 0;
}

static unsigned int proc_sys_poll(struct file *filp, poll_table *wait)
{
	struct inode *inode = filp->f_path.dentry->d_inode;
	struct ctl_table *table = PROC_I(inode)->sysctl_entry;
	unsigned long event = (unsigned long)filp->private_data;
	unsigned int ret = DEFAULT_POLLMASK;

	if (!table->proc_handler)
		goto out;

	if (!table->poll)
		goto out;

	poll_wait(filp, &table->poll->wait, wait);

	if (event != atomic_read(&table->poll->event)) {
		filp->private_data = proc_sys_poll_event(table->poll);
		ret = POLLIN | POLLRDNORM | POLLERR | POLLPRI;
	}

out:
	return ret;
}

static int proc_sys_fill_cache(struct file *filp, void *dirent,
				filldir_t filldir,
				struct ctl_table_header *head,
				struct ctl_table *table)
{
	struct dentry *child, *dir = filp->f_path.dentry;
	struct inode *inode;
	struct qstr qname;
	ino_t ino = 0;
	unsigned type = DT_UNKNOWN;

	qname.name = table->procname;
	qname.len  = strlen(table->procname);
	qname.hash = full_name_hash(qname.name, qname.len);

	child = d_lookup(dir, &qname);
	if (!child) {
		child = d_alloc(dir, &qname);
		if (child) {
			inode = proc_sys_make_inode(dir->d_sb, head, table);
			if (!inode) {
				dput(child);
				return -ENOMEM;
			} else {
				d_set_d_op(child, &proc_sys_dentry_operations);
				d_add(child, inode);
			}
		} else {
			return -ENOMEM;
		}
	}
	inode = child->d_inode;
	ino  = inode->i_ino;
	type = inode->i_mode >> 12;
	dput(child);
	return !!filldir(dirent, qname.name, qname.len, filp->f_pos, ino, type);
}

static int scan(struct ctl_table_header *head, ctl_table *table,
		unsigned long *pos, struct file *file,
		void *dirent, filldir_t filldir)
{

	for (; table->procname; table++, (*pos)++) {
		int res;

		if (*pos < file->f_pos)
			continue;

		res = proc_sys_fill_cache(file, dirent, filldir, head, table);
		if (res)
			return res;

		file->f_pos = *pos + 1;
	}
	return 0;
}

static int proc_sys_readdir(struct file *filp, void *dirent, filldir_t filldir)
{
	struct dentry *dentry = filp->f_path.dentry;
	struct inode *inode = dentry->d_inode;
	struct ctl_table_header *head = grab_header(inode);
	struct ctl_table *table = PROC_I(inode)->sysctl_entry;
	struct ctl_table_header *h = NULL;
	unsigned long pos;
	int ret = -EINVAL;

	if (IS_ERR(head))
		return PTR_ERR(head);

	if (table && !table->child) {
		WARN_ON(1);
		goto out;
	}

	table = table ? table->child : head->ctl_table;

	ret = 0;
	/* Avoid a switch here: arm builds fail with missing __cmpdi2 */
	if (filp->f_pos == 0) {
		if (filldir(dirent, ".", 1, filp->f_pos,
				inode->i_ino, DT_DIR) < 0)
			goto out;
		filp->f_pos++;
	}
	if (filp->f_pos == 1) {
		if (filldir(dirent, "..", 2, filp->f_pos,
				parent_ino(dentry), DT_DIR) < 0)
			goto out;
		filp->f_pos++;
	}
	pos = 2;

	ret = scan(head, table, &pos, filp, dirent, filldir);
	if (ret)
		goto out;

	for (h = sysctl_head_next(NULL); h; h = sysctl_head_next(h)) {
		if (h->attached_to != table)
			continue;
		ret = scan(h, h->attached_by, &pos, filp, dirent, filldir);
		if (ret) {
			sysctl_head_finish(h);
			break;
		}
	}
	ret = 1;
out:
	sysctl_head_finish(head);
	return ret;
}

static int proc_sys_permission(struct inode *inode, int mask)
{
	/*
	 * sysctl entries that are not writeable,
	 * are _NOT_ writeable, capabilities or not.
	 */
	struct ctl_table_header *head;
	struct ctl_table *table;
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
		error = sysctl_perm(head->root, table, mask & ~MAY_NOT_BLOCK);

	sysctl_head_finish(head);
	return error;
}

static int proc_sys_setattr(struct dentry *dentry, struct iattr *attr)
{
	struct inode *inode = dentry->d_inode;
	int error;

	if (attr->ia_valid & (ATTR_MODE | ATTR_UID | ATTR_GID))
		return -EPERM;

	error = inode_change_ok(inode, attr);
	if (error)
		return error;

	if ((attr->ia_valid & ATTR_SIZE) &&
	    attr->ia_size != i_size_read(inode)) {
		error = vmtruncate(inode, attr->ia_size);
		if (error)
			return error;
	}

	setattr_copy(inode, attr);
	mark_inode_dirty(inode);
	return 0;
}

static int proc_sys_getattr(struct vfsmount *mnt, struct dentry *dentry, struct kstat *stat)
{
	struct inode *inode = dentry->d_inode;
	struct ctl_table_header *head = grab_header(inode);
	struct ctl_table *table = PROC_I(inode)->sysctl_entry;

	if (IS_ERR(head))
		return PTR_ERR(head);

	generic_fillattr(inode, stat);
	if (table)
		stat->mode = (stat->mode & S_IFMT) | table->mode;

	sysctl_head_finish(head);
	return 0;
}

static const struct file_operations proc_sys_file_operations = {
	.open		= proc_sys_open,
	.poll		= proc_sys_poll,
	.read		= proc_sys_read,
	.write		= proc_sys_write,
	.llseek		= default_llseek,
};

static const struct file_operations proc_sys_dir_file_operations = {
	.read		= generic_read_dir,
	.readdir	= proc_sys_readdir,
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

static int proc_sys_revalidate(struct dentry *dentry, struct nameidata *nd)
{
	if (nd->flags & LOOKUP_RCU)
		return -ECHILD;
	return !PROC_I(dentry->d_inode)->sysctl->unregistering;
}

static int proc_sys_delete(const struct dentry *dentry)
{
	return !!PROC_I(dentry->d_inode)->sysctl->unregistering;
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

static int proc_sys_compare(const struct dentry *parent,
		const struct inode *pinode,
		const struct dentry *dentry, const struct inode *inode,
		unsigned int len, const char *str, const struct qstr *name)
{
	struct ctl_table_header *head;
	/* Although proc doesn't have negative dentries, rcu-walk means
	 * that inode here can be NULL */
	/* AV: can it, indeed? */
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

static struct ctl_table *is_branch_in(struct ctl_table *branch,
				      struct ctl_table *table)
{
	struct ctl_table *p;
	const char *s = branch->procname;

	/* branch should have named subdirectory as its first element */
	if (!s || !branch->child)
		return NULL;

	/* ... and nothing else */
	if (branch[1].procname)
		return NULL;

	/* table should contain subdirectory with the same name */
	for (p = table; p->procname; p++) {
		if (!p->child)
			continue;
		if (p->procname && strcmp(p->procname, s) == 0)
			return p;
	}
	return NULL;
}

/* see if attaching q to p would be an improvement */
static void try_attach(struct ctl_table_header *p, struct ctl_table_header *q)
{
	struct ctl_table *to = p->ctl_table, *by = q->ctl_table;
	struct ctl_table *next;
	int is_better = 0;
	int not_in_parent = !p->attached_by;

	while ((next = is_branch_in(by, to)) != NULL) {
		if (by == q->attached_by)
			is_better = 1;
		if (to == p->attached_by)
			not_in_parent = 1;
		by = by->child;
		to = next->child;
	}

	if (is_better && not_in_parent) {
		q->attached_by = by;
		q->attached_to = to;
		q->parent = p;
	}
}

#ifdef CONFIG_SYSCTL_SYSCALL_CHECK
static int sysctl_depth(struct ctl_table *table)
{
	struct ctl_table *tmp;
	int depth;

	depth = 0;
	for (tmp = table; tmp->parent; tmp = tmp->parent)
		depth++;

	return depth;
}

static struct ctl_table *sysctl_parent(struct ctl_table *table, int n)
{
	int i;

	for (i = 0; table && i < n; i++)
		table = table->parent;

	return table;
}


static void sysctl_print_path(struct ctl_table *table)
{
	struct ctl_table *tmp;
	int depth, i;
	depth = sysctl_depth(table);
	if (table->procname) {
		for (i = depth; i >= 0; i--) {
			tmp = sysctl_parent(table, i);
			printk("/%s", tmp->procname?tmp->procname:"");
		}
	}
	printk(" ");
}

static struct ctl_table *sysctl_check_lookup(struct nsproxy *namespaces,
						struct ctl_table *table)
{
	struct ctl_table_header *head;
	struct ctl_table *ref, *test;
	int depth, cur_depth;

	depth = sysctl_depth(table);

	for (head = __sysctl_head_next(namespaces, NULL); head;
	     head = __sysctl_head_next(namespaces, head)) {
		cur_depth = depth;
		ref = head->ctl_table;
repeat:
		test = sysctl_parent(table, cur_depth);
		for (; ref->procname; ref++) {
			int match = 0;
			if (cur_depth && !ref->child)
				continue;

			if (test->procname && ref->procname &&
			    (strcmp(test->procname, ref->procname) == 0))
					match++;

			if (match) {
				if (cur_depth != 0) {
					cur_depth--;
					ref = ref->child;
					goto repeat;
				}
				goto out;
			}
		}
	}
	ref = NULL;
out:
	sysctl_head_finish(head);
	return ref;
}

static void set_fail(const char **fail, struct ctl_table *table, const char *str)
{
	if (*fail) {
		printk(KERN_ERR "sysctl table check failed: ");
		sysctl_print_path(table);
		printk(" %s\n", *fail);
		dump_stack();
	}
	*fail = str;
}

static void sysctl_check_leaf(struct nsproxy *namespaces,
				struct ctl_table *table, const char **fail)
{
	struct ctl_table *ref;

	ref = sysctl_check_lookup(namespaces, table);
	if (ref && (ref != table))
		set_fail(fail, table, "Sysctl already exists");
}

static int sysctl_check_table(struct nsproxy *namespaces, struct ctl_table *table)
{
	int error = 0;
	for (; table->procname; table++) {
		const char *fail = NULL;

		if (table->parent) {
			if (!table->parent->procname)
				set_fail(&fail, table, "Parent without procname");
		}
		if (table->child) {
			if (table->data)
				set_fail(&fail, table, "Directory with data?");
			if (table->maxlen)
				set_fail(&fail, table, "Directory with maxlen?");
			if ((table->mode & (S_IRUGO|S_IXUGO)) != table->mode)
				set_fail(&fail, table, "Writable sysctl directory");
			if (table->proc_handler)
				set_fail(&fail, table, "Directory with proc_handler");
			if (table->extra1)
				set_fail(&fail, table, "Directory with extra1");
			if (table->extra2)
				set_fail(&fail, table, "Directory with extra2");
		} else {
			if ((table->proc_handler == proc_dostring) ||
			    (table->proc_handler == proc_dointvec) ||
			    (table->proc_handler == proc_dointvec_minmax) ||
			    (table->proc_handler == proc_dointvec_jiffies) ||
			    (table->proc_handler == proc_dointvec_userhz_jiffies) ||
			    (table->proc_handler == proc_dointvec_ms_jiffies) ||
			    (table->proc_handler == proc_doulongvec_minmax) ||
			    (table->proc_handler == proc_doulongvec_ms_jiffies_minmax)) {
				if (!table->data)
					set_fail(&fail, table, "No data");
				if (!table->maxlen)
					set_fail(&fail, table, "No maxlen");
			}
#ifdef CONFIG_PROC_SYSCTL
			if (!table->proc_handler)
				set_fail(&fail, table, "No proc_handler");
#endif
			sysctl_check_leaf(namespaces, table, &fail);
		}
		if (table->mode > 0777)
			set_fail(&fail, table, "bogus .mode");
		if (fail) {
			set_fail(&fail, table, NULL);
			error = -EINVAL;
		}
		if (table->child)
			error |= sysctl_check_table(namespaces, table->child);
	}
	return error;
}
#endif /* CONFIG_SYSCTL_SYSCALL_CHECK */

/**
 * __register_sysctl_paths - register a sysctl hierarchy
 * @root: List of sysctl headers to register on
 * @namespaces: Data to compute which lists of sysctl entries are visible
 * @path: The path to the directory the sysctl table is in.
 * @table: the top-level table structure
 *
 * Register a sysctl table hierarchy. @table should be a filled in ctl_table
 * array. A completely 0 filled entry terminates the table.
 *
 * The members of the &struct ctl_table structure are used as follows:
 *
 * procname - the name of the sysctl file under /proc/sys. Set to %NULL to not
 *            enter a sysctl file
 *
 * data - a pointer to data for use by proc_handler
 *
 * maxlen - the maximum size in bytes of the data
 *
 * mode - the file permissions for the /proc/sys file, and for sysctl(2)
 *
 * child - a pointer to the child sysctl table if this entry is a directory, or
 *         %NULL.
 *
 * proc_handler - the text handler routine (described below)
 *
 * de - for internal use by the sysctl routines
 *
 * extra1, extra2 - extra pointers usable by the proc handler routines
 *
 * Leaf nodes in the sysctl tree will be represented by a single file
 * under /proc; non-leaf nodes will be represented by directories.
 *
 * sysctl(2) can automatically manage read and write requests through
 * the sysctl table.  The data and maxlen fields of the ctl_table
 * struct enable minimal validation of the values being written to be
 * performed, and the mode field allows minimal authentication.
 *
 * There must be a proc_handler routine for any terminal nodes
 * mirrored under /proc/sys (non-terminals are handled by a built-in
 * directory handler).  Several default handlers are available to
 * cover common cases -
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
struct ctl_table_header *__register_sysctl_paths(
	struct ctl_table_root *root,
	struct nsproxy *namespaces,
	const struct ctl_path *path, struct ctl_table *table)
{
	struct ctl_table_header *header;
	struct ctl_table *new, **prevp;
	unsigned int n, npath;
	struct ctl_table_set *set;

	/* Count the path components */
	for (npath = 0; path[npath].procname; ++npath)
		;

	/*
	 * For each path component, allocate a 2-element ctl_table array.
	 * The first array element will be filled with the sysctl entry
	 * for this, the second will be the sentinel (procname == 0).
	 *
	 * We allocate everything in one go so that we don't have to
	 * worry about freeing additional memory in unregister_sysctl_table.
	 */
	header = kzalloc(sizeof(struct ctl_table_header) +
			 (2 * npath * sizeof(struct ctl_table)), GFP_KERNEL);
	if (!header)
		return NULL;

	new = (struct ctl_table *) (header + 1);

	/* Now connect the dots */
	prevp = &header->ctl_table;
	for (n = 0; n < npath; ++n, ++path) {
		/* Copy the procname */
		new->procname = path->procname;
		new->mode     = 0555;

		*prevp = new;
		prevp = &new->child;

		new += 2;
	}
	*prevp = table;
	header->ctl_table_arg = table;

	INIT_LIST_HEAD(&header->ctl_entry);
	header->used = 0;
	header->unregistering = NULL;
	header->root = root;
	sysctl_set_parent(NULL, header->ctl_table);
	header->count = 1;
#ifdef CONFIG_SYSCTL_SYSCALL_CHECK
	if (sysctl_check_table(namespaces, header->ctl_table)) {
		kfree(header);
		return NULL;
	}
#endif
	spin_lock(&sysctl_lock);
	header->set = lookup_header_set(root, namespaces);
	header->attached_by = header->ctl_table;
	header->attached_to = root_table;
	header->parent = &root_table_header;
	for (set = header->set; set; set = set->parent) {
		struct ctl_table_header *p;
		list_for_each_entry(p, &set->list, ctl_entry) {
			if (p->unregistering)
				continue;
			try_attach(p, header);
		}
	}
	header->parent->count++;
	list_add_tail(&header->ctl_entry, &header->set->list);
	spin_unlock(&sysctl_lock);

	return header;
}

/**
 * register_sysctl_table_path - register a sysctl table hierarchy
 * @path: The path to the directory the sysctl table is in.
 * @table: the top-level table structure
 *
 * Register a sysctl table hierarchy. @table should be a filled in ctl_table
 * array. A completely 0 filled entry terminates the table.
 *
 * See __register_sysctl_paths for more details.
 */
struct ctl_table_header *register_sysctl_paths(const struct ctl_path *path,
						struct ctl_table *table)
{
	return __register_sysctl_paths(&sysctl_table_root, current->nsproxy,
					path, table);
}
EXPORT_SYMBOL(register_sysctl_paths);

/**
 * register_sysctl_table - register a sysctl table hierarchy
 * @table: the top-level table structure
 *
 * Register a sysctl table hierarchy. @table should be a filled in ctl_table
 * array. A completely 0 filled entry terminates the table.
 *
 * See register_sysctl_paths for more details.
 */
struct ctl_table_header *register_sysctl_table(struct ctl_table *table)
{
	static const struct ctl_path null_path[] = { {} };

	return register_sysctl_paths(null_path, table);
}
EXPORT_SYMBOL(register_sysctl_table);

/**
 * unregister_sysctl_table - unregister a sysctl table hierarchy
 * @header: the header returned from register_sysctl_table
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
	start_unregistering(header);
	if (!--header->parent->count) {
		WARN_ON(1);
		kfree_rcu(header->parent, rcu);
	}
	if (!--header->count)
		kfree_rcu(header, rcu);
	spin_unlock(&sysctl_lock);
}
EXPORT_SYMBOL(unregister_sysctl_table);

void setup_sysctl_set(struct ctl_table_set *p,
	struct ctl_table_set *parent,
	int (*is_seen)(struct ctl_table_set *))
{
	INIT_LIST_HEAD(&p->list);
	p->parent = parent ? parent : &sysctl_table_root.default_set;
	p->is_seen = is_seen;
}


int __init proc_sys_init(void)
{
	struct proc_dir_entry *proc_sys_root;

	proc_sys_root = proc_mkdir("sys", NULL);
	proc_sys_root->proc_iops = &proc_sys_dir_operations;
	proc_sys_root->proc_fops = &proc_sys_dir_file_operations;
	proc_sys_root->nlink = 0;

	return sysctl_init();
}
