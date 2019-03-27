#	from: @(#)Makefile	5.8 (Berkeley) 7/28/90
# $FreeBSD$

.include <src.opts.mk>

PROG=	bootparamd
MAN=	bootparams.5 bootparamd.8
SRCS=	bootparamd.c main.c ${GENSRCS}
GENSRCS=bootparam_prot.h bootparam_prot_svc.c bootparam_prot_xdr.c

CFLAGS+= -DTFTP_DIR=\"/tftpboot\" -I.
.if ${MK_NIS} != "no"
CFLAGS+= -DYP
.endif

CLEANFILES= ${GENSRCS}

RPCSRC=	${SYSROOT:U${DESTDIR}}/usr/include/rpcsvc/bootparam_prot.x

bootparam_prot_svc.c: ${RPCSRC}
	RPCGEN_CPP=${CPP:Q} rpcgen -C -m -o ${.TARGET} ${RPCSRC}

bootparam_prot_xdr.c: ${RPCSRC}
	RPCGEN_CPP=${CPP:Q} rpcgen -C -c -o ${.TARGET} ${RPCSRC}

bootparam_prot.h: ${RPCSRC}
	RPCGEN_CPP=${CPP:Q} rpcgen -C -h -o ${.TARGET} ${RPCSRC}

.include <bsd.prog.mk>
