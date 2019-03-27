#!/bin/sh

# $FreeBSD$

passphrase=passphrase
iterations=50000

# The smallest FAT32 filesystem is 33292 KB
espsize=33292

#
# Builds all the bat-shit crazy combinations we support booting from,
# at least for amd64. It assume you have a ~sane kernel in /boot/kernel
# and copies that into the ~150MB root images we create (we create the du
# size of the kernel + 20MB
#
# Sad panda sez: this runs as root, but could be userland if someone
# creates userland geli and zfs tools.
#
# This assumes an external program install-boot.sh which will install
# the appropriate boot files in the appropriate locations.
#
# These images assume ada0 will be the root image. We should likely
# use labels, but we don't.
#
# Assumes you've already rebuilt... maybe bad? Also maybe bad: the env
# vars should likely be conditionally set to allow better automation.
#

cpsys() {
    src=$1
    dst=$2

    # Copy kernel + boot loader
    (cd $src ; tar cf - .) | (cd $dst; tar xf -)
}

mk_nogeli_gpt_ufs_legacy() {
    src=$1
    img=$2

    cat > ${src}/etc/fstab <<EOF
/dev/ada0p2	/		ufs	rw	1	1
EOF
    makefs -t ffs -B little -s 200m ${img}.p2 ${src}
    mkimg -s gpt -b ${src}/boot/pmbr \
	  -p freebsd-boot:=${src}/boot/gptboot \
	  -p freebsd-ufs:=${img}.p2 -o ${img}
    rm -f ${src}/etc/fstab
}

mk_nogeli_gpt_ufs_uefi() {
    src=$1
    img=$2

    cat > ${src}/etc/fstab <<EOF
/dev/ada0p2	/		ufs	rw	1	1
EOF
    make_esp_file ${img}.p1 ${espsize} ${src}
    makefs -t ffs -B little -s 200m ${img}.p2 ${src}
    mkimg -s gpt \
	  -p efi:=${img}.p1 \
	  -p freebsd-ufs:=${img}.p2 -o ${img}
    rm -f ${src}/etc/fstab
}

mk_nogeli_gpt_ufs_both() {
    src=$1
    img=$2

    cat > ${src}/etc/fstab <<EOF
/dev/ada0p3	/		ufs	rw	1	1
EOF
    make_esp_file ${img}.p1 ${espsize} ${src}
    makefs -t ffs -B little -s 200m ${img}.p3 ${src}
    # p1 is boot for uefi, p2 is boot for gpt, p3 is /
    mkimg -b ${src}/boot/pmbr -s gpt \
	  -p efi:=${img}.p1 \
	  -p freebsd-boot:=${src}/boot/gptboot \
	  -p freebsd-ufs:=${img}.p3 \
	  -o ${img}
    rm -f ${src}/etc/fstab
}

mk_nogeli_gpt_zfs_legacy() {
    src=$1
    img=$2
    mntpt=$3
    geli=$4
    scheme=$5
    fs=$6
    bios=$7
    pool=nogeli-gpt-zfs-legacy

    dd if=/dev/zero of=${img} count=1 seek=$((200 * 1024 * 1024 / 512))
    md=$(mdconfig -f ${img})
    gpart create -s gpt ${md}
    gpart add -t freebsd-boot -s 400k -a 4k	${md}	# <= ~540k
    gpart add -t freebsd-zfs -l root $md
    # install-boot will make this bootable
    zpool create -O mountpoint=none -R ${mntpt} ${pool} ${md}p2
    zpool set bootfs=${pool} ${pool}
    zfs create -po mountpoint=/ ${pool}/ROOT/default
    # NB: The online guides go nuts customizing /var and other mountpoints here, no need
    cpsys ${src} ${mntpt}
    # need to make a couple of tweaks
    cat >> ${mntpt}/boot/loader.conf <<EOF
zfs_load=YES
opensolaris_load=YES
EOF
    cp /boot/kernel/zfs.ko ${mntpt}/boot/kernel/zfs.ko
    cp /boot/kernel/opensolaris.ko ${mntpt}/boot/kernel/opensolaris.ko
    # end tweaks
    zfs umount -f ${pool}/ROOT/default
    zfs set mountpoint=none ${pool}/ROOT/default
    zpool set bootfs=${pool}/ROOT/default ${pool}
    zpool set autoexpand=on ${pool}
    zpool export ${pool}
    ${SRCTOP}/tools/boot/install-boot.sh -g ${geli} -s ${scheme} -f ${fs} -b ${bios} -d ${src} ${md}
    mdconfig -d -u ${md}
}

