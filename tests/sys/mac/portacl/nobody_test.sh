#!/bin/sh
# $FreeBSD$

dir=`dirname $0`
. ${dir}/misc.sh

echo "1..64"

# security.mac.portacl.suser_exempt value doesn't affect unprivileged users
# behaviour.
# mac_portacl has no impact on ports <= net.inet.ip.portrange.reservedhigh.

trap restore_settings EXIT INT TERM

sysctl security.mac.portacl.suser_exempt=1 >/dev/null
sysctl net.inet.ip.portrange.reservedhigh=78 >/dev/null

bind_test fl fl uid nobody tcp 77
bind_test ok ok uid nobody tcp 7777
bind_test fl fl uid nobody udp 77
bind_test ok ok uid nobody udp 7777

bind_test fl fl gid nobody tcp 77
bind_test ok ok gid nobody tcp 7777
bind_test fl fl gid nobody udp 77
bind_test ok ok gid nobody udp 7777

sysctl security.mac.portacl.suser_exempt=0 >/dev/null

bind_test fl fl uid nobody tcp 77
bind_test ok ok uid nobody tcp 7777
bind_test fl fl uid nobody udp 77
bind_test ok ok uid nobody udp 7777

bind_test fl fl gid nobody tcp 77
bind_test ok ok gid nobody tcp 7777
bind_test fl fl gid nobody udp 77
bind_test ok ok gid nobody udp 7777

# Verify if security.mac.portacl.port_high works.

sysctl security.mac.portacl.port_high=7778 >/dev/null

bind_test fl fl uid nobody tcp 77
bind_test fl ok uid nobody tcp 7777
bind_test fl fl uid nobody udp 77
bind_test fl ok uid nobody udp 7777

bind_test fl fl gid nobody tcp 77
bind_test fl ok gid nobody tcp 7777
bind_test fl fl gid nobody udp 77
bind_test fl ok gid nobody udp 7777

# Verify if mac_portacl rules work.

sysctl net.inet.ip.portrange.reservedhigh=76 >/dev/null
sysctl security.mac.portacl.port_high=7776 >/dev/null

bind_test fl ok uid nobody tcp 77
bind_test ok ok uid nobody tcp 7777
bind_test fl ok uid nobody udp 77
bind_test ok ok uid nobody udp 7777

bind_test fl ok gid nobody tcp 77
bind_test ok ok gid nobody tcp 7777
bind_test fl ok gid nobody udp 77
bind_test ok ok gid nobody udp 7777
