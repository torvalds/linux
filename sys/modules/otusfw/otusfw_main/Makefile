# $FreeBSD$

KMOD=	otusfw_main
FIRMWS=	otusfw_main:otusfw_main:1

CLEANFILES=	otusfw_main

otusfw_main: ${SRCTOP}/sys/contrib/dev/otus/otus-main
	cp ${.ALLSRC} ${.TARGET}

.include <bsd.kmod.mk>
