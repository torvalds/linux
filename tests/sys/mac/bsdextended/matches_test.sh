#!/bin/sh
#
# $FreeBSD$
#

uidrange="60000:100000"
gidrange="60000:100000"
uidinrange="nobody"
uidoutrange="daemon"
gidinrange="nobody" # We expect $uidinrange in this group
gidoutrange="daemon" # We expect $uidinrange in this group

test_num=1
pass()
{
	echo "ok $test_num # $@"
	: $(( test_num += 1 ))
}

fail()
{
	echo "not ok $test_num # $@"
	: $(( test_num += 1 ))
}

#
# Setup
#

: ${TMPDIR=/tmp}
if [ $(id -u) -ne 0 ]; then
	echo "1..0 # SKIP test must be run as root"
	exit 0
fi
if ! sysctl -N security.mac.bsdextended >/dev/null 2>&1; then
	echo "1..0 # SKIP mac_bsdextended(4) support isn't available"
	exit 0
fi
if [ "$TMPDIR" != "/tmp" ]; then
	if ! chmod -Rf 0755 $TMPDIR; then
		echo "1..0 # SKIP failed to chmod $TMPDIR"
		exit 0
	fi
fi
if ! playground=$(mktemp -d $TMPDIR/tmp.XXXXXXX); then
	echo "1..0 # SKIP failed to create temporary directory"
	exit 0
fi
trap "rmdir $playground" EXIT INT TERM
if ! mdmfs -s 25m md $playground; then
	echo "1..0 # SKIP failed to mount md device"
	exit 0
fi
chmod a+rwx $playground
md_device=$(mount -p | grep "$playground" | awk '{ gsub(/^\/dev\//, "", $1); print $1 }')
trap "umount -f $playground; mdconfig -d -u $md_device; rmdir $playground" EXIT INT TERM
if [ -z "$md_device" ]; then
	mount -p | grep $playground
	echo "1..0 # SKIP md device not properly attached to the system"
fi

ugidfw remove 1

file1=$playground/test-$uidinrange
file2=$playground/test-$uidoutrange
cat > $playground/test-script.sh <<'EOF'
#!/bin/sh
: > $1
EOF
if [ $? -ne 0 ]; then
	echo "1..0 # SKIP failed to create test script"
	exit 0
fi
echo "1..30"

command1="sh $playground/test-script.sh $file1"
command2="sh $playground/test-script.sh $file2"

desc="$uidinrange file"
if su -m $uidinrange -c "$command1"; then
	pass $desc
else
	fail $desc
fi

chown "$uidinrange":"$gidinrange" $file1
chmod a+w $file1

desc="$uidoutrange file"
if $command2; then
	pass $desc
else
	fail $desc
fi

chown "$uidoutrange":"$gidoutrange" $file2
chmod a+w $file2

#
# No rules
#
desc="no rules $uidinrange"
if su -fm $uidinrange -c "$command1"; then
	pass $desc
else
	fail $desc
fi

desc="no rules $uidoutrange"
if su -fm $uidoutrange -c "$command1"; then
	pass $desc
else
	fail $desc
fi

#
# Subject Match on uid
#
ugidfw set 1 subject uid $uidrange object mode rasx
desc="subject uid in range"
if su -fm $uidinrange -c "$command1"; then
	fail $desc
else
	pass $desc
fi

desc="subject uid out range"
if su -fm $uidoutrange -c "$command1"; then
	pass $desc
else
	fail $desc
fi

#
# Subject Match on gid
#
ugidfw set 1 subject gid $gidrange object mode rasx

desc="subject gid in range"
if su -fm $uidinrange -c "$command1"; then
	fail $desc
else
	pass $desc
fi

desc="subject gid out range"
if su -fm $uidoutrange -c "$command1"; then
	pass $desc
else
	fail $desc
fi

if which jail >/dev/null; then
	#
	# Subject Match on jail
	#
	rm -f $playground/test-jail

	desc="subject matching jailid"
	jailid=`jail -i / localhost 127.0.0.1 /usr/sbin/daemon -f /bin/sh -c "(sleep 5; touch $playground/test-jail) &"`
	ugidfw set 1 subject jailid $jailid object mode rasx
	sleep 10

	if [ -f $playground/test-jail ]; then
		fail "TODO $desc: this testcase fails (see bug # 205481)"
	else
		pass $desc
	fi

	rm -f $playground/test-jail
	desc="subject nonmatching jailid"
	jailid=`jail -i / localhost 127.0.0.1 /usr/sbin/daemon -f /bin/sh -c "(sleep 5; touch $playground/test-jail) &"`
	sleep 10
	if [ -f $playground/test-jail ]; then
		pass $desc
	else
		fail $desc
	fi
