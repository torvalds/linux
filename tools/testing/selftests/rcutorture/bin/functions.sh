#!/bin/bash
#
# Shell functions for the rest of the scripts.
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
# Copyright (C) IBM Corporation, 2013
#
# Authors: Paul E. McKenney <paulmck@linux.vnet.ibm.com>

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

# identify_qemu builddir
#
# Returns our best guess as to which qemu command is appropriate for
# the kernel at hand.  Override with the RCU_QEMU_CMD environment variable.
identify_qemu () {
	local u="`file "$1"`"
	if test -n "$RCU_QEMU_CMD"
	then
		echo $RCU_QEMU_CMD
	elif echo $u | grep -q x86-64
	then
		echo qemu-system-x86_64
	elif echo $u | grep -q "Intel 80386"
	then
		echo qemu-system-i386
	elif uname -a | grep -q ppc64
	then
		echo qemu-system-ppc64
	else
		echo Cannot figure out what qemu command to use! 1>&2
		# Usually this will be one of /usr/bin/qemu-system-*
		# Use RCU_QEMU_CMD environment variable or appropriate
		# argument to top-level script.
		exit 1
	fi
}

# identify_qemu_append qemu-cmd
#
# Output arguments for the qemu "-append" string based on CPU type
# and the RCU_QEMU_INTERACTIVE environment variable.
identify_qemu_append () {
	case "$1" in
	qemu-system-x86_64|qemu-system-i386)
		echo noapic selinux=0 initcall_debug debug
		;;
	esac
	if test -n "$RCU_QEMU_INTERACTIVE"
	then
		echo root=/dev/sda
	else
		echo console=ttyS0
	fi
}

# identify_qemu_args qemu-cmd serial-file
#
# Output arguments for qemu arguments based on the RCU_QEMU_MAC
# and RCU_QEMU_INTERACTIVE environment variables.
identify_qemu_args () {
	case "$1" in
	qemu-system-x86_64|qemu-system-i386)
		;;
	qemu-system-ppc64)
		echo -enable-kvm -M pseries -cpu POWER7 -nodefaults
		echo -device spapr-vscsi
		if test -n "$RCU_QEMU_INTERACTIVE" -a -n "$RCU_QEMU_MAC"
		then
			echo -device spapr-vlan,netdev=net0,mac=$RCU_QEMU_MAC
			echo -netdev bridge,br=br0,id=net0
		elif test -n "$RCU_QEMU_INTERACTIVE"
		then
			echo -net nic -net user
		fi
		;;
	esac
	if test -n "$RCU_QEMU_INTERACTIVE"
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
		qemu-system-x86_64|qemu-system-i386)
			echo $2 -smp $3
			;;
		qemu-system-ppc64)
			nt="`lscpu | grep '^NUMA node0' | sed -e 's/^[^,]*,\([0-9]*\),.*$/\1/'`"
			echo $2 -smp cores=`expr \( $3 + $nt - 1 \) / $nt`,threads=$nt
			;;
		esac
	fi
}
