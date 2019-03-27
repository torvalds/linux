# $FreeBSD$

PROG=	kadmin
MAN=	kadmin.8

SRCS=	add_enctype.c \
	ank.c \
	check.c \
	cpw.c \
	del.c \
	del_enctype.c \
	dump.c \
	ext.c \
	get.c \
	init.c \
	kadmin.c \
	kadmin-commands.c \
	kadmin-commands.h \
	load.c \
	mod.c \
	pw_quality.c \
	random_password.c \
	rename.c \
	stash.c \
	util.c

CFLAGS+=-I${KRB5DIR}/lib/asn1 -I${KRB5DIR}/lib/krb5 -I${KRB5DIR}/lib/roken \
	-I${KRB5DIR}/lib/sl -I${SRCTOP}/contrib/com_err -I. ${LDAPCFLAGS}
LIBADD=	kadm5clnt kadm5srv hdb krb5 roken vers sl asn1 crypto edit
DPADD=	${LDAPDPADD}
LDADD=	${LDAPLDADD}
LDFLAGS=${LDAPLDFLAGS}

CLEANFILES=	kadmin-commands.h kadmin-commands.c

.include <bsd.prog.mk>

kadmin-commands.h: ${KRB5DIR}/kadmin/kadmin-commands.in
	${SLC} ${.ALLSRC:M*.in}

kadmin-commands.c: kadmin-commands.h

.PATH: ${KRB5DIR}/kadmin

