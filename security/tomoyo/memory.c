/*
 * security/tomoyo/memory.c
 *
 * Copyright (C) 2005-2011  NTT DATA CORPORATION
 */

#include <linux/hash.h>
#include <linux/slab.h>
#include "common.h"

/**
 * tomoyo_warn_oom - Print out of memory warning message.
 *
 * @function: Function's name.
 */
void tomoyo_warn_oom(const char *function)
{
	/* Reduce error messages. */
	static pid_t tomoyo_last_pid;
	const pid_t pid = current->pid;
	if (tomoyo_last_pid != pid) {
		printk(KERN_WARNING "ERROR: Out of memory at %s.\n",
		       function);
		tomoyo_last_pid = pid;
	}
	if (!tomoyo_policy_loaded)
		panic("MAC Initialization failed.\n");
}

/* Memoy currently used by policy/audit log/query. */
unsigned int tomoyo_memory_used[TOMOYO_MAX_MEMORY_STAT];
/* Memory quota for "policy"/"audit log"/"query". */
unsigned int tomoyo_memory_quota[TOMOYO_MAX_MEMORY_STAT];

/**
 * tomoyo_memory_ok - Check memory quota.
 *
 * @ptr: Pointer to allocated memory.
 *
 * Returns true on success, false otherwise.
 *
 * Returns true if @ptr is not NULL and quota not exceeded, false otherwise.
 *
 * Caller holds tomoyo_policy_lock mutex.
 */
bool tomoyo_memory_ok(void *ptr)
{
	if (ptr) {
		const size_t s = ksize(ptr);
		tomoyo_memory_used[TOMOYO_MEMORY_POLICY] += s;
		if (!tomoyo_memory_quota[TOMOYO_MEMORY_POLICY] ||
		    tomoyo_memory_used[TOMOYO_MEMORY_POLICY] <=
		    tomoyo_memory_quota[TOMOYO_MEMORY_POLICY])
			return true;
		tomoyo_memory_used[TOMOYO_MEMORY_POLICY] -= s;
	}
	tomoyo_warn_oom(__func__);
	return false;
}

/**
 * tomoyo_commit_ok - Check memory quota.
 *
 * @data:   Data to copy from.
 * @size:   Size in byte.
 *
 * Returns pointer to allocated memory on success, NULL otherwise.
 * @data is zero-cleared on success.
 *
 * Caller holds tomoyo_policy_lock mutex.
 */
void *tomoyo_commit_ok(void *data, const unsigned int size)
{
	void *ptr = kzalloc(size, GFP_NOFS);
	if (tomoyo_memory_ok(ptr)) {
		memmove(ptr, data, size);
		memset(data, 0, size);
		return ptr;
	}
	kfree(ptr);
	return NULL;
}

/**
 * tomoyo_get_group - Allocate memory for "struct tomoyo_path_group"/"struct tomoyo_number_group".
 *
 * @param: Pointer to "struct tomoyo_acl_param".
 * @idx:   Index number.
 *
 * Returns pointer to "struct tomoyo_group" on success, NULL otherwise.
 */
struct tomoyo_group *tomoyo_get_group(struct tomoyo_acl_param *param,
				      const u8 idx)
{
	struct tomoyo_group e = { };
	struct tomoyo_group *group = NULL;
	struct list_head *list;
	const char *group_name = tomoyo_read_token(param);
	bool found = false;
	if (!tomoyo_correct_word(group_name) || idx >= TOMOYO_MAX_GROUP)
		return NULL;
	e.group_name = tomoyo_get_name(group_name);
	if (!e.group_name)
		return NULL;
	if (mutex_lock_interruptible(&tomoyo_policy_lock))
		goto out;
	list = &param->ns->group_list[idx];
	list_for_each_entry(group, list, head.list) {
		if (e.group_name != group->group_name ||
		    atomic_read(&group->head.users) == TOMOYO_GC_IN_PROGRESS)
			continue;
		atomic_inc(&group->head.users);
		found = true;
		break;
	}
	if (!found) {
		struct tomoyo_group *entry = tomoyo_commit_ok(&e, sizeof(e));
		if (entry) {
			INIT_LIST_HEAD(&entry->member_list);
			atomic_set(&entry->head.users, 1);
			list_add_tail_rcu(&entry->head.list, list);
			group = entry;
			found = true;
		}
	}
	mutex_unlock(&tomoyo_policy_lock);
out:
	tomoyo_put_name(e.group_name);
	return found ? group : NULL;
}

/*
 * tomoyo_name_list is used for holding string data used by TOMOYO.
 * Since same string data is likely used for multiple times (e.g.
 * "/lib/libc-2.5.so"), TOMOYO shares string data in the form of
 * "const struct tomoyo_path_info *".
 */
struct list_head tomoyo_name_list[TOMOYO_MAX_HASH];

/**
 * tomoyo_get_name - Allocate permanent memory for string data.
 *
 * @name: The string to store into the permernent memory.
 *
 * Returns pointer to "struct tomoyo_path_info" on success, NULL otherwise.
 */
const struct tomoyo_path_info *tomoyo_get_name(const char *name)
{
	struct tomoyo_name *ptr;
	unsigned int hash;
	int len;
	struct list_head *head;

	if (!name)
		return NULL;
	len = strlen(name) + 1;
	hash = full_name_hash(NULL, (const unsigned char *) name, len - 1);
	head = &tomoyo_name_list[hash_long(hash, TOMOYO_HASH_BITS)];
	if (mutex_lock_interruptible(&tomoyo_policy_lock))
		return NULL;
	list_for_each_entry(ptr, head, head.list) {
		if (hash != ptr->entry.hash || strcmp(name, ptr->entry.name) ||
		    atomic_read(&ptr->head.users) == TOMOYO_GC_IN_PROGRESS)
			continue;
		atomic_inc(&ptr->head.users);
		goto out;
	}
	ptr = kzalloc(sizeof(*ptr) + len, GFP_NOFS);
	if (tomoyo_memory_ok(ptr)) {
		ptr->entry.name = ((char *) ptr) + sizeof(*ptr);
		memmove((char *) ptr->entry.name, name, len);
		atomic_set(&ptr->head.users, 1);
		tomoyo_fill_path_info(&ptr->entry);
		list_add_tail(&ptr->head.list, head);
	} else {
		kfree(ptr);
		ptr = NULL;
	}
out:
	mutex_unlock(&tomoyo_policy_lock);
	return ptr ? &ptr->entry : NULL;
}

/* Initial namespace.*/
struct tomoyo_policy_namespace tomoyo_kernel_namespace;

/**
 * tomoyo_mm_init - Initialize mm related code.
 */
void __init tomoyo_mm_init(void)
{
	int idx;
	for (idx = 0; idx < TOMOYO_MAX_HASH; idx++)
		INIT_LIST_HEAD(&tomoyo_name_list[idx]);
	tomoyo_kernel_namespace.name = "<kernel>";
	tomoyo_init_policy_namespace(&tomoyo_kernel_namespace);
	tomoyo_kernel_domain.ns = &tomoyo_kernel_namespace;
	INIT_LIST_HEAD(&tomoyo_kernel_domain.acl_info_list);
	tomoyo_kernel_domain.domainname = tomoyo_get_name("<kernel>");
	list_add_tail_rcu(&tomoyo_kernel_domain.list, &tomoyo_domain_list);
}
