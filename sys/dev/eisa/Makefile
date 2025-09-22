#	$OpenBSD: Makefile,v 1.1 1996/04/18 23:47:08 niklas Exp $
#	$NetBSD: Makefile,v 1.1 1996/02/26 23:46:17 cgd Exp $

AWK=	awk

eisadevs.h eisadevs_data.h: eisadevs devlist2h.awk
	/bin/rm -f eisadevs.h eisadevs_data.h
	${AWK} -f devlist2h.awk eisadevs
