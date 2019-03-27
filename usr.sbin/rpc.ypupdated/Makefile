# $FreeBSD$

.PATH: ${SRCTOP}/usr.sbin/ypserv ${SRCTOP}/libexec/ypxfr

PROG=	rpc.ypupdated
MAN=
SRCS=	ypupdate_prot_svc.c ypupdate_prot.h ypupdated_main.c \
	yp_error.c update.c ypupdated_server.c \
	yp_dblookup.c yp_dbwrite.c yp_dbdelete.c yp_dbupdate.c

#CFLAGS+= -DYP
CFLAGS+= -I${SRCTOP}/usr.sbin/ypserv -I. -I${SRCTOP}/libexec/ypxfr

WARNS?=	1

LIBADD=	rpcsvc

CLEANFILES= ypupdate_prot_svc.c ypupdate_prot.h

RPCDIR=	${SYSROOT:U${DESTDIR}}/usr/include/rpcsvc
RPCGEN= RPCGEN_CPP=${CPP:Q} rpcgen -I -C

# We need to remove the 'static' keyword from _rpcsvcstate so that
# ypupdated_main.c can see it.
ypupdate_prot_svc.c: ${RPCDIR}/ypupdate_prot.x
	rm -f ${.TARGET}
	${RPCGEN} -m ${.ALLSRC} | \
	sed s/"static int _rpcsvcstate"/"int _rpcsvcstate"/g > ${.TARGET}

ypupdate_prot.h: ${RPCDIR}/ypupdate_prot.x
	${RPCGEN} -h -o ${.TARGET} ${.ALLSRC}

.include <bsd.prog.mk>
