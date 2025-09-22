#	$OpenBSD: Makefile,v 1.4 1996/10/14 09:01:34 deraadt Exp $
#	$NetBSD: Makefile,v 1.1 1995/06/18 01:07:04 cgd Exp $

AWK=	awk

pcidevs.h pcidevs_data.h: pcidevs devlist2h.awk
	/bin/rm -f pcidevs.h pcidevs_data.h
	${AWK} -f devlist2h.awk pcidevs
