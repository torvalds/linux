// SPDX-License-Identifier: GPL-2.0

#ifndef _LINUX_MM_SLOT_H
#define _LINUX_MM_SLOT_H

#include <linux/hashtable.h>
#include <linux/slab.h>

/*
 * struct mm_slot - hash lookup from mm to mm_slot
 * @hash: link to the mm_slots hash list
 * @mm_node: link into the mm_slots list
 * @mm: the mm that this information is valid for
 */
struct mm_slot {
	struct hlist_node hash;
	struct list_head mm_node;
	struct mm_struct *mm;
};

#define mm_slot_entry(ptr, type, member) \
	container_of(ptr, type, member)

static inline void *mm_slot_alloc(struct kmem_cache *cache)
{
	if (!cache)	/* initialization failed */
		return NULL;
	return kmem_cache_zalloc(cache, GFP_KERNEL);
}

static inline void mm_slot_free(struct kmem_cache *cache, void *objp)
{
	kmem_cache_free(cache, objp);
}

#define mm_slot_lookup(_hashtable, _mm) 				       \
({									       \
	struct mm_slot *tmp_slot, *mm_slot = NULL;			       \
									       \
	hash_for_each_possible(_hashtable, tmp_slot, hash, (unsigned long)_mm) \
		if (_mm == tmp_slot->mm) {				       \
			mm_slot = tmp_slot;				       \
			break;						       \
		}							       \
									       \
	mm_slot;							       \
})

#define mm_slot_insert(_hashtable, _mm, _mm_slot)			       \
({									       \
	_mm_slot->mm = _mm;						       \
	hash_add(_hashtable, &_mm_slot->hash, (unsigned long)_mm);	       \
})

#endif /* _LINUX_MM_SLOT_H */
