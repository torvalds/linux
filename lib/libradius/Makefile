# Copyright 1998 Juniper Networks, Inc.
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

PACKAGE=lib${LIB}
LIB=		radius
SRCS=		radlib.c
INCS=		radlib.h radlib_vs.h
CFLAGS+=	-Wall
SHLIB_MAJOR=	4
MAN=		libradius.3 radius.conf.5

MLINKS+=libradius.3 rad_acct_open.3 \
	libradius.3 rad_add_server.3 \
	libradius.3 rad_add_server_ex.3 \
	libradius.3 rad_auth_open.3 \
	libradius.3 rad_bind_to.3 \
	libradius.3 rad_close.3 \
	libradius.3 rad_config.3 \
	libradius.3 rad_continue_send_request.3 \
	libradius.3 rad_create_request.3 \
	libradius.3 rad_create_response.3 \
	libradius.3 rad_cvt_addr.3 \
	libradius.3 rad_cvt_int.3 \
	libradius.3 rad_cvt_string.3 \
	libradius.3 rad_demangle.3 \
	libradius.3 rad_demangle_mppe_key.3 \
	libradius.3 rad_get_attr.3 \
	libradius.3 rad_get_vendor_attr.3 \
	libradius.3 rad_init_send_request.3 \
	libradius.3 rad_put_addr.3 \
	libradius.3 rad_put_attr.3 \
	libradius.3 rad_put_int.3 \
	libradius.3 rad_put_message_authentic.3 \
	libradius.3 rad_put_string.3 \
	libradius.3 rad_put_vendor_addr.3 \
	libradius.3 rad_put_vendor_attr.3 \
	libradius.3 rad_put_vendor_int.3 \
	libradius.3 rad_put_vendor_string.3 \
	libradius.3 rad_receive_request.3 \
	libradius.3 rad_request_authenticator.3 \
	libradius.3 rad_send_request.3 \
	libradius.3 rad_send_response.3 \
	libradius.3 rad_server_open.3 \
	libradius.3 rad_server_secret.3 \
	libradius.3 rad_strerror.3

WARNS?=		3

.if ${MK_OPENSSL} != "no"
LIBADD+=	crypto
CFLAGS+=	-DWITH_SSL
.else
LIBADD+=	md
.endif

.include <bsd.lib.mk>
