/*
 * security/tomoyo/gc.c
 *
 * Implementation of the Domain-Based Mandatory Access Control.
 *
 * Copyright (C) 2005-2010  NTT DATA CORPORATION
 *
 */

#include "common.h"
#include <linux/kthread.h>
#include <linux/slab.h>

struct tomoyo_gc {
	struct list_head list;
	enum tomoyo_policy_id type;
	struct list_head *element;
};
static LIST_HEAD(tomoyo_gc_queue);
static DEFINE_MUTEX(tomoyo_gc_mutex);

/**
 * tomoyo_add_to_gc - Add an entry to to be deleted list.
 *
 * @type:    One of values in "enum tomoyo_policy_id".
 * @element: Pointer to "struct list_head".
 *
 * Returns true on success, false otherwise.
 *
 * Caller holds tomoyo_policy_lock mutex.
 *
 * Adding an entry needs kmalloc(). Thus, if we try to add thousands of
 * entries at once, it will take too long time. Thus, do not add more than 128
 * entries per a scan. But to be able to handle worst case where all entries
 * are in-use, we accept one more entry per a scan.
 *
 * If we use singly linked list using "struct list_head"->prev (which is
 * LIST_POISON2), we can avoid kmalloc().
 */
static bool tomoyo_add_to_gc(const int type, struct list_head *element)
{
	struct tomoyo_gc *entry = kzalloc(sizeof(*entry), GFP_ATOMIC);
	if (!entry)
		return false;
	entry->type = type;
	entry->element = element;
	list_add(&entry->list, &tomoyo_gc_queue);
	list_del_rcu(element);
	return true;
}

/**
 * tomoyo_del_transition_control - Delete members in "struct tomoyo_transition_control".
 *
 * @element: Pointer to "struct list_head".
 *
 * Returns nothing.
 */
static void tomoyo_del_transition_control(struct list_head *element)
{
	struct tomoyo_transition_control *ptr =
		container_of(element, typeof(*ptr), head.list);
	tomoyo_put_name(ptr->domainname);
	tomoyo_put_name(ptr->program);
}

/**
 * tomoyo_del_aggregator - Delete members in "struct tomoyo_aggregator".
 *
 * @element: Pointer to "struct list_head".
 *
 * Returns nothing.
 */
static void tomoyo_del_aggregator(struct list_head *element)
{
	struct tomoyo_aggregator *ptr =
		container_of(element, typeof(*ptr), head.list);
	tomoyo_put_name(ptr->original_name);
	tomoyo_put_name(ptr->aggregated_name);
}

/**
 * tomoyo_del_manager - Delete members in "struct tomoyo_manager".
 *
 * @element: Pointer to "struct list_head".
 *
 * Returns nothing.
 */
static void tomoyo_del_manager(struct list_head *element)
{
	struct tomoyo_manager *ptr =
		container_of(element, typeof(*ptr), head.list);
	tomoyo_put_name(ptr->manager);
}

/**
 * tomoyo_del_acl - Delete members in "struct tomoyo_acl_info".
 *
 * @element: Pointer to "struct list_head".
 *
 * Returns nothing.
 */
static void tomoyo_del_acl(struct list_head *element)
{
	struct tomoyo_acl_info *acl =
		container_of(element, typeof(*acl), list);
	switch (acl->type) {
	case TOMOYO_TYPE_PATH_ACL:
		{
			struct tomoyo_path_acl *entry
				= container_of(acl, typeof(*entry), head);
			tomoyo_put_name_union(&entry->name);
		}
		break;
	case TOMOYO_TYPE_PATH2_ACL:
		{
			struct tomoyo_path2_acl *entry
				= container_of(acl, typeof(*entry), head);
			tomoyo_put_name_union(&entry->name1);
			tomoyo_put_name_union(&entry->name2);
		}
		break;
	case TOMOYO_TYPE_PATH_NUMBER_ACL:
		{
			struct tomoyo_path_number_acl *entry
				= container_of(acl, typeof(*entry), head);
			tomoyo_put_name_union(&entry->name);
			tomoyo_put_number_union(&entry->number);
		}
		break;
	case TOMOYO_TYPE_MKDEV_ACL:
		{
			struct tomoyo_mkdev_acl *entry
				= container_of(acl, typeof(*entry), head);
			tomoyo_put_name_union(&entry->name);
			tomoyo_put_number_union(&entry->mode);
			tomoyo_put_number_union(&entry->major);
			tomoyo_put_number_union(&entry->minor);
		}
		break;
	case TOMOYO_TYPE_MOUNT_ACL:
		{
			struct tomoyo_mount_acl *entry
				= container_of(acl, typeof(*entry), head);
			tomoyo_put_name_union(&entry->dev_name);
			tomoyo_put_name_union(&entry->dir_name);
			tomoyo_put_name_union(&entry->fs_type);
			tomoyo_put_number_union(&entry->flags);
		}
		break;
	}
}

