#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only
# perf-with-kcore: use perf with a copy of kcore
# Copyright (c) 2014, Intel Corporation.
#

set -e

usage()
{
        echo "Usage: perf-with-kcore <perf sub-command> <perf.data directory> [<sub-command options> [ -- <workload>]]" >&2
        echo "       <perf sub-command> can be record, script, report or inject" >&2
        echo "   or: perf-with-kcore fix_buildid_cache_permissions" >&2
        exit 1
}

find_perf()
{
	if [ -n "$PERF" ] ; then
		return
	fi
	PERF=`which perf || true`
	if [ -z "$PERF" ] ; then
		echo "Failed to find perf" >&2
	        exit 1
	fi
	if [ ! -x "$PERF" ] ; then
		echo "Failed to find perf" >&2
	        exit 1
	fi
	echo "Using $PERF"
	"$PERF" version
}

copy_kcore()
{
	echo "Copying kcore"

	if [ $EUID -eq 0 ] ; then
		SUDO=""
	else
		SUDO="sudo"
	fi

	rm -f perf.data.junk
	("$PERF" record -o perf.data.junk "${PERF_OPTIONS[@]}" -- sleep 60) >/dev/null 2>/dev/null &
	PERF_PID=$!

	# Need to make sure that perf has started
	sleep 1

	KCORE=$(($SUDO "$PERF" buildid-cache -v -f -k /proc/kcore >/dev/null) 2>&1)
	case "$KCORE" in
	"kcore added to build-id cache directory "*)
		KCORE_DIR=${KCORE#"kcore added to build-id cache directory "}
	;;
	*)
		kill $PERF_PID
		wait >/dev/null 2>/dev/null || true
		rm perf.data.junk
		echo "$KCORE"
		echo "Failed to find kcore" >&2
		exit 1
	;;
	esac

	kill $PERF_PID
	wait >/dev/null 2>/dev/null || true
	rm perf.data.junk

	$SUDO cp -a "$KCORE_DIR" "$(pwd)/$PERF_DATA_DIR"
	$SUDO rm -f "$KCORE_DIR/kcore"
	$SUDO rm -f "$KCORE_DIR/kallsyms"
	$SUDO rm -f "$KCORE_DIR/modules"
	$SUDO rmdir "$KCORE_DIR"

	KCORE_DIR_BASENAME=$(basename "$KCORE_DIR")
	KCORE_DIR="$(pwd)/$PERF_DATA_DIR/$KCORE_DIR_BASENAME"

	$SUDO chown $UID "$KCORE_DIR"
	$SUDO chown $UID "$KCORE_DIR/kcore"
	$SUDO chown $UID "$KCORE_DIR/kallsyms"
	$SUDO chown $UID "$KCORE_DIR/modules"

	$SUDO chgrp $GROUPS "$KCORE_DIR"
	$SUDO chgrp $GROUPS "$KCORE_DIR/kcore"
	$SUDO chgrp $GROUPS "$KCORE_DIR/kallsyms"
	$SUDO chgrp $GROUPS "$KCORE_DIR/modules"

	ln -s "$KCORE_DIR_BASENAME" "$PERF_DATA_DIR/kcore_dir"
}

fix_buildid_cache_permissions()
{
	if [ $EUID -ne 0 ] ; then
		echo "This script must be run as root via sudo " >&2
		exit 1
	fi

	if [ -z "$SUDO_USER" ] ; then
		echo "This script must be run via sudo" >&2
		exit 1
	fi

	USER_HOME=$(bash <<< "echo ~$SUDO_USER")

	echo "Fixing buildid cache permissions"

	find "$USER_HOME/.debug" -xdev -type d          ! -user "$SUDO_USER" -ls -exec chown    "$SUDO_USER" \{\} \;
	find "$USER_HOME/.debug" -xdev -type f -links 1 ! -user "$SUDO_USER" -ls -exec chown    "$SUDO_USER" \{\} \;
	find "$USER_HOME/.debug" -xdev -type l          ! -user "$SUDO_USER" -ls -exec chown -h "$SUDO_USER" \{\} \;

	if [ -n "$SUDO_GID" ] ; then
		find "$USER_HOME/.debug" -xdev -type d          ! -group "$SUDO_GID" -ls -exec chgrp    "$SUDO_GID" \{\} \;
		find "$USER_HOME/.debug" -xdev -type f -links 1 ! -group "$SUDO_GID" -ls -exec chgrp    "$SUDO_GID" \{\} \;
		find "$USER_HOME/.debug" -xdev -type l          ! -group "$SUDO_GID" -ls -exec chgrp -h "$SUDO_GID" \{\} \;
	fi

	echo "Done"
}

