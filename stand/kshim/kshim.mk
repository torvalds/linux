#
# $FreeBSD$
#
# Copyright (c) 2013 Hans Petter Selasky.
# Copyright (c) 2014 SRI International
# All rights reserved.
#
# This software was developed by SRI International and the University of
# Cambridge Computer Laboratory under DARPA/AFRL contract FA8750-10-C-0237
# ("CTSRD"), as part of the DARPA CRASH research programme.
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

KSHIM_DIR:=	${.PARSEDIR}
.PATH:		${KSHIM_DIR}

CFLAGS+=	-I${KSHIM_DIR}
CFLAGS+=	-I${KSHIM_DIR}/../..
CFLAGS+=	-DUSB_GLOBAL_INCLUDE_FILE=\"bsd_global.h\"
CFLAGS+=	-DHAVE_ENDIAN_DEFS

#
# Single threaded BSD kernel
#
KSRCS+=	bsd_kernel.c

#
# BUSSPACE implementation
#
KSRCS+=	bsd_busspace.c

SRCS+=	sysinit_data.c
SRCS+=	sysuninit_data.c

CLEANFILES+= sysinit.bin
CLEANFILES+= sysinit_data.c
CLEANFILES+= sysuninit_data.c

SRCS+=	${KSRCS}
SYSINIT_OBJS=	${KSRCS:R:C/$/.osys/}
CLEANFILES+=	${SYSINIT_OBJS}

#
# SYSINIT() and SYSUNINIT() handling
#

sysinit_data.c: sysinit.bin
	sysinit -i sysinit.bin -o ${.TARGET} -k sysinit -s sysinit_data

sysuninit_data.c: sysinit.bin
	sysinit -i sysinit.bin -o ${.TARGET} -R -k sysuninit -s sysuninit_data

.for KSRC in ${KSRCS:R}
${KSRC}.osys: ${KSRC}.o
	${OBJCOPY} -j ".debug.sysinit" -O binary ${KSRC}.o ${KSRC}.osys
.endfor

sysinit.bin: ${SYSINIT_OBJS}
	cat ${.ALLSRC} > sysinit.bin
