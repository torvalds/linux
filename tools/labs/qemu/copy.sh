#!/bin/bash

YOCTO_IMAGE=$1
TEMPDIR=`mktemp -u`

# $1 - target directory
# $2 - prefix for cp command (e.g. sudo)
do_copy()
{
  find skels -type f \( -name *.ko -or -executable \) | xargs --no-run-if-empty $2 cp --parents -t $1
  find skels -type d \( -name checker \) | xargs --no-run-if-empty $2 cp -r --parents -t $1
}

if  [ -e qemu.mon ]; then
  ip=`tail -n1 /tmp/dnsmasq-lkt-tap0.leases | cut -f3 -d ' '`
  if [ -z "$ip" ]; then
    echo "qemu is running and no IP address found"
    exit 1
  fi
  mkdir $TEMPDIR
  do_copy $TEMPDIR
  scp -q -r -O -o StrictHostKeyChecking=no $TEMPDIR/* root@$ip:.
  rm -rf $TEMPDIR
else
  mkdir $TEMPDIR
  sudo mount -t ext4 -o loop $YOCTO_IMAGE $TEMPDIR
  do_copy $TEMPDIR/home/root sudo
  sudo umount $TEMPDIR
  rmdir $TEMPDIR
fi
