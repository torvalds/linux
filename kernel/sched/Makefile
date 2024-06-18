# SPDX-License-Identifier: GPL-2.0

# The compilers are complaining about unused variables inside an if(0) scope
# block. This is daft, shut them up.
ccflags-y += $(call cc-disable-warning, unused-but-set-variable)

# These files are disabled because they produce non-interesting flaky coverage
# that is not a function of syscall inputs. E.g. involuntary context switches.
KCOV_INSTRUMENT := n

# Disable KCSAN to avoid excessive noise and performance degradation. To avoid
# false positives ensure barriers implied by sched functions are instrumented.
KCSAN_SANITIZE := n
KCSAN_INSTRUMENT_BARRIERS := y

ifneq ($(CONFIG_SCHED_OMIT_FRAME_POINTER),y)
# According to Alan Modra <alan@linuxcare.com.au>, the -fno-omit-frame-pointer is
# needed for x86 only.  Why this used to be enabled for all architectures is beyond
# me.  I suspect most platforms don't need this, but until we know that for sure
# I turn this off for IA-64 only.  Andreas Schwab says it's also needed on m68k
# to get a correct value for the wait-channel (WCHAN in ps). --davidm
CFLAGS_core.o := $(PROFILING) -fno-omit-frame-pointer
endif

#
# Build efficiency:
#
# These compilation units have roughly the same size and complexity - so their
# build parallelizes well and finishes roughly at once:
#
obj-y += core.o
obj-y += fair.o
obj-y += build_policy.o
obj-y += build_utility.o
