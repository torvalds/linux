#	$OpenBSD: Makefile,v 1.1 1998/09/10 17:17:32 jason Exp $
#	$NetBSD: Makefile.miidevs,v 1.1 1998/08/10 23:55:17 thorpej Exp $

AWK=	awk

miidevs.h: miidevs devlist2h.awk
	/bin/rm -f miidevs.h
	${AWK} -f devlist2h.awk miidevs
