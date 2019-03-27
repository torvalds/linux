#!/bin/sh
# $FreeBSD$

dir=`dirname $0`
. ${dir}/../../misc.sh

echo "1..27"

disks_create 4
names_create 1

expect_ok ${ZPOOL} create ${name0} ${disk0} log mirror ${disk1} ${disk2}
expect_ok ${ZPOOL} export ${name0}
dname1=${disk1}
fdname1=${fdisk1}
guid1=`get_guid ${fdisk1}`
disk_destroy 1
disk_create 1 ${dname1}
expect_ok ${ZPOOL} import ${import_flags} ${name0}
exp=`(
  echo "  pool: ${name0}"
  echo " state: DEGRADED"
  echo "status: One or more devices could not be used because the label is missing or"
  echo "      invalid.  Sufficient replicas exist for the pool to continue"
  echo "      functioning in a degraded state."
  echo "action: Replace the device using 'zpool replace'."
  echo "   see: http://www.sun.com/msg/ZFS-8000-4J"
  echo " scrub: none requested"
  echo "config:"
  echo "	NAME          STATE     READ WRITE CKSUM"
  echo "	${name0}      DEGRADED     0     0     0"
  echo "	  ${disk0}    ONLINE       0     0     0"
  echo "	logs          DEGRADED     0     0     0"
  echo "	  mirror      DEGRADED     0     0     0"
  echo "	    ${guid1}  UNAVAIL      0     0     0 was ${fdname1}"
  echo "	    ${disk2}  ONLINE       0     0     0"
  echo "errors: No known data errors"
)`
add_msg="# TODO Sun CR 6710376, Lustre bug 16912"
expect "${exp}" ${ZPOOL} status ${name0}
add_msg=""
expect_ok ${ZPOOL} replace ${name0} ${disk1} ${disk3}
wait_for_resilver ${name0}
exp=`(
  echo "  pool: ${name0}"
  echo " state: ONLINE"
  echo " scrub: resilver completed after [0-9]+h[0-9]+m with 0 errors on .*"
  echo "config:"
  echo "	NAME          STATE   READ WRITE CKSUM"
  echo "	${name0}      ONLINE     0     0     0"
  echo "	  ${disk0}    ONLINE     0     0     0"
  echo "	logs          ONLINE     0     0     0"
  echo "	  mirror      ONLINE     0     0     0"
  echo "	    ${disk3}  ONLINE     0     0     0"
  echo "	    ${disk2}  ONLINE     0     0     0"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name0}
expect_ok ${ZPOOL} destroy ${name0}
expect_fl ${ZPOOL} status -x ${name0}

expect_ok ${ZPOOL} create ${name0} ${disk0} log mirror ${disk1} ${disk2}
expect_ok ${ZPOOL} replace ${name0} ${disk2} ${disk3}
wait_for_resilver ${name0}
exp=`(
  echo "  pool: ${name0}"
  echo " state: ONLINE"
  echo " scrub: resilver completed after [0-9]+h[0-9]+m with 0 errors on .*"
  echo "config:"
  echo "	NAME          STATE   READ WRITE CKSUM"
  echo "	${name0}      ONLINE     0     0     0"
  echo "	  ${disk0}    ONLINE     0     0     0"
  echo "	logs          ONLINE     0     0     0"
  echo "	  mirror      ONLINE     0     0     0"
  echo "	    ${disk1}  ONLINE     0     0     0"
  echo "	    ${disk3}  ONLINE     0     0     0"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name0}
expect_ok ${ZPOOL} destroy ${name0}
expect_fl ${ZPOOL} status -x ${name0}

expect_ok ${ZPOOL} create ${name0} ${disk0} log mirror ${disk1} ${disk2}
expect_ok ${ZPOOL} export ${name0}
dname1=${disk1}
fdname1=${fdisk1}
guid1=`get_guid ${fdisk1}`
disk_destroy 1
expect_ok ${ZPOOL} import ${import_flags} ${name0}
exp=`(
  echo "  pool: ${name0}"
  echo " state: DEGRADED"
  echo "status: One or more devices could not be opened.  Sufficient replicas exist for"
  echo "      the pool to continue functioning in a degraded state."
  echo "action: Attach the missing device and online it using 'zpool online'."
  echo "   see: http://www.sun.com/msg/ZFS-8000-2Q"
  echo " scrub: none requested"
  echo "config:"
  echo "	NAME           STATE     READ WRITE CKSUM"
  echo "	${name0}       DEGRADED     0     0     0"
  echo "	  ${disk0}     ONLINE       0     0     0"
  echo "	logs           DEGRADED     0     0     0"
  echo "	  mirror       DEGRADED     0     0     0"
  echo "	    ${guid1}   UNAVAIL      0     0     0  was ${fdname1}"
  echo "	    ${disk2}   ONLINE       0     0     0"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name0}
expect_ok ${ZPOOL} replace ${name0} ${dname1} ${disk3}
wait_for_resilver ${name0}
exp=`(
  echo "  pool: ${name0}"
  echo " state: ONLINE"
  echo " scrub: resilver completed after [0-9]+h[0-9]+m with 0 errors on .*"
  echo "config:"
  echo "	NAME          STATE   READ WRITE CKSUM"
  echo "	${name0}      ONLINE     0     0     0"
  echo "	  ${disk0}    ONLINE     0     0     0"
  echo "	logs          ONLINE     0     0     0"
  echo "	  mirror      ONLINE     0     0     0"
  echo "	    ${disk3}  ONLINE     0     0     0"
  echo "	    ${disk2}  ONLINE     0     0     0"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name0}
expect_ok ${ZPOOL} destroy ${name0}
expect_fl ${ZPOOL} status -x ${name0}
disk_create 1 ${dname1}

expect_ok ${ZPOOL} create ${name0} ${disk0} log mirror ${disk1} ${disk2}
expect_ok ${ZPOOL} offline ${name0} ${disk1}
expect_ok ${ZPOOL} replace ${name0} ${disk1} ${disk3}
wait_for_resilver ${name0}
exp=`(
  echo "  pool: ${name0}"
  echo " state: ONLINE"
  echo " scrub: resilver completed after [0-9]+h[0-9]+m with 0 errors on .*"
  echo "config:"
  echo "	NAME          STATE   READ WRITE CKSUM"
  echo "	${name0}      ONLINE     0     0     0"
  echo "	  ${disk0}    ONLINE     0     0     0"
  echo "	logs          ONLINE     0     0     0"
  echo "	  mirror      ONLINE     0     0     0"
  echo "	    ${disk3}  ONLINE     0     0     0"
  echo "	    ${disk2}  ONLINE     0     0     0"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name0}
expect_ok ${ZPOOL} destroy ${name0}
expect_fl ${ZPOOL} status -x ${name0}

disks_destroy
