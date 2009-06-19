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

/*
 * tomoyo_domain_list is used for holding list of domains.
 * The ->acl_info_list of "struct tomoyo_domain_info" is used for holding
 * permissions (e.g. "allow_read /lib/libc-2.5.so") given to each domain.
 *
 * An entry is added by
 *
 * # ( echo "<kernel>"; echo "allow_execute /sbin/init" ) > \
 *                                  /sys/kernel/security/tomoyo/domain_policy
 *
 * and is deleted by
 *
 * # ( echo "<kernel>"; echo "delete allow_execute /sbin/init" ) > \
 *                                  /sys/kernel/security/tomoyo/domain_policy
 *
 * and all entries are retrieved by
 *
 * # cat /sys/kernel/security/tomoyo/domain_policy
 *
 * A domain is added by
 *
 * # echo "<kernel>" > /sys/kernel/security/tomoyo/domain_policy
 *
 * and is deleted by
 *
 * # echo "delete <kernel>" > /sys/kernel/security/tomoyo/domain_policy
 *
 * and all domains are retrieved by
 *
 * # grep '^<kernel>' /sys/kernel/security/tomoyo/domain_policy
 *
 * Normally, a domainname is monotonically getting longer because a domainname
 * which the process will belong to if an execve() operation succeeds is
 * defined as a concatenation of "current domainname" + "pathname passed to
 * execve()".
 * See tomoyo_domain_initializer_list and tomoyo_domain_keeper_list for
 * exceptions.
 */
LIST_HEAD(tomoyo_domain_list);
DECLARE_RWSEM(tomoyo_domain_list_lock);

/*
 * tomoyo_domain_initializer_entry is a structure which is used for holding
 * "initialize_domain" and "no_initialize_domain" entries.
 * It has following fields.
 *
 *  (1) "list" which is linked to tomoyo_domain_initializer_list .
 *  (2) "domainname" which is "a domainname" or "the last component of a
 *      domainname". This field is NULL if "from" clause is not specified.
 *  (3) "program" which is a program's pathname.
 *  (4) "is_deleted" is a bool which is true if marked as deleted, false
 *      otherwise.
 *  (5) "is_not" is a bool which is true if "no_initialize_domain", false
 *      otherwise.
 *  (6) "is_last_name" is a bool which is true if "domainname" is "the last
 *      component of a domainname", false otherwise.
 */
struct tomoyo_domain_initializer_entry {
	struct list_head list;
	const struct tomoyo_path_info *domainname;    /* This may be NULL */
	const struct tomoyo_path_info *program;
	bool is_deleted;
	bool is_not;       /* True if this entry is "no_initialize_domain".  */
	/* True if the domainname is tomoyo_get_last_name(). */
	bool is_last_name;
};

/*
 * tomoyo_domain_keeper_entry is a structure which is used for holding
 * "keep_domain" and "no_keep_domain" entries.
 * It has following fields.
 *
 *  (1) "list" which is linked to tomoyo_domain_keeper_list .
 *  (2) "domainname" which is "a domainname" or "the last component of a
 *      domainname".
 *  (3) "program" which is a program's pathname.
 *      This field is NULL if "from" clause is not specified.
 *  (4) "is_deleted" is a bool which is true if marked as deleted, false
 *      otherwise.
 *  (5) "is_not" is a bool which is true if "no_initialize_domain", false
 *      otherwise.
 *  (6) "is_last_name" is a bool which is true if "domainname" is "the last
 *      component of a domainname", false otherwise.
 */
struct tomoyo_domain_keeper_entry {
	struct list_head list;
	const struct tomoyo_path_info *domainname;
	const struct tomoyo_path_info *program;       /* This may be NULL */
	bool is_deleted;
	bool is_not;       /* True if this entry is "no_keep_domain".        */
	/* True if the domainname is tomoyo_get_last_name(). */
	bool is_last_name;
};

/*
 * tomoyo_alias_entry is a structure which is used for holding "alias" entries.
 * It has following fields.
 *
 *  (1) "list" which is linked to tomoyo_alias_list .
 *  (2) "original_name" which is a dereferenced pathname.
 *  (3) "aliased_name" which is a symlink's pathname.
 *  (4) "is_deleted" is a bool which is true if marked as deleted, false
 *      otherwise.
 */
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
	spin_lock(&lock);
	if (!is_delete)
		domain->flags |= flags;
	else
		domain->flags &= ~flags;
	spin_unlock(&lock);
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

/*
 * tomoyo_domain_initializer_list is used for holding list of programs which
 * triggers reinitialization of domainname. Normally, a domainname is
 * monotonically getting longer. But sometimes, we restart daemon programs.
 * It would be convenient for us that "a daemon started upon system boot" and
 * "the daemon restarted from console" belong to the same domain. Thus, TOMOYO
 * provides a way to shorten domainnames.
 *
 * An entry is added by
 *
 * # echo 'initialize_domain /usr/sbin/httpd' > \
 *                               /sys/kernel/security/tomoyo/exception_policy
 *
 * and is deleted by
 *
 * # echo 'delete initialize_domain /usr/sbin/httpd' > \
 *                               /sys/kernel/security/tomoyo/exception_policy
 *
 * and all entries are retrieved by
 *
 * # grep ^initialize_domain /sys/kernel/security/tomoyo/exception_policy
 *
 * In the example above, /usr/sbin/httpd will belong to
 * "<kernel> /usr/sbin/httpd" domain.
 *
 * You may specify a domainname using "from" keyword.
 * "initialize_domain /usr/sbin/httpd from <kernel> /etc/rc.d/init.d/httpd"
 * will cause "/usr/sbin/httpd" executed from "<kernel> /etc/rc.d/init.d/httpd"
 * domain to belong to "<kernel> /usr/sbin/httpd" domain.
 *
 * You may add "no_" prefix to "initialize_domain".
 * "initialize_domain /usr/sbin/httpd" and
 * "no_initialize_domain /usr/sbin/httpd from <kernel> /etc/rc.d/init.d/httpd"
 * will cause "/usr/sbin/httpd" to belong to "<kernel> /usr/sbin/httpd" domain
 * unless executed from "<kernel> /etc/rc.d/init.d/httpd" domain.
 */
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
		done = tomoyo_io_printf(head,
					"%s" TOMOYO_KEYWORD_INITIALIZE_DOMAIN
					"%s%s%s\n", no, ptr->program->name,
					from, domain);
		if (!done)
			break;
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

