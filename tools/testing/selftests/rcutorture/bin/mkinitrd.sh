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
if [ -s "$D/initrd/init" ]; then
    echo "$D/initrd/init already exists, no need to create it"
    exit 0
fi

T=${TMPDIR-/tmp}/mkinitrd.sh.$$
trap 'rm -rf $T' 0 2
mkdir $T

cat > $T/init << '__EOF___'
#!/bin/sh
# Run in userspace a few milliseconds every second.  This helps to
# exercise the NO_HZ_FULL portions of RCU.
while :
do
	q=
	for i in \
		a a a a a a a a a a a a a a a a a a a a a a a a a a a a a a a \
		a a a a a a a a a a a a a a a a a a a a a a a a a a a a a a a \
		a a a a a a a a a a a a a a a a a a a a a a a a a a a a a a a \
		a a a a a a a a a a a a a a a a a a a a a a a a a a a a a a a \
		a a a a a a a a a a a a a a a a a a a a a a a a a a a a a a a \
		a a a a a a a a a a a a a a a a a a a a a a a a a a a a a a a
	do
		q="$q $i"
	done
	sleep 1
done
__EOF___

# Try using dracut to create initrd
if command -v dracut >/dev/null 2>&1
then
	echo Creating $D/initrd using dracut.
	# Filesystem creation
	dracut --force --no-hostonly --no-hostonly-cmdline --module "base" $T/initramfs.img
	cd $D
	mkdir -p initrd
	cd initrd
	zcat $T/initramfs.img | cpio -id
	cp $T/init init
	chmod +x init
	echo Done creating $D/initrd using dracut
	exit 0
fi

# No dracut, so create a C-language initrd/init program and statically
# link it.  This results in a very small initrd, but might be a bit less
# future-proof than dracut.
echo "Could not find dracut, attempting C initrd"
cd $D
mkdir -p initrd
cd initrd
cat > init.c << '___EOF___'
#ifndef NOLIBC
#include <unistd.h>
#include <sys/time.h>
#endif

volatile unsigned long delaycount;

int main(int argc, int argv[])
{
	int i;
	struct timeval tv;
	struct timeval tvb;

	for (;;) {
		sleep(1);
		/* Need some userspace time. */
		if (gettimeofday(&tvb, NULL))
			continue;
		do {
			for (i = 0; i < 1000 * 100; i++)
				delaycount = i * i;
			if (gettimeofday(&tv, NULL))
				break;
			tv.tv_sec -= tvb.tv_sec;
			if (tv.tv_sec > 1)
				break;
			tv.tv_usec += tv.tv_sec * 1000 * 1000;
			tv.tv_usec -= tvb.tv_usec;
		} while (tv.tv_usec < 1000);
	}
	return 0;
}
___EOF___

# build using nolibc on supported archs (smaller executable) and fall
# back to regular glibc on other ones.
if echo -e "#if __x86_64__||__i386__||__i486__||__i586__||__i686__" \
           "||__ARM_EABI__||__aarch64__\nyes\n#endif" \
   | ${CROSS_COMPILE}gcc -E -nostdlib -xc - \
   | grep -q '^yes'; then
	# architecture supported by nolibc
        ${CROSS_COMPILE}gcc -fno-asynchronous-unwind-tables -fno-ident \
		-nostdlib -include ../bin/nolibc.h -lgcc -s -static -Os \
		-o init init.c
else
	${CROSS_COMPILE}gcc -s -static -Os -o init init.c
fi

rm init.c
echo "Done creating a statically linked C-language initrd"

exit 0
