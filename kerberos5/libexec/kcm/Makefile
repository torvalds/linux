# $FreeBSD$

PROG=	kcm
MAN=	kcm.8

SRCS=	acl.c \
	acquire.c \
	cache.c \
	client.c \
	config.c \
	connect.c \
	events.c \
	glue.c \
	log.c \
	main.c \
	protocol.c \
	sessions.c \
	renew.c

CFLAGS+=-I${KRB5DIR}/lib/krb5 -I${KRB5DIR}/lib/asn1 -I${KRB5DIR}/lib/roken \
	-I${KRB5DIR}/kcm -I${KRB5DIR}/lib/ipc ${LDAPCFLAGS}
LIBADD=	krb5 roken heimntlm heimipcs crypto
DPADD=	${LDAPDPADD}
LDADD=	${LIBVERS} ${LDAPLDADD}
LDFLAGS=${LDAPLDFLAGS}

.include <bsd.prog.mk>

.PATH: ${KRB5DIR}/kcm
