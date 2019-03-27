#!/bin/sh
# $FreeBSD$

dir=`dirname $0`
. ${dir}/../../misc.sh

echo "1..115"

disks_create 6
names_create 1

expect_ok ${ZPOOL} create ${name0} raidz2 ${disk0} ${disk1} ${disk2} ${disk3}
expect_ok ${ZPOOL} export ${name0}
dname0=${disk0}
fdname0=${fdisk0}
guid0=`get_guid ${fdisk0}`
disk_destroy 0
disk_create 0 ${dname0}
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
  echo "	  raidz2      DEGRADED     0     0     0"
  echo "	    ${guid0}  UNAVAIL      0     0     0  was ${fdname0}"
  echo "	    ${disk1}  ONLINE       0     0     0"
  echo "	    ${disk2}  ONLINE       0     0     0"
  echo "	    ${disk3}  ONLINE       0     0     0"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name0}
expect_ok ${ZPOOL} replace ${name0} ${disk0} ${disk4}
wait_for_resilver ${name0}
exp=`(
  echo "  pool: ${name0}"
  echo " state: ONLINE"
  echo " scrub: (scrub|resilver) completed after [0-9]+h[0-9]+m with 0 errors on .*"
  echo "config:"
  echo "	NAME          STATE   READ WRITE CKSUM"
  echo "	${name0}      ONLINE     0     0     0"
  echo "	  raidz2      ONLINE     0     0     0"
  echo "	    ${disk4}  ONLINE     0     0     0(  [0-9.]+[A-Z] resilvered)?"
  echo "	    ${disk1}  ONLINE     0     0     0(  [0-9.]+[A-Z] resilvered)?"
  echo "	    ${disk2}  ONLINE     0     0     0(  [0-9.]+[A-Z] resilvered)?"
  echo "	    ${disk3}  ONLINE     0     0     0(  [0-9.]+[A-Z] resilvered)?"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name0}
expect_ok ${ZPOOL} destroy ${name0}
expect_fl ${ZPOOL} status -x ${name0}

expect_ok ${ZPOOL} create ${name0} raidz2 ${disk0} ${disk1} ${disk2} ${disk3}
expect_ok ${ZPOOL} replace ${name0} ${disk1} ${disk4}
wait_for_resilver ${name0}
exp=`(
  echo "  pool: ${name0}"
  echo " state: ONLINE"
  echo " scrub: (scrub|resilver) completed after [0-9]+h[0-9]+m with 0 errors on .*"
  echo "config:"
  echo "	NAME          STATE   READ WRITE CKSUM"
  echo "	${name0}      ONLINE     0     0     0"
  echo "	  raidz2      ONLINE     0     0     0"
  echo "	    ${disk0}  ONLINE     0     0     0(  [0-9.]+[A-Z] resilvered)?"
  echo "	    ${disk4}  ONLINE     0     0     0(  [0-9.]+[A-Z] resilvered)?"
  echo "	    ${disk2}  ONLINE     0     0     0(  [0-9.]+[A-Z] resilvered)?"
  echo "	    ${disk3}  ONLINE     0     0     0(  [0-9.]+[A-Z] resilvered)?"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name0}
expect_ok ${ZPOOL} destroy ${name0}
expect_fl ${ZPOOL} status -x ${name0}

