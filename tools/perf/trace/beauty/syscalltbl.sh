#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
#
# Generate all syscall tables.
#
# Each line of the syscall table should have the following format:
#
# NR ABI NAME [NATIVE] [COMPAT [noreturn]]
#
# NR       syscall number
# ABI      ABI name
# NAME     syscall name
# NATIVE   native entry point (optional)
# COMPAT   compat entry point (optional)
# noreturn system call doesn't return (optional)
set -e

usage() {
       cat >&2 <<EOF
usage: $0 <TOOLS DIRECTORY> <OUTFILE>

  <TOOLS DIRECTORY>    path to kernel tools directory
  <OUTFILE>            output header file
EOF
       exit 1
}

if [ $# -ne 2 ]; then
       usage
fi
tools_dir=$1
outfile=$2

build_tables() {
	infile="$1"
	outfile="$2"
	abis=$(echo "($3)" | tr ',' '|')
	e_machine="$4"

	if [ ! -f "$infile" ]
	then
		echo "Missing file $infile"
		exit 1
	fi
	sorted_table=$(mktemp /tmp/syscalltbl.XXXXXX)
	grep -E "^[0-9]+[[:space:]]+$abis" "$infile" | sort -n > "$sorted_table"

	echo "static const char *const syscall_num_to_name_${e_machine}[] = {" >> "$outfile"
	# the params are: nr abi name entry compat
	# use _ for intentionally unused variables according to SC2034
	while read -r nr _ name _ _; do
		echo "	[$nr] = \"$name\"," >> "$outfile"
	done < "$sorted_table"
	echo "};" >> "$outfile"

	echo "static const uint16_t syscall_sorted_names_${e_machine}[] = {" >> "$outfile"

	# When sorting by name, add a suffix of 0s upto 20 characters so that
	# system calls that differ with a numerical suffix don't sort before
	# those without. This default behavior of sort differs from that of
	# strcmp used at runtime. Use sed to strip the trailing 0s suffix
	# afterwards.
	grep -E "^[0-9]+[[:space:]]+$abis" "$infile" | awk '{printf $3; for (i = length($3); i < 20; i++) { printf "0"; }; print " " $1}'| sort | sed 's/\([a-zA-Z1-9]\+\)0\+ \([0-9]\+\)/\1 \2/' > "$sorted_table"
	while read -r name nr; do
		echo "	$nr,	/* $name */" >> "$outfile"
	done < "$sorted_table"
	echo "};" >> "$outfile"

	rm -f "$sorted_table"
}

rm -f "$outfile"
cat >> "$outfile" <<EOF
#include <elf.h>
#include <stdint.h>
#include <asm/bitsperlong.h>
#include <linux/kernel.h>

struct syscalltbl {
       const char *const *num_to_name;
       const uint16_t *sorted_names;
       uint16_t e_machine;
       uint16_t num_to_name_len;
       uint16_t sorted_names_len;
};

#if defined(ALL_SYSCALLTBL) || defined(__alpha__)
EOF
build_tables "$tools_dir/perf/arch/alpha/entry/syscalls/syscall.tbl" "$outfile" common,64 EM_ALPHA
cat >> "$outfile" <<EOF
#endif // defined(ALL_SYSCALLTBL) || defined(__alpha__)

#if defined(ALL_SYSCALLTBL) || defined(__arm__) || defined(__aarch64__)
EOF
build_tables "$tools_dir/perf/arch/arm/entry/syscalls/syscall.tbl" "$outfile" common,32,oabi EM_ARM
build_tables "$tools_dir/perf/arch/arm64/entry/syscalls/syscall_64.tbl" "$outfile" common,64,renameat,rlimit,memfd_secret EM_AARCH64
cat >> "$outfile" <<EOF
#endif // defined(ALL_SYSCALLTBL) || defined(__arm__) || defined(__aarch64__)

#if defined(ALL_SYSCALLTBL) || defined(__csky__)
EOF
build_tables "$tools_dir/scripts/syscall.tbl" "$outfile" common,32,csky,time32,stat64,rlimit EM_CSKY
cat >> "$outfile" <<EOF
#endif // defined(ALL_SYSCALLTBL) || defined(__csky__)

#if defined(ALL_SYSCALLTBL) || defined(__mips__)
EOF
build_tables "$tools_dir/perf/arch/mips/entry/syscalls/syscall_n64.tbl" "$outfile" common,64,n64 EM_MIPS
cat >> "$outfile" <<EOF
#endif // defined(ALL_SYSCALLTBL) || defined(__mips__)

#if defined(ALL_SYSCALLTBL) || defined(__hppa__)
#if __BITS_PER_LONG != 64
EOF
build_tables "$tools_dir/perf/arch/parisc/entry/syscalls/syscall.tbl" "$outfile" common,32 EM_PARISC
echo "#else" >> "$outfile"
build_tables "$tools_dir/perf/arch/parisc/entry/syscalls/syscall.tbl" "$outfile" common,64 EM_PARISC
cat >> "$outfile" <<EOF
#endif //__BITS_PER_LONG != 64
#endif // defined(ALL_SYSCALLTBL) || defined(__hppa__)

#if defined(ALL_SYSCALLTBL) || defined(__powerpc__) || defined(__powerpc64__)
EOF
build_tables "$tools_dir/perf/arch/powerpc/entry/syscalls/syscall.tbl" "$outfile" common,32,nospu EM_PPC
build_tables "$tools_dir/perf/arch/powerpc/entry/syscalls/syscall.tbl" "$outfile" common,64,nospu EM_PPC64
cat >> "$outfile" <<EOF
#endif // defined(ALL_SYSCALLTBL) || defined(__powerpc__) || defined(__powerpc64__)

#if defined(ALL_SYSCALLTBL) || defined(__riscv)
#if __BITS_PER_LONG != 64
EOF
build_tables "$tools_dir/scripts/syscall.tbl" "$outfile" common,32,riscv,memfd_secret EM_RISCV
echo "#else" >> "$outfile"
build_tables "$tools_dir/scripts/syscall.tbl" "$outfile" common,64,riscv,rlimit,memfd_secret EM_RISCV
cat >> "$outfile" <<EOF
#endif //__BITS_PER_LONG != 64
#endif // defined(ALL_SYSCALLTBL) || defined(__riscv)
#if defined(ALL_SYSCALLTBL) || defined(__s390x__)
EOF
build_tables "$tools_dir/perf/arch/s390/entry/syscalls/syscall.tbl" "$outfile" common,64,renameat,rlimit,memfd_secret EM_S390
cat >> "$outfile" <<EOF
#endif // defined(ALL_SYSCALLTBL) || defined(__s390x__)

#if defined(ALL_SYSCALLTBL) || defined(__sh__)
EOF
build_tables "$tools_dir/perf/arch/sh/entry/syscalls/syscall.tbl" "$outfile" common,32 EM_SH
cat >> "$outfile" <<EOF
#endif // defined(ALL_SYSCALLTBL) || defined(__sh__)

#if defined(ALL_SYSCALLTBL) || defined(__sparc64__) || defined(__sparc__)
#if __BITS_PER_LONG != 64
EOF
build_tables "$tools_dir/perf/arch/sparc/entry/syscalls/syscall.tbl" "$outfile" common,32 EM_SPARC
echo "#else" >> "$outfile"
build_tables "$tools_dir/perf/arch/sparc/entry/syscalls/syscall.tbl" "$outfile" common,64 EM_SPARC
cat >> "$outfile" <<EOF
#endif //__BITS_PER_LONG != 64
#endif // defined(ALL_SYSCALLTBL) || defined(__sparc64__) || defined(__sparc__)

#if defined(ALL_SYSCALLTBL) || defined(__i386__) || defined(__x86_64__)
EOF
build_tables "$tools_dir/perf/arch/x86/entry/syscalls/syscall_32.tbl" "$outfile" common,32,i386 EM_386
build_tables "$tools_dir/perf/arch/x86/entry/syscalls/syscall_64.tbl" "$outfile" common,64 EM_X86_64
cat >> "$outfile" <<EOF
#endif // defined(ALL_SYSCALLTBL) || defined(__i386__) || defined(__x86_64__)

#if defined(ALL_SYSCALLTBL) || defined(__xtensa__)
EOF
build_tables "$tools_dir/perf/arch/xtensa/entry/syscalls/syscall.tbl" "$outfile" common,32 EM_XTENSA
cat >> "$outfile" <<EOF
#endif // defined(ALL_SYSCALLTBL) || defined(__xtensa__)

#if __BITS_PER_LONG != 64
EOF
build_tables "$tools_dir/scripts/syscall.tbl" "$outfile" common,32 EM_NONE
echo "#else" >> "$outfile"
build_tables "$tools_dir/scripts/syscall.tbl" "$outfile" common,64 EM_NONE
echo "#endif //__BITS_PER_LONG != 64" >> "$outfile"

build_outer_table() {
       e_machine=$1
       outfile="$2"
       cat >> "$outfile" <<EOF
       {
	      .num_to_name = syscall_num_to_name_$e_machine,
	      .sorted_names = syscall_sorted_names_$e_machine,
	      .e_machine = $e_machine,
	      .num_to_name_len = ARRAY_SIZE(syscall_num_to_name_$e_machine),
	      .sorted_names_len = ARRAY_SIZE(syscall_sorted_names_$e_machine),
       },
EOF
}

cat >> "$outfile" <<EOF
static const struct syscalltbl syscalltbls[] = {
#if defined(ALL_SYSCALLTBL) || defined(__alpha__)
EOF
build_outer_table EM_ALPHA "$outfile"
cat >> "$outfile" <<EOF
#endif // defined(ALL_SYSCALLTBL) || defined(__alpha__)

#if defined(ALL_SYSCALLTBL) || defined(__arm__) || defined(__aarch64__)
EOF
build_outer_table EM_ARM "$outfile"
build_outer_table EM_AARCH64 "$outfile"
cat >> "$outfile" <<EOF
#endif // defined(ALL_SYSCALLTBL) || defined(__arm__) || defined(__aarch64__)

#if defined(ALL_SYSCALLTBL) || defined(__csky__)
EOF
build_outer_table EM_CSKY "$outfile"
cat >> "$outfile" <<EOF
#endif // defined(ALL_SYSCALLTBL) || defined(__csky__)

#if defined(ALL_SYSCALLTBL) || defined(__mips__)
EOF
build_outer_table EM_MIPS "$outfile"
cat >> "$outfile" <<EOF
#endif // defined(ALL_SYSCALLTBL) || defined(__mips__)

#if defined(ALL_SYSCALLTBL) || defined(__hppa__)
EOF
build_outer_table EM_PARISC "$outfile"
cat >> "$outfile" <<EOF
#endif // defined(ALL_SYSCALLTBL) || defined(__hppa__)

#if defined(ALL_SYSCALLTBL) || defined(__powerpc__) || defined(__powerpc64__)
EOF
build_outer_table EM_PPC "$outfile"
build_outer_table EM_PPC64 "$outfile"
cat >> "$outfile" <<EOF
#endif // defined(ALL_SYSCALLTBL) || defined(__powerpc__) || defined(__powerpc64__)

#if defined(ALL_SYSCALLTBL) || defined(__riscv)
EOF
build_outer_table EM_RISCV "$outfile"
cat >> "$outfile" <<EOF
#endif // defined(ALL_SYSCALLTBL) || defined(__riscv)

#if defined(ALL_SYSCALLTBL) || defined(__s390x__)
EOF
build_outer_table EM_S390 "$outfile"
cat >> "$outfile" <<EOF
#endif // defined(ALL_SYSCALLTBL) || defined(__s390x__)

#if defined(ALL_SYSCALLTBL) || defined(__sh__)
EOF
build_outer_table EM_SH "$outfile"
cat >> "$outfile" <<EOF
#endif // defined(ALL_SYSCALLTBL) || defined(__sh__)

#if defined(ALL_SYSCALLTBL) || defined(__sparc64__) || defined(__sparc__)
EOF
build_outer_table EM_SPARC "$outfile"
cat >> "$outfile" <<EOF
#endif // defined(ALL_SYSCALLTBL) || defined(__sparc64__) || defined(__sparc__)

#if defined(ALL_SYSCALLTBL) || defined(__i386__) || defined(__x86_64__)
EOF
build_outer_table EM_386 "$outfile"
build_outer_table EM_X86_64 "$outfile"
cat >> "$outfile" <<EOF
#endif // defined(ALL_SYSCALLTBL) || defined(__i386__) || defined(__x86_64__)

#if defined(ALL_SYSCALLTBL) || defined(__xtensa__)
EOF
build_outer_table EM_XTENSA "$outfile"
cat >> "$outfile" <<EOF
#endif // defined(ALL_SYSCALLTBL) || defined(__xtensa__)
EOF
build_outer_table EM_NONE "$outfile"
cat >> "$outfile" <<EOF
};
EOF