static bool tomoyo_del_domain(struct list_head *element)
{
	struct tomoyo_domain_info *domain =
		container_of(element, typeof(*domain), list);
	struct tomoyo_acl_info *acl;
	struct tomoyo_acl_info *tmp;
	/*
	 * Since we don't protect whole execve() operation using SRCU,
	 * we need to recheck domain->users at this point.
	 *
	 * (1) Reader starts SRCU section upon execve().
	 * (2) Reader traverses tomoyo_domain_list and finds this domain.
	 * (3) Writer marks this domain as deleted.
	 * (4) Garbage collector removes this domain from tomoyo_domain_list
	 *     because this domain is marked as deleted and used by nobody.
	 * (5) Reader saves reference to this domain into
	 *     "struct linux_binprm"->cred->security .
	 * (6) Reader finishes SRCU section, although execve() operation has
	 *     not finished yet.
	 * (7) Garbage collector waits for SRCU synchronization.
	 * (8) Garbage collector kfree() this domain because this domain is
	 *     used by nobody.
	 * (9) Reader finishes execve() operation and restores this domain from
	 *     "struct linux_binprm"->cred->security.
	 *
	 * By updating domain->users at (5), we can solve this race problem
	 * by rechecking domain->users at (8).
	 */
	if (atomic_read(&domain->users))
		return false;
	list_for_each_entry_safe(acl, tmp, &domain->acl_info_list, list) {
		tomoyo_del_acl(&acl->list);
		tomoyo_memory_free(acl);
	}
	tomoyo_put_name(domain->domainname);
	return true;
}


/**
 * tomoyo_del_name - Delete members in "struct tomoyo_name".
 *
 * @element: Pointer to "struct list_head".
 *
 * Returns nothing.
 */
static void tomoyo_del_name(struct list_head *element)
{
	const struct tomoyo_name *ptr =
		container_of(element, typeof(*ptr), head.list);
}

/**
 * tomoyo_del_path_group - Delete members in "struct tomoyo_path_group".
 *
 * @element: Pointer to "struct list_head".
 *
 * Returns nothing.
 */
static void tomoyo_del_path_group(struct list_head *element)
{
	struct tomoyo_path_group *member =
		container_of(element, typeof(*member), head.list);
	tomoyo_put_name(member->member_name);
}

/**
 * tomoyo_del_group - Delete "struct tomoyo_group".
 *
 * @element: Pointer to "struct list_head".
 *
 * Returns nothing.
 */
static void tomoyo_del_group(struct list_head *element)
{
	struct tomoyo_group *group =
		container_of(element, typeof(*group), head.list);
	tomoyo_put_name(group->group_name);
}

/**
 * tomoyo_del_number_group - Delete members in "struct tomoyo_number_group".
 *
 * @element: Pointer to "struct list_head".
 *
 * Returns nothing.
 */
static void tomoyo_del_number_group(struct list_head *element)
{
	struct tomoyo_number_group *member =
		container_of(element, typeof(*member), head.list);
}

/**
 * tomoyo_collect_member - Delete elements with "struct tomoyo_acl_head".
 *
 * @id:          One of values in "enum tomoyo_policy_id".
 * @member_list: Pointer to "struct list_head".
 *
 * Returns true if some elements are deleted, false otherwise.
 */
static bool tomoyo_collect_member(const enum tomoyo_policy_id id,
				  struct list_head *member_list)
{
	struct tomoyo_acl_head *member;
	list_for_each_entry(member, member_list, list) {
		if (!member->is_deleted)
			continue;
		if (!tomoyo_add_to_gc(id, &member->list))
			return false;
	}
        return true;
}

static bool tomoyo_collect_acl(struct tomoyo_domain_info *domain)
{
	struct tomoyo_acl_info *acl;
	list_for_each_entry(acl, &domain->acl_info_list, list) {
		if (!acl->is_deleted)
			continue;
		if (!tomoyo_add_to_gc(TOMOYO_ID_ACL, &acl->list))
			return false;
	}
	return true;
}

/**
 * tomoyo_collect_entry - Scan lists for deleted elements.
 *
 * Returns nothing.
 */
