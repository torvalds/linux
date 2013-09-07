#
# Makefile for the linux kernel.
#

obj-y     = fork.o exec_domain.o panic.o printk.o \
	    cpu.o exit.o itimer.o time.o softirq.o resource.o \
	    sysctl.o sysctl_binary.o capability.o ptrace.o timer.o user.o \
	    signal.o sys.o kmod.o workqueue.o pid.o task_work.o \
	    rcupdate.o extable.o params.o posix-timers.o \
	    kthread.o wait.o sys_ni.o posix-cpu-timers.o mutex.o \
	    hrtimer.o rwsem.o nsproxy.o srcu.o semaphore.o \
	    notifier.o ksysfs.o cred.o reboot.o \
	    async.o range.o groups.o lglock.o smpboot.o

ifdef CONFIG_FUNCTION_TRACER
# Do not trace debug files and internal ftrace files
CFLAGS_REMOVE_lockdep.o = -pg
CFLAGS_REMOVE_lockdep_proc.o = -pg
CFLAGS_REMOVE_mutex-debug.o = -pg
CFLAGS_REMOVE_rtmutex-debug.o = -pg
CFLAGS_REMOVE_cgroup-debug.o = -pg
CFLAGS_REMOVE_irq_work.o = -pg
endif

obj-y += sched/
obj-y += power/
obj-y += cpu/

obj-$(CONFIG_CHECKPOINT_RESTORE) += kcmp.o
obj-$(CONFIG_FREEZER) += freezer.o
obj-$(CONFIG_PROFILING) += profile.o
obj-$(CONFIG_STACKTRACE) += stacktrace.o
obj-y += time/
obj-$(CONFIG_DEBUG_MUTEXES) += mutex-debug.o
obj-$(CONFIG_LOCKDEP) += lockdep.o
ifeq ($(CONFIG_PROC_FS),y)
obj-$(CONFIG_LOCKDEP) += lockdep_proc.o
endif
obj-$(CONFIG_FUTEX) += futex.o
ifeq ($(CONFIG_COMPAT),y)
obj-$(CONFIG_FUTEX) += futex_compat.o
endif
obj-$(CONFIG_RT_MUTEXES) += rtmutex.o
obj-$(CONFIG_DEBUG_RT_MUTEXES) += rtmutex-debug.o
obj-$(CONFIG_RT_MUTEX_TESTER) += rtmutex-tester.o
obj-$(CONFIG_GENERIC_ISA_DMA) += dma.o
obj-$(CONFIG_SMP) += smp.o
ifneq ($(CONFIG_SMP),y)
obj-y += up.o
endif
obj-$(CONFIG_SMP) += spinlock.o
obj-$(CONFIG_DEBUG_SPINLOCK) += spinlock.o
obj-$(CONFIG_PROVE_LOCKING) += spinlock.o
obj-$(CONFIG_UID16) += uid16.o
obj-$(CONFIG_MODULES) += module.o
obj-$(CONFIG_MODULE_SIG) += module_signing.o modsign_pubkey.o modsign_certificate.o
obj-$(CONFIG_KALLSYMS) += kallsyms.o
obj-$(CONFIG_BSD_PROCESS_ACCT) += acct.o
obj-$(CONFIG_KEXEC) += kexec.o
obj-$(CONFIG_BACKTRACE_SELF_TEST) += backtracetest.o
obj-$(CONFIG_COMPAT) += compat.o
obj-$(CONFIG_CGROUPS) += cgroup.o
obj-$(CONFIG_CGROUP_FREEZER) += cgroup_freezer.o
obj-$(CONFIG_CPUSETS) += cpuset.o
obj-$(CONFIG_UTS_NS) += utsname.o
obj-$(CONFIG_USER_NS) += user_namespace.o
obj-$(CONFIG_PID_NS) += pid_namespace.o
obj-$(CONFIG_IKCONFIG) += configs.o
obj-$(CONFIG_RESOURCE_COUNTERS) += res_counter.o
obj-$(CONFIG_SMP) += stop_machine.o
obj-$(CONFIG_KPROBES_SANITY_TEST) += test_kprobes.o
obj-$(CONFIG_AUDIT) += audit.o auditfilter.o
obj-$(CONFIG_AUDITSYSCALL) += auditsc.o
obj-$(CONFIG_AUDIT_WATCH) += audit_watch.o
obj-$(CONFIG_AUDIT_TREE) += audit_tree.o
obj-$(CONFIG_GCOV_KERNEL) += gcov/
obj-$(CONFIG_KPROBES) += kprobes.o
obj-$(CONFIG_KGDB) += debug/
obj-$(CONFIG_DETECT_HUNG_TASK) += hung_task.o
obj-$(CONFIG_LOCKUP_DETECTOR) += watchdog.o
obj-$(CONFIG_GENERIC_HARDIRQS) += irq/
obj-$(CONFIG_SECCOMP) += seccomp.o
obj-$(CONFIG_RCU_TORTURE_TEST) += rcutorture.o
obj-$(CONFIG_TREE_RCU) += rcutree.o
obj-$(CONFIG_TREE_PREEMPT_RCU) += rcutree.o
obj-$(CONFIG_TREE_RCU_TRACE) += rcutree_trace.o
obj-$(CONFIG_TINY_RCU) += rcutiny.o
obj-$(CONFIG_TINY_PREEMPT_RCU) += rcutiny.o
obj-$(CONFIG_RELAY) += relay.o
obj-$(CONFIG_SYSCTL) += utsname_sysctl.o
obj-$(CONFIG_TASK_DELAY_ACCT) += delayacct.o
obj-$(CONFIG_TASKSTATS) += taskstats.o tsacct.o
obj-$(CONFIG_TRACEPOINTS) += tracepoint.o
obj-$(CONFIG_LATENCYTOP) += latencytop.o
obj-$(CONFIG_BINFMT_ELF) += elfcore.o
obj-$(CONFIG_COMPAT_BINFMT_ELF) += elfcore.o
obj-$(CONFIG_BINFMT_ELF_FDPIC) += elfcore.o
obj-$(CONFIG_FUNCTION_TRACER) += trace/
obj-$(CONFIG_TRACING) += trace/
obj-$(CONFIG_TRACE_CLOCK) += trace/
obj-$(CONFIG_RING_BUFFER) += trace/
obj-$(CONFIG_TRACEPOINTS) += trace/
obj-$(CONFIG_IRQ_WORK) += irq_work.o
obj-$(CONFIG_CPU_PM) += cpu_pm.o

