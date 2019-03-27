#!/bin/sh
# $FreeBSD$

dir=`dirname $0`
. ${dir}/../../misc.sh

echo "1..35"

disks_create 5
names_create 1

expect_ok ${ZPOOL} create ${name0} raidz1 ${disk0} ${disk1}
expect_ok ${ZPOOL} offline ${name0} ${disk0}
exp=`(
  echo "  pool: ${name0}"
  echo " state: DEGRADED"
  echo "status: One or more devices has been taken offline by the administrator."
  echo "        Sufficient replicas exist for the pool to continue functioning in a"
  echo "        degraded state."
  echo "action: Online the device using 'zpool online' or replace the device with"
  echo "        'zpool replace'."
  echo " scrub: none requested"
  echo "config:"
  echo "	NAME          STATE     READ WRITE CKSUM"
  echo "	${name0}      DEGRADED     0     0     0"
  echo "	  raidz1      DEGRADED     0     0     0"
  echo "	    ${disk0}  OFFLINE      0     0     0"
  echo "	    ${disk1}  ONLINE       0     0     0"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name0}
expect_ok ${ZPOOL} destroy ${name0}
expect_fl ${ZPOOL} status -x ${name0}

expect_ok ${ZPOOL} create -f ${name0} raidz1 ${disk0} ${disk1}
expect_ok ${ZPOOL} offline ${name0} ${disk1}
exp=`(
  echo "  pool: ${name0}"
  echo " state: DEGRADED"
  echo "status: One or more devices has been taken offline by the administrator."
  echo "        Sufficient replicas exist for the pool to continue functioning in a"
  echo "        degraded state."
  echo "action: Online the device using 'zpool online' or replace the device with"
  echo "        'zpool replace'."
  echo " scrub: none requested"
  echo "config:"
  echo "	NAME          STATE     READ WRITE CKSUM"
  echo "	${name0}      DEGRADED     0     0     0"
  echo "	  raidz1      DEGRADED     0     0     0"
  echo "	    ${disk0}  ONLINE       0     0     0"
  echo "	    ${disk1}  OFFLINE      0     0     0"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name0}
expect_ok ${ZPOOL} destroy ${name0}
expect_fl ${ZPOOL} status -x ${name0}

expect_ok ${ZPOOL} create -f ${name0} raidz1 ${disk0} ${disk1}
expect_ok ${ZPOOL} offline ${name0} ${disk0}
expect_fl ${ZPOOL} offline ${name0} ${disk1}
exp=`(
  echo "  pool: ${name0}"
  echo " state: DEGRADED"
  echo "status: One or more devices has been taken offline by the administrator."
  echo "        Sufficient replicas exist for the pool to continue functioning in a"
  echo "        degraded state."
  echo "action: Online the device using 'zpool online' or replace the device with"
  echo "        'zpool replace'."
  echo " scrub: none requested"
  echo "config:"
  echo "	NAME          STATE     READ WRITE CKSUM"
  echo "	${name0}      DEGRADED     0     0     0"
  echo "	  raidz1      DEGRADED     0     0     0"
  echo "	    ${disk0}  OFFLINE      0     0     0"
  echo "	    ${disk1}  ONLINE       0     0     0"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name0}
expect_ok ${ZPOOL} destroy ${name0}
expect_fl ${ZPOOL} status -x ${name0}

expect_ok ${ZPOOL} create -f ${name0} raidz1 ${disk0} ${disk1} raidz1 ${disk2} ${disk3}
expect_ok ${ZPOOL} offline ${name0} ${disk0}
exp=`(
  echo "  pool: ${name0}"
  echo " state: DEGRADED"
  echo "status: One or more devices has been taken offline by the administrator."
  echo "        Sufficient replicas exist for the pool to continue functioning in a"
  echo "        degraded state."
  echo "action: Online the device using 'zpool online' or replace the device with"
  echo "        'zpool replace'."
  echo " scrub: none requested"
  echo "config:"
  echo "	NAME          STATE     READ WRITE CKSUM"
  echo "	${name0}      DEGRADED     0     0     0"
  echo "	  raidz1      DEGRADED     0     0     0"
  echo "	    ${disk0}  OFFLINE      0     0     0"
  echo "	    ${disk1}  ONLINE       0     0     0"
  echo "	  raidz1      ONLINE       0     0     0"
  echo "	    ${disk2}  ONLINE       0     0     0"
  echo "	    ${disk3}  ONLINE       0     0     0"
  echo "errors: No known data errors"
)`
expect_ok ${ZPOOL} offline ${name0} ${disk3}
expect_fl ${ZPOOL} offline ${name0} ${disk1}
expect_fl ${ZPOOL} offline ${name0} ${disk2}
exp=`(
  echo "  pool: ${name0}"
  echo " state: DEGRADED"
  echo "status: One or more devices has been taken offline by the administrator."
  echo "        Sufficient replicas exist for the pool to continue functioning in a"
  echo "        degraded state."
  echo "action: Online the device using 'zpool online' or replace the device with"
  echo "        'zpool replace'."
  echo " scrub: none requested"
  echo "config:"
  echo "	NAME          STATE     READ WRITE CKSUM"
  echo "	${name0}      DEGRADED     0     0     0"
  echo "	  raidz1      DEGRADED     0     0     0"
  echo "	    ${disk0}  OFFLINE      0     0     0"
  echo "	    ${disk1}  ONLINE       0     0     0"
  echo "	  raidz1      DEGRADED     0     0     0"
  echo "	    ${disk2}  ONLINE       0     0     0"
  echo "	    ${disk3}  OFFLINE      0     0     0"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name0}
