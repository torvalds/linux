#!/bin/sh
if [ `id -u` -ne 0 ]; then
	echo "$0: must be root to install the selinux policy"
	exit 1
fi
SF=`which setfiles`
if [ $? -eq 1 ]; then
	if [ -f /sbin/setfiles ]; then
		SF="/usr/setfiles"
	else
		echo "no selinux tools installed: setfiles"
		exit 1
	fi
fi

cd mdp

CP=`which checkpolicy`
VERS=`$CP -V | awk '{print $1}'`

./mdp policy.conf file_contexts
$CP -o policy.$VERS policy.conf

mkdir -p /etc/selinux/dummy/policy
mkdir -p /etc/selinux/dummy/contexts/files

cp file_contexts /etc/selinux/dummy/contexts/files
cp dbus_contexts /etc/selinux/dummy/contexts
cp policy.$VERS /etc/selinux/dummy/policy
FC_FILE=/etc/selinux/dummy/contexts/files/file_contexts

if [ ! -d /etc/selinux ]; then
	mkdir -p /etc/selinux
fi
if [ ! -f /etc/selinux/config ]; then
	cat > /etc/selinux/config << EOF
SELINUX=enforcing
SELINUXTYPE=dummy
EOF
else
	TYPE=`cat /etc/selinux/config | grep "^SELINUXTYPE" | tail -1 | awk -F= '{ print $2 '}`
	if [ "eq$TYPE" != "eqdummy" ]; then
		selinuxenabled
		if [ $? -eq 0 ]; then
			echo "SELinux already enabled with a non-dummy policy."
			echo "Exiting.  Please install policy by hand if that"
			echo "is what you REALLY want."
			exit 1
		fi
		mv /etc/selinux/config /etc/selinux/config.mdpbak
		grep -v "^SELINUXTYPE" /etc/selinux/config.mdpbak >> /etc/selinux/config
		echo "SELINUXTYPE=dummy" >> /etc/selinux/config
	fi
fi

cd /etc/selinux/dummy/contexts/files
$SF file_contexts /

mounts=`cat /proc/$$/mounts | egrep "ext2|ext3|xfs|jfs|ext4|ext4dev|gfs2" | awk '{ print $2 '}`
$SF file_contexts $mounts


dodev=`cat /proc/$$/mounts | grep "/dev "`
if [ "eq$dodev" != "eq" ]; then
	mount --move /dev /mnt
	$SF file_contexts /dev
	mount --move /mnt /dev
fi

