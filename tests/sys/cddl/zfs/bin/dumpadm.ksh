#! /usr/local/bin/ksh93 -p

# $FreeBSD$

if [ $# != 0 ]
then
	echo "ERROR option not supported"
	return 1
fi
grep dumpdev /etc/rc.conf
