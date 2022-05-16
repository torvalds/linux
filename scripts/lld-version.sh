#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
#
# Usage: $ ./scripts/lld-version.sh ld.lld
#
# Print the linker version of `ld.lld' in a 5 or 6-digit form
# such as `100001' for ld.lld 10.0.1 etc.

linker_string="$($* --version)"

if ! ( echo $linker_string | grep -q LLD ); then
	echo 0
	exit 1
fi

VERSION=$(echo $linker_string | cut -d ' ' -f 2)
MAJOR=$(echo $VERSION | cut -d . -f 1)
MINOR=$(echo $VERSION | cut -d . -f 2)
PATCHLEVEL=$(echo $VERSION | cut -d . -f 3)
printf "%d%02d%02d\\n" $MAJOR $MINOR $PATCHLEVEL