obj-$(CONFIG_PERF_EVENTS) += events/

obj-$(CONFIG_USER_RETURN_NOTIFIER) += user-return-notifier.o
obj-$(CONFIG_PADATA) += padata.o
obj-$(CONFIG_CRASH_DUMP) += crash_dump.o
obj-$(CONFIG_JUMP_LABEL) += jump_label.o
obj-$(CONFIG_CONTEXT_TRACKING) += context_tracking.o

$(obj)/configs.o: $(obj)/config_data.h

# config_data.h contains the same information as ikconfig.h but gzipped.
# Info from config_data can be extracted from /proc/config*
targets += config_data.gz
$(obj)/config_data.gz: $(KCONFIG_CONFIG) FORCE
	$(call if_changed,gzip)

      filechk_ikconfiggz = (echo "static const char kernel_config_data[] __used = MAGIC_START"; cat $< | scripts/bin2c; echo "MAGIC_END;")
targets += config_data.h
$(obj)/config_data.h: $(obj)/config_data.gz FORCE
	$(call filechk,ikconfiggz)

$(obj)/time.o: $(obj)/timeconst.h

quiet_cmd_hzfile = HZFILE  $@
      cmd_hzfile = echo "hz=$(CONFIG_HZ)" > $@

targets += hz.bc
$(obj)/hz.bc: $(objtree)/include/config/hz.h FORCE
	$(call if_changed,hzfile)

quiet_cmd_bc  = BC      $@
      cmd_bc  = bc -q $(filter-out FORCE,$^) > $@

targets += timeconst.h
$(obj)/timeconst.h: $(obj)/hz.bc $(src)/timeconst.bc FORCE
	$(call if_changed,bc)

ifeq ($(CONFIG_MODULE_SIG),y)
#
# Pull the signing certificate and any extra certificates into the kernel
#

quiet_cmd_touch = TOUCH   $@
      cmd_touch = touch   $@

extra_certificates:
	$(call cmd,touch)

kernel/modsign_certificate.o: signing_key.x509 extra_certificates

###############################################################################
#
# If module signing is requested, say by allyesconfig, but a key has not been
# supplied, then one will need to be generated to make sure the build does not
# fail and that the kernel may be used afterwards.
#
###############################################################################
ifndef CONFIG_MODULE_SIG_HASH
$(error Could not determine digest type to use from kernel config)
endif

signing_key.priv signing_key.x509: x509.genkey
	@echo "###"
	@echo "### Now generating an X.509 key pair to be used for signing modules."
	@echo "###"
	@echo "### If this takes a long time, you might wish to run rngd in the"
	@echo "### background to keep the supply of entropy topped up.  It"
	@echo "### needs to be run as root, and uses a hardware random"
	@echo "### number generator if one is available."
	@echo "###"
	openssl req -new -nodes -utf8 -$(CONFIG_MODULE_SIG_HASH) -days 36500 \
		-batch -x509 -config x509.genkey \
		-outform DER -out signing_key.x509 \
		-keyout signing_key.priv 2>&1
	@echo "###"
	@echo "### Key pair generated."
	@echo "###"

x509.genkey:
	@echo Generating X.509 key generation config
	@echo  >x509.genkey "[ req ]"
	@echo >>x509.genkey "default_bits = 4096"
	@echo >>x509.genkey "distinguished_name = req_distinguished_name"
	@echo >>x509.genkey "prompt = no"
	@echo >>x509.genkey "string_mask = utf8only"
	@echo >>x509.genkey "x509_extensions = myexts"
	@echo >>x509.genkey
	@echo >>x509.genkey "[ req_distinguished_name ]"
	@echo >>x509.genkey "O = Magrathea"
	@echo >>x509.genkey "CN = Glacier signing key"
	@echo >>x509.genkey "emailAddress = slartibartfast@magrathea.h2g2"
	@echo >>x509.genkey
	@echo >>x509.genkey "[ myexts ]"
	@echo >>x509.genkey "basicConstraints=critical,CA:FALSE"
	@echo >>x509.genkey "keyUsage=digitalSignature"
	@echo >>x509.genkey "subjectKeyIdentifier=hash"
	@echo >>x509.genkey "authorityKeyIdentifier=keyid"
endif
