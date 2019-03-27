#!/bin/sh
#
# Copyright (c) 2013 Hudson River Trading LLC
# Written by: John H. Baldwin <jhb@FreeBSD.org>
# All rights reserved.
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

# Regression tests for the pre-world (-p) mode 

FAILED=no
WORKDIR=work

usage()
{
	echo "Usage: preworld.sh [-s script] [-w workdir]"
	exit 1
}

# Allow the user to specify an alternate work directory or script.
COMMAND=etcupdate
while getopts "s:w:" option; do
	case $option in
		s)
			COMMAND="sh $OPTARG"
			;;
		w)
			WORKDIR=$OPTARG
			;;
		*)
			echo
			usage
			;;
	esac
done
shift $((OPTIND - 1))
if [ $# -ne 0 ]; then
	usage
fi

CONFLICTS=$WORKDIR/conflicts
SRC=$WORKDIR/src
OLD=$WORKDIR/current
TEST=$WORKDIR/test

build_trees()
{

	# Populate trees with pre-world files and additional files
	# that should not be touched.

	rm -rf $SRC $OLD $TEST $CONFLICTS

	# Create the "old" source tree as the starting point
	mkdir -p $OLD/etc
	cat >> $OLD/etc/master.passwd <<EOF
#
root::0:0::0:0:Charlie &:/root:/bin/csh
toor:*:0:0::0:0:Bourne-again Superuser:/root:
daemon:*:1:1::0:0:Owner of many system processes:/root:/usr/sbin/nologin
operator:*:2:5::0:0:System &:/:/usr/sbin/nologin
_dhcp:*:65:65::0:0:dhcp programs:/var/empty:/usr/sbin/nologin
uucp:*:66:66::0:0:UUCP pseudo-user:/var/spool/uucppublic:/usr/local/libexec/uucp/uucico
pop:*:68:6::0:0:Post Office Owner:/nonexistent:/usr/sbin/nologin
www:*:80:80::0:0:World Wide Web Owner:/nonexistent:/usr/sbin/nologin
hast:*:845:845::0:0:HAST unprivileged user:/var/empty:/usr/sbin/nologin
nobody:*:65534:65534::0:0:Unprivileged user:/nonexistent:/usr/sbin/nologin
EOF
	cat >> $OLD/etc/group <<EOF
#
wheel:*:0:root
daemon:*:1:
kmem:*:2:
sys:*:3:
tty:*:4:
operator:*:5:root
_dhcp:*:65:
uucp:*:66:
dialer:*:68:
network:*:69:
www:*:80:
hast:*:845:
nogroup:*:65533:
nobody:*:65534:
EOF
	cat >> $OLD/etc/inetd.conf <<EOF
# Yet another file
EOF

	# Copy the "old" source tree to the test tree and make local
	# modifications.
	cp -R $OLD $TEST
	sed -I "" -e 's/root::/root:<rpass>:/' $TEST/etc/master.passwd
	cat >> $TEST/etc/master.passwd <<EOF
john:<password>:1001:1001::0:0:John Baldwin:/home/john:/bin/tcsh
messagebus:*:556:556::0:0:D-BUS Daemon User:/nonexistent:/usr/sbin/nologin
polkit:*:562:562::0:0:PolicyKit User:/nonexistent:/usr/sbin/nologin
haldaemon:*:560:560::0:0:HAL Daemon User:/nonexistent:/usr/sbin/nologin
EOF
	awk '/wheel/ { printf "%s,john\n", $0; next } // { print }' \
	    $OLD/etc/group > $TEST/etc/group
	cat >> $TEST/etc/group <<EOF
john:*:1001:
messagebus:*:556:
polkit:*:562:
haldaemon:*:560:
EOF
	rm $TEST/etc/inetd.conf
	touch $TEST/etc/localtime

	# Copy the "old" source tree to the new source tree and
	# make upstream modifications.
	cp -R $OLD $SRC
	sed -I "" -e '/:80:/i\
auditdistd:*:78:77::0:0:Auditdistd unprivileged user:/var/empty:/usr/sbin/nologin' \
	    $SRC/etc/master.passwd
	sed -I "" -e '/:80:/i\
audit:*:77:' \
	    $SRC/etc/group
	cat >> $SRC/etc/inetd.conf <<EOF
# Making this larger
EOF
}

# $1 - relative path to file that should be missing from TEST
missing()
{
	if [ -e $TEST/$1 -o -L $TEST/$1 ]; then
		echo "File $1 should be missing"
		FAILED=yes
	fi
}

# $1 - relative path to file that should be present in TEST
present()
{
	if ! [ -e $TEST/$1 -o -L $TEST/$1 ]; then
		echo "File $1 should be present"
		FAILED=yes
	fi
}

# $1 - relative path to regular file that should be present in TEST
# $2 - optional string that should match file contents
# $3 - optional MD5 of the flie contents, overrides $2 if present
file()
{
	local contents sum

	if ! [ -f $TEST/$1 ]; then
		echo "File $1 should be a regular file"
		FAILED=yes
	elif [ $# -eq 2 ]; then
		contents=`cat $TEST/$1`
		if [ "$contents" != "$2" ]; then
			echo "File $1 has wrong contents"
			FAILED=yes
		fi
	elif [ $# -eq 3 ]; then
		sum=`md5 -q $TEST/$1`
		if [ "$sum" != "$3" ]; then
			echo "File $1 has wrong contents"
			FAILED=yes
		fi
	fi
}

# $1 - relative path to a regular file that should have a conflict
# $2 - optional MD5 of the conflict file contents
conflict()
{
	local sum

	if ! [ -f $CONFLICTS/$1 ]; then
		echo "File $1 missing conflict"
		FAILED=yes
	elif [ $# -gt 1 ]; then
		sum=`md5 -q $CONFLICTS/$1`
		if [ "$sum" != "$2" ]; then
			echo "Conflict $1 has wrong contents"
			FAILED=yes
		fi
	fi
}

check_trees()
{

	echo "Checking tree for correct results:"

	file /etc/master.passwd "" 1385366e8b424d33d59b7d8a2bdb15d3
	file /etc/group "" 21273f845f6ec0cda9188c4ddac9ed47
	missing /etc/inetd.conf

	# These should be auto-generated by pwd_mkdb
	file /etc/passwd "" 9831537874bdc99adccaa2b0293248a1
	file /etc/pwd.db
	file /etc/spwd.db
}

if [ `id -u` -ne 0 ]; then
	echo "must be root"
	exit 0
fi

if [ -r /etc/etcupdate.conf ]; then
	echo "WARNING: /etc/etcupdate.conf settings may break some tests."
fi

build_trees

$COMMAND -np -s $SRC -d $WORKDIR -D $TEST > $WORKDIR/testn.out

cat > $WORKDIR/correct.out <<EOF
  M /etc/group
  M /etc/master.passwd
EOF

echo "Differences for -n:"
diff -u -L "correct" $WORKDIR/correct.out -L "test" $WORKDIR/testn.out \
    || FAILED=yes

$COMMAND -p -s $SRC -d $WORKDIR -D $TEST > $WORKDIR/test.out

echo "Differences for real:"
diff -u -L "correct" $WORKDIR/correct.out -L "test" $WORKDIR/test.out \
    || FAILED=yes

check_trees

[ "${FAILED}" = no ]
