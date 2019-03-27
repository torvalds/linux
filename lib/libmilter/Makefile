# $FreeBSD$

.include <src.opts.mk>

PACKAGE=sendmail
SENDMAIL_DIR=${SRCTOP}/contrib/sendmail
.PATH:	${SENDMAIL_DIR}/libmilter ${SENDMAIL_DIR}/libsm

CFLAGS+=-I${SENDMAIL_DIR}/src -I${SENDMAIL_DIR}/include -I.
CFLAGS+=-DNOT_SENDMAIL -Dsm_snprintf=snprintf
CFLAGS+=-D_THREAD_SAFE
CFLAGS+=-DSM_CONF_POLL

.if ${MK_INET6_SUPPORT} != "no"
CFLAGS+=-DNETINET6
.endif

# User customizations to the sendmail build environment
CFLAGS+=${SENDMAIL_CFLAGS}

INCSDIR=${INCLUDEDIR}/libmilter
INCS=	${SENDMAIL_DIR}/include/libmilter/mfapi.h \
	${SENDMAIL_DIR}/include/libmilter/mfdef.h
LIB=	milter

SRCS+=	sm_os.h
SRCS+=	main.c engine.c listener.c handler.c comm.c monitor.c smfi.c \
	signal.c sm_gethost.c errstring.c strl.c worker.c
CLEANFILES+=sm_os.h

WARNS?=	0
SHLIB_MAJOR= 6

sm_os.h: ${SENDMAIL_DIR}/include/sm/os/sm_os_freebsd.h .NOMETA
	ln -sf ${.ALLSRC} ${.TARGET}

.include <bsd.lib.mk>
