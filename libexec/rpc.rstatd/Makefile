#	$OpenBSD: Makefile,v 1.9 2014/07/16 20:02:45 okan Exp $

PROG =	rpc.rstatd
.PATH: ${.CURDIR}/../../usr.bin/vmstat

CFLAGS+=-I${.CURDIR}/../../usr.bin/vmstat -DNOKVM
SRCS =	dkstats.c rstatd.c rstat_proc.c
MAN =	rpc.rstatd.8

DPADD=	${LIBRPCSVC}
LDADD=	-lrpcsvc

.include <bsd.prog.mk>
