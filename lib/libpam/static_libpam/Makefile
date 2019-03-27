#-
# Copyright (c) 1998-2015 Juniper Networks, Inc.
# All rights reserved.
# Copyright (c) 2002 Networks Associates Technology, Inc.
# All rights reserved.
#
# Portions of this software was developed for the FreeBSD Project by
# ThinkSec AS and NAI Labs, the Security Research Division of Network
# Associates, Inc.  under DARPA/SPAWAR contract N66001-01-C-8035
# ("CBOSS"), as part of the DARPA CHATS research program.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
# 3. The name of the author may not be used to endorse or promote
#    products derived from this software without specific prior written
#    permission.
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

.PATH: ${SRCTOP}/lib/libpam

# Only build the static library.
LIB=	pam
NO_PIC=

# Avoid redundancy with the master Makefile.
MAN=
INCS=
MLINKS=
MK_TESTS=	no

#
# Static modules
#
# We build static versions of all modules and of openpam_static.o,
# then link them all together into openpam_static_modules.o.  None of
# the modules export any symbols, but they store structures with
# pointers to their service functions in a linker set which the code
# in openpam_static.c traverses to locate the individual modules.
#
MODULE_DIR=	../modules
.include "${.CURDIR}/${MODULE_DIR}/modules.inc"
STATIC_MODULES=	${MODULES:C/.*/${MODULE_DIR}\/&\/lib&.a/}
STATICOBJS+=	openpam_static_modules.o
CLEANFILES+=	openpam_static.o \
		openpam_static_modules.o

openpam_static_modules.o: openpam_static.o ${STATIC_MODULES}
	${CC:N${CCACHE_BIN}} -nostdlib ${LDFLAGS} -o ${.TARGET} \
	    -r -Wl,--whole-archive ${.ALLSRC}

.include "${.CURDIR:H}/libpam/Makefile"
