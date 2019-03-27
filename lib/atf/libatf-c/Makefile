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

# Store the toolchain executable in ATF_BUILD_{CC,CPP,CXX} to ensure other
# values -- like -target, -B ..., etc -- don't get leaked into the tests.
#
# Be sure to omit ${CCACHE_BIN} (if specified) from the variable as it gets
# automatically appended to the variables in bsd.compiler.mk when
# ${MK_CCACHE_BUILD} != no.
ATF_BUILD_CC:=		${CC:N${CCACHE_BIN}:[1]}
ATF_BUILD_CPP:=		${CPP:N${CCACHE_BIN}:[1]}
ATF_BUILD_CXX:=		${CXX:N${CCACHE_BIN}:[1]}

# Only capture defines, includes, linker flags, optimization levels, warnings
# and preprocessor flags when building ATF_BUILD_{C,CPP,CXX}FLAGS.
ATF_BUILD_CFLAGS:=	${CFLAGS:M-[DILOWf]*}
ATF_BUILD_CPPFLAGS:=	${CPPFLAGS:M-[DILOWf]*}
ATF_BUILD_CXXFLAGS:=	${CXXFLAGS:M-[DILOWf]*}

LIB=		atf-c
PRIVATELIB=	true
SHLIB_MAJOR=	1

ATF=		${SRCTOP}/contrib/atf
.PATH:		${ATF}
.PATH:		${ATF}/atf-c
.PATH:		${ATF}/atf-c/detail

CFLAGS+=	-DATF_BUILD_CC='"${ATF_BUILD_CC}"'
CFLAGS+=	-DATF_BUILD_CFLAGS='"${ATF_BUILD_CFLAGS}"'
CFLAGS+=	-DATF_BUILD_CPP='"${ATF_BUILD_CPP}"'
CFLAGS+=	-DATF_BUILD_CPPFLAGS='"${ATF_BUILD_CPPFLAGS}"'
CFLAGS+=	-DATF_BUILD_CXX='"${ATF_BUILD_CXX}"'
CFLAGS+=	-DATF_BUILD_CXXFLAGS='"${ATF_BUILD_CXXFLAGS}"'
CFLAGS+=	-I${ATF}
CFLAGS+=	-I${.CURDIR}
CFLAGS+=	-I.

SRCS=		build.c \
		check.c \
		dynstr.c \
		env.c \
		error.c \
		fs.c \
		list.c \
		map.c \
		process.c \
		sanity.c \
		text.c \
		user.c \
		utils.c \
		tc.c \
		tp.c \
		tp_main.c

INCS=		build.h \
		check.h \
		defs.h \
		error.h \
		error_fwd.h \
		macros.h \
		tc.h \
		tp.h \
		utils.h
INCSDIR=	${INCLUDEDIR}/atf-c

INCS+=		atf-c.h
INCSDIR_atf-c.h= ${INCLUDEDIR}

MAN=		atf-c.3
MLINKS+=	atf-c.3 atf-c-api.3  # Backwards compatibility.

HAS_TESTS=
SUBDIR.${MK_TESTS}+= tests

.include "../common.mk"
.include <bsd.lib.mk>
