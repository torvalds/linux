#undef TRACE_SYSTEM
#define TRACE_SYSTEM arm-ipi

#if !defined(_TRACE_ARM_IPI_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_ARM_IPI_H

#include <linux/tracepoint.h>

#define show_arm_ipi_name(val)				\
	__print_symbolic(val,				\
			 { 0, "IPI_WAKEUP" },		\
			 { 1, "IPI_TIMER" },		\
			 { 2, "IPI_RESCHEDULE" },		\
			 { 3, "IPI_CALL_FUNC" },		\
			 { 4, "IPI_CALL_FUNC_SINGLE" },		\
			 { 5, "IPI_CPU_STOP" },	\
			 { 6, "IPI_COMPLETION" },		\
			 { 7, "IPI_CPU_BACKTRACE" })

DECLARE_EVENT_CLASS(arm_ipi,

	TP_PROTO(unsigned int ipi_nr),

	TP_ARGS(ipi_nr),

	TP_STRUCT__entry(
		__field(	unsigned int,	ipi	)
	),

	TP_fast_assign(
		__entry->ipi = ipi_nr;
	),

	TP_printk("ipi=%u [action=%s]", __entry->ipi,
		show_arm_ipi_name(__entry->ipi))
);

/**
 * arm_ipi_entry - called in the arm-generic ipi handler immediately before
 *                 entering ipi-type handler
 * @ipi_nr:  ipi number
 *
 * When used in combination with the arm_ipi_exit tracepoint
 * we can determine the ipi handler runtine.
 */
DEFINE_EVENT(arm_ipi, arm_ipi_entry,

	TP_PROTO(unsigned int ipi_nr),

	TP_ARGS(ipi_nr)
);

/**
 * arm_ipi_exit - called in the arm-generic ipi handler immediately
 *                after the ipi-type handler returns
 * @ipi_nr:  ipi number
 *
 * When used in combination with the arm_ipi_entry tracepoint
 * we can determine the ipi handler runtine.
 */
DEFINE_EVENT(arm_ipi, arm_ipi_exit,

	TP_PROTO(unsigned int ipi_nr),

	TP_ARGS(ipi_nr)
);

/**
 * arm_ipi_send - called as the ipi target mask is built, immediately
 *                before the register is written
 * @ipi_nr:  ipi number
 * @dest:    cpu to send to
 *
 * When used in combination with the arm_ipi_entry tracepoint
 * we can determine the ipi raise to run latency.
 */
TRACE_EVENT(arm_ipi_send,

	TP_PROTO(unsigned int ipi_nr, int dest),

	TP_ARGS(ipi_nr, dest),

	TP_STRUCT__entry(
		__field(	unsigned int,	ipi	)
		__field(	int			,	dest )
	),

	TP_fast_assign(
		__entry->ipi = ipi_nr;
		__entry->dest = dest;
	),

	TP_printk("dest=%d ipi=%u [action=%s]", __entry->dest,
			__entry->ipi, show_arm_ipi_name(__entry->ipi))
);

#endif /*  _TRACE_ARM_IPI_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
