#!/bin/sh
# $FreeBSD$

dir=`dirname $0`
. ${dir}/../../misc.sh

echo "1..263"

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
expect "${exp}" ${ZPOOL} add ${name1} ${disk0}
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
expect_ok ${ZPOOL} add -f ${name1} ${disk0}
exp=`(
  echo "  pool: ${name1}"
  echo " state: ONLINE"
  echo " scrub: none requested"
  echo "config:"
  echo "        NAME        STATE   READ WRITE CKSUM"
  echo "        ${name1}    ONLINE     0     0     0"
  echo "          ${disk1}  ONLINE     0     0     0"
  echo "          ${disk0}  ONLINE     0     0     0"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name1}
expect_ok ${ZPOOL} destroy ${name1}

expect_ok ${ZPOOL} create ${name0} ${disk0}
expect_ok ${ZPOOL} export ${name0}
expect_ok ${ZPOOL} create ${name1} ${disk1}
exp=`(
  echo "invalid vdev specification"
  echo "use '-f' to override the following errors:"
  echo "${fdisk0} is part of exported pool '${name0}'"
)`
expect "${exp}" ${ZPOOL} add ${name1} ${disk2} ${disk0}
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
expect_ok ${ZPOOL} add -f ${name1} ${disk2} ${disk0}
exp=`(
  echo "  pool: ${name1}"
  echo " state: ONLINE"
  echo " scrub: none requested"
  echo "config:"
  echo "        NAME        STATE   READ WRITE CKSUM"
  echo "        ${name1}    ONLINE     0     0     0"
  echo "          ${disk1}  ONLINE     0     0     0"
  echo "          ${disk2}  ONLINE     0     0     0"
  echo "          ${disk0}  ONLINE     0     0     0"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name1}
expect_ok ${ZPOOL} destroy ${name1}

expect_ok ${ZPOOL} create ${name0} mirror ${disk0} ${disk1}
expect_ok ${ZPOOL} export ${name0}
expect_ok ${ZPOOL} create ${name1} mirror ${disk2} ${disk3}
exp=`(
  echo "invalid vdev specification"
  echo "use '-f' to override the following errors:"
  echo "${fdisk0} is part of exported pool '${name0}'"
)`
expect "${exp}" ${ZPOOL} add ${name1} mirror ${disk0} ${disk1}
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
expect_ok ${ZPOOL} add -f ${name1} mirror ${disk0} ${disk1}
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
  echo "          mirror      ONLINE     0     0     0"
  echo "            ${disk0}  ONLINE     0     0     0"
  echo "            ${disk1}  ONLINE     0     0     0"
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
expect "${exp}" ${ZPOOL} add ${name1} mirror ${disk3} ${disk0}
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
expect_ok ${ZPOOL} add -f ${name1} mirror ${disk3} ${disk0}
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
  echo "          mirror      ONLINE     0     0     0"
  echo "            ${disk3}  ONLINE     0     0     0"
  echo "            ${disk0}  ONLINE     0     0     0"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name1}
expect_ok ${ZPOOL} destroy ${name1}

expect_ok ${ZPOOL} create ${name0} raidz1 ${disk0} ${disk1} ${disk2}
expect_ok ${ZPOOL} export ${name0}
expect_ok ${ZPOOL} create ${name1} raidz1 ${disk3} ${disk4} ${disk5}
exp=`(
  echo "invalid vdev specification"
  echo "use '-f' to override the following errors:"
  echo "${fdisk0} is part of exported pool '${name0}'"
)`
expect "${exp}" ${ZPOOL} add ${name1} raidz1 ${disk0} ${disk1} ${disk2}
exp=`(
  echo "  pool: ${name1}"
  echo " state: ONLINE"
  echo " scrub: none requested"
  echo "config:"
  echo "        NAME          STATE   READ WRITE CKSUM"
  echo "        ${name1}      ONLINE     0     0     0"
  echo "          raidz1      ONLINE     0     0     0"
  echo "            ${disk3}  ONLINE     0     0     0"
  echo "            ${disk4}  ONLINE     0     0     0"
  echo "            ${disk5}  ONLINE     0     0     0"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name1}
expect_ok ${ZPOOL} add -f ${name1} raidz1 ${disk0} ${disk1} ${disk2}
exp=`(
  echo "  pool: ${name1}"
  echo " state: ONLINE"
  echo " scrub: none requested"
  echo "config:"
  echo "        NAME          STATE   READ WRITE CKSUM"
  echo "        ${name1}      ONLINE     0     0     0"
  echo "          raidz1      ONLINE     0     0     0"
  echo "            ${disk3}  ONLINE     0     0     0"
  echo "            ${disk4}  ONLINE     0     0     0"
  echo "            ${disk5}  ONLINE     0     0     0"
  echo "          raidz1      ONLINE     0     0     0"
  echo "            ${disk0}  ONLINE     0     0     0"
  echo "            ${disk1}  ONLINE     0     0     0"
  echo "            ${disk2}  ONLINE     0     0     0"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name1}
