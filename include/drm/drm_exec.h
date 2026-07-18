/* SPDX-License-Identifier: GPL-2.0 OR MIT */

#ifndef __DRM_EXEC_H__
#define __DRM_EXEC_H__

#include <linux/compiler.h>
#include <linux/ww_mutex.h>

#define DRM_EXEC_INTERRUPTIBLE_WAIT	BIT(0)
#define DRM_EXEC_IGNORE_DUPLICATES	BIT(1)

/*
 * Dummy value used to initially enter the retry loop.
 * internal use only.
 */
#define DRM_EXEC_DUMMY ((void *)~0)

struct drm_gem_object;

/**
 * struct drm_exec - Execution context
 */
struct drm_exec {
	/**
	 * @flags: Flags to control locking behavior
	 */
	u32                     flags;

	/**
	 * @ticket: WW ticket used for acquiring locks
	 */
	struct ww_acquire_ctx	ticket;

	/**
	 * @num_objects: number of objects locked
	 */
	unsigned int		num_objects;

	/**
	 * @max_objects: maximum objects in array
	 */
	unsigned int		max_objects;

	/**
	 * @objects: array of the locked objects
	 */
	struct drm_gem_object	**objects;

	/**
	 * @contended: contended GEM object we backed off for
	 */
	struct drm_gem_object	*contended;

	/**
	 * @prelocked: already locked GEM object due to contention
	 */
	struct drm_gem_object *prelocked;
};

/**
 * drm_exec_obj() - Return the object for a give drm_exec index
 * @exec: Pointer to the drm_exec context
 * @index: The index.
 *
 * Return: Pointer to the locked object corresponding to @index if
 * index is within the number of locked objects. NULL otherwise.
 */
static inline struct drm_gem_object *
drm_exec_obj(struct drm_exec *exec, unsigned long index)
{
	return index < exec->num_objects ? exec->objects[index] : NULL;
}

/* Helper for drm_exec_for_each_locked_object(). Internal use only. */
#define __drm_exec_for_each_locked_object(exec, obj, __index)		\
	for (unsigned long __index = 0; ((obj) = drm_exec_obj(exec, __index)); ++__index)
/**
 * drm_exec_for_each_locked_object - iterate over all the locked objects
 * @exec: drm_exec object
 * @obj: the current GEM object
 *
 * Iterate over all the locked GEM objects inside the drm_exec object.
 */
#define drm_exec_for_each_locked_object(exec, obj)			\
	__drm_exec_for_each_locked_object(exec, obj, __UNIQUE_ID(drm_exec))

/* Helper for drm_exec_for_each_locked_object_reverse(). Internal use only. */
#define __drm_exec_for_each_locked_object_reverse(exec, obj, __index)	\
	for (unsigned long __index = (exec)->num_objects - 1;		\
	     ((obj) = drm_exec_obj(exec, __index)); --__index)
/**
 * drm_exec_for_each_locked_object_reverse - iterate over all the locked
 * objects in reverse locking order
 * @exec: drm_exec object
 * @obj: the current GEM object
 *
 * Iterate over all the locked GEM objects inside the drm_exec object in
 * reverse locking order. Note that the internal index may wrap around,
 * but that will be caught by drm_exec_obj(), returning a NULL object.
 */
#define drm_exec_for_each_locked_object_reverse(exec, obj)		\
	__drm_exec_for_each_locked_object_reverse(exec, obj, __UNIQUE_ID(drm_exec))

/*
 * Helper to drm_exec_until_all_locked(). Don't use directly.
 *
 * Since labels can't be defined local to the loop's body we use a jump pointer
 * to make sure that the retry is only used from within the loop's body.
 */
#define __drm_exec_until_all_locked(exec, _label)			 \
_label:									 \
	for (void *const __maybe_unused __drm_exec_retry_ptr = &&_label; \
	     drm_exec_cleanup(exec);)

/**
 * drm_exec_until_all_locked - loop until all GEM objects are locked
 * @exec: drm_exec object
 *
 * Core functionality of the drm_exec object. Loops until all GEM objects are
 * locked and no more contention exists. At the beginning of the loop it is
 * guaranteed that no GEM object is locked.
 */
#define drm_exec_until_all_locked(exec)					\
	__drm_exec_until_all_locked(exec, __UNIQUE_ID(drm_exec))

/**
 * drm_exec_retry_on_contention - restart the loop to grap all locks
 * @exec: drm_exec object
 *
 * Control flow helper to continue when a contention was detected and we need to
 * clean up and re-start the loop to prepare all GEM objects.
 */
#define drm_exec_retry_on_contention(exec)			\
	do {							\
		if (unlikely(drm_exec_is_contended(exec)))	\
			goto *__drm_exec_retry_ptr;		\
	} while (0)

/**
 * drm_exec_is_contended - check for contention
 * @exec: drm_exec object
 *
 * Returns true if the drm_exec object has run into some contention while
 * locking a GEM object and needs to clean up.
 */
static inline bool drm_exec_is_contended(struct drm_exec *exec)
{
	return !!exec->contended;
}

/**
 * drm_exec_retry() - Unconditionally restart the loop to grab all locks.
 * @exec: drm_exec object
 *
 * Unconditionally retry the loop to lock all objects. For consistency,
 * the exec object needs to be newly initialized.
 */
#define drm_exec_retry(_exec)					\
	do {							\
		WARN_ON((_exec)->contended != DRM_EXEC_DUMMY);	\
		goto *__drm_exec_retry_ptr;			\
	} while (0)

/**
 * drm_exec_ticket - return the ww_acquire_ctx for this exec context
 * @exec: drm_exec object
 *
 * Return: Pointer to the ww_acquire_ctx embedded in @exec.
 */
static inline struct ww_acquire_ctx *drm_exec_ticket(struct drm_exec *exec)
{
	return &exec->ticket;
}

void drm_exec_init(struct drm_exec *exec, u32 flags, unsigned nr);
void drm_exec_fini(struct drm_exec *exec);
bool drm_exec_cleanup(struct drm_exec *exec);
int drm_exec_lock_obj(struct drm_exec *exec, struct drm_gem_object *obj);
void drm_exec_unlock_obj(struct drm_exec *exec, struct drm_gem_object *obj);
int drm_exec_prepare_obj(struct drm_exec *exec, struct drm_gem_object *obj,
			 unsigned int num_fences);
int drm_exec_prepare_array(struct drm_exec *exec,
			   struct drm_gem_object **objects,
			   unsigned int num_objects,
			   unsigned int num_fences);

#endif
