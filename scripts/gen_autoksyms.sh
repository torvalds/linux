#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only

# Create an autoksyms.h header file from the list of all module's needed symbols
# as recorded on the third line of *.mod files and the user-provided symbol
# whitelist.

set -e

output_file="$1"

# Use "make V=1" to debug this script.
case "$KBUILD_VERBOSE" in
*1*)
	set -x
	;;
esac

# We need access to CONFIG_ symbols
. include/config/auto.conf

ksym_wl=/dev/null
if [ -n "$CONFIG_UNUSED_KSYMS_WHITELIST" ]; then
	# Use 'eval' to expand the whitelist path and check if it is relative
	eval ksym_wl="$CONFIG_UNUSED_KSYMS_WHITELIST"
	[ "${ksym_wl}" != "${ksym_wl#/}" ] || ksym_wl="$abs_srctree/$ksym_wl"
	if [ ! -f "$ksym_wl" ] || [ ! -r "$ksym_wl" ]; then
		echo "ERROR: '$ksym_wl' whitelist file not found" >&2
		exit 1
	fi
fi

# Generate a new ksym list file with symbols needed by the current
# set of modules.
cat > "$output_file" << EOT
/*
 * Automatically generated file; DO NOT EDIT.
 */

EOT

for mod in "$MODVERDIR"/*.mod; do
	[ -f "$mod" ] && sed -n -e '3{s/ /\n/g;/^$/!p;}' "$mod"
done | cat - "$ksym_wl" | sort -u |
while read sym; do
	echo "#define __KSYM_${sym} 1"
done >> "$output_file"

# Special case for modversions (see modpost.c)
if [ -n "$CONFIG_MODVERSIONS" ]; then
	echo "#define __KSYM_module_layout 1" >> "$output_file"
fi
