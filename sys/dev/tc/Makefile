#	$OpenBSD: Makefile,v 1.2 2006/03/13 22:00:31 miod Exp $
#	$NetBSD: Makefile,v 1.1 1996/03/02 01:16:47 cgd Exp $

AWK=	awk

tcdevs_data.h: tcdevs devlist2h.awk
	/bin/rm -f tcdevs_data.h
	${AWK} -f devlist2h.awk tcdevs
