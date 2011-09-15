/*
 * security/tomoyo/gc.c
 *
 * Copyright (C) 2005-2011  NTT DATA CORPORATION
 */

#include "common.h"
#include <linux/kthread.h>
#include <linux/slab.h>

/* The list for "struct tomoyo_io_buffer". */
static LIST_HEAD(tomoyo_io_buffer_list);
/* Lock for protecting tomoyo_io_buffer_list. */
static DEFINE_SPINLOCK(tomoyo_io_buffer_list_lock);

/* Size of an element. */
static const u8 tomoyo_element_size[TOMOYO_MAX_POLICY] = {
	[TOMOYO_ID_GROUP] = sizeof(struct tomoyo_group),
	[TOMOYO_ID_PATH_GROUP] = sizeof(struct tomoyo_path_group),
	[TOMOYO_ID_NUMBER_GROUP] = sizeof(struct tomoyo_number_group),
	[TOMOYO_ID_AGGREGATOR] = sizeof(struct tomoyo_aggregator),
	[TOMOYO_ID_TRANSITION_CONTROL] =
	sizeof(struct tomoyo_transition_control),
	[TOMOYO_ID_MANAGER] = sizeof(struct tomoyo_manager),
	/* [TOMOYO_ID_CONDITION] = "struct tomoyo_condition"->size, */
	/* [TOMOYO_ID_NAME] = "struct tomoyo_name"->size, */
	/* [TOMOYO_ID_ACL] =
	   tomoyo_acl_size["struct tomoyo_acl_info"->type], */
	[TOMOYO_ID_DOMAIN] = sizeof(struct tomoyo_domain_info),
};

/* Size of a domain ACL element. */
static const u8 tomoyo_acl_size[] = {
	[TOMOYO_TYPE_PATH_ACL] = sizeof(struct tomoyo_path_acl),
	[TOMOYO_TYPE_PATH2_ACL] = sizeof(struct tomoyo_path2_acl),
	[TOMOYO_TYPE_PATH_NUMBER_ACL] = sizeof(struct tomoyo_path_number_acl),
	[TOMOYO_TYPE_MKDEV_ACL] = sizeof(struct tomoyo_mkdev_acl),
	[TOMOYO_TYPE_MOUNT_ACL] = sizeof(struct tomoyo_mount_acl),
};

/**
 * tomoyo_struct_used_by_io_buffer - Check whether the list element is used by /sys/kernel/security/tomoyo/ users or not.
 *
 * @element: Pointer to "struct list_head".
 *
 * Returns true if @element is used by /sys/kernel/security/tomoyo/ users,
 * false otherwise.
 */
static bool tomoyo_struct_used_by_io_buffer(const struct list_head *element)
{
	struct tomoyo_io_buffer *head;
	bool in_use = false;

	spin_lock(&tomoyo_io_buffer_list_lock);
	list_for_each_entry(head, &tomoyo_io_buffer_list, list) {
		head->users++;
		spin_unlock(&tomoyo_io_buffer_list_lock);
		if (mutex_lock_interruptible(&head->io_sem)) {
			in_use = true;
			goto out;
		}
		if (head->r.domain == element || head->r.group == element ||
		    head->r.acl == element || &head->w.domain->list == element)
			in_use = true;
		mutex_unlock(&head->io_sem);
out:
		spin_lock(&tomoyo_io_buffer_list_lock);
		head->users--;
		if (in_use)
			break;
	}
	spin_unlock(&tomoyo_io_buffer_list_lock);
	return in_use;
}

/**
 * tomoyo_name_used_by_io_buffer - Check whether the string is used by /sys/kernel/security/tomoyo/ users or not.
 *
 * @string: String to check.
 * @size:   Memory allocated for @string .
 *
 * Returns true if @string is used by /sys/kernel/security/tomoyo/ users,
 * false otherwise.
 */
static bool tomoyo_name_used_by_io_buffer(const char *string,
					  const size_t size)
{
	struct tomoyo_io_buffer *head;
	bool in_use = false;

	spin_lock(&tomoyo_io_buffer_list_lock);
	list_for_each_entry(head, &tomoyo_io_buffer_list, list) {
		int i;
		head->users++;
		spin_unlock(&tomoyo_io_buffer_list_lock);
		if (mutex_lock_interruptible(&head->io_sem)) {
			in_use = true;
			goto out;
		}
		for (i = 0; i < TOMOYO_MAX_IO_READ_QUEUE; i++) {
			const char *w = head->r.w[i];
			if (w < string || w > string + size)
				continue;
			in_use = true;
			break;
		}
		mutex_unlock(&head->io_sem);
out:
		spin_lock(&tomoyo_io_buffer_list_lock);
		head->users--;
		if (in_use)
			break;
	}
	spin_unlock(&tomoyo_io_buffer_list_lock);
	return in_use;
}

