/*
 * security/tomoyo/common.c
 *
 * Common functions for TOMOYO.
 *
 * Copyright (C) 2005-2010  NTT DATA CORPORATION
 */

#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/security.h>
#include "common.h"

/* Profile version. Currently only 20090903 is defined. */
static unsigned int tomoyo_profile_version;

/* Profile table. Memory is allocated as needed. */
static struct tomoyo_profile *tomoyo_profile_ptr[TOMOYO_MAX_PROFILES];

/* String table for operation mode. */
const char * const tomoyo_mode[TOMOYO_CONFIG_MAX_MODE] = {
	[TOMOYO_CONFIG_DISABLED]   = "disabled",
	[TOMOYO_CONFIG_LEARNING]   = "learning",
	[TOMOYO_CONFIG_PERMISSIVE] = "permissive",
	[TOMOYO_CONFIG_ENFORCING]  = "enforcing"
};

/* String table for /sys/kernel/security/tomoyo/profile */
static const char *tomoyo_mac_keywords[TOMOYO_MAX_MAC_INDEX
				       + TOMOYO_MAX_MAC_CATEGORY_INDEX] = {
	[TOMOYO_MAC_FILE_EXECUTE]    = "file::execute",
	[TOMOYO_MAC_FILE_OPEN]       = "file::open",
	[TOMOYO_MAC_FILE_CREATE]     = "file::create",
	[TOMOYO_MAC_FILE_UNLINK]     = "file::unlink",
	[TOMOYO_MAC_FILE_GETATTR]    = "file::getattr",
	[TOMOYO_MAC_FILE_MKDIR]      = "file::mkdir",
	[TOMOYO_MAC_FILE_RMDIR]      = "file::rmdir",
	[TOMOYO_MAC_FILE_MKFIFO]     = "file::mkfifo",
	[TOMOYO_MAC_FILE_MKSOCK]     = "file::mksock",
	[TOMOYO_MAC_FILE_TRUNCATE]   = "file::truncate",
	[TOMOYO_MAC_FILE_SYMLINK]    = "file::symlink",
	[TOMOYO_MAC_FILE_MKBLOCK]    = "file::mkblock",
	[TOMOYO_MAC_FILE_MKCHAR]     = "file::mkchar",
	[TOMOYO_MAC_FILE_LINK]       = "file::link",
	[TOMOYO_MAC_FILE_RENAME]     = "file::rename",
	[TOMOYO_MAC_FILE_CHMOD]      = "file::chmod",
	[TOMOYO_MAC_FILE_CHOWN]      = "file::chown",
	[TOMOYO_MAC_FILE_CHGRP]      = "file::chgrp",
	[TOMOYO_MAC_FILE_IOCTL]      = "file::ioctl",
	[TOMOYO_MAC_FILE_CHROOT]     = "file::chroot",
	[TOMOYO_MAC_FILE_MOUNT]      = "file::mount",
	[TOMOYO_MAC_FILE_UMOUNT]     = "file::unmount",
	[TOMOYO_MAC_FILE_PIVOT_ROOT] = "file::pivot_root",
	[TOMOYO_MAX_MAC_INDEX + TOMOYO_MAC_CATEGORY_FILE] = "file",
};

