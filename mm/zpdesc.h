/* SPDX-License-Identifier: GPL-2.0 */
/* zpdesc.h: zswap.zpool memory descriptor
 *
 * Written by Alex Shi <alexs@kernel.org>
 *	      Hyeonggon Yoo <42.hyeyoo@gmail.com>
 */
#ifndef __MM_ZPDESC_H__
#define __MM_ZPDESC_H__

/*
 * struct zpdesc -	Memory descriptor for zpool memory.
 * @flags:		Page flags, mostly unused by zsmalloc.
 * @lru:		Indirectly used by page migration.
 * @movable_ops:	Used by page migration.
 * @next:		Next zpdesc in a zspage in zsmalloc zpool.
 * @handle:		For huge zspage in zsmalloc zpool.
 * @zspage:		Points to the zspage this zpdesc is a part of.
 * @first_obj_offset:	First object offset in zsmalloc zpool.
 * @_refcount:		The number of references to this zpdesc.
 *
 * This struct overlays struct page for now. Do not modify without a good
 * understanding of the issues. In particular, do not expand into the overlap
 * with memcg_data.
 *
 * Page flags used:
 * * PG_private identifies the first component page.
 * * PG_locked is used by page migration code.
 */
struct zpdesc {
	unsigned long flags;
	struct list_head lru;
	unsigned long movable_ops;
	union {
		struct zpdesc *next;
		unsigned long handle;
	};
	struct zspage *zspage;
	/*
	 * Only the lower 24 bits are available for offset, limiting a page
	 * to 16 MiB. The upper 8 bits are reserved for PGTY_zsmalloc.
	 *
	 * Do not access this field directly.
	 * Instead, use {get,set}_first_obj_offset() helpers.
	 */
	unsigned int first_obj_offset;
	atomic_t _refcount;
};
#define ZPDESC_MATCH(pg, zp) \
	static_assert(offsetof(struct page, pg) == offsetof(struct zpdesc, zp))

ZPDESC_MATCH(flags, flags);
ZPDESC_MATCH(lru, lru);
ZPDESC_MATCH(mapping, movable_ops);
ZPDESC_MATCH(index, next);
ZPDESC_MATCH(index, handle);
ZPDESC_MATCH(private, zspage);
ZPDESC_MATCH(page_type, first_obj_offset);
ZPDESC_MATCH(_refcount, _refcount);
#undef ZPDESC_MATCH
static_assert(sizeof(struct zpdesc) <= sizeof(struct page));

/*
 * zpdesc_page - The first struct page allocated for a zpdesc
 * @zp: The zpdesc.
 *
 * A convenience wrapper for converting zpdesc to the first struct page of the
 * underlying folio, to communicate with code not yet converted to folio or
 * struct zpdesc.
 *
 */
#define zpdesc_page(zp)			(_Generic((zp),			\
	const struct zpdesc *:		(const struct page *)(zp),	\
	struct zpdesc *:		(struct page *)(zp)))

/**
 * zpdesc_folio - The folio allocated for a zpdesc
 * @zp: The zpdesc.
 *
 * Zpdescs are descriptors for zpool memory. The zpool memory itself is
 * allocated as folios that contain the zpool objects, and zpdesc uses specific
 * fields in the first struct page of the folio - those fields are now accessed
 * by struct zpdesc.
 *
 * It is occasionally necessary convert to back to a folio in order to
 * communicate with the rest of the mm. Please use this helper function
 * instead of casting yourself, as the implementation may change in the future.
 */
#define zpdesc_folio(zp)		(_Generic((zp),			\
	const struct zpdesc *:		(const struct folio *)(zp),	\
	struct zpdesc *:		(struct folio *)(zp)))
/**
 * page_zpdesc - Converts from first struct page to zpdesc.
 * @p: The first (either head of compound or single) page of zpdesc.
 *
 * A temporary wrapper to convert struct page to struct zpdesc in situations
 * where we know the page is the compound head, or single order-0 page.
 *
 * Long-term ideally everything would work with struct zpdesc directly or go
 * through folio to struct zpdesc.
 *
 * Return: The zpdesc which contains this page
 */
#define page_zpdesc(p)			(_Generic((p),			\
	const struct page *:		(const struct zpdesc *)(p),	\
	struct page *:			(struct zpdesc *)(p)))

static inline void zpdesc_lock(struct zpdesc *zpdesc)
{
	folio_lock(zpdesc_folio(zpdesc));
}

static inline bool zpdesc_trylock(struct zpdesc *zpdesc)
{
	return folio_trylock(zpdesc_folio(zpdesc));
}

static inline void zpdesc_unlock(struct zpdesc *zpdesc)
{
	folio_unlock(zpdesc_folio(zpdesc));
}

static inline void zpdesc_wait_locked(struct zpdesc *zpdesc)
{
	folio_wait_locked(zpdesc_folio(zpdesc));
}

static inline void zpdesc_get(struct zpdesc *zpdesc)
{
	folio_get(zpdesc_folio(zpdesc));
}

static inline void zpdesc_put(struct zpdesc *zpdesc)
{
	folio_put(zpdesc_folio(zpdesc));
}

static inline void *kmap_local_zpdesc(struct zpdesc *zpdesc)
{
	return kmap_local_page(zpdesc_page(zpdesc));
}

static inline unsigned long zpdesc_pfn(struct zpdesc *zpdesc)
{
	return page_to_pfn(zpdesc_page(zpdesc));
}

static inline struct zpdesc *pfn_zpdesc(unsigned long pfn)
{
	return page_zpdesc(pfn_to_page(pfn));
}

static inline void __zpdesc_set_movable(struct zpdesc *zpdesc,
					const struct movable_operations *mops)
{
	__SetPageMovable(zpdesc_page(zpdesc), mops);
}

static inline void __zpdesc_set_zsmalloc(struct zpdesc *zpdesc)
{
	__SetPageZsmalloc(zpdesc_page(zpdesc));
}

static inline void __zpdesc_clear_zsmalloc(struct zpdesc *zpdesc)
{
	__ClearPageZsmalloc(zpdesc_page(zpdesc));
}

static inline bool zpdesc_is_isolated(struct zpdesc *zpdesc)
{
	return PageIsolated(zpdesc_page(zpdesc));
}

static inline struct zone *zpdesc_zone(struct zpdesc *zpdesc)
{
	return page_zone(zpdesc_page(zpdesc));
}

static inline bool zpdesc_is_locked(struct zpdesc *zpdesc)
{
	return folio_test_locked(zpdesc_folio(zpdesc));
}
#endif
