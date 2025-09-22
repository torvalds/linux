#!/bin/sh
#	$OpenBSD: ypxfr_2perday.sh,v 1.1 1997/04/20 10:08:38 maja Exp $
#
# ypxfr_2perday.sh - YP maps to be updated twice a day
#

/usr/sbin/ypxfr hosts.byname
/usr/sbin/ypxfr hosts.byaddr
/usr/sbin/ypxfr ethers.byaddr
/usr/sbin/ypxfr ethers.byname
