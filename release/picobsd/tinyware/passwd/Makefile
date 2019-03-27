# From:	@(#)Makefile	8.3 (Berkeley) 4/2/94
# $FreeBSD$

# Only NO_PAM is used by PicoBSD and supported here


PROG=	passwd
SRCS=	local_passwd.c passwd.c pw_copy.c pw_util.c 

GENSRCS=yp.h yp_clnt.c yppasswd.h yppasswd_clnt.c \
	yppasswd_private.h yppasswd_private_clnt.c yppasswd_private_xdr.c
CFLAGS+=-Wall

LIBADD=	crypt util
.PATH:  ${.CURDIR}/../../../../usr.bin/chpass \
#	${.CURDIR}/../../../../usr.sbin/vipw \
#	${.CURDIR}/../../../../usr.bin/passwd

CFLAGS+= -DLOGIN_CAP -DCRYPT -I. -I${.CURDIR} \
#	-I${.CURDIR}/../../../../usr.bin/passwd \
#	-I${.CURDIR}/../../../../usr.sbin/vipw \
#	-I${.CURDIR}/../../../../usr.bin/chpass \
#	-I${.CURDIR}/../../../../lib/libc/gen \
#	-Dyp_error=warnx -DLOGGING

CLEANFILES= ${GENSRCS}

RPCGEN=	RPCGEN_CPP=${CPP:Q} rpcgen -C
RPCSRC= ${DESTDIR}/usr/include/rpcsvc/yp.x
RPCSRC_PW= ${DESTDIR}/usr/include/rpcsvc/yppasswd.x
RPCSRC_PRIV= ${.CURDIR}/../../usr.sbin/rpc.yppasswdd/yppasswd_private.x

yp.h: ${RPCSRC}
	${RPCGEN} -h -o ${.TARGET} ${RPCSRC}

yp_clnt.c: ${RPCSRC} yp.h
	${RPCGEN} -l -o ${.TARGET} ${RPCSRC}

yppasswd.h: ${RPCSRC_PW}
	${RPCGEN} -h -o ${.TARGET} ${RPCSRC_PW}

yppasswd_clnt.c: ${RPCSRC_PW}
	${RPCGEN} -l -o ${.TARGET} ${RPCSRC_PW}

yppasswd_private.h: ${RPCSRC_PRIV}
	${RPCGEN} -h -o ${.TARGET} ${RPCSRC_PRIV}

yppasswd_private_xdr.c: ${RPCSRC_PRIV}
	${RPCGEN} -c -o ${.TARGET} ${RPCSRC_PRIV}

yppasswd_private_clnt.c: ${RPCSRC_PRIV}
	${RPCGEN} -l -o ${.TARGET} ${RPCSRC_PRIV}

BINOWN=	root
BINMODE=4555
LINKS=${BINDIR}/passwd ${BINDIR}/yppasswd
MLINKS=passwd.1 yppasswd.1

beforeinstall:
.for i in passwd yppasswd
	[ ! -e ${DESTDIR}${BINDIR}/$i ] || \
		chflags noschg ${DESTDIR}${BINDIR}/$i || true
.endfor

afterinstall:
	-chflags schg ${DESTDIR}${BINDIR}/passwd

.include <bsd.prog.mk>
