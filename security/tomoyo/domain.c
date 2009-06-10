/*
 * security/tomoyo/domain.c
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
#include <linux/binfmts.h>

/* Variables definitions.*/

/* The initial domain. */
struct tomoyo_domain_info tomoyo_kernel_domain;

/* The list for "struct tomoyo_domain_info". */
LIST_HEAD(tomoyo_domain_list);
DECLARE_RWSEM(tomoyo_domain_list_lock);

/* Structure for "initialize_domain" and "no_initialize_domain" keyword. */
struct tomoyo_domain_initializer_entry {
	struct list_head list;
	const struct tomoyo_path_info *domainname;    /* This may be NULL */
	const struct tomoyo_path_info *program;
	bool is_deleted;
	bool is_not;       /* True if this entry is "no_initialize_domain".  */
	/* True if the domainname is tomoyo_get_last_name(). */
	bool is_last_name;
};

/* Structure for "keep_domain" and "no_keep_domain" keyword. */
struct tomoyo_domain_keeper_entry {
	struct list_head list;
	const struct tomoyo_path_info *domainname;
	const struct tomoyo_path_info *program;       /* This may be NULL */
	bool is_deleted;
	bool is_not;       /* True if this entry is "no_keep_domain".        */
	/* True if the domainname is tomoyo_get_last_name(). */
	bool is_last_name;
};

/* Structure for "alias" keyword. */
struct tomoyo_alias_entry {
	struct list_head list;
	const struct tomoyo_path_info *original_name;
	const struct tomoyo_path_info *aliased_name;
	bool is_deleted;
};

/**
 * tomoyo_set_domain_flag - Set or clear domain's attribute flags.
 *
 * @domain:    Pointer to "struct tomoyo_domain_info".
 * @is_delete: True if it is a delete request.
 * @flags:     Flags to set or clear.
 *
 * Returns nothing.
 */
void tomoyo_set_domain_flag(struct tomoyo_domain_info *domain,
			    const bool is_delete, const u8 flags)
{
	/* We need to serialize because this is bitfield operation. */
	static DEFINE_SPINLOCK(lock);
	/***** CRITICAL SECTION START *****/
	spin_lock(&lock);
	if (!is_delete)
		domain->flags |= flags;
	else
		domain->flags &= ~flags;
	spin_unlock(&lock);
	/***** CRITICAL SECTION END *****/
}

/**
 * tomoyo_get_last_name - Get last component of a domainname.
 *
 * @domain: Pointer to "struct tomoyo_domain_info".
 *
 * Returns the last component of the domainname.
 */
const char *tomoyo_get_last_name(const struct tomoyo_domain_info *domain)
{
	const char *cp0 = domain->domainname->name;
	const char *cp1 = strrchr(cp0, ' ');

	if (cp1)
		return cp1 + 1;
	return cp0;
}

/* The list for "struct tomoyo_domain_initializer_entry". */
static LIST_HEAD(tomoyo_domain_initializer_list);
static DECLARE_RWSEM(tomoyo_domain_initializer_list_lock);

/**
 * tomoyo_update_domain_initializer_entry - Update "struct tomoyo_domain_initializer_entry" list.
 *
 * @domainname: The name of domain. May be NULL.
 * @program:    The name of program.
 * @is_not:     True if it is "no_initialize_domain" entry.
 * @is_delete:  True if it is a delete request.
 *
 * Returns 0 on success, negative value otherwise.
 */
