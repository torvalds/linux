#!/bin/bash

set -e

script_dir=$(cd $(dirname ${BASH_SOURCE:-$0}); pwd)
hijack_script=${script_dir}/../bin/lkl-hijack.sh

if [[ ! -e ${script_dir}/../liblkl-hijack.so ]]; then
    echo "WARNING: tests can't be run. Quitting early."
    exit 0;
fi

# Make a temporary directory to run tests in, since we'll be copying
# things there.
work_dir=$(mktemp -d)

# And make sure we clean up when we're done
function clear_work_dir {
    test -f ${VDESWITCH}.pid && kill $(cat ${VDESWITCH}.pid)
    rm -rf ${work_dir}
    sudo ip link set dev lkl_ptt0 down &> /dev/null || true
    sudo ip tuntap del dev lkl_ptt0 mode tap &> /dev/null || true
    sudo ip link set dev lkl_ptt1 down &> /dev/null || true
    sudo ip tuntap del dev lkl_ptt1 mode tap &> /dev/null || true
}

trap clear_work_dir EXIT

echo "Running tests in ${work_dir}"

echo "== ip addr test=="
${hijack_script} ip addr

echo "== ip route test=="
${hijack_script} ip route

echo "== ping test=="
cp `which ping` .
${hijack_script} ./ping -c 1 127.0.0.1
rm ./ping

echo "== ping6 test=="
cp `which ping6` .
${hijack_script} ./ping6 -c 1 ::1
rm ./ping6

echo "== Mount/dump tests =="
cfgjson=${work_dir}/hijack-test.conf

cat << EOF > ${cfgjson}
{
	"mount":"proc,sysfs",
	"dump":"/sysfs/class/net/lo/mtu,/sysfs/class/net/lo/dev_id",
	"debug":"1"
}
EOF

# Need to say || true because ip -h returns < 0
ans=$(LKL_HIJACK_CONFIG_FILE=$cfgjson\
  ${hijack_script} ip -h) || true
# Need to grab the end because something earlier on prints out this
# number
# XXX: android-23 doesn't call destructor...
if [ -z ${LKL_ANDROID_TEST} ] ; then
echo "$ans" | tail -n 15 | grep "65536" # lo's MTU
# lo's dev id
echo "$ans" | grep "0x0"        # lo's dev_id
# Doesn't really belong in this section, but might as well check for
# it here.
! echo "$ans" | grep "WARN: failed to free"
fi

# boot_cmdline test
echo "== boot command line tests =="

cat << EOF > ${cfgjson}
{
	"debug":"1",
	"boot_cmdline":"mem=100M"
}
EOF

ans=$(LKL_HIJACK_CONFIG_FILE=$cfgjson ${hijack_script} ip ad)
if [ -z ${CROSS_COMPILE} ] ; then
echo "$ans" | grep "100752k"
elif [ "${CROSS_COMPILE}" = "arm-linux-androideabi-" ] ; then
(echo "$ans" | grep "101424k") || true
elif [ "${CROSS_COMPILE}" = "aarch64-linux-android-" ] ; then
(echo "$ans" | grep "100756k") || true
fi

echo "== PIPE tests =="
if [ -z `which mkfifo` ]; then
    echo "WARNIG: no mkfifo command, skipping PIPE tests."
else

fifo1=${work_dir}/fifo1
fifo2=${work_dir}/fifo2
mkfifo ${fifo1}
mkfifo ${fifo2}

cat << EOF > ${cfgjson}
{
	"interfaces":[
	{
		"type":"pipe",
		"param":"${fifo1}|${fifo2}",
		"ip":"192.168.13.2",
		"masklen":"24",
		"mac":"aa:bb:cc:dd:ee:ff",
	}
	]
}
EOF

# Make sure our device has the addresses we expect
addr=$(LKL_HIJACK_CONFIG_FILE=$cfgjson ${hijack_script} ip addr)
echo "$addr" | grep eth0
echo "$addr" | grep 192.168.13.2
echo "$addr" | grep "aa:bb:cc:dd:ee:ff"

# Copy ping so we're allowed to run it under LKL
cp $(which ping) .
cp $(which ping6) .

