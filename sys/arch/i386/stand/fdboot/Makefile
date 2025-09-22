#	$OpenBSD: Makefile,v 1.3 2016/03/30 06:38:45 jmc Exp $

MAN=

.if ${MACHINE} == "i386"
PROG=	fdboot
COPTS=	-DFDBOOT
.endif

.PATH:	${.CURDIR}/../boot

.include "${.CURDIR}/../boot/Makefile"
