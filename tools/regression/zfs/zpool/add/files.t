#!/bin/sh
# $FreeBSD$

dir=`dirname $0`
. ${dir}/../../misc.sh

echo "1..54"

files_create 8
names_create 1

expect_ok ${ZPOOL} create ${name0} ${file0}
expect_fl ${ZPOOL} add ${name0} ${file0}
exp=`(
  echo "  pool: ${name0}"
  echo " state: ONLINE"
  echo " scrub: none requested"
  echo "config:"
  echo "	NAME        STATE   READ WRITE CKSUM"
  echo "	${name0}    ONLINE     0     0     0"
  echo "	  ${file0}  ONLINE     0     0     0"
  echo "errors: No known data errors"
)`
expect_ok ${ZPOOL} destroy ${name0}
expect_fl ${ZPOOL} status -x ${name0}

expect_ok ${ZPOOL} create ${name0} ${file0} ${file1}
expect_fl ${ZPOOL} add ${name0} ${file0}
expect_fl ${ZPOOL} add ${name0} ${file1}
exp=`(
  echo "  pool: ${name0}"
  echo " state: ONLINE"
  echo " scrub: none requested"
  echo "config:"
  echo "	NAME        STATE   READ WRITE CKSUM"
  echo "	${name0}    ONLINE     0     0     0"
  echo "	  ${file0}  ONLINE     0     0     0"
  echo "errors: No known data errors"
)`
expect_ok ${ZPOOL} destroy ${name0}
expect_fl ${ZPOOL} status -x ${name0}

expect_ok ${ZPOOL} create ${name0} ${file0}
expect_ok ${ZPOOL} add ${name0} ${file1}
exp=`(
  echo "  pool: ${name0}"
  echo " state: ONLINE"
  echo " scrub: none requested"
  echo "config:"
  echo "	NAME        STATE   READ WRITE CKSUM"
  echo "	${name0}    ONLINE     0     0     0"
  echo "	  ${file0}  ONLINE     0     0     0"
  echo "	  ${file1}  ONLINE     0     0     0"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name0}
expect_ok ${ZPOOL} destroy ${name0}
expect_fl ${ZPOOL} status -x ${name0}

expect_ok ${ZPOOL} create ${name0} ${file0} ${file1} ${file2}
expect_ok ${ZPOOL} add ${name0} ${file3} ${file4}
exp=`(
  echo "  pool: ${name0}"
  echo " state: ONLINE"
  echo " scrub: none requested"
  echo "config:"
  echo "	NAME        STATE   READ WRITE CKSUM"
  echo "	${name0}    ONLINE     0     0     0"
  echo "	  ${file0}  ONLINE     0     0     0"
  echo "	  ${file1}  ONLINE     0     0     0"
  echo "	  ${file2}  ONLINE     0     0     0"
  echo "	  ${file3}  ONLINE     0     0     0"
  echo "	  ${file4}  ONLINE     0     0     0"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name0}
expect_ok ${ZPOOL} destroy ${name0}
expect_fl ${ZPOOL} status -x ${name0}

expect_ok ${ZPOOL} create ${name0} mirror ${file0} ${file1}
expect_ok ${ZPOOL} add ${name0} mirror ${file2} ${file3}
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
  echo "	  mirror      ONLINE     0     0     0"
  echo "	    ${file2}  ONLINE     0     0     0"
  echo "	    ${file3}  ONLINE     0     0     0"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name0}
expect_ok ${ZPOOL} destroy ${name0}
expect_fl ${ZPOOL} status -x ${name0}

expect_ok ${ZPOOL} create ${name0} raidz1 ${file0} ${file1} ${file2}
expect_ok ${ZPOOL} add ${name0} raidz1 ${file3} ${file4} ${file5}
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
  echo "	  raidz1      ONLINE     0     0     0"
  echo "	    ${file3}  ONLINE     0     0     0"
  echo "	    ${file4}  ONLINE     0     0     0"
  echo "	    ${file5}  ONLINE     0     0     0"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name0}
expect_ok ${ZPOOL} destroy ${name0}
expect_fl ${ZPOOL} status -x ${name0}

expect_ok ${ZPOOL} create ${name0} raidz2 ${file0} ${file1} ${file2} ${file3}
expect_ok ${ZPOOL} add ${name0} raidz2 ${file4} ${file5} ${file6} ${file7}
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
  echo "	  raidz2      ONLINE     0     0     0"
  echo "	    ${file4}  ONLINE     0     0     0"
  echo "	    ${file5}  ONLINE     0     0     0"
  echo "	    ${file6}  ONLINE     0     0     0"
  echo "	    ${file7}  ONLINE     0     0     0"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name0}
expect_ok ${ZPOOL} destroy ${name0}
expect_fl ${ZPOOL} status -x ${name0}

expect_ok ${ZPOOL} create ${name0} mirror ${file0} ${file1}
expect_ok ${ZPOOL} add ${name0} spare ${file2} ${file3}
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
  echo "	spares"
  echo "	  ${file2}    AVAIL"
  echo "	  ${file3}    AVAIL"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name0}
expect_ok ${ZPOOL} destroy ${name0}
expect_fl ${ZPOOL} status -x ${name0}

expect_ok ${ZPOOL} create ${name0} mirror ${file0} ${file1}
expect_ok ${ZPOOL} add ${name0} log ${file2} ${file3}
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
  echo "	logs          ONLINE     0     0     0"
  echo "	  ${file2}    ONLINE     0     0     0"
  echo "	  ${file3}    ONLINE     0     0     0"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name0}
expect_ok ${ZPOOL} destroy ${name0}
expect_fl ${ZPOOL} status -x ${name0}

expect_ok ${ZPOOL} create ${name0} mirror ${file0} ${file1}
expect_ok ${ZPOOL} add ${name0} log mirror ${file2} ${file3}
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
  echo "	logs          ONLINE     0     0     0"
  echo "	  mirror      ONLINE     0     0     0"
  echo "	    ${file2}  ONLINE     0     0     0"
  echo "	    ${file3}  ONLINE     0     0     0"
  echo "errors: No known data errors"
)`
expect "${exp}" ${ZPOOL} status ${name0}
expect_ok ${ZPOOL} destroy ${name0}
expect_fl ${ZPOOL} status -x ${name0}

expect_ok ${ZPOOL} create ${name0} mirror ${file0} ${file1}
expect_fl ${ZPOOL} add ${name0} cache ${file2} ${file3}
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

files_destroy
