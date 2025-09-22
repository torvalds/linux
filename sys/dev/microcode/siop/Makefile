# $OpenBSD: Makefile,v 1.4 2007/04/05 23:41:20 drahn Exp $
# $NetBSD: Makefile,v 1.1 2000/04/21 17:57:01 bouyer Exp $

all: siop.out osiop.out oosiop.out

PROG=	ncr53cxxx
MKSHARE=no
MAN=
CC=${HOSTCC}

.include <bsd.prog.mk>

regen: siop.out osiop.out oosiop.out
headers: siop.out osiop.out oosiop.out

siop.out: siop.ss ${PROG}
	./${PROG} siop.ss -p siop.out

osiop.out: osiop.ss ${PROG}
	./${PROG} osiop.ss -p osiop.out

oosiop.out: oosiop.ss ${PROG}
	./${PROG} oosiop.ss -p oosiop.out
