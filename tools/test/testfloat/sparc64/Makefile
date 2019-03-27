# Copyright (c) 2010 by Peter Jeremy <peterjeremy@acm.org>
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
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
# IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
# OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
# IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
# INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
# (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
# SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
# CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
# OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
# USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
# $FreeBSD$

.PATH: ${.CURDIR}/../../../../lib/libc/softfloat/bits64 ${.CURDIR}/..

LIBC_DIR=	${.CURDIR}/../../../../lib/libc
EMUFLOAT_DIR=	${LIBC_DIR}/sparc64/fpu

LN=	ln -sf

# Common source files
SRCS1=	fail.c random.c softfloat.c testCases.c testLoops.c writeHex.c

# Additional common sources to build testfloat/testemufloat
SRCS2=	testFunction.c testfloat.c

# Additional sources to build testemufloat
SRCS3=	fpu.c fpu_add.c fpu_compare.c fpu_div.c fpu_emul.S fpu_explode.c \
	fpu_implode.c fpu_mul.c fpu_qp.c fpu_sqrt.c fpu_subr.c fpu_util.c

# Additional sources to build testfloat
SRCS4=	systflags.c systfloat.S systmodes.c

# Additional sources to build testsoftfloat
SRCS5=	slowfloat.c testsoftfloat.c

SRCS=	${SRCS1} ${SRCS2} ${SRCS3} ${SRCS4} ${SRCS5}

OBJ_TF=		${SRCS1:R:S/$/.o/g} ${SRCS2:R:S/$/.o/g} ${SRCS4:R:S/$/.o/g}
OBJ_TEF=	${SRCS1:R:S/$/.o/g} ${SRCS2:R:S/$/.o/g} ${SRCS3:R:S/$/.o/g}
OBJ_TSF=	${SRCS1:R:S/$/.o/g} ${SRCS5:R:S/$/.o/g}

all: testfloat testemufloat testsoftfloat

CFLAGS+=	-I. -I${.CURDIR} -I${.CURDIR}/.. -I${LIBC_DIR}/sparc64/fpu \
	-I${LIBC_DIR}/sparc64/sys -I${LIBC_DIR}/softfloat/bits64 \
	-I${LIBC_DIR}/softfloat

CLEANFILES+=	fpu.c fpu_add.c fpu_compare.c fpu_div.c fpu_emu.h \
	fpu_explode.c fpu_implode.c fpu_mul.c fpu_qp.c fpu_sqrt.c fpu_subr.c \
	${SRCS:R:S/$/.o/g} testfloat testemufloat testsoftfloat

testsoftfloat: ${OBJ_TSF}
	${CC} ${CFLAGS} ${LDFLAGS} -o ${.TARGET} ${OBJ_TSF}

testfloat: ${OBJ_TF}
	${CC} ${CFLAGS} ${LDFLAGS} -o ${.TARGET} ${OBJ_TF}

testemufloat: ${OBJ_TEF}
	${CC} ${CFLAGS} ${LDFLAGS} -o ${.TARGET} ${OBJ_TEF}

beforedepend: fpu_emu.h

# The emulator code needs to be built with a local fpu_reg.h instead of
# the one in ${EMUFLOAT_DIR}.  Unfortunately, C preprocessor semantics
# means that a header file in the same directory as the source file
# overrides any alternative header file location.  In order to include
# the wanted header file, create symlinks pointing to the real files
# and compile through the symlink.
fpu.c: ${EMUFLOAT_DIR}/fpu.c
	${LN} ${.ALLSRC} ${.TARGET}
fpu_add.c: ${EMUFLOAT_DIR}/fpu_add.c
	${LN} ${.ALLSRC} ${.TARGET}
fpu_compare.c: ${EMUFLOAT_DIR}/fpu_compare.c
	${LN} ${.ALLSRC} ${.TARGET}
fpu_div.c: ${EMUFLOAT_DIR}/fpu_div.c
	${LN} ${.ALLSRC} ${.TARGET}
fpu_emu.h: ${EMUFLOAT_DIR}/fpu_emu.h
	${LN} ${.ALLSRC} ${.TARGET}
fpu_explode.c: ${EMUFLOAT_DIR}/fpu_explode.c
	${LN} ${.ALLSRC} ${.TARGET}
fpu_implode.c: ${EMUFLOAT_DIR}/fpu_implode.c
	${LN} ${.ALLSRC} ${.TARGET}
fpu_mul.c: ${EMUFLOAT_DIR}/fpu_mul.c
	${LN} ${.ALLSRC} ${.TARGET}
fpu_qp.c: ${EMUFLOAT_DIR}/fpu_qp.c
	${LN} ${.ALLSRC} ${.TARGET}
fpu_sqrt.c: ${EMUFLOAT_DIR}/fpu_sqrt.c
	${LN} ${.ALLSRC} ${.TARGET}
fpu_subr.c: ${EMUFLOAT_DIR}/fpu_subr.c
	${LN} ${.ALLSRC} ${.TARGET}

.include <bsd.prog.mk>
