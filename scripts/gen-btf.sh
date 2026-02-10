#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Copyright (c) 2025 Meta Platforms, Inc. and affiliates.
#
# This script generates BTF data for the provided ELF file.
#
# Kernel BTF generation involves these conceptual steps:
#   1. pahole generates BTF from DWARF data
#   2. resolve_btfids applies kernel-specific btf2btf
#      transformations and computes data for .BTF_ids section
#   3. the result gets linked/objcopied into the target binary
#
# How step (3) should be done differs between vmlinux, and
# kernel modules, which is the primary reason for the existence
# of this script.
#
# For modules the script expects vmlinux passed in as --btf_base.
# Generated .BTF, .BTF.base and .BTF_ids sections become embedded
# into the input ELF file with objcopy.
#
# For vmlinux the input file remains unchanged and two files are produced:
#   - ${1}.btf.o ready for linking into vmlinux
#   - ${1}.BTF_ids with .BTF_ids data blob
# This output is consumed by scripts/link-vmlinux.sh

set -e

usage()
{
	echo "Usage: $0 [--btf_base <file>] <target ELF file>"
	exit 1
}

BTF_BASE=""

while [ $# -gt 0 ]; do
	case "$1" in
	--btf_base)
		BTF_BASE="$2"
		shift 2
		;;
	-*)
		echo "Unknown option: $1" >&2
		usage
		;;
	*)
		break
		;;
	esac
done

if [ $# -ne 1 ]; then
	usage
fi

ELF_FILE="$1"
shift

is_enabled() {
	grep -q "^$1=y" ${objtree}/include/config/auto.conf
}

case "${KBUILD_VERBOSE}" in
*1*)
	set -x
	;;
esac

gen_btf_data()
{
	btf1="${ELF_FILE}.BTF.1"
	${PAHOLE} -J ${PAHOLE_FLAGS}			\
		${BTF_BASE:+--btf_base ${BTF_BASE}}	\
		--btf_encode_detached=${btf1}		\
		"${ELF_FILE}"

	${RESOLVE_BTFIDS} ${RESOLVE_BTFIDS_FLAGS}	\
		${BTF_BASE:+--btf_base ${BTF_BASE}}	\
		--btf ${btf1} "${ELF_FILE}"
}

gen_btf_o()
{
	btf_data=${ELF_FILE}.btf.o

	# Create ${btf_data} which contains just .BTF section but no symbols. Add
	# SHF_ALLOC because .BTF will be part of the vmlinux image. --strip-all
	# deletes all symbols including __start_BTF and __stop_BTF, which will
	# be redefined in the linker script.
	echo "" | ${CC} ${CLANG_FLAGS} ${KBUILD_CPPFLAGS} ${KBUILD_CFLAGS} -fno-lto -c -x c -o ${btf_data} -
	${OBJCOPY} --add-section .BTF=${ELF_FILE}.BTF \
		--set-section-flags .BTF=alloc,readonly ${btf_data}
	${OBJCOPY} --only-section=.BTF --strip-all ${btf_data}

	# Change e_type to ET_REL so that it can be used to link final vmlinux.
	# GNU ld 2.35+ and lld do not allow an ET_EXEC input.
	if is_enabled CONFIG_CPU_BIG_ENDIAN; then
		et_rel='\0\1'
	else
		et_rel='\1\0'
	fi
	printf "${et_rel}" | dd of="${btf_data}" conv=notrunc bs=1 seek=16 status=none
}

embed_btf_data()
{
	${OBJCOPY} --add-section .BTF=${ELF_FILE}.BTF ${ELF_FILE}

	# a module might not have a .BTF_ids or .BTF.base section
	btf_base="${ELF_FILE}.BTF.base"
	if [ -f "${btf_base}" ]; then
		${OBJCOPY} --add-section .BTF.base=${btf_base} ${ELF_FILE}
	fi
	btf_ids="${ELF_FILE}.BTF_ids"
	if [ -f "${btf_ids}" ]; then
		${RESOLVE_BTFIDS} --patch_btfids ${btf_ids} ${ELF_FILE}
	fi
}

cleanup()
{
	rm -f "${ELF_FILE}.BTF.1"
	rm -f "${ELF_FILE}.BTF"
	if [ "${BTFGEN_MODE}" = "module" ]; then
		rm -f "${ELF_FILE}.BTF.base"
		rm -f "${ELF_FILE}.BTF_ids"
	fi
}
trap cleanup EXIT

BTFGEN_MODE="vmlinux"
if [ -n "${BTF_BASE}" ]; then
	BTFGEN_MODE="module"
fi

gen_btf_data

case "${BTFGEN_MODE}" in
vmlinux)
	gen_btf_o
	;;
module)
	embed_btf_data
	;;
esac

exit 0
