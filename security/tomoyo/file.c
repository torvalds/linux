/*
 * security/tomoyo/file.c
 *
 * Pathname restriction functions.
 *
 * Copyright (C) 2005-2010  NTT DATA CORPORATION
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

static const u8 tomoyo_p2mac[TOMOYO_MAX_PATH_OPERATION] = {
	[TOMOYO_TYPE_READ_WRITE] = TOMOYO_MAC_FILE_OPEN,
	[TOMOYO_TYPE_EXECUTE]    = TOMOYO_MAC_FILE_EXECUTE,
	[TOMOYO_TYPE_READ]       = TOMOYO_MAC_FILE_OPEN,
	[TOMOYO_TYPE_WRITE]      = TOMOYO_MAC_FILE_OPEN,
	[TOMOYO_TYPE_UNLINK]     = TOMOYO_MAC_FILE_UNLINK,
	[TOMOYO_TYPE_RMDIR]      = TOMOYO_MAC_FILE_RMDIR,
	[TOMOYO_TYPE_TRUNCATE]   = TOMOYO_MAC_FILE_TRUNCATE,
	[TOMOYO_TYPE_SYMLINK]    = TOMOYO_MAC_FILE_SYMLINK,
	[TOMOYO_TYPE_REWRITE]    = TOMOYO_MAC_FILE_REWRITE,
	[TOMOYO_TYPE_CHROOT]     = TOMOYO_MAC_FILE_CHROOT,
	[TOMOYO_TYPE_UMOUNT]     = TOMOYO_MAC_FILE_UMOUNT,
};

static const u8 tomoyo_pnnn2mac[TOMOYO_MAX_PATH_NUMBER3_OPERATION] = {
	[TOMOYO_TYPE_MKBLOCK] = TOMOYO_MAC_FILE_MKBLOCK,
	[TOMOYO_TYPE_MKCHAR]  = TOMOYO_MAC_FILE_MKCHAR,
};

static const u8 tomoyo_pp2mac[TOMOYO_MAX_PATH2_OPERATION] = {
	[TOMOYO_TYPE_LINK]       = TOMOYO_MAC_FILE_LINK,
	[TOMOYO_TYPE_RENAME]     = TOMOYO_MAC_FILE_RENAME,
	[TOMOYO_TYPE_PIVOT_ROOT] = TOMOYO_MAC_FILE_PIVOT_ROOT,
};

static const u8 tomoyo_pn2mac[TOMOYO_MAX_PATH_NUMBER_OPERATION] = {
	[TOMOYO_TYPE_CREATE] = TOMOYO_MAC_FILE_CREATE,
	[TOMOYO_TYPE_MKDIR]  = TOMOYO_MAC_FILE_MKDIR,
	[TOMOYO_TYPE_MKFIFO] = TOMOYO_MAC_FILE_MKFIFO,
	[TOMOYO_TYPE_MKSOCK] = TOMOYO_MAC_FILE_MKSOCK,
	[TOMOYO_TYPE_IOCTL]  = TOMOYO_MAC_FILE_IOCTL,
	[TOMOYO_TYPE_CHMOD]  = TOMOYO_MAC_FILE_CHMOD,
	[TOMOYO_TYPE_CHOWN]  = TOMOYO_MAC_FILE_CHOWN,
	[TOMOYO_TYPE_CHGRP]  = TOMOYO_MAC_FILE_CHGRP,
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
		return tomoyo_path_matches_group(name, ptr->group);
	return tomoyo_path_matches_pattern(name, ptr->filename);
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

static void tomoyo_add_slash(struct tomoyo_path_info *buf)
{
	if (buf->is_dir)
		return;
	/*
	 * This is OK because tomoyo_encode() reserves space for appending "/".
	 */
	strcat((char *) buf->name, "/");
	tomoyo_fill_path_info(buf);
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
 * tomoyo_get_realpath - Get realpath.
 *
 * @buf:  Pointer to "struct tomoyo_path_info".
 * @path: Pointer to "struct path".
 *
 * Returns true on success, false otherwise.
 */
