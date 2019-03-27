#!/bin/sh
#
# Copyright (c) 2012 The FreeBSD Foundation
# All rights reserved.
#
# This software was developed by Edward Tomasz Napierala under sponsorship
# from the FreeBSD Foundation.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
# OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
# OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.
#
# $FreeBSD$
#

#
# This expects that the iSCSI server being tested is at $TARGETIP and exports
# two targets: $TARGET1 and $TARGET2; the former requiring no authentication,
# and the latter using CHAP with user $USER and secret $SECRET.  Discovery
# must be permitted without authentication.  Each target must contain exactly
# two LUNs, 4GB each.  For example, ctl.conf(5) should look like this:
# 
# auth-group meh {
# 	chap user secretsecret
# }
# 
# portal-group meh {
# 	listen 0.0.0.0
# 	discovery-auth-group no-authentication
# }
# 
# target iqn.2012-06.com.example:1 {
# 	auth-group no-authentication
# 	portal-group meh
# 	lun 0 {
# 		path /var/tmp/example_t1l0
# 		size 4G
# 	}
# 	lun 1 {
# 		path /var/tmp/example_t1l1
# 		size 4G
# 	}
# }
# 
# target iqn.2012-06.com.example:2 {
# 	auth-group meh
# 	portal-group meh
# 	lun 0 {
# 		path /var/tmp/example_t2l0
# 		size 4G
# 	}
# 	lun 1 {
# 		path /var/tmp/example_t2l1
# 		size 4G
# 	}
# }
# 
# Remember to create the backing files (/var/tmp/example_t1l0 etcc)
#
# On the initiator, $MNTDIR will be used for testing.
#

TARGETIP=192.168.56.101
TARGET1=iqn.2012-06.com.example:1
TARGET2=iqn.2012-06.com.example:2
USER=user
SECRET=secretsecret
MNTDIR=/mnt
TMPDIR=/tmp

die() {
	echo "$*"
	exit 1
}

case `uname` in
	FreeBSD)
		LUN0=/dev/da0
		LUN1=/dev/da1
		LUN2=/dev/da2
		LUN3=/dev/da3
		ZFSPOOL=iscsipool
		;;
	Linux)
		LUN0=/dev/sdb
		LUN1=/dev/sdc
		LUN2=/dev/sdd
		LUN3=/dev/sde
		;;
	SunOS)
		# LUN names are being set later, during attach.
		ZFSPOOL=iscsipool
		;;
	*)
		die "unsupported system"
		;;
esac

check() {
	echo "# $@" > /dev/stderr
	$@ || die "$@ failed"
}

banner() {
	echo "Will try to attach to $TARGET1 and $TARGET2 on $TARGETIP,"
	echo "user $USER, secret $SECRET.  Will use mountpoint $MNTDIR, temporary dir $TMPDIR,"
	if [ -n "$LUN0" ]; then
		echo "scratch disks $LUN0, $LUN1, $LUN2, $LUN3."
	else
		echo "scratch disks unknown at this stage."
	fi
	echo
	echo "This script is NOT safe to run on multiuser system."
	echo
	echo "Press ^C to interrupt; will proceed in 5 seconds."
	sleep 5
}

test_discovery_freebsd_9() {
	kldload iscsi_initiator
	check iscontrol -dt $TARGETIP > $TMPDIR/discovered
	cat $TMPDIR/discovered
	echo "TargetName=$TARGET1" > $TMPDIR/expected
	echo "TargetName=$TARGET2" >> $TMPDIR/expected
	check cmp $TMPDIR/expected $TMPDIR/discovered
	rm -f $TMPDIR/expected $TMPDIR/discovered
}

test_discovery_freebsd() {
	/etc/rc.d/iscsid onestart
	check iscsictl -Ad $TARGETIP
	sleep 1
	iscsictl | awk '{ print $1 }' | sort > $TMPDIR/discovered
	printf "Target\n$TARGET1\n$TARGET2\n" | sort > $TMPDIR/expected
	check cmp $TMPDIR/expected $TMPDIR/discovered
	rm -f $TMPDIR/expected $TMPDIR/discovered
	check iscsictl -Ra
	sleep 1
}

