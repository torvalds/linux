#	$OpenBSD: Makefile,v 1.15 2025/01/30 20:02:13 kn Exp $

PROG=	sysctl
MAN=	sysctl.8

afterinstall:
	ln -sf ../../sbin/sysctl ${DESTDIR}/usr/sbin
	chown -h ${BINOWN}:${BINGRP} ${DESTDIR}/usr/sbin/sysctl

.include <bsd.prog.mk>