mk_nogeli_gpt_zfs_uefi() {
    src=$1
    img=$2
    mntpt=$3
    geli=$4
    scheme=$5
    fs=$6
    bios=$7
    pool=nogeli-gpt-zfs-uefi

    dd if=/dev/zero of=${img} count=1 seek=$((200 * 1024 * 1024 / 512))
    md=$(mdconfig -f ${img})
    gpart create -s gpt ${md}
    gpart add -t efi -s ${espsize}k -a 4k ${md}
    gpart add -t freebsd-zfs -l root $md
    # install-boot will make this bootable
    zpool create -O mountpoint=none -R ${mntpt} ${pool} ${md}p2
    zpool set bootfs=${pool} ${pool}
    zfs create -po mountpoint=/ ${pool}/ROOT/default
    # NB: The online guides go nuts customizing /var and other mountpoints here, no need
    cpsys ${src} ${mntpt}
    # need to make a couple of tweaks
    cat >> ${mntpt}/boot/loader.conf <<EOF
zfs_load=YES
opensolaris_load=YES
EOF
    cp /boot/kernel/zfs.ko ${mntpt}/boot/kernel/zfs.ko
    cp /boot/kernel/opensolaris.ko ${mntpt}/boot/kernel/opensolaris.ko
    # end tweaks
    zfs umount -f ${pool}/ROOT/default
    zfs set mountpoint=none ${pool}/ROOT/default
    zpool set bootfs=${pool}/ROOT/default ${pool}
    zpool set autoexpand=on ${pool}
    zpool export ${pool}
    ${SRCTOP}/tools/boot/install-boot.sh -g ${geli} -s ${scheme} -f ${fs} -b ${bios} -d ${src} ${md}
    mdconfig -d -u ${md}
}

mk_nogeli_gpt_zfs_both() {
    src=$1
    img=$2
    mntpt=$3
    geli=$4
    scheme=$5
    fs=$6
    bios=$7
    pool=nogeli-gpt-zfs-both

    dd if=/dev/zero of=${img} count=1 seek=$((200 * 1024 * 1024 / 512))
    md=$(mdconfig -f ${img})
    gpart create -s gpt ${md}
    gpart add -t efi -s ${espsize}k -a 4k ${md}
    gpart add -t freebsd-boot -s 400k -a 4k	${md}	# <= ~540k
    gpart add -t freebsd-zfs -l root $md
    # install-boot will make this bootable
    zpool create -O mountpoint=none -R ${mntpt} ${pool} ${md}p3
    zpool set bootfs=${pool} ${pool}
    zfs create -po mountpoint=/ ${pool}/ROOT/default
    # NB: The online guides go nuts customizing /var and other mountpoints here, no need
    cpsys ${src} ${mntpt}
    # need to make a couple of tweaks
    cat >> ${mntpt}/boot/loader.conf <<EOF
zfs_load=YES
opensolaris_load=YES
EOF
    cp /boot/kernel/zfs.ko ${mntpt}/boot/kernel/zfs.ko
    cp /boot/kernel/opensolaris.ko ${mntpt}/boot/kernel/opensolaris.ko
    # end tweaks
    zfs umount -f ${pool}/ROOT/default
    zfs set mountpoint=none ${pool}/ROOT/default
    zpool set bootfs=${pool}/ROOT/default ${pool}
    zpool set autoexpand=on ${pool}
    zpool export ${pool}
    ${SRCTOP}/tools/boot/install-boot.sh -g ${geli} -s ${scheme} -f ${fs} -b ${bios} -d ${src} ${md}
    mdconfig -d -u ${md}
}

mk_nogeli_mbr_ufs_legacy() {
    src=$1
    img=$2

    cat > ${src}/etc/fstab <<EOF
/dev/ada0s1a	/		ufs	rw	1	1
EOF
    makefs -t ffs -B little -s 200m ${img}.s1a ${src}
    mkimg -s bsd -b ${src}/boot/boot -p freebsd-ufs:=${img}.s1a -o ${img}.s1
    mkimg -a 1 -s mbr -b ${src}/boot/boot0sio -p freebsd:=${img}.s1 -o ${img}
    rm -f ${src}/etc/fstab
}

