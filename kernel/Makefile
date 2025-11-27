# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the linux kernel.
#

obj-y     = fork.o exec_domain.o panic.o \
	    cpu.o exit.o softirq.o resource.o \
	    sysctl.o capability.o ptrace.o user.o \
	    signal.o sys.o umh.o workqueue.o pid.o task_work.o \
	    extable.o params.o \
	    kthread.o sys_ni.o nsproxy.o nstree.o nscommon.o \
	    notifier.o ksysfs.o cred.o reboot.o \
	    async.o range.o smpboot.o ucount.o regset.o ksyms_common.o

obj-$(CONFIG_MULTIUSER) += groups.o
obj-$(CONFIG_VHOST_TASK) += vhost_task.o

ifdef CONFIG_FUNCTION_TRACER
# Do not trace internal ftrace files
CFLAGS_REMOVE_irq_work.o = $(CC_FLAGS_FTRACE)
endif

# Branch profiling isn't noinstr-safe
ifdef CONFIG_TRACE_BRANCH_PROFILING
CFLAGS_context_tracking.o += -DDISABLE_BRANCH_PROFILING
endif

# Prevents flicker of uninteresting __do_softirq()/__local_bh_disable_ip()
# in coverage traces.
KCOV_INSTRUMENT_softirq.o := n
# Avoid KCSAN instrumentation in softirq ("No shared variables, all the data
# are CPU local" => assume no data races), to reduce overhead in interrupts.
KCSAN_SANITIZE_softirq.o = n
# These are called from save_stack_trace() on slub debug path,
# and produce insane amounts of uninteresting coverage.
KCOV_INSTRUMENT_extable.o := n
KCOV_INSTRUMENT_stacktrace.o := n
# Don't self-instrument.
KCOV_INSTRUMENT_kcov.o := n
# If sanitizers detect any issues in kcov, it may lead to recursion
# via printk, etc.
KASAN_SANITIZE_kcov.o := n
KCSAN_SANITIZE_kcov.o := n
UBSAN_SANITIZE_kcov.o := n
KMSAN_SANITIZE_kcov.o := n
CFLAGS_kcov.o := $(call cc-option, -fno-conserve-stack) -fno-stack-protector

obj-y += sched/
obj-y += locking/
obj-y += power/
obj-y += printk/
obj-y += irq/
obj-y += rcu/
obj-y += livepatch/
obj-y += dma/
obj-y += entry/
obj-y += unwind/
obj-$(CONFIG_MODULES) += module/

obj-$(CONFIG_KCMP) += kcmp.o
obj-$(CONFIG_FREEZER) += freezer.o
obj-$(CONFIG_PROFILING) += profile.o
obj-$(CONFIG_STACKTRACE) += stacktrace.o
obj-y += time/
obj-$(CONFIG_FUTEX) += futex/
obj-$(CONFIG_GENERIC_ISA_DMA) += dma.o
obj-$(CONFIG_SMP) += smp.o
ifneq ($(CONFIG_SMP),y)
obj-y += up.o
endif
obj-$(CONFIG_UID16) += uid16.o
obj-$(CONFIG_MODULE_SIG_FORMAT) += module_signature.o
obj-$(CONFIG_KALLSYMS) += kallsyms.o
obj-$(CONFIG_KALLSYMS_SELFTEST) += kallsyms_selftest.o
obj-$(CONFIG_BSD_PROCESS_ACCT) += acct.o
obj-$(CONFIG_VMCORE_INFO) += vmcore_info.o elfcorehdr.o
obj-$(CONFIG_CRASH_RESERVE) += crash_reserve.o
obj-$(CONFIG_KEXEC_CORE) += kexec_core.o
obj-$(CONFIG_CRASH_DUMP) += crash_core.o
obj-$(CONFIG_CRASH_DM_CRYPT) += crash_dump_dm_crypt.o
obj-$(CONFIG_CRASH_DUMP_KUNIT_TEST) += crash_core_test.o
obj-$(CONFIG_KEXEC) += kexec.o
obj-$(CONFIG_KEXEC_FILE) += kexec_file.o
obj-$(CONFIG_KEXEC_ELF) += kexec_elf.o
obj-$(CONFIG_KEXEC_HANDOVER) += kexec_handover.o
obj-$(CONFIG_KEXEC_HANDOVER_DEBUG) += kexec_handover_debug.o
obj-$(CONFIG_BACKTRACE_SELF_TEST) += backtracetest.o
obj-$(CONFIG_COMPAT) += compat.o
obj-$(CONFIG_CGROUPS) += cgroup/
obj-$(CONFIG_UTS_NS) += utsname.o
obj-$(CONFIG_USER_NS) += user_namespace.o
obj-$(CONFIG_PID_NS) += pid_namespace.o
obj-$(CONFIG_IKCONFIG) += configs.o
obj-$(CONFIG_IKHEADERS) += kheaders.o
obj-$(CONFIG_SMP) += stop_machine.o
obj-$(CONFIG_AUDIT) += audit.o auditfilter.o
obj-$(CONFIG_AUDITSYSCALL) += auditsc.o audit_watch.o audit_fsnotify.o audit_tree.o
obj-$(CONFIG_GCOV_KERNEL) += gcov/
obj-$(CONFIG_KCOV) += kcov.o
obj-$(CONFIG_KPROBES) += kprobes.o
obj-$(CONFIG_FAIL_FUNCTION) += fail_function.o
obj-$(CONFIG_KGDB) += debug/
obj-$(CONFIG_DETECT_HUNG_TASK) += hung_task.o
obj-$(CONFIG_LOCKUP_DETECTOR) += watchdog.o
obj-$(CONFIG_HARDLOCKUP_DETECTOR_BUDDY) += watchdog_buddy.o
obj-$(CONFIG_HARDLOCKUP_DETECTOR_PERF) += watchdog_perf.o
obj-$(CONFIG_SECCOMP) += seccomp.o
obj-$(CONFIG_RELAY) += relay.o
obj-$(CONFIG_SYSCTL) += utsname_sysctl.o
obj-$(CONFIG_TASK_DELAY_ACCT) += delayacct.o
obj-$(CONFIG_TASKSTATS) += taskstats.o tsacct.o
obj-$(CONFIG_TRACEPOINTS) += tracepoint.o
obj-$(CONFIG_LATENCYTOP) += latencytop.o
obj-$(CONFIG_FUNCTION_TRACER) += trace/
obj-$(CONFIG_TRACING) += trace/
obj-$(CONFIG_TRACE_CLOCK) += trace/
obj-$(CONFIG_RING_BUFFER) += trace/
obj-$(CONFIG_TRACEPOINTS) += trace/
obj-$(CONFIG_RETHOOK) += trace/
obj-$(CONFIG_IRQ_WORK) += irq_work.o
obj-$(CONFIG_CPU_PM) += cpu_pm.o
obj-$(CONFIG_BPF) += bpf/
obj-$(CONFIG_KCSAN) += kcsan/
obj-$(CONFIG_SHADOW_CALL_STACK) += scs.o
obj-$(CONFIG_HAVE_STATIC_CALL) += static_call.o
obj-$(CONFIG_HAVE_STATIC_CALL_INLINE) += static_call_inline.o
obj-$(CONFIG_CFI) += cfi.o

