#	$OpenBSD: Makefile,v 1.4 2014/07/16 20:08:25 okan Exp $

AWK?=	awk

cpudevs.h cpudevs_data.h: cpudevs devlist2h.awk
	/bin/rm -f cpudevs.h cpudevs_data.h
	${AWK} -f devlist2h.awk cpudevs