cat << EOF > ${cfgjson}
{
	"gateway":"192.168.13.2",
	"gateway6":"fc03::2",
	"interfaces":[
	{
		"type":"pipe",
		"param":"${fifo1}|${fifo2}",
		"ip":"192.168.13.1",
		"masklen":"24",
		"mac":"aa:bb:cc:dd:ee:ff",
		"ipv6":"fc03::1",
		"masklen6":"64"
	}
	]
}
EOF

LKL_HIJACK_CONFIG_FILE=$cfgjson ${hijack_script} sleep 10 &

sleep 5

cat << EOF > ${cfgjson}
{
	"gateway":"192.168.13.1",
	"gateway6":"fc03::1",
	"interfaces":[
	{
		"type":"pipe",
		"param":"${fifo2}|${fifo1}",
		"ip":"192.168.13.2",
		"masklen":"24",
		"mac":"aa:bb:cc:dd:ee:ff",
		"ipv6":"fc03::2",
		"masklen6":"64"
	}
	]
}
EOF

# Ping under LKL
sudo LKL_HIJACK_CONFIG_FILE=$cfgjson ${hijack_script} ./ping 192.168.13.1 -c 1

# Ping 6 under LKL
sudo LKL_HIJACK_CONFIG_FILE=$cfgjson ${hijack_script} ./ping6 fc03::1 -c 1

wait
rm ./ping
rm ./ping6
fi

echo "== TAP tests =="
if [ ! -c /dev/net/tun ]; then
    echo "WARNING: missing /dev/net/tun, skipping TAP and VDE tests."
    exit 0
fi

# Set up the TAP device we'd like to use
sudo ip tuntap del dev lkl_ptt0 mode tap || true
sudo ip tuntap add dev lkl_ptt0 mode tap user $USER
sudo ip link set dev lkl_ptt0 up
sudo ip addr add dev lkl_ptt0 192.168.13.1/24
sudo ip -6 addr add dev lkl_ptt0 fc03::1/64

cat << EOF > ${cfgjson}
{
	"gateway":"192.168.13.1",
	"gateway6":"fc03::1",
	"interfaces":[
	{
		"type":"tap",
		"param":"lkl_ptt0",
		"ip":"192.168.13.2",
		"masklen":"24",
		"ipv6":"fc03::2",
		"masklen6":"64"
	}
	]
}
EOF

# Make sure our device has the addresses we expect
addr=$(LKL_HIJACK_DEBUG=1 LKL_HIJACK_NET_MAC="aa:bb:cc:dd:ee:ff" \
	LKL_HIJACK_CONFIG_FILE=$cfgjson ${hijack_script} ip addr)
echo "$addr" | grep eth0
echo "$addr" | grep 192.168.13.2
echo "$addr" | grep "aa:bb:cc:dd:ee:ff"
echo "$addr" | grep "fc03::2"
! echo "$addr" | grep "WARN: failed to free"

# Copy ping so we're allowed to run it under LKL
cp `which ping` .
cp `which ping6` .

# Make sure we can ping the host from inside LKL
LKL_HIJACK_CONFIG_FILE=$cfgjson ${hijack_script} ./ping 192.168.13.1 -c 1
LKL_HIJACK_CONFIG_FILE=$cfgjson ${hijack_script} ./ping6 fc03::1 -c 1
rm ./ping ./ping6

# Now let's check that the host can see LKL.
sudo ip -6 neigh del fc03::2 dev lkl_ptt0
sudo ip neigh del 192.168.13.2 dev lkl_ptt0
sudo ping -i 0.01 -c 65 192.168.13.2 &
sudo ping6 -i 0.01 -c 65 fc03::2 &
LKL_HIJACK_CONFIG_FILE=$cfgjson ${hijack_script} sleep 3

neigh1="192.168.13.100|12:34:56:78:9a:bc"
neigh2="fc03::100|12:34:56:78:9a:be"

cat << EOF > ${cfgjson}
{
	"gateway":"192.168.13.1",
	"gateway6":"fc03::1",
	"interfaces":[
	{
		"type":"tap",
		"param":"lkl_ptt0",
		"ip":"192.168.13.2",
		"masklen":"24",
		"ipv6":"fc03::2",
		"masklen6":"64",
		"neigh":"${neigh1};${neigh2}"
	}
	]
}
EOF

# add neighbor entries
ans=$(LKL_HIJACK_CONFIG_FILE=$cfgjson ${hijack_script} ip neighbor show) || true
echo "$ans" | tail -n 15 | grep "12:34:56:78:9a:bc"
echo "$ans" | tail -n 15 | grep "12:34:56:78:9a:be"

