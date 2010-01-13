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
#include "tomoyo.h"
#include "realpath.h"

/*
 * tomoyo_globally_readable_file_entry is a structure which is used for holding
 * "allow_read" entries.
 * It has following fields.
 *
 *  (1) "list" which is linked to tomoyo_globally_readable_list .
 *  (2) "filename" is a pathname which is allowed to open(O_RDONLY).
 *  (3) "is_deleted" is a bool which is true if marked as deleted, false
 *      otherwise.
 */
struct tomoyo_globally_readable_file_entry {
	struct list_head list;
	const struct tomoyo_path_info *filename;
	bool is_deleted;
};

/*
 * tomoyo_pattern_entry is a structure which is used for holding
 * "tomoyo_pattern_list" entries.
 * It has following fields.
 *
 *  (1) "list" which is linked to tomoyo_pattern_list .
 *  (2) "pattern" is a pathname pattern which is used for converting pathnames
 *      to pathname patterns during learning mode.
 *  (3) "is_deleted" is a bool which is true if marked as deleted, false
 *      otherwise.
 */
struct tomoyo_pattern_entry {
	struct list_head list;
	const struct tomoyo_path_info *pattern;
	bool is_deleted;
};

/*
 * tomoyo_no_rewrite_entry is a structure which is used for holding
 * "deny_rewrite" entries.
 * It has following fields.
 *
 *  (1) "list" which is linked to tomoyo_no_rewrite_list .
 *  (2) "pattern" is a pathname which is by default not permitted to modify
 *      already existing content.
 *  (3) "is_deleted" is a bool which is true if marked as deleted, false
 *      otherwise.
 */
struct tomoyo_no_rewrite_entry {
	struct list_head list;
	const struct tomoyo_path_info *pattern;
	bool is_deleted;
};

/* Keyword array for single path operations. */
static const char *tomoyo_sp_keyword[TOMOYO_MAX_SINGLE_PATH_OPERATION] = {
	[TOMOYO_TYPE_READ_WRITE_ACL] = "read/write",
	[TOMOYO_TYPE_EXECUTE_ACL]    = "execute",
	[TOMOYO_TYPE_READ_ACL]       = "read",
	[TOMOYO_TYPE_WRITE_ACL]      = "write",
	[TOMOYO_TYPE_CREATE_ACL]     = "create",
	[TOMOYO_TYPE_UNLINK_ACL]     = "unlink",
	[TOMOYO_TYPE_MKDIR_ACL]      = "mkdir",
	[TOMOYO_TYPE_RMDIR_ACL]      = "rmdir",
	[TOMOYO_TYPE_MKFIFO_ACL]     = "mkfifo",
	[TOMOYO_TYPE_MKSOCK_ACL]     = "mksock",
	[TOMOYO_TYPE_MKBLOCK_ACL]    = "mkblock",
	[TOMOYO_TYPE_MKCHAR_ACL]     = "mkchar",
	[TOMOYO_TYPE_TRUNCATE_ACL]   = "truncate",
	[TOMOYO_TYPE_SYMLINK_ACL]    = "symlink",
	[TOMOYO_TYPE_REWRITE_ACL]    = "rewrite",
};

/* Keyword array for double path operations. */
static const char *tomoyo_dp_keyword[TOMOYO_MAX_DOUBLE_PATH_OPERATION] = {
	[TOMOYO_TYPE_LINK_ACL]    = "link",
	[TOMOYO_TYPE_RENAME_ACL]  = "rename",
};

/**
 * tomoyo_sp2keyword - Get the name of single path operation.
 *
 * @operation: Type of operation.
 *
 * Returns the name of single path operation.
 */
const char *tomoyo_sp2keyword(const u8 operation)
{
	return (operation < TOMOYO_MAX_SINGLE_PATH_OPERATION)
		? tomoyo_sp_keyword[operation] : NULL;
}

/**
 * tomoyo_dp2keyword - Get the name of double path operation.
 *
 * @operation: Type of operation.
 *
 * Returns the name of double path operation.
 */
const char *tomoyo_dp2keyword(const u8 operation)
{
	return (operation < TOMOYO_MAX_DOUBLE_PATH_OPERATION)
		? tomoyo_dp_keyword[operation] : NULL;
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
	struct tomoyo_path_info_with_data *buf = tomoyo_alloc(sizeof(*buf));

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
	tomoyo_free(buf);
	return NULL;
}

/* Lock for domain->acl_info_list. */
DECLARE_RWSEM(tomoyo_domain_acl_info_list_lock);

static int tomoyo_update_double_path_acl(const u8 type, const char *filename1,
					 const char *filename2,
					 struct tomoyo_domain_info *
					 const domain, const bool is_delete);
