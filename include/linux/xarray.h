/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef _LINUX_XARRAY_H
#define _LINUX_XARRAY_H
/*
 * eXtensible Arrays
 * Copyright (c) 2017 Microsoft Corporation
 * Author: Matthew Wilcox <willy@infradead.org>
 *
 * See Documentation/core-api/xarray.rst for how to use the XArray.
 */

#include <linux/bitmap.h>
#include <linux/bug.h>
#include <linux/compiler.h>
#include <linux/gfp.h>
#include <linux/kconfig.h>
#include <linux/kernel.h>
#include <linux/rcupdate.h>
#include <linux/sched/mm.h>
#include <linux/spinlock.h>
#include <linux/types.h>

/*
 * The bottom two bits of the entry determine how the XArray interprets
 * the contents:
 *
 * 00: Pointer entry
 * 10: Internal entry
 * x1: Value entry or tagged pointer
 *
 * Attempting to store internal entries in the XArray is a bug.
 *
 * Most internal entries are pointers to the next node in the tree.
 * The following internal entries have a special meaning:
 *
 * 0-62: Sibling entries
 * 256: Retry entry
 * 257: Zero entry
 *
 * Errors are also represented as internal entries, but use the negative
 * space (-4094 to -2).  They're never stored in the slots array; only
 * returned by the normal API.
 */

#define BITS_PER_XA_VALUE	(BITS_PER_LONG - 1)

/**
 * xa_mk_value() - Create an XArray entry from an integer.
 * @v: Value to store in XArray.
 *
 * Context: Any context.
 * Return: An entry suitable for storing in the XArray.
 */
static inline void *xa_mk_value(unsigned long v)
{
	WARN_ON((long)v < 0);
	return (void *)((v << 1) | 1);
}

/**
 * xa_to_value() - Get value stored in an XArray entry.
 * @entry: XArray entry.
 *
 * Context: Any context.
 * Return: The value stored in the XArray entry.
 */
static inline unsigned long xa_to_value(const void *entry)
{
	return (unsigned long)entry >> 1;
}

/**
 * xa_is_value() - Determine if an entry is a value.
 * @entry: XArray entry.
 *
 * Context: Any context.
 * Return: True if the entry is a value, false if it is a pointer.
 */
static inline bool xa_is_value(const void *entry)
{
	return (unsigned long)entry & 1;
}

/**
 * xa_tag_pointer() - Create an XArray entry for a tagged pointer.
 * @p: Plain pointer.
 * @tag: Tag value (0, 1 or 3).
 *
 * If the user of the XArray prefers, they can tag their pointers instead
 * of storing value entries.  Three tags are available (0, 1 and 3).
 * These are distinct from the xa_mark_t as they are not replicated up
 * through the array and cannot be searched for.
 *
 * Context: Any context.
 * Return: An XArray entry.
 */
static inline void *xa_tag_pointer(void *p, unsigned long tag)
{
	return (void *)((unsigned long)p | tag);
}

/**
 * xa_untag_pointer() - Turn an XArray entry into a plain pointer.
 * @entry: XArray entry.
 *
 * If you have stored a tagged pointer in the XArray, call this function
 * to get the untagged version of the pointer.
 *
 * Context: Any context.
 * Return: A pointer.
 */
static inline void *xa_untag_pointer(void *entry)
{
	return (void *)((unsigned long)entry & ~3UL);
}

/**
 * xa_pointer_tag() - Get the tag stored in an XArray entry.
 * @entry: XArray entry.
 *
 * If you have stored a tagged pointer in the XArray, call this function
 * to get the tag of that pointer.
 *
 * Context: Any context.
 * Return: A tag.
 */
static inline unsigned int xa_pointer_tag(void *entry)
{
	return (unsigned long)entry & 3UL;
}

/*
 * xa_mk_internal() - Create an internal entry.
 * @v: Value to turn into an internal entry.
 *
 * Internal entries are used for a number of purposes.  Entries 0-255 are
 * used for sibling entries (only 0-62 are used by the current code).  256
 * is used for the retry entry.  257 is used for the reserved / zero entry.
 * Negative internal entries are used to represent errnos.  Node pointers
 * are also tagged as internal entries in some situations.
 *
 * Context: Any context.
 * Return: An XArray internal entry corresponding to this value.
 */
static inline void *xa_mk_internal(unsigned long v)
{
	return (void *)((v << 2) | 2);
}

/*
 * xa_to_internal() - Extract the value from an internal entry.
 * @entry: XArray entry.
 *
 * Context: Any context.
 * Return: The value which was stored in the internal entry.
 */
static inline unsigned long xa_to_internal(const void *entry)
{
	return (unsigned long)entry >> 2;
}

/*
 * xa_is_internal() - Is the entry an internal entry?
 * @entry: XArray entry.
 *
 * Context: Any context.
 * Return: %true if the entry is an internal entry.
 */
static inline bool xa_is_internal(const void *entry)
{
	return ((unsigned long)entry & 3) == 2;
}

#define XA_ZERO_ENTRY		xa_mk_internal(257)

/**
 * xa_is_zero() - Is the entry a zero entry?
 * @entry: Entry retrieved from the XArray
 *
 * The normal API will return NULL as the contents of a slot containing
 * a zero entry.  You can only see zero entries by using the advanced API.
 *
 * Return: %true if the entry is a zero entry.
 */
static inline bool xa_is_zero(const void *entry)
{
	return unlikely(entry == XA_ZERO_ENTRY);
}

/**
 * xa_is_err() - Report whether an XArray operation returned an error
 * @entry: Result from calling an XArray function
 *
 * If an XArray operation cannot complete an operation, it will return
 * a special value indicating an error.  This function tells you
 * whether an error occurred; xa_err() tells you which error occurred.
 *
 * Context: Any context.
 * Return: %true if the entry indicates an error.
 */
static inline bool xa_is_err(const void *entry)
{
	return unlikely(xa_is_internal(entry) &&
			entry >= xa_mk_internal(-MAX_ERRNO));
}

/**
 * xa_err() - Turn an XArray result into an errno.
 * @entry: Result from calling an XArray function.
 *
 * If an XArray operation cannot complete an operation, it will return
 * a special pointer value which encodes an errno.  This function extracts
 * the errno from the pointer value, or returns 0 if the pointer does not
 * represent an errno.
 *
 * Context: Any context.
 * Return: A negative errno or 0.
 */
static inline int xa_err(void *entry)
{
	/* xa_to_internal() would not do sign extension. */
	if (xa_is_err(entry))
		return (long)entry >> 2;
	return 0;
}

/**
 * struct xa_limit - Represents a range of IDs.
 * @min: The lowest ID to allocate (inclusive).
 * @max: The maximum ID to allocate (inclusive).
 *
 * This structure is used either directly or via the XA_LIMIT() macro
 * to communicate the range of IDs that are valid for allocation.
 * Three common ranges are predefined for you:
 * * xa_limit_32b	- [0 - UINT_MAX]
 * * xa_limit_31b	- [0 - INT_MAX]
 * * xa_limit_16b	- [0 - USHRT_MAX]
 */
struct xa_limit {
	u32 max;
	u32 min;
};

#define XA_LIMIT(_min, _max) (struct xa_limit) { .min = _min, .max = _max }

#define xa_limit_32b	XA_LIMIT(0, UINT_MAX)
#define xa_limit_31b	XA_LIMIT(0, INT_MAX)
#define xa_limit_16b	XA_LIMIT(0, USHRT_MAX)

typedef unsigned __bitwise xa_mark_t;
#define XA_MARK_0		((__force xa_mark_t)0U)
#define XA_MARK_1		((__force xa_mark_t)1U)
#define XA_MARK_2		((__force xa_mark_t)2U)
#define XA_PRESENT		((__force xa_mark_t)8U)
#define XA_MARK_MAX		XA_MARK_2
#define XA_FREE_MARK		XA_MARK_0

enum xa_lock_type {
	XA_LOCK_IRQ = 1,
	XA_LOCK_BH = 2,
};

/*
 * Values for xa_flags.  The radix tree stores its GFP flags in the xa_flags,
 * and we remain compatible with that.
 */
