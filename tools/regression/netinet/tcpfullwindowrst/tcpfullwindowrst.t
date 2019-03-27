#!/bin/sh
#
# $FreeBSD$

make tcpfullwindowrsttest 2>&1 > /dev/null

./tcpfullwindowrsttest
