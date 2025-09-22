# $OpenBSD: Makefile,v 1.17 2025/07/23 07:47:23 tb Exp $

SUBDIR += libressl
.if exists(/usr/local/bin/eopenssl35)
SUBDIR += openssl
.endif

.include <bsd.subdir.mk>
.include <bsd.regress.mk>
