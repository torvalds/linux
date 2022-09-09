/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * livepatch.h - Kernel Live Patching Core
 *
 * Copyright (C) 2014 Seth Jennings <sjenning@redhat.com>
 * Copyright (C) 2014 SUSE
 */

#ifndef _LINUX_LIVEPATCH_H_
#define _LINUX_LIVEPATCH_H_

#include <linux/module.h>
#include <linux/ftrace.h>
#include <linux/completion.h>
#include <linux/list.h>

#if IS_ENABLED(CONFIG_LIVEPATCH)

/* task patch states */
#define KLP_UNDEFINED	-1
#define KLP_UNPATCHED	 0
#define KLP_PATCHED	 1

/**
 * struct klp_func - function structure for live patching
 * @old_name:	name of the function to be patched
 * @new_func:	pointer to the patched function code
 * @old_sympos: a hint indicating which symbol position the old function
 *		can be found (optional)
 * @old_func:	pointer to the function being patched
 * @kobj:	kobject for sysfs resources
 * @node:	list node for klp_object func_list
 * @stack_node:	list node for klp_ops func_stack list
 * @old_size:	size of the old function
 * @new_size:	size of the new function
 * @nop:        temporary patch to use the original code again; dyn. allocated
 * @patched:	the func has been added to the klp_ops list
 * @transition:	the func is currently being applied or reverted
 *
 * The patched and transition variables define the func's patching state.  When
 * patching, a func is always in one of the following states:
 *
 *   patched=0 transition=0: unpatched
 *   patched=0 transition=1: unpatched, temporary starting state
 *   patched=1 transition=1: patched, may be visible to some tasks
 *   patched=1 transition=0: patched, visible to all tasks
 *
 * And when unpatching, it goes in the reverse order:
 *
 *   patched=1 transition=0: patched, visible to all tasks
 *   patched=1 transition=1: patched, may be visible to some tasks
 *   patched=0 transition=1: unpatched, temporary ending state
 *   patched=0 transition=0: unpatched
 */
struct klp_func {
	/* external */
	const char *old_name;
	void *new_func;
	/*
	 * The old_sympos field is optional and can be used to resolve
	 * duplicate symbol names in livepatch objects. If this field is zero,
	 * it is expected the symbol is unique, otherwise patching fails. If
	 * this value is greater than zero then that occurrence of the symbol
	 * in kallsyms for the given object is used.
	 */
	unsigned long old_sympos;

	/* internal */
	void *old_func;
	struct kobject kobj;
	struct list_head node;
	struct list_head stack_node;
	unsigned long old_size, new_size;
	bool nop;
	bool patched;
	bool transition;
};

struct klp_object;

/**
 * struct klp_callbacks - pre/post live-(un)patch callback structure
 * @pre_patch:		executed before code patching
 * @post_patch:		executed after code patching
 * @pre_unpatch:	executed before code unpatching
 * @post_unpatch:	executed after code unpatching
 * @post_unpatch_enabled:	flag indicating if post-unpatch callback
 * 				should run
 *
 * All callbacks are optional.  Only the pre-patch callback, if provided,
 * will be unconditionally executed.  If the parent klp_object fails to
 * patch for any reason, including a non-zero error status returned from
 * the pre-patch callback, no further callbacks will be executed.
 */
struct klp_callbacks {
	int (*pre_patch)(struct klp_object *obj);
	void (*post_patch)(struct klp_object *obj);
	void (*pre_unpatch)(struct klp_object *obj);
	void (*post_unpatch)(struct klp_object *obj);
	bool post_unpatch_enabled;
};

/**
 * struct klp_object - kernel object structure for live patching
 * @name:	module name (or NULL for vmlinux)
 * @funcs:	function entries for functions to be patched in the object
 * @callbacks:	functions to be executed pre/post (un)patching
 * @kobj:	kobject for sysfs resources
 * @func_list:	dynamic list of the function entries
 * @node:	list node for klp_patch obj_list
 * @mod:	kernel module associated with the patched object
 *		(NULL for vmlinux)
 * @dynamic:    temporary object for nop functions; dynamically allocated
 * @patched:	the object's funcs have been added to the klp_ops list
 */
struct klp_object {
	/* external */
	const char *name;
	struct klp_func *funcs;
	struct klp_callbacks callbacks;

	/* internal */
	struct kobject kobj;
	struct list_head func_list;
	struct list_head node;
	struct module *mod;
	bool dynamic;
	bool patched;
};

