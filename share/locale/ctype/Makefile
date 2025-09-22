# $OpenBSD: Makefile,v 1.12 2016/05/26 21:23:46 millert Exp $
# $NetBSD: Makefile,v 1.20 2005/02/10 18:03:02 tnozaki Exp $

NOMAN=	# defined

# pull LOCALEDIR and other declarations
.include <bsd.own.mk>

all: UTF-8.out

UTF-8.out: en_US.UTF-8.src
	mklocale -o ${.TARGET} ${.CURDIR}/en_US.UTF-8.src

CLEANFILES+=	UTF-8.out

install:
	install -o ${LOCALEOWN} -g ${LOCALEGRP} -m ${DIRMODE} -d \
	    ${DESTDIR}${LOCALEDIR}/UTF-8
	install -o ${LOCALEOWN} -g ${LOCALEGRP} -m ${LOCALEMODE} \
	    UTF-8.out ${DESTDIR}${LOCALEDIR}/UTF-8/LC_CTYPE

.include <bsd.prog.mk>
