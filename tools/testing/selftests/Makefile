# SPDX-License-Identifier: GPL-2.0
TARGETS += alsa
TARGETS += amd-pstate
TARGETS += arm64
TARGETS += bpf
TARGETS += breakpoints
TARGETS += cachestat
TARGETS += capabilities
TARGETS += cgroup
TARGETS += clone3
TARGETS += connector
TARGETS += core
TARGETS += cpufreq
TARGETS += cpu-hotplug
TARGETS += damon
TARGETS += devices
TARGETS += dmabuf-heaps
TARGETS += drivers/dma-buf
TARGETS += drivers/s390x/uvdevice
TARGETS += drivers/net
TARGETS += drivers/net/bonding
TARGETS += drivers/net/team
TARGETS += drivers/net/virtio_net
TARGETS += dt
TARGETS += efivarfs
TARGETS += exec
TARGETS += fchmodat2
TARGETS += filesystems
TARGETS += filesystems/binderfs
TARGETS += filesystems/epoll
TARGETS += filesystems/fat
TARGETS += filesystems/overlayfs
TARGETS += filesystems/statmount
TARGETS += firmware
TARGETS += fpu
TARGETS += ftrace
TARGETS += futex
TARGETS += gpio
TARGETS += hid
TARGETS += intel_pstate
TARGETS += iommu
TARGETS += ipc
TARGETS += ir
TARGETS += kcmp
TARGETS += kexec
TARGETS += kvm
TARGETS += landlock
TARGETS += lib
TARGETS += livepatch
TARGETS += lkdtm
TARGETS += lsm
TARGETS += membarrier
TARGETS += memfd
TARGETS += memory-hotplug
TARGETS += mincore
TARGETS += mount
TARGETS += mount_setattr
TARGETS += move_mount_set_group
TARGETS += mqueue
TARGETS += nci
TARGETS += net
TARGETS += net/af_unix
TARGETS += net/forwarding
TARGETS += net/hsr
TARGETS += net/mptcp
TARGETS += net/openvswitch
TARGETS += net/tcp_ao
TARGETS += net/netfilter
TARGETS += nsfs
TARGETS += perf_events
TARGETS += pidfd
TARGETS += pid_namespace
TARGETS += power_supply
TARGETS += powerpc
TARGETS += prctl
TARGETS += proc
TARGETS += pstore
TARGETS += ptrace
TARGETS += openat2
TARGETS += resctrl
TARGETS += riscv
TARGETS += rlimits
TARGETS += rseq
TARGETS += rtc
TARGETS += rust
TARGETS += seccomp
TARGETS += sgx
TARGETS += sigaltstack
TARGETS += size
TARGETS += sparc64
TARGETS += splice
TARGETS += static_keys
TARGETS += sync
TARGETS += syscall_user_dispatch
TARGETS += sysctl
TARGETS += tc-testing
TARGETS += tdx
TARGETS += thermal/intel/power_floor
TARGETS += thermal/intel/workload_hint
TARGETS += timens
ifneq (1, $(quicktest))
TARGETS += timers
endif
TARGETS += tmpfs
TARGETS += tpm2
TARGETS += tty
TARGETS += uevent
TARGETS += user
TARGETS += user_events
TARGETS += vDSO
TARGETS += mm
TARGETS += x86
TARGETS += zram
#Please keep the TARGETS list alphabetically sorted
# Run "make quicktest=1 run_tests" or
# "make quicktest=1 kselftest" from top level Makefile

TARGETS_HOTPLUG = cpu-hotplug
TARGETS_HOTPLUG += memory-hotplug

# Networking tests want the net/lib target, include it automatically
ifneq ($(filter net drivers/net drivers/net/hw,$(TARGETS)),)
ifeq ($(filter net/lib,$(TARGETS)),)
	INSTALL_DEP_TARGETS := net/lib
endif
endif

