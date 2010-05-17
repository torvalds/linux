/*
 * security/tomoyo/file.c
 *
 * Implementation of the Domain-Based Mandatory Access Control.
 *
 * Copyright (C) 2005-2009  NTT DATA CORPORATION
 *
 * Version: 2.2.0   2009/04/01
 *
 */

#include "common.h"
#include <linux/slab.h>

/* Keyword array for operations with one pathname. */
static const char *tomoyo_path_keyword[TOMOYO_MAX_PATH_OPERATION] = {
	[TOMOYO_TYPE_READ_WRITE] = "read/write",
	[TOMOYO_TYPE_EXECUTE]    = "execute",
	[TOMOYO_TYPE_READ]       = "read",
	[TOMOYO_TYPE_WRITE]      = "write",
	[TOMOYO_TYPE_UNLINK]     = "unlink",
	[TOMOYO_TYPE_RMDIR]      = "rmdir",
	[TOMOYO_TYPE_TRUNCATE]   = "truncate",
	[TOMOYO_TYPE_SYMLINK]    = "symlink",
	[TOMOYO_TYPE_REWRITE]    = "rewrite",
	[TOMOYO_TYPE_CHROOT]     = "chroot",
	[TOMOYO_TYPE_UMOUNT]     = "unmount",
};

/* Keyword array for operations with one pathname and three numbers. */
static const char *tomoyo_path_number3_keyword
[TOMOYO_MAX_PATH_NUMBER3_OPERATION] = {
	[TOMOYO_TYPE_MKBLOCK]    = "mkblock",
	[TOMOYO_TYPE_MKCHAR]     = "mkchar",
};

/* Keyword array for operations with two pathnames. */
static const char *tomoyo_path2_keyword[TOMOYO_MAX_PATH2_OPERATION] = {
	[TOMOYO_TYPE_LINK]       = "link",
	[TOMOYO_TYPE_RENAME]     = "rename",
	[TOMOYO_TYPE_PIVOT_ROOT] = "pivot_root",
};

/* Keyword array for operations with one pathname and one number. */
static const char *tomoyo_path_number_keyword
[TOMOYO_MAX_PATH_NUMBER_OPERATION] = {
	[TOMOYO_TYPE_CREATE]     = "create",
	[TOMOYO_TYPE_MKDIR]      = "mkdir",
	[TOMOYO_TYPE_MKFIFO]     = "mkfifo",
	[TOMOYO_TYPE_MKSOCK]     = "mksock",
	[TOMOYO_TYPE_IOCTL]      = "ioctl",
	[TOMOYO_TYPE_CHMOD]      = "chmod",
	[TOMOYO_TYPE_CHOWN]      = "chown",
	[TOMOYO_TYPE_CHGRP]      = "chgrp",
};

void tomoyo_put_name_union(struct tomoyo_name_union *ptr)
{
	if (!ptr)
		return;
	if (ptr->is_group)
		tomoyo_put_path_group(ptr->group);
	else
		tomoyo_put_name(ptr->filename);
}

bool tomoyo_compare_name_union(const struct tomoyo_path_info *name,
			       const struct tomoyo_name_union *ptr)
{
	if (ptr->is_group)
		return tomoyo_path_matches_group(name, ptr->group, 1);
	return tomoyo_path_matches_pattern(name, ptr->filename);
}

static bool tomoyo_compare_name_union_pattern(const struct tomoyo_path_info
					      *name,
					      const struct tomoyo_name_union
					      *ptr, const bool may_use_pattern)
{
	if (ptr->is_group)
		return tomoyo_path_matches_group(name, ptr->group,
						 may_use_pattern);
	if (may_use_pattern || !ptr->filename->is_patterned)
		return tomoyo_path_matches_pattern(name, ptr->filename);
	return false;
}

void tomoyo_put_number_union(struct tomoyo_number_union *ptr)
{
	if (ptr && ptr->is_group)
		tomoyo_put_number_group(ptr->group);
}

bool tomoyo_compare_number_union(const unsigned long value,
				 const struct tomoyo_number_union *ptr)
{
	if (ptr->is_group)
		return tomoyo_number_matches_group(value, value, ptr->group);
	return value >= ptr->values[0] && value <= ptr->values[1];
}

/**
 * tomoyo_init_request_info - Initialize "struct tomoyo_request_info" members.
 *
 * @r:      Pointer to "struct tomoyo_request_info" to initialize.
 * @domain: Pointer to "struct tomoyo_domain_info". NULL for tomoyo_domain().
 *
 * Returns mode.
 */
int tomoyo_init_request_info(struct tomoyo_request_info *r,
			     struct tomoyo_domain_info *domain)
{
	memset(r, 0, sizeof(*r));
	if (!domain)
		domain = tomoyo_domain();
	r->domain = domain;
	r->mode = tomoyo_check_flags(domain, TOMOYO_MAC_FOR_FILE);
	return r->mode;
}

static void tomoyo_warn_log(struct tomoyo_request_info *r, const char *fmt, ...)
     __attribute__ ((format(printf, 2, 3)));
/**
 * tomoyo_warn_log - Print warning or error message on console.
 *
 * @r:   Pointer to "struct tomoyo_request_info".
 * @fmt: The printf()'s format string, followed by parameters.
 */
static void tomoyo_warn_log(struct tomoyo_request_info *r, const char *fmt, ...)
{
	int len = PAGE_SIZE;
	va_list args;
	char *buffer;
	if (!tomoyo_verbose_mode(r->domain))
		return;
	while (1) {
		int len2;
		buffer = kmalloc(len, GFP_NOFS);
		if (!buffer)
			return;
		va_start(args, fmt);
		len2 = vsnprintf(buffer, len - 1, fmt, args);
		va_end(args);
		if (len2 <= len - 1) {
			buffer[len2] = '\0';
			break;
		}
		len = len2 + 1;
		kfree(buffer);
	}
	printk(KERN_WARNING "TOMOYO-%s: Access %s denied for %s\n",
	       r->mode == TOMOYO_CONFIG_ENFORCING ? "ERROR" : "WARNING",
	       buffer, tomoyo_get_last_name(r->domain));
	kfree(buffer);
}

/**
 * tomoyo_path2keyword - Get the name of single path operation.
 *
 * @operation: Type of operation.
 *
 * Returns the name of single path operation.
 */
const char *tomoyo_path2keyword(const u8 operation)
{
	return (operation < TOMOYO_MAX_PATH_OPERATION)
		? tomoyo_path_keyword[operation] : NULL;
}

/**
 * tomoyo_path_number32keyword - Get the name of path/number/number/number operations.
 *
 * @operation: Type of operation.
 *
 * Returns the name of path/number/number/number operation.
 */
const char *tomoyo_path_number32keyword(const u8 operation)
{
	return (operation < TOMOYO_MAX_PATH_NUMBER3_OPERATION)
		? tomoyo_path_number3_keyword[operation] : NULL;
}

/**
 * tomoyo_path22keyword - Get the name of double path operation.
 *
 * @operation: Type of operation.
 *
 * Returns the name of double path operation.
 */
const char *tomoyo_path22keyword(const u8 operation)
{
	return (operation < TOMOYO_MAX_PATH2_OPERATION)
		? tomoyo_path2_keyword[operation] : NULL;
}

/**
 * tomoyo_path_number2keyword - Get the name of path/number operations.
 *
 * @operation: Type of operation.
 *
 * Returns the name of path/number operation.
 */