expect_ok ${ZPOOL} destroy ${name1}

expect_ok ${ZPOOL} create ${name0} ${disk0}
expect_ok ${ZPOOL} export ${name0}
expect_ok ${ZPOOL} create ${name1} raidz1 ${disk1} ${disk2} ${disk3}
exp=`(
  echo "invalid vdev specification"
  echo "use '-f' to override the following errors:"
  echo "${fdisk0} is part of exported pool '${name0}'"
)`
expect "${exp}" ${ZPOOL} add ${name1} raidz1 ${disk4} ${disk5} ${disk0}
exp=`(
  echo "  pool: ${name1}"
  echo " state: ONLINE"
  echo " scrub: none requested"
  echo "config:"
  echo "        NAME          STATE   READ WRITE CKSUM"
  echo "        ${name1}      ONLINE     0     0     0"
  echo "          raidz1      ONLINE     0     0     0"
  echo "            ${disk1}  ONLINE     0     0     0"
  echo "            ${disk2}  ONLINE     0     0     0"
  echo "            ${disk3}  ONLINE     0     0     0"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name1}
expect_ok ${ZPOOL} add -f ${name1} raidz1 ${disk4} ${disk5} ${disk0}
exp=`(
  echo "  pool: ${name1}"
  echo " state: ONLINE"
  echo " scrub: none requested"
  echo "config:"
  echo "        NAME          STATE   READ WRITE CKSUM"
  echo "        ${name1}      ONLINE     0     0     0"
  echo "          raidz1      ONLINE     0     0     0"
  echo "            ${disk1}  ONLINE     0     0     0"
  echo "            ${disk2}  ONLINE     0     0     0"
  echo "            ${disk3}  ONLINE     0     0     0"
  echo "          raidz1      ONLINE     0     0     0"
  echo "            ${disk4}  ONLINE     0     0     0"
  echo "            ${disk5}  ONLINE     0     0     0"
  echo "            ${disk0}  ONLINE     0     0     0"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name1}
expect_ok ${ZPOOL} destroy ${name1}

expect_ok ${ZPOOL} create ${name0} raidz2 ${disk0} ${disk1} ${disk2} ${disk3}
expect_ok ${ZPOOL} export ${name0}
expect_ok ${ZPOOL} create ${name1} raidz2 ${disk4} ${disk5} ${disk6} ${disk7}
exp=`(
  echo "invalid vdev specification"
  echo "use '-f' to override the following errors:"
  echo "${fdisk0} is part of exported pool '${name0}'"
)`
expect "${exp}" ${ZPOOL} add ${name1} raidz2 ${disk0} ${disk1} ${disk2} ${disk3}
exp=`(
  echo "  pool: ${name1}"
  echo " state: ONLINE"
  echo " scrub: none requested"
  echo "config:"
  echo "        NAME          STATE   READ WRITE CKSUM"
  echo "        ${name1}      ONLINE     0     0     0"
  echo "          raidz2      ONLINE     0     0     0"
  echo "            ${disk4}  ONLINE     0     0     0"
  echo "            ${disk5}  ONLINE     0     0     0"
  echo "            ${disk6}  ONLINE     0     0     0"
  echo "            ${disk7}  ONLINE     0     0     0"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name1}
expect_ok ${ZPOOL} add -f ${name1} raidz2 ${disk0} ${disk1} ${disk2} ${disk3}
exp=`(
  echo "  pool: ${name1}"
  echo " state: ONLINE"
  echo " scrub: none requested"
  echo "config:"
  echo "        NAME          STATE   READ WRITE CKSUM"
  echo "        ${name1}      ONLINE     0     0     0"
  echo "          raidz2      ONLINE     0     0     0"
  echo "            ${disk4}  ONLINE     0     0     0"
  echo "            ${disk5}  ONLINE     0     0     0"
  echo "            ${disk6}  ONLINE     0     0     0"
  echo "            ${disk7}  ONLINE     0     0     0"
  echo "          raidz2      ONLINE     0     0     0"
  echo "            ${disk0}  ONLINE     0     0     0"
  echo "            ${disk1}  ONLINE     0     0     0"
  echo "            ${disk2}  ONLINE     0     0     0"
  echo "            ${disk3}  ONLINE     0     0     0"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name1}
