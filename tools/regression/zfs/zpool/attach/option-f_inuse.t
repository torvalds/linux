#!/bin/sh
# $FreeBSD$

dir=`dirname $0`
. ${dir}/../../misc.sh

echo "1..141"

disks_create 11
names_create 2

expect_ok ${ZPOOL} create ${name0} ${disk0}
expect_ok ${ZPOOL} export ${name0}
expect_ok ${ZPOOL} create ${name1} ${disk1}
exp=`(
  echo "invalid vdev specification"
  echo "use '-f' to override the following errors:"
  echo "${fdisk0} is part of exported pool '${name0}'"
)`
expect "${exp}" ${ZPOOL} attach ${name1} ${disk1} ${disk0}
exp=`(
  echo "  pool: ${name1}"
  echo " state: ONLINE"
  echo " scrub: none requested"
  echo "config:"
  echo "        NAME        STATE   READ WRITE CKSUM"
  echo "        ${name1}    ONLINE     0     0     0"
  echo "          ${disk1}  ONLINE     0     0     0"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name1}
expect_ok ${ZPOOL} attach -f ${name1} ${disk1} ${disk0}
wait_for_resilver ${name1}
exp=`(
  echo "  pool: ${name1}"
  echo " state: ONLINE"
  echo " scrub: resilver completed after [0-9]+h[0-9]+m with 0 errors on .*"
  echo "config:"
  echo "        NAME          STATE   READ WRITE CKSUM"
  echo "        ${name1}      ONLINE     0     0     0"
  echo "          mirror      ONLINE     0     0     0"
  echo "            ${disk1}  ONLINE     0     0     0(  [0-9.]+[A-Z] resilvered)?"
  echo "            ${disk0}  ONLINE     0     0     0(  [0-9.]+[A-Z] resilvered)?"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name1}
expect_ok ${ZPOOL} destroy ${name1}

expect_ok ${ZPOOL} create ${name0} ${disk0}
expect_ok ${ZPOOL} export ${name0}
expect_ok ${ZPOOL} create ${name1} mirror ${disk1} ${disk2}
exp=`(
  echo "invalid vdev specification"
  echo "use '-f' to override the following errors:"
  echo "${fdisk0} is part of exported pool '${name0}'"
)`
expect "${exp}" ${ZPOOL} attach ${name1} ${disk1} ${disk0}
exp=`(
  echo "  pool: ${name1}"
  echo " state: ONLINE"
  echo " scrub: none requested"
  echo "config:"
  echo "        NAME          STATE   READ WRITE CKSUM"
  echo "        ${name1}      ONLINE     0     0     0"
  echo "          mirror      ONLINE     0     0     0"
  echo "            ${disk1}  ONLINE     0     0     0"
  echo "            ${disk2}  ONLINE     0     0     0"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name1}
expect_ok ${ZPOOL} attach -f ${name1} ${disk1} ${disk0}
wait_for_resilver ${name1}
exp=`(
  echo "  pool: ${name1}"
  echo " state: ONLINE"
  echo " scrub: resilver completed after [0-9]+h[0-9]+m with 0 errors on .*"
  echo "config:"
  echo "        NAME          STATE   READ WRITE CKSUM"
  echo "        ${name1}      ONLINE     0     0     0"
  echo "          mirror      ONLINE     0     0     0"
  echo "            ${disk1}  ONLINE     0     0     0(  [0-9.]+[A-Z] resilvered)?"
  echo "            ${disk2}  ONLINE     0     0     0(  [0-9.]+[A-Z] resilvered)?"
  echo "            ${disk0}  ONLINE     0     0     0(  [0-9.]+[A-Z] resilvered)?"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name1}
expect_ok ${ZPOOL} destroy ${name1}

