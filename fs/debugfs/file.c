/*
 *  file.c - part of debugfs, a tiny little debug file system
 *
 *  Copyright (C) 2004 Greg Kroah-Hartman <greg@kroah.com>
 *  Copyright (C) 2004 IBM Inc.
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License version
 *	2 as published by the Free Software Foundation.
 *
 *  debugfs is for people to use instead of /proc or /sys.
 *  See Documentation/DocBook/filesystems for more details.
 *
 */

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/seq_file.h>
#include <linux/pagemap.h>
#include <linux/debugfs.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/atomic.h>
#include <linux/device.h>
#include <linux/srcu.h>
#include <asm/poll.h>

#include "internal.h"

struct poll_table_struct;

static ssize_t default_read_file(struct file *file, char __user *buf,
				 size_t count, loff_t *ppos)
{
	return 0;
}

static ssize_t default_write_file(struct file *file, const char __user *buf,
				   size_t count, loff_t *ppos)
{
	return count;
}

const struct file_operations debugfs_noop_file_operations = {
	.read =		default_read_file,
	.write =	default_write_file,
	.open =		simple_open,
	.llseek =	noop_llseek,
};

/**
 * debugfs_use_file_start - mark the beginning of file data access
 * @dentry: the dentry object whose data is being accessed.
 * @srcu_idx: a pointer to some memory to store a SRCU index in.
 *
 * Up to a matching call to debugfs_use_file_finish(), any
 * successive call into the file removing functions debugfs_remove()
 * and debugfs_remove_recursive() will block. Since associated private
 * file data may only get freed after a successful return of any of
 * the removal functions, you may safely access it after a successful
 * call to debugfs_use_file_start() without worrying about
 * lifetime issues.
 *
 * If -%EIO is returned, the file has already been removed and thus,
 * it is not safe to access any of its data. If, on the other hand,
 * it is allowed to access the file data, zero is returned.
 *
 * Regardless of the return code, any call to
 * debugfs_use_file_start() must be followed by a matching call
 * to debugfs_use_file_finish().
 */
int debugfs_use_file_start(const struct dentry *dentry, int *srcu_idx)
	__acquires(&debugfs_srcu)
{
	*srcu_idx = srcu_read_lock(&debugfs_srcu);
	barrier();
	if (d_unlinked(dentry))
		return -EIO;
	return 0;
}
EXPORT_SYMBOL_GPL(debugfs_use_file_start);

/**
 * debugfs_use_file_finish - mark the end of file data access
 * @srcu_idx: the SRCU index "created" by a former call to
 *            debugfs_use_file_start().
 *
 * Allow any ongoing concurrent call into debugfs_remove() or
 * debugfs_remove_recursive() blocked by a former call to
 * debugfs_use_file_start() to proceed and return to its caller.
 */
void debugfs_use_file_finish(int srcu_idx) __releases(&debugfs_srcu)
{
	srcu_read_unlock(&debugfs_srcu, srcu_idx);
}
EXPORT_SYMBOL_GPL(debugfs_use_file_finish);

#define F_DENTRY(filp) ((filp)->f_path.dentry)

static int open_proxy_open(struct inode *inode, struct file *filp)
{
	const struct dentry *dentry = F_DENTRY(filp);
	const struct file_operations *real_fops = NULL;
	int srcu_idx, r;

	r = debugfs_use_file_start(dentry, &srcu_idx);
	if (r) {
		r = -ENOENT;
		goto out;
	}

	real_fops = debugfs_real_fops(filp);
	real_fops = fops_get(real_fops);
	if (!real_fops) {
		/* Huh? Module did not clean up after itself at exit? */
		WARN(1, "debugfs file owner did not clean up at exit: %pd",
			dentry);
		r = -ENXIO;
		goto out;
	}
	replace_fops(filp, real_fops);

	if (real_fops->open)
		r = real_fops->open(inode, filp);

out:
	debugfs_use_file_finish(srcu_idx);
	return r;
}

const struct file_operations debugfs_open_proxy_file_operations = {
	.open = open_proxy_open,
};

#define PROTO(args...) args
#define ARGS(args...) args

#define FULL_PROXY_FUNC(name, ret_type, filp, proto, args)		\
static ret_type full_proxy_ ## name(proto)				\
{									\
	const struct dentry *dentry = F_DENTRY(filp);			\
	const struct file_operations *real_fops =			\
		debugfs_real_fops(filp);				\
	int srcu_idx;							\
	ret_type r;							\
									\
	r = debugfs_use_file_start(dentry, &srcu_idx);			\
	if (likely(!r))						\
		r = real_fops->name(args);				\
	debugfs_use_file_finish(srcu_idx);				\
	return r;							\
}

FULL_PROXY_FUNC(llseek, loff_t, filp,
		PROTO(struct file *filp, loff_t offset, int whence),
		ARGS(filp, offset, whence));

FULL_PROXY_FUNC(read, ssize_t, filp,
		PROTO(struct file *filp, char __user *buf, size_t size,
			loff_t *ppos),
		ARGS(filp, buf, size, ppos));

FULL_PROXY_FUNC(write, ssize_t, filp,
		PROTO(struct file *filp, const char __user *buf, size_t size,
			loff_t *ppos),
		ARGS(filp, buf, size, ppos));

FULL_PROXY_FUNC(unlocked_ioctl, long, filp,
		PROTO(struct file *filp, unsigned int cmd, unsigned long arg),
		ARGS(filp, cmd, arg));

