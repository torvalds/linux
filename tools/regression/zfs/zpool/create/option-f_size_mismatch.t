#!/bin/sh
# $FreeBSD$

dir=`dirname $0`
. ${dir}/../../misc.sh

echo "1..104"

disks_create 1 64M
disks_create 4
disks_create 3 64M
files_create 1 64M
files_create 4
files_create 3 64M
names_create 1

expect_ok ${ZPOOL} create ${name0} ${disk0} ${disk1}
expect_ok ${ZPOOL} status -x ${name0}
expect_ok ${ZPOOL} destroy ${name0}

expect_ok ${ZPOOL} create ${name0} ${file0} ${file1}
expect_ok ${ZPOOL} status -x ${name0}
expect_ok ${ZPOOL} destroy ${name0}

expect_ok ${ZPOOL} create ${name0} mirror ${disk1} ${disk2} mirror ${disk0} ${disk5}
expect_ok ${ZPOOL} status -x ${name0}
expect_ok ${ZPOOL} destroy ${name0}

expect_ok ${ZPOOL} create ${name0} mirror ${file1} ${file2} mirror ${file0} ${file5}
expect_ok ${ZPOOL} status -x ${name0}
expect_ok ${ZPOOL} destroy ${name0}

expect_ok ${ZPOOL} create ${name0} raidz1 ${disk1} ${disk2} ${disk3} raidz1 ${disk0} ${disk5} ${disk6}
expect_ok ${ZPOOL} status -x ${name0}
expect_ok ${ZPOOL} destroy ${name0}

expect_ok ${ZPOOL} create ${name0} raidz1 ${file1} ${file2} ${file3} raidz1 ${file0} ${file5} ${file6}
expect_ok ${ZPOOL} status -x ${name0}
expect_ok ${ZPOOL} destroy ${name0}

expect_ok ${ZPOOL} create ${name0} raidz2 ${disk1} ${disk2} ${disk3} ${disk4} raidz2 ${disk0} ${disk5} ${disk6} ${disk7}
expect_ok ${ZPOOL} status -x ${name0}
expect_ok ${ZPOOL} destroy ${name0}

expect_ok ${ZPOOL} create ${name0} raidz2 ${file1} ${file2} ${file3} ${file4} raidz2 ${file0} ${file5} ${file6} ${file7}
expect_ok ${ZPOOL} status -x ${name0}
expect_ok ${ZPOOL} destroy ${name0}

expect_fl ${ZPOOL} create ${name0} mirror ${disk0} ${disk1}
expect_fl ${ZPOOL} status -x ${name0}
expect_fl ${ZPOOL} destroy ${name0}

expect_ok ${ZPOOL} create ${zpool_f_flag} ${name0} mirror ${disk0} ${disk1}
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

expect_fl ${ZPOOL} create ${name0} mirror ${file0} ${file1}
expect_fl ${ZPOOL} status -x ${name0}
expect_fl ${ZPOOL} destroy ${name0}

expect_ok ${ZPOOL} create ${zpool_f_flag} ${name0} mirror ${file0} ${file1}
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
  echo "	    ${file0}  ONLINE     0     0     0"
  echo "	    ${file1}  ONLINE     0     0     0"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name0}
expect_ok ${ZPOOL} destroy ${name0}
expect_fl ${ZPOOL} status -x ${name0}
expect_fl ${ZPOOL} destroy ${name0}

expect_fl ${ZPOOL} create ${name0} raidz1 ${disk0} ${disk1} ${disk2}
expect_fl ${ZPOOL} status -x ${name0}
expect_fl ${ZPOOL} destroy ${name0}

expect_ok ${ZPOOL} create ${zpool_f_flag} ${name0} raidz1 ${disk0} ${disk1} ${disk2}
expect_ok ${ZPOOL} status -x ${name0}
expect "pool '${name0}' is healthy" ${ZPOOL} status -x ${name0}
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
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name0}
expect_ok ${ZPOOL} destroy ${name0}
expect_fl ${ZPOOL} status -x ${name0}
expect_fl ${ZPOOL} destroy ${name0}

expect_fl ${ZPOOL} create ${name0} raidz1 ${file0} ${file1} ${file2}
expect_fl ${ZPOOL} status -x ${name0}
expect_fl ${ZPOOL} destroy ${name0}

