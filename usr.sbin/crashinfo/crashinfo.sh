#!/bin/sh
#
# SPDX-License-Identifier: BSD-3-Clause
#
# Copyright (c) 2008 Yahoo!, Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
# 3. Neither the name of the author nor the names of any co-contributors
#    may be used to endorse or promote products derived from this software
#    without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
# OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
# OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.
#
# $FreeBSD$

usage()
{
	echo "usage: crashinfo [-b] [-d crashdir] [-n dumpnr]" \
		"[-k kernel] [core]"
	exit 1
}

# Remove an uncompressed copy of a dump
cleanup()
{

	[ -e $VMCORE ] && rm -f $VMCORE
}

# Find a gdb binary to use and save the value in GDB.
find_gdb()
{
	local binary

	for binary in /usr/local/bin/gdb /usr/libexec/gdb /usr/bin/gdb; do
		if [ -x ${binary} ]; then
			GDB=${binary}
			return
		fi
	done
}

# Run a single gdb command against a kernel file in batch mode.
# The kernel file is specified as the first argument and the command
# is given in the remaining arguments.
gdb_command()
{
	local k

	k=$1 ; shift

	if [ ${GDB} = /usr/local/bin/gdb ]; then
		${GDB} -batch -ex "$@" $k
	else
		echo -e "$@" | ${GDB} -x /dev/stdin -batch $k
	fi
}

