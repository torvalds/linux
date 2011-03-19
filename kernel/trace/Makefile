
# Do not instrument the tracer itself:

ifdef CONFIG_FUNCTION_TRACER
ORIG_CFLAGS := $(KBUILD_CFLAGS)
KBUILD_CFLAGS = $(subst -pg,,$(ORIG_CFLAGS))

# selftest needs instrumentation
CFLAGS_trace_selftest_dynamic.o = -pg
obj-y += trace_selftest_dynamic.o
endif

# If unlikely tracing is enabled, do not trace these files
ifdef CONFIG_TRACING_BRANCHES
KBUILD_CFLAGS += -DDISABLE_BRANCH_PROFILING
endif

#
# Make the trace clocks available generally: it's infrastructure
# relied on by ptrace for example:
#
obj-y += trace_clock.o

obj-$(CONFIG_FUNCTION_TRACER) += libftrace.o
obj-$(CONFIG_RING_BUFFER) += ring_buffer.o
obj-$(CONFIG_RING_BUFFER_BENCHMARK) += ring_buffer_benchmark.o

obj-$(CONFIG_TRACING) += trace.o
obj-$(CONFIG_TRACING) += trace_output.o
obj-$(CONFIG_TRACING) += trace_stat.o
obj-$(CONFIG_TRACING) += trace_printk.o
obj-$(CONFIG_CONTEXT_SWITCH_TRACER) += trace_sched_switch.o
obj-$(CONFIG_FUNCTION_TRACER) += trace_functions.o
obj-$(CONFIG_IRQSOFF_TRACER) += trace_irqsoff.o
obj-$(CONFIG_PREEMPT_TRACER) += trace_irqsoff.o
obj-$(CONFIG_SCHED_TRACER) += trace_sched_wakeup.o
obj-$(CONFIG_NOP_TRACER) += trace_nop.o
obj-$(CONFIG_STACK_TRACER) += trace_stack.o
obj-$(CONFIG_MMIOTRACE) += trace_mmiotrace.o
obj-$(CONFIG_FUNCTION_GRAPH_TRACER) += trace_functions_graph.o
obj-$(CONFIG_TRACE_BRANCH_PROFILING) += trace_branch.o
obj-$(CONFIG_WORKQUEUE_TRACER) += trace_workqueue.o
obj-$(CONFIG_BLK_DEV_IO_TRACE) += blktrace.o
ifeq ($(CONFIG_BLOCK),y)
obj-$(CONFIG_EVENT_TRACING) += blktrace.o
endif
obj-$(CONFIG_EVENT_TRACING) += trace_events.o
obj-$(CONFIG_EVENT_TRACING) += trace_export.o
obj-$(CONFIG_FTRACE_SYSCALLS) += trace_syscalls.o
ifeq ($(CONFIG_PERF_EVENTS),y)
obj-$(CONFIG_EVENT_TRACING) += trace_event_perf.o
endif
obj-$(CONFIG_EVENT_TRACING) += trace_events_filter.o
obj-$(CONFIG_KPROBE_EVENT) += trace_kprobe.o
obj-$(CONFIG_TRACEPOINTS) += power-traces.o
ifeq ($(CONFIG_TRACING),y)
obj-$(CONFIG_KGDB_KDB) += trace_kdb.o
endif

libftrace-y := ftrace.o
