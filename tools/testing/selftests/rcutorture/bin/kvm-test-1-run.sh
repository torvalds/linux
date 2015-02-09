#!/bin/bash
#
# Run a kvm-based test of the specified tree on the specified configs.
# Fully automated run and error checking, no graphics console.
#
# Execute this in the source tree.  Do not run it as a background task
# because qemu does not seem to like that much.
#
# Usage: kvm-test-1-run.sh config builddir resdir minutes qemu-args boot_args
#
# qemu-args defaults to "-enable-kvm -soundhw pcspk -nographic", along with
#			arguments specifying the number of CPUs and other
#			options generated from the underlying CPU architecture.
# boot_args defaults to value returned by the per_version_boot_params
#			shell function.
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

T=/tmp/kvm-test-1-run.sh.$$
trap 'rm -rf $T' 0
touch $T

. $KVM/bin/functions.sh
. $CONFIGFRAG/ver_functions.sh

config_template=${1}
config_dir=`echo $config_template | sed -e 's,/[^/]*$,,'`
title=`echo $config_template | sed -e 's/^.*\///'`
builddir=${2}
if test -z "$builddir" -o ! -d "$builddir" -o ! -w "$builddir"
then
	echo "kvm-test-1-run.sh :$builddir: Not a writable directory, cannot build into it"
	exit 1
fi
resdir=${3}
if test -z "$resdir" -o ! -d "$resdir" -o ! -w "$resdir"
then
	echo "kvm-test-1-run.sh :$resdir: Not a writable directory, cannot store results into it"
	exit 1
fi
cp $config_template $resdir/ConfigFragment
echo ' ---' `date`: Starting build
echo ' ---' Kconfig fragment at: $config_template >> $resdir/log
if test -r "$config_dir/CFcommon"
then
	cat < $config_dir/CFcommon >> $T
fi
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
	QEMU="`identify_qemu $builddir/vmlinux`"
	BOOT_IMAGE="`identify_boot_image $QEMU`"
	cp $builddir/Make*.out $resdir
	cp $builddir/.config $resdir
	if test -n "$BOOT_IMAGE"
	then
		cp $builddir/$BOOT_IMAGE $resdir
	else
		echo No identifiable boot image, not running KVM, see $resdir.
		echo Do the torture scripts know about your architecture?
	fi
	parse-build.sh $resdir/Make.out $title
	if test -f $builddir.wait
	then
		mv $builddir.wait $builddir.ready
	fi
else
	cp $builddir/Make*.out $resdir
	cp $builddir/.config $resdir || :
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
if test -z "$TORTURE_BUILDONLY"
then
	echo ' ---' `date`: Starting kernel
fi

# Generate -smp qemu argument.
qemu_args="-enable-kvm -soundhw pcspk -nographic $qemu_args"
cpu_count=`configNR_CPUS.sh $config_template`
cpu_count=`configfrag_boot_cpus "$boot_args" "$config_template" "$cpu_count"`
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
# Generate kernel-version-specific boot parameters
boot_args="`per_version_boot_params "$boot_args" $builddir/.config $seconds`"

if test -n "$TORTURE_BUILDONLY"
then
	echo Build-only run specified, boot/test omitted.
	touch $resdir/buildonly
	exit 0
fi
echo "NOTE: $QEMU either did not run or was interactive" > $builddir/console.log
echo $QEMU $qemu_args -m 512 -kernel $resdir/bzImage -append \"$qemu_append $boot_args\" > $resdir/qemu-cmd
( $QEMU $qemu_args -m 512 -kernel $resdir/bzImage -append "$qemu_append $boot_args"; echo $? > $resdir/qemu-retval ) &
qemu_pid=$!
commandcompleted=0
echo Monitoring qemu job at pid $qemu_pid
while :
do
	kruntime=`awk 'BEGIN { print systime() - '"$kstarttime"' }' < /dev/null`
	if kill -0 $qemu_pid > /dev/null 2>&1
	then
		if test $kruntime -ge $seconds
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
			echo ' ---' `date`: Kernel done
		fi
		break
	fi
done
if test $commandcompleted -eq 0
then
	echo Grace period for qemu job at pid $qemu_pid
	while :
	do
		kruntime=`awk 'BEGIN { print systime() - '"$kstarttime"' }' < /dev/null`
		if kill -0 $qemu_pid > /dev/null 2>&1
		then
			:
		else
			break
		fi
		if test $kruntime -ge $((seconds + grace))
		then
			echo "!!! PID $qemu_pid hung at $kruntime vs. $seconds seconds" >> $resdir/Warnings 2>&1
			kill -KILL $qemu_pid
			break
		fi
		sleep 1
	done
fi

cp $builddir/console.log $resdir
parse-torture.sh $resdir/console.log $title
parse-console.sh $resdir/console.log $title
