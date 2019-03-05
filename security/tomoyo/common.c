// SPDX-License-Identifier: GPL-2.0
/*
 * security/tomoyo/common.c
 *
 * Copyright (C) 2005-2011  NTT DATA CORPORATION
 */

#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/security.h>
#include "common.h"

/* String table for operation mode. */
const char * const tomoyo_mode[TOMOYO_CONFIG_MAX_MODE] = {
	[TOMOYO_CONFIG_DISABLED]   = "disabled",
	[TOMOYO_CONFIG_LEARNING]   = "learning",
	[TOMOYO_CONFIG_PERMISSIVE] = "permissive",
	[TOMOYO_CONFIG_ENFORCING]  = "enforcing"
};

/* String table for /sys/kernel/security/tomoyo/profile */
const char * const tomoyo_mac_keywords[TOMOYO_MAX_MAC_INDEX
				       + TOMOYO_MAX_MAC_CATEGORY_INDEX] = {
	/* CONFIG::file group */
	[TOMOYO_MAC_FILE_EXECUTE]    = "execute",
	[TOMOYO_MAC_FILE_OPEN]       = "open",
	[TOMOYO_MAC_FILE_CREATE]     = "create",
	[TOMOYO_MAC_FILE_UNLINK]     = "unlink",
	[TOMOYO_MAC_FILE_GETATTR]    = "getattr",
	[TOMOYO_MAC_FILE_MKDIR]      = "mkdir",
	[TOMOYO_MAC_FILE_RMDIR]      = "rmdir",
	[TOMOYO_MAC_FILE_MKFIFO]     = "mkfifo",
	[TOMOYO_MAC_FILE_MKSOCK]     = "mksock",
	[TOMOYO_MAC_FILE_TRUNCATE]   = "truncate",
	[TOMOYO_MAC_FILE_SYMLINK]    = "symlink",
	[TOMOYO_MAC_FILE_MKBLOCK]    = "mkblock",
	[TOMOYO_MAC_FILE_MKCHAR]     = "mkchar",
	[TOMOYO_MAC_FILE_LINK]       = "link",
	[TOMOYO_MAC_FILE_RENAME]     = "rename",
	[TOMOYO_MAC_FILE_CHMOD]      = "chmod",
	[TOMOYO_MAC_FILE_CHOWN]      = "chown",
	[TOMOYO_MAC_FILE_CHGRP]      = "chgrp",
	[TOMOYO_MAC_FILE_IOCTL]      = "ioctl",
	[TOMOYO_MAC_FILE_CHROOT]     = "chroot",
	[TOMOYO_MAC_FILE_MOUNT]      = "mount",
	[TOMOYO_MAC_FILE_UMOUNT]     = "unmount",
	[TOMOYO_MAC_FILE_PIVOT_ROOT] = "pivot_root",
	/* CONFIG::network group */
	[TOMOYO_MAC_NETWORK_INET_STREAM_BIND]       = "inet_stream_bind",
	[TOMOYO_MAC_NETWORK_INET_STREAM_LISTEN]     = "inet_stream_listen",
	[TOMOYO_MAC_NETWORK_INET_STREAM_CONNECT]    = "inet_stream_connect",
	[TOMOYO_MAC_NETWORK_INET_DGRAM_BIND]        = "inet_dgram_bind",
	[TOMOYO_MAC_NETWORK_INET_DGRAM_SEND]        = "inet_dgram_send",
	[TOMOYO_MAC_NETWORK_INET_RAW_BIND]          = "inet_raw_bind",
	[TOMOYO_MAC_NETWORK_INET_RAW_SEND]          = "inet_raw_send",
	[TOMOYO_MAC_NETWORK_UNIX_STREAM_BIND]       = "unix_stream_bind",
	[TOMOYO_MAC_NETWORK_UNIX_STREAM_LISTEN]     = "unix_stream_listen",
	[TOMOYO_MAC_NETWORK_UNIX_STREAM_CONNECT]    = "unix_stream_connect",
	[TOMOYO_MAC_NETWORK_UNIX_DGRAM_BIND]        = "unix_dgram_bind",
	[TOMOYO_MAC_NETWORK_UNIX_DGRAM_SEND]        = "unix_dgram_send",
	[TOMOYO_MAC_NETWORK_UNIX_SEQPACKET_BIND]    = "unix_seqpacket_bind",
	[TOMOYO_MAC_NETWORK_UNIX_SEQPACKET_LISTEN]  = "unix_seqpacket_listen",
	[TOMOYO_MAC_NETWORK_UNIX_SEQPACKET_CONNECT] = "unix_seqpacket_connect",
	/* CONFIG::misc group */
	[TOMOYO_MAC_ENVIRON] = "env",
	/* CONFIG group */
	[TOMOYO_MAX_MAC_INDEX + TOMOYO_MAC_CATEGORY_FILE] = "file",
	[TOMOYO_MAX_MAC_INDEX + TOMOYO_MAC_CATEGORY_NETWORK] = "network",
	[TOMOYO_MAX_MAC_INDEX + TOMOYO_MAC_CATEGORY_MISC] = "misc",
};

/* String table for conditions. */
const char * const tomoyo_condition_keyword[TOMOYO_MAX_CONDITION_KEYWORD] = {
	[TOMOYO_TASK_UID]             = "task.uid",
	[TOMOYO_TASK_EUID]            = "task.euid",
	[TOMOYO_TASK_SUID]            = "task.suid",
	[TOMOYO_TASK_FSUID]           = "task.fsuid",
	[TOMOYO_TASK_GID]             = "task.gid",
	[TOMOYO_TASK_EGID]            = "task.egid",
	[TOMOYO_TASK_SGID]            = "task.sgid",
	[TOMOYO_TASK_FSGID]           = "task.fsgid",
	[TOMOYO_TASK_PID]             = "task.pid",
	[TOMOYO_TASK_PPID]            = "task.ppid",
	[TOMOYO_EXEC_ARGC]            = "exec.argc",
	[TOMOYO_EXEC_ENVC]            = "exec.envc",
	[TOMOYO_TYPE_IS_SOCKET]       = "socket",
	[TOMOYO_TYPE_IS_SYMLINK]      = "symlink",
	[TOMOYO_TYPE_IS_FILE]         = "file",
	[TOMOYO_TYPE_IS_BLOCK_DEV]    = "block",
	[TOMOYO_TYPE_IS_DIRECTORY]    = "directory",
	[TOMOYO_TYPE_IS_CHAR_DEV]     = "char",
	[TOMOYO_TYPE_IS_FIFO]         = "fifo",
	[TOMOYO_MODE_SETUID]          = "setuid",
	[TOMOYO_MODE_SETGID]          = "setgid",
	[TOMOYO_MODE_STICKY]          = "sticky",
	[TOMOYO_MODE_OWNER_READ]      = "owner_read",
	[TOMOYO_MODE_OWNER_WRITE]     = "owner_write",
	[TOMOYO_MODE_OWNER_EXECUTE]   = "owner_execute",
	[TOMOYO_MODE_GROUP_READ]      = "group_read",
	[TOMOYO_MODE_GROUP_WRITE]     = "group_write",
	[TOMOYO_MODE_GROUP_EXECUTE]   = "group_execute",
	[TOMOYO_MODE_OTHERS_READ]     = "others_read",
	[TOMOYO_MODE_OTHERS_WRITE]    = "others_write",
	[TOMOYO_MODE_OTHERS_EXECUTE]  = "others_execute",
	[TOMOYO_EXEC_REALPATH]        = "exec.realpath",
	[TOMOYO_SYMLINK_TARGET]       = "symlink.target",
	[TOMOYO_PATH1_UID]            = "path1.uid",
	[TOMOYO_PATH1_GID]            = "path1.gid",
	[TOMOYO_PATH1_INO]            = "path1.ino",
	[TOMOYO_PATH1_MAJOR]          = "path1.major",
	[TOMOYO_PATH1_MINOR]          = "path1.minor",
	[TOMOYO_PATH1_PERM]           = "path1.perm",
	[TOMOYO_PATH1_TYPE]           = "path1.type",
	[TOMOYO_PATH1_DEV_MAJOR]      = "path1.dev_major",
	[TOMOYO_PATH1_DEV_MINOR]      = "path1.dev_minor",
	[TOMOYO_PATH2_UID]            = "path2.uid",
	[TOMOYO_PATH2_GID]            = "path2.gid",
	[TOMOYO_PATH2_INO]            = "path2.ino",
	[TOMOYO_PATH2_MAJOR]          = "path2.major",
	[TOMOYO_PATH2_MINOR]          = "path2.minor",
	[TOMOYO_PATH2_PERM]           = "path2.perm",
	[TOMOYO_PATH2_TYPE]           = "path2.type",
	[TOMOYO_PATH2_DEV_MAJOR]      = "path2.dev_major",
	[TOMOYO_PATH2_DEV_MINOR]      = "path2.dev_minor",
	[TOMOYO_PATH1_PARENT_UID]     = "path1.parent.uid",
	[TOMOYO_PATH1_PARENT_GID]     = "path1.parent.gid",
	[TOMOYO_PATH1_PARENT_INO]     = "path1.parent.ino",
	[TOMOYO_PATH1_PARENT_PERM]    = "path1.parent.perm",
	[TOMOYO_PATH2_PARENT_UID]     = "path2.parent.uid",
	[TOMOYO_PATH2_PARENT_GID]     = "path2.parent.gid",
	[TOMOYO_PATH2_PARENT_INO]     = "path2.parent.ino",
	[TOMOYO_PATH2_PARENT_PERM]    = "path2.parent.perm",
};

/* String table for PREFERENCE keyword. */
static const char * const tomoyo_pref_keywords[TOMOYO_MAX_PREF] = {
	[TOMOYO_PREF_MAX_AUDIT_LOG]      = "max_audit_log",
	[TOMOYO_PREF_MAX_LEARNING_ENTRY] = "max_learning_entry",
};

/* String table for path operation. */
const char * const tomoyo_path_keyword[TOMOYO_MAX_PATH_OPERATION] = {
	[TOMOYO_TYPE_EXECUTE]    = "execute",
	[TOMOYO_TYPE_READ]       = "read",
	[TOMOYO_TYPE_WRITE]      = "write",
	[TOMOYO_TYPE_APPEND]     = "append",
	[TOMOYO_TYPE_UNLINK]     = "unlink",
	[TOMOYO_TYPE_GETATTR]    = "getattr",
	[TOMOYO_TYPE_RMDIR]      = "rmdir",
	[TOMOYO_TYPE_TRUNCATE]   = "truncate",
	[TOMOYO_TYPE_SYMLINK]    = "symlink",
	[TOMOYO_TYPE_CHROOT]     = "chroot",
	[TOMOYO_TYPE_UMOUNT]     = "unmount",
};

/* String table for socket's operation. */
const char * const tomoyo_socket_keyword[TOMOYO_MAX_NETWORK_OPERATION] = {
	[TOMOYO_NETWORK_BIND]    = "bind",
	[TOMOYO_NETWORK_LISTEN]  = "listen",
	[TOMOYO_NETWORK_CONNECT] = "connect",
	[TOMOYO_NETWORK_SEND]    = "send",
};

/* String table for categories. */
static const char * const tomoyo_category_keywords
[TOMOYO_MAX_MAC_CATEGORY_INDEX] = {
	[TOMOYO_MAC_CATEGORY_FILE]    = "file",
	[TOMOYO_MAC_CATEGORY_NETWORK] = "network",
	[TOMOYO_MAC_CATEGORY_MISC]    = "misc",
};

/* Permit policy management by non-root user? */
static bool tomoyo_manage_by_non_root;

/* Utility functions. */

/**
 * tomoyo_yesno - Return "yes" or "no".
 *
 * @value: Bool value.
 */
const char *tomoyo_yesno(const unsigned int value)
{
	return value ? "yes" : "no";
}

/**
 * tomoyo_addprintf - strncat()-like-snprintf().
 *
 * @buffer: Buffer to write to. Must be '\0'-terminated.
 * @len:    Size of @buffer.
 * @fmt:    The printf()'s format string, followed by parameters.
 *
 * Returns nothing.
 */
static void tomoyo_addprintf(char *buffer, int len, const char *fmt, ...)
{
	va_list args;
	const int pos = strlen(buffer);
	va_start(args, fmt);
	vsnprintf(buffer + pos, len - pos - 1, fmt, args);
	va_end(args);
}

/**
 * tomoyo_flush - Flush queued string to userspace's buffer.
 *
 * @head:   Pointer to "struct tomoyo_io_buffer".
 *
 * Returns true if all data was flushed, false otherwise.
 */
