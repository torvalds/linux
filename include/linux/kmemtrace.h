/*
 * Copyright (C) 2008 Eduard - Gabriel Munteanu
 *
 * This file is released under GPL version 2.
 */

#ifndef _LINUX_KMEMTRACE_H
#define _LINUX_KMEMTRACE_H

#ifdef __KERNEL__

#include <linux/types.h>
#include <linux/marker.h>

enum kmemtrace_type_id {
	KMEMTRACE_TYPE_KMALLOC = 0,	/* kmalloc() or kfree(). */
	KMEMTRACE_TYPE_CACHE,		/* kmem_cache_*(). */
	KMEMTRACE_TYPE_PAGES,		/* __get_free_pages() and friends. */
};

#ifdef CONFIG_KMEMTRACE

extern void kmemtrace_init(void);

static inline void kmemtrace_mark_alloc_node(enum kmemtrace_type_id type_id,
					     unsigned long call_site,
					     const void *ptr,
					     size_t bytes_req,
					     size_t bytes_alloc,
					     gfp_t gfp_flags,
					     int node)
{
	trace_mark(kmemtrace_alloc, "type_id %d call_site %lu ptr %lu "
		   "bytes_req %lu bytes_alloc %lu gfp_flags %lu node %d",
		   type_id, call_site, (unsigned long) ptr,
		   bytes_req, bytes_alloc, (unsigned long) gfp_flags, node);
}

static inline void kmemtrace_mark_free(enum kmemtrace_type_id type_id,
				       unsigned long call_site,
				       const void *ptr)
{
	trace_mark(kmemtrace_free, "type_id %d call_site %lu ptr %lu",
		   type_id, call_site, (unsigned long) ptr);
}

#else /* CONFIG_KMEMTRACE */

static inline void kmemtrace_init(void)
{
}

static inline void kmemtrace_mark_alloc_node(enum kmemtrace_type_id type_id,
					     unsigned long call_site,
					     const void *ptr,
					     size_t bytes_req,
					     size_t bytes_alloc,
					     gfp_t gfp_flags,
					     int node)
{
}

static inline void kmemtrace_mark_free(enum kmemtrace_type_id type_id,
				       unsigned long call_site,
				       const void *ptr)
{
}

#endif /* CONFIG_KMEMTRACE */

static inline void kmemtrace_mark_alloc(enum kmemtrace_type_id type_id,
					unsigned long call_site,
					const void *ptr,
					size_t bytes_req,
					size_t bytes_alloc,
					gfp_t gfp_flags)
{
	kmemtrace_mark_alloc_node(type_id, call_site, ptr,
				  bytes_req, bytes_alloc, gfp_flags, -1);
}

#endif /* __KERNEL__ */

#endif /* _LINUX_KMEMTRACE_H */