test_discovery_linux() {
	cat > /etc/iscsi/iscsid.conf << END

discovery.sendtargets.auth.authmethod = None
node.startup = manual

END

	check iscsiadm  -m discovery -t sendtargets -p $TARGETIP > $TMPDIR/discovered
	cat $TMPDIR/discovered
	echo "$TARGETIP:3260,-1 $TARGET1" > $TMPDIR/expected
	echo "$TARGETIP:3260,-1 $TARGET2" >> $TMPDIR/expected
	check cmp $TMPDIR/expected $TMPDIR/discovered
	rm -f $TMPDIR/expected $TMPDIR/discovered

}

test_discovery_solaris() {
	check iscsiadm add discovery-address $TARGETIP
	check iscsiadm modify discovery --sendtargets enable
	check iscsiadm modify discovery --static enable
	check iscsiadm list target | awk '/^Target/ { print $2 }' | sort > $TMPDIR/discovered
	check iscsiadm remove discovery-address $TARGETIP
	cat $TMPDIR/discovered
	echo "$TARGET1" > $TMPDIR/expected
	echo "$TARGET2" >> $TMPDIR/expected
	check cmp $TMPDIR/expected $TMPDIR/discovered
	rm -f $TMPDIR/expected $TMPDIR/discovered
}

test_discovery() {
	echo "*** discovery test ***"
	case `uname` in
		FreeBSD)
			case `uname -r` in
				9*)
					test_discovery_freebsd_9
					;;
				*)
					test_discovery_freebsd
					;;
			esac
			;;
		Linux)
			test_discovery_linux
			;;
		SunOS)
			test_discovery_solaris
			;;
		*)
			die "unsupported system"
			;;
	esac
}

test_attach_freebsd_9() {
	[ ! -e LUN0 ] || die "$LUN0 already exists"
	[ ! -e LUN1 ] || die "$LUN1 already exists"
	[ ! -e LUN2 ] || die "$LUN2 already exists"
	[ ! -e LUN3 ] || die "$LUN3 already exists"

	cat > $TMPDIR/iscsi.conf << END

target1 {
	TargetName = $TARGET1
	TargetAddress = $TARGETIP
}

target2 {
	TargetName = $TARGET2
	TargetAddress = $TARGETIP
	AuthMethod = CHAP
	chapIName = $USER
	chapSecret = $SECRET
}

END
	check iscontrol -c $TMPDIR/iscsi.conf -n target1
	check iscontrol -c $TMPDIR/iscsi.conf -n target2

	echo "Waiting 10 seconds for attach to complete."
	sleep 10

	[ -e $LUN0 ] || die "$LUN0 doesn't exist"
	[ -e $LUN1 ] || die "$LUN1 doesn't exist"
	[ -e $LUN2 ] || die "$LUN2 doesn't exist"
	[ -e $LUN3 ] || die "$LUN3 doesn't exist"

	rm $TMPDIR/iscsi.conf
}

test_attach_freebsd() {
	[ ! -e LUN0 ] || die "$LUN0 already exists"
	[ ! -e LUN1 ] || die "$LUN1 already exists"
	[ ! -e LUN2 ] || die "$LUN2 already exists"
	[ ! -e LUN3 ] || die "$LUN3 already exists"

	cat > $TMPDIR/iscsi.conf << END

target1 {
	TargetName = $TARGET1
	TargetAddress = $TARGETIP
}

target2 {
	TargetName = $TARGET2
	TargetAddress = $TARGETIP
	AuthMethod = CHAP
	chapIName = $USER
	chapSecret = $SECRET
}

END
	check iscsictl -Ac $TMPDIR/iscsi.conf -n target1
	check iscsictl -Ac $TMPDIR/iscsi.conf -n target2

	echo "Waiting 10 seconds for attach to complete."
	sleep 10

	[ -e $LUN0 ] || die "$LUN0 doesn't exist"
	[ -e $LUN1 ] || die "$LUN1 doesn't exist"
	[ -e $LUN2 ] || die "$LUN2 doesn't exist"
	[ -e $LUN3 ] || die "$LUN3 doesn't exist"

	rm $TMPDIR/iscsi.conf
}

