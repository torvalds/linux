#/*
# * Copyright (c) 2017-2018 Cavium, Inc.
# * All rights reserved.
# *
# *  Redistribution and use in source and binary forms, with or without
# *  modification, are permitted provided that the following conditions
# *  are met:
# *
# *  1. Redistributions of source code must retain the above copyright
# *     notice, this list of conditions and the following disclaimer.
# *  2. Redistributions in binary form must reproduce the above copyright
# *     notice, this list of conditions and the following disclaimer in the
# *     documentation and/or other materials provided with the distribution.
# *
# *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
# *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# *  POSSIBILITY OF SUCH DAMAGE.
# */
#/*
# * File : Makefile
# * Author : David C Somayajulu, Cavium, Inc., San Jose, CA 95131.
# */
#
# $FreeBSD$
#

#.PATH:  ${.CURDIR}
#OFEDDIR= /usr/src/sys
#ETHDRVR=${.CURDIR}/../qlnxe

.PATH: ${SRCTOP}/sys/dev/qlnx/qlnxr
OFEDDIR=${SRCTOP}/sys
ETHDRVR=${SRCTOP}/sys/dev/qlnx/qlnxe

KMOD=   qlnxr
SRCS=   device_if.h bus_if.h vnode_if.h pci_if.h \
        opt_inet.h opt_inet6.h \
        qlnxr_os.c\
	qlnxr_cm.c\
	qlnxr_verbs.c

.include <bsd.kmod.mk>

CFLAGS+= -I${.CURDIR}
CFLAGS+= -I${ETHDRVR}
CFLAGS+= -I${OFEDDIR}/ofed/include
CFLAGS+= -I${OFEDDIR}/ofed/include/uapi
CFLAGS+= -I${OFEDDIR}/compat/linuxkpi/common/include

CFLAGS+= -DLINUX_TYPES_DEFINED
CFLAGS+= -DCONFIG_INFINIBAND_USER_MEM
CFLAGS+= -DINET6 -DINET
#CFLAGS+= -DDEFINE_NO_IP_BASED_GIDS

CWARNEXTRA += -Wno-cast-qual
CWARNEXTRA += -Wno-unused-function
.if ${COMPILER_TYPE} == "clang"
CWARNEXTRA += -Wno-gnu-variable-sized-type-not-at-end
.endif
CWARNEXTRA += -Wno-missing-prototypes
CWARNEXTRA += ${NO_WCONSTANT_CONVERSION}
CWARNEXTRA += -Wno-format

CWARNEXTRA += ${NO_WSHIFT_COUNT_OVERFLOW}
CWARNEXTRA += -Wno-empty-body

CFLAGS += -DQLNX_DEBUG
CFLAGS += -DECORE_PACKAGE
CFLAGS += -DCONFIG_ECORE_L2
CFLAGS += -DCONFIG_ECORE_LL2
CFLAGS += -DCONFIG_ECORE_ROCE
CFLAGS += -DCONFIG_ECORE_IWARP
CFLAGS += -DCONFIG_ECORE_RDMA
CFLAGS += -DECORE_CONFIG_DIRECT_HWFN
CFLAGS += -g -fno-inline
CFLAGS += -DQLNX_RDMA

CFLAGS+= -Wno-cast-qual -Wno-pointer-arith

