#ifndef _LINUX_FRONTSWAP_H
#define _LINUX_FRONTSWAP_H

#include <linux/swap.h>
#include <linux/mm.h>
#include <linux/bitops.h>

struct frontswap_ops {
	void (*init)(unsigned);
	int (*put_page)(unsigned, pgoff_t, struct page *);
	int (*get_page)(unsigned, pgoff_t, struct page *);
	void (*invalidate_page)(unsigned, pgoff_t);
	void (*invalidate_area)(unsigned);
};

extern bool frontswap_enabled;
extern struct frontswap_ops
	frontswap_register_ops(struct frontswap_ops *ops);
extern void frontswap_shrink(unsigned long);
extern unsigned long frontswap_curr_pages(void);
extern void frontswap_writethrough(bool);

extern void __frontswap_init(unsigned type);
extern int __frontswap_put_page(struct page *page);
extern int __frontswap_get_page(struct page *page);
extern void __frontswap_invalidate_page(unsigned, pgoff_t);
extern void __frontswap_invalidate_area(unsigned);

#ifdef CONFIG_FRONTSWAP

static inline bool frontswap_test(struct swap_info_struct *sis, pgoff_t offset)
{
	bool ret = false;

	if (frontswap_enabled && sis->frontswap_map)
		ret = test_bit(offset, sis->frontswap_map);
	return ret;
}

static inline void frontswap_set(struct swap_info_struct *sis, pgoff_t offset)
{
	if (frontswap_enabled && sis->frontswap_map)
		set_bit(offset, sis->frontswap_map);
}

static inline void frontswap_clear(struct swap_info_struct *sis, pgoff_t offset)
{
	if (frontswap_enabled && sis->frontswap_map)
		clear_bit(offset, sis->frontswap_map);
}

static inline void frontswap_map_set(struct swap_info_struct *p,
				     unsigned long *map)
{
	p->frontswap_map = map;
}

static inline unsigned long *frontswap_map_get(struct swap_info_struct *p)
{
	return p->frontswap_map;
}
#else
/* all inline routines become no-ops and all externs are ignored */

#define frontswap_enabled (0)

static inline bool frontswap_test(struct swap_info_struct *sis, pgoff_t offset)
{
	return false;
}

static inline void frontswap_set(struct swap_info_struct *sis, pgoff_t offset)
{
}

static inline void frontswap_clear(struct swap_info_struct *sis, pgoff_t offset)
{
}

static inline void frontswap_map_set(struct swap_info_struct *p,
				     unsigned long *map)
{
}

static inline unsigned long *frontswap_map_get(struct swap_info_struct *p)
{
	return NULL;
}
#endif

static inline int frontswap_put_page(struct page *page)
{
	int ret = -1;

	if (frontswap_enabled)
		ret = __frontswap_put_page(page);
	return ret;
}

static inline int frontswap_get_page(struct page *page)
{
	int ret = -1;

	if (frontswap_enabled)
		ret = __frontswap_get_page(page);
	return ret;
}

static inline void frontswap_invalidate_page(unsigned type, pgoff_t offset)
{
	if (frontswap_enabled)
		__frontswap_invalidate_page(type, offset);
}

static inline void frontswap_invalidate_area(unsigned type)
{
	if (frontswap_enabled)
		__frontswap_invalidate_area(type);
}

static inline void frontswap_init(unsigned type)
{
	if (frontswap_enabled)
		__frontswap_init(type);
}

#endif /* _LINUX_FRONTSWAP_H */
