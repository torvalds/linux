#!/bin/sh
# $FreeBSD$

today=`date '+%Y%m%d'`

I32="/usr/obj/Rescue/rescue_${today}_x32.img"
I64="/usr/obj/Rescue/rescue_${today}_x64.img"
IAL="/usr/obj/Rescue/rescue_${today}_xal.img"
D64="/usr/obj/nanobsd.rescue_amd64"
MNT="/usr/obj/Rescue/_mnt"

if [ \! -d "$MNT" ]; then
  mkdir "$MNT"
fi

dd if=${I32} of=${IAL} bs=128k
MD=`mdconfig -a -t vnode -f ${IAL}`

dd if=${D64}/_.disk.image of=/dev/${MD}s2 bs=128k
tunefs -L rescues2a /dev/${MD}s2a
mount /dev/${MD}s2a ${MNT}

sed -i "" -e 's/rescues1/rescues2/' ${MNT}/conf/base/etc/fstab
sed -i "" -e 's/rescues1/rescues2/' ${MNT}/etc/fstab

umount ${MNT}

mdconfig -d -u ${MD}
