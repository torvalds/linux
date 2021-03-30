/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_IOASID_H
#define __LINUX_IOASID_H

#include <linux/types.h>
#include <linux/errno.h>

#define INVALID_IOASID ((ioasid_t)-1)
typedef unsigned int ioasid_t;
typedef ioasid_t (*ioasid_alloc_fn_t)(ioasid_t min, ioasid_t max, void *data);
typedef void (*ioasid_free_fn_t)(ioasid_t ioasid, void *data);

struct ioasid_set {
	int dummy;
};

/**
 * struct ioasid_allocator_ops - IOASID allocator helper functions and data
 *
 * @alloc:	helper function to allocate IOASID
 * @free:	helper function to free IOASID
 * @list:	for tracking ops that share helper functions but not data
 * @pdata:	data belong to the allocator, provided when calling alloc()
 */
struct ioasid_allocator_ops {
	ioasid_alloc_fn_t alloc;
	ioasid_free_fn_t free;
	struct list_head list;
	void *pdata;
};

#define DECLARE_IOASID_SET(name) struct ioasid_set name = { 0 }

#if IS_ENABLED(CONFIG_IOASID)
ioasid_t ioasid_alloc(struct ioasid_set *set, ioasid_t min, ioasid_t max,
		      void *private);
void ioasid_get(ioasid_t ioasid);
bool ioasid_put(ioasid_t ioasid);
void *ioasid_find(struct ioasid_set *set, ioasid_t ioasid,
		  bool (*getter)(void *));
int ioasid_register_allocator(struct ioasid_allocator_ops *allocator);
void ioasid_unregister_allocator(struct ioasid_allocator_ops *allocator);
int ioasid_set_data(ioasid_t ioasid, void *data);

#else /* !CONFIG_IOASID */
static inline ioasid_t ioasid_alloc(struct ioasid_set *set, ioasid_t min,
				    ioasid_t max, void *private)
{
	return INVALID_IOASID;
}

static inline void ioasid_get(ioasid_t ioasid)
{
}

static inline bool ioasid_put(ioasid_t ioasid)
{
	return false;
}

static inline void *ioasid_find(struct ioasid_set *set, ioasid_t ioasid,
				bool (*getter)(void *))
{
	return NULL;
}

static inline int ioasid_register_allocator(struct ioasid_allocator_ops *allocator)
{
	return -ENOTSUPP;
}

static inline void ioasid_unregister_allocator(struct ioasid_allocator_ops *allocator)
{
}

static inline int ioasid_set_data(ioasid_t ioasid, void *data)
{
	return -ENOTSUPP;
}

#endif /* CONFIG_IOASID */
#endif /* __LINUX_IOASID_H */
