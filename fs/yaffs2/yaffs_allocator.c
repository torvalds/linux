/*
 * YAFFS: Yet Another Flash File System. A NAND-flash specific file system.
 *
 * Copyright (C) 2002-2010 Aleph One Ltd.
 *   for Toby Churchill Ltd and Brightstar Engineering
 *
 * Created by Charles Manning <charles@aleph1.co.uk>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "yaffs_allocator.h"
#include "yaffs_guts.h"
#include "yaffs_trace.h"
#include "yportenv.h"

#ifdef CONFIG_YAFFS_KMALLOC_ALLOCATOR

void yaffs_deinit_raw_tnodes_and_objs(struct yaffs_dev *dev)
{
	dev = dev;
}

void yaffs_init_raw_tnodes_and_objs(struct yaffs_dev *dev)
{
	dev = dev;
}

struct yaffs_tnode *yaffs_alloc_raw_tnode(struct yaffs_dev *dev)
{
	return (struct yaffs_tnode *)kmalloc(dev->tnode_size, GFP_NOFS);
}

void yaffs_free_raw_tnode(struct yaffs_dev *dev, struct yaffs_tnode *tn)
{
	dev = dev;
	kfree(tn);
}

void yaffs_init_raw_objs(struct yaffs_dev *dev)
{
	dev = dev;
}

void yaffs_deinit_raw_objs(struct yaffs_dev *dev)
{
	dev = dev;
}

struct yaffs_obj *yaffs_alloc_raw_obj(struct yaffs_dev *dev)
{
	dev = dev;
	return (struct yaffs_obj *)kmalloc(sizeof(struct yaffs_obj));
}

void yaffs_free_raw_obj(struct yaffs_dev *dev, struct yaffs_obj *obj)
{

	dev = dev;
	kfree(obj);
}

#else

struct yaffs_tnode_list {
	struct yaffs_tnode_list *next;
	struct yaffs_tnode *tnodes;
};

struct yaffs_obj_list {
	struct yaffs_obj_list *next;
	struct yaffs_obj *objects;
};

struct yaffs_allocator {
	int n_tnodes_created;
	struct yaffs_tnode *free_tnodes;
	int n_free_tnodes;
	struct yaffs_tnode_list *alloc_tnode_list;

	int n_obj_created;
	struct yaffs_obj *free_objs;
	int n_free_objects;

	struct yaffs_obj_list *allocated_obj_list;
};

static void yaffs_deinit_raw_tnodes(struct yaffs_dev *dev)
{

	struct yaffs_allocator *allocator =
	    (struct yaffs_allocator *)dev->allocator;

	struct yaffs_tnode_list *tmp;

	if (!allocator) {
		YBUG();
		return;
	}

	while (allocator->alloc_tnode_list) {
		tmp = allocator->alloc_tnode_list->next;

		kfree(allocator->alloc_tnode_list->tnodes);
		kfree(allocator->alloc_tnode_list);
		allocator->alloc_tnode_list = tmp;

	}

	allocator->free_tnodes = NULL;
	allocator->n_free_tnodes = 0;
	allocator->n_tnodes_created = 0;
}

static void yaffs_init_raw_tnodes(struct yaffs_dev *dev)
{
	struct yaffs_allocator *allocator = dev->allocator;

	if (allocator) {
		allocator->alloc_tnode_list = NULL;
		allocator->free_tnodes = NULL;
		allocator->n_free_tnodes = 0;
		allocator->n_tnodes_created = 0;
	} else {
		YBUG();
	}
}

static int yaffs_create_tnodes(struct yaffs_dev *dev, int n_tnodes)
{
	struct yaffs_allocator *allocator =
	    (struct yaffs_allocator *)dev->allocator;
	int i;
	struct yaffs_tnode *new_tnodes;
	u8 *mem;
	struct yaffs_tnode *curr;
	struct yaffs_tnode *next;
	struct yaffs_tnode_list *tnl;

	if (!allocator) {
		YBUG();
		return YAFFS_FAIL;
	}

	if (n_tnodes < 1)
		return YAFFS_OK;

	/* make these things */

	new_tnodes = kmalloc(n_tnodes * dev->tnode_size, GFP_NOFS);
	mem = (u8 *) new_tnodes;

	if (!new_tnodes) {
		yaffs_trace(YAFFS_TRACE_ERROR,
			"yaffs: Could not allocate Tnodes");
		return YAFFS_FAIL;
	}

	/* New hookup for wide tnodes */
	for (i = 0; i < n_tnodes - 1; i++) {
		curr = (struct yaffs_tnode *)&mem[i * dev->tnode_size];
		next = (struct yaffs_tnode *)&mem[(i + 1) * dev->tnode_size];
		curr->internal[0] = next;
	}

	curr = (struct yaffs_tnode *)&mem[(n_tnodes - 1) * dev->tnode_size];
	curr->internal[0] = allocator->free_tnodes;
	allocator->free_tnodes = (struct yaffs_tnode *)mem;

	allocator->n_free_tnodes += n_tnodes;
	allocator->n_tnodes_created += n_tnodes;

	/* Now add this bunch of tnodes to a list for freeing up.
	 * NB If we can't add this to the management list it isn't fatal
	 * but it just means we can't free this bunch of tnodes later.
	 */

	tnl = kmalloc(sizeof(struct yaffs_tnode_list), GFP_NOFS);
	if (!tnl) {
		yaffs_trace(YAFFS_TRACE_ERROR,
			"Could not add tnodes to management list");
		return YAFFS_FAIL;
	} else {
		tnl->tnodes = new_tnodes;
		tnl->next = allocator->alloc_tnode_list;
		allocator->alloc_tnode_list = tnl;
	}

	yaffs_trace(YAFFS_TRACE_ALLOCATE,"Tnodes added");

	return YAFFS_OK;
}

