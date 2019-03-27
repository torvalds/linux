# $FreeBSD$

PROG=	ypxfr
SRCS=	yp_dblookup.c yp_dbwrite.c yp_error.c \
	ypxfr_getmap.c ypxfr_main.c ypxfr_misc.c \
	ypxfrd_getmap.c \
	${GENSRCS}
GENSRCS=yp.h yp_clnt.c ypxfr_clnt.c

.PATH: ${SRCTOP}/usr.sbin/ypserv

MAN=	ypxfr.8

CFLAGS+= -I.

WARNS?=	2
WFORMAT=0

LIBADD=	rpcsvc

CLEANFILES= ${GENSRCS}

RPCDIR= ${SRCTOP}/include/rpcsvc
RPCGEN=	RPCGEN_CPP=${CPP:Q} rpcgen -I -C

ypxfr_clnt.c: ${RPCDIR}/yp.x
	rm -f ${.TARGET}
	${RPCGEN} -DYPPUSH_ONLY -l -o ${.TARGET} ${RPCDIR}/yp.x

yp_clnt.c: ${RPCDIR}/yp.x
	rm -f ${.TARGET}
	${RPCGEN} -DYPSERV_ONLY -l -o ${.TARGET} ${RPCDIR}/yp.x

yp.h: ${RPCDIR}/yp.x
	rm -f ${.TARGET}
	${RPCGEN} -h -o ${.TARGET} ${RPCDIR}/yp.x

# ypxfrd_xdr.c: ${RPCDIR}/ypxfrd.x
#	rm -f ${.TARGET}
#	${RPCGEN} -c -o ${.TARGET} ${RPCDIR}/ypxfrd.x

ypxfrd.h: ${RPCDIR}/ypxfrd.x
	rm -f ${.TARGET}
	${RPCGEN} -h -o ${.TARGET} ${RPCDIR}/ypxfrd.x

.include <bsd.prog.mk>
