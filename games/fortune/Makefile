#	$OpenBSD: Makefile,v 1.2 1997/09/21 11:35:57 deraadt Exp $

SUBDIR=	fortune

.ifmake !(install)
SUBDIR+= strfile
.endif

SUBDIR+= datfiles

.include <bsd.subdir.mk>
