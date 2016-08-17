/*****************************************************************************\
 *  Copyright (C) 2007-2010 Lawrence Livermore National Security, LLC.
 *  Copyright (C) 2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Brian Behlendorf <behlendorf1@llnl.gov>.
 *  UCRL-CODE-235197
 *
 *  This file is part of the SPL, Solaris Porting Layer.
 *  For details, see <http://zfsonlinux.org/>.
 *
 *  The SPL is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  The SPL is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with the SPL.  If not, see <http://www.gnu.org/licenses/>.
 *****************************************************************************
 *  Solaris Porting LAyer Tests (SPLAT) List Tests.
\*****************************************************************************/

#include <sys/list.h>
#include <sys/kmem.h>
#include "splat-internal.h"

#define SPLAT_LIST_NAME			"list"
#define SPLAT_LIST_DESC			"Kernel List Tests"

#define SPLAT_LIST_TEST1_ID		0x0c01
#define SPLAT_LIST_TEST1_NAME		"create/destroy"
#define SPLAT_LIST_TEST1_DESC		"Create/destroy Test"

#define SPLAT_LIST_TEST2_ID		0x0c02
#define SPLAT_LIST_TEST2_NAME		"ins/rm head"
#define SPLAT_LIST_TEST2_DESC		"Insert/remove head Test"

#define SPLAT_LIST_TEST3_ID		0x0c03
#define SPLAT_LIST_TEST3_NAME		"ins/rm tail"
#define SPLAT_LIST_TEST3_DESC		"Insert/remove tail Test"

#define SPLAT_LIST_TEST4_ID		0x0c04
#define SPLAT_LIST_TEST4_NAME		"insert_after"
#define SPLAT_LIST_TEST4_DESC		"Insert_after Test"

#define SPLAT_LIST_TEST5_ID		0x0c05
#define SPLAT_LIST_TEST5_NAME		"insert_before"
#define SPLAT_LIST_TEST5_DESC		"Insert_before Test"

#define SPLAT_LIST_TEST6_ID		0x0c06
#define SPLAT_LIST_TEST6_NAME		"remove"
#define SPLAT_LIST_TEST6_DESC		"Remove Test"

#define SPLAT_LIST_TEST7_ID		0x0c7
#define SPLAT_LIST_TEST7_NAME		"active"
#define SPLAT_LIST_TEST7_DESC		"Active Test"

/* It is important that li_node is not the first element, this
 * ensures the list_d2l/list_object macros are working correctly. */
typedef struct list_item {
	int li_data;
	list_node_t li_node;
} list_item_t;

#define LIST_ORDER_STACK		0
#define LIST_ORDER_QUEUE		1

static int
splat_list_test1(struct file *file, void *arg)
{
	list_t list;

	splat_vprint(file, SPLAT_LIST_TEST1_NAME, "Creating list\n%s", "");
	list_create(&list, sizeof(list_item_t), offsetof(list_item_t, li_node));

	if (!list_is_empty(&list)) {
		splat_vprint(file, SPLAT_LIST_TEST1_NAME,
			     "New list NOT empty%s\n", "");
		/* list_destroy() intentionally skipped to avoid assert */
		return -EEXIST;
	}

	splat_vprint(file, SPLAT_LIST_TEST1_NAME, "Destroying list\n%s", "");
	list_destroy(&list);

	/* Validate the list has been destroyed */
	if (list_link_active(&list.list_head)) {
		splat_vprint(file, SPLAT_LIST_TEST1_NAME,
			     "Destroyed list still active%s", "");
		return -EIO;
	}

        return 0;
}

static int
splat_list_validate(list_t *list, int size, int order, int mult)
{
	list_item_t *li;
	int i;

	/* Walk all items in list from head to verify stack or queue
	 * ordering.  We bound the for loop by size+1 to ensure that
	 * we still terminate if there is list corruption.  We also
	 * intentionally make things a little more complex than they
	 * need to be by using list_head/list_next for queues, and
	 * list_tail/list_prev for stacks.  This is simply done for
	 * coverage and to ensure these function are working right.
	 */
	for (i = 0, li = (order ? list_head(list) : list_tail(list));
	     i < size + 1 && li != NULL;
	     i++, li = (order ? list_next(list, li) : list_prev(list, li)))
		if (li->li_data != i * mult)
			return -EIDRM;

	if (i != size)
		return -E2BIG;

	return 0;
}

