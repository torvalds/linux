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
	arch="$1"

	header="$toolsdir/arch/$arch/include/uapi/asm/errno.h"
	if test -r "$header"; then
		echo "$header"
	else
		echo "$toolsdir/include/uapi/asm-generic/errno.h"
	fi
}

create_errno_lookup_func()
{
	arch=$(arch_string "$1")

	printf "static const char *errno_to_name__%s(int err)\n{\n\tswitch (err) {\n" $arch

	while read name nr; do
		printf '\tcase %d: return "%s";\n' $nr $name
	done

	printf '\tdefault: return "(unknown)";\n\t}\n}\n'
}

process_arch()
{
	arch="$1"
	asm_errno=$(asm_errno_file "$arch")

	$gcc $CFLAGS $include_path -E -dM -x c $asm_errno \
		|grep -hE '^#define[[:blank:]]+(E[^[:blank:]]+)[[:blank:]]+([[:digit:]]+).*' \
		|awk '{ print $2","$3; }' \
		|sort -t, -k2 -nu \
		|IFS=, create_errno_lookup_func "$arch"
}

create_arch_errno_table_func()
{
	archlist="$1"
	default="$2"

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

# Create list of architectures that have a specific errno.h.
archlist=""
for arch in $(find $toolsdir/arch -maxdepth 1 -mindepth 1 -type d -printf "%f\n" | sort -r); do
	test -f $toolsdir/arch/$arch/include/uapi/asm/errno.h && archlist="$archlist $arch"
done

for arch in generic $archlist; do
	process_arch "$arch"
done
create_arch_errno_table_func "$archlist" "generic"
