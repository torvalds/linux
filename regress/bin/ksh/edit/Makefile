# $OpenBSD: Makefile,v 1.5 2021/07/10 07:10:31 anton Exp $

KSH=	/bin/ksh

PROG=		edit
CFLAGS+=	-Wall
LDADD+=		-lutil

REGRESS_SETUP_ONCE=	${PROG}

REGRESS_TARGETS+=	emacs
REGRESS_TARGETS+=	vi

emacs:
	sh ${.CURDIR}/emacs.sh ${.CURDIR} ${KSH}

vi:
	sh ${.CURDIR}/vi.sh ${.CURDIR} ${KSH}

.include <bsd.regress.mk>