static bool tomoyo_flush(struct tomoyo_io_buffer *head)
{
	while (head->r.w_pos) {
		const char *w = head->r.w[0];
		size_t len = strlen(w);
		if (len) {
			if (len > head->read_user_buf_avail)
				len = head->read_user_buf_avail;
			if (!len)
				return false;
			if (copy_to_user(head->read_user_buf, w, len))
				return false;
			head->read_user_buf_avail -= len;
			head->read_user_buf += len;
			w += len;
		}
		head->r.w[0] = w;
		if (*w)
			return false;
		/* Add '\0' for audit logs and query. */
		if (head->poll) {
			if (!head->read_user_buf_avail ||
			    copy_to_user(head->read_user_buf, "", 1))
				return false;
			head->read_user_buf_avail--;
			head->read_user_buf++;
		}
		head->r.w_pos--;
		for (len = 0; len < head->r.w_pos; len++)
			head->r.w[len] = head->r.w[len + 1];
	}
	head->r.avail = 0;
	return true;
}

/**
 * tomoyo_set_string - Queue string to "struct tomoyo_io_buffer" structure.
 *
 * @head:   Pointer to "struct tomoyo_io_buffer".
 * @string: String to print.
 *
 * Note that @string has to be kept valid until @head is kfree()d.
 * This means that char[] allocated on stack memory cannot be passed to
 * this function. Use tomoyo_io_printf() for char[] allocated on stack memory.
 */
static void tomoyo_set_string(struct tomoyo_io_buffer *head, const char *string)
{
	if (head->r.w_pos < TOMOYO_MAX_IO_READ_QUEUE) {
		head->r.w[head->r.w_pos++] = string;
		tomoyo_flush(head);
	} else
		WARN_ON(1);
}

static void tomoyo_io_printf(struct tomoyo_io_buffer *head, const char *fmt,
			     ...) __printf(2, 3);

/**
 * tomoyo_io_printf - printf() to "struct tomoyo_io_buffer" structure.
 *
 * @head: Pointer to "struct tomoyo_io_buffer".
 * @fmt:  The printf()'s format string, followed by parameters.
 */
static void tomoyo_io_printf(struct tomoyo_io_buffer *head, const char *fmt,
			     ...)
{
	va_list args;
	size_t len;
	size_t pos = head->r.avail;
	int size = head->readbuf_size - pos;
	if (size <= 0)
		return;
	va_start(args, fmt);
	len = vsnprintf(head->read_buf + pos, size, fmt, args) + 1;
	va_end(args);
	if (pos + len >= head->readbuf_size) {
		WARN_ON(1);
		return;
	}
	head->r.avail += len;
	tomoyo_set_string(head, head->read_buf + pos);
}

/**
 * tomoyo_set_space - Put a space to "struct tomoyo_io_buffer" structure.
 *
 * @head: Pointer to "struct tomoyo_io_buffer".
 *
 * Returns nothing.
 */
static void tomoyo_set_space(struct tomoyo_io_buffer *head)
{
	tomoyo_set_string(head, " ");
}

/**
 * tomoyo_set_lf - Put a line feed to "struct tomoyo_io_buffer" structure.
 *
 * @head: Pointer to "struct tomoyo_io_buffer".
 *
 * Returns nothing.
 */
static bool tomoyo_set_lf(struct tomoyo_io_buffer *head)
{
	tomoyo_set_string(head, "\n");
	return !head->r.w_pos;
}

/**
 * tomoyo_set_slash - Put a shash to "struct tomoyo_io_buffer" structure.
 *
 * @head: Pointer to "struct tomoyo_io_buffer".
 *
 * Returns nothing.
 */
static void tomoyo_set_slash(struct tomoyo_io_buffer *head)
{
	tomoyo_set_string(head, "/");
}

/* List of namespaces. */
LIST_HEAD(tomoyo_namespace_list);
/* True if namespace other than tomoyo_kernel_namespace is defined. */
static bool tomoyo_namespace_enabled;

/**
 * tomoyo_init_policy_namespace - Initialize namespace.
 *
 * @ns: Pointer to "struct tomoyo_policy_namespace".
 *
 * Returns nothing.
 */
void tomoyo_init_policy_namespace(struct tomoyo_policy_namespace *ns)
{
	unsigned int idx;
	for (idx = 0; idx < TOMOYO_MAX_ACL_GROUPS; idx++)
		INIT_LIST_HEAD(&ns->acl_group[idx]);
	for (idx = 0; idx < TOMOYO_MAX_GROUP; idx++)
		INIT_LIST_HEAD(&ns->group_list[idx]);
	for (idx = 0; idx < TOMOYO_MAX_POLICY; idx++)
		INIT_LIST_HEAD(&ns->policy_list[idx]);
	ns->profile_version = 20110903;
	tomoyo_namespace_enabled = !list_empty(&tomoyo_namespace_list);
	list_add_tail_rcu(&ns->namespace_list, &tomoyo_namespace_list);
}

/**
 * tomoyo_print_namespace - Print namespace header.
 *
 * @head: Pointer to "struct tomoyo_io_buffer".
 *
 * Returns nothing.
 */
static void tomoyo_print_namespace(struct tomoyo_io_buffer *head)
{
	if (!tomoyo_namespace_enabled)
		return;
	tomoyo_set_string(head,
			  container_of(head->r.ns,
				       struct tomoyo_policy_namespace,
				       namespace_list)->name);
	tomoyo_set_space(head);
}

/**
 * tomoyo_print_name_union - Print a tomoyo_name_union.
 *
 * @head: Pointer to "struct tomoyo_io_buffer".
 * @ptr:  Pointer to "struct tomoyo_name_union".
 */
static void tomoyo_print_name_union(struct tomoyo_io_buffer *head,
				    const struct tomoyo_name_union *ptr)
{
	tomoyo_set_space(head);
	if (ptr->group) {
		tomoyo_set_string(head, "@");
		tomoyo_set_string(head, ptr->group->group_name->name);
	} else {
		tomoyo_set_string(head, ptr->filename->name);
	}
}

/**
 * tomoyo_print_name_union_quoted - Print a tomoyo_name_union with a quote.
 *
 * @head: Pointer to "struct tomoyo_io_buffer".
 * @ptr:  Pointer to "struct tomoyo_name_union".
 *
 * Returns nothing.
 */
static void tomoyo_print_name_union_quoted(struct tomoyo_io_buffer *head,
					   const struct tomoyo_name_union *ptr)
{
	if (ptr->group) {
		tomoyo_set_string(head, "@");
		tomoyo_set_string(head, ptr->group->group_name->name);
	} else {
		tomoyo_set_string(head, "\"");
		tomoyo_set_string(head, ptr->filename->name);
		tomoyo_set_string(head, "\"");
	}
}

/**
 * tomoyo_print_number_union_nospace - Print a tomoyo_number_union without a space.
 *
 * @head: Pointer to "struct tomoyo_io_buffer".
 * @ptr:  Pointer to "struct tomoyo_number_union".
 *
 * Returns nothing.
 */
static void tomoyo_print_number_union_nospace
(struct tomoyo_io_buffer *head, const struct tomoyo_number_union *ptr)
{
	if (ptr->group) {
		tomoyo_set_string(head, "@");
		tomoyo_set_string(head, ptr->group->group_name->name);
	} else {
		int i;
		unsigned long min = ptr->values[0];
		const unsigned long max = ptr->values[1];
		u8 min_type = ptr->value_type[0];
		const u8 max_type = ptr->value_type[1];
		char buffer[128];
		buffer[0] = '\0';
		for (i = 0; i < 2; i++) {
			switch (min_type) {
			case TOMOYO_VALUE_TYPE_HEXADECIMAL:
				tomoyo_addprintf(buffer, sizeof(buffer),
						 "0x%lX", min);
				break;
			case TOMOYO_VALUE_TYPE_OCTAL:
				tomoyo_addprintf(buffer, sizeof(buffer),
						 "0%lo", min);
				break;
			default:
				tomoyo_addprintf(buffer, sizeof(buffer), "%lu",
						 min);
				break;
			}
			if (min == max && min_type == max_type)
				break;
			tomoyo_addprintf(buffer, sizeof(buffer), "-");
			min_type = max_type;
			min = max;
		}
		tomoyo_io_printf(head, "%s", buffer);
	}
}

/**
 * tomoyo_print_number_union - Print a tomoyo_number_union.
 *
 * @head: Pointer to "struct tomoyo_io_buffer".
 * @ptr:  Pointer to "struct tomoyo_number_union".
 *
 * Returns nothing.
 */
static void tomoyo_print_number_union(struct tomoyo_io_buffer *head,
				      const struct tomoyo_number_union *ptr)
{
	tomoyo_set_space(head);
	tomoyo_print_number_union_nospace(head, ptr);
}

/**
 * tomoyo_assign_profile - Create a new profile.
 *
 * @ns:      Pointer to "struct tomoyo_policy_namespace".
 * @profile: Profile number to create.
 *
 * Returns pointer to "struct tomoyo_profile" on success, NULL otherwise.
 */
static struct tomoyo_profile *tomoyo_assign_profile
(struct tomoyo_policy_namespace *ns, const unsigned int profile)
{
	struct tomoyo_profile *ptr;
	struct tomoyo_profile *entry;
	if (profile >= TOMOYO_MAX_PROFILES)
		return NULL;
	ptr = ns->profile_ptr[profile];
	if (ptr)
		return ptr;
	entry = kzalloc(sizeof(*entry), GFP_NOFS);
	if (mutex_lock_interruptible(&tomoyo_policy_lock))
		goto out;
	ptr = ns->profile_ptr[profile];
	if (!ptr && tomoyo_memory_ok(entry)) {
		ptr = entry;
		ptr->default_config = TOMOYO_CONFIG_DISABLED |
			TOMOYO_CONFIG_WANT_GRANT_LOG |
			TOMOYO_CONFIG_WANT_REJECT_LOG;
		memset(ptr->config, TOMOYO_CONFIG_USE_DEFAULT,
		       sizeof(ptr->config));
		ptr->pref[TOMOYO_PREF_MAX_AUDIT_LOG] =
			CONFIG_SECURITY_TOMOYO_MAX_AUDIT_LOG;
		ptr->pref[TOMOYO_PREF_MAX_LEARNING_ENTRY] =
			CONFIG_SECURITY_TOMOYO_MAX_ACCEPT_ENTRY;
		mb(); /* Avoid out-of-order execution. */
		ns->profile_ptr[profile] = ptr;
		entry = NULL;
	}
	mutex_unlock(&tomoyo_policy_lock);
 out:
	kfree(entry);
	return ptr;
}

/**
 * tomoyo_profile - Find a profile.
 *
 * @ns:      Pointer to "struct tomoyo_policy_namespace".
 * @profile: Profile number to find.
 *
 * Returns pointer to "struct tomoyo_profile".
 */
struct tomoyo_profile *tomoyo_profile(const struct tomoyo_policy_namespace *ns,
				      const u8 profile)
{
	static struct tomoyo_profile tomoyo_null_profile;
	struct tomoyo_profile *ptr = ns->profile_ptr[profile];
	if (!ptr)
		ptr = &tomoyo_null_profile;
	return ptr;
}

/**
 * tomoyo_find_yesno - Find values for specified keyword.
 *
 * @string: String to check.
 * @find:   Name of keyword.
 *
 * Returns 1 if "@find=yes" was found, 0 if "@find=no" was found, -1 otherwise.
 */
static s8 tomoyo_find_yesno(const char *string, const char *find)
{
	const char *cp = strstr(string, find);
	if (cp) {
		cp += strlen(find);
		if (!strncmp(cp, "=yes", 4))
			return 1;
		else if (!strncmp(cp, "=no", 3))
			return 0;
	}
	return -1;
}

/**
 * tomoyo_set_uint - Set value for specified preference.
 *
 * @i:      Pointer to "unsigned int".
 * @string: String to check.
 * @find:   Name of keyword.
 *
 * Returns nothing.
 */
static void tomoyo_set_uint(unsigned int *i, const char *string,
			    const char *find)
{
	const char *cp = strstr(string, find);
	if (cp)
		sscanf(cp + strlen(find), "=%u", i);
}

/**
 * tomoyo_set_mode - Set mode for specified profile.
 *
 * @name:    Name of functionality.
 * @value:   Mode for @name.
 * @profile: Pointer to "struct tomoyo_profile".
 *
 * Returns 0 on success, negative value otherwise.
 */
