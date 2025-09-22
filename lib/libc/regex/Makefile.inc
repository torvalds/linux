#	$OpenBSD: Makefile.inc,v 1.8 2016/03/30 06:38:41 jmc Exp $
#	@(#)Makefile.inc	8.1 (Berkeley) 6/4/93

# regex sources
.PATH: ${LIBCSRCDIR}/regex

SRCS+=	regcomp.c regerror.c regexec.c regfree.c

MAN+=	regex.3 re_format.7