static void tomoyo_collect_entry(void)
{
	int i;
	if (mutex_lock_interruptible(&tomoyo_policy_lock))
		return;
	for (i = 0; i < TOMOYO_MAX_POLICY; i++) {
		if (!tomoyo_collect_member(i, &tomoyo_policy_list[i]))
			goto unlock;
	}
	{
		struct tomoyo_domain_info *domain;
		list_for_each_entry_rcu(domain, &tomoyo_domain_list, list) {
			if (!tomoyo_collect_acl(domain))
				goto unlock;
			if (!domain->is_deleted || atomic_read(&domain->users))
				continue;
			/*
			 * Nobody is referring this domain. But somebody may
			 * refer this domain after successful execve().
			 * We recheck domain->users after SRCU synchronization.
			 */
			if (!tomoyo_add_to_gc(TOMOYO_ID_DOMAIN, &domain->list))
				goto unlock;
		}
	}
	for (i = 0; i < TOMOYO_MAX_HASH; i++) {
		struct tomoyo_name *ptr;
		list_for_each_entry_rcu(ptr, &tomoyo_name_list[i], head.list) {
			if (atomic_read(&ptr->head.users))
				continue;
			if (!tomoyo_add_to_gc(TOMOYO_ID_NAME, &ptr->head.list))
				goto unlock;
		}
	}
	for (i = 0; i < TOMOYO_MAX_GROUP; i++) {
		struct list_head *list = &tomoyo_group_list[i];
		int id;
		struct tomoyo_group *group;
		switch (i) {
		case 0:
			id = TOMOYO_ID_PATH_GROUP;
			break;
		default:
			id = TOMOYO_ID_NUMBER_GROUP;
			break;
		}
		list_for_each_entry(group, list, head.list) {
			if (!tomoyo_collect_member(id, &group->member_list))
				goto unlock;
			if (!list_empty(&group->member_list) ||
			    atomic_read(&group->head.users))
				continue;
			if (!tomoyo_add_to_gc(TOMOYO_ID_GROUP,
					      &group->head.list))
				goto unlock;
		}
	}
 unlock:
	mutex_unlock(&tomoyo_policy_lock);
}

static void tomoyo_kfree_entry(void)
{
	struct tomoyo_gc *p;
	struct tomoyo_gc *tmp;

	list_for_each_entry_safe(p, tmp, &tomoyo_gc_queue, list) {
		struct list_head *element = p->element;
		switch (p->type) {
		case TOMOYO_ID_TRANSITION_CONTROL:
			tomoyo_del_transition_control(element);
			break;
		case TOMOYO_ID_AGGREGATOR:
			tomoyo_del_aggregator(element);
			break;
		case TOMOYO_ID_MANAGER:
			tomoyo_del_manager(element);
			break;
		case TOMOYO_ID_NAME:
			tomoyo_del_name(element);
			break;
		case TOMOYO_ID_ACL:
			tomoyo_del_acl(element);
			break;
		case TOMOYO_ID_DOMAIN:
			if (!tomoyo_del_domain(element))
				continue;
			break;
		case TOMOYO_ID_PATH_GROUP:
			tomoyo_del_path_group(element);
			break;
		case TOMOYO_ID_GROUP:
			tomoyo_del_group(element);
			break;
		case TOMOYO_ID_NUMBER_GROUP:
			tomoyo_del_number_group(element);
			break;
		case TOMOYO_MAX_POLICY:
			break;
		}
		tomoyo_memory_free(element);
		list_del(&p->list);
		kfree(p);
	}
}

/**
 * tomoyo_gc_thread - Garbage collector thread function.
 *
 * @unused: Unused.
 *
 * In case OOM-killer choose this thread for termination, we create this thread
 * as a short live thread whenever /sys/kernel/security/tomoyo/ interface was
 * close()d.
 *
 * Returns 0.
 */
static int tomoyo_gc_thread(void *unused)
{
	daemonize("GC for TOMOYO");
	if (mutex_trylock(&tomoyo_gc_mutex)) {
		int i;
		for (i = 0; i < 10; i++) {
			tomoyo_collect_entry();
			if (list_empty(&tomoyo_gc_queue))
				break;
			synchronize_srcu(&tomoyo_ss);
			tomoyo_kfree_entry();
		}
		mutex_unlock(&tomoyo_gc_mutex);
	}
	do_exit(0);
}

void tomoyo_run_gc(void)
{
	struct task_struct *task = kthread_create(tomoyo_gc_thread, NULL,
						  "GC for TOMOYO");
	if (!IS_ERR(task))
		wake_up_process(task);
}
