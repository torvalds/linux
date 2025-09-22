#	$OpenBSD: Makefile.inc,v 1.6 2014/07/11 13:13:04 deraadt Exp $

BINDIR?=	/sbin
LDSTATIC=	${STATIC}
COPTS+=		-Werror-implicit-function-declaration