test_attach_linux() {
	check iscsiadm --mode node --targetname "$TARGET1"  -p "$TARGETIP:3260" --op=update --name node.session.auth.authmethod --value=None
	check iscsiadm --mode node --targetname "$TARGET1"  -p "$TARGETIP:3260" --login
	check iscsiadm --mode node --targetname "$TARGET2"  -p "$TARGETIP:3260" --op=update --name node.session.auth.authmethod --value=CHAP
	check iscsiadm --mode node --targetname "$TARGET2"  -p "$TARGETIP:3260" --op=update --name node.session.auth.username --value="$USER"
	check iscsiadm --mode node --targetname "$TARGET2"  -p "$TARGETIP:3260" --op=update --name node.session.auth.password --value="$SECRET"
	check iscsiadm --mode node --targetname "$TARGET2"  -p "$TARGETIP:3260" --login
}

test_attach_solaris() {
	# XXX: How to enter the CHAP secret from the script?  For now,
	# just use the first target, and thus first two LUNs.
	#check iscsiadm modify initiator-node --authentication CHAP
	#check iscsiadm modify initiator-node --CHAP-name $USER
	#check iscsiadm modify initiator-node --CHAP-secret $SECRET
	iscsiadm add static-config $TARGET1,$TARGETIP
	LUN0=`iscsiadm list target -S | awk '/OS Device Name/ { print $4 }' | sed -n 1p`
	LUN1=`iscsiadm list target -S | awk '/OS Device Name/ { print $4 }' | sed -n 2p`
	LUN0=`echo ${LUN0}2 | sed 's/rdsk/dsk/'`
	LUN1=`echo ${LUN1}2 | sed 's/rdsk/dsk/'`
	[ -n "$LUN0" -a -n "LUN1" ] || die "attach failed"
	echo "Scratch disks: $LUN0, $LUN1"
}

test_attach() {
	echo "*** attach test ***"
	case `uname` in
		FreeBSD)
			case `uname -r` in
				9*)
					test_attach_freebsd_9
					;;
				*)
					test_attach_freebsd
					;;
			esac
			;;
		Linux)
			test_attach_linux
			;;
		SunOS)
			test_attach_solaris
			;;
		*)
			die "unsupported system"
			;;
	esac
}

test_newfs_freebsd_ufs() {
	echo "*** UFS filesystem smoke test ***"
	check newfs $LUN0
	check newfs $LUN1
	check newfs $LUN2
	check newfs $LUN3
	check fsck -t ufs $LUN0
	check fsck -t ufs $LUN1
	check fsck -t ufs $LUN2
	check fsck -t ufs $LUN3
}

test_newfs_freebsd_zfs() {
	echo "*** ZFS filesystem smoke test ***"
	check zpool create -f $ZFSPOOL $LUN0 $LUN1 $LUN2 $LUN3
	check zpool destroy -f $ZFSPOOL
}

test_newfs_linux_ext4() {
	echo "*** ext4 filesystem smoke test ***"
	yes | check mkfs $LUN0
	yes | check mkfs $LUN1
	yes | check mkfs $LUN2
	yes | check mkfs $LUN3
	check fsck -f $LUN0
	check fsck -f $LUN1
	check fsck -f $LUN2
	check fsck -f $LUN3
}

test_newfs_linux_btrfs() {
	echo "*** btrfs filesystem smoke test ***"
	check mkfs.btrfs $LUN0 $LUN1 $LUN2 $LUN3
}


test_newfs_solaris_ufs() {
	echo "*** UFS filesystem smoke test ***"
	yes | check newfs $LUN0
	yes | check newfs $LUN1
	check fsck -F ufs $LUN0
	check fsck -F ufs $LUN1
}

test_newfs_solaris_zfs() {
	echo "*** ZFS filesystem smoke test ***"
	check zpool create -f $ZFSPOOL $LUN0 $LUN1
	check zpool destroy -f $ZFSPOOL
}

test_newfs() {
	case `uname` in
		FreeBSD)
			test_newfs_freebsd_ufs
			test_newfs_freebsd_zfs
			;;
		Linux)
			test_newfs_linux_ext4
			test_newfs_linux_btrfs
			;;
		SunOS)
			test_newfs_solaris_ufs
			test_newfs_solaris_zfs
			;;
		*)
			die "unsupported system"
			;;
	esac
}