mk_nogeli_mbr_ufs_uefi() {
    src=$1
    img=$2

    cat > ${src}/etc/fstab <<EOF
/dev/ada0s2a	/		ufs	rw	1	1
EOF
    make_esp_file ${img}.s1 ${espsize} ${src}
    makefs -t ffs -B little -s 200m ${img}.s2a ${src}
    mkimg -s bsd -p freebsd-ufs:=${img}.s2a -o ${img}.s2
    mkimg -a 1 -s mbr -p efi:=${img}.s1 -p freebsd:=${img}.s2 -o ${img}
    rm -f ${src}/etc/fstab
}

mk_nogeli_mbr_ufs_both() {
    src=$1
    img=$2

    cat > ${src}/etc/fstab <<EOF
/dev/ada0s2a	/		ufs	rw	1	1
EOF
    make_esp_file ${img}.s1 ${espsize} ${src}
    makefs -t ffs -B little -s 200m ${img}.s2a ${src}
    mkimg -s bsd -b ${src}/boot/boot -p freebsd-ufs:=${img}.s2a -o ${img}.s2
    mkimg -a 2 -s mbr -b ${src}/boot/mbr -p efi:=${img}.s1 -p freebsd:=${img}.s2 -o ${img}
    rm -f ${src}/etc/fstab
}

mk_nogeli_mbr_zfs_legacy() {
    src=$1
    img=$2
    mntpt=$3
    geli=$4
    scheme=$5
    fs=$6
    bios=$7
    pool=nogeli-mbr-zfs-legacy

    dd if=/dev/zero of=${img} count=1 seek=$((200 * 1024 * 1024 / 512))
    md=$(mdconfig -f ${img})
    gpart create -s mbr ${md}
    gpart add -t freebsd ${md}
    gpart set -a active -i 1 ${md}
    gpart create -s bsd ${md}s1
    gpart add -t freebsd-zfs ${md}s1
    # install-boot will make this bootable
    zpool create -O mountpoint=none -R ${mntpt} ${pool} ${md}s1a
    zpool set bootfs=${pool} ${pool}
    zfs create -po mountpoint=/ ${pool}/ROOT/default
    # NB: The online guides go nuts customizing /var and other mountpoints here, no need
    cpsys ${src} ${mntpt}
    # need to make a couple of tweaks
    cat >> ${mntpt}/boot/loader.conf <<EOF
zfs_load=YES
opensolaris_load=YES
EOF
    cp /boot/kernel/zfs.ko ${mntpt}/boot/kernel/zfs.ko
    cp /boot/kernel/opensolaris.ko ${mntpt}/boot/kernel/opensolaris.ko
    # end tweaks
    zfs umount -f ${pool}/ROOT/default
    zfs set mountpoint=none ${pool}/ROOT/default
    zpool set bootfs=${pool}/ROOT/default ${pool}
    zpool set autoexpand=on ${pool}
    zpool export ${pool}
    ${SRCTOP}/tools/boot/install-boot.sh -g ${geli} -s ${scheme} -f ${fs} -b ${bios} -d ${src} ${md}
    mdconfig -d -u ${md}
}

mk_nogeli_mbr_zfs_uefi() {
    src=$1
    img=$2
    mntpt=$3
    geli=$4
    scheme=$5
    fs=$6
    bios=$7
    pool=nogeli-mbr-zfs-uefi

    dd if=/dev/zero of=${img} count=1 seek=$((200 * 1024 * 1024 / 512))
    md=$(mdconfig -f ${img})
    gpart create -s mbr ${md}
    gpart add -t efi -s ${espsize}k ${md}
    gpart add -t freebsd ${md}
    gpart set -a active -i 2 ${md}
    gpart create -s bsd ${md}s2
    gpart add -t freebsd-zfs ${md}s2
    # install-boot will make this bootable
    zpool create -O mountpoint=none -R ${mntpt} ${pool} ${md}s2a
    zpool set bootfs=${pool} ${pool}
    zfs create -po mountpoint=/ ${pool}/ROOT/default
    # NB: The online guides go nuts customizing /var and other mountpoints here, no need
    cpsys ${src} ${mntpt}
    # need to make a couple of tweaks
    cat >> ${mntpt}/boot/loader.conf <<EOF
zfs_load=YES
opensolaris_load=YES
EOF
    cp /boot/kernel/zfs.ko ${mntpt}/boot/kernel/zfs.ko
    cp /boot/kernel/opensolaris.ko ${mntpt}/boot/kernel/opensolaris.ko
    # end tweaks
    zfs umount -f ${pool}/ROOT/default
    zfs set mountpoint=none ${pool}/ROOT/default
    zpool set bootfs=${pool}/ROOT/default ${pool}
    zpool set autoexpand=on ${pool}
    zpool export ${pool}
    ${SRCTOP}/tools/boot/install-boot.sh -g ${geli} -s ${scheme} -f ${fs} -b ${bios} -d ${src} ${md}
    mdconfig -d -u ${md}
}