expect_ok ${ZPOOL} create ${name0} ${disk1} log mirror ${disk2} ${disk0}
expect_ok ${ZPOOL} export ${name0}
expect_ok ${ZPOOL} create ${name1} ${disk3} log ${disk4}
exp=`(
  echo "invalid vdev specification"
  echo "use '-f' to override the following errors:"
  echo "${fdisk0} is part of exported pool '${name0}'"
)`
expect "${exp}" ${ZPOOL} attach ${name1} ${disk4} ${disk0}
exp=`(
  echo "  pool: ${name1}"
  echo " state: ONLINE"
  echo " scrub: none requested"
  echo "config:"
  echo "        NAME        STATE   READ WRITE CKSUM"
  echo "        ${name1}    ONLINE     0     0     0"
  echo "          ${disk3}  ONLINE     0     0     0"
  echo "        logs        ONLINE     0     0     0"
  echo "          ${disk4}  ONLINE     0     0     0"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name1}
expect_ok ${ZPOOL} attach -f ${name1} ${disk4} ${disk0}
wait_for_resilver ${name1}
exp=`(
  echo "  pool: ${name1}"
  echo " state: ONLINE"
  echo " scrub: resilver completed after [0-9]+h[0-9]+m with 0 errors on .*"
  echo "config:"
  echo "        NAME          STATE   READ WRITE CKSUM"
  echo "        ${name1}      ONLINE     0     0     0"
  echo "          ${disk3}    ONLINE     0     0     0"
  echo "        logs          ONLINE     0     0     0"
  echo "          mirror      ONLINE     0     0     0"
  echo "            ${disk4}  ONLINE     0     0     0(  [0-9.]+[A-Z] resilvered)?"
  echo "            ${disk0}  ONLINE     0     0     0(  [0-9.]+[A-Z] resilvered)?"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name1}
expect_ok ${ZPOOL} destroy ${name1}
expect_ok ${ZPOOL} import ${name0}
expect_ok ${ZPOOL} destroy ${name0}

expect_ok ${ZPOOL} create ${name0} ${disk1} log mirror ${disk2} ${disk0}
expect_ok ${ZPOOL} export ${name0}
expect_ok ${ZPOOL} create ${name1} ${disk3} log mirror ${disk4} ${disk5}
exp=`(
  echo "invalid vdev specification"
  echo "use '-f' to override the following errors:"
  echo "${fdisk0} is part of exported pool '${name0}'"
)`
expect "${exp}" ${ZPOOL} attach ${name1} ${disk2} ${disk0}
exp=`(
  echo "  pool: ${name1}"
  echo " state: ONLINE"
  echo " scrub: none requested"
  echo "config:"
  echo "        NAME          STATE   READ WRITE CKSUM"
  echo "        ${name1}      ONLINE     0     0     0"
  echo "          ${disk3}    ONLINE     0     0     0"
  echo "        logs          ONLINE     0     0     0"
  echo "          mirror      ONLINE     0     0     0"
  echo "            ${disk4}  ONLINE     0     0     0"
  echo "            ${disk5}  ONLINE     0     0     0"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name1}
expect_ok ${ZPOOL} attach -f ${name1} ${disk4} ${disk0}
wait_for_resilver ${name1}
exp=`(
  echo "  pool: ${name1}"
  echo " state: ONLINE"
  echo " scrub: resilver completed after [0-9]+h[0-9]+m with 0 errors on .*"
  echo "config:"
  echo "        NAME          STATE   READ WRITE CKSUM"
  echo "        ${name1}      ONLINE     0     0     0"
  echo "          ${disk3}    ONLINE     0     0     0"
  echo "        logs          ONLINE     0     0     0"
  echo "          mirror      ONLINE     0     0     0"
  echo "            ${disk4}  ONLINE     0     0     0(  [0-9.]+[A-Z] resilvered)?"
  echo "            ${disk5}  ONLINE     0     0     0(  [0-9.]+[A-Z] resilvered)?"
  echo "            ${disk0}  ONLINE     0     0     0(  [0-9.]+[A-Z] resilvered)?"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name1}
expect_ok ${ZPOOL} destroy ${name1}
expect_ok ${ZPOOL} import ${name0}
expect_ok ${ZPOOL} destroy ${name0}

