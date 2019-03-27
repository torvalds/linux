#!/bin/sh
#
# $FreeBSD$

make arphold 2>&1 > /dev/null

./arphold 192.168.1.222
