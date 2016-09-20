#!/bin/sh
srctree=$(dirname "$0")

SHOW_ERROR=
if [ "$1" = "--show-error" ] ; then
	SHOW_ERROR=1
	shift || true
fi

gccplugins_dir=$($3 -print-file-name=plugin)
plugincc=$($1 -E -x c++ - -o /dev/null -I"${srctree}"/gcc-plugins -I"${gccplugins_dir}"/include 2>&1 <<EOF
#include "gcc-common.h"
#if BUILDING_GCC_VERSION >= 4008 || defined(ENABLE_BUILD_WITH_CXX)
#warning $2 CXX
#else
#warning $1 CC
#endif
EOF
)

if [ $? -ne 0 ]
then
	if [ -n "$SHOW_ERROR" ] ; then
		echo "${plugincc}" >&2
	fi
	exit 1
fi

case "$plugincc" in
	*"$1 CC"*)
		echo "$1"
		exit 0
		;;

	*"$2 CXX"*)
		# the c++ compiler needs another test, see below
		;;

	*)
		exit 1
		;;
esac

# we need a c++ compiler that supports the designated initializer GNU extension
plugincc=$($2 -c -x c++ -std=gnu++98 - -fsyntax-only -I"${srctree}"/gcc-plugins -I"${gccplugins_dir}"/include 2>&1 <<EOF
#include "gcc-common.h"
class test {
public:
	int test;
} test = {
	.test = 1
};
EOF
)

if [ $? -eq 0 ]
then
	echo "$2"
	exit 0
fi

if [ -n "$SHOW_ERROR" ] ; then
	echo "${plugincc}" >&2
fi
exit 1