mk_nogeli_mbr_zfs_both() {
    src=$1
    img=$2
    mntpt=$3
    geli=$4
    scheme=$5
    fs=$6
    bios=$7
    pool=nogeli-mbr-zfs-both

    dd if=/dev/zero of=${img} count=1 seek=$((200 * 1024 * 1024 / 512))
    md=$(mdconfig -f ${img})
    gpart create -s mbr ${md}
    gpart add -t efi -s  ${espsize}k ${md}
    gpart add -t freebsd ${md}
    gpart set -a active -i 2 ${md}
    gpart create -s bsd ${md}s2
    gpart add -t freebsd-zfs ${md}s2
    # install-boot will make this bootable
    zpool create -O mountpoint=none -R ${mntpt} ${pool} ${md}s2a
    zpool set bootfs=${pool} ${pool}
    zfs create -po mountpoint=/ ${pool}/ROOT/default
    # NB: The online guides go nuts customizing /var and other mountpoints here, no need
    cpsys ${src} ${mntpt}
    # need to make a couple of tweaks
    cat >> ${mntpt}/boot/loader.conf <<EOF
zfs_load=YES
opensolaris_load=YES
EOF
    cp /boot/kernel/zfs.ko ${mntpt}/boot/kernel/zfs.ko
    cp /boot/kernel/opensolaris.ko ${mntpt}/boot/kernel/opensolaris.ko
    # end tweaks
    zfs umount -f ${pool}/ROOT/default
    zfs set mountpoint=none ${pool}/ROOT/default
    zpool set bootfs=${pool}/ROOT/default ${pool}
    zpool set autoexpand=on ${pool}
    zpool export ${pool}
    ${SRCTOP}/tools/boot/install-boot.sh -g ${geli} -s ${scheme} -f ${fs} -b ${bios} -d ${src} ${md}
    mdconfig -d -u ${md}
}

mk_geli_gpt_ufs_legacy() {
    src=$1
    img=$2
    mntpt=$3
    geli=$4
    scheme=$5
    fs=$6
    bios=$7

    dd if=/dev/zero of=${img} count=1 seek=$(( 200 * 1024 * 1024 / 512 ))
    md=$(mdconfig -f ${img})
    gpart create -s gpt ${md}
    gpart add -t freebsd-boot -s 400k -a 4k	${md}	# <= ~540k
    gpart add -t freebsd-ufs -l root $md
    # install-boot will make this bootable
    echo ${passphrase} | geli init -bg -e AES-XTS -i ${iterations} -J - -l 256 -s 4096 ${md}p2
    echo ${passphrase} | geli attach -j - ${md}p2
    newfs /dev/${md}p2.eli
    mount /dev/${md}p2.eli ${mntpt}
    cpsys ${src} ${mntpt}
    # need to make a couple of tweaks
    cat > ${mntpt}/boot/loader.conf <<EOF
geom_eli_load=YES
EOF
    cat > ${mntpt}/etc/fstab <<EOF
/dev/ada0p2.eli	/		ufs	rw	1	1
EOF

    cp /boot/kernel/geom_eli.ko ${mntpt}/boot/kernel/geom_eli.ko
    # end tweaks
    umount -f ${mntpt}
    geli detach ${md}p2
    ${SRCTOP}/tools/boot/install-boot.sh -g ${geli} -s ${scheme} -f ${fs} -b ${bios} -d ${src} ${md}
    mdconfig -d -u ${md}
}