const char *tomoyo_path_number2keyword(const u8 operation)
{
	return (operation < TOMOYO_MAX_PATH_NUMBER_OPERATION)
		? tomoyo_path_number_keyword[operation] : NULL;
}

/**
 * tomoyo_strendswith - Check whether the token ends with the given token.
 *
 * @name: The token to check.
 * @tail: The token to find.
 *
 * Returns true if @name ends with @tail, false otherwise.
 */
static bool tomoyo_strendswith(const char *name, const char *tail)
{
	int len;

	if (!name || !tail)
		return false;
	len = strlen(name) - strlen(tail);
	return len >= 0 && !strcmp(name + len, tail);
}

/**
 * tomoyo_get_path - Get realpath.
 *
 * @path: Pointer to "struct path".
 *
 * Returns pointer to "struct tomoyo_path_info" on success, NULL otherwise.
 */
static struct tomoyo_path_info *tomoyo_get_path(struct path *path)
{
	int error;
	struct tomoyo_path_info_with_data *buf = kzalloc(sizeof(*buf),
							 GFP_NOFS);

	if (!buf)
		return NULL;
	/* Reserve one byte for appending "/". */
	error = tomoyo_realpath_from_path2(path, buf->body,
					   sizeof(buf->body) - 2);
	if (!error) {
		buf->head.name = buf->body;
		tomoyo_fill_path_info(&buf->head);
		return &buf->head;
	}
	kfree(buf);
	return NULL;
}

static int tomoyo_update_path2_acl(const u8 type, const char *filename1,
				   const char *filename2,
				   struct tomoyo_domain_info *const domain,
				   const bool is_delete);
static int tomoyo_update_path_acl(const u8 type, const char *filename,
				  struct tomoyo_domain_info *const domain,
				  const bool is_delete);

/*
 * tomoyo_globally_readable_list is used for holding list of pathnames which
 * are by default allowed to be open()ed for reading by any process.
 *
 * An entry is added by
 *
 * # echo 'allow_read /lib/libc-2.5.so' > \
 *                               /sys/kernel/security/tomoyo/exception_policy
 *
 * and is deleted by
 *
 * # echo 'delete allow_read /lib/libc-2.5.so' > \
 *                               /sys/kernel/security/tomoyo/exception_policy
 *
 * and all entries are retrieved by
 *
 * # grep ^allow_read /sys/kernel/security/tomoyo/exception_policy
 *
 * In the example above, any process is allowed to
 * open("/lib/libc-2.5.so", O_RDONLY).
 * One exception is, if the domain which current process belongs to is marked
 * as "ignore_global_allow_read", current process can't do so unless explicitly
 * given "allow_read /lib/libc-2.5.so" to the domain which current process
 * belongs to.
 */
LIST_HEAD(tomoyo_globally_readable_list);

/**
 * tomoyo_update_globally_readable_entry - Update "struct tomoyo_globally_readable_file_entry" list.
 *
 * @filename:  Filename unconditionally permitted to open() for reading.
 * @is_delete: True if it is a delete request.
 *
 * Returns 0 on success, negative value otherwise.
 *
 * Caller holds tomoyo_read_lock().
 */
static int tomoyo_update_globally_readable_entry(const char *filename,
						 const bool is_delete)
{
	struct tomoyo_globally_readable_file_entry *ptr;
	struct tomoyo_globally_readable_file_entry e = { };
	int error = is_delete ? -ENOENT : -ENOMEM;

	if (!tomoyo_is_correct_path(filename, 1, 0, -1))
		return -EINVAL;
	e.filename = tomoyo_get_name(filename);
	if (!e.filename)
		return -ENOMEM;
	if (mutex_lock_interruptible(&tomoyo_policy_lock))
		goto out;
	list_for_each_entry_rcu(ptr, &tomoyo_globally_readable_list, list) {
		if (ptr->filename != e.filename)
			continue;
		ptr->is_deleted = is_delete;
		error = 0;
		break;
	}
	if (!is_delete && error) {
		struct tomoyo_globally_readable_file_entry *entry =
			tomoyo_commit_ok(&e, sizeof(e));
		if (entry) {
			list_add_tail_rcu(&entry->list,
					  &tomoyo_globally_readable_list);
			error = 0;
		}
	}
	mutex_unlock(&tomoyo_policy_lock);
 out:
	tomoyo_put_name(e.filename);
	return error;
}

/**
 * tomoyo_is_globally_readable_file - Check if the file is unconditionnaly permitted to be open()ed for reading.
 *
 * @filename: The filename to check.
 *
 * Returns true if any domain can open @filename for reading, false otherwise.
 *
 * Caller holds tomoyo_read_lock().
 */
static bool tomoyo_is_globally_readable_file(const struct tomoyo_path_info *
					     filename)
{
	struct tomoyo_globally_readable_file_entry *ptr;
	bool found = false;

	list_for_each_entry_rcu(ptr, &tomoyo_globally_readable_list, list) {
		if (!ptr->is_deleted &&
		    tomoyo_path_matches_pattern(filename, ptr->filename)) {
			found = true;
			break;
		}
	}
	return found;
}

/**
 * tomoyo_write_globally_readable_policy - Write "struct tomoyo_globally_readable_file_entry" list.
 *
 * @data:      String to parse.
 * @is_delete: True if it is a delete request.
 *
 * Returns 0 on success, negative value otherwise.
 *
 * Caller holds tomoyo_read_lock().
 */
int tomoyo_write_globally_readable_policy(char *data, const bool is_delete)
{
	return tomoyo_update_globally_readable_entry(data, is_delete);
}

/**
 * tomoyo_read_globally_readable_policy - Read "struct tomoyo_globally_readable_file_entry" list.
 *
 * @head: Pointer to "struct tomoyo_io_buffer".
 *
 * Returns true on success, false otherwise.
 *
 * Caller holds tomoyo_read_lock().
 */
bool tomoyo_read_globally_readable_policy(struct tomoyo_io_buffer *head)
{
	struct list_head *pos;
	bool done = true;

	list_for_each_cookie(pos, head->read_var2,
			     &tomoyo_globally_readable_list) {
		struct tomoyo_globally_readable_file_entry *ptr;
		ptr = list_entry(pos,
				 struct tomoyo_globally_readable_file_entry,
				 list);
		if (ptr->is_deleted)
			continue;
		done = tomoyo_io_printf(head, TOMOYO_KEYWORD_ALLOW_READ "%s\n",
					ptr->filename->name);
		if (!done)
			break;
	}
	return done;
}

/* tomoyo_pattern_list is used for holding list of pathnames which are used for
 * converting pathnames to pathname patterns during learning mode.
 *
 * An entry is added by
 *
 * # echo 'file_pattern /proc/\$/mounts' > \
 *                             /sys/kernel/security/tomoyo/exception_policy
 *
 * and is deleted by
 *
 * # echo 'delete file_pattern /proc/\$/mounts' > \
 *                             /sys/kernel/security/tomoyo/exception_policy
 *
 * and all entries are retrieved by
 *
 * # grep ^file_pattern /sys/kernel/security/tomoyo/exception_policy
 *
 * In the example above, if a process which belongs to a domain which is in
 * learning mode requested open("/proc/1/mounts", O_RDONLY),
 * "allow_read /proc/\$/mounts" is automatically added to the domain which that
 * process belongs to.
 *
 * It is not a desirable behavior that we have to use /proc/\$/ instead of
 * /proc/self/ when current process needs to access only current process's
 * information. As of now, LSM version of TOMOYO is using __d_path() for
 * calculating pathname. Non LSM version of TOMOYO is using its own function
 * which pretends as if /proc/self/ is not a symlink; so that we can forbid
 * current process from accessing other process's information.
 */
