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

ATF=		${SRCTOP}/contrib/atf
.PATH:		${ATF}/atf-sh

PROG_CXX=	atf-sh
SRCS=		atf-sh.cpp
MAN=		atf-sh.1 atf-sh.3
# Backwards compatibility.
MLINKS+=	atf-sh.3 atf-sh-api.3

MLINKS+=	\
		atf-sh.3 atf_add_test_case.3 \
		atf-sh.3 atf_check.3 \
		atf-sh.3 atf_check_equal.3 \
		atf-sh.3 atf_config_get.3 \
		atf-sh.3 atf_config_has.3 \
		atf-sh.3 atf_expect_death.3 \
		atf-sh.3 atf_expect_exit.3 \
		atf-sh.3 atf_expect_fail.3 \
		atf-sh.3 atf_expect_pass.3 \
		atf-sh.3 atf_expect_signal.3 \
		atf-sh.3 atf_expect_timeout.3 \
		atf-sh.3 atf_fail.3 \
		atf-sh.3 atf_get.3 \
		atf-sh.3 atf_get_srcdir.3 \
		atf-sh.3 atf_init_test_cases.3 \
		atf-sh.3 atf_pass.3 \
		atf-sh.3 atf_require_prog.3 \
		atf-sh.3 atf_set.3 \
		atf-sh.3 atf_skip.3 \
		atf-sh.3 atf_test_case.3

CFLAGS+=	-DHAVE_CONFIG_H
CFLAGS+=	-DATF_LIBEXECDIR='"${LIBEXECDIR}"'
CFLAGS+=	-DATF_PKGDATADIR='"${SHAREDIR}/atf"'
CFLAGS+=	-DATF_SHELL='"/bin/sh"'
CFLAGS+=	-I${ATF}

LIBADD=		atf_cxx

FILESGROUPS=	SUBR

SUBRDIR=	${SHAREDIR}/atf
SUBR=		libatf-sh.subr

HAS_TESTS=
SUBDIR.${MK_TESTS}+= tests

.include "../../../lib/atf/common.mk"
.include <bsd.prog.mk>
