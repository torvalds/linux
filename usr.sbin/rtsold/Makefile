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

PROG=	rtsold
MAN=	rtsold.8
MLINKS=	rtsold.8 rtsol.8
SRCS=	cap_llflags.c \
	cap_script.c \
	cap_sendmsg.c \
	dump.c \
	if.c \
	rtsock.c \
	rtsol.c \
	rtsold.c

LIBADD= util

.include <src.opts.mk>

.if ${MK_CASPER} != "no"
CFLAGS+= -DWITH_CASPER
LIBADD+= casper cap_syslog nv
.endif

.include <bsd.prog.mk>
