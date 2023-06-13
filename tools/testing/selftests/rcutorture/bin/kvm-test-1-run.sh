#!/bin/bash
# SPDX-License-Identifier: GPL-2.0+
#
# Run a kvm-based test of the specified tree on the specified configs.
# Fully automated run and error checking, no graphics console.
#
# Execute this in the source tree.  Do not run it as a background task
# because qemu does not seem to like that much.
#
# Usage: kvm-test-1-run.sh config resdir seconds qemu-args boot_args_in
#
# qemu-args defaults to "-enable-kvm -nographic", along with arguments
#			specifying the number of CPUs and other options
#			generated from the underlying CPU architecture.
# boot_args_in defaults to value returned by the per_version_boot_params
#			shell function.
#
# Anything you specify for either qemu-args or boot_args_in is appended to
# the default values.  The "-smp" value is deduced from the contents of
# the config fragment.
#
# More sophisticated argument parsing is clearly needed.
#
# Copyright (C) IBM Corporation, 2011
#
# Authors: Paul E. McKenney <paulmck@linux.ibm.com>

T="`mktemp -d ${TMPDIR-/tmp}/kvm-test-1-run.sh.XXXXXX`"
trap 'rm -rf $T' 0

. functions.sh
. $CONFIGFRAG/ver_functions.sh

config_template=${1}
config_dir=`echo $config_template | sed -e 's,/[^/]*$,,'`
title=`echo $config_template | sed -e 's/^.*\///'`
resdir=${2}
if test -z "$resdir" -o ! -d "$resdir" -o ! -w "$resdir"
then
	echo "kvm-test-1-run.sh :$resdir: Not a writable directory, cannot store results into it"
	exit 1
