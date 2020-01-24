#!/bin/bash
# SPDX-License-Identifier: GPL-2.0+
#
# Usage: configinit.sh config-spec-file results-dir
#
# Create a .config file from the spec file.  Run from the kernel source tree.
# Exits with 0 if all went well, with 1 if all went well but the config
# did not match, and some other number for other failures.
#
# The first argument is the .config specification file, which contains
# desired settings, for example, "CONFIG_NO_HZ=y".  For best results,
# this should be a full pathname.
#
# Copyright (C) IBM Corporation, 2013
#
# Authors: Paul E. McKenney <paulmck@linux.ibm.com>

T=${TMPDIR-/tmp}/configinit.sh.$$
trap 'rm -rf $T' 0
mkdir $T

# Capture config spec file.

c=$1
resdir=$2

sed -e 's/^\(CONFIG[0-9A-Z_]*\)=.*$/grep -v "^# \1" |/' < $c > $T/u.sh
sed -e 's/^\(CONFIG[0-9A-Z_]*=\).*$/grep -v \1 |/' < $c >> $T/u.sh
grep '^grep' < $T/u.sh > $T/upd.sh
echo "cat - $c" >> $T/upd.sh
if test -z "$TORTURE_TRUST_MAKE"
then
	make clean > $resdir/Make.clean 2>&1
fi
make $TORTURE_DEFCONFIG > $resdir/Make.defconfig.out 2>&1
mv .config .config.sav
sh $T/upd.sh < .config.sav > .config
cp .config .config.new
yes '' | make oldconfig > $resdir/Make.oldconfig.out 2> $resdir/Make.oldconfig.err

# verify new config matches specification.
configcheck.sh .config $c

exit 0
