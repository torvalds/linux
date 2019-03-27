# $FreeBSD$

PROG=	kcc
MAN=	klist.1 kswitch.1
LINKS=	${BINDIR}/kcc ${BINDIR}/klist \
	${BINDIR}/kcc ${BINDIR}/kswitch
CFLAGS+=	-I${KRB5DIR}/lib/hx509 \
		-I${KRB5DIR}/lib/asn1 \
		-I${KRB5DIR}/lib/roken \
		-I${KRB5DIR}/lib/sl -I.
LIBADD=	krb5 roken asn1 kafs5 edit sl vers
SRCS=	kcc.c \
	kcc-commands.c \
	kcc-commands.h \
	klist.c \
	kswitch.c \
	copy_cred_cache.c

CLEANFILES=	kcc-commands.h kcc-commands.c

kcc-commands.h: kcc-commands.in
	${SLC} ${.ALLSRC:M*.in}

kcc-commands.c: kcc-commands.h

.include <bsd.prog.mk>

.PATH: ${KRB5DIR}/kuser
