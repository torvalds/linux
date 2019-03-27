# $FreeBSD$

PROG =	rpc.rusersd
SRCS =	rusersd.c rusers_proc.c extern.h
MAN =	rpc.rusersd.8

LIBADD=	rpcsvc

#.if exists(/usr/X11R6/include/X11/extensions/xidle.h)
#CFLAGS+= -DXIDLE
#LDADD+= -L/usr/X11R6/lib -lXext -lX11
#.endif

.include <bsd.prog.mk>