mk_geli_gpt_ufs_uefi() {
    src=$1
    img=$2
    mntpt=$3
    geli=$4
    scheme=$5
    fs=$6
    bios=$7

    dd if=/dev/zero of=${img} count=1 seek=$(( 200 * 1024 * 1024 / 512 ))
    md=$(mdconfig -f ${img})
    gpart create -s gpt ${md}
    gpart add -t efi -s ${espsize}k -a 4k ${md}
    gpart add -t freebsd-ufs -l root $md
    # install-boot will make this bootable
    echo ${passphrase} | geli init -bg -e AES-XTS -i ${iterations} -J - -l 256 -s 4096 ${md}p2
    echo ${passphrase} | geli attach -j - ${md}p2
    newfs /dev/${md}p2.eli
    mount /dev/${md}p2.eli ${mntpt}
    cpsys ${src} ${mntpt}
    # need to make a couple of tweaks
    cat > ${mntpt}/boot/loader.conf <<EOF
geom_eli_load=YES
EOF
    cat > ${mntpt}/etc/fstab <<EOF
/dev/ada0p2.eli	/		ufs	rw	1	1
EOF

    cp /boot/kernel/geom_eli.ko ${mntpt}/boot/kernel/geom_eli.ko
    # end tweaks
    umount -f ${mntpt}
    geli detach ${md}p2
    ${SRCTOP}/tools/boot/install-boot.sh -g ${geli} -s ${scheme} -f ${fs} -b ${bios} -d ${src} ${md}
    mdconfig -d -u ${md}
}

mk_geli_gpt_ufs_both() {
    src=$1
    img=$2
    mntpt=$3
    geli=$4
    scheme=$5
    fs=$6
    bios=$7

    dd if=/dev/zero of=${img} count=1 seek=$(( 200 * 1024 * 1024 / 512 ))
    md=$(mdconfig -f ${img})
    gpart create -s gpt ${md}
    gpart add -t efi -s ${espsize}k -a 4k ${md}
    gpart add -t freebsd-boot -s 400k -a 4k	${md}	# <= ~540k
    gpart add -t freebsd-ufs -l root $md
    # install-boot will make this bootable
    echo ${passphrase} | geli init -bg -e AES-XTS -i ${iterations} -J - -l 256 -s 4096 ${md}p3
    echo ${passphrase} | geli attach -j - ${md}p3
    newfs /dev/${md}p3.eli
    mount /dev/${md}p3.eli ${mntpt}
    cpsys ${src} ${mntpt}
    # need to make a couple of tweaks
    cat > ${mntpt}/boot/loader.conf <<EOF
geom_eli_load=YES
EOF
    cat > ${mntpt}/etc/fstab <<EOF
/dev/ada0p3.eli	/		ufs	rw	1	1
EOF

    cp /boot/kernel/geom_eli.ko ${mntpt}/boot/kernel/geom_eli.ko
    # end tweaks
    umount -f ${mntpt}
    geli detach ${md}p3
    ${SRCTOP}/tools/boot/install-boot.sh -g ${geli} -s ${scheme} -f ${fs} -b ${bios} -d ${src} ${md}
    mdconfig -d -u ${md}
}

mk_geli_gpt_zfs_legacy() {
    src=$1
    img=$2
    mntpt=$3
    geli=$4
    scheme=$5
    fs=$6
    bios=$7
    pool=geli-gpt-zfs-legacy

    dd if=/dev/zero of=${img} count=1 seek=$(( 200 * 1024 * 1024 / 512 ))
    md=$(mdconfig -f ${img})
    gpart create -s gpt ${md}
    gpart add -t freebsd-boot -s 400k -a 4k	${md}	# <= ~540k
    gpart add -t freebsd-zfs -l root $md
    # install-boot will make this bootable
    echo ${passphrase} | geli init -bg -e AES-XTS -i ${iterations} -J - -l 256 -s 4096 ${md}p2
    echo ${passphrase} | geli attach -j - ${md}p2
    zpool create -O mountpoint=none -R ${mntpt} ${pool} ${md}p2.eli
    zpool set bootfs=${pool} ${pool}
    zfs create -po mountpoint=/ ${pool}/ROOT/default
    # NB: The online guides go nuts customizing /var and other mountpoints here, no need
    cpsys ${src} ${mntpt}
    # need to make a couple of tweaks
    cat >> ${mntpt}/boot/loader.conf <<EOF
zfs_load=YES
opensolaris_load=YES
geom_eli_load=YES
EOF
    cp /boot/kernel/zfs.ko ${mntpt}/boot/kernel/zfs.ko
    cp /boot/kernel/opensolaris.ko ${mntpt}/boot/kernel/opensolaris.ko
    cp /boot/kernel/geom_eli.ko ${mntpt}/boot/kernel/geom_eli.ko
    # end tweaks
    zfs umount -f ${pool}/ROOT/default
    zfs set mountpoint=none ${pool}/ROOT/default
    zpool set bootfs=${pool}/ROOT/default ${pool}
    zpool set autoexpand=on ${pool}
    zpool export ${pool}
    geli detach ${md}p2
    ${SRCTOP}/tools/boot/install-boot.sh -g ${geli} -s ${scheme} -f ${fs} -b ${bios} -d ${src} ${md}
    mdconfig -d -u ${md}
}

