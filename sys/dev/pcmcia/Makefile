#	$OpenBSD: Makefile,v 1.3 1999/04/30 07:28:25 fgsch Exp $
#	$NetBSD: Makefile.pcmciadevs,v 1.1 1998/07/19 17:28:15 christos Exp $

AWK=	awk

pcmciadevs.h: pcmciadevs devlist2h.awk
	/bin/rm -f pcmciadevs.h
	${AWK} -f devlist2h.awk pcmciadevs
