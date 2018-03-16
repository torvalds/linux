#!/bin/bash
### BEGIN INIT INFO
# Provides:       wifi
# Required-Start: $network $remote_fs $syslog
# Required-Stop:  $network $remote_fs $syslog
# Default-Start:  2 3 4 5
# Default-Stop:   0 1 6
# Description:    Load MediaTek MT6625L firmware
### END INIT INFO

# Don't exit on error status
set +e

PATH=/usr/local/sbin:/usr/local/bin:/sbin:/bin:/usr/sbin:/usr/bin
DAEMON=/dev/null
NAME=wifi
DESC="Load MediaTek MT6625L firmware wifi & BT"

#disable kernel-messages
echo 4 > /proc/sys/kernel/printk
#disable printing messages to console
dmesg -D

if [[ ! -e /dev/wmtWifi ]];
then
  echo "wifidev does not exist...create it..."
  # Check FILE exists and is executable
  if [[ -x /usr/bin/wmt_loader ]];
  then
	# ??
	/usr/bin/wmt_loader > /var/log/wmtloader.log
	sleep 3
  else
	echo "Error, unable to find wmt_loader"
  fi

  # Check FILE exists and is character special
  if  [[ -c /dev/stpwmt ]];
  then
	# Load firmware
	/usr/bin/stp_uart_launcher -p /etc/firmware > /var/log/stp_launcher.log &
	sleep 5
  else
  	echo "Error, device no created, /dev/stpwmt"
  fi
fi

# Check FILE exists and is character special
if  [[ -c /dev/wmtWifi ]];
then
	if [[ -n $(pidof hostapd) ]];
	then
		echo "hostapd running...kill it";
		killall hostapd
	fi
	if [[ -n $(ip a|grep ap0) ]];
	then
		echo "ap0 exists, reset it";
		echo 0 >/dev/wmtWifi
		sleep 5
	fi
	echo A >/dev/wmtWifi
	sleep 2
else
	echo "Error, wifi device no created, /dev/wmtWifi"
fi

# Check NIC ap0
#ifconfig ap0
ip addr show ap0
if [[ $? != "0" ]]
then
	echo "Error, device no available, ap0"
else
#	echo "set MAC"
#	ip link set ap0 address 01:02:03:04:05:06 up
	echo "Done, all good, ready to lauch hostapd"
	hostapd -dd /etc/hostapd/hostapd_ap0.conf > /var/log/hostapd_ap0.log &
	echo "set IP"
	ip addr add 192.168.10.1/24 dev ap0
	echo "restart dnsmasq..."
	service dnsmasq restart
fi
