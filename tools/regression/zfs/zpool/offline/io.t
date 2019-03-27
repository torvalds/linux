#!/bin/sh
# $FreeBSD$

dir=`dirname $0`
. ${dir}/../../misc.sh

echo "1..31"

disks_create 4 128M
names_create 1

expect_ok ${ZPOOL} create ${name0} mirror ${disk0} ${disk1}
expect_ok ${ZPOOL} offline ${name0} ${disk0}
sum0_before=`calcsum ${fdisk0}`
sum1_before=`calcsum ${fdisk1}`
${ZFS} snapshot ${name0}@test
sum0_after=`calcsum ${fdisk0}`
sum1_after=`calcsum ${fdisk1}`
expect_ok test "${sum0_before}" = "${sum0_after}"
expect_fl test "${sum1_before}" = "${sum1_after}"
expect_ok ${ZPOOL} destroy ${name0}
expect_fl ${ZPOOL} status -x ${name0}

expect_ok ${ZPOOL} create ${zpool_f_flag} ${name0} mirror ${disk0} ${disk1} ${disk2} ${disk3}
expect_ok ${ZPOOL} offline ${name0} ${disk1}
add_msg="# TODO Sun CR 6328632, Lustre bug 16878"
expect_ok ${ZPOOL} offline ${name0} ${disk3}
add_msg=""
sum0_before=`calcsum ${fdisk0}`
sum1_before=`calcsum ${fdisk1}`
sum2_before=`calcsum ${fdisk2}`
sum3_before=`calcsum ${fdisk3}`
${ZFS} snapshot ${name0}@test
sum0_after=`calcsum ${fdisk0}`
sum1_after=`calcsum ${fdisk1}`
sum2_after=`calcsum ${fdisk2}`
sum3_after=`calcsum ${fdisk3}`
expect_fl test "${sum0_before}" = "${sum0_after}"
expect_ok test "${sum1_before}" = "${sum1_after}"
expect_fl test "${sum2_before}" = "${sum2_after}"
add_msg="# TODO Sun CR 6328632, Lustre bug 16878"
expect_ok test "${sum3_before}" = "${sum3_after}"
add_msg=""
expect_ok ${ZPOOL} destroy ${name0}
expect_fl ${ZPOOL} status -x ${name0}

expect_ok ${ZPOOL} create ${zpool_f_flag} ${name0} raidz1 ${disk0} ${disk1} ${disk2}
expect_ok ${ZPOOL} offline ${name0} ${disk1}
sum0_before=`calcsum ${fdisk0}`
sum1_before=`calcsum ${fdisk1}`
sum2_before=`calcsum ${fdisk2}`
${ZFS} snapshot ${name0}@test
sum0_after=`calcsum ${fdisk0}`
sum1_after=`calcsum ${fdisk1}`
sum2_after=`calcsum ${fdisk2}`
expect_fl test "${sum0_before}" = "${sum0_after}"
expect_ok test "${sum1_before}" = "${sum1_after}"
expect_fl test "${sum2_before}" = "${sum2_after}"
expect_ok ${ZPOOL} destroy ${name0}
expect_fl ${ZPOOL} status -x ${name0}

expect_ok ${ZPOOL} create ${zpool_f_flag} ${name0} raidz2 ${disk0} ${disk1} ${disk2} ${disk3}
expect_ok ${ZPOOL} offline ${name0} ${disk1}
add_msg="# TODO Sun CR 6328632, Lustre bug 16878"
expect_ok ${ZPOOL} offline ${name0} ${disk3}
add_msg=""
sum0_before=`calcsum ${fdisk0}`
sum1_before=`calcsum ${fdisk1}`
sum2_before=`calcsum ${fdisk2}`
sum3_before=`calcsum ${fdisk3}`
${ZFS} snapshot ${name0}@test
sum0_after=`calcsum ${fdisk0}`
sum1_after=`calcsum ${fdisk1}`
sum2_after=`calcsum ${fdisk2}`
sum3_after=`calcsum ${fdisk3}`
expect_fl test "${sum0_before}" = "${sum0_after}"
expect_ok test "${sum1_before}" = "${sum1_after}"
expect_fl test "${sum2_before}" = "${sum2_after}"
add_msg="# TODO Sun CR 6328632, Lustre bug 16878"
expect_ok test "${sum3_before}" = "${sum3_after}"
add_msg=""
expect_ok ${ZPOOL} destroy ${name0}
expect_fl ${ZPOOL} status -x ${name0}

disks_destroy
