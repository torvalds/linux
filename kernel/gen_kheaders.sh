#!/bin/sh
# SPDX-License-Identifier: GPL-2.0

# This script generates an archive consisting of kernel headers
# for CONFIG_IKHEADERS.
set -e
sfile="$(readlink -f "$0")"
outdir="$(pwd)"
tarfile=$1
tmpdir=$outdir/${tarfile%/*}/.tmp_dir

dir_list="
include/
arch/$SRCARCH/include/
"

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
headers_md5="$(find $all_dirs -name "*.h" -a			\
		! -path include/generated/utsversion.h -a	\
		! -path include/generated/autoconf.h		|
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

rm -rf "${tmpdir}"
mkdir "${tmpdir}"

if [ "$building_out_of_srctree" ]; then
	(
		cd $srctree
		for f in $dir_list
			do find "$f" -name "*.h";
		done | tar -c -f - -T - | tar -xf - -C "${tmpdir}"
	)
fi

for f in $dir_list;
	do find "$f" -name "*.h";
done | tar -c -f - -T - | tar -xf - -C "${tmpdir}"

# Always exclude include/generated/utsversion.h
# Otherwise, the contents of the tarball may vary depending on the build steps.
rm -f "${tmpdir}/include/generated/utsversion.h"

# Remove comments except SDPX lines
# Use a temporary file to store directory contents to prevent find/xargs from
# seeing temporary files created by perl.
find "${tmpdir}" -type f -print0 > "${tmpdir}.contents.txt"
xargs -0 -P8 -n1 \
	perl -pi -e 'BEGIN {undef $/;}; s/\/\*((?!SPDX).)*?\*\///smg;' \
	< "${tmpdir}.contents.txt"
rm -f "${tmpdir}.contents.txt"

# Create archive and try to normalize metadata for reproducibility.
tar "${KBUILD_BUILD_TIMESTAMP:+--mtime=$KBUILD_BUILD_TIMESTAMP}" \
    --exclude=".__afs*" --exclude=".nfs*" \
    --owner=0 --group=0 --sort=name --numeric-owner --mode=u=rw,go=r,a+X \
    -I $XZ -cf $tarfile -C "${tmpdir}/" . > /dev/null

echo $headers_md5 > kernel/kheaders.md5
echo "$this_file_md5" >> kernel/kheaders.md5
echo "$(md5sum $tarfile | cut -d ' ' -f1)" >> kernel/kheaders.md5

rm -rf "${tmpdir}"