expect_ok ${ZPOOL} create ${name0} ${disk1} cache ${disk0}
expect_ok ${ZPOOL} export ${name0}
expect_ok ${ZPOOL} create ${name1} ${disk2}
exp=`(
  echo "invalid vdev specification"
  echo "use '-f' to override the following errors:"
  echo "${fdisk0} is part of exported pool '${name0}'"
)`
add_msg="# TODO It shouldn't be possible to use offlined cache vdevs."
expect "${exp}" ${ZPOOL} attach ${name1} ${disk2} ${disk0}
exp=`(
  echo "  pool: ${name1}"
  echo " state: ONLINE"
  echo " scrub: none requested"
  echo "config:"
  echo "        NAME        STATE   READ WRITE CKSUM"
  echo "        ${name1}    ONLINE     0     0     0"
  echo "          ${disk2}  ONLINE     0     0     0"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name1}
expect_ok ${ZPOOL} attach -f ${name1} ${disk2} ${disk0}
add_msg=""
wait_for_resilver ${name1}
exp=`(
  echo "  pool: ${name1}"
  echo " state: ONLINE"
  echo " scrub: resilver completed after [0-9]+h[0-9]+m with 0 errors on .*"
  echo "config:"
  echo "        NAME          STATE   READ WRITE CKSUM"
  echo "        ${name1}      ONLINE     0     0     0"
  echo "          mirror      ONLINE     0     0     0"
  echo "            ${disk2}  ONLINE     0     0     0(  [0-9.]+[A-Z] resilvered)?"
  echo "            ${disk0}  ONLINE     0     0     0(  [0-9.]+[A-Z] resilvered)?"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name1}
expect_ok ${ZPOOL} destroy ${name1}
expect_ok ${ZPOOL} import ${name0}
expect_ok ${ZPOOL} destroy ${name0}

expect_ok ${ZPOOL} create ${name0} mirror ${disk0} ${disk1}
expect_ok ${ZPOOL} offline ${name0} ${disk0}
expect_ok ${ZPOOL} export ${name0}
expect_ok ${ZPOOL} create ${name1} ${disk2}
exp=`(
  echo "invalid vdev specification"
  echo "use '-f' to override the following errors:"
  echo "${fdisk0} is part of potentially active pool '${name0}'"
)`
expect "${exp}" ${ZPOOL} attach ${name1} ${disk2} ${disk0}
exp=`(
  echo "  pool: ${name1}"
  echo " state: ONLINE"
  echo " scrub: none requested"
  echo "config:"
  echo "        NAME        STATE   READ WRITE CKSUM"
  echo "        ${name1}    ONLINE     0     0     0"
  echo "          ${disk2}  ONLINE     0     0     0"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name1}
expect_ok ${ZPOOL} attach -f ${name1} ${disk2} ${disk0}
wait_for_resilver ${name1}
exp=`(
  echo "  pool: ${name1}"
  echo " state: ONLINE"
  echo " scrub: resilver completed after [0-9]+h[0-9]+m with 0 errors on .*"
  echo "config:"
  echo "        NAME          STATE   READ WRITE CKSUM"
  echo "        ${name1}      ONLINE     0     0     0"
  echo "          mirror      ONLINE     0     0     0"
  echo "            ${disk2}  ONLINE     0     0     0(  [0-9.]+[A-Z] resilvered)?"
  echo "            ${disk0}  ONLINE     0     0     0(  [0-9.]+[A-Z] resilvered)?"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name1}
expect_ok ${ZPOOL} destroy ${name1}
expect_ok ${ZPOOL} import ${name0}
expect_ok ${ZPOOL} destroy ${name0}