#define XA_FLAGS_LOCK_IRQ	((__force gfp_t)XA_LOCK_IRQ)
#define XA_FLAGS_LOCK_BH	((__force gfp_t)XA_LOCK_BH)
#define XA_FLAGS_TRACK_FREE	((__force gfp_t)4U)
#define XA_FLAGS_ZERO_BUSY	((__force gfp_t)8U)
#define XA_FLAGS_ALLOC_WRAPPED	((__force gfp_t)16U)
#define XA_FLAGS_ACCOUNT	((__force gfp_t)32U)
#define XA_FLAGS_MARK(mark)	((__force gfp_t)((1U << __GFP_BITS_SHIFT) << \
						(__force unsigned)(mark)))

/* ALLOC is for a normal 0-based alloc.  ALLOC1 is for an 1-based alloc */
#define XA_FLAGS_ALLOC	(XA_FLAGS_TRACK_FREE | XA_FLAGS_MARK(XA_FREE_MARK))
#define XA_FLAGS_ALLOC1	(XA_FLAGS_TRACK_FREE | XA_FLAGS_ZERO_BUSY)

/**
 * struct xarray - The anchor of the XArray.
 * @xa_lock: Lock that protects the contents of the XArray.
 *
 * To use the xarray, define it statically or embed it in your data structure.
 * It is a very small data structure, so it does not usually make sense to
 * allocate it separately and keep a pointer to it in your data structure.
 *
 * You may use the xa_lock to protect your own data structures as well.
 */
/*
 * If all of the entries in the array are NULL, @xa_head is a NULL pointer.
 * If the only non-NULL entry in the array is at index 0, @xa_head is that
 * entry.  If any other entry in the array is non-NULL, @xa_head points
 * to an @xa_node.
 */
struct xarray {
	spinlock_t	xa_lock;
/* private: The rest of the data structure is not to be used directly. */
	gfp_t		xa_flags;
	void __rcu *	xa_head;
};

#define XARRAY_INIT(name, flags) {				\
	.xa_lock = __SPIN_LOCK_UNLOCKED(name.xa_lock),		\
	.xa_flags = flags,					\
	.xa_head = NULL,					\
}

/**
 * DEFINE_XARRAY_FLAGS() - Define an XArray with custom flags.
 * @name: A string that names your XArray.
 * @flags: XA_FLAG values.
 *
 * This is intended for file scope definitions of XArrays.  It declares
 * and initialises an empty XArray with the chosen name and flags.  It is
 * equivalent to calling xa_init_flags() on the array, but it does the
 * initialisation at compiletime instead of runtime.
 */
#define DEFINE_XARRAY_FLAGS(name, flags)				\
	struct xarray name = XARRAY_INIT(name, flags)

/**
 * DEFINE_XARRAY() - Define an XArray.
 * @name: A string that names your XArray.
 *
 * This is intended for file scope definitions of XArrays.  It declares
 * and initialises an empty XArray with the chosen name.  It is equivalent
 * to calling xa_init() on the array, but it does the initialisation at
 * compiletime instead of runtime.
 */
#define DEFINE_XARRAY(name) DEFINE_XARRAY_FLAGS(name, 0)

/**
 * DEFINE_XARRAY_ALLOC() - Define an XArray which allocates IDs starting at 0.
 * @name: A string that names your XArray.
 *
 * This is intended for file scope definitions of allocating XArrays.
 * See also DEFINE_XARRAY().
 */
#define DEFINE_XARRAY_ALLOC(name) DEFINE_XARRAY_FLAGS(name, XA_FLAGS_ALLOC)

/**
 * DEFINE_XARRAY_ALLOC1() - Define an XArray which allocates IDs starting at 1.
 * @name: A string that names your XArray.
 *
 * This is intended for file scope definitions of allocating XArrays.
 * See also DEFINE_XARRAY().
 */
#define DEFINE_XARRAY_ALLOC1(name) DEFINE_XARRAY_FLAGS(name, XA_FLAGS_ALLOC1)

void *xa_load(struct xarray *, unsigned long index);
void *xa_store(struct xarray *, unsigned long index, void *entry, gfp_t);
void *xa_erase(struct xarray *, unsigned long index);
void *xa_store_range(struct xarray *, unsigned long first, unsigned long last,
			void *entry, gfp_t);
bool xa_get_mark(struct xarray *, unsigned long index, xa_mark_t);
void xa_set_mark(struct xarray *, unsigned long index, xa_mark_t);
void xa_clear_mark(struct xarray *, unsigned long index, xa_mark_t);
void *xa_find(struct xarray *xa, unsigned long *index,
		unsigned long max, xa_mark_t) __attribute__((nonnull(2)));
void *xa_find_after(struct xarray *xa, unsigned long *index,
		unsigned long max, xa_mark_t) __attribute__((nonnull(2)));
unsigned int xa_extract(struct xarray *, void **dst, unsigned long start,
		unsigned long max, unsigned int n, xa_mark_t);
void xa_destroy(struct xarray *);

/**
 * xa_init_flags() - Initialise an empty XArray with flags.
 * @xa: XArray.
 * @flags: XA_FLAG values.
 *
 * If you need to initialise an XArray with special flags (eg you need
 * to take the lock from interrupt context), use this function instead
 * of xa_init().
 *
 * Context: Any context.
 */
static inline void xa_init_flags(struct xarray *xa, gfp_t flags)
{
	spin_lock_init(&xa->xa_lock);
	xa->xa_flags = flags;
	xa->xa_head = NULL;
}

/**
 * xa_init() - Initialise an empty XArray.
 * @xa: XArray.
 *
 * An empty XArray is full of NULL entries.
 *
 * Context: Any context.
 */
static inline void xa_init(struct xarray *xa)
{
	xa_init_flags(xa, 0);
}

/**
 * xa_empty() - Determine if an array has any present entries.
 * @xa: XArray.
 *
 * Context: Any context.
 * Return: %true if the array contains only NULL pointers.
 */
static inline bool xa_empty(const struct xarray *xa)
{
	return xa->xa_head == NULL;
}

/**
 * xa_marked() - Inquire whether any entry in this array has a mark set
 * @xa: Array
 * @mark: Mark value
 *
 * Context: Any context.
 * Return: %true if any entry has this mark set.
 */
static inline bool xa_marked(const struct xarray *xa, xa_mark_t mark)
{
	return xa->xa_flags & XA_FLAGS_MARK(mark);
}

/**
 * xa_for_each_range() - Iterate over a portion of an XArray.
 * @xa: XArray.
 * @index: Index of @entry.
 * @entry: Entry retrieved from array.
 * @start: First index to retrieve from array.
 * @last: Last index to retrieve from array.
 *
 * During the iteration, @entry will have the value of the entry stored
 * in @xa at @index.  You may modify @index during the iteration if you
 * want to skip or reprocess indices.  It is safe to modify the array
 * during the iteration.  At the end of the iteration, @entry will be set
 * to NULL and @index will have a value less than or equal to max.
 *
 * xa_for_each_range() is O(n.log(n)) while xas_for_each() is O(n).  You have
 * to handle your own locking with xas_for_each(), and if you have to unlock
 * after each iteration, it will also end up being O(n.log(n)).
 * xa_for_each_range() will spin if it hits a retry entry; if you intend to
 * see retry entries, you should use the xas_for_each() iterator instead.
 * The xas_for_each() iterator will expand into more inline code than
 * xa_for_each_range().
 *
 * Context: Any context.  Takes and releases the RCU lock.
 */
#define xa_for_each_range(xa, index, entry, start, last)		\
	for (index = start,						\
	     entry = xa_find(xa, &index, last, XA_PRESENT);		\
	     entry;							\
	     entry = xa_find_after(xa, &index, last, XA_PRESENT))

/**
 * xa_for_each_start() - Iterate over a portion of an XArray.
 * @xa: XArray.
 * @index: Index of @entry.
 * @entry: Entry retrieved from array.
 * @start: First index to retrieve from array.
 *
 * During the iteration, @entry will have the value of the entry stored
 * in @xa at @index.  You may modify @index during the iteration if you
 * want to skip or reprocess indices.  It is safe to modify the array
 * during the iteration.  At the end of the iteration, @entry will be set
 * to NULL and @index will have a value less than or equal to max.
 *
 * xa_for_each_start() is O(n.log(n)) while xas_for_each() is O(n).  You have
 * to handle your own locking with xas_for_each(), and if you have to unlock
 * after each iteration, it will also end up being O(n.log(n)).
 * xa_for_each_start() will spin if it hits a retry entry; if you intend to
 * see retry entries, you should use the xas_for_each() iterator instead.
 * The xas_for_each() iterator will expand into more inline code than
 * xa_for_each_start().
 *
 * Context: Any context.  Takes and releases the RCU lock.
 */
