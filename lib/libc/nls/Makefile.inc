#	$OpenBSD: Makefile.inc,v 1.5 2015/09/05 11:25:30 guenther Exp $

.PATH: ${LIBCSRCDIR}/nls

SRCS+=	catclose.c catgets.c catopen.c 
MAN+=	catclose.3 catgets.3 catopen.3