expect_ok ${ZPOOL} create ${name0} mirror ${disk0} ${disk1}
expect_ok ${ZPOOL} offline ${name0} ${disk0}
expect_ok ${ZPOOL} export ${name0}
expect_ok ${ZPOOL} create ${name1} mirror ${disk2} ${disk3}
exp=`(
  echo "invalid vdev specification"
  echo "use '-f' to override the following errors:"
  echo "${fdisk0} is part of potentially active pool '${name0}'"
)`
expect "${exp}" ${ZPOOL} attach ${name1} ${disk2} ${disk0}
exp=`(
  echo "  pool: ${name1}"
  echo " state: ONLINE"
  echo " scrub: none requested"
  echo "config:"
  echo "        NAME          STATE   READ WRITE CKSUM"
  echo "        ${name1}      ONLINE     0     0     0"
  echo "          mirror      ONLINE     0     0     0"
  echo "            ${disk2}  ONLINE     0     0     0"
  echo "            ${disk3}  ONLINE     0     0     0"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name1}
expect_ok ${ZPOOL} attach -f ${name1} ${disk2} ${disk0}
wait_for_resilver ${name1}
exp=`(
  echo "  pool: ${name1}"
  echo " state: ONLINE"
  echo " scrub: resilver completed after [0-9]+h[0-9]+m with 0 errors on .*"
  echo "config:"
  echo "        NAME          STATE   READ WRITE CKSUM"
  echo "        ${name1}      ONLINE     0     0     0"
  echo "          mirror      ONLINE     0     0     0"
  echo "            ${disk2}  ONLINE     0     0     0(  [0-9.]+[A-Z] resilvered)?"
  echo "            ${disk3}  ONLINE     0     0     0(  [0-9.]+[A-Z] resilvered)?"
  echo "            ${disk0}  ONLINE     0     0     0(  [0-9.]+[A-Z] resilvered)?"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name1}
expect_ok ${ZPOOL} destroy ${name1}
expect_ok ${ZPOOL} import ${name0}
expect_ok ${ZPOOL} destroy ${name0}

expect_ok ${ZPOOL} create ${name0} ${disk1} log mirror ${disk2} ${disk0}
expect_ok ${ZPOOL} offline ${name0} ${disk0}
expect_ok ${ZPOOL} export ${name0}
expect_ok ${ZPOOL} create ${name1} ${disk3} log ${disk4}
exp=`(
  echo "invalid vdev specification"
  echo "use '-f' to override the following errors:"
  echo "${fdisk0} is part of potentially active pool '${name0}'"
)`
expect "${exp}" ${ZPOOL} attach ${name1} ${disk4} ${disk0}
exp=`(
  echo "  pool: ${name1}"
  echo " state: ONLINE"
  echo " scrub: none requested"
  echo "config:"
  echo "        NAME        STATE   READ WRITE CKSUM"
  echo "        ${name1}    ONLINE     0     0     0"
  echo "          ${disk3}  ONLINE     0     0     0"
  echo "        logs        ONLINE     0     0     0"
  echo "          ${disk4}  ONLINE     0     0     0"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name1}
expect_ok ${ZPOOL} attach -f ${name1} ${disk4} ${disk0}
wait_for_resilver ${name1}
exp=`(
  echo "  pool: ${name1}"
  echo " state: ONLINE"
  echo " scrub: resilver completed after [0-9]+h[0-9]+m with 0 errors on .*"
  echo "config:"
  echo "        NAME          STATE   READ WRITE CKSUM"
  echo "        ${name1}      ONLINE     0     0     0"
  echo "          ${disk3}    ONLINE     0     0     0"
  echo "        logs          ONLINE     0     0     0"
  echo "          mirror      ONLINE     0     0     0"
  echo "            ${disk4}  ONLINE     0     0     0(  [0-9.]+[A-Z] resilvered)?"
  echo "            ${disk0}  ONLINE     0     0     0(  [0-9.]+[A-Z] resilvered)?"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name1}
expect_ok ${ZPOOL} destroy ${name1}
expect_ok ${ZPOOL} import ${name0}
expect_ok ${ZPOOL} destroy ${name0}