# gateway
ans=$(LKL_HIJACK_CONFIG_FILE=$cfgjson ${hijack_script} ip route show) || true
echo "$ans" | tail -n 15 | grep "192.168.13.1"

# gateway v6
ans=$(LKL_HIJACK_CONFIG_FILE=$cfgjson ${hijack_script} ip -6 route show) || true
echo "$ans" | tail -n 15 | grep "fc03::1"

# offload
# LKL_VIRTIO_NET_F_HOST_TSO4 && LKL_VIRTIO_NET_F_GUEST_TSO4
# LKL_VIRTIO_NET_F_CSUM && LKL_VIRTIO_NET_F_GUEST_CSUM
cat << EOF > ${cfgjson}
{
	"gateway":"192.168.13.1",
	"gateway6":"fc03::1",
	"interfaces":[
	{
		"offload":"0x883",
		"type":"tap",
		"param":"lkl_ptt0",
		"ip":"192.168.13.2",
		"masklen":"24",
		"ipv6":"fc03::2",
		"masklen6":"64"
	}
	]
}
EOF

LKL_HIJACK_CONFIG_FILE=$cfgjson sh \
	${script_dir}/run_netperf.sh 192.168.13.1 1 0 TCP_STREAM
LKL_HIJACK_CONFIG_FILE=$cfgjson sh \
	${script_dir}/run_netperf.sh 192.168.13.1 1 0 TCP_MAERTS

# offload
# LKL_VIRTIO_NET_F_HOST_TSO4 && LKL_VIRTIO_NET_F_MRG_RXBUF
# LKL_VIRTIO_NET_F_CSUM && LKL_VIRTIO_NET_F_GUEST_CSUM
cat << EOF > ${cfgjson}
{
	"gateway":"192.168.13.1",
	"gateway6":"fc03::1",
	"interfaces":[
	{
		"offload":"0x8803",
		"type":"tap",
		"param":"lkl_ptt0",
		"ip":"192.168.13.2",
		"masklen":"24",
		"ipv6":"fc03::2",
		"masklen6":"64"
	}
	]
}
EOF

LKL_HIJACK_CONFIG_FILE=$cfgjson sh \
	${script_dir}/run_netperf.sh 192.168.13.1 1 0 TCP_MAERTS

cat << EOF > ${cfgjson}
{
	"gateway":"192.168.13.1",
	"gateway6":"fc03::1",
	"interfaces":[
	{
		"type":"tap",
		"param":"lkl_ptt0",
		"ip":"192.168.13.2",
		"masklen":"24",
		"ipv6":"fc03::2",
		"masklen6":"64"
	}
	]
}
EOF

LKL_HIJACK_CONFIG_FILE=$cfgjson sh \
	${script_dir}/run_netperf.sh 192.168.13.1 1 0 TCP_RR
LKL_HIJACK_CONFIG_FILE=$cfgjson sh \
	${script_dir}/run_netperf.sh fc03::1 1 0 TCP_STREAM

# QDISC test
cat << EOF > ${cfgjson}
{
	"gateway":"192.168.13.1",
	"gateway6":"fc03::1",
	"interfaces":[
	{
		"type":"tap",
		"param":"lkl_ptt0",
		"ip":"192.168.13.2",
		"masklen":"24",
		"ipv6":"fc03::2",
		"masklen6":"64",
		"mac":"aa:bb:cc:dd:ee:ff",
		"qdisc":"root|fq"
	}
	]
}
EOF

qdisc=$(LKL_HIJACK_CONFIG_FILE=$cfgjson ${hijack_script} tc -s -d qdisc show)
echo "$qdisc"
echo "$qdisc" | grep "qdisc fq" > /dev/null
echo "$qdisc" | grep throttled > /dev/null

echo "== Multiple interfaces with TAP tests=="
# Set up 2nd TAP device we'd like to use
sudo ip tuntap del dev lkl_ptt1 mode tap || true
sudo ip tuntap add dev lkl_ptt1 mode tap user $USER
sudo ip link set dev lkl_ptt1 up
sudo ip addr add dev lkl_ptt1 192.168.14.1/24
sudo ip -6 addr add dev lkl_ptt1 fc04::1/64