LIST_HEAD(tomoyo_pattern_list);

/**
 * tomoyo_update_file_pattern_entry - Update "struct tomoyo_pattern_entry" list.
 *
 * @pattern:   Pathname pattern.
 * @is_delete: True if it is a delete request.
 *
 * Returns 0 on success, negative value otherwise.
 *
 * Caller holds tomoyo_read_lock().
 */
static int tomoyo_update_file_pattern_entry(const char *pattern,
					    const bool is_delete)
{
	struct tomoyo_pattern_entry *ptr;
	struct tomoyo_pattern_entry e = { .pattern = tomoyo_get_name(pattern) };
	int error = is_delete ? -ENOENT : -ENOMEM;

	if (!e.pattern)
		return error;
	if (!e.pattern->is_patterned)
		goto out;
	if (mutex_lock_interruptible(&tomoyo_policy_lock))
		goto out;
	list_for_each_entry_rcu(ptr, &tomoyo_pattern_list, list) {
		if (e.pattern != ptr->pattern)
			continue;
		ptr->is_deleted = is_delete;
		error = 0;
		break;
	}
	if (!is_delete && error) {
		struct tomoyo_pattern_entry *entry =
			tomoyo_commit_ok(&e, sizeof(e));
		if (entry) {
			list_add_tail_rcu(&entry->list, &tomoyo_pattern_list);
			error = 0;
		}
	}
	mutex_unlock(&tomoyo_policy_lock);
 out:
	tomoyo_put_name(e.pattern);
	return error;
}

/**
 * tomoyo_get_file_pattern - Get patterned pathname.
 *
 * @filename: The filename to find patterned pathname.
 *
 * Returns pointer to pathname pattern if matched, @filename otherwise.
 *
 * Caller holds tomoyo_read_lock().
 */
const struct tomoyo_path_info *
tomoyo_get_file_pattern(const struct tomoyo_path_info *filename)
{
	struct tomoyo_pattern_entry *ptr;
	const struct tomoyo_path_info *pattern = NULL;

	list_for_each_entry_rcu(ptr, &tomoyo_pattern_list, list) {
		if (ptr->is_deleted)
			continue;
		if (!tomoyo_path_matches_pattern(filename, ptr->pattern))
			continue;
		pattern = ptr->pattern;
		if (tomoyo_strendswith(pattern->name, "/\\*")) {
			/* Do nothing. Try to find the better match. */
		} else {
			/* This would be the better match. Use this. */
			break;
		}
	}
	if (pattern)
		filename = pattern;
	return filename;
}

/**
 * tomoyo_write_pattern_policy - Write "struct tomoyo_pattern_entry" list.
 *
 * @data:      String to parse.
 * @is_delete: True if it is a delete request.
 *
 * Returns 0 on success, negative value otherwise.
 *
 * Caller holds tomoyo_read_lock().
 */
int tomoyo_write_pattern_policy(char *data, const bool is_delete)
{
	return tomoyo_update_file_pattern_entry(data, is_delete);
}

/**
 * tomoyo_read_file_pattern - Read "struct tomoyo_pattern_entry" list.
 *
 * @head: Pointer to "struct tomoyo_io_buffer".
 *
 * Returns true on success, false otherwise.
 *
 * Caller holds tomoyo_read_lock().
 */
bool tomoyo_read_file_pattern(struct tomoyo_io_buffer *head)
{
	struct list_head *pos;
	bool done = true;

	list_for_each_cookie(pos, head->read_var2, &tomoyo_pattern_list) {
		struct tomoyo_pattern_entry *ptr;
		ptr = list_entry(pos, struct tomoyo_pattern_entry, list);
		if (ptr->is_deleted)
			continue;
		done = tomoyo_io_printf(head, TOMOYO_KEYWORD_FILE_PATTERN
					"%s\n", ptr->pattern->name);
		if (!done)
			break;
	}
	return done;
}

/*
 * tomoyo_no_rewrite_list is used for holding list of pathnames which are by
 * default forbidden to modify already written content of a file.
 *
 * An entry is added by
 *
 * # echo 'deny_rewrite /var/log/messages' > \
 *                              /sys/kernel/security/tomoyo/exception_policy
 *
 * and is deleted by
 *
 * # echo 'delete deny_rewrite /var/log/messages' > \
 *                              /sys/kernel/security/tomoyo/exception_policy
 *
 * and all entries are retrieved by
 *
 * # grep ^deny_rewrite /sys/kernel/security/tomoyo/exception_policy
 *
 * In the example above, if a process requested to rewrite /var/log/messages ,
 * the process can't rewrite unless the domain which that process belongs to
 * has "allow_rewrite /var/log/messages" entry.
 *
 * It is not a desirable behavior that we have to add "\040(deleted)" suffix
 * when we want to allow rewriting already unlink()ed file. As of now,
 * LSM version of TOMOYO is using __d_path() for calculating pathname.
 * Non LSM version of TOMOYO is using its own function which doesn't append
 * " (deleted)" suffix if the file is already unlink()ed; so that we don't
 * need to worry whether the file is already unlink()ed or not.
 */
LIST_HEAD(tomoyo_no_rewrite_list);

/**
 * tomoyo_update_no_rewrite_entry - Update "struct tomoyo_no_rewrite_entry" list.
 *
 * @pattern:   Pathname pattern that are not rewritable by default.
 * @is_delete: True if it is a delete request.
 *
 * Returns 0 on success, negative value otherwise.
 *
 * Caller holds tomoyo_read_lock().
 */
static int tomoyo_update_no_rewrite_entry(const char *pattern,
					  const bool is_delete)
{
	struct tomoyo_no_rewrite_entry *ptr;
	struct tomoyo_no_rewrite_entry e = { };
	int error = is_delete ? -ENOENT : -ENOMEM;

	if (!tomoyo_is_correct_path(pattern, 0, 0, 0))
		return -EINVAL;
	e.pattern = tomoyo_get_name(pattern);
	if (!e.pattern)
		return error;
	if (mutex_lock_interruptible(&tomoyo_policy_lock))
		goto out;
	list_for_each_entry_rcu(ptr, &tomoyo_no_rewrite_list, list) {
		if (ptr->pattern != e.pattern)
			continue;
		ptr->is_deleted = is_delete;
		error = 0;
		break;
	}
	if (!is_delete && error) {
		struct tomoyo_no_rewrite_entry *entry =
			tomoyo_commit_ok(&e, sizeof(e));
		if (entry) {
			list_add_tail_rcu(&entry->list,
					  &tomoyo_no_rewrite_list);
			error = 0;
		}
	}
	mutex_unlock(&tomoyo_policy_lock);
 out:
	tomoyo_put_name(e.pattern);
	return error;
}

/**
 * tomoyo_is_no_rewrite_file - Check if the given pathname is not permitted to be rewrited.
 *
 * @filename: Filename to check.
 *
 * Returns true if @filename is specified by "deny_rewrite" directive,
 * false otherwise.
 *
 * Caller holds tomoyo_read_lock().
 */