static int
splat_list_test2(struct file *file, void *arg)
{
	list_t list;
	list_item_t *li;
	int i, list_size = 8, rc = 0;

	splat_vprint(file, SPLAT_LIST_TEST2_NAME, "Creating list\n%s", "");
	list_create(&list, sizeof(list_item_t), offsetof(list_item_t, li_node));

	/* Insert all items at the list head to form a stack */
	splat_vprint(file, SPLAT_LIST_TEST2_NAME,
		     "Adding %d items to list head\n", list_size);
	for (i = 0; i < list_size; i++) {
		li = kmem_alloc(sizeof(list_item_t), KM_SLEEP);
		if (li == NULL) {
			rc = -ENOMEM;
			goto out;
		}

		list_link_init(&li->li_node);
		li->li_data = i;
		list_insert_head(&list, li);
	}

	splat_vprint(file, SPLAT_LIST_TEST2_NAME,
		     "Validating %d item list is a stack\n", list_size);
	rc = splat_list_validate(&list, list_size, LIST_ORDER_STACK, 1);
	if (rc)
		splat_vprint(file, SPLAT_LIST_TEST2_NAME,
			     "List validation failed, %d\n", rc);
out:
	/* Remove all items */
	splat_vprint(file, SPLAT_LIST_TEST2_NAME,
		     "Removing %d items from list head\n", list_size);
	while ((li = list_remove_head(&list)))
		kmem_free(li, sizeof(list_item_t));

	splat_vprint(file, SPLAT_LIST_TEST2_NAME, "Destroying list\n%s", "");
	list_destroy(&list);

        return rc;
}

static int
splat_list_test3(struct file *file, void *arg)
{
	list_t list;
	list_item_t *li;
	int i, list_size = 8, rc = 0;

	splat_vprint(file, SPLAT_LIST_TEST3_NAME, "Creating list\n%s", "");
	list_create(&list, sizeof(list_item_t), offsetof(list_item_t, li_node));

	/* Insert all items at the list tail to form a queue */
	splat_vprint(file, SPLAT_LIST_TEST3_NAME,
		     "Adding %d items to list tail\n", list_size);
	for (i = 0; i < list_size; i++) {
		li = kmem_alloc(sizeof(list_item_t), KM_SLEEP);
		if (li == NULL) {
			rc = -ENOMEM;
			goto out;
		}

		list_link_init(&li->li_node);
		li->li_data = i;
		list_insert_tail(&list, li);
	}

	splat_vprint(file, SPLAT_LIST_TEST3_NAME,
		     "Validating %d item list is a queue\n", list_size);
	rc = splat_list_validate(&list, list_size, LIST_ORDER_QUEUE, 1);
	if (rc)
		splat_vprint(file, SPLAT_LIST_TEST3_NAME,
			     "List validation failed, %d\n", rc);
out:
	/* Remove all items */
	splat_vprint(file, SPLAT_LIST_TEST3_NAME,
		     "Removing %d items from list tail\n", list_size);
	while ((li = list_remove_tail(&list)))
		kmem_free(li, sizeof(list_item_t));

	splat_vprint(file, SPLAT_LIST_TEST3_NAME, "Destroying list\n%s", "");
	list_destroy(&list);

        return rc;
}

static int
splat_list_test4(struct file *file, void *arg)
{
	list_t list;
	list_item_t *li_new, *li_last = NULL;
	int i, list_size = 8, rc = 0;

	splat_vprint(file, SPLAT_LIST_TEST4_NAME, "Creating list\n%s", "");
	list_create(&list, sizeof(list_item_t), offsetof(list_item_t, li_node));

	/* Insert all items after the last item to form a queue */
	splat_vprint(file, SPLAT_LIST_TEST4_NAME,
		     "Adding %d items each after the last item\n", list_size);
	for (i = 0; i < list_size; i++) {
		li_new = kmem_alloc(sizeof(list_item_t), KM_SLEEP);
		if (li_new == NULL) {
			rc = -ENOMEM;
			goto out;
		}

		list_link_init(&li_new->li_node);
		li_new->li_data = i;
		list_insert_after(&list, li_last, li_new);
		li_last = li_new;
	}

	splat_vprint(file, SPLAT_LIST_TEST4_NAME,
		     "Validating %d item list is a queue\n", list_size);
	rc = splat_list_validate(&list, list_size, LIST_ORDER_QUEUE, 1);
	if (rc)
		splat_vprint(file, SPLAT_LIST_TEST4_NAME,
			     "List validation failed, %d\n", rc);
out:
	/* Remove all items */
	splat_vprint(file, SPLAT_LIST_TEST4_NAME,
		     "Removing %d items from list tail\n", list_size);
	while ((li_new = list_remove_head(&list)))
		kmem_free(li_new, sizeof(list_item_t));

	splat_vprint(file, SPLAT_LIST_TEST4_NAME, "Destroying list\n%s", "");
	list_destroy(&list);

        return rc;
}