static unsigned int full_proxy_poll(struct file *filp,
				struct poll_table_struct *wait)
{
	const struct dentry *dentry = F_DENTRY(filp);
	const struct file_operations *real_fops = debugfs_real_fops(filp);
	int srcu_idx;
	unsigned int r = 0;

	if (debugfs_use_file_start(dentry, &srcu_idx)) {
		debugfs_use_file_finish(srcu_idx);
		return POLLHUP;
	}

	r = real_fops->poll(filp, wait);
	debugfs_use_file_finish(srcu_idx);
	return r;
}

static int full_proxy_release(struct inode *inode, struct file *filp)
{
	const struct dentry *dentry = F_DENTRY(filp);
	const struct file_operations *real_fops = debugfs_real_fops(filp);
	const struct file_operations *proxy_fops = filp->f_op;
	int r = 0;

	/*
	 * We must not protect this against removal races here: the
	 * original releaser should be called unconditionally in order
	 * not to leak any resources. Releasers must not assume that
	 * ->i_private is still being meaningful here.
	 */
	if (real_fops->release)
		r = real_fops->release(inode, filp);

	replace_fops(filp, d_inode(dentry)->i_fop);
	kfree((void *)proxy_fops);
	fops_put(real_fops);
	return r;
}

static void __full_proxy_fops_init(struct file_operations *proxy_fops,
				const struct file_operations *real_fops)
{
	proxy_fops->release = full_proxy_release;
	if (real_fops->llseek)
		proxy_fops->llseek = full_proxy_llseek;
	if (real_fops->read)
		proxy_fops->read = full_proxy_read;
	if (real_fops->write)
		proxy_fops->write = full_proxy_write;
	if (real_fops->poll)
		proxy_fops->poll = full_proxy_poll;
	if (real_fops->unlocked_ioctl)
		proxy_fops->unlocked_ioctl = full_proxy_unlocked_ioctl;
}

static int full_proxy_open(struct inode *inode, struct file *filp)
{
	const struct dentry *dentry = F_DENTRY(filp);
	const struct file_operations *real_fops = NULL;
	struct file_operations *proxy_fops = NULL;
	int srcu_idx, r;

	r = debugfs_use_file_start(dentry, &srcu_idx);
	if (r) {
		r = -ENOENT;
		goto out;
	}

	real_fops = debugfs_real_fops(filp);
	real_fops = fops_get(real_fops);
	if (!real_fops) {
		/* Huh? Module did not cleanup after itself at exit? */
		WARN(1, "debugfs file owner did not clean up at exit: %pd",
			dentry);
		r = -ENXIO;
		goto out;
	}

	proxy_fops = kzalloc(sizeof(*proxy_fops), GFP_KERNEL);
	if (!proxy_fops) {
		r = -ENOMEM;
		goto free_proxy;
	}
	__full_proxy_fops_init(proxy_fops, real_fops);
	replace_fops(filp, proxy_fops);

	if (real_fops->open) {
		r = real_fops->open(inode, filp);
		if (r) {
			replace_fops(filp, d_inode(dentry)->i_fop);
			goto free_proxy;
		} else if (filp->f_op != proxy_fops) {
			/* No protection against file removal anymore. */
			WARN(1, "debugfs file owner replaced proxy fops: %pd",
				dentry);
			goto free_proxy;
		}
	}

	goto out;
free_proxy:
	kfree(proxy_fops);
	fops_put(real_fops);
out:
	debugfs_use_file_finish(srcu_idx);
	return r;
}

const struct file_operations debugfs_full_proxy_file_operations = {
	.open = full_proxy_open,
};

ssize_t debugfs_attr_read(struct file *file, char __user *buf,
			size_t len, loff_t *ppos)
{
	ssize_t ret;
	int srcu_idx;

	ret = debugfs_use_file_start(F_DENTRY(file), &srcu_idx);
	if (likely(!ret))
		ret = simple_attr_read(file, buf, len, ppos);
	debugfs_use_file_finish(srcu_idx);
	return ret;
}
EXPORT_SYMBOL_GPL(debugfs_attr_read);

ssize_t debugfs_attr_write(struct file *file, const char __user *buf,
			 size_t len, loff_t *ppos)
{
	ssize_t ret;
	int srcu_idx;

	ret = debugfs_use_file_start(F_DENTRY(file), &srcu_idx);
	if (likely(!ret))
		ret = simple_attr_write(file, buf, len, ppos);
	debugfs_use_file_finish(srcu_idx);
	return ret;
}
EXPORT_SYMBOL_GPL(debugfs_attr_write);

static struct dentry *debugfs_create_mode_unsafe(const char *name, umode_t mode,
					struct dentry *parent, void *value,
					const struct file_operations *fops,
					const struct file_operations *fops_ro,
					const struct file_operations *fops_wo)
{
	/* if there are no write bits set, make read only */
	if (!(mode & S_IWUGO))
		return debugfs_create_file_unsafe(name, mode, parent, value,
						fops_ro);
	/* if there are no read bits set, make write only */
	if (!(mode & S_IRUGO))
		return debugfs_create_file_unsafe(name, mode, parent, value,
						fops_wo);

	return debugfs_create_file_unsafe(name, mode, parent, value, fops);
}

