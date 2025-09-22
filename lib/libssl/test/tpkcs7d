#!/bin/sh

cmd='../util/shlib_wrap.sh ../apps/openssl pkcs7'

if [ "$1"x != "x" ]; then
	t=$1
else
	t=pkcs7-1.pem
fi

echo "testing pkcs7 conversions (2)"
cp $t fff.p

echo "p -> d"
$cmd -in fff.p -inform p -outform d >f.d
if [ $? != 0 ]; then exit 1; fi
echo "p -> p"
$cmd -in fff.p -inform p -outform p >f.p
if [ $? != 0 ]; then exit 1; fi

echo "d -> d"
$cmd -in f.d -inform d -outform d >ff.d1
if [ $? != 0 ]; then exit 1; fi
echo "p -> d"
$cmd -in f.p -inform p -outform d >ff.d3
if [ $? != 0 ]; then exit 1; fi

echo "d -> p"
$cmd -in f.d -inform d -outform p >ff.p1
if [ $? != 0 ]; then exit 1; fi
echo "p -> p"
$cmd -in f.p -inform p -outform p >ff.p3
if [ $? != 0 ]; then exit 1; fi

cmp f.p ff.p1
if [ $? != 0 ]; then exit 1; fi
cmp f.p ff.p3
if [ $? != 0 ]; then exit 1; fi

/bin/rm -f f.* ff.* fff.*
exit 0