static int
splat_list_test5(struct file *file, void *arg)
{
	list_t list;
	list_item_t *li_new, *li_last = NULL;
	int i, list_size = 8, rc = 0;

	splat_vprint(file, SPLAT_LIST_TEST5_NAME, "Creating list\n%s", "");
	list_create(&list, sizeof(list_item_t), offsetof(list_item_t, li_node));

	/* Insert all items before the last item to form a stack */
	splat_vprint(file, SPLAT_LIST_TEST5_NAME,
		     "Adding %d items each before the last item\n", list_size);
	for (i = 0; i < list_size; i++) {
		li_new = kmem_alloc(sizeof(list_item_t), KM_SLEEP);
		if (li_new == NULL) {
			rc = -ENOMEM;
			goto out;
		}

		list_link_init(&li_new->li_node);
		li_new->li_data = i;
		list_insert_before(&list, li_last, li_new);
		li_last = li_new;
	}

	splat_vprint(file, SPLAT_LIST_TEST5_NAME,
		     "Validating %d item list is a queue\n", list_size);
	rc = splat_list_validate(&list, list_size, LIST_ORDER_STACK, 1);
	if (rc)
		splat_vprint(file, SPLAT_LIST_TEST5_NAME,
			     "List validation failed, %d\n", rc);
out:
	/* Remove all items */
	splat_vprint(file, SPLAT_LIST_TEST5_NAME,
		     "Removing %d items from list tail\n", list_size);
	while ((li_new = list_remove_tail(&list)))
		kmem_free(li_new, sizeof(list_item_t));

	splat_vprint(file, SPLAT_LIST_TEST5_NAME, "Destroying list\n%s", "");
	list_destroy(&list);

        return rc;
}

static int
splat_list_test6(struct file *file, void *arg)
{
	list_t list;
	list_item_t *li, *li_prev;
	int i, list_size = 8, rc = 0;

	splat_vprint(file, SPLAT_LIST_TEST6_NAME, "Creating list\n%s", "");
	list_create(&list, sizeof(list_item_t), offsetof(list_item_t, li_node));

	/* Insert all items at the list tail to form a queue */
	splat_vprint(file, SPLAT_LIST_TEST6_NAME,
		     "Adding %d items to list tail\n", list_size);
	for (i = 0; i < list_size; i++) {
		li = kmem_alloc(sizeof(list_item_t), KM_SLEEP);
		if (li == NULL) {
			rc = -ENOMEM;
			goto out;
		}

		list_link_init(&li->li_node);
		li->li_data = i;
		list_insert_tail(&list, li);
	}

	/* Remove all odd items from the queue */
	splat_vprint(file, SPLAT_LIST_TEST6_NAME,
		     "Removing %d odd items from the list\n", list_size >> 1);
	for (li = list_head(&list); li != NULL; li = list_next(&list, li)) {
		if (li->li_data % 2 == 1) {
			li_prev = list_prev(&list, li);
			list_remove(&list, li);
			kmem_free(li, sizeof(list_item_t));
			li = li_prev;
		}
	}

	splat_vprint(file, SPLAT_LIST_TEST6_NAME, "Validating %d item "
		     "list is a queue of only even elements\n", list_size / 2);
	rc = splat_list_validate(&list, list_size / 2, LIST_ORDER_QUEUE, 2);
	if (rc)
		splat_vprint(file, SPLAT_LIST_TEST6_NAME,
			     "List validation failed, %d\n", rc);
out:
	/* Remove all items */
	splat_vprint(file, SPLAT_LIST_TEST6_NAME,
		     "Removing %d items from list tail\n", list_size / 2);
	while ((li = list_remove_tail(&list)))
		kmem_free(li, sizeof(list_item_t));

	splat_vprint(file, SPLAT_LIST_TEST6_NAME, "Destroying list\n%s", "");
	list_destroy(&list);

        return rc;
}

