#!/bin/sh

if [ $# -eq 1 ]  ; then
	OUTPUT=$1
fi

GVF=${OUTPUT}PERF-VERSION-FILE

LF='
'

#
# First check if there is a .git to get the version from git describe
# otherwise try to get the version from the kernel Makefile
#
if test -d ../../.git -o -f ../../.git &&
	VN=$(git tag 2>/dev/null | tail -1 | grep -E "v[0-9].[0-9]*")
then
	VN=$(echo $VN"-g"$(git log -1 --abbrev=4 --pretty=format:"%h" HEAD))
	VN=$(echo "$VN" | sed -e 's/-/./g');
else
	VN=$(MAKEFLAGS= make -sC ../.. kernelversion)
fi

VN=$(expr "$VN" : v*'\(.*\)')

if test -r $GVF
then
	VC=$(sed -e 's/^#define PERF_VERSION "\(.*\)"/\1/' <$GVF)
else
	VC=unset
fi
test "$VN" = "$VC" || {
	echo >&2 "PERF_VERSION = $VN"
	echo "#define PERF_VERSION \"$VN\"" >$GVF
}


