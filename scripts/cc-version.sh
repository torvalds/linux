#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
#
# Print the C compiler name and its version in a 5 or 6-digit form.
# Also, perform the minimum version check.

set -e

# Print the C compiler name and some version components.
get_c_compiler_info()
{
	cat <<- EOF | "$@" -E -P -x c - 2>/dev/null
	#if defined(__clang__)
	Clang	__clang_major__  __clang_minor__  __clang_patchlevel__
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
set -- $(get_c_compiler_info "$@")

name=$1

min_tool_version=$(dirname $0)/min-tool-version.sh

case "$name" in
GCC)
	version=$2.$3.$4
	min_version=$($min_tool_version gcc)
	;;
Clang)
	version=$2.$3.$4
	min_version=$($min_tool_version llvm)
	;;
*)
	echo "$orig_args: unknown C compiler" >&2
	exit 1
	;;
esac

cversion=$(get_canonical_version $version)
min_cversion=$(get_canonical_version $min_version)

if [ "$cversion" -lt "$min_cversion" ]; then
	echo >&2 "***"
	echo >&2 "*** C compiler is too old."
	echo >&2 "***   Your $name version:    $version"
	echo >&2 "***   Minimum $name version: $min_version"
	echo >&2 "***"
	exit 1
fi

echo $name $cversion
