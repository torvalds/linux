#!/bin/bash
# SPDX-License-Identifier: GPL-2.0+
#
# Shell functions for the rest of the scripts.
#
# Copyright (C) IBM Corporation, 2013
#
# Authors: Paul E. McKenney <paulmck@linux.ibm.com>

# bootparam_hotplug_cpu bootparam-string
#
# Returns 1 if the specified boot-parameter string tells rcutorture to
# test CPU-hotplug operations.
bootparam_hotplug_cpu () {
	echo "$1" | grep -q "rcutorture\.onoff_"
}

# checkarg --argname argtype $# arg mustmatch cannotmatch
#
# Checks the specified argument "arg" against the mustmatch and cannotmatch
# patterns.
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

# configfrag_boot_params bootparam-string config-fragment-file
#
# Adds boot parameters from the .boot file, if any.
configfrag_boot_params () {
	if test -r "$2.boot"
	then
		echo $1 `grep -v '^#' "$2.boot" | tr '\012' ' '`
	else
		echo $1
	fi
}

# configfrag_boot_cpus bootparam-string config-fragment-file config-cpus
#
# Decreases number of CPUs based on any nr_cpus= boot parameters specified.
configfrag_boot_cpus () {
	local bootargs="`configfrag_boot_params "$1" "$2"`"
	local nr_cpus
	if echo "${bootargs}" | grep -q 'nr_cpus=[0-9]'
	then
		nr_cpus="`echo "${bootargs}" | sed -e 's/^.*nr_cpus=\([0-9]*\).*$/\1/'`"
		if test "$3" -gt "$nr_cpus"
		then
			echo $nr_cpus
		else
			echo $3
		fi
	else
		echo $3
	fi
}

# configfrag_boot_maxcpus bootparam-string config-fragment-file config-cpus
#
# Decreases number of CPUs based on any maxcpus= boot parameters specified.
# This allows tests where additional CPUs come online later during the
# test run.  However, the torture parameters will be set based on the
# number of CPUs initially present, so the scripting should schedule
# test runs based on the maxcpus= boot parameter controlling the initial
# number of CPUs instead of on the ultimate number of CPUs.
configfrag_boot_maxcpus () {
	local bootargs="`configfrag_boot_params "$1" "$2"`"
	local maxcpus
	if echo "${bootargs}" | grep -q 'maxcpus=[0-9]'
	then
		maxcpus="`echo "${bootargs}" | sed -e 's/^.*maxcpus=\([0-9]*\).*$/\1/'`"
		if test "$3" -gt "$maxcpus"
		then
			echo $maxcpus
		else
			echo $3
		fi
	else
		echo $3
	fi
}

# configfrag_hotplug_cpu config-fragment-file
#
# Returns 1 if the config fragment specifies hotplug CPU.
configfrag_hotplug_cpu () {
	if test ! -r "$1"
	then
		echo Unreadable config fragment "$1" 1>&2
		exit -1
	fi
	grep -q '^CONFIG_HOTPLUG_CPU=y$' "$1"
}

# identify_boot_image qemu-cmd
#
# Returns the relative path to the kernel build image.  This will be
# arch/<arch>/boot/bzImage or vmlinux if bzImage is not a target for the
# architecture, unless overridden with the TORTURE_BOOT_IMAGE environment
# variable.
identify_boot_image () {
	if test -n "$TORTURE_BOOT_IMAGE"
	then
		echo $TORTURE_BOOT_IMAGE
	else
		case "$1" in
		qemu-system-x86_64|qemu-system-i386)
			echo arch/x86/boot/bzImage
			;;
		qemu-system-aarch64)
			echo arch/arm64/boot/Image
			;;
		*)
			echo vmlinux
			;;
		esac
	fi
}

