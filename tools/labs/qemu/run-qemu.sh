#!/bin/bash

die() { echo "$0: error: $@" >&2; exit 1; }

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" > /dev/null && pwd)"

base_dir=${ROOT:-"$(readlink -f "$script_dir/..")"}
arch=${ARCH:-"x86"}
kernel=${ZIMAGE:-"$(readlink -f "$base_dir/../../arch/$arch/boot/bzImage")"}
rootfs=${ROOTFS:-"$(readlink -f "$base_dir/rootfs")"}
skels=${SKELS:-"$(readlink -f "$base_dir/skels")"}

mode="${MODE:-console}"
case "$mode" in
    console)
	    qemu_display="-nographic"
	    linux_console="console=ttyS0"
	    ;;
    gui)
	    # QEMU_DISPLAY = sdl, gtk, ...
	    qemu_display="-display ${QEMU_DISPLAY:-"sdl"}"
	    linux_console=""
	    ;;
    *) echo "unknown mode '$MODE'" >&2; exit 1 ;;
esac

case "$arch" in
    x86) qemu_arch=i386 ;;
    arm) qemu_arch=arm ;;
    *) echo "unknown architecture '$arch'" >&2; exit 1 ;;
esac

smbd=${SMBD:-"smbd"}

qemu=${QEMU:-"qemu-system-$qemu_arch"}
if kvm-ok; then
	qemu_kvm=${QEMU_KVM:-"-enable-kvm -cpu host"}
fi
qemu_cpus=${QEMU_CPUS:-"1"}
qemu_mem=${QEMU_MEM:-"512"}
qemu_display=${QEMU_DISPLAY:-"$qemu_display"}
qemu_addopts=${QEMU_ADD_OPTS:-""}
linux_console=${LINUX_CONSOLE:-"$linux_console"}
linux_loglevel=${LINUX_LOGLEVEL:-"15"}
linux_term=${LINUX_TERM:-"TERM=xterm"}
linux_addcmdline=${LINUX_ADD_CMDLINE:-""}

linux_cmdline=${LINUX_CMDLINE:-"root=/dev/cifs rw ip=dhcp cifsroot=//10.0.2.2/rootfs,port=4450,guest,user=dummy $linux_console loglevel=$linux_loglevel $linux_term $linux_addcmdline"}

tmp_dir=$(mktemp -d)
user=$(id -un)

cat << EOF > "$tmp_dir/smbd.conf"
[global]
    interfaces = 127.0.0.1
    smb ports = 4450
    private dir = $tmp_dir
    bind interfaces only = yes
    pid directory = $tmp_dir
    lock directory = $tmp_dir
    state directory = $tmp_dir
    cache directory = $tmp_dir
    ncalrpc dir = $tmp_dir/ncalrpc
    log file = $tmp_dir/log.smbd
    smb passwd file = $tmp_dir/smbpasswd
    security = user
    map to guest = Bad User
    load printers = no
    printing = bsd
    disable spoolss = yes
    usershare max shares = 0

    server min protocol = NT1
    unix extensions = yes

    server role = standalone server
    public = yes
    writeable = yes
    #admin users = root
    #create mask = 0777
    #directory mask = 0777
    force user = $user
    force group = $user


[rootfs]
    path = $rootfs
[skels]
    path = $skels
EOF

[ -x "$(command -v "$smbd")" ] || die "samba ('$smbd') not found"
[ -x "$(command -v "$qemu")" ] || die "qemu ('$qemu') not found"

mkdir -p "$skels"

"$smbd" --no-process-group -s "$tmp_dir/smbd.conf" -l "$tmp_dir" >/dev/null 2>/dev/null &

"$qemu" \
    $qemu_kvm \
    -smp "$qemu_cpus" -m "$qemu_mem" \
    -no-reboot \
    -kernel "$kernel" \
    -append "$linux_cmdline" \
    -gdb tcp::1234 \
    $qemu_display \
    $qemu_addopts

# This seems to reset to the mode the terminal was prior to launching QEMU
# Inspired by
# https://github.com/landley/toybox/blob/990e0e7a40e4509c7987a190febe5d867f412af6/toys/other/reset.c#L26-L28
# man 4 console_codes, ESC [ ? 7 h
printf '\e[?7h'

pkill -F "$tmp_dir/smbd.pid"
rm -rf "$tmp_dir"
