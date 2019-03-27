#!/bin/sh
# $FreeBSD$

dir=`dirname $0`
. ${dir}/../../misc.sh

echo "1..10"

disks_create 20
names_create 1

expect_ok ${ZPOOL} create ${name0} raidz2 ${disk0} ${disk1} ${disk2} ${disk3}
expect_ok ${ZPOOL} add ${name0} raidz2 ${disk4} ${disk5} ${disk6} ${disk7}
exp=`(
  echo "  pool: ${name0}"
  echo " state: ONLINE"
  echo " scrub: none requested"
  echo "config:"
  echo "	NAME          STATE   READ WRITE CKSUM"
  echo "	${name0}      ONLINE     0     0     0"
  echo "	  raidz2      ONLINE     0     0     0"
  echo "	    ${disk0}  ONLINE     0     0     0"
  echo "	    ${disk1}  ONLINE     0     0     0"
  echo "	    ${disk2}  ONLINE     0     0     0"
  echo "	    ${disk3}  ONLINE     0     0     0"
  echo "	  raidz2      ONLINE     0     0     0"
  echo "	    ${disk4}  ONLINE     0     0     0"
  echo "	    ${disk5}  ONLINE     0     0     0"
  echo "	    ${disk6}  ONLINE     0     0     0"
  echo "	    ${disk7}  ONLINE     0     0     0"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name0}
expect_ok ${ZPOOL} destroy ${name0}
expect_fl ${ZPOOL} status -x ${name0}

expect_ok ${ZPOOL} create ${name0} raidz2 ${disk0} ${disk1} ${disk2} ${disk3} raidz2 ${disk4} ${disk5} ${disk6} ${disk7}
expect_ok ${ZPOOL} add ${name0} raidz2 ${disk8} ${disk9} ${disk10} ${disk11} raidz2 ${disk12} ${disk13} ${disk14} ${disk15} raidz2 ${disk16} ${disk17} ${disk18} ${disk19}
exp=`(
  echo "  pool: ${name0}"
  echo " state: ONLINE"
  echo " scrub: none requested"
  echo "config:"
  echo "	NAME           STATE   READ WRITE CKSUM"
  echo "	${name0}       ONLINE     0     0     0"
  echo "	  raidz2       ONLINE     0     0     0"
  echo "	    ${disk0}   ONLINE     0     0     0"
  echo "	    ${disk1}   ONLINE     0     0     0"
  echo "	    ${disk2}   ONLINE     0     0     0"
  echo "	    ${disk3}   ONLINE     0     0     0"
  echo "	  raidz2       ONLINE     0     0     0"
  echo "	    ${disk4}   ONLINE     0     0     0"
  echo "	    ${disk5}   ONLINE     0     0     0"
  echo "	    ${disk6}   ONLINE     0     0     0"
  echo "	    ${disk7}   ONLINE     0     0     0"
  echo "	  raidz2       ONLINE     0     0     0"
  echo "	    ${disk8}   ONLINE     0     0     0"
  echo "	    ${disk9}   ONLINE     0     0     0"
  echo "	    ${disk10}  ONLINE     0     0     0"
  echo "	    ${disk11}  ONLINE     0     0     0"
  echo "	  raidz2       ONLINE     0     0     0"
  echo "	    ${disk12}  ONLINE     0     0     0"
  echo "	    ${disk13}  ONLINE     0     0     0"
  echo "	    ${disk14}  ONLINE     0     0     0"
  echo "	    ${disk15}  ONLINE     0     0     0"
  echo "	  raidz2       ONLINE     0     0     0"
  echo "	    ${disk16}  ONLINE     0     0     0"
  echo "	    ${disk17}  ONLINE     0     0     0"
  echo "	    ${disk18}  ONLINE     0     0     0"
  echo "	    ${disk19}  ONLINE     0     0     0"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name0}
expect_ok ${ZPOOL} destroy ${name0}
expect_fl ${ZPOOL} status -x ${name0}

disks_destroy