# identify_qemu builddir
#
# Returns our best guess as to which qemu command is appropriate for
# the kernel at hand.  Override with the TORTURE_QEMU_CMD environment variable.
identify_qemu () {
	local u="`file "$1"`"
	if test -n "$TORTURE_QEMU_CMD"
	then
		echo $TORTURE_QEMU_CMD
	elif echo $u | grep -q x86-64
	then
		echo qemu-system-x86_64
	elif echo $u | grep -q "Intel 80386"
	then
		echo qemu-system-i386
	elif echo $u | grep -q aarch64
	then
		echo qemu-system-aarch64
	elif uname -a | grep -q ppc64
	then
		echo qemu-system-ppc64
	else
		echo Cannot figure out what qemu command to use! 1>&2
		echo file $1 output: $u
		# Usually this will be one of /usr/bin/qemu-system-*
		# Use TORTURE_QEMU_CMD environment variable or appropriate
		# argument to top-level script.
		exit 1
	fi
}

# identify_qemu_append qemu-cmd
#
# Output arguments for the qemu "-append" string based on CPU type
# and the TORTURE_QEMU_INTERACTIVE environment variable.
identify_qemu_append () {
	local console=ttyS0
	case "$1" in
	qemu-system-x86_64|qemu-system-i386)
		echo noapic selinux=0 initcall_debug debug
		;;
	qemu-system-aarch64)
		console=ttyAMA0
		;;
	esac
	if test -n "$TORTURE_QEMU_INTERACTIVE"
	then
		echo root=/dev/sda
	else
		echo console=$console
	fi
}

# identify_qemu_args qemu-cmd serial-file
#
# Output arguments for qemu arguments based on the TORTURE_QEMU_MAC
# and TORTURE_QEMU_INTERACTIVE environment variables.
identify_qemu_args () {
	case "$1" in
	qemu-system-x86_64|qemu-system-i386)
		;;
	qemu-system-aarch64)
		echo -machine virt,gic-version=host -cpu host
		;;
	qemu-system-ppc64)
		echo -enable-kvm -M pseries -nodefaults
		echo -device spapr-vscsi
		if test -n "$TORTURE_QEMU_INTERACTIVE" -a -n "$TORTURE_QEMU_MAC"
		then
			echo -device spapr-vlan,netdev=net0,mac=$TORTURE_QEMU_MAC
			echo -netdev bridge,br=br0,id=net0
		elif test -n "$TORTURE_QEMU_INTERACTIVE"
		then
			echo -net nic -net user
		fi
		;;
	esac
	if test -n "$TORTURE_QEMU_INTERACTIVE"
	then
		echo -monitor stdio -serial pty -S
	else
		echo -serial file:$2
	fi
}

# identify_qemu_vcpus
#
# Returns the number of virtual CPUs available to the aggregate of the
# guest OSes.
identify_qemu_vcpus () {
	lscpu | grep '^CPU(s):' | sed -e 's/CPU(s)://'
}

# print_bug
#
# Prints "BUG: " in red followed by remaining arguments
print_bug () {
	printf '\033[031mBUG: \033[m'
	echo $*
}

# print_warning
#
# Prints "WARNING: " in yellow followed by remaining arguments
print_warning () {
	printf '\033[033mWARNING: \033[m'
	echo $*
}

# specify_qemu_cpus qemu-cmd qemu-args #cpus
#
# Appends a string containing "-smp XXX" to qemu-args, unless the incoming
# qemu-args already contains "-smp".
specify_qemu_cpus () {
	local nt;

	if echo $2 | grep -q -e -smp
	then
		echo $2
	else
		case "$1" in
		qemu-system-x86_64|qemu-system-i386|qemu-system-aarch64)
			echo $2 -smp $3
			;;
		qemu-system-ppc64)
			nt="`lscpu | grep '^NUMA node0' | sed -e 's/^[^,]*,\([0-9]*\),.*$/\1/'`"
			echo $2 -smp cores=`expr \( $3 + $nt - 1 \) / $nt`,threads=$nt
			;;
		esac
	fi
}
