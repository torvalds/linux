#	$FreeBSD$

.include <src.opts.mk>

WARNS?=		2
NO_WFORMAT=
NO_WARRAY_BOUNDS=

CFLAGS+=	-I${SRCTOP}/contrib/ipfilter
CFLAGS+=	-I${SRCTOP}/contrib/ipfilter/tools
CFLAGS+=	-I${SRCTOP}/sys
CFLAGS+=	-I${SRCTOP}/sys/contrib/ipfilter
CFLAGS+=	-DSTATETOP -D__UIO_EXPOSE

.if ${MK_INET6_SUPPORT} != "no"
CFLAGS+=	-DUSE_INET6
.else
CFLAGS+=	-DNOINET6
.endif

.if ${.CURDIR:M*/libipf} == ""
LIBADD+=	ipf
.endif

CLEANFILES+=	y.tab.c y.tab.h

.PATH:	${SRCTOP}/contrib/ipfilter		\
	${SRCTOP}/contrib/ipfilter/lib		\
	${SRCTOP}/contrib/ipfilter/tools	\
	${SRCTOP}/contrib/ipfilter/man

.include "../Makefile.inc"
