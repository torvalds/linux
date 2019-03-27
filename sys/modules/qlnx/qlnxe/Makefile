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

.PATH: ${SRCTOP}/sys/dev/qlnx/qlnxe

#.PATH: ${.CURDIR}

KMOD=if_qlnxe

SRCS=ecore_cxt.c ecore_dcbx.c ecore_dev.c ecore_hw.c 
SRCS+=ecore_init_fw_funcs.c ecore_int.c ecore_mcp.c
SRCS+=ecore_sp_commands.c ecore_spq.c ecore_l2.c
SRCS+=ecore_init_ops.c ecore_dbg_fw_funcs.c
SRCS+=ecore_mng_tlv.c

SRCS+=ecore_sriov.c
SRCS+=ecore_vf.c

#roce/iwarp files. Compilation can be turned off roce/iwarp are not required.
# In other words if you don't need RDMA please comment out SRCS adds for
# ecore_rdma.c ecore_roce.c ecore_iwarp.c ecore_ooo.c ecore_ll2.c qlnx_rdma.c
SRCS+=ecore_rdma.c
SRCS+=ecore_roce.c
SRCS+=ecore_iwarp.c
SRCS+=ecore_ooo.c
SRCS+=ecore_ll2.c
SRCS+=qlnx_rdma.c


SRCS+=qlnx_ioctl.c
SRCS+=qlnx_os.c

SRCS+= device_if.h
SRCS+= bus_if.h
SRCS+= pci_if.h
SRCS+= pci_iov_if.h


.include <bsd.kmod.mk>

#CFLAGS += -DQLNX_DEBUG
CFLAGS += -DECORE_PACKAGE
CFLAGS += -DCONFIG_ECORE_L2
CFLAGS += -DECORE_CONFIG_DIRECT_HWFN

CFLAGS+= -I${SRCTOP}/sys/compat/linuxkpi/common/include

#CFLAGS += -g
#CFLAGS += -fno-inline

#CFLAGS += -DQLNX_SOFT_LRO
#CFLAGS += -DQLNX_QSORT_LRO
#CFLAGS += -DQLNX_MAX_COALESCE
#CFLAGS += -DQLNX_USER_LLDP

CFLAGS += -DCONFIG_ECORE_SRIOV

# For roce/iwarp files. Compilation can be turned off if roce/iwarp are not required.
# In other words if you don't need RDMA please comment out the CFLAGS which define
# CONFIG_ECORE_LL2 CONFIG_ECORE_ROCE CONFIG_ECORE_IWARP QLNX_ENABLE_IWARP
CFLAGS += -DCONFIG_ECORE_LL2
CFLAGS += -DCONFIG_ECORE_ROCE
CFLAGS += -DCONFIG_ECORE_IWARP
CFLAGS += -DCONFIG_ECORE_RDMA
CFLAGS += -DQLNX_ENABLE_IWARP

CWARNFLAGS+= -Wno-cast-qual