cat << EOF > ${cfgjson}
{
	"gateway":"192.168.13.1",
	"gateway6":"fc03::1",
	"interfaces":[
	{
		"type":"tap",
		"param":"lkl_ptt0",
		"ip":"192.168.13.2",
		"masklen":"24",
		"ipv6":"fc03::2",
		"masklen6":"64",
		"mac":"aa:bb:cc:dd:ee:ff"
	},
	{
		"type":"tap",
		"param":"lkl_ptt1",
		"ip":"192.168.14.2",
		"masklen":"24",
		"ipv6":"fc04::2",
		"masklen6":"64",
		"mac":"aa:bb:cc:dd:ee:aa"
	}
	]
}
EOF

# Make sure our device has the addresses we expect
addr=$(LKL_HIJACK_DEBUG=1 \
	LKL_HIJACK_CONFIG_FILE=$cfgjson ${hijack_script} ip addr)
echo "$addr" | grep eth0
echo "$addr" | grep 192.168.13.2
echo "$addr" | grep "aa:bb:cc:dd:ee:ff"
echo "$addr" | grep "fc03::2"
echo "$addr" | grep eth1
echo "$addr" | grep 192.168.14.2
echo "$addr" | grep "aa:bb:cc:dd:ee:aa"
echo "$addr" | grep "fc04::2"
! echo "$addr" | grep "WARN: failed to free"

# Copy ping so we're allowed to run it under LKL
cp `which ping` .
cp `which ping6` .

# Make sure we can ping the host from inside LKL
LKL_HIJACK_CONFIG_FILE=$cfgjson ${hijack_script} ./ping 192.168.13.1 -c 1
LKL_HIJACK_CONFIG_FILE=$cfgjson ${hijack_script} ./ping6 fc03::1 -c 1
LKL_HIJACK_CONFIG_FILE=$cfgjson ${hijack_script} ./ping 192.168.14.1 -c 1
LKL_HIJACK_CONFIG_FILE=$cfgjson ${hijack_script} ./ping6 fc04::1 -c 1
rm ./ping ./ping6

neigh1="192.168.13.100|12:34:56:78:9a:bc"
neigh2="fc03::100|12:34:56:78:9a:be"
neigh3="192.168.14.100|12:34:56:78:9a:bd"
neigh4="fc04::100|12:34:56:78:9a:bf"

cat << EOF > ${cfgjson}
{
	"gateway":"192.168.13.1",
	"gateway6":"fc03::1",
	"interfaces":[
	{
		"type":"tap",
		"param":"lkl_ptt0",
		"ip":"192.168.13.2",
		"masklen":"24",
		"ipv6":"fc03::2",
		"masklen6":"64",
		"mac":"aa:bb:cc:dd:ee:ff",
		"neigh":"${neigh1};${neigh2}"
	},
	{
		"type":"tap",
		"param":"lkl_ptt1",
		"ip":"192.168.14.2",
		"masklen":"24",
		"ipv6":"fc04::2",
		"masklen6":"64",
		"mac":"aa:bb:cc:dd:ee:aa",
		"neigh":"${neigh3};${neigh4}"
	}
	]
}
EOF

# add neighbor entries
ans=$(LKL_HIJACK_CONFIG_FILE=$cfgjson ${hijack_script} ip neighbor show) || true
echo "$ans" | tail -n 15 | grep "12:34:56:78:9a:bc"
echo "$ans" | tail -n 15 | grep "12:34:56:78:9a:be"
echo "$ans" | tail -n 15 | grep "12:34:56:78:9a:bd"
echo "$ans" | tail -n 15 | grep "12:34:56:78:9a:bf"

# gateway
ans=$(LKL_HIJACK_CONFIG_FILE=$cfgjson ${hijack_script} ip route show) || true
echo "$ans" | tail -n 15 | grep "192.168.13.1"

# gateway v6
ans=$(LKL_HIJACK_CONFIG_FILE=$cfgjson ${hijack_script} ip -6 route show) || true
echo "$ans" | tail -n 15 | grep "fc03::1"

echo "== multiple table tests =="

