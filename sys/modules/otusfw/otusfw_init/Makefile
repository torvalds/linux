# $FreeBSD$

KMOD=	otusfw_init
FIRMWS=	otusfw_init:otusfw_init:1

CLEANFILES=	otus_init

otusfw_init: ${SRCTOP}/sys/contrib/dev/otus/otus-init
	cp ${.ALLSRC} ${.TARGET}

.include <bsd.kmod.mk>