static bool tomoyo_is_no_rewrite_file(const struct tomoyo_path_info *filename)
{
	struct tomoyo_no_rewrite_entry *ptr;
	bool found = false;

	list_for_each_entry_rcu(ptr, &tomoyo_no_rewrite_list, list) {
		if (ptr->is_deleted)
			continue;
		if (!tomoyo_path_matches_pattern(filename, ptr->pattern))
			continue;
		found = true;
		break;
	}
	return found;
}

/**
 * tomoyo_write_no_rewrite_policy - Write "struct tomoyo_no_rewrite_entry" list.
 *
 * @data:      String to parse.
 * @is_delete: True if it is a delete request.
 *
 * Returns 0 on success, negative value otherwise.
 *
 * Caller holds tomoyo_read_lock().
 */
int tomoyo_write_no_rewrite_policy(char *data, const bool is_delete)
{
	return tomoyo_update_no_rewrite_entry(data, is_delete);
}

/**
 * tomoyo_read_no_rewrite_policy - Read "struct tomoyo_no_rewrite_entry" list.
 *
 * @head: Pointer to "struct tomoyo_io_buffer".
 *
 * Returns true on success, false otherwise.
 *
 * Caller holds tomoyo_read_lock().
 */
bool tomoyo_read_no_rewrite_policy(struct tomoyo_io_buffer *head)
{
	struct list_head *pos;
	bool done = true;

	list_for_each_cookie(pos, head->read_var2, &tomoyo_no_rewrite_list) {
		struct tomoyo_no_rewrite_entry *ptr;
		ptr = list_entry(pos, struct tomoyo_no_rewrite_entry, list);
		if (ptr->is_deleted)
			continue;
		done = tomoyo_io_printf(head, TOMOYO_KEYWORD_DENY_REWRITE
					"%s\n", ptr->pattern->name);
		if (!done)
			break;
	}
	return done;
}

/**
 * tomoyo_update_file_acl - Update file's read/write/execute ACL.
 *
 * @perm:      Permission (between 1 to 7).
 * @filename:  Filename.
 * @domain:    Pointer to "struct tomoyo_domain_info".
 * @is_delete: True if it is a delete request.
 *
 * Returns 0 on success, negative value otherwise.
 *
 * This is legacy support interface for older policy syntax.
 * Current policy syntax uses "allow_read/write" instead of "6",
 * "allow_read" instead of "4", "allow_write" instead of "2",
 * "allow_execute" instead of "1".
 *
 * Caller holds tomoyo_read_lock().
 */
static int tomoyo_update_file_acl(u8 perm, const char *filename,
				  struct tomoyo_domain_info * const domain,
				  const bool is_delete)
{
	if (perm > 7 || !perm) {
		printk(KERN_DEBUG "%s: Invalid permission '%d %s'\n",
		       __func__, perm, filename);
		return -EINVAL;
	}
	if (filename[0] != '@' && tomoyo_strendswith(filename, "/"))
		/*
		 * Only 'allow_mkdir' and 'allow_rmdir' are valid for
		 * directory permissions.
		 */
		return 0;
	if (perm & 4)
		tomoyo_update_path_acl(TOMOYO_TYPE_READ, filename, domain,
				       is_delete);
	if (perm & 2)
		tomoyo_update_path_acl(TOMOYO_TYPE_WRITE, filename, domain,
				       is_delete);
	if (perm & 1)
		tomoyo_update_path_acl(TOMOYO_TYPE_EXECUTE, filename, domain,
				       is_delete);
	return 0;
}

/**
 * tomoyo_path_acl - Check permission for single path operation.
 *
 * @r:               Pointer to "struct tomoyo_request_info".
 * @filename:        Filename to check.
 * @perm:            Permission.
 * @may_use_pattern: True if patterned ACL is permitted.
 *
 * Returns 0 on success, -EPERM otherwise.
 *
 * Caller holds tomoyo_read_lock().
 */
static int tomoyo_path_acl(const struct tomoyo_request_info *r,
			   const struct tomoyo_path_info *filename,
			   const u32 perm, const bool may_use_pattern)
{
	struct tomoyo_domain_info *domain = r->domain;
	struct tomoyo_acl_info *ptr;
	int error = -EPERM;

	list_for_each_entry_rcu(ptr, &domain->acl_info_list, list) {
		struct tomoyo_path_acl *acl;
		if (ptr->type != TOMOYO_TYPE_PATH_ACL)
			continue;
		acl = container_of(ptr, struct tomoyo_path_acl, head);
		if (!(acl->perm & perm) ||
		    !tomoyo_compare_name_union_pattern(filename, &acl->name,
                                                       may_use_pattern))
			continue;
		error = 0;
		break;
	}
	return error;
}

/**
 * tomoyo_file_perm - Check permission for opening files.
 *
 * @r:         Pointer to "struct tomoyo_request_info".
 * @filename:  Filename to check.
 * @mode:      Mode ("read" or "write" or "read/write" or "execute").
 *
 * Returns 0 on success, negative value otherwise.
 *
 * Caller holds tomoyo_read_lock().
 */
static int tomoyo_file_perm(struct tomoyo_request_info *r,
			    const struct tomoyo_path_info *filename,
			    const u8 mode)
{
	const char *msg = "<unknown>";
	int error = 0;
	u32 perm = 0;

	if (!filename)
		return 0;

	if (mode == 6) {
		msg = tomoyo_path2keyword(TOMOYO_TYPE_READ_WRITE);
		perm = 1 << TOMOYO_TYPE_READ_WRITE;
	} else if (mode == 4) {
		msg = tomoyo_path2keyword(TOMOYO_TYPE_READ);
		perm = 1 << TOMOYO_TYPE_READ;
	} else if (mode == 2) {
		msg = tomoyo_path2keyword(TOMOYO_TYPE_WRITE);
		perm = 1 << TOMOYO_TYPE_WRITE;
	} else if (mode == 1) {
		msg = tomoyo_path2keyword(TOMOYO_TYPE_EXECUTE);
		perm = 1 << TOMOYO_TYPE_EXECUTE;
	} else
		BUG();
	error = tomoyo_path_acl(r, filename, perm, mode != 1);
	if (error && mode == 4 && !r->domain->ignore_global_allow_read
	    && tomoyo_is_globally_readable_file(filename))
		error = 0;
	if (!error)
		return 0;
	tomoyo_warn_log(r, "%s %s", msg, filename->name);
	if (r->mode == TOMOYO_CONFIG_ENFORCING)
		return error;
	if (tomoyo_domain_quota_is_ok(r)) {
		/* Don't use patterns for execute permission. */
		const struct tomoyo_path_info *patterned_file = (mode != 1) ?
			tomoyo_get_file_pattern(filename) : filename;
		tomoyo_update_file_acl(mode, patterned_file->name, r->domain,
				       false);
	}
	return 0;
}

/**
 * tomoyo_update_path_acl - Update "struct tomoyo_path_acl" list.
 *
 * @type:      Type of operation.
 * @filename:  Filename.
 * @domain:    Pointer to "struct tomoyo_domain_info".
 * @is_delete: True if it is a delete request.
 *
 * Returns 0 on success, negative value otherwise.
 *
 * Caller holds tomoyo_read_lock().
 */