#define xa_for_each_start(xa, index, entry, start) \
	xa_for_each_range(xa, index, entry, start, ULONG_MAX)

/**
 * xa_for_each() - Iterate over present entries in an XArray.
 * @xa: XArray.
 * @index: Index of @entry.
 * @entry: Entry retrieved from array.
 *
 * During the iteration, @entry will have the value of the entry stored
 * in @xa at @index.  You may modify @index during the iteration if you want
 * to skip or reprocess indices.  It is safe to modify the array during the
 * iteration.  At the end of the iteration, @entry will be set to NULL and
 * @index will have a value less than or equal to max.
 *
 * xa_for_each() is O(n.log(n)) while xas_for_each() is O(n).  You have
 * to handle your own locking with xas_for_each(), and if you have to unlock
 * after each iteration, it will also end up being O(n.log(n)).  xa_for_each()
 * will spin if it hits a retry entry; if you intend to see retry entries,
 * you should use the xas_for_each() iterator instead.  The xas_for_each()
 * iterator will expand into more inline code than xa_for_each().
 *
 * Context: Any context.  Takes and releases the RCU lock.
 */
#define xa_for_each(xa, index, entry) \
	xa_for_each_start(xa, index, entry, 0)

/**
 * xa_for_each_marked() - Iterate over marked entries in an XArray.
 * @xa: XArray.
 * @index: Index of @entry.
 * @entry: Entry retrieved from array.
 * @filter: Selection criterion.
 *
 * During the iteration, @entry will have the value of the entry stored
 * in @xa at @index.  The iteration will skip all entries in the array
 * which do not match @filter.  You may modify @index during the iteration
 * if you want to skip or reprocess indices.  It is safe to modify the array
 * during the iteration.  At the end of the iteration, @entry will be set to
 * NULL and @index will have a value less than or equal to max.
 *
 * xa_for_each_marked() is O(n.log(n)) while xas_for_each_marked() is O(n).
 * You have to handle your own locking with xas_for_each(), and if you have
 * to unlock after each iteration, it will also end up being O(n.log(n)).
 * xa_for_each_marked() will spin if it hits a retry entry; if you intend to
 * see retry entries, you should use the xas_for_each_marked() iterator
 * instead.  The xas_for_each_marked() iterator will expand into more inline
 * code than xa_for_each_marked().
 *
 * Context: Any context.  Takes and releases the RCU lock.
 */
#define xa_for_each_marked(xa, index, entry, filter) \
	for (index = 0, entry = xa_find(xa, &index, ULONG_MAX, filter); \
	     entry; entry = xa_find_after(xa, &index, ULONG_MAX, filter))

#define xa_trylock(xa)		spin_trylock(&(xa)->xa_lock)
#define xa_lock(xa)		spin_lock(&(xa)->xa_lock)
#define xa_unlock(xa)		spin_unlock(&(xa)->xa_lock)
#define xa_lock_bh(xa)		spin_lock_bh(&(xa)->xa_lock)
#define xa_unlock_bh(xa)	spin_unlock_bh(&(xa)->xa_lock)
#define xa_lock_irq(xa)		spin_lock_irq(&(xa)->xa_lock)
#define xa_unlock_irq(xa)	spin_unlock_irq(&(xa)->xa_lock)
#define xa_lock_irqsave(xa, flags) \
				spin_lock_irqsave(&(xa)->xa_lock, flags)
#define xa_unlock_irqrestore(xa, flags) \
				spin_unlock_irqrestore(&(xa)->xa_lock, flags)
#define xa_lock_nested(xa, subclass) \
				spin_lock_nested(&(xa)->xa_lock, subclass)
#define xa_lock_bh_nested(xa, subclass) \
				spin_lock_bh_nested(&(xa)->xa_lock, subclass)
#define xa_lock_irq_nested(xa, subclass) \
				spin_lock_irq_nested(&(xa)->xa_lock, subclass)
#define xa_lock_irqsave_nested(xa, flags, subclass) \
		spin_lock_irqsave_nested(&(xa)->xa_lock, flags, subclass)

/*
 * Versions of the normal API which require the caller to hold the
 * xa_lock.  If the GFP flags allow it, they will drop the lock to
 * allocate memory, then reacquire it afterwards.  These functions
 * may also re-enable interrupts if the XArray flags indicate the
 * locking should be interrupt safe.
 */
void *__xa_erase(struct xarray *, unsigned long index);
void *__xa_store(struct xarray *, unsigned long index, void *entry, gfp_t);
void *__xa_cmpxchg(struct xarray *, unsigned long index, void *old,
		void *entry, gfp_t);
int __must_check __xa_insert(struct xarray *, unsigned long index,
		void *entry, gfp_t);
int __must_check __xa_alloc(struct xarray *, u32 *id, void *entry,
		struct xa_limit, gfp_t);
int __must_check __xa_alloc_cyclic(struct xarray *, u32 *id, void *entry,
		struct xa_limit, u32 *next, gfp_t);
void __xa_set_mark(struct xarray *, unsigned long index, xa_mark_t);
void __xa_clear_mark(struct xarray *, unsigned long index, xa_mark_t);

/**
 * xa_store_bh() - Store this entry in the XArray.
 * @xa: XArray.
 * @index: Index into array.
 * @entry: New entry.
 * @gfp: Memory allocation flags.
 *
 * This function is like calling xa_store() except it disables softirqs
 * while holding the array lock.
 *
 * Context: Any context.  Takes and releases the xa_lock while
 * disabling softirqs.
 * Return: The old entry at this index or xa_err() if an error happened.
 */
static inline void *xa_store_bh(struct xarray *xa, unsigned long index,
		void *entry, gfp_t gfp)
{
	void *curr;

	might_alloc(gfp);
	xa_lock_bh(xa);
	curr = __xa_store(xa, index, entry, gfp);
	xa_unlock_bh(xa);

	return curr;
}

/**
 * xa_store_irq() - Store this entry in the XArray.
 * @xa: XArray.
 * @index: Index into array.
 * @entry: New entry.
 * @gfp: Memory allocation flags.
 *
 * This function is like calling xa_store() except it disables interrupts
 * while holding the array lock.
 *
 * Context: Process context.  Takes and releases the xa_lock while
 * disabling interrupts.
 * Return: The old entry at this index or xa_err() if an error happened.
 */
static inline void *xa_store_irq(struct xarray *xa, unsigned long index,
		void *entry, gfp_t gfp)
{
	void *curr;

	might_alloc(gfp);
	xa_lock_irq(xa);
	curr = __xa_store(xa, index, entry, gfp);
	xa_unlock_irq(xa);

	return curr;
}

/**
 * xa_erase_bh() - Erase this entry from the XArray.
 * @xa: XArray.
 * @index: Index of entry.
 *
 * After this function returns, loading from @index will return %NULL.
 * If the index is part of a multi-index entry, all indices will be erased
 * and none of the entries will be part of a multi-index entry.
 *
 * Context: Any context.  Takes and releases the xa_lock while
 * disabling softirqs.
 * Return: The entry which used to be at this index.
 */
static inline void *xa_erase_bh(struct xarray *xa, unsigned long index)
{
	void *entry;

	xa_lock_bh(xa);
	entry = __xa_erase(xa, index);
	xa_unlock_bh(xa);

	return entry;
}

/**
 * xa_erase_irq() - Erase this entry from the XArray.
 * @xa: XArray.
 * @index: Index of entry.
 *
 * After this function returns, loading from @index will return %NULL.
 * If the index is part of a multi-index entry, all indices will be erased
 * and none of the entries will be part of a multi-index entry.
 *
 * Context: Process context.  Takes and releases the xa_lock while
 * disabling interrupts.
 * Return: The entry which used to be at this index.
 */
static inline void *xa_erase_irq(struct xarray *xa, unsigned long index)
{
	void *entry;

	xa_lock_irq(xa);
	entry = __xa_erase(xa, index);
	xa_unlock_irq(xa);

	return entry;
}

