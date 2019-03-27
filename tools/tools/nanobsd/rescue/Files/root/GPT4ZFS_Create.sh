#!/bin/sh
# $FreeBSD$

# some default sizes
align=2048
swapsize=$(expr 1 \* 1024 \* 2048 + 1024)
zfssize=0

# define our bail out shortcut
exerr () { echo -e "$*" >&2 ; exit 1; }

usage="Usage: $0 <dsk> [ -s <swap size> ] [ -z <zfs size> ]\n\
	\tswap size: if no -s size in blocks is given, default is $swapsize blocks\n\
	\tzfs size: if no -z size in blocks is given, default is the rest of the disk"

dsk=$1
if [ -z "$dsk" -o \! -c "/dev/$dsk" ]; then
  exerr ${usage};
  exit;
fi

shift; while getopts :s:z: arg; do case ${arg} in
  s) swapsize=${OPTARG};;
  z) zfssize=${OPTARG};;                                                                                                               
  #?) exerr ${usage};;                                                                                                      
esac; done; shift $(( ${OPTIND} - 1 ))                                                                                                
 
gpart destroy -F $dsk
gpart create -s gpt $dsk

# Boot
siz=$(expr 1024 \- 34)
gpart add -i 1 -b 34 -s $siz -t freebsd-boot $dsk
gpart bootcode -b /boot/pmbr -p /boot/gptzfsboot -i 1 $dsk

# Swap
off=$align
siz=$swapsize
gpart add -i 2 -b $off -s $siz -t freebsd-swap $dsk

# ZFS
off=$(expr $align + $swapsize)
if [ "$zfssize" -gt 0 ]; then
  siz="-s $zfssize"
else
  siz=""
fi
gpart add -i 3 -b $off $siz -t freebsd-zfs $dsk

gpart show $dsk