expect_ok ${ZPOOL} destroy ${name1}

expect_ok ${ZPOOL} create ${name0} ${disk0}
expect_ok ${ZPOOL} export ${name0}
expect_ok ${ZPOOL} create ${name1} raidz2 ${disk1} ${disk2} ${disk3} ${disk4}
exp=`(
  echo "invalid vdev specification"
  echo "use '-f' to override the following errors:"
  echo "${fdisk0} is part of exported pool '${name0}'"
)`
expect "${exp}" ${ZPOOL} add ${name1} raidz2 ${disk5} ${disk6} ${disk0} ${disk7}
exp=`(
  echo "  pool: ${name1}"
  echo " state: ONLINE"
  echo " scrub: none requested"
  echo "config:"
  echo "        NAME          STATE   READ WRITE CKSUM"
  echo "        ${name1}      ONLINE     0     0     0"
  echo "          raidz2      ONLINE     0     0     0"
  echo "            ${disk1}  ONLINE     0     0     0"
  echo "            ${disk2}  ONLINE     0     0     0"
  echo "            ${disk3}  ONLINE     0     0     0"
  echo "            ${disk4}  ONLINE     0     0     0"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name1}
expect_ok ${ZPOOL} add -f ${name1} raidz2 ${disk5} ${disk6} ${disk0} ${disk7}
exp=`(
  echo "  pool: ${name1}"
  echo " state: ONLINE"
  echo " scrub: none requested"
  echo "config:"
  echo "        NAME          STATE   READ WRITE CKSUM"
  echo "        ${name1}      ONLINE     0     0     0"
  echo "          raidz2      ONLINE     0     0     0"
  echo "            ${disk1}  ONLINE     0     0     0"
  echo "            ${disk2}  ONLINE     0     0     0"
  echo "            ${disk3}  ONLINE     0     0     0"
  echo "            ${disk4}  ONLINE     0     0     0"
  echo "          raidz2      ONLINE     0     0     0"
  echo "            ${disk5}  ONLINE     0     0     0"
  echo "            ${disk6}  ONLINE     0     0     0"
  echo "            ${disk0}  ONLINE     0     0     0"
  echo "            ${disk7}  ONLINE     0     0     0"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name1}
expect_ok ${ZPOOL} destroy ${name1}

expect_ok ${ZPOOL} create ${name0} ${disk0}
expect_ok ${ZPOOL} export ${name0}
expect_ok ${ZPOOL} create ${name1} ${disk1}
exp=`(
  echo "invalid vdev specification"
  echo "use '-f' to override the following errors:"
  echo "${fdisk0} is part of exported pool '${name0}'"
)`
expect "${exp}" ${ZPOOL} add ${name1} log ${disk0}
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
expect_ok ${ZPOOL} add -f ${name1} log ${disk0}
exp=`(
  echo "  pool: ${name1}"
  echo " state: ONLINE"
  echo " scrub: none requested"
  echo "config:"
  echo "        NAME          STATE   READ WRITE CKSUM"
  echo "        ${name1}      ONLINE     0     0     0"
  echo "          ${disk1}    ONLINE     0     0     0"
  echo "          logs        ONLINE     0     0     0"
  echo "            ${disk0}  ONLINE     0     0     0"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name1}
expect_ok ${ZPOOL} destroy ${name1}

expect_ok ${ZPOOL} create ${name0} ${disk0}
expect_ok ${ZPOOL} export ${name0}
expect_ok ${ZPOOL} create ${name1} ${disk1}
exp=`(
  echo "invalid vdev specification"
  echo "use '-f' to override the following errors:"
  echo "${fdisk0} is part of exported pool '${name0}'"
)`
expect "${exp}" ${ZPOOL} add ${name1} log ${disk2} ${disk0}
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
expect_ok ${ZPOOL} add -f ${name1} log ${disk2} ${disk0}
exp=`(
  echo "  pool: ${name1}"
  echo " state: ONLINE"
  echo " scrub: none requested"
  echo "config:"
  echo "        NAME          STATE   READ WRITE CKSUM"
  echo "        ${name1}      ONLINE     0     0     0"
  echo "          ${disk1}    ONLINE     0     0     0"
  echo "          logs        ONLINE     0     0     0"
  echo "            ${disk2}  ONLINE     0     0     0"
  echo "            ${disk0}  ONLINE     0     0     0"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name1}