test_cp() {
	echo "*** basic filesystem test ***"
	case `uname` in
		FreeBSD)
			check newfs $LUN0
			check mount -t ufs $LUN0 $MNTDIR
			check dd if=/dev/urandom of=$MNTDIR/1 bs=1m count=500
			check cp $MNTDIR/1 $MNTDIR/2
			check umount $MNTDIR
			check fsck -t ufs $LUN0
			check mount -t ufs $LUN0 $MNTDIR
			check cmp $MNTDIR/1 $MNTDIR/2
			check umount $MNTDIR

			check zpool create -f $ZFSPOOL $LUN0 $LUN1 $LUN2 $LUN3
			check dd if=/dev/urandom of=/$ZFSPOOL/1 bs=1m count=500
			check zpool scrub $ZFSPOOL
			check cp /$ZFSPOOL/1 /$ZFSPOOL/2
			check cmp /$ZFSPOOL/1 /$ZFSPOOL/2
			check zpool destroy -f $ZFSPOOL
			;;
		Linux)
			yes | check mkfs $LUN0
			check mount $LUN0 $MNTDIR
			check dd if=/dev/urandom of=$MNTDIR/1 bs=1M count=500
			check cp $MNTDIR/1 $MNTDIR/2
			check umount $MNTDIR
			check fsck -f $LUN0
			check mount $LUN0 $MNTDIR
			check cmp $MNTDIR/1 $MNTDIR/2
			check umount $MNTDIR

			check mkfs.btrfs $LUN0 $LUN1 $LUN2 $LUN3
			check mount $LUN0 $MNTDIR
			check dd if=/dev/urandom of=$MNTDIR/1 bs=1M count=500
			check cp $MNTDIR/1 $MNTDIR/2
			check umount $MNTDIR
			check mount $LUN0 $MNTDIR
			check cmp $MNTDIR/1 $MNTDIR/2
			check umount $MNTDIR
			;;
		SunOS)
			yes | check newfs $LUN0
			check mount -F ufs $LUN0 $MNTDIR
			check dd if=/dev/urandom of=$MNTDIR/1 bs=1024k count=500
			check cp $MNTDIR/1 $MNTDIR/2
			check umount $MNTDIR
			check fsck -yF ufs $LUN0
			check mount -F ufs $LUN0 $MNTDIR
			check cmp $MNTDIR/1 $MNTDIR/2
			check umount $MNTDIR

			check zpool create -f $ZFSPOOL $LUN0 $LUN1
			check dd if=/dev/urandom of=/$ZFSPOOL/1 bs=1024k count=500
			check zpool scrub $ZFSPOOL
			check cp /$ZFSPOOL/1 /$ZFSPOOL/2
			check cmp /$ZFSPOOL/1 /$ZFSPOOL/2
			check zpool destroy -f $ZFSPOOL
			;;
		*)
			die "unsupported system"
			;;
	esac
}

test_bonnie() {
	echo "*** bonnie++ ***"
	case `uname` in
		FreeBSD)
			check newfs $LUN0
			check mount -t ufs $LUN0 $MNTDIR
			check cd $MNTDIR
			check bonnie++ -u root -s 2g -r 1g -n0
			check bonnie++ -u root -s 0
			check cd -
			check umount $MNTDIR
			check fsck -t ufs $LUN0

			check zpool create -f $ZFSPOOL $LUN0 $LUN1 $LUN2 $LUN3
			check cd /$ZFSPOOL
			check bonnie++ -u root -s 2g -r 1g -n0
			check bonnie++ -u root -s 0
			check cd -
			check zpool destroy -f $ZFSPOOL
			;;
		Linux)
			yes | check mkfs $LUN0
			check mount $LUN0 $MNTDIR
			check cd $MNTDIR
			check bonnie++ -u root -s 2g -r 1g -n0
			check bonnie++ -u root -s 0
			check cd -
			check umount $MNTDIR
			check fsck -f $LUN0

			check mkfs.btrfs $LUN0 $LUN1 $LUN2 $LUN3
			check mount $LUN0 $MNTDIR
			check cd $MNTDIR
			check bonnie++ -u root -s 2g -r 1g -n0
			check bonnie++ -u root -s 0
			check cd -
			check umount $MNTDIR
			;;
		SunOS)
			yes | check newfs $LUN0
			check mount -F ufs $LUN0 $MNTDIR
			check cd $MNTDIR
			check bonnie++ -u root -s 2g -r 1g -n0
			check bonnie++ -u root -s 0
			check cd -
			check umount $MNTDIR
			check fsck -yF ufs $LUN0

			check zpool create -f $ZFSPOOL $LUN0 $LUN1
			check cd /$ZFSPOOL
			check bonnie++ -u root -s 2g -r 1g -n0
			check bonnie++ -u root -s 0
			check cd -
			check zpool destroy -f $ZFSPOOL
			;;
		*)
			die "unsupported system"
			;;
	esac
}

