#	$OpenBSD: Makefile,v 1.17 2020/09/19 09:46:04 tb Exp $

PROG=		ldapd
MAN=		ldapd.8 ldapd.conf.5
SRCS=		log.c logmsg.c control.c \
		util.c ldapd.c ldape.c conn.c attributes.c namespace.c \
		btree.c filter.c search.c parse.y \
		auth.c modify.c index.c evbuffer_tls.c \
		validate.c uuid.c schema.c imsgev.c syntax.c matching.c

LDADD=		-levent -ltls -lssl -lcrypto -lz -lutil
DPADD=		${LIBEVENT} ${LIBTLS} ${LIBSSL} ${LIBCRYPTO} ${LIBZ} ${LIBUTIL}
CFLAGS+=	-I${.CURDIR} -g
CFLAGS+=	-Wall -Wstrict-prototypes -Wmissing-prototypes
CFLAGS+=	-Wmissing-declarations
CFLAGS+=	-Wshadow -Wpointer-arith -Wcast-qual
CFLAGS+=	-Wsign-compare
CLEANFILES+=	y.tab.h parse.c

SCHEMA_FILES=	bsd.schema \
		core.schema \
		inetorgperson.schema \
		nis.schema

distribution:
	for i in ${SCHEMA_FILES}; do \
		${INSTALL} -C -o root -g wheel -m 0644 ${.CURDIR}/schema/$$i ${DESTDIR}/etc/ldap/; \
	done

.include <bsd.prog.mk>
