#	$OpenBSD: Makefile,v 1.16 2021/05/26 19:14:32 bluhm Exp $

.PATH: ${.CURDIR}/lib

.include <bsd.own.mk>		# to check ${COMPILER_VERSION}

LIB=	expat
SRCS=	xmlparse.c xmltok.c xmlrole.c
CFLAGS+=-I${.CURDIR}
.if ${COMPILER_VERSION:L} != "gcc3"
CFLAGS+=-fvisibility=hidden -DXML_ENABLE_VISIBILITY=1
.endif

PC_FILES=expat.pc
CLEANFILES+=${PC_FILES}

includes:
	cmp -s ${DESTDIR}/usr/include/expat.h ${.CURDIR}/lib/expat.h || \
	  ${INSTALL} ${INSTALL_COPY} -m 444 -o $(BINOWN) -g $(BINGRP) \
	  ${.CURDIR}/lib/expat.h ${DESTDIR}/usr/include/expat.h
	cmp -s ${DESTDIR}/usr/include/expat_external.h ${.CURDIR}/lib/expat_external.h || \
	  ${INSTALL} ${INSTALL_COPY} -m 444 -o $(BINOWN) -g $(BINGRP) \
	  ${.CURDIR}/lib/expat_external.h ${DESTDIR}/usr/include/expat_external.h

all: ${PC_FILES}
${PC_FILES}: lib/expat.h
	/bin/sh ${.CURDIR}/generate_pkgconfig.sh -c ${.CURDIR} -o ${.OBJDIR}

beforeinstall:
	${INSTALL} ${INSTALL_COPY} -o root -g ${SHAREGRP} \
	    -m ${SHAREMODE} ${.OBJDIR}/${PC_FILES} ${DESTDIR}/usr/lib/pkgconfig/

.include <bsd.lib.mk>
