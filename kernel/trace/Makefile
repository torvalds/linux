# SPDX-License-Identifier: GPL-2.0

# Do not instrument the tracer itself:

ccflags-remove-$(CONFIG_FUNCTION_TRACER) += $(CC_FLAGS_FTRACE)

ifdef CONFIG_FUNCTION_TRACER

# Avoid recursion due to instrumentation.
KCSAN_SANITIZE := n

ifdef CONFIG_FTRACE_SELFTEST
# selftest needs instrumentation
CFLAGS_trace_selftest_dynamic.o = $(CC_FLAGS_FTRACE)
obj-y += trace_selftest_dynamic.o
endif
endif

# Allow some files to be function traced
ifdef CONFIG_FUNCTION_SELF_TRACING
CFLAGS_trace_output.o = $(CC_FLAGS_FTRACE)
CFLAGS_trace_seq.o = $(CC_FLAGS_FTRACE)
CFLAGS_trace_stat.o = $(CC_FLAGS_FTRACE)
CFLAGS_tracing_map.o = $(CC_FLAGS_FTRACE)
CFLAGS_synth_event_gen_test.o = $(CC_FLAGS_FTRACE)
CFLAGS_trace_events.o = $(CC_FLAGS_FTRACE)
CFLAGS_trace_syscalls.o = $(CC_FLAGS_FTRACE)
CFLAGS_trace_events_filter.o = $(CC_FLAGS_FTRACE)
CFLAGS_trace_events_trigger.o = $(CC_FLAGS_FTRACE)
CFLAGS_trace_events_synth.o = $(CC_FLAGS_FTRACE)
CFLAGS_trace_events_hist.o = $(CC_FLAGS_FTRACE)
CFLAGS_trace_events_user.o = $(CC_FLAGS_FTRACE)
CFLAGS_trace_dynevent.o = $(CC_FLAGS_FTRACE)
endif

ifdef CONFIG_FTRACE_STARTUP_TEST
CFLAGS_trace_kprobe_selftest.o = $(CC_FLAGS_FTRACE)
obj-$(CONFIG_KPROBE_EVENTS) += trace_kprobe_selftest.o
endif

# If unlikely tracing is enabled, do not trace these files
ifdef CONFIG_TRACING_BRANCHES
KBUILD_CFLAGS += -DDISABLE_BRANCH_PROFILING
endif

# for GCOV coverage profiling
ifdef CONFIG_GCOV_PROFILE_FTRACE
GCOV_PROFILE := y
endif

# Functions in this file could be invoked from early interrupt
# code and produce random code coverage.
KCOV_INSTRUMENT_trace_preemptirq.o := n

CFLAGS_bpf_trace.o := -I$(src)

CFLAGS_trace_benchmark.o := -I$(src)
CFLAGS_trace_events_filter.o := -I$(src)

obj-$(CONFIG_TRACE_CLOCK) += trace_clock.o

obj-$(CONFIG_FUNCTION_TRACER) += libftrace.o
obj-$(CONFIG_RING_BUFFER) += ring_buffer.o
obj-$(CONFIG_RING_BUFFER_BENCHMARK) += ring_buffer_benchmark.o

obj-$(CONFIG_TRACING) += trace.o
obj-$(CONFIG_TRACING) += trace_output.o
obj-$(CONFIG_TRACING) += trace_seq.o
obj-$(CONFIG_TRACING) += trace_stat.o
obj-$(CONFIG_TRACING) += trace_printk.o
obj-$(CONFIG_TRACING) += trace_pid.o
obj-$(CONFIG_TRACER_SNAPSHOT) += trace_snapshot.o
obj-$(CONFIG_TRACING) += 	pid_list.o
obj-$(CONFIG_TRACING_MAP) += tracing_map.o
obj-$(CONFIG_PREEMPTIRQ_DELAY_TEST) += preemptirq_delay_test.o
obj-$(CONFIG_SYNTH_EVENT_GEN_TEST) += synth_event_gen_test.o
obj-$(CONFIG_KPROBE_EVENT_GEN_TEST) += kprobe_event_gen_test.o
obj-$(CONFIG_CONTEXT_SWITCH_TRACER) += trace_sched_switch.o
obj-$(CONFIG_FUNCTION_TRACER) += trace_functions.o
obj-$(CONFIG_PREEMPTIRQ_TRACEPOINTS) += trace_preemptirq.o
obj-$(CONFIG_IRQSOFF_TRACER) += trace_irqsoff.o
obj-$(CONFIG_PREEMPT_TRACER) += trace_irqsoff.o
obj-$(CONFIG_SCHED_TRACER) += trace_sched_wakeup.o
obj-$(CONFIG_HWLAT_TRACER) += trace_hwlat.o
obj-$(CONFIG_OSNOISE_TRACER) += trace_osnoise.o
obj-$(CONFIG_NOP_TRACER) += trace_nop.o
obj-$(CONFIG_STACK_TRACER) += trace_stack.o
obj-$(CONFIG_MMIOTRACE) += trace_mmiotrace.o
obj-$(CONFIG_FUNCTION_GRAPH_TRACER) += trace_functions_graph.o
obj-$(CONFIG_TRACE_BRANCH_PROFILING) += trace_branch.o
obj-$(CONFIG_BLK_DEV_IO_TRACE) += blktrace.o
obj-$(CONFIG_FUNCTION_GRAPH_TRACER) += fgraph.o
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
obj-$(CONFIG_EVENT_TRACING) += trace_events_trigger.o
obj-$(CONFIG_EPROBE_EVENTS) += trace_eprobe.o
obj-$(CONFIG_TRACE_EVENT_INJECT) += trace_events_inject.o
obj-$(CONFIG_SYNTH_EVENTS) += trace_events_synth.o
obj-$(CONFIG_HIST_TRIGGERS) += trace_events_hist.o
obj-$(CONFIG_USER_EVENTS) += trace_events_user.o
obj-$(CONFIG_BPF_EVENTS) += bpf_trace.o
obj-$(CONFIG_KPROBE_EVENTS) += trace_kprobe.o
obj-$(CONFIG_TRACEPOINTS) += error_report-traces.o
obj-$(CONFIG_TRACEPOINTS) += power-traces.o
ifeq ($(CONFIG_PM),y)
obj-$(CONFIG_TRACEPOINTS) += rpm-traces.o
endif
ifeq ($(CONFIG_TRACING),y)
obj-$(CONFIG_KGDB_KDB) += trace_kdb.o
endif
obj-$(CONFIG_DYNAMIC_EVENTS) += trace_dynevent.o
obj-$(CONFIG_PROBE_EVENTS) += trace_probe.o
obj-$(CONFIG_PROBE_EVENTS_BTF_ARGS) += trace_btf.o
obj-$(CONFIG_UPROBE_EVENTS) += trace_uprobe.o
obj-$(CONFIG_BOOTTIME_TRACING) += trace_boot.o
obj-$(CONFIG_FTRACE_RECORD_RECURSION) += trace_recursion_record.o
obj-$(CONFIG_FPROBE) += fprobe.o
obj-$(CONFIG_RETHOOK) += rethook.o
obj-$(CONFIG_FPROBE_EVENTS) += trace_fprobe.o

