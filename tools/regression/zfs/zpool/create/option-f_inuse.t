#!/bin/sh
# $FreeBSD$

dir=`dirname $0`
. ${dir}/../../misc.sh

echo "1..146"

disks_create 7
names_create 2

expect_ok ${ZPOOL} create ${name0} ${disk0}
expect_ok ${ZPOOL} export ${name0}
exp=`(
  echo "invalid vdev specification"
  echo "use '-f' to override the following errors:"
  echo "${fdisk0} is part of exported pool '${name0}'"
)`
expect "${exp}" ${ZPOOL} create ${name1} ${disk0}
expect_fl ${ZPOOL} status -x ${name1}
expect_ok ${ZPOOL} create -f ${name1} ${disk0}
expect_ok ${ZPOOL} status -x ${name1}
expect_ok ${ZPOOL} destroy ${name1}

expect_ok ${ZPOOL} create ${name0} mirror ${disk0} ${disk1}
expect_ok ${ZPOOL} export ${name0}
exp=`(
  echo "invalid vdev specification"
  echo "use '-f' to override the following errors:"
  echo "${fdisk0} is part of exported pool '${name0}'"
)`
expect "${exp}" ${ZPOOL} create ${name1} mirror ${disk0} ${disk1}
expect_fl ${ZPOOL} status -x ${name1}
expect_ok ${ZPOOL} create -f ${name1} mirror ${disk0} ${disk1}
expect_ok ${ZPOOL} status -x ${name1}
expect_ok ${ZPOOL} destroy ${name1}

expect_ok ${ZPOOL} create ${name0} raidz1 ${disk0} ${disk1} ${disk2}
expect_ok ${ZPOOL} export ${name0}
exp=`(
  echo "invalid vdev specification"
  echo "use '-f' to override the following errors:"
  echo "${fdisk0} is part of exported pool '${name0}'"
)`
expect "${exp}" ${ZPOOL} create ${name1} raidz1 ${disk0} ${disk1} ${disk2}
expect_fl ${ZPOOL} status -x ${name1}
expect_ok ${ZPOOL} create -f ${name1} raidz1 ${disk0} ${disk1} ${disk2}
expect_ok ${ZPOOL} status -x ${name1}
expect_ok ${ZPOOL} destroy ${name1}

expect_ok ${ZPOOL} create ${name0} raidz2 ${disk0} ${disk1} ${disk2} ${disk3}
expect_ok ${ZPOOL} export ${name0}
exp=`(
  echo "invalid vdev specification"
  echo "use '-f' to override the following errors:"
  echo "${fdisk0} is part of exported pool '${name0}'"
)`
expect "${exp}" ${ZPOOL} create ${name1} raidz2 ${disk0} ${disk1} ${disk2} ${disk3}
expect_fl ${ZPOOL} status -x ${name1}
expect_ok ${ZPOOL} create -f ${name1} raidz2 ${disk0} ${disk1} ${disk2} ${disk3}
expect_ok ${ZPOOL} status -x ${name1}
expect_ok ${ZPOOL} destroy ${name1}

expect_ok ${ZPOOL} create ${name0} ${disk0} log ${disk1}
expect_ok ${ZPOOL} export ${name0}
exp=`(
  echo "invalid vdev specification"
  echo "use '-f' to override the following errors:"
  echo "${fdisk0} is part of exported pool '${name0}'"
)`
expect "${exp}" ${ZPOOL} create ${name1} ${disk0} log ${disk1}
expect_fl ${ZPOOL} status -x ${name1}
expect_ok ${ZPOOL} create -f ${name1} ${disk0} log ${disk1}
expect_ok ${ZPOOL} status -x ${name1}
expect_ok ${ZPOOL} destroy ${name1}

expect_ok ${ZPOOL} create ${name0} ${disk0} log mirror ${disk1} ${disk2}
expect_ok ${ZPOOL} export ${name0}
exp=`(
  echo "invalid vdev specification"
  echo "use '-f' to override the following errors:"
  echo "${fdisk0} is part of exported pool '${name0}'"
)`
expect "${exp}" ${ZPOOL} create ${name1} ${disk0} log mirror ${disk1} ${disk2}
expect_fl ${ZPOOL} status -x ${name1}
expect_ok ${ZPOOL} create -f ${name1} ${disk0} log mirror ${disk1} ${disk2}
expect_ok ${ZPOOL} status -x ${name1}
expect_ok ${ZPOOL} destroy ${name1}

expect_ok ${ZPOOL} create ${name0} ${disk0} cache ${disk1}
expect_ok ${ZPOOL} export ${name0}
exp=`(
  echo "invalid vdev specification"
  echo "use '-f' to override the following errors:"
  echo "${fdisk0} is part of exported pool '${name0}'"
)`
expect "${exp}" ${ZPOOL} create ${name1} ${disk0} cache ${disk1}
expect_fl ${ZPOOL} status -x ${name1}
expect_ok ${ZPOOL} create -f ${name1} ${disk0} cache ${disk1}
expect_ok ${ZPOOL} status -x ${name1}
expect_ok ${ZPOOL} destroy ${name1}

