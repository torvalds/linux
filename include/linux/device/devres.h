/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _DEVICE_DEVRES_H_
#define _DEVICE_DEVRES_H_

#include <linux/err.h>
#include <linux/gfp_types.h>
#include <linux/numa.h>
#include <linux/overflow.h>
#include <linux/stdarg.h>
#include <linux/types.h>
#include <asm/bug.h>
#include <asm/percpu.h>

struct device;
struct device_node;
struct resource;

/* device resource management */
typedef void (*dr_release_t)(struct device *dev, void *res);
typedef int (*dr_match_t)(struct device *dev, void *res, void *match_data);

void * __malloc
__devres_alloc_node(dr_release_t release, size_t size, gfp_t gfp, int nid, const char *name);
#define devres_alloc(release, size, gfp) \
	__devres_alloc_node(release, size, gfp, NUMA_NO_NODE, #release)
#define devres_alloc_node(release, size, gfp, nid) \
	__devres_alloc_node(release, size, gfp, nid, #release)

void devres_free(void *res);
void devres_add(struct device *dev, void *res);
void *devres_find(struct device *dev, dr_release_t release, dr_match_t match, void *match_data);
void *devres_get(struct device *dev, void *new_res, dr_match_t match, void *match_data);
void *devres_remove(struct device *dev, dr_release_t release, dr_match_t match, void *match_data);
int devres_destroy(struct device *dev, dr_release_t release, dr_match_t match, void *match_data);
int devres_release(struct device *dev, dr_release_t release, dr_match_t match, void *match_data);

/* devres group */
void * __must_check devres_open_group(struct device *dev, void *id, gfp_t gfp);
void devres_close_group(struct device *dev, void *id);
void devres_remove_group(struct device *dev, void *id);
int devres_release_group(struct device *dev, void *id);

/* managed devm_k.alloc/kfree for device drivers */
void * __alloc_size(2)
devm_kmalloc(struct device *dev, size_t size, gfp_t gfp);
void * __must_check __realloc_size(3)
devm_krealloc(struct device *dev, void *ptr, size_t size, gfp_t gfp);
static inline void *devm_kzalloc(struct device *dev, size_t size, gfp_t gfp)
{
	return devm_kmalloc(dev, size, gfp | __GFP_ZERO);
}
static inline void *devm_kmalloc_array(struct device *dev, size_t n, size_t size, gfp_t flags)
{
	size_t bytes;

	if (unlikely(check_mul_overflow(n, size, &bytes)))
		return NULL;

	return devm_kmalloc(dev, bytes, flags);
}
static inline void *devm_kcalloc(struct device *dev, size_t n, size_t size, gfp_t flags)
{
	return devm_kmalloc_array(dev, n, size, flags | __GFP_ZERO);
}

/**
 * __devm_alloc_objs - Device-managed allocation of objects of a given type
 * @DEVM_ALLOC: which device-managed size-based allocator to use.
 * @DEV: device to scope the allocation lifetime to.
 * @GFP: GFP flags for the allocation.
 * @TYPE: type to allocate space for.
 * @COUNT: how many @TYPE objects to allocate.
 *
 * Device-managed counterpart of __alloc_objs().
 * @DEVM_ALLOC takes (@DEV, size, gfp).
 *
 * Returns: Newly allocated pointer to (first) @TYPE of @COUNT-many allocated
 * @TYPE objects, or NULL on failure.
 */
#define __devm_alloc_objs(DEVM_ALLOC, DEV, GFP, TYPE, COUNT)		\
({									\
	const size_t __obj_size = size_mul(sizeof(TYPE), COUNT);	\
	(TYPE *)DEVM_ALLOC(DEV, __obj_size, GFP);			\
})

/**
 * devm_kmalloc_obj - Device-managed allocation of a single typed object
 * @DEV: Device to scope the allocation lifetime to.
 * @VAR_OR_TYPE: Variable or type to allocate.
 * @GFP: Optional GFP flags (defaults to %GFP_KERNEL).
 *
 * Device-managed counterpart of kmalloc_obj(): the allocation is sized from
 * @VAR_OR_TYPE and returned as a pointer to that type, not void *.
 *
 * Returns: newly allocated pointer to a @VAR_OR_TYPE on success, or NULL on
 * failure.
 */
#define devm_kmalloc_obj(DEV, VAR_OR_TYPE, ...)				\
	__devm_alloc_objs(devm_kmalloc, DEV, default_gfp(__VA_ARGS__),	\
			  typeof(VAR_OR_TYPE), 1)

/**
 * devm_kmalloc_objs - Device-managed allocation of an array of a typed object
 * @DEV: Device to scope the allocation lifetime to.
 * @VAR_OR_TYPE: Variable or type to allocate an array of.
 * @COUNT: How many elements in the array.
 * @GFP: Optional GFP flags (defaults to %GFP_KERNEL).
 *
 * Returns: newly allocated pointer to an array of @VAR_OR_TYPE on success, or
 * NULL on failure (including when the total size overflows).
 */
#define devm_kmalloc_objs(DEV, VAR_OR_TYPE, COUNT, ...) \
	__devm_alloc_objs(devm_kmalloc, DEV, default_gfp(__VA_ARGS__), \
			  typeof(VAR_OR_TYPE), COUNT)

/**
 * __devm_alloc_flex - Device-managed allocation of a trailing-flex-array object
 * @DEVM_ALLOC: which device-managed allocator to use.
 * @DEV: device to scope the allocation lifetime to.
 * @GFP: GFP flags for the allocation.
 * @TYPE: type of structure to allocate space for.
 * @FAM: name of the flexible array member of @TYPE.
 * @COUNT: how many @FAM elements to allocate space for.
 *
 * Device-managed counterpart of __alloc_flex().
 * @DEVM_ALLOC takes (@DEV, size, gfp).
 *
 * Returns: Newly allocated pointer to @TYPE with @COUNT-many trailing @FAM
 * elements, or NULL on failure.
 */
#define __devm_alloc_flex(DEVM_ALLOC, DEV, GFP, TYPE, FAM, COUNT)	\
({									\
	const size_t __count = (COUNT);					\
	const size_t __obj_size = struct_size_t(TYPE, FAM, __count);	\
	TYPE *__obj_ptr = DEVM_ALLOC(DEV, __obj_size, GFP);		\
	if (__obj_ptr)							\
		__set_flex_counter(__obj_ptr->FAM, __count);		\
	__obj_ptr;							\
})

/**
 * devm_kmalloc_flex - Device-managed allocation of a flexible-array structure
 * @DEV: Device to scope the allocation lifetime to.
 * @VAR_OR_TYPE: Variable or type to allocate (with its flexible array).
 * @FAM: The name of the flexible array member of the structure.
 * @COUNT: How many flexible array member elements to allocate.
 * @GFP: Optional GFP flags (defaults to %GFP_KERNEL).
 *
 * Returns: newly allocated pointer to @VAR_OR_TYPE on success, or NULL on
 * failure. If @FAM has been annotated with __counted_by(), its counter is set
 * to @COUNT.
 */
#define devm_kmalloc_flex(DEV, VAR_OR_TYPE, FAM, COUNT, ...) \
	__devm_alloc_flex(devm_kmalloc, DEV, default_gfp(__VA_ARGS__), \
			  typeof(VAR_OR_TYPE), FAM, COUNT)

/* All devm_kzalloc aliases for devm_kmalloc_(obj|objs|flex). */
#define devm_kzalloc_obj(DEV, P, ...) \
	__devm_alloc_objs(devm_kzalloc, DEV, default_gfp(__VA_ARGS__), typeof(P), 1)
#define devm_kzalloc_objs(DEV, P, COUNT, ...) \
	__devm_alloc_objs(devm_kzalloc, DEV, default_gfp(__VA_ARGS__), \
			  typeof(P), COUNT)
#define devm_kzalloc_flex(DEV, P, FAM, COUNT, ...) \
	__devm_alloc_flex(devm_kzalloc, DEV, default_gfp(__VA_ARGS__), \
			  typeof(P), FAM, COUNT)

static inline __realloc_size(3, 4) void * __must_check
devm_krealloc_array(struct device *dev, void *p, size_t new_n, size_t new_size, gfp_t flags)
{
	size_t bytes;

	if (unlikely(check_mul_overflow(new_n, new_size, &bytes)))
		return NULL;

	return devm_krealloc(dev, p, bytes, flags);
}

void devm_kfree(struct device *dev, const void *p);

void * __realloc_size(3)
devm_kmemdup(struct device *dev, const void *src, size_t len, gfp_t gfp);
const void *
devm_kmemdup_const(struct device *dev, const void *src, size_t len, gfp_t gfp);
static inline void *devm_kmemdup_array(struct device *dev, const void *src,
				       size_t n, size_t size, gfp_t flags)
{
	return devm_kmemdup(dev, src, size_mul(size, n), flags);
}

char * __malloc
devm_kstrdup(struct device *dev, const char *s, gfp_t gfp);
const char *devm_kstrdup_const(struct device *dev, const char *s, gfp_t gfp);
char * __printf(3, 0) __malloc
devm_kvasprintf(struct device *dev, gfp_t gfp, const char *fmt, va_list ap);
char * __printf(3, 4) __malloc
devm_kasprintf(struct device *dev, gfp_t gfp, const char *fmt, ...);

/**
 * devm_alloc_percpu - Resource-managed alloc_percpu
 * @dev: Device to allocate per-cpu memory for
 * @type: Type to allocate per-cpu memory for
 *
 * Managed alloc_percpu. Per-cpu memory allocated with this function is
 * automatically freed on driver detach.
 *
 * RETURNS:
 * Pointer to allocated memory on success, NULL on failure.
 */
#define devm_alloc_percpu(dev, type)      \
	((typeof(type) __percpu *)__devm_alloc_percpu((dev), sizeof(type), __alignof__(type)))

void __percpu *__devm_alloc_percpu(struct device *dev, size_t size, size_t align);

unsigned long devm_get_free_pages(struct device *dev, gfp_t gfp_mask, unsigned int order);
void devm_free_pages(struct device *dev, unsigned long addr);

#ifdef CONFIG_HAS_IOMEM

void __iomem *devm_ioremap_resource(struct device *dev, const struct resource *res);
void __iomem *devm_ioremap_resource_wc(struct device *dev, const struct resource *res);

void __iomem *devm_of_iomap(struct device *dev, struct device_node *node, int index,
			    resource_size_t *size);
#else

static inline
void __iomem *devm_ioremap_resource(struct device *dev, const struct resource *res)
{
	return IOMEM_ERR_PTR(-EINVAL);
}

static inline
void __iomem *devm_ioremap_resource_wc(struct device *dev, const struct resource *res)
{
	return IOMEM_ERR_PTR(-EINVAL);
}

static inline
void __iomem *devm_of_iomap(struct device *dev, struct device_node *node, int index,
			    resource_size_t *size)
{
	return IOMEM_ERR_PTR(-EINVAL);
}

#endif

/* allows to add/remove a custom action to devres stack */
int devm_remove_action_nowarn(struct device *dev, void (*action)(void *), void *data);

/**
 * devm_remove_action() - removes previously added custom action
 * @dev: Device that owns the action
 * @action: Function implementing the action
 * @data: Pointer to data passed to @action implementation
 *
 * Removes instance of @action previously added by devm_add_action().
 * Both action and data should match one of the existing entries.
 */
static inline
void devm_remove_action(struct device *dev, void (*action)(void *), void *data)
{
	WARN_ON(devm_remove_action_nowarn(dev, action, data));
}

void devm_release_action(struct device *dev, void (*action)(void *), void *data);

int __devm_add_action(struct device *dev, void (*action)(void *), void *data, const char *name);
#define devm_add_action(dev, action, data) \
	__devm_add_action(dev, action, data, #action)

static inline int __devm_add_action_or_reset(struct device *dev, void (*action)(void *),
					     void *data, const char *name)
{
	int ret;

	ret = __devm_add_action(dev, action, data, name);
	if (ret)
		action(data);

	return ret;
}
#define devm_add_action_or_reset(dev, action, data) \
	__devm_add_action_or_reset(dev, action, data, #action)

bool devm_is_action_added(struct device *dev, void (*action)(void *), void *data);

#endif /* _DEVICE_DEVRES_H_ */