/* Structure for garbage collection. */
struct tomoyo_gc {
	struct list_head list;
	enum tomoyo_policy_id type;
	size_t size;
	struct list_head *element;
};
/* List of entries to be deleted. */
static LIST_HEAD(tomoyo_gc_list);
/* Length of tomoyo_gc_list. */
static int tomoyo_gc_list_len;

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
	if (type == TOMOYO_ID_ACL)
		entry->size = tomoyo_acl_size[
			      container_of(element,
					   typeof(struct tomoyo_acl_info),
					   list)->type];
	else if (type == TOMOYO_ID_NAME)
		entry->size = strlen(container_of(element,
						  typeof(struct tomoyo_name),
						  head.list)->entry.name) + 1;
	else if (type == TOMOYO_ID_CONDITION)
		entry->size =
			container_of(element, typeof(struct tomoyo_condition),
				     head.list)->size;
	else
		entry->size = tomoyo_element_size[type];
	entry->element = element;
	list_add(&entry->list, &tomoyo_gc_list);
	list_del_rcu(element);
	return tomoyo_gc_list_len++ < 128;
}

/**
 * tomoyo_element_linked_by_gc - Validate next element of an entry.
 *
 * @element: Pointer to an element.
 * @size:    Size of @element in byte.
 *
 * Returns true if @element is linked by other elements in the garbage
 * collector's queue, false otherwise.
 */