expect_ok ${ZPOOL} create ${name0} mirror ${disk0} ${disk1}
expect_ok ${ZPOOL} offline ${name0} ${disk1}
expect_ok ${ZPOOL} export ${name0}
exp=`(
  echo "invalid vdev specification"
  echo "use '-f' to override the following errors:"
  echo "${fdisk1} is part of potentially active pool '${name0}'"
)`
expect "${exp}" ${ZPOOL} create ${name1} mirror ${disk1} ${disk2}
expect_fl ${ZPOOL} status -x ${name1}
expect_ok ${ZPOOL} create -f ${name1} mirror ${disk1} ${disk2}
expect_ok ${ZPOOL} status -x ${name1}
expect_ok ${ZPOOL} destroy ${name1}
expect_ok ${ZPOOL} import ${name0}
expect_ok ${ZPOOL} destroy ${name0}

expect_ok ${ZPOOL} create ${name0} raidz1 ${disk0} ${disk1} ${disk2}
expect_ok ${ZPOOL} offline ${name0} ${disk2}
expect_ok ${ZPOOL} export ${name0}
exp=`(
  echo "invalid vdev specification"
  echo "use '-f' to override the following errors:"
  echo "${fdisk2} is part of potentially active pool '${name0}'"
)`
expect "${exp}" ${ZPOOL} create ${name1} raidz1 ${disk2} ${disk3} ${disk4}
expect_fl ${ZPOOL} status -x ${name1}
expect_ok ${ZPOOL} create -f ${name1} raidz1 ${disk2} ${disk3} ${disk4}
expect_ok ${ZPOOL} status -x ${name1}
expect_ok ${ZPOOL} destroy ${name1}
expect_ok ${ZPOOL} import ${name0}
expect_ok ${ZPOOL} destroy ${name0}

expect_ok ${ZPOOL} create ${name0} raidz2 ${disk0} ${disk1} ${disk2} ${disk3}
expect_ok ${ZPOOL} offline ${name0} ${disk3}
expect_ok ${ZPOOL} export ${name0}
exp=`(
  echo "invalid vdev specification"
  echo "use '-f' to override the following errors:"
  echo "${fdisk3} is part of potentially active pool '${name0}'"
)`
expect "${exp}" ${ZPOOL} create ${name1} raidz2 ${disk3} ${disk4} ${disk5} ${disk6}
expect_fl ${ZPOOL} status -x ${name1}
expect_ok ${ZPOOL} create -f ${name1} raidz2 ${disk3} ${disk4} ${disk5} ${disk6}
expect_ok ${ZPOOL} status -x ${name1}
expect_ok ${ZPOOL} destroy ${name1}
expect_ok ${ZPOOL} import ${name0}
expect_ok ${ZPOOL} destroy ${name0}

expect_ok ${ZPOOL} create ${name0} ${disk0} log mirror ${disk1} ${disk2}
expect_ok ${ZPOOL} offline ${name0} ${disk2}
expect_ok ${ZPOOL} export ${name0}
exp=`(
  echo "invalid vdev specification"
  echo "use '-f' to override the following errors:"
  echo "${fdisk2} is part of potentially active pool '${name0}'"
)`
expect "${exp}" ${ZPOOL} create ${name1} ${disk3} log mirror ${disk2} ${disk4}
expect_fl ${ZPOOL} status -x ${name1}
expect_ok ${ZPOOL} create -f ${name1} ${disk3} log mirror ${disk2} ${disk4}
expect_ok ${ZPOOL} status -x ${name1}
expect_ok ${ZPOOL} destroy ${name1}
expect_ok ${ZPOOL} import ${name0}
expect_ok ${ZPOOL} destroy ${name0}

expect_ok ${ZPOOL} create ${name0} ${disk0} cache ${disk1}
expect_ok ${ZPOOL} offline ${name0} ${disk1}
expect_ok ${ZPOOL} export ${name0}
exp=`(
  echo "invalid vdev specification"
  echo "use '-f' to override the following errors:"
  echo "${fdisk1} is part of potentially active pool '${name0}'"
)`
add_msg="# TODO It shouldn't be possible to use offlined cache vdev."
expect "${exp}" ${ZPOOL} create ${name1} ${disk2} cache ${disk1}
expect_fl ${ZPOOL} status -x ${name1}
expect_ok ${ZPOOL} create -f ${name1} ${disk2} cache ${disk1}
add_msg=""
expect_ok ${ZPOOL} status -x ${name1}
expect_ok ${ZPOOL} destroy ${name1}
expect_ok ${ZPOOL} import ${name0}
expect_ok ${ZPOOL} destroy ${name0}

expect_ok ${ZPOOL} create ${name0} mirror ${disk0} ${disk1}
expect_ok ${ZPOOL} offline ${name0} ${disk1}
exp=`(
  echo "invalid vdev specification"
  echo "use '-f' to override the following errors:"
  echo "${fdisk1} is part of active pool '${name0}'"
)`
expect "${exp}" ${ZPOOL} create ${name1} mirror ${disk1} ${disk2}
expect_fl ${ZPOOL} status -x ${name1}
exp=`(
  echo "invalid vdev specification"
  echo "the following errors must be manually repaired:"
  echo "${fdisk1} is part of active pool '${name0}'"
)`
expect "${exp}" ${ZPOOL} create -f ${name1} mirror ${disk1} ${disk2}
expect_fl ${ZPOOL} status -x ${name1}
expect_ok ${ZPOOL} online ${name0} ${disk1}
expect_ok ${ZPOOL} destroy ${name0}