/**
 * xa_cmpxchg() - Conditionally replace an entry in the XArray.
 * @xa: XArray.
 * @index: Index into array.
 * @old: Old value to test against.
 * @entry: New value to place in array.
 * @gfp: Memory allocation flags.
 *
 * If the entry at @index is the same as @old, replace it with @entry.
 * If the return value is equal to @old, then the exchange was successful.
 *
 * Context: Any context.  Takes and releases the xa_lock.  May sleep
 * if the @gfp flags permit.
 * Return: The old value at this index or xa_err() if an error happened.
 */
static inline void *xa_cmpxchg(struct xarray *xa, unsigned long index,
			void *old, void *entry, gfp_t gfp)
{
	void *curr;

	might_alloc(gfp);
	xa_lock(xa);
	curr = __xa_cmpxchg(xa, index, old, entry, gfp);
	xa_unlock(xa);

	return curr;
}

/**
 * xa_cmpxchg_bh() - Conditionally replace an entry in the XArray.
 * @xa: XArray.
 * @index: Index into array.
 * @old: Old value to test against.
 * @entry: New value to place in array.
 * @gfp: Memory allocation flags.
 *
 * This function is like calling xa_cmpxchg() except it disables softirqs
 * while holding the array lock.
 *
 * Context: Any context.  Takes and releases the xa_lock while
 * disabling softirqs.  May sleep if the @gfp flags permit.
 * Return: The old value at this index or xa_err() if an error happened.
 */
static inline void *xa_cmpxchg_bh(struct xarray *xa, unsigned long index,
			void *old, void *entry, gfp_t gfp)
{
	void *curr;

	might_alloc(gfp);
	xa_lock_bh(xa);
	curr = __xa_cmpxchg(xa, index, old, entry, gfp);
	xa_unlock_bh(xa);

	return curr;
}

/**
 * xa_cmpxchg_irq() - Conditionally replace an entry in the XArray.
 * @xa: XArray.
 * @index: Index into array.
 * @old: Old value to test against.
 * @entry: New value to place in array.
 * @gfp: Memory allocation flags.
 *
 * This function is like calling xa_cmpxchg() except it disables interrupts
 * while holding the array lock.
 *
 * Context: Process context.  Takes and releases the xa_lock while
 * disabling interrupts.  May sleep if the @gfp flags permit.
 * Return: The old value at this index or xa_err() if an error happened.
 */
static inline void *xa_cmpxchg_irq(struct xarray *xa, unsigned long index,
			void *old, void *entry, gfp_t gfp)
{
	void *curr;

	might_alloc(gfp);
	xa_lock_irq(xa);
	curr = __xa_cmpxchg(xa, index, old, entry, gfp);
	xa_unlock_irq(xa);

	return curr;
}

/**
 * xa_insert() - Store this entry in the XArray unless another entry is
 *			already present.
 * @xa: XArray.
 * @index: Index into array.
 * @entry: New entry.
 * @gfp: Memory allocation flags.
 *
 * Inserting a NULL entry will store a reserved entry (like xa_reserve())
 * if no entry is present.  Inserting will fail if a reserved entry is
 * present, even though loading from this index will return NULL.
 *
 * Context: Any context.  Takes and releases the xa_lock.  May sleep if
 * the @gfp flags permit.
 * Return: 0 if the store succeeded.  -EBUSY if another entry was present.
 * -ENOMEM if memory could not be allocated.
 */
static inline int __must_check xa_insert(struct xarray *xa,
		unsigned long index, void *entry, gfp_t gfp)
{
	int err;

	might_alloc(gfp);
	xa_lock(xa);
	err = __xa_insert(xa, index, entry, gfp);
	xa_unlock(xa);

	return err;
}

/**
 * xa_insert_bh() - Store this entry in the XArray unless another entry is
 *			already present.
 * @xa: XArray.
 * @index: Index into array.
 * @entry: New entry.
 * @gfp: Memory allocation flags.
 *
 * Inserting a NULL entry will store a reserved entry (like xa_reserve())
 * if no entry is present.  Inserting will fail if a reserved entry is
 * present, even though loading from this index will return NULL.
 *
 * Context: Any context.  Takes and releases the xa_lock while
 * disabling softirqs.  May sleep if the @gfp flags permit.
 * Return: 0 if the store succeeded.  -EBUSY if another entry was present.
 * -ENOMEM if memory could not be allocated.
 */
static inline int __must_check xa_insert_bh(struct xarray *xa,
		unsigned long index, void *entry, gfp_t gfp)
{
	int err;

	might_alloc(gfp);
	xa_lock_bh(xa);
	err = __xa_insert(xa, index, entry, gfp);
	xa_unlock_bh(xa);

	return err;
}

/**
 * xa_insert_irq() - Store this entry in the XArray unless another entry is
 *			already present.
 * @xa: XArray.
 * @index: Index into array.
 * @entry: New entry.
 * @gfp: Memory allocation flags.
 *
 * Inserting a NULL entry will store a reserved entry (like xa_reserve())
 * if no entry is present.  Inserting will fail if a reserved entry is
 * present, even though loading from this index will return NULL.
 *
 * Context: Process context.  Takes and releases the xa_lock while
 * disabling interrupts.  May sleep if the @gfp flags permit.
 * Return: 0 if the store succeeded.  -EBUSY if another entry was present.
 * -ENOMEM if memory could not be allocated.
 */
static inline int __must_check xa_insert_irq(struct xarray *xa,
		unsigned long index, void *entry, gfp_t gfp)
{
	int err;

	might_alloc(gfp);
	xa_lock_irq(xa);
	err = __xa_insert(xa, index, entry, gfp);
	xa_unlock_irq(xa);

	return err;
}

/**
 * xa_alloc() - Find somewhere to store this entry in the XArray.
 * @xa: XArray.
 * @id: Pointer to ID.
 * @entry: New entry.
 * @limit: Range of ID to allocate.
 * @gfp: Memory allocation flags.
 *
 * Finds an empty entry in @xa between @limit.min and @limit.max,
 * stores the index into the @id pointer, then stores the entry at
 * that index.  A concurrent lookup will not see an uninitialised @id.
 *
 * Context: Any context.  Takes and releases the xa_lock.  May sleep if
 * the @gfp flags permit.
 * Return: 0 on success, -ENOMEM if memory could not be allocated or
 * -EBUSY if there are no free entries in @limit.
 */
static inline __must_check int xa_alloc(struct xarray *xa, u32 *id,
		void *entry, struct xa_limit limit, gfp_t gfp)
{
	int err;

	might_alloc(gfp);
	xa_lock(xa);
	err = __xa_alloc(xa, id, entry, limit, gfp);
	xa_unlock(xa);

	return err;
}

/**
 * xa_alloc_bh() - Find somewhere to store this entry in the XArray.
 * @xa: XArray.
 * @id: Pointer to ID.
 * @entry: New entry.
 * @limit: Range of ID to allocate.
 * @gfp: Memory allocation flags.
 *
 * Finds an empty entry in @xa between @limit.min and @limit.max,
 * stores the index into the @id pointer, then stores the entry at
 * that index.  A concurrent lookup will not see an uninitialised @id.
 *
 * Context: Any context.  Takes and releases the xa_lock while
 * disabling softirqs.  May sleep if the @gfp flags permit.
 * Return: 0 on success, -ENOMEM if memory could not be allocated or
 * -EBUSY if there are no free entries in @limit.
 */
static inline int __must_check xa_alloc_bh(struct xarray *xa, u32 *id,
		void *entry, struct xa_limit limit, gfp_t gfp)
{
	int err;

	might_alloc(gfp);
	xa_lock_bh(xa);
	err = __xa_alloc(xa, id, entry, limit, gfp);
	xa_unlock_bh(xa);

	return err;
}

/**
 * xa_alloc_irq() - Find somewhere to store this entry in the XArray.
 * @xa: XArray.
 * @id: Pointer to ID.
 * @entry: New entry.
 * @limit: Range of ID to allocate.
 * @gfp: Memory allocation flags.
 *
 * Finds an empty entry in @xa between @limit.min and @limit.max,
 * stores the index into the @id pointer, then stores the entry at
 * that index.  A concurrent lookup will not see an uninitialised @id.
 *
 * Context: Process context.  Takes and releases the xa_lock while
 * disabling interrupts.  May sleep if the @gfp flags permit.
 * Return: 0 on success, -ENOMEM if memory could not be allocated or
 * -EBUSY if there are no free entries in @limit.
 */
