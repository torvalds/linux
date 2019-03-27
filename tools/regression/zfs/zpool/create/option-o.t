#!/bin/sh
# $FreeBSD$

dir=`dirname $0`
. ${dir}/../../misc.sh

echo "1..44"

disks_create 1
names_create 2

expect_fl ${ZPOOL} create -o size=96M ${name0} ${disk0}
expect_fl ${ZPOOL} destroy ${name0}

expect_fl ${ZPOOL} create -o used=0 ${name0} ${disk0}
expect_fl ${ZPOOL} destroy ${name0}

expect_fl ${ZPOOL} create -o available=96M ${name0} ${disk0}
expect_fl ${ZPOOL} destroy ${name0}

expect_fl ${ZPOOL} create -o capacity=0% ${name0} ${disk0}
expect_fl ${ZPOOL} destroy ${name0}

expect_fl is_mountpoint /${name0}
expect_fl is_mountpoint /${name1}
expect_ok ${ZPOOL} create -o altroot=/${name1} ${name0} ${disk0}
exp=`(
  echo "NAME      PROPERTY  VALUE      SOURCE"
  echo "${name0}  altroot   /${name1}  local"
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

expect_fl ${ZPOOL} create -o health=ONLINE ${name0} ${disk0}
expect_fl ${ZPOOL} destroy ${name0}

expect_fl ${ZPOOL} create -o guid=13949667482126165574 ${name0} ${disk0}
expect_fl ${ZPOOL} destroy ${name0}

expect_ok ${ZPOOL} create -o version=9 ${name0} ${disk0}
exp=`(
  echo "NAME      PROPERTY  VALUE  SOURCE"
  echo "${name0}  version   9      local"
)`
expect "${exp}" ${ZPOOL} get version ${name0}
expect_ok ${ZPOOL} destroy ${name0}

expect_fl ${ZPOOL} create -o bootfs=${name0}/root ${name0} ${disk0}
expect_fl ${ZPOOL} destroy ${name0}

expect_ok ${ZPOOL} create -o delegation=off ${name0} ${disk0}
exp=`(
  echo "NAME      PROPERTY    VALUE  SOURCE"
  echo "${name0}  delegation  off    local"
)`
expect "${exp}" ${ZPOOL} get delegation ${name0}
expect_ok ${ZPOOL} destroy ${name0}

expect_ok ${ZPOOL} create -o autoreplace=on ${name0} ${disk0}
exp=`(
  echo "NAME      PROPERTY     VALUE  SOURCE"
  echo "${name0}  autoreplace  on     local"
)`
expect "${exp}" ${ZPOOL} get autoreplace ${name0}
expect_ok ${ZPOOL} destroy ${name0}

expect_ok ${ZPOOL} create -o cachefile=none ${name0} ${disk0}
exp=`(
  echo "NAME      PROPERTY   VALUE  SOURCE"
  echo "${name0}  cachefile  none   local"
)`
expect "${exp}" ${ZPOOL} get cachefile ${name0}
expect_ok ${ZPOOL} destroy ${name0}

expect_ok ${ZPOOL} create -o cachefile=/tmp/${name1} ${name0} ${disk0}
exp=`(
  echo "NAME      PROPERTY   VALUE          SOURCE"
  echo "${name0}  cachefile  /tmp/${name1}  local"
)`
expect "${exp}" ${ZPOOL} get cachefile ${name0}
expect_ok ${ZPOOL} destroy ${name0}

expect_ok ${ZPOOL} create -o failmode=continue ${name0} ${disk0}
exp=`(
  echo "NAME      PROPERTY  VALUE     SOURCE"
  echo "${name0}  failmode  continue  local"
)`
expect "${exp}" ${ZPOOL} get failmode ${name0}
expect_ok ${ZPOOL} destroy ${name0}

expect_ok ${ZPOOL} create -o failmode=panic ${name0} ${disk0}
exp=`(
  echo "NAME      PROPERTY  VALUE  SOURCE"
  echo "${name0}  failmode  panic  local"
)`
expect "${exp}" ${ZPOOL} get failmode ${name0}
expect_ok ${ZPOOL} destroy ${name0}

disks_destroy
