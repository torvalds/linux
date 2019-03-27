#!/bin/sh

# $FreeBSD$

scriptpath=$(realpath $0)
parent=$(dirname $scriptpath)
blockfiles=${parent}/blockfiles

version=$1
if [ -z "$version" ]; then
	echo "Must specify ZFS pool version"
	exit 1
fi

# In case we need to test feature enabling?
#avail_features=$(zpool upgrade -v | awk '/^[a-z]/ && !/^see the/ { print $1 }')

zpool_opts=""
# For v5000, the rest of the arguments are <feature>=<enabled|disabled>.
if [ "$version" = "5000" ]; then
	shift
	for feature in $*; do
		zpool_opts="$zpool_opts -o feature@${feature}"
	done
else
	zpool_opts="-o version=${version}"
fi

dir=$(pwd)
datfile=zfs-pool-v${version}.dat
dat=${dir}/${datfile}
poolname=v${version}-pool

rm -f ${dat} ${dat}.Z
set -e
set -x
dd if=/dev/zero of=${dat} bs=1M count=64
zpool create ${zpool_opts} ${poolname} ${dat}
zpool export ${poolname}
compress ${dat}
cp ${dat}.Z ${blockfiles}
ls -l ${blockfiles}/${datfile}.Z
