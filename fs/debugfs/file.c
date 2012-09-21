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
#include <linux/namei.h>
#include <linux/debugfs.h>
#include <linux/io.h>
#include <linux/slab.h>

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

const struct file_operations debugfs_file_operations = {
	.read =		default_read_file,
	.write =	default_write_file,
	.open =		simple_open,
	.llseek =	noop_llseek,
};

static void *debugfs_follow_link(struct dentry *dentry, struct nameidata *nd)
{
	nd_set_link(nd, dentry->d_inode->i_private);
	return NULL;
}

const struct inode_operations debugfs_link_operations = {
	.readlink       = generic_readlink,
	.follow_link    = debugfs_follow_link,
};

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
DEFINE_SIMPLE_ATTRIBUTE(fops_u8, debugfs_u8_get, debugfs_u8_set, "%llu\n");
DEFINE_SIMPLE_ATTRIBUTE(fops_u8_ro, debugfs_u8_get, NULL, "%llu\n");
DEFINE_SIMPLE_ATTRIBUTE(fops_u8_wo, NULL, debugfs_u8_set, "%llu\n");

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
	/* if there are no write bits set, make read only */
	if (!(mode & S_IWUGO))
		return debugfs_create_file(name, mode, parent, value, &fops_u8_ro);
	/* if there are no read bits set, make write only */
	if (!(mode & S_IRUGO))
		return debugfs_create_file(name, mode, parent, value, &fops_u8_wo);

	return debugfs_create_file(name, mode, parent, value, &fops_u8);
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
DEFINE_SIMPLE_ATTRIBUTE(fops_u16, debugfs_u16_get, debugfs_u16_set, "%llu\n");
DEFINE_SIMPLE_ATTRIBUTE(fops_u16_ro, debugfs_u16_get, NULL, "%llu\n");
DEFINE_SIMPLE_ATTRIBUTE(fops_u16_wo, NULL, debugfs_u16_set, "%llu\n");

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
	/* if there are no write bits set, make read only */
	if (!(mode & S_IWUGO))
		return debugfs_create_file(name, mode, parent, value, &fops_u16_ro);
	/* if there are no read bits set, make write only */
	if (!(mode & S_IRUGO))
		return debugfs_create_file(name, mode, parent, value, &fops_u16_wo);

	return debugfs_create_file(name, mode, parent, value, &fops_u16);
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
DEFINE_SIMPLE_ATTRIBUTE(fops_u32, debugfs_u32_get, debugfs_u32_set, "%llu\n");
DEFINE_SIMPLE_ATTRIBUTE(fops_u32_ro, debugfs_u32_get, NULL, "%llu\n");
DEFINE_SIMPLE_ATTRIBUTE(fops_u32_wo, NULL, debugfs_u32_set, "%llu\n");

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
	/* if there are no write bits set, make read only */
	if (!(mode & S_IWUGO))
		return debugfs_create_file(name, mode, parent, value, &fops_u32_ro);
	/* if there are no read bits set, make write only */
	if (!(mode & S_IRUGO))
		return debugfs_create_file(name, mode, parent, value, &fops_u32_wo);

	return debugfs_create_file(name, mode, parent, value, &fops_u32);
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
DEFINE_SIMPLE_ATTRIBUTE(fops_u64, debugfs_u64_get, debugfs_u64_set, "%llu\n");
DEFINE_SIMPLE_ATTRIBUTE(fops_u64_ro, debugfs_u64_get, NULL, "%llu\n");
DEFINE_SIMPLE_ATTRIBUTE(fops_u64_wo, NULL, debugfs_u64_set, "%llu\n");

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
	/* if there are no write bits set, make read only */
	if (!(mode & S_IWUGO))
		return debugfs_create_file(name, mode, parent, value, &fops_u64_ro);
	/* if there are no read bits set, make write only */
	if (!(mode & S_IRUGO))
		return debugfs_create_file(name, mode, parent, value, &fops_u64_wo);

	return debugfs_create_file(name, mode, parent, value, &fops_u64);
}
EXPORT_SYMBOL_GPL(debugfs_create_u64);

DEFINE_SIMPLE_ATTRIBUTE(fops_x8, debugfs_u8_get, debugfs_u8_set, "0x%02llx\n");
DEFINE_SIMPLE_ATTRIBUTE(fops_x8_ro, debugfs_u8_get, NULL, "0x%02llx\n");
DEFINE_SIMPLE_ATTRIBUTE(fops_x8_wo, NULL, debugfs_u8_set, "0x%02llx\n");

DEFINE_SIMPLE_ATTRIBUTE(fops_x16, debugfs_u16_get, debugfs_u16_set, "0x%04llx\n");
DEFINE_SIMPLE_ATTRIBUTE(fops_x16_ro, debugfs_u16_get, NULL, "0x%04llx\n");
DEFINE_SIMPLE_ATTRIBUTE(fops_x16_wo, NULL, debugfs_u16_set, "0x%04llx\n");

DEFINE_SIMPLE_ATTRIBUTE(fops_x32, debugfs_u32_get, debugfs_u32_set, "0x%08llx\n");
DEFINE_SIMPLE_ATTRIBUTE(fops_x32_ro, debugfs_u32_get, NULL, "0x%08llx\n");
DEFINE_SIMPLE_ATTRIBUTE(fops_x32_wo, NULL, debugfs_u32_set, "0x%08llx\n");

