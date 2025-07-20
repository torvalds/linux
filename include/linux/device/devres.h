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

void devres_for_each_res(struct device *dev, dr_release_t release,
			 dr_match_t match, void *match_data,
			 void (*fn)(struct device *, void *, void *),
			 void *data);
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
