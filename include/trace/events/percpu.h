/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM percpu

#if !defined(_TRACE_PERCPU_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_PERCPU_H

#include <linux/tracepoint.h>
#include <trace/events/mmflags.h>

TRACE_EVENT(percpu_alloc_percpu,

	TP_PROTO(unsigned long call_site,
		 bool reserved, bool is_atomic, size_t size,
		 size_t align, void *base_addr, int off,
		 void __percpu *ptr, size_t bytes_alloc, gfp_t gfp_flags),

	TP_ARGS(call_site, reserved, is_atomic, size, align, base_addr, off,
		ptr, bytes_alloc, gfp_flags),

	TP_STRUCT__entry(
		__field(	unsigned long,		call_site	)
		__field(	bool,			reserved	)
		__field(	bool,			is_atomic	)
		__field(	size_t,			size		)
		__field(	size_t,			align		)
		__field(	void *,			base_addr	)
		__field(	int,			off		)
		__field(	void __percpu *,	ptr		)
		__field(	size_t,			bytes_alloc	)
		__field(	unsigned long,		gfp_flags	)
	),
	TP_fast_assign(
		__entry->call_site	= call_site;
		__entry->reserved	= reserved;
		__entry->is_atomic	= is_atomic;
		__entry->size		= size;
		__entry->align		= align;
		__entry->base_addr	= base_addr;
		__entry->off		= off;
		__entry->ptr		= ptr;
		__entry->bytes_alloc	= bytes_alloc;
		__entry->gfp_flags	= (__force unsigned long)gfp_flags;
	),

	TP_printk("call_site=%pS reserved=%d is_atomic=%d size=%zu align=%zu base_addr=%p off=%d ptr=%p bytes_alloc=%zu gfp_flags=%s",
		  (void *)__entry->call_site,
		  __entry->reserved, __entry->is_atomic,
		  __entry->size, __entry->align,
		  __entry->base_addr, __entry->off, __entry->ptr,
		  __entry->bytes_alloc, show_gfp_flags(__entry->gfp_flags))
);

TRACE_EVENT(percpu_free_percpu,

	TP_PROTO(void *base_addr, int off, void __percpu *ptr),

	TP_ARGS(base_addr, off, ptr),

	TP_STRUCT__entry(
		__field(	void *,			base_addr	)
		__field(	int,			off		)
		__field(	void __percpu *,	ptr		)
	),

	TP_fast_assign(
		__entry->base_addr	= base_addr;
		__entry->off		= off;
		__entry->ptr		= ptr;
	),

	TP_printk("base_addr=%p off=%d ptr=%p",
		__entry->base_addr, __entry->off, __entry->ptr)
);

TRACE_EVENT(percpu_alloc_percpu_fail,

	TP_PROTO(bool reserved, bool is_atomic, size_t size, size_t align),

	TP_ARGS(reserved, is_atomic, size, align),

	TP_STRUCT__entry(
		__field(	bool,	reserved	)
		__field(	bool,	is_atomic	)
		__field(	size_t,	size		)
		__field(	size_t, align		)
	),

	TP_fast_assign(
		__entry->reserved	= reserved;
		__entry->is_atomic	= is_atomic;
		__entry->size		= size;
		__entry->align		= align;
	),

	TP_printk("reserved=%d is_atomic=%d size=%zu align=%zu",
		  __entry->reserved, __entry->is_atomic,
		  __entry->size, __entry->align)
);

TRACE_EVENT(percpu_create_chunk,

	TP_PROTO(void *base_addr),

	TP_ARGS(base_addr),

	TP_STRUCT__entry(
		__field(	void *, base_addr	)
	),

	TP_fast_assign(
		__entry->base_addr	= base_addr;
	),

	TP_printk("base_addr=%p", __entry->base_addr)
);

TRACE_EVENT(percpu_destroy_chunk,

	TP_PROTO(void *base_addr),

	TP_ARGS(base_addr),

	TP_STRUCT__entry(
		__field(	void *,	base_addr	)
	),

	TP_fast_assign(
		__entry->base_addr	= base_addr;
	),

	TP_printk("base_addr=%p", __entry->base_addr)
);

#endif /* _TRACE_PERCPU_H */

#include <trace/define_trace.h>
