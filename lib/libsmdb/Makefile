# $FreeBSD$

PACKAGE=lib${LIB}
SENDMAIL_DIR=${SRCTOP}/contrib/sendmail
.PATH:	${SENDMAIL_DIR}/libsmdb

CFLAGS+=-I${SENDMAIL_DIR}/src -I${SENDMAIL_DIR}/include -I.
CFLAGS+=-DNEWDB -DNOT_SENDMAIL

# User customizations to the sendmail build environment
CFLAGS+=${SENDMAIL_CFLAGS}

WARNS?=	3

LIB=	smdb

SRCS+=	sm_os.h
SRCS+=	smdb.c smdb1.c smdb2.c smndbm.c
CLEANFILES+=sm_os.h

INTERNALLIB=

sm_os.h: ${SENDMAIL_DIR}/include/sm/os/sm_os_freebsd.h .NOMETA
	ln -sf ${.ALLSRC} ${.TARGET}

.include <bsd.lib.mk>
