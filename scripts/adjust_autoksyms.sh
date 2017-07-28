#!/bin/sh

# Script to create/update include/generated/autoksyms.h and dependency files
#
# Copyright:	(C) 2016  Linaro Limited
# Created by:	Nicolas Pitre, January 2016
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License version 2 as
# published by the Free Software Foundation.

# Create/update the include/generated/autoksyms.h file from the list
# of all module's needed symbols as recorded on the third line of
# .tmp_versions/*.mod files.
#
# For each symbol being added or removed, the corresponding dependency
# file's timestamp is updated to force a rebuild of the affected source
# file. All arguments passed to this script are assumed to be a command
# to be exec'd to trigger a rebuild of those files.

set -e

cur_ksyms_file="include/generated/autoksyms.h"
new_ksyms_file="include/generated/autoksyms.h.tmpnew"

info() {
	if [ "$quiet" != "silent_" ]; then
		printf "  %-7s %s\n" "$1" "$2"
	fi
}

info "CHK" "$cur_ksyms_file"

# Use "make V=1" to debug this script.
case "$KBUILD_VERBOSE" in
*1*)
	set -x
	;;
esac

# We need access to CONFIG_ symbols
case "${KCONFIG_CONFIG}" in
*/*)
	. "${KCONFIG_CONFIG}"
	;;
*)
	# Force using a file from the current directory
	. "./${KCONFIG_CONFIG}"
esac

# In case it doesn't exist yet...
if [ -e "$cur_ksyms_file" ]; then touch "$cur_ksyms_file"; fi

# Generate a new ksym list file with symbols needed by the current
# set of modules.
cat > "$new_ksyms_file" << EOT
/*
 * Automatically generated file; DO NOT EDIT.
 */

EOT
[ "$(ls -A "$MODVERDIR")" ] &&
sed -ns -e '3{s/ /\n/g;/^$/!p;}' "$MODVERDIR"/*.mod | sort -u |
while read sym; do
	if [ -n "$CONFIG_HAVE_UNDERSCORE_SYMBOL_PREFIX" ]; then
		sym="${sym#_}"
	fi
	echo "#define __KSYM_${sym} 1"
done >> "$new_ksyms_file"

# Special case for modversions (see modpost.c)
if [ -n "$CONFIG_MODVERSIONS" ]; then
	echo "#define __KSYM_module_layout 1" >> "$new_ksyms_file"
fi

# Extract changes between old and new list and touch corresponding
# dependency files.
changed=$(
count=0
sort "$cur_ksyms_file" "$new_ksyms_file" | uniq -u |
sed -n 's/^#define __KSYM_\(.*\) 1/\1/p' | tr "A-Z_" "a-z/" |
while read sympath; do
	if [ -z "$sympath" ]; then continue; fi
	depfile="include/config/ksym/${sympath}.h"
	mkdir -p "$(dirname "$depfile")"
	touch "$depfile"
	echo $((count += 1))
done | tail -1 )
changed=${changed:-0}

if [ $changed -gt 0 ]; then
	# Replace the old list with tne new one
	old=$(grep -c "^#define __KSYM_" "$cur_ksyms_file" || true)
	new=$(grep -c "^#define __KSYM_" "$new_ksyms_file" || true)
	info "KSYMS" "symbols: before=$old, after=$new, changed=$changed"
	info "UPD" "$cur_ksyms_file"
	mv -f "$new_ksyms_file" "$cur_ksyms_file"
	# Then trigger a rebuild of affected source files
	exec $@
else
	rm -f "$new_ksyms_file"
fi
