#!/bin/sh
#
# $FreeBSD$
#

_run () {
  case "$1" in
  "alix_dsk")
    ARG="-cpu pentium"
    ARG="$ARG -hda /usr/obj/nanobsd.alix_dsk/_.disk.full -boot c"
    ARG="$ARG -hdb /z/scratch/scratch"
    ARG="$ARG -net nic,model=e1000"
    ARG="$ARG -net tap,ifname=tap0,script=no,downscript=no"
    ARG="$ARG -m 1024 -k de -localtime -nographic"
    break
    ;;
  "alix_nfs")
    ARG="-cpu pentium"
    ARG="$ARG -hda /usr/obj/nanobsd.alix_nfs/_.disk.full -boot c"
    ARG="$ARG -hdb /z/scratch/scratch"
    ARG="$ARG -net nic,model=e1000"
    ARG="$ARG -net tap,ifname=tap0,script=no,downscript=no"
    ARG="$ARG -m 1024 -k de -localtime -nographic"
    break
    ;;

  esac
  qemu-system-x86_64 -kernel-kqemu $ARG
}

_init () {
  kldstat -n kqemu || kldload kqemu
  kldstat -n aio || kldload aio
  kldstat -n if_tap || kldload if_tap
  kldstat -n if_bridge || kldload if_bridge
  sysctl net.link.tap.up_on_open=1
  ifconfig bridge0 down destroy
  ifconfig tap0 down destroy
  ifconfig tap0 create up
  ifconfig bridge0 create
  ifconfig bridge0 addm nfe0 addm tap0 up
}

_ifup () {
  sleep 2;
  ifconfig bridge0 -learn nfe0
  ifconfig tap0 up
  ifconfig bridge0 up
}

_clear () {
  ifconfig bridge0 down destroy
  ifconfig tap0 down destroy
}

_init
(_ifup) &
_run "$1"
_clear