find_kernel()
{
	local ivers k kvers

	ivers=$(awk '
	/Version String/ {
		print
		nextline=1
		next
	}
	nextline==1 {
		if ($0 ~ "^  [A-Za-z ]+: ") {
			nextline=0
		} else {
			print
		}
	}' $INFO)

	# Look for a matching kernel version, handling possible truncation
	# of the version string recovered from the dump.
	for k in `sysctl -n kern.bootfile` $(ls -t /boot/*/kernel); do
		kvers=$(gdb_command $k 'printf "  Version String: %s", version' | \
		    awk "{line=line\$0\"\n\"} END{print substr(line,1,${#ivers})}" \
		    2>/dev/null)
		if [ "$ivers" = "$kvers" ]; then
			KERNEL=$k
			break
		fi
	done
}

BATCH=false
CRASHDIR=/var/crash
DUMPNR=
KERNEL=

while getopts "bd:n:k:" opt; do
	case "$opt" in
	b)
		BATCH=true
		;;
	d)
		CRASHDIR=$OPTARG
		;;
	n)
		DUMPNR=$OPTARG
		;;
	k)
		KERNEL=$OPTARG
		;;
	\?)
		usage
		;;
	esac
done

shift $((OPTIND - 1))

if [ $# -eq 1 ]; then
	if [ -n "$DUMPNR" ]; then
		echo "-n and an explicit vmcore are mutually exclusive"
		usage
	fi

	# Figure out the crash directory and number from the vmcore name.
	CRASHDIR=`dirname $1`
	DUMPNR=$(expr $(basename $1) : 'vmcore\.\([0-9]*\)')
	if [ -z "$DUMPNR" ]; then
		echo "Unable to determine dump number from vmcore file $1."
		exit 1
	fi
elif [ $# -gt 1 ]; then
	usage
else
	# If we don't have an explicit dump number, operate on the most
	# recent dump.
	if [ -z "$DUMPNR" ]; then
		if ! [ -r $CRASHDIR/bounds ]; then
			echo "No crash dumps in $CRASHDIR."
			exit 1
		fi			
		next=`cat $CRASHDIR/bounds`
		if [ -z "$next" ] || [ "$next" -eq 0 ]; then
			echo "No crash dumps in $CRASHDIR."
			exit 1
		fi
		DUMPNR=$(($next - 1))
	fi
fi

VMCORE=$CRASHDIR/vmcore.$DUMPNR
INFO=$CRASHDIR/info.$DUMPNR
FILE=$CRASHDIR/core.txt.$DUMPNR
HOSTNAME=`hostname`

if $BATCH; then
	echo "Writing crash summary to $FILE."
	exec > $FILE 2>&1
fi

find_gdb
if [ -z "$GDB" ]; then
	echo "Unable to find a kernel debugger."
	exit 1
fi

if [ ! -e $VMCORE ]; then
    	if [ -e $VMCORE.gz ]; then
		trap cleanup EXIT HUP INT QUIT TERM
		gzcat $VMCORE.gz > $VMCORE
	elif [ -e $VMCORE.zst ]; then
		trap cleanup EXIT HUP INT QUIT TERM
		zstdcat $VMCORE.zst > $VMCORE
	else
		echo "$VMCORE not found"
		exit 1
	fi
fi

if [ ! -e $INFO ]; then
	echo "$INFO not found"
	exit 1
fi

# If the user didn't specify a kernel, then try to find one.
if [ -z "$KERNEL" ]; then
	find_kernel
	if [ -z "$KERNEL" ]; then
		echo "Unable to find matching kernel for $VMCORE"
		exit 1
	fi
elif [ ! -e $KERNEL ]; then
	echo "$KERNEL not found"
	exit 1
fi

umask 077

# Simulate uname
ostype=$(gdb_command $KERNEL 'printf "%s", ostype')
osrelease=$(gdb_command $KERNEL 'printf "%s", osrelease')
version=$(gdb_command $KERNEL 'printf "%s", version' | tr '\t\n' '  ')
machine=$(gdb_command $KERNEL 'printf "%s", machine')

if ! $BATCH; then
	echo "Writing crash summary to $FILE."
	exec > $FILE 2>&1
fi

echo "$HOSTNAME dumped core - see $VMCORE"
echo
date
echo
echo "$ostype $HOSTNAME $osrelease $version $machine"
echo
sed -ne '/^  Panic String: /{s//panic: /;p;}' $INFO
echo

# XXX: /bin/sh on 7.0+ is broken so we can't simply pipe the commands to
# kgdb via stdin and have to use a temporary file instead.
file=`mktemp /tmp/crashinfo.XXXXXX`
if [ $? -eq 0 ]; then
	echo "bt" >> $file
	echo "quit" >> $file
	${GDB%gdb}kgdb $KERNEL $VMCORE < $file
	rm -f $file
	echo
fi
echo

echo "------------------------------------------------------------------------"
echo "ps -axlww"
echo
ps -M $VMCORE -N $KERNEL -axlww
echo

echo "------------------------------------------------------------------------"
echo "vmstat -s"
echo
vmstat -M $VMCORE -N $KERNEL -s
echo

echo "------------------------------------------------------------------------"
echo "vmstat -m"
echo
vmstat -M $VMCORE -N $KERNEL -m
echo

echo "------------------------------------------------------------------------"
echo "vmstat -z"
echo
vmstat -M $VMCORE -N $KERNEL -z
echo

echo "------------------------------------------------------------------------"
echo "vmstat -i"
echo
vmstat -M $VMCORE -N $KERNEL -i
echo

echo "------------------------------------------------------------------------"
echo "pstat -T"
echo
pstat -M $VMCORE -N $KERNEL -T
echo

echo "------------------------------------------------------------------------"
echo "pstat -s"
echo
pstat -M $VMCORE -N $KERNEL -s
echo

echo "------------------------------------------------------------------------"
echo "iostat"
echo
iostat -M $VMCORE -N $KERNEL
echo

echo "------------------------------------------------------------------------"
echo "ipcs -a"
echo
ipcs -C $VMCORE -N $KERNEL -a
echo

echo "------------------------------------------------------------------------"
echo "ipcs -T"
echo
ipcs -C $VMCORE -N $KERNEL -T
echo

# XXX: This doesn't actually work in 5.x+
if false; then
echo "------------------------------------------------------------------------"
echo "w -dn"
echo
w -M $VMCORE -N $KERNEL -dn
echo
fi

echo "------------------------------------------------------------------------"
echo "nfsstat"
echo
nfsstat -M $VMCORE -N $KERNEL
echo

echo "------------------------------------------------------------------------"
echo "netstat -s"
echo
netstat -M $VMCORE -N $KERNEL -s
echo

echo "------------------------------------------------------------------------"
echo "netstat -m"
echo
netstat -M $VMCORE -N $KERNEL -m
echo

echo "------------------------------------------------------------------------"
echo "netstat -anA"
echo
netstat -M $VMCORE -N $KERNEL -anA
echo

echo "------------------------------------------------------------------------"
echo "netstat -aL"
echo
netstat -M $VMCORE -N $KERNEL -aL
echo

echo "------------------------------------------------------------------------"
echo "fstat"
echo
fstat -M $VMCORE -N $KERNEL
echo

echo "------------------------------------------------------------------------"
echo "dmesg"
echo
dmesg -a -M $VMCORE -N $KERNEL
echo

echo "------------------------------------------------------------------------"
echo "kernel config"
echo
config -x $KERNEL

echo
echo "------------------------------------------------------------------------"
echo "ddb capture buffer"
echo

ddb capture -M $VMCORE -N $KERNEL print