static int tomoyo_set_mode(char *name, const char *value,
			   struct tomoyo_profile *profile)
{
	u8 i;
	u8 config;
	if (!strcmp(name, "CONFIG")) {
		i = TOMOYO_MAX_MAC_INDEX + TOMOYO_MAX_MAC_CATEGORY_INDEX;
		config = profile->default_config;
	} else if (tomoyo_str_starts(&name, "CONFIG::")) {
		config = 0;
		for (i = 0; i < TOMOYO_MAX_MAC_INDEX
			     + TOMOYO_MAX_MAC_CATEGORY_INDEX; i++) {
			int len = 0;
			if (i < TOMOYO_MAX_MAC_INDEX) {
				const u8 c = tomoyo_index2category[i];
				const char *category =
					tomoyo_category_keywords[c];
				len = strlen(category);
				if (strncmp(name, category, len) ||
				    name[len++] != ':' || name[len++] != ':')
					continue;
			}
			if (strcmp(name + len, tomoyo_mac_keywords[i]))
				continue;
			config = profile->config[i];
			break;
		}
		if (i == TOMOYO_MAX_MAC_INDEX + TOMOYO_MAX_MAC_CATEGORY_INDEX)
			return -EINVAL;
	} else {
		return -EINVAL;
	}
	if (strstr(value, "use_default")) {
		config = TOMOYO_CONFIG_USE_DEFAULT;
	} else {
		u8 mode;
		for (mode = 0; mode < 4; mode++)
			if (strstr(value, tomoyo_mode[mode]))
				/*
				 * Update lower 3 bits in order to distinguish
				 * 'config' from 'TOMOYO_CONFIG_USE_DEAFULT'.
				 */
				config = (config & ~7) | mode;
		if (config != TOMOYO_CONFIG_USE_DEFAULT) {
			switch (tomoyo_find_yesno(value, "grant_log")) {
			case 1:
				config |= TOMOYO_CONFIG_WANT_GRANT_LOG;
				break;
			case 0:
				config &= ~TOMOYO_CONFIG_WANT_GRANT_LOG;
				break;
			}
			switch (tomoyo_find_yesno(value, "reject_log")) {
			case 1:
				config |= TOMOYO_CONFIG_WANT_REJECT_LOG;
				break;
			case 0:
				config &= ~TOMOYO_CONFIG_WANT_REJECT_LOG;
				break;
			}
		}
	}
	if (i < TOMOYO_MAX_MAC_INDEX + TOMOYO_MAX_MAC_CATEGORY_INDEX)
		profile->config[i] = config;
	else if (config != TOMOYO_CONFIG_USE_DEFAULT)
		profile->default_config = config;
	return 0;
}

/**
 * tomoyo_write_profile - Write profile table.
 *
 * @head: Pointer to "struct tomoyo_io_buffer".
 *
 * Returns 0 on success, negative value otherwise.
 */
static int tomoyo_write_profile(struct tomoyo_io_buffer *head)
{
	char *data = head->write_buf;
	unsigned int i;
	char *cp;
	struct tomoyo_profile *profile;
	if (sscanf(data, "PROFILE_VERSION=%u", &head->w.ns->profile_version)
	    == 1)
		return 0;
	i = simple_strtoul(data, &cp, 10);
	if (*cp != '-')
		return -EINVAL;
	data = cp + 1;
	profile = tomoyo_assign_profile(head->w.ns, i);
	if (!profile)
		return -EINVAL;
	cp = strchr(data, '=');
	if (!cp)
		return -EINVAL;
	*cp++ = '\0';
	if (!strcmp(data, "COMMENT")) {
		static DEFINE_SPINLOCK(lock);
		const struct tomoyo_path_info *new_comment
			= tomoyo_get_name(cp);
		const struct tomoyo_path_info *old_comment;
		if (!new_comment)
			return -ENOMEM;
		spin_lock(&lock);
		old_comment = profile->comment;
		profile->comment = new_comment;
		spin_unlock(&lock);
		tomoyo_put_name(old_comment);
		return 0;
	}
	if (!strcmp(data, "PREFERENCE")) {
		for (i = 0; i < TOMOYO_MAX_PREF; i++)
			tomoyo_set_uint(&profile->pref[i], cp,
					tomoyo_pref_keywords[i]);
		return 0;
	}
	return tomoyo_set_mode(data, cp, profile);
}

/**
 * tomoyo_print_config - Print mode for specified functionality.
 *
 * @head:   Pointer to "struct tomoyo_io_buffer".
 * @config: Mode for that functionality.
 *
 * Returns nothing.
 *
 * Caller prints functionality's name.
 */
static void tomoyo_print_config(struct tomoyo_io_buffer *head, const u8 config)
{
	tomoyo_io_printf(head, "={ mode=%s grant_log=%s reject_log=%s }\n",
			 tomoyo_mode[config & 3],
			 tomoyo_yesno(config & TOMOYO_CONFIG_WANT_GRANT_LOG),
			 tomoyo_yesno(config & TOMOYO_CONFIG_WANT_REJECT_LOG));
}

/**
 * tomoyo_read_profile - Read profile table.
 *
 * @head: Pointer to "struct tomoyo_io_buffer".
 *
 * Returns nothing.
 */
static void tomoyo_read_profile(struct tomoyo_io_buffer *head)
{
	u8 index;
	struct tomoyo_policy_namespace *ns =
		container_of(head->r.ns, typeof(*ns), namespace_list);
	const struct tomoyo_profile *profile;
	if (head->r.eof)
		return;
 next:
	index = head->r.index;
	profile = ns->profile_ptr[index];
	switch (head->r.step) {
	case 0:
		tomoyo_print_namespace(head);
		tomoyo_io_printf(head, "PROFILE_VERSION=%u\n",
				 ns->profile_version);
		head->r.step++;
		break;
	case 1:
		for ( ; head->r.index < TOMOYO_MAX_PROFILES;
		      head->r.index++)
			if (ns->profile_ptr[head->r.index])
				break;
		if (head->r.index == TOMOYO_MAX_PROFILES) {
			head->r.eof = true;
			return;
		}
		head->r.step++;
		break;
	case 2:
		{
			u8 i;
			const struct tomoyo_path_info *comment =
				profile->comment;
			tomoyo_print_namespace(head);
			tomoyo_io_printf(head, "%u-COMMENT=", index);
			tomoyo_set_string(head, comment ? comment->name : "");
			tomoyo_set_lf(head);
			tomoyo_print_namespace(head);
			tomoyo_io_printf(head, "%u-PREFERENCE={ ", index);
			for (i = 0; i < TOMOYO_MAX_PREF; i++)
				tomoyo_io_printf(head, "%s=%u ",
						 tomoyo_pref_keywords[i],
						 profile->pref[i]);
			tomoyo_set_string(head, "}\n");
			head->r.step++;
		}
		break;
	case 3:
		{
			tomoyo_print_namespace(head);
			tomoyo_io_printf(head, "%u-%s", index, "CONFIG");
			tomoyo_print_config(head, profile->default_config);
			head->r.bit = 0;
			head->r.step++;
		}
		break;
	case 4:
		for ( ; head->r.bit < TOMOYO_MAX_MAC_INDEX
			      + TOMOYO_MAX_MAC_CATEGORY_INDEX; head->r.bit++) {
			const u8 i = head->r.bit;
			const u8 config = profile->config[i];
			if (config == TOMOYO_CONFIG_USE_DEFAULT)
				continue;
			tomoyo_print_namespace(head);
			if (i < TOMOYO_MAX_MAC_INDEX)
				tomoyo_io_printf(head, "%u-CONFIG::%s::%s",
						 index,
						 tomoyo_category_keywords
						 [tomoyo_index2category[i]],
						 tomoyo_mac_keywords[i]);
			else
				tomoyo_io_printf(head, "%u-CONFIG::%s", index,
						 tomoyo_mac_keywords[i]);
			tomoyo_print_config(head, config);
			head->r.bit++;
			break;
		}
		if (head->r.bit == TOMOYO_MAX_MAC_INDEX
		    + TOMOYO_MAX_MAC_CATEGORY_INDEX) {
			head->r.index++;
			head->r.step = 1;
		}
		break;
	}
	if (tomoyo_flush(head))
		goto next;
}

/**
 * tomoyo_same_manager - Check for duplicated "struct tomoyo_manager" entry.
 *
 * @a: Pointer to "struct tomoyo_acl_head".
 * @b: Pointer to "struct tomoyo_acl_head".
 *
 * Returns true if @a == @b, false otherwise.
 */
static bool tomoyo_same_manager(const struct tomoyo_acl_head *a,
				const struct tomoyo_acl_head *b)
{
	return container_of(a, struct tomoyo_manager, head)->manager ==
		container_of(b, struct tomoyo_manager, head)->manager;
}

/**
 * tomoyo_update_manager_entry - Add a manager entry.
 *
 * @manager:   The path to manager or the domainnamme.
 * @is_delete: True if it is a delete request.
 *
 * Returns 0 on success, negative value otherwise.
 *
 * Caller holds tomoyo_read_lock().
 */
static int tomoyo_update_manager_entry(const char *manager,
				       const bool is_delete)
{
	struct tomoyo_manager e = { };
	struct tomoyo_acl_param param = {
		/* .ns = &tomoyo_kernel_namespace, */
		.is_delete = is_delete,
		.list = &tomoyo_kernel_namespace.
		policy_list[TOMOYO_ID_MANAGER],
	};
	int error = is_delete ? -ENOENT : -ENOMEM;
	if (!tomoyo_correct_domain(manager) &&
	    !tomoyo_correct_word(manager))
		return -EINVAL;
	e.manager = tomoyo_get_name(manager);
	if (e.manager) {
		error = tomoyo_update_policy(&e.head, sizeof(e), &param,
					     tomoyo_same_manager);
		tomoyo_put_name(e.manager);
	}
	return error;
}

/**
 * tomoyo_write_manager - Write manager policy.
 *
 * @head: Pointer to "struct tomoyo_io_buffer".
 *
 * Returns 0 on success, negative value otherwise.
 *
 * Caller holds tomoyo_read_lock().
 */
static int tomoyo_write_manager(struct tomoyo_io_buffer *head)
{
	char *data = head->write_buf;

	if (!strcmp(data, "manage_by_non_root")) {
		tomoyo_manage_by_non_root = !head->w.is_delete;
		return 0;
	}
	return tomoyo_update_manager_entry(data, head->w.is_delete);
}

/**
 * tomoyo_read_manager - Read manager policy.
 *
 * @head: Pointer to "struct tomoyo_io_buffer".
 *
 * Caller holds tomoyo_read_lock().
 */
static void tomoyo_read_manager(struct tomoyo_io_buffer *head)
{
	if (head->r.eof)
		return;
	list_for_each_cookie(head->r.acl, &tomoyo_kernel_namespace.
			     policy_list[TOMOYO_ID_MANAGER]) {
		struct tomoyo_manager *ptr =
			list_entry(head->r.acl, typeof(*ptr), head.list);
		if (ptr->head.is_deleted)
			continue;
		if (!tomoyo_flush(head))
			return;
		tomoyo_set_string(head, ptr->manager->name);
		tomoyo_set_lf(head);
	}
	head->r.eof = true;
}

/**
 * tomoyo_manager - Check whether the current process is a policy manager.
 *
 * Returns true if the current process is permitted to modify policy
 * via /sys/kernel/security/tomoyo/ interface.
 *
 * Caller holds tomoyo_read_lock().
 */
static bool tomoyo_manager(void)
{
	struct tomoyo_manager *ptr;
	const char *exe;
	const struct task_struct *task = current;
	const struct tomoyo_path_info *domainname = tomoyo_domain()->domainname;
	bool found = false;

	if (!tomoyo_policy_loaded)
		return true;
	if (!tomoyo_manage_by_non_root &&
	    (!uid_eq(task->cred->uid,  GLOBAL_ROOT_UID) ||
	     !uid_eq(task->cred->euid, GLOBAL_ROOT_UID)))
		return false;
	exe = tomoyo_get_exe();
	if (!exe)
		return false;
	list_for_each_entry_rcu(ptr, &tomoyo_kernel_namespace.
				policy_list[TOMOYO_ID_MANAGER], head.list) {
		if (!ptr->head.is_deleted &&
		    (!tomoyo_pathcmp(domainname, ptr->manager) ||
		     !strcmp(exe, ptr->manager->name))) {
			found = true;
			break;
		}
	}
	if (!found) { /* Reduce error messages. */
		static pid_t last_pid;
		const pid_t pid = current->pid;
		if (last_pid != pid) {
			printk(KERN_WARNING "%s ( %s ) is not permitted to "
			       "update policies.\n", domainname->name, exe);
			last_pid = pid;
		}
	}
	kfree(exe);
	return found;
}

