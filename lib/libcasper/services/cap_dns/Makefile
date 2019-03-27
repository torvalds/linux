# $FreeBSD$

SHLIBDIR?=	/lib/casper

.include <src.opts.mk>

PACKAGE=libcasper

SHLIB_MAJOR=	2
INCSDIR?=	${INCLUDEDIR}/casper

.if ${MK_CASPER} != "no"
SHLIB=	cap_dns

SRCS=	cap_dns.c
.endif

INCS=	cap_dns.h

LIBADD=	nv

CFLAGS+=-I${.CURDIR}

HAS_TESTS=
SUBDIR.${MK_TESTS}+= tests

MAN+=	cap_dns.3

MLINKS+=cap_dns.3 libcap_dns.3
MLINKS+=cap_dns.3 cap_gethostbyname.3
MLINKS+=cap_dns.3 cap_gethostbyname2.3
MLINKS+=cap_dns.3 cap_gethostbyaddr.3
MLINKS+=cap_dns.3 cap_getaddrinfo.3
MLINKS+=cap_dns.3 cap_getnameinfo.3
MLINKS+=cap_dns.3 cap_dns_type_limit.3
MLINKS+=cap_dns.3 cap_dns_family_limit.3

.include <bsd.lib.mk>
