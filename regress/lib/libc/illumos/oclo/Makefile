#	$OpenBSD: Makefile,v 1.2 2025/08/09 18:17:42 anton Exp $

.if exists(/usr/local/share/illumos-os-tests)

PROGS =		oclo
PROGS +=	oclo_errors
PROGS +=	ocloexec_verify

LDADD_ocloexec_verify = -lkvm

WARNINGS =	yes

regress: ${PROGS}

.PATH: /usr/local/share/illumos-os-tests/tests/oclo
.endif

.include <bsd.regress.mk>
