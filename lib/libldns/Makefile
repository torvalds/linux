# $FreeBSD$

# Vendor sources and generated files
LDNSDIR = ${SRCTOP}/contrib/ldns

PACKAGE=lib${LIB}
.PATH: ${LDNSDIR} ${LDNSDIR}/compat

LIB=	ldns
PRIVATELIB= true

CFLAGS+= -I${LDNSDIR}

SRCS=	buffer.c dane.c dname.c dnssec.c dnssec_sign.c dnssec_verify.c \
	dnssec_zone.c duration.c error.c higher.c host2str.c host2wire.c \
	keys.c net.c packet.c parse.c radix.c rbtree.c rdata.c resolver.c \
	rr.c rr_functions.c sha1.c sha2.c str2host.c tsig.c update.c util.c \
	wire2host.c zone.c

SRCS+=	b64_ntop.c b64_pton.c

LIBADD=	ssl crypto

WARNS ?= 3

.include <bsd.lib.mk>
