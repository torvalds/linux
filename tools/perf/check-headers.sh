#!/bin/sh
# SPDX-License-Identifier: GPL-2.0

FILES='
include/uapi/linux/const.h
include/uapi/drm/drm.h
include/uapi/drm/i915_drm.h
include/uapi/linux/fadvise.h
include/uapi/linux/fcntl.h
include/uapi/linux/fs.h
include/uapi/linux/fscrypt.h
include/uapi/linux/kcmp.h
include/uapi/linux/kvm.h
include/uapi/linux/in.h
include/uapi/linux/mount.h
include/uapi/linux/openat2.h
include/uapi/linux/perf_event.h
include/uapi/linux/prctl.h
include/uapi/linux/sched.h
include/uapi/linux/stat.h
include/uapi/linux/usbdevice_fs.h
include/uapi/linux/vhost.h
include/uapi/sound/asound.h
include/linux/bits.h
include/vdso/bits.h
include/linux/const.h
include/vdso/const.h
include/linux/hash.h
include/uapi/linux/hw_breakpoint.h
arch/x86/include/asm/disabled-features.h
arch/x86/include/asm/required-features.h
arch/x86/include/asm/cpufeatures.h
arch/x86/include/asm/inat_types.h
arch/x86/include/asm/emulate_prefix.h
arch/x86/include/asm/irq_vectors.h
arch/x86/include/asm/msr-index.h
arch/x86/include/uapi/asm/prctl.h
arch/x86/lib/x86-opcode-map.txt
arch/x86/tools/gen-insn-attr-x86.awk
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
arch/arm64/include/uapi/asm/unistd.h
arch/alpha/include/uapi/asm/errno.h
arch/mips/include/asm/errno.h
arch/mips/include/uapi/asm/errno.h
arch/parisc/include/uapi/asm/errno.h
arch/powerpc/include/uapi/asm/errno.h
arch/sparc/include/uapi/asm/errno.h
arch/x86/include/uapi/asm/errno.h
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
include/uapi/asm-generic/unistd.h
'

check_2 () {
  file1=$1
  file2=$2

  shift
  shift

  cmd="diff $* $file1 $file2 > /dev/null"

  test -f $file2 && {
    eval $cmd || {
      echo "Warning: Kernel ABI header at '$file1' differs from latest version at '$file2'" >&2
      echo diff -u $file1 $file2
    }
  }
}

check () {
  file=$1

  shift

  check_2 tools/$file $file $*
}

# Check if we have the kernel headers (tools/perf/../../include), else
# we're probably on a detached tarball, so no point in trying to check
# differences.
test -d ../../include || exit 0

cd ../..

# simple diff check
for i in $FILES; do
  check $i -B
done

# diff with extra ignore lines
check arch/x86/lib/memcpy_64.S        '-I "^EXPORT_SYMBOL" -I "^#include <asm/export.h>" -I"^SYM_FUNC_START\(_LOCAL\)*(memcpy_\(erms\|orig\))"'
check arch/x86/lib/memset_64.S        '-I "^EXPORT_SYMBOL" -I "^#include <asm/export.h>" -I"^SYM_FUNC_START\(_LOCAL\)*(memset_\(erms\|orig\))"'
check include/uapi/asm-generic/mman.h '-I "^#include <\(uapi/\)*asm-generic/mman-common\(-tools\)*.h>"'
check include/uapi/linux/mman.h       '-I "^#include <\(uapi/\)*asm/mman.h>"'
check include/linux/build_bug.h       '-I "^#\(ifndef\|endif\)\( \/\/\)* static_assert$"'
check include/linux/ctype.h	      '-I "isdigit("'
check lib/ctype.c		      '-I "^EXPORT_SYMBOL" -I "^#include <linux/export.h>" -B'
check arch/x86/include/asm/inat.h     '-I "^#include [\"<]\(asm/\)*inat_types.h[\">]"'
check arch/x86/include/asm/insn.h     '-I "^#include [\"<]\(asm/\)*inat.h[\">]"'
check arch/x86/lib/inat.c	      '-I "^#include [\"<]\(../include/\)*asm/insn.h[\">]"'
check arch/x86/lib/insn.c             '-I "^#include [\"<]\(../include/\)*asm/in\(at\|sn\).h[\">]" -I "^#include [\"<]\(../include/\)*asm/emulate_prefix.h[\">]"'

# diff non-symmetric files
check_2 tools/perf/arch/x86/entry/syscalls/syscall_64.tbl arch/x86/entry/syscalls/syscall_64.tbl

# check duplicated library files
check_2 tools/perf/util/hashmap.h tools/lib/bpf/hashmap.h
check_2 tools/perf/util/hashmap.c tools/lib/bpf/hashmap.c

cd tools/perf