static int tomoyo_update_single_path_acl(const u8 type, const char *filename,
					 struct tomoyo_domain_info *
					 const domain, const bool is_delete);

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
static LIST_HEAD(tomoyo_globally_readable_list);
static DECLARE_RWSEM(tomoyo_globally_readable_list_lock);

/**
 * tomoyo_update_globally_readable_entry - Update "struct tomoyo_globally_readable_file_entry" list.
 *
 * @filename:  Filename unconditionally permitted to open() for reading.
 * @is_delete: True if it is a delete request.
 *
 * Returns 0 on success, negative value otherwise.
 */
static int tomoyo_update_globally_readable_entry(const char *filename,
						 const bool is_delete)
{
	struct tomoyo_globally_readable_file_entry *new_entry;
	struct tomoyo_globally_readable_file_entry *ptr;
	const struct tomoyo_path_info *saved_filename;
	int error = -ENOMEM;

	if (!tomoyo_is_correct_path(filename, 1, 0, -1, __func__))
		return -EINVAL;
	saved_filename = tomoyo_save_name(filename);
	if (!saved_filename)
		return -ENOMEM;
	down_write(&tomoyo_globally_readable_list_lock);
	list_for_each_entry(ptr, &tomoyo_globally_readable_list, list) {
		if (ptr->filename != saved_filename)
			continue;
		ptr->is_deleted = is_delete;
		error = 0;
		goto out;
	}
	if (is_delete) {
		error = -ENOENT;
		goto out;
	}
	new_entry = tomoyo_alloc_element(sizeof(*new_entry));
	if (!new_entry)
		goto out;
	new_entry->filename = saved_filename;
	list_add_tail(&new_entry->list, &tomoyo_globally_readable_list);
	error = 0;
 out:
	up_write(&tomoyo_globally_readable_list_lock);
	return error;
}

/**
 * tomoyo_is_globally_readable_file - Check if the file is unconditionnaly permitted to be open()ed for reading.
 *
 * @filename: The filename to check.
 *
 * Returns true if any domain can open @filename for reading, false otherwise.
 */
static bool tomoyo_is_globally_readable_file(const struct tomoyo_path_info *
					     filename)
{
	struct tomoyo_globally_readable_file_entry *ptr;
	bool found = false;
	down_read(&tomoyo_globally_readable_list_lock);
	list_for_each_entry(ptr, &tomoyo_globally_readable_list, list) {
		if (!ptr->is_deleted &&
		    tomoyo_path_matches_pattern(filename, ptr->filename)) {
			found = true;
			break;
		}
	}
	up_read(&tomoyo_globally_readable_list_lock);
	return found;
}

/**
 * tomoyo_write_globally_readable_policy - Write "struct tomoyo_globally_readable_file_entry" list.
 *
 * @data:      String to parse.
 * @is_delete: True if it is a delete request.
 *
 * Returns 0 on success, negative value otherwise.
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
 */