check_buildid_cache_permissions()
{
	if [ $EUID -eq 0 ] ; then
		return
	fi

	PERMISSIONS_OK+=$(find "$HOME/.debug" -xdev -type d          ! -user "$USER" -print -quit)
	PERMISSIONS_OK+=$(find "$HOME/.debug" -xdev -type f -links 1 ! -user "$USER" -print -quit)
	PERMISSIONS_OK+=$(find "$HOME/.debug" -xdev -type l          ! -user "$USER" -print -quit)

	PERMISSIONS_OK+=$(find "$HOME/.debug" -xdev -type d          ! -group "$GROUPS" -print -quit)
	PERMISSIONS_OK+=$(find "$HOME/.debug" -xdev -type f -links 1 ! -group "$GROUPS" -print -quit)
	PERMISSIONS_OK+=$(find "$HOME/.debug" -xdev -type l          ! -group "$GROUPS" -print -quit)

	if [ -n "$PERMISSIONS_OK" ] ; then
		echo "*** WARNING *** buildid cache permissions may need fixing" >&2
	fi
}

record()
{
	echo "Recording"

	if [ $EUID -ne 0 ] ; then

		if [ "$(cat /proc/sys/kernel/kptr_restrict)" -ne 0 ] ; then
			echo "*** WARNING *** /proc/sys/kernel/kptr_restrict prevents access to kernel addresses" >&2
		fi

		if echo "${PERF_OPTIONS[@]}" | grep -q ' -a \|^-a \| -a$\|^-a$\| --all-cpus \|^--all-cpus \| --all-cpus$\|^--all-cpus$' ; then
			echo "*** WARNING *** system-wide tracing without root access will not be able to read all necessary information from /proc" >&2
		fi

		if echo "${PERF_OPTIONS[@]}" | grep -q 'intel_pt\|intel_bts\| -I\|^-I' ; then
			if [ "$(cat /proc/sys/kernel/perf_event_paranoid)" -gt -1 ] ; then
				echo "*** WARNING *** /proc/sys/kernel/perf_event_paranoid restricts buffer size and tracepoint (sched_switch) use" >&2
			fi

			if echo "${PERF_OPTIONS[@]}" | grep -q ' --per-thread \|^--per-thread \| --per-thread$\|^--per-thread$' ; then
				true
			elif echo "${PERF_OPTIONS[@]}" | grep -q ' -t \|^-t \| -t$\|^-t$' ; then
				true
			elif [ ! -r /sys/kernel/debug -o ! -x /sys/kernel/debug ] ; then
				echo "*** WARNING *** /sys/kernel/debug permissions prevent tracepoint (sched_switch) use" >&2
			fi
		fi
	fi

	if [ -z "$1" ] ; then
		echo "Workload is required for recording" >&2
		usage
	fi

	if [ -e "$PERF_DATA_DIR" ] ; then
		echo "'$PERF_DATA_DIR' exists" >&2
		exit 1
	fi

	find_perf

	mkdir "$PERF_DATA_DIR"

	echo "$PERF record -o $PERF_DATA_DIR/perf.data ${PERF_OPTIONS[@]} -- $@"
	"$PERF" record -o "$PERF_DATA_DIR/perf.data" "${PERF_OPTIONS[@]}" -- "$@" || true

	if rmdir "$PERF_DATA_DIR" > /dev/null 2>/dev/null ; then
		exit 1
	fi

	copy_kcore

	echo "Done"
}

subcommand()
{
	find_perf
	check_buildid_cache_permissions
	echo "$PERF $PERF_SUB_COMMAND -i $PERF_DATA_DIR/perf.data --kallsyms=$PERF_DATA_DIR/kcore_dir/kallsyms $@"
	"$PERF" $PERF_SUB_COMMAND -i "$PERF_DATA_DIR/perf.data" "--kallsyms=$PERF_DATA_DIR/kcore_dir/kallsyms" "$@"
}

if [ "$1" = "fix_buildid_cache_permissions" ] ; then
	fix_buildid_cache_permissions
	exit 0
fi

PERF_SUB_COMMAND=$1
PERF_DATA_DIR=$2
shift || true
shift || true

if [ -z "$PERF_SUB_COMMAND" ] ; then
	usage
fi

if [ -z "$PERF_DATA_DIR" ] ; then
	usage
fi

case "$PERF_SUB_COMMAND" in
"record")
	while [ "$1" != "--" ] ; do
		PERF_OPTIONS+=("$1")
		shift || break
	done
	if [ "$1" != "--" ] ; then
		echo "Options and workload are required for recording" >&2
		usage
	fi
	shift
	record "$@"
;;
"script")
	subcommand "$@"
;;
"report")
	subcommand "$@"
;;
"inject")
	subcommand "$@"
;;
*)
	usage
;;
esac
