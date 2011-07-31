/*
 * security/tomoyo/group.c
 *
 * Copyright (C) 2005-2010  NTT DATA CORPORATION
 */

#include <linux/slab.h>
#include "common.h"

static bool tomoyo_same_path_group(const struct tomoyo_acl_head *a,
				const struct tomoyo_acl_head *b)
{
	return container_of(a, struct tomoyo_path_group, head)->member_name ==
		container_of(b, struct tomoyo_path_group, head)->member_name;
}

static bool tomoyo_same_number_group(const struct tomoyo_acl_head *a,
				  const struct tomoyo_acl_head *b)
{
	return !memcmp(&container_of(a, struct tomoyo_number_group, head)
		       ->number,
		       &container_of(b, struct tomoyo_number_group, head)
		       ->number,
		       sizeof(container_of(a, struct tomoyo_number_group, head)
			      ->number));
}

/**
 * tomoyo_write_group - Write "struct tomoyo_path_group"/"struct tomoyo_number_group" list.
 *
 * @data:      String to parse.
 * @is_delete: True if it is a delete request.
 * @type:      Type of this group.
 *
 * Returns 0 on success, negative value otherwise.
 */
int tomoyo_write_group(char *data, const bool is_delete, const u8 type)
{
	struct tomoyo_group *group;
	struct list_head *member;
	char *w[2];
	int error = -EINVAL;
	if (!tomoyo_tokenize(data, w, sizeof(w)) || !w[1][0])
		return -EINVAL;
	group = tomoyo_get_group(w[0], type);
	if (!group)
		return -ENOMEM;
	member = &group->member_list;
	if (type == TOMOYO_PATH_GROUP) {
		struct tomoyo_path_group e = { };
		e.member_name = tomoyo_get_name(w[1]);
		if (!e.member_name) {
			error = -ENOMEM;
			goto out;
		}
		error = tomoyo_update_policy(&e.head, sizeof(e), is_delete,
					     member, tomoyo_same_path_group);
		tomoyo_put_name(e.member_name);
	} else if (type == TOMOYO_NUMBER_GROUP) {
		struct tomoyo_number_group e = { };
		if (w[1][0] == '@'
		    || !tomoyo_parse_number_union(w[1], &e.number)
		    || e.number.values[0] > e.number.values[1])
			goto out;
		error = tomoyo_update_policy(&e.head, sizeof(e), is_delete,
					     member, tomoyo_same_number_group);
		/*
		 * tomoyo_put_number_union() is not needed because
		 * w[1][0] != '@'.
		 */
	}
 out:
	tomoyo_put_group(group);
	return error;
}

/**
 * tomoyo_path_matches_group - Check whether the given pathname matches members of the given pathname group.
 *
 * @pathname:        The name of pathname.
 * @group:           Pointer to "struct tomoyo_path_group".
 *
 * Returns matched member's pathname if @pathname matches pathnames in @group,
 * NULL otherwise.
 *
 * Caller holds tomoyo_read_lock().
 */
const struct tomoyo_path_info *
tomoyo_path_matches_group(const struct tomoyo_path_info *pathname,
			  const struct tomoyo_group *group)
{
	struct tomoyo_path_group *member;
	list_for_each_entry_rcu(member, &group->member_list, head.list) {
		if (member->head.is_deleted)
			continue;
		if (!tomoyo_path_matches_pattern(pathname, member->member_name))
			continue;
		return member->member_name;
	}
	return NULL;
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
