# SPDX-License-Identifier: GPL-2.0
TARGETS = android
TARGETS += bpf
TARGETS += breakpoints
TARGETS += capabilities
TARGETS += cgroup
TARGETS += cpufreq
TARGETS += cpu-hotplug
TARGETS += drivers/dma-buf
TARGETS += efivarfs
TARGETS += exec
TARGETS += filesystems
TARGETS += filesystems/binderfs
TARGETS += firmware
TARGETS += ftrace
TARGETS += futex
TARGETS += gpio
TARGETS += intel_pstate
TARGETS += ipc
TARGETS += ir
TARGETS += kcmp
TARGETS += kexec
TARGETS += kvm
TARGETS += lib
TARGETS += livepatch
TARGETS += membarrier
TARGETS += memfd
TARGETS += memory-hotplug
TARGETS += mount
TARGETS += mqueue
TARGETS += net
TARGETS += netfilter
TARGETS += networking/timestamping
TARGETS += nsfs
TARGETS += pidfd
TARGETS += powerpc
TARGETS += proc
TARGETS += pstore
TARGETS += ptrace
TARGETS += rseq
TARGETS += rtc
TARGETS += seccomp
TARGETS += sigaltstack
TARGETS += size
TARGETS += sparc64
TARGETS += splice
TARGETS += static_keys
TARGETS += sync
TARGETS += sysctl
ifneq (1, $(quicktest))
TARGETS += timers
endif
TARGETS += tmpfs
TARGETS += tpm2
TARGETS += user
TARGETS += vm
TARGETS += x86
TARGETS += zram
#Please keep the TARGETS list alphabetically sorted
# Run "make quicktest=1 run_tests" or
# "make quicktest=1 kselftest" from top level Makefile

TARGETS_HOTPLUG = cpu-hotplug
TARGETS_HOTPLUG += memory-hotplug

# Clear LDFLAGS and MAKEFLAGS if called from main
# Makefile to avoid test build failures when test
# Makefile doesn't have explicit build rules.
ifeq (1,$(MAKELEVEL))
override LDFLAGS =
override MAKEFLAGS =
endif

ifneq ($(KBUILD_SRC),)
override LDFLAGS =
endif

BUILD := $(O)
ifndef BUILD
  BUILD := $(KBUILD_OUTPUT)
endif
ifndef BUILD
  BUILD := $(shell pwd)
endif

# KSFT_TAP_LEVEL is used from KSFT framework to prevent nested TAP header
# printing from tests. Applicable to run_tests case where run_tests adds
# TAP header prior running tests and when a test program invokes another
# with system() call. Export it here to cover override RUN_TESTS defines.
export KSFT_TAP_LEVEL=`echo 1`

export BUILD
all:
	@for TARGET in $(TARGETS); do		\
		BUILD_TARGET=$$BUILD/$$TARGET;	\
		mkdir $$BUILD_TARGET  -p;	\
		make OUTPUT=$$BUILD_TARGET -C $$TARGET;\
	done;

run_tests: all
	@for TARGET in $(TARGETS); do \
		BUILD_TARGET=$$BUILD/$$TARGET;	\
		make OUTPUT=$$BUILD_TARGET -C $$TARGET run_tests;\
	done;

hotplug:
	@for TARGET in $(TARGETS_HOTPLUG); do \
		BUILD_TARGET=$$BUILD/$$TARGET;	\
		make OUTPUT=$$BUILD_TARGET -C $$TARGET;\
	done;

run_hotplug: hotplug
	@for TARGET in $(TARGETS_HOTPLUG); do \
		BUILD_TARGET=$$BUILD/$$TARGET;	\
		make OUTPUT=$$BUILD_TARGET -C $$TARGET run_full_test;\
	done;

clean_hotplug:
	@for TARGET in $(TARGETS_HOTPLUG); do \
		BUILD_TARGET=$$BUILD/$$TARGET;	\
		make OUTPUT=$$BUILD_TARGET -C $$TARGET clean;\
	done;

run_pstore_crash:
	make -C pstore run_crash

INSTALL_PATH ?= install
INSTALL_PATH := $(abspath $(INSTALL_PATH))
ALL_SCRIPT := $(INSTALL_PATH)/run_kselftest.sh

install:
ifdef INSTALL_PATH
	@# Ask all targets to install their files
	mkdir -p $(INSTALL_PATH)
	@for TARGET in $(TARGETS); do \
		BUILD_TARGET=$$BUILD/$$TARGET;	\
		make OUTPUT=$$BUILD_TARGET -C $$TARGET INSTALL_PATH=$(INSTALL_PATH)/$$TARGET install; \
	done;

	@# Ask all targets to emit their test scripts
	echo "#!/bin/sh" > $(ALL_SCRIPT)
	echo "BASE_DIR=\$$(realpath \$$(dirname \$$0))" >> $(ALL_SCRIPT)
	echo "cd \$$BASE_DIR" >> $(ALL_SCRIPT)
	echo "ROOT=\$$PWD" >> $(ALL_SCRIPT)
	echo "if [ \"\$$1\" = \"--summary\" ]; then" >> $(ALL_SCRIPT)
	echo "  OUTPUT=\$$BASE_DIR/output.log" >> $(ALL_SCRIPT)
	echo "  cat /dev/null > \$$OUTPUT" >> $(ALL_SCRIPT)
	echo "else" >> $(ALL_SCRIPT)
	echo "  OUTPUT=/dev/stdout" >> $(ALL_SCRIPT)
	echo "fi" >> $(ALL_SCRIPT)
	echo "export KSFT_TAP_LEVEL=1" >> $(ALL_SCRIPT)
	echo "export skip=4" >> $(ALL_SCRIPT)

	for TARGET in $(TARGETS); do \
		BUILD_TARGET=$$BUILD/$$TARGET;	\
		echo "echo ; echo TAP version 13" >> $(ALL_SCRIPT);	\
		echo "echo Running tests in $$TARGET" >> $(ALL_SCRIPT); \
		echo "echo ========================================" >> $(ALL_SCRIPT); \
		echo "[ -w /dev/kmsg ] && echo \"kselftest: Running tests in $$TARGET\" >> /dev/kmsg" >> $(ALL_SCRIPT); \
		echo "cd $$TARGET" >> $(ALL_SCRIPT); \
		make -s --no-print-directory OUTPUT=$$BUILD_TARGET -C $$TARGET emit_tests >> $(ALL_SCRIPT); \
		echo "cd \$$ROOT" >> $(ALL_SCRIPT); \
	done;

	chmod u+x $(ALL_SCRIPT)
else
	$(error Error: set INSTALL_PATH to use install)
endif

clean:
	@for TARGET in $(TARGETS); do \
		BUILD_TARGET=$$BUILD/$$TARGET;	\
		make OUTPUT=$$BUILD_TARGET -C $$TARGET clean;\
	done;

.PHONY: all run_tests hotplug run_hotplug clean_hotplug run_pstore_crash install clean
