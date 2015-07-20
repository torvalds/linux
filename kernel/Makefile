#
# Makefile for the linux kernel.
#

obj-y     = fork.o exec_domain.o panic.o \
	    cpu.o exit.o softirq.o resource.o \
	    sysctl.o sysctl_binary.o capability.o ptrace.o user.o \
	    signal.o sys.o kmod.o workqueue.o pid.o task_work.o \
	    extable.o params.o \
	    kthread.o sys_ni.o nsproxy.o \
	    notifier.o ksysfs.o cred.o reboot.o \
	    async.o range.o smpboot.o

obj-$(CONFIG_MULTIUSER) += groups.o

ifdef CONFIG_FUNCTION_TRACER
# Do not trace debug files and internal ftrace files
CFLAGS_REMOVE_cgroup-debug.o = $(CC_FLAGS_FTRACE)
CFLAGS_REMOVE_irq_work.o = $(CC_FLAGS_FTRACE)
endif

# cond_syscall is currently not LTO compatible
CFLAGS_sys_ni.o = $(DISABLE_LTO)

obj-y += sched/
obj-y += locking/
obj-y += power/
obj-y += printk/
obj-y += irq/
obj-y += rcu/
obj-y += livepatch/

obj-$(CONFIG_CHECKPOINT_RESTORE) += kcmp.o
obj-$(CONFIG_FREEZER) += freezer.o
obj-$(CONFIG_PROFILING) += profile.o
obj-$(CONFIG_STACKTRACE) += stacktrace.o
obj-y += time/
obj-$(CONFIG_FUTEX) += futex.o
ifeq ($(CONFIG_COMPAT),y)
obj-$(CONFIG_FUTEX) += futex_compat.o
endif
obj-$(CONFIG_GENERIC_ISA_DMA) += dma.o
obj-$(CONFIG_SMP) += smp.o
ifneq ($(CONFIG_SMP),y)
obj-y += up.o
endif
obj-$(CONFIG_UID16) += uid16.o
obj-$(CONFIG_SYSTEM_TRUSTED_KEYRING) += system_keyring.o system_certificates.o
obj-$(CONFIG_MODULES) += module.o
obj-$(CONFIG_MODULE_SIG) += module_signing.o
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
obj-$(CONFIG_SECCOMP) += seccomp.o
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
obj-$(CONFIG_BPF) += bpf/

obj-$(CONFIG_PERF_EVENTS) += events/

obj-$(CONFIG_USER_RETURN_NOTIFIER) += user-return-notifier.o
obj-$(CONFIG_PADATA) += padata.o
obj-$(CONFIG_CRASH_DUMP) += crash_dump.o
obj-$(CONFIG_JUMP_LABEL) += jump_label.o
obj-$(CONFIG_CONTEXT_TRACKING) += context_tracking.o
obj-$(CONFIG_TORTURE_TEST) += torture.o

$(obj)/configs.o: $(obj)/config_data.h

# config_data.h contains the same information as ikconfig.h but gzipped.
# Info from config_data can be extracted from /proc/config*
targets += config_data.gz
$(obj)/config_data.gz: $(KCONFIG_CONFIG) FORCE
	$(call if_changed,gzip)

      filechk_ikconfiggz = (echo "static const char kernel_config_data[] __used = MAGIC_START"; cat $< | scripts/basic/bin2c; echo "MAGIC_END;")
targets += config_data.h
$(obj)/config_data.h: $(obj)/config_data.gz FORCE
	$(call filechk,ikconfiggz)