static int tomoyo_update_domain_initializer_entry(const char *domainname,
						  const char *program,
						  const bool is_not,
						  const bool is_delete)
{
	struct tomoyo_domain_initializer_entry *new_entry;
	struct tomoyo_domain_initializer_entry *ptr;
	const struct tomoyo_path_info *saved_program;
	const struct tomoyo_path_info *saved_domainname = NULL;
	int error = -ENOMEM;
	bool is_last_name = false;

	if (!tomoyo_is_correct_path(program, 1, -1, -1, __func__))
		return -EINVAL; /* No patterns allowed. */
	if (domainname) {
		if (!tomoyo_is_domain_def(domainname) &&
		    tomoyo_is_correct_path(domainname, 1, -1, -1, __func__))
			is_last_name = true;
		else if (!tomoyo_is_correct_domain(domainname, __func__))
			return -EINVAL;
		saved_domainname = tomoyo_save_name(domainname);
		if (!saved_domainname)
			return -ENOMEM;
	}
	saved_program = tomoyo_save_name(program);
	if (!saved_program)
		return -ENOMEM;
	/***** EXCLUSIVE SECTION START *****/
	down_write(&tomoyo_domain_initializer_list_lock);
	list_for_each_entry(ptr, &tomoyo_domain_initializer_list, list) {
		if (ptr->is_not != is_not ||
		    ptr->domainname != saved_domainname ||
		    ptr->program != saved_program)
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
	new_entry->domainname = saved_domainname;
	new_entry->program = saved_program;
	new_entry->is_not = is_not;
	new_entry->is_last_name = is_last_name;
	list_add_tail(&new_entry->list, &tomoyo_domain_initializer_list);
	error = 0;
 out:
	up_write(&tomoyo_domain_initializer_list_lock);
	/***** EXCLUSIVE SECTION END *****/
	return error;
}

/**
 * tomoyo_read_domain_initializer_policy - Read "struct tomoyo_domain_initializer_entry" list.
 *
 * @head: Pointer to "struct tomoyo_io_buffer".
 *
 * Returns true on success, false otherwise.
 */
bool tomoyo_read_domain_initializer_policy(struct tomoyo_io_buffer *head)
{
	struct list_head *pos;
	bool done = true;

	down_read(&tomoyo_domain_initializer_list_lock);
	list_for_each_cookie(pos, head->read_var2,
			     &tomoyo_domain_initializer_list) {
		const char *no;
		const char *from = "";
		const char *domain = "";
		struct tomoyo_domain_initializer_entry *ptr;
		ptr = list_entry(pos, struct tomoyo_domain_initializer_entry,
				  list);
		if (ptr->is_deleted)
			continue;
		no = ptr->is_not ? "no_" : "";
		if (ptr->domainname) {
			from = " from ";
			domain = ptr->domainname->name;
		}
		if (!tomoyo_io_printf(head,
				      "%s" TOMOYO_KEYWORD_INITIALIZE_DOMAIN
				      "%s%s%s\n", no, ptr->program->name, from,
				      domain)) {
			done = false;
			break;
		}
	}
	up_read(&tomoyo_domain_initializer_list_lock);
	return done;
}

/**
 * tomoyo_write_domain_initializer_policy - Write "struct tomoyo_domain_initializer_entry" list.
 *
 * @data:      String to parse.
 * @is_not:    True if it is "no_initialize_domain" entry.
 * @is_delete: True if it is a delete request.
 *
 * Returns 0 on success, negative value otherwise.
 */
int tomoyo_write_domain_initializer_policy(char *data, const bool is_not,
					   const bool is_delete)
{
	char *cp = strstr(data, " from ");

	if (cp) {
		*cp = '\0';
		return tomoyo_update_domain_initializer_entry(cp + 6, data,
							      is_not,
							      is_delete);
	}
	return tomoyo_update_domain_initializer_entry(NULL, data, is_not,
						      is_delete);
}

/**
 * tomoyo_is_domain_initializer - Check whether the given program causes domainname reinitialization.
 *
 * @domainname: The name of domain.
 * @program:    The name of program.
 * @last_name:  The last component of @domainname.
 *
 * Returns true if executing @program reinitializes domain transition,
 * false otherwise.
 */
static bool tomoyo_is_domain_initializer(const struct tomoyo_path_info *
					 domainname,
					 const struct tomoyo_path_info *program,
					 const struct tomoyo_path_info *
					 last_name)
{
	struct tomoyo_domain_initializer_entry *ptr;
	bool flag = false;

	down_read(&tomoyo_domain_initializer_list_lock);
	list_for_each_entry(ptr,  &tomoyo_domain_initializer_list, list) {
		if (ptr->is_deleted)
			continue;
		if (ptr->domainname) {
			if (!ptr->is_last_name) {
				if (ptr->domainname != domainname)
					continue;
			} else {
				if (tomoyo_pathcmp(ptr->domainname, last_name))
					continue;
			}
		}
		if (tomoyo_pathcmp(ptr->program, program))
			continue;
		if (ptr->is_not) {
			flag = false;
			break;
		}
		flag = true;
	}
	up_read(&tomoyo_domain_initializer_list_lock);
	return flag;
}

/* The list for "struct tomoyo_domain_keeper_entry". */
static LIST_HEAD(tomoyo_domain_keeper_list);
static DECLARE_RWSEM(tomoyo_domain_keeper_list_lock);

/**
 * tomoyo_update_domain_keeper_entry - Update "struct tomoyo_domain_keeper_entry" list.
 *
 * @domainname: The name of domain.
 * @program:    The name of program. May be NULL.
 * @is_not:     True if it is "no_keep_domain" entry.
 * @is_delete:  True if it is a delete request.
 *
 * Returns 0 on success, negative value otherwise.
 */
static int tomoyo_update_domain_keeper_entry(const char *domainname,
					     const char *program,
					     const bool is_not,
					     const bool is_delete)
{
	struct tomoyo_domain_keeper_entry *new_entry;
	struct tomoyo_domain_keeper_entry *ptr;
	const struct tomoyo_path_info *saved_domainname;
	const struct tomoyo_path_info *saved_program = NULL;
	static DEFINE_MUTEX(lock);
	int error = -ENOMEM;
	bool is_last_name = false;

	if (!tomoyo_is_domain_def(domainname) &&
	    tomoyo_is_correct_path(domainname, 1, -1, -1, __func__))
		is_last_name = true;
	else if (!tomoyo_is_correct_domain(domainname, __func__))
		return -EINVAL;
	if (program) {
		if (!tomoyo_is_correct_path(program, 1, -1, -1, __func__))
			return -EINVAL;
		saved_program = tomoyo_save_name(program);
		if (!saved_program)
			return -ENOMEM;
	}
	saved_domainname = tomoyo_save_name(domainname);
	if (!saved_domainname)
		return -ENOMEM;
	/***** EXCLUSIVE SECTION START *****/
	down_write(&tomoyo_domain_keeper_list_lock);
	list_for_each_entry(ptr, &tomoyo_domain_keeper_list, list) {
		if (ptr->is_not != is_not ||
		    ptr->domainname != saved_domainname ||
		    ptr->program != saved_program)
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
	new_entry->domainname = saved_domainname;
	new_entry->program = saved_program;
	new_entry->is_not = is_not;
	new_entry->is_last_name = is_last_name;
	list_add_tail(&new_entry->list, &tomoyo_domain_keeper_list);
	error = 0;
 out:
	up_write(&tomoyo_domain_keeper_list_lock);
	/***** EXCLUSIVE SECTION END *****/
	return error;
}

/**
 * tomoyo_write_domain_keeper_policy - Write "struct tomoyo_domain_keeper_entry" list.
 *
 * @data:      String to parse.
 * @is_not:    True if it is "no_keep_domain" entry.
 * @is_delete: True if it is a delete request.
 *
 */
int tomoyo_write_domain_keeper_policy(char *data, const bool is_not,
				      const bool is_delete)
{
	char *cp = strstr(data, " from ");

	if (cp) {
		*cp = '\0';
		return tomoyo_update_domain_keeper_entry(cp + 6, data, is_not,
							 is_delete);
	}
	return tomoyo_update_domain_keeper_entry(data, NULL, is_not, is_delete);
}

/**
 * tomoyo_read_domain_keeper_policy - Read "struct tomoyo_domain_keeper_entry" list.
 *
 * @head: Pointer to "struct tomoyo_io_buffer".
 *
 * Returns true on success, false otherwise.
 */
bool tomoyo_read_domain_keeper_policy(struct tomoyo_io_buffer *head)
{
	struct list_head *pos;
	bool done = true;

	down_read(&tomoyo_domain_keeper_list_lock);
	list_for_each_cookie(pos, head->read_var2,
			     &tomoyo_domain_keeper_list) {
		struct tomoyo_domain_keeper_entry *ptr;
		const char *no;
		const char *from = "";
		const char *program = "";

		ptr = list_entry(pos, struct tomoyo_domain_keeper_entry, list);
		if (ptr->is_deleted)
			continue;
		no = ptr->is_not ? "no_" : "";
		if (ptr->program) {
			from = " from ";
			program = ptr->program->name;
		}
		if (!tomoyo_io_printf(head,
				      "%s" TOMOYO_KEYWORD_KEEP_DOMAIN
				      "%s%s%s\n", no, program, from,
				      ptr->domainname->name)) {
			done = false;
			break;
		}
	}
	up_read(&tomoyo_domain_keeper_list_lock);
	return done;
}

/**
 * tomoyo_is_domain_keeper - Check whether the given program causes domain transition suppression.
 *
 * @domainname: The name of domain.
 * @program:    The name of program.
 * @last_name:  The last component of @domainname.
 *
 * Returns true if executing @program supresses domain transition,
 * false otherwise.
 */
static bool tomoyo_is_domain_keeper(const struct tomoyo_path_info *domainname,
				    const struct tomoyo_path_info *program,
				    const struct tomoyo_path_info *last_name)
{
	struct tomoyo_domain_keeper_entry *ptr;
	bool flag = false;

	down_read(&tomoyo_domain_keeper_list_lock);
	list_for_each_entry(ptr, &tomoyo_domain_keeper_list, list) {
		if (ptr->is_deleted)
			continue;
		if (!ptr->is_last_name) {
			if (ptr->domainname != domainname)
				continue;
		} else {
			if (tomoyo_pathcmp(ptr->domainname, last_name))
				continue;
		}
		if (ptr->program && tomoyo_pathcmp(ptr->program, program))
			continue;
		if (ptr->is_not) {
			flag = false;
			break;
		}
		flag = true;
	}
	up_read(&tomoyo_domain_keeper_list_lock);
	return flag;
}

/* The list for "struct tomoyo_alias_entry". */
static LIST_HEAD(tomoyo_alias_list);
static DECLARE_RWSEM(tomoyo_alias_list_lock);

/**
 * tomoyo_update_alias_entry - Update "struct tomoyo_alias_entry" list.
 *
 * @original_name: The original program's real name.
 * @aliased_name:  The symbolic program's symbolic link's name.
 * @is_delete:     True if it is a delete request.
 *
 * Returns 0 on success, negative value otherwise.
 */
static int tomoyo_update_alias_entry(const char *original_name,
				     const char *aliased_name,
				     const bool is_delete)
{
	struct tomoyo_alias_entry *new_entry;
	struct tomoyo_alias_entry *ptr;
	const struct tomoyo_path_info *saved_original_name;
	const struct tomoyo_path_info *saved_aliased_name;
	int error = -ENOMEM;

	if (!tomoyo_is_correct_path(original_name, 1, -1, -1, __func__) ||
	    !tomoyo_is_correct_path(aliased_name, 1, -1, -1, __func__))
		return -EINVAL; /* No patterns allowed. */
	saved_original_name = tomoyo_save_name(original_name);
	saved_aliased_name = tomoyo_save_name(aliased_name);
	if (!saved_original_name || !saved_aliased_name)
		return -ENOMEM;
	/***** EXCLUSIVE SECTION START *****/
	down_write(&tomoyo_alias_list_lock);
	list_for_each_entry(ptr, &tomoyo_alias_list, list) {
		if (ptr->original_name != saved_original_name ||
		    ptr->aliased_name != saved_aliased_name)
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
	new_entry->original_name = saved_original_name;
	new_entry->aliased_name = saved_aliased_name;
	list_add_tail(&new_entry->list, &tomoyo_alias_list);
	error = 0;
 out:
	up_write(&tomoyo_alias_list_lock);
	/***** EXCLUSIVE SECTION END *****/
	return error;
}

/**
 * tomoyo_read_alias_policy - Read "struct tomoyo_alias_entry" list.
 *
 * @head: Pointer to "struct tomoyo_io_buffer".
 *
 * Returns true on success, false otherwise.
 */
bool tomoyo_read_alias_policy(struct tomoyo_io_buffer *head)
{
	struct list_head *pos;
	bool done = true;

	down_read(&tomoyo_alias_list_lock);
	list_for_each_cookie(pos, head->read_var2, &tomoyo_alias_list) {
		struct tomoyo_alias_entry *ptr;

		ptr = list_entry(pos, struct tomoyo_alias_entry, list);
		if (ptr->is_deleted)
			continue;
		if (!tomoyo_io_printf(head, TOMOYO_KEYWORD_ALIAS "%s %s\n",
				      ptr->original_name->name,
				      ptr->aliased_name->name)) {
			done = false;
			break;
		}
	}
	up_read(&tomoyo_alias_list_lock);
	return done;
}

/**
 * tomoyo_write_alias_policy - Write "struct tomoyo_alias_entry" list.
 *
 * @data:      String to parse.
 * @is_delete: True if it is a delete request.
 *
 * Returns 0 on success, negative value otherwise.
 */
int tomoyo_write_alias_policy(char *data, const bool is_delete)
{
	char *cp = strchr(data, ' ');

	if (!cp)
		return -EINVAL;
	*cp++ = '\0';
	return tomoyo_update_alias_entry(data, cp, is_delete);
}

/* Domain create/delete handler. */

/**
 * tomoyo_delete_domain - Delete a domain.
 *
 * @domainname: The name of domain.
 *
 * Returns 0.
 */
int tomoyo_delete_domain(char *domainname)
{
	struct tomoyo_domain_info *domain;
	struct tomoyo_path_info name;

	name.name = domainname;
	tomoyo_fill_path_info(&name);
	/***** EXCLUSIVE SECTION START *****/
	down_write(&tomoyo_domain_list_lock);
	/* Is there an active domain? */
	list_for_each_entry(domain, &tomoyo_domain_list, list) {
		/* Never delete tomoyo_kernel_domain */
		if (domain == &tomoyo_kernel_domain)
			continue;
		if (domain->is_deleted ||
		    tomoyo_pathcmp(domain->domainname, &name))
			continue;
		domain->is_deleted = true;
		break;
	}
	up_write(&tomoyo_domain_list_lock);
	/***** EXCLUSIVE SECTION END *****/
	return 0;
}

/**
 * tomoyo_find_or_assign_new_domain - Create a domain.
 *
 * @domainname: The name of domain.
 * @profile:    Profile number to assign if the domain was newly created.
 *
 * Returns pointer to "struct tomoyo_domain_info" on success, NULL otherwise.
 */
struct tomoyo_domain_info *tomoyo_find_or_assign_new_domain(const char *
							    domainname,
							    const u8 profile)
{
	struct tomoyo_domain_info *domain = NULL;
	const struct tomoyo_path_info *saved_domainname;

	/***** EXCLUSIVE SECTION START *****/
	down_write(&tomoyo_domain_list_lock);
	domain = tomoyo_find_domain(domainname);
	if (domain)
		goto out;
	if (!tomoyo_is_correct_domain(domainname, __func__))
		goto out;
	saved_domainname = tomoyo_save_name(domainname);
	if (!saved_domainname)
		goto out;
	/* Can I reuse memory of deleted domain? */
	list_for_each_entry(domain, &tomoyo_domain_list, list) {
		struct task_struct *p;
		struct tomoyo_acl_info *ptr;
		bool flag;
		if (!domain->is_deleted ||
		    domain->domainname != saved_domainname)
			continue;
		flag = false;
		/***** CRITICAL SECTION START *****/
		read_lock(&tasklist_lock);
		for_each_process(p) {
			if (tomoyo_real_domain(p) != domain)
				continue;
			flag = true;
			break;
		}
		read_unlock(&tasklist_lock);
		/***** CRITICAL SECTION END *****/
		if (flag)
			continue;
		list_for_each_entry(ptr, &domain->acl_info_list, list) {
			ptr->type |= TOMOYO_ACL_DELETED;
		}
		tomoyo_set_domain_flag(domain, true, domain->flags);
		domain->profile = profile;
		domain->quota_warned = false;
		mb(); /* Avoid out-of-order execution. */
		domain->is_deleted = false;
		goto out;
	}
	/* No memory reusable. Create using new memory. */
	domain = tomoyo_alloc_element(sizeof(*domain));
	if (domain) {
		INIT_LIST_HEAD(&domain->acl_info_list);
		domain->domainname = saved_domainname;
		domain->profile = profile;
		list_add_tail(&domain->list, &tomoyo_domain_list);
	}
 out:
	up_write(&tomoyo_domain_list_lock);
	/***** EXCLUSIVE SECTION END *****/
	return domain;
}

/**
 * tomoyo_find_next_domain - Find a domain.
 *
 * @bprm:           Pointer to "struct linux_binprm".
 * @next_domain:    Pointer to pointer to "struct tomoyo_domain_info".
 *
 * Returns 0 on success, negative value otherwise.
 */
int tomoyo_find_next_domain(struct linux_binprm *bprm,
			    struct tomoyo_domain_info **next_domain)
{
	/*
	 * This function assumes that the size of buffer returned by
	 * tomoyo_realpath() = TOMOYO_MAX_PATHNAME_LEN.
	 */
	struct tomoyo_page_buffer *tmp = tomoyo_alloc(sizeof(*tmp));
	struct tomoyo_domain_info *old_domain = tomoyo_domain();
	struct tomoyo_domain_info *domain = NULL;
	const char *old_domain_name = old_domain->domainname->name;
	const char *original_name = bprm->filename;
	char *new_domain_name = NULL;
	char *real_program_name = NULL;
	char *symlink_program_name = NULL;
	const u8 mode = tomoyo_check_flags(old_domain, TOMOYO_MAC_FOR_FILE);
	const bool is_enforce = (mode == 3);
	int retval = -ENOMEM;
	struct tomoyo_path_info r; /* real name */
	struct tomoyo_path_info s; /* symlink name */
	struct tomoyo_path_info l; /* last name */
	static bool initialized;

	if (!tmp)
		goto out;

	if (!initialized) {
		/*
		 * Built-in initializers. This is needed because policies are
		 * not loaded until starting /sbin/init.
		 */
		tomoyo_update_domain_initializer_entry(NULL, "/sbin/hotplug",
						       false, false);
		tomoyo_update_domain_initializer_entry(NULL, "/sbin/modprobe",
						       false, false);
		initialized = true;
	}

	/* Get tomoyo_realpath of program. */
	retval = -ENOENT;
	/* I hope tomoyo_realpath() won't fail with -ENOMEM. */
	real_program_name = tomoyo_realpath(original_name);
	if (!real_program_name)
		goto out;
	/* Get tomoyo_realpath of symbolic link. */
	symlink_program_name = tomoyo_realpath_nofollow(original_name);
	if (!symlink_program_name)
		goto out;

	r.name = real_program_name;
	tomoyo_fill_path_info(&r);
	s.name = symlink_program_name;
	tomoyo_fill_path_info(&s);
	l.name = tomoyo_get_last_name(old_domain);
	tomoyo_fill_path_info(&l);

	/* Check 'alias' directive. */
	if (tomoyo_pathcmp(&r, &s)) {
		struct tomoyo_alias_entry *ptr;
		/* Is this program allowed to be called via symbolic links? */
		down_read(&tomoyo_alias_list_lock);
		list_for_each_entry(ptr, &tomoyo_alias_list, list) {
			if (ptr->is_deleted ||
			    tomoyo_pathcmp(&r, ptr->original_name) ||
			    tomoyo_pathcmp(&s, ptr->aliased_name))
				continue;
			memset(real_program_name, 0, TOMOYO_MAX_PATHNAME_LEN);
			strncpy(real_program_name, ptr->aliased_name->name,
				TOMOYO_MAX_PATHNAME_LEN - 1);
			tomoyo_fill_path_info(&r);
			break;
		}
		up_read(&tomoyo_alias_list_lock);
	}

	/* Check execute permission. */
	retval = tomoyo_check_exec_perm(old_domain, &r, tmp);
	if (retval < 0)
		goto out;

	new_domain_name = tmp->buffer;
	if (tomoyo_is_domain_initializer(old_domain->domainname, &r, &l)) {
		/* Transit to the child of tomoyo_kernel_domain domain. */
		snprintf(new_domain_name, TOMOYO_MAX_PATHNAME_LEN + 1,
			 TOMOYO_ROOT_NAME " " "%s", real_program_name);
	} else if (old_domain == &tomoyo_kernel_domain &&
		   !tomoyo_policy_loaded) {
		/*
		 * Needn't to transit from kernel domain before starting
		 * /sbin/init. But transit from kernel domain if executing
		 * initializers because they might start before /sbin/init.
		 */
		domain = old_domain;
	} else if (tomoyo_is_domain_keeper(old_domain->domainname, &r, &l)) {
		/* Keep current domain. */
		domain = old_domain;
	} else {
		/* Normal domain transition. */
		snprintf(new_domain_name, TOMOYO_MAX_PATHNAME_LEN + 1,
			 "%s %s", old_domain_name, real_program_name);
	}
	if (domain || strlen(new_domain_name) >= TOMOYO_MAX_PATHNAME_LEN)
		goto done;
	down_read(&tomoyo_domain_list_lock);
	domain = tomoyo_find_domain(new_domain_name);
	up_read(&tomoyo_domain_list_lock);
	if (domain)
		goto done;
	if (is_enforce)
		goto done;
	domain = tomoyo_find_or_assign_new_domain(new_domain_name,
						  old_domain->profile);
 done:
	if (domain)
		goto out;
	printk(KERN_WARNING "TOMOYO-ERROR: Domain '%s' not defined.\n",
	       new_domain_name);
	if (is_enforce)
		retval = -EPERM;
	else
		tomoyo_set_domain_flag(old_domain, false,
				       TOMOYO_DOMAIN_FLAGS_TRANSITION_FAILED);
 out:
	tomoyo_free(real_program_name);
	tomoyo_free(symlink_program_name);
	*next_domain = domain ? domain : old_domain;
	tomoyo_free(tmp);
	return retval;
}
