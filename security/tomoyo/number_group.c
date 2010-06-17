/*
 * security/tomoyo/number_group.c
 *
 * Copyright (C) 2005-2009  NTT DATA CORPORATION
 */

#include <linux/slab.h>
#include "common.h"

/**
 * tomoyo_get_group - Allocate memory for "struct tomoyo_number_group".
 *
 * @group_name: The name of number group.
 *
 * Returns pointer to "struct tomoyo_number_group" on success,
 * NULL otherwise.
 */
struct tomoyo_group *tomoyo_get_number_group(const char *group_name)
{
	struct tomoyo_group *entry = NULL;
	struct tomoyo_group *group = NULL;
	const struct tomoyo_path_info *saved_group_name;
	int error = -ENOMEM;
	if (!tomoyo_correct_word(group_name))
		return NULL;
	saved_group_name = tomoyo_get_name(group_name);
	if (!saved_group_name)
		return NULL;
	entry = kzalloc(sizeof(*entry), GFP_NOFS);
	if (mutex_lock_interruptible(&tomoyo_policy_lock))
		goto out;
	list_for_each_entry_rcu(group, &tomoyo_group_list[TOMOYO_NUMBER_GROUP],
				list) {
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
		list_add_tail_rcu(&entry->list,
				  &tomoyo_group_list[TOMOYO_NUMBER_GROUP]);
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

static bool tomoyo_same_number_group(const struct tomoyo_acl_head *a,
				     const struct tomoyo_acl_head *b)
{
	return !memcmp(&container_of(a, struct tomoyo_number_group,
				     head)->number,
		       &container_of(b, struct tomoyo_number_group,
				     head)->number,
		       sizeof(container_of(a,
					   struct tomoyo_number_group,
					   head)->number));
}

/**
 * tomoyo_write_number_group_policy - Write "struct tomoyo_number_group" list.
 *
 * @data:      String to parse.
 * @is_delete: True if it is a delete request.
 *
 * Returns 0 on success, nagative value otherwise.
 */
int tomoyo_write_number_group_policy(char *data, const bool is_delete)
{
	struct tomoyo_group *group;
	struct tomoyo_number_group e = { };
	int error;
	char *w[2];
	if (!tomoyo_tokenize(data, w, sizeof(w)))
		return -EINVAL;
	if (w[1][0] == '@' || !tomoyo_parse_number_union(w[1], &e.number) ||
	    e.number.values[0] > e.number.values[1])
		return -EINVAL;
	group = tomoyo_get_number_group(w[0]);
	if (!group)
		return -ENOMEM;
	error = tomoyo_update_policy(&e.head, sizeof(e), is_delete,
				     &group->member_list,
				     tomoyo_same_number_group);
	tomoyo_put_group(group);
	return error;
}

/**
 * tomoyo_number_matches_group - Check whether the given number matches members of the given number group.
 *
 * @min:   Min number.
 * @max:   Max number.
 * @group: Pointer to "struct tomoyo_number_group".
 *
 * Returns true if @min and @max partially overlaps @group, false otherwise.
 *
 * Caller holds tomoyo_read_lock().
 */
bool tomoyo_number_matches_group(const unsigned long min,
				 const unsigned long max,
				 const struct tomoyo_group *group)
{
	struct tomoyo_number_group *member;
	bool matched = false;
	list_for_each_entry_rcu(member, &group->member_list, head.list) {
		if (member->head.is_deleted)
			continue;
		if (min > member->number.values[1] ||
		    max < member->number.values[0])
			continue;
		matched = true;
		break;
	}
	return matched;
}
