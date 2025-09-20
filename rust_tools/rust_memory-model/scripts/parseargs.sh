#!/bin/sh
# SPDX-License-Identifier: GPL-2.0+
#
# Parse arguments common to the various scripts.
#
# . scripts/parseargs.sh
#
# Include into other Linux kernel tools/memory-model scripts.
#
# Copyright IBM Corporation, 2018
#
# Author: Paul E. McKenney <paulmck@linux.ibm.com>

T=/tmp/parseargs.sh.$$
mkdir $T

# Initialize one parameter: initparam name default
initparam () {
	echo if test -z '"$'$1'"' > $T/s
	echo then >> $T/s
	echo	$1='"'$2'"' >> $T/s
	echo	export $1 >> $T/s
	echo fi >> $T/s
	echo $1_DEF='$'$1  >> $T/s
	. $T/s
}

initparam LKMM_DESTDIR "."
initparam LKMM_HERD_OPTIONS "-conf linux-kernel.cfg"
initparam LKMM_HW_MAP_FILE ""
initparam LKMM_JOBS `getconf _NPROCESSORS_ONLN`
initparam LKMM_PROCS "3"
initparam LKMM_TIMEOUT "1m"

scriptname=$0

usagehelp () {
	echo "Usage $scriptname [ arguments ]"
	echo "      --destdir path (place for .litmus.out, default by .litmus)"
	echo "      --herdopts -conf linux-kernel.cfg ..."
	echo "      --hw AArch64"
	echo "      --jobs N (number of jobs, default one per CPU)"
	echo "      --procs N (litmus tests with at most this many processes)"
	echo "      --timeout N (herd7 timeout (e.g., 10s, 1m, 2hr, 1d, '')"
	echo "Defaults: --destdir '$LKMM_DESTDIR_DEF' --herdopts '$LKMM_HERD_OPTIONS_DEF' --hw '$LKMM_HW_MAP_FILE' --jobs '$LKMM_JOBS_DEF' --procs '$LKMM_PROCS_DEF' --timeout '$LKMM_TIMEOUT_DEF'"
	exit 1
}

usage () {
	usagehelp 1>&2
}

# checkarg --argname argtype $# arg mustmatch cannotmatch
checkarg () {
	if test $3 -le 1
	then
		echo $1 needs argument $2 matching \"$5\"
		usage
	fi
	if echo "$4" | grep -q -e "$5"
	then
		:
	else
		echo $1 $2 \"$4\" must match \"$5\"
		usage
	fi
	if echo "$4" | grep -q -e "$6"
	then
		echo $1 $2 \"$4\" must not match \"$6\"
		usage
	fi
}

while test $# -gt 0
do
	case "$1" in
	--destdir)
		checkarg --destdir "(path to directory)" "$#" "$2" '.\+' '^--'
		LKMM_DESTDIR="$2"
		mkdir $LKMM_DESTDIR > /dev/null 2>&1
		if ! test -e "$LKMM_DESTDIR"
		then
			echo "Cannot create directory --destdir '$LKMM_DESTDIR'"
			usage
		fi
		if test -d "$LKMM_DESTDIR" -a -x "$LKMM_DESTDIR"
		then
			:
		else
			echo "Directory --destdir '$LKMM_DESTDIR' insufficient permissions to create files"
			usage
		fi
		shift
		;;
	--herdopts|--herdopt)
		checkarg --destdir "(herd7 options)" "$#" "$2" '.*' '^--'
		LKMM_HERD_OPTIONS="$2"
		shift
		;;
	--hw)
		checkarg --hw "(.map file architecture name)" "$#" "$2" '^[A-Za-z0-9_-]\+' '^--'
		LKMM_HW_MAP_FILE="$2"
		shift
		;;
	-j[1-9]*)
		njobs="`echo $1 | sed -e 's/^-j//'`"
		trailchars="`echo $njobs | sed -e 's/[0-9]\+\(.*\)$/\1/'`"
		if test -n "$trailchars"
		then
			echo $1 trailing characters "'$trailchars'"
			usagehelp
		fi
		LKMM_JOBS="`echo $njobs | sed -e 's/^\([0-9]\+\).*$/\1/'`"
		;;
	--jobs|--job|-j)
		checkarg --jobs "(number)" "$#" "$2" '^[1-9][0-9]*$' '^--'
		LKMM_JOBS="$2"
		shift
		;;
	--procs|--proc)
		checkarg --procs "(number)" "$#" "$2" '^[0-9]\+$' '^--'
		LKMM_PROCS="$2"
		shift
		;;
	--timeout)
		checkarg --timeout "(timeout spec)" "$#" "$2" '^\([0-9]\+[smhd]\?\|\)$' '^--'
		LKMM_TIMEOUT="$2"
		shift
		;;
	--)
		shift
		break
		;;
	*)
		echo Unknown argument $1
		usage
		;;
	esac
	shift
done
if test -z "$LKMM_TIMEOUT"
then
	LKMM_TIMEOUT_CMD=""; export LKMM_TIMEOUT_CMD
else
	LKMM_TIMEOUT_CMD="timeout $LKMM_TIMEOUT"; export LKMM_TIMEOUT_CMD
fi
rm -rf $T
