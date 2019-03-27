# $FreeBSD$

PACKAGE=lib${LIB}
.PATH:	${SRCTOP}/contrib/pf/libevent

.include <src.opts.mk>

LIB=		event
SHLIB_MAJOR=	1
PRIVATELIB=

SRCS=	buffer.c evbuffer.c event.c kqueue.c log.c poll.c select.c signal.c
INCS=	event.h

CFLAGS+= -I${.CURDIR} \
	-DHAVE_CLOCK_GETTIME \
	-DHAVE_FCNTL_H \
	-DHAVE_POLL \
	-DHAVE_SELECT \
	-DHAVE_SETFD \
	-DHAVE_STDARG_H \
	-DHAVE_SYS_IOCTL_H \
	-DHAVE_SYS_TIME_H \
	-DHAVE_UNISTD_H \
	-DHAVE_VASPRINTF \
	-DHAVE_WORKING_KQUEUE \
	-DVERSION='"1.3b"'

WARNS?=	2

.include <bsd.lib.mk>
