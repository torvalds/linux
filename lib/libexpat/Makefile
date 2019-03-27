# $FreeBSD$

PACKAGE=lib${LIB}
EXPAT=		${SRCTOP}/contrib/expat

LIB=		bsdxml
SHLIBDIR?=	/lib
SHLIB_MAJOR=	4
SRCS=		xmlparse.c xmlrole.c xmltok.c
INCS=		bsdxml.h bsdxml_external.h
MAN=		libbsdxml.3

.PATH:		${EXPAT}/lib

CFLAGS+= 	-I${.CURDIR} -DHAVE_EXPAT_CONFIG_H
CLEANFILES=	bsdxml.h bsdxml_external.h

WARNS?=		2

# OK, so it is not entirely unadulterated: we amend the COPYING to
# point people to the right place and use FreeBSD-style include guards.
# We also want to point it at the new bsdxml_external.h rather than the
# old expat_external.h file.
bsdxml.h: expat.h
	sed -e 's/XmlParse_INCLUDED/_BSD_XML_H_/' \
	    -e 's/COPYING/src\/contrib\/expat\/COPYING/' \
	    -e 's/expat_external/bsdxml_external/' ${.ALLSRC} \
		> ${.TARGET}

bsdxml_external.h: expat_external.h
	${CP} ${.ALLSRC} ${.TARGET}

.include <bsd.lib.mk>
