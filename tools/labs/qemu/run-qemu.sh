#!/bin/bash

QEMU_PIDFILE=$(mktemp)
SAMBA_DIR=$(mktemp -d)

die() { echo "$0: error: $@" >&2; exit 1; }

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" > /dev/null && pwd)"

base_dir=${ROOT:-"$(readlink -f "$script_dir/..")"}
arch=${ARCH:-"x86"}
kernel=${ZIMAGE:-"$(readlink -f "$base_dir/../../arch/$arch/boot/bzImage")"}
rootfs=${ROOTFS:-"$(readlink -f "$base_dir/rootfs")"}
skels=${SKELS:-"$(readlink -f "$base_dir/skels")"}

QEMU_BKGRND="-daemonize"
CHECKER=0

mode="${MODE:-console}"
case "$mode" in
    console)
	    qemu_display="-display none"
	    linux_console="console=hvc0"
	    ;;
    gui)
	    # QEMU_DISPLAY = sdl, gtk, ...
	    qemu_display="-display ${QEMU_DISPLAY:-"sdl"}"
	    linux_console=""
	    ;;
    checker)
	    qemu_display="-display none"
            QEMU_BKGRND=""
	    CHECKER=1
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

linux_cmdline=${LINUX_CMDLINE:-"root=/dev/cifs rw ip=dhcp cifsroot=//10.0.2.1/rootfs,port=4450,guest,user=dummy $linux_console loglevel=$linux_loglevel pci=noacpi $linux_term $linux_addcmdline"}

user=$(id -un)

cat << EOF > "$SAMBA_DIR/smbd.conf"
[global]
    interfaces = 10.0.2.1
    smb ports = 4450
    private dir = $SAMBA_DIR
    bind interfaces only = yes
    pid directory = $SAMBA_DIR
    lock directory = $SAMBA_DIR
    state directory = $SAMBA_DIR
    cache directory = $SAMBA_DIR
    ncalrpc dir = $SAMBA_DIR/ncalrpc
    log file = $SAMBA_DIR/log.smbd
    smb passwd file = $SAMBA_DIR/smbpasswd
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

"$smbd" --no-process-group -s "$SAMBA_DIR/smbd.conf" -l "$SAMBA_DIR" >/dev/null 2>/dev/null &

"$qemu" \
    $qemu_kvm \
    $QEMU_BKGRND \
    -pidfile $QEMU_PIDFILE \
    -device virtio-serial -chardev socket,id=virtiocon0,path=serial_console.socket,server,nowait -device virtconsole,chardev=virtiocon0 \
    -smp "$qemu_cpus" -m "$qemu_mem" \
    -no-reboot \
    -kernel "$kernel" \
    -append "$linux_cmdline" \
    -serial pipe:pipe1 \
    -serial pipe:pipe2 \
    -netdev tap,id=lkt-tap-smbd,ifname=lkt-tap-smbd,script=no,downscript=no -net nic,netdev=lkt-tap-smbd,model=virtio \
    -netdev tap,id=lkt-tap0,ifname=lkt-tap0,script=no,downscript=no -net nic,netdev=lkt-tap0,model=virtio \
    -netdev tap,id=lkt-tap1,ifname=lkt-tap1,script=no,downscript=no -net nic,netdev=lkt-tap1,model=i82559er \
    -drive file=disk0.img,if=virtio,format=raw \
    -drive file=disk1.img,if=virtio,format=raw \
    -drive file=disk2.img,if=virtio,format=raw \
    -gdb tcp::1234 \
    $qemu_display \
    $qemu_addopts

sleep 2
if [[ $CHECKER != 1 ]]; then
	minicom -D unix\#serial_console.socket
fi

# This seems to reset to the mode the terminal was prior to launching QEMU
# Inspired by
# https://github.com/landley/toybox/blob/990e0e7a40e4509c7987a190febe5d867f412af6/toys/other/reset.c#L26-L28
# man 4 console_codes, ESC [ ? 7 h
printf '\e[?7h'

echo "Cleaning up...Please wait!"
pkill -F $QEMU_PIDFILE
$script_dir/cleanup-net.sh
pkill -F ${SAMBA_DIR}/smbd.pid
sleep 1
rm -rf $SAMBA_DIR
rm -f $QEMU_PIDFILE
rm -f serial_console.socket
