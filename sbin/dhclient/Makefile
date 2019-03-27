# $OpenBSD: Makefile,v 1.9 2004/05/04 12:52:05 henning Exp $
# $FreeBSD$
#
# Copyright (c) 1996, 1997 The Internet Software Consortium.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
#
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
# 3. Neither the name of The Internet Software Consortium nor the names of its
#    contributors may be used to endorse or promote products derived
#    from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE INTERNET SOFTWARE CONSORTIUM AND
# CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING,
# BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
# FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL
# THE INTERNET SOFTWARE CONSORTIUM OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
# INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
# (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
# SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
# STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
# OF THE POSSIBILITY OF SUCH DAMAGE.
#

.include <src.opts.mk>

CONFS=	dhclient.conf
PACKAGE=runtime
SRCS=	dhclient.c clparse.c alloc.c dispatch.c hash.c bpf.c options.c \
	tree.c conflex.c errwarn.c inet.c packet.c convert.c tables.c \
	parse.c privsep.c

PROG=	dhclient
SCRIPTS=dhclient-script
MAN=	dhclient.8 dhclient.conf.5 dhclient.leases.5 dhcp-options.5 \
	dhclient-script.8
LIBADD=	util

.if ${MK_DYNAMICROOT} == "no"
.warning ${PROG} built without libcasper support
.elif ${MK_CASPER} != "no" && !defined(RESCUE)
LIBADD+=        casper
LIBADD+=        cap_syslog
CFLAGS+=-DWITH_CASPER
.endif

NO_WCAST_ALIGN=	yes

HAS_TESTS=
SUBDIR.${MK_TESTS}+= tests

.include <bsd.prog.mk>