static int tomoyo_update_path_acl(const u8 type, const char *filename,
				  struct tomoyo_domain_info *const domain,
				  const bool is_delete)
{
	static const u16 tomoyo_rw_mask =
		(1 << TOMOYO_TYPE_READ) | (1 << TOMOYO_TYPE_WRITE);
	const u16 perm = 1 << type;
	struct tomoyo_acl_info *ptr;
	struct tomoyo_path_acl e = {
		.head.type = TOMOYO_TYPE_PATH_ACL,
		.perm = perm
	};
	int error = is_delete ? -ENOENT : -ENOMEM;

	if (type == TOMOYO_TYPE_READ_WRITE)
		e.perm |= tomoyo_rw_mask;
	if (!domain)
		return -EINVAL;
	if (!tomoyo_parse_name_union(filename, &e.name))
		return -EINVAL;
	if (mutex_lock_interruptible(&tomoyo_policy_lock))
		goto out;
	list_for_each_entry_rcu(ptr, &domain->acl_info_list, list) {
		struct tomoyo_path_acl *acl =
			container_of(ptr, struct tomoyo_path_acl, head);
		if (!tomoyo_is_same_path_acl(acl, &e))
			continue;
		if (is_delete) {
			acl->perm &= ~perm;
			if ((acl->perm & tomoyo_rw_mask) != tomoyo_rw_mask)
				acl->perm &= ~(1 << TOMOYO_TYPE_READ_WRITE);
			else if (!(acl->perm & (1 << TOMOYO_TYPE_READ_WRITE)))
				acl->perm &= ~tomoyo_rw_mask;
		} else {
			acl->perm |= perm;
			if ((acl->perm & tomoyo_rw_mask) == tomoyo_rw_mask)
				acl->perm |= 1 << TOMOYO_TYPE_READ_WRITE;
			else if (acl->perm & (1 << TOMOYO_TYPE_READ_WRITE))
				acl->perm |= tomoyo_rw_mask;
		}
		error = 0;
		break;
	}
	if (!is_delete && error) {
		struct tomoyo_path_acl *entry =
			tomoyo_commit_ok(&e, sizeof(e));
		if (entry) {
			list_add_tail_rcu(&entry->head.list,
					  &domain->acl_info_list);
			error = 0;
		}
	}
	mutex_unlock(&tomoyo_policy_lock);
 out:
	tomoyo_put_name_union(&e.name);
	return error;
}

/**
 * tomoyo_update_path_number3_acl - Update "struct tomoyo_path_number3_acl" list.
 *
 * @type:      Type of operation.
 * @filename:  Filename.
 * @mode:      Create mode.
 * @major:     Device major number.
 * @minor:     Device minor number.
 * @domain:    Pointer to "struct tomoyo_domain_info".
 * @is_delete: True if it is a delete request.
 *
 * Returns 0 on success, negative value otherwise.
 */
static inline int tomoyo_update_path_number3_acl(const u8 type,
						 const char *filename,
						 char *mode,
						 char *major, char *minor,
						 struct tomoyo_domain_info *
						 const domain,
						 const bool is_delete)
{
	const u8 perm = 1 << type;
	struct tomoyo_acl_info *ptr;
	struct tomoyo_path_number3_acl e = {
		.head.type = TOMOYO_TYPE_PATH_NUMBER3_ACL,
		.perm = perm
	};
	int error = is_delete ? -ENOENT : -ENOMEM;
	if (!tomoyo_parse_name_union(filename, &e.name) ||
	    !tomoyo_parse_number_union(mode, &e.mode) ||
	    !tomoyo_parse_number_union(major, &e.major) ||
	    !tomoyo_parse_number_union(minor, &e.minor))
		goto out;
	if (mutex_lock_interruptible(&tomoyo_policy_lock))
		goto out;
	list_for_each_entry_rcu(ptr, &domain->acl_info_list, list) {
		struct tomoyo_path_number3_acl *acl =
			container_of(ptr, struct tomoyo_path_number3_acl, head);
		if (!tomoyo_is_same_path_number3_acl(acl, &e))
			continue;
		if (is_delete)
			acl->perm &= ~perm;
		else
			acl->perm |= perm;
		error = 0;
		break;
	}
	if (!is_delete && error) {
		struct tomoyo_path_number3_acl *entry =
			tomoyo_commit_ok(&e, sizeof(e));
		if (entry) {
			list_add_tail_rcu(&entry->head.list,
					  &domain->acl_info_list);
			error = 0;
		}
	}
	mutex_unlock(&tomoyo_policy_lock);
 out:
	tomoyo_put_name_union(&e.name);
	tomoyo_put_number_union(&e.mode);
	tomoyo_put_number_union(&e.major);
	tomoyo_put_number_union(&e.minor);
	return error;
}

/**
 * tomoyo_update_path2_acl - Update "struct tomoyo_path2_acl" list.
 *
 * @type:      Type of operation.
 * @filename1: First filename.
 * @filename2: Second filename.
 * @domain:    Pointer to "struct tomoyo_domain_info".
 * @is_delete: True if it is a delete request.
 *
 * Returns 0 on success, negative value otherwise.
 *
 * Caller holds tomoyo_read_lock().
 */
static int tomoyo_update_path2_acl(const u8 type, const char *filename1,
				   const char *filename2,
				   struct tomoyo_domain_info *const domain,
				   const bool is_delete)
{
	const u8 perm = 1 << type;
	struct tomoyo_path2_acl e = {
		.head.type = TOMOYO_TYPE_PATH2_ACL,
		.perm = perm
	};
	struct tomoyo_acl_info *ptr;
	int error = is_delete ? -ENOENT : -ENOMEM;

	if (!domain)
		return -EINVAL;
	if (!tomoyo_parse_name_union(filename1, &e.name1) ||
	    !tomoyo_parse_name_union(filename2, &e.name2))
		goto out;
	if (mutex_lock_interruptible(&tomoyo_policy_lock))
		goto out;
	list_for_each_entry_rcu(ptr, &domain->acl_info_list, list) {
		struct tomoyo_path2_acl *acl =
			container_of(ptr, struct tomoyo_path2_acl, head);
		if (!tomoyo_is_same_path2_acl(acl, &e))
			continue;
		if (is_delete)
			acl->perm &= ~perm;
		else
			acl->perm |= perm;
		error = 0;
		break;
	}
	if (!is_delete && error) {
		struct tomoyo_path2_acl *entry =
			tomoyo_commit_ok(&e, sizeof(e));
		if (entry) {
			list_add_tail_rcu(&entry->head.list,
					  &domain->acl_info_list);
			error = 0;
		}
	}
	mutex_unlock(&tomoyo_policy_lock);
 out:
	tomoyo_put_name_union(&e.name1);
	tomoyo_put_name_union(&e.name2);
	return error;
}

/**
 * tomoyo_path_number3_acl - Check permission for path/number/number/number operation.
 *
 * @r:        Pointer to "struct tomoyo_request_info".
 * @filename: Filename to check.
 * @perm:     Permission.
 * @mode:     Create mode.
 * @major:    Device major number.
 * @minor:    Device minor number.
 *
 * Returns 0 on success, -EPERM otherwise.
 *
 * Caller holds tomoyo_read_lock().
 */