expect_ok ${ZPOOL} destroy ${name1}

expect_ok ${ZPOOL} create ${name0} ${disk0} log ${disk1}
expect_ok ${ZPOOL} export ${name0}
expect_ok ${ZPOOL} create ${name1} ${disk2}
exp=`(
  echo "invalid vdev specification"
  echo "use '-f' to override the following errors:"
  echo "${fdisk1} is part of exported pool '${name0}'"
)`
expect "${exp}" ${ZPOOL} add ${name1} log mirror ${disk1} ${disk0}
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
expect_ok ${ZPOOL} add -f ${name1} log mirror ${disk1} ${disk0}
exp=`(
  echo "  pool: ${name1}"
  echo " state: ONLINE"
  echo " scrub: none requested"
  echo "config:"
  echo "        NAME            STATE   READ WRITE CKSUM"
  echo "        ${name1}        ONLINE     0     0     0"
  echo "          ${disk2}      ONLINE     0     0     0"
  echo "          logs          ONLINE     0     0     0"
  echo "            mirror      ONLINE     0     0     0"
  echo "              ${disk1}  ONLINE     0     0     0"
  echo "              ${disk0}  ONLINE     0     0     0"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name1}
expect_ok ${ZPOOL} destroy ${name1}

expect_ok ${ZPOOL} create ${name0} ${disk0}
expect_ok ${ZPOOL} export ${name0}
expect_ok ${ZPOOL} create ${name1} ${disk1}
exp=`(
  echo "invalid vdev specification"
  echo "use '-f' to override the following errors:"
  echo "${fdisk0} is part of exported pool '${name0}'"
)`
expect "${exp}" ${ZPOOL} add ${name1} log mirror ${disk2} ${disk0}
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
expect_ok ${ZPOOL} add -f ${name1} log mirror ${disk2} ${disk0}
exp=`(
  echo "  pool: ${name1}"
  echo " state: ONLINE"
  echo " scrub: none requested"
  echo "config:"
  echo "        NAME            STATE   READ WRITE CKSUM"
  echo "        ${name1}        ONLINE     0     0     0"
  echo "          ${disk1}      ONLINE     0     0     0"
  echo "          logs          ONLINE     0     0     0"
  echo "            mirror      ONLINE     0     0     0"
  echo "              ${disk2}  ONLINE     0     0     0"
  echo "              ${disk0}  ONLINE     0     0     0"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name1}
expect_ok ${ZPOOL} destroy ${name1}

