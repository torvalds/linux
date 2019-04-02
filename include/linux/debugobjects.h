/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_DEOBJECTS_H
#define _LINUX_DEOBJECTS_H

#include <linux/list.h>
#include <linux/spinlock.h>

enum de_obj_state {
	ODE_STATE_NONE,
	ODE_STATE_INIT,
	ODE_STATE_INACTIVE,
	ODE_STATE_ACTIVE,
	ODE_STATE_DESTROYED,
	ODE_STATE_NOTAVAILABLE,
	ODE_STATE_MAX,
};

struct de_obj_descr;

/**
 * struct de_obj - representaion of an tracked object
 * @node:	hlist node to link the object into the tracker list
 * @state:	tracked object state
 * @astate:	current active state
 * @object:	pointer to the real object
 * @descr:	pointer to an object type specific de description structure
 */
struct de_obj {
	struct hlist_node	node;
	enum de_obj_state	state;
	unsigned int		astate;
	void			*object;
	struct de_obj_descr	*descr;
};

/**
 * struct de_obj_descr - object type specific de description structure
 *
 * @name:		name of the object typee
 * @de_hint:		function returning address, which have associated
 *			kernel symbol, to allow identify the object
 * @is_static_object:	return true if the obj is static, otherwise return false
 * @fixup_init:		fixup function, which is called when the init check
 *			fails. All fixup functions must return true if fixup
 *			was successful, otherwise return false
 * @fixup_activate:	fixup function, which is called when the activate check
 *			fails
 * @fixup_destroy:	fixup function, which is called when the destroy check
 *			fails
 * @fixup_free:		fixup function, which is called when the free check
 *			fails
 * @fixup_assert_init:  fixup function, which is called when the assert_init
 *			check fails
 */
struct de_obj_descr {
	const char		*name;
	void *(*de_hint)(void *addr);
	bool (*is_static_object)(void *addr);
	bool (*fixup_init)(void *addr, enum de_obj_state state);
	bool (*fixup_activate)(void *addr, enum de_obj_state state);
	bool (*fixup_destroy)(void *addr, enum de_obj_state state);
	bool (*fixup_free)(void *addr, enum de_obj_state state);
	bool (*fixup_assert_init)(void *addr, enum de_obj_state state);
};

#ifdef CONFIG_DE_OBJECTS
extern void de_object_init      (void *addr, struct de_obj_descr *descr);
extern void
de_object_init_on_stack(void *addr, struct de_obj_descr *descr);
extern int de_object_activate  (void *addr, struct de_obj_descr *descr);
extern void de_object_deactivate(void *addr, struct de_obj_descr *descr);
extern void de_object_destroy   (void *addr, struct de_obj_descr *descr);
extern void de_object_free      (void *addr, struct de_obj_descr *descr);
extern void de_object_assert_init(void *addr, struct de_obj_descr *descr);

/*
 * Active state:
 * - Set at 0 upon initialization.
 * - Must return to 0 before deactivation.
 */
extern void
de_object_active_state(void *addr, struct de_obj_descr *descr,
			  unsigned int expect, unsigned int next);

extern void de_objects_early_init(void);
extern void de_objects_mem_init(void);
#else
static inline void
de_object_init      (void *addr, struct de_obj_descr *descr) { }
static inline void
de_object_init_on_stack(void *addr, struct de_obj_descr *descr) { }
static inline int
de_object_activate  (void *addr, struct de_obj_descr *descr) { return 0; }
static inline void
de_object_deactivate(void *addr, struct de_obj_descr *descr) { }
static inline void
de_object_destroy   (void *addr, struct de_obj_descr *descr) { }
static inline void
de_object_free      (void *addr, struct de_obj_descr *descr) { }
static inline void
de_object_assert_init(void *addr, struct de_obj_descr *descr) { }

static inline void de_objects_early_init(void) { }
static inline void de_objects_mem_init(void) { }
#endif

#ifdef CONFIG_DE_OBJECTS_FREE
extern void de_check_no_obj_freed(const void *address, unsigned long size);
#else
static inline void
de_check_no_obj_freed(const void *address, unsigned long size) { }
#endif

#endif
