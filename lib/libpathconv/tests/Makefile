# $FreeBSD$

TAP_TESTS_C+=	abs2rel
TAP_TESTS_C+=	rel2abs

WARNS?=		6
#LIBADD+=	pathconv
#LDADD+= -L .. -lpathconv
LDADD+= ../libpathconv.a

.include <bsd.test.mk>