static inline int __must_check xa_alloc_irq(struct xarray *xa, u32 *id,
		void *entry, struct xa_limit limit, gfp_t gfp)
{
	int err;

	might_alloc(gfp);
	xa_lock_irq(xa);
	err = __xa_alloc(xa, id, entry, limit, gfp);
	xa_unlock_irq(xa);

	return err;
}

/**
 * xa_alloc_cyclic() - Find somewhere to store this entry in the XArray.
 * @xa: XArray.
 * @id: Pointer to ID.
 * @entry: New entry.
 * @limit: Range of allocated ID.
 * @next: Pointer to next ID to allocate.
 * @gfp: Memory allocation flags.
 *
 * Finds an empty entry in @xa between @limit.min and @limit.max,
 * stores the index into the @id pointer, then stores the entry at
 * that index.  A concurrent lookup will not see an uninitialised @id.
 * The search for an empty entry will start at @next and will wrap
 * around if necessary.
 *
 * Context: Any context.  Takes and releases the xa_lock.  May sleep if
 * the @gfp flags permit.
 * Return: 0 if the allocation succeeded without wrapping.  1 if the
 * allocation succeeded after wrapping, -ENOMEM if memory could not be
 * allocated or -EBUSY if there are no free entries in @limit.
 */
static inline int xa_alloc_cyclic(struct xarray *xa, u32 *id, void *entry,
		struct xa_limit limit, u32 *next, gfp_t gfp)
{
	int err;

	might_alloc(gfp);
	xa_lock(xa);
	err = __xa_alloc_cyclic(xa, id, entry, limit, next, gfp);
	xa_unlock(xa);

	return err;
}

/**
 * xa_alloc_cyclic_bh() - Find somewhere to store this entry in the XArray.
 * @xa: XArray.
 * @id: Pointer to ID.
 * @entry: New entry.
 * @limit: Range of allocated ID.
 * @next: Pointer to next ID to allocate.
 * @gfp: Memory allocation flags.
 *
 * Finds an empty entry in @xa between @limit.min and @limit.max,
 * stores the index into the @id pointer, then stores the entry at
 * that index.  A concurrent lookup will not see an uninitialised @id.
 * The search for an empty entry will start at @next and will wrap
 * around if necessary.
 *
 * Context: Any context.  Takes and releases the xa_lock while
 * disabling softirqs.  May sleep if the @gfp flags permit.
 * Return: 0 if the allocation succeeded without wrapping.  1 if the
 * allocation succeeded after wrapping, -ENOMEM if memory could not be
 * allocated or -EBUSY if there are no free entries in @limit.
 */
static inline int xa_alloc_cyclic_bh(struct xarray *xa, u32 *id, void *entry,
		struct xa_limit limit, u32 *next, gfp_t gfp)
{
	int err;

	might_alloc(gfp);
	xa_lock_bh(xa);
	err = __xa_alloc_cyclic(xa, id, entry, limit, next, gfp);
	xa_unlock_bh(xa);

	return err;
}

/**
 * xa_alloc_cyclic_irq() - Find somewhere to store this entry in the XArray.
 * @xa: XArray.
 * @id: Pointer to ID.
 * @entry: New entry.
 * @limit: Range of allocated ID.
 * @next: Pointer to next ID to allocate.
 * @gfp: Memory allocation flags.
 *
 * Finds an empty entry in @xa between @limit.min and @limit.max,
 * stores the index into the @id pointer, then stores the entry at
 * that index.  A concurrent lookup will not see an uninitialised @id.
 * The search for an empty entry will start at @next and will wrap
 * around if necessary.
 *
 * Context: Process context.  Takes and releases the xa_lock while
 * disabling interrupts.  May sleep if the @gfp flags permit.
 * Return: 0 if the allocation succeeded without wrapping.  1 if the
 * allocation succeeded after wrapping, -ENOMEM if memory could not be
 * allocated or -EBUSY if there are no free entries in @limit.
 */
static inline int xa_alloc_cyclic_irq(struct xarray *xa, u32 *id, void *entry,
		struct xa_limit limit, u32 *next, gfp_t gfp)
{
	int err;

	might_alloc(gfp);
	xa_lock_irq(xa);
	err = __xa_alloc_cyclic(xa, id, entry, limit, next, gfp);
	xa_unlock_irq(xa);

	return err;
}

/**
 * xa_reserve() - Reserve this index in the XArray.
 * @xa: XArray.
 * @index: Index into array.
 * @gfp: Memory allocation flags.
 *
 * Ensures there is somewhere to store an entry at @index in the array.
 * If there is already something stored at @index, this function does
 * nothing.  If there was nothing there, the entry is marked as reserved.
 * Loading from a reserved entry returns a %NULL pointer.
 *
 * If you do not use the entry that you have reserved, call xa_release()
 * or xa_erase() to free any unnecessary memory.
 *
 * Context: Any context.  Takes and releases the xa_lock.
 * May sleep if the @gfp flags permit.
 * Return: 0 if the reservation succeeded or -ENOMEM if it failed.
 */
static inline __must_check
int xa_reserve(struct xarray *xa, unsigned long index, gfp_t gfp)
{
	return xa_err(xa_cmpxchg(xa, index, NULL, XA_ZERO_ENTRY, gfp));
}

/**
 * xa_reserve_bh() - Reserve this index in the XArray.
 * @xa: XArray.
 * @index: Index into array.
 * @gfp: Memory allocation flags.
 *
 * A softirq-disabling version of xa_reserve().
 *
 * Context: Any context.  Takes and releases the xa_lock while
 * disabling softirqs.
 * Return: 0 if the reservation succeeded or -ENOMEM if it failed.
 */
static inline __must_check
int xa_reserve_bh(struct xarray *xa, unsigned long index, gfp_t gfp)
{
	return xa_err(xa_cmpxchg_bh(xa, index, NULL, XA_ZERO_ENTRY, gfp));
}

/**
 * xa_reserve_irq() - Reserve this index in the XArray.
 * @xa: XArray.
 * @index: Index into array.
 * @gfp: Memory allocation flags.
 *
 * An interrupt-disabling version of xa_reserve().
 *
 * Context: Process context.  Takes and releases the xa_lock while
 * disabling interrupts.
 * Return: 0 if the reservation succeeded or -ENOMEM if it failed.
 */
static inline __must_check
int xa_reserve_irq(struct xarray *xa, unsigned long index, gfp_t gfp)
{
	return xa_err(xa_cmpxchg_irq(xa, index, NULL, XA_ZERO_ENTRY, gfp));
}

/**
 * xa_release() - Release a reserved entry.
 * @xa: XArray.
 * @index: Index of entry.
 *
 * After calling xa_reserve(), you can call this function to release the
 * reservation.  If the entry at @index has been stored to, this function
 * will do nothing.
 */
static inline void xa_release(struct xarray *xa, unsigned long index)
{
	xa_cmpxchg(xa, index, XA_ZERO_ENTRY, NULL, 0);
}

/* Everything below here is the Advanced API.  Proceed with caution. */

/*
 * The xarray is constructed out of a set of 'chunks' of pointers.  Choosing
 * the best chunk size requires some tradeoffs.  A power of two recommends
 * itself so that we can walk the tree based purely on shifts and masks.
 * Generally, the larger the better; as the number of slots per level of the
 * tree increases, the less tall the tree needs to be.  But that needs to be
 * balanced against the memory consumption of each node.  On a 64-bit system,
 * xa_node is currently 576 bytes, and we get 7 of them per 4kB page.  If we
 * doubled the number of slots per node, we'd get only 3 nodes per 4kB page.
 */
#ifndef XA_CHUNK_SHIFT
#define XA_CHUNK_SHIFT		(CONFIG_BASE_SMALL ? 4 : 6)
#endif
#define XA_CHUNK_SIZE		(1UL << XA_CHUNK_SHIFT)
#define XA_CHUNK_MASK		(XA_CHUNK_SIZE - 1)
#define XA_MAX_MARKS		3
#define XA_MARK_LONGS		DIV_ROUND_UP(XA_CHUNK_SIZE, BITS_PER_LONG)

