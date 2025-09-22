# $OpenBSD: Makefile,v 1.7 2021/09/02 07:14:15 jasper Exp $

CSH=	/bin/csh

REGRESS_TARGETS=	filec

PROG=		edit
CFLAGS+=	-Wall
LDADD+=		-lutil
CLEANFILES+=	edit.c

edit.c: ${.CURDIR}/../ksh/edit/edit.c
	@cp ${.CURDIR}/../ksh/edit/edit.c .

filec: edit
	sh ${.CURDIR}/filec.sh ${CSH}

REGRESS_TARGETS+=	env

.SUFFIXES: .in

.in:
	${CSH} <${.CURDIR}/${@}.in 2>&1 | diff -u ${.CURDIR}/${@}.ok -

.include <bsd.regress.mk>