expect_ok ${ZPOOL} create ${name0} raidz2 ${disk0} ${disk1} ${disk2} ${disk3}
expect_ok ${ZPOOL} export ${name0}
dname0=${disk0}
fdname0=${fdisk0}
guid0=`get_guid ${fdisk0}`
disk_destroy 0
expect_ok ${ZPOOL} import ${import_flags} ${name0}
exp=`(
  echo "  pool: ${name0}"
  echo " state: DEGRADED"
  echo " scrub: none requested"
  echo "config:"
  echo "	NAME           STATE     READ WRITE CKSUM"
  echo "	${name0}       DEGRADED     0     0     0"
  echo "	  raidz2       DEGRADED     0     0     0"
  echo "	    ${guid0}   REMOVED      0     0     0  was ${fdname0}"
  echo "	    ${disk1}   ONLINE       0     0     0"
  echo "	    ${disk2}   ONLINE       0     0     0"
  echo "	    ${disk3}   ONLINE       0     0     0"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name0}
expect_ok ${ZPOOL} replace ${name0} ${dname0} ${disk4}
wait_for_resilver ${name0}
exp=`(
  echo "  pool: ${name0}"
  echo " state: ONLINE"
  echo " scrub: (scrub|resilver) completed after [0-9]+h[0-9]+m with 0 errors on .*"
  echo "config:"
  echo "	NAME          STATE   READ WRITE CKSUM"
  echo "	${name0}      ONLINE     0     0     0"
  echo "	  raidz2      ONLINE     0     0     0"
  echo "	    ${disk4}  ONLINE     0     0     0(  [0-9.]+[A-Z] resilvered)?"
  echo "	    ${disk1}  ONLINE     0     0     0(  [0-9.]+[A-Z] resilvered)?"
  echo "	    ${disk2}  ONLINE     0     0     0(  [0-9.]+[A-Z] resilvered)?"
  echo "	    ${disk3}  ONLINE     0     0     0(  [0-9.]+[A-Z] resilvered)?"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name0}
expect_ok ${ZPOOL} destroy ${name0}
expect_fl ${ZPOOL} status -x ${name0}
disk_create 0 ${dname0}

expect_ok ${ZPOOL} create ${name0} raidz2 ${disk0} ${disk1} ${disk2} ${disk3}
expect_ok ${ZPOOL} offline ${name0} ${disk1}
expect_ok ${ZPOOL} replace ${name0} ${disk1} ${disk4}
wait_for_resilver ${name0}
exp=`(
  echo "  pool: ${name0}"
  echo " state: ONLINE"
  echo " scrub: (scrub|resilver) completed after [0-9]+h[0-9]+m with 0 errors on .*"
  echo "config:"
  echo "	NAME          STATE   READ WRITE CKSUM"
  echo "	${name0}      ONLINE     0     0     0"
  echo "	  raidz2      ONLINE     0     0     0"
  echo "	    ${disk0}  ONLINE     0     0     0(  [0-9.]+[A-Z] resilvered)?"
  echo "	    ${disk4}  ONLINE     0     0     0(  [0-9.]+[A-Z] resilvered)?"
  echo "	    ${disk2}  ONLINE     0     0     0(  [0-9.]+[A-Z] resilvered)?"
  echo "	    ${disk3}  ONLINE     0     0     0(  [0-9.]+[A-Z] resilvered)?"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name0}
expect_ok ${ZPOOL} destroy ${name0}
expect_fl ${ZPOOL} status -x ${name0}

expect_ok ${ZPOOL} create ${zpool_f_flag} ${name0} raidz2 ${disk0} ${disk1} ${disk2} ${disk3}
expect_ok ${ZPOOL} export ${name0}
dname0=${disk0}
fdname0=${fdisk0}
guid0=`get_guid ${fdisk0}`
disk_destroy 0
disk_create 0 ${dname0}
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
  echo "	  raidz2      DEGRADED     0     0     0"
  echo "	    ${guid0}  UNAVAIL      0     0     0  was ${fdname0}"
  echo "	    ${guid1}  UNAVAIL      0     0     0  was ${fdname1}"
  echo "	    ${disk2}  ONLINE       0     0     0"
  echo "	    ${disk3}  ONLINE       0     0     0"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name0}
expect_ok ${ZPOOL} replace ${name0} ${disk0} ${disk4}
expect_ok ${ZPOOL} replace ${name0} ${disk1} ${disk5}
wait_for_resilver ${name0}
exp=`(
  echo "  pool: ${name0}"
  echo " state: ONLINE"
  echo " scrub: (scrub|resilver) completed after [0-9]+h[0-9]+m with 0 errors on .*"
  echo "config:"
  echo "	NAME          STATE   READ WRITE CKSUM"
  echo "	${name0}      ONLINE     0     0     0"
  echo "	  raidz2      ONLINE     0     0     0"
  echo "	    ${disk4}  ONLINE     0     0     0(  [0-9.]+[A-Z] resilvered)?"
  echo "	    ${disk5}  ONLINE     0     0     0(  [0-9.]+[A-Z] resilvered)?"
  echo "	    ${disk2}  ONLINE     0     0     0(  [0-9.]+[A-Z] resilvered)?"
  echo "	    ${disk3}  ONLINE     0     0     0(  [0-9.]+[A-Z] resilvered)?"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name0}
