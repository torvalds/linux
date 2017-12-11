/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM percpu

#if !defined(_TRACE_PERCPU_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_PERCPU_H

#include <linux/tracepoint.h>

TRACE_EVENT(percpu_alloc_percpu,

	TP_PROTO(bool reserved, bool is_atomic, size_t size,
		 size_t align, void *base_addr, int off, void __percpu *ptr),

	TP_ARGS(reserved, is_atomic, size, align, base_addr, off, ptr),

	TP_STRUCT__entry(
		__field(	bool,			reserved	)
		__field(	bool,			is_atomic	)
		__field(	size_t,			size		)
		__field(	size_t,			align		)
		__field(	void *,			base_addr	)
		__field(	int,			off		)
		__field(	void __percpu *,	ptr		)
	),

	TP_fast_assign(
		__entry->reserved	= reserved;
		__entry->is_atomic	= is_atomic;
		__entry->size		= size;
		__entry->align		= align;
		__entry->base_addr	= base_addr;
		__entry->off		= off;
		__entry->ptr		= ptr;
	),

	TP_printk("reserved=%d is_atomic=%d size=%zu align=%zu base_addr=%p off=%d ptr=%p",
		  __entry->reserved, __entry->is_atomic,
		  __entry->size, __entry->align,
		  __entry->base_addr, __entry->off, __entry->ptr)
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