expect_ok ${ZPOOL} create ${name0} ${disk1} log mirror ${disk2} ${disk0}
expect_ok ${ZPOOL} offline ${name0} ${disk0}
expect_ok ${ZPOOL} export ${name0}
expect_ok ${ZPOOL} create ${name1} ${disk3} log mirror ${disk4} ${disk5}
exp=`(
  echo "invalid vdev specification"
  echo "use '-f' to override the following errors:"
  echo "${fdisk0} is part of potentially active pool '${name0}'"
)`
expect "${exp}" ${ZPOOL} attach ${name1} ${disk2} ${disk0}
exp=`(
  echo "  pool: ${name1}"
  echo " state: ONLINE"
  echo " scrub: none requested"
  echo "config:"
  echo "        NAME          STATE   READ WRITE CKSUM"
  echo "        ${name1}      ONLINE     0     0     0"
  echo "          ${disk3}    ONLINE     0     0     0"
  echo "        logs          ONLINE     0     0     0"
  echo "          mirror      ONLINE     0     0     0"
  echo "            ${disk4}  ONLINE     0     0     0"
  echo "            ${disk5}  ONLINE     0     0     0"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name1}
expect_ok ${ZPOOL} attach -f ${name1} ${disk4} ${disk0}
wait_for_resilver ${name1}
exp=`(
  echo "  pool: ${name1}"
  echo " state: ONLINE"
  echo " scrub: resilver completed after [0-9]+h[0-9]+m with 0 errors on .*"
  echo "config:"
  echo "        NAME          STATE   READ WRITE CKSUM"
  echo "        ${name1}      ONLINE     0     0     0"
  echo "          ${disk3}    ONLINE     0     0     0"
  echo "        logs          ONLINE     0     0     0"
  echo "          mirror      ONLINE     0     0     0"
  echo "            ${disk4}  ONLINE     0     0     0(  [0-9.]+[A-Z] resilvered)?"
  echo "            ${disk5}  ONLINE     0     0     0(  [0-9.]+[A-Z] resilvered)?"
  echo "            ${disk0}  ONLINE     0     0     0(  [0-9.]+[A-Z] resilvered)?"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name1}
expect_ok ${ZPOOL} destroy ${name1}
expect_ok ${ZPOOL} import ${name0}
expect_ok ${ZPOOL} destroy ${name0}

expect_ok ${ZPOOL} create ${name0} ${disk1} cache ${disk0}
expect_ok ${ZPOOL} offline ${name0} ${disk0}
expect_ok ${ZPOOL} export ${name0}
expect_ok ${ZPOOL} create ${name1} ${disk2}
exp=`(
  echo "invalid vdev specification"
  echo "use '-f' to override the following errors:"
  echo "${fdisk0} is part of potentially active pool '${name0}'"
)`
add_msg="# TODO It shouldn't be possible to use offlined cache vdevs."
expect "${exp}" ${ZPOOL} attach ${name1} ${disk2} ${disk0}
exp=`(
  echo "  pool: ${name1}"
  echo " state: ONLINE"
  echo " scrub: none requested"
  echo "config:"
  echo "        NAME        STATE   READ WRITE CKSUM"
  echo "        ${name1}    ONLINE     0     0     0"
  echo "          ${disk2}  ONLINE     0     0     0"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name1}
expect_ok ${ZPOOL} attach -f ${name1} ${disk2} ${disk0}
add_msg=""
wait_for_resilver ${name1}
exp=`(
  echo "  pool: ${name1}"
  echo " state: ONLINE"
  echo " scrub: resilver completed after [0-9]+h[0-9]+m with 0 errors on .*"
  echo "config:"
  echo "        NAME          STATE   READ WRITE CKSUM"
  echo "        ${name1}      ONLINE     0     0     0"
  echo "          mirror      ONLINE     0     0     0"
  echo "            ${disk2}  ONLINE     0     0     0(  [0-9.]+[A-Z] resilvered)?"
  echo "            ${disk0}  ONLINE     0     0     0(  [0-9.]+[A-Z] resilvered)?"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name1}
expect_ok ${ZPOOL} destroy ${name1}
expect_ok ${ZPOOL} import ${name0}
expect_ok ${ZPOOL} destroy ${name0}





