test_iozone() {
	echo "*** iozone ***"
	case `uname` in
		FreeBSD)
			check newfs $LUN0
			check mount -t ufs $LUN0 $MNTDIR
			check cd $MNTDIR
			check iozone -a
			check cd -
			check umount $MNTDIR
			check fsck -t ufs $LUN0

			check zpool create -f $ZFSPOOL $LUN0 $LUN1 $LUN2 $LUN3
			check cd /$ZFSPOOL
			check iozone -a
			check cd -
			check zpool destroy -f $ZFSPOOL
			;;
		Linux)
			yes | check mkfs $LUN0
			check mount $LUN0 $MNTDIR
			check cd $MNTDIR
			check iozone -a
			check cd -
			check umount $MNTDIR
			check fsck -f $LUN0

			check mkfs.btrfs $LUN0 $LUN1 $LUN2 $LUN3
			check mount $LUN0 $MNTDIR
			check cd $MNTDIR
			check iozone -a
			check cd -
			check umount $MNTDIR
			;;
		SunOS)
			yes | check newfs $LUN0
			check mount -F ufs $LUN0 $MNTDIR
			check cd $MNTDIR
			check iozone -a
			check cd -
			check umount $MNTDIR
			check fsck -yF ufs $LUN0

			check zpool create -f $ZFSPOOL $LUN0 $LUN1
			check cd /$ZFSPOOL
			check iozone -a
			check cd -
			check zpool destroy -f $ZFSPOOL
			;;
		*)
			die "unsupported system"
			;;
	esac

}

test_postmark() {
	echo "*** postmark ***"
	case `uname` in
		FreeBSD)
			check newfs $LUN0
			check mount -t ufs $LUN0 $MNTDIR
			check cd $MNTDIR
			printf "set number 10000\nrun\n" | check postmark
			check cd -
			check umount $MNTDIR
			check fsck -t ufs $LUN0

			check zpool create -f $ZFSPOOL $LUN0 $LUN1 $LUN2 $LUN3
			check cd /$ZFSPOOL
			printf "set number 10000\nrun\n" | check postmark
			check cd -
			check zpool destroy -f $ZFSPOOL
			;;
		Linux)
			yes | check mkfs $LUN0
			check mount $LUN0 $MNTDIR
			check cd $MNTDIR
			printf "set number 10000\nrun\n" | check postmark
			check cd -
			check umount $MNTDIR
			check fsck -f $LUN0

			check mkfs.btrfs $LUN0 $LUN1 $LUN2 $LUN3
			check mount $LUN0 $MNTDIR
			check cd $MNTDIR
			printf "set number 10000\nrun\n" | check postmark
			check cd -
			check umount $MNTDIR
			;;
		SunOS)
			yes | check newfs $LUN0
			check mount -F ufs $LUN0 $MNTDIR
			check cd $MNTDIR
			printf "set number 10000\nrun\n" | check postmark
			check cd -
			check umount $MNTDIR
			check fsck -yF ufs $LUN0

			check zpool create -f $ZFSPOOL $LUN0 $LUN1
			check cd /$ZFSPOOL
			printf "set number 10000\nrun\n" | check postmark
			check cd -
			check zpool destroy -f $ZFSPOOL
			;;
		*)
			die "unsupported system"
			;;
	esac
}

test_postgresql_freebsd() {
	check newfs $LUN0
	check mount -t ufs $LUN0 $MNTDIR
	check chown pgsql $MNTDIR
	check chmod 755 $MNTDIR
	check cd /
	# XXX: How to make 'check' work here?
	su -m pgsql -c "initdb -D $MNTDIR/db"
	su -m pgsql -c "pg_ctl -D $MNTDIR/db -l /tmp/log start"
	check sleep 10
	su -m pgsql -c "pgbench -i postgres"
	su -m pgsql -c "pgbench -t 10000 postgres"
	su -m pgsql -c "pg_ctl -D $MNTDIR/db stop"
	check cd -
	check umount $MNTDIR
	check fsck -t ufs $LUN0

	check zpool create -f $ZFSPOOL $LUN0 $LUN1 $LUN2 $LUN3
	check chown pgsql /$ZFSPOOL
	check chmod 755 /$ZFSPOOL
	check cd /
	# XXX: How to make 'check' work here?
	su -m pgsql -c "initdb -D /$ZFSPOOL/db"
	su -m pgsql -c "pg_ctl -D /$ZFSPOOL/db -l /tmp/log start"
	check sleep 10
	su -m pgsql -c "pgbench -i postgres"
	su -m pgsql -c "pgbench -t 10000 postgres"
	su -m pgsql -c "pg_ctl -D /$ZFSPOOL/db stop"
	check cd -
	check zpool destroy -f $ZFSPOOL
}

