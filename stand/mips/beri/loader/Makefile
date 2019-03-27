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

LOADER_MSDOS_SUPPORT?=	yes
LOADER_UFS_SUPPORT?=	yes
LOADER_CD9660_SUPPORT?=	no
LOADER_EXT2FS_SUPPORT?=	no
LOADER_GZIP_SUPPORT?=	yes
LOADER_BZIP2_SUPPORT?=	yes

.include <bsd.init.mk>

PROG?=		loader
NEWVERSWHAT=	"BERI loader" ${MACHINE_CPUARCH}
INSTALLFLAGS=	-b

# Architecture-specific loader code
SRCS=		start.S						\
		main.c						\
		devicename.c					\
		exec.c						\
		vers.c						\
		arch.c

# libstand front-ends for shared driver code
SRCS+=		beri_console.c					\
		beri_disk_cfi.c					\
		beri_disk_sdcard.c

# Common code with boot2
SRCS+=		altera_jtag_uart.c				\
		cfi.c						\
		sdcard.c

# Since we don't have a backward compatibility issue, default to this on BERI.
CFLAGS+=	-DBOOT_PROMPT_123

HELP_FILES=	${.CURDIR}/help.mips

# Always add MI sources
.include	"${BOOTSRC}/loader.mk"

# BERI files common to boot2 and loader
.PATH:		${BOOTSRC}/mips/beri/common
CFLAGS+=	-I${BOOTSRC}/mips/beri/common

# Loader-specific MD headers
CFLAGS+=	-I${.CURDIR}

# Generate code appropriate for the loader environment
CFLAGS+=	-G0					\
		-fno-pic				\
		-mno-abicalls				\
		-g

LDFLAGS+=	-static					\
		-T ${.CURDIR}/loader.ldscript		\
		-L${.CURDIR}				\
		-e __start

DPADD=		${LDR_INTERP} ${LIBSA}
LDADD=		${LDR_INTERP} ${LIBSA}

.if defined(LOADER_USB_SUPPORT)
# Do garbage collection
CFLAGS+= -ffunction-sections -fdata-sections
CFLAGS+= -Wl,--gc-sections
# Link USB BOOT library
LDADD+= ${BOOTOBJ}/usb/libusbboot.a
CFLAGS+= -I${BOOTSRC}/usb
# Define USB SUPPORT
CFLAGS+= -DLOADER_USB_SUPPORT
.endif

all: loader

.include <bsd.prog.mk>