expect_ok ${ZPOOL} create ${name0} ${disk0}
expect_ok ${ZPOOL} create ${name1} ${disk1}
exp=`(
  echo "invalid vdev specification"
  echo "use '-f' to override the following errors:"
  echo "${fdisk0} is part of active pool '${name0}'"
)`
expect "${exp}" ${ZPOOL} attach ${name1} ${disk2} ${disk0}
exp=`(
  echo "  pool: ${name1}"
  echo " state: ONLINE"
  echo " scrub: none requested"
  echo "config:"
  echo "        NAME        STATE   READ WRITE CKSUM"
  echo "        ${name1}    ONLINE     0     0     0"
  echo "          ${disk1}  ONLINE     0     0     0"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name1}
exp=`(
  echo "invalid vdev specification"
  echo "the following errors must be manually repaired:"
  echo "${fdisk0} is part of active pool '${name0}'"
)`
expect "${exp}" ${ZPOOL} attach -f ${name1} ${disk2} ${disk0}
exp=`(
  echo "  pool: ${name1}"
  echo " state: ONLINE"
  echo " scrub: none requested"
  echo "config:"
  echo "        NAME        STATE   READ WRITE CKSUM"
  echo "        ${name1}    ONLINE     0     0     0"
  echo "          ${disk1}  ONLINE     0     0     0"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name1}
expect_ok ${ZPOOL} destroy ${name1}
expect_ok ${ZPOOL} destroy ${name0}

expect_ok ${ZPOOL} create ${name0} mirror ${disk0} ${disk1}
expect_ok ${ZPOOL} create ${name1} mirror ${disk2} ${disk3}
exp=`(
  echo "invalid vdev specification"
  echo "use '-f' to override the following errors:"
  echo "${fdisk0} is part of active pool '${name0}'"
)`
expect "${exp}" ${ZPOOL} attach ${name1} ${disk2} ${disk0}
exp=`(
  echo "  pool: ${name1}"
  echo " state: ONLINE"
  echo " scrub: none requested"
  echo "config:"
  echo "        NAME          STATE   READ WRITE CKSUM"
  echo "        ${name1}      ONLINE     0     0     0"
  echo "          mirror      ONLINE     0     0     0"
  echo "            ${disk2}  ONLINE     0     0     0"
  echo "            ${disk3}  ONLINE     0     0     0"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name1}
exp=`(
  echo "invalid vdev specification"
  echo "the following errors must be manually repaired:"
  echo "${fdisk0} is part of active pool '${name0}'"
)`
expect "${exp}" ${ZPOOL} attach -f ${name1} ${disk2} ${disk0}
exp=`(
  echo "  pool: ${name1}"
  echo " state: ONLINE"
  echo " scrub: none requested"
  echo "config:"
  echo "        NAME          STATE   READ WRITE CKSUM"
  echo "        ${name1}      ONLINE     0     0     0"
  echo "          mirror      ONLINE     0     0     0"
  echo "            ${disk2}  ONLINE     0     0     0"
  echo "            ${disk3}  ONLINE     0     0     0"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name1}
expect_ok ${ZPOOL} destroy ${name1}
expect_ok ${ZPOOL} destroy ${name0}

expect_ok ${ZPOOL} create ${name0} ${disk1} log ${disk0}
expect_ok ${ZPOOL} create ${name1} ${disk2} log ${disk3}
exp=`(
  echo "invalid vdev specification"
  echo "use '-f' to override the following errors:"
  echo "${fdisk0} is part of active pool '${name0}'"
)`
expect "${exp}" ${ZPOOL} attach ${name1} ${disk3} ${disk0}
exp=`(
  echo "  pool: ${name1}"
  echo " state: ONLINE"
  echo " scrub: none requested"
  echo "config:"
  echo "        NAME        STATE   READ WRITE CKSUM"
  echo "        ${name1}    ONLINE     0     0     0"
  echo "          ${disk2}  ONLINE     0     0     0"
  echo "        logs        ONLINE     0     0     0"
  echo "          ${disk3}  ONLINE     0     0     0"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name1}
exp=`(
  echo "invalid vdev specification"
  echo "the following errors must be manually repaired:"
  echo "${fdisk0} is part of active pool '${name0}'"
)`
expect "${exp}" ${ZPOOL} attach -f ${name1} ${disk3} ${disk0}
exp=`(
  echo "  pool: ${name1}"
  echo " state: ONLINE"
  echo " scrub: none requested"
  echo "config:"
  echo "        NAME        STATE   READ WRITE CKSUM"
  echo "        ${name1}    ONLINE     0     0     0"
  echo "          ${disk2}  ONLINE     0     0     0"
  echo "        logs        ONLINE     0     0     0"
  echo "          ${disk3}  ONLINE     0     0     0"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name1}
