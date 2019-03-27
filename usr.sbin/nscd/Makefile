# $FreeBSD$

CONFS=	nscd.conf
PROG=	nscd
MAN=	nscd.conf.5 nscd.8

WARNS?=	3
SRCS=	agent.c nscd.c nscdcli.c cachelib.c cacheplcs.c debug.c log.c \
	config.c query.c mp_ws_query.c mp_rs_query.c singletons.c protocol.c \
	parser.c
CFLAGS+= -DCONFIG_PATH="\"${PREFIX}/etc/nscd.conf\""

LIBADD=	util pthread

.PATH: ${.CURDIR}/agents
.include "${.CURDIR}/agents/Makefile.inc"
.include <bsd.prog.mk>
