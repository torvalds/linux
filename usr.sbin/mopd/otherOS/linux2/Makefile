#  Copyright (c) 1999 Mats O Jansson, Stockholm, Sweden
#  All rights reserved.
#
# @(#) $OpenBSD: Makefile,v 1.2 2014/07/16 20:07:03 okan Exp $

INSTBIN=	install -c -s -o root -g root -m 755
INSTINIT=	install -c -o root -g root -m 755

install:	mopd.init
	echo "Installing files in linux2";
	$(INSTINIT) $< $(INSTALL)/etc/rc.d/init.d/mopd