static int tomoyo_path_number3_acl(struct tomoyo_request_info *r,
				   const struct tomoyo_path_info *filename,
				   const u16 perm, const unsigned int mode,
				   const unsigned int major,
				   const unsigned int minor)
{
	struct tomoyo_domain_info *domain = r->domain;
	struct tomoyo_acl_info *ptr;
	int error = -EPERM;
	list_for_each_entry_rcu(ptr, &domain->acl_info_list, list) {
		struct tomoyo_path_number3_acl *acl;
		if (ptr->type != TOMOYO_TYPE_PATH_NUMBER3_ACL)
			continue;
		acl = container_of(ptr, struct tomoyo_path_number3_acl, head);
		if (!tomoyo_compare_number_union(mode, &acl->mode))
			continue;
		if (!tomoyo_compare_number_union(major, &acl->major))
			continue;
		if (!tomoyo_compare_number_union(minor, &acl->minor))
			continue;
		if (!(acl->perm & perm))
			continue;
		if (!tomoyo_compare_name_union(filename, &acl->name))
			continue;
		error = 0;
		break;
	}
	return error;
}

/**
 * tomoyo_path2_acl - Check permission for double path operation.
 *
 * @r:         Pointer to "struct tomoyo_request_info".
 * @type:      Type of operation.
 * @filename1: First filename to check.
 * @filename2: Second filename to check.
 *
 * Returns 0 on success, -EPERM otherwise.
 *
 * Caller holds tomoyo_read_lock().
 */
static int tomoyo_path2_acl(const struct tomoyo_request_info *r, const u8 type,
			    const struct tomoyo_path_info *filename1,
			    const struct tomoyo_path_info *filename2)
{
	const struct tomoyo_domain_info *domain = r->domain;
	struct tomoyo_acl_info *ptr;
	const u8 perm = 1 << type;
	int error = -EPERM;

	list_for_each_entry_rcu(ptr, &domain->acl_info_list, list) {
		struct tomoyo_path2_acl *acl;
		if (ptr->type != TOMOYO_TYPE_PATH2_ACL)
			continue;
		acl = container_of(ptr, struct tomoyo_path2_acl, head);
		if (!(acl->perm & perm))
			continue;
		if (!tomoyo_compare_name_union(filename1, &acl->name1))
			continue;
		if (!tomoyo_compare_name_union(filename2, &acl->name2))
			continue;
		error = 0;
		break;
	}
	return error;
}

/**
 * tomoyo_path_permission - Check permission for single path operation.
 *
 * @r:         Pointer to "struct tomoyo_request_info".
 * @operation: Type of operation.
 * @filename:  Filename to check.
 *
 * Returns 0 on success, negative value otherwise.
 *
 * Caller holds tomoyo_read_lock().
 */
static int tomoyo_path_permission(struct tomoyo_request_info *r, u8 operation,
				  const struct tomoyo_path_info *filename)
{
	int error;

 next:
	error = tomoyo_path_acl(r, filename, 1 << operation, 1);
	if (!error)
		goto ok;
	tomoyo_warn_log(r, "%s %s", tomoyo_path2keyword(operation),
			filename->name);
	if (tomoyo_domain_quota_is_ok(r)) {
		const char *name = tomoyo_get_file_pattern(filename)->name;
		tomoyo_update_path_acl(operation, name, r->domain, false);
	}
	if (r->mode != TOMOYO_CONFIG_ENFORCING)
		error = 0;
 ok:
	/*
	 * Since "allow_truncate" doesn't imply "allow_rewrite" permission,
	 * we need to check "allow_rewrite" permission if the filename is
	 * specified by "deny_rewrite" keyword.
	 */
	if (!error && operation == TOMOYO_TYPE_TRUNCATE &&
	    tomoyo_is_no_rewrite_file(filename)) {
		operation = TOMOYO_TYPE_REWRITE;
		goto next;
	}
	return error;
}

/**
 * tomoyo_path_number_acl - Check permission for ioctl/chmod/chown/chgrp operation.
 *
 * @r:        Pointer to "struct tomoyo_request_info".
 * @type:     Operation.
 * @filename: Filename to check.
 * @number:   Number.
 *
 * Returns 0 on success, -EPERM otherwise.
 *
 * Caller holds tomoyo_read_lock().
 */
static int tomoyo_path_number_acl(struct tomoyo_request_info *r, const u8 type,
				  const struct tomoyo_path_info *filename,
				  const unsigned long number)
{
	struct tomoyo_domain_info *domain = r->domain;
	struct tomoyo_acl_info *ptr;
	const u8 perm = 1 << type;
	int error = -EPERM;
	list_for_each_entry_rcu(ptr, &domain->acl_info_list, list) {
		struct tomoyo_path_number_acl *acl;
		if (ptr->type != TOMOYO_TYPE_PATH_NUMBER_ACL)
			continue;
		acl = container_of(ptr, struct tomoyo_path_number_acl,
				   head);
		if (!(acl->perm & perm) ||
		    !tomoyo_compare_number_union(number, &acl->number) ||
		    !tomoyo_compare_name_union(filename, &acl->name))
			continue;
		error = 0;
		break;
	}
	return error;
}

/**
 * tomoyo_update_path_number_acl - Update ioctl/chmod/chown/chgrp ACL.
 *
 * @type:      Type of operation.
 * @filename:  Filename.
 * @number:    Number.
 * @domain:    Pointer to "struct tomoyo_domain_info".
 * @is_delete: True if it is a delete request.
 *
 * Returns 0 on success, negative value otherwise.
 */
static inline int tomoyo_update_path_number_acl(const u8 type,
						const char *filename,
						char *number,
						struct tomoyo_domain_info *
						const domain,
						const bool is_delete)
{
	const u8 perm = 1 << type;
	struct tomoyo_acl_info *ptr;
	struct tomoyo_path_number_acl e = {
		.head.type = TOMOYO_TYPE_PATH_NUMBER_ACL,
		.perm = perm
	};
	int error = is_delete ? -ENOENT : -ENOMEM;
	if (!domain)
		return -EINVAL;
	if (!tomoyo_parse_name_union(filename, &e.name))
		return -EINVAL;
	if (!tomoyo_parse_number_union(number, &e.number))
		goto out;
	if (mutex_lock_interruptible(&tomoyo_policy_lock))
		goto out;
	list_for_each_entry_rcu(ptr, &domain->acl_info_list, list) {
		struct tomoyo_path_number_acl *acl =
			container_of(ptr, struct tomoyo_path_number_acl, head);
		if (!tomoyo_is_same_path_number_acl(acl, &e))
			continue;
		if (is_delete)
			acl->perm &= ~perm;
		else
			acl->perm |= perm;
		error = 0;
		break;
	}
	if (!is_delete && error) {
		struct tomoyo_path_number_acl *entry =
			tomoyo_commit_ok(&e, sizeof(e));
		if (entry) {
			list_add_tail_rcu(&entry->head.list,
					  &domain->acl_info_list);
			error = 0;
		}
	}
	mutex_unlock(&tomoyo_policy_lock);
 out:
	tomoyo_put_name_union(&e.name);
	tomoyo_put_number_union(&e.number);
	return error;
}

/**
 * tomoyo_path_number_perm2 - Check permission for "create", "mkdir", "mkfifo", "mksock", "ioctl", "chmod", "chown", "chgrp".
 *
 * @r:        Pointer to "strct tomoyo_request_info".
 * @filename: Filename to check.
 * @number:   Number.
 *
 * Returns 0 on success, negative value otherwise.
 *
 * Caller holds tomoyo_read_lock().
 */
