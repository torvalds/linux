#	$OpenBSD: Makefile,v 1.1 2018/03/19 14:34:33 beck Exp $
PROG=	init_pledge
SRCS+=	init_pledge.c
NOMAN=	yes

LDADD+=		-lcrypto -lutil
CFLAGS+=	-Wall -Werror

REGRESS_TARGETS+=	test_normal

test_normal: ${PROG}
	./${PROG} 

.include <bsd.regress.mk>
