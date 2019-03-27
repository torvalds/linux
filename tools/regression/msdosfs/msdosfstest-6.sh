#!/bin/sh
# $FreeBSD$
# A really simple script to create a swap-backed msdosfs filesystem, then
# test to make sure the nmount conversion(mount_msdosfs.c rev 1.37)
# doesn't break multi-byte characters.

mkdir /tmp/msdosfstest/
mdconfig -a -t swap -s 128m -u 10
bsdlabel -w md10 auto
newfs_msdos -F 32 -b 8192 /dev/md10a
mount_msdosfs -L zh_TW.Big5 -D CP950 /dev/md10a /tmp/msdosfstest/
# The comment is UTF-8, the actual command uses the Big5 representation.
# mkdir /tmp/msdosfstest/是否看過坊間常見的許茹芸淚海慶功宴吃蓋飯第四集
subdir=$'\254\117\247\137\254\335\271\114\247\173\266\241\261\140\250\243'\
$'\252\272\263\134\257\370\252\345\262\134\256\374\274\171\245\134'\
$'\256\142\246\131\273\134\266\272\262\304\245\174\266\260'
mkdir /tmp/msdosfstest/$subdir
cd /tmp/msdosfstest/$subdir
if [ $? -eq 0 ]; then
	echo "ok 6";
else
	echo "not ok 6";
fi
cd /tmp
umount /tmp/msdosfstest/
mdconfig -d -u 10
rm -rf /tmp/msdosfstest/