# User can optionally provide a TARGETS skiplist.  By default we skip
# BPF since it has cutting edge build time dependencies which require
# more effort to install.
SKIP_TARGETS ?= bpf
ifneq ($(SKIP_TARGETS),)
	TMP := $(filter-out $(SKIP_TARGETS), $(TARGETS))
	override TARGETS := $(TMP)
endif

# User can set FORCE_TARGETS to 1 to require all targets to be successfully
# built; make will fail if any of the targets cannot be built. If
# FORCE_TARGETS is not set (the default), make will succeed if at least one
# of the targets gets built.
FORCE_TARGETS ?=

# Clear LDFLAGS and MAKEFLAGS when implicit rules are missing.  This provides
# implicit rules to sub-test Makefiles which avoids build failures in test
# Makefile that don't have explicit build rules.
ifeq (,$(LINK.c))
override LDFLAGS =
override MAKEFLAGS =
endif

# Append kselftest to KBUILD_OUTPUT and O to avoid cluttering
# KBUILD_OUTPUT with selftest objects and headers installed
# by selftests Makefile or lib.mk.
ifdef building_out_of_srctree
override LDFLAGS =
endif

top_srcdir ?= ../../..

ifeq ("$(origin O)", "command line")
  KBUILD_OUTPUT := $(O)
endif

ifneq ($(KBUILD_OUTPUT),)
  # Make's built-in functions such as $(abspath ...), $(realpath ...) cannot
  # expand a shell special character '~'. We use a somewhat tedious way here.
  abs_objtree := $(shell cd $(top_srcdir) && mkdir -p $(KBUILD_OUTPUT) && cd $(KBUILD_OUTPUT) && pwd)
  $(if $(abs_objtree),, \
    $(error failed to create output directory "$(KBUILD_OUTPUT)"))
  # $(realpath ...) resolves symlinks
  abs_objtree := $(realpath $(abs_objtree))
  BUILD := $(abs_objtree)/kselftest
  KHDR_INCLUDES := -isystem ${abs_objtree}/usr/include
else
  BUILD := $(CURDIR)
  abs_srctree := $(shell cd $(top_srcdir) && pwd)
  KHDR_INCLUDES := -isystem ${abs_srctree}/usr/include
  DEFAULT_INSTALL_HDR_PATH := 1
endif

# Prepare for headers install
include $(top_srcdir)/scripts/subarch.include
ARCH           ?= $(SUBARCH)
export BUILD
export KHDR_INCLUDES

# set default goal to all, so make without a target runs all, even when
# all isn't the first target in the file.
.DEFAULT_GOAL := all

all:
	@ret=1;							\
	for TARGET in $(TARGETS); do				\
		BUILD_TARGET=$$BUILD/$$TARGET;			\
		mkdir $$BUILD_TARGET  -p;			\
		$(MAKE) OUTPUT=$$BUILD_TARGET -C $$TARGET	\
				O=$(abs_objtree)		\
				$(if $(FORCE_TARGETS),|| exit);	\
		ret=$$((ret * $$?));				\
	done; exit $$ret;

run_tests: all
	@for TARGET in $(TARGETS); do \
		BUILD_TARGET=$$BUILD/$$TARGET;	\
		$(MAKE) OUTPUT=$$BUILD_TARGET -C $$TARGET run_tests \
				SRC_PATH=$(shell readlink -e $$(pwd)) \
				OBJ_PATH=$(BUILD)                   \
				O=$(abs_objtree);		    \
	done;

hotplug:
	@for TARGET in $(TARGETS_HOTPLUG); do \
		BUILD_TARGET=$$BUILD/$$TARGET;	\
		$(MAKE) OUTPUT=$$BUILD_TARGET -C $$TARGET;\
	done;

run_hotplug: hotplug
	@for TARGET in $(TARGETS_HOTPLUG); do \
		BUILD_TARGET=$$BUILD/$$TARGET;	\
		$(MAKE) OUTPUT=$$BUILD_TARGET -C $$TARGET run_full_test;\
	done;