static bool tomoyo_get_realpath(struct tomoyo_path_info *buf, struct path *path)
{
	buf->name = tomoyo_realpath_from_path(path);
	if (buf->name) {
		tomoyo_fill_path_info(buf);
		return true;
	}
        return false;
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

static bool tomoyo_same_globally_readable(const struct tomoyo_acl_head *a,
					  const struct tomoyo_acl_head *b)
{
	return container_of(a, struct tomoyo_globally_readable_file_entry,
			    head)->filename ==
		container_of(b, struct tomoyo_globally_readable_file_entry,
			     head)->filename;
}

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
	struct tomoyo_globally_readable_file_entry e = { };
	int error;

	if (!tomoyo_is_correct_word(filename))
		return -EINVAL;
	e.filename = tomoyo_get_name(filename);
	if (!e.filename)
		return -ENOMEM;
	error = tomoyo_update_policy(&e.head, sizeof(e), is_delete,
				     &tomoyo_globally_readable_list,
				     tomoyo_same_globally_readable);
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

	list_for_each_entry_rcu(ptr, &tomoyo_globally_readable_list,
				head.list) {
		if (!ptr->head.is_deleted &&
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
				 head.list);
		if (ptr->head.is_deleted)
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

static bool tomoyo_same_pattern(const struct tomoyo_acl_head *a,
				const struct tomoyo_acl_head *b)
{
	return container_of(a, struct tomoyo_pattern_entry, head)->pattern ==
		container_of(b, struct tomoyo_pattern_entry, head)->pattern;
}

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
	struct tomoyo_pattern_entry e = { };
	int error;

	if (!tomoyo_is_correct_word(pattern))
		return -EINVAL;
	e.pattern = tomoyo_get_name(pattern);
	if (!e.pattern)
		return -ENOMEM;
	error = tomoyo_update_policy(&e.head, sizeof(e), is_delete,
				     &tomoyo_pattern_list,
				     tomoyo_same_pattern);
	tomoyo_put_name(e.pattern);
	return error;
}

/**
 * tomoyo_file_pattern - Get patterned pathname.
 *
 * @filename: The filename to find patterned pathname.
 *
 * Returns pointer to pathname pattern if matched, @filename otherwise.
 *
 * Caller holds tomoyo_read_lock().
 */
const char *tomoyo_file_pattern(const struct tomoyo_path_info *filename)
{
	struct tomoyo_pattern_entry *ptr;
	const struct tomoyo_path_info *pattern = NULL;

	list_for_each_entry_rcu(ptr, &tomoyo_pattern_list, head.list) {
		if (ptr->head.is_deleted)
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
	return filename->name;
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
		ptr = list_entry(pos, struct tomoyo_pattern_entry, head.list);
		if (ptr->head.is_deleted)
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

static bool tomoyo_same_no_rewrite(const struct tomoyo_acl_head *a,
				   const struct tomoyo_acl_head *b)
{
	return container_of(a, struct tomoyo_no_rewrite_entry, head)->pattern
		== container_of(b, struct tomoyo_no_rewrite_entry, head)
		->pattern;
}

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
	struct tomoyo_no_rewrite_entry e = { };
	int error;

	if (!tomoyo_is_correct_word(pattern))
		return -EINVAL;
	e.pattern = tomoyo_get_name(pattern);
	if (!e.pattern)
		return -ENOMEM;
	error = tomoyo_update_policy(&e.head, sizeof(e), is_delete,
				     &tomoyo_no_rewrite_list,
				     tomoyo_same_no_rewrite);
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

	list_for_each_entry_rcu(ptr, &tomoyo_no_rewrite_list, head.list) {
		if (ptr->head.is_deleted)
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
		ptr = list_entry(pos, struct tomoyo_no_rewrite_entry,
				 head.list);
		if (ptr->head.is_deleted)
			continue;
		done = tomoyo_io_printf(head, TOMOYO_KEYWORD_DENY_REWRITE
					"%s\n", ptr->pattern->name);
		if (!done)
			break;
	}
	return done;
}

/**
 * tomoyo_path_acl - Check permission for single path operation.
 *
 * @r:               Pointer to "struct tomoyo_request_info".
 * @filename:        Filename to check.
 * @perm:            Permission.
 *
 * Returns 0 on success, -EPERM otherwise.
 *
 * Caller holds tomoyo_read_lock().
 */
static int tomoyo_path_acl(const struct tomoyo_request_info *r,
			   const struct tomoyo_path_info *filename,
			   const u32 perm)
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
		    !tomoyo_compare_name_union(filename, &acl->name))
			continue;
		error = 0;
		break;
	}
	return error;
}