cat << EOF > ${cfgjson}
{
	"gateway":"192.168.13.1",
	"gateway6":"fc03::1",
	"interfaces":[
	{
		"type":"tap",
		"param":"lkl_ptt0",
		"ip":"192.168.13.2",
		"masklen":"24",
		"ifgateway":"192.168.13.1",
		"ipv6":"fc03::2",
		"masklen6":"64",
		"ifgateway6":"fc03::1",
		"mac":"aa:bb:cc:dd:ee:ff",
		"neigh":"${neigh1};${neigh2}"
	},
	{
		"type":"tap",
		"param":"lkl_ptt1",
		"ip":"192.168.14.2",
		"masklen":"24",
		"ifgateway":"192.168.14.1",
		"ipv6":"fc04::2",
		"masklen6":"64",
		"ifgateway6":"fc04::1",
		"mac":"aa:bb:cc:dd:ee:aa",
		"neigh":"${neigh3};${neigh4}"
	}
	]
}
EOF

# Make sure our device has ipv4 rule we expect
addr=$(LKL_HIJACK_CONFIG_FILE=$cfgjson ${hijack_script} ip rule show)
echo "$addr" | grep 192.168.13.2
echo "$addr" | grep 192.168.14.2

# Make sure our device has ipv6 rule we expect
addr=$(LKL_HIJACK_CONFIG_FILE=$cfgjson ${hijack_script} ip -6 rule show)
echo "$addr" | grep fc03::2
echo "$addr" | grep fc04::2

# Make sure our device has ipv4 rule table
addr=$(LKL_HIJACK_CONFIG_FILE=$cfgjson ${hijack_script} ip route show table 4)
echo "$addr" | grep 192.168.13.1

# Make sure our device has ipv6 rule table
addr=$(LKL_HIJACK_CONFIG_FILE=$cfgjson \
		${hijack_script} ip -6 route show table 5)
echo "$addr" | grep fc03::
echo "$addr" | grep fc03::1

# Make sure our device has ipv4 rule table
addr=$(LKL_HIJACK_CONFIG_FILE=$cfgjson ${hijack_script} ip route show table 6)
echo "$addr" | grep 192.168.14.1

# Make sure our device has ipv6 rule table
addr=$(LKL_HIJACK_CONFIG_FILE=$cfgjson \
		${hijack_script} ip -6 route show table 7)
echo "$addr" | grep fc04::
echo "$addr" | grep fc04::1

if [ -z "`printenv CONFIG_AUTO_LKL_VIRTIO_NET_VDE`" ]; then
    exit 0
fi

echo "== VDE tests =="
if [ ! -x "$(which vde_switch)" ]; then
    echo "WARNING: Cannot find a vde_switch executable, skipping VDE tests."
    exit 0
fi
VDESWITCH=${work_dir}/vde_switch

cat << EOF > ${cfgjson}
{
	"gateway":"192.168.13.1",
	"gateway6":"fc03::1",
	"interfaces":[
	{
		"type":"vde",
		"param":"${VDESWITCH}",
		"ip":"192.168.13.2",
		"masklen":"24",
		"ipv6":"fc03::2",
		"masklen6":"64",
		"mac":"aa:bb:cc:dd:ee:ff",
		"neigh":"${neigh1};${neigh2}"
	}
	]
}
EOF

sudo ip link set dev lkl_ptt0 down &> /dev/null || true
sudo ip link del dev lkl_ptt0 &> /dev/null || true
sudo ip tuntap add dev lkl_ptt0 mode tap user $USER
sudo ip link set dev lkl_ptt0 up
sudo ip addr add dev lkl_ptt0 192.168.13.1/24

sleep 2
vde_switch -d -t lkl_ptt0 -s ${VDESWITCH} -p ${VDESWITCH}.pid

# Make sure our device has the addresses we expect
addr=$(LKL_HIJACK_CONFIG_FILE=$cfgjson ${hijack_script} ip addr)
echo "$addr" | grep eth0
echo "$addr" | grep 192.168.13.2
echo "$addr" | grep "aa:bb:cc:dd:ee:ff"

# Copy ping so we're allowed to run it under LKL
cp $(which ping) .

# Make sure we can ping the host from inside LKL
LKL_HIJACK_CONFIG_FILE=$cfgjson ${hijack_script} ./ping 192.168.13.1 -c 1
rm ./ping

# Now let's check that the host can see LKL.
sudo arp -d 192.168.13.2
sudo ping -i 0.01 -c 65 192.168.13.2 &
LKL_HIJACK_CONFIG_FILE=$cfgjson ${hijack_script} sleep 3