static int debugfs_u8_set(void *data, u64 val)
{
	*(u8 *)data = val;
	return 0;
}
static int debugfs_u8_get(void *data, u64 *val)
{
	*val = *(u8 *)data;
	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(fops_u8, debugfs_u8_get, debugfs_u8_set, "%llu\n");
DEFINE_DEBUGFS_ATTRIBUTE(fops_u8_ro, debugfs_u8_get, NULL, "%llu\n");
DEFINE_DEBUGFS_ATTRIBUTE(fops_u8_wo, NULL, debugfs_u8_set, "%llu\n");

/**
 * debugfs_create_u8 - create a debugfs file that is used to read and write an unsigned 8-bit value
 * @name: a pointer to a string containing the name of the file to create.
 * @mode: the permission that the file should have
 * @parent: a pointer to the parent dentry for this file.  This should be a
 *          directory dentry if set.  If this parameter is %NULL, then the
 *          file will be created in the root of the debugfs filesystem.
 * @value: a pointer to the variable that the file should read to and write
 *         from.
 *
 * This function creates a file in debugfs with the given name that
 * contains the value of the variable @value.  If the @mode variable is so
 * set, it can be read from, and written to.
 *
 * This function will return a pointer to a dentry if it succeeds.  This
 * pointer must be passed to the debugfs_remove() function when the file is
 * to be removed (no automatic cleanup happens if your module is unloaded,
 * you are responsible here.)  If an error occurs, %NULL will be returned.
 *
 * If debugfs is not enabled in the kernel, the value -%ENODEV will be
 * returned.  It is not wise to check for this value, but rather, check for
 * %NULL or !%NULL instead as to eliminate the need for #ifdef in the calling
 * code.
 */
struct dentry *debugfs_create_u8(const char *name, umode_t mode,
				 struct dentry *parent, u8 *value)
{
	return debugfs_create_mode_unsafe(name, mode, parent, value, &fops_u8,
				   &fops_u8_ro, &fops_u8_wo);
}
EXPORT_SYMBOL_GPL(debugfs_create_u8);

static int debugfs_u16_set(void *data, u64 val)
{
	*(u16 *)data = val;
	return 0;
}
static int debugfs_u16_get(void *data, u64 *val)
{
	*val = *(u16 *)data;
	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(fops_u16, debugfs_u16_get, debugfs_u16_set, "%llu\n");
DEFINE_DEBUGFS_ATTRIBUTE(fops_u16_ro, debugfs_u16_get, NULL, "%llu\n");
DEFINE_DEBUGFS_ATTRIBUTE(fops_u16_wo, NULL, debugfs_u16_set, "%llu\n");

/**
 * debugfs_create_u16 - create a debugfs file that is used to read and write an unsigned 16-bit value
 * @name: a pointer to a string containing the name of the file to create.
 * @mode: the permission that the file should have
 * @parent: a pointer to the parent dentry for this file.  This should be a
 *          directory dentry if set.  If this parameter is %NULL, then the
 *          file will be created in the root of the debugfs filesystem.
 * @value: a pointer to the variable that the file should read to and write
 *         from.
 *
 * This function creates a file in debugfs with the given name that
 * contains the value of the variable @value.  If the @mode variable is so
 * set, it can be read from, and written to.
 *
 * This function will return a pointer to a dentry if it succeeds.  This
 * pointer must be passed to the debugfs_remove() function when the file is
 * to be removed (no automatic cleanup happens if your module is unloaded,
 * you are responsible here.)  If an error occurs, %NULL will be returned.
 *
 * If debugfs is not enabled in the kernel, the value -%ENODEV will be
 * returned.  It is not wise to check for this value, but rather, check for
 * %NULL or !%NULL instead as to eliminate the need for #ifdef in the calling
 * code.
 */
struct dentry *debugfs_create_u16(const char *name, umode_t mode,
				  struct dentry *parent, u16 *value)
{
	return debugfs_create_mode_unsafe(name, mode, parent, value, &fops_u16,
				   &fops_u16_ro, &fops_u16_wo);
}
EXPORT_SYMBOL_GPL(debugfs_create_u16);

static int debugfs_u32_set(void *data, u64 val)
{
	*(u32 *)data = val;
	return 0;
}
static int debugfs_u32_get(void *data, u64 *val)
{
	*val = *(u32 *)data;
	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(fops_u32, debugfs_u32_get, debugfs_u32_set, "%llu\n");
DEFINE_DEBUGFS_ATTRIBUTE(fops_u32_ro, debugfs_u32_get, NULL, "%llu\n");
DEFINE_DEBUGFS_ATTRIBUTE(fops_u32_wo, NULL, debugfs_u32_set, "%llu\n");

/**
 * debugfs_create_u32 - create a debugfs file that is used to read and write an unsigned 32-bit value
 * @name: a pointer to a string containing the name of the file to create.
 * @mode: the permission that the file should have
 * @parent: a pointer to the parent dentry for this file.  This should be a
 *          directory dentry if set.  If this parameter is %NULL, then the
 *          file will be created in the root of the debugfs filesystem.
 * @value: a pointer to the variable that the file should read to and write
 *         from.
 *
 * This function creates a file in debugfs with the given name that
 * contains the value of the variable @value.  If the @mode variable is so
 * set, it can be read from, and written to.
 *
 * This function will return a pointer to a dentry if it succeeds.  This
 * pointer must be passed to the debugfs_remove() function when the file is
 * to be removed (no automatic cleanup happens if your module is unloaded,
 * you are responsible here.)  If an error occurs, %NULL will be returned.
 *
 * If debugfs is not enabled in the kernel, the value -%ENODEV will be
 * returned.  It is not wise to check for this value, but rather, check for
 * %NULL or !%NULL instead as to eliminate the need for #ifdef in the calling
 * code.
 */
struct dentry *debugfs_create_u32(const char *name, umode_t mode,
				 struct dentry *parent, u32 *value)
{
	return debugfs_create_mode_unsafe(name, mode, parent, value, &fops_u32,
				   &fops_u32_ro, &fops_u32_wo);
}
EXPORT_SYMBOL_GPL(debugfs_create_u32);

static int debugfs_u64_set(void *data, u64 val)
{
	*(u64 *)data = val;
	return 0;
}

static int debugfs_u64_get(void *data, u64 *val)
{
	*val = *(u64 *)data;
	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(fops_u64, debugfs_u64_get, debugfs_u64_set, "%llu\n");
DEFINE_DEBUGFS_ATTRIBUTE(fops_u64_ro, debugfs_u64_get, NULL, "%llu\n");
DEFINE_DEBUGFS_ATTRIBUTE(fops_u64_wo, NULL, debugfs_u64_set, "%llu\n");

/**
 * debugfs_create_u64 - create a debugfs file that is used to read and write an unsigned 64-bit value
 * @name: a pointer to a string containing the name of the file to create.
 * @mode: the permission that the file should have
 * @parent: a pointer to the parent dentry for this file.  This should be a
 *          directory dentry if set.  If this parameter is %NULL, then the
 *          file will be created in the root of the debugfs filesystem.
 * @value: a pointer to the variable that the file should read to and write
 *         from.
 *
 * This function creates a file in debugfs with the given name that
 * contains the value of the variable @value.  If the @mode variable is so
 * set, it can be read from, and written to.
 *
 * This function will return a pointer to a dentry if it succeeds.  This
 * pointer must be passed to the debugfs_remove() function when the file is
 * to be removed (no automatic cleanup happens if your module is unloaded,
 * you are responsible here.)  If an error occurs, %NULL will be returned.
 *
 * If debugfs is not enabled in the kernel, the value -%ENODEV will be
 * returned.  It is not wise to check for this value, but rather, check for
 * %NULL or !%NULL instead as to eliminate the need for #ifdef in the calling
 * code.
 */
struct dentry *debugfs_create_u64(const char *name, umode_t mode,
				 struct dentry *parent, u64 *value)
{
	return debugfs_create_mode_unsafe(name, mode, parent, value, &fops_u64,
				   &fops_u64_ro, &fops_u64_wo);
}
EXPORT_SYMBOL_GPL(debugfs_create_u64);

static int debugfs_ulong_set(void *data, u64 val)
{
	*(unsigned long *)data = val;
	return 0;
}

static int debugfs_ulong_get(void *data, u64 *val)
{
	*val = *(unsigned long *)data;
	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(fops_ulong, debugfs_ulong_get, debugfs_ulong_set,
			"%llu\n");
DEFINE_DEBUGFS_ATTRIBUTE(fops_ulong_ro, debugfs_ulong_get, NULL, "%llu\n");
DEFINE_DEBUGFS_ATTRIBUTE(fops_ulong_wo, NULL, debugfs_ulong_set, "%llu\n");

/**
 * debugfs_create_ulong - create a debugfs file that is used to read and write
 * an unsigned long value.
 * @name: a pointer to a string containing the name of the file to create.
 * @mode: the permission that the file should have
 * @parent: a pointer to the parent dentry for this file.  This should be a
 *          directory dentry if set.  If this parameter is %NULL, then the
 *          file will be created in the root of the debugfs filesystem.
 * @value: a pointer to the variable that the file should read to and write
 *         from.
 *
 * This function creates a file in debugfs with the given name that
 * contains the value of the variable @value.  If the @mode variable is so
 * set, it can be read from, and written to.
 *
 * This function will return a pointer to a dentry if it succeeds.  This
 * pointer must be passed to the debugfs_remove() function when the file is
 * to be removed (no automatic cleanup happens if your module is unloaded,
 * you are responsible here.)  If an error occurs, %NULL will be returned.
 *
 * If debugfs is not enabled in the kernel, the value -%ENODEV will be
 * returned.  It is not wise to check for this value, but rather, check for
 * %NULL or !%NULL instead as to eliminate the need for #ifdef in the calling
 * code.
 */
struct dentry *debugfs_create_ulong(const char *name, umode_t mode,
				    struct dentry *parent, unsigned long *value)
{
	return debugfs_create_mode_unsafe(name, mode, parent, value,
					&fops_ulong, &fops_ulong_ro,
					&fops_ulong_wo);
}
EXPORT_SYMBOL_GPL(debugfs_create_ulong);

DEFINE_DEBUGFS_ATTRIBUTE(fops_x8, debugfs_u8_get, debugfs_u8_set, "0x%02llx\n");
DEFINE_DEBUGFS_ATTRIBUTE(fops_x8_ro, debugfs_u8_get, NULL, "0x%02llx\n");
DEFINE_DEBUGFS_ATTRIBUTE(fops_x8_wo, NULL, debugfs_u8_set, "0x%02llx\n");

DEFINE_DEBUGFS_ATTRIBUTE(fops_x16, debugfs_u16_get, debugfs_u16_set,
			"0x%04llx\n");
DEFINE_DEBUGFS_ATTRIBUTE(fops_x16_ro, debugfs_u16_get, NULL, "0x%04llx\n");
DEFINE_DEBUGFS_ATTRIBUTE(fops_x16_wo, NULL, debugfs_u16_set, "0x%04llx\n");

DEFINE_DEBUGFS_ATTRIBUTE(fops_x32, debugfs_u32_get, debugfs_u32_set,
			"0x%08llx\n");
DEFINE_DEBUGFS_ATTRIBUTE(fops_x32_ro, debugfs_u32_get, NULL, "0x%08llx\n");
DEFINE_DEBUGFS_ATTRIBUTE(fops_x32_wo, NULL, debugfs_u32_set, "0x%08llx\n");

DEFINE_DEBUGFS_ATTRIBUTE(fops_x64, debugfs_u64_get, debugfs_u64_set,
			"0x%016llx\n");
DEFINE_DEBUGFS_ATTRIBUTE(fops_x64_ro, debugfs_u64_get, NULL, "0x%016llx\n");
DEFINE_DEBUGFS_ATTRIBUTE(fops_x64_wo, NULL, debugfs_u64_set, "0x%016llx\n");

/*
 * debugfs_create_x{8,16,32,64} - create a debugfs file that is used to read and write an unsigned {8,16,32,64}-bit value
 *
 * These functions are exactly the same as the above functions (but use a hex
 * output for the decimal challenged). For details look at the above unsigned
 * decimal functions.
 */

/**
 * debugfs_create_x8 - create a debugfs file that is used to read and write an unsigned 8-bit value
 * @name: a pointer to a string containing the name of the file to create.
 * @mode: the permission that the file should have
 * @parent: a pointer to the parent dentry for this file.  This should be a
 *          directory dentry if set.  If this parameter is %NULL, then the
 *          file will be created in the root of the debugfs filesystem.
 * @value: a pointer to the variable that the file should read to and write
 *         from.
 */
struct dentry *debugfs_create_x8(const char *name, umode_t mode,
				 struct dentry *parent, u8 *value)
{
	return debugfs_create_mode_unsafe(name, mode, parent, value, &fops_x8,
				   &fops_x8_ro, &fops_x8_wo);
}
EXPORT_SYMBOL_GPL(debugfs_create_x8);

/**
 * debugfs_create_x16 - create a debugfs file that is used to read and write an unsigned 16-bit value
 * @name: a pointer to a string containing the name of the file to create.
 * @mode: the permission that the file should have
 * @parent: a pointer to the parent dentry for this file.  This should be a
 *          directory dentry if set.  If this parameter is %NULL, then the
 *          file will be created in the root of the debugfs filesystem.
 * @value: a pointer to the variable that the file should read to and write
 *         from.
 */
struct dentry *debugfs_create_x16(const char *name, umode_t mode,
				 struct dentry *parent, u16 *value)
{
	return debugfs_create_mode_unsafe(name, mode, parent, value, &fops_x16,
				   &fops_x16_ro, &fops_x16_wo);
}
EXPORT_SYMBOL_GPL(debugfs_create_x16);

/**
 * debugfs_create_x32 - create a debugfs file that is used to read and write an unsigned 32-bit value
 * @name: a pointer to a string containing the name of the file to create.
 * @mode: the permission that the file should have
 * @parent: a pointer to the parent dentry for this file.  This should be a
 *          directory dentry if set.  If this parameter is %NULL, then the
 *          file will be created in the root of the debugfs filesystem.
 * @value: a pointer to the variable that the file should read to and write
 *         from.
 */
struct dentry *debugfs_create_x32(const char *name, umode_t mode,
				 struct dentry *parent, u32 *value)
{
	return debugfs_create_mode_unsafe(name, mode, parent, value, &fops_x32,
				   &fops_x32_ro, &fops_x32_wo);
}
EXPORT_SYMBOL_GPL(debugfs_create_x32);

/**
 * debugfs_create_x64 - create a debugfs file that is used to read and write an unsigned 64-bit value
 * @name: a pointer to a string containing the name of the file to create.
 * @mode: the permission that the file should have
 * @parent: a pointer to the parent dentry for this file.  This should be a
 *          directory dentry if set.  If this parameter is %NULL, then the
 *          file will be created in the root of the debugfs filesystem.
 * @value: a pointer to the variable that the file should read to and write
 *         from.
 */
struct dentry *debugfs_create_x64(const char *name, umode_t mode,
				 struct dentry *parent, u64 *value)
{
	return debugfs_create_mode_unsafe(name, mode, parent, value, &fops_x64,
				   &fops_x64_ro, &fops_x64_wo);
}
EXPORT_SYMBOL_GPL(debugfs_create_x64);


static int debugfs_size_t_set(void *data, u64 val)
{
	*(size_t *)data = val;
	return 0;
}
static int debugfs_size_t_get(void *data, u64 *val)
{
	*val = *(size_t *)data;
	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(fops_size_t, debugfs_size_t_get, debugfs_size_t_set,
			"%llu\n"); /* %llu and %zu are more or less the same */
DEFINE_DEBUGFS_ATTRIBUTE(fops_size_t_ro, debugfs_size_t_get, NULL, "%llu\n");
DEFINE_DEBUGFS_ATTRIBUTE(fops_size_t_wo, NULL, debugfs_size_t_set, "%llu\n");

/**
 * debugfs_create_size_t - create a debugfs file that is used to read and write an size_t value
 * @name: a pointer to a string containing the name of the file to create.
 * @mode: the permission that the file should have
 * @parent: a pointer to the parent dentry for this file.  This should be a
 *          directory dentry if set.  If this parameter is %NULL, then the
 *          file will be created in the root of the debugfs filesystem.
 * @value: a pointer to the variable that the file should read to and write
 *         from.
 */
struct dentry *debugfs_create_size_t(const char *name, umode_t mode,
				     struct dentry *parent, size_t *value)
{
	return debugfs_create_mode_unsafe(name, mode, parent, value,
					&fops_size_t, &fops_size_t_ro,
					&fops_size_t_wo);
}
EXPORT_SYMBOL_GPL(debugfs_create_size_t);

static int debugfs_atomic_t_set(void *data, u64 val)
{
	atomic_set((atomic_t *)data, val);
	return 0;
}
static int debugfs_atomic_t_get(void *data, u64 *val)
{
	*val = atomic_read((atomic_t *)data);
	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(fops_atomic_t, debugfs_atomic_t_get,
			debugfs_atomic_t_set, "%lld\n");
DEFINE_DEBUGFS_ATTRIBUTE(fops_atomic_t_ro, debugfs_atomic_t_get, NULL,
			"%lld\n");
DEFINE_DEBUGFS_ATTRIBUTE(fops_atomic_t_wo, NULL, debugfs_atomic_t_set,
			"%lld\n");

/**
 * debugfs_create_atomic_t - create a debugfs file that is used to read and
 * write an atomic_t value
 * @name: a pointer to a string containing the name of the file to create.
 * @mode: the permission that the file should have
 * @parent: a pointer to the parent dentry for this file.  This should be a
 *          directory dentry if set.  If this parameter is %NULL, then the
 *          file will be created in the root of the debugfs filesystem.
 * @value: a pointer to the variable that the file should read to and write
 *         from.
 */
struct dentry *debugfs_create_atomic_t(const char *name, umode_t mode,
				 struct dentry *parent, atomic_t *value)
{
	return debugfs_create_mode_unsafe(name, mode, parent, value,
					&fops_atomic_t, &fops_atomic_t_ro,
					&fops_atomic_t_wo);
}
EXPORT_SYMBOL_GPL(debugfs_create_atomic_t);

ssize_t debugfs_read_file_bool(struct file *file, char __user *user_buf,
			       size_t count, loff_t *ppos)
{
	char buf[3];
	bool val;
	int r, srcu_idx;

	r = debugfs_use_file_start(F_DENTRY(file), &srcu_idx);
	if (likely(!r))
		val = *(bool *)file->private_data;
	debugfs_use_file_finish(srcu_idx);
	if (r)
		return r;

	if (val)
		buf[0] = 'Y';
	else
		buf[0] = 'N';
	buf[1] = '\n';
	buf[2] = 0x00;
	return simple_read_from_buffer(user_buf, count, ppos, buf, 2);
}
EXPORT_SYMBOL_GPL(debugfs_read_file_bool);

ssize_t debugfs_write_file_bool(struct file *file, const char __user *user_buf,
				size_t count, loff_t *ppos)
{
	char buf[32];
	size_t buf_size;
	bool bv;
	int r, srcu_idx;
	bool *val = file->private_data;

	buf_size = min(count, (sizeof(buf)-1));
	if (copy_from_user(buf, user_buf, buf_size))
		return -EFAULT;

	buf[buf_size] = '\0';
	if (strtobool(buf, &bv) == 0) {
		r = debugfs_use_file_start(F_DENTRY(file), &srcu_idx);
		if (likely(!r))
			*val = bv;
		debugfs_use_file_finish(srcu_idx);
		if (r)
			return r;
	}

	return count;
}
EXPORT_SYMBOL_GPL(debugfs_write_file_bool);

static const struct file_operations fops_bool = {
	.read =		debugfs_read_file_bool,
	.write =	debugfs_write_file_bool,
	.open =		simple_open,
	.llseek =	default_llseek,
};

static const struct file_operations fops_bool_ro = {
	.read =		debugfs_read_file_bool,
	.open =		simple_open,
	.llseek =	default_llseek,
};

static const struct file_operations fops_bool_wo = {
	.write =	debugfs_write_file_bool,
	.open =		simple_open,
	.llseek =	default_llseek,
};

/**
 * debugfs_create_bool - create a debugfs file that is used to read and write a boolean value
 * @name: a pointer to a string containing the name of the file to create.
 * @mode: the permission that the file should have
 * @parent: a pointer to the parent dentry for this file.  This should be a
 *          directory dentry if set.  If this parameter is %NULL, then the
 *          file will be created in the root of the debugfs filesystem.
 * @value: a pointer to the variable that the file should read to and write
 *         from.
 *
 * This function creates a file in debugfs with the given name that
 * contains the value of the variable @value.  If the @mode variable is so
 * set, it can be read from, and written to.
 *
 * This function will return a pointer to a dentry if it succeeds.  This
 * pointer must be passed to the debugfs_remove() function when the file is
 * to be removed (no automatic cleanup happens if your module is unloaded,
 * you are responsible here.)  If an error occurs, %NULL will be returned.
 *
 * If debugfs is not enabled in the kernel, the value -%ENODEV will be
 * returned.  It is not wise to check for this value, but rather, check for
 * %NULL or !%NULL instead as to eliminate the need for #ifdef in the calling
 * code.
 */
struct dentry *debugfs_create_bool(const char *name, umode_t mode,
				   struct dentry *parent, bool *value)
{
	return debugfs_create_mode_unsafe(name, mode, parent, value, &fops_bool,
				   &fops_bool_ro, &fops_bool_wo);
}
EXPORT_SYMBOL_GPL(debugfs_create_bool);

static ssize_t read_file_blob(struct file *file, char __user *user_buf,
			      size_t count, loff_t *ppos)
{
	struct debugfs_blob_wrapper *blob = file->private_data;
	ssize_t r;
	int srcu_idx;

	r = debugfs_use_file_start(F_DENTRY(file), &srcu_idx);
	if (likely(!r))
		r = simple_read_from_buffer(user_buf, count, ppos, blob->data,
					blob->size);
	debugfs_use_file_finish(srcu_idx);
	return r;
}

static const struct file_operations fops_blob = {
	.read =		read_file_blob,
	.open =		simple_open,
	.llseek =	default_llseek,
};

/**
 * debugfs_create_blob - create a debugfs file that is used to read a binary blob
 * @name: a pointer to a string containing the name of the file to create.
 * @mode: the permission that the file should have
 * @parent: a pointer to the parent dentry for this file.  This should be a
 *          directory dentry if set.  If this parameter is %NULL, then the
 *          file will be created in the root of the debugfs filesystem.
 * @blob: a pointer to a struct debugfs_blob_wrapper which contains a pointer
 *        to the blob data and the size of the data.
 *
 * This function creates a file in debugfs with the given name that exports
 * @blob->data as a binary blob. If the @mode variable is so set it can be
 * read from. Writing is not supported.
 *
 * This function will return a pointer to a dentry if it succeeds.  This
 * pointer must be passed to the debugfs_remove() function when the file is
 * to be removed (no automatic cleanup happens if your module is unloaded,
 * you are responsible here.)  If an error occurs, %NULL will be returned.
 *
 * If debugfs is not enabled in the kernel, the value -%ENODEV will be
 * returned.  It is not wise to check for this value, but rather, check for
 * %NULL or !%NULL instead as to eliminate the need for #ifdef in the calling
 * code.
 */
struct dentry *debugfs_create_blob(const char *name, umode_t mode,
				   struct dentry *parent,
				   struct debugfs_blob_wrapper *blob)
{
	return debugfs_create_file_unsafe(name, mode, parent, blob, &fops_blob);
}
EXPORT_SYMBOL_GPL(debugfs_create_blob);

struct array_data {
	void *array;
	u32 elements;
};

static size_t u32_format_array(char *buf, size_t bufsize,
			       u32 *array, int array_size)
{
	size_t ret = 0;

	while (--array_size >= 0) {
		size_t len;
		char term = array_size ? ' ' : '\n';

		len = snprintf(buf, bufsize, "%u%c", *array++, term);
		ret += len;

		buf += len;
		bufsize -= len;
	}
	return ret;
}

static int u32_array_open(struct inode *inode, struct file *file)
{
	struct array_data *data = inode->i_private;
	int size, elements = data->elements;
	char *buf;

	/*
	 * Max size:
	 *  - 10 digits + ' '/'\n' = 11 bytes per number
	 *  - terminating NUL character
	 */
	size = elements*11;
	buf = kmalloc(size+1, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;
	buf[size] = 0;

	file->private_data = buf;
	u32_format_array(buf, size, data->array, data->elements);

	return nonseekable_open(inode, file);
}

static ssize_t u32_array_read(struct file *file, char __user *buf, size_t len,
			      loff_t *ppos)
{
	size_t size = strlen(file->private_data);

	return simple_read_from_buffer(buf, len, ppos,
					file->private_data, size);
}

static int u32_array_release(struct inode *inode, struct file *file)
{
	kfree(file->private_data);

	return 0;
}

static const struct file_operations u32_array_fops = {
	.owner	 = THIS_MODULE,
	.open	 = u32_array_open,
	.release = u32_array_release,
	.read	 = u32_array_read,
	.llseek  = no_llseek,
};

/**
 * debugfs_create_u32_array - create a debugfs file that is used to read u32
 * array.
 * @name: a pointer to a string containing the name of the file to create.
 * @mode: the permission that the file should have.
 * @parent: a pointer to the parent dentry for this file.  This should be a
 *          directory dentry if set.  If this parameter is %NULL, then the
 *          file will be created in the root of the debugfs filesystem.
 * @array: u32 array that provides data.
 * @elements: total number of elements in the array.
 *
 * This function creates a file in debugfs with the given name that exports
 * @array as data. If the @mode variable is so set it can be read from.
 * Writing is not supported. Seek within the file is also not supported.
 * Once array is created its size can not be changed.
 *
 * The function returns a pointer to dentry on success. If debugfs is not
 * enabled in the kernel, the value -%ENODEV will be returned.
 */
struct dentry *debugfs_create_u32_array(const char *name, umode_t mode,
					    struct dentry *parent,
					    u32 *array, u32 elements)
{
	struct array_data *data = kmalloc(sizeof(*data), GFP_KERNEL);

	if (data == NULL)
		return NULL;

	data->array = array;
	data->elements = elements;

	return debugfs_create_file_unsafe(name, mode, parent, data,
					&u32_array_fops);
}
EXPORT_SYMBOL_GPL(debugfs_create_u32_array);

#ifdef CONFIG_HAS_IOMEM

/*
 * The regset32 stuff is used to print 32-bit registers using the
 * seq_file utilities. We offer printing a register set in an already-opened
 * sequential file or create a debugfs file that only prints a regset32.
 */

/**
 * debugfs_print_regs32 - use seq_print to describe a set of registers
 * @s: the seq_file structure being used to generate output
 * @regs: an array if struct debugfs_reg32 structures
 * @nregs: the length of the above array
 * @base: the base address to be used in reading the registers
 * @prefix: a string to be prefixed to every output line
 *
 * This function outputs a text block describing the current values of
 * some 32-bit hardware registers. It is meant to be used within debugfs
 * files based on seq_file that need to show registers, intermixed with other
 * information. The prefix argument may be used to specify a leading string,
 * because some peripherals have several blocks of identical registers,
 * for example configuration of dma channels
 */
void debugfs_print_regs32(struct seq_file *s, const struct debugfs_reg32 *regs,
			  int nregs, void __iomem *base, char *prefix)
{
	int i;

	for (i = 0; i < nregs; i++, regs++) {
		if (prefix)
			seq_printf(s, "%s", prefix);
		seq_printf(s, "%s = 0x%08x\n", regs->name,
			   readl(base + regs->offset));
		if (seq_has_overflowed(s))
			break;
	}
}
EXPORT_SYMBOL_GPL(debugfs_print_regs32);

static int debugfs_show_regset32(struct seq_file *s, void *data)
{
	struct debugfs_regset32 *regset = s->private;

	debugfs_print_regs32(s, regset->regs, regset->nregs, regset->base, "");
	return 0;
}

static int debugfs_open_regset32(struct inode *inode, struct file *file)
{
	return single_open(file, debugfs_show_regset32, inode->i_private);
}

static const struct file_operations fops_regset32 = {
	.open =		debugfs_open_regset32,
	.read =		seq_read,
	.llseek =	seq_lseek,
	.release =	single_release,
};

/**
 * debugfs_create_regset32 - create a debugfs file that returns register values
 * @name: a pointer to a string containing the name of the file to create.
 * @mode: the permission that the file should have
 * @parent: a pointer to the parent dentry for this file.  This should be a
 *          directory dentry if set.  If this parameter is %NULL, then the
 *          file will be created in the root of the debugfs filesystem.
 * @regset: a pointer to a struct debugfs_regset32, which contains a pointer
 *          to an array of register definitions, the array size and the base
 *          address where the register bank is to be found.
 *
 * This function creates a file in debugfs with the given name that reports
 * the names and values of a set of 32-bit registers. If the @mode variable
 * is so set it can be read from. Writing is not supported.
 *
 * This function will return a pointer to a dentry if it succeeds.  This
 * pointer must be passed to the debugfs_remove() function when the file is
 * to be removed (no automatic cleanup happens if your module is unloaded,
 * you are responsible here.)  If an error occurs, %NULL will be returned.
 *
 * If debugfs is not enabled in the kernel, the value -%ENODEV will be
 * returned.  It is not wise to check for this value, but rather, check for
 * %NULL or !%NULL instead as to eliminate the need for #ifdef in the calling
 * code.
 */
struct dentry *debugfs_create_regset32(const char *name, umode_t mode,
				       struct dentry *parent,
				       struct debugfs_regset32 *regset)
{
	return debugfs_create_file(name, mode, parent, regset, &fops_regset32);
}
EXPORT_SYMBOL_GPL(debugfs_create_regset32);

#endif /* CONFIG_HAS_IOMEM */

struct debugfs_devm_entry {
	int (*read)(struct seq_file *seq, void *data);
	struct device *dev;
};

static int debugfs_devm_entry_open(struct inode *inode, struct file *f)
{
	struct debugfs_devm_entry *entry = inode->i_private;

	return single_open(f, entry->read, entry->dev);
}

static const struct file_operations debugfs_devm_entry_ops = {
	.owner = THIS_MODULE,
	.open = debugfs_devm_entry_open,
	.release = single_release,
	.read = seq_read,
	.llseek = seq_lseek
};

/**
 * debugfs_create_devm_seqfile - create a debugfs file that is bound to device.
 *
 * @dev: device related to this debugfs file.
 * @name: name of the debugfs file.
 * @parent: a pointer to the parent dentry for this file.  This should be a
 *	directory dentry if set.  If this parameter is %NULL, then the
 *	file will be created in the root of the debugfs filesystem.
 * @read_fn: function pointer called to print the seq_file content.
 */
struct dentry *debugfs_create_devm_seqfile(struct device *dev, const char *name,
					   struct dentry *parent,
					   int (*read_fn)(struct seq_file *s,
							  void *data))
{
	struct debugfs_devm_entry *entry;

	if (IS_ERR(parent))
		return ERR_PTR(-ENOENT);

	entry = devm_kzalloc(dev, sizeof(*entry), GFP_KERNEL);
	if (!entry)
		return ERR_PTR(-ENOMEM);

	entry->read = read_fn;
	entry->dev = dev;

	return debugfs_create_file(name, S_IRUGO, parent, entry,
				   &debugfs_devm_entry_ops);
}
EXPORT_SYMBOL_GPL(debugfs_create_devm_seqfile);

