# $OpenBSD: Makefile,v 1.6 2014/07/16 20:04:21 okan Exp $

PROG=	socket2a
CFLAGS+= -I${.CURDIR}/../../include

REGRESS_TARGETS=dummy

# build prog and link into test 2 directory.  Code is used by test 2
#
dummy: ${PROG}
	@cd ${.CURDIR}/../2; \
	if test -d ${__objdir} ; then \
		cd ${__objdir} ; \
	fi; \
	ln -sf ${.OBJDIR}/${PROG}
	@echo ${PROG} ready

.include <bsd.regress.mk>
