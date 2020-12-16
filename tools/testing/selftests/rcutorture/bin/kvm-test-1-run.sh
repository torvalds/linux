#!/bin/bash
# SPDX-License-Identifier: GPL-2.0+
#
# Run a kvm-based test of the specified tree on the specified configs.
# Fully automated run and error checking, no graphics console.
#
# Execute this in the source tree.  Do not run it as a background task
# because qemu does not seem to like that much.
#
# Usage: kvm-test-1-run.sh config builddir resdir seconds qemu-args boot_args
#
# qemu-args defaults to "-enable-kvm -nographic", along with arguments
#			specifying the number of CPUs and other options
#			generated from the underlying CPU architecture.
# boot_args defaults to value returned by the per_version_boot_params
#			shell function.
#
# Anything you specify for either qemu-args or boot_args is appended to
# the default values.  The "-smp" value is deduced from the contents of
# the config fragment.
#
# More sophisticated argument parsing is clearly needed.
#
# Copyright (C) IBM Corporation, 2011
#
# Authors: Paul E. McKenney <paulmck@linux.ibm.com>

T=${TMPDIR-/tmp}/kvm-test-1-run.sh.$$
trap 'rm -rf $T' 0
mkdir $T

. functions.sh
. $CONFIGFRAG/ver_functions.sh

config_template=${1}
config_dir=`echo $config_template | sed -e 's,/[^/]*$,,'`
title=`echo $config_template | sed -e 's/^.*\///'`
builddir=${2}
resdir=${3}
if test -z "$resdir" -o ! -d "$resdir" -o ! -w "$resdir"
then
	echo "kvm-test-1-run.sh :$resdir: Not a writable directory, cannot store results into it"
	exit 1
fi
echo ' ---' `date`: Starting build
echo ' ---' Kconfig fragment at: $config_template >> $resdir/log
touch $resdir/ConfigFragment.input

# Combine additional Kconfig options into an existing set such that
# newer options win.  The first argument is the Kconfig source ID, the
# second the to-be-updated file within $T, and the third and final the
# list of additional Kconfig options.  Note that a $2.tmp file is
# created when doing the update.
config_override_param () {
	if test -n "$3"
	then
		echo $3 | sed -e 's/^ *//' -e 's/ *$//' | tr -s " " "\012" > $T/Kconfig_args
		echo " --- $1" >> $resdir/ConfigFragment.input
		cat $T/Kconfig_args >> $resdir/ConfigFragment.input
		config_override.sh $T/$2 $T/Kconfig_args > $T/$2.tmp
		mv $T/$2.tmp $T/$2
		# Note that "#CHECK#" is not permitted on commandline.
	fi
}

echo > $T/KcList
config_override_param "$config_dir/CFcommon" KcList "`cat $config_dir/CFcommon 2> /dev/null`"
config_override_param "$config_template" KcList "`cat $config_template 2> /dev/null`"
config_override_param "--gdb options" KcList "$TORTURE_KCONFIG_GDB_ARG"
config_override_param "--kasan options" KcList "$TORTURE_KCONFIG_KASAN_ARG"
config_override_param "--kcsan options" KcList "$TORTURE_KCONFIG_KCSAN_ARG"
config_override_param "--kconfig argument" KcList "$TORTURE_KCONFIG_ARG"
cp $T/KcList $resdir/ConfigFragment

