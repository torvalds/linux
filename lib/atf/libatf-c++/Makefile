#-
# Copyright (c) 2011 Google, Inc.
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
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
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
.include <bsd.init.mk>

LIB_CXX=	atf-c++
PRIVATELIB=	true
SHLIB_MAJOR=	2

# libatf-c++ depends on the C version of the ATF library to build.
LIBADD+=	atf_c

LDFLAGS+=	-L${.OBJDIR:H}/libatf-c

ATF=		${SRCTOP}/contrib/atf
.PATH:		${ATF}
.PATH:		${ATF}/atf-c++
.PATH:		${ATF}/atf-c++/detail

CFLAGS+=	-I${ATF}
CFLAGS+=	-I${.CURDIR:H}/libatf-c
CFLAGS+=	-I.

CFLAGS+=	-DHAVE_CONFIG_H

SRCS=		application.cpp \
		build.cpp \
		check.cpp \
		env.cpp \
		exceptions.cpp \
		fs.cpp \
		process.cpp \
		tests.cpp \
		text.cpp \
		utils.cpp

INCS=		build.hpp \
		check.hpp \
		macros.hpp \
		tests.hpp \
		utils.hpp
INCSDIR=	${INCLUDEDIR}/atf-c++

INCS+=		atf-c++.hpp
INCSDIR_atf-c++.hpp= ${INCLUDEDIR}

MAN=		atf-c++.3
MLINKS+=	atf-c++.3 atf-c-api++.3  # Backwards compatibility.

HAS_TESTS=
SUBDIR.${MK_TESTS}+= tests

.include "../common.mk"
.include <bsd.lib.mk>
