#!/bin/sh
# SPDX-License-Identifier: GPL-2.0

# This script generates an archive consisting of kernel headers
# for CONFIG_IKHEADERS.
set -e
tarfile=$1
srclist=$2
objlist=$3
timestamp=$4

dir=$(dirname "${tarfile}")
tmpdir=${dir}/.tmp_dir
depfile=${dir}/.$(basename "${tarfile}").d

# generate dependency list.
{
	echo
	echo "deps_${tarfile} := \\"
	sed 's:\(.*\):  \1 \\:' "${srclist}"
	sed -n '/^include\/generated\/autoconf\.h$/!s:\(.*\):  \1 \\:p' "${objlist}"
	echo
	echo "${tarfile}: \$(deps_${tarfile})"
	echo
	echo "\$(deps_${tarfile}):"

} > "${depfile}"

rm -rf "${tmpdir}"
mkdir "${tmpdir}"

# shellcheck disable=SC2154 # srctree is passed as an env variable
sed "s:^${srctree}/::" "${srclist}" | ${TAR} -c -f - -C "${srctree}" -T - | ${TAR} -xf - -C "${tmpdir}"
${TAR} -c -f - -T "${objlist}" | ${TAR} -xf - -C "${tmpdir}"

# Remove comments except SDPX lines
# Use a temporary file to store directory contents to prevent find/xargs from
# seeing temporary files created by perl.
find "${tmpdir}" -type f -print0 > "${tmpdir}.contents.txt"
xargs -0 -P8 -n1 \
	perl -pi -e 'BEGIN {undef $/;}; s/\/\*((?!SPDX).)*?\*\///smg;' \
	< "${tmpdir}.contents.txt"
rm -f "${tmpdir}.contents.txt"

# Create archive and try to normalize metadata for reproducibility.
${TAR} "${timestamp:+--mtime=$timestamp}" \
    --owner=0 --group=0 --sort=name --numeric-owner --mode=u=rw,go=r,a+X \
    -I "${XZ}" -cf "${tarfile}" -C "${tmpdir}/" . > /dev/null

rm -rf "${tmpdir}"
