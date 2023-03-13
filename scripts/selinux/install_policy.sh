#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
set -e
if [ `id -u` -ne 0 ]; then
	echo "$0: must be root to install the selinux policy"
	exit 1
fi

SF=`which setfiles`
if [ $? -eq 1 ]; then
	echo "Could not find setfiles"
	echo "Do you have policycoreutils installed?"
	exit 1
fi

CP=`which checkpolicy`
if [ $? -eq 1 ]; then
	echo "Could not find checkpolicy"
	echo "Do you have checkpolicy installed?"
	exit 1
fi
VERS=`$CP -V | awk '{print $1}'`

ENABLED=`which selinuxenabled`
if [ $? -eq 1 ]; then
	echo "Could not find selinuxenabled"
	echo "Do you have libselinux-utils installed?"
	exit 1
fi

if selinuxenabled; then
    echo "SELinux is already enabled"
    echo "This prevents safely relabeling all files."
    echo "Boot with selinux=0 on the kernel command-line or"
    echo "SELINUX=disabled in /etc/selinux/config."
    exit 1
fi

cd mdp
./mdp -m policy.conf file_contexts
$CP -U allow -M -o policy.$VERS policy.conf

mkdir -p /etc/selinux/dummy/policy
mkdir -p /etc/selinux/dummy/contexts/files

echo "__default__:user_u:s0" > /etc/selinux/dummy/seusers
echo "base_r:base_t:s0" > /etc/selinux/dummy/contexts/failsafe_context
echo "base_r:base_t:s0 base_r:base_t:s0" > /etc/selinux/dummy/default_contexts
cat > /etc/selinux/dummy/contexts/x_contexts <<EOF
client * user_u:base_r:base_t:s0
property * user_u:object_r:base_t:s0
extension * user_u:object_r:base_t:s0
selection * user_u:object_r:base_t:s0
event * user_u:object_r:base_t:s0
EOF
touch /etc/selinux/dummy/contexts/virtual_domain_context
touch /etc/selinux/dummy/contexts/virtual_image_context

cp file_contexts /etc/selinux/dummy/contexts/files
cp dbus_contexts /etc/selinux/dummy/contexts
cp policy.$VERS /etc/selinux/dummy/policy
FC_FILE=/etc/selinux/dummy/contexts/files/file_contexts

if [ ! -d /etc/selinux ]; then
	mkdir -p /etc/selinux
fi
if [ -f /etc/selinux/config ]; then
    echo "/etc/selinux/config exists, moving to /etc/selinux/config.bak."
    mv /etc/selinux/config /etc/selinux/config.bak
fi
echo "Creating new /etc/selinux/config for dummy policy."
cat > /etc/selinux/config << EOF
SELINUX=permissive
SELINUXTYPE=dummy
EOF

cd /etc/selinux/dummy/contexts/files
$SF -F file_contexts /

mounts=`cat /proc/$$/mounts | \
	grep -E "ext[234]|jfs|xfs|reiserfs|jffs2|gfs2|btrfs|f2fs|ocfs2" | \
	awk '{ print $2 '}`
$SF -F file_contexts $mounts

echo "-F" > /.autorelabel
