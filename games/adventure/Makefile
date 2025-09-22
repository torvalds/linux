#	$OpenBSD: Makefile,v 1.5 2002/05/23 18:42:59 deraadt Exp $

PROG=	adventure
SRCS=	main.c init.c done.c save.c subr.c vocab.c wizard.c io.c data.c crc.c
MAN=	adventure.6
CLEANFILES+=setup data.c

data.c: glorkz setup
	./setup ${.CURDIR}/glorkz > data.c

setup: setup.c hdr.h
	${HOSTCC} -o setup ${.CURDIR}/setup.c

.include <bsd.prog.mk>
