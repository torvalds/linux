#!/bin/sh
# $FreeBSD$

dir=`dirname $0`
. ${dir}/misc.sh

echo "1..48"

# Verify if security.mac.portacl.suser_exempt=1 really exempts super-user.

trap restore_settings EXIT INT TERM

sysctl security.mac.portacl.suser_exempt=1 >/dev/null

bind_test ok ok uid root tcp 77
bind_test ok ok uid root tcp 7777
bind_test ok ok uid root udp 77
bind_test ok ok uid root udp 7777

bind_test ok ok gid root tcp 77
bind_test ok ok gid root tcp 7777
bind_test ok ok gid root udp 77
bind_test ok ok gid root udp 7777

# Verify if security.mac.portacl.suser_exempt=0 really doesn't exempt super-user.

sysctl security.mac.portacl.suser_exempt=0 >/dev/null

bind_test fl ok uid root tcp 77
bind_test ok ok uid root tcp 7777
bind_test fl ok uid root udp 77
bind_test ok ok uid root udp 7777

bind_test fl ok gid root tcp 77
bind_test ok ok gid root tcp 7777
bind_test fl ok gid root udp 77
bind_test ok ok gid root udp 7777

# Verify if security.mac.portacl.port_high works for super-user.

sysctl security.mac.portacl.port_high=7778 >/dev/null

bind_test fl ok uid root tcp 77
bind_test fl ok uid root tcp 7777
bind_test fl ok uid root udp 77
bind_test fl ok uid root udp 7777

bind_test fl ok gid root tcp 77
bind_test fl ok gid root tcp 7777
bind_test fl ok gid root udp 77
bind_test fl ok gid root udp 7777