expect_ok ${ZPOOL} create ${name0} ${disk1} cache ${disk0}
expect_ok ${ZPOOL} export ${name0}
expect_ok ${ZPOOL} create ${name1} ${disk2}
exp=`(
  echo "invalid vdev specification"
  echo "use '-f' to override the following errors:"
  echo "${fdisk0} is part of exported pool '${name0}'"
)`
add_msg="# TODO It shouldn't be possible to use offlined cache vdevs."
expect "${exp}" ${ZPOOL} add ${name1} cache ${disk0}
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
expect_ok ${ZPOOL} add -f ${name1} cache ${disk0}
add_msg=""
exp=`(
  echo "  pool: ${name1}"
  echo " state: ONLINE"
  echo " scrub: none requested"
  echo "config:"
  echo "        NAME          STATE   READ WRITE CKSUM"
  echo "        ${name1}      ONLINE     0     0     0"
  echo "          ${disk2}    ONLINE     0     0     0"
  echo "          cache"
  echo "            ${disk0}  ONLINE     0     0     0"
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
expect "${exp}" ${ZPOOL} add ${name1} cache ${disk3} ${disk0}
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
expect_ok ${ZPOOL} add -f ${name1} cache ${disk3} ${disk0}
add_msg=""
exp=`(
  echo "  pool: ${name1}"
  echo " state: ONLINE"
  echo " scrub: none requested"
  echo "config:"
  echo "        NAME          STATE   READ WRITE CKSUM"
  echo "        ${name1}      ONLINE     0     0     0"
  echo "          ${disk2}    ONLINE     0     0     0"
  echo "          cache"
  echo "            ${disk3}  ONLINE     0     0     0"
  echo "            ${disk0}  ONLINE     0     0     0"
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
expect "${exp}" ${ZPOOL} add ${name1} ${disk0}
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
expect_ok ${ZPOOL} add -f ${name1} ${disk0}
exp=`(
  echo "  pool: ${name1}"
  echo " state: ONLINE"
  echo " scrub: none requested"
  echo "config:"
  echo "        NAME        STATE   READ WRITE CKSUM"
  echo "        ${name1}    ONLINE     0     0     0"
  echo "          ${disk2}  ONLINE     0     0     0"
  echo "          ${disk0}  ONLINE     0     0     0"
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
expect "${exp}" ${ZPOOL} add ${name1} mirror ${disk0} ${disk4}
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
expect_ok ${ZPOOL} add -f ${name1} mirror ${disk0} ${disk4}
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
  echo "          mirror      ONLINE     0     0     0"
  echo "            ${disk0}  ONLINE     0     0     0"
  echo "            ${disk4}  ONLINE     0     0     0"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name1}
expect_ok ${ZPOOL} destroy ${name1}
expect_ok ${ZPOOL} import ${name0}
expect_ok ${ZPOOL} destroy ${name0}

expect_ok ${ZPOOL} create ${name0} raidz1 ${disk0} ${disk1} ${disk2}
expect_ok ${ZPOOL} offline ${name0} ${disk0}
expect_ok ${ZPOOL} export ${name0}
expect_ok ${ZPOOL} create ${name1} raidz1 ${disk3} ${disk4} ${disk5}
exp=`(
  echo "invalid vdev specification"
  echo "use '-f' to override the following errors:"
  echo "${fdisk0} is part of potentially active pool '${name0}'"
)`
expect "${exp}" ${ZPOOL} add ${name1} raidz1 ${disk0} ${disk6} ${disk7}
exp=`(
  echo "  pool: ${name1}"
  echo " state: ONLINE"
  echo " scrub: none requested"
  echo "config:"
  echo "        NAME          STATE   READ WRITE CKSUM"
  echo "        ${name1}      ONLINE     0     0     0"
  echo "          raidz1      ONLINE     0     0     0"
  echo "            ${disk3}  ONLINE     0     0     0"
  echo "            ${disk4}  ONLINE     0     0     0"
  echo "            ${disk5}  ONLINE     0     0     0"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name1}
expect_ok ${ZPOOL} add -f ${name1} raidz1 ${disk0} ${disk6} ${disk7}
exp=`(
  echo "  pool: ${name1}"
  echo " state: ONLINE"
  echo " scrub: none requested"
  echo "config:"
  echo "        NAME          STATE   READ WRITE CKSUM"
  echo "        ${name1}      ONLINE     0     0     0"
  echo "          raidz1      ONLINE     0     0     0"
  echo "            ${disk3}  ONLINE     0     0     0"
  echo "            ${disk4}  ONLINE     0     0     0"
  echo "            ${disk5}  ONLINE     0     0     0"
  echo "          raidz1      ONLINE     0     0     0"
  echo "            ${disk0}  ONLINE     0     0     0"
  echo "            ${disk6}  ONLINE     0     0     0"
  echo "            ${disk7}  ONLINE     0     0     0"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name1}
expect_ok ${ZPOOL} destroy ${name1}
expect_ok ${ZPOOL} import ${name0}
expect_ok ${ZPOOL} destroy ${name0}

expect_ok ${ZPOOL} create ${name0} raidz2 ${disk0} ${disk1} ${disk2} ${disk3}
expect_ok ${ZPOOL} offline ${name0} ${disk0}
expect_ok ${ZPOOL} export ${name0}
expect_ok ${ZPOOL} create ${name1} raidz2 ${disk4} ${disk5} ${disk6} ${disk7}
exp=`(
  echo "invalid vdev specification"
  echo "use '-f' to override the following errors:"
  echo "${fdisk0} is part of potentially active pool '${name0}'"
)`
expect "${exp}" ${ZPOOL} add ${name1} raidz2 ${disk0} ${disk8} ${disk9} ${disk10}
exp=`(
  echo "  pool: ${name1}"
  echo " state: ONLINE"
  echo " scrub: none requested"
  echo "config:"
  echo "        NAME          STATE   READ WRITE CKSUM"
  echo "        ${name1}      ONLINE     0     0     0"
  echo "          raidz2      ONLINE     0     0     0"
  echo "            ${disk4}  ONLINE     0     0     0"
  echo "            ${disk5}  ONLINE     0     0     0"
  echo "            ${disk6}  ONLINE     0     0     0"
  echo "            ${disk7}  ONLINE     0     0     0"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name1}
expect_ok ${ZPOOL} add -f ${name1} raidz2 ${disk0} ${disk8} ${disk9} ${disk10}
exp=`(
  echo "  pool: ${name1}"
  echo " state: ONLINE"
  echo " scrub: none requested"
  echo "config:"
  echo "        NAME           STATE   READ WRITE CKSUM"
  echo "        ${name1}       ONLINE     0     0     0"
  echo "          raidz2       ONLINE     0     0     0"
  echo "            ${disk4}   ONLINE     0     0     0"
  echo "            ${disk5}   ONLINE     0     0     0"
  echo "            ${disk6}   ONLINE     0     0     0"
  echo "            ${disk7}   ONLINE     0     0     0"
  echo "          raidz2       ONLINE     0     0     0"
  echo "            ${disk0}   ONLINE     0     0     0"
  echo "            ${disk8}   ONLINE     0     0     0"
  echo "            ${disk9}   ONLINE     0     0     0"
  echo "            ${disk10}  ONLINE     0     0     0"
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
expect "${exp}" ${ZPOOL} add ${name1} log ${disk0}
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
expect_ok ${ZPOOL} add -f ${name1} log ${disk0}
exp=`(
  echo "  pool: ${name1}"
  echo " state: ONLINE"
  echo " scrub: none requested"
  echo "config:"
  echo "        NAME          STATE   READ WRITE CKSUM"
  echo "        ${name1}      ONLINE     0     0     0"
  echo "          ${disk2}    ONLINE     0     0     0"
  echo "          logs        ONLINE     0     0     0"
  echo "            ${disk0}  ONLINE     0     0     0"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name1}
expect_ok ${ZPOOL} destroy ${name1}
expect_ok ${ZPOOL} import ${name0}
expect_ok ${ZPOOL} destroy ${name0}

expect_ok ${ZPOOL} create ${name0} ${disk0} log mirror ${disk1} ${disk2}
expect_ok ${ZPOOL} offline ${name0} ${disk1}
expect_ok ${ZPOOL} export ${name0}
expect_ok ${ZPOOL} create ${name1} ${disk3}
exp=`(
  echo "invalid vdev specification"
  echo "use '-f' to override the following errors:"
  echo "${fdisk1} is part of potentially active pool '${name0}'"
)`
expect "${exp}" ${ZPOOL} add ${name1} log mirror ${disk1} ${disk4}
exp=`(
  echo "  pool: ${name1}"
  echo " state: ONLINE"
  echo " scrub: none requested"
  echo "config:"
  echo "        NAME        STATE   READ WRITE CKSUM"
  echo "        ${name1}    ONLINE     0     0     0"
  echo "          ${disk3}  ONLINE     0     0     0"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name1}
expect_ok ${ZPOOL} add -f ${name1} log mirror ${disk1} ${disk4}
exp=`(
  echo "  pool: ${name1}"
  echo " state: ONLINE"
  echo " scrub: none requested"
  echo "config:"
  echo "        NAME            STATE   READ WRITE CKSUM"
  echo "        ${name1}        ONLINE     0     0     0"
  echo "          ${disk3}      ONLINE     0     0     0"
  echo "          logs          ONLINE     0     0     0"
  echo "            mirror      ONLINE     0     0     0"
  echo "              ${disk1}  ONLINE     0     0     0"
  echo "              ${disk4}  ONLINE     0     0     0"
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
expect "${exp}" ${ZPOOL} add ${name1} cache ${disk0}
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
expect_ok ${ZPOOL} add -f ${name1} cache ${disk0}
add_msg=""
exp=`(
  echo "  pool: ${name1}"
  echo " state: ONLINE"
  echo " scrub: none requested"
  echo "config:"
  echo "        NAME          STATE   READ WRITE CKSUM"
  echo "        ${name1}      ONLINE     0     0     0"
  echo "          ${disk2}    ONLINE     0     0     0"
  echo "          cache"
  echo "            ${disk0}  ONLINE     0     0     0"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name1}
expect_ok ${ZPOOL} destroy ${name1}
expect_ok ${ZPOOL} import ${name0}
expect_ok ${ZPOOL} destroy ${name0}

expect_ok ${ZPOOL} create ${name0} mirror ${disk0} ${disk1}
expect_ok ${ZPOOL} offline ${name0} ${disk0}
expect_ok ${ZPOOL} create ${name1} ${disk2}
exp=`(
  echo "invalid vdev specification"
  echo "use '-f' to override the following errors:"
  echo "${fdisk0} is part of active pool '${name0}'"
)`
expect "${exp}" ${ZPOOL} add ${name1} ${disk0}
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
exp=`(
  echo "invalid vdev specification"
  echo "the following errors must be manually repaired:"
  echo "${fdisk0} is part of active pool '${name0}'"
)`
expect "${exp}" ${ZPOOL} add -f ${name1} ${disk0}
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
expect_ok ${ZPOOL} online ${name0} ${disk0}
expect_ok ${ZPOOL} destroy ${name0}

expect_ok ${ZPOOL} create ${name0} mirror ${disk0} ${disk1}
expect_ok ${ZPOOL} offline ${name0} ${disk0}
expect_ok ${ZPOOL} create ${name1} mirror ${disk2} ${disk3}
exp=`(
  echo "invalid vdev specification"
  echo "use '-f' to override the following errors:"
  echo "${fdisk0} is part of active pool '${name0}'"
)`
expect "${exp}" ${ZPOOL} add ${name1} mirror ${disk0} ${disk4}
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
expect "${exp}" ${ZPOOL} add -f ${name1} mirror ${disk0} ${disk4}
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
expect_ok ${ZPOOL} online ${name0} ${disk0}
expect_ok ${ZPOOL} destroy ${name0}

expect_ok ${ZPOOL} create ${name0} raidz1 ${disk0} ${disk1} ${disk2}
expect_ok ${ZPOOL} offline ${name0} ${disk0}
expect_ok ${ZPOOL} create ${name1} raidz1 ${disk3} ${disk4} ${disk5}
exp=`(
  echo "invalid vdev specification"
  echo "use '-f' to override the following errors:"
  echo "${fdisk0} is part of active pool '${name0}'"
)`
expect "${exp}" ${ZPOOL} add ${name1} raidz1 ${disk0} ${disk6} ${disk7}
exp=`(
  echo "  pool: ${name1}"
  echo " state: ONLINE"
  echo " scrub: none requested"
  echo "config:"
  echo "        NAME          STATE   READ WRITE CKSUM"
  echo "        ${name1}      ONLINE     0     0     0"
  echo "          raidz1      ONLINE     0     0     0"
  echo "            ${disk3}  ONLINE     0     0     0"
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
expect "${exp}" ${ZPOOL} add -f ${name1} raidz1 ${disk0} ${disk6} ${disk7}
exp=`(
  echo "  pool: ${name1}"
  echo " state: ONLINE"
  echo " scrub: none requested"
  echo "config:"
  echo "        NAME          STATE   READ WRITE CKSUM"
  echo "        ${name1}      ONLINE     0     0     0"
  echo "          raidz1      ONLINE     0     0     0"
  echo "            ${disk3}  ONLINE     0     0     0"
  echo "            ${disk4}  ONLINE     0     0     0"
  echo "            ${disk5}  ONLINE     0     0     0"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name1}
expect_ok ${ZPOOL} destroy ${name1}
expect_ok ${ZPOOL} online ${name0} ${disk0}
expect_ok ${ZPOOL} destroy ${name0}

expect_ok ${ZPOOL} create ${name0} raidz2 ${disk0} ${disk1} ${disk2} ${disk3}
expect_ok ${ZPOOL} offline ${name0} ${disk0}
expect_ok ${ZPOOL} create ${name1} raidz2 ${disk4} ${disk5} ${disk6} ${disk7}
exp=`(
  echo "invalid vdev specification"
  echo "use '-f' to override the following errors:"
  echo "${fdisk0} is part of active pool '${name0}'"
)`
expect "${exp}" ${ZPOOL} add ${name1} raidz2 ${disk0} ${disk8} ${disk9} ${disk10}
exp=`(
  echo "  pool: ${name1}"
  echo " state: ONLINE"
  echo " scrub: none requested"
  echo "config:"
  echo "        NAME          STATE   READ WRITE CKSUM"
  echo "        ${name1}      ONLINE     0     0     0"
  echo "          raidz2      ONLINE     0     0     0"
  echo "            ${disk4}  ONLINE     0     0     0"
  echo "            ${disk5}  ONLINE     0     0     0"
  echo "            ${disk6}  ONLINE     0     0     0"
  echo "            ${disk7}  ONLINE     0     0     0"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name1}
exp=`(
  echo "invalid vdev specification"
  echo "the following errors must be manually repaired:"
  echo "${fdisk0} is part of active pool '${name0}'"
)`
expect "${exp}" ${ZPOOL} add -f ${name1} raidz2 ${disk0} ${disk8} ${disk9} ${disk10}
exp=`(
  echo "  pool: ${name1}"
  echo " state: ONLINE"
  echo " scrub: none requested"
  echo "config:"
  echo "        NAME           STATE   READ WRITE CKSUM"
  echo "        ${name1}       ONLINE     0     0     0"
  echo "          raidz2       ONLINE     0     0     0"
  echo "            ${disk4}   ONLINE     0     0     0"
  echo "            ${disk5}   ONLINE     0     0     0"
  echo "            ${disk6}   ONLINE     0     0     0"
  echo "            ${disk7}   ONLINE     0     0     0"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name1}
expect_ok ${ZPOOL} destroy ${name1}
expect_ok ${ZPOOL} online ${name0} ${disk0}
expect_ok ${ZPOOL} destroy ${name0}

expect_ok ${ZPOOL} create ${name0} mirror ${disk0} ${disk1}
expect_ok ${ZPOOL} offline ${name0} ${disk0}
expect_ok ${ZPOOL} create ${name1} ${disk2}
exp=`(
  echo "invalid vdev specification"
  echo "use '-f' to override the following errors:"
  echo "${fdisk0} is part of active pool '${name0}'"
)`
expect "${exp}" ${ZPOOL} add ${name1} log ${disk0}
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
exp=`(
  echo "invalid vdev specification"
  echo "the following errors must be manually repaired:"
  echo "${fdisk0} is part of active pool '${name0}'"
)`
expect "${exp}" ${ZPOOL} add -f ${name1} log ${disk0}
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
expect_ok ${ZPOOL} online ${name0} ${disk0}
expect_ok ${ZPOOL} destroy ${name0}

expect_ok ${ZPOOL} create ${name0} ${disk0} log mirror ${disk1} ${disk2}
expect_ok ${ZPOOL} offline ${name0} ${disk1}
expect_ok ${ZPOOL} create ${name1} ${disk3}
exp=`(
  echo "invalid vdev specification"
  echo "use '-f' to override the following errors:"
  echo "${fdisk1} is part of active pool '${name0}'"
)`
expect "${exp}" ${ZPOOL} add ${name1} log mirror ${disk1} ${disk4}
exp=`(
  echo "  pool: ${name1}"
  echo " state: ONLINE"
  echo " scrub: none requested"
  echo "config:"
  echo "        NAME        STATE   READ WRITE CKSUM"
  echo "        ${name1}    ONLINE     0     0     0"
  echo "          ${disk3}  ONLINE     0     0     0"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name1}
exp=`(
  echo "invalid vdev specification"
  echo "the following errors must be manually repaired:"
  echo "${fdisk1} is part of active pool '${name0}'"
)`
expect "${exp}" ${ZPOOL} add -f ${name1} log mirror ${disk1} ${disk4}
exp=`(
  echo "  pool: ${name1}"
  echo " state: ONLINE"
  echo " scrub: none requested"
  echo "config:"
  echo "        NAME        STATE   READ WRITE CKSUM"
  echo "        ${name1}    ONLINE     0     0     0"
  echo "          ${disk3}  ONLINE     0     0     0"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name1}
expect_ok ${ZPOOL} destroy ${name1}
expect_ok ${ZPOOL} online ${name0} ${disk1}
expect_ok ${ZPOOL} destroy ${name0}

expect_ok ${ZPOOL} create ${name0} ${disk1} cache ${disk0}
expect_ok ${ZPOOL} offline ${name0} ${disk0}
expect_ok ${ZPOOL} create ${name1} ${disk2}
exp=`(
  echo "invalid vdev specification"
  echo "use '-f' to override the following errors:"
  echo "${fdisk0} is part of active pool '${name0}'"
)`
add_msg="# TODO It reports that ${fdisk0} is part of unknown pool."
expect "${exp}" ${ZPOOL} add ${name1} cache ${disk0}
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
add_msg="# TODO Invalid problem description."
exp=`(
  echo "invalid vdev specification"
  echo "the following errors must be manually repaired:"
  echo "${fdisk0} is part of active pool '${name0}'"
)`
expect "${exp}" ${ZPOOL} add -f ${name1} cache ${disk0}
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
expect_ok ${ZPOOL} online ${name0} ${disk0}
expect_ok ${ZPOOL} destroy ${name0}

disks_destroy
