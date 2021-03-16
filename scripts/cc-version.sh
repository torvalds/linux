#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
#
# Print the compiler name and its version in a 5 or 6-digit form.
# Also, perform the minimum version check.

set -e

# When you raise the minimum compiler version, please update
# Documentation/process/changes.rst as well.
gcc_min_version=4.9.0
clang_min_version=10.0.1
icc_min_version=16.0.3 # temporary

# https://gcc.gnu.org/bugzilla/show_bug.cgi?id=63293
# https://lore.kernel.org/r/20210107111841.GN1551@shell.armlinux.org.uk
if [ "$SRCARCH" = arm64 ]; then
	gcc_min_version=5.1.0
fi

# Print the compiler name and some version components.
get_compiler_info()
{
	cat <<- EOF | "$@" -E -P -x c - 2>/dev/null
	#if defined(__clang__)
	Clang	__clang_major__  __clang_minor__  __clang_patchlevel__
	#elif defined(__INTEL_COMPILER)
	ICC	__INTEL_COMPILER  __INTEL_COMPILER_UPDATE
	#elif defined(__GNUC__)
	GCC	__GNUC__  __GNUC_MINOR__  __GNUC_PATCHLEVEL__
	#else
	unknown
	#endif
	EOF
}

# Convert the version string x.y.z to a canonical 5 or 6-digit form.
get_canonical_version()
{
	IFS=.
	set -- $1
	echo $((10000 * $1 + 100 * $2 + $3))
}

# $@ instead of $1 because multiple words might be given, e.g. CC="ccache gcc".
orig_args="$@"
set -- $(get_compiler_info "$@")

name=$1

case "$name" in
GCC)
	version=$2.$3.$4
	min_version=$gcc_min_version
	;;
Clang)
	version=$2.$3.$4
	min_version=$clang_min_version
	;;
ICC)
	version=$(($2 / 100)).$(($2 % 100)).$3
	min_version=$icc_min_version
	;;
*)
	echo "$orig_args: unknown compiler" >&2
	exit 1
	;;
esac

cversion=$(get_canonical_version $version)
min_cversion=$(get_canonical_version $min_version)

if [ "$cversion" -lt "$min_cversion" ]; then
	echo >&2 "***"
	echo >&2 "*** Compiler is too old."
	echo >&2 "***   Your $name version:    $version"
	echo >&2 "***   Minimum $name version: $min_version"
	echo >&2 "***"
	exit 1
fi

echo $name $cversion
