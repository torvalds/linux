#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
#
# Print the minimum supported version of the given tool.
# When you raise the minimum version, please update
# Documentation/process/changes.rst as well.

set -e

if [ $# != 1 ]; then
	echo "Usage: $0 toolname" >&2
	exit 1
fi

case "$1" in
binutils)
	echo 2.23.0
	;;
gcc)
	# https://gcc.gnu.org/bugzilla/show_bug.cgi?id=63293
	# https://lore.kernel.org/r/20210107111841.GN1551@shell.armlinux.org.uk
	if [ "$SRCARCH" = arm64 ]; then
		echo 5.1.0
	else
		echo 4.9.0
	fi
	;;
icc)
	# temporary
	echo 16.0.3
	;;
llvm)
	# https://lore.kernel.org/r/YMtib5hKVyNknZt3@osiris/
	if [ "$SRCARCH" = s390 ]; then
		echo 13.0.0
	else
		echo 10.0.1
	fi
	;;
*)
	echo "$1: unknown tool" >&2
	exit 1
	;;
esac
