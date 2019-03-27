#!/bin/sh
# $FreeBSD$

dir=`dirname $0`
. ${dir}/../../misc.sh

echo "1..28"

disks_create 1
names_create 2

expect_fl is_mountpoint /${name0}
expect_fl is_mountpoint /${name1}
expect_ok ${ZPOOL} create -m /${name1} ${name0} ${disk0}
exp=`(
  echo "NAME      PROPERTY  VALUE  SOURCE"
  echo "${name0}  altroot   -      default"
)`
expect "${exp}" ${ZPOOL} get altroot ${name0}
expect_fl is_mountpoint /${name0}
if [ -z "${no_mountpoint}" ]; then
	expect_ok is_mountpoint /${name1}
else
	expect_fl is_mountpoint /${name1}
fi
expect_ok ${ZPOOL} destroy ${name0}
expect_fl is_mountpoint /${name0}
expect_fl is_mountpoint /${name1}
expect_ok rmdir /${name1}

expect_ok mkdir /${name1}
expect_ok ${ZPOOL} create -m legacy ${name0} ${disk0}
expect_fl is_mountpoint /${name0}
expect_ok mount ${mount_t_flag} zfs ${name0} /${name1}
if [ -z "${no_mountpoint}" ]; then
	expect_ok is_mountpoint /${name1}
else
	expect_fl is_mountpoint /${name1}
fi
expect_ok umount /${name1}
expect_fl is_mountpoint /${name1}
expect_ok ${ZPOOL} destroy ${name0}
expect_ok rmdir /${name1}

expect_ok mkdir /${name1}
expect_ok ${ZPOOL} create -m none ${name0} ${disk0}
expect_fl is_mountpoint /${name0}
expect_ok mount ${mount_t_flag} zfs ${name0} /${name1}
if [ -z "${no_mountpoint}" ]; then
	expect_ok is_mountpoint /${name1}
else
	expect_fl is_mountpoint /${name1}
fi
expect_ok umount /${name1}
expect_fl is_mountpoint /${name1}
expect_ok ${ZPOOL} destroy ${name0}
expect_ok rmdir /${name1}

disks_destroy
