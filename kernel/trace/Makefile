
# Do not instrument the tracer itself:

ifdef CONFIG_FUNCTION_TRACER
ORIG_CFLAGS := $(KBUILD_CFLAGS)
KBUILD_CFLAGS = $(subst -pg,,$(ORIG_CFLAGS))

# selftest needs instrumentation
CFLAGS_trace_selftest_dynamic.o = -pg
obj-y += trace_selftest_dynamic.o
endif

# If unlikely tracing is enabled, do not trace these files
ifdef CONFIG_TRACING_UNLIKELY
KBUILD_CFLAGS += '-Dlikely(x)=likely_notrace(x)'
KBUILD_CFLAGS += '-Dunlikely(x)=unlikely_notrace(x)'
endif

obj-$(CONFIG_FUNCTION_TRACER) += libftrace.o
obj-$(CONFIG_RING_BUFFER) += ring_buffer.o

obj-$(CONFIG_TRACING) += trace.o
obj-$(CONFIG_CONTEXT_SWITCH_TRACER) += trace_sched_switch.o
obj-$(CONFIG_SYSPROF_TRACER) += trace_sysprof.o
obj-$(CONFIG_FUNCTION_TRACER) += trace_functions.o
obj-$(CONFIG_IRQSOFF_TRACER) += trace_irqsoff.o
obj-$(CONFIG_PREEMPT_TRACER) += trace_irqsoff.o
obj-$(CONFIG_SCHED_TRACER) += trace_sched_wakeup.o
obj-$(CONFIG_NOP_TRACER) += trace_nop.o
obj-$(CONFIG_STACK_TRACER) += trace_stack.o
obj-$(CONFIG_MMIOTRACE) += trace_mmiotrace.o
obj-$(CONFIG_BOOT_TRACER) += trace_boot.o
obj-$(CONFIG_FUNCTION_RET_TRACER) += trace_functions_return.o
obj-$(CONFIG_TRACE_UNLIKELY_PROFILE) += trace_unlikely.o

libftrace-y := ftrace.o