expect_ok ${ZPOOL} destroy ${name0}
expect_fl ${ZPOOL} status -x ${name0}

expect_ok ${ZPOOL} create ${name0} raidz2 ${disk0} ${disk1} ${disk2} ${disk3}
expect_ok ${ZPOOL} replace ${name0} ${disk1} ${disk4}
expect_ok ${ZPOOL} replace ${name0} ${disk2} ${disk5}
wait_for_resilver ${name0}
exp=`(
  echo "  pool: ${name0}"
  echo " state: ONLINE"
  echo " scrub: (scrub|resilver) completed after [0-9]+h[0-9]+m with 0 errors on .*"
  echo "config:"
  echo "	NAME          STATE   READ WRITE CKSUM"
  echo "	${name0}      ONLINE     0     0     0"
  echo "	  raidz2      ONLINE     0     0     0"
  echo "	    ${disk0}  ONLINE     0     0     0(  [0-9.]+[A-Z] resilvered)?"
  echo "	    ${disk4}  ONLINE     0     0     0(  [0-9.]+[A-Z] resilvered)?"
  echo "	    ${disk5}  ONLINE     0     0     0(  [0-9.]+[A-Z] resilvered)?"
  echo "	    ${disk3}  ONLINE     0     0     0(  [0-9.]+[A-Z] resilvered)?"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name0}
expect_ok ${ZPOOL} destroy ${name0}
expect_fl ${ZPOOL} status -x ${name0}

expect_ok ${ZPOOL} create ${name0} raidz2 ${disk0} ${disk1} ${disk2} ${disk3}
expect_ok ${ZPOOL} export ${name0}
dname0=${disk0}
fdname0=${fdisk0}
guid0=`get_guid ${fdisk0}`
disk_destroy 0
dname1=${disk1}
fdname1=${fdisk1}
guid1=`get_guid ${fdisk1}`
disk_destroy 1
expect_ok ${ZPOOL} import ${import_flags} ${name0}
exp=`(
  echo "  pool: ${name0}"
  echo " state: DEGRADED"
  echo " scrub: none requested"
  echo "config:"
  echo "	NAME           STATE     READ WRITE CKSUM"
  echo "	${name0}       DEGRADED     0     0     0"
  echo "	  raidz2       DEGRADED     0     0     0"
  echo "	    ${guid0}   REMOVED      0     0     0  was ${fdname0}"
  echo "	    ${guid1}   REMOVED      0     0     0  was ${fdname1}"
  echo "	    ${disk2}   ONLINE       0     0     0"
  echo "	    ${disk3}   ONLINE       0     0     0"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name0}
