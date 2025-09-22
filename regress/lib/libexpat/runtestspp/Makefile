# $OpenBSD: Makefile,v 1.5 2024/02/11 01:01:32 bluhm Exp $

SRCS=		runtestspp.cpp
SRCS+=		acc_tests.c alloc_tests.c
SRCS+=		basic_tests.c
SRCS+=		chardata.c common.c
SRCS+=		dummy.c
SRCS+=		handlers.c
SRCS+=		memcheck.c minicheck.c misc_tests.c
SRCS+=		ns_tests.c nsalloc_tests.c
SRCS+=		structdata.c 

# _INTERNAL_trim_to_complete_utf8_characters is not exported in dynamic library
LDFLAGS=	-static

.include <bsd.regress.mk>
