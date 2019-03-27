#-
# Copyright (c) 2013-2014 Robert N. M. Watson
# All rights reserved.
#
# This software was developed by SRI International and the University of
# Cambridge Computer Laboratory under DARPA/AFRL contract (FA8750-10-C-0237)
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
# $FreeBSD$

.include <bsd.init.mk>

INSTALLFLAGS=	-b

LOADERS=	flashboot jtagboot
FILES=		${LOADERS} ${LOADERS:S/$/.md5/}

SRCS=		relocate.S		\
		start.S			\
		boot2.c			\
		altera_jtag_uart.c	\
		cfi.c			\
		sdcard.c

AFLAGS=		-G0

CFLAGS+=	-I${LDRSRC}			\
		-Wall				\
		-G0 				\
		-fno-pic -mno-abicalls		\
		-g

LDFLAGS+=	-static				\
		-Wl,-N				\
		-G0				\
		-L${.CURDIR}

.PATH:		${BOOTSRC}/mips/beri/common
CFLAGS+=	-I${BOOTSRC}/mips/beri/common

flashboot.elf: relocate.o start.o boot2.o altera_jtag_uart.o cfi.o sdcard.o
	${CC:N${CCACHE_BIN}} ${LDFLAGS} -T ${.CURDIR}/flashboot.ldscript \
	    -o ${.TARGET} ${.ALLSRC} ${LIBSA}
flashboot: flashboot.elf
	${OBJCOPY} -S -O binary ${.TARGET}.elf ${.TARGET}
flashboot.md5: flashboot
	md5 flashboot > flashboot.md5

jtagboot: start.o boot2.o altera_jtag_uart.o cfi.o sdcard.o
	${CC:N${CCACHE_BIN}} ${LDFLAGS} -T ${.CURDIR}/jtagboot.ldscript \
	    -o ${.TARGET} ${.ALLSRC} ${LIBSA}
jtagboot.md5: jtagboot
	md5 jtagboot > jtagboot.md5

CLEANFILES+=	flashboot.elf

.include <bsd.prog.mk>
