#!/bin/sh

	if [ $# != 1 ]
	then
		echo "usage: natd.test ifname"
		exit 1
	fi

	ipfw flush
	ipfw add divert 32000 ip from any to any via $1 
	ipfw add pass ip from any to any

	./natd -port 32000 -interface $1 -verbose

