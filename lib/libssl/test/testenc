#!/bin/sh

testsrc=Makefile
test=./p
cmd="../util/shlib_wrap.sh ../apps/openssl"

cat $testsrc >$test;

echo cat
$cmd enc < $test > $test.cipher
$cmd enc < $test.cipher >$test.clear
cmp $test $test.clear
if [ $? != 0 ]
then
	exit 1
else
	/bin/rm $test.cipher $test.clear
fi
echo base64
$cmd enc -a -e < $test > $test.cipher
$cmd enc -a -d < $test.cipher >$test.clear
cmp $test $test.clear
if [ $? != 0 ]
then
	exit 1
else
	/bin/rm $test.cipher $test.clear
fi

for i in `$cmd list-cipher-commands`
do
	echo $i
	$cmd $i -bufsize 113 -e -k test < $test > $test.$i.cipher
	$cmd $i -bufsize 157 -d -k test < $test.$i.cipher >$test.$i.clear
	cmp $test $test.$i.clear
	if [ $? != 0 ]
	then
		exit 1
	else
		/bin/rm $test.$i.cipher $test.$i.clear
	fi

	echo $i base64
	$cmd $i -bufsize 113 -a -e -k test < $test > $test.$i.cipher
	$cmd $i -bufsize 157 -a -d -k test < $test.$i.cipher >$test.$i.clear
	cmp $test $test.$i.clear
	if [ $? != 0 ]
	then
		exit 1
	else
		/bin/rm $test.$i.cipher $test.$i.clear
	fi
done
rm -f $test
