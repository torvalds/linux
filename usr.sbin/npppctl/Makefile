#	$Id: Makefile,v 1.2 2014/03/22 04:30:31 yasuoka Exp $
WARNS=		2
PROG=		npppctl
SRCS=		npppctl.c parser.c
MAN=		npppctl.8
CPPFLAGS+=	-I${.CURDIR}/../npppd/npppd -I${.CURDIR}/../npppd/common
LDADD+=		-lutil
DPADD+=		${LIBUTIL}

.include <bsd.prog.mk>