static bool tomoyo_same_path_acl(const struct tomoyo_acl_info *a,
				 const struct tomoyo_acl_info *b)
{
	const struct tomoyo_path_acl *p1 = container_of(a, typeof(*p1), head);
	const struct tomoyo_path_acl *p2 = container_of(b, typeof(*p2), head);
	return tomoyo_is_same_acl_head(&p1->head, &p2->head) &&
		tomoyo_is_same_name_union(&p1->name, &p2->name);
}

static bool tomoyo_merge_path_acl(struct tomoyo_acl_info *a,
				  struct tomoyo_acl_info *b,
				  const bool is_delete)
{
	u16 * const a_perm = &container_of(a, struct tomoyo_path_acl, head)
		->perm;
	u16 perm = *a_perm;
	const u16 b_perm = container_of(b, struct tomoyo_path_acl, head)->perm;
	if (is_delete) {
		perm &= ~b_perm;
		if ((perm & TOMOYO_RW_MASK) != TOMOYO_RW_MASK)
			perm &= ~(1 << TOMOYO_TYPE_READ_WRITE);
		else if (!(perm & (1 << TOMOYO_TYPE_READ_WRITE)))
			perm &= ~TOMOYO_RW_MASK;
	} else {
		perm |= b_perm;
		if ((perm & TOMOYO_RW_MASK) == TOMOYO_RW_MASK)
			perm |= (1 << TOMOYO_TYPE_READ_WRITE);
		else if (perm & (1 << TOMOYO_TYPE_READ_WRITE))
			perm |= TOMOYO_RW_MASK;
	}
	*a_perm = perm;
	return !perm;
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
				  struct tomoyo_domain_info * const domain,
				  const bool is_delete)
{
	struct tomoyo_path_acl e = {
		.head.type = TOMOYO_TYPE_PATH_ACL,
		.perm = 1 << type
	};
	int error;
	if (e.perm == (1 << TOMOYO_TYPE_READ_WRITE))
		e.perm |= TOMOYO_RW_MASK;
	if (!tomoyo_parse_name_union(filename, &e.name))
		return -EINVAL;
	error = tomoyo_update_domain(&e.head, sizeof(e), is_delete, domain,
				     tomoyo_same_path_acl,
				     tomoyo_merge_path_acl);
	tomoyo_put_name_union(&e.name);
	return error;
}

static bool tomoyo_same_path_number3_acl(const struct tomoyo_acl_info *a,
					 const struct tomoyo_acl_info *b)
{
	const struct tomoyo_path_number3_acl *p1 = container_of(a, typeof(*p1),
								head);
	const struct tomoyo_path_number3_acl *p2 = container_of(b, typeof(*p2),
								head);
	return tomoyo_is_same_acl_head(&p1->head, &p2->head)
		&& tomoyo_is_same_name_union(&p1->name, &p2->name)
		&& tomoyo_is_same_number_union(&p1->mode, &p2->mode)
		&& tomoyo_is_same_number_union(&p1->major, &p2->major)
		&& tomoyo_is_same_number_union(&p1->minor, &p2->minor);
}

