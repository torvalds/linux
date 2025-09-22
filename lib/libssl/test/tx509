#!/bin/sh

cmd='../util/shlib_wrap.sh ../apps/openssl x509'

if [ "$1"x != "x" ]; then
	t=$1
else
	t=testx509.pem
fi

echo testing X509 conversions
cp $t fff.p

echo "p -> d"
$cmd -in fff.p -inform p -outform d >f.d
if [ $? != 0 ]; then exit 1; fi
echo "p -> n"
$cmd -in fff.p -inform p -outform n >f.n
if [ $? != 0 ]; then exit 1; fi
echo "p -> p"
$cmd -in fff.p -inform p -outform p >f.p
if [ $? != 0 ]; then exit 1; fi

echo "d -> d"
$cmd -in f.d -inform d -outform d >ff.d1
if [ $? != 0 ]; then exit 1; fi
echo "n -> d"
$cmd -in f.n -inform n -outform d >ff.d2
if [ $? != 0 ]; then exit 1; fi
echo "p -> d"
$cmd -in f.p -inform p -outform d >ff.d3
if [ $? != 0 ]; then exit 1; fi

echo "d -> n"
$cmd -in f.d -inform d -outform n >ff.n1
if [ $? != 0 ]; then exit 1; fi
echo "n -> n"
$cmd -in f.n -inform n -outform n >ff.n2
if [ $? != 0 ]; then exit 1; fi
echo "p -> n"
$cmd -in f.p -inform p -outform n >ff.n3
if [ $? != 0 ]; then exit 1; fi

echo "d -> p"
$cmd -in f.d -inform d -outform p >ff.p1
if [ $? != 0 ]; then exit 1; fi
echo "n -> p"
$cmd -in f.n -inform n -outform p >ff.p2
if [ $? != 0 ]; then exit 1; fi
echo "p -> p"
$cmd -in f.p -inform p -outform p >ff.p3
if [ $? != 0 ]; then exit 1; fi

cmp fff.p f.p
if [ $? != 0 ]; then exit 1; fi
cmp fff.p ff.p1
if [ $? != 0 ]; then exit 1; fi
cmp fff.p ff.p2
if [ $? != 0 ]; then exit 1; fi
cmp fff.p ff.p3
if [ $? != 0 ]; then exit 1; fi

cmp f.n ff.n1
if [ $? != 0 ]; then exit 1; fi
cmp f.n ff.n2
if [ $? != 0 ]; then exit 1; fi
cmp f.n ff.n3
if [ $? != 0 ]; then exit 1; fi

cmp f.p ff.p1
if [ $? != 0 ]; then exit 1; fi
cmp f.p ff.p2
if [ $? != 0 ]; then exit 1; fi
cmp f.p ff.p3
if [ $? != 0 ]; then exit 1; fi

/bin/rm -f f.* ff.* fff.*
exit 0
