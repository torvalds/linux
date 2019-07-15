#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# This script generates an archive consisting of kernel headers
# for CONFIG_IKHEADERS.
set -e
spath="$(dirname "$(readlink -f "$0")")"
kroot="$spath/.."
outdir="$(pwd)"
tarfile=$1
cpio_dir=$outdir/$tarfile.tmp

# Script filename relative to the kernel source root
# We add it to the archive because it is small and any changes
# to this script will also cause a rebuild of the archive.
sfile="$(realpath --relative-to $kroot "$(readlink -f "$0")")"

src_file_list="
include/
arch/$SRCARCH/include/
$sfile
"

obj_file_list="
include/
arch/$SRCARCH/include/
"

# Support incremental builds by skipping archive generation
# if timestamps of files being archived are not changed.

# This block is useful for debugging the incremental builds.
# Uncomment it for debugging.
# if [ ! -f /tmp/iter ]; then iter=1; echo 1 > /tmp/iter;
# else iter=$(($(cat /tmp/iter) + 1)); echo $iter > /tmp/iter; fi
# find $src_file_list -type f | xargs ls -lR > /tmp/src-ls-$iter
# find $obj_file_list -type f | xargs ls -lR > /tmp/obj-ls-$iter

# include/generated/compile.h is ignored because it is touched even when none
# of the source files changed. This causes pointless regeneration, so let us
# ignore them for md5 calculation.
pushd $kroot > /dev/null
src_files_md5="$(find $src_file_list -type f                       |
		grep -v "include/generated/compile.h"		   |
		grep -v "include/generated/autoconf.h"		   |
		grep -v "include/config/auto.conf"		   |
		grep -v "include/config/auto.conf.cmd"		   |
		grep -v "include/config/tristate.conf"		   |
		xargs ls -lR | md5sum | cut -d ' ' -f1)"
popd > /dev/null
obj_files_md5="$(find $obj_file_list -type f                       |
		grep -v "include/generated/compile.h"		   |
		grep -v "include/generated/autoconf.h"		   |
		grep -v "include/config/auto.conf"                 |
		grep -v "include/config/auto.conf.cmd"		   |
		grep -v "include/config/tristate.conf"		   |
		xargs ls -lR | md5sum | cut -d ' ' -f1)"

if [ -f $tarfile ]; then tarfile_md5="$(md5sum $tarfile | cut -d ' ' -f1)"; fi
if [ -f kernel/kheaders.md5 ] &&
	[ "$(cat kernel/kheaders.md5|head -1)" == "$src_files_md5" ] &&
	[ "$(cat kernel/kheaders.md5|head -2|tail -1)" == "$obj_files_md5" ] &&
	[ "$(cat kernel/kheaders.md5|tail -1)" == "$tarfile_md5" ]; then
		exit
fi

if [ "${quiet}" != "silent_" ]; then
       echo "  GEN     $tarfile"
fi

rm -rf $cpio_dir
mkdir $cpio_dir

pushd $kroot > /dev/null
for f in $src_file_list;
	do find "$f" ! -name "*.cmd" ! -name ".*";
done | cpio --quiet -pd $cpio_dir
popd > /dev/null

# The second CPIO can complain if files already exist which can
# happen with out of tree builds. Just silence CPIO for now.
for f in $obj_file_list;
	do find "$f" ! -name "*.cmd" ! -name ".*";
done | cpio --quiet -pd $cpio_dir >/dev/null 2>&1

# Remove comments except SDPX lines
find $cpio_dir -type f -print0 |
	xargs -0 -P8 -n1 perl -pi -e 'BEGIN {undef $/;}; s/\/\*((?!SPDX).)*?\*\///smg;'

tar -Jcf $tarfile -C $cpio_dir/ . > /dev/null

echo "$src_files_md5" >  kernel/kheaders.md5
echo "$obj_files_md5" >> kernel/kheaders.md5
echo "$(md5sum $tarfile | cut -d ' ' -f1)" >> kernel/kheaders.md5

rm -rf $cpio_dir
