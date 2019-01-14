// SPDX-License-Identifier: GPL-2.0
/*
 * fail_function.c: Function-based error injection
 */
#include <linux/error-injection.h>
#include <linux/debugfs.h>
#include <linux/fault-inject.h>
#include <linux/kallsyms.h>
#include <linux/kprobes.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

static int fei_kprobe_handler(struct kprobe *kp, struct pt_regs *regs);

static void fei_post_handler(struct kprobe *kp, struct pt_regs *regs,
			     unsigned long flags)
{
	/*
	 * A dummy post handler is required to prohibit optimizing, because
	 * jump optimization does not support execution path overriding.
	 */
}

struct fei_attr {
	struct list_head list;
	struct kprobe kp;
	unsigned long retval;
};
static DEFINE_MUTEX(fei_lock);
static LIST_HEAD(fei_attr_list);
static DECLARE_FAULT_ATTR(fei_fault_attr);
static struct dentry *fei_debugfs_dir;

static unsigned long adjust_error_retval(unsigned long addr, unsigned long retv)
{
	switch (get_injectable_error_type(addr)) {
	case EI_ETYPE_NULL:
		if (retv != 0)
			return 0;
		break;
	case EI_ETYPE_ERRNO:
		if (retv < (unsigned long)-MAX_ERRNO)
			return (unsigned long)-EINVAL;
		break;
	case EI_ETYPE_ERRNO_NULL:
		if (retv != 0 && retv < (unsigned long)-MAX_ERRNO)
			return (unsigned long)-EINVAL;
		break;
	}

	return retv;
}

static struct fei_attr *fei_attr_new(const char *sym, unsigned long addr)
{
	struct fei_attr *attr;

	attr = kzalloc(sizeof(*attr), GFP_KERNEL);
	if (attr) {
		attr->kp.symbol_name = kstrdup(sym, GFP_KERNEL);
		if (!attr->kp.symbol_name) {
			kfree(attr);
			return NULL;
		}
		attr->kp.pre_handler = fei_kprobe_handler;
		attr->kp.post_handler = fei_post_handler;
		attr->retval = adjust_error_retval(addr, 0);
		INIT_LIST_HEAD(&attr->list);
	}
	return attr;
}

static void fei_attr_free(struct fei_attr *attr)
{
	if (attr) {
		kfree(attr->kp.symbol_name);
		kfree(attr);
	}
}

static struct fei_attr *fei_attr_lookup(const char *sym)
{
	struct fei_attr *attr;

	list_for_each_entry(attr, &fei_attr_list, list) {
		if (!strcmp(attr->kp.symbol_name, sym))
			return attr;
	}

	return NULL;
}

static bool fei_attr_is_valid(struct fei_attr *_attr)
{
	struct fei_attr *attr;

	list_for_each_entry(attr, &fei_attr_list, list) {
		if (attr == _attr)
			return true;
	}

	return false;
}

static int fei_retval_set(void *data, u64 val)
{
	struct fei_attr *attr = data;
	unsigned long retv = (unsigned long)val;
	int err = 0;

	mutex_lock(&fei_lock);
	/*
	 * Since this operation can be done after retval file is removed,
	 * It is safer to check the attr is still valid before accessing
	 * its member.
	 */
	if (!fei_attr_is_valid(attr)) {
		err = -ENOENT;
		goto out;
	}

	if (attr->kp.addr) {
		if (adjust_error_retval((unsigned long)attr->kp.addr,
					val) != retv)
			err = -EINVAL;
	}
	if (!err)
		attr->retval = val;
out:
	mutex_unlock(&fei_lock);

	return err;
}

static int fei_retval_get(void *data, u64 *val)
{
	struct fei_attr *attr = data;
	int err = 0;

	mutex_lock(&fei_lock);
	/* Here we also validate @attr to ensure it still exists. */
	if (!fei_attr_is_valid(attr))
		err = -ENOENT;
	else
		*val = attr->retval;
	mutex_unlock(&fei_lock);

	return err;
}
DEFINE_DEBUGFS_ATTRIBUTE(fei_retval_ops, fei_retval_get, fei_retval_set,
			 "%llx\n");

static int fei_debugfs_add_attr(struct fei_attr *attr)
{
	struct dentry *dir;

	dir = debugfs_create_dir(attr->kp.symbol_name, fei_debugfs_dir);
	if (!dir)
		return -ENOMEM;

	if (!debugfs_create_file("retval", 0600, dir, attr, &fei_retval_ops)) {
		debugfs_remove_recursive(dir);
		return -ENOMEM;
	}

	return 0;
}