static int tomoyo_path_number_perm2(struct tomoyo_request_info *r,
				    const u8 type,
				    const struct tomoyo_path_info *filename,
				    const unsigned long number)
{
	char buffer[64];
	int error;
	u8 radix;

	if (!filename)
		return 0;
	switch (type) {
	case TOMOYO_TYPE_CREATE:
	case TOMOYO_TYPE_MKDIR:
	case TOMOYO_TYPE_MKFIFO:
	case TOMOYO_TYPE_MKSOCK:
	case TOMOYO_TYPE_CHMOD:
		radix = TOMOYO_VALUE_TYPE_OCTAL;
		break;
	case TOMOYO_TYPE_IOCTL:
		radix = TOMOYO_VALUE_TYPE_HEXADECIMAL;
		break;
	default:
		radix = TOMOYO_VALUE_TYPE_DECIMAL;
		break;
	}
	tomoyo_print_ulong(buffer, sizeof(buffer), number, radix);
	error = tomoyo_path_number_acl(r, type, filename, number);
	if (!error)
		return 0;
	tomoyo_warn_log(r, "%s %s %s", tomoyo_path_number2keyword(type),
			filename->name, buffer);
	if (tomoyo_domain_quota_is_ok(r))
		tomoyo_update_path_number_acl(type,
					      tomoyo_get_file_pattern(filename)
					      ->name, buffer, r->domain, false);
	if (r->mode != TOMOYO_CONFIG_ENFORCING)
		error = 0;
	return error;
}

/**
 * tomoyo_path_number_perm - Check permission for "create", "mkdir", "mkfifo", "mksock", "ioctl", "chmod", "chown", "chgrp".
 *
 * @type:   Type of operation.
 * @path:   Pointer to "struct path".
 * @number: Number.
 *
 * Returns 0 on success, negative value otherwise.
 */
int tomoyo_path_number_perm(const u8 type, struct path *path,
			    unsigned long number)
{
	struct tomoyo_request_info r;
	int error = -ENOMEM;
	struct tomoyo_path_info *buf;
	int idx;

	if (tomoyo_init_request_info(&r, NULL) == TOMOYO_CONFIG_DISABLED ||
	    !path->mnt || !path->dentry)
		return 0;
	idx = tomoyo_read_lock();
	buf = tomoyo_get_path(path);
	if (!buf)
		goto out;
	if (type == TOMOYO_TYPE_MKDIR && !buf->is_dir) {
		/*
		 * tomoyo_get_path() reserves space for appending "/."
		 */
		strcat((char *) buf->name, "/");
		tomoyo_fill_path_info(buf);
	}
	error = tomoyo_path_number_perm2(&r, type, buf, number);
 out:
	kfree(buf);
	tomoyo_read_unlock(idx);
	if (r.mode != TOMOYO_CONFIG_ENFORCING)
		error = 0;
	return error;
}

/**
 * tomoyo_check_exec_perm - Check permission for "execute".
 *
 * @domain:   Pointer to "struct tomoyo_domain_info".
 * @filename: Check permission for "execute".
 *
 * Returns 0 on success, negativevalue otherwise.
 *
 * Caller holds tomoyo_read_lock().
 */
int tomoyo_check_exec_perm(struct tomoyo_domain_info *domain,
			   const struct tomoyo_path_info *filename)
{
	struct tomoyo_request_info r;

	if (tomoyo_init_request_info(&r, NULL) == TOMOYO_CONFIG_DISABLED)
		return 0;
	return tomoyo_file_perm(&r, filename, 1);
}

/**
 * tomoyo_check_open_permission - Check permission for "read" and "write".
 *
 * @domain: Pointer to "struct tomoyo_domain_info".
 * @path:   Pointer to "struct path".
 * @flag:   Flags for open().
 *
 * Returns 0 on success, negative value otherwise.
 */
int tomoyo_check_open_permission(struct tomoyo_domain_info *domain,
				 struct path *path, const int flag)
{
	const u8 acc_mode = ACC_MODE(flag);
	int error = -ENOMEM;
	struct tomoyo_path_info *buf;
	struct tomoyo_request_info r;
	int idx;

	if (tomoyo_init_request_info(&r, domain) == TOMOYO_CONFIG_DISABLED ||
	    !path->mnt)
		return 0;
	if (acc_mode == 0)
		return 0;
	if (path->dentry->d_inode && S_ISDIR(path->dentry->d_inode->i_mode))
		/*
		 * I don't check directories here because mkdir() and rmdir()
		 * don't call me.
		 */
		return 0;
	idx = tomoyo_read_lock();
	buf = tomoyo_get_path(path);
	if (!buf)
		goto out;
	error = 0;
	/*
	 * If the filename is specified by "deny_rewrite" keyword,
	 * we need to check "allow_rewrite" permission when the filename is not
	 * opened for append mode or the filename is truncated at open time.
	 */
	if ((acc_mode & MAY_WRITE) &&
	    ((flag & O_TRUNC) || !(flag & O_APPEND)) &&
	    (tomoyo_is_no_rewrite_file(buf))) {
		error = tomoyo_path_permission(&r, TOMOYO_TYPE_REWRITE, buf);
	}
	if (!error)
		error = tomoyo_file_perm(&r, buf, acc_mode);
	if (!error && (flag & O_TRUNC))
		error = tomoyo_path_permission(&r, TOMOYO_TYPE_TRUNCATE, buf);
 out:
	kfree(buf);
	tomoyo_read_unlock(idx);
	if (r.mode != TOMOYO_CONFIG_ENFORCING)
		error = 0;
	return error;
}

/**
 * tomoyo_path_perm - Check permission for "unlink", "rmdir", "truncate", "symlink", "rewrite", "chroot" and "unmount".
 *
 * @operation: Type of operation.
 * @path:      Pointer to "struct path".
 *
 * Returns 0 on success, negative value otherwise.
 */
int tomoyo_path_perm(const u8 operation, struct path *path)
{
	int error = -ENOMEM;
	struct tomoyo_path_info *buf;
	struct tomoyo_request_info r;
	int idx;

	if (tomoyo_init_request_info(&r, NULL) == TOMOYO_CONFIG_DISABLED ||
	    !path->mnt)
		return 0;
	idx = tomoyo_read_lock();
	buf = tomoyo_get_path(path);
	if (!buf)
		goto out;
	switch (operation) {
	case TOMOYO_TYPE_REWRITE:
		if (!tomoyo_is_no_rewrite_file(buf)) {
			error = 0;
			goto out;
		}
		break;
	case TOMOYO_TYPE_RMDIR:
	case TOMOYO_TYPE_CHROOT:
		if (!buf->is_dir) {
			/*
			 * tomoyo_get_path() reserves space for appending "/."
			 */
			strcat((char *) buf->name, "/");
			tomoyo_fill_path_info(buf);
		}
	}
	error = tomoyo_path_permission(&r, operation, buf);
 out:
	kfree(buf);
	tomoyo_read_unlock(idx);
	if (r.mode != TOMOYO_CONFIG_ENFORCING)
		error = 0;
	return error;
}

