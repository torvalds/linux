#!/bin/sh

HEADERS='
include/uapi/linux/perf_event.h
include/linux/hash.h
include/uapi/linux/hw_breakpoint.h
arch/x86/include/asm/disabled-features.h
arch/x86/include/asm/required-features.h
arch/x86/include/asm/cpufeatures.h
arch/arm/include/uapi/asm/perf_regs.h
arch/arm64/include/uapi/asm/perf_regs.h
arch/powerpc/include/uapi/asm/perf_regs.h
arch/x86/include/uapi/asm/perf_regs.h
arch/x86/include/uapi/asm/kvm.h
arch/x86/include/uapi/asm/kvm_perf.h
arch/x86/include/uapi/asm/svm.h
arch/x86/include/uapi/asm/vmx.h
arch/powerpc/include/uapi/asm/kvm.h
arch/s390/include/uapi/asm/kvm.h
arch/s390/include/uapi/asm/kvm_perf.h
arch/s390/include/uapi/asm/sie.h
arch/arm/include/uapi/asm/kvm.h
arch/arm64/include/uapi/asm/kvm.h
include/asm-generic/bitops/arch_hweight.h
include/asm-generic/bitops/const_hweight.h
include/asm-generic/bitops/__fls.h
include/asm-generic/bitops/fls.h
include/asm-generic/bitops/fls64.h
include/linux/coresight-pmu.h
include/uapi/asm-generic/mman-common.h
'

check () {
  file=$1
  opts=

  shift
  while [ -n "$*" ]; do
    opts="$opts \"$1\""
    shift
  done

  cmd="diff $opts ../$file ../../$file > /dev/null"

  test -f ../../$file &&
  eval $cmd || echo "Warning: $file differs from kernel" >&2
}


# simple diff check
for i in $HEADERS; do
  check $i -B
done

# diff with extra ignore lines
check arch/x86/lib/memcpy_64.S        -B -I "^EXPORT_SYMBOL" -I "^#include <asm/export.h>"
check arch/x86/lib/memset_64.S        -B -I "^EXPORT_SYMBOL" -I "^#include <asm/export.h>"
check include/uapi/asm-generic/mman.h -B -I "^#include <\(uapi/\)*asm-generic/mman-common.h>"
check include/uapi/linux/mman.h       -B -I "^#include <\(uapi/\)*asm/mman.h>"