static struct tomoyo_domain_info *tomoyo_find_domain_by_qid
(unsigned int serial);

/**
 * tomoyo_select_domain - Parse select command.
 *
 * @head: Pointer to "struct tomoyo_io_buffer".
 * @data: String to parse.
 *
 * Returns true on success, false otherwise.
 *
 * Caller holds tomoyo_read_lock().
 */
static bool tomoyo_select_domain(struct tomoyo_io_buffer *head,
				 const char *data)
{
	unsigned int pid;
	struct tomoyo_domain_info *domain = NULL;
	bool global_pid = false;
	if (strncmp(data, "select ", 7))
		return false;
	data += 7;
	if (sscanf(data, "pid=%u", &pid) == 1 ||
	    (global_pid = true, sscanf(data, "global-pid=%u", &pid) == 1)) {
		struct task_struct *p;
		rcu_read_lock();
		if (global_pid)
			p = find_task_by_pid_ns(pid, &init_pid_ns);
		else
			p = find_task_by_vpid(pid);
		if (p)
			domain = tomoyo_real_domain(p);
		rcu_read_unlock();
	} else if (!strncmp(data, "domain=", 7)) {
		if (tomoyo_domain_def(data + 7))
			domain = tomoyo_find_domain(data + 7);
	} else if (sscanf(data, "Q=%u", &pid) == 1) {
		domain = tomoyo_find_domain_by_qid(pid);
	} else
		return false;
	head->w.domain = domain;
	/* Accessing read_buf is safe because head->io_sem is held. */
	if (!head->read_buf)
		return true; /* Do nothing if open(O_WRONLY). */
	memset(&head->r, 0, sizeof(head->r));
	head->r.print_this_domain_only = true;
	if (domain)
		head->r.domain = &domain->list;
	else
		head->r.eof = 1;
	tomoyo_io_printf(head, "# select %s\n", data);
	if (domain && domain->is_deleted)
		tomoyo_io_printf(head, "# This is a deleted domain.\n");
	return true;
}

/**
 * tomoyo_same_task_acl - Check for duplicated "struct tomoyo_task_acl" entry.
 *
 * @a: Pointer to "struct tomoyo_acl_info".
 * @b: Pointer to "struct tomoyo_acl_info".
 *
 * Returns true if @a == @b, false otherwise.
 */
static bool tomoyo_same_task_acl(const struct tomoyo_acl_info *a,
			      const struct tomoyo_acl_info *b)
{
	const struct tomoyo_task_acl *p1 = container_of(a, typeof(*p1), head);
	const struct tomoyo_task_acl *p2 = container_of(b, typeof(*p2), head);
	return p1->domainname == p2->domainname;
}

/**
 * tomoyo_write_task - Update task related list.
 *
 * @param: Pointer to "struct tomoyo_acl_param".
 *
 * Returns 0 on success, negative value otherwise.
 *
 * Caller holds tomoyo_read_lock().
 */
static int tomoyo_write_task(struct tomoyo_acl_param *param)
{
	int error = -EINVAL;
	if (tomoyo_str_starts(&param->data, "manual_domain_transition ")) {
		struct tomoyo_task_acl e = {
			.head.type = TOMOYO_TYPE_MANUAL_TASK_ACL,
			.domainname = tomoyo_get_domainname(param),
		};
		if (e.domainname)
			error = tomoyo_update_domain(&e.head, sizeof(e), param,
						     tomoyo_same_task_acl,
						     NULL);
		tomoyo_put_name(e.domainname);
	}
	return error;
}

/**
 * tomoyo_delete_domain - Delete a domain.
 *
 * @domainname: The name of domain.
 *
 * Returns 0 on success, negative value otherwise.
 *
 * Caller holds tomoyo_read_lock().
 */
static int tomoyo_delete_domain(char *domainname)
{
	struct tomoyo_domain_info *domain;
	struct tomoyo_path_info name;

	name.name = domainname;
	tomoyo_fill_path_info(&name);
	if (mutex_lock_interruptible(&tomoyo_policy_lock))
		return -EINTR;
	/* Is there an active domain? */
	list_for_each_entry_rcu(domain, &tomoyo_domain_list, list) {
		/* Never delete tomoyo_kernel_domain */
		if (domain == &tomoyo_kernel_domain)
			continue;
		if (domain->is_deleted ||
		    tomoyo_pathcmp(domain->domainname, &name))
			continue;
		domain->is_deleted = true;
		break;
	}
	mutex_unlock(&tomoyo_policy_lock);
	return 0;
}

/**
 * tomoyo_write_domain2 - Write domain policy.
 *
 * @ns:        Pointer to "struct tomoyo_policy_namespace".
 * @list:      Pointer to "struct list_head".
 * @data:      Policy to be interpreted.
 * @is_delete: True if it is a delete request.
 *
 * Returns 0 on success, negative value otherwise.
 *
 * Caller holds tomoyo_read_lock().
 */
static int tomoyo_write_domain2(struct tomoyo_policy_namespace *ns,
				struct list_head *list, char *data,
				const bool is_delete)
{
	struct tomoyo_acl_param param = {
		.ns = ns,
		.list = list,
		.data = data,
		.is_delete = is_delete,
	};
	static const struct {
		const char *keyword;
		int (*write) (struct tomoyo_acl_param *);
	} tomoyo_callback[5] = {
		{ "file ", tomoyo_write_file },
		{ "network inet ", tomoyo_write_inet_network },
		{ "network unix ", tomoyo_write_unix_network },
		{ "misc ", tomoyo_write_misc },
		{ "task ", tomoyo_write_task },
	};
	u8 i;

	for (i = 0; i < ARRAY_SIZE(tomoyo_callback); i++) {
		if (!tomoyo_str_starts(&param.data,
				       tomoyo_callback[i].keyword))
			continue;
		return tomoyo_callback[i].write(&param);
	}
	return -EINVAL;
}

/* String table for domain flags. */
const char * const tomoyo_dif[TOMOYO_MAX_DOMAIN_INFO_FLAGS] = {
	[TOMOYO_DIF_QUOTA_WARNED]      = "quota_exceeded\n",
	[TOMOYO_DIF_TRANSITION_FAILED] = "transition_failed\n",
};

/**
 * tomoyo_write_domain - Write domain policy.
 *
 * @head: Pointer to "struct tomoyo_io_buffer".
 *
 * Returns 0 on success, negative value otherwise.
 *
 * Caller holds tomoyo_read_lock().
 */
static int tomoyo_write_domain(struct tomoyo_io_buffer *head)
{
	char *data = head->write_buf;
	struct tomoyo_policy_namespace *ns;
	struct tomoyo_domain_info *domain = head->w.domain;
	const bool is_delete = head->w.is_delete;
	bool is_select = !is_delete && tomoyo_str_starts(&data, "select ");
	unsigned int profile;
	if (*data == '<') {
		int ret = 0;
		domain = NULL;
		if (is_delete)
			ret = tomoyo_delete_domain(data);
		else if (is_select)
			domain = tomoyo_find_domain(data);
		else
			domain = tomoyo_assign_domain(data, false);
		head->w.domain = domain;
		return ret;
	}
	if (!domain)
		return -EINVAL;
	ns = domain->ns;
	if (sscanf(data, "use_profile %u", &profile) == 1
	    && profile < TOMOYO_MAX_PROFILES) {
		if (!tomoyo_policy_loaded || ns->profile_ptr[profile])
			domain->profile = (u8) profile;
		return 0;
	}
	if (sscanf(data, "use_group %u\n", &profile) == 1
	    && profile < TOMOYO_MAX_ACL_GROUPS) {
		if (!is_delete)
			domain->group = (u8) profile;
		return 0;
	}
	for (profile = 0; profile < TOMOYO_MAX_DOMAIN_INFO_FLAGS; profile++) {
		const char *cp = tomoyo_dif[profile];
		if (strncmp(data, cp, strlen(cp) - 1))
			continue;
		domain->flags[profile] = !is_delete;
		return 0;
	}
	return tomoyo_write_domain2(ns, &domain->acl_info_list, data,
				    is_delete);
}

/**
 * tomoyo_print_condition - Print condition part.
 *
 * @head: Pointer to "struct tomoyo_io_buffer".
 * @cond: Pointer to "struct tomoyo_condition".
 *
 * Returns true on success, false otherwise.
 */
static bool tomoyo_print_condition(struct tomoyo_io_buffer *head,
				   const struct tomoyo_condition *cond)
{
	switch (head->r.cond_step) {
	case 0:
		head->r.cond_index = 0;
		head->r.cond_step++;
		if (cond->transit) {
			tomoyo_set_space(head);
			tomoyo_set_string(head, cond->transit->name);
		}
		/* fall through */
	case 1:
		{
			const u16 condc = cond->condc;
			const struct tomoyo_condition_element *condp =
				(typeof(condp)) (cond + 1);
			const struct tomoyo_number_union *numbers_p =
				(typeof(numbers_p)) (condp + condc);
			const struct tomoyo_name_union *names_p =
				(typeof(names_p))
				(numbers_p + cond->numbers_count);
			const struct tomoyo_argv *argv =
				(typeof(argv)) (names_p + cond->names_count);
			const struct tomoyo_envp *envp =
				(typeof(envp)) (argv + cond->argc);
			u16 skip;
			for (skip = 0; skip < head->r.cond_index; skip++) {
				const u8 left = condp->left;
				const u8 right = condp->right;
				condp++;
				switch (left) {
				case TOMOYO_ARGV_ENTRY:
					argv++;
					continue;
				case TOMOYO_ENVP_ENTRY:
					envp++;
					continue;
				case TOMOYO_NUMBER_UNION:
					numbers_p++;
					break;
				}
				switch (right) {
				case TOMOYO_NAME_UNION:
					names_p++;
					break;
				case TOMOYO_NUMBER_UNION:
					numbers_p++;
					break;
				}
			}
			while (head->r.cond_index < condc) {
				const u8 match = condp->equals;
				const u8 left = condp->left;
				const u8 right = condp->right;
				if (!tomoyo_flush(head))
					return false;
				condp++;
				head->r.cond_index++;
				tomoyo_set_space(head);
				switch (left) {
				case TOMOYO_ARGV_ENTRY:
					tomoyo_io_printf(head,
							 "exec.argv[%lu]%s=\"",
							 argv->index, argv->
							 is_not ? "!" : "");
					tomoyo_set_string(head,
							  argv->value->name);
					tomoyo_set_string(head, "\"");
					argv++;
					continue;
				case TOMOYO_ENVP_ENTRY:
					tomoyo_set_string(head,
							  "exec.envp[\"");
					tomoyo_set_string(head,
							  envp->name->name);
					tomoyo_io_printf(head, "\"]%s=", envp->
							 is_not ? "!" : "");
					if (envp->value) {
						tomoyo_set_string(head, "\"");
						tomoyo_set_string(head, envp->
								  value->name);
						tomoyo_set_string(head, "\"");
					} else {
						tomoyo_set_string(head,
								  "NULL");
					}
					envp++;
					continue;
				case TOMOYO_NUMBER_UNION:
					tomoyo_print_number_union_nospace
						(head, numbers_p++);
					break;
				default:
					tomoyo_set_string(head,
					       tomoyo_condition_keyword[left]);
					break;
				}
				tomoyo_set_string(head, match ? "=" : "!=");
				switch (right) {
				case TOMOYO_NAME_UNION:
					tomoyo_print_name_union_quoted
						(head, names_p++);
					break;
				case TOMOYO_NUMBER_UNION:
					tomoyo_print_number_union_nospace
						(head, numbers_p++);
					break;
				default:
					tomoyo_set_string(head,
					  tomoyo_condition_keyword[right]);
					break;
				}
			}
		}
		head->r.cond_step++;
		/* fall through */
	case 2:
		if (!tomoyo_flush(head))
			break;
		head->r.cond_step++;
		/* fall through */
	case 3:
		if (cond->grant_log != TOMOYO_GRANTLOG_AUTO)
			tomoyo_io_printf(head, " grant_log=%s",
					 tomoyo_yesno(cond->grant_log ==
						      TOMOYO_GRANTLOG_YES));
		tomoyo_set_lf(head);
		return true;
	}
	return false;
}

/**
 * tomoyo_set_group - Print "acl_group " header keyword and category name.
 *
 * @head:     Pointer to "struct tomoyo_io_buffer".
 * @category: Category name.
 *
 * Returns nothing.
 */
static void tomoyo_set_group(struct tomoyo_io_buffer *head,
			     const char *category)
{
	if (head->type == TOMOYO_EXCEPTIONPOLICY) {
		tomoyo_print_namespace(head);
		tomoyo_io_printf(head, "acl_group %u ",
				 head->r.acl_group_index);
	}
	tomoyo_set_string(head, category);
}

