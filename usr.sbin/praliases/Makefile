#	@(#)Makefile	8.2 (Berkeley) 9/21/96
# $FreeBSD$

SENDMAIL_DIR=	${SRCTOP}/contrib/sendmail
.PATH: ${SENDMAIL_DIR}/praliases

PROG=	praliases
SRCS=	praliases.c
MAN=	praliases.8

CFLAGS+= -I${SENDMAIL_DIR}/src -I${SENDMAIL_DIR}/include -I.
CFLAGS+= -DNEWDB -DNOT_SENDMAIL

WARNS?=	2

LIBADD=	sm smdb smutil

SRCS+=	sm_os.h
CLEANFILES+=sm_os.h

# User customizations to the sendmail build environment
CFLAGS+= ${SENDMAIL_CFLAGS}
LDFLAGS+=${SENDMAIL_LDFLAGS}
DPADD+=	${SENDMAIL_DPADD}
LDADD+=	${SENDMAIL_LDADD}

sm_os.h: ${SENDMAIL_DIR}/include/sm/os/sm_os_freebsd.h .NOMETA
	ln -sf ${.ALLSRC} ${.TARGET}

.include <bsd.prog.mk>
