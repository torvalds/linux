#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only

# Script to update include/generated/autoksyms.h and dependency files
#
# Copyright:	(C) 2016  Linaro Limited
# Created by:	Nicolas Pitre, January 2016
#

# Update the include/generated/autoksyms.h file.
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

# Generate a new symbol list file
$CONFIG_SHELL $srctree/scripts/gen_autoksyms.sh --modorder "$new_ksyms_file"

# Extract changes between old and new list and touch corresponding
# dependency files.
changed=$(
count=0
sort "$cur_ksyms_file" "$new_ksyms_file" | uniq -u |
sed -n 's/^#define __KSYM_\(.*\) 1/\1/p' |
while read sympath; do
	if [ -z "$sympath" ]; then continue; fi
	depfile="include/ksym/${sympath}"
	mkdir -p "$(dirname "$depfile")"
	touch "$depfile"
	# Filesystems with coarse time precision may create timestamps
	# equal to the one from a file that was very recently built and that
	# needs to be rebuild. Let's guard against that by making sure our
	# dep files are always newer than the first file we created here.
	while [ ! "$depfile" -nt "$new_ksyms_file" ]; do
		touch "$depfile"
	done
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