test_postgresql_linux() {
	yes | check mkfs $LUN0
	check mount $LUN0 $MNTDIR
	check chown postgres $MNTDIR
	check chmod 755 $MNTDIR
	check cd /
	# XXX: How to make 'check' work here?
	su -m postgres -c "initdb -D $MNTDIR/db"
	su -m postgres -c "pg_ctl -D $MNTDIR/db -l /tmp/log start"
	check sleep 5
	su -m postgres -c "pgbench -i"
	su -m postgres -c "pgbench -t 10000"
	su -m postgres -c "pg_ctl -D $MNTDIR/db stop"
	check cd -
	check umount $MNTDIR
	check fsck -f $LUN0

	check mkfs.btrfs $LUN0 $LUN1 $LUN2 $LUN3
	check mount $LUN0 $MNTDIR
	check chown postgres $MNTDIR
	check chmod 755 $MNTDIR
	check cd /
	su -m postgres -c "initdb -D $MNTDIR/db"
	su -m postgres -c "pg_ctl -D $MNTDIR/db -l /tmp/log start"
	check sleep 5
	su -m postgres -c "pgbench -i"
	su -m postgres -c "pgbench -t 10000"
	su -m postgres -c "pg_ctl -D $MNTDIR/db stop"
	check cd -
	check umount $MNTDIR
}

test_postgresql_solaris() {
	PATH="$PATH:/usr/postgres/9.2-pgdg/bin" export PATH
	yes | check newfs $LUN0
	check mount -F ufs $LUN0 $MNTDIR
	check chown postgres $MNTDIR
	check chmod 755 $MNTDIR
	check cd /
	# XXX: How to make 'check' work here?
	su postgres -c "initdb -D $MNTDIR/db"
	su postgres -c "pg_ctl -D $MNTDIR/db -l /tmp/log start"
	check sleep 10
	su postgres -c "pgbench -i postgres"
	su postgres -c "pgbench -t 10000 postgres"
	su postgres -c "pg_ctl -D $MNTDIR/db stop"
	check cd -
	check umount $MNTDIR
	check fsck -yF ufs $LUN0

	check zpool create -f $ZFSPOOL $LUN0 $LUN1 $LUN2 $LUN3
	check chown postgres /$ZFSPOOL
	check chmod 755 /$ZFSPOOL
	check cd /
	# XXX: How to make 'check' work here?
	su postgres -c "initdb -D /$ZFSPOOL/db"
	su postgres -c "pg_ctl -D /$ZFSPOOL/db -l /tmp/log start"
	check sleep 10
	su postgres -c "pgbench -i postgres"
	su postgres -c "pgbench -t 10000 postgres"
	su postgres -c "pg_ctl -D /$ZFSPOOL/db stop"
	check cd -
	check zpool destroy -f $ZFSPOOL
}

test_postgresql() {
	echo "*** postgresql ***"
	case `uname` in
		FreeBSD)
			test_postgresql_freebsd
			;;
		Linux)
			test_postgresql_linux
			;;
		SunOS)
			test_postgresql_solaris
			;;
		*)
			die "unsupported system"
			;;
	esac
}

test_detach() {
	echo "*** detach ***"
	case `uname` in
		FreeBSD)
			case `uname -r` in
				9*)
					echo "*** detaching not supported on FreeBSD 9 ***"
					echo "*** please reboot the initiator VM before trying to run this script again ***"
					;;
				*)
					check iscsictl -Ra
					;;
			esac
			;;
		Linux)
			check iscsiadm -m node --logout
			;;
		SunOS)
			check iscsiadm remove static-config $TARGET1,$TARGETIP
			;;
		*)
			die "unsupported system"
			;;
	esac
}

banner
test_discovery
test_attach
test_newfs
test_cp
test_bonnie
test_iozone
test_postmark
test_postgresql
test_detach

echo "*** done ***"

