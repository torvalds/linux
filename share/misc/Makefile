#	$OpenBSD: Makefile,v 1.19 2019/12/21 21:40:00 espie Exp $
#	from: @(#)Makefile	5.13 (Berkeley) 5/7/91

FILES=	airport birthtoken countrycodes \
	inter.phone license.template mdoc.template mime.types \
	na.phone scsi_modes usb_hid_usages usb_hid_usages \
	zipcodes 

.if (${MACHINE} == "i386") || (${MACHINE} == "amd64") || \
    (${MACHINE} == "alpha")
SUBDIR=	pcvtfonts
.endif

all clean cleandir obj tags: _SUBDIRUSE

realinstall:
	cd ${.CURDIR}; install -c -o ${BINOWN} -g ${BINGRP} -m 444 ${FILES} \
	    ${DESTDIR}${BINDIR}/misc

.include <bsd.prog.mk>