fi
echo ' ---' `date`: Starting build, PID $$
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
if test "$base_resdir" != "$resdir" && test -f $base_resdir/bzImage && test -f $base_resdir/vmlinux
then
	# Rerunning previous test, so use that test's kernel.
	QEMU="`identify_qemu $base_resdir/vmlinux`"
	BOOT_IMAGE="`identify_boot_image $QEMU`"
	KERNEL=$base_resdir/${BOOT_IMAGE##*/} # use the last component of ${BOOT_IMAGE}
	ln -s $base_resdir/Make*.out $resdir  # for kvm-recheck.sh
	ln -s $base_resdir/.config $resdir  # for kvm-recheck.sh
	# Arch-independent indicator
	touch $resdir/builtkernel
elif test "$base_resdir" != "$resdir"
then
	# Rerunning previous test for which build failed
	ln -s $base_resdir/Make*.out $resdir  # for kvm-recheck.sh
	ln -s $base_resdir/.config $resdir  # for kvm-recheck.sh
	echo Initial build failed, not running KVM, see $resdir.
	if test -f $resdir/build.wait
	then
		mv $resdir/build.wait $resdir/build.ready
	fi
	exit 1
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
	if test -f $resdir/build.wait
	then
		mv $resdir/build.wait $resdir/build.ready
	fi
	exit 1
fi
if test -f $resdir/build.wait
then
	mv $resdir/build.wait $resdir/build.ready
fi
while test -f $resdir/build.ready
do
	sleep 1
done
seconds=$3
qemu_args=$4
boot_args_in=$5

if test -z "$TORTURE_BUILDONLY"
then
	echo ' ---' `date`: Starting kernel
fi

# Generate -smp qemu argument.
qemu_args="-enable-kvm -nographic $qemu_args"
cpu_count=`configNR_CPUS.sh $resdir/ConfigFragment`
cpu_count=`configfrag_boot_cpus "$boot_args_in" "$config_template" "$cpu_count"`
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
boot_args="`configfrag_boot_params "$boot_args_in" "$config_template"`"
# Generate kernel-version-specific boot parameters
boot_args="`per_version_boot_params "$boot_args" $resdir/.config $seconds`"
if test -n "$TORTURE_BOOT_GDB_ARG"
then
	boot_args="$boot_args $TORTURE_BOOT_GDB_ARG"
fi

# Give bare-metal advice
modprobe_args="`echo $boot_args | tr -s ' ' '\012' | grep "^$TORTURE_MOD\." | sed -e "s/$TORTURE_MOD\.//g"`"
kboot_args="`echo $boot_args | tr -s ' ' '\012' | grep -v "^$TORTURE_MOD\."`"
testid_txt="`dirname $resdir`/testid.txt"
touch $resdir/bare-metal
echo To run this scenario on bare metal: >> $resdir/bare-metal
echo >> $resdir/bare-metal
echo " 1." Set your bare-metal build tree to the state shown in this file: >> $resdir/bare-metal
echo "   " $testid_txt >> $resdir/bare-metal
echo " 2." Update your bare-metal build tree"'"s .config based on this file: >> $resdir/bare-metal
echo "   " $resdir/ConfigFragment >> $resdir/bare-metal
echo " 3." Make the bare-metal kernel"'"s build system aware of your .config updates: >> $resdir/bare-metal
echo "   " $ 'yes "" | make oldconfig' >> $resdir/bare-metal
echo " 4." Build your bare-metal kernel. >> $resdir/bare-metal
echo " 5." Boot your bare-metal kernel with the following parameters: >> $resdir/bare-metal
echo "   " $kboot_args >> $resdir/bare-metal
echo " 6." Start the test with the following command: >> $resdir/bare-metal
echo "   " $ modprobe $TORTURE_MOD $modprobe_args >> $resdir/bare-metal
echo " 7." After some time, end the test with the following command: >> $resdir/bare-metal
echo "   " $ rmmod $TORTURE_MOD >> $resdir/bare-metal
echo " 8." Copy your bare-metal kernel"'"s .config file, overwriting this file: >> $resdir/bare-metal
echo "   " $resdir/.config >> $resdir/bare-metal
echo " 9." Copy the console output from just before the modprobe to just after >> $resdir/bare-metal
echo "   " the rmmod into this file: >> $resdir/bare-metal
echo "   " $resdir/console.log >> $resdir/bare-metal
echo "10." Check for runtime errors using the following command: >> $resdir/bare-metal
echo "   " $ tools/testing/selftests/rcutorture/bin/kvm-recheck.sh `dirname $resdir` >> $resdir/bare-metal
echo >> $resdir/bare-metal
echo Some of the above steps may be skipped if you build your bare-metal >> $resdir/bare-metal
echo kernel here: `head -n 1 $testid_txt | sed -e 's/^Build directory: //'`  >> $resdir/bare-metal

echo $QEMU $qemu_args -m $TORTURE_QEMU_MEM -kernel $KERNEL -append \"$qemu_append $boot_args\" $TORTURE_QEMU_GDB_ARG > $resdir/qemu-cmd
echo "# TORTURE_SHUTDOWN_GRACE=$TORTURE_SHUTDOWN_GRACE" >> $resdir/qemu-cmd
echo "# seconds=$seconds" >> $resdir/qemu-cmd
echo "# TORTURE_KCONFIG_GDB_ARG=\"$TORTURE_KCONFIG_GDB_ARG\"" >> $resdir/qemu-cmd
echo "# TORTURE_JITTER_START=\"$TORTURE_JITTER_START\"" >> $resdir/qemu-cmd
echo "# TORTURE_JITTER_STOP=\"$TORTURE_JITTER_STOP\"" >> $resdir/qemu-cmd
echo "# TORTURE_TRUST_MAKE=\"$TORTURE_TRUST_MAKE\"; export TORTURE_TRUST_MAKE" >> $resdir/qemu-cmd
echo "# TORTURE_CPU_COUNT=$cpu_count" >> $resdir/qemu-cmd

if test -n "$TORTURE_BUILDONLY"
then
	echo Build-only run specified, boot/test omitted.
	touch $resdir/buildonly
	exit 0
fi

kvm-test-1-run-qemu.sh $resdir
parse-console.sh $resdir/console.log $title