clean_hotplug:
	@for TARGET in $(TARGETS_HOTPLUG); do \
		BUILD_TARGET=$$BUILD/$$TARGET;	\
		$(MAKE) OUTPUT=$$BUILD_TARGET -C $$TARGET clean;\
	done;

run_pstore_crash:
	$(MAKE) -C pstore run_crash

# Use $BUILD as the default install root. $BUILD points to the
# right output location for the following cases:
# 1. output_dir=kernel_src
# 2. a separate output directory is specified using O= KBUILD_OUTPUT
# 3. a separate output directory is specified using KBUILD_OUTPUT
# Avoid conflict with INSTALL_PATH set by the main Makefile
#
KSFT_INSTALL_PATH ?= $(BUILD)/kselftest_install
KSFT_INSTALL_PATH := $(abspath $(KSFT_INSTALL_PATH))
# Avoid changing the rest of the logic here and lib.mk.
INSTALL_PATH := $(KSFT_INSTALL_PATH)
ALL_SCRIPT := $(INSTALL_PATH)/run_kselftest.sh
TEST_LIST := $(INSTALL_PATH)/kselftest-list.txt

install: all
ifdef INSTALL_PATH
	@# Ask all targets to install their files
	mkdir -p $(INSTALL_PATH)/kselftest
	install -m 744 kselftest/module.sh $(INSTALL_PATH)/kselftest/
	install -m 744 kselftest/runner.sh $(INSTALL_PATH)/kselftest/
	install -m 744 kselftest/prefix.pl $(INSTALL_PATH)/kselftest/
	install -m 744 kselftest/ktap_helpers.sh $(INSTALL_PATH)/kselftest/
	install -m 744 run_kselftest.sh $(INSTALL_PATH)/
	rm -f $(TEST_LIST)
	@ret=1;	\
	for TARGET in $(TARGETS) $(INSTALL_DEP_TARGETS); do \
		BUILD_TARGET=$$BUILD/$$TARGET;	\
		$(MAKE) OUTPUT=$$BUILD_TARGET -C $$TARGET install \
				INSTALL_PATH=$(INSTALL_PATH)/$$TARGET \
				SRC_PATH=$(shell readlink -e $$(pwd)) \
				OBJ_PATH=$(INSTALL_PATH) \
				O=$(abs_objtree)		\
				$(if $(FORCE_TARGETS),|| exit);	\
		ret=$$((ret * $$?));		\
	done; exit $$ret;


	@# Ask all targets to emit their test scripts
	@# While building kselftest-list.text skip also non-existent TARGET dirs:
	@# they could be the result of a build failure and should NOT be
	@# included in the generated runlist.
	for TARGET in $(TARGETS); do \
		BUILD_TARGET=$$BUILD/$$TARGET;	\
		[ ! -d $(INSTALL_PATH)/$$TARGET ] && printf "Skipping non-existent dir: $$TARGET\n" && continue; \
		printf "Emit Tests for $$TARGET\n"; \
		$(MAKE) -s --no-print-directory OUTPUT=$$BUILD_TARGET COLLECTION=$$TARGET \
			-C $$TARGET emit_tests >> $(TEST_LIST); \
	done;
else
	$(error Error: set INSTALL_PATH to use install)
endif

FORMAT ?= .gz
TAR_PATH = $(abspath ${INSTALL_PATH}/kselftest-packages/kselftest.tar${FORMAT})
gen_tar: install
	@mkdir -p ${INSTALL_PATH}/kselftest-packages/
	@tar caf ${TAR_PATH} --exclude=kselftest-packages -C ${INSTALL_PATH} .
	@echo "Created ${TAR_PATH}"

clean:
	@for TARGET in $(TARGETS); do \
		BUILD_TARGET=$$BUILD/$$TARGET;	\
		$(MAKE) OUTPUT=$$BUILD_TARGET -C $$TARGET clean;\
	done;

.PHONY: all run_tests hotplug run_hotplug clean_hotplug run_pstore_crash install clean gen_tar