/**
 * tomoyo_print_entry - Print an ACL entry.
 *
 * @head: Pointer to "struct tomoyo_io_buffer".
 * @acl:  Pointer to an ACL entry.
 *
 * Returns true on success, false otherwise.
 */
static bool tomoyo_print_entry(struct tomoyo_io_buffer *head,
			       struct tomoyo_acl_info *acl)
{
	const u8 acl_type = acl->type;
	bool first = true;
	u8 bit;

	if (head->r.print_cond_part)
		goto print_cond_part;
	if (acl->is_deleted)
		return true;
	if (!tomoyo_flush(head))
		return false;
	else if (acl_type == TOMOYO_TYPE_PATH_ACL) {
		struct tomoyo_path_acl *ptr =
			container_of(acl, typeof(*ptr), head);
		const u16 perm = ptr->perm;
		for (bit = 0; bit < TOMOYO_MAX_PATH_OPERATION; bit++) {
			if (!(perm & (1 << bit)))
				continue;
			if (head->r.print_transition_related_only &&
			    bit != TOMOYO_TYPE_EXECUTE)
				continue;
			if (first) {
				tomoyo_set_group(head, "file ");
				first = false;
			} else {
				tomoyo_set_slash(head);
			}
			tomoyo_set_string(head, tomoyo_path_keyword[bit]);
		}
		if (first)
			return true;
		tomoyo_print_name_union(head, &ptr->name);
	} else if (acl_type == TOMOYO_TYPE_MANUAL_TASK_ACL) {
		struct tomoyo_task_acl *ptr =
			container_of(acl, typeof(*ptr), head);
		tomoyo_set_group(head, "task ");
		tomoyo_set_string(head, "manual_domain_transition ");
		tomoyo_set_string(head, ptr->domainname->name);
	} else if (head->r.print_transition_related_only) {
		return true;
	} else if (acl_type == TOMOYO_TYPE_PATH2_ACL) {
		struct tomoyo_path2_acl *ptr =
			container_of(acl, typeof(*ptr), head);
		const u8 perm = ptr->perm;
		for (bit = 0; bit < TOMOYO_MAX_PATH2_OPERATION; bit++) {
			if (!(perm & (1 << bit)))
				continue;
			if (first) {
				tomoyo_set_group(head, "file ");
				first = false;
			} else {
				tomoyo_set_slash(head);
			}
			tomoyo_set_string(head, tomoyo_mac_keywords
					  [tomoyo_pp2mac[bit]]);
		}
		if (first)
			return true;
		tomoyo_print_name_union(head, &ptr->name1);
		tomoyo_print_name_union(head, &ptr->name2);
	} else if (acl_type == TOMOYO_TYPE_PATH_NUMBER_ACL) {
		struct tomoyo_path_number_acl *ptr =
			container_of(acl, typeof(*ptr), head);
		const u8 perm = ptr->perm;
		for (bit = 0; bit < TOMOYO_MAX_PATH_NUMBER_OPERATION; bit++) {
			if (!(perm & (1 << bit)))
				continue;
			if (first) {
				tomoyo_set_group(head, "file ");
				first = false;
			} else {
				tomoyo_set_slash(head);
			}
			tomoyo_set_string(head, tomoyo_mac_keywords
					  [tomoyo_pn2mac[bit]]);
		}
		if (first)
			return true;
		tomoyo_print_name_union(head, &ptr->name);
		tomoyo_print_number_union(head, &ptr->number);
	} else if (acl_type == TOMOYO_TYPE_MKDEV_ACL) {
		struct tomoyo_mkdev_acl *ptr =
			container_of(acl, typeof(*ptr), head);
		const u8 perm = ptr->perm;
		for (bit = 0; bit < TOMOYO_MAX_MKDEV_OPERATION; bit++) {
			if (!(perm & (1 << bit)))
				continue;
			if (first) {
				tomoyo_set_group(head, "file ");
				first = false;
			} else {
				tomoyo_set_slash(head);
			}
			tomoyo_set_string(head, tomoyo_mac_keywords
					  [tomoyo_pnnn2mac[bit]]);
		}
		if (first)
			return true;
		tomoyo_print_name_union(head, &ptr->name);
		tomoyo_print_number_union(head, &ptr->mode);
		tomoyo_print_number_union(head, &ptr->major);
		tomoyo_print_number_union(head, &ptr->minor);
	} else if (acl_type == TOMOYO_TYPE_INET_ACL) {
		struct tomoyo_inet_acl *ptr =
			container_of(acl, typeof(*ptr), head);
		const u8 perm = ptr->perm;

		for (bit = 0; bit < TOMOYO_MAX_NETWORK_OPERATION; bit++) {
			if (!(perm & (1 << bit)))
				continue;
			if (first) {
				tomoyo_set_group(head, "network inet ");
				tomoyo_set_string(head, tomoyo_proto_keyword
						  [ptr->protocol]);
				tomoyo_set_space(head);
				first = false;
			} else {
				tomoyo_set_slash(head);
			}
			tomoyo_set_string(head, tomoyo_socket_keyword[bit]);
		}
		if (first)
			return true;
		tomoyo_set_space(head);
		if (ptr->address.group) {
			tomoyo_set_string(head, "@");
			tomoyo_set_string(head, ptr->address.group->group_name
					  ->name);
		} else {
			char buf[128];
			tomoyo_print_ip(buf, sizeof(buf), &ptr->address);
			tomoyo_io_printf(head, "%s", buf);
		}
		tomoyo_print_number_union(head, &ptr->port);
	} else if (acl_type == TOMOYO_TYPE_UNIX_ACL) {
		struct tomoyo_unix_acl *ptr =
			container_of(acl, typeof(*ptr), head);
		const u8 perm = ptr->perm;

		for (bit = 0; bit < TOMOYO_MAX_NETWORK_OPERATION; bit++) {
			if (!(perm & (1 << bit)))
				continue;
			if (first) {
				tomoyo_set_group(head, "network unix ");
				tomoyo_set_string(head, tomoyo_proto_keyword
						  [ptr->protocol]);
				tomoyo_set_space(head);
				first = false;
			} else {
				tomoyo_set_slash(head);
			}
			tomoyo_set_string(head, tomoyo_socket_keyword[bit]);
		}
		if (first)
			return true;
		tomoyo_print_name_union(head, &ptr->name);
	} else if (acl_type == TOMOYO_TYPE_MOUNT_ACL) {
		struct tomoyo_mount_acl *ptr =
			container_of(acl, typeof(*ptr), head);
		tomoyo_set_group(head, "file mount");
		tomoyo_print_name_union(head, &ptr->dev_name);
		tomoyo_print_name_union(head, &ptr->dir_name);
		tomoyo_print_name_union(head, &ptr->fs_type);
		tomoyo_print_number_union(head, &ptr->flags);
	} else if (acl_type == TOMOYO_TYPE_ENV_ACL) {
		struct tomoyo_env_acl *ptr =
			container_of(acl, typeof(*ptr), head);

		tomoyo_set_group(head, "misc env ");
		tomoyo_set_string(head, ptr->env->name);
	}
	if (acl->cond) {
		head->r.print_cond_part = true;
		head->r.cond_step = 0;
		if (!tomoyo_flush(head))
			return false;
print_cond_part:
		if (!tomoyo_print_condition(head, acl->cond))
			return false;
		head->r.print_cond_part = false;
	} else {
		tomoyo_set_lf(head);
	}
	return true;
}

/**
 * tomoyo_read_domain2 - Read domain policy.
 *
 * @head: Pointer to "struct tomoyo_io_buffer".
 * @list: Pointer to "struct list_head".
 *
 * Caller holds tomoyo_read_lock().
 *
 * Returns true on success, false otherwise.
 */
static bool tomoyo_read_domain2(struct tomoyo_io_buffer *head,
				struct list_head *list)
{
	list_for_each_cookie(head->r.acl, list) {
		struct tomoyo_acl_info *ptr =
			list_entry(head->r.acl, typeof(*ptr), list);
		if (!tomoyo_print_entry(head, ptr))
			return false;
	}
	head->r.acl = NULL;
	return true;
}

/**
 * tomoyo_read_domain - Read domain policy.
 *
 * @head: Pointer to "struct tomoyo_io_buffer".
 *
 * Caller holds tomoyo_read_lock().
 */
static void tomoyo_read_domain(struct tomoyo_io_buffer *head)
{
	if (head->r.eof)
		return;
	list_for_each_cookie(head->r.domain, &tomoyo_domain_list) {
		struct tomoyo_domain_info *domain =
			list_entry(head->r.domain, typeof(*domain), list);
		switch (head->r.step) {
			u8 i;
		case 0:
			if (domain->is_deleted &&
			    !head->r.print_this_domain_only)
				continue;
			/* Print domainname and flags. */
			tomoyo_set_string(head, domain->domainname->name);
			tomoyo_set_lf(head);
			tomoyo_io_printf(head, "use_profile %u\n",
					 domain->profile);
			tomoyo_io_printf(head, "use_group %u\n",
					 domain->group);
			for (i = 0; i < TOMOYO_MAX_DOMAIN_INFO_FLAGS; i++)
				if (domain->flags[i])
					tomoyo_set_string(head, tomoyo_dif[i]);
			head->r.step++;
			tomoyo_set_lf(head);
			/* fall through */
		case 1:
			if (!tomoyo_read_domain2(head, &domain->acl_info_list))
				return;
			head->r.step++;
			if (!tomoyo_set_lf(head))
				return;
			/* fall through */
		case 2:
			head->r.step = 0;
			if (head->r.print_this_domain_only)
				goto done;
		}
	}
 done:
	head->r.eof = true;
}

/**
 * tomoyo_write_pid: Specify PID to obtain domainname.
 *
 * @head: Pointer to "struct tomoyo_io_buffer".
 *
 * Returns 0.
 */
static int tomoyo_write_pid(struct tomoyo_io_buffer *head)
{
	head->r.eof = false;
	return 0;
}

/**
 * tomoyo_read_pid - Get domainname of the specified PID.
 *
 * @head: Pointer to "struct tomoyo_io_buffer".
 *
 * Returns the domainname which the specified PID is in on success,
 * empty string otherwise.
 * The PID is specified by tomoyo_write_pid() so that the user can obtain
 * using read()/write() interface rather than sysctl() interface.
 */
static void tomoyo_read_pid(struct tomoyo_io_buffer *head)
{
	char *buf = head->write_buf;
	bool global_pid = false;
	unsigned int pid;
	struct task_struct *p;
	struct tomoyo_domain_info *domain = NULL;

	/* Accessing write_buf is safe because head->io_sem is held. */
	if (!buf) {
		head->r.eof = true;
		return; /* Do nothing if open(O_RDONLY). */
	}
	if (head->r.w_pos || head->r.eof)
		return;
	head->r.eof = true;
	if (tomoyo_str_starts(&buf, "global-pid "))
		global_pid = true;
	if (kstrtouint(buf, 10, &pid))
		return;
	rcu_read_lock();
	if (global_pid)
		p = find_task_by_pid_ns(pid, &init_pid_ns);
	else
		p = find_task_by_vpid(pid);
	if (p)
		domain = tomoyo_real_domain(p);
	rcu_read_unlock();
	if (!domain)
		return;
	tomoyo_io_printf(head, "%u %u ", pid, domain->profile);
	tomoyo_set_string(head, domain->domainname->name);
}

/* String table for domain transition control keywords. */
static const char *tomoyo_transition_type[TOMOYO_MAX_TRANSITION_TYPE] = {
	[TOMOYO_TRANSITION_CONTROL_NO_RESET]      = "no_reset_domain ",
	[TOMOYO_TRANSITION_CONTROL_RESET]         = "reset_domain ",
	[TOMOYO_TRANSITION_CONTROL_NO_INITIALIZE] = "no_initialize_domain ",
	[TOMOYO_TRANSITION_CONTROL_INITIALIZE]    = "initialize_domain ",
	[TOMOYO_TRANSITION_CONTROL_NO_KEEP]       = "no_keep_domain ",
	[TOMOYO_TRANSITION_CONTROL_KEEP]          = "keep_domain ",
};

/* String table for grouping keywords. */
static const char *tomoyo_group_name[TOMOYO_MAX_GROUP] = {
	[TOMOYO_PATH_GROUP]    = "path_group ",
	[TOMOYO_NUMBER_GROUP]  = "number_group ",
	[TOMOYO_ADDRESS_GROUP] = "address_group ",
};