###############################################################################
#
# When a Kconfig string contains a filename, it is suitable for
# passing to shell commands. It is surrounded by double-quotes, and
# any double-quotes or backslashes within it are escaped by
# backslashes.
#
# This is no use for dependencies or $(wildcard). We need to strip the
# surrounding quotes and the escaping from quotes and backslashes, and
# we *do* need to escape any spaces in the string. So, for example:
#
# Usage: $(eval $(call config_filename,FOO))
#
# Defines FOO_FILENAME based on the contents of the CONFIG_FOO option,
# transformed as described above to be suitable for use within the
# makefile.
#
# Also, if the filename is a relative filename and exists in the source
# tree but not the build tree, define FOO_SRCPREFIX as $(srctree)/ to
# be prefixed to *both* command invocation and dependencies.
#
# Note: We also print the filenames in the quiet_cmd_foo text, and
# perhaps ought to have a version specially escaped for that purpose.
# But it's only cosmetic, and $(patsubst "%",%,$(CONFIG_FOO)) is good
# enough.  It'll strip the quotes in the common case where there's no
# space and it's a simple filename, and it'll retain the quotes when
# there's a space. There are some esoteric cases in which it'll print
# the wrong thing, but we don't really care. The actual dependencies
# and commands *do* get it right, with various combinations of single
# and double quotes, backslashes and spaces in the filenames.
#
###############################################################################
#
quote := $(firstword " ")
space :=
space +=
space_escape := %%%SPACE%%%
#
define config_filename
ifneq ($$(CONFIG_$(1)),"")
$(1)_FILENAME := $$(subst \\,\,$$(subst \$$(quote),$$(quote),$$(subst $$(space_escape),\$$(space),$$(patsubst "%",%,$$(subst $$(space),$$(space_escape),$$(CONFIG_$(1)))))))
ifneq ($$(patsubst /%,%,$$(firstword $$($(1)_FILENAME))),$$(firstword $$($(1)_FILENAME)))
else
ifeq ($$(wildcard $$($(1)_FILENAME)),)
ifneq ($$(wildcard $$(srctree)/$$($(1)_FILENAME)),)
$(1)_SRCPREFIX := $(srctree)/
endif
endif
endif
endif
endef
#
###############################################################################

ifeq ($(CONFIG_SYSTEM_TRUSTED_KEYRING),y)

$(eval $(call config_filename,SYSTEM_TRUSTED_KEYS))

# GCC doesn't include .incbin files in -MD generated dependencies (PR#66871)
$(obj)/system_certificates.o: $(obj)/x509_certificate_list

# Cope with signing_key.x509 existing in $(srctree) not $(objtree)
AFLAGS_system_certificates.o := -I$(srctree)

quiet_cmd_extract_certs  = EXTRACT_CERTS   $(patsubst "%",%,$(2))
      cmd_extract_certs  = scripts/extract-cert $(2) $@ || ( rm $@; exit 1)

targets += x509_certificate_list
$(obj)/x509_certificate_list: scripts/extract-cert $(SYSTEM_TRUSTED_KEYS_SRCPREFIX)$(SYSTEM_TRUSTED_KEYS_FILENAME) FORCE
	$(call if_changed,extract_certs,$(SYSTEM_TRUSTED_KEYS_SRCPREFIX)$(CONFIG_SYSTEM_TRUSTED_KEYS))
endif

clean-files := x509_certificate_list .x509.list

ifeq ($(CONFIG_MODULE_SIG),y)
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

# We do it this way rather than having a boolean option for enabling an
# external private key, because 'make randconfig' might enable such a
# boolean option and we unfortunately can't make it depend on !RANDCONFIG.
ifeq ($(CONFIG_MODULE_SIG_KEY),"signing_key.pem")
signing_key.pem: x509.genkey
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
		-outform PEM -out signing_key.pem \
		-keyout signing_key.pem 2>&1
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
	@echo >>x509.genkey "#O = Unspecified company"
	@echo >>x509.genkey "CN = Build time autogenerated kernel key"
	@echo >>x509.genkey "#emailAddress = unspecified.user@unspecified.company"
	@echo >>x509.genkey
	@echo >>x509.genkey "[ myexts ]"
	@echo >>x509.genkey "basicConstraints=critical,CA:FALSE"
	@echo >>x509.genkey "keyUsage=digitalSignature"
	@echo >>x509.genkey "subjectKeyIdentifier=hash"
	@echo >>x509.genkey "authorityKeyIdentifier=keyid"
endif

$(eval $(call config_filename,MODULE_SIG_KEY))

# If CONFIG_MODULE_SIG_KEY isn't a PKCS#11 URI, depend on it
ifeq ($(patsubst pkcs11:%,%,$(firstword $(MODULE_SIG_KEY_FILENAME))),$(firstword $(MODULE_SIG_KEY_FILENAME)))
X509_DEP := $(MODULE_SIG_KEY_SRCPREFIX)$(MODULE_SIG_KEY_FILENAME)
endif

# GCC PR#66871 again.
$(obj)/system_certificates.o: signing_key.x509

signing_key.x509: scripts/extract-cert include/config/module/sig/key.h $(X509_DEP)
	$(call cmd,extract_certs,$(MODULE_SIG_KEY_SRCPREFIX)$(CONFIG_MODULE_SIG_KEY))
endif
