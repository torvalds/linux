#ifndef _TRACE_KMEM_H
#define _TRACE_KMEM_H

#include <linux/tracepoint.h>
#include <linux/types.h>

DECLARE_TRACE(kmalloc,
	      TP_PROTO(unsigned long call_site,
		      const void *ptr,
		      size_t bytes_req,
		      size_t bytes_alloc,
		      gfp_t gfp_flags),
	      TP_ARGS(call_site, ptr, bytes_req, bytes_alloc, gfp_flags));
DECLARE_TRACE(kmem_cache_alloc,
	      TP_PROTO(unsigned long call_site,
		      const void *ptr,
		      size_t bytes_req,
		      size_t bytes_alloc,
		      gfp_t gfp_flags),
	      TP_ARGS(call_site, ptr, bytes_req, bytes_alloc, gfp_flags));
DECLARE_TRACE(kmalloc_node,
	      TP_PROTO(unsigned long call_site,
		      const void *ptr,
		      size_t bytes_req,
		      size_t bytes_alloc,
		      gfp_t gfp_flags,
		      int node),
	      TP_ARGS(call_site, ptr, bytes_req, bytes_alloc, gfp_flags, node));
DECLARE_TRACE(kmem_cache_alloc_node,
	      TP_PROTO(unsigned long call_site,
		      const void *ptr,
		      size_t bytes_req,
		      size_t bytes_alloc,
		      gfp_t gfp_flags,
		      int node),
	      TP_ARGS(call_site, ptr, bytes_req, bytes_alloc, gfp_flags, node));
DECLARE_TRACE(kfree,
	      TP_PROTO(unsigned long call_site, const void *ptr),
	      TP_ARGS(call_site, ptr));
DECLARE_TRACE(kmem_cache_free,
	      TP_PROTO(unsigned long call_site, const void *ptr),
	      TP_ARGS(call_site, ptr));

#endif /* _TRACE_KMEM_H */
