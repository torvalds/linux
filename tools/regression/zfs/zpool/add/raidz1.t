#!/bin/sh
# $FreeBSD$

dir=`dirname $0`
. ${dir}/../../misc.sh

echo "1..10"

disks_create 15
names_create 1

expect_ok ${ZPOOL} create ${name0} raidz1 ${disk0} ${disk1} ${disk2}
expect_ok ${ZPOOL} add ${name0} raidz1 ${disk3} ${disk4} ${disk5}
exp=`(
  echo "  pool: ${name0}"
  echo " state: ONLINE"
  echo " scrub: none requested"
  echo "config:"
  echo "	NAME          STATE   READ WRITE CKSUM"
  echo "	${name0}      ONLINE     0     0     0"
  echo "	  raidz1      ONLINE     0     0     0"
  echo "	    ${disk0}  ONLINE     0     0     0"
  echo "	    ${disk1}  ONLINE     0     0     0"
  echo "	    ${disk2}  ONLINE     0     0     0"
  echo "	  raidz1      ONLINE     0     0     0"
  echo "	    ${disk3}  ONLINE     0     0     0"
  echo "	    ${disk4}  ONLINE     0     0     0"
  echo "	    ${disk5}  ONLINE     0     0     0"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name0}
expect_ok ${ZPOOL} destroy ${name0}
expect_fl ${ZPOOL} status -x ${name0}

expect_ok ${ZPOOL} create ${name0} raidz1 ${disk0} ${disk1} ${disk2} raidz1 ${disk3} ${disk4} ${disk5}
expect_ok ${ZPOOL} add ${name0} raidz1 ${disk6} ${disk7} ${disk8} raidz1 ${disk9} ${disk10} ${disk11} raidz1 ${disk12} ${disk13} ${disk14}
exp=`(
  echo "  pool: ${name0}"
  echo " state: ONLINE"
  echo " scrub: none requested"
  echo "config:"
  echo "	NAME           STATE   READ WRITE CKSUM"
  echo "	${name0}       ONLINE     0     0     0"
  echo "	  raidz1       ONLINE     0     0     0"
  echo "	    ${disk0}   ONLINE     0     0     0"
  echo "	    ${disk1}   ONLINE     0     0     0"
  echo "	    ${disk2}   ONLINE     0     0     0"
  echo "	  raidz1       ONLINE     0     0     0"
  echo "	    ${disk3}   ONLINE     0     0     0"
  echo "	    ${disk4}   ONLINE     0     0     0"
  echo "	    ${disk5}   ONLINE     0     0     0"
  echo "	  raidz1       ONLINE     0     0     0"
  echo "	    ${disk6}   ONLINE     0     0     0"
  echo "	    ${disk7}   ONLINE     0     0     0"
  echo "	    ${disk8}   ONLINE     0     0     0"
  echo "	  raidz1       ONLINE     0     0     0"
  echo "	    ${disk9}   ONLINE     0     0     0"
  echo "	    ${disk10}  ONLINE     0     0     0"
  echo "	    ${disk11}  ONLINE     0     0     0"
  echo "	  raidz1       ONLINE     0     0     0"
  echo "	    ${disk12}  ONLINE     0     0     0"
  echo "	    ${disk13}  ONLINE     0     0     0"
  echo "	    ${disk14}  ONLINE     0     0     0"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name0}
expect_ok ${ZPOOL} destroy ${name0}
expect_fl ${ZPOOL} status -x ${name0}

disks_destroy
