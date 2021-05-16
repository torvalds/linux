#!/bin/bash

size=$(stat -c%s $1)
if [ $size -lt 50000000 ]; then
    e2fsck -f $1
    resize2fs $1 64M
fi

TMP=$(mktemp -d)

mount -t ext4 -o loop $1 $TMP

# add console
if [ "$ARCH" = "x86" ]; then
    echo "hvc0:12345:respawn:/sbin/getty 115200 hvc0" >> $TMP/etc/inittab
else
    echo "mxc0:12345:respawn:/sbin/getty 115200 ttymxc0" >> $TMP/etc/inittab
fi

# add more vty
cat >> $TMP/etc/inittab <<EOF
2:12345:respawn:/sbin/getty 38400 tty2
3:12345:respawn:/sbin/getty 38400 tty3
4:12345:respawn:/sbin/getty 38400 tty4
5:12345:respawn:/sbin/getty 38400 tty5
EOF

# enable networking
echo -e "auto eth0\niface eth0 inet dhcp" >> $TMP/etc/network/interfaces

sudo umount $TMP
rmdir $TMP
