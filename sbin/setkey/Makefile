# Copyright (C) 1995, 1996, 1997, 1998, and 1999 WIDE Project.
# All rights reserved.
# 
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
# 3. Neither the name of the project nor the names of its contributors
#    may be used to endorse or promote products derived from this software
#    without specific prior written permission.
# 
# THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED.  IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
# OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
# OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.
#
# $FreeBSD$

.include <src.opts.mk>

PACKAGE=runtime
PROG=	setkey
MAN=	setkey.8
SRCS=	setkey.c parse.y token.l
WARNS?=	1

CFLAGS+= -I${.CURDIR} -I${SRCTOP}/lib/libipsec
YFLAGS=	-d

LIBADD=	l y

CLEANFILES=	y.tab.c y.tab.h key_test.o keytest

# libpfkey
# ipsec_strerror.c is for avoiding shlib reference to non-exported function.
.PATH: ${SRCTOP}/lib/libipsec ${SRCTOP}/sys/netipsec
SRCS+= pfkey.c pfkey_dump.c key_debug.c ipsec_strerror.c
CFLAGS+= -I${SRCTOP}/sys

SRCS+=	y.tab.h
y.tab.h: parse.y
CFLAGS+= -DIPSEC_DEBUG -DYY_NO_UNPUT
.if ${MK_INET_SUPPORT} != "no"
CFLAGS+= -DINET
.endif
.if ${MK_INET6_SUPPORT} != "no"
CFLAGS+= -DINET6
.endif
CFLAGS+= -I.
LIBADD+=	ipsec
CLEANFILES+=	scriptdump y.tab.h

#SCRIPTS= scriptdump

LOCALPREFIX=	/usr/local

scriptdump: scriptdump.pl
	sed -e 's#@LOCALPREFIX@#${LOCALPREFIX}#' < $> > scriptdump

.include <bsd.prog.mk>
