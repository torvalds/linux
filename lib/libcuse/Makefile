# $FreeBSD$
#
# Copyright (c) 2010 Hans Petter Selasky. All rights reserved.
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

PACKAGE=lib${LIB}
LIB=		cuse
SHLIB_MAJOR=	1
SHLIB_MINOR=	0
SRCS=		cuse_lib.c
INCS=		cuse.h
MAN=		cuse.3
CFLAGS+=	-D_GNU_SOURCE
.if defined(HAVE_DEBUG)
CFLAGS+=	-g
CFLAGS+=	-DHAVE_DEBUG
.endif
LIBADD=	pthread

MLINKS=
MLINKS+= cuse.3 cuse_alloc_unit_number.3
MLINKS+= cuse.3 cuse_alloc_unit_number_by_id.3
MLINKS+= cuse.3 cuse_copy_in.3
MLINKS+= cuse.3 cuse_copy_out.3
MLINKS+= cuse.3 cuse_dev_create.3
MLINKS+= cuse.3 cuse_dev_destroy.3
MLINKS+= cuse.3 cuse_dev_get_current.3
MLINKS+= cuse.3 cuse_dev_get_per_file_handle.3
MLINKS+= cuse.3 cuse_dev_get_priv0.3
MLINKS+= cuse.3 cuse_dev_get_priv1.3
MLINKS+= cuse.3 cuse_dev_set_per_file_handle.3
MLINKS+= cuse.3 cuse_dev_set_priv0.3
MLINKS+= cuse.3 cuse_dev_set_priv1.3
MLINKS+= cuse.3 cuse_free_unit_number.3
MLINKS+= cuse.3 cuse_free_unit_number_by_id.3
MLINKS+= cuse.3 cuse_got_peer_signal.3
MLINKS+= cuse.3 cuse_init.3
MLINKS+= cuse.3 cuse_poll_wakeup.3
MLINKS+= cuse.3 cuse_set_local.3
MLINKS+= cuse.3 cuse_get_local.3
MLINKS+= cuse.3 cuse_uninit.3
MLINKS+= cuse.3 cuse_vmalloc.3
MLINKS+= cuse.3 cuse_is_vmalloc_addr.3
MLINKS+= cuse.3 cuse_vmfree.3
MLINKS+= cuse.3 cuse_vmoffset.3
MLINKS+= cuse.3 cuse_wait_and_process.3

.include <bsd.lib.mk>