expect_ok ${ZPOOL} create ${name0} raidz1 ${disk0} ${disk1} ${disk2}
expect_ok ${ZPOOL} offline ${name0} ${disk2}
exp=`(
  echo "invalid vdev specification"
  echo "use '-f' to override the following errors:"
  echo "${fdisk2} is part of active pool '${name0}'"
)`
expect "${exp}" ${ZPOOL} create ${name1} raidz1 ${disk2} ${disk3} ${disk4}
expect_fl ${ZPOOL} status -x ${name1}
exp=`(
  echo "invalid vdev specification"
  echo "the following errors must be manually repaired:"
  echo "${fdisk2} is part of active pool '${name0}'"
)`
expect "${exp}" ${ZPOOL} create -f ${name1} raidz1 ${disk2} ${disk3} ${disk4}
expect_fl ${ZPOOL} status -x ${name1}
expect_ok ${ZPOOL} online ${name0} ${disk2}
expect_ok ${ZPOOL} destroy ${name0}

expect_ok ${ZPOOL} create ${name0} raidz2 ${disk0} ${disk1} ${disk2} ${disk3}
expect_ok ${ZPOOL} offline ${name0} ${disk3}
exp=`(
  echo "invalid vdev specification"
  echo "use '-f' to override the following errors:"
  echo "${fdisk3} is part of active pool '${name0}'"
)`
expect "${exp}" ${ZPOOL} create ${name1} raidz2 ${disk3} ${disk4} ${disk5} ${disk6}
expect_fl ${ZPOOL} status -x ${name1}
exp=`(
  echo "invalid vdev specification"
  echo "the following errors must be manually repaired:"
  echo "${fdisk3} is part of active pool '${name0}'"
)`
expect "${exp}" ${ZPOOL} create -f ${name1} raidz2 ${disk3} ${disk4} ${disk5} ${disk6}
expect_fl ${ZPOOL} status -x ${name1}
expect_ok ${ZPOOL} online ${name0} ${disk3}
expect_ok ${ZPOOL} destroy ${name0}

expect_ok ${ZPOOL} create ${name0} ${disk0} log mirror ${disk1} ${disk2}
expect_ok ${ZPOOL} offline ${name0} ${disk2}
exp=`(
  echo "invalid vdev specification"
  echo "use '-f' to override the following errors:"
  echo "${fdisk2} is part of active pool '${name0}'"
)`
expect "${exp}" ${ZPOOL} create ${name1} ${disk3} log mirror ${disk2} ${disk4}
expect_fl ${ZPOOL} status -x ${name1}
exp=`(
  echo "invalid vdev specification"
  echo "the following errors must be manually repaired:"
  echo "${fdisk2} is part of active pool '${name0}'"
)`
expect "${exp}" ${ZPOOL} create -f ${name1} ${disk3} log mirror ${disk2} ${disk4}
expect_fl ${ZPOOL} status -x ${name1}
expect_ok ${ZPOOL} online ${name0} ${disk2}
expect_ok ${ZPOOL} destroy ${name0}

expect_ok ${ZPOOL} create ${name0} ${disk0} cache ${disk1}
expect_ok ${ZPOOL} offline ${name0} ${disk1}
exp=`(
  echo "invalid vdev specification"
  echo "use '-f' to override the following errors:"
  echo "${fdisk1} is part of active pool '${name0}'"
)`
add_msg="# TODO It reports that ${fdisk1} is part of unknown pool."
expect "${exp}" ${ZPOOL} create ${name1} ${disk2} cache ${disk1}
add_msg=""
expect_fl ${ZPOOL} status -x ${name1}
exp=`(
  echo "invalid vdev specification"
  echo "the following errors must be manually repaired:"
  echo "${fdisk1} is part of active pool '${name0}'"
)`
add_msg="# TODO It reports that ${fdisk1} is used twice."
expect "${exp}" ${ZPOOL} create -f ${name1} ${disk2} cache ${disk1}
add_msg=""
expect_fl ${ZPOOL} status -x ${name1}
expect_ok ${ZPOOL} online ${name0} ${disk1}
expect_ok ${ZPOOL} destroy ${name0}

expect_ok ${ZPOOL} create ${name0} ${disk0}
expect_ok ${ZPOOL} export ${name0}
exp=`(
  echo "invalid vdev specification"
  echo "use '-f' to override the following errors:"
  echo "${fdisk0} is part of exported pool '${name0}'"
)`
expect "${exp}" ${ZPOOL} create ${name0} ${disk0}
expect_fl ${ZPOOL} status -x ${name0}
expect_ok ${ZPOOL} create -f ${name0} ${disk0}
expect_ok ${ZPOOL} status -x ${name0}
expect_ok ${ZPOOL} destroy ${name0}

disks_destroy
