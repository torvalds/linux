# $FreeBSD$

RPCDIR=	${DESTDIR}/usr/include/rpcsvc

.PATH: ${SRCTOP}/usr.sbin/ypserv ${SRCTOP}/usr.bin/chpass \
       ${SRCTOP}/libexec/ypxfr ${RPCDIR}

PROG=	rpc.yppasswdd
SCRIPTS=yppwupdate
SCRIPTSDIR= ${LIBEXECDIR}
MAN=	rpc.yppasswdd.8
SRCS=	util.c yp_access.c yp_dblookup.c yp_dbwrite.c \
	yp_error.c yppasswdd_main.c yppasswdd_server.c ypxfr_misc.c ${GENSRCS}
GENSRCS=yp.h yp_clnt.c yppasswd.h yppasswd_private.h yppasswd_private_svc.c \
	yppasswd_private_xdr.c yppasswd_svc.c

WARNS?=	 5
CFLAGS+= -fno-strict-aliasing
CFLAGS+= -I${SRCTOP}/usr.sbin/vipw \
	 -I${SRCTOP}/usr.sbin/ypserv \
	 -I${SRCTOP}/libexec/ypxfr \
	 -I${.CURDIR} -I.
LIBADD=	rpcsvc crypt util

CLEANFILES= ${GENSRCS}

RPCGEN=	RPCGEN_CPP=${CPP:Q} rpcgen -I -C

# We need to remove the 'static' keyword from _rpcsvcstate so that
# yppasswdd_main.c can see it.
yppasswd_svc.c: yppasswd.x
	rm -f ${.TARGET}
	${RPCGEN} -m ${RPCDIR}/yppasswd.x | \
	sed s/"static int _rpcsvcstate"/"int _rpcsvcstate"/g > ${.TARGET}

yppasswd.h: yppasswd.x
	rm -f ${.TARGET}
	${RPCGEN} -h -o ${.TARGET} ${RPCDIR}/yppasswd.x

yp.h: yp.x
	rm -f ${.TARGET}
	${RPCGEN} -h -o ${.TARGET} ${RPCDIR}/yp.x

yp_clnt.c: yp.x
	rm -f ${.TARGET}
	${RPCGEN} -DYPSERV_ONLY -l -o ${.TARGET} ${RPCDIR}/yp.x

yppasswd_private.h: yppasswd_private.x
	rm -f ${.TARGET}
	${RPCGEN} -h -o ${.TARGET} ${.CURDIR}/yppasswd_private.x

yppasswd_private_xdr.c: yppasswd_private.x
	rm -f ${.TARGET}
	${RPCGEN} -c -o ${.TARGET} ${.CURDIR}/yppasswd_private.x

yppasswd_private_svc.c: yppasswd_private.x
	rm -f ${.TARGET}
	${RPCGEN} -m ${.CURDIR}/yppasswd_private.x | \
	sed s/"static int _rpcsvcstate = _IDLE"/"extern int _rpcsvcstate"/g > ${.TARGET}

.include <bsd.prog.mk>