expect_ok ${ZPOOL} replace ${name0} ${dname0} ${disk4}
expect_ok ${ZPOOL} replace ${name0} ${dname1} ${disk5}
wait_for_resilver ${name0}
exp=`(
  echo "  pool: ${name0}"
  echo " state: ONLINE"
  echo " scrub: (scrub|resilver) completed after [0-9]+h[0-9]+m with 0 errors on .*"
  echo "config:"
  echo "	NAME          STATE   READ WRITE CKSUM"
  echo "	${name0}      ONLINE     0     0     0"
  echo "	  raidz2      ONLINE     0     0     0"
  echo "	    ${disk4}  ONLINE     0     0     0(  [0-9.]+[A-Z] resilvered)?"
  echo "	    ${disk5}  ONLINE     0     0     0(  [0-9.]+[A-Z] resilvered)?"
  echo "	    ${disk2}  ONLINE     0     0     0(  [0-9.]+[A-Z] resilvered)?"
  echo "	    ${disk3}  ONLINE     0     0     0(  [0-9.]+[A-Z] resilvered)?"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name0}
expect_ok ${ZPOOL} destroy ${name0}
expect_fl ${ZPOOL} status -x ${name0}
disk_create 0 ${dname0}
disk_create 1 ${dname1}

expect_ok ${ZPOOL} create ${name0} raidz2 ${disk0} ${disk1} ${disk2} ${disk3}
expect_ok ${ZPOOL} offline ${name0} ${disk0}
add_msg="# TODO Sun CR 6328632, Lustre bug 16878"
expect_ok ${ZPOOL} offline ${name0} ${disk1}
exp=`(
  echo "  pool: ${name0}"
  echo " state: DEGRADED"
  echo "status: One or more devices has been taken offline by the administrator."
  echo "      Sufficient replicas exist for the pool to continue functioning in a"
  echo "      degraded state."
  echo "action: Online the device using 'zpool online' or replace the device with"
  echo "      'zpool replace'."
  echo " scrub: none requested"
  echo "config:"
  echo "	NAME          STATE     READ WRITE CKSUM"
  echo "	${name0}      DEGRADED     0     0     0"
  echo "	  raidz2      DEGRADED     0     0     0"
  echo "	    ${disk0}  OFFLINE      0     0     0"
  echo "	    ${disk1}  OFFLINE      0     0     0"
  echo "	    ${disk2}  ONLINE       0     0     0"
  echo "	    ${disk3}  ONLINE       0     0     0"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name0}
add_msg=""
expect_ok ${ZPOOL} replace ${name0} ${disk0} ${disk4}
expect_ok ${ZPOOL} replace ${name0} ${disk1} ${disk5}
wait_for_resilver ${name0}
exp=`(
  echo "  pool: ${name0}"
  echo " state: ONLINE"
  echo " scrub: (scrub|resilver) completed after [0-9]+h[0-9]+m with 0 errors on .*"
  echo "config:"
  echo "	NAME          STATE   READ WRITE CKSUM"
  echo "	${name0}      ONLINE     0     0     0"
  echo "	  raidz2      ONLINE     0     0     0"
  echo "	    ${disk4}  ONLINE     0     0     0(  [0-9.]+[A-Z] resilvered)?"
  echo "	    ${disk5}  ONLINE     0     0     0(  [0-9.]+[A-Z] resilvered)?"
  echo "	    ${disk2}  ONLINE     0     0     0(  [0-9.]+[A-Z] resilvered)?"
  echo "	    ${disk3}  ONLINE     0     0     0(  [0-9.]+[A-Z] resilvered)?"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name0}
expect_ok ${ZPOOL} destroy ${name0}
expect_fl ${ZPOOL} status -x ${name0}

expect_ok ${ZPOOL} create ${zpool_f_flag} ${name0} raidz2 ${disk0} ${disk1} ${disk2} ${disk3}
expect_ok ${ZPOOL} export ${name0}
dname0=${disk0}
fdname0=${fdisk0}
guid0=`get_guid ${fdisk0}`
disk_destroy 0
disk_create 0 ${dname0}
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
  echo "	  raidz2      DEGRADED     0     0     0"
  echo "	    ${guid0}  UNAVAIL      0     0     0  was ${fdname0}"
  echo "	    ${disk1}  ONLINE       0     0     0"
  echo "	    ${disk2}  ONLINE       0     0     0"
  echo "	    ${disk3}  ONLINE       0     0     0"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name0}
expect_ok ${ZPOOL} replace ${name0} ${disk0} ${disk4}
expect_ok ${ZPOOL} replace ${name0} ${disk1} ${disk5}
wait_for_resilver ${name0}
exp=`(
  echo "  pool: ${name0}"
  echo " state: ONLINE"
  echo " scrub: (scrub|resilver) completed after [0-9]+h[0-9]+m with 0 errors on .*"
  echo "config:"
  echo "	NAME          STATE   READ WRITE CKSUM"
  echo "	${name0}      ONLINE     0     0     0"
  echo "	  raidz2      ONLINE     0     0     0"
  echo "	    ${disk4}  ONLINE     0     0     0(  [0-9.]+[A-Z] resilvered)?"
  echo "	    ${disk5}  ONLINE     0     0     0(  [0-9.]+[A-Z] resilvered)?"
  echo "	    ${disk2}  ONLINE     0     0     0(  [0-9.]+[A-Z] resilvered)?"
  echo "	    ${disk3}  ONLINE     0     0     0(  [0-9.]+[A-Z] resilvered)?"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name0}
