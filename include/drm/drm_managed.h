// SPDX-License-Identifier: GPL-2.0

#ifndef _DRM_MANAGED_H_
#define _DRM_MANAGED_H_

#include <linux/gfp.h>
#include <linux/overflow.h>
#include <linux/types.h>

struct drm_device;
struct mutex;

typedef void (*drmres_release_t)(struct drm_device *dev, void *res);

/**
 * drmm_add_action - add a managed release action to a &drm_device
 * @dev: DRM device
 * @action: function which should be called when @dev is released
 * @data: opaque pointer, passed to @action
 *
 * This function adds the @release action with optional parameter @data to the
 * list of cleanup actions for @dev. The cleanup actions will be run in reverse
 * order in the final drm_dev_put() call for @dev.
 */
#define drmm_add_action(dev, action, data) \
	__drmm_add_action(dev, action, data, #action)

int __must_check __drmm_add_action(struct drm_device *dev,
				   drmres_release_t action,
				   void *data, const char *name);

/**
 * drmm_add_action_or_reset - add a managed release action to a &drm_device
 * @dev: DRM device
 * @action: function which should be called when @dev is released
 * @data: opaque pointer, passed to @action
 *
 * Similar to drmm_add_action(), with the only difference that upon failure
 * @action is directly called for any cleanup work necessary on failures.
 */
#define drmm_add_action_or_reset(dev, action, data) \
	__drmm_add_action_or_reset(dev, action, data, #action)

int __must_check __drmm_add_action_or_reset(struct drm_device *dev,
					    drmres_release_t action,
					    void *data, const char *name);

void drmm_release_action(struct drm_device *dev,
			 drmres_release_t action,
			 void *data);

void *drmm_kmalloc(struct drm_device *dev, size_t size, gfp_t gfp) __malloc;

/**
 * drmm_kzalloc - &drm_device managed kzalloc()
 * @dev: DRM device
 * @size: size of the memory allocation
 * @gfp: GFP allocation flags
 *
 * This is a &drm_device managed version of kzalloc(). The allocated memory is
 * automatically freed on the final drm_dev_put(). Memory can also be freed
 * before the final drm_dev_put() by calling drmm_kfree().
 */
static inline void *drmm_kzalloc(struct drm_device *dev, size_t size, gfp_t gfp)
{
	return drmm_kmalloc(dev, size, gfp | __GFP_ZERO);
}

/**
 * drmm_kmalloc_array - &drm_device managed kmalloc_array()
 * @dev: DRM device
 * @n: number of array elements to allocate
 * @size: size of array member
 * @flags: GFP allocation flags
 *
 * This is a &drm_device managed version of kmalloc_array(). The allocated
 * memory is automatically freed on the final drm_dev_put() and works exactly
 * like a memory allocation obtained by drmm_kmalloc().
 */
static inline void *drmm_kmalloc_array(struct drm_device *dev,
				       size_t n, size_t size, gfp_t flags)
{
	size_t bytes;

	if (unlikely(check_mul_overflow(n, size, &bytes)))
		return NULL;

	return drmm_kmalloc(dev, bytes, flags);
}

/**
 * drmm_kcalloc - &drm_device managed kcalloc()
 * @dev: DRM device
 * @n: number of array elements to allocate
 * @size: size of array member
 * @flags: GFP allocation flags
 *
 * This is a &drm_device managed version of kcalloc(). The allocated memory is
 * automatically freed on the final drm_dev_put() and works exactly like a
 * memory allocation obtained by drmm_kmalloc().
 */
static inline void *drmm_kcalloc(struct drm_device *dev,
				 size_t n, size_t size, gfp_t flags)
{
	return drmm_kmalloc_array(dev, n, size, flags | __GFP_ZERO);
}

char *drmm_kstrdup(struct drm_device *dev, const char *s, gfp_t gfp);

void drmm_kfree(struct drm_device *dev, void *data);

void __drmm_mutex_release(struct drm_device *dev, void *res);

/**
 * drmm_mutex_init - &drm_device-managed mutex_init()
 * @dev: DRM device
 * @lock: lock to be initialized
 *
 * Returns:
 * 0 on success, or a negative errno code otherwise.
 *
 * This is a &drm_device-managed version of mutex_init(). The initialized
 * lock is automatically destroyed on the final drm_dev_put().
 */
#define drmm_mutex_init(dev, lock) ({					     \
	mutex_init(lock);						     \
	drmm_add_action_or_reset(dev, __drmm_mutex_release, lock);	     \
})									     \

#endif