/* String table for PREFERENCE keyword. */
static const char * const tomoyo_pref_keywords[TOMOYO_MAX_PREF] = {
	[TOMOYO_PREF_MAX_AUDIT_LOG]      = "max_audit_log",
	[TOMOYO_PREF_MAX_LEARNING_ENTRY] = "max_learning_entry",
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
		int len = strlen(w);
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

/**
 * tomoyo_io_printf - printf() to "struct tomoyo_io_buffer" structure.
 *
 * @head: Pointer to "struct tomoyo_io_buffer".
 * @fmt:  The printf()'s format string, followed by parameters.
 */
void tomoyo_io_printf(struct tomoyo_io_buffer *head, const char *fmt, ...)
{
	va_list args;
	int len;
	int pos = head->r.avail;
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
 * tomoyo_print_number_union - Print a tomoyo_number_union.
 *
 * @head:       Pointer to "struct tomoyo_io_buffer".
 * @ptr:        Pointer to "struct tomoyo_number_union".
 */
static void tomoyo_print_number_union(struct tomoyo_io_buffer *head,
				      const struct tomoyo_number_union *ptr)
{
	tomoyo_set_space(head);
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
				tomoyo_addprintf(buffer, sizeof(buffer),
						 "%lu", min);
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
 * tomoyo_assign_profile - Create a new profile.
 *
 * @profile: Profile number to create.
 *
 * Returns pointer to "struct tomoyo_profile" on success, NULL otherwise.
 */
static struct tomoyo_profile *tomoyo_assign_profile(const unsigned int profile)
{
	struct tomoyo_profile *ptr;
	struct tomoyo_profile *entry;
	if (profile >= TOMOYO_MAX_PROFILES)
		return NULL;
	ptr = tomoyo_profile_ptr[profile];
	if (ptr)
		return ptr;
	entry = kzalloc(sizeof(*entry), GFP_NOFS);
	if (mutex_lock_interruptible(&tomoyo_policy_lock))
		goto out;
	ptr = tomoyo_profile_ptr[profile];
	if (!ptr && tomoyo_memory_ok(entry)) {
		ptr = entry;
		ptr->default_config = TOMOYO_CONFIG_DISABLED |
			TOMOYO_CONFIG_WANT_GRANT_LOG |
			TOMOYO_CONFIG_WANT_REJECT_LOG;
		memset(ptr->config, TOMOYO_CONFIG_USE_DEFAULT,
		       sizeof(ptr->config));
		ptr->pref[TOMOYO_PREF_MAX_AUDIT_LOG] = 1024;
		ptr->pref[TOMOYO_PREF_MAX_LEARNING_ENTRY] = 2048;
		mb(); /* Avoid out-of-order execution. */
		tomoyo_profile_ptr[profile] = ptr;
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
 * @profile: Profile number to find.
 *
 * Returns pointer to "struct tomoyo_profile".
 */
struct tomoyo_profile *tomoyo_profile(const u8 profile)
{
	static struct tomoyo_profile tomoyo_null_profile;
	struct tomoyo_profile *ptr = tomoyo_profile_ptr[profile];
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
			if (strcmp(name, tomoyo_mac_keywords[i]))
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
	if (sscanf(data, "PROFILE_VERSION=%u", &tomoyo_profile_version) == 1)
		return 0;
	i = simple_strtoul(data, &cp, 10);
	if (*cp != '-')
		return -EINVAL;
	data = cp + 1;
	profile = tomoyo_assign_profile(i);
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
	const struct tomoyo_profile *profile;
 next:
	index = head->r.index;
	profile = tomoyo_profile_ptr[index];
	switch (head->r.step) {
	case 0:
		tomoyo_io_printf(head, "PROFILE_VERSION=%u\n", 20090903);
		head->r.step++;
		break;
	case 1:
		for ( ; head->r.index < TOMOYO_MAX_PROFILES;
		      head->r.index++)
			if (tomoyo_profile_ptr[head->r.index])
				break;
		if (head->r.index == TOMOYO_MAX_PROFILES)
			return;
		head->r.step++;
		break;
	case 2:
		{
			u8 i;
			const struct tomoyo_path_info *comment =
				profile->comment;
			tomoyo_io_printf(head, "%u-COMMENT=", index);
			tomoyo_set_string(head, comment ? comment->name : "");
			tomoyo_set_lf(head);
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
			tomoyo_io_printf(head, "%u-%s%s", index, "CONFIG::",
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
		.is_delete = is_delete,
		.list = &tomoyo_policy_list[TOMOYO_ID_MANAGER],
	};
	int error = is_delete ? -ENOENT : -ENOMEM;
	if (tomoyo_domain_def(manager)) {
		if (!tomoyo_correct_domain(manager))
			return -EINVAL;
		e.is_domain = true;
	} else {
		if (!tomoyo_correct_path(manager))
			return -EINVAL;
	}
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
	bool is_delete = tomoyo_str_starts(&data, "delete ");

	if (!strcmp(data, "manage_by_non_root")) {
		tomoyo_manage_by_non_root = !is_delete;
		return 0;
	}
	return tomoyo_update_manager_entry(data, is_delete);
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
	list_for_each_cookie(head->r.acl,
			     &tomoyo_policy_list[TOMOYO_ID_MANAGER]) {
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
	if (!tomoyo_manage_by_non_root && (task->cred->uid || task->cred->euid))
		return false;
	list_for_each_entry_rcu(ptr, &tomoyo_policy_list[TOMOYO_ID_MANAGER],
				head.list) {
		if (!ptr->head.is_deleted && ptr->is_domain
		    && !tomoyo_pathcmp(domainname, ptr->manager)) {
			found = true;
			break;
		}
	}
	if (found)
		return true;
	exe = tomoyo_get_exe();
	if (!exe)
		return false;
	list_for_each_entry_rcu(ptr, &tomoyo_policy_list[TOMOYO_ID_MANAGER],
				head.list) {
		if (!ptr->head.is_deleted && !ptr->is_domain
		    && !strcmp(exe, ptr->manager->name)) {
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

/**
 * tomoyo_select_one - Parse select command.
 *
 * @head: Pointer to "struct tomoyo_io_buffer".
 * @data: String to parse.
 *
 * Returns true on success, false otherwise.
 *
 * Caller holds tomoyo_read_lock().
 */
static bool tomoyo_select_one(struct tomoyo_io_buffer *head, const char *data)
{
	unsigned int pid;
	struct tomoyo_domain_info *domain = NULL;
	bool global_pid = false;

	if (!strcmp(data, "allow_execute")) {
		head->r.print_execute_only = true;
		return true;
	}
	if (sscanf(data, "pid=%u", &pid) == 1 ||
	    (global_pid = true, sscanf(data, "global-pid=%u", &pid) == 1)) {
		struct task_struct *p;
		rcu_read_lock();
		read_lock(&tasklist_lock);
		if (global_pid)
			p = find_task_by_pid_ns(pid, &init_pid_ns);
		else
			p = find_task_by_vpid(pid);
		if (p)
			domain = tomoyo_real_domain(p);
		read_unlock(&tasklist_lock);
		rcu_read_unlock();
	} else if (!strncmp(data, "domain=", 7)) {
		if (tomoyo_domain_def(data + 7))
			domain = tomoyo_find_domain(data + 7);
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
 * tomoyo_delete_domain - Delete a domain.
 *
 * @domainname: The name of domain.
 *
 * Returns 0.
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
		return 0;
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
 * @list:      Pointer to "struct list_head".
 * @data:      Policy to be interpreted.
 * @is_delete: True if it is a delete request.
 *
 * Returns 0 on success, negative value otherwise.
 *
 * Caller holds tomoyo_read_lock().
 */
static int tomoyo_write_domain2(struct list_head *list, char *data,
				const bool is_delete)
{
	struct tomoyo_acl_param param = {
		.list = list,
		.data = data,
		.is_delete = is_delete,
	};
	static const struct {
		const char *keyword;
		int (*write) (struct tomoyo_acl_param *);
	} tomoyo_callback[1] = {
		{ "file ", tomoyo_write_file },
	};
	u8 i;
	for (i = 0; i < 1; i++) {
		if (!tomoyo_str_starts(&param.data,
				       tomoyo_callback[i].keyword))
			continue;
		return tomoyo_callback[i].write(&param);
	}
	return -EINVAL;
}

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
	struct tomoyo_domain_info *domain = head->w.domain;
	bool is_delete = false;
	bool is_select = false;
	unsigned int profile;

	if (tomoyo_str_starts(&data, "delete "))
		is_delete = true;
	else if (tomoyo_str_starts(&data, "select "))
		is_select = true;
	if (is_select && tomoyo_select_one(head, data))
		return 0;
	/* Don't allow updating policies by non manager programs. */
	if (!tomoyo_manager())
		return -EPERM;
	if (tomoyo_domain_def(data)) {
		domain = NULL;
		if (is_delete)
			tomoyo_delete_domain(data);
		else if (is_select)
			domain = tomoyo_find_domain(data);
		else
			domain = tomoyo_assign_domain(data, 0);
		head->w.domain = domain;
		return 0;
	}
	if (!domain)
		return -EINVAL;

	if (sscanf(data, "use_profile %u", &profile) == 1
	    && profile < TOMOYO_MAX_PROFILES) {
		if (tomoyo_profile_ptr[profile] || !tomoyo_policy_loaded)
			domain->profile = (u8) profile;
		return 0;
	}
	if (sscanf(data, "use_group %u\n", &profile) == 1
	    && profile < TOMOYO_MAX_ACL_GROUPS) {
		if (!is_delete)
			domain->group = (u8) profile;
		return 0;
	}
	if (!strcmp(data, "quota_exceeded")) {
		domain->quota_warned = !is_delete;
		return 0;
	}
	if (!strcmp(data, "transition_failed")) {
		domain->transition_failed = !is_delete;
		return 0;
	}
	return tomoyo_write_domain2(&domain->acl_info_list, data, is_delete);
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
	if (head->type == TOMOYO_EXCEPTIONPOLICY)
		tomoyo_io_printf(head, "acl_group %u ",
				 head->r.acl_group_index);
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
			if (head->r.print_execute_only &&
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
	} else if (head->r.print_execute_only) {
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
	} else if (acl_type == TOMOYO_TYPE_MOUNT_ACL) {
		struct tomoyo_mount_acl *ptr =
			container_of(acl, typeof(*ptr), head);
		tomoyo_set_group(head, "file mount");
		tomoyo_print_name_union(head, &ptr->dev_name);
		tomoyo_print_name_union(head, &ptr->dir_name);
		tomoyo_print_name_union(head, &ptr->fs_type);
		tomoyo_print_number_union(head, &ptr->flags);
	}
	tomoyo_set_lf(head);
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
			if (domain->quota_warned)
				tomoyo_set_string(head, "quota_exceeded\n");
			if (domain->transition_failed)
				tomoyo_set_string(head, "transition_failed\n");
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
 * tomoyo_write_domain_profile - Assign profile for specified domain.
 *
 * @head: Pointer to "struct tomoyo_io_buffer".
 *
 * Returns 0 on success, -EINVAL otherwise.
 *
 * This is equivalent to doing
 *
 *     ( echo "select " $domainname; echo "use_profile " $profile ) |
 *     /usr/sbin/tomoyo-loadpolicy -d
 *
 * Caller holds tomoyo_read_lock().
 */
static int tomoyo_write_domain_profile(struct tomoyo_io_buffer *head)
{
	char *data = head->write_buf;
	char *cp = strchr(data, ' ');
	struct tomoyo_domain_info *domain;
	unsigned long profile;

	if (!cp)
		return -EINVAL;
	*cp = '\0';
	domain = tomoyo_find_domain(cp + 1);
	if (strict_strtoul(data, 10, &profile))
		return -EINVAL;
	if (domain && profile < TOMOYO_MAX_PROFILES
	    && (tomoyo_profile_ptr[profile] || !tomoyo_policy_loaded))
		domain->profile = (u8) profile;
	return 0;
}

/**
 * tomoyo_read_domain_profile - Read only domainname and profile.
 *
 * @head: Pointer to "struct tomoyo_io_buffer".
 *
 * Returns list of profile number and domainname pairs.
 *
 * This is equivalent to doing
 *
 *     grep -A 1 '^<kernel>' /sys/kernel/security/tomoyo/domain_policy |
 *     awk ' { if ( domainname == "" ) { if ( $1 == "<kernel>" )
 *     domainname = $0; } else if ( $1 == "use_profile" ) {
 *     print $2 " " domainname; domainname = ""; } } ; '
 *
 * Caller holds tomoyo_read_lock().
 */
static void tomoyo_read_domain_profile(struct tomoyo_io_buffer *head)
{
	if (head->r.eof)
		return;
	list_for_each_cookie(head->r.domain, &tomoyo_domain_list) {
		struct tomoyo_domain_info *domain =
			list_entry(head->r.domain, typeof(*domain), list);
		if (domain->is_deleted)
			continue;
		if (!tomoyo_flush(head))
			return;
		tomoyo_io_printf(head, "%u ", domain->profile);
		tomoyo_set_string(head, domain->domainname->name);
		tomoyo_set_lf(head);
	}
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
	pid = (unsigned int) simple_strtoul(buf, NULL, 10);
	rcu_read_lock();
	read_lock(&tasklist_lock);
	if (global_pid)
		p = find_task_by_pid_ns(pid, &init_pid_ns);
	else
		p = find_task_by_vpid(pid);
	if (p)
		domain = tomoyo_real_domain(p);
	read_unlock(&tasklist_lock);
	rcu_read_unlock();
	if (!domain)
		return;
	tomoyo_io_printf(head, "%u %u ", pid, domain->profile);
	tomoyo_set_string(head, domain->domainname->name);
}

static const char *tomoyo_transition_type[TOMOYO_MAX_TRANSITION_TYPE] = {
	[TOMOYO_TRANSITION_CONTROL_NO_INITIALIZE] = "no_initialize_domain",
	[TOMOYO_TRANSITION_CONTROL_INITIALIZE]    = "initialize_domain",
	[TOMOYO_TRANSITION_CONTROL_NO_KEEP]       = "no_keep_domain",
	[TOMOYO_TRANSITION_CONTROL_KEEP]          = "keep_domain",
};

static const char *tomoyo_group_name[TOMOYO_MAX_GROUP] = {
	[TOMOYO_PATH_GROUP]   = "path_group ",
	[TOMOYO_NUMBER_GROUP] = "number_group ",
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
	struct tomoyo_acl_param param = {
		.data = head->write_buf,
	};
	u8 i;
	param.is_delete = tomoyo_str_starts(&param.data, "delete ");
	if (!param.is_delete && tomoyo_str_starts(&param.data, "select ") &&
	    !strcmp(param.data, "execute_only")) {
		head->r.print_execute_only = true;
		return 0;
	}
	/* Don't allow updating policies by non manager programs. */
	if (!tomoyo_manager())
		return -EPERM;
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
			return tomoyo_write_domain2(&tomoyo_acl_group[group],
						    data, param.is_delete);
	}
	return -EINVAL;
}

/**
 * tomoyo_read_group - Read "struct tomoyo_path_group"/"struct tomoyo_number_group" list.
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
	list_for_each_cookie(head->r.group, &tomoyo_group_list[idx]) {
		struct tomoyo_group *group =
			list_entry(head->r.group, typeof(*group), head.list);
		list_for_each_cookie(head->r.acl, &group->member_list) {
			struct tomoyo_acl_head *ptr =
				list_entry(head->r.acl, typeof(*ptr), list);
			if (ptr->is_deleted)
				continue;
			if (!tomoyo_flush(head))
				return false;
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
	list_for_each_cookie(head->r.acl, &tomoyo_policy_list[idx]) {
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
		if (!tomoyo_read_domain2(head, &tomoyo_acl_group
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
	char *cp = strchr(header, '\n');
	int len;
	if (!cp)
		return;
	cp = strchr(cp + 1, '\n');
	if (!cp)
		return;
	*cp++ = '\0';
	len = strlen(cp) + 1;
	buffer = kmalloc(len, GFP_NOFS);
	if (!buffer)
		return;
	snprintf(buffer, len - 1, "%s", cp);
	tomoyo_normalize_line(buffer);
	tomoyo_write_domain2(&domain->acl_info_list, buffer, false);
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
 * tomoyo_poll_query - poll() for /sys/kernel/security/tomoyo/query.
 *
 * @file: Pointer to "struct file".
 * @wait: Pointer to "poll_table".
 *
 * Returns POLLIN | POLLRDNORM when ready to read, 0 otherwise.
 *
 * Waits for access requests which violated policy in enforcing mode.
 */
static int tomoyo_poll_query(struct file *file, poll_table *wait)
{
	struct list_head *tmp;
	bool found = false;
	u8 i;
	for (i = 0; i < 2; i++) {
		spin_lock(&tomoyo_query_list_lock);
		list_for_each(tmp, &tomoyo_query_list) {
			struct tomoyo_query *ptr =
				list_entry(tmp, typeof(*ptr), list);
			if (ptr->answer)
				continue;
			found = true;
			break;
		}
		spin_unlock(&tomoyo_query_list_lock);
		if (found)
			return POLLIN | POLLRDNORM;
		if (i)
			break;
		poll_wait(file, &tomoyo_query_wait, wait);
	}
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
	int pos = 0;
	int len = 0;
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
		if (ptr->answer)
			continue;
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
		if (ptr->answer)
			continue;
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
		if (!ptr->answer)
			ptr->answer = answer;
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
		tomoyo_io_printf(head, "2.4.0");
		head->r.eof = true;
	}
}

/**
 * tomoyo_read_self_domain - Get the current process's domainname.
 *
 * @head: Pointer to "struct tomoyo_io_buffer".
 *
 * Returns the current process's domainname.
 */
static void tomoyo_read_self_domain(struct tomoyo_io_buffer *head)
{
	if (!head->r.eof) {
		/*
		 * tomoyo_domain()->domainname != NULL
		 * because every process belongs to a domain and
		 * the domain's name cannot be NULL.
		 */
		tomoyo_io_printf(head, "%s", tomoyo_domain()->domainname->name);
		head->r.eof = true;
	}
}

/**
 * tomoyo_open_control - open() for /sys/kernel/security/tomoyo/ interface.
 *
 * @type: Type of interface.
 * @file: Pointer to "struct file".
 *
 * Associates policy handler and returns 0 on success, -ENOMEM otherwise.
 *
 * Caller acquires tomoyo_read_lock().
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
	case TOMOYO_SELFDOMAIN:
		/* /sys/kernel/security/tomoyo/self_domain */
		head->read = tomoyo_read_self_domain;
		break;
	case TOMOYO_DOMAIN_STATUS:
		/* /sys/kernel/security/tomoyo/.domain_status */
		head->write = tomoyo_write_domain_profile;
		head->read = tomoyo_read_domain_profile;
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
	case TOMOYO_MEMINFO:
		/* /sys/kernel/security/tomoyo/meminfo */
		head->write = tomoyo_write_memory_quota;
		head->read = tomoyo_read_memory_counter;
		head->readbuf_size = 512;
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
	if (type != TOMOYO_QUERY && type != TOMOYO_AUDIT)
		head->reader_idx = tomoyo_read_lock();
	file->private_data = head;
	/*
	 * If the file is /sys/kernel/security/tomoyo/query , increment the
	 * observer counter.
	 * The obserber counter is used by tomoyo_supervisor() to see if
	 * there is some process monitoring /sys/kernel/security/tomoyo/query.
	 */
	if (type == TOMOYO_QUERY)
		atomic_inc(&tomoyo_query_observers);
	return 0;
}

/**
 * tomoyo_poll_control - poll() for /sys/kernel/security/tomoyo/ interface.
 *
 * @file: Pointer to "struct file".
 * @wait: Pointer to "poll_table".
 *
 * Waits for read readiness.
 * /sys/kernel/security/tomoyo/query is handled by /usr/sbin/tomoyo-queryd and
 * /sys/kernel/security/tomoyo/audit is handled by /usr/sbin/tomoyo-auditd.
 */
int tomoyo_poll_control(struct file *file, poll_table *wait)
{
	struct tomoyo_io_buffer *head = file->private_data;
	if (!head->poll)
		return -ENOSYS;
	return head->poll(file, wait);
}

/**
 * tomoyo_read_control - read() for /sys/kernel/security/tomoyo/ interface.
 *
 * @head:       Pointer to "struct tomoyo_io_buffer".
 * @buffer:     Poiner to buffer to write to.
 * @buffer_len: Size of @buffer.
 *
 * Returns bytes read on success, negative value otherwise.
 *
 * Caller holds tomoyo_read_lock().
 */
int tomoyo_read_control(struct tomoyo_io_buffer *head, char __user *buffer,
			const int buffer_len)
{
	int len;

	if (!head->read)
		return -ENOSYS;
	if (mutex_lock_interruptible(&head->io_sem))
		return -EINTR;
	head->read_user_buf = buffer;
	head->read_user_buf_avail = buffer_len;
	if (tomoyo_flush(head))
		/* Call the policy handler. */
		head->read(head);
	tomoyo_flush(head);
	len = head->read_user_buf - buffer;
	mutex_unlock(&head->io_sem);
	return len;
}

/**
 * tomoyo_write_control - write() for /sys/kernel/security/tomoyo/ interface.
 *
 * @head:       Pointer to "struct tomoyo_io_buffer".
 * @buffer:     Pointer to buffer to read from.
 * @buffer_len: Size of @buffer.
 *
 * Returns @buffer_len on success, negative value otherwise.
 *
 * Caller holds tomoyo_read_lock().
 */
int tomoyo_write_control(struct tomoyo_io_buffer *head,
			 const char __user *buffer, const int buffer_len)
{
	int error = buffer_len;
	int avail_len = buffer_len;
	char *cp0 = head->write_buf;

	if (!head->write)
		return -ENOSYS;
	if (!access_ok(VERIFY_READ, buffer, buffer_len))
		return -EFAULT;
	/* Don't allow updating policies by non manager programs. */
	if (head->write != tomoyo_write_pid &&
	    head->write != tomoyo_write_domain &&
	    head->write != tomoyo_write_exception && !tomoyo_manager())
		return -EPERM;
	if (mutex_lock_interruptible(&head->io_sem))
		return -EINTR;
	/* Read a line and dispatch it to the policy handler. */
	while (avail_len > 0) {
		char c;
		if (head->w.avail >= head->writebuf_size - 1) {
			error = -ENOMEM;
			break;
		} else if (get_user(c, buffer)) {
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
		head->write(head);
	}
	mutex_unlock(&head->io_sem);
	return error;
}

/**
 * tomoyo_close_control - close() for /sys/kernel/security/tomoyo/ interface.
 *
 * @head: Pointer to "struct tomoyo_io_buffer".
 *
 * Releases memory and returns 0.
 *
 * Caller looses tomoyo_read_lock().
 */
int tomoyo_close_control(struct tomoyo_io_buffer *head)
{
	const bool is_write = !!head->write_buf;

	/*
	 * If the file is /sys/kernel/security/tomoyo/query , decrement the
	 * observer counter.
	 */
	if (head->type == TOMOYO_QUERY)
		atomic_dec(&tomoyo_query_observers);
	else if (head->type != TOMOYO_AUDIT)
		tomoyo_read_unlock(head->reader_idx);
	/* Release memory used for policy I/O. */
	kfree(head->read_buf);
	head->read_buf = NULL;
	kfree(head->write_buf);
	head->write_buf = NULL;
	kfree(head);
	if (is_write)
		tomoyo_run_gc();
	return 0;
}

/**
 * tomoyo_check_profile - Check all profiles currently assigned to domains are defined.
 */
void tomoyo_check_profile(void)
{
	struct tomoyo_domain_info *domain;
	const int idx = tomoyo_read_lock();
	tomoyo_policy_loaded = true;
	/* Check all profiles currently assigned to domains are defined. */
	list_for_each_entry_rcu(domain, &tomoyo_domain_list, list) {
		const u8 profile = domain->profile;
		if (tomoyo_profile_ptr[profile])
			continue;
		printk(KERN_ERR "You need to define profile %u before using it.\n",
		       profile);
		printk(KERN_ERR "Please see http://tomoyo.sourceforge.jp/2.3/ "
		       "for more information.\n");
		panic("Profile %u (used by '%s') not defined.\n",
		      profile, domain->domainname->name);
	}
	tomoyo_read_unlock(idx);
	if (tomoyo_profile_version != 20090903) {
		printk(KERN_ERR "You need to install userland programs for "
		       "TOMOYO 2.3 and initialize policy configuration.\n");
		printk(KERN_ERR "Please see http://tomoyo.sourceforge.jp/2.3/ "
		       "for more information.\n");
		panic("Profile version %u is not supported.\n",
		      tomoyo_profile_version);
	}
	printk(KERN_INFO "TOMOYO: 2.3.0\n");
	printk(KERN_INFO "Mandatory Access Control activated.\n");
}
