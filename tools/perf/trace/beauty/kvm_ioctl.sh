#!/bin/sh
# SPDX-License-Identifier: LGPL-2.1

[ $# -eq 1 ] && header_dir=$1 || header_dir=tools/include/uapi/linux/

printf "static const char *kvm_ioctl_cmds[] = {\n"
regex='^#[[:space:]]*define[[:space:]]+KVM_(\w+)[[:space:]]+_IO[RW]*\([[:space:]]*KVMIO[[:space:]]*,[[:space:]]*(0x[[:xdigit:]]+).*'
grep -E $regex ${header_dir}/kvm.h	| \
	sed -r "s/$regex/\2 \1/g"	| \
	grep -E -v " ((ARM|PPC|S390)_|[GS]ET_(DEBUGREGS|PIT2|XSAVE|TSC_KHZ)|CREATE_SPAPR_TCE_64)" | \
	sort | xargs printf "\t[%s] = \"%s\",\n"
printf "};\n"
