#	$OpenBSD: Makefile.inc,v 1.1 2019/01/23 13:05:27 florian Exp $

.PATH: ${.CURDIR}/libunbound/util/storage

SRCS+=	dnstree.c lookup3.c lruhash.c slabhash.c