expect_ok ${ZPOOL} destroy ${name0}
expect_fl ${ZPOOL} status -x ${name0}

expect_ok ${ZPOOL} create ${name0} raidz2 ${disk0} ${disk1} ${disk2} ${disk3}
expect_ok ${ZPOOL} export ${name0}
dname0=${disk0}
fdname0=${fdisk0}
guid0=`get_guid ${fdisk0}`
disk_destroy 0
disk_create 0 ${dname0}
dname1=${disk1}
fdname1=${fdisk1}
guid1=`get_guid ${fdisk1}`
disk_destroy 1
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
  echo "	NAME           STATE     READ WRITE CKSUM"
  echo "	${name0}       DEGRADED     0     0     0"
  echo "	  raidz2       DEGRADED     0     0     0"
  echo "	    ${guid0}   UNAVAIL      0     0     0  was ${fdname0}"
  echo "	    ${guid1}   REMOVED      0     0     0  was ${fdname1}"
  echo "	    ${disk2}   ONLINE       0     0     0"
  echo "	    ${disk3}   ONLINE       0     0     0"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name0}
expect_ok ${ZPOOL} replace ${name0} ${disk0} ${disk4}
expect_ok ${ZPOOL} replace ${name0} ${dname1} ${disk5}
wait_for_resilver ${name0}
exp=`(
  echo "  pool: ${name0}"
  echo " state: ONLINE"
  echo " scrub: (scrub|resilver) completed after [0-9]+h[0-9]+m with 0 errors on .*"
  echo "config:"
  echo "	NAME          STATE   READ WRITE CKSUM"
  echo "	${name0}      ONLINE     0     0     0"
  echo "	  raidz2      ONLINE     0     0     0"
  echo "	    ${disk4}  ONLINE     0     0     0(  [0-9.]+[A-Z] resilvered)?"
  echo "	    ${disk5}  ONLINE     0     0     0(  [0-9.]+[A-Z] resilvered)?"
  echo "	    ${disk2}  ONLINE     0     0     0(  [0-9.]+[A-Z] resilvered)?"
  echo "	    ${disk3}  ONLINE     0     0     0(  [0-9.]+[A-Z] resilvered)?"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name0}
expect_ok ${ZPOOL} destroy ${name0}
expect_fl ${ZPOOL} status -x ${name0}
disk_create 1 ${dname1}

expect_ok ${ZPOOL} create ${name0} raidz2 ${disk0} ${disk1} ${disk2} ${disk3}
expect_ok ${ZPOOL} export ${name0}
dname0=${disk0}
fdname0=${fdisk0}
guid0=`get_guid ${fdisk0}`
disk_destroy 0
expect_ok ${ZPOOL} import ${import_flags} ${name0}
exp=`(
  echo "  pool: ${name0}"
  echo " state: DEGRADED"
  echo " scrub: none requested"
  echo "config:"
  echo "	NAME           STATE     READ WRITE CKSUM"
  echo "	${name0}       DEGRADED     0     0     0"
  echo "	  raidz2       DEGRADED     0     0     0"
  echo "	    ${guid0}   REMOVED      0     0     0  was ${fdname0}"
  echo "	    ${disk1}   ONLINE       0     0     0"
  echo "	    ${disk2}   ONLINE       0     0     0"
  echo "	    ${disk3}   ONLINE       0     0     0"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name0}