else
	# XXX: kyua is too dumb to parse skip ranges, still..
	pass "skip jail(8) not installed"
	pass "skip jail(8) not installed"
fi

#
# Object uid
#
ugidfw set 1 subject object uid $uidrange mode rasx

desc="object uid in range"
if su -fm $uidinrange -c "$command1"; then
	fail $desc
else
	pass $desc
fi

desc="object uid out range"
if su -fm $uidinrange -c "$command2"; then
	pass $desc
else
	fail $desc
fi
ugidfw set 1 subject object uid $uidrange mode rasx

desc="object uid in range (different subject)"
if su -fm $uidoutrange -c "$command1"; then
	fail $desc
else
	pass $desc
fi

desc="object uid out range (different subject)"
if su -fm $uidoutrange -c "$command2"; then
	pass $desc
else
	fail $desc
fi

#
# Object gid
#
ugidfw set 1 subject object gid $uidrange mode rasx

desc="object gid in range"
if su -fm $uidinrange -c "$command1"; then
	fail $desc
else
	pass $desc
fi

desc="object gid out range"
if su -fm $uidinrange -c "$command2"; then
	pass $desc
else
	fail $desc
fi
desc="object gid in range (different subject)"
if su -fm $uidoutrange -c "$command1"; then
	fail $desc
else
	pass $desc
fi

desc="object gid out range (different subject)"
if su -fm $uidoutrange -c "$command2"; then
	pass $desc
else
	fail $desc
fi

#
# Object filesys
#
ugidfw set 1 subject uid $uidrange object filesys / mode rasx
desc="object out of filesys"
if su -fm $uidinrange -c "$command1"; then
	pass $desc
else
	fail $desc
fi

ugidfw set 1 subject uid $uidrange object filesys $playground mode rasx
desc="object in filesys"
if su -fm $uidinrange -c "$command1"; then
	fail $desc
else
	pass $desc
fi

#
# Object suid
#
ugidfw set 1 subject uid $uidrange object suid mode rasx
desc="object notsuid"
if su -fm $uidinrange -c "$command1"; then
	pass $desc
else
	fail $desc
fi

chmod u+s $file1
desc="object suid"
if su -fm $uidinrange -c "$command1"; then
	fail $desc
else
	pass $desc
fi
chmod u-s $file1

#
# Object sgid
#
ugidfw set 1 subject uid $uidrange object sgid mode rasx
desc="object notsgid"
if su -fm $uidinrange -c "$command1"; then
	pass $desc
else
	fail $desc
fi

chmod g+s $file1
desc="object sgid"
if su -fm $uidinrange -c "$command1"; then
	fail $desc
else
	pass $desc
fi
chmod g-s $file1

#
# Object uid matches subject
#
ugidfw set 1 subject uid $uidrange object uid_of_subject mode rasx

desc="object uid notmatches subject"
if su -fm $uidinrange -c "$command2"; then
	pass $desc
else
	fail $desc
fi

desc="object uid matches subject"
if su -fm $uidinrange -c "$command1"; then
	fail $desc
else
	pass $desc
fi

#
# Object gid matches subject
#
ugidfw set 1 subject uid $uidrange object gid_of_subject mode rasx

desc="object gid notmatches subject"
if su -fm $uidinrange -c "$command2"; then
	pass $desc
else
	fail $desc
fi

desc="object gid matches subject"
if su -fm $uidinrange -c "$command1"; then
	fail $desc
else
	pass $desc
fi

#
# Object type
#
desc="object not type"
ugidfw set 1 subject uid $uidrange object type dbclsp mode rasx
if su -fm $uidinrange -c "$command1"; then
	pass $desc
else
	fail $desc
fi

desc="object type"
ugidfw set 1 subject uid $uidrange object type r mode rasx
if su -fm $uidinrange -c "$command1"; then
	fail $desc
else
	pass $desc
fi
