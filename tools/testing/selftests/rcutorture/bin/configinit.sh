#!/bin/bash
#
# Usage: configinit.sh config-spec-file [ build output dir ]
#
# Create a .config file from the spec file.  Run from the kernel source tree.
# Exits with 0 if all went well, with 1 if all went well but the config
# did not match, and some other number for other failures.
#
# The first argument is the .config specification file, which contains
# desired settings, for example, "CONFIG_NO_HZ=y".  For best results,
# this should be a full pathname.
#
# The second argument is a optional path to a build output directory,
# for example, "O=/tmp/foo".  If this argument is omitted, the .config
# file will be generated directly in the current directory.
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

T=${TMPDIR-/tmp}/configinit.sh.$$
trap 'rm -rf $T' 0
mkdir $T

# Capture config spec file.

c=$1
buildloc=$2
builddir=
if test -n $buildloc
then
	if echo $buildloc | grep -q '^O='
	then
		builddir=`echo $buildloc | sed -e 's/^O=//'`
		if test ! -d $builddir
		then
			mkdir $builddir
		fi
	else
		echo Bad build directory: \"$builddir\"
		exit 2
	fi
fi

sed -e 's/^\(CONFIG[0-9A-Z_]*\)=.*$/grep -v "^# \1" |/' < $c > $T/u.sh
sed -e 's/^\(CONFIG[0-9A-Z_]*=\).*$/grep -v \1 |/' < $c >> $T/u.sh
grep '^grep' < $T/u.sh > $T/upd.sh
echo "cat - $c" >> $T/upd.sh
make mrproper
make $buildloc distclean > $builddir/Make.distclean 2>&1
make $buildloc $TORTURE_DEFCONFIG > $builddir/Make.defconfig.out 2>&1
mv $builddir/.config $builddir/.config.sav
sh $T/upd.sh < $builddir/.config.sav > $builddir/.config
cp $builddir/.config $builddir/.config.new
yes '' | make $buildloc oldconfig > $builddir/Make.oldconfig.out 2> $builddir/Make.oldconfig.err

# verify new config matches specification.
configcheck.sh $builddir/.config $c

exit 0
