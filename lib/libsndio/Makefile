#	$OpenBSD: Makefile,v 1.15 2021/04/28 19:12:53 ratchov Exp $

LIB=	sndio
MAN=	sio_open.3 mio_open.3 sioctl_open.3 sndio.7
SRCS=	debug.c aucat.c sio_aucat.c sio_sun.c sio.c \
	mio_rmidi.c mio_aucat.c mio.c \
	sioctl_aucat.c sioctl_sun.c sioctl.c
CFLAGS+=-DDEBUG
COPTS+=	-Wall -Wstrict-prototypes -Wmissing-prototypes -Wpointer-arith -Wundef

VERSION_SCRIPT=	${.CURDIR}/Symbols.map

PC_FILES=sndio.pc

beforeinstall:
	${INSTALL} ${INSTALL_COPY} -o root -g ${SHAREGRP} \
	    -m ${SHAREMODE} ${.CURDIR}/${PC_FILES} ${DESTDIR}/usr/lib/pkgconfig/

.include <bsd.lib.mk>
