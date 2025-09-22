#	$OpenBSD: files.adb,v 1.2 2014/10/18 12:21:57 miod Exp $

file	dev/adb/adb_subr.c		adb

device	akbd: wskbddev
attach	akbd at adb
file	dev/adb/akbd.c			akbd needs-flag

device	ams: wsmousedev
attach	ams at adb
file	dev/adb/ams.c			ams
