#	$OpenBSD: Makefile.inc,v 1.1 2019/01/23 13:05:27 florian Exp $

.PATH: ${.CURDIR}/libunbound/util/data

SRCS+=	dname.c msgencode.c msgparse.c msgreply.c packed_rrset.c
