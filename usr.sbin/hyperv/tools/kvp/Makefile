# $FreeBSD$

.include <bsd.own.mk>

HV_KVP_DAEMON_DISTDIR?= ${SRCTOP}/contrib/hyperv/tools
.PATH: ${HV_KVP_DAEMON_DISTDIR}

PROG= 	hv_kvp_daemon
MAN= 	hv_kvp_daemon.8

CFLAGS+=	-I${SRCTOP}/sys/dev/hyperv/utilities

.include <bsd.prog.mk>
