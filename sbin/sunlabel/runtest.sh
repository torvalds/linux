#!/bin/sh
# $FreeBSD$

TMP=/tmp/$$.
set -e
MD=`mdconfig -a -t malloc -s 2m`
trap "exec 7</dev/null; rm -f ${TMP}* ; mdconfig -d -u ${MD}" EXIT INT TERM

./sunlabel -r -w $MD auto

dd if=/dev/$MD of=${TMP}i0 count=16 > /dev/null 2>&1
./sunlabel $MD > ${TMP}l0

sed '
/  c:/{
p
s/c:/a:/
s/3969/1024/
}
' ${TMP}l0 > ${TMP}l1

./sunlabel -R $MD ${TMP}l1
if [ -c /dev/${MD}a ] ; then
	echo "PASS: Created a: partition" 1>&2
else
	echo "FAIL: Did not create a: partition" 1>&2
	exit 2
fi

# Spoil and rediscover

true > /dev/${MD}
if [ -c /dev/${MD}a ] ; then
	echo "PASS: Recreated a: partition after spoilage" 1>&2
else
	echo "FAIL: Did not recreate a: partition after spoilage" 1>&2
	exit 2
fi

dd if=/dev/$MD of=${TMP}i1 count=16 > /dev/null 2>&1
sed '
/  c:/{
p
s/c:/a:/
s/3969/2048/
}
' ${TMP}l0 > ${TMP}l2

./sunlabel -R $MD ${TMP}l2
dd if=/dev/$MD of=${TMP}i2 count=16 > /dev/null 2>&1

exec 7< /dev/${MD}a

for t in a c
do
	if dd if=${TMP}i2 of=/dev/${MD}$t 2>/dev/null ; then
		echo "PASS: Could rewrite same label to ...$t while ...a open" 1>&2
	else
		echo "FAIL: Could not rewrite same label to ...$t while ...a open" 1>&2
		exit 2
	fi

	if dd if=${TMP}i1 of=/dev/${MD}$t 2>/dev/null ; then
		echo "FAIL: Could label with smaller ...a to ...$t while ...a open" 1>&2
		exit 2
	else
		echo "PASS: Could not label with smaller ...a to ...$t while ...a open" 1>&2
	fi

	if dd if=${TMP}i0 of=/dev/${MD}$t 2>/dev/null ; then
		echo "FAIL: Could write label missing ...a to ...$t while ...a open" 1>&2
		exit 2
	else
		echo "PASS: Could not write label missing ...a to ...$t while ...a open" 1>&2
	fi
done

exec 7< /dev/null

if dd if=${TMP}i0 of=/dev/${MD}c 2>/dev/null ; then
	echo "PASS: Could write missing ...a label to ...c" 1>&2
else
	echo "FAIL: Could not write missing ...a label to ...c" 1>&2
	exit 2
fi

if dd if=${TMP}i2 of=/dev/${MD}c 2>/dev/null ; then
	echo "PASS: Could write large ...a label to ...c" 1>&2
else
	echo "FAIL: Could not write large ...a label to ...c" 1>&2
	exit 2
fi

if dd if=${TMP}i1 of=/dev/${MD}c 2>/dev/null ; then
	echo "PASS: Could write small ...a label to ...c" 1>&2
else
	echo "FAIL: Could not write small ...a label to ...c" 1>&2
	exit 2
fi

if dd if=${TMP}i2 of=/dev/${MD}a 2>/dev/null ; then
	echo "PASS: Could increase size of ...a by writing to ...a" 1>&2
else
	echo "FAIL: Could not increase size of ...a by writing to ...a" 1>&2
	exit 2
fi

if dd if=${TMP}i1 of=/dev/${MD}a 2>/dev/null ; then
	echo "FAIL: Could decrease size of ...a by writing to ...a" 1>&2
	exit 2
else
	echo "PASS: Could not decrease size of ...a by writing to ...a" 1>&2
fi

if dd if=${TMP}i0 of=/dev/${MD}a 2>/dev/null ; then
	echo "FAIL: Could delete ...a by writing to ...a" 1>&2
	exit 2
else
	echo "PASS: Could not delete ...a by writing to ...a" 1>&2
fi

if ./sunlabel -B -b ${TMP}i0 ${MD} ; then
	if [ ! -c /dev/${MD}a ] ; then
		echo "FAILED: Writing bootcode killed ...a" 1>&2
		exit 2
	else
		echo "PASS: Could write bootcode while closed" 1>&2
	fi
else
	echo "FAILED: Could not write bootcode while closed" 1>&2
	exit 2
fi

exec 7> /dev/${MD}c
if ktrace ./sunlabel -B -b ${TMP}i0 ${MD} ; then
	if [ ! -c /dev/${MD}a ] ; then
		echo "FAILED: Writing bootcode killed ...a" 1>&2
		exit 2
	else
		echo "PASS: Could write bootcode while open" 1>&2
	fi
else
	echo "FAILED: Could not write bootcode while open" 1>&2
	exit 2
fi
exec 7> /dev/null

if dd if=${TMP}i0 of=/dev/${MD}c 2>/dev/null ; then
	echo "PASS: Could delete ...a by writing to ...c" 1>&2
else
	echo "FAIL: Could not delete ...a by writing to ...c" 1>&2
	exit 2
fi

# XXX: need to add a 'b' partition and check for overlaps.

exit 0