expect_ok ${ZPOOL} destroy ${name0}
expect_fl ${ZPOOL} status -x ${name0}

expect_ok ${ZPOOL} create -f ${name0} raidz1 ${disk0} ${disk1} raidz1 ${disk2} ${disk3}
expect_ok ${ZPOOL} offline ${name0} ${disk1} ${disk2}
exp=`(
  echo "  pool: ${name0}"
  echo " state: DEGRADED"
  echo "status: One or more devices has been taken offline by the administrator."
  echo "        Sufficient replicas exist for the pool to continue functioning in a"
  echo "        degraded state."
  echo "action: Online the device using 'zpool online' or replace the device with"
  echo "        'zpool replace'."
  echo " scrub: none requested"
  echo "config:"
  echo "	NAME          STATE     READ WRITE CKSUM"
  echo "	${name0}      DEGRADED     0     0     0"
  echo "	  raidz1      DEGRADED     0     0     0"
  echo "	    ${disk0}  ONLINE       0     0     0"
  echo "	    ${disk1}  OFFLINE      0     0     0"
  echo "	  raidz1      DEGRADED     0     0     0"
  echo "	    ${disk2}  OFFLINE      0     0     0"
  echo "	    ${disk3}  ONLINE       0     0     0"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name0}
expect_ok ${ZPOOL} destroy ${name0}
expect_fl ${ZPOOL} status -x ${name0}

expect_ok ${ZPOOL} create -f ${name0} raidz1 ${disk0} ${disk1} ${disk2} ${disk3} ${disk4}
expect_ok ${ZPOOL} offline ${name0} ${disk1}
expect_fl ${ZPOOL} offline ${name0} ${disk3}
exp=`(
  echo "  pool: ${name0}"
  echo " state: DEGRADED"
  echo "status: One or more devices has been taken offline by the administrator."
  echo "        Sufficient replicas exist for the pool to continue functioning in a"
  echo "        degraded state."
  echo "action: Online the device using 'zpool online' or replace the device with"
  echo "        'zpool replace'."
  echo " scrub: none requested"
  echo "config:"
  echo "	NAME          STATE     READ WRITE CKSUM"
  echo "	${name0}      DEGRADED     0     0     0"
  echo "	  raidz1      DEGRADED     0     0     0"
  echo "	    ${disk0}  ONLINE       0     0     0"
  echo "	    ${disk1}  OFFLINE      0     0     0"
  echo "	    ${disk2}  ONLINE       0     0     0"
  echo "	    ${disk3}  ONLINE       0     0     0"
  echo "	    ${disk4}  ONLINE       0     0     0"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name0}
expect_ok ${ZPOOL} destroy ${name0}
expect_fl ${ZPOOL} status -x ${name0}

disks_destroy
