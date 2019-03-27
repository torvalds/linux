# $FreeBSD$

PROG=	ktutil
MAN=	ktutil.8

SRCS=	add.c \
	change.c \
	copy.c \
	destroy.c \
	get.c \
	ktutil.c \
	ktutil-commands.c \
	ktutil-commands.h \
	list.c \
	purge.c \
	remove.c \
	rename.c

CFLAGS+=-I${KRB5DIR}/lib/roken -I${KRB5DIR}/lib/sl -I.
LIBADD=	kadm5clnt krb5 roken crypto edit sl vers

CLEANFILES=	ktutil-commands.h ktutil-commands.c

.include <bsd.prog.mk>

ktutil-commands.h: ${KRB5DIR}/admin/ktutil-commands.in
	${SLC} ${.ALLSRC:M*.in}

ktutil-commands.c: ktutil-commands.h

.PATH: ${KRB5DIR}/admin
