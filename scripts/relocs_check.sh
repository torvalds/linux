#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later

# Get a list of all the relocations, remove from it the relocations
# that are known to be legitimate and return this list to arch specific
# script that will look for suspicious relocations.

objdump="$1"
nm="$2"
vmlinux="$3"

# Remove from the possible bad relocations those that match an undefined
# weak symbol which will result in an absolute relocation to 0.
# Weak unresolved symbols are of that form in nm output:
# "                  w _binary__btf_vmlinux_bin_end"
undef_weak_symbols=$($nm "$vmlinux" | awk '$1 ~ /w/ { print $2 }')

$objdump -R "$vmlinux" |
	grep -E '\<R_' |
	([ "$undef_weak_symbols" ] && grep -F -w -v "$undef_weak_symbols" || cat)
