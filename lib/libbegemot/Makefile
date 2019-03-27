# $FreeBSD$

LIBBEGEMOT_DIR=${SRCTOP}/contrib/libbegemot

PACKAGE=lib${LIB}
.PATH: ${LIBBEGEMOT_DIR}

LIB=	begemot
SHLIB_MAJOR= 4
SHLIBDIR?= /lib

CFLAGS+=	-DUSE_SELECT -DQUADFMT='"ll"'
SRCS=	rpoll.c
INCS=	rpoll.h
MAN=	rpoll.3

CLEANFILES= rpoll.3
rpoll.3: rpoll.man
	cat ${.ALLSRC} > ${.TARGET}

MLINKS=	rpoll.3 poll_register.3		\
	rpoll.3 poll_unregister.3	\
	rpoll.3 poll_start_timer.3	\
	rpoll.3 poll_stop_timer.3	\
	rpoll.3 poll_dispatch.3

.include <bsd.lib.mk>