static bool tomoyo_element_linked_by_gc(const u8 *element, const size_t size)
{
	struct tomoyo_gc *p;
	list_for_each_entry(p, &tomoyo_gc_list, list) {
		const u8 *ptr = (const u8 *) p->element->next;
		if (ptr < element || element + size < ptr)
			continue;
		return true;
	}
	return false;
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
	tomoyo_put_condition(acl->cond);
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

/**
 * tomoyo_del_domain - Delete members in "struct tomoyo_domain_info".
 *
 * @element: Pointer to "struct list_head".
 *
 * Returns true if deleted, false otherwise.
 */
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
 * tomoyo_del_condition - Delete members in "struct tomoyo_condition".
 *
 * @element: Pointer to "struct list_head".
 *
 * Returns nothing.
 */
void tomoyo_del_condition(struct list_head *element)
{
	struct tomoyo_condition *cond = container_of(element, typeof(*cond),
						     head.list);
	const u16 condc = cond->condc;
	const u16 numbers_count = cond->numbers_count;
	const u16 names_count = cond->names_count;
	const u16 argc = cond->argc;
	const u16 envc = cond->envc;
	unsigned int i;
	const struct tomoyo_condition_element *condp
		= (const struct tomoyo_condition_element *) (cond + 1);
	struct tomoyo_number_union *numbers_p
		= (struct tomoyo_number_union *) (condp + condc);
	struct tomoyo_name_union *names_p
		= (struct tomoyo_name_union *) (numbers_p + numbers_count);
	const struct tomoyo_argv *argv
		= (const struct tomoyo_argv *) (names_p + names_count);
	const struct tomoyo_envp *envp
		= (const struct tomoyo_envp *) (argv + argc);
	for (i = 0; i < numbers_count; i++)
		tomoyo_put_number_union(numbers_p++);
	for (i = 0; i < names_count; i++)
		tomoyo_put_name_union(names_p++);
	for (i = 0; i < argc; argv++, i++)
		tomoyo_put_name(argv->value);
	for (i = 0; i < envc; envp++, i++) {
		tomoyo_put_name(envp->name);
		tomoyo_put_name(envp->value);
	}
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

/**
 * tomoyo_collect_acl - Delete elements in "struct tomoyo_domain_info".
 *
 * @list: Pointer to "struct list_head".
 *
 * Returns true if some elements are deleted, false otherwise.
 */
static bool tomoyo_collect_acl(struct list_head *list)
{
	struct tomoyo_acl_info *acl;
	list_for_each_entry(acl, list, list) {
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
	enum tomoyo_policy_id id;
	struct tomoyo_policy_namespace *ns;
	int idx;
	if (mutex_lock_interruptible(&tomoyo_policy_lock))
		return;
	idx = tomoyo_read_lock();
	{
		struct tomoyo_domain_info *domain;
		list_for_each_entry_rcu(domain, &tomoyo_domain_list, list) {
			if (!tomoyo_collect_acl(&domain->acl_info_list))
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
	list_for_each_entry_rcu(ns, &tomoyo_namespace_list, namespace_list) {
		for (id = 0; id < TOMOYO_MAX_POLICY; id++)
			if (!tomoyo_collect_member(id, &ns->policy_list[id]))
				goto unlock;
		for (i = 0; i < TOMOYO_MAX_ACL_GROUPS; i++)
			if (!tomoyo_collect_acl(&ns->acl_group[i]))
				goto unlock;
		for (i = 0; i < TOMOYO_MAX_GROUP; i++) {
			struct list_head *list = &ns->group_list[i];
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
				if (!tomoyo_collect_member
				    (id, &group->member_list))
					goto unlock;
				if (!list_empty(&group->member_list) ||
				    atomic_read(&group->head.users))
					continue;
				if (!tomoyo_add_to_gc(TOMOYO_ID_GROUP,
						      &group->head.list))
					goto unlock;
			}
		}
	}
	id = TOMOYO_ID_CONDITION;
	for (i = 0; i < TOMOYO_MAX_HASH + 1; i++) {
		struct list_head *list = !i ?
			&tomoyo_condition_list : &tomoyo_name_list[i - 1];
		struct tomoyo_shared_acl_head *ptr;
		list_for_each_entry(ptr, list, list) {
			if (atomic_read(&ptr->users))
				continue;
			if (!tomoyo_add_to_gc(id, &ptr->list))
				goto unlock;
		}
		id = TOMOYO_ID_NAME;
	}
unlock:
	tomoyo_read_unlock(idx);
	mutex_unlock(&tomoyo_policy_lock);
}

/**
 * tomoyo_kfree_entry - Delete entries in tomoyo_gc_list.
 *
 * Returns true if some entries were kfree()d, false otherwise.
 */
static bool tomoyo_kfree_entry(void)
{
	struct tomoyo_gc *p;
	struct tomoyo_gc *tmp;
	bool result = false;

	list_for_each_entry_safe(p, tmp, &tomoyo_gc_list, list) {
		struct list_head *element = p->element;

		/*
		 * list_del_rcu() in tomoyo_add_to_gc() guarantees that the
		 * list element became no longer reachable from the list which
		 * the element was originally on (e.g. tomoyo_domain_list).
		 * Also, synchronize_srcu() in tomoyo_gc_thread() guarantees
		 * that the list element became no longer referenced by syscall
		 * users.
		 *
		 * However, there are three users which may still be using the
		 * list element. We need to defer until all of these users
		 * forget the list element.
		 *
		 * Firstly, defer until "struct tomoyo_io_buffer"->r.{domain,
		 * group,acl} and "struct tomoyo_io_buffer"->w.domain forget
		 * the list element.
		 */
		if (tomoyo_struct_used_by_io_buffer(element))
			continue;
		/*
		 * Secondly, defer until all other elements in the
		 * tomoyo_gc_list list forget the list element.
		 */
		if (tomoyo_element_linked_by_gc((const u8 *) element, p->size))
			continue;
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
		case TOMOYO_ID_CONDITION:
			tomoyo_del_condition(element);
			break;
		case TOMOYO_ID_NAME:
			/*
			 * Thirdly, defer until all "struct tomoyo_io_buffer"
			 * ->r.w[] forget the list element.
			 */
			if (tomoyo_name_used_by_io_buffer(
			    container_of(element, typeof(struct tomoyo_name),
					 head.list)->entry.name, p->size))
				continue;
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
		tomoyo_gc_list_len--;
		result = true;
	}
	return result;
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
	/* Garbage collector thread is exclusive. */
	static DEFINE_MUTEX(tomoyo_gc_mutex);
	if (!mutex_trylock(&tomoyo_gc_mutex))
		goto out;
	daemonize("GC for TOMOYO");
	do {
		tomoyo_collect_entry();
		if (list_empty(&tomoyo_gc_list))
			break;
		synchronize_srcu(&tomoyo_ss);
	} while (tomoyo_kfree_entry());
	{
		struct tomoyo_io_buffer *head;
		struct tomoyo_io_buffer *tmp;

		spin_lock(&tomoyo_io_buffer_list_lock);
		list_for_each_entry_safe(head, tmp, &tomoyo_io_buffer_list,
					 list) {
			if (head->users)
				continue;
			list_del(&head->list);
			kfree(head->read_buf);
			kfree(head->write_buf);
			kfree(head);
		}
		spin_unlock(&tomoyo_io_buffer_list_lock);
	}
	mutex_unlock(&tomoyo_gc_mutex);
out:
	/* This acts as do_exit(0). */
	return 0;
}

/**
 * tomoyo_notify_gc - Register/unregister /sys/kernel/security/tomoyo/ users.
 *
 * @head:        Pointer to "struct tomoyo_io_buffer".
 * @is_register: True if register, false if unregister.
 *
 * Returns nothing.
 */
void tomoyo_notify_gc(struct tomoyo_io_buffer *head, const bool is_register)
{
	bool is_write = false;

	spin_lock(&tomoyo_io_buffer_list_lock);
	if (is_register) {
		head->users = 1;
		list_add(&head->list, &tomoyo_io_buffer_list);
	} else {
		is_write = head->write_buf != NULL;
		if (!--head->users) {
			list_del(&head->list);
			kfree(head->read_buf);
			kfree(head->write_buf);
			kfree(head);
		}
	}
	spin_unlock(&tomoyo_io_buffer_list_lock);
	if (is_write) {
		struct task_struct *task = kthread_create(tomoyo_gc_thread,
							  NULL,
							  "GC for TOMOYO");
		if (!IS_ERR(task))
			wake_up_process(task);
	}
}
