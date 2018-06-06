#!/bin/sh
# SPDX-License-Identifier: GPL-2.0

HEADERS='
include/uapi/drm/drm.h
include/uapi/drm/i915_drm.h
include/uapi/linux/fcntl.h
include/uapi/linux/kcmp.h
include/uapi/linux/kvm.h
include/uapi/linux/perf_event.h
include/uapi/linux/prctl.h
include/uapi/linux/sched.h
include/uapi/linux/stat.h
include/uapi/linux/vhost.h
include/uapi/sound/asound.h
include/linux/hash.h
include/uapi/linux/hw_breakpoint.h
arch/x86/include/asm/disabled-features.h
arch/x86/include/asm/required-features.h
arch/x86/include/asm/cpufeatures.h
arch/arm/include/uapi/asm/perf_regs.h
arch/arm64/include/uapi/asm/perf_regs.h
arch/powerpc/include/uapi/asm/perf_regs.h
arch/s390/include/uapi/asm/perf_regs.h
arch/x86/include/uapi/asm/perf_regs.h
arch/x86/include/uapi/asm/kvm.h
arch/x86/include/uapi/asm/kvm_perf.h
arch/x86/include/uapi/asm/svm.h
arch/x86/include/uapi/asm/unistd.h
arch/x86/include/uapi/asm/vmx.h
arch/powerpc/include/uapi/asm/kvm.h
arch/s390/include/uapi/asm/kvm.h
arch/s390/include/uapi/asm/kvm_perf.h
arch/s390/include/uapi/asm/ptrace.h
arch/s390/include/uapi/asm/sie.h
arch/arm/include/uapi/asm/kvm.h
arch/arm64/include/uapi/asm/kvm.h
arch/alpha/include/uapi/asm/errno.h
arch/mips/include/asm/errno.h
arch/mips/include/uapi/asm/errno.h
arch/parisc/include/uapi/asm/errno.h
arch/powerpc/include/uapi/asm/errno.h
arch/sparc/include/uapi/asm/errno.h
arch/x86/include/uapi/asm/errno.h
arch/powerpc/include/uapi/asm/unistd.h
include/asm-generic/bitops/arch_hweight.h
include/asm-generic/bitops/const_hweight.h
include/asm-generic/bitops/__fls.h
include/asm-generic/bitops/fls.h
include/asm-generic/bitops/fls64.h
include/linux/coresight-pmu.h
include/uapi/asm-generic/errno.h
include/uapi/asm-generic/errno-base.h
include/uapi/asm-generic/ioctls.h
include/uapi/asm-generic/mman-common.h
'

check_2 () {
  file1=$1
  file2=$2

  shift
  shift

  cmd="diff $* $file1 $file2 > /dev/null"

  test -f $file2 &&
  eval $cmd || echo "Warning: Kernel ABI header at 'tools/$file' differs from latest version at '$file'" >&2
}

check () {
  file=$1

  shift

  check_2 ../$file ../../$file $*
}

# Check if we have the kernel headers (tools/perf/../../include), else
# we're probably on a detached tarball, so no point in trying to check
# differences.
test -d ../../include || exit 0

# simple diff check
for i in $HEADERS; do
  check $i -B
done

# diff with extra ignore lines
check arch/x86/lib/memcpy_64.S        '-I "^EXPORT_SYMBOL" -I "^#include <asm/export.h>"'
check arch/x86/lib/memset_64.S        '-I "^EXPORT_SYMBOL" -I "^#include <asm/export.h>"'
check include/uapi/asm-generic/mman.h '-I "^#include <\(uapi/\)*asm-generic/mman-common.h>"'
check include/uapi/linux/mman.h       '-I "^#include <\(uapi/\)*asm/mman.h>"'
