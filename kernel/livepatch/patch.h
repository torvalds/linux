/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LIVEPATCH_PATCH_H
#define _LIVEPATCH_PATCH_H

#include <linux/livepatch.h>
#include <linux/list.h>
#include <linux/ftrace.h>

/**
 * struct klp_ops - structure for tracking registered ftrace ops structs
 *
 * A single ftrace_ops is shared between all enabled replacement functions
 * (klp_func structs) which have the same old_func.  This allows the switch
 * between function versions to happen instantaneously by updating the klp_ops
 * struct's func_stack list.  The winner is the klp_func at the top of the
 * func_stack (front of the list).
 *
 * @node:	node for the global klp_ops list
 * @func_stack:	list head for the stack of klp_func's (active func is on top)
 * @fops:	registered ftrace ops struct
 */
struct klp_ops {
	struct list_head node;
	struct list_head func_stack;
	struct ftrace_ops fops;
};

struct klp_ops *klp_find_ops(void *old_func);

int klp_patch_object(struct klp_object *obj);
void klp_unpatch_object(struct klp_object *obj);
void klp_unpatch_objects(struct klp_patch *patch);
void klp_unpatch_objects_dynamic(struct klp_patch *patch);

#endif /* _LIVEPATCH_PATCH_H */