static void fei_debugfs_remove_attr(struct fei_attr *attr)
{
	struct dentry *dir;

	dir = debugfs_lookup(attr->kp.symbol_name, fei_debugfs_dir);
	debugfs_remove_recursive(dir);
}

static int fei_kprobe_handler(struct kprobe *kp, struct pt_regs *regs)
{
	struct fei_attr *attr = container_of(kp, struct fei_attr, kp);

	if (should_fail(&fei_fault_attr, 1)) {
		regs_set_return_value(regs, attr->retval);
		override_function_with_return(regs);
		return 1;
	}

	return 0;
}
NOKPROBE_SYMBOL(fei_kprobe_handler)

static void *fei_seq_start(struct seq_file *m, loff_t *pos)
{
	mutex_lock(&fei_lock);
	return seq_list_start(&fei_attr_list, *pos);
}

static void fei_seq_stop(struct seq_file *m, void *v)
{
	mutex_unlock(&fei_lock);
}

static void *fei_seq_next(struct seq_file *m, void *v, loff_t *pos)
{
	return seq_list_next(v, &fei_attr_list, pos);
}

static int fei_seq_show(struct seq_file *m, void *v)
{
	struct fei_attr *attr = list_entry(v, struct fei_attr, list);

	seq_printf(m, "%pf\n", attr->kp.addr);
	return 0;
}

static const struct seq_operations fei_seq_ops = {
	.start	= fei_seq_start,
	.next	= fei_seq_next,
	.stop	= fei_seq_stop,
	.show	= fei_seq_show,
};

static int fei_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &fei_seq_ops);
}

static void fei_attr_remove(struct fei_attr *attr)
{
	fei_debugfs_remove_attr(attr);
	unregister_kprobe(&attr->kp);
	list_del(&attr->list);
	fei_attr_free(attr);
}

static void fei_attr_remove_all(void)
{
	struct fei_attr *attr, *n;

	list_for_each_entry_safe(attr, n, &fei_attr_list, list) {
		fei_attr_remove(attr);
	}
}

static ssize_t fei_write(struct file *file, const char __user *buffer,
			 size_t count, loff_t *ppos)
{
	struct fei_attr *attr;
	unsigned long addr;
	char *buf, *sym;
	int ret;

	/* cut off if it is too long */
	if (count > KSYM_NAME_LEN)
		count = KSYM_NAME_LEN;
	buf = kmalloc(count + 1, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	if (copy_from_user(buf, buffer, count)) {
		ret = -EFAULT;
		goto out;
	}
	buf[count] = '\0';
	sym = strstrip(buf);

	mutex_lock(&fei_lock);

	/* Writing just spaces will remove all injection points */
	if (sym[0] == '\0') {
		fei_attr_remove_all();
		ret = count;
		goto out;
	}
	/* Writing !function will remove one injection point */
	if (sym[0] == '!') {
		attr = fei_attr_lookup(sym + 1);
		if (!attr) {
			ret = -ENOENT;
			goto out;
		}
		fei_attr_remove(attr);
		ret = count;
		goto out;
	}

	addr = kallsyms_lookup_name(sym);
	if (!addr) {
		ret = -EINVAL;
		goto out;
	}
	if (!within_error_injection_list(addr)) {
		ret = -ERANGE;
		goto out;
	}
	if (fei_attr_lookup(sym)) {
		ret = -EBUSY;
		goto out;
	}
	attr = fei_attr_new(sym, addr);
	if (!attr) {
		ret = -ENOMEM;
		goto out;
	}

	ret = register_kprobe(&attr->kp);
	if (!ret)
		ret = fei_debugfs_add_attr(attr);
	if (ret < 0)
		fei_attr_remove(attr);
	else {
		list_add_tail(&attr->list, &fei_attr_list);
		ret = count;
	}
out:
	kfree(buf);
	mutex_unlock(&fei_lock);
	return ret;
}

static const struct file_operations fei_ops = {
	.open =		fei_open,
	.read =		seq_read,
	.write =	fei_write,
	.llseek =	seq_lseek,
	.release =	seq_release,
};

static int __init fei_debugfs_init(void)
{
	struct dentry *dir;

	dir = fault_create_debugfs_attr("fail_function", NULL,
					&fei_fault_attr);
	if (IS_ERR(dir))
		return PTR_ERR(dir);

	/* injectable attribute is just a symlink of error_inject/list */
	if (!debugfs_create_symlink("injectable", dir,
				    "../error_injection/list"))
		goto error;

	if (!debugfs_create_file("inject", 0600, dir, NULL, &fei_ops))
		goto error;

	fei_debugfs_dir = dir;

	return 0;
error:
	debugfs_remove_recursive(dir);
	return -ENOMEM;
}

late_initcall(fei_debugfs_init);
