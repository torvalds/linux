# $FreeBSD$

SRCS.xdr_test=		${RPCSRC:.x=_xdr.c} t_xdr.c ${RPCSRC:.x=.h} \
			h_testbits.h

NETBSD_ATF_TESTS_C=	rpc_test
NETBSD_ATF_TESTS_C+=	xdr_test

RPCSRC=			h_testbits.x
RPCGEN=			RPCGEN_CPP=${CPP:Q} rpcgen -L -C

h_testbits.h: ${RPCSRC}
	${RPCGEN} -h -o ${.TARGET} ${.ALLSRC}

h_testbits_xdr.c: ${RPCSRC} h_testbits.h
	${RPCGEN} ${.ALLSRC:M*.x}

CLEANFILES+=	${RPCSRC:.x=.h} ${RPCSRC:.x=.c} h_testbits_xdr.c
CFLAGS+=	-I${.OBJDIR}

LIBADD+=	rpcsvc util

.include "../Makefile.netbsd-tests"

.include <bsd.test.mk>