mk_geli_gpt_zfs_uefi() {
    src=$1
    img=$2
    mntpt=$3
    geli=$4
    scheme=$5
    fs=$6
    bios=$7
    pool=geli-gpt-zfs-uefi

    dd if=/dev/zero of=${img} count=1 seek=$(( 200 * 1024 * 1024 / 512 ))
    md=$(mdconfig -f ${img})
    gpart create -s gpt ${md}
    gpart add -t efi -s ${espsize}k -a 4k ${md}
    gpart add -t freebsd-zfs -l root $md
    # install-boot will make this bootable
    echo ${passphrase} | geli init -bg -e AES-XTS -i ${iterations} -J - -l 256 -s 4096 ${md}p2
    echo ${passphrase} | geli attach -j - ${md}p2
    zpool create -O mountpoint=none -R ${mntpt} ${pool} ${md}p2.eli
    zpool set bootfs=${pool} ${pool}
    zfs create -po mountpoint=/ ${pool}/ROOT/default
    # NB: The online guides go nuts customizing /var and other mountpoints here, no need
    cpsys ${src} ${mntpt}
    # need to make a couple of tweaks
    cat >> ${mntpt}/boot/loader.conf <<EOF
zfs_load=YES
opensolaris_load=YES
geom_eli_load=YES
EOF
    cp /boot/kernel/zfs.ko ${mntpt}/boot/kernel/zfs.ko
    cp /boot/kernel/opensolaris.ko ${mntpt}/boot/kernel/opensolaris.ko
    cp /boot/kernel/geom_eli.ko ${mntpt}/boot/kernel/geom_eli.ko
    # end tweaks
    zfs umount -f ${pool}/ROOT/default
    zfs set mountpoint=none ${pool}/ROOT/default
    zpool set bootfs=${pool}/ROOT/default ${pool}
    zpool set autoexpand=on ${pool}
    zpool export ${pool}
    geli detach ${md}p2
    ${SRCTOP}/tools/boot/install-boot.sh -g ${geli} -s ${scheme} -f ${fs} -b ${bios} -d ${src} ${md}
    mdconfig -d -u ${md}
}

mk_geli_gpt_zfs_both() {
    src=$1
    img=$2
    mntpt=$3
    geli=$4
    scheme=$5
    fs=$6
    bios=$7
    pool=geli-gpt-zfs-both

    dd if=/dev/zero of=${img} count=1 seek=$(( 200 * 1024 * 1024 / 512 ))
    md=$(mdconfig -f ${img})
    gpart create -s gpt ${md}
    gpart add -t efi -s ${espsize}k -a 4k ${md}
    gpart add -t freebsd-boot -s 400k -a 4k	${md}	# <= ~540k
    gpart add -t freebsd-zfs -l root $md
    # install-boot will make this bootable
    echo ${passphrase} | geli init -bg -e AES-XTS -i ${iterations} -J - -l 256 -s 4096 ${md}p3
    echo ${passphrase} | geli attach -j - ${md}p3
    zpool create -O mountpoint=none -R ${mntpt} ${pool} ${md}p3.eli
    zpool set bootfs=${pool} ${pool}
    zfs create -po mountpoint=/ ${pool}/ROOT/default
    # NB: The online guides go nuts customizing /var and other mountpoints here, no need
    cpsys ${src} ${mntpt}
    # need to make a couple of tweaks
    cat > ${mntpt}/boot/loader.conf <<EOF
zfs_load=YES
opensolaris_load=YES
geom_eli_load=YES
EOF
    cp /boot/kernel/zfs.ko ${mntpt}/boot/kernel/zfs.ko
    cp /boot/kernel/opensolaris.ko ${mntpt}/boot/kernel/opensolaris.ko
    cp /boot/kernel/geom_eli.ko ${mntpt}/boot/kernel/geom_eli.ko
    # end tweaks
    zfs umount -f ${pool}/ROOT/default
    zfs set mountpoint=none ${pool}/ROOT/default
    zpool set bootfs=${pool}/ROOT/default ${pool}
    zpool set autoexpand=on ${pool}
    zpool export ${pool}
    geli detach ${md}p3
    ${SRCTOP}/tools/boot/install-boot.sh -g ${geli} -s ${scheme} -f ${fs} -b ${bios} -d ${src} ${md}
    mdconfig -d -u ${md}
}

