#!/bin/sh
#
# Script which prints out the version to use for building cpupowerutils.
# Must be called from tools/power/cpupower/
# 
# Heavily based on tools/perf/util/PERF-VERSION-GEN .

LF='
'

# First check if there is a .git to get the version from git describe
# otherwise try to get the version from the kernel makefile
if test -d ../../../.git -o -f ../../../.git &&
	VN=$(git describe --abbrev=4 HEAD 2>/dev/null) &&
	case "$VN" in
	*$LF*) (exit 1) ;;
	v[0-9]*)
		git update-index -q --refresh
		test -z "$(git diff-index --name-only HEAD --)" ||
		VN="$VN-dirty" ;;
	esac
then
	VN=$(echo "$VN" | sed -e 's/-/./g');
else
	eval $(grep '^VERSION[[:space:]]*=' ../../../Makefile|tr -d ' ')
	eval $(grep '^PATCHLEVEL[[:space:]]*=' ../../../Makefile|tr -d ' ')
	eval $(grep '^SUBLEVEL[[:space:]]*=' ../../../Makefile|tr -d ' ')
	eval $(grep '^EXTRAVERSION[[:space:]]*=' ../../../Makefile|tr -d ' ')

	VN="${VERSION}.${PATCHLEVEL}.${SUBLEVEL}${EXTRAVERSION}"
fi

VN=$(expr "$VN" : v*'\(.*\)')

echo $VN
