# Copyright (c) 1996 WIDE Project. All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modifications, are permitted provided that the above copyright notice
# and this paragraph are duplicated in all such forms and that any
# documentation, advertising materials, and other materials related to
# such distribution and use acknowledge that the software was developed
# by the WIDE Project, Japan. The name of the Project may not be used to
# endorse or promote products derived from this software without
# specific prior written permission. THIS SOFTWARE IS PROVIDED ``AS IS''
# AND WITHOUT ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, WITHOUT
# LIMITATION, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE.
# $FreeBSD$

TRACEROUTE_DISTDIR?= ${SRCTOP}/contrib/traceroute
.PATH: ${TRACEROUTE_DISTDIR}

PROG=	traceroute6
MAN=	traceroute6.8
SRCS=	as.c traceroute6.c
BINOWN=	root
BINMODE= 4555

CFLAGS+= -DIPSEC -DUSE_RFC2292BIS -DHAVE_POLL
CFLAGS+= -I${.CURDIR} -I${TRACEROUTE_DISTDIR} -I.

WARNS?=	3

LIBADD=	ipsec

.include <bsd.prog.mk>