/**
 * tomoyo_write_exception - Write exception policy.
 *
 * @head: Pointer to "struct tomoyo_io_buffer".
 *
 * Returns 0 on success, negative value otherwise.
 *
 * Caller holds tomoyo_read_lock().
 */
static int tomoyo_write_exception(struct tomoyo_io_buffer *head)
{
	const bool is_delete = head->w.is_delete;
	struct tomoyo_acl_param param = {
		.ns = head->w.ns,
		.is_delete = is_delete,
		.data = head->write_buf,
	};
	u8 i;
	if (tomoyo_str_starts(&param.data, "aggregator "))
		return tomoyo_write_aggregator(&param);
	for (i = 0; i < TOMOYO_MAX_TRANSITION_TYPE; i++)
		if (tomoyo_str_starts(&param.data, tomoyo_transition_type[i]))
			return tomoyo_write_transition_control(&param, i);
	for (i = 0; i < TOMOYO_MAX_GROUP; i++)
		if (tomoyo_str_starts(&param.data, tomoyo_group_name[i]))
			return tomoyo_write_group(&param, i);
	if (tomoyo_str_starts(&param.data, "acl_group ")) {
		unsigned int group;
		char *data;
		group = simple_strtoul(param.data, &data, 10);
		if (group < TOMOYO_MAX_ACL_GROUPS && *data++ == ' ')
			return tomoyo_write_domain2
				(head->w.ns, &head->w.ns->acl_group[group],
				 data, is_delete);
	}
	return -EINVAL;
}

/**
 * tomoyo_read_group - Read "struct tomoyo_path_group"/"struct tomoyo_number_group"/"struct tomoyo_address_group" list.
 *
 * @head: Pointer to "struct tomoyo_io_buffer".
 * @idx:  Index number.
 *
 * Returns true on success, false otherwise.
 *
 * Caller holds tomoyo_read_lock().
 */
static bool tomoyo_read_group(struct tomoyo_io_buffer *head, const int idx)
{
	struct tomoyo_policy_namespace *ns =
		container_of(head->r.ns, typeof(*ns), namespace_list);
	struct list_head *list = &ns->group_list[idx];
	list_for_each_cookie(head->r.group, list) {
		struct tomoyo_group *group =
			list_entry(head->r.group, typeof(*group), head.list);
		list_for_each_cookie(head->r.acl, &group->member_list) {
			struct tomoyo_acl_head *ptr =
				list_entry(head->r.acl, typeof(*ptr), list);
			if (ptr->is_deleted)
				continue;
			if (!tomoyo_flush(head))
				return false;
			tomoyo_print_namespace(head);
			tomoyo_set_string(head, tomoyo_group_name[idx]);
			tomoyo_set_string(head, group->group_name->name);
			if (idx == TOMOYO_PATH_GROUP) {
				tomoyo_set_space(head);
				tomoyo_set_string(head, container_of
					       (ptr, struct tomoyo_path_group,
						head)->member_name->name);
			} else if (idx == TOMOYO_NUMBER_GROUP) {
				tomoyo_print_number_union(head, &container_of
							  (ptr,
						   struct tomoyo_number_group,
							   head)->number);
			} else if (idx == TOMOYO_ADDRESS_GROUP) {
				char buffer[128];

				struct tomoyo_address_group *member =
					container_of(ptr, typeof(*member),
						     head);
				tomoyo_print_ip(buffer, sizeof(buffer),
						&member->address);
				tomoyo_io_printf(head, " %s", buffer);
			}
			tomoyo_set_lf(head);
		}
		head->r.acl = NULL;
	}
	head->r.group = NULL;
	return true;
}

/**
 * tomoyo_read_policy - Read "struct tomoyo_..._entry" list.
 *
 * @head: Pointer to "struct tomoyo_io_buffer".
 * @idx:  Index number.
 *
 * Returns true on success, false otherwise.
 *
 * Caller holds tomoyo_read_lock().
 */
static bool tomoyo_read_policy(struct tomoyo_io_buffer *head, const int idx)
{
	struct tomoyo_policy_namespace *ns =
		container_of(head->r.ns, typeof(*ns), namespace_list);
	struct list_head *list = &ns->policy_list[idx];
	list_for_each_cookie(head->r.acl, list) {
		struct tomoyo_acl_head *acl =
			container_of(head->r.acl, typeof(*acl), list);
		if (acl->is_deleted)
			continue;
		if (!tomoyo_flush(head))
			return false;
		switch (idx) {
		case TOMOYO_ID_TRANSITION_CONTROL:
			{
				struct tomoyo_transition_control *ptr =
					container_of(acl, typeof(*ptr), head);
				tomoyo_print_namespace(head);
				tomoyo_set_string(head, tomoyo_transition_type
						  [ptr->type]);
				tomoyo_set_string(head, ptr->program ?
						  ptr->program->name : "any");
				tomoyo_set_string(head, " from ");
				tomoyo_set_string(head, ptr->domainname ?
						  ptr->domainname->name :
						  "any");
			}
			break;
		case TOMOYO_ID_AGGREGATOR:
			{
				struct tomoyo_aggregator *ptr =
					container_of(acl, typeof(*ptr), head);
				tomoyo_print_namespace(head);
				tomoyo_set_string(head, "aggregator ");
				tomoyo_set_string(head,
						  ptr->original_name->name);
				tomoyo_set_space(head);
				tomoyo_set_string(head,
					       ptr->aggregated_name->name);
			}
			break;
		default:
			continue;
		}
		tomoyo_set_lf(head);
	}
	head->r.acl = NULL;
	return true;
}

/**
 * tomoyo_read_exception - Read exception policy.
 *
 * @head: Pointer to "struct tomoyo_io_buffer".
 *
 * Caller holds tomoyo_read_lock().
 */
static void tomoyo_read_exception(struct tomoyo_io_buffer *head)
{
	struct tomoyo_policy_namespace *ns =
		container_of(head->r.ns, typeof(*ns), namespace_list);
	if (head->r.eof)
		return;
	while (head->r.step < TOMOYO_MAX_POLICY &&
	       tomoyo_read_policy(head, head->r.step))
		head->r.step++;
	if (head->r.step < TOMOYO_MAX_POLICY)
		return;
	while (head->r.step < TOMOYO_MAX_POLICY + TOMOYO_MAX_GROUP &&
	       tomoyo_read_group(head, head->r.step - TOMOYO_MAX_POLICY))
		head->r.step++;
	if (head->r.step < TOMOYO_MAX_POLICY + TOMOYO_MAX_GROUP)
		return;
	while (head->r.step < TOMOYO_MAX_POLICY + TOMOYO_MAX_GROUP
	       + TOMOYO_MAX_ACL_GROUPS) {
		head->r.acl_group_index = head->r.step - TOMOYO_MAX_POLICY
			- TOMOYO_MAX_GROUP;
		if (!tomoyo_read_domain2(head, &ns->acl_group
					 [head->r.acl_group_index]))
			return;
		head->r.step++;
	}
	head->r.eof = true;
}

/* Wait queue for kernel -> userspace notification. */
static DECLARE_WAIT_QUEUE_HEAD(tomoyo_query_wait);
/* Wait queue for userspace -> kernel notification. */
static DECLARE_WAIT_QUEUE_HEAD(tomoyo_answer_wait);

/* Structure for query. */
struct tomoyo_query {
	struct list_head list;
	struct tomoyo_domain_info *domain;
	char *query;
	size_t query_len;
	unsigned int serial;
	u8 timer;
	u8 answer;
	u8 retry;
};

/* The list for "struct tomoyo_query". */
static LIST_HEAD(tomoyo_query_list);

/* Lock for manipulating tomoyo_query_list. */
static DEFINE_SPINLOCK(tomoyo_query_list_lock);

/*
 * Number of "struct file" referring /sys/kernel/security/tomoyo/query
 * interface.
 */
static atomic_t tomoyo_query_observers = ATOMIC_INIT(0);

/**
 * tomoyo_truncate - Truncate a line.
 *
 * @str: String to truncate.
 *
 * Returns length of truncated @str.
 */
static int tomoyo_truncate(char *str)
{
	char *start = str;
	while (*(unsigned char *) str > (unsigned char) ' ')
		str++;
	*str = '\0';
	return strlen(start) + 1;
}

/**
 * tomoyo_add_entry - Add an ACL to current thread's domain. Used by learning mode.
 *
 * @domain: Pointer to "struct tomoyo_domain_info".
 * @header: Lines containing ACL.
 *
 * Returns nothing.
 */
static void tomoyo_add_entry(struct tomoyo_domain_info *domain, char *header)
{
	char *buffer;
	char *realpath = NULL;
	char *argv0 = NULL;
	char *symlink = NULL;
	char *cp = strchr(header, '\n');
	int len;
	if (!cp)
		return;
	cp = strchr(cp + 1, '\n');
	if (!cp)
		return;
	*cp++ = '\0';
	len = strlen(cp) + 1;
	/* strstr() will return NULL if ordering is wrong. */
	if (*cp == 'f') {
		argv0 = strstr(header, " argv[]={ \"");
		if (argv0) {
			argv0 += 10;
			len += tomoyo_truncate(argv0) + 14;
		}
		realpath = strstr(header, " exec={ realpath=\"");
		if (realpath) {
			realpath += 8;
			len += tomoyo_truncate(realpath) + 6;
		}
		symlink = strstr(header, " symlink.target=\"");
		if (symlink)
			len += tomoyo_truncate(symlink + 1) + 1;
	}
	buffer = kmalloc(len, GFP_NOFS);
	if (!buffer)
		return;
	snprintf(buffer, len - 1, "%s", cp);
	if (realpath)
		tomoyo_addprintf(buffer, len, " exec.%s", realpath);
	if (argv0)
		tomoyo_addprintf(buffer, len, " exec.argv[0]=%s", argv0);
	if (symlink)
		tomoyo_addprintf(buffer, len, "%s", symlink);
	tomoyo_normalize_line(buffer);
	if (!tomoyo_write_domain2(domain->ns, &domain->acl_info_list, buffer,
				  false))
		tomoyo_update_stat(TOMOYO_STAT_POLICY_UPDATES);
	kfree(buffer);
}

/**
 * tomoyo_supervisor - Ask for the supervisor's decision.
 *
 * @r:   Pointer to "struct tomoyo_request_info".
 * @fmt: The printf()'s format string, followed by parameters.
 *
 * Returns 0 if the supervisor decided to permit the access request which
 * violated the policy in enforcing mode, TOMOYO_RETRY_REQUEST if the
 * supervisor decided to retry the access request which violated the policy in
 * enforcing mode, 0 if it is not in enforcing mode, -EPERM otherwise.
 */
int tomoyo_supervisor(struct tomoyo_request_info *r, const char *fmt, ...)
{
	va_list args;
	int error;
	int len;
	static unsigned int tomoyo_serial;
	struct tomoyo_query entry = { };
	bool quota_exceeded = false;
	va_start(args, fmt);
	len = vsnprintf((char *) &len, 1, fmt, args) + 1;
	va_end(args);
	/* Write /sys/kernel/security/tomoyo/audit. */
	va_start(args, fmt);
	tomoyo_write_log2(r, len, fmt, args);
	va_end(args);
	/* Nothing more to do if granted. */
	if (r->granted)
		return 0;
	if (r->mode)
		tomoyo_update_stat(r->mode);
	switch (r->mode) {
	case TOMOYO_CONFIG_ENFORCING:
		error = -EPERM;
		if (atomic_read(&tomoyo_query_observers))
			break;
		goto out;
	case TOMOYO_CONFIG_LEARNING:
		error = 0;
		/* Check max_learning_entry parameter. */
		if (tomoyo_domain_quota_is_ok(r))
			break;
		/* fall through */
	default:
		return 0;
	}
	/* Get message. */
	va_start(args, fmt);
	entry.query = tomoyo_init_log(r, len, fmt, args);
	va_end(args);
	if (!entry.query)
		goto out;
	entry.query_len = strlen(entry.query) + 1;
	if (!error) {
		tomoyo_add_entry(r->domain, entry.query);
		goto out;
	}
	len = tomoyo_round2(entry.query_len);
	entry.domain = r->domain;
	spin_lock(&tomoyo_query_list_lock);
	if (tomoyo_memory_quota[TOMOYO_MEMORY_QUERY] &&
	    tomoyo_memory_used[TOMOYO_MEMORY_QUERY] + len
	    >= tomoyo_memory_quota[TOMOYO_MEMORY_QUERY]) {
		quota_exceeded = true;
	} else {
		entry.serial = tomoyo_serial++;
		entry.retry = r->retry;
		tomoyo_memory_used[TOMOYO_MEMORY_QUERY] += len;
		list_add_tail(&entry.list, &tomoyo_query_list);
	}
	spin_unlock(&tomoyo_query_list_lock);
	if (quota_exceeded)
		goto out;
	/* Give 10 seconds for supervisor's opinion. */
	while (entry.timer < 10) {
		wake_up_all(&tomoyo_query_wait);
		if (wait_event_interruptible_timeout
		    (tomoyo_answer_wait, entry.answer ||
		     !atomic_read(&tomoyo_query_observers), HZ))
			break;
		else
			entry.timer++;
	}
	spin_lock(&tomoyo_query_list_lock);
	list_del(&entry.list);
	tomoyo_memory_used[TOMOYO_MEMORY_QUERY] -= len;
	spin_unlock(&tomoyo_query_list_lock);
	switch (entry.answer) {
	case 3: /* Asked to retry by administrator. */
		error = TOMOYO_RETRY_REQUEST;
		r->retry++;
		break;
	case 1:
		/* Granted by administrator. */
		error = 0;
		break;
	default:
		/* Timed out or rejected by administrator. */
		break;
	}
out:
	kfree(entry.query);
	return error;
}