expect_ok ${ZPOOL} replace ${name0} ${dname0} ${disk4}
expect_ok ${ZPOOL} replace ${name0} ${disk1} ${disk5}
wait_for_resilver ${name0}
exp=`(
  echo "  pool: ${name0}"
  echo " state: ONLINE"
  echo " scrub: (scrub|resilver) completed after [0-9]+h[0-9]+m with 0 errors on .*"
  echo "config:"
  echo "	NAME          STATE   READ WRITE CKSUM"
  echo "	${name0}      ONLINE     0     0     0"
  echo "	  raidz2      ONLINE     0     0     0"
  echo "	    ${disk4}  ONLINE     0     0     0(  [0-9.]+[A-Z] resilvered)?"
  echo "	    ${disk5}  ONLINE     0     0     0(  [0-9.]+[A-Z] resilvered)?"
  echo "	    ${disk2}  ONLINE     0     0     0(  [0-9.]+[A-Z] resilvered)?"
  echo "	    ${disk3}  ONLINE     0     0     0(  [0-9.]+[A-Z] resilvered)?"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name0}
expect_ok ${ZPOOL} destroy ${name0}
expect_fl ${ZPOOL} status -x ${name0}
disk_create 0 ${dname0}

expect_ok ${ZPOOL} create ${name0} raidz2 ${disk0} ${disk1} ${disk2} ${disk3}
expect_ok ${ZPOOL} offline ${name0} ${disk0}
exp=`(
  echo "  pool: ${name0}"
  echo " state: DEGRADED"
  echo "status: One or more devices has been taken offline by the administrator."
  echo "      Sufficient replicas exist for the pool to continue functioning in a"
  echo "      degraded state."
  echo "action: Online the device using 'zpool online' or replace the device with"
  echo "      'zpool replace'."
  echo " scrub: none requested"
  echo "config:"
  echo "	NAME          STATE     READ WRITE CKSUM"
  echo "	${name0}      DEGRADED     0     0     0"
  echo "	  raidz2      DEGRADED     0     0     0"
  echo "	    ${disk0}  OFFLINE      0     0     0"
  echo "	    ${disk1}  ONLINE       0     0     0"
  echo "	    ${disk2}  ONLINE       0     0     0"
  echo "	    ${disk3}  ONLINE       0     0     0"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name0}
expect_ok ${ZPOOL} replace ${name0} ${disk0} ${disk4}
expect_ok ${ZPOOL} replace ${name0} ${disk1} ${disk5}
wait_for_resilver ${name0}
exp=`(
  echo "  pool: ${name0}"
  echo " state: ONLINE"
  echo " scrub: (scrub|resilver) completed after [0-9]+h[0-9]+m with 0 errors on .*"
  echo "config:"
  echo "	NAME          STATE   READ WRITE CKSUM"
  echo "	${name0}      ONLINE     0     0     0"
  echo "	  raidz2      ONLINE     0     0     0"
  echo "	    ${disk4}  ONLINE     0     0     0(  [0-9.]+[A-Z] resilvered)?"
  echo "	    ${disk5}  ONLINE     0     0     0(  [0-9.]+[A-Z] resilvered)?"
  echo "	    ${disk2}  ONLINE     0     0     0(  [0-9.]+[A-Z] resilvered)?"
  echo "	    ${disk3}  ONLINE     0     0     0(  [0-9.]+[A-Z] resilvered)?"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name0}
expect_ok ${ZPOOL} destroy ${name0}
expect_fl ${ZPOOL} status -x ${name0}

expect_ok ${ZPOOL} create ${zpool_f_flag} ${name0} raidz2 ${disk0} ${disk1} ${disk2} ${disk3}
expect_ok ${ZPOOL} offline ${name0} ${disk0}
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
  echo "	  raidz2      DEGRADED     0     0     0"
  echo "	    ${disk0}  OFFLINE      0     0     0"
  echo "	    ${guid1}  UNAVAIL      0     0     0  was ${fdname1}"
  echo "	    ${disk2}  ONLINE       0     0     0"
  echo "	    ${disk3}  ONLINE       0     0     0"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name0}
expect_ok ${ZPOOL} replace ${name0} ${disk0} ${disk4}
expect_ok ${ZPOOL} replace ${name0} ${disk1} ${disk5}
wait_for_resilver ${name0}
exp=`(
  echo "  pool: ${name0}"
  echo " state: ONLINE"
  echo " scrub: (scrub|resilver) completed after [0-9]+h[0-9]+m with 0 errors on .*"
  echo "config:"
  echo "	NAME          STATE   READ WRITE CKSUM"
  echo "	${name0}      ONLINE     0     0     0"
  echo "	  raidz2      ONLINE     0     0     0"
  echo "	    ${disk4}  ONLINE     0     0     0(  [0-9.]+[A-Z] resilvered)?"
  echo "	    ${disk5}  ONLINE     0     0     0(  [0-9.]+[A-Z] resilvered)?"
  echo "	    ${disk2}  ONLINE     0     0     0(  [0-9.]+[A-Z] resilvered)?"
  echo "	    ${disk3}  ONLINE     0     0     0(  [0-9.]+[A-Z] resilvered)?"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name0}
