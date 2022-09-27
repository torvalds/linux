#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only

# Create an autoksyms.h header file from the list of all module's needed symbols
# as recorded in *.usyms files and the user-provided symbol whitelist.

set -e

# Use "make V=1" to debug this script.
case "$KBUILD_VERBOSE" in
*1*)
	set -x
	;;
esac

read_modorder=

if [ "$1" = --modorder ]; then
	shift
	read_modorder=1
fi

output_file="$1"

needed_symbols=

# Special case for modversions (see modpost.c)
if grep -q "^CONFIG_MODVERSIONS=y$" include/config/auto.conf; then
	needed_symbols="$needed_symbols module_layout"
fi

ksym_wl=$(sed -n 's/^CONFIG_UNUSED_KSYMS_WHITELIST=\(.*\)$/\1/p' include/config/auto.conf)
if [ -n "$ksym_wl" ]; then
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

{
	[ -n "${read_modorder}" ] && sed 's/ko$/usyms/' modules.order | xargs cat
	echo "$needed_symbols"
	[ -n "$ksym_wl" ] && cat "$ksym_wl"
} | sed -e 's/ /\n/g' | sed -n -e '/^$/!p' |
# Remove the dot prefix for ppc64; symbol names with a dot (.) hold entry
# point addresses.
sed -e 's/^\.//' |
sort -u |
# Ignore __this_module. It's not an exported symbol, and will be resolved
# when the final .ko's are linked.
grep -v '^__this_module$' |
sed -e 's/\(.*\)/#define __KSYM_\1 1/' >> "$output_file"
