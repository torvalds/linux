/*
 * security/tomoyo/path_group.c
 *
 * Copyright (C) 2005-2009  NTT DATA CORPORATION
 */

#include <linux/slab.h>
#include "common.h"
/* The list for "struct ccs_path_group". */
LIST_HEAD(tomoyo_path_group_list);

/**
 * tomoyo_get_path_group - Allocate memory for "struct tomoyo_path_group".
 *
 * @group_name: The name of pathname group.
 *
 * Returns pointer to "struct tomoyo_path_group" on success, NULL otherwise.
 */
struct tomoyo_path_group *tomoyo_get_path_group(const char *group_name)
{
	struct tomoyo_path_group *entry = NULL;
	struct tomoyo_path_group *group = NULL;
	const struct tomoyo_path_info *saved_group_name;
	int error = -ENOMEM;
	if (!tomoyo_is_correct_path(group_name, 0, 0, 0) ||
	    !group_name[0])
		return NULL;
	saved_group_name = tomoyo_get_name(group_name);
	if (!saved_group_name)
		return NULL;
	entry = kzalloc(sizeof(*entry), GFP_NOFS);
	if (mutex_lock_interruptible(&tomoyo_policy_lock))
		goto out;
	list_for_each_entry_rcu(group, &tomoyo_path_group_list, list) {
		if (saved_group_name != group->group_name)
			continue;
		atomic_inc(&group->users);
		error = 0;
		break;
	}
	if (error && tomoyo_memory_ok(entry)) {
		INIT_LIST_HEAD(&entry->member_list);
		entry->group_name = saved_group_name;
		saved_group_name = NULL;
		atomic_set(&entry->users, 1);
		list_add_tail_rcu(&entry->list, &tomoyo_path_group_list);
		group = entry;
		entry = NULL;
		error = 0;
	}
	mutex_unlock(&tomoyo_policy_lock);
 out:
	tomoyo_put_name(saved_group_name);
	kfree(entry);
	return !error ? group : NULL;
}

/**
 * tomoyo_write_path_group_policy - Write "struct tomoyo_path_group" list.
 *
 * @data:      String to parse.
 * @is_delete: True if it is a delete request.
 *
 * Returns 0 on success, nagative value otherwise.
 */
int tomoyo_write_path_group_policy(char *data, const bool is_delete)
{
	struct tomoyo_path_group *group;
	struct tomoyo_path_group_member *member;
	struct tomoyo_path_group_member e = { };
	int error = is_delete ? -ENOENT : -ENOMEM;
	char *w[2];
	if (!tomoyo_tokenize(data, w, sizeof(w)) || !w[1][0])
		return -EINVAL;
	group = tomoyo_get_path_group(w[0]);
	if (!group)
		return -ENOMEM;
	e.member_name = tomoyo_get_name(w[1]);
	if (!e.member_name)
		goto out;
	if (mutex_lock_interruptible(&tomoyo_policy_lock))
		goto out;
	list_for_each_entry_rcu(member, &group->member_list, list) {
		if (member->member_name != e.member_name)
			continue;
		member->is_deleted = is_delete;
		error = 0;
		break;
	}
	if (!is_delete && error) {
		struct tomoyo_path_group_member *entry =
			tomoyo_commit_ok(&e, sizeof(e));
		if (entry) {
			list_add_tail_rcu(&entry->list, &group->member_list);
			error = 0;
		}
	}
	mutex_unlock(&tomoyo_policy_lock);
 out:
	tomoyo_put_name(e.member_name);
	tomoyo_put_path_group(group);
	return error;
}

/**
 * tomoyo_read_path_group_policy - Read "struct tomoyo_path_group" list.
 *
 * @head: Pointer to "struct tomoyo_io_buffer".
 *
 * Returns true on success, false otherwise.
 *
 * Caller holds tomoyo_read_lock().
 */
bool tomoyo_read_path_group_policy(struct tomoyo_io_buffer *head)
{
	struct list_head *gpos;
	struct list_head *mpos;
	list_for_each_cookie(gpos, head->read_var1, &tomoyo_path_group_list) {
		struct tomoyo_path_group *group;
		group = list_entry(gpos, struct tomoyo_path_group, list);
		list_for_each_cookie(mpos, head->read_var2,
				     &group->member_list) {
			struct tomoyo_path_group_member *member;
			member = list_entry(mpos,
					    struct tomoyo_path_group_member,
					    list);
			if (member->is_deleted)
				continue;
			if (!tomoyo_io_printf(head, TOMOYO_KEYWORD_PATH_GROUP
					      "%s %s\n",
					      group->group_name->name,
					      member->member_name->name))
				return false;
		}
	}
	return true;
}

/**
 * tomoyo_path_matches_group - Check whether the given pathname matches members of the given pathname group.
 *
 * @pathname:        The name of pathname.
 * @group:           Pointer to "struct tomoyo_path_group".
 * @may_use_pattern: True if wild card is permitted.
 *
 * Returns true if @pathname matches pathnames in @group, false otherwise.
 *
 * Caller holds tomoyo_read_lock().
 */
bool tomoyo_path_matches_group(const struct tomoyo_path_info *pathname,
			       const struct tomoyo_path_group *group,
			       const bool may_use_pattern)
{
	struct tomoyo_path_group_member *member;
	bool matched = false;
	list_for_each_entry_rcu(member, &group->member_list, list) {
		if (member->is_deleted)
			continue;
		if (!member->member_name->is_patterned) {
			if (tomoyo_pathcmp(pathname, member->member_name))
				continue;
		} else if (may_use_pattern) {
			if (!tomoyo_path_matches_pattern(pathname,
							 member->member_name))
				continue;
		} else
			continue;
		matched = true;
		break;
	}
	return matched;
}
