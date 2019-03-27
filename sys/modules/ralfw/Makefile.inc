# $FreeBSD$

#
# Common rules for building firmware.  Note this gets auto-included
# by the subdir Makefile's as a consequence of included bsd.kmod.mk.
#
KMOD=	${IMG}fw
_FIRM=	${IMG}.fw

CLEANFILES+=	${_FIRM}

FIRMWS=	${_FIRM}:${KMOD}

${_FIRM}: ${SRCTOP}/sys/contrib/dev/ral/${_FIRM}.uu
	uudecode -p $? > ${.TARGET}