/**
 * tomoyo_find_domain_by_qid - Get domain by query id.
 *
 * @serial: Query ID assigned by tomoyo_supervisor().
 *
 * Returns pointer to "struct tomoyo_domain_info" if found, NULL otherwise.
 */
static struct tomoyo_domain_info *tomoyo_find_domain_by_qid
(unsigned int serial)
{
	struct tomoyo_query *ptr;
	struct tomoyo_domain_info *domain = NULL;
	spin_lock(&tomoyo_query_list_lock);
	list_for_each_entry(ptr, &tomoyo_query_list, list) {
		if (ptr->serial != serial)
			continue;
		domain = ptr->domain;
		break;
	}
	spin_unlock(&tomoyo_query_list_lock);
	return domain;
}

/**
 * tomoyo_poll_query - poll() for /sys/kernel/security/tomoyo/query.
 *
 * @file: Pointer to "struct file".
 * @wait: Pointer to "poll_table".
 *
 * Returns EPOLLIN | EPOLLRDNORM when ready to read, 0 otherwise.
 *
 * Waits for access requests which violated policy in enforcing mode.
 */
static __poll_t tomoyo_poll_query(struct file *file, poll_table *wait)
{
	if (!list_empty(&tomoyo_query_list))
		return EPOLLIN | EPOLLRDNORM;
	poll_wait(file, &tomoyo_query_wait, wait);
	if (!list_empty(&tomoyo_query_list))
		return EPOLLIN | EPOLLRDNORM;
	return 0;
}

/**
 * tomoyo_read_query - Read access requests which violated policy in enforcing mode.
 *
 * @head: Pointer to "struct tomoyo_io_buffer".
 */
static void tomoyo_read_query(struct tomoyo_io_buffer *head)
{
	struct list_head *tmp;
	unsigned int pos = 0;
	size_t len = 0;
	char *buf;
	if (head->r.w_pos)
		return;
	if (head->read_buf) {
		kfree(head->read_buf);
		head->read_buf = NULL;
	}
	spin_lock(&tomoyo_query_list_lock);
	list_for_each(tmp, &tomoyo_query_list) {
		struct tomoyo_query *ptr = list_entry(tmp, typeof(*ptr), list);
		if (pos++ != head->r.query_index)
			continue;
		len = ptr->query_len;
		break;
	}
	spin_unlock(&tomoyo_query_list_lock);
	if (!len) {
		head->r.query_index = 0;
		return;
	}
	buf = kzalloc(len + 32, GFP_NOFS);
	if (!buf)
		return;
	pos = 0;
	spin_lock(&tomoyo_query_list_lock);
	list_for_each(tmp, &tomoyo_query_list) {
		struct tomoyo_query *ptr = list_entry(tmp, typeof(*ptr), list);
		if (pos++ != head->r.query_index)
			continue;
		/*
		 * Some query can be skipped because tomoyo_query_list
		 * can change, but I don't care.
		 */
		if (len == ptr->query_len)
			snprintf(buf, len + 31, "Q%u-%hu\n%s", ptr->serial,
				 ptr->retry, ptr->query);
		break;
	}
	spin_unlock(&tomoyo_query_list_lock);
	if (buf[0]) {
		head->read_buf = buf;
		head->r.w[head->r.w_pos++] = buf;
		head->r.query_index++;
	} else {
		kfree(buf);
	}
}

/**
 * tomoyo_write_answer - Write the supervisor's decision.
 *
 * @head: Pointer to "struct tomoyo_io_buffer".
 *
 * Returns 0 on success, -EINVAL otherwise.
 */
static int tomoyo_write_answer(struct tomoyo_io_buffer *head)
{
	char *data = head->write_buf;
	struct list_head *tmp;
	unsigned int serial;
	unsigned int answer;
	spin_lock(&tomoyo_query_list_lock);
	list_for_each(tmp, &tomoyo_query_list) {
		struct tomoyo_query *ptr = list_entry(tmp, typeof(*ptr), list);
		ptr->timer = 0;
	}
	spin_unlock(&tomoyo_query_list_lock);
	if (sscanf(data, "A%u=%u", &serial, &answer) != 2)
		return -EINVAL;
	spin_lock(&tomoyo_query_list_lock);
	list_for_each(tmp, &tomoyo_query_list) {
		struct tomoyo_query *ptr = list_entry(tmp, typeof(*ptr), list);
		if (ptr->serial != serial)
			continue;
		ptr->answer = answer;
		/* Remove from tomoyo_query_list. */
		if (ptr->answer)
			list_del_init(&ptr->list);
		break;
	}
	spin_unlock(&tomoyo_query_list_lock);
	return 0;
}

/**
 * tomoyo_read_version: Get version.
 *
 * @head: Pointer to "struct tomoyo_io_buffer".
 *
 * Returns version information.
 */
static void tomoyo_read_version(struct tomoyo_io_buffer *head)
{
	if (!head->r.eof) {
		tomoyo_io_printf(head, "2.5.0");
		head->r.eof = true;
	}
}

/* String table for /sys/kernel/security/tomoyo/stat interface. */
static const char * const tomoyo_policy_headers[TOMOYO_MAX_POLICY_STAT] = {
	[TOMOYO_STAT_POLICY_UPDATES]    = "update:",
	[TOMOYO_STAT_POLICY_LEARNING]   = "violation in learning mode:",
	[TOMOYO_STAT_POLICY_PERMISSIVE] = "violation in permissive mode:",
	[TOMOYO_STAT_POLICY_ENFORCING]  = "violation in enforcing mode:",
};

/* String table for /sys/kernel/security/tomoyo/stat interface. */
static const char * const tomoyo_memory_headers[TOMOYO_MAX_MEMORY_STAT] = {
	[TOMOYO_MEMORY_POLICY] = "policy:",
	[TOMOYO_MEMORY_AUDIT]  = "audit log:",
	[TOMOYO_MEMORY_QUERY]  = "query message:",
};

/* Timestamp counter for last updated. */
static unsigned int tomoyo_stat_updated[TOMOYO_MAX_POLICY_STAT];
/* Counter for number of updates. */
static time64_t tomoyo_stat_modified[TOMOYO_MAX_POLICY_STAT];

/**
 * tomoyo_update_stat - Update statistic counters.
 *
 * @index: Index for policy type.
 *
 * Returns nothing.
 */
void tomoyo_update_stat(const u8 index)
{
	/*
	 * I don't use atomic operations because race condition is not fatal.
	 */
	tomoyo_stat_updated[index]++;
	tomoyo_stat_modified[index] = ktime_get_real_seconds();
}

/**
 * tomoyo_read_stat - Read statistic data.
 *
 * @head: Pointer to "struct tomoyo_io_buffer".
 *
 * Returns nothing.
 */
static void tomoyo_read_stat(struct tomoyo_io_buffer *head)
{
	u8 i;
	unsigned int total = 0;
	if (head->r.eof)
		return;
	for (i = 0; i < TOMOYO_MAX_POLICY_STAT; i++) {
		tomoyo_io_printf(head, "Policy %-30s %10u",
				 tomoyo_policy_headers[i],
				 tomoyo_stat_updated[i]);
		if (tomoyo_stat_modified[i]) {
			struct tomoyo_time stamp;
			tomoyo_convert_time(tomoyo_stat_modified[i], &stamp);
			tomoyo_io_printf(head, " (Last: %04u/%02u/%02u "
					 "%02u:%02u:%02u)",
					 stamp.year, stamp.month, stamp.day,
					 stamp.hour, stamp.min, stamp.sec);
		}
		tomoyo_set_lf(head);
	}
	for (i = 0; i < TOMOYO_MAX_MEMORY_STAT; i++) {
		unsigned int used = tomoyo_memory_used[i];
		total += used;
		tomoyo_io_printf(head, "Memory used by %-22s %10u",
				 tomoyo_memory_headers[i], used);
		used = tomoyo_memory_quota[i];
		if (used)
			tomoyo_io_printf(head, " (Quota: %10u)", used);
		tomoyo_set_lf(head);
	}
	tomoyo_io_printf(head, "Total memory used:                    %10u\n",
			 total);
	head->r.eof = true;
}

/**
 * tomoyo_write_stat - Set memory quota.
 *
 * @head: Pointer to "struct tomoyo_io_buffer".
 *
 * Returns 0.
 */
static int tomoyo_write_stat(struct tomoyo_io_buffer *head)
{
	char *data = head->write_buf;
	u8 i;
	if (tomoyo_str_starts(&data, "Memory used by "))
		for (i = 0; i < TOMOYO_MAX_MEMORY_STAT; i++)
			if (tomoyo_str_starts(&data, tomoyo_memory_headers[i]))
				sscanf(data, "%u", &tomoyo_memory_quota[i]);
	return 0;
}

/**
 * tomoyo_open_control - open() for /sys/kernel/security/tomoyo/ interface.
 *
 * @type: Type of interface.
 * @file: Pointer to "struct file".
 *
 * Returns 0 on success, negative value otherwise.
 */
int tomoyo_open_control(const u8 type, struct file *file)
{
	struct tomoyo_io_buffer *head = kzalloc(sizeof(*head), GFP_NOFS);

	if (!head)
		return -ENOMEM;
	mutex_init(&head->io_sem);
	head->type = type;
	switch (type) {
	case TOMOYO_DOMAINPOLICY:
		/* /sys/kernel/security/tomoyo/domain_policy */
		head->write = tomoyo_write_domain;
		head->read = tomoyo_read_domain;
		break;
	case TOMOYO_EXCEPTIONPOLICY:
		/* /sys/kernel/security/tomoyo/exception_policy */
		head->write = tomoyo_write_exception;
		head->read = tomoyo_read_exception;
		break;
	case TOMOYO_AUDIT:
		/* /sys/kernel/security/tomoyo/audit */
		head->poll = tomoyo_poll_log;
		head->read = tomoyo_read_log;
		break;
	case TOMOYO_PROCESS_STATUS:
		/* /sys/kernel/security/tomoyo/.process_status */
		head->write = tomoyo_write_pid;
		head->read = tomoyo_read_pid;
		break;
	case TOMOYO_VERSION:
		/* /sys/kernel/security/tomoyo/version */
		head->read = tomoyo_read_version;
		head->readbuf_size = 128;
		break;
	case TOMOYO_STAT:
		/* /sys/kernel/security/tomoyo/stat */
		head->write = tomoyo_write_stat;
		head->read = tomoyo_read_stat;
		head->readbuf_size = 1024;
		break;
	case TOMOYO_PROFILE:
		/* /sys/kernel/security/tomoyo/profile */
		head->write = tomoyo_write_profile;
		head->read = tomoyo_read_profile;
		break;
	case TOMOYO_QUERY: /* /sys/kernel/security/tomoyo/query */
		head->poll = tomoyo_poll_query;
		head->write = tomoyo_write_answer;
		head->read = tomoyo_read_query;
		break;
	case TOMOYO_MANAGER:
		/* /sys/kernel/security/tomoyo/manager */
		head->write = tomoyo_write_manager;
		head->read = tomoyo_read_manager;
		break;
	}
	if (!(file->f_mode & FMODE_READ)) {
		/*
		 * No need to allocate read_buf since it is not opened
		 * for reading.
		 */
		head->read = NULL;
		head->poll = NULL;
	} else if (!head->poll) {
		/* Don't allocate read_buf for poll() access. */
		if (!head->readbuf_size)
			head->readbuf_size = 4096 * 2;
		head->read_buf = kzalloc(head->readbuf_size, GFP_NOFS);
		if (!head->read_buf) {
			kfree(head);
			return -ENOMEM;
		}
	}
	if (!(file->f_mode & FMODE_WRITE)) {
		/*
		 * No need to allocate write_buf since it is not opened
		 * for writing.
		 */
		head->write = NULL;
	} else if (head->write) {
		head->writebuf_size = 4096 * 2;
		head->write_buf = kzalloc(head->writebuf_size, GFP_NOFS);
		if (!head->write_buf) {
			kfree(head->read_buf);
			kfree(head);
			return -ENOMEM;
		}
	}
	/*
	 * If the file is /sys/kernel/security/tomoyo/query , increment the
	 * observer counter.
	 * The obserber counter is used by tomoyo_supervisor() to see if
	 * there is some process monitoring /sys/kernel/security/tomoyo/query.
	 */
	if (type == TOMOYO_QUERY)
		atomic_inc(&tomoyo_query_observers);
	file->private_data = head;
	tomoyo_notify_gc(head, true);
	return 0;
}

