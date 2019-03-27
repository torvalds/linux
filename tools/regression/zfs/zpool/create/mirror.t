#!/bin/sh
# $FreeBSD$

dir=`dirname $0`
. ${dir}/../../misc.sh

echo "1..22"

disks_create 6
names_create 1

expect_fl ${ZPOOL} create ${name0} mirror ${disk0}

expect_ok ${ZPOOL} create ${name0} mirror ${disk0} ${disk1}
expect_ok ${ZPOOL} status -x ${name0}
expect "pool '${name0}' is healthy" ${ZPOOL} status -x ${name0}
exp=`(
  echo "  pool: ${name0}"
  echo " state: ONLINE"
  echo " scrub: none requested"
  echo "config:"
  echo "	NAME          STATE   READ WRITE CKSUM"
  echo "	${name0}      ONLINE     0     0     0"
  echo "	  mirror      ONLINE     0     0     0"
  echo "	    ${disk0}  ONLINE     0     0     0"
  echo "	    ${disk1}  ONLINE     0     0     0"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name0}
expect_ok ${ZPOOL} destroy ${name0}
expect_fl ${ZPOOL} status -x ${name0}
expect_fl ${ZPOOL} destroy ${name0}

expect_ok ${ZPOOL} create ${name0} mirror ${disk0} ${disk1} ${disk2} ${disk3} ${disk4}
expect_ok ${ZPOOL} status -x ${name0}
expect "pool '${name0}' is healthy" ${ZPOOL} status -x ${name0}
exp=`(
  echo "  pool: ${name0}"
  echo " state: ONLINE"
  echo " scrub: none requested"
  echo "config:"
  echo "	NAME          STATE   READ WRITE CKSUM"
  echo "	${name0}      ONLINE     0     0     0"
  echo "	  mirror      ONLINE     0     0     0"
  echo "	    ${disk0}  ONLINE     0     0     0"
  echo "	    ${disk1}  ONLINE     0     0     0"
  echo "	    ${disk2}  ONLINE     0     0     0"
  echo "	    ${disk3}  ONLINE     0     0     0"
  echo "	    ${disk4}  ONLINE     0     0     0"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name0}
expect_ok ${ZPOOL} destroy ${name0}
expect_fl ${ZPOOL} status -x ${name0}
expect_fl ${ZPOOL} destroy ${name0}

expect_ok ${ZPOOL} create ${name0} mirror ${disk0} ${disk1} mirror ${disk2} ${disk3} mirror ${disk4} ${disk5}
expect_ok ${ZPOOL} status -x ${name0}
expect "pool '${name0}' is healthy" ${ZPOOL} status -x ${name0}
exp=`(
  echo "  pool: ${name0}"
  echo " state: ONLINE"
  echo " scrub: none requested"
  echo "config:"
  echo "	NAME          STATE   READ WRITE CKSUM"
  echo "	${name0}      ONLINE     0     0     0"
  echo "	  mirror      ONLINE     0     0     0"
  echo "	    ${disk0}  ONLINE     0     0     0"
  echo "	    ${disk1}  ONLINE     0     0     0"
  echo "	  mirror      ONLINE     0     0     0"
  echo "	    ${disk2}  ONLINE     0     0     0"
  echo "	    ${disk3}  ONLINE     0     0     0"
  echo "	  mirror      ONLINE     0     0     0"
  echo "	    ${disk4}  ONLINE     0     0     0"
  echo "	    ${disk5}  ONLINE     0     0     0"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name0}
expect_ok ${ZPOOL} destroy ${name0}
expect_fl ${ZPOOL} status -x ${name0}
expect_fl ${ZPOOL} destroy ${name0}

disks_destroy
