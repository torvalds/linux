# $FreeBSD$

.PATH: ${SRCTOP}/contrib/ntp/util

PROG=	ntptime
MAN=

CFLAGS+= -I${SRCTOP}/contrib/ntp/include \
	-I${SRCTOP}/contrib/ntp/lib/isc/include/ \
	-I${SRCTOP}/contrib/ntp/lib/isc/unix/include/ \
	-I${SRCTOP}/contrib/ntp/lib/isc/pthreads/include \
	-I${.CURDIR:H}

LIBADD=	m ntp pthread

.include <bsd.prog.mk>
