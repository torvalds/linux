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
#include <linux/hardirq.h>
#include "common.h"

/* String table for functionality that takes 4 modes. */
static const char *tomoyo_mode_4[4] = {
	"disabled", "learning", "permissive", "enforcing"
};
/* String table for functionality that takes 2 modes. */
static const char *tomoyo_mode_2[4] = {
	"disabled", "enabled", "enabled", "enabled"
};

/*
 * tomoyo_control_array is a static data which contains
 *
 *  (1) functionality name used by /sys/kernel/security/tomoyo/profile .
 *  (2) initial values for "struct tomoyo_profile".
 *  (3) max values for "struct tomoyo_profile".
 */
static struct {
	const char *keyword;
	unsigned int current_value;
	const unsigned int max_value;
} tomoyo_control_array[TOMOYO_MAX_CONTROL_INDEX] = {
	[TOMOYO_MAC_FOR_FILE]     = { "MAC_FOR_FILE",        0,       3 },
	[TOMOYO_MAX_ACCEPT_ENTRY] = { "MAX_ACCEPT_ENTRY", 2048, INT_MAX },
	[TOMOYO_VERBOSE]          = { "TOMOYO_VERBOSE",      1,       1 },
};

/*
 * tomoyo_profile is a structure which is used for holding the mode of access
 * controls. TOMOYO has 4 modes: disabled, learning, permissive, enforcing.
 * An administrator can define up to 256 profiles.
 * The ->profile of "struct tomoyo_domain_info" is used for remembering
 * the profile's number (0 - 255) assigned to that domain.
 */
static struct tomoyo_profile {
	unsigned int value[TOMOYO_MAX_CONTROL_INDEX];
	const struct tomoyo_path_info *comment;
} *tomoyo_profile_ptr[TOMOYO_MAX_PROFILES];

/* Permit policy management by non-root user? */
static bool tomoyo_manage_by_non_root;

/* Utility functions. */

/**
 * tomoyo_print_name_union - Print a tomoyo_name_union.
 *
 * @head: Pointer to "struct tomoyo_io_buffer".
 * @ptr:  Pointer to "struct tomoyo_name_union".
 *
 * Returns true on success, false otherwise.
 */
static bool tomoyo_print_name_union(struct tomoyo_io_buffer *head,
				 const struct tomoyo_name_union *ptr)
{
	int pos = head->read_avail;
	if (pos && head->read_buf[pos - 1] == ' ')
		head->read_avail--;
	if (ptr->is_group)
		return tomoyo_io_printf(head, " @%s",
					ptr->group->group_name->name);
	return tomoyo_io_printf(head, " %s", ptr->filename->name);
}

/**
 * tomoyo_print_number_union - Print a tomoyo_number_union.
 *
 * @head:       Pointer to "struct tomoyo_io_buffer".
 * @ptr:        Pointer to "struct tomoyo_number_union".
 *
 * Returns true on success, false otherwise.
 */
bool tomoyo_print_number_union(struct tomoyo_io_buffer *head,
			       const struct tomoyo_number_union *ptr)
{
	unsigned long min;
	unsigned long max;
	u8 min_type;
	u8 max_type;
	if (!tomoyo_io_printf(head, " "))
		return false;
	if (ptr->is_group)
		return tomoyo_io_printf(head, "@%s",
					ptr->group->group_name->name);
	min_type = ptr->min_type;
	max_type = ptr->max_type;
	min = ptr->values[0];
	max = ptr->values[1];
	switch (min_type) {
	case TOMOYO_VALUE_TYPE_HEXADECIMAL:
		if (!tomoyo_io_printf(head, "0x%lX", min))
			return false;
		break;
	case TOMOYO_VALUE_TYPE_OCTAL:
		if (!tomoyo_io_printf(head, "0%lo", min))
			return false;
		break;
	default:
		if (!tomoyo_io_printf(head, "%lu", min))
			return false;
		break;
	}
	if (min == max && min_type == max_type)
		return true;
	switch (max_type) {
	case TOMOYO_VALUE_TYPE_HEXADECIMAL:
		return tomoyo_io_printf(head, "-0x%lX", max);
	case TOMOYO_VALUE_TYPE_OCTAL:
		return tomoyo_io_printf(head, "-0%lo", max);
	default:
		return tomoyo_io_printf(head, "-%lu", max);
	}
}

/**
 * tomoyo_io_printf - Transactional printf() to "struct tomoyo_io_buffer" structure.
 *
 * @head: Pointer to "struct tomoyo_io_buffer".
 * @fmt:  The printf()'s format string, followed by parameters.
 *
 * Returns true if output was written, false otherwise.
 *
 * The snprintf() will truncate, but tomoyo_io_printf() won't.
 */
bool tomoyo_io_printf(struct tomoyo_io_buffer *head, const char *fmt, ...)
{
	va_list args;
	int len;
	int pos = head->read_avail;
	int size = head->readbuf_size - pos;

	if (size <= 0)
		return false;
	va_start(args, fmt);
	len = vsnprintf(head->read_buf + pos, size, fmt, args);
	va_end(args);
	if (pos + len >= head->readbuf_size)
		return false;
	head->read_avail += len;
	return true;
}

/**
 * tomoyo_check_flags - Check mode for specified functionality.
 *
 * @domain: Pointer to "struct tomoyo_domain_info".
 * @index:  The functionality to check mode.
 *
 * TOMOYO checks only process context.
 * This code disables TOMOYO's enforcement in case the function is called from
 * interrupt context.
 */
unsigned int tomoyo_check_flags(const struct tomoyo_domain_info *domain,
				const u8 index)
{
	const u8 profile = domain->profile;

	if (WARN_ON(in_interrupt()))
		return 0;
	return tomoyo_policy_loaded && index < TOMOYO_MAX_CONTROL_INDEX
#if TOMOYO_MAX_PROFILES != 256
		&& profile < TOMOYO_MAX_PROFILES
#endif
		&& tomoyo_profile_ptr[profile] ?
		tomoyo_profile_ptr[profile]->value[index] : 0;
}

/**
 * tomoyo_verbose_mode - Check whether TOMOYO is verbose mode.
 *
 * @domain: Pointer to "struct tomoyo_domain_info".
 *
 * Returns true if domain policy violation warning should be printed to
 * console.
 */
bool tomoyo_verbose_mode(const struct tomoyo_domain_info *domain)
{
	return tomoyo_check_flags(domain, TOMOYO_VERBOSE) != 0;
}

/**
 * tomoyo_find_or_assign_new_profile - Create a new profile.
 *
 * @profile: Profile number to create.
 *
 * Returns pointer to "struct tomoyo_profile" on success, NULL otherwise.
 */