expect_ok ${ZPOOL} destroy ${name0}
expect_fl ${ZPOOL} status -x ${name0}

expect_ok ${ZPOOL} create ${zpool_f_flag} ${name0} raidz2 ${disk0} ${disk1} ${disk2} ${disk3}
expect_ok ${ZPOOL} offline ${name0} ${disk0}
expect_ok ${ZPOOL} export ${name0}
dname1=${disk1}
fdname1=${fdisk1}
guid1=`get_guid ${fdisk1}`
disk_destroy 1
expect_ok ${ZPOOL} import ${import_flags} ${name0}
exp=`(
  echo "  pool: ${name0}"
  echo " state: DEGRADED"
  echo "status: One or more devices has been taken offline by the administrator."
  echo "      Sufficient replicas exist for the pool to continue functioning in a"
  echo "      degraded state."
  echo "action: Online the device using 'zpool online' or replace the device with"
  echo "      'zpool replace'."
  echo " scrub: none requested"
  echo "config:"
  echo "	NAME           STATE     READ WRITE CKSUM"
  echo "	${name0}       DEGRADED     0     0     0"
  echo "	  raidz2       DEGRADED     0     0     0"
  echo "	    ${disk0}   OFFLINE      0     0     0"
  echo "	    ${guid1}   REMOVED      0     0     0  was ${fdname1}"
  echo "	    ${disk2}   ONLINE       0     0     0"
  echo "	    ${disk3}   ONLINE       0     0     0"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name0}
expect_ok ${ZPOOL} replace ${name0} ${disk0} ${disk4}
expect_ok ${ZPOOL} replace ${name0} ${dname1} ${disk5}
wait_for_resilver ${name0}
exp=`(
  echo "  pool: ${name0}"
  echo " state: ONLINE"
  echo " scrub: (scrub|resilver) completed after [0-9]+h[0-9]+m with 0 errors on .*"
  echo "config:"
  echo "	NAME          STATE   READ WRITE CKSUM"
  echo "	${name0}      ONLINE     0     0     0"
  echo "	  raidz2      ONLINE     0     0     0"
  echo "	    ${disk4}  ONLINE     0     0     0(  [0-9.]+[A-Z] resilvered)?"
  echo "	    ${disk5}  ONLINE     0     0     0(  [0-9.]+[A-Z] resilvered)?"
  echo "	    ${disk2}  ONLINE     0     0     0(  [0-9.]+[A-Z] resilvered)?"
  echo "	    ${disk3}  ONLINE     0     0     0(  [0-9.]+[A-Z] resilvered)?"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name0}
expect_ok ${ZPOOL} destroy ${name0}
expect_fl ${ZPOOL} status -x ${name0}
disk_create 1 ${dname1}

disks_destroy