struct yaffs_tnode *yaffs_alloc_raw_tnode(struct yaffs_dev *dev)
{
	struct yaffs_allocator *allocator =
	    (struct yaffs_allocator *)dev->allocator;
	struct yaffs_tnode *tn = NULL;

	if (!allocator) {
		YBUG();
		return NULL;
	}

	/* If there are none left make more */
	if (!allocator->free_tnodes)
		yaffs_create_tnodes(dev, YAFFS_ALLOCATION_NTNODES);

	if (allocator->free_tnodes) {
		tn = allocator->free_tnodes;
		allocator->free_tnodes = allocator->free_tnodes->internal[0];
		allocator->n_free_tnodes--;
	}

	return tn;
}

/* FreeTnode frees up a tnode and puts it back on the free list */
void yaffs_free_raw_tnode(struct yaffs_dev *dev, struct yaffs_tnode *tn)
{
	struct yaffs_allocator *allocator = dev->allocator;

	if (!allocator) {
		YBUG();
		return;
	}

	if (tn) {
		tn->internal[0] = allocator->free_tnodes;
		allocator->free_tnodes = tn;
		allocator->n_free_tnodes++;
	}
	dev->checkpoint_blocks_required = 0;	/* force recalculation */
}

static void yaffs_init_raw_objs(struct yaffs_dev *dev)
{
	struct yaffs_allocator *allocator = dev->allocator;

	if (allocator) {
		allocator->allocated_obj_list = NULL;
		allocator->free_objs = NULL;
		allocator->n_free_objects = 0;
	} else {
		YBUG();
	}
}

static void yaffs_deinit_raw_objs(struct yaffs_dev *dev)
{
	struct yaffs_allocator *allocator = dev->allocator;
	struct yaffs_obj_list *tmp;

	if (!allocator) {
		YBUG();
		return;
	}

	while (allocator->allocated_obj_list) {
		tmp = allocator->allocated_obj_list->next;
		kfree(allocator->allocated_obj_list->objects);
		kfree(allocator->allocated_obj_list);

		allocator->allocated_obj_list = tmp;
	}

	allocator->free_objs = NULL;
	allocator->n_free_objects = 0;
	allocator->n_obj_created = 0;
}

static int yaffs_create_free_objs(struct yaffs_dev *dev, int n_obj)
{
	struct yaffs_allocator *allocator = dev->allocator;

	int i;
	struct yaffs_obj *new_objs;
	struct yaffs_obj_list *list;

	if (!allocator) {
		YBUG();
		return YAFFS_FAIL;
	}

	if (n_obj < 1)
		return YAFFS_OK;

	/* make these things */
	new_objs = kmalloc(n_obj * sizeof(struct yaffs_obj), GFP_NOFS);
	list = kmalloc(sizeof(struct yaffs_obj_list), GFP_NOFS);

	if (!new_objs || !list) {
		if (new_objs) {
			kfree(new_objs);
			new_objs = NULL;
		}
		if (list) {
			kfree(list);
			list = NULL;
		}
		yaffs_trace(YAFFS_TRACE_ALLOCATE,
			"Could not allocate more objects");
		return YAFFS_FAIL;
	}

	/* Hook them into the free list */
	for (i = 0; i < n_obj - 1; i++) {
		new_objs[i].siblings.next =
		    (struct list_head *)(&new_objs[i + 1]);
	}

	new_objs[n_obj - 1].siblings.next = (void *)allocator->free_objs;
	allocator->free_objs = new_objs;
	allocator->n_free_objects += n_obj;
	allocator->n_obj_created += n_obj;

	/* Now add this bunch of Objects to a list for freeing up. */

	list->objects = new_objs;
	list->next = allocator->allocated_obj_list;
	allocator->allocated_obj_list = list;

	return YAFFS_OK;
}

struct yaffs_obj *yaffs_alloc_raw_obj(struct yaffs_dev *dev)
{
	struct yaffs_obj *obj = NULL;
	struct yaffs_allocator *allocator = dev->allocator;

	if (!allocator) {
		YBUG();
		return obj;
	}

	/* If there are none left make more */
	if (!allocator->free_objs)
		yaffs_create_free_objs(dev, YAFFS_ALLOCATION_NOBJECTS);

	if (allocator->free_objs) {
		obj = allocator->free_objs;
		allocator->free_objs =
		    (struct yaffs_obj *)(allocator->free_objs->siblings.next);
		allocator->n_free_objects--;
	}

	return obj;
}

void yaffs_free_raw_obj(struct yaffs_dev *dev, struct yaffs_obj *obj)
{

	struct yaffs_allocator *allocator = dev->allocator;

	if (!allocator)
		YBUG();
	else {
		/* Link into the free list. */
		obj->siblings.next = (struct list_head *)(allocator->free_objs);
		allocator->free_objs = obj;
		allocator->n_free_objects++;
	}
}

void yaffs_deinit_raw_tnodes_and_objs(struct yaffs_dev *dev)
{
	if (dev->allocator) {
		yaffs_deinit_raw_tnodes(dev);
		yaffs_deinit_raw_objs(dev);

		kfree(dev->allocator);
		dev->allocator = NULL;
	} else {
		YBUG();
	}
}

void yaffs_init_raw_tnodes_and_objs(struct yaffs_dev *dev)
{
	struct yaffs_allocator *allocator;

	if (!dev->allocator) {
		allocator = kmalloc(sizeof(struct yaffs_allocator), GFP_NOFS);
		if (allocator) {
			dev->allocator = allocator;
			yaffs_init_raw_tnodes(dev);
			yaffs_init_raw_objs(dev);
		}
	} else {
		YBUG();
	}
}

#endif
