#	$OpenBSD: Makefile,v 1.5 2013/10/14 16:58:05 deraadt Exp $

PROG=		crunchgen
MAN=		crunchgen.8
SRCS=		crunchgen.c crunched_skel.c crunchide.c elf_hide.c mangle.c
CFLAGS+=	-g -Wall
CLEANFILES+=	crunched_skel.c

crunched_skel.c: crunched_main.c
	sh ${.CURDIR}/mkskel.sh ${.CURDIR}/crunched_main.c > crunched_skel.c

.include <bsd.prog.mk>
