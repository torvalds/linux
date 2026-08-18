// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/fs/filesystems.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  table of configured filesystems
 */

#include <linux/syscalls.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/kmod.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/fs_parser.h>
#include <linux/rculist.h>

/*
 * Read-mostly filesystem drivers list.
 *
 * Readers walk under rcu_read_lock(); writers take file_systems_lock
 * and publish via _rcu hlist primitives.  unregister_filesystem()
 * synchronize_rcu()s after unlock so the embedded file_system_type
 * can't go away under a reader.  To keep using a filesystem after
 * the RCU section ends, take a module reference via try_module_get().
 */
static HLIST_HEAD(file_systems);
static DEFINE_SPINLOCK(file_systems_lock);

#ifdef CONFIG_PROC_FS
/*
 * Cache a stringified version of the filesystem list.
 *
 * The fs list gets queried a lot by userspace because of libselinux, including
 * rather surprising programs (would you guess *sed* is on the list?). In order
 * to reduce the overhead we cache the resulting string, which normally hangs
 * around below 512 bytes in size.
 *
 * As the list almost never changes, its creation is not particularly optimized
 * to keep things simple.
 *
 * We sort it out on read in order to not introduce a failure point for fs
 * registration (in principle we may be unable to alloc memory for the list).
 */
struct file_systems_string {
	struct rcu_head rcu;
	unsigned long gen;
	size_t len;
	char string[];
};

static unsigned long file_systems_gen;
static struct file_systems_string __read_mostly __rcu *file_systems_string;

static void invalidate_filesystems_string(void);
#else
static inline void invalidate_filesystems_string(void) { }
#endif

/* WARNING: This can be used only if we _already_ own a reference */
struct file_system_type *get_filesystem(struct file_system_type *fs)
{
	__module_get(fs->owner);
	return fs;
}

void put_filesystem(struct file_system_type *fs)
{
	module_put(fs->owner);
}

static struct file_system_type *find_filesystem(const char *name, unsigned len)
{
	struct file_system_type *fs;

	hlist_for_each_entry_rcu(fs, &file_systems, list,
				 lockdep_is_held(&file_systems_lock))
		if (strncmp(fs->name, name, len) == 0 && !fs->name[len])
			return fs;
	return NULL;
}

/**
 *	register_filesystem - register a new filesystem
 *	@fs: the file system structure
 *
 *	Adds the file system passed to the list of file systems the kernel
 *	is aware of for mount and other syscalls. Returns 0 on success,
 *	or a negative errno code on an error.
 *
 *	The &struct file_system_type that is passed is linked into the kernel
 *	structures and must not be freed until the file system has been
 *	unregistered.
 */
int register_filesystem(struct file_system_type *fs)
{
	if (fs->parameters &&
	    !fs_validate_description(fs->name, fs->parameters))
		return -EINVAL;

	BUG_ON(strchr(fs->name, '.'));
	if (!hlist_unhashed_lockless(&fs->list))
		return -EBUSY;

	guard(spinlock)(&file_systems_lock);
	if (find_filesystem(fs->name, strlen(fs->name)))
		return -EBUSY;
	hlist_add_tail_rcu(&fs->list, &file_systems);
	invalidate_filesystems_string();
	return 0;
}
EXPORT_SYMBOL(register_filesystem);

/**
 *	unregister_filesystem - unregister a file system
 *	@fs: filesystem to unregister
 *
 *	Remove a file system that was previously successfully registered
 *	with the kernel. An error is returned if the file system is not found.
 *	Zero is returned on a success.
 *
 *	Once this function has returned the &struct file_system_type structure
 *	may be freed or reused.
 */
int unregister_filesystem(struct file_system_type *fs)
{
	scoped_guard(spinlock, &file_systems_lock) {
		if (hlist_unhashed(&fs->list))
			return -EINVAL;
		hlist_del_init_rcu(&fs->list);
		invalidate_filesystems_string();
	}
	synchronize_rcu();
	return 0;
}
EXPORT_SYMBOL(unregister_filesystem);

#ifdef CONFIG_SYSFS_SYSCALL
static int fs_index(const char __user *__name)
{
	struct file_system_type *p;
	char *name __free(kfree) = strndup_user(__name, PATH_MAX);
	int index = 0;

	if (IS_ERR(name))
		return PTR_ERR(name);

	guard(rcu)();
	hlist_for_each_entry_rcu(p, &file_systems, list) {
		if (strcmp(p->name, name) == 0)
			return index;
		index++;
	}
	return -EINVAL;
}

static int fs_name(unsigned int index, char __user *buf)
{
	struct file_system_type *p, *found = NULL;
	int len, res;

	scoped_guard(rcu) {
		hlist_for_each_entry_rcu(p, &file_systems, list) {
			if (index--)
				continue;
			if (try_module_get(p->owner))
				found = p;
			break;
		}
	}
	if (!found)
		return -EINVAL;

	/* OK, we got the reference, so we can safely block */
	len = strlen(found->name) + 1;
	res = copy_to_user(buf, found->name, len) ? -EFAULT : 0;
	put_filesystem(found);
	return res;
}

static int fs_maxindex(void)
{
	struct file_system_type *p;
	int index = 0;

	guard(rcu)();
	hlist_for_each_entry_rcu(p, &file_systems, list)
		index++;
	return index;
}

/*
 * Whee.. Weird sysv syscall.
 */
SYSCALL_DEFINE3(sysfs, int, option, unsigned long, arg1, unsigned long, arg2)
{
	int retval = -EINVAL;

	switch (option) {
		case 1:
			retval = fs_index((const char __user *) arg1);
			break;

		case 2:
			retval = fs_name(arg1, (char __user *) arg2);
			break;

		case 3:
			retval = fs_maxindex();
			break;
	}
	return retval;
}
#endif