/*
 * @count is the count of every non-NULL element in the ->slots array
 * whether that is a value entry, a retry entry, a user pointer,
 * a sibling entry or a pointer to the next level of the tree.
 * @nr_values is the count of every element in ->slots which is
 * either a value entry or a sibling of a value entry.
 */
struct xa_node {
	unsigned char	shift;		/* Bits remaining in each slot */
	unsigned char	offset;		/* Slot offset in parent */
	unsigned char	count;		/* Total entry count */
	unsigned char	nr_values;	/* Value entry count */
	struct xa_node __rcu *parent;	/* NULL at top of tree */
	struct xarray	*array;		/* The array we belong to */
	union {
		struct list_head private_list;	/* For tree user */
		struct rcu_head	rcu_head;	/* Used when freeing node */
	};
	void __rcu	*slots[XA_CHUNK_SIZE];
	union {
		unsigned long	tags[XA_MAX_MARKS][XA_MARK_LONGS];
		unsigned long	marks[XA_MAX_MARKS][XA_MARK_LONGS];
	};
};

void xa_dump(const struct xarray *);
void xa_dump_node(const struct xa_node *);

#ifdef XA_DEBUG
#define XA_BUG_ON(xa, x) do {					\
		if (x) {					\
			xa_dump(xa);				\
			BUG();					\
		}						\
	} while (0)
#define XA_NODE_BUG_ON(node, x) do {				\
		if (x) {					\
			if (node) xa_dump_node(node);		\
			BUG();					\
		}						\
	} while (0)
#else
#define XA_BUG_ON(xa, x)	do { } while (0)
#define XA_NODE_BUG_ON(node, x)	do { } while (0)
#endif

/* Private */
static inline void *xa_head(const struct xarray *xa)
{
	return rcu_dereference_check(xa->xa_head,
						lockdep_is_held(&xa->xa_lock));
}

/* Private */
static inline void *xa_head_locked(const struct xarray *xa)
{
	return rcu_dereference_protected(xa->xa_head,
						lockdep_is_held(&xa->xa_lock));
}

/* Private */
static inline void *xa_entry(const struct xarray *xa,
				const struct xa_node *node, unsigned int offset)
{
	XA_NODE_BUG_ON(node, offset >= XA_CHUNK_SIZE);
	return rcu_dereference_check(node->slots[offset],
						lockdep_is_held(&xa->xa_lock));
}

/* Private */
static inline void *xa_entry_locked(const struct xarray *xa,
				const struct xa_node *node, unsigned int offset)
{
	XA_NODE_BUG_ON(node, offset >= XA_CHUNK_SIZE);
	return rcu_dereference_protected(node->slots[offset],
						lockdep_is_held(&xa->xa_lock));
}

/* Private */
static inline struct xa_node *xa_parent(const struct xarray *xa,
					const struct xa_node *node)
{
	return rcu_dereference_check(node->parent,
						lockdep_is_held(&xa->xa_lock));
}

/* Private */
static inline struct xa_node *xa_parent_locked(const struct xarray *xa,
					const struct xa_node *node)
{
	return rcu_dereference_protected(node->parent,
						lockdep_is_held(&xa->xa_lock));
}

/* Private */
static inline void *xa_mk_node(const struct xa_node *node)
{
	return (void *)((unsigned long)node | 2);
}

/* Private */
static inline struct xa_node *xa_to_node(const void *entry)
{
	return (struct xa_node *)((unsigned long)entry - 2);
}

/* Private */
static inline bool xa_is_node(const void *entry)
{
	return xa_is_internal(entry) && (unsigned long)entry > 4096;
}

/* Private */
static inline void *xa_mk_sibling(unsigned int offset)
{
	return xa_mk_internal(offset);
}

/* Private */
static inline unsigned long xa_to_sibling(const void *entry)
{
	return xa_to_internal(entry);
}

/**
 * xa_is_sibling() - Is the entry a sibling entry?
 * @entry: Entry retrieved from the XArray
 *
 * Return: %true if the entry is a sibling entry.
 */
static inline bool xa_is_sibling(const void *entry)
{
	return IS_ENABLED(CONFIG_XARRAY_MULTI) && xa_is_internal(entry) &&
		(entry < xa_mk_sibling(XA_CHUNK_SIZE - 1));
}

#define XA_RETRY_ENTRY		xa_mk_internal(256)

/**
 * xa_is_retry() - Is the entry a retry entry?
 * @entry: Entry retrieved from the XArray
 *
 * Return: %true if the entry is a retry entry.
 */
static inline bool xa_is_retry(const void *entry)
{
	return unlikely(entry == XA_RETRY_ENTRY);
}

/**
 * xa_is_advanced() - Is the entry only permitted for the advanced API?
 * @entry: Entry to be stored in the XArray.
 *
 * Return: %true if the entry cannot be stored by the normal API.
 */
static inline bool xa_is_advanced(const void *entry)
{
	return xa_is_internal(entry) && (entry <= XA_RETRY_ENTRY);
}

/**
 * typedef xa_update_node_t - A callback function from the XArray.
 * @node: The node which is being processed
 *
 * This function is called every time the XArray updates the count of
 * present and value entries in a node.  It allows advanced users to
 * maintain the private_list in the node.
 *
 * Context: The xa_lock is held and interrupts may be disabled.
 *	    Implementations should not drop the xa_lock, nor re-enable
 *	    interrupts.
 */
typedef void (*xa_update_node_t)(struct xa_node *node);

void xa_delete_node(struct xa_node *, xa_update_node_t);

/*
 * The xa_state is opaque to its users.  It contains various different pieces
 * of state involved in the current operation on the XArray.  It should be
 * declared on the stack and passed between the various internal routines.
 * The various elements in it should not be accessed directly, but only
 * through the provided accessor functions.  The below documentation is for
 * the benefit of those working on the code, not for users of the XArray.
 *
 * @xa_node usually points to the xa_node containing the slot we're operating
 * on (and @xa_offset is the offset in the slots array).  If there is a
 * single entry in the array at index 0, there are no allocated xa_nodes to
 * point to, and so we store %NULL in @xa_node.  @xa_node is set to
 * the value %XAS_RESTART if the xa_state is not walked to the correct
 * position in the tree of nodes for this operation.  If an error occurs
 * during an operation, it is set to an %XAS_ERROR value.  If we run off the
 * end of the allocated nodes, it is set to %XAS_BOUNDS.
 */
struct xa_state {
	struct xarray *xa;
	unsigned long xa_index;
	unsigned char xa_shift;
	unsigned char xa_sibs;
	unsigned char xa_offset;
	unsigned char xa_pad;		/* Helps gcc generate better code */
	struct xa_node *xa_node;
	struct xa_node *xa_alloc;
	xa_update_node_t xa_update;
	struct list_lru *xa_lru;
};

/*
 * We encode errnos in the xas->xa_node.  If an error has happened, we need to
 * drop the lock to fix it, and once we've done so the xa_state is invalid.
 */
#define XA_ERROR(errno) ((struct xa_node *)(((unsigned long)errno << 2) | 2UL))
#define XAS_BOUNDS	((struct xa_node *)1UL)
#define XAS_RESTART	((struct xa_node *)3UL)

#define __XA_STATE(array, index, shift, sibs)  {	\
	.xa = array,					\
	.xa_index = index,				\
	.xa_shift = shift,				\
	.xa_sibs = sibs,				\
	.xa_offset = 0,					\
	.xa_pad = 0,					\
	.xa_node = XAS_RESTART,				\
	.xa_alloc = NULL,				\
	.xa_update = NULL,				\
	.xa_lru = NULL,					\
}

/**
 * XA_STATE() - Declare an XArray operation state.
 * @name: Name of this operation state (usually xas).
 * @array: Array to operate on.
 * @index: Initial index of interest.
 *
 * Declare and initialise an xa_state on the stack.
 */
#define XA_STATE(name, array, index)				\
	struct xa_state name = __XA_STATE(array, index, 0, 0)