static struct tomoyo_profile *tomoyo_find_or_assign_new_profile(const unsigned
								int profile)
{
	struct tomoyo_profile *ptr = NULL;
	int i;

	if (profile >= TOMOYO_MAX_PROFILES)
		return NULL;
	if (mutex_lock_interruptible(&tomoyo_policy_lock))
		return NULL;
	ptr = tomoyo_profile_ptr[profile];
	if (ptr)
		goto ok;
	ptr = kmalloc(sizeof(*ptr), GFP_NOFS);
	if (!tomoyo_memory_ok(ptr)) {
		kfree(ptr);
		ptr = NULL;
		goto ok;
	}
	for (i = 0; i < TOMOYO_MAX_CONTROL_INDEX; i++)
		ptr->value[i] = tomoyo_control_array[i].current_value;
	mb(); /* Avoid out-of-order execution. */
	tomoyo_profile_ptr[profile] = ptr;
 ok:
	mutex_unlock(&tomoyo_policy_lock);
	return ptr;
}

/**
 * tomoyo_write_profile - Write to profile table.
 *
 * @head: Pointer to "struct tomoyo_io_buffer".
 *
 * Returns 0 on success, negative value otherwise.
 */
static int tomoyo_write_profile(struct tomoyo_io_buffer *head)
{
	char *data = head->write_buf;
	unsigned int i;
	unsigned int value;
	char *cp;
	struct tomoyo_profile *profile;
	unsigned long num;

	cp = strchr(data, '-');
	if (cp)
		*cp = '\0';
	if (strict_strtoul(data, 10, &num))
		return -EINVAL;
	if (cp)
		data = cp + 1;
	profile = tomoyo_find_or_assign_new_profile(num);
	if (!profile)
		return -EINVAL;
	cp = strchr(data, '=');
	if (!cp)
		return -EINVAL;
	*cp = '\0';
	if (!strcmp(data, "COMMENT")) {
		const struct tomoyo_path_info *old_comment = profile->comment;
		profile->comment = tomoyo_get_name(cp + 1);
		tomoyo_put_name(old_comment);
		return 0;
	}
	for (i = 0; i < TOMOYO_MAX_CONTROL_INDEX; i++) {
		if (strcmp(data, tomoyo_control_array[i].keyword))
			continue;
		if (sscanf(cp + 1, "%u", &value) != 1) {
			int j;
			const char **modes;
			switch (i) {
			case TOMOYO_VERBOSE:
				modes = tomoyo_mode_2;
				break;
			default:
				modes = tomoyo_mode_4;
				break;
			}
			for (j = 0; j < 4; j++) {
				if (strcmp(cp + 1, modes[j]))
					continue;
				value = j;
				break;
			}
			if (j == 4)
				return -EINVAL;
		} else if (value > tomoyo_control_array[i].max_value) {
			value = tomoyo_control_array[i].max_value;
		}
		profile->value[i] = value;
		return 0;
	}
	return -EINVAL;
}

/**
 * tomoyo_read_profile - Read from profile table.
 *
 * @head: Pointer to "struct tomoyo_io_buffer".
 *
 * Returns 0.
 */
static int tomoyo_read_profile(struct tomoyo_io_buffer *head)
{
	static const int total = TOMOYO_MAX_CONTROL_INDEX + 1;
	int step;

	if (head->read_eof)
		return 0;
	for (step = head->read_step; step < TOMOYO_MAX_PROFILES * total;
	     step++) {
		const u8 index = step / total;
		u8 type = step % total;
		const struct tomoyo_profile *profile
			= tomoyo_profile_ptr[index];
		head->read_step = step;
		if (!profile)
			continue;
		if (!type) { /* Print profile' comment tag. */
			if (!tomoyo_io_printf(head, "%u-COMMENT=%s\n",
					      index, profile->comment ?
					      profile->comment->name : ""))
				break;
			continue;
		}
		type--;
		if (type < TOMOYO_MAX_CONTROL_INDEX) {
			const unsigned int value = profile->value[type];
			const char **modes = NULL;
			const char *keyword
				= tomoyo_control_array[type].keyword;
			switch (tomoyo_control_array[type].max_value) {
			case 3:
				modes = tomoyo_mode_4;
				break;
			case 1:
				modes = tomoyo_mode_2;
				break;
			}
			if (modes) {
				if (!tomoyo_io_printf(head, "%u-%s=%s\n", index,
						      keyword, modes[value]))
					break;
			} else {
				if (!tomoyo_io_printf(head, "%u-%s=%u\n", index,
						      keyword, value))
					break;
			}
		}
	}
	if (step == TOMOYO_MAX_PROFILES * total)
		head->read_eof = true;
	return 0;
}

/*
 * tomoyo_policy_manager_list is used for holding list of domainnames or
 * programs which are permitted to modify configuration via
 * /sys/kernel/security/tomoyo/ interface.
 *
 * An entry is added by
 *
 * # echo '<kernel> /sbin/mingetty /bin/login /bin/bash' > \
 *                                        /sys/kernel/security/tomoyo/manager
 *  (if you want to specify by a domainname)
 *
 *  or
 *
 * # echo '/usr/sbin/tomoyo-editpolicy' > /sys/kernel/security/tomoyo/manager
 *  (if you want to specify by a program's location)
 *
 * and is deleted by
 *
 * # echo 'delete <kernel> /sbin/mingetty /bin/login /bin/bash' > \
 *                                        /sys/kernel/security/tomoyo/manager
 *
 *  or
 *
 * # echo 'delete /usr/sbin/tomoyo-editpolicy' > \
 *                                        /sys/kernel/security/tomoyo/manager
 *
 * and all entries are retrieved by
 *
 * # cat /sys/kernel/security/tomoyo/manager
 */
LIST_HEAD(tomoyo_policy_manager_list);

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
	struct tomoyo_policy_manager_entry *ptr;
	struct tomoyo_policy_manager_entry e = { };
	int error = is_delete ? -ENOENT : -ENOMEM;

	if (tomoyo_is_domain_def(manager)) {
		if (!tomoyo_is_correct_domain(manager))
			return -EINVAL;
		e.is_domain = true;
	} else {
		if (!tomoyo_is_correct_path(manager, 1, -1, -1))
			return -EINVAL;
	}
	e.manager = tomoyo_get_name(manager);
	if (!e.manager)
		return -ENOMEM;
	if (mutex_lock_interruptible(&tomoyo_policy_lock))
		goto out;
	list_for_each_entry_rcu(ptr, &tomoyo_policy_manager_list, list) {
		if (ptr->manager != e.manager)
			continue;
		ptr->is_deleted = is_delete;
		error = 0;
		break;
	}
	if (!is_delete && error) {
		struct tomoyo_policy_manager_entry *entry =
			tomoyo_commit_ok(&e, sizeof(e));
		if (entry) {
			list_add_tail_rcu(&entry->list,
					  &tomoyo_policy_manager_list);
			error = 0;
		}
	}
	mutex_unlock(&tomoyo_policy_lock);
 out:
	tomoyo_put_name(e.manager);
	return error;
}

