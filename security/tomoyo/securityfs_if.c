/*
 * security/tomoyo/securityfs_if.c
 *
 * Copyright (C) 2005-2011  NTT DATA CORPORATION
 */

#include <linux/security.h>
#include "common.h"

/**
 * tomoyo_open - open() for /sys/kernel/security/tomoyo/ interface.
 *
 * @inode: Pointer to "struct inode".
 * @file:  Pointer to "struct file".
 *
 * Returns 0 on success, negative value otherwise.
 */
static int tomoyo_open(struct inode *inode, struct file *file)
{
	const int key = ((u8 *) file->f_path.dentry->d_inode->i_private)
		- ((u8 *) NULL);
	return tomoyo_open_control(key, file);
}

/**
 * tomoyo_release - close() for /sys/kernel/security/tomoyo/ interface.
 *
 * @inode: Pointer to "struct inode".
 * @file:  Pointer to "struct file".
 *
 * Returns 0 on success, negative value otherwise.
 */
static int tomoyo_release(struct inode *inode, struct file *file)
{
	return tomoyo_close_control(file->private_data);
}

/**
 * tomoyo_poll - poll() for /sys/kernel/security/tomoyo/ interface.
 *
 * @file: Pointer to "struct file".
 * @wait: Pointer to "poll_table".
 *
 * Returns 0 on success, negative value otherwise.
 */
static unsigned int tomoyo_poll(struct file *file, poll_table *wait)
{
	return tomoyo_poll_control(file, wait);
}

/**
 * tomoyo_read - read() for /sys/kernel/security/tomoyo/ interface.
 *
 * @file:  Pointer to "struct file".
 * @buf:   Pointer to buffer.
 * @count: Size of @buf.
 * @ppos:  Unused.
 *
 * Returns bytes read on success, negative value otherwise.
 */
static ssize_t tomoyo_read(struct file *file, char __user *buf, size_t count,
			   loff_t *ppos)
{
	return tomoyo_read_control(file->private_data, buf, count);
}

/**
 * tomoyo_write - write() for /sys/kernel/security/tomoyo/ interface.
 *
 * @file:  Pointer to "struct file".
 * @buf:   Pointer to buffer.
 * @count: Size of @buf.
 * @ppos:  Unused.
 *
 * Returns @count on success, negative value otherwise.
 */
static ssize_t tomoyo_write(struct file *file, const char __user *buf,
			    size_t count, loff_t *ppos)
{
	return tomoyo_write_control(file->private_data, buf, count);
}

/*
 * tomoyo_operations is a "struct file_operations" which is used for handling
 * /sys/kernel/security/tomoyo/ interface.
 *
 * Some files under /sys/kernel/security/tomoyo/ directory accept open(O_RDWR).
 * See tomoyo_io_buffer for internals.
 */
static const struct file_operations tomoyo_operations = {
	.open    = tomoyo_open,
	.release = tomoyo_release,
	.poll    = tomoyo_poll,
	.read    = tomoyo_read,
	.write   = tomoyo_write,
	.llseek  = noop_llseek,
};

/**
 * tomoyo_create_entry - Create interface files under /sys/kernel/security/tomoyo/ directory.
 *
 * @name:   The name of the interface file.
 * @mode:   The permission of the interface file.
 * @parent: The parent directory.
 * @key:    Type of interface.
 *
 * Returns nothing.
 */
static void __init tomoyo_create_entry(const char *name, const mode_t mode,
				       struct dentry *parent, const u8 key)
{
	securityfs_create_file(name, mode, parent, ((u8 *) NULL) + key,
			       &tomoyo_operations);
}

/**
 * tomoyo_initerface_init - Initialize /sys/kernel/security/tomoyo/ interface.
 *
 * Returns 0.
 */
static int __init tomoyo_initerface_init(void)
{
	struct dentry *tomoyo_dir;

	/* Don't create securityfs entries unless registered. */
	if (current_cred()->security != &tomoyo_kernel_domain)
		return 0;

	tomoyo_dir = securityfs_create_dir("tomoyo", NULL);
	tomoyo_create_entry("query",            0600, tomoyo_dir,
			    TOMOYO_QUERY);
	tomoyo_create_entry("domain_policy",    0600, tomoyo_dir,
			    TOMOYO_DOMAINPOLICY);
	tomoyo_create_entry("exception_policy", 0600, tomoyo_dir,
			    TOMOYO_EXCEPTIONPOLICY);
	tomoyo_create_entry("audit",            0400, tomoyo_dir,
			    TOMOYO_AUDIT);
	tomoyo_create_entry("self_domain",      0400, tomoyo_dir,
			    TOMOYO_SELFDOMAIN);
	tomoyo_create_entry(".process_status",  0600, tomoyo_dir,
			    TOMOYO_PROCESS_STATUS);
	tomoyo_create_entry("stat",             0644, tomoyo_dir,
			    TOMOYO_STAT);
	tomoyo_create_entry("profile",          0600, tomoyo_dir,
			    TOMOYO_PROFILE);
	tomoyo_create_entry("manager",          0600, tomoyo_dir,
			    TOMOYO_MANAGER);
	tomoyo_create_entry("version",          0400, tomoyo_dir,
			    TOMOYO_VERSION);
	return 0;
}

fs_initcall(tomoyo_initerface_init);
