# $OpenBSD: Makefile,v 1.1 2006/03/04 16:27:03 grange Exp $

AWK?=	awk

onewiredevs.h onewiredevs_data.h: onewiredevs devlist2h.awk
	/bin/rm -f onewiredevs.h onewiredevs_data.h
	${AWK} -f devlist2h.awk onewiredevs