obj-$(CONFIG_TRACEPOINT_BENCHMARK) += trace_benchmark.o
obj-$(CONFIG_RV) += rv/

obj-$(CONFIG_TRACE_REMOTE) += trace_remote.o
obj-$(CONFIG_SIMPLE_RING_BUFFER) += simple_ring_buffer.o
obj-$(CONFIG_TRACE_REMOTE_TEST) += remote_test.o

#
# simple_ring_buffer is used by the pKVM hypervisor which does not have access
# to all kernel symbols. Fail the build if forbidden symbols are found.
#
# undefsyms_base generates a set of compiler and tooling-generated symbols that can
# safely be ignored for simple_ring_buffer.
#
filechk_undefsyms_base = \
	echo '$(pound)include <linux/atomic.h>'; \
	echo '$(pound)include <linux/string.h>'; \
	echo '$(pound)include <asm/page.h>'; \
	echo 'static char page[PAGE_SIZE] __aligned(PAGE_SIZE);'; \
	echo 'void undefsyms_base(void *p, int n);'; \
	echo 'void undefsyms_base(void *p, int n) {'; \
	echo '	char buffer[256] = { 0 };'; \
	echo '	u32 u = 0;'; \
	echo '	memset((char * volatile)page, 8, PAGE_SIZE);'; \
	echo '	memset((char * volatile)buffer, 8, sizeof(buffer));'; \
	echo '	memcpy((void * volatile)p, buffer, sizeof(buffer));'; \
	echo '	cmpxchg((u32 * volatile)&u, 0, 8);'; \
	echo '	WARN_ON(n == 0xdeadbeef);'; \
	echo '}'

$(obj)/undefsyms_base.c: FORCE
	$(call filechk,undefsyms_base)

clean-files += undefsyms_base.c

$(obj)/undefsyms_base.o: $(obj)/undefsyms_base.c

targets += undefsyms_base.o

# Ensure KASAN is enabled to avoid logic that may disable FORTIFY_SOURCE when
# KASAN is not enabled. undefsyms_base.o does not automatically get KASAN flags
# because it is not linked into vmlinux.
KASAN_SANITIZE_undefsyms_base.o := y

UNDEFINED_ALLOWLIST = __asan __gcov __kasan __kcsan __hwasan __sancov __sanitizer __tsan __ubsan __x86_indirect_thunk \
		      __msan simple_ring_buffer \
		      $(shell $(NM) -u $(obj)/undefsyms_base.o 2>/dev/null | awk '{print $$2}')

quiet_cmd_check_undefined = NM      $<
      cmd_check_undefined = \
          undefsyms=$$($(NM) -u $< | grep -v $(addprefix -e , $(UNDEFINED_ALLOWLIST)) || true); \
          if [ -n "$$undefsyms" ]; then \
              echo "Unexpected symbols in $<:" >&2; \
              echo "$$undefsyms" >&2; \
              false; \
          fi

$(obj)/%.o.checked: $(obj)/%.o $(obj)/undefsyms_base.o FORCE
	$(call if_changed,check_undefined)

always-$(CONFIG_SIMPLE_RING_BUFFER) += simple_ring_buffer.o.checked

libftrace-y := ftrace.o