# GELI+MBR is not a valid configuration
mk_geli_mbr_ufs_legacy() {
}

mk_geli_mbr_ufs_uefi() {
}

mk_geli_mbr_ufs_both() {
}

mk_geli_mbr_zfs_legacy() {
}

mk_geli_mbr_zfs_uefi() {
}

mk_geli_mbr_zfs_both() {
}

# iso
# pxeldr
# u-boot
# powerpc

mk_sparc64_nogeli_vtoc8_ufs_ofw() {
    src=$1
    img=$2
    mntpt=$3
    geli=$4
    scheme=$5
    fs=$6
    bios=$7

    cat > ${src}/etc/fstab <<EOF
/dev/ada0a	/		ufs	rw	1	1
EOF
    makefs -t ffs -B big -s 200m ${img} ${src}
    md=$(mdconfig -f ${img})
    # For non-native builds, ensure that geom_part(4) supports VTOC8.
    kldload geom_part_vtoc8.ko
    gpart create -s VTOC8 ${md}
    gpart add -t freebsd-ufs ${md}
    ${SRCTOP}/tools/boot/install-boot.sh -g ${geli} -s ${scheme} -f ${fs} -b ${bios} -d ${src} ${md}
    mdconfig -d -u ${md}
    rm -f ${src}/etc/fstab
}

qser="-serial telnet::4444,server -nographic"

# https://wiki.freebsd.org/QemuRecipes
# aarch64
qemu_aarch64_uefi()
{
    img=$1
    sh=$2

    echo "qemu-system-aarch64 -m 4096M -cpu cortex-a57 -M virt  \
        -bios QEMU_EFI.fd ${qser} \
        -drive if=none,file=${img},id=hd0 \
        -device virtio-blk-device,drive=hd0" > $sh
    chmod 755 $sh
# https://wiki.freebsd.org/arm64/QEMU also has
#       -device virtio-net-device,netdev=net0
#       -netdev user,id=net0
}

# Amd64 qemu
qemu_amd64_legacy()
{
    img=$1
    sh=$2

    echo "qemu-system-x86_64 -m 256m --drive file=${img},format=raw ${qser}" > $sh
    chmod 755 $sh
}

qemu_amd64_uefi()
{
    img=$1
    sh=$2

    echo "qemu-system-x86_64 -m 256m -bios ~/bios/OVMF-X64.fd --drive file=${img},format=raw ${qser}" > $sh
    chmod 755 $sh
}

qemu_amd64_both()
{
    img=$1
    sh=$2

    echo "qemu-system-x86_64 -m 256m --drive file=${img},format=raw ${qser}" > $sh
    echo "qemu-system-x86_64 -m 256m -bios ~/bios/OVMF-X64.fd --drive file=${img},format=raw ${qser}" >> $sh
    chmod 755 $sh
}

# arm
# nothing listed?

# i386
qemu_i386_legacy()
{
    img=$1
    sh=$2

    echo "qemu-system-i386 --drive file=${img},format=raw ${qser}" > $sh
    chmod 755 $sh
}

# Not yet supported
qemu_i386_uefi()
{
    img=$1
    sh=$2

    echo "qemu-system-i386 -bios ~/bios/OVMF-X32.fd --drive file=${img},format=raw ${qser}" > $sh
    chmod 755 $sh
}

# Needs UEFI to be supported
qemu_i386_both()
{
    img=$1
    sh=$2

    echo "qemu-system-i386 --drive file=${img},format=raw ${qser}" > $sh
    echo "qemu-system-i386 -bios ~/bios/OVMF-X32.fd --drive file=${img},format=raw ${qser}" >> $sh
    chmod 755 $sh
}