/**
 * XA_STATE_ORDER() - Declare an XArray operation state.
 * @name: Name of this operation state (usually xas).
 * @array: Array to operate on.
 * @index: Initial index of interest.
 * @order: Order of entry.
 *
 * Declare and initialise an xa_state on the stack.  This variant of
 * XA_STATE() allows you to specify the 'order' of the element you
 * want to operate on.`
 */
#define XA_STATE_ORDER(name, array, index, order)		\
	struct xa_state name = __XA_STATE(array,		\
			(index >> order) << order,		\
			order - (order % XA_CHUNK_SHIFT),	\
			(1U << (order % XA_CHUNK_SHIFT)) - 1)

#define xas_marked(xas, mark)	xa_marked((xas)->xa, (mark))
#define xas_trylock(xas)	xa_trylock((xas)->xa)
#define xas_lock(xas)		xa_lock((xas)->xa)
#define xas_unlock(xas)		xa_unlock((xas)->xa)
#define xas_lock_bh(xas)	xa_lock_bh((xas)->xa)
#define xas_unlock_bh(xas)	xa_unlock_bh((xas)->xa)
#define xas_lock_irq(xas)	xa_lock_irq((xas)->xa)
#define xas_unlock_irq(xas)	xa_unlock_irq((xas)->xa)
#define xas_lock_irqsave(xas, flags) \
				xa_lock_irqsave((xas)->xa, flags)
#define xas_unlock_irqrestore(xas, flags) \
				xa_unlock_irqrestore((xas)->xa, flags)

/**
 * xas_error() - Return an errno stored in the xa_state.
 * @xas: XArray operation state.
 *
 * Return: 0 if no error has been noted.  A negative errno if one has.
 */
static inline int xas_error(const struct xa_state *xas)
{
	return xa_err(xas->xa_node);
}

/**
 * xas_set_err() - Note an error in the xa_state.
 * @xas: XArray operation state.
 * @err: Negative error number.
 *
 * Only call this function with a negative @err; zero or positive errors
 * will probably not behave the way you think they should.  If you want
 * to clear the error from an xa_state, use xas_reset().
 */
static inline void xas_set_err(struct xa_state *xas, long err)
{
	xas->xa_node = XA_ERROR(err);
}

/**
 * xas_invalid() - Is the xas in a retry or error state?
 * @xas: XArray operation state.
 *
 * Return: %true if the xas cannot be used for operations.
 */
static inline bool xas_invalid(const struct xa_state *xas)
{
	return (unsigned long)xas->xa_node & 3;
}

/**
 * xas_valid() - Is the xas a valid cursor into the array?
 * @xas: XArray operation state.
 *
 * Return: %true if the xas can be used for operations.
 */
static inline bool xas_valid(const struct xa_state *xas)
{
	return !xas_invalid(xas);
}

/**
 * xas_is_node() - Does the xas point to a node?
 * @xas: XArray operation state.
 *
 * Return: %true if the xas currently references a node.
 */
static inline bool xas_is_node(const struct xa_state *xas)
{
	return xas_valid(xas) && xas->xa_node;
}

/* True if the pointer is something other than a node */
static inline bool xas_not_node(struct xa_node *node)
{
	return ((unsigned long)node & 3) || !node;
}

/* True if the node represents RESTART or an error */
static inline bool xas_frozen(struct xa_node *node)
{
	return (unsigned long)node & 2;
}

/* True if the node represents head-of-tree, RESTART or BOUNDS */
static inline bool xas_top(struct xa_node *node)
{
	return node <= XAS_RESTART;
}

/**
 * xas_reset() - Reset an XArray operation state.
 * @xas: XArray operation state.
 *
 * Resets the error or walk state of the @xas so future walks of the
 * array will start from the root.  Use this if you have dropped the
 * xarray lock and want to reuse the xa_state.
 *
 * Context: Any context.
 */
static inline void xas_reset(struct xa_state *xas)
{
	xas->xa_node = XAS_RESTART;
}

/**
 * xas_retry() - Retry the operation if appropriate.
 * @xas: XArray operation state.
 * @entry: Entry from xarray.
 *
 * The advanced functions may sometimes return an internal entry, such as
 * a retry entry or a zero entry.  This function sets up the @xas to restart
 * the walk from the head of the array if needed.
 *
 * Context: Any context.
 * Return: true if the operation needs to be retried.
 */
static inline bool xas_retry(struct xa_state *xas, const void *entry)
{
	if (xa_is_zero(entry))
		return true;
	if (!xa_is_retry(entry))
		return false;
	xas_reset(xas);
	return true;
}

void *xas_load(struct xa_state *);
void *xas_store(struct xa_state *, void *entry);
void *xas_find(struct xa_state *, unsigned long max);
void *xas_find_conflict(struct xa_state *);

bool xas_get_mark(const struct xa_state *, xa_mark_t);
void xas_set_mark(const struct xa_state *, xa_mark_t);
void xas_clear_mark(const struct xa_state *, xa_mark_t);
void *xas_find_marked(struct xa_state *, unsigned long max, xa_mark_t);
void xas_init_marks(const struct xa_state *);

bool xas_nomem(struct xa_state *, gfp_t);
void xas_destroy(struct xa_state *);
void xas_pause(struct xa_state *);

void xas_create_range(struct xa_state *);

#ifdef CONFIG_XARRAY_MULTI
int xa_get_order(struct xarray *, unsigned long index);
void xas_split(struct xa_state *, void *entry, unsigned int order);
void xas_split_alloc(struct xa_state *, void *entry, unsigned int order, gfp_t);
#else
static inline int xa_get_order(struct xarray *xa, unsigned long index)
{
	return 0;
}

static inline void xas_split(struct xa_state *xas, void *entry,
		unsigned int order)
{
	xas_store(xas, entry);
}

static inline void xas_split_alloc(struct xa_state *xas, void *entry,
		unsigned int order, gfp_t gfp)
{
}
#endif

/**
 * xas_reload() - Refetch an entry from the xarray.
 * @xas: XArray operation state.
 *
 * Use this function to check that a previously loaded entry still has
 * the same value.  This is useful for the lockless pagecache lookup where
 * we walk the array with only the RCU lock to protect us, lock the page,
 * then check that the page hasn't moved since we looked it up.
 *
 * The caller guarantees that @xas is still valid.  If it may be in an
 * error or restart state, call xas_load() instead.
 *
 * Return: The entry at this location in the xarray.
 */
static inline void *xas_reload(struct xa_state *xas)
{
	struct xa_node *node = xas->xa_node;
	void *entry;
	char offset;

	if (!node)
		return xa_head(xas->xa);
	if (IS_ENABLED(CONFIG_XARRAY_MULTI)) {
		offset = (xas->xa_index >> node->shift) & XA_CHUNK_MASK;
		entry = xa_entry(xas->xa, node, offset);
		if (!xa_is_sibling(entry))
			return entry;
		offset = xa_to_sibling(entry);
	} else {
		offset = xas->xa_offset;
	}
	return xa_entry(xas->xa, node, offset);
}

/**
 * xas_set() - Set up XArray operation state for a different index.
 * @xas: XArray operation state.
 * @index: New index into the XArray.
 *
 * Move the operation state to refer to a different index.  This will
 * have the effect of starting a walk from the top; see xas_next()
 * to move to an adjacent index.
 */
static inline void xas_set(struct xa_state *xas, unsigned long index)
{
	xas->xa_index = index;
	xas->xa_node = XAS_RESTART;
}

/**
 * xas_advance() - Skip over sibling entries.
 * @xas: XArray operation state.
 * @index: Index of last sibling entry.
 *
 * Move the operation state to refer to the last sibling entry.
 * This is useful for loops that normally want to see sibling
 * entries but sometimes want to skip them.  Use xas_set() if you
 * want to move to an index which is not part of this entry.
 */
static inline void xas_advance(struct xa_state *xas, unsigned long index)
{
	unsigned char shift = xas_is_node(xas) ? xas->xa_node->shift : 0;

	xas->xa_index = index;
	xas->xa_offset = (index >> shift) & XA_CHUNK_MASK;
}

/**
 * xas_set_order() - Set up XArray operation state for a multislot entry.
 * @xas: XArray operation state.
 * @index: Target of the operation.
 * @order: Entry occupies 2^@order indices.
 */