/**
 * tomoyo_write_manager_policy - Write manager policy.
 *
 * @head: Pointer to "struct tomoyo_io_buffer".
 *
 * Returns 0 on success, negative value otherwise.
 *
 * Caller holds tomoyo_read_lock().
 */
static int tomoyo_write_manager_policy(struct tomoyo_io_buffer *head)
{
	char *data = head->write_buf;
	bool is_delete = tomoyo_str_starts(&data, TOMOYO_KEYWORD_DELETE);

	if (!strcmp(data, "manage_by_non_root")) {
		tomoyo_manage_by_non_root = !is_delete;
		return 0;
	}
	return tomoyo_update_manager_entry(data, is_delete);
}

/**
 * tomoyo_read_manager_policy - Read manager policy.
 *
 * @head: Pointer to "struct tomoyo_io_buffer".
 *
 * Returns 0.
 *
 * Caller holds tomoyo_read_lock().
 */
static int tomoyo_read_manager_policy(struct tomoyo_io_buffer *head)
{
	struct list_head *pos;
	bool done = true;

	if (head->read_eof)
		return 0;
	list_for_each_cookie(pos, head->read_var2,
			     &tomoyo_policy_manager_list) {
		struct tomoyo_policy_manager_entry *ptr;
		ptr = list_entry(pos, struct tomoyo_policy_manager_entry,
				 list);
		if (ptr->is_deleted)
			continue;
		done = tomoyo_io_printf(head, "%s\n", ptr->manager->name);
		if (!done)
			break;
	}
	head->read_eof = done;
	return 0;
}

/**
 * tomoyo_is_policy_manager - Check whether the current process is a policy manager.
 *
 * Returns true if the current process is permitted to modify policy
 * via /sys/kernel/security/tomoyo/ interface.
 *
 * Caller holds tomoyo_read_lock().
 */