static bool tomoyo_merge_path_number3_acl(struct tomoyo_acl_info *a,
					  struct tomoyo_acl_info *b,
					  const bool is_delete)
{
	u8 *const a_perm = &container_of(a, struct tomoyo_path_number3_acl,
					 head)->perm;
	u8 perm = *a_perm;
	const u8 b_perm = container_of(b, struct tomoyo_path_number3_acl, head)
		->perm;
	if (is_delete)
		perm &= ~b_perm;
	else
		perm |= b_perm;
	*a_perm = perm;
	return !perm;
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
 *
 * Caller holds tomoyo_read_lock().
 */
static int tomoyo_update_path_number3_acl(const u8 type, const char *filename,
					  char *mode, char *major, char *minor,
					  struct tomoyo_domain_info * const
					  domain, const bool is_delete)
{
	struct tomoyo_path_number3_acl e = {
		.head.type = TOMOYO_TYPE_PATH_NUMBER3_ACL,
		.perm = 1 << type
	};
	int error = is_delete ? -ENOENT : -ENOMEM;
	if (!tomoyo_parse_name_union(filename, &e.name) ||
	    !tomoyo_parse_number_union(mode, &e.mode) ||
	    !tomoyo_parse_number_union(major, &e.major) ||
	    !tomoyo_parse_number_union(minor, &e.minor))
		goto out;
	error = tomoyo_update_domain(&e.head, sizeof(e), is_delete, domain,
				     tomoyo_same_path_number3_acl,
				     tomoyo_merge_path_number3_acl);
 out:
	tomoyo_put_name_union(&e.name);
	tomoyo_put_number_union(&e.mode);
	tomoyo_put_number_union(&e.major);
	tomoyo_put_number_union(&e.minor);
	return error;
}

static bool tomoyo_same_path2_acl(const struct tomoyo_acl_info *a,
				  const struct tomoyo_acl_info *b)
{
	const struct tomoyo_path2_acl *p1 = container_of(a, typeof(*p1), head);
	const struct tomoyo_path2_acl *p2 = container_of(b, typeof(*p2), head);
	return tomoyo_is_same_acl_head(&p1->head, &p2->head)
		&& tomoyo_is_same_name_union(&p1->name1, &p2->name1)
		&& tomoyo_is_same_name_union(&p1->name2, &p2->name2);
}

static bool tomoyo_merge_path2_acl(struct tomoyo_acl_info *a,
				   struct tomoyo_acl_info *b,
				   const bool is_delete)
{
	u8 * const a_perm = &container_of(a, struct tomoyo_path2_acl, head)
		->perm;
	u8 perm = *a_perm;
	const u8 b_perm = container_of(b, struct tomoyo_path2_acl, head)->perm;
	if (is_delete)
		perm &= ~b_perm;
	else
		perm |= b_perm;
	*a_perm = perm;
	return !perm;
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
				   struct tomoyo_domain_info * const domain,
				   const bool is_delete)
{
	struct tomoyo_path2_acl e = {
		.head.type = TOMOYO_TYPE_PATH2_ACL,
		.perm = 1 << type
	};
	int error = is_delete ? -ENOENT : -ENOMEM;
	if (!tomoyo_parse_name_union(filename1, &e.name1) ||
	    !tomoyo_parse_name_union(filename2, &e.name2))
		goto out;
	error = tomoyo_update_domain(&e.head, sizeof(e), is_delete, domain,
				     tomoyo_same_path2_acl,
				     tomoyo_merge_path2_acl);
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
int tomoyo_path_permission(struct tomoyo_request_info *r, u8 operation,
			   const struct tomoyo_path_info *filename)
{
	const char *msg;
	int error;

 next:
	r->type = tomoyo_p2mac[operation];
	r->mode = tomoyo_get_mode(r->profile, r->type);
	if (r->mode == TOMOYO_CONFIG_DISABLED)
		return 0;
	r->param_type = TOMOYO_TYPE_PATH_ACL;
	r->param.path.filename = filename;
	r->param.path.operation = operation;
	do {
		error = tomoyo_path_acl(r, filename, 1 << operation);
		if (error && operation == TOMOYO_TYPE_READ &&
		    !r->domain->ignore_global_allow_read &&
		    tomoyo_is_globally_readable_file(filename))
			error = 0;
		if (!error)
			break;
		msg = tomoyo_path2keyword(operation);
		tomoyo_warn_log(r, "%s %s", msg, filename->name);
		error = tomoyo_supervisor(r, "allow_%s %s\n", msg,
					  tomoyo_file_pattern(filename));
		/*
		 * Do not retry for execute request, for alias may have
		 * changed.
		 */
	} while (error == TOMOYO_RETRY_REQUEST &&
		 operation != TOMOYO_TYPE_EXECUTE);
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

static bool tomoyo_same_path_number_acl(const struct tomoyo_acl_info *a,
					const struct tomoyo_acl_info *b)
{
	const struct tomoyo_path_number_acl *p1 = container_of(a, typeof(*p1),
							       head);
	const struct tomoyo_path_number_acl *p2 = container_of(b, typeof(*p2),
							       head);
	return tomoyo_is_same_acl_head(&p1->head, &p2->head)
		&& tomoyo_is_same_name_union(&p1->name, &p2->name)
		&& tomoyo_is_same_number_union(&p1->number, &p2->number);
}

static bool tomoyo_merge_path_number_acl(struct tomoyo_acl_info *a,
					 struct tomoyo_acl_info *b,
					 const bool is_delete)
{
	u8 * const a_perm = &container_of(a, struct tomoyo_path_number_acl,
					  head)->perm;
	u8 perm = *a_perm;
	const u8 b_perm = container_of(b, struct tomoyo_path_number_acl, head)
		->perm;
	if (is_delete)
		perm &= ~b_perm;
	else
		perm |= b_perm;
	*a_perm = perm;
	return !perm;
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
static int tomoyo_update_path_number_acl(const u8 type, const char *filename,
					 char *number,
					 struct tomoyo_domain_info * const
					 domain,
					 const bool is_delete)
{
	struct tomoyo_path_number_acl e = {
		.head.type = TOMOYO_TYPE_PATH_NUMBER_ACL,
		.perm = 1 << type
	};
	int error = is_delete ? -ENOENT : -ENOMEM;
	if (!tomoyo_parse_name_union(filename, &e.name))
		return -EINVAL;
	if (!tomoyo_parse_number_union(number, &e.number))
		goto out;
	error = tomoyo_update_domain(&e.head, sizeof(e), is_delete, domain,
				     tomoyo_same_path_number_acl,
				     tomoyo_merge_path_number_acl);
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
	const char *msg;

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
	r->param_type = TOMOYO_TYPE_PATH_NUMBER_ACL;
	r->param.path_number.operation = type;
	r->param.path_number.filename = filename;
	r->param.path_number.number = number;
	do {
		error = tomoyo_path_number_acl(r, type, filename, number);
		if (!error)
			break;
		msg = tomoyo_path_number2keyword(type);
		tomoyo_warn_log(r, "%s %s %s", msg, filename->name, buffer);
		error = tomoyo_supervisor(r, "allow_%s %s %s\n", msg,
					  tomoyo_file_pattern(filename),
					  buffer);
	} while (error == TOMOYO_RETRY_REQUEST);
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
	struct tomoyo_path_info buf;
	int idx;

	if (tomoyo_init_request_info(&r, NULL, tomoyo_pn2mac[type])
	    == TOMOYO_CONFIG_DISABLED || !path->mnt || !path->dentry)
		return 0;
	idx = tomoyo_read_lock();
	if (!tomoyo_get_realpath(&buf, path))
		goto out;
	if (type == TOMOYO_TYPE_MKDIR)
		tomoyo_add_slash(&buf);
	error = tomoyo_path_number_perm2(&r, type, &buf, number);
 out:
	kfree(buf.name);
	tomoyo_read_unlock(idx);
	if (r.mode != TOMOYO_CONFIG_ENFORCING)
		error = 0;
	return error;
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
	struct tomoyo_path_info buf;
	struct tomoyo_request_info r;
	int idx;

	if (!path->mnt ||
	    (path->dentry->d_inode && S_ISDIR(path->dentry->d_inode->i_mode)))
		return 0;
	buf.name = NULL;
	r.mode = TOMOYO_CONFIG_DISABLED;
	idx = tomoyo_read_lock();
	if (!tomoyo_get_realpath(&buf, path))
		goto out;
	error = 0;
	/*
	 * If the filename is specified by "deny_rewrite" keyword,
	 * we need to check "allow_rewrite" permission when the filename is not
	 * opened for append mode or the filename is truncated at open time.
	 */
	if ((acc_mode & MAY_WRITE) && !(flag & O_APPEND)
	    && tomoyo_init_request_info(&r, domain, TOMOYO_MAC_FILE_REWRITE)
	    != TOMOYO_CONFIG_DISABLED) {
		if (!tomoyo_get_realpath(&buf, path)) {
			error = -ENOMEM;
			goto out;
		}
		if (tomoyo_is_no_rewrite_file(&buf))
			error = tomoyo_path_permission(&r, TOMOYO_TYPE_REWRITE,
						       &buf);
	}
	if (!error && acc_mode &&
	    tomoyo_init_request_info(&r, domain, TOMOYO_MAC_FILE_OPEN)
	    != TOMOYO_CONFIG_DISABLED) {
		u8 operation;
		if (!buf.name && !tomoyo_get_realpath(&buf, path)) {
			error = -ENOMEM;
			goto out;
		}
		if (acc_mode == (MAY_READ | MAY_WRITE))
			operation = TOMOYO_TYPE_READ_WRITE;
		else if (acc_mode == MAY_READ)
			operation = TOMOYO_TYPE_READ;
		else
			operation = TOMOYO_TYPE_WRITE;
		error = tomoyo_path_permission(&r, operation, &buf);
	}
 out:
	kfree(buf.name);
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
	struct tomoyo_path_info buf;
	struct tomoyo_request_info r;
	int idx;

	if (!path->mnt)
		return 0;
	if (tomoyo_init_request_info(&r, NULL, tomoyo_p2mac[operation])
	    == TOMOYO_CONFIG_DISABLED)
		return 0;
	buf.name = NULL;
	idx = tomoyo_read_lock();
	if (!tomoyo_get_realpath(&buf, path))
		goto out;
	switch (operation) {
	case TOMOYO_TYPE_REWRITE:
		if (!tomoyo_is_no_rewrite_file(&buf)) {
			error = 0;
			goto out;
		}
		break;
	case TOMOYO_TYPE_RMDIR:
	case TOMOYO_TYPE_CHROOT:
	case TOMOYO_TYPE_UMOUNT:
		tomoyo_add_slash(&buf);
		break;
	}
	error = tomoyo_path_permission(&r, operation, &buf);
 out:
	kfree(buf.name);
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
	const char *msg;
	const unsigned int major = MAJOR(dev);
	const unsigned int minor = MINOR(dev);

	do {
		error = tomoyo_path_number3_acl(r, filename, 1 << operation,
						mode, major, minor);
		if (!error)
			break;
		msg = tomoyo_path_number32keyword(operation);
		tomoyo_warn_log(r, "%s %s 0%o %u %u", msg, filename->name,
				mode, major, minor);
		error = tomoyo_supervisor(r, "allow_%s %s 0%o %u %u\n", msg,
					  tomoyo_file_pattern(filename), mode,
					  major, minor);
	} while (error == TOMOYO_RETRY_REQUEST);
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
	struct tomoyo_path_info buf;
	int idx;

	if (!path->mnt ||
	    tomoyo_init_request_info(&r, NULL, tomoyo_pnnn2mac[operation])
	    == TOMOYO_CONFIG_DISABLED)
		return 0;
	idx = tomoyo_read_lock();
	error = -ENOMEM;
	if (tomoyo_get_realpath(&buf, path)) {
		dev = new_decode_dev(dev);
		r.param_type = TOMOYO_TYPE_PATH_NUMBER3_ACL;
		r.param.mkdev.filename = &buf;
		r.param.mkdev.operation = operation;
		r.param.mkdev.mode = mode;
		r.param.mkdev.major = MAJOR(dev);
		r.param.mkdev.minor = MINOR(dev);
		error = tomoyo_path_number3_perm2(&r, operation, &buf, mode,
						  dev);
		kfree(buf.name);
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
	const char *msg;
	struct tomoyo_path_info buf1;
	struct tomoyo_path_info buf2;
	struct tomoyo_request_info r;
	int idx;

	if (!path1->mnt || !path2->mnt ||
	    tomoyo_init_request_info(&r, NULL, tomoyo_pp2mac[operation])
	    == TOMOYO_CONFIG_DISABLED)
		return 0;
	buf1.name = NULL;
	buf2.name = NULL;
	idx = tomoyo_read_lock();
	if (!tomoyo_get_realpath(&buf1, path1) ||
	    !tomoyo_get_realpath(&buf2, path2))
		goto out;
	switch (operation) {
		struct dentry *dentry;
	case TOMOYO_TYPE_RENAME:
        case TOMOYO_TYPE_LINK:
		dentry = path1->dentry;
	        if (!dentry->d_inode || !S_ISDIR(dentry->d_inode->i_mode))
                        break;
                /* fall through */
        case TOMOYO_TYPE_PIVOT_ROOT:
                tomoyo_add_slash(&buf1);
                tomoyo_add_slash(&buf2);
		break;
        }
	r.param_type = TOMOYO_TYPE_PATH2_ACL;
	r.param.path2.operation = operation;
	r.param.path2.filename1 = &buf1;
	r.param.path2.filename2 = &buf2;
	do {
		error = tomoyo_path2_acl(&r, operation, &buf1, &buf2);
		if (!error)
			break;
		msg = tomoyo_path22keyword(operation);
		tomoyo_warn_log(&r, "%s %s %s", msg, buf1.name, buf2.name);
		error = tomoyo_supervisor(&r, "allow_%s %s %s\n", msg,
					  tomoyo_file_pattern(&buf1),
					  tomoyo_file_pattern(&buf2));
        } while (error == TOMOYO_RETRY_REQUEST);
 out:
	kfree(buf1.name);
	kfree(buf2.name);
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
	if (strncmp(w[0], "allow_", 6))
		goto out;
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