make_one_image()
{
    local arch=${1?}
    local geli=${2?}
    local scheme=${3?}
    local fs=${4?}
    local bios=${5?}

    # Create sparse file and mount newly created filesystem(s) on it
    img=${IMGDIR}/${arch}-${geli}-${scheme}-${fs}-${bios}.img
    sh=${IMGDIR}/${arch}-${geli}-${scheme}-${fs}-${bios}.sh
    echo "vvvvvvvvvvvvvv   Creating $img  vvvvvvvvvvvvvvv"
    rm -f ${img}*
    eval mk_${geli}_${scheme}_${fs}_${bios} ${DESTDIR} ${img} ${MNTPT} ${geli} ${scheme} ${fs} ${bios}
    eval qemu_${arch}_${bios} ${img} ${sh}
    [ -n "${SUDO_USER}" ] && chown ${SUDO_USER} ${img}*
    echo "^^^^^^^^^^^^^^   Created $img   ^^^^^^^^^^^^^^^"
}

# mips
# qemu-system-mips -kernel /path/to/rootfs/boot/kernel/kernel -nographic -hda /path/to/disk.img -m 2048

# Powerpc -- doesn't work but maybe it would enough for testing -- needs details
# powerpc64
# qemu-system-ppc64 -drive file=/path/to/disk.img,format=raw

# sparc64
# qemu-system-sparc64 -drive file=/path/to/disk.img,format=raw

# Misc variables
SRCTOP=$(make -v SRCTOP)
cd ${SRCTOP}/stand
OBJDIR=$(make -v .OBJDIR)
IMGDIR=${OBJDIR}/boot-images
mkdir -p ${IMGDIR}
MNTPT=$(mktemp -d /tmp/stand-test.XXXXXX)

# Setup the installed tree...
DESTDIR=${OBJDIR}/boot-tree
rm -rf ${DESTDIR}
mkdir -p ${DESTDIR}/boot/defaults
mkdir -p ${DESTDIR}/boot/kernel
cp /boot/kernel/kernel ${DESTDIR}/boot/kernel
echo -h -D -S115200 > ${DESTDIR}/boot.config
cat > ${DESTDIR}/boot/loader.conf <<EOF
console=comconsole
comconsole_speed=115200
boot_serial=yes
boot_multicons=yes
EOF
# XXX
cp /boot/device.hints ${DESTDIR}/boot/device.hints
# Assume we're already built
make install DESTDIR=${DESTDIR} MK_MAN=no MK_INSTALL_AS_USER=yes
# Copy init, /bin/sh, minimal libraries and testing /etc/rc
mkdir -p ${DESTDIR}/sbin ${DESTDIR}/bin \
      ${DESTDIR}/lib ${DESTDIR}/libexec \
      ${DESTDIR}/etc ${DESTDIR}/dev
for f in /sbin/halt /sbin/init /bin/sh /sbin/sysctl $(ldd /bin/sh | awk 'NF == 4 { print $3; }') /libexec/ld-elf.so.1; do
    cp $f ${DESTDIR}/$f
done
cat > ${DESTDIR}/etc/rc <<EOF
#!/bin/sh

sysctl machdep.bootmethod
echo "RC COMMAND RUNNING -- SUCCESS!!!!!"
halt -p
EOF

# If we were given exactly 5 args, go make that one image.

if [ $# -eq 5 ]; then
    make_one_image $*
    exit
fi

# OK. Let the games begin

for arch in amd64; do
    for geli in nogeli geli; do
	for scheme in gpt mbr; do
	    for fs in ufs zfs; do
		for bios in legacy uefi both; do
		    make_one_image ${arch} ${geli} ${scheme} ${fs} ${bios}
		done
	    done
	done
    done
done

rmdir ${MNTPT}

exit 0

# Notes for the future

for arch in i386; do
    for geli in nogeli geli; do
	for scheme in gpt mbr; do
	    for fs in ufs zfs; do
		for bios in legacy; do
		    make_one_image ${arch} ${geli} ${scheme} ${fs} ${bios}
		done
	    done
	done
    done
done

for arch in arm aarch64; do
    for scheme in gpt mbr; do
	fs=ufs
	for bios in uboot efi; do
	    make_one_image ${arch} ${geli} ${scheme} ${fs} ${bios}
	done
    done
done

for arch in powerpc powerpc64; do
    for scheme in ppc-wtf; do
	fs=ufs
	for bios in ofw uboot chrp; do
	    make_one_image ${arch} ${geli} ${scheme} ${fs} ${bios}
	done
    done
done

for arch in sparc64; do
    for geli in nogeli; do
	for scheme in vtoc8; do
	    for fs in ufs; do
		for bios in ofw; do
		    make_one_image ${arch} ${geli} ${scheme} ${fs} ${bios}
		done
	    done
	done
    done
done