expect_ok ${ZPOOL} destroy ${name1}
expect_ok ${ZPOOL} destroy ${name0}

expect_ok ${ZPOOL} create ${name0} ${disk1} log mirror ${disk2} ${disk0}
expect_ok ${ZPOOL} create ${name1} ${disk3} log mirror ${disk4} ${disk5}
exp=`(
  echo "invalid vdev specification"
  echo "use '-f' to override the following errors:"
  echo "${fdisk0} is part of active pool '${name0}'"
)`
expect "${exp}" ${ZPOOL} attach ${name1} ${disk2} ${disk0}
exp=`(
  echo "  pool: ${name1}"
  echo " state: ONLINE"
  echo " scrub: none requested"
  echo "config:"
  echo "        NAME          STATE   READ WRITE CKSUM"
  echo "        ${name1}      ONLINE     0     0     0"
  echo "          ${disk3}    ONLINE     0     0     0"
  echo "        logs          ONLINE     0     0     0"
  echo "          mirror      ONLINE     0     0     0"
  echo "            ${disk4}  ONLINE     0     0     0"
  echo "            ${disk5}  ONLINE     0     0     0"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name1}
exp=`(
  echo "invalid vdev specification"
  echo "the following errors must be manually repaired:"
  echo "${fdisk0} is part of active pool '${name0}'"
)`
expect "${exp}" ${ZPOOL} attach -f ${name1} ${disk4} ${disk0}
exp=`(
  echo "  pool: ${name1}"
  echo " state: ONLINE"
  echo " scrub: none requested"
  echo "config:"
  echo "        NAME          STATE   READ WRITE CKSUM"
  echo "        ${name1}      ONLINE     0     0     0"
  echo "          ${disk3}    ONLINE     0     0     0"
  echo "        logs          ONLINE     0     0     0"
  echo "          mirror      ONLINE     0     0     0"
  echo "            ${disk4}  ONLINE     0     0     0"
  echo "            ${disk5}  ONLINE     0     0     0"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name1}
expect_ok ${ZPOOL} destroy ${name1}
expect_ok ${ZPOOL} destroy ${name0}

expect_ok ${ZPOOL} create ${name0} ${disk1} cache ${disk0}
expect_ok ${ZPOOL} create ${name1} ${disk2}
exp=`(
  echo "invalid vdev specification"
  echo "use '-f' to override the following errors:"
  echo "${fdisk0} is part of active pool '${name0}'"
)`
add_msg="# TODO It shouldn't be possible to use offlined cache vdevs."
expect "${exp}" ${ZPOOL} attach ${name1} ${disk2} ${disk0}
add_msg=""
exp=`(
  echo "  pool: ${name1}"
  echo " state: ONLINE"
  echo " scrub: none requested"
  echo "config:"
  echo "        NAME        STATE   READ WRITE CKSUM"
  echo "        ${name1}    ONLINE     0     0     0"
  echo "          ${disk2}  ONLINE     0     0     0"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name1}
add_msg="# TODO It shouldn't be possible to use offlined cache vdevs."
exp=`(
  echo "invalid vdev specification"
  echo "the following errors must be manually repaired:"
  echo "${fdisk0} is part of active pool '${name0}'"
)`
expect "${exp}" ${ZPOOL} attach -f ${name1} ${disk2} ${disk0}
add_msg=""
exp=`(
  echo "  pool: ${name1}"
  echo " state: ONLINE"
  echo " scrub: none requested"
  echo "config:"
  echo "        NAME        STATE   READ WRITE CKSUM"
  echo "        ${name1}    ONLINE     0     0     0"
  echo "          ${disk2}  ONLINE     0     0     0"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name1}
expect_ok ${ZPOOL} destroy ${name1}
expect_ok ${ZPOOL} destroy ${name0}

disks_destroy