/*
 * tomoyo_domain_keeper_list is used for holding list of domainnames which
 * suppresses domain transition. Normally, a domainname is monotonically
 * getting longer. But sometimes, we want to suppress domain transition.
 * It would be convenient for us that programs executed from a login session
 * belong to the same domain. Thus, TOMOYO provides a way to suppress domain
 * transition.
 *
 * An entry is added by
 *
 * # echo 'keep_domain <kernel> /usr/sbin/sshd /bin/bash' > \
 *                              /sys/kernel/security/tomoyo/exception_policy
 *
 * and is deleted by
 *
 * # echo 'delete keep_domain <kernel> /usr/sbin/sshd /bin/bash' > \
 *                              /sys/kernel/security/tomoyo/exception_policy
 *
 * and all entries are retrieved by
 *
 * # grep ^keep_domain /sys/kernel/security/tomoyo/exception_policy
 *
 * In the example above, any process which belongs to
 * "<kernel> /usr/sbin/sshd /bin/bash" domain will remain in that domain,
 * unless explicitly specified by "initialize_domain" or "no_keep_domain".
 *
 * You may specify a program using "from" keyword.
 * "keep_domain /bin/pwd from <kernel> /usr/sbin/sshd /bin/bash"
 * will cause "/bin/pwd" executed from "<kernel> /usr/sbin/sshd /bin/bash"
 * domain to remain in "<kernel> /usr/sbin/sshd /bin/bash" domain.
 *
 * You may add "no_" prefix to "keep_domain".
 * "keep_domain <kernel> /usr/sbin/sshd /bin/bash" and
 * "no_keep_domain /usr/bin/passwd from <kernel> /usr/sbin/sshd /bin/bash" will
 * cause "/usr/bin/passwd" to belong to
 * "<kernel> /usr/sbin/sshd /bin/bash /usr/bin/passwd" domain, unless
 * explicitly specified by "initialize_domain".
 */
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
		done = tomoyo_io_printf(head,
					"%s" TOMOYO_KEYWORD_KEEP_DOMAIN
					"%s%s%s\n", no, program, from,
					ptr->domainname->name);
		if (!done)
			break;
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

/*
 * tomoyo_alias_list is used for holding list of symlink's pathnames which are
 * allowed to be passed to an execve() request. Normally, the domainname which
 * the current process will belong to after execve() succeeds is calculated
 * using dereferenced pathnames. But some programs behave differently depending
 * on the name passed to argv[0]. For busybox, calculating domainname using
 * dereferenced pathnames will cause all programs in the busybox to belong to
 * the same domain. Thus, TOMOYO provides a way to allow use of symlink's
 * pathname for checking execve()'s permission and calculating domainname which
 * the current process will belong to after execve() succeeds.
 *
 * An entry is added by
 *
 * # echo 'alias /bin/busybox /bin/cat' > \
 *                            /sys/kernel/security/tomoyo/exception_policy
 *
 * and is deleted by
 *
 * # echo 'delete alias /bin/busybox /bin/cat' > \
 *                            /sys/kernel/security/tomoyo/exception_policy
 *
 * and all entries are retrieved by
 *
 * # grep ^alias /sys/kernel/security/tomoyo/exception_policy
 *
 * In the example above, if /bin/cat is a symlink to /bin/busybox and execution
 * of /bin/cat is requested, permission is checked for /bin/cat rather than
 * /bin/busybox and domainname which the current process will belong to after
 * execve() succeeds is calculated using /bin/cat rather than /bin/busybox .
 */
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
		done = tomoyo_io_printf(head, TOMOYO_KEYWORD_ALIAS "%s %s\n",
					ptr->original_name->name,
					ptr->aliased_name->name);
		if (!done)
			break;
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
		read_lock(&tasklist_lock);
		for_each_process(p) {
			if (tomoyo_real_domain(p) != domain)
				continue;
			flag = true;
			break;
		}
		read_unlock(&tasklist_lock);
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
	return domain;
}

/**
 * tomoyo_find_next_domain - Find a domain.
 *
 * @bprm: Pointer to "struct linux_binprm".
 *
 * Returns 0 on success, negative value otherwise.
 */
int tomoyo_find_next_domain(struct linux_binprm *bprm)
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
	retval = tomoyo_check_exec_perm(old_domain, &r);
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
	if (!domain)
		domain = old_domain;
	bprm->cred->security = domain;
	tomoyo_free(real_program_name);
	tomoyo_free(symlink_program_name);
	tomoyo_free(tmp);
	return retval;
}