static int
splat_list_test7(struct file *file, void *arg)
{
	list_t list;
	list_item_t *li;
	int rc = 0;

	splat_vprint(file, SPLAT_LIST_TEST7_NAME, "Creating list\n%s", "");
	list_create(&list, sizeof(list_item_t), offsetof(list_item_t, li_node));

	li = kmem_alloc(sizeof(list_item_t), KM_SLEEP);
	if (li == NULL) {
		rc = -ENOMEM;
		goto out;
	}

	/* Validate newly initialized node is inactive */
	splat_vprint(file, SPLAT_LIST_TEST7_NAME, "Init list node\n%s", "");
	list_link_init(&li->li_node);
	if (list_link_active(&li->li_node)) {
		splat_vprint(file, SPLAT_LIST_TEST7_NAME, "Newly initialized "
			    "list node should inactive %p/%p\n",
			    li->li_node.prev, li->li_node.next);
		rc = -EINVAL;
		goto out_li;
	}

	/* Validate node is active when linked in to a list */
	splat_vprint(file, SPLAT_LIST_TEST7_NAME, "Insert list node\n%s", "");
	list_insert_head(&list, li);
	if (!list_link_active(&li->li_node)) {
		splat_vprint(file, SPLAT_LIST_TEST7_NAME, "List node "
			    "inserted in list should be active %p/%p\n",
			    li->li_node.prev, li->li_node.next);
		rc = -EINVAL;
		goto out;
	}

	/* Validate node is inactive when removed from list */
	splat_vprint(file, SPLAT_LIST_TEST7_NAME, "Remove list node\n%s", "");
	list_remove(&list, li);
	if (list_link_active(&li->li_node)) {
		splat_vprint(file, SPLAT_LIST_TEST7_NAME, "List node "
			    "removed from list should be inactive %p/%p\n",
			    li->li_node.prev, li->li_node.next);
		rc = -EINVAL;
	}
out_li:
	kmem_free(li, sizeof(list_item_t));
out:
	/* Remove all items */
	while ((li = list_remove_head(&list)))
		kmem_free(li, sizeof(list_item_t));

	splat_vprint(file, SPLAT_LIST_TEST7_NAME, "Destroying list\n%s", "");
	list_destroy(&list);

        return rc;
}

splat_subsystem_t *
splat_list_init(void)
{
        splat_subsystem_t *sub;

        sub = kmalloc(sizeof(*sub), GFP_KERNEL);
        if (sub == NULL)
                return NULL;

        memset(sub, 0, sizeof(*sub));
        strncpy(sub->desc.name, SPLAT_LIST_NAME, SPLAT_NAME_SIZE);
	strncpy(sub->desc.desc, SPLAT_LIST_DESC, SPLAT_DESC_SIZE);
        INIT_LIST_HEAD(&sub->subsystem_list);
	INIT_LIST_HEAD(&sub->test_list);
        spin_lock_init(&sub->test_lock);
        sub->desc.id = SPLAT_SUBSYSTEM_LIST;

        splat_test_init(sub, SPLAT_LIST_TEST1_NAME, SPLAT_LIST_TEST1_DESC,
	                SPLAT_LIST_TEST1_ID, splat_list_test1);
        splat_test_init(sub, SPLAT_LIST_TEST2_NAME, SPLAT_LIST_TEST2_DESC,
	                SPLAT_LIST_TEST2_ID, splat_list_test2);
        splat_test_init(sub, SPLAT_LIST_TEST3_NAME, SPLAT_LIST_TEST3_DESC,
	                SPLAT_LIST_TEST3_ID, splat_list_test3);
        splat_test_init(sub, SPLAT_LIST_TEST4_NAME, SPLAT_LIST_TEST4_DESC,
	                SPLAT_LIST_TEST4_ID, splat_list_test4);
        splat_test_init(sub, SPLAT_LIST_TEST5_NAME, SPLAT_LIST_TEST5_DESC,
	                SPLAT_LIST_TEST5_ID, splat_list_test5);
        splat_test_init(sub, SPLAT_LIST_TEST6_NAME, SPLAT_LIST_TEST6_DESC,
	                SPLAT_LIST_TEST6_ID, splat_list_test6);
        splat_test_init(sub, SPLAT_LIST_TEST7_NAME, SPLAT_LIST_TEST7_DESC,
	                SPLAT_LIST_TEST7_ID, splat_list_test7);

        return sub;
}

void
splat_list_fini(splat_subsystem_t *sub)
{
        ASSERT(sub);

        splat_test_fini(sub, SPLAT_LIST_TEST7_ID);
        splat_test_fini(sub, SPLAT_LIST_TEST6_ID);
        splat_test_fini(sub, SPLAT_LIST_TEST5_ID);
        splat_test_fini(sub, SPLAT_LIST_TEST4_ID);
        splat_test_fini(sub, SPLAT_LIST_TEST3_ID);
        splat_test_fini(sub, SPLAT_LIST_TEST2_ID);
        splat_test_fini(sub, SPLAT_LIST_TEST1_ID);

        kfree(sub);
}

int
splat_list_id(void)
{
        return SPLAT_SUBSYSTEM_LIST;
}