bool tomoyo_read_globally_readable_policy(struct tomoyo_io_buffer *head)
{
	struct list_head *pos;
	bool done = true;

	down_read(&tomoyo_globally_readable_list_lock);
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
	up_read(&tomoyo_globally_readable_list_lock);
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
static LIST_HEAD(tomoyo_pattern_list);
static DECLARE_RWSEM(tomoyo_pattern_list_lock);

/**
 * tomoyo_update_file_pattern_entry - Update "struct tomoyo_pattern_entry" list.
 *
 * @pattern:   Pathname pattern.
 * @is_delete: True if it is a delete request.
 *
 * Returns 0 on success, negative value otherwise.
 */
static int tomoyo_update_file_pattern_entry(const char *pattern,
					    const bool is_delete)
{
	struct tomoyo_pattern_entry *new_entry;
	struct tomoyo_pattern_entry *ptr;
	const struct tomoyo_path_info *saved_pattern;
	int error = -ENOMEM;

	if (!tomoyo_is_correct_path(pattern, 0, 1, 0, __func__))
		return -EINVAL;
	saved_pattern = tomoyo_save_name(pattern);
	if (!saved_pattern)
		return -ENOMEM;
	down_write(&tomoyo_pattern_list_lock);
	list_for_each_entry(ptr, &tomoyo_pattern_list, list) {
		if (saved_pattern != ptr->pattern)
			continue;
		ptr->is_deleted = is_delete;
		error = 0;
		goto out;
	}
	if (is_delete) {
		error = -ENOENT;
		goto out;
	}
	new_entry = tomoyo_alloc_element(sizeof(*new_entry));
	if (!new_entry)
		goto out;
	new_entry->pattern = saved_pattern;
	list_add_tail(&new_entry->list, &tomoyo_pattern_list);
	error = 0;
 out:
	up_write(&tomoyo_pattern_list_lock);
	return error;
}

/**
 * tomoyo_get_file_pattern - Get patterned pathname.
 *
 * @filename: The filename to find patterned pathname.
 *
 * Returns pointer to pathname pattern if matched, @filename otherwise.
 */
static const struct tomoyo_path_info *
tomoyo_get_file_pattern(const struct tomoyo_path_info *filename)
{
	struct tomoyo_pattern_entry *ptr;
	const struct tomoyo_path_info *pattern = NULL;

	down_read(&tomoyo_pattern_list_lock);
	list_for_each_entry(ptr, &tomoyo_pattern_list, list) {
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
	up_read(&tomoyo_pattern_list_lock);
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
 */
bool tomoyo_read_file_pattern(struct tomoyo_io_buffer *head)
{
	struct list_head *pos;
	bool done = true;

	down_read(&tomoyo_pattern_list_lock);
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
	up_read(&tomoyo_pattern_list_lock);
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
static LIST_HEAD(tomoyo_no_rewrite_list);
static DECLARE_RWSEM(tomoyo_no_rewrite_list_lock);

/**
 * tomoyo_update_no_rewrite_entry - Update "struct tomoyo_no_rewrite_entry" list.
 *
 * @pattern:   Pathname pattern that are not rewritable by default.
 * @is_delete: True if it is a delete request.
 *
 * Returns 0 on success, negative value otherwise.
 */
static int tomoyo_update_no_rewrite_entry(const char *pattern,
					  const bool is_delete)
{
	struct tomoyo_no_rewrite_entry *new_entry, *ptr;
	const struct tomoyo_path_info *saved_pattern;
	int error = -ENOMEM;

	if (!tomoyo_is_correct_path(pattern, 0, 0, 0, __func__))
		return -EINVAL;
	saved_pattern = tomoyo_save_name(pattern);
	if (!saved_pattern)
		return -ENOMEM;
	down_write(&tomoyo_no_rewrite_list_lock);
	list_for_each_entry(ptr, &tomoyo_no_rewrite_list, list) {
		if (ptr->pattern != saved_pattern)
			continue;
		ptr->is_deleted = is_delete;
		error = 0;
		goto out;
	}
	if (is_delete) {
		error = -ENOENT;
		goto out;
	}
	new_entry = tomoyo_alloc_element(sizeof(*new_entry));
	if (!new_entry)
		goto out;
	new_entry->pattern = saved_pattern;
	list_add_tail(&new_entry->list, &tomoyo_no_rewrite_list);
	error = 0;
 out:
	up_write(&tomoyo_no_rewrite_list_lock);
	return error;
}

/**
 * tomoyo_is_no_rewrite_file - Check if the given pathname is not permitted to be rewrited.
 *
 * @filename: Filename to check.
 *
 * Returns true if @filename is specified by "deny_rewrite" directive,
 * false otherwise.
 */
static bool tomoyo_is_no_rewrite_file(const struct tomoyo_path_info *filename)
{
	struct tomoyo_no_rewrite_entry *ptr;
	bool found = false;

	down_read(&tomoyo_no_rewrite_list_lock);
	list_for_each_entry(ptr, &tomoyo_no_rewrite_list, list) {
		if (ptr->is_deleted)
			continue;
		if (!tomoyo_path_matches_pattern(filename, ptr->pattern))
			continue;
		found = true;
		break;
	}
	up_read(&tomoyo_no_rewrite_list_lock);
	return found;
}

/**
 * tomoyo_write_no_rewrite_policy - Write "struct tomoyo_no_rewrite_entry" list.
 *
 * @data:      String to parse.
 * @is_delete: True if it is a delete request.
 *
 * Returns 0 on success, negative value otherwise.
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
 */
bool tomoyo_read_no_rewrite_policy(struct tomoyo_io_buffer *head)
{
	struct list_head *pos;
	bool done = true;

	down_read(&tomoyo_no_rewrite_list_lock);
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
	up_read(&tomoyo_no_rewrite_list_lock);
	return done;
}

/**
 * tomoyo_update_file_acl - Update file's read/write/execute ACL.
 *
 * @filename:  Filename.
 * @perm:      Permission (between 1 to 7).
 * @domain:    Pointer to "struct tomoyo_domain_info".
 * @is_delete: True if it is a delete request.
 *
 * Returns 0 on success, negative value otherwise.
 *
 * This is legacy support interface for older policy syntax.
 * Current policy syntax uses "allow_read/write" instead of "6",
 * "allow_read" instead of "4", "allow_write" instead of "2",
 * "allow_execute" instead of "1".
 */
static int tomoyo_update_file_acl(const char *filename, u8 perm,
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
		tomoyo_update_single_path_acl(TOMOYO_TYPE_READ_ACL, filename,
					      domain, is_delete);
	if (perm & 2)
		tomoyo_update_single_path_acl(TOMOYO_TYPE_WRITE_ACL, filename,
					      domain, is_delete);
	if (perm & 1)
		tomoyo_update_single_path_acl(TOMOYO_TYPE_EXECUTE_ACL,
					      filename, domain, is_delete);
	return 0;
}

/**
 * tomoyo_check_single_path_acl2 - Check permission for single path operation.
 *
 * @domain:          Pointer to "struct tomoyo_domain_info".
 * @filename:        Filename to check.
 * @perm:            Permission.
 * @may_use_pattern: True if patterned ACL is permitted.
 *
 * Returns 0 on success, -EPERM otherwise.
 */
static int tomoyo_check_single_path_acl2(const struct tomoyo_domain_info *
					 domain,
					 const struct tomoyo_path_info *
					 filename,
					 const u16 perm,
					 const bool may_use_pattern)
{
	struct tomoyo_acl_info *ptr;
	int error = -EPERM;

	down_read(&tomoyo_domain_acl_info_list_lock);
	list_for_each_entry(ptr, &domain->acl_info_list, list) {
		struct tomoyo_single_path_acl_record *acl;
		if (tomoyo_acl_type2(ptr) != TOMOYO_TYPE_SINGLE_PATH_ACL)
			continue;
		acl = container_of(ptr, struct tomoyo_single_path_acl_record,
				   head);
		if (!(acl->perm & perm))
			continue;
		if (may_use_pattern || !acl->filename->is_patterned) {
			if (!tomoyo_path_matches_pattern(filename,
							 acl->filename))
				continue;
		} else {
			continue;
		}
		error = 0;
		break;
	}
	up_read(&tomoyo_domain_acl_info_list_lock);
	return error;
}

/**
 * tomoyo_check_file_acl - Check permission for opening files.
 *
 * @domain:    Pointer to "struct tomoyo_domain_info".
 * @filename:  Filename to check.
 * @operation: Mode ("read" or "write" or "read/write" or "execute").
 *
 * Returns 0 on success, -EPERM otherwise.
 */
static int tomoyo_check_file_acl(const struct tomoyo_domain_info *domain,
				 const struct tomoyo_path_info *filename,
				 const u8 operation)
{
	u16 perm = 0;

	if (!tomoyo_check_flags(domain, TOMOYO_MAC_FOR_FILE))
		return 0;
	if (operation == 6)
		perm = 1 << TOMOYO_TYPE_READ_WRITE_ACL;
	else if (operation == 4)
		perm = 1 << TOMOYO_TYPE_READ_ACL;
	else if (operation == 2)
		perm = 1 << TOMOYO_TYPE_WRITE_ACL;
	else if (operation == 1)
		perm = 1 << TOMOYO_TYPE_EXECUTE_ACL;
	else
		BUG();
	return tomoyo_check_single_path_acl2(domain, filename, perm,
					     operation != 1);
}

/**
 * tomoyo_check_file_perm2 - Check permission for opening files.
 *
 * @domain:    Pointer to "struct tomoyo_domain_info".
 * @filename:  Filename to check.
 * @perm:      Mode ("read" or "write" or "read/write" or "execute").
 * @operation: Operation name passed used for verbose mode.
 * @mode:      Access control mode.
 *
 * Returns 0 on success, negative value otherwise.
 */
static int tomoyo_check_file_perm2(struct tomoyo_domain_info * const domain,
				   const struct tomoyo_path_info *filename,
				   const u8 perm, const char *operation,
				   const u8 mode)
{
	const bool is_enforce = (mode == 3);
	const char *msg = "<unknown>";
	int error = 0;

	if (!filename)
		return 0;
	error = tomoyo_check_file_acl(domain, filename, perm);
	if (error && perm == 4 &&
	    (domain->flags & TOMOYO_DOMAIN_FLAGS_IGNORE_GLOBAL_ALLOW_READ) == 0
	    && tomoyo_is_globally_readable_file(filename))
		error = 0;
	if (perm == 6)
		msg = tomoyo_sp2keyword(TOMOYO_TYPE_READ_WRITE_ACL);
	else if (perm == 4)
		msg = tomoyo_sp2keyword(TOMOYO_TYPE_READ_ACL);
	else if (perm == 2)
		msg = tomoyo_sp2keyword(TOMOYO_TYPE_WRITE_ACL);
	else if (perm == 1)
		msg = tomoyo_sp2keyword(TOMOYO_TYPE_EXECUTE_ACL);
	else
		BUG();
	if (!error)
		return 0;
	if (tomoyo_verbose_mode(domain))
		printk(KERN_WARNING "TOMOYO-%s: Access '%s(%s) %s' denied "
		       "for %s\n", tomoyo_get_msg(is_enforce), msg, operation,
		       filename->name, tomoyo_get_last_name(domain));
	if (is_enforce)
		return error;
	if (mode == 1 && tomoyo_domain_quota_is_ok(domain)) {
		/* Don't use patterns for execute permission. */
		const struct tomoyo_path_info *patterned_file = (perm != 1) ?
			tomoyo_get_file_pattern(filename) : filename;
		tomoyo_update_file_acl(patterned_file->name, perm,
				       domain, false);
	}
	return 0;
}

/**
 * tomoyo_write_file_policy - Update file related list.
 *
 * @data:      String to parse.
 * @domain:    Pointer to "struct tomoyo_domain_info".
 * @is_delete: True if it is a delete request.
 *
 * Returns 0 on success, negative value otherwise.
 */
int tomoyo_write_file_policy(char *data, struct tomoyo_domain_info *domain,
			     const bool is_delete)
{
	char *filename = strchr(data, ' ');
	char *filename2;
	unsigned int perm;
	u8 type;

	if (!filename)
		return -EINVAL;
	*filename++ = '\0';
	if (sscanf(data, "%u", &perm) == 1)
		return tomoyo_update_file_acl(filename, (u8) perm, domain,
					      is_delete);
	if (strncmp(data, "allow_", 6))
		goto out;
	data += 6;
	for (type = 0; type < TOMOYO_MAX_SINGLE_PATH_OPERATION; type++) {
		if (strcmp(data, tomoyo_sp_keyword[type]))
			continue;
		return tomoyo_update_single_path_acl(type, filename,
						     domain, is_delete);
	}
	filename2 = strchr(filename, ' ');
	if (!filename2)
		goto out;
	*filename2++ = '\0';
	for (type = 0; type < TOMOYO_MAX_DOUBLE_PATH_OPERATION; type++) {
		if (strcmp(data, tomoyo_dp_keyword[type]))
			continue;
		return tomoyo_update_double_path_acl(type, filename, filename2,
						     domain, is_delete);
	}
 out:
	return -EINVAL;
}

/**
 * tomoyo_update_single_path_acl - Update "struct tomoyo_single_path_acl_record" list.
 *
 * @type:      Type of operation.
 * @filename:  Filename.
 * @domain:    Pointer to "struct tomoyo_domain_info".
 * @is_delete: True if it is a delete request.
 *
 * Returns 0 on success, negative value otherwise.
 */
static int tomoyo_update_single_path_acl(const u8 type, const char *filename,
					 struct tomoyo_domain_info *
					 const domain, const bool is_delete)
{
	static const u16 rw_mask =
		(1 << TOMOYO_TYPE_READ_ACL) | (1 << TOMOYO_TYPE_WRITE_ACL);
	const struct tomoyo_path_info *saved_filename;
	struct tomoyo_acl_info *ptr;
	struct tomoyo_single_path_acl_record *acl;
	int error = -ENOMEM;
	const u16 perm = 1 << type;

	if (!domain)
		return -EINVAL;
	if (!tomoyo_is_correct_path(filename, 0, 0, 0, __func__))
		return -EINVAL;
	saved_filename = tomoyo_save_name(filename);
	if (!saved_filename)
		return -ENOMEM;
	down_write(&tomoyo_domain_acl_info_list_lock);
	if (is_delete)
		goto delete;
	list_for_each_entry(ptr, &domain->acl_info_list, list) {
		if (tomoyo_acl_type1(ptr) != TOMOYO_TYPE_SINGLE_PATH_ACL)
			continue;
		acl = container_of(ptr, struct tomoyo_single_path_acl_record,
				   head);
		if (acl->filename != saved_filename)
			continue;
		/* Special case. Clear all bits if marked as deleted. */
		if (ptr->type & TOMOYO_ACL_DELETED)
			acl->perm = 0;
		acl->perm |= perm;
		if ((acl->perm & rw_mask) == rw_mask)
			acl->perm |= 1 << TOMOYO_TYPE_READ_WRITE_ACL;
		else if (acl->perm & (1 << TOMOYO_TYPE_READ_WRITE_ACL))
			acl->perm |= rw_mask;
		ptr->type &= ~TOMOYO_ACL_DELETED;
		error = 0;
		goto out;
	}
	/* Not found. Append it to the tail. */
	acl = tomoyo_alloc_acl_element(TOMOYO_TYPE_SINGLE_PATH_ACL);
	if (!acl)
		goto out;
	acl->perm = perm;
	if (perm == (1 << TOMOYO_TYPE_READ_WRITE_ACL))
		acl->perm |= rw_mask;
	acl->filename = saved_filename;
	list_add_tail(&acl->head.list, &domain->acl_info_list);
	error = 0;
	goto out;
 delete:
	error = -ENOENT;
	list_for_each_entry(ptr, &domain->acl_info_list, list) {
		if (tomoyo_acl_type2(ptr) != TOMOYO_TYPE_SINGLE_PATH_ACL)
			continue;
		acl = container_of(ptr, struct tomoyo_single_path_acl_record,
				   head);
		if (acl->filename != saved_filename)
			continue;
		acl->perm &= ~perm;
		if ((acl->perm & rw_mask) != rw_mask)
			acl->perm &= ~(1 << TOMOYO_TYPE_READ_WRITE_ACL);
		else if (!(acl->perm & (1 << TOMOYO_TYPE_READ_WRITE_ACL)))
			acl->perm &= ~rw_mask;
		if (!acl->perm)
			ptr->type |= TOMOYO_ACL_DELETED;
		error = 0;
		break;
	}
 out:
	up_write(&tomoyo_domain_acl_info_list_lock);
	return error;
}

/**
 * tomoyo_update_double_path_acl - Update "struct tomoyo_double_path_acl_record" list.
 *
 * @type:      Type of operation.
 * @filename1: First filename.
 * @filename2: Second filename.
 * @domain:    Pointer to "struct tomoyo_domain_info".
 * @is_delete: True if it is a delete request.
 *
 * Returns 0 on success, negative value otherwise.
 */
static int tomoyo_update_double_path_acl(const u8 type, const char *filename1,
					 const char *filename2,
					 struct tomoyo_domain_info *
					 const domain, const bool is_delete)
{
	const struct tomoyo_path_info *saved_filename1;
	const struct tomoyo_path_info *saved_filename2;
	struct tomoyo_acl_info *ptr;
	struct tomoyo_double_path_acl_record *acl;
	int error = -ENOMEM;
	const u8 perm = 1 << type;

	if (!domain)
		return -EINVAL;
	if (!tomoyo_is_correct_path(filename1, 0, 0, 0, __func__) ||
	    !tomoyo_is_correct_path(filename2, 0, 0, 0, __func__))
		return -EINVAL;
	saved_filename1 = tomoyo_save_name(filename1);
	saved_filename2 = tomoyo_save_name(filename2);
	if (!saved_filename1 || !saved_filename2)
		return -ENOMEM;
	down_write(&tomoyo_domain_acl_info_list_lock);
	if (is_delete)
		goto delete;
	list_for_each_entry(ptr, &domain->acl_info_list, list) {
		if (tomoyo_acl_type1(ptr) != TOMOYO_TYPE_DOUBLE_PATH_ACL)
			continue;
		acl = container_of(ptr, struct tomoyo_double_path_acl_record,
				   head);
		if (acl->filename1 != saved_filename1 ||
		    acl->filename2 != saved_filename2)
			continue;
		/* Special case. Clear all bits if marked as deleted. */
		if (ptr->type & TOMOYO_ACL_DELETED)
			acl->perm = 0;
		acl->perm |= perm;
		ptr->type &= ~TOMOYO_ACL_DELETED;
		error = 0;
		goto out;
	}
	/* Not found. Append it to the tail. */
	acl = tomoyo_alloc_acl_element(TOMOYO_TYPE_DOUBLE_PATH_ACL);
	if (!acl)
		goto out;
	acl->perm = perm;
	acl->filename1 = saved_filename1;
	acl->filename2 = saved_filename2;
	list_add_tail(&acl->head.list, &domain->acl_info_list);
	error = 0;
	goto out;
 delete:
	error = -ENOENT;
	list_for_each_entry(ptr, &domain->acl_info_list, list) {
		if (tomoyo_acl_type2(ptr) != TOMOYO_TYPE_DOUBLE_PATH_ACL)
			continue;
		acl = container_of(ptr, struct tomoyo_double_path_acl_record,
				   head);
		if (acl->filename1 != saved_filename1 ||
		    acl->filename2 != saved_filename2)
			continue;
		acl->perm &= ~perm;
		if (!acl->perm)
			ptr->type |= TOMOYO_ACL_DELETED;
		error = 0;
		break;
	}
 out:
	up_write(&tomoyo_domain_acl_info_list_lock);
	return error;
}

/**
 * tomoyo_check_single_path_acl - Check permission for single path operation.
 *
 * @domain:   Pointer to "struct tomoyo_domain_info".
 * @type:     Type of operation.
 * @filename: Filename to check.
 *
 * Returns 0 on success, negative value otherwise.
 */
static int tomoyo_check_single_path_acl(struct tomoyo_domain_info *domain,
					const u8 type,
					const struct tomoyo_path_info *filename)
{
	if (!tomoyo_check_flags(domain, TOMOYO_MAC_FOR_FILE))
		return 0;
	return tomoyo_check_single_path_acl2(domain, filename, 1 << type, 1);
}

/**
 * tomoyo_check_double_path_acl - Check permission for double path operation.
 *
 * @domain:    Pointer to "struct tomoyo_domain_info".
 * @type:      Type of operation.
 * @filename1: First filename to check.
 * @filename2: Second filename to check.
 *
 * Returns 0 on success, -EPERM otherwise.
 */
static int tomoyo_check_double_path_acl(const struct tomoyo_domain_info *domain,
					const u8 type,
					const struct tomoyo_path_info *
					filename1,
					const struct tomoyo_path_info *
					filename2)
{
	struct tomoyo_acl_info *ptr;
	const u8 perm = 1 << type;
	int error = -EPERM;

	if (!tomoyo_check_flags(domain, TOMOYO_MAC_FOR_FILE))
		return 0;
	down_read(&tomoyo_domain_acl_info_list_lock);
	list_for_each_entry(ptr, &domain->acl_info_list, list) {
		struct tomoyo_double_path_acl_record *acl;
		if (tomoyo_acl_type2(ptr) != TOMOYO_TYPE_DOUBLE_PATH_ACL)
			continue;
		acl = container_of(ptr, struct tomoyo_double_path_acl_record,
				   head);
		if (!(acl->perm & perm))
			continue;
		if (!tomoyo_path_matches_pattern(filename1, acl->filename1))
			continue;
		if (!tomoyo_path_matches_pattern(filename2, acl->filename2))
			continue;
		error = 0;
		break;
	}
	up_read(&tomoyo_domain_acl_info_list_lock);
	return error;
}

/**
 * tomoyo_check_single_path_permission2 - Check permission for single path operation.
 *
 * @domain:    Pointer to "struct tomoyo_domain_info".
 * @operation: Type of operation.
 * @filename:  Filename to check.
 * @mode:      Access control mode.
 *
 * Returns 0 on success, negative value otherwise.
 */
static int tomoyo_check_single_path_permission2(struct tomoyo_domain_info *
						const domain, u8 operation,
						const struct tomoyo_path_info *
						filename, const u8 mode)
{
	const char *msg;
	int error;
	const bool is_enforce = (mode == 3);

	if (!mode)
		return 0;
 next:
	error = tomoyo_check_single_path_acl(domain, operation, filename);
	msg = tomoyo_sp2keyword(operation);
	if (!error)
		goto ok;
	if (tomoyo_verbose_mode(domain))
		printk(KERN_WARNING "TOMOYO-%s: Access '%s %s' denied for %s\n",
		       tomoyo_get_msg(is_enforce), msg, filename->name,
		       tomoyo_get_last_name(domain));
	if (mode == 1 && tomoyo_domain_quota_is_ok(domain)) {
		const char *name = tomoyo_get_file_pattern(filename)->name;
		tomoyo_update_single_path_acl(operation, name, domain, false);
	}
	if (!is_enforce)
		error = 0;
 ok:
	/*
	 * Since "allow_truncate" doesn't imply "allow_rewrite" permission,
	 * we need to check "allow_rewrite" permission if the filename is
	 * specified by "deny_rewrite" keyword.
	 */
	if (!error && operation == TOMOYO_TYPE_TRUNCATE_ACL &&
	    tomoyo_is_no_rewrite_file(filename)) {
		operation = TOMOYO_TYPE_REWRITE_ACL;
		goto next;
	}
	return error;
}

/**
 * tomoyo_check_exec_perm - Check permission for "execute".
 *
 * @domain:   Pointer to "struct tomoyo_domain_info".
 * @filename: Check permission for "execute".
 *
 * Returns 0 on success, negativevalue otherwise.
 */
int tomoyo_check_exec_perm(struct tomoyo_domain_info *domain,
			   const struct tomoyo_path_info *filename)
{
	const u8 mode = tomoyo_check_flags(domain, TOMOYO_MAC_FOR_FILE);

	if (!mode)
		return 0;
	return tomoyo_check_file_perm2(domain, filename, 1, "do_execve", mode);
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
	const u8 mode = tomoyo_check_flags(domain, TOMOYO_MAC_FOR_FILE);
	const bool is_enforce = (mode == 3);

	if (!mode || !path->mnt)
		return 0;
	if (acc_mode == 0)
		return 0;
	if (path->dentry->d_inode && S_ISDIR(path->dentry->d_inode->i_mode))
		/*
		 * I don't check directories here because mkdir() and rmdir()
		 * don't call me.
		 */
		return 0;
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
		error = tomoyo_check_single_path_permission2(domain,
						     TOMOYO_TYPE_REWRITE_ACL,
							     buf, mode);
	}
	if (!error)
		error = tomoyo_check_file_perm2(domain, buf, acc_mode, "open",
						mode);
	if (!error && (flag & O_TRUNC))
		error = tomoyo_check_single_path_permission2(domain,
						     TOMOYO_TYPE_TRUNCATE_ACL,
							     buf, mode);
 out:
	tomoyo_free(buf);
	if (!is_enforce)
		error = 0;
	return error;
}

/**
 * tomoyo_check_1path_perm - Check permission for "create", "unlink", "mkdir", "rmdir", "mkfifo", "mksock", "mkblock", "mkchar", "truncate" and "symlink".
 *
 * @domain:    Pointer to "struct tomoyo_domain_info".
 * @operation: Type of operation.
 * @path:      Pointer to "struct path".
 *
 * Returns 0 on success, negative value otherwise.
 */
int tomoyo_check_1path_perm(struct tomoyo_domain_info *domain,
			    const u8 operation, struct path *path)
{
	int error = -ENOMEM;
	struct tomoyo_path_info *buf;
	const u8 mode = tomoyo_check_flags(domain, TOMOYO_MAC_FOR_FILE);
	const bool is_enforce = (mode == 3);

	if (!mode || !path->mnt)
		return 0;
	buf = tomoyo_get_path(path);
	if (!buf)
		goto out;
	switch (operation) {
	case TOMOYO_TYPE_MKDIR_ACL:
	case TOMOYO_TYPE_RMDIR_ACL:
		if (!buf->is_dir) {
			/*
			 * tomoyo_get_path() reserves space for appending "/."
			 */
			strcat((char *) buf->name, "/");
			tomoyo_fill_path_info(buf);
		}
	}
	error = tomoyo_check_single_path_permission2(domain, operation, buf,
						     mode);
 out:
	tomoyo_free(buf);
	if (!is_enforce)
		error = 0;
	return error;
}

/**
 * tomoyo_check_rewrite_permission - Check permission for "rewrite".
 *
 * @domain: Pointer to "struct tomoyo_domain_info".
 * @filp: Pointer to "struct file".
 *
 * Returns 0 on success, negative value otherwise.
 */
int tomoyo_check_rewrite_permission(struct tomoyo_domain_info *domain,
				    struct file *filp)
{
	int error = -ENOMEM;
	const u8 mode = tomoyo_check_flags(domain, TOMOYO_MAC_FOR_FILE);
	const bool is_enforce = (mode == 3);
	struct tomoyo_path_info *buf;

	if (!mode || !filp->f_path.mnt)
		return 0;
	buf = tomoyo_get_path(&filp->f_path);
	if (!buf)
		goto out;
	if (!tomoyo_is_no_rewrite_file(buf)) {
		error = 0;
		goto out;
	}
	error = tomoyo_check_single_path_permission2(domain,
						     TOMOYO_TYPE_REWRITE_ACL,
						     buf, mode);
 out:
	tomoyo_free(buf);
	if (!is_enforce)
		error = 0;
	return error;
}

/**
 * tomoyo_check_2path_perm - Check permission for "rename" and "link".
 *
 * @domain:    Pointer to "struct tomoyo_domain_info".
 * @operation: Type of operation.
 * @path1:      Pointer to "struct path".
 * @path2:      Pointer to "struct path".
 *
 * Returns 0 on success, negative value otherwise.
 */
int tomoyo_check_2path_perm(struct tomoyo_domain_info * const domain,
			    const u8 operation, struct path *path1,
			    struct path *path2)
{
	int error = -ENOMEM;
	struct tomoyo_path_info *buf1, *buf2;
	const u8 mode = tomoyo_check_flags(domain, TOMOYO_MAC_FOR_FILE);
	const bool is_enforce = (mode == 3);
	const char *msg;

	if (!mode || !path1->mnt || !path2->mnt)
		return 0;
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
	error = tomoyo_check_double_path_acl(domain, operation, buf1, buf2);
	msg = tomoyo_dp2keyword(operation);
	if (!error)
		goto out;
	if (tomoyo_verbose_mode(domain))
		printk(KERN_WARNING "TOMOYO-%s: Access '%s %s %s' "
		       "denied for %s\n", tomoyo_get_msg(is_enforce),
		       msg, buf1->name, buf2->name,
		       tomoyo_get_last_name(domain));
	if (mode == 1 && tomoyo_domain_quota_is_ok(domain)) {
		const char *name1 = tomoyo_get_file_pattern(buf1)->name;
		const char *name2 = tomoyo_get_file_pattern(buf2)->name;
		tomoyo_update_double_path_acl(operation, name1, name2, domain,
					      false);
	}
 out:
	tomoyo_free(buf1);
	tomoyo_free(buf2);
	if (!is_enforce)
		error = 0;
	return error;
}