expect_ok ${ZPOOL} create ${zpool_f_flag} ${name0} raidz1 ${file0} ${file1} ${file2}
expect_ok ${ZPOOL} status -x ${name0}
expect "pool '${name0}' is healthy" ${ZPOOL} status -x ${name0}
exp=`(
  echo "  pool: ${name0}"
  echo " state: ONLINE"
  echo " scrub: none requested"
  echo "config:"
  echo "	NAME          STATE   READ WRITE CKSUM"
  echo "	${name0}      ONLINE     0     0     0"
  echo "	  raidz1      ONLINE     0     0     0"
  echo "	    ${file0}  ONLINE     0     0     0"
  echo "	    ${file1}  ONLINE     0     0     0"
  echo "	    ${file2}  ONLINE     0     0     0"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name0}
expect_ok ${ZPOOL} destroy ${name0}
expect_fl ${ZPOOL} status -x ${name0}
expect_fl ${ZPOOL} destroy ${name0}

expect_fl ${ZPOOL} create ${name0} raidz2 ${disk0} ${disk1} ${disk2} ${disk3}
expect_fl ${ZPOOL} status -x ${name0}
expect_fl ${ZPOOL} destroy ${name0}

expect_ok ${ZPOOL} create ${zpool_f_flag} ${name0} raidz2 ${disk0} ${disk1} ${disk2} ${disk3}
expect_ok ${ZPOOL} status -x ${name0}
expect "pool '${name0}' is healthy" ${ZPOOL} status -x ${name0}
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
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name0}
expect_ok ${ZPOOL} destroy ${name0}
expect_fl ${ZPOOL} status -x ${name0}
expect_fl ${ZPOOL} destroy ${name0}

expect_fl ${ZPOOL} create ${name0} raidz2 ${file0} ${file1} ${file2} ${file3}
expect_fl ${ZPOOL} status -x ${name0}
expect_fl ${ZPOOL} destroy ${name0}

expect_ok ${ZPOOL} create ${zpool_f_flag} ${name0} raidz2 ${file0} ${file1} ${file2} ${file3}
expect_ok ${ZPOOL} status -x ${name0}
expect "pool '${name0}' is healthy" ${ZPOOL} status -x ${name0}
exp=`(
  echo "  pool: ${name0}"
  echo " state: ONLINE"
  echo " scrub: none requested"
  echo "config:"
  echo "	NAME          STATE   READ WRITE CKSUM"
  echo "	${name0}      ONLINE     0     0     0"
  echo "	  raidz2      ONLINE     0     0     0"
  echo "	    ${file0}  ONLINE     0     0     0"
  echo "	    ${file1}  ONLINE     0     0     0"
  echo "	    ${file2}  ONLINE     0     0     0"
  echo "	    ${file3}  ONLINE     0     0     0"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name0}
expect_ok ${ZPOOL} destroy ${name0}
expect_fl ${ZPOOL} status -x ${name0}
expect_fl ${ZPOOL} destroy ${name0}

add_msg="# TODO Sun CR 6726091, Lustre bug 16873"
expect_fl ${ZPOOL} create ${name0} ${disk1} log mirror ${disk0} ${disk2}
expect_fl ${ZPOOL} status -x ${name0}
expect_fl ${ZPOOL} destroy ${name0}
add_msg=""

expect_ok ${ZPOOL} create ${zpool_f_flag} ${name0} ${disk1} log mirror ${disk0} ${disk2}
expect_ok ${ZPOOL} status -x ${name0}
expect "pool '${name0}' is healthy" ${ZPOOL} status -x ${name0}
exp=`(
  echo "  pool: ${name0}"
  echo " state: ONLINE"
  echo " scrub: none requested"
  echo "config:"
  echo "	NAME          STATE   READ WRITE CKSUM"
  echo "	${name0}      ONLINE     0     0     0"
  echo "	  ${disk1}    ONLINE     0     0     0"
  echo "	logs          ONLINE     0     0     0"
  echo "	  mirror      ONLINE     0     0     0"
  echo "	    ${disk0}  ONLINE     0     0     0"
  echo "	    ${disk2}  ONLINE     0     0     0"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name0}
expect_ok ${ZPOOL} destroy ${name0}
expect_fl ${ZPOOL} status -x ${name0}
expect_fl ${ZPOOL} destroy ${name0}

add_msg="# TODO Sun CR 6726091, Lustre bug 16873"
expect_fl ${ZPOOL} create ${name0} ${file1} log mirror ${file0} ${file2}
expect_fl ${ZPOOL} status -x ${name0}
expect_fl ${ZPOOL} destroy ${name0}
add_msg=""

expect_ok ${ZPOOL} create ${zpool_f_flag} ${name0} ${file1} log mirror ${file0} ${file2}
expect_ok ${ZPOOL} status -x ${name0}
expect "pool '${name0}' is healthy" ${ZPOOL} status -x ${name0}
exp=`(
  echo "  pool: ${name0}"
  echo " state: ONLINE"
  echo " scrub: none requested"
  echo "config:"
  echo "	NAME          STATE   READ WRITE CKSUM"
  echo "	${name0}      ONLINE     0     0     0"
  echo "	  ${file1}    ONLINE     0     0     0"
  echo "	logs          ONLINE     0     0     0"
  echo "	  mirror      ONLINE     0     0     0"
  echo "	    ${file0}  ONLINE     0     0     0"
  echo "	    ${file2}  ONLINE     0     0     0"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name0}
expect_ok ${ZPOOL} destroy ${name0}
expect_fl ${ZPOOL} status -x ${name0}
expect_fl ${ZPOOL} destroy ${name0}

disks_destroy
files_destroy