base_resdir=`echo $resdir | sed -e 's/\.[0-9]\+$//'`
if test "$base_resdir" != "$resdir" -a -f $base_resdir/bzImage -a -f $base_resdir/vmlinux
then
	# Rerunning previous test, so use that test's kernel.
	QEMU="`identify_qemu $base_resdir/vmlinux`"
	BOOT_IMAGE="`identify_boot_image $QEMU`"
	KERNEL=$base_resdir/${BOOT_IMAGE##*/} # use the last component of ${BOOT_IMAGE}
	ln -s $base_resdir/Make*.out $resdir  # for kvm-recheck.sh
	ln -s $base_resdir/.config $resdir  # for kvm-recheck.sh
	# Arch-independent indicator
	touch $resdir/builtkernel
elif kvm-build.sh $T/KcList $resdir
then
	# Had to build a kernel for this test.
	QEMU="`identify_qemu vmlinux`"
	BOOT_IMAGE="`identify_boot_image $QEMU`"
	cp vmlinux $resdir
	cp .config $resdir
	cp Module.symvers $resdir > /dev/null || :
	cp System.map $resdir > /dev/null || :
	if test -n "$BOOT_IMAGE"
	then
		cp $BOOT_IMAGE $resdir
		KERNEL=$resdir/${BOOT_IMAGE##*/}
		# Arch-independent indicator
		touch $resdir/builtkernel
	else
		echo No identifiable boot image, not running KVM, see $resdir.
		echo Do the torture scripts know about your architecture?
	fi
	parse-build.sh $resdir/Make.out $title
else
	# Build failed.
	cp .config $resdir || :
	echo Build failed, not running KVM, see $resdir.
	if test -f $builddir.wait
	then
		mv $builddir.wait $builddir.ready
	fi
	exit 1
fi
if test -f $builddir.wait
then
	mv $builddir.wait $builddir.ready
fi
while test -f $builddir.ready
do
	sleep 1
done
seconds=$4
qemu_args=$5
boot_args=$6

kstarttime=`gawk 'BEGIN { print systime() }' < /dev/null`
if test -z "$TORTURE_BUILDONLY"
then
	echo ' ---' `date`: Starting kernel
fi

# Generate -smp qemu argument.
qemu_args="-enable-kvm -nographic $qemu_args"
cpu_count=`configNR_CPUS.sh $resdir/ConfigFragment`
cpu_count=`configfrag_boot_cpus "$boot_args" "$config_template" "$cpu_count"`
if test "$cpu_count" -gt "$TORTURE_ALLOTED_CPUS"
then
	echo CPU count limited from $cpu_count to $TORTURE_ALLOTED_CPUS | tee -a $resdir/Warnings
	cpu_count=$TORTURE_ALLOTED_CPUS
fi
qemu_args="`specify_qemu_cpus "$QEMU" "$qemu_args" "$cpu_count"`"
qemu_args="`specify_qemu_net "$qemu_args"`"

# Generate architecture-specific and interaction-specific qemu arguments
qemu_args="$qemu_args `identify_qemu_args "$QEMU" "$resdir/console.log"`"

# Generate qemu -append arguments
qemu_append="`identify_qemu_append "$QEMU"`"

# Pull in Kconfig-fragment boot parameters
boot_args="`configfrag_boot_params "$boot_args" "$config_template"`"
# Generate kernel-version-specific boot parameters
boot_args="`per_version_boot_params "$boot_args" $resdir/.config $seconds`"
if test -n "$TORTURE_BOOT_GDB_ARG"
then
	boot_args="$boot_args $TORTURE_BOOT_GDB_ARG"
fi
echo $QEMU $qemu_args -m $TORTURE_QEMU_MEM -kernel $KERNEL -append \"$qemu_append $boot_args\" $TORTURE_QEMU_GDB_ARG > $resdir/qemu-cmd

if test -n "$TORTURE_BUILDONLY"
then
	echo Build-only run specified, boot/test omitted.
	touch $resdir/buildonly
	exit 0
fi

# Decorate qemu-cmd with redirection, backgrounding, and PID capture
sed -e 's/$/ 2>\&1 \&/' < $resdir/qemu-cmd > $T/qemu-cmd
echo 'echo $! > $resdir/qemu_pid' >> $T/qemu-cmd

# In case qemu refuses to run...
echo "NOTE: $QEMU either did not run or was interactive" > $resdir/console.log

# Attempt to run qemu
( . $T/qemu-cmd; wait `cat  $resdir/qemu_pid`; echo $? > $resdir/qemu-retval ) &
commandcompleted=0
if test -z "$TORTURE_KCONFIG_GDB_ARG"
then
	sleep 10 # Give qemu's pid a chance to reach the file
	if test -s "$resdir/qemu_pid"
	then
		qemu_pid=`cat "$resdir/qemu_pid"`
		echo Monitoring qemu job at pid $qemu_pid
	else
		qemu_pid=""
		echo Monitoring qemu job at yet-as-unknown pid
	fi
fi
if test -n "$TORTURE_KCONFIG_GDB_ARG"
then
	echo Waiting for you to attach a debug session, for example: > /dev/tty
	echo "    gdb $base_resdir/vmlinux" > /dev/tty
	echo 'After symbols load and the "(gdb)" prompt appears:' > /dev/tty
	echo "    target remote :1234" > /dev/tty
	echo "    continue" > /dev/tty
	kstarttime=`gawk 'BEGIN { print systime() }' < /dev/null`
fi
while :
do
	if test -z "$qemu_pid" -a -s "$resdir/qemu_pid"
	then
		qemu_pid=`cat "$resdir/qemu_pid"`
	fi
	kruntime=`gawk 'BEGIN { print systime() - '"$kstarttime"' }' < /dev/null`
	if test -z "$qemu_pid" || kill -0 "$qemu_pid" > /dev/null 2>&1
	then
		if test $kruntime -ge $seconds -o -f "$TORTURE_STOPFILE"
		then
			break;
		fi
		sleep 1
	else
		commandcompleted=1
		if test $kruntime -lt $seconds
		then
			echo Completed in $kruntime vs. $seconds >> $resdir/Warnings 2>&1
			grep "^(qemu) qemu:" $resdir/kvm-test-1-run.sh.out >> $resdir/Warnings 2>&1
			killpid="`sed -n "s/^(qemu) qemu: terminating on signal [0-9]* from pid \([0-9]*\).*$/\1/p" $resdir/Warnings`"
			if test -n "$killpid"
			then
				echo "ps -fp $killpid" >> $resdir/Warnings 2>&1
				ps -fp $killpid >> $resdir/Warnings 2>&1
			fi
		else
			echo ' ---' `date`: "Kernel done"
		fi
		break
	fi
done
if test -z "$qemu_pid" -a -s "$resdir/qemu_pid"
then
	qemu_pid=`cat "$resdir/qemu_pid"`
fi
if test $commandcompleted -eq 0 -a -n "$qemu_pid"
then
	if ! test -f "$TORTURE_STOPFILE"
	then
		echo Grace period for qemu job at pid $qemu_pid
	fi
	oldline="`tail $resdir/console.log`"
	while :
	do
		if test -f "$TORTURE_STOPFILE"
		then
			echo "PID $qemu_pid killed due to run STOP request" >> $resdir/Warnings 2>&1
			kill -KILL $qemu_pid
			break
		fi
		kruntime=`gawk 'BEGIN { print systime() - '"$kstarttime"' }' < /dev/null`
		if kill -0 $qemu_pid > /dev/null 2>&1
		then
			:
		else
			break
		fi
		must_continue=no
		newline="`tail $resdir/console.log`"
		if test "$newline" != "$oldline" && echo $newline | grep -q ' [0-9]\+us : '
		then
			must_continue=yes
		fi
		last_ts="`tail $resdir/console.log | grep '^\[ *[0-9]\+\.[0-9]\+]' | tail -1 | sed -e 's/^\[ *//' -e 's/\..*$//'`"
		if test -z "$last_ts"
		then
			last_ts=0
		fi
		if test "$newline" != "$oldline" -a "$last_ts" -lt $((seconds + $TORTURE_SHUTDOWN_GRACE))
		then
			must_continue=yes
		fi
		if test $must_continue = no -a $kruntime -ge $((seconds + $TORTURE_SHUTDOWN_GRACE))
		then
			echo "!!! PID $qemu_pid hung at $kruntime vs. $seconds seconds" >> $resdir/Warnings 2>&1
			kill -KILL $qemu_pid
			break
		fi
		oldline=$newline
		sleep 10
	done
elif test -z "$qemu_pid"
then
	echo Unknown PID, cannot kill qemu command
fi

parse-console.sh $resdir/console.log $title
