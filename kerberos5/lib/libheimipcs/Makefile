#$FreeBSD$

LIB=	heimipcs
PRIVATELIB=
LIBADD=	heimbase roken pthread

SRCS=	\
	server.c \
	common.c

CFLAGS+=	-I${KRB5DIR}/lib/roken \
		-I${KRB5DIR}/base \
		-I${KRB5DIR}/lib/ipc -I.

.include <bsd.lib.mk>

.PATH: ${KRB5DIR}/lib/ipc