/**
 * struct klp_state - state of the system modified by the livepatch
 * @id:		system state identifier (non-zero)
 * @version:	version of the change
 * @data:	custom data
 */
struct klp_state {
	unsigned long id;
	unsigned int version;
	void *data;
};

/**
 * struct klp_patch - patch structure for live patching
 * @mod:	reference to the live patch module
 * @objs:	object entries for kernel objects to be patched
 * @states:	system states that can get modified
 * @replace:	replace all actively used patches
 * @list:	list node for global list of actively used patches
 * @kobj:	kobject for sysfs resources
 * @obj_list:	dynamic list of the object entries
 * @enabled:	the patch is enabled (but operation may be incomplete)
 * @forced:	was involved in a forced transition
 * @free_work:	patch cleanup from workqueue-context
 * @finish:	for waiting till it is safe to remove the patch module
 */
struct klp_patch {
	/* external */
	struct module *mod;
	struct klp_object *objs;
	struct klp_state *states;
	bool replace;

	/* internal */
	struct list_head list;
	struct kobject kobj;
	struct list_head obj_list;
	bool enabled;
	bool forced;
	struct work_struct free_work;
	struct completion finish;
};

#define klp_for_each_object_static(patch, obj) \
	for (obj = patch->objs; obj->funcs || obj->name; obj++)

#define klp_for_each_object_safe(patch, obj, tmp_obj)		\
	list_for_each_entry_safe(obj, tmp_obj, &patch->obj_list, node)

#define klp_for_each_object(patch, obj)	\
	list_for_each_entry(obj, &patch->obj_list, node)

#define klp_for_each_func_static(obj, func) \
	for (func = obj->funcs; \
	     func->old_name || func->new_func || func->old_sympos; \
	     func++)

#define klp_for_each_func_safe(obj, func, tmp_func)			\
	list_for_each_entry_safe(func, tmp_func, &obj->func_list, node)

#define klp_for_each_func(obj, func)	\
	list_for_each_entry(func, &obj->func_list, node)

int klp_enable_patch(struct klp_patch *);

/* Called from the module loader during module coming/going states */
int klp_module_coming(struct module *mod);
void klp_module_going(struct module *mod);

void klp_copy_process(struct task_struct *child);
void klp_update_patch_state(struct task_struct *task);

static inline bool klp_patch_pending(struct task_struct *task)
{
	return test_tsk_thread_flag(task, TIF_PATCH_PENDING);
}

static inline bool klp_have_reliable_stack(void)
{
	return IS_ENABLED(CONFIG_STACKTRACE) &&
	       IS_ENABLED(CONFIG_HAVE_RELIABLE_STACKTRACE);
}

typedef int (*klp_shadow_ctor_t)(void *obj,
				 void *shadow_data,
				 void *ctor_data);
typedef void (*klp_shadow_dtor_t)(void *obj, void *shadow_data);

void *klp_shadow_get(void *obj, unsigned long id);
void *klp_shadow_alloc(void *obj, unsigned long id,
		       size_t size, gfp_t gfp_flags,
		       klp_shadow_ctor_t ctor, void *ctor_data);
void *klp_shadow_get_or_alloc(void *obj, unsigned long id,
			      size_t size, gfp_t gfp_flags,
			      klp_shadow_ctor_t ctor, void *ctor_data);
void klp_shadow_free(void *obj, unsigned long id, klp_shadow_dtor_t dtor);
void klp_shadow_free_all(unsigned long id, klp_shadow_dtor_t dtor);

struct klp_state *klp_get_state(struct klp_patch *patch, unsigned long id);
struct klp_state *klp_get_prev_state(unsigned long id);

int klp_apply_section_relocs(struct module *pmod, Elf_Shdr *sechdrs,
			     const char *shstrtab, const char *strtab,
			     unsigned int symindex, unsigned int secindex,
			     const char *objname);

#else /* !CONFIG_LIVEPATCH */

static inline int klp_module_coming(struct module *mod) { return 0; }
static inline void klp_module_going(struct module *mod) {}
static inline bool klp_patch_pending(struct task_struct *task) { return false; }
static inline void klp_update_patch_state(struct task_struct *task) {}
static inline void klp_copy_process(struct task_struct *child) {}

static inline
int klp_apply_section_relocs(struct module *pmod, Elf_Shdr *sechdrs,
			     const char *shstrtab, const char *strtab,
			     unsigned int symindex, unsigned int secindex,
			     const char *objname)
{
	return 0;
}

#endif /* CONFIG_LIVEPATCH */

#endif /* _LINUX_LIVEPATCH_H_ */