int __init list_bdev_fs_names(char *buf, size_t size)
{
	struct file_system_type *p;
	size_t len;
	int count = 0;

	guard(rcu)();
	hlist_for_each_entry_rcu(p, &file_systems, list) {
		if (!(p->fs_flags & FS_REQUIRES_DEV))
			continue;
		len = strlen(p->name) + 1;
		if (len > size) {
			pr_warn("%s: truncating file system list\n", __func__);
			break;
		}
		memcpy(buf, p->name, len);
		buf += len;
		size -= len;
		count++;
	}
	return count;
}

#ifdef CONFIG_PROC_FS
static void invalidate_filesystems_string(void)
{
	struct file_systems_string *old;

	lockdep_assert_held_write(&file_systems_lock);
	file_systems_gen++;
	old = rcu_replace_pointer(file_systems_string, NULL,
			   lockdep_is_held(&file_systems_lock));
	if (old)
		kfree_rcu(old, rcu);
}

static __cold noinline int regen_filesystems_string(void)
{
	struct file_system_type *p;
	struct file_systems_string *old, *new;
	size_t newlen, usedlen;
	unsigned long gen;

retry:
	newlen = 0;

	/* pre-calc space for each fs */
	spin_lock(&file_systems_lock);
	gen = file_systems_gen;
	hlist_for_each_entry_rcu(p, &file_systems, list) {
		if (!(p->fs_flags & FS_REQUIRES_DEV))
			newlen += strlen("nodev");
		newlen += strlen("\t") + strlen(p->name) + strlen("\n");
	}
	spin_unlock(&file_systems_lock);

	new = kmalloc(offsetof(struct file_systems_string, string) + newlen + 1,
		      GFP_KERNEL);
	if (!new)
		return -ENOMEM;

	new->gen = gen;
	new->len = newlen;
	new->string[newlen] = '\0';

	spin_lock(&file_systems_lock);
	old = file_systems_string;

	/*
	 * Did someone beat us to it?
	 */
	if (old && old->gen == file_systems_gen) {
		spin_unlock(&file_systems_lock);
		kfree(new);
		return 0;
	}

	/*
	 * Did the list change in the meantime?
	 */
	if (gen != file_systems_gen) {
		spin_unlock(&file_systems_lock);
		kfree(new);
		goto retry;
	}

	/*
	 * Populate the string.
	 *
	 * We know we have just enough space because we calculated the right
	 * size the previous time we had the lock and confirmed the list has
	 * not changed after reacquiring it.
	 */
	usedlen = 0;
	hlist_for_each_entry_rcu(p, &file_systems, list) {
		usedlen += sprintf(&new->string[usedlen], "%s\t%s\n",
				   (p->fs_flags & FS_REQUIRES_DEV) ? "" : "nodev",
				   p->name);
	}

	if (WARN_ON_ONCE(new->len != strlen(new->string))) {
		/*
		 * Should never happen of course, keep this in case someone changes string
		 * generation above and messes it up.
		 */
		spin_unlock(&file_systems_lock);
		kfree(new);
		return -EINVAL;
	}

	rcu_assign_pointer(file_systems_string, new);
	spin_unlock(&file_systems_lock);
	if (old)
		kfree_rcu(old, rcu);
	return 0;
}

static __cold noinline int filesystems_proc_show_fallback(struct seq_file *m, void *v)
{
	struct file_system_type *p;

	guard(rcu)();
	hlist_for_each_entry_rcu(p, &file_systems, list) {
		seq_printf(m, "%s\t%s\n",
			   (p->fs_flags & FS_REQUIRES_DEV) ? "" : "nodev",
			   p->name);
	}
	return 0;
}

static int filesystems_proc_show(struct seq_file *m, void *v)
{
	struct file_systems_string *fss;

	for (;;) {
		scoped_guard(rcu) {
			fss = rcu_dereference(file_systems_string);
			if (likely(fss)) {
				seq_write(m, fss->string, fss->len);
				return 0;
			}
		}

		int err = regen_filesystems_string();
		if (unlikely(err))
			return filesystems_proc_show_fallback(m, v);
	}
}

static int __init proc_filesystems_init(void)
{
	struct proc_dir_entry *pde;

	pde = proc_create_single("filesystems", 0, NULL, filesystems_proc_show);
	if (!pde)
		return -ENOMEM;
	proc_make_permanent(pde);
	return 0;
}
module_init(proc_filesystems_init);
#endif

static struct file_system_type *__get_fs_type(const char *name, int len)
{
	struct file_system_type *fs;

	guard(rcu)();
	fs = find_filesystem(name, len);
	if (fs && !try_module_get(fs->owner))
		fs = NULL;
	return fs;
}

struct file_system_type *get_fs_type(const char *name)
{
	struct file_system_type *fs;
	const char *dot = strchr(name, '.');
	int len = dot ? dot - name : strlen(name);

	fs = __get_fs_type(name, len);
	if (!fs && (request_module("fs-%.*s", len, name) == 0)) {
		fs = __get_fs_type(name, len);
		if (!fs)
			pr_warn_once("request_module fs-%.*s succeeded, but still no fs?\n",
				     len, name);
	}

	if (dot && fs && !(fs->fs_flags & FS_HAS_SUBTYPE)) {
		put_filesystem(fs);
		fs = NULL;
	}
	return fs;
}
EXPORT_SYMBOL(get_fs_type);
