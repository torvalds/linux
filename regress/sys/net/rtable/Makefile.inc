#	$OpenBSD: Makefile.inc,v 1.8 2021/04/13 08:21:12 claudio Exp $

TOPDIR=		${.CURDIR}/..

SRCS+=		art.c

CPPFLAGS+=	-I${TOPDIR} -Wall

.PATH:		${TOPDIR} ${TOPDIR}/../../../../sys/net