static inline void xas_set_order(struct xa_state *xas, unsigned long index,
					unsigned int order)
{
#ifdef CONFIG_XARRAY_MULTI
	xas->xa_index = order < BITS_PER_LONG ? (index >> order) << order : 0;
	xas->xa_shift = order - (order % XA_CHUNK_SHIFT);
	xas->xa_sibs = (1 << (order % XA_CHUNK_SHIFT)) - 1;
	xas->xa_node = XAS_RESTART;
#else
	BUG_ON(order > 0);
	xas_set(xas, index);
#endif
}

/**
 * xas_set_update() - Set up XArray operation state for a callback.
 * @xas: XArray operation state.
 * @update: Function to call when updating a node.
 *
 * The XArray can notify a caller after it has updated an xa_node.
 * This is advanced functionality and is only needed by the page cache.
 */
static inline void xas_set_update(struct xa_state *xas, xa_update_node_t update)
{
	xas->xa_update = update;
}

static inline void xas_set_lru(struct xa_state *xas, struct list_lru *lru)
{
	xas->xa_lru = lru;
}

/**
 * xas_next_entry() - Advance iterator to next present entry.
 * @xas: XArray operation state.
 * @max: Highest index to return.
 *
 * xas_next_entry() is an inline function to optimise xarray traversal for
 * speed.  It is equivalent to calling xas_find(), and will call xas_find()
 * for all the hard cases.
 *
 * Return: The next present entry after the one currently referred to by @xas.
 */
static inline void *xas_next_entry(struct xa_state *xas, unsigned long max)
{
	struct xa_node *node = xas->xa_node;
	void *entry;

	if (unlikely(xas_not_node(node) || node->shift ||
			xas->xa_offset != (xas->xa_index & XA_CHUNK_MASK)))
		return xas_find(xas, max);

	do {
		if (unlikely(xas->xa_index >= max))
			return xas_find(xas, max);
		if (unlikely(xas->xa_offset == XA_CHUNK_MASK))
			return xas_find(xas, max);
		entry = xa_entry(xas->xa, node, xas->xa_offset + 1);
		if (unlikely(xa_is_internal(entry)))
			return xas_find(xas, max);
		xas->xa_offset++;
		xas->xa_index++;
	} while (!entry);

	return entry;
}

/* Private */
static inline unsigned int xas_find_chunk(struct xa_state *xas, bool advance,
		xa_mark_t mark)
{
	unsigned long *addr = xas->xa_node->marks[(__force unsigned)mark];
	unsigned int offset = xas->xa_offset;

	if (advance)
		offset++;
	if (XA_CHUNK_SIZE == BITS_PER_LONG) {
		if (offset < XA_CHUNK_SIZE) {
			unsigned long data = *addr & (~0UL << offset);
			if (data)
				return __ffs(data);
		}
		return XA_CHUNK_SIZE;
	}

	return find_next_bit(addr, XA_CHUNK_SIZE, offset);
}

/**
 * xas_next_marked() - Advance iterator to next marked entry.
 * @xas: XArray operation state.
 * @max: Highest index to return.
 * @mark: Mark to search for.
 *
 * xas_next_marked() is an inline function to optimise xarray traversal for
 * speed.  It is equivalent to calling xas_find_marked(), and will call
 * xas_find_marked() for all the hard cases.
 *
 * Return: The next marked entry after the one currently referred to by @xas.
 */
static inline void *xas_next_marked(struct xa_state *xas, unsigned long max,
								xa_mark_t mark)
{
	struct xa_node *node = xas->xa_node;
	void *entry;
	unsigned int offset;

	if (unlikely(xas_not_node(node) || node->shift))
		return xas_find_marked(xas, max, mark);
	offset = xas_find_chunk(xas, true, mark);
	xas->xa_offset = offset;
	xas->xa_index = (xas->xa_index & ~XA_CHUNK_MASK) + offset;
	if (xas->xa_index > max)
		return NULL;
	if (offset == XA_CHUNK_SIZE)
		return xas_find_marked(xas, max, mark);
	entry = xa_entry(xas->xa, node, offset);
	if (!entry)
		return xas_find_marked(xas, max, mark);
	return entry;
}

/*
 * If iterating while holding a lock, drop the lock and reschedule
 * every %XA_CHECK_SCHED loops.
 */
enum {
	XA_CHECK_SCHED = 4096,
};

/**
 * xas_for_each() - Iterate over a range of an XArray.
 * @xas: XArray operation state.
 * @entry: Entry retrieved from the array.
 * @max: Maximum index to retrieve from array.
 *
 * The loop body will be executed for each entry present in the xarray
 * between the current xas position and @max.  @entry will be set to
 * the entry retrieved from the xarray.  It is safe to delete entries
 * from the array in the loop body.  You should hold either the RCU lock
 * or the xa_lock while iterating.  If you need to drop the lock, call
 * xas_pause() first.
 */
#define xas_for_each(xas, entry, max) \
	for (entry = xas_find(xas, max); entry; \
	     entry = xas_next_entry(xas, max))

/**
 * xas_for_each_marked() - Iterate over a range of an XArray.
 * @xas: XArray operation state.
 * @entry: Entry retrieved from the array.
 * @max: Maximum index to retrieve from array.
 * @mark: Mark to search for.
 *
 * The loop body will be executed for each marked entry in the xarray
 * between the current xas position and @max.  @entry will be set to
 * the entry retrieved from the xarray.  It is safe to delete entries
 * from the array in the loop body.  You should hold either the RCU lock
 * or the xa_lock while iterating.  If you need to drop the lock, call
 * xas_pause() first.
 */
#define xas_for_each_marked(xas, entry, max, mark) \
	for (entry = xas_find_marked(xas, max, mark); entry; \
	     entry = xas_next_marked(xas, max, mark))

/**
 * xas_for_each_conflict() - Iterate over a range of an XArray.
 * @xas: XArray operation state.
 * @entry: Entry retrieved from the array.
 *
 * The loop body will be executed for each entry in the XArray that
 * lies within the range specified by @xas.  If the loop terminates
 * normally, @entry will be %NULL.  The user may break out of the loop,
 * which will leave @entry set to the conflicting entry.  The caller
 * may also call xa_set_err() to exit the loop while setting an error
 * to record the reason.
 */
#define xas_for_each_conflict(xas, entry) \
	while ((entry = xas_find_conflict(xas)))

void *__xas_next(struct xa_state *);
void *__xas_prev(struct xa_state *);

/**
 * xas_prev() - Move iterator to previous index.
 * @xas: XArray operation state.
 *
 * If the @xas was in an error state, it will remain in an error state
 * and this function will return %NULL.  If the @xas has never been walked,
 * it will have the effect of calling xas_load().  Otherwise one will be
 * subtracted from the index and the state will be walked to the correct
 * location in the array for the next operation.
 *
 * If the iterator was referencing index 0, this function wraps
 * around to %ULONG_MAX.
 *
 * Return: The entry at the new index.  This may be %NULL or an internal
 * entry.
 */
static inline void *xas_prev(struct xa_state *xas)
{
	struct xa_node *node = xas->xa_node;

	if (unlikely(xas_not_node(node) || node->shift ||
				xas->xa_offset == 0))
		return __xas_prev(xas);

	xas->xa_index--;
	xas->xa_offset--;
	return xa_entry(xas->xa, node, xas->xa_offset);
}

/**
 * xas_next() - Move state to next index.
 * @xas: XArray operation state.
 *
 * If the @xas was in an error state, it will remain in an error state
 * and this function will return %NULL.  If the @xas has never been walked,
 * it will have the effect of calling xas_load().  Otherwise one will be
 * added to the index and the state will be walked to the correct
 * location in the array for the next operation.
 *
 * If the iterator was referencing index %ULONG_MAX, this function wraps
 * around to 0.
 *
 * Return: The entry at the new index.  This may be %NULL or an internal
 * entry.
 */
static inline void *xas_next(struct xa_state *xas)
{
	struct xa_node *node = xas->xa_node;

	if (unlikely(xas_not_node(node) || node->shift ||
				xas->xa_offset == XA_CHUNK_MASK))
		return __xas_next(xas);

	xas->xa_index++;
	xas->xa_offset++;
	return xa_entry(xas->xa, node, xas->xa_offset);
}

#endif /* _LINUX_XARRAY_H */
