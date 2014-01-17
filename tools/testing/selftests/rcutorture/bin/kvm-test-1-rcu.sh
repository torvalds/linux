#!/bin/bash
#
# Run a kvm-based test of the specified tree on the specified configs.
# Fully automated run and error checking, no graphics console.
#
# Execute this in the source tree.  Do not run it as a background task
# because qemu does not seem to like that much.
#
# Usage: sh kvm-test-1-rcu.sh config builddir resdir minutes qemu-args boot_args
#
# qemu-args defaults to "" -- you will want "-nographic" if running headless.
# boot_args defaults to	"root=/dev/sda noapic selinux=0 console=ttyS0"
#			"initcall_debug debug rcutorture.stat_interval=15"
#			"rcutorture.shutdown_secs=$((minutes * 60))"
#			"rcutorture.rcutorture_runnable=1"
#
# Anything you specify for either qemu-args or boot_args is appended to
# the default values.  The "-smp" value is deduced from the contents of
# the config fragment.
#
# More sophisticated argument parsing is clearly needed.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, you can access it online at
# http://www.gnu.org/licenses/gpl-2.0.html.
#
# Copyright (C) IBM Corporation, 2011
#
# Authors: Paul E. McKenney <paulmck@linux.vnet.ibm.com>

grace=120

T=/tmp/kvm-test-1-rcu.sh.$$
trap 'rm -rf $T' 0

. $KVM/bin/functions.sh
. $KVPATH/ver_functions.sh

config_template=${1}
title=`echo $config_template | sed -e 's/^.*\///'`
builddir=${2}
if test -z "$builddir" -o ! -d "$builddir" -o ! -w "$builddir"
then
	echo "kvm-test-1-rcu.sh :$builddir: Not a writable directory, cannot build into it"
	exit 1
fi
resdir=${3}
if test -z "$resdir" -o ! -d "$resdir" -o ! -w "$resdir"
then
	echo "kvm-test-1-rcu.sh :$resdir: Not a writable directory, cannot store results into it"
	exit 1
fi
cp $config_template $resdir/ConfigFragment
echo ' ---' `date`: Starting build
echo ' ---' Kconfig fragment at: $config_template >> $resdir/log
cat << '___EOF___' >> $T
CONFIG_RCU_TORTURE_TEST=y
___EOF___
# Optimizations below this point
# CONFIG_USB=n
# CONFIG_SECURITY=n
# CONFIG_NFS_FS=n
# CONFIG_SOUND=n
# CONFIG_INPUT_JOYSTICK=n
# CONFIG_INPUT_TABLET=n
# CONFIG_INPUT_TOUCHSCREEN=n
# CONFIG_INPUT_MISC=n
# CONFIG_INPUT_MOUSE=n
# # CONFIG_NET=n # disables console access, so accept the slower build.
# CONFIG_SCSI=n
# CONFIG_ATA=n
# CONFIG_FAT_FS=n
# CONFIG_MSDOS_FS=n
# CONFIG_VFAT_FS=n
# CONFIG_ISO9660_FS=n
# CONFIG_QUOTA=n
# CONFIG_HID=n
# CONFIG_CRYPTO=n
# CONFIG_PCCARD=n
# CONFIG_PCMCIA=n
# CONFIG_CARDBUS=n
# CONFIG_YENTA=n
if kvm-build.sh $config_template $builddir $T
then
	cp $builddir/Make*.out $resdir
	cp $builddir/.config $resdir
	cp $builddir/arch/x86/boot/bzImage $resdir
	parse-build.sh $resdir/Make.out $title
	if test -f $builddir.wait
	then
		mv $builddir.wait $builddir.ready
	fi
else
	cp $builddir/Make*.out $resdir
	echo Build failed, not running KVM, see $resdir.
	if test -f $builddir.wait
	then
		mv $builddir.wait $builddir.ready
	fi
	exit 1
fi
while test -f $builddir.ready
do
	sleep 1
done
minutes=$4
seconds=$(($minutes * 60))
qemu_args=$5
boot_args=$6

cd $KVM
kstarttime=`awk 'BEGIN { print systime() }' < /dev/null`
echo ' ---' `date`: Starting kernel

# Determine the appropriate flavor of qemu command.
QEMU="`identify_qemu $builddir/vmlinux`"

# Generate -smp qemu argument.
qemu_args="-nographic $qemu_args"
cpu_count=`configNR_CPUS.sh $config_template`
vcpus=`identify_qemu_vcpus`
if test $cpu_count -gt $vcpus
then
	echo CPU count limited from $cpu_count to $vcpus
	touch $resdir/Warnings
	echo CPU count limited from $cpu_count to $vcpus >> $resdir/Warnings
	cpu_count=$vcpus
fi
qemu_args="`specify_qemu_cpus "$QEMU" "$qemu_args" "$cpu_count"`"

# Generate architecture-specific and interaction-specific qemu arguments
qemu_args="$qemu_args `identify_qemu_args "$QEMU" "$builddir/console.log"`"

# Generate qemu -append arguments
qemu_append="`identify_qemu_append "$QEMU"`"

# Pull in Kconfig-fragment boot parameters
boot_args="`configfrag_boot_params "$boot_args" "$config_template"`"
# Generate CPU-hotplug boot parameters
boot_args="`rcutorture_param_onoff "$boot_args" $builddir/.config`"
# Generate rcu_barrier() boot parameter
boot_args="`rcutorture_param_n_barrier_cbs "$boot_args"`"
# Pull in standard rcutorture boot arguments
boot_args="$boot_args rcutorture.stat_interval=15 rcutorture.shutdown_secs=$seconds rcutorture.rcutorture_runnable=1 rcutorture.test_no_idle_hz=1 rcutorture.verbose=1"

echo $QEMU $qemu_args -m 512 -kernel $builddir/arch/x86/boot/bzImage -append \"$qemu_append $boot_args\" > $resdir/qemu-cmd
if test -n "$RCU_BUILDONLY"
then
	echo Build-only run specified, boot/test omitted.
	exit 0
fi
$QEMU $qemu_args -m 512 -kernel $builddir/arch/x86/boot/bzImage -append "$qemu_append $boot_args" &
qemu_pid=$!
commandcompleted=0
echo Monitoring qemu job at pid $qemu_pid
for ((i=0;i<$seconds;i++))
do
	if kill -0 $qemu_pid > /dev/null 2>&1
	then
		sleep 1
	else
		commandcompleted=1
		kruntime=`awk 'BEGIN { print systime() - '"$kstarttime"' }' < /dev/null`
		if test $kruntime -lt $seconds
		then
			echo Completed in $kruntime vs. $seconds >> $resdir/Warnings 2>&1
		else
			echo ' ---' `date`: Kernel done
		fi
		break
	fi
done
if test $commandcompleted -eq 0
then
	echo Grace period for qemu job at pid $qemu_pid
	for ((i=0;i<=$grace;i++))
	do
		if kill -0 $qemu_pid > /dev/null 2>&1
		then
			sleep 1
		else
			break
		fi
		if test $i -eq $grace
		then
			kruntime=`awk 'BEGIN { print systime() - '"$kstarttime"' }'`
			echo "!!! Hang at $kruntime vs. $seconds seconds" >> $resdir/Warnings 2>&1
			kill -KILL $qemu_pid
		fi
	done
fi

cp $builddir/console.log $resdir
parse-rcutorture.sh $resdir/console.log $title
parse-console.sh $resdir/console.log $title
