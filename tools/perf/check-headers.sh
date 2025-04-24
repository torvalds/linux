#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

YELLOW='\033[0;33m'
NC='\033[0m' # No Color

declare -a FILES
FILES=(
  "include/uapi/linux/const.h"
  "include/uapi/drm/drm.h"
  "include/uapi/drm/i915_drm.h"
  "include/uapi/linux/bits.h"
  "include/uapi/linux/fadvise.h"
  "include/uapi/linux/fscrypt.h"
  "include/uapi/linux/kcmp.h"
  "include/uapi/linux/kvm.h"
  "include/uapi/linux/in.h"
  "include/uapi/linux/perf_event.h"
  "include/uapi/linux/seccomp.h"
  "include/uapi/linux/stat.h"
  "include/linux/bits.h"
  "include/vdso/bits.h"
  "include/linux/const.h"
  "include/vdso/const.h"
  "include/vdso/unaligned.h"
  "include/linux/hash.h"
  "include/linux/list-sort.h"
  "include/uapi/linux/hw_breakpoint.h"
  "arch/x86/include/asm/cpufeatures.h"
  "arch/x86/include/asm/inat_types.h"
  "arch/x86/include/asm/emulate_prefix.h"
  "arch/x86/include/asm/msr-index.h"
  "arch/x86/lib/x86-opcode-map.txt"
  "arch/x86/tools/gen-insn-attr-x86.awk"
  "arch/arm/include/uapi/asm/perf_regs.h"
  "arch/arm64/include/uapi/asm/perf_regs.h"
  "arch/loongarch/include/uapi/asm/perf_regs.h"
  "arch/mips/include/uapi/asm/perf_regs.h"
  "arch/powerpc/include/uapi/asm/perf_regs.h"
  "arch/s390/include/uapi/asm/perf_regs.h"
  "arch/x86/include/uapi/asm/perf_regs.h"
  "arch/x86/include/uapi/asm/kvm.h"
  "arch/x86/include/uapi/asm/kvm_perf.h"
  "arch/x86/include/uapi/asm/svm.h"
  "arch/x86/include/uapi/asm/unistd.h"
  "arch/x86/include/uapi/asm/vmx.h"
  "arch/powerpc/include/uapi/asm/kvm.h"
  "arch/s390/include/uapi/asm/kvm.h"
  "arch/s390/include/uapi/asm/kvm_perf.h"
  "arch/s390/include/uapi/asm/sie.h"
  "arch/arm/include/uapi/asm/kvm.h"
  "arch/arm64/include/uapi/asm/kvm.h"
  "arch/arm64/include/uapi/asm/unistd.h"
  "arch/alpha/include/uapi/asm/errno.h"
  "arch/mips/include/asm/errno.h"
  "arch/mips/include/uapi/asm/errno.h"
  "arch/parisc/include/uapi/asm/errno.h"
  "arch/powerpc/include/uapi/asm/errno.h"
  "arch/sparc/include/uapi/asm/errno.h"
  "arch/x86/include/uapi/asm/errno.h"
  "include/asm-generic/bitops/arch_hweight.h"
  "include/asm-generic/bitops/const_hweight.h"
  "include/asm-generic/bitops/__fls.h"
  "include/asm-generic/bitops/fls.h"
  "include/asm-generic/bitops/fls64.h"
  "include/linux/coresight-pmu.h"
  "include/uapi/asm-generic/errno.h"
  "include/uapi/asm-generic/errno-base.h"
  "include/uapi/asm-generic/ioctls.h"
  "include/uapi/asm-generic/mman-common.h"
  "include/uapi/asm-generic/unistd.h"
  "scripts/syscall.tbl"
)

declare -a SYNC_CHECK_FILES
SYNC_CHECK_FILES=(
  "arch/x86/include/asm/inat.h"
  "arch/x86/include/asm/insn.h"
  "arch/x86/lib/inat.c"
  "arch/x86/lib/insn.c"
)

# These copies are under tools/perf/trace/beauty/ as they are not used to in
# building object files only by scripts in tools/perf/trace/beauty/ to generate
# tables that then gets included in .c files for things like id->string syscall
# tables (and the reverse lookup as well: string -> id)

declare -a BEAUTY_FILES
BEAUTY_FILES=(
  "arch/x86/include/asm/irq_vectors.h"
  "arch/x86/include/uapi/asm/prctl.h"
  "include/linux/socket.h"
  "include/uapi/linux/fcntl.h"
  "include/uapi/linux/fs.h"
  "include/uapi/linux/mount.h"
  "include/uapi/linux/prctl.h"
  "include/uapi/linux/sched.h"
  "include/uapi/linux/stat.h"
  "include/uapi/linux/usbdevice_fs.h"
  "include/uapi/linux/vhost.h"
  "include/uapi/sound/asound.h"
)

declare -a FAILURES

check_2 () {
  tools_file=$1
  orig_file=$2

  shift
  shift

  cmd="diff $* $tools_file $orig_file > /dev/null"

  if [ -f "$orig_file" ] && ! eval "$cmd"
  then
    FAILURES+=(
      "$tools_file $orig_file"
    )
  fi
}

check () {
  file=$1

  shift

  check_2 "tools/$file" "$file" "$@"
}

beauty_check () {
  file=$1

  shift

  check_2 "tools/perf/trace/beauty/$file" "$file" "$@"
}

