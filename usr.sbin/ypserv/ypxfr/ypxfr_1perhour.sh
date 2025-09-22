#!/bin/sh
#	$OpenBSD: ypxfr_1perhour.sh,v 1.2 2002/07/19 02:38:40 deraadt Exp $
#
# ypxfr_1perhour.sh - YP maps to be updated every hour
#

/usr/sbin/ypxfr passwd.byname
/usr/sbin/ypxfr passwd.byuid
/usr/sbin/ypxfr master.passwd.byname
/usr/sbin/ypxfr master.passwd.byuid
/usr/sbin/ypxfr netid.byname
