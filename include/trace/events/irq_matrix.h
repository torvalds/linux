#undef TRACE_SYSTEM
#define TRACE_SYSTEM irq_matrix

#if !defined(_TRACE_IRQ_MATRIX_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_IRQ_MATRIX_H

#include <linux/tracepoint.h>

struct irq_matrix;
struct cpumap;

DECLARE_EVENT_CLASS(irq_matrix_global,

	TP_PROTO(struct irq_matrix *matrix),

	TP_ARGS(matrix),

	TP_STRUCT__entry(
		__field(	unsigned int,	online_maps		)
		__field(	unsigned int,	global_available	)
		__field(	unsigned int,	global_reserved		)
		__field(	unsigned int,	total_allocated		)
	),

	TP_fast_assign(
		__entry->online_maps		= matrix->online_maps;
		__entry->global_available	= matrix->global_available;
		__entry->global_reserved	= matrix->global_reserved;
		__entry->total_allocated	= matrix->total_allocated;
	),

	TP_printk("online_maps=%d global_avl=%u, global_rsvd=%u, total_alloc=%u",
		  __entry->online_maps, __entry->global_available,
		  __entry->global_reserved, __entry->total_allocated)
);

DECLARE_EVENT_CLASS(irq_matrix_global_update,

	TP_PROTO(int bit, struct irq_matrix *matrix),

	TP_ARGS(bit, matrix),

	TP_STRUCT__entry(
		__field(	int,		bit			)
		__field(	unsigned int,	online_maps		)
		__field(	unsigned int,	global_available	)
		__field(	unsigned int,	global_reserved		)
		__field(	unsigned int,	total_allocated		)
	),

	TP_fast_assign(
		__entry->bit			= bit;
		__entry->online_maps		= matrix->online_maps;
		__entry->global_available	= matrix->global_available;
		__entry->global_reserved	= matrix->global_reserved;
		__entry->total_allocated	= matrix->total_allocated;
	),

	TP_printk("bit=%d online_maps=%d global_avl=%u, global_rsvd=%u, total_alloc=%u",
		  __entry->bit, __entry->online_maps,
		  __entry->global_available, __entry->global_reserved,
		  __entry->total_allocated)
);

DECLARE_EVENT_CLASS(irq_matrix_cpu,

	TP_PROTO(int bit, unsigned int cpu, struct irq_matrix *matrix,
		 struct cpumap *cmap),

	TP_ARGS(bit, cpu, matrix, cmap),

	TP_STRUCT__entry(
		__field(	int,		bit			)
		__field(	unsigned int,	cpu			)
		__field(	bool,		online			)
		__field(	unsigned int,	available		)
		__field(	unsigned int,	allocated		)
		__field(	unsigned int,	managed			)
		__field(	unsigned int,	online_maps		)
		__field(	unsigned int,	global_available	)
		__field(	unsigned int,	global_reserved		)
		__field(	unsigned int,	total_allocated		)
	),

	TP_fast_assign(
		__entry->bit			= bit;
		__entry->cpu			= cpu;
		__entry->online			= cmap->online;
		__entry->available		= cmap->available;
		__entry->allocated		= cmap->allocated;
		__entry->managed		= cmap->managed;
		__entry->online_maps		= matrix->online_maps;
		__entry->global_available	= matrix->global_available;
		__entry->global_reserved	= matrix->global_reserved;
		__entry->total_allocated	= matrix->total_allocated;
	),

	TP_printk("bit=%d cpu=%u online=%d avl=%u alloc=%u managed=%u online_maps=%u global_avl=%u, global_rsvd=%u, total_alloc=%u",
		  __entry->bit, __entry->cpu, __entry->online,
		  __entry->available, __entry->allocated,
		  __entry->managed, __entry->online_maps,
		  __entry->global_available, __entry->global_reserved,
		  __entry->total_allocated)
);

DEFINE_EVENT(irq_matrix_global, irq_matrix_online,

	TP_PROTO(struct irq_matrix *matrix),

	TP_ARGS(matrix)
);

DEFINE_EVENT(irq_matrix_global, irq_matrix_offline,

	TP_PROTO(struct irq_matrix *matrix),

	TP_ARGS(matrix)
);

DEFINE_EVENT(irq_matrix_global, irq_matrix_reserve,

	TP_PROTO(struct irq_matrix *matrix),

	TP_ARGS(matrix)
);

DEFINE_EVENT(irq_matrix_global, irq_matrix_remove_reserved,

	TP_PROTO(struct irq_matrix *matrix),

	TP_ARGS(matrix)
);

DEFINE_EVENT(irq_matrix_global_update, irq_matrix_assign_system,

	TP_PROTO(int bit, struct irq_matrix *matrix),

	TP_ARGS(bit, matrix)
);

DEFINE_EVENT(irq_matrix_cpu, irq_matrix_reserve_managed,

	TP_PROTO(int bit, unsigned int cpu,
		 struct irq_matrix *matrix, struct cpumap *cmap),

	TP_ARGS(bit, cpu, matrix, cmap)
);

DEFINE_EVENT(irq_matrix_cpu, irq_matrix_remove_managed,

	TP_PROTO(int bit, unsigned int cpu,
		 struct irq_matrix *matrix, struct cpumap *cmap),

	TP_ARGS(bit, cpu, matrix, cmap)
);

DEFINE_EVENT(irq_matrix_cpu, irq_matrix_alloc_managed,

	TP_PROTO(int bit, unsigned int cpu,
		 struct irq_matrix *matrix, struct cpumap *cmap),

	TP_ARGS(bit, cpu, matrix, cmap)
);

DEFINE_EVENT(irq_matrix_cpu, irq_matrix_assign,

	TP_PROTO(int bit, unsigned int cpu,
		 struct irq_matrix *matrix, struct cpumap *cmap),

	TP_ARGS(bit, cpu, matrix, cmap)
);

DEFINE_EVENT(irq_matrix_cpu, irq_matrix_alloc,

	TP_PROTO(int bit, unsigned int cpu,
		 struct irq_matrix *matrix, struct cpumap *cmap),

	TP_ARGS(bit, cpu, matrix, cmap)
);

DEFINE_EVENT(irq_matrix_cpu, irq_matrix_free,

	TP_PROTO(int bit, unsigned int cpu,
		 struct irq_matrix *matrix, struct cpumap *cmap),

	TP_ARGS(bit, cpu, matrix, cmap)
);


#endif /*  _TRACE_IRQ_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
