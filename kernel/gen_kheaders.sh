#!/bin/sh
# SPDX-License-Identifier: GPL-2.0

# This script generates an archive consisting of kernel headers
# for CONFIG_IKHEADERS.
set -e
sfile="$(readlink -f "$0")"
outdir="$(pwd)"
tarfile=$1
cpio_dir=$outdir/$tarfile.tmp

dir_list="
include/
arch/$SRCARCH/include/
"

type cpio > /dev/null

# Support incremental builds by skipping archive generation
# if timestamps of files being archived are not changed.

# This block is useful for debugging the incremental builds.
# Uncomment it for debugging.
# if [ ! -f /tmp/iter ]; then iter=1; echo 1 > /tmp/iter;
# else iter=$(($(cat /tmp/iter) + 1)); echo $iter > /tmp/iter; fi
# find $all_dirs -name "*.h" | xargs ls -l > /tmp/ls-$iter

all_dirs=
if [ "$building_out_of_srctree" ]; then
	for d in $dir_list; do
		all_dirs="$all_dirs $srctree/$d"
	done
fi
all_dirs="$all_dirs $dir_list"

# include/generated/utsversion.h is ignored because it is generated after this
# script is executed. (utsversion.h is unneeded for kheaders)
#
# When Kconfig regenerates include/generated/autoconf.h, its timestamp is
# updated, but the contents might be still the same. When any CONFIG option is
# changed, Kconfig touches the corresponding timestamp file include/config/*.
# Hence, the md5sum detects the configuration change anyway. We do not need to
# check include/generated/autoconf.h explicitly.
#
# Ignore them for md5 calculation to avoid pointless regeneration.
headers_md5="$(find $all_dirs -name "*.h"			|
		grep -v "include/generated/utsversion.h"	|
		grep -v "include/generated/autoconf.h"	|
		xargs ls -l | md5sum | cut -d ' ' -f1)"

# Any changes to this script will also cause a rebuild of the archive.
this_file_md5="$(ls -l $sfile | md5sum | cut -d ' ' -f1)"
if [ -f $tarfile ]; then tarfile_md5="$(md5sum $tarfile | cut -d ' ' -f1)"; fi
if [ -f kernel/kheaders.md5 ] &&
	[ "$(head -n 1 kernel/kheaders.md5)" = "$headers_md5" ] &&
	[ "$(head -n 2 kernel/kheaders.md5 | tail -n 1)" = "$this_file_md5" ] &&
	[ "$(tail -n 1 kernel/kheaders.md5)" = "$tarfile_md5" ]; then
		exit
fi

echo "  GEN     $tarfile"

rm -rf $cpio_dir
mkdir $cpio_dir

if [ "$building_out_of_srctree" ]; then
	(
		cd $srctree
		for f in $dir_list
			do find "$f" -name "*.h";
		done | cpio --quiet -pd $cpio_dir
	)
fi

# The second CPIO can complain if files already exist which can happen with out
# of tree builds having stale headers in srctree. Just silence CPIO for now.
for f in $dir_list;
	do find "$f" -name "*.h";
done | cpio --quiet -pdu $cpio_dir >/dev/null 2>&1

# Remove comments except SDPX lines
find $cpio_dir -type f -print0 |
	xargs -0 -P8 -n1 perl -pi -e 'BEGIN {undef $/;}; s/\/\*((?!SPDX).)*?\*\///smg;'

# Create archive and try to normalize metadata for reproducibility.
# For compatibility with older versions of tar, files are fed to tar
# pre-sorted, as --sort=name might not be available.
find $cpio_dir -printf "./%P\n" | LC_ALL=C sort | \
    tar "${KBUILD_BUILD_TIMESTAMP:+--mtime=$KBUILD_BUILD_TIMESTAMP}" \
    --owner=0 --group=0 --numeric-owner --no-recursion \
    -I $XZ -cf $tarfile -C $cpio_dir/ -T - > /dev/null

echo $headers_md5 > kernel/kheaders.md5
echo "$this_file_md5" >> kernel/kheaders.md5
echo "$(md5sum $tarfile | cut -d ' ' -f1)" >> kernel/kheaders.md5

rm -rf $cpio_dir
