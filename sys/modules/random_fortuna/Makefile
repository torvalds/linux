# $FreeBSD$

.PATH:	${SRCTOP}/sys/dev/random

KMOD	= random_fortuna
SRCS	= randomdev.c hash.c fortuna.c
SRCS	+= opt_param.h bus_if.h device_if.h
SRCS	+= opt_ddb.h
CFLAGS	+= -DRANDOM_LOADABLE

.include <bsd.kmod.mk>
