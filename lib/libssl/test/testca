#!/bin/sh

SH="/bin/sh"
if test "$OSTYPE" = msdosdjgpp; then
    PATH="../apps\;$PATH"
else
    PATH="../apps:$PATH"
fi
export SH PATH

SSLEAY_CONFIG="-config CAss.cnf"
export SSLEAY_CONFIG

OPENSSL="`pwd`/../util/opensslwrap.sh"
export OPENSSL

/bin/rm -fr demoCA
$SH ../apps/CA.sh -newca <<EOF
EOF

if [ $? != 0 ]; then
	exit 1;
fi

SSLEAY_CONFIG="-config Uss.cnf"
export SSLEAY_CONFIG
$SH ../apps/CA.sh -newreq
if [ $? != 0 ]; then
	exit 1;
fi


SSLEAY_CONFIG="-config ../apps/openssl.cnf"
export SSLEAY_CONFIG
$SH ../apps/CA.sh -sign  <<EOF
y
y
EOF
if [ $? != 0 ]; then
	exit 1;
fi


$SH ../apps/CA.sh -verify newcert.pem
if [ $? != 0 ]; then
	exit 1;
fi

/bin/rm -fr demoCA newcert.pem newreq.pem
#usage: CA -newcert|-newreq|-newca|-sign|-verify

