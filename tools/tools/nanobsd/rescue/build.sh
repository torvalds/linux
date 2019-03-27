#!/bin/sh
#
# $FreeBSD$
#

today=`date '+%Y%m%d'`

if [ -z "${1}" -o \! -f "${1}" ]; then
  echo "Usage: $0 cfg_file [-bhiknw]"
  echo "-i : skip image build"
  echo "-w : skip buildworld step"
  echo "-k : skip buildkernel step"
  echo "-b : skip buildworld and buildkernel step"
  exit
fi

CFG="${1}"
shift;

if [ \! -d /usr/obj/Rescue ]; then
  mkdir -p /usr/obj/Rescue
fi

sh ../nanobsd.sh $* -c ${CFG}

if [ \! -d /usr/obj/Rescue ]; then
  mkdir -p /usr/obj/Rescue
fi
F32="/usr/obj/Rescue/rescue_${today}_x32"
D32="/usr/obj/nanobsd.rescue_i386"
if [ -f "${D32}/_.disk.full" ]; then
  cp "${D32}/_.disk.full" "${F32}.img"
fi
if [ -f "${D32}/_.disk.iso" ]; then
  cp "${D32}/_.disk.iso" "${F32}.iso"
fi

F64="/usr/obj/Rescue/rescue_${today}_x64"
D64="/usr/obj/nanobsd.rescue_amd64"
if [ -f "${D64}/_.disk.full" ]; then
  cp "${D64}/_.disk.full" "${F64}.img"
fi
if [ -f "${D64}/_.disk.iso" ]; then
  cp "${D64}/_.disk.iso" "${F64}.iso"
fi
