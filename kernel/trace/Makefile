
# Do not instrument the tracer itself:

ifdef CONFIG_FTRACE
ORIG_CFLAGS := $(KBUILD_CFLAGS)
KBUILD_CFLAGS = $(subst -pg,,$(ORIG_CFLAGS))

# selftest needs instrumentation
CFLAGS_trace_selftest_dynamic.o = -pg
obj-y += trace_selftest_dynamic.o
endif

obj-$(CONFIG_FTRACE) += libftrace.o

obj-$(CONFIG_TRACING) += trace.o
obj-$(CONFIG_CONTEXT_SWITCH_TRACER) += trace_sched_switch.o
obj-$(CONFIG_SYSPROF_TRACER) += trace_sysprof.o
obj-$(CONFIG_FTRACE) += trace_functions.o
obj-$(CONFIG_IRQSOFF_TRACER) += trace_irqsoff.o
obj-$(CONFIG_PREEMPT_TRACER) += trace_irqsoff.o
obj-$(CONFIG_SCHED_TRACER) += trace_sched_wakeup.o
obj-$(CONFIG_MMIOTRACE) += trace_mmiotrace.o

libftrace-y := ftrace.o