obj-$(CONFIG_PERF_EVENTS) += events/

obj-$(CONFIG_USER_RETURN_NOTIFIER) += user-return-notifier.o
obj-$(CONFIG_PADATA) += padata.o
obj-$(CONFIG_JUMP_LABEL) += jump_label.o
obj-$(CONFIG_CONTEXT_TRACKING) += context_tracking.o
obj-$(CONFIG_TORTURE_TEST) += torture.o

obj-$(CONFIG_HAS_IOMEM) += iomem.o
obj-$(CONFIG_RSEQ) += rseq.o
obj-$(CONFIG_WATCH_QUEUE) += watch_queue.o

obj-$(CONFIG_RESOURCE_KUNIT_TEST) += resource_kunit.o
obj-$(CONFIG_SYSCTL_KUNIT_TEST) += sysctl-test.o

CFLAGS_kstack_erase.o += $(DISABLE_KSTACK_ERASE)
CFLAGS_kstack_erase.o += $(call cc-option,-mgeneral-regs-only)
obj-$(CONFIG_KSTACK_ERASE) += kstack_erase.o
KASAN_SANITIZE_kstack_erase.o := n
KCSAN_SANITIZE_kstack_erase.o := n
KCOV_INSTRUMENT_kstack_erase.o := n

obj-$(CONFIG_SCF_TORTURE_TEST) += scftorture.o

$(obj)/configs.o: $(obj)/config_data.gz

targets += config_data config_data.gz
$(obj)/config_data.gz: $(obj)/config_data FORCE
	$(call if_changed,gzip)

filechk_cat = cat $<

$(obj)/config_data: $(KCONFIG_CONFIG) FORCE
	$(call filechk,cat)

# kheaders_data.tar.xz
$(obj)/kheaders.o: $(obj)/kheaders_data.tar.xz

quiet_cmd_kheaders_data = GEN     $@
      cmd_kheaders_data = "$<" "$@" "$(obj)/kheaders-srclist" "$(obj)/kheaders-objlist" "$(KBUILD_BUILD_TIMESTAMP)"
      cmd_kheaders_data_dep = cat $(depfile) >> $(dot-target).cmd; rm -f $(depfile)

define rule_kheaders_data
	$(call cmd_and_savecmd,kheaders_data)
	$(call cmd,kheaders_data_dep)
endef

targets += kheaders_data.tar.xz
$(obj)/kheaders_data.tar.xz: $(src)/gen_kheaders.sh $(obj)/kheaders-srclist $(obj)/kheaders-objlist $(obj)/kheaders.md5 FORCE
	$(call if_changed_rule,kheaders_data)

# generated headers in objtree
#
# include/generated/utsversion.h is ignored because it is generated
# after gen_kheaders.sh is executed. (utsversion.h is unneeded for kheaders)
filechk_kheaders_objlist = \
	for d in include "arch/$(SRCARCH)/include"; do \
		find "$${d}/generated" ! -path "include/generated/utsversion.h" -a -name "*.h" -print; \
	done

$(obj)/kheaders-objlist: FORCE
	$(call filechk,kheaders_objlist)

# non-generated headers in srctree
filechk_kheaders_srclist = \
	for d in include "arch/$(SRCARCH)/include"; do \
		find "$(srctree)/$${d}" -path "$(srctree)/$${d}/generated" -prune -o -name "*.h" -print; \
	done

$(obj)/kheaders-srclist: FORCE
	$(call filechk,kheaders_srclist)

# Some files are symlinks. If symlinks are changed, kheaders_data.tar.xz should
# be rebuilt.
filechk_kheaders_md5sum = xargs -r -a $< stat -c %N | md5sum

$(obj)/kheaders.md5: $(obj)/kheaders-srclist FORCE
	$(call filechk,kheaders_md5sum)

clean-files := kheaders.md5 kheaders-srclist kheaders-objlist
