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
#
# $FreeBSD$

.include <src.opts.mk>

PROG=	rtadvd
MAN=	rtadvd.conf.5 rtadvd.8
SRCS=	rtadvd.c rrenum.c advcap.c if.c config.c timer.c timer_subr.c	\
	control.c control_server.c

.if ${MK_EXPERIMENTAL} != "no"
CFLAGS+=	-DEXPERIMENTAL
CFLAGS+=	-DDRAFT_IETF_6MAN_IPV6ONLY_FLAG
.endif

LIBADD=	util

WARNS?=	1

.include <bsd.prog.mk>
