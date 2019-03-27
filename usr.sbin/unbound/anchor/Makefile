# $FreeBSD$

# Vendor sources and generated files
LDNSDIR= ${SRCTOP}/contrib/ldns
UNBOUNDDIR= ${SRCTOP}/contrib/unbound
EXPATDIR= ${SRCTOP}/contrib/expat

.PATH: ${UNBOUNDDIR} ${UNBOUNDDIR}/smallapp ${UNBOUNDDIR}/doc

PROG=	local-unbound-anchor
SRCS=	unbound-anchor.c
CFLAGS+=	-I${UNBOUNDDIR} -I${LDNSDIR} -I${EXPATDIR}/lib
LIBADD=	unbound bsdxml ssl crypto pthread
MAN=	local-unbound-anchor.8

.include <bsd.prog.mk>