/**
 * tomoyo_poll_control - poll() for /sys/kernel/security/tomoyo/ interface.
 *
 * @file: Pointer to "struct file".
 * @wait: Pointer to "poll_table". Maybe NULL.
 *
 * Returns EPOLLIN | EPOLLRDNORM | EPOLLOUT | EPOLLWRNORM if ready to read/write,
 * EPOLLOUT | EPOLLWRNORM otherwise.
 */
__poll_t tomoyo_poll_control(struct file *file, poll_table *wait)
{
	struct tomoyo_io_buffer *head = file->private_data;
	if (head->poll)
		return head->poll(file, wait) | EPOLLOUT | EPOLLWRNORM;
	return EPOLLIN | EPOLLRDNORM | EPOLLOUT | EPOLLWRNORM;
}

/**
 * tomoyo_set_namespace_cursor - Set namespace to read.
 *
 * @head: Pointer to "struct tomoyo_io_buffer".
 *
 * Returns nothing.
 */
static inline void tomoyo_set_namespace_cursor(struct tomoyo_io_buffer *head)
{
	struct list_head *ns;
	if (head->type != TOMOYO_EXCEPTIONPOLICY &&
	    head->type != TOMOYO_PROFILE)
		return;
	/*
	 * If this is the first read, or reading previous namespace finished
	 * and has more namespaces to read, update the namespace cursor.
	 */
	ns = head->r.ns;
	if (!ns || (head->r.eof && ns->next != &tomoyo_namespace_list)) {
		/* Clearing is OK because tomoyo_flush() returned true. */
		memset(&head->r, 0, sizeof(head->r));
		head->r.ns = ns ? ns->next : tomoyo_namespace_list.next;
	}
}

/**
 * tomoyo_has_more_namespace - Check for unread namespaces.
 *
 * @head: Pointer to "struct tomoyo_io_buffer".
 *
 * Returns true if we have more entries to print, false otherwise.
 */
static inline bool tomoyo_has_more_namespace(struct tomoyo_io_buffer *head)
{
	return (head->type == TOMOYO_EXCEPTIONPOLICY ||
		head->type == TOMOYO_PROFILE) && head->r.eof &&
		head->r.ns->next != &tomoyo_namespace_list;
}

/**
 * tomoyo_read_control - read() for /sys/kernel/security/tomoyo/ interface.
 *
 * @head:       Pointer to "struct tomoyo_io_buffer".
 * @buffer:     Poiner to buffer to write to.
 * @buffer_len: Size of @buffer.
 *
 * Returns bytes read on success, negative value otherwise.
 */
ssize_t tomoyo_read_control(struct tomoyo_io_buffer *head, char __user *buffer,
			    const int buffer_len)
{
	int len;
	int idx;

	if (!head->read)
		return -ENOSYS;
	if (mutex_lock_interruptible(&head->io_sem))
		return -EINTR;
	head->read_user_buf = buffer;
	head->read_user_buf_avail = buffer_len;
	idx = tomoyo_read_lock();
	if (tomoyo_flush(head))
		/* Call the policy handler. */
		do {
			tomoyo_set_namespace_cursor(head);
			head->read(head);
		} while (tomoyo_flush(head) &&
			 tomoyo_has_more_namespace(head));
	tomoyo_read_unlock(idx);
	len = head->read_user_buf - buffer;
	mutex_unlock(&head->io_sem);
	return len;
}

/**
 * tomoyo_parse_policy - Parse a policy line.
 *
 * @head: Poiter to "struct tomoyo_io_buffer".
 * @line: Line to parse.
 *
 * Returns 0 on success, negative value otherwise.
 *
 * Caller holds tomoyo_read_lock().
 */
static int tomoyo_parse_policy(struct tomoyo_io_buffer *head, char *line)
{
	/* Delete request? */
	head->w.is_delete = !strncmp(line, "delete ", 7);
	if (head->w.is_delete)
		memmove(line, line + 7, strlen(line + 7) + 1);
	/* Selecting namespace to update. */
	if (head->type == TOMOYO_EXCEPTIONPOLICY ||
	    head->type == TOMOYO_PROFILE) {
		if (*line == '<') {
			char *cp = strchr(line, ' ');
			if (cp) {
				*cp++ = '\0';
				head->w.ns = tomoyo_assign_namespace(line);
				memmove(line, cp, strlen(cp) + 1);
			} else
				head->w.ns = NULL;
		} else
			head->w.ns = &tomoyo_kernel_namespace;
		/* Don't allow updating if namespace is invalid. */
		if (!head->w.ns)
			return -ENOENT;
	}
	/* Do the update. */
	return head->write(head);
}

/**
 * tomoyo_write_control - write() for /sys/kernel/security/tomoyo/ interface.
 *
 * @head:       Pointer to "struct tomoyo_io_buffer".
 * @buffer:     Pointer to buffer to read from.
 * @buffer_len: Size of @buffer.
 *
 * Returns @buffer_len on success, negative value otherwise.
 */
ssize_t tomoyo_write_control(struct tomoyo_io_buffer *head,
			     const char __user *buffer, const int buffer_len)
{
	int error = buffer_len;
	size_t avail_len = buffer_len;
	char *cp0 = head->write_buf;
	int idx;
	if (!head->write)
		return -ENOSYS;
	if (!access_ok(buffer, buffer_len))
		return -EFAULT;
	if (mutex_lock_interruptible(&head->io_sem))
		return -EINTR;
	head->read_user_buf_avail = 0;
	idx = tomoyo_read_lock();
	/* Read a line and dispatch it to the policy handler. */
	while (avail_len > 0) {
		char c;
		if (head->w.avail >= head->writebuf_size - 1) {
			const int len = head->writebuf_size * 2;
			char *cp = kzalloc(len, GFP_NOFS);
			if (!cp) {
				error = -ENOMEM;
				break;
			}
			memmove(cp, cp0, head->w.avail);
			kfree(cp0);
			head->write_buf = cp;
			cp0 = cp;
			head->writebuf_size = len;
		}
		if (get_user(c, buffer)) {
			error = -EFAULT;
			break;
		}
		buffer++;
		avail_len--;
		cp0[head->w.avail++] = c;
		if (c != '\n')
			continue;
		cp0[head->w.avail - 1] = '\0';
		head->w.avail = 0;
		tomoyo_normalize_line(cp0);
		if (!strcmp(cp0, "reset")) {
			head->w.ns = &tomoyo_kernel_namespace;
			head->w.domain = NULL;
			memset(&head->r, 0, sizeof(head->r));
			continue;
		}
		/* Don't allow updating policies by non manager programs. */
		switch (head->type) {
		case TOMOYO_PROCESS_STATUS:
			/* This does not write anything. */
			break;
		case TOMOYO_DOMAINPOLICY:
			if (tomoyo_select_domain(head, cp0))
				continue;
			/* fall through */
		case TOMOYO_EXCEPTIONPOLICY:
			if (!strcmp(cp0, "select transition_only")) {
				head->r.print_transition_related_only = true;
				continue;
			}
			/* fall through */
		default:
			if (!tomoyo_manager()) {
				error = -EPERM;
				goto out;
			}
		}
		switch (tomoyo_parse_policy(head, cp0)) {
		case -EPERM:
			error = -EPERM;
			goto out;
		case 0:
			switch (head->type) {
			case TOMOYO_DOMAINPOLICY:
			case TOMOYO_EXCEPTIONPOLICY:
			case TOMOYO_STAT:
			case TOMOYO_PROFILE:
			case TOMOYO_MANAGER:
				tomoyo_update_stat(TOMOYO_STAT_POLICY_UPDATES);
				break;
			default:
				break;
			}
			break;
		}
	}
out:
	tomoyo_read_unlock(idx);
	mutex_unlock(&head->io_sem);
	return error;
}

/**
 * tomoyo_close_control - close() for /sys/kernel/security/tomoyo/ interface.
 *
 * @head: Pointer to "struct tomoyo_io_buffer".
 */
void tomoyo_close_control(struct tomoyo_io_buffer *head)
{
	/*
	 * If the file is /sys/kernel/security/tomoyo/query , decrement the
	 * observer counter.
	 */
	if (head->type == TOMOYO_QUERY &&
	    atomic_dec_and_test(&tomoyo_query_observers))
		wake_up_all(&tomoyo_answer_wait);
	tomoyo_notify_gc(head, false);
}

/**
 * tomoyo_check_profile - Check all profiles currently assigned to domains are defined.
 */
void tomoyo_check_profile(void)
{
	struct tomoyo_domain_info *domain;
	const int idx = tomoyo_read_lock();
	tomoyo_policy_loaded = true;
	printk(KERN_INFO "TOMOYO: 2.5.0\n");
	list_for_each_entry_rcu(domain, &tomoyo_domain_list, list) {
		const u8 profile = domain->profile;
		const struct tomoyo_policy_namespace *ns = domain->ns;
		if (ns->profile_version != 20110903)
			printk(KERN_ERR
			       "Profile version %u is not supported.\n",
			       ns->profile_version);
		else if (!ns->profile_ptr[profile])
			printk(KERN_ERR
			       "Profile %u (used by '%s') is not defined.\n",
			       profile, domain->domainname->name);
		else
			continue;
		printk(KERN_ERR
		       "Userland tools for TOMOYO 2.5 must be installed and "
		       "policy must be initialized.\n");
		printk(KERN_ERR "Please see http://tomoyo.sourceforge.jp/2.5/ "
		       "for more information.\n");
		panic("STOP!");
	}
	tomoyo_read_unlock(idx);
	printk(KERN_INFO "Mandatory Access Control activated.\n");
}

/**
 * tomoyo_load_builtin_policy - Load built-in policy.
 *
 * Returns nothing.
 */
void __init tomoyo_load_builtin_policy(void)
{
	/*
	 * This include file is manually created and contains built-in policy
	 * named "tomoyo_builtin_profile", "tomoyo_builtin_exception_policy",
	 * "tomoyo_builtin_domain_policy", "tomoyo_builtin_manager",
	 * "tomoyo_builtin_stat" in the form of "static char [] __initdata".
	 */
#include "builtin-policy.h"
	u8 i;
	const int idx = tomoyo_read_lock();
	for (i = 0; i < 5; i++) {
		struct tomoyo_io_buffer head = { };
		char *start = "";
		switch (i) {
		case 0:
			start = tomoyo_builtin_profile;
			head.type = TOMOYO_PROFILE;
			head.write = tomoyo_write_profile;
			break;
		case 1:
			start = tomoyo_builtin_exception_policy;
			head.type = TOMOYO_EXCEPTIONPOLICY;
			head.write = tomoyo_write_exception;
			break;
		case 2:
			start = tomoyo_builtin_domain_policy;
			head.type = TOMOYO_DOMAINPOLICY;
			head.write = tomoyo_write_domain;
			break;
		case 3:
			start = tomoyo_builtin_manager;
			head.type = TOMOYO_MANAGER;
			head.write = tomoyo_write_manager;
			break;
		case 4:
			start = tomoyo_builtin_stat;
			head.type = TOMOYO_STAT;
			head.write = tomoyo_write_stat;
			break;
		}
		while (1) {
			char *end = strchr(start, '\n');
			if (!end)
				break;
			*end = '\0';
			tomoyo_normalize_line(start);
			head.write_buf = start;
			tomoyo_parse_policy(&head, start);
			start = end + 1;
		}
	}
	tomoyo_read_unlock(idx);
#ifdef CONFIG_SECURITY_TOMOYO_OMIT_USERSPACE_LOADER
	tomoyo_check_profile();
#endif
}