/**
 * tomoyo_path_number3_perm2 - Check permission for path/number/number/number operation.
 *
 * @r:         Pointer to "struct tomoyo_request_info".
 * @operation: Type of operation.
 * @filename:  Filename to check.
 * @mode:      Create mode.
 * @dev:       Device number.
 *
 * Returns 0 on success, negative value otherwise.
 *
 * Caller holds tomoyo_read_lock().
 */
static int tomoyo_path_number3_perm2(struct tomoyo_request_info *r,
				     const u8 operation,
				     const struct tomoyo_path_info *filename,
				     const unsigned int mode,
				     const unsigned int dev)
{
	int error;
	const unsigned int major = MAJOR(dev);
	const unsigned int minor = MINOR(dev);

	error = tomoyo_path_number3_acl(r, filename, 1 << operation, mode,
					major, minor);
	if (!error)
		return 0;
	tomoyo_warn_log(r, "%s %s 0%o %u %u",
			tomoyo_path_number32keyword(operation),
			filename->name, mode, major, minor);
	if (tomoyo_domain_quota_is_ok(r)) {
		char mode_buf[64];
		char major_buf[64];
		char minor_buf[64];
		memset(mode_buf, 0, sizeof(mode_buf));
		memset(major_buf, 0, sizeof(major_buf));
		memset(minor_buf, 0, sizeof(minor_buf));
		snprintf(mode_buf, sizeof(mode_buf) - 1, "0%o", mode);
		snprintf(major_buf, sizeof(major_buf) - 1, "%u", major);
		snprintf(minor_buf, sizeof(minor_buf) - 1, "%u", minor);
		tomoyo_update_path_number3_acl(operation,
					       tomoyo_get_file_pattern(filename)
					       ->name, mode_buf, major_buf,
					       minor_buf, r->domain, false);
	}
	if (r->mode != TOMOYO_CONFIG_ENFORCING)
		error = 0;
	return error;
}

/**
 * tomoyo_path_number3_perm - Check permission for "mkblock" and "mkchar".
 *
 * @operation: Type of operation. (TOMOYO_TYPE_MKCHAR or TOMOYO_TYPE_MKBLOCK)
 * @path:      Pointer to "struct path".
 * @mode:      Create mode.
 * @dev:       Device number.
 *
 * Returns 0 on success, negative value otherwise.
 */
int tomoyo_path_number3_perm(const u8 operation, struct path *path,
			     const unsigned int mode, unsigned int dev)
{
	struct tomoyo_request_info r;
	int error = -ENOMEM;
	struct tomoyo_path_info *buf;
	int idx;

	if (tomoyo_init_request_info(&r, NULL) == TOMOYO_CONFIG_DISABLED ||
	    !path->mnt)
		return 0;
	idx = tomoyo_read_lock();
	error = -ENOMEM;
	buf = tomoyo_get_path(path);
	if (buf) {
		error = tomoyo_path_number3_perm2(&r, operation, buf, mode,
						  new_decode_dev(dev));
		kfree(buf);
	}
	tomoyo_read_unlock(idx);
	if (r.mode != TOMOYO_CONFIG_ENFORCING)
		error = 0;
	return error;
}

/**
 * tomoyo_path2_perm - Check permission for "rename", "link" and "pivot_root".
 *
 * @operation: Type of operation.
 * @path1:      Pointer to "struct path".
 * @path2:      Pointer to "struct path".
 *
 * Returns 0 on success, negative value otherwise.
 */
int tomoyo_path2_perm(const u8 operation, struct path *path1,
		      struct path *path2)
{
	int error = -ENOMEM;
	struct tomoyo_path_info *buf1;
	struct tomoyo_path_info *buf2;
	struct tomoyo_request_info r;
	int idx;

	if (tomoyo_init_request_info(&r, NULL) == TOMOYO_CONFIG_DISABLED ||
	    !path1->mnt || !path2->mnt)
		return 0;
	idx = tomoyo_read_lock();
	buf1 = tomoyo_get_path(path1);
	buf2 = tomoyo_get_path(path2);
	if (!buf1 || !buf2)
		goto out;
	{
		struct dentry *dentry = path1->dentry;
		if (dentry->d_inode && S_ISDIR(dentry->d_inode->i_mode)) {
			/*
			 * tomoyo_get_path() reserves space for appending "/."
			 */
			if (!buf1->is_dir) {
				strcat((char *) buf1->name, "/");
				tomoyo_fill_path_info(buf1);
			}
			if (!buf2->is_dir) {
				strcat((char *) buf2->name, "/");
				tomoyo_fill_path_info(buf2);
			}
		}
	}
	error = tomoyo_path2_acl(&r, operation, buf1, buf2);
	if (!error)
		goto out;
	tomoyo_warn_log(&r, "%s %s %s", tomoyo_path22keyword(operation),
			buf1->name, buf2->name);
	if (tomoyo_domain_quota_is_ok(&r)) {
		const char *name1 = tomoyo_get_file_pattern(buf1)->name;
		const char *name2 = tomoyo_get_file_pattern(buf2)->name;
		tomoyo_update_path2_acl(operation, name1, name2, r.domain,
					false);
	}
 out:
	kfree(buf1);
	kfree(buf2);
	tomoyo_read_unlock(idx);
	if (r.mode != TOMOYO_CONFIG_ENFORCING)
		error = 0;
	return error;
}

/**
 * tomoyo_write_file_policy - Update file related list.
 *
 * @data:      String to parse.
 * @domain:    Pointer to "struct tomoyo_domain_info".
 * @is_delete: True if it is a delete request.
 *
 * Returns 0 on success, negative value otherwise.
 *
 * Caller holds tomoyo_read_lock().
 */
int tomoyo_write_file_policy(char *data, struct tomoyo_domain_info *domain,
			     const bool is_delete)
{
	char *w[5];
	u8 type;
	if (!tomoyo_tokenize(data, w, sizeof(w)) || !w[1][0])
		return -EINVAL;
	if (strncmp(w[0], "allow_", 6)) {
		unsigned int perm;
		if (sscanf(w[0], "%u", &perm) == 1)
			return tomoyo_update_file_acl((u8) perm, w[1], domain,
						      is_delete);
		goto out;
	}
	w[0] += 6;
	for (type = 0; type < TOMOYO_MAX_PATH_OPERATION; type++) {
		if (strcmp(w[0], tomoyo_path_keyword[type]))
			continue;
		return tomoyo_update_path_acl(type, w[1], domain, is_delete);
	}
	if (!w[2][0])
		goto out;
	for (type = 0; type < TOMOYO_MAX_PATH2_OPERATION; type++) {
		if (strcmp(w[0], tomoyo_path2_keyword[type]))
			continue;
		return tomoyo_update_path2_acl(type, w[1], w[2], domain,
					       is_delete);
	}
	for (type = 0; type < TOMOYO_MAX_PATH_NUMBER_OPERATION; type++) {
		if (strcmp(w[0], tomoyo_path_number_keyword[type]))
			continue;
		return tomoyo_update_path_number_acl(type, w[1], w[2], domain,
						     is_delete);
	}
	if (!w[3][0] || !w[4][0])
		goto out;
	for (type = 0; type < TOMOYO_MAX_PATH_NUMBER3_OPERATION; type++) {
		if (strcmp(w[0], tomoyo_path_number3_keyword[type]))
			continue;
		return tomoyo_update_path_number3_acl(type, w[1], w[2], w[3],
						      w[4], domain, is_delete);
	}
 out:
	return -EINVAL;
}
