#!/bin/sh

PROG=$1

if [ -x $PROG ]; then
    if expr "x`$PROG version`" : "xOpenSSL" > /dev/null; then
	:
    else
	echo "$PROG is not OpenSSL executable"
	exit 1
    fi
else
    echo "$PROG is not executable"
    exit 1;
fi

if $PROG engine padlock | grep -v no-ACE; then

    HASH=`cat $PROG | $PROG dgst -hex`

    ACE_ALGS="	aes-128-ecb aes-192-ecb aes-256-ecb \
		aes-128-cbc aes-192-cbc aes-256-cbc \
		aes-128-cfb aes-192-cfb aes-256-cfb \
		aes-128-ofb aes-192-ofb aes-256-ofb"

    nerr=0

    for alg in $ACE_ALGS; do
	echo $alg
	TEST=`(	cat $PROG | \
		$PROG enc -e -k "$HASH" -$alg -bufsize 999 -engine padlock | \
		$PROG enc -d -k "$HASH" -$alg | \
		$PROG dgst -hex ) 2>/dev/null`
	if [ "$TEST" != "$HASH" ]; then
		echo "-$alg encrypt test failed"
		nerr=`expr $nerr + 1`
	fi
	TEST=`(	cat $PROG | \
		$PROG enc -e -k "$HASH" -$alg | \
		$PROG enc -d -k "$HASH" -$alg -bufsize 999 -engine padlock | \
		$PROG dgst -hex ) 2>/dev/null`
	if [ "$TEST" != "$HASH" ]; then
		echo "-$alg decrypt test failed"
		nerr=`expr $nerr + 1`
	fi
	TEST=`(	cat $PROG | \
		$PROG enc -e -k "$HASH" -$alg -engine padlock | \
		$PROG enc -d -k "$HASH" -$alg -engine padlock | \
		$PROG dgst -hex ) 2>/dev/null`
	if [ "$TEST" != "$HASH" ]; then
		echo "-$alg en/decrypt test failed"
		nerr=`expr $nerr + 1`
	fi
    done

    if [ $nerr -gt 0 ]; then
	echo "PadLock ACE test failed."
	exit 1;
    fi
else
    echo "PadLock ACE is not available"
fi

exit 0