check_ignore_some_hunks () {
  orig_file="$1"
  tools_file="tools/$orig_file"
  hunks_to_ignore="tools/perf/check-header_ignore_hunks/$orig_file"

  if [ ! -f "$hunks_to_ignore" ]; then
    echo "$hunks_to_ignore not found. Skipping $orig_file check."
    FAILURES+=(
      "$tools_file $orig_file"
    )
    return
  fi

  cmd="diff -u \"$tools_file\" \"$orig_file\" | grep -vf \"$hunks_to_ignore\" | wc -l | grep -qw 0"

  if [ -f "$orig_file" ] && ! eval "$cmd"
  then
    FAILURES+=(
      "$tools_file $orig_file"
    )
  fi
}


# Check if we have the kernel headers (tools/perf/../../include), else
# we're probably on a detached tarball, so no point in trying to check
# differences.
if ! [ -d ../../include ]
then
  echo -e "${YELLOW}Warning${NC}: Skipped check-headers due to missing ../../include"
  exit 0
fi

cd ../..

# simple diff check
for i in "${FILES[@]}"
do
  check "$i" -B
done

for i in "${SYNC_CHECK_FILES[@]}"
do
  check "$i" '-I "^.*\/\*.*__ignore_sync_check__.*\*\/.*$"'
done

# diff with extra ignore lines
check arch/x86/lib/memcpy_64.S        '-I "^EXPORT_SYMBOL" -I "^#include <asm/export.h>" -I"^SYM_FUNC_START\(_LOCAL\)*(memcpy_\(erms\|orig\))" -I"^#include <linux/cfi_types.h>"'
check arch/x86/lib/memset_64.S        '-I "^EXPORT_SYMBOL" -I "^#include <asm/export.h>" -I"^SYM_FUNC_START\(_LOCAL\)*(memset_\(erms\|orig\))"'
check arch/x86/include/asm/amd-ibs.h  '-I "^#include [<\"]\(asm/\)*msr-index.h"'
check arch/arm64/include/asm/cputype.h '-I "^#include [<\"]\(asm/\)*sysreg.h"'
check include/linux/unaligned.h '-I "^#include <linux/unaligned/packed_struct.h>" -I "^#include <asm/byteorder.h>" -I "^#pragma GCC diagnostic"'
check include/uapi/asm-generic/mman.h '-I "^#include <\(uapi/\)*asm-generic/mman-common\(-tools\)*.h>"'
check include/uapi/linux/mman.h       '-I "^#include <\(uapi/\)*asm/mman.h>"'
check include/linux/build_bug.h       '-I "^#\(ifndef\|endif\)\( \/\/\)* static_assert$"'
check include/linux/ctype.h	      '-I "isdigit("'
check lib/ctype.c		      '-I "^EXPORT_SYMBOL" -I "^#include <linux/export.h>" -B'

# diff non-symmetric files
check_2 tools/perf/arch/x86/entry/syscalls/syscall_32.tbl arch/x86/entry/syscalls/syscall_32.tbl
check_2 tools/perf/arch/x86/entry/syscalls/syscall_64.tbl arch/x86/entry/syscalls/syscall_64.tbl
check_2 tools/perf/arch/powerpc/entry/syscalls/syscall.tbl arch/powerpc/kernel/syscalls/syscall.tbl
check_2 tools/perf/arch/s390/entry/syscalls/syscall.tbl arch/s390/kernel/syscalls/syscall.tbl
check_2 tools/perf/arch/mips/entry/syscalls/syscall_n64.tbl arch/mips/kernel/syscalls/syscall_n64.tbl
check_2 tools/perf/arch/arm/entry/syscalls/syscall.tbl arch/arm/tools/syscall.tbl
check_2 tools/perf/arch/sh/entry/syscalls/syscall.tbl arch/sh/kernel/syscalls/syscall.tbl
check_2 tools/perf/arch/sparc/entry/syscalls/syscall.tbl arch/sparc/kernel/syscalls/syscall.tbl
check_2 tools/perf/arch/xtensa/entry/syscalls/syscall.tbl arch/xtensa/kernel/syscalls/syscall.tbl
check_2 tools/perf/arch/alpha/entry/syscalls/syscall.tbl arch/alpha/entry/syscalls/syscall.tbl
check_2 tools/perf/arch/parisc/entry/syscalls/syscall.tbl arch/parisc/entry/syscalls/syscall.tbl
check_2 tools/perf/arch/arm64/entry/syscalls/syscall_32.tbl arch/arm64/entry/syscalls/syscall_32.tbl
check_2 tools/perf/arch/arm64/entry/syscalls/syscall_64.tbl arch/arm64/entry/syscalls/syscall_64.tbl

for i in "${BEAUTY_FILES[@]}"
do
  beauty_check "$i" -B
done

# check duplicated library files
check_2 tools/perf/util/hashmap.h tools/lib/bpf/hashmap.h
check_2 tools/perf/util/hashmap.c tools/lib/bpf/hashmap.c

# Files with larger differences

check_ignore_some_hunks lib/list_sort.c

cd tools/perf || exit

if [ ${#FAILURES[@]} -gt 0 ]
then
  echo -e "${YELLOW}Warning${NC}: Kernel ABI header differences:"
  for i in "${FAILURES[@]}"
  do
    echo "  diff -u $i"
  done
fi
