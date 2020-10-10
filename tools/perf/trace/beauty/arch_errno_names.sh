#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
#
# Generate C file mapping errno codes to errno names.
#
# Copyright IBM Corp. 2018
# Author(s):  Hendrik Brueckner <brueckner@linux.vnet.ibm.com>

gcc="$1"
toolsdir="$2"
include_path="-I$toolsdir/include/uapi"

arch_string()
{
	echo "$1" |sed -e 'y/- /__/' |tr '[[:upper:]]' '[[:lower:]]'
}

asm_errno_file()
{
	local arch="$1"
	local header

	header="$toolsdir/arch/$arch/include/uapi/asm/errno.h"
	if test -r "$header"; then
		echo "$header"
	else
		echo "$toolsdir/include/uapi/asm-generic/errno.h"
	fi
}

create_errno_lookup_func()
{
	local arch=$(arch_string "$1")
	local nr name

	cat <<EoFuncBegin
static const char *errno_to_name__$arch(int err)
{
	switch (err) {
EoFuncBegin

	while read name nr; do
		printf '\tcase %d: return "%s";\n' $nr $name
	done

	cat <<EoFuncEnd
	default:
		return "(unknown)";
	}
}

EoFuncEnd
}

process_arch()
{
	local arch="$1"
	local asm_errno=$(asm_errno_file "$arch")

	$gcc $include_path -E -dM -x c $asm_errno \
		|grep -hE '^#define[[:blank:]]+(E[^[:blank:]]+)[[:blank:]]+([[:digit:]]+).*' \
		|awk '{ print $2","$3; }' \
		|sort -t, -k2 -nu \
		|IFS=, create_errno_lookup_func "$arch"
}

create_arch_errno_table_func()
{
	local archlist="$1"
	local default="$2"
	local arch

	printf 'const char *arch_syscalls__strerrno(const char *arch, int err)\n'
	printf '{\n'
	for arch in $archlist; do
		printf '\tif (!strcmp(arch, "%s"))\n' $(arch_string "$arch")
		printf '\t\treturn errno_to_name__%s(err);\n' $(arch_string "$arch")
	done
	printf '\treturn errno_to_name__%s(err);\n' $(arch_string "$default")
	printf '}\n'
}

cat <<EoHEADER
/* SPDX-License-Identifier: GPL-2.0 */

#include <string.h>

EoHEADER

# Create list of architectures and ignore those that do not appear
# in tools/perf/arch
archlist=""
for arch in $(find $toolsdir/arch -maxdepth 1 -mindepth 1 -type d -printf "%f\n" | grep -v x86 | sort); do
	test -d $toolsdir/perf/arch/$arch && archlist="$archlist $arch"
done

for arch in x86 $archlist generic; do
	process_arch "$arch"
done
create_arch_errno_table_func "x86 $archlist" "generic"