static bool tomoyo_is_policy_manager(void)
{
	struct tomoyo_policy_manager_entry *ptr;
	const char *exe;
	const struct task_struct *task = current;
	const struct tomoyo_path_info *domainname = tomoyo_domain()->domainname;
	bool found = false;

	if (!tomoyo_policy_loaded)
		return true;
	if (!tomoyo_manage_by_non_root && (task->cred->uid || task->cred->euid))
		return false;
	list_for_each_entry_rcu(ptr, &tomoyo_policy_manager_list, list) {
		if (!ptr->is_deleted && ptr->is_domain
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
	list_for_each_entry_rcu(ptr, &tomoyo_policy_manager_list, list) {
		if (!ptr->is_deleted && !ptr->is_domain
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
 * tomoyo_is_select_one - Parse select command.
 *
 * @head: Pointer to "struct tomoyo_io_buffer".
 * @data: String to parse.
 *
 * Returns true on success, false otherwise.
 *
 * Caller holds tomoyo_read_lock().
 */
static bool tomoyo_is_select_one(struct tomoyo_io_buffer *head,
				 const char *data)
{
	unsigned int pid;
	struct tomoyo_domain_info *domain = NULL;
	bool global_pid = false;

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
		if (tomoyo_is_domain_def(data + 7))
			domain = tomoyo_find_domain(data + 7);
	} else
		return false;
	head->write_var1 = domain;
	/* Accessing read_buf is safe because head->io_sem is held. */
	if (!head->read_buf)
		return true; /* Do nothing if open(O_WRONLY). */
	head->read_avail = 0;
	tomoyo_io_printf(head, "# select %s\n", data);
	head->read_single_domain = true;
	head->read_eof = !domain;
	if (domain) {
		struct tomoyo_domain_info *d;
		head->read_var1 = NULL;
		list_for_each_entry_rcu(d, &tomoyo_domain_list, list) {
			if (d == domain)
				break;
			head->read_var1 = &d->list;
		}
		head->read_var2 = NULL;
		head->read_bit = 0;
		head->read_step = 0;
		if (domain->is_deleted)
			tomoyo_io_printf(head, "# This is a deleted domain.\n");
	}
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
 * tomoyo_write_domain_policy2 - Write domain policy.
 *
 * @head: Pointer to "struct tomoyo_io_buffer".
 *
 * Returns 0 on success, negative value otherwise.
 *
 * Caller holds tomoyo_read_lock().
 */
static int tomoyo_write_domain_policy2(char *data,
				       struct tomoyo_domain_info *domain,
				       const bool is_delete)
{
	if (tomoyo_str_starts(&data, TOMOYO_KEYWORD_ALLOW_MOUNT))
                return tomoyo_write_mount_policy(data, domain, is_delete);
	return tomoyo_write_file_policy(data, domain, is_delete);
}

/**
 * tomoyo_write_domain_policy - Write domain policy.
 *
 * @head: Pointer to "struct tomoyo_io_buffer".
 *
 * Returns 0 on success, negative value otherwise.
 *
 * Caller holds tomoyo_read_lock().
 */
static int tomoyo_write_domain_policy(struct tomoyo_io_buffer *head)
{
	char *data = head->write_buf;
	struct tomoyo_domain_info *domain = head->write_var1;
	bool is_delete = false;
	bool is_select = false;
	unsigned int profile;

	if (tomoyo_str_starts(&data, TOMOYO_KEYWORD_DELETE))
		is_delete = true;
	else if (tomoyo_str_starts(&data, TOMOYO_KEYWORD_SELECT))
		is_select = true;
	if (is_select && tomoyo_is_select_one(head, data))
		return 0;
	/* Don't allow updating policies by non manager programs. */
	if (!tomoyo_is_policy_manager())
		return -EPERM;
	if (tomoyo_is_domain_def(data)) {
		domain = NULL;
		if (is_delete)
			tomoyo_delete_domain(data);
		else if (is_select)
			domain = tomoyo_find_domain(data);
		else
			domain = tomoyo_find_or_assign_new_domain(data, 0);
		head->write_var1 = domain;
		return 0;
	}
	if (!domain)
		return -EINVAL;

	if (sscanf(data, TOMOYO_KEYWORD_USE_PROFILE "%u", &profile) == 1
	    && profile < TOMOYO_MAX_PROFILES) {
		if (tomoyo_profile_ptr[profile] || !tomoyo_policy_loaded)
			domain->profile = (u8) profile;
		return 0;
	}
	if (!strcmp(data, TOMOYO_KEYWORD_IGNORE_GLOBAL_ALLOW_READ)) {
		domain->ignore_global_allow_read = !is_delete;
		return 0;
	}
	if (!strcmp(data, TOMOYO_KEYWORD_QUOTA_EXCEEDED)) {
		domain->quota_warned = !is_delete;
		return 0;
	}
	if (!strcmp(data, TOMOYO_KEYWORD_TRANSITION_FAILED)) {
		domain->transition_failed = !is_delete;
		return 0;
	}
	return tomoyo_write_domain_policy2(data, domain, is_delete);
}

/**
 * tomoyo_print_path_acl - Print a single path ACL entry.
 *
 * @head: Pointer to "struct tomoyo_io_buffer".
 * @ptr:  Pointer to "struct tomoyo_path_acl".
 *
 * Returns true on success, false otherwise.
 */
static bool tomoyo_print_path_acl(struct tomoyo_io_buffer *head,
				  struct tomoyo_path_acl *ptr)
{
	int pos;
	u8 bit;
	const u16 perm = ptr->perm;

	for (bit = head->read_bit; bit < TOMOYO_MAX_PATH_OPERATION; bit++) {
		if (!(perm & (1 << bit)))
			continue;
		/* Print "read/write" instead of "read" and "write". */
		if ((bit == TOMOYO_TYPE_READ || bit == TOMOYO_TYPE_WRITE)
		    && (perm & (1 << TOMOYO_TYPE_READ_WRITE)))
			continue;
		pos = head->read_avail;
		if (!tomoyo_io_printf(head, "allow_%s ",
				      tomoyo_path2keyword(bit)) ||
		    !tomoyo_print_name_union(head, &ptr->name) ||
		    !tomoyo_io_printf(head, "\n"))
			goto out;
	}
	head->read_bit = 0;
	return true;
 out:
	head->read_bit = bit;
	head->read_avail = pos;
	return false;
}

/**
 * tomoyo_print_path2_acl - Print a double path ACL entry.
 *
 * @head: Pointer to "struct tomoyo_io_buffer".
 * @ptr:  Pointer to "struct tomoyo_path2_acl".
 *
 * Returns true on success, false otherwise.
 */
static bool tomoyo_print_path2_acl(struct tomoyo_io_buffer *head,
				   struct tomoyo_path2_acl *ptr)
{
	int pos;
	const u8 perm = ptr->perm;
	u8 bit;

	for (bit = head->read_bit; bit < TOMOYO_MAX_PATH2_OPERATION; bit++) {
		if (!(perm & (1 << bit)))
			continue;
		pos = head->read_avail;
		if (!tomoyo_io_printf(head, "allow_%s ",
				      tomoyo_path22keyword(bit)) ||
		    !tomoyo_print_name_union(head, &ptr->name1) ||
		    !tomoyo_print_name_union(head, &ptr->name2) ||
		    !tomoyo_io_printf(head, "\n"))
			goto out;
	}
	head->read_bit = 0;
	return true;
 out:
	head->read_bit = bit;
	head->read_avail = pos;
	return false;
}

/**
 * tomoyo_print_path_number_acl - Print a path_number ACL entry.
 *
 * @head: Pointer to "struct tomoyo_io_buffer".
 * @ptr:  Pointer to "struct tomoyo_path_number_acl".
 *
 * Returns true on success, false otherwise.
 */
static bool tomoyo_print_path_number_acl(struct tomoyo_io_buffer *head,
					 struct tomoyo_path_number_acl *ptr)
{
	int pos;
	u8 bit;
	const u8 perm = ptr->perm;
	for (bit = head->read_bit; bit < TOMOYO_MAX_PATH_NUMBER_OPERATION;
	     bit++) {
		if (!(perm & (1 << bit)))
			continue;
		pos = head->read_avail;
		if (!tomoyo_io_printf(head, "allow_%s",
				      tomoyo_path_number2keyword(bit)) ||
		    !tomoyo_print_name_union(head, &ptr->name) ||
		    !tomoyo_print_number_union(head, &ptr->number) ||
		    !tomoyo_io_printf(head, "\n"))
			goto out;
	}
	head->read_bit = 0;
	return true;
 out:
	head->read_bit = bit;
	head->read_avail = pos;
	return false;
}

/**
 * tomoyo_print_path_number3_acl - Print a path_number3 ACL entry.
 *
 * @head: Pointer to "struct tomoyo_io_buffer".
 * @ptr:  Pointer to "struct tomoyo_path_number3_acl".
 *
 * Returns true on success, false otherwise.
 */
static bool tomoyo_print_path_number3_acl(struct tomoyo_io_buffer *head,
					  struct tomoyo_path_number3_acl *ptr)
{
	int pos;
	u8 bit;
	const u16 perm = ptr->perm;
	for (bit = head->read_bit; bit < TOMOYO_MAX_PATH_NUMBER3_OPERATION;
	     bit++) {
		if (!(perm & (1 << bit)))
			continue;
		pos = head->read_avail;
		if (!tomoyo_io_printf(head, "allow_%s",
				      tomoyo_path_number32keyword(bit)) ||
		    !tomoyo_print_name_union(head, &ptr->name) ||
		    !tomoyo_print_number_union(head, &ptr->mode) ||
		    !tomoyo_print_number_union(head, &ptr->major) ||
		    !tomoyo_print_number_union(head, &ptr->minor) ||
		    !tomoyo_io_printf(head, "\n"))
			goto out;
	}
	head->read_bit = 0;
	return true;
 out:
	head->read_bit = bit;
	head->read_avail = pos;
	return false;
}

/**
 * tomoyo_print_mount_acl - Print a mount ACL entry.
 *
 * @head: Pointer to "struct tomoyo_io_buffer".
 * @ptr:  Pointer to "struct tomoyo_mount_acl".
 *
 * Returns true on success, false otherwise.
 */
static bool tomoyo_print_mount_acl(struct tomoyo_io_buffer *head,
				   struct tomoyo_mount_acl *ptr)
{
	const int pos = head->read_avail;
	if (ptr->is_deleted)
		return true;
	if (!tomoyo_io_printf(head, TOMOYO_KEYWORD_ALLOW_MOUNT) ||
	    !tomoyo_print_name_union(head, &ptr->dev_name) ||
	    !tomoyo_print_name_union(head, &ptr->dir_name) ||
	    !tomoyo_print_name_union(head, &ptr->fs_type) ||
	    !tomoyo_print_number_union(head, &ptr->flags) ||
	    !tomoyo_io_printf(head, "\n")) {
		head->read_avail = pos;
		return false;
	}
	return true;
}

/**
 * tomoyo_print_entry - Print an ACL entry.
 *
 * @head: Pointer to "struct tomoyo_io_buffer".
 * @ptr:  Pointer to an ACL entry.
 *
 * Returns true on success, false otherwise.
 */
static bool tomoyo_print_entry(struct tomoyo_io_buffer *head,
			       struct tomoyo_acl_info *ptr)
{
	const u8 acl_type = ptr->type;

	if (acl_type == TOMOYO_TYPE_PATH_ACL) {
		struct tomoyo_path_acl *acl
			= container_of(ptr, struct tomoyo_path_acl, head);
		return tomoyo_print_path_acl(head, acl);
	}
	if (acl_type == TOMOYO_TYPE_PATH2_ACL) {
		struct tomoyo_path2_acl *acl
			= container_of(ptr, struct tomoyo_path2_acl, head);
		return tomoyo_print_path2_acl(head, acl);
	}
	if (acl_type == TOMOYO_TYPE_PATH_NUMBER_ACL) {
		struct tomoyo_path_number_acl *acl
			= container_of(ptr, struct tomoyo_path_number_acl,
				       head);
		return tomoyo_print_path_number_acl(head, acl);
	}
	if (acl_type == TOMOYO_TYPE_PATH_NUMBER3_ACL) {
		struct tomoyo_path_number3_acl *acl
			= container_of(ptr, struct tomoyo_path_number3_acl,
				       head);
		return tomoyo_print_path_number3_acl(head, acl);
	}
	if (acl_type == TOMOYO_TYPE_MOUNT_ACL) {
		struct tomoyo_mount_acl *acl
			= container_of(ptr, struct tomoyo_mount_acl, head);
		return tomoyo_print_mount_acl(head, acl);
	}
	BUG(); /* This must not happen. */
	return false;
}

/**
 * tomoyo_read_domain_policy - Read domain policy.
 *
 * @head: Pointer to "struct tomoyo_io_buffer".
 *
 * Returns 0.
 *
 * Caller holds tomoyo_read_lock().
 */
static int tomoyo_read_domain_policy(struct tomoyo_io_buffer *head)
{
	struct list_head *dpos;
	struct list_head *apos;
	bool done = true;

	if (head->read_eof)
		return 0;
	if (head->read_step == 0)
		head->read_step = 1;
	list_for_each_cookie(dpos, head->read_var1, &tomoyo_domain_list) {
		struct tomoyo_domain_info *domain;
		const char *quota_exceeded = "";
		const char *transition_failed = "";
		const char *ignore_global_allow_read = "";
		domain = list_entry(dpos, struct tomoyo_domain_info, list);
		if (head->read_step != 1)
			goto acl_loop;
		if (domain->is_deleted && !head->read_single_domain)
			continue;
		/* Print domainname and flags. */
		if (domain->quota_warned)
			quota_exceeded = "quota_exceeded\n";
		if (domain->transition_failed)
			transition_failed = "transition_failed\n";
		if (domain->ignore_global_allow_read)
			ignore_global_allow_read
				= TOMOYO_KEYWORD_IGNORE_GLOBAL_ALLOW_READ "\n";
		done = tomoyo_io_printf(head, "%s\n" TOMOYO_KEYWORD_USE_PROFILE
					"%u\n%s%s%s\n",
					domain->domainname->name,
					domain->profile, quota_exceeded,
					transition_failed,
					ignore_global_allow_read);
		if (!done)
			break;
		head->read_step = 2;
acl_loop:
		if (head->read_step == 3)
			goto tail_mark;
		/* Print ACL entries in the domain. */
		list_for_each_cookie(apos, head->read_var2,
				     &domain->acl_info_list) {
			struct tomoyo_acl_info *ptr
				= list_entry(apos, struct tomoyo_acl_info,
					     list);
			done = tomoyo_print_entry(head, ptr);
			if (!done)
				break;
		}
		if (!done)
			break;
		head->read_step = 3;
tail_mark:
		done = tomoyo_io_printf(head, "\n");
		if (!done)
			break;
		head->read_step = 1;
		if (head->read_single_domain)
			break;
	}
	head->read_eof = done;
	return 0;
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
static int tomoyo_read_domain_profile(struct tomoyo_io_buffer *head)
{
	struct list_head *pos;
	bool done = true;

	if (head->read_eof)
		return 0;
	list_for_each_cookie(pos, head->read_var1, &tomoyo_domain_list) {
		struct tomoyo_domain_info *domain;
		domain = list_entry(pos, struct tomoyo_domain_info, list);
		if (domain->is_deleted)
			continue;
		done = tomoyo_io_printf(head, "%u %s\n", domain->profile,
					domain->domainname->name);
		if (!done)
			break;
	}
	head->read_eof = done;
	return 0;
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
	unsigned long pid;
	/* No error check. */
	strict_strtoul(head->write_buf, 10, &pid);
	head->read_step = (int) pid;
	head->read_eof = false;
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
static int tomoyo_read_pid(struct tomoyo_io_buffer *head)
{
	if (head->read_avail == 0 && !head->read_eof) {
		const int pid = head->read_step;
		struct task_struct *p;
		struct tomoyo_domain_info *domain = NULL;
		rcu_read_lock();
		read_lock(&tasklist_lock);
		p = find_task_by_vpid(pid);
		if (p)
			domain = tomoyo_real_domain(p);
		read_unlock(&tasklist_lock);
		rcu_read_unlock();
		if (domain)
			tomoyo_io_printf(head, "%d %u %s", pid, domain->profile,
					 domain->domainname->name);
		head->read_eof = true;
	}
	return 0;
}

/**
 * tomoyo_write_exception_policy - Write exception policy.
 *
 * @head: Pointer to "struct tomoyo_io_buffer".
 *
 * Returns 0 on success, negative value otherwise.
 *
 * Caller holds tomoyo_read_lock().
 */
static int tomoyo_write_exception_policy(struct tomoyo_io_buffer *head)
{
	char *data = head->write_buf;
	bool is_delete = tomoyo_str_starts(&data, TOMOYO_KEYWORD_DELETE);

	if (tomoyo_str_starts(&data, TOMOYO_KEYWORD_KEEP_DOMAIN))
		return tomoyo_write_domain_keeper_policy(data, false,
							 is_delete);
	if (tomoyo_str_starts(&data, TOMOYO_KEYWORD_NO_KEEP_DOMAIN))
		return tomoyo_write_domain_keeper_policy(data, true, is_delete);
	if (tomoyo_str_starts(&data, TOMOYO_KEYWORD_INITIALIZE_DOMAIN))
		return tomoyo_write_domain_initializer_policy(data, false,
							      is_delete);
	if (tomoyo_str_starts(&data, TOMOYO_KEYWORD_NO_INITIALIZE_DOMAIN))
		return tomoyo_write_domain_initializer_policy(data, true,
							      is_delete);
	if (tomoyo_str_starts(&data, TOMOYO_KEYWORD_ALIAS))
		return tomoyo_write_alias_policy(data, is_delete);
	if (tomoyo_str_starts(&data, TOMOYO_KEYWORD_ALLOW_READ))
		return tomoyo_write_globally_readable_policy(data, is_delete);
	if (tomoyo_str_starts(&data, TOMOYO_KEYWORD_FILE_PATTERN))
		return tomoyo_write_pattern_policy(data, is_delete);
	if (tomoyo_str_starts(&data, TOMOYO_KEYWORD_DENY_REWRITE))
		return tomoyo_write_no_rewrite_policy(data, is_delete);
	if (tomoyo_str_starts(&data, TOMOYO_KEYWORD_PATH_GROUP))
		return tomoyo_write_path_group_policy(data, is_delete);
	if (tomoyo_str_starts(&data, TOMOYO_KEYWORD_NUMBER_GROUP))
		return tomoyo_write_number_group_policy(data, is_delete);
	return -EINVAL;
}

/**
 * tomoyo_read_exception_policy - Read exception policy.
 *
 * @head: Pointer to "struct tomoyo_io_buffer".
 *
 * Returns 0 on success, -EINVAL otherwise.
 *
 * Caller holds tomoyo_read_lock().
 */
static int tomoyo_read_exception_policy(struct tomoyo_io_buffer *head)
{
	if (!head->read_eof) {
		switch (head->read_step) {
		case 0:
			head->read_var2 = NULL;
			head->read_step = 1;
		case 1:
			if (!tomoyo_read_domain_keeper_policy(head))
				break;
			head->read_var2 = NULL;
			head->read_step = 2;
		case 2:
			if (!tomoyo_read_globally_readable_policy(head))
				break;
			head->read_var2 = NULL;
			head->read_step = 3;
		case 3:
			head->read_var2 = NULL;
			head->read_step = 4;
		case 4:
			if (!tomoyo_read_domain_initializer_policy(head))
				break;
			head->read_var2 = NULL;
			head->read_step = 5;
		case 5:
			if (!tomoyo_read_alias_policy(head))
				break;
			head->read_var2 = NULL;
			head->read_step = 6;
		case 6:
			head->read_var2 = NULL;
			head->read_step = 7;
		case 7:
			if (!tomoyo_read_file_pattern(head))
				break;
			head->read_var2 = NULL;
			head->read_step = 8;
		case 8:
			if (!tomoyo_read_no_rewrite_policy(head))
				break;
			head->read_var2 = NULL;
			head->read_step = 9;
		case 9:
			if (!tomoyo_read_path_group_policy(head))
				break;
			head->read_var1 = NULL;
			head->read_var2 = NULL;
			head->read_step = 10;
		case 10:
			if (!tomoyo_read_number_group_policy(head))
				break;
			head->read_var1 = NULL;
			head->read_var2 = NULL;
			head->read_step = 11;
		case 11:
			head->read_eof = true;
			break;
		default:
			return -EINVAL;
		}
	}
	return 0;
}

/**
 * tomoyo_print_header - Get header line of audit log.
 *
 * @r: Pointer to "struct tomoyo_request_info".
 *
 * Returns string representation.
 *
 * This function uses kmalloc(), so caller must kfree() if this function
 * didn't return NULL.
 */
static char *tomoyo_print_header(struct tomoyo_request_info *r)
{
	static const char *tomoyo_mode_4[4] = {
		"disabled", "learning", "permissive", "enforcing"
	};
	struct timeval tv;
	const pid_t gpid = task_pid_nr(current);
	static const int tomoyo_buffer_len = 4096;
	char *buffer = kmalloc(tomoyo_buffer_len, GFP_NOFS);
	if (!buffer)
		return NULL;
	do_gettimeofday(&tv);
	snprintf(buffer, tomoyo_buffer_len - 1,
		 "#timestamp=%lu profile=%u mode=%s (global-pid=%u)"
		 " task={ pid=%u ppid=%u uid=%u gid=%u euid=%u"
		 " egid=%u suid=%u sgid=%u fsuid=%u fsgid=%u }",
		 tv.tv_sec, r->profile, tomoyo_mode_4[r->mode], gpid,
		 (pid_t) sys_getpid(), (pid_t) sys_getppid(),
		 current_uid(), current_gid(), current_euid(),
		 current_egid(), current_suid(), current_sgid(),
		 current_fsuid(), current_fsgid());
	return buffer;
}

/**
 * tomoyo_init_audit_log - Allocate buffer for audit logs.
 *
 * @len: Required size.
 * @r:   Pointer to "struct tomoyo_request_info".
 *
 * Returns pointer to allocated memory.
 *
 * The @len is updated to add the header lines' size on success.
 *
 * This function uses kzalloc(), so caller must kfree() if this function
 * didn't return NULL.
 */
static char *tomoyo_init_audit_log(int *len, struct tomoyo_request_info *r)
{
	char *buf = NULL;
	const char *header;
	const char *domainname;
	if (!r->domain)
		r->domain = tomoyo_domain();
	domainname = r->domain->domainname->name;
	header = tomoyo_print_header(r);
	if (!header)
		return NULL;
	*len += strlen(domainname) + strlen(header) + 10;
	buf = kzalloc(*len, GFP_NOFS);
	if (buf)
		snprintf(buf, (*len) - 1, "%s\n%s\n", header, domainname);
	kfree(header);
	return buf;
}

/* Wait queue for tomoyo_query_list. */
static DECLARE_WAIT_QUEUE_HEAD(tomoyo_query_wait);

/* Lock for manipulating tomoyo_query_list. */
static DEFINE_SPINLOCK(tomoyo_query_list_lock);

/* Structure for query. */
struct tomoyo_query_entry {
	struct list_head list;
	char *query;
	int query_len;
	unsigned int serial;
	int timer;
	int answer;
};

/* The list for "struct tomoyo_query_entry". */
static LIST_HEAD(tomoyo_query_list);

/*
 * Number of "struct file" referring /sys/kernel/security/tomoyo/query
 * interface.
 */
static atomic_t tomoyo_query_observers = ATOMIC_INIT(0);

/**
 * tomoyo_supervisor - Ask for the supervisor's decision.
 *
 * @r:       Pointer to "struct tomoyo_request_info".
 * @fmt:     The printf()'s format string, followed by parameters.
 *
 * Returns 0 if the supervisor decided to permit the access request which
 * violated the policy in enforcing mode, TOMOYO_RETRY_REQUEST if the
 * supervisor decided to retry the access request which violated the policy in
 * enforcing mode, 0 if it is not in enforcing mode, -EPERM otherwise.
 */
int tomoyo_supervisor(struct tomoyo_request_info *r, const char *fmt, ...)
{
	va_list args;
	int error = -EPERM;
	int pos;
	int len;
	static unsigned int tomoyo_serial;
	struct tomoyo_query_entry *tomoyo_query_entry = NULL;
	bool quota_exceeded = false;
	char *header;
	switch (r->mode) {
		char *buffer;
	case TOMOYO_CONFIG_LEARNING:
		if (!tomoyo_domain_quota_is_ok(r))
			return 0;
		va_start(args, fmt);
		len = vsnprintf((char *) &pos, sizeof(pos) - 1, fmt, args) + 4;
		va_end(args);
		buffer = kmalloc(len, GFP_NOFS);
		if (!buffer)
			return 0;
		va_start(args, fmt);
		vsnprintf(buffer, len - 1, fmt, args);
		va_end(args);
		tomoyo_normalize_line(buffer);
		tomoyo_write_domain_policy2(buffer, r->domain, false);
		kfree(buffer);
		/* fall through */
	case TOMOYO_CONFIG_PERMISSIVE:
		return 0;
	}
	if (!r->domain)
		r->domain = tomoyo_domain();
	if (!atomic_read(&tomoyo_query_observers))
		return -EPERM;
	va_start(args, fmt);
	len = vsnprintf((char *) &pos, sizeof(pos) - 1, fmt, args) + 32;
	va_end(args);
	header = tomoyo_init_audit_log(&len, r);
	if (!header)
		goto out;
	tomoyo_query_entry = kzalloc(sizeof(*tomoyo_query_entry), GFP_NOFS);
	if (!tomoyo_query_entry)
		goto out;
	tomoyo_query_entry->query = kzalloc(len, GFP_NOFS);
	if (!tomoyo_query_entry->query)
		goto out;
	len = ksize(tomoyo_query_entry->query);
	INIT_LIST_HEAD(&tomoyo_query_entry->list);
	spin_lock(&tomoyo_query_list_lock);
	if (tomoyo_quota_for_query && tomoyo_query_memory_size + len +
	    sizeof(*tomoyo_query_entry) >= tomoyo_quota_for_query) {
		quota_exceeded = true;
	} else {
		tomoyo_query_memory_size += len + sizeof(*tomoyo_query_entry);
		tomoyo_query_entry->serial = tomoyo_serial++;
	}
	spin_unlock(&tomoyo_query_list_lock);
	if (quota_exceeded)
		goto out;
	pos = snprintf(tomoyo_query_entry->query, len - 1, "Q%u-%hu\n%s",
		       tomoyo_query_entry->serial, r->retry, header);
	kfree(header);
	header = NULL;
	va_start(args, fmt);
	vsnprintf(tomoyo_query_entry->query + pos, len - 1 - pos, fmt, args);
	tomoyo_query_entry->query_len = strlen(tomoyo_query_entry->query) + 1;
	va_end(args);
	spin_lock(&tomoyo_query_list_lock);
	list_add_tail(&tomoyo_query_entry->list, &tomoyo_query_list);
	spin_unlock(&tomoyo_query_list_lock);
	/* Give 10 seconds for supervisor's opinion. */
	for (tomoyo_query_entry->timer = 0;
	     atomic_read(&tomoyo_query_observers) && tomoyo_query_entry->timer < 100;
	     tomoyo_query_entry->timer++) {
		wake_up(&tomoyo_query_wait);
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(HZ / 10);
		if (tomoyo_query_entry->answer)
			break;
	}
	spin_lock(&tomoyo_query_list_lock);
	list_del(&tomoyo_query_entry->list);
	tomoyo_query_memory_size -= len + sizeof(*tomoyo_query_entry);
	spin_unlock(&tomoyo_query_list_lock);
	switch (tomoyo_query_entry->answer) {
	case 3: /* Asked to retry by administrator. */
		error = TOMOYO_RETRY_REQUEST;
		r->retry++;
		break;
	case 1:
		/* Granted by administrator. */
		error = 0;
		break;
	case 0:
		/* Timed out. */
		break;
	default:
		/* Rejected by administrator. */
		break;
	}
 out:
	if (tomoyo_query_entry)
		kfree(tomoyo_query_entry->query);
	kfree(tomoyo_query_entry);
	kfree(header);
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
			struct tomoyo_query_entry *ptr
				= list_entry(tmp, struct tomoyo_query_entry,
					     list);
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
 *
 * Returns 0.
 */
static int tomoyo_read_query(struct tomoyo_io_buffer *head)
{
	struct list_head *tmp;
	int pos = 0;
	int len = 0;
	char *buf;
	if (head->read_avail)
		return 0;
	if (head->read_buf) {
		kfree(head->read_buf);
		head->read_buf = NULL;
		head->readbuf_size = 0;
	}
	spin_lock(&tomoyo_query_list_lock);
	list_for_each(tmp, &tomoyo_query_list) {
		struct tomoyo_query_entry *ptr
			= list_entry(tmp, struct tomoyo_query_entry, list);
		if (ptr->answer)
			continue;
		if (pos++ != head->read_step)
			continue;
		len = ptr->query_len;
		break;
	}
	spin_unlock(&tomoyo_query_list_lock);
	if (!len) {
		head->read_step = 0;
		return 0;
	}
	buf = kzalloc(len, GFP_NOFS);
	if (!buf)
		return 0;
	pos = 0;
	spin_lock(&tomoyo_query_list_lock);
	list_for_each(tmp, &tomoyo_query_list) {
		struct tomoyo_query_entry *ptr
			= list_entry(tmp, struct tomoyo_query_entry, list);
		if (ptr->answer)
			continue;
		if (pos++ != head->read_step)
			continue;
		/*
		 * Some query can be skipped because tomoyo_query_list
		 * can change, but I don't care.
		 */
		if (len == ptr->query_len)
			memmove(buf, ptr->query, len);
		break;
	}
	spin_unlock(&tomoyo_query_list_lock);
	if (buf[0]) {
		head->read_avail = len;
		head->readbuf_size = head->read_avail;
		head->read_buf = buf;
		head->read_step++;
	} else {
		kfree(buf);
	}
	return 0;
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
		struct tomoyo_query_entry *ptr
			= list_entry(tmp, struct tomoyo_query_entry, list);
		ptr->timer = 0;
	}
	spin_unlock(&tomoyo_query_list_lock);
	if (sscanf(data, "A%u=%u", &serial, &answer) != 2)
		return -EINVAL;
	spin_lock(&tomoyo_query_list_lock);
	list_for_each(tmp, &tomoyo_query_list) {
		struct tomoyo_query_entry *ptr
			= list_entry(tmp, struct tomoyo_query_entry, list);
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
static int tomoyo_read_version(struct tomoyo_io_buffer *head)
{
	if (!head->read_eof) {
		tomoyo_io_printf(head, "2.2.0");
		head->read_eof = true;
	}
	return 0;
}

/**
 * tomoyo_read_self_domain - Get the current process's domainname.
 *
 * @head: Pointer to "struct tomoyo_io_buffer".
 *
 * Returns the current process's domainname.
 */
static int tomoyo_read_self_domain(struct tomoyo_io_buffer *head)
{
	if (!head->read_eof) {
		/*
		 * tomoyo_domain()->domainname != NULL
		 * because every process belongs to a domain and
		 * the domain's name cannot be NULL.
		 */
		tomoyo_io_printf(head, "%s", tomoyo_domain()->domainname->name);
		head->read_eof = true;
	}
	return 0;
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
		head->write = tomoyo_write_domain_policy;
		head->read = tomoyo_read_domain_policy;
		break;
	case TOMOYO_EXCEPTIONPOLICY:
		/* /sys/kernel/security/tomoyo/exception_policy */
		head->write = tomoyo_write_exception_policy;
		head->read = tomoyo_read_exception_policy;
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
		head->write = tomoyo_write_manager_policy;
		head->read = tomoyo_read_manager_policy;
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
	if (type != TOMOYO_QUERY)
		head->reader_idx = tomoyo_read_lock();
	file->private_data = head;
	/*
	 * Call the handler now if the file is
	 * /sys/kernel/security/tomoyo/self_domain
	 * so that the user can use
	 * cat < /sys/kernel/security/tomoyo/self_domain"
	 * to know the current process's domainname.
	 */
	if (type == TOMOYO_SELFDOMAIN)
		tomoyo_read_control(file, NULL, 0);
	/*
	 * If the file is /sys/kernel/security/tomoyo/query , increment the
	 * observer counter.
	 * The obserber counter is used by tomoyo_supervisor() to see if
	 * there is some process monitoring /sys/kernel/security/tomoyo/query.
	 */
	else if (type == TOMOYO_QUERY)
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
 * /sys/kernel/security/tomoyo/query is handled by /usr/sbin/tomoyo-queryd .
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
 * @file:       Pointer to "struct file".
 * @buffer:     Poiner to buffer to write to.
 * @buffer_len: Size of @buffer.
 *
 * Returns bytes read on success, negative value otherwise.
 *
 * Caller holds tomoyo_read_lock().
 */
int tomoyo_read_control(struct file *file, char __user *buffer,
			const int buffer_len)
{
	int len = 0;
	struct tomoyo_io_buffer *head = file->private_data;
	char *cp;

	if (!head->read)
		return -ENOSYS;
	if (mutex_lock_interruptible(&head->io_sem))
		return -EINTR;
	/* Call the policy handler. */
	len = head->read(head);
	if (len < 0)
		goto out;
	/* Write to buffer. */
	len = head->read_avail;
	if (len > buffer_len)
		len = buffer_len;
	if (!len)
		goto out;
	/* head->read_buf changes by some functions. */
	cp = head->read_buf;
	if (copy_to_user(buffer, cp, len)) {
		len = -EFAULT;
		goto out;
	}
	head->read_avail -= len;
	memmove(cp, cp + len, head->read_avail);
 out:
	mutex_unlock(&head->io_sem);
	return len;
}

/**
 * tomoyo_write_control - write() for /sys/kernel/security/tomoyo/ interface.
 *
 * @file:       Pointer to "struct file".
 * @buffer:     Pointer to buffer to read from.
 * @buffer_len: Size of @buffer.
 *
 * Returns @buffer_len on success, negative value otherwise.
 *
 * Caller holds tomoyo_read_lock().
 */
int tomoyo_write_control(struct file *file, const char __user *buffer,
			 const int buffer_len)
{
	struct tomoyo_io_buffer *head = file->private_data;
	int error = buffer_len;
	int avail_len = buffer_len;
	char *cp0 = head->write_buf;

	if (!head->write)
		return -ENOSYS;
	if (!access_ok(VERIFY_READ, buffer, buffer_len))
		return -EFAULT;
	/* Don't allow updating policies by non manager programs. */
	if (head->write != tomoyo_write_pid &&
	    head->write != tomoyo_write_domain_policy &&
	    !tomoyo_is_policy_manager())
		return -EPERM;
	if (mutex_lock_interruptible(&head->io_sem))
		return -EINTR;
	/* Read a line and dispatch it to the policy handler. */
	while (avail_len > 0) {
		char c;
		if (head->write_avail >= head->writebuf_size - 1) {
			error = -ENOMEM;
			break;
		} else if (get_user(c, buffer)) {
			error = -EFAULT;
			break;
		}
		buffer++;
		avail_len--;
		cp0[head->write_avail++] = c;
		if (c != '\n')
			continue;
		cp0[head->write_avail - 1] = '\0';
		head->write_avail = 0;
		tomoyo_normalize_line(cp0);
		head->write(head);
	}
	mutex_unlock(&head->io_sem);
	return error;
}

/**
 * tomoyo_close_control - close() for /sys/kernel/security/tomoyo/ interface.
 *
 * @file: Pointer to "struct file".
 *
 * Releases memory and returns 0.
 *
 * Caller looses tomoyo_read_lock().
 */
int tomoyo_close_control(struct file *file)
{
	struct tomoyo_io_buffer *head = file->private_data;
	const bool is_write = !!head->write_buf;

	/*
	 * If the file is /sys/kernel/security/tomoyo/query , decrement the
	 * observer counter.
	 */
	if (head->type == TOMOYO_QUERY)
		atomic_dec(&tomoyo_query_observers);
	else
		tomoyo_read_unlock(head->reader_idx);
	/* Release memory used for policy I/O. */
	kfree(head->read_buf);
	head->read_buf = NULL;
	kfree(head->write_buf);
	head->write_buf = NULL;
	kfree(head);
	head = NULL;
	file->private_data = NULL;
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
		panic("Profile %u (used by '%s') not defined.\n",
		      profile, domain->domainname->name);
	}
	tomoyo_read_unlock(idx);
	printk(KERN_INFO "TOMOYO: 2.2.0   2009/04/01\n");
	printk(KERN_INFO "Mandatory Access Control activated.\n");
}
