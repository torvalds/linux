#!/bin/bash
###############################################################################
# Copyright (C) 2007-2010 Lawrence Livermore National Security, LLC.
# Copyright (C) 2007 The Regents of the University of California.
# Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
# Written by Brian Behlendorf <behlendorf1@llnl.gov>.
# UCRL-CODE-235197
#
# This file is part of the SPL, Solaris Porting Layer.
# For details, see <http://zfsonlinux.org/>.
#
# The SPL is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the
# Free Software Foundation; either version 2 of the License, or (at your
# option) any later version.
#
# The SPL is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
# for more details.
#
# You should have received a copy of the GNU General Public License along
# with the SPL.  If not, see <http://www.gnu.org/licenses/>.
###############################################################################
# This script runs the full set of regression tests.
###############################################################################

prog=check.sh
spl_module=../module/spl/spl.ko
splat_module=../module/splat/splat.ko
splat_cmd=../cmd/splat
verbose=

die() {
	echo "${prog}: $1" >&2
	exit 1
}

warn() {
	echo "${prog}: $1" >&2
}

if [ -n "$V" ]; then
	verbose="-v"
fi

if [ -n "$TESTS" ]; then
	tests="$TESTS"
else
	tests="-a"
fi

if [ $(id -u) != 0 ]; then
	die "Must run as root"
fi

if /sbin/lsmod | egrep -q "^spl|^splat"; then
	die "Must start with spl modules unloaded"
fi

if [ ! -f ${spl_module} ] || [ ! -f ${splat_module} ]; then
	die "Source tree must be built, run 'make'"
fi

/sbin/modprobe zlib_inflate &>/dev/null
/sbin/modprobe zlib_deflate &>/dev/null

echo "Loading ${spl_module}"
/sbin/insmod ${spl_module} || die "Failed to load ${spl_module}"

echo "Loading ${splat_module}"
/sbin/insmod ${splat_module} || die "Unable to load ${splat_module}"

# Wait a maximum of 3 seconds for udev to detect the new splatctl 
# device, if we do not see the character device file created assume
# udev is not running and manually create the character device.
for i in `seq 1 50`; do
	sleep 0.1

	if [ -c /dev/splatctl ]; then
		break
	fi

	if [ $i -eq 50 ]; then
		mknod /dev/splatctl c 229 0
	fi
done

$splat_cmd $tests $verbose

echo "Unloading ${splat_module}"
/sbin/rmmod ${splat_module} || die "Failed to unload ${splat_module}"

echo "Unloading ${spl_module}"
/sbin/rmmod ${spl_module} || die "Unable to unload ${spl_module}"

exit 0
