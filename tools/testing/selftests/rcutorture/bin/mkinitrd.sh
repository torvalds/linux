#!/bin/bash
#
# Create an initrd directory if one does not already exist.
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
# Author: Connor Shu <Connor.Shu@ibm.com>

D=tools/testing/selftests/rcutorture

# Prerequisite checks
[ -z "$D" ] && echo >&2 "No argument supplied" && exit 1
if [ ! -d "$D" ]; then
    echo >&2 "$D does not exist: Malformed kernel source tree?"
    exit 1
fi
if [ -d "$D/initrd" ]; then
    echo "$D/initrd already exists, no need to create it"
    exit 0
fi

T=${TMPDIR-/tmp}/mkinitrd.sh.$$
trap 'rm -rf $T' 0 2
mkdir $T

cat > $T/init << '__EOF___'
#!/bin/sh
while :
do
	sleep 1000000
done
__EOF___

# Try using dracut to create initrd
command -v dracut >/dev/null 2>&1 || { echo >&2 "Dracut not installed"; exit 1; }
echo Creating $D/initrd using dracut.

# Filesystem creation
dracut --force --no-hostonly --no-hostonly-cmdline --module "base" $T/initramfs.img
cd $D
mkdir initrd
cd initrd
zcat $T/initramfs.img | cpio -id
cp $T/init init
echo Done creating $D/initrd using dracut
exit 0
