# $FreeBSD$

.PATH:	${SRCTOP}/contrib/pf/pflogd

PACKAGE=pf
PROG=	pflogd
SRCS= 	pflogd.c pidfile.c privsep.c privsep_fdpass.c
MAN=	pflogd.8

CFLAGS+=-include ${SRCTOP}/lib/libpcap/config.h
CFLAGS+=-I${SRCTOP}/contrib/libpcap

LIBADD=	pcap

WARNS?=	2

.include <bsd.prog.mk>