DEFINE_SIMPLE_ATTRIBUTE(fops_x64, debugfs_u64_get, debugfs_u64_set, "0x%016llx\n");

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
	/* if there are no write bits set, make read only */
	if (!(mode & S_IWUGO))
		return debugfs_create_file(name, mode, parent, value, &fops_x8_ro);
	/* if there are no read bits set, make write only */
	if (!(mode & S_IRUGO))
		return debugfs_create_file(name, mode, parent, value, &fops_x8_wo);

	return debugfs_create_file(name, mode, parent, value, &fops_x8);
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
	/* if there are no write bits set, make read only */
	if (!(mode & S_IWUGO))
		return debugfs_create_file(name, mode, parent, value, &fops_x16_ro);
	/* if there are no read bits set, make write only */
	if (!(mode & S_IRUGO))
		return debugfs_create_file(name, mode, parent, value, &fops_x16_wo);

	return debugfs_create_file(name, mode, parent, value, &fops_x16);
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
	/* if there are no write bits set, make read only */
	if (!(mode & S_IWUGO))
		return debugfs_create_file(name, mode, parent, value, &fops_x32_ro);
	/* if there are no read bits set, make write only */
	if (!(mode & S_IRUGO))
		return debugfs_create_file(name, mode, parent, value, &fops_x32_wo);

	return debugfs_create_file(name, mode, parent, value, &fops_x32);
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
	return debugfs_create_file(name, mode, parent, value, &fops_x64);
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
DEFINE_SIMPLE_ATTRIBUTE(fops_size_t, debugfs_size_t_get, debugfs_size_t_set,
			"%llu\n");	/* %llu and %zu are more or less the same */

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
	return debugfs_create_file(name, mode, parent, value, &fops_size_t);
}
EXPORT_SYMBOL_GPL(debugfs_create_size_t);


static ssize_t read_file_bool(struct file *file, char __user *user_buf,
			      size_t count, loff_t *ppos)
{
	char buf[3];
	u32 *val = file->private_data;
	
	if (*val)
		buf[0] = 'Y';
	else
		buf[0] = 'N';
	buf[1] = '\n';
	buf[2] = 0x00;
	return simple_read_from_buffer(user_buf, count, ppos, buf, 2);
}

static ssize_t write_file_bool(struct file *file, const char __user *user_buf,
			       size_t count, loff_t *ppos)
{
	char buf[32];
	size_t buf_size;
	bool bv;
	u32 *val = file->private_data;

	buf_size = min(count, (sizeof(buf)-1));
	if (copy_from_user(buf, user_buf, buf_size))
		return -EFAULT;

	if (strtobool(buf, &bv) == 0)
		*val = bv;

	return count;
}

static const struct file_operations fops_bool = {
	.read =		read_file_bool,
	.write =	write_file_bool,
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
				   struct dentry *parent, u32 *value)
{
	return debugfs_create_file(name, mode, parent, value, &fops_bool);
}
EXPORT_SYMBOL_GPL(debugfs_create_bool);

static ssize_t read_file_blob(struct file *file, char __user *user_buf,
			      size_t count, loff_t *ppos)
{
	struct debugfs_blob_wrapper *blob = file->private_data;
	return simple_read_from_buffer(user_buf, count, ppos, blob->data,
			blob->size);
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
	return debugfs_create_file(name, mode, parent, blob, &fops_blob);
}
EXPORT_SYMBOL_GPL(debugfs_create_blob);

struct array_data {
	void *array;
	u32 elements;
};

static size_t format_array(char *buf, size_t bufsize, const char *fmt,
			   u32 *array, u32 array_size)
{
	size_t ret = 0;
	u32 i;

	for (i = 0; i < array_size; i++) {
		size_t len;

		len = snprintf(buf, bufsize, fmt, array[i]);
		len++;	/* ' ' or '\n' */
		ret += len;

		if (buf) {
			buf += len;
			bufsize -= len;
			buf[-1] = (i == array_size-1) ? '\n' : ' ';
		}
	}

	ret++;		/* \0 */
	if (buf)
		*buf = '\0';

	return ret;
}

static char *format_array_alloc(const char *fmt, u32 *array,
						u32 array_size)
{
	size_t len = format_array(NULL, 0, fmt, array, array_size);
	char *ret;

	ret = kmalloc(len, GFP_KERNEL);
	if (ret == NULL)
		return NULL;

	format_array(ret, len, fmt, array, array_size);
	return ret;
}

static int u32_array_open(struct inode *inode, struct file *file)
{
	struct array_data *data = inode->i_private;

	file->private_data = format_array_alloc("%u", data->array,
						      data->elements);
	if (!file->private_data)
		return -ENOMEM;
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

	return debugfs_create_file(name, mode, parent, data, &u32_array_fops);
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
int debugfs_print_regs32(struct seq_file *s, const struct debugfs_reg32 *regs,
			   int nregs, void __iomem *base, char *prefix)
{
	int i, ret = 0;

	for (i = 0; i < nregs; i++, regs++) {
		if (prefix)
			ret += seq_printf(s, "%s", prefix);
		ret += seq_printf(s, "%s = 0x%08x\n", regs->name,
				  readl(base + regs->offset));
	}
	return ret;
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
