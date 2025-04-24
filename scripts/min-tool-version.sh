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
	echo 2.25.0
	;;
gcc)
	if [ "$ARCH" = parisc64 ]; then
		echo 12.0.0
	elif [ "$SRCARCH" = x86 ]; then
		echo 8.1.0
	else
		echo 5.1.0
	fi
	;;
llvm)
	if [ "$SRCARCH" = s390 -o "$SRCARCH" = x86 ]; then
		echo 15.0.0
	elif [ "$SRCARCH" = loongarch ]; then
		echo 18.0.0
	else
		echo 13.0.1
	fi
	;;
rustc)
	echo 1.78.0
	;;
bindgen)
	echo 0.65.1
	;;
*)
	echo "$1: unknown tool" >&2
	exit 1
	;;
esac
